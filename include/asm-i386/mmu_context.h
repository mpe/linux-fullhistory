#ifndef __I386_MMU_CONTEXT_H
#define __I386_MMU_CONTEXT_H

#include <asm/desc.h>

/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)		do { } while(0)
#define init_new_context(tsk,mm)	do { } while (0)

static inline void activate_context(void)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

	load_LDT(mm);
	__asm__ __volatile__("movl %0,%%cr3": :"r" (__pa(mm->pgd)));
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next)
{
	/*
	 * Re-load LDT if necessary
	 */
	if (prev->segments != next->segments)
		load_LDT(next);

	/* Re-load page tables */
	asm volatile("movl %0,%%cr3": :"r" (__pa(next->pgd)));
}

#endif
