/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>

unsigned long highmem_mapnr;
unsigned long nr_free_highpages = 0;

struct page * prepare_highmem_swapout(struct page * page)
{
	unsigned long regular_page;
	unsigned long vaddr;
	/*
	 * If this is a highmem page so it can't be swapped out directly
	 * otherwise the b_data buffer addresses will break
	 * the lowlevel device drivers.
	 */
	if (!PageHighMem(page))
		return page;

	regular_page = __get_free_page(GFP_ATOMIC);
	if (!regular_page)
		return NULL;

	vaddr = kmap(page, KM_READ);
	copy_page((void *)regular_page, (void *)vaddr);
	kunmap(vaddr, KM_READ);

	/*
	 * ok, we can just forget about our highmem page since 
	 * we stored its data into the new regular_page.
	 */
	__free_page(page);

	return mem_map + MAP_NR(regular_page);
}

struct page * replace_with_highmem(struct page * page)
{
	struct page *highpage;
	unsigned long vaddr;

	if (PageHighMem(page) || !nr_free_highpages)
		return page;

	highpage = get_free_highpage(GFP_ATOMIC|__GFP_HIGHMEM);
	if (!highpage)
		return page;
	if (!PageHighMem(highpage)) {
		__free_page(highpage);
		return page;
	}

	vaddr = kmap(highpage, KM_WRITE);
	copy_page((void *)vaddr, (void *)page_address(page));
	kunmap(vaddr, KM_WRITE);

	/* Preserve the caching of the swap_entry. */
	highpage->offset = page->offset;
	highpage->inode = page->inode;

	/*
	 * We can just forget the old page since 
	 * we stored its data into the new highmem-page.
	 */
	__free_page(page);

	return highpage;
}
