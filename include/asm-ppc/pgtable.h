#include <linux/config.h>

#ifndef _PPC_PGTABLE_H
#define _PPC_PGTABLE_H

#ifndef __ASSEMBLY__
#include <linux/mm.h>
#include <asm/processor.h>		/* For TASK_SIZE */
#include <asm/mmu.h>
#include <asm/page.h>

extern void local_flush_tlb_all(void);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void local_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			    unsigned long end);

#define flush_tlb_all local_flush_tlb_all
#define flush_tlb_mm local_flush_tlb_mm
#define flush_tlb_page local_flush_tlb_page
#define flush_tlb_range local_flush_tlb_range

/*
 * No cache flushing is required when address mappings are
 * changed, because the caches on PowerPCs are physically
 * addressed.
 * Also, when SMP we use the coherency (M) bit of the
 * BATs and PTEs.  -- Cort
 */
#define flush_cache_all()		do { } while (0)
#define flush_cache_mm(mm)		do { } while (0)
#define flush_cache_range(mm, a, b)	do { } while (0)
#define flush_cache_page(vma, p)	do { } while (0)

extern void flush_icache_range(unsigned long, unsigned long);
extern void flush_page_to_ram(unsigned long);

extern unsigned long va_to_phys(unsigned long address);
extern pte_t *va_to_pte(struct task_struct *tsk, unsigned long address);
#endif /* __ASSEMBLY__ */
/*
 * The PowerPC MMU uses a hash table containing PTEs, together with
 * a set of 16 segment registers (on 32-bit implementations), to define
 * the virtual to physical address mapping.
 *
 * We use the hash table as an extended TLB, i.e. a cache of currently
 * active mappings.  We maintain a two-level page table tree, much like
 * that used by the i386, for the sake of the Linux memory management code.
 * Low-level assembler code in head.S (procedure hash_page) is responsible
 * for extracting ptes from the tree and putting them into the hash table
 * when necessary, and updating the accessed and modified bits in the
 * page table tree.
 *
 * The PowerPC MPC8xx uses a TLB with hardware assisted, software tablewalk.
 * We also use the two level tables, but we can put the real bits in them
 * needed for the TLB and tablewalk.  These definitions require Mx_CTR.PPM = 0,
 * Mx_CTR.PPCS = 0, and MD_CTR.TWAM = 1.  The level 2 descriptor has
 * additional page protection (when Mx_CTR.PPCS = 1) that allows TLB hit
 * based upon user/super access.  The TLB does not have accessed nor write
 * protect.  We assume that if the TLB get loaded with an entry it is
 * accessed, and overload the changed bit for write protect.  We use
 * two bits in the software pte that are supposed to be set to zero in
 * the TLB entry (24 and 25) for these indicators.  Although the level 1
 * descriptor contains the guarded and writethrough/copyback bits, we can
 * set these at the page level since they get copied from the Mx_TWC
 * register when the TLB entry is loaded.  We will use bit 27 for guard, since
 * that is where it exists in the MD_TWC, and bit 26 for writethrough.
 * These will get masked from the level 2 descriptor at TLB load time, and
 * copied to the MD_TWC before it gets loaded.
 */

/* PMD_SHIFT determines the size of the area mapped by the second-level page tables */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: our page-table tree is two-level, so
 * we don't really have any PMD directory.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024
#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)

/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 64MB value just means that there will be a 64MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 *
 * The vmalloc_offset MUST be larger than the gap between the bat2 mapping
 * and the size of physical ram.  Since the bat2 mapping can be larger than
 * the amount of ram we have vmalloc_offset must ensure that we don't try
 * to allocate areas that don't exist! This value of 64M will only cause
 * problems when we have >128M -- Cort
 */
#define VMALLOC_OFFSET	(0x4000000) /* 64M */
#define VMALLOC_START ((((long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1)))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	0xf0000000

/*
 * Bits in a linux-style PTE.  These match the bits in the
 * (hardware-defined) PowerPC PTE as closely as possible.
 */
#ifndef CONFIG_8xx
#define _PAGE_PRESENT	0x001	/* software: pte contains a translation */
#define _PAGE_USER	0x002	/* matches one of the PP bits */
#define _PAGE_RW	0x004	/* software: user write access allowed */
#define _PAGE_GUARDED	0x008
#define _PAGE_COHERENT	0x010	/* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x020	/* I: cache inhibit */
#define _PAGE_WRITETHRU	0x040	/* W: cache write-through */
#define _PAGE_DIRTY	0x080	/* C: page changed */
#define _PAGE_ACCESSED	0x100	/* R: page referenced */
#define _PAGE_HWWRITE	0x200	/* software: _PAGE_RW & _PAGE_DIRTY */
#define _PAGE_SHARED	0

#else
#define _PAGE_PRESENT	0x0001	/* Page is valid */
#define _PAGE_NO_CACHE	0x0002	/* I: cache inhibit */
#define _PAGE_SHARED	0x0004	/* No ASID (context) compare */

/* These four software bits must be masked out when the entry is loaded
 * into the TLB.
 */
#define _PAGE_GUARDED	0x0010	/* software: guarded access */
#define _PAGE_WRITETHRU 0x0020	/* software: use writethrough cache */
#define _PAGE_RW	0x0040	/* software: user write access allowed */
#define _PAGE_ACCESSED	0x0080	/* software: page referenced */

#define _PAGE_DIRTY	0x0100	/* C: page changed (write protect) */
#define _PAGE_USER	0x0800	/* One of the PP bits, the other must be 0 */

/* This is used to enable or disable the actual hardware write
 * protection.
 */
#define _PAGE_HWWRITE	_PAGE_DIRTY

#endif /* CONFIG_8xx */

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#ifdef __SMP__
#define _PAGE_BASE	_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_COHERENT
#else
#define _PAGE_BASE	_PAGE_PRESENT | _PAGE_ACCESSED
#endif
#define _PAGE_WRENABLE	_PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_USER | \
				 _PAGE_SHARED)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_WRENABLE | _PAGE_SHARED)
#define PAGE_KERNEL_CI	__pgprot(_PAGE_BASE | _PAGE_WRENABLE | _PAGE_SHARED | \
				 _PAGE_NO_CACHE )

/*
 * The PowerPC can only do execute protection on a segment (256MB) basis,
 * not on a page basis.  So we consider execute permission the same as read.
 * Also, write permissions imply read permissions.
 * This is the closest we can get..
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
#ifndef __ASSEMBLY__
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

extern unsigned long empty_zero_page[1024];
#endif __ASSEMBLY__
#define BAD_PAGETABLE	__bad_pagetable()
#define BAD_PAGE	__bad_page()
#define ZERO_PAGE	((unsigned long) empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR	(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK	(~(sizeof(void*)-1))

/* sizeof(void*) == 1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2	2

/* to set the page-dir */
/* tsk is a task_struct and pgdir is a pte_t */
#ifndef CONFIG_8xx
#define SET_PAGE_DIR(tsk,pgdir)  \
	((tsk)->tss.pg_tables = (unsigned long *)(pgdir))
#else /* CONFIG_8xx */     
#define SET_PAGE_DIR(tsk,pgdir)  \
 do { \
	unsigned long __pgdir = (unsigned long)pgdir; \
	((tsk)->tss.pg_tables = (unsigned long *)(__pgdir)); \
	asm("mtspr %0,%1 \n\t" : : "i"(M_TWB), "r"(__pa(__pgdir))); \
 } while (0)
#endif /* CONFIG_8xx */
     
#ifndef __ASSEMBLY__
extern inline int pte_none(pte_t pte)		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_clear(pte_t *ptep)	{ pte_val(*ptep) = 0; }

extern inline int pmd_none(pmd_t pmd)		{ return !pmd_val(pmd); }
extern inline int pmd_bad(pmd_t pmd)		{ return (pmd_val(pmd) & ~PAGE_MASK) != 0; }
extern inline int pmd_present(pmd_t pmd)	{ return (pmd_val(pmd) & PAGE_MASK) != 0; }
extern inline void pmd_clear(pmd_t * pmdp)	{ pmd_val(*pmdp) = 0; }


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
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

extern inline void pte_uncache(pte_t pte)       { pte_val(pte) |= _PAGE_NO_CACHE; }
extern inline void pte_cache(pte_t pte)         { pte_val(pte) &= ~_PAGE_NO_CACHE; }

extern inline pte_t pte_rdprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_wrprotect(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_RW | _PAGE_HWWRITE); return pte; }
extern inline pte_t pte_mkclean(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_DIRTY | _PAGE_HWWRITE); return pte; }
extern inline pte_t pte_mkold(pte_t pte) {
	pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }

extern inline pte_t pte_mkread(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_RW;
	if (pte_val(pte) & _PAGE_DIRTY)
		pte_val(pte) |= _PAGE_HWWRITE;
	return pte;
}
extern inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	if (pte_val(pte) & _PAGE_RW)
		pte_val(pte) |= _PAGE_HWWRITE;
	return pte;
}
extern inline pte_t pte_mkyoung(pte_t pte) {
	pte_val(pte) |= _PAGE_ACCESSED; return pte; }

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#if 1
#define set_pte(pteptr, pteval)	((*(pteptr)) = (pteval))
#else
extern inline void set_pte(pte_t *pteptr, pte_t pteval)
{
	unsigned long val = pte_val(pteval);
	extern void xmon(void *);

	if ((val & _PAGE_PRESENT) && ((val < 0x111000 || (val & 0x800)
	    || ((val & _PAGE_HWWRITE) && (~val & (_PAGE_RW|_PAGE_DIRTY)))) {
		printk("bad pte val %lx ptr=%p\n", val, pteptr);
		xmon(0);
	}
	*pteptr = pteval;
}
#endif

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

static inline pte_t mk_pte_phys(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = (page) | pgprot_val(pgprot); return pte; }

extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = __pa(page) | pgprot_val(pgprot); return pte; }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline unsigned long pte_page(pte_t pte)
{ return (unsigned long) __va(pte_val(pte) & PAGE_MASK); }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return pmd_val(pmd); }


/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

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
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}


/*
 * This is handled very differently on the PPC since out page tables
 * are all 0's and I want to be able to use these zero'd pages elsewhere
 * as well - it gives us quite a speedup.
 *
 * Note that the SMP/UP versions are the same since we don't need a
 * per cpu list of zero pages since we do the zero-ing with the cache
 * off and the access routines are lock-free but the pgt cache stuff
 * _IS_ per-cpu since it isn't done with any lock-free access routines
 * (although I think we need arch-specific routines so I can do lock-free).
 *
 * I need to generalize this so we can use it for other arch's as well.
 * -- Cort
 */
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
	unsigned long *zero_cache;    /* head linked list of pre-zero'd pages */
  	unsigned long zero_sz;	      /* # currently pre-zero'd pages */
	unsigned long zeropage_hits;  /* # zero'd pages request that we've done */
	unsigned long zeropage_calls; /* # zero'd pages request that've been made */
  	unsigned long zerototal;      /* # pages zero'd over time */
} quicklists;

#ifdef __SMP__
/*#warning Tell Cort to do the pgt cache for SMP*/
#define pgd_quicklist (quicklists.pgd_cache)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (quicklists.pte_cache)
#define pgtable_cache_size (quicklists.pgtable_cache_sz)
#else /* __SMP__ */
#define pgd_quicklist (quicklists.pgd_cache)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (quicklists.pte_cache)
#define pgtable_cache_size (quicklists.pgtable_cache_sz)
#endif /* __SMP__ */

#define zero_quicklist (quicklists.zero_cache)
#define zero_cache_sz  (quicklists.zero_sz)

/* return a pre-zero'd page from the list, return NULL if none available -- Cort */
extern unsigned long get_zero_page_fast(void);

extern __inline__ pgd_t *get_pgd_slow(void)
{
	pgd_t *ret/* = (pgd_t *)__get_free_page(GFP_KERNEL)*/, *init;

	if ( (ret = (pgd_t *)get_zero_page_fast()) == NULL )
	{
		ret = (pgd_t *)__get_free_page(GFP_KERNEL);
		memset (ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
	}
	if (ret) {
		init = pgd_offset(&init_mm, 0);
		/*memset (ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));*/
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

extern void __bad_pte(pmd_t *pmd);

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)
#define pgd_free(pgd)           free_pgd_fast(pgd)
#define pgd_alloc()             get_pgd_fast()

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_pte_fast();
		
		if (!page)
			return get_pte_slow(pmd, address);
		pmd_val(*pmd) = (unsigned long) page;
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
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
#define pte_alloc_kernel	pte_alloc

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

extern __inline__ pte_t *find_pte(struct mm_struct *mm,unsigned long va)
{
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;

	va &= PAGE_MASK;
	
	dir = pgd_offset( mm, va );
	if (dir)
	{
		pmd = pmd_offset(dir, va & PAGE_MASK);
		if (pmd && pmd_present(*pmd))
		{
			pte = pte_offset(pmd, va);
			if (pte && pte_present(*pte))
			{			
				pte_uncache(*pte);
				flush_tlb_page(find_vma(mm,va),va);
			}
		}
	}
	return pte;
}

/*
 * Page tables may have changed.  We don't need to do anything here
 * as entries are faulted into the hash table by the low-level
 * data/instruction access exception handlers.
 */
#define update_mmu_cache(vma, addr, pte)	do { } while (0)

/*
 * When flushing the tlb entry for a page, we also need to flush the
 * hash table entry.  flush_hash_page is assembler (for speed) in head.S.
 */
extern void flush_hash_segments(unsigned low_vsid, unsigned high_vsid);
extern void flush_hash_page(unsigned context, unsigned long va);


#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))

#define module_map      vmalloc
#define module_unmap    vfree

/* CONFIG_APUS */
/* For virtual address to physical address conversion */
extern void cache_clear(__u32 addr, int length);
extern void cache_push(__u32 addr, int length);
extern int mm_end_of_chunk (unsigned long addr, int len);
extern unsigned long iopa(unsigned long addr);
extern unsigned long mm_ptov(unsigned long addr) __attribute__ ((const));

/* Values for nocacheflag and cmode */
/* These are not used by the APUS kernel_map, but prevents
   compilation errors. */
#define	KERNELMAP_FULL_CACHING		0
#define	KERNELMAP_NOCACHE_SER		1
#define	KERNELMAP_NOCACHE_NONSER	2
#define	KERNELMAP_NO_COPYBACK		3

/*
 * Map some physical address range into the kernel address space.
 */
extern unsigned long kernel_map(unsigned long paddr, unsigned long size,
				int nocacheflag, unsigned long *memavailp );

/*
 * Set cache mode of (kernel space) address range. 
 */
extern void kernel_set_cachemode (unsigned long address, unsigned long size,
                                 unsigned int cmode);

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)
#define kern_addr_valid(addr)	(1)

#endif __ASSEMBLY__
#endif /* _PPC_PGTABLE_H */
