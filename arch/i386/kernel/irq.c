/*
 *	linux/arch/i386/kernel/irq.c
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

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#define CR0_NE 32

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

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
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that do all
 * the operations that are needed to keep the AT interrupt-controller
 * happy. They are also written to be fast - and to disable interrupts
 * as little as humanly possible.
 *
 * NOTE! These macros expand to three different handlers for each line: one
 * complete handler that does all the fancy stuff (including signal handling),
 * and one fast handler that is meant for simple IRQ's that want to be
 * atomic. The specific handler is chosen depending on the SA_INTERRUPT
 * flag when installing a handler. Finally, one "bad interrupt" handler, that
 * is used when no handler is present.
 */
BUILD_IRQ(FIRST,0,0x01)
BUILD_IRQ(FIRST,1,0x02)
BUILD_IRQ(FIRST,2,0x04)
BUILD_IRQ(FIRST,3,0x08)
BUILD_IRQ(FIRST,4,0x10)
BUILD_IRQ(FIRST,5,0x20)
BUILD_IRQ(FIRST,6,0x40)
BUILD_IRQ(FIRST,7,0x80)
BUILD_IRQ(SECOND,8,0x01)
BUILD_IRQ(SECOND,9,0x02)
BUILD_IRQ(SECOND,10,0x04)
BUILD_IRQ(SECOND,11,0x08)
BUILD_IRQ(SECOND,12,0x10)
BUILD_IRQ(SECOND,13,0x20)
BUILD_IRQ(SECOND,14,0x40)
BUILD_IRQ(SECOND,15,0x80)

/*
 * Pointers to the low-level handlers: first the general ones, then the
 * fast ones, then the bad ones.
 */
static void (*interrupt[16])(void) = {
	IRQ0_interrupt, IRQ1_interrupt, IRQ2_interrupt, IRQ3_interrupt,
	IRQ4_interrupt, IRQ5_interrupt, IRQ6_interrupt, IRQ7_interrupt,
	IRQ8_interrupt, IRQ9_interrupt, IRQ10_interrupt, IRQ11_interrupt,
	IRQ12_interrupt, IRQ13_interrupt, IRQ14_interrupt, IRQ15_interrupt
};

static void (*fast_interrupt[16])(void) = {
	fast_IRQ0_interrupt, fast_IRQ1_interrupt,
	fast_IRQ2_interrupt, fast_IRQ3_interrupt,
	fast_IRQ4_interrupt, fast_IRQ5_interrupt,
	fast_IRQ6_interrupt, fast_IRQ7_interrupt,
	fast_IRQ8_interrupt, fast_IRQ9_interrupt,
	fast_IRQ10_interrupt, fast_IRQ11_interrupt,
	fast_IRQ12_interrupt, fast_IRQ13_interrupt,
	fast_IRQ14_interrupt, fast_IRQ15_interrupt
};

static void (*bad_interrupt[16])(void) = {
	bad_IRQ0_interrupt, bad_IRQ1_interrupt,
	bad_IRQ2_interrupt, bad_IRQ3_interrupt,
	bad_IRQ4_interrupt, bad_IRQ5_interrupt,
	bad_IRQ6_interrupt, bad_IRQ7_interrupt,
	bad_IRQ8_interrupt, bad_IRQ9_interrupt,
	bad_IRQ10_interrupt, bad_IRQ11_interrupt,
	bad_IRQ12_interrupt, bad_IRQ13_interrupt,
	bad_IRQ14_interrupt, bad_IRQ15_interrupt
};

/*
 * Initial irq handlers.
 */
struct irqaction {
	void (*handler)(int, struct pt_regs *);
	unsigned long flags;
	unsigned long mask;
	const char *name;
};

static struct irqaction irq_action[16] = {
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL }
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action = irq_action;

	for (i = 0 ; i < 16 ; i++, action++) {
		if (!action->handler)
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s\n",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
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
	struct irqaction * action = irq + irq_action;

	kstat.interrupts[irq]++;
	action->handler(irq, regs);
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
asmlinkage void do_fast_IRQ(int irq)
{
	struct irqaction * action = irq + irq_action;

	kstat.interrupts[irq]++;
	action->handler(irq, NULL);
}

#define SA_PROBE SA_ONESHOT

int request_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
	unsigned long irqflags, const char * devname)
{
	struct irqaction * action;
	unsigned long flags;

	if (irq > 15)
		return -EINVAL;
	action = irq + irq_action;
	if (action->handler)
		return -EBUSY;
	if (!handler)
		return -EINVAL;
	save_flags(flags);
	cli();
	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	if (!(action->flags & SA_PROBE)) { /* SA_ONESHOT is used by probing */
		if (action->flags & SA_INTERRUPT)
			set_intr_gate(0x20+irq,fast_interrupt[irq]);
		else
			set_intr_gate(0x20+irq,interrupt[irq]);
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
	restore_flags(flags);
	return 0;
}
		
void free_irq(unsigned int irq)
{
	struct irqaction * action = irq + irq_action;
	unsigned long flags;

	if (irq > 15) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (!action->handler) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	save_flags(flags);
	cli();
	if (irq < 8) {
		cache_21 |= 1 << irq;
		outb(cache_21,0x21);
	} else {
		cache_A1 |= 1 << (irq-8);
		outb(cache_A1,0xA1);
	}
	set_intr_gate(0x20+irq,bad_interrupt[irq]);
	action->handler = NULL;
	action->flags = 0;
	action->mask = 0;
	action->name = NULL;
	restore_flags(flags);
}

/*
 * Note that on a 486, we don't want to do a SIGFPE on a irq13
 * as the irq is unreliable, and exception 16 works correctly
 * (ie as explained in the intel literature). On a 386, you
 * can't use exception 16 due to bad IBM design, so we have to
 * rely on the less exact irq13.
 *
 * Careful.. Not only is IRQ13 unreliable, but it is also
 * leads to races. IBM designers who came up with it should
 * be shot.
 */
static void math_error_irq(int cpl, struct pt_regs *regs)
{
	outb(0,0xF0);
	if (ignore_irq13 || !hard_math)
		return;
	math_error();
}

static void no_action(int cpl, struct pt_regs * regs) { }

unsigned int probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
	unsigned long delay;

	/* first, snaffle up any unassigned irqs */
	for (i = 15; i > 0; i--) {
		if (!request_irq(i, no_action, SA_PROBE, "probe")) {
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
			free_irq(i);
		}
	}
#ifdef DEBUG
	printk("probe_irq_on:  irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	return irqs;
}

int probe_irq_off (unsigned int irqs)
{
	unsigned int i, irqmask;

	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i)) {
			free_irq(i);
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

	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	for (i = 0; i < 16 ; i++)
		set_intr_gate(0x20+i,bad_interrupt[i]);
	if (request_irq(2, no_action, SA_INTERRUPT, "cascade"))
		printk("Unable to get IRQ2 for cascade\n");
	if (request_irq(13,math_error_irq, 0, "math error"))
		printk("Unable to get IRQ13 for math-error handler\n");
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
} 
