/* $Id: mmu_context.h,v 1.24 1998/05/06 02:07:54 paulus Exp $ */
#ifndef __SPARC64_MMU_CONTEXT_H
#define __SPARC64_MMU_CONTEXT_H

/* Derived heavily from Linus's Alpha/AXP ASN code... */

#include <asm/system.h>
#include <asm/spitfire.h>
#include <asm/spinlock.h>

#define NO_CONTEXT     0

#ifndef __ASSEMBLY__

extern unsigned long tlb_context_cache;
extern spinlock_t scheduler_lock;
extern unsigned long mmu_context_bmap[];

#define CTX_VERSION_SHIFT	(PAGE_SHIFT - 3)
#define CTX_VERSION_MASK	((~0UL) << CTX_VERSION_SHIFT)
#define CTX_FIRST_VERSION	((1UL << CTX_VERSION_SHIFT) + 1UL)

extern void get_new_mmu_context(struct mm_struct *mm);

/* Initialize/destroy the context related info for a new mm_struct
 * instance.
 */
#define init_new_context(mm)	((mm)->context = NO_CONTEXT)
#define destroy_context(mm)	do { 								\
	if ((mm)->context != NO_CONTEXT) { 							\
		spin_lock(&scheduler_lock); 							\
		if (!(((mm)->context ^ tlb_context_cache) & CTX_VERSION_MASK))			\
			clear_bit((mm)->context & ~(CTX_VERSION_MASK), mmu_context_bmap);	\
		spin_unlock(&scheduler_lock); 							\
		(mm)->context = NO_CONTEXT; 							\
	} 											\
} while (0)

extern __inline__ void get_mmu_context(struct task_struct *tsk)
{
	register unsigned long paddr asm("o5");
	struct mm_struct *mm = tsk->mm;

	flushw_user();
	if(!(tsk->tss.flags & SPARC_FLAG_KTHREAD)	&&
	   !(tsk->flags & PF_EXITING)) {
		unsigned long ctx = tlb_context_cache;
		if((mm->context ^ ctx) & CTX_VERSION_MASK)
			get_new_mmu_context(mm);
		if(!(mm->cpu_vm_mask & (1UL<<smp_processor_id()))) {
			spitfire_set_secondary_context(mm->context & 0x3ff);
			__asm__ __volatile__("flush %g6");
			spitfire_flush_dtlb_secondary_context();
			spitfire_flush_itlb_secondary_context();
			__asm__ __volatile__("flush %g6");
		}
		/* Don't worry, set_fs() will restore it... */
		/* Sigh, damned include loops... just poke seg directly.  */
		tsk->tss.ctx = (tsk->tss.current_ds.seg ?
				(mm->context & 0x3ff) : 0);
	} else
		tsk->tss.ctx = 0;
	spitfire_set_secondary_context(tsk->tss.ctx);
	__asm__ __volatile__("flush %g6");
	paddr = __pa(mm->pgd);
	__asm__ __volatile__("
		rdpr		%%pstate, %%o4
		wrpr		%%o4, %1, %%pstate
		mov		%0, %%g7
		wrpr		%%o4, 0x0, %%pstate
	" : /* no outputs */
	  : "r" (paddr), "i" (PSTATE_MG|PSTATE_IE)
	  : "o4");
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
#define activate_context(tsk)	get_mmu_context(tsk)

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
