
/* Driver for USB Printers
 * 
 * (C) Michael Gee (michael@linuxspecific.com) 1999
 * 
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

#ifndef USB_PRINTER_MAJOR
#define USB_PRINTER_MAJOR 63
#endif

static int mymajor = USB_PRINTER_MAJOR;

#define MAX_PRINTERS	8

struct pp_usb_data {
	struct usb_device 	*pusb_dev;
	__u8			isopen;			/* nz if open */
	__u8			noinput;		/* nz if no input stream */
	__u8			minor;			/* minor number of device */
	__u8			status;			/* last status from device */
	int			maxin, maxout;		/* max transfer size in and out */
	char			*obuf;			/* transfer buffer (out only) */
	wait_queue_head_t	wait_q;			/* for timeouts */
	unsigned int		last_error;		/* save for checking */
};

static struct pp_usb_data *minor_data[MAX_PRINTERS];

#define PPDATA(x) ((struct pp_usb_data *)(x))

unsigned char printer_read_status(struct pp_usb_data *p)
{
	__u8 status;
	devrequest dr;
	struct usb_device *dev = p->pusb_dev;

	dr.requesttype = USB_TYPE_CLASS | USB_RT_INTERFACE | 0x80;
	dr.request = 1;
	dr.value = 0;
	dr.index = 0;
	dr.length = 1;
	if (dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, &status, 1)) {
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

void printer_reset(struct pp_usb_data *p)
{
	devrequest dr;
	struct usb_device *dev = p->pusb_dev;

	dr.requesttype = USB_TYPE_CLASS | USB_RECIP_OTHER;
	dr.request = 2;
	dr.value = 0;
	dr.index = 0;
	dr.length = 0;
	dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

static int open_printer(struct inode * inode, struct file * file)
{
	struct pp_usb_data *p;

	if(MINOR(inode->i_rdev) >= MAX_PRINTERS ||
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
	return 0;
}

static int close_printer(struct inode * inode, struct file * file)
{
	struct pp_usb_data *p = file->private_data;

	free_page((unsigned long)p->obuf);
	p->isopen = 0;
	file->private_data = NULL;
	if(!p->pusb_dev) {
		minor_data[p->minor] = NULL;
		kfree(p);

		MOD_DEC_USE_COUNT;

	}
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
	int endpoint_num;
	struct usb_interface_descriptor *interface;
	
	interface = p->pusb_dev->config->interface->altsetting;
	endpoint_num = (interface->endpoint[1].bEndpointAddress & 0x0f);

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
					 usb_sndbulkpipe(p->pusb_dev, endpoint_num),
					 obuf, thistime, &partial);
			if (partial) {
				obuf += partial;
				thistime -= partial;
				maxretry = MAX_RETRY_COUNT;
			}
			if (result == USB_ST_TIMEOUT) {	/* NAK - so hold for a while */
				if(!maxretry--)
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
	int read_count;
	int this_read;
	char buf[64];
	unsigned long partial;
	int result;
	int endpoint_num;
	struct usb_interface_descriptor *interface;
	
	interface = p->pusb_dev->config->interface->altsetting;
	endpoint_num = (interface->endpoint[0].bEndpointAddress & 0x0f);

	if (p->noinput)
		return -EINVAL;

	read_count = 0;
	while (count) {
		if (signal_pending(current)) {
			return read_count ? read_count : -EINTR;
		}
		if (!p->pusb_dev)
			return -ENODEV;
		this_read = (count > sizeof(buf)) ? sizeof(buf) : count;

		result = p->pusb_dev->bus->op->bulk_msg(p->pusb_dev,
			  usb_rcvbulkpipe(p->pusb_dev, endpoint_num),
			  buf, this_read, &partial);

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

static int printer_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	int i;

	/*
	 * FIXME - this will not cope with combined printer/scanners
	 */
	if ((dev->descriptor.bDeviceClass != 7 &&
	     dev->descriptor.bDeviceClass != 0) ||
	    dev->descriptor.bNumConfigurations != 1 ||
	    dev->config[0].bNumInterfaces != 1) {
		return -1;
	}

	interface = &dev->config[0].interface[0].altsetting[0];

	/* Lets be paranoid (for the moment)*/
	if (interface->bInterfaceClass != 7 ||
	    interface->bInterfaceSubClass != 1 ||
	    (interface->bInterfaceProtocol != 2 && interface->bInterfaceProtocol != 1)||
	    interface->bNumEndpoints > 2) {
		return -1;
	}

	if ((interface->endpoint[0].bEndpointAddress & 0xf0) != 0x00 ||
	    interface->endpoint[0].bmAttributes != 0x02 ||
	    (interface->bNumEndpoints > 1 && (
		    (interface->endpoint[1].bEndpointAddress & 0xf0) != 0x80 ||
		    interface->endpoint[1].bmAttributes != 0x02))) {
		return -1;
	}

	for (i=0; i<MAX_PRINTERS; i++) {
		if (!minor_data[i])
			break;
	}
	if (i >= MAX_PRINTERS) {
		return -1;
	}

	printk(KERN_INFO "USB Printer found at address %d\n", dev->devnum);

	if (!(dev->private = kmalloc(sizeof(struct pp_usb_data), GFP_KERNEL))) {
		printk( KERN_DEBUG "usb_printer: no memory!\n");
		return -1;
	}

	memset(dev->private, 0, sizeof(struct pp_usb_data));
	minor_data[i] = PPDATA(dev->private);
	minor_data[i]->minor = i;
	minor_data[i]->pusb_dev = dev;
	/* The max packet size can't be more than 64 (& will be 64 for
	 * any decent bulk device); this calculation was silly.  -greg
	 * minor_data[i]->maxout = interface->endpoint[0].wMaxPacketSize * 16;
	 */
	minor_data[i]->maxout = 8192;
	if (minor_data[i]->maxout > PAGE_SIZE) {
                minor_data[i]->maxout = PAGE_SIZE;
	}
	if (interface->bInterfaceProtocol != 2)
		minor_data[i]->noinput = 1;
	else {
		minor_data[i]->maxin = interface->endpoint[1].wMaxPacketSize;
	}

        if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		printk(KERN_INFO "  Failed usb_set_configuration: printer\n");
		return -1;
	}
#if 0
	{
		__u8 status;
		__u8 ieee_id[64];
		devrequest dr;

		/* Lets get the device id if possible */
		dr.requesttype = USB_TYPE_CLASS | USB_RT_INTERFACE | 0x80;
		dr.request = 0;
		dr.value = 0;
		dr.index = 0;
		dr.length = sizeof(ieee_id) - 1;
		if (dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, ieee_id, sizeof(ieee_id)-1) == 0) {
			if (ieee_id[1] < sizeof(ieee_id) - 1)
				ieee_id[ieee_id[1]+2] = '\0';
			else
				ieee_id[sizeof(ieee_id)-1] = '\0';
			printk(KERN_INFO "  Printer ID is %s\n", &ieee_id[2]);
		}
		status = printer_read_status(PPDATA(dev->private));
		printk(KERN_INFO "  Status is %s,%s,%s\n",
		       (status & 0x10) ? "Selected" : "Not Selected",
		       (status & 0x20) ? "No Paper" : "Paper",
		       (status & 0x08) ? "No Error" : "Error");
	}
#endif
	return 0;
}

static void printer_disconnect(struct usb_device *dev)
{
	struct pp_usb_data *pp = dev->private;

	if (pp->isopen) {
		/* better let it finish - the release will do whats needed */
		pp->pusb_dev = NULL;
		return;
	}
	minor_data[pp->minor] = NULL;
	kfree(pp);
	dev->private = NULL;		/* just in case */
	MOD_DEC_USE_COUNT;
}

static struct usb_driver printer_driver = {
	"printer",
	printer_probe,
	printer_disconnect,
	{ NULL, NULL }
};

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

int usb_printer_init(void)
{
	int result;

	MOD_INC_USE_COUNT;
	
	if ((result = register_chrdev(USB_PRINTER_MAJOR, "usblp", &usb_printer_fops)) < 0) {
		printk(KERN_WARNING "usbprinter: Cannot register device\n");
		return result;
	}
	if (mymajor == 0) {
		mymajor = result;
	}
	usb_register(&printer_driver);
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
	unregister_chrdev(mymajor, "usblp");
}
#endif
