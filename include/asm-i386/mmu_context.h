#ifndef __I386_MMU_CONTEXT_H
#define __I386_MMU_CONTEXT_H

#include <asm/desc.h>

/*
 * get a new mmu context.. x86's don't know much about contexts,
 * but we have to reload the new LDT in exec(). 
 */
#define get_mmu_context(tsk)	do { } while(0)

#define init_new_context(mm)	do { } while(0)
/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)	do { } while(0)
#define activate_context(x)	load_LDT((x)->mm)

#endif
