/*
 * linux/include/asm-arm/mmu_context.h
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  27-06-1996	RMK	Created
 */
#ifndef __ASM_ARM_MMU_CONTEXT_H
#define __ASM_ARM_MMU_CONTEXT_H

#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/arch/memory.h>

#define destroy_context(mm)		do { } while(0)
#define init_new_context(tsk,mm)	do { } while(0)

/*
 * This is the actual mm switch as far as the scheduler
 * is concerned.  No registers are touched.
 */
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, unsigned int cpu)
{
	if (prev != next) {
		processor.u.armv3v4._set_pgd(__virt_to_phys((unsigned long)next->pgd));
		clear_bit(cpu, &prev->cpu_vm_mask);
	}
	set_bit(cpu, &next->cpu_vm_mask);
}

#endif
