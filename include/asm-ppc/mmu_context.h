#ifndef __PPC_MMU_CONTEXT_H
#define __PPC_MMU_CONTEXT_H

/*
 * get a new mmu context.. PowerPC's don't know about contexts [yet]
 */
#define get_mmu_context(x) do { } while (0)

#define init_new_context(mm)	do { } while(0)
#define destroy_context(mm)	do { } while(0)

#endif

