/*
 * USB ZyXEL omni.net LCD PLUS driver
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 */

#include <linux/config.h>

#ifdef CONFIG_USB_SERIAL_OMNINET

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
#include <linux/spinlock.h>


#ifdef CONFIG_USB_SERIAL_DEBUG
	#define isalpha(x) ( ( x > 96 && x < 123) || ( x > 64 && x < 91) || (x > 47 && x < 58) )
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/usb.h>

#include "usb-serial.h"


#define ZYXEL_VENDOR_ID		0x0586
#define ZYXEL_OMNINET_ID	0x1000

/* function prototypes */
static int  omninet_open		(struct usb_serial_port *port, struct file *filp);
static void omninet_close		(struct usb_serial_port *port, struct file *filp);
static void omninet_read_bulk_callback	(struct urb *urb);
static void omninet_write_bulk_callback	(struct urb *urb);
static int  omninet_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static int  omninet_write_room		(struct usb_serial_port *port);

/* All of the device info needed for the omni.net */
static __u16	zyxel_vendor_id			= ZYXEL_VENDOR_ID;
static __u16	zyxel_omninet_product_id	= ZYXEL_OMNINET_ID;

struct usb_serial_device_type zyxel_omninet_device = {
	name:			"ZyXEL - omni.net lcd plus usb",
	idVendor:		&zyxel_vendor_id,
	idProduct:		&zyxel_omninet_product_id,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		2,
	num_ports:		1,
	open:			omninet_open,
	close:			omninet_close,
	write:			omninet_write,
	write_room:		omninet_write_room,
	read_bulk_callback:	omninet_read_bulk_callback,
	write_bulk_callback:	omninet_write_bulk_callback,
};


/* The protocol.
 *
 * The omni.net always exchange 64 bytes of data with the host. The first
 * four bytes are the control header, you can see it in the above structure.
 *
 * oh_seq is a sequence number. Don't know if/how it's used.
 * oh_len is the length of the data bytes in the packet.
 * oh_xxx Bit-mapped, related to handshaking and status info.
 *	I normally set it to 0x03 in trasmitted frames.
 *	7: Active when the TA is in a CONNECTed state.
 *	6: unknown
 *	5: handshaking, unknown
 *	4: handshaking, unknown
 *	3: unknown, usually 0
 *	2: unknown, usually 0
 *	1: handshaking, unknown, usually set to 1 in trasmitted frames
 *	0: handshaking, unknown, usually set to 1 in trasmitted frames
 * oh_pad Probably a pad byte.
 *
 * After the header you will find data bytes if oh_len was greater than zero.
 *
 */

struct omninet_header
{
	__u8	oh_seq;
	__u8	oh_len;
	__u8	oh_xxx;
	__u8	oh_pad;
};

struct omninet_data
{
	__u8	od_outseq;	// Sequence number for bulk_out URBs
};

static int omninet_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial 	*serial = port->serial;
	struct usb_serial_port 	*wport	= &serial->port[1];
	struct omninet_data 	*od;

	dbg("omninet_open port %d", port->number);

	if (port->active) {
		dbg ("device already open");
		return -EINVAL;
	}
	port->active = 1;

	od = kmalloc( sizeof(struct omninet_data), GFP_KERNEL );

	if( !od )
	{
		err("omninet_open: kmalloc(%d) failed.", sizeof(struct omninet_data));
		return -ENOMEM;
	}

	port->private = od;

	/* Start reading from the device */
	if (usb_submit_urb(port->read_urb))
		dbg("usb_submit_urb(read bulk, %p) failed", port->read_urb);

	wport->tty = port->tty;

	return (0);
}

static void omninet_close (struct usb_serial_port *port, struct file * filp)
{
	struct usb_serial 	*serial = port->serial;
	struct usb_serial_port 	*wport 	= &serial->port[1];
	struct omninet_data 	*od 	= (struct omninet_data *) port->private;

	port->active = 0;


	dbg("zyxel_close port %d", port->number);

	usb_unlink_urb (wport->write_urb);
	usb_unlink_urb (port->read_urb);

	if(od) kfree(od);
}


#define OMNINET_DATAOFFSET	0x04
#define OMNINET_HEADERLEN	sizeof(struct omninet_header)
#define OMNINET_BULKOUTSIZE 	(64 - OMNINET_HEADERLEN)

static void omninet_read_bulk_callback (struct urb *urb)
{
	struct usb_serial_port 	*port 	= (struct usb_serial_port *)urb->context;
	struct usb_serial 	*serial = port->serial;

	unsigned char 		*data 	= urb->transfer_buffer;
	struct omninet_header 	*header = (struct omninet_header *) &data[0];

	int i;

//	dbg("omninet_read_bulk_callback");

	if (port_paranoia_check (port, "omninet_read_bulk_callback")) {
		return;
	}

	if (serial_paranoia_check (serial, "omninet_read_bulk_callback")) {
		return;
	}

	if (urb->status) {
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

#ifdef DEBUG
	if(header->oh_xxx != 0x30)
	{
		if (urb->actual_length) {
			printk (KERN_DEBUG __FILE__ ": omninet_read %d: ", header->oh_len);
			for (i = 0; i < (header->oh_len + OMNINET_HEADERLEN); i++) {
				printk ("%.2x ", data[i]);
			}
			printk ("\n");
		}
	}
#endif

	if (urb->actual_length && header->oh_len)
	{
		for (i = 0; i < header->oh_len; i++) {
			 tty_insert_flip_char(port->tty, data[OMNINET_DATAOFFSET + i], 0);
	  	}
	  	tty_flip_buffer_push(port->tty);
	}

	/* Continue trying to always read  */
	if (usb_submit_urb(urb))
		dbg("failed resubmitting read urb");

	return;
}

static int omninet_write (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial 	*serial	= port->serial;
	struct usb_serial_port 	*wport	= &serial->port[1];

	struct omninet_data 	*od 	= (struct omninet_data   *) port->private;
	struct omninet_header	*header = (struct omninet_header *) wport->write_urb->transfer_buffer;
/*
#ifdef DEBUG
	int i;
#endif
*/

//	dbg("omninet_write port %d", port->number);

	if (count == 0) {
		dbg("write request of 0 bytes");
		return (0);
	}
/*
#ifdef DEBUG
	printk (KERN_DEBUG __FILE__ ": omninet_write %d: ", count);
		for (i = 0; i < count; i++) {
			if( isalpha(buf[i]) )
				printk ("%c ", buf[i]);
			else
				printk ("%.2x ", buf[i]);
		}
		printk ("\n");
#endif
*/
	if (wport->write_urb->status == -EINPROGRESS) {
		dbg ("already writing");
		return (0);
	}

	count = (count > OMNINET_BULKOUTSIZE) ? OMNINET_BULKOUTSIZE : count;

	if (from_user) {
		copy_from_user(wport->write_urb->transfer_buffer + OMNINET_DATAOFFSET, buf, count);
	}
	else {
		memcpy (wport->write_urb->transfer_buffer + OMNINET_DATAOFFSET, buf, count);
	}


	header->oh_seq 	= od->od_outseq++;
	header->oh_len 	= count;
	header->oh_xxx  = 0x03;
	header->oh_pad 	= 0x00;

	/* send the data out the bulk port, always 64 bytes */
	wport->write_urb->transfer_buffer_length = 64;

	if (usb_submit_urb(wport->write_urb))
		dbg("usb_submit_urb(write bulk) failed");

//	dbg("omninet_write returns %d", count);

	return (count);
}


static int omninet_write_room (struct usb_serial_port *port)
{
	struct usb_serial 	*serial = port->serial;
	struct usb_serial_port 	*wport 	= &serial->port[1];

	int room = 0; // Default: no room

	if (wport->write_urb->status != -EINPROGRESS)
		room = wport->bulk_out_size - OMNINET_HEADERLEN;

//	dbg("omninet_write_room returns %d", room);

	return (room);
}

static void omninet_write_bulk_callback (struct urb *urb)
{
/*	struct omninet_header	*header = (struct omninet_header  *) urb->transfer_buffer; */
	struct usb_serial_port 	*port   = (struct usb_serial_port *) urb->context;
	struct usb_serial 	*serial;
	struct tty_struct 	*tty;

//	dbg("omninet_write_bulk_callback, port %0x\n", port);


	if (port_paranoia_check (port, "omninet_write_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "omninet_write_bulk_callback")) {
		return;
	}

	if (urb->status) {
		dbg("nonzero write bulk status received: %d", urb->status);
		return;
	}

	tty = port->tty;

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);

//	dbg("omninet_write_bulk_callback, tty %0x\n", tty);

	return;
}

#endif	/* CONFIG_USB_SERIAL_OMNINET */


