/*
 * include/asm-arm/arch-ebsa110/irq.h
 *
 * Copyright (C) 1996,1997,1998 Russell King
 */

#define IRQ_MCLR	((volatile unsigned char *)0xf3000000)
#define IRQ_MSET	((volatile unsigned char *)0xf2c00000)
#define IRQ_MASK	((volatile unsigned char *)0xf2c00000)

static __inline__ void mask_and_ack_irq(unsigned int irq)
{
	if (irq < 8)
		*IRQ_MCLR = 1 << irq;
}

static __inline__ void mask_irq(unsigned int irq)
{
	if (irq < 8)
		*IRQ_MCLR = 1 << irq;
}

static __inline__ void unmask_irq(unsigned int irq)
{
	if (irq < 8)
		*IRQ_MSET = 1 << irq;
}
 
static __inline__ unsigned long get_enabled_irqs(void)
{
	return 0;
}

static __inline__ void irq_init_irq(void)
{
	unsigned long flags;

	save_flags_cli (flags);
	*IRQ_MCLR = 0xff;
	*IRQ_MSET = 0x55;
	*IRQ_MSET = 0x00;
	if (*IRQ_MASK != 0x55)
		while (1);
	*IRQ_MCLR = 0xff;		/* clear all interrupt enables */
	restore_flags (flags);
}
