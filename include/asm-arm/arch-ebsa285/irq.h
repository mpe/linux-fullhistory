/*
 * include/asm-arm/arch-ebsa285/irq.h
 *
 * Copyright (C) 1996-1998 Russell King
 *
 * Changelog:
 *  22-Aug-1998	RMK	Restructured IRQ routines
 *  03-Sep-1998	PJB	Merged CATS support
 *  20-Jan-1998	RMK	Started merge of EBSA286, CATS and NetWinder
 *  26-Jan-1999	PJB	Don't use IACK on CATS
 *  16-Mar-1999	RMK	Added autodetect of ISA PICs
 */
#include <linux/config.h>
#include <asm/hardware.h>
#include <asm/dec21285.h>
#include <asm/irq.h>

/*
 * Footbridge IRQ translation table
 *  Converts from our IRQ numbers into FootBridge masks
 */
static int dc21285_irq_mask[] = {
	IRQ_MASK_UART_RX,	/*  0 */
	IRQ_MASK_UART_TX,	/*  1 */
	IRQ_MASK_TIMER1,	/*  2 */
	IRQ_MASK_TIMER2,	/*  3 */
	IRQ_MASK_TIMER3,	/*  4 */
	IRQ_MASK_IN0,		/*  5 */
	IRQ_MASK_IN1,		/*  6 */
	IRQ_MASK_IN2,		/*  7 */
	IRQ_MASK_IN3,		/*  8 */
	IRQ_MASK_DOORBELLHOST,	/*  9 */
	IRQ_MASK_DMA1,		/* 10 */
	IRQ_MASK_DMA2,		/* 11 */
	IRQ_MASK_PCI,		/* 12 */
	IRQ_MASK_SDRAMPARITY,	/* 13 */
	IRQ_MASK_I2OINPOST,	/* 14 */
	IRQ_MASK_PCI_ERR	/* 15 */
};

static int isa_irq = -1;

static inline int fixup_irq(unsigned int irq)
{
#ifdef CONFIG_HOST_FOOTBRIDGE
	if (irq == isa_irq)
		irq = *(unsigned char *)PCIIACK_BASE;
#endif

	return irq;
}

static void dc21285_mask_irq(unsigned int irq)
{
	*CSR_IRQ_DISABLE = dc21285_irq_mask[irq & 15];
}

static void dc21285_unmask_irq(unsigned int irq)
{
	*CSR_IRQ_ENABLE = dc21285_irq_mask[irq & 15];
}

static void isa_mask_pic_lo_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_LO) | mask, PIC_MASK_LO);
}

static void isa_mask_ack_pic_lo_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_LO) | mask, PIC_MASK_LO);
	outb(0x20, PIC_LO);
}

static void isa_unmask_pic_lo_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_LO) & ~mask, PIC_MASK_LO);
}

static void isa_mask_pic_hi_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_HI) | mask, PIC_MASK_HI);
}

static void isa_mask_ack_pic_hi_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_HI) | mask, PIC_MASK_HI);
	outb(0x62, PIC_LO);
	outb(0x20, PIC_HI);
}

static void isa_unmask_pic_hi_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 7);

	outb(inb(PIC_MASK_HI) & ~mask, PIC_MASK_HI);
}

static void no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
}

static struct irqaction irq_cascade = { no_action, 0, 0, "cascade", NULL, NULL };

static __inline__ void irq_init_irq(void)
{
	int irq;

	/*
	 * setup DC21285 IRQs
	 */
	*CSR_IRQ_DISABLE = -1;
	*CSR_FIQ_DISABLE = -1;

	for (irq = _DC21285_IRQ(0); irq < _DC21285_IRQ(16); irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= dc21285_mask_irq;
		irq_desc[irq].mask	= dc21285_mask_irq;
		irq_desc[irq].unmask	= dc21285_unmask_irq;
	}

	/*
	 * Determine the ISA settings for
	 * the machine we're running on.
	 */
	switch (machine_arch_type) {
	default:
		isa_irq = -1;
		break;

	case MACH_TYPE_EBSA285:
		/* The following is dependent on which slot
		 * you plug the Southbridge card into.  We
		 * currently assume that you plug it into
		 * the right-hand most slot.
		 */
		isa_irq = IRQ_PCI;
		break;

	case MACH_TYPE_CATS:
		isa_irq = IRQ_IN2;
		break;

	case MACH_TYPE_NETWINDER:
		isa_irq = IRQ_IN3;
		break;
	}

	if (isa_irq != -1) {
		/*
		 * Setup, and then probe for an ISA PIC
		 */
		outb(0x11, PIC_LO);
		outb(_ISA_IRQ(0), PIC_MASK_LO);	/* IRQ number		*/
		outb(0x04, PIC_MASK_LO);	/* Slave on Ch2		*/
		outb(0x01, PIC_MASK_LO);	/* x86			*/
		outb(0xf5, PIC_MASK_LO);	/* pattern: 11110101	*/

		outb(0x11, PIC_HI);
		outb(_ISA_IRQ(8), PIC_MASK_HI);	/* IRQ number		*/
		outb(0x02, PIC_MASK_HI);	/* Slave on Ch1		*/
		outb(0x01, PIC_MASK_HI);	/* x86			*/
		outb(0xfa, PIC_MASK_HI);	/* pattern: 11111010	*/

//		outb(0x68, PIC_LO);		/* enable special mode	*/
//		outb(0x68, PIC_HI);		/* enable special mode	*/
		outb(0x0b, PIC_LO);
		outb(0x0b, PIC_HI);

		if (inb(PIC_MASK_LO) == 0xf5 && inb(PIC_MASK_HI) == 0xfa) {
			outb(0xff, PIC_MASK_LO);/* mask all IRQs	*/
			outb(0xff, PIC_MASK_HI);/* mask all IRQs	*/
		} else
			isa_irq = -1;
	}

	if (isa_irq != -1) {
		for (irq = _ISA_IRQ(0); irq < _ISA_IRQ(8); irq++) {
			irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			irq_desc[irq].mask_ack	= isa_mask_ack_pic_lo_irq;
			irq_desc[irq].mask	= isa_mask_pic_lo_irq;
			irq_desc[irq].unmask	= isa_unmask_pic_lo_irq;
		}

		for (irq = _ISA_IRQ(8); irq < _ISA_IRQ(16); irq++) {
			irq_desc[irq].valid	= 1;
			irq_desc[irq].probe_ok	= 1;
			irq_desc[irq].mask_ack	= isa_mask_ack_pic_hi_irq;
			irq_desc[irq].mask	= isa_mask_pic_hi_irq;
			irq_desc[irq].unmask	= isa_unmask_pic_hi_irq;
		}

		request_region(PIC_LO, 2, "pic1");
		request_region(PIC_HI, 2, "pic2");
		setup_arm_irq(IRQ_ISA_CASCADE, &irq_cascade);
		setup_arm_irq(isa_irq, &irq_cascade);
	}
}
