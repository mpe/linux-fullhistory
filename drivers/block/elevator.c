/*
 *  linux/drivers/block/elevator.c
 *
 *  Block device elevator/IO-scheduler.
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 * 30042000 Jens Axboe <axboe@suse.de> :
 *
 * Split the elevator a bit so that it is possible to choose a different
 * one or even write a new "plug in". There are three pieces:
 * - elevator_fn, inserts a new request in the queue list
 * - elevator_merge_fn, decides whether a new buffer can be merged with
 *   an existing request
 * - elevator_dequeue_fn, called when a request is taken off the active list
 *
 * 20082000 Dave Jones <davej@suse.de> :
 * Removed tests for max-bomb-segments, which was breaking elvtune
 *  when run without -bN
 *
 * Jens:
 * - Rework again to work with bio instead of buffer_heads
 * - loose bi_dev comparisons, partition handling is right now
 * - completely modularize elevator setup and teardown
 *
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/blk.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>

#include <asm/uaccess.h>

/*
 * This is a bit tricky. It's given that bio and rq are for the same
 * device, but the next request might of course not be. Run through
 * the tests below to check if we want to insert here if we can't merge
 * bio into an existing request
 */
inline int bio_rq_in_between(struct bio *bio, struct request *rq,
			     struct list_head *head)
{
	struct list_head *next;
	struct request *next_rq;

	/*
	 * if .next is a valid request
	 */
	next = rq->queuelist.next;
	if (unlikely(next == head))
		return 0;

	next_rq = list_entry(next, struct request, queuelist);

	BUG_ON(next_rq->flags & REQ_STARTED);

	/*
	 * not a sector based request
	 */
	if (!(next_rq->flags & REQ_CMD))
		return 0;

	/*
	 * if the device is different (not a normal case) just check if
	 * bio is after rq
	 */
	if (!kdev_same(next_rq->rq_dev, rq->rq_dev))
		return bio->bi_sector > rq->sector;

	/*
	 * ok, rq, next_rq and bio are on the same device. if bio is in between
	 * the two, this is the sweet spot
	 */
	if (bio->bi_sector < next_rq->sector && bio->bi_sector > rq->sector)
		return 1;

	/*
	 * next_rq is ordered wrt rq, but bio is not in between the two
	 */
	if (next_rq->sector > rq->sector)
		return 0;

	/*
	 * next_rq and rq not ordered, if we happen to be either before
	 * next_rq or after rq insert here anyway
	 */
	if (bio->bi_sector > rq->sector || bio->bi_sector < next_rq->sector)
		return 1;

	return 0;
}

/*
 * can we safely merge with this request?
 */
inline int elv_rq_merge_ok(struct request *rq, struct bio *bio)
{
	if (!rq_mergeable(rq))
		return 0;

	/*
	 * different data direction or already started, don't merge
	 */
	if (bio_data_dir(bio) != rq_data_dir(rq))
		return 0;

	/*
	 * same device and no special stuff set, merge is ok
	 */
	if (kdev_same(rq->rq_dev, bio->bi_dev) && !rq->waiting && !rq->special)
		return 1;

	return 0;
}

inline int elv_try_merge(struct request *__rq, struct bio *bio)
{
	unsigned int count = bio_sectors(bio);
	int ret = ELEVATOR_NO_MERGE;

	/*
	 * we can merge and sequence is ok, check if it's possible
	 */
	if (elv_rq_merge_ok(__rq, bio)) {
		if (__rq->sector + __rq->nr_sectors == bio->bi_sector) {
			ret = ELEVATOR_BACK_MERGE;
		} else if (__rq->sector - count == bio->bi_sector) {
			__rq->elevator_sequence -= count;
			ret = ELEVATOR_FRONT_MERGE;
		}
	}

	return ret;
}

int elevator_linus_merge(request_queue_t *q, struct request **req,
			 struct bio *bio)
{
	struct list_head *entry;
	struct request *__rq;
	int ret;

	/*
	 * give a one-shot try to merging with the last touched
	 * request
	 */
	if (q->last_merge) {
		__rq = list_entry_rq(q->last_merge);
		BUG_ON(__rq->flags & REQ_STARTED);

		if ((ret = elv_try_merge(__rq, bio))) {
			*req = __rq;
			return ret;
		}
	}

	entry = &q->queue_head;
	ret = ELEVATOR_NO_MERGE;
	while ((entry = entry->prev) != &q->queue_head) {
		__rq = list_entry_rq(entry);

		if (__rq->flags & (REQ_BARRIER | REQ_STARTED))
			break;

		/*
		 * simply "aging" of requests in queue
		 */
		if (__rq->elevator_sequence-- <= 0)
			break;
		if (!(__rq->flags & REQ_CMD))
			continue;
		if (__rq->elevator_sequence < bio_sectors(bio))
			break;

		if (!*req && bio_rq_in_between(bio, __rq, &q->queue_head))
			*req = __rq;

		if ((ret = elv_try_merge(__rq, bio))) {
			*req = __rq;
			q->last_merge = &__rq->queuelist;
			break;
		}
	}

	return ret;
}

void elevator_linus_merge_cleanup(request_queue_t *q, struct request *req, int count)
{
	struct list_head *entry;

	BUG_ON(req->q != q);

	/*
	 * second pass scan of requests that got passed over, if any
	 */
	entry = &req->queuelist;
	while ((entry = entry->next) != &q->queue_head) {
		struct request *tmp;
		tmp = list_entry_rq(entry);
		tmp->elevator_sequence -= count;
	}
}

void elevator_linus_merge_req(struct request *req, struct request *next)
{
	if (next->elevator_sequence < req->elevator_sequence)
		req->elevator_sequence = next->elevator_sequence;
}

void elv_add_request_fn(request_queue_t *q, struct request *rq,
			       struct list_head *insert_here)
{
	list_add(&rq->queuelist, insert_here);

	/*
	 * new merges must not precede this barrier
	 */
	if (rq->flags & REQ_BARRIER)
		q->last_merge = NULL;
	else if (!q->last_merge)
		q->last_merge = &rq->queuelist;
}

struct request *elv_next_request_fn(request_queue_t *q)
{
	if (!blk_queue_empty(q))
		return list_entry_rq(q->queue_head.next);

	return NULL;
}

int elv_linus_init(request_queue_t *q, elevator_t *e)
{
	return 0;
}

void elv_linus_exit(request_queue_t *q, elevator_t *e)
{
}

/*
 * See if we can find a request that this buffer can be coalesced with.
 */
int elevator_noop_merge(request_queue_t *q, struct request **req,
			struct bio *bio)
{
	struct list_head *entry = &q->queue_head;
	struct request *__rq;
	int ret;

	if (q->last_merge) {
		__rq = list_entry_rq(q->last_merge);
		BUG_ON(__rq->flags & REQ_STARTED);

		if ((ret = elv_try_merge(__rq, bio))) {
			*req = __rq;
			return ret;
		}
	}

	while ((entry = entry->prev) != &q->queue_head) {
		__rq = list_entry_rq(entry);

		if (__rq->flags & (REQ_BARRIER | REQ_STARTED))
			break;

		if (!(__rq->flags & REQ_CMD))
			continue;

		if ((ret = elv_try_merge(__rq, bio))) {
			*req = __rq;
			q->last_merge = &__rq->queuelist;
			return ret;
		}
	}

	return ELEVATOR_NO_MERGE;
}

void elevator_noop_merge_cleanup(request_queue_t *q, struct request *req, int count) {}

void elevator_noop_merge_req(struct request *req, struct request *next) {}

int elevator_init(request_queue_t *q, elevator_t *e, elevator_t type)
{
	*e = type;

	INIT_LIST_HEAD(&q->queue_head);
	q->last_merge = NULL;

	if (e->elevator_init_fn)
		return e->elevator_init_fn(q, e);

	return 0;
}

void elevator_exit(request_queue_t *q, elevator_t *e)
{
	if (e->elevator_exit_fn)
		e->elevator_exit_fn(q, e);
}

int elevator_global_init(void)
{
	return 0;
}

module_init(elevator_global_init);
