/*
 *  scsi.c Copyright (C) 1992 Drew Eckhardt
 *         Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *
 *  generic mid-level SCSI driver
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Bug correction thanks go to :
 *      Rik Faith <faith@cs.unc.edu>
 *      Tommy Thorn <tthorn>
 *      Thomas Wuensche <tw@fgb1.fgb.mw.tu-muenchen.de>
 *
 *  Modified by Eric Youngdale eric@andante.org or ericy@gnu.ai.mit.edu to
 *  add scatter-gather, multiple outstanding request, and other
 *  enhancements.
 *
 *  Native multichannel, wide scsi, /proc/scsi and hot plugging
 *  support added by Michael Neuffer <mike@i-connect.net>
 *
 *  Added request_module("scsi_hostadapter") for kerneld:
 *  (Put an "alias scsi_hostadapter your_hostadapter" in /etc/modules.conf)
 *  Bjorn Ekwall  <bj0rn@blox.se>
 *  (changed to kmod)
 *
 *  Major improvements to the timeout, abort, and reset processing,
 *  as well as performance modifications for large queue depths by
 *  Leonard N. Zubkoff <lnz@dandelion.com>
 *
 *  Converted cli() code to spinlocks, Ingo Molnar
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *
 *  out_of_space hacks, D. Gilbert (dpg) 990608
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/unistd.h>
#include <linux/spinlock.h>
#include <linux/kmod.h>
#include <linux/interrupt.h>

#include "scsi.h"
#include "hosts.h"

#include "scsi_priv.h"
#include "scsi_logging.h"


/*
 * Definitions and constants.
 */

#define MIN_RESET_DELAY (2*HZ)

/* Do not call reset on error if we just did a reset within 15 sec. */
#define MIN_RESET_PERIOD (15*HZ)

/*
 * Macro to determine the size of SCSI command. This macro takes vendor
 * unique commands into account. SCSI commands in groups 6 and 7 are
 * vendor unique and we will depend upon the command length being
 * supplied correctly in cmd_len.
 */
#define CDB_SIZE(cmd)	(((((cmd)->cmnd[0] >> 5) & 7) < 6) ? \
				COMMAND_SIZE((cmd)->cmnd[0]) : (cmd)->cmd_len)

/*
 * Data declarations.
 */
unsigned long scsi_pid;
static unsigned long serial_number;

/*
 * Note - the initial logging level can be set here to log events at boot time.
 * After the system is up, you may enable logging via the /proc interface.
 */
unsigned int scsi_logging_level;

const char *const scsi_device_types[MAX_SCSI_DEVICE_CODE] = {
	"Direct-Access    ",
	"Sequential-Access",
	"Printer          ",
	"Processor        ",
	"WORM             ",
	"CD-ROM           ",
	"Scanner          ",
	"Optical Device   ",
	"Medium Changer   ",
	"Communications   ",
	"Unknown          ",
	"Unknown          ",
	"Unknown          ",
	"Enclosure        ",
};

/*
 * Function:    scsi_allocate_request
 *
 * Purpose:     Allocate a request descriptor.
 *
 * Arguments:   device    - device for which we want a request
 *
 * Lock status: No locks assumed to be held.  This function is SMP-safe.
 *
 * Returns:     Pointer to request block.
 *
 * Notes:       With the new queueing code, it becomes important
 *              to track the difference between a command and a
 *              request.  A request is a pending item in the queue that
 *              has not yet reached the top of the queue.
 *
 * XXX(hch):	Need to add a gfp_mask argument.
 */
struct scsi_request *scsi_allocate_request(struct scsi_device *sdev)
{
	const int offset = ALIGN(sizeof(struct scsi_request), 4);
	const int size = offset + sizeof(struct request);
	struct scsi_request *sreq;
  
	sreq = kmalloc(size, GFP_ATOMIC);
	if (likely(sreq != NULL)) {
		memset(sreq, 0, size);
		sreq->sr_request = (struct request *)(((char *)sreq) + offset);
		sreq->sr_device = sdev;
		sreq->sr_host = sdev->host;
		sreq->sr_magic = SCSI_REQ_MAGIC;
		sreq->sr_data_direction = DMA_BIDIRECTIONAL;
	}

	return sreq;
}

void __scsi_release_request(struct scsi_request *sreq)
{
	if (likely(sreq->sr_command != NULL)) {
		struct scsi_cmnd *cmd = sreq->sr_command;

		sreq->sr_command = NULL;
		scsi_next_command(cmd);
	}
}

/*
 * Function:    scsi_release_request
 *
 * Purpose:     Release a request descriptor.
 *
 * Arguments:   sreq    - request to release
 *
 * Lock status: No locks assumed to be held.  This function is SMP-safe.
 */
void scsi_release_request(struct scsi_request *sreq)
{
	__scsi_release_request(sreq);
	kfree(sreq);
}

struct scsi_host_cmd_pool {
	kmem_cache_t	*slab;
	unsigned int	users;
	char		*name;
	unsigned int	slab_flags;
	unsigned int	gfp_mask;
};

static struct scsi_host_cmd_pool scsi_cmd_pool = {
	.name		= "scsi_cmd_cache",
	.slab_flags	= SLAB_HWCACHE_ALIGN,
};

static struct scsi_host_cmd_pool scsi_cmd_dma_pool = {
	.name		= "scsi_cmd_cache(DMA)",
	.slab_flags	= SLAB_HWCACHE_ALIGN|SLAB_CACHE_DMA,
	.gfp_mask	= __GFP_DMA,
};

static DECLARE_MUTEX(host_cmd_pool_mutex);

static struct scsi_cmnd *__scsi_get_command(struct Scsi_Host *shost,
					    int gfp_mask)
{
	struct scsi_cmnd *cmd;

	cmd = kmem_cache_alloc(shost->cmd_pool->slab,
			gfp_mask | shost->cmd_pool->gfp_mask);

	if (unlikely(!cmd)) {
		unsigned long flags;

		spin_lock_irqsave(&shost->free_list_lock, flags);
		if (likely(!list_empty(&shost->free_list))) {
			cmd = list_entry(shost->free_list.next,
					 struct scsi_cmnd, list);
			list_del_init(&cmd->list);
		}
		spin_unlock_irqrestore(&shost->free_list_lock, flags);
	}

	return cmd;
}

/*
 * Function:	scsi_get_command()
 *
 * Purpose:	Allocate and setup a scsi command block
 *
 * Arguments:	dev	- parent scsi device
 *		gfp_mask- allocator flags
 *
 * Returns:	The allocated scsi command structure.
 */
struct scsi_cmnd *scsi_get_command(struct scsi_device *dev, int gfp_mask)
{
	struct scsi_cmnd *cmd = __scsi_get_command(dev->host, gfp_mask);

	if (likely(cmd != NULL)) {
		unsigned long flags;

		memset(cmd, 0, sizeof(*cmd));
		cmd->device = dev;
		cmd->state = SCSI_STATE_UNUSED;
		cmd->owner = SCSI_OWNER_NOBODY;
		init_timer(&cmd->eh_timeout);
		INIT_LIST_HEAD(&cmd->list);
		spin_lock_irqsave(&dev->list_lock, flags);
		list_add_tail(&cmd->list, &dev->cmd_list);
		spin_unlock_irqrestore(&dev->list_lock, flags);
	}

	return cmd;
}				

/*
 * Function:	scsi_put_command()
 *
 * Purpose:	Free a scsi command block
 *
 * Arguments:	cmd	- command block to free
 *
 * Returns:	Nothing.
 *
 * Notes:	The command must not belong to any lists.
 */
void scsi_put_command(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *shost = cmd->device->host;
	unsigned long flags;
	
	/* serious error if the command hasn't come from a device list */
	spin_lock_irqsave(&cmd->device->list_lock, flags);
	BUG_ON(list_empty(&cmd->list));
	list_del_init(&cmd->list);
	spin_unlock(&cmd->device->list_lock);
	/* changing locks here, don't need to restore the irq state */
	spin_lock(&shost->free_list_lock);
	if (unlikely(list_empty(&shost->free_list))) {
		list_add(&cmd->list, &shost->free_list);
		cmd = NULL;
	}
	spin_unlock_irqrestore(&shost->free_list_lock, flags);

	if (likely(cmd != NULL))
		kmem_cache_free(shost->cmd_pool->slab, cmd);
}

/*
 * Function:	scsi_setup_command_freelist()
 *
 * Purpose:	Setup the command freelist for a scsi host.
 *
 * Arguments:	shost	- host to allocate the freelist for.
 *
 * Returns:	Nothing.
 */
int scsi_setup_command_freelist(struct Scsi_Host *shost)
{
	struct scsi_host_cmd_pool *pool;
	struct scsi_cmnd *cmd;

	spin_lock_init(&shost->free_list_lock);
	INIT_LIST_HEAD(&shost->free_list);

	/*
	 * Select a command slab for this host and create it if not
	 * yet existant.
	 */
	down(&host_cmd_pool_mutex);
	pool = (shost->unchecked_isa_dma ? &scsi_cmd_dma_pool : &scsi_cmd_pool);
	if (!pool->users) {
		pool->slab = kmem_cache_create(pool->name,
				sizeof(struct scsi_cmnd), 0,
				pool->slab_flags, NULL, NULL);
		if (!pool->slab)
			goto fail;
	}

	pool->users++;
	shost->cmd_pool = pool;
	up(&host_cmd_pool_mutex);

	/*
	 * Get one backup command for this host.
	 */
	cmd = kmem_cache_alloc(shost->cmd_pool->slab,
			GFP_KERNEL | shost->cmd_pool->gfp_mask);
	if (!cmd)
		goto fail2;
	list_add(&cmd->list, &shost->free_list);		
	return 0;

 fail2:
	if (!--pool->users)
		kmem_cache_destroy(pool->slab);
	return -ENOMEM;
 fail:
	up(&host_cmd_pool_mutex);
	return -ENOMEM;

}

/*
 * Function:	scsi_destroy_command_freelist()
 *
 * Purpose:	Release the command freelist for a scsi host.
 *
 * Arguments:	shost	- host that's freelist is going to be destroyed
 */
void scsi_destroy_command_freelist(struct Scsi_Host *shost)
{
	while (!list_empty(&shost->free_list)) {
		struct scsi_cmnd *cmd;

		cmd = list_entry(shost->free_list.next, struct scsi_cmnd, list);
		list_del_init(&cmd->list);
		kmem_cache_free(shost->cmd_pool->slab, cmd);
	}

	down(&host_cmd_pool_mutex);
	if (!--shost->cmd_pool->users)
		kmem_cache_destroy(shost->cmd_pool->slab);
	up(&host_cmd_pool_mutex);
}

/*
 * Function:    scsi_dispatch_command
 *
 * Purpose:     Dispatch a command to the low-level driver.
 *
 * Arguments:   cmd - command block we are dispatching.
 *
 * Notes:
 */
int scsi_dispatch_cmd(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	unsigned long flags = 0;
	unsigned long timeout;
	int rtn = 1;

	/* Assign a unique nonzero serial_number. */
	/* XXX(hch): this is racy */
	if (++serial_number == 0)
		serial_number = 1;
	cmd->serial_number = serial_number;
	cmd->pid = scsi_pid++;

	/* 
	 * If SCSI-2 or lower, store the LUN value in cmnd.
	 */
	if (cmd->device->scsi_level <= SCSI_2) {
		cmd->cmnd[1] = (cmd->cmnd[1] & 0x1f) |
			       (cmd->device->lun << 5 & 0xe0);
	}

	/*
	 * We will wait MIN_RESET_DELAY clock ticks after the last reset so
	 * we can avoid the drive not being ready.
	 */
	timeout = host->last_reset + MIN_RESET_DELAY;

	if (host->resetting && time_before(jiffies, timeout)) {
		int ticks_remaining = timeout - jiffies;
		/*
		 * NOTE: This may be executed from within an interrupt
		 * handler!  This is bad, but for now, it'll do.  The irq
		 * level of the interrupt handler has been masked out by the
		 * platform dependent interrupt handling code already, so the
		 * sti() here will not cause another call to the SCSI host's
		 * interrupt handler (assuming there is one irq-level per
		 * host).
		 */
		while (--ticks_remaining >= 0)
			mdelay(1 + 999 / HZ);
		host->resetting = 0;
	}

	scsi_add_timer(cmd, cmd->timeout_per_command, scsi_times_out);

	/*
	 * We will use a queued command if possible, otherwise we will
	 * emulate the queuing and calling of completion function ourselves.
	 */
	SCSI_LOG_MLQUEUE(3, printk("scsi_dispatch_cmnd (host = %d, "
				"channel = %d, target = %d, command = %p, "
				"buffer = %p, \nbufflen = %d, done = %p)\n",
				host->host_no, cmd->device->channel,
				cmd->device->id, cmd->cmnd, cmd->buffer,
				cmd->bufflen, cmd->done));

	cmd->state = SCSI_STATE_QUEUED;
	cmd->owner = SCSI_OWNER_LOWLEVEL;

	/*
	 * Before we queue this command, check if the command
	 * length exceeds what the host adapter can handle.
	 */
	if (CDB_SIZE(cmd) > cmd->device->host->max_cmd_len) {
		SCSI_LOG_MLQUEUE(3,
				printk("queuecommand : command too long.\n"));
		cmd->result = (DID_ABORT << 16);

		spin_lock_irqsave(host->host_lock, flags);
		scsi_done(cmd);
		spin_unlock_irqrestore(host->host_lock, flags);
		goto out;
	}

	SCSI_LOG_MLQUEUE(3, printk("queuecommand : routine at %p\n",
				   host->hostt->queuecommand));

	spin_lock_irqsave(host->host_lock, flags);
	rtn = host->hostt->queuecommand(cmd, scsi_done);
	spin_unlock_irqrestore(host->host_lock, flags);
	if (rtn) {
		scsi_queue_insert(cmd,
				(rtn == SCSI_MLQUEUE_DEVICE_BUSY) ?
				 rtn : SCSI_MLQUEUE_HOST_BUSY);
		SCSI_LOG_MLQUEUE(3,
		    printk("queuecommand : request rejected\n"));
	}

 out:
	SCSI_LOG_MLQUEUE(3, printk("leaving scsi_dispatch_cmnd()\n"));
	return rtn;
}

/*
 * Function:    scsi_init_cmd_from_req
 *
 * Purpose:     Queue a SCSI command
 * Purpose:     Initialize a struct scsi_cmnd from a struct scsi_request
 *
 * Arguments:   cmd       - command descriptor.
 *              sreq      - Request from the queue.
 *
 * Lock status: None needed.
 *
 * Returns:     Nothing.
 *
 * Notes:       Mainly transfer data from the request structure to the
 *              command structure.  The request structure is allocated
 *              using the normal memory allocator, and requests can pile
 *              up to more or less any depth.  The command structure represents
 *              a consumable resource, as these are allocated into a pool
 *              when the SCSI subsystem initializes.  The preallocation is
 *              required so that in low-memory situations a disk I/O request
 *              won't cause the memory manager to try and write out a page.
 *              The request structure is generally used by ioctls and character
 *              devices.
 */
void scsi_init_cmd_from_req(struct scsi_cmnd *cmd, struct scsi_request *sreq)
{
	sreq->sr_command = cmd;

	cmd->owner = SCSI_OWNER_MIDLEVEL;
	cmd->cmd_len = sreq->sr_cmd_len;
	cmd->use_sg = sreq->sr_use_sg;

	cmd->request = sreq->sr_request;
	memcpy(cmd->data_cmnd, sreq->sr_cmnd, sizeof(cmd->data_cmnd));
	cmd->serial_number = 0;
	cmd->serial_number_at_timeout = 0;
	cmd->bufflen = sreq->sr_bufflen;
	cmd->buffer = sreq->sr_buffer;
	cmd->flags = 0;
	cmd->retries = 0;
	cmd->allowed = sreq->sr_allowed;
	cmd->done = sreq->sr_done;
	cmd->timeout_per_command = sreq->sr_timeout_per_command;
	cmd->sc_data_direction = sreq->sr_data_direction;
	cmd->sglist_len = sreq->sr_sglist_len;
	cmd->underflow = sreq->sr_underflow;
	cmd->sc_request = sreq;
	memcpy(cmd->cmnd, sreq->sr_cmnd, sizeof(sreq->sr_cmnd));

	/*
	 * Zero the sense buffer.  Some host adapters automatically request
	 * sense on error.  0 is not a valid sense code.
	 */
	memset(cmd->sense_buffer, 0, sizeof(sreq->sr_sense_buffer));
	cmd->request_buffer = sreq->sr_buffer;
	cmd->request_bufflen = sreq->sr_bufflen;
	cmd->old_use_sg = cmd->use_sg;
	if (cmd->cmd_len == 0)
		cmd->cmd_len = COMMAND_SIZE(cmd->cmnd[0]);
	cmd->old_cmd_len = cmd->cmd_len;
	cmd->sc_old_data_direction = cmd->sc_data_direction;
	cmd->old_underflow = cmd->underflow;

	/*
	 * Start the timer ticking.
	 */
	cmd->internal_timeout = NORMAL_TIMEOUT;
	cmd->abort_reason = 0;
	cmd->result = 0;

	SCSI_LOG_MLQUEUE(3, printk("Leaving scsi_init_cmd_from_req()\n"));
}

/*
 * Per-CPU I/O completion queue.
 */
static struct list_head done_q[NR_CPUS] __cacheline_aligned;

/**
 * scsi_done - Enqueue the finished SCSI command into the done queue.
 * @cmd: The SCSI Command for which a low-level device driver (LLDD) gives
 * ownership back to SCSI Core -- i.e. the LLDD has finished with it.
 *
 * This function is the mid-level's (SCSI Core) interrupt routine, which
 * regains ownership of the SCSI command (de facto) from a LLDD, and enqueues
 * the command to the done queue for further processing.
 *
 * This is the producer of the done queue who enqueues at the tail.
 *
 * This function is interrupt context safe.
 */
void scsi_done(struct scsi_cmnd *cmd)
{
	unsigned long flags;
	int cpu;

	/*
	 * We don't have to worry about this one timing out any more.
	 * If we are unable to remove the timer, then the command
	 * has already timed out.  In which case, we have no choice but to
	 * let the timeout function run, as we have no idea where in fact
	 * that function could really be.  It might be on another processor,
	 * etc, etc.
	 */
	if (!scsi_delete_timer(cmd))
		return;

	/*
	 * Set the serial numbers back to zero
	 */
	cmd->serial_number = 0;
	cmd->serial_number_at_timeout = 0;
	cmd->state = SCSI_STATE_BHQUEUE;
	cmd->owner = SCSI_OWNER_BH_HANDLER;

	/*
	 * Next, enqueue the command into the done queue.
	 * It is a per-CPU queue, so we just disable local interrupts
	 * and need no spinlock.
	 */
	local_irq_save(flags);
	cpu = smp_processor_id();
	list_add_tail(&cmd->eh_entry, &done_q[cpu]);
	raise_softirq_irqoff(SCSI_SOFTIRQ);
	local_irq_restore(flags);
}

/**
 * scsi_softirq - Perform post-interrupt processing of finished SCSI commands.
 *
 * This is the consumer of the done queue.
 *
 * This is called with all interrupts enabled.  This should reduce
 * interrupt latency, stack depth, and reentrancy of the low-level
 * drivers.
 */
static void scsi_softirq(struct softirq_action *h)
{
	LIST_HEAD(local_q);

	local_irq_disable();
	list_splice_init(&done_q[smp_processor_id()], &local_q);
	local_irq_enable();

	while (!list_empty(&local_q)) {
		struct scsi_cmnd *cmd = list_entry(local_q.next,
						   struct scsi_cmnd, eh_entry);
		list_del_init(&cmd->eh_entry);

		switch (scsi_decide_disposition(cmd)) {
		case SUCCESS:
			/*
			 * Add to BH queue.
			 */
			SCSI_LOG_MLCOMPLETE(3,
					    printk("Command finished %d %d "
						   "0x%x\n",
					   cmd->device->host->host_busy,
					   cmd->device->host->host_failed,
						   cmd->result));

			scsi_finish_command(cmd);
			break;
		case NEEDS_RETRY:
			/*
			 * We only come in here if we want to retry a
			 * command.  The test to see whether the
			 * command should be retried should be keeping
			 * track of the number of tries, so we don't
			 * end up looping, of course.
			 */
			SCSI_LOG_MLCOMPLETE(3, printk("Command needs retry "
						      "%d %d 0x%x\n",
					      cmd->device->host->host_busy,
					      cmd->device->host->host_failed,
						      cmd->result));

			scsi_retry_command(cmd);
			break;
		case ADD_TO_MLQUEUE:
			/* 
			 * This typically happens for a QUEUE_FULL
			 * message - typically only when the queue
			 * depth is only approximate for a given
			 * device.  Adding a command to the queue for
			 * the device will prevent further commands
			 * from being sent to the device, so we
			 * shouldn't end up with tons of things being
			 * sent down that shouldn't be.
			 */
			SCSI_LOG_MLCOMPLETE(3, printk("Command rejected as "
						      "device queue full, "
						      "put on ml queue %p\n",
						      cmd));
			scsi_queue_insert(cmd, SCSI_MLQUEUE_DEVICE_BUSY);
			break;
		default:
			/*
			 * Here we have a fatal error of some sort.
			 * Turn it over to the error handler.
			 */
			SCSI_LOG_MLCOMPLETE(3,
					    printk("Command failed %p %x "
						   "busy=%d failed=%d\n",
						   cmd, cmd->result,
					   cmd->device->host->host_busy,
					   cmd->device->host->host_failed));

			/*
			 * Dump the sense information too.
			 */
			if (status_byte(cmd->result) & CHECK_CONDITION)
				SCSI_LOG_MLCOMPLETE(3, print_sense("bh", cmd));

			/*
			 * We only fail here if the error recovery thread
			 * has died.
			 */
			if (!scsi_eh_scmd_add(cmd, 0))
				scsi_finish_command(cmd);
		}
	}
}

/*
 * Function:    scsi_retry_command
 *
 * Purpose:     Send a command back to the low level to be retried.
 *
 * Notes:       This command is always executed in the context of the
 *              bottom half handler, or the error handler thread. Low
 *              level drivers should not become re-entrant as a result of
 *              this.
 */
int scsi_retry_command(struct scsi_cmnd *cmd)
{
	/*
	 * Restore the SCSI command state.
	 */
	scsi_setup_cmd_retry(cmd);

        /*
         * Zero the sense information from the last time we tried
         * this command.
         */
	memset(cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));

	return scsi_dispatch_cmd(cmd);
}

/*
 * Function:    scsi_finish_command
 *
 * Purpose:     Pass command off to upper layer for finishing of I/O
 *              request, waking processes that are waiting on results,
 *              etc.
 */
void scsi_finish_command(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct Scsi_Host *shost = sdev->host;
	struct scsi_request *sreq;

	scsi_device_unbusy(sdev);

        /*
         * Clear the flags which say that the device/host is no longer
         * capable of accepting new commands.  These are set in scsi_queue.c
         * for both the queue full condition on a device, and for a
         * host full condition on the host.
	 *
	 * XXX(hch): What about locking?
         */
        shost->host_blocked = 0;
        sdev->device_blocked = 0;

	/*
	 * If we have valid sense information, then some kind of recovery
	 * must have taken place.  Make a note of this.
	 */
	if (SCSI_SENSE_VALID(cmd))
		cmd->result |= (DRIVER_SENSE << 24);

	SCSI_LOG_MLCOMPLETE(3, printk("Notifying upper driver of completion "
				"for device %d %x\n", sdev->id, cmd->result));

	cmd->owner = SCSI_OWNER_HIGHLEVEL;
	cmd->state = SCSI_STATE_FINISHED;

	/*
	 * We can get here with use_sg=0, causing a panic in the upper level
	 */
	cmd->use_sg = cmd->old_use_sg;

	/*
	 * If there is an associated request structure, copy the data over
	 * before we call the completion function.
	 */
	sreq = cmd->sc_request;
	if (sreq) {
	       sreq->sr_result = sreq->sr_command->result;
	       if (sreq->sr_result) {
		       memcpy(sreq->sr_sense_buffer,
			      sreq->sr_command->sense_buffer,
			      sizeof(sreq->sr_sense_buffer));
	       }
	}

	cmd->done(cmd);
}

/*
 * Function:	scsi_adjust_queue_depth()
 *
 * Purpose:	Allow low level drivers to tell us to change the queue depth
 * 		on a specific SCSI device
 *
 * Arguments:	sdev	- SCSI Device in question
 * 		tagged	- Do we use tagged queueing (non-0) or do we treat
 * 			  this device as an untagged device (0)
 * 		tags	- Number of tags allowed if tagged queueing enabled,
 * 			  or number of commands the low level driver can
 * 			  queue up in non-tagged mode (as per cmd_per_lun).
 *
 * Returns:	Nothing
 *
 * Lock Status:	None held on entry
 *
 * Notes:	Low level drivers may call this at any time and we will do
 * 		the right thing depending on whether or not the device is
 * 		currently active and whether or not it even has the
 * 		command blocks built yet.
 *
 * XXX(hch):	What exactly is device_request_lock trying to protect?
 */
void scsi_adjust_queue_depth(struct scsi_device *sdev, int tagged, int tags)
{
	static spinlock_t device_request_lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;

	/*
	 * refuse to set tagged depth to an unworkable size
	 */
	if (tags <= 0)
		return;
	/*
	 * Limit max queue depth on a single lun to 256 for now.  Remember,
	 * we allocate a struct scsi_command for each of these and keep it
	 * around forever.  Too deep of a depth just wastes memory.
	 */
	if (tags > 256)
		return;

	spin_lock_irqsave(&device_request_lock, flags);
	sdev->queue_depth = tags;
	switch (tagged) {
		case MSG_ORDERED_TAG:
			sdev->ordered_tags = 1;
			sdev->simple_tags = 1;
			break;
		case MSG_SIMPLE_TAG:
			sdev->ordered_tags = 0;
			sdev->simple_tags = 1;
			break;
		default:
			printk(KERN_WARNING "(scsi%d:%d:%d:%d) "
				"scsi_adjust_queue_depth, bad queue type, "
				"disabled\n", sdev->host->host_no,
				sdev->channel, sdev->id, sdev->lun); 
		case 0:
			sdev->ordered_tags = sdev->simple_tags = 0;
			sdev->queue_depth = tags;
			break;
	}
	spin_unlock_irqrestore(&device_request_lock, flags);
}

/*
 * Function:	scsi_track_queue_full()
 *
 * Purpose:	This function will track successive QUEUE_FULL events on a
 * 		specific SCSI device to determine if and when there is a
 * 		need to adjust the queue depth on the device.
 *
 * Arguments:	sdev	- SCSI Device in question
 * 		depth	- Current number of outstanding SCSI commands on
 * 			  this device, not counting the one returned as
 * 			  QUEUE_FULL.
 *
 * Returns:	0 - No change needed
 * 		>0 - Adjust queue depth to this new depth
 * 		-1 - Drop back to untagged operation using host->cmd_per_lun
 * 			as the untagged command depth
 *
 * Lock Status:	None held on entry
 *
 * Notes:	Low level drivers may call this at any time and we will do
 * 		"The Right Thing."  We are interrupt context safe.
 */
int scsi_track_queue_full(struct scsi_device *sdev, int depth)
{
	if ((jiffies >> 4) == sdev->last_queue_full_time)
		return 0;

	sdev->last_queue_full_time = (jiffies >> 4);
	if (sdev->last_queue_full_depth != depth) {
		sdev->last_queue_full_count = 1;
		sdev->last_queue_full_depth = depth;
	} else {
		sdev->last_queue_full_count++;
	}

	if (sdev->last_queue_full_count <= 10)
		return 0;
	if (sdev->last_queue_full_depth < 8) {
		/* Drop back to untagged */
		scsi_adjust_queue_depth(sdev, 0, sdev->host->cmd_per_lun);
		return -1;
	}
	
	if (sdev->ordered_tags)
		scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, depth);
	else
		scsi_adjust_queue_depth(sdev, MSG_SIMPLE_TAG, depth);
	return depth;
}

int scsi_device_get(struct scsi_device *sdev)
{
	if (!try_module_get(sdev->host->hostt->module))
		return -ENXIO;

	sdev->access_count++;
	return 0;
}

void scsi_device_put(struct scsi_device *sdev)
{
	sdev->access_count--;
	module_put(sdev->host->hostt->module);
}

/**
 * scsi_set_device_offline - set scsi_device offline
 * @sdev:	pointer to struct scsi_device to offline. 
 *
 * Locks:	host_lock held on entry.
 **/
void scsi_set_device_offline(struct scsi_device *sdev)
{
	struct scsi_cmnd *scmd;
	LIST_HEAD(active_list);
	struct list_head *lh, *lh_sf;
	unsigned long flags;

	sdev->online = 0;

	spin_lock_irqsave(&sdev->list_lock, flags);
	list_for_each_entry(scmd, &sdev->cmd_list, list) {
		if (scmd->request && scmd->request->rq_status != RQ_INACTIVE) {
			/*
			 * If we are unable to remove the timer, it means
			 * that the command has already timed out or
			 * finished.
			 */
			if (!scsi_delete_timer(scmd))
				continue;
			list_add_tail(&scmd->eh_entry, &active_list);
		}
	}
	spin_unlock_irqrestore(&sdev->list_lock, flags);

	if (!list_empty(&active_list)) {
		list_for_each_safe(lh, lh_sf, &active_list) {
			scmd = list_entry(lh, struct scsi_cmnd, eh_entry);
			scsi_eh_scmd_add(scmd, SCSI_EH_CANCEL_CMD);
		}
	} else {
		/* FIXME: Send online state change hotplug event */
	}
}

MODULE_DESCRIPTION("SCSI core");
MODULE_LICENSE("GPL");

module_param(scsi_logging_level, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(scsi_logging_level, "a bit mask of logging levels");

static int __init init_scsi(void)
{
	int error, i;

	error = scsi_init_queue();
	if (error)
		return error;
	error = scsi_init_procfs();
	if (error)
		goto cleanup_queue;
	error = scsi_init_devinfo();
	if (error)
		goto cleanup_procfs;
	error = scsi_sysfs_register();
	if (error)
		goto cleanup_devlist;

	for (i = 0; i < NR_CPUS; i++)
		INIT_LIST_HEAD(&done_q[i]);

	devfs_mk_dir("scsi");
	open_softirq(SCSI_SOFTIRQ, scsi_softirq, NULL);
	printk(KERN_NOTICE "SCSI subsystem initialized\n");
	return 0;

cleanup_devlist:
	scsi_exit_devinfo();
cleanup_procfs:
	scsi_exit_procfs();
cleanup_queue:
	scsi_exit_queue();
	printk(KERN_ERR "SCSI subsystem failed to initialize, error = %d\n",
	       -error);
	return error;
}

static void __exit exit_scsi(void)
{
	scsi_sysfs_unregister();
	scsi_exit_devinfo();
	devfs_remove("scsi");
	scsi_exit_procfs();
	scsi_exit_queue();
}

subsys_initcall(init_scsi);
module_exit(exit_scsi);
