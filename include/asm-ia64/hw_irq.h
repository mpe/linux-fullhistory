#ifndef _ASM_IA64_HW_IRQ_H
#define _ASM_IA64_HW_IRQ_H

/*
 * Copyright (C) 2000 Hewlett-Packard Co
 * Copyright (C) 2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>

#include <linux/types.h>

#include <asm/ptrace.h>

#define NR_ISA_IRQS	 16

/*
 * 0 special
 *
 * 1,3-14 are reserved from firmware
 *
 * 16-255 (vectored external interrupts) are available
 *
 * 15 spurious interrupt (see IVR)
 *
 * 16 lowest priority, 255 highest priority
 *
 * 15 classes of 16 interrupts each.
 */
#define IA64_MIN_VECTORED_IRQ	 16
#define IA64_MAX_VECTORED_IRQ	255

#define IA64_SPURIOUS_INT	0x0f

#define IA64_MIN_VECTORED_IRQ	 16
#define IA64_MAX_VECTORED_IRQ	255

#define PERFMON_IRQ		0x28	/* performanc monitor interrupt vector */
#define TIMER_IRQ		0xef	/* use highest-prio group 15 interrupt for timer */
#define IPI_IRQ			0xfe	/* inter-processor interrupt vector */
#define CMC_IRQ			0xff	/* correctable machine-check interrupt vector */

/* IA64 inter-cpu interrupt related definitions */

/* Delivery modes for inter-cpu interrupts */
enum {
        IA64_IPI_DM_INT =       0x0,    /* pend an external interrupt */
        IA64_IPI_DM_PMI =       0x2,    /* pend a PMI */
        IA64_IPI_DM_NMI =       0x4,    /* pend an NMI (vector 2) */
        IA64_IPI_DM_INIT =      0x5,    /* pend an INIT interrupt */
        IA64_IPI_DM_EXTINT =    0x7,    /* pend an 8259-compatible interrupt. */
};

#define IA64_BUS_ID(cpu)        (cpu >> 8)
#define IA64_LOCAL_ID(cpu)      (cpu & 0xff)

extern __u8 isa_irq_to_vector_map[16];
#define isa_irq_to_vector(x)	isa_irq_to_vector_map[(x)]

extern struct hw_interrupt_type irq_type_ia64_internal;	/* CPU-internal interrupt controller */

extern void ipi_send (int cpu, int vector, int delivery_mode);

#ifdef CONFIG_SMP
extern void handle_IPI(int irq, void *dev_id, struct pt_regs *regs);

static inline void
hw_resend_irq (struct hw_interrupt_type *h, unsigned int i)
{
	send_IPI_self(i);
}
#else
# define hw_resend_irq(h,i)
#endif

#endif /* _ASM_IA64_HW_IRQ_H */
