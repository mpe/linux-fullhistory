#ifndef __SPARC_MMU_CONTEXT_H
#define __SPARC_MMU_CONTEXT_H

/* For now I still leave the context handling in the
 * switch_to() macro, I'll do it right soon enough.
 */
#define get_mmu_context(x) do { } while (0)

/* Initialize the context related info for a new mm_struct
 * instance.
 */
extern void (*init_new_context)(struct mm_struct *mm);

/* Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
extern void (*destroy_context)(struct mm_struct *mm);

#endif /* !(__SPARC_MMU_CONTEXT_H) */
