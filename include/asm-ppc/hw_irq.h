/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifdef __KERNEL__
#ifndef _PPC_HW_IRQ_H
#define _PPC_HW_IRQ_H

extern unsigned long timer_interrupt_intercept;
extern unsigned long do_IRQ_intercept;
extern int timer_interrupt(struct pt_regs *);
extern void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq);

#define INLINE_IRQS

#ifdef INLINE_IRQS

#define mfmsr()		({unsigned int rval; \
			asm volatile("mfmsr %0" : "=r" (rval)); rval;})
#define mtmsr(v)	asm volatile("mtmsr %0" : : "r" (v))

#define __save_flags(flags)	((flags) = mfmsr())
#define __restore_flags(flags)	mtmsr(flags)

static inline void __cli(void)
{
	unsigned long msr;
	msr = mfmsr();
	mtmsr(msr & ~MSR_EE);
	__asm__ __volatile__("": : :"memory");
}

static inline void __sti(void)
{
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
	mtmsr(msr | MSR_EE);
}

static inline void __do_save_and_cli(unsigned long *flags)
{
	unsigned long msr;
	msr = mfmsr();
	*flags = msr;
	mtmsr(msr & ~MSR_EE);
	__asm__ __volatile__("": : :"memory");
}

#define __save_and_cli(flags)          __do_save_and_cli(&flags)

#else

extern void __sti(void);
extern void __cli(void);
extern void __restore_flags(unsigned long);
extern void __save_flags_ptr(unsigned long *);
extern unsigned long __sti_end, __cli_end, __restore_flags_end, __save_flags_ptr_end;

#define __save_flags(flags) __save_flags_ptr((unsigned long *)&flags)
#define __save_and_cli(flags) ({__save_flags(flags);__cli();})

#endif

extern void do_lost_interrupts(unsigned long);

#define mask_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->disable) irq_desc[irq].handler->disable(irq);})
#define unmask_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->enable) irq_desc[irq].handler->enable(irq);})
#define ack_irq(irq) ({if (irq_desc[irq].handler && irq_desc[irq].handler->ack) irq_desc[irq].handler->ack(irq);})

/* Should we handle this via lost interrupts and IPIs or should we don't care like
 * we do now ? --BenH.
 */
struct hw_interrupt_type;
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {}


#endif /* _PPC_HW_IRQ_H */
#endif /* __KERNEL__ */
