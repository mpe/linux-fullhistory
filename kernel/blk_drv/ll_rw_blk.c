/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/locks.h>

#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
struct wait_queue * wait_for_request = NULL;

/* This specifies how many sectors to read ahead on the disk.  */

int read_ahead[MAX_BLKDEV] = {0, };

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
struct blk_dev_struct blk_dev[MAX_BLKDEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL },		/* dev lp */
	{ NULL, NULL },		/* dev pipes */
	{ NULL, NULL },		/* dev sd */
	{ NULL, NULL }		/* dev st */
};

/*
 * blk_size contains the size of all block-devices in units of 1024 byte
 * sectors:
 *
 * blk_size[MAJOR][MINOR]
 *
 * if (!blk_size[MAJOR]) then no minor size checking is done.
 */
int * blk_size[MAX_BLKDEV] = { NULL, NULL, };

/*
 * blksize_size contains the size of all block-devices:
 *
 * blksize_size[MAJOR][MINOR]
 *
 * if (!blksize_size[MAJOR]) then 1024 bytes is assumed.
 */
int * blksize_size[MAX_BLKDEV] = { NULL, NULL, };

/* RO fail safe mechanism */

static long ro_bits[MAX_BLKDEV][8];

int is_read_only(int dev)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return 0;
	return ro_bits[major][minor >> 5] & (1 << (minor & 31));
}

void set_device_ro(int dev,int flag)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return;
	if (flag) ro_bits[major][minor >> 5] |= 1 << (minor & 31);
	else ro_bits[major][minor >> 5] &= ~(1 << (minor & 31));
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	req->next = NULL;
	cli();
	if (req->bh)
		req->bh->b_dirt = 0;
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		(dev->request_fn)();
		sti();
		return;
	}
	for ( ; tmp->next ; tmp = tmp->next) {
		if ((IN_ORDER(tmp,req) ||
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	}
	req->next = tmp->next;
	tmp->next = req;

/* Scsi devices are treated differently */
	if(MAJOR(req->dev) == 8 || 
	   MAJOR(req->dev) == 9 ||
	   MAJOR(req->dev) == 11)
	  (dev->request_fn)();

	sti();
}

static void make_request(int major,int rw, struct buffer_head * bh)
{
	unsigned int sector, count;
	struct request * req;
	int rw_ahead;

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
	rw_ahead = (rw == READA || rw == WRITEA);
	if (rw_ahead) {
		if (bh->b_lock)
			return;
		if (rw == READA)
			rw = READ;
		else
			rw = WRITE;
	}
	if (rw!=READ && rw!=WRITE) {
		printk("Bad block dev command, must be R/W/RA/WA\n");
		return;
	}
	count = bh->b_size >> 9;
	sector = bh->b_blocknr * count;
	if (blk_size[major])
		if (blk_size[major][MINOR(bh->b_dev)] < (sector + count)>>1) {
			bh->b_dirt = bh->b_uptodate = 0;
			return;
		}
	lock_buffer(bh);
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
/* The scsi disk drivers completely remove the request from the queue when
   they start processing an entry.  For this reason it is safe to continue
   to add links to the top entry for scsi devices */

repeat:
	cli();
	if ((major == 3 ||  major == 8 || major == 11)&& (req = blk_dev[major].current_request)) {
	        if(major == 3) req = req->next;
		while (req) {
			if (req->dev == bh->b_dev &&
			    !req->waiting &&
			    req->cmd == rw &&
			    req->sector + req->nr_sectors == sector &&
			    req->nr_sectors < 254) {
				req->bhtail->b_reqnext = bh;
				req->bhtail = bh;
				req->nr_sectors += count;
				bh->b_dirt = 0;
				sti();
				return;
			      }
			req = req->next;
		      }
	      }
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
	if (rw == READ)
		req = request+NR_REQUEST;
	else
		req = request+(NR_REQUEST/2);
/* find an empty request */
	while (--req >= request)
		if (req->dev < 0)
			goto found;
/* if none found, sleep on new requests: check for rw_ahead */
	if (rw_ahead) {
		sti();
		unlock_buffer(bh);
		return;
	}
	sleep_on(&wait_for_request);
	sti();
	goto repeat;

found:
/* fill up the request-info, and add it to the queue */
	req->dev = bh->b_dev;
	sti();
	req->cmd = rw;
	req->errors = 0;
	req->sector = sector;
	req->nr_sectors = count;
	req->current_nr_sectors = count;
	req->buffer = bh->b_data;
	req->waiting = NULL;
	req->bh = bh;
	req->bhtail = bh;
	req->next = NULL;
	add_request(major+blk_dev,req);
}

void ll_rw_page(int rw, int dev, int page, char * buffer)
{
	struct request * req;
	unsigned int major = MAJOR(dev);

	if (major >= MAX_BLKDEV || !(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device %04x (%d)\n",dev,page*8);
		return;
	}
	if (rw!=READ && rw!=WRITE)
		panic("Bad block dev command, must be R/W");
	if (rw == WRITE && is_read_only(dev)) {
		printk("Can't page to read-only device 0x%X\n",dev);
		return;
	}
	cli();
repeat:
	req = request+NR_REQUEST;
	while (--req >= request)
		if (req->dev<0)
			break;
	if (req < request) {
		sleep_on(&wait_for_request);
		goto repeat;
	}
	sti();
/* fill up the request-info, and add it to the queue */
	req->dev = dev;
	req->cmd = rw;
	req->errors = 0;
	req->sector = page<<3;
	req->nr_sectors = 8;
	req->current_nr_sectors = 8;
	req->buffer = buffer;
	req->waiting = current;
	req->bh = NULL;
	req->next = NULL;
	current->state = TASK_SWAPPING;
	add_request(major+blk_dev,req);
	schedule();
}

/* This function can be used to request a number of buffers from a block
   device. Currently the only restriction is that all buffers must belong to
   the same device */

void ll_rw_block(int rw, int nr, struct buffer_head * bh[])
{
	unsigned int major;

	struct request plug;
	int plugged;
	int correct_size;
	struct blk_dev_struct * dev;
	int i, j;

	/* Make sure that the first block contains something reasonable */
	while(!bh[0]){
	  bh++;
	  nr--;
	  if (nr <= 0) return;
	};

	if ((major=MAJOR(bh[0]->b_dev)) >= MAX_BLKDEV ||
	!(blk_dev[major].request_fn)) {
		printk("ll_rw_block: Trying to read nonexistent block-device %04x (%d)\n",bh[0]->b_dev,bh[0]->b_blocknr);
		for (i=0;i<nr; i++)
		  if (bh[i]) bh[i]->b_dirt = bh[i]->b_uptodate = 0;
		return;
	}

	for(j=0;j<nr; j++){
	  if(!bh[j]) continue;
	  /* Determine correct block size for this device */
	  correct_size = BLOCK_SIZE;
	  if(blksize_size[major] && blksize_size[major][MINOR(bh[j]->b_dev)])
	    correct_size = blksize_size[major][MINOR(bh[j]->b_dev)];
	  
	  if(bh[j]->b_size != correct_size) {
	    
	    printk("ll_rw_block: only %d-char blocks implemented (%d)\n",
		   correct_size, bh[j]->b_size);
	    
	    for (i=0;i<nr; i++)
	      if (bh[i]) bh[i]->b_dirt = bh[i]->b_uptodate = 0;
	    return;
	  }
	};

	if ((rw == WRITE || rw == WRITEA) && is_read_only(bh[0]->b_dev)) {
		printk("Can't write to read-only device 0x%X\n",bh[0]->b_dev);
		for (i=0;i<nr; i++)
		  if (bh[i]) bh[i]->b_dirt = bh[i]->b_uptodate = 0;
		return;
	}
/* If there are no pending requests for this device, then we insert a dummy
   request for that device.  This will prevent the request from starting until
   we have shoved all of the blocks into the queue, and then we let it rip */

	plugged = 0;
	cli();
	if (!blk_dev[major].current_request && nr > 1) {
	  blk_dev[major].current_request = &plug;
	  plug.dev = -1;
	  plug.next = NULL;
	  plugged = 1;
	};
	sti();
	for (i=0;i<nr; i++)
	  if (bh[i]) {
	    bh[i]->b_req = 1;
	    make_request(major, rw, bh[i]);
	}
	if(plugged){
	  cli();
	  blk_dev[major].current_request = plug.next;
	  dev = major+blk_dev;
	  (dev->request_fn)();
	  sti();
	};
}

void ll_rw_swap_file(int rw, int dev, unsigned int *b, int nb, char *buf)
{
	int i;
	int buffersize;
	struct request * req;
	unsigned int major = MAJOR(dev);

	if (major >= MAX_BLKDEV || !(blk_dev[major].request_fn)) {
		printk("ll_rw_swap_file: trying to swap nonexistent block-device\n");
		return;
	}

	if (rw!=READ && rw!=WRITE) {
		printk("ll_rw_swap: bad block dev command, must be R/W");
		return;
	}
	if (rw == WRITE && is_read_only(dev)) {
		printk("Can't swap to read-only device 0x%X\n",dev);
		return;
	}
	
	buffersize = PAGE_SIZE / nb;

	for (i=0; i<nb; i++, buf += buffersize)
	{
repeat:
		req = request+NR_REQUEST;
		while (--req >= request)
			if (req->dev<0)
				break;
		if (req < request) {
			sleep_on(&wait_for_request);
			goto repeat;
		}

		req->dev = dev;
		req->cmd = rw;
		req->errors = 0;
		req->sector = (b[i] * buffersize) >> 9;
		req->nr_sectors = buffersize >> 9;
		req->current_nr_sectors = buffersize >> 9;
		req->buffer = buf;
		req->waiting = current;
		req->bh = NULL;
		req->next = NULL;
		current->state = TASK_UNINTERRUPTIBLE;
		add_request(major+blk_dev,req);
		schedule();
	}
}

long blk_dev_init(long mem_start, long mem_end)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {
		request[i].dev = -1;
		request[i].next = NULL;
	}
	memset(ro_bits,0,sizeof(ro_bits));
#ifdef CONFIG_BLK_DEV_HD
	mem_start = hd_init(mem_start,mem_end);
#endif
#ifdef CONFIG_BLK_DEV_XD
	mem_start = xd_init(mem_start,mem_end);
#endif
	if (ramdisk_size)
		mem_start += rd_init(mem_start, ramdisk_size*1024);
	return mem_start;
}
