#ifndef __I386_MMU_CONTEXT_H
#define __I386_MMU_CONTEXT_H

#include <asm/desc.h>

/*
 * get a new mmu context.. x86's don't know much about contexts,
 * but we have to reload the new LDT in exec().
 *
 * We implement lazy MMU context-switching on x86 to optimize context
 * switches done to/from kernel threads. Kernel threads 'inherit' the
 * previous MM, so Linux doesnt have to flush the TLB. In most cases
 * we switch back to the same process so we preserve the TLB cache.
 * This all means that kernel threads have about as much overhead as
 * a function call ...
 */
#define get_mmu_context(prev, next) \
	do { if (next->flags & PF_LAZY_TLB) \
		{ mmget(prev->mm); next->mm = prev->mm; \
			next->thread.cr3 = prev->thread.cr3; } } while(0)

#define put_mmu_context(prev, next) \
	do { if (prev->flags & PF_LAZY_TLB) \
			{ mmput(prev->mm); } } while(0)

#define init_new_context(mm)	do { } while(0)
/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)	do { } while(0)
#define activate_context(x)	load_LDT((x)->mm)

#endif
