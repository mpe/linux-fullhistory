/* -*- linux-c -*- */

/* 
 * Driver for USB HP Scanners
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
 * */

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
#include <linux/spinlock.h>

#include "usb.h"

/* stall/wait timeout for scanner */
#define NAK_TIMEOUT (HZ)

/* For some reason, an IBUF_SIZE of 8192 causes REALLY big problems
 * with linux-2.3.15.  Anything more than 4k seems to not have an
 * effect on increasing performance. Anything smaller than 4k hurts
 * it.  */
#define IBUF_SIZE 4096

/* This is a scanner, so not much data is sent to it.  The largest
 * stuff may be some kind of maps and stuff but that's kinda rare.  */
#define OBUF_SIZE 128

struct hpscan_usb_data {
	struct usb_device 	*hpscan_dev;            /* init: probe_scanner */
	__u8			isopen;			/* nz if open */

	__u8                    present;                /* Device is present on the bus */
	char			*obuf;			/* transfer buffers */
	char			*ibuf;		
	wait_queue_head_t	wait_q;			/* for timeouts */
};

static struct hpscan_usb_data hpscan;

static int
open_scanner(struct inode * inode, struct file * file)
{
	struct hpscan_usb_data *hps = &hpscan;

	if (hps->isopen) {
		return -EBUSY;
	}
	hps->isopen = 1;

	init_waitqueue_head(&hps->wait_q);

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

	int result = 0;
	int maxretry;
	
	do {
		unsigned long thistime;
		char *obuf = hps->obuf;

		thistime = copy_size = (count > OBUF_SIZE) ?  OBUF_SIZE : count;
		if (copy_from_user(hps->obuf, buffer, copy_size))
			return -EFAULT;
		maxretry = 5;
		while (thistime) {
			if (!hps->hpscan_dev)
				return -ENODEV;
			if (signal_pending(current)) {
				return bytes_written ? bytes_written : -EINTR;
			}

			result = hps->hpscan_dev->bus->op->bulk_msg(hps->hpscan_dev,usb_sndbulkpipe(hps->hpscan_dev, 2), obuf, thistime, &partial, HZ*10);
			
			//printk(KERN_DEBUG "write stats: result:%d thistime:%lu partial:%lu\n", result, thistime, partial);

			if (result == USB_ST_TIMEOUT) {	/* NAK - so hold for a while */
				if(!maxretry--) {
					return -ETIME;
				}
				interruptible_sleep_on_timeout(&hps->wait_q, NAK_TIMEOUT);
				continue;
			} else if (!result & partial) {
				obuf += partial;
				thistime -= partial;
			} else
				break;
		};
		if (result) {
			printk("Write Whoops - %x\n", result);
			return -EIO;
		}
		bytes_written += copy_size;
		count -= copy_size;
		buffer += copy_size;
	} while ( count > 0 );
	
	return bytes_written ? bytes_written : -EIO;
}

static ssize_t
read_scanner(struct file * file, char * buffer,
             size_t count, loff_t *ppos)
{
	struct hpscan_usb_data *hps = &hpscan;

	ssize_t read_count;

	unsigned long partial;

	int this_read;
	int result;

/* Wait for the scanner to get it's act together.  This may involve
 * resetting the head, warming up the lamp, etc.  maxretry is number
 * of seconds.  */
	int maxretry = 30; 

	char *ibuf = hps->ibuf;

	read_count = 0;

	while (count) {
		if (signal_pending(current)) {
			return read_count ? read_count : -EINTR;
		}
		if (!hps->hpscan_dev)
			return -ENODEV;
		this_read = (count > IBUF_SIZE) ? IBUF_SIZE : count;
		
		result = hps->hpscan_dev->bus->op->bulk_msg(hps->hpscan_dev, usb_rcvbulkpipe(hps->hpscan_dev, 1), ibuf, this_read, &partial, HZ*10);

		printk(KERN_DEBUG "read stats: result:%d this_read:%u partial:%lu\n", result, this_read, partial);
		
		if (partial) {
			count = this_read = partial;
		} else if (result == USB_ST_TIMEOUT || result == 15) {
			if(!maxretry--) {
				printk(KERN_DEBUG "read_scanner: maxretry timeout\n");
				return -ETIME;
			}
			interruptible_sleep_on_timeout(&hps->wait_q, NAK_TIMEOUT);
			continue;
		} else if (result != USB_ST_DATAUNDERRUN) {
			printk("Read Whoops - result:%u partial:%lu this_read:%u\n", result, partial, this_read);
			return -EIO;
		} else {
			return (0);
		}
		
		if (this_read) {
			if (copy_to_user(buffer, ibuf, this_read))
				return -EFAULT;
			count -= this_read;
			read_count += this_read;
			buffer += this_read;
		}
	}
	return read_count;
}

static int
probe_scanner(struct usb_device *dev)
{
	struct hpscan_usb_data *hps = &hpscan;

	/*
	 * Don't bother using an HP 4200C since it does NOT understand
	 * SCL and HP isn't going to be releasing the specs any time
	 * soon.  */
	if (dev->descriptor.idVendor != 0x3f0 ) {
		printk(KERN_INFO "Scanner is not an HP Scanner.\n");
		return -1;
	}

	if (dev->descriptor.idProduct != 0x101 && /* HP 4100C */
	    dev->descriptor.idProduct != 0x202 && /* HP 5100C */
            dev->descriptor.idProduct != 0x601) { /* HP 6300C */
		printk(KERN_INFO "Scanner model not supported/tested.\n");
		return -1;
	}
	
	printk(KERN_DEBUG "USB Scanner found at address %d\n", dev->devnum);
	
	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		printk(KERN_DEBUG "Failed to set configuration\n");
		return -1;
	}

	hps->present = 1;
	hps->hpscan_dev = dev;

	if (!(hps->obuf = (char *)kmalloc(OBUF_SIZE, GFP_KERNEL))) {
		return -ENOMEM;
	}

	if (!(hps->ibuf = (char *)kmalloc(IBUF_SIZE, GFP_KERNEL))) {
		return -ENOMEM;
	}
  
	return 0;
}

static void
disconnect_scanner(struct usb_device *dev)
{
	struct hpscan_usb_data *hps = &hpscan;

	if (hps->isopen) {
		/* better let it finish - the release will do whats needed */
		hps->hpscan_dev = NULL;
		return;
	}
	kfree(hps->ibuf);
	kfree(hps->obuf);

	dev->private = NULL;		/* just in case */
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
usb_hp_scanner_init(void)
{
	usb_register(&scanner_driver);
	printk(KERN_DEBUG "USB Scanner support registered.\n");
	return 0;
}


void
usb_hp_scanner_cleanup(void)
{
	struct hpscan_usb_data *hps = &hpscan;

	hps->present = 0;
	usb_deregister(&scanner_driver);
}

#ifdef MODULE

int
init_module(void)
{
	return usb_hp_scanner_init();
}

void
cleanup_module(void)
{
	usb_hp_scanner_cleanup();
}
#endif

