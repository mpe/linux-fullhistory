#ifndef _ASM_IA64_MMU_CONTEXT_H
#define _ASM_IA64_MMU_CONTEXT_H

/*
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/sched.h>

#include <asm/processor.h>

/*
 * Routines to manage the allocation of task context numbers.  Task
 * context numbers are used to reduce or eliminate the need to perform
 * TLB flushes due to context switches.  Context numbers are
 * implemented using ia-64 region ids.  Since ia-64 TLBs do not
 * guarantee that the region number is checked when performing a TLB
 * lookup, we need to assign a unique region id to each region in a
 * process.  We use the least significant three bits in a region id
 * for this purpose.  On processors where the region number is checked
 * in TLB lookups, we can get back those two bits by defining
 * CONFIG_IA64_TLB_CHECKS_REGION_NUMBER.  The macro
 * IA64_REGION_ID_BITS gives the number of bits in a region id.  The
 * architecture manual guarantees this number to be in the range
 * 18-24.
 *
 * A context number has the following format:
 *
 *  +--------------------+---------------------+
 *  |  generation number |    region id        |
 *  +--------------------+---------------------+
 *
 * A context number of 0 is considered "invalid".
 *
 * The generation number is incremented whenever we end up having used
 * up all available region ids.  At that point with flush the entire
 * TLB and reuse the first region id.  The new generation number
 * ensures that when we context switch back to an old process, we do
 * not inadvertently end up using its possibly reused region id.
 * Instead, we simply allocate a new region id for that process.
 *
 * Copyright (C) 1998 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#define IA64_REGION_ID_KERNEL	0 /* the kernel's region id (tlb.c depends on this being 0) */

#define IA64_REGION_ID_BITS	18

#ifdef CONFIG_IA64_TLB_CHECKS_REGION_NUMBER
# define IA64_HW_CONTEXT_BITS	IA64_REGION_ID_BITS
#else
# define IA64_HW_CONTEXT_BITS	(IA64_REGION_ID_BITS - 3)
#endif

#define IA64_HW_CONTEXT_MASK	((1UL << IA64_HW_CONTEXT_BITS) - 1)

extern unsigned long ia64_next_context;

extern void get_new_mmu_context (struct mm_struct *mm);

static inline void
enter_lazy_tlb (struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}

extern inline unsigned long
ia64_rid (unsigned long context, unsigned long region_addr)
{
# ifdef CONFIG_IA64_TLB_CHECKS_REGION_NUMBER
	return context;
# else
	return context << 3 | (region_addr >> 61);
# endif
}

extern inline void
get_mmu_context (struct mm_struct *mm)
{
	/* check if our ASN is of an older generation and thus invalid: */
	if (((mm->context ^ ia64_next_context) & ~IA64_HW_CONTEXT_MASK) != 0) {
		get_new_mmu_context(mm);
	}
}

extern inline int
init_new_context (struct task_struct *p, struct mm_struct *mm)
{
	mm->context = 0;
	return 0;
}

extern inline void
destroy_context (struct mm_struct *mm)
{
	/* Nothing to do.  */
}

extern inline void
reload_context (struct mm_struct *mm)
{
	unsigned long rid;
	unsigned long rid_incr = 0;
	unsigned long rr0, rr1, rr2, rr3, rr4;

	rid = (mm->context & IA64_HW_CONTEXT_MASK);

#ifndef CONFIG_IA64_TLB_CHECKS_REGION_NUMBER
	rid <<= 3;	/* make space for encoding the region number */
	rid_incr = 1 << 8;
#endif

	/* encode the region id, preferred page size, and VHPT enable bit: */
	rr0 = (rid << 8) | (PAGE_SHIFT << 2) | 1;
	rr1 = rr0 + 1*rid_incr;
	rr2 = rr0 + 2*rid_incr;
	rr3 = rr0 + 3*rid_incr;
	rr4 = rr0 + 4*rid_incr;
	ia64_set_rr(0x0000000000000000, rr0);
	ia64_set_rr(0x2000000000000000, rr1);
	ia64_set_rr(0x4000000000000000, rr2);
	ia64_set_rr(0x6000000000000000, rr3);
	ia64_set_rr(0x8000000000000000, rr4);
	ia64_insn_group_barrier();
	ia64_srlz_i();			/* srlz.i implies srlz.d */
	ia64_insn_group_barrier();
}

/*
 * Switch from address space PREV to address space NEXT.  Note that
 * TSK may be NULL.
 */
static inline void
switch_mm (struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk, unsigned cpu)
{
	/*
	 * We may get interrupts here, but that's OK because interrupt
	 * handlers cannot touch user-space.
	 */
	__asm__ __volatile__ ("mov ar.k7=%0" :: "r"(__pa(next->pgd)));
	get_mmu_context(next);
	reload_context(next);
}

#define activate_mm(prev,next)					\
	switch_mm((prev), (next), NULL, smp_processor_id())

#endif /* _ASM_IA64_MMU_CONTEXT_H */
