/*
 * printer.c  Version 0.1
 *
 * Copyright (c) 1999 Michael Gee 	<michael@linuxspecific.com>
 * Copyright (c) 1999 Pavel Machek      <pavel@suse.cz>
 * Copyright (c) 2000 Vojtech Pavlik    <vojtech@suse.cz>
 *
 * USB Printer Device Class driver for USB printers and printer cables
 *
 * Sponsored by SuSE
 *
 * ChangeLog:
 *	v0.1 - thorough cleaning, URBification, almost a rewrite
 *	v0.2 - some more cleanups
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/lp.h>

#define DEBUG

#include "usb.h"

#define USBLP_BUF_SIZE		8192

/*
 * USB Printer Requests
 */

#define USBLP_REQ_GET_ID	0x00
#define USBLP_REQ_GET_STATUS	0x01
#define USBLP_REQ_RESET		0x02

#define USBLP_MINORS		16
#define USBLP_MINOR_BASE	0

#define USBLP_WRITE_TIMEOUT	(60*HZ)				/* 60 seconds */

struct usblp {
	struct usb_device 	*dev;				/* USB device */
	struct urb		readurb, writeurb;		/* The urbs */
	wait_queue_head_t	readwait, writewait, pollwait;	/* Zzzzz ... */
	int			readcount;			/* Counter for reads */
	int			ifnum;				/* Interface number */
	unsigned int		minor;				/* minor number of device */
	unsigned char		used;				/* True if open */
	unsigned char		bidir;				/* interface is bidirectional */
};

static struct usblp *usblp_table[USBLP_MINORS] = { NULL, /* ... */ };

/*
 * Functions for usblp control messages.
 */

static int usblp_ctrl_msg(struct usblp *usblp, int request, int dir, int recip, void *buf, int len)
{
	int retval = usb_control_msg(usblp->dev,
		dir ? usb_rcvctrlpipe(usblp->dev, 0) : usb_sndctrlpipe(usblp->dev, 0),
		request, USB_TYPE_CLASS | dir | recip, 0, usblp->ifnum, buf, len, HZ * 5);
	dbg("usblp_control_msg: rq: 0x%02x dir: %d recip: %d len: %#x result: %d", request, !!dir, recip, len, retval);
	return retval < 0 ? retval : 0;
}

#define usblp_read_status(usblp, status)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_STATUS, USB_DIR_IN, USB_RECIP_INTERFACE, status, 1)
#define usblp_get_id(usblp, id, maxlen)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_ID, USB_DIR_IN, USB_RECIP_INTERFACE, id, maxlen)
#define usblp_reset(usblp)\
	usblp_ctrl_msg(usblp, USBLP_REQ_RESET, USB_DIR_OUT, USB_RECIP_OTHER, NULL, 0)

/*
 * URB callback.
 */

static void usblp_bulk(struct urb *urb)
{
	struct usblp *usblp = urb->context;

	if (!usblp || !usblp->dev || !usblp->used)
		return;

	if (urb->status)
		warn("nonzero read bulk status received: %d", urb->status);

	if (urb == &usblp->writeurb)
		wake_up_interruptible(&usblp->writewait);

	if (urb == &usblp->readurb)
		wake_up_interruptible(&usblp->readwait);

	wake_up_interruptible(&usblp->pollwait);
}

/*
 * Get and print printer errors.
 */

static int usblp_check_status(struct usblp *usblp)
{
	unsigned char status;

	if (usblp_read_status(usblp, &status)) {
		err("failed reading usblp status");
		return -EIO;
	}

	if (status & LP_PERRORP) {
	
		if (status & LP_POUTPA) {
			info("usblp%d: out of paper", usblp->minor);
			return -ENOSPC;
		}
		if (~status & LP_PSELECD) {
			info("usblp%d: off-line", usblp->minor);
			return -EIO;
		}
		if (~status & LP_PERRORP) {
			info("usblp%d: on fire", usblp->minor);
			return -EIO;
		}
	}

	return 0;
}

/*
 * File op functions.
 */

static int usblp_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev) - USBLP_MINOR_BASE;
	struct usblp *usblp;
	int retval;

	if (minor < 0 || minor >= USBLP_MINORS)
		return -ENODEV;

	usblp  = usblp_table[minor];

	if (!usblp || !usblp->dev)
		return -ENODEV;

	if (usblp->used)
		return -EBUSY;

	if ((retval = usblp_check_status(usblp)))
		return retval;

	MOD_INC_USE_COUNT;
	usblp->used = 1;
	file->private_data = usblp;

	usblp->writeurb.transfer_buffer_length = 0;
	usblp->writeurb.status = 0;
	usblp->readcount = 0;

	usb_submit_urb(&usblp->readurb);
	return 0;
}

static int usblp_release(struct inode *inode, struct file *file)
{
	struct usblp *usblp = file->private_data;

	MOD_DEC_USE_COUNT;
	usblp->used = 0;
			
	if (usblp->dev) {
        	usb_unlink_urb(&usblp->readurb);
        	usb_unlink_urb(&usblp->writeurb);
		return 0;
	}

	usblp_table[usblp->minor] = NULL;
	kfree(usblp);

	return 0;
}

static unsigned int usblp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usblp *usblp = file->private_data;

	poll_wait(file, &usblp->pollwait, wait);

	return (usblp->readurb.status  == -EINPROGRESS ? 0 : POLLIN  | POLLRDNORM)
	     | (usblp->writeurb.status == -EINPROGRESS ? 0 : POLLOUT | POLLWRNORM);
}

static ssize_t usblp_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct usblp *usblp = file->private_data;
	int retval, timeout, writecount = 0;

	while (writecount < count) {

		if (usblp->writeurb.status == -EINPROGRESS) {

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			timeout = USBLP_WRITE_TIMEOUT;
			while (timeout && usblp->writeurb.status == -EINPROGRESS) {

				if (signal_pending(current))
					return writecount ? writecount : -EINTR;

				timeout = interruptible_sleep_on_timeout(&usblp->writewait, timeout);	
			}
		}

		if (usblp->writeurb.status == -EINPROGRESS) {
			usb_unlink_urb(&usblp->writeurb);
			err("usblp%d: timed out", usblp->minor);
			return -EIO;
		}

		if (!usblp->dev)
			return -ENODEV;

		if (!usblp->writeurb.status)
			writecount += usblp->writeurb.transfer_buffer_length;
		else {
			if (!(retval = usblp_check_status(usblp))) {
				err("usblp%d: error %d writing to printer",
					usblp->minor, usblp->writeurb.status);
				return -EIO;
			}

			return retval;
		}
	
		if (writecount == count)
			continue;

		usblp->writeurb.transfer_buffer_length = (count - writecount) < USBLP_BUF_SIZE ?
							 (count - writecount) : USBLP_BUF_SIZE;

		if (copy_from_user(usblp->writeurb.transfer_buffer, buffer + writecount,
				usblp->writeurb.transfer_buffer_length)) return -EFAULT;

		usb_submit_urb(&usblp->writeurb);
	}

	return count;
}

static ssize_t usblp_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct usblp *usblp = file->private_data;
	DECLARE_WAITQUEUE(wait, current);

	if (!usblp->bidir)
		return -EINVAL;

	if (usblp->readurb.status == -EINPROGRESS) {

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		while (usblp->readurb.status == -EINPROGRESS) {
			if (signal_pending(current))
				return -EINTR;
			interruptible_sleep_on(&usblp->readwait);	
		}
	}

	if (!usblp->dev)
		return -ENODEV;

	if (usblp->readurb.status) {
		err("usblp%d: error %d reading from printer",
			usblp->minor, usblp->readurb.status);
		usb_submit_urb(&usblp->readurb);
		return -EIO;
	}

	count = count < usblp->readurb.actual_length - usblp->readcount ?
		count :	usblp->readurb.actual_length - usblp->readcount;

	if (copy_to_user(buffer, usblp->readurb.transfer_buffer + usblp->readcount, count))
		return -EFAULT;

	if ((usblp->readcount += count) == usblp->readurb.actual_length)
		usb_submit_urb(&usblp->readurb);

	return count;
}

static void *usblp_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *epread, *epwrite;
	struct usblp *usblp;
	int minor, i, alts = -1, bidir = 0;
	char *buf;


	for (i = 0; i < dev->actconfig->interface[ifnum].num_altsetting; i++) {

		interface = &dev->actconfig->interface[ifnum].altsetting[i];

		if (interface->bInterfaceClass != 7 || interface->bInterfaceSubClass != 1 ||
		   (interface->bInterfaceProtocol != 1 && interface->bInterfaceProtocol != 2) ||
		   (interface->bInterfaceProtocol > interface->bNumEndpoints))
			continue;

		if (alts == -1)
			alts = i;

		if (!bidir && interface->bInterfaceProtocol == 2) {
			bidir = 1;
			alts = i;
		}
	}

	if (alts == -1)
		return NULL;

	interface = &dev->actconfig->interface[ifnum].altsetting[alts];
	if (usb_set_interface(dev, ifnum, alts))
		err("can't set desired altsetting %d on interface %d", alts, ifnum);

	epwrite = interface->endpoint + 0;
	epread = NULL;

	if (bidir) {
		epread  = interface->endpoint + 1;
		if ((epread->bEndpointAddress & 0x80) != 0x80) {
			epwrite = interface->endpoint + 1;
			epread  = interface->endpoint + 0;

			if ((epread->bEndpointAddress & 0x80) != 0x80)
				return NULL;
		}
	}

	if ((epwrite->bEndpointAddress & 0x80) == 0x80)
		return NULL;

	for (minor = 0; minor < USBLP_MINORS && usblp_table[minor]; minor++);
	if (usblp_table[minor]) {
		err("no more free usblp devices");
		return NULL;
	}

	if (!(usblp = kmalloc(sizeof(struct usblp), GFP_KERNEL))) {
		err("out of memory");
		return NULL;
	}
	memset(usblp, 0, sizeof(struct usblp));

	usblp->dev = dev;
	usblp->ifnum = ifnum;
	usblp->minor = minor;
	usblp->bidir = bidir;

	init_waitqueue_head(&usblp->writewait);
	init_waitqueue_head(&usblp->readwait);
	init_waitqueue_head(&usblp->pollwait);

	if (!(buf = kmalloc(USBLP_BUF_SIZE * (bidir ? 2 : 1), GFP_KERNEL))) {
		err("out of memory");
		kfree(usblp);
		return NULL;
	}

	FILL_BULK_URB(&usblp->writeurb, dev, usb_sndbulkpipe(dev, epwrite->bEndpointAddress),
		buf, 0, usblp_bulk, usblp);

	if (bidir) {
		FILL_BULK_URB(&usblp->readurb, dev, usb_rcvbulkpipe(dev, epread->bEndpointAddress),
			buf + USBLP_BUF_SIZE, USBLP_BUF_SIZE, usblp_bulk, usblp);
	}

	printk(KERN_INFO "usblp%d: USB %sdirectional printer dev %d if %d alt %d\n",
		minor, bidir ? "Bi" : "Uni", dev->devnum, ifnum, alts);

	return usblp_table[minor] = usblp;
}

static void usblp_disconnect(struct usb_device *dev, void *ptr)
{
	struct usblp *usblp = ptr;

	if (!usblp || !usblp->dev) {
		err("disconnect on nonexisting interface");
		return;
	}

	usblp->dev = NULL;

	usb_unlink_urb(&usblp->readurb);
	usb_unlink_urb(&usblp->writeurb);

	kfree(usblp->writeurb.transfer_buffer);

	if (usblp->used) return;

	usblp_table[usblp->minor] = NULL;
	kfree(usblp);
}

static struct file_operations usblp_fops = {
	read:		usblp_read,
	write:		usblp_write,
	open:		usblp_open,
	release:	usblp_release,
	poll:		usblp_poll
};

static struct usb_driver usblp_driver = {
	name:		"usblp",
	probe:		usblp_probe,
	disconnect:	usblp_disconnect,
	fops:		&usblp_fops,
	minor:		USBLP_MINOR_BASE
};

#ifdef MODULE
void cleanup_module(void)
{
	usb_deregister(&usblp_driver);
}
int init_module(void)
#else
int usb_printer_init(void)
#endif
{
	if (usb_register(&usblp_driver))
		return -1;

	return 0;
}
