/*
 *	linux/arch/ppc/kernel/irq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *      Adapted from arch/i386 by Gary Thomas
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

/*
 * For the BeBox, interrupt numbers are 0..15 for 8259 PIC interrupts
 * and 16..31 for other BeBox motherboard type interrupts.
 */
 
unsigned long isBeBox[];
unsigned char *BeBox_IO_page;

static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

void disable_irq(unsigned int irq_nr)
{
	unsigned char mask;
	int s = _disable_interrupts();

	if (isBeBox[0] && (irq_nr >= 16))
	{
		BeBox_disable_irq(irq_nr);
	} else
	{
		mask = 1 << (irq_nr & 7);
		if (irq_nr < 8) {
			cache_21 |= mask;
			outb(cache_21,0x21);
		} else
		{
			cache_A1 |= mask;
			outb(cache_A1,0xA1);
		}
	}
	_enable_interrupts(s);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned char mask;
	int s = _disable_interrupts();

	if (isBeBox[0] && (irq_nr >= 16))
	{
		BeBox_enable_irq(irq_nr);
		_enable_interrupts(s);
		return;
	} else
	{
		mask = ~(1 << (irq_nr & 7));
		if (irq_nr < 8) {
			cache_21 &= mask;
			outb(cache_21,0x21);
		} else
		{
			cache_A1 &= mask;
			outb(cache_A1,0xA1);
		}
	}
	_enable_interrupts(s);
}

/*
 * Irq handlers.
 */
struct irq_action {
	void (*handler)(int, void *dev, struct pt_regs *);
	unsigned long flags;
	unsigned long mask;
	const char *name;
	int notified;
	void *dev_id;
};

static struct irq_action irq_action[32] = {
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
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
	struct irq_action * action = irq_action;

	for (i = 0;  i < 132;  i++, action++) {
		if (!action->handler)
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s\n",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
	}
	return len;
}

asmlinkage void handle_IRQ(struct pt_regs *regs)
{
	int irq, _irq, s;
	struct irq_action *action;
	intr_count++;
	if (!isBeBox[0] || ((irq = BeBox_irq()) < 16))
	{
		/* Figure out IRQ#, etc. */
		outb(0x0C, 0x20);  /* Poll interrupt controller */
		irq = _irq = inb(0x20);
		irq &= 0x07;  /* Caution! */
		if (irq == 2)
		{ /* Cascaded interrupt -> IRQ8..IRQ15 */
			outb(0x0C, 0xA0);
			irq = (_irq = inb(0xA0)) & 0x07;
			irq += 8;
		}
		/* Mask interrupt & Issue EOI to interrupt controller */
		if (irq > 7)
		{
			cache_A1 |= (1<<(irq-8));
			outb(cache_A1, 0xA1);
#if 0			
			outb(0x20, 0xA0);
			/* Need to ack cascade controller as well */
			outb(0x20, 0x20);
#else			
			outb(0x60|(irq-8), 0xA0);	/* Specific EOI */
			/* Need to ack cascade controller as well */
			outb(0x62, 0x20);
#endif			
		} else
		{
			cache_21 |= (1<<irq);
			outb(cache_21, 0x21);
			outb(0x20, 0x20);
		}
	}
	action = irq + irq_action;
	kstat.interrupts[irq]++;
	if (action->handler)
	{
		action->handler(irq, action->dev_id, regs);
	} else
	{
		printk("Bogus interrupt #%d/%x, PC: %x\n", irq, _irq, regs->nip);
	}
	if (_disable_interrupts() && !action->notified)
	{
		action->notified = 1;
		printk("*** WARNING! %s handler [IRQ %d] turned interrupts on!\n", action->name, irq);
	}
	if (irq < 16)
	{
		if (!(action->flags & SA_ONESHOT))
		{
			/* Re-enable interrupt */
			if (irq > 7)
			{
				cache_A1 &= ~(1<<(irq-8));
				outb(cache_A1, 0xA1);
			} else
			{
				cache_21 &= ~(1<<irq);
				outb(cache_21, 0x21);
			}
		}
	} else
	{
		BeBox_enable_irq(irq);
	}
	intr_count--;
}

/*
 * This routine gets called when the SCSI times out on an operation.
 * I don't know why this happens, but every so often it does and it
 * seems to be a problem with the interrupt controller [state].  It
 * happens a lot when there is also network activity (both devices
 * are on the PCI bus with interrupts on the cascaded controller).
 * Re-initializing the interrupt controller [which might lose some
 * pending edge detected interrupts] seems to fix it.
 */
check_irq()
{
	int s;
	unsigned char _a0, _a1, _20, _21;
	if (isBeBox[0])
	{
		return;
	}
	s = _disable_interrupts();
	_a1 = inb(0xA1);
	_21 = inb(0x21);
	outb(0x0C, 0x20);  _20 = inb(0x20);	
	outb(0x0C, 0xA0);  _a0 = inb(0xA0);
#if 0	
	printk("IRQ 0x20 = %x, 0x21 = %x/%x, 0xA0 = %x, 0xA1 = %x/%x\n",
		_20, _21, cache_21, _a0, _a1, cache_A1);
#endif		
	/* Reset interrupt controller - see if this fixes it! */
	/* Initialize interrupt controllers */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x40, 0x21); /* Vector base */
	outb(0x04, 0x21); /* Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */
	outb(0xFF, 0x21); /* Mask all */
	outb(0x00, 0x4D0); /* All edge triggered */
	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x48, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* Cascade (slave) on IRQ2 */
	outb(0x01, 0xA1); /* Select 8086 mode */
	outb(0xFF, 0xA1); /* Mask all */
	outb(0xCF, 0x4D1); /* Trigger mode */
	outb(cache_A1, 0xA1);
	outb(cache_21, 0x21);
	enable_irq(2);  /* Enable cascade interrupt */
	_enable_interrupts(s);
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irq_action * action;
	unsigned long flags;

#if 0
_printk("Request IRQ #%d, Handler: %x\n", irq, handler);
cnpause();
#endif
	if (irq > 15)
	{
		if (!isBeBox[0] || (irq > 31))
			return -EINVAL;
	}
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
	action->dev_id = dev_id;
	enable_irq(irq);
	restore_flags(flags);
	return 0;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irq_action * action = irq + irq_action;
	unsigned long flags;

	if (irq > 31) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (!action->handler) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	disable_irq(irq);
	save_flags(flags);
	cli();
	action->handler = NULL;
	action->flags = 0;
	action->mask = 0;
	action->name = NULL;
	action->dev_id = NULL;
	restore_flags(flags);
}

#define SA_PROBE SA_ONESHOT

static void no_action(int irq, void *dev, struct pt_regs * regs)
{
#ifdef DEBUG
	printk("Probe got IRQ: %d\n", irq);
#endif
}

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

	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	if (request_irq(2, no_action, SA_INTERRUPT, "cascade", NULL))
		printk("Unable to get IRQ2 for cascade\n");
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");

	/* Set up PCI interrupts */
	route_PCI_interrupts();

	if (isBeBox[0])
	{
		BeBox_init_IRQ();
	}
}

/*
 * Wrapper for "bottom 1/2" of interrupt processing.  This routine
 * is called whenever an interrupt needs non-interrupt-time service.
 */

_do_bottom_half()
{
	_enable_interrupts(1);
	do_bottom_half();
	_disable_interrupts();
}
 

/*
 * Support for interrupts on the BeBox
 */

#define CPU0_INT_MASK	(volatile unsigned long *)(BeBox_IO_page+0x0F0)
#define CPU1_INT_MASK	(volatile unsigned long *)(BeBox_IO_page+0x1F0)
#define INT_SOURCE	(volatile unsigned long *)(BeBox_IO_page+0x2F0)
#define CPU_RESET	(volatile unsigned long *)(BeBox_IO_page+0x4F0)

#define CPU_HRESET	0x20000000
#define CPU_SRESET	0x40000000

#define SCSI_IRQ	16

#define INT_SCSI	(1<<21)
#define INT_8259	(1<<5)

/*
 * Map of pseudo IRQs to actual bits
 * Note: We give out IRQ #16..31 for all interrupt sources which are
 * not found in the 8259 PIC.
 */
 
unsigned long BeBox_IRQ_map[] =
   {
   	INT_SCSI,	/* 16 - SCSI */
   	0x00000000,	/* 17 - Unused */
   	0x00000000,	/* 18 - Unused */
   	0x00000000,	/* 19 - Unused */
   	0x00000000,	/* 20 - Unused */
   	0x00000000,	/* 21 - Unused */
   	0x00000000,	/* 22 - Unused */
   	0x00000000,	/* 23 - Unused */
   	0x00000000,	/* 24 - Unused */
   	0x00000000,	/* 25 - Unused */
   	0x00000000,	/* 26 - Unused */
   	0x00000000,	/* 27 - Unused */
   	0x00000000,	/* 28 - Unused */
   	0x00000000,	/* 29 - Unused */
   	0x00000000,	/* 30 - Unused */
   	0x00000000,	/* 31 - Unused */
   };

volatile int CPU1_alive;
volatile int CPU1_trace;

static
_NOP()
{
}

static
_delay()
{
	int i;
	for (i = 0;  i < 100;  i++) _NOP();
}

void
BeBox_init_IRQ(void)
{
	int tmr;
	volatile extern long BeBox_CPU1_vector;
	*CPU0_INT_MASK = 0x0FFFFFFC;  /* Clear all bits? */	
	*CPU0_INT_MASK = 0x80000003 | INT_8259;
	*CPU1_INT_MASK = 0x0FFFFFFC;  
printk("Start CPU #1 - CPU Status: %x\n", *CPU_RESET);
	BeBox_CPU1_vector = 0x0100;  /* Reset */
	tmr = 0;
	while (CPU1_alive == 0)
	{
		if (++tmr == 1000)
		{
printk("CPU #1 not there? - CPU Status: %x, Trace: %x\n", *CPU_RESET, CPU1_trace);
			break;
		}
		_delay();
	}
printk("CPU #1 running!\n");
#if 0
/* Temp - for SCSI */
	*(unsigned char *)0x81000038 = 0x00;
	*(unsigned char *)0x8080103C = 0xFF;
	*(unsigned char *)0x8080100D = 0x32;
#endif	
}

void
BeBox_disable_irq(int irq)
{
	/* Note: this clears the particular bit */
	*CPU0_INT_MASK = BeBox_IRQ_map[irq-16];
}

void
BeBox_enable_irq(int irq)
{
	int s = _disable_interrupts();
	/* Sets a single bit */
#if 0	
printk("BeBox IRQ Mask = %x", *CPU0_INT_MASK);
#endif
	*CPU0_INT_MASK = 0x80000000 | BeBox_IRQ_map[irq-16];
#if 0
printk("/%x\n", *CPU0_INT_MASK);
#endif	
	_enable_interrupts(s);	
}

int
BeBox_irq(void)
{
	int i;
	unsigned long cpu0_int_mask;
	unsigned long int_state;
	cpu0_int_mask = (*CPU0_INT_MASK & 0x0FFFFFFC) & ~INT_8259;
	int_state = cpu0_int_mask & *INT_SOURCE;
	if (int_state)
	{ /* Determine the pseudo-interrupt # */
#if 0	
		printk("Ints[%x] = %x, Mask[%x] = %x/%x, State = %x\n", INT_SOURCE, *INT_SOURCE, CPU0_INT_MASK, *CPU0_INT_MASK, cpu0_int_mask, int_state);
#endif		
		for (i = 0;  i < 16;  i++)
		{
			if (BeBox_IRQ_map[i] & int_state)
			{
				return (i+16);
			}
		}
printk("Ints[%x] = %x, Mask[%x] = %x/%x, State = %x\n", INT_SOURCE, *INT_SOURCE, CPU0_INT_MASK, *CPU0_INT_MASK, cpu0_int_mask, int_state);
printk("Can't find BeBox IRQ!\n");
	}
	return (0);
}

BeBox_state()
{
	printk("Int state = %x, CPU0 mask = %x, CPU1 mask = %x\n", *INT_SOURCE, *CPU0_INT_MASK, *CPU1_INT_MASK);
}

BeBox_CPU1()
{
	CPU1_alive++;
	while (1) ;
}
