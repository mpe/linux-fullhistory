#ifndef __I386_MMU_CONTEXT_H
#define __I386_MMU_CONTEXT_H

#include <asm/desc.h>

/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)		do { } while(0)
#define init_new_context(tsk,mm)	do { } while (0)

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, unsigned cpu)
{
	unsigned long vm_mask;

	/*
	 * Re-load LDT if necessary
	 */
	if (prev->segments != next->segments)
		load_LDT(next);

	/* Re-load page tables */
	asm volatile("movl %0,%%cr3": :"r" (__pa(next->pgd)));

	vm_mask = 1UL << cpu;
	next->cpu_vm_mask |= vm_mask;
	prev->cpu_vm_mask &= ~vm_mask;
}

#endif
