#ifndef __M68K_MMU_CONTEXT_H
#define __M68K_MMU_CONTEXT_H

#include <asm/setup.h>
#include <asm/page.h>

extern inline void
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context = virt_to_phys(mm->pgd);
}

#define destroy_context(mm)		do { } while(0)

extern inline void switch_mm_0230(struct mm_struct *mm)
{
	unsigned long crp[2] = {
		0x80000000 | _PAGE_TABLE, mm->context
	};
	unsigned long tmp;

	asm volatile (".chip 68030");

	/* flush MC68030/MC68020 caches (they are virtually addressed) */
	asm volatile (
		"movec %%cacr,%0;"
		"orw %1,%0; "
		"movec %0,%%cacr"
		: "=d" (tmp) : "di" (FLUSH_I_AND_D));

	/* Switch the root pointer. For a 030-only kernel,
	 * avoid flushing the whole ATC, we only need to
	 * flush the user entries. The 68851 does this by
	 * itself. Avoid a runtime check here.
	 */
	asm volatile (
#ifdef CPU_M68030_ONLY
		"pmovefd %0,%%crp; "
		"pflush #0,#4"
#else
		"pmove %0,%%crp"
#endif
		: : "m" (crp[0]));

	asm volatile (".chip 68k");
}

extern inline void switch_mm_0460(struct mm_struct *mm)
{
	asm volatile (".chip 68040");

	/* flush address translation cache (user entries) */
	asm volatile ("pflushan");

	/* switch the root pointer */
	asm volatile ("movec %0,%%urp" : : "r" (mm->context));

	if (CPU_IS_060) {
		unsigned long tmp;

		/* clear user entries in the branch cache */
		asm volatile (
			"movec %%cacr,%0; "
		        "orl %1,%0; "
		        "movec %0,%%cacr"
			: "=d" (tmp): "di" (0x00200000));
	}

	asm volatile (".chip 68k");
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk, unsigned cpu)
{
	if (prev != next) {
		if (CPU_IS_020_OR_030)
			switch_mm_0230(next);
		else
			switch_mm_0460(next);
	}
}

extern inline void activate_mm(struct mm_struct *prev_mm,
			       struct mm_struct *next_mm)
{
	next_mm->context = virt_to_phys(next_mm->pgd);

	if (CPU_IS_020_OR_030)
		switch_mm_0230(next_mm);
	else
		switch_mm_0460(next_mm);
}

#endif
