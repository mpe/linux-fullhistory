/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995-1999  Russell King
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
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/hardware.h>

#include "map.h"

pgd_t swapper_pg_dir[PTRS_PER_PGD];
#ifndef CONFIG_NO_PGT_CACHE
struct pgtable_cache_struct quicklists;
#endif

extern unsigned long free_area_init(unsigned long, unsigned long);
extern void show_net_buffers(void);

extern char _etext, _text, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
#ifndef CONFIG_NO_PGT_CACHE
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
#endif
	return freed;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t *empty_bad_page_table;

pte_t *__bad_pagetable(void)
{
	pte_t bad_page;
	int i;

	bad_page = BAD_PAGE;
	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(empty_bad_page_table + i, bad_page);

	return empty_bad_page_table;
}

unsigned long *empty_zero_page;
unsigned long *empty_bad_page;

pte_t __bad_page(void)
{
	memzero (empty_bad_page, PAGE_SIZE);
	return pte_nocache(pte_mkdirty(mk_pte((unsigned long) empty_bad_page, PAGE_SHARED)));
}

void show_mem(void)
{
	extern void show_net_buffers(void);
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = MAP_NR(high_memory);
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

/*
 * paging_init() sets up the page tables...
 */
unsigned long __init paging_init(unsigned long start_mem, unsigned long end_mem)
{
	start_mem = PAGE_ALIGN(start_mem);

	empty_zero_page = (unsigned long *)start_mem;
	memzero(empty_zero_page, PAGE_SIZE);
	start_mem += PAGE_SIZE;

	empty_bad_page = (unsigned long *)start_mem;
	start_mem += PAGE_SIZE;

#ifdef CONFIG_CPU_32
	start_mem += PTRS_PER_PTE * BYTES_PER_PTR;
#endif
	empty_bad_page_table = (pte_t *)start_mem;
	start_mem += PTRS_PER_PTE * BYTES_PER_PTR;

	start_mem = setup_page_tables(start_mem, end_mem);

	flush_tlb_all();

	end_mem &= PAGE_MASK;
	high_memory = (void *)end_mem;

	return free_area_init(start_mem, end_mem);
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
			clear_bit(PG_reserved, &mem_map[MAP_NR(low)].flags);
			low += PAGE_SIZE;
		}
	}
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	int initpages = 0, i, min_nr;
	unsigned long tmp;

	end_mem      &= PAGE_MASK;
	high_memory   = (void *)end_mem;
	max_mapnr     = MAP_NR(end_mem);
	num_physpages = 0;

	/* setup address validity bitmap */
	start_mem = create_mem_holes(start_mem, end_mem);

	start_mem = PAGE_ALIGN(start_mem);

	/* mark usable pages in the mem_map[] */
	mark_usable_memory_areas(start_mem, end_mem);

	/* free unused mem_map[] entries */
	free_unused_mem_map();

#define BETWEEN(w,min,max) ((w) >= (unsigned long)(min) && \
			    (w) < (unsigned long)(max))

	for (tmp = PAGE_OFFSET; tmp < end_mem ; tmp += PAGE_SIZE) {
		if (PageSkip(mem_map+MAP_NR(tmp))) {
			unsigned long next;

			next = mem_map[MAP_NR(tmp)].next_hash - mem_map;

			next = (next << PAGE_SHIFT) + PAGE_OFFSET;

			if (next < tmp || next >= end_mem)
				break;
			tmp = next;
		}
		num_physpages++;
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if (BETWEEN(tmp, &__init_begin, &__init_end))
				initpages++;
			else if (BETWEEN(tmp, &_text, &_etext))
				codepages++;
			else if (BETWEEN(tmp, &_etext, &_edata))
				datapages++;
			else if (BETWEEN(tmp, &__bss_start, start_mem))
				datapages++;
			else
				reservedpages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || !BETWEEN(tmp, initrd_start, initrd_end))
#endif
			free_page(tmp);
	}

#undef BETWEEN

	printk ("Memory: %luk/%luM available (%dk code, %dk reserved, %dk data, %dk init)\n",
		 (unsigned long) nr_free_pages << (PAGE_SHIFT-10),
		 num_physpages >> (20 - PAGE_SHIFT),
		 codepages     << (PAGE_SHIFT-10),
		 reservedpages << (PAGE_SHIFT-10),
		 datapages     << (PAGE_SHIFT-10),
		 initpages     << (PAGE_SHIFT-10));

	i = nr_free_pages >> 7;
	if (PAGE_SIZE < 32768)
		min_nr = 10;
	else
		min_nr = 2;
	if (i < min_nr)
		i = min_nr;
	if (i > 256)
		i = 256;
	freepages.min = i;
	freepages.low = i * 2;
	freepages.high = i * 3;

#ifdef CONFIG_CPU_26
	if (max_mapnr <= 128) {
		extern int sysctl_overcommit_memory;
		/* On a machine this small we won't get anywhere without
		   overcommit, so turn it on by default.  */
		sysctl_overcommit_memory = 1;
	}
#endif
}

static void free_area(unsigned long addr, unsigned long end, char *s)
{
	unsigned int size = (end - addr) >> 10;

	for (; addr < end; addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		free_page(addr);
	}

	if (size)
		printk(" %dk %s", size, s);
}

void free_initmem (void)
{
	printk("Freeing unused kernel memory:");

	free_area((unsigned long)(&__init_begin),
		  (unsigned long)(&__init_end),
		  "init");

#ifdef CONFIG_FOOTBRIDGE
	{
	extern int __netwinder_begin, __netwinder_end,
		   __ebsa285_begin, __ebsa285_end;

	if (!machine_is_netwinder())
		free_area((unsigned long)(&__netwinder_begin),
			  (unsigned long)(&__netwinder_end),
			  "netwinder");

	if (!machine_is_ebsa285() && !machine_is_cats() &&
	    !machine_is_co285())
		free_area((unsigned long)(&__ebsa285_begin),
			  (unsigned long)(&__ebsa285_end),
			  "ebsa285/cats");
	}
#endif

	printk("\n");
}

void si_meminfo(struct sysinfo *val)
{
	struct page *page, *end;

	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = atomic_read(&buffermem);
	for (page = mem_map, end = mem_map + max_mapnr;
	     page < end; page++) {
		if (PageSkip(page)) {
			if (page->next_hash < page)
				break;
			page = page->next_hash;
		}
		if (PageReserved(page))
			continue;
		val->totalram++;
		if (!atomic_read(&page->count))
			continue;
		val->sharedram += atomic_read(&page->count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	val->totalbig = 0;
	val->freebig = 0;
}
