#ifndef __I386_MMU_CONTEXT_H
#define __I386_MMU_CONTEXT_H

#include <asm/desc.h>
#include <asm/atomic.h>

/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)		do { } while(0)
#define init_new_context(tsk,mm)	do { } while (0)

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk, unsigned cpu)
{

	if (prev != next) {
		/*
		 * Re-load LDT if necessary
		 */
		if (prev->segments != next->segments)
			load_LDT(next);

		/* Re-load page tables */
		asm volatile("movl %0,%%cr3": :"r" (__pa(next->pgd)));
		clear_bit(cpu, &prev->cpu_vm_mask);
	}
	set_bit(cpu, &next->cpu_vm_mask);
}

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL,smp_processor_id())

#endif
