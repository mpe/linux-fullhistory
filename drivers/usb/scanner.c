/* -*- linux-c -*- */

/* 
 * Driver for USB Scanners (linux-2.3.41)
 *
 * Copyright (C) 1999, 2000 David E. Nelson
 *
 * Portions may be copyright Brad Keryan and Michael Gee.
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
 * Originally based upon mouse.c (Brad Keryan) and printer.c (Michael Gee).
 *
 * History
 *
 *  0.1  8/31/1999
 *
 *    Developed/tested using linux-2.3.15 with minor ohci.c changes to
 *    support short packes during bulk xfer mode.  Some testing was
 *    done with ohci-hcd but the performace was low.  Very limited
 *    testing was performed with uhci but I was unable to get it to
 *    work.  Initial relase to the linux-usb development effort.
 *
 *
 *  0.2  10/16/1999
 *
 *    - Device can't be opened unless a scanner is plugged into the USB.
 *    - Finally settled on a reasonable value for the I/O buffer's.
 *    - Cleaned up write_scanner()
 *    - Disabled read/write stats
 *    - A little more code cleanup
 *
 *
 *  0.3  10/18/1999
 *
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
 *
 *  0.3.1
 *
 *    - Applied patches for linux-2.3.25.
 *    - Error number reporting changed to reflect negative return codes.
 *
 *
 *  0.3.2
 *
 *    - Applied patches for linux-2.3.26 to scanner_init().
 *    - Debug read/write stats now report values as signed decimal.
 *
 *
 *  0.3.3
 *
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
 *    - Added the HP 5200C USB Vendor/Product ID's.
 *
 *
 *  0.3.4
 *
 *    - Added Greg K-H's <greg@kroah.com> patch for better handling of 
 *      Product/Vendor detection.
 *    - The driver now autoconfigures its endpoints including interrupt
 *      endpoints if one is detected.  The concept was originally based
 *      upon David Brownell's method.
 *    - Added some Seiko/Epson ID's. Thanks to Karl Heinz 
 *      Kremer <khk@khk.net>.
 *    - Added some preliminary ioctl() calls for the PV8630 which is used
 *      by the HP4200. The ioctl()'s still have to be registered. Thanks 
 *      to Adrian Perez Jorge <adrianpj@easynews.com>.
 *    - Moved/migrated stuff to scanner.h
 *    - Removed the usb_set_configuration() since this is handled by
 *      the usb_new_device() routine in usb.c.
 *    - Added the HP 3300C.  Thanks to Bruce Tenison.
 *    - Changed user specified vendor/product id so that root hub doesn't
 *      get falsely attached to. Thanks to Greg K-H.
 *    - Added some Mustek ID's. Thanks to Gernot Hoyler 
 *      <Dr.Hoyler@t-online.de>.
 *    - Modified the usb_string() reporting.  See kfree() comment above.
 *    - Added Umax Astra 2000U. Thanks to Doug Alcorn.
 *    - Updated the printk()'s to use the info/warn/dbg macros.
 *    - Updated usb_bulk_msg() argument types to correct gcc warnings.
 *
 *
 *  TODO
 *
 *    - Simultaneous multiple device attachment
 *
 *
 *  Thanks to:
 *
 *    - All the folks on the linux-usb list who put up with me. :)  This 
 *      has been a great learning experience for me.
 *    - To Linus Torvalds for this great OS.
 *    - The GNU folks.
 *    - The folks that forwarded Vendor:Product ID's to me.
 *    - And anybody else who chimed in with reports and suggestions.
 *
 *  Performance:
 *
 *    System: Pentium 120, 80 MB RAM, OHCI, Linux 2.3.23, HP 4100C USB Scanner
 *            300 dpi scan of the entire bed
 *      24 Bit Color ~ 70 secs - 3.6 Mbit/sec
 *       8 Bit Gray  ~ 17 secs - 4.2 Mbit/sec
 * */

#include "scanner.h"

static int
open_scanner(struct inode * inode, struct file * file)
{
	struct hpscan_usb_data *hps = &hpscan;
	struct usb_device *dev;

	dev = hps->hpscan_dev;

	if (!dev) {
		return -ENODEV;
	}

	if (!hps->present) {
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
	struct usb_device *dev;
	
	ssize_t bytes_written = 0;
	ssize_t ret = 0;

	int copy_size;
	int partial;
	int result = 0;
	
	char *obuf = hps->obuf;

	dev = hps->hpscan_dev;

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

		result = usb_bulk_msg(dev,usb_sndbulkpipe(dev, hps->bulk_out_ep), obuf, copy_size, &partial, 60*HZ);
		dbg("write stats: result:%d copy_size:%d partial:%d", result, copy_size, partial);

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
			printk(KERN_DEBUG "dump: ");
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
	mdelay(5);
	return ret ? ret : bytes_written;
}

static ssize_t
read_scanner(struct file * file, char * buffer,
             size_t count, loff_t *ppos)
{
	struct hpscan_usb_data *hps = &hpscan;
	struct usb_device *dev;

	ssize_t read_count, ret = 0;

	int partial;
	int this_read;
	int result;

	char *ibuf = hps->ibuf;

	dev = hps->hpscan_dev;

	read_count = 0;

	while (count) {
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		this_read = (count > IBUF_SIZE) ? IBUF_SIZE : count;
		
		result = usb_bulk_msg(dev, usb_rcvbulkpipe(dev, hps->bulk_in_ep), ibuf, this_read, &partial, 60*HZ);
		dbg("read stats: result:%d this_read:%d partial:%d", result, this_read, partial);

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
			printk(KERN_DEBUG "dump: ");
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
	return ret ? ret : read_count;
}

static void *
probe_scanner(struct usb_device *dev, unsigned int ifnum)
{
	struct hpscan_usb_data *hps = &hpscan;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	
	int ep_cnt;

	char *ident;
	char valid_device = 0;
	char have_bulk_in, have_bulk_out, have_intr;

	hps->present = 0;

	if (vendor != -1 && product != -1) {
		info("probe_scanner: User specified USB scanner -- Vendor:Product - %x:%x", vendor, product);
	}

/*
 * 1. Check Vendor/Product
 * 2. Determine/Assign Bulk Endpoints
 * 3. Determine/Assign Intr Endpoint
 */

/* 
 * There doesn't seem to be an imaging class defined in the USB
 * Spec. (yet).  If there is, HP isn't following it and it doesn't
 * look like anybody else is either.  Therefore, we have to test the
 * Vendor and Product ID's to see what we have.  Also, other scanners
 * may be able to use this driver by specifying both vendor and
 * product ID's as options to the scanner module in conf.modules.
 *
 * NOTE: Just because a product is supported here does not mean that
 * applications exist that support the product.  It's in the hopes
 * that this will allow developers a means to produce applications
 * that will support USB products.
 *
 * Until we detect a device which is pleasing, we silently punt.  */

	do {
		if (dev->descriptor.idVendor == 0x03f0) {          /* Hewlett Packard */
			if (dev->descriptor.idProduct == 0x0205 || /* 3300C */
			    dev->descriptor.idProduct == 0x0101 || /* 4100C */
			    dev->descriptor.idProduct == 0x0105 || /* 4200C */
			    dev->descriptor.idProduct == 0x0202 || /* PhotoSmart S20 */
			    dev->descriptor.idProduct == 0x0401 || /* 5200C */
			    dev->descriptor.idProduct == 0x0201 || /* 6200C */
			    dev->descriptor.idProduct == 0x0601) { /* 6300C */
				valid_device = 1;
				break;
			}
		}
		
		if (dev->descriptor.idVendor == 0x06bd &&  /* AGFA */
		    dev->descriptor.idProduct == 0x0001) { /* SnapScan 1212U */
			valid_device = 1;
			break;
		}
		
		if (dev->descriptor.idVendor == 0x1606 &&  /* Umax */
		    dev->descriptor.idProduct == 0x0030) { /* Astra 2000U */
			valid_device = 1;
			break;
		}
		
		if (dev->descriptor.idVendor == 0x04b8)	{          /* Seiko/Epson Corp. */
			if (dev->descriptor.idProduct == 0x0101 || /* Perfection 636 */
			    dev->descriptor.idProduct == 0x0104) { /* Perfection 1200U */
				valid_device = 1;
				break;
			}
		}

		if (dev->descriptor.idVendor == 0x055f)	{          /* Mustek */
			if (dev->descriptor.idProduct == 0x0001) { /* 1200 CU */
				valid_device = 1;
				break;
			}
		}

		if (dev->descriptor.idVendor == vendor &&   /* User specified */
		    dev->descriptor.idProduct == product) { /* User specified */
			valid_device = 1;
			break;
		}
	} while (0);

	if (!valid_device)
		return NULL;


/*
 * After this point we can be a little noisy about what we are trying to
 *  configure.
 */

	if (dev->descriptor.bNumConfigurations != 1) {
		info("probe_scanner: Only one configuration is supported.");
		return NULL;
	}

	if (dev->config[0].bNumInterfaces != 1) {
		info("probe_scanner: Only one interface is supported.");
		return NULL;
	}

	interface = dev->config[0].interface[0].altsetting;
	endpoint = interface[0].endpoint;

/* 
 * Start checking for two bulk endpoints OR two bulk endpoints *and* one
 * interrupt endpoint. If we have an interrupt endpoint go ahead and
 * setup the handler. FIXME: This is a future enhancement...
 */


	dbg("probe_scanner: Number of Endpoints: %d", (int) interface->bNumEndpoints);

	if ((interface->bNumEndpoints != 2) && (interface->bNumEndpoints != 3)) {
		info("probe_scanner: Only two or three endpoints supported.");
		return NULL;
	}

	ep_cnt = have_bulk_in = have_bulk_out = have_intr = 0;

	while (ep_cnt < interface->bNumEndpoints) {

		if (!have_bulk_in && IS_EP_BULK_IN(endpoint[ep_cnt])) {
			have_bulk_in = 1;
			hps->bulk_in_ep = ep_cnt + 1;
			ep_cnt++;
			dbg("probe_scanner: bulk_in_ep: %d", (int)hps->bulk_in_ep);
			continue;
		}
		
		if (!have_bulk_out && IS_EP_BULK_OUT(endpoint[ep_cnt])) {
			have_bulk_out = 1;
			hps->bulk_out_ep = ep_cnt + 1;
			ep_cnt++;
			dbg("probe_scanner: bulk_out_ep: %d", (int)hps->bulk_out_ep);
			continue;
		}

		if (!have_intr && IS_EP_INTR(endpoint[ep_cnt])) {
			have_intr = 1;
			hps->intr_ep = ep_cnt + 1;
			ep_cnt++;
			dbg("probe_scanner: intr_ep: %d", (int)hps->intr_ep);
			continue;
		}
		info("probe_scanner: Undetected endpoint. Notify the maintainer.");
		return NULL;	/* Shouldn't ever get here unless we have something weird */
	}

	switch(interface->bNumEndpoints) {
	case 2:
		if (!have_bulk_in || !have_bulk_out) {
			info("probe_scanner: Two bulk endpoints required.");
			return NULL;
		}
		break;
	case 3:
		if (!have_bulk_in || !have_bulk_out || !have_intr) {
			info("probe_scanner: Two bulk endpoints and one interrupt endpoint required.");
			return NULL;
		}
		break;
	default:
		info("probe_scanner: Endpoint determination failed.  Notify the maintainer.");
		return NULL;
	}

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

static int
ioctl_scanner(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	struct hpscan_usb_data *hps = &hpscan;
	struct usb_device *dev;

	int result;
	
	dev = hps->hpscan_dev;

	switch (cmd)
	{
	case PV8630_RECEIVE :
	{
		struct {
			unsigned char data;
			__u16 value;
			__u16 index;
		} args;
		
		if (copy_from_user(&args, (void *)arg, sizeof(args)))
			return -EFAULT;
		
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), 0x0,
					 USB_TYPE_VENDOR|USB_RECIP_DEVICE|USB_DIR_IN,
					 args.value, args.index, &args.data, 1, HZ);

		dbg("ioctl_scanner recv: args.data:%x args.value:%x args.index:%x",
				 args.data, args.value, args.index);

		if (copy_to_user((void *)arg, &args, sizeof(args)))
			return -EFAULT;
		
		dbg("ioctl_scanner recv: result:%d", result);

		return result;
	}
	case PV8630_SEND :
	{
		struct {
			__u16 value;
			__u16 index;
		} args;
		
		if (copy_from_user(&args, (void *)arg, sizeof(args)))
			return -EFAULT;
		
		dbg("ioctl_scanner send: args.value:%x args.index:%x", args.value, args.index);

		result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x1 /* Vendor Specific bRequest */,
					 USB_TYPE_VENDOR|USB_RECIP_DEVICE|USB_DIR_OUT /* 0x40 */,
					 args.value, args.index, NULL, 0, HZ);

		dbg("ioctl_scanner send: result:%d", result);
		

		return result;
	}
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static struct
file_operations usb_scanner_fops = {
	NULL,		/* seek */
	read_scanner,
	write_scanner,
	NULL,		/* readdir */
	NULL,		/* poll */
	ioctl_scanner, 	/* ioctl */
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

