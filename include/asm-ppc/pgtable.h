#ifndef _PPC_PGTABLE_H
#define _PPC_PGTABLE_H

#include <linux/config.h>
#include <asm/page.h>
#include <asm/mmu.h>

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void flush_tlb_range(struct mm_struct *mm, unsigned long start,
			    unsigned long end);
extern void flush_tlb(void);

/* Caches aren't brain-dead on the ppc. */
#define flush_cache_all()			
#define flush_cache_mm(mm)			
#define flush_cache_range(mm, start, end)	
#define flush_cache_page(vma, vmaddr)		
/*
 * For the page specified, write modified lines in the data cache
 * out to memory, and invalidate lines in the instruction cache.
 */
extern void flush_page_to_ram(unsigned long);

extern unsigned long va_to_phys(unsigned long address);

/* PMD_SHIFT determines the size of the area mapped by the second-level page tables */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: our page-table tree is two-level, so
 * we don't really have any PMD directory.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	(0x2000000) /* 32M */
#define VMALLOC_START ((((long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1)))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

/*
 * Bits in a linux-style PTE.  These match the bits in the
 * (hardware-defined) PowerPC PTE as closely as possible.
 */
#define _PAGE_PRESENT	0x001	/* software: pte contains a translation */
#define _PAGE_USER	0x002	/* matches one of the PP bits */
#define _PAGE_RW	0x004	/* software: user write access allowed */
#define _PAGE_GUARDED	0x008
#define _PAGE_COHERENT	0x010	/* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x020	/* I: cache inhibit */
#define _PAGE_WRITETHRU	0x040	/* W: cache write-through */
#define _PAGE_DIRTY	0x080	/* C: page changed */
#define _PAGE_ACCESSED	0x100	/* R: page referenced */
#define _PAGE_HWWRITE	0x200	/* software: _PAGE_RW & _PAGE_DIRTY */

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | \
				 _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | \
				 _PAGE_HWWRITE | _PAGE_ACCESSED)
#define PAGE_KERNEL_CI	__pgprot(_PAGE_PRESENT | _PAGE_NO_CACHE | _PAGE_RW | \
				 _PAGE_HWWRITE | _PAGE_DIRTY | _PAGE_ACCESSED)

/*
 * The PowerPC can only do execute protection on a segment (256MB) basis,
 * not on a page basis.  So we consider execute permission the same as read.
 * Also, write permissions imply read permissions.
 * This is the closest we can get..
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

extern unsigned long empty_zero_page[1024];

#define BAD_PAGETABLE	__bad_pagetable()
#define BAD_PAGE	__bad_page()
#define ZERO_PAGE	((unsigned long) empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR	(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK	(~(sizeof(void*)-1))

/* sizeof(void*) == 1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2	2

/* to set the page-dir */
/* tsk is a task_struct and pgdir is a pte_t */
#define SET_PAGE_DIR(tsk,pgdir) ({ \
	((tsk)->tss.pg_tables = (unsigned long *)(pgdir)); \
})

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

extern inline int pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
extern inline int pmd_bad(pmd_t pmd)		{ return (pmd_val(pmd) & ~PAGE_MASK) != 0; }
extern inline int pmd_present(pmd_t pmd)	{ return (pmd_val(pmd) & PAGE_MASK) != 0; }
extern inline void pmd_clear(pmd_t * pmdp)	{ pmd_val(*pmdp) = 0; }

     
/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)		{ return 0; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return 1; }
extern inline void pgd_clear(pgd_t * pgdp)	{ }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

extern inline int pte_uncache(pte_t pte)        { return pte_val(pte) |= _PAGE_NO_CACHE; }
extern inline int pte_cache(pte_t pte)          { return pte_val(pte) &= ~_PAGE_NO_CACHE; }

extern inline pte_t pte_rdprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_wrprotect(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_RW | _PAGE_HWWRITE); return pte; }
extern inline pte_t pte_mkclean(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_DIRTY | _PAGE_HWWRITE); return pte; }
extern inline pte_t pte_mkold(pte_t pte) {
	pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }

extern inline pte_t pte_mkread(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_RW;
	if (pte_val(pte) & _PAGE_DIRTY)
		pte_val(pte) |= _PAGE_HWWRITE;
	return pte;
}
extern inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	if (pte_val(pte) & _PAGE_RW)
		pte_val(pte) |= _PAGE_HWWRITE;
	return pte;
}
extern inline pte_t pte_mkyoung(pte_t pte) {
	pte_val(pte) |= _PAGE_ACCESSED; return pte; }

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#if 1
#define set_pte(pteptr, pteval)	((*(pteptr)) = (pteval))
#else
extern inline void set_pte(pte_t *pteptr, pte_t pteval)
{
	unsigned long val = pte_val(pteval);
	extern void xmon(void *);

	if ((val & _PAGE_PRESENT) && ((val < 0x111000 || (val & 0x800)
	    || ((val & _PAGE_HWWRITE) && (~val & (_PAGE_RW|_PAGE_DIRTY)))) {
		printk("bad pte val %lx ptr=%p\n", val, pteptr);
		xmon(0);
	}
	*pteptr = pteval;
}
#endif

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

static inline pte_t mk_pte_phys(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = (page) | pgprot_val(pgprot); return pte; }

extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = __pa(page) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline unsigned long pte_page(pte_t pte)
{ return (pte_val(pte) & PAGE_MASK) + KERNELBASE; }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return pmd_val(pmd); }


/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
extern inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> PGDIR_SHIFT);
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
 * used to allocate a kernel page table, but are actually identical
 * to the xxx() versions.
 */
extern inline void pte_free_kernel(pte_t * pte)
{
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_val(*pmd) = (unsigned long) page;
				return page + address;
			}
			pmd_val(*pmd) = (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = (unsigned long) BAD_PAGETABLE;
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
				pmd_val(*pmd) = (unsigned long) page;
				return page + address;
			}
			pmd_val(*pmd) = (unsigned long) BAD_PAGETABLE;
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_val(*pmd) = (unsigned long) BAD_PAGETABLE;
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

extern inline void pgd_free(pgd_t * pgd)
{
	free_page((unsigned long) pgd);
}

extern inline pgd_t * pgd_alloc(void)
{
	return (pgd_t *) get_free_page(GFP_KERNEL);
}

extern pgd_t swapper_pg_dir[1024];

/*
 * Page tables may have changed.  We don't need to do anything here
 * as entries are faulted into the hash table by the low-level
 * data/instruction access exception handlers.
 */
#define update_mmu_cache(vma,address,pte) while(0){}


#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))



#endif /* _PPC_PAGE_H */
