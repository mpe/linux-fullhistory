/*
 *  linux/arch/i386/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/e820.h>

unsigned long highstart_pfn, highend_pfn;
static unsigned long totalram_pages = 0;
static unsigned long totalhigh_pages = 0;

extern void show_net_buffers(void);

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */

/*
 * These are allocated in head.S so that we get proper page alignment.
 * If you change the size of these then change head.S as well.
 */
extern char empty_bad_page[PAGE_SIZE];
#if CONFIG_X86_PAE
extern pmd_t empty_bad_pmd_table[PTRS_PER_PMD];
#endif
extern pte_t empty_bad_pte_table[PTRS_PER_PTE];

/*
 * We init them before every return and make them writable-shared.
 * This guarantees we get out of the kernel in some more or less sane
 * way.
 */
#if CONFIG_X86_PAE
static pmd_t * get_bad_pmd_table(void)
{
	pmd_t v;
	int i;

	pmd_val(v) = _PAGE_TABLE + __pa(empty_bad_pte_table);

	for (i = 0; i < PAGE_SIZE/sizeof(pmd_t); i++)
		empty_bad_pmd_table[i] = v;

	return empty_bad_pmd_table;
}
#endif

static pte_t * get_bad_pte_table(void)
{
	pte_t v;
	int i;

	v = pte_mkdirty(mk_pte_phys(__pa(empty_bad_page), PAGE_SHARED));

	for (i = 0; i < PAGE_SIZE/sizeof(pte_t); i++)
		empty_bad_pte_table[i] = v;

	return empty_bad_pte_table;
}



void __handle_bad_pmd(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_val(*pmd) = _PAGE_TABLE + __pa(get_bad_pte_table());
}

void __handle_bad_pmd_kernel(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
	pmd_val(*pmd) = _KERNPG_TABLE + __pa(get_bad_pte_table());
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page(pte);
			pmd_val(*pmd) = _KERNPG_TABLE + __pa(pte);
			return pte + offset;
		}
		pmd_val(*pmd) = _KERNPG_TABLE + __pa(get_bad_pte_table());
		return NULL;
	}
	free_page((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	unsigned long pte;

	pte = (unsigned long) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page((void *)pte);
			pmd_val(*pmd) = _PAGE_TABLE + __pa(pte);
			return (pte_t *)pte + offset;
		}
		pmd_val(*pmd) = _PAGE_TABLE + __pa(get_bad_pte_table());
		return NULL;
	}
	free_page(pte);
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
	if(pgtable_cache_size > high) {
		do {
			if(pgd_quicklist)
				free_pgd_slow(get_pgd_fast()), freed++;
			if(pmd_quicklist)
				free_pmd_slow(get_pmd_fast()), freed++;
			if(pte_quicklist)
				free_pte_slow(get_pte_fast()), freed++;
		} while(pgtable_cache_size > low);
	}
	return freed;
}

/*
 * NOTE: pagetable_init alloc all the fixmap pagetables contiguous on the
 * physical space so we can cache the place of the first one and move
 * around without checking the pgd every time.
 */

#if CONFIG_HIGHMEM
pte_t *kmap_pte;
pgprot_t kmap_prot;

#define kmap_get_fixmap_pte(vaddr)					\
	pte_offset(pmd_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr))

void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = kmap_get_fixmap_pte(kmap_vstart);

	kmap_prot = PAGE_KERNEL;
	if (boot_cpu_data.x86_capability & X86_FEATURE_PGE)
		pgprot_val(kmap_prot) |= _PAGE_GLOBAL;
}
#endif

void show_mem(void)
{
	int i,free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0;
	int highmem = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageHighMem(mem_map+i))
			highmem++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map+i))
			free++;
		else
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d pages of HIGHMEM\n",highmem);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n",pgtable_cache_size);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

static void set_pte_phys (unsigned long vaddr, unsigned long phys)
{
	pgprot_t prot;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + __pgd_offset(vaddr);
	pmd = pmd_offset(pgd, vaddr);
	pte = pte_offset(pmd, vaddr);
	prot = PAGE_KERNEL;
	if (boot_cpu_data.x86_capability & X86_FEATURE_PGE)
		pgprot_val(prot) |= _PAGE_GLOBAL;
	set_pte(pte, mk_pte_phys(phys, prot));

	/*
	 * It's enough to flush this one mapping.
	 */
	__flush_tlb_one(vaddr);
}

void set_fixmap (enum fixed_addresses idx, unsigned long phys)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		printk("Invalid set_fixmap\n");
		return;
	}
	set_pte_phys (address,phys);
}

static void __init fixrange_init (unsigned long start, unsigned long end, pgd_t *pgd_base)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int i, j;

	i = __pgd_offset(start);
	j = __pmd_offset(start);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (start != end); pgd++, i++) {
#if CONFIG_X86_PAE
		if (pgd_none(*pgd)) {
			pmd = (pmd_t *) alloc_bootmem_low_pages(PAGE_SIZE);
			memset((void*)pmd, 0, PAGE_SIZE);
			pgd_val(*pgd) = __pa(pmd) + 0x1;
			if (pmd != pmd_offset(pgd, start))
				BUG();
		}
		pmd = pmd_offset(pgd, start);
#else
		pmd = (pmd_t *)pgd;
#endif
		for (; (j < PTRS_PER_PMD) && start; pmd++, j++) {
			if (pmd_none(*pmd)) {
				pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
				memset((void*)pte, 0, PAGE_SIZE);
				pmd_val(*pmd) = _KERNPG_TABLE + __pa(pte);
				if (pte != pte_offset(pmd, 0))
					BUG();
			}
			start += PMD_SIZE;
		}
		j = 0;
	}
}

static void __init pagetable_init(void)
{
	pgd_t *pgd, *pgd_base;
	pmd_t *pmd;
	pte_t *pte;
	int i, j, k;
	unsigned long vaddr;
	unsigned long end = (unsigned long)__va(max_low_pfn*PAGE_SIZE);

	pgd_base = swapper_pg_dir;

	vaddr = PAGE_OFFSET;
	i = __pgd_offset(vaddr);
	pgd = pgd_base + i;

	for (; (i < PTRS_PER_PGD) && (vaddr <= end); pgd++, i++) {
		vaddr = i*PGDIR_SIZE;
#if CONFIG_X86_PAE
		pmd = (pmd_t *) alloc_bootmem_low_pages(PAGE_SIZE);
		memset((void*)pmd, 0, PAGE_SIZE);
		pgd_val(*pgd) = __pa(pmd) + 0x1;
#else
		pmd = (pmd_t *)pgd;
#endif
		if (pmd != pmd_offset(pgd, 0))
			BUG();
		for (j = 0; (j < PTRS_PER_PMD) && (vaddr <= end); pmd++, j++) {
			vaddr = i*PGDIR_SIZE + j*PMD_SIZE;
			if (cpu_has_pse) {
				unsigned long __pe;

				set_in_cr4(X86_CR4_PSE);
				boot_cpu_data.wp_works_ok = 1;
				__pe = _KERNPG_TABLE + _PAGE_PSE + __pa(vaddr);
				/* Make it "global" too if supported */
				if (cpu_has_pge) {
					set_in_cr4(X86_CR4_PGE);
					__pe += _PAGE_GLOBAL;
				}
				pmd_val(*pmd) = __pe;
				continue;
			}

			pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
			memset((void*)pte, 0, PAGE_SIZE);
			pmd_val(*pmd) = _KERNPG_TABLE + __pa(pte);

			if (pte != pte_offset(pmd, 0))
				BUG();

			for (k = 0;
				(k < PTRS_PER_PTE) && (vaddr <= end);
					pte++, k++) {
				vaddr = i*PGDIR_SIZE + j*PMD_SIZE + k*PAGE_SIZE;
				*pte = mk_pte_phys(__pa(vaddr), PAGE_KERNEL);
			}
		}
	}

	/*
	 * Fixed mappings, only the page table structure has to be
	 * created - mappings will be set by set_fixmap():
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, 0, pgd_base);

#if CONFIG_HIGHMEM
	/*
	 * Permanent kmaps:
	 */
	vaddr = PKMAP_BASE;
	fixrange_init(vaddr, vaddr + 4*1024*1024, pgd_base);

	pgd = swapper_pg_dir + __pgd_offset(vaddr);
	pmd = pmd_offset(pgd, vaddr);
	pte = pte_offset(pmd, vaddr);
	pkmap_page_table = pte;
#endif

#if CONFIG_X86_PAE
	/*
	 * Add low memory identity-mappings - SMP needs it when
	 * starting up on an AP from real-mode. In the non-PAE
	 * case we already have these mappings through head.S.
	 * All user-space mappings are explicitly cleared after
	 * SMP startup.
	 */
	pgd_base[0] = pgd_base[USER_PTRS_PER_PGD];
#endif
}

void __init zap_low_mappings (void)
{
	int i;
	/*
	 * Zap initial low-memory mappings.
	 *
	 * Note that "pgd_clear()" doesn't do it for
	 * us in this case, because pgd_clear() is a
	 * no-op in the 2-level case (pmd_clear() is
	 * the thing that clears the page-tables in
	 * that case).
	 */
	for (i = 0; i < USER_PTRS_PER_PGD; i++)
		pgd_val(swapper_pg_dir[i]) = 0;
	flush_tlb_all();
}

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
	pagetable_init();

	__asm__( "movl %%ecx,%%cr3\n" ::"c"(__pa(swapper_pg_dir)));

#if CONFIG_X86_PAE
	/*
	 * We will bail out later - printk doesnt work right now so
	 * the user would just see a hanging kernel.
	 */
	if (cpu_has_pae)
		set_in_cr4(X86_CR4_PAE);
#endif

	__flush_tlb();

#ifdef __SMP__
	init_smp_mappings();
#endif

#ifdef CONFIG_HIGHMEM
	kmap_init();
#endif
	{
		unsigned int zones_size[3];

		zones_size[0] = virt_to_phys((char *)MAX_DMA_ADDRESS)
					 >> PAGE_SHIFT;
		zones_size[1] = max_low_pfn - zones_size[0];
		zones_size[2] = highend_pfn - zones_size[0] - zones_size[1];

		free_area_init(zones_size);
	}
	return;
}

/*
 * Test if the WP bit works in supervisor mode. It isn't supported on 386's
 * and also on some strange 486's (NexGen etc.). All 586+'s are OK. The jumps
 * before and after the test are here to work-around some nasty CPU bugs.
 */

void __init test_wp_bit(void)
{
/*
 * Ok, all PAE-capable CPUs are definitely handling the WP bit right.
 */
	const unsigned long vaddr = PAGE_OFFSET;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte, old_pte;
	char tmp_reg;

	printk("Checking if this processor honours the WP bit even in supervisor mode... ");

	pgd = swapper_pg_dir + __pgd_offset(vaddr);
	pmd = pmd_offset(pgd, vaddr);
	pte = pte_offset(pmd, vaddr);
	old_pte = *pte;
	*pte = mk_pte_phys(0, PAGE_READONLY);
	local_flush_tlb();

	__asm__ __volatile__(
		"jmp 1f; 1:\n"
		"movb %0,%1\n"
		"movb %1,%0\n"
		"jmp 1f; 1:\n"
		:"=m" (*(char *) vaddr),
		 "=q" (tmp_reg)
		:/* no inputs */
		:"memory");

	*pte = old_pte;
	local_flush_tlb();

	if (boot_cpu_data.wp_works_ok < 0) {
		boot_cpu_data.wp_works_ok = 0;
		printk("No.\n");
#ifdef CONFIG_X86_WP_WORKS_OK
		panic("This kernel doesn't support CPU's with broken WP. Recompile it for a 386!");
#endif
	} else
		printk(".\n");
}

static inline int page_is_ram (unsigned long pagenr)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		unsigned long addr, size;

		if (e820.map[i].type != E820_RAM)	/* not usable memory */
			continue;
		/*
		 *	!!!FIXME!!! Some BIOSen report areas as RAM that
		 *	are not. Notably the 640->1Mb area. We need a sanity
		 *	check here.
		 */
		addr = (e820.map[i].addr+PAGE_SIZE-1) >> PAGE_SHIFT;
		size = e820.map[i].size >> PAGE_SHIFT;
		if  ((pagenr >= addr) && (pagenr < addr+size))
			return 1;
	}
	return 0;
}

void __init mem_init(void)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	int initpages = 0;
#ifdef CONFIG_HIGHMEM
	int tmp;

	if (!mem_map)
		BUG();
	highmem_start_page = mem_map + highstart_pfn;
	/* cache the highmem_mapnr */
	highmem_mapnr = highstart_pfn;
	max_mapnr = num_physpages = highend_pfn;
#else
	max_mapnr = num_physpages = max_low_pfn;
#endif
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();

#ifdef CONFIG_HIGHMEM
	for (tmp = highstart_pfn; tmp < highend_pfn; tmp++) {
		struct page *page = mem_map + tmp;

		if (!page_is_ram(tmp)) {
			SetPageReserved(page);
			continue;
		}
		ClearPageReserved(page);
		set_bit(PG_highmem, &page->flags);
		atomic_set(&page->count, 1);
		__free_page(page);
		totalhigh_pages++;
	}
	totalram_pages += totalhigh_pages;
#endif
	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init, %ldk highmem)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		max_mapnr << (PAGE_SHIFT-10),
		codepages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10),
		initpages << (PAGE_SHIFT-10),
		(unsigned long) (totalhigh_pages << (PAGE_SHIFT-10))
	       );

#if CONFIG_X86_PAE
	if (!cpu_has_pae)
		panic("cannot execute a PAE-enabled kernel on a PAE-incapable CPU!");
#endif
	if (boot_cpu_data.wp_works_ok < 0)
		test_wp_bit();

	/*
	 * Subtle. SMP is doing it's boot stuff late (because it has to
	 * fork idle threads) - but it also needs low mappings for the
	 * protected-mode entry to work. We zap these entries only after
	 * the WP-bit has been tested.
	 */
#ifndef CONFIG_SMP
	zap_low_mappings();
#endif

}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(mem_map + MAP_NR(addr));
		set_page_count(mem_map+MAP_NR(addr), 1);
		free_page(addr);
		totalram_pages++;
	}
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;
	return;
}
