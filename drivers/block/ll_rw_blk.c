/*
 *  linux/drivers/block/ll_rw_blk.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/locks.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
static struct request all_requests[NR_REQUEST];

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
	{ NULL, NULL },		/* 0 no_dev */
	{ NULL, NULL },		/* 1 dev mem */
	{ NULL, NULL },		/* 2 dev fd */
	{ NULL, NULL },		/* 3 dev ide0 or hd */
	{ NULL, NULL },		/* 4 dev ttyx */
	{ NULL, NULL },		/* 5 dev tty */
	{ NULL, NULL },		/* 6 dev lp */
	{ NULL, NULL },		/* 7 dev pipes */
	{ NULL, NULL },		/* 8 dev sd */
	{ NULL, NULL },		/* 9 dev st */
	{ NULL, NULL },		/* 10 */
	{ NULL, NULL },		/* 11 */
	{ NULL, NULL },		/* 12 */
	{ NULL, NULL },		/* 13 */
	{ NULL, NULL },		/* 14 */
	{ NULL, NULL },		/* 15 */
	{ NULL, NULL },		/* 16 */
	{ NULL, NULL },		/* 17 */
	{ NULL, NULL },		/* 18 */
	{ NULL, NULL },		/* 19 */
	{ NULL, NULL },		/* 20 */
	{ NULL, NULL },		/* 21 */
	{ NULL, NULL }		/* 22 dev ide1 */
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

/*
 * hardsect_size contains the size of the hardware sector of a device.
 *
 * hardsect_size[MAJOR][MINOR]
 *
 * if (!hardsect_size[MAJOR])
 *		then 512 bytes is assumed.
 * else
 *		sector_size is hardsect_size[MAJOR][MINOR]
 * This is currently set by some scsi device and read by the msdos fs driver
 * This might be a some uses later.
 */
int * hardsect_size[MAX_BLKDEV] = { NULL, NULL, };

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 */
static void plug_device(struct blk_dev_struct * dev, struct request * plug)
{
	unsigned long flags;

	plug->dev = -1;
	plug->cmd = -1;
	plug->next = NULL;
	save_flags(flags);
	cli();
	if (!dev->current_request)
		dev->current_request = plug;
	restore_flags(flags);
}

/*
 * remove the plug and let it rip..
 */
static void unplug_device(struct blk_dev_struct * dev)
{
	struct request * req;
	unsigned long flags;

	save_flags(flags);
	cli();
	req = dev->current_request;
	if (req && req->dev == -1 && req->cmd == -1) {
		dev->current_request = req->next;
		(dev->request_fn)();
	}
	restore_flags(flags);
}

/*
 * look for a free request in the first N entries.
 * NOTE: interrupts must be disabled on the way in, and will still
 *       be disabled on the way out.
 */
static inline struct request * get_request(int n, int dev)
{
	static struct request *prev_found = NULL, *prev_limit = NULL;
	register struct request *req, *limit;

	if (n <= 0)
		panic("get_request(%d): impossible!\n", n);

	limit = all_requests + n;
	if (limit != prev_limit) {
		prev_limit = limit;
		prev_found = all_requests;
	}
	req = prev_found;
	for (;;) {
		req = ((req > all_requests) ? req : limit) - 1;
		if (req->dev < 0)
			break;
		if (req == prev_found)
			return NULL;
	}
	prev_found = req;
	req->dev = dev;
	return req;
}

/*
 * wait until a free request in the first N entries is available.
 */
static struct request * __get_request_wait(int n, int dev)
{
	register struct request *req;
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&wait_for_request, &wait);
	for (;;) {
		unplug_device(MAJOR(dev)+blk_dev);
		current->state = TASK_UNINTERRUPTIBLE;
		cli();
		req = get_request(n, dev);
		sti();
		if (req)
			break;
		schedule();
	}
	remove_wait_queue(&wait_for_request, &wait);
	current->state = TASK_RUNNING;
	return req;
}

static inline struct request * get_request_wait(int n, int dev)
{
	register struct request *req;

	cli();
	req = get_request(n, dev);
	sti();
	if (req)
		return req;
	return __get_request_wait(n, dev);
}

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
	short		 disk_index;

	switch (MAJOR(req->dev)) {
		case SCSI_DISK_MAJOR:	disk_index = (MINOR(req->dev) & 0x0070) >> 4;
					if (disk_index < 4)
						kstat.dk_drive[disk_index]++;
					break;
		case HD_MAJOR:
		case XT_DISK_MAJOR:	disk_index = (MINOR(req->dev) & 0x0040) >> 6;
					kstat.dk_drive[disk_index]++;
					break;
		case IDE1_MAJOR:	disk_index = ((MINOR(req->dev) & 0x0040) >> 6) + 2;
					kstat.dk_drive[disk_index]++;
		default:		break;
	}

	req->next = NULL;
	cli();
	if (req->bh)
		mark_buffer_clean(req->bh);
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

/* for SCSI devices, call request_fn unconditionally */
	if (scsi_major(MAJOR(req->dev)))
		(dev->request_fn)();

	sti();
}

static void make_request(int major,int rw, struct buffer_head * bh)
{
	unsigned int sector, count;
	struct request * req;
	int rw_ahead, max_req;

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
			bh->b_req = 0;
			return;
		}
	/* Uhhuh.. Nasty dead-lock possible here.. */
	if (bh->b_lock)
		return;
	/* Maybe the above fixes it, and maybe it doesn't boot. Life is interesting */
	lock_buffer(bh);
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}

/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
	max_req = (rw == READ) ? NR_REQUEST : ((NR_REQUEST*2)/3);

/* look for a free request. */
	cli();

/* The scsi disk drivers and the IDE driver completely remove the request
 * from the queue when they start processing an entry.  For this reason
 * it is safe to continue to add links to the top entry for those devices.
 */
	if ((   major == IDE0_MAJOR	/* same as HD_MAJOR */
	     || major == IDE1_MAJOR
	     || major == FLOPPY_MAJOR
	     || major == SCSI_DISK_MAJOR
	     || major == SCSI_CDROM_MAJOR)
	    && (req = blk_dev[major].current_request))
	{
#ifdef CONFIG_BLK_DEV_HD
	        if (major == HD_MAJOR || major == FLOPPY_MAJOR)
#else
		if (major == FLOPPY_MAJOR)
#endif CONFIG_BLK_DEV_HD
			req = req->next;
		while (req) {
			if (req->dev == bh->b_dev &&
			    !req->sem &&
			    req->cmd == rw &&
			    req->sector + req->nr_sectors == sector &&
			    req->nr_sectors < 244)
			{
				req->bhtail->b_reqnext = bh;
				req->bhtail = bh;
				req->nr_sectors += count;
				mark_buffer_clean(bh);
				sti();
				return;
			}

			if (req->dev == bh->b_dev &&
			    !req->sem &&
			    req->cmd == rw &&
			    req->sector - count == sector &&
			    req->nr_sectors < 244)
			{
			    	req->nr_sectors += count;
			    	bh->b_reqnext = req->bh;
			    	req->buffer = bh->b_data;
			    	req->current_nr_sectors = count;
			    	req->sector = sector;
				mark_buffer_clean(bh);
			    	req->bh = bh;
			    	sti();
			    	return;
			}    

			req = req->next;
		}
	}

/* find an unused request. */
	req = get_request(max_req, bh->b_dev);
	sti();

/* if no request available: if rw_ahead, forget it; otherwise try again blocking.. */
	if (!req) {
		if (rw_ahead) {
			unlock_buffer(bh);
			return;
		}
		req = __get_request_wait(max_req, bh->b_dev);
	}

/* fill up the request-info, and add it to the queue */
	req->cmd = rw;
	req->errors = 0;
	req->sector = sector;
	req->nr_sectors = count;
	req->current_nr_sectors = count;
	req->buffer = bh->b_data;
	req->sem = NULL;
	req->bh = bh;
	req->bhtail = bh;
	req->next = NULL;
	add_request(major+blk_dev,req);
}

void ll_rw_page(int rw, int dev, int page, char * buffer)
{
	struct request * req;
	unsigned int major = MAJOR(dev);
	struct semaphore sem = MUTEX_LOCKED;

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
	req = get_request_wait(NR_REQUEST, dev);
/* fill up the request-info, and add it to the queue */
	req->cmd = rw;
	req->errors = 0;
	req->sector = page<<3;
	req->nr_sectors = 8;
	req->current_nr_sectors = 8;
	req->buffer = buffer;
	req->sem = &sem;
	req->bh = NULL;
	req->next = NULL;
	add_request(major+blk_dev,req);
	down(&sem);
}

/* This function can be used to request a number of buffers from a block
   device. Currently the only restriction is that all buffers must belong to
   the same device */

void ll_rw_block(int rw, int nr, struct buffer_head * bh[])
{
	unsigned int major;
	struct request plug;
	int correct_size;
	struct blk_dev_struct * dev;
	int i;

	/* Make sure that the first block contains something reasonable */
	while (!*bh) {
		bh++;
		if (--nr <= 0)
			return;
	};

	dev = NULL;
	if ((major = MAJOR(bh[0]->b_dev)) < MAX_BLKDEV)
		dev = blk_dev + major;
	if (!dev || !dev->request_fn) {
		printk(
	"ll_rw_block: Trying to read nonexistent block-device %04lX (%ld)\n",
		       (unsigned long) bh[0]->b_dev, bh[0]->b_blocknr);
		goto sorry;
	}

	/* Determine correct block size for this device.  */
	correct_size = BLOCK_SIZE;
	if (blksize_size[major]) {
		i = blksize_size[major][MINOR(bh[0]->b_dev)];
		if (i)
			correct_size = i;
	}

	/* Verify requested block sizes.  */
	for (i = 0; i < nr; i++) {
		if (bh[i] && bh[i]->b_size != correct_size) {
			printk(
			"ll_rw_block: only %d-char blocks implemented (%lu)\n",
			       correct_size, bh[i]->b_size);
			goto sorry;
		}
	}

	if ((rw == WRITE || rw == WRITEA) && is_read_only(bh[0]->b_dev)) {
		printk("Can't write to read-only device 0x%X\n",bh[0]->b_dev);
		goto sorry;
	}

	/* If there are no pending requests for this device, then we insert
	   a dummy request for that device.  This will prevent the request
	   from starting until we have shoved all of the blocks into the
	   queue, and then we let it rip.  */

	if (nr > 1)
		plug_device(dev, &plug);
	for (i = 0; i < nr; i++) {
		if (bh[i]) {
			bh[i]->b_req = 1;
			make_request(major, rw, bh[i]);
			if (rw == READ || rw == READA)
				kstat.pgpgin++;
			else
				kstat.pgpgout++;
		}
	}
	unplug_device(dev);
	return;

      sorry:
	for (i = 0; i < nr; i++) {
		if (bh[i])
			bh[i]->b_dirt = bh[i]->b_uptodate = 0;
	}
	return;
}

void ll_rw_swap_file(int rw, int dev, unsigned int *b, int nb, char *buf)
{
	int i;
	int buffersize;
	struct request * req;
	unsigned int major = MAJOR(dev);
	struct semaphore sem = MUTEX_LOCKED;

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
		req = get_request_wait(NR_REQUEST, dev);
		req->cmd = rw;
		req->errors = 0;
		req->sector = (b[i] * buffersize) >> 9;
		req->nr_sectors = buffersize >> 9;
		req->current_nr_sectors = buffersize >> 9;
		req->buffer = buf;
		req->sem = &sem;
		req->bh = NULL;
		req->next = NULL;
		add_request(major+blk_dev,req);
		down(&sem);
	}
}

long blk_dev_init(long mem_start, long mem_end)
{
	struct request * req;

	req = all_requests + NR_REQUEST;
	while (--req >= all_requests) {
		req->dev = -1;
		req->next = NULL;
	}
	memset(ro_bits,0,sizeof(ro_bits));
#ifdef CONFIG_BLK_DEV_HD
	mem_start = hd_init(mem_start,mem_end);
#endif
#ifdef CONFIG_BLK_DEV_IDE
	mem_start = ide_init(mem_start,mem_end);
#endif
#ifdef CONFIG_BLK_DEV_XD
	mem_start = xd_init(mem_start,mem_end);
#endif
#ifdef CONFIG_CDU31A
	mem_start = cdu31a_init(mem_start,mem_end);
#endif
#ifdef CONFIG_CDU535
	mem_start = sony535_init(mem_start,mem_end);
#endif
#ifdef CONFIG_MCD
	mem_start = mcd_init(mem_start,mem_end);
#endif
#ifdef CONFIG_AZTCD
        mem_start = aztcd_init(mem_start,mem_end);
#endif
#ifdef CONFIG_BLK_DEV_FD
	floppy_init();
#else
	outb_p(0xc, 0x3f2);
#endif
#ifdef CONFIG_SBPCD
	mem_start = sbpcd_init(mem_start, mem_end);
#endif CONFIG_SBPCD
	if (ramdisk_size)
		mem_start += rd_init(mem_start, ramdisk_size*1024);
	return mem_start;
}
