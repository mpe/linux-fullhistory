/*
 * linux/include/asm-arm/proc-armo/pgtable.h
 *
 * Copyright (C) 1995, 1996 Russell King
 */
#ifndef __ASM_PROC_PGTABLE_H
#define __ASM_PROC_PGTABLE_H

#include <asm/arch/mmu.h>

#define LIBRARY_TEXT_START 0x0c000000

/*
 * Cache flushing...
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(mm,start,end)		do { } while (0)
#define flush_cache_page(vma,vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 */

#define flush_tlb() flush_tlb_mm(current->mm)

extern __inline__ void flush_tlb_all(void)
{
	struct task_struct *p;

	p = &init_task;
	do {
		processor.u.armv2._update_map(p);
		p = p->next_task;
	} while (p != &init_task);

	processor.u.armv2._remap_memc (current);
}

extern __inline__ void flush_tlb_mm(struct mm_struct *mm)
{
	struct task_struct *p;

	p = &init_task;
	do {
		if (p->mm == mm)
			processor.u.armv2._update_map(p);
		p = p->next_task;
	} while (p != &init_task);

	if (current->mm == mm)
		processor.u.armv2._remap_memc (current);
}

#define flush_tlb_range(mm, start, end) flush_tlb_mm(mm)
#define flush_tlb_page(vma, vmaddr) flush_tlb_mm(vma->vm_mm)

#define __flush_entry_to_ram(entry)

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT       PAGE_SHIFT
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT     PAGE_SHIFT
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the arm3 is one-level, so
 * we don't really have any PMD or PTE directory physically.
 */
#define PTRS_PER_PTE    1
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    1024

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_START	0x01a00000
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

#define _PAGE_PRESENT   0x001
#define _PAGE_RW        0x002
#define _PAGE_USER      0x004
#define _PAGE_PCD       0x010
#define _PAGE_ACCESSED  0x020
#define _PAGE_DIRTY     0x040

#define _PAGE_TABLE     (_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE       __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL     __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)

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

#undef TEST_VERIFY_AREA

/*
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern unsigned long *empty_zero_page;

#define BAD_PAGE __bad_page()
#define ZERO_PAGE ((unsigned long) empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BYTES_PER_PTR			(sizeof(unsigned long))
#define BITS_PER_PTR                    (8*BYTES_PER_PTR)

/* to align the pointer to a pointer address */
#define PTR_MASK                        (~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
#define SIZEOF_PTR_LOG2                 2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir)						\
do {									\
	tsk->tss.memmap = (unsigned long)pgdir;				\
	processor.u.armv2._update_map(tsk);				\
	if ((tsk) == current)						\
		processor.u.armv2._remap_memc (current);		\
} while (0)

extern unsigned long physical_start;
extern unsigned long physical_end;

extern inline int pte_none(pte_t pte)           { return !pte_val(pte); }
extern inline int pte_present(pte_t pte)        { return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_clear(pte_t *ptep)       { pte_val(*ptep) = 0; }

extern inline int pmd_none(pmd_t pmd)           { return 0; }
extern inline int pmd_bad(pmd_t pmd)            { return 0; }
extern inline int pmd_present(pmd_t pmd)        { return 1; }
extern inline void pmd_clear(pmd_t * pmdp)      { }

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)           { return 0; }
extern inline int pgd_bad(pgd_t pgd)            { return 0; }
extern inline int pgd_present(pgd_t pgd)        { return 1; }
extern inline void pgd_clear(pgd_t * pgdp)      { }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)           { return pte_val(pte) & _PAGE_USER; }
extern inline int pte_write(pte_t pte)          { return pte_val(pte) & _PAGE_RW; }
extern inline int pte_exec(pte_t pte)           { return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)          { return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)          { return pte_val(pte) & _PAGE_ACCESSED; }
#define pte_cacheable(pte) 1

extern inline pte_t pte_nocache(pte_t pte)	{ return pte; }
extern inline pte_t pte_wrprotect(pte_t pte)    { pte_val(pte) &= ~_PAGE_RW; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)    { pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)    { pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)      { pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)        { pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)      { pte_val(pte) |= _PAGE_RW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)       { pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)       { pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)      { pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)      { pte_val(pte) |= _PAGE_ACCESSED; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = virt_to_phys(page) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline unsigned long pte_page(pte_t pte)
{ return phys_to_virt(pte_val(pte) & PAGE_MASK); }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return phys_to_virt(pmd_val(pmd) & PAGE_MASK); }

/* to find an entry in a page-table-directory */
extern inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
        return mm->pgd + (address >> PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, address) ((pmd_t *)(dir))

/* Find an entry in the third-level page table.. */
#define pte_offset(dir, address) ((pte_t *)(dir))

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */
extern inline void pte_free_kernel(pte_t * pte)
{
	pte_val(*pte) = 0;
}

extern inline pte_t * pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	return (pte_t *) pmd;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
#define pmd_free_kernel(pmdp)
#define pmd_alloc_kernel(pgd,address) ((pmd_t *)(pgd))

#define pte_free(ptep)
#define pte_alloc(pmd,address) ((pte_t *)(pmd))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
#define pmd_free(pmd)
#define pmd_alloc(pgd,address) ((pmd_t *)(pgd))

extern inline void pgd_free(pgd_t * pgd)
{
	extern void kfree(void *);
	kfree((void *)pgd);
}

extern inline pgd_t * pgd_alloc(void)
{
	pgd_t *pgd;
	extern void *kmalloc(unsigned int, int);
	
	pgd = (pgd_t *) kmalloc(PTRS_PER_PGD * BYTES_PER_PTR, GFP_KERNEL);
	if (pgd)
		memset(pgd, 0, PTRS_PER_PGD * BYTES_PER_PTR);
	return pgd;
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#define update_mmu_cache(vma,address,pte) processor.u.armv2._update_mmu_cache(vma,address,pte)

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) <<  8))

#endif /* __ASM_PROC_PAGE_H */

