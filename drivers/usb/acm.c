/*
 * acm.c  Version 0.10
 *
 * Copyright (c) 1999 Armin Fuerst	<fuerst@in.tum.de>
 * Copyright (c) 1999 Pavel Machek	<pavel@suse.cz>
 * Copyright (c) 1999 Johannes Erdfelt	<jerdfelt@valinux.com>
 * Copyright (c) 1999 Vojtech Pavlik	<vojtech@suse.cz>
 *
 * USB Abstract Control Model driver for USB modems and ISDN adapters
 *
 * Sponsored by SuSE
 *
 * ChangeLog:
 *	v0.9  Vojtech Pavlik - thorough cleaning, URBification, almost a rewrite
 *      v0.10 Vojtech Pavlik - some more cleanups
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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/config.h>

#include "usb.h"

#ifdef CONFIG_USB_ACM_DEBUG
#define acm_debug(fmt,arg...)	printk(KERN_DEBUG "acm: " fmt "\n" , ##arg)
#else
#define acm_debug(fmt,arg...)	do {} while(0)
#endif

/*
 * Major and minor numbers.
 */

#define ACM_TTY_MAJOR		166
#define ACM_TTY_MINORS		8

/*
 * Output control lines.
 */

#define ACM_CTRL_DTR		0x01
#define ACM_CTRL_RTS		0x02

/*
 * Input control lines and line errors.
 */

#define ACM_CTRL_DCD		0x01
#define ACM_CTRL_DSR		0x02
#define ACM_CTRL_BRK		0x04
#define ACM_CTRL_RI		0x08

#define ACM_CTRL_FRAMING	0x10
#define ACM_CTRL_PARITY		0x20
#define ACM_CTRL_OVERRUN	0x40

/*
 * Line speed and caracter encoding.
 */

struct acm_coding {
	__u32 speed;
	__u8 stopbits;
	__u8 parity;
	__u8 databits;
} __attribute__ ((packed));

/*
 * Internal driver structures.
 */

struct acm {
	struct usb_device *dev;				/* the coresponding usb device */
	struct usb_config_descriptor *cfg;		/* configuration number on this device */
	struct tty_struct *tty;				/* the coresponding tty */
	unsigned int ctrlin;				/* input control lines (DCD, DSR, RI, break, overruns) */
	unsigned int ctrlout;				/* output control lines (DTR, RTS) */
	struct acm_coding linecoding;			/* line coding (bits, stop, parity) */
	unsigned int writesize;				/* max packet size for the output bulk endpoint */
	struct urb ctrlurb, readurb, writeurb;		/* urbs */
	unsigned char present;				/* this device is connected to the usb bus */
	unsigned char used;				/* someone has this acm's device open */
};

static struct usb_driver acm_driver;
static struct acm acm_table[ACM_TTY_MINORS];

#define ACM_READY(acm)	(acm->present && acm->used)

/*
 * Functions for ACM control messages.
 */

static void acm_set_control(unsigned int status, struct acm *acm)
{
	if (usb_control_msg(acm->dev, usb_sndctrlpipe(acm->dev, 0), 0x22, 0x22, status, 0, NULL, 0, HZ) < 0)
		acm_debug("acm_set_control() failed");

	acm_debug("output control lines: dtr%c rts%c",
		acm->ctrlout & ACM_CTRL_DTR ? '+' : '-', acm->ctrlout & ACM_CTRL_RTS ? '+' : '-');
}

#if 0
static void acm_set_coding(struct acm_coding *coding, struct acm *acm)
{
	if (usb_control_msg(acm->dev, usb_sndctrlpipe(acm->dev, 0), 0x30, 0x22, 0, 0, coding, sizeof(struct acm_coding), HZ) < 0)
		acm_debug("acm_set_coding() failed");
}

static void acm_send_break(int ms, struct acm *acm)
{
	if (usb_control_msg(acm->dev, usb_sndctrlpipe(acm->dev, 0), 0x30, 0x33, ms, 0, NULL, 0, HZ) < 0)
		acm_debug("acm_send_break() failed");
}
#endif

/*
 * Interrupt handler for various ACM control events
 */

static void acm_ctrl_irq(struct urb *urb)
{
	struct acm *acm = urb->context;
	devrequest *dr = urb->transfer_buffer;
	unsigned char *data = (unsigned char *)(dr + 1);

	if (urb->status < 0) {
		acm_debug("nonzero ctrl irq status received: %d", urb->status);
		return;
	}

	if (!ACM_READY(acm)) return;

	switch (dr->request) {

		case 0x20: /* Set serial line state */

			if ((dr->index != 1) || (dr->length != 2)) {
				acm_debug("unknown set serial line request: index %d len %d", dr->index, dr->length);
				return;
			}

			acm->ctrlin = data[0] | (((unsigned int) data[1]) << 8);

			acm_debug("input control lines: dcd%c dsr%c break%c ring%c framing%c parity%c overrun%c",
				acm->ctrlin & ACM_CTRL_DCD ? '+' : '-',	acm->ctrlin & ACM_CTRL_DSR ? '+' : '-',
				acm->ctrlin & ACM_CTRL_BRK ? '+' : '-',	acm->ctrlin & ACM_CTRL_RI  ? '+' : '-',
				acm->ctrlin & ACM_CTRL_FRAMING ? '+' : '-',	acm->ctrlin & ACM_CTRL_PARITY ? '+' : '-',
				acm->ctrlin & ACM_CTRL_OVERRUN ? '+' : '-');

			return;

		default:
			acm_debug("unknown control event received: request %d index %d len %d data0 %d data1 %d",
				dr->request, dr->index, dr->length, data[0], data[1]);
			return;
	}

	return;
}

static void acm_read_bulk(struct urb *urb)
{
	struct acm *acm = urb->context;
	struct tty_struct *tty = acm->tty;
	unsigned char *data = urb->transfer_buffer;
	int i;

	if (urb->status) {
		acm_debug("nonzero read bulk status received: %d", urb->status);
		return;
	}

	if (!ACM_READY(acm)) return;

	for (i = 0; i < urb->actual_length; i++)
		tty_insert_flip_char(tty, data[i], 0);

	tty_flip_buffer_push(tty);

	if (usb_submit_urb(urb))
		acm_debug("failed resubmitting read urb");

	return;
}

static void acm_write_bulk(struct urb *urb)
{
	struct acm *acm = (struct acm *)urb->context;
	struct tty_struct *tty = acm->tty;

	if (urb->status) {
		acm_debug("nonzero write bulk status received: %d", urb->status);
		return;
	}

	if (!ACM_READY(acm)) return;

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);

	return;
}

/*
 * TTY handlers
 */

static int acm_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct acm *acm = &acm_table[MINOR(tty->device)];

	tty->driver_data = acm;
	acm->tty = tty;

	if (!acm->present) return -EINVAL;

	if (acm->used++) {
		MOD_INC_USE_COUNT;
		return 0;
	}

	MOD_INC_USE_COUNT;

	if (usb_submit_urb(&acm->ctrlurb))
		acm_debug("usb_submit_urb(ctrl irq) failed");

	if (usb_submit_urb(&acm->readurb))
		acm_debug("usb_submit_urb(read bulk) failed");

	acm_set_control(acm->ctrlout = ACM_CTRL_DTR | ACM_CTRL_RTS, acm);

	return 0;
}

static void acm_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct acm *acm = tty->driver_data;

	if (!ACM_READY(acm)) return;

	if (--acm->used) {
		MOD_DEC_USE_COUNT;
		return;
	}

	acm_set_control(acm->ctrlout = 0, acm);
	usb_unlink_urb(&acm->writeurb);
	usb_unlink_urb(&acm->readurb);

	MOD_DEC_USE_COUNT;
}

static int acm_tty_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
	struct acm *acm = tty->driver_data;

	if (!ACM_READY(acm)) return -EINVAL;
	if (acm->writeurb.status == -EINPROGRESS) return 0;

	count = (count > acm->writesize) ? acm->writesize : count;

	if (from_user)
		copy_from_user(acm->writeurb.transfer_buffer, buf, count);
	else
		memcpy(acm->writeurb.transfer_buffer, buf, count);

	acm->writeurb.transfer_buffer_length = count;

	if (usb_submit_urb(&acm->writeurb))
		acm_debug("usb_submit_urb(write bulk) failed");

	return count;
}

static int acm_tty_write_room(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;
	if (!ACM_READY(acm)) return -EINVAL;
	return acm->writeurb.status == -EINPROGRESS ? 0 : acm->writesize;
}

static int acm_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;
	if (!ACM_READY(acm)) return -EINVAL;
	return acm->writeurb.status == -EINPROGRESS ? acm->writesize : 0;
}

static void acm_tty_throttle(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;
	if (!ACM_READY(acm)) return;
	if (tty->termios->c_cflag & CRTSCTS)
		acm_set_control(acm->ctrlout &= ~ACM_CTRL_RTS, acm);
}

static void acm_tty_unthrottle(struct tty_struct *tty)
{
	struct acm *acm = tty->driver_data;
	if (!ACM_READY(acm)) return;
	if (tty->termios->c_cflag & CRTSCTS)
		acm_set_control(acm->ctrlout |= ACM_CTRL_RTS, acm);
}

static void acm_tty_set_termios(struct tty_struct *tty, struct termios *old)
{
	acm_debug("set_termios called, but not there yet");
	return;
}

/*
 * USB probe and disconnect routines.
 */

static void *acm_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct acm *acm;
	struct usb_interface_descriptor *ifcom, *ifdata;
	struct usb_endpoint_descriptor *epctrl, *epread, *epwrite;
	int readsize, ctrlsize, minor, i;
	unsigned char *buf;
	char *s = NULL;

	for (minor = 0; minor < ACM_TTY_MINORS &&
		(acm_table[minor].present || acm_table[minor].used); minor++);
	if (acm_table[minor].present || acm_table[minor].used) {
		acm_debug("no more free acm devices");
		return NULL;
	}
	acm = acm_table + minor;
	memset(acm, 0, sizeof(struct acm));

	acm->dev = dev;

	if (dev->descriptor.bDeviceClass != 2 || dev->descriptor.bDeviceSubClass != 0
		|| dev->descriptor.bDeviceProtocol != 0) {
		return NULL;
	}

	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {

		acm_debug("probing config %d", i);
		acm->cfg = dev->config + i;

		ifcom = acm->cfg->interface[0].altsetting + 0;
		if (ifcom->bInterfaceClass != 2 || ifcom->bInterfaceSubClass != 2 ||
		    ifcom->bInterfaceProtocol != 1 || ifcom->bNumEndpoints != 1)
			continue;

		epctrl = ifcom->endpoint + 0;
		if ((epctrl->bEndpointAddress & 0x80) != 0x80 || (epctrl->bmAttributes & 3) != 3)
			continue;

		ifdata = acm->cfg->interface[1].altsetting + 0;
		if (ifdata->bInterfaceClass != 10 || ifdata->bNumEndpoints != 2)
			continue;

		if (usb_interface_claimed(acm->cfg->interface + 0) ||
		    usb_interface_claimed(acm->cfg->interface + 1))
			continue;

		epread = ifdata->endpoint + 0;
		epwrite = ifdata->endpoint + 1;

		if ((epread->bmAttributes & 3) != 2 || (epwrite->bmAttributes & 3) != 2 ||
		   ((epread->bEndpointAddress & 0x80) ^ (epwrite->bEndpointAddress & 0x80)) != 0x80)
			continue;

		if ((epread->bEndpointAddress & 0x80) != 0x80) {
			epread = ifdata->endpoint + 1;
			epwrite = ifdata->endpoint + 0;
		}

		usb_set_configuration(dev, acm->cfg->bConfigurationValue);

		ctrlsize = epctrl->wMaxPacketSize;
		readsize = epread->wMaxPacketSize;
		acm->writesize = epwrite->wMaxPacketSize;

		if (!(buf = kmalloc(ctrlsize + readsize + acm->writesize, GFP_KERNEL)))
			return NULL;

		FILL_INT_URB(&acm->ctrlurb, dev, usb_rcvintpipe(dev, epctrl->bEndpointAddress),
			buf, ctrlsize, acm_ctrl_irq, acm, epctrl->bInterval);

		FILL_BULK_URB(&acm->readurb, dev, usb_rcvbulkpipe(dev, epread->bEndpointAddress),
			buf += ctrlsize, readsize, acm_read_bulk, acm);

		FILL_BULK_URB(&acm->writeurb, dev, usb_sndbulkpipe(dev, epwrite->bEndpointAddress),
			buf += readsize , acm->writesize, acm_write_bulk, acm);

		printk(KERN_INFO "ttyACM%d: USB ACM device\n", minor);

		usb_driver_claim_interface(&acm_driver, acm->cfg->interface + 0, acm);
		usb_driver_claim_interface(&acm_driver, acm->cfg->interface + 1, acm);

		acm->present = 1;

		return acm;
	}

	return NULL;
}

static void acm_disconnect(struct usb_device *dev, void *ptr)
{
	struct acm *acm = ptr;

	if (!acm->present) {
		acm_debug("disconnect on nonexisting interface");
		return;
	}

	acm->present = 0;

	usb_unlink_urb(&acm->ctrlurb);
	usb_unlink_urb(&acm->readurb);
	usb_unlink_urb(&acm->writeurb);

	kfree(acm->ctrlurb.transfer_buffer);

	usb_driver_release_interface(&acm_driver, acm->cfg->interface + 0);
	usb_driver_release_interface(&acm_driver, acm->cfg->interface + 1);
}

/*
 * USB driver structure.
 */

static struct usb_driver acm_driver = {
	name:		"acm",
	probe:		acm_probe,
	disconnect:	acm_disconnect
};

/*
 * TTY driver structures.
 */

static int acm_tty_refcount;

static struct tty_struct *acm_tty_table[ACM_TTY_MINORS];
static struct termios *acm_tty_termios[ACM_TTY_MINORS];
static struct termios *acm_tty_termios_locked[ACM_TTY_MINORS];

static struct tty_driver acm_tty_driver = {
	magic:			TTY_DRIVER_MAGIC,
	driver_name:		"usb",
	name:			"ttyACM",
	major:			ACM_TTY_MAJOR,
	minor_start:		0,
	num:			ACM_TTY_MINORS,
	type:			TTY_DRIVER_TYPE_SERIAL,
	subtype:		SERIAL_TYPE_NORMAL,
	flags:			TTY_DRIVER_REAL_RAW,

	refcount:		&acm_tty_refcount,

	table:			acm_tty_table,
	termios:		acm_tty_termios,
	termios_locked:		acm_tty_termios_locked,

	open:			acm_tty_open,
	close:			acm_tty_close,
	write:			acm_tty_write,
	write_room:		acm_tty_write_room,
	set_termios:		acm_tty_set_termios,
	throttle:		acm_tty_throttle,
	unthrottle:		acm_tty_unthrottle,
	chars_in_buffer:	acm_tty_chars_in_buffer
};

/*
 * Init / cleanup.
 */

#ifdef MODULE
void cleanup_module(void)
{
	usb_deregister(&acm_driver);
	tty_unregister_driver(&acm_tty_driver);
}

int init_module(void)
#else
int usb_acm_init(void)
#endif
{
	memset(acm_table, 0, sizeof(struct acm) * ACM_TTY_MINORS);

	acm_tty_driver.init_termios =		tty_std_termios;
	acm_tty_driver.init_termios.c_cflag =	B9600 | CS8 | CREAD | HUPCL | CLOCAL;

	if (tty_register_driver(&acm_tty_driver))
		return -1;

	if (usb_register(&acm_driver) < 0) {
		tty_unregister_driver(&acm_tty_driver);
		return -1;
	}

	return 0;
}

