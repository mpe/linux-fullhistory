/* $Id: irq_imask.c,v 1.2 2000/02/11 04:57:40 gniibe Exp $
 *
 * linux/arch/sh/kernel/irq_imask.c
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 * Simple interrupt handling using IMASK of SR register.
 *
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/irq.h>

/* Bitmap of IRQ masked */
static unsigned long imask_mask = 0x7fff;
static int interrupt_priority = 0;

static void enable_imask_irq(unsigned int irq);
static void disable_imask_irq(unsigned int irq);
static void shutdown_imask_irq(unsigned int irq);
static void mask_and_ack_imask(unsigned int);
static void end_imask_irq(unsigned int irq);

#define IMASK_PRIORITY	15

static unsigned int startup_imask_irq(unsigned int irq)
{ 
	enable_imask_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type imask_irq_type = {
	"Interrupt using IMASK of SR register",
	startup_imask_irq,
	shutdown_imask_irq,
	enable_imask_irq,
	disable_imask_irq,
	mask_and_ack_imask,
	end_imask_irq
};

void disable_imask_irq(unsigned int irq)
{
	unsigned long __dummy;

	clear_bit(irq, &imask_mask);
	if (interrupt_priority < IMASK_PRIORITY - irq)
		interrupt_priority = IMASK_PRIORITY - irq;

	asm volatile("stc	sr,%0\n\t"
		     "and	%1,%0\n\t"
		     "or	%2,%0\n\t"
		     "ldc	%0,sr"
		     : "=&r" (__dummy)
		     : "r" (0xffffff0f), "r" (interrupt_priority << 4));
}

static void enable_imask_irq(unsigned int irq)
{
	unsigned long __dummy;

	set_bit(irq, &imask_mask);
	interrupt_priority = IMASK_PRIORITY - ffz(imask_mask);

	asm volatile("stc	sr,%0\n\t"
		     "and	%1,%0\n\t"
		     "or	%2,%0\n\t"
		     "ldc	%0,sr"
		     : "=&r" (__dummy)
		     : "r" (0xffffff0f), "r" (interrupt_priority << 4));
}

static void mask_and_ack_imask(unsigned int irq)
{
	disable_imask_irq(irq);
}

static void end_imask_irq(unsigned int irq)
{
	enable_imask_irq(irq);
}

static void shutdown_imask_irq(unsigned int irq)
{
	disable_imask_irq(irq);
}

void make_imask_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &imask_irq_type;
	enable_irq(irq);
}
