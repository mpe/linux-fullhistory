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

/* This need not do anything on Sparc32.  The switch happens
 * properly later as a side effect of calling flush_thread.
 */
#define activate_context(tsk)	do { } while(0)

#endif /* !(__SPARC_MMU_CONTEXT_H) */
