/*
 * linux/arch/arm/mach-h720x/cpu-h7201.c
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *               2004 Sascha Hauer    <s.hauer@pengutronix.de>
 *
 * processor specific stuff for the Hynix h7201
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/irqs.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

extern unsigned long h720x_gettimeoffset(void);
extern void __init h720x_init_irq (void);

/*
 * Timer interrupt handler
 */
static irqreturn_t
h7201_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	CPU_REG (TIMER_VIRT, TIMER_TOPSTAT);
	timer_tick(regs);
	return IRQ_HANDLED;
}

static struct irqaction h7201_timer_irq = {
	.name		= "h7201 Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= h7201_timer_interrupt
};

/*
 * Setup TIMER0 as system timer
 */
void __init h7201_init_time(void)
{
	gettimeoffset = h720x_gettimeoffset;

	CPU_REG (TIMER_VIRT, TM0_PERIOD) = LATCH;
	CPU_REG (TIMER_VIRT, TM0_CTRL) = TM_RESET;
	CPU_REG (TIMER_VIRT, TM0_CTRL) = TM_REPEAT | TM_START;
	CPU_REG (TIMER_VIRT, TIMER_TOPCTRL) = ENABLE_TM0_INTR | TIMER_ENABLE_BIT;

	setup_irq(IRQ_TIMER0, &h7201_timer_irq);
}
