/* $Id: mmu_context.h,v 1.20 1997/09/18 10:42:08 rth Exp $ */
#ifndef __SPARC64_MMU_CONTEXT_H
#define __SPARC64_MMU_CONTEXT_H

/* Derived heavily from Linus's Alpha/AXP ASN code... */

#include <asm/system.h>
#include <asm/spitfire.h>

#define NO_CONTEXT     0

#ifndef __ASSEMBLY__

extern unsigned long tlb_context_cache;

#define CTX_VERSION_SHIFT	PAGE_SHIFT
#define CTX_VERSION_MASK	((~0UL) << CTX_VERSION_SHIFT)
#define CTX_FIRST_VERSION	((1UL << CTX_VERSION_SHIFT) + 1UL)

extern void get_new_mmu_context(struct mm_struct *mm, unsigned long *ctx);

/* Initialize/destroy the context related info for a new mm_struct
 * instance.
 */
#define init_new_context(mm)	((mm)->context = NO_CONTEXT)
#define destroy_context(mm)	((mm)->context = NO_CONTEXT)

#ifdef __SMP__
#define LOCAL_FLUSH_PENDING(cpu)	\
	((cpu_data[(cpu)].last_tlbversion_seen ^ tlb_context_cache) & CTX_VERSION_MASK)
#define DO_LOCAL_FLUSH(cpu)		do { __flush_tlb_all();				\
					     cpu_data[cpu].last_tlbversion_seen =	\
					     tlb_context_cache & CTX_VERSION_MASK;	\
					} while(0)
#else
#define LOCAL_FLUSH_PENDING(cpu)	0
#define DO_LOCAL_FLUSH(cpu)		do { __flush_tlb_all(); } while(0)
#endif

extern void __flush_tlb_all(void);

extern __inline__ void get_mmu_context(struct task_struct *tsk)
{
	register unsigned long paddr asm("o5");
	struct mm_struct *mm = tsk->mm;

	flushw_user();
	if(LOCAL_FLUSH_PENDING(current->processor))
		DO_LOCAL_FLUSH(current->processor);
	if(!(tsk->tss.flags & SPARC_FLAG_KTHREAD)	&&
	   !(tsk->flags & PF_EXITING)) {
		unsigned long ctx = tlb_context_cache;
		if((mm->context ^ ctx) & CTX_VERSION_MASK)
			get_new_mmu_context(mm, &tlb_context_cache);

		/* Don't worry, set_fs() will restore it... */
		/* Sigh, damned include loops... just poke seg directly.  */
		tsk->tss.ctx = (tsk->tss.current_ds.seg ?
				(mm->context & 0x1fff) : 0);
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
