/*
 * $Id: irq_control.h,v 1.8 1999/09/15 23:58:48 cort Exp $
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _PPC_HW_IRQ_H
#define _PPC_HW_IRQ_H

#if 0
/* Structure describing interrupts */
struct hw_interrupt_type {
	const char * typename;
	void (*startup)(unsigned int irq);
	void (*shutdown)(unsigned int irq);
	void (*enable)(unsigned int irq);
	void (*disable)(unsigned int irq);
	void (*mask_and_ack)(unsigned int irq);
	int irq_offset;
};

struct irqdesc {
	struct irqaction *action;
	struct hw_interrupt_type *ctl;
};
#endif

struct int_control_struct
{
	void (*int_cli)(void);
	void (*int_sti)(void);
	void (*int_restore_flags)(unsigned long);
	void (*int_save_flags)(unsigned long *);
};
extern struct int_control_struct int_control;
extern unsigned long timer_interrupt_intercept;
extern unsigned long do_IRQ_intercept;
void timer_interrupt(struct pt_regs *);

extern void __no_use_sti(void);
extern void __no_use_cli(void);
extern void __no_use_restore_flags(unsigned long);
extern void __no_use_save_flags(unsigned long *);

#define __cli() int_control.int_cli()
#define __sti() int_control.int_sti()
#define __save_flags(flags) int_control.int_save_flags(&flags)
#define __restore_flags(flags) int_control.int_restore_flags(flags)
#define __save_and_cli(flags) ({__save_flags(flags);__cli();})

extern void do_lost_interrupts(unsigned long);
extern atomic_t ppc_n_lost_interrupts;

#define mask_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->disable) irq_desc[irq].handler->disable(irq);})
#define unmask_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->enable) irq_desc[irq].handler->enable(irq);})
#define mask_and_ack_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->ack) irq_desc[irq].handler->ack(irq);})

#endif /* _PPC_HW_IRQ_H */
