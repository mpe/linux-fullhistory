#ifndef _ASMARM_PGTABLE_H
#define _ASMARM_PGTABLE_H

#include <linux/config.h>

#include <asm/arch/memory.h>		/* For TASK_SIZE */
#include <asm/proc-fns.h>
#include <asm/system.h>
#include <asm/proc/cache.h>

#define LIBRARY_TEXT_START	0x0c000000

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

#define BAD_PAGETABLE		__bad_pagetable()
#define BAD_PAGE		__bad_page()
#define ZERO_PAGE(vaddr)	((unsigned long) empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BYTES_PER_PTR		(sizeof(unsigned long))
#define BITS_PER_PTR		(8*BYTES_PER_PTR)

/* to align the pointer to a pointer address */
#define PTR_MASK		(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
#define SIZEOF_PTR_LOG2		2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
	((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

extern void __bad_pmd(pmd_t *pmd);
extern void __bad_pmd_kernel(pmd_t *pmd);

/*
 * Page table cache stuff
 */
#ifndef CONFIG_NO_PGT_CACHE

#ifndef __SMP__
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist		(quicklists.pgd_cache)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		(quicklists.pte_cache)
#define pgtable_cache_size	(quicklists.pgtable_cache_sz)

#else	/* __SMP__ */
#error Pgtable caches have to be per-CPU, so that no locking is needed.
#endif	/* __SMP__ */

/* used for quicklists */
#define __pgd_next(pgd) (((unsigned long *)pgd)[1])
#define __pte_next(pte)	(((unsigned long *)pte)[0])

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)__pgd_next(ret);
		ret[1] = ret[2];
		clean_cache_area(ret + 1, 4);
		pgtable_cache_size--;
	}
	return (pgd_t *)ret;
}

/* We don't use pmd cache, so this is a dummy routine */
extern __inline__ pmd_t *get_pmd_fast(void)
{
	return (pmd_t *)0;
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

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	__pgd_next(pgd) = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	__pte_next(pte) = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

#else	/* CONFIG_NO_PGT_CACHE */

#define get_pgd_fast()		(NULL)
#define get_pmd_fast()		(NULL)
#define get_pte_fast()		(NULL)

#define free_pgd_fast(pgd)	free_pgd_slow(pgd)
#define free_pmd_fast(pmd)	free_pmd_slow(pmd)
#define free_pte_fast(pte)	free_pte_slow(pte)

#endif	/* CONFIG_NO_PGT_CACHE */

#include <asm/proc/pgtable.h>

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

#define SWP_TYPE(entry)		(((entry) >> 2) & 0x7f)
#define SWP_OFFSET(entry)	((entry) >> 9)
#define SWP_ENTRY(type,offset)	(((type) << 2) | ((offset) << 9))

#define module_map		vmalloc
#define module_unmap		vfree

extern int do_check_pgt_cache(int, int);

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

#endif /* _ASMARM_PGTABLE_H */
