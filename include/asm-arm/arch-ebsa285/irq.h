/*
 * include/asm-arm/arch-ebsa110/irq.h
 *
 * Copyright (C) 1996,1997,1998 Russell King
 */

static __inline__ void mask_and_ack_irq(unsigned int irq)
{
	if (irq < 32)
		*CSR_IRQ_DISABLE = 1 << irq;
}

static __inline__ void mask_irq(unsigned int irq)
{
	if (irq < 32)
		*CSR_IRQ_DISABLE = 1 << irq;
}

static __inline__ void unmask_irq(unsigned int irq)
{
	if (irq < 32)
		*CSR_IRQ_ENABLE = 1 << irq;
}
 
static __inline__ unsigned long get_enabled_irqs(void)
{
	return 0;
}

static __inline__ void irq_init_irq(void)
{
	*CSR_IRQ_DISABLE = -1;
	*CSR_FIQ_DISABLE = -1;
}
