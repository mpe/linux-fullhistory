/*
 * linux/include/asm-arm/proc-armv/pgtable.h
 *
 * Copyright (C) 1995, 1996, 1997 Russell King
 *
 * 12-Jan-1997	RMK	Altered flushing routines to use function pointers
 *			now possible to combine ARM6, ARM7 and StrongARM versions.
 * 17-Apr-1999	RMK	Now pass an area size to clean_cache_area and
 *			flush_icache_area.
 */
#ifndef __ASM_PROC_PGTABLE_H
#define __ASM_PROC_PGTABLE_H

#include <asm/arch/memory.h>		/* For TASK_SIZE */

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

#define clean_cache_range(_start,_end)					\
	do {								\
		unsigned long _s, _sz;					\
		_s = (unsigned long)_start;				\
		_sz = (unsigned long)_end - _s;				\
		processor.u.armv3v4._clean_cache_area(_s, _sz);		\
	} while (0)

#define clean_cache_area(_start,_size)					\
	do {								\
		unsigned long _s;					\
		_s = (unsigned long)_start;				\
		processor.u.armv3v4._clean_cache_area(_s, _size);	\
	} while (0)

#define flush_icache_range(_start,_end)					\
	processor.u.armv3v4._flush_icache_area((_start), (_end) - (_start))

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


/*
 * Domains
 */
#define DOMAIN_USER	0
#define DOMAIN_KERNEL	1
#define DOMAIN_TABLE	1
#define DOMAIN_IO	2



#undef TEST_VERIFY_AREA

/*
 * The sa110 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
extern __inline__ void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
}


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

#define BAD_PAGETABLE	__bad_pagetable()
#define BAD_PAGE	__bad_page()
#define ZERO_PAGE	((unsigned long) empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BYTES_PER_PTR	(sizeof(unsigned long))
#define BITS_PER_PTR	(8*BYTES_PER_PTR)

/* to align the pointer to a pointer address */
#define PTR_MASK	(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
#define SIZEOF_PTR_LOG2	2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* to set the page-dir
 * Note that we need to flush the cache and TLBs
 * if we are affecting the current task.
 */
#define SET_PAGE_DIR(tsk,pgdir)					\
do {								\
	tsk->tss.memmap = __virt_to_phys((unsigned long)pgdir);	\
	if ((tsk) == current) {					\
		flush_cache_all();				\
		__asm__ __volatile__(				\
		"mcr%?	p15, 0, %0, c2, c0, 0\n"		\
		: : "r" (tsk->tss.memmap));			\
		flush_tlb_all();				\
	}							\
} while (0)


/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_present(pgd)	(1)
#define pgd_clear(pgdp)

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
extern __inline__ pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> PGDIR_SHIFT);
}

extern unsigned long get_page_2k(int priority);
extern void free_page_2k(unsigned long page);

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
		clean_cache_area(ret, 4);
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

#define pgd_free(pgd)		free_pgd_fast(pgd)
#define pgd_alloc()		get_pgd_fast()

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

/****************
* PMD functions *
****************/

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

#define _PAGE_USER_TABLE	(PMD_TYPE_TABLE | PMD_DOMAIN(DOMAIN_USER))
#define _PAGE_KERNEL_TABLE	(PMD_TYPE_TABLE | PMD_DOMAIN(DOMAIN_KERNEL))

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_clear(pmdp)		set_pmd(pmdp, __pmd(0))
#define pmd_bad(pmd)		(pmd_val(pmd) & 2)
#define mk_user_pmd(ptep)	__mk_pmd(ptep, _PAGE_USER_TABLE)
#define mk_kernel_pmd(ptep)	__mk_pmd(ptep, _PAGE_KERNEL_TABLE)
#define set_pmd(pmdp,pmd)	processor.u.armv3v4._set_pmd(pmdp,pmd)

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, address) ((pmd_t *)(dir))

extern __inline__ int pmd_present(pmd_t pmd)
{
	return ((pmd_val(pmd) + 1) & 2);
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

extern __inline__ pmd_t __mk_pmd(pte_t *ptep, unsigned long prot)
{
	unsigned long pte_ptr = (unsigned long)ptep;
	pmd_t pmd;

	pte_ptr -= PTRS_PER_PTE * BYTES_PER_PTR;

	/*
	 * The pmd must be loaded with the physical
	 * address of the PTE table
	 */
	pmd_val(pmd) = __virt_to_phys(pte_ptr) | prot;

	return pmd;
}

extern __inline__ unsigned long pmd_page(pmd_t pmd)
{
	unsigned long ptr;

	ptr = pmd_val(pmd) & ~(PTRS_PER_PTE * BYTES_PER_PTR - 1);

	ptr += PTRS_PER_PTE * BYTES_PER_PTR;

	return __phys_to_virt(ptr);
}


/****************
* PTE functions *
****************/

/* PTE types (actially level 2 descriptor) */
#define PTE_TYPE_MASK		0x0003
#define PTE_TYPE_FAULT		0x0000
#define PTE_TYPE_LARGE		0x0001
#define PTE_TYPE_SMALL		0x0002
#define PTE_AP_READ		0x0aa0
#define PTE_AP_WRITE		0x0550
#define PTE_CACHEABLE		0x0008
#define PTE_BUFFERABLE		0x0004

#define pte_none(pte)		(!pte_val(pte))
#define pte_clear(ptep)		set_pte(ptep, __pte(0))

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

#define set_pte(ptep, pte)	processor.u.armv3v4._set_pte(ptep,pte)

extern __inline__ unsigned long pte_page(pte_t pte)
{
	return __phys_to_virt(pte_val(pte) & PAGE_MASK);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		clean_cache_area(ret, 4);
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
	free_page_2k((unsigned long)(pte - PTRS_PER_PTE));
}

#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)

/*###############################################################################
 * New PageTableEntry stuff...
 */
/* We now keep two sets of ptes - the physical and the linux version.
 * This gives us many advantages, and allows us greater flexibility.
 *
 * The Linux pte's contain:
 *  bit   meaning
 *   0    page present
 *   1    young
 *   2    bufferable	- matches physical pte
 *   3    cacheable	- matches physical pte
 *   4    user
 *   5    write
 *   6    execute
 *   7    dirty
 *  8-11  unused
 *  12-31 virtual page address
 *
 * These are stored at the pte pointer; the physical PTE is at -1024bytes
 */
#define L_PTE_PRESENT		(1 << 0)
#define L_PTE_YOUNG		(1 << 1)
#define L_PTE_BUFFERABLE	(1 << 2)
#define L_PTE_CACHEABLE		(1 << 3)
#define L_PTE_USER		(1 << 4)
#define L_PTE_WRITE		(1 << 5)
#define L_PTE_EXEC		(1 << 6)
#define L_PTE_DIRTY		(1 << 7)

/*
 * The following macros handle the cache and bufferable bits...
 */
#define _L_PTE_DEFAULT	L_PTE_PRESENT | L_PTE_YOUNG
#define _L_PTE_READ	L_PTE_USER | L_PTE_CACHEABLE
#define _L_PTE_EXEC	_L_PTE_READ | L_PTE_EXEC

#define PAGE_NONE       __pgprot(_L_PTE_DEFAULT)
#define PAGE_COPY       __pgprot(_L_PTE_DEFAULT | _L_PTE_READ  | L_PTE_BUFFERABLE)
#define PAGE_SHARED     __pgprot(_L_PTE_DEFAULT | _L_PTE_READ  | L_PTE_BUFFERABLE | L_PTE_WRITE)
#define PAGE_READONLY   __pgprot(_L_PTE_DEFAULT | _L_PTE_READ)
#define PAGE_KERNEL     __pgprot(_L_PTE_DEFAULT | L_PTE_CACHEABLE | L_PTE_BUFFERABLE | L_PTE_DIRTY | L_PTE_WRITE)

#define _PAGE_CHG_MASK		(PAGE_MASK | L_PTE_DIRTY | L_PTE_YOUNG)

/*
 * The table below defines the page protection levels that we insert into our
 * Linux page table version.  These get translated into the best that the
 * architecture can perform.  Note that on most ARM hardware:
 *  1) We cannot do execute protection
 *  2) If we could do execute protection, then read is implied
 *  3) write implies read permissions
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



#define pte_present(pte)	(pte_val(pte) & L_PTE_PRESENT)

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_read(pte)			(pte_val(pte) & L_PTE_USER)
#define pte_write(pte)			(pte_val(pte) & L_PTE_WRITE)
#define pte_exec(pte)			(pte_val(pte) & L_PTE_EXEC)
#define pte_dirty(pte)			(pte_val(pte) & L_PTE_DIRTY)
#define pte_young(pte)			(pte_val(pte) & L_PTE_YOUNG)

#define PTE_BIT_FUNC(fn,op)			\
extern inline pte_t fn##(pte_t pte) { pte_val(pte) op##; return pte; }

//PTE_BIT_FUNC(pte_rdprotect, &= ~L_PTE_USER);
PTE_BIT_FUNC(pte_wrprotect, &= ~L_PTE_WRITE);
PTE_BIT_FUNC(pte_exprotect, &= ~L_PTE_EXEC);
PTE_BIT_FUNC(pte_mkclean,   &= ~L_PTE_DIRTY);
PTE_BIT_FUNC(pte_mkold,     &= ~L_PTE_YOUNG);
//PTE_BIT_FUNC(pte_mkread,    |= L_PTE_USER);
PTE_BIT_FUNC(pte_mkwrite,   |= L_PTE_WRITE);
PTE_BIT_FUNC(pte_mkexec,    |= L_PTE_EXEC);
PTE_BIT_FUNC(pte_mkdirty,   |= L_PTE_DIRTY);
PTE_BIT_FUNC(pte_mkyoung,   |= L_PTE_YOUNG);
PTE_BIT_FUNC(pte_nocache,   &= ~L_PTE_CACHEABLE);

extern __inline__ pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

/* Find an entry in the third-level page table.. */
extern __inline__ pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

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

#define SWP_TYPE(entry) (((entry) >> 2) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 9)
#define SWP_ENTRY(type,offset) (((type) << 2) | ((offset) << 9))

#endif /* __ASM_PROC_PAGE_H */
