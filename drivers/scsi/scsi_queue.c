/*
 *  scsi_queue.c Copyright (C) 1997 Eric Youngdale
 *
 *  generic mid-level SCSI queueing.
 *
 *  The point of this is that we need to track when hosts are unable to
 *  accept a command because they are busy.  In addition, we track devices
 *  that cannot accept a command because of a QUEUE_FULL condition.  In both
 *  of these cases, we enter the command in the queue.  At some later point,
 *  we attempt to remove commands from the queue and retry them.
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

/*
 * TODO:
 *	1) Prevent multiple traversals of list to look for commands to
 *	   queue.
 *	2) Protect against multiple insertions of list at the same time.
 * DONE:
 *	1) Set state of scsi command to a new state value for ml queue.
 *	2) Insert into queue when host rejects command.
 *	3) Make sure status code is properly passed from low-level queue func
 *	   so that internal_cmnd properly returns the right value.
 *	4) Insert into queue when QUEUE_FULL.
 *	5) Cull queue in bottom half handler.
 *	6) Check usage count prior to queue insertion.  Requeue if usage
 *	   count is 0.
 *	7) Don't send down any more commands if the host/device is busy.
 */

static const char RCSid[] = "$Header: /mnt/ide/home/eric/CVSROOT/linux/drivers/scsi/scsi_queue.c,v 1.1 1997/10/21 11:16:38 eric Exp $";

/*
 * Lock used to prevent more than one process from frobbing the list at the
 * same time.  FIXME(eric) - there should be separate spinlocks for each host.
 * This will reduce contention.
 */
spinlock_t	scsi_mlqueue_lock        = SPIN_LOCK_UNLOCKED;
spinlock_t	scsi_mlqueue_remove_lock = SPIN_LOCK_UNLOCKED;

/*
 * Function:    scsi_mlqueue_insert()
 *
 * Purpose:     Insert a command in the midlevel queue.
 *
 * Arguments:   cmd    - command that we are adding to queue.
 *		reason - why we are inserting command to queue.
 *
 * Returns:     Nothing.
 *
 * Notes:	We do this for one of two cases.  Either the host is busy
 *		and it cannot accept any more commands for the time being,
 *		or the device returned QUEUE_FULL and can accept no more
 *		commands.
 * Notes:	This could be called either from an interrupt context or a
 *		normal process context.
 */
int
scsi_mlqueue_insert(Scsi_Cmnd * cmd, int reason)
{
    Scsi_Cmnd        * cpnt;
    unsigned long      flags;
    struct Scsi_Host * host;

    SCSI_LOG_MLQUEUE(1,printk("Inserting command %p into mlqueue\n", cmd));

    /*
     * We are inserting the command into the ml queue.  First, we
     * cancel the timer, so it doesn't time out.
     */
    scsi_delete_timer(cmd);

    host = cmd->host;

    /*
     * Next, set the appropriate busy bit for the device/host.
     */
    if( reason == SCSI_MLQUEUE_HOST_BUSY )
    {
	/*
	 * Protect against race conditions.  If the host isn't busy,
	 * assume that something actually completed, and that we should
	 * be able to queue a command now.  Note that there is an implicit
	 * assumption that every host can always queue at least one command.
	 * If a host is inactive and cannot queue any commands, I don't see
	 * how things could possibly work anyways.
	 */
	if( host->host_busy == 0 )
	{
	    if( scsi_retry_command(cmd) == 0 )
	    {
		return 0;
	    }
	}

	host->host_blocked   = TRUE;
	cmd->host_wait       = TRUE;
    }
    else
    {
	/*
	 * Protect against race conditions.  If the device isn't busy,
	 * assume that something actually completed, and that we should
	 * be able to queue a command now.  Note that there is an implicit
	 * assumption that every host can always queue at least one command.
	 * If a host is inactive and cannot queue any commands, I don't see
	 * how things could possibly work anyways.
	 */
	if( cmd->device->device_busy == 0 )
	{
	    if( scsi_retry_command(cmd) == 0 )
	    {
		return 0;
	    }
	}

	cmd->device->device_busy = TRUE;
	cmd->device_wait         = TRUE;
    }

    /*
     * Register the fact that we own the thing for now.
     */
    cmd->state = SCSI_STATE_MLQUEUE;
    cmd->owner = SCSI_OWNER_MIDLEVEL;
    cmd->bh_next = NULL;

    /*
     * As a performance enhancement, look to see whether the list is
     * empty.  If it is, then we can just atomicly insert the command
     * in the list and return without locking.
     */
    if( host->pending_commands == NULL )
    {
	cpnt = xchg(&host->pending_commands, cmd);
	if( cpnt == NULL )
	{
	    return 0;
	}
	/*
	 * Rats.  Something slipped in while we were exchanging.
	 * Swap it back and fall through to do it the hard way.
	 */
	cmd = xchg(&host->pending_commands, cpnt);

    }

    /*
     * Next append the command to the list of pending commands.
     */
    spin_lock_irqsave(&scsi_mlqueue_lock, flags);
    for(cpnt = host->pending_commands; cpnt && cpnt->bh_next; 
	cpnt = cpnt->bh_next)
    {
	continue;
    }
    if( cpnt != NULL )
    {
	cpnt->bh_next = cmd;
    }
    else
    {
	host->pending_commands = cmd;
    }

    spin_unlock_irqrestore(&scsi_mlqueue_lock, flags);
    return 0;
}

/*
 * Function:    scsi_mlqueue_finish()
 *
 * Purpose:     Try and queue commands from the midlevel queue.
 *
 * Arguments:   host    - host that just finished a command.
 *		device  - device that just finished a command.
 *
 * Returns:     Nothing.
 *
 * Notes:	This could be called either from an interrupt context or a
 *		normal process context.
 */
int
scsi_mlqueue_finish(struct Scsi_Host * host, Scsi_Device * device)
{
    Scsi_Cmnd      * cpnt;
    unsigned long    flags;
    Scsi_Cmnd      * next;
    Scsi_Cmnd      * prev;
    int              reason = 0;
    int              rtn;

    SCSI_LOG_MLQUEUE(2,printk("scsi_mlqueue_finish starting\n"));
    /*
     * First, clear the flag for the host/device.  We will then start
     * pushing commands through until either something else blocks, or
     * the queue is empty.
     */
    if( host->host_blocked )
    {
	reason = SCSI_MLQUEUE_HOST_BUSY;
	host->host_blocked = FALSE;
    }

    if( device->device_busy )
    {
	reason = SCSI_MLQUEUE_DEVICE_BUSY;
	device->device_busy = FALSE;
    }

    /*
     * Walk the list of commands to see if there is anything we can
     * queue.  This probably needs to be optimized for performance at
     * some point.
     */
    prev = NULL;
    spin_lock_irqsave(&scsi_mlqueue_remove_lock, flags);
    for(cpnt = host->pending_commands; cpnt; cpnt = next)
    {
	next = cpnt->bh_next;
	/*
	 * First, see if this command is suitable for being retried now.
	 */
	if( reason == SCSI_MLQUEUE_HOST_BUSY )
	{
	    /*
	     * The host was busy, but isn't any more.  Thus we may be
	     * able to queue the command now, but we were waiting for
	     * the device, then we should keep waiting.  Similarily, if
	     * the device is now busy, we should also keep waiting.
	     */
	    if(    (cpnt->host_wait == FALSE)
		|| (device->device_busy == TRUE) )
	    {
		prev = cpnt;
		continue;
	    }
	}

	if( reason == SCSI_MLQUEUE_DEVICE_BUSY )
	{
	    /*
	     * The device was busy, but isn't any more.  Thus we may be
	     * able to queue the command now, but we were waiting for
	     * the host, then we should keep waiting.  Similarily, if
	     * the host is now busy, we should also keep waiting.
	     */
	    if(    (cpnt->device_wait == FALSE)
		|| (host->host_blocked == TRUE) )
	    {
		prev = cpnt;
		continue;
	    }
	}

	/*
	 * First, remove the command from the list.
	 */
	if( prev == NULL )
	{
	    host->pending_commands = next;
	}
	else
	{
	    prev->bh_next = next;
	}
	cpnt->bh_next = NULL;

	rtn = scsi_retry_command(cpnt);

	/*
	 * If we got a non-zero return value, it means that the host rejected
	 * the command.  The internal_cmnd function will have added the
	 * command back to the end of the list, so we don't have anything
	 * more to do here except return.
	 */
	if( rtn )
	{
	    spin_unlock_irqrestore(&scsi_mlqueue_remove_lock, flags);
	    SCSI_LOG_MLQUEUE(1,printk("Unable to remove command %p from mlqueue\n", cpnt));
	    goto finish;
	}
	SCSI_LOG_MLQUEUE(1,printk("Removed command %p from mlqueue\n", cpnt));
    }

    spin_unlock_irqrestore(&scsi_mlqueue_remove_lock, flags);
finish:
    SCSI_LOG_MLQUEUE(2,printk("scsi_mlqueue_finish returning\n"));
    return 0;
}
