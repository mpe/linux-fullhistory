#ifndef __I386_MMU_CONTEXT_H
#define __I386_MMU_CONTEXT_H

#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>

/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)		do { } while(0)
#define init_new_context(tsk,mm)	do { } while (0)

#ifdef __SMP__

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
	if(cpu_tlbstate[cpu].state == TLBSTATE_OK)
		cpu_tlbstate[cpu].state = TLBSTATE_LAZY;	
}
#else
static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}
#endif

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk, unsigned cpu)
{
	set_bit(cpu, &next->cpu_vm_mask);
	if (prev != next) {
		/*
		 * Re-load LDT if necessary
		 */
		if (prev->segments != next->segments)
			load_LDT(next);
#ifdef CONFIG_SMP
		cpu_tlbstate[cpu].state = TLBSTATE_OK;
		cpu_tlbstate[cpu].active_mm = next;
#endif
		/* Re-load page tables */
		asm volatile("movl %0,%%cr3": :"r" (__pa(next->pgd)));
		clear_bit(cpu, &prev->cpu_vm_mask);
	}
#ifdef __SMP__
	else {
		int old_state = cpu_tlbstate[cpu].state;
		cpu_tlbstate[cpu].state = TLBSTATE_OK;
		if(cpu_tlbstate[cpu].active_mm != next)
			BUG();
		if(old_state == TLBSTATE_OLD)
			local_flush_tlb();
	}

#endif
}

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL,smp_processor_id())

#endif
