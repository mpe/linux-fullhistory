/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
 *  Removed race in async swapping. 14.4.1996. Bruno Haible
 *  Add swap of shared pages through the page cache. 20.2.1998. Stephen Tweedie
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/locks.h>
#include <linux/swapctl.h>

#include <asm/dma.h>
#include <asm/uaccess.h> /* for copy_to/from_user */
#include <asm/pgtable.h>

static struct wait_queue * lock_queue = NULL;

/*
 * Reads or writes a swap page.
 * wait=1: start I/O and wait for completion. wait=0: start asynchronous I/O.
 * All IO to swap files (as opposed to swap partitions) is done
 * synchronously.
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

void rw_swap_page(int rw, unsigned long entry, char * buf, int wait)
{
	unsigned long type, offset;
	struct swap_info_struct * p;
	struct page *page = mem_map + MAP_NR(buf);

#ifdef DEBUG_SWAP
	printk ("DebugVM: %s_swap_page entry %08lx, page %p (count %d), %s\n",
		(rw == READ) ? "read" : "write", 
		entry, buf, atomic_read(&page->count),
		wait ? "wait" : "nowait");
#endif

	if (page->inode && page->inode != &swapper_inode)
		panic ("Tried to swap a non-swapper page");
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
	
	/* Make sure we are the only process doing I/O with this swap page. */
	while (test_and_set_bit(offset,p->swap_lockmap)) {
		run_task_queue(&tq_disk);
		sleep_on(&lock_queue);
	}
	
	if (rw == READ) {
		clear_bit(PG_uptodate, &page->flags);
		kstat.pswpin++;
	} else
		kstat.pswpout++;

	atomic_inc(&page->count);
	/* 
	 * Make sure that we have a swap cache association for this
	 * page.  We need this to find which swap page to unlock once
	 * the swap IO has completed to the physical page.  If the page
	 * is not already in the cache, just overload the offset entry
	 * as if it were: we are not allowed to manipulate the inode
	 * hashing for locked pages.
	 */
	if (!PageSwapCache(page)) {
		printk(KERN_ERR "VM: swap page is not in swap cache\n");
		return;
	}
	if (page->offset != entry) {
		printk (KERN_ERR "VM: swap entry mismatch\n");
		return;
	}

	if (p->swap_device) {
		if (!wait) {
			set_bit(PG_free_after, &page->flags);
			set_bit(PG_decr_after, &page->flags);
			set_bit(PG_swap_unlock_after, &page->flags);
			atomic_inc(&nr_async_pages);
		}
		ll_rw_page(rw,p->swap_device,offset,buf);
		/*
		 * NOTE! We don't decrement the page count if we
		 * don't wait - that will happen asynchronously
		 * when the IO completes.
		 */
		if (!wait)
			return;
		wait_on_page(page);
	} else if (p->swap_file) {
		struct inode *swapf = p->swap_file->d_inode;
		unsigned int zones[PAGE_SIZE/512];
		int i;
		if (swapf->i_op->bmap == NULL
			&& swapf->i_op->smap != NULL){
			/*
				With MS-DOS, we use msdos_smap which return
				a sector number (not a cluster or block number).
				It is a patch to enable the UMSDOS project.
				Other people are working on better solution.

				It sounds like ll_rw_swap_file defined
				it operation size (sector size) based on
				PAGE_SIZE and the number of block to read.
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
		}else{
			int j;
			unsigned int block = offset
				<< (PAGE_SHIFT - swapf->i_sb->s_blocksize_bits);

			for (i=0, j=0; j< PAGE_SIZE ; i++, j +=swapf->i_sb->s_blocksize)
				if (!(zones[i] = bmap(swapf,block++))) {
					printk("rw_swap_page: bad swap file\n");
					return;
				}
		}
		ll_rw_swap_file(rw,swapf->i_dev, zones, i,buf);
		/* Unlike ll_rw_page, ll_rw_swap_file won't unlock the
		   page for us. */
		clear_bit(PG_locked, &page->flags);
		wake_up(&page->wait);
	} else
		printk(KERN_ERR "rw_swap_page: no swap file or device\n");

	/* This shouldn't happen, but check to be sure. */
	if (atomic_read(&page->count) == 1)
		printk(KERN_ERR "rw_swap_page: page unused while waiting!\n");
	atomic_dec(&page->count);
	if (offset && !test_and_clear_bit(offset,p->swap_lockmap))
		printk(KERN_ERR "rw_swap_page: lock already cleared\n");
	wake_up(&lock_queue);
#ifdef DEBUG_SWAP
	printk ("DebugVM: %s_swap_page finished on page %p (count %d)\n",
		(rw == READ) ? "read" : "write", 
		buf, atomic_read(&page->count));
#endif
}

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
 * Swap partitions are now read via brw_page.  ll_rw_page is an
 * asynchronous function now --- we must call wait_on_page afterwards
 * if synchronous IO is required.  
 */
void ll_rw_page(int rw, kdev_t dev, unsigned long offset, char * buffer)
{
	int block = offset;
	struct page *page;

	switch (rw) {
		case READ:
			break;
		case WRITE:
			if (is_read_only(dev)) {
				printk("Can't page to read-only device %s\n",
					kdevname(dev));
				return;
			}
			break;
		default:
			panic("ll_rw_page: bad block dev cmd, must be R/W");
	}
	page = mem_map + MAP_NR(buffer);
	if (!PageLocked(page))
		panic ("ll_rw_page: page not already locked");
	brw_page(rw, page, dev, &block, PAGE_SIZE, 0);
}
