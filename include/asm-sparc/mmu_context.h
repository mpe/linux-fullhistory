#ifndef __SPARC_MMU_CONTEXT_H
#define __SPARC_MMU_CONTEXT_H

/* For now I still leave the context handling in the
 * switch_to() macro, I'll do it right soon enough.
 */
#define get_mmu_context(x) do { } while (0)

/* Initialize the context related info for a new mm_struct
 * instance.
 */
#define init_new_context(mm) ((mm)->context = NO_CONTEXT)

#endif /* !(__SPARC_MMU_CONTEXT_H) */
