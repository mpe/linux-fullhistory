/*
 *  linux/arch/arm/mm/small_page.c
 *
 *  Copyright (C) 1996  Russell King
 *
 * Changelog:
 *  26/01/1996	RMK	Cleaned up various areas to make little more generic
 */

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

#define SMALL_ALLOC_SHIFT	(10)
#define SMALL_ALLOC_SIZE	(1 << SMALL_ALLOC_SHIFT)
#define NR_BLOCKS		(PAGE_SIZE / SMALL_ALLOC_SIZE)

#if NR_BLOCKS != 4
#error I only support 4 blocks per page!
#endif

#define USED(pg)		((atomic_read(&(pg)->count) >> 8) & 15)
#define SET_USED(pg,off)	(atomic_read(&(pg)->count) |= 256 << off)
#define CLEAR_USED(pg,off)	(atomic_read(&(pg)->count) &= ~(256 << off))
#define IS_FREE(pg,off)		(!(atomic_read(&(pg)->count) & (256 << off)))
#define PAGE_PTR(page,block)	((struct free_small_page *)((page) + \
					((block) << SMALL_ALLOC_SHIFT)))

struct free_small_page {
	unsigned long next;
	unsigned long prev;
};

/*
 * To handle allocating small pages, we use the main get_free_page routine,
 * and split the page up into 4.  The page is marked in mem_map as reserved,
 * so it can't be free'd by free_page.  The count field is used to keep track
 * of which sections of this page are allocated.
 */
static unsigned long small_page_ptr;

static unsigned char offsets[1<<NR_BLOCKS] = {
	0,	/* 0000 */
	1,	/* 0001 */
	0,	/* 0010 */
	2,	/* 0011 */
	0,	/* 0100 */
	1,	/* 0101 */
	0,	/* 0110 */
	3,	/* 0111 */
	0,	/* 1000 */
	1,	/* 1001 */
	0,	/* 1010 */
	2,	/* 1011 */
	0,	/* 1100 */
	1,	/* 1101 */
	0,	/* 1110 */
	4	/* 1111 */
};

static inline void clear_page_links(unsigned long page)
{
	struct free_small_page *fsp;
	int i;

	for (i = 0; i < NR_BLOCKS; i++) {
		fsp = PAGE_PTR(page, i);
		fsp->next = fsp->prev = 0;
	}
}

static inline void set_page_links_prev(unsigned long page, unsigned long prev)
{
	struct free_small_page *fsp;
	unsigned int mask;
	int i;

	if (!page)
		return;

	mask = USED(&mem_map[MAP_NR(page)]);
	for (i = 0; i < NR_BLOCKS; i++) {
		if (mask & (1 << i))
			continue;
		fsp = PAGE_PTR(page, i);
		fsp->prev = prev;
	}
}

static inline void set_page_links_next(unsigned long page, unsigned long next)
{
	struct free_small_page *fsp;
	unsigned int mask;
	int i;

	if (!page)
		return;

	mask = USED(&mem_map[MAP_NR(page)]);
	for (i = 0; i < NR_BLOCKS; i++) {
		if (mask & (1 << i))
			continue;
		fsp = PAGE_PTR(page, i);
		fsp->next = next;
	}
}

unsigned long get_small_page(int priority)
{
	struct free_small_page *fsp;
	unsigned long new_page;
	unsigned long flags;
	struct page *page;
	int offset;

	save_flags(flags);
	if (!small_page_ptr)
		goto need_new_page;
	cli();
again:
	page = mem_map + MAP_NR(small_page_ptr);
	offset = offsets[USED(page)];
	SET_USED(page, offset);
	new_page = (unsigned long)PAGE_PTR(small_page_ptr, offset);
	if (USED(page) == 15) {
		fsp = (struct free_small_page *)new_page;
		set_page_links_prev (fsp->next, 0);
		small_page_ptr = fsp->next;
	}
	restore_flags(flags);
	return new_page;

need_new_page:
	new_page = __get_free_page(priority);
	if (!small_page_ptr) {
		if (new_page) {
			set_bit (PG_reserved, &mem_map[MAP_NR(new_page)].flags);
			clear_page_links (new_page);
			cli();
			small_page_ptr = new_page;
			goto again;
		}
		restore_flags(flags);
		return 0;
	}
	free_page(new_page);
	cli();
	goto again;
}

void free_small_page(unsigned long spage)
{
	struct free_small_page *ofsp, *cfsp;
	unsigned long flags;
	struct page *page;
	int offset, oldoffset;

	offset = (spage >> SMALL_ALLOC_SHIFT) & (NR_BLOCKS - 1);
	spage -= offset << SMALL_ALLOC_SHIFT;

	page = mem_map + MAP_NR(spage);
	if (!PageReserved(page) || !USED(page)) {
		printk ("Trying to free non-small page from %p\n", __builtin_return_address(0));
		return;
	}
	if (IS_FREE(page, offset)) {
		printk ("Trying to free free small page from %p\n", __builtin_return_address(0));
		return;
	}
	save_flags_cli (flags);
	oldoffset = offsets[USED(page)];
	CLEAR_USED(page, offset);
	ofsp = PAGE_PTR(spage, oldoffset);
	cfsp = PAGE_PTR(spage, offset);

	if (oldoffset == NR_BLOCKS) { /* going from totally used to mostly used */
		cfsp->prev = 0;
		cfsp->next = small_page_ptr;
		set_page_links_prev (small_page_ptr, spage);
		small_page_ptr = spage;
	} else if (!USED(page)) {
		set_page_links_prev (ofsp->next, ofsp->prev);
		set_page_links_next (ofsp->prev, ofsp->next);
		if (spage == small_page_ptr)
			small_page_ptr = ofsp->next;
		clear_bit (PG_reserved, &page->flags);
		restore_flags(flags);
		free_page (spage);
	} else
		*cfsp = *ofsp;
	restore_flags(flags);
}
