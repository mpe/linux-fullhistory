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
#define IRQ_LEVEL	64	/* IRQ level triggered */
#define IRQ_MASKED	128	/* IRQ masked - shouldn't be seen again */

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
	void (*set_affinity)(unsigned int irq, unsigned int mask);
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
	unsigned int status;		/* IRQ status */
	hw_irq_controller *handler;
	struct irqaction *action;	/* IRQ action list */
	unsigned int depth;		/* nested irq disables */
	spinlock_t lock;
	unsigned int __pad[3];
} ____cacheline_aligned irq_desc_t;

extern irq_desc_t irq_desc [NR_IRQS];

typedef struct {
	unsigned int __local_irq_count;
	unsigned int __local_bh_count;
	atomic_t __nmi_counter;
	unsigned int __pad[5];
} ____cacheline_aligned irq_cpustat_t;

extern irq_cpustat_t irq_stat [NR_CPUS];

/*
 * Simple wrappers reducing source bloat
 */
#define local_irq_count(cpu) (irq_stat[(cpu)].__local_irq_count)
#define local_bh_count(cpu) (irq_stat[(cpu)].__local_bh_count)
#define nmi_counter(cpu) (irq_stat[(cpu)].__nmi_counter)

#include <asm/hw_irq.h> /* the arch dependent stuff */

extern int handle_IRQ_event(unsigned int, struct pt_regs *, struct irqaction *);
extern spinlock_t irq_controller_lock;
extern int setup_irq(unsigned int , struct irqaction * );

extern hw_irq_controller no_irq_type;  /* needed in every arch ? */

#endif /* __asm_h */

