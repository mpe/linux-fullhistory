#ifndef _ALPHA_PGTABLE_H
#define _ALPHA_PGTABLE_H

/*
 * This file contains the functions and defines necessary to modify and use
 * the alpha page table tree.
 *
 * This hopefully works with any standard alpha page-size, as defined
 * in <asm/page.h> (currently 8192).
 */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + 2*(PAGE_SHIFT-3))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the alpha is three-level, with
 * all levels having a one-page page table.
 *
 * The PGD is special: the last entry is reserved for self-mapping.
 */
#define PTRS_PER_PTE	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PMD	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PGD	((1UL << (PAGE_SHIFT-3))-1)

/* the no. of pointers that fit on a page: this will go away */
#define PTRS_PER_PAGE	(1UL << (PAGE_SHIFT-3))

#define VMALLOC_START		0xFFFFFE0000000000
#define VMALLOC_VMADDR(x)	((unsigned long)(x))

/*
 * OSF/1 PAL-code-imposed page table bits
 */
#define _PAGE_VALID	0x0001
#define _PAGE_FOR	0x0002	/* used for page protection (fault on read) */
#define _PAGE_FOW	0x0004	/* used for page protection (fault on write) */
#define _PAGE_FOE	0x0008	/* used for page protection (fault on exec) */
#define _PAGE_ASM	0x0010
#define _PAGE_KRE	0x0100	/* xxx - see below on the "accessed" bit */
#define _PAGE_URE	0x0200	/* xxx */
#define _PAGE_KWE	0x1000	/* used to do the dirty bit in software */
#define _PAGE_UWE	0x2000	/* used to do the dirty bit in software */

/* .. and these are ours ... */
#define _PAGE_COW	0x10000
#define _PAGE_DIRTY	0x20000
#define _PAGE_ACCESSED	0x40000

/*
 * NOTE! The "accessed" bit isn't necessarily exact: it can be kept exactly
 * by software (use the KRE/URE/KWE/UWE bits appropriately), but I'll fake it.
 * Under Linux/AXP, the "accessed" bit just means "read", and I'll just use
 * the KRE/URE bits to watch for it. That way we don't need to overload the
 * KWE/UWE bits with both handling dirty and accessed.
 *
 * Note that the kernel uses the accessed bit just to check whether to page
 * out a page or not, so it doesn't have to be exact anyway.
 */

#define __DIRTY_BITS	(_PAGE_DIRTY | _PAGE_KWE | _PAGE_UWE)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_KRE | _PAGE_URE)

#define _PFN_MASK	0xFFFFFFFF00000000

#define _PAGE_TABLE	(_PAGE_VALID | __DIRTY_BITS | __ACCESS_BITS)
#define _PAGE_CHG_MASK	(_PFN_MASK | __DIRTY_BITS | __ACCESS_BITS)

/*
 * All the normal masks have the "page accessed" bits on, as any time they are used,
 * the page is accessed. They are cleared only by the page-out routines
 */
#define PAGE_NONE	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOR | _PAGE_FOW | _PAGE_FOE)
#define PAGE_SHARED	__pgprot(_PAGE_VALID | __ACCESS_BITS)
#define PAGE_COPY	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOW | _PAGE_COW)
#define PAGE_READONLY	__pgprot(_PAGE_VALID | __ACCESS_BITS | _PAGE_FOW)
#define PAGE_KERNEL	__pgprot(_PAGE_VALID | _PAGE_ASM | _PAGE_KRE | _PAGE_KWE)

#define _PAGE_NORMAL(x) __pgprot(_PAGE_VALID | __ACCESS_BITS | (x))

#define __P000	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR | _PAGE_FOW | _PAGE_FOE)
#define __P001	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOW | _PAGE_FOE)
#define __P010	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR | _PAGE_FOE)
#define __P011	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOE)
#define __P100	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR | _PAGE_FOW)
#define __P101	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOW)
#define __P110	_PAGE_NORMAL(_PAGE_COW | _PAGE_FOR)
#define __P111	_PAGE_NORMAL(_PAGE_COW)

#define __S000	_PAGE_NORMAL(_PAGE_FOR | _PAGE_FOW | _PAGE_FOE)
#define __S001	_PAGE_NORMAL(_PAGE_FOW | _PAGE_FOE)
#define __S010	_PAGE_NORMAL(_PAGE_FOR | _PAGE_FOE)
#define __S011	_PAGE_NORMAL(_PAGE_FOE)
#define __S100	_PAGE_NORMAL(_PAGE_FOR | _PAGE_FOW)
#define __S101	_PAGE_NORMAL(_PAGE_FOW)
#define __S110	_PAGE_NORMAL(_PAGE_FOR)
#define __S111	_PAGE_NORMAL(0)

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pmd_t * __bad_pagetable(void);

extern unsigned long __zero_page(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
#define SIZEOF_PTR_LOG2			3

/* to find an entry in a page-table */
#define PAGE_PTR(address)		\
  ((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

extern unsigned long high_memory;

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = ((page-PAGE_OFFSET) << (32-PAGE_SHIFT)) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{ pmd_val(*pmdp) = _PAGE_TABLE | ((((unsigned long) ptep) - PAGE_OFFSET) << (32-PAGE_SHIFT)); }

extern inline void pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{ pgd_val(*pgdp) = _PAGE_TABLE | ((((unsigned long) pmdp) - PAGE_OFFSET) << (32-PAGE_SHIFT)); }

extern inline unsigned long pte_page(pte_t pte)
{ return PAGE_OFFSET + ((pte_val(pte) & _PFN_MASK) >> (32-PAGE_SHIFT)); }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return PAGE_OFFSET + ((pmd_val(pmd) & _PFN_MASK) >> (32-PAGE_SHIFT)); }

extern inline unsigned long pgd_page(pgd_t pgd)
{ return PAGE_OFFSET + ((pgd_val(pgd) & _PFN_MASK) >> (32-PAGE_SHIFT)); }

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_VALID; }
extern inline int pte_inuse(pte_t *ptep)	{ return mem_map[MAP_NR(ptep)] != 1; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }
extern inline void pte_reuse(pte_t * ptep)
{
	if (!(mem_map[MAP_NR(ptep)] & MAP_PAGE_RESERVED))
		mem_map[MAP_NR(ptep)]++;
}

extern inline int pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
extern inline int pmd_bad(pmd_t pmd)		{ return (pmd_val(pmd) & ~_PFN_MASK) != _PAGE_TABLE || pmd_page(pmd) > high_memory; }
extern inline int pmd_present(pmd_t pmd)	{ return pmd_val(pmd) & _PAGE_VALID; }
extern inline int pmd_inuse(pmd_t *pmdp)	{ return mem_map[MAP_NR(pmdp)] != 1; }
extern inline void pmd_clear(pmd_t * pmdp)	{ pmd_val(*pmdp) = 0; }
extern inline void pmd_reuse(pmd_t * pmdp)
{
	if (!(mem_map[MAP_NR(pmdp)] & MAP_PAGE_RESERVED))
		mem_map[MAP_NR(pmdp)]++;
}

extern inline int pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & ~_PFN_MASK) != _PAGE_TABLE || pgd_page(pgd) > high_memory; }
extern inline int pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & _PAGE_VALID; }
extern inline int pgd_inuse(pgd_t *pgdp)	{ return mem_map[MAP_NR(pgdp)] != 1; }
extern inline void pgd_clear(pgd_t * pgdp)	{ pgd_val(*pgdp) = 0; }
extern inline void pgd_reuse(pgd_t * pgdp)
{
	if (!(mem_map[MAP_NR(pgdp)] & MAP_PAGE_RESERVED))
		mem_map[MAP_NR(pgdp)]++;
}

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOR); }
extern inline int pte_write(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOW); }
extern inline int pte_exec(pte_t pte)		{ return !(pte_val(pte) & _PAGE_FOE); }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }
extern inline int pte_cow(pte_t pte)		{ return pte_val(pte) & _PAGE_COW; }

extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOW; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOR; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_FOE; return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~(__DIRTY_BITS); return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~(__ACCESS_BITS); return pte; }
extern inline pte_t pte_uncow(pte_t pte)	{ pte_val(pte) &= ~_PAGE_COW; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) &= _PAGE_FOW; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ pte_val(pte) &= _PAGE_FOR; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ pte_val(pte) &= _PAGE_FOE; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= __DIRTY_BITS; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= __ACCESS_BITS; return pte; }
extern inline pte_t pte_mkcow(pte_t pte)	{ pte_val(pte) |= _PAGE_COW; return pte; }

/* to set the page-dir. Note the self-mapping in the last entry */
extern inline void SET_PAGE_DIR(struct task_struct * tsk, pgd_t * pgdir)
{
	pgd_val(pgdir[PTRS_PER_PGD]) = pte_val(mk_pte((unsigned long) pgdir, PAGE_KERNEL));
	tsk->tss.ptbr = ((unsigned long) pgdir - PAGE_OFFSET) >> PAGE_SHIFT;
	if (tsk == current)
		invalidate();
}

#define PAGE_DIR_OFFSET(tsk,address) pgd_offset((tsk),(address))

/* to find an entry in a page-table-directory. */
extern inline pgd_t * pgd_offset(struct task_struct * tsk, unsigned long address)
{
	return (pgd_t *) ((tsk->tss.ptbr << PAGE_SHIFT) + PAGE_OFFSET) +
		((address >> PGDIR_SHIFT) & (PTRS_PER_PAGE - 1));
}

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) pgd_page(*dir) + ((address >> PMD_SHIFT) & (PTRS_PER_PAGE - 1));
}

/* Find an entry in the third-level page table.. */
extern inline pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PAGE - 1));
}

/*              
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
extern inline void pte_free_kernel(pte_t * pte)
{
	mem_map[MAP_NR(pte)] = 1;
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_set(pmd, page);
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline void pmd_free_kernel(pmd_t * pmd)
{
	mem_map[MAP_NR(pmd)] = 1;
	free_page((unsigned long) pmd);
}

extern inline pmd_t * pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = (pmd_t *) get_free_page(GFP_KERNEL);
		if (pgd_none(*pgd)) {
			if (page) {
				pgd_set(pgd, page);
				mem_map[MAP_NR(page)] = MAP_PAGE_RESERVED;
				return page + address;
			}
			pgd_set(pgd, BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

extern inline void pte_free(pte_t * pte)
{
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline void pmd_free(pmd_t * pmd)
{
	free_page((unsigned long) pmd);
}

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = (pmd_t *) get_free_page(GFP_KERNEL);
		if (pgd_none(*pgd)) {
			if (page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

extern inline void pgd_free(pgd_t * pgd)
{
	free_page((unsigned long) pgd);
}

extern inline pgd_t * pgd_alloc(void)
{
	return (pgd_t *) get_free_page(GFP_KERNEL);
}

extern pgd_t swapper_pg_dir[1024];

/*
 * The alpha doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
}

#endif /* _ALPHA_PGTABLE_H */
