/*
 * include/asm-arm/arch-nexuspci/irq.h
 *
 * Copyright (C) 1998 Philip Blundell
 */

#define INT_RESET	((volatile unsigned char *)0xfff00000)
#define INT_ENABLE	((volatile unsigned char *)0xfff00000)
#define INT_DISABLE	((volatile unsigned char *)0xfff00000)

static __inline__ void mask_and_ack_irq(unsigned int irq)
{
	INT_DISABLE[irq << 2] = 0;
	INT_RESET[irq << 2] = 0;
}

static __inline__ void mask_irq(unsigned int irq)
{
	INT_DISABLE[irq << 2] = 0;
}

static __inline__ void unmask_irq(unsigned int irq)
{
	INT_ENABLE[irq << 2] = 0;
}
 
static __inline__ unsigned long get_enabled_irqs(void)
{
	return 0;
}

static __inline__ void irq_init_irq(void)
{
	unsigned int i;
	/* Disable all interrupts initially. */
	for (i = 0; i < 8; i++)
		mask_irq(i);
}
