/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *
 *  Rewritten to use page cache, (C) 1998 Stephen Tweedie
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
#include <linux/pagemap.h>

#include <asm/bitops.h>
#include <asm/pgtable.h>

#ifdef SWAP_CACHE_INFO
unsigned long swap_cache_add_total = 0;
unsigned long swap_cache_add_success = 0;
unsigned long swap_cache_del_total = 0;
unsigned long swap_cache_del_success = 0;
unsigned long swap_cache_find_total = 0;
unsigned long swap_cache_find_success = 0;

/* 
 * Keep a reserved false inode which we will use to mark pages in the
 * page cache are acting as swap cache instead of file cache. 
 *
 * We only need a unique pointer to satisfy the page cache, but we'll
 * reserve an entire zeroed inode structure for the purpose just to
 * ensure that any mistaken dereferences of this structure cause a
 * kernel oops.
 */
struct inode swapper_inode;


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
#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
#ifdef DEBUG_SWAP
	printk("DebugVM: add_to_swap_cache(%08lx count %d, entry %08lx)\n",
	       page_address(page), atomic_read(&page->count), entry);
#endif
	if (PageTestandSetSwapCache(page)) {
		printk("swap_cache: replacing non-empty entry %08lx "
		       "on page %08lx\n",
		       page->offset, page_address(page));
		return 0;
	}
	if (page->inode) {
		printk("swap_cache: replacing page-cached entry "
		       "on page %08lx\n", page_address(page));
		return 0;
	}
	atomic_inc(&page->count);
	page->inode = &swapper_inode;
	page->offset = entry;
	add_page_to_hash_queue(page, &swapper_inode, entry);
	add_page_to_inode_queue(&swapper_inode, page);
#ifdef SWAP_CACHE_INFO
	swap_cache_add_success++;
#endif
	return 1;
}

/*
 * If swap_map[] reaches 127, the entries are treated as "permanent".
 */
void swap_duplicate(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		goto out;
	type = SWP_TYPE(entry);
	if (type & SHM_SWP_TYPE)
		goto out;
	if (type >= nr_swapfiles)
		goto bad_file;
	p = type + swap_info;
	offset = SWP_OFFSET(entry);
	if (offset >= p->max)
		goto bad_offset;
	if (!p->swap_map[offset])
		goto bad_unused;
	if (p->swap_map[offset] < 126)
		p->swap_map[offset]++;
	else {
		static int overflow = 0;
		if (overflow++ < 5)
			printk("swap_duplicate: entry %08lx map count=%d\n",
				entry, p->swap_map[offset]);
		p->swap_map[offset] = 127;
	}
#ifdef DEBUG_SWAP
	printk("DebugVM: swap_duplicate(entry %08lx, count now %d)\n",
	       entry, p->swap_map[offset]);
#endif
out:
	return;

bad_file:
	printk("swap_duplicate: Trying to duplicate nonexistent swap-page\n");
	goto out;
bad_offset:
	printk("swap_duplicate: offset exceeds max\n");
	goto out;
bad_unused:
	printk("swap_duplicate at %8p: unused page\n", 
	       __builtin_return_address(0));
	goto out;
}


void remove_from_swap_cache(struct page *page)
{
	if (!page->inode) {
		printk ("VM: Removing swap cache page with zero inode hash "
			"on page %08lx\n", page_address(page));
		return;
	}
	if (page->inode != &swapper_inode) {
		printk ("VM: Removing swap cache page with wrong inode hash "
			"on page %08lx\n", page_address(page));
	}
	/*
	 * This is a legal case, but warn about it.
	 */
	if (atomic_read(&page->count) == 1) {
		printk (KERN_WARNING 
			"VM: Removing page cache on unshared page %08lx\n", 
			page_address(page));
	}

#ifdef DEBUG_SWAP
	printk("DebugVM: remove_from_swap_cache(%08lx count %d)\n",
	       page_address(page), atomic_read(&page->count));
#endif
	PageClearSwapCache (page);
	remove_inode_page(page);
}


int delete_from_swap_cache(struct page *page)
{
#ifdef SWAP_CACHE_INFO
	swap_cache_del_total++;
#endif	
	if (PageSwapCache (page))  {
		long entry = page->offset;
#ifdef SWAP_CACHE_INFO
		swap_cache_del_success++;
#endif
#ifdef DEBUG_SWAP
		printk("DebugVM: delete_from_swap_cache(%08lx count %d, "
		       "entry %08lx)\n",
		       page_address(page), atomic_read(&page->count), entry);
#endif
		remove_from_swap_cache (page);
		swap_free (entry);
		return 1;
	}
	return 0;
}

/* 
 * Perform a free_page(), also freeing any swap cache associated with
 * this page if it is the last user of the page. 
 */

void free_page_and_swap_cache(unsigned long addr)
{
	struct page *page = mem_map + MAP_NR(addr);
	/* 
	 * If we are the only user, then free up the swap cache. 
	 */
	if (PageSwapCache(page) && !is_page_shared(page)) {
		delete_from_swap_cache(page);
	}
	
	free_page(addr);
}


/*
 * Lookup a swap entry in the swap cache.  We need to be careful about
 * locked pages.  A found page will be returned with its refcount
 * incremented.
 */

static struct page * lookup_swap_cache(unsigned long entry)
{
	struct page *found;
	
	while (1) {
		found = find_page(&swapper_inode, entry);
		if (!found)
			return 0;
		if (found->inode != &swapper_inode 
		    || !PageSwapCache(found)) {
			__free_page(found);
			printk ("VM: Found a non-swapper swap page!\n");
			return 0;
		}
		if (!PageLocked(found))
			return found;
		__free_page(found);
		__wait_on_page(found);
	}
}

/* 
 * Locate a page of swap in physical memory, reserving swap cache space
 * and reading the disk if it is not already cached.  If wait==0, we are
 * only doing readahead, so don't worry if the page is already locked.
 */

struct page * read_swap_cache_async(unsigned long entry, int wait)
{
	struct page *found_page, *new_page = 0;
	unsigned long new_page_addr = 0;
	
#ifdef DEBUG_SWAP
	printk("DebugVM: read_swap_cache_async entry %08lx%s\n",
	       entry, wait ? ", wait" : "");
#endif
repeat:
	found_page = lookup_swap_cache(entry);
	if (found_page) {
		if (new_page)
			__free_page(new_page);
		return found_page;
	}

	/* The entry is not present.  Lock down a new page, add it to
	 * the swap cache and read its contents. */
	if (!new_page) {
		new_page_addr = __get_free_page(GFP_KERNEL);
		if (!new_page_addr)
			return 0;	/* Out of memory */
		new_page = mem_map + MAP_NR(new_page_addr);
		goto repeat;		/* We might have stalled */
	}
	
	if (!add_to_swap_cache(new_page, entry)) {
		free_page(new_page_addr);
		return 0;
	}
	swap_duplicate(entry);		/* Account for the swap cache */
	set_bit(PG_locked, &new_page->flags);
	rw_swap_page(READ, entry, (char *) new_page_addr, wait);
#ifdef DEBUG_SWAP
	printk("DebugVM: read_swap_cache_async created "
	       "entry %08lx at %p\n",
	       entry, (char *) page_address(new_page));
#endif
	return new_page;
}

