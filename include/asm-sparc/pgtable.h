#ifndef _SPARC_PGTABLE_H
#define _SPARC_PGTABLE_H

/*  asm-sparc/pgtable.h:  Defines and functions used to work
 *                        with Sparc page tables.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT       18
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT       18
#define PGDIR_SIZE        (1UL << PGDIR_SHIFT)
#define PGDIR_MASK        (~(PGDIR_SIZE-1))
#define PGDIR_ALIGN(addr) (((addr)+PGDIR_SIZE-1)&PGDIR_MASK)

/*
 * Just following the i386 lead, because it works on the Sparc sun4c
 * machines.  Two-level, therefore there is no real PMD.
 */

#define PTRS_PER_PTE    1024
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    1024

/* the no. of pointers that fit on a page: this will go away */
#define PTRS_PER_PAGE   (PAGE_SIZE/sizeof(void*))

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET  (8*1024*1024)
#define VMALLOC_START ((high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) (TASK_SIZE + (unsigned long)(x))

/*
 * Sparc page table fields.
 */

#define _PAGE_VALID     0x80000000   /* valid page */
#define _PAGE_WRITE     0x40000000   /* can be written to */
#define _PAGE_PRIV      0x20000000   /* bit to signify privileged page */
#define _PAGE_NOCACHE   0x10000000   /* non-cacheable page */
#define _PAGE_REF       0x02000000   /* Page has been accessed/referenced */
#define _PAGE_DIRTY     0x01000000   /* Page has been modified, is dirty */
#define _PAGE_COW       0x00800000   /* COW page, hardware ignores this bit (untested) */


/* Sparc sun4c mmu has only a writable bit. Thus if a page is valid it can be
 * read in a load, and executed as code automatically. Although, the memory fault
 * hardware does make a distinction between date-read faults and insn-read faults
 * which is determined by which trap happened plus magic sync/async fault register
 * values which must be checked in the actual fault handler.
 */

/* We want the swapper not to swap out page tables, thus dirty and writable
 * so that the kernel can change the entries as needed. Also valid for
 * obvious reasons.
 */
#define _PAGE_TABLE     (_PAGE_VALID | _PAGE_WRITE | _PAGE_DIRTY)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_REF | _PAGE_DIRTY)

#define PAGE_NONE       __pgprot(_PAGE_VALID | _PAGE_REF)
#define PAGE_SHARED     __pgprot(_PAGE_VALID | _PAGE_WRITE | _PAGE_REF)
#define PAGE_COPY       __pgprot(_PAGE_VALID | _PAGE_REF | _PAGE_COW)
#define PAGE_READONLY   __pgprot(_PAGE_VALID | _PAGE_REF)
#define PAGE_KERNEL     __pgprot(_PAGE_VALID | _PAGE_WRITE | _PAGE_NOCACHE | _PAGE_REF | _PAGE_PRIV)
#define PAGE_INVALID    __pgprot(_PAGE_PRIV)

#define _PAGE_NORMAL(x) __pgprot(_PAGE_VALID | _PAGE_REF | (x))

/* I define these like the i386 does because the check for text or data fault
 * is done at trap time by the low level handler. Maybe I can set these bits
 * then once determined. I leave them like this for now though.
 */
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY
#define __P101  PAGE_READONLY
#define __P110  PAGE_COPY
#define __P111  PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED


extern unsigned long pg0[1024];

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

extern unsigned long __zero_page(void);


#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR      (8*sizeof(unsigned long))   /* better check this stuff */

/* to align the pointer to a pointer address */
#define PTR_MASK          (~(sizeof(void*)-1))


#define SIZEOF_PTR_LOG2   2


/* to set the page-dir
 *
 * On the Sparc the page segments hold 64 pte's which means 256k/segment.
 * Therefore there is no global idea of 'the' page directory, although we
 * make a virtual one in kernel memory so that we can keep the stats on
 * all the pages since not all can be loaded at once in the mmu.
 */

#define SET_PAGE_DIR(tsk,pgdir)

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

extern unsigned long high_memory;

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_VALID; }
extern inline int pte_inuse(pte_t *ptep)        { return mem_map[MAP_NR(ptep)] > 1; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }
extern inline void pte_reuse(pte_t *ptep)
{
  if(!(mem_map[MAP_NR(ptep)] & MAP_PAGE_RESERVED))
    mem_map[MAP_NR(ptep)]++;
}

extern inline int pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
extern inline int pmd_bad(pmd_t pmd)		{ return (pmd_val(pmd) & ~PAGE_MASK) != _PAGE_TABLE || pmd_val(pmd) > high_memory; }
extern inline int pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & _PAGE_VALID; }
extern inline int pmd_inuse(pmd_t *pmdp)        { return 0; }
extern inline void pmd_clear(pmd_t *pmdp)	{ pmd_val(*pmdp) = 0; }
extern inline void pmd_reuse(pmd_t * pmdp)      { }

extern inline int pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & ~PAGE_MASK) != _PAGE_TABLE || pgd_val(pgd) > high_memory; }
extern inline int pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & _PAGE_VALID; }
extern inline int pgd_inuse(pgd_t *pgdp)        { return mem_map[MAP_NR(pgdp)] > 1; }
extern inline void pgd_clear(pgd_t * pgdp)	{ pgd_val(*pgdp) = 0; }
extern inline void pgd_reuse(pgd_t *pgdp)
{
  if (!(mem_map[MAP_NR(pgdp)] & MAP_PAGE_RESERVED))
    mem_map[MAP_NR(pgdp)]++;
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_VALID; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_WRITE; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_VALID; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_REF; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_REF; }
extern inline int pte_cow(pte_t pte)		{ return pte_val(pte) & _PAGE_COW; }

extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_WRITE; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_VALID; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_VALID; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_REF; return pte; }
extern inline pte_t pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_PAGE_COW; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_WRITE; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_VALID; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) |= _PAGE_VALID; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_REF; return pte; }
extern inline pte_t pte_mkcow(pte_t pte)	{ pte_val(pte) |= _PAGE_COW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = page | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline unsigned long pte_page(pte_t pte)	{ return pte_val(pte) & PAGE_MASK; }

extern inline unsigned long pmd_page(pmd_t pmd) { return pmd_val(pmd) & PAGE_MASK; }

extern inline unsigned long pgd_page(pgd_t pgd)	{ return pgd_val(pgd) & PAGE_MASK; }

extern inline void pgd_set(pgd_t * pgdp, pte_t * ptep)
{ pgd_val(*pgdp) = _PAGE_TABLE | (unsigned long) ptep; }

/* to find an entry in a page-table-directory */
#define PAGE_DIR_OFFSET(tsk,address) \
((((unsigned long)(address)) >> 22) + (pgd_t *) (tsk)->tss.cr3)

/* to find an entry in a page-table-directory */
extern inline pgd_t * pgd_offset(struct task_struct * tsk, unsigned long address)
{
	return (pgd_t *) tsk->tss.cr3 + (address >> PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
extern inline pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}


/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
extern inline void pte_free_kernel(pte_t * pte)
{
	mem_map[MAP_NR(pte)] = 1;
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) page;
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free_kernel(pmd_t * pmd)
{
}

extern inline pmd_t * pmd_alloc_kernel(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

extern inline void pte_free(pte_t * pte)
{
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) page;
				return page + address;
			}
			pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = _PAGE_TABLE | (unsigned long) BAD_PAGETABLE;
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free(pmd_t * pmd)
{
}

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

extern inline void pgd_free(pgd_t *pgd)
{
  free_page((unsigned long) pgd);
}
extern inline pgd_t *pgd_alloc(void)
{
  return (pgd_t *) get_free_page(GFP_KERNEL);
}

extern pgd_t swapper_pg_dir[1024];

#endif /* !(_SPARC_PGTABLE_H) */
