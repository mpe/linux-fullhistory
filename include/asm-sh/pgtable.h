#ifndef __ASM_SH_PGTABLE_H
#define __ASM_SH_PGTABLE_H

/* Copyright (C) 1999 Niibe Yutaka */

/*
 * This file contains the functions and defines necessary to modify and use
 * the SuperH page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/addrspace.h>
#include <linux/threads.h>

extern pgd_t swapper_pg_dir[1024];

#if defined(__sh3__)
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
#elif defined(__SH4__)
/*
 *  Caches are broken on SH-4, so we need them.
 */
extern void flush_cache_all(void);
extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_range(struct mm_struct *mm, unsigned long start,
			      unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma, unsigned long addr);
extern void flush_page_to_ram(unsigned long page);
extern void flush_icache_range(unsigned long start, unsigned long end);
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
extern inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
}

/*
 * Basically we have the same two-level (which is the logical three level
 * Linux page table layout folded) page tables as the i386.
 */

#endif /* !__ASSEMBLY__ */

#define pgd_quicklist (current_cpu_data.pgd_quick)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (current_cpu_data.pte_quick)
#define pgtable_cache_size (current_cpu_data.pgtable_cache_sz)

#include <asm/pgtable-2level.h>

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

#define __beep() asm("")

#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

#define TWOLEVEL_PGDIR_SHIFT	22
#define BOOT_USER_PGD_PTRS (PAGE_OFFSET >> TWOLEVEL_PGDIR_SHIFT)
#define BOOT_KERNEL_PGD_PTRS (1024-BOOT_USER_PGD_PTRS)

#ifndef __ASSEMBLY__
#define VMALLOC_START	P3SEG
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	P4SEG

#define _PAGE_READ 	0x001  /* software: read access alowed */
#define _PAGE_ACCESSED	0x002  /* software: page referenced */
#define _PAGE_DIRTY	0x004  /* D-bit   : page changed */
/*		 	0x008  */
/*		 	0x010  */
#define _PAGE_RW	0x020  /* PR0-bit : write access allowed */
#define _PAGE_USER	0x040  /* PR1-bit : user space access allowed */
#define _PAGE_PROTNONE	0x080  /* software: if not present */
#define _PAGE_PRESENT	0x100  /* V-bit   : page is valid */

#if defined(__sh3__)
/* Mask which drop software flags */
#define _PAGE_FLAGS_HARDWARE_MASK	0x1ffff164
/* Flags defalult: SZ=1 (4k-byte), C=1 (cachable), SH=0 (not shared) */
#define _PAGE_FLAGS_HARDWARE_DEFAULT	0x00000018
#elif defined(__SH4__)
/* Mask which drops software flags */
#define _PAGE_FLAGS_HARDWARE_MASK	0x1ffff164
/* Flags defalult: SZ=01 (4k-byte), C=1 (cachable), SH=0 (not shared), WT=0 */
#define _PAGE_FLAGS_HARDWARE_DEFAULT	0x00000018
#endif

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

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[1024];
#define ZERO_PAGE(vaddr) (mem_map + MAP_NR(empty_zero_page))

/*
 * Handling allocation failures during page table setup.
 */
extern void __handle_bad_pmd(pmd_t * pmd);
extern void __handle_bad_pmd_kernel(pmd_t * pmd);

#define pte_none(x)	(!pte_val(x))
#define pte_present(x)	(pte_val(x) & (_PAGE_PRESENT | _PAGE_PROTNONE))
#define pte_clear(xp)	do { pte_val(*(xp)) = 0; } while (0)
#define pte_pagenr(x)	((unsigned long)(((pte_val(x) -__MEMORY_START) >> PAGE_SHIFT)))

#define pmd_none(x)	(!pmd_val(x))
#define	pmd_bad(x)	((pmd_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define pmd_present(x)	(pmd_val(x) & _PAGE_PRESENT)
#define pmd_clear(xp)	do { pmd_val(*(xp)) = 0; } while (0)

/*
 * Permanent address of a page. Obviously must never be
 * called on a highmem page.
 */
#define page_address(page) ({ PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT) + __MEMORY_START; })
#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))
#define pte_page(x) (mem_map+pte_pagenr(x))

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte) { return pte_val(pte) & _PAGE_USER; }
extern inline int pte_exec(pte_t pte) { return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte){ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte){ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_write(pte_t pte){ return pte_val(pte) & _PAGE_RW; }
       
extern inline pte_t pte_rdprotect(pte_t pte){ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte){ pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_wrprotect(pte_t pte){ pte_val(pte) &= ~_PAGE_RW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) |= _PAGE_RW; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(struct page *page, pgprot_t pgprot)
{
	pte_t __pte;

	pte_val(__pte) = (page-mem_map)*(unsigned long long)PAGE_SIZE +
				__MEMORY_START + pgprot_val(pgprot);
	return __pte;
}

/* This takes a physical page address that is used by the remapping functions */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; pte_val(__pte) = physpage + pgprot_val(pgprot); __pte; })

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

#define page_pte_prot(page,prot) mk_pte(page, prot)
#define page_pte(page) page_pte_prot(page, __pgprot(0))

#define pmd_page(pmd) \
((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/* to find an entry in a page-table-directory. */
#define __pgd_offset(address) \
		((address >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))

#define pgd_offset(mm, address) ((mm)->pgd+__pgd_offset(address))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define __pmd_offset(address) \
		(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/* Find an entry in the third-level page table.. */
#define __pte_offset(address) \
		((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset(dir, address) ((pte_t *) pmd_page(*(dir)) + \
			__pte_offset(address))

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

extern __inline__ pgd_t *get_pgd_slow(void)
{
	pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (ret) {
		/* Clear User space */
		memset(ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));

		/* XXX: Copy vmalloc-ed space??? */
		memcpy(ret + USER_PTRS_PER_PGD,
		       swapper_pg_dir + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
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

#define pte_free_kernel(pte)    free_pte_slow(pte)
#define pte_free(pte)           free_pte_slow(pte)
#define pgd_free(pgd)           free_pgd_slow(pgd)
#define pgd_alloc()             get_pgd_fast()

extern __inline__ pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_pte_fast();
		
		if (!page)
			return get_pte_kernel_slow(pmd, address);
		pmd_val(*pmd) = _KERNPG_TABLE + __pa(page);
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern __inline__ pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none(*pmd))
		goto getnew;
	if (pmd_bad(*pmd))
		goto fix;
	return (pte_t *)pmd_page(*pmd) + address;
getnew:
{
	unsigned long page = (unsigned long) get_pte_fast();
	
	if (!page)
		return get_pte_slow(pmd, address);
	pmd_val(*pmd) = _PAGE_TABLE + __pa(page);
	return (pte_t *)page + address;
}
fix:
	__handle_bad_pmd(pmd);
	return NULL;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free(pmd_t * pmd)
{
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

/* Encode and de-code a swap entry */
#define SWP_TYPE(x)			(((x).val >> 1) & 0x3f)
#define SWP_OFFSET(x)			((x).val >> 8)
#define SWP_ENTRY(type, offset)		((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define swp_entry_to_pte(x)		((pte_t) { (x).val })

#define module_map      vmalloc
#define module_unmap    vfree

#endif /* !__ASSEMBLY__ */

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)
#define kern_addr_valid(addr)	(1)

#define io_remap_page_range remap_page_range

#endif /* __ASM_SH_PAGE_H */
