/*
 * include/asm-arm/arch-ebsa285/irq.h
 *
 * Copyright (C) 1996-1998 Russell King
 *
 * Changelog:
 *   22-08-1998	RMK	Restructured IRQ routines
 */

static void ebsa285_mask_irq(unsigned int irq)
{
	*CSR_IRQ_DISABLE = 1 << irq;
}

static void ebsa285_unmask_irq(unsigned int irq)
{
	*CSR_IRQ_ENABLE = 1 << irq;
}
 
static __inline__ void irq_init_irq(void)
{
	int irq;

	*CSR_IRQ_DISABLE = -1;
	*CSR_FIQ_DISABLE = -1;

	for (irq = 0; irq < NR_IRQS; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= ebsa285_mask_irq;
		irq_desc[irq].mask	= ebsa285_mask_irq;
		irq_desc[irq].unmask	= ebsa285_unmask_irq;
	}
}
