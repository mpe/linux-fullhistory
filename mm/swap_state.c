/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

/*
 * To save us from swapping out pages which have just been swapped in and
 * have not been modified since then, we keep in swap_cache[page>>PAGE_SHIFT]
 * the swap entry which was last used to fill the page, or zero if the
 * page does not currently correspond to a page in swap. PAGE_DIRTY makes
 * this info useless.
 */
unsigned long *swap_cache;

#ifdef SWAP_CACHE_INFO
unsigned long swap_cache_add_total = 0;
unsigned long swap_cache_add_success = 0;
unsigned long swap_cache_del_total = 0;
unsigned long swap_cache_del_success = 0;
unsigned long swap_cache_find_total = 0;
unsigned long swap_cache_find_success = 0;

void show_swap_cache_info(void)
{
	printk("Swap cache: add %ld/%ld, delete %ld/%ld, find %ld/%ld\n",
		swap_cache_add_total, swap_cache_add_success, 
		swap_cache_del_total, swap_cache_del_success,
		swap_cache_find_total, swap_cache_find_success);
}
#endif

int add_to_swap_cache(unsigned long index, unsigned long entry)
{
	struct swap_info_struct * p = &swap_info[SWP_TYPE(entry)];

#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
	if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
		entry = xchg(swap_cache + index, entry);
		if (entry)  {
			printk("swap_cache: replacing non-NULL entry\n");
		}
#ifdef SWAP_CACHE_INFO
		swap_cache_add_success++;
#endif
		return 1;
	}
	return 0;
}

unsigned long init_swap_cache(unsigned long mem_start,
	unsigned long mem_end)
{
	unsigned long swap_cache_size;

	mem_start = (mem_start + 15) & ~15;
	swap_cache = (unsigned long *) mem_start;
	swap_cache_size = MAP_NR(mem_end);
	memset(swap_cache, 0, swap_cache_size * sizeof (unsigned long));
	return (unsigned long) (swap_cache + swap_cache_size);
}

void swap_duplicate(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		return;
	offset = SWP_OFFSET(entry);
	type = SWP_TYPE(entry);
	if (type & SHM_SWP_TYPE)
		return;
	if (type >= nr_swapfiles) {
		printk("Trying to duplicate nonexistent swap-page\n");
		return;
	}
	p = type + swap_info;
	if (offset >= p->max) {
		printk("swap_duplicate: weirdness\n");
		return;
	}
	if (!p->swap_map[offset]) {
		printk("swap_duplicate: trying to duplicate unused page\n");
		return;
	}
	p->swap_map[offset]++;
	return;
}

