/* $Id: mmu_context.h,v 1.4 1996/12/28 18:39:51 davem Exp $ */
#ifndef __SPARC64_MMU_CONTEXT_H
#define __SPARC64_MMU_CONTEXT_H

#include <asm/system.h>
#include <asm/spitfire.h>

#define NO_CONTEXT     -1

#ifndef __ASSEMBLY__

/* Initialize the context related info for a new mm_struct
 * instance.
 */
#define init_new_context(mm) ((mm)->context = NO_CONTEXT)

extern void spitfire_get_new_context(struct mm_struct *mm);

extern __inline__ void get_mmu_context(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;

	if(tsk->mm->context == NO_CONTEXT)
		spitfire_get_new_context(mm);

	/* Get current set of user windows out of the cpu. */
	flushw_user();

	/* Jump into new ASN. */
	spitfire_set_primary_context(mm->context);
	spitfire_set_secondary_context(mm->context);
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC64_MMU_CONTEXT_H) */
