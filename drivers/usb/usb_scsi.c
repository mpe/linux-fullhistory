
/* Driver for USB scsi like devices
 * 
 * (C) Michael Gee (michael@linuxspecific.com) 1999
 *
 * This driver is scitzoid  - it makes a USB device appear as both a SCSI device
 * and a character device. The latter is only available if the device has an
 * interrupt endpoint, and is used specifically to receive interrupt events.
 *
 * In order to support various 'strange' devices, this module supports plug in
 * device specific filter modules, which can do their own thing when required.
 *
 * Further reference.
 *	This driver is based on the 'USB Mass Storage Class' document. This
 *	describes in detail the transformation of SCSI command blocks to the
 *	equivalent USB control and data transfer required.
 *	It is important to note that in a number of cases this class exhibits
 *	class-specific exemptions from the USB specification. Notably the
 *	usage of NAK, STALL and ACK differs from the norm, in that they are
 *	used to communicate wait, failed and OK on SCSI commands.
 *	Also, for certain devices, the interrupt endpoint is used to convey
 *	status of a command.
 *
 *	Basically, this stuff is WIERD!!
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>

#include <asm/spinlock.h>
#include <linux/smp_lock.h>

#include <linux/blk.h>
#include "../scsi/scsi.h"
#include "../scsi/hosts.h"
#include "../scsi/sd.h"

#include "usb.h"
#include "usb_scsi.h"

/* direction table (what a pain) */

unsigned char us_direction[256/8] = {

#include "usb_scsi_dt.c"

};

/*
 * Per device data
 */

static int my_host_number;

int usbscsi_debug = 1;

struct us_data {
	struct us_data	*next;			/* next device */
	struct usb_device 	*pusb_dev;
	struct usb_scsi_filter  *filter;		/* filter driver */
	void 			*fdata;			/* filter data */
	unsigned int		flags;			/* from filter initially*/
	__u8			ep_in;			/* in endpoint */
	__u8			ep_out;			/* out ....... */
	__u8			ep_int;			/* interrupt . */
	__u8			subclass;		/* as in overview */
	__u8			protocol;		/* .............. */
	__u8			attention_done;		/* force attention on first command */
	int (*pop)(Scsi_Cmnd *);			/* protocol specific do cmd */
	int (*pop_reset)(struct us_data *);		/* ................. device reset */
	GUID(guid);					/* unique dev id */
	struct Scsi_Host	*host;			/* our dummy host data */
	Scsi_Host_Template	*htmplt;		/* own host template */
	int			host_number;		/* to find us */
	int			host_no;		/* allocated by scsi */
	int			fixedlength;		/* expand commands */
	Scsi_Cmnd		*srb;			/* current srb */
	int			action;			/* what to do */
	wait_queue_head_t	waitq;			/* thread waits */
	wait_queue_head_t	ip_waitq;		/* for CBI interrupts */
	__u16			ip_data;		/* interrupt data */
	int			ip_wanted;		/* needed */
	int			pid;			/* control thread */
	struct semaphore      	*notify; 		/* wait for thread to begin */
};

/*
 * kernel thread actions
 */

#define US_ACT_COMMAND		1
#define US_ACT_ABORT		2
#define US_ACT_DEVICE_RESET	3
#define US_ACT_BUS_RESET	4
#define US_ACT_HOST_RESET	5

static struct proc_dir_entry proc_usb_scsi =
{
    PROC_SCSI_USB_SCSI,
    0,
    NULL,
    S_IFDIR | S_IRUGO | S_IXUGO,
    2
};

static struct us_data *us_list;

static struct usb_scsi_filter *filters;

static int scsi_probe(struct usb_device *dev);
static void scsi_disconnect(struct usb_device *dev);
static struct usb_driver scsi_driver = {
	"usb_scsi",
	scsi_probe,
	scsi_disconnect,
	{ NULL, NULL }
};

/* Data handling, using SG if required */

static int us_one_transfer(struct us_data *us, int pipe, char *buf, int length)
{
    int max_size = usb_maxpacket(us->pusb_dev, pipe) * 16;
    int this_xfer;
    int result;
    unsigned long partial;
    int maxtry = 100;
    while (length) {
	this_xfer = length > max_size ? max_size : length;
	length -= this_xfer;
	do {
	    /*US_DEBUGP("Bulk xfer %x(%d)\n", (unsigned int)buf, this_xfer);*/
	    result = us->pusb_dev->bus->op->bulk_msg(us->pusb_dev, pipe, buf, 
						    this_xfer, &partial);

	    if (result != 0 || partial != this_xfer)
		US_DEBUGP("bulk_msg returned %d xferred %lu/%d\n",
			  result, partial, this_xfer);

	    if (result == USB_ST_STALL) {
		US_DEBUGP("clearing endpoing halt for pipe %x\n", pipe);
		usb_clear_halt(us->pusb_dev,
			       usb_pipeendpoint(pipe) | (pipe & 0x80));
	    }

	    /* we want to retry if the device reported NAK */
	    if (result == USB_ST_TIMEOUT) {
		if (partial != this_xfer) {
		    return 0;	/* I do not like this */
		}
		if (!maxtry--)
		    break;
		this_xfer -= partial;
		buf += partial;
	    } else if (!result && partial != this_xfer) {
		/* short data - assume end */
		result = USB_ST_DATAUNDERRUN;
		break;
	    } else if (result == USB_ST_STALL && us->protocol == US_PR_CB) {
		if (!maxtry--)
		    break;
		this_xfer -= partial;
		buf += partial;
	    } else
		break;
	} while ( this_xfer );
	if (result)
	    return result;
	buf += this_xfer;
    }
    return 0;

}

static int us_transfer(Scsi_Cmnd *srb, int dir_in)
{
    struct us_data *us = (struct us_data *)srb->host_scribble;
    int i;
    int result = -1;
    unsigned int pipe = dir_in ? usb_rcvbulkpipe(us->pusb_dev, us->ep_in) :
	    usb_sndbulkpipe(us->pusb_dev, us->ep_out);

    if (srb->use_sg) {
	struct scatterlist *sg = (struct scatterlist *) srb->request_buffer;

	for (i = 0; i < srb->use_sg; i++) {
	    result = us_one_transfer(us, pipe, sg[i].address, sg[i].length);
	    if (result)
		break;
	}
    }
    else
	result = us_one_transfer(us, pipe,
				 srb->request_buffer, srb->request_bufflen);

    if (result)
	US_DEBUGP("us_transfer returning error %d\n", result);
    return result;
}

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

static int pop_CBI_irq(int state, void *buffer, int len, void *dev_id)
{
    struct us_data *us = (struct us_data *)dev_id;

    if (state != USB_ST_REMOVED) {
	us->ip_data = le16_to_cpup((__u16 *)buffer);
	/* US_DEBUGP("Interrupt Status %x\n", us->ip_data); */
    }
    if (us->ip_wanted) {
	us->ip_wanted = 0;
	wake_up(&us->ip_waitq);
    }

    /* we dont want another interrupt */

    return 0;
}

static int pop_CB_reset(struct us_data *us)
{
    unsigned char cmd[12];
    devrequest dr;
    int result;

    US_DEBUGP("pop_CB_reset\n");
    dr.requesttype = USB_TYPE_CLASS | USB_RT_INTERFACE;
    dr.request = US_CBI_ADSC;
    dr.value = 0;
    dr.index = us->pusb_dev->ifnum;
    dr.length = 12;
    memset(cmd, -1, sizeof(cmd));
    cmd[0] = SEND_DIAGNOSTIC;
    cmd[1] = 4;
    us->pusb_dev->bus->op->control_msg(us->pusb_dev, 
					usb_sndctrlpipe(us->pusb_dev,0),
					&dr, cmd, 12);

    /* long wait for reset */

    schedule_timeout(HZ*5);

    US_DEBUGP("pop_CB_reset: clearing endpoint halt\n");
    usb_clear_halt(us->pusb_dev, us->ep_in | 0x80);
    usb_clear_halt(us->pusb_dev, us->ep_out);

    US_DEBUGP("pop_CB_reset done\n");
    return 0;
}

static int pop_CB_command(Scsi_Cmnd *srb)
{
    struct us_data *us = (struct us_data *)srb->host_scribble;
    devrequest dr;
    unsigned char cmd[16];
    int result;
    int retry = 5;
    int done_start = 0;

    while (retry--) {
	dr.requesttype = USB_TYPE_CLASS | USB_RT_INTERFACE;
	dr.request = US_CBI_ADSC;
	dr.value = 0;
	dr.index = us->pusb_dev->ifnum;
	dr.length = srb->cmd_len;
    
	if (us->flags & US_FL_FIXED_COMMAND) {
	    dr.length = us->fixedlength;
	    memset(cmd, 0, us->fixedlength);

	    /* fix some commands */

	    switch (srb->cmnd[0]) {
	    case WRITE_6:
	    case READ_6:
		cmd[0] = srb->cmnd[0] | 0x20;
		cmd[1] = srb->cmnd[1] & 0xE0;
		cmd[2] = 0;
		cmd[3] = srb->cmnd[1] & 0x1F;
		cmd[4] = srb->cmnd[2];
		cmd[5] = srb->cmnd[3];
		cmd[8] = srb->cmnd[4];
		break;

	    case MODE_SENSE:
	    case MODE_SELECT:
		cmd[0] = srb->cmnd[0] | 0x40;
		cmd[1] = srb->cmnd[1];
		cmd[2] = srb->cmnd[2];
		cmd[8] = srb->cmnd[4];
		break;

	    default:
		memcpy(cmd, srb->cmnd, srb->cmd_len);
		break;
	    }
	    result = us->pusb_dev->bus->op->control_msg(us->pusb_dev, 
						  usb_sndctrlpipe(us->pusb_dev,0),
						  &dr, cmd, us->fixedlength);
	    if (!done_start && (us->subclass == US_SC_UFI /*|| us->subclass == US_SC_8070*/)
		 && cmd[0] == TEST_UNIT_READY && result) {
		/* as per spec try a start command, wait and retry */

		done_start++;
		memset(cmd, 0, sizeof(cmd));
		cmd[0] = START_STOP;
		cmd[4] = 1;		/* start */
		result = us->pusb_dev->bus->op->control_msg(us->pusb_dev, 
						      usb_sndctrlpipe(us->pusb_dev,0),
						      &dr, cmd, us->fixedlength);
		wait_ms(100);
		retry++;
		continue;
	    }
	} else
	    result = us->pusb_dev->bus->op->control_msg(us->pusb_dev, 
						  usb_sndctrlpipe(us->pusb_dev,0),
						  &dr, srb->cmnd, srb->cmd_len);
	if (/*result != USB_ST_STALL &&*/ result != USB_ST_TIMEOUT)
	    return result;
    }
    return result;
}

/*
 * Control/Bulk status handler
 */

static int pop_CB_status(Scsi_Cmnd *srb)
{
    struct us_data *us = (struct us_data *)srb->host_scribble;
    int result;
    __u8 status[2];
    devrequest dr;
    int retry = 5;

    US_DEBUGP("pop_CB_status, proto=%x\n", us->protocol);
    switch (us->protocol) {
    case US_PR_CB:
	/* get from control */

	while (retry--) {
	    dr.requesttype = 0x80 | USB_TYPE_STANDARD | USB_RT_DEVICE;
	    dr.request = USB_REQ_GET_STATUS;
	    dr.index = 0;
	    dr.value = 0;
	    dr.length = 2;
	    result = us->pusb_dev->bus->op->control_msg(us->pusb_dev, 
						  usb_rcvctrlpipe(us->pusb_dev,0),
						  &dr, status, sizeof(status));
	    if (result != USB_ST_TIMEOUT)
		break;
	}
	if (result) {
	    US_DEBUGP("Bad AP status request %d\n", result);
	    return DID_ABORT << 16;
	}
	US_DEBUGP("Got AP status %x %x\n", status[0], status[1]);
	if (srb->cmnd[0] != REQUEST_SENSE && srb->cmnd[0] != INQUIRY && 
	    ( (status[0] & ~3) || status[1]))
	    return (DID_OK << 16) | 2;
	else
	    return DID_OK << 16;
	break;

    case US_PR_CBI:
	/* get from interrupt pipe */

	/* add interrupt transfer, marked for removal */
	us->ip_wanted = 1;
	result = us->pusb_dev->bus->op->request_irq(us->pusb_dev, 
						    usb_rcvctrlpipe(us->pusb_dev, us->ep_int),
						     pop_CBI_irq, 0, (void *)us);
	if (result) {
	    US_DEBUGP("No interrupt for CBI %x\n", result);
	    return DID_ABORT << 16;
	}
	sleep_on(&us->ip_waitq);
	if (us->ip_wanted) {
	    US_DEBUGP("Did not get interrupt on CBI\n");
	    us->ip_wanted = 0;
	    return DID_ABORT << 16;
	}

	US_DEBUGP("Got interrupt data %x\n", us->ip_data);

	/* sort out what it means */

	if (us->subclass == US_SC_UFI) {
	    /* gives us asc and ascq, as per request sense */

	    if (srb->cmnd[0] == REQUEST_SENSE ||
		srb->cmnd[0] == INQUIRY)
		return DID_OK << 16;
	    else
		return (DID_OK << 16) + ((us->ip_data & 0xff) ? 2 : 0);
	}
	if (us->ip_data & 0xff) {
	    US_DEBUGP("Bad CBI interrupt data %x\n", us->ip_data);
	    return DID_ABORT << 16;
	}
	return (DID_OK << 16) + ((us->ip_data & 0x300) ? 2 : 0);
    }
    return DID_ERROR << 16;
}

/* Protocol command handlers */

static int pop_CBI(Scsi_Cmnd *srb)
{
    struct us_data *us = (struct us_data *)srb->host_scribble;
    int result;

    /* run the command */

    if ((result = pop_CB_command(srb))) {
	US_DEBUGP("CBI command %x\n", result);
	if (result == USB_ST_STALL || result == USB_ST_TIMEOUT)	{
	    return (DID_OK << 16) | 2;
	}
	return DID_ERROR << 16;
    }

    /* transfer the data */

    if (us_transfer_length(srb)) {
	result = us_transfer(srb, US_DIRECTION(srb->cmnd[0]));
	if (result && result != USB_ST_DATAUNDERRUN && result != USB_ST_STALL) {
	    US_DEBUGP("CBI  transfer %x\n", result);
	    return DID_ERROR << 16;
	}
#if 0
	else if (result == USB_ST_DATAUNDERRUN) {
	    return DID_OK << 16;
	}
    } else {
	if (!result) {
	    return DID_OK << 16;
	}
#endif
    }

    /* get status */

    return pop_CB_status(srb);
}

static int pop_Bulk_reset(struct us_data *us)
{
    devrequest dr;
    int result;

    dr.requesttype = USB_TYPE_CLASS | USB_RT_INTERFACE;
    dr.request = US_BULK_RESET;
    dr.value = US_BULK_RESET_HARD;
    dr.index = 0;
    dr.length = 0;

    result = us->pusb_dev->bus->op->control_msg(us->pusb_dev, usb_sndctrlpipe(us->pusb_dev,0), &dr, NULL, 0);
    if (result)
	US_DEBUGP("Bulk hard reset failed %d\n", result);
    usb_clear_halt(us->pusb_dev, us->ep_in | 0x80);
    usb_clear_halt(us->pusb_dev, us->ep_out);

    /* long wait for reset */

    schedule_timeout(HZ*5);

    return result;
}
/*
 * The bulk only protocol handler.
 * 	Uses the in and out endpoints to transfer commands and data (nasty)
 */
static int pop_Bulk(Scsi_Cmnd *srb)
{
    struct us_data *us = (struct us_data *)srb->host_scribble;
    struct bulk_cb_wrap bcb;
    struct bulk_cs_wrap bcs;
    int result;
    unsigned long partial;
    int stall;

    /* set up the command wrapper */

    bcb.Signature = US_BULK_CB_SIGN;
    bcb.DataTransferLength = us_transfer_length(srb);;
    bcb.Flags = US_DIRECTION(srb->cmnd[0]) << 7;
    bcb.Tag = srb->serial_number;
    bcb.Lun = 0;
    memset(bcb.CDB, 0, sizeof(bcb.CDB));
    memcpy(bcb.CDB, srb->cmnd, srb->cmd_len);
    if (us->flags & US_FL_FIXED_COMMAND) {
	bcb.Length = us->fixedlength;
    } else {
	bcb.Length = srb->cmd_len;
    }

    /* send it to out endpoint */

    US_DEBUGP("Bulk command S %x T %x L %d F %d CL %d\n", bcb.Signature,
	      bcb.Tag, bcb.DataTransferLength, bcb.Flags, bcb.Length);
    result = us->pusb_dev->bus->op->bulk_msg(us->pusb_dev,
		     usb_sndbulkpipe(us->pusb_dev, us->ep_out), &bcb, 
					    US_BULK_CB_WRAP_LEN, &partial);
    if (result) {
	US_DEBUGP("Bulk command result %x\n", result);
	return DID_ABORT << 16;
    }

    //return DID_BAD_TARGET << 16;
    /* send/receive data */

    if (bcb.DataTransferLength) {
	result = us_transfer(srb, bcb.Flags);
	if (result && result != USB_ST_DATAUNDERRUN && result != USB_ST_STALL) {
	    US_DEBUGP("Bulk transfer result %x\n", result);
	    return DID_ABORT << 16;
	}
    }

    /* get status */


    stall = 0;
    do {
	result = us->pusb_dev->bus->op->bulk_msg(us->pusb_dev,
			 usb_rcvbulkpipe(us->pusb_dev, us->ep_in), &bcs, 
						US_BULK_CS_WRAP_LEN, &partial);
	if (result == USB_ST_STALL || result == USB_ST_TIMEOUT)
	    stall++;
	else
	    break;
    } while ( stall < 3);
    if (result && result != USB_ST_DATAUNDERRUN) {
	US_DEBUGP("Bulk status result = %x\n", result);
	return DID_ABORT << 16;
    }

    /* check bulk status */

    US_DEBUGP("Bulk status S %x T %x R %d V %x\n", bcs.Signature, bcs.Tag,
	      bcs.Residue, bcs.Status);
    if (bcs.Signature != US_BULK_CS_SIGN || bcs.Tag != bcb.Tag ||
	bcs.Status > US_BULK_STAT_PHASE) {
	US_DEBUGP("Bulk logical error\n");
	return DID_ABORT << 16;
    }
    switch (bcs.Status) {
    case US_BULK_STAT_OK:
	return DID_OK << 16;

    case US_BULK_STAT_FAIL:
	/* check for underrun - dont report */
	if (bcs.Residue)
	    return DID_OK << 16;
	//pop_Bulk_reset(us);
	break;

    case US_BULK_STAT_PHASE:
	return DID_ERROR << 16;
    }
    return (DID_OK << 16) | 2;		/* check sense required */

}

/* Host functions */

/* detect adapter (always true ) */
static int us_detect(struct SHT *sht)
{
    /* FIXME - not nice at all, but how else ? */
    struct us_data *us = (struct us_data *)sht->proc_dir;
    char name[32];

    sprintf(name, "usbscsi%d", us->host_number);
    proc_usb_scsi.namelen = strlen(name);
    proc_usb_scsi.name = kmalloc(proc_usb_scsi.namelen+1, GFP_KERNEL);
    if (!proc_usb_scsi.name)
	return 0;
    strcpy((char *)proc_usb_scsi.name, name);
    sht->proc_dir = kmalloc(sizeof(*sht->proc_dir), GFP_KERNEL);
    if (!sht->proc_dir) {
	kfree(proc_usb_scsi.name);
	return 0;
    }
    *sht->proc_dir = proc_usb_scsi;
    sht->name = proc_usb_scsi.name;
    us->host = scsi_register(sht, sizeof(us));
    if (us->host) {
	us->host->hostdata[0] = (unsigned long)us;
	us->host_no = us->host->host_no;
	return 1;
    }
    kfree(proc_usb_scsi.name);
    kfree(sht->proc_dir);
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

    if (us->filter)
	us->filter->release(us->fdata);
    if (us->pusb_dev)
	usb_deregister(&scsi_driver);

    /* FIXME - leaves hanging host template copy */
    /* (bacause scsi layer uses it after removal !!!) */
    while(prev->next != us)
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

static int us_abort( Scsi_Cmnd *srb )
{
    return 0;
}

static int us_bus_reset( Scsi_Cmnd *srb )
{
    struct us_data *us = (struct us_data *)srb->host->hostdata[0];

    us->pop_reset(us);
    return SUCCESS;
}

static int us_host_reset( Scsi_Cmnd *srb )
{
    return 0;
}


#undef SPRINTF
#define SPRINTF(args...) { if (pos < (buffer + length)) pos += sprintf (pos, ## args); }

int usb_scsi_proc_info (char *buffer, char **start, off_t offset, int length, int hostno, int inout)
{
    struct us_data *us = us_list;
    char *pos = buffer;
    char *vendor;
    char *product;
    char *style = "";

    /* find our data from hostno */

    while (us) {
	if (us->host_no == hostno)
	    break;
	us = us->next;
    }

    if (!us)
	return -ESRCH;

    /* null on outward */

    if (inout)
	return length;

    if (!us->pusb_dev || !(vendor = usb_string(us->pusb_dev, us->pusb_dev->descriptor.iManufacturer)))
	vendor = "?";
    if (!us->pusb_dev || !(product = usb_string(us->pusb_dev, us->pusb_dev->descriptor.iProduct)))
	product = "?";

    switch (us->protocol) {
    case US_PR_CB:
	style = "Control/Bulk";
	break;

    case US_PR_CBI:
	style = "Control/Bulk/Interrupt";
	break;

    case US_PR_ZIP:
	style = "Bulk only";
	break;

    }
    SPRINTF ("Host scsi%d: usb-scsi\n", hostno);
    SPRINTF ("Device: %s %s - GUID " GUID_FORMAT "\n", vendor, product, GUID_ARGS(us->guid) );
    SPRINTF ("Style: %s\n", style);

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
    NULL,			/* next */
    NULL,			/* module */
    NULL,			/* proc_dir */
    usb_scsi_proc_info,
    NULL,			/* name - points to unique */
    us_detect,
    us_release,
    NULL,			/* info */
    NULL,			/* ioctl */
    us_command,
    us_queuecommand,
    NULL,			/* eh_strategy */
    us_abort,
    us_bus_reset,
    us_bus_reset,
    us_host_reset,
    NULL,			/* abort */
    NULL,			/* reset */
    NULL,			/* slave_attach */
    NULL,			/* bios_param */
    1,				/* can_queue */
    -1,				/* this_id */
    SG_ALL,			/* sg_tablesize */
    1,				/* cmd_per_lun */
    0,				/* present */
    FALSE,			/* unchecked_isa_dma */
    FALSE,			/* use_clustering */
    TRUE,			/* use_new_eh_code */
    TRUE			/* emulated */
};

static unsigned char sense_notready[] = {
    0x70,			/* current error */
    0x00,
    0x02,			/* not ready */
    0x00,
    0x00,
    10,				/* additional length */
    0x00,
    0x00,
    0x00,
    0x00,
    0x04,			/* not ready */
    0x03,			/* manual intervention */
    0x00,
    0x00,
    0x00,
    0x00
};

static int usbscsi_control_thread(void * __us)
{
    struct us_data *us = (struct us_data *)__us;
    int action;

    lock_kernel();

    /*
     * This thread doesn't need any user-level access,
     * so get rid of all our resources..
     */
    exit_mm(current);
    exit_files(current);
    //exit_fs(current);

    sprintf(current->comm, "usbscsi%d", us->host_number);
    
    unlock_kernel();

    up(us->notify);

    for(;;) {
	    siginfo_t info;
	    int unsigned long signr;

	    interruptible_sleep_on(&us->waitq);

	    action = us->action;
	    us->action = 0;

	    switch (action) {
	    case US_ACT_COMMAND       :
		if (us->srb->target || us->srb->lun) {
		    /* bad device */
		    US_DEBUGP( "Bad device number (%d/%d) or dev %x\n", us->srb->target, us->srb->lun, (unsigned int)us->pusb_dev);
		    us->srb->result = DID_BAD_TARGET << 16;
		} else if (!us->pusb_dev) {

		    /* our device has gone - pretend not ready */

		    if (us->srb->cmnd[0] == REQUEST_SENSE) {
			memcpy(us->srb->request_buffer, sense_notready, sizeof(sense_notready));
			us->srb->result = DID_OK << 16;
		    } else {
			us->srb->result = (DID_OK << 16) | 2;
		    }
		} else {
		    US_DEBUG(us_show_command(us->srb));

		    /* check for variable length - do properly if so */

		    if (us->filter && us->filter->command)
		        us->srb->result = us->filter->command(us->fdata, us->srb);
		    else if (us->srb->cmnd[0] == START_STOP &&
			     us->pusb_dev->descriptor.idProduct == 0x0001 &&
			     us->pusb_dev->descriptor.idVendor == 0x04e6)
			us->srb->result = DID_OK << 16;
		    else {
			unsigned int savelen = us->srb->request_bufflen;
			unsigned int saveallocation;

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
			}
		        us->srb->result = us->pop(us->srb);

			if (savelen != us->srb->request_bufflen &&
			    us->srb->result == (DID_OK << 16)) {
			    unsigned char *p = (unsigned char *)us->srb->request_buffer;
			    unsigned int length;

			    /* set correct length and retry */
			    switch (us->srb->cmnd[0]) {
			    case REQUEST_SENSE:
				/* simply return 18 bytes */
				p[7] = 10;
				length = us->srb->request_bufflen;;
				break;

			    case INQUIRY:
				length = p[4] + 5 > savelen ? savelen : p[4] + 5;
				us->srb->cmnd[4] = length;
				break;

			    case MODE_SENSE:
				length = p[0] + 4 > savelen ? savelen : p[0] + 4;
				us->srb->cmnd[4] = 4;
				break;

			    case LOG_SENSE:
				length = ((p[2] << 8) + p[3]) + 4 > savelen ? savelen : ((p[2] << 8) + p[3]) + 4;
				us->srb->cmnd[7] = length >> 8;
				us->srb->cmnd[8] = length;
				break;

			    case MODE_SENSE_10:
				length = ((p[0] << 8) + p[1]) + 8 > savelen ? savelen : ((p[0] << 8) + p[1]) + 8;
				us->srb->cmnd[7] = length >> 8;
				us->srb->cmnd[8] = length;
				break;
			    }

			    US_DEBUGP("Old/New length = %d/%d\n", savelen, length);

			    if (us->srb->request_bufflen != length) {
			        US_DEBUGP("redoing cmd with len=%d\n", length);
				us->srb->request_bufflen = length;
				us->srb->result = us->pop(us->srb);
			    }
			    /* reset back to original values */

			    us->srb->request_bufflen = savelen;
			    switch (us->srb->cmnd[0]) {
			    case REQUEST_SENSE:
			    case INQUIRY:
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
				us->srb->cmnd[4] = saveallocation;
				break;

			    case LOG_SENSE:
			    case MODE_SENSE_10:
				us->srb->cmnd[7] = saveallocation >> 8;
				us->srb->cmnd[8] = saveallocation;
				break;
			    }
			}
			/* force attention on first command */
			if (!us->attention_done) {
			    US_DEBUGP("forcing unit attention\n");
			    if (us->srb->cmnd[0] == REQUEST_SENSE) {
				if (us->srb->result == (DID_OK << 16)) {
				    unsigned char *p = (unsigned char *)us->srb->request_buffer;

				    us->attention_done = 1;
				    if ((p[2] & 0x0f) != UNIT_ATTENTION) {
					p[2] = UNIT_ATTENTION;
					p[12] = 0x29;	/* power on, reset or bus-reset */
					p[13] = 0;
				    }
				}
			    } else if (us->srb->cmnd[0] != INQUIRY &&
				       us->srb->result == (DID_OK << 16)) {
				us->srb->result |= 2;	/* force check condition */
			    }
			}
		    }
		}
		US_DEBUGP("scsi cmd done, result=%x\n", us->srb->result);
		us->srb->scsi_done(us->srb);
		us->srb = NULL;
		break;

	    case US_ACT_ABORT         :
		break;

	    case US_ACT_DEVICE_RESET  :
		break;

	    case US_ACT_BUS_RESET     :
		break;

	    case US_ACT_HOST_RESET    :
		break;

	    }

	    if(signal_pending(current)) {
		    /* sending SIGUSR1 makes us print out some info */
		    spin_lock_irq(&current->sigmask_lock);
		    signr = dequeue_signal(&current->blocked, &info);
		    spin_unlock_irq(&current->sigmask_lock);

		    if (signr == SIGUSR2) {
			    printk("USBSCSI debug toggle\n");
			    usbscsi_debug = !usbscsi_debug;
		    } else {
			    break;
		    }
	    }
    }

    MOD_DEC_USE_COUNT;

    printk("usbscsi_control_thread exiting\n");

    return 0;
}	

static int scsi_probe(struct usb_device *dev)
{
    struct usb_interface_descriptor *interface;
    int i;
    char *mf;		/* manufacturer */
    char *prod;		/* product */
    char *serial;		/* serial number */
    struct us_data *ss = NULL;
    struct usb_scsi_filter *filter = filters;
    void *fdata = NULL;
    unsigned int flags = 0;
    GUID(guid);
    struct us_data *prev;
    Scsi_Host_Template *htmplt;
    int protocol = 0;
    int subclass = 0;

    GUID_CLEAR(guid);
    mf = usb_string(dev, dev->descriptor.iManufacturer);
    prod = usb_string(dev, dev->descriptor.iProduct);
    serial = usb_string(dev, dev->descriptor.iSerialNumber);

    /* probe with filters first */

    if (mf && prod)	{
	    while (filter) {
		    if ((fdata = filter->probe(dev, mf, prod, serial)) != NULL) {
			    flags = filter->flags;
			    printk(KERN_INFO "USB Scsi filter %s\n", filter->name);
			    break;
		    }
		    filter = filter->next;
	    }
    }

    /* generic devices next */

    if (fdata == NULL) {

	/* some exceptions */
	if (dev->descriptor.idVendor == 0x04e6 &&
	    dev->descriptor.idProduct == 0x0001) {
	    /* shuttle E-USB */
	    protocol = US_PR_CB;
	    subclass = US_SC_8070;	/* an assumption */
	} else if (dev->descriptor.bDeviceClass != 0 ||
	    dev->config->altsetting->interface->bInterfaceClass != 8 ||
	    dev->config->altsetting->interface->bInterfaceSubClass < US_SC_MIN ||
	    dev->config->altsetting->interface->bInterfaceSubClass > US_SC_MAX) {
	    return -1;
	}

	/* now check if we have seen it before */

	if (dev->descriptor.iSerialNumber && 
	    usb_string(dev, dev->descriptor.iSerialNumber) ) {
	    make_guid(guid, dev->descriptor.idVendor, dev->descriptor.idProduct,
		      usb_string(dev, dev->descriptor.iSerialNumber));
	} else {
	    make_guid(guid, dev->descriptor.idVendor, dev->descriptor.idProduct,
		      "0");
	}
	for (ss = us_list; ss; ss = ss->next) {
	    if (!ss->pusb_dev && GUID_EQUAL(guid, ss->guid))	{
		US_DEBUGP("Found existing GUID " GUID_FORMAT "\n", GUID_ARGS(guid));
		flags = ss->flags;
		break;
	    }
	}
    }

    if (!ss) {
	if ((ss = (struct us_data *)kmalloc(sizeof(*ss), GFP_KERNEL)) == NULL) {
	    printk(KERN_WARNING USB_SCSI "Out of memory\n");
	    if (filter)
		    filter->release(fdata);
	    return -1;
	}
	memset(ss, 0, sizeof(struct us_data));
    }

    interface = dev->config->altsetting->interface;
    ss->filter = filter;
    ss->fdata = fdata;
    ss->flags = flags;
    if (subclass) {
	ss->subclass = subclass;
	ss->protocol = protocol;
    } else {
	ss->subclass = interface->bInterfaceSubClass;
	ss->protocol = interface->bInterfaceProtocol;
    }
    ss->attention_done = 0;

    /* set the protocol op */

    US_DEBUGP("Protocol ");
    switch (ss->protocol) {
    case US_PR_CB:
	US_DEBUGPX("Control/Bulk\n");
	ss->pop = pop_CBI;
	ss->pop_reset = pop_CB_reset;
	break;

    case US_PR_CBI:
	US_DEBUGPX("Control/Bulk/Interrupt\n");
	ss->pop = pop_CBI;
	ss->pop_reset = pop_CB_reset;
	break;

    default:
	US_DEBUGPX("Bulk\n");
	ss->pop = pop_Bulk;
	ss->pop_reset = pop_Bulk_reset;
	break;
    }

    /* 
     * we are expecting a minimum of 2 endpoints - in and out (bulk)
     * an optional interrupt is OK (necessary for CBI protocol)
     * we will ignore any others
     */

    for (i = 0; i < interface->bNumEndpoints; i++) { 
	    if (interface->endpoint[i].bmAttributes == 0x02) {
		    if (interface->endpoint[i].bEndpointAddress & 0x80)
			    ss->ep_in = interface->endpoint[i].bEndpointAddress & 0x0f;
		    else
			    ss->ep_out = interface->endpoint[i].bEndpointAddress & 0x0f;
	    } else if (interface->endpoint[i].bmAttributes == 0x03) {
		    ss->ep_int = interface->endpoint[i].bEndpointAddress & 0x0f;
	    }
    }
    US_DEBUGP("Endpoints In %d Out %d Int %d\n", ss->ep_in, ss->ep_out, ss->ep_int);

    /* exit if strange looking */

    if (usb_set_configuration(dev, dev->config[0].bConfigurationValue) ||
	usb_set_interface(dev, interface->bInterfaceNumber, 0) ||
	!ss->ep_in || !ss->ep_out || (ss->protocol == US_PR_CBI && ss->ep_int == 0)) {
	US_DEBUGP("Problems with device\n");
	if (ss->host) {
	    scsi_unregister_module(MODULE_SCSI_HA, ss->htmplt);
	    kfree(ss->htmplt->name);
	    kfree(ss->htmplt);
	}
	if (filter)
		filter->release(fdata);
	kfree(ss);
	return -1;			/* no endpoints */
    }

    if (dev->config[0].iConfiguration && usb_string(dev, dev->config[0].iConfiguration))
	US_DEBUGP("Configuration %s\n", usb_string(dev, dev->config[0].iConfiguration));
    if (interface->iInterface && usb_string(dev, interface->iInterface))
	US_DEBUGP("Interface %s\n", usb_string(dev, interface->iInterface));

    ss->pusb_dev = dev;

    /* Now generate a scsi host definition, and register with scsi above us */

    if (!ss->host) {

	/* make unique id if possible */

	US_DEBUGP("New GUID " GUID_FORMAT "\n", GUID_ARGS(guid));
	memcpy(ss->guid, guid, sizeof(guid));

	/* set class specific stuff */

	US_DEBUGP("SubClass ");
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
	    ss->flags |= US_FL_FIXED_COMMAND;
	    ss->fixedlength = 12;
	    break;
	case US_SC_SCSI:
	    US_DEBUGPX("Transparent SCSI\n");
	    break;
	case US_SC_UFI:
	    US_DEBUGPX(" UFF\n");
	    ss->flags |= US_FL_FIXED_COMMAND;
	    ss->fixedlength = 12;
	    break;

	default:
	    break;
	}

	/* create unique host template */

	if ((htmplt = (Scsi_Host_Template *)kmalloc(sizeof(*ss->htmplt), GFP_KERNEL)) == NULL ) {
	    printk(KERN_WARNING USB_SCSI "Out of memory\n");
	    if (filter)
		    filter->release(fdata);
	    kfree(ss);
	    return -1;
	}
	memcpy(htmplt, &my_host_template, sizeof(my_host_template));
	ss->host_number = my_host_number++;


	(struct us_data *)htmplt->proc_dir = ss;

	if (dev->descriptor.idVendor == 0x04e6 &&
	    dev->descriptor.idProduct == 0x0001) {
	    devrequest dr;
	    __u8 qstat[2];

	    /* shuttle E-USB */
	    dr.requesttype = 0xC0;
	    dr.request = 1;
	    dr.index = 0;
	    dr.value = 0;
	    dr.length = 0;
	    ss->pusb_dev->bus->op->control_msg(ss->pusb_dev, usb_rcvctrlpipe(dev,0), &dr, qstat, 2);
	    US_DEBUGP("C0 status %x %x\n", qstat[0], qstat[1]);
	    init_waitqueue_head(&ss->ip_waitq);
	    ss->pusb_dev->bus->op->request_irq(ss->pusb_dev, 
						usb_rcvctrlpipe(ss->pusb_dev, ss->ep_int),
						pop_CBI_irq, 0, (void *)ss);
	    interruptible_sleep_on_timeout(&ss->ip_waitq, HZ*5);

	} else if (ss->protocol == US_PR_CBI)
	    init_waitqueue_head(&ss->ip_waitq);


	/* start up our thread */

	{
	    DECLARE_MUTEX_LOCKED(sem);

	    init_waitqueue_head(&ss->waitq);
    
	    ss->notify = &sem;
	    ss->pid = kernel_thread(usbscsi_control_thread, ss,
		    CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	    if (ss->pid < 0) {
		printk(KERN_WARNING USB_SCSI "Unable to start control thread\n");
		kfree(htmplt);
		if (filter)
			filter->release(fdata);
		kfree(ss);
		return -1;
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


    printk(KERN_INFO "USB SCSI device found at address %d\n", dev->devnum);

    dev->private = ss;
    return 0;
}

static void scsi_disconnect(struct usb_device *dev)
{
	struct us_data *ss = dev->private;

	if (!ss)
	    return;
	if (ss->filter)
		ss->filter->release(ss->fdata);
	ss->pusb_dev = NULL;
	dev->private = NULL;		/* just in case */
	MOD_DEC_USE_COUNT;
}

int usb_scsi_init(void)
{

	MOD_INC_USE_COUNT;
#ifdef CONFIG_USB_HP4100
	hp4100_init();
#endif
#ifdef CONFIG_USB_ZIP
	usb_zip_init();
#endif
	usb_register(&scsi_driver);
	printk(KERN_INFO "USB SCSI support registered.\n");
	return 0;
}


int usb_scsi_register(struct usb_scsi_filter *filter)
{
	struct usb_scsi_filter *prev = (struct usb_scsi_filter *)&filters;

	while (prev->next)
		prev = prev->next;
	prev->next = filter;
	return 0;
}

void usb_scsi_deregister(struct usb_scsi_filter *filter)
{
	struct usb_scsi_filter *prev = (struct usb_scsi_filter *)&filters;

	while (prev->next && prev->next != filter)
		prev = prev->next;
	if (prev->next)
		prev->next = filter->next;
}

#ifdef MODULE
int init_module(void)
{

	return usb_scsi_init();
}

void cleanup_module(void)
{
	unsigned int offset;

	usb_deregister(&scsi_driver);
}
#endif
