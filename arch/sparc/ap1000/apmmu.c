  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* 
 * apmmu.c:  mmu routines for the AP1000
 *
 * based on srmmu.c 
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/kdebug.h>
#include <asm/vaddrs.h>
#include <asm/traps.h>
#include <asm/smp.h>
#include <asm/mbus.h>
#include <asm/cache.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/iommu.h>
#include <asm/asi.h>
#include <asm/msi.h>
#include <asm/a.out.h>
#include <asm/ap1000/pgtapmmu.h>
#include <asm/viking.h>


extern void mc_tlb_flush_all(void);

static void poke_viking(void);
static void viking_flush_tlb_page_for_cbit)(unsigned long page);

static struct apmmu_stats {
	int invall;
	int invpg;
	int invrnge;
	int invmm;
} module_stats;

static char *apmmu_name;

static ctxd_t *apmmu_ctx_table_phys;
static ctxd_t *apmmu_context_table;

static unsigned long ap_mem_size;
static unsigned long mempool;


static inline unsigned long apmmu_v2p(unsigned long vaddr)
{
	if (KERNBASE <= vaddr &&
	    (KERNBASE + ap_mem_size > vaddr)) {
		return (vaddr - KERNBASE);
	}
	return 0xffffffffUL;
}

static inline unsigned long apmmu_p2v(unsigned long paddr)
{
	if (ap_mem_size > paddr)
		return (paddr + KERNBASE);
	return 0xffffffffUL;
}

/* In general all page table modifications should use the V8 atomic
 * swap instruction.  This insures the mmu and the cpu are in sync
 * with respect to ref/mod bits in the page tables.
 */
static inline unsigned long apmmu_swap(unsigned long *addr, unsigned long value)
{
	/* the AP1000 has its memory on bus 8, not 0 like suns do */
	if ((value&0xF0000000) == 0)
		value |= MEM_BUS_SPACE<<28;
	__asm__ __volatile__("swap [%2], %0\n\t" :
			     "=&r" (value) :
			     "0" (value), "r" (addr));
	return value;
}

/* Functions really use this, not apmmu_swap directly. */
#define apmmu_set_entry(ptr, newentry) \
        apmmu_swap((unsigned long *) (ptr), (newentry))


/* The very generic APMMU page table operations. */
static unsigned int apmmu_pmd_align(unsigned int addr) { return APMMU_PMD_ALIGN(addr); }
static unsigned int apmmu_pgdir_align(unsigned int addr) { return APMMU_PGDIR_ALIGN(addr); }

static inline int apmmu_device_memory(unsigned long x) 
{
	return ((x & 0xF0000000) != 0);
}

static unsigned long apmmu_pgd_page(pgd_t pgd)
{ return apmmu_device_memory(pgd_val(pgd))?~0:apmmu_p2v((pgd_val(pgd) & APMMU_PTD_PMASK) << 4); }

static unsigned long apmmu_pmd_page(pmd_t pmd)
{ return apmmu_device_memory(pmd_val(pmd))?~0:apmmu_p2v((pmd_val(pmd) & APMMU_PTD_PMASK) << 4); }

static unsigned long apmmu_pte_page(pte_t pte)
{ return apmmu_device_memory(pte_val(pte))?~0:apmmu_p2v((pte_val(pte) & APMMU_PTE_PMASK) << 4); }

static int apmmu_pte_none(pte_t pte)          
{ return !(pte_val(pte) & 0xFFFFFFF); }

static int apmmu_pte_present(pte_t pte)
{ return ((pte_val(pte) & APMMU_ET_MASK) == APMMU_ET_PTE); }

static void apmmu_pte_clear(pte_t *ptep)      { set_pte(ptep, __pte(0)); }

static int apmmu_pmd_none(pmd_t pmd)          
{ return !(pmd_val(pmd) & 0xFFFFFFF); }

static int apmmu_pmd_bad(pmd_t pmd)
{ return (pmd_val(pmd) & APMMU_ET_MASK) != APMMU_ET_PTD; }

static int apmmu_pmd_present(pmd_t pmd)
{ return ((pmd_val(pmd) & APMMU_ET_MASK) == APMMU_ET_PTD); }

static void apmmu_pmd_clear(pmd_t *pmdp)      { set_pte((pte_t *)pmdp, __pte(0)); }

static int apmmu_pgd_none(pgd_t pgd)          
{ return !(pgd_val(pgd) & 0xFFFFFFF); }

static int apmmu_pgd_bad(pgd_t pgd)
{ return (pgd_val(pgd) & APMMU_ET_MASK) != APMMU_ET_PTD; }

static int apmmu_pgd_present(pgd_t pgd)
{ return ((pgd_val(pgd) & APMMU_ET_MASK) == APMMU_ET_PTD); }

static void apmmu_pgd_clear(pgd_t * pgdp)     { set_pte((pte_t *)pgdp, __pte(0)); }

static pte_t apmmu_pte_mkwrite(pte_t pte)     { return __pte(pte_val(pte) | APMMU_WRITE);}
static pte_t apmmu_pte_mkdirty(pte_t pte)     { return __pte(pte_val(pte) | APMMU_DIRTY);}
static pte_t apmmu_pte_mkyoung(pte_t pte)     { return __pte(pte_val(pte) | APMMU_REF);}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static pte_t apmmu_mk_pte(unsigned long page, pgprot_t pgprot)
{ return __pte(((apmmu_v2p(page)) >> 4) | pgprot_val(pgprot)); }

static pte_t apmmu_mk_pte_phys(unsigned long page, pgprot_t pgprot)
{ return __pte(((page) >> 4) | pgprot_val(pgprot)); }

static pte_t apmmu_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{
	return __pte(((page) >> 4) | (space << 28) | pgprot_val(pgprot));
}

static void apmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ 
	set_pte((pte_t *)ctxp, (APMMU_ET_PTD | (apmmu_v2p((unsigned long) pgdp) >> 4)));
}

static void apmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	set_pte((pte_t *)pgdp, (APMMU_ET_PTD | (apmmu_v2p((unsigned long) pmdp) >> 4)));
}

static void apmmu_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	set_pte((pte_t *)pmdp, (APMMU_ET_PTD | (apmmu_v2p((unsigned long) ptep) >> 4)));
}

static pte_t apmmu_pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & APMMU_CHG_MASK) | pgprot_val(newprot));
}

/* to find an entry in a top-level page table... */
static pgd_t *apmmu_pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + ((address >> APMMU_PGDIR_SHIFT) & (APMMU_PTRS_PER_PGD - 1));
}

/* Find an entry in the second-level page table.. */
static pmd_t *apmmu_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) apmmu_pgd_page(*dir) + ((address >> APMMU_PMD_SHIFT) & (APMMU_PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
static pte_t *apmmu_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) apmmu_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (APMMU_PTRS_PER_PTE - 1));
}

/* This must update the context table entry for this process. */
static void apmmu_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	if(tsk->mm->context != NO_CONTEXT) {
		flush_cache_mm(current->mm);
		apmmu_ctxd_set(&apmmu_context_table[tsk->mm->context], pgdp);
		flush_tlb_mm(current->mm);
	}
}


/* Accessing the MMU control register. */
static inline unsigned int apmmu_get_mmureg(void)
{
        unsigned int retval;
	__asm__ __volatile__("lda [%%g0] %1, %0\n\t" :
			     "=r" (retval) :
			     "i" (ASI_M_MMUREGS));
	return retval;
}

static inline void apmmu_set_mmureg(unsigned long regval)
{
	__asm__ __volatile__("sta %0, [%%g0] %1\n\t" : :
			     "r" (regval), "i" (ASI_M_MMUREGS) : "memory");

}

static inline void apmmu_set_ctable_ptr(unsigned long paddr)
{
	paddr = ((paddr >> 4) & APMMU_CTX_PMASK);
        paddr |= (MEM_BUS_SPACE<<28);
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (paddr), "r" (APMMU_CTXTBL_PTR),
			     "i" (ASI_M_MMUREGS) :
			     "memory");
}

static inline void apmmu_flush_whole_tlb(void)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t": :
			     "r" (0x400),        /* Flush entire TLB!! */
			     "i" (ASI_M_FLUSH_PROBE) : "memory");

}

/* These flush types are not available on all chips... */
static inline void apmmu_flush_tlb_ctx(void)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t": :
			     "r" (0x300),        /* Flush TLB ctx.. */
			     "i" (ASI_M_FLUSH_PROBE) : "memory");

}

static inline void apmmu_flush_tlb_region(unsigned long addr)
{
	addr &= APMMU_PGDIR_MASK;
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t": :
			     "r" (addr | 0x200), /* Flush TLB region.. */
			     "i" (ASI_M_FLUSH_PROBE) : "memory");

}


static inline void apmmu_flush_tlb_segment(unsigned long addr)
{
	addr &= APMMU_PMD_MASK;
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t": :
			     "r" (addr | 0x100), /* Flush TLB segment.. */
			     "i" (ASI_M_FLUSH_PROBE) : "memory");

}

static inline void apmmu_flush_tlb_page(unsigned long page)
{
	page &= PAGE_MASK;
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t": :
			     "r" (page),        /* Flush TLB page.. */
			     "i" (ASI_M_FLUSH_PROBE) : "memory");

}

static inline unsigned long apmmu_hwprobe(unsigned long vaddr)
{
	unsigned long retval;

	vaddr &= PAGE_MASK;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (retval) :
			     "r" (vaddr | 0x400), "i" (ASI_M_FLUSH_PROBE));

	return retval;
}

static inline void apmmu_uncache_page(unsigned long addr)
{
	pgd_t *pgdp = apmmu_pgd_offset(init_task.mm, addr);
	pmd_t *pmdp;
	pte_t *ptep;

	if((pgd_val(*pgdp) & APMMU_ET_MASK) == APMMU_ET_PTE) {
		ptep = (pte_t *) pgdp;
	} else {
		pmdp = apmmu_pmd_offset(pgdp, addr);
		if((pmd_val(*pmdp) & APMMU_ET_MASK) == APMMU_ET_PTE) {
			ptep = (pte_t *) pmdp;
		} else {
			ptep = apmmu_pte_offset(pmdp, addr);
		}
	}

	set_pte(ptep, __pte((pte_val(*ptep) & ~APMMU_CACHE)));
	viking_flush_tlb_page_for_cbit(addr);
}

static inline void apmmu_recache_page(unsigned long addr)
{
	pgd_t *pgdp = apmmu_pgd_offset(init_task.mm, addr);
	pmd_t *pmdp;
	pte_t *ptep;

	if((pgd_val(*pgdp) & APMMU_ET_MASK) == APMMU_ET_PTE) {
		ptep = (pte_t *) pgdp;
	} else {
		pmdp = apmmu_pmd_offset(pgdp, addr);
		if((pmd_val(*pmdp) & APMMU_ET_MASK) == APMMU_ET_PTE) {
			ptep = (pte_t *) pmdp;
		} else {
			ptep = apmmu_pte_offset(pmdp, addr);
		}
	}
	set_pte(ptep, __pte((pte_val(*ptep) | APMMU_CACHE)));
	viking_flush_tlb_page_for_cbit(addr);
}

static inline unsigned long apmmu_getpage(void)
{
	unsigned long page = get_free_page(GFP_KERNEL);

	return page;
}

static inline void apmmu_putpage(unsigned long page)
{
	free_page(page);
}

/* The easy versions. */
#define NEW_PGD() (pgd_t *) apmmu_getpage()
#define NEW_PMD() (pmd_t *) apmmu_getpage()
#define NEW_PTE() (pte_t *) apmmu_getpage()
#define FREE_PGD(chunk) apmmu_putpage((unsigned long)(chunk))
#define FREE_PMD(chunk) apmmu_putpage((unsigned long)(chunk))
#define FREE_PTE(chunk) apmmu_putpage((unsigned long)(chunk))

static pte_t *apmmu_get_pte_fast(void)
{
	return (pte_t *)0;
}

static pmd_t *apmmu_get_pmd_fast(void)
{
	return (pmd_t *)0;
}

static pgd_t *apmmu_get_pgd_fast(void)
{
	return (pgd_t *)0;
}

static void apmmu_free_pte_slow(pte_t *pte)
{
/* TBD */
}

static void apmmu_free_pmd_slow(pmd_t *pmd)
{
/* TBD */
}

static void apmmu_free_pgd_slow(pgd_t *pgd)
{
/* TBD */
}


/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
static void apmmu_pte_free_kernel(pte_t *pte)
{
	FREE_PTE(pte);
}

static pte_t *apmmu_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (APMMU_PTRS_PER_PTE - 1);
	if(apmmu_pmd_none(*pmd)) {
		pte_t *page = NEW_PTE();
		if(apmmu_pmd_none(*pmd)) {
			if(page) {
				apmmu_pmd_set(pmd, page);
				return page + address;
			}
			apmmu_pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		FREE_PTE(page);
	}
	if(apmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		apmmu_pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) apmmu_pmd_page(*pmd) + address;
}

static void apmmu_pmd_free_kernel(pmd_t *pmd)
{
	FREE_PMD(pmd);
}

static pmd_t *apmmu_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	address = (address >> APMMU_PMD_SHIFT) & (APMMU_PTRS_PER_PMD - 1);
	if(apmmu_pgd_none(*pgd)) {
		pmd_t *page;
		page = NEW_PMD();
		if(apmmu_pgd_none(*pgd)) {
			if(page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		FREE_PMD(page);
	}
	if(apmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

static void apmmu_pte_free(pte_t *pte)
{
	FREE_PTE(pte);
}

static pte_t *apmmu_pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (APMMU_PTRS_PER_PTE - 1);
	if(apmmu_pmd_none(*pmd)) {
		pte_t *page = NEW_PTE();
		if(apmmu_pmd_none(*pmd)) {
			if(page) {
				apmmu_pmd_set(pmd, page);
				return page + address;
			}
			apmmu_pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		FREE_PTE(page);
	}
	if(apmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		apmmu_pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return ((pte_t *) apmmu_pmd_page(*pmd)) + address;
}

/* Real three-level page tables on APMMU. */
static void apmmu_pmd_free(pmd_t * pmd)
{
	FREE_PMD(pmd);
}

static pmd_t *apmmu_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> APMMU_PMD_SHIFT) & (APMMU_PTRS_PER_PMD - 1);
	if(apmmu_pgd_none(*pgd)) {
		pmd_t *page = NEW_PMD();
		if(apmmu_pgd_none(*pgd)) {
			if(page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		FREE_PMD(page);
	}
	if(apmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) apmmu_pgd_page(*pgd) + address;
}

static void apmmu_pgd_free(pgd_t *pgd)
{
	FREE_PGD(pgd);
}

static pgd_t *apmmu_pgd_alloc(void)
{
	return NEW_PGD();
}

static void apmmu_pgd_flush(pgd_t *pgdp)
{
}

static void apmmu_set_pte_cacheable(pte_t *ptep, pte_t pteval)
{
	apmmu_set_entry(ptep, pte_val(pteval));
}

static void apmmu_quick_kernel_fault(unsigned long address)
{
	printk("Kernel faults at addr=0x%08lx\n", address);
	printk("PTE=%08lx\n", apmmu_hwprobe((address & PAGE_MASK)));
	die_if_kernel("APMMU bolixed...", current->tss.kregs);
}

static inline void alloc_context(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;
	struct ctx_list *ctxp;

	if (tsk->taskid >= MPP_TASK_BASE) {
		mm->context = MPP_CONTEXT_BASE + (tsk->taskid - MPP_TASK_BASE);
		return;
	}

	ctxp = ctx_free.next;
	if(ctxp != &ctx_free) {
		remove_from_ctx_list(ctxp);
		add_to_used_ctxlist(ctxp);
		mm->context = ctxp->ctx_number;
		ctxp->ctx_mm = mm;
		return;
	}
	ctxp = ctx_used.next;
	if(ctxp->ctx_mm == current->mm)
		ctxp = ctxp->next;
	if(ctxp == &ctx_used)
		panic("out of mmu contexts");
	flush_cache_mm(ctxp->ctx_mm);
	flush_tlb_mm(ctxp->ctx_mm);
	remove_from_ctx_list(ctxp);
	add_to_used_ctxlist(ctxp);
	ctxp->ctx_mm->context = NO_CONTEXT;
	ctxp->ctx_mm = mm;
	mm->context = ctxp->ctx_number;
}

static inline void free_context(int context)
{
	struct ctx_list *ctx_old;

	if (context >= MPP_CONTEXT_BASE)
		return; /* nothing to do! */
	
	ctx_old = ctx_list_pool + context;
	remove_from_ctx_list(ctx_old);
	add_to_free_ctxlist(ctx_old);
}


static void apmmu_switch_to_context(struct task_struct *tsk)
{
	if(tsk->mm->context == NO_CONTEXT) {
		alloc_context(tsk);
		flush_cache_mm(current->mm);
		apmmu_ctxd_set(&apmmu_context_table[tsk->mm->context], tsk->mm->pgd);
		flush_tlb_mm(current->mm);
	}
	apmmu_set_context(tsk->mm->context);
}

static char *apmmu_lockarea(char *vaddr, unsigned long len)
{
	return vaddr;
}

static void apmmu_unlockarea(char *vaddr, unsigned long len)
{
}

struct task_struct *apmmu_alloc_task_struct(void)
{
	return (struct task_struct *) kmalloc(sizeof(struct task_struct), GFP_KERNEL);
}

static void apmmu_free_task_struct(struct task_struct *tsk)
{
	kfree(tsk);
}

static void apmmu_null_func(void)
{
}

static inline void mc_tlb_flush_all(void)
{
	unsigned long long *tlb4k;
	int i;
	
	tlb4k = (unsigned long long *)MC_MMU_TLB4K;
	for (i = MC_MMU_TLB4K_SIZE/4; i > 0; --i) {
		tlb4k[0] = 0;
		tlb4k[1] = 0;
		tlb4k[2] = 0;
		tlb4k[3] = 0;
		tlb4k += 4;
	}
}

static inline void mc_tlb_flush_page(unsigned vaddr,int ctx) 
{
	if (ctx == SYSTEM_CONTEXT || MPP_IS_PAR_CTX(ctx)) {
		*(((unsigned long long *)MC_MMU_TLB4K) + ((vaddr>>12)&0xFF)) = 0;
	}
}

static inline void mc_tlb_flush_ctx(int ctx) 
{
	unsigned long long *tlb4k = (unsigned long long *)MC_MMU_TLB4K;
	if (ctx == SYSTEM_CONTEXT || MPP_IS_PAR_CTX(ctx)) {
		int i;
		for (i=0; i<MC_MMU_TLB4K_SIZE;i++)
			if (((tlb4k[i] >> 5) & 0xFFF) == ctx) tlb4k[i] = 0;
	}
}

static inline void mc_tlb_flush_region(unsigned start,int ctx)
{
	mc_tlb_flush_ctx(ctx);
}

static inline void mc_tlb_flush_segment(unsigned start,int ctx)
{
	mc_tlb_flush_ctx(ctx);
}

static void viking_flush_tlb_all(void)
{
	module_stats.invall++;
	flush_user_windows(); 	
	apmmu_flush_whole_tlb();
	mc_tlb_flush_all();
}

static void viking_flush_tlb_mm(struct mm_struct *mm)
{
	int octx;

	module_stats.invmm++;

	if(mm->context != NO_CONTEXT) {
		flush_user_windows();
		octx = apmmu_get_context();
		if (octx != mm->context) 
			apmmu_set_context(mm->context);
		apmmu_flush_tlb_ctx();
		mc_tlb_flush_ctx(mm->context);
		if (octx != mm->context)
			apmmu_set_context(octx);
	}
}

static void viking_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int octx;

	module_stats.invrnge++;

	if(mm->context != NO_CONTEXT) {
		flush_user_windows();
		octx = apmmu_get_context();
		if (octx != mm->context)
			apmmu_set_context(mm->context);
		if((end - start) < APMMU_PMD_SIZE) {
			start &= PAGE_MASK;
			while(start < end) {
				apmmu_flush_tlb_page(start);
				mc_tlb_flush_page(start,mm->context);
				start += PAGE_SIZE;
			}
		} else if((end - start) < APMMU_PGDIR_SIZE) {
			start &= APMMU_PMD_MASK;
			while(start < end) {
				apmmu_flush_tlb_segment(start);
				mc_tlb_flush_segment(start,mm->context);
				start += APMMU_PMD_SIZE;
			}
		} else {
			start &= APMMU_PGDIR_MASK;
			while(start < end) {
				apmmu_flush_tlb_region(start);
				mc_tlb_flush_region(start,mm->context);
				start += APMMU_PGDIR_SIZE;
			}
		}
		if (octx != mm->context)
			apmmu_set_context(octx);
	}
}

static void viking_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int octx;
	struct mm_struct *mm = vma->vm_mm;

	module_stats.invpg++;
	if(mm->context != NO_CONTEXT) {
		flush_user_windows();
		octx = apmmu_get_context();
		if (octx != mm->context)
			apmmu_set_context(mm->context);
		apmmu_flush_tlb_page(page);
		mc_tlb_flush_page(page,mm->context);
		if (octx != mm->context)
			apmmu_set_context(octx);
	}
}

static void viking_flush_tlb_page_for_cbit(unsigned long page)
{
	apmmu_flush_tlb_page(page);
	mc_tlb_flush_page(page,apmmu_get_context());
}

/* Some dirty hacks to abstract away the painful boot up init. */
static inline unsigned long apmmu_early_paddr(unsigned long vaddr)
{
	return (vaddr - KERNBASE);
}

static inline void apmmu_early_pgd_set(pgd_t *pgdp, pmd_t *pmdp)
{
	set_pte((pte_t *)pgdp, __pte((APMMU_ET_PTD | (apmmu_early_paddr((unsigned long) pmdp) >> 4))));
}

static inline void apmmu_early_pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	set_pte((pte_t *)pmdp, __pte((APMMU_ET_PTD | (apmmu_early_paddr((unsigned long) ptep) >> 4))));
}

static inline unsigned long apmmu_early_pgd_page(pgd_t pgd)
{
	return ((pgd_val(pgd) & APMMU_PTD_PMASK) << 4) + KERNBASE;
}

static inline unsigned long apmmu_early_pmd_page(pmd_t pmd)
{
	return ((pmd_val(pmd) & APMMU_PTD_PMASK) << 4) + KERNBASE;
}

static inline pmd_t *apmmu_early_pmd_offset(pgd_t *dir, unsigned long address)
{
	return (pmd_t *) apmmu_early_pgd_page(*dir) + ((address >> APMMU_PMD_SHIFT) & (APMMU_PTRS_PER_PMD - 1));
}

static inline pte_t *apmmu_early_pte_offset(pmd_t *dir, unsigned long address)
{
	return (pte_t *) apmmu_early_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (APMMU_PTRS_PER_PTE - 1));
}

__initfunc(static inline void apmmu_allocate_ptable_skeleton(unsigned long start, unsigned long end))
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	while(start < end) {
		pgdp = apmmu_pgd_offset(init_task.mm, start);
		if(apmmu_pgd_none(*pgdp)) {
			pmdp = sparc_init_alloc(&mempool, APMMU_PMD_TABLE_SIZE);
			apmmu_early_pgd_set(pgdp, pmdp);
		}
		pmdp = apmmu_early_pmd_offset(pgdp, start);
		if(apmmu_pmd_none(*pmdp)) {
			ptep = sparc_init_alloc(&mempool, APMMU_PTE_TABLE_SIZE);
			apmmu_early_pmd_set(pmdp, ptep);
		}
		start = (start + APMMU_PMD_SIZE) & APMMU_PMD_MASK;
	}
}


__initfunc(static void make_page(unsigned virt_page, unsigned phys_page, unsigned prot))
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned start = virt_page<<12;

	pgdp = apmmu_pgd_offset(init_task.mm, start);
	if(apmmu_pgd_none(*pgdp)) {
		pmdp = sparc_init_alloc(&mempool, APMMU_PMD_TABLE_SIZE);
		apmmu_early_pgd_set(pgdp, pmdp);
	}
	pmdp = apmmu_early_pmd_offset(pgdp, start);
	if(apmmu_pmd_none(*pmdp)) {
		ptep = sparc_init_alloc(&mempool, APMMU_PTE_TABLE_SIZE);
		apmmu_early_pmd_set(pmdp, ptep);
	}
	ptep = apmmu_early_pte_offset(pmdp, start);
	*ptep = __pte((phys_page<<8) | prot);
}


__initfunc(static void make_large_page(unsigned virt_page, unsigned phys_page, unsigned prot))
{
	pgd_t *pgdp;
	unsigned start = virt_page<<12;

	pgdp = apmmu_pgd_offset(init_task.mm, start);
	*pgdp = __pgd((phys_page<<8) | prot);
}


__initfunc(static void ap_setup_mappings(void))
{
	unsigned Srwe = APMMU_PRIV | APMMU_VALID;
	unsigned SrweUr = 0x14 | APMMU_VALID; /* weird! */

	/* LBus */
	make_large_page(0xfb000,0x9fb000,Srwe);
	make_large_page(0xff000,0x9ff000,SrweUr); 
	make_large_page(0xfc000,0x911000,Srwe);

	/* MC Register */
	make_page(0xfa000,0xb00000,SrweUr);
	make_page(0xfa001,0xb00001,Srwe);
	make_page(0xfa002,0xb00002,Srwe);
	make_page(0xfa003,0xb00003,Srwe);
	make_page(0xfa004,0xb00004,Srwe);
	make_page(0xfa005,0xb00005,Srwe);
	make_page(0xfa006,0xb00006,Srwe);
	make_page(0xfa007,0xb00007,Srwe);
  
	/* MSC+ Register */
	make_page(0xfa008,0xc00000,SrweUr);
	make_page(0xfa009,0xc00001,Srwe);
	make_page(0xfa00a,0xc00002,Srwe);
	make_page(0xfa00b,0xc00003,Srwe);
	make_page(0xfa00c,0xc00004,Srwe);
	make_page(0xfa00d,0xc00005,Srwe); /* RBMPR 0 */
	make_page(0xfa00e,0xc00006,Srwe);  /* RBMPR 1 */
	make_page(0xfa00f,0xc00007,Srwe);  /* RBMPR 2 */

	/* user queues */
	make_page(MSC_PUT_QUEUE>>PAGE_SHIFT,  0xa00000,Srwe); 
	make_page(MSC_GET_QUEUE>>PAGE_SHIFT,  0xa00001,Srwe); 
	make_page(MSC_SEND_QUEUE>>PAGE_SHIFT, 0xa00040,Srwe); 
	make_page(MSC_XY_QUEUE>>PAGE_SHIFT,   0xa00640,Srwe); 
	make_page(MSC_X_QUEUE>>PAGE_SHIFT,    0xa00240,Srwe); 
	make_page(MSC_Y_QUEUE>>PAGE_SHIFT,    0xa00440,Srwe); 
	make_page(MSC_XYG_QUEUE>>PAGE_SHIFT,  0xa00600,Srwe); 
	make_page(MSC_XG_QUEUE>>PAGE_SHIFT,   0xa00200,Srwe); 
	make_page(MSC_YG_QUEUE>>PAGE_SHIFT,   0xa00400,Srwe); 
	make_page(MSC_CSI_QUEUE>>PAGE_SHIFT,  0xa02004,Srwe); 
	make_page(MSC_FOP_QUEUE>>PAGE_SHIFT,  0xa02005,Srwe); 
	
	/* system queues */
	make_page(MSC_PUT_QUEUE_S>>PAGE_SHIFT,  0xa02000,Srwe); /* system put */
	make_page(MSC_CPUT_QUEUE_S>>PAGE_SHIFT, 0xa02020,Srwe); /* system creg put */
	make_page(MSC_GET_QUEUE_S>>PAGE_SHIFT,  0xa02001,Srwe); /* system get */
	make_page(MSC_CGET_QUEUE_S>>PAGE_SHIFT, 0xa02021,Srwe); /* system creg get */
	make_page(MSC_SEND_QUEUE_S>>PAGE_SHIFT, 0xa02040,Srwe); /* system send */
	make_page(MSC_BSEND_QUEUE_S>>PAGE_SHIFT,0xa02640,Srwe); /* system send broad */
	make_page(MSC_XYG_QUEUE_S>>PAGE_SHIFT,  0xa02600,Srwe); /* system put broad */  
	make_page(MSC_CXYG_QUEUE_S>>PAGE_SHIFT, 0xa02620,Srwe); /* system put broad */  
	
	/* Direct queue access entries for refilling the MSC send queue */
	make_page(MSC_SYSTEM_DIRECT>>PAGE_SHIFT, 0xa08000,Srwe);
	make_page(MSC_USER_DIRECT>>PAGE_SHIFT, 0xa08001,Srwe);
	make_page(MSC_REMOTE_DIRECT>>PAGE_SHIFT, 0xa08002,Srwe);
	make_page(MSC_REPLY_DIRECT>>PAGE_SHIFT, 0xa08003,Srwe);
	make_page(MSC_REMREPLY_DIRECT>>PAGE_SHIFT, 0xa08004,Srwe);

	/* As above with end-bit set */
	make_page(MSC_SYSTEM_DIRECT_END>>PAGE_SHIFT, 0xa0c000,Srwe);
	make_page(MSC_USER_DIRECT_END>>PAGE_SHIFT, 0xa0c001,Srwe);
	make_page(MSC_REMOTE_DIRECT_END>>PAGE_SHIFT, 0xa0c002,Srwe);
	make_page(MSC_REPLY_DIRECT_END>>PAGE_SHIFT, 0xa0c003,Srwe);
	make_page(MSC_REMREPLY_DIRECT_END>>PAGE_SHIFT, 0xa0c004,Srwe);
}

__initfunc(static void map_kernel(void))
{
	int phys;

	/* the AP+ only ever has one bank of memory starting at address 0 */
	ap_mem_size = sp_banks[0].num_bytes;
	for (phys=0; phys < sp_banks[0].num_bytes; phys += APMMU_PGDIR_SIZE)
		make_large_page((KERNBASE+phys)>>12,
				(phys>>12),
				APMMU_CACHE|APMMU_PRIV|APMMU_VALID);
	init_task.mm->mmap->vm_start = page_offset = KERNBASE;
	stack_top = page_offset - PAGE_SIZE;
}

extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long sparc_context_init(unsigned long, int);

extern int physmem_mapped_contig;
extern int linux_num_cpus;

__initfunc(unsigned long apmmu_paging_init(unsigned long start_mem, unsigned long end_mem))
{
	int i;

	physmem_mapped_contig = 1;   /* for init.c:taint_real_pages()   */

	num_contexts = AP_NUM_CONTEXTS;
	mempool = PAGE_ALIGN(start_mem);
	memset(swapper_pg_dir, 0, PAGE_SIZE);

	apmmu_allocate_ptable_skeleton(KERNBASE, end_mem);
	mempool = PAGE_ALIGN(mempool);
	map_kernel();
	ap_setup_mappings();

	/* the MSC wants this aligned on a 16k boundary */
	apmmu_context_table = 
	  sparc_init_alloc(&mempool, 
			   num_contexts*sizeof(ctxd_t)<0x4000?
			   0x4000:
			   num_contexts*sizeof(ctxd_t));
	apmmu_ctx_table_phys = (ctxd_t *) apmmu_v2p((unsigned long) apmmu_context_table);
	for(i = 0; i < num_contexts; i++)
		apmmu_ctxd_set(&apmmu_context_table[i], swapper_pg_dir);

	start_mem = PAGE_ALIGN(mempool);

	flush_cache_all();
	apmmu_set_ctable_ptr((unsigned long) apmmu_ctx_table_phys);
	flush_tlb_all();
	poke_viking();

	/* on the AP we don't put the top few contexts into the free
	   context list as these are reserved for parallel tasks */
	start_mem = sparc_context_init(start_mem, MPP_CONTEXT_BASE);
	start_mem = free_area_init(start_mem, end_mem);

	return PAGE_ALIGN(start_mem);
}

static int apmmu_mmu_info(char *buf)
{
	return sprintf(buf, 
		"MMU type\t: %s\n"
		"invall\t\t: %d\n"
		"invmm\t\t: %d\n"
		"invrnge\t\t: %d\n"
		"invpg\t\t: %d\n"
		"contexts\t: %d\n"
		, apmmu_name,
		module_stats.invall,
		module_stats.invmm,
		module_stats.invrnge,
		module_stats.invpg,
		num_contexts
		);
}

static void apmmu_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
}

__initfunc(static void poke_viking(void))
{
	unsigned long mreg = apmmu_get_mmureg();

	mreg |= VIKING_SPENABLE;
	mreg |= (VIKING_ICENABLE | VIKING_DCENABLE);
	mreg &= ~VIKING_ACENABLE;
	mreg &= ~VIKING_SBENABLE;
	mreg |= VIKING_TCENABLE;
	apmmu_set_mmureg(mreg);
}

__initfunc(static void init_viking(void))
{
	apmmu_name = "TI Viking/AP1000";

	BTFIXUPSET_CALL(flush_cache_all, apmmu_null_func, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_cache_mm, apmmu_null_func, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_cache_page, apmmu_null_func, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_cache_range, apmmu_null_func, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(flush_tlb_all, viking_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, viking_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, viking_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, viking_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_page_to_ram, apmmu_null_func, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, apmmu_null_func, BTFIXUPCALL_NOP);
}


extern unsigned long spwin_mmu_patchme, fwin_mmu_patchme,
	tsetup_mmu_patchme, rtrap_mmu_patchme;

extern unsigned long spwin_srmmu_stackchk, srmmu_fwin_stackchk,
	tsetup_srmmu_stackchk, srmmu_rett_stackchk;

extern unsigned long srmmu_fault;

#define PATCH_BRANCH(insn, dest) do { \
		iaddr = &(insn); \
		daddr = &(dest); \
		*iaddr = SPARC_BRANCH((unsigned long) daddr, (unsigned long) iaddr); \
	} while(0);

__initfunc(static void patch_window_trap_handlers(void))
{
	unsigned long *iaddr, *daddr;
	
	PATCH_BRANCH(spwin_mmu_patchme, spwin_srmmu_stackchk);
	PATCH_BRANCH(fwin_mmu_patchme, srmmu_fwin_stackchk);
	PATCH_BRANCH(tsetup_mmu_patchme, tsetup_srmmu_stackchk);
	PATCH_BRANCH(rtrap_mmu_patchme, srmmu_rett_stackchk);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_TFLT].inst_three, srmmu_fault);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_DFLT].inst_three, srmmu_fault);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_DACC].inst_three, srmmu_fault);
}

/* Load up routines and constants for apmmu */
__initfunc(void ld_mmu_apmmu(void))
{
	/* First the constants */
	BTFIXUPSET_SIMM13(pmd_shift, APMMU_PMD_SHIFT);
	BTFIXUPSET_SETHI(pmd_size, APMMU_PMD_SIZE);
	BTFIXUPSET_SETHI(pmd_mask, APMMU_PMD_MASK);
	BTFIXUPSET_SIMM13(pgdir_shift, APMMU_PGDIR_SHIFT);
	BTFIXUPSET_SETHI(pgdir_size, APMMU_PGDIR_SIZE);
	BTFIXUPSET_SETHI(pgdir_mask, APMMU_PGDIR_MASK);

	BTFIXUPSET_SIMM13(ptrs_per_pte, APMMU_PTRS_PER_PTE);
	BTFIXUPSET_SIMM13(ptrs_per_pmd, APMMU_PTRS_PER_PMD);
	BTFIXUPSET_SIMM13(ptrs_per_pgd, APMMU_PTRS_PER_PGD);

	BTFIXUPSET_INT(page_none, pgprot_val(APMMU_PAGE_NONE));
	BTFIXUPSET_INT(page_shared, pgprot_val(APMMU_PAGE_SHARED));
	BTFIXUPSET_INT(page_copy, pgprot_val(APMMU_PAGE_COPY));
	BTFIXUPSET_INT(page_readonly, pgprot_val(APMMU_PAGE_RDONLY));
	BTFIXUPSET_INT(page_kernel, pgprot_val(APMMU_PAGE_KERNEL));
	pg_iobits = APMMU_VALID | APMMU_WRITE | APMMU_REF;
	    
	/* Functions */
	BTFIXUPSET_CALL(get_pte_fast, apmmu_get_pte_fast, BTFIXUPCALL_RETINT(0));
	BTFIXUPSET_CALL(get_pmd_fast, apmmu_get_pmd_fast, BTFIXUPCALL_RETINT(0));
	BTFIXUPSET_CALL(get_pgd_fast, apmmu_get_pgd_fast, BTFIXUPCALL_RETINT(0));
	BTFIXUPSET_CALL(free_pte_slow, apmmu_free_pte_slow, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(free_pmd_slow, apmmu_free_pmd_slow, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(free_pgd_slow, apmmu_free_pgd_slow, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(set_pte, apmmu_set_pte_cacheable, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(switch_to_context, apmmu_switch_to_context, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(pte_page, apmmu_pte_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_page, apmmu_pmd_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_page, apmmu_pgd_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(sparc_update_rootmmu_dir, apmmu_update_rootmmu_dir, BTFIXUPCALL_NORM);

	BTFIXUPSET_SETHI(none_mask, 0xF0000000);

	BTFIXUPSET_CALL(pte_present, apmmu_pte_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_clear, apmmu_pte_clear, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(pmd_bad, apmmu_pmd_bad, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_present, apmmu_pmd_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_clear, apmmu_pmd_clear, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(pgd_none, apmmu_pgd_none, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_bad, apmmu_pgd_bad, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_present, apmmu_pgd_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_clear, apmmu_pgd_clear, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(mk_pte, apmmu_mk_pte, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mk_pte_phys, apmmu_mk_pte_phys, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mk_pte_io, apmmu_mk_pte_io, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_set, apmmu_pgd_set, BTFIXUPCALL_NORM);
	
	BTFIXUPSET_INT(pte_modify_mask, APMMU_CHG_MASK);
	BTFIXUPSET_CALL(pgd_offset, apmmu_pgd_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_offset, apmmu_pmd_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_offset, apmmu_pte_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_free_kernel, apmmu_pte_free_kernel, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_free_kernel, apmmu_pmd_free_kernel, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_alloc_kernel, apmmu_pte_alloc_kernel, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_alloc_kernel, apmmu_pmd_alloc_kernel, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_free, apmmu_pte_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_alloc, apmmu_pte_alloc, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_free, apmmu_pmd_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_alloc, apmmu_pmd_alloc, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_free, apmmu_pgd_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_alloc, apmmu_pgd_alloc, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_flush, apmmu_pgd_flush, BTFIXUPCALL_NORM);

	BTFIXUPSET_HALF(pte_writei, APMMU_WRITE);
	BTFIXUPSET_HALF(pte_dirtyi, APMMU_DIRTY);
	BTFIXUPSET_HALF(pte_youngi, APMMU_REF);
	BTFIXUPSET_HALF(pte_wrprotecti, APMMU_WRITE);
	BTFIXUPSET_HALF(pte_mkcleani, APMMU_DIRTY);
	BTFIXUPSET_HALF(pte_mkoldi, APMMU_REF);
	BTFIXUPSET_CALL(pte_mkwrite, apmmu_pte_mkwrite, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_mkdirty, apmmu_pte_mkdirty, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_mkyoung, apmmu_pte_mkyoung, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(update_mmu_cache, apmmu_update_mmu_cache, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(mmu_lockarea, apmmu_lockarea, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_unlockarea, apmmu_unlockarea, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(mmu_get_scsi_one, apmmu_null_func, BTFIXUPCALL_RETO0);
	BTFIXUPSET_CALL(mmu_get_scsi_sgl, apmmu_null_func, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(mmu_release_scsi_one, apmmu_null_func, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(mmu_release_scsi_sgl, apmmu_null_func, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(mmu_info, apmmu_mmu_info, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_v2p, apmmu_v2p, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_p2v, apmmu_p2v, BTFIXUPCALL_NORM);

	/* Task struct and kernel stack allocating/freeing. */
	BTFIXUPSET_CALL(alloc_task_struct, apmmu_alloc_task_struct, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(free_task_struct, apmmu_free_task_struct, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(quick_kernel_fault, apmmu_quick_kernel_fault, BTFIXUPCALL_NORM);

	init_viking();
	patch_window_trap_handlers();
}
