/*
 *  scsi_lib.c Copyright (C) 1999 Eric Youngdale
 *
 *  SCSI queueing library.
 *      Initial versions: Eric Youngdale (eric@andante.org).
 *                        Based upon conversations with large numbers
 *                        of people at Linux Expo.
 */

/*
 * The fundamental purpose of this file is to contain a library of utility
 * routines that can be used by low-level drivers.   Ultimately the idea
 * is that there should be a sufficiently rich number of functions that it
 * would be possible for a driver author to fashion a queueing function for
 * a low-level driver if they wished.   Note however that this file also
 * contains the "default" versions of these functions, as we don't want to
 * go through and retrofit queueing functions into all 30 some-odd drivers.
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>


#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include "hosts.h"
#include "constants.h"
#include <scsi/scsi_ioctl.h>

/*
 * This entire source file deals with the new queueing code.
 */

/*
 * Function:    scsi_insert_special_cmd()
 *
 * Purpose:     Insert pre-formed command into request queue.
 *
 * Arguments:   SCpnt   - command that is ready to be queued.
 *              at_head - boolean.  True if we should insert at head
 *                        of queue, false if we should insert at tail.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       This function is called from character device and from
 *              ioctl types of functions where the caller knows exactly
 *              what SCSI command needs to be issued.   The idea is that
 *              we merely inject the command into the queue (at the head
 *              for now), and then call the queue request function to actually
 *              process it.
 */
int scsi_insert_special_cmd(Scsi_Cmnd * SCpnt, int at_head)
{
	unsigned long flags;
	request_queue_t *q;

	ASSERT_LOCK(&io_request_lock, 0);

	/*
	 * The SCpnt already contains a request structure - we will doctor the
	 * thing up with the appropriate values and use that in the actual
	 * request queue.
	 */
	q = &SCpnt->device->request_queue;
	SCpnt->request.cmd = SPECIAL;
	SCpnt->request.special = (void *) SCpnt;

	/*
	 * For the moment, we insert at the head of the queue.   This may turn
	 * out to be a bad idea, but we will see about that when we get there.
	 */
	spin_lock_irqsave(&io_request_lock, flags);

	if (at_head) {
		SCpnt->request.next = q->current_request;
		q->current_request = &SCpnt->request;
	} else {
		/*
		 * FIXME(eric) - we always insert at the tail of the list.  Otherwise
		 * ioctl commands would always take precedence over normal I/O.
		 */
		SCpnt->request.next = NULL;
		if (q->current_request == NULL) {
			q->current_request = &SCpnt->request;
		} else {
			struct request *req;

			for (req = q->current_request; req; req = req->next) {
				if (req->next == NULL) {
					req->next = &SCpnt->request;
					break;
				}
			}
		}
	}

	/*
	 * Now hit the requeue function for the queue.   If the host is already
	 * busy, so be it - we have nothing special to do.   If the host can queue
	 * it, then send it off.
	 */
	q->request_fn(q);
	spin_unlock_irqrestore(&io_request_lock, flags);
	return 0;
}

/*
 * Function:    scsi_init_cmd_errh()
 *
 * Purpose:     Initialize SCpnt fields related to error handling.
 *
 * Arguments:   SCpnt   - command that is ready to be queued.
 *
 * Returns:     Nothing
 *
 * Notes:       This function has the job of initializing a number of
 *              fields related to error handling.   Typically this will
 *              be called once for each command, as required.
 */
int scsi_init_cmd_errh(Scsi_Cmnd * SCpnt)
{
	ASSERT_LOCK(&io_request_lock, 0);

	SCpnt->owner = SCSI_OWNER_MIDLEVEL;
	SCpnt->reset_chain = NULL;
	SCpnt->serial_number = 0;
	SCpnt->serial_number_at_timeout = 0;
	SCpnt->flags = 0;
	SCpnt->retries = 0;

	SCpnt->abort_reason = 0;

	memset((void *) SCpnt->sense_buffer, 0, sizeof SCpnt->sense_buffer);

	if (SCpnt->cmd_len == 0)
		SCpnt->cmd_len = COMMAND_SIZE(SCpnt->cmnd[0]);

	/*
	 * We need saved copies of a number of fields - this is because
	 * error handling may need to overwrite these with different values
	 * to run different commands, and once error handling is complete,
	 * we will need to restore these values prior to running the actual
	 * command.
	 */
	SCpnt->old_use_sg = SCpnt->use_sg;
	SCpnt->old_cmd_len = SCpnt->cmd_len;
	memcpy((void *) SCpnt->data_cmnd,
	       (const void *) SCpnt->cmnd, sizeof(SCpnt->cmnd));
	SCpnt->buffer = SCpnt->request_buffer;
	SCpnt->bufflen = SCpnt->request_bufflen;

	SCpnt->reset_chain = NULL;

	SCpnt->internal_timeout = NORMAL_TIMEOUT;
	SCpnt->abort_reason = 0;

	return 1;
}

/*
 * Function:    scsi_queue_next_request()
 *
 * Purpose:     Handle post-processing of completed commands.
 *
 * Arguments:   SCpnt   - command that may need to be requeued.
 *
 * Returns:     Nothing
 *
 * Notes:       After command completion, there may be blocks left
 *              over which weren't finished by the previous command
 *              this can be for a number of reasons - the main one is
 *              that a medium error occurred, and the sectors after
 *              the bad block need to be re-read.
 *
 *              If SCpnt is NULL, it means that the previous command
 *              was completely finished, and we should simply start
 *              a new command, if possible.
 */
void scsi_queue_next_request(request_queue_t * q, Scsi_Cmnd * SCpnt)
{
	int all_clear;
	unsigned long flags;
	Scsi_Device *SDpnt;
	struct Scsi_Host *SHpnt;

	ASSERT_LOCK(&io_request_lock, 0);

	spin_lock_irqsave(&io_request_lock, flags);
	if (SCpnt != NULL) {

		/*
		 * For some reason, we are not done with this request.
		 * This happens for I/O errors in the middle of the request,
		 * in which case we need to request the blocks that come after
		 * the bad sector.
		 */
		SCpnt->request.next = q->current_request;
		q->current_request = &SCpnt->request;
		SCpnt->request.special = (void *) SCpnt;
	}
	/*
	 * Just hit the requeue function for the queue.
	 * FIXME - if this queue is empty, check to see if we might need to
	 * start requests for other devices attached to the same host.
	 */
	q->request_fn(q);

	/*
	 * Now see whether there are other devices on the bus which
	 * might be starved.  If so, hit the request function.  If we
	 * don't find any, then it is safe to reset the flag.  If we
	 * find any device that it is starved, it isn't safe to reset the
	 * flag as the queue function releases the lock and thus some
	 * other device might have become starved along the way.
	 */
	SDpnt = (Scsi_Device *) q->queuedata;
	SHpnt = SDpnt->host;
	all_clear = 1;
	if (SHpnt->some_device_starved) {
		for (SDpnt = SHpnt->host_queue; SDpnt; SDpnt = SDpnt->next) {
			request_queue_t *q;
			if ((SHpnt->can_queue > 0 && (SHpnt->host_busy >= SHpnt->can_queue))
			    || (SHpnt->host_blocked)) {
				break;
			}
			if (SDpnt->device_blocked || !SDpnt->starved) {
				continue;
			}
			q = &SDpnt->request_queue;
			q->request_fn(q);
			all_clear = 0;
		}
		if (SDpnt == NULL && all_clear) {
			SHpnt->some_device_starved = 0;
		}
	}
	spin_unlock_irqrestore(&io_request_lock, flags);
}

/*
 * Function:    scsi_end_request()
 *
 * Purpose:     Post-processing of completed commands called from interrupt
 *              handler.
 *
 * Arguments:   SCpnt    - command that is complete.
 *              uptodate - 1 if I/O indicates success, 0 for I/O error.
 *              sectors  - number of sectors we want to mark.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       This is called for block device requests in order to
 *              mark some number of sectors as complete.
 */
Scsi_Cmnd *scsi_end_request(Scsi_Cmnd * SCpnt, int uptodate, int sectors)
{
	struct request *req;
	struct buffer_head *bh;

	ASSERT_LOCK(&io_request_lock, 0);

	req = &SCpnt->request;
	req->errors = 0;
	if (!uptodate) {
		printk(" I/O error: dev %s, sector %lu\n",
		       kdevname(req->rq_dev), req->sector);
	}
	do {
		if ((bh = req->bh) != NULL) {
			req->bh = bh->b_reqnext;
			req->nr_sectors -= bh->b_size >> 9;
			req->sector += bh->b_size >> 9;
			bh->b_reqnext = NULL;
			sectors -= bh->b_size >> 9;
			bh->b_end_io(bh, uptodate);
			if ((bh = req->bh) != NULL) {
				req->current_nr_sectors = bh->b_size >> 9;
				if (req->nr_sectors < req->current_nr_sectors) {
					req->nr_sectors = req->current_nr_sectors;
					printk("scsi_end_request: buffer-list destroyed\n");
				}
			}
		}
	} while (sectors && bh);

	/*
	 * If there are blocks left over at the end, set up the command
	 * to queue the remainder of them.
	 */
	if (req->bh) {
		req->buffer = bh->b_data;
		return SCpnt;
	}
	/*
	 * This request is done.  If there is someone blocked waiting for this
	 * request, wake them up.  Typically used to wake up processes trying
	 * to swap a page into memory.
	 */
	if (req->sem != NULL) {
		up(req->sem);
	}
	add_blkdev_randomness(MAJOR(req->rq_dev));
	scsi_release_command(SCpnt);
	return NULL;
}

/*
 * Function:    scsi_io_completion()
 *
 * Purpose:     Completion processing for block device I/O requests.
 *
 * Arguments:   SCpnt   - command that is finished.
 *
 * Lock status: Assumed that no lock is held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       This function is matched in terms of capabilities to
 *              the function that created the scatter-gather list.
 *              In other words, if there are no bounce buffers
 *              (the normal case for most drivers), we don't need
 *              the logic to deal with cleaning up afterwards.
 */
void scsi_io_completion(Scsi_Cmnd * SCpnt, int good_sectors,
			int block_sectors)
{
	int result = SCpnt->result;
	int this_count = SCpnt->bufflen >> 9;
	request_queue_t *q = &SCpnt->device->request_queue;

	ASSERT_LOCK(&io_request_lock, 0);

	/*
	 * Free up any indirection buffers we allocated for DMA purposes. 
	 * For the case of a READ, we need to copy the data out of the
	 * bounce buffer and into the real buffer.
	 */
	if (SCpnt->use_sg) {
		struct scatterlist *sgpnt;
		int i;

		sgpnt = (struct scatterlist *) SCpnt->buffer;

		for (i = 0; i < SCpnt->use_sg; i++) {
			if (sgpnt[i].alt_address) {
				if (SCpnt->request.cmd == READ) {
					memcpy(sgpnt[i].alt_address, 
					       sgpnt[i].address,
					       sgpnt[i].length);
				}
				scsi_free(sgpnt[i].address, sgpnt[i].length);
			}
		}
		scsi_free(SCpnt->buffer, SCpnt->sglist_len);
	} else {
		if (SCpnt->buffer != SCpnt->request.buffer) {
			if (SCpnt->request.cmd == READ) {
				memcpy(SCpnt->request.buffer, SCpnt->buffer,
				       SCpnt->bufflen);
			}
			scsi_free(SCpnt->buffer, SCpnt->bufflen);
		}
	}

	/*
	 * Zero these out.  They now point to freed memory, and it is
	 * dangerous to hang onto the pointers.
	 */
	SCpnt->buffer  = NULL;
	SCpnt->bufflen = 0;
	SCpnt->request_buffer = NULL;
	SCpnt->request_bufflen = 0;

	/*
	 * Next deal with any sectors which we were able to correctly
	 * handle.
	 */
	if (good_sectors > 0) {
		SCSI_LOG_HLCOMPLETE(1, printk("%ld sectors total, %d sectors done.\n",
					      SCpnt->request.nr_sectors,
					      good_sectors));
		SCSI_LOG_HLCOMPLETE(1, printk("use_sg is %d\n ", SCpnt->use_sg));

		SCpnt->request.errors = 0;
		/*
		 * If multiple sectors are requested in one buffer, then
		 * they will have been finished off by the first command.
		 * If not, then we have a multi-buffer command.
		 */
		SCpnt = scsi_end_request(SCpnt, 1, good_sectors);

		/*
		 * If the command completed without error, then either finish off the
		 * rest of the command, or start a new one.
		 */
		if (result == 0) {
			scsi_queue_next_request(q, SCpnt);
			return;
		}
	}
	/*
	 * Now, if we were good little boys and girls, Santa left us a request
	 * sense buffer.  We can extract information from this, so we
	 * can choose a block to remap, etc.
	 */
	if (driver_byte(result) != 0) {
		if (suggestion(result) == SUGGEST_REMAP) {
#ifdef REMAP
			/*
			 * Not yet implemented.  A read will fail after being remapped,
			 * a write will call the strategy routine again.
			 */
			if (SCpnt->device->remap) {
				result = 0;
			}
#endif
		}
		if ((SCpnt->sense_buffer[0] & 0x7f) == 0x70
		    && (SCpnt->sense_buffer[2] & 0xf) == UNIT_ATTENTION) {
			if (SCpnt->device->removable) {
				/* detected disc change.  set a bit and quietly refuse
				 * further access.
				 */
				SCpnt->device->changed = 1;
				SCpnt = scsi_end_request(SCpnt, 0, this_count);
				scsi_queue_next_request(q, SCpnt);
				return;
			} else {
				/*
				 * Must have been a power glitch, or a bus reset.
				 * Could not have been a media change, so we just retry
				 * the request and see what happens.
				 */
				scsi_queue_next_request(q, SCpnt);
				return;
			}
		}
		/* If we had an ILLEGAL REQUEST returned, then we may have
		 * performed an unsupported command.  The only thing this should be
		 * would be a ten byte read where only a six byte read was supported.
		 * Also, on a system where READ CAPACITY failed, we have have read
		 * past the end of the disk.
		 */

		switch (SCpnt->sense_buffer[2]) {
		case ILLEGAL_REQUEST:
			if (SCpnt->device->ten) {
				SCpnt->device->ten = 0;
				scsi_queue_next_request(q, SCpnt);
				result = 0;
			} else {
				SCpnt = scsi_end_request(SCpnt, 0, this_count);
				scsi_queue_next_request(q, SCpnt);
				return;
			}
			break;
		case NOT_READY:
			printk(KERN_INFO "Device %x not ready.\n",
			       SCpnt->request.rq_dev);
			SCpnt = scsi_end_request(SCpnt, 0, this_count);
			scsi_queue_next_request(q, SCpnt);
			return;
			break;
		case MEDIUM_ERROR:
		case VOLUME_OVERFLOW:
			printk("scsi%d: ERROR on channel %d, id %d, lun %d, CDB: ",
			       SCpnt->host->host_no, (int) SCpnt->channel,
			       (int) SCpnt->target, (int) SCpnt->lun);
			print_command(SCpnt->cmnd);
			print_sense("sd", SCpnt);
			SCpnt = scsi_end_request(SCpnt, 0, block_sectors);
			scsi_queue_next_request(q, SCpnt);
			return;
		default:
			break;
		}
	}			/* driver byte != 0 */
	if (result) {
		printk("SCSI disk error : host %d channel %d id %d lun %d return code = %x\n",
		       SCpnt->device->host->host_no,
		       SCpnt->device->channel,
		       SCpnt->device->id,
		       SCpnt->device->lun, result);

		if (driver_byte(result) & DRIVER_SENSE)
			print_sense("sd", SCpnt);
		SCpnt = scsi_end_request(SCpnt, 0, SCpnt->request.current_nr_sectors);
		scsi_queue_next_request(q, SCpnt);
		return;
	}
}

/*
 * Function:    scsi_get_request_dev()
 *
 * Purpose:     Find the upper-level driver that is responsible for this
 *              request
 *
 * Arguments:   request   - I/O request we are preparing to queue.
 *
 * Lock status: No locks assumed to be held, but as it happens the
 *              io_request_lock is held when this is called.
 *
 * Returns:     Nothing
 *
 * Notes:       The requests in the request queue may have originated
 *              from any block device driver.  We need to find out which
 *              one so that we can later form the appropriate command.
 */
struct Scsi_Device_Template *scsi_get_request_dev(struct request *req)
{
	struct Scsi_Device_Template *spnt;
	kdev_t dev = req->rq_dev;
	int major = MAJOR(dev);

	ASSERT_LOCK(&io_request_lock, 1);

	for (spnt = scsi_devicelist; spnt; spnt = spnt->next) {
		/*
		 * Search for a block device driver that supports this
		 * major.
		 */
		if (spnt->blk && spnt->major == major) {
			return spnt;
		}
	}
	return NULL;
}

/*
 * Function:    scsi_request_fn()
 *
 * Purpose:     Generic version of request function for SCSI hosts.
 *
 * Arguments:   q       - Pointer to actual queue.
 *
 * Returns:     Nothing
 *
 * Lock status: IO request lock assumed to be held when called.
 *
 * Notes:       The theory is that this function is something which individual
 *              drivers could also supply if they wished to.   The problem
 *              is that we have 30 some odd low-level drivers in the kernel
 *              tree already, and it would be most difficult to retrofit
 *              this crap into all of them.   Thus this function has the job
 *              of acting as a generic queue manager for all of those existing
 *              drivers.
 */
void scsi_request_fn(request_queue_t * q)
{
	struct request *req;
	Scsi_Cmnd *SCpnt;
	Scsi_Device *SDpnt;
	struct Scsi_Host *SHpnt;
	struct Scsi_Device_Template *STpnt;

	ASSERT_LOCK(&io_request_lock, 1);

	SDpnt = (Scsi_Device *) q->queuedata;
	if (!SDpnt) {
		panic("Missing device");
	}
	SHpnt = SDpnt->host;

	/*
	 * If the host for this device is in error recovery mode, don't
	 * do anything at all here.  When the host leaves error recovery
	 * mode, it will automatically restart things and start queueing
	 * commands again.  Same goes if the queue is actually plugged,
	 * if the device itself is blocked, or if the host is fully
	 * occupied.
	 */
	if (SHpnt->in_recovery
	    || q->plugged) {
		return;
	}
	/*
	 * To start with, we keep looping until the queue is empty, or until
	 * the host is no longer able to accept any more requests.
	 */
	while (1 == 1) {
		/*
		 * If the host cannot accept another request, then quit.
		 */
		if (SDpnt->device_blocked) {
			break;
		}
		if ((SHpnt->can_queue > 0 && (SHpnt->host_busy >= SHpnt->can_queue))
		    || (SHpnt->host_blocked)) {
			/*
			 * If we are unable to process any commands at all for this
			 * device, then we consider it to be starved.  What this means
			 * is that there are no outstanding commands for this device
			 * and hence we need a little help getting it started again
			 * once the host isn't quite so busy.
			 */
			if (SDpnt->device_busy == 0) {
				SDpnt->starved = 1;
				SHpnt->some_device_starved = 1;
			}
			break;
		} else {
			SDpnt->starved = 0;
		}
		/*
		 * Loop through all of the requests in this queue, and find
		 * one that is queueable.
		 */
		req = q->current_request;

		/*
		 * If we couldn't find a request that could be queued, then we
		 * can also quit.
		 */
		if (!req) {
			break;
		}
		/*
		 * Find the actual device driver associated with this command.
		 * The SPECIAL requests are things like character device or
		 * ioctls, which did not originate from ll_rw_blk.  Note that
		 * the special field is also used to indicate the SCpnt for
		 * the remainder of a partially fulfilled request that can 
		 * come up when there is a medium error.  We have to treat
		 * these two cases differently.  We differentiate by looking
		 * at request.cmd, as this tells us the real story.
		 */
		if (req->cmd == SPECIAL) {
			STpnt = NULL;
			SCpnt = (Scsi_Cmnd *) req->special;
		} else {
			STpnt = scsi_get_request_dev(req);
			if (!STpnt) {
				panic("Unable to find device associated with request");
			}
			/*
			 * Now try and find a command block that we can use.
			 */
			if( req->special != NULL ) {
				SCpnt = (Scsi_Cmnd *) req->special;
				/*
				 * We need to recount the number of
				 * scatter-gather segments here - the
				 * normal case code assumes this to be
				 * correct, as it would be a performance
				 * lose to always recount.  Handling
				 * errors is always unusual, of course.
				 */
				recount_segments(SCpnt);
			} else {
				SCpnt = scsi_allocate_device(SDpnt, FALSE);
			}
			/*
			 * If so, we are ready to do something.  Bump the count
			 * while the queue is locked and then break out of the loop.
			 * Otherwise loop around and try another request.
			 */
			if (!SCpnt) {
				break;
			}
			SHpnt->host_busy++;
			SDpnt->device_busy++;
		}

		/*
		 * FIXME(eric)
		 * I am not sure where the best place to do this is.  We need
		 * to hook in a place where we are likely to come if in user
		 * space.   Technically the error handling thread should be
		 * doing this crap, but the error handler isn't used by
		 * most hosts.
		 */
		if (SDpnt->was_reset) {
			/*
			 * We need to relock the door, but we might
			 * be in an interrupt handler.  Only do this
			 * from user space, since we do not want to
			 * sleep from an interrupt.
			 */
			if (SDpnt->removable && !in_interrupt()) {
				spin_unlock_irq(&io_request_lock);
				scsi_ioctl(SDpnt, SCSI_IOCTL_DOORLOCK, 0);
				SDpnt->was_reset = 0;
				spin_lock_irq(&io_request_lock);
				continue;
			}
			SDpnt->was_reset = 0;
		}
		/*
		 * Finally, before we release the lock, we copy the
		 * request to the command block, and remove the
		 * request from the request list.   Note that we always
		 * operate on the queue head - there is absolutely no
		 * reason to search the list, because all of the commands
		 * in this queue are for the same device.
		 */
		q->current_request = req->next;
		SCpnt->request.next = NULL;

		if (req != &SCpnt->request) {
			memcpy(&SCpnt->request, req, sizeof(struct request));

			/*
			 * We have copied the data out of the request block - it is now in
			 * a field in SCpnt.  Release the request block.
			 */
			req->next = NULL;
			req->rq_status = RQ_INACTIVE;
			wake_up(&wait_for_request);
		}
		/*
		 * Now it is finally safe to release the lock.  We are
		 * not going to noodle the request list until this
		 * request has been queued and we loop back to queue
		 * another.  
		 */
		req = NULL;
		spin_unlock_irq(&io_request_lock);

		if (SCpnt->request.cmd != SPECIAL) {
			/*
			 * This will do a couple of things:
			 *  1) Fill in the actual SCSI command.
			 *  2) Fill in any other upper-level specific fields (timeout).
			 *
			 * If this returns 0, it means that the request failed (reading
			 * past end of disk, reading offline device, etc).   This won't
			 * actually talk to the device, but some kinds of consistency
			 * checking may cause the request to be rejected immediately.
			 */
			if (STpnt == NULL) {
				STpnt = scsi_get_request_dev(req);
			}
			/* 
			 * This sets up the scatter-gather table (allocating if
			 * required).  Hosts that need bounce buffers will also
			 * get those allocated here.  
			 */
			if (!SDpnt->scsi_init_io_fn(SCpnt)) {
				continue;
			}
			/*
			 * Initialize the actual SCSI command for this request.
			 */
			if (!STpnt->init_command(SCpnt)) {
				continue;
			}
		}
		/*
		 * Finally, initialize any error handling parameters, and set up
		 * the timers for timeouts.
		 */
		scsi_init_cmd_errh(SCpnt);

		/*
		 * Dispatch the command to the low-level driver.
		 */
		scsi_dispatch_cmd(SCpnt);

		/*
		 * Now we need to grab the lock again.  We are about to mess with
		 * the request queue and try to find another command.
		 */
		spin_lock_irq(&io_request_lock);
	}

	/*
	 * If this is a single-lun device, and we are currently finished
	 * with this device, then see if we need to get another device
	 * started.
	 */
	if (SDpnt->single_lun
	    && q->current_request == NULL
	    && SDpnt->device_busy == 0) {
		request_queue_t *q;

		for (SDpnt = SHpnt->host_queue;
		     SDpnt;
		     SDpnt = SDpnt->next) {
			if (((SHpnt->can_queue > 0)
			     && (SHpnt->host_busy >= SHpnt->can_queue))
			    || (SHpnt->host_blocked)
			    || (SDpnt->device_blocked)) {
				break;
			}
			q = &SDpnt->request_queue;
			q->request_fn(q);
		}
	}
}
