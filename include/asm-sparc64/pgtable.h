/* $Id: pgtable.h,v 1.59 1997/10/12 06:20:43 davem Exp $
 * pgtable.h: SpitFire page table operations.
 *
 * Copyright 1996,1997 David S. Miller (davem@caip.rutgers.edu)
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
#define PGDIR_SHIFT	(PAGE_SHIFT + 2*(PAGE_SHIFT-3))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Entries per page directory level. */
#define PTRS_PER_PTE	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PMD	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PGD	(1UL << (PAGE_SHIFT-3))

#define PTE_TABLE_SIZE	0x2000	/* 1024 entries 8 bytes each */
#define PMD_TABLE_SIZE	0x2000	/* 1024 entries 8 bytes each */
#define PGD_TABLE_SIZE	0x2000	/* 1024 entries 8 bytes each */

/* the no. of pointers that fit on a page */
#define PTRS_PER_PAGE	(1UL << (PAGE_SHIFT-3))

/* NOTE: TLB miss handlers depend heavily upon where this is. */
#define VMALLOC_START		0x0000000800000000UL
#define VMALLOC_VMADDR(x)	((unsigned long)(x))

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

#define PAGE_NONE	__pgprot (_PAGE_PRESENT | _PAGE_VALID | _PAGE_CACHE | \
				  __PRIV_BITS | __ACCESS_BITS)

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
extern pmd_t *__bad_pmd(void);
extern pte_t *__bad_pte(void);

#define BAD_PMD		__bad_pmd()
#define BAD_PTE		__bad_pte()
#define BAD_PAGE	__bad_page()

/* First phsical page can be anywhere, the following is needed so that
 * va-->pa and vice versa conversions work properly without performance
 * hit for all __pa()/__va() operations.
 */
extern unsigned long phys_base;
#define ZERO_PAGE	((unsigned long)__va(phys_base))

/* This is for making TLB miss faster to process. */
extern unsigned long null_pmd_table;
extern unsigned long null_pte_table;

/* Allocate a block of RAM which is aligned to its size.
   This procedure can be used until the call to mem_init(). */
extern void *sparc_init_alloc(unsigned long *kbrk, unsigned long size);

/* Cache and TLB flush operations. */

/* These are the same regardless of whether this is an SMP kernel or not. */
#define flush_cache_mm(mm)			flushw_user()
#define flush_cache_range(mm, start, end)	flushw_user()
#define flush_cache_page(vma, page)		flushw_user()

/* This operation in unnecessary on the SpitFire since D-CACHE is write-through. */
#define flush_page_to_ram(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)

extern void __flush_cache_all(void);

extern void __flush_tlb_all(void);
extern void __flush_tlb_mm(unsigned long context);
extern void __flush_tlb_range(unsigned long context, unsigned long start,
			      unsigned long end);
extern void __flush_tlb_page(unsigned long context, unsigned long page);

#ifndef __SMP__

#define flush_cache_all()	__flush_cache_all()
#define flush_tlb_all()		__flush_tlb_all()

extern __inline__ void flush_tlb_mm(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT)
		__flush_tlb_mm(mm->context & 0x1fff);
}

extern __inline__ void flush_tlb_range(struct mm_struct *mm, unsigned long start,
				       unsigned long end)
{
	if(mm->context != NO_CONTEXT)
		__flush_tlb_range(mm->context & 0x1fff, start, end);
}

extern __inline__ void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if(mm->context != NO_CONTEXT)
		__flush_tlb_page(mm->context & 0x1fff, page);
}

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

extern inline pte_t mk_pte(unsigned long page, pgprot_t pgprot)
{ return __pte(__pa(page) | pgprot_val(pgprot)); }

extern inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{ return __pte(physpage | pgprot_val(pgprot)); }

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot); return pte; }

extern inline void pmd_set(pmd_t *pmdp, pte_t *ptep)
{ pmd_val(*pmdp) = __pa((unsigned long) ptep); }

extern inline void pgd_set(pgd_t *pgdp, pmd_t *pmdp)
{ pgd_val(*pgdp) = __pa((unsigned long) pmdp); }

extern inline unsigned long pte_page(pte_t pte)
{ return (unsigned long) __va((pte_val(pte) & _PFN_MASK)); }

extern inline unsigned long pmd_page(pmd_t pmd)
{ return (unsigned long) __va(pmd_val(pmd)); }

extern inline unsigned long pgd_page(pgd_t pgd)
{ return (unsigned long) __va(pgd_val(pgd)); }

#define PMD_NONE_MAGIC		0x80
#define PGD_NONE_MAGIC		0x40

extern inline int pte_none(pte_t pte) 		{ return !pte_val(pte); }
extern inline int pte_present(pte_t pte)	{ return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_clear(pte_t *pte)	{ pte_val(*pte) = 0; }

extern inline int pmd_none(pmd_t pmd)		{ return pmd_val(pmd)&PMD_NONE_MAGIC; }
extern inline int pmd_bad(pmd_t pmd)		{ return 0; }
extern inline int pmd_present(pmd_t pmd)	{ return !(pmd_val(pmd)&PMD_NONE_MAGIC);}
extern inline void pmd_clear(pmd_t *pmdp)	{ pmd_val(*pmdp) = null_pte_table; }

extern inline int pgd_none(pgd_t pgd)		{ return pgd_val(pgd) & PGD_NONE_MAGIC; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return !(pgd_val(pgd)&PGD_NONE_MAGIC);}
extern inline void pgd_clear(pgd_t *pgdp)	{ pgd_val(*pgdp) = null_pmd_table; }

/* The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_READ; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_WRITE; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_MODIFIED; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

extern inline pte_t pte_wrprotect(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_WRITE|_PAGE_W)); }

extern inline pte_t pte_rdprotect(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_READ|_PAGE_R)); }

extern inline pte_t pte_mkclean(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_MODIFIED | _PAGE_W)); }

extern inline pte_t pte_mkold(pte_t pte)
{ return __pte(pte_val(pte) & ~(_PAGE_ACCESSED | _PAGE_R)); }

extern inline pte_t pte_mkwrite(pte_t pte)
{
	if(pte_val(pte) & _PAGE_MODIFIED)
		return __pte(pte_val(pte) | (_PAGE_WRITE | _PAGE_W));
	else
		return __pte(pte_val(pte) | (_PAGE_WRITE));
}

extern inline pte_t pte_mkdirty(pte_t pte)
{
	if(pte_val(pte) & _PAGE_WRITE)
		return __pte(pte_val(pte) | (_PAGE_MODIFIED | _PAGE_W));
	else
		return __pte(pte_val(pte) | (_PAGE_MODIFIED));
}

extern inline pte_t pte_mkyoung(pte_t pte)
{
	if(pte_val(pte) & _PAGE_READ)
		return __pte(pte_val(pte) | (_PAGE_ACCESSED | _PAGE_R));
	else
		return __pte(pte_val(pte) | (_PAGE_ACCESSED));
}

/* to find an entry in a page-table-directory. */
extern inline pgd_t *pgd_offset(struct mm_struct *mm, unsigned long address)
{ return mm->pgd + ((address >> PGDIR_SHIFT) & (PTRS_PER_PAGE - 1)); }

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
extern inline pmd_t *pmd_offset(pgd_t *dir, unsigned long address)
{ return (pmd_t *) pgd_page(*dir) + ((address >> PMD_SHIFT) & (PTRS_PER_PAGE - 1)); }

/* Find an entry in the third-level page table.. */
extern inline pte_t *pte_offset(pmd_t *dir, unsigned long address)
{ return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PAGE - 1)); }

/* Very stupidly, we used to get new pgd's and pmd's, init their contents
 * to point to the NULL versions of the next level page table, later on
 * completely re-init them the same way, then free them up.  This wasted
 * a lot of work and caused unnecessary memory traffic.  How broken...
 * We fix this by caching them.
 */

#ifdef __SMP__
/* Sliiiicck */
#define pgd_quicklist		(cpu_data[smp_processor_id()].pgd_cache)
#define pmd_quicklist		(cpu_data[smp_processor_id()].pmd_cache)
#define pte_quicklist		(cpu_data[smp_processor_id()].pte_cache)
#define pgtable_cache_size	(cpu_data[smp_processor_id()].pgcache_size)
#else
extern unsigned long *pgd_quicklist;
extern unsigned long *pmd_quicklist;
extern unsigned long *pte_quicklist;
extern unsigned long pgtable_cache_size;
#endif

extern pgd_t *get_pgd_slow(void);

extern __inline__ pgd_t *get_pgd_fast(void)
{
	pgd_t *ret;

	if((ret = (pgd_t *)pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)pgd_val(*ret);
		pgd_val(ret[0]) = pgd_val(ret[1]);
		(pgtable_cache_size)--;
	} else
		ret = get_pgd_slow();
	return ret;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	pgd_val(*pgd) = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	(pgtable_cache_size)++;
}

extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long address_premasked);

extern __inline__ pmd_t *get_pmd_fast(void)
{
	pmd_t *ret;

	if((ret = (pmd_t *)pmd_quicklist) != NULL) {
		pmd_quicklist = (unsigned long *)pmd_val(*ret);
		pmd_val(ret[0]) = pmd_val(ret[1]);
		(pgtable_cache_size)--;
	}
	return ret;
}

extern __inline__ void free_pmd_fast(pgd_t *pmd)
{
	pmd_val(*pmd) = (unsigned long) pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	(pgtable_cache_size)++;
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t *get_pte_fast(void)
{
	pte_t *ret;

	if((ret = (pte_t *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)pte_val(*ret);
		pte_val(ret[0]) = pte_val(ret[1]);
		(pgtable_cache_size)--;
	}
	return ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	pte_val(*pte) = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	(pgtable_cache_size)++;
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
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
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

extern pgd_t swapper_pg_dir[1024];

extern inline void SET_PAGE_DIR(struct task_struct *tsk, pgd_t *pgdir)
{
	if(pgdir != swapper_pg_dir && tsk == current) {
		register unsigned long paddr asm("o5");

		paddr = __pa(pgdir);
		__asm__ __volatile__ ("
			rdpr		%%pstate, %%o4
			wrpr		%%o4, %1, %%pstate
			mov		%0, %%g7
			wrpr		%%o4, 0x0, %%pstate
		" : /* No outputs */
		  : "r" (paddr), "i" (PSTATE_MG|PSTATE_IE)
		  : "o4");
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

extern __inline__ void update_mmu_cache(struct vm_area_struct *vma,
					unsigned long address, pte_t pte)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long ctx = mm->context & 0x1fff;
	unsigned long tag_access;

	tag_access = address | ctx;

	__asm__ __volatile__("
	rdpr	%%pstate, %%g1
	wrpr	%%g1, %0, %%pstate
	brz,pt	%1, 1f
	 mov	%2, %%g2
	stxa	%3, [%%g2] %5
	b,pt	%%xcc, 2f
	 stxa	%4, [%%g0] %6
1:
	stxa	%3, [%%g2] %7
	stxa	%4, [%%g0] %8
2:
	wrpr	%%g1, 0x0, %%pstate
"	: /* no outputs */
	: "i" (PSTATE_IE), "r" (vma->vm_flags & VM_EXEC),
	  "i" (TLB_TAG_ACCESS), "r" (tag_access), "r" (pte_val(pte)),
	  "i" (ASI_IMMU), "i" (ASI_ITLB_DATA_IN),
	  "i" (ASI_DMMU), "i" (ASI_DTLB_DATA_IN)
	: "g1", "g2");
}

/* Make a non-present pseudo-TTE. */
extern inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = (type<<PAGE_SHIFT)|(offset<<(PAGE_SHIFT+8)); return pte; }

extern inline pte_t mk_pte_io(unsigned long page, pgprot_t prot, int space)
{ pte_t pte; pte_val(pte) = ((page) | pgprot_val(prot) | _PAGE_E) & ~(unsigned long)_PAGE_CACHE; return pte; }

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

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC64_PGTABLE_H) */
