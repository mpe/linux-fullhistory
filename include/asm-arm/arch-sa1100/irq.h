/*
 * linux/include/asm-arm/arch-sa1100/irq.h
 *
 * Copyright (C) 1996-1999 Russell king
 * Copyright (C) 1999 Hugo Fiennes
 *
 * Changelog:
 *   22-08-1998	RMK	Restructured IRQ routines
 *   06-01-1999	HBF	SA1100 twiddles
 *   12-02-1999	NP	added ICCR
 *   17-02-1999	NP	empeg henry ugly hacks now in a separate file ;)
 *   11-08-1999	PD	SA1101 support added
 *   25-09-1999	RMK	Merged into main ARM tree, cleaned up
 */
static inline unsigned int fixup_irq(unsigned int irq)
{
#ifdef CONFIG_SA1101
	if (irq == SA1101_IRQ) {
		unsigned long mask;
		mask = INTSTATCLR0 & INTENABLE0;
		irq  = 32;
		if (!mask) {
			mask = IRQSTATCLR1 & INTENABLE1;
			irq  = 64;
		}
		if (mask)
			while ((mask & 1) == 0) {
				mask >>= 1;
				irq += 1;
			}
	}
#endif
	return irq;
}

/* We don't need to ACK IRQs on the SA1100 unless they're <= 10,
 * ie, an edge-detect.
 */
static void sa1100_mask_and_ack_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
	if (irq <= 10)
		GEDR = 1 << irq;
}

static void sa1100_mask_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
}

static void sa1100_unmask_irq(unsigned int irq)
{
	ICMR |= 1 << irq;
}

#ifdef CONFIG_SA1101

static void sa1101_mask_and_ack_lowirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 31);

	INTENABLE0 &= ~mask;
	GEDR = 1 << SA1101_IRQ;
	INTSTATCLR0 = mask;
}

static void sa1101_mask_and_ack_highirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 31);

	INTENABLE1 &= ~mask;
	GEDR = 1 << SA1101_IRQ;
	INTSTATCLR1 = mask;
}

static void sa1101_mask_lowirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 31);

	INTENABLE0 &= ~mask;
}

static void sa1101_mask_highirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 31);

	INTENABLE1 &= ~mask;
}

/*
 * unmasking an IRq with the wrong polarity can be
 * fatal, but there is no need to check this in the
 * interrupt code - it will be spotted anyway ;-)
 */
static void sa1101_unmask_lowirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 31);

	INTENABLE0 |= mask;
	ICMR |= 1 << SA1101_IRQ;
}

static void sa1101_unmask_highirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq & 31);

	INTENABLE1 |= mask;
	ICMR |= 1 << SA1101_IRQ;
}
#endif

static __inline__ void irq_init_irq(void)
{
	int irq;

	/* disable all IRQs */
	ICMR = 0;

	/* all IRQs are IRQ, not FIQ */
	ICLR = 0;

	/* clear all GPIO edge detects */
	GEDR = -1;

#ifdef CONFIG_SA1101
	/* turn on interrupt controller */
	SKPCR |= 4;

	/* disable all IRQs */
	INTENABLE0 = 0;
	INTENABLE1 = 0;

	/* detect on rising edge */
	INTPOL0 = 0;
	INTPOL1 = 0;

	/* clear all IRQs */
	INTSTATCLR0 = -1;
	INTSTATCLR1 = -1;

	/* SA1101 generates a rising edge */
	GRER |= 1 << SA1101_IRQ;
	GPER &= ~(1 << SA1101_IRQ);
#endif

	/*
	 * Whatever the doc says, this has to be set for the wait-on-irq
	 * instruction to work... on a SA1100 rev 9 at least.
	 */
	ICCR = 1;

#ifndef CONFIG_SA1101
	for (irq = 0; irq < NR_IRQS; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1100_mask_and_ack_irq;
		irq_desc[irq].mask	= sa1100_mask_irq;
		irq_desc[irq].unmask	= sa1100_unmask_irq;
	}
#else
	for (irq = 0; irq < 31; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1100_mask_and_ack_irq;
		irq_desc[irq].mask	= sa1100_mask_irq;
		irq_desc[irq].unmask	= sa1100_unmask_irq;
	}
	for (irq = 32; irq < 63; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1101_mask_and_ack_lowirq;
		irq_desc[irq].mask	= sa1101_mask_lowirq;
		irq_desc[irq].unmask	= sa1101_unmask_lowirq;
	}
	for (irq = 64; irq < NR_IRQS; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= sa1101_mask_and_ack_highirq;
		irq_desc[irq].mask	= sa1101_mask_highirq;
		irq_desc[irq].unmask	= sa1101_unmask_highirq;
	}
#endif
}
