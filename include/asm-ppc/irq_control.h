/*
 * $Id: irq_control.h,v 1.2 1999/07/17 20:23:58 cort Exp $
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _PPC_IRQ_CONTROL_H
#define _PPC_IRQ_CONTROL_H

#include <linux/config.h>

#include <asm/irq.h>
#include <asm/atomic.h>

extern void do_lost_interrupts(unsigned long);
extern atomic_t ppc_n_lost_interrupts;

#define __no_use_save_flags(flags) \
	({__asm__ __volatile__ ("mfmsr %0" : "=r" ((flags)) : : "memory"); })

extern __inline__ void __no_use_restore_flags(unsigned long flags)
{
        if ((flags & MSR_EE) && atomic_read(&ppc_n_lost_interrupts) != 0) {
                do_lost_interrupts(flags);
        } else {
                __asm__ __volatile__ ("sync; mtmsr %0; isync"
                              : : "r" (flags) : "memory");
        }
}

extern __inline__ void __no_use_sti(void)
{
	unsigned long flags;

	__asm__ __volatile__ ("mfmsr %0": "=r" (flags));
	flags |= MSR_EE;
	if ( atomic_read(&ppc_n_lost_interrupts) )
		do_lost_interrupts(flags);
	__asm__ __volatile__ ("sync; mtmsr %0; isync":: "r" (flags));
}

extern __inline__ void __no_use_cli(void)
{
	unsigned long flags;
	__asm__ __volatile__ ("mfmsr %0": "=r" (flags));
	flags &= ~MSR_EE;
	__asm__ __volatile__ ("sync; mtmsr %0; isync":: "r" (flags));
}

#define __no_use_mask_irq(irq) ({if (irq_desc[irq].ctl && irq_desc[irq].ctl->disable) irq_desc[irq].ctl->disable(irq);})
#define __no_use_unmask_irq(irq) ({if (irq_desc[irq].ctl && irq_desc[irq].ctl->enable) irq_desc[irq].ctl->enable(irq);})
#define __no_use_mask_and_ack_irq(irq) ({if (irq_desc[irq].ctl && irq_desc[irq].ctl->mask_and_ack) irq_desc[irq].ctl->mask_and_ack(irq);})

#ifdef CONFIG_RTL

/* the rtl system provides these -- Cort */
extern void __sti(void);
extern void __cli(void);
extern void __restore_flags(unsigned int);
extern unsigned int __return_flags(void);
#define __save_flags(flags) (flags = __return_flags())

#define rtl_hard_cli __no_use_cli
#define rtl_hard_sti __no_use_sti
#define rtl_hard_save_flags(flags) __no_use_save_flags(flags)
#define rtl_hard_restore_flags(flags) __no_use_restore_flags(flags)

#define rtl_hard_mask_irq(irq) __no_use_mask_irq(irq)
#define rtl_hard_unmask_irq(irq) __no_use_unmask_irq(irq)
#define rtl_hard_mask_and_ack_irq(irq) __no_use_mask_and_ack_irq(irq)

#else /* CONFIG_RTL */

#define __cli __no_use_cli
#define __sti __no_use_sti
#define __save_flags(flags) __no_use_save_flags(flags)
#define __restore_flags(flags) __no_use_restore_flags(flags)

#define mask_irq(irq) __no_use_mask_irq(irq)
#define unmask_irq(irq) __no_use_unmask_irq(irq)
#define mask_and_ack_irq(irq) __no_use_mask_and_ack_irq(irq)

#endif /* CONFIG_RTL */

#endif /* _PPC_IRQ_CONTROL_H */
