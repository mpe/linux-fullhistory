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
	printk("%s:%d: bad pte %016Lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %016Lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %016Lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Subtle, in PAE mode we cannot have zeroes in the top level
 * page directory, the CPU enforces this.
 */
#define pgd_none(x)	(pgd_val(x) == 1ULL)
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return !pgd_none(pgd); }
/*
 * Pentium-II errata A13: in PAE mode we explicitly have to flush
 * the TLB via cr3 if the top-level pgd is changed... This was one tough
 * thing to find out - guess i should first read all the documentation
 * next time around ;)
 */
extern inline void __pgd_clear (pgd_t * pgd)
{
	pgd_val(*pgd) = 1; // no zero allowed!
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

extern __inline__ pmd_t *get_pmd_slow(void)
{
	pmd_t *ret = (pmd_t *)__get_free_page(GFP_KERNEL);

	if (ret)
		memset(ret, 0, PAGE_SIZE);
	return ret;
}

extern __inline__ pmd_t *get_pmd_fast(void)
{
	unsigned long *ret;

	if ((ret = pmd_quicklist) != NULL) {
		pmd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	} else
		ret = (unsigned long *)get_pmd_slow();
	return (pmd_t *)ret;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long) pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	pgtable_cache_size++;
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	if (!pgd)
		BUG();
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_pmd_fast();

		if (!page)
			page = get_pmd_slow();
		if (page) {
			if (pgd_none(*pgd)) {
				pgd_val(*pgd) = 1 + __pa(page);
				__flush_tlb();
				return page + address;
			} else
				free_pmd_fast(page);
		} else
			return NULL;
	}
	return (pmd_t *)pgd_page(*pgd) + address;
}

#endif /* _I386_PGTABLE_3LEVEL_H */
