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

#define DEBUG_ELEVATOR

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

static inline int get_request_latency(elevator_t * elevator, int rw)
{
	int latency;

	if (rw != READ)
		latency = elevator->write_latency;
	else
		latency = elevator->read_latency;

	return latency;
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
		q->nr_segments++;
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

	if (req->bhtail->b_data + req->bhtail->b_size == next->bh->b_data) {
		total_segments--;
		q->nr_segments--;
	}
    
	if (total_segments > max_segments)
		return 0;

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
	q->elevator = ELEVATOR_DEFAULTS;
	q->request_fn     	= rfn;
	q->back_merges_fn       	= ll_back_merge_fn;
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

static inline void drive_stat_acct(struct request *req,
				unsigned long nr_sectors, int new_io)
{
	int major = MAJOR(req->rq_dev);
	int minor = MINOR(req->rq_dev);
	unsigned int disk_index;

	switch (major) {
		case DAC960_MAJOR+0:
			disk_index = (minor & 0x00f8) >> 3;
			break;
		case SCSI_DISK0_MAJOR:
			disk_index = (minor & 0x00f0) >> 4;
			break;
		case IDE0_MAJOR:	/* same as HD_MAJOR */
		case XT_DISK_MAJOR:
			disk_index = (minor & 0x0040) >> 6;
			break;
		case IDE1_MAJOR:
			disk_index = ((minor & 0x0040) >> 6) + 2;
			break;
		default:
			return;
	}
	if (disk_index >= DK_NDRIVE)
		return;

	kstat.dk_drive[disk_index] += new_io;
	if (req->cmd == READ) {
		kstat.dk_drive_rio[disk_index] += new_io;
		kstat.dk_drive_rblk[disk_index] += nr_sectors;
	} else if (req->cmd == WRITE) {
		kstat.dk_drive_wio[disk_index] += new_io;
		kstat.dk_drive_wblk[disk_index] += nr_sectors;
	} else
		printk(KERN_ERR "drive_stat_acct: cmd not R/W?\n");
}

/* elevator */

#define elevator_sequence_after(a,b) ((int)((b)-(a)) < 0)
#define elevator_sequence_before(a,b) elevator_sequence_after(b,a)
#define elevator_sequence_after_eq(a,b) ((int)((b)-(a)) <= 0)
#define elevator_sequence_before_eq(a,b) elevator_sequence_after_eq(b,a)

static inline struct list_head * seek_to_not_starving_chunk(request_queue_t * q,
							    int * lat, int * starving)
{
	int sequence = q->elevator.sequence;
	struct list_head * entry = q->queue_head.prev;
	int pos = 0;

	do {
		struct request * req = blkdev_entry_to_request(entry);
		if (elevator_sequence_before(req->elevator_sequence, sequence)) {
			*lat -= q->nr_segments - pos;
			*starving = 1;
			return entry;
		}
		pos += req->nr_segments;
	} while ((entry = entry->prev) != &q->queue_head);

	*starving = 0;

	return entry->next;
}

static inline void elevator_merge_requests(elevator_t * e, struct request * req, struct request * next)
{
	if (elevator_sequence_before(next->elevator_sequence, req->elevator_sequence))
		req->elevator_sequence = next->elevator_sequence;
	if (req->cmd == READ)
		e->read_pendings--;

}

static inline int elevator_sequence(elevator_t * e, int latency)
{
	return latency + e->sequence;
}

#define elevator_merge_before(q, req, lat)	__elevator_merge((q), (req), (lat), 0)
#define elevator_merge_after(q, req, lat)	__elevator_merge((q), (req), (lat), 1)
static inline void __elevator_merge(request_queue_t * q, struct request * req, int latency, int after)
{
#ifdef DEBUG_ELEVATOR
	int sequence = elevator_sequence(&q->elevator, latency);
	if (after)
		sequence -= req->nr_segments;
	if (elevator_sequence_before(sequence, req->elevator_sequence)) {
		static int warned = 0;
		if (!warned) {
			printk(KERN_WARNING __FUNCTION__
			       ": req latency %d req latency %d\n",
			       req->elevator_sequence - q->elevator.sequence,
			       sequence - q->elevator.sequence);
			warned = 1;
		}
		req->elevator_sequence = sequence;
	}
#endif
}

static inline void elevator_queue(request_queue_t * q,
				  struct request * req,
				  struct list_head * entry,
				  int latency, int starving)
{
	struct request * tmp, * __tmp;
	int __latency = latency;

	__tmp = tmp = blkdev_entry_to_request(entry);

	for (;; tmp = blkdev_next_request(tmp))
	{
		if ((latency -= tmp->nr_segments) <= 0)
		{
			tmp = __tmp;
			latency = __latency;

			if (starving)
				break;

			if (q->head_active && !q->plugged)
			{
				latency -= tmp->nr_segments;
				break;
			}

			list_add(&req->queue, &q->queue_head);
			goto after_link;
		}

		if (tmp->queue.next == &q->queue_head)
			break;

		{
			const int after_current = IN_ORDER(tmp,req);
			const int before_next = IN_ORDER(req,blkdev_next_request(tmp));

			if (!IN_ORDER(tmp,blkdev_next_request(tmp))) {
				if (after_current || before_next)
					break;
			} else {
				if (after_current && before_next)
					break;
			}
		}
	}

	list_add(&req->queue, &tmp->queue);

 after_link:
	req->elevator_sequence = elevator_sequence(&q->elevator, latency);
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

static inline void __add_request(request_queue_t * q, struct request * req,
				 int empty, struct list_head * entry,
				 int latency, int starving)
{
	int major;

	drive_stat_acct(req, req->nr_sectors, 1);

	if (empty) {
		req->elevator_sequence = elevator_sequence(&q->elevator, latency);
		list_add(&req->queue, &q->queue_head);
		return;
	}
	elevator_queue(q, req, entry, latency, starving);

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
static inline void attempt_merge (request_queue_t * q,
				  struct request *req,
				  int max_sectors,
				  int max_segments)
{
	struct request *next;
  
	if (req->queue.next == &q->queue_head)
		return;
	next = blkdev_next_request(req);
	if (req->sector + req->nr_sectors != next->sector)
		return;
	if (next->sem || req->cmd != next->cmd || req->rq_dev != next->rq_dev || req->nr_sectors + next->nr_sectors > max_sectors)
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
	req->nr_sectors += next->nr_sectors;
	next->rq_status = RQ_INACTIVE;
	list_del(&next->queue);
	wake_up (&wait_for_request);
}

static inline void elevator_debug(request_queue_t * q, kdev_t dev)
{
#ifdef DEBUG_ELEVATOR
	int read_pendings = 0, nr_segments = 0;
	elevator_t * elevator = &q->elevator;
	struct list_head * entry = &q->queue_head;
	static int counter;

	if (counter++ % 100)
		return;

	while ((entry = entry->next) != &q->queue_head)
	{
		struct request * req;

		req = blkdev_entry_to_request(entry);
		if (!req->q)
			continue;
		if (req->cmd == READ)
			read_pendings++;
		nr_segments += req->nr_segments;
	}

	if (read_pendings != elevator->read_pendings)
	{
		printk(KERN_WARNING
		       "%s: elevator read_pendings %d should be %d\n",
		       kdevname(dev), elevator->read_pendings,
		       read_pendings);
		elevator->read_pendings = read_pendings;
	}
	if (nr_segments != q->nr_segments)
	{
		printk(KERN_WARNING
		       "%s: elevator nr_segments %d should be %d\n",
		       kdevname(dev), q->nr_segments,
		       nr_segments);
		q->nr_segments = nr_segments;
	}
#endif
}

static inline void elevator_account_request(request_queue_t * q, struct request * req)
{
	q->elevator.sequence++;
	if (req->cmd == READ)
		q->elevator.read_pendings++;
	q->nr_segments++;
}

static inline void __make_request(request_queue_t * q, int rw,
			   struct buffer_head * bh)
{
	int major = MAJOR(bh->b_rdev);
	unsigned int sector, count;
	int max_segments = MAX_SEGMENTS;
	struct request * req, * prev;
	int rw_ahead, max_req, max_sectors;
	unsigned long flags;
	int orig_latency, latency, __latency, starving, __starving, empty;
	struct list_head * entry, * __entry;

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

	__latency = orig_latency = get_request_latency(&q->elevator, rw);

	/*
	 * Now we acquire the request spinlock, we have to be mega careful
	 * not to schedule or do something nonatomic
	 */
	spin_lock_irqsave(&io_request_lock,flags);
	elevator_debug(q, bh->b_rdev);

	empty = 0;
	if (list_empty(&q->queue_head)) {
		empty = 1;
		q->plug_device_fn(q, bh->b_rdev); /* is atomic */
		goto get_rq;
	}

	/* avoid write-bombs to not hurt iteractiveness of reads */
	if (rw != READ && q->elevator.read_pendings)
		max_segments = q->elevator.max_bomb_segments;

	entry = seek_to_not_starving_chunk(q, &__latency, &starving);

	__entry = entry;
	__starving = starving;

	latency = __latency;

	if (q->head_active && !q->plugged) {
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
		if (entry == q->queue_head.next) {
			latency -= blkdev_entry_to_request(entry)->nr_segments;
			if ((entry = entry->next) == &q->queue_head)
				goto get_rq;
			starving = 0;
		}
	}

	prev = NULL;
	do {
		req = blkdev_entry_to_request(entry);

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
				continue;
			req->bhtail->b_reqnext = bh;
			req->bhtail = bh;
		    	req->nr_sectors += count;
			drive_stat_acct(req, count, 0);

			elevator_merge_after(q, req, latency);

			/* Can we now merge this req with the next? */
			attempt_merge(q, req, max_sectors, max_segments);
		/* or to the beginning? */
		} else if (req->sector - count == sector) {
			if (!prev && starving)
				continue;
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
				continue;
		    	bh->b_reqnext = req->bh;
		    	req->bh = bh;
		    	req->buffer = bh->b_data;
		    	req->current_nr_sectors = count;
		    	req->sector = sector;
		    	req->nr_sectors += count;
			drive_stat_acct(req, count, 0);

			elevator_merge_before(q, req, latency);

			if (prev)
				attempt_merge(q, prev, max_sectors, max_segments);
		} else
			continue;

		q->elevator.sequence++;
		spin_unlock_irqrestore(&io_request_lock,flags);
	    	return;

	} while (prev = req,
		 (latency -= req->nr_segments) >= 0 &&
		 (entry = entry->next) != &q->queue_head);

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

		/* lock got dropped so revalidate elevator */
		empty = 1;
		if (!list_empty(&q->queue_head)) {
			empty = 0;
			__latency = orig_latency;
			__entry = seek_to_not_starving_chunk(q, &__latency, &__starving);
		}
	}
	/*
	 * Dont start the IO if the buffer has been
	 * invalidated meanwhile. (we have to do this
	 * within the io request lock and atomically
	 * before adding the request, see buffer.c's
	 * insert_into_queues_exclusive() function.
	 */
	if (!test_bit(BH_Req, &bh->b_state)) {
		req->rq_status = RQ_INACTIVE;
		spin_unlock_irqrestore(&io_request_lock,flags);
		/*
		 * A fake 'everything went ok' completion event.
		 * The bh doesnt matter anymore, but we should not
		 * signal errors to RAID levels.
	 	 */
		bh->b_end_io(bh, 1);
		return;
	}

/* fill up the request-info, and add it to the queue */
	req->cmd = rw;
	req->errors = 0;
	req->sector = sector;
	req->nr_sectors = count;
	req->current_nr_sectors = count;
	req->nr_segments = 1; /* Always 1 for a new request. */
	req->nr_hw_segments = 1; /* Always 1 for a new request. */
	req->buffer = bh->b_data;
	req->sem = NULL;
	req->bh = bh;
	req->bhtail = bh;
	req->q = q;
	__add_request(q, req, empty, __entry, __latency, __starving);
	elevator_account_request(q, req);

	spin_unlock_irqrestore(&io_request_lock, flags);
	return;

end_io:
	bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
}

void generic_make_request(int rw, struct buffer_head * bh)
{
	request_queue_t * q;
	unsigned long flags;

	q = blk_get_queue(bh->b_rdev);

	__make_request(q, rw, bh);

	spin_lock_irqsave(&io_request_lock,flags);
	if (q && !q->plugged)
		(q->request_fn)(q);
	spin_unlock_irqrestore(&io_request_lock,flags);
}


/* This function can be used to request a number of buffers from a block
   device. Currently the only restriction is that all buffers must belong to
   the same device */

static void __ll_rw_block(int rw, int nr, struct buffer_head * bh[],int haslock)
{
	unsigned int major;
	int correct_size;
	request_queue_t *q;
	int i;

	major = MAJOR(bh[0]->b_dev);
	q = blk_get_queue(bh[0]->b_dev);
	if (!q) {
		printk(KERN_ERR
	"ll_rw_block: Trying to read nonexistent block-device %s (%ld)\n",
		kdevname(bh[0]->b_dev), bh[0]->b_blocknr);
		goto sorry;
	}

	/* Determine correct block size for this device. */
	correct_size = BLOCK_SIZE;
	if (blksize_size[major]) {
		i = blksize_size[major][MINOR(bh[0]->b_dev)];
		if (i)
			correct_size = i;
	}

	/* Verify requested block sizes. */
	for (i = 0; i < nr; i++) {
		if (bh[i]->b_size != correct_size) {
			printk(KERN_NOTICE "ll_rw_block: device %s: "
			       "only %d-char blocks implemented (%u)\n",
			       kdevname(bh[0]->b_dev),
			       correct_size, bh[i]->b_size);
			goto sorry;
		}
	}

	if ((rw & WRITE) && is_read_only(bh[0]->b_dev)) {
		printk(KERN_NOTICE "Can't write to read-only device %s\n",
		       kdevname(bh[0]->b_dev));
		goto sorry;
	}

	for (i = 0; i < nr; i++) {
		/* Only one thread can actually submit the I/O. */
		if (haslock) {
			if (!buffer_locked(bh[i]))
				BUG();
		} else {
			if (test_and_set_bit(BH_Lock, &bh[i]->b_state))
				continue;
		}
		set_bit(BH_Req, &bh[i]->b_state);

		if (q->make_request_fn)
			q->make_request_fn(rw, bh[i]);
		else {
			bh[i]->b_rdev = bh[i]->b_dev;
			bh[i]->b_rsector = bh[i]->b_blocknr*(bh[i]->b_size>>9);

			generic_make_request(rw, bh[i]);
		}
	}

	return;

sorry:
	for (i = 0; i < nr; i++) {
		mark_buffer_clean(bh[i]); /* remeber to refile it */
		clear_bit(BH_Uptodate, &bh[i]->b_state);
		bh[i]->b_end_io(bh[i], 0);
	}
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
#if !defined (__mc68000__) && !defined(CONFIG_PPC) && !defined(__sparc__)\
    && !defined(CONFIG_APUS) && !defined(__sh__) && !defined(__ia64__)
	outb_p(0xc, 0x3f2);	/* XXX do something with the floppy controller?? */
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
EXPORT_SYMBOL(blk_init_queue);
EXPORT_SYMBOL(blk_cleanup_queue);
EXPORT_SYMBOL(blk_queue_headactive);
EXPORT_SYMBOL(blk_queue_pluggable);
EXPORT_SYMBOL(generic_make_request);
