/*
 * include/asm-arm/arch-a5k/irq.h
 *
 * Copyright (C) 1996 Russell King
 *
 * Changelog:
 *   24-09-1996	RMK	Created
 *   10-10-1996	RMK	Brought up to date with arch-sa110eval
 *   22-10-1996	RMK	Changed interrupt numbers & uses new inb/outb macros
 *   11-01-1998	RMK	Added mask_and_ack_irq
 */

#define BUILD_IRQ(s,n,m) \
	void IRQ##n##_interrupt(void); \
	void fast_IRQ##n##_interrupt(void); \
	void bad_IRQ##n##_interrupt(void); \
	void probe_IRQ##n##_interrupt(void);

/*
 * The timer is a special interrupt
 */
#define IRQ5_interrupt		timer_IRQ_interrupt

#define IRQ_INTERRUPT(n)	IRQ##n##_interrupt
#define FAST_INTERRUPT(n)	fast_IRQ##n##_interrupt
#define BAD_INTERRUPT(n)	bad_IRQ##n##_interrupt
#define PROBE_INTERRUPT(n)	probe_IRQ##n##_interrupt
                                
#define X(x) (x)|0x01, (x)|0x02, (x)|0x04, (x)|0x08, (x)|0x10, (x)|0x20, (x)|0x40, (x)|0x80
#define Z(x) (x), (x), (x), (x), (x), (x), (x), (x)

static __inline__ void mask_and_ack_irq(unsigned int irq)
{
	static const int addrmasks[] = {
		X((IOC_IRQMASKA - IOC_BASE)<<18 | (1 << 15)),
		X((IOC_IRQMASKB - IOC_BASE)<<18),
		Z(0),
		Z(0),
		Z(0),
		Z(0),
		Z(0),
		Z(0),
		X((IOC_FIQMASK - IOC_BASE)<<18),
		Z(0),
		Z(0),
		Z(0),
		Z(0),
		Z(0),
		Z(0),
		Z(0)
	};
	unsigned int temp1, temp2;

	__asm__ __volatile__(
"	ldr	%1, [%5, %3, lsl #2]\n"
"	teq	%1, #0\n"
"	beq	2f\n"
"	ldrb	%0, [%2, %1, lsr #16]\n"
"	bic	%0, %0, %1\n"
"	strb	%0, [%2, %1, lsr #16]\n"
"	tst	%1, #0x8000\n"			/* do we need an IRQ clear? */
"	strneb	%1, [%2, %4]\n"
"2:"
	: "=&r" (temp1), "=&r" (temp2)
	: "r" (ioaddr(IOC_BASE)), "r" (irq),
	  "I" ((IOC_IRQCLRA - IOC_BASE) << 2), "r" (addrmasks));
}

#undef X
#undef Z

static __inline__ void mask_irq(unsigned int irq)
{
	extern void ecard_disableirq (unsigned int);
	extern void ecard_disablefiq (unsigned int);
	unsigned char mask = 1 << (irq & 7);

	switch (irq >> 3) {
	case 0:
		outb(inb(IOC_IRQMASKA) & ~mask, IOC_IRQMASKA);
		break;
	case 1:
		outb(inb(IOC_IRQMASKB) & ~mask, IOC_IRQMASKB);
		break;
	case 4:
		ecard_disableirq (irq & 7);
		break;
	case 8:
		outb(inb(IOC_FIQMASK) & ~mask, IOC_FIQMASK);
		break;
	case 12:
		ecard_disablefiq (irq & 7);
	}
}

static __inline__ void unmask_irq(unsigned int irq)
{
	extern void ecard_enableirq (unsigned int);
	extern void ecard_enablefiq (unsigned int);
	unsigned char mask = 1 << (irq & 7);

	switch (irq >> 3) {
	case 0:
		outb(inb(IOC_IRQMASKA) | mask, IOC_IRQMASKA);
		break;
	case 1:
		outb(inb(IOC_IRQMASKB) | mask, IOC_IRQMASKB);
		break;
	case 4:
		ecard_enableirq (irq & 7);
		break;
	case 8:
		outb(inb(IOC_FIQMASK) | mask, IOC_FIQMASK);
		break;
	case 12:
		ecard_enablefiq (irq & 7);
	}
}

static __inline__ unsigned long get_enabled_irqs(void)
{
	return inb(IOC_IRQMASKA) | inb(IOC_IRQMASKB) << 8;
}

static __inline__ void irq_init_irq(void)
{
	outb(0, IOC_IRQMASKA);
	outb(0, IOC_IRQMASKB);
	outb(0, IOC_FIQMASK);
}
