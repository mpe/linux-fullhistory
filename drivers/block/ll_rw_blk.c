/*
 *  linux/drivers/block/ll_rw_blk.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 * Elevator latency, (C) 2000  Andrea Arcangeli <andrea@suse.de> SuSE
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
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/io.h>
#include <linux/blk.h>
#include <linux/highmem.h>
#include <linux/raid/md.h>

#include <linux/module.h>

/*
 * MAC Floppy IWM hooks
 */

#ifdef CONFIG_MAC_FLOPPY_IWM
extern int mac_floppy_init(void);
#endif

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
DECLARE_WAIT_QUEUE_HEAD(wait_for_request);

/* This specifies how many sectors to read ahead on the disk. */

int read_ahead[MAX_BLKDEV];

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
int * blk_size[MAX_BLKDEV];

/*
 * blksize_size contains the size of all block-devices:
 *
 * blksize_size[MAJOR][MINOR]
 *
 * if (!blksize_size[MAJOR]) then 1024 bytes is assumed.
 */
int * blksize_size[MAX_BLKDEV];

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
int * hardsect_size[MAX_BLKDEV];

/*
 * The following tunes the read-ahead algorithm in mm/filemap.c
 */
int * max_readahead[MAX_BLKDEV];

/*
 * Max number of sectors per request
 */
int * max_sectors[MAX_BLKDEV];

static inline int get_max_sectors(kdev_t dev)
{
	if (!max_sectors[MAJOR(dev)])
		return MAX_SECTORS;
	return max_sectors[MAJOR(dev)][MINOR(dev)];
}

/*
 * NOTE: the device-specific queue() functions
 * have to be atomic!
 */
request_queue_t * blk_get_queue (kdev_t dev)
{
	int major = MAJOR(dev);
	struct blk_dev_struct *bdev = blk_dev + major;
	unsigned long flags;
	request_queue_t *ret;

	spin_lock_irqsave(&io_request_lock,flags);
	if (bdev->queue)
		ret = bdev->queue(dev);
	else
		ret = &blk_dev[major].request_queue;
	spin_unlock_irqrestore(&io_request_lock,flags);

	return ret;
}

void blk_cleanup_queue(request_queue_t * q)
{
	memset(q, 0, sizeof(*q));
}

void blk_queue_headactive(request_queue_t * q, int active)
{
	q->head_active = active;
}

void blk_queue_pluggable (request_queue_t * q, plug_device_fn *plug)
{
	q->plug_device_fn = plug;
}

void blk_queue_make_request(request_queue_t * q, make_request_fn * mfn)
{
	q->make_request_fn = mfn;
}

static inline int ll_new_segment(request_queue_t *q, struct request *req, int max_segments)
{
	if (req->nr_segments < max_segments) {
		req->nr_segments++;
		q->elevator.nr_segments++;
		return 1;
	}
	return 0;
}

static int ll_back_merge_fn(request_queue_t *q, struct request *req, 
			    struct buffer_head *bh, int max_segments)
{
	if (req->bhtail->b_data + req->bhtail->b_size == bh->b_data)
		return 1;
	return ll_new_segment(q, req, max_segments);
}

static int ll_front_merge_fn(request_queue_t *q, struct request *req, 
			     struct buffer_head *bh, int max_segments)
{
	if (bh->b_data + bh->b_size == req->bh->b_data)
		return 1;
	return ll_new_segment(q, req, max_segments);
}

static int ll_merge_requests_fn(request_queue_t *q, struct request *req,
				struct request *next, int max_segments)
{
	int total_segments = req->nr_segments + next->nr_segments;
	int same_segment;

	same_segment = 0;
	if (req->bhtail->b_data + req->bhtail->b_size == next->bh->b_data) {
		total_segments--;
		same_segment = 1;
	}
    
	if (total_segments > max_segments)
		return 0;

	q->elevator.nr_segments -= same_segment;
	req->nr_segments = total_segments;
	return 1;
}

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 *
 * This is called with interrupts off and no requests on the queue.
 * (and with the request spinlock aquired)
 */
static void generic_plug_device (request_queue_t *q, kdev_t dev)
{
#ifdef CONFIG_BLK_DEV_MD
	if (MAJOR(dev) == MD_MAJOR) {
		spin_unlock_irq(&io_request_lock);
		BUG();
	}
#endif
	if (!list_empty(&q->queue_head))
		return;

	q->plugged = 1;
	queue_task(&q->plug_tq, &tq_disk);
}

void blk_init_queue(request_queue_t * q, request_fn_proc * rfn)
{
	INIT_LIST_HEAD(&q->queue_head);
	elevator_init(&q->elevator);
	q->request_fn     	= rfn;
	q->back_merge_fn       	= ll_back_merge_fn;
	q->front_merge_fn      	= ll_front_merge_fn;
	q->merge_requests_fn	= ll_merge_requests_fn;
	q->make_request_fn	= NULL;
	q->plug_tq.sync   	= 0;
	q->plug_tq.routine	= &generic_unplug_device;
	q->plug_tq.data   	= q;
	q->plugged        	= 0;
	/*
	 * These booleans describe the queue properties.  We set the
	 * default (and most common) values here.  Other drivers can
	 * use the appropriate functions to alter the queue properties.
	 * as appropriate.
	 */
	q->plug_device_fn 	= generic_plug_device;
	q->head_active    	= 1;
}

/*
 * remove the plug and let it rip..
 */
void generic_unplug_device(void * data)
{
	request_queue_t * q = (request_queue_t *) data;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock,flags);
	if (q->plugged) {
		q->plugged = 0;
		if (!list_empty(&q->queue_head))
			(q->request_fn)(q);
	}
	spin_unlock_irqrestore(&io_request_lock,flags);
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
	req->special = NULL;
	return req;
}

/*
 * wait until a free request in the first N entries is available.
 */
static struct request * __get_request_wait(int n, kdev_t dev)
{
	register struct request *req;
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;

	add_wait_queue_exclusive(&wait_for_request, &wait);
	for (;;) {
		__set_current_state(TASK_UNINTERRUPTIBLE|TASK_EXCLUSIVE);
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

inline void drive_stat_acct (kdev_t dev, int rw,
				unsigned long nr_sectors, int new_io)
{
	unsigned int major = MAJOR(dev);
	unsigned int index;

	index = disk_index(dev);
	if ((index >= DK_MAX_DISK) || (major >= DK_MAX_MAJOR))
		return;

	kstat.dk_drive[major][index] += new_io;
	if (rw == READ) {
		kstat.dk_drive_rio[major][index] += new_io;
		kstat.dk_drive_rblk[major][index] += nr_sectors;
	} else if (rw == WRITE) {
		kstat.dk_drive_wio[major][index] += new_io;
		kstat.dk_drive_wblk[major][index] += nr_sectors;
	} else
		printk(KERN_ERR "drive_stat_acct: cmd not R/W?\n");
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts (aquires the request spinlock) so that it can muck
 * with the request-lists in peace. Thus it should be called with no spinlocks
 * held.
 *
 * By this point, req->cmd is always either READ/WRITE, never READA,
 * which is important for drive_stat_acct() above.
 */

static inline void add_request(request_queue_t * q, struct request * req,
			       struct list_head * head, int latency)
{
	int major;

	drive_stat_acct(req->rq_dev, req->cmd, req->nr_sectors, 1);

	elevator_account_request(&q->elevator, req);
	if (list_empty(head)) {
		req->elevator_sequence = elevator_sequence(&q->elevator, latency);
		list_add(&req->queue, &q->queue_head);
		return;
	}
	q->elevator.elevator_fn(req, &q->elevator, &q->queue_head, head, latency);

	/*
	 * FIXME(eric) I don't understand why there is a need for this
	 * special case code.  It clearly doesn't fit any more with
	 * the new queueing architecture, and it got added in 2.3.10.
	 * I am leaving this in here until I hear back from the COMPAQ
	 * people.
	 */
	major = MAJOR(req->rq_dev);
	if (major >= COMPAQ_SMART2_MAJOR+0 && major <= COMPAQ_SMART2_MAJOR+7)
	{
		(q->request_fn)(q);
	}

	if (major >= DAC960_MAJOR+0 && major <= DAC960_MAJOR+7)
	{
		(q->request_fn)(q);
	}
}

/*
 * Has to be called with the request spinlock aquired
 */
static void attempt_merge(request_queue_t * q,
			  struct request *req,
			  int max_sectors,
			  int max_segments)
{
	struct request *next;
  
	next = blkdev_next_request(req);
	if (req->sector + req->nr_sectors != next->sector)
		return;
	if (req->cmd != next->cmd || req->rq_dev != next->rq_dev || req->nr_sectors + next->nr_sectors > max_sectors || next->sem)
		return;
	/*
	 * If we are not allowed to merge these requests, then
	 * return.  If we are allowed to merge, then the count
	 * will have been updated to the appropriate number,
	 * and we shouldn't do it here too.
	 */
	if(!(q->merge_requests_fn)(q, req, next, max_segments))
		return;

	elevator_merge_requests(&q->elevator, req, next);
	req->bhtail->b_reqnext = next->bh;
	req->bhtail = next->bhtail;
	req->nr_sectors = req->hard_nr_sectors += next->hard_nr_sectors;
	next->rq_status = RQ_INACTIVE;
	list_del(&next->queue);
	wake_up (&wait_for_request);
}

static inline void attempt_back_merge(request_queue_t * q,
				      struct request *req,
				      int max_sectors,
				      int max_segments)
{
	if (&req->queue == q->queue_head.prev)
		return;
	attempt_merge(q, req, max_sectors, max_segments);
}

static inline void attempt_front_merge(request_queue_t * q,
				       struct list_head * head,
				       struct request *req,
				       int max_sectors,
				       int max_segments)
{
	struct list_head * prev;

	prev = req->queue.prev;
	if (head == prev)
		return;
	attempt_merge(q, blkdev_entry_to_request(prev), max_sectors, max_segments);
}

static inline void __make_request(request_queue_t * q, int rw,
			   struct buffer_head * bh)
{
	int major = MAJOR(bh->b_rdev);
	unsigned int sector, count;
	int max_segments = MAX_SEGMENTS;
	struct request * req;
	int rw_ahead, max_req, max_sectors;
	unsigned long flags;

	int orig_latency, latency, starving, sequence;
	struct list_head * entry, * head = &q->queue_head;
	elevator_t * elevator;

	count = bh->b_size >> 9;
	sector = bh->b_rsector;

	if (blk_size[major]) {
		unsigned long maxsector = (blk_size[major][MINOR(bh->b_rdev)] << 1) + 1;

		if (maxsector < count || maxsector - count < sector) {
			bh->b_state &= (1 << BH_Lock) | (1 << BH_Mapped);
			if (!blk_size[major][MINOR(bh->b_rdev)])
				goto end_io;
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
	}

	rw_ahead = 0;	/* normal case; gets changed below for READA */
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
		case WRITERAW:
			rw = WRITE;
			goto do_write;	/* Skip the buffer refile */
		case WRITE:
			if (!test_and_clear_bit(BH_Dirty, &bh->b_state))
				goto end_io;	/* Hmmph! Nothing to write */
			refile_buffer(bh);
		do_write:
			/*
			 * We don't allow the write-requests to fill up the
			 * queue completely:  we want some room for reads,
			 * as they take precedence. The last third of the
			 * requests are only for reads.
			 */
			kstat.pgpgout++;
			max_req = (NR_REQUEST * 2) / 3;
			break;
		default:
			BUG();
			goto end_io;
	}

	/* We'd better have a real physical mapping!
	   Check this bit only if the buffer was dirty and just locked
	   down by us so at this point flushpage will block and
	   won't clear the mapped bit under us. */
	if (!buffer_mapped(bh))
		BUG();

	/*
	 * Temporary solution - in 2.5 this will be done by the lowlevel
	 * driver. Create a bounce buffer if the buffer data points into
	 * high memory - keep the original buffer otherwise.
	 */
#if CONFIG_HIGHMEM
	bh = create_bounce(rw, bh);
#endif

/* look for a free request. */
	/*
	 * Loop uses two requests, 1 for loop and 1 for the real device.
	 * Cut max_req in half to avoid running out and deadlocking.
	 */
	 if ((major == LOOP_MAJOR) || (major == NBD_MAJOR))
		max_req >>= 1;

	/*
	 * Try to coalesce the new request with old requests
	 */
	max_sectors = get_max_sectors(bh->b_rdev);

	elevator = &q->elevator;
	orig_latency = elevator_request_latency(elevator, rw);

	/*
	 * Now we acquire the request spinlock, we have to be mega careful
	 * not to schedule or do something nonatomic
	 */
	spin_lock_irqsave(&io_request_lock,flags);
	elevator_debug(q, bh->b_rdev);

	if (list_empty(head)) {
		q->plug_device_fn(q, bh->b_rdev); /* is atomic */
		goto get_rq;
	}

	/* avoid write-bombs to not hurt iteractiveness of reads */
	if (rw != READ && elevator->read_pendings)
		max_segments = elevator->max_bomb_segments;

	sequence = elevator->sequence;
	latency = orig_latency - elevator->nr_segments;
	starving = 0;
	entry = head;

	/*
	 * The scsi disk and cdrom drivers completely remove the request
	 * from the queue when they start processing an entry.  For this
	 * reason it is safe to continue to add links to the top entry
	 * for those devices.
	 *
	 * All other drivers need to jump over the first entry, as that
	 * entry may be busy being processed and we thus can't change
	 * it.
	 */
	if (q->head_active && !q->plugged)
		head = head->next;

	while ((entry = entry->prev) != head && !starving) {
		req = blkdev_entry_to_request(entry);
		if (!req->q)
			break;
		latency += req->nr_segments;
		if (elevator_sequence_before(req->elevator_sequence, sequence))
			starving = 1;
		if (latency < 0)
			continue;

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
			if (latency - req->nr_segments < 0)
				break;
			/*
			 * The merge_fn is a more advanced way
			 * of accomplishing the same task.  Instead
			 * of applying a fixed limit of some sort
			 * we instead define a function which can
			 * determine whether or not it is safe to
			 * merge the request or not.
			 *
			 * See if this queue has rules that
			 * may suggest that we shouldn't merge
			 * this 
			 */
			if(!(q->back_merge_fn)(q, req, bh, max_segments))
				break;
			req->bhtail->b_reqnext = bh;
			req->bhtail = bh;
		    	req->nr_sectors = req->hard_nr_sectors += count;
			drive_stat_acct(req->rq_dev, req->cmd, count, 0);

			elevator_merge_after(elevator, req, latency);

			/* Can we now merge this req with the next? */
			attempt_back_merge(q, req, max_sectors, max_segments);
		/* or to the beginning? */
		} else if (req->sector - count == sector) {
			if (starving)
				break;
			/*
			 * The merge_fn is a more advanced way
			 * of accomplishing the same task.  Instead
			 * of applying a fixed limit of some sort
			 * we instead define a function which can
			 * determine whether or not it is safe to
			 * merge the request or not.
			 *
			 * See if this queue has rules that
			 * may suggest that we shouldn't merge
			 * this 
			 */
			if(!(q->front_merge_fn)(q, req, bh, max_segments))
				break;
		    	bh->b_reqnext = req->bh;
		    	req->bh = bh;
		    	req->buffer = bh->b_data;
		    	req->current_nr_sectors = count;
		    	req->sector = req->hard_sector = sector;
		    	req->nr_sectors = req->hard_nr_sectors += count;
			drive_stat_acct(req->rq_dev, req->cmd, count, 0);

			elevator_merge_before(elevator, req, latency);

			attempt_front_merge(q, head, req, max_sectors, max_segments);
		} else
			continue;

		q->elevator.sequence++;
		spin_unlock_irqrestore(&io_request_lock,flags);
	    	return;

	}

/* find an unused request. */
get_rq:
	req = get_request(max_req, bh->b_rdev);

	/*
	 * if no request available: if rw_ahead, forget it,
	 * otherwise try again blocking..
	 */
	if (!req) {
		spin_unlock_irqrestore(&io_request_lock,flags);
		if (rw_ahead)
			goto end_io;
		req = __get_request_wait(max_req, bh->b_rdev);
		spin_lock_irqsave(&io_request_lock,flags);

		/* revalidate elevator */
		head = &q->queue_head;
		if (q->head_active && !q->plugged)
			head = head->next;
	}

/* fill up the request-info, and add it to the queue */
	req->cmd = rw;
	req->errors = 0;
	req->hard_sector = req->sector = sector;
	req->hard_nr_sectors = req->nr_sectors = count;
	req->current_nr_sectors = count;
	req->nr_segments = 1; /* Always 1 for a new request. */
	req->nr_hw_segments = 1; /* Always 1 for a new request. */
	req->buffer = bh->b_data;
	req->sem = NULL;
	req->bh = bh;
	req->bhtail = bh;
	req->q = q;
	add_request(q, req, head, orig_latency);

	spin_unlock_irqrestore(&io_request_lock, flags);
	return;

end_io:
	bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
}

int generic_make_request (request_queue_t *q, int rw, struct buffer_head * bh)
{
	unsigned long flags;
	int ret;

	/*
	 * Resolve the mapping until finished. (drivers are
	 * still free to implement/resolve their own stacking
	 * by explicitly returning 0)
	 */

	while (q->make_request_fn) {
		ret = q->make_request_fn(q, rw, bh);
		if (ret > 0) {
			q = blk_get_queue(bh->b_rdev);
			continue;
		}
		return ret;
	}
	/*
	 * Does the block device want us to queue
	 * the IO request? (normal case)
	 */
	__make_request(q, rw, bh);
	spin_lock_irqsave(&io_request_lock,flags);
	if (q && !q->plugged)
		(q->request_fn)(q);
	spin_unlock_irqrestore(&io_request_lock,flags);

	return 0;
}

/* This function can be used to request a number of buffers from a block
   device. Currently the only restriction is that all buffers must belong to
   the same device */

static void __ll_rw_block(int rw, int nr, struct buffer_head * bhs[],
								int haslock)
{
	struct buffer_head *bh;
	request_queue_t *q;
	unsigned int major;
	int correct_size;
	int i;

	major = MAJOR(bhs[0]->b_dev);
	q = blk_get_queue(bhs[0]->b_dev);
	if (!q) {
		printk(KERN_ERR
	"ll_rw_block: Trying to read nonexistent block-device %s (%ld)\n",
		kdevname(bhs[0]->b_dev), bhs[0]->b_blocknr);
		goto sorry;
	}

	/* Determine correct block size for this device. */
	correct_size = BLOCK_SIZE;
	if (blksize_size[major]) {
		i = blksize_size[major][MINOR(bhs[0]->b_dev)];
		if (i)
			correct_size = i;
	}

	/* Verify requested block sizes. */
	for (i = 0; i < nr; i++) {
		bh = bhs[i];
		if (bh->b_size != correct_size) {
			printk(KERN_NOTICE "ll_rw_block: device %s: "
			       "only %d-char blocks implemented (%u)\n",
			       kdevname(bhs[0]->b_dev),
			       correct_size, bh->b_size);
			goto sorry;
		}
	}

	if ((rw & WRITE) && is_read_only(bhs[0]->b_dev)) {
		printk(KERN_NOTICE "Can't write to read-only device %s\n",
		       kdevname(bhs[0]->b_dev));
		goto sorry;
	}

	for (i = 0; i < nr; i++) {
		bh = bhs[i];

		/* Only one thread can actually submit the I/O. */
		if (haslock) {
			if (!buffer_locked(bh))
				BUG();
		} else {
			if (test_and_set_bit(BH_Lock, &bh->b_state))
				continue;
		}
		set_bit(BH_Req, &bh->b_state);

		/*
		 * First step, 'identity mapping' - RAID or LVM might
		 * further remap this.
		 */
		bh->b_rdev = bh->b_dev;
		bh->b_rsector = bh->b_blocknr * (bh->b_size>>9);

		generic_make_request(q, rw, bh);
	}
	return;

sorry:
	for (i = 0; i < nr; i++)
		buffer_IO_error(bhs[i]);
	return;
}

void ll_rw_block(int rw, int nr, struct buffer_head * bh[])
{
	__ll_rw_block(rw, nr, bh, 0);
}

void ll_rw_block_locked(int rw, int nr, struct buffer_head * bh[])
{
	__ll_rw_block(rw, nr, bh, 1);
}

#ifdef CONFIG_STRAM_SWAP
extern int stram_device_init (void);
#endif

/*
 * First step of what used to be end_request
 *
 * 0 means continue with end_that_request_last,
 * 1 means we are done
 */

int end_that_request_first (struct request *req, int uptodate, char *name)
{
	struct buffer_head * bh;
	int nsect;

	req->errors = 0;
	if (!uptodate)
		printk("end_request: I/O error, dev %s (%s), sector %lu\n",
			kdevname(req->rq_dev), name, req->sector);

	if ((bh = req->bh) != NULL) {
		nsect = bh->b_size >> 9;
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_end_io(bh, uptodate);
		if ((bh = req->bh) != NULL) {
			req->hard_sector += nsect;
			req->hard_nr_sectors -= nsect;
			req->sector = req->hard_sector;
			req->nr_sectors = req->hard_nr_sectors;

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

void end_that_request_last(struct request *req)
{
	if (req->q)
		BUG();
	if (req->sem != NULL)
		up(req->sem);
	req->rq_status = RQ_INACTIVE;
	wake_up(&wait_for_request);
}

int __init blk_dev_init(void)
{
	struct request * req;
	struct blk_dev_struct *dev;

	for (dev = blk_dev + MAX_BLKDEV; dev-- != blk_dev;) {
		dev->queue = NULL;
		blk_init_queue(&dev->request_queue, NULL);
	}

	req = all_requests + NR_REQUEST;
	while (--req >= all_requests) {
		req->rq_status = RQ_INACTIVE;
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
#if defined(CONFIG_IDE) && defined(CONFIG_BLK_DEV_IDE)
	ide_init();		/* this MUST precede hd_init */
#endif
#if defined(CONFIG_IDE) && defined(CONFIG_BLK_DEV_HD)
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
#ifdef CONFIG_BLK_DEV_SWIM_IOP
	swimiop_init();
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
#if defined(__i386__)	/* Do we even need this? */
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
#ifdef CONFIG_MDISK
	mdisk_init();
#endif
#ifdef CONFIG_DASD
	dasd_init();
#endif
#ifdef CONFIG_SUN_JSFLASH
	jsfd_init();
#endif
#ifdef CONFIG_BLK_DEV_LVM
	lvm_init();
#endif 
	return 0;
};

EXPORT_SYMBOL(io_request_lock);
EXPORT_SYMBOL(end_that_request_first);
EXPORT_SYMBOL(end_that_request_last);
EXPORT_SYMBOL(blk_init_queue);
EXPORT_SYMBOL(blk_get_queue);
EXPORT_SYMBOL(blk_cleanup_queue);
EXPORT_SYMBOL(blk_queue_headactive);
EXPORT_SYMBOL(blk_queue_pluggable);
EXPORT_SYMBOL(blk_queue_make_request);
EXPORT_SYMBOL(generic_make_request);
