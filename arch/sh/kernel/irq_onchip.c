/* $Id: irq_onchip.c,v 1.5 1999/10/28 02:18:33 gniibe Exp $
 *
 * linux/arch/sh/kernel/irq_onchip.c
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 *
 * Interrupt handling for on-chip supporting modules (TMU, RTC, etc.).
 *
 */

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
#if defined(__sh3__)
#define INTC_IPR	0xfffffee2UL	/* Word access */
#define INTC_SIZE	0x2
#elif defined(__SH4__)
#define INTC_IPR	0xffd00004UL	/* Word access */
#define INTC_SIZE	0x4
#endif

void disable_onChip_irq(unsigned int irq)
{
	unsigned long val, flags;
	/* Set priority in IPR to 0 */
	int offset = ipr_data[irq-TIMER_IRQ].offset;
	unsigned long intc_ipr_address = INTC_IPR + (offset/16*INTC_SIZE);
	unsigned short mask = 0xffff ^ (0xf << (offset%16));

	save_and_cli(flags);
	val = ctrl_inw(intc_ipr_address);
	val &= mask;
	ctrl_outw(val, intc_ipr_address);
	restore_flags(flags);
}

static void enable_onChip_irq(unsigned int irq)
{
	unsigned long val, flags;
	/* Set priority in IPR back to original value */
	int offset = ipr_data[irq-TIMER_IRQ].offset;
	int priority = ipr_data[irq-TIMER_IRQ].priority;
	unsigned long intc_ipr_address = INTC_IPR + (offset/16*INTC_SIZE);
	unsigned short value = (priority << (offset%16));

	save_and_cli(flags);
	val = ctrl_inw(intc_ipr_address);
	val |= value;
	ctrl_outw(val, intc_ipr_address);
	restore_flags(flags);
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
}

static void end_onChip_irq(unsigned int irq)
{
	enable_onChip_irq(irq);
}


#ifdef CONFIG_CPU_SUBTYPE_SH7709
/*
 * SH7707/SH7709/SH7709A/SH7729 Extended on-chip I/O
 */

#define INTC_IRR0	0xa4000004UL
#define INTC_IPRC	0xa4000016UL

#define IRQ0_IRQ	32
#define IRQ1_IRQ	33
#define IRQ2_IRQ	34
#define IRQ3_IRQ	35
#define IRQ4_IRQ	36
#define IRQ5_IRQ	37

#define IRQ0_IRP_OFFSET	32
#define IRQ1_IRP_OFFSET	36
#define IRQ2_IRP_OFFSET	40
#define IRQ3_IRP_OFFSET	44
#define IRQ4_IRP_OFFSET	48
#define IRQ5_IRP_OFFSET	52

#define IRQ0_PRIORITY	1
#define IRQ1_PRIORITY	1
#define IRQ2_PRIORITY	1
#define IRQ3_PRIORITY	1
#define IRQ4_PRIORITY	1
#define IRQ5_PRIORITY	1

static void enable_onChip2_irq(unsigned int irq);
void disable_onChip2_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_onChip2_irq	disable_onChip2_irq

static void mask_and_ack_onChip2(unsigned int);
static void end_onChip2_irq(unsigned int irq);

static unsigned int startup_onChip2_irq(unsigned int irq)
{ 
	enable_onChip2_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type onChip2_irq_type = {
	"SH7709 Extended On-Chip Supporting Module",
	startup_onChip2_irq,
	shutdown_onChip2_irq,
	enable_onChip2_irq,
	disable_onChip2_irq,
	mask_and_ack_onChip2,
	end_onChip2_irq
};

void disable_onChip2_irq(unsigned int irq)
{
	unsigned long val, flags;
	/* Set priority in IPR to 0 */
	int offset = ipr_data[irq-TIMER_IRQ].offset - 32;
	unsigned long intc_ipr_address = INTC_IPRC + (offset/16*INTC_SIZE);
	unsigned short mask = 0xffff ^ (0xf << (offset%16));

	save_and_cli(flags);
	val = ctrl_inw(intc_ipr_address);
	val &= mask;
	ctrl_outw(val, intc_ipr_address);
	restore_flags(flags);
}

static void enable_onChip2_irq(unsigned int irq)
{
	unsigned long val, flags;
	/* Set priority in IPR back to original value */
	int offset = ipr_data[irq-TIMER_IRQ].offset - 32;
	int priority = ipr_data[irq-TIMER_IRQ].priority;
	unsigned long intc_ipr_address = INTC_IPRC + (offset/16*INTC_SIZE);
	unsigned short value = (priority << (offset%16));

	save_and_cli(flags);
	val = ctrl_inw(intc_ipr_address);
	val |= value;
	ctrl_outw(val, intc_ipr_address);
	restore_flags(flags);
}

static void mask_and_ack_onChip2(unsigned int irq)
{
	disable_onChip2_irq(irq);
	if (IRQ0_IRQ <= irq && irq <= IRQ5_IRQ) {
		/* Clear external interrupt request */
		int a = ctrl_inb(INTC_IRR0);
		a &= ~(1 << (irq - IRQ0_IRQ));
		ctrl_outb(a, INTC_IRR0);
	}
}

static void end_onChip2_irq(unsigned int irq)
{
	enable_onChip2_irq(irq);
}
#endif /* CONFIG_CPU_SUBTYPE_SH7709 */

void __init init_IRQ(void)
{
	int i;

	for (i = TIMER_IRQ; i < NR_IRQS; i++) {
		irq_desc[i].handler = &onChip_irq_type;
	}

#ifdef CONFIG_CPU_SUBTYPE_SH7709
	for (i = IRQ0_IRQ; i < NR_IRQS; i++) {
		irq_desc[i].handler = &onChip2_irq_type;
	}

	/*
	 * Enable external irq(INTC IRQ mode).
	 * You should set corresponding bits of PFC to "00"
	 * to enable these interrupts.
	 */
	set_ipr_data(IRQ0_IRQ, IRQ0_IRP_OFFSET, IRQ0_PRIORITY);
	set_ipr_data(IRQ1_IRQ, IRQ1_IRP_OFFSET, IRQ1_PRIORITY);
	set_ipr_data(IRQ2_IRQ, IRQ2_IRP_OFFSET, IRQ2_PRIORITY);
	set_ipr_data(IRQ3_IRQ, IRQ3_IRP_OFFSET, IRQ3_PRIORITY);
	set_ipr_data(IRQ4_IRQ, IRQ4_IRP_OFFSET, IRQ4_PRIORITY);
	set_ipr_data(IRQ5_IRQ, IRQ5_IRP_OFFSET, IRQ5_PRIORITY);

	ctrl_inb(INTC_IRR0);
	ctrl_outb(0, INTC_IRR0);
#endif /* CONFIG_CPU_SUBTYPE_SH7709 */
}
