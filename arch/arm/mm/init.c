/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995-2000 Russell King
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
#include <linux/swapctl.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include "map.h"

#ifdef CONFIG_CPU_32
#define TABLE_OFFSET	(PTRS_PER_PTE)
#else
#define TABLE_OFFSET	0
#endif
#define TABLE_SIZE	((TABLE_OFFSET + PTRS_PER_PTE) * sizeof(void *))

static unsigned long totalram_pages;
pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern int _stext, _text, _etext, _edata, _end;

/*
 * The sole use of this is to pass memory configuration
 * data from paging_init to mem_init.
 */
static struct meminfo __initdata meminfo;

/*
 * empty_bad_page is the page that is used for page faults when
 * linux is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * empty_bad_pte_table is the accompanying page-table: it is
 * initialized to point to BAD_PAGE entries.
 *
 * empty_zero_page is a special page that is used for
 * zero-initialized data and COW.
 */
struct page *empty_zero_page;
struct page *empty_bad_page;
pte_t *empty_bad_pte_table;

pte_t *get_bad_pte_table(void)
{
	pte_t v;
	int i;

	v = pte_mkdirty(mk_pte(empty_bad_page, PAGE_SHARED));

	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(empty_bad_pte_table + i, v);

	return empty_bad_pte_table;
}

void __handle_bad_pmd(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
#ifdef CONFIG_DEBUG_ERRORS
	__backtrace();
#endif
	set_pmd(pmd, mk_user_pmd(get_bad_pte_table()));
}

void __handle_bad_pmd_kernel(pmd_t *pmd)
{
	pmd_ERROR(*pmd);
#ifdef CONFIG_DEBUG_ERRORS
	__backtrace();
#endif
	set_pmd(pmd, mk_kernel_pmd(get_bad_pte_table()));
}

#ifndef CONFIG_NO_PGT_CACHE
struct pgtable_cache_struct quicklists;

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;

	if(pgtable_cache_size > high) {
		do {
			if(pgd_quicklist) {
				free_pgd_slow(get_pgd_fast());
				freed++;
			}
			if(pmd_quicklist) {
				free_pmd_slow(get_pmd_fast());
				freed++;
			}
			if(pte_quicklist) {
				free_pte_slow(get_pte_fast());
				 freed++;
			}
		} while(pgtable_cache_size > low);
	}
	return freed;
}
#else
int do_check_pgt_cache(int low, int high)
{
	return 0;
}
#endif

void show_mem(void)
{
	int free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0, node;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));

	for (node = 0; node < numnodes; node++) {
		struct page *page, *end;

		page = NODE_MEM_MAP(node);
		end  = page + NODE_DATA(node)->node_size;

		do {
			if (PageSkip(page)) {
				page = page->next_hash;
				if (page == NULL)
					break;
			}
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (!page_count(page))
				free++;
			else
				shared += atomic_read(&page->count) - 1;
			page++;
		} while (page < end);
	}

	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
#ifndef CONFIG_NO_PGT_CACHE
	printk("%ld page tables cached\n", pgtable_cache_size);
#endif
	show_buffers();
}

#define O_PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define V_PFN_DOWN(x)	O_PFN_DOWN(__pa(x))

#define O_PFN_UP(x)	(PAGE_ALIGN(x) >> PAGE_SHIFT)
#define V_PFN_UP(x)	O_PFN_UP(__pa(x))

#define PFN_SIZE(x)	((x) >> PAGE_SHIFT)
#define PFN_RANGE(s,e)	PFN_SIZE(PAGE_ALIGN((unsigned long)(e)) - \
				(((unsigned long)(s)) & PAGE_MASK))

static unsigned int __init
find_bootmap_pfn(struct meminfo *mi, unsigned int bootmap_pages)
{
	unsigned int start_pfn, bank, bootmap_pfn;

	start_pfn   = V_PFN_UP(&_end);
	bootmap_pfn = 0;

	/*
	 * FIXME: We really want to avoid allocating the bootmap
	 * over the top of the initrd.
	 */
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		if (__pa(initrd_end) > mi->end) {
			printk ("initrd extends beyond end of memory "
				"(0x%08lx > 0x%08lx) - disabling initrd\n",
				__pa(initrd_end), mi->end);
			initrd_start = 0;
			initrd_end   = 0;
		}
	}
#endif

	for (bank = 0; bank < mi->nr_banks; bank ++) {
		unsigned int start, end;

		if (mi->bank[bank].size == 0)
			continue;

		start = O_PFN_UP(mi->bank[bank].start);
		end   = O_PFN_DOWN(mi->bank[bank].size +
				   mi->bank[bank].start);

		if (end < start_pfn)
			continue;

		if (start < start_pfn)
			start = start_pfn;

		if (end <= start)
			continue;

		if (end - start >= bootmap_pages) {
			bootmap_pfn = start;
			break;
		}
	}

	if (bootmap_pfn == 0)
		BUG();

	return bootmap_pfn;
}

/*
 * Initialise one node of the bootmem allocator.  For now, we
 * only initialise node 0.  Notice that we have a bootmem
 * bitmap per node.
 */
static void __init setup_bootmem_node(int node, struct meminfo *mi)
{
	unsigned int end_pfn, start_pfn, bootmap_pages, bootmap_pfn;
	unsigned int i;

	if (node != 0)	/* only initialise node 0 for now */
		return;

	start_pfn     = O_PFN_UP(PHYS_OFFSET);
	end_pfn	      = O_PFN_DOWN(mi->end);
	bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
	bootmap_pfn   = find_bootmap_pfn(mi, bootmap_pages);

	/*
	 * Initialise the boot-time allocator
	 */
	init_bootmem_node(node, bootmap_pfn, start_pfn, end_pfn);

	/*
	 * Register all available RAM with the bootmem allocator.
	 */
	for (i = 0; i < mi->nr_banks; i++)
		if (mi->bank[i].size)
			free_bootmem_node(node, mi->bank[i].start,
					  PFN_SIZE(mi->bank[i].size) << PAGE_SHIFT);

	reserve_bootmem_node(node, bootmap_pfn << PAGE_SHIFT,
			     bootmap_pages << PAGE_SHIFT);
}

/*
 * Initialise the bootmem allocator.
 */
void __init bootmem_init(struct meminfo *mi)
{
	unsigned int i, node;

	/*
	 * Calculate the  physical address of the top of memory.
	 * Note that there are no guarantees assumed about the
	 * ordering of the bank information.
	 */
	mi->end = 0;
	for (i = 0; i < mi->nr_banks; i++) {
		unsigned long end;

		if (mi->bank[i].size != 0) {
			end = mi->bank[i].start + mi->bank[i].size;
			if (mi->end < end)
				mi->end = end;
		}
	}

	max_low_pfn = O_PFN_DOWN(mi->end - PHYS_OFFSET);

	/*
	 * Setup each node
	 */
	for (node = 0; node < numnodes; node++)
		setup_bootmem_node(node, mi);

	/*
	 * Register the kernel text and data with bootmem.
	 * Note that this can only be in node 0.
	 */
	reserve_bootmem_node(0, V_PFN_DOWN(&_stext) << PAGE_SHIFT,
			     PFN_RANGE(&_stext, &_end) << PAGE_SHIFT);

#ifdef CONFIG_CPU_32
	/*
	 * Reserve the page tables.  These are already in use,
	 * and can only be in node 0.
	 */
	reserve_bootmem_node(0, V_PFN_DOWN(swapper_pg_dir) << PAGE_SHIFT,
			     PFN_SIZE(PTRS_PER_PGD * sizeof(void *)) << PAGE_SHIFT);
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	/*
	 * This may be in any bank.  Currently, we assume that
	 * it is in bank 0.
	 */
	if (initrd_start)
		reserve_bootmem_node(0, V_PFN_DOWN(initrd_start) << PAGE_SHIFT,
				     PFN_RANGE(initrd_start, initrd_end) << PAGE_SHIFT);
#endif
}

/*
 * paging_init() sets up the page tables...
 */
void __init paging_init(struct meminfo *mi)
{
	void *zero_page, *bad_page, *bad_table;
	int node;

	memcpy(&meminfo, mi, sizeof(meminfo));

	/*
	 * allocate what we need for the bad pages
	 */
	zero_page = alloc_bootmem_low_pages(PAGE_SIZE);
	bad_page  = alloc_bootmem_low_pages(PAGE_SIZE);
	bad_table = alloc_bootmem_low_pages(TABLE_SIZE);

	/*
	 * initialise the page tables
	 */
	pagetable_init(mi);
	flush_tlb_all();

	/*
	 * initialise the zones within each node
	 */
	for (node = 0; node < numnodes; node++) {
		unsigned long zone_size[MAX_NR_ZONES];
		unsigned long zhole_size[MAX_NR_ZONES];
		struct bootmem_data *bdata;
		pg_data_t *pgdat;
		int i;

		/*
		 * Initialise the zone size information.
		 */
		for (i = 0; i < MAX_NR_ZONES; i++) {
			zone_size[i]  = 0;
			zhole_size[i] = 0;
		}

		pgdat = NODE_DATA(node);
		bdata = pgdat->bdata;

		/*
		 * The size of this node has already been determined.
		 * If we need to do anything fancy with the allocation
		 * of this memory to the zones, now is the time to do
		 * it.  For now, we don't touch zhole_size.
		 */
		zone_size[0] = bdata->node_low_pfn -
				(bdata->node_boot_start >> PAGE_SHIFT);

		free_area_init_node(node, pgdat, zone_size,
				bdata->node_boot_start, zhole_size);
	}

	/*
	 * finish off the bad pages once
	 * the mem_map is initialised
	 */
	memzero(zero_page, PAGE_SIZE);
	memzero(bad_page, PAGE_SIZE);

	empty_zero_page = mem_map + MAP_NR(zero_page);
	empty_bad_page  = mem_map + MAP_NR(bad_page);
	empty_bad_pte_table = ((pte_t *)bad_table) + TABLE_OFFSET;
}

static inline void free_unused_mem_map(void)
{
	struct page *page, *end;

	end = mem_map + max_mapnr;

	for (page = mem_map; page < end; page++) {
		unsigned long low, high;

		if (!PageSkip(page))
			continue;

		low = PAGE_ALIGN((unsigned long)(page + 1));
		if (page->next_hash < page)
			high = ((unsigned long)end) & PAGE_MASK;
		else
			high = ((unsigned long)page->next_hash) & PAGE_MASK;

		while (low < high) {
			ClearPageReserved(mem_map + MAP_NR(low));
			low += PAGE_SIZE;
		}
	}
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(void)
{
	extern char __init_begin, __init_end;
	unsigned int codepages, datapages, initpages;
	int i, node;

	codepages = &_etext - &_text;
	datapages = &_end - &_etext;
	initpages = &__init_end - &__init_begin;

	high_memory = (void *)__va(meminfo.end);
	max_mapnr   = MAP_NR(high_memory);

	/*
	 * We may have non-contiguous memory.  Setup the PageSkip stuff,
	 * and mark the areas of mem_map which can be freed
	 */
	if (meminfo.nr_banks != 1)
		create_memmap_holes(&meminfo);

	/* this will put all unused low memory onto the freelists */
	for (node = 0; node < numnodes; node++)
		totalram_pages += free_all_bootmem_node(node);

	/*
	 * Since our memory may not be contiguous, calculate the
	 * real number of pages we have in this system
	 */
	printk(KERN_INFO "Memory:");

	num_physpages = 0;
	for (i = 0; i < meminfo.nr_banks; i++) {
		num_physpages += meminfo.bank[i].size >> PAGE_SHIFT;
		printk(" %ldMB", meminfo.bank[i].size >> 20);
	}

	printk(" = %luMB total\n", num_physpages >> (20 - PAGE_SHIFT));
	printk(KERN_NOTICE "Memory: %luKB available (%dK code, "
		"%dK data, %dK init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		codepages >> 10, datapages >> 10, initpages >> 10);

	if (PAGE_SIZE >= 16384 && num_physpages <= 128) {
		extern int sysctl_overcommit_memory;
		/*
		 * On a machine this small we won't get
		 * anywhere without overcommit, so turn
		 * it on by default.
		 */
		sysctl_overcommit_memory = 1;
	}
}

static inline void free_area(unsigned long addr, unsigned long end, char *s)
{
	unsigned int size = (end - addr) >> 10;
	struct page *page = mem_map + MAP_NR(addr);

	for (; addr < end; addr += PAGE_SIZE, page ++) {
		ClearPageReserved(page);
		set_page_count(page, 1);
		free_page(addr);
		totalram_pages++;
	}

	if (size)
		printk(" %dk %s", size, s);
}

void free_initmem(void)
{
	extern char __init_begin, __init_end;

	printk("Freeing unused kernel memory:");

	free_area((unsigned long)(&__init_begin),
		  (unsigned long)(&__init_end),
		  "init");

	printk("\n");
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	unsigned long addr;
	for (addr = start; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(mem_map + MAP_NR(addr));
		set_page_count(mem_map+MAP_NR(addr), 1);
		free_page(addr);
		totalram_pages++;
	}
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

void si_meminfo(struct sysinfo *val)
{
	val->totalram  = totalram_pages;
	val->sharedram = 0;
	val->freeram   = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = 0;
	val->freehigh  = 0;
	val->mem_unit  = PAGE_SIZE;
}
