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
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/blk.h>
#include <asm/uaccess.h>

void elevator_default(struct request *req, elevator_t * elevator,
		      struct list_head * real_head,
		      struct list_head * head, int orig_latency)
{
	struct list_head * entry = real_head, * point = NULL;
	struct request * tmp;
	int sequence = elevator->sequence;
	int latency = orig_latency -= elevator->nr_segments, pass = 0;
	int point_latency = 0xbeefbeef;

	if (list_empty(real_head)) {
		req->elevator_sequence = elevator_sequence(elevator, orig_latency);
		list_add(&req->queue, real_head);
		return;
	}

	while ((entry = entry->prev) != head) {
		if (!point && latency >= 0) {
			point = entry;
			point_latency = latency;
		}
		tmp = blkdev_entry_to_request(entry);
		if (elevator_sequence_before(tmp->elevator_sequence, sequence) || !tmp->q)
			break;
		if (latency >= 0) {
			if (IN_ORDER(tmp, req) ||
			    (pass && !IN_ORDER(tmp, blkdev_next_request(tmp))))
				goto link;
		}
		latency += tmp->nr_segments;
		pass = 1;
	}

	if (point) {
		entry = point;
		latency = point_latency;
	}

 link:
	list_add(&req->queue, entry);
	req->elevator_sequence = elevator_sequence(elevator, latency);
}

int elevator_default_merge(request_queue_t *q, struct request **req,
			   struct buffer_head *bh, int rw,
			   int *max_sectors, int *max_segments)
{
	struct list_head *entry, *head = &q->queue_head;
	unsigned int count = bh->b_size >> 9;
	elevator_t *elevator = &q->elevator;
	int orig_latency, latency, sequence, action, starving = 0;

	/*
	 * Avoid write-bombs as not to hurt interactiveness of reads
	 */
	if (rw == WRITE)
		*max_segments = elevator->max_bomb_segments;

	latency = orig_latency = elevator_request_latency(elevator, rw);
	sequence = elevator->sequence;
	
	if (q->head_active && !q->plugged)
		head = head->next;

	entry = head;
	while ((entry = entry->prev) != head && !starving) {
		*req = blkdev_entry_to_request(entry);
		latency += (*req)->nr_segments;
		if (elevator_sequence_before((*req)->elevator_sequence, sequence))
			starving = 1;
		if (latency < 0)
			continue;
		if ((*req)->sem)
			continue;
		if ((*req)->cmd != rw)
			continue;
		if ((*req)->nr_sectors + count > *max_sectors)
			continue;
		if ((*req)->rq_dev != bh->b_rdev)
			continue;
		if ((*req)->sector + (*req)->nr_sectors == bh->b_rsector) {
			if (latency - (*req)->nr_segments < 0)
				break;
			action = ELEVATOR_BACK_MERGE;
		} else if ((*req)->sector - count == bh->b_rsector) {
			if (starving)
				break;
			action = ELEVATOR_FRONT_MERGE;
		} else {
			continue;
		}
		q->elevator.sequence++;
		return action;
	}
	return ELEVATOR_NO_MERGE;
}

inline void elevator_default_dequeue(struct request *req)
{
	if (req->cmd == READ)
		req->e->read_pendings--;

	req->e->nr_segments -= req->nr_segments;
}

/*
 * Order ascending, but only allow a request to be skipped a certain
 * number of times
 */
void elevator_linus(struct request *req, elevator_t *elevator,
		    struct list_head *real_head,
		    struct list_head *head, int orig_latency)
{
	struct list_head *entry = real_head;
	struct request *tmp;

	if (list_empty(real_head)) {
		list_add(&req->queue, real_head);
		return;
	}

	while ((entry = entry->prev) != head) {
		tmp = blkdev_entry_to_request(entry);
		if (!tmp->elevator_sequence)
			break;
		if (IN_ORDER(tmp, req))
			break;
		tmp->elevator_sequence--;
	}
	list_add(&req->queue, entry);
}

int elevator_linus_merge(request_queue_t *q, struct request **req,
			 struct buffer_head *bh, int rw,
			 int *max_sectors, int *max_segments)
{
	struct list_head *entry, *head = &q->queue_head;
	unsigned int count = bh->b_size >> 9;

	if (q->head_active && !q->plugged)
		head = head->next;

	entry = head;
	while ((entry = entry->prev) != head) {
		*req = blkdev_entry_to_request(entry);
		if (!(*req)->elevator_sequence)
			break;
		if ((*req)->sem)
			continue;
		if ((*req)->cmd != rw)
			continue;
		if ((*req)->nr_sectors + count > *max_sectors)
			continue;
		if ((*req)->rq_dev != bh->b_rdev)
			continue;
		if ((*req)->sector + (*req)->nr_sectors == bh->b_rsector)
			return ELEVATOR_BACK_MERGE;
		if ((*req)->sector - count == bh->b_rsector)
			return ELEVATOR_FRONT_MERGE;
		(*req)->elevator_sequence--;
	}
	return ELEVATOR_NO_MERGE;
}

/*
 * No request sorting, just add it to the back of the list
 */
void elevator_noop(struct request *req, elevator_t *elevator,
		   struct list_head *real_head, struct list_head *head,
		   int orig_latency)
{
	list_add_tail(&req->queue, real_head);
}

/*
 * See if we can find a request that is buffer can be coalesced with.
 */
int elevator_noop_merge(request_queue_t *q, struct request **req,
			struct buffer_head *bh, int rw,
			int *max_sectors, int *max_segments)
{
	struct list_head *entry, *head = &q->queue_head;
	unsigned int count = bh->b_size >> 9;

	if (q->head_active && !q->plugged)
		head = head->next;

	entry = head;
	while ((entry = entry->prev) != head) {
		*req = blkdev_entry_to_request(entry);
		if ((*req)->sem)
			continue;
		if ((*req)->cmd != rw)
			continue;
		if ((*req)->nr_sectors + count > *max_sectors)
			continue;
		if ((*req)->rq_dev != bh->b_rdev)
			continue;
		if ((*req)->sector + (*req)->nr_sectors == bh->b_rsector)
			return ELEVATOR_BACK_MERGE;
		if ((*req)->sector - count == bh->b_rsector)
			return ELEVATOR_FRONT_MERGE;
	}
	return ELEVATOR_NO_MERGE;
}

/*
 * The noop "elevator" does not do any accounting
 */
void elevator_noop_dequeue(struct request *req) {}

#ifdef ELEVATOR_DEBUG
void elevator_default_debug(request_queue_t * q, kdev_t dev)
{
	int read_pendings = 0, nr_segments = 0;
	elevator_t * elevator = &q->elevator;
	struct list_head * entry = &q->queue_head;
	static int counter;

	if (elevator->elevator_fn != elevator_default)
		return;

	if (counter++ % 100)
		return;

	while ((entry = entry->prev) != &q->queue_head) {
		struct request * req;

		req = blkdev_entry_to_request(entry);
		if (req->cmd != READ && req->cmd != WRITE && (req->q || req->nr_segments))
			printk(KERN_WARNING
			       "%s: elevator req->cmd %d req->nr_segments %u req->q %p\n",
			       kdevname(dev), req->cmd, req->nr_segments, req->q);
		if (!req->q) {
			if (req->nr_segments)
				printk(KERN_WARNING
				       "%s: elevator req->q NULL req->nr_segments %u\n",
				       kdevname(dev), req->nr_segments);
			continue;
		}
		if (req->cmd == READ)
			read_pendings++;
		nr_segments += req->nr_segments;
	}

	if (read_pendings != elevator->read_pendings) {
		printk(KERN_WARNING
		       "%s: elevator read_pendings %d should be %d\n",
		       kdevname(dev), elevator->read_pendings,
		       read_pendings);
		elevator->read_pendings = read_pendings;
	}
	if (nr_segments != elevator->nr_segments) {
		printk(KERN_WARNING
		       "%s: elevator nr_segments %d should be %d\n",
		       kdevname(dev), elevator->nr_segments,
		       nr_segments);
		elevator->nr_segments = nr_segments;
	}
}
#endif

int blkelvget_ioctl(elevator_t * elevator, blkelv_ioctl_arg_t * arg)
{
	blkelv_ioctl_arg_t output;

	output.queue_ID			= elevator->queue_ID;
	output.read_latency		= elevator->read_latency;
	output.write_latency		= elevator->write_latency;
	output.max_bomb_segments	= elevator->max_bomb_segments;

	if (copy_to_user(arg, &output, sizeof(blkelv_ioctl_arg_t)))
		return -EFAULT;

	return 0;
}

int blkelvset_ioctl(elevator_t * elevator, const blkelv_ioctl_arg_t * arg)
{
	blkelv_ioctl_arg_t input;

	if (copy_from_user(&input, arg, sizeof(blkelv_ioctl_arg_t)))
		return -EFAULT;

	if (input.read_latency < 0)
		return -EINVAL;
	if (input.write_latency < 0)
		return -EINVAL;
	if (input.max_bomb_segments <= 0)
		return -EINVAL;

	elevator->read_latency		= input.read_latency;
	elevator->write_latency		= input.write_latency;
	elevator->max_bomb_segments	= input.max_bomb_segments;

	return 0;
}

void elevator_init(elevator_t * elevator, elevator_t type)
{
	static unsigned int queue_ID;

	*elevator = type;
	elevator->queue_ID = queue_ID++;
}
