/*
 *	linux/arch/alpha/kernel/irq.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

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
	halt();
}

static void handle_irq(int irq, struct pt_regs * regs)
{
	struct irqaction * action = irq + irq_action;

	kstat.interrupts[irq]++;
	if (action->handler)
		action->handler(irq, regs);
}

/*
 * I don't have any good documentation on the EISA hardware interrupt
 * stuff: I don't know the mapping between the interrupt vector and the
 * EISA interrupt number.
 *
 * It *seems* to be 0x8X0 for EISA interrupt X, and 0x9X0 for the
 * local motherboard interrupts..
 *
 *	0x660 - NMI?
 *
 *	0x800 - ??? I've gotten this, but EISA irq0 shouldn't happen
 *		as the timer is not on the EISA bus
 *
 *	0x860 - ??? floppy disk (EISA irq6)
 *
 *	0x900 - ??? I get this at autoprobing when the EISA serial
 *		lines com3/com4 don't exist. It keeps coming after
 *		that..
 *
 *	0x980 - keyboard
 *	0x990 - mouse
 *
 * We'll see..
 */
static void device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int i;
	static int nr = 0;

	if (vector == 0x980 && irq_action[1].handler) {
		handle_irq(1, regs);
		return;
	}

	if (nr > 3)
		return;
	nr++;
	printk("IO device interrupt, vector = %lx\n", vector);
	printk("PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
	printk("Expecting: ");
	for (i = 0; i < 16; i++)
		if (irq_action[i].handler)
			printk("[%s:%d] ", irq_action[i].name, i);
	printk("\n");
	printk("64=%02x, 60=%02x, 3fa=%02x 2fa=%02x\n",
		inb(0x64), inb(0x60), inb(0x3fa), inb(0x2fa));
	printk("61=%02x, 461=%02x\n", inb(0x61), inb(0x461));
}

static void machine_check(unsigned long vector, unsigned long la_ptr, struct pt_regs * regs)
{
	printk("Machine check\n");
}

asmlinkage void do_entInt(unsigned long type, unsigned long vector,
	unsigned long la_ptr, struct pt_regs *regs)
{
	switch (type) {
		case 0:
			printk("Interprocessor interrupt? You must be kidding\n");
			break;
		case 1:
			/* timer interrupt.. */
			handle_irq(0, regs);
			return;
		case 2:
			machine_check(vector, la_ptr, regs);
			break;
		case 3:
			device_interrupt(vector, regs);
			return;
		case 4:
			printk("Performance counter interrupt\n");
			break;;
		default:
			printk("Hardware intr %ld %lx? Huh?\n", type, vector);
	}
	printk("PC = %016lx PS=%04lx\n", regs->pc, regs->ps);
}

extern asmlinkage void entInt(void);

void init_IRQ(void)
{
	wrent(entInt, 0);
}
