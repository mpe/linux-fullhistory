/* Driver for USB Mass Storage compliant devices
 *
 * (c) 1999 Michael Gee (michael@linuxspecific.com)
 * (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/usb.h>

#include <linux/blk.h>
#include "../scsi/scsi.h"
#include "../scsi/hosts.h"
#include "../scsi/sd.h"

#include "usb-storage.h"
#include "usb-storage-debug.h"

/* direction table -- this indicates the direction of the data
 * transfer for each command code -- a 1 indicates input
 */
/* FIXME: we need to use the new direction indicators in the Scsi_Cmnd
 * structure, not this table.  First we need to evaluate if it's being set
 * correctly for us, though
 */
unsigned char us_direction[256/8] = {
	0x28, 0x81, 0x14, 0x14, 0x20, 0x01, 0x90, 0x77, 
	0x0C, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Per device data
 */

static int my_host_number;

struct us_data;

typedef int (*trans_cmnd)(Scsi_Cmnd*, struct us_data*);
typedef int (*trans_reset)(struct us_data*);
typedef void (*proto_cmnd)(Scsi_Cmnd*, struct us_data*);

/* we allocate one of these for every device that we remember */
struct us_data {
	struct us_data	        *next;	         /* next device */

	/* the device we're working with */
	struct semaphore	dev_semaphore;   /* protect pusb_dev */
	struct usb_device	*pusb_dev;       /* this usb_device */

	unsigned int		flags;		 /* from filter initially */

	/* information about the device -- always good */
	char                    vendor[32];
	char                    product[32];
	char                    serial[32];
	char                    *transport_name;
	char                    *protocol_name;
	__u8			subclass;
	__u8			protocol;

	/* information about the device -- only good if device is attached */
	__u8			ifnum;		 /* interface number   */
	__u8			ep_in;		 /* bulk in endpoint   */
	__u8			ep_out;		 /* bulk out endpoint  */
	__u8			ep_int;		 /* interrupt endpoint */
	__u8                    ep_interval;     /* interrupt interval */

	/* function pointers for this device */
	trans_cmnd              transport;	 /* transport function     */
	trans_reset             transport_reset; /* transport device reset */
	proto_cmnd              proto_handler;   /* protocol handler       */

	/* SCSI interfaces */
	GUID(guid);				 /* unique dev id       */
	struct Scsi_Host	*host;		 /* our dummy host data */
	Scsi_Host_Template	htmplt;  	 /* own host template   */
	int			host_number;	 /* to find us          */
	int			host_no;	 /* allocated by scsi   */
	Scsi_Cmnd		*srb;		 /* current srb         */
	
	/* thread information */
	Scsi_Cmnd		*queue_srb;	 /* the single queue slot */
	int			action;		 /* what to do            */
	int			pid;		 /* control thread        */

	/* interrupt info for CBI devices -- only good if attached */
	struct semaphore	ip_waitq;	 /* for CBI interrupts   */
	__u16			ip_data;	 /* interrupt data       */
	int			ip_wanted;	 /* is an IRQ expected?  */
	void			*irq_handle;	 /* for USB int requests */
	unsigned int		irqpipe;	 /* pipe for release_irq */

	/* mutual exclusion structures */
	struct semaphore	notify;          /* thread begin/end        */
	struct semaphore        sleeper;         /* to sleep the thread on  */
	struct semaphore        queue_exclusion; /* to protect data structs */
};

/*
 * kernel thread actions
 */

#define US_ACT_COMMAND		1
#define US_ACT_DEVICE_RESET	2
#define US_ACT_BUS_RESET	3
#define US_ACT_HOST_RESET	4
#define US_ACT_EXIT		5

/* The list of structures and the protective lock for them */
static struct us_data *us_list;
struct semaphore us_list_semaphore;

static void * storage_probe(struct usb_device *dev, unsigned int ifnum);
static void storage_disconnect(struct usb_device *dev, void *ptr);
static struct usb_driver storage_driver = {
	name:		"usb-storage",
	probe:		storage_probe,
	disconnect:	storage_disconnect,
};

/***********************************************************************
 * Data transfer routines
 ***********************************************************************/

/*
 * Transfer one SCSI scatter-gather buffer via bulk transfer
 *
 * Note that this function is necessary because we want the ability to
 * use scatter-gather memory.  Good performance is achived by a combination
 * of scatter-gather and clustering (which makes each chunk bigger).
 *
 * Note that the lower layer will always retry when a NAK occurs, up to the
 * timeout limit.  Thus we don't have to worry about it for individual
 * packets.
 */
static int us_transfer_partial(struct us_data *us, int pipe, 
			       char *buf, int length)
{
	int result;
	int partial;

	/* transfer the data */
	US_DEBUGP("Bulk xfer 0x%x(%d)\n", (unsigned int)buf, length);
	result = usb_bulk_msg(us->pusb_dev, pipe, buf, length, &partial, HZ);
	US_DEBUGP("bulk_msg returned %d xferred %d/%d\n",
		  result, partial, length);
	
	/* if we stall, we need to clear it before we go on */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);
	}

	/* did we send all the data? */
	if (partial == length) {
		return US_BULK_TRANSFER_GOOD;
	}

	/* uh oh... we have an error code, so something went wrong. */
	if (result) {
		/* NAK - that means we've retried a few times allready */
		if (result == -ETIMEDOUT) {
			US_DEBUGP("us_transfer_partial(): device NAKed\n");
		}
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
static void us_transfer(Scsi_Cmnd *srb, int dir_in)
{
	struct us_data *us;
	int i;
	int result = -1;
	unsigned int pipe;
	struct scatterlist *sg;

	/* calculate the appropriate pipe information */
	us = (struct us_data*) srb->host_scribble;
	if (dir_in)
		pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	else
		pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);

	/* are we scatter-gathering? */
	if (srb->use_sg) {

		/* loop over all the scatter gather structures and 
		 * make the appropriate requests for each, until done
		 */
		sg = (struct scatterlist *) srb->request_buffer;
		for (i = 0; i < srb->use_sg; i++) {
			result = us_transfer_partial(us, pipe, sg[i].address,
						     sg[i].length);
			if (result)
				break;
		}
	}
	else
		/* no scatter-gather, just make the request */
		result = us_transfer_partial(us, pipe, srb->request_buffer,
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
static void invoke_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int need_auto_sense;
	int result;

	/* send the command to the transport layer */
	result = us->transport(srb, us);

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

		srb->cmnd[0] = REQUEST_SENSE;
		srb->cmnd[1] = 0;
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
		if (temp_result == USB_STOR_TRANSPORT_ERROR) {
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
static int CBI_irq(int state, void *buffer, int len, void *dev_id)
{
	struct us_data *us = (struct us_data *)dev_id;

	US_DEBUGP("USB IRQ recieved for device on host %d\n", us->host_no);
	US_DEBUGP("-- IRQ data length is %d\n", len);
	US_DEBUGP("-- IRQ state is %d\n", state);

	/* is the device removed? */
	if (state != -ENOENT) {
		/* save the data for interpretation later */
		us->ip_data = le16_to_cpup((__u16 *)buffer);
		US_DEBUGP("-- Interrupt Status 0x%x\n", us->ip_data);
  
		/* was this a wanted interrupt? */
		if (us->ip_wanted) {
			us->ip_wanted = 0;
			up(&(us->ip_waitq));
		} else
			US_DEBUGP("ERROR: Unwanted interrupt received!\n");
	} else
		US_DEBUGP("-- device has been removed\n");

	/* This return code is truly meaningless -- and I mean truly.  It gets
	 * ignored by other layers.  It used to indicate if we wanted to get
	 * another interrupt or disable the interrupt callback
	 */
	return 0;
}

static int CBI_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_control_msg(us->pusb_dev, 
				 usb_sndctrlpipe(us->pusb_dev,0), US_CBI_ADSC, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, 
				 us->ifnum, srb->cmnd, srb->cmd_len, HZ*5);

	/* check the return code for the command */
	US_DEBUGP("Call to usb_control_msg() returned %d\n", result);
	if (result < 0) {
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
		us_transfer(srb, US_DIRECTION(srb->cmnd[0]));
		US_DEBUGP("CBI data stage result is 0x%x\n", srb->result);
	}

	/* STATUS STAGE */

	/* go to sleep until we get this interrupt */
	down(&(us->ip_waitq));
	
	/* if we were woken up by an abort instead of the actual interrupt */
	if (us->ip_wanted) {
		US_DEBUGP("Did not get interrupt on CBI\n");
		us->ip_wanted = 0;
		return USB_STOR_TRANSPORT_ERROR;
	}
	
	US_DEBUGP("Got interrupt data 0x%x\n", us->ip_data);
	
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
			if (us->ip_data)
				return USB_STOR_TRANSPORT_FAILED;
			else
				return USB_STOR_TRANSPORT_GOOD;
	}
	
	/* If not UFI, we interpret the data as a result code 
	 * The first byte should always be a 0x0
	 * The second byte & 0x0F should be 0x0 for good, otherwise error 
	 */
	switch ((us->ip_data & 0xFF0F)) {
	case 0x0000: 
		return USB_STOR_TRANSPORT_GOOD;
	case 0x0001: 
		return USB_STOR_TRANSPORT_FAILED;
	default: 
		return USB_STOR_TRANSPORT_ERROR;
	}

	US_DEBUGP("CBI_transport() reached end of function\n");
	return USB_STOR_TRANSPORT_ERROR;
}

/*
 * Control/Bulk transport
 */
static int CB_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_control_msg(us->pusb_dev, 
				 usb_sndctrlpipe(us->pusb_dev,0), US_CBI_ADSC, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, 
				 us->ifnum, srb->cmnd, srb->cmd_len, HZ*5);

	/* check the return code for the command */
	if (result < 0) {
		US_DEBUGP("Call to usb_control_msg() returned %d\n", result);

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
		us_transfer(srb, US_DIRECTION(srb->cmnd[0]));
		US_DEBUGP("CB data stage result is 0x%x\n", srb->result);
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
static int Bulk_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	struct bulk_cb_wrap bcb;
	struct bulk_cs_wrap bcs;
	int result;
	int pipe;
	int partial;
	
	/* set up the command wrapper */
	bcb.Signature = US_BULK_CB_SIGN;
	bcb.DataTransferLength = us_transfer_length(srb, us);
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
		  bcb.Signature, bcb.Tag, bcb.Lun, bcb.DataTransferLength,
		  bcb.Flags, bcb.Length);
	result = usb_bulk_msg(us->pusb_dev, pipe, &bcb,
			      US_BULK_CB_WRAP_LEN, &partial, HZ*5);
	US_DEBUGP("Bulk command transfer result=%d\n", result);
	
	/* if we stall, we need to clear it before we go on */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);
	}
	
	/* if the command transfered well, then we go to the data stage */
	if (result == 0) {
		/* send/receive data payload, if there is any */
		if (bcb.DataTransferLength) {
			us_transfer(srb, bcb.Flags);
			US_DEBUGP("Bulk data transfer result 0x%x\n", 
				  srb->result);
		}
	}
	
	/* See flow chart on pg 15 of the Bulk Only Transport spec for
	 * an explanation of how this code works.
	 */
	
	/* construct the pipe handle */
	pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	
	/* get CSW for device status */
	US_DEBUGP("Attempting to get CSW...\n");
	result = usb_bulk_msg(us->pusb_dev, pipe, &bcs,
			      US_BULK_CS_WRAP_LEN, &partial, HZ);
	
	/* did the attempt to read the CSW fail? */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);

		/* get the status again */
		US_DEBUGP("Attempting to get CSW (2nd try)...\n");
		result = usb_bulk_msg(us->pusb_dev, pipe, &bcs,
				      US_BULK_CS_WRAP_LEN, &partial, HZ);
		
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
		  bcs.Signature, bcs.Tag, bcs.Residue, bcs.Status);
	if (bcs.Signature != US_BULK_CS_SIGN || bcs.Tag != bcb.Tag ||
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
 * Protocol routines
 ***********************************************************************/

static void ATAPI_command(Scsi_Cmnd *srb, struct us_data *us)
{
	int old_cmnd = 0;
  
	/* Fix some commands -- this is a form of mode translation
	 * ATAPI devices only accept 12 byte long commands 
	 *
	 * NOTE: This only works because a Scsi_Cmnd struct field contains
	 * a unsigned char cmnd[12], so we know we have storage available
	 */

	/* set command length to 12 bytes */
	srb->cmd_len = 12;

	/* determine the correct (or minimum) data length for these commands */
	switch (srb->cmnd[0]) {

		/* change MODE_SENSE/MODE_SELECT from 6 to 10 byte commands */
	case MODE_SENSE:
	case MODE_SELECT:
		/* save the command so we can tell what it was */
		old_cmnd = srb->cmnd[0];

		srb->cmnd[11] = 0;
		srb->cmnd[10] = 0;
		srb->cmnd[9] = 0;
		srb->cmnd[8] = srb->cmnd[4];
		srb->cmnd[7] = 0;
		srb->cmnd[6] = 0;
		srb->cmnd[5] = 0;
		srb->cmnd[4] = 0;
		srb->cmnd[3] = 0;
		srb->cmnd[2] = srb->cmnd[2];
		srb->cmnd[1] = srb->cmnd[1];
		srb->cmnd[0] = srb->cmnd[0] | 0x40;
		break;

		/* change READ_6/WRITE_6 to READ_10/WRITE_10, which 
		 * are ATAPI commands */
	case WRITE_6:
	case READ_6:
		srb->cmnd[11] = 0;
		srb->cmnd[10] = 0;
		srb->cmnd[9] = 0;
		srb->cmnd[8] = srb->cmnd[4];
		srb->cmnd[7] = 0;
		srb->cmnd[6] = 0;
		srb->cmnd[5] = srb->cmnd[3];
		srb->cmnd[4] = srb->cmnd[2];
		srb->cmnd[3] = srb->cmnd[1] & 0x1F;
		srb->cmnd[2] = 0;
		srb->cmnd[1] = srb->cmnd[1] & 0xE0;
		srb->cmnd[0] = srb->cmnd[0] | 0x20;
		break;
	} /* end switch on cmnd[0] */
  
	/* send the command to the transport layer */
	invoke_transport(srb, us);
  
	/* Fix the MODE_SENSE data if we translated the command
	 */
	if (old_cmnd == MODE_SENSE) {
		unsigned char *dta = (unsigned char *)us->srb->request_buffer;

		/* FIXME: we need to compress the entire data structure here
		 */
		dta[0] = dta[1];	/* data len */
		dta[1] = dta[2];	/* med type */
		dta[2] = dta[3];	/* dev-spec prm */
		dta[3] = dta[7];	/* block desc len */
		printk (KERN_DEBUG USB_STORAGE
			"new MODE_SENSE_6 data = %.2X %.2X %.2X %.2X\n",
			dta[0], dta[1], dta[2], dta[3]);
	}

	/* Fix-up the return data from an INQUIRY command to show 
	 * ANSI SCSI rev 2 so we don't confuse the SCSI layers above us
	 */
	if (srb->cmnd[0] == INQUIRY) {
		((unsigned char *)us->srb->request_buffer)[2] |= 0x2;
	}
}


static void ufi_command(Scsi_Cmnd *srb, struct us_data *us)
{
	int old_cmnd = 0;
  
	/* fix some commands -- this is a form of mode translation
	 * UFI devices only accept 12 byte long commands 
	 *
	 * NOTE: This only works because a Scsi_Cmnd struct field contains
	 * a unsigned char cmnd[12], so we know we have storage available
	 */

	/* set command length to 12 bytes (this affects the transport layer) */
	srb->cmd_len = 12;

	/* determine the correct (or minimum) data length for these commands */
	switch (srb->cmnd[0]) {

		/* for INQUIRY, UFI devices only ever return 36 bytes */
	case INQUIRY:
		srb->cmnd[4] = 36;
		break;

		/* change MODE_SENSE/MODE_SELECT from 6 to 10 byte commands */
	case MODE_SENSE:
	case MODE_SELECT:
		/* save the command so we can tell what it was */
		old_cmnd = srb->cmnd[0];

		srb->cmnd[11] = 0;
		srb->cmnd[10] = 0;
		srb->cmnd[9] = 0;

		/* if we're sending data, we send all.  If getting data, 
		 * get the minimum */
		if (srb->cmnd[0] == MODE_SELECT)
			srb->cmnd[8] = srb->cmnd[4];
		else
			srb->cmnd[8] = 8;

		srb->cmnd[7] = 0;
		srb->cmnd[6] = 0;
		srb->cmnd[5] = 0;
		srb->cmnd[4] = 0;
		srb->cmnd[3] = 0;
		srb->cmnd[2] = srb->cmnd[2];
		srb->cmnd[1] = srb->cmnd[1];
		srb->cmnd[0] = srb->cmnd[0] | 0x40;
		break;

		/* again, for MODE_SENSE_10, we get the minimum (8) */
	case MODE_SENSE_10:
		srb->cmnd[7] = 0;
		srb->cmnd[8] = 8;
		break;
 
		/* for REQUEST_SENSE, UFI devices only ever return 18 bytes */
	case REQUEST_SENSE:
		srb->cmnd[4] = 18;
		break;

		/* change READ_6/WRITE_6 to READ_10/WRITE_10, which 
		 * are UFI commands */
	case WRITE_6:
	case READ_6:
		srb->cmnd[11] = 0;
		srb->cmnd[10] = 0;
		srb->cmnd[9] = 0;
		srb->cmnd[8] = srb->cmnd[4];
		srb->cmnd[7] = 0;
		srb->cmnd[6] = 0;
		srb->cmnd[5] = srb->cmnd[3];
		srb->cmnd[4] = srb->cmnd[2];
		srb->cmnd[3] = srb->cmnd[1] & 0x1F;
		srb->cmnd[2] = 0;
		srb->cmnd[1] = srb->cmnd[1] & 0xE0;
		srb->cmnd[0] = srb->cmnd[0] | 0x20;
		break;
	} /* end switch on cmnd[0] */

	/* send the command to the transport layer */
	invoke_transport(srb, us);
	
	/* Fix the MODE_SENSE data here if we had to translate the command
	 */
	if (old_cmnd == MODE_SENSE) {
		unsigned char *dta = (unsigned char *)us->srb->request_buffer;

		/* FIXME: we need to compress the entire data structure here
		 */
		dta[0] = dta[1];	/* data len */
		dta[1] = dta[2];	/* med type */
		dta[2] = dta[3];	/* dev-spec prm */
		dta[3] = dta[7];	/* block desc len */
		printk (KERN_DEBUG USB_STORAGE
			"new MODE_SENSE_6 data = %.2X %.2X %.2X %.2X\n",
			dta[0], dta[1], dta[2], dta[3]);
	}

	/* Fix-up the return data from an INQUIRY command to show 
	 * ANSI SCSI rev 2 so we don't confuse the SCSI layers above us
	 */
	if (srb->cmnd[0] == INQUIRY) {
		((unsigned char *)us->srb->request_buffer)[2] |= 0x2;
	}
}

static void transparent_scsi_command(Scsi_Cmnd *srb, struct us_data *us)
{
	/* This code supports devices which do not support {READ|WRITE}_6
	 * Apparently, neither Windows or MacOS will use these commands,
	 * so some devices do not support them
	 */
	if (us->flags & US_FL_MODE_XLATE) {
    
		/* translate READ_6 to READ_10 */
		if (srb->cmnd[0] == 0x08) {
      
			/* get the control */
			srb->cmnd[9] = us->srb->cmnd[5];
      
			/* get the length */
			srb->cmnd[8] = us->srb->cmnd[6];
			srb->cmnd[7] = 0;
      
			/* set the reserved area to 0 */
			srb->cmnd[6] = 0;	    
      
			/* get LBA */
			srb->cmnd[5] = us->srb->cmnd[3];
			srb->cmnd[4] = us->srb->cmnd[2];
			srb->cmnd[3] = 0;
			srb->cmnd[2] = 0;
      
			/* LUN and other info in cmnd[1] can stay */
      
			/* fix command code */
			srb->cmnd[0] = 0x28;
      
			US_DEBUGP("Changing READ_6 to READ_10\n");
			US_DEBUG(us_show_command(srb));
		}
    
		/* translate WRITE_6 to WRITE_10 */
		if (srb->cmnd[0] == 0x0A) {
      
			/* get the control */
			srb->cmnd[9] = us->srb->cmnd[5];
      
			/* get the length */
			srb->cmnd[8] = us->srb->cmnd[4];
			srb->cmnd[7] = 0;
      
			/* set the reserved area to 0 */
			srb->cmnd[6] = 0;	    
      
			/* get LBA */
			srb->cmnd[5] = us->srb->cmnd[3];
			srb->cmnd[4] = us->srb->cmnd[2];
			srb->cmnd[3] = 0;
			srb->cmnd[2] = 0;
	    
			/* LUN and other info in cmnd[1] can stay */
      
			/* fix command code */
			srb->cmnd[0] = 0x2A;

			US_DEBUGP("Changing WRITE_6 to WRITE_10\n");
			US_DEBUG(us_show_command(us->srb));
		}
	} /* if (us->flags & US_FL_MODE_XLATE) */

	/* send the command to the transport layer */
	invoke_transport(srb, us);

	/* fix the results of an INQUIRY */
	if (srb->cmnd[0] == INQUIRY) {
		US_DEBUGP("Fixing INQUIRY data, setting SCSI rev to 2\n");
		((unsigned char*)us->srb->request_buffer)[2] |= 2;
	}
}

/***********************************************************************
 * Reset routines
 ***********************************************************************/

/* This issues a CB[I] Reset to the device in question
 */
static int CB_reset(struct us_data *us)
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
static int Bulk_reset(struct us_data *us)
{
	int result;

	result = usb_control_msg(us->pusb_dev, 
				 usb_sndctrlpipe(us->pusb_dev,0), 
				 US_BULK_RESET, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 US_BULK_RESET_HARD, us->ifnum, NULL, 0, HZ*5);

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

/***********************************************************************
 * Host functions 
 ***********************************************************************/

static const char* us_info(struct Scsi_Host *host)
{
	return "SCSI emulation for USB Mass Storage devices\n";
}

/* detect a virtual adapter (always works) */
static int us_detect(struct SHT *sht)
{
	struct us_data *us;
	char local_name[32];

	/* This is not nice at all, but how else are we to get the
	 * data here? */
	us = (struct us_data *)sht->proc_dir;

	/* set up the name of our subdirectory under /proc/scsi/ */
	sprintf(local_name, "usb-storage-%d", us->host_number);
	sht->proc_name = kmalloc (strlen(local_name) + 1, GFP_KERNEL);
	if (!sht->proc_name)
		return 0;
	strcpy(sht->proc_name, local_name);

	/* we start with no /proc directory entry */
	sht->proc_dir = NULL;

	/* register the host */
	us->host = scsi_register(sht, sizeof(us));
	if (us->host) {
		us->host->hostdata[0] = (unsigned long)us;
		us->host_no = us->host->host_no;
		return 1;
	}

	/* odd... didn't register properly.  Abort and free pointers */
	kfree(sht->proc_name);
	sht->proc_name = NULL;
	return 0;
}

/* Release all resources used by the virtual host
 *
 * NOTE: There is no contention here, because we're allready deregistered
 * the driver and we're doing each virtual host in turn, not in parallel
 */
static int us_release(struct Scsi_Host *psh)
{
	struct us_data *us = (struct us_data *)psh->hostdata[0];

	US_DEBUGP("us_release() called for host %s\n", us->htmplt.name);

	/* Kill the control threads
	 *
	 * Enqueue the command, wake up the thread, and wait for 
	 * notification that it's exited.
	 */
	US_DEBUGP("-- sending US_ACT_EXIT command to thread\n");
	us->action = US_ACT_EXIT;
	up(&(us->sleeper));
	down(&(us->notify));
	
	/* free the data structure we were using */
	US_DEBUGP("-- freeing private host data structure\n");
	kfree(us);
	(struct us_data*)psh->hostdata[0] = NULL;

	/* we always have a successful release */
	return 0;
}

/* run command */
static int us_command( Scsi_Cmnd *srb )
{
	US_DEBUGP("Bad use of us_command\n");

	return DID_BAD_TARGET << 16;
}

/* run command */
static int us_queuecommand( Scsi_Cmnd *srb , void (*done)(Scsi_Cmnd *))
{
	struct us_data *us = (struct us_data *)srb->host->hostdata[0];

	US_DEBUGP("us_queuecommand() called\n");
	srb->host_scribble = (unsigned char *)us;

	/* get exclusive access to the structures we want */
	down(&(us->queue_exclusion));

	/* enqueue the command */
	us->queue_srb = srb;
	srb->scsi_done = done;
	us->action = US_ACT_COMMAND;

	/* wake up the process task */
	up(&(us->queue_exclusion));
	up(&(us->sleeper));

	return 0;
}

/***********************************************************************
 * Error handling functions
 ***********************************************************************/

/* Command abort
 *
 * Note that this is really only meaningful right now for CBI transport
 * devices which have failed to give us the command completion interrupt
 */
static int us_abort( Scsi_Cmnd *srb )
{
	struct us_data *us = (struct us_data *)srb->host->hostdata[0];

	US_DEBUGP("us_abort() called\n");

	/* if we're stuck waiting for an IRQ, simulate it */
	if (us->ip_wanted) {
		US_DEBUGP("-- simulating missing IRQ\n");
		up(&(us->ip_waitq));
		return SUCCESS;
	}

	return FAILED;
}

/* FIXME: this doesn't do anything right now */
static int us_bus_reset( Scsi_Cmnd *srb )
{
	// struct us_data *us = (struct us_data *)srb->host->hostdata[0];

	printk(KERN_CRIT "usb-storage: bus_reset() requested but not implemented\n" );
	US_DEBUGP("Bus reset requested\n");
	//  us->transport_reset(us);
	return FAILED;
}

/* FIXME: This doesn't actually reset anything */
static int us_host_reset( Scsi_Cmnd *srb )
{
	printk(KERN_CRIT "usb-storage: host_reset() requested but not implemented\n" );
	return FAILED;
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

/* we use this macro to help us write into the buffer */
#undef SPRINTF
#define SPRINTF(args...) \
	do { if (pos < buffer+length) pos += sprintf(pos, ## args); } while (0)

int usb_stor_proc_info (char *buffer, char **start, off_t offset, 
			int length, int hostno, int inout)
{
	struct us_data *us;
	char *pos = buffer;

	/* if someone is sending us data, just throw it away */
	if (inout)
		return length;

	/* lock the data structures */
	down(&us_list_semaphore);

	/* find our data from hostno */
	us = us_list;
	while (us) {
		if (us->host_no == hostno)
			break;
		us = us->next;
	}

	/* if we couldn't find it, we return an error */
	if (!us) {
		up(&us_list_semaphore);
		return -ESRCH;
	}
	
	/* print the controler name */
	SPRINTF("   Host scsi%d: usb-storage\n", hostno);

	/* print product, vendor, and serial number strings */
	SPRINTF("       Vendor: %s\n", us->vendor);
	SPRINTF("      Product: %s\n", us->product);
	SPRINTF("Serial Number: %s\n", us->serial);

	/* show the protocol and transport */
	SPRINTF("     Protocol: %s\n", us->protocol_name);
	SPRINTF("    Transport: %s\n", us->transport_name);

	/* show the GUID of the device */
	SPRINTF("         GUID: " GUID_FORMAT "\n", GUID_ARGS(us->guid));

	/* release our lock on the data structures */
	up(&us_list_semaphore);

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
 * this defines our 'host'
 */

static Scsi_Host_Template my_host_template = {
	name:			"usb-storage",
	proc_info:		usb_stor_proc_info,
	info:			us_info,

	detect:			us_detect,
	release:		us_release,
	command:		us_command,
	queuecommand:		us_queuecommand,

	eh_abort_handler:	us_abort,
	eh_device_reset_handler:us_bus_reset,
	eh_bus_reset_handler:	us_bus_reset,
	eh_host_reset_handler:	us_host_reset,

	can_queue:		1,
	this_id:		-1,

	sg_tablesize:		SG_ALL,
	cmd_per_lun:		1,
	present:		0,
	unchecked_isa_dma:	FALSE,
	use_clustering:		TRUE,
	use_new_eh_code:	TRUE,
	emulated:		TRUE
};

static unsigned char sense_notready[] = {
	0x70,			    /* current error */
	0x00,
	0x02,			    /* not ready */
	0x00,
	0x00,
	0x0a,			    /* additional length */
	0x00,
	0x00,
	0x00,
	0x00,
	0x04,			    /* not ready */
	0x03,			    /* manual intervention */
	0x00,
	0x00,
	0x00,
	0x00
};

static int usb_stor_control_thread(void * __us)
{
	struct us_data *us = (struct us_data *)__us;
	int action;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources..
	 */
	daemonize();

	/* set our name for identification purposes */
	sprintf(current->comm, "usb-storage-%d", us->host_number);

	unlock_kernel();

	/* signal that we've started the thread */
	up(&(us->notify));

	for(;;) {
		US_DEBUGP("*** thread sleeping.\n");
		down(&(us->sleeper));
		down(&(us->queue_exclusion));
		US_DEBUGP("*** thread awakened.\n");

		/* take the command off the queue */
		action = us->action;
		us->action = 0;
		us->srb = us->queue_srb;
     	
		/* release the queue lock as fast as possible */
		up(&(us->queue_exclusion));

		switch (action) {
		case US_ACT_COMMAND:
			/* reject if target != 0 or if single-lun device
			 * and LUN != 0
			 */
			if (us->srb->target ||
			    ((us->flags & US_FL_SINGLE_LUN) && us->srb->lun)) {
				US_DEBUGP("Bad device number (%d/%d)\n",
					  us->srb->target, us->srb->lun);

				us->srb->result = DID_BAD_TARGET << 16;

				us->srb->scsi_done(us->srb);
				us->srb = NULL;
				break;
			}

			/* handle those devices which can't do a START_STOP */
			if ((us->srb->cmnd[0] == START_STOP) &&
			    (us->flags & US_FL_START_STOP)) {
				us->srb->result = GOOD;
				us->srb->scsi_done(us->srb);
				us->srb = NULL;
				break;
			}
				     
			/* lock the device pointers */
			down(&(us->dev_semaphore));

			/* our device has gone - pretend not ready */
			if (!us->pusb_dev) {
				US_DEBUGP("Request is for removed device\n");
				/* For REQUEST_SENSE, it's the data.  But
				 * for anything else, it should look like
				 * we auto-sensed for it.
				 */
				if (us->srb->cmnd[0] == REQUEST_SENSE) {
					memcpy(us->srb->request_buffer, 
					       sense_notready, 
					       sizeof(sense_notready));
					us->srb->result = GOOD;
				} else {
					memcpy(us->srb->sense_buffer,
					       sense_notready,
					       sizeof(sense_notready));
					us->srb->result = CHECK_CONDITION;
				}
			} else { /* !us->pusb_dev */
				/* we've got a command, let's do it! */
				US_DEBUG(us_show_command(us->srb));
				us->proto_handler(us->srb, us);
			}

			/* unlock the device pointers */
			up(&(us->dev_semaphore));

			/* indicate that the command is done */
			US_DEBUGP("scsi cmd done, result=0x%x\n",
				  us->srb->result);
			us->srb->scsi_done(us->srb);
			us->srb = NULL;
			break;

		case US_ACT_DEVICE_RESET:
			break;

		case US_ACT_BUS_RESET:
			break;

		case US_ACT_HOST_RESET:
			break;

		} /* end switch on action */

		/* exit if we get a signal to exit */
		if (action == US_ACT_EXIT) {
			US_DEBUGP("-- US_ACT_EXIT command recieved\n");
			break;
		}
	} /* for (;;) */

	/* notify the exit routine that we're actually exiting now */
	up(&(us->notify));

	return 0;
}	

static struct us_unusual_dev us_unusual_dev_list[] = {
	{ 0x057b, 0x0000, 0x0114,
	  "Y-E Data Flashbuster-U", US_SC_UFI, US_PR_CB, US_FL_SINGLE_LUN },
	{ 0x0781, 0x0001, 0x0200,
	  "Sandisk ImageMate", US_SC_SCSI, US_PR_CB, 
	  US_FL_SINGLE_LUN | US_FL_START_STOP },
	{ 0x0781, 0x0002, 0x0009,
	  "** SECRET DEVICE **", US_SC_SCSI, US_PR_BULK, US_FL_SINGLE_LUN },
	{ 0x04e6, 0x0002, 0x0100,
	  "Microtech USB-SCSI-HD50", US_SC_SCSI, US_PR_BULK, US_FL_ALT_LENGTH},
	{ 0x07af, 0x0005, 0x0100,
	  "Shuttle eUSCSI Bridge", US_SC_SCSI, US_PR_BULK, US_FL_ALT_LENGTH},
	{ 0x0000, 0x0000, 0x0,
	  "", 0, 0, 0}
};

/* Search our ususual device list, based on vendor/product combinations
 * to see if we can support this device.  Returns a pointer to a structure
 * defining how we should support this device, or NULL if it's not in the
 * list
 */
static struct us_unusual_dev* us_find_dev(__u16 idVendor, __u16 idProduct, 
					  __u16 bcdDevice)
{
	struct us_unusual_dev* ptr;

	US_DEBUGP("Searching unusual device list for (0x%x, 0x%x, 0x%x)...\n",
		  idVendor, idProduct, bcdDevice);

	ptr = us_unusual_dev_list;
	while ((ptr->idVendor != 0x0000) && 
	       !((ptr->idVendor == idVendor) && 
		 (ptr->idProduct == idProduct) &&
		 (ptr->bcdDevice == bcdDevice)))
		ptr++;
	
	/* if the search ended because we hit the end record, we failed */
	if (ptr->idVendor == 0x0000) {
		US_DEBUGP("-- did not find a matching device\n");
		return NULL;
	}

	/* otherwise, we found one! */
	US_DEBUGP("-- found matching device: %s\n", ptr->name);
	return ptr;
}

/* Probe to see if a new device is actually a SCSI device */
static void * storage_probe(struct usb_device *dev, unsigned int ifnum)
{
	int i;
	char mf[USB_STOR_STRING_LEN];		     /* manufacturer */
	char prod[USB_STOR_STRING_LEN];		     /* product */
	char serial[USB_STOR_STRING_LEN];      	     /* serial number */
	GUID(guid);		           /* Global Unique Identifier */
	unsigned int flags;
	struct us_unusual_dev *unusual_dev;
	struct us_data *ss = NULL;
	int result;

	/* these are temporary copies -- we test on these, then put them
	 * in the us-data structure 
	 */
	__u8 ep_in = 0;
	__u8 ep_out = 0;
	__u8 ep_int = 0;
	__u8 ep_interval = 0;
	__u8 subclass = 0;
	__u8 protocol = 0;

	/* the altsettting 0 on the interface we're probing */
	struct usb_interface_descriptor *altsetting = 
		&(dev->actconfig->interface[ifnum].altsetting[0]); 

	/* clear the temporary strings */
	memset(mf, 0, sizeof(mf));
	memset(prod, 0, sizeof(prod));
	memset(serial, 0, sizeof(serial));

	/* search for this device in our unusual device list */
	unusual_dev = us_find_dev(dev->descriptor.idVendor, 
				  dev->descriptor.idProduct,
				  dev->descriptor.bcdDevice);

	/* 
	 * Can we support this device, either because we know about it
	 * from our unusual device list, or because it advertises that it's
	 * compliant to the specification?
	 */
	if (!unusual_dev &&
	    !(dev->descriptor.bDeviceClass == 0 &&
	      altsetting->bInterfaceClass == USB_CLASS_MASS_STORAGE &&
	      altsetting->bInterfaceSubClass >= US_SC_MIN &&
	      altsetting->bInterfaceSubClass <= US_SC_MAX)) {
		/* if it's not a mass storage, we go no further */
		return NULL;
	}

	/* At this point, we know we've got a live one */
	US_DEBUGP("USB Mass Storage device detected\n");

	/* Determine subclass and protocol, or copy from the interface */
	if (unusual_dev) {
		subclass = unusual_dev->useProtocol;
		protocol = unusual_dev->useTransport;
		flags = unusual_dev->flags;
	} else {
		subclass = altsetting->bInterfaceSubClass;
		protocol = altsetting->bInterfaceProtocol;
		flags = 0;
	}
	
	/*
	 * Find the endpoints we need
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * An optional interrupt is OK (necessary for CBI protocol).
	 * We will ignore any others.
	 */
	for (i = 0; i < altsetting->bNumEndpoints; i++) {
		/* is it an BULK endpoint? */
		if ((altsetting->endpoint[i].bmAttributes & 
		     USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
			/* BULK in or out? */
			if (altsetting->endpoint[i].bEndpointAddress & 
			    USB_DIR_IN)
				ep_in = altsetting->endpoint[i].bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
			else
				ep_out = altsetting->endpoint[i].bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
		}

		/* is it an interrupt endpoint? */
		if ((altsetting->endpoint[i].bmAttributes & 
		     USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
			ep_int = altsetting->endpoint[i].bEndpointAddress &
				USB_ENDPOINT_NUMBER_MASK;
			ep_interval = altsetting->endpoint[i].bInterval;
		}
	}
	US_DEBUGP("Endpoints: In %d Out %d Int %d (Period %d)\n",
		  ep_in, ep_out, ep_int, ep_interval);

	/* set the interface -- STALL is an acceptable response here */
	result = usb_set_interface(dev, altsetting->bInterfaceNumber, 0);
	US_DEBUGP("Result from usb_set_interface is %d\n", result);
	if (result == -EPIPE) {
		US_DEBUGP("-- clearing stall on control interface\n");
		usb_clear_halt(dev, usb_sndctrlpipe(dev, 0));
	} else if (result != 0) {
		/* it's not a stall, but another error -- time to bail */
		US_DEBUGP("-- Unknown error.  Rejecting device\n");
		return NULL;
	}

	/* Do some basic sanity checks, and bail if we find a problem */
	if (!ep_in || !ep_out || (protocol == US_PR_CBI && !ep_int)) {
		US_DEBUGP("Sanity check failed.  Rejecting device.\n");
		return NULL;
	}

	/* At this point, we're committed to using the device */

	/* clear the GUID and fetch the strings */
	GUID_CLEAR(guid);
	if (dev->descriptor.iManufacturer)
		usb_string(dev, dev->descriptor.iManufacturer, mf, 
			   sizeof(mf));
	if (dev->descriptor.iProduct)
		usb_string(dev, dev->descriptor.iProduct, prod, 
			   sizeof(prod));
	if (dev->descriptor.iSerialNumber)
		usb_string(dev, dev->descriptor.iSerialNumber, serial, 
			   sizeof(serial));
	
	/* Create a GUID for this device */
	if (dev->descriptor.iSerialNumber && serial[0]) {
		/* If we have a serial number, and it's a non-NULL string */
		make_guid(guid, dev->descriptor.idVendor, 
			  dev->descriptor.idProduct, serial);
	} else {
		/* We don't have a serial number, so we use 0 */
		make_guid(guid, dev->descriptor.idVendor, 
			  dev->descriptor.idProduct, "0");
	}

	/* lock access to the data structures */
	down(&us_list_semaphore);

	/*
	 * Now check if we have seen this GUID before
	 * We're looking for a device with a matching GUID that isn't
	 * allready on the system
	 */
	ss = us_list;
	while ((ss != NULL) && 
	       ((ss->pusb_dev) || !GUID_EQUAL(guid, ss->guid)))
		ss = ss->next;

	if (ss != NULL) {
		/* Existing device -- re-connect */
		US_DEBUGP("Found existing GUID " GUID_FORMAT "\n",
			  GUID_ARGS(guid));

		/* establish the connection to the new device upon reconnect */
		ss->ifnum = ifnum;
		ss->pusb_dev = dev;
	
		/* hook up the IRQ handler again */
		if (ss->protocol == US_PR_CBI) {
			/* set up so we'll wait for notification */
			init_MUTEX_LOCKED(&(ss->ip_waitq));
			
			/* set up the IRQ pipe and handler */
			US_DEBUGP("Allocating IRQ for CBI transport\n");
			ss->irqpipe = usb_rcvintpipe(ss->pusb_dev, ss->ep_int);
			result = usb_request_irq(ss->pusb_dev, ss->irqpipe, 
						 CBI_irq, ss->ep_interval,
						 (void *)ss,
						 &(ss->irq_handle));
			US_DEBUGP("-- usb_request_irq returned %d\n", result);
		}
	} else { 
		/* New device -- Allocate memory and initialize */
		US_DEBUGP("New GUID " GUID_FORMAT "\n", GUID_ARGS(guid));
	
		if ((ss = (struct us_data *)kmalloc(sizeof(struct us_data), 
						    GFP_KERNEL)) == NULL) {
			printk(KERN_WARNING USB_STORAGE "Out of memory\n");
			up(&us_list_semaphore);
			return NULL;
		}
		memset(ss, 0, sizeof(struct us_data));

		/* Initialize the mutexes only when the struct is new */
		init_MUTEX_LOCKED(&(ss->sleeper));
		init_MUTEX_LOCKED(&(ss->notify));
		init_MUTEX(&(ss->queue_exclusion));
		init_MUTEX(&(ss->dev_semaphore));

		/* copy over the subclass and protocol data */
		ss->subclass = subclass;
		ss->protocol = protocol;
		ss->flags = flags;

		/* copy over the endpoint data */
		ss->ep_in = ep_in;
		ss->ep_out = ep_out;
		ss->ep_int = ep_int;
		ss->ep_interval = ep_interval;

		/* establish the connection to the new device */
		ss->ifnum = ifnum;
		ss->pusb_dev = dev;

		/* copy over the identifiying strings */
		strncpy(ss->vendor, mf, USB_STOR_STRING_LEN);
		strncpy(ss->product, prod, USB_STOR_STRING_LEN);
		strncpy(ss->serial, serial, USB_STOR_STRING_LEN);
		if (strlen(ss->vendor) == 0)
			strncpy(ss->vendor, "Unknown", USB_STOR_STRING_LEN);
		if (strlen(ss->product) == 0)
			strncpy(ss->product, "Unknown", USB_STOR_STRING_LEN);
		if (strlen(ss->serial) == 0)
			strncpy(ss->serial, "None", USB_STOR_STRING_LEN);

		/* copy the GUID we created before */
		memcpy(ss->guid, guid, sizeof(guid));
		
		/* 
		 * Set the handler pointers based on the protocol
		 * Again, this data is persistant across reattachments
		 */
		switch (ss->protocol) {
		case US_PR_CB:
			ss->transport_name = "Control/Bulk";
			ss->transport = CB_transport;
			ss->transport_reset = CB_reset;
			break;
			
		case US_PR_CBI:
			ss->transport_name = "Control/Bulk/Interrupt";
			ss->transport = CBI_transport;
			ss->transport_reset = CB_reset;
			break;
			
		case US_PR_BULK:
			ss->transport_name = "Bulk";
			ss->transport = Bulk_transport;
			ss->transport_reset = Bulk_reset;
			break;
			
		default:
			ss->transport_name = "Unknown";
			up(&us_list_semaphore);
			kfree(ss);
			return NULL;
			break;
		}
		US_DEBUGP("Transport: %s\n", ss->transport_name);

		switch (ss->subclass) {
		case US_SC_RBC:
			ss->protocol_name = "Reduced Block Commands (RBC)";
			ss->proto_handler = transparent_scsi_command;
			break;

		case US_SC_8020:
			ss->protocol_name = "8020i";
			ss->proto_handler = ATAPI_command;
			break;

		case US_SC_QIC:
			ss->protocol_name = "QIC-157";
			US_DEBUGP("Sorry, device not supported.  Please\n");
			US_DEBUGP("contact mdharm-usb@one-eyed-alien.net\n");
			US_DEBUGP("if you see this message.\n");
			up(&us_list_semaphore);
			kfree(ss);
			return NULL;
			break;

		case US_SC_8070:
			ss->protocol_name = "8070i";
			ss->proto_handler = ATAPI_command;
			break;

		case US_SC_SCSI:
			ss->protocol_name = "Transparent SCSI";
			ss->proto_handler = transparent_scsi_command;
			break;

		case US_SC_UFI:
			ss->protocol_name = "Uniform Floppy Interface (UFI)";
			ss->proto_handler = ufi_command;
			break;

		default:
			ss->protocol_name = "Unknown";
			up(&us_list_semaphore);
			kfree(ss);
			return NULL;
			break;
		}
		US_DEBUGP("Protocol: %s\n", ss->protocol_name);

		if (ss->protocol == US_PR_CBI) {
			/* set up so we'll wait for notification */
			init_MUTEX_LOCKED(&(ss->ip_waitq));
			
			/* set up the IRQ pipe and handler */
			US_DEBUGP("Allocating IRQ for CBI transport\n");
			ss->irqpipe = usb_rcvintpipe(ss->pusb_dev, ss->ep_int);
			result = usb_request_irq(ss->pusb_dev, ss->irqpipe, 
						 CBI_irq, ss->ep_interval,
						 (void *)ss,
						 &(ss->irq_handle));
			US_DEBUGP("-- usb_request_irq returned %d\n", result);
		}
		
		/*
		 * Since this is a new device, we need to generate a scsi 
		 * host definition, and register with the higher SCSI layers
		 */

		/* Initialize the host template based on the default one */
		memcpy(&(ss->htmplt), &my_host_template, 
		       sizeof(my_host_template));

		/* Grab the next host number */
		ss->host_number = my_host_number++;
			
		/* We abuse this pointer so we can pass the ss pointer to 
		 * the host controler thread in us_detect.  But how else are
		 * we to do it?
		 */
		(struct us_data *)ss->htmplt.proc_dir = ss; 
		
		/* start up our thread */
		ss->pid = kernel_thread(usb_stor_control_thread, ss,
					CLONE_FS | CLONE_FILES |
					CLONE_SIGHAND);
		if (ss->pid < 0) {
			printk(KERN_WARNING USB_STORAGE 
			       "Unable to start control thread\n");
			kfree(ss);
			return NULL;
		}
		
		/* wait for it to start */
		down(&(ss->notify));
			
		/* now register - our detect function will be called */
		ss->htmplt.module = THIS_MODULE;
		scsi_register_module(MODULE_SCSI_HA, &(ss->htmplt));
		
		/* put us in the list */
		ss->next = us_list;
		us_list = ss;
	}

	/* release the data structure lock */
	up(&us_list_semaphore);

	printk(KERN_DEBUG 
	       "WARNING: USB Mass Storage data integrity not assured\n");
	printk(KERN_DEBUG 
	       "USB Mass Storage device found at %d\n", dev->devnum);

	/* return a pointer for the disconnect function */
	return ss;
}

/* Handle a disconnect event from the USB core */
static void storage_disconnect(struct usb_device *dev, void *ptr)
{
	struct us_data *ss = ptr;
	int result;

	US_DEBUGP("storage_disconnect() called\n");
 
	/* this is the odd case -- we disconnected but weren't using it */
	if (!ss) {
		US_DEBUGP("-- device was not in use\n");
		return;
	}

	/* lock access to the device data structure */
	down(&(ss->dev_semaphore));

	/* release the IRQ, if we have one */
	if (ss->irq_handle) {
		US_DEBUGP("-- releasing irq handle\n");
		result = usb_release_irq(ss->pusb_dev, ss->irq_handle, 
					 ss->irqpipe);
		US_DEBUGP("-- usb_release_irq() returned %d\n", result);
		ss->irq_handle = NULL;
	}

	/* mark the device as gone */
	ss->pusb_dev = NULL;

	/* lock access to the device data structure */
	up(&(ss->dev_semaphore));
}


/***********************************************************************
 * Initialization and registration
 ***********************************************************************/

int __init usb_stor_init(void)
{
	/* initialize internal global data elements */
	us_list = NULL;
	init_MUTEX(&us_list_semaphore);
	my_host_number = 0;

	/* register the driver, return -1 if error */
	if (usb_register(&storage_driver) < 0)
		return -1;

	/* we're all set */
	printk(KERN_INFO "USB Mass Storage support registered.\n");
	return 0;
}

void __exit usb_stor_exit(void)
{
	struct us_data *next;
	
	US_DEBUGP("usb_stor_exit() called\n");

	/* Deregister the driver
	 * This eliminates races with probes and disconnects 
	 */
	US_DEBUGP("-- calling usb_deregister()\n");
	usb_deregister(&storage_driver) ;
	
	/* lock access to the data structures */
	down(&us_list_semaphore);

	/* While there are still virtual hosts, unregister them
	 *
	 * Note that the us_release() routine will destroy the local data
	 * structure.  So we have to peel these off the top of the list
	 * and keep updating the head pointer as we go.
	 */
	while (us_list) {
		/* keep track of where the next one is */
		next = us_list->next;

		US_DEBUGP("-- calling scsi_unregister_module()\n");
		scsi_unregister_module(MODULE_SCSI_HA, &(us_list->htmplt));

		/* advance the list pointer */
		us_list = next;
	}
	
	/* unlock the data structures */
	up(&us_list_semaphore);
}

module_init(usb_stor_init) ;
module_exit(usb_stor_exit) ;
