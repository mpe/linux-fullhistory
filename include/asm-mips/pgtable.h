#ifndef __ASM_MIPS_PGTABLE_H
#define __ASM_MIPS_PGTABLE_H

#ifndef __LANGUAGE_ASSEMBLY__

#include <linux/linkage.h>
#include <asm/cachectl.h>

/*
 * The Linux memory management assumes a three-level page table setup. In
 * 32 bit mode we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * i386 mmu expects. The 64 bit version uses a three level setup.
 *
 * This file contains the functions and defines necessary to modify and use
 * the MIPS page table tree.  Note the frequent conversion between addresses
 * in KSEG0 and KSEG1.
 *
 * This is required due to the cache aliasing problem of the R4xx0 series.
 * Sometimes doing uncached accesses also to improve the cache performance
 * slightly.  The R10000 caching mode "uncached accelerated" will help even
 * further.
 */

/*
 * TLB invalidation:
 *
 *  - invalidate() invalidates the current mm struct TLBs
 *  - invalidate_all() invalidates all processes TLBs
 *  - invalidate_mm(mm) invalidates the specified mm context TLB's
 *  - invalidate_page(mm, vmaddr) invalidates one page
 *  - invalidate_range(mm, start, end) invalidates a range of pages
 *
 * FIXME: MIPS has full control of all TLB activity in the CPU.  Though
 * we just stick with complete flushing of TLBs for now.
 */
extern asmlinkage void tlbflush(void);
#define invalidate()	({sys_cacheflush(0, ~0, BCACHE);tlbflush();})

#define invalidate_all() invalidate()
#define invalidate_mm(mm_struct) \
do { if ((mm_struct) == current->mm) invalidate(); } while (0)
#define invalidate_page(mm_struct,addr) \
do { if ((mm_struct) == current->mm) invalidate(); } while (0)
#define invalidate_range(mm_struct,start,end) \
do { if ((mm_struct) == current->mm) invalidate(); } while (0)

/*
 * We need a special version of copy_page that can handle virtual caches.
 * While we're at tweaking with caches we can use that to make it faster.
 * The R10000's accelerated caching mode will further accelerate it.
 */
extern void __copy_page(unsigned long from, unsigned long to);
#define copy_page(from,to) __copy_page((unsigned long)from, (unsigned long)to)

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

#endif /* !defined (__LANGUAGE_ASSEMBLY__) */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: we use two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024

#define VMALLOC_START     KSEG2
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

/*
 * Note that we shift the lower 32bits of each EntryLo[01] entry
 * 6 bits to the left. That way we can convert the PFN into the
 * physical address by a single 'and' operation and gain 6 additional
 * bits for storing information which isn't present in a normal
 * MIPS page table.
 * Since the Mips has chosen some quite misleading names for the
 * valid and dirty bits they're defined here but only their synonyms
 * will be used.
 */
#define _PAGE_PRESENT               (1<<0)  /* implemented in software */
#define _PAGE_COW                   (1<<1)  /* implemented in software */
#define _PAGE_READ                  (1<<2)  /* implemented in software */
#define _PAGE_WRITE                 (1<<3)  /* implemented in software */
#define _PAGE_ACCESSED              (1<<4)  /* implemented in software */
#define _PAGE_MODIFIED              (1<<5)  /* implemented in software */
#define _PAGE_GLOBAL                (1<<6)
#define _PAGE_VALID                 (1<<7)
#define _PAGE_SILENT_READ           (1<<7)  /* synonym                 */
#define _PAGE_DIRTY                 (1<<8)  /* The MIPS dirty bit      */
#define _PAGE_SILENT_WRITE          (1<<8)
#define _CACHE_CACHABLE_NO_WA       (0<<9)  /* R4600 only              */
#define _CACHE_CACHABLE_WA          (1<<9)  /* R4600 only              */
#define _CACHE_UNCACHED             (2<<9)  /* R4[0246]00              */
#define _CACHE_CACHABLE_NONCOHERENT (3<<9)  /* R4[0246]00              */
#define _CACHE_CACHABLE_CE          (4<<9)  /* R4[04]00 only           */
#define _CACHE_CACHABLE_COW         (5<<9)  /* R4[04]00 only           */
#define _CACHE_CACHABLE_CUW         (6<<9)  /* R4[04]00 only           */
#define _CACHE_CACHABLE_ACCELERATED (7<<9)  /* R10000 only             */
#define _CACHE_MASK                 (7<<9)

#define __READABLE	(_PAGE_READ|_PAGE_SILENT_READ|_PAGE_ACCESSED)
#define __WRITEABLE	(_PAGE_WRITE|_PAGE_SILENT_WRITE|_PAGE_MODIFIED)

#define _PAGE_TABLE	(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
			_PAGE_DIRTY | _CACHE_UNCACHED)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | _CACHE_MASK)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | __READABLE | _CACHE_UNCACHED)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | __READABLE | _PAGE_WRITE | \
			_PAGE_ACCESSED | _CACHE_CACHABLE_NONCOHERENT)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | __READABLE | _PAGE_COW | \
			_CACHE_CACHABLE_NONCOHERENT)
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | __READABLE | \
			_CACHE_CACHABLE_NONCOHERENT)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
			_CACHE_CACHABLE_NONCOHERENT)

/*
 * MIPS can't do page protection for execute, and considers that the same like
 * read. Also, write permissions imply read permissions. This is the closest
 * we can get by reasonable means..
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

#if !defined (__LANGUAGE_ASSEMBLY__)

/* page table for 0-4MB for everybody */
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
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/*
 * sizeof(void*)==1<<SIZEOF_PTR_LOG2
 */
#if __mips == 3
#define SIZEOF_PTR_LOG2			3
#else
#define SIZEOF_PTR_LOG2			2
#endif

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

/* to set the page-dir */
#define SET_PAGE_DIR(tsk,pgdir) \
do { \
	(tsk)->tss.pg_dir = ((unsigned long) (pgdir)) - PT_OFFSET; \
	if ((tsk) == current) \
	{ \
		void load_pgd(unsigned long pg_dir); \
 \
		load_pgd((tsk)->tss.pg_dir); \
	} \
} while (0)

extern unsigned long high_memory;
extern pmd_t invalid_pte_table[PAGE_SIZE/sizeof(pmd_t)];

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline unsigned long pte_page(pte_t pte)
{ return PAGE_OFFSET + (pte_val(pte) & PAGE_MASK); }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return PAGE_OFFSET + (pmd_val(pmd) & PAGE_MASK); }

extern inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{ pmd_val(*pmdp) = _PAGE_TABLE | ((unsigned long) ptep - PT_OFFSET); }

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
extern inline int pte_inuse(pte_t *ptep)	{ return mem_map[MAP_NR(ptep)].reserved || mem_map[MAP_NR(ptep)].count != 1; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }
extern inline void pte_reuse(pte_t * ptep)
{
	if (!mem_map[MAP_NR(ptep)].reserved)
		mem_map[MAP_NR(ptep)].count++;
}

/*
 * Empty pgd/pmd entries point to the invalid_pte_table.
 */
extern inline int pmd_none(pmd_t pmd)		{ return (pmd_val(pmd) & PAGE_MASK) == ((unsigned long) invalid_pte_table - PAGE_OFFSET); }

extern inline int pmd_bad(pmd_t pmd)
{
	return (pmd_val(pmd) & ~PAGE_MASK) != _PAGE_TABLE ||
	        pmd_page(pmd) > high_memory ||
	        pmd_page(pmd) < PAGE_OFFSET;
}
extern inline int pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & _PAGE_PRESENT; }
extern inline int pmd_inuse(pmd_t *pmdp)	{ return 0; }
extern inline void pmd_clear(pmd_t * pmdp)	{ pmd_val(*pmdp) = ((unsigned long) invalid_pte_table - PAGE_OFFSET); }
extern inline void pmd_reuse(pmd_t * pmdp)	{ }

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)		{ return 0; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return 1; }
extern inline int pgd_inuse(pgd_t * pgdp)	{ return mem_map[MAP_NR(pgdp)].reserved; }
extern inline void pgd_clear(pgd_t * pgdp)	{ }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_READ; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_WRITE; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_READ; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_MODIFIED; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_cow(pte_t pte)		{ return pte_val(pte) & _PAGE_COW; }

extern inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_SILENT_WRITE);
	return pte;
}
extern inline pte_t pte_rdprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_READ | _PAGE_SILENT_READ); return pte;
}
extern inline pte_t pte_exprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_READ | _PAGE_SILENT_READ); return pte;
}
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~(_PAGE_MODIFIED|_PAGE_SILENT_WRITE); return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~(_PAGE_ACCESSED|_PAGE_SILENT_READ|_PAGE_SILENT_WRITE); return pte; }
extern inline pte_t pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_PAGE_COW; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_MODIFIED)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}
extern inline pte_t pte_mkread(pte_t pte)
{
	pte_val(pte) |= _PAGE_READ;
	if (pte_val(pte) & _PAGE_ACCESSED)
		pte_val(pte) |= _PAGE_SILENT_READ;
	return pte;
}
extern inline pte_t pte_mkexec(pte_t pte)
{
	pte_val(pte) |= _PAGE_READ;
	if (pte_val(pte) & _PAGE_ACCESSED)
		pte_val(pte) |= _PAGE_SILENT_READ;
	return pte;
}
extern inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_MODIFIED;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}
extern inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	if (pte_val(pte) & _PAGE_READ)
	{
		pte_val(pte) |= _PAGE_SILENT_READ;
		if ((pte_val(pte) & (_PAGE_WRITE|_PAGE_MODIFIED)) == (_PAGE_WRITE|_PAGE_MODIFIED))
			pte_val(pte) |= _PAGE_SILENT_WRITE;
	}
	return pte;
}
extern inline pte_t pte_mkcow(pte_t pte)
{
	pte_val(pte) |= _PAGE_COW;
	return pte;
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = (page - PAGE_OFFSET) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

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
	return (pte_t *) (pmd_page(*dir) + (PT_OFFSET - PAGE_OFFSET)) +
	       ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
extern inline void pte_free_kernel(pte_t * pte)
{
	unsigned long page = (unsigned long) pte;

	mem_map[MAP_NR(pte)].reserved = 0;
	if(!page)
		return;
	page -= (PT_OFFSET - PAGE_OFFSET);
	free_page(page);
}

extern inline pte_t * pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		unsigned long page = __get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				mem_map[MAP_NR(page)].reserved = 1;
				memset((void *) page, 0, PAGE_SIZE);
				sys_cacheflush((void *)page, PAGE_SIZE, DCACHE);
				sync_mem();
				page += (PT_OFFSET - PAGE_OFFSET);
				pmd_set(pmd, (pte_t *)page);
				return ((pte_t *)page) + address;
			}
			pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page(page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc_kernel: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) (pmd_page(*pmd) + (PT_OFFSET - PAGE_OFFSET)) + address;
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
	unsigned long page = (unsigned long) pte;

	if(!page)
		return;
	page -= (PT_OFFSET - PAGE_OFFSET);
	free_page(page);
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		unsigned long page = __get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				memset((void *) page, 0, PAGE_SIZE);
				sys_cacheflush((void *)page, PAGE_SIZE, DCACHE);
				sync_mem();
				page += (PT_OFFSET - PAGE_OFFSET);
				pmd_set(pmd, (pte_t *)page);
				return ((pte_t *)page) + address;
			}
			pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page(page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) (pmd_page(*pmd) + (PT_OFFSET - PAGE_OFFSET)) + address;
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
	unsigned long page = (unsigned long) pgd;

	if(!page)
		return;
	page -= (PT_OFFSET - PAGE_OFFSET);
	free_page(page);
}

/*
 * Initialize new page directory with pointers to invalid ptes
 */
extern inline void pgd_init(unsigned long page)
{
	unsigned long dummy1, dummy2;

	page += (PT_OFFSET - PAGE_OFFSET);
#if __mips >= 3
	/*
	 * Ich will Spass - ich geb Gas ich geb Gas...
	 */
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"dsll32\t$1,%2,0\n\t"
		"dsrl32\t%2,$1,0\n\t"
		"or\t%2,$1\n"
		"1:\tsd\t%2,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,8\n\t"
		".set\tmips0\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"r" (((unsigned long) invalid_pte_table - PAGE_OFFSET) |
		       _PAGE_TABLE),
		 "0" (page),
		 "1" (PAGE_SIZE/(sizeof(pmd_t)*2))
		:"$1");
#else
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tsw\t%2,(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,4\n\t"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"r" (((unsigned long) invalid_pte_table - PAGE_OFFSET) |
		       _PAGE_TABLE),
		 "0" (page),
		 "1" (PAGE_SIZE/sizeof(pmd_t)));
#endif
}

extern inline pgd_t * pgd_alloc(void)
{
	unsigned long page;

	if(!(page = __get_free_page(GFP_KERNEL)))
		return NULL;

	sys_cacheflush((void *)page, PAGE_SIZE, DCACHE);
	sync_mem();
	pgd_init(page);

	return (pgd_t *) (page + (PT_OFFSET - PAGE_OFFSET));
}

extern pgd_t swapper_pg_dir[1024];

/*
 * MIPS doesn't need any external MMU info: the kernel page tables contain
 * all the necessary information.  We use this hook though to load the
 * TLB as early as possible with uptodate information avoiding unnecessary
 * exceptions.
 */
extern void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte);

#if __mips >= 3

#define SWP_TYPE(entry) (((entry) >> 32) & 0xff)
#define SWP_OFFSET(entry) ((entry) >> 40)
#define SWP_ENTRY(type,offset) pte_val(mk_swap_pte((type),(offset)))

#else

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))

#endif

#endif /* !defined (__LANGUAGE_ASSEMBLY__) */

#endif /* __ASM_MIPS_PGTABLE_H */
