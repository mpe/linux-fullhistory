
#ifndef _PPC_KERNEL_LOCAL_IRQ_H
#define _PPC_KERNEL_LOCAL_IRQ_H

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>

void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq);

/* Structure describing interrupts */
struct hw_interrupt_type {
	const char * typename;
	void (*startup)(unsigned int irq);
	void (*shutdown)(unsigned int irq);
	void (*handle)(unsigned int irq, struct pt_regs * regs);
	void (*enable)(unsigned int irq);
	void (*disable)(unsigned int irq);
	void (*mask_and_ack)(unsigned int irq);
	int irq_offset;
};

struct irqdesc {
	struct irqaction *action;
	struct hw_interrupt_type *ctl;
};

extern struct irqdesc irq_desc[NR_IRQS];


#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)

extern int ppc_spurious_interrupts;
extern int ppc_second_irq;
extern struct irqaction *ppc_irq_action[NR_IRQS];
extern unsigned int ppc_local_bh_count[NR_CPUS];
extern unsigned int ppc_local_irq_count[NR_CPUS];
extern unsigned int ppc_cached_irq_mask[NR_MASK_WORDS];
extern unsigned int ppc_lost_interrupts[NR_MASK_WORDS];
extern atomic_t ppc_n_lost_interrupts;

#endif /* _PPC_KERNEL_LOCAL_IRQ_H */
