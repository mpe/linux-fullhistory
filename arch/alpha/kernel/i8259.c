/*
 *      linux/arch/alpha/kernel/i8259.c
 *
 * This is the 'legacy' 8259A Programmable Interrupt Controller,
 * present in the majority of PC/AT boxes.
 *
 * Started hacking from linux-2.3.30pre6/arch/i386/kernel/i8259.c.
 */

#include <linux/init.h>
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/io.h>

#include "proto.h"
#include "irq_impl.h"


/* Note mask bit is true for DISABLED irqs.  */
static unsigned int cached_irq_mask = 0xffff;

static inline void
i8259_update_irq_hw(unsigned int irq, unsigned long mask)
{
	int port = 0x21;
	if (irq & 8) mask >>= 8;
	if (irq & 8) port = 0xA1;
	outb(mask, port);
}

inline void
i8259a_enable_irq(unsigned int irq)
{
	i8259_update_irq_hw(irq, cached_irq_mask &= ~(1 << irq));
}

inline void
i8259a_disable_irq(unsigned int irq)
{
	i8259_update_irq_hw(irq, cached_irq_mask |= 1 << irq);
}

void
i8259a_mask_and_ack_irq(unsigned int irq)
{
	i8259a_disable_irq(irq);

	/* Ack the interrupt making it the lowest priority.  */
	if (irq >= 8) {
		outb(0xE0 | (irq - 8), 0xa0);   /* ack the slave */
		irq = 2;
	}
	outb(0xE0 | irq, 0x20);			/* ack the master */
}

unsigned int
i8259a_startup_irq(unsigned int irq)
{
	i8259a_enable_irq(irq);
	return 0; /* never anything pending */
}

struct hw_interrupt_type i8259a_irq_type = {
	typename:	"XT-PIC",
	startup:	i8259a_startup_irq,
	shutdown:	i8259a_disable_irq,
	enable:		i8259a_enable_irq,
	disable:	i8259a_disable_irq,
	ack:		i8259a_mask_and_ack_irq,
	end:		i8259a_enable_irq,
};

void __init
init_i8259a_irqs(void)
{
	static struct irqaction cascade = {
		handler:	no_action,
		name:		"cascade",
	};

	long i;

	outb(0xff, 0x21);	/* mask all of 8259A-1 */
	outb(0xff, 0xA1);	/* mask all of 8259A-2 */

	for (i = 0; i < 16; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].handler = &i8259a_irq_type;
	}

	setup_irq(2, &cascade);
}
