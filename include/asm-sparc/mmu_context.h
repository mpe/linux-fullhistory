#ifndef __SPARC_MMU_CONTEXT_H
#define __SPARC_MMU_CONTEXT_H

#include <asm/btfixup.h>

/* For now I still leave the context handling in the
 * switch_to() macro, I'll do it right soon enough.
 */
#define get_mmu_context(x) do { } while (0)

/* Initialize the context related info for a new mm_struct
 * instance.
 */
BTFIXUPDEF_CALL(void, init_new_context, struct mm_struct *)

#define init_new_context(mm) BTFIXUP_CALL(init_new_context)(mm)

/* Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
BTFIXUPDEF_CALL(void, destroy_context, struct mm_struct *)

#define destroy_context(mm) BTFIXUP_CALL(destroy_context)(mm)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 * XXX this presumably needs a sensible implementation - paulus.
 */
#define activate_context(tsk)	do { } while(0)

#endif /* !(__SPARC_MMU_CONTEXT_H) */
