/*
 * include/asm-arm/arch-ebsa285/irq.h
 *
 * Copyright (C) 1996-1998 Russell King
 *
 * Changelog:
 *   22-08-1998	RMK	Restructured IRQ routines
 *   03-09-1998	PJB	Merged CATS support
 */
#include <linux/config.h>

static void ebsa285_mask_irq(unsigned int irq)
{
	*CSR_IRQ_DISABLE = 1 << irq;
}

static void ebsa285_unmask_irq(unsigned int irq)
{
	*CSR_IRQ_ENABLE = 1 << irq;
}

#ifdef CONFIG_CATS

/*
 * This contains the irq mask for both 8259A irq controllers,
 */
static unsigned int isa_irq_mask = 0xffff;

#define cached_21	(isa_irq_mask & 0xff)
#define cached_A1	((isa_irq_mask >> 8) & 0xff)

#define update_8259(_irq)			\
	if ((_irq) & 8)				\
		outb(cached_A1, 0xa1);		\
	else					\
		outb(cached_21, 0x21);

static void isa_interrupt(int irq, void *h, struct pt_regs *regs)
{
	asmlinkage void do_IRQ(int irq, struct pt_regs * regs);
	unsigned int irqbits = inb(0x20) | (inb(0xa0) << 8), irqnr = 0;
	irqbits &= ~(1<<2);	/* don't try to service the cascade */
	while (irqbits) {
		if (irqbits & 1)
			do_IRQ(32 + irqnr, regs);
		irqbits >>= 1;
		irqnr++;
	}
}

static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

static struct irqaction irq_isa = 
		{ isa_interrupt, SA_INTERRUPT, 0, "ISA PIC", NULL, NULL };
static struct irqaction irq_cascade = 
		{ no_action, 0, 0, "cascade", NULL, NULL };

static void cats_mask_and_ack_isa_irq(unsigned int irq)
{
	isa_irq_mask |= (1 << (irq - 32));
	update_8259(irq);
	if (irq & 8) {
		inb(0xA1);	/* DUMMY */
		outb(cached_A1,0xA1);
		outb(0x62,0x20);	/* Specific EOI to cascade */
		outb(0x20,0xA0);
	} else {
		inb(0x21);	/* DUMMY */
		outb(cached_21,0x21);
		outb(0x20,0x20);
	}
}

static void cats_mask_isa_irq(unsigned int irq)
{
	isa_irq_mask |= (1 << (irq - 32));
	update_8259(irq);
}

static void cats_unmask_isa_irq(unsigned int irq)
{
	isa_irq_mask &= ~(1 << (irq - 32));
	update_8259(irq);
}
 
#endif 

static __inline__ void irq_init_irq(void)
{
	int irq;

	*CSR_IRQ_DISABLE = -1;
	*CSR_FIQ_DISABLE = -1;

	for (irq = 0; irq < NR_IRQS; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
#ifdef CONFIG_CATS
		if (machine_is_cats() && IRQ_IS_ISA(irq)) {
			irq_desc[irq].mask_ack	= cats_mask_and_ack_isa_irq;
			irq_desc[irq].mask	= cats_mask_isa_irq;
			irq_desc[irq].unmask	= cats_unmask_isa_irq;
		} else
#endif
		{
			irq_desc[irq].mask_ack	= ebsa285_mask_irq;
			irq_desc[irq].mask	= ebsa285_mask_irq;
			irq_desc[irq].unmask	= ebsa285_unmask_irq;
		}
	}

#ifdef CONFIG_CATS
	if (machine_is_cats()) {
		request_region(0x20, 2, "pic1");
		request_region(0xa0, 2, "pic2");

		/* set up master 8259 */
		outb(0x11, 0x20);
		outb(0, 0x21);
		outb(1<<2, 0x21);
		outb(0x1, 0x21);
		outb(0xff, 0x21);
		outb(0x68, 0x20);
		outb(0xa, 0x20);
		
		/* set up slave 8259 */
		outb(0x11, 0xa0);
		outb(0, 0xa1);
		outb(2, 0xa1);
		outb(0x1, 0xa1);
		outb(0xff, 0xa1);
		outb(0x68, 0xa0);
		outb(0xa, 0xa0);

		setup_arm_irq(IRQ_ISA_PIC, &irq_isa);
		setup_arm_irq(IRQ_ISA_CASCADE, &irq_cascade);
	}
#endif
}
