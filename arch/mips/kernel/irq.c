/* $Id: irq.c,v 1.13 1998/05/28 03:17:55 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Code to handle x86 style IRQs plus some generic interrupt stuff.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 Ralf Baechle
 */
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
#include <asm/mipsregs.h>
#include <asm/system.h>

unsigned char cache_21 = 0xff;
unsigned char cache_A1 = 0xff;

unsigned int local_bh_count[NR_CPUS];
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
			i, kstat.irqs[0][i],
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

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction *action;
	int do_random, cpu;

	cpu = smp_processor_id();
	irq_enter(cpu, irq);
	kstat.irqs[cpu][irq]++;

	/*
	 * mask and ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * Commented out because we've already done this in the
	 * machinespecific part of the handler.  It's reasonable to
	 * do this here in a highlevel language though because that way
	 * we could get rid of a good part of duplicated code ...
	 */
        /* mask_and_ack_irq(irq); */

	action = *(irq + irq_action);
	if (action) {
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		action = *(irq + irq_action);
		do_random = 0;
        	do {
			do_random |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
        	} while (action);
		if (do_random & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		unmask_irq (irq);
		__cli();
	}
	irq_exit(cpu, irq);

	/* unmasking and bottom half handling is done magically for us. */
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

	if (irq >= 32)
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
		if (!irq[irq_action])
			mask_irq(irq);
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
	for (delay = jiffies + HZ/10; time_before(jiffies, delay); )
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

int (*irq_cannonicalize)(int irq);

static int i8259a_irq_cannonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}

__initfunc(void init_IRQ(void))
{
	irq_cannonicalize = i8259a_irq_cannonicalize;
	irq_setup();
}
