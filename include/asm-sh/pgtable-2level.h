#ifndef __ASM_SH_PGTABLE_2LEVEL_H
#define __ASM_SH_PGTABLE_2LEVEL_H

/*
 * traditional two-level paging structure:
 */

#define PGDIR_SHIFT	22
#define PTRS_PER_PGD	1024

/*
 * this is two-level, so we don't really have any
 * PMD directory physically.
 */
#define PMD_SHIFT	22
#define PTRS_PER_PMD	1

#define PTRS_PER_PTE	1024

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)		{ return 0; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return 1; }
#define pgd_clear(xp)	do { } while (0)

#define pgd_page(pgd) \
((unsigned long) __va(pgd_val(pgd) & PAGE_MASK))

extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

extern __inline__ pmd_t *get_pmd_fast(void)
{
	return (pmd_t *)0;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd) { }
extern __inline__ void free_pmd_slow(pmd_t *pmd) { }

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	if (!pgd)
		BUG();
	return (pmd_t *) pgd;
}

#endif /* __ASM_SH_PGTABLE_2LEVEL_H */
