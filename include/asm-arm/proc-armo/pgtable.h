/*
 * linux/include/asm-arm/proc-armo/pgtable.h
 *
 * Copyright (C) 1995-1999 Russell King
 *
 * 18-Oct-1997	RMK	Now two-level (32x32)
 */
#ifndef __ASM_PROC_PGTABLE_H
#define __ASM_PROC_PGTABLE_H

/*
 * PMD_SHIFT determines the size of the area a second-level page table can map
 */
#define PMD_SHIFT       20
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/*
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
#define PGDIR_SHIFT     20
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the arm3 is one-level, so
 * we don't really have any PMD or PTE directory physically.
 */
#define PTRS_PER_PTE    32
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    32
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

#define VMALLOC_START	0x01a00000
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	0x01c00000

#define _PAGE_PRESENT	0x01
#define _PAGE_READONLY	0x02
#define _PAGE_NOT_USER	0x04
#define _PAGE_OLD	0x08
#define _PAGE_CLEAN	0x10

#define _PAGE_TABLE     (_PAGE_PRESENT)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_OLD | _PAGE_CLEAN)

/*                               -- present --   -- !dirty --  --- !write ---   ---- !user --- */
#define PAGE_NONE       __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY | _PAGE_NOT_USER)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_CLEAN                                  )
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY                 )
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY                 )
#define PAGE_KERNEL     __pgprot(_PAGE_PRESENT                                | _PAGE_NOT_USER)

/*
 * The arm can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
 */
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY
#define __P101  PAGE_READONLY
#define __P110  PAGE_COPY
#define __P111  PAGE_COPY

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_SHARED
#define __S011  PAGE_SHARED
#define __S100  PAGE_READONLY
#define __S101  PAGE_READONLY
#define __S110  PAGE_SHARED
#define __S111  PAGE_SHARED

extern unsigned long physical_start;
extern unsigned long physical_end;

#define pte_none(pte)		(!pte_val(pte))
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)
#define pte_clear(ptep)		set_pte((ptep), __pte(0))

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_bad(pmd)		((pmd_val(pmd) & 0xfc000002))
#define pmd_present(pmd)	(pmd_val(pmd) & _PAGE_PRESENT)
#define pmd_clear(pmdp)		set_pmd(pmdp, __pmd(0))

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_present(pgd)	(1)
#define pgd_clear(pgdp)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)           { return !(pte_val(pte) & _PAGE_NOT_USER);     }
extern inline int pte_write(pte_t pte)          { return !(pte_val(pte) & _PAGE_READONLY);     }
extern inline int pte_exec(pte_t pte)           { return !(pte_val(pte) & _PAGE_NOT_USER);     }
extern inline int pte_dirty(pte_t pte)          { return !(pte_val(pte) & _PAGE_CLEAN);        }
extern inline int pte_young(pte_t pte)          { return !(pte_val(pte) & _PAGE_OLD);          }

extern inline pte_t pte_nocache(pte_t pte)	{ return pte; }
extern inline pte_t pte_wrprotect(pte_t pte)    { pte_val(pte) |= _PAGE_READONLY;  return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)    { pte_val(pte) |= _PAGE_NOT_USER;  return pte; }
extern inline pte_t pte_exprotect(pte_t pte)    { pte_val(pte) |= _PAGE_NOT_USER;  return pte; }
extern inline pte_t pte_mkclean(pte_t pte)      { pte_val(pte) |= _PAGE_CLEAN;     return pte; }
extern inline pte_t pte_mkold(pte_t pte)        { pte_val(pte) |= _PAGE_OLD;       return pte; }

extern inline pte_t pte_mkwrite(pte_t pte)      { pte_val(pte) &= ~_PAGE_READONLY; return pte; }
extern inline pte_t pte_mkread(pte_t pte)       { pte_val(pte) &= ~_PAGE_NOT_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)       { pte_val(pte) &= ~_PAGE_NOT_USER; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)      { pte_val(pte) &= ~_PAGE_CLEAN;    return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)      { pte_val(pte) &= ~_PAGE_OLD;      return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern __inline__ pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{
	pte_t pte;
	pte_val(pte) = __virt_to_phys(page) | pgprot_val(pgprot);
	return pte;
}

/* This takes a physical page address that is used by the remapping functions */
extern __inline__ pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
	pte_t pte;
	pte_val(pte) = physpage + pgprot_val(pgprot);
	return pte;
}

extern __inline__ pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

extern __inline__ unsigned long pte_page(pte_t pte)
{
	return __phys_to_virt(pte_val(pte) & PAGE_MASK);
}

extern __inline__ pmd_t mk_pmd(pte_t *ptep)
{
	pmd_t pmd;
	pmd_val(pmd) = __virt_to_phys((unsigned long)ptep) | _PAGE_TABLE;
	return pmd;
}

/* these are aliases for the above function */
#define mk_user_pmd(ptep)   mk_pmd(ptep)
#define mk_kernel_pmd(ptep) mk_pmd(ptep)

#define set_pmd(pmdp,pmd) ((*(pmdp)) = (pmd))

extern __inline__ unsigned long pmd_page(pmd_t pmd)
{
	return __phys_to_virt(pmd_val(pmd) & ~_PAGE_TABLE);
}

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
extern __inline__ pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
        return mm->pgd + (address >> PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, address) ((pmd_t *)(dir))

/* Find an entry in the third-level page table.. */
extern __inline__ pte_t * pte_offset(pmd_t *dir, unsigned long address)
{
	return (pte_t *)pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

extern void free_table(void *table);

/* keep this as an inline so we get type checking */
extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
	free_table(pgd);
}

/* keep this as an inline so we get type checking */
extern __inline__ void free_pte_slow(pte_t *pte)
{
	free_table(pte);
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
}

#define pgd_free(pgd)           free_pgd_fast(pgd)

extern __inline__ pgd_t *pgd_alloc(void)
{
	extern pgd_t *get_pgd_slow(void);
	pgd_t *pgd;

	pgd = get_pgd_fast();
	if (!pgd)
		pgd = get_pgd_slow();

	return pgd;
}

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)

extern __inline__ pte_t *pte_alloc(pmd_t * pmd, unsigned long address)
{
	extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);

	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none (*pmd)) {
		pte_t *page = (pte_t *) get_pte_fast();

		if (!page)
			return get_pte_slow(pmd, address);
		set_pmd(pmd, mk_pmd(page));
		return page + address;
	}
	if (pmd_bad (*pmd)) {
		__bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern __inline__ void pmd_free(pmd_t *pmd)
{
}

extern __inline__ pmd_t *pmd_alloc(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

#define pmd_free_kernel         pmd_free
#define pmd_alloc_kernel        pmd_alloc
#define pte_alloc_kernel        pte_alloc

#endif /* __ASM_PROC_PGTABLE_H */
