/*
 * include/asm-arm/arch-nexuspci/irq.h
 *
 * Copyright (C) 1998 Philip Blundell
 *
 * Changelog:
 *   22-08-1998	RMK	Restructured IRQ routines
 */

#include <asm/io.h>

#define INTCONT		0xffe00000

extern unsigned long soft_irq_mask;

static void nexuspci_mask_irq(unsigned int irq)
{
	writel((irq << 1), INTCONT);
	soft_irq_mask &= ~(1<<irq);
}

static void nexuspci_unmask_irq(unsigned int irq)
{
	writel((irq << 1) + 1, INTCONT);
	soft_irq_mask |= (1<<irq);
}
 
static __inline__ void irq_init_irq(void)
{
	unsigned int i;
	/* Disable all interrupts initially. */
	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].valid	= 1;
		irq_desc[i].probe_ok	= 1;
		irq_desc[i].mask_ack	= nexuspci_mask_irq;
		irq_desc[i].mask	= nexuspci_mask_irq;
		irq_desc[i].unmask	= nexuspci_unmask_irq;
		mask_irq(i);
	}
}
