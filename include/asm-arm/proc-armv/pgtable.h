/*
 * linux/include/asm-arm/proc-armv/pgtable.h
 *
 * Copyright (C) 1995, 1996, 1997 Russell King
 *
 * 12-01-1997	RMK	Altered flushing routines to use function pointers
 *			now possible to combine ARM6, ARM7 and StrongARM versions.
 */
#ifndef __ASM_PROC_PGTABLE_H
#define __ASM_PROC_PGTABLE_H

#include <asm/arch/mmu.h>
#include <asm/arch/processor.h>		/* For TASK_SIZE */

#define LIBRARY_TEXT_START 0x0c000000

/*
 * Cache flushing...
 */
#define flush_cache_all()						\
	processor.u.armv3v4._flush_cache_all()

#define flush_cache_mm(_mm)						\
	do {								\
		if ((_mm) == current->mm)				\
			processor.u.armv3v4._flush_cache_all();		\
	} while (0)

#define flush_cache_range(_mm,_start,_end)				\
	do {								\
		if ((_mm) == current->mm)				\
			processor.u.armv3v4._flush_cache_area		\
				((_start), (_end), 1);			\
	} while (0)

#define flush_cache_page(_vma,_vmaddr)					\
	do {								\
		if ((_vma)->vm_mm == current->mm)			\
			processor.u.armv3v4._flush_cache_area		\
				((_vmaddr), (_vmaddr) + PAGE_SIZE,	\
				 ((_vma)->vm_flags & VM_EXEC) ? 1 : 0);	\
	} while (0)

#define flush_icache_range(_start,_end)					\
	processor.u.armv3v4._flush_icache_area((_start), (_end))

/*
 * We don't have a MEMC chip...
 */
#define update_memc_all()		do { } while (0)
#define update_memc_task(tsk)		do { } while (0)
#define update_memc_mm(mm)		do { } while (0)
#define update_memc_addr(mm,addr,pte)	do { } while (0)

/*
 * This flushes back any buffered write data.  We have to clean and flush the entries
 * in the cache for this page.  Is it necessary to invalidate the I-cache?
 */
#define flush_page_to_ram(_page)					\
	processor.u.armv3v4._flush_ram_page ((_page) & PAGE_MASK);

/*
 * Make the page uncacheable (must flush page beforehand).
 */
#define uncache_page(_page)						\
	processor.u.armv3v4._flush_ram_page ((_page) & PAGE_MASK);

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * GCC uses conditional instructions, and expects the assembler code to do so as well.
 *
 * We drain the write buffer in here to ensure that the page tables in ram
 * are really up to date.  It is more efficient to do this here...
 */
#define flush_tlb() flush_tlb_all()

#define flush_tlb_all()								\
	processor.u.armv3v4._flush_tlb_all()

#define flush_tlb_mm(_mm)							\
	do {									\
		if ((_mm) == current->mm)					\
			processor.u.armv3v4._flush_tlb_all();			\
	} while (0)

#define flush_tlb_range(_mm,_start,_end)					\
	do {									\
		if ((_mm) == current->mm)					\
			processor.u.armv3v4._flush_tlb_area			\
				((_start), (_end), 1);				\
	} while (0)

#define flush_tlb_page(_vma,_vmaddr)						\
	do {									\
		if ((_vma)->vm_mm == current->mm)				\
			processor.u.armv3v4._flush_tlb_area			\
				((_vmaddr), (_vmaddr) + PAGE_SIZE,		\
				 ((_vma)->vm_flags & VM_EXEC) ? 1 : 0);		\
	} while (0)

/*
 * Since the page tables are in cached memory, we need to flush the dirty
 * data cached entries back before we flush the tlb...  This is also useful
 * to flush out the SWI instruction for signal handlers...
 */
#define __flush_entry_to_ram(entry)						\
	processor.u.armv3v4._flush_cache_entry((unsigned long)(entry))

#define __flush_pte_to_ram(entry)						\
	processor.u.armv3v4._flush_cache_pte((unsigned long)(entry))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT       20
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT     20
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the sa110 is two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE    256
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    4096
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	  (8*1024*1024)
#define VMALLOC_START	  (((unsigned long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END       (PAGE_OFFSET + 0x10000000)

/* PMD types (actually level 1 descriptor) */
#define PMD_TYPE_MASK		0x0003
#define PMD_TYPE_FAULT		0x0000
#define PMD_TYPE_TABLE		0x0001
#define PMD_TYPE_SECT		0x0002
#define PMD_UPDATABLE		0x0010
#define PMD_SECT_CACHEABLE	0x0008
#define PMD_SECT_BUFFERABLE	0x0004
#define PMD_SECT_AP_WRITE	0x0400
#define PMD_SECT_AP_READ	0x0800
#define PMD_DOMAIN(x)		((x) << 5)

/* PTE types (actially level 2 descriptor) */
#define PTE_TYPE_MASK	0x0003
#define PTE_TYPE_FAULT	0x0000
#define PTE_TYPE_LARGE	0x0001
#define PTE_TYPE_SMALL	0x0002
#define PTE_AP_READ	0x0aa0
#define PTE_AP_WRITE	0x0550
#define PTE_CACHEABLE	0x0008
#define PTE_BUFFERABLE	0x0004

/* Domains */
#define DOMAIN_USER	0
#define DOMAIN_KERNEL	1
#define DOMAIN_TABLE	1
#define DOMAIN_IO	2

#define _PAGE_CHG_MASK  (0xfffff00c | PTE_TYPE_MASK)

/*
 * We define the bits in the page tables as follows:
 *  PTE_BUFFERABLE	page is dirty
 *  PTE_AP_WRITE	page is writable
 *  PTE_AP_READ		page is a young (unsetting this causes faults for any access)
 *  PTE_CACHEABLE       page is readable
 *
 * A page will not be made writable without the dirty bit set.
 * It is not legal to have a writable non-dirty page though (it breaks).
 *
 * A readable page is marked as being cacheable.
 * Youngness is indicated by hardware read.  If the page is old,
 * then we will fault and make the page young again.
 */
#define _PTE_YOUNG	PTE_AP_READ
#define _PTE_DIRTY	PTE_BUFFERABLE
#define _PTE_READ	PTE_CACHEABLE
#define _PTE_WRITE	PTE_AP_WRITE

#define PAGE_NONE       __pgprot(PTE_TYPE_SMALL | _PTE_YOUNG)
#define PAGE_SHARED     __pgprot(PTE_TYPE_SMALL | _PTE_YOUNG | _PTE_READ | _PTE_WRITE)
#define PAGE_COPY       __pgprot(PTE_TYPE_SMALL | _PTE_YOUNG | _PTE_READ)
#define PAGE_READONLY   __pgprot(PTE_TYPE_SMALL | _PTE_YOUNG | _PTE_READ)
#define PAGE_KERNEL     __pgprot(PTE_TYPE_SMALL | _PTE_READ  | _PTE_DIRTY | _PTE_WRITE)

#define _PAGE_USER_TABLE	(PMD_TYPE_TABLE | PMD_DOMAIN(DOMAIN_USER))
#define _PAGE_KERNEL_TABLE	(PMD_TYPE_TABLE | PMD_DOMAIN(DOMAIN_KERNEL))

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
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);
extern unsigned long *empty_zero_page;

#define BAD_PAGETABLE __bad_pagetable()
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
#define SET_PAGE_DIR(tsk,pgdir)					\
do {								\
	tsk->tss.memmap = __virt_to_phys((unsigned long)pgdir);	\
	if ((tsk) == current)					\
		__asm__ __volatile__(				\
		"mcr%?	p15, 0, %0, c2, c0, 0\n"		\
		: : "r" (tsk->tss.memmap));			\
} while (0)

extern __inline__ int pte_none(pte_t pte)
{
	return !pte_val(pte);
}

#define pte_clear(ptep)	set_pte(ptep, __pte(0))

extern __inline__ int pte_present(pte_t pte)
{
#if 0
	/* This is what it really does, the else
	   part is just to make it easier for the compiler */
	switch (pte_val(pte) & PTE_TYPE_MASK) {
	case PTE_TYPE_LARGE:
	case PTE_TYPE_SMALL:
		return 1;
	default:
		return 0;
	}
#else
	return ((pte_val(pte) + 1) & 2);
#endif
}

extern __inline__ int pmd_none(pmd_t pmd)
{
	return !pmd_val(pmd);
}

#define pmd_clear(pmdp) set_pmd(pmdp, __pmd(0))

extern __inline__ int pmd_bad(pmd_t pmd)
{
#if 0
	/* This is what it really does, the else
	   part is just to make it easier for the compiler */
	switch (pmd_val(pmd) & PMD_TYPE_MASK) {
	case PMD_TYPE_FAULT:
	case PMD_TYPE_TABLE:
		return 0;
	default:
		return 1;
	}
#else
	return pmd_val(pmd) & 2;
#endif
}

extern __inline__ int pmd_present(pmd_t pmd)
{
#if 0
	/* This is what it really does, the else
	   part is just to make it easier for the compiler */
	switch (pmd_val(pmd) & PMD_TYPE_MASK) {
	case PMD_TYPE_TABLE:
		return 1;
	default:
		return 0;
	}
#else
	return ((pmd_val(pmd) + 1) & 2);
#endif
}

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
#define pte_read(pte)		(1)
#define pte_exec(pte)		(1)

extern __inline__ int pte_write(pte_t pte)
{
	return pte_val(pte) & _PTE_WRITE;
}

extern __inline__ int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PTE_DIRTY;
}

extern __inline__ int pte_young(pte_t pte)
{
	return pte_val(pte) & _PTE_YOUNG;
}

extern __inline__ pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~_PTE_WRITE;
	return pte;
}

extern __inline__ pte_t pte_nocache(pte_t pte)
{
	pte_val(pte) &= ~PTE_CACHEABLE;
	return pte;
}

extern __inline__ pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PTE_DIRTY;
	return pte;
}

extern __inline__ pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PTE_YOUNG;
	return pte;
}

extern __inline__ pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PTE_WRITE;
	return pte;
}

extern __inline__ pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PTE_DIRTY;
	return pte;
}

extern __inline__ pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PTE_YOUNG;
	return pte;
}

/*
 * The following are unable to be implemented on this MMU
 */
#if 0
extern __inline__ pte_t pte_rdprotect(pte_t pte)
{
	pte_val(pte) &= ~(PTE_CACHEABLE|PTE_AP_READ);
	return pte;
}

extern __inline__ pte_t pte_exprotect(pte_t pte)
{
	pte_val(pte) &= ~(PTE_CACHEABLE|PTE_AP_READ);
	return pte;
}

extern __inline__ pte_t pte_mkread(pte_t pte)
{
	pte_val(pte) |= PTE_CACHEABLE;
	return pte;
}

extern __inline__ pte_t pte_mkexec(pte_t pte)
{
	pte_val(pte) |= PTE_CACHEABLE;
	return pte;
}
#endif

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

extern __inline__ void set_pte(pte_t *pteptr, pte_t pteval)
{
	*pteptr = pteval;
	__flush_pte_to_ram(pteptr);
}

extern __inline__ unsigned long pte_page(pte_t pte)
{
	return __phys_to_virt(pte_val(pte) & PAGE_MASK);
}

extern __inline__ pmd_t mk_user_pmd(pte_t *ptep)
{
	pmd_t pmd;
	pmd_val(pmd) = __virt_to_phys((unsigned long)ptep) | _PAGE_USER_TABLE;
	return pmd;
}

extern __inline__ pmd_t mk_kernel_pmd(pte_t *ptep)
{
	pmd_t pmd;
	pmd_val(pmd) = __virt_to_phys((unsigned long)ptep) | _PAGE_KERNEL_TABLE;
	return pmd;
}

#if 1
#define set_pmd(pmdp,pmd) processor.u.armv3v4._set_pmd(pmdp,pmd)
#else
extern __inline__ void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
	__flush_pte_to_ram(pmdp);
}
#endif

extern __inline__ unsigned long pmd_page(pmd_t pmd)
{
	return __phys_to_virt(pmd_val(pmd) & 0xfffffc00);
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
extern __inline__ pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

extern unsigned long get_small_page(int priority);
extern void free_small_page(unsigned long page);

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

#ifndef __SMP__
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist (quicklists.pgd_cache)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (quicklists.pte_cache)
#define pgtable_cache_size (quicklists.pgtable_cache_sz)
#else
#error Pgtable caches have to be per-CPU, so that no locking is needed.
#endif

extern pgd_t *get_pgd_slow(void);

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	} else
		ret = (unsigned long *)get_pgd_slow();
	return (pgd_t *)ret;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
	free_pages((unsigned long) pgd, 2);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

extern __inline__ void free_pte_slow(pte_t *pte)
{
	free_small_page((unsigned long)pte);
}

/* We don't use pmd cache, so this is a dummy routine */
extern __inline__ pmd_t *get_pmd_fast(void)
{
	return (pmd_t *)0;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
}

extern void __bad_pmd(pmd_t *pmd);
extern void __bad_pmd_kernel(pmd_t *pmd);

#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)
#define pgd_free(pgd)		free_pgd_fast(pgd)
#define pgd_alloc()		get_pgd_fast()

extern __inline__ pte_t * pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_pte_fast();

		if (!page)
			return get_pte_kernel_slow(pmd, address);
		set_pmd(pmd, mk_kernel_pmd(page));
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern __inline__ pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_pte_fast();

		if (!page)
			return get_pte_slow(pmd, address);
		set_pmd(pmd, mk_user_pmd(page));
		return page + address;
	}
	if (pmd_bad(*pmd)) {
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

#define pmd_free_kernel		pmd_free
#define pmd_alloc_kernel	pmd_alloc

extern __inline__ void set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;
	pgd_t *pgd;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm,address) = entry;
	}
	read_unlock(&tasklist_lock);
	for (pgd = (pgd_t *)pgd_quicklist; pgd; pgd = (pgd_t *)*(unsigned long *)pgd)
		pgd[address >> PGDIR_SHIFT] = entry;
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * The sa110 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
extern __inline__ void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
}

#define SWP_TYPE(entry) (((entry) >> 2) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 9)
#define SWP_ENTRY(type,offset) (((type) << 2) | ((offset) << 9))

#endif /* __ASM_PROC_PAGE_H */
