/*
 * queue.h: queue handling
 *
 * (c) 1997 Russell King
 */
#ifndef QUEUE_H
#define QUEUE_H

typedef struct {
	struct queue_entry *head;		/* head of queue */
	struct queue_entry *tail;		/* tail of queue */
	struct queue_entry *free;		/* free list */
	void *alloc;				/* start of allocated mem */
} Queue_t;

/*
 * Function: void queue_initialise (Queue_t *queue)
 * Purpose : initialise a queue
 * Params  : queue - queue to initialise
 */
extern int queue_initialise (Queue_t *queue);

/*
 * Function: void queue_free (Queue_t *queue)
 * Purpose : free a queue
 * Params  : queue - queue to free
 */
extern void queue_free (Queue_t *queue);

/*
 * Function: Scsi_Cmnd *queue_remove (queue)
 * Purpose : removes first SCSI command from a queue
 * Params  : queue   - queue to remove command from
 * Returns : Scsi_Cmnd if successful (and a reference), or NULL if no command available
 */
extern Scsi_Cmnd *queue_remove (Queue_t *queue);

/*
 * Function: Scsi_Cmnd *queue_remove_exclude_ref (queue, exclude, ref)
 * Purpose : remove a SCSI command from a queue
 * Params  : queue   - queue to remove command from
 *	     exclude - array of busy LUNs
 *	     ref     - a reference that can be used to put the command back
 * Returns : Scsi_Cmnd if successful (and a reference), or NULL if no command available
 */
extern Scsi_Cmnd *queue_remove_exclude (Queue_t *queue, unsigned char *exclude);

/*
 * Function: int queue_add_cmd_ordered (Queue_t *queue, Scsi_Cmnd *SCpnt)
 * Purpose : Add a new command onto a queue, queueing REQUEST_SENSE first
 * Params  : queue - destination queue
 *	     SCpnt - command to add
 * Returns : 0 on error, !0 on success
 */
extern int queue_add_cmd_ordered (Queue_t *queue, Scsi_Cmnd *SCpnt);

/*
 * Function: int queue_add_cmd_tail (Queue_t *queue, Scsi_Cmnd *SCpnt)
 * Purpose : Add a new command onto a queue, queueing at end of list
 * Params  : queue - destination queue
 *	     SCpnt - command to add
 * Returns : 0 on error, !0 on success
 */
extern int queue_add_cmd_tail (Queue_t *queue, Scsi_Cmnd *SCpnt);

/*
 * Function: Scsi_Cmnd *queue_remove_tgtluntag (queue, target, lun, tag)
 * Purpose : remove a SCSI command from the queue for a specified target/lun/tag
 * Params  : queue  - queue to remove command from
 *	     target - target that we want
 *	     lun    - lun on device
 *	     tag    - tag on device
 * Returns : Scsi_Cmnd if successful, or NULL if no command satisfies requirements
 */
extern Scsi_Cmnd *queue_remove_tgtluntag (Queue_t *queue, int target, int lun, int tag);

/*
 * Function: int queue_probetgtlun (queue, target, lun)
 * Purpose : check to see if we have a command in the queue for the specified
 *	     target/lun.
 * Params  : queue  - queue to look in
 *	     target - target we want to probe
 *	     lun    - lun on target
 * Returns : 0 if not found, != 0 if found
 */
extern int queue_probetgtlun (Queue_t *queue, int target, int lun);

/*
 * Function: int queue_cmdonqueue (queue, SCpnt)
 * Purpose : check to see if we have a command on the queue
 * Params  : queue - queue to look in
 *	     SCpnt - command to find
 * Returns : 0 if not found, != 0 if found
 */
int queue_cmdonqueue (Queue_t *queue, Scsi_Cmnd *SCpnt);

/*
 * Function: int queue_removecmd (Queue_t *queue, Scsi_Cmnd *SCpnt)
 * Purpose : remove a specific command from the queues
 * Params  : queue - queue to look in
 *	     SCpnt - command to find
 * Returns : 0 if not found
 */
int queue_removecmd (Queue_t *queue, Scsi_Cmnd *SCpnt);

#endif /* QUEUE_H */
