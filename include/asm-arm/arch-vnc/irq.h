/*
 * include/asm-arm/arch-vnc/irq.h
 *
 * Copyright (C) 1998 Russell King
 */

#include <asm/irq.h>

/*
 * FootBridge IRQ translation table
 *  Converts form our IRQ numbers into FootBridge masks (defined in irqs.h)
 */
static int fb_irq_mask[16] = {
	0,
	IRQ_MASK_SOFTIRQ,
	IRQ_MASK_UART_DEBUG,
	0,
	IRQ_MASK_TIMER0,
	IRQ_MASK_TIMER1,
	IRQ_MASK_TIMER2,
	IRQ_MASK_WATCHDOG,
	IRQ_MASK_ETHER10,
	IRQ_MASK_ETHER100,
	IRQ_MASK_VIDCOMP,
	IRQ_MASK_EXTERN_IRQ,
	IRQ_MASK_DMA1,
	0,
	0,
	IRQ_MASK_PCI_ERR
};

static __inline__ void mask_and_ack_irq(unsigned int irq)
{
	if (irq < 16)
		*CSR_IRQ_DISABLE = fb_irq_mask[irq];
	else {
		unsigned int pic_mask, mask;

		if (irq < 24)
			pic_mask = PIC_MASK_LO;
		else
			pic_mask = PIC_MASK_HI;

		mask = 1 << (irq & 7);

		outb(inb(pic_mask) | mask, pic_mask);
	}
}

static __inline__ void mask_irq(unsigned int irq)
{
	if (irq < 16)
		*CSR_IRQ_DISABLE = fb_irq_mask[irq];
	else {
		unsigned int pic_mask, mask;

		if (irq < 24)
			pic_mask = PIC_MASK_LO;
		else
			pic_mask = PIC_MASK_HI;

		mask = 1 << (irq & 7);

		outb(inb(pic_mask) | mask, pic_mask);
	}
}

static __inline__ void unmask_irq(unsigned int irq)
{
	if (irq < 16)
		*CSR_IRQ_ENABLE = fb_irq_mask[irq];
	else {
		unsigned int pic_mask, mask;

		if (irq < 24)
			pic_mask = PIC_MASK_LO;
		else
			pic_mask = PIC_MASK_HI;

		mask = 1 << (irq & 7);

		outb(inb(pic_mask) & ~mask, pic_mask);
	}
}
 
static __inline__ unsigned long get_enabled_irqs(void)
{
	return 0;
}

static __inline__ void irq_init_irq(void)
{
	outb(0x11, PIC_LO);
	outb(0x10, PIC_MASK_LO);
	outb(0x04, PIC_MASK_LO);
	outb(1, PIC_MASK_LO);

	outb(0x11, PIC_HI);
	outb(0x18, PIC_MASK_HI);
	outb(0x02, PIC_MASK_HI);
	outb(1, PIC_MASK_HI);

	*CSR_IRQ_DISABLE = ~IRQ_MASK_EXTERN_IRQ;
	*CSR_IRQ_ENABLE  = IRQ_MASK_EXTERN_IRQ;
	*CSR_FIQ_DISABLE = -1;
}
