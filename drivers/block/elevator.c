/*
 *  linux/drivers/block/elevator.c
 *
 *  Block device elevator/IO-scheduler.
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/blk.h>
#include <asm/uaccess.h>

static void elevator_default(struct request * req, elevator_t * elevator,
			     struct list_head * real_head,
			     struct list_head * head, int orig_latency)
{
	struct list_head * entry = real_head, * point = NULL;
	struct request * tmp;
	int sequence = elevator->sequence;
	int latency = orig_latency -= elevator->nr_segments, pass = 0;
	int point_latency = 0xbeefbeef;

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

#ifdef ELEVATOR_DEBUG
void elevator_debug(request_queue_t * q, kdev_t dev)
{
	int read_pendings = 0, nr_segments = 0;
	elevator_t * elevator = &q->elevator;
	struct list_head * entry = &q->queue_head;
	static int counter;

	if (counter++ % 100)
		return;

	while ((entry = entry->prev) != &q->queue_head)
	{
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

	if (read_pendings != elevator->read_pendings)
	{
		printk(KERN_WARNING
		       "%s: elevator read_pendings %d should be %d\n",
		       kdevname(dev), elevator->read_pendings,
		       read_pendings);
		elevator->read_pendings = read_pendings;
	}
	if (nr_segments != elevator->nr_segments)
	{
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
	int ret;
	blkelv_ioctl_arg_t output;

	output.queue_ID			= elevator;
	output.read_latency		= elevator->read_latency;
	output.write_latency		= elevator->write_latency;
	output.max_bomb_segments	= elevator->max_bomb_segments;

	ret = -EFAULT;
	if (copy_to_user(arg, &output, sizeof(blkelv_ioctl_arg_t)))
		goto out;
	ret = 0;
 out:
	return ret;
}

int blkelvset_ioctl(elevator_t * elevator, const blkelv_ioctl_arg_t * arg)
{
	blkelv_ioctl_arg_t input;
	int ret;

	ret = -EFAULT;
	if (copy_from_user(&input, arg, sizeof(blkelv_ioctl_arg_t)))
		goto out;

	ret = -EINVAL;
	if (input.read_latency < 0)
		goto out;
	if (input.write_latency < 0)
		goto out;
	if (input.max_bomb_segments <= 0)
		goto out;

	elevator->read_latency		= input.read_latency;
	elevator->write_latency		= input.write_latency;
	elevator->max_bomb_segments	= input.max_bomb_segments;

	ret = 0;
 out:
	return ret;
}

void elevator_init(elevator_t * elevator)
{
	*elevator = ELEVATOR_DEFAULTS;
}
