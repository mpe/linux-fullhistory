/*
 * linux/include/asm-arm/pgtable.h
 */
#ifndef _ASMARM_PGTABLE_H
#define _ASMARM_PGTABLE_H

#include <linux/config.h>

#include <asm/arch/memory.h>
#include <asm/proc-fns.h>
#include <asm/system.h>

/*
 * PMD_SHIFT determines the size of the area a second-level page table can map
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
#define PMD_SHIFT		20
#define PGDIR_SHIFT		20

#define LIBRARY_TEXT_START	0x0c000000

#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, unsigned long val);
extern void __pmd_error(const char *file, int line, unsigned long val);
extern void __pgd_error(const char *file, int line, unsigned long val);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte_val(pte))
#define pmd_ERROR(pmd)		__pmd_error(__FILE__, __LINE__, pmd_val(pmd))
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd_val(pgd))
#endif /* !__ASSEMBLY__ */

#define PMD_SIZE		(1UL << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE-1))
#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))

#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

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

#ifndef __ASSEMBLY__
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
struct page *empty_zero_page;
#define ZERO_PAGE(vaddr)	(empty_zero_page)

/*
 * Handling allocation failures during page table setup.
 */
extern void __handle_bad_pmd(pmd_t *pmd);
extern void __handle_bad_pmd_kernel(pmd_t *pmd);

#define pte_none(pte)		(!pte_val(pte))
#define pte_clear(ptep)		set_pte((ptep), __pte(0))
#define pte_pagenr(pte)		((unsigned long)(((pte_val(pte) - PHYS_OFFSET) >> PAGE_SHIFT)))

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_clear(pmdp)		set_pmd(pmdp, __pmd(0))

/*
 * Permanent address of a page.
 */
#define page_address(page)	(PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT))
#define pages_to_mb(x)		((x) >> (20 - PAGE_SHIFT))
#define pte_page(x)		(mem_map + pte_pagenr(x))

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
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern __inline__ pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
	pte_t pte;
	pte_val(pte) = physpage | pgprot_val(pgprot);
	return pte;
}

extern __inline__ pte_t mk_pte(struct page *page, pgprot_t pgprot)
{
	pte_t pte;
	pte_val(pte) = (PHYS_OFFSET + ((page - mem_map) << PAGE_SHIFT)) | pgprot_val(pgprot);
	return pte;
}

#define page_pte_prot(page,prot)	mk_pte(page, prot)
#define page_pte(page)		mk_pte(page, __pgprot(0))

/* to find an entry in a page-table-directory */
#define __pgd_offset(addr)	((addr) >> PGDIR_SHIFT)

#define pgd_offset(mm, addr)	((mm)->pgd+__pgd_offset(addr))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, addr)	((pmd_t *)(dir))

/* Find an entry in the third-level page table.. */
#define __pte_offset(addr)	(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset(dir, addr)	((pte_t *)pmd_page(*(dir)) + __pte_offset(addr))

/*
 * Get the cache handling stuff now.
 */
#include <asm/proc/cache.h>

/*
 * Page table cache stuff
 */
#ifndef CONFIG_NO_PGT_CACHE

#ifdef __SMP__
#error Pgtable caches have to be per-CPU, so that no locking is needed.
#endif	/* __SMP__ */

extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist		(quicklists.pgd_cache)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		(quicklists.pte_cache)
#define pgtable_cache_size	(quicklists.pgtable_cache_sz)

/* used for quicklists */
#define __pgd_next(pgd) (((unsigned long *)pgd)[1])
#define __pte_next(pte)	(((unsigned long *)pte)[0])

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if ((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)__pgd_next(ret);
		ret[1] = ret[2];
		clean_cache_area(ret + 1, 4);
		pgtable_cache_size--;
	}
	return (pgd_t *)ret;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	__pgd_next(pgd) = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

/* We don't use pmd cache, so this is a dummy routine */
#define get_pmd_fast()		((pmd_t *)0)

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
}

extern __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)__pte_next(ret);
		ret[0] = ret[1];
		clean_cache_area(ret, 4);
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	__pte_next(pte) = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

#else	/* CONFIG_NO_PGT_CACHE */

#define pgd_quicklist		((unsigned long *)0)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		((unsigned long *)0)

#define get_pgd_fast()		((pgd_t *)0)
#define get_pmd_fast()		((pmd_t *)0)
#define get_pte_fast()		((pte_t *)0)

#define free_pgd_fast(pgd)	free_pgd_slow(pgd)
#define free_pmd_fast(pmd)	free_pmd_slow(pmd)
#define free_pte_fast(pte)	free_pte_slow(pte)

#endif	/* CONFIG_NO_PGT_CACHE */

extern pgd_t *get_pgd_slow(void);
extern void free_pgd_slow(pgd_t *pgd);

#define free_pmd_slow(pmd)	do { } while (0)

extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long addr_preadjusted);
extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long addr_preadjusted);
extern void free_pte_slow(pte_t *pte);

#include <asm/proc/pgtable.h>

extern __inline__ pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */
#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)

#ifndef pte_alloc_kernel
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
		__handle_bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}
#endif

extern __inline__ pte_t *pte_alloc(pmd_t * pmd, unsigned long address)
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
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

#define pmd_free_kernel		pmd_free
#define pmd_free(pmd)		do { } while (0)

#define pmd_alloc_kernel	pmd_alloc
extern __inline__ pmd_t *pmd_alloc(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

#define pgd_free(pgd)		free_pgd_fast(pgd)

extern __inline__ pgd_t *pgd_alloc(void)
{
	pgd_t *pgd;

	pgd = get_pgd_fast();
	if (!pgd)
		pgd = get_pgd_slow();

	return pgd;
}

extern int do_check_pgt_cache(int, int);

extern __inline__ void set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm,address) = entry;
	}
	read_unlock(&tasklist_lock);

#ifndef CONFIG_NO_PGT_CACHE
	{
		pgd_t *pgd;
		for (pgd = (pgd_t *)pgd_quicklist; pgd;
		     pgd = (pgd_t *)__pgd_next(pgd))
			pgd[address >> PGDIR_SHIFT] = entry;
	}
#endif
}

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#define update_mmu_cache(vma,address,pte)

/*
 * We support up to 32GB of swap on 4k machines
 */
#define SWP_TYPE(entry)		(((pte_val(entry)) >> 2) & 0x7f)
#define SWP_OFFSET(entry)	((pte_val(entry)) >> 9)
#define SWP_ENTRY(type,offset)	__pte((((type) << 2) | ((offset) << 9)))

#define module_map		vmalloc
#define module_unmap		vfree

/*
 * We rely on GCC optimising this code away for
 * architectures which it doesn't apply to.  Note
 * that `addr' is checked against PAGE_OFFSET and
 * end_mem by the calling code.
 */
#define __kern_valid_idx(a)	(((a) - PAGE_OFFSET) >> 20)

extern __inline__ int __kern_valid_addr(unsigned long addr)
{
	extern unsigned long *valid_addr_bitmap;
	unsigned int idx = __kern_valid_idx(addr);

	return test_bit(idx, valid_addr_bitmap);
}

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(machine_is_riscpc() && test_bit(PG_skip, &(page)->flags))
#define kern_addr_valid(addr)	(!machine_is_riscpc() || __kern_valid_addr(addr))

#define io_remap_page_range	remap_page_range

#endif /* !__ASSEMBLY__ */

#endif /* _ASMARM_PGTABLE_H */
