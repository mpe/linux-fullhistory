/*
 * linux/arch/arm/mach-sa1100/irq.c
 *
 * Copyright (C) 1999-2001 Nicolas Pitre
 *
 * Generic IRQ handling for the SA11x0, GPIO 11-27 IRQ demultiplexing.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/irq.h>

#include "generic.h"


/*
 * SA1100 GPIO edge detection for IRQs:
 * IRQs are generated on Falling-Edge, Rising-Edge, or both.
 * This must be called *before* the appropriate IRQ is registered.
 * Use this instead of directly setting GRER/GFER.
 */

static int GPIO_IRQ_rising_edge;
static int GPIO_IRQ_falling_edge;

void set_GPIO_IRQ_edge( int gpio_mask, int edge )
{
	if (edge & GPIO_FALLING_EDGE)
		GPIO_IRQ_falling_edge |= gpio_mask;
	else
		GPIO_IRQ_falling_edge &= ~gpio_mask;
	if (edge & GPIO_RISING_EDGE)
		GPIO_IRQ_rising_edge |= gpio_mask;
	else
		GPIO_IRQ_rising_edge &= ~gpio_mask;
}

EXPORT_SYMBOL(set_GPIO_IRQ_edge);


/*
 * We don't need to ACK IRQs on the SA1100 unless they're GPIOs
 * this is for internal IRQs i.e. from 11 to 31.
 */

static void sa1100_mask_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
}

static void sa1100_unmask_irq(unsigned int irq)
{
	ICMR |= (1 << irq);
}

/*
 * GPIO IRQs must be acknoledged.  This is for IRQs from 0 to 10.
 */

static void sa1100_mask_and_ack_GPIO0_10_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
	GEDR = (1 << irq);
}

static void sa1100_mask_GPIO0_10_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
}

static void sa1100_unmask_GPIO0_10_irq(unsigned int irq)
{
	GRER = (GRER & ~(1 << irq)) | (GPIO_IRQ_rising_edge & (1 << irq));
	GFER = (GFER & ~(1 << irq)) | (GPIO_IRQ_falling_edge & (1 << irq));
	ICMR |= (1 << irq);
}

/*
 * Install handler for GPIO 11-27 edge detect interrupts
 */

static int GPIO_11_27_enabled;		/* enabled i.e. unmasked GPIO IRQs */
static int GPIO_11_27_spurious;		/* GPIOs that triggered when masked */

static void sa1100_GPIO11_27_demux(int irq, void *dev_id,
				   struct pt_regs *regs)
{
	int i, spurious;

	while ((irq = (GEDR & 0xfffff800))) {
		/*
		 * We don't want to clear GRER/GFER when the corresponding
		 * IRQ is masked because we could miss a level transition
		 * i.e. an IRQ which need servicing as soon as it is
		 * unmasked.  However, such situation should happen only
		 * during the loop below.  Thus all IRQs which aren't
		 * enabled at this point are considered spurious.  Those
		 * are cleared but only de-activated if they happen twice.
		 */
		spurious = irq & ~GPIO_11_27_enabled;
		if (spurious) {
			GEDR = spurious;
			GRER &= ~(spurious & GPIO_11_27_spurious);
			GFER &= ~(spurious & GPIO_11_27_spurious);
			GPIO_11_27_spurious |= spurious;
			irq ^= spurious;
			if (!irq) continue;
		}

		for (i = 11; i <= 27; ++i) {
			if (irq & (1<<i)) {
				do_IRQ (IRQ_GPIO_11_27(i), regs);
			}
		}
	}
}

static struct irqaction GPIO11_27_irq = {
	name:		"GPIO 11-27",
	handler:	sa1100_GPIO11_27_demux,
	flags:		SA_INTERRUPT
};

static void sa1100_mask_and_ack_GPIO11_27_irq(unsigned int irq)
{
	int mask = (1 << GPIO_11_27_IRQ(irq));
	GPIO_11_27_spurious &= ~mask;
	GPIO_11_27_enabled &= ~mask;
	GEDR = mask;
}

static void sa1100_mask_GPIO11_27_irq(unsigned int irq)
{
	int mask = (1 << GPIO_11_27_IRQ(irq));
	GPIO_11_27_spurious &= ~mask;
	GPIO_11_27_enabled &= ~mask;
}

static void sa1100_unmask_GPIO11_27_irq(unsigned int irq)
{
	int mask = (1 << GPIO_11_27_IRQ(irq));
	if (GPIO_11_27_spurious & mask) {
		/*
		 * We don't want to miss an interrupt that would have occurred
		 * while it was masked.  Simulate it if it is the case.
		 */
		int state = GPLR;
		if (((state & GPIO_IRQ_rising_edge) |
		     (~state & GPIO_IRQ_falling_edge)) & mask)
		{
			/* just in case it gets referenced: */
			struct pt_regs dummy;

			memzero(&dummy, sizeof(dummy));
			do_IRQ(irq, &dummy);

			/* we are being called recursively from do_IRQ() */
			return;
		}
	}
	GPIO_11_27_enabled |= mask;
	GRER = (GRER & ~mask) | (GPIO_IRQ_rising_edge & mask);
	GFER = (GFER & ~mask) | (GPIO_IRQ_falling_edge & mask);
}


void __init sa1100_init_irq(void)
{
	int irq;

	/* disable all IRQs */
	ICMR = 0;

	/* all IRQs are IRQ, not FIQ */
	ICLR = 0;

	/* clear all GPIO edge detects */
	GFER = 0;
	GRER = 0;
	GEDR = -1;

	/*
	 * Whatever the doc says, this has to be set for the wait-on-irq
	 * instruction to work... on a SA1100 rev 9 at least.
	 */
	ICCR = 1;

	for (irq = 0; irq <= 10; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1100_mask_and_ack_GPIO0_10_irq;
		irq_desc[irq].mask	= sa1100_mask_GPIO0_10_irq;
		irq_desc[irq].unmask	= sa1100_unmask_GPIO0_10_irq;
	}

	for (irq = 11; irq <= 31; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 0;
		irq_desc[irq].mask_ack	= sa1100_mask_irq;
		irq_desc[irq].mask	= sa1100_mask_irq;
		irq_desc[irq].unmask	= sa1100_unmask_irq;
	}

	for (irq = 32; irq <= 48; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1100_mask_and_ack_GPIO11_27_irq;
		irq_desc[irq].mask	= sa1100_mask_GPIO11_27_irq;
		irq_desc[irq].unmask	= sa1100_unmask_GPIO11_27_irq;
	}
	setup_arm_irq( IRQ_GPIO11_27, &GPIO11_27_irq );

	/*
	 * We generally don't want the LCD IRQ being
	 * enabled as soon as we request it.
	 */
	irq_desc[IRQ_LCD].noautoenable = 1;
}
