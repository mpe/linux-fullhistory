/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
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
	while (set_bit(offset,p->swap_lockmap))
		sleep_on(&lock_queue);
	if (rw == READ)
		kstat.pswpin++;
	else
		kstat.pswpout++;
	page = mem_map + MAP_NR(buf);
	wait_on_page(page);
	if (p->swap_device) {
		if (!wait) {
			page->count++;
			page->free_after = 1;
			nr_async_pages++;
		}
		ll_rw_page(rw,p->swap_device,offset,buf);
		if (wait)
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
		printk("re_swap_page: no swap file or device\n");
	if (offset && !clear_bit(offset,p->swap_lockmap))
		printk("rw_swap_page: lock already cleared\n");
	wake_up(&lock_queue);
}
