/* $Id: mmu_context.h,v 1.16 1997/07/05 09:54:46 davem Exp $ */
#ifndef __SPARC64_MMU_CONTEXT_H
#define __SPARC64_MMU_CONTEXT_H

/* Derived heavily from Linus's Alpha/AXP ASN code... */

#include <asm/system.h>
#include <asm/spitfire.h>

#define NO_CONTEXT     0

#ifndef __ASSEMBLY__

/* Initialize the context related info for a new mm_struct
 * instance.
 */
#define init_new_context(mm)	((mm)->context = NO_CONTEXT)

#define destroy_context(mm)	do { } while(0)

extern unsigned long tlb_context_cache;

#define CTX_VERSION_SHIFT	PAGE_SHIFT
#define CTX_VERSION_MASK	((~0UL) << CTX_VERSION_SHIFT)
#define CTX_FIRST_VERSION	((1UL << CTX_VERSION_SHIFT) + 1UL)

extern void get_new_mmu_context(struct mm_struct *mm, unsigned long ctx);

extern __inline__ void get_mmu_context(struct task_struct *tsk)
{
	register unsigned long paddr asm("o5");
	struct mm_struct *mm = tsk->mm;

	flushw_user();
	if(!(tsk->tss.flags & SPARC_FLAG_KTHREAD)	&&
	   !(tsk->flags & PF_EXITING)) {
		unsigned long ctx = tlb_context_cache;
		if((mm->context ^ ctx) & CTX_VERSION_MASK)
			get_new_mmu_context(mm, ctx);

		/* Don't worry, set_fs() will restore it... */
		tsk->tss.ctx = (tsk->tss.current_ds ?
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

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
