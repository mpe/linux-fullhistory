#ifndef _I386_PGTABLE_3LEVEL_H
#define _I386_PGTABLE_3LEVEL_H

/*
 * Intel Physical Address Extension (PAE) Mode - three-level page
 * tables on PPro+ CPUs.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

/*
 * PGDIR_SHIFT determines what a top-level page table entry can map
 */
#define PGDIR_SHIFT	30
#define PTRS_PER_PGD	4

/*
 * PMD_SHIFT determines the size of the area a middle-level
 * page table can map
 */
#define PMD_SHIFT	21
#define PTRS_PER_PMD	512

/*
 * entries per page directory level
 */
#define PTRS_PER_PTE	512

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %p(%016Lx).\n", __FILE__, __LINE__, &(e), pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %p(%016Lx).\n", __FILE__, __LINE__, &(e), pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %p(%016Lx).\n", __FILE__, __LINE__, &(e), pgd_val(e))

/*
 * Subtle, in PAE mode we cannot have zeroes in the top level
 * page directory, the CPU enforces this. (ie. the PGD entry
 * always has to have the present bit set.) The CPU caches
 * the 4 pgd entries internally, so there is no extra memory
 * load on TLB miss, despite one more level of indirection.
 */
#define pgd_none(x)	(pgd_val(x) == 1ULL)
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return !pgd_none(pgd); }

#define set_pte(pteptr,pteval) \
		set_64bit((unsigned long long *)(pteptr),pte_val(pteval))
#define set_pmd(pmdptr,pmdval) \
		set_64bit((unsigned long long *)(pmdptr),pmd_val(pmdval))
#define set_pgd(pgdptr,pgdval) \
		set_64bit((unsigned long long *)(pgdptr),pgd_val(pgdval))

/*
 * Pentium-II errata A13: in PAE mode we explicitly have to flush
 * the TLB via cr3 if the top-level pgd is changed... This was one tough
 * thing to find out - guess i should first read all the documentation
 * next time around ;)
 */
extern inline void __pgd_clear (pgd_t * pgd)
{
	set_pgd(pgd, __pgd(1ULL));
}

extern inline void pgd_clear (pgd_t * pgd)
{
	__pgd_clear(pgd);
	__flush_tlb();
}

#define pgd_page(pgd) \
((unsigned long) __va(pgd_val(pgd) & PAGE_MASK))

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, address) ((pmd_t *) pgd_page(*(dir)) + \
			__pmd_offset(address))

#endif /* _I386_PGTABLE_3LEVEL_H */
