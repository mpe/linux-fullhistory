/*
 *  include/asm-s390/bugs.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/pgalloc.h"
 *    Copyright (C) 1994  Linus Torvalds
 */

#ifndef _S390_PGALLOC_H
#define _S390_PGALLOC_H

#include <linux/config.h>
#include <asm/processor.h>
#include <linux/threads.h>

#define check_pgt_cache()	do {} while (0)

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;
	int i;

	pgd = (pgd_t *) __get_free_pages(GFP_KERNEL,1);
        if (pgd != NULL)
		for (i = 0; i < USER_PTRS_PER_PGD; i++)
			pmd_clear(pmd_offset(pgd + i, i*PGDIR_SIZE));
	return pgd;
}

static inline void pgd_free(pgd_t *pgd)
{
        free_pages((unsigned long) pgd, 1);
}

/*
 * page middle directory allocation/free routines.
 * We don't use pmd cache, so these are dummy routines. This
 * code never triggers because the pgd will always be present.
 */
#define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)                     do { } while (0)
#define pmd_free_tlb(tlb,x)		do { } while (0)
#define pgd_populate(mm, pmd, pte)      BUG()

static inline void 
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_val(pmd[0]) = _PAGE_TABLE + __pa(pte);
	pmd_val(pmd[1]) = _PAGE_TABLE + __pa(pte+256);
	pmd_val(pmd[2]) = _PAGE_TABLE + __pa(pte+512);
	pmd_val(pmd[3]) = _PAGE_TABLE + __pa(pte+768);
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *page)
{
	pmd_populate_kernel(mm, pmd, (pte_t *)((page-mem_map) << PAGE_SHIFT));
}

/*
 * page table entry allocation/free routines.
 */
static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long vmaddr)
{
	pte_t *pte;
	int count;
        int i;

	count = 0;
	do {
		pte = (pte_t *) __get_free_page(GFP_KERNEL);
		if (pte != NULL) {
			for (i=0; i < PTRS_PER_PTE; i++)
				pte_clear(pte+i);
		} else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
		}
	} while (!pte && (count++ < 10));
	return pte;
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long vmaddr)
{
	return virt_to_page(pte_alloc_one_kernel(mm, vmaddr));
}

static inline void pte_free_kernel(pte_t *pte)
{
        free_page((unsigned long) pte);
}

static inline void pte_free(struct page *pte)
{
        __free_page(pte);
}

#define pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * This establishes kernel virtual mappings (e.g., as a result of a
 * vmalloc call).  Since s390-esame uses a separate kernel page table,
 * there is nothing to do here... :)
 */
#define set_pgdir(addr,entry) do { } while(0)

static inline pte_t ptep_invalidate(struct vm_area_struct *vma, 
                                    unsigned long address, pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!(pte_val(pte) & _PAGE_INVALID)) {
		/* S390 has 1mb segments, we are emulating 4MB segments */
		pte_t *pto = (pte_t *) (((unsigned long) ptep) & 0x7ffffc00);
		__asm__ __volatile__ ("ipte %0,%1" : : "a" (pto), "a" (address));
	}
	pte_clear(ptep);
	return pte;
}

static inline void ptep_establish(struct vm_area_struct *vma, 
                                  unsigned long address, pte_t *ptep, pte_t entry)
{
	ptep_invalidate(vma, address, ptep);
	set_pte(ptep, entry);
}

#endif /* _S390_PGALLOC_H */
