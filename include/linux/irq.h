#ifndef __irq_h
#define __irq_h

#include <asm/irq.h>
/*
 * IRQ line status.
 */
#define IRQ_INPROGRESS	1	/* IRQ handler active - do not enter! */
#define IRQ_DISABLED	2	/* IRQ disabled - do not enter! */
#define IRQ_PENDING	4	/* IRQ pending - replay on enable */
#define IRQ_REPLAY	8	/* IRQ has been replayed but not acked yet */
#define IRQ_AUTODETECT	16	/* IRQ is being autodetected */
#define IRQ_WAITING	32	/* IRQ not yet seen - for autodetection */

/*
 * Interrupt controller descriptor. This is all we need
 * to describe about the low-level hardware. 
 */
struct hw_interrupt_type {
	const char * typename;
	unsigned int (*startup)(unsigned int irq);
	void (*shutdown)(unsigned int irq);
	void (*enable)(unsigned int irq);
	void (*disable)(unsigned int irq);
	void (*ack)(unsigned int irq);
	void (*end)(unsigned int irq);
};

typedef struct hw_interrupt_type  hw_irq_controller;

/*
 * This is the "IRQ descriptor", which contains various information
 * about the irq, including what kind of hardware handling it has,
 * whether it is disabled etc etc.
 *
 * Pad this out to 32 bytes for cache and indexing reasons.
 */
typedef struct {
	unsigned int status;	/* IRQ status
				  - IRQ_INPROGRESS, IRQ_DISABLED */
	hw_irq_controller *handler;	/* never derefed in arch
					   independent code */
	struct irqaction *action;		/* IRQ action list */
	unsigned int depth;			/* Disable depth for nested irq disables */
} irq_desc_t;

#include <asm/hw_irq.h> /* the arch dependent stuff */

extern irq_desc_t irq_desc[NR_IRQS];

extern int handle_IRQ_event(unsigned int, struct pt_regs *, struct irqaction *);
extern spinlock_t irq_controller_lock;
extern int setup_irq(unsigned int , struct irqaction * );

#ifdef __SMP__

#include <asm/atomic.h>

static inline void irq_enter(int cpu, unsigned int irq)
{
	hardirq_enter(cpu);
	while (test_bit(0,&global_irq_lock)) {
		/* nothing */;
	}
}

static inline void irq_exit(int cpu, unsigned int irq)
{
	hardirq_exit(cpu);
}
#else
#define irq_enter(cpu, irq)	(++local_irq_count[cpu])
#define irq_exit(cpu, irq)	(--local_irq_count[cpu])
#endif

extern hw_irq_controller no_irq_type;  /* needed in every arch ? */

#endif /* __asm_h */

