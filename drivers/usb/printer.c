
/* Driver for USB Printers
 * 
 * Copyright 1999 Michael Gee (michael@linuxspecific.com)
 * Copyright 1999 Pavel Machek (pavel@suse.cz)
 *
 * Distribute under GPL version 2 or later.
 */

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
#include <linux/lp.h>
#include <linux/spinlock.h>

#include "usb.h"

#define NAK_TIMEOUT (HZ)				/* stall wait for printer */
#define MAX_RETRY_COUNT ((60*60*HZ)/NAK_TIMEOUT)	/* should not take 1 minute a page! */

#define BIG_BUF_SIZE			8192

/*
 * USB Printer Requests
 */
#define USB_PRINTER_REQ_GET_DEVICE_ID	0
#define USB_PRINTER_REQ_GET_PORT_STATUS	1
#define USB_PRINTER_REQ_SOFT_RESET	2

#define MAX_PRINTERS	8

struct pp_usb_data {
	struct usb_device 	*pusb_dev;
	__u8			isopen;			/* True if open */
	__u8			noinput;		/* True if no input stream */
	__u8			minor;			/* minor number of device */
	__u8			status;			/* last status from device */
	int			maxin, maxout;		/* max transfer size in and out */
	char			*obuf;			/* transfer buffer (out only) */
	wait_queue_head_t	wait_q;			/* for timeouts */
	unsigned int		last_error;		/* save for checking */
	int			bulk_in_ep;		/* Bulk IN endpoint */
	int			bulk_out_ep;		/* Bulk OUT endpoint */
	int			bulk_in_index;		/* endpoint[bulk_in_index] */
	int			bulk_out_index;		/* endpoint[bulk_out_index] */
};

static struct pp_usb_data *minor_data[MAX_PRINTERS];

#define PPDATA(x) ((struct pp_usb_data *)(x))

static unsigned char printer_read_status(struct pp_usb_data *p)
{
	__u8 status;
	struct usb_device *dev = p->pusb_dev;

	if (usb_control_msg(dev, usb_rcvctrlpipe(dev,0),
		USB_PRINTER_REQ_GET_PORT_STATUS,
		USB_TYPE_CLASS | USB_RT_INTERFACE | USB_DIR_IN,
		0, 0, &status, 1, HZ)) {
 		return 0;
	}
	return status;
}

static int printer_check_status(struct pp_usb_data *p)
{
	unsigned int last = p->last_error;
	unsigned char status = printer_read_status(p);

	if (status & LP_PERRORP)
		/* No error. */
		last = 0;
	else if ((status & LP_POUTPA)) {
		if (last != LP_POUTPA) {
			last = LP_POUTPA;
			printk(KERN_INFO "usblp%d out of paper\n", p->minor);
		}
	} else if (!(status & LP_PSELECD)) {
		if (last != LP_PSELECD) {
			last = LP_PSELECD;
			printk(KERN_INFO "usblp%d off-line\n", p->minor);
		}
	} else {
		if (last != LP_PERRORP) {
			last = LP_PERRORP;
			printk(KERN_INFO "usblp%d on fire\n", p->minor);
		}
	}

	p->last_error = last;

	return status;
}

static void printer_reset(struct pp_usb_data *p)
{
	struct usb_device *dev = p->pusb_dev;

	usb_control_msg(dev, usb_sndctrlpipe(dev,0),
		USB_PRINTER_REQ_SOFT_RESET,
		USB_TYPE_CLASS | USB_RECIP_OTHER,
		0, 0, NULL, 0, HZ);
}

static int open_printer(struct inode * inode, struct file * file)
{
	struct pp_usb_data *p;

	if (MINOR(inode->i_rdev) >= MAX_PRINTERS ||
	   !minor_data[MINOR(inode->i_rdev)]) {
		return -ENODEV;
	}

	p = minor_data[MINOR(inode->i_rdev)];
	p->minor = MINOR(inode->i_rdev);

	if (p->isopen++) {
		return -EBUSY;
	}
	if (!(p->obuf = (char *)__get_free_page(GFP_KERNEL))) {
		p->isopen = 0;
		return -ENOMEM;
	}

	printer_check_status(p);

	file->private_data = p;
//	printer_reset(p);
	init_waitqueue_head(&p->wait_q);

	MOD_INC_USE_COUNT;
	return 0;
}

static int close_printer(struct inode * inode, struct file * file)
{
	struct pp_usb_data *p = file->private_data;

	free_page((unsigned long)p->obuf);
	p->isopen = 0;
	file->private_data = NULL;
	/* free the resources if the printer is no longer around */
	if (!p->pusb_dev) {
		minor_data[p->minor] = NULL;
		kfree(p);
	}
	MOD_DEC_USE_COUNT;
	return 0;
}

static ssize_t write_printer(struct file * file,
       const char * buffer, size_t count, loff_t *ppos)
{
	struct pp_usb_data *p = file->private_data;
	unsigned long copy_size;
	unsigned long bytes_written = 0;
	unsigned long partial;
	int result = USB_ST_NOERROR;
	int maxretry;
	
	do {
		char *obuf = p->obuf;
		unsigned long thistime;

		thistime = copy_size = (count > p->maxout) ? p->maxout : count;
		if (copy_from_user(p->obuf, buffer, copy_size))
			return -EFAULT;
		maxretry = MAX_RETRY_COUNT;
		while (thistime) {
			if (!p->pusb_dev)
				return -ENODEV;
			if (signal_pending(current)) {
				return bytes_written ? bytes_written : -EINTR;
			}
			result = p->pusb_dev->bus->op->bulk_msg(p->pusb_dev,
					 usb_sndbulkpipe(p->pusb_dev, p->bulk_out_ep),
					 obuf, thistime, &partial, HZ*20);
			if (partial) {
				obuf += partial;
				thistime -= partial;
				maxretry = MAX_RETRY_COUNT;
			}
			if (result == USB_ST_TIMEOUT) {	/* NAK - so hold for a while */
				if (!maxretry--)
					return -ETIME;
                                interruptible_sleep_on_timeout(&p->wait_q, NAK_TIMEOUT);
				continue;
			} else if (!result && !partial) {
				break;
			}
		};
		if (result) {
			/* whoops - let's reset and fail the request */
//			printk("Whoops - %x\n", result);
			printer_reset(p);
			interruptible_sleep_on_timeout(&p->wait_q, 5*HZ);  /* let reset do its stuff */
			return -EIO;
		}
		bytes_written += copy_size;
		count -= copy_size;
		buffer += copy_size;
	} while ( count > 0 );

	return bytes_written ? bytes_written : -EIO;
}

static ssize_t read_printer(struct file * file,
       char * buffer, size_t count, loff_t *ppos)
{
	struct pp_usb_data *p = file->private_data;
	int read_count = 0;
	int this_read;
	char buf[64];
	unsigned long partial;
	int result;
	
	if (p->noinput)
		return -EINVAL;

	while (count) {
		if (signal_pending(current)) {
			return read_count ? read_count : -EINTR;
		}
		if (!p->pusb_dev)
			return -ENODEV;
		this_read = (count > sizeof(buf)) ? sizeof(buf) : count;

		result = p->pusb_dev->bus->op->bulk_msg(p->pusb_dev,
			  usb_rcvbulkpipe(p->pusb_dev, p->bulk_in_ep),
			  buf, this_read, &partial, HZ*20);

		/* unlike writes, we don't retry a NAK, just stop now */
		if (!result & partial)
			count = this_read = partial;
		else if (result)
			return -EIO;

		if (this_read) {
			if (copy_to_user(buffer, buf, this_read))
				return -EFAULT;
			count -= this_read;
			read_count += this_read;
			buffer += this_read;
		}
	}
	return read_count;
}

static void * printer_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct pp_usb_data *pp;
	int i;

	/*
	 * FIXME - this will not cope with combined printer/scanners
	 */
	if ((dev->descriptor.bDeviceClass != USB_CLASS_PRINTER &&
	    dev->descriptor.bDeviceClass != 0) ||
	    dev->descriptor.bNumConfigurations != 1 ||
	    dev->actconfig->bNumInterfaces != 1) {
		return NULL;
	}

	interface = &dev->actconfig->interface[ifnum].altsetting[0];

	/* Let's be paranoid (for the moment). */
	if (interface->bInterfaceClass != USB_CLASS_PRINTER ||
	    interface->bInterfaceSubClass != 1 ||
	    (interface->bInterfaceProtocol != 2 && interface->bInterfaceProtocol != 1) ||
	    interface->bNumEndpoints > 2) {
		return NULL;
	}

	/* Does this (these) interface(s) support bulk transfers? */
	if ((interface->endpoint[0].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	      != USB_ENDPOINT_XFER_BULK) {
		return NULL;
	}
	if ((interface->bNumEndpoints > 1) &&
	      ((interface->endpoint[1].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	      != USB_ENDPOINT_XFER_BULK)) {
		return NULL;
	}

	/*
	 *  Does this interface have at least one OUT endpoint
	 *  that we can write to: endpoint index 0 or 1?
	 */
	if ((interface->endpoint[0].bEndpointAddress & USB_ENDPOINT_DIR_MASK)
	      != USB_DIR_OUT &&
	    (interface->bNumEndpoints > 1 &&
	      (interface->endpoint[1].bEndpointAddress & USB_ENDPOINT_DIR_MASK)
	      != USB_DIR_OUT)) {
 		return NULL;
 	}

	for (i=0; i<MAX_PRINTERS; i++) {
		if (!minor_data[i])
			break;
	}
	if (i >= MAX_PRINTERS) {
		printk("No minor table space available for USB Printer\n");
		return NULL;
	}

	printk(KERN_INFO "USB Printer found at address %d\n", dev->devnum);

	if (!(pp = kmalloc(sizeof(struct pp_usb_data), GFP_KERNEL))) {
		printk(KERN_DEBUG "usb_printer: no memory!\n");
		return NULL;
	}

	memset(pp, 0, sizeof(struct pp_usb_data));
	minor_data[i] = PPDATA(pp);
	minor_data[i]->minor = i;
	minor_data[i]->pusb_dev = dev;
	minor_data[i]->maxout = (BIG_BUF_SIZE > PAGE_SIZE) ? PAGE_SIZE : BIG_BUF_SIZE;
	if (interface->bInterfaceProtocol != 2)		/* if not bidirectional */
		minor_data[i]->noinput = 1;

	minor_data[i]->bulk_out_index =
		((interface->endpoint[0].bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		  == USB_DIR_OUT) ? 0 : 1;
	minor_data[i]->bulk_in_index = minor_data[i]->noinput ? -1 :
		(minor_data[i]->bulk_out_index == 0) ? 1 : 0;
	minor_data[i]->bulk_in_ep = minor_data[i]->noinput ? -1 :
		interface->endpoint[minor_data[i]->bulk_in_index].bEndpointAddress &
		USB_ENDPOINT_NUMBER_MASK;
	minor_data[i]->bulk_out_ep =
		interface->endpoint[minor_data[i]->bulk_out_index].bEndpointAddress &
		USB_ENDPOINT_NUMBER_MASK;
	if (interface->bInterfaceProtocol == 2) {	/* if bidirectional */
		minor_data[i]->maxin =
			interface->endpoint[minor_data[i]->bulk_in_index].wMaxPacketSize;
	}

	printk(KERN_INFO "USB Printer Summary:\n");
	printk(KERN_INFO "index=%d, maxout=%d, noinput=%d\n",
		i, minor_data[i]->maxout, minor_data[i]->noinput);
	printk(KERN_INFO "bulk_in_ix=%d, bulk_in_ep=%d, bulk_out_ix=%d, bulk_out_ep=%d\n",
		minor_data[i]->bulk_in_index,
		minor_data[i]->bulk_in_ep,
		minor_data[i]->bulk_out_index,
		minor_data[i]->bulk_out_ep);

#if 0
	{
		__u8 status;
		__u8 ieee_id[64];

		/* Let's get the device id if possible. */
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev,0),
		    USB_PRINTER_REQ_GET_DEVICE_ID,
		    USB_TYPE_CLASS | USB_RT_INTERFACE | USB_DIR_IN,
		    0, 0, ieee_id,
		    sizeof(ieee_id)-1, HZ) == 0) {
			if (ieee_id[1] < sizeof(ieee_id) - 1)
				ieee_id[ieee_id[1]+2] = '\0';
			else
				ieee_id[sizeof(ieee_id)-1] = '\0';
			printk(KERN_INFO "  USB Printer ID is %s\n",
				&ieee_id[2]);
		}
		status = printer_read_status(PPDATA(pp));
		printk(KERN_INFO "  Status is %s,%s,%s\n",
		       (status & LP_PSELECD) ? "Selected" : "Not Selected",
		       (status & LP_POUTPA)  ? "No Paper" : "Paper",
		       (status & LP_PERRORP) ? "No Error" : "Error");
	}
#endif
	return pp;
}

static void printer_disconnect(struct usb_device *dev, void *ptr)
{
	struct pp_usb_data *pp = ptr;

	if (pp->isopen) {
		/* better let it finish - the release will do whats needed */
		pp->pusb_dev = NULL;
		return;
	}
	minor_data[pp->minor] = NULL;
	kfree(pp);
}

static struct file_operations usb_printer_fops = {
	NULL,		/* seek */
	read_printer,
	write_printer,
	NULL,		/* readdir */
	NULL,		/* poll - out for the moment */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	open_printer,
	NULL,		/* flush ? */
	close_printer,
	NULL,
	NULL
};

static struct usb_driver printer_driver = {
	"printer",
	printer_probe,
	printer_disconnect,
	{ NULL, NULL },
	&usb_printer_fops,
	0
};

int usb_printer_init(void)
{
	if (usb_register(&printer_driver)) {
		printk(KERN_ERR "USB Printer driver cannot register: "
			"minor number %d already in use\n",
			printer_driver.minor);
		return -1;
	}

	printk(KERN_INFO "USB Printer support registered.\n");
	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return usb_printer_init();
}

void cleanup_module(void)
{
	usb_deregister(&printer_driver);
}
#endif
