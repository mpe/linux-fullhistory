/*
 *  linux/drivers/block/ll_rw_blk.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 * Elevator latency, (C) 2000  Andrea Arcangeli <andrea@suse.de> SuSE
 * Queue request tables / lock, selectable elevator, Jens Axboe <axboe@suse.de>
 * kernel-doc documentation started by NeilBrown <neilb@cse.unsw.edu.au> -  July2000
 * bio rewrite, highmem i/o, etc, Jens Axboe <axboe@suse.de> - may 2001
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/blk.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>	/* for max_pfn/max_low_pfn */
#include <linux/completion.h>
#include <linux/slab.h>

/*
 * Disk stats 
 */
struct disk_stat dkstat;

/*
 * For the allocated request tables
 */
static kmem_cache_t *request_cachep;

/*
 * plug management
 */
static LIST_HEAD(blk_plug_list);
static spinlock_t blk_plug_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;

/*
 * Number of requests per queue.  This many for reads and for writes (twice
 * this number, total).
 */
static int queue_nr_requests;

/*
 * How many free requests must be available before we wake a process which
 * is waiting for a request?
 */
static int batch_requests;

unsigned long blk_max_low_pfn, blk_max_pfn;
atomic_t nr_iowait_tasks = ATOMIC_INIT(0);
int blk_nohighio = 0;

static struct congestion_state {
	wait_queue_head_t wqh;
	atomic_t nr_congested_queues;
} congestion_states[2];

/*
 * Return the threshold (number of free requests) at which the queue is
 * considered to be congested.  It include a little hysteresis to keep the
 * context switch rate down.
 */
static inline int queue_congestion_on_threshold(void)
{
	int ret;

	ret = queue_nr_requests / 4 - 1;
	if (ret < 0)
		ret = 1;
	return ret;
}

/*
 * The threshold at which a queue is considered to be uncongested
 */
static inline int queue_congestion_off_threshold(void)
{
	int ret;

	ret = queue_nr_requests / 4 + 1;
	if (ret > queue_nr_requests)
		ret = queue_nr_requests;
	return ret;
}

static void clear_queue_congested(request_queue_t *q, int rw)
{
	enum bdi_state bit;
	struct congestion_state *cs = &congestion_states[rw];

	bit = (rw == WRITE) ? BDI_write_congested : BDI_read_congested;

	if (test_and_clear_bit(bit, &q->backing_dev_info.state))
		atomic_dec(&cs->nr_congested_queues);
	if (waitqueue_active(&cs->wqh))
		wake_up(&cs->wqh);
}

static void set_queue_congested(request_queue_t *q, int rw)
{
	enum bdi_state bit;

	bit = (rw == WRITE) ? BDI_write_congested : BDI_read_congested;

	if (!test_and_set_bit(bit, &q->backing_dev_info.state))
		atomic_inc(&congestion_states[rw].nr_congested_queues);
}

/*
 * This task is about to go to sleep on IO.  Increment nr_iowait_tasks so
 * that process accounting knows that this is a task in IO wait state.
 *
 * But don't do that if it is a deliberate, throttling IO wait (this task
 * has set its backing_dev_info: the queue against which it should throttle)
 */
void io_schedule(void)
{
	atomic_inc(&nr_iowait_tasks);
	schedule();
	atomic_dec(&nr_iowait_tasks);
}

void io_schedule_timeout(long timeout)
{
	atomic_inc(&nr_iowait_tasks);
	schedule_timeout(timeout);
	atomic_dec(&nr_iowait_tasks);
}

/**
 * blk_get_backing_dev_info - get the address of a queue's backing_dev_info
 * @dev:	device
 *
 * Locates the passed device's request queue and returns the address of its
 * backing_dev_info
 *
 * Will return NULL if the request queue cannot be located.
 */
struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev)
{
	struct backing_dev_info *ret = NULL;
	request_queue_t *q = bdev_get_queue(bdev);

	if (q)
		ret = &q->backing_dev_info;
	return ret;
}

/**
 * blk_queue_prep_rq - set a prepare_request function for queue
 * @q:		queue
 * @pfn:	prepare_request function
 *
 * It's possible for a queue to register a prepare_request callback which
 * is invoked before the request is handed to the request_fn. The goal of
 * the function is to prepare a request for I/O, it can be used to build a
 * cdb from the request data for instance.
 *
 */
void blk_queue_prep_rq(request_queue_t *q, prep_rq_fn *pfn)
{
	q->prep_rq_fn = pfn;
}

/**
 * blk_queue_merge_bvec - set a merge_bvec function for queue
 * @q:		queue
 * @mbfn:	merge_bvec_fn
 *
 * Usually queues have static limitations on the max sectors or segments that
 * we can put in a request. Stacking drivers may have some settings that
 * are dynamic, and thus we have to query the queue whether it is ok to
 * add a new bio_vec to a bio at a given offset or not. If the block device
 * has such limitations, it needs to register a merge_bvec_fn to control
 * the size of bio's sent to it. Per default now merge_bvec_fn is defined for
 * a queue, and only the fixed limits are honored.
 *
 */
void blk_queue_merge_bvec(request_queue_t *q, merge_bvec_fn *mbfn)
{
	q->merge_bvec_fn = mbfn;
}

/**
 * blk_queue_make_request - define an alternate make_request function for a device
 * @q:  the request queue for the device to be affected
 * @mfn: the alternate make_request function
 *
 * Description:
 *    The normal way for &struct bios to be passed to a device
 *    driver is for them to be collected into requests on a request
 *    queue, and then to allow the device driver to select requests
 *    off that queue when it is ready.  This works well for many block
 *    devices. However some block devices (typically virtual devices
 *    such as md or lvm) do not benefit from the processing on the
 *    request queue, and are served best by having the requests passed
 *    directly to them.  This can be achieved by providing a function
 *    to blk_queue_make_request().
 *
 * Caveat:
 *    The driver that does this *must* be able to deal appropriately
 *    with buffers in "highmemory". This can be accomplished by either calling
 *    bio_kmap() to get a temporary kernel mapping, or by calling
 *    blk_queue_bounce() to create a buffer in normal memory.
 **/
void blk_queue_make_request(request_queue_t * q, make_request_fn * mfn)
{
	/*
	 * set defaults
	 */
	q->max_phys_segments = MAX_PHYS_SEGMENTS;
	q->max_hw_segments = MAX_HW_SEGMENTS;
	q->make_request_fn = mfn;
	q->backing_dev_info.ra_pages = (VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE;
	q->backing_dev_info.state = 0;
	blk_queue_max_sectors(q, MAX_SECTORS);
	blk_queue_hardsect_size(q, 512);
	blk_queue_dma_alignment(q, 511);

	/*
	 * by default assume old behaviour and bounce for any highmem page
	 */
	blk_queue_bounce_limit(q, BLK_BOUNCE_HIGH);

	init_waitqueue_head(&q->queue_wait);
	INIT_LIST_HEAD(&q->plug_list);
}

/**
 * blk_queue_bounce_limit - set bounce buffer limit for queue
 * @q:  the request queue for the device
 * @dma_addr:   bus address limit
 *
 * Description:
 *    Different hardware can have different requirements as to what pages
 *    it can do I/O directly to. A low level driver can call
 *    blk_queue_bounce_limit to have lower memory pages allocated as bounce
 *    buffers for doing I/O to pages residing above @page. By default
 *    the block layer sets this to the highest numbered "low" memory page.
 **/
void blk_queue_bounce_limit(request_queue_t *q, u64 dma_addr)
{
	unsigned long bounce_pfn = dma_addr >> PAGE_SHIFT;
	unsigned long mb = dma_addr >> 20;
	static request_queue_t *last_q;

	/*
	 * set appropriate bounce gfp mask -- unfortunately we don't have a
	 * full 4GB zone, so we have to resort to low memory for any bounces.
	 * ISA has its own < 16MB zone.
	 */
	if (bounce_pfn < blk_max_low_pfn) {
		BUG_ON(dma_addr < BLK_BOUNCE_ISA);
		init_emergency_isa_pool();
		q->bounce_gfp = GFP_NOIO | GFP_DMA;
	} else
		q->bounce_gfp = GFP_NOHIGHIO;

	/*
	 * keep this for debugging for now...
	 */
	if (dma_addr != BLK_BOUNCE_HIGH && q != last_q) {
		printk("blk: queue %p, ", q);
		if (dma_addr == BLK_BOUNCE_ANY)
			printk("no I/O memory limit\n");
		else
			printk("I/O limit %luMb (mask 0x%Lx)\n", mb, (long long) dma_addr);
	}

	q->bounce_pfn = bounce_pfn;
	last_q = q;
}


/**
 * blk_queue_max_sectors - set max sectors for a request for this queue
 * @q:  the request queue for the device
 * @max_sectors:  max sectors in the usual 512b unit
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the size of
 *    received requests.
 **/
void blk_queue_max_sectors(request_queue_t *q, unsigned short max_sectors)
{
	if ((max_sectors << 9) < PAGE_CACHE_SIZE) {
		max_sectors = 1 << (PAGE_CACHE_SHIFT - 9);
		printk("%s: set to minimum %d\n", __FUNCTION__, max_sectors);
	}

	q->max_sectors = max_sectors;
}

/**
 * blk_queue_max_phys_segments - set max phys segments for a request for this queue
 * @q:  the request queue for the device
 * @max_segments:  max number of segments
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the number of
 *    physical data segments in a request.  This would be the largest sized
 *    scatter list the driver could handle.
 **/
void blk_queue_max_phys_segments(request_queue_t *q, unsigned short max_segments)
{
	if (!max_segments) {
		max_segments = 1;
		printk("%s: set to minimum %d\n", __FUNCTION__, max_segments);
	}

	q->max_phys_segments = max_segments;
}

/**
 * blk_queue_max_hw_segments - set max hw segments for a request for this queue
 * @q:  the request queue for the device
 * @max_segments:  max number of segments
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the number of
 *    hw data segments in a request.  This would be the largest number of
 *    address/length pairs the host adapter can actually give as once
 *    to the device.
 **/
void blk_queue_max_hw_segments(request_queue_t *q, unsigned short max_segments)
{
	if (!max_segments) {
		max_segments = 1;
		printk("%s: set to minimum %d\n", __FUNCTION__, max_segments);
	}

	q->max_hw_segments = max_segments;
}

/**
 * blk_queue_max_segment_size - set max segment size for blk_rq_map_sg
 * @q:  the request queue for the device
 * @max_size:  max size of segment in bytes
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the size of a
 *    coalesced segment
 **/
void blk_queue_max_segment_size(request_queue_t *q, unsigned int max_size)
{
	if (max_size < PAGE_CACHE_SIZE) {
		max_size = PAGE_CACHE_SIZE;
		printk("%s: set to minimum %d\n", __FUNCTION__, max_size);
	}

	q->max_segment_size = max_size;
}

/**
 * blk_queue_hardsect_size - set hardware sector size for the queue
 * @q:  the request queue for the device
 * @size:  the hardware sector size, in bytes
 *
 * Description:
 *   This should typically be set to the lowest possible sector size
 *   that the hardware can operate on (possible without reverting to
 *   even internal read-modify-write operations). Usually the default
 *   of 512 covers most hardware.
 **/
void blk_queue_hardsect_size(request_queue_t *q, unsigned short size)
{
	q->hardsect_size = size;
}

/**
 * blk_queue_segment_boundary - set boundary rules for segment merging
 * @q:  the request queue for the device
 * @mask:  the memory boundary mask
 **/
void blk_queue_segment_boundary(request_queue_t *q, unsigned long mask)
{
	if (mask < PAGE_CACHE_SIZE - 1) {
		mask = PAGE_CACHE_SIZE - 1;
		printk("%s: set to minimum %lx\n", __FUNCTION__, mask);
	}

	q->seg_boundary_mask = mask;
}

/**
 * blk_queue_dma_alignment - set dma length and memory alignment
 * @q:  the request queue for the device
 * @dma_mask:  alignment mask
 *
 * description:
 *    set required memory and length aligment for direct dma transactions.
 *    this is used when buiding direct io requests for the queue.
 *
 **/
void blk_queue_dma_alignment(request_queue_t *q, int mask)
{
	q->dma_alignment = mask;
}

void blk_queue_assign_lock(request_queue_t *q, spinlock_t *lock)
{
	spin_lock_init(lock);
	q->queue_lock = lock;
}

/**
 * blk_queue_find_tag - find a request by its tag and queue
 *
 * @q:	 The request queue for the device
 * @tag: The tag of the request
 *
 * Notes:
 *    Should be used when a device returns a tag and you want to match
 *    it with a request.
 *
 *    no locks need be held.
 **/
struct request *blk_queue_find_tag(request_queue_t *q, int tag)
{
	struct blk_queue_tag *bqt = q->queue_tags;

	if(unlikely(bqt == NULL || bqt->max_depth < tag))
		return NULL;

	return bqt->tag_index[tag];
}
/**
 * blk_queue_free_tags - release tag maintenance info
 * @q:  the request queue for the device
 *
 *  Notes:
 *    blk_cleanup_queue() will take care of calling this function, if tagging
 *    has been used. So there's usually no need to call this directly, unless
 *    tagging is just being disabled but the queue remains in function.
 **/
void blk_queue_free_tags(request_queue_t *q)
{
	struct blk_queue_tag *bqt = q->queue_tags;

	if (!bqt)
		return;

	BUG_ON(bqt->busy);
	BUG_ON(!list_empty(&bqt->busy_list));

	kfree(bqt->tag_index);
	bqt->tag_index = NULL;

	kfree(bqt->tag_map);
	bqt->tag_map = NULL;

	kfree(bqt);
	q->queue_tags = NULL;
	q->queue_flags &= ~(1 << QUEUE_FLAG_QUEUED);
}

/**
 * blk_queue_init_tags - initialize the queue tag info
 * @q:  the request queue for the device
 * @depth:  the maximum queue depth supported
 **/
int blk_queue_init_tags(request_queue_t *q, int depth)
{
	struct blk_queue_tag *tags;
	int bits, i;

	if (depth > (queue_nr_requests*2)) {
		depth = (queue_nr_requests*2);
		printk("blk_queue_init_tags: adjusted depth to %d\n", depth);
	}

	tags = kmalloc(sizeof(struct blk_queue_tag),GFP_ATOMIC);
	if (!tags)
		goto fail;

	tags->tag_index = kmalloc(depth * sizeof(struct request *), GFP_ATOMIC);
	if (!tags->tag_index)
		goto fail_index;

	bits = (depth / BLK_TAGS_PER_LONG) + 1;
	tags->tag_map = kmalloc(bits * sizeof(unsigned long), GFP_ATOMIC);
	if (!tags->tag_map)
		goto fail_map;

	memset(tags->tag_index, 0, depth * sizeof(struct request *));
	memset(tags->tag_map, 0, bits * sizeof(unsigned long));
	INIT_LIST_HEAD(&tags->busy_list);
	tags->busy = 0;
	tags->max_depth = depth;

	/*
	 * set the upper bits if the depth isn't a multiple of the word size
	 */
	for (i = depth; i < bits * BLK_TAGS_PER_LONG; i++)
		__set_bit(i, tags->tag_map);

	/*
	 * assign it, all done
	 */
	q->queue_tags = tags;
	q->queue_flags |= (1 << QUEUE_FLAG_QUEUED);
	return 0;

fail_map:
	kfree(tags->tag_index);
fail_index:
	kfree(tags);
fail:
	return -ENOMEM;
}

/**
 * blk_queue_end_tag - end tag operations for a request
 * @q:  the request queue for the device
 * @tag:  the tag that has completed
 *
 *  Description:
 *    Typically called when end_that_request_first() returns 0, meaning
 *    all transfers have been done for a request. It's important to call
 *    this function before end_that_request_last(), as that will put the
 *    request back on the free list thus corrupting the internal tag list.
 *
 *  Notes:
 *   queue lock must be held.
 **/
void blk_queue_end_tag(request_queue_t *q, struct request *rq)
{
	struct blk_queue_tag *bqt = q->queue_tags;
	int tag = rq->tag;

	BUG_ON(tag == -1);

	if (unlikely(tag >= bqt->max_depth))
		return;

	if (unlikely(!__test_and_clear_bit(tag, bqt->tag_map))) {
		printk("attempt to clear non-busy tag (%d)\n", tag);
		return;
	}

	list_del_init(&rq->queuelist);
	rq->flags &= ~REQ_QUEUED;
	rq->tag = -1;

	if (unlikely(bqt->tag_index[tag] == NULL))
		printk("tag %d is missing\n", tag);

	bqt->tag_index[tag] = NULL;
	bqt->busy--;
}

/**
 * blk_queue_start_tag - find a free tag and assign it
 * @q:  the request queue for the device
 * @rq:  the block request that needs tagging
 *
 *  Description:
 *    This can either be used as a stand-alone helper, or possibly be
 *    assigned as the queue &prep_rq_fn (in which case &struct request
 *    automagically gets a tag assigned). Note that this function
 *    assumes that any type of request can be queued! if this is not
 *    true for your device, you must check the request type before
 *    calling this function.  The request will also be removed from
 *    the request queue, so it's the drivers responsibility to readd
 *    it if it should need to be restarted for some reason.
 *
 *  Notes:
 *   queue lock must be held.
 **/
int blk_queue_start_tag(request_queue_t *q, struct request *rq)
{
	struct blk_queue_tag *bqt = q->queue_tags;
	unsigned long *map = bqt->tag_map;
	int tag = 0;

	if (unlikely((rq->flags & REQ_QUEUED))) {
		printk(KERN_ERR 
		       "request %p for device [%s] already tagged %d",
		       rq, rq->rq_disk ? rq->rq_disk->disk_name : "?", rq->tag);
		BUG();
	}

	for (map = bqt->tag_map; *map == -1UL; map++) {
		tag += BLK_TAGS_PER_LONG;

		if (tag >= bqt->max_depth)
			return 1;
	}

	tag += ffz(*map);
	__set_bit(tag, bqt->tag_map);

	rq->flags |= REQ_QUEUED;
	rq->tag = tag;
	bqt->tag_index[tag] = rq;
	blkdev_dequeue_request(rq);
	list_add(&rq->queuelist, &bqt->busy_list);
	bqt->busy++;
	return 0;
}

/**
 * blk_queue_invalidate_tags - invalidate all pending tags
 * @q:  the request queue for the device
 *
 *  Description:
 *   Hardware conditions may dictate a need to stop all pending requests.
 *   In this case, we will safely clear the block side of the tag queue and
 *   readd all requests to the request queue in the right order.
 *
 *  Notes:
 *   queue lock must be held.
 **/
void blk_queue_invalidate_tags(request_queue_t *q)
{
	struct blk_queue_tag *bqt = q->queue_tags;
	struct list_head *tmp, *n;
	struct request *rq;

	list_for_each_safe(tmp, n, &bqt->busy_list) {
		rq = list_entry_rq(tmp);

		if (rq->tag == -1) {
			printk("bad tag found on list\n");
			list_del_init(&rq->queuelist);
			rq->flags &= ~REQ_QUEUED;
		} else
			blk_queue_end_tag(q, rq);

		rq->flags &= ~REQ_STARTED;
		__elv_add_request(q, rq, 0, 0);
	}
}

static char *rq_flags[] = {
	"REQ_RW",
	"REQ_RW_AHEAD",
	"REQ_BARRIER",
	"REQ_CMD",
	"REQ_NOMERGE",
	"REQ_STARTED",
	"REQ_DONTPREP",
	"REQ_QUEUED",
	"REQ_PC",
	"REQ_BLOCK_PC",
	"REQ_SENSE",
	"REQ_FAILED",
	"REQ_QUIET",
	"REQ_SPECIAL"
	"REQ_DRIVE_CMD",
	"REQ_DRIVE_TASK",
	"REQ_DRIVE_TASKFILE",
};

void blk_dump_rq_flags(struct request *rq, char *msg)
{
	int bit;

	printk("%s: dev %s: flags = ", msg,
		rq->rq_disk ? rq->rq_disk->disk_name : "?");
	bit = 0;
	do {
		if (rq->flags & (1 << bit))
			printk("%s ", rq_flags[bit]);
		bit++;
	} while (bit < __REQ_NR_BITS);

	printk("\nsector %llu, nr/cnr %lu/%u\n", (unsigned long long)rq->sector,
						       rq->nr_sectors,
						       rq->current_nr_sectors);
	printk("bio %p, biotail %p, buffer %p, data %p, len %u\n", rq->bio, rq->biotail, rq->buffer, rq->data, rq->data_len);

	if (rq->flags & (REQ_BLOCK_PC | REQ_PC)) {
		printk("cdb: ");
		for (bit = 0; bit < sizeof(rq->cmd); bit++)
			printk("%02x ", rq->cmd[bit]);
		printk("\n");
	}
}

void blk_recount_segments(request_queue_t *q, struct bio *bio)
{
	struct bio_vec *bv, *bvprv = NULL;
	int i, nr_phys_segs, nr_hw_segs, seg_size, cluster;

	if (unlikely(!bio->bi_io_vec))
		return;

	cluster = q->queue_flags & (1 << QUEUE_FLAG_CLUSTER);
	seg_size = nr_phys_segs = nr_hw_segs = 0;
	bio_for_each_segment(bv, bio, i) {
		if (bvprv && cluster) {
			int phys, seg;

			if (seg_size + bv->bv_len > q->max_segment_size) {
				nr_phys_segs++;
				goto new_segment;
			}

			phys = BIOVEC_PHYS_MERGEABLE(bvprv, bv);
			seg = BIOVEC_SEG_BOUNDARY(q, bvprv, bv);
			if (!phys || !seg)
				nr_phys_segs++;
			if (!seg)
				goto new_segment;

			if (!BIOVEC_VIRT_MERGEABLE(bvprv, bv))
				goto new_segment;

			seg_size += bv->bv_len;
			bvprv = bv;
			continue;
		} else {
			nr_phys_segs++;
		}
new_segment:
		nr_hw_segs++;
		bvprv = bv;
		seg_size = bv->bv_len;
	}

	bio->bi_phys_segments = nr_phys_segs;
	bio->bi_hw_segments = nr_hw_segs;
	bio->bi_flags |= (1 << BIO_SEG_VALID);
}


inline int blk_phys_contig_segment(request_queue_t *q, struct bio *bio,
				   struct bio *nxt)
{
	if (!(q->queue_flags & (1 << QUEUE_FLAG_CLUSTER)))
		return 0;

	if (!BIOVEC_PHYS_MERGEABLE(__BVEC_END(bio), __BVEC_START(nxt)))
		return 0;
	if (bio->bi_size + nxt->bi_size > q->max_segment_size)
		return 0;

	/*
	 * bio and nxt are contigous in memory, check if the queue allows
	 * these two to be merged into one
	 */
	if (BIO_SEG_BOUNDARY(q, bio, nxt))
		return 1;

	return 0;
}

inline int blk_hw_contig_segment(request_queue_t *q, struct bio *bio,
				 struct bio *nxt)
{
	if (!(q->queue_flags & (1 << QUEUE_FLAG_CLUSTER)))
		return 0;

	if (!BIOVEC_VIRT_MERGEABLE(__BVEC_END(bio), __BVEC_START(nxt)))
		return 0;
	if (bio->bi_size + nxt->bi_size > q->max_segment_size)
		return 0;

	/*
	 * bio and nxt are contigous in memory, check if the queue allows
	 * these two to be merged into one
	 */
	if (BIO_SEG_BOUNDARY(q, bio, nxt))
		return 1;

	return 0;
}

/*
 * map a request to scatterlist, return number of sg entries setup. Caller
 * must make sure sg can hold rq->nr_phys_segments entries
 */
int blk_rq_map_sg(request_queue_t *q, struct request *rq, struct scatterlist *sg)
{
	struct bio_vec *bvec, *bvprv;
	struct bio *bio;
	int nsegs, i, cluster;

	nsegs = 0;
	cluster = q->queue_flags & (1 << QUEUE_FLAG_CLUSTER);

	/*
	 * for each bio in rq
	 */
	bvprv = NULL;
	rq_for_each_bio(bio, rq) {
		/*
		 * for each segment in bio
		 */
		bio_for_each_segment(bvec, bio, i) {
			int nbytes = bvec->bv_len;

			if (bvprv && cluster) {
				if (sg[nsegs - 1].length + nbytes > q->max_segment_size)
					goto new_segment;

				if (!BIOVEC_PHYS_MERGEABLE(bvprv, bvec))
					goto new_segment;
				if (!BIOVEC_SEG_BOUNDARY(q, bvprv, bvec))
					goto new_segment;

				sg[nsegs - 1].length += nbytes;
			} else {
new_segment:
				memset(&sg[nsegs],0,sizeof(struct scatterlist));
				sg[nsegs].page = bvec->bv_page;
				sg[nsegs].length = nbytes;
				sg[nsegs].offset = bvec->bv_offset;

				nsegs++;
			}
			bvprv = bvec;
		} /* segments in bio */
	} /* bios in rq */

	return nsegs;
}

/*
 * the standard queue merge functions, can be overridden with device
 * specific ones if so desired
 */

static inline int ll_new_mergeable(request_queue_t *q,
				   struct request *req,
				   struct bio *bio)
{
	int nr_phys_segs = bio_phys_segments(q, bio);

	if (req->nr_phys_segments + nr_phys_segs > q->max_phys_segments) {
		req->flags |= REQ_NOMERGE;
		q->last_merge = NULL;
		return 0;
	}

	/*
	 * A hw segment is just getting larger, bump just the phys
	 * counter.
	 */
	req->nr_phys_segments += nr_phys_segs;
	return 1;
}

static inline int ll_new_hw_segment(request_queue_t *q,
				    struct request *req,
				    struct bio *bio)
{
	int nr_hw_segs = bio_hw_segments(q, bio);
	int nr_phys_segs = bio_phys_segments(q, bio);

	if (req->nr_hw_segments + nr_hw_segs > q->max_hw_segments
	    || req->nr_phys_segments + nr_phys_segs > q->max_phys_segments) {
		req->flags |= REQ_NOMERGE;
		q->last_merge = NULL;
		return 0;
	}

	/*
	 * This will form the start of a new hw segment.  Bump both
	 * counters.
	 */
	req->nr_hw_segments += nr_hw_segs;
	req->nr_phys_segments += nr_phys_segs;
	return 1;
}

static int ll_back_merge_fn(request_queue_t *q, struct request *req, 
			    struct bio *bio)
{
	if (req->nr_sectors + bio_sectors(bio) > q->max_sectors) {
		req->flags |= REQ_NOMERGE;
		q->last_merge = NULL;
		return 0;
	}

	if (BIOVEC_VIRT_MERGEABLE(__BVEC_END(req->biotail), __BVEC_START(bio)))
		return ll_new_mergeable(q, req, bio);

	return ll_new_hw_segment(q, req, bio);
}

static int ll_front_merge_fn(request_queue_t *q, struct request *req, 
			     struct bio *bio)
{
	if (req->nr_sectors + bio_sectors(bio) > q->max_sectors) {
		req->flags |= REQ_NOMERGE;
		q->last_merge = NULL;
		return 0;
	}

	if (BIOVEC_VIRT_MERGEABLE(__BVEC_END(bio), __BVEC_START(req->bio)))
		return ll_new_mergeable(q, req, bio);

	return ll_new_hw_segment(q, req, bio);
}

static int ll_merge_requests_fn(request_queue_t *q, struct request *req,
				struct request *next)
{
	int total_phys_segments = req->nr_phys_segments +next->nr_phys_segments;
	int total_hw_segments = req->nr_hw_segments + next->nr_hw_segments;

	/*
	 * First check if the either of the requests are re-queued
	 * requests.  Can't merge them if they are.
	 */
	if (req->special || next->special)
		return 0;

	/*
	 * Will it become to large?
	 */
	if ((req->nr_sectors + next->nr_sectors) > q->max_sectors)
		return 0;

	total_phys_segments = req->nr_phys_segments + next->nr_phys_segments;
	if (blk_phys_contig_segment(q, req->biotail, next->bio))
		total_phys_segments--;

	if (total_phys_segments > q->max_phys_segments)
		return 0;

	total_hw_segments = req->nr_hw_segments + next->nr_hw_segments;
	if (blk_hw_contig_segment(q, req->biotail, next->bio))
		total_hw_segments--;

	if (total_hw_segments > q->max_hw_segments)
		return 0;

	/* Merge is OK... */
	req->nr_phys_segments = total_phys_segments;
	req->nr_hw_segments = total_hw_segments;
	return 1;
}

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 *
 * This is called with interrupts off and no requests on the queue and
 * with the queue lock held.
 */
void blk_plug_device(request_queue_t *q)
{
	if (!blk_queue_plugged(q)) {
		spin_lock(&blk_plug_lock);
		list_add_tail(&q->plug_list, &blk_plug_list);
		spin_unlock(&blk_plug_lock);
	}
}

/*
 * remove the queue from the plugged list, if present. called with
 * queue lock held and interrupts disabled.
 */
inline int blk_remove_plug(request_queue_t *q)
{
	if (blk_queue_plugged(q)) {
		spin_lock(&blk_plug_lock);
		list_del_init(&q->plug_list);
		spin_unlock(&blk_plug_lock);
		return 1;
	}

	return 0;
}

/*
 * remove the plug and let it rip..
 */
static inline void __generic_unplug_device(request_queue_t *q)
{
	if (!blk_remove_plug(q))
		return;

	if (test_bit(QUEUE_FLAG_STOPPED, &q->queue_flags))
		return;

	/*
	 * was plugged, fire request_fn if queue has stuff to do
	 */
	if (!elv_queue_empty(q))
		q->request_fn(q);
}

/**
 * generic_unplug_device - fire a request queue
 * @data:    The &request_queue_t in question
 *
 * Description:
 *   Linux uses plugging to build bigger requests queues before letting
 *   the device have at them. If a queue is plugged, the I/O scheduler
 *   is still adding and merging requests on the queue. Once the queue
 *   gets unplugged (either by manually calling this function, or by
 *   calling blk_run_queues()), the request_fn defined for the
 *   queue is invoked and transfers started.
 **/
void generic_unplug_device(void *data)
{
	request_queue_t *q = data;

	spin_lock_irq(q->queue_lock);
	__generic_unplug_device(q);
	spin_unlock_irq(q->queue_lock);
}

/**
 * blk_start_queue - restart a previously stopped queue
 * @q:    The &request_queue_t in question
 *
 * Description:
 *   blk_start_queue() will clear the stop flag on the queue, and call
 *   the request_fn for the queue if it was in a stopped state when
 *   entered. Also see blk_stop_queue(). Must not be called from driver
 *   request function due to recursion issues.
 **/
void blk_start_queue(request_queue_t *q)
{
	if (test_and_clear_bit(QUEUE_FLAG_STOPPED, &q->queue_flags)) {
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);
		if (!elv_queue_empty(q))
			q->request_fn(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

/**
 * __blk_stop_queue: see blk_stop_queue()
 *
 * Description:
 *  Like blk_stop_queue(), but queue_lock must be held
 **/
void __blk_stop_queue(request_queue_t *q)
{
	blk_remove_plug(q);
	set_bit(QUEUE_FLAG_STOPPED, &q->queue_flags);
}

/**
 * blk_stop_queue - stop a queue
 * @q:    The &request_queue_t in question
 *
 * Description:
 *   The Linux block layer assumes that a block driver will consume all
 *   entries on the request queue when the request_fn strategy is called.
 *   Often this will not happen, because of hardware limitations (queue
 *   depth settings). If a device driver gets a 'queue full' response,
 *   or if it simply chooses not to queue more I/O at one point, it can
 *   call this function to prevent the request_fn from being called until
 *   the driver has signalled it's ready to go again. This happens by calling
 *   blk_start_queue() to restart queue operations.
 **/
void blk_stop_queue(request_queue_t *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__blk_stop_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/**
 * blk_run_queues - fire all plugged queues
 *
 * Description:
 *   Start I/O on all plugged queues known to the block layer. Queues that
 *   are currently stopped are ignored. This is equivalent to the older
 *   tq_disk task queue run.
 **/
#define blk_plug_entry(entry) list_entry((entry), request_queue_t, plug_list)
void blk_run_queues(void)
{
	struct list_head local_plug_list;

	INIT_LIST_HEAD(&local_plug_list);

	spin_lock_irq(&blk_plug_lock);

	/*
	 * this will happen fairly often
	 */
	if (list_empty(&blk_plug_list)) {
		spin_unlock_irq(&blk_plug_lock);
		return;
	}

	list_splice_init(&blk_plug_list, &local_plug_list);
	spin_unlock_irq(&blk_plug_lock);
	
	while (!list_empty(&local_plug_list)) {
		request_queue_t *q = blk_plug_entry(local_plug_list.next);

		q->unplug_fn(q);
	}
}

static int __blk_cleanup_queue(struct request_list *list)
{
	struct list_head *head = &list->free;
	struct request *rq;
	int i = 0;

	while (!list_empty(head)) {
		rq = list_entry(head->next, struct request, queuelist);
		list_del_init(&rq->queuelist);
		kmem_cache_free(request_cachep, rq);
		i++;
	}

	if (i != list->count)
		printk("request list leak!\n");

	list->count = 0;
	return i;
}

/**
 * blk_cleanup_queue: - release a &request_queue_t when it is no longer needed
 * @q:    the request queue to be released
 *
 * Description:
 *     blk_cleanup_queue is the pair to blk_init_queue().  It should
 *     be called when a request queue is being released; typically
 *     when a block device is being de-registered.  Currently, its
 *     primary task it to free all the &struct request structures that
 *     were allocated to the queue.
 * Caveat:
 *     Hopefully the low level driver will have finished any
 *     outstanding requests first...
 **/
void blk_cleanup_queue(request_queue_t * q)
{
	int count = (queue_nr_requests*2);

	count -= __blk_cleanup_queue(&q->rq[READ]);
	count -= __blk_cleanup_queue(&q->rq[WRITE]);

	if (count)
		printk("blk_cleanup_queue: leaked requests (%d)\n", count);

	if (blk_queue_tagged(q))
		blk_queue_free_tags(q);

	elevator_exit(q);

	memset(q, 0, sizeof(*q));
}

static int blk_init_free_list(request_queue_t *q)
{
	struct request_list *rl;
	struct request *rq;
	int i;

	INIT_LIST_HEAD(&q->rq[READ].free);
	INIT_LIST_HEAD(&q->rq[WRITE].free);
	q->rq[READ].count = 0;
	q->rq[WRITE].count = 0;

	/*
	 * Divide requests in half between read and write
	 */
	rl = &q->rq[READ];
	for (i = 0; i < (queue_nr_requests*2); i++) {
		rq = kmem_cache_alloc(request_cachep, SLAB_KERNEL);
		if (!rq)
			goto nomem;

		/*
		 * half way through, switch to WRITE list
		 */
		if (i == queue_nr_requests)
			rl = &q->rq[WRITE];

		memset(rq, 0, sizeof(struct request));
		rq->rq_status = RQ_INACTIVE;
		list_add(&rq->queuelist, &rl->free);
		rl->count++;
	}

	init_waitqueue_head(&q->rq[READ].wait);
	init_waitqueue_head(&q->rq[WRITE].wait);
	return 0;
nomem:
	blk_cleanup_queue(q);
	return 1;
}

static int __make_request(request_queue_t *, struct bio *);

/**
 * blk_init_queue  - prepare a request queue for use with a block device
 * @q:    The &request_queue_t to be initialised
 * @rfn:  The function to be called to process requests that have been
 *        placed on the queue.
 *
 * Description:
 *    If a block device wishes to use the standard request handling procedures,
 *    which sorts requests and coalesces adjacent requests, then it must
 *    call blk_init_queue().  The function @rfn will be called when there
 *    are requests on the queue that need to be processed.  If the device
 *    supports plugging, then @rfn may not be called immediately when requests
 *    are available on the queue, but may be called at some time later instead.
 *    Plugged queues are generally unplugged when a buffer belonging to one
 *    of the requests on the queue is needed, or due to memory pressure.
 *
 *    @rfn is not required, or even expected, to remove all requests off the
 *    queue, but only as many as it can handle at a time.  If it does leave
 *    requests on the queue, it is responsible for arranging that the requests
 *    get dealt with eventually.
 *
 *    The queue spin lock must be held while manipulating the requests on the
 *    request queue.
 *
 * Note:
 *    blk_init_queue() must be paired with a blk_cleanup_queue() call
 *    when the block device is deactivated (such as at module unload).
 **/
int blk_init_queue(request_queue_t *q, request_fn_proc *rfn, spinlock_t *lock)
{
	int ret;

	if (blk_init_free_list(q))
		return -ENOMEM;

	if ((ret = elevator_init(q, &iosched_deadline))) {
		blk_cleanup_queue(q);
		return ret;
	}

	q->request_fn		= rfn;
	q->back_merge_fn       	= ll_back_merge_fn;
	q->front_merge_fn      	= ll_front_merge_fn;
	q->merge_requests_fn	= ll_merge_requests_fn;
	q->prep_rq_fn		= NULL;
	q->unplug_fn		= generic_unplug_device;
	q->queue_flags		= (1 << QUEUE_FLAG_CLUSTER);
	q->queue_lock		= lock;

	blk_queue_segment_boundary(q, 0xffffffff);

	blk_queue_make_request(q, __make_request);
	blk_queue_max_segment_size(q, MAX_SEGMENT_SIZE);

	blk_queue_max_hw_segments(q, MAX_HW_SEGMENTS);
	blk_queue_max_phys_segments(q, MAX_PHYS_SEGMENTS);

	INIT_LIST_HEAD(&q->plug_list);

	return 0;
}

#define blkdev_free_rq(list) list_entry((list)->next, struct request, queuelist)
/*
 * Get a free request. queue lock must be held and interrupts
 * disabled on the way in.
 */
static struct request *get_request(request_queue_t *q, int rw)
{
	struct request *rq = NULL;
	struct request_list *rl = q->rq + rw;

	if (!list_empty(&rl->free)) {
		rq = blkdev_free_rq(&rl->free);
		list_del_init(&rq->queuelist);
		rq->ref_count = 1;
		rl->count--;
		if (rl->count < queue_congestion_on_threshold())
			set_queue_congested(q, rw);
		rq->flags = 0;
		rq->rq_status = RQ_ACTIVE;
		rq->errors = 0;
		rq->special = NULL;
		rq->buffer = NULL;
		rq->data = NULL;
		rq->sense = NULL;
		rq->waiting = NULL;
		rq->bio = rq->biotail = NULL;
		rq->q = q;
		rq->rl = rl;
	}

	return rq;
}

/*
 * No available requests for this queue, unplug the device.
 */
static struct request *get_request_wait(request_queue_t *q, int rw)
{
	DEFINE_WAIT(wait);
	struct request_list *rl = &q->rq[rw];
	struct request *rq;

	spin_lock_prefetch(q->queue_lock);

	generic_unplug_device(q);
	do {
		prepare_to_wait_exclusive(&rl->wait, &wait,
					TASK_UNINTERRUPTIBLE);
		if (!rl->count)
			io_schedule();
		finish_wait(&rl->wait, &wait);
		spin_lock_irq(q->queue_lock);
		rq = get_request(q, rw);
		spin_unlock_irq(q->queue_lock);
	} while (rq == NULL);
	return rq;
}

struct request *blk_get_request(request_queue_t *q, int rw, int gfp_mask)
{
	struct request *rq;

	BUG_ON(rw != READ && rw != WRITE);

	spin_lock_irq(q->queue_lock);
	rq = get_request(q, rw);
	spin_unlock_irq(q->queue_lock);

	if (!rq && (gfp_mask & __GFP_WAIT))
		rq = get_request_wait(q, rw);

	if (rq) {
		rq->flags = 0;
		rq->buffer = NULL;
		rq->bio = rq->biotail = NULL;
		rq->waiting = NULL;
	}
	return rq;
}

/*
 * Non-locking blk_get_request variant, for special requests from drivers.
 */
struct request *__blk_get_request(request_queue_t *q, int rw)
{
	struct request *rq;

	BUG_ON(rw != READ && rw != WRITE);

	rq = get_request(q, rw);

	if (rq) {
		rq->flags = 0;
		rq->buffer = NULL;
		rq->bio = rq->biotail = NULL;
		rq->waiting = NULL;
	}
	return rq;
}

/**
 * blk_insert_request - insert a special request in to a request queue
 * @q:		request queue where request should be inserted
 * @rq:		request to be inserted
 * @at_head:	insert request at head or tail of queue
 * @data:	private data
 *
 * Description:
 *    Many block devices need to execute commands asynchronously, so they don't
 *    block the whole kernel from preemption during request execution.  This is
 *    accomplished normally by inserting aritficial requests tagged as
 *    REQ_SPECIAL in to the corresponding request queue, and letting them be
 *    scheduled for actual execution by the request queue.
 *
 *    We have the option of inserting the head or the tail of the queue.
 *    Typically we use the tail for new ioctls and so forth.  We use the head
 *    of the queue for things like a QUEUE_FULL message from a device, or a
 *    host that is unable to accept a particular command.
 */
void blk_insert_request(request_queue_t *q, struct request *rq,
			int at_head, void *data)
{
	unsigned long flags;

	/*
	 * tell I/O scheduler that this isn't a regular read/write (ie it
	 * must not attempt merges on this) and that it acts as a soft
	 * barrier
	 */
	rq->flags |= REQ_SPECIAL | REQ_BARRIER;

	rq->special = data;

	spin_lock_irqsave(q->queue_lock, flags);

	/*
	 * If command is tagged, release the tag
	 */
	if (blk_rq_tagged(rq))
		blk_queue_end_tag(q, rq);

	__elv_add_request(q, rq, !at_head, 0);
	q->request_fn(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

void drive_stat_acct(struct request *rq, int nr_sectors, int new_io)
{
	int rw = rq_data_dir(rq);
	unsigned int major, index;

	if (!rq->rq_disk)
		return;

	if (rw == READ) {
		rq->rq_disk->read_sectors += nr_sectors;
		if (!new_io)
			rq->rq_disk->read_merges++;
	} else if (rw == WRITE) {
		rq->rq_disk->write_sectors += nr_sectors;
		if (!new_io)
			rq->rq_disk->write_merges++;
	}
	if (new_io) {
		disk_round_stats(rq->rq_disk);
		rq->rq_disk->in_flight++;
	}

	major = rq->rq_disk->major;
	index = rq->rq_disk->first_minor >> rq->rq_disk->minor_shift;

	if ((index >= DK_MAX_DISK) || (major >= DK_MAX_MAJOR))
		return;

	dkstat.drive[major][index] += new_io;
	if (rw == READ) {
		dkstat.drive_rio[major][index] += new_io;
		dkstat.drive_rblk[major][index] += nr_sectors;
	} else if (rw == WRITE) {
		dkstat.drive_wio[major][index] += new_io;
		dkstat.drive_wblk[major][index] += nr_sectors;
	} else
		printk(KERN_ERR "drive_stat_acct: cmd not R/W?\n");
}

/*
 * add-request adds a request to the linked list.
 * queue lock is held and interrupts disabled, as we muck with the
 * request queue list.
 */
static inline void add_request(request_queue_t * q, struct request * req,
			       struct list_head *insert_here)
{
	drive_stat_acct(req, req->nr_sectors, 1);

	/*
	 * elevator indicated where it wants this request to be
	 * inserted at elevator_merge time
	 */
	__elv_add_request_pos(q, req, insert_here);
}
 
/*
 * disk_round_stats()	- Round off the performance stats on a struct
 * disk_stats.
 *
 * The average IO queue length and utilisation statistics are maintained
 * by observing the current state of the queue length and the amount of
 * time it has been in this state for.
 *
 * Normally, that accounting is done on IO completion, but that can result
 * in more than a second's worth of IO being accounted for within any one
 * second, leading to >100% utilisation.  To deal with that, we call this
 * function to do a round-off before returning the results when reading
 * /proc/diskstats.  This accounts immediately for all queue usage up to
 * the current jiffies and restarts the counters again.
 */
void disk_round_stats(struct gendisk *disk)
{
	unsigned long now = jiffies;

	disk->time_in_queue += disk->in_flight * (now - disk->stamp);
	disk->stamp = now;

	if (disk->in_flight)
		disk->io_ticks += (now - disk->stamp_idle);
	disk->stamp_idle = now;
}

void __blk_put_request(request_queue_t *q, struct request *req)
{
	struct request_list *rl = req->rl;

	if (unlikely(--req->ref_count))
		return;
	if (unlikely(!q))
		return;

	req->rq_status = RQ_INACTIVE;
	req->q = NULL;
	req->rl = NULL;

	/*
	 * Request may not have originated from ll_rw_blk. if not,
	 * it didn't come out of our reserved rq pools
	 */
	if (rl) {
		int rw = 0;

		BUG_ON(!list_empty(&req->queuelist));

		list_add(&req->queuelist, &rl->free);

		if (rl == &q->rq[WRITE])
			rw = WRITE;
		else if (rl == &q->rq[READ])
			rw = READ;
		else
			BUG();

		rl->count++;
		if (rl->count >= queue_congestion_off_threshold())
			clear_queue_congested(q, rw);
		if (rl->count >= batch_requests && waitqueue_active(&rl->wait))
			wake_up(&rl->wait);
	}
}

void blk_put_request(struct request *req)
{
	request_queue_t *q = req->q;
	
	/*
	 * if req->q isn't set, this request didnt originate from the
	 * block layer, so it's safe to just disregard it
	 */
	if (q) {
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);
		__blk_put_request(q, req);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

/**
 * blk_congestion_wait - wait for a queue to become uncongested
 * @rw: READ or WRITE
 * @timeout: timeout in jiffies
 *
 * Waits for up to @timeout jiffies for a queue (any queue) to exit congestion.
 * If no queues are congested then just return, in the hope that the caller
 * will submit some more IO.
 */
void blk_congestion_wait(int rw, long timeout)
{
	DEFINE_WAIT(wait);
	struct congestion_state *cs = &congestion_states[rw];

	if (atomic_read(&cs->nr_congested_queues) == 0)
		return;
	blk_run_queues();
	prepare_to_wait(&cs->wqh, &wait, TASK_UNINTERRUPTIBLE);
	if (atomic_read(&cs->nr_congested_queues) != 0)
		io_schedule_timeout(timeout);
	finish_wait(&cs->wqh, &wait);
}

/*
 * Has to be called with the request spinlock acquired
 */
static void attempt_merge(request_queue_t *q, struct request *req,
			  struct request *next)
{
	if (!rq_mergeable(req) || !rq_mergeable(next))
		return;

	/*
	 * not contigious
	 */
	if (req->sector + req->nr_sectors != next->sector)
		return;

	if (rq_data_dir(req) != rq_data_dir(next)
	    || req->rq_disk != next->rq_disk
	    || next->waiting || next->special)
		return;

	/*
	 * If we are allowed to merge, then append bio list
	 * from next to rq and release next. merge_requests_fn
	 * will have updated segment counts, update sector
	 * counts here.
	 */
	if (q->merge_requests_fn(q, req, next)) {
		req->biotail->bi_next = next->bio;
		req->biotail = next->biotail;

		req->nr_sectors = req->hard_nr_sectors += next->hard_nr_sectors;

		elv_merge_requests(q, req, next);

		if (req->rq_disk) {
			disk_round_stats(req->rq_disk);
			req->rq_disk->in_flight--;
		}

		blkdev_dequeue_request(next);
		__blk_put_request(q, next);
	}
}

static inline void attempt_back_merge(request_queue_t *q, struct request *rq)
{
	struct list_head *next = rq->queuelist.next;
	struct list_head *sort_head = elv_get_sort_head(q, rq);

	if (next != sort_head)
		attempt_merge(q, rq, list_entry_rq(next));
}

static inline void attempt_front_merge(request_queue_t *q, struct request *rq)
{
	struct list_head *prev = rq->queuelist.prev;
	struct list_head *sort_head = elv_get_sort_head(q, rq);

	if (prev != sort_head)
		attempt_merge(q, list_entry_rq(prev), rq);
}

/**
 * blk_attempt_remerge  - attempt to remerge active head with next request
 * @q:    The &request_queue_t belonging to the device
 * @rq:   The head request (usually)
 *
 * Description:
 *    For head-active devices, the queue can easily be unplugged so quickly
 *    that proper merging is not done on the front request. This may hurt
 *    performance greatly for some devices. The block layer cannot safely
 *    do merging on that first request for these queues, but the driver can
 *    call this function and make it happen any way. Only the driver knows
 *    when it is safe to do so.
 **/
void blk_attempt_remerge(request_queue_t *q, struct request *rq)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	attempt_back_merge(q, rq);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/*
 * Non-locking blk_attempt_remerge variant.
 */
void __blk_attempt_remerge(request_queue_t *q, struct request *rq)
{
	attempt_back_merge(q, rq);
}

static int __make_request(request_queue_t *q, struct bio *bio)
{
	struct request *req, *freereq = NULL;
	int el_ret, rw, nr_sectors, cur_nr_sectors, barrier;
	struct list_head *insert_here;
	sector_t sector;

	sector = bio->bi_sector;
	nr_sectors = bio_sectors(bio);
	cur_nr_sectors = bio_iovec(bio)->bv_len >> 9;
	rw = bio_data_dir(bio);

	/*
	 * low level driver can indicate that it wants pages above a
	 * certain limit bounced to low memory (ie for highmem, or even
	 * ISA dma in theory)
	 */
	blk_queue_bounce(q, &bio);

	spin_lock_prefetch(q->queue_lock);

	barrier = test_bit(BIO_RW_BARRIER, &bio->bi_rw);

	spin_lock_irq(q->queue_lock);
again:
	insert_here = NULL;

	if (blk_queue_empty(q)) {
		blk_plug_device(q);
		goto get_rq;
	}
	if (barrier)
		goto get_rq;

	el_ret = elv_merge(q, &insert_here, bio);
	switch (el_ret) {
		case ELEVATOR_BACK_MERGE:
			req = list_entry_rq(insert_here);

			BUG_ON(!rq_mergeable(req));

			if (!q->back_merge_fn(q, req, bio)) {
				insert_here = &req->queuelist;
				break;
			}

			req->biotail->bi_next = bio;
			req->biotail = bio;
			req->nr_sectors = req->hard_nr_sectors += nr_sectors;
			drive_stat_acct(req, nr_sectors, 0);
			elv_merged_request(q, req);
			attempt_back_merge(q, req);
			goto out;

		case ELEVATOR_FRONT_MERGE:
			req = list_entry_rq(insert_here);

			BUG_ON(!rq_mergeable(req));

			if (!q->front_merge_fn(q, req, bio)) {
				insert_here = req->queuelist.prev;
				break;
			}

			bio->bi_next = req->bio;
			req->bio = bio;
			/*
			 * may not be valid. if the low level driver said
			 * it didn't need a bounce buffer then it better
			 * not touch req->buffer either...
			 */
			req->buffer = bio_data(bio);
			req->current_nr_sectors = cur_nr_sectors;
			req->hard_cur_sectors = cur_nr_sectors;
			req->sector = req->hard_sector = sector;
			req->nr_sectors = req->hard_nr_sectors += nr_sectors;
			drive_stat_acct(req, nr_sectors, 0);
			elv_merged_request(q, req);
			attempt_front_merge(q, req);
			goto out;

		/*
		 * elevator says don't/can't merge. get new request
		 */
		case ELEVATOR_NO_MERGE:
			break;

		default:
			printk("elevator returned crap (%d)\n", el_ret);
			BUG();
	}

	/*
	 * Grab a free request from the freelist - if that is empty, check
	 * if we are doing read ahead and abort instead of blocking for
	 * a free slot.
	 */
get_rq:
	if (freereq) {
		req = freereq;
		freereq = NULL;
	} else if ((req = get_request(q, rw)) == NULL) {
		spin_unlock_irq(q->queue_lock);

		/*
		 * READA bit set
		 */
		if (bio_flagged(bio, BIO_RW_AHEAD))
			goto end_io;

		freereq = get_request_wait(q, rw);
		spin_lock_irq(q->queue_lock);
		goto again;
	}

	/*
	 * first three bits are identical in rq->flags and bio->bi_rw,
	 * see bio.h and blkdev.h
	 */
	req->flags = (bio->bi_rw & 7) | REQ_CMD;

	/*
	 * REQ_BARRIER implies no merging, but lets make it explicit
	 */
	if (barrier)
		req->flags |= (REQ_BARRIER | REQ_NOMERGE);

	req->errors = 0;
	req->hard_sector = req->sector = sector;
	req->hard_nr_sectors = req->nr_sectors = nr_sectors;
	req->current_nr_sectors = req->hard_cur_sectors = cur_nr_sectors;
	req->nr_phys_segments = bio_phys_segments(q, bio);
	req->nr_hw_segments = bio_hw_segments(q, bio);
	req->buffer = bio_data(bio);	/* see ->buffer comment above */
	req->waiting = NULL;
	req->bio = req->biotail = bio;
	req->rq_dev = to_kdev_t(bio->bi_bdev->bd_dev);
	req->rq_disk = bio->bi_bdev->bd_disk;
	req->start_time = jiffies;
	add_request(q, req, insert_here);
out:
	if (freereq)
		__blk_put_request(q, freereq);
	spin_unlock_irq(q->queue_lock);
	return 0;

end_io:
	bio_endio(bio, nr_sectors << 9, -EWOULDBLOCK);
	return 0;
}


/*
 * If bio->bi_dev is a partition, remap the location
 */
static inline void blk_partition_remap(struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;
	struct gendisk *disk = bdev->bd_disk;
	struct hd_struct *p;
	if (bdev == bdev->bd_contains)
		return;

	p = &disk->part[bdev->bd_dev-MKDEV(disk->major,disk->first_minor)-1];
	switch (bio->bi_rw) {
	case READ:
		p->read_sectors += bio_sectors(bio);
		p->reads++;
		break;
	case WRITE:
		p->write_sectors += bio_sectors(bio);
		p->writes++;
		break;
	}
	bio->bi_sector += bdev->bd_offset;
	bio->bi_bdev = bdev->bd_contains;
}

/**
 * generic_make_request: hand a buffer to it's device driver for I/O
 * @bio:  The bio describing the location in memory and on the device.
 *
 * generic_make_request() is used to make I/O requests of block
 * devices. It is passed a &struct bio, which describes the I/O that needs
 * to be done.
 *
 * generic_make_request() does not return any status.  The
 * success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the bio->bi_end_io
 * function described (one day) else where.
 *
 * The caller of generic_make_request must make sure that bi_io_vec
 * are set to describe the memory buffer, and that bi_dev and bi_sector are
 * set to describe the device address, and the
 * bi_end_io and optionally bi_private are set to describe how
 * completion notification should be signaled.
 *
 * generic_make_request and the drivers it calls may use bi_next if this
 * bio happens to be merged with someone else, and may change bi_dev and
 * bi_sector for remaps as it sees fit.  So the values of these fields
 * should NOT be depended on after the call to generic_make_request.
 *
 * */
void generic_make_request(struct bio *bio)
{
	request_queue_t *q;
	sector_t maxsector;
	int ret, nr_sectors = bio_sectors(bio);

	/* Test device or partition size, when known. */
	maxsector = bio->bi_bdev->bd_inode->i_size >> 9;
	if (maxsector) {
		sector_t sector = bio->bi_sector;

		if (maxsector < nr_sectors ||
		    maxsector - nr_sectors < sector) {
			/* This may well happen - the kernel calls
			 * bread() without checking the size of the
			 * device, e.g., when mounting a device. */
			printk(KERN_INFO
			       "attempt to access beyond end of device\n");
			printk(KERN_INFO "%s: rw=%ld, want=%Lu, limit=%Lu\n",
			       bdevname(bio->bi_bdev),
			       bio->bi_rw,
			       (unsigned long long) sector + nr_sectors,
			       (long long) maxsector);

			set_bit(BIO_EOF, &bio->bi_flags);
			goto end_io;
		}
	}

	/*
	 * Resolve the mapping until finished. (drivers are
	 * still free to implement/resolve their own stacking
	 * by explicitly returning 0)
	 *
	 * NOTE: we don't repeat the blk_size check for each new device.
	 * Stacking drivers are expected to know what they are doing.
	 */
	do {
		q = bdev_get_queue(bio->bi_bdev);
		if (!q) {
			printk(KERN_ERR
			       "generic_make_request: Trying to access nonexistent block-device %s (%Lu)\n",
			       bdevname(bio->bi_bdev),
			       (long long) bio->bi_sector);
end_io:
			bio_endio(bio, bio->bi_size, -EIO);
			break;
		}

		if (unlikely(bio_sectors(bio) > q->max_sectors)) {
			printk("bio too big device %s (%u > %u)\n", 
			       bdevname(bio->bi_bdev),
			       bio_sectors(bio),
			       q->max_sectors);
			goto end_io;
		}

		/*
		 * If this device has partitions, remap block n
		 * of partition p to block n+start(p) of the disk.
		 */
		blk_partition_remap(bio);

		ret = q->make_request_fn(q, bio);
	} while (ret);
}

/**
 * submit_bio: submit a bio to the block device layer for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bio: The &struct bio which describes the I/O
 *
 * submit_bio() is very similar in purpose to generic_make_request(), and
 * uses that function to do most of the work. Both are fairly rough
 * interfaces, @bio must be presetup and ready for I/O.
 *
 */
int submit_bio(int rw, struct bio *bio)
{
	int count = bio_sectors(bio);

	BIO_BUG_ON(!bio->bi_size);
	BIO_BUG_ON(!bio->bi_io_vec);
	bio->bi_rw = rw;
	if (rw & WRITE)
		mod_page_state(pgpgout, count);
	else
		mod_page_state(pgpgin, count);
	generic_make_request(bio);
	return 1;
}

inline void blk_recalc_rq_segments(struct request *rq)
{
	struct bio *bio;
	int nr_phys_segs, nr_hw_segs;

	if (!rq->bio)
		return;

	rq->buffer = bio_data(rq->bio);

	nr_phys_segs = nr_hw_segs = 0;
	rq_for_each_bio(bio, rq) {
		/* Force bio hw/phys segs to be recalculated. */
		bio->bi_flags &= ~(1 << BIO_SEG_VALID);

		nr_phys_segs += bio_phys_segments(rq->q, bio);
		nr_hw_segs += bio_hw_segments(rq->q, bio);
	}

	rq->nr_phys_segments = nr_phys_segs;
	rq->nr_hw_segments = nr_hw_segs;
}

inline void blk_recalc_rq_sectors(struct request *rq, int nsect)
{
	if (blk_fs_request(rq)) {
		rq->hard_sector += nsect;
		rq->nr_sectors = rq->hard_nr_sectors -= nsect;
		rq->sector = rq->hard_sector;

		rq->current_nr_sectors = bio_iovec(rq->bio)->bv_len >> 9;
		rq->hard_cur_sectors = rq->current_nr_sectors;

		/*
		 * if total number of sectors is less than the first segment
		 * size, something has gone terribly wrong
		 */
		if (rq->nr_sectors < rq->current_nr_sectors) {
			printk("blk: request botched\n");
			rq->nr_sectors = rq->current_nr_sectors;
		}
	}
}

static int __end_that_request_first(struct request *req, int uptodate,
				    int nr_bytes)
{
	int total_bytes, bio_nbytes, error = 0, next_idx = 0;
	struct bio *bio;

	/*
	 * for a REQ_BLOCK_PC request, we want to carry any eventual
	 * sense key with us all the way through
	 */
	if (!blk_pc_request(req))
		req->errors = 0;

	if (!uptodate) {
		error = -EIO;
		if (!(req->flags & REQ_QUIET))
			printk("end_request: I/O error, dev %s, sector %llu\n",
				req->rq_disk ? req->rq_disk->disk_name : "?",
				(unsigned long long)req->sector);
	}

	total_bytes = bio_nbytes = 0;
	while ((bio = req->bio)) {
		int nbytes;

		if (nr_bytes >= bio->bi_size) {
			req->bio = bio->bi_next;
			nbytes = bio->bi_size;
			bio_endio(bio, nbytes, error);
			next_idx = 0;
			bio_nbytes = 0;
		} else {
			int idx = bio->bi_idx + next_idx;

			if (unlikely(bio->bi_idx >= bio->bi_vcnt)) {
				blk_dump_rq_flags(req, "__end_that");
				printk("%s: bio idx %d >= vcnt %d\n",
						__FUNCTION__,
						bio->bi_idx, bio->bi_vcnt);
				break;
			}

			nbytes = bio_iovec_idx(bio, idx)->bv_len;
			BIO_BUG_ON(nbytes > bio->bi_size);

			/*
			 * not a complete bvec done
			 */
			if (unlikely(nbytes > nr_bytes)) {
				bio_iovec(bio)->bv_offset += nr_bytes;
				bio_iovec(bio)->bv_len -= nr_bytes;
				bio_nbytes += nr_bytes;
				total_bytes += nr_bytes;
				break;
			}

			/*
			 * advance to the next vector
			 */
			next_idx++;
			bio_nbytes += nbytes;
		}

		total_bytes += nbytes;
		nr_bytes -= nbytes;

		if ((bio = req->bio)) {
			/*
			 * end more in this run, or just return 'not-done'
			 */
			if (unlikely(nr_bytes <= 0))
				break;
		}
	}

	/*
	 * completely done
	 */
	if (!req->bio)
		return 0;

	/*
	 * if the request wasn't completed, update state
	 */
	if (bio_nbytes) {
		bio_endio(bio, bio_nbytes, error);
		req->bio->bi_idx += next_idx;
	}

	blk_recalc_rq_sectors(req, total_bytes >> 9);
	blk_recalc_rq_segments(req);
	return 1;
}

/**
 * end_that_request_first - end I/O on a request
 * @req:      the request being processed
 * @uptodate: 0 for I/O error
 * @nr_sectors: number of sectors to end I/O on
 *
 * Description:
 *     Ends I/O on a number of sectors attached to @req, and sets it up
 *     for the next range of segments (if any) in the cluster.
 *
 * Return:
 *     0 - we are done with this request, call end_that_request_last()
 *     1 - still buffers pending for this request
 **/
int end_that_request_first(struct request *req, int uptodate, int nr_sectors)
{
	return __end_that_request_first(req, uptodate, nr_sectors << 9);
}

/**
 * end_that_request_chunk - end I/O on a request
 * @req:      the request being processed
 * @uptodate: 0 for I/O error
 * @nr_bytes: number of bytes to complete
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @req, and sets it up
 *     for the next range of segments (if any). Like end_that_request_first(),
 *     but deals with bytes instead of sectors.
 *
 * Return:
 *     0 - we are done with this request, call end_that_request_last()
 *     1 - still buffers pending for this request
 **/
int end_that_request_chunk(struct request *req, int uptodate, int nr_bytes)
{
	return __end_that_request_first(req, uptodate, nr_bytes);
}

/*
 * queue lock must be held
 */
void end_that_request_last(struct request *req)
{
	struct gendisk *disk = req->rq_disk;
	if (req->waiting)
		complete(req->waiting);

	if (disk) {
		unsigned long duration = jiffies - req->start_time;
		switch (rq_data_dir(req)) {
		    case WRITE:
			disk->writes++;
			disk->write_ticks += duration;
			break;
		    case READ:
			disk->reads++;
			disk->read_ticks += duration;
			break;
		}
		disk_round_stats(disk);
		disk->in_flight--;
	}
	__blk_put_request(req->q, req);
}

int __init blk_dev_init(void)
{
	int total_ram = nr_free_pages() << (PAGE_SHIFT - 10);
	int i;

	request_cachep = kmem_cache_create("blkdev_requests",
			sizeof(struct request), 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!request_cachep)
		panic("Can't create request pool slab cache\n");

	/*
	 * Free request slots per queue.  One per quarter-megabyte.
	 * We use this many requests for reads, and this many for writes.
	 */
	queue_nr_requests = (total_ram >> 9) & ~7;
	if (queue_nr_requests < 16)
		queue_nr_requests = 16;
	if (queue_nr_requests > 128)
		queue_nr_requests = 128;

	batch_requests = queue_nr_requests / 8;
	if (batch_requests > 8)
		batch_requests = 8;

	printk("block request queues:\n");
	printk(" %d requests per read queue\n", queue_nr_requests);
	printk(" %d requests per write queue\n", queue_nr_requests);
	printk(" %d requests per batch\n", batch_requests);
	printk(" enter congestion at %d\n", queue_congestion_on_threshold());
	printk(" exit congestion at %d\n", queue_congestion_off_threshold());

	blk_max_low_pfn = max_low_pfn;
	blk_max_pfn = max_pfn;

	for (i = 0; i < ARRAY_SIZE(congestion_states); i++) {
		init_waitqueue_head(&congestion_states[i].wqh);
		atomic_set(&congestion_states[i].nr_congested_queues, 0);
	}
	return 0;
};

EXPORT_SYMBOL(end_that_request_first);
EXPORT_SYMBOL(end_that_request_chunk);
EXPORT_SYMBOL(end_that_request_last);
EXPORT_SYMBOL(blk_init_queue);
EXPORT_SYMBOL(blk_cleanup_queue);
EXPORT_SYMBOL(blk_queue_make_request);
EXPORT_SYMBOL(blk_queue_bounce_limit);
EXPORT_SYMBOL(generic_make_request);
EXPORT_SYMBOL(generic_unplug_device);
EXPORT_SYMBOL(blk_plug_device);
EXPORT_SYMBOL(blk_remove_plug);
EXPORT_SYMBOL(blk_attempt_remerge);
EXPORT_SYMBOL(__blk_attempt_remerge);
EXPORT_SYMBOL(blk_max_low_pfn);
EXPORT_SYMBOL(blk_max_pfn);
EXPORT_SYMBOL(blk_queue_max_sectors);
EXPORT_SYMBOL(blk_queue_max_phys_segments);
EXPORT_SYMBOL(blk_queue_max_hw_segments);
EXPORT_SYMBOL(blk_queue_max_segment_size);
EXPORT_SYMBOL(blk_queue_hardsect_size);
EXPORT_SYMBOL(blk_queue_segment_boundary);
EXPORT_SYMBOL(blk_queue_dma_alignment);
EXPORT_SYMBOL(blk_rq_map_sg);
EXPORT_SYMBOL(blk_nohighio);
EXPORT_SYMBOL(blk_dump_rq_flags);
EXPORT_SYMBOL(submit_bio);
EXPORT_SYMBOL(blk_queue_assign_lock);
EXPORT_SYMBOL(blk_phys_contig_segment);
EXPORT_SYMBOL(blk_hw_contig_segment);
EXPORT_SYMBOL(blk_get_request);
EXPORT_SYMBOL(__blk_get_request);
EXPORT_SYMBOL(blk_put_request);
EXPORT_SYMBOL(blk_insert_request);

EXPORT_SYMBOL(blk_queue_prep_rq);
EXPORT_SYMBOL(blk_queue_merge_bvec);

EXPORT_SYMBOL(blk_queue_find_tag);
EXPORT_SYMBOL(blk_queue_init_tags);
EXPORT_SYMBOL(blk_queue_free_tags);
EXPORT_SYMBOL(blk_queue_start_tag);
EXPORT_SYMBOL(blk_queue_end_tag);
EXPORT_SYMBOL(blk_queue_invalidate_tags);

EXPORT_SYMBOL(blk_start_queue);
EXPORT_SYMBOL(blk_stop_queue);
EXPORT_SYMBOL(__blk_stop_queue);
EXPORT_SYMBOL(blk_run_queues);
