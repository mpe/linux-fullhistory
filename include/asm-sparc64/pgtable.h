/* $Id: pgtable.h,v 1.96 1998/10/27 23:28:42 davem Exp $
 * pgtable.h: SpitFire page table operations.
 *
 * Copyright 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef _SPARC64_PGTABLE_H
#define _SPARC64_PGTABLE_H

/* This file contains the functions and defines necessary to modify and use
 * the SpitFire page tables.
 */

#ifndef __ASSEMBLY__
#include <linux/mm.h>
#endif
#include <asm/spitfire.h>
#include <asm/asi.h>
#include <asm/mmu_context.h>
#include <asm/system.h>

#ifndef __ASSEMBLY__
#include <asm/sbus.h>

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3) + (PAGE_SHIFT-2))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Entries per page directory level. */
#define PTRS_PER_PTE		(1UL << (PAGE_SHIFT-3))

/* We the first one in this file, what we export to the kernel
 * is different so we can optimize correctly for 32-bit tasks.
 */
#define REAL_PTRS_PER_PMD	(1UL << (PAGE_SHIFT-2))
#define PTRS_PER_PMD		((const int)((current->tss.flags & SPARC_FLAG_32BIT) ? \
				 (REAL_PTRS_PER_PMD >> 2) : (REAL_PTRS_PER_PMD)))

/* We cannot use the top 16G because VPTE table lives there. */
#define PTRS_PER_PGD		((1UL << (PAGE_SHIFT-3))-1)

/* Kernel has a separate 44bit address space. */
#define USER_PTRS_PER_PGD	((const int)((current->tss.flags & SPARC_FLAG_32BIT) ? \
				 (1) : (PTRS_PER_PGD)))

#define PTE_TABLE_SIZE	0x2000	/* 1024 entries 8 bytes each */
#define PMD_TABLE_SIZE	0x2000	/* 2048 entries 4 bytes each */
#define PGD_TABLE_SIZE	0x1000	/* 1024 entries 4 bytes each */

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE	(1UL << (PAGE_SHIFT-3))

/* NOTE: TLB miss handlers depend heavily upon where this is. */
#define VMALLOC_START		0x0000000140000000UL
#define VMALLOC_VMADDR(x)	((unsigned long)(x))
#define VMALLOC_END		0x0000000200000000UL

#endif /* !(__ASSEMBLY__) */

/* SpitFire TTE bits. */
#define _PAGE_VALID	0x8000000000000000	/* Valid TTE                          */
#define _PAGE_R		0x8000000000000000	/* Used to keep ref bit up to date    */
#define _PAGE_SZ4MB	0x6000000000000000	/* 4MB Page                           */
#define _PAGE_SZ512K	0x4000000000000000	/* 512K Page                          */
#define _PAGE_SZ64K	0x2000000000000000	/* 64K Page                           */
#define _PAGE_SZ8K	0x0000000000000000	/* 8K Page                            */
#define _PAGE_NFO	0x1000000000000000	/* No Fault Only                      */
#define _PAGE_IE	0x0800000000000000	/* Invert Endianness                  */
#define _PAGE_SOFT2	0x07FC000000000000	/* Second set of software bits        */
#define _PAGE_DIAG	0x0003FE0000000000	/* Diagnostic TTE bits                */
#define _PAGE_PADDR	0x000001FFFFFFE000	/* Physical Address bits [40:13]      */
#define _PAGE_SOFT	0x0000000000001F80	/* First set of software bits         */
#define _PAGE_L		0x0000000000000040	/* Locked TTE                         */
#define _PAGE_CP	0x0000000000000020	/* Cacheable in Physical Cache        */
#define _PAGE_CV	0x0000000000000010	/* Cacheable in Virtual Cache         */
#define _PAGE_E		0x0000000000000008	/* side-Effect                        */
#define _PAGE_P		0x0000000000000004	/* Privileged Page                    */
#define _PAGE_W		0x0000000000000002	/* Writable                           */
#define _PAGE_G		0x0000000000000001	/* Global                             */

/* Here are the SpitFire software bits we use in the TTE's. */
#define _PAGE_MODIFIED	0x0000000000000800	/* Modified Page (ie. dirty)          */
#define _PAGE_ACCESSED	0x0000000000000400	/* Accessed Page (ie. referenced)     */
#define _PAGE_READ	0x0000000000000200	/* Readable SW Bit                    */
#define _PAGE_WRITE	0x0000000000000100	/* Writable SW Bit                    */
#define _PAGE_PRESENT	0x0000000000000080	/* Present Page (ie. not swapped out) */

#define _PAGE_CACHE	(_PAGE_CP | _PAGE_CV)

#define __DIRTY_BITS	(_PAGE_MODIFIED | _PAGE_WRITE | _PAGE_W)
#define __ACCESS_BITS	(_PAGE_ACCESSED | _PAGE_READ | _PAGE_R)
#define __PRIV_BITS	_PAGE_P

#define PAGE_NONE	__pgprot (_PAGE_PRESENT | _PAGE_ACCESSED)

#define PAGE_SHARED	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS | _PAGE_W | _PAGE_WRITE)

#define PAGE_COPY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS)

#define PAGE_READONLY	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __ACCESS_BITS)

#define PAGE_KERNEL	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __PRIV_BITS | __ACCESS_BITS | __DIRTY_BITS)

#define PAGE_INVALID	__pgprot (0)

#define _PFN_MASK	_PAGE_PADDR

#define _PAGE_CHG_MASK	(_PFN_MASK | _PAGE_MODIFIED | _PAGE_ACCESSED | _PAGE_PRESENT)

#define pg_iobits (_PAGE_VALID | _PAGE_PRESENT | __DIRTY_BITS | __ACCESS_BITS | _PAGE_E)

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

#ifndef __ASSEMBLY__

extern pte_t __bad_page(void);

#define BAD_PAGE	__bad_page()

/* First physical page can be anywhere, the following is needed so that
 * va-->pa and vice versa conversions work properly without performance
 * hit for all __pa()/__va() operations.
 */
extern unsigned long phys_base;
#define ZERO_PAGE	((unsigned long)__va(phys_base))

/* Allocate a block of RAM which is aligned to its size.
 * This procedure can be used until the call to mem_init().
 */
extern void *sparc_init_alloc(unsigned long *kbrk, unsigned long size);

/* Cache and TLB flush operations. */

/* These are the same regardless of whether this is an SMP kernel or not. */
#define flush_cache_mm(mm)			flushw_user()
#define flush_cache_range(mm, start, end)	flushw_user()
#define flush_cache_page(vma, page)		flushw_user()

/* These operations are unnecessary on the SpitFire since D-CACHE is write-through. */
#define flush_icache_range(start, end)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)

extern void __flush_dcache_range(unsigned long start, unsigned long end);

extern void __flush_cache_all(void);

extern void __flush_tlb_all(void);
extern void __flush_tlb_mm(unsigned long context, unsigned long r);
extern void __flush_tlb_range(unsigned long context, unsigned long start,
			      unsigned long r, unsigned long end,
			      unsigned long pgsz, unsigned long size);
extern void __flush_tlb_page(unsigned long context, unsigned long page, unsigned long r);

#ifndef __SMP__

#define flush_cache_all()	__flush_cache_all()
#define flush_tlb_all()		__flush_tlb_all()

#define flush_tlb_mm(mm) \
do { if((mm)->context != NO_CONTEXT) \
	__flush_tlb_mm((mm)->context & 0x3ff, SECONDARY_CONTEXT); \
} while(0)

#define flush_tlb_range(mm, start, end) \
do { if((mm)->context != NO_CONTEXT) { \
	unsigned long __start = (start)&PAGE_MASK; \
	unsigned long __end = (end)&PAGE_MASK; \
	__flush_tlb_range((mm)->context & 0x3ff, __start, \
			  SECONDARY_CONTEXT, __end, PAGE_SIZE, \
			  (__end - __start)); \
     } \
} while(0)

#define flush_tlb_page(vma, page) \
do { struct mm_struct *__mm = (vma)->vm_mm; \
     if(__mm->context != NO_CONTEXT) \
	__flush_tlb_page(__mm->context & 0x3ff, (page)&PAGE_MASK, \
			 SECONDARY_CONTEXT); \
} while(0)

#else /* __SMP__ */

extern void smp_flush_cache_all(void);
extern void smp_flush_tlb_all(void);
extern void smp_flush_tlb_mm(struct mm_struct *mm);
extern void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				unsigned long end);
extern void smp_flush_tlb_page(struct mm_struct *mm, unsigned long page);

#define flush_cache_all()	smp_flush_cache_all()
#define flush_tlb_all()		smp_flush_tlb_all()

extern __inline__ void flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT)
		smp_flush_tlb_mm(mm);
}

extern __inline__ void flush_tlb_range(struct mm_struct *mm, unsigned long start,
				       unsigned long end)
{
	if(mm->context != NO_CONTEXT)
		smp_flush_tlb_range(mm, start, end);
}

extern __inline__ void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT)
		smp_flush_tlb_page(mm, page);
}

#endif

#define mk_pte(page, pgprot)		(__pte(__pa(page) | pgprot_val(pgprot)))
#define mk_pte_phys(physpage, pgprot)	(__pte((physpage) | pgprot_val(pgprot)))
#define pte_modify(_pte, newprot) \
	(pte_val(_pte) = ((pte_val(_pte) & _PAGE_CHG_MASK) | pgprot_val(newprot)))
#define pmd_set(pmdp, ptep)		(pmd_val(*(pmdp)) = __pa((unsigned long) (ptep)))
#define pgd_set(pgdp, pmdp)		(pgd_val(*(pgdp)) = __pa((unsigned long) (pmdp)))
#define pte_page(pte)   ((unsigned long) __va(((pte_val(pte)&~PAGE_OFFSET)&~(0xfffUL))))
#define pmd_page(pmd)			((unsigned long) __va(pmd_val(pmd)))
#define pgd_page(pgd)			((unsigned long) __va(pgd_val(pgd)))
#define pte_none(pte) 			(!pte_val(pte))
#define pte_present(pte)		(pte_val(pte) & _PAGE_PRESENT)
#define pte_clear(pte)			(pte_val(*(pte)) = 0UL)
#define pmd_none(pmd)			(!pmd_val(pmd))
#define pmd_bad(pmd)			(0)
#define pmd_present(pmd)		(pmd_val(pmd) != 0UL)
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0UL)
#define pgd_none(pgd)			(!pgd_val(pgd))
#define pgd_bad(pgd)			(0)
#define pgd_present(pgd)		(pgd_val(pgd) != 0UL)
#define pgd_clear(pgdp)			(pgd_val(*(pgdp)) = 0UL)

/* The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
#define pte_read(pte)		(pte_val(pte) & _PAGE_READ)
#define pte_write(pte)		(pte_val(pte) & _PAGE_WRITE)
#define pte_dirty(pte)		(pte_val(pte) & _PAGE_MODIFIED)
#define pte_young(pte)		(pte_val(pte) & _PAGE_ACCESSED)
#define pte_wrprotect(pte)	(__pte(pte_val(pte) & ~(_PAGE_WRITE|_PAGE_W)))
#define pte_rdprotect(pte)	(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_READ))
#define pte_mkclean(pte)	(__pte(pte_val(pte) & ~(_PAGE_MODIFIED|_PAGE_W)))
#define pte_mkold(pte)		(__pte(((pte_val(pte)<<1UL)>>1UL) & ~_PAGE_ACCESSED))

/* Be very careful when you change these three, they are delicate. */
static __inline__ pte_t pte_mkyoung(pte_t _pte)
{	if(pte_val(_pte) & _PAGE_READ)
		return __pte(pte_val(_pte)|(_PAGE_ACCESSED|_PAGE_R));
	else
		return __pte(pte_val(_pte)|(_PAGE_ACCESSED));
}

static __inline__ pte_t pte_mkwrite(pte_t _pte)
{	if(pte_val(_pte) & _PAGE_MODIFIED)
		return __pte(pte_val(_pte)|(_PAGE_WRITE|_PAGE_W));
	else
		return __pte(pte_val(_pte)|(_PAGE_WRITE));
}

static __inline__ pte_t pte_mkdirty(pte_t _pte)
{	if(pte_val(_pte) & _PAGE_WRITE)
		return __pte(pte_val(_pte)|(_PAGE_MODIFIED|_PAGE_W));
	else
		return __pte(pte_val(_pte)|(_PAGE_MODIFIED));
}

/* to find an entry in a page-table-directory. */
#define pgd_offset(mm, address) ((mm)->pgd + ((address >> PGDIR_SHIFT) & (PTRS_PER_PGD)))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir, address)	((pmd_t *) pgd_page(*(dir)) + \
					((address >> PMD_SHIFT) & (REAL_PTRS_PER_PMD-1)))

/* Find an entry in the third-level page table.. */
#define pte_offset(dir, address)	((pte_t *) pmd_page(*(dir)) + \
					((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))

/* Very stupidly, we used to get new pgd's and pmd's, init their contents
 * to point to the NULL versions of the next level page table, later on
 * completely re-init them the same way, then free them up.  This wasted
 * a lot of work and caused unnecessary memory traffic.  How broken...
 * We fix this by caching them.
 */

#ifdef __SMP__
/* Sliiiicck */
#define pgt_quicklists	cpu_data[smp_processor_id()]
#else
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgcache_size;
	unsigned long pgdcache_size;
} pgt_quicklists;
#endif
#define pgd_quicklist		(pgt_quicklists.pgd_cache)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		(pgt_quicklists.pte_cache)
#define pgtable_cache_size	(pgt_quicklists.pgcache_size)
#define pgd_cache_size		(pgt_quicklists.pgdcache_size)

#ifndef __SMP__

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	struct page *page = mem_map + MAP_NR(pgd);

	if (!page->pprev_hash) {
		(unsigned long *)page->next_hash = pgd_quicklist;
		pgd_quicklist = (unsigned long *)page;
	}
	(unsigned long)page->pprev_hash |=
		(((unsigned long)pgd & (PAGE_SIZE / 2)) ? 2 : 1);
	pgd_cache_size++;
}

extern __inline__ pgd_t *get_pgd_fast(void)
{
        struct page *ret;

        if ((ret = (struct page *)pgd_quicklist) != NULL) {
                unsigned long mask = (unsigned long)ret->pprev_hash;
		unsigned long off = 0;

		if (mask & 1)
			mask &= ~1;
		else {
			off = PAGE_SIZE / 2;
			mask &= ~2;
		}
		(unsigned long)ret->pprev_hash = mask;
		if (!mask)
			pgd_quicklist = (unsigned long *)ret->next_hash;
                ret = (struct page *) (page_address(ret) + off);
                pgd_cache_size--;
        } else {
		ret = (struct page *) __get_free_page(GFP_KERNEL);
		if(ret) {
			struct page *page = mem_map + MAP_NR(ret);
			
			memset(ret, 0, PAGE_SIZE);
			(unsigned long)page->pprev_hash = 2;
			(unsigned long *)page->next_hash = pgd_quicklist;
			pgd_quicklist = (unsigned long *)page;
			pgd_cache_size++;
		}
        }
        return (pgd_t *)ret;
}

#else /* __SMP__ */

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	} else {
		ret = (unsigned long *) __get_free_page(GFP_KERNEL);
		if(ret)
			memset(ret, 0, PAGE_SIZE);
	}
	return (pgd_t *)ret;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#endif /* __SMP__ */

extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long address_premasked);

extern __inline__ pmd_t *get_pmd_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	}
	return (pmd_t *)ret;
}

extern __inline__ void free_pmd_fast(pgd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pmd;
	pgtable_cache_size++;
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
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

#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)
#define pmd_free_kernel(pmd)	free_pmd_fast(pmd)
#define pmd_free(pmd)		free_pmd_fast(pmd)
#define pgd_free(pgd)		free_pgd_fast(pgd)
#define pgd_alloc()		get_pgd_fast()

extern inline pte_t * pte_alloc(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = get_pte_fast();

		if (!page)
			return get_pte_slow(pmd, address);
		pmd_set(pmd, page);
		return page + address;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (REAL_PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_pmd_fast();

		if (!page)
			return get_pmd_slow(pgd, address);
		pgd_set(pgd, page);
		return page + address;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

#define pte_alloc_kernel(pmd, addr)	pte_alloc(pmd, addr)
#define pmd_alloc_kernel(pgd, addr)	pmd_alloc(pgd, addr)

extern int do_check_pgt_cache(int, int);

/* Nothing to do on sparc64 :) */
#define set_pgdir(address, entry)	do { } while(0)

extern pgd_t swapper_pg_dir[1];

extern inline void SET_PAGE_DIR(struct task_struct *tsk, pgd_t *pgdir)
{
	if(pgdir != swapper_pg_dir && tsk->mm == current->mm) {
		register unsigned long paddr asm("o5");

		paddr = __pa(pgdir);
		__asm__ __volatile__ ("
			rdpr		%%pstate, %%o4
			wrpr		%%o4, %1, %%pstate
			mov		%3, %%g4
			mov		%0, %%g7
			stxa		%%g0, [%%g4] %2
			wrpr		%%o4, 0x0, %%pstate
		" : /* No outputs */
		  : "r" (paddr), "i" (PSTATE_MG|PSTATE_IE),
		    "i" (ASI_DMMU), "i" (TSB_REG)
		  : "o4");
		flush_tlb_mm(current->mm);
	}
}

/* Routines for getting a dvma scsi buffer. */
struct mmu_sglist {
	char *addr;
	char *__dont_touch;
	unsigned int len;
	__u32 dvma_addr;
};

extern __u32 mmu_get_scsi_one(char *, unsigned long, struct linux_sbus *sbus);
extern void  mmu_get_scsi_sgl(struct mmu_sglist *, int, struct linux_sbus *sbus);

extern void mmu_release_scsi_one(u32 vaddr, unsigned long len,
				 struct linux_sbus *sbus);
extern void mmu_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus);

#define NEED_DMA_SYNCHRONIZATION
#define mmu_sync_dma(dma_addr, len, sbus_instance)	\
	mmu_release_scsi_one((dma_addr), (len), (sbus_instance))

/* These do nothing with the way I have things setup. */
#define mmu_lockarea(vaddr, len)		(vaddr)
#define mmu_unlockarea(vaddr, len)		do { } while(0)

/* There used to be some funny code here which tried to guess which
 * TLB wanted the mapping, that wasn't accurate enough to justify it's
 * existance.  The real way to do that is to have each TLB miss handler
 * pass in a distinct code to do_sparc64_fault() and do it more accurately
 * there.
 *
 * What we do need to handle here is prevent I-cache corruption.  The
 * deal is that the I-cache snoops stores from other CPUs and all DMA
 * activity, however stores from the local processor are not snooped.
 * The dynamic linker and our signal handler mechanism take care of
 * the cases where they write into instruction space, but when a page
 * is copied in the kernel and then executed in user-space is not handled
 * right.  This leads to corruptions if things are "just right", consider
 * the following scenerio:
 * 1) Process 1 frees up a page that was used for the PLT of libc in
 *    it's address space.
 * 2) Process 2 writes into a page in the PLT of libc for the first
 *    time.  do_wp_page() copies the page locally, the local I-cache of
 *    the processor does not notice the writes during the page copy.
 *    The new page used just so happens to be the one just freed in #1.
 * 3) After the PLT write, later the cpu calls into an unresolved PLT
 *    entry, the CPU executes old instructions from process 1's PLT
 *    table.
 * 4) Splat.
 */
extern void flush_icache_page(unsigned long phys_page);
#define update_mmu_cache(__vma, __address, _pte) \
do { \
	unsigned short __flags = ((__vma)->vm_flags); \
	if ((__flags & VM_EXEC) != 0 && \
	    ((pte_val(_pte) & (_PAGE_PRESENT | _PAGE_WRITE | _PAGE_MODIFIED)) == \
	     (_PAGE_PRESENT | _PAGE_WRITE | _PAGE_MODIFIED))) { \
		flush_icache_page(pte_page(_pte) - page_offset); \
	} \
} while(0)

/* Make a non-present pseudo-TTE. */
extern inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = (type<<PAGE_SHIFT)|(offset<<(PAGE_SHIFT+8)); return pte; }

extern inline pte_t mk_pte_io(unsigned long page, pgprot_t prot, int space)
{
	pte_t pte;
	pte_val(pte) = ((page) | pgprot_val(prot) | _PAGE_E) & ~(unsigned long)_PAGE_CACHE;
	pte_val(pte) |= (((unsigned long)space) << 32);
	return pte;
}

#define SWP_TYPE(entry)		(((entry>>PAGE_SHIFT) & 0xff))
#define SWP_OFFSET(entry)	((entry) >> (PAGE_SHIFT+8))
#define SWP_ENTRY(type,offset)	pte_val(mk_swap_pte((type),(offset)))

extern __inline__ unsigned long
sun4u_get_pte (unsigned long addr)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (addr >= PAGE_OFFSET)
		return addr & _PAGE_PADDR;
	pgdp = pgd_offset_k (addr);
	pmdp = pmd_offset (pgdp, addr);
	ptep = pte_offset (pmdp, addr);
	return pte_val (*ptep) & _PAGE_PADDR;
}

extern __inline__ unsigned long
__get_phys (unsigned long addr)
{
	return sun4u_get_pte (addr);
}

extern __inline__ int
__get_iospace (unsigned long addr)
{
	return ((sun4u_get_pte (addr) & 0xf0000000) >> 28);
}

extern void * module_map (unsigned long size);
extern void module_unmap (void *addr);

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(test_bit(PG_skip, &(page)->flags))

extern int io_remap_page_range(unsigned long from, unsigned long offset,
			       unsigned long size, pgprot_t prot, int space);

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC64_PGTABLE_H) */
