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
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/uaccess.h> /* for cop_to/from_user */
#include <asm/bitops.h>
#include <asm/pgtable.h>

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

int add_to_swap_cache(struct page *page, unsigned long entry)
{
	struct swap_info_struct * p = &swap_info[SWP_TYPE(entry)];

#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
	if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
		page->pg_swap_entry = entry;
		if (PageTestandSetSwapCache(page))
			printk("swap_cache: replacing non-empty entry\n");
#ifdef SWAP_CACHE_INFO
		swap_cache_add_success++;
#endif
		return 1;
	}
	return 0;
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

