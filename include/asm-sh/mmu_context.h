/*
 * Copyright (C) 1999 Niibe Yutaka
 *
 * ASID handling idea taken from MIPS implementation.
 */
#ifndef __ASM_SH_MMU_CONTEXT_H
#define __ASM_SH_MMU_CONTEXT_H

/* The MMU "context" consists of two things:
     (a) TLB cache version (or round, cycle whatever expression you like)
     (b) ASID (Address Space IDentifier)
 */

/*
 * Cache of MMU context last used.
 */
extern unsigned long mmu_context_cache;

#define MMU_CONTEXT_ASID_MASK 0xff
#define MMU_CONTEXT_VERSION_MASK 0xffffff00
#define MMU_CONTEXT_FIRST_VERSION 0x100
#define NO_CONTEXT 0

extern __inline__ void
get_new_mmu_context(struct mm_struct *mm)
{
	unsigned long mc = ++mmu_context_cache;

	if (!(mc & MMU_CONTEXT_ASID_MASK)) {
		/* We exhaust ASID of this version.
		   Flush all TLB and start new cycle. */
		flush_tlb_all();
		/* Fix version if needed.
		   Note that we avoid version #0 to distingush NO_CONTEXT. */
		if (!mc) 
			mmu_context_cache = mc = MMU_CONTEXT_FIRST_VERSION;
	}
	mm->context = mc;
}

/*P
 * Get MMU context if needed.
 */
extern __inline__ void
get_mmu_context(struct mm_struct *mm)
{
	if (mm) {
		unsigned long mc = mmu_context_cache;
		/* Check if we have old version of context.
		   If it's old, we need to get new context with new version. */
		if ((mm->context ^ mc) & MMU_CONTEXT_VERSION_MASK)
			get_new_mmu_context(mm);
	}
}

/*P
 * Initialize the context related info for a new mm_struct
 * instance.
 */
extern __inline__ void init_new_context(struct task_struct *tsk,
					struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;
}

/*P
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
extern __inline__ void destroy_context(struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;
}

/* Other MMU related constants. */

#define MMU_PTEH	0xFFFFFFF0	/* Page table entry register HIGH */
#define MMU_PTEL	0xFFFFFFF4	/* Page table entry register LOW */
#define MMUCR		0xFFFFFFE0	/* MMU Control Register */

#define MMU_TLB_ADDRESS_ARRAY 0xF2000000
#define MMU_PAGE_ASSOC_BIT 0x80

#define MMU_NTLB_ENTRIES       128	/* for 7708 */

#define MMU_CONTROL_INIT 0x007	/* SV=0, TF=1, IX=1, AT=1 */

#include <asm/uaccess.h> /* to get the definition of  __m */

extern __inline__ void set_asid (unsigned long asid)
{
	__asm__ __volatile__ ("mov.l	%0,%1"
			      : /* no output */
			      : "r" (asid), "m" (__m(MMU_PTEH)));
}

extern __inline__ unsigned long get_asid (void)
{
	unsigned long asid;

	__asm__ __volatile__ ("mov.l %1,%0"
			      : "=r" (asid)
			      : "m" (__m(MMU_PTEH)));
	asid &= MMU_CONTEXT_ASID_MASK;
	return asid;
}

/*P
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
extern __inline__ void activate_context(struct mm_struct *mm)
{
	get_mmu_context(mm);
	set_asid(mm->context & MMU_CONTEXT_ASID_MASK);
}

/* MMU_TTB can be used for optimizing the fault handling.
   (Currently not used) */
#define MMU_TTB   0xFFFFFFF8	/* Translation table base register */
extern __inline__ void switch_mm(struct mm_struct *prev,
				 struct mm_struct *next,
				 struct task_struct *tsk, unsigned int cpu)
{
	if (prev != next) {
		unsigned long __pgdir = __pa(next->pgd);

		__asm__ __volatile__("mov.l	%0,%1": \
				     :"r" (__pgdir), "m" (__m(MMU_TTB)));
		activate_context(next);
		clear_bit(cpu, &prev->cpu_vm_mask);
	}
	set_bit(cpu, &next->cpu_vm_mask);
}

#define activate_mm(prev, next) \
	switch_mm((prev),(next),NULL,smp_processor_id())

#endif /* __ASM_SH_MMU_CONTEXT_H */
