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

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>


#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include "hosts.h"
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
	request_queue_t *q = &SCpnt->device->request_queue;

	blk_insert_request(q, SCpnt->request, at_head, SCpnt);
	return 0;
}

/*
 * Function:    scsi_insert_special_req()
 *
 * Purpose:     Insert pre-formed request into request queue.
 *
 * Arguments:   SRpnt   - request that is ready to be queued.
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
int scsi_insert_special_req(Scsi_Request * SRpnt, int at_head)
{
	request_queue_t *q = &SRpnt->sr_device->request_queue;

	blk_insert_request(q, SRpnt->sr_request, at_head, SRpnt);
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
	SCpnt->sc_old_data_direction = SCpnt->sc_data_direction;
	SCpnt->old_underflow = SCpnt->underflow;
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
 * Function:   scsi_setup_cmd_retry()
 *
 * Purpose:    Restore the command state for a retry
 *
 * Arguments:  SCpnt   - command to be restored
 *
 * Returns:    Nothing
 *
 * Notes:      Immediately prior to retrying a command, we need
 *             to restore certain fields that we saved above.
 */
void scsi_setup_cmd_retry(Scsi_Cmnd *SCpnt)
{
	memcpy((void *) SCpnt->cmnd, (void *) SCpnt->data_cmnd,
		sizeof(SCpnt->data_cmnd));
	SCpnt->request_buffer = SCpnt->buffer;
	SCpnt->request_bufflen = SCpnt->bufflen;
	SCpnt->use_sg = SCpnt->old_use_sg;
	SCpnt->cmd_len = SCpnt->old_cmd_len;
	SCpnt->sc_data_direction = SCpnt->sc_old_data_direction;
	SCpnt->underflow = SCpnt->old_underflow;
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
 *
 *		This is where a lot of special case code has begun to
 *		accumulate.  It doesn't really affect readability or
 *		anything, but it might be considered architecturally
 *		inelegant.  If more of these special cases start to
 *		accumulate, I am thinking along the lines of implementing
 *		an atexit() like technology that gets run when commands
 *		complete.  I am not convinced that it is worth the
 *		added overhead, however.  Right now as things stand,
 *		there are simple conditional checks, and most hosts
 *		would skip past.
 *
 *		Another possible solution would be to tailor different
 *		handler functions, sort of like what we did in scsi_merge.c.
 *		This is probably a better solution, but the number of different
 *		permutations grows as 2**N, and if too many more special cases
 *		get added, we start to get screwed.
 */
void scsi_queue_next_request(request_queue_t * q, Scsi_Cmnd * SCpnt)
{
	int all_clear;
	unsigned long flags;
	Scsi_Device *SDpnt;
	struct Scsi_Host *SHpnt;

	ASSERT_LOCK(q->queue_lock, 0);

	spin_lock_irqsave(q->queue_lock, flags);
	if (SCpnt != NULL) {

		/*
		 * For some reason, we are not done with this request.
		 * This happens for I/O errors in the middle of the request,
		 * in which case we need to request the blocks that come after
		 * the bad sector.
		 */
		SCpnt->request->special = (void *) SCpnt;
		if(blk_rq_tagged(SCpnt->request))
			blk_queue_end_tag(q, SCpnt->request);
		__elv_add_request(q, SCpnt->request, 0, 0);
	}

	/*
	 * Just hit the requeue function for the queue.
	 */
	q->request_fn(q);

	SDpnt = (Scsi_Device *) q->queuedata;
	SHpnt = SDpnt->host;

	/*
	 * If this is a single-lun device, and we are currently finished
	 * with this device, then see if we need to get another device
	 * started.  FIXME(eric) - if this function gets too cluttered
	 * with special case code, then spin off separate versions and
	 * use function pointers to pick the right one.
	 */
	if (SDpnt->single_lun && blk_queue_empty(q) && SDpnt->device_busy ==0) {
		request_queue_t *q;

		for (SDpnt = SHpnt->host_queue; SDpnt; SDpnt = SDpnt->next) {
			if (((SHpnt->can_queue > 0)
			     && (SHpnt->host_busy >= SHpnt->can_queue))
			    || (SHpnt->host_blocked)
			    || (SHpnt->host_self_blocked)
			    || (SDpnt->device_blocked)) {
				break;
			}

			q = &SDpnt->request_queue;
			q->request_fn(q);
		}
	}

	/*
	 * Now see whether there are other devices on the bus which
	 * might be starved.  If so, hit the request function.  If we
	 * don't find any, then it is safe to reset the flag.  If we
	 * find any device that it is starved, it isn't safe to reset the
	 * flag as the queue function releases the lock and thus some
	 * other device might have become starved along the way.
	 */
	all_clear = 1;
	if (SHpnt->some_device_starved) {
		for (SDpnt = SHpnt->host_queue; SDpnt; SDpnt = SDpnt->next) {
			request_queue_t *q;
			if ((SHpnt->can_queue > 0 && (SHpnt->host_busy >= SHpnt->can_queue))
			    || (SHpnt->host_blocked) 
			    || (SHpnt->host_self_blocked)) {
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
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/*
 * Function:    scsi_end_request()
 *
 * Purpose:     Post-processing of completed commands called from interrupt
 *              handler or a bottom-half handler.
 *
 * Arguments:   SCpnt    - command that is complete.
 *              uptodate - 1 if I/O indicates success, 0 for I/O error.
 *              sectors  - number of sectors we want to mark.
 *		requeue  - indicates whether we should requeue leftovers.
 *		frequeue - indicates that if we release the command block
 *			   that the queue request function should be called.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       This is called for block device requests in order to
 *              mark some number of sectors as complete.
 * 
 *		We are guaranteeing that the request queue will be goosed
 *		at some point during this call.
 */
static Scsi_Cmnd *__scsi_end_request(Scsi_Cmnd * SCpnt, 
				     int uptodate, 
				     int sectors,
				     int requeue,
				     int frequeue)
{
	request_queue_t *q = &SCpnt->device->request_queue;
	struct request *req = SCpnt->request;
	unsigned long flags;

	ASSERT_LOCK(q->queue_lock, 0);

	/*
	 * If there are blocks left over at the end, set up the command
	 * to queue the remainder of them.
	 */
	if (end_that_request_first(req, uptodate, sectors)) {
		if (!requeue)
			return SCpnt;

		/*
		 * Bleah.  Leftovers again.  Stick the leftovers in
		 * the front of the queue, and goose the queue again.
		 */
		scsi_queue_next_request(q, SCpnt);
		return SCpnt;
	}

	add_disk_randomness(req->rq_disk);

	spin_lock_irqsave(q->queue_lock, flags);

	if (blk_rq_tagged(req))
		blk_queue_end_tag(q, req);

	end_that_request_last(req);

	spin_unlock_irqrestore(q->queue_lock, flags);

	/*
	 * This will goose the queue request function at the end, so we don't
	 * need to worry about launching another command.
	 */
	__scsi_release_command(SCpnt);

	if (frequeue)
		scsi_queue_next_request(q, NULL);

	return NULL;
}

/*
 * Function:    scsi_end_request()
 *
 * Purpose:     Post-processing of completed commands called from interrupt
 *              handler or a bottom-half handler.
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
 * 
 *		We are guaranteeing that the request queue will be goosed
 *		at some point during this call.
 */
Scsi_Cmnd *scsi_end_request(Scsi_Cmnd * SCpnt, int uptodate, int sectors)
{
	return __scsi_end_request(SCpnt, uptodate, sectors, 1, 1);
}

/*
 * Function:    scsi_release_buffers()
 *
 * Purpose:     Completion processing for block device I/O requests.
 *
 * Arguments:   SCpnt   - command that we are bailing.
 *
 * Lock status: Assumed that no lock is held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       In the event that an upper level driver rejects a
 *		command, we must release resources allocated during
 *		the __init_io() function.  Primarily this would involve
 *		the scatter-gather table, and potentially any bounce
 *		buffers.
 */
static void scsi_release_buffers(Scsi_Cmnd * SCpnt)
{
	struct request *req = SCpnt->request;

	ASSERT_LOCK(SCpnt->host->host_lock, 0);

	/*
	 * Free up any indirection buffers we allocated for DMA purposes. 
	 */
	if (SCpnt->use_sg) {
		struct scatterlist *sgpnt;

		sgpnt = (struct scatterlist *) SCpnt->request_buffer;
		scsi_free_sgtable(SCpnt->request_buffer, SCpnt->sglist_len);
	} else {
		if (SCpnt->request_buffer != req->buffer)
			kfree(SCpnt->request_buffer);
	}

	/*
	 * Zero these out.  They now point to freed memory, and it is
	 * dangerous to hang onto the pointers.
	 */
	SCpnt->buffer  = NULL;
	SCpnt->bufflen = 0;
	SCpnt->request_buffer = NULL;
	SCpnt->request_bufflen = 0;
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
	struct request *req = SCpnt->request;

	/*
	 * We must do one of several things here:
	 *
	 *	Call scsi_end_request.  This will finish off the specified
	 *	number of sectors.  If we are done, the command block will
	 *	be released, and the queue function will be goosed.  If we
	 *	are not done, then scsi_end_request will directly goose
	 *	the queue.
	 *
	 *	We can just use scsi_queue_next_request() here.  This
	 *	would be used if we just wanted to retry, for example.
	 *
	 */
	ASSERT_LOCK(q->queue_lock, 0);

	/*
	 * Free up any indirection buffers we allocated for DMA purposes. 
	 * For the case of a READ, we need to copy the data out of the
	 * bounce buffer and into the real buffer.
	 */
	if (SCpnt->use_sg) {
		struct scatterlist *sgpnt;

		sgpnt = (struct scatterlist *) SCpnt->buffer;
		scsi_free_sgtable(SCpnt->buffer, SCpnt->sglist_len);
	} else {
		if (SCpnt->buffer != req->buffer) {
			if (rq_data_dir(req) == READ) {
				unsigned long flags;
				char *to = bio_kmap_irq(req->bio, &flags);

				memcpy(to, SCpnt->buffer, SCpnt->bufflen);
				bio_kunmap_irq(to, &flags);
			}
			kfree(SCpnt->buffer);
		}
	}

	if (blk_pc_request(req)) {
		req->errors = result & 0xff;
		if (!result)
			req->data_len -= SCpnt->bufflen;
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
	if (good_sectors >= 0) {
		SCSI_LOG_HLCOMPLETE(1, printk("%ld sectors total, %d sectors done.\n",
					      req->nr_sectors, good_sectors));
		SCSI_LOG_HLCOMPLETE(1, printk("use_sg is %d\n ", SCpnt->use_sg));

		req->errors = 0;
		/*
		 * If multiple sectors are requested in one buffer, then
		 * they will have been finished off by the first command.
		 * If not, then we have a multi-buffer command.
		 *
		 * If block_sectors != 0, it means we had a medium error
		 * of some sort, and that we want to mark some number of
		 * sectors as not uptodate.  Thus we want to inhibit
		 * requeueing right here - we will requeue down below
		 * when we handle the bad sectors.
		 */
		SCpnt = __scsi_end_request(SCpnt, 
					   1, 
					   good_sectors,
					   result == 0,
					   1);

		/*
		 * If the command completed without error, then either finish off the
		 * rest of the command, or start a new one.
		 */
		if (result == 0 || SCpnt == NULL ) {
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
		if ((SCpnt->sense_buffer[0] & 0x7f) == 0x70) {
			/*
			 * If the device is in the process of becoming ready,
			 * retry.
			 */
			if (SCpnt->sense_buffer[12] == 0x04 &&
			    SCpnt->sense_buffer[13] == 0x01) {
				scsi_queue_next_request(q, SCpnt);
				return;
			}
			if ((SCpnt->sense_buffer[2] & 0xf) == UNIT_ATTENTION) {
				if (SCpnt->device->removable) {
					/* detected disc change.  set a bit 
					 * and quietly refuse further access.
		 			 */
					SCpnt->device->changed = 1;
					SCpnt = scsi_end_request(SCpnt, 0, this_count);
					return;
				} else {
					/*
				 	* Must have been a power glitch, or a
				 	* bus reset.  Could not have been a
				 	* media change, so we just retry the
				 	* request and see what happens.  
				 	*/
					scsi_queue_next_request(q, SCpnt);
					return;
				}
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
				/*
				 * This will cause a retry with a 6-byte
				 * command.
				 */
				scsi_queue_next_request(q, SCpnt);
				result = 0;
			} else {
				SCpnt = scsi_end_request(SCpnt, 0, this_count);
				return;
			}
			break;
		case NOT_READY:
			printk(KERN_INFO "Device %s not ready.\n",
			       kdevname(req->rq_dev));
			SCpnt = scsi_end_request(SCpnt, 0, this_count);
			return;
			break;
		case MEDIUM_ERROR:
		case VOLUME_OVERFLOW:
			printk("scsi%d: ERROR on channel %d, id %d, lun %d, CDB: ",
			       SCpnt->host->host_no, (int) SCpnt->channel,
			       (int) SCpnt->target, (int) SCpnt->lun);
			print_command(SCpnt->data_cmnd);
			print_sense("sd", SCpnt);
			SCpnt = scsi_end_request(SCpnt, 0, block_sectors);
			return;
		default:
			break;
		}
	}			/* driver byte != 0 */
	if (host_byte(result) == DID_RESET) {
		/*
		 * Third party bus reset or reset for error
		 * recovery reasons.  Just retry the request
		 * and see what happens.  
		 */
		scsi_queue_next_request(q, SCpnt);
		return;
	}
	if (result) {
		struct Scsi_Device_Template *STpnt;

		STpnt = scsi_get_request_dev(SCpnt->request);
		printk("SCSI %s error : host %d channel %d id %d lun %d return code = %x\n",
		       (STpnt ? STpnt->name : "device"),
		       SCpnt->device->host->host_no,
		       SCpnt->device->channel,
		       SCpnt->device->id,
		       SCpnt->device->lun, result);

		if (driver_byte(result) & DRIVER_SENSE)
			print_sense("sd", SCpnt);
		/*
		 * Mark a single buffer as not uptodate.  Queue the remainder.
		 * We sometimes get this cruft in the event that a medium error
		 * isn't properly reported.
		 */
		SCpnt = scsi_end_request(SCpnt, 0, req->current_nr_sectors);
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
 *              q->queue_lock is held when this is called.
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
	int major = major(dev);

	for (spnt = scsi_devicelist; spnt; spnt = spnt->next) {
		/*
		 * Search for a block device driver that supports this
		 * major.
		 */
		if (spnt->blk && spnt->major == major) {
			return spnt;
		}
		/*
		 * I am still not entirely satisfied with this solution,
		 * but it is good enough for now.  Disks have a number of
		 * major numbers associated with them, the primary
		 * 8, which we test above, and a secondary range of 7
		 * different consecutive major numbers.   If this ever
		 * becomes insufficient, then we could add another function
		 * to the structure, and generalize this completely.
		 */
		if( spnt->min_major != 0 
		    && spnt->max_major != 0
		    && major >= spnt->min_major
		    && major <= spnt->max_major )
		{
			return spnt;
		}
	}
	return NULL;
}

/*
 * Function:    scsi_init_io()
 *
 * Purpose:     SCSI I/O initialize function.
 *
 * Arguments:   SCpnt   - Command descriptor we wish to initialize
 *
 * Returns:     1 on success.
 */
static int scsi_init_io(Scsi_Cmnd *SCpnt)
{
	struct request     *req = SCpnt->request;
	struct scatterlist *sgpnt;
	int count, gfp_mask;

	/*
	 * if this is a rq->data based REQ_BLOCK_PC, setup for a non-sg xfer
	 */
	if ((req->flags & REQ_BLOCK_PC) && !req->bio) {
		SCpnt->request_bufflen = req->data_len;
		SCpnt->request_buffer = req->data;
		req->buffer = req->data;
		SCpnt->use_sg = 0;
		return 1;
	}

	/*
	 * we used to not use scatter-gather for single segment request,
	 * but now we do (it makes highmem I/O easier to support without
	 * kmapping pages)
	 */
	SCpnt->use_sg = req->nr_phys_segments;

	gfp_mask = GFP_NOIO;
	if (in_interrupt()) {
		gfp_mask &= ~__GFP_WAIT;
		gfp_mask |= __GFP_HIGH;
	}

	/*
	 * if sg table allocation fails, requeue request later.
	 */
	sgpnt = scsi_alloc_sgtable(SCpnt, gfp_mask);
	if (unlikely(!sgpnt))
		goto out;

	SCpnt->request_buffer = (char *) sgpnt;
	SCpnt->request_bufflen = req->nr_sectors << 9;
	if (blk_pc_request(req))
		SCpnt->request_bufflen = req->data_len;
	req->buffer = NULL;

	/* 
	 * Next, walk the list, and fill in the addresses and sizes of
	 * each segment.
	 */
	count = blk_rq_map_sg(req->q, req, SCpnt->request_buffer);

	/*
	 * mapped well, send it off
	 */
	if (unlikely(count > SCpnt->use_sg))
		goto incorrect;
	SCpnt->use_sg = count;
	return 1;

incorrect:
	printk(KERN_ERR "Incorrect number of segments after building list\n");
	printk(KERN_ERR "counted %d, received %d\n", count, SCpnt->use_sg);
	printk(KERN_ERR "req nr_sec %lu, cur_nr_sec %u\n", req->nr_sectors,
			req->current_nr_sectors);

	/*
	 * kill it. there should be no leftover blocks in this request
	 */
	SCpnt = scsi_end_request(SCpnt, 0, req->nr_sectors);
	BUG_ON(SCpnt);
out:
	return 0;
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
	Scsi_Request *SRpnt;
	Scsi_Device *SDpnt;
	struct Scsi_Host *SHpnt;
	struct Scsi_Device_Template *STpnt;

	ASSERT_LOCK(q->queue_lock, 1);

	SDpnt = (Scsi_Device *) q->queuedata;
	if (!SDpnt) {
		panic("Missing device");
	}
	SHpnt = SDpnt->host;

	/*
	 * To start with, we keep looping until the queue is empty, or until
	 * the host is no longer able to accept any more requests.
	 */
	while (1 == 1) {
		/*
		 * Check this again - each time we loop through we will have
		 * released the lock and grabbed it again, so each time
		 * we need to check to see if the queue is plugged or not.
		 */
		if (SHpnt->in_recovery || blk_queue_plugged(q))
			return;

		if(SHpnt->host_busy == 0 && SHpnt->host_blocked) {
			/* unblock after host_blocked iterates to zero */
			if(--SHpnt->host_blocked == 0) {
				SCSI_LOG_MLQUEUE(3, printk("scsi%d unblocking host at zero depth\n", SHpnt->host_no));
			} else {
				blk_plug_device(q);
				break;
			}
		}
		if(SDpnt->device_busy == 0 && SDpnt->device_blocked) {
			/* unblock after device_blocked iterates to zero */
			if(--SDpnt->device_blocked == 0) {
				SCSI_LOG_MLQUEUE(3, printk("scsi%d (%d:%d) unblocking device at zero depth\n", SHpnt->host_no, SDpnt->id, SDpnt->lun));
			} else {
				blk_plug_device(q);
				break;
			}
		}
		/*
		 * If the device cannot accept another request, then quit.
		 */
		if (SDpnt->device_blocked) {
			break;
		}
		if ((SHpnt->can_queue > 0 && (SHpnt->host_busy >= SHpnt->can_queue))
		    || (SHpnt->host_blocked) 
		    || (SHpnt->host_self_blocked)) {
			/*
			 * If we are unable to process any commands at all for
			 * this device, then we consider it to be starved.
			 * What this means is that there are no outstanding
			 * commands for this device and hence we need a
			 * little help getting it started again
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
		 * If we couldn't find a request that could be queued, then we
		 * can also quit.
		 */
		if (blk_queue_empty(q))
			break;

		/*
		 * get next queueable request.
		 */
		req = elv_next_request(q);

		/*
		 * Find the actual device driver associated with this command.
		 * The SPECIAL requests are things like character device or
		 * ioctls, which did not originate from ll_rw_blk.  Note that
		 * the special field is also used to indicate the SCpnt for
		 * the remainder of a partially fulfilled request that can 
		 * come up when there is a medium error.  We have to treat
		 * these two cases differently.  We differentiate by looking
		 * at request->cmd, as this tells us the real story.
		 */
		if (req->flags & REQ_SPECIAL) {
			STpnt = NULL;
			SCpnt = (Scsi_Cmnd *) req->special;
			SRpnt = (Scsi_Request *) req->special;

			if( SRpnt->sr_magic == SCSI_REQ_MAGIC ) {
				SCpnt = scsi_allocate_device(SRpnt->sr_device, 
							     FALSE, FALSE);
				if (!SCpnt)
					break;
				scsi_init_cmd_from_req(SCpnt, SRpnt);
			}

		} else if (req->flags & (REQ_CMD | REQ_BLOCK_PC)) {
			SRpnt = NULL;
			STpnt = scsi_get_request_dev(req);
			if (!STpnt) {
				panic("Unable to find device associated with request");
			}
			/*
			 * Now try and find a command block that we can use.
			 */
			if (req->special) {
				SCpnt = (Scsi_Cmnd *) req->special;
			} else {
				SCpnt = scsi_allocate_device(SDpnt, FALSE, FALSE);
			}
			/*
			 * If so, we are ready to do something.  Bump the count
			 * while the queue is locked and then break out of the
			 * loop. Otherwise loop around and try another request.
			 */
			if (!SCpnt)
				break;

			/* pull a tag out of the request if we have one */
			SCpnt->tag = req->tag;
		} else {
			blk_dump_rq_flags(req, "SCSI bad req");
			break;
		}

		/*
		 * Now bump the usage count for both the host and the
		 * device.
		 */
		SHpnt->host_busy++;
		SDpnt->device_busy++;

		/*
		 * Finally, before we release the lock, we copy the
		 * request to the command block, and remove the
		 * request from the request list.   Note that we always
		 * operate on the queue head - there is absolutely no
		 * reason to search the list, because all of the commands
		 * in this queue are for the same device.
		 */
		if(!(blk_queue_tagged(q) && (blk_queue_start_tag(q, req) == 0)))
			blkdev_dequeue_request(req);

		/* note the overloading of req->special.  When the tag
		 * is active it always means SCpnt.  If the tag goes
		 * back for re-queueing, it may be reset */
		req->special = SCpnt;
		SCpnt->request = req;

		/*
		 * Now it is finally safe to release the lock.  We are
		 * not going to noodle the request list until this
		 * request has been queued and we loop back to queue
		 * another.  
		 */
		req = NULL;
		spin_unlock_irq(q->queue_lock);

		if (!(SCpnt->request->flags & REQ_DONTPREP)
		    && (SCpnt->request->flags & (REQ_CMD | REQ_BLOCK_PC))) {
			/*
			 * This will do a couple of things:
			 *  1) Fill in the actual SCSI command.
			 *  2) Fill in any other upper-level specific fields
			 * (timeout).
			 *
			 * If this returns 0, it means that the request failed
			 * (reading past end of disk, reading offline device,
			 * etc).   This won't actually talk to the device, but
			 * some kinds of consistency checking may cause the	
			 * request to be rejected immediately.
			 */
			if (STpnt == NULL)
				STpnt = scsi_get_request_dev(SCpnt->request);

			/* 
			 * This sets up the scatter-gather table (allocating if
			 * required).
			 */
			if (!scsi_init_io(SCpnt)) {
				scsi_mlqueue_insert(SCpnt, SCSI_MLQUEUE_DEVICE_BUSY);
				spin_lock_irq(q->queue_lock);
				break;
			}

			/*
			 * Initialize the actual SCSI command for this request.
			 */
			if (!STpnt->init_command(SCpnt)) {
				scsi_release_buffers(SCpnt);
				SCpnt = __scsi_end_request(SCpnt, 0, 
							   SCpnt->request->nr_sectors, 0, 0);
				if( SCpnt != NULL )
				{
					panic("Should not have leftover blocks\n");
				}
				spin_lock_irq(q->queue_lock);
				SHpnt->host_busy--;
				SDpnt->device_busy--;
				continue;
			}
		}
		SCpnt->request->flags |= REQ_DONTPREP;
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
		 * Now we need to grab the lock again.  We are about to mess
		 * with the request queue and try to find another command.
		 */
		spin_lock_irq(q->queue_lock);
	}
}

/*
 * Function:    scsi_block_requests()
 *
 * Purpose:     Utility function used by low-level drivers to prevent further
 *		commands from being queued to the device.
 *
 * Arguments:   SHpnt       - Host in question
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       There is no timer nor any other means by which the requests
 *		get unblocked other than the low-level driver calling
 *		scsi_unblock_requests().
 */
void scsi_block_requests(struct Scsi_Host * SHpnt)
{
	SHpnt->host_self_blocked = TRUE;
}

/*
 * Function:    scsi_unblock_requests()
 *
 * Purpose:     Utility function used by low-level drivers to allow further
 *		commands from being queued to the device.
 *
 * Arguments:   SHpnt       - Host in question
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       There is no timer nor any other means by which the requests
 *		get unblocked other than the low-level driver calling
 *		scsi_unblock_requests().
 *
 *		This is done as an API function so that changes to the
 *		internals of the scsi mid-layer won't require wholesale
 *		changes to drivers that use this feature.
 */
void scsi_unblock_requests(struct Scsi_Host * SHpnt)
{
	Scsi_Device *SDloop;

	SHpnt->host_self_blocked = FALSE;
	/* Now that we are unblocked, try to start the queues. */
	for (SDloop = SHpnt->host_queue; SDloop; SDloop = SDloop->next)
		scsi_queue_next_request(&SDloop->request_queue, NULL);
}

/*
 * Function:    scsi_report_bus_reset()
 *
 * Purpose:     Utility function used by low-level drivers to report that
 *		they have observed a bus reset on the bus being handled.
 *
 * Arguments:   SHpnt       - Host in question
 *		channel     - channel on which reset was observed.
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       This only needs to be called if the reset is one which
 *		originates from an unknown location.  Resets originated
 *		by the mid-level itself don't need to call this, but there
 *		should be no harm.
 *
 *		The main purpose of this is to make sure that a CHECK_CONDITION
 *		is properly treated.
 */
void scsi_report_bus_reset(struct Scsi_Host * SHpnt, int channel)
{
	Scsi_Device *SDloop;
	for (SDloop = SHpnt->host_queue; SDloop; SDloop = SDloop->next) {
		if (channel == SDloop->channel) {
			SDloop->was_reset = 1;
			SDloop->expecting_cc_ua = 1;
		}
	}
}

/*
 * FIXME(eric) - these are empty stubs for the moment.  I need to re-implement
 * host blocking from scratch. The theory is that hosts that wish to block
 * will register/deregister using these functions instead of the old way
 * of setting the wish_block flag.
 *
 * The details of the implementation remain to be settled, however the
 * stubs are here now so that the actual drivers will properly compile.
 */
void scsi_register_blocked_host(struct Scsi_Host * SHpnt)
{
}

void scsi_deregister_blocked_host(struct Scsi_Host * SHpnt)
{
}
