/* $Id: srmmu.c,v 1.175 1998/08/28 18:57:31 zaitcev Exp $
 * srmmu.c:  SRMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Peter A. Zaitcev (zaitcev@ithil.mcst.ru)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
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
#include <asm/asi.h>
#include <asm/msi.h>
#include <asm/a.out.h>
#include <asm/mmu_context.h>
#include <asm/io-unit.h>
#include <asm/spinlock.h>

/* Now the cpu specific definitions. */
#include <asm/viking.h>
#include <asm/mxcc.h>
#include <asm/ross.h>
#include <asm/tsunami.h>
#include <asm/swift.h>
#include <asm/turbosparc.h>

#include <asm/btfixup.h>

/* #define DEBUG_MAP_KERNEL */
/* #define PAGESKIP_DEBUG */

enum mbus_module srmmu_modtype;
unsigned int hwbug_bitmask;
int vac_cache_size;
int vac_line_size;
int vac_badbits;

extern unsigned long sparc_iobase_vaddr;

#ifdef __SMP__
#define FLUSH_BEGIN(mm)
#define FLUSH_END
#else
#define FLUSH_BEGIN(mm) if((mm)->context != NO_CONTEXT) {
#define FLUSH_END	}
#endif

static int phys_mem_contig;
BTFIXUPDEF_SETHI(page_contig_offset)

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

/* Don't change this without changing access to this
 * in arch/sparc/mm/viking.S
 */
static struct srmmu_trans {
	unsigned long vbase;
	unsigned long pbase;
	unsigned long size;
} srmmu_map[SPARC_PHYS_BANKS];

#define SRMMU_HASHSZ	256

/* Not static, viking.S uses it. */
unsigned long srmmu_v2p_hash[SRMMU_HASHSZ];
static unsigned long srmmu_p2v_hash[SRMMU_HASHSZ];

#define srmmu_ahashfn(addr)	((addr) >> 24)

int viking_mxcc_present = 0;

/* Physical memory can be _very_ non-contiguous on the sun4m, especially
 * the SS10/20 class machines and with the latest openprom revisions.
 * So we have to do a quick lookup.
 * We use the same for SS1000/SC2000 as a fall back, when phys memory is
 * non-contiguous.
 */
static inline unsigned long srmmu_v2p(unsigned long vaddr)
{
	unsigned long off = srmmu_v2p_hash[srmmu_ahashfn(vaddr)];
	
	return (vaddr + off);
}

static inline unsigned long srmmu_p2v(unsigned long paddr)
{
	unsigned long off = srmmu_p2v_hash[srmmu_ahashfn(paddr)];
	
	if (off != 0xffffffffUL)
		return (paddr - off);
	else
		return 0xffffffffUL;
}

/* Physical memory on most SS1000/SC2000 can be contiguous, so we handle that case
 * as a special case to make things faster.
 */
/* FIXME: gcc is stupid here and generates very very bad code in this
 * heavily used routine. So we help it a bit. */
static inline unsigned long srmmu_c_v2p(unsigned long vaddr)
{
#if KERNBASE != 0xf0000000
	if (vaddr >= KERNBASE) return vaddr - KERNBASE;
	return vaddr - BTFIXUP_SETHI(page_contig_offset);
#else
	register unsigned long kernbase;
	
	__asm__ ("sethi %%hi(0xf0000000), %0" : "=r"(kernbase));
	return vaddr - ((vaddr >= kernbase) ? kernbase : BTFIXUP_SETHI(page_contig_offset));
#endif
}

static inline unsigned long srmmu_c_p2v(unsigned long paddr)
{
#if KERNBASE != 0xf0000000
	if (paddr < (0xfd000000 - KERNBASE)) return paddr + KERNBASE;
	return (paddr + BTFIXUP_SETHI(page_contig_offset));
#else
	register unsigned long kernbase;
	register unsigned long limit;
	
	__asm__ ("sethi %%hi(0x0d000000), %0" : "=r"(limit));
	__asm__ ("sethi %%hi(0xf0000000), %0" : "=r"(kernbase));

	return paddr + ((paddr < limit) ? kernbase : BTFIXUP_SETHI(page_contig_offset));
#endif
}

/* On boxes where there is no lots_of_ram, KERNBASE is mapped to PA<0> and highest
   PA is below 0x0d000000, we can optimize even more :) */
static inline unsigned long srmmu_s_v2p(unsigned long vaddr)
{
	return vaddr - PAGE_OFFSET;
}

static inline unsigned long srmmu_s_p2v(unsigned long paddr)
{
	return paddr + PAGE_OFFSET;
}

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

#ifdef PAGESKIP_DEBUG
#define PGSKIP_DEBUG(from,to) prom_printf("PG_skip %ld->%ld\n", (long)(from), (long)(to)); printk("PG_skip %ld->%ld\n", (long)(from), (long)(to))
#else
#define PGSKIP_DEBUG(from,to) do { } while (0)
#endif

__initfunc(void srmmu_frob_mem_map(unsigned long start_mem))
{
	unsigned long bank_start, bank_end = 0;
	unsigned long addr;
	int i;

	/* First, mark all pages as invalid. */
	for(addr = PAGE_OFFSET; MAP_NR(addr) < max_mapnr; addr += PAGE_SIZE)
		mem_map[MAP_NR(addr)].flags |= (1<<PG_reserved);
		
	/* Next, pg[0-3] is sun4c cruft, so we can free it... */
	mem_map[MAP_NR(pg0)].flags &= ~(1<<PG_reserved);
	mem_map[MAP_NR(pg1)].flags &= ~(1<<PG_reserved);
	mem_map[MAP_NR(pg2)].flags &= ~(1<<PG_reserved);
	mem_map[MAP_NR(pg3)].flags &= ~(1<<PG_reserved);

	start_mem = PAGE_ALIGN(start_mem);
	for(i = 0; srmmu_map[i].size; i++) {
		bank_start = srmmu_map[i].vbase;
		
		if (i && bank_start - bank_end > 2 * PAGE_SIZE) {
			mem_map[MAP_NR(bank_end)].flags |= (1<<PG_skip);
			mem_map[MAP_NR(bank_end)].next_hash = mem_map + MAP_NR(bank_start);
			PGSKIP_DEBUG(MAP_NR(bank_end), MAP_NR(bank_start));
			if (bank_end > KERNBASE && bank_start < KERNBASE) {
				mem_map[0].flags |= (1<<PG_skip);
				mem_map[0].next_hash = mem_map + MAP_NR(bank_start);
				PGSKIP_DEBUG(0, MAP_NR(bank_start));
			}
		}
		
		bank_end = bank_start + srmmu_map[i].size;
		while(bank_start < bank_end) {
			if((bank_start >= KERNBASE) &&
			   (bank_start < start_mem)) {
				bank_start += PAGE_SIZE;
				continue;
			}
			mem_map[MAP_NR(bank_start)].flags &= ~(1<<PG_reserved);
			bank_start += PAGE_SIZE;
		}
		
		if (bank_end == 0xfd000000)
			bank_end = PAGE_OFFSET;
	}
	
	if (bank_end < KERNBASE) {
		mem_map[MAP_NR(bank_end)].flags |= (1<<PG_skip);
		mem_map[MAP_NR(bank_end)].next_hash = mem_map + MAP_NR(KERNBASE);
		PGSKIP_DEBUG(MAP_NR(bank_end), MAP_NR(KERNBASE));
	} else if (MAP_NR(bank_end) < max_mapnr) {
		mem_map[MAP_NR(bank_end)].flags |= (1<<PG_skip);
		if (mem_map[0].flags & (1 << PG_skip)) {
			mem_map[MAP_NR(bank_end)].next_hash = mem_map[0].next_hash;
			PGSKIP_DEBUG(MAP_NR(bank_end), mem_map[0].next_hash - mem_map);
		} else {
			mem_map[MAP_NR(bank_end)].next_hash = mem_map;
			PGSKIP_DEBUG(MAP_NR(bank_end), 0);
		}
	}
}

/* The very generic SRMMU page table operations. */
static inline int srmmu_device_memory(unsigned long x) 
{
	return ((x & 0xF0000000) != 0);
}

static unsigned long srmmu_pgd_page(pgd_t pgd)
{ return srmmu_device_memory(pgd_val(pgd))?~0:srmmu_p2v((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pmd_page(pmd_t pmd)
{ return srmmu_device_memory(pmd_val(pmd))?~0:srmmu_p2v((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_pte_page(pte_t pte)
{ return srmmu_device_memory(pte_val(pte))?~0:srmmu_p2v((pte_val(pte) & SRMMU_PTE_PMASK) << 4); }

static unsigned long srmmu_c_pgd_page(pgd_t pgd)
{ return srmmu_device_memory(pgd_val(pgd))?~0:srmmu_c_p2v((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_c_pmd_page(pmd_t pmd)
{ return srmmu_device_memory(pmd_val(pmd))?~0:srmmu_c_p2v((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_c_pte_page(pte_t pte)
{ return srmmu_device_memory(pte_val(pte))?~0:srmmu_c_p2v((pte_val(pte) & SRMMU_PTE_PMASK) << 4); }

static unsigned long srmmu_s_pgd_page(pgd_t pgd)
{ return srmmu_device_memory(pgd_val(pgd))?~0:srmmu_s_p2v((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_s_pmd_page(pmd_t pmd)
{ return srmmu_device_memory(pmd_val(pmd))?~0:srmmu_s_p2v((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4); }

static unsigned long srmmu_s_pte_page(pte_t pte)
{ return srmmu_device_memory(pte_val(pte))?~0:srmmu_s_p2v((pte_val(pte) & SRMMU_PTE_PMASK) << 4); }

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
static pte_t srmmu_mk_pte(unsigned long page, pgprot_t pgprot)
{ return __pte(((srmmu_v2p(page)) >> 4) | pgprot_val(pgprot)); }

static pte_t srmmu_c_mk_pte(unsigned long page, pgprot_t pgprot)
{ return __pte(((srmmu_c_v2p(page)) >> 4) | pgprot_val(pgprot)); }

static pte_t srmmu_s_mk_pte(unsigned long page, pgprot_t pgprot)
{ return __pte(((srmmu_s_v2p(page)) >> 4) | pgprot_val(pgprot)); }

static pte_t srmmu_mk_pte_phys(unsigned long page, pgprot_t pgprot)
{ return __pte(((page) >> 4) | pgprot_val(pgprot)); }

static pte_t srmmu_mk_pte_io(unsigned long page, pgprot_t pgprot, int space)
{
	return __pte(((page) >> 4) | (space << 28) | pgprot_val(pgprot));
}

static void srmmu_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ 
	set_pte((pte_t *)ctxp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pgdp) >> 4)));
}

static void srmmu_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pmdp) >> 4)));
}

static void srmmu_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	set_pte((pte_t *)pmdp, (SRMMU_ET_PTD | (srmmu_v2p((unsigned long) ptep) >> 4)));
}

static void srmmu_c_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ 
	set_pte((pte_t *)ctxp, (SRMMU_ET_PTD | (srmmu_c_v2p((unsigned long) pgdp) >> 4)));
}

static void srmmu_c_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (srmmu_c_v2p((unsigned long) pmdp) >> 4)));
}

static void srmmu_c_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	set_pte((pte_t *)pmdp, (SRMMU_ET_PTD | (srmmu_c_v2p((unsigned long) ptep) >> 4)));
}

static void srmmu_s_ctxd_set(ctxd_t *ctxp, pgd_t *pgdp)
{ 
	set_pte((pte_t *)ctxp, (SRMMU_ET_PTD | (srmmu_s_v2p((unsigned long) pgdp) >> 4)));
}

static void srmmu_s_pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (srmmu_s_v2p((unsigned long) pmdp) >> 4)));
}

static void srmmu_s_pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	set_pte((pte_t *)pmdp, (SRMMU_ET_PTD | (srmmu_s_v2p((unsigned long) ptep) >> 4)));
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

static inline pmd_t *srmmu_c_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) srmmu_c_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

static inline pte_t *srmmu_c_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) srmmu_c_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

static inline pmd_t *srmmu_s_pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) srmmu_s_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

static inline pte_t *srmmu_s_pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) srmmu_s_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

/* This must update the context table entry for this process. */
static void srmmu_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	if(tsk->mm->context != NO_CONTEXT) {
		flush_cache_mm(tsk->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
		flush_tlb_mm(tsk->mm);
	}
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
	die_if_kernel("SRMMU bolixed...", current->tss.kregs);
#endif
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

static inline void free_context(int context)
{
	struct ctx_list *ctx_old;

	ctx_old = ctx_list_pool + context;
	remove_from_ctx_list(ctx_old);
	add_to_free_ctxlist(ctx_old);
}


static void srmmu_switch_to_context(struct task_struct *tsk)
{
	if(tsk->mm->context == NO_CONTEXT) {
		alloc_context(tsk->mm);
		flush_cache_mm(tsk->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], tsk->mm->pgd);
		flush_tlb_mm(tsk->mm);
	}
	srmmu_set_context(tsk->mm->context);
}

static void srmmu_init_new_context(struct mm_struct *mm)
{
	alloc_context(mm);

	flush_cache_mm(mm);
	ctxd_set(&srmmu_context_table[mm->context], mm->pgd);
	flush_tlb_mm(mm);

	if(mm == current->mm)
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
	pgdp = srmmu_pgd_offset(init_task.mm, virt_addr);
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
	flush_page_to_ram(virt_addr);
	set_pte(ptep, __pte(tmp));
	flush_tlb_all();
}

void srmmu_unmapioaddr(unsigned long virt_addr)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	pgdp = srmmu_pgd_offset(init_task.mm, virt_addr);
	pmdp = pmd_offset(pgdp, virt_addr);
	ptep = pte_offset(pmdp, virt_addr);

	/* No need to flush uncacheable page. */
	set_pte(ptep, mk_pte((unsigned long) EMPTY_PGE, PAGE_SHARED));
	flush_tlb_all();
}

/* This is used in many routines below. */
#define UWINMASK_OFFSET (const unsigned long)(&(((struct task_struct *)0)->tss.uwinmask))

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

/* Workaround, until we find what's going on with Swift. When low on memory, it sometimes
 * loops in fault/handle_mm_fault incl. flush_tlb_page to find out it is already in page tables/
 * fault again on the same instruction. I really don't understand it, have checked it and contexts
 * are right, flush_tlb_all is done as well, and it faults again... Strange. -jj
 */
static void swift_update_mmu_cache(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
	static unsigned long last;

	if (last == address) viking_hwprobe(address);
	last = address;
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
	FLUSH_BEGIN(mm)
	flush_user_windows();
	swift_idflash_clear();
	FLUSH_END
}

static void swift_flush_cache_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	FLUSH_BEGIN(mm)
	flush_user_windows();
	swift_idflash_clear();
	FLUSH_END
}

static void swift_flush_cache_page(struct vm_area_struct *vma, unsigned long page)
{
	FLUSH_BEGIN(vma->vm_mm)
	flush_user_windows();
	if(vma->vm_flags & VM_EXEC)
		swift_flush_icache();
	swift_flush_dcache();
	FLUSH_END
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

/* Again, Swift is non-snooping split I/D cache'd just like tsunami,
 * so have to punt the icache for on-stack signal insns.  Only the
 * icache need be flushed since the dcache is write-through.
 */
static void swift_flush_sig_insns(struct mm_struct *mm, unsigned long insn_addr)
{
	swift_flush_icache();
}

static void swift_flush_chunk(unsigned long chunk)
{
}

static void swift_flush_tlb_all(void)
{
	srmmu_flush_whole_tlb();
	module_stats.invall++;
}

static void swift_flush_tlb_mm(struct mm_struct *mm)
{
	FLUSH_BEGIN(mm)
	srmmu_flush_whole_tlb();
	module_stats.invmm++;
	FLUSH_END
}

static void swift_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	FLUSH_BEGIN(mm)
	srmmu_flush_whole_tlb();
	module_stats.invrnge++;
	FLUSH_END
}

static void swift_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	FLUSH_BEGIN(vma->vm_mm)
	srmmu_flush_whole_tlb();
	module_stats.invpg++;
	FLUSH_END
}

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
extern void viking_c_flush_chunk(unsigned long chunk);
extern void viking_s_flush_chunk(unsigned long chunk);
extern void viking_mxcc_flush_chunk(unsigned long chunk);
extern void viking_flush_tlb_all(void);
extern void viking_flush_tlb_mm(struct mm_struct *mm);
extern void viking_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				   unsigned long end);
extern void viking_flush_tlb_page(struct vm_area_struct *vma,
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
	srmmu_set_entry((pte_t *)ctxp, __pte((SRMMU_ET_PTD | (srmmu_v2p((unsigned long) pgdp) >> 4))));
	hypersparc_flush_page_to_ram((unsigned long)ctxp);
	hyper_flush_whole_icache();
}

static void hypersparc_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	unsigned long page = ((unsigned long) pgdp) & PAGE_MASK;

	if(pgdp != swapper_pg_dir)
		hypersparc_flush_page_to_ram(page);

	if(tsk->mm->context != NO_CONTEXT) {
		flush_cache_mm(tsk->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
		flush_tlb_mm(tsk->mm);
	}
}

static void viking_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	viking_flush_page((unsigned long)pgdp);
	if(tsk->mm->context != NO_CONTEXT) {
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
		flush_tlb_mm(current->mm);
	}
}

static void cypress_update_rootmmu_dir(struct task_struct *tsk, pgd_t *pgdp) 
{
	register unsigned long a, b, c, d, e, f, g;
	unsigned long page = ((unsigned long) pgdp) & PAGE_MASK;
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

	if(tsk->mm->context != NO_CONTEXT) {
		flush_cache_mm(current->mm);
		ctxd_set(&srmmu_context_table[tsk->mm->context], pgdp);
		flush_tlb_mm(current->mm);
	}
}

static void hypersparc_switch_to_context(struct task_struct *tsk)
{
	if(tsk->mm->context == NO_CONTEXT) {
		ctxd_t *ctxp;

		alloc_context(tsk->mm);
		ctxp = &srmmu_context_table[tsk->mm->context];
		srmmu_set_entry((pte_t *)ctxp, __pte((SRMMU_ET_PTD | (srmmu_v2p((unsigned long) tsk->mm->pgd) >> 4))));
		hypersparc_flush_page_to_ram((unsigned long)ctxp);
	}
	hyper_flush_whole_icache();
	srmmu_set_context(tsk->mm->context);
}

static void hypersparc_init_new_context(struct mm_struct *mm)
{
	ctxd_t *ctxp;

	alloc_context(mm);

	ctxp = &srmmu_context_table[mm->context];
	srmmu_set_entry((pte_t *)ctxp, __pte((SRMMU_ET_PTD | (srmmu_v2p((unsigned long) mm->pgd) >> 4))));
	hypersparc_flush_page_to_ram((unsigned long)ctxp);

	hyper_flush_whole_icache();
	if(mm == current->mm)
		srmmu_set_context(mm->context);
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
	return ((vaddr - KERNBASE) + kbpage);
}

static inline void srmmu_early_pgd_set(pgd_t *pgdp, pmd_t *pmdp)
{
	set_pte((pte_t *)pgdp, __pte((SRMMU_ET_PTD | (srmmu_early_paddr((unsigned long) pmdp) >> 4))));
}

static inline void srmmu_early_pmd_set(pmd_t *pmdp, pte_t *ptep)
{
	set_pte((pte_t *)pmdp, __pte((SRMMU_ET_PTD | (srmmu_early_paddr((unsigned long) ptep) >> 4))));
}

static inline unsigned long srmmu_early_pgd_page(pgd_t pgd)
{
	return (((pgd_val(pgd) & SRMMU_PTD_PMASK) << 4) - kbpage) + KERNBASE;
}

static inline unsigned long srmmu_early_pmd_page(pmd_t pmd)
{
	return (((pmd_val(pmd) & SRMMU_PTD_PMASK) << 4) - kbpage) + KERNBASE;
}

static inline pmd_t *srmmu_early_pmd_offset(pgd_t *dir, unsigned long address)
{
	return (pmd_t *) srmmu_early_pgd_page(*dir) + ((address >> SRMMU_PMD_SHIFT) & (SRMMU_PTRS_PER_PMD - 1));
}

static inline pte_t *srmmu_early_pte_offset(pmd_t *dir, unsigned long address)
{
	return (pte_t *) srmmu_early_pmd_page(*dir) + ((address >> PAGE_SHIFT) & (SRMMU_PTRS_PER_PTE - 1));
}

static inline void srmmu_allocate_ptable_skeleton(unsigned long start, unsigned long end)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	while(start < end) {
		pgdp = srmmu_pgd_offset(init_task.mm, start);
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = sparc_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
			srmmu_early_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		if(srmmu_pmd_none(*pmdp)) {
			ptep = sparc_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_early_pmd_set(pmdp, ptep);
		}
		start = (start + SRMMU_PMD_SIZE) & SRMMU_PMD_MASK;
	}
}

/* This is much cleaner than poking around physical address space
 * looking at the prom's page table directly which is what most
 * other OS's do.  Yuck... this is much better.
 */
__initfunc(void srmmu_inherit_prom_mappings(unsigned long start,unsigned long end))
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
			*pgdp = __pgd(prompte);
			start += SRMMU_PGDIR_SIZE;
			continue;
		}
		if(srmmu_pgd_none(*pgdp)) {
			pmdp = sparc_init_alloc(&mempool, SRMMU_PMD_TABLE_SIZE);
			srmmu_early_pgd_set(pgdp, pmdp);
		}
		pmdp = srmmu_early_pmd_offset(pgdp, start);
		if(what == 1) {
			*pmdp = __pmd(prompte);
			start += SRMMU_PMD_SIZE;
			continue;
		}
		if(srmmu_pmd_none(*pmdp)) {
			ptep = sparc_init_alloc(&mempool, SRMMU_PTE_TABLE_SIZE);
			srmmu_early_pmd_set(pmdp, ptep);
		}
		ptep = srmmu_early_pte_offset(pmdp, start);
		*ptep = __pte(prompte);
		start += PAGE_SIZE;
	}
}

#ifdef DEBUG_MAP_KERNEL
#define MKTRACE(foo) prom_printf foo
#else
#define MKTRACE(foo)
#endif

static int lots_of_ram __initdata = 0;
static int srmmu_low_pa __initdata = 0;
static unsigned long end_of_phys_memory __initdata = 0;

__initfunc(void srmmu_end_memory(unsigned long memory_size, unsigned long *end_mem_p))
{
	unsigned int sum = 0;
	unsigned long last = 0xff000000;
	long first, cur;
	unsigned long pa;
	unsigned long total = 0;
	int i;

	pa = srmmu_hwprobe(KERNBASE + PAGE_SIZE);
	pa = (pa & SRMMU_PTE_PMASK) << 4;
	if (!sp_banks[0].base_addr && pa == PAGE_SIZE) {
		for(i = 0; sp_banks[i].num_bytes != 0; i++) {
			if (sp_banks[i].base_addr + sp_banks[i].num_bytes > 0x0d000000)
				break;
		}
		if (!sp_banks[i].num_bytes) {
			srmmu_low_pa = 1;
			end_of_phys_memory = SRMMU_PGDIR_ALIGN(sp_banks[i-1].base_addr + sp_banks[i-1].num_bytes);
			*end_mem_p = KERNBASE + end_of_phys_memory;
			if (sp_banks[0].num_bytes >= (6 * 1024 * 1024) || end_of_phys_memory <= 0x06000000) {
				/* Make sure there will be enough memory for the whole mem_map (even if sparse) */
				return;
			}
		}
	}
	for(i = 0; sp_banks[i].num_bytes != 0; i++) {
		pa = sp_banks[i].base_addr;
		first = (pa & (~SRMMU_PGDIR_MASK));
		cur = (sp_banks[i].num_bytes + first - SRMMU_PGDIR_SIZE);
		if (cur < 0) cur = 0;
		if (!first || last != (pa & SRMMU_PGDIR_MASK))
			total += SRMMU_PGDIR_SIZE;
		sum += sp_banks[i].num_bytes;
		if (memory_size) {
			if (sum > memory_size) {
				sp_banks[i].num_bytes -=
					(sum - memory_size);
				cur = (sp_banks[i].num_bytes + first - SRMMU_PGDIR_SIZE);
				if (cur < 0) cur = 0;
				total += SRMMU_PGDIR_ALIGN(cur);
				sum = memory_size;
				sp_banks[++i].base_addr = 0xdeadbeef;
				sp_banks[i].num_bytes = 0;
				break;
			}
		}
		total += SRMMU_PGDIR_ALIGN(cur);
		last = (sp_banks[i].base_addr + sp_banks[i].num_bytes - 1) & SRMMU_PGDIR_MASK;
	}
	if (total <= 0x0d000000)
		*end_mem_p = KERNBASE + total;
	else {
		*end_mem_p = 0xfd000000;
		lots_of_ram = 1;
	}
	end_of_phys_memory = total;
}

#define KERNEL_PTE(page_shifted) ((page_shifted)|SRMMU_CACHE|SRMMU_PRIV|SRMMU_VALID)

/* Create a third-level SRMMU 16MB page mapping. */
__initfunc(static void do_large_mapping(unsigned long vaddr, unsigned long phys_base))
{
	pgd_t *pgdp = srmmu_pgd_offset(init_task.mm, vaddr);
	unsigned long big_pte;

	MKTRACE(("dlm[v<%08lx>-->p<%08lx>]", vaddr, phys_base));
	big_pte = KERNEL_PTE(phys_base >> 4);
	*pgdp = __pgd(big_pte);
}

/* Look in the sp_bank for the given physical page, return the
 * index number the entry was found in, or -1 for not found.
 */
static inline int find_in_spbanks(unsigned long phys_page)
{
	int entry;

	for(entry = 0; sp_banks[entry].num_bytes; entry++) {
		unsigned long start = sp_banks[entry].base_addr;
		unsigned long end = start + sp_banks[entry].num_bytes;

		if((start <= phys_page) && (phys_page < end))
			return entry;
	}
	return -1;
}

/* Find an spbank entry not mapped as of yet, TAKEN_VECTOR is an
 * array of char's, each member indicating if that spbank is mapped
 * yet or not.
 */
__initfunc(static int find_free_spbank(char *taken_vector))
{
	int entry;

	for(entry = 0; sp_banks[entry].num_bytes; entry++)
		if(!taken_vector[entry])
			break;
	return entry;
}

static unsigned long map_spbank_last_pa __initdata = 0xff000000;

/* Map sp_bank entry SP_ENTRY, starting at virtual address VBASE.
 */
__initfunc(static unsigned long map_spbank(unsigned long vbase, int sp_entry))
{
	unsigned long pstart = (sp_banks[sp_entry].base_addr & SRMMU_PGDIR_MASK);
	unsigned long vstart = (vbase & SRMMU_PGDIR_MASK);
	unsigned long vend = SRMMU_PGDIR_ALIGN(vbase + sp_banks[sp_entry].num_bytes);
	static int srmmu_bank = 0;

	MKTRACE(("map_spbank %d[v<%08lx>p<%08lx>s<%08lx>]", sp_entry, vbase, sp_banks[sp_entry].base_addr, sp_banks[sp_entry].num_bytes));
	MKTRACE(("map_spbank2 %d[p%08lx v%08lx-%08lx]", sp_entry, pstart, vstart, vend));
	while(vstart < vend) {
		do_large_mapping(vstart, pstart);
		vstart += SRMMU_PGDIR_SIZE; pstart += SRMMU_PGDIR_SIZE;
	}
	srmmu_map[srmmu_bank].vbase = vbase;
	srmmu_map[srmmu_bank].pbase = sp_banks[sp_entry].base_addr;
	srmmu_map[srmmu_bank].size = sp_banks[sp_entry].num_bytes;
	srmmu_bank++;
	map_spbank_last_pa = pstart - SRMMU_PGDIR_SIZE;
	return vstart;
}

static inline void memprobe_error(char *msg)
{
	prom_printf(msg);
	prom_printf("Halting now...\n");
	prom_halt();
}

/* Assumptions: The bank given to the kernel from the prom/bootloader
 * is part of a full bank which is at least 4MB in size and begins at
 * 0xf0000000 (ie. KERNBASE).
 */
static inline void map_kernel(void)
{
	unsigned long raw_pte, physpage;
	unsigned long vaddr, low_base;
	char etaken[SPARC_PHYS_BANKS];
	int entry;

	/* Step 1: Clear out sp_banks taken map. */
	MKTRACE(("map_kernel: clearing etaken vector... "));
	for(entry = 0; entry < SPARC_PHYS_BANKS; entry++)
		etaken[entry] = 0;

	low_base = KERNBASE;

	/* Step 2: Fill in KERNBASE base pgd.  Lots of sanity checking here. */
	raw_pte = srmmu_hwprobe(KERNBASE + PAGE_SIZE);
	if((raw_pte & SRMMU_ET_MASK) != SRMMU_ET_PTE)
		memprobe_error("Wheee, kernel not mapped at all by boot loader.\n");
	physpage = (raw_pte & SRMMU_PTE_PMASK) << 4;
	physpage -= PAGE_SIZE;
	if(physpage & ~(SRMMU_PGDIR_MASK))
		memprobe_error("Wheee, kernel not mapped on 16MB physical boundry.\n");
	entry = find_in_spbanks(physpage);
	if(entry == -1 || (sp_banks[entry].base_addr != physpage))
		memprobe_error("Kernel mapped in non-existant memory.\n");
	MKTRACE(("map_kernel: map_spbank(vbase=%08x, entry<%d>)[%08lx,%08lx]\n", KERNBASE, entry, sp_banks[entry].base_addr, sp_banks[entry].num_bytes));
	if (sp_banks[entry].num_bytes > 0x0d000000) {
		unsigned long orig_base = sp_banks[entry].base_addr;
		unsigned long orig_len = sp_banks[entry].num_bytes;
		unsigned long can_map = 0x0d000000;
		
		/* Map a partial bank in this case, adjust the base
		 * and the length, but don't mark it used.
		 */
		sp_banks[entry].num_bytes = can_map;
		MKTRACE(("wheee really big mapping [%08lx,%08lx]", orig_base, can_map));
		vaddr = map_spbank(KERNBASE, entry);
		MKTRACE(("vaddr now %08lx ", vaddr));
		sp_banks[entry].base_addr = orig_base + can_map;
		sp_banks[entry].num_bytes = orig_len - can_map;
		MKTRACE(("adjust[%08lx,%08lx]\n", (orig_base + can_map), (orig_len - can_map)));
		MKTRACE(("map_kernel: skipping first loop\n"));
		goto loop_skip;
	}
	vaddr = map_spbank(KERNBASE, entry);
	etaken[entry] = 1;

	/* Step 3: Map what we can above KERNBASE. */
	MKTRACE(("map_kernel: vaddr=%08lx, entering first loop\n", vaddr));
	for(;;) {
		unsigned long bank_size;

		MKTRACE(("map_kernel: ffsp()"));
		entry = find_free_spbank(&etaken[0]);
		bank_size = sp_banks[entry].num_bytes;
		MKTRACE(("<%d> base=%08lx bs=%08lx ", entry, sp_banks[entry].base_addr, bank_size));
		if(!bank_size)
			break;
		if (srmmu_low_pa)
			vaddr = KERNBASE + sp_banks[entry].base_addr;
		else if (sp_banks[entry].base_addr & (~SRMMU_PGDIR_MASK)) {
			if (map_spbank_last_pa == (sp_banks[entry].base_addr & SRMMU_PGDIR_MASK))
				vaddr -= SRMMU_PGDIR_SIZE;
			vaddr += (sp_banks[entry].base_addr & (~SRMMU_PGDIR_MASK));
		}
		if ((vaddr + bank_size - KERNBASE) > 0x0d000000) {
			unsigned long orig_base = sp_banks[entry].base_addr;
			unsigned long orig_len = sp_banks[entry].num_bytes;
			unsigned long can_map = (0xfd000000 - vaddr);

			/* Map a partial bank in this case, adjust the base
			 * and the length, but don't mark it used.
			 */
			sp_banks[entry].num_bytes = can_map;
			MKTRACE(("wheee really big mapping [%08lx,%08lx]", orig_base, can_map));
			vaddr = map_spbank(vaddr, entry);
			MKTRACE(("vaddr now %08lx ", vaddr));
			sp_banks[entry].base_addr = orig_base + can_map;
			sp_banks[entry].num_bytes = orig_len - can_map;
			MKTRACE(("adjust[%08lx,%08lx]\n", (orig_base + can_map), (orig_len - can_map)));
			break;
		}

		/* Ok, we can map this one, do it. */
		MKTRACE(("map_spbank(%08lx,entry<%d>) ", vaddr, entry));
		vaddr = map_spbank(vaddr, entry);
		etaken[entry] = 1;
		MKTRACE(("vaddr now %08lx\n", vaddr));
	}
	MKTRACE(("\n"));
	/* If not lots_of_ram, assume we did indeed map it all above. */
loop_skip:
	if(!lots_of_ram)
		goto check_and_return;
	
	/* Step 4: Map the rest (if any) right below KERNBASE. */
	MKTRACE(("map_kernel: doing low mappings... "));
	low_base = (KERNBASE - end_of_phys_memory + 0x0d000000);
	MKTRACE(("end_of_phys_memory=%08lx low_base=%08lx\n", end_of_phys_memory, low_base));

	/* Ok, now map 'em. */
	MKTRACE(("map_kernel: Allocate pt skeleton (%08lx, %08x)\n",low_base,KERNBASE));
	srmmu_allocate_ptable_skeleton(low_base, KERNBASE);
	vaddr = low_base;
	map_spbank_last_pa = 0xff000000;
	MKTRACE(("map_kernel: vaddr=%08lx Entering second loop for low maps.\n", vaddr));
	for(;;) {
		unsigned long bank_size;

		entry = find_free_spbank(&etaken[0]);
		bank_size = sp_banks[entry].num_bytes;
		MKTRACE(("map_kernel: e<%d> base=%08lx bs=%08lx ", entry, sp_banks[entry].base_addr, bank_size));
		if(!bank_size)
			break;
		if (sp_banks[entry].base_addr & (~SRMMU_PGDIR_MASK)) {
			if (map_spbank_last_pa == (sp_banks[entry].base_addr & SRMMU_PGDIR_MASK))
				vaddr -= SRMMU_PGDIR_SIZE;
			vaddr += (sp_banks[entry].base_addr & (~SRMMU_PGDIR_MASK));
		}
		if((vaddr + bank_size) > KERNBASE)
			memprobe_error("Wheee, kernel low mapping overflow.\n");
		MKTRACE(("map_spbank(%08lx, %d) ", vaddr, entry));
		vaddr = map_spbank(vaddr, entry);
		etaken[entry] = 1;
		MKTRACE(("Now, vaddr=%08lx end_of_phys_memory=%08lx\n", vaddr, end_of_phys_memory));
	}
	MKTRACE(("\n"));

check_and_return:
	/* Step 5: Sanity check, make sure we did it all. */
	MKTRACE(("check_and_return: "));
	for(entry = 0; sp_banks[entry].num_bytes; entry++) {
		MKTRACE(("e[%d]=%d ", entry, etaken[entry]));
		if(!etaken[entry]) {
			MKTRACE(("oops\n"));
			memprobe_error("Some bank did not get mapped.\n");
		}
	}
	MKTRACE(("success\n"));
	init_task.mm->mmap->vm_start = page_offset = low_base;
	stack_top = page_offset - PAGE_SIZE;
	BTFIXUPSET_SETHI(page_offset, low_base);
	BTFIXUPSET_SETHI(stack_top, page_offset - PAGE_SIZE);
	BTFIXUPSET_SIMM13(user_ptrs_per_pgd, page_offset / SRMMU_PGDIR_SIZE);
	
#if 1
	for(entry = 0; srmmu_map[entry].size; entry++) {
		printk("[%d]: v[%08lx,%08lx](%lx) p[%08lx]\n", entry,
		       srmmu_map[entry].vbase,
		       srmmu_map[entry].vbase + srmmu_map[entry].size,
		       srmmu_map[entry].size,
		       srmmu_map[entry].pbase);
	}
#endif

	/* Now setup the p2v/v2p hash tables. */
	for(entry = 0; entry < SRMMU_HASHSZ; entry++)
		srmmu_v2p_hash[entry] = ((0xff - entry) << 24);
	for(entry = 0; entry < SRMMU_HASHSZ; entry++)
		srmmu_p2v_hash[entry] = 0xffffffffUL;
	for(entry = 0; srmmu_map[entry].size; entry++) {
		unsigned long addr;

		for(addr = srmmu_map[entry].vbase;
		    addr < (srmmu_map[entry].vbase + srmmu_map[entry].size);
		    addr += (1 << 24))
			srmmu_v2p_hash[srmmu_ahashfn(addr)] =
				srmmu_map[entry].pbase - srmmu_map[entry].vbase;
		for(addr = srmmu_map[entry].pbase;
		    addr < (srmmu_map[entry].pbase + srmmu_map[entry].size);
		    addr += (1 << 24))
			srmmu_p2v_hash[srmmu_ahashfn(addr)] = 
				srmmu_map[entry].pbase - srmmu_map[entry].vbase;
	}

	BTFIXUPSET_SETHI(page_contig_offset, page_offset - (0xfd000000 - KERNBASE));
	if (srmmu_low_pa)
		phys_mem_contig = 0;
	else {
		phys_mem_contig = 1;
		for(entry = 0; srmmu_map[entry].size; entry++)
			if (srmmu_map[entry].pbase != srmmu_c_v2p (srmmu_map[entry].vbase)) {
				phys_mem_contig = 0;
				break;
			}
	}
	if (phys_mem_contig) {
		printk ("SRMMU: Physical memory is contiguous, bypassing VA<->PA hashes.\n");
		BTFIXUPSET_CALL(pte_page, srmmu_c_pte_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_page, srmmu_c_pmd_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pgd_page, srmmu_c_pgd_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mk_pte, srmmu_c_mk_pte, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pte_offset, srmmu_c_pte_offset, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_offset, srmmu_c_pmd_offset, BTFIXUPCALL_NORM);
		if (BTFIXUPVAL_CALL(ctxd_set) == (unsigned long)srmmu_ctxd_set)
			BTFIXUPSET_CALL(ctxd_set, srmmu_c_ctxd_set, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pgd_set, srmmu_c_pgd_set, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_set, srmmu_c_pmd_set, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mmu_v2p, srmmu_c_v2p, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mmu_p2v, srmmu_c_p2v, BTFIXUPCALL_NORM);
		if (BTFIXUPVAL_CALL(flush_chunk) == (unsigned long)viking_flush_chunk)
			BTFIXUPSET_CALL(flush_chunk, viking_c_flush_chunk, BTFIXUPCALL_NORM);
	} else if (srmmu_low_pa) {
		printk ("SRMMU: Compact physical memory. Using strightforward VA<->PA translations.\n");
		BTFIXUPSET_CALL(pte_page, srmmu_s_pte_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_page, srmmu_s_pmd_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pgd_page, srmmu_s_pgd_page, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mk_pte, srmmu_s_mk_pte, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pte_offset, srmmu_s_pte_offset, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_offset, srmmu_s_pmd_offset, BTFIXUPCALL_NORM);
		if (BTFIXUPVAL_CALL(ctxd_set) == (unsigned long)srmmu_ctxd_set)
			BTFIXUPSET_CALL(ctxd_set, srmmu_s_ctxd_set, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pgd_set, srmmu_s_pgd_set, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_set, srmmu_s_pmd_set, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mmu_v2p, srmmu_s_v2p, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(mmu_p2v, srmmu_s_p2v, BTFIXUPCALL_NORM);
		if (BTFIXUPVAL_CALL(flush_chunk) == (unsigned long)viking_flush_chunk)
			BTFIXUPSET_CALL(flush_chunk, viking_s_flush_chunk, BTFIXUPCALL_NORM);
	}
	btfixup();

	return; /* SUCCESS! */
}

/* Paging initialization on the Sparc Reference MMU. */
extern unsigned long free_area_init(unsigned long, unsigned long);
extern unsigned long sparc_context_init(unsigned long, int);

extern int physmem_mapped_contig;
extern int linux_num_cpus;

void (*poke_srmmu)(void) __initdata = NULL;

__initfunc(unsigned long srmmu_paging_init(unsigned long start_mem, unsigned long end_mem))
{
	unsigned long ptables_start;
	int i, cpunode;
	char node_str[128];

	sparc_iobase_vaddr = 0xfd000000;    /* 16MB of IOSPACE on all sun4m's. */
	physmem_mapped_contig = 0;	    /* for init.c:taint_real_pages()   */

	if (sparc_cpu_model == sun4d)
		num_contexts = 65536; /* We know it is Viking */
	else {
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
	}

	if(!num_contexts) {
		prom_printf("Something wrong, can't find cpu node in paging_init.\n");
		prom_halt();
	}
		
	ptables_start = mempool = PAGE_ALIGN(start_mem);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	kbpage = srmmu_hwprobe(KERNBASE + PAGE_SIZE);
	kbpage = (kbpage & SRMMU_PTE_PMASK) << 4;
	kbpage -= PAGE_SIZE;

	srmmu_allocate_ptable_skeleton(KERNBASE, end_mem);
#if CONFIG_SUN_IO
	srmmu_allocate_ptable_skeleton(sparc_iobase_vaddr, IOBASE_END);
	srmmu_allocate_ptable_skeleton(DVMA_VADDR, DVMA_END);
#endif

	mempool = PAGE_ALIGN(mempool);
        srmmu_inherit_prom_mappings(0xfe400000,(LINUX_OPPROM_ENDVM-PAGE_SIZE));
	map_kernel();
	srmmu_context_table = sparc_init_alloc(&mempool, num_contexts*sizeof(ctxd_t));
	srmmu_ctx_table_phys = (ctxd_t *) srmmu_v2p((unsigned long) srmmu_context_table);
	for(i = 0; i < num_contexts; i++)
		ctxd_set(&srmmu_context_table[i], swapper_pg_dir);

	start_mem = PAGE_ALIGN(mempool);

	flush_cache_all();
	if(BTFIXUPVAL_CALL(flush_page_for_dma) == (unsigned long)viking_flush_page) {
		unsigned long start = ptables_start;
		unsigned long end = start_mem;

		while(start < end) {
			viking_flush_page(start);
			start += PAGE_SIZE;
		}
	}
	srmmu_set_ctable_ptr((unsigned long) srmmu_ctx_table_phys);
	flush_tlb_all();
	poke_srmmu();

	start_mem = sparc_context_init(start_mem, num_contexts);
	start_mem = free_area_init(start_mem, end_mem);

	return PAGE_ALIGN(start_mem);
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
	if(mm->context != NO_CONTEXT && atomic_read(&mm->count) == 1) {
		flush_cache_mm(mm);
		ctxd_set(&srmmu_context_table[mm->context], swapper_pg_dir);
		flush_tlb_mm(mm);
		free_context(mm->context);
		mm->context = NO_CONTEXT;
	}
}

static void srmmu_vac_update_mmu_cache(struct vm_area_struct * vma,
				       unsigned long address, pte_t pte)
{
	if((vma->vm_flags & (VM_WRITE|VM_SHARED)) == (VM_WRITE|VM_SHARED)) {
		struct vm_area_struct *vmaring;
		struct file *file;
		struct inode *inode;
		unsigned long flags, offset, vaddr, start;
		int alias_found = 0;
		pgd_t *pgdp;
		pmd_t *pmdp;
		pte_t *ptep;

		__save_and_cli(flags);

		file = vma->vm_file;
		if (!file)
			goto done;
		inode = file->f_dentry->d_inode;
		offset = (address & PAGE_MASK) - vma->vm_start;
		vmaring = inode->i_mmap; 
		do {
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
#if 1
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

		if(alias_found && !(pte_val(pte) & _SUN4C_PAGE_NOCACHE)) {
			pgdp = srmmu_pgd_offset(vma->vm_mm, address);
			ptep = srmmu_pte_offset((pmd_t *) pgdp, address);
			flush_cache_page(vma, address);
			*ptep = __pte(pte_val(*ptep) | _SUN4C_PAGE_NOCACHE);
			flush_tlb_page(vma, address);
		}
	done:
		__restore_flags(flags);
	}
}

static void hypersparc_destroy_context(struct mm_struct *mm)
{
	if(mm->context != NO_CONTEXT && atomic_read(&mm->count) == 1) {
		ctxd_t *ctxp;

		/* HyperSparc is copy-back, any data for this
		 * process in a modified cache line is stale
		 * and must be written back to main memory now
		 * else we eat shit later big time.
		 */
		flush_cache_mm(mm);

		ctxp = &srmmu_context_table[mm->context];
		srmmu_set_entry((pte_t *)ctxp, __pte((SRMMU_ET_PTD | (srmmu_v2p((unsigned long) swapper_pg_dir) >> 4))));
		hypersparc_flush_page_to_ram((unsigned long)ctxp);

		flush_tlb_mm(mm);
		free_context(mm->context);
		mm->context = NO_CONTEXT;
	}
}

/* Init various srmmu chip types. */
__initfunc(static void srmmu_is_bad(void))
{
	prom_printf("Could not determine SRMMU chip type.\n");
	prom_halt();
}

__initfunc(static void init_vac_layout(void))
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

__initfunc(static void poke_hypersparc(void))
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

__initfunc(static void init_hypersparc(void))
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

	BTFIXUPSET_CALL(flush_page_to_ram, hypersparc_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, hypersparc_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, hypersparc_flush_page_for_dma, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(flush_chunk, hypersparc_flush_chunk, BTFIXUPCALL_NORM); /* local flush _only_ */

	BTFIXUPSET_CALL(ctxd_set, hypersparc_ctxd_set, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(switch_to_context, hypersparc_switch_to_context, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(init_new_context, hypersparc_init_new_context, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(destroy_context, hypersparc_destroy_context, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(update_mmu_cache, srmmu_vac_update_mmu_cache, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(sparc_update_rootmmu_dir, hypersparc_update_rootmmu_dir, BTFIXUPCALL_NORM);
	poke_srmmu = poke_hypersparc;

	hypersparc_setup_blockops();
}

__initfunc(static void poke_cypress(void))
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

__initfunc(static void init_cypress_common(void))
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

	BTFIXUPSET_CALL(flush_page_to_ram, cypress_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_sig_insns, cypress_flush_sig_insns, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_page_for_dma, cypress_flush_page_for_dma, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(sparc_update_rootmmu_dir, cypress_update_rootmmu_dir, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(update_mmu_cache, srmmu_vac_update_mmu_cache, BTFIXUPCALL_NORM);
	poke_srmmu = poke_cypress;
}

__initfunc(static void init_cypress_604(void))
{
	srmmu_name = "ROSS Cypress-604(UP)";
	srmmu_modtype = Cypress;
	init_cypress_common();
}

__initfunc(static void init_cypress_605(unsigned long mrev))
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

__initfunc(static void poke_swift(void))
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
__initfunc(static void init_swift(void))
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

	BTFIXUPSET_CALL(flush_chunk, swift_flush_chunk, BTFIXUPCALL_NOP); /* local flush _only_ */

	BTFIXUPSET_CALL(flush_tlb_all, swift_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, swift_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, swift_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, swift_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_page_to_ram, swift_flush_page_to_ram, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, swift_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, swift_flush_page_for_dma, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(update_mmu_cache, swift_update_mmu_cache, BTFIXUPCALL_NORM);

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


__initfunc(static void poke_turbosparc(void))
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

__initfunc(static void init_turbosparc(void))
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

	BTFIXUPSET_CALL(flush_page_to_ram, turbosparc_flush_page_to_ram, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_chunk, turbosparc_flush_chunk, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_sig_insns, turbosparc_flush_sig_insns, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_page_for_dma, turbosparc_flush_page_for_dma, BTFIXUPCALL_NOP);

	poke_srmmu = poke_turbosparc;
}

__initfunc(static void poke_tsunami(void))
{
	unsigned long mreg = srmmu_get_mmureg();

	tsunami_flush_icache();
	tsunami_flush_dcache();
	mreg &= ~TSUNAMI_ITD;
	mreg |= (TSUNAMI_IENAB | TSUNAMI_DENAB);
	srmmu_set_mmureg(mreg);
}

__initfunc(static void init_tsunami(void))
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

	BTFIXUPSET_CALL(flush_chunk, tsunami_flush_chunk, BTFIXUPCALL_NOP); /* local flush _only_ */

	BTFIXUPSET_CALL(flush_tlb_all, tsunami_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, tsunami_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, tsunami_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, tsunami_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_page_to_ram, tsunami_flush_page_to_ram, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, tsunami_flush_sig_insns, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_for_dma, tsunami_flush_page_for_dma, BTFIXUPCALL_NORM);

	poke_srmmu = poke_tsunami;
}

__initfunc(static void poke_viking(void))
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
	BTFIXUPCOPY_CALL(flush_page_to_ram, local_flush_page_to_ram);
	BTFIXUPCOPY_CALL(flush_sig_insns, local_flush_sig_insns);
	BTFIXUPCOPY_CALL(flush_page_for_dma, local_flush_page_for_dma);
	btfixup();
#endif
}

__initfunc(static void init_viking(void))
{
	unsigned long mreg = srmmu_get_mmureg();

	/* Ahhh, the viking.  SRMMU VLSI abortion number two... */
	if(mreg & VIKING_MMODE) {
		unsigned long bpreg;

		srmmu_name = "TI Viking";
		viking_mxcc_present = 0;

		bpreg = viking_get_bpreg();
		bpreg &= ~(VIKING_ACTION_MIX);
		viking_set_bpreg(bpreg);

		msi_set_sync();

		BTFIXUPSET_CALL(set_pte, srmmu_set_pte_nocache_viking, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pte_clear, srmmu_pte_clear, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pmd_clear, srmmu_pmd_clear, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(pgd_clear, srmmu_pgd_clear, BTFIXUPCALL_NORM);
		BTFIXUPSET_CALL(sparc_update_rootmmu_dir, viking_update_rootmmu_dir, BTFIXUPCALL_NORM);

		BTFIXUPSET_CALL(flush_chunk, viking_flush_chunk, BTFIXUPCALL_NORM); /* local flush _only_ */

		/* We need this to make sure old viking takes no hits
		 * on it's cache for dma snoops to workaround the
		 * "load from non-cacheable memory" interrupt bug.
		 * This is only necessary because of the new way in
		 * which we use the IOMMU.
		 */
		BTFIXUPSET_CALL(flush_page_for_dma, viking_flush_page, BTFIXUPCALL_NORM);
		/* Also, this is so far the only chip which actually uses
		   the page argument to flush_page_for_dma */
		flush_page_for_dma_global = 0;
	} else {
		srmmu_name = "TI Viking/MXCC";
		viking_mxcc_present = 1;

		BTFIXUPSET_CALL(flush_chunk, viking_mxcc_flush_chunk, BTFIXUPCALL_NOP); /* local flush _only_ */

		/* MXCC vikings lack the DMA snooping bug. */
		BTFIXUPSET_CALL(flush_page_for_dma, viking_flush_page_for_dma, BTFIXUPCALL_NOP);
	}

	/* flush_cache_* are nops */
	BTFIXUPSET_CALL(flush_cache_all, viking_flush_cache_all, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_cache_mm, viking_flush_cache_mm, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_cache_page, viking_flush_cache_page, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_cache_range, viking_flush_cache_range, BTFIXUPCALL_NOP);

	BTFIXUPSET_CALL(flush_tlb_all, viking_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, viking_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, viking_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, viking_flush_tlb_range, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(flush_page_to_ram, viking_flush_page_to_ram, BTFIXUPCALL_NOP);
	BTFIXUPSET_CALL(flush_sig_insns, viking_flush_sig_insns, BTFIXUPCALL_NOP);

	poke_srmmu = poke_viking;
}

/* Probe for the srmmu chip version. */
__initfunc(static void get_srmmu_type(void))
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

#ifdef __SMP__
/* Local cross-calls. */
static void smp_flush_page_for_dma(unsigned long page)
{
	xc1((smpfunc_t) BTFIXUP_CALL(local_flush_page_for_dma), page);
}

#endif

/* Load up routines and constants for sun4m and sun4d mmu */
__initfunc(void ld_mmu_srmmu(void))
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
	BTFIXUPSET_CALL(init_new_context, srmmu_init_new_context, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(switch_to_context, srmmu_switch_to_context, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(pte_page, srmmu_pte_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_page, srmmu_pmd_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pgd_page, srmmu_pgd_page, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(sparc_update_rootmmu_dir, srmmu_update_rootmmu_dir, BTFIXUPCALL_NORM);

	BTFIXUPSET_SETHI(none_mask, 0xF0000000);
	
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
	BTFIXUPSET_CALL(mmu_v2p, srmmu_v2p, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_p2v, srmmu_p2v, BTFIXUPCALL_NORM);

	/* Task struct and kernel stack allocating/freeing. */
	BTFIXUPSET_CALL(alloc_task_struct, srmmu_alloc_task_struct, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(free_task_struct, srmmu_free_task_struct, BTFIXUPCALL_NORM);

	BTFIXUPSET_CALL(quick_kernel_fault, srmmu_quick_kernel_fault, BTFIXUPCALL_NORM);

	/* SRMMU specific. */
	BTFIXUPSET_CALL(ctxd_set, srmmu_ctxd_set, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(pmd_set, srmmu_pmd_set, BTFIXUPCALL_NORM);

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
	BTFIXUPCOPY_CALL(local_flush_page_to_ram, flush_page_to_ram);
	BTFIXUPCOPY_CALL(local_flush_sig_insns, flush_sig_insns);
	BTFIXUPCOPY_CALL(local_flush_page_for_dma, flush_page_for_dma);

	BTFIXUPSET_CALL(flush_cache_all, smp_flush_cache_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_mm, smp_flush_cache_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_range, smp_flush_cache_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_cache_page, smp_flush_cache_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_all, smp_flush_tlb_all, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_mm, smp_flush_tlb_mm, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_range, smp_flush_tlb_range, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_tlb_page, smp_flush_tlb_page, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(flush_page_to_ram, smp_flush_page_to_ram, BTFIXUPCALL_NORM);
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
