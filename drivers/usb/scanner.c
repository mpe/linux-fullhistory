/* -*- linux-c -*- */

/* 
 * Driver for USB Scanners (linux-2.3.33)
 *
 * David E. Nelson (dnelson@jump.net)
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Based upon mouse.c (Brad Keryan) and printer.c (Michael Gee).
 *
 * History
 *  0.1  8/31/1999
 *
 *    Developed/tested using linux-2.3.15 with minor ohci.c changes to
 *    support short packes during bulk xfer mode.  Some testing was
 *    done with ohci-hcd but the performace was low.  Very limited
 *    testing was performed with uhci but I was unable to get it to
 *    work.  Initial relase to the linux-usb development effort.
 *
 *  0.2  10/16/1999
 *
 *    FIXED:
 *    - Device can't be opened unless a scanner is plugged into the USB.
 *    - Finally settled on a reasonable value for the I/O buffer's.
 *    - Cleaned up write_scanner()
 *    - Disabled read/write stats
 *    - A little more code cleanup
 *
 *  0.3  10/18/1999
 *
 *    FIXED:
 *    - Device registration changed to reflect new device
 *      allocation/registration for linux-2.3.22+.
 *    - Adopted David Brownell's <david-b@pacbell.net> technique for 
 *      assigning bulk endpoints.
 *    - Removed unnessesary #include's
 *    - Scanner model now reported via syslog INFO after being detected 
 *      *and* configured.
 *    - Added user specified verdor:product USB ID's which can be passed 
 *      as module parameters.
 *
 *  0.3.1
 *    FIXED:
 *    - Applied patches for linux-2.3.25.
 *    - Error number reporting changed to reflect negative return codes.
 *
 *  0.3.2
 *    FIXED:
 *    - Applied patches for linux-2.3.26 to scanner_init().
 *    - Debug read/write stats now report values as signed decimal.
 *
 *
 *  0.3.3
 *    FIXED:
 *    - Updated the bulk_msg() calls to usb usb_bulk_msg().
 *    - Added a small delay in the write_scanner() method to aid in
 *      avoiding NULL data reads on HP scanners.  We'll see how this works.
 *    - Return values from usb_bulk_msg() now ignore positive values for
 *      use with the ohci driver.
 *    - Added conditional debugging instead of commenting/uncommenting
 *      all over the place.
 *    - kfree()'d the pointer after using usb_string() as documented in
 *      linux-usb-api.txt.
 *    - Added usb_set_configuration().  It got lost in version 0.3 -- ack!
 *    - Added the HP 5200C USB Vendor/Product ID's
 *
 *  TODO
 *    - Simultaneous multiple device attachment
 *    - ioctl()'s ?
 *
 *  Thanks to:
 *    - All the folks on the linux-usb list who put up with me. :)  This 
 *      has been a great learning experience for me.
 *    - To Linus Torvalds for this great OS.
 *    - The GNU folks.
 *    - The folks that forwarded Vendor:Product ID's to me.
 *    - And anybody else who chimed in with reports and suggestions.
 *
 *  Performance:
 *    System: Pentium 120, 80 MB RAM, OHCI, Linux 2.3.23, HP 4100C USB Scanner
 *            300 dpi scan of the entire bed
 *      24 Bit Color ~ 70 secs - 3.6 Mbit/sec
 *       8 Bit Gray  ~ 17 secs - 4.2 Mbit/sec
 * */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/malloc.h>
#include <linux/delay.h>

#undef DEBUG		/* Enable to print results of read/write_scanner() calls */
#undef RD_DATA_DUMP	/* Enable to dump data - limited to 24 bytes */
#undef WR_DATA_DUMP

#include "usb.h"

#define IBUF_SIZE 32768
#define OBUF_SIZE 4096

struct hpscan_usb_data {
	struct usb_device *hpscan_dev;
        int isopen;		/* Not zero if the device is open */
	int present;		/* Device is present on the bus */
	char *obuf, *ibuf;	/* transfer buffers */
	char iep, oep;          /* I/O Endpoints */
};

static struct hpscan_usb_data hpscan;

MODULE_AUTHOR("David E. Nelson, dnelson@jump.net, http://www.jump.net/~dnelson");
MODULE_DESCRIPTION("USB Scanner Driver");

static __u16 vendor=0x05f9, product=0xffff;
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");

static int
open_scanner(struct inode * inode, struct file * file)
{
	struct hpscan_usb_data *hps = &hpscan;

	if (!hps->present) {
		return -ENODEV;
	}

	if (!hps->hpscan_dev) {
		return -ENODEV;
	}

	if (hps->isopen) {
		return -EBUSY;
	}

	hps->isopen = 1;

	MOD_INC_USE_COUNT;

	return 0;
}

static int
close_scanner(struct inode * inode, struct file * file)
{
	struct hpscan_usb_data *hps = &hpscan;

	hps->isopen = 0;

	MOD_DEC_USE_COUNT;

	return 0;
}

static ssize_t
write_scanner(struct file * file, const char * buffer,
              size_t count, loff_t *ppos)
{
	struct hpscan_usb_data *hps = &hpscan;

	unsigned long copy_size;
	unsigned long bytes_written = 0;
	unsigned long partial;

	ssize_t ret = 0;

	int result = 0;
	
	char *obuf = hps->obuf;

	set_current_state(TASK_INTERRUPTIBLE);

	while (count > 0) {

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		copy_size = (count > OBUF_SIZE) ? OBUF_SIZE : count;
		
		if (copy_from_user(hps->obuf, buffer, copy_size)) {
			ret = -EFAULT;
			break;
		}

		result = usb_bulk_msg(hps->hpscan_dev,usb_sndbulkpipe(hps->hpscan_dev, hps->oep), obuf, copy_size, &partial, 30*HZ);
		dbg("write stats: result:%d copy_size:%lu partial:%lu", (int)result, copy_size, partial);

		if (result == USB_ST_TIMEOUT) {	/* NAK -- shouldn't happen */
			warn("write_scanner: NAK recieved.");
			ret = -ETIME;
			break;
		} else if (result < 0) { /* We should not get any I/O errors */
			warn("write_scanner: funky result: %d. Please notify the maintainer.", result);
			ret = -EIO;
			break;
		} 

#ifdef WR_DATA_DUMP
		if (partial) {
			unsigned char cnt, cnt_max;
			cnt_max = (partial > 24) ? 24 : partial;
			printk(KERN_DEBUG __FILE__ ": dump: ");
			for (cnt=0; cnt < cnt_max; cnt++) {
				printk("%X ", obuf[cnt]);
			}
			printk("\n");
		}
#endif
		if (partial != copy_size) { /* Unable to write complete amount */
			ret = -EIO;
			break;
		}

		if (partial) { /* Data written */
			obuf += partial;
			count -= partial;
			bytes_written += partial;
		} else { /* No data written */
			ret = 0;
			bytes_written = 0;
			break;
		}
	}
//	mdelay(5);
	set_current_state(TASK_RUNNING);
	return ret ? ret : bytes_written;
}

static ssize_t
read_scanner(struct file * file, char * buffer,
             size_t count, loff_t *ppos)
{
	struct hpscan_usb_data *hps = &hpscan;

	ssize_t read_count, ret = 0;

	unsigned long partial;

	int this_read;
	int result;

	char *ibuf = hps->ibuf;

	read_count = 0;
	set_current_state(TASK_INTERRUPTIBLE);

	while (count) {
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		this_read = (count > IBUF_SIZE) ? IBUF_SIZE : count;
		
		result = usb_bulk_msg(hps->hpscan_dev, usb_rcvbulkpipe(hps->hpscan_dev, hps->iep), ibuf, this_read, &partial, 60*HZ);
		dbg("read stats: result:%d this_read:%u partial:%lu", (int)result, this_read, partial);

		if (result == USB_ST_TIMEOUT) { /* NAK -- shouldn't happen */
			warn("read_scanner: NAK received");
			ret = -ETIME;
			break;
		} else if ((result < 0) && (result != USB_ST_DATAUNDERRUN)) {
			warn("read_scanner: funky result: %d. Please notify the maintainer.", (int)result);
			ret = -EIO;
			break;
		}

#ifdef RD_DATA_DUMP
		if (partial) {
			unsigned char cnt, cnt_max;
			cnt_max = (partial > 24) ? 24 : partial;
			printk(KERN_DEBUG __FILE__ ": dump: ");
			for (cnt=0; cnt < cnt_max; cnt++) {
				printk("%X ", ibuf[cnt]);
			}
			printk("\n");
		}
#endif

		if (partial) { /* Data returned */
			count = this_read = partial;
		} else {
			ret = 0;
			read_count = 0;
			break;
		}

		if (this_read) {
			if (copy_to_user(buffer, ibuf, this_read)) {
				ret = -EFAULT;
				break;
			}
			count -= this_read;
			read_count += this_read;
			buffer += this_read;
		}
	}
	set_current_state(TASK_RUNNING);
	return ret ? ret : read_count;
}

static void *
probe_scanner(struct usb_device *dev, unsigned int ifnum)
{
	struct hpscan_usb_data *hps = &hpscan;
	struct usb_endpoint_descriptor *endpoint;

	char *ident;

	hps->present = 0;

	if (vendor != 0 || product != 0)
		info("USB Scanner Vendor:Product - %x:%x\n", vendor, product);

/* There doesn't seem to be an imaging class defined in the USB
 * Spec. (yet).  If there is, HP isn't following it and it doesn't
 * look like anybody else is either.  Therefore, we have to test the
 * Vendor and Product ID's to see what we have.  This makes this
 * driver a high maintenance driver since it has to be updated with
 * each release of a product.  Also, other scanners may be able to use
 * this driver but again, their Vendor and Product ID's must be added.
 *
 * NOTE: Just because a product is supported here does not mean that
 * applications exist that support the product.  It's in the hopes
 * that this will allow developers a means to produce applications
 * that will support USB products.
 *
 * Until we detect a device which is pleasing, we silently punt.
 * */

	if (dev->descriptor.idVendor != 0x03f0 && /* Hewlett Packard */
	    dev->descriptor.idVendor != 0x06bd && /* AGFA */
	    dev->descriptor.idVendor != 0x1606 && /* UMAX */
	    dev->descriptor.idVendor != vendor ) { /* User specified */
		return NULL;
	}

	if (dev->descriptor.idProduct != 0x0101 && /* HP 4100C */
	    dev->descriptor.idProduct != 0x0102 && /* HP 4200C & PhotoSmart S20? */
	    dev->descriptor.idProduct != 0x0202 && /* HP 5100C */
	    dev->descriptor.idProduct != 0x0401 && /* HP 5200C */
	    dev->descriptor.idProduct != 0x0201 && /* HP 6200C */
	    dev->descriptor.idProduct != 0x0601 && /* HP 6300C */
	    dev->descriptor.idProduct != 0x0001 && /* AGFA SnapScan 1212U */
	    dev->descriptor.idProduct != 0x0030 && /* Umax 2000U */
	    dev->descriptor.idProduct != product) { /* User specified */
		return NULL;
	}

/* After this point we can be a little noisy about what we are trying to
 *  configure. */

	if (dev->descriptor.bNumConfigurations != 1 || 
	    dev->config[0].bNumInterfaces != 1) {
		dbg("probe_scanner: only simple configurations supported");
		return NULL;
	}

	endpoint = dev->config[0].interface[0].altsetting[0].endpoint;

	if (endpoint[0].bmAttributes != USB_ENDPOINT_XFER_BULK
	    || endpoint [1].bmAttributes != USB_ENDPOINT_XFER_BULK) {
		dbg("probe_scanner: invalid bulk endpoints");
		return NULL;
	}

	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
                dbg("probe_scanner: failed usb_set_configuration");
		hps->hpscan_dev = NULL;
                return NULL;
        }

/* By the time we get here, we should be dealing with a fairly simple
 * device that supports at least two bulk endpoints on endpoints 1 and
 * 2.
 *
 * We determine the bulk endpoints so that the read_*() and write_*()
 * procedures can recv/send data to the correct endpoint.
 * */

	hps->iep = hps->oep = 0;

	if ((endpoint[0].bEndpointAddress & 0x80) == 0x80) {
		hps->iep = endpoint[0].bEndpointAddress & 0x7f;
	} else {
		hps->oep = endpoint[0].bEndpointAddress;
	}

	if ((endpoint[1].bEndpointAddress & 0x80) == 0x80) {
		hps->iep = endpoint[1].bEndpointAddress & 0x7f;
	} else {
		hps->oep = endpoint[1].bEndpointAddress;
	}

	ident = kmalloc(256, GFP_KERNEL);
	if (ident) {
		usb_string(dev, dev->descriptor.iProduct, ident, 256);
		info("USB Scanner (%s) found at address %d", ident, dev->devnum);
		kfree(ident);
	}

	dbg("probe_scanner: using bulk endpoints - In: %x  Out: %x", hps->iep, hps->oep);

	hps->present = 1;
	hps->hpscan_dev = dev;

	if (!(hps->obuf = (char *)kmalloc(OBUF_SIZE, GFP_KERNEL))) {
		return NULL;
	}

	if (!(hps->ibuf = (char *)kmalloc(IBUF_SIZE, GFP_KERNEL))) {
		return NULL;
	}

	return hps;
}

static void
disconnect_scanner(struct usb_device *dev, void *ptr)
{
	struct hpscan_usb_data *hps = (struct hpscan_usb_data *) ptr;

	if (hps->isopen) {
		/* better let it finish - the release will do whats needed */
		hps->hpscan_dev = NULL;
		return;
	}
	kfree(hps->ibuf);
	kfree(hps->obuf);

	hps->present = 0;
}

static struct
file_operations usb_scanner_fops = {
	NULL,		/* seek */
	read_scanner,
	write_scanner,
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,    	/* ioctl */
	NULL,		/* mmap */
	open_scanner,
	NULL,		/* flush */
	close_scanner,
	NULL,         
	NULL,         /* fasync */
};

static struct
usb_driver scanner_driver = {
       "usbscanner",
       probe_scanner,
       disconnect_scanner,
       { NULL, NULL },
       &usb_scanner_fops,
       48
};

int
usb_scanner_init(void)
{
        if (usb_register(&scanner_driver) < 0)
                return -1;

	info("USB Scanner support registered.");
	return 0;
}


void
usb_scanner_cleanup(void)
{
	struct hpscan_usb_data *hps = &hpscan;

	hps->present = 0;
	usb_deregister(&scanner_driver);
}

#ifdef MODULE

int
init_module(void)
{
	return usb_scanner_init();
}

void
cleanup_module(void)
{
	usb_scanner_cleanup();
}
#endif

