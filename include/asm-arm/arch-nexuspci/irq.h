/*
 * include/asm-arm/arch-nexuspci/irq.h
 *
 * Copyright (C) 1998 Philip Blundell
 */

#include <asm/io.h>

#define INTCONT		0xffe00000

extern unsigned long soft_irq_mask;

static __inline__ void mask_irq(unsigned int irq)
{
	writel((irq << 1), INTCONT);
	soft_irq_mask &= ~(1<<irq);
}

#define mask_and_ack_irq(_x)	mask_irq(_x)

static __inline__ void unmask_irq(unsigned int irq)
{
	writel((irq << 1) + 1, INTCONT);
	soft_irq_mask |= (1<<irq);
}
 
static __inline__ unsigned long get_enabled_irqs(void)
{
	return soft_irq_mask;
}

static __inline__ void irq_init_irq(void)
{
	unsigned int i;
	/* Disable all interrupts initially. */
	for (i = 0; i < NR_IRQS; i++)
		mask_irq(i);
}
