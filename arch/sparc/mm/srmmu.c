/* $Id: srmmu.c,v 1.207 2000/02/14 02:51:53 davem Exp $
 * srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Pete Zaitcev
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1999 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
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
#include <asm/asi.h>
#include <asm/msi.h>
#include <asm/a.out.h>
#include <asm/mmu_context.h>
#include <asm/io-unit.h>

/* Now the cpu specific definitions. */
#include <asm/viking.h>
#include <asm/mxcc.h>
#include <asm/ross.h>
#include <asm/tsunami.h>
#include <asm/swift.h>
#include <asm/turbosparc.h>

#include <asm/btfixup.h>

enum mbus_module srmmu_modtype;
unsigned int hwbug_bitmask;
int vac_cache_size;
int vac_line_size;
int vac_badbits;

extern struct resource sparc_iomap;

extern unsigned long last_valid_pfn;

#ifdef __SMP__
#define FLUSH_BEGIN(mm)
#define FLUSH_END
#else
#define FLUSH_BEGIN(mm) if((mm)->context != NO_CONTEXT) {
#define FLUSH_END	}
#endif

BTFIXUPDEF_CALL(void, ctxd_set, ctxd_t *, pgd_t *)
BTFIXUPDEF_CALL(void, pmd_set, pmd_t *, pte_t *)

#define ctxd_set(ctxp,pgdp) BTFIXUP_CALL(ctxd_set)(ctxp,pgdp)
#define pmd_set(pmdp,ptep) BTFIXUP_CALL(pmd_set)(pmdp,ptep)

BTFIXUPDEF_CALL(void, flush_page_for_dma, unsigned long)
BTFIXUPDEF_CALL(void, flush_chunk, unsigned long)

#define flush_page_for_dma(page) BTFIXUP_CALL(flush_page_for_dma)(page)
int flush_page_for_dma_global = 1;
#define flush_chunk(chunk) BTFIXUP_CALL(flush_chunk)(chunk)
#ifdef __SMP__
BTFIXUPDEF_CALL(void, local_flush_page_for_dma, unsigned long)

#define local_flush_page_for_dma(page) BTFIXUP_CALL(local_flush_page_for_dma)(page)
#endif

static struct srmmu_stats {
	int invall;
	int invpg;
	int invrnge;
	int invmm;
} module_stats;

char *srmmu_name;

ctxd_t *srmmu_ctx_table_phys;
ctxd_t *srmmu_context_table;

int viking_mxcc_present = 0;
static spinlock_t srmmu_context_spinlock = SPIN_LOCK_UNLOCKED;

/* In general all page table modifications should use the V8 atomic
 * swap instruction.  This insures the mmu and the cpu are in sync
 * with respect to ref/mod bits in the page tables.
 */
static inline unsigned long srmmu_swap(unsigned long *addr, unsigned long value)
{
	__asm__ __volatile__("swap [%2], %0" : "=&r" (value) : "0" (value), "r" (addr));
	return value;
}

/* Functions really use this, not srmmu_swap directly. */
#define srmmu_set_entry(ptr, newentry) srmmu_swap((unsigned long *) (ptr), (newentry))

/* The very generic SRMMU page table operations. */
static inline int srmmu_device_memory(unsigned long x)
{
	return ((x & 0xF0000000) != 0);
}

static unsigned long srmmu_pgd_page(pgd_t pgd)
{ return srmmu_device_memory(pgd_val(pgd))?~0:(unsigned long)__va((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pmd_page(pmd_t pmd)
{ return srmmu_device_memory(pmd_val(pmd))?~0:(unsigned long)__va((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pte_pagenr(pte_t pte)
{ return srmmu_device_memory(pte_val(pte))?~0:(((pte_val(pte) & SRMMU_PTE_PMASK) << 4) >> PAGE_SHIFT); }

static inline int srmmu_pte_none(pte_t pte)
{ return !(pte_val(pte) & 0xFFFFFFF); }
static inline int srmmu_pte_present(pte_t pte)
{ return ((pte_val(pte) & SRMMU_ET_MASK) == SRMMU_ET_PTE); }

static inline void srmmu_pte_clear(pte_t *ptep)      { set_pte(ptep, __pte(0)); }

static inline int srmmu_pmd_none(pmd_t pmd)
{ return !(pmd_val(pmd) & 0xFFFFFFF); }
static inline int srmmu_pmd_bad(pmd_t pmd)
{ return (pmd_val(pmd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static inline int srmmu_pmd_present(pmd_t pmd)
{ return ((pmd_val(pmd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static inline void srmmu_pmd_clear(pmd_t *pmdp)      { set_pte((pte_t *)pmdp, __pte(0)); }

static inline int srmmu_pgd_none(pgd_t pgd)          
{ return !(pgd_val(pgd) & 0xFFFFFFF); }

static inline int srmmu_pgd_bad(pgd_t pgd)
{ return (pgd_val(pgd) & SRMMU_ET_MASK) != SRMMU_ET_PTD; }

static inline int srmmu_pgd_present(pgd_t pgd)
{ return ((pgd_val(pgd) & SRMMU_ET_MASK) == SRMMU_ET_PTD); }

static inline void srmmu_pgd_clear(pgd_t * pgdp)     { set_pte((pte_t *)pgdp, __pte(0)); }

static inline int srmmu_pte_write(pte_t pte)         { return pte_val(pte) & SRMMU_WRITE; }
static inline int srmmu_pte_dirty(pte_t pte)         { return pte_val(pte) & SRMMU_DIRTY; }
static inline int srmmu_pte_young(pte_t pte)         { return pte_val(pte) & SRMMU_REF; }

static inline pte_t srmmu_pte_wrprotect(pte_t pte)   { return __pte(pte_val(pte) & ~SRMMU_WRITE);}
static inline pte_t srmmu_pte_mkclean(pte_t pte)     { return __pte(pte_val(pte) & ~SRMMU_DIRTY);}
static inline pte_t srmmu_pte_mkold(pte_t pte)       { return __pte(pte_val(pte) & ~SRMMU_REF);}
static inline pte_t srmmu_pte_mkwrite(pte_t pte)     { return __pte(pte_val(pte) | SRMMU_WRITE);}
static inline pte_t srmmu_pte_mkdirty(pte_t pte)     { return __pte(pte_val(pte) | SRMMU_DIRTY);}
static inline pte_t srmmu_pte_mkyoung(pte_t pte)     { return __pte(pte_val(pte) | SRMMU_REF);}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static pte_t srmmu_mk_pte(struct page *page, pgprot_t pgprot)
{ return __pte((((page - mem_map) << PAGE_SHIFT) >> 4) | pgprot_val(pgprot)); }

static pte_t srmmu_mk_pte_phys(unsigned long page, pgprot_t pgprot)
{ return __pte(((page) >> 4) | pgprot_val(pgprot)); }

static pte_t srmmu_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{
	return __pte(((page) >> 4) | (space << 28) | pgprot_val(pgprot));
}

static void srmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ 
	set_pte((pte_t *)ctxp, (SRMMU_ET_PTD | (__pa((unsigned long) pgdp) >> 4)));
}

static void srmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (__pa((unsigned long) pmdp) >> 4)));
}

static void srmmu_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	set_pte((pte_t *)pmdp, (SRMMU_ET_PTD | (__pa((unsigned long) ptep) >> 4)));
}

static inline pte_t srmmu_pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & SRMMU_CHG_MASK) | pgprot_val(newprot));
}

/* to find an entry in a top-level page table... */
static inline pgd_t *srmmu_pgd_offset(struct mm_struct * mm, unsigned long address)
{
	return mm->pgd + (address >> SRMMU_PGDIR_SHIFT);
}

/* Find an entry in the second-level page table.. */
static inline pmd_t *srmmu_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) srmmu_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
static inline pte_t *srmmu_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) srmmu_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

static inline pte_t *srmmu_get_pte_fast(void)
{
	struct page *ret;
	
	spin_lock(&pte_spinlock);
	if ((ret = (struct page *)pte_quicklist) != NULL) {
		unsigned int mask = (unsigned int)ret->pprev_hash;
		unsigned int tmp, off;
		
		if (mask & 0xff)
			for (tmp = 0x001, off = 0; (mask & tmp) == 0; tmp <<= 1, off += 256);
		else
			for (tmp = 0x100, off = 2048; (mask & tmp) == 0; tmp <<= 1, off += 256);
		(unsigned int)ret->pprev_hash = mask & ~tmp;
		if (!(mask & ~tmp))
			pte_quicklist = (unsigned long *)ret->next_hash;
		ret = (struct page *)(page_address(ret) + off);
		pgtable_cache_size--;
	}
	spin_unlock(&pte_spinlock);
	return (pte_t *)ret;
}

static inline pte_t *srmmu_get_pte_slow(void)
{
	pte_t *ret;
	struct page *page;
	
	ret = (pte_t *)get_free_page(GFP_KERNEL);
	if (ret) {
		page = mem_map + MAP_NR(ret);
		flush_chunk((unsigned long)ret);
		(unsigned int)page->pprev_hash = 0xfffe;
		spin_lock(&pte_spinlock);
		(unsigned long *)page->next_hash = pte_quicklist;
		pte_quicklist = (unsigned long *)page;
		pgtable_cache_size += 15;
	}
	return ret;
}

static inline pgd_t *srmmu_get_pgd_fast(void)
{
	struct page *ret;

	spin_lock(&pgd_spinlock);	
	if ((ret = (struct page *)pgd_quicklist) != NULL) {
		unsigned int mask = (unsigned int)ret->pprev_hash;
		unsigned int tmp, off;
		
		for (tmp = 0x001, off = 0; (mask & tmp) == 0; tmp <<= 1, off += 1024);
		(unsigned int)ret->pprev_hash = mask & ~tmp;
		if (!(mask & ~tmp))
			pgd_quicklist = (unsigned long *)ret->next_hash;
		ret = (struct page *)(page_address(ret) + off);
		pgd_cache_size--;
	}
	spin_unlock(&pgd_spinlock);
	return (pgd_t *)ret;
}

static inline pgd_t *srmmu_get_pgd_slow(void)
{
	pgd_t *ret;
	struct page *page;
	
	ret = (pgd_t *)__get_free_page(GFP_KERNEL);
	if (ret) {
		pgd_t *init = pgd_offset(&init_mm, 0);
		memset(ret + (0 * PTRS_PER_PGD), 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(ret + (0 * PTRS_PER_PGD) + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
						(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
		memset(ret + (1 * PTRS_PER_PGD), 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(ret + (1 * PTRS_PER_PGD) + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
						(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
		memset(ret + (2 * PTRS_PER_PGD), 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(ret + (2 * PTRS_PER_PGD) + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
						(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
		memset(ret + (3 * PTRS_PER_PGD), 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(ret + (3 * PTRS_PER_PGD) + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
						(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
		page = mem_map + MAP_NR(ret);
		flush_chunk((unsigned long)ret);
		(unsigned int)page->pprev_hash = 0xe;
		spin_lock(&pgd_spinlock);
		(unsigned long *)page->next_hash = pgd_quicklist;
		pgd_quicklist = (unsigned long *)page;
		pgd_cache_size += 3;
		spin_unlock(&pgd_spinlock);
	}
	return ret;
}

static void srmmu_free_pte_slow(pte_t *pte)
{
}

static void srmmu_free_pgd_slow(pgd_t *pgd)
{
}

static inline void srmmu_pte_free(pte_t *pte)
{
	struct page *page = mem_map + MAP_NR(pte);

	spin_lock(&pte_spinlock);	
	if (!page->pprev_hash) {
		(unsigned long *)page->next_hash = pte_quicklist;
		pte_quicklist = (unsigned long *)page;
	}
	(unsigned int)page->pprev_hash |= (1 << ((((unsigned long)pte) >> 8) & 15));
	pgtable_cache_size++;
	spin_unlock(&pte_spinlock);
}

static pte_t *srmmu_pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1);
	if(srmmu_pmd_none(*pmd)) {
		pte_t *page = srmmu_get_pte_fast();
		
		if (page) {
			pmd_set(pmd, page);
			return page + address;
		}
		page = srmmu_get_pte_slow();
		if(srmmu_pmd_none(*pmd)) {
			if(page) {
				spin_unlock(&pte_spinlock);
				pmd_set(pmd, page);
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		if (page) {
			(unsigned int)(((struct page *)pte_quicklist)->pprev_hash) = 0xffff;
			pgtable_cache_size++;
			spin_unlock(&pte_spinlock);
		}
	}
	if(srmmu_pmd_bad(*pmd)) {
		printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return ((pte_t *) pmd_page(*pmd)) + address;
}

/* Real three-level page tables on SRMMU. */
static void srmmu_pmd_free(pmd_t * pmd)
{
	return srmmu_pte_free((pte_t *)pmd);
}

static pmd_t *srmmu_pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1);
	if(srmmu_pgd_none(*pgd)) {
		pmd_t *page = (pmd_t *)srmmu_get_pte_fast();
		
		if (page) {
			pgd_set(pgd, page);
			return page + address;
		}
		page = (pmd_t *)srmmu_get_pte_slow();
		if(srmmu_pgd_none(*pgd)) {
			if(page) {
				spin_unlock(&pte_spinlock);
				pgd_set(pgd, page);
				return page + address;
			}
			pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
			return NULL;
		}
		if (page) {
			(unsigned int)(((struct page *)pte_quicklist)->pprev_hash) = 0xffff;
			pgtable_cache_size++;
			spin_unlock(&pte_spinlock);
		}
	}
	if(srmmu_pgd_bad(*pgd)) {
		printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
		pgd_set(pgd, (pmd_t *) BAD_PAGETABLE);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

static void srmmu_pgd_free(pgd_t *pgd)
{
	struct page *page = mem_map + MAP_NR(pgd);

	spin_lock(&pgd_spinlock);
	if (!page->pprev_hash) {
		(unsigned long *)page->next_hash = pgd_quicklist;
		pgd_quicklist = (unsigned long *)page;
	}
	(unsigned int)page->pprev_hash |= (1 << ((((unsigned long)pgd) >> 10) & 3));
	pgd_cache_size++;
	spin_unlock(&pgd_spinlock);
}

static pgd_t *srmmu_pgd_alloc(void)
{
	pgd_t *ret;
	
	ret = srmmu_get_pgd_fast();
	if (ret) return ret;
	return srmmu_get_pgd_slow();
}


static void srmmu_set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;
	struct page *page;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm,address) = entry;
	}
	read_unlock(&tasklist_lock);
	spin_lock(&pgd_spinlock);
	address >>= SRMMU_PGDIR_SHIFT;
	for (page = (struct page *)pgd_quicklist; page; page = page->next_hash) {
		pgd_t *pgd = (pgd_t *)page_address(page);
		unsigned int mask = (unsigned int)page->pprev_hash;
		
		if (mask & 1)
			pgd[address + 0 * SRMMU_PTRS_PER_PGD] = entry;
		if (mask & 2)
			pgd[address + 1 * SRMMU_PTRS_PER_PGD] = entry;
		if (mask & 4)
			pgd[address + 2 * SRMMU_PTRS_PER_PGD] = entry;
		if (mask & 8)
			pgd[address + 3 * SRMMU_PTRS_PER_PGD] = entry;
		if (mask)
			flush_chunk((unsigned long)pgd);
	}
	spin_unlock(&pgd_spinlock);
}

static void srmmu_set_pte_cacheable(pte_t *ptep, pte_t pteval)
{
	srmmu_set_entry(ptep, pte_val(pteval));
}

static void srmmu_set_pte_nocache_cypress(pte_t *ptep, pte_t pteval)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long line, page;

	srmmu_set_entry(ptep, pte_val(pteval));
	page = ((unsigned long)ptep) & PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
}

static void srmmu_set_pte_nocache_viking(pte_t *ptep, pte_t pteval)
{
	unsigned long vaddr;
	int set;
	int i;

	set = ((unsigned long)ptep >> 5) & 0x7f;
	vaddr = (KERNBASE + PAGE_SIZE) | (set << 5);
	srmmu_set_entry(ptep, pte_val(pteval));
	for (i = 0; i < 8; i++) {
		__asm__ __volatile__ ("ld [%0], %%g0" : : "r" (vaddr));
		vaddr += PAGE_SIZE;
	}
}

static void srmmu_quick_kernel_fault(unsigned long address)
{
#ifdef __SMP__
	printk("CPU[%d]: Kernel faults at addr=0x%08lx\n",
	       smp_processor_id(), address);
	while (1) ;
#else
	printk("Kernel faults at addr=0x%08lx\n", address);
	printk("PTE=%08lx\n", srmmu_hwprobe((address & PAGE_MASK)));
	die_if_kernel("SRMMU bolixed...", current->thread.kregs);
#endif
}

static inline void alloc_context(struct mm_struct *old_mm, struct mm_struct *mm)
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
	if(ctxp->ctx_mm == old_mm)
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

	ctx_old = ctx_list_pool + context;
	remove_from_ctx_list(ctx_old);
	add_to_free_ctxlist(ctx_old);
}


static void srmmu_switch_mm(struct mm_struct *old_mm, struct mm_struct *mm,
    struct task_struct *tsk, int cpu)
{
	if(mm->context == NO_CONTEXT) {
		spin_lock(&srmmu_context_spinlock);
		alloc_context(old_mm, mm);
		spin_unlock(&srmmu_context_spinlock);
		ctxd_set(&srmmu_context_table[mm->context], mm->pgd);
	}
	srmmu_set_context(mm->context);
}

/* Low level IO area allocation on the SRMMU. */
void srmmu_mapioaddr(unsigned long physaddr, unsigned long virt_addr, int bus_type, int rdonly)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long tmp;

	physaddr &= PAGE_MASK;
	pgdp = srmmu_pgd_offset(&init_mm, virt_addr);
	pmdp = pmd_offset(pgdp, virt_addr);
	ptep = pte_offset(pmdp, virt_addr);
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
	__flush_page_to_ram(virt_addr);
	set_pte(ptep, __pte(tmp));
	flush_tlb_all();
}

void srmmu_unmapioaddr(unsigned long virt_addr)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = srmmu_pgd_offset(&init_mm, virt_addr);
	pmdp = pmd_offset(pgdp, virt_addr);
	ptep = pte_offset(pmdp, virt_addr);

	/* No need to flush uncacheable page. */
	pte_clear(ptep);
	flush_tlb_all();
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
	return (struct task_struct *) __get_free_pages(GFP_KERNEL, 1);
}

static void srmmu_free_task_struct(struct task_struct *tsk)
{
	free_pages((unsigned long)tsk, 1);
}

static void srmmu_get_task_struct(struct task_struct *tsk)
{
	atomic_inc(&mem_map[MAP_NR(tsk)].count);
}

/* tsunami.S */
extern void tsunami_flush_cache_all(void);
extern void tsunami_flush_cache_mm(struct mm_struct *mm);
extern void tsunami_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end);
extern void tsunami_flush_cache_page(struct vm_area_struct *vma, unsigned long page);
extern void tsunami_flush_page_to_ram(unsigned long page);
extern void tsunami_flush_page_for_dma(unsigned long page);
extern void tsunami_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);
extern void tsunami_flush_chunk(unsigned long chunk);
extern void tsunami_flush_tlb_all(void);
extern void tsunami_flush_tlb_mm(struct mm_struct *mm);
extern void tsunami_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end);
extern void tsunami_flush_tlb_page(struct vm_area_struct *vma, unsigned long page);
extern void tsunami_setup_blockops(void);

/* Workaround, until we find what's going on with Swift. When low on memory,
 * it sometimes loops in fault/handle_mm_fault incl. flush_tlb_page to find
 * out it is already in page tables/ fault again on the same instruction.
 * I really don't understand it, have checked it and contexts
 * are right, flush_tlb_all is done as well, and it faults again...
 * Strange. -jj
 *
 * The following code is a deadwood that may be necessary when
 * we start to make precise page flushes again. --zaitcev
 */
static void swift_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
#if 0
	static unsigned long last;
	unsigned int val;
	/* unsigned int n; */

	if (address == last) {
		val = srmmu_hwprobe(address);
		if (val != 0 && pte_val(pte) != val) {
			printk("swift_update_mmu_cache: "
			    "addr %lx put %08x probed %08x from %p\n",
			    address, pte_val(pte), val,
			    __builtin_return_address(0));
			srmmu_flush_whole_tlb();
		}
	}
	last = address;
#endif
}

/* swift.S */
extern void swift_flush_cache_all(void);
extern void swift_flush_cache_mm(struct mm_struct *mm);
extern void swift_flush_cache_range(struct mm_struct *mm,
				    unsigned long start, unsigned long end);
extern void swift_flush_cache_page(struct vm_area_struct *vma, unsigned long page);
extern void swift_flush_page_to_ram(unsigned long page);
extern void swift_flush_page_for_dma(unsigned long page);
extern void swift_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);
extern void swift_flush_chunk(unsigned long chunk);
extern void swift_flush_tlb_all(void);
extern void swift_flush_tlb_mm(struct mm_struct *mm);
extern void swift_flush_tlb_range(struct mm_struct *mm,
				  unsigned long start, unsigned long end);
extern void swift_flush_tlb_page(struct vm_area_struct *vma, unsigned long page);

#if 0  /* P3: deadwood to debug precise flushes on Swift. */
void swift_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int cctx, ctx1;

	page &= PAGE_MASK;
	if ((ctx1 = vma->vm_mm->context) != -1) {
		cctx = srmmu_get_context();
/* Is context # ever different from current context? P3 */
		if (cctx != ctx1) {
			printk("flush ctx %02x curr %02x\n", ctx1, cctx);
			srmmu_set_context(ctx1);
			swift_flush_page(page);
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
					"r" (page), "i" (ASI_M_FLUSH_PROBE));
			srmmu_set_context(cctx);
		} else {
			 /* Rm. prot. bits from virt. c. */
			/* swift_flush_cache_all(); */
			/* swift_flush_cache_page(vma, page); */
			swift_flush_page(page);

			__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
				"r" (page), "i" (ASI_M_FLUSH_PROBE));
			/* same as above: srmmu_flush_tlb_page() */
		}
	}
	module_stats.invpg++;
}
#endif

/* The following are all MBUS based SRMMU modules, and therefore could
 * be found in a multiprocessor configuration.  On the whole, these
 * chips seems to be much more touchy about DVMA and page tables
 * with respect to cache coherency.
 */

/* Cypress flushes. */
static void cypress_flush_cache_all(void)
{
	volatile unsigned long cypress_sucks;
	unsigned long faddr, tagval;

	flush_user_windows();
	for(faddr = 0; faddr < 0x10000; faddr += 0x20) {
		__asm__ __volatile__("lda [%1 + %2] %3, %0\n\t" :
				     "=r" (tagval) :
				     "r" (faddr), "r" (0x40000),
				     "i" (ASI_M_DATAC_TAG));

		/* If modified and valid, kick it. */
		if((tagval & 0x60) == 0x60)
			cypress_sucks = *(unsigned long *)(0xf0020000 + faddr);
	}
}

static void cypress_flush_cache_mm(struct mm_struct *mm)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long flags, faddr;
	int octx;

	FLUSH_BEGIN(mm)
	flush_user_windows();
	__save_and_cli(flags);
	octx = srmmu_get_context();
	srmmu_set_context(mm->context);
	a = 0x20; b = 0x40; c = 0x60;
	d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;

	faddr = (0x10000 - 0x100);
	goto inside;
	do {
		faddr -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (faddr), "i" (ASI_M_FLUSH_CTX),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(faddr);
	srmmu_set_context(octx);
	__restore_flags(flags);
	FLUSH_END
}

static void cypress_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long flags, faddr;
	int octx;

	FLUSH_BEGIN(mm)
	flush_user_windows();
	__save_and_cli(flags);
	octx = srmmu_get_context();
	srmmu_set_context(mm->context);
	a = 0x20; b = 0x40; c = 0x60;
	d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;

	start &= SRMMU_PMD_MASK;
	while(start < end) {
		faddr = (start + (0x10000 - 0x100));
		goto inside;
		do {
			faddr -= 0x100;
		inside:
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
					     "sta %%g0, [%0 + %2] %1\n\t"
					     "sta %%g0, [%0 + %3] %1\n\t"
					     "sta %%g0, [%0 + %4] %1\n\t"
					     "sta %%g0, [%0 + %5] %1\n\t"
					     "sta %%g0, [%0 + %6] %1\n\t"
					     "sta %%g0, [%0 + %7] %1\n\t"
					     "sta %%g0, [%0 + %8] %1\n\t" : :
					     "r" (faddr),
					     "i" (ASI_M_FLUSH_SEG),
					     "r" (a), "r" (b), "r" (c), "r" (d),
					     "r" (e), "r" (f), "r" (g));
		} while (faddr != start);
		start += SRMMU_PMD_SIZE;
	}
	srmmu_set_context(octx);
	__restore_flags(flags);
	FLUSH_END
}

static void cypress_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	register unsigned long a, b, c, d, e, f, g;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags, line;
	int octx;

	FLUSH_BEGIN(mm)
	flush_user_windows();
	__save_and_cli(flags);
	octx = srmmu_get_context();
	srmmu_set_context(mm->context);
	a = 0x20; b = 0x40; c = 0x60;
	d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;

	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
			__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
					     "sta %%g0, [%0 + %2] %1\n\t"
					     "sta %%g0, [%0 + %3] %1\n\t"
					     "sta %%g0, [%0 + %4] %1\n\t"
					     "sta %%g0, [%0 + %5] %1\n\t"
					     "sta %%g0, [%0 + %6] %1\n\t"
					     "sta %%g0, [%0 + %7] %1\n\t"
					     "sta %%g0, [%0 + %8] %1\n\t" : :
					     "r" (line),
					     "i" (ASI_M_FLUSH_PAGE),
					     "r" (a), "r" (b), "r" (c), "r" (d),
					     "r" (e), "r" (f), "r" (g));
	} while(line != page);
	srmmu_set_context(octx);
	__restore_flags(flags);
	FLUSH_END
}

/* Cypress is copy-back, at least that is how we configure it. */
static void cypress_flush_page_to_ram(unsigned long page)
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long line;

	a = 0x20; b = 0x40; c = 0x60; d = 0x80; e = 0xa0; f = 0xc0; g = 0xe0;
	page &= PAGE_MASK;
	line = (page + PAGE_SIZE) - 0x100;
	goto inside;
	do {
		line -= 0x100;
	inside:
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     "sta %%g0, [%0 + %2] %1\n\t"
				     "sta %%g0, [%0 + %3] %1\n\t"
				     "sta %%g0, [%0 + %4] %1\n\t"
				     "sta %%g0, [%0 + %5] %1\n\t"
				     "sta %%g0, [%0 + %6] %1\n\t"
				     "sta %%g0, [%0 + %7] %1\n\t"
				     "sta %%g0, [%0 + %8] %1\n\t" : :
				     "r" (line),
				     "i" (ASI_M_FLUSH_PAGE),
				     "r" (a), "r" (b), "r" (c), "r" (d),
				     "r" (e), "r" (f), "r" (g));
	} while(line != page);
}

static void cypress_flush_chunk(unsigned long chunk)
{
	cypress_flush_page_to_ram(chunk);
}

/* Cypress is also IO cache coherent. */
static void cypress_flush_page_for_dma(unsigned long page)
{
}

/* Cypress has unified L2 VIPT, from which both instructions and data
 * are stored.  It does not have an onboard icache of any sort, therefore
 * no flush is necessary.
 */
static void cypress_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
}

static void cypress_flush_tlb_all(void)
{
	srmmu_flush_whole_tlb();
	module_stats.invall++;
}

static void cypress_flush_tlb_mm(struct mm_struct *mm)
{
	FLUSH_BEGIN(mm)
	__asm__ __volatile__("
	lda	[%0] %3, %%g5
	sta	%2, [%0] %3
	sta	%%g0, [%1] %4
	sta	%%g5, [%0] %3"
	: /* no outputs */
	: "r" (SRMMU_CTX_REG), "r" (0x300), "r" (mm->context),
	  "i" (ASI_M_MMUREGS), "i" (ASI_M_FLUSH_PROBE)
	: "g5");
	module_stats.invmm++;
	FLUSH_END
}

static void cypress_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	unsigned long size;

	FLUSH_BEGIN(mm)
	start &= SRMMU_PGDIR_MASK;
	size = SRMMU_PGDIR_ALIGN(end) - start;
	__asm__ __volatile__("
		lda	[%0] %5, %%g5
		sta	%1, [%0] %5
	1:	subcc	%3, %4, %3
		bne	1b
		 sta	%%g0, [%2 + %3] %6
		sta	%%g5, [%0] %5"
	: /* no outputs */
	: "r" (SRMMU_CTX_REG), "r" (mm->context), "r" (start | 0x200),
	  "r" (size), "r" (SRMMU_PGDIR_SIZE), "i" (ASI_M_MMUREGS),
	  "i" (ASI_M_FLUSH_PROBE)
	: "g5", "cc");
	module_stats.invrnge++;
	FLUSH_END
}

static void cypress_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	FLUSH_BEGIN(mm)
	__asm__ __volatile__("
	lda	[%0] %3, %%g5
	sta	%1, [%0] %3
	sta	%%g0, [%2] %4
	sta	%%g5, [%0] %3"
	: /* no outputs */
	: "r" (SRMMU_CTX_REG), "r" (mm->context), "r" (page & PAGE_MASK),
	  "i" (ASI_M_MMUREGS), "i" (ASI_M_FLUSH_PROBE)
	: "g5");
	module_stats.invpg++;
	FLUSH_END
}

/* viking.S */
extern void viking_flush_cache_all(void);
extern void viking_flush_cache_mm(struct mm_struct *mm);
extern void viking_flush_cache_range(struct mm_struct *mm, unsigned long start,
				     unsigned long end);
extern void viking_flush_cache_page(struct vm_area_struct *vma,
				    unsigned long page);
extern void viking_flush_page_to_ram(unsigned long page);
extern void viking_flush_page_for_dma(unsigned long page);
extern void viking_flush_sig_insns(struct mm_struct *mm, unsigned long addr);
extern void viking_flush_page(unsigned long page);
extern void viking_mxcc_flush_page(unsigned long page);
extern void viking_flush_chunk(unsigned long chunk);
extern void viking_mxcc_flush_chunk(unsigned long chunk);
extern void viking_flush_tlb_all(void);
extern void viking_flush_tlb_mm(struct mm_struct *mm);
extern void viking_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				   unsigned long end);
extern void viking_flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long page);
extern void sun4dsmp_flush_tlb_all(void);
extern void sun4dsmp_flush_tlb_mm(struct mm_struct *mm);
extern void sun4dsmp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				   unsigned long end);
extern void sun4dsmp_flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long page);

/* hypersparc.S */
extern void hypersparc_flush_cache_all(void);
extern void hypersparc_flush_cache_mm(struct mm_struct *mm);
extern void hypersparc_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end);
extern void hypersparc_flush_cache_page(struct vm_area_struct *vma, unsigned long page);
extern void hypersparc_flush_page_to_ram(unsigned long page);
extern void hypersparc_flush_chunk(unsigned long chunk);
extern void hypersparc_flush_page_for_dma(unsigned long page);
extern void hypersparc_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr);
extern void hypersparc_flush_tlb_all(void);
extern void hypersparc_flush_tlb_mm(struct mm_struct *mm);
extern void hypersparc_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end);
extern void hypersparc_flush_tlb_page(struct vm_area_struct *vma, unsigned long page);
extern void hypersparc_setup_blockops(void);

static void srmmu_set_pte_nocache_hyper(pte_t *ptep, pte_t pteval)
{
	unsigned long page = ((unsigned long)ptep) & PAGE_MASK;

	srmmu_set_entry(ptep, pte_val(pteval));
	hypersparc_flush_page_to_ram(page);
}

static void hypersparc_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{
	srmmu_set_entry((pte_t *)ctxp, __pte((SRMMU_ET_PTD | (__pa((unsigned long) pgdp) >> 4))));
	hypersparc_flush_page_to_ram((unsigned long)ctxp);
	hyper_flush_whole_icache();
}

static void hypersparc_switch_mm(struct mm_struct *old_mm,
    struct mm_struct *mm, struct task_struct *tsk, int cpu)
{
	if(mm->context == NO_CONTEXT) {
		ctxd_t *ctxp;

		spin_lock(&srmmu_context_spinlock);
		alloc_context(old_mm, mm);
		spin_unlock(&srmmu_context_spinlock);
		ctxp = &srmmu_context_table[mm->context];
		srmmu_set_entry((pte_t *)ctxp, __pte((SRMMU_ET_PTD | (__pa((unsigned long) mm->pgd) >> 4))));
		hypersparc_flush_page_to_ram((unsigned long)ctxp);
	}
	hyper_flush_whole_icache();
	srmmu_set_context(mm->context);
}

/* NOTE: All of this startup code assumes the low 16mb (approx.) of
 *       kernel mappings are done with one single contiguous chunk of
 *       ram.  On small ram machines (classics mainly) we only get
 *       around 8mb mapped for us.
 */

void __init early_pgtable_allocfail(char *type)
{
	prom_printf("inherit_prom_mappings: Cannot alloc kernel %s.\n", type);
	prom_halt();
}

static inline void srmmu_allocate_ptable_skeleton(unsigned long start, unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	while(start < end) {
		pgdp = srmmu_pgd_offset(&init_mm, start);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = __alloc_bootmem(SRMMU_PMD_TABLE_SIZE,
					SRMMU_PMD_TABLE_SIZE, 0UL);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(pmdp, 0, SRMMU_PMD_TABLE_SIZE);
			srmmu_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_pmd_offset(pgdp, start);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = __alloc_bootmem(SRMMU_PTE_TABLE_SIZE, SRMMU_PTE_TABLE_SIZE, 0UL);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(ptep, 0, SRMMU_PTE_TABLE_SIZE);
			srmmu_pmd_set(pmdp, ptep);
		}
		start = (start + SRMMU_PMD_SIZE) & SRMMU_PMD_MASK;
	}
}

/* This is much cleaner than poking around physical address space
 * looking at the prom's page table directly which is what most
 * other OS's do.  Yuck... this is much better.
 */
void __init srmmu_inherit_prom_mappings(unsigned long start,unsigned long end)
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
    
		pgdp = srmmu_pgd_offset(&init_mm, start);
		if(what == 2) {
			*pgdp = __pgd(prompte);
			start += SRMMU_PGDIR_SIZE;
			continue;
		}
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = __alloc_bootmem(SRMMU_PMD_TABLE_SIZE,
					SRMMU_PMD_TABLE_SIZE, 0UL);
			if (pmdp == NULL)
				early_pgtable_allocfail("pmd");
			memset(pmdp, 0, SRMMU_PMD_TABLE_SIZE);
			srmmu_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_pmd_offset(pgdp, start);
		if(what == 1) {
			*pmdp = __pmd(prompte);
			start += SRMMU_PMD_SIZE;
			continue;
		}
		if(srmmu_pmd_none(*pmdp)) {
			ptep = __alloc_bootmem(SRMMU_PTE_TABLE_SIZE, SRMMU_PTE_TABLE_SIZE, 0UL);
			if (ptep == NULL)
				early_pgtable_allocfail("pte");
			memset(ptep, 0, SRMMU_PTE_TABLE_SIZE);
			srmmu_pmd_set(pmdp, ptep);
		}
		ptep = srmmu_pte_offset(pmdp, start);
		*ptep = __pte(prompte);
		start += PAGE_SIZE;
	}
}

static unsigned long end_of_phys_memory __initdata = 0;

#define KERNEL_PTE(page_shifted) ((page_shifted)|SRMMU_CACHE|SRMMU_PRIV|SRMMU_VALID)

/* Create a third-level SRMMU 16MB page mapping. */
static void __init do_large_mapping(unsigned long vaddr, unsigned long phys_base)
{
	pgd_t *pgdp = srmmu_pgd_offset(&init_mm, vaddr);
	unsigned long big_pte;

	big_pte = KERNEL_PTE(phys_base >> 4);
	*pgdp = __pgd(big_pte);
}

/* Map sp_bank entry SP_ENTRY, starting at virtual address VBASE.
 */
static unsigned long __init map_spbank(unsigned long vbase, int sp_entry)
{
	unsigned long pstart = (sp_banks[sp_entry].base_addr & SRMMU_PGDIR_MASK);
	unsigned long vstart = (vbase & SRMMU_PGDIR_MASK);
	unsigned long vend = SRMMU_PGDIR_ALIGN(vbase + sp_banks[sp_entry].num_bytes);

	while(vstart < vend) {
		do_large_mapping(vstart, pstart);
		vstart += SRMMU_PGDIR_SIZE; pstart += SRMMU_PGDIR_SIZE;
	}
	return vstart;
}

static inline void memprobe_error(char *msg)
{
	prom_printf(msg);
	prom_printf("Halting now...\n");
	prom_halt();
}

static inline void map_kernel(void)
{
	int i;

	if (phys_base > 0) {
		do_large_mapping(PAGE_OFFSET, phys_base);
	}

	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		map_spbank((unsigned long)__va(sp_banks[i].base_addr), i);
	}

	init_mm.mmap->vm_start = PAGE_OFFSET;
	BTFIXUPSET_SIMM13(user_ptrs_per_pgd, PAGE_OFFSET / SRMMU_PGDIR_SIZE);
}

/* Paging initialization on the Sparc Reference MMU. */
extern void sparc_context_init(int);

extern int linux_num_cpus;

void (*poke_srmmu)(void) __initdata = NULL;

extern unsigned long bootmem_init(void);
extern void sun_serial_setup(void);

void __init srmmu_paging_init(void)
{
	int i, cpunode;
	char node_str[128];
	unsigned long end_pfn;

	sparc_iomap.start = 0xfd000000;    /* 16MB of IOSPACE on all sun4m's. */

	if (sparc_cpu_model == sun4d)
		num_contexts = 65536; /* We know it is Viking */
	else {
		/* Find the number of contexts on the srmmu. */
		cpunode = prom_getchild(prom_root_node);
		num_contexts = 0;
		while(cpunode != 0) {
			prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
			if(!strcmp(node_str, "cpu")) {
				num_contexts = prom_getintdefault(cpunode, "mmu-nctx", 0x8);
				break;
			}
			cpunode = prom_getsibling(cpunode);
		}
	}

	if(!num_contexts) {
		prom_printf("Something wrong, can't find cpu node in paging_init.\n");
		prom_halt();
	}
		
	memset(swapper_pg_dir, 0, PAGE_SIZE);

	last_valid_pfn = end_pfn = bootmem_init();

	srmmu_allocate_ptable_skeleton(KERNBASE, (unsigned long)__va(end_of_phys_memory));
#if CONFIG_SUN_IO
	srmmu_allocate_ptable_skeleton(sparc_iomap.start, IOBASE_END);
	srmmu_allocate_ptable_skeleton(DVMA_VADDR, DVMA_END);
#endif

	/* This does not logically belong here, but we need to
	 * call it at the moment we are able to use the bootmem
	 * allocator.
	 */
	sun_serial_setup();

        srmmu_inherit_prom_mappings(0xfe400000,(LINUX_OPPROM_ENDVM-PAGE_SIZE));
	map_kernel();

#define BOOTMEM_BROKEN
#ifdef BOOTMEM_BROKEN
	srmmu_context_table = __alloc_bootmem(num_contexts*sizeof(ctxd_t)*2, SMP_CACHE_BYTES, 0UL);
	(unsigned long)srmmu_context_table += num_contexts*sizeof(ctxd_t);
	(unsigned long)srmmu_context_table &= ~(num_contexts*sizeof(ctxd_t)-1);
#else
	srmmu_context_table = __alloc_bootmem(num_contexts*sizeof(ctxd_t), num_contexts*sizeof(ctxd_t), 0UL);
#endif

	srmmu_ctx_table_phys = (ctxd_t *) __pa((unsigned long) srmmu_context_table);
	for(i = 0; i < num_contexts; i++)
		ctxd_set(&srmmu_context_table[i], swapper_pg_dir);

	flush_cache_all();
	if(BTFIXUPVAL_CALL(flush_page_for_dma) == (unsigned long)viking_flush_page) {
		unsigned long start = (unsigned long)srmmu_context_table;
		unsigned long end = PAGE_ALIGN((unsigned long)srmmu_context_table + num_contexts*sizeof(ctxd_t));

		while(start < end) {
			viking_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	srmmu_set_ctable_ptr((unsigned long) srmmu_ctx_table_phys);
	flush_tlb_all();
	poke_srmmu();

	sparc_context_init(num_contexts);

	{
		unsigned long zones_size[MAX_NR_ZONES] = { 0, 0, 0};

		zones_size[ZONE_DMA] = end_pfn;
		free_area_init(zones_size);
	}
	
#ifdef CONFIG_BLK_DEV_INITRD
	/* If initial ramdisk was specified with physical address,
	   translate it here, as the p2v translation in srmmu
	   is not straightforward. */
	if (initrd_start && initrd_start < KERNBASE) {
		initrd_start = __va(initrd_start);
		initrd_end = __va(initrd_end);
		if (initrd_end <= initrd_start)
			initrd_start = 0;
	}
#endif

}

static int srmmu_mmu_info(char *buf)
{
	return sprintf(buf, 
		"MMU type\t: %s\n"
		"invall\t\t: %d\n"
		"invmm\t\t: %d\n"
		"invrnge\t\t: %d\n"
		"invpg\t\t: %d\n"
		"contexts\t: %d\n"
		, srmmu_name,
		module_stats.invall,
		module_stats.invmm,
		module_stats.invrnge,
		module_stats.invpg,
		num_contexts
	);
}

static void srmmu_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
}

static void srmmu_destroy_context(struct mm_struct *mm)
{

	if(mm->context != NO_CONTEXT) {
		flush_cache_mm(mm);
		ctxd_set(&srmmu_context_table[mm->context], swapper_pg_dir);
		flush_tlb_mm(mm);
		spin_lock(&srmmu_context_spinlock);
		free_context(mm->context);
		spin_unlock(&srmmu_context_spinlock);
		mm->context = NO_CONTEXT;
	}
}

static void srmmu_vac_update_mmu_cache(struct vm_area_struct * vma,
				       unsigned long address, pte_t pte)
{
	if((vma->vm_flags & (VM_WRITE|VM_SHARED)) == (VM_WRITE|VM_SHARED)) {
		struct vm_area_struct *vmaring;
		struct file *file;
		struct address_space *mapping;
		unsigned long flags, offset, vaddr, start;
		int alias_found = 0;
		pgd_t *pgdp;
		pmd_t *pmdp;
		pte_t *ptep;

		__save_and_cli(flags);

		file = vma->vm_file;
		if (!file)
			goto done;
		mapping = file->f_dentry->d_inode->i_mapping;
		offset = (address & PAGE_MASK) - vma->vm_start;
		spin_lock(&mapping->i_shared_lock);
		vmaring = mapping->i_mmap; 
		do {
			/* Do not mistake ourselves as another mapping. */
			if(vmaring == vma)
				continue;

			vaddr = vmaring->vm_start + offset;
			if ((vaddr ^ address) & vac_badbits) {
				alias_found++;
				start = vmaring->vm_start;
				while (start < vmaring->vm_end) {
					pgdp = srmmu_pgd_offset(vmaring->vm_mm, start);
					if(!pgdp) goto next;
					pmdp = srmmu_pmd_offset(pgdp, start);
					if(!pmdp) goto next;
					ptep = srmmu_pte_offset(pmdp, start);
					if(!ptep) goto next;

					if((pte_val(*ptep) & SRMMU_ET_MASK) == SRMMU_VALID) {
#if 0
						printk("Fixing USER/USER alias [%ld:%08lx]\n",
						       vmaring->vm_mm->context, start);
#endif
						flush_cache_page(vmaring, start);
						set_pte(ptep, __pte((pte_val(*ptep) &
								     ~SRMMU_CACHE)));
						flush_tlb_page(vmaring, start);
					}
				next:
					start += PAGE_SIZE;
				}
			}
		} while ((vmaring = vmaring->vm_next_share) != NULL);
		spin_unlock(&mapping->i_shared_lock);

		if(alias_found && ((pte_val(pte) & SRMMU_CACHE) != 0)) {
			pgdp = srmmu_pgd_offset(vma->vm_mm, address);
			pmdp = srmmu_pmd_offset(pgdp, address);
			ptep = srmmu_pte_offset(pmdp, address);
			flush_cache_page(vma, address);
			set_pte(ptep, __pte((pte_val(*ptep) & ~SRMMU_CACHE)));
			flush_tlb_page(vma, address);
		}
	done:
		__restore_flags(flags);
	}
}

static void hypersparc_destroy_context(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT) {
		ctxd_t *ctxp;

		/* HyperSparc is copy-back, any data for this
		 * process in a modified cache line is stale
		 * and must be written back to main memory now
		 * else we eat shit later big time.
		 */
		flush_cache_mm(mm);

		ctxp = &srmmu_context_table[mm->context];
		srmmu_set_entry((pte_t *)ctxp, __pte((SRMMU_ET_PTD | (__pa((unsigned long) swapper_pg_dir) >> 4))));
		hypersparc_flush_page_to_ram((unsigned long)ctxp);

		flush_tlb_mm(mm);
		spin_lock(&srmmu_context_spinlock);
		free_context(mm->context);
		spin_unlock(&srmmu_context_spinlock);
		mm->context = NO_CONTEXT;
	}
}

/* Init various srmmu chip types. */
static void __init srmmu_is_bad(void)
{
	prom_printf("Could not determine SRMMU chip type.\n");
	prom_halt();
}

static void __init init_vac_layout(void)
{
	int nd, cache_lines;
	char node_str[128];
#ifdef __SMP__
	int cpu = 0;
	unsigned long max_size = 0;
	unsigned long min_line_size = 0x10000000;
#endif

	nd = prom_getchild(prom_root_node);
	while((nd = prom_getsibling(nd)) != 0) {
		prom_getstring(nd, "device_type", node_str, sizeof(node_str));
		if(!strcmp(node_str, "cpu")) {
			vac_line_size = prom_getint(nd, "cache-line-size");
			if (vac_line_size == -1) {
				prom_printf("can't determine cache-line-size, "
					    "halting.\n");
				prom_halt();
			}
			cache_lines = prom_getint(nd, "cache-nlines");
			if (cache_lines == -1) {
				prom_printf("can't determine cache-nlines, halting.\n");
				prom_halt();
			}

			vac_cache_size = cache_lines * vac_line_size;
			vac_badbits = (vac_cache_size - 1) & PAGE_MASK;
#ifdef __SMP__
			if(vac_cache_size > max_size)
				max_size = vac_cache_size;
			if(vac_line_size < min_line_size)
				min_line_size = vac_line_size;
			cpu++;
			if(cpu == smp_num_cpus)
				break;
#else
			break;
#endif
		}
	}
	if(nd == 0) {
		prom_printf("No CPU nodes found, halting.\n");
		prom_halt();
	}
#ifdef __SMP__
	vac_cache_size = max_size;
	vac_line_size = min_line_size;
	vac_badbits = (vac_cache_size - 1) & PAGE_MASK;
#endif
	printk("SRMMU: Using VAC size of %d bytes, line size %d bytes.\n",
	       (int)vac_cache_size, (int)vac_line_size);
}

static void __init poke_hypersparc(void)
{
	volatile unsigned long clear;
	unsigned long mreg = srmmu_get_mmureg();

	hyper_flush_unconditional_combined();

	mreg &= ~(HYPERSPARC_CWENABLE);
	mreg |= (HYPERSPARC_CENABLE | HYPERSPARC_WBENABLE);
	mreg |= (HYPERSPARC_CMODE);

	srmmu_set_mmureg(mreg);

#if 0 /* I think this is bad news... -DaveM */
	hyper_clear_all_tags();
#endif

	put_ross_icr(HYPERSPARC_ICCR_FTD | HYPERSPARC_ICCR_ICE);
	hyper_flush_whole_icache();
	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();
}

static void __init init_hypersparc(void)
{
	srmmu_name = "ROSS HyperSparc";

	init_vac_layout();

	BTFIXUPSET_CALL(set_pte, srmmu_set_pte_nocache_hyper, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_all, hypersparc_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, hypersparc_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, hypersparc_flush_cache_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, hypersparc_flush_cache_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_tlb_all, hypersparc_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, hypersparc_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, hypersparc_flush_tlb_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, hypersparc_flush_tlb_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, hypersparc_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, hypersparc_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, hypersparc_flush_page_for_dma, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(flush_chunk, hypersparc_flush_chunk, BTFIXUPCALL_NORM); /* local flush _only_ */

	BTFIXUPSET_CALL(ctxd_set, hypersparc_ctxd_set, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(switch_mm, hypersparc_switch_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(destroy_context, hypersparc_destroy_context, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(update_mmu_cache, srmmu_vac_update_mmu_cache, BTFIXUPCALL_NORM);
	poke_srmmu = poke_hypersparc;

	hypersparc_setup_blockops();
}

static void __init poke_cypress(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	unsigned long faddr, tagval;
	volatile unsigned long cypress_sucks;
	volatile unsigned long clear;

	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();

	if (!(mreg & CYPRESS_CENABLE)) {
		for(faddr = 0x0; faddr < 0x10000; faddr += 20) {
			__asm__ __volatile__("sta %%g0, [%0 + %1] %2\n\t"
					     "sta %%g0, [%0] %2\n\t" : :
					     "r" (faddr), "r" (0x40000),
					     "i" (ASI_M_DATAC_TAG));
		}
	} else {
		for(faddr = 0; faddr < 0x10000; faddr += 0x20) {
			__asm__ __volatile__("lda [%1 + %2] %3, %0\n\t" :
					     "=r" (tagval) :
					     "r" (faddr), "r" (0x40000),
					     "i" (ASI_M_DATAC_TAG));

			/* If modified and valid, kick it. */
			if((tagval & 0x60) == 0x60)
				cypress_sucks = *(unsigned long *)
							(0xf0020000 + faddr);
		}
	}

	/* And one more, for our good neighbor, Mr. Broken Cypress. */
	clear = srmmu_get_faddr();
	clear = srmmu_get_fstatus();

	mreg |= (CYPRESS_CENABLE | CYPRESS_CMODE);
	srmmu_set_mmureg(mreg);
}

static void __init init_cypress_common(void)
{
	init_vac_layout();

	BTFIXUPSET_CALL(set_pte, srmmu_set_pte_nocache_cypress, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_all, cypress_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, cypress_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, cypress_flush_cache_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, cypress_flush_cache_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_tlb_all, cypress_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, cypress_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, cypress_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, cypress_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_chunk, cypress_flush_chunk, BTFIXUPCALL_NORM); /* local flush _only_ */

	BTFIXUPSET_CALL(__flush_page_to_ram, cypress_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, cypress_flush_sig_insns, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_page_for_dma, cypress_flush_page_for_dma, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(update_mmu_cache, srmmu_vac_update_mmu_cache, BTFIXUPCALL_NORM);
	poke_srmmu = poke_cypress;
}

static void __init init_cypress_604(void)
{
	srmmu_name = "ROSS Cypress-604(UP)";
	srmmu_modtype = Cypress;
	init_cypress_common();
}

static void __init init_cypress_605(unsigned long mrev)
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

static void __init poke_swift(void)
{
	unsigned long mreg;

	/* Clear any crap from the cache or else... */
	swift_flush_cache_all();

	/* Enable I & D caches */
	mreg = srmmu_get_mmureg();
	mreg |= (SWIFT_IE | SWIFT_DE);
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
static void __init init_swift(void)
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

	BTFIXUPSET_CALL(flush_cache_all, swift_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, swift_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, swift_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, swift_flush_cache_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_chunk, swift_flush_chunk, BTFIXUPCALL_NORM); /* local flush _only_ */

	BTFIXUPSET_CALL(flush_tlb_all, swift_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, swift_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, swift_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, swift_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, swift_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, swift_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, swift_flush_page_for_dma, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(update_mmu_cache, swift_update_mmu_cache, BTFIXUPCALL_NORM);

	flush_page_for_dma_global = 0;

	/* Are you now convinced that the Swift is one of the
	 * biggest VLSI abortions of all time?  Bravo Fujitsu!
	 * Fujitsu, the !#?!%$'d up processor people.  I bet if
	 * you examined the microcode of the Swift you'd find
	 * XXX's all over the place.
	 */
	poke_srmmu = poke_swift;
}

static void turbosparc_flush_cache_all(void)
{
	flush_user_windows();
	turbosparc_idflash_clear();
}

static void turbosparc_flush_cache_mm(struct mm_struct *mm)
{
	FLUSH_BEGIN(mm)
	flush_user_windows();
	turbosparc_idflash_clear();
	FLUSH_END
}

static void turbosparc_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	FLUSH_BEGIN(mm)
	flush_user_windows();
	turbosparc_idflash_clear();
	FLUSH_END
}

static void turbosparc_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	FLUSH_BEGIN(vma->vm_mm)
	flush_user_windows();
	if (vma->vm_flags & VM_EXEC)
		turbosparc_flush_icache();
	turbosparc_flush_dcache();
	FLUSH_END
}

/* TurboSparc is copy-back, if we turn it on, but this does not work. */
static void turbosparc_flush_page_to_ram(unsigned long page)
{
#ifdef TURBOSPARC_WRITEBACK
	volatile unsigned long clear;

	if (srmmu_hwprobe(page))
		turbosparc_flush_page_cache(page);
	clear = srmmu_get_fstatus();
#endif
}

static void turbosparc_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
}

static void turbosparc_flush_page_for_dma(unsigned long page)
{
	turbosparc_flush_dcache();
}

static void turbosparc_flush_chunk(unsigned long chunk)
{
}

static void turbosparc_flush_tlb_all(void)
{
	srmmu_flush_whole_tlb();
	module_stats.invall++;
}

static void turbosparc_flush_tlb_mm(struct mm_struct *mm)
{
	FLUSH_BEGIN(mm)
	srmmu_flush_whole_tlb();
	module_stats.invmm++;
	FLUSH_END
}

static void turbosparc_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	FLUSH_BEGIN(mm)
	srmmu_flush_whole_tlb();
	module_stats.invrnge++;
	FLUSH_END
}

static void turbosparc_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	FLUSH_BEGIN(vma->vm_mm)
	srmmu_flush_whole_tlb();
	module_stats.invpg++;
	FLUSH_END
}


static void __init poke_turbosparc(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	unsigned long ccreg;

	/* Clear any crap from the cache or else... */
	turbosparc_flush_cache_all();
	mreg &= ~(TURBOSPARC_ICENABLE | TURBOSPARC_DCENABLE); /* Temporarily disable I & D caches */
	mreg &= ~(TURBOSPARC_PCENABLE);		/* Don't check parity */
	srmmu_set_mmureg(mreg);
	
	ccreg = turbosparc_get_ccreg();

#ifdef TURBOSPARC_WRITEBACK
	ccreg |= (TURBOSPARC_SNENABLE);		/* Do DVMA snooping in Dcache */
	ccreg &= ~(TURBOSPARC_uS2 | TURBOSPARC_WTENABLE);
			/* Write-back D-cache, emulate VLSI
			 * abortion number three, not number one */
#else
	/* For now let's play safe, optimize later */
	ccreg |= (TURBOSPARC_SNENABLE | TURBOSPARC_WTENABLE);
			/* Do DVMA snooping in Dcache, Write-thru D-cache */
	ccreg &= ~(TURBOSPARC_uS2);
			/* Emulate VLSI abortion number three, not number one */
#endif

	switch (ccreg & 7) {
	case 0: /* No SE cache */
	case 7: /* Test mode */
		break;
	default:
		ccreg |= (TURBOSPARC_SCENABLE);
	}
	turbosparc_set_ccreg (ccreg);

	mreg |= (TURBOSPARC_ICENABLE | TURBOSPARC_DCENABLE); /* I & D caches on */
	mreg |= (TURBOSPARC_ICSNOOP);		/* Icache snooping on */
	srmmu_set_mmureg(mreg);
}

static void __init init_turbosparc(void)
{
	srmmu_name = "Fujitsu TurboSparc";
	srmmu_modtype = TurboSparc;

	BTFIXUPSET_CALL(flush_cache_all, turbosparc_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, turbosparc_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, turbosparc_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, turbosparc_flush_cache_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_tlb_all, turbosparc_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, turbosparc_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, turbosparc_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, turbosparc_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, turbosparc_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_chunk, turbosparc_flush_chunk, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_sig_insns, turbosparc_flush_sig_insns, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_page_for_dma, turbosparc_flush_page_for_dma, BTFIXUPCALL_NORM);

	poke_srmmu = poke_turbosparc;
}

static void __init poke_tsunami(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	tsunami_flush_icache();
	tsunami_flush_dcache();
	mreg &= ~TSUNAMI_ITD;
	mreg |= (TSUNAMI_IENAB | TSUNAMI_DENAB);
	srmmu_set_mmureg(mreg);
}

static void __init init_tsunami(void)
{
	/* Tsunami's pretty sane, Sun and TI actually got it
	 * somewhat right this time.  Fujitsu should have
	 * taken some lessons from them.
	 */

	srmmu_name = "TI Tsunami";
	srmmu_modtype = Tsunami;

	BTFIXUPSET_CALL(flush_cache_all, tsunami_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, tsunami_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, tsunami_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, tsunami_flush_cache_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_chunk, tsunami_flush_chunk, BTFIXUPCALL_NORM); /* local flush _only_ */

	BTFIXUPSET_CALL(flush_tlb_all, tsunami_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, tsunami_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, tsunami_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, tsunami_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(__flush_page_to_ram, tsunami_flush_page_to_ram, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, tsunami_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, tsunami_flush_page_for_dma, BTFIXUPCALL_NORM);

	poke_srmmu = poke_tsunami;

	tsunami_setup_blockops();
}

static void __init poke_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();
	static int smp_catch = 0;

	if(viking_mxcc_present) {
		unsigned long mxcc_control = mxcc_get_creg();

		mxcc_control |= (MXCC_CTL_ECE | MXCC_CTL_PRE | MXCC_CTL_MCE);
		mxcc_control &= ~(MXCC_CTL_RRC);
		mxcc_set_creg(mxcc_control);

		/* We don't need memory parity checks.
		 * XXX This is a mess, have to dig out later. ecd.
		viking_mxcc_turn_off_parity(&mreg, &mxcc_control);
		 */

		/* We do cache ptables on MXCC. */
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

	mreg |= VIKING_SPENABLE;
	mreg |= (VIKING_ICENABLE | VIKING_DCENABLE);
	mreg |= VIKING_SBENABLE;
	mreg &= ~(VIKING_ACENABLE);
	srmmu_set_mmureg(mreg);

#ifdef __SMP__
	/* Avoid unnecessary cross calls. */
	BTFIXUPCOPY_CALL(flush_cache_all, local_flush_cache_all);
	BTFIXUPCOPY_CALL(flush_cache_mm, local_flush_cache_mm);
	BTFIXUPCOPY_CALL(flush_cache_range, local_flush_cache_range);
	BTFIXUPCOPY_CALL(flush_cache_page, local_flush_cache_page);
	BTFIXUPCOPY_CALL(__flush_page_to_ram, local_flush_page_to_ram);
	BTFIXUPCOPY_CALL(flush_sig_insns, local_flush_sig_insns);
	BTFIXUPCOPY_CALL(flush_page_for_dma, local_flush_page_for_dma);
	btfixup();
#endif
}

static void __init init_viking(void)
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Ahhh, the viking.  SRMMU VLSI abortion number two... */
	if(mreg & VIKING_MMODE) {
		srmmu_name = "TI Viking";
		viking_mxcc_present = 0;
		msi_set_sync();

		BTFIXUPSET_CALL(set_pte, srmmu_set_pte_nocache_viking, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_NORM);

		BTFIXUPSET_CALL(flush_chunk, viking_flush_chunk, BTFIXUPCALL_NORM); /* local flush _only_ */

		/* We need this to make sure old viking takes no hits
		 * on it's cache for dma snoops to workaround the
		 * "load from non-cacheable memory" interrupt bug.
		 * This is only necessary because of the new way in
		 * which we use the IOMMU.
		 */
		BTFIXUPSET_CALL(flush_page_for_dma, viking_flush_page, BTFIXUPCALL_NORM);

		flush_page_for_dma_global = 0;
	} else {
		srmmu_name = "TI Viking/MXCC";
		viking_mxcc_present = 1;

		BTFIXUPSET_CALL(flush_chunk, viking_mxcc_flush_chunk, BTFIXUPCALL_NOP); /* local flush _only_ */

		/* MXCC vikings lack the DMA snooping bug. */
		BTFIXUPSET_CALL(flush_page_for_dma, viking_flush_page_for_dma, BTFIXUPCALL_NOP);
	}

	BTFIXUPSET_CALL(flush_cache_all, viking_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, viking_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, viking_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, viking_flush_cache_range, BTFIXUPCALL_NORM);

#ifdef __SMP__
	if (sparc_cpu_model == sun4d) {
		BTFIXUPSET_CALL(flush_tlb_all, sun4dsmp_flush_tlb_all, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_mm, sun4dsmp_flush_tlb_mm, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_page, sun4dsmp_flush_tlb_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_range, sun4dsmp_flush_tlb_range, BTFIXUPCALL_NORM);
	} else
#endif
	{
		BTFIXUPSET_CALL(flush_tlb_all, viking_flush_tlb_all, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_mm, viking_flush_tlb_mm, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_page, viking_flush_tlb_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_range, viking_flush_tlb_range, BTFIXUPCALL_NORM);
	}

	BTFIXUPSET_CALL(__flush_page_to_ram, viking_flush_page_to_ram, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, viking_flush_sig_insns, BTFIXUPCALL_NOP);

	poke_srmmu = poke_viking;
}

/* Probe for the srmmu chip version. */
static void __init get_srmmu_type(void)
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
		case 2:
			/* Uniprocessor Cypress */
			init_cypress_604();
			break;
		case 10:
		case 11:
		case 12:
			/* _REALLY OLD_ Cypress MP chips... */
		case 13:
		case 14:
		case 15:
			/* MP Cypress mmu/cache-controller */
			init_cypress_605(mod_rev);
			break;
		default:
			/* Some other Cypress revision, assume a 605. */
			init_cypress_605(mod_rev);
			break;
		};
		return;
	}
	
	/* Now Fujitsu TurboSparc. It might happen that it is
	   in Swift emulation mode, so we will check later... */
	if (psr_typ == 0 && psr_vers == 5) {
		init_turbosparc();
		return;
	}

	/* Next check for Fujitsu Swift. */
	if(psr_typ == 0 && psr_vers == 4) {
		int cpunode;
		char node_str[128];

		/* Look if it is not a TurboSparc emulating Swift... */
		cpunode = prom_getchild(prom_root_node);
		while((cpunode = prom_getsibling(cpunode)) != 0) {
			prom_getstring(cpunode, "device_type", node_str, sizeof(node_str));
			if(!strcmp(node_str, "cpu")) {
				if (!prom_getintdefault(cpunode, "psr-implementation", 1) &&
				    prom_getintdefault(cpunode, "psr-version", 1) == 5) {
					init_turbosparc();
					return;
				}
				break;
			}
		}
		
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

static int srmmu_check_pgt_cache(int low, int high)
{
	struct page *page, *page2;
	int freed = 0;

	if (pgtable_cache_size > high) {
		spin_lock(&pte_spinlock);
		for (page2 = NULL, page = (struct page *)pte_quicklist; page;) {
			if ((unsigned int)page->pprev_hash == 0xffff) {
				if (page2)
					page2->next_hash = page->next_hash;
				else
					(struct page *)pte_quicklist = page->next_hash;
				page->next_hash = NULL;
				page->pprev_hash = NULL;
				pgtable_cache_size -= 16;
				__free_page(page);
				freed++;
				if (page2)
					page = page2->next_hash;
				else
					page = (struct page *)pte_quicklist;
				if (pgtable_cache_size <= low)
					break;
				continue;
			}
			page2 = page;
			page = page->next_hash;
		}
		spin_unlock(&pte_spinlock);
	}
	if (pgd_cache_size > high / 4) {
		spin_lock(&pgd_spinlock);
		for (page2 = NULL, page = (struct page *)pgd_quicklist; page;) {
			if ((unsigned int)page->pprev_hash == 0xf) {
				if (page2)
					page2->next_hash = page->next_hash;
				else
					(struct page *)pgd_quicklist = page->next_hash;
				page->next_hash = NULL;
				page->pprev_hash = NULL;
				pgd_cache_size -= 4;
				__free_page(page);
				freed++;
				if (page2)
					page = page2->next_hash;
				else
					page = (struct page *)pgd_quicklist;
				if (pgd_cache_size <= low / 4)
					break;
				continue;
			}
			page2 = page;
			page = page->next_hash;
		}
		spin_unlock(&pgd_spinlock);
	}
	return freed;
}

static void srmmu_flush_dma_area(unsigned long addr, int len)
{
	/* XXX Later */
}

static void srmmu_inval_dma_area(unsigned long addr, int len)
{
	/* XXX Later */
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

static void __init patch_window_trap_handlers(void)
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

#ifdef __SMP__
/* Local cross-calls. */
static void smp_flush_page_for_dma(unsigned long page)
{
	xc1((smpfunc_t) BTFIXUP_CALL(local_flush_page_for_dma), page);
}

#endif

/* Load up routines and constants for sun4m and sun4d mmu */
void __init ld_mmu_srmmu(void)
{
	extern void ld_mmu_iommu(void);
	extern void ld_mmu_iounit(void);
	extern void ___xchg32_sun4md(void);
	
	/* First the constants */
	BTFIXUPSET_SIMM13(pmd_shift, SRMMU_PMD_SHIFT);
	BTFIXUPSET_SETHI(pmd_size, SRMMU_PMD_SIZE);
	BTFIXUPSET_SETHI(pmd_mask, SRMMU_PMD_MASK);
	BTFIXUPSET_SIMM13(pgdir_shift, SRMMU_PGDIR_SHIFT);
	BTFIXUPSET_SETHI(pgdir_size, SRMMU_PGDIR_SIZE);
	BTFIXUPSET_SETHI(pgdir_mask, SRMMU_PGDIR_MASK);

	BTFIXUPSET_SIMM13(ptrs_per_pte, SRMMU_PTRS_PER_PTE);
	BTFIXUPSET_SIMM13(ptrs_per_pmd, SRMMU_PTRS_PER_PMD);
	BTFIXUPSET_SIMM13(ptrs_per_pgd, SRMMU_PTRS_PER_PGD);

	BTFIXUPSET_INT(page_none, pgprot_val(SRMMU_PAGE_NONE));
	BTFIXUPSET_INT(page_shared, pgprot_val(SRMMU_PAGE_SHARED));
	BTFIXUPSET_INT(page_copy, pgprot_val(SRMMU_PAGE_COPY));
	BTFIXUPSET_INT(page_readonly, pgprot_val(SRMMU_PAGE_RDONLY));
	BTFIXUPSET_INT(page_kernel, pgprot_val(SRMMU_PAGE_KERNEL));
	pg_iobits = SRMMU_VALID | SRMMU_WRITE | SRMMU_REF;
	
	/* Functions */
#ifndef __SMP__	
	BTFIXUPSET_CALL(___xchg32, ___xchg32_sun4md, BTFIXUPCALL_SWAPG1G2);
#endif
	BTFIXUPSET_CALL(get_pte_fast, srmmu_get_pte_fast, BTFIXUPCALL_RETINT(0));
	BTFIXUPSET_CALL(get_pgd_fast, srmmu_get_pgd_fast, BTFIXUPCALL_RETINT(0));
	BTFIXUPSET_CALL(free_pte_slow, srmmu_free_pte_slow, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(free_pgd_slow, srmmu_free_pgd_slow, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(do_check_pgt_cache, srmmu_check_pgt_cache, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(set_pgdir, srmmu_set_pgdir, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(set_pte, srmmu_set_pte_cacheable, BTFIXUPCALL_SWAPO0O1);
	BTFIXUPSET_CALL(switch_mm, srmmu_switch_mm, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(pte_pagenr, srmmu_pte_pagenr, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_page, srmmu_pmd_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_page, srmmu_pgd_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_SETHI(none_mask, 0xF0000000);	/* P3: is it used? */

	BTFIXUPSET_CALL(pte_present, srmmu_pte_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_SWAPO0G0);

	BTFIXUPSET_CALL(pmd_bad, srmmu_pmd_bad, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_present, srmmu_pmd_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_SWAPO0G0);

	BTFIXUPSET_CALL(pgd_none, srmmu_pgd_none, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_bad, srmmu_pgd_bad, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_present, srmmu_pgd_present, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_SWAPO0G0);

	BTFIXUPSET_CALL(mk_pte, srmmu_mk_pte, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mk_pte_phys, srmmu_mk_pte_phys, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mk_pte_io, srmmu_mk_pte_io, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_set, srmmu_pgd_set, BTFIXUPCALL_NORM);
	
	BTFIXUPSET_INT(pte_modify_mask, SRMMU_CHG_MASK);
	BTFIXUPSET_CALL(pgd_offset, srmmu_pgd_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_offset, srmmu_pmd_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_offset, srmmu_pte_offset, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_free_kernel, srmmu_pte_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_free_kernel, srmmu_pmd_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_alloc_kernel, srmmu_pte_alloc, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_alloc_kernel, srmmu_pmd_alloc, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_free, srmmu_pte_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pte_alloc, srmmu_pte_alloc, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_free, srmmu_pmd_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_alloc, srmmu_pmd_alloc, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_free, srmmu_pgd_free, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_alloc, srmmu_pgd_alloc, BTFIXUPCALL_NORM);

	BTFIXUPSET_HALF(pte_writei, SRMMU_WRITE);
	BTFIXUPSET_HALF(pte_dirtyi, SRMMU_DIRTY);
	BTFIXUPSET_HALF(pte_youngi, SRMMU_REF);
	BTFIXUPSET_HALF(pte_wrprotecti, SRMMU_WRITE);
	BTFIXUPSET_HALF(pte_mkcleani, SRMMU_DIRTY);
	BTFIXUPSET_HALF(pte_mkoldi, SRMMU_REF);
	BTFIXUPSET_CALL(pte_mkwrite, srmmu_pte_mkwrite, BTFIXUPCALL_ORINT(SRMMU_WRITE));
	BTFIXUPSET_CALL(pte_mkdirty, srmmu_pte_mkdirty, BTFIXUPCALL_ORINT(SRMMU_DIRTY));
	BTFIXUPSET_CALL(pte_mkyoung, srmmu_pte_mkyoung, BTFIXUPCALL_ORINT(SRMMU_REF));
	BTFIXUPSET_CALL(update_mmu_cache, srmmu_update_mmu_cache, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(destroy_context, srmmu_destroy_context, BTFIXUPCALL_NORM);
	
	BTFIXUPSET_CALL(mmu_info, srmmu_mmu_info, BTFIXUPCALL_NORM);

	/* Task struct and kernel stack allocating/freeing. */
	BTFIXUPSET_CALL(alloc_task_struct, srmmu_alloc_task_struct, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(free_task_struct, srmmu_free_task_struct, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(get_task_struct, srmmu_get_task_struct, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(quick_kernel_fault, srmmu_quick_kernel_fault, BTFIXUPCALL_NORM);

	/* SRMMU specific. */
	BTFIXUPSET_CALL(ctxd_set, srmmu_ctxd_set, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_set, srmmu_pmd_set, BTFIXUPCALL_NORM);

/* hmm isn't flush_dma_area the same thing as flush_page_for_dma? */
/* It is, except flush_page_for_dma was local to srmmu.c */
	BTFIXUPSET_CALL(mmu_flush_dma_area, srmmu_flush_dma_area, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_inval_dma_area, srmmu_inval_dma_area, BTFIXUPCALL_NORM);

	get_srmmu_type();
	patch_window_trap_handlers();

#ifdef __SMP__
	/* El switcheroo... */

	BTFIXUPCOPY_CALL(local_flush_cache_all, flush_cache_all);
	BTFIXUPCOPY_CALL(local_flush_cache_mm, flush_cache_mm);
	BTFIXUPCOPY_CALL(local_flush_cache_range, flush_cache_range);
	BTFIXUPCOPY_CALL(local_flush_cache_page, flush_cache_page);
	BTFIXUPCOPY_CALL(local_flush_tlb_all, flush_tlb_all);
	BTFIXUPCOPY_CALL(local_flush_tlb_mm, flush_tlb_mm);
	BTFIXUPCOPY_CALL(local_flush_tlb_range, flush_tlb_range);
	BTFIXUPCOPY_CALL(local_flush_tlb_page, flush_tlb_page);
	BTFIXUPCOPY_CALL(local_flush_page_to_ram, __flush_page_to_ram);
	BTFIXUPCOPY_CALL(local_flush_sig_insns, flush_sig_insns);
	BTFIXUPCOPY_CALL(local_flush_page_for_dma, flush_page_for_dma);

	BTFIXUPSET_CALL(flush_cache_all, smp_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, smp_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, smp_flush_cache_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, smp_flush_cache_page, BTFIXUPCALL_NORM);
	if (sparc_cpu_model != sun4d) {
		BTFIXUPSET_CALL(flush_tlb_all, smp_flush_tlb_all, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_mm, smp_flush_tlb_mm, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_range, smp_flush_tlb_range, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(flush_tlb_page, smp_flush_tlb_page, BTFIXUPCALL_NORM);
	}
	BTFIXUPSET_CALL(__flush_page_to_ram, smp_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, smp_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, smp_flush_page_for_dma, BTFIXUPCALL_NORM);
#endif

	if (sparc_cpu_model == sun4d)
		ld_mmu_iounit();
	else
		ld_mmu_iommu();
#ifdef __SMP__
	if (sparc_cpu_model == sun4d)
		sun4d_init_smp();
	else
		sun4m_init_smp();
#endif
}
