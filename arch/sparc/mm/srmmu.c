/* $Id: srmmu.c,v 1.34 1996/03/01 07:16:23 davem Exp $
 * srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Peter A. Zaitcev (zaitcev@ithil.mcst.ru)
 */

#include <linux/kernel.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/kdebug.h>
#include <asm/vaddrs.h>
#include <asm/traps.h>
#include <asm/mp.h>
#include <asm/mbus.h>
#include <asm/cache.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/iommu.h>

/* Now the cpu specific definitions. */
#include <asm/viking.h>
#include <asm/ross.h>
#include <asm/tsunami.h>
#include <asm/swift.h>

enum mbus_module srmmu_modtype;
unsigned int hwbug_bitmask;

int hyper_cache_size;

ctxd_t *srmmu_context_table;

/* In general all page table modifications should use the V8 atomic
 * swap instruction.  This insures the mmu and the cpu are in sync
 * with respect to ref/mod bits in the page tables.
 */
static inline unsigned long srmmu_swap(unsigned long *addr, unsigned long value)
{
	__asm__ __volatile__("swap [%1], %0\n\t" :
			     "=&r" (value), "=&r" (addr) :
			     "0" (value), "1" (addr));
	return value;
}

/* Functions really use this, not srmmu_swap directly. */
#define srmmu_set_entry(ptr, newentry) \
        srmmu_swap((unsigned long *) (ptr), (newentry))

/* We still don't use these at all, perhaps we don't need them
 * at all.
 */
unsigned long (*srmmu_read_physical)(unsigned long paddr);
void (*srmmu_write_physical)(unsigned long paddr, unsigned long word);

static unsigned long gensrmmu_read_physical(unsigned long paddr)
{
	unsigned long word;

	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (word) :
			     "r" (paddr), "i" (ASI_M_BYPASS) :
			     "memory");
	return word;
}

static unsigned long msparc_read_physical(unsigned long paddr)
{
	unsigned long word, flags;

	save_flags(flags); cli();
	__asm__ __volatile__("lda [%%g0] %3, %%g1\n\t"
			     "or  %%g1, %4, %%g2\n\t"
			     "sta %%g2, [%%g0] %3\n\t"
			     "lda [%1] %2, %0\n\t"
			     "sta %%g1, [%%g0] %3\n\t" :
			     "=r" (word) :
			     "r" (paddr), "i" (ASI_M_BYPASS),
			     "i" (ASI_M_MMUREGS), "r" (VIKING_ACENABLE) :
			     "g1", "g2", "memory");
	restore_flags(flags);
	return word;
}

static void gensrmmu_write_physical(unsigned long paddr, unsigned long word)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
			     "r" (word), "r" (paddr), "i" (ASI_M_BYPASS) :
			     "memory");
}

static void msparc_write_physical(unsigned long paddr, unsigned long word)
{
	unsigned long flags;

	save_flags(flags); cli();
	__asm__ __volatile__("lda [%%g0] %3, %%g1\n\t"
			     "or  %%g1, %4, %%g2\n\t"
			     "sta %%g2, [%%g0] %3\n\t"
			     "sta %0, [%1] %2\n\t"
			     "sta %%g1, [%%g0] %3\n\t" : :
			     "r" (word), "r" (paddr), "i" (ASI_M_BYPASS),
			     "i" (ASI_M_MMUREGS), "r" (VIKING_ACENABLE) :
			     "g1", "g2", "memory");
	restore_flags(flags);
}

static unsigned int srmmu_pmd_align(unsigned int addr) { return SRMMU_PMD_ALIGN(addr); }
static unsigned int srmmu_pgdir_align(unsigned int addr) { return SRMMU_PGDIR_ALIGN(addr); }

static unsigned long srmmu_vmalloc_start(void)
{
	return SRMMU_VMALLOC_START;
}

static unsigned long srmmu_pgd_page(pgd_t pgd)
{ return PAGE_OFFSET + ((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pmd_page(pmd_t pmd)
{ return PAGE_OFFSET + ((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pte_page(pte_t pte)
{ return PAGE_OFFSET + ((pte_val(pte) & SRMMU_PTE_PMASK) << 4); }

static int srmmu_pte_none(pte_t pte)          { return !pte_val(pte); }
static int srmmu_pte_present(pte_t pte)
{ return ((pte_val(pte) & SRMMU_ET_MASK) == SRMMU_ET_PTE); }

static int srmmu_pte_inuse(pte_t *ptep)
{ return mem_map[MAP_NR(ptep)].reserved || mem_map[MAP_NR(ptep)].count != 1; }

static void srmmu_pte_clear(pte_t *ptep)      { pte_val(*ptep) = 0; }
static void srmmu_pte_reuse(pte_t *ptep)
{
	if(!mem_map[MAP_NR(ptep)].reserved)
		mem_map[MAP_NR(ptep)].count++;
}

static int srmmu_pmd_none(pmd_t pmd)          { return !pmd_val(pmd); }
static int srmmu_pmd_bad(pmd_t pmd)
{ return (pmd_val(pmd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static int srmmu_pmd_present(pmd_t pmd)
{ return ((pmd_val(pmd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static int srmmu_pmd_inuse(pmd_t *pmdp)
{ return mem_map[MAP_NR(pmdp)].reserved || mem_map[MAP_NR(pmdp)].count != 1; }

static void srmmu_pmd_clear(pmd_t *pmdp)      { pmd_val(*pmdp) = 0; }
static void srmmu_pmd_reuse(pmd_t * pmdp)
{
	if (!mem_map[MAP_NR(pmdp)].reserved)
		mem_map[MAP_NR(pmdp)].count++;
}

static int srmmu_pgd_none(pgd_t pgd)          { return !pgd_val(pgd); }
static int srmmu_pgd_bad(pgd_t pgd)
{ return (pgd_val(pgd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static int srmmu_pgd_present(pgd_t pgd)
{ return ((pgd_val(pgd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static int srmmu_pgd_inuse(pgd_t *pgdp)       { return mem_map[MAP_NR(pgdp)].reserved; }
static void srmmu_pgd_clear(pgd_t * pgdp)     { pgd_val(*pgdp) = 0; }
static void srmmu_pgd_reuse(pgd_t *pgdp)
{
	if (!mem_map[MAP_NR(pgdp)].reserved)
		mem_map[MAP_NR(pgdp)].count++;
}

static int srmmu_pte_write(pte_t pte)         { return pte_val(pte) & SRMMU_WRITE; }
static int srmmu_pte_dirty(pte_t pte)         { return pte_val(pte) & SRMMU_DIRTY; }
static int srmmu_pte_young(pte_t pte)         { return pte_val(pte) & SRMMU_REF; }

static pte_t srmmu_pte_wrprotect(pte_t pte)   { pte_val(pte) &= ~SRMMU_WRITE; return pte;}
static pte_t srmmu_pte_mkclean(pte_t pte)     { pte_val(pte) &= ~SRMMU_DIRTY; return pte; }
static pte_t srmmu_pte_mkold(pte_t pte)       { pte_val(pte) &= ~SRMMU_REF; return pte; }
static pte_t srmmu_pte_mkwrite(pte_t pte)     { pte_val(pte) |= SRMMU_WRITE; return pte; }
static pte_t srmmu_pte_mkdirty(pte_t pte)     { pte_val(pte) |= SRMMU_DIRTY; return pte; }
static pte_t srmmu_pte_mkyoung(pte_t pte)     { pte_val(pte) |= SRMMU_REF; return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static pte_t srmmu_mk_pte(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = ((page - PAGE_OFFSET) >> 4) | pgprot_val(pgprot); return pte; }

static pte_t srmmu_mk_pte_io(unsigned long page, pgprot_t pgprot)
{ pte_t pte; pte_val(pte) = ((page) >> 4) | pgprot_val(pgprot); return pte; }

static void srmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ srmmu_set_entry(ctxp, (SRMMU_ET_PTD | ((((unsigned long) pgdp) - PAGE_OFFSET) >> 4))); }

static void srmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{ srmmu_set_entry(pgdp, (SRMMU_ET_PTD | ((((unsigned long) pmdp) - PAGE_OFFSET) >> 4))); }

static void srmmu_pmd_set(pmd_t * pmdp, pte_t * ptep)
{ srmmu_set_entry(pmdp, (SRMMU_ET_PTD | ((((unsigned long) ptep) - PAGE_OFFSET) >> 4))); }

static pte_t srmmu_pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & ~0xff) | pgprot_val(newprot); return pte; }

/* to find an entry in a top-level page table... */
static pgd_t *srmmu_pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + ((address >> SRMMU_PGDIR_SHIFT) & (SRMMU_PTRS_PER_PGD - 1));
}

/* Find an entry in the second-level page table.. */
static pmd_t *srmmu_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
static pte_t *srmmu_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

/* This must update the context table entry for this process. */
static void srmmu_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	if(tsk->mm->context != NO_CONTEXT)
		srmmu_ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
static void srmmu_pte_free_kernel(pte_t *pte)
{
	mem_map[MAP_NR(pte)].reserved = 0;
	free_page((unsigned long) pte);
}

static pte_t *srmmu_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if(srmmu_pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if(srmmu_pmd_none(*pmd)) {
			if(page) {
				srmmu_pmd_set(pmd, page);
				mem_map[MAP_NR(page)].reserved = 1;
				return page + address;
			}
			srmmu_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if(srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		srmmu_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) srmmu_pmd_page(*pmd) + address;
}

static void srmmu_pmd_free_kernel(pmd_t *pmd)
{
	mem_map[MAP_NR(pmd)].reserved = 0;
	free_page((unsigned long) pmd);
}

static pmd_t *srmmu_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if(srmmu_pgd_none(*pgd)) {
		pmd_t *page = (pmd_t *) get_free_page(GFP_KERNEL);
		if(srmmu_pgd_none(*pgd)) {
			if(page) {
				srmmu_pgd_set(pgd, page);
				mem_map[MAP_NR(page)].reserved = 1;
				return page + address;
			}
			srmmu_pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if(srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		srmmu_pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

static void srmmu_pte_free(pte_t *pte)
{
	free_page((unsigned long) pte);
}

static pte_t *srmmu_pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if(srmmu_pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_free_page(GFP_KERNEL);
		if(srmmu_pmd_none(*pmd)) {
			if(page) {
				srmmu_pmd_set(pmd, page);
				return page + address;
			}
			srmmu_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if(srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		srmmu_pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

/* Real three-level page tables on SRMMU. */
static void srmmu_pmd_free(pmd_t * pmd)
{
	free_page((unsigned long) pmd);
}

static pmd_t *srmmu_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if(srmmu_pgd_none(*pgd)) {
		pmd_t *page = (pmd_t *) get_free_page(GFP_KERNEL);
		if(srmmu_pgd_none(*pgd)) {
			if(page) {
				srmmu_pgd_set(pgd, page);
				return page + address;
			}
			srmmu_pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		free_page((unsigned long) page);
	}
	if(srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		srmmu_pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) srmmu_pgd_page(*pgd) + address;
}

static void srmmu_pgd_free(pgd_t *pgd)
{
	free_page((unsigned long) pgd);
}

static pgd_t *srmmu_pgd_alloc(void)
{
	return (pgd_t *) get_free_page(GFP_KERNEL);
}

/* Tsunami invalidates.  It's page level tlb invalidation is not very
 * useful at all, you must be in the context that page exists in to
 * get a match.  It might be worthwhile to try someday though...
 */
/* static */ inline void tsunami_invalidate_all(void)
{
	tsunami_invalidate_icache();
	tsunami_invalidate_dcache();
	srmmu_flush_whole_tlb();
}
static void tsunami_invalidate_mm(struct mm_struct *mm)
{
	tsunami_invalidate_all();
}

static void tsunami_invalidate_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	tsunami_invalidate_all();
}

/* XXX do page level tlb flushes at some point XXX */
static void tsunami_invalidate_page(struct vm_area_struct *vmp, unsigned long page)
{
	tsunami_invalidate_all();
}

/* Swift invalidates.  It has the recommended SRMMU specification flushing
 * facilities, so we can do things in a more fine grained fashion than we
 * could on the tsunami.  Let's watch out for HARDWARE BUGS...
 */
static inline void swift_invalidate_all(void)
{
	unsigned long addr = 0;

	/* Invalidate all cache tags */
	for(addr = 0; addr < (PAGE_SIZE << 2); addr += 16) {
		swift_inv_insn_tag(addr); /* whiz- */
		swift_inv_data_tag(addr); /* bang */
	}
	srmmu_flush_whole_tlb();
}

static void swift_invalidate_mm(struct mm_struct *mm)
{
	unsigned long flags;
	int cc, ncc = mm->context;

	if(ncc == NO_CONTEXT)
		return;

	/* have context will travel... */
	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	swift_flush_context(); /* POOF! */
	srmmu_flush_tlb_ctx(); /* POW! */

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}

static void swift_invalidate_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	unsigned long flags, addr;
	int cc, ncc = mm->context;

	if(ncc == NO_CONTEXT)
		return;

	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	/* XXX Inefficient, we don't do the best we can... XXX */
	addr = start & SRMMU_PGDIR_MASK;
	while(addr < end) {
		swift_flush_region(addr);
		srmmu_flush_tlb_region(addr);
		addr += SRMMU_PGDIR_SIZE;
	}

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}

static void swift_invalidate_page(struct vm_area_struct *vmp, unsigned long page)
{
	unsigned long flags;
	int cc, ncc = vmp->vm_mm->context;

	if(ncc == NO_CONTEXT)
		return;

	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	swift_flush_page(page);
	srmmu_flush_tlb_page(page);

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}

/* The following are all MBUS based SRMMU modules, and therefore could
 * be found in a multiprocessor configuration.
 */

/* Viking invalidates.  For Sun's mainline MBUS processor it is pretty much
 * a crappy mmu.  The on-chip I&D caches only have full flushes, no fine
 * grained cache invalidations.  It only has these "flash clear" things
 * just like the MicroSparcI.  Added to this many revs of the chip are
 * teaming with hardware buggery.
 *
 * XXX need to handle SMP broadcast invalidations! XXX
 */
static inline void viking_invalidate_all(void)
{
	viking_flush_icache();
	viking_flush_dcache();
	srmmu_flush_whole_tlb();
}
static void viking_invalidate_mm(struct mm_struct *mm)
{
	unsigned long flags;
	int cc, ncc = mm->context;

	if(ncc == NO_CONTEXT)
		return;

	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	viking_flush_icache();
	viking_flush_dcache();
	srmmu_flush_tlb_ctx();

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}

static void viking_invalidate_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	unsigned long flags, addr;
	int cc, ncc = mm->context;

	if(ncc == NO_CONTEXT)
		return;

	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	/* XXX Inefficient, we don't do the best we can... XXX */
	viking_flush_icache();
	viking_flush_dcache();
	addr = start & SRMMU_PGDIR_MASK;
	while(addr < end) {
		srmmu_flush_tlb_region(addr);
		addr += SRMMU_PGDIR_SIZE;
	}

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}
static void viking_invalidate_page(struct vm_area_struct *vmp, unsigned long page)
{
	unsigned long flags;
	int cc, ncc = vmp->vm_mm->context;

	if(ncc == NO_CONTEXT)
		return;

	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	viking_flush_icache();
	viking_flush_dcache();
	srmmu_flush_tlb_page(page);

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}

/* Cypress invalidates. */
static inline void cypress_invalidate_all(void)
{
	srmmu_flush_whole_tlb();
}
static void cypress_invalidate_mm(struct mm_struct *mm)
{
	unsigned long flags;
	int cc, ncc = mm->context;

	if(ncc == NO_CONTEXT)
		return;

	/* have context will travel... */
	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	cypress_flush_context(); /* POOF! */
	srmmu_flush_whole_tlb(); /* POW! */

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}
static void cypress_invalidate_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	unsigned long flags, addr;
	int cc, ncc = mm->context;

	if(ncc == NO_CONTEXT)
		return;

	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	/* XXX Inefficient, we don't do the best we can... XXX */
	addr = start & SRMMU_PGDIR_MASK;
	while(addr < end) {
		cypress_flush_region(addr);
		addr += SRMMU_PGDIR_SIZE;
	}
	srmmu_flush_whole_tlb();

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}

static void cypress_invalidate_page(struct vm_area_struct *vmp, unsigned long page)
{
	unsigned long flags;
	int cc, ncc = vmp->vm_mm->context;

	if(ncc == NO_CONTEXT)
		return;

	save_flags(flags); cli();
	cc = srmmu_get_context();
	if(cc != ncc)
		srmmu_set_context(ncc);

	swift_flush_page(page);
	srmmu_flush_whole_tlb();

	if(cc != ncc)
		srmmu_set_context(cc);
	restore_flags(flags);
}

/* Hypersparc invalidates. */
static inline void hypersparc_invalidate_all(void)
{

	hyper_flush_whole_icache();
	srmmu_flush_whole_tlb();
}

static void hypersparc_invalidate_mm(struct mm_struct *mm)
{

}

static void hypersparc_invalidate_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{

}

static void hypersparc_invalidate_page(struct vm_area_struct *vmp, unsigned long page)
{

}

static void srmmu_set_pte(pte_t *ptep, pte_t pteval)
{
	srmmu_set_entry(ptep, pte_val(pteval));
}

static void srmmu_quick_kernel_fault(unsigned long address)
{
	printk("SRMMU: quick_kernel_fault called for %08lx\n", address);
	panic("Srmmu bolixed...");
}

static inline void alloc_context(struct mm_struct *mm)
{
	struct ctx_list *ctxp;

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
	remove_from_ctx_list(ctxp);
	add_to_used_ctxlist(ctxp);
	ctxp->ctx_mm->context = NO_CONTEXT;
	ctxp->ctx_mm = mm;
	mm->context = ctxp->ctx_number;
}

static void srmmu_switch_to_context(struct task_struct *tsk)
{
	/* Kernel threads can execute in any context and so can tasks
	 * sleeping in the middle of exiting. If this task has already
	 * been allocated a piece of the mmu realestate, just jump to
	 * it.
	 */
	if((tsk->tss.flags & SPARC_FLAG_KTHREAD) ||
	   (tsk->flags & PF_EXITING))
		return;
	if(tsk->mm->context == NO_CONTEXT) {
		alloc_context(tsk->mm);
		srmmu_ctxd_set(&srmmu_context_table[tsk->mm->context], tsk->mm->pgd);
	}
	srmmu_set_context(tsk->mm->context);
}

/* Low level IO area allocation on the SRMMU. */
void srmmu_mapioaddr(unsigned long physaddr, unsigned long virt_addr, int bus_type, int rdonly)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long tmp;

	physaddr &= PAGE_MASK;
	pgdp = srmmu_pgd_offset(init_task.mm, virt_addr);
	pmdp = srmmu_pmd_offset(pgdp, virt_addr);
	ptep = srmmu_pte_offset(pmdp, virt_addr);
	tmp = (physaddr >> 4) | SRMMU_ET_PTE;

	/* I need to test whether this is consistent over all
	 * sun4m's.  The bus_type represents the upper 4 bits of
	 * 36-bit physical address on the I/O space lines...
	 */
	tmp |= (bus_type << 28);
	if(rdonly)
		tmp |= SRMMU_PRIV_RDONLY;
	else
		tmp |= SRMMU_PRIV;
	srmmu_set_entry(ptep, tmp);
	invalidate_all();
}

static char *srmmu_lockarea(char *vaddr, unsigned long len)
{
	return vaddr;
}

static void srmmu_unlockarea(char *vaddr, unsigned long len)
{
}

/* IOMMU things go here. */

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))
static unsigned long first_dvma_page, last_dvma_page;

static inline void srmmu_map_dvma_pages_for_iommu(struct iommu_struct *iommu)
{
	unsigned long first = first_dvma_page;
	unsigned long last = last_dvma_page;
	iopte_t *iopte;

	iopte = iommu->page_table;
	iopte += ((DVMA_VADDR - iommu->start) >> PAGE_SHIFT);
	while(first <= last) {
		iopte_val(*iopte++) = ((((first - PAGE_OFFSET) >> 4) & IOPTE_PAGE) |
				       (IOPTE_WRITE | IOPTE_VALID)) & ~(IOPTE_WAZ);
		first += PAGE_SIZE;
	}
}

void srmmu_uncache_iommu_page_table(unsigned long start, int size)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long end = start + size;

	while(start < end) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		pmdp = srmmu_pmd_offset(pgdp, start);
		ptep = srmmu_pte_offset(pmdp, start);
		pte_val(*ptep) &= ~SRMMU_CACHE;
		start += PAGE_SIZE;
	}
}

unsigned long iommu_init(int iommund, unsigned long memory_start,
			 unsigned long memory_end, struct linux_sbus *sbus)
{
	int impl, vers, ptsize;
	unsigned long tmp;
	struct iommu_struct *iommu;
	struct linux_prom_registers iommu_promregs[PROMREG_MAX];

	memory_start = LONG_ALIGN(memory_start);
	iommu = (struct iommu_struct *) memory_start;
	memory_start += sizeof(struct iommu_struct);
	prom_getproperty(iommund, "reg", (void *) iommu_promregs, sizeof(iommu_promregs));
	iommu->regs = (struct iommu_regs *)
		sparc_alloc_io(iommu_promregs[0].phys_addr, 0, (PAGE_SIZE * 3),
			       "IOMMU registers", iommu_promregs[0].which_io, 0x0);
	if(!iommu->regs)
		panic("Cannot map IOMMU registers.");
	impl = (iommu->regs->control & IOMMU_CTRL_IMPL) >> 28;
	vers = (iommu->regs->control & IOMMU_CTRL_VERS) >> 24;
	tmp = iommu->regs->control;
	tmp &= ~(IOMMU_CTRL_RNGE);
	tmp |= (IOMMU_RNGE_64MB | IOMMU_CTRL_ENAB);
	iommu->regs->control = tmp;
	iommu_invalidate(iommu->regs);
	iommu->start = 0xfc000000;
	iommu->end = 0xffffffff;

	/* Allocate IOMMU page table */
	ptsize = iommu->end - iommu->start + 1;
	ptsize = (ptsize >> PAGE_SHIFT) * sizeof(iopte_t);

	/* Stupid alignment constraints give me a headache. */
	memory_start = PAGE_ALIGN(memory_start);
	memory_start = (((memory_start) + (ptsize - 1)) & ~(ptsize - 1));
	iommu->page_table = (iopte_t *) memory_start;
	memory_start += ptsize;

	/* Initialize new table. */
	memset(iommu->page_table, 0, ptsize);
	srmmu_map_dvma_pages_for_iommu(iommu);
	iommu->regs->base = (((unsigned long) iommu->page_table) - PAGE_OFFSET) >> 4;
	srmmu_uncache_iommu_page_table((unsigned long) iommu->page_table, ptsize);
	iommu_invalidate(iommu->regs);
	invalidate_all();

	sbus->iommu = iommu;
	printk("IOMMU: impl %d vers %d page table at %p of size %d bytes\n",
	       impl, vers, iommu->page_table, ptsize);
	return memory_start;
}


static char *srmmu_get_scsi_buffer(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
	unsigned long page = (unsigned long) vaddr;
	unsigned long start, end, offset;
	iopte_t *iopte;

	if(len > PAGE_SIZE)
		panic("Can only handle page sized iommu mappings.");
	offset = page & ~PAGE_MASK;
	page &= PAGE_MASK;

	start = iommu->start;
	end = KADB_DEBUGGER_BEGVM; /* Don't step on kadb/prom. */
	iopte = iommu->page_table;
	while(start < end) {
		if(!(iopte_val(*iopte) & IOPTE_VALID))
			break;
		iopte++;
		start += PAGE_SIZE;
	}
	if(start == KADB_DEBUGGER_BEGVM)
		panic("Could not find free iommu entry in get_scsi_buffer.");

	vaddr = (char *) (start | offset);
	iopte_val(*iopte) = ((((page - PAGE_OFFSET) >> 4) & IOPTE_PAGE) |
		(IOPTE_WRITE | IOPTE_VALID)) & ~(IOPTE_WAZ);
	iommu_invalidate(iommu->regs);
	invalidate_all();

	return vaddr;
}

static void srmmu_release_scsi_buffer(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
	unsigned long page = (unsigned long) vaddr;
	iopte_t *iopte;

	if(len > PAGE_SIZE)
		panic("Can only handle page sized IOMMU mappings.");
	page &= PAGE_MASK;
	iopte = iommu->page_table + ((page - iommu->start) >> PAGE_SHIFT);
	iopte_val(*iopte) = 0;
	iommu_invalidate(iommu->regs);
	invalidate_all();
}

/* On the SRMMU we do not have the problems with limited tlb entries
 * for mapping kernel pages, so we just take things from the free page
 * pool.  As a side effect we are putting a little too much pressure
 * on the gfp() subsystem and we don't catch stack overflow like we
 * did on the sun4c with virtual kstack mappings.  This setup also
 * makes the logic of the iommu mapping code a lot easier as we can
 * transparently handle mappings on the kernel stack without any
 * special code as we did need on the sun4c.
 */
struct task_struct *srmmu_alloc_task_struct(void)
{
	unsigned long page;

	page = get_free_page(GFP_KERNEL);
	if(!page)
		return (struct task_struct *) 0;
	return (struct task_struct *) page;
}

unsigned long srmmu_alloc_kernel_stack(struct task_struct *tsk)
{
	unsigned long pages;

	pages = __get_free_pages(GFP_KERNEL, 1, 0);
	if(!pages)
		return 0;
	memset((void *) pages, 0, (PAGE_SIZE << 1));
	return pages;
}

static void srmmu_free_task_struct(struct task_struct *tsk)
{
	free_page((unsigned long) tsk);
}

static void srmmu_free_kernel_stack(unsigned long stack)
{
	free_pages(stack, 1);
}

static unsigned long mempool;

/* Allocate a block of RAM which is aligned to its size.
 * This procedure can be used until the call to mem_init().
 */
static void *srmmu_init_alloc(unsigned long *kbrk, unsigned size)
{
	register unsigned mask = size - 1;
	register unsigned long ret;

	if(size==0) return 0x0;
	if(size & mask) {
		prom_printf("panic: srmmu_init_alloc botch\n");
		prom_halt();
	}
	ret = (*kbrk + mask) & ~mask;
	*kbrk = ret + size;
	memset((void*) ret, 0, size);
	return (void*) ret;
}

static inline void srmmu_allocate_ptable_skeleton(unsigned long start, unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	while(start < end) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = srmmu_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
			srmmu_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_pmd_offset(pgdp, start);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = srmmu_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_pmd_set(pmdp, ptep);
		}
		start = (start + SRMMU_PMD_SIZE) & SRMMU_PMD_MASK;
	}
}

/* This is much cleaner than poking around physical address space
 * looking at the prom's page table directly which is what most
 * other OS's do.  Yuck... this is much better.
 */
static inline void srmmu_inherit_prom_mappings(void)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long start, end;
	unsigned long prompte;

	start = KADB_DEBUGGER_BEGVM;
	end = LINUX_OPPROM_ENDVM;
	while(start < end) {
		/* Something going wrong here on some ss5's... */
		prompte = srmmu_hwprobe(start);

		if((prompte & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
			pgdp = srmmu_pgd_offset(init_task.mm, start);
			if(srmmu_pgd_none(*pgdp)) {
				pmdp = srmmu_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
				srmmu_pgd_set(pgdp, pmdp);
			}
			pmdp = srmmu_pmd_offset(pgdp, start);
			if(srmmu_pmd_none(*pmdp)) {
				ptep = srmmu_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
				srmmu_pmd_set(pmdp, ptep);
			}
			ptep = srmmu_pte_offset(pmdp, start);
			pte_val(*ptep) = prompte;
		}
		start += PAGE_SIZE;
	}
}

static inline void srmmu_map_dvma_pages_for_cpu(unsigned long first, unsigned long last)
{
	unsigned long start;
	pgprot_t dvma_prot;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	start = DVMA_VADDR;
	dvma_prot = __pgprot(SRMMU_ET_PTE | SRMMU_PRIV);
	while(first <= last) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		pmdp = srmmu_pmd_offset(pgdp, start);
		ptep = srmmu_pte_offset(pmdp, start);

		/* Map with cacheable bit clear. */
		srmmu_set_entry(ptep, pte_val(srmmu_mk_pte(first, dvma_prot)));

		first += PAGE_SIZE;
		start += PAGE_SIZE;
	}
}

static void srmmu_map_kernel(unsigned long start, unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	end = (PAGE_ALIGN(end) + PAGE_SIZE);
	while(start < end) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = srmmu_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
			srmmu_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_pmd_offset(pgdp, start);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = srmmu_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_pmd_set(pmdp, ptep);
		}
		ptep = srmmu_pte_offset(pmdp, start);
		*ptep = srmmu_mk_pte(start, SRMMU_PAGE_KERNEL);
		start += PAGE_SIZE;
	}
}

/* Paging initialization on the Sparc Reference MMU. */
extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long sparc_context_init(unsigned long, int);

unsigned long srmmu_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int i, cpunode;
	char node_str[128];

	/* Find the number of contexts on the srmmu. */
	cpunode = prom_getchild(prom_root_node);
	num_contexts = 0;
	while((cpunode = prom_getsibling(cpunode)) != 0) {
		prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
		if(!strcmp(node_str, "cpu")) {
			num_contexts = prom_getintdefault(cpunode, "mmu-nctx", 0x8);
			break;
		}
	}
	if(!num_contexts) {
		prom_printf("Something wrong, cant find cpu node in paging_init.\n");
		prom_halt();
	}
		
	prom_printf("Number of MMU contexts %d\n", num_contexts);
	mempool = start_mem;
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	srmmu_map_kernel(KERNBASE, end_mem);
	srmmu_allocate_ptable_skeleton(IOBASE_VADDR, IOBASE_END);
	srmmu_allocate_ptable_skeleton(DVMA_VADDR, DVMA_END);
	mempool = PAGE_ALIGN(mempool);
	first_dvma_page = mempool;
	last_dvma_page = (mempool + (DVMA_LEN) - PAGE_SIZE);
	mempool = last_dvma_page + PAGE_SIZE;
	srmmu_map_dvma_pages_for_cpu(first_dvma_page, last_dvma_page);

	srmmu_inherit_prom_mappings();
	srmmu_context_table = srmmu_init_alloc(&mempool, num_contexts*sizeof(ctxd_t));
	for(i = 0; i < num_contexts; i++)
		srmmu_ctxd_set(&srmmu_context_table[i], swapper_pg_dir);

	prom_printf("Taking over MMU from PROM.\n");
	srmmu_flush_whole_tlb();
	srmmu_set_ctable_ptr(((unsigned)srmmu_context_table) - PAGE_OFFSET);
	srmmu_flush_whole_tlb();

	start_mem = PAGE_ALIGN(mempool);
	start_mem = sparc_context_init(start_mem, num_contexts);
	start_mem = free_area_init(start_mem, end_mem);

	prom_printf("survived...\n");
	return PAGE_ALIGN(start_mem);
}

/* Test the WP bit on the Sparc Reference MMU. */
void srmmu_test_wp(void)
{
	pgd_t *pgdp;
	
	wp_works_ok = -1;
	/* We mapped page zero as a read-only page in paging_init()
	 * So fire up the test, then invalidate the pgd for page zero.
	 * It is no longer needed.
	 */

	/* Let it rip... */
	__asm__ __volatile__("st %%g0, [0x0]\n\t": : :"memory");
	if (wp_works_ok < 0)
		wp_works_ok = 0;

	pgdp = srmmu_pgd_offset(init_task.mm, 0x0);
	pgd_val(*pgdp) = 0x0;
}

static char *srmmu_mmu_info(void)
{
	return "";
}

static void srmmu_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
}

static void srmmu_exit_hook(void)
{
	struct ctx_list *ctx_old;
	struct mm_struct *mm = current->mm;

	if(mm->context != NO_CONTEXT) {
		srmmu_ctxd_set(&srmmu_context_table[mm->context], swapper_pg_dir);
		ctx_old = ctx_list_pool + mm->context;
		remove_from_ctx_list(ctx_old);
		add_to_free_ctxlist(ctx_old);
		mm->context = NO_CONTEXT;
	}
}

static void
srmmu_flush_hook(void)
{
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		alloc_context(current->mm);
		srmmu_ctxd_set(&srmmu_context_table[current->mm->context], current->mm->pgd);
		srmmu_set_context(current->mm->context);
	}
}

/* Init various srmmu chip types. */
void srmmu_is_bad(void)
{
	prom_printf("Could not determine SRMMU chip type.\n");
	prom_halt();
}

void init_hypersparc(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	prom_printf("HyperSparc MMU detected.\n");
	if(mreg & HYPERSPARC_CSIZE)
		hyper_cache_size = (256 * 1024);
	else
		hyper_cache_size = (128 * 1024);

	srmmu_modtype = HyperSparc;
	hwbug_bitmask |= HWBUG_VACFLUSH_BITROT;

	hyper_flush_whole_icache();
	hyper_flush_all_combined();

	/* Keep things sane for now, cache in write-through mode. */
	mreg &= ~(HYPERSPARC_CWENABLE | HYPERSPARC_CMODE | HYPERSPARC_WBENABLE);
	mreg |= HYPERSPARC_CENABLE;
	srmmu_set_mmureg(mreg);
	put_ross_icr(get_ross_icr() | 0x3);
	invalidate_all = hypersparc_invalidate_all;
	invalidate_mm = hypersparc_invalidate_mm;
	invalidate_page = hypersparc_invalidate_page;
	invalidate_range = hypersparc_invalidate_range;
}

void init_cypress_common(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	mreg &= ~CYPRESS_CMODE;
	mreg |= CYPRESS_CENABLE;
	srmmu_set_mmureg(mreg);
	invalidate_all = cypress_invalidate_all;
	invalidate_mm = cypress_invalidate_mm;
	invalidate_page = cypress_invalidate_page;
	invalidate_range = cypress_invalidate_range;
}

void init_cypress_604(void)
{
	prom_printf("Cypress 604(UP) MMU detected.\n");
	srmmu_modtype = Cypress;
	init_cypress_common();
}

void init_cypress_605(unsigned long mrev)
{
	prom_printf("Cypress 605(MP) MMU detected.\n");
	if(mrev == 0xe) {
		srmmu_modtype = Cypress_vE;
		hwbug_bitmask |= HWBUG_COPYBACK_BROKEN;
	} else {
		if(mrev == 0xd) {
			srmmu_modtype = Cypress_vD;
			hwbug_bitmask |= HWBUG_ASIFLUSH_BROKEN;
		} else {
			srmmu_modtype = Cypress;
		}
	}
	init_cypress_common();
}

#define SWIFT_REVISION_ADDR  0x10003000
void init_swift(void)
{
	unsigned long swift_rev, addr;
	unsigned long mreg = srmmu_get_mmureg();

	prom_printf("Swift MMU detected.\n");
	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     "srl %0, 0x18, %0\n\t" :
			     "=r" (swift_rev) :
			     "r" (SWIFT_REVISION_ADDR), "i" (0x20));
	switch(swift_rev) {
	case 0x11:
	case 0x20:
	case 0x23:
	case 0x30:
		srmmu_modtype = Swift_lots_o_bugs;
		hwbug_bitmask |= (HWBUG_KERN_ACCBROKEN | HWBUG_KERN_CBITBROKEN);
		/* Gee george, I wonder why Sun is so hush hush about
		 * this hardware bug... really braindamage stuff going
		 * on here.  However I think we can find a way to avoid
		 * all of the workaround overhead under Linux.  Basically,
		 * any page fault can cause kernel pages to become user
		 * accessible (the mmu gets confused and clears some of
		 * the ACC bits in kernel ptes).  Aha, sounds pretty
		 * horrible eh?  But wait, after extensive testing it appears
		 * that if you use pgd_t level large kernel pte's (like the
		 * 4MB pages on the Pentium) the bug does not get tripped
		 * at all.  This avoids almost all of the major overhead.
		 * Welcome to a world where your vendor tells you to,
		 * "apply this kernel patch" instead of "sorry for the
		 * broken hardware, send it back and we'll give you
		 * properly functioning parts"
		 */
		break;
	case 0x25:
	case 0x31:
		srmmu_modtype = Swift_bad_c;
		hwbug_bitmask |= HWBUG_KERN_CBITBROKEN;
		/* You see Sun allude to this hardware bug but never
		 * admit things directly, they'll say things like,
		 * "the Swift chip cache problems" or similar.
		 */
		break;
	default:
		srmmu_modtype = Swift_ok;
		break;
	};
	/* Clear any crap from the cache or else... */
	for(addr = 0; addr < (PAGE_SIZE * 4); addr += 16) {
		swift_inv_insn_tag(addr); /* whiz- */
		swift_inv_data_tag(addr); /* bang */
	}
	mreg |= (SWIFT_IE | SWIFT_DE); /* I & D caches on */

	/* The Swift branch folding logic is completely broken.  At
	 * trap time, if things are just right, if can mistakenly
	 * thing that a trap is coming from kernel mode when in fact
	 * it is coming from user mode (it misexecutes the branch in
	 * the trap code).  So you see things like crashme completely
	 * hosing your machine which is completely unacceptable.  Turn
	 * this crap off... nice job Fujitsu.
	 */
	mreg &= ~(SWIFT_BF);
	srmmu_set_mmureg(mreg);

	invalidate_all = swift_invalidate_all;
	invalidate_mm = swift_invalidate_mm;
	invalidate_page = swift_invalidate_page;
	invalidate_range = swift_invalidate_range;

	/* Are you now convinced that the Swift is one of the
	 * biggest VLSI abortions of all time?  Bravo Fujitsu!
	 */
}

void init_tsunami(unsigned long mreg)
{
	/* Tsunami's pretty sane, Sun and TI actually got it
	 * somewhat right this time.  Fujitsu should have
	 * taken some lessons from them.
	 */

	prom_printf("Tsunami MMU detected.\n");
	srmmu_modtype = Tsunami;
	tsunami_invalidate_icache();
	tsunami_invalidate_dcache();
	mreg &= ~TSUNAMI_ITD;
	mreg |= (TSUNAMI_IENAB | TSUNAMI_DENAB);
	srmmu_set_mmureg(mreg);
	invalidate_all = tsunami_invalidate_all;
	invalidate_mm = tsunami_invalidate_mm;
	invalidate_page = tsunami_invalidate_page;
	invalidate_range = tsunami_invalidate_range;
}

void init_viking(unsigned long psr_vers, unsigned long mod_rev)
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Ahhh, the viking.  SRMMU VLSI abortion number two... */

	prom_printf("Viking MMU detected.\n");
	if(!psr_vers && ! mod_rev) {
		srmmu_modtype = Viking_12;
		hwbug_bitmask |= (HWBUG_MODIFIED_BITROT | HWBUG_PC_BADFAULT_ADDR);

		/* On a fault, the chip gets entirely confused.  It will
		 * do one of two things.  Either it will set the modified
		 * bit for a read-only page (!!!) or it will improperly
		 * report a fault when a dcti/loadstore sequence is the
		 * last two instructions on a page.  Oh baby...
		 */
	} else {
		if(psr_vers) {
			srmmu_modtype = Viking_2x;
			hwbug_bitmask |= HWBUG_PC_BADFAULT_ADDR; /* see above */
		} else {
			if(mod_rev == 1) {
				srmmu_modtype = Viking_30;
				hwbug_bitmask |= HWBUG_PACINIT_BITROT;

				/* At boot time the physical cache
				 * has cherry bombs in it, so you
				 * have to scrape it by hand before
				 * enabling it.  Nice CAD tools guys.
				 */
			} else {
				if(mod_rev < 8)
					srmmu_modtype = Viking_35;
				else
					srmmu_modtype = Viking_new;
			}
		}
	}
	/* XXX Dave, play with the MXCC you pinhead XXX */
	viking_flush_icache();
	viking_flush_dcache();
	mreg |= (VIKING_DCENABLE | VIKING_ICENABLE | VIKING_SBENABLE |
		 VIKING_TCENABLE | VIKING_DPENABLE);
	srmmu_set_mmureg(mreg);
	invalidate_all = viking_invalidate_all;
	invalidate_mm = viking_invalidate_mm;
	invalidate_page = viking_invalidate_page;
	invalidate_range = viking_invalidate_range;
}

/* Probe for the srmmu chip version. */
static void get_srmmu_type(void)
{
	unsigned long mreg, psr;
	unsigned long mod_typ, mod_rev, psr_typ, psr_vers;

	srmmu_modtype = SRMMU_INVAL_MOD;
	hwbug_bitmask = 0;

	mreg = srmmu_get_mmureg(); psr = get_psr();
	mod_typ = (mreg & 0xf0000000) >> 28;
	mod_rev = (mreg & 0x0f000000) >> 24;
	psr_typ = (psr >> 28) & 0xf;
	psr_vers = (psr >> 24) & 0xf;

	/* First, check for HyperSparc or Cypress. */
	if(mod_typ == 1) {
		switch(mod_rev) {
		case 7:
			/* UP or MP Hypersparc */
			init_hypersparc();
			break;
		case 0:
			/* Uniprocessor Cypress */
			init_cypress_604();
			break;
		case 13:
		case 14:
		case 15:
			/* MP Cypress mmu/cache-controller */
			init_cypress_605(mod_rev);
			break;
		default:
			srmmu_is_bad();
			break;
		};
		return;
	}

	/* Next check for Fujitsu Swift. */
	if(psr_typ == 0 && psr_vers == 4) {
		init_swift();
		return;
	}

	/* Now the Viking family of srmmu. */
	if(psr_typ == 4 &&
	   ((psr_vers == 0) ||
	    ((psr_vers == 1) && (mod_typ == 0) && (mod_rev == 0)))) {
		init_viking(psr_vers, mod_rev);
		return;
	}

	/* Finally the Tsunami. */
	if(psr_typ == 4 && psr_vers == 1 && (mod_typ || mod_rev)) {
		init_tsunami(mreg);
		return;
	}

	/* Oh well */
	srmmu_is_bad();
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

static void patch_window_trap_handlers(void)
{
	unsigned long *iaddr, *daddr;
	
	PATCH_BRANCH(spwin_mmu_patchme, spwin_srmmu_stackchk);
	PATCH_BRANCH(fwin_mmu_patchme, srmmu_fwin_stackchk);
	PATCH_BRANCH(tsetup_mmu_patchme, tsetup_srmmu_stackchk);
	PATCH_BRANCH(rtrap_mmu_patchme, srmmu_rett_stackchk);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_TFLT].inst_three, srmmu_fault);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_DFLT].inst_three, srmmu_fault);
}

/* Load up routines and constants for sun4m mmu */
void ld_mmu_srmmu(void)
{
	prom_printf("Loading srmmu MMU routines\n");

	/* First the constants */
	pmd_shift = SRMMU_PMD_SHIFT;
	pmd_size = SRMMU_PMD_SIZE;
	pmd_mask = SRMMU_PMD_MASK;
	pgdir_shift = SRMMU_PGDIR_SHIFT;
	pgdir_size = SRMMU_PGDIR_SIZE;
	pgdir_mask = SRMMU_PGDIR_MASK;

	ptrs_per_pte = SRMMU_PTRS_PER_PTE;
	ptrs_per_pmd = SRMMU_PTRS_PER_PMD;
	ptrs_per_pgd = SRMMU_PTRS_PER_PGD;

	page_none = SRMMU_PAGE_NONE;
	page_shared = SRMMU_PAGE_SHARED;
	page_copy = SRMMU_PAGE_COPY;
	page_readonly = SRMMU_PAGE_RDONLY;
	page_kernel = SRMMU_PAGE_KERNEL;
	pg_iobits = SRMMU_VALID | SRMMU_WRITE | SRMMU_REF;
	    
	/* Functions */
	set_pte = srmmu_set_pte;
	switch_to_context = srmmu_switch_to_context;
	pmd_align = srmmu_pmd_align;
	pgdir_align = srmmu_pgdir_align;
	vmalloc_start = srmmu_vmalloc_start;

	pte_page = srmmu_pte_page;
	pmd_page = srmmu_pmd_page;
	pgd_page = srmmu_pgd_page;

	sparc_update_rootmmu_dir = srmmu_update_rootmmu_dir;

	pte_none = srmmu_pte_none;
	pte_present = srmmu_pte_present;
	pte_inuse = srmmu_pte_inuse;
	pte_clear = srmmu_pte_clear;
	pte_reuse = srmmu_pte_reuse;

	pmd_none = srmmu_pmd_none;
	pmd_bad = srmmu_pmd_bad;
	pmd_present = srmmu_pmd_present;
	pmd_inuse = srmmu_pmd_inuse;
	pmd_clear = srmmu_pmd_clear;
	pmd_reuse = srmmu_pmd_reuse;

	pgd_none = srmmu_pgd_none;
	pgd_bad = srmmu_pgd_bad;
	pgd_present = srmmu_pgd_present;
	pgd_inuse = srmmu_pgd_inuse;
	pgd_clear = srmmu_pgd_clear;
	pgd_reuse = srmmu_pgd_reuse;

	mk_pte = srmmu_mk_pte;
	pgd_set = srmmu_pgd_set;
	mk_pte_io = srmmu_mk_pte_io;
	pte_modify = srmmu_pte_modify;
	pgd_offset = srmmu_pgd_offset;
	pmd_offset = srmmu_pmd_offset;
	pte_offset = srmmu_pte_offset;
	pte_free_kernel = srmmu_pte_free_kernel;
	pmd_free_kernel = srmmu_pmd_free_kernel;
	pte_alloc_kernel = srmmu_pte_alloc_kernel;
	pmd_alloc_kernel = srmmu_pmd_alloc_kernel;
	pte_free = srmmu_pte_free;
	pte_alloc = srmmu_pte_alloc;
	pmd_free = srmmu_pmd_free;
	pmd_alloc = srmmu_pmd_alloc;
	pgd_free = srmmu_pgd_free;
	pgd_alloc = srmmu_pgd_alloc;

	pte_write = srmmu_pte_write;
	pte_dirty = srmmu_pte_dirty;
	pte_young = srmmu_pte_young;
	pte_wrprotect = srmmu_pte_wrprotect;
	pte_mkclean = srmmu_pte_mkclean;
	pte_mkold = srmmu_pte_mkold;
	pte_mkwrite = srmmu_pte_mkwrite;
	pte_mkdirty = srmmu_pte_mkdirty;
	pte_mkyoung = srmmu_pte_mkyoung;
	update_mmu_cache = srmmu_update_mmu_cache;
	mmu_exit_hook = srmmu_exit_hook;
	mmu_flush_hook = srmmu_flush_hook;
	mmu_lockarea = srmmu_lockarea;
	mmu_unlockarea = srmmu_unlockarea;
	mmu_get_scsi_buffer = srmmu_get_scsi_buffer;
	mmu_release_scsi_buffer = srmmu_release_scsi_buffer;
	mmu_info = srmmu_mmu_info;

	/* Task struct and kernel stack allocating/freeing. */
	alloc_kernel_stack = srmmu_alloc_kernel_stack;
	alloc_task_struct = srmmu_alloc_task_struct;
	free_kernel_stack = srmmu_free_kernel_stack;
	free_task_struct = srmmu_free_task_struct;

	quick_kernel_fault = srmmu_quick_kernel_fault;

	get_srmmu_type();
	if(!(srmmu_get_mmureg() & 0x800)) {
		srmmu_read_physical = msparc_read_physical;
		srmmu_write_physical = msparc_write_physical;
	} else {
		srmmu_read_physical = gensrmmu_read_physical;
		srmmu_write_physical = gensrmmu_write_physical;
	}
	patch_window_trap_handlers();
}
