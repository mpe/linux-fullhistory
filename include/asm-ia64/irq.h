#ifndef _ASM_IA64_IRQ_H
#define _ASM_IA64_IRQ_H

/*
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1998 Stephane Eranian <eranian@hpl.hp.com>
 *
 * 11/24/98	S.Eranian 	updated TIMER_IRQ and irq_cannonicalize
 * 01/20/99	S.Eranian	added keyboard interrupt
 */

#include <linux/config.h>
#include <linux/spinlock.h>

#include <asm/ptrace.h>

#define NR_IRQS		256
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
#define TIMER_IRQ		0xef	/* use highest-prio group 15 interrupt for timer */
#define IPI_IRQ			0xfe	/* inter-processor interrupt vector */
#define PERFMON_IRQ		0x28	/* performanc monitor interrupt vector */

#define IA64_MIN_VECTORED_IRQ	 16
#define IA64_MAX_VECTORED_IRQ	255

extern __u8 irq_to_vector_map[IA64_MIN_VECTORED_IRQ];
#define map_legacy_irq(x) (((x) < IA64_MIN_VECTORED_IRQ) ? irq_to_vector_map[(x)] : (x))

#define IRQ_INPROGRESS	(1 << 0)	/* irq handler active */
#define IRQ_ENABLED	(1 << 1)	/* irq enabled */
#define IRQ_PENDING	(1 << 2)	/* irq pending */
#define IRQ_REPLAY	(1 << 3)	/* irq has been replayed but not acked yet */
#define IRQ_AUTODETECT	(1 << 4)	/* irq is being autodetected */
#define IRQ_WAITING	(1 << 5)	/* used for autodetection: irq not yet seen yet */

struct hw_interrupt_type {
	const char *typename;
	void (*init) (unsigned long addr);
	void (*startup) (unsigned int irq);
	void (*shutdown) (unsigned int irq);
	int (*handle) (unsigned int irq, struct pt_regs *regs);
	void (*enable) (unsigned int irq);
	void (*disable) (unsigned int irq);
};

extern struct hw_interrupt_type irq_type_default;	/* dummy interrupt controller */
extern struct hw_interrupt_type irq_type_ia64_internal;	/* CPU-internal interrupt controller */

struct irq_desc {
	unsigned int type;		/* type of interrupt (level vs. edge triggered) */
	unsigned int status;		/* see above */
	unsigned int depth;		/* disable depth for nested irq disables */
	struct hw_interrupt_type *handler;
	struct irqaction *action;	/* irq action list */
};

extern struct irq_desc irq_desc[NR_IRQS];

extern spinlock_t irq_controller_lock;

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

static __inline__ int
irq_cannonicalize (int irq)
{
	/*
	 * We do the legacy thing here of pretending that irqs < 16
	 * are 8259 irqs.
	 */
	return ((irq == 2) ? 9 : irq);
}

extern int invoke_irq_handlers (unsigned int irq, struct pt_regs *regs, struct irqaction *action);
extern void disable_irq (unsigned int);
extern void enable_irq (unsigned int);
extern void ipi_send (int cpu, int vector, int delivery_mode);

#ifdef CONFIG_SMP
  extern void irq_enter(int cpu, int irq);
  extern void irq_exit(int cpu, int irq);
  extern void handle_IPI(int irq, void *dev_id, struct pt_regs *regs);
#else
# define irq_enter(cpu, irq)	(++local_irq_count[cpu])
# define irq_exit(cpu, irq)	(--local_irq_count[cpu])
#endif

#endif /* _ASM_IA64_IRQ_H */
