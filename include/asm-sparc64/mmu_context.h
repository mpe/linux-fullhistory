/* $Id: mmu_context.h,v 1.8 1997/05/18 20:44:23 davem Exp $ */
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

#define MAX_CTX			PAGE_SIZE

#define CTX_VERSION_SHIFT	PAGE_SHIFT
#define CTX_VERSION_MASK	((~0UL) << CTX_VERSION_SHIFT)
#define CTX_FIRST_VERSION	((1UL << CTX_VERSION_SHIFT) + 1UL)

extern __inline__ void get_new_mmu_context(struct mm_struct *mm,
					   unsigned long ctx)
{
	if((ctx & ~CTX_VERSION_MASK) > MAX_CTX) {
		unsigned long flags;
		int entry;

		save_and_cli(flags);
		__asm__ __volatile__("stxa	%%g0, [%0] %1\n\t"
				     "stxa	%%g0, [%0] %2"
				     : /* No outputs */
				     : "r" (TLB_TAG_ACCESS), "i" (ASI_IMMU),
				       "i" (ASI_DMMU));
		for(entry = 0; entry < 62; entry++) {
			spitfire_put_dtlb_data(entry, 0x0UL);
			spitfire_put_itlb_data(entry, 0x0UL);
		}
		membar("#Sync");
		flushi(PAGE_OFFSET);
		restore_flags(flags);

		ctx = (ctx & CTX_VERSION_MASK) + CTX_FIRST_VERSION;
		if(!ctx)
			ctx = CTX_FIRST_VERSION;
	}
	tlb_context_cache = ctx + 1;
	mm->context = ctx;
}

extern __inline__ void get_mmu_context(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;

	if(mm						&&
	   !(tsk->tss.flags & SPARC_FLAG_KTHREAD)	&&
	   !(tsk->flags & PF_EXITING)) {
		unsigned long ctx = tlb_context_cache;
		register unsigned long paddr asm("o5");

		flushw_user();
		if((mm->context ^ ctx) & CTX_VERSION_MASK)
			get_new_mmu_context(mm, ctx);
		spitfire_set_secondary_context(mm->context);
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
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
