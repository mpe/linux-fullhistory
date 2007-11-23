/* $Id: mmu_context.h,v 1.4 2000/02/23 00:41:38 ralf Exp $
 *
 * Switch a MMU context.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_MMU_CONTEXT_H
#define _ASM_MMU_CONTEXT_H

#include <linux/config.h>
#include <linux/slab.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>

#ifndef CONFIG_SMP
#define CPU_CONTEXT(cpu, mm)	(mm)->context
#else
#define CPU_CONTEXT(cpu, mm)	(*((unsigned long *)((mm)->context) + cpu))
#endif
#define ASID_CACHE(cpu)		cpu_data[cpu].asid_cache

#define ASID_INC	0x1
#define ASID_MASK	0xff

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}

/*
 *  All unused by hardware upper bits will be considered
 *  as a software asid extension.
 */
#define ASID_VERSION_MASK  ((unsigned long)~(ASID_MASK|(ASID_MASK-1)))
#define ASID_FIRST_VERSION ((unsigned long)(~ASID_VERSION_MASK) + 1)

extern inline void
get_new_cpu_mmu_context(struct mm_struct *mm, unsigned long cpu)
{
	unsigned long asid = ASID_CACHE(cpu);

	if (! ((asid += ASID_INC) & ASID_MASK) ) {
		_flush_tlb_all(); /* start new asid cycle */
		if (!asid)      /* fix version if needed */
			asid = ASID_FIRST_VERSION;
	}
	CPU_CONTEXT(cpu, mm) = ASID_CACHE(cpu) = asid;
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
extern inline void
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
#ifndef CONFIG_SMP
	mm->context = 0;
#else
	/* Make sure not to do anything during a clone-vm operation */
	if ((current == tsk) || (current->mm != mm)) {
		mm->context = (unsigned long)kmalloc(smp_num_cpus * 
					sizeof(unsigned long), GFP_KERNEL);
		/*
	 	 * Init the "context" values so that a tlbpid allocation 
	 	 * happens on the first switch.
	 	 */
		if (mm->context)
			memset((void *)mm->context, 0, smp_num_cpus * 
							sizeof(unsigned long));
		else
			printk("Warning: init_new_context failed\n");
	}
#endif
}

extern inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
                             struct task_struct *tsk, unsigned cpu)
{
	/* Check if our ASID is of an older version and thus invalid */
	if ((CPU_CONTEXT(cpu, next) ^ ASID_CACHE(cpu)) & ASID_VERSION_MASK)
		get_new_cpu_mmu_context(next, cpu);

	set_entryhi(CPU_CONTEXT(cpu, next));
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
extern inline void destroy_context(struct mm_struct *mm)
{
#ifdef CONFIG_SMP
	if (mm->context)
		kfree((void *)mm->context);
#endif
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
extern inline void
activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	/* Unconditionally get a new ASID.  */
	get_new_cpu_mmu_context(next, smp_processor_id());

	set_entryhi(CPU_CONTEXT(smp_processor_id(), next));
}

#endif /* _ASM_MMU_CONTEXT_H */
