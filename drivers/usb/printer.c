/*
 * printer.c  Version 0.5
 *
 * Copyright (c) 1999 Michael Gee	<michael@linuxspecific.com>
 * Copyright (c) 1999 Pavel Machek	<pavel@suse.cz>
 * Copyright (c) 2000 Vojtech Pavlik	<vojtech@suse.cz>
 * Copyright (c) 2000 Randy Dunlap	<randy.dunlap@intel.com>
 *
 * USB Printer Device Class driver for USB printers and printer cables
 *
 * Sponsored by SuSE
 *
 * ChangeLog:
 *	v0.1 - thorough cleaning, URBification, almost a rewrite
 *	v0.2 - some more cleanups
 *	v0.3 - cleaner again, waitqueue fixes
 *	v0.4 - fixes in unidirectional mode
 *	v0.5 - add DEVICE_ID string support
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
#include <linux/usb.h>

#define USBLP_BUF_SIZE		8192
#define DEVICE_ID_SIZE		1024

#define IOCNR_GET_DEVICE_ID	1
#define LPIOC_GET_DEVICE_ID(len) _IOC(_IOC_READ, 'P', IOCNR_GET_DEVICE_ID, len)	/* get device_id string */

/*
 * A DEVICE_ID string may include the printer's serial number.
 * It should end with a semi-colon (';').
 * An example from an HP 970C DeskJet printer is (this is one long string,
 * with the serial number changed):
MFG:HEWLETT-PACKARD;MDL:DESKJET 970C;CMD:MLC,PCL,PML;CLASS:PRINTER;DESCRIPTION:Hewlett-Packard DeskJet 970C;SERN:US970CSEPROF;VSTATUS:$HB0$NC0,ff,DN,IDLE,CUT,K1,C0,DP,NR,KP000,CP027;VP:0800,FL,B0;VJ:                    ;
 */

/*
 * USB Printer Requests
 */

#define USBLP_REQ_GET_ID	0x00
#define USBLP_REQ_GET_STATUS	0x01
#define USBLP_REQ_RESET		0x02

#define USBLP_MINORS		16
#define USBLP_MINOR_BASE	0

#define USBLP_WRITE_TIMEOUT	(60*60*HZ)		/* 60 minutes */

struct usblp {
	struct usb_device 	*dev;			/* USB device */
	struct urb		readurb, writeurb;	/* The urbs */
	wait_queue_head_t	wait;			/* Zzzzz ... */
	int			readcount;		/* Counter for reads */
	int			ifnum;			/* Interface number */
	int			minor;			/* minor number of device */
	unsigned char		used;			/* True if open */
	unsigned char		bidir;			/* interface is bidirectional */
	unsigned char		*device_id_string;	/* IEEE 1284 DEVICE ID string (ptr) */
					/* first 2 bytes are (big-endian) length */
};

static struct usblp *usblp_table[USBLP_MINORS] = { NULL, /* ... */ };

/*
 * Functions for usblp control messages.
 */

static int usblp_ctrl_msg(struct usblp *usblp, int request, int dir, int recip, int value, void *buf, int len)
{
	int retval = usb_control_msg(usblp->dev,
		dir ? usb_rcvctrlpipe(usblp->dev, 0) : usb_sndctrlpipe(usblp->dev, 0),
		request, USB_TYPE_CLASS | dir | recip, value, usblp->ifnum, buf, len, HZ * 5);
	dbg("usblp_control_msg: rq: 0x%02x dir: %d recip: %d value: %d len: %#x result: %d",
		request, !!dir, recip, value, len, retval);
	return retval < 0 ? retval : 0;
}

#define usblp_read_status(usblp, status)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_STATUS, USB_DIR_IN, USB_RECIP_INTERFACE, 0, status, 1)
#define usblp_get_id(usblp, config, id, maxlen)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_ID, USB_DIR_IN, USB_RECIP_INTERFACE, config, id, maxlen)
#define usblp_reset(usblp)\
	usblp_ctrl_msg(usblp, USBLP_REQ_RESET, USB_DIR_OUT, USB_RECIP_OTHER, 0, NULL, 0)

/*
 * URB callback.
 */

static void usblp_bulk(struct urb *urb)
{
	struct usblp *usblp = urb->context;

	if (!usblp || !usblp->dev || !usblp->used)
		return;

	if (urb->status)
		warn("nonzero read/write bulk status received: %d", urb->status);

	wake_up_interruptible(&usblp->wait);
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

	if (~status & LP_PERRORP) {
		if (status & LP_POUTPA) {
			info("usblp%d: out of paper", usblp->minor);
			return -ENOSPC;
		}
		if (~status & LP_PSELECD) {
			info("usblp%d: off-line", usblp->minor);
			return -EIO;
		}
		info("usblp%d: on fire", usblp->minor);
		return -EIO;
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

	if ((retval = usblp_check_status(usblp))) {
		return retval;
	}

	usblp->used = 1;
	file->private_data = usblp;

	usblp->writeurb.transfer_buffer_length = 0;
	usblp->writeurb.status = 0;

	if (usblp->bidir) {
		usblp->readcount = 0;
		usb_submit_urb(&usblp->readurb);
	}

	return 0;
}

static int usblp_release(struct inode *inode, struct file *file)
{
	struct usblp *usblp = file->private_data;

	usblp->used = 0;

	if (usblp->dev) {
		if (usblp->bidir)
			usb_unlink_urb(&usblp->readurb);
		usb_unlink_urb(&usblp->writeurb);
		return 0;
	}

	usblp_table[usblp->minor] = NULL;
	kfree(usblp->device_id_string);
	kfree(usblp);

	return 0;
}

/* No kernel lock - fine */
static unsigned int usblp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usblp *usblp = file->private_data;
	poll_wait(file, &usblp->wait, wait);
	return ((usblp->bidir || usblp->readurb.status  == -EINPROGRESS) ? 0 : POLLIN  | POLLRDNORM)
	     		      | (usblp->writeurb.status == -EINPROGRESS  ? 0 : POLLOUT | POLLWRNORM);
}

static int usblp_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int	length;
	struct usblp *usblp = file->private_data;

	if ((_IOC_TYPE(cmd) != 'P') || (_IOC_DIR(cmd) != _IOC_READ))
		return -EINVAL;

	switch (_IOC_NR(cmd)) {
	case IOCNR_GET_DEVICE_ID:		/* get the DEVICE_ID string */
		length =  (usblp->device_id_string[0] << 8) + usblp->device_id_string[1]; /* big-endian */
#if 0
		dbg ("usblp_ioctl GET_DEVICE_ID: actlen=%d, user size=%d, string='%s'",
			length, _IOC_SIZE(cmd), &usblp->device_id_string[2]);
#endif
		if (length > _IOC_SIZE(cmd))
			length = _IOC_SIZE(cmd);	/* truncate */
		if (copy_to_user ((unsigned char *)arg, usblp->device_id_string, (unsigned long) length))
		    	return -EFAULT;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static ssize_t usblp_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
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

				timeout = interruptible_sleep_on_timeout(&usblp->wait, timeout);
			}
		}

		if (usblp->writeurb.status == -EINPROGRESS) {
			usb_unlink_urb(&usblp->writeurb);
			err("usblp%d: timed out", usblp->minor);
			return -EIO;
		}

		if (!usblp->dev)
			return -ENODEV;

		if (!usblp->writeurb.status) {
			writecount += usblp->writeurb.transfer_buffer_length;
			usblp->writeurb.transfer_buffer_length = 0;
		} else {
			if (!(retval = usblp_check_status(usblp))) {
				err("usblp%d: error %d writing to printer (retval=%d)",
					usblp->minor, usblp->writeurb.status, retval);
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

	if (!usblp->bidir)
		return -EINVAL;

	if (usblp->readurb.status == -EINPROGRESS) {

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		while (usblp->readurb.status == -EINPROGRESS) {
			if (signal_pending(current))
				return -EINTR;
			interruptible_sleep_on(&usblp->wait);
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
	int length, err;
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
	epread = bidir ? interface->endpoint + 1 : NULL;

	if ((epwrite->bEndpointAddress & 0x80) == 0x80) {
		if (interface->bNumEndpoints == 1)
			return NULL;
		epwrite = interface->endpoint + 1;
		epread = bidir ? interface->endpoint + 0 : NULL;
	}

	if ((epwrite->bEndpointAddress & 0x80) == 0x80)
		return NULL;

	if (bidir && (epread->bEndpointAddress & 0x80) != 0x80)
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

	init_waitqueue_head(&usblp->wait);

	if (!(buf = kmalloc(USBLP_BUF_SIZE * (bidir ? 2 : 1), GFP_KERNEL))) {
		err("out of memory");
		kfree(usblp);
		return NULL;
	}

	if (!(usblp->device_id_string = kmalloc(DEVICE_ID_SIZE, GFP_KERNEL))) {
		err("out of memory");
		kfree(usblp);
		kfree(buf);
		return NULL;
	}

	FILL_BULK_URB(&usblp->writeurb, dev, usb_sndbulkpipe(dev, epwrite->bEndpointAddress),
		buf, 0, usblp_bulk, usblp);

	if (bidir)
		FILL_BULK_URB(&usblp->readurb, dev, usb_rcvbulkpipe(dev, epread->bEndpointAddress),
			buf + USBLP_BUF_SIZE, USBLP_BUF_SIZE, usblp_bulk, usblp);

	/* Get the device_id string if possible. FIXME: Could make this kmalloc(length). */
	err = usblp_get_id(usblp, 0, usblp->device_id_string, DEVICE_ID_SIZE - 1);
	if (err >= 0) {
		length = (usblp->device_id_string[0] << 8) + usblp->device_id_string[1]; /* big-endian */
		if (length < DEVICE_ID_SIZE)
			usblp->device_id_string[length] = '\0';
		else
			usblp->device_id_string[DEVICE_ID_SIZE - 1] = '\0';
		dbg ("usblp%d Device ID string [%d]=%s",
			minor, length, &usblp->device_id_string[2]);
	}
	else {
		err ("usblp%d: error = %d reading IEEE-1284 Device ID string",
			minor, err);
		usblp->device_id_string[0] = usblp->device_id_string[1] = '\0';
	}

#ifdef DEBUG
	usblp_check_status(usblp);
#endif

	info("usblp%d: USB %sdirectional printer dev %d if %d alt %d",
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

	usb_unlink_urb(&usblp->writeurb);
	if (usblp->bidir)
		usb_unlink_urb(&usblp->readurb);

	kfree(usblp->writeurb.transfer_buffer);

	if (usblp->used) return;

	kfree(usblp->device_id_string);

	usblp_table[usblp->minor] = NULL;
	kfree(usblp);
}

static struct file_operations usblp_fops = {
	owner:		THIS_MODULE,
	read:		usblp_read,
	write:		usblp_write,
	poll:		usblp_poll,
	ioctl:		usblp_ioctl,
	open:		usblp_open,
	release:	usblp_release,
};

static struct usb_driver usblp_driver = {
	name:		"usblp",
	probe:		usblp_probe,
	disconnect:	usblp_disconnect,
	fops:		&usblp_fops,
	minor:		USBLP_MINOR_BASE
};

static int __init usblp_init(void)
{
	if (usb_register(&usblp_driver))
		return -1;
	return 0;
}

static void __exit usblp_exit(void)
{
	usb_deregister(&usblp_driver);
}

module_init(usblp_init);
module_exit(usblp_exit);
