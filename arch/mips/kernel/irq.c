/*
 *	linux/arch/mips/kernel/irq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * Mips support by Ralf Baechle and Andreas Busse
 *
 * $Id: irq.c,v 1.6 1997/06/30 15:52:34 ralf Exp $
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/jazz.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/vector.h>
#ifdef CONFIG_SGI
#include <asm/sgialib.h>
#endif

unsigned char cache_21 = 0xff;
unsigned char cache_A1 = 0xff;

unsigned int local_irq_count[NR_CPUS];
unsigned long spurious_count = 0;

/*
 * (un)mask_irq, disable_irq() and enable_irq() only handle (E)ISA and
 * PCI devices.  Other onboard hardware needs specific routines.
 */
static inline void mask_irq(unsigned int irq_nr)
{
	unsigned char mask;

	mask = 1 << (irq_nr & 7);
	if (irq_nr < 8) {
		cache_21 |= mask;
		outb(cache_21,0x21);
	} else {
		cache_A1 |= mask;
		outb(cache_A1,0xA1);
	}
}

static inline void unmask_irq(unsigned int irq_nr)
{
	unsigned char mask;

	mask = ~(1 << (irq_nr & 7));
	if (irq_nr < 8) {
		cache_21 &= mask;
		outb(cache_21,0x21);
	} else {
		cache_A1 &= mask;
		outb(cache_A1,0xA1);
	}
}

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	mask_irq(irq_nr);
	restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	save_and_cli(flags);
	unmask_irq(irq_nr);
	restore_flags(flags);
}

/*
 * Pointers to the low-level handlers: first the general ones, then the
 * fast ones, then the bad ones.
 */
extern void interrupt(void);
extern void fast_interrupt(void);
extern void bad_interrupt(void);

static struct irqaction *irq_action[32] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < 32 ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}

atomic_t __mips_bh_counter;

#ifdef __SMP__
#error Send superfluous SMP boxes to ralf@uni-koblenz.de
#else
#define irq_enter(cpu, irq)     (++local_irq_count[cpu])
#define irq_exit(cpu, irq)      (--local_irq_count[cpu])
#endif

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction * action = *(irq + irq_action);
	int do_random, cpu = smp_processor_id();

	irq_enter(cpu, irq);
	kstat.interrupts[irq]++;

	/* slow interrupts run with interrupts enabled */
	sti();
	action = *(irq + irq_action);
	do_random = 0;
        while (action) {
		do_random |= action->flags;
		action->handler(irq, action->dev_id, regs);
		action = action->next;
        }
	if (do_random & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	irq_exit(cpu, irq);
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void do_fast_IRQ(int irq)
{
	struct irqaction * action;
	int do_random, cpu = smp_processor_id();

	irq_enter(cpu, irq);
	kstat.interrupts[irq]++;
	action = *(irq + irq_action);
	do_random = 0;
	while (action) {
		do_random |= action->flags;
		action->handler(irq, action->dev_id, NULL);
		action = action->next;
	}
	if (do_random & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	irq_exit(cpu, irq);
}

/*
 * Used only for setup of PC style interrupts and therefore still
 * called setup_x86_irq.  Later on I'll provide a machine specific
 * function with similar purpose.  Idea is to put all interrupts
 * in a single table and differenciate them just by number.
 */
int setup_x86_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	p = irq_action + irq;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ))
			return -EBUSY;

		/* Can't share interrupts unless both are same type */
		if ((old->flags ^ new->flags) & SA_INTERRUPT)
			return -EBUSY;

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	if (new->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	save_and_cli(flags);
	*p = new;

	if (!shared) {
		if (new->flags & SA_INTERRUPT)
			set_int_vector(irq,fast_interrupt);
		else
			set_int_vector(irq,interrupt);
		unmask_irq(irq);
	}
	restore_flags(flags);
	return 0;
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq > 31)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_x86_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq > 31) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_and_cli(flags);
		*p = action->next;
		if (!irq[irq_action]) {
			mask_irq(irq);
			set_int_vector(irq, bad_interrupt);
		}
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
	unsigned long delay;

	/* first, enable any unassigned (E)ISA irqs */
	for (i = 15; i > 0; i--) {
		if (!irq_action[i]) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + HZ/10; delay > jiffies; )
		/* about 100ms delay */;

	/* now filter out any obviously spurious interrupts */
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	return irqs & ~irqmask;
}

int probe_irq_off (unsigned long irqs)
{
	unsigned int i, irqmask;

	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
#ifdef DEBUG
	printk("probe_irq_off: irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	irqs &= irqmask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (irqs & (1 << i)))
		i = -i;
	return i;
}

__initfunc(void init_IRQ(void))
{
	int i;

	for (i = 0; i < 32 ; i++)
		set_int_vector(i, bad_interrupt);
	irq_setup();
}
