#ifndef _MOTOROLA_PGALLOC_H
#define _MOTOROLA_PGALLOC_H

#include <asm/tlb.h>

extern pmd_t *get_pointer_table(void);
extern int free_pointer_table(pmd_t *);


static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pte) {
		clear_page(pte);
		__flush_page_to_ram((unsigned long)pte);
		flush_tlb_kernel_page((unsigned long)pte);
		nocache_page((unsigned long)pte);
	}

	return pte;
}

static inline void pte_free_kernel(pte_t *pte)
{
	cache_page((unsigned long)pte);
	free_page((unsigned long) pte);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *page = alloc_pages(GFP_KERNEL, 0);
	pte_t *pte;

	if(!page)
		return NULL;

	pte = kmap(page);
	if (pte) {
		clear_page(pte);
		__flush_page_to_ram((unsigned long)pte);
		flush_tlb_kernel_page((unsigned long)pte);
		nocache_page((unsigned long)pte);
	}
	kunmap(pte);

	return page;
}

static inline void pte_free(struct page *page)
{
	cache_page((unsigned long)kmap(page));
	kunmap(page);
	__free_page(page);
}

static inline void pte_free_tlb(mmu_gather_t *tlb, struct page *page)
{
	cache_page((unsigned long)kmap(page));
	kunmap(page);
	__free_page(page);
}


static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return get_pointer_table();
}

static inline int pmd_free(pmd_t *pmd)
{
	return free_pointer_table(pmd);
}

static inline int pmd_free_tlb(mmu_gather_t *tlb, pmd_t *pmd)
{
	return free_pointer_table(pmd);
}


static inline void pgd_free(pgd_t *pgd)
{
	pmd_free((pmd_t *)pgd);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t *)get_pointer_table();
}


static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_set(pmd, pte);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *page)
{
	pmd_set(pmd, page_address(page));
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmd)
{
	pgd_set(pgd, pmd);
}


#define check_pgt_cache()	do { } while (0)

#endif /* _MOTOROLA_PGALLOC_H */
