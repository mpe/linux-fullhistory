#ifndef _M68K_PGTABLE_H
#define _M68K_PGTABLE_H

#ifndef __ASSEMBLY__

/*
 * This file contains the functions and defines necessary to modify and use
 * the m68k page table tree.
 */

#define __flush_tlb() \
do { 	\
	if (m68k_is040or060) \
		__asm__ __volatile__(".word 0xf510\n"::); /* pflushan */ \
	else \
		__asm__ __volatile__("pflusha\n"::); \
} while (0)

static inline void __flush_tlb_one(unsigned long addr)
{
	if (m68k_is040or060) {
		register unsigned long a0 __asm__ ("a0") = addr;
		__asm__ __volatile__(".word 0xf508" /* pflush (%a0) */
				     : : "a" (a0));
	} else
		__asm__ __volatile__("pflush #0,#0,(%0)" : : "a" (addr));
}

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() flush_tlb()

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

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	25
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the m68k is configured as three-level,
 * so we do have PMD level physically.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	8
#define PTRS_PER_PGD	128

/* the no. of pointers that fit on a page: this will go away */
#define PTRS_PER_PAGE	(PAGE_SIZE/sizeof(void*))

typedef pgd_t pgd_table[PTRS_PER_PGD];
typedef pmd_t pmd_table[PTRS_PER_PMD];
typedef pte_t pte_table[PTRS_PER_PTE];

#define PGD_TABLES_PER_PAGE (PAGE_SIZE/sizeof(pgd_table))
#define PMD_TABLES_PER_PAGE (PAGE_SIZE/sizeof(pmd_table))
#define PTE_TABLES_PER_PAGE (PAGE_SIZE/sizeof(pte_table))

typedef pgd_table pgd_tablepage[PGD_TABLES_PER_PAGE];
typedef pmd_table pmd_tablepage[PMD_TABLES_PER_PAGE];
typedef pte_table pte_tablepage[PTE_TABLES_PER_PAGE];

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	(8*1024*1024)
#define VMALLOC_START ((high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

#endif /* __ASSEMBLY__ */

/*
 * Definitions for MMU descriptors
 */
#define _PAGE_PRESENT	0x001
#define _PAGE_SHORT	0x002
#define _PAGE_RONLY	0x004
#define _PAGE_ACCESSED	0x008
#define _PAGE_DIRTY	0x010
#define _PAGE_GLOBAL040	0x400	/* 68040 global bit, used for kva descs */
#define _PAGE_COW	0x800	/* implemented in software */
#define _PAGE_NOCACHE030 0x040	/* 68030 no-cache mode */
#define _PAGE_NOCACHE	0x060	/* 68040 cache mode, non-serialized */
#define _PAGE_NOCACHE_S	0x040	/* 68040 no-cache mode, serialized */
#define _PAGE_CACHE040	0x020	/* 68040 cache mode, cachable, copyback */
#define _PAGE_CACHE040W	0x000	/* 68040 cache mode, cachable, write-through */

#define _DESCTYPE_MASK	0x003

#define _CACHEMASK040	(~0x060)
#define _TABLE_MASK	(0xfffffff0)

#define _PAGE_TABLE	(_PAGE_SHORT)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_NOCACHE)

#ifndef __ASSEMBLY__

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | _PAGE_CACHE040)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_CACHE040)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | _PAGE_CACHE040)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | _PAGE_CACHE040)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_CACHE040)

/*
 * The m68k can't do page protection for execute, and considers that the same are read.
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

/* zero page used for uninitialized stuff */
extern unsigned long empty_zero_page;

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
#define ZERO_PAGE empty_zero_page

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

extern unsigned long high_memory;

/* For virtual address to physical address conversion */
extern unsigned long mm_vtop(unsigned long addr) __attribute__ ((const));
extern unsigned long mm_ptov(unsigned long addr) __attribute__ ((const));
#define VTOP(addr)  (mm_vtop((unsigned long)(addr)))
#define PTOV(addr)  (mm_ptov((unsigned long)(addr)))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = VTOP(page) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	int i;

	ptep = (pte_t *) VTOP(ptep);
	for (i = 0; i < 16; i++, ptep += PTRS_PER_PTE/16)
		pmdp->pmd[i] = _PAGE_TABLE | (unsigned long)ptep;
}

/* early termination version of the above */
extern inline void pmd_set_et(pmd_t * pmdp, pte_t * ptep)
{
	int i;

	ptep = (pte_t *) VTOP(ptep);
	for (i = 0; i < 16; i++, ptep += PTRS_PER_PTE/16)
		pmdp->pmd[i] = _PAGE_PRESENT | (unsigned long)ptep;
}

extern inline void pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{ pgd_val(*pgdp) = _PAGE_TABLE | VTOP(pmdp); }

extern inline unsigned long pte_page(pte_t pte)
{ return PTOV(pte_val(pte) & PAGE_MASK); }

extern inline unsigned long pmd_page2(pmd_t *pmd)
{ return PTOV(pmd_val(*pmd) & _TABLE_MASK); }
#define pmd_page(pmd) pmd_page2(&(pmd))

extern inline unsigned long pgd_page(pgd_t pgd)
{ return PTOV(pgd_val(pgd) & _TABLE_MASK); }

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

extern inline int pmd_none2(pmd_t *pmd)		{ return !pmd_val(*pmd); }
#define pmd_none(pmd) pmd_none2(&(pmd))
extern inline int pmd_bad2(pmd_t *pmd)		{ return (pmd_val(*pmd) & _DESCTYPE_MASK) != _PAGE_TABLE || pmd_page(*pmd) > high_memory; }
#define pmd_bad(pmd) pmd_bad2(&(pmd))
extern inline int pmd_present2(pmd_t *pmd)	{ return pmd_val(*pmd) & _PAGE_TABLE; }
#define pmd_present(pmd) pmd_present2(&(pmd))
extern inline void pmd_clear(pmd_t * pmdp)
{
	short i;

	for (i = 15; i >= 0; i--)
		pmdp->pmd[i] = 0;
}

extern inline int pgd_none(pgd_t pgd)		{ return !pgd_val(pgd); }
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & _DESCTYPE_MASK) != _PAGE_TABLE || pgd_page(pgd) > high_memory; }
extern inline int pgd_present(pgd_t pgd)	{ return pgd_val(pgd) & _PAGE_TABLE; }

extern inline void pgd_clear(pgd_t * pgdp)	{ pgd_val(*pgdp) = 0; }

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return 1; }
extern inline int pte_write(pte_t pte)		{ return !(pte_val(pte) & _PAGE_RONLY); }
extern inline int pte_exec(pte_t pte)		{ return 1; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

extern inline pte_t pte_wrprotect(pte_t pte)	{ pte_val(pte) |= _PAGE_RONLY; return pte; }
extern inline pte_t pte_rdprotect(pte_t pte)	{ return pte; }
extern inline pte_t pte_exprotect(pte_t pte)	{ return pte; }
extern inline pte_t pte_mkclean(pte_t pte)	{ pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkold(pte_t pte)	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)	{ pte_val(pte) &= ~_PAGE_RONLY; return pte; }
extern inline pte_t pte_mkread(pte_t pte)	{ return pte; }
extern inline pte_t pte_mkexec(pte_t pte)	{ return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mknocache(pte_t pte)
{
	pte_val(pte) = (pte_val(pte) & _CACHEMASK040) | m68k_pgtable_cachemode;
	return pte;
}
extern inline pte_t pte_mkcache(pte_t pte)	{ pte_val(pte) = (pte_val(pte) & _CACHEMASK040) | _PAGE_CACHE040; return pte; }

/* to set the page-dir */
extern inline void SET_PAGE_DIR(struct task_struct * tsk, pgd_t * pgdir)
{
	tsk->tss.pagedir_v = (unsigned long *)pgdir;
	tsk->tss.pagedir_p = VTOP(pgdir);
	tsk->tss.crp[0] = 0x80000000 | _PAGE_SHORT;
	tsk->tss.crp[1] = tsk->tss.pagedir_p;
	if (tsk == current) {
		if (m68k_is040or060)
			__asm__ __volatile__ (".word 0xf510\n\t" /* pflushan */
					      "movel %0@,%/d0\n\t"
					      ".long 0x4e7b0806\n\t"
					      /* movec d0,urp */
					      : : "a" (&tsk->tss.crp[1])
					      : "d0");
		else
			__asm__ __volatile__ ("movec  %/cacr,%/d0\n\t"
					      "oriw #0x0808,%/d0\n\t"
					      "movec %/d0,%/cacr\n\t"
					      "pmove %0@,%/crp\n\t"
					      : : "a" (&tsk->tss.crp[0])
					      : "d0");
	}
}

#define PAGE_DIR_OFFSET(tsk,address) pgd_offset((tsk),(address))

/* to find an entry in a page-table-directory */
extern inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> PGDIR_SHIFT);
}

extern pgd_t swapper_pg_dir[128];
extern pgd_t kernel_pg_dir[128];

extern inline pgd_t * pgd_offset_k(unsigned long address)
{
	return kernel_pg_dir + (address >> PGDIR_SHIFT);
}


/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) pgd_page(*dir) + ((address >> PMD_SHIFT) & (PTRS_PER_PMD-1));
}

/* Find an entry in the third-level page table.. */ 
extern inline pte_t * pte_offset(pmd_t * pmdp, unsigned long address)
{
	return (pte_t *) pmd_page(*pmdp) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

extern inline void nocache_page (unsigned long vaddr)
{
	if (m68k_is040or060) {
		pgd_t *dir;
		pmd_t *pmdp;
		pte_t *ptep;

		dir = pgd_offset_k(vaddr);
		pmdp = pmd_offset(dir,vaddr);
		ptep = pte_offset(pmdp,vaddr);
		*ptep = pte_mknocache(*ptep);
	}
}

static inline void cache_page (unsigned long vaddr)
{
	if (m68k_is040or060) {
		pgd_t *dir;
		pmd_t *pmdp;
		pte_t *ptep;

		dir = pgd_offset_k(vaddr);
		pmdp = pmd_offset(dir,vaddr);
		ptep = pte_offset(pmdp,vaddr);
		*ptep = pte_mkcache(*ptep);
	}
}


extern const char PgtabStr_bad_pmd[];
extern const char PgtabStr_bad_pgd[];
extern const char PgtabStr_bad_pmdk[];
extern const char PgtabStr_bad_pgdk[];

extern inline void pte_free(pte_t * pte)
{
	cache_page((unsigned long)pte);
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *)get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				nocache_page((unsigned long)page);
				pmd_set(pmd,page);
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long)page);
	}
	if (pmd_bad(*pmd)) {
		printk(PgtabStr_bad_pmd, pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern pmd_t *get_pointer_table (void);
extern void free_pointer_table (pmd_t *);
extern pmd_t *get_kpointer_table (void);
extern void free_kpointer_table (pmd_t *);

extern inline void pmd_free(pmd_t * pmd)
{
	free_pointer_table (pmd);
}

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_pointer_table();
		if (pgd_none(*pgd)) {
			if (page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
			return NULL;
		}
		free_pointer_table(page);
	}
	if (pgd_bad(*pgd)) {
		printk(PgtabStr_bad_pgd, pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

extern inline void pte_free_kernel(pte_t * pte)
{
	cache_page((unsigned long)pte);
	free_page((unsigned long) pte);
}

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
		if (pmd_none(*pmd)) {
			if (page) {
				nocache_page((unsigned long)page);
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk(PgtabStr_bad_pmdk, pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline void pmd_free_kernel(pmd_t * pmd)
{
	free_kpointer_table(pmd);
}

extern inline pmd_t * pmd_alloc_kernel(pgd_t * pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_kpointer_table();
		if (pgd_none(*pgd)) {
			if (page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
			return NULL;
		}
		free_kpointer_table(page);
	}
	if (pgd_bad(*pgd)) {
		printk(PgtabStr_bad_pgdk, pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

extern inline void pgd_free(pgd_t * pgd)
{
	free_pointer_table ((pmd_t *) pgd);
}

extern inline pgd_t * pgd_alloc(void)
{
	return (pgd_t *)get_pointer_table ();
}

#define flush_icache() \
do { \
	if (m68k_is040or060) \
		asm ("nop; .word 0xf498 /* cinva %%ic */"); \
	else \
		asm ("movec %/cacr,%/d0;" \
		     "oriw %0,%/d0;" \
		     "movec %/d0,%/cacr" \
		     : /* no outputs */ \
		     : "i" (FLUSH_I) \
		     : "d0"); \
} while (0)

/*
 * invalidate the cache for the specified memory range.
 * It starts at the physical address specified for
 * the given number of bytes.
 */
extern void cache_clear (unsigned long paddr, int len);
/*
 * push any dirty cache in the specified memory range.
 * It starts at the physical address specified for
 * the given number of bytes.
 */
extern void cache_push (unsigned long paddr, int len);

/*
 * push and invalidate pages in the specified user virtual
 * memory range.
 */
extern void cache_push_v (unsigned long vaddr, int len);

/* cache code */
#define FLUSH_I_AND_D	(0x00000808)
#define FLUSH_I 	(0x00000008)

/* This is needed whenever the virtual mapping of the current
   process changes.  */
#define __flush_cache_all()						\
    do {								\
	if (m68k_is040or060)					        \
               __asm__ __volatile__ ("nop; .word 0xf478\n" ::);         \
        else                                                            \
	       __asm__ __volatile__ ("movec %%cacr,%%d0\n\t"		\
				     "orw %0,%%d0\n\t"			\
				     "movec %%d0,%%cacr"		\
				     : : "di" (FLUSH_I_AND_D) : "d0");	\
    } while (0)

#define __flush_cache_030()						\
    do {								\
	if (m68k_is040or060 == 0)					\
	       __asm__ __volatile__ ("movec %%cacr,%%d0\n\t"		\
				     "orw %0,%%d0\n\t"			\
				     "movec %%d0,%%cacr"		\
				     : : "di" (FLUSH_I_AND_D) : "d0");	\
    } while (0)

#define flush_cache_all() __flush_cache_all()

extern inline void flush_cache_mm(struct mm_struct *mm)
{
	if (mm == current->mm) __flush_cache_all();
}

extern inline void flush_cache_range(struct mm_struct *mm,
				     unsigned long start,
				     unsigned long end)
{
	if (mm == current->mm){
	    if (m68k_is040or060)
	        cache_push_v(start, end-start);
	    else
	        __flush_cache_030();
	}
}

extern inline void flush_cache_page(struct vm_area_struct *vma,
				    unsigned long vmaddr)
{
	if (vma->vm_mm == current->mm){
	    if (m68k_is040or060)
	        cache_push_v(vmaddr, PAGE_SIZE);
	    else
	        __flush_cache_030();
	}
}

/* Push the page at kernel virtual address and clear the icache */
extern inline void flush_page_to_ram (unsigned long address)
{
    if (m68k_is040or060) {
	register unsigned long tmp __asm ("a0") = VTOP(address);
	__asm__ __volatile__ ("nop\n\t"
			      ".word 0xf470 /* cpushp %%dc,(%0) */\n\t"
			      ".word 0xf490 /* cinvp %%ic,(%0) */"
			      : : "a" (tmp));
    }
    else
	__asm volatile ("movec %%cacr,%%d0\n\t"
			"orw %0,%%d0\n\t"
			"movec %%d0,%%cacr"
			: : "di" (FLUSH_I) : "d0");
}

/* Push n pages at kernel virtual address and clear the icache */
extern inline void flush_pages_to_ram (unsigned long address, int n)
{
    if (m68k_is040or060) {
	while (n--) {
	    register unsigned long tmp __asm ("a0") = VTOP(address);
	    __asm__ __volatile__ ("nop\n\t"
				  ".word 0xf470 /* cpushp %%dc,(%0) */\n\t"
				  ".word 0xf490 /* cinvp %%ic,(%0) */"
				  : : "a" (tmp));
	    address += PAGE_SIZE;
	}
    }
    else
	__asm volatile ("movec %%cacr,%%d0\n\t"
			"orw %0,%%d0\n\t"
			"movec %%d0,%%cacr"
			: : "di" (FLUSH_I) : "d0");
}

/*
 * Check if the addr/len goes up to the end of a physical
 * memory chunk.  Used for DMA functions.
 */
int mm_end_of_chunk (unsigned long addr, int len);

/*
 * Map some physical address range into the kernel address space. The
 * code is copied and adapted from map_chunk().
 */
extern unsigned long kernel_map(unsigned long paddr, unsigned long size,
				int nocacheflag, unsigned long *memavailp );
/*
 * Change the cache mode of some kernel address range.
 */
extern void kernel_set_cachemode( unsigned long address, unsigned long size,
				  unsigned cmode );

/* Values for nocacheflag and cmode */
#define	KERNELMAP_FULL_CACHING		0
#define	KERNELMAP_NOCACHE_SER		1
#define	KERNELMAP_NOCACHE_NONSER	2
#define	KERNELMAP_NO_COPYBACK		3

/*
 * The m68k doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
extern inline void update_mmu_cache(struct vm_area_struct * vma,
	unsigned long address, pte_t pte)
{
}

/*
 * I don't know what is going on here, but since these were changed,
 * swapping hasn't been working on the 68040.
 */

#define SWP_TYPE(entry)  (((entry) >> 2) & 0x7f)
#if 0
#define SWP_OFFSET(entry) ((entry) >> 9)
#define SWP_ENTRY(type,offset) (((type) << 2) | ((offset) << 9))
#else
#define SWP_OFFSET(entry) ((entry) >> PAGE_SHIFT)
#define SWP_ENTRY(type,offset) (((type) << 2) | ((offset) << PAGE_SHIFT))
#endif

#endif /* __ASSEMBLY__ */

#endif /* _M68K_PGTABLE_H */
