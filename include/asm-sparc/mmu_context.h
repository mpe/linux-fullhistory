#ifndef __SPARC_MMU_CONTEXT_H
#define __SPARC_MMU_CONTEXT_H

#include <asm/btfixup.h>

#ifndef __ASSEMBLY__

/*
 * Initialize a new mmu context.  This is invoked when a new
 * address space instance (unique or shared) is instantiated.
 */
#define init_new_context(tsk, mm) ((mm)->context = NO_CONTEXT)

/*
 * Destroy a dead context.  This occurs when mmput drops the
 * mm_users count to zero, the mmaps have been released, and
 * all the page tables have been flushed.  Our job is to destroy
 * any remaining processor-specific state.
 */
BTFIXUPDEF_CALL(void, destroy_context, struct mm_struct *)

#define destroy_context(mm) BTFIXUP_CALL(destroy_context)(mm)

/* Switch the current MM context. */
BTFIXUPDEF_CALL(void, switch_mm, struct mm_struct *, struct mm_struct *, struct task_struct *, int)

#define switch_mm(old_mm, mm, tsk, cpu) BTFIXUP_CALL(switch_mm)(old_mm, mm, tsk, cpu)

/* Activate a new MM instance for the current task. */
#define activate_mm(active_mm, mm) switch_mm((active_mm), (mm), NULL, smp_processor_id())

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC_MMU_CONTEXT_H) */
