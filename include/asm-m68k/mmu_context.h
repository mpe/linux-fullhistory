#ifndef __68K_MMU_CONTEXT_H
#define __68K_MMU_CONTEXT_H

/*
 * get a new mmu context.. do we need this on the m68k?
 */
#define get_mmu_context(x) do { } while (0)

#define init_new_context(mm)	do { } while(0)
#define destroy_context(mm)	do { } while(0)
#define activate_context(tsk)	do { } while(0)

#endif
