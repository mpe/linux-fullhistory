/*
 * queue.c: queue handling primitives
 *
 * (c) 1997 Russell King
 *
 * Changelog:
 *  15-Sep-1997 RMK	Created.
 *  11-Oct-1997	RMK	Corrected problem with queue_remove_exclude
 *			not updating internal linked list properly
 *			(was causing commands to go missing).
 */

#define SECTOR_SIZE 512

#include <linux/module.h>
#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/malloc.h>

#include "../../scsi/scsi.h"

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("SCSI command queueing");

typedef struct queue_entry {
	struct queue_entry *next;
	struct queue_entry *prev;
	unsigned long	   magic;
	Scsi_Cmnd	   *SCpnt;
} QE_t;

#define QUEUE_MAGIC_FREE	0xf7e1c9a3
#define QUEUE_MAGIC_USED	0xf7e1cc33

#include "queue.h"

/*
 * Function: void queue_initialise (Queue_t *queue)
 * Purpose : initialise a queue
 * Params  : queue - queue to initialise
 */
int queue_initialise (Queue_t *queue)
{
	unsigned int nqueues;
	QE_t *q;

	queue->alloc = queue->free = q = (QE_t *) kmalloc (SECTOR_SIZE, GFP_KERNEL);
	if (q) {
		nqueues = SECTOR_SIZE / sizeof (QE_t);

		for (; nqueues; q++, nqueues--) {
			q->next = q + 1;
			q->prev = NULL;
			q->magic = QUEUE_MAGIC_FREE;
			q->SCpnt = NULL;
		}
		q->next = NULL;
	}

	return q != NULL;
}

/*
 * Function: void queue_free (Queue_t *queue)
 * Purpose : free a queue
 * Params  : queue - queue to free
 */
void queue_free (Queue_t *queue)
{
	if (queue->alloc)
		kfree (queue->alloc);
}
     

/*
 * Function: int queue_add_cmd_ordered (Queue_t *queue, Scsi_Cmnd *SCpnt)
 * Purpose : Add a new command onto a queue, adding REQUEST_SENSE to head.
 * Params  : queue - destination queue
 *	     SCpnt - command to add
 * Returns : 0 on error, !0 on success
 */
int queue_add_cmd_ordered (Queue_t *queue, Scsi_Cmnd *SCpnt)
{
	unsigned long flags;
	QE_t *q;

	save_flags_cli (flags);
	q = queue->free;
	if (q)
		queue->free = q->next;

	if (q) {
		if (q->magic != QUEUE_MAGIC_FREE) {
			restore_flags (flags);
			panic ("scsi queues corrupted - queue entry not free");
		}

		q->magic = QUEUE_MAGIC_USED;
		q->SCpnt = SCpnt;

		if (SCpnt->cmnd[0] == REQUEST_SENSE) { /* request_sense gets put on the queue head */
			if (queue->head) {
				q->prev = NULL;
				q->next = queue->head;
				queue->head->prev = q;
				queue->head = q;
			} else {
			    	q->next = q->prev = NULL;
			    	queue->head = queue->tail = q;
			}
		} else {				/* others get put on the tail */
			if (queue->tail) {
				q->next = NULL;
				q->prev = queue->tail;
				queue->tail->next = q;
				queue->tail = q;
			} else {
				q->next = q->prev = NULL;
				queue->head = queue->tail = q;
			}
		}    
	}
	restore_flags (flags);

	return q != NULL;
}

/*
 * Function: int queue_add_cmd_tail (Queue_t *queue, Scsi_Cmnd *SCpnt)
 * Purpose : Add a new command onto a queue, adding onto tail of list
 * Params  : queue - destination queue
 *	     SCpnt - command to add
 * Returns : 0 on error, !0 on success
 */
int queue_add_cmd_tail (Queue_t *queue, Scsi_Cmnd *SCpnt)
{
	unsigned long flags;
	QE_t *q;

	save_flags_cli (flags);
	q = queue->free;
	if (q)
		queue->free = q->next;

	if (q) {
		if (q->magic != QUEUE_MAGIC_FREE) {
			restore_flags (flags);
			panic ("scsi queues corrupted - queue entry not free");
		}

		q->magic = QUEUE_MAGIC_USED;
		q->SCpnt = SCpnt;

		if (queue->tail) {
			q->next = NULL;
			q->prev = queue->tail;
			queue->tail->next = q;
			queue->tail = q;
		} else {
			q->next = q->prev = NULL;
			queue->head = queue->tail = q;
		}    
	}
	restore_flags (flags);

	return q != NULL;
}

/*
 * Function: Scsi_Cmnd *queue_remove_exclude (queue, exclude)
 * Purpose : remove a SCSI command from a queue
 * Params  : queue   - queue to remove command from
 *	     exclude - bit array of target&lun which is busy
 * Returns : Scsi_Cmnd if successful (and a reference), or NULL if no command available
 */
Scsi_Cmnd *queue_remove_exclude (Queue_t *queue, unsigned char *exclude)
{
	unsigned long flags;
	Scsi_Cmnd *SCpnt;
	QE_t *q, *prev;

	save_flags_cli (flags);
	for (q = queue->head, prev = NULL; q; q = q->next) {
		if (exclude && !test_bit (q->SCpnt->target * 8 + q->SCpnt->lun, exclude))
			break;
		prev = q;
	}

	if (q) {
		if (q->magic != QUEUE_MAGIC_USED) {
			restore_flags (flags);
			panic ("q_remove_exclude: scsi queues corrupted - queue entry not used");
		}
		if (q->prev != prev)
			panic ("q_remove_exclude: scsi queues corrupted - q->prev != prev");

		if (!prev) {
			queue->head = q->next;
			if (queue->head)
				queue->head->prev = NULL;
			else
				queue->tail = NULL;
		} else {
			prev->next = q->next;
			if (prev->next)
				prev->next->prev = prev;
			else
				queue->tail = prev;
		}

		SCpnt = q->SCpnt;

		q->next = queue->free;
		queue->free = q;
		q->magic = QUEUE_MAGIC_FREE;
	} else
		SCpnt = NULL;

	restore_flags (flags);

	return SCpnt;
}

/*
 * Function: Scsi_Cmnd *queue_remove (queue)
 * Purpose : removes first SCSI command from a queue
 * Params  : queue   - queue to remove command from
 * Returns : Scsi_Cmnd if successful (and a reference), or NULL if no command available
 */
Scsi_Cmnd *queue_remove (Queue_t *queue)
{
	unsigned long flags;
	Scsi_Cmnd *SCpnt;
	QE_t *q;

	save_flags_cli (flags);
	q = queue->head;
	if (q) {
		queue->head = q->next;
		if (queue->head)
			queue->head->prev = NULL;
		else
			queue->tail = NULL;

		if (q->magic != QUEUE_MAGIC_USED) {
			restore_flags (flags);
			panic ("scsi queues corrupted - queue entry not used");
		}

		SCpnt = q->SCpnt;

		q->next = queue->free;
		queue->free = q;
		q->magic = QUEUE_MAGIC_FREE;
	} else
		SCpnt = NULL;

	restore_flags (flags);

	return SCpnt;
}

/*
 * Function: Scsi_Cmnd *queue_remove_tgtluntag (queue, target, lun, tag)
 * Purpose : remove a SCSI command from the queue for a specified target/lun/tag
 * Params  : queue  - queue to remove command from
 *	     target - target that we want
 *	     lun    - lun on device
 *	     tag    - tag on device
 * Returns : Scsi_Cmnd if successful, or NULL if no command satisfies requirements
 */
Scsi_Cmnd *queue_remove_tgtluntag (Queue_t *queue, int target, int lun, int tag)
{
	unsigned long flags;
	Scsi_Cmnd *SCpnt;
	QE_t *q, *prev;

	save_flags_cli (flags);
	for (q = queue->head, prev = NULL; q; q = q->next) {
		if (q->SCpnt->target == target &&
		    q->SCpnt->lun == lun &&
		    q->SCpnt->tag == tag)
			break;

		prev = q;
	}

	if (q) {
		if (q->magic != QUEUE_MAGIC_USED) {
			restore_flags (flags);
			panic ("q_remove_tgtluntag: scsi queues corrupted - queue entry not used");
		}
		if (q->prev != prev)
			panic ("q_remove_tgtluntag: scsi queues corrupted - q->prev != prev");

		if (!prev) {
			queue->head = q->next;
			if (queue->head)
				queue->head->prev = NULL;
			else
				queue->tail = NULL;
		} else {
			prev->next = q->next;
			if (prev->next)
				prev->next->prev = prev;
			else
				queue->tail = prev;
		}

		SCpnt = q->SCpnt;

		q->magic = QUEUE_MAGIC_FREE;
		q->next = queue->free;
		queue->free = q;
	} else
		SCpnt = NULL;

	restore_flags (flags);

	return SCpnt;
}

/*
 * Function: int queue_probetgtlun (queue, target, lun)
 * Purpose : check to see if we have a command in the queue for the specified
 *	     target/lun.
 * Params  : queue  - queue to look in
 *	     target - target we want to probe
 *	     lun    - lun on target
 * Returns : 0 if not found, != 0 if found
 */
int queue_probetgtlun (Queue_t *queue, int target, int lun)
{
	QE_t *q;

	for (q = queue->head; q; q = q->next)
		if (q->SCpnt->target == target &&
		    q->SCpnt->lun == lun)
			break;

	return q != NULL;
}

/*
 * Function: int queue_cmdonqueue (queue, SCpnt)
 * Purpose : check to see if we have a command on the queue
 * Params  : queue - queue to look in
 *	     SCpnt - command to find
 * Returns : 0 if not found, != 0 if found
 */
int queue_cmdonqueue (Queue_t *queue, Scsi_Cmnd *SCpnt)
{
	QE_t *q;

	for (q = queue->head; q; q = q->next)
		if (q->SCpnt == SCpnt)
			break;

	return q != NULL;
}

/*
 * Function: int queue_removecmd (Queue_t *queue, Scsi_Cmnd *SCpnt)
 * Purpose : remove a specific command from the queues
 * Params  : queue - queue to look in
 *	     SCpnt - command to find
 * Returns : 0 if not found
 */
int queue_removecmd (Queue_t *queue, Scsi_Cmnd *SCpnt)
{
	unsigned long flags;
	QE_t *q, *prev;

	save_flags_cli (flags);
	for (q = queue->head, prev = NULL; q; q = q->next) {
		if (q->SCpnt == SCpnt)
			break;

		prev = q;
	}

	if (q) {
		if (q->magic != QUEUE_MAGIC_USED) {
			restore_flags (flags);
			panic ("q_removecmd: scsi queues corrupted - queue entry not used");
		}
		if (q->prev != prev)
			panic ("q_removecmd: scsi queues corrupted - q->prev != prev");

		if (!prev) {
			queue->head = q->next;
			if (queue->head)
				queue->head->prev = NULL;
			else
				queue->tail = NULL;
		} else {
			prev->next = q->next;
			if (prev->next)
				prev->next->prev = prev;
			else
				queue->tail = prev;
		}

		q->magic = QUEUE_MAGIC_FREE;
		q->next = queue->free;
		queue->free = q;
	}

	restore_flags (flags);

	return q != NULL;
}

EXPORT_SYMBOL(queue_initialise);
EXPORT_SYMBOL(queue_free);
EXPORT_SYMBOL(queue_remove);
EXPORT_SYMBOL(queue_remove_exclude);
EXPORT_SYMBOL(queue_add_cmd_ordered);
EXPORT_SYMBOL(queue_add_cmd_tail);
EXPORT_SYMBOL(queue_remove_tgtluntag);
EXPORT_SYMBOL(queue_probetgtlun);
EXPORT_SYMBOL(queue_cmdonqueue);
EXPORT_SYMBOL(queue_removecmd);

#ifdef MODULE
int init_module (void)
{
	return 0;
}

void cleanup_module (void)
{
}
#endif
