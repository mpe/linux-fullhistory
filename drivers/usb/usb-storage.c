/* Driver for USB Mass Storage compliant devices
 *
 * (c) 1999 Michael Gee (michael@linuxspecific.com)
 * (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Further reference:
 *	This driver is based on the 'USB Mass Storage Class' document. This
 *	describes in detail the protocol used to communicate with such
 *      devices.  Clearly, the designers had SCSI commands in mind when they
 *      created this document.  The commands are all similar to commands
 *      in the SCSI-II specification.
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

int usb_stor_debug = 1;

struct us_data;

typedef int (*trans_cmnd)(Scsi_Cmnd*, struct us_data*);
typedef int (*trans_reset)(struct us_data*);
typedef void (*proto_cmnd)(Scsi_Cmnd*, struct us_data*);

struct us_data {
	struct us_data	*next;		         /* next device */
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
	int			action;		 /* what to do */
	wait_queue_head_t	waitq;		 /* thread waits */
	wait_queue_head_t	ip_waitq;	 /* for CBI interrupts */
	__u16			ip_data;	 /* interrupt data */
	int			ip_wanted;	 /* needed */
	int			pid;		 /* control thread */
	struct semaphore	*notify;	 /* wait for thread to begin */
	void			*irq_handle;	 /* for USB int requests */
	unsigned int		irqpipe;	 /* pipe for release_irq */
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

/* Transfer one buffer (breaking into packets if necessary)
 * Note that this function is necessary because if the device NAKs, we
 * need to know that information directly
 *
 * FIXME: is the above true?  Or will the URB status show ETIMEDOUT after
 * retrying several times allready?  Perhaps this is the way we should
 * be going anyway?
 */
static int us_one_transfer(struct us_data *us, int pipe, char *buf, int length)
{
	int max_size;
	int this_xfer;
	int result;
	int partial;
	int maxtry;

	/* determine the maximum packet size for these transfers */
	max_size = usb_maxpacket(us->pusb_dev, 
				 pipe, usb_pipeout(pipe)) * 16;

	/* while we have data left to transfer */
	while (length) {

		/* calculate how long this will be -- maximum or a remainder */
		this_xfer = length > max_size ? max_size : length;
		length -= this_xfer;

		/* FIXME: this number is totally outrageous.  We need to pick
		 * a better (smaller) number).
		 */

		/* setup the retry counter */
		maxtry = 100;

		/* set up the transfer loop */
		do {
			/* transfer the data */
			US_DEBUGP("Bulk xfer 0x%x(%d) try #%d\n", 
				  (unsigned int)buf, this_xfer, 101 - maxtry);
			result = usb_bulk_msg(us->pusb_dev, pipe, buf,
					      this_xfer, &partial, HZ*5);
			US_DEBUGP("bulk_msg returned %d xferred %d/%d\n",
				  result, partial, this_xfer);

			/* if we stall, we need to clear it before we go on */
			if (result == -EPIPE) {
				US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
				usb_clear_halt(us->pusb_dev, pipe);
			}

			/* update to show what data was transferred */
			this_xfer -= partial;
			buf += partial;

			/* NAK - we retry a few times */
			if (result == -ETIMEDOUT) {

				US_DEBUGP("us_one_transfer: device NAKed\n");

				/* if our try counter reaches 0, bail out */
				if (!maxtry--)
					return -ETIMEDOUT;

				/* just continue the while loop */
				continue;
			}
      
			/* other errors (besides NAK) -- we just bail out*/
			if (result != 0) {
				US_DEBUGP("us_one_transfer: device returned error %d\n", result);
				return result;
			}

			/* continue until this transfer is done */
		} while ( this_xfer );
	}

	/* if we get here, we're done and successful */
	return 0;
}

static unsigned int us_transfer_length(Scsi_Cmnd *srb);

/* transfer one SCSI command, using scatter-gather if requested */
/* FIXME: what do the return codes here mean? */
static int us_transfer(Scsi_Cmnd *srb, int dir_in)
{
	struct us_data *us = (struct us_data *)srb->host_scribble;
	int i;
	int result = -1;
	unsigned int pipe = dir_in ? usb_rcvbulkpipe(us->pusb_dev, us->ep_in) :
		usb_sndbulkpipe(us->pusb_dev, us->ep_out);

	/* FIXME: stop transferring data at us_transfer_length(), not 
	 * bufflen */
	if (srb->use_sg) {
		struct scatterlist *sg = (struct scatterlist *) srb->request_buffer;

		for (i = 0; i < srb->use_sg; i++) {
			result = us_one_transfer(us, pipe, sg[i].address, sg[i].length);
			if (result)
				break;
		}
	}
	else
		result = us_one_transfer(us, pipe, srb->request_buffer, 
					 us_transfer_length(srb));

	if (result < 0)
		US_DEBUGP("us_transfer returning error %d\n", result);
	return result;
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

	case LOG_SENSE:
	case MODE_SENSE_10:
		return (srb->cmnd[7] << 8) + srb->cmnd[8];

	default:
		break;
	}

	if (srb->use_sg) {
		struct scatterlist *sg = (struct scatterlist *) srb->request_buffer;

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

static int CB_transport(Scsi_Cmnd *srb, struct us_data *us);
static int Bulk_transport(Scsi_Cmnd *srb, struct us_data *us);

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
	us->srb->result = us->transport(srb, us);

	/* if we have an error, we're going to do a 
	 * REQUEST_SENSE automatically */

	/* FIXME: we should only do this for device 
	 * errors, not system errors */
	if (us->srb->result) {
		int temp_result;
		int count;
		void* old_request_buffer;

		US_DEBUGP("Command FAILED: Issuing auto-REQUEST_SENSE\n");

		/* set the result so the higher layers expect this data */
		us->srb->result = CHECK_CONDITION;

		us->srb->cmnd[0] = REQUEST_SENSE;
		us->srb->cmnd[1] = 0;
		us->srb->cmnd[2] = 0;
		us->srb->cmnd[3] = 0;
		us->srb->cmnd[4] = 18;
		us->srb->cmnd[5] = 0;
    
		/* set the buffer length for transfer */
		old_request_buffer = us->srb->request_buffer;
		us->srb->request_bufflen = 18;
		us->srb->request_buffer = kmalloc(18, GFP_KERNEL);

		/* FIXME: what if this command fails? */
		temp_result = us->transport(us->srb, us);
		US_DEBUGP("-- Result from auto-sense is %d\n", temp_result);

		/* copy the data from the request buffer to the sense buffer */
		for(count = 0; count < 18; count++)
			us->srb->sense_buffer[count] = 
				((unsigned char *)(us->srb->request_buffer))[count];

		US_DEBUGP("-- sense key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  us->srb->sense_buffer[2] & 0xf,
			  us->srb->sense_buffer[12], us->srb->sense_buffer[13]);

		/* we're done here */
		kfree(us->srb->request_buffer);
		us->srb->request_buffer = old_request_buffer;
		return;
	}
  
	/* FIXME: if we need to send more data, or recieve data, we should
	 * do it here.  Then, we can do status handling here also.
	 *
	 * This includes MODE_SENSE from above
	 */
	if (old_cmnd == MODE_SENSE) {
		unsigned char *dta = (unsigned char *)us->srb->request_buffer;

		/* calculate the new length */
		int length = (dta[0] << 8) + dta[1] + 2;

		/* copy the available data length into the structure */
		us->srb->cmnd[7] = length >> 8;
		us->srb->cmnd[8] = length & 0xFF;

		/* send the command to the transport layer */
		us->srb->result = us->transport(srb, us);

		/* FIXME: this assumes that the 2nd attempt is always
		 * successful convert MODE_SENSE_10 return data format 
		 * to MODE_SENSE_6 format */
		dta[0] = dta[1];	/* data len */
		dta[1] = dta[2];	/* med type */
		dta[2] = dta[3];	/* dev-spec prm */
		dta[3] = dta[7];	/* block desc len */
		printk (KERN_DEBUG USB_STORAGE
			"new MODE_SENSE_6 data = %.2X %.2X %.2X %.2X\n",
			dta[0], dta[1], dta[2], dta[3]);
	}

	/* FIXME: if this was a TEST_UNIT_READY, and we get a NOT READY/
	 * LOGICAL DRIVE NOT READY then we do a START_STOP, and retry 
	 */

	/* FIXME: here is where we need to fix-up the return data from 
	 * an INQUIRY command to show ANSI SCSI rev 2
	 */

	/* FIXME: The rest of this is bogus.  usb_control_msg() will only
	 * return an error if we've really honked things up.  If it just
	 * needs a START_STOP, then we'll get some data back via 
	 * REQUEST_SENSE --  either way, this belongs at a higher level
	 */

#if 0
	/* For UFI, if this is the first time we've sent this TEST_UNIT_READY 
	 * command, we can try again
	 */
	if (!done_start && (us->subclass == US_SC_UFI)
	    && (cmd[0] == TEST_UNIT_READY) && (result < 0)) {
    
		/* as per spec try a start command, wait and retry */
		wait_ms(100);
    
		done_start++;
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = START_STOP;
		cmd[4] = 1;		/* start */
    
		result = usb_control_msg(us->pusb_dev, usb_sndctrlpipe(us->pusb_dev,0),
					 US_CBI_ADSC, 
					 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
					 0, us->ifnum,
					 cmd, 12, HZ*5);
		US_DEBUGP("Next usb_control_msg returns %d\n", result);
    
				/* allow another retry */
		retry++;
		continue;
	}
#endif
}

static void transparent_scsi_command(Scsi_Cmnd *srb, struct us_data *us)
{
	unsigned int savelen = us->srb->request_bufflen;
	unsigned int saveallocation = 0;

#if 0
	/* force attention on first command */
	if (!us->attention_done) {
		if (us->srb->cmnd[0] == REQUEST_SENSE) {
			US_DEBUGP("forcing unit attention\n");
			us->attention_done = 1;

			if (us->srb->result == USB_STOR_TRANSPORT_GOOD) {
				unsigned char *p = (unsigned char *)us->srb->request_buffer;
	
				if ((p[2] & 0x0f) != UNIT_ATTENTION) {
					p[2] = UNIT_ATTENTION;
					p[12] = 0x29;	/* power on, reset or bus-reset */
					p[13] = 0;
				} /* if ((p[2] & 0x0f) != UNIT_ATTENTION) */
			} /* if (us->srb->result == USB_STORE_TRANSPORT_GOOD) */
		}
	} /* if (!us->attention_done) */
#endif

	/* If the command has a variable-length payload, then we do them
	 * in two steps -- first we do the minimum, then we recalculate
	 * then length, and re-issue the command 
	 *
	 * we use savelen to remember how much buffer we really have
	 * we use savealloction to remember how much was really requested
	 */

	/* FIXME: remove savelen based on mods to us_transfer_length() */
	switch (us->srb->cmnd[0]) {
	case REQUEST_SENSE:
		if (us->srb->request_bufflen > 18)
			us->srb->request_bufflen = 18;
		else
			break;
		saveallocation = us->srb->cmnd[4];
		us->srb->cmnd[4] = 18;
		break;
    
	case INQUIRY:
		if (us->srb->request_bufflen > 36)
			us->srb->request_bufflen = 36;
		else
			break;
		saveallocation = us->srb->cmnd[4];
		us->srb->cmnd[4] = 36;
		break;
    
	case MODE_SENSE:
		if (us->srb->request_bufflen > 4)
			us->srb->request_bufflen = 4;
		else
			break;
		saveallocation = us->srb->cmnd[4];
		us->srb->cmnd[4] = 4;
		break;
    
	case LOG_SENSE:
	case MODE_SENSE_10:
		if (us->srb->request_bufflen > 8)
			us->srb->request_bufflen = 8;
		else
			break;
		saveallocation = (us->srb->cmnd[7] << 8) | us->srb->cmnd[8];
		us->srb->cmnd[7] = 0;
		us->srb->cmnd[8] = 8;
		break;
    
	default:
		break;
	} /* end switch on cmnd[0] */
  
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
	} /* end if (us->flags & US_FL_MODE_XLATE) */
  
	/* send the command to the transport layer */
	us->srb->result = us->transport(us->srb, us);

	/* if we have an error, we're going to do a REQUEST_SENSE 
	 * automatically */
	/* FIXME: we should only do this for device errors, not 
	 * system errors */
	if (us->srb->result) {
		int temp_result;
		int count;
		void* old_request_buffer;

		US_DEBUGP("Command FAILED: Issuing auto-REQUEST_SENSE\n");

		/* set the result so the higher layers expect this data */
		us->srb->result = CHECK_CONDITION;

		us->srb->cmnd[0] = REQUEST_SENSE;
		us->srb->cmnd[1] = 0;
		us->srb->cmnd[2] = 0;
		us->srb->cmnd[3] = 0;
		us->srb->cmnd[4] = 18;
		us->srb->cmnd[5] = 0;
    
		/* set the buffer length for transfer */
		old_request_buffer = us->srb->request_buffer;
		us->srb->request_bufflen = 18;
		us->srb->request_buffer = kmalloc(18, GFP_KERNEL);

		/* FIXME: what if this command fails? */
		temp_result = us->transport(us->srb, us);
		US_DEBUGP("-- Result from auto-sense is %d\n", temp_result);

		/* copy the data from the request buffer to the sense buffer */
		for(count = 0; count < 18; count++)
			us->srb->sense_buffer[count] = 
				((unsigned char *)(us->srb->request_buffer))[count];

		US_DEBUGP("-- sense key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  us->srb->sense_buffer[2] & 0xf,
			  us->srb->sense_buffer[12], us->srb->sense_buffer[13]);

		/* we're done here */
		kfree(us->srb->request_buffer);
		us->srb->request_buffer = old_request_buffer;
		return;
	}

	if (savelen != us->srb->request_bufflen) {
		unsigned char *p = (unsigned char *)us->srb->request_buffer;
		unsigned int length = 0;
    
		/* set correct length and retry */
		switch (us->srb->cmnd[0]) {

			/* FIXME: we should try to get all the sense data */
		case REQUEST_SENSE:
			/* simply return 18 bytes */
			p[7] = 10;
			length = us->srb->request_bufflen;
			break;
      
		case INQUIRY:
			length = p[4] + 5 > savelen ? savelen : p[4] + 5;
			us->srb->cmnd[4] = length;
			break;
      
		case MODE_SENSE:
			US_DEBUGP("MODE_SENSE Mode data length is %d\n", p[0]);
			length = p[0] + 1 > savelen ? savelen : p[0] + 1;
			us->srb->cmnd[4] = length;
			break;
      
		case LOG_SENSE:
			length = ((p[2] << 8) + p[3]) + 4 > savelen ? savelen : ((p[2] << 8) + p[3]) + 4;
			us->srb->cmnd[7] = length >> 8;
			us->srb->cmnd[8] = length;
			break;
      
		case MODE_SENSE_10:
			US_DEBUGP("MODE_SENSE_10 Mode data length is %d\n",
				  (p[0] << 8) + p[1]);
			length = ((p[0] << 8) + p[1]) + 6 > savelen ? savelen : ((p[0] << 8) + p[1]) + 6;
			us->srb->cmnd[7] = length >> 8;
			us->srb->cmnd[8] = length;
			break;
		} /* end switch on cmnd[0] */
    
		US_DEBUGP("Old/New length = %d/%d\n",
			  savelen, length);
    
		/* issue the new command */
		/* FIXME: this assumes that the second attempt is 
		 * always successful */
		if (us->srb->request_bufflen != length) {
			US_DEBUGP("redoing cmd with len=%d\n", length);
			us->srb->request_bufflen = length;
			us->srb->result = us->transport(us->srb, us);
		}
    
		/* reset back to original values */
		us->srb->request_bufflen = savelen;

		/* fix data as necessary */
		switch (us->srb->cmnd[0]) {
		case INQUIRY:
			if ((((unsigned char*)us->srb->request_buffer)[2] & 0x7) == 0) { 
				US_DEBUGP("Fixing INQUIRY data, setting SCSI rev to 2\n");
				((unsigned char*)us->srb->request_buffer)[2] |= 2;
			}
			/* FALL THROUGH */
		case REQUEST_SENSE:
		case MODE_SENSE:
			if (us->srb->use_sg == 0 && length > 0) {
				int i;
				printk(KERN_DEBUG "Data is");
				for (i = 0; i < 32 && i < length; ++i)
					printk(" %.2x", ((unsigned char *)us->srb->request_buffer)[i]);
				if (i < length)
					printk(" ...");
				printk("\n");
			}

			/* FIXME: is this really necessary? */
			us->srb->cmnd[4] = saveallocation;
			break;
      
		case LOG_SENSE:
		case MODE_SENSE_10:
			/* FIXME: is this really necessary? */
			us->srb->cmnd[7] = saveallocation >> 8;
			us->srb->cmnd[8] = saveallocation;
			break;
		} /* end switch on cmnd[0] */
	} /* if good command */
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
		wake_up(&us->ip_waitq);
	} else {
		US_DEBUGP("ERROR: Unwanted interrupt received!\n");
	}

	/* This return code is truly meaningless -- and I mean truly.  It gets
	 * ignored by other layers.  It used to indicate if we wanted to get
	 * another interrupt or disable the interrupt callback
	 */
	return 0;
}

/* FIXME: this reset function doesn't really reset the port, and it
 * should. Actually it should probably do what it's doing here, and
 * reset the port physically
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
				 US_CBI_ADSC, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, cmd, sizeof(cmd), HZ*5);

	/* long wait for reset */
	schedule_timeout(HZ*6);

	US_DEBUGP("CB_reset: clearing endpoint halt\n");
	usb_clear_halt(us->pusb_dev, usb_rcvbulkpipe(us->pusb_dev, us->ep_in));
	usb_clear_halt(us->pusb_dev, usb_rcvbulkpipe(us->pusb_dev, us->ep_out));

	US_DEBUGP("CB_reset done\n");
	return 0;
}

static int pop_CB_status(Scsi_Cmnd *srb);

/* FIXME: we also need a CBI_command which sets up the completion
 * interrupt, and waits for it
 */
static int CB_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int result;

	US_DEBUGP("CBI gets a command:\n");
	US_DEBUG(us_show_command(srb));

	/* FIXME: we aren't setting the ip_wanted indicator early enough, which
	 * causes some commands to never complete.  This hangs the driver.
	 */

	/* let's send the command via the control pipe */
	result = usb_control_msg(us->pusb_dev, usb_sndctrlpipe(us->pusb_dev,0),
				 US_CBI_ADSC, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum,
				 srb->cmnd, srb->cmd_len, HZ*5);

	/* check the return code for the command */
	if (result < 0) {
		US_DEBUGP("Call to usb_control_msg() returned %d\n", result);

		/* a stall is a fatal condition from the device */
		if (result == -EPIPE) {
			US_DEBUGP("-- Stall on control pipe detected. Clearing\n");
      
			US_DEBUGP("-- Return from usb_clear_halt() is %d\n",
				  usb_clear_halt(us->pusb_dev, 
						 usb_sndctrlpipe(us->pusb_dev, 0)));
			return USB_STOR_TRANSPORT_ERROR;
		}

		/* FIXME: we need to handle NAKs here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* transfer the data payload for this command, if one exists*/
	if (us_transfer_length(srb)) {
		result = us_transfer(srb, US_DIRECTION(srb->cmnd[0]));
		US_DEBUGP("CBI attempted to transfer data, result is 0x%x\n", result);

		/* FIXME: what do the return codes from us_transfer mean? */
		if ((result < 0) && 
		    (result != USB_ST_DATAUNDERRUN) && 
		    (result != USB_ST_STALL)) {
			return DID_ERROR << 16;
		}
	} /* if (us_transfer_length(srb)) */

	/* get status and return it */
	return pop_CB_status(srb);
}

/*
 * Control/Bulk status handler
 */

static int pop_CB_status(Scsi_Cmnd *srb)
{
	struct us_data *us = (struct us_data *)srb->host_scribble;
	int result = 0;
	__u8 status[2];
	int retry = 5;

	US_DEBUGP("pop_CB_status, proto=0x%x\n", us->protocol);
	switch (us->protocol) {
	case US_PR_CB:
		/* get from control */

		while (retry--) {
			result = usb_control_msg(us->pusb_dev, usb_rcvctrlpipe(us->pusb_dev,0),
						 USB_REQ_GET_STATUS, USB_DIR_IN |
						 USB_TYPE_STANDARD | USB_RECIP_DEVICE,
						 0, us->ifnum, status, sizeof(status), HZ*5);
			if (result != USB_ST_TIMEOUT)
				break;
		}
		if (result) {
			US_DEBUGP("Bad AP status request %d\n", result);
			return DID_ABORT << 16;
		}
		US_DEBUGP("Got AP status 0x%x 0x%x\n", status[0], status[1]);
		if (srb->cmnd[0] != REQUEST_SENSE && srb->cmnd[0] != INQUIRY &&
		    ( (status[0] & ~3) || status[1]))
			return (DID_OK << 16) | 2;
		else
			return USB_STOR_TRANSPORT_GOOD;
		break;

		/* FIXME: this should be in a separate function */
	case US_PR_CBI:
		/* get from interrupt pipe */

		/* add interrupt transfer, marked for removal */
		us->ip_wanted = 1;

		/* go to sleep until we get this interrup */
		/* FIXME: this should be changed to use a timeout */
		sleep_on(&us->ip_waitq);
    
		if (us->ip_wanted) {
			US_DEBUGP("Did not get interrupt on CBI\n");
			us->ip_wanted = 0;
			return USB_STOR_TRANSPORT_ERROR;
		}
    
		US_DEBUGP("Got interrupt data 0x%x\n", us->ip_data);

		/* UFI gives us ASC and ASCQ, like a request sense */
		/* FIXME: is this right?  do REQUEST_SENSE and INQUIRY need special
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
	}
	US_DEBUGP("pop_CB_status, reached end of function\n");
	return USB_STOR_TRANSPORT_ERROR;
}

static int Bulk_reset(struct us_data *us)
{
	int result;

	result = usb_control_msg(us->pusb_dev, usb_sndctrlpipe(us->pusb_dev,0),
				 US_BULK_RESET, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 US_BULK_RESET_HARD, us->ifnum,
				 NULL, 0, HZ*5);
	if (result)
		US_DEBUGP("Bulk hard reset failed %d\n", result);
	usb_clear_halt(us->pusb_dev, usb_rcvbulkpipe(us->pusb_dev, us->ep_in));
	usb_clear_halt(us->pusb_dev, usb_sndbulkpipe(us->pusb_dev, us->ep_out));

	/* long wait for reset */
	schedule_timeout(HZ*6);

	return result;
}

/*
 * The bulk only protocol handler.
 *	Uses the in and out endpoints to transfer commands and data
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
	/* FIXME: Regardless of the status of the data stage, we go on to the
	 * status stage.  Note that this implies that if a command is
	 * partially successful, we rely on the device reporting an error
	 * the CSW. The spec says that the device may just decide to short us.
	 */
	if (result == 0) {
		/* send/receive data payload, if there is any */
		if (bcb.DataTransferLength) {
			result = us_transfer(srb, bcb.Flags);
			US_DEBUGP("Bulk data transfer result 0x%x\n", result);
#if 0
			if ((result < 0) && (result != USB_ST_DATAUNDERRUN) 
			    && (result != USB_ST_STALL)) {
				US_DEBUGP("Bulk data transfer result 0x%x\n", result);
				return DID_ABORT << 16;
			}
#endif
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
			return (DID_ABORT << 16);
		}
	}

	/* if we still have a failure at this point, we're in trouble */
	if (result) {
		US_DEBUGP("Bulk status result = 0x%x\n", result);
		return DID_ABORT << 16;
	}

	/* check bulk status */
	US_DEBUGP("Bulk status S 0x%x T 0x%x R %d V 0x%x\n",
		  bcs.Signature, bcs.Tag, bcs.Residue, bcs.Status);
	if (bcs.Signature != US_BULK_CS_SIGN || bcs.Tag != bcb.Tag ||
	    bcs.Status > US_BULK_STAT_PHASE || partial != 13) {
		US_DEBUGP("Bulk logical error\n");
		return DID_ABORT << 16;
	}

	/* based on the status code, we report good or bad */
	switch (bcs.Status) {
	case US_BULK_STAT_OK:
		/* if there is residue, we really didn't finish the command */
		if (bcs.Residue)
			return DID_ERROR << 16;
		else
			return DID_OK << 16;

	case US_BULK_STAT_FAIL:
		return DID_ERROR << 16;

	case US_BULK_STAT_PHASE:
		Bulk_reset(us);
		return DID_ERROR << 16;
	}

	return DID_OK << 16;	    /* check sense required */
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
	if (us->pusb_dev)
		usb_deregister(&storage_driver);

	/* FIXME - leaves hanging host template copy */
	/* (because scsi layer uses it after removal !!!) */
	while (prev->next != us)
		prev = prev->next;
	prev->next = us->next;
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
	if (us->srb) {
		/* busy */
	}
	srb->host_scribble = (unsigned char *)us;
	us->srb = srb;
	srb->scsi_done = done;
	us->action = US_ACT_COMMAND;

	/* wake up the process task */

	wake_up_interruptible(&us->waitq);

	return 0;
}

/* FIXME: This doesn't actually abort anything */
static int us_abort( Scsi_Cmnd *srb )
{
	return 0;
}

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
	SG_ALL,		    /* sg_tablesize */
	1,			    /* cmd_per_lun */
	0,			    /* present */
	FALSE,		    /* unchecked_isa_dma */
	FALSE,		    /* use_clustering */
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

		interruptible_sleep_on(&us->waitq);

		action = us->action;
		us->action = 0;

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
    
		if (signal_pending(current)) {
			/* sending SIGUSR1 makes us print out some info */
			spin_lock_irq(&current->sigmask_lock);
			signr = dequeue_signal(&current->blocked, &info);
			spin_unlock_irq(&current->sigmask_lock);

			if (signr == SIGUSR2) {
				usb_stor_debug = !usb_stor_debug;
				printk(USB_STORAGE "debug toggle = %d\n", usb_stor_debug);
			} else {
				break;	    /* exit the loop on any other signal */
			}
		}
	}
  
	//  MOD_DEC_USE_COUNT;

	printk("usb_stor_control_thread exiting\n");

	/* FIXME: this is a hack to allow for debugging */
	// scsi_unregister_module(MODULE_SCSI_HA, us->htmplt);

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
	Scsi_Host_Template *htmplt;
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
	}

	/* Initialize the us_data structure with some useful info */
	interface = altsetting;
	ss->flags = flags;
	ss->ifnum = ifnum;
	ss->pusb_dev = dev;
	ss->attention_done = 0;

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
		ss->transport = CB_transport;
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
		if ((interface->endpoint[i].bmAttributes &  USB_ENDPOINT_XFERTYPE_MASK)
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
			scsi_unregister_module(MODULE_SCSI_HA, ss->htmplt);
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
			US_DEBUGPX("Reduced Block Commands\n");
			break;

		case US_SC_8020:
			US_DEBUGPX("8020\n");
			break;

		case US_SC_QIC:
			US_DEBUGPX("QIC157\n");
			break;

		case US_SC_8070:
			US_DEBUGPX("8070\n");
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

		/* We only handle certain protocols.  Currently, these are
		 *the only ones that devices use.
		 */
		if ((ss->subclass != US_SC_SCSI) && (ss->subclass != US_SC_UFI)) {
			US_DEBUGP("Sorry, we do not support that protocol yet.\n");
			US_DEBUGP("If you have a device which uses one of the unsupported\n");
			US_DEBUGP("protocols, please contact mdharm-usb@one-eyed-alien.net\n");
     
			kfree(ss);
			return NULL;
		}

		/* Allocate memory for the SCSI Host Template */
		if ((htmplt = (Scsi_Host_Template *)
		     kmalloc(sizeof(*ss->htmplt), GFP_KERNEL)) == NULL ) {

			printk(KERN_WARNING USB_STORAGE "Out of memory\n");

			kfree(ss);
			return NULL;
		}

		/* Initialize the host template based on the default one */
		memcpy(htmplt, &my_host_template, sizeof(my_host_template));

		/* Grab the next host number */
		ss->host_number = my_host_number++;

		/* MDD: FIXME: this is bad.  We abuse this pointer so we
		 * can pass the ss pointer to the host controler thread
		 * in us_detect
		 */
		(struct us_data *)htmplt->proc_dir = ss; 

		/* shuttle E-USB */	
		if (dev->descriptor.idVendor == 0x04e6 &&
		    dev->descriptor.idProduct == 0x0001) {
			__u8 qstat[2];
			int result;
	    
			result = usb_control_msg(ss->pusb_dev, usb_rcvctrlpipe(dev,0),
						 1, 0xC0,
						 0, ss->ifnum,
						 qstat, 2, HZ*5);
			US_DEBUGP("C0 status 0x%x 0x%x\n", qstat[0], qstat[1]);
			init_waitqueue_head(&ss->ip_waitq);
			ss->irqpipe = usb_rcvintpipe(ss->pusb_dev, ss->ep_int);
			result = usb_request_irq(ss->pusb_dev, ss->irqpipe, CBI_irq,
						 255, (void *)ss, &ss->irq_handle);
			if (result)
				return NULL;

			interruptible_sleep_on_timeout(&ss->ip_waitq, HZ*6);
		} else if (ss->protocol == US_PR_CBI)
		{
			int result; 

			init_waitqueue_head(&ss->ip_waitq);

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

			init_waitqueue_head(&ss->waitq);

			ss->notify = &sem;
			ss->pid = kernel_thread(usb_stor_control_thread, ss,
						CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
			if (ss->pid < 0) {
				printk(KERN_WARNING USB_STORAGE "Unable to start control thread\n");
				kfree(htmplt);

				kfree(ss);
				return NULL;
			}

			/* wait for it to start */
			down(&sem);
		}

		/* now register - our detect function will be called */
		scsi_register_module(MODULE_SCSI_HA, htmplt);

		/* put us in the list */
		prev = (struct us_data *)&us_list;
		while (prev->next)
			prev = prev->next;
		prev->next = ss;
	}

	printk(KERN_INFO "WARNING: USB Mass Storage data integrity not assured\n");
	printk(KERN_INFO "USB Mass Storage device found at %d\n", dev->devnum);

	return ss;
}

/* Handle a disconnect event from the USB core */
static void storage_disconnect(struct usb_device *dev, void *ptr)
{
	struct us_data *ss = ptr;

	if (!ss)
		return;

	ss->pusb_dev = NULL;
	//  MOD_DEC_USE_COUNT;
}


/***********************************************************************
 * Initialization and registration
 ***********************************************************************/

int __init usb_stor_init(void)
{
	//  MOD_INC_USE_COUNT;

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
	usb_deregister(&storage_driver) ;
}

module_init(usb_stor_init) ;
module_exit(usb_stor_exit) ;
