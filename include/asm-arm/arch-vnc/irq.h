/*
 * include/asm-arm/arch-vnc/irq.h
 *
 * Copyright (C) 1998 Russell King
 *
 * Changelog:
 *   22-08-1998	RMK	Restructured IRQ routines
 */

#include <asm/dec21285.h>
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

static void vnc_mask_csr_irq(unsigned int irq)
{
	*CSR_IRQ_DISABLE = fb_irq_mask[irq];
}

static void vnc_unmask_csr_irq(unsigned int irq)
{
	*CSR_IRQ_ENABLE = fb_irq_mask[irq];
}

static void vnc_mask_pic_lo_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_LO) | mask, PIC_MASK_LO);
}

static void vnc_mask_ack_pic_lo_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_LO) | mask, PIC_MASK_LO);
	outb(0x20, PIC_LO);
}

static void vnc_unmask_pic_lo_irq(unsigned int irq)
{
	unsigned int mask = ~(1 << (irq & 7));

	outb(inb(PIC_MASK_LO) & mask, PIC_MASK_LO);
}

static void vnc_mask_pic_hi_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_HI) | mask, PIC_MASK_HI);
}

static void vnc_mask_ack_pic_hi_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_HI) | mask, PIC_MASK_HI);
	outb(0x62, PIC_LO);
	outb(0x20, PIC_HI);
}

static void vnc_unmask_pic_hi_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_HI) & ~mask, PIC_MASK_HI);
}

static void no_action(int irq, void *dev_id, struct pt_regs *regs)
{
}

static struct irqaction irq_cascade = { no_action, 0, 0, "cascade", NULL, NULL };

static __inline__ void irq_init_irq(void)
{
	unsigned int irq;

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

	for (irq = 0; irq < NR_IRQS; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;

		if (irq < 16) {
			irq_desc[irq].mask_ack	= vnc_mask_csr_irq;
			irq_desc[irq].mask	= vnc_mask_csr_irq;
			irq_desc[irq].unmask	= vnc_unmask_csr_irq;
		} else if (irq < 24) {
irq_desc[irq].probe_ok = 0;
			irq_desc[irq].mask_ack	= vnc_mask_ack_pic_lo_irq;
			irq_desc[irq].mask	= vnc_mask_pic_lo_irq;
			irq_desc[irq].unmask	= vnc_unmask_pic_lo_irq;
		} else {
irq_desc[irq].probe_ok = 0;
			irq_desc[irq].mask_ack	= vnc_mask_ack_pic_hi_irq;
			irq_desc[irq].mask	= vnc_mask_pic_hi_irq;
			irq_desc[irq].unmask	= vnc_unmask_pic_hi_irq;
		}
	}

	irq_desc[0].probe_ok = 0;
	irq_desc[IRQ_SOFTIRQ].probe_ok = 0;
	irq_desc[IRQ_CONRX].probe_ok = 0;
	irq_desc[IRQ_CONTX].probe_ok = 0;
	irq_desc[IRQ_TIMER0].probe_ok = 0;
	irq_desc[IRQ_TIMER1].probe_ok = 0;
	irq_desc[IRQ_TIMER2].probe_ok = 0;
	irq_desc[IRQ_WATCHDOG].probe_ok = 0;
	irq_desc[IRQ_DMA1].probe_ok = 0;
	irq_desc[13].probe_ok = 0;
	irq_desc[14].probe_ok = 0;
	irq_desc[IRQ_PCI_ERR].probe_ok = 0;
	irq_desc[IRQ_PIC_HI].probe_ok = 0;
	irq_desc[29].probe_ok = 0;
	irq_desc[31].probe_ok = 0;

	outb(0xff, PIC_MASK_LO);
	outb(0xff, PIC_MASK_HI);

	setup_arm_irq(IRQ_PIC_HI, &irq_cascade);
}
