#ifndef _M68K_PGTABLE_H
#define _M68K_PGTABLE_H

#include <linux/config.h>
#include <asm/setup.h>

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/tasks.h>

/*
 * This file contains the functions and defines necessary to modify and use
 * the m68k page table tree.
 */

#include <asm/virtconvert.h>

/*
 * Cache handling functions
 */

#define flush_icache()					\
do {							\
	if (CPU_IS_040_OR_060)				\
		asm __volatile__ ("nop\n\t"		\
				  ".chip 68040\n\t"	\
				  "cinva %%ic\n\t"	\
				  ".chip 68k" : );	\
	else {						\
		unsigned long _tmp;			\
		asm __volatile__ ("movec %%cacr,%0\n\t"	\
		     "orw %1,%0\n\t"			\
		     "movec %0,%%cacr"			\
		     : "=&d" (_tmp)			\
		     : "id" (FLUSH_I));			\
	}						\
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
	if (CPU_IS_040_OR_060)						\
		__asm__ __volatile__ ("nop\n\t"				\
				      ".chip 68040\n\t"			\
				      "cpusha %dc\n\t"			\
				      ".chip 68k");			\
	else {								\
		unsigned long _tmp;					\
		__asm__ __volatile__ ("movec %%cacr,%0\n\t"		\
				      "orw %1,%0\n\t"			\
				      "movec %0,%%cacr"			\
				      : "=&d" (_tmp)			\
				      : "di" (FLUSH_I_AND_D));		\
	}								\
    } while (0)

#define __flush_cache_030()						\
    do {								\
	if (CPU_IS_020_OR_030) {					\
		unsigned long _tmp;					\
		__asm__ __volatile__ ("movec %%cacr,%0\n\t"		\
				      "orw %1,%0\n\t"			\
				      "movec %0,%%cacr"			\
				      : "=&d" (_tmp)			\
				      : "di" (FLUSH_I_AND_D));		\
	}								\
    } while (0)

#define flush_cache_all() __flush_cache_all()

extern inline void flush_cache_mm(struct mm_struct *mm)
{
	if (mm == current->mm)
		__flush_cache_030();
}

extern inline void flush_cache_range(struct mm_struct *mm,
				     unsigned long start,
				     unsigned long end)
{
	if (mm == current->mm)
	        __flush_cache_030();
}

extern inline void flush_cache_page(struct vm_area_struct *vma,
				    unsigned long vmaddr)
{
	if (vma->vm_mm == current->mm)
	        __flush_cache_030();
}

/* Push the page at kernel virtual address and clear the icache */
extern inline void flush_page_to_ram (unsigned long address)
{
    if (CPU_IS_040_OR_060) {
	__asm__ __volatile__ ("nop\n\t"
			      ".chip 68040\n\t"
			      "cpushp %%dc,(%0)\n\t"
			      "cinvp %%ic,(%0)\n\t"
			      ".chip 68k"
			      : : "a" (virt_to_phys((void *)address)));
    }
    else {
	unsigned long _tmp;
	__asm volatile ("movec %%cacr,%0\n\t"
			"orw %1,%0\n\t"
			"movec %0,%%cacr"
			: "=&d" (_tmp)
			: "di" (FLUSH_I));
    }
}

/* Push n pages at kernel virtual address and clear the icache */
extern inline void flush_icache_range (unsigned long address,
				       unsigned long endaddr)
{
    if (CPU_IS_040_OR_060) {
	short n = (endaddr - address + PAGE_SIZE - 1) / PAGE_SIZE;

	while (n--) {
	    __asm__ __volatile__ ("nop\n\t"
				  ".chip 68040\n\t"
				  "cpushp %%dc,(%0)\n\t"
				  "cinvp %%ic,(%0)\n\t"
				  ".chip 68k"
				  : : "a" (virt_to_phys((void *)address)));
	    address += PAGE_SIZE;
	}
    }
    else {
	unsigned long _tmp;
	__asm volatile ("movec %%cacr,%0\n\t"
			"orw %1,%0\n\t"
			"movec %0,%%cacr"
			: "=&d" (_tmp)
			: "di" (FLUSH_I));
    }
}


/*
 * flush all user-space atc entries.
 */
static inline void __flush_tlb(void)
{
	if (CPU_IS_040_OR_060)
		__asm__ __volatile__(".chip 68040\n\t"
				     "pflushan\n\t"
				     ".chip 68k");
	else
		__asm__ __volatile__("pflush #0,#4");
}

static inline void __flush_tlb_one(unsigned long addr)
{
	if (CPU_IS_040_OR_060) {
		__asm__ __volatile__(".chip 68040\n\t"
				     "pflush (%0)\n\t"
				     ".chip 68k"
				     : : "a" (addr));
	} else
		__asm__ __volatile__("pflush #0,#4,(%0)" : : "a" (addr));
}

#define flush_tlb() __flush_tlb()

/*
 * flush all atc entries (both kernel and user-space entries).
 */
static inline void flush_tlb_all(void)
{
	if (CPU_IS_040_OR_060)
		__asm__ __volatile__(".chip 68040\n\t"
				     "pflusha\n\t"
				     ".chip 68k");
	else
		__asm__ __volatile__("pflusha");
}

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

extern inline void flush_tlb_kernel_page(unsigned long addr)
{
	if (CPU_IS_040_OR_060) {
		mm_segment_t old_fs = get_fs();
		set_fs(KERNEL_DS);
		__asm__ __volatile__(".chip 68040\n\t"
				     "pflush (%0)\n\t"
				     ".chip 68k"
				     : : "a" (addr));
		set_fs(old_fs);
	} else
		__asm__ __volatile__("pflush #4,#4,(%0)" : : "a" (addr));
}

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval)					\
	do{							\
		*(pteptr) = (pteval);				\
	} while(0)


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
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)

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

/* Virtual address region for use by kernel_map() */
#define	KMAP_START	0xd0000000
#define	KMAP_END	0xf0000000

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET	(8*1024*1024)
#define VMALLOC_START (((unsigned long) high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END KMAP_START

#endif /* __ASSEMBLY__ */

/*
 * Definitions for MMU descriptors
 */
#define _PAGE_PRESENT	0x001
#define _PAGE_SHORT	0x002
#define _PAGE_RONLY	0x004
#define _PAGE_ACCESSED	0x008
#define _PAGE_DIRTY	0x010
#define _PAGE_SUPER	0x080	/* 68040 supervisor only */
#define _PAGE_FAKE_SUPER 0x200	/* fake supervisor only on 680[23]0 */
#define _PAGE_GLOBAL040	0x400	/* 68040 global bit, used for kva descs */
#define _PAGE_COW	0x800	/* implemented in software */
#define _PAGE_NOCACHE030 0x040	/* 68030 no-cache mode */
#define _PAGE_NOCACHE	0x060	/* 68040 cache mode, non-serialized */
#define _PAGE_NOCACHE_S	0x040	/* 68040 no-cache mode, serialized */
#define _PAGE_CACHE040	0x020	/* 68040 cache mode, cachable, copyback */
#define _PAGE_CACHE040W	0x000	/* 68040 cache mode, cachable, write-through */

#define _DESCTYPE_MASK	0x003

#define _CACHEMASK040	(~0x060)
#define _TABLE_MASK	(0xfffffe00)

#define _PAGE_TABLE	(_PAGE_SHORT)
#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_NOCACHE)

#ifndef __ASSEMBLY__

/* This is the cache mode to be used for pages containing page descriptors for
 * processors >= '040. It is in pte_mknocache(), and the variable is defined
 * and initialized in head.S */
extern int m68k_pgtable_cachemode;

/* This is the cache mode for normal pages, for supervisor access on
 * processors >= '040. It is used in pte_mkcache(), and the variable is
 * defined and initialized in head.S */

#if defined(CONFIG_060_WRITETHROUGH)
extern int m68k_supervisor_cachemode;
#else
#define m68k_supervisor_cachemode _PAGE_CACHE040
#endif

#if defined(CPU_M68040_OR_M68060_ONLY)
#define mm_cachebits _PAGE_CACHE040
#elif defined(CPU_M68020_OR_M68030_ONLY)
#define mm_cachebits 0
#else
extern unsigned long mm_cachebits;
#endif

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_COPY	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_READONLY	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED | mm_cachebits)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_DIRTY | _PAGE_ACCESSED | mm_cachebits)

/* Alternate definitions that are compile time constants, for
   initializing protection_map.  The cachebits are fixed later.  */
#define PAGE_NONE_C	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED)
#define PAGE_SHARED_C	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_COPY_C	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED)
#define PAGE_READONLY_C	__pgprot(_PAGE_PRESENT | _PAGE_RONLY | _PAGE_ACCESSED)

/*
 * The m68k can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
 */
#define __P000	PAGE_NONE_C
#define __P001	PAGE_READONLY_C
#define __P010	PAGE_COPY_C
#define __P011	PAGE_COPY_C
#define __P100	PAGE_READONLY_C
#define __P101	PAGE_READONLY_C
#define __P110	PAGE_COPY_C
#define __P111	PAGE_COPY_C

#define __S000	PAGE_NONE_C
#define __S001	PAGE_READONLY_C
#define __S010	PAGE_SHARED_C
#define __S011	PAGE_SHARED_C
#define __S100	PAGE_READONLY_C
#define __S101	PAGE_READONLY_C
#define __S110	PAGE_SHARED_C
#define __S111	PAGE_SHARED_C

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

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot) \
({ pte_t __pte; pte_val(__pte) = virt_to_phys((void *)page) + pgprot_val(pgprot); __pte; })
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; pte_val(__pte) = (unsigned long)physpage + pgprot_val(pgprot); __pte; })

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	int i;
	unsigned long ptbl;
	ptbl = virt_to_phys(ptep);
	for (i = 0; i < 16; i++, ptbl += sizeof(pte_table)/16)
		pmdp->pmd[i] = _PAGE_TABLE | _PAGE_ACCESSED | ptbl;
}

extern inline void pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{ pgd_val(*pgdp) = _PAGE_TABLE | _PAGE_ACCESSED | virt_to_phys(pmdp); }

extern inline unsigned long pte_page(pte_t pte)
{ return (unsigned long)phys_to_virt(pte_val(pte) & PAGE_MASK); }

extern inline unsigned long pmd_page2(pmd_t *pmd)
{ return (unsigned long)phys_to_virt(pmd_val(*pmd) & _TABLE_MASK); }
#define pmd_page(pmd) pmd_page2(&(pmd))

extern inline unsigned long pgd_page(pgd_t pgd)
{ return (unsigned long)phys_to_virt(pgd_val(pgd) & _TABLE_MASK); }

extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & (_PAGE_PRESENT | _PAGE_FAKE_SUPER); }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

extern inline int pmd_none2(pmd_t *pmd)		{ return !pmd_val(*pmd); }
#define pmd_none(pmd) pmd_none2(&(pmd))
extern inline int pmd_bad2(pmd_t *pmd)		{ return (pmd_val(*pmd) & _DESCTYPE_MASK) != _PAGE_TABLE; }
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
extern inline int pgd_bad(pgd_t pgd)		{ return (pgd_val(pgd) & _DESCTYPE_MASK) != _PAGE_TABLE; }
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
extern inline pte_t pte_mkcache(pte_t pte)	{ pte_val(pte) = (pte_val(pte) & _CACHEMASK040) | m68k_supervisor_cachemode; return pte; }

/* to set the page-dir */
extern inline void SET_PAGE_DIR(struct task_struct * tsk, pgd_t * pgdir)
{
	tsk->tss.crp[0] = 0x80000000 | _PAGE_TABLE;
	tsk->tss.crp[1] = virt_to_phys(pgdir);
	if (tsk == current) {
		if (CPU_IS_040_OR_060)
			__asm__ __volatile__ (".chip 68040\n\t"
					      "movec %0,%%urp\n\t"
					      ".chip 68k"
					      : : "r" (tsk->tss.crp[1]));
		else {
			unsigned long tmp;
			__asm__ __volatile__ ("movec  %%cacr,%0\n\t"
					      "orw #0x0808,%0\n\t"
					      "movec %0,%%cacr\n\t"
					      "pmove %1,%%crp\n\t"
					      : "=d" (tmp)
					      : "m" (tsk->tss.crp[0]));
		}
	}
}

#define PAGE_DIR_OFFSET(tsk,address) pgd_offset((tsk),(address))

/* to find an entry in a page-table-directory */
extern inline pgd_t * pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> PGDIR_SHIFT);
}

#define swapper_pg_dir kernel_pg_dir
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

/* Prior to calling these routines, the page should have been flushed
 * from both the cache and ATC, or the CPU might not notice that the
 * cache setting for the page has been changed. -jskov
 */
static inline void nocache_page (unsigned long vaddr)
{
	if (CPU_IS_040_OR_060) {
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
	if (CPU_IS_040_OR_060) {
		pgd_t *dir;
		pmd_t *pmdp;
		pte_t *ptep;

		dir = pgd_offset_k(vaddr);
		pmdp = pmd_offset(dir,vaddr);
		ptep = pte_offset(pmdp,vaddr);
		*ptep = pte_mkcache(*ptep);
	}
}

extern struct pgtable_cache_struct {
	unsigned long *pmd_cache;
	unsigned long *pte_cache;
/* This counts in units of pointer tables, of which can be eight per page. */
	unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist ((unsigned long *)0)
#define pmd_quicklist (quicklists.pmd_cache)
#define pte_quicklist (quicklists.pte_cache)
/* This isn't accurate because of fragmentation of allocated pages for
   pointer tables, but that should not be a problem. */
#define pgtable_cache_size ((quicklists.pgtable_cache_sz+7)/8)

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset);
extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset);

extern pmd_t *get_pointer_table(void);
extern int free_pointer_table(pmd_t *);

extern __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	ret = pte_quicklist;
	if (ret) {
		pte_quicklist = (unsigned long *)*ret;
		ret[0] = 0;
		quicklists.pgtable_cache_sz -= 8;
	}
	return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long)pte_quicklist;
	pte_quicklist = (unsigned long *)pte;
	quicklists.pgtable_cache_sz += 8;
}

extern __inline__ void free_pte_slow(pte_t *pte)
{
	cache_page((unsigned long)pte);
	free_page((unsigned long) pte);
}

extern __inline__ pmd_t *get_pmd_fast(void)
{
	unsigned long *ret;

	ret = pmd_quicklist;
	if (ret) {
		pmd_quicklist = (unsigned long *)*ret;
		ret[0] = 0;
		quicklists.pgtable_cache_sz--;
	}
	return (pmd_t *)ret;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long)pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	quicklists.pgtable_cache_sz++;
}

extern __inline__ int free_pmd_slow(pmd_t *pmd)
{
	return free_pointer_table(pmd);
}

/* The pgd cache is folded into the pmd cache, so these are dummy routines. */
extern __inline__ pgd_t *get_pgd_fast(void)
{
	return (pgd_t *)0;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
}

extern void __bad_pte(pmd_t *pmd);
extern void __bad_pmd(pgd_t *pgd);

extern inline void pte_free(pte_t * pte)
{
	free_pte_fast(pte);
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = get_pte_fast();

		if (!page)
			return get_pte_slow(pmd, address);
		pmd_set(pmd,page);
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline void pmd_free(pmd_t * pmd)
{
	free_pmd_fast(pmd);
}

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_pmd_fast();

		if (!page)
			return get_pmd_slow(pgd, address);
		pgd_set(pgd, page);
		return page + address;
	}
	if (pgd_bad(*pgd)) {
		__bad_pmd(pgd);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

extern inline void pte_free_kernel(pte_t * pte)
{
	free_pte_fast(pte);
}

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	return pte_alloc(pmd, address);
}

extern inline void pmd_free_kernel(pmd_t * pmd)
{
	free_pmd_fast(pmd);
}

extern inline pmd_t * pmd_alloc_kernel(pgd_t * pgd, unsigned long address)
{
	return pmd_alloc(pgd, address);
}

extern inline void pgd_free(pgd_t * pgd)
{
	free_pmd_fast((pmd_t *)pgd);
}

extern inline pgd_t * pgd_alloc(void)
{
	pgd_t *pgd = (pgd_t *)get_pmd_fast();
	if (!pgd)
		pgd = (pgd_t *)get_pointer_table();
	return pgd;
}

extern int do_check_pgt_cache(int, int);

extern inline void set_pgdir(unsigned long address, pgd_t entry)
{
}

/*
 * Check if the addr/len goes up to the end of a physical
 * memory chunk.  Used for DMA functions.
 */
#ifdef CONFIG_SINGLE_MEMORY_CHUNK
/*
 * It makes no sense to consider whether we cross a memory boundary if
 * we support just one physical chunk of memory.
 */
extern inline int mm_end_of_chunk (unsigned long addr, int len)
{
	return 0;
}
#else
int mm_end_of_chunk (unsigned long addr, int len);
#endif

extern void kernel_set_cachemode(void *addr, unsigned long size, int cmode);

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
/* With the new handling of PAGE_NONE the old definitions definitely
   don't work any more.  */

#define SWP_TYPE(entry)  (((entry) >> 2) & 0x7f)
#if 0
#define SWP_OFFSET(entry) ((entry) >> 9)
#define SWP_ENTRY(type,offset) (((type) << 2) | ((offset) << 9))
#else
#define SWP_OFFSET(entry) ((entry) >> PAGE_SHIFT)
#define SWP_ENTRY(type,offset) (((type) << 2) | ((offset) << PAGE_SHIFT))
#endif

#endif /* __ASSEMBLY__ */

#define module_map      vmalloc
#define module_unmap    vfree

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)

#endif /* _M68K_PGTABLE_H */
