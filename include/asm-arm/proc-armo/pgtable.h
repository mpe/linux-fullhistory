/*
 * linux/include/asm-arm/proc-armo/pgtable.h
 *
 * Copyright (C) 1995, 1996 Russell King
 * Modified 18/19-Oct-1997 for two-level page table
 */
#ifndef __ASM_PROC_PGTABLE_H
#define __ASM_PROC_PGTABLE_H

#include <asm/arch/mmu.h>
#include <linux/slab.h>
#include <asm/arch/processor.h>		/* For TASK_SIZE */

#define LIBRARY_TEXT_START 0x0c000000

/*
 * Cache flushing...
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(mm,start,end)		do { } while (0)
#define flush_cache_page(vma,vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)
#define flush_icache_range(start,end)		do { } while (0)

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 */
#define flush_tlb()			do { } while (0)
#define flush_tlb_all()			do { } while (0)
#define flush_tlb_mm(mm)		do { } while (0)
#define flush_tlb_range(mm, start, end) do { } while (0)
#define flush_tlb_page(vma, vmaddr)	do { } while (0)

/*
 * We have a mem map cache...
 */
extern __inline__ void update_memc_all(void)
{
	struct task_struct *p;

	p = &init_task;
	do {
		processor.u.armv2._update_map(p);
		p = p->next_task;
	} while (p != &init_task);

	processor.u.armv2._remap_memc (current);
}

extern __inline__ void update_memc_task(struct task_struct *tsk)
{
	processor.u.armv2._update_map(tsk);

	if (tsk == current)
		processor.u.armv2._remap_memc (tsk);
}

extern __inline__ void update_memc_mm(struct mm_struct *mm)
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

extern __inline__ void update_memc_addr(struct mm_struct *mm, unsigned long addr, pte_t pte)
{
	struct task_struct *p;

	p = &init_task;
	do {
		if (p->mm == mm)
			processor.u.armv2._update_mmu_cache(p, addr, pte);
		p = p->next_task;
	} while (p != &init_task);

	if (current->mm == mm)
		processor.u.armv2._remap_memc (current);
}

#define __flush_entry_to_ram(entry)

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT       20
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT     20
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the arm3 is one-level, so
 * we don't really have any PMD or PTE directory physically.
 *
 * 18-Oct-1997 RMK Now two-level (32x32)
 */
#define PTRS_PER_PTE    32
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    32
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

#define VMALLOC_START	0x01a00000
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	0x01c00000

#define _PAGE_PRESENT		0x01
#define _PAGE_READONLY		0x02
#define _PAGE_NOT_USER		0x04
#define _PAGE_OLD		0x08
#define _PAGE_CLEAN		0x10

#define _PAGE_TABLE     (_PAGE_PRESENT)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_OLD | _PAGE_CLEAN)

/*                               -- present --   -- !dirty --  --- !write ---   ---- !user --- */
#define PAGE_NONE       __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY | _PAGE_NOT_USER)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_CLEAN                                  )
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY                 )
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_CLEAN | _PAGE_READONLY                 )
#define PAGE_KERNEL     __pgprot(_PAGE_PRESENT                                | _PAGE_NOT_USER)

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

extern unsigned long *empty_zero_page;

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t *__bad_pagetable(void);

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
#define SET_PAGE_DIR(tsk,pgdir)						\
do {									\
	tsk->tss.memmap = (unsigned long)pgdir;				\
	processor.u.armv2._update_map(tsk);				\
	if ((tsk) == current)						\
		processor.u.armv2._remap_memc (current);		\
} while (0)

extern unsigned long physical_start;
extern unsigned long physical_end;

#define pte_none(pte)		(!pte_val(pte))
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)
#define pte_clear(ptep)		set_pte((ptep), __pte(0))

#define pmd_none(pmd)		(!pmd_val(pmd))
#define pmd_bad(pmd)		((pmd_val(pmd) & 0xfc000002))
#define pmd_present(pmd)	(pmd_val(pmd) & _PAGE_PRESENT)
#define pmd_clear(pmdp)		set_pmd(pmdp, __pmd(0))

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
extern inline int pte_read(pte_t pte)           { return !(pte_val(pte) & _PAGE_NOT_USER);     }
extern inline int pte_write(pte_t pte)          { return !(pte_val(pte) & _PAGE_READONLY);     }
extern inline int pte_exec(pte_t pte)           { return !(pte_val(pte) & _PAGE_NOT_USER);     }
extern inline int pte_dirty(pte_t pte)          { return !(pte_val(pte) & _PAGE_CLEAN);        }
extern inline int pte_young(pte_t pte)          { return !(pte_val(pte) & _PAGE_OLD);          }
#define pte_cacheable(pte) 1

extern inline pte_t pte_nocache(pte_t pte)	{ return pte; }
extern inline pte_t pte_wrprotect(pte_t pte)    { pte_val(pte) |= _PAGE_READONLY;  return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)    { pte_val(pte) |= _PAGE_NOT_USER;  return pte; }
extern inline pte_t pte_exprotect(pte_t pte)    { pte_val(pte) |= _PAGE_NOT_USER;  return pte; }
extern inline pte_t pte_mkclean(pte_t pte)      { pte_val(pte) |= _PAGE_CLEAN;     return pte; }
extern inline pte_t pte_mkold(pte_t pte)        { pte_val(pte) |= _PAGE_OLD;       return pte; }

extern inline pte_t pte_mkwrite(pte_t pte)      { pte_val(pte) &= ~_PAGE_READONLY; return pte; }
extern inline pte_t pte_mkread(pte_t pte)       { pte_val(pte) &= ~_PAGE_NOT_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)       { pte_val(pte) &= ~_PAGE_NOT_USER; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)      { pte_val(pte) &= ~_PAGE_CLEAN;    return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)      { pte_val(pte) &= ~_PAGE_OLD;      return pte; }

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

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

extern __inline__ unsigned long pte_page(pte_t pte)
{
	return __phys_to_virt(pte_val(pte) & PAGE_MASK);
}

extern __inline__ pmd_t mk_pmd (pte_t *ptep)
{
	pmd_t pmd;
	pmd_val(pmd) = __virt_to_phys((unsigned long)ptep) | _PAGE_TABLE;
	return pmd;
}

#define set_pmd(pmdp,pmd) ((*(pmdp)) = (pmd))

extern __inline__ unsigned long pmd_page(pmd_t pmd)
{
	return __phys_to_virt(pmd_val(pmd) & ~_PAGE_TABLE);
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
extern __inline__ pte_t * pte_offset(pmd_t *dir, unsigned long address)
{
	return (pte_t *)pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

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

#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (quicklists.pte_cache)
#define pgd_quicklist (quicklists.pgd_cache)
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
	kfree(pgd);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);

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
	kfree(pte);
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

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)
#define pgd_free(pgd)           free_pgd_fast(pgd)
#define pgd_alloc()             get_pgd_fast()

extern __inline__ pte_t *pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none (*pmd)) {
		pte_t *page = (pte_t *) get_pte_fast();

		if (!page)
			return get_pte_slow(pmd, address);
		set_pmd(pmd, mk_pmd(page));
		return page + address;
	}
	if (pmd_bad (*pmd)) {
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

#define pmd_free_kernel         pmd_free
#define pmd_alloc_kernel        pmd_alloc
#define pte_alloc_kernel        pte_alloc

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

#define update_mmu_cache(vma,address,pte)

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) <<  8))

#endif /* __ASM_PROC_PAGE_H */

