/*
 * BIGMEM common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/bigmem.h>

unsigned long bigmem_mapnr;
int nr_free_bigpages = 0;

struct page * prepare_bigmem_swapout(struct page * page)
{
	/* if this is a bigmem page so it can't be swapped out directly
	   otherwise the b_data buffer addresses will break
	   the lowlevel device drivers. */
	if (PageBIGMEM(page)) {
		unsigned long regular_page;
		unsigned long vaddr;

		regular_page = __get_free_page(GFP_ATOMIC);
		if (!regular_page)
			return NULL;

		vaddr = kmap(page_address(page), KM_READ);
		copy_page(regular_page, vaddr);
		kunmap(vaddr, KM_READ);

		/* ok, we can just forget about our bigmem page since 
		   we stored its data into the new regular_page. */
		__free_page(page);

		page = MAP_NR(regular_page) + mem_map;
	}
	return page;
}

struct page * replace_with_bigmem(struct page * page)
{
	if (!PageBIGMEM(page) && nr_free_bigpages) {
		unsigned long kaddr;

		kaddr = __get_free_page(GFP_ATOMIC|GFP_BIGMEM);
		if (kaddr) {
			struct page * bigmem_page;

			bigmem_page = MAP_NR(kaddr) + mem_map;
			if (PageBIGMEM(bigmem_page)) {
				unsigned long vaddr;

				vaddr = kmap(kaddr, KM_WRITE);
				copy_page(vaddr, page_address(page));
				kunmap(vaddr, KM_WRITE);

				/* Preserve the caching of the swap_entry. */
				bigmem_page->offset = page->offset;

				/* We can just forget the old page since 
				   we stored its data into the new
				   bigmem_page. */
				__free_page(page);

				page = bigmem_page;
			}
		}
	}
	return page;
}
