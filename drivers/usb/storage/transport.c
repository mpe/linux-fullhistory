/* Driver for USB Mass Storage compliant devices
 *
 * $Id: transport.c,v 1.2 2000/06/27 10:20:39 mdharm Exp $
 *
 * Current development and maintainance by:
 *   (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
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
#include "transport.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/malloc.h>

/***********************************************************************
 * Data transfer routines
 ***********************************************************************/

/* This is the completion handler which will wake us up when an URB
 * completes.
 */
static void usb_stor_blocking_completion(urb_t *urb)
{
	api_wrapper_data *awd = (api_wrapper_data *)urb->context;

	if (waitqueue_active(awd->wakeup))
		wake_up(awd->wakeup);
}

/* This is our function to emulate usb_control_msg() but give us enough
 * access to make aborts/resets work
 */
int usb_stor_control_msg(struct us_data *us, unsigned int pipe,
			 u8 request, u8 requesttype, u16 value, u16 index, 
			 void *data, u16 size)
{
	DECLARE_WAITQUEUE(wait, current);
	DECLARE_WAIT_QUEUE_HEAD(wqh);
	api_wrapper_data awd;
	int status;
	devrequest *dr;

	/* allocate the device request structure */
	dr = kmalloc(sizeof(devrequest), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	/* fill in the structure */
	dr->requesttype = requesttype;
	dr->request = request;
	dr->value = cpu_to_le16(value);
	dr->index = cpu_to_le16(index);
	dr->length = cpu_to_le16(size);

	/* set up data structures for the wakeup system */
	awd.wakeup = &wqh;
	awd.handler = 0;
	init_waitqueue_head(&wqh); 	
	add_wait_queue(&wqh, &wait);

	/* lock the URB */
	down(&(us->current_urb_sem));

	/* fill the URB */
	FILL_CONTROL_URB(us->current_urb, us->pusb_dev, pipe, 
			 (unsigned char*) dr, data, size, 
			 usb_stor_blocking_completion, &awd);

	/* submit the URB */
	set_current_state(TASK_UNINTERRUPTIBLE);
	status = usb_submit_urb(us->current_urb);
	if (status) {
		/* something went wrong */
		up(&(us->current_urb_sem));
		remove_wait_queue(&wqh, &wait);
		kfree(dr);
		return status;
	}

	/* wait for the completion of the URB */
	up(&(us->current_urb_sem));
	if (us->current_urb->status == -EINPROGRESS)
		schedule();
	down(&(us->current_urb_sem));

	/* we either timed out or got woken up -- clean up either way */
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&wqh, &wait);

	/* return the actual length of the data transferred if no error*/
	status = us->current_urb->status;
	if (status >= 0)
		status = us->current_urb->actual_length;

	/* release the lock and return status */
	up(&(us->current_urb_sem));
	kfree(dr);
  	return status;
}

/* This is our function to emulate usb_bulk_msg() but give us enough
 * access to make aborts/resets work
 */
int usb_stor_bulk_msg(struct us_data *us, void *data, int pipe,
		      unsigned int len, unsigned int *act_len)
{
	DECLARE_WAITQUEUE(wait, current);
	DECLARE_WAIT_QUEUE_HEAD(wqh);
	api_wrapper_data awd;
	int status;

	/* set up data structures for the wakeup system */
	awd.wakeup = &wqh;
	awd.handler = 0;
	init_waitqueue_head(&wqh); 	
	add_wait_queue(&wqh, &wait);

	/* lock the URB */
	down(&(us->current_urb_sem));

	/* fill the URB */
	FILL_BULK_URB(us->current_urb, us->pusb_dev, pipe, data, len,
		      usb_stor_blocking_completion, &awd);

	/* submit the URB */
	set_current_state(TASK_UNINTERRUPTIBLE);
	status = usb_submit_urb(us->current_urb);
	if (status) {
		/* something went wrong */
		up(&(us->current_urb_sem));
		remove_wait_queue(&wqh, &wait);
		return status;
	}

	/* wait for the completion of the URB */
	up(&(us->current_urb_sem));
	if (us->current_urb->status == -EINPROGRESS)
		schedule();
	down(&(us->current_urb_sem));

	/* we either timed out or got woken up -- clean up either way */
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&wqh, &wait);

	/* return the actual length of the data transferred */
	*act_len = us->current_urb->actual_length;

	/* release the lock and return status */
	up(&(us->current_urb_sem));
	return us->current_urb->status;
}

/*
 * Transfer one SCSI scatter-gather buffer via bulk transfer
 *
 * Note that this function is necessary because we want the ability to
 * use scatter-gather memory.  Good performance is achieved by a combination
 * of scatter-gather and clustering (which makes each chunk bigger).
 *
 * Note that the lower layer will always retry when a NAK occurs, up to the
 * timeout limit.  Thus we don't have to worry about it for individual
 * packets.
 */
static int us_transfer_partial(struct us_data *us, char *buf, int length)
{
	int result;
	int partial;
	int pipe;

	/* calculate the appropriate pipe information */
	if (US_DIRECTION(us->srb->cmnd[0]))
		pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	else
		pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);

	/* transfer the data */
	US_DEBUGP("us_transfer_partial(): xfer %d bytes\n", length);
	result = usb_stor_bulk_msg(us, buf, pipe, length, &partial);
	US_DEBUGP("usb_stor_bulk_msg() returned %d xferred %d/%d\n",
		  result, partial, length);

	/* if we stall, we need to clear it before we go on */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);
	}
	
	/* did we send all the data? */
	if (partial == length) {
		US_DEBUGP("us_transfer_partial(): transfer complete\n");
		return US_BULK_TRANSFER_GOOD;
	}

	/* uh oh... we have an error code, so something went wrong. */
	if (result) {
		/* NAK - that means we've retried a few times allready */
		if (result == -ETIMEDOUT) {
			US_DEBUGP("us_transfer_partial(): device NAKed\n");
			return US_BULK_TRANSFER_FAILED;
		}

		/* -ENOENT -- we canceled this transfer */
		if (result == -ENOENT) {
			US_DEBUGP("us_transfer_partial(): transfer aborted\n");
			return US_BULK_TRANSFER_ABORTED;
		}

		/* the catch-all case */
		US_DEBUGP("us_transfer_partial(): unknown error\n");
		return US_BULK_TRANSFER_FAILED;
	}

	/* no error code, so we must have transferred some data, 
	 * just not all of it */
	return US_BULK_TRANSFER_SHORT;
}

/*
 * Transfer an entire SCSI command's worth of data payload over the bulk
 * pipe.
 *
 * Note that this uses us_transfer_partial to achieve it's goals -- this
 * function simply determines if we're going to use scatter-gather or not,
 * and acts appropriately.  For now, it also re-interprets the error codes.
 */
static void us_transfer(Scsi_Cmnd *srb, struct us_data* us, int dir_in)
{
	int i;
	int result = -1;
	struct scatterlist *sg;

	/* are we scatter-gathering? */
	if (srb->use_sg) {

		/* loop over all the scatter gather structures and 
		 * make the appropriate requests for each, until done
		 */
		sg = (struct scatterlist *) srb->request_buffer;
		for (i = 0; i < srb->use_sg; i++) {
			result = us_transfer_partial(us, sg[i].address, 
						     sg[i].length);
			if (result)
				break;
		}
	}
	else
		/* no scatter-gather, just make the request */
		result = us_transfer_partial(us, srb->request_buffer, 
					     srb->request_bufflen);

	/* return the result in the data structure itself */
	srb->result = result;
}

/* Calculate the length of the data transfer (not the command) for any
 * given SCSI command
 */
static unsigned int us_transfer_length(Scsi_Cmnd *srb, struct us_data *us)
{
	int i;
	unsigned int total = 0;
	struct scatterlist *sg;

	/* support those devices which need the length calculated
	 * differently 
	 */
	if (us->flags & US_FL_ALT_LENGTH) {
		if (srb->cmnd[0] == INQUIRY) {
			srb->cmnd[4] = 36;
		}

		if ((srb->cmnd[0] == INQUIRY) || (srb->cmnd[0] == MODE_SENSE))
			return srb->cmnd[4];

		if (srb->cmnd[0] == TEST_UNIT_READY)
			return 0;
	}

	/* Are we going to scatter gather? */
	if (srb->use_sg) {
		/* Add up the sizes of all the scatter-gather segments */
		sg = (struct scatterlist *) srb->request_buffer;
		for (i = 0; i < srb->use_sg; i++)
			total += sg[i].length;

		return total;
	}
	else
		/* Just return the length of the buffer */
		return srb->request_bufflen;
}

/***********************************************************************
 * Transport routines
 ***********************************************************************/

/* Invoke the transport and basic error-handling/recovery methods
 *
 * This is used by the protocol layers to actually send the message to
 * the device and recieve the response.
 */
void usb_stor_invoke_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int need_auto_sense;
	int result;

	/* send the command to the transport layer */
	result = us->transport(srb, us);

	/* if the command gets aborted by the higher layers, we need to
	 * short-circuit all other processing
	 */
	if (result == USB_STOR_TRANSPORT_ABORTED) {
		US_DEBUGP("-- transport indicates command was aborted\n");
		srb->result = DID_ABORT << 16;
		return;
	}

	/* Determine if we need to auto-sense
	 *
	 * I normally don't use a flag like this, but it's almost impossible
	 * to understand what's going on here if I don't.
	 */
	need_auto_sense = 0;

	/*
	 * If we're running the CB transport, which is incapable
	 * of determining status on it's own, we need to auto-sense almost
	 * every time.
	 */
	if (us->protocol == US_PR_CB) {
		US_DEBUGP("-- CB transport device requiring auto-sense\n");
		need_auto_sense = 1;

		/* There are some exceptions to this.  Notably, if this is
		 * a UFI device and the command is REQUEST_SENSE or INQUIRY,
		 * then it is impossible to truly determine status.
		 */
		if (us->subclass == US_SC_UFI &&
		    ((srb->cmnd[0] == REQUEST_SENSE) ||
		     (srb->cmnd[0] == INQUIRY))) {
			US_DEBUGP("** no auto-sense for a special command\n");
			need_auto_sense = 0;
		}
	}

	/*
	 * If we have an error, we're going to do a REQUEST_SENSE 
	 * automatically.  Note that we differentiate between a command
	 * "failure" and an "error" in the transport mechanism.
	 */
	if (result == USB_STOR_TRANSPORT_FAILED) {
		US_DEBUGP("-- transport indicates command failure\n");
		need_auto_sense = 1;
	}
	if (result == USB_STOR_TRANSPORT_ERROR) {
		/* FIXME: we need to invoke a transport reset here */
		US_DEBUGP("-- transport indicates transport failure\n");
		need_auto_sense = 0;
		srb->result = DID_ERROR << 16;
		return;
	}

	/*
	 * Also, if we have a short transfer on a command that can't have
	 * a short transfer, we're going to do this.
	 */
	if ((srb->result == US_BULK_TRANSFER_SHORT) &&
	    !((srb->cmnd[0] == REQUEST_SENSE) ||
	      (srb->cmnd[0] == INQUIRY) ||
	      (srb->cmnd[0] == MODE_SENSE) ||
	      (srb->cmnd[0] == LOG_SENSE) ||
	      (srb->cmnd[0] == MODE_SENSE_10))) {
		US_DEBUGP("-- unexpectedly short transfer\n");
		need_auto_sense = 1;
	}

	/* Now, if we need to do the auto-sense, let's do it */
	if (need_auto_sense) {
		int temp_result;
		void* old_request_buffer;
		int old_sg;
		int old_request_bufflen;
		unsigned char old_cmnd[MAX_COMMAND_SIZE];

		US_DEBUGP("Issuing auto-REQUEST_SENSE\n");

		/* save the old command */
		memcpy(old_cmnd, srb->cmnd, MAX_COMMAND_SIZE);

		/* set the command and the LUN */
		srb->cmnd[0] = REQUEST_SENSE;
		srb->cmnd[1] = old_cmnd[1] & 0xE0;
		srb->cmnd[2] = 0;
		srb->cmnd[3] = 0;
		srb->cmnd[4] = 18;
		srb->cmnd[5] = 0;

		/* set the buffer length for transfer */
		old_request_buffer = srb->request_buffer;
		old_request_bufflen = srb->request_bufflen;
		old_sg = srb->use_sg;
		srb->use_sg = 0;
		srb->request_bufflen = 18;
		srb->request_buffer = srb->sense_buffer;

		/* issue the auto-sense command */
		temp_result = us->transport(us->srb, us);
		if (temp_result != USB_STOR_TRANSPORT_GOOD) {
			/* FIXME: we need to invoke a transport reset here */
			US_DEBUGP("-- auto-sense failure\n");
			srb->result = DID_ERROR << 16;
			return;
		}

		US_DEBUGP("-- Result from auto-sense is %d\n", temp_result);
		US_DEBUGP("-- code: 0x%x, key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  srb->sense_buffer[0],
			  srb->sense_buffer[2] & 0xf,
			  srb->sense_buffer[12], 
			  srb->sense_buffer[13]);

		/* set the result so the higher layers expect this data */
		srb->result = CHECK_CONDITION;

		/* we're done here, let's clean up */
		srb->request_buffer = old_request_buffer;
		srb->request_bufflen = old_request_bufflen;
		srb->use_sg = old_sg;
		memcpy(srb->cmnd, old_cmnd, MAX_COMMAND_SIZE);

		/* If things are really okay, then let's show that */
		if ((srb->sense_buffer[2] & 0xf) == 0x0)
			srb->result = GOOD;
	} else /* if (need_auto_sense) */
		srb->result = GOOD;

	/* Regardless of auto-sense, if we _know_ we have an error
	 * condition, show that in the result code
	 */
	if (result == USB_STOR_TRANSPORT_FAILED)
		srb->result = CHECK_CONDITION;

	/* If we think we're good, then make sure the sense data shows it.
	 * This is necessary because the auto-sense for some devices always
	 * sets byte 0 == 0x70, even if there is no error
	 */
	if ((us->protocol == US_PR_CB) && 
	    (result == USB_STOR_TRANSPORT_GOOD) &&
	    ((srb->sense_buffer[2] & 0xf) == 0x0))
		srb->sense_buffer[0] = 0x0;
}

/*
 * Control/Bulk/Interrupt transport
 */

/* The interrupt handler for CBI devices */
void usb_stor_CBI_irq(struct urb *urb)
{
	struct us_data *us = (struct us_data *)urb->context;

	US_DEBUGP("USB IRQ recieved for device on host %d\n", us->host_no);
	US_DEBUGP("-- IRQ data length is %d\n", urb->actual_length);
	US_DEBUGP("-- IRQ state is %d\n", urb->status);

	/* is the device removed? */
	if (urb->status != -ENOENT) {
		/* save the data for interpretation later */
		US_DEBUGP("-- Interrupt Status (0x%x, 0x%x)\n",
			  ((unsigned char*)urb->transfer_buffer)[0], 
			  ((unsigned char*)urb->transfer_buffer)[1]);


		/* was this a wanted interrupt? */
		if (us->ip_wanted) {
			us->ip_wanted = 0;
			up(&(us->ip_waitq));
		} else
			US_DEBUGP("ERROR: Unwanted interrupt received!\n");
	} else
		US_DEBUGP("-- device has been removed\n");
}

int usb_stor_CBI_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_stor_control_msg(us, usb_sndctrlpipe(us->pusb_dev,0),
				      US_CBI_ADSC, 
				      USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, 
				      us->ifnum, srb->cmnd, srb->cmd_len);

	/* check the return code for the command */
	US_DEBUGP("Call to usb_stor_control_msg() returned %d\n", result);
	if (result < 0) {
		/* if the command was aborted, indicate that */
		if (result == -ENOENT)
			return USB_STOR_TRANSPORT_ABORTED;

		/* STALL must be cleared when they are detected */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe. Clearing\n");
			result = usb_clear_halt(us->pusb_dev,	
						usb_sndctrlpipe(us->pusb_dev,
								0));
			US_DEBUGP("-- usb_clear_halt() returns %d\n", result);
			return USB_STOR_TRANSPORT_FAILED;
		}

		/* Uh oh... serious problem here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* Set up for status notification */
	us->ip_wanted = 1;

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (us_transfer_length(srb, us)) {
		us_transfer(srb, us, US_DIRECTION(srb->cmnd[0]));
		US_DEBUGP("CBI data stage result is 0x%x\n", srb->result);

		/* if it was aborted, we need to indicate that */
		if (srb->result == USB_STOR_TRANSPORT_ABORTED)
			return USB_STOR_TRANSPORT_ABORTED;
	}

	/* STATUS STAGE */

	/* go to sleep until we get this interrupt */
	down(&(us->ip_waitq));
	
	/* if we were woken up by an abort instead of the actual interrupt */
	if (us->ip_wanted) {
		US_DEBUGP("Did not get interrupt on CBI\n");
		us->ip_wanted = 0;
		return USB_STOR_TRANSPORT_ABORTED;
	}
	
	US_DEBUGP("Got interrupt data (0x%x, 0x%x)\n", 
		  ((unsigned char*)us->irq_urb->transfer_buffer)[0],
		  ((unsigned char*)us->irq_urb->transfer_buffer)[1]);
	
	/* UFI gives us ASC and ASCQ, like a request sense
	 *
	 * REQUEST_SENSE and INQUIRY don't affect the sense data on UFI
	 * devices, so we ignore the information for those commands.  Note
	 * that this means we could be ignoring a real error on these
	 * commands, but that can't be helped.
	 */
	if (us->subclass == US_SC_UFI) {
		if (srb->cmnd[0] == REQUEST_SENSE ||
		    srb->cmnd[0] == INQUIRY)
			return USB_STOR_TRANSPORT_GOOD;
		else
			if (((unsigned char*)us->irq_urb->transfer_buffer)[0])
				return USB_STOR_TRANSPORT_FAILED;
			else
				return USB_STOR_TRANSPORT_GOOD;
	}
	
	/* If not UFI, we interpret the data as a result code 
	 * The first byte should always be a 0x0
	 * The second byte & 0x0F should be 0x0 for good, otherwise error 
	 */
	if (((unsigned char*)us->irq_urb->transfer_buffer)[0]) {
		US_DEBUGP("CBI IRQ data showed reserved bType\n");
		return USB_STOR_TRANSPORT_ERROR;
	}
	switch (((unsigned char*)us->irq_urb->transfer_buffer)[1] & 0x0F) {
	case 0x00: 
		return USB_STOR_TRANSPORT_GOOD;
	case 0x01: 
		return USB_STOR_TRANSPORT_FAILED;
	default: 
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/*
 * Control/Bulk transport
 */
int usb_stor_CB_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_stor_control_msg(us, usb_sndctrlpipe(us->pusb_dev,0),
				      US_CBI_ADSC, 
				      USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, 
				      us->ifnum, srb->cmnd, srb->cmd_len);

	/* check the return code for the command */
	US_DEBUGP("Call to usb_stor_control_msg() returned %d\n", result);
	if (result < 0) {
		/* if the command was aborted, indicate that */
		if (result == -ENOENT)
			return USB_STOR_TRANSPORT_ABORTED;

		/* a stall is a fatal condition from the device */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe. Clearing\n");
			result = usb_clear_halt(us->pusb_dev, 
						usb_sndctrlpipe(us->pusb_dev,
								0));
			US_DEBUGP("-- usb_clear_halt() returns %d\n", result);
			return USB_STOR_TRANSPORT_FAILED;
		}

		/* Uh oh... serious problem here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (us_transfer_length(srb, us)) {
		us_transfer(srb, us, US_DIRECTION(srb->cmnd[0]));
		US_DEBUGP("CB data stage result is 0x%x\n", srb->result);

		/* if it was aborted, we need to indicate that */
		if (srb->result == USB_STOR_TRANSPORT_ABORTED)
			return USB_STOR_TRANSPORT_ABORTED;
	}

	/* STATUS STAGE */
	/* NOTE: CB does not have a status stage.  Silly, I know.  So
	 * we have to catch this at a higher level.
	 */
	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Bulk only transport
 */

/* Determine what the maximum LUN supported is */
int usb_stor_Bulk_max_lun(struct us_data *us)
{
	unsigned char data;
	int result;
	int pipe;

	/* issue the command */
	pipe = usb_rcvctrlpipe(us->pusb_dev, 0);
	result = usb_control_msg(us->pusb_dev, pipe,
				 US_BULK_GET_MAX_LUN, 
				 USB_DIR_IN | USB_TYPE_CLASS | 
				 USB_RECIP_INTERFACE,
				 0, us->ifnum, &data, sizeof(data), HZ);

	US_DEBUGP("GetMaxLUN command result is %d, data is %d\n", 
		  result, data);

	/* if we have a successful request, return the result */
	if (result == 1)
		return data;

	/* if we get a STALL, clear the stall */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);
	}

	/* return the default -- no LUNs */
	return 0;
}

int usb_stor_Bulk_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	struct bulk_cb_wrap bcb;
	struct bulk_cs_wrap bcs;
	int result;
	int pipe;
	int partial;
	
	/* set up the command wrapper */
	bcb.Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb.DataTransferLength = cpu_to_le32(us_transfer_length(srb, us));
	bcb.Flags = US_DIRECTION(srb->cmnd[0]) << 7;
	bcb.Tag = srb->serial_number;
	bcb.Lun = srb->cmnd[1] >> 5;
	bcb.Length = srb->cmd_len;
	
	/* construct the pipe handle */
	pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);
	
	/* copy the command payload */
	memset(bcb.CDB, 0, sizeof(bcb.CDB));
	memcpy(bcb.CDB, srb->cmnd, bcb.Length);
	
	/* send it to out endpoint */
	US_DEBUGP("Bulk command S 0x%x T 0x%x LUN %d L %d F %d CL %d\n",
		  le32_to_cpu(bcb.Signature), bcb.Tag, bcb.Lun, 
		  bcb.DataTransferLength, bcb.Flags, bcb.Length);
	result = usb_stor_bulk_msg(us, &bcb, pipe, US_BULK_CB_WRAP_LEN, 
				   &partial);
	US_DEBUGP("Bulk command transfer result=%d\n", result);
	
	/* if the command was aborted, indicate that */
	if (result == -ENOENT)
		return USB_STOR_TRANSPORT_ABORTED;

	/* if we stall, we need to clear it before we go on */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);
	} else if (result) {
		/* unknown error -- we've got a problem */
		return USB_STOR_TRANSPORT_ERROR;
	}
	
	/* if the command transfered well, then we go to the data stage */
	if (result == 0) {
		/* send/receive data payload, if there is any */
		if (bcb.DataTransferLength) {
			us_transfer(srb, us, bcb.Flags);
			US_DEBUGP("Bulk data transfer result 0x%x\n", 
				  srb->result);

			/* if it was aborted, we need to indicate that */
			if (srb->result == USB_STOR_TRANSPORT_ABORTED)
				return USB_STOR_TRANSPORT_ABORTED;
		}
	}
	
	/* See flow chart on pg 15 of the Bulk Only Transport spec for
	 * an explanation of how this code works.
	 */
	
	/* construct the pipe handle */
	pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	
	/* get CSW for device status */
	US_DEBUGP("Attempting to get CSW...\n");
	result = usb_stor_bulk_msg(us, &bcs, pipe, US_BULK_CS_WRAP_LEN, 
				   &partial);

	/* if the command was aborted, indicate that */
	if (result == -ENOENT)
		return USB_STOR_TRANSPORT_ABORTED;

	/* did the attempt to read the CSW fail? */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);
	       
		/* get the status again */
		US_DEBUGP("Attempting to get CSW (2nd try)...\n");
		result = usb_stor_bulk_msg(us, &bcs, pipe,
					   US_BULK_CS_WRAP_LEN, &partial);

		/* if the command was aborted, indicate that */
		if (result == -ENOENT)
			return USB_STOR_TRANSPORT_ABORTED;
		
		/* if it fails again, we need a reset and return an error*/
		if (result == -EPIPE) {
			US_DEBUGP("clearing halt for pipe 0x%x\n", pipe);
			usb_clear_halt(us->pusb_dev, pipe);
			return USB_STOR_TRANSPORT_ERROR;
		}
	}
	
	/* if we still have a failure at this point, we're in trouble */
	US_DEBUGP("Bulk status result = %d\n", result);
	if (result) {
		return USB_STOR_TRANSPORT_ERROR;
	}
	
	/* check bulk status */
	US_DEBUGP("Bulk status S 0x%x T 0x%x R %d V 0x%x\n",
		  le32_to_cpu(bcs.Signature), bcs.Tag, 
		  bcs.Residue, bcs.Status);
	if (bcs.Signature != cpu_to_le32(US_BULK_CS_SIGN) || 
	    bcs.Tag != bcb.Tag || 
	    bcs.Status > US_BULK_STAT_PHASE || partial != 13) {
		US_DEBUGP("Bulk logical error\n");
		return USB_STOR_TRANSPORT_ERROR;
	}
	
	/* based on the status code, we report good or bad */
	switch (bcs.Status) {
	case US_BULK_STAT_OK:
		/* command good -- note that we could be short on data */
		return USB_STOR_TRANSPORT_GOOD;

	case US_BULK_STAT_FAIL:
		/* command failed */
		return USB_STOR_TRANSPORT_FAILED;
		
	case US_BULK_STAT_PHASE:
		/* phase error */
		return USB_STOR_TRANSPORT_ERROR;
	}
	
	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/***********************************************************************
 * Reset routines
 ***********************************************************************/

/* This issues a CB[I] Reset to the device in question
 */
int usb_stor_CB_reset(struct us_data *us)
{
	unsigned char cmd[12];
	int result;

	US_DEBUGP("CB_reset() called\n");

	memset(cmd, 0xFF, sizeof(cmd));
	cmd[0] = SEND_DIAGNOSTIC;
	cmd[1] = 4;
	result = usb_control_msg(us->pusb_dev, usb_sndctrlpipe(us->pusb_dev,0),
				 US_CBI_ADSC, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, cmd, sizeof(cmd), HZ*5);

	/* long wait for reset */
	schedule_timeout(HZ*6);

	US_DEBUGP("CB_reset: clearing endpoint halt\n");
	usb_clear_halt(us->pusb_dev, 
		       usb_rcvbulkpipe(us->pusb_dev, us->ep_in));
	usb_clear_halt(us->pusb_dev, 
		       usb_rcvbulkpipe(us->pusb_dev, us->ep_out));

	US_DEBUGP("CB_reset done\n");
	return 0;
}

/* FIXME: Does this work? */
int usb_stor_Bulk_reset(struct us_data *us)
{
	int result;

	result = usb_control_msg(us->pusb_dev, 
				 usb_sndctrlpipe(us->pusb_dev,0), 
				 US_BULK_RESET_REQUEST, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, NULL, 0, HZ*5);

	if (result < 0)
		US_DEBUGP("Bulk hard reset failed %d\n", result);

	usb_clear_halt(us->pusb_dev, 
		       usb_rcvbulkpipe(us->pusb_dev, us->ep_in));
	usb_clear_halt(us->pusb_dev, 
		       usb_sndbulkpipe(us->pusb_dev, us->ep_out));

	/* long wait for reset */
	schedule_timeout(HZ*6);

	return result;
}

