#ifndef __I386_MMU_CONTEXT_H
#define __I386_MMU_CONTEXT_H

/*
 * get a new mmu context.. x86's don't know about contexts.
 */
#define get_mmu_context(x) do { } while (0)

#endif
