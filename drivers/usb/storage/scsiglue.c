/* Driver for USB Mass Storage compliant devices
 * SCSI layer glue code
 *
 * $Id: scsiglue.c,v 1.26 2002/04/22 03:39:43 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *   (c) 2000 Stephen J. Gowdy (SGowdy@lbl.gov)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "scsiglue.h"
#include "usb.h"
#include "debug.h"
#include "transport.h"

#include <linux/slab.h>
#include <linux/module.h>
#include <scsi/scsi_devinfo.h>


/***********************************************************************
 * Host functions 
 ***********************************************************************/

static const char* host_info(struct Scsi_Host *host)
{
	return "SCSI emulation for USB Mass Storage devices";
}

static int slave_configure (struct scsi_device *sdev)
{
	/* this is to satisify the compiler, tho I don't think the 
	 * return code is ever checked anywhere. */
	return 0;
}

/* queue a command */
/* This is always called with scsi_lock(srb->host) held */
static int queuecommand( Scsi_Cmnd *srb , void (*done)(Scsi_Cmnd *))
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];

	US_DEBUGP("%s called\n", __FUNCTION__);
	srb->host_scribble = (unsigned char *)us;

	/* enqueue the command */
	if (us->sm_state != US_STATE_IDLE || us->srb != NULL) {
		printk(KERN_ERR USB_STORAGE "Error in %s: " 
			"state = %d, us->srb = %p\n",
			__FUNCTION__, us->sm_state, us->srb);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	srb->scsi_done = done;
	us->srb = srb;

	/* wake up the process task */
	up(&(us->sema));

	return 0;
}

/***********************************************************************
 * Error handling functions
 ***********************************************************************/

/* Command abort */
/* This is always called with scsi_lock(srb->host) held */
static int command_abort( Scsi_Cmnd *srb )
{
	struct Scsi_Host *host = srb->device->host;
	struct us_data *us = (struct us_data *) host->hostdata[0];

	US_DEBUGP("%s called\n", __FUNCTION__);

	/* Is this command still active? */
	if (us->srb != srb) {
		US_DEBUGP ("-- nothing to abort\n");
		return FAILED;
	}

	/* Normally the current state is RUNNING.  If the control thread
	 * hasn't even started processing this command, the state will be
	 * IDLE.  Anything else is a bug. */
	if (us->sm_state != US_STATE_RUNNING
				&& us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* Set state to ABORTING, set the ABORTING bit, and release the lock */
	us->sm_state = US_STATE_ABORTING;
	set_bit(US_FLIDX_ABORTING, &us->flags);
	scsi_unlock(host);

	/* Stop an ongoing USB transfer */
	usb_stor_stop_transport(us);

	/* Wait for the aborted command to finish */
	wait_for_completion(&us->notify);

	/* Reacquire the lock and allow USB transfers to resume */
	scsi_lock(host);
	clear_bit(US_FLIDX_ABORTING, &us->flags);
	return SUCCESS;
}

/* This invokes the transport reset mechanism to reset the state of the
 * device */
/* This is always called with scsi_lock(srb->host) held */
static int device_reset( Scsi_Cmnd *srb )
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];
	int result;

	US_DEBUGP("%s called\n", __FUNCTION__);
	if (us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* set the state and release the lock */
	us->sm_state = US_STATE_RESETTING;
	scsi_unlock(srb->device->host);

	/* lock the device pointers and do the reset */
	down(&(us->dev_semaphore));
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		result = FAILED;
		US_DEBUGP("No reset during disconnect\n");
	} else
		result = us->transport_reset(us);
	up(&(us->dev_semaphore));

	/* lock access to the state and clear it */
	scsi_lock(srb->device->host);
	us->sm_state = US_STATE_IDLE;
	return result;
}

/* This resets the device's USB port. */
/* It refuses to work if there's more than one interface in
 * the device, so that other users are not affected. */
/* This is always called with scsi_lock(srb->host) held */
static int bus_reset( Scsi_Cmnd *srb )
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];
	int result;

	US_DEBUGP("%s called\n", __FUNCTION__);
	if (us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* set the state and release the lock */
	us->sm_state = US_STATE_RESETTING;
	scsi_unlock(srb->device->host);

	/* The USB subsystem doesn't handle synchronisation between
	 * a device's several drivers. Therefore we reset only devices
	 * with just one interface, which we of course own. */

	down(&(us->dev_semaphore));
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		result = -EIO;
		US_DEBUGP("No reset during disconnect\n");
	} else if (us->pusb_dev->actconfig->desc.bNumInterfaces != 1) {
		result = -EBUSY;
		US_DEBUGP("Refusing to reset a multi-interface device\n");
	} else {
		result = usb_reset_device(us->pusb_dev);
		US_DEBUGP("usb_reset_device returns %d\n", result);
	}
	up(&(us->dev_semaphore));

	/* lock access to the state and clear it */
	scsi_lock(srb->device->host);
	us->sm_state = US_STATE_IDLE;
	return result < 0 ? FAILED : SUCCESS;
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

/* we use this macro to help us write into the buffer */
#undef SPRINTF
#define SPRINTF(args...) \
	do { if (pos < buffer+length) pos += sprintf(pos, ## args); } while (0)

static int proc_info (struct Scsi_Host *hostptr, char *buffer, char **start, off_t offset,
		int length, int inout)
{
	struct us_data *us;
	char *pos = buffer;
	unsigned long f;

	/* if someone is sending us data, just throw it away */
	if (inout)
		return length;

	us = (struct us_data*)hostptr->hostdata[0];

	/* print the controller name */
	SPRINTF("   Host scsi%d: usb-storage\n", hostptr->host_no);

	/* print product, vendor, and serial number strings */
	SPRINTF("       Vendor: %s\n", us->vendor);
	SPRINTF("      Product: %s\n", us->product);
	SPRINTF("Serial Number: %s\n", us->serial);

	/* show the protocol and transport */
	SPRINTF("     Protocol: %s\n", us->protocol_name);
	SPRINTF("    Transport: %s\n", us->transport_name);

	/* show the device flags */
	if (pos < buffer + length) {
		pos += sprintf(pos, "       Quirks:");
		f = us->flags;

#define DO_FLAG(a)  	if (f & US_FL_##a)  pos += sprintf(pos, " " #a)
		DO_FLAG(SINGLE_LUN);
		DO_FLAG(SCM_MULT_TARG);
		DO_FLAG(FIX_INQUIRY);
		DO_FLAG(FIX_CAPACITY);
#undef DO_FLAG

		*(pos++) = '\n';
		}

	/*
	 * Calculate start of next buffer, and return value.
	 */
	*start = buffer + offset;

	if ((pos - buffer) < offset)
		return (0);
	else if ((pos - buffer - offset) < length)
		return (pos - buffer - offset);
	else
		return (length);
}

/*
 * this defines our host template, with which we'll allocate hosts
 */

struct scsi_host_template usb_stor_host_template = {
	/* basic userland interface stuff */
	.name =				"usb-storage",
	.proc_name =			"usb-storage",
	.proc_info =			proc_info,
	.info =				host_info,

	/* command interface -- queued only */
	.queuecommand =			queuecommand,

	/* error and abort handlers */
	.eh_abort_handler =		command_abort,
	.eh_device_reset_handler =	device_reset,
	.eh_bus_reset_handler =		bus_reset,

	/* queue commands only, only one command per LUN */
	.can_queue =			1,
	.cmd_per_lun =			1,

	/* unknown initiator id */
	.this_id =			-1,

	.slave_configure =		slave_configure,

	/* lots of sg segments can be handled */
	.sg_tablesize =			SG_ALL,

	/* limit the total size of a transfer to 120 KB */
	.max_sectors =                  240,

	/* merge commands... this seems to help performance, but
	 * periodically someone should test to see which setting is more
	 * optimal.
	 */
	.use_clustering =		TRUE,

	/* emulated HBA */
	.emulated =			TRUE,

	/* modify scsi_device bits on probe */
	.flags = (BLIST_MS_SKIP_PAGE_08 | BLIST_MS_SKIP_PAGE_3F |
		  BLIST_USE_10_BYTE_MS),

	/* module management */
	.module =			THIS_MODULE
};

/* For a device that is "Not Ready" */
unsigned char usb_stor_sense_notready[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= 0x02,			    /* not ready */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x04,			    /* not ready */
	[13]	= 0x03			    /* manual intervention */
};

/* To Report "Illegal Request: Invalid Field in CDB */
unsigned char usb_stor_sense_invalidCDB[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= ILLEGAL_REQUEST,	    /* Illegal Request = 0x05 */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x24			    /* Invalid Field in CDB */
};

