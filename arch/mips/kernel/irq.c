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
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

/*
 * Mips support by Ralf Baechle and Andreas Busse
 *
 * The Deskstation Tyne is almost completely like an IBM compatible PC with
 * another type of microprocessor. Therefore this code is almost completely
 * the same. More work needs to be done to support Acer PICA and other
 * machines.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
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

#define TIMER_IRQ 0                     /* Keep this in sync with time.c */

unsigned char cache_21 = 0xff;
unsigned char cache_A1 = 0xff;

unsigned long spurious_count = 0;

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	mask = 1 << (irq_nr & 7);
	save_flags(flags);
	if (irq_nr < 8) {
		cli();
		cache_21 |= mask;
		outb(cache_21,0x21);
		restore_flags(flags);
		return;
	}
	cli();
	cache_A1 |= mask;
	outb(cache_A1,0xA1);
	restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	mask = ~(1 << (irq_nr & 7));
	save_flags(flags);
	if (irq_nr < 8) {
		cli();
		cache_21 &= mask;
		outb(cache_21,0x21);
		restore_flags(flags);
		return;
	}
	cli();
	cache_A1 &= mask;
	outb(cache_A1,0xA1);
	restore_flags(flags);
}

/*
 * Pointers to the low-level handlers: first the general ones, then the
 * fast ones, then the bad ones.
 */
extern void interrupt(void);
extern void fast_interrupt(void);
extern void bad_interrupt(void);

/*
 * Initial irq handlers.
 */
static struct irqaction timer_irq = { NULL, 0, 0, NULL, NULL, NULL};
static struct irqaction cascade_irq = { NULL, 0, 0, NULL, NULL, NULL};
static struct irqaction math_irq = { NULL, 0, 0, NULL, NULL, NULL};

static struct irqaction *irq_action[16] = {
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL,
	  NULL, NULL, NULL, NULL, NULL, NULL , NULL, NULL
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < 16 ; i++) {
	        action = *(i + irq_action);
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

	kstat.interrupts[irq]++;
	if (action->flags & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	while (action) {
	    action->handler(irq, action->dev_id, regs);
	    action = action->next;
	}
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void do_fast_IRQ(int irq)
{
	struct irqaction * action = *(irq + irq_action);

	kstat.interrupts[irq]++;
	if (action->flags & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	while (action) {
	    action->handler(irq, action->dev_id, NULL);
	    action = action->next;
	}
}

#define SA_PROBE SA_ONESHOT

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	struct irqaction * action, *tmp = NULL;
	unsigned long flags;

	if (irq > 15)
		return -EINVAL;
	if (!handler)
	    return -EINVAL;
	action = *(irq + irq_action);
	if (action) {
	    if ((action->flags & SA_SHIRQ) && (irqflags & SA_SHIRQ)) {
		for (tmp = action; tmp->next; tmp = tmp->next);
	    } else {
		return -EBUSY;
	    }
	    if ((action->flags & SA_INTERRUPT) ^ (irqflags & SA_INTERRUPT)) {
	      printk("Attempt to mix fast and slow interrupts on IRQ%d denied\n", irq);
	      return -EBUSY;
	    }   
	}
	if (irqflags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);
	save_flags(flags);
	cli();
	if (irq == 2)
	    action = &cascade_irq;
	else if (irq == 13)
	  action = &math_irq;
	else if (irq == TIMER_IRQ)
	  action = &timer_irq;
	else
	  action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);

	if (!action) { 
	    restore_flags(flags);
	    return -ENOMEM;
	}

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	if (tmp) {
	    tmp->next = action;
	} else {
	    *(irq + irq_action) = action;
	    if (!(action->flags & SA_PROBE)) {/* SA_ONESHOT used by probing */
		/*
		 * FIXME: Does the SA_INTERRUPT flag make any sense on MIPS???
		 */
		if (action->flags & SA_INTERRUPT)
			set_int_vector(irq,fast_interrupt);
		else
			set_int_vector(irq,interrupt);
	    }
	    if (irq < 8) {
		cache_21 &= ~(1<<irq);
		outb(cache_21,0x21);
	    } else {
		cache_21 &= ~(1<<2);
		cache_A1 &= ~(1<<(irq-8));
		outb(cache_21,0x21);
		outb(cache_A1,0xA1);
	    }
	}
	restore_flags(flags);
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action = *(irq + irq_action);
	struct irqaction * tmp = NULL;
	unsigned long flags;

	if (irq > 15) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (!action->handler) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	if (dev_id) {
	    for (; action; action = action->next) {
	        if (action->dev_id == dev_id) break;
		tmp = action;
	    }
	    if (!action) {
		printk("Trying to free free shared IRQ%d\n",irq);
		return;
	    }
	} else if (action->flags & SA_SHIRQ) {
	    printk("Trying to free shared IRQ%d with NULL device ID\n", irq);
	    return;
	}
	save_flags(flags);
	cli();
	if (action && tmp) {
	    tmp->next = action->next;
	} else {
	    *(irq + irq_action) = action->next;
	}

	if ((irq == 2) || (irq == 13) | (irq == TIMER_IRQ))
	  memset(action, 0, sizeof(struct irqaction));
	else 
	  kfree_s(action, sizeof(struct irqaction));
	
	if (!(*(irq + irq_action))) {
	    if (irq < 8) {
		cache_21 |= 1 << irq;
		outb(cache_21,0x21);
	    } else {
		cache_A1 |= 1 << (irq-8);
		outb(cache_A1,0xA1);
	    }
	    set_int_vector(irq,bad_interrupt);
	}
	restore_flags(flags);
}

static void no_action(int cpl, void *dev_id, struct pt_regs * regs) { }

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
	unsigned long delay;

	/* first, snaffle up any unassigned irqs */
	for (i = 15; i > 0; i--) {
		if (!request_irq(i, no_action, SA_PROBE, "probe", NULL)) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + 2; delay > jiffies; );	/* min 10ms delay */

	/* now filter out any obviously spurious interrupts */
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i) & irqmask) {
			irqs ^= (1 << i);
			free_irq(i, NULL);
		}
	}
#ifdef DEBUG
	printk("probe_irq_on:  irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	return irqs;
}

int probe_irq_off (unsigned long irqs)
{
	unsigned int i, irqmask;

	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i)) {
			free_irq(i, NULL);
		}
	}
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

void init_IRQ(void)
{
	int i;

	switch (boot_info.machtype) {
		case MACH_MIPS_MAGNUM_4000:
		case MACH_ACER_PICA_61:
	                r4030_write_reg16(JAZZ_IO_IRQ_ENABLE,
					  JAZZ_IE_ETHERNET |
					  JAZZ_IE_SERIAL1  |
					  JAZZ_IE_SERIAL2  |
 					  JAZZ_IE_PARALLEL |
					  JAZZ_IE_FLOPPY);
	                r4030_read_reg16(JAZZ_IO_IRQ_SOURCE); /* clear pending IRQs */
			set_cp0_status(ST0_IM, IE_IRQ4 | IE_IRQ1);
			/* set the clock to 100 Hz */
			r4030_write_reg32(JAZZ_TIMER_INTERVAL, 9);
			break;
		case MACH_DESKSTATION_TYNE:
			/* set the clock to 100 Hz */
			outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
			outb_p(LATCH & 0xff , 0x40);	/* LSB */
			outb(LATCH >> 8 , 0x40);	/* MSB */

			if (request_irq(2, no_action, SA_INTERRUPT, "cascade", NULL))
				printk("Unable to get IRQ2 for cascade\n");
			break;
		default:
			panic("Unknown machtype in init_IRQ");
	}

	for (i = 0; i < 16 ; i++)
		set_int_vector(i, bad_interrupt);

	/* initialize the bottom half routines. */
	for (i = 0; i < 32; i++) {
		bh_base[i].routine = NULL;
		bh_base[i].data = NULL;
	}
	bh_active = 0;
	atomic_set(&intr_count, 0);
}
