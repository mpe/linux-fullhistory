/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
 *  Removed race in async swapping. 14.4.1996. Bruno Haible
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
#include <linux/locks.h>
#include <linux/swapctl.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

static struct wait_queue * lock_queue = NULL;

/*
 * Reads or writes a swap page.
 * wait=1: start I/O and wait for completion. wait=0: start asynchronous I/O.
 *
 * Important prevention of race condition: The first thing we do is set a lock
 * on this swap page, which lasts until I/O completes. This way a
 * write_swap_page(entry) immediately followed by a read_swap_page(entry)
 * on the same entry will first complete the write_swap_page(). Fortunately,
 * not more than one write_swap_page() request can be pending per entry. So
 * all races the caller must catch are: multiple read_swap_page() requests
 * on the same entry.
 */
void rw_swap_page(int rw, unsigned long entry, char * buf, int wait)
{
	unsigned long type, offset;
	struct swap_info_struct * p;
	struct page *page;
	
	type = SWP_TYPE(entry);
	if (type >= nr_swapfiles) {
		printk("Internal error: bad swap-device\n");
		return;
	}
	p = &swap_info[type];
	offset = SWP_OFFSET(entry);
	if (offset >= p->max) {
		printk("rw_swap_page: weirdness\n");
		return;
	}
	if (p->swap_map && !p->swap_map[offset]) {
		printk("Hmm.. Trying to use unallocated swap (%08lx)\n", entry);
		return;
	}
	if (!(p->flags & SWP_USED)) {
		printk("Trying to swap to unused swap-device\n");
		return;
	}
	/* Make sure we are the only process doing I/O with this swap page. */
	while (set_bit(offset,p->swap_lockmap)) {
		run_task_queue(&tq_disk);
		sleep_on(&lock_queue);
	}
	if (rw == READ)
		kstat.pswpin++;
	else
		kstat.pswpout++;
	page = mem_map + MAP_NR(buf);
	atomic_inc(&page->count);
	wait_on_page(page);
	if (p->swap_device) {
		if (!wait) {
			set_bit(PG_free_after, &page->flags);
			set_bit(PG_decr_after, &page->flags);
			set_bit(PG_swap_unlock_after, &page->flags);
			page->swap_unlock_entry = entry;
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
		struct inode *swapf = p->swap_file;
		unsigned int zones[PAGE_SIZE/512];
		int i;
		if (swapf->i_op->bmap == NULL
			&& swapf->i_op->smap != NULL){
			/*
				With MsDOS, we use msdos_smap which return
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
				}
		}
		ll_rw_swap_file(rw,swapf->i_dev, zones, i,buf);
	} else
		printk("rw_swap_page: no swap file or device\n");
	atomic_dec(&page->count);
	if (offset && !clear_bit(offset,p->swap_lockmap))
		printk("rw_swap_page: lock already cleared\n");
	wake_up(&lock_queue);
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
	if (!clear_bit(offset,p->swap_lockmap))
		printk("swap_after_unlock_page: lock already cleared\n");
	wake_up(&lock_queue);
}

/*
 * Swap partitions are now read via brw_page.  ll_rw_page is an
 * asynchronous function now --- we must call wait_on_page afterwards
 * if synchronous IO is required.  
 */
void ll_rw_page(int rw, kdev_t dev, unsigned long page, char * buffer)
{
	int block = page;

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
	if (set_bit(PG_locked, &mem_map[MAP_NR(buffer)].flags))
		panic ("ll_rw_page: page already locked");
	brw_page(rw, (unsigned long) buffer, dev, &block, PAGE_SIZE, 0);
}
