#ifndef __ASM_SH_PGTABLE_H
#define __ASM_SH_PGTABLE_H

/* Copyright (C) 1999 Niibe Yutaka */

#include <linux/config.h>

/*
 * This file contains the functions and defines necessary to modify and use
 * the SuperH page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/threads.h>

extern pgd_t swapper_pg_dir[1024];

#ifdef CONFIG_CPU_SH3
/* Cache flushing:
 *
 *  - flush_cache_all() flushes entire cache
 *  - flush_cache_mm(mm) flushes the specified mm context's cache lines
 *  - flush_cache_page(mm, vmaddr) flushes a single page
 *  - flush_cache_range(mm, start, end) flushes a range of pages
 *  - flush_page_to_ram(page) write back kernel page to ram
 *
 *  Caches are indexed (effectively) by physical address on SH-3, so
 *  we don't need them.
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(mm, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)
#elif CONFIG_CPU_SH4
/*
 *  Caches are broken on SH-4, so we need them.
 *  You do bad job!
 */
flush_cache_all()
flush_cache_mm(mm)
flush_cache_range(mm, start, end)
flush_cache_page(vma, vmaddr)
flush_page_to_ram(page)
flush_icache_range(start, end)
#endif

/* TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLB entries
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB entries
 *  - flush_tlb_page(mm, vmaddr) flushes a single page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 */
extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_range(struct mm_struct *mm, unsigned long start,
			    unsigned long end);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long page);

/*
 * Basically we have the same two-level (which is the logical three level
 * Linux page table layout folded) page tables as the i386.
 */

#endif /* !__ASSEMBLY__ */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level: we use two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

#ifndef __ASSEMBLY__
#define VMALLOC_START	0xc0000000
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	0xe0000000

#define _PAGE_READ 	0x001  /* software: read access alowed */
#define _PAGE_ACCESSED	0x002  /* software: page referenced */
#define _PAGE_DIRTY	0x004  /* D-bit   : page changed */
/*		 	0x008  */
/*		 	0x010  */
#define _PAGE_RW	0x020  /* PR0-bit : write access allowed */
#define _PAGE_USER	0x040  /* PR1-bit : user space access allowed */
/*		 	0x080  */
#define _PAGE_PRESENT	0x100  /* V-bit   : page is valid */

/* Mask which drop software flags */
#define _PAGE_FLAGS_HARDWARE_MASK	0xfffff164
/* Flags defalult: SZ=1 (4k-byte), C=1 (cachable), SH=0 (not shared) */
#define _PAGE_FLAGS_HARDWARE_DEFAULT	0x00000018


#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED)

/*
 * As i386 and MIPS, SuperH can't do page protection for execute, and
 * considers that the same are read.  Also, write permissions imply
 * read permissions. This is the closest we can get..  
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

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE(vaddr) ((unsigned long) empty_zero_page)

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

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
extern __inline__ void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

extern __inline__ int pte_none(pte_t pte)
{
	return !pte_val(pte);
}

extern __inline__ int pte_present(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

extern __inline__ void pte_clear(pte_t *ptep)
{
	pte_val(*ptep) = 0;
}

extern __inline__ int pmd_none(pmd_t pmd)
{
	return !pmd_val(pmd);
}

#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)

extern __inline__ int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) & _PAGE_PRESENT;
}

extern __inline__ void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = 0;
}

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern __inline__ int pgd_none(pgd_t pgd)	{ return 0; }
extern __inline__ int pgd_bad(pgd_t pgd)	{ return 0; }
extern __inline__ int pgd_present(pgd_t pgd)	{ return 1; }
extern __inline__ void pgd_clear(pgd_t * pgdp)	{ }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern __inline__ int pte_read(pte_t pte) { return pte_val(pte) & _PAGE_USER; }
extern __inline__ int pte_exec(pte_t pte) { return pte_val(pte) & _PAGE_USER; }
extern __inline__ int pte_dirty(pte_t pte){ return pte_val(pte) & _PAGE_DIRTY; }
extern __inline__ int pte_young(pte_t pte){ return pte_val(pte) & _PAGE_ACCESSED; }
extern __inline__ int pte_write(pte_t pte){ return pte_val(pte) & _PAGE_RW; }

extern __inline__ pte_t pte_rdprotect(pte_t pte){ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern __inline__ pte_t pte_exprotect(pte_t pte){ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern __inline__ pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern __inline__ pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern __inline__ pte_t pte_wrprotect(pte_t pte){ pte_val(pte) &= ~_PAGE_RW; return pte; }
extern __inline__ pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern __inline__ pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern __inline__ pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern __inline__ pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern __inline__ pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_RW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern __inline__ pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{
	return __pte(__pa(page) | pgprot_val(pgprot));
}

/* This takes a physical page address that is used by the remapping functions */
extern __inline__ pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
	return __pte(physpage | pgprot_val(pgprot));
}

extern __inline__ pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

extern __inline__ unsigned long pte_page(pte_t pte)
{
	return (unsigned long)__va(pte_val(pte) & PAGE_MASK);
}

extern __inline__ unsigned long pmd_page(pmd_t pmd)
{
	return (unsigned long)__va(pmd_val(pmd) & PAGE_MASK);
}

extern __inline__ void pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	pmd_val(*pmdp) = __pa(((unsigned long) ptep) & PAGE_MASK) | _PAGE_TABLE;
}

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
extern __inline__ pgd_t *pgd_offset(struct mm_struct *mm, unsigned long addr)
{
	return mm->pgd + (addr >> PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
extern __inline__ pmd_t * pmd_offset(pgd_t * dir, unsigned long addr)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
extern __inline__ pte_t *pte_offset(pmd_t * dir, unsigned long addr)
{
	return (pte_t *) (pmd_page(*dir)) +
	       ((addr >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

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
	pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (ret) {
		memset(ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(ret + USER_PTRS_PER_PGD, swapper_pg_dir + USER_PTRS_PER_PGD, (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return ret;
}

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if ((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
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

#define pte_free_kernel(pte)    free_pte_slow(pte)
#define pte_free(pte)           free_pte_slow(pte)
#define pgd_free(pgd)           free_pgd_slow(pgd)
#define pgd_alloc()             get_pgd_fast()

extern __inline__ pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = get_pte_fast();
		
		if (!page)
			return get_pte_kernel_slow(pmd, address);
		pmd_set(pmd, page);
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__bad_pte_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern __inline__ pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
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

extern pgd_t swapper_pg_dir[1024];

extern void update_mmu_cache(struct vm_area_struct * vma,
			     unsigned long address, pte_t pte);

#define SWP_TYPE(entry) (((entry) >> 1) & 0x3f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))

#define module_map      vmalloc
#define module_unmap    vfree

#endif /* !__ASSEMBLY__ */

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)
#define kern_addr_valid(addr)	(1)

#define io_remap_page_range remap_page_range

#endif /* __ASM_SH_PAGE_H */
