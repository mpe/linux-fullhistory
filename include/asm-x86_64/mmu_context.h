#ifndef __X86_64_MMU_CONTEXT_H
#define __X86_64_MMU_CONTEXT_H

#include <linux/config.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/pgalloc.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

/*
 * possibly do the LDT unload here?
 */
#define destroy_context(mm)		do { } while(0)
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);

#ifdef CONFIG_SMP

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
	if (read_pda(mmu_state) == TLBSTATE_OK) 
		write_pda(mmu_state, TLBSTATE_LAZY);
}
#else
static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}
#endif

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
			     struct task_struct *tsk, unsigned cpu)
{
	if (likely(prev != next)) {
		/* stop flush ipis for the previous mm */
		clear_bit(cpu, &prev->cpu_vm_mask);
#ifdef CONFIG_SMP
		write_pda(mmu_state, TLBSTATE_OK);
		write_pda(active_mm, next);
#endif
		set_bit(cpu, &next->cpu_vm_mask);
		/* Re-load page tables */
		*read_pda(level4_pgt) = __pa(next->pgd) | _PAGE_TABLE;
		__flush_tlb();

		if (next->context.size + prev->context.size) 
			load_LDT(&next->context);
	}
#ifdef CONFIG_SMP
	else {
		write_pda(mmu_state, TLBSTATE_OK);
		if (read_pda(active_mm) != next)
			out_of_line_bug();
		if(!test_and_set_bit(cpu, &next->cpu_vm_mask)) {
			/* We were in lazy tlb mode and leave_mm disabled 
			 * tlb flush IPI delivery. We must flush our tlb.
			 */
			local_flush_tlb();
			load_LDT(&next->context);
		}
	}
#endif
}

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL,smp_processor_id())


#endif
