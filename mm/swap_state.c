/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *
 *  Rewritten to use page cache, (C) 1998 Stephen Tweedie
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/init.h>
#include <linux/pagemap.h>

#include <asm/pgtable.h>

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

#ifdef SWAP_CACHE_INFO
unsigned long swap_cache_add_total = 0;
unsigned long swap_cache_del_total = 0;
unsigned long swap_cache_find_total = 0;
unsigned long swap_cache_find_success = 0;

void show_swap_cache_info(void)
{
	printk("Swap cache: add %ld, delete %ld, find %ld/%ld\n",
		swap_cache_add_total, 
		swap_cache_del_total,
		swap_cache_find_success, swap_cache_find_total);
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
		printk(KERN_ERR "swap_cache: replacing non-empty entry %08lx "
		       "on page %08lx\n",
		       page->offset, page_address(page));
		return 0;
	}
	if (page->inode) {
		printk(KERN_ERR "swap_cache: replacing page-cached entry "
		       "on page %08lx\n", page_address(page));
		return 0;
	}
	atomic_inc(&page->count);
	page->inode = &swapper_inode;
	page->offset = entry;
	add_page_to_hash_queue(page, &swapper_inode, entry);
	add_page_to_inode_queue(&swapper_inode, page);
	return 1;
}

/*
 * Verify that a swap entry is valid and increment its swap map count.
 *
 * Note: if swap_map[] reaches SWAP_MAP_MAX the entries are treated as
 * "permanent", but will be reclaimed by the next swapoff.
 */
int swap_duplicate(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;
	int result = 0;

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
	/*
	 * Entry is valid, so increment the map count.
	 */
	if (p->swap_map[offset] < SWAP_MAP_MAX)
		p->swap_map[offset]++;
	else {
		static int overflow = 0;
		if (overflow++ < 5)
			printk(KERN_WARNING
				"swap_duplicate: entry %08lx map count=%d\n",
				entry, p->swap_map[offset]);
		p->swap_map[offset] = SWAP_MAP_MAX;
	}
	result = 1;
#ifdef DEBUG_SWAP
	printk("DebugVM: swap_duplicate(entry %08lx, count now %d)\n",
	       entry, p->swap_map[offset]);
#endif
out:
	return result;

bad_file:
	printk(KERN_ERR
		"swap_duplicate: entry %08lx, nonexistent swap file\n", entry);
	goto out;
bad_offset:
	printk(KERN_ERR
		"swap_duplicate: entry %08lx, offset exceeds max\n", entry);
	goto out;
bad_unused:
	printk(KERN_ERR
		"swap_duplicate at %8p: entry %08lx, unused page\n", 
	       __builtin_return_address(0), entry);
	goto out;
}

int swap_count(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;
	int retval = 0;

	if (!entry)
		goto bad_entry;
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
	retval = p->swap_map[offset];
#ifdef DEBUG_SWAP
	printk("DebugVM: swap_count(entry %08lx, count %d)\n",
	       entry, retval);
#endif
out:
	return retval;

bad_entry:
	printk(KERN_ERR "swap_count: null entry!\n");
	goto out;
bad_file:
	printk(KERN_ERR
	       "swap_count: entry %08lx, nonexistent swap file!\n", entry);
	goto out;
bad_offset:
	printk(KERN_ERR
	       "swap_count: entry %08lx, offset exceeds max!\n", entry);
	goto out;
bad_unused:
	printk(KERN_ERR
	       "swap_count at %8p: entry %08lx, unused page!\n", 
	       __builtin_return_address(0), entry);
	goto out;
}

static inline void remove_from_swap_cache(struct page *page)
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

#ifdef DEBUG_SWAP
	printk("DebugVM: remove_from_swap_cache(%08lx count %d)\n",
	       page_address(page), atomic_read(&page->count));
#endif
	PageClearSwapCache (page);
	remove_inode_page(page);
}


/*
 * This must be called only on pages that have
 * been verified to be in the swap cache.
 */
void delete_from_swap_cache(struct page *page)
{
	long entry = page->offset;

#ifdef SWAP_CACHE_INFO
	swap_cache_del_total++;
#endif
#ifdef DEBUG_SWAP
	printk("DebugVM: delete_from_swap_cache(%08lx count %d, "
	       "entry %08lx)\n",
	       page_address(page), atomic_read(&page->count), entry);
#endif
	remove_from_swap_cache (page);
	swap_free (entry);
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
	
	__free_page(page);
}


/*
 * Lookup a swap entry in the swap cache.  We need to be careful about
 * locked pages.  A found page will be returned with its refcount
 * incremented.
 */

struct page * lookup_swap_cache(unsigned long entry)
{
	struct page *found;

#ifdef SWAP_CACHE_INFO
	swap_cache_find_total++;
#endif
	while (1) {
		found = find_page(&swapper_inode, entry);
		if (!found)
			return 0;
		if (found->inode != &swapper_inode || !PageSwapCache(found))
			goto out_bad;
		if (!PageLocked(found)) {
#ifdef SWAP_CACHE_INFO
			swap_cache_find_success++;
#endif
			return found;
		}
		__free_page(found);
		__wait_on_page(found);
	}

out_bad:
	printk (KERN_ERR "VM: Found a non-swapper swap page!\n");
	__free_page(found);
	return 0;
}

/* 
 * Locate a page of swap in physical memory, reserving swap cache space
 * and reading the disk if it is not already cached.  If wait==0, we are
 * only doing readahead, so don't worry if the page is already locked.
 *
 * A failure return means that either the page allocation failed or that
 * the swap entry is no longer in use.
 */

struct page * read_swap_cache_async(unsigned long entry, int wait)
{
	struct page *found_page = 0, *new_page;
	unsigned long new_page_addr;
	
#ifdef DEBUG_SWAP
	printk("DebugVM: read_swap_cache_async entry %08lx%s\n",
	       entry, wait ? ", wait" : "");
#endif
	/*
	 * Make sure the swap entry is still in use.
	 */
	if (!swap_duplicate(entry))	/* Account for the swap cache */
		goto out;
	/*
	 * Look for the page in the swap cache.
	 */
	found_page = lookup_swap_cache(entry);
	if (found_page)
		goto out_free_swap;

	new_page_addr = __get_free_page(GFP_USER);
	if (!new_page_addr)
		goto out_free_swap;	/* Out of memory */
	new_page = mem_map + MAP_NR(new_page_addr);

	/*
	 * Check the swap cache again, in case we stalled above.
	 */
	found_page = lookup_swap_cache(entry);
	if (found_page)
		goto out_free_page;
	/* 
	 * Add it to the swap cache and read its contents.
	 */
	if (!add_to_swap_cache(new_page, entry))
		goto out_free_page;

	set_bit(PG_locked, &new_page->flags);
	rw_swap_page(READ, entry, (char *) new_page_addr, wait);
#ifdef DEBUG_SWAP
	printk("DebugVM: read_swap_cache_async created "
	       "entry %08lx at %p\n",
	       entry, (char *) page_address(new_page));
#endif
	return new_page;

out_free_page:
	__free_page(new_page);
out_free_swap:
	swap_free(entry);
out:
	return found_page;
}
