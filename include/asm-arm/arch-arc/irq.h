/*
 * include/asm-arm/arch-arc/irq.h
 *
 * Copyright (C) 1996 Russell King
 *
 * Changelog:
 *   24-09-1996	RMK	Created
 *   10-10-1996	RMK	Brought up to date with arch-sa110eval
 *   22-10-1996	RMK	Changed interrupt numbers & uses new inb/outb macros
 *   11-01-1998	RMK	Added mask_and_ack_irq
 *   22-08-1998	RMK	Restructured IRQ routines
 */

static void arc_mask_irq_ack_a(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]\n"
"	strb	%1, [%3]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOC_IRQMASKA)),
	  "r" (ioaddr(IOC_IRQCLRA)));
}

static void arc_mask_irq_a(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOC_IRQMASKA)));
}

static void arc_unmask_irq_a(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	orr	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOC_IRQMASKA)));
}

static void arc_mask_irq_b(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOC_IRQMASKB)));
}

static void arc_unmask_irq_b(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	orr	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOC_IRQMASKB)));
}

static void arc_mask_irq_fiq(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOC_FIQMASK)));
}

static void arc_unmask_irq_fiq(unsigned int irq)
{
	unsigned int temp;

	__asm__ __volatile__(
	"ldrb	%0, [%2]\n"
"	orr	%0, %0, %1\n"
"	strb	%0, [%2]"
	: "=&r" (temp)
	: "r" (1 << (irq & 7)), "r" (ioaddr(IOC_FIQMASK)));
}

static __inline__ void irq_init_irq(void)
{
	extern void ecard_disableirq(unsigned int irq);
	extern void ecard_enableirq(unsigned int irq);
	int irq;

	outb(0, IOC_IRQMASKA);
	outb(0, IOC_IRQMASKB);
	outb(0, IOC_FIQMASK);

	for (irq = 0; irq < NR_IRQS; irq++) {
		switch (irq & 0xf8) {
		case 0 ... 6:
			irq_desc[irq].probe_ok = 1;
		case 7:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = arc_mask_irq_ack_a;
			irq_desc[irq].mask     = arc_mask_irq_a;
			irq_desc[irq].unmask   = arc_unmask_irq_a;
			break;

		case 9 ... 15:
			irq_desc[irq].probe_ok = 1;
		case 8:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = arc_mask_irq_b;
			irq_desc[irq].mask     = arc_mask_irq_b;
			irq_desc[irq].unmask   = arc_unmask_irq_b;
			break;

		case 32 ... 40:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = ecard_disableirq;
			irq_desc[irq].mask     = ecard_disableirq;
			irq_desc[irq].unmask   = ecard_enableirq;
			break;

		case 64 ... 72:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = arc_mask_irq_fiq;
			irq_desc[irq].mask     = arc_mask_irq_fiq;
			irq_desc[irq].unmask   = arc_unmask_irq_fiq;
			break;
		}
	}
}
