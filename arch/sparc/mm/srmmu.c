/* $Id: srmmu.c,v 1.62 1996/04/25 09:11:47 davem Exp $
 * srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Peter A. Zaitcev (zaitcev@ithil.mcst.ru)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@pool.informatik.rwth-aachen.de)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>

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

/* Now the cpu specific definitions. */
#include <asm/viking.h>
#include <asm/mxcc.h>
#include <asm/ross.h>
#include <asm/tsunami.h>
#include <asm/swift.h>

enum mbus_module srmmu_modtype;
unsigned int hwbug_bitmask;
int hyper_cache_size;
int hyper_line_size;

#ifdef __SMP__
extern void smp_capture(void);
extern void smp_release(void);
#else
#define smp_capture()
#define smp_release()
#endif /* !(__SMP__) */

static void (*ctxd_set)(ctxd_t *ctxp, pgd_t *pgdp);
static void (*pmd_set)(pmd_t *pmdp, pte_t *ptep);

static void (*flush_page_for_dma)(unsigned long page);
static void (*flush_cache_page_to_uncache)(unsigned long page);
static void (*flush_tlb_page_for_cbit)(unsigned long page);
#ifdef __SMP__
static void (*local_flush_page_for_dma)(unsigned long page);
static void (*local_flush_cache_page_to_uncache)(unsigned long page);
static void (*local_flush_tlb_page_for_cbit)(unsigned long page);
#endif

static struct srmmu_stats {
	int invall;
	int invpg;
	int invrnge;
	int invmm;
} module_stats;

static char *srmmu_name;

ctxd_t *srmmu_ctx_table_phys;
ctxd_t *srmmu_context_table;

static struct srmmu_trans {
	unsigned long vbase;
	unsigned long pbase;
	int size;
} srmmu_map[SPARC_PHYS_BANKS];

static int can_cache_ptables = 0;
static int viking_mxcc_present = 0;

/* Physical memory can be _very_ non-contiguous on the sun4m, especially
 * the SS10/20 class machines and with the latest openprom revisions.
 * So we have to crunch the free page pool.
 */
static inline unsigned long srmmu_v2p(unsigned long vaddr)
{
	int i;

	for(i=0; srmmu_map[i].size != 0; i++) {
		if(srmmu_map[i].vbase <= vaddr &&
		   (srmmu_map[i].vbase + srmmu_map[i].size > vaddr))
			return (vaddr - srmmu_map[i].vbase) + srmmu_map[i].pbase;
	}
	return 0xffffffffUL;
}

static inline unsigned long srmmu_p2v(unsigned long paddr)
{
	int i;

	for(i=0; srmmu_map[i].size != 0; i++) {
		if(srmmu_map[i].pbase <= paddr &&
		   (srmmu_map[i].pbase + srmmu_map[i].size > paddr))
			return (paddr - srmmu_map[i].pbase) + srmmu_map[i].vbase;
	}
	return 0xffffffffUL;
}

/* In general all page table modifications should use the V8 atomic
 * swap instruction.  This insures the mmu and the cpu are in sync
 * with respect to ref/mod bits in the page tables.
 */
static inline unsigned long srmmu_swap(unsigned long *addr, unsigned long value)
{
#if CONFIG_AP1000
  /* the AP1000 has its memory on bus 8, not 0 like suns do */
  if (!(value&0xf0000000))
    value |= 0x80000000;
  if (value == 0x80000000) value = 0;
#endif
	__asm__ __volatile__("swap [%2], %0\n\t" :
			     "=&r" (value) :
			     "0" (value), "r" (addr));
	return value;
}

/* Functions really use this, not srmmu_swap directly. */
#define srmmu_set_entry(ptr, newentry) \
        srmmu_swap((unsigned long *) (ptr), (newentry))


/* The very generic SRMMU page table operations. */
static unsigned int srmmu_pmd_align(unsigned int addr) { return SRMMU_PMD_ALIGN(addr); }
static unsigned int srmmu_pgdir_align(unsigned int addr) { return SRMMU_PGDIR_ALIGN(addr); }

static unsigned long srmmu_vmalloc_start(void)
{
	return SRMMU_VMALLOC_START;
}

static unsigned long srmmu_pgd_page(pgd_t pgd)
{ return srmmu_p2v((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pmd_page(pmd_t pmd)
{ return srmmu_p2v((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pte_page(pte_t pte)
{ return srmmu_p2v((pte_val(pte) & SRMMU_PTE_PMASK) << 4); }

static int srmmu_pte_none(pte_t pte)          { return !pte_val(pte); }
static int srmmu_pte_present(pte_t pte)
{ return ((pte_val(pte) & SRMMU_ET_MASK) == SRMMU_ET_PTE); }

static void srmmu_pte_clear(pte_t *ptep)      { set_pte(ptep, __pte(0)); }

static int srmmu_pmd_none(pmd_t pmd)          { return !pmd_val(pmd); }
static int srmmu_pmd_bad(pmd_t pmd)
{ return (pmd_val(pmd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static int srmmu_pmd_present(pmd_t pmd)
{ return ((pmd_val(pmd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static void srmmu_pmd_clear(pmd_t *pmdp)      { set_pte((pte_t *)pmdp, __pte(0)); }

static int srmmu_pgd_none(pgd_t pgd)          { return !pgd_val(pgd); }
static int srmmu_pgd_bad(pgd_t pgd)
{ return (pgd_val(pgd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static int srmmu_pgd_present(pgd_t pgd)
{ return ((pgd_val(pgd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static void srmmu_pgd_clear(pgd_t * pgdp)     { set_pte((pte_t *)pgdp, __pte(0)); }

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
{ pte_t pte; pte_val(pte) = ((srmmu_v2p(page)) >> 4) | pgprot_val(pgprot); return pte; }

static pte_t srmmu_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{
	pte_t pte;
	pte_val(pte) = ((page) >> 4) | (space << 28) | pgprot_val(pgprot);
	return pte;
}

static void srmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ 
	srmmu_set_entry(ctxp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pgdp) >> 4)));
}

static void srmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	srmmu_set_entry(pgdp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pmdp) >> 4)));
}

static void srmmu_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	srmmu_set_entry(pmdp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) ptep) >> 4)));
}

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
	return (pmd_t *) srmmu_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
static pte_t *srmmu_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) srmmu_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

/* This must update the context table entry for this process. */
static void srmmu_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	if(tsk->mm->context != NO_CONTEXT)
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
}

static inline void srmmu_uncache_page(unsigned long addr)
{
	pgd_t *pgdp = srmmu_pgd_offset(init_task.mm, addr);
	pmd_t *pmdp = srmmu_pmd_offset(pgdp, addr);
	pte_t *ptep = srmmu_pte_offset(pmdp, addr);

	flush_cache_page_to_uncache(addr);
	set_pte(ptep, __pte((pte_val(*ptep) & ~SRMMU_CACHE)));
	flush_tlb_page_for_cbit(addr);
}

static inline void srmmu_recache_page(unsigned long addr)
{
	pgd_t *pgdp = srmmu_pgd_offset(init_task.mm, addr);
	pmd_t *pmdp = srmmu_pmd_offset(pgdp, addr);
	pte_t *ptep = srmmu_pte_offset(pmdp, addr);

	set_pte(ptep, __pte((pte_val(*ptep) | SRMMU_CACHE)));
	flush_tlb_page_for_cbit(addr);
}

static inline unsigned long srmmu_getpage(void)
{
	unsigned long page = get_free_page(GFP_KERNEL);

	if (can_cache_ptables)
		return page;

	if(page)
		srmmu_uncache_page(page);
	return page;
}

static inline void srmmu_putpage(unsigned long page)
{
	if (!can_cache_ptables)
		srmmu_recache_page(page);
	free_page(page);
}

/* The easy versions. */
#define NEW_PGD() (pgd_t *) srmmu_getpage()
#define NEW_PMD() (pmd_t *) srmmu_getpage()
#define NEW_PTE() (pte_t *) srmmu_getpage()
#define FREE_PGD(chunk) srmmu_putpage((unsigned long)(chunk))
#define FREE_PMD(chunk) srmmu_putpage((unsigned long)(chunk))
#define FREE_PTE(chunk) srmmu_putpage((unsigned long)(chunk))

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
static void srmmu_pte_free_kernel(pte_t *pte)
{
	FREE_PTE(pte);
}

static pte_t *srmmu_pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if(srmmu_pmd_none(*pmd)) {
		pte_t *page = NEW_PTE();
		if(srmmu_pmd_none(*pmd)) {
			if(page) {
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		FREE_PTE(page);
	}
	if(srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) srmmu_pmd_page(*pmd) + address;
}

static void srmmu_pmd_free_kernel(pmd_t *pmd)
{
	FREE_PMD(pmd);
}

static pmd_t *srmmu_pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if(srmmu_pgd_none(*pgd)) {
		pmd_t *page = NEW_PMD();
		if(srmmu_pgd_none(*pgd)) {
			if(page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		FREE_PMD(page);
	}
	if(srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

static void srmmu_pte_free(pte_t *pte)
{
	FREE_PTE(pte);
}

static pte_t *srmmu_pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if(srmmu_pmd_none(*pmd)) {
		pte_t *page = NEW_PTE();
		if(srmmu_pmd_none(*pmd)) {
			if(page) {
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		FREE_PTE(page);
	}
	if(srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return ((pte_t *) srmmu_pmd_page(*pmd)) + address;
}

/* Real three-level page tables on SRMMU. */
static void srmmu_pmd_free(pmd_t * pmd)
{
	FREE_PMD(pmd);
}

static pmd_t *srmmu_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if(srmmu_pgd_none(*pgd)) {
		pmd_t *page = NEW_PMD();
		if(srmmu_pgd_none(*pgd)) {
			if(page) {
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		FREE_PMD(page);
	}
	if(srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) srmmu_pgd_page(*pgd) + address;
}

static void srmmu_pgd_free(pgd_t *pgd)
{
	FREE_PGD(pgd);
}

static pgd_t *srmmu_pgd_alloc(void)
{
	return NEW_PGD();
}

static void srmmu_set_pte(pte_t *ptep, pte_t pteval)
{
	srmmu_set_entry(ptep, pte_val(pteval));
}

static void srmmu_quick_kernel_fault(unsigned long address)
{
	printk("Penguin faults at address %08lx\n", address);
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
	flush_cache_mm(ctxp->ctx_mm);
	flush_tlb_mm(ctxp->ctx_mm);
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
		ctxd_set(&srmmu_context_table[tsk->mm->context], tsk->mm->pgd);
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
	flush_page_to_ram(virt_addr);
	srmmu_set_entry(ptep, tmp);
	flush_tlb_all();
}

static char *srmmu_lockarea(char *vaddr, unsigned long len)
{
	return vaddr;
}

static void srmmu_unlockarea(char *vaddr, unsigned long len)
{
}

/* On the SRMMU we do not have the problems with limited tlb entries
 * for mapping kernel pages, so we just take things from the free page
 * pool.  As a side effect we are putting a little too much pressure
 * on the gfp() subsystem.  This setup also makes the logic of the
 * iommu mapping code a lot easier as we can transparently handle
 * mappings on the kernel stack without any special code as we did
 * need on the sun4c.
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

	pages = __get_free_pages(GFP_KERNEL, 2, 0);
	if(!pages)
		return 0;
	memset((void *) pages, 0, (PAGE_SIZE << 2));
	return pages;
}

static void srmmu_free_task_struct(struct task_struct *tsk)
{
	free_page((unsigned long) tsk);
}

static void srmmu_free_kernel_stack(unsigned long stack)
{
	free_pages(stack, 2);
}

/* Tsunami flushes.  It's page level tlb invalidation is not very
 * useful at all, you must be in the context that page exists in to
 * get a match.
 */
static void tsunami_flush_cache_all(void)
{
	flush_user_windows();
	tsunami_flush_icache();
	tsunami_flush_dcache();
}

static void tsunami_flush_cache_mm(struct mm_struct *mm)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		tsunami_flush_icache();
		tsunami_flush_dcache();
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		tsunami_flush_icache();
		tsunami_flush_dcache();
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
#ifndef __SMP__
	struct mm_struct *mm = vma->vm_mm;
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		tsunami_flush_icache();
		tsunami_flush_dcache();
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_cache_page_to_uncache(unsigned long page)
{
	tsunami_flush_dcache();
}

/* Tsunami does not have a Copy-back style virtual cache. */
static void tsunami_flush_page_to_ram(unsigned long page)
{
	tsunami_flush_icache();
	tsunami_flush_dcache();
}

/* However, Tsunami is not IO coherent. */
static void tsunami_flush_page_for_dma(unsigned long page)
{
	tsunami_flush_icache();
	tsunami_flush_dcache();
}

/* TLB flushes seem to upset the tsunami sometimes, I can't figure out
 * what the hell is going on.  All I see is a tlb flush (page or whole,
 * there is no consistent pattern) and then total local variable corruption
 * in the procedure who called us after return.  Usually triggerable
 * by "cool" programs like crashme and bonnie.  I played around a bit
 * and adding a bunch of forced nops seems to make the problems all
 * go away. (missed instruction fetches possibly? ugh...)
 */
#define TSUNAMI_SUCKS do { nop(); nop(); nop(); nop(); nop(); \
	                   nop(); nop(); nop(); nop(); nop(); } while(0)

static void tsunami_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
	TSUNAMI_SUCKS;
}

static void tsunami_flush_tlb_mm(struct mm_struct *mm)
{
	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		srmmu_flush_whole_tlb();
		TSUNAMI_SUCKS;
#ifndef __SMP__
        }
#endif
}

static void tsunami_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	module_stats.invrnge++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		srmmu_flush_whole_tlb();
		TSUNAMI_SUCKS;
#ifndef __SMP__
	}
#endif
}

static void tsunami_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int octx;
	struct mm_struct *mm = vma->vm_mm;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();

		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		TSUNAMI_SUCKS;
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
	module_stats.invpg++;
}

static void tsunami_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_tlb_page(page);
}

/* Swift flushes.  It has the recommended SRMMU specification flushing
 * facilities, so we can do things in a more fine grained fashion than we
 * could on the tsunami.  Let's watch out for HARDWARE BUGS...
 */

static void swift_flush_cache_all(void)
{
	flush_user_windows();
	swift_idflash_clear();
}

static void swift_flush_cache_mm(struct mm_struct *mm)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		swift_idflash_clear();
#ifndef __SMP__
	}
#endif
}

static void swift_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		swift_idflash_clear();
#ifndef __SMP__
	}
#endif
}

static void swift_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
#ifndef __SMP__
	struct mm_struct *mm = vma->vm_mm;
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		if(vma->vm_flags & VM_EXEC)
			swift_flush_icache();
		swift_flush_dcache();
#ifndef __SMP__
	}
#endif
}

/* Not copy-back on swift. */
static void swift_flush_page_to_ram(unsigned long page)
{
}

/* But not IO coherent either. */
static void swift_flush_page_for_dma(unsigned long page)
{
	swift_flush_dcache();
}

static void swift_flush_cache_page_to_uncache(unsigned long page)
{
	swift_flush_dcache();
}

static void swift_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void swift_flush_tlb_mm(struct mm_struct *mm)
{
	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT)
#endif
		srmmu_flush_whole_tlb();
}

static void swift_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	module_stats.invrnge++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT)
#endif
		srmmu_flush_whole_tlb();
}

static void swift_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
#ifndef __SMP__
	struct mm_struct *mm = vma->vm_mm;
	if(mm->context != NO_CONTEXT)
#endif
		srmmu_flush_whole_tlb();
	module_stats.invpg++;
}

static void swift_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_whole_tlb();
}

/* The following are all MBUS based SRMMU modules, and therefore could
 * be found in a multiprocessor configuration.  On the whole, these
 * chips seems to be much more touchy about DVMA and page tables
 * with respect to cache coherency.
 */

/* Viking flushes.  For Sun's mainline MBUS processor it is pretty much
 * a crappy mmu.  The on-chip I&D caches only have full flushes, no fine
 * grained cache invalidations.  It only has these "flash clear" things
 * just like the MicroSparcI.  Added to this many revs of the chip are
 * teaming with hardware buggery.  Someday maybe we'll do direct
 * diagnostic tag accesses for page level flushes as those should
 * be painless and will increase performance due to the frequency of
 * page level flushes. This is a must to _really_ flush the caches,
 * crazy hardware ;-)
 */

static void viking_flush_cache_all(void)
{
}

static void viking_flush_cache_mm(struct mm_struct *mm)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
#ifndef __SMP__
	}
#endif
}

static void viking_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
#ifndef __SMP__
	}
#endif
}

static void viking_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
#ifndef __SMP__
	struct mm_struct *mm = vma->vm_mm;
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
#ifndef __SMP__
	}
#endif
}

/* Non-mxcc vikings are copy-back but are pure-physical so no flushing. */
static void viking_flush_page_to_ram(unsigned long page)
{
}

/* Viking is IO cache coherent. */
static void viking_flush_page_for_dma(unsigned long page)
{
}

static void viking_mxcc_flush_page(unsigned long page)
{
	unsigned long ppage = srmmu_hwprobe(page);
	unsigned long paddr0, paddr1;

	if (!ppage)
		return;

	paddr0 = (ppage >> 28) | 0x10;		/* Set cacheable bit. */
	paddr1 = (ppage << 4) & PAGE_MASK;

	/* Read the page's data through the stream registers,
	 * and write it back to memory. This will issue
	 * coherent write invalidates to all other caches, thus
         * should also be sufficient in an MP system.
	 */
	__asm__ __volatile__ ("or %%g0, %0, %%g2\n\t"
			      "or %%g0, %1, %%g3\n"
			      "1:\n\t"
			      "stda %%g2, [%2] %5\n\t"
			      "stda %%g2, [%3] %5\n\t"
			      "add %%g3, %4, %%g3\n\t"
			      "btst 0xfff, %%g3\n\t"
			      "bne 1b\n\t"
			      "nop\n\t" : :
			      "r" (paddr0), "r" (paddr1),
			      "r" (MXCC_SRCSTREAM),
			      "r" (MXCC_DESSTREAM),
			      "r" (MXCC_STREAM_SIZE),
			      "i" (ASI_M_MXCC) : "g2", "g3");

	/* This was handcoded after a look at the gcc output from
	 *
	 *	do {
	 *		mxcc_set_stream_src(paddr);
	 *		mxcc_set_stream_dst(paddr);
	 *		paddr[1] += MXCC_STREAM_SIZE;
	 *	} while (paddr[1] & ~PAGE_MASK);
	 */
}

static void viking_no_mxcc_flush_page(unsigned long page)
{
	unsigned long ppage = srmmu_hwprobe(page) >> 8;
	int set, block;
	unsigned long ptag[2];
	unsigned long vaddr;
	int i;

	if (!ppage)
		return;

	for (set = 0; set < 128; set++) {
		for (block = 0; block < 4; block++) {

			viking_get_dcache_ptag(set, block, ptag);

			if (ptag[1] != ppage)
				continue;
			if (!(ptag[0] & VIKING_PTAG_VALID))
				continue;
			if (!(ptag[0] & VIKING_PTAG_DIRTY))
				continue;

			/* There was a great cache from TI
			 * with comfort as much as vi,
			 * 4 pages to flush,
			 * 4 pages, no rush,
			 * since anything else makes him die.
			 */
			vaddr = (KERNBASE + PAGE_SIZE) | (set << 5);
			for (i = 0; i < 8; i++) {
				__asm__ __volatile__ ("ld [%0], %%g2\n\t" : :
						      "r" (vaddr) : "g2");
				vaddr += PAGE_SIZE;
			}

			/* Continue with next set. */
			break;
		}
	}
}

static void viking_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void viking_flush_tlb_mm(struct mm_struct *mm)
{
	int octx;
	module_stats.invmm++;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_ctx();
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void viking_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int octx;
	module_stats.invrnge++;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		start &= SRMMU_PMD_MASK;
		while(start < end) {
			srmmu_flush_tlb_segment(start);
			start += SRMMU_PMD_SIZE;
		}
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void viking_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int octx;
	struct mm_struct *mm = vma->vm_mm;

	module_stats.invpg++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void viking_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_tlb_page(page);
}

/* Cypress flushes. */

static void cypress_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void cypress_flush_tlb_mm(struct mm_struct *mm)
{
	int octx;

	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_ctx();
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void cypress_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int octx;

	module_stats.invrnge++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		start &= SRMMU_PMD_MASK;
		while(start < end) {
			srmmu_flush_tlb_segment(start);
			start += SRMMU_PMD_SIZE;
		}
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

static void cypress_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int octx;
	struct mm_struct *mm = vma->vm_mm;

	module_stats.invpg++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

/* Hypersparc flushes.  Very nice chip... */
static void hypersparc_flush_cache_all(void)
{
	flush_user_windows();
	hyper_flush_unconditional_combined();
	hyper_flush_whole_icache();
}

static void hypersparc_flush_cache_mm(struct mm_struct *mm)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		hyper_flush_unconditional_combined();
		hyper_flush_whole_icache();
#ifndef __SMP__
	}
#endif
}

static void hypersparc_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		flush_user_windows();
		hyper_flush_unconditional_combined();
		hyper_flush_whole_icache();
#ifndef __SMP__
	}
#endif
}

/* HyperSparc requires a valid mapping where we are about to flush
 * in order to check for a physical tag match during the flush.
 */
static void hypersparc_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	volatile unsigned long clear;
	int octx;

#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif
		octx = srmmu_get_context();
		flush_user_windows();
		srmmu_set_context(mm->context);
		hyper_flush_whole_icache();
		if(!srmmu_hwprobe(page))
			goto no_mapping;
		hyper_flush_cache_page(page);
	no_mapping:
		clear = srmmu_get_fstatus();
		srmmu_set_context(octx);
#ifndef __SMP__
	}
#endif
}

/* HyperSparc is copy-back. */
static void hypersparc_flush_page_to_ram(unsigned long page)
{
	volatile unsigned long clear;

	if(srmmu_hwprobe(page))
		hyper_flush_cache_page(page);
	clear = srmmu_get_fstatus();
}

/* HyperSparc is IO cache coherent. */
static void hypersparc_flush_page_for_dma(unsigned long page)
{
	volatile unsigned long clear;

	if(srmmu_hwprobe(page))
		hyper_flush_cache_page(page);
	clear = srmmu_get_fstatus();
}

static void hypersparc_flush_cache_page_to_uncache(unsigned long page)
{
	volatile unsigned long clear;

	if(srmmu_hwprobe(page))
		hyper_flush_cache_page(page);
	clear = srmmu_get_fstatus();
}

static void hypersparc_flush_tlb_all(void)
{
	module_stats.invall++;
	srmmu_flush_whole_tlb();
}

static void hypersparc_flush_tlb_mm(struct mm_struct *mm)
{
	int octx;

	module_stats.invmm++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif

		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_ctx();
		srmmu_set_context(octx);

#ifndef __SMP__
	}
#endif
}

static void hypersparc_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	int octx;

	module_stats.invrnge++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif

		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		start &= SRMMU_PMD_MASK;
		while(start < end) {
			srmmu_flush_tlb_segment(start);
			start += SRMMU_PMD_SIZE;
		}
		srmmu_set_context(octx);

#ifndef __SMP__
	}
#endif
}

static void hypersparc_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	int octx;

	module_stats.invpg++;
#ifndef __SMP__
	if(mm->context != NO_CONTEXT) {
#endif

		octx = srmmu_get_context();
		srmmu_set_context(mm->context);
		srmmu_flush_tlb_page(page);
		srmmu_set_context(octx);

#ifndef __SMP__
	}
#endif
}

static void hypersparc_flush_tlb_page_for_cbit(unsigned long page)
{
	srmmu_flush_tlb_page(page);
}

static void hypersparc_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{
	hyper_flush_whole_icache();
	srmmu_set_entry(ctxp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pgdp) >> 4)));
}

static void hypersparc_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	if(tsk->mm->context != NO_CONTEXT) {
		hyper_flush_whole_icache();
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
	}
}

static void hypersparc_set_pte(pte_t *ptep, pte_t pteval)
{
	/* xor is your friend */
	__asm__ __volatile__("rd	%%psr, %%g1\n\t"
			     "wr	%%g1, %4, %%psr\n\t"
			     "nop; nop; nop;\n\t"
			     "swap	[%0], %1\n\t"
			     "wr	%%g1, 0x0, %%psr\n\t"
			     "nop; nop; nop;\n\t" :
			     "=r" (ptep), "=r" (pteval) :
			     "0" (ptep), "1" (pteval), "i" (PSR_ET) :
			     "g1");
}

static void hypersparc_switch_to_context(struct task_struct *tsk)
{
	/* Kernel threads can execute in any context and so can tasks
	 * sleeping in the middle of exiting. If this task has already
	 * been allocated a piece of the mmu realestate, just jump to
	 * it.
	 */
	hyper_flush_whole_icache();
	if((tsk->tss.flags & SPARC_FLAG_KTHREAD) ||
	   (tsk->flags & PF_EXITING))
		return;
	if(tsk->mm->context == NO_CONTEXT) {
		alloc_context(tsk->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], tsk->mm->pgd);
	}
	srmmu_set_context(tsk->mm->context);
}

/* IOMMU things go here. */

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))
static unsigned long first_dvma_page, last_dvma_page;

#define IOPERM        (IOPTE_CACHE | IOPTE_WRITE | IOPTE_VALID)
#define MKIOPTE(phys) (((((phys)>>4) & IOPTE_PAGE) | IOPERM) & ~IOPTE_WAZ)

static inline void srmmu_map_dvma_pages_for_iommu(struct iommu_struct *iommu)
{
	unsigned long first = first_dvma_page;
	unsigned long last = last_dvma_page;
	iopte_t *iopte;

	iopte = iommu->page_table;
	iopte += ((DVMA_VADDR - iommu->start) >> PAGE_SHIFT);
	while(first <= last) {
		iopte_val(*iopte++) = MKIOPTE(srmmu_v2p(first));
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
	iommu->plow = iommu->start = 0xfc000000;
	iommu->end = 0xffffffff;

	/* Allocate IOMMU page table */
	ptsize = iommu->end - iommu->start + 1;
	ptsize = (ptsize >> PAGE_SHIFT) * sizeof(iopte_t);

	/* Stupid alignment constraints give me a headache. */
	memory_start = PAGE_ALIGN(memory_start);
	memory_start = (((memory_start) + (ptsize - 1)) & ~(ptsize - 1));
	iommu->lowest = iommu->page_table = (iopte_t *) memory_start;
	memory_start += ptsize;

	/* Initialize new table. */
	flush_cache_all();
	srmmu_uncache_iommu_page_table((unsigned long) iommu->page_table, ptsize);
	flush_tlb_all();
	memset(iommu->page_table, 0, ptsize);
	srmmu_map_dvma_pages_for_iommu(iommu);
	iommu->regs->base = srmmu_v2p((unsigned long) iommu->page_table) >> 4;
	iommu_invalidate(iommu->regs);

	sbus->iommu = iommu;
	printk("IOMMU: impl %d vers %d page table at %p of size %d bytes\n",
	       impl, vers, iommu->page_table, ptsize);
	return memory_start;
}

static char *srmmu_get_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
	unsigned long page = (unsigned long) vaddr;
	unsigned long start, end, offset;
	iopte_t *iopte;

	offset = page & ~PAGE_MASK;
	page &= PAGE_MASK;

	start = iommu->plow;
	end = KADB_DEBUGGER_BEGVM; /* Don't step on kadb/prom. */
	iopte = iommu->lowest;
	while(start < end) {
		if(!(iopte_val(*iopte) & IOPTE_VALID))
			break;
		iopte++;
		start += PAGE_SIZE;
	}

	flush_page_for_dma(page);
	vaddr = (char *) (start | offset);
	iopte_val(*iopte) = MKIOPTE(srmmu_v2p(page));
	iommu_invalidate_page(iommu->regs, start);
	iommu->lowest = iopte + 1;
	iommu->plow = start + PAGE_SIZE;

	return vaddr;
}

static void srmmu_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
        unsigned long page, start, end, offset;
	iopte_t *iopte = iommu->lowest;

	start = iommu->plow;
	end = KADB_DEBUGGER_BEGVM;
	while(sz >= 0) {
		page = ((unsigned long) sg[sz].addr) & PAGE_MASK;
		offset = ((unsigned long) sg[sz].addr) & ~PAGE_MASK;
		while(start < end) {
			if(!(iopte_val(*iopte) & IOPTE_VALID))
				break;
			iopte++;
			start += PAGE_SIZE;
		}
		if(start == KADB_DEBUGGER_BEGVM)
			panic("Wheee, iomapping overflow.");
		flush_page_for_dma(page);
		sg[sz].alt_addr = (char *) (start | offset);
		iopte_val(*iopte) = MKIOPTE(srmmu_v2p(page));
		iommu_invalidate_page(iommu->regs, start);
		iopte++;
		start += PAGE_SIZE;
		sz--;
	}
	iommu->lowest = iopte;
	iommu->plow = start;
}

static void srmmu_release_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
	unsigned long page = (unsigned long) vaddr;
	iopte_t *iopte;

	if(len > PAGE_SIZE)
		panic("Can only handle page sized IOMMU mappings.");
	page &= PAGE_MASK;
	iopte = iommu->page_table + ((page - iommu->start) >> PAGE_SHIFT);
	iopte_val(*iopte) = 0;
	iommu_invalidate_page(iommu->regs, page);
	if(iopte < iommu->lowest) {
		iommu->lowest = iopte;
		iommu->plow = page;
	}
}

static void srmmu_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	struct iommu_struct *iommu = sbus->iommu;
	unsigned long page;
	iopte_t *iopte;

	while(sz >= 0) {
		page = ((unsigned long)sg[sz].alt_addr) & PAGE_MASK;
		iopte = iommu->page_table + ((page - iommu->start) >> PAGE_SHIFT);
		iopte_val(*iopte) = 0;
		iommu_invalidate_page(iommu->regs, page);
		if(iopte < iommu->lowest) {
			iommu->lowest = iopte;
			iommu->plow = page;
		}
		sg[sz].alt_addr = 0;
		sz--;
	}
}

static unsigned long mempool;

/* NOTE: All of this startup code assumes the low 16mb (approx.) of
 *       kernel mappings are done with one single contiguous chunk of
 *       ram.  On small ram machines (classics mainly) we only get
 *       around 8mb mapped for us.
 */

static unsigned long kbpage;

/* Some dirty hacks to abstract away the painful boot up init. */
static inline unsigned long srmmu_early_paddr(unsigned long vaddr)
{
	return ((vaddr - PAGE_OFFSET) + kbpage);
}

static inline void srmmu_early_pgd_set(pgd_t *pgdp, pmd_t *pmdp)
{
	srmmu_set_entry(pgdp, (SRMMU_ET_PTD | (srmmu_early_paddr((unsigned long) pmdp) >> 4)));
}

static inline void srmmu_early_pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	srmmu_set_entry(pmdp, (SRMMU_ET_PTD | (srmmu_early_paddr((unsigned long) ptep) >> 4)));
}

static inline unsigned long srmmu_early_pgd_page(pgd_t pgd)
{
	return (((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4) - kbpage) + PAGE_OFFSET;
}

static inline unsigned long srmmu_early_pmd_page(pmd_t pmd)
{
	return (((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4) - kbpage) + PAGE_OFFSET;
}

static inline pmd_t *srmmu_early_pmd_offset(pgd_t *dir, unsigned long address)
{
	return (pmd_t *) srmmu_early_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

static inline pte_t *srmmu_early_pte_offset(pmd_t *dir, unsigned long address)
{
	return (pte_t *) srmmu_early_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

/* Allocate a block of RAM which is aligned to its size.
 * This procedure can be used until the call to mem_init().
 */
static void *srmmu_init_alloc(unsigned long *kbrk, unsigned long size)
{
	unsigned long mask = size - 1;
	unsigned long ret;

	if(!size)
		return 0x0;
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
			srmmu_early_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = srmmu_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_early_pmd_set(pmdp, ptep);
		}
		start = (start + SRMMU_PMD_SIZE) & SRMMU_PMD_MASK;
	}
}

/* This is much cleaner than poking around physical address space
 * looking at the prom's page table directly which is what most
 * other OS's do.  Yuck... this is much better.
 */
void srmmu_inherit_prom_mappings(unsigned long start,unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int what = 0; /* 0 = normal-pte, 1 = pmd-level pte, 2 = pgd-level pte */
	unsigned long prompte;

	while(start <= end) {
		if (start == 0)
			break; /* probably wrap around */
		if(start == 0xfef00000)
			start = KADB_DEBUGGER_BEGVM;
		if(!(prompte = srmmu_hwprobe(start))) {
			start += PAGE_SIZE;
			continue;
		}
    
		/* A red snapper, see what it really is. */
		what = 0;
    
		if(!(start & ~(SRMMU_PMD_MASK))) {
			if(srmmu_hwprobe((start-PAGE_SIZE) + SRMMU_PMD_SIZE) == prompte)
				what = 1;
		}
    
		if(!(start & ~(SRMMU_PGDIR_MASK))) {
			if(srmmu_hwprobe((start-PAGE_SIZE) + SRMMU_PGDIR_SIZE) ==
			   prompte)
				what = 2;
		}
    
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		if(what == 2) {
			pgd_val(*pgdp) = prompte;
			start += SRMMU_PGDIR_SIZE;
			continue;
		}
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = srmmu_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
			srmmu_early_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		if(what == 1) {
			pmd_val(*pmdp) = prompte;
			start += SRMMU_PMD_SIZE;
			continue;
		}
		if(srmmu_pmd_none(*pmdp)) {
			ptep = srmmu_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_early_pmd_set(pmdp, ptep);
		}
		ptep = srmmu_early_pte_offset(pmdp, start);
		pte_val(*ptep) = prompte;
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
	if (viking_mxcc_present)
		dvma_prot = __pgprot(SRMMU_CACHE | SRMMU_ET_PTE | SRMMU_PRIV);
	else
		dvma_prot = __pgprot(SRMMU_ET_PTE | SRMMU_PRIV);
	while(first <= last) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		pmdp = srmmu_pmd_offset(pgdp, start);
		ptep = srmmu_pte_offset(pmdp, start);

		srmmu_set_entry(ptep, pte_val(srmmu_mk_pte(first, dvma_prot)));

		first += PAGE_SIZE;
		start += PAGE_SIZE;
	}

	/* Uncache DVMA pages. */
	if (!viking_mxcc_present) {
		first = first_dvma_page;
		last = last_dvma_page;
		while(first <= last) {
			pgdp = srmmu_pgd_offset(init_task.mm, first);
			pmdp = srmmu_pmd_offset(pgdp, first);
			ptep = srmmu_pte_offset(pmdp, first);
			pte_val(*ptep) &= ~SRMMU_CACHE;
			first += PAGE_SIZE;
		}
	}
}

static void srmmu_map_kernel(unsigned long start, unsigned long end)
{
	unsigned long last_page;
	int srmmu_bank, phys_bank, i;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	end = PAGE_ALIGN(end);

	if(start == (KERNBASE + PAGE_SIZE)) {
		unsigned long pte;
		unsigned long tmp;

		pgdp = srmmu_pgd_offset(init_task.mm, KERNBASE);
		pmdp = srmmu_early_pmd_offset(pgdp, KERNBASE);
		ptep = srmmu_early_pte_offset(pmdp, KERNBASE);

		/* Put a real mapping in for the KERNBASE page. */
		tmp = kbpage;
		pte = (tmp) >> 4;
		pte |= (SRMMU_CACHE | SRMMU_PRIV | SRMMU_VALID);
		pte_val(*ptep) = pte;
	}

	/* Copy over mappings prom already gave us. */
	last_page = (srmmu_hwprobe(start) & SRMMU_PTE_PMASK) << 4;
	while((srmmu_hwprobe(start) & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
		unsigned long tmp;

		pgdp = srmmu_pgd_offset(init_task.mm, start);
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		ptep = srmmu_early_pte_offset(pmdp, start);
		tmp = srmmu_hwprobe(start);
		tmp &= ~(0xff);
		tmp |= (SRMMU_CACHE | SRMMU_PRIV | SRMMU_VALID);
		pte_val(*ptep) = tmp;
		start += PAGE_SIZE;
		tmp = (srmmu_hwprobe(start) & SRMMU_PTE_PMASK) << 4;

		/* Never a cross bank boundary, thank you. */
		if(tmp != last_page + PAGE_SIZE)
			break;
		last_page = tmp;
	}

	/* Ok, that was assumed to be one full bank, begin
	 * construction of srmmu_map[].
	 */
	for(phys_bank = 0; sp_banks[phys_bank].num_bytes != 0; phys_bank++) {
		if(kbpage >= sp_banks[phys_bank].base_addr &&
		   (kbpage <
		    (sp_banks[phys_bank].base_addr + sp_banks[phys_bank].num_bytes)))
			break; /* found it */
	}
	srmmu_bank = 0;
	srmmu_map[srmmu_bank].vbase = KERNBASE;
	srmmu_map[srmmu_bank].pbase = sp_banks[phys_bank].base_addr;
	srmmu_map[srmmu_bank].size = sp_banks[phys_bank].num_bytes;
	if(kbpage != sp_banks[phys_bank].base_addr) {
		prom_printf("Detected PenguinPages, getting out of here.\n");
		prom_halt();
#if 0
		srmmu_map[srmmu_bank].pbase = kbpage;
		srmmu_map[srmmu_bank].size -=
			(kbpage - sp_banks[phys_bank].base_addr);
#endif
	}
	/* Prom didn't map all of this first bank, fill
	 * in the rest by hand.
	 */
	while(start < (srmmu_map[srmmu_bank].vbase + srmmu_map[srmmu_bank].size)) {
		unsigned long pteval;

		pgdp = srmmu_pgd_offset(init_task.mm, start);
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		ptep = srmmu_early_pte_offset(pmdp, start);

		pteval = (start - KERNBASE + srmmu_map[srmmu_bank].pbase) >> 4;
		pteval |= (SRMMU_VALID | SRMMU_CACHE | SRMMU_PRIV);
		pte_val(*ptep) = pteval;
		start += PAGE_SIZE;
	}

	/* Mark this sp_bank invalid... */
	sp_banks[phys_bank].base_addr |= 1;
	srmmu_bank++;

	/* Now, deal with what is left. */
	while(start < end) {
		unsigned long baddr;
		int btg;

		/* Find a usable cluster of physical ram. */
		for(i=0; sp_banks[i].num_bytes != 0; i++)
			if(!(sp_banks[i].base_addr & 1))
				break;
		if(sp_banks[i].num_bytes == 0)
			break;

		/* Add it to srmmu_map */
		srmmu_map[srmmu_bank].vbase = start;
		srmmu_map[srmmu_bank].pbase = sp_banks[i].base_addr;
		srmmu_map[srmmu_bank].size = sp_banks[i].num_bytes;
		srmmu_bank++;

		btg = sp_banks[i].num_bytes;
		baddr = sp_banks[i].base_addr;
		while(btg) {
			pgdp = srmmu_pgd_offset(init_task.mm, start);
			pmdp = srmmu_early_pmd_offset(pgdp, start);
			ptep = srmmu_early_pte_offset(pmdp, start);
			pte_val(*ptep) = (SRMMU_VALID | SRMMU_CACHE | SRMMU_PRIV);
			pte_val(*ptep) |= (baddr >> 4);

			baddr += PAGE_SIZE;
			start += PAGE_SIZE;
			btg -= PAGE_SIZE;
		}
		sp_banks[i].base_addr |= 1;
	}
	if(start < end) {
		prom_printf("weird, didn't use all of physical memory... ");
		prom_halt();
	}
	for(phys_bank = 0; sp_banks[phys_bank].num_bytes != 0; phys_bank++)
		sp_banks[phys_bank].base_addr &= ~1;
#if 0
	for(i = 0; srmmu_map[i].size != 0; i++) {
		prom_printf("srmmu_map[%d]: vbase=%08lx pbase=%08lx size=%d\n",
			    i, srmmu_map[i].vbase,
			    srmmu_map[i].pbase, srmmu_map[i].size);
	}
	prom_getchar();
	for(i = 0; sp_banks[i].num_bytes != 0; i++) {
		prom_printf("sp_banks[%d]: base_addr=%08lx num_bytes=%d\n",
			    i,
			    sp_banks[i].base_addr,
			    sp_banks[i].num_bytes);
	}
	prom_getchar();
	prom_halt();
#endif
}

/* Paging initialization on the Sparc Reference MMU. */
extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long sparc_context_init(unsigned long, int);

extern int physmem_mapped_contig;
extern int linux_num_cpus;

void (*poke_srmmu)(void);

unsigned long srmmu_paging_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long ptables_start, first_mapped_page;
	int i, cpunode;
	char node_str[128];
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	physmem_mapped_contig = 0; /* for init.c:taint_real_pages() */

#if CONFIG_AP1000
        printk("Forcing num_contexts to 1024\n");
        num_contexts = 1024;
#else
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
#endif
	if(!num_contexts) {
		prom_printf("Something wrong, cant find cpu node in paging_init.\n");
		prom_halt();
	}
		
	ptables_start = mempool = PAGE_ALIGN(start_mem);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	first_mapped_page = KERNBASE;
	kbpage = srmmu_hwprobe(KERNBASE);
	if((kbpage & SRMMU_ET_MASK) != SRMMU_ET_PTE) {
		kbpage = srmmu_hwprobe(KERNBASE + PAGE_SIZE);
		kbpage = (kbpage & SRMMU_PTE_PMASK) << 4;
		kbpage -= PAGE_SIZE;
		first_mapped_page += PAGE_SIZE;
	} else
		kbpage = (kbpage & SRMMU_PTE_PMASK) << 4;

	srmmu_allocate_ptable_skeleton(KERNBASE, end_mem);
#if CONFIG_SUN_IO
	srmmu_allocate_ptable_skeleton(IOBASE_VADDR, IOBASE_END);
	srmmu_allocate_ptable_skeleton(DVMA_VADDR, DVMA_END);
#endif

	/* Steal DVMA pages now, I still don't like how we waste all this. */
	mempool = PAGE_ALIGN(mempool);
	first_dvma_page = mempool;
	last_dvma_page = (mempool + (DVMA_LEN) - PAGE_SIZE);
	mempool = last_dvma_page + PAGE_SIZE;

#if CONFIG_AP1000
        ap_inherit_mappings();
#else
        srmmu_inherit_prom_mappings(0xfe400000,(LINUX_OPPROM_ENDVM-PAGE_SIZE));
#endif
	srmmu_map_kernel(first_mapped_page, end_mem);
#if CONFIG_SUN_IO
	srmmu_map_dvma_pages_for_cpu(first_dvma_page, last_dvma_page);
#endif
	srmmu_context_table = srmmu_init_alloc(&mempool, num_contexts*sizeof(ctxd_t));
	srmmu_ctx_table_phys = (ctxd_t *) srmmu_v2p((unsigned long) srmmu_context_table);
	for(i = 0; i < num_contexts; i++)
		ctxd_set(&srmmu_context_table[i], swapper_pg_dir);

	start_mem = PAGE_ALIGN(mempool);

	/* Some SRMMU's are _very_ stupid indeed. */
	if(!can_cache_ptables) {
		for( ; ptables_start < start_mem; ptables_start += PAGE_SIZE) {
			pgdp = srmmu_pgd_offset(init_task.mm, ptables_start);
			pmdp = srmmu_early_pmd_offset(pgdp, ptables_start);
			ptep = srmmu_early_pte_offset(pmdp, ptables_start);
			pte_val(*ptep) &= ~SRMMU_CACHE;
		}

		pgdp = srmmu_pgd_offset(init_task.mm, (unsigned long)swapper_pg_dir);
		pmdp = srmmu_early_pmd_offset(pgdp, (unsigned long)swapper_pg_dir);
		ptep = srmmu_early_pte_offset(pmdp, (unsigned long)swapper_pg_dir);
		pte_val(*ptep) &= ~SRMMU_CACHE;
	}

	flush_cache_all();
	srmmu_set_ctable_ptr((unsigned long) srmmu_ctx_table_phys);
	flush_tlb_all();
	poke_srmmu();

	start_mem = sparc_context_init(start_mem, num_contexts);
	start_mem = free_area_init(start_mem, end_mem);

	return PAGE_ALIGN(start_mem);
}

static char srmmuinfo[512];

static char *srmmu_mmu_info(void)
{
	sprintf(srmmuinfo, "MMU type\t: %s\n"
		"invall\t\t: %d\n"
		"invmm\t\t: %d\n"
		"invrnge\t\t: %d\n"
		"invpg\t\t: %d\n"
		"contexts\t: %d\n"
		"big_chunks\t: %d\n"
		"little_chunks\t: %d\n",
		srmmu_name,
		module_stats.invall,
		module_stats.invmm,
		module_stats.invrnge,
		module_stats.invpg,
		num_contexts,
#if 0
		num_big_chunks,
		num_little_chunks
#else
		0, 0
#endif
		);
	return srmmuinfo;
}

static void srmmu_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
}

static void srmmu_exit_hook(void)
{
	struct ctx_list *ctx_old;
	struct mm_struct *mm = current->mm;

	if(mm->context != NO_CONTEXT) {
		flush_cache_mm(mm);
		ctxd_set(&srmmu_context_table[mm->context], swapper_pg_dir);
		flush_tlb_mm(mm);
		ctx_old = ctx_list_pool + mm->context;
		remove_from_ctx_list(ctx_old);
		add_to_free_ctxlist(ctx_old);
		mm->context = NO_CONTEXT;
	}
}

static void srmmu_flush_hook(void)
{
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		alloc_context(current->mm);
		ctxd_set(&srmmu_context_table[current->mm->context], current->mm->pgd);
		srmmu_set_context(current->mm->context);
	}
}

static void hypersparc_exit_hook(void)
{
	struct ctx_list *ctx_old;
	struct mm_struct *mm = current->mm;

	if(mm->context != NO_CONTEXT) {
		/* HyperSparc is copy-back, any data for this
		 * process in a modified cache line is stale
		 * and must be written back to main memory now
		 * else we eat shit later big time.
		 */
		flush_cache_mm(mm);
		ctxd_set(&srmmu_context_table[mm->context], swapper_pg_dir);
		flush_tlb_mm(mm);
		ctx_old = ctx_list_pool + mm->context;
		remove_from_ctx_list(ctx_old);
		add_to_free_ctxlist(ctx_old);
		mm->context = NO_CONTEXT;
	}
}

static void hypersparc_flush_hook(void)
{
	if(current->tss.flags & SPARC_FLAG_KTHREAD) {
		alloc_context(current->mm);
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[current->mm->context], current->mm->pgd);
		srmmu_set_context(current->mm->context);
	}
}

/* Init various srmmu chip types. */
void srmmu_is_bad(void)
{
	prom_printf("Could not determine SRMMU chip type.\n");
	prom_halt();
}

void poke_hypersparc(void)
{
	volatile unsigned long clear;
	unsigned long mreg = srmmu_get_mmureg();

	hyper_flush_unconditional_combined();

	mreg &= ~(HYPERSPARC_CWENABLE);
	mreg |= (HYPERSPARC_CENABLE | HYPERSPARC_WBENABLE);
	mreg |= (HYPERSPARC_CMODE);

	srmmu_set_mmureg(mreg);
	hyper_clear_all_tags();

	put_ross_icr(HYPERSPARC_ICCR_FTD | HYPERSPARC_ICCR_ICE);
	hyper_flush_whole_icache();
	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();
}

void init_hypersparc(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	srmmu_name = "ROSS HyperSparc";
	can_cache_ptables = 0;
	if(mreg & HYPERSPARC_CSIZE) {
		hyper_cache_size = (256 * 1024);
		hyper_line_size = 64;
	} else {
		hyper_cache_size = (128 * 1024);
		hyper_line_size = 32;
	}

	flush_cache_all = hypersparc_flush_cache_all;
	flush_cache_mm = hypersparc_flush_cache_mm;
	flush_cache_range = hypersparc_flush_cache_range;
	flush_cache_page = hypersparc_flush_cache_page;

	flush_tlb_all = hypersparc_flush_tlb_all;
	flush_tlb_mm = hypersparc_flush_tlb_mm;
	flush_tlb_range = hypersparc_flush_tlb_range;
	flush_tlb_page = hypersparc_flush_tlb_page;

	flush_page_to_ram = hypersparc_flush_page_to_ram;
	flush_page_for_dma = hypersparc_flush_page_for_dma;
	flush_cache_page_to_uncache = hypersparc_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = hypersparc_flush_tlb_page_for_cbit;

	ctxd_set = hypersparc_ctxd_set;
	switch_to_context = hypersparc_switch_to_context;
	mmu_exit_hook = hypersparc_exit_hook;
	mmu_flush_hook = hypersparc_flush_hook;
	sparc_update_rootmmu_dir = hypersparc_update_rootmmu_dir;
	set_pte = hypersparc_set_pte;
	poke_srmmu = poke_hypersparc;
}

void poke_cypress(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	mreg &= ~CYPRESS_CMODE;
	mreg |= CYPRESS_CENABLE;
	srmmu_set_mmureg(mreg);
}

void init_cypress_common(void)
{
	can_cache_ptables = 0;
	flush_tlb_all = cypress_flush_tlb_all;
	flush_tlb_mm = cypress_flush_tlb_mm;
	flush_tlb_page = cypress_flush_tlb_page;
	flush_tlb_range = cypress_flush_tlb_range;
	poke_srmmu = poke_cypress;

	/* XXX Need to write cache flushes for this one... XXX */

}

void init_cypress_604(void)
{
	srmmu_name = "ROSS Cypress-604(UP)";
	srmmu_modtype = Cypress;
	init_cypress_common();
}

void init_cypress_605(unsigned long mrev)
{
	srmmu_name = "ROSS Cypress-605(MP)";
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

void poke_swift(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Clear any crap from the cache or else... */
	swift_idflash_clear();
	mreg |= (SWIFT_IE | SWIFT_DE); /* I & D caches on */

	/* The Swift branch folding logic is completely broken.  At
	 * trap time, if things are just right, if can mistakenly
	 * think that a trap is coming from kernel mode when in fact
	 * it is coming from user mode (it mis-executes the branch in
	 * the trap code).  So you see things like crashme completely
	 * hosing your machine which is completely unacceptable.  Turn
	 * this shit off... nice job Fujitsu.
	 */
	mreg &= ~(SWIFT_BF);
	srmmu_set_mmureg(mreg);
}

#define SWIFT_MASKID_ADDR  0x10003018
void init_swift(void)
{
	unsigned long swift_rev;

	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     "srl %0, 0x18, %0\n\t" :
			     "=r" (swift_rev) :
			     "r" (SWIFT_MASKID_ADDR), "i" (ASI_M_BYPASS));
	srmmu_name = "Fujitsu Swift";
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

	flush_cache_all = swift_flush_cache_all;
	flush_cache_mm = swift_flush_cache_mm;
	flush_cache_page = swift_flush_cache_page;
	flush_cache_range = swift_flush_cache_range;

	flush_tlb_all = swift_flush_tlb_all;
	flush_tlb_mm = swift_flush_tlb_mm;
	flush_tlb_page = swift_flush_tlb_page;
	flush_tlb_range = swift_flush_tlb_range;

	flush_page_to_ram = swift_flush_page_to_ram;
	flush_page_for_dma = swift_flush_page_for_dma;
	flush_cache_page_to_uncache = swift_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = swift_flush_tlb_page_for_cbit;

	/* Are you now convinced that the Swift is one of the
	 * biggest VLSI abortions of all time?  Bravo Fujitsu!
	 * Fujitsu, the !#?!%$'d up processor people.  I bet if
	 * you examined the microcode of the Swift you'd find
	 * XXX's all over the place.
	 */
	poke_srmmu = poke_swift;
}

void poke_tsunami(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	tsunami_flush_icache();
	tsunami_flush_dcache();
	mreg &= ~TSUNAMI_ITD;
	mreg |= (TSUNAMI_IENAB | TSUNAMI_DENAB);
	srmmu_set_mmureg(mreg);
}

void init_tsunami(void)
{
	/* Tsunami's pretty sane, Sun and TI actually got it
	 * somewhat right this time.  Fujitsu should have
	 * taken some lessons from them.
	 */

	srmmu_name = "TI Tsunami";
	srmmu_modtype = Tsunami;
	can_cache_ptables = 1;

	flush_cache_all = tsunami_flush_cache_all;
	flush_cache_mm = tsunami_flush_cache_mm;
	flush_cache_page = tsunami_flush_cache_page;
	flush_cache_range = tsunami_flush_cache_range;

	flush_tlb_all = tsunami_flush_tlb_all;
	flush_tlb_mm = tsunami_flush_tlb_mm;
	flush_tlb_page = tsunami_flush_tlb_page;
	flush_tlb_range = tsunami_flush_tlb_range;

	flush_page_to_ram = tsunami_flush_page_to_ram;
	flush_page_for_dma = tsunami_flush_page_for_dma;
	flush_cache_page_to_uncache = tsunami_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = tsunami_flush_tlb_page_for_cbit;

	poke_srmmu = poke_tsunami;
}

void poke_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	static int smp_catch = 0;

	if(viking_mxcc_present) {
		unsigned long mxcc_control;

		__asm__ __volatile__("set -1, %%g2\n\t"
				     "set -1, %%g3\n\t"
				     "stda %%g2, [%1] %2\n\t"
				     "lda [%3] %2, %0\n\t" :
				     "=r" (mxcc_control) :
				     "r" (MXCC_EREG), "i" (ASI_M_MXCC),
				     "r" (MXCC_CREG) : "g2", "g3");
		mxcc_control |= (MXCC_CTL_ECE | MXCC_CTL_PRE | MXCC_CTL_MCE);
		mxcc_control &= ~(MXCC_CTL_PARE | MXCC_CTL_RRC);
		mreg &= ~(VIKING_PCENABLE);
		__asm__ __volatile__("sta %0, [%1] %2\n\t" : :
				     "r" (mxcc_control), "r" (MXCC_CREG),
				     "i" (ASI_M_MXCC));
		srmmu_set_mmureg(mreg);
		mreg |= VIKING_TCENABLE;
	} else {
		unsigned long bpreg;

		mreg &= ~(VIKING_TCENABLE);
		if(smp_catch++) {
			/* Must disable mixed-cmd mode here for
			 * other cpu's.
			 */
			bpreg = viking_get_bpreg();
			bpreg &= ~(VIKING_ACTION_MIX);
			viking_set_bpreg(bpreg);

			/* Just in case PROM does something funny. */
			msi_set_sync();
		}
	}

	viking_unlock_icache();
	viking_flush_icache();
#if 0
	viking_unlock_dcache();
	viking_flush_dcache();
#endif
	mreg |= VIKING_SPENABLE;
	mreg |= (VIKING_ICENABLE | VIKING_DCENABLE);
	mreg |= VIKING_SBENABLE;
	mreg &= ~(VIKING_ACENABLE);
#if CONFIG_AP1000
        mreg &= ~(VIKING_SBENABLE);
#endif
#ifdef __SMP__
	mreg &= ~(VIKING_SBENABLE);
#endif
	srmmu_set_mmureg(mreg);
}

void init_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Ahhh, the viking.  SRMMU VLSI abortion number two... */

	if(mreg & VIKING_MMODE) {
		unsigned long bpreg;

		srmmu_name = "TI Viking";
		viking_mxcc_present = 0;
		can_cache_ptables = 0;

		bpreg = viking_get_bpreg();
		bpreg &= ~(VIKING_ACTION_MIX);
		viking_set_bpreg(bpreg);

		msi_set_sync();

		flush_cache_page_to_uncache = viking_no_mxcc_flush_page;
	} else {
		srmmu_name = "TI Viking/MXCC";
		viking_mxcc_present = 1;
		can_cache_ptables = 1;
		flush_cache_page_to_uncache = viking_mxcc_flush_page;
	}

	flush_cache_all = viking_flush_cache_all;
	flush_cache_mm = viking_flush_cache_mm;
	flush_cache_page = viking_flush_cache_page;
	flush_cache_range = viking_flush_cache_range;

	flush_tlb_all = viking_flush_tlb_all;
	flush_tlb_mm = viking_flush_tlb_mm;
	flush_tlb_page = viking_flush_tlb_page;
	flush_tlb_range = viking_flush_tlb_range;

	flush_page_to_ram = viking_flush_page_to_ram;
	flush_page_for_dma = viking_flush_page_for_dma;
	flush_tlb_page_for_cbit = viking_flush_tlb_page_for_cbit;

	poke_srmmu = poke_viking;
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
		init_viking();
		return;
	}

	/* Finally the Tsunami. */
	if(psr_typ == 4 && psr_vers == 1 && (mod_typ || mod_rev)) {
		init_tsunami();
		return;
	}

	/* Oh well */
	srmmu_is_bad();
}

extern unsigned long spwin_mmu_patchme, fwin_mmu_patchme,
	tsetup_mmu_patchme, rtrap_mmu_patchme;

extern unsigned long spwin_srmmu_stackchk, srmmu_fwin_stackchk,
	tsetup_srmmu_stackchk, srmmu_rett_stackchk;

#ifdef __SMP__
extern unsigned long rirq_mmu_patchme, srmmu_reti_stackchk;
#endif

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
#ifdef __SMP__
	PATCH_BRANCH(rirq_mmu_patchme, srmmu_reti_stackchk);
#endif
	PATCH_BRANCH(sparc_ttable[SP_TRAP_TFLT].inst_three, srmmu_fault);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_DFLT].inst_three, srmmu_fault);
	PATCH_BRANCH(sparc_ttable[SP_TRAP_DACC].inst_three, srmmu_fault);
}

#ifdef __SMP__
/* Local cross-calls. */
static void smp_flush_page_for_dma(unsigned long page)
{
	xc1((smpfunc_t) local_flush_page_for_dma, page);
}

static void smp_flush_cache_page_to_uncache(unsigned long page)
{
	xc1((smpfunc_t) local_flush_cache_page_to_uncache, page);
}

static void smp_flush_tlb_page_for_cbit(unsigned long page)
{
	xc1((smpfunc_t) local_flush_tlb_page_for_cbit, page);
}
#endif

/* Load up routines and constants for sun4m mmu */
void ld_mmu_srmmu(void)
{
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
	pte_clear = srmmu_pte_clear;

	pmd_none = srmmu_pmd_none;
	pmd_bad = srmmu_pmd_bad;
	pmd_present = srmmu_pmd_present;
	pmd_clear = srmmu_pmd_clear;

	pgd_none = srmmu_pgd_none;
	pgd_bad = srmmu_pgd_bad;
	pgd_present = srmmu_pgd_present;
	pgd_clear = srmmu_pgd_clear;

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

	mmu_get_scsi_one = srmmu_get_scsi_one;
	mmu_get_scsi_sgl = srmmu_get_scsi_sgl;
	mmu_release_scsi_one = srmmu_release_scsi_one;
	mmu_release_scsi_sgl = srmmu_release_scsi_sgl;

	mmu_info = srmmu_mmu_info;
        mmu_v2p = srmmu_v2p;
        mmu_p2v = srmmu_p2v;

	/* Task struct and kernel stack allocating/freeing. */
	alloc_kernel_stack = srmmu_alloc_kernel_stack;
	alloc_task_struct = srmmu_alloc_task_struct;
	free_kernel_stack = srmmu_free_kernel_stack;
	free_task_struct = srmmu_free_task_struct;

	quick_kernel_fault = srmmu_quick_kernel_fault;

	/* SRMMU specific. */
	ctxd_set = srmmu_ctxd_set;
	pmd_set = srmmu_pmd_set;

	get_srmmu_type();
	patch_window_trap_handlers();

#ifdef __SMP__
	/* El switcheroo... */

	local_flush_cache_all = flush_cache_all;
	local_flush_cache_mm = flush_cache_mm;
	local_flush_cache_range = flush_cache_range;
	local_flush_cache_page = flush_cache_page;
	local_flush_tlb_all = flush_tlb_all;
	local_flush_tlb_mm = flush_tlb_mm;
	local_flush_tlb_range = flush_tlb_range;
	local_flush_tlb_page = flush_tlb_page;
	local_flush_page_to_ram = flush_page_to_ram;
	local_flush_page_for_dma = flush_page_for_dma;
	local_flush_cache_page_to_uncache = flush_cache_page_to_uncache;
	local_flush_tlb_page_for_cbit = flush_tlb_page_for_cbit;

	flush_cache_all = smp_flush_cache_all;
	flush_cache_mm = smp_flush_cache_mm;
	flush_cache_range = smp_flush_cache_range;
	flush_cache_page = smp_flush_cache_page;
	flush_tlb_all = smp_flush_tlb_all;
	flush_tlb_mm = smp_flush_tlb_mm;
	flush_tlb_range = smp_flush_tlb_range;
	flush_tlb_page = smp_flush_tlb_page;
	flush_page_to_ram = smp_flush_page_to_ram;
	flush_page_for_dma = smp_flush_page_for_dma;
	flush_cache_page_to_uncache = smp_flush_cache_page_to_uncache;
	flush_tlb_page_for_cbit = smp_flush_tlb_page_for_cbit;
#endif
}
