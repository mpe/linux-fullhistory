#ifndef __ASM_SH_PGALLOC_2LEVEL_H
#define __ASM_SH_PGALLOC_2LEVEL_H

/*
 * traditional two-level paging, page table allocation routines:
 */

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

#endif /* __ASM_SH_PGALLOC_2LEVEL_H */
