/*
 *  linux/arch/arm/mm/fault-armo.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-1999 Russell King
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

#define FAULT_CODE_FORCECOW	0x80
#define FAULT_CODE_PREFETCH	0x04
#define FAULT_CODE_WRITE	0x02
#define FAULT_CODE_USER		0x01

#define DO_COW(m)		((m) & (FAULT_CODE_WRITE|FAULT_CODE_FORCECOW))
#define READ_FAULT(m)		(!((m) & FAULT_CODE_WRITE))

#include "fault-common.c"

static void *alloc_table(int size, int prio)
{
	if (size != 128)
		printk("invalid table size\n");
	return (void *)get_page_8k(prio);
}

void free_table(void *table)
{
	free_page_8k((unsigned long)table);
}

pgd_t *get_pgd_slow(void)
{
	pgd_t *pgd = (pgd_t *)alloc_table(PTRS_PER_PGD * BYTES_PER_PTR, GFP_KERNEL);
	pgd_t *init;

	if (pgd) {
		init = pgd_offset(&init_mm, 0);
		memzero(pgd, USER_PTRS_PER_PGD * BYTES_PER_PTR);
		memcpy(pgd + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * BYTES_PER_PTR);
	}
	return pgd;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *)alloc_table(PTRS_PER_PTE * BYTES_PER_PTR, GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			memzero(pte, PTRS_PER_PTE * BYTES_PER_PTR);
			set_pmd(pmd, mk_pmd(pte));
			return pte + offset;
		}
		set_pmd(pmd, mk_pmd(BAD_PAGETABLE));
		return NULL;
	}
	free_table((void *)pte);
	if (pmd_bad(*pmd)) {
		__bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

/*
 * Handle a data abort.  Note that we have to handle a range of addresses
 * on ARM2/3 for ldm.  If both pages are zero-mapped, then we have to force
 * a copy-on-write.  However, on the second page, we always force COW.
 */
asmlinkage void
do_DataAbort(unsigned long min_addr, unsigned long max_addr, int mode, struct pt_regs *regs)
{
	do_page_fault(min_addr, mode, regs);

	if ((min_addr ^ max_addr) >> PAGE_SHIFT)
		do_page_fault(max_addr, mode | FAULT_CODE_FORCECOW, regs);
}

asmlinkage int
do_PrefetchAbort(unsigned long addr, struct pt_regs *regs)
{
#if 0
	if (the memc mapping for this page exists - can check now...) {
		printk ("Page in, but got abort (undefined instruction?)\n");
		return 0;
	}
#endif
	do_page_fault(addr, FAULT_CODE_USER|FAULT_CODE_PREFETCH, regs);
	return 1;
}
