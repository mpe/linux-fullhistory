/* started hacking from linux-2.3.30pre6/arch/i386/kernel/i8259.c */

#include <linux/init.h>
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/delay.h>

/*
 * This is the 'legacy' 8259A Programmable Interrupt Controller,
 * present in the majority of PC/AT boxes.
 */

static void enable_8259A_irq(unsigned int irq);
static void disable_8259A_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define end_8259A_irq		enable_8259A_irq
#define shutdown_8259A_irq	disable_8259A_irq

static void mask_and_ack_8259A(unsigned int);

static unsigned int startup_8259A_irq(unsigned int irq)
{
	enable_8259A_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type i8259A_irq_type = {
	"XT-PIC",
	startup_8259A_irq,
	shutdown_8259A_irq,
	enable_8259A_irq,
	disable_8259A_irq,
	mask_and_ack_8259A,
	end_8259A_irq
};

/*
 * 8259A PIC functions to handle ISA devices:
 */

/*
 * This contains the irq mask for both 8259A irq controllers,
 */
static unsigned int cached_irq_mask = 0xffff;

#define __byte(x,y) 	(((unsigned char *)&(y))[x])
#define cached_21	(__byte(0,cached_irq_mask))
#define cached_A1	(__byte(1,cached_irq_mask))

/*
 * These have to be protected by the irq controller spinlock
 * before being called.
 */
static void disable_8259A_irq(unsigned int irq)
{
	unsigned int mask = 1 << irq;
	cached_irq_mask |= mask;
	if (irq & 8)
		outb(cached_A1,0xA1);
	else
		outb(cached_21,0x21);
}

static void enable_8259A_irq(unsigned int irq)
{
	unsigned int mask = ~(1 << irq);
	cached_irq_mask &= mask;
	if (irq & 8)
		outb(cached_A1,0xA1);
	else
		outb(cached_21,0x21);
}

static void mask_and_ack_8259A(unsigned int irq)
{
	disable_8259A_irq(irq);

	/* Ack the interrupt making it the lowest priority */
	/*  First the slave .. */
	if (irq > 7) {
		outb(0xE0 | (irq - 8), 0xa0);
		irq = 2;
	}
	/* .. then the master */
	outb(0xE0 | irq, 0x20);
}

static void init_8259A(void)
{
	outb(0xff, 0x21);	/* mask all of 8259A-1 */
	outb(0xff, 0xA1);	/* mask all of 8259A-2 */
}

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2 = { no_action, 0, 0, "cascade", NULL, NULL};

void __init
init_ISA_irqs (void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		if (i == RTC_IRQ)
			continue;
		if (i >= 16)
			break;
		irq_desc[i].status = IRQ_DISABLED;
		/*
		 * 16 old-style INTA-cycle interrupts:
		 */
		irq_desc[i].handler = &i8259A_irq_type;
	}

	init_8259A();
	setup_irq(2, &irq2);
}
