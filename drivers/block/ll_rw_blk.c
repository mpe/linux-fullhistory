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
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <linux/blk.h>

#include <linux/module.h>

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
static struct request all_requests[NR_REQUEST];

/*
 * The "disk" task queue is used to start the actual requests
 * after a plug
 */
DECLARE_TASK_QUEUE(tq_disk);

/*
 * Protect the request list against multiple users..
 *
 * With this spinlock the Linux block IO subsystem is 100% SMP threaded
 * from the IRQ event side, and almost 100% SMP threaded from the syscall
 * side (we still have protect against block device array operations, and
 * the do_request() side is casually still unsafe. The kernel lock protects
 * this part currently.).
 *
 * there is a fair chance that things will work just OK if these functions
 * are called with no global kernel lock held ...
 */
spinlock_t io_request_lock = SPIN_LOCK_UNLOCKED;

/*
 * used to wait on when there are no free requests
 */
struct wait_queue * wait_for_request = NULL;

/* This specifies how many sectors to read ahead on the disk.  */

int read_ahead[MAX_BLKDEV] = {0, };

/* blk_dev_struct is:
 *	*request_fn
 *	*current_request
 */
struct blk_dev_struct blk_dev[MAX_BLKDEV]; /* initialized by blk_dev_init() */

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
 * This is currently set by some scsi devices and read by the msdos fs driver.
 * Other uses may appear later.
 */
int * hardsect_size[MAX_BLKDEV] = { NULL, NULL, };

/*
 * The following tunes the read-ahead algorithm in mm/filemap.c
 */
int * max_readahead[MAX_BLKDEV] = { NULL, NULL, };

/*
 * Max number of sectors per request
 */
int * max_sectors[MAX_BLKDEV] = { NULL, NULL, };

static inline int get_max_sectors(kdev_t dev)
{
	if (!max_sectors[MAJOR(dev)])
		return MAX_SECTORS;
	return max_sectors[MAJOR(dev)][MINOR(dev)];
}

/*
 * Is called with the request spinlock aquired.
 * NOTE: the device-specific queue() functions
 * have to be atomic!
 */
static inline struct request **get_queue(kdev_t dev)
{
	int major = MAJOR(dev);
	struct blk_dev_struct *bdev = blk_dev + major;

	if (bdev->queue)
		return bdev->queue(dev);
	return &blk_dev[major].current_request;
}

/*
 * remove the plug and let it rip..
 */
void unplug_device(void * data)
{
	struct blk_dev_struct * dev = (struct blk_dev_struct *) data;
	int queue_new_request=0;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock,flags);
	if (dev->current_request == &dev->plug) {
		struct request * next = dev->plug.next;
		dev->current_request = next;
		if (next || dev->queue) {
			dev->plug.next = NULL;
			queue_new_request = 1;
		}
	}
	if (queue_new_request)
		(dev->request_fn)();

	spin_unlock_irqrestore(&io_request_lock,flags);
}

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 *
 * This is called with interrupts off and no requests on the queue.
 * (and with the request spinlock aquired)
 */
static inline void plug_device(struct blk_dev_struct * dev)
{
	if (dev->current_request)
		return;
	dev->current_request = &dev->plug;
	queue_task(&dev->plug_tq, &tq_disk);
}

/*
 * look for a free request in the first N entries.
 * NOTE: interrupts must be disabled on the way in (on SMP the request queue
 * spinlock has to be aquired), and will still be disabled on the way out.
 */
static inline struct request * get_request(int n, kdev_t dev)
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
		if (req->rq_status == RQ_INACTIVE)
			break;
		if (req == prev_found)
			return NULL;
	}
	prev_found = req;
	req->rq_status = RQ_ACTIVE;
	req->rq_dev = dev;
	return req;
}

/*
 * wait until a free request in the first N entries is available.
 */
static struct request * __get_request_wait(int n, kdev_t dev)
{
	register struct request *req;
	struct wait_queue wait = { current, NULL };
	unsigned long flags;

	add_wait_queue(&wait_for_request, &wait);
	for (;;) {
		current->state = TASK_UNINTERRUPTIBLE;
		spin_lock_irqsave(&io_request_lock,flags);
		req = get_request(n, dev);
		spin_unlock_irqrestore(&io_request_lock,flags);
		if (req)
			break;
		run_task_queue(&tq_disk);
		schedule();
	}
	remove_wait_queue(&wait_for_request, &wait);
	current->state = TASK_RUNNING;
	return req;
}

static inline struct request * get_request_wait(int n, kdev_t dev)
{
	register struct request *req;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock,flags);
	req = get_request(n, dev);
	spin_unlock_irqrestore(&io_request_lock,flags);
	if (req)
		return req;
	return __get_request_wait(n, dev);
}

/* RO fail safe mechanism */

static long ro_bits[MAX_BLKDEV][8];

int is_read_only(kdev_t dev)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return 0;
	return ro_bits[major][minor >> 5] & (1 << (minor & 31));
}

void set_device_ro(kdev_t dev,int flag)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return;
	if (flag) ro_bits[major][minor >> 5] |= 1 << (minor & 31);
	else ro_bits[major][minor >> 5] &= ~(1 << (minor & 31));
}

static inline void drive_stat_acct(int cmd, unsigned long nr_sectors,
                                   short disk_index)
{
	kstat.dk_drive[disk_index]++;
	if (cmd == READ) {
		kstat.dk_drive_rio[disk_index]++;
		kstat.dk_drive_rblk[disk_index] += nr_sectors;
	} else if (cmd == WRITE) {
		kstat.dk_drive_wio[disk_index]++;
		kstat.dk_drive_wblk[disk_index] += nr_sectors;
	} else
		printk(KERN_ERR "drive_stat_acct: cmd not R/W?\n");
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts (aquires the request spinlock) so that it can muck
 * with the request-lists in peace. Thus it should be called with no spinlocks
 * held.
 *
 * By this point, req->cmd is always either READ/WRITE, never READA/WRITEA,
 * which is important for drive_stat_acct() above.
 */

void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp, **current_request;
	short		 disk_index;
	unsigned long flags;
	int queue_new_request = 0;

	switch (MAJOR(req->rq_dev)) {
		case SCSI_DISK0_MAJOR:
			disk_index = (MINOR(req->rq_dev) & 0x00f0) >> 4;
			if (disk_index < 4)
				drive_stat_acct(req->cmd, req->nr_sectors, disk_index);
			break;
		case IDE0_MAJOR:	/* same as HD_MAJOR */
		case XT_DISK_MAJOR:
			disk_index = (MINOR(req->rq_dev) & 0x0040) >> 6;
			drive_stat_acct(req->cmd, req->nr_sectors, disk_index);
			break;
		case IDE1_MAJOR:
			disk_index = ((MINOR(req->rq_dev) & 0x0040) >> 6) + 2;
			drive_stat_acct(req->cmd, req->nr_sectors, disk_index);
		default:
			break;
	}

	req->next = NULL;

	/*
	 * We use the goto to reduce locking complexity
	 */
	spin_lock_irqsave(&io_request_lock,flags);
	current_request = get_queue(req->rq_dev);

	if (req->bh)
		mark_buffer_clean(req->bh);
	if (!(tmp = *current_request)) {
		*current_request = req;
		if (dev->current_request != &dev->plug)
			queue_new_request = 1;
		goto out;
	}
	for ( ; tmp->next ; tmp = tmp->next) {
		const int after_current = IN_ORDER(tmp,req);
		const int before_next = IN_ORDER(req,tmp->next);

		if (!IN_ORDER(tmp,tmp->next)) {
			if (after_current || before_next)
				break;
		} else {
			if (after_current && before_next)
				break;
		}
	}
	req->next = tmp->next;
	tmp->next = req;

/* for SCSI devices, call request_fn unconditionally */
	if (scsi_blk_major(MAJOR(req->rq_dev)))
		queue_new_request = 1;
out:
	if (queue_new_request)
		(dev->request_fn)();
	spin_unlock_irqrestore(&io_request_lock,flags);
}

/*
 * Has to be called with the request spinlock aquired
 */
static inline void attempt_merge (struct request *req, int max_sectors)
{
	struct request *next = req->next;

	if (!next)
		return;
	if (req->sector + req->nr_sectors != next->sector)
		return;
	if (next->sem || req->cmd != next->cmd || req->rq_dev != next->rq_dev || req->nr_sectors + next->nr_sectors > max_sectors)
		return;
	req->bhtail->b_reqnext = next->bh;
	req->bhtail = next->bhtail;
	req->nr_sectors += next->nr_sectors;
	next->rq_status = RQ_INACTIVE;
	req->next = next->next;
	wake_up (&wait_for_request);
}

void make_request(int major,int rw, struct buffer_head * bh)
{
	unsigned int sector, count;
	struct request * req;
	int rw_ahead, max_req, max_sectors;
	unsigned long flags;

	count = bh->b_size >> 9;
	sector = bh->b_rsector;

	/* Uhhuh.. Nasty dead-lock possible here.. */
	if (buffer_locked(bh))
		return;
	/* Maybe the above fixes it, and maybe it doesn't boot. Life is interesting */

	lock_buffer(bh);

	if (blk_size[major])
		if (blk_size[major][MINOR(bh->b_rdev)] < (sector + count)>>1) {
			bh->b_state &= (1 << BH_Lock);
                        /* This may well happen - the kernel calls bread()
                           without checking the size of the device, e.g.,
                           when mounting a device. */
			printk(KERN_INFO
                               "attempt to access beyond end of device\n");
			printk(KERN_INFO "%s: rw=%d, want=%d, limit=%d\n",
                               kdevname(bh->b_rdev), rw,
                               (sector + count)>>1,
                               blk_size[major][MINOR(bh->b_rdev)]);
			goto end_io;
		}

	rw_ahead = 0;	/* normal case; gets changed below for READA/WRITEA */
	switch (rw) {
		case READA:
			rw_ahead = 1;
			rw = READ;	/* drop into READ */
		case READ:
			if (buffer_uptodate(bh)) /* Hmmph! Already have it */
				goto end_io;
			kstat.pgpgin++;
			max_req = NR_REQUEST;	/* reads take precedence */
			break;
		case WRITEA:
			rw_ahead = 1;
			rw = WRITE;	/* drop into WRITE */
		case WRITE:
			if (!buffer_dirty(bh))   /* Hmmph! Nothing to write */
				goto end_io;
			/* We don't allow the write-requests to fill up the
			 * queue completely:  we want some room for reads,
			 * as they take precedence. The last third of the
			 * requests are only for reads.
			 */
			kstat.pgpgout++;
			max_req = (NR_REQUEST * 2) / 3;
			break;
		default:
			printk(KERN_ERR "make_request: bad block dev cmd,"
                               " must be R/W/RA/WA\n");
			goto end_io;
	}

/* look for a free request. */
       /* Loop uses two requests, 1 for loop and 1 for the real device.
        * Cut max_req in half to avoid running out and deadlocking. */
	 if ((major == LOOP_MAJOR) || (major == NBD_MAJOR))
	     max_req >>= 1;

	/*
	 * Try to coalesce the new request with old requests
	 */
	max_sectors = get_max_sectors(bh->b_rdev);

	/*
	 * Now we acquire the request spinlock, we have to be mega careful
	 * not to schedule or do something nonatomic
	 */
	spin_lock_irqsave(&io_request_lock,flags);
	req = *get_queue(bh->b_rdev);
	if (!req) {
		/* MD and loop can't handle plugging without deadlocking */
		if (major != MD_MAJOR && major != LOOP_MAJOR && 
		    major != DDV_MAJOR && major != NBD_MAJOR)
			plug_device(blk_dev + major); /* is atomic */
	} else switch (major) {
	     case IDE0_MAJOR:	/* same as HD_MAJOR */
	     case IDE1_MAJOR:
	     case FLOPPY_MAJOR:
	     case IDE2_MAJOR:
	     case IDE3_MAJOR:
	     case IDE4_MAJOR:
	     case IDE5_MAJOR:
	     case ACSI_MAJOR:
	     case MFM_ACORN_MAJOR:
		/*
		 * The scsi disk and cdrom drivers completely remove the request
		 * from the queue when they start processing an entry.  For this
		 * reason it is safe to continue to add links to the top entry for
		 * those devices.
		 *
		 * All other drivers need to jump over the first entry, as that
		 * entry may be busy being processed and we thus can't change it.
		 */
		if (req == blk_dev[major].current_request)
	        	req = req->next;
		if (!req)
			break;
		/* fall through */

	     case SCSI_DISK0_MAJOR:
	     case SCSI_DISK1_MAJOR:
	     case SCSI_DISK2_MAJOR:
	     case SCSI_DISK3_MAJOR:
	     case SCSI_DISK4_MAJOR:
	     case SCSI_DISK5_MAJOR:
	     case SCSI_DISK6_MAJOR:
	     case SCSI_DISK7_MAJOR:
	     case SCSI_CDROM_MAJOR:

		do {
			if (req->sem)
				continue;
			if (req->cmd != rw)
				continue;
			if (req->nr_sectors + count > max_sectors)
				continue;
			if (req->rq_dev != bh->b_rdev)
				continue;
			/* Can we add it to the end of this request? */
			if (req->sector + req->nr_sectors == sector) {
				req->bhtail->b_reqnext = bh;
				req->bhtail = bh;
			    	req->nr_sectors += count;
				/* Can we now merge this req with the next? */
				attempt_merge(req, max_sectors);
			/* or to the beginning? */
			} else if (req->sector - count == sector) {
			    	bh->b_reqnext = req->bh;
			    	req->bh = bh;
			    	req->buffer = bh->b_data;
			    	req->current_nr_sectors = count;
			    	req->sector = sector;
			    	req->nr_sectors += count;
			} else
				continue;

			mark_buffer_clean(bh);
			spin_unlock_irqrestore(&io_request_lock,flags);
		    	return;

		} while ((req = req->next) != NULL);
	}

/* find an unused request. */
	req = get_request(max_req, bh->b_rdev);

	spin_unlock_irqrestore(&io_request_lock,flags);

/* if no request available: if rw_ahead, forget it; otherwise try again blocking.. */
	if (!req) {
		if (rw_ahead)
			goto end_io;
		req = __get_request_wait(max_req, bh->b_rdev);
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
	return;

end_io:
	bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
}

/* This function can be used to request a number of buffers from a block
   device. Currently the only restriction is that all buffers must belong to
   the same device */

void ll_rw_block(int rw, int nr, struct buffer_head * bh[])
{
	unsigned int major;
	int correct_size;
	struct blk_dev_struct * dev;
	int i;

	/* Make sure that the first block contains something reasonable */
	while (!*bh) {
		bh++;
		if (--nr <= 0)
			return;
	}

	dev = NULL;
	if ((major = MAJOR(bh[0]->b_dev)) < MAX_BLKDEV)
		dev = blk_dev + major;
	if (!dev || !dev->request_fn) {
		printk(KERN_ERR
	"ll_rw_block: Trying to read nonexistent block-device %s (%ld)\n",
		kdevname(bh[0]->b_dev), bh[0]->b_blocknr);
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
			printk(KERN_NOTICE "ll_rw_block: device %s: "
			       "only %d-char blocks implemented (%lu)\n",
			       kdevname(bh[0]->b_dev),
			       correct_size, bh[i]->b_size);
			goto sorry;
		}

		/* Md remaps blocks now */
		bh[i]->b_rdev = bh[i]->b_dev;
		bh[i]->b_rsector=bh[i]->b_blocknr*(bh[i]->b_size >> 9);
#ifdef CONFIG_BLK_DEV_MD
		if (major==MD_MAJOR &&
		    md_map (MINOR(bh[i]->b_dev), &bh[i]->b_rdev,
			    &bh[i]->b_rsector, bh[i]->b_size >> 9)) {
		        printk (KERN_ERR
				"Bad md_map in ll_rw_block\n");
		        goto sorry;
		}
#endif
	}

	if ((rw == WRITE || rw == WRITEA) && is_read_only(bh[0]->b_dev)) {
		printk(KERN_NOTICE "Can't write to read-only device %s\n",
		       kdevname(bh[0]->b_dev));
		goto sorry;
	}

	for (i = 0; i < nr; i++) {
		if (bh[i]) {
			set_bit(BH_Req, &bh[i]->b_state);
#ifdef CONFIG_BLK_DEV_MD
			if (MAJOR(bh[i]->b_dev) == MD_MAJOR) {
				md_make_request(MINOR (bh[i]->b_dev), rw, bh[i]);
				continue;
			}
#endif
			make_request(MAJOR(bh[i]->b_rdev), rw, bh[i]);
		}
	}
	return;

      sorry:
	for (i = 0; i < nr; i++) {
		if (bh[i]) {
			clear_bit(BH_Dirty, &bh[i]->b_state);
			clear_bit(BH_Uptodate, &bh[i]->b_state);
			bh[i]->b_end_io(bh[i], 0);
		}
	}
	return;
}

#ifdef CONFIG_STRAM_SWAP
extern int stram_device_init( void );
#endif

/*
 * First step of what used to be end_request
 *
 * 0 means continue with end_that_request_last,
 * 1 means we are done
 */

int 
end_that_request_first( struct request *req, int uptodate, char *name ) 
{
	struct buffer_head * bh;
	int nsect;

	req->errors = 0;
	if (!uptodate) {
		printk("end_request: I/O error, dev %s (%s), sector %lu\n",
			kdevname(req->rq_dev), name, req->sector);
		if ((bh = req->bh) != NULL) {
			nsect = bh->b_size >> 9;
			req->nr_sectors--;
			req->nr_sectors &= ~(nsect - 1);
			req->sector += nsect;
			req->sector &= ~(nsect - 1);
		}
	}

	if ((bh = req->bh) != NULL) {
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_end_io(bh, uptodate);
		if ((bh = req->bh) != NULL) {
			req->current_nr_sectors = bh->b_size >> 9;
			if (req->nr_sectors < req->current_nr_sectors) {
				req->nr_sectors = req->current_nr_sectors;
				printk("end_request: buffer-list destroyed\n");
			}
			req->buffer = bh->b_data;
			return 1;
		}
	}
	return 0;
}

void
end_that_request_last( struct request *req ) 
{
	if (req->sem != NULL)
		up(req->sem);
	req->rq_status = RQ_INACTIVE;
	wake_up(&wait_for_request);
}

__initfunc(int blk_dev_init(void))
{
	struct request * req;
	struct blk_dev_struct *dev;

	for (dev = blk_dev + MAX_BLKDEV; dev-- != blk_dev;) {
		dev->request_fn      = NULL;
		dev->queue           = NULL;
		dev->current_request = NULL;
		dev->plug.rq_status  = RQ_INACTIVE;
		dev->plug.cmd        = -1;
		dev->plug.next       = NULL;
		dev->plug_tq.sync    = 0;
		dev->plug_tq.routine = &unplug_device;
		dev->plug_tq.data    = dev;
	}

	req = all_requests + NR_REQUEST;
	while (--req >= all_requests) {
		req->rq_status = RQ_INACTIVE;
		req->next = NULL;
	}
	memset(ro_bits,0,sizeof(ro_bits));
	memset(max_readahead, 0, sizeof(max_readahead));
	memset(max_sectors, 0, sizeof(max_sectors));
#ifdef CONFIG_AMIGA_Z2RAM
	z2_init();
#endif
#ifdef CONFIG_STRAM_SWAP
	stram_device_init();
#endif
#ifdef CONFIG_BLK_DEV_RAM
	rd_init();
#endif
#ifdef CONFIG_BLK_DEV_LOOP
	loop_init();
#endif
#ifdef CONFIG_ISP16_CDI
	isp16_init();
#endif CONFIG_ISP16_CDI
#ifdef CONFIG_BLK_DEV_IDE
	ide_init();		/* this MUST precede hd_init */
#endif
#ifdef CONFIG_BLK_DEV_HD
	hd_init();
#endif
#ifdef CONFIG_BLK_DEV_PS2
	ps2esdi_init();
#endif
#ifdef CONFIG_BLK_DEV_XD
	xd_init();
#endif
#ifdef CONFIG_BLK_DEV_MFM
	mfm_init();
#endif
#ifdef CONFIG_PARIDE
	{ extern void paride_init(void); paride_init(); };
#endif
#ifdef CONFIG_MAC_FLOPPY
	swim3_init();
#endif
#ifdef CONFIG_AMIGA_FLOPPY
	amiga_floppy_init();
#endif
#ifdef CONFIG_ATARI_FLOPPY
	atari_floppy_init();
#endif
#ifdef CONFIG_BLK_DEV_FD
	floppy_init();
#else
#if !defined (__mc68000__) && !defined(CONFIG_PMAC) && !defined(__sparc__)\
    && !defined(CONFIG_APUS)
	outb_p(0xc, 0x3f2);
#endif
#endif
#ifdef CONFIG_CDU31A
	cdu31a_init();
#endif CONFIG_CDU31A
#ifdef CONFIG_ATARI_ACSI
	acsi_init();
#endif CONFIG_ATARI_ACSI
#ifdef CONFIG_MCD
	mcd_init();
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
	mcdx_init();
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
	sbpcd_init();
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
        aztcd_init();
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
	sony535_init();
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
	gscd_init();
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
	cm206_init();
#endif
#ifdef CONFIG_OPTCD
	optcd_init();
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
	sjcd_init();
#endif CONFIG_SJCD
#ifdef CONFIG_BLK_DEV_MD
	md_init();
#endif CONFIG_BLK_DEV_MD
#ifdef CONFIG_APBLOCK
	ap_init();
#endif
#ifdef CONFIG_DDV
	ddv_init();
#endif
#ifdef CONFIG_BLK_DEV_NBD
	nbd_init();
#endif
	return 0;
};

EXPORT_SYMBOL(io_request_lock);
EXPORT_SYMBOL(end_that_request_first);
EXPORT_SYMBOL(end_that_request_last);
