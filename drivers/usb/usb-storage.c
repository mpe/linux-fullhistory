/* Driver for USB Mass Storage compliant devices
 *
 * (c) 1999 Michael Gee (michael@linuxspecific.com)
 * (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Further reference:
 *	This driver is based on the 'USB Mass Storage Class' document. This
 *	describes in detail the protocol used to communicate with such
 *      devices.  Clearly, the designers had SCSI and ATAPI commands in mind 
 *      when they created this document.  The commands are all very similar 
 *      to commands in the SCSI-II and ATAPI specifications.
 *
 *	It is important to note that in a number of cases this class exhibits
 *	class-specific exemptions from the USB specification. Notably the
 *	usage of NAK, STALL and ACK differs from the norm, in that they are
 *	used to communicate wait, failed and OK on commands.
 *	Also, for certain devices, the interrupt endpoint is used to convey
 *	status of a command.
 *
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
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/usb.h>

#include <linux/blk.h>
#include "../scsi/scsi.h"
#include "../scsi/hosts.h"
#include "../scsi/sd.h"

#include "usb-storage.h"
#include "usb-storage-debug.h"


/*
 * This is the size of the structure Scsi_Host_Template.  We create
 * an instance of this structure in this file and this is a check
 * to see if this structure may have changed within the SCSI module.
 * This is by no means foolproof, but it does help us some.
 */
#define SCSI_HOST_TEMPLATE_SIZE			(104)

/* direction table -- this indicates the direction of the data
 * transfer for each command code -- a 1 indicates input
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

struct us_data {
	struct us_data	        *next;	         /* next device */
	struct usb_device	*pusb_dev;       /* this usb_device */
	unsigned int		flags;		 /* from filter initially */
	__u8			ifnum;		 /* interface number */
	__u8			ep_in;		 /* in endpoint */
	__u8			ep_out;		 /* out ....... */
	__u8			ep_int;		 /* interrupt . */
	__u8			subclass;	 /* as in overview */
	__u8			protocol;	 /* .............. */
	__u8			attention_done;  /* force attn on first cmd */
	trans_cmnd              transport;	 /* protocol specific do cmd */
	trans_reset             transport_reset; /* .......... device reset */
	proto_cmnd              proto_handler;   /* protocol handler */
	GUID(guid);				 /* unique dev id */
	struct Scsi_Host	*host;		 /* our dummy host data */
	Scsi_Host_Template	*htmplt;	 /* own host template */
	int			host_number;	 /* to find us */
	int			host_no;	 /* allocated by scsi */
	Scsi_Cmnd		*srb;		 /* current srb */
	Scsi_Cmnd		*queue_srb;	 /* the single queue slot */
	int			action;		 /* what to do */
	struct semaphore	ip_waitq;	 /* for CBI interrupts */
	__u16			ip_data;	 /* interrupt data */
	int			ip_wanted;	 /* needed */
	int			pid;		 /* control thread */
	struct semaphore	*notify;	 /* wait for thread to begin */
	void			*irq_handle;	 /* for USB int requests */
	unsigned int		irqpipe;	 /* pipe for release_irq */
	struct semaphore        sleeper;         /* to sleep on */
	struct semaphore        queue_exclusion; /* to protect data structs */
};

/*
 * kernel thread actions
 */

#define US_ACT_COMMAND		1
#define US_ACT_ABORT		2
#define US_ACT_DEVICE_RESET	3
#define US_ACT_BUS_RESET	4
#define US_ACT_HOST_RESET	5

static struct us_data *us_list;

static void * storage_probe(struct usb_device *dev, unsigned int ifnum);
static void storage_disconnect(struct usb_device *dev, void *ptr);
static struct usb_driver storage_driver = {
	"usb-storage",
	storage_probe,
	storage_disconnect,
	{ NULL, NULL }
};

/***********************************************************************
 * Data transfer routines
 ***********************************************************************/

/* FIXME: the names of these functions are poorly choosen. */

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
static int us_bulk_transfer(struct us_data *us, int pipe, 
			    char *buf, int length)
{
	int result;
	int partial;

	/* transfer the data */
	US_DEBUGP("Bulk xfer 0x%x(%d)\n", (unsigned int)buf, length);
	result = usb_bulk_msg(us->pusb_dev, pipe, buf, length, &partial, HZ*5);
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
			US_DEBUGP("us_bulk_transfer: device NAKed\n");
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
 * Note that this uses us_bulk_transfer to achive it's goals -- this
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
			result = us_bulk_transfer(us, pipe, sg[i].address, 
						  sg[i].length);
			if (result)
				break;
		}
	}
	else
		/* no scatter-gather, just make the request */
		result = us_bulk_transfer(us, pipe, srb->request_buffer, 
					  srb->request_bufflen);

	/* return the result in the data structure itself */
	srb->result = result;
}

/* calculate the length of the data transfer (not the command) for any
 * given SCSI command
 */
static unsigned int us_transfer_length(Scsi_Cmnd *srb)
{
	int i;
	unsigned int total = 0;

	/* always zero for some commands */
	switch (srb->cmnd[0]) {
	case SEEK_6:
	case SEEK_10:
	case REZERO_UNIT:
	case ALLOW_MEDIUM_REMOVAL:
	case START_STOP:
	case TEST_UNIT_READY:
		return 0;

	case REQUEST_SENSE:
	case INQUIRY:
	case MODE_SENSE:
		return srb->cmnd[4];

	case READ_CAPACITY:
		return 8;

	case LOG_SENSE:
	case MODE_SENSE_10:
		return (srb->cmnd[7] << 8) + srb->cmnd[8];

	default:
		break;
	}

	if (srb->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) srb->request_buffer;
		for (i = 0; i < srb->use_sg; i++) {
			total += sg[i].length;
		}
		return total;
	}
	else
		return srb->request_bufflen;
}

/***********************************************************************
 * Protocol routines
 ***********************************************************************/

static void ATAPI_command(Scsi_Cmnd *srb, struct us_data *us)
{
	int old_cmnd = 0;
	int result;
  
	/* Fix some commands -- this is a form of mode translation
	 * ATAPI devices only accept 12 byte long commands 
	 *
	 * NOTE: This only works because a Scsi_Cmnd struct field contains
	 * a unsigned char cmnd[12], so we know we have storage available
	 */

	/* set command length to 12 bytes */
	srb->cmd_len = 12;

	/* determine the correct (or minimum) data length for these commands */
	switch (us->srb->cmnd[0]) {

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
	result = us->transport(srb, us);

	/* If we got a short transfer, but it was for a command that
	 * can have short transfers, we're actually okay
	 */
	if ((us->srb->result == US_BULK_TRANSFER_SHORT) &&
	    ((us->srb->cmnd[0] == REQUEST_SENSE) ||
	     (us->srb->cmnd[0] == INQUIRY) ||
	     (us->srb->cmnd[0] == MODE_SENSE) ||
	     (us->srb->cmnd[0] == LOG_SENSE) ||
	     (us->srb->cmnd[0] == MODE_SENSE_10))) {
		us->srb->result = DID_OK;
	}

	/*
	 * If we have an error, we're going to do a 
	 * REQUEST_SENSE automatically
	 */
	if (result != USB_STOR_TRANSPORT_GOOD) {
		int temp_result;
		void* old_request_buffer;
		int old_sg;

		US_DEBUGP("Command FAILED: Issuing auto-REQUEST_SENSE\n");

		us->srb->cmnd[0] = REQUEST_SENSE;
		us->srb->cmnd[1] = 0;
		us->srb->cmnd[2] = 0;
		us->srb->cmnd[3] = 0;
		us->srb->cmnd[4] = 18;
		us->srb->cmnd[5] = 0;
    
		/* set the buffer length for transfer */
		old_request_buffer = us->srb->request_buffer;
	        old_sg = us->srb->use_sg;
		us->srb->request_bufflen = 18;
		us->srb->request_buffer = us->srb->sense_buffer;

		/* FIXME: what if this command fails? */
		temp_result = us->transport(us->srb, us);
		US_DEBUGP("-- Result from auto-sense is %d\n", temp_result);
		US_DEBUGP("-- sense key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  us->srb->sense_buffer[2] & 0xf,
			  us->srb->sense_buffer[12], 
			  us->srb->sense_buffer[13]);

		/* set the result so the higher layers expect this data */
		us->srb->result = CHECK_CONDITION;

		/* we're done here */
		us->srb->request_buffer = old_request_buffer;
		us->srb->use_sg = old_sg;
		return;
	}
  
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
	if (us->srb->cmnd[0] == INQUIRY) {
		((unsigned char *)us->srb->request_buffer)[2] |= 0x2;
	}
}


static void ufi_command(Scsi_Cmnd *srb, struct us_data *us)
{
	int old_cmnd = 0;
	int result;
  
	/* fix some commands -- this is a form of mode translation
	 * UFI devices only accept 12 byte long commands 
	 *
	 * NOTE: This only works because a Scsi_Cmnd struct field contains
	 * a unsigned char cmnd[12], so we know we have storage available
	 */

	/* set command length to 12 bytes (this affects the transport layer) */
	srb->cmd_len = 12;

	/* determine the correct (or minimum) data length for these commands */
	switch (us->srb->cmnd[0]) {

		/* for INQUIRY, UFI devices only ever return 36 bytes */
	case INQUIRY:
		us->srb->cmnd[4] = 36;
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
		us->srb->cmnd[7] = 0;
		us->srb->cmnd[8] = 8;
		break;
 
		/* for REQUEST_SENSE, UFI devices only ever return 18 bytes */
	case REQUEST_SENSE:
		us->srb->cmnd[4] = 18;
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
	result = us->transport(srb, us);

	/* If we got a short transfer, but it was for a command that
	 * can have short transfers, we're actually okay
	 */
	if ((us->srb->result == US_BULK_TRANSFER_SHORT) &&
	    ((us->srb->cmnd[0] == REQUEST_SENSE) ||
	     (us->srb->cmnd[0] == INQUIRY) ||
	     (us->srb->cmnd[0] == MODE_SENSE) ||
	     (us->srb->cmnd[0] == LOG_SENSE) ||
	     (us->srb->cmnd[0] == MODE_SENSE_10))) {
		us->srb->result = DID_OK;
	}

	/*
	 * If we have an error, we're going to do a 
	 * REQUEST_SENSE automatically
	 */
	if (result != USB_STOR_TRANSPORT_GOOD) {
		int temp_result;
		void* old_request_buffer;
		int old_sg;

		US_DEBUGP("Command FAILED: Issuing auto-REQUEST_SENSE\n");

		us->srb->cmnd[0] = REQUEST_SENSE;
		us->srb->cmnd[1] = 0;
		us->srb->cmnd[2] = 0;
		us->srb->cmnd[3] = 0;
		us->srb->cmnd[4] = 18;
		us->srb->cmnd[5] = 0;
    
		/* set the buffer length for transfer */
		old_request_buffer = us->srb->request_buffer;
	        old_sg = us->srb->use_sg;
		us->srb->request_bufflen = 18;
		us->srb->request_buffer = us->srb->sense_buffer;

		/* FIXME: what if this command fails? */
		temp_result = us->transport(us->srb, us);
		US_DEBUGP("-- Result from auto-sense is %d\n", temp_result);
		US_DEBUGP("-- sense key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  us->srb->sense_buffer[2] & 0xf,
			  us->srb->sense_buffer[12], 
			  us->srb->sense_buffer[13]);

		/* set the result so the higher layers expect this data */
		us->srb->result = CHECK_CONDITION;

		/* we're done here */
		us->srb->request_buffer = old_request_buffer;
		us->srb->use_sg = old_sg;
		return;
	}
  
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
	if (us->srb->cmnd[0] == INQUIRY) {
		((unsigned char *)us->srb->request_buffer)[2] |= 0x2;
	}
}

static void transparent_scsi_command(Scsi_Cmnd *srb, struct us_data *us)
{
	unsigned int result = 0;

	/* This code supports devices which do not support {READ|WRITE}_6
	 * Apparently, neither Windows or MacOS will use these commands,
	 * so some devices do not support them
	 */
	if (us->flags & US_FL_MODE_XLATE) {
    
		/* translate READ_6 to READ_10 */
		if (us->srb->cmnd[0] == 0x08) {
      
			/* get the control */
			us->srb->cmnd[9] = us->srb->cmnd[5];
      
			/* get the length */
			us->srb->cmnd[8] = us->srb->cmnd[6];
			us->srb->cmnd[7] = 0;
      
			/* set the reserved area to 0 */
			us->srb->cmnd[6] = 0;	    
      
			/* get LBA */
			us->srb->cmnd[5] = us->srb->cmnd[3];
			us->srb->cmnd[4] = us->srb->cmnd[2];
			us->srb->cmnd[3] = 0;
			us->srb->cmnd[2] = 0;
      
			/* LUN and other info in cmnd[1] can stay */
      
			/* fix command code */
			us->srb->cmnd[0] = 0x28;
      
			US_DEBUGP("Changing READ_6 to READ_10\n");
			US_DEBUG(us_show_command(us->srb));
		}
    
		/* translate WRITE_6 to WRITE_10 */
		if (us->srb->cmnd[0] == 0x0A) {
      
			/* get the control */
			us->srb->cmnd[9] = us->srb->cmnd[5];
      
			/* get the length */
			us->srb->cmnd[8] = us->srb->cmnd[4];
			us->srb->cmnd[7] = 0;
      
			/* set the reserved area to 0 */
			us->srb->cmnd[6] = 0;	    
      
			/* get LBA */
			us->srb->cmnd[5] = us->srb->cmnd[3];
			us->srb->cmnd[4] = us->srb->cmnd[2];
			us->srb->cmnd[3] = 0;
			us->srb->cmnd[2] = 0;
	    
			/* LUN and other info in cmnd[1] can stay */
      
			/* fix command code */
			us->srb->cmnd[0] = 0x2A;

			US_DEBUGP("Changing WRITE_6 to WRITE_10\n");
			US_DEBUG(us_show_command(us->srb));
		}
	} /* if (us->flags & US_FL_MODE_XLATE) */
  
	/* send the command to the transport layer */
	result = us->transport(us->srb, us);

	/* If we got a short transfer, but it was for a command that
	 * can have short transfers, we're actually okay
	 */
	if ((us->srb->result == US_BULK_TRANSFER_SHORT) &&
	    ((us->srb->cmnd[0] == REQUEST_SENSE) ||
	     (us->srb->cmnd[0] == INQUIRY) ||
	     (us->srb->cmnd[0] == MODE_SENSE) ||
	     (us->srb->cmnd[0] == LOG_SENSE) ||
	     (us->srb->cmnd[0] == MODE_SENSE_10))) {
		us->srb->result = DID_OK;
	}

	/* if we have an error, we're going to do a REQUEST_SENSE 
	 * automatically */
	if (result != USB_STOR_TRANSPORT_GOOD) {
		int temp_result;
		int old_sg;
		void* old_request_buffer;

		US_DEBUGP("Command FAILED: Issuing auto-REQUEST_SENSE\n");

		/* set up the REQUEST_SENSE command and parameters */
		us->srb->cmnd[0] = REQUEST_SENSE;
		us->srb->cmnd[1] = 0;
		us->srb->cmnd[2] = 0;
		us->srb->cmnd[3] = 0;
		us->srb->cmnd[4] = 18;
		us->srb->cmnd[5] = 0;
    
		/* set the buffer length for transfer */
		old_request_buffer = us->srb->request_buffer;
	        old_sg = us->srb->use_sg;
		us->srb->request_bufflen = 18;
		us->srb->request_buffer = us->srb->sense_buffer;

		/* FIXME: what if this command fails? */
		temp_result = us->transport(us->srb, us);
		US_DEBUGP("-- Result from auto-sense is %d\n", temp_result);
		US_DEBUGP("-- sense key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  us->srb->sense_buffer[2] & 0xf,
			  us->srb->sense_buffer[12], 
			  us->srb->sense_buffer[13]);

		/* set the result so the higher layers expect this data */
		us->srb->result = CHECK_CONDITION;

		/* we're done here */
		us->srb->use_sg = old_sg;
		us->srb->request_buffer = old_request_buffer;
		return;
	}

	/* fix the results of an INQUIRY */
	if (us->srb->cmnd[0] == INQUIRY) {
		US_DEBUGP("Fixing INQUIRY data, setting SCSI rev to 2\n");
		((unsigned char*)us->srb->request_buffer)[2] |= 2;
	}
}

/***********************************************************************
 * Transport routines
 ***********************************************************************/

static int CBI_irq(int state, void *buffer, int len, void *dev_id)
{
	struct us_data *us = (struct us_data *)dev_id;

	US_DEBUGP("USB IRQ recieved for device on host %d\n", us->host_no);

	/* save the data for interpretation later */
	if (state != USB_ST_REMOVED) {
		us->ip_data = le16_to_cpup((__u16 *)buffer);
		US_DEBUGP("Interrupt Status 0x%x\n", us->ip_data);
	}
  
	/* was this a wanted interrupt? */
	if (us->ip_wanted) {
		us->ip_wanted = 0;
		up(&(us->ip_waitq));
	} else {
		US_DEBUGP("ERROR: Unwanted interrupt received!\n");
	}

	/* This return code is truly meaningless -- and I mean truly.  It gets
	 * ignored by other layers.  It used to indicate if we wanted to get
	 * another interrupt or disable the interrupt callback
	 */
	return 0;
}

/* This issues a CB[I] Reset to the device in question
 */
static int CB_reset(struct us_data *us)
{
	unsigned char cmd[12];
	int result;

	US_DEBUGP("CB_reset\n");

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

/*
 * Control/Bulk/Interrupt transport
 */
static int CBI_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;

	US_DEBUGP("CBI gets a command:\n");
	US_DEBUG(us_show_command(srb));

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
			US_DEBUGP("-- Return from usb_clear_halt() is %d\n",
				  usb_clear_halt(us->pusb_dev, 
						 usb_sndctrlpipe(us->pusb_dev,
								 0)));
			return USB_STOR_TRANSPORT_ERROR;
		}

				/* FIXME: we need to handle NAKs here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* Set up for status notification */
	us->ip_wanted = 1;

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (us_transfer_length(srb)) {
		us_transfer(srb, US_DIRECTION(srb->cmnd[0]));
		US_DEBUGP("CBI data stage result is 0x%x\n", result);
	}

	/* STATUS STAGE */

	/* go to sleep until we get this interrup */
	/* FIXME: this should be changed to use a timeout */
	down(&(us->ip_waitq));
	
	/* FIXME: currently this code is unreachable, but the idea is
	 * necessary.  See above comment.
	 */
	if (us->ip_wanted) {
		US_DEBUGP("Did not get interrupt on CBI\n");
		us->ip_wanted = 0;
		return USB_STOR_TRANSPORT_ERROR;
	}
	
	US_DEBUGP("Got interrupt data 0x%x\n", us->ip_data);
	
	/* UFI gives us ASC and ASCQ, like a request sense */
	/* FIXME: is this right?  Do REQUEST_SENSE and INQUIRY need special
	 * case handling?
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
	
	/* otherwise, we interpret the data normally */
	switch (us->ip_data) {
	case 0x0001: 
		return USB_STOR_TRANSPORT_GOOD;
	case 0x0002: 
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
	__u8 status[2];

	US_DEBUGP("CBC gets a command:\n");
	US_DEBUG(us_show_command(srb));

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
			US_DEBUGP("-- Return from usb_clear_halt() is %d\n",
				  usb_clear_halt(us->pusb_dev, 
						 usb_sndctrlpipe(us->pusb_dev,
								 0)));
			return USB_STOR_TRANSPORT_ERROR;
		}

				/* FIXME: we need to handle NAKs here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (us_transfer_length(srb)) {
		us_transfer(srb, US_DIRECTION(srb->cmnd[0]));
		US_DEBUGP("CBC data stage result is 0x%x\n", result);
	}
	
	
	/* STATUS STAGE */
	/* FIXME: this is wrong */
	result = usb_control_msg(us->pusb_dev, 
				 usb_rcvctrlpipe(us->pusb_dev,0),
				 USB_REQ_GET_STATUS, USB_DIR_IN |
				 USB_TYPE_STANDARD | USB_RECIP_DEVICE,
				 0, us->ifnum, status, sizeof(status), HZ*5);

	if (result < 0) {
		US_DEBUGP("CBC Status stage returns %d\n", result);
		return USB_STOR_TRANSPORT_ERROR;
	}

	US_DEBUGP("Got CB status 0x%x 0x%x\n", status[0], status[1]);
	if (srb->cmnd[0] != REQUEST_SENSE && srb->cmnd[0] != INQUIRY &&
	    ( (status[0] & ~3) || status[1]))
		return USB_STOR_TRANSPORT_FAILED;
	else
		return USB_STOR_TRANSPORT_GOOD;
	
	US_DEBUGP("CB_transport() reached end of function\n");
	return USB_STOR_TRANSPORT_ERROR;
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
	bcb.DataTransferLength = us_transfer_length(srb);
	bcb.Flags = US_DIRECTION(srb->cmnd[0]) << 7;
	bcb.Tag = srb->serial_number;
	bcb.Lun = 0;
	bcb.Length = srb->cmd_len;
	
	/* construct the pipe handle */
	pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);
	
	/* copy the command payload */
	memset(bcb.CDB, 0, sizeof(bcb.CDB));
	memcpy(bcb.CDB, srb->cmnd, bcb.Length);
	
	/* send it to out endpoint */
	US_DEBUGP("Bulk command S 0x%x T 0x%x L %d F %d CL %d\n",
		  bcb.Signature, bcb.Tag, bcb.DataTransferLength,
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
	result = usb_bulk_msg(us->pusb_dev, pipe, &bcs,
			      US_BULK_CS_WRAP_LEN, &partial, HZ*5);
	
	/* did the attempt to read the CSW fail? */
	if (result == -EPIPE) {
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		usb_clear_halt(us->pusb_dev, pipe);
		
		/* get the status again */
		result = usb_bulk_msg(us->pusb_dev, pipe, &bcs,
				      US_BULK_CS_WRAP_LEN, &partial, HZ*5);
		
		/* if it fails again, we need a reset and return an error*/
		if (result == -EPIPE) {
			Bulk_reset(us);
			return USB_STOR_TRANSPORT_ERROR;
		}
	}
	
	/* if we still have a failure at this point, we're in trouble */
	if (result) {
		US_DEBUGP("Bulk status result = %d\n", result);
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
		Bulk_reset(us);
		return USB_STOR_TRANSPORT_ERROR;
	}
	
	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/***********************************************************************
 * Host functions 
 ***********************************************************************/

/* detect adapter (always true ) */
static int us_detect(struct SHT *sht)
{
	/* FIXME - not nice at all, but how else ? */
	struct us_data *us = (struct us_data *)sht->proc_dir;
	char name[32];

	/* set up our name */
	sprintf(name, "usbscsi%d", us->host_number);
	sht->name = sht->proc_name = kmalloc(strlen(name)+1, GFP_KERNEL);
	if (!sht->proc_name)
		return 0;
	strcpy(sht->proc_name, name);

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
	sht->name = NULL;
	return 0;
}

/* release - must be here to stop scsi
 *	from trying to release IRQ etc.
 *	Kill off our data
 */
static int us_release(struct Scsi_Host *psh)
{
	struct us_data *us = (struct us_data *)psh->hostdata[0];
	struct us_data *prev = (struct us_data *)&us_list;

	if (us->irq_handle) {
		usb_release_irq(us->pusb_dev, us->irq_handle, us->irqpipe);
		us->irq_handle = NULL;
	}

	/* FIXME: release the interface claim here? */
	//	if (us->pusb_dev)
	//		usb_deregister(&storage_driver);

	/* FIXME - leaves hanging host template copy */
	/* (because scsi layer uses it after removal !!!) */
	if (us_list == us)
		us_list = us->next;
	else {
		while (prev->next != us)
			prev = prev->next;
		prev->next = us->next;
	}
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

	US_DEBUGP("Command wakeup\n");
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

/* FIXME: This doesn't actually abort anything */
static int us_abort( Scsi_Cmnd *srb )
{
	return 0;
}

/* FIXME: this doesn't do anything right now */
static int us_bus_reset( Scsi_Cmnd *srb )
{
	//  struct us_data *us = (struct us_data *)srb->host->hostdata[0];

	US_DEBUGP("Bus reset requested\n");
	//  us->transport_reset(us);
	return SUCCESS;
}

/* FIXME: This doesn't actually reset anything */
static int us_host_reset( Scsi_Cmnd *srb )
{
	return 0;
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

/* we use this macro to help us write into the buffer */
#undef SPRINTF
#define SPRINTF(args...) do { if (pos < (buffer + length)) pos += sprintf (pos, ## args); } while (0)

int usb_stor_proc_info (char *buffer, char **start, off_t offset, 
			int length, int hostno, int inout)
{
	struct us_data *us = us_list;
	char *pos = buffer;
	char *tmp_ptr;

	/* find our data from hostno */
	while (us) {
		if (us->host_no == hostno)
			break;
		us = us->next;
	}

	/* if we couldn't find it, we return an error */
	if (!us)
		return -ESRCH;

	/* if someone is sending us data, just throw it away */
	if (inout)
		return length;

	/* print the controler name */
	SPRINTF ("Host scsi%d: usb-storage\n", hostno);

	/* print product and vendor strings */
	tmp_ptr = kmalloc(256, GFP_KERNEL);
	if (!us->pusb_dev || !tmp_ptr) {
		SPRINTF("    Vendor: Unknown Vendor\n");
		SPRINTF("   Product: Unknown Product\n");
	} else {
		SPRINTF("    Vendor: ");
		if (usb_string(us->pusb_dev, us->pusb_dev->descriptor.iManufacturer, tmp_ptr, 256) > 0)
			SPRINTF("%s\n", tmp_ptr);
		else
			SPRINTF("Unknown Vendor\n");
    
		SPRINTF("   Product: ");
		if (usb_string(us->pusb_dev, us->pusb_dev->descriptor.iProduct, tmp_ptr, 256) > 0)
			SPRINTF("%s\n", tmp_ptr);
		else
			SPRINTF("Unknown Product\n");
		kfree(tmp_ptr);
	}

	SPRINTF("  Protocol: ");
	switch (us->protocol) {
	case US_PR_CB:
		SPRINTF("Control/Bulk\n");
		break;
    
	case US_PR_CBI:
		SPRINTF("Control/Bulk/Interrupt\n");
		break;
    
	case US_PR_BULK:
		SPRINTF("Bulk only\n");
		break;
    
	default:
		SPRINTF("Unknown Protocol\n");
		break;
	}

	/* show the GUID of the device */
	SPRINTF("      GUID: " GUID_FORMAT "\n", GUID_ARGS(us->guid));

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
	NULL,			    /* next */
	NULL,			    /* module */
	NULL,			    /* proc_dir */
	usb_stor_proc_info,
	NULL,			    /* name - points to unique */
	us_detect,
	us_release,
	NULL,			    /* info */
	NULL,			    /* ioctl */
	us_command,
	us_queuecommand,
	NULL,			    /* eh_strategy */
	us_abort,
	us_bus_reset,
	us_bus_reset,
	us_host_reset,
	NULL,			    /* abort */
	NULL,			    /* reset */
	NULL,			    /* slave_attach */
	NULL,			    /* bios_param */
	NULL,			    /* select_queue_depths */
	1,			    /* can_queue */
	-1,			    /* this_id */
	SG_ALL, 		    /* sg_tablesize */
	1,			    /* cmd_per_lun */
	0,			    /* present */
	FALSE,		            /* unchecked_isa_dma */
	TRUE,  		            /* use_clustering */
	TRUE,			    /* use_new_eh_code */
	TRUE			    /* emulated */
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

	sprintf(current->comm, "usbscsi%d", us->host_number);

	unlock_kernel();

	up(us->notify);

	for(;;) {
		siginfo_t info;
		int unsigned long signr;

		US_DEBUGP("*** thread sleeping.\n");
		down(&(us->sleeper));
		down(&(us->queue_exclusion));
		US_DEBUGP("*** thread awakened.\n");

				/* take the command off the queue */
		action = us->action;
		us->action = 0;
		us->srb = us-> queue_srb;
     	
		/* release the queue lock as fast as possible */
		up(&(us->queue_exclusion));

		/* FIXME: we need to examine placment of break; and 
		 * scsi_done() calls */

		switch (action) {
		case US_ACT_COMMAND:
			/* bad device */
			if (us->srb->target || us->srb->lun) {
				US_DEBUGP( "Bad device number (%d/%d) or dev 0x%x\n",
					   us->srb->target, us->srb->lun, (unsigned int)us->pusb_dev);
				us->srb->result = DID_BAD_TARGET << 16;

				us->srb->scsi_done(us->srb);
				us->srb = NULL;
				break;
			}

			/* our device has gone - pretend not ready */
			/* FIXME: we also need to handle INQUIRY here, 
			 * probably */
			if (!us->pusb_dev) {
				if (us->srb->cmnd[0] == REQUEST_SENSE) {
					memcpy(us->srb->request_buffer, sense_notready, 
					       sizeof(sense_notready));
					us->srb->result = DID_OK << 16;
				} else {
					us->srb->result = (DID_OK << 16) | 2;
				}

				us->srb->scsi_done(us->srb);
				us->srb = NULL;
				break;
			}

			/* we've got a command, let's do it! */
			US_DEBUG(us_show_command(us->srb));

			/* FIXME: this is to support Shuttle E-USB bridges, it 
			 * appears */
			if (us->srb->cmnd[0] == START_STOP &&
			    us->pusb_dev->descriptor.idProduct == 0x0001 &&
			    us->pusb_dev->descriptor.idVendor == 0x04e6)
				us->srb->result = DID_OK << 16;
			else {
				us->proto_handler(us->srb, us);
			}
      
			US_DEBUGP("scsi cmd done, result=0x%x\n", us->srb->result);
			us->srb->scsi_done(us->srb);
			us->srb = NULL;
			break;
      
		case US_ACT_ABORT:
			break;

		case US_ACT_DEVICE_RESET:
			break;

		case US_ACT_BUS_RESET:
			break;

		case US_ACT_HOST_RESET:
			break;

		} /* end switch on action */

		/* FIXME: we ignore TERM and KILL... is this right? */
		if (signal_pending(current)) {
			/* sending SIGUSR1 makes us print out some info */
			spin_lock_irq(&current->sigmask_lock);
			signr = dequeue_signal(&current->blocked, &info);
			spin_unlock_irq(&current->sigmask_lock);
		} /* if (singal_pending(current)) */
	} /* for (;;) */
  
	//  MOD_DEC_USE_COUNT;

	printk("usb_stor_control_thread exiting\n");

	return 0;
}	

/* Probe to see if a new device is actually a SCSI device */
static void * storage_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	int i;
	char mf[32];		     /* manufacturer */
	char prod[32];		     /* product */
	char serial[32];       	     /* serial number */
	struct us_data *ss = NULL;
	unsigned int flags = 0;
	GUID(guid);		     /* Global Unique Identifier */
	struct us_data *prev;
	int protocol = 0;
	int subclass = 0;
	struct usb_interface_descriptor *altsetting = 
		&(dev->actconfig->interface[ifnum].altsetting[0]); 

	/* clear the GUID and fetch the strings */
	GUID_CLEAR(guid);
	memset(mf, 0, sizeof(mf));
	memset(prod, 0, sizeof(prod));
	memset(serial, 0, sizeof(serial));
	if (dev->descriptor.iManufacturer)
		usb_string(dev, dev->descriptor.iManufacturer, mf, sizeof(mf));
	if (dev->descriptor.iProduct)
		usb_string(dev, dev->descriptor.iProduct, prod, sizeof(prod));
	if (dev->descriptor.iSerialNumber)
		usb_string(dev, dev->descriptor.iSerialNumber, serial, sizeof(serial));
	
	/* let's examine the device now */

	/* We make an exception for the shuttle E-USB */
	if (dev->descriptor.idVendor == 0x04e6 &&
	    dev->descriptor.idProduct == 0x0001) {
		protocol = US_PR_CB;
		subclass = US_SC_8070;	    /* an assumption */
	} else if (dev->descriptor.bDeviceClass != 0 ||
		   altsetting->bInterfaceClass != USB_CLASS_MASS_STORAGE ||
		   altsetting->bInterfaceSubClass < US_SC_MIN ||
		   altsetting->bInterfaceSubClass > US_SC_MAX) {
		/* if it's not a mass storage, we go no further */
		return NULL;
	}

	/* At this point, we know we've got a live one */
	US_DEBUGP("USB Mass Storage device detected\n");

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

	/* Now check if we have seen this GUID before, and restore
	 * the flags if we find it
	 */
	for (ss = us_list; ss != NULL; ss = ss->next) {
		if (!ss->pusb_dev && GUID_EQUAL(guid, ss->guid))    {
			US_DEBUGP("Found existing GUID " GUID_FORMAT "\n",
				  GUID_ARGS(guid));
			flags = ss->flags;
			break;
		}
	}

	/* If ss == NULL, then this is a new device.  Allocate memory for it */
	if (!ss) {
		if ((ss = (struct us_data *)kmalloc(sizeof(*ss), 
						    GFP_KERNEL)) == NULL) {
			printk(KERN_WARNING USB_STORAGE "Out of memory\n");
			return NULL;
		}
		memset(ss, 0, sizeof(struct us_data));

		/* Initialize the mutexes only when the struct is new */
		init_MUTEX_LOCKED(&(ss->sleeper));
		init_MUTEX(&(ss->queue_exclusion));
	}

	/* establish the connection to the new device */
	interface = altsetting;
	ss->flags = flags;
	ss->ifnum = ifnum;
	ss->attention_done = 0;
	ss->pusb_dev = dev;

	/* If the device has subclass and protocol, then use that.  Otherwise, 
	 * take data from the specific interface.
	 */
	if (subclass) {
		ss->subclass = subclass;
		ss->protocol = protocol;
	} else {
		ss->subclass = interface->bInterfaceSubClass;
		ss->protocol = interface->bInterfaceProtocol;
	}

	/* set the handler pointers based on the protocol */
	US_DEBUGP("Transport: ");
	switch (ss->protocol) {
	case US_PR_CB:
		US_DEBUGPX("Control/Bulk\n");
		ss->transport = CB_transport;
		ss->transport_reset = CB_reset;
		break;

	case US_PR_CBI:
		US_DEBUGPX("Control/Bulk/Interrupt\n");
		ss->transport = CBI_transport;
		ss->transport_reset = CB_reset;
		break;

	case US_PR_BULK:
		US_DEBUGPX("Bulk\n");
		ss->transport = Bulk_transport;
		ss->transport_reset = Bulk_reset;
		break;

	default:
		US_DEBUGPX("Unknown\n");    
		kfree(ss);
		return NULL;
		break;
	}

	/*
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * An optional interrupt is OK (necessary for CBI protocol).
	 * We will ignore any others.
	 */
	for (i = 0; i < interface->bNumEndpoints; i++) {
		/* is it an BULK endpoint? */
		if ((interface->endpoint[i].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		    == USB_ENDPOINT_XFER_BULK) {
			if (interface->endpoint[i].bEndpointAddress & USB_DIR_IN)
				ss->ep_in = interface->endpoint[i].bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
			else
				ss->ep_out = interface->endpoint[i].bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
		}

		/* is it an interrupt endpoint? */
		if ((interface->endpoint[i].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) 
		    == USB_ENDPOINT_XFER_INT) {
			ss->ep_int = interface->endpoint[i].bEndpointAddress &
				USB_ENDPOINT_NUMBER_MASK;
		}
	}
	US_DEBUGP("Endpoints In %d Out %d Int %d\n",
		  ss->ep_in, ss->ep_out, ss->ep_int);

	/* Do some basic sanity checks, and bail if we find a problem */
	if (usb_set_interface(dev, interface->bInterfaceNumber, 0) ||
	    !ss->ep_in || !ss->ep_out || 
	    (ss->protocol == US_PR_CBI && ss->ep_int == 0)) {
		US_DEBUGP("Problems with device\n");
		if (ss->host) {
			kfree(ss->htmplt->name);
			kfree(ss->htmplt);
		}

		kfree(ss);
		return NULL;
	}

	/* If this is a new device (i.e. we haven't seen it before), we need to
	 * generate a scsi host definition, and register with scsi above us 
	 */
	if (!ss->host) {
		/* copy the GUID we created before */
		US_DEBUGP("New GUID " GUID_FORMAT "\n", GUID_ARGS(guid));
		memcpy(ss->guid, guid, sizeof(guid));

		/* set class specific stuff */
		US_DEBUGP("Protocol: ");
		switch (ss->subclass) {
		case US_SC_RBC:
			US_DEBUGPX("Reduced Block Commands (RBC)\n");
			ss->proto_handler = transparent_scsi_command;
			break;

		case US_SC_8020:
			US_DEBUGPX("8020i\n");
			ss->proto_handler = ATAPI_command;
			break;

		case US_SC_QIC:
			US_DEBUGPX("QIC157\n");
			break;

		case US_SC_8070:
			US_DEBUGPX("8070i\n");
			ss->proto_handler = ATAPI_command;
			break;

		case US_SC_SCSI:
			US_DEBUGPX("Transparent SCSI\n");
			ss->proto_handler = transparent_scsi_command;
			break;

		case US_SC_UFI:
			US_DEBUGPX("UFI\n");
			ss->proto_handler = ufi_command;
			break;

		default:
			US_DEBUGPX("Unknown\n");
			break;
		}

		/* Allocate memory for the SCSI Host Template */
		if ((ss->htmplt = (Scsi_Host_Template *)
		     kmalloc(sizeof(Scsi_Host_Template),GFP_KERNEL))==NULL ) {
			printk(KERN_WARNING USB_STORAGE "Out of memory\n");

			kfree(ss);
			return NULL;
		}

		/* Initialize the host template based on the default one */
		memcpy(ss->htmplt, &my_host_template, sizeof(my_host_template));

		/* Grab the next host number */
		ss->host_number = my_host_number++;

		/* MDD: FIXME: this is bad.  We abuse this pointer so we
		 * can pass the ss pointer to the host controler thread
		 * in us_detect
		 */
		(struct us_data *)ss->htmplt->proc_dir = ss; 

		/* shuttle E-USB */	
		if (dev->descriptor.idVendor == 0x04e6 &&
		    dev->descriptor.idProduct == 0x0001) {
			__u8 qstat[2];
			int result;
			
			result = usb_control_msg(ss->pusb_dev, 
						 usb_rcvctrlpipe(dev,0),
						 1, 0xC0,
						 0, ss->ifnum,
						 qstat, 2, HZ*5);
			US_DEBUGP("C0 status 0x%x 0x%x\n", qstat[0], qstat[1]);
			init_MUTEX_LOCKED(&(ss->ip_waitq));
			ss->irqpipe = usb_rcvintpipe(ss->pusb_dev, ss->ep_int);
			result = usb_request_irq(ss->pusb_dev, ss->irqpipe, 
						 CBI_irq, 255, (void *)ss, 
						 &ss->irq_handle);
			if (result < 0)
				return NULL;
			/* FIXME: what is this?? */
			down(&(ss->ip_waitq));
		} else if (ss->protocol == US_PR_CBI) {
			int result; 
			
			/* set up so we'll wait for notification */
			init_MUTEX_LOCKED(&(ss->ip_waitq));

			/* set up the IRQ pipe and handler */
			/* FIXME: This needs to get the period from the device */
			ss->irqpipe = usb_rcvintpipe(ss->pusb_dev, ss->ep_int);
			result = usb_request_irq(ss->pusb_dev, ss->irqpipe, CBI_irq,
						 255, (void *)ss, &ss->irq_handle);
			if (result) {
				US_DEBUGP("usb_request_irq failed (0x%x), No interrupt for CBI\n",
					  result);
			}
		}
    

				/* start up our thread */
		{
			DECLARE_MUTEX_LOCKED(sem);

			ss->notify = &sem;
			ss->pid = kernel_thread(usb_stor_control_thread, ss,
						CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
			if (ss->pid < 0) {
				printk(KERN_WARNING USB_STORAGE "Unable to start control thread\n");
				kfree(ss->htmplt);

				kfree(ss);
				return NULL;
			}

			/* wait for it to start */
			down(&sem);
		}

		/* now register - our detect function will be called */
		ss->htmplt->module = &__this_module;
		scsi_register_module(MODULE_SCSI_HA, ss->htmplt);

		/* put us in the list */
		ss->next = us_list;
                us_list = ss;
	}

	printk(KERN_DEBUG "WARNING: USB Mass Storage data integrity not assured\n");
	printk(KERN_DEBUG "USB Mass Storage device found at %d\n", dev->devnum);

	return ss;
}

/* Handle a disconnect event from the USB core */
static void storage_disconnect(struct usb_device *dev, void *ptr)
{
	struct us_data *ss = ptr;

	if (!ss)
		return;

	ss->pusb_dev = NULL;
}


/***********************************************************************
 * Initialization and registration
 ***********************************************************************/

int __init usb_stor_init(void)
{
	if (sizeof(my_host_template) != SCSI_HOST_TEMPLATE_SIZE) {
		printk(KERN_ERR "usb-storage: SCSI_HOST_TEMPLATE_SIZE does not match\n") ;
		printk(KERN_ERR "usb-storage: expected %d bytes, got %d bytes\n", 
		       SCSI_HOST_TEMPLATE_SIZE, sizeof(my_host_template)) ;

		return -1 ;
	}

	/* register the driver, return -1 if error */
	if (usb_register(&storage_driver) < 0)
		return -1;

	printk(KERN_INFO "USB Mass Storage support registered.\n");
	return 0;
}

void __exit usb_stor_exit(void)
{
	static struct us_data *ptr;

	// FIXME: this needs to be put back to free _all_ the hosts
	//	for (ptr = us_list; ptr != NULL; ptr = ptr->next)
	//		scsi_unregister_module(MODULE_SCSI_HA, ptr->htmplt);
	printk("MDD: us_list->htmplt is 0x%x\n", (unsigned int)(us_list->htmplt));
	scsi_unregister_module(MODULE_SCSI_HA, us_list->htmplt);

	usb_deregister(&storage_driver) ;
}

module_init(usb_stor_init) ;
module_exit(usb_stor_exit) ;
