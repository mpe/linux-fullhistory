/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
 *  Removed race in async swapping. 14.4.1996. Bruno Haible
 *  Add swap of shared pages through the page cache. 20.2.1998. Stephen Tweedie
 *  Always use brw_page, life becomes simpler. 12 May 1998 Eric Biederman
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/locks.h>
#include <linux/swapctl.h>

#include <asm/pgtable.h>

static struct wait_queue * lock_queue = NULL;

/*
 * Reads or writes a swap page.
 * wait=1: start I/O and wait for completion. wait=0: start asynchronous I/O.
 *
 * Important prevention of race condition: the caller *must* atomically 
 * create a unique swap cache entry for this swap page before calling
 * rw_swap_page, and must lock that page.  By ensuring that there is a
 * single page of memory reserved for the swap entry, the normal VM page
 * lock on that page also doubles as a lock on swap entries.  Having only
 * one lock to deal with per swap entry (rather than locking swap and memory
 * independently) also makes it easier to make certain swapping operations
 * atomic, which is particularly important when we are trying to ensure 
 * that shared pages stay shared while being swapped.
 */

static void rw_swap_page_base(int rw, unsigned long entry, struct page *page, int wait)
{
	unsigned long type, offset;
	struct swap_info_struct * p;
	int zones[PAGE_SIZE/512];
	int zones_used;
	kdev_t dev = 0;
	int block_size;

#ifdef DEBUG_SWAP
	printk ("DebugVM: %s_swap_page entry %08lx, page %p (count %d), %s\n",
		(rw == READ) ? "read" : "write", 
		entry, (char *) page_address(page), atomic_read(&page->count),
		wait ? "wait" : "nowait");
#endif

	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles) {
		printk("Internal error: bad swap-device\n");
		return;
	}

	/* Don't allow too many pending pages in flight.. */
	if (atomic_read(&nr_async_pages) > pager_daemon.swap_cluster)
		wait = 1;

	p = &swap_info[type];
	offset = SWP_OFFSET(entry);
	if (offset >= p->max) {
		printk("rw_swap_page: weirdness\n");
		return;
	}
	if (p->swap_map && !p->swap_map[offset]) {
		printk(KERN_ERR "rw_swap_page: "
			"Trying to %s unallocated swap (%08lx)\n", 
			(rw == READ) ? "read" : "write", entry);
		return;
	}
	if (!(p->flags & SWP_USED)) {
		printk(KERN_ERR "rw_swap_page: "
			"Trying to swap to unused swap-device\n");
		return;
	}

	if (!PageLocked(page)) {
		printk(KERN_ERR "VM: swap page is unlocked\n");
		return;
	}

	if (PageSwapCache(page)) {
		/* Make sure we are the only process doing I/O with this swap page. */
		while (test_and_set_bit(offset,p->swap_lockmap)) {
			run_task_queue(&tq_disk);
			sleep_on(&lock_queue);
		}

		/* 
		 * Make sure that we have a swap cache association for this
		 * page.  We need this to find which swap page to unlock once
		 * the swap IO has completed to the physical page.  If the page
		 * is not already in the cache, just overload the offset entry
		 * as if it were: we are not allowed to manipulate the inode
		 * hashing for locked pages.
		 */
		if (page->offset != entry) {
			printk ("swap entry mismatch");
			return;
		}
	}
	if (rw == READ) {
		clear_bit(PG_uptodate, &page->flags);
		kstat.pswpin++;
	} else
		kstat.pswpout++;

	atomic_inc(&page->count);
	if (p->swap_device) {
		zones[0] = offset;
		zones_used = 1;
		dev = p->swap_device;
		block_size = PAGE_SIZE;
	} else if (p->swap_file) {
		struct inode *swapf = p->swap_file->d_inode;
		int i;
		if (swapf->i_op->bmap == NULL
			&& swapf->i_op->smap != NULL){
			/*
				With MS-DOS, we use msdos_smap which returns
				a sector number (not a cluster or block number).
				It is a patch to enable the UMSDOS project.
				Other people are working on better solution.

				It sounds like ll_rw_swap_file defined
				its operation size (sector size) based on
				PAGE_SIZE and the number of blocks to read.
				So using bmap or smap should work even if
				smap will require more blocks.
			*/
			int j;
			unsigned int block = offset << 3;

			for (i=0, j=0; j< PAGE_SIZE ; i++, j += 512){
				if (!(zones[i] = swapf->i_op->smap(swapf,block++))) {
					printk("rw_swap_page: bad swap file\n");
					return;
				}
			}
			block_size = 512;
		}else{
			int j;
			unsigned int block = offset
				<< (PAGE_SHIFT - swapf->i_sb->s_blocksize_bits);

			block_size = swapf->i_sb->s_blocksize;
			for (i=0, j=0; j< PAGE_SIZE ; i++, j += block_size)
				if (!(zones[i] = bmap(swapf,block++))) {
					printk("rw_swap_page: bad swap file\n");
					return;
				}
			zones_used = i;
			dev = swapf->i_dev;
		}
	} else {
		printk(KERN_ERR "rw_swap_page: no swap file or device\n");
		/* Do some cleaning up so if this ever happens we can hopefully
		 * trigger controlled shutdown.
		 */
		if (PageSwapCache(page)) {
			if (!test_and_clear_bit(offset,p->swap_lockmap))
				printk("swap_after_unlock_page: lock already cleared\n");
			wake_up(&lock_queue);
		}
		atomic_dec(&page->count);
		return;
	}
 	if (!wait) {
 		set_bit(PG_decr_after, &page->flags);
 		atomic_inc(&nr_async_pages);
 	}
 	if (PageSwapCache(page)) {
 		/* only lock/unlock swap cache pages! */
 		set_bit(PG_swap_unlock_after, &page->flags);
 	}
 	set_bit(PG_free_after, &page->flags);

 	/* block_size == PAGE_SIZE/zones_used */
 	brw_page(rw, page, dev, zones, block_size, 0);
 
 	/* Note! For consistency we do all of the logic,
 	 * decrementing the page count, and unlocking the page in the
 	 * swap lock map - in the IO completion handler.
 	 */
 	if (!wait) 
 		return;
 	wait_on_page(page);
	/* This shouldn't happen, but check to be sure. */
	if (atomic_read(&page->count) == 0)
		printk(KERN_ERR "rw_swap_page: page unused while waiting!\n");

#ifdef DEBUG_SWAP
	printk ("DebugVM: %s_swap_page finished on page %p (count %d)\n",
		(rw == READ) ? "read" : "write", 
		(char *) page_adddress(page), 
		atomic_read(&page->count));
#endif
}

/* Note: We could remove this totally asynchronous function,
 * and improve swap performance, and remove the need for the swap lock map,
 * by not removing pages from the swap cache until after I/O has been
 * processed and letting remove_from_page_cache decrement the swap count
 * just before it removes the page from the page cache.
 */
/* This is run when asynchronous page I/O has completed. */
void swap_after_unlock_page (unsigned long entry)
{
	unsigned long type, offset;
	struct swap_info_struct * p;

	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles) {
		printk("swap_after_unlock_page: bad swap-device\n");
		return;
	}
	p = &swap_info[type];
	offset = SWP_OFFSET(entry);
	if (offset >= p->max) {
		printk("swap_after_unlock_page: weirdness\n");
		return;
	}
	if (!test_and_clear_bit(offset,p->swap_lockmap))
		printk("swap_after_unlock_page: lock already cleared\n");
	wake_up(&lock_queue);
}

/* A simple wrapper so the base function doesn't need to enforce
 * that all swap pages go through the swap cache!
 */
void rw_swap_page(int rw, unsigned long entry, char *buf, int wait)
{
	struct page *page = mem_map + MAP_NR(buf);

	if (page->inode && page->inode != &swapper_inode)
		panic ("Tried to swap a non-swapper page");

	/*
	 * Make sure that we have a swap cache association for this
	 * page.  We need this to find which swap page to unlock once
	 * the swap IO has completed to the physical page.  If the page
	 * is not already in the cache, just overload the offset entry
	 * as if it were: we are not allowed to manipulate the inode
	 * hashing for locked pages.
	 */
	if (!PageSwapCache(page)) {
		printk("VM: swap page is not in swap cache\n");
		return;
	}
	if (page->offset != entry) {
		printk ("swap entry mismatch");
		return;
	}
	rw_swap_page_base(rw, entry, page, wait);
}

/*
 * Setting up a new swap file needs a simple wrapper just to read the 
 * swap signature.  SysV shared memory also needs a simple wrapper.
 */
void rw_swap_page_nocache(int rw, unsigned long entry, char *buffer)
{
	struct page *page;
	
	page = mem_map + MAP_NR((unsigned long) buffer);
	wait_on_page(page);
	set_bit(PG_locked, &page->flags);
	if (test_and_set_bit(PG_swap_cache, &page->flags)) {
		printk ("VM: read_swap_page: page already in swap cache!\n");
		return;
	}
	if (page->inode) {
		printk ("VM: read_swap_page: page already in page cache!\n");
		return;
	}
	page->inode = &swapper_inode;
	page->offset = entry;
	atomic_inc(&page->count);	/* Protect from shrink_mmap() */
	rw_swap_page(rw, entry, buffer, 1);
	atomic_dec(&page->count);
	page->inode = 0;
	clear_bit(PG_swap_cache, &page->flags);
}

/*
 * shmfs needs a version that doesn't put the page in the page cache!
 * The swap lock map insists that pages be in the page cache!
 * Therefore we can't use it.  Later when we can remove the need for the
 * lock map and we can reduce the number of functions exported.
 */
void rw_swap_page_nolock(int rw, unsigned long entry, char *buffer, int wait)
{
	struct page *page = mem_map + MAP_NR((unsigned long) buffer);
	
	if (!PageLocked(page)) {
		printk("VM: rw_swap_page_nolock: page not locked!\n");
		return;
	}
	if (PageSwapCache(page)) {
		printk ("VM: rw_swap_page_nolock: page in swap cache!\n");
		return;
	}
	rw_swap_page_base(rw, entry, page, wait);
}
