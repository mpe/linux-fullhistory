/*
 * linux/arch/sh/kernel/irq_onchip.c
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 * Interrupt handling for on-chip supporting modules (TMU, RTC, etc.).
 *
 */

#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/smp.h>
#include <asm/pgtable.h>
#include <asm/delay.h>

#include <linux/irq.h>


/*
 * SH (non-)specific no controller code
 */

static void enable_none(unsigned int irq) { }
static unsigned int startup_none(unsigned int irq) { return 0; }
static void disable_none(unsigned int irq) { }
static void ack_none(unsigned int irq)
{
}

/* startup is the same as "enable", shutdown is same as "disable" */
#define shutdown_none	disable_none
#define end_none	enable_none

struct hw_interrupt_type no_irq_type = {
	"none",
	startup_none,
	shutdown_none,
	enable_none,
	disable_none,
	ack_none,
	end_none
};

struct ipr_data {
	int offset;
	int priority;
};
static struct ipr_data ipr_data[NR_IRQS-TIMER_IRQ];

void set_ipr_data(unsigned int irq, int offset, int priority)
{
	ipr_data[irq-TIMER_IRQ].offset = offset;
	ipr_data[irq-TIMER_IRQ].priority = priority;
}

static void enable_onChip_irq(unsigned int irq);
void disable_onChip_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_onChip_irq	disable_onChip_irq

static void mask_and_ack_onChip(unsigned int);
static void end_onChip_irq(unsigned int irq);

static unsigned int startup_onChip_irq(unsigned int irq)
{ 
	enable_onChip_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type onChip_irq_type = {
	"On-Chip Supporting Module",
	startup_onChip_irq,
	shutdown_onChip_irq,
	enable_onChip_irq,
	disable_onChip_irq,
	mask_and_ack_onChip,
	end_onChip_irq
};

/*
 * These have to be protected by the irq controller spinlock
 * before being called.
 *
 *
 * IPRA  15-12  11-8  7-4  3-0
 * IPRB  15-12  11-8  7-4  3-0
 * IPRC  15-12  11-8  7-4  3-0
 *
 */
#define INTC_IPR	0xfffffee2UL	/* Word access */

void disable_onChip_irq(unsigned int irq)
{
	/* Set priority in IPR to 0 */
	int offset = ipr_data[irq-TIMER_IRQ].offset;
	unsigned long intc_ipr_address = INTC_IPR + offset/16;
	unsigned short mask = 0xffff ^ (0xf << (offset%16));
	unsigned long __dummy;

	asm volatile("mov.w	@%1,%0\n\t"
		     "and	%2,%0\n\t"
		     "mov.w	%0,@%1"
		     : "=&z" (__dummy)
		     : "r" (intc_ipr_address), "r" (mask)
		     : "memory" );
}

static void enable_onChip_irq(unsigned int irq)
{
	/* Set priority in IPR back to original value */
	int offset = ipr_data[irq-TIMER_IRQ].offset;
	int priority = ipr_data[irq-TIMER_IRQ].priority;
	unsigned long intc_ipr_address = INTC_IPR + offset/16;
	unsigned short value = (priority << (offset%16));
	unsigned long __dummy;

	asm volatile("mov.w	@%1,%0\n\t"
		     "or	%2,%0\n\t"
		     "mov.w	%0,@%1"
		     : "=&z" (__dummy)
		     : "r" (intc_ipr_address), "r" (value)
		     : "memory" );
}

void make_onChip_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &onChip_irq_type;
	enable_irq(irq);
}

static void mask_and_ack_onChip(unsigned int irq)
{
	disable_onChip_irq(irq);
	sti();
}

static void end_onChip_irq(unsigned int irq)
{
	enable_onChip_irq(irq);
	cli();
}

void __init init_IRQ(void)
{
	int i;

	for (i = TIMER_IRQ; i < NR_IRQS; i++) {
		irq_desc[i].handler = &onChip_irq_type;
	}
}
