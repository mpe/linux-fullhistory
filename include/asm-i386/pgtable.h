#ifndef _I386_PGTABLE_H
#define _I386_PGTABLE_H

#include <linux/config.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * the i386, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the i386 page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <linux/tasks.h>

/* Caches aren't brain-dead on the intel. */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(mm, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 */

#define __flush_tlb() \
do { unsigned long tmpreg; __asm__ __volatile__("movl %%cr3,%0\n\tmovl %0,%%cr3":"=r" (tmpreg) : :"memory"); } while (0)

#ifndef CONFIG_X86_INVLPG
#define __flush_tlb_one(addr) flush_tlb()
#else
#define __flush_tlb_one(addr) \
__asm__ __volatile__("invlpg %0": :"m" (*(char *) addr))
#endif
 
#ifndef __SMP__

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() __flush_tlb()
#define local_flush_tlb() __flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->mm)
		__flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	if (vma->vm_mm == current->mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_range(struct mm_struct *mm,
	unsigned long start, unsigned long end)
{
	if (mm == current->mm)
		__flush_tlb();
}

#else

/*
 * We aren't very clever about this yet -  SMP could certainly
 * avoid some global flushes..
 */

#include <asm/smp.h>

#define local_flush_tlb() \
	__flush_tlb()


#define CLEVER_SMP_INVALIDATE
#ifdef CLEVER_SMP_INVALIDATE

/*
 *	Smarter SMP flushing macros. 
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 */
 
static inline void flush_tlb_current_task(void)
{
	/* just one copy of this mm? */
	if (atomic_read(&current->mm->count) == 1)
		local_flush_tlb();	/* and that's us, so.. */
	else
		smp_flush_tlb();
}

#define flush_tlb() flush_tlb_current_task()

#define flush_tlb_all() smp_flush_tlb()

static inline void flush_tlb_mm(struct mm_struct * mm)
{
	if (mm == current->mm && atomic_read(&mm->count) == 1)
		local_flush_tlb();
	else
		smp_flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct * vma,
	unsigned long va)
{
	if (vma->vm_mm == current->mm && atomic_read(&current->mm->count) == 1)
		__flush_tlb_one(va);
	else
		smp_flush_tlb();
}

static inline void flush_tlb_range(struct mm_struct * mm,
	unsigned long start, unsigned long end)
{
	flush_tlb_mm(mm);
}


#else

#define flush_tlb() \
	smp_flush_tlb()

#define flush_tlb_all() flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	flush_tlb();
}

static inline void flush_tlb_range(struct mm_struct *mm,
	unsigned long start, unsigned long end)
{
	flush_tlb();
}
#endif
#endif
#endif /* !__ASSEMBLY__ */


/* Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the i386 is two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

/*
 * pgd entries used up by user/kernel:
 */

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)
#define __USER_PGD_PTRS ((__PAGE_OFFSET >> PGDIR_SHIFT) & 0x3ff)
#define __KERNEL_PGD_PTRS (PTRS_PER_PGD-__USER_PGD_PTRS)

#ifndef __ASSEMBLY__
/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	(8*1024*1024)
#define VMALLOC_START	(((unsigned long) high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	(FIXADDR_START)

/*
 * The 4MB page is guessing..  Detailed in the infamous "Chapter H"
 * of the Pentium details, but assuming intel did the straightforward
 * thing, this bit set in the page directory entry just means that
 * the page directory entry points directly to a 4MB-aligned block of
 * memory. 
 */
#define _PAGE_PRESENT	0x001
#define _PAGE_RW	0x002
#define _PAGE_USER	0x004
#define _PAGE_WT	0x008
#define _PAGE_PCD	0x010
#define _PAGE_ACCESSED	0x020
#define _PAGE_DIRTY	0x040
#define _PAGE_4M	0x080	/* 4 MB page, Pentium+, if present.. */
#define _PAGE_GLOBAL	0x100	/* Global TLB entry PPro+ */

#define _PAGE_PROTNONE	0x080	/* If not present */

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED)

/*
 * The i386 can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
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
 * Define this if things work differently on an i386 and an i486:
 * it will (on an i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef TEST_VERIFY_AREA

/* page table for 0-4MB for everybody */
extern unsigned long pg0[1024];
/* zero page used for uninitialized stuff */
extern unsigned long empty_zero_page[1024];

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE ((unsigned long) empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2			2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	unsigned long __pgdir = __pa(pgdir); \
	(tsk)->tss.cr3 = __pgdir; \
	if ((tsk) == current) \
		__asm__ __volatile__("movl %0,%%cr3": :"r" (__pgdir)); \
} while (0)

#define pte_none(x)	(!pte_val(x))
#define pte_present(x)	(pte_val(x) & (_PAGE_PRESENT | _PAGE_PROTNONE))
#define pte_clear(xp)	do { pte_val(*(xp)) = 0; } while (0)

#define pmd_none(x)	(!pmd_val(x))
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = 0; } while (0)

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
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }

extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) &= ~_PAGE_RW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_RW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot) \
({ pte_t __pte; pte_val(__pte) = __pa(page) + pgprot_val(pgprot); __pte; })

/* This takes a physical page address that is used by the remapping functions */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; pte_val(__pte) = physpage + pgprot_val(pgprot); __pte; })

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

#define pte_page(pte) \
((unsigned long) __va(pte_val(pte) & PAGE_MASK))

#define pmd_page(pmd) \
((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/* to find an entry in a page-table-directory */
#define pgd_offset(mm, address) \
((mm)->pgd + ((address) >> PGDIR_SHIFT))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
#define pte_offset(pmd, address) \
((pte_t *) (pmd_page(*pmd) + ((address>>10) & ((PTRS_PER_PTE-1)<<2))))

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

#define pgd_quicklist (current_cpu_data.pgd_quick)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (current_cpu_data.pte_quick)
#define pgtable_cache_size (current_cpu_data.pgtable_cache_sz)

extern __inline__ pgd_t *get_pgd_slow(void)
{
	pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL), *init;

	if (ret) {
		init = pgd_offset(&init_mm, 0);
		memset (ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy (ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return ret;
}

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
	free_page((unsigned long)pgd);
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
	free_page((unsigned long)pte);
}

/* We don't use pmd cache, so these are dummy routines */
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

extern void __bad_pte(pmd_t *pmd);
extern void __bad_pte_kernel(pmd_t *pmd);

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)
#define pgd_free(pgd)           free_pgd_fast(pgd)
#define pgd_alloc()             get_pgd_fast()

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_pte_fast();
		
		if (!page)
			return get_pte_kernel_slow(pmd, address);
		pmd_val(*pmd) = _KERNPG_TABLE + __pa(page);
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__bad_pte_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> (PAGE_SHIFT-2)) & 4*(PTRS_PER_PTE - 1);

	if (pmd_none(*pmd))
		goto getnew;
	if (pmd_bad(*pmd))
		goto fix;
	return (pte_t *) (pmd_page(*pmd) + address);
getnew:
{
	unsigned long page = (unsigned long) get_pte_fast();
	
	if (!page)
		return get_pte_slow(pmd, address);
	pmd_val(*pmd) = _PAGE_TABLE + __pa(page);
	return (pte_t *) (page + address);
}
fix:
	__bad_pte(pmd);
	return NULL;
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

#define pmd_free_kernel		pmd_free
#define pmd_alloc_kernel	pmd_alloc

extern int do_check_pgt_cache(int, int);

extern inline void set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;
	pgd_t *pgd;
#ifdef __SMP__
	int i;
#endif	
        
	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm,address) = entry;
	}
	read_unlock(&tasklist_lock);
#ifndef __SMP__
	for (pgd = (pgd_t *)pgd_quicklist; pgd; pgd = (pgd_t *)*(unsigned long *)pgd)
		pgd[address >> PGDIR_SHIFT] = entry;
#else
	/* To pgd_alloc/pgd_free, one holds master kernel lock and so does our callee, so we can
	   modify pgd caches of other CPUs as well. -jj */
	for (i = 0; i < NR_CPUS; i++)
		for (pgd = (pgd_t *)cpu_data[i].pgd_quick; pgd; pgd = (pgd_t *)*(unsigned long *)pgd)
			pgd[address >> PGDIR_SHIFT] = entry;
#endif
}

extern pgd_t swapper_pg_dir[1024];

/*
 * The i386 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
}

#define SWP_TYPE(entry) (((entry) >> 1) & 0x3f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))

#define module_map      vmalloc
#define module_unmap    vfree

#endif /* !__ASSEMBLY__ */

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)

#endif /* _I386_PAGE_H */
