/*
 * USB HandSpring Visor driver
 *
 *	Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 * (07/23/2000) gkh
 *	Added pool of write urbs to speed up transfers to the visor.
 * 
 * (07/19/2000) gkh
 *	Added module_init and module_exit functions to handle the fact that this
 *	driver is a loadable module now.
 *
 * (07/03/2000) gkh
 *	Added visor_set_ioctl and visor_set_termios functions (they don't do much
 *	of anything, but are good for debugging.)
 * 
 * (06/25/2000) gkh
 *	Fixed bug in visor_unthrottle that should help with the disconnect in PPP
 *	bug that people have been reporting.
 *
 * (06/23/2000) gkh
 *	Cleaned up debugging statements in a quest to find UHCI timeout bug.
 *
 * (04/27/2000) Ryan VanderBijl
 * 	Fixed memory leak in visor_close
 *
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
 * 
 */

#include <linux/config.h>
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
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#include "usb-serial.h"

#include "visor.h"


/* function prototypes for a handspring visor */
static int  visor_open		(struct usb_serial_port *port, struct file *filp);
static void visor_close		(struct usb_serial_port *port, struct file *filp);
static int  visor_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static void visor_throttle	(struct usb_serial_port *port);
static void visor_unthrottle	(struct usb_serial_port *port);
static int  visor_startup	(struct usb_serial *serial);
static int  visor_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void visor_set_termios	(struct usb_serial_port *port, struct termios *old_termios);
static void visor_write_bulk_callback (struct urb *urb);

/* All of the device info needed for the Handspring Visor */
static __u16	handspring_vendor_id	= HANDSPRING_VENDOR_ID;
static __u16	handspring_product_id	= HANDSPRING_VISOR_ID;
struct usb_serial_device_type handspring_device = {
	name:			"Handspring Visor",
	idVendor:		&handspring_vendor_id,	/* the Handspring vendor ID */
	idProduct:		&handspring_product_id,	/* the Handspring Visor product id */
	needs_interrupt_in:	MUST_HAVE_NOT,		/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		2,
	num_bulk_out:		2,
	num_ports:		2,
	open:			visor_open,
	close:			visor_close,
	throttle:		visor_throttle,
	unthrottle:		visor_unthrottle,
	startup:		visor_startup,
	ioctl:			visor_ioctl,
	set_termios:		visor_set_termios,
	write:			visor_write,
	write_bulk_callback:	visor_write_bulk_callback,
};


#define NUM_URBS	24
static struct urb *write_urb_pool[NUM_URBS];


/******************************************************************************
 * Handspring Visor specific driver functions
 ******************************************************************************/
static int visor_open (struct usb_serial_port *port, struct file *filp)
{
	dbg(__FUNCTION__ " - port %d", port->number);

	if (port->active) {
		dbg (__FUNCTION__ " - device already open");
		return -EINVAL;
	}

	port->active = 1;

	/*Start reading from the device*/
	if (usb_submit_urb(port->read_urb))
		dbg(__FUNCTION__  " - usb_submit_urb(read bulk) failed");

	return (0);
}


static void visor_close (struct usb_serial_port *port, struct file * filp)
{
	struct usb_serial *serial = port->serial;
	unsigned char *transfer_buffer =  kmalloc (0x12, GFP_KERNEL);
	
	dbg(__FUNCTION__ " - port %d", port->number);
			 
	if (!transfer_buffer) {
		err(__FUNCTION__ " - kmalloc(%d) failed.", 0x12);
	} else {
		/* send a shutdown message to the device */
		usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_CLOSE_NOTIFICATION,
				0xc2, 0x0000, 0x0000, transfer_buffer, 0x12, 300);
		kfree (transfer_buffer);
	}

	/* shutdown our bulk reads and writes */
	usb_unlink_urb (port->write_urb);
	usb_unlink_urb (port->read_urb);
	port->active = 0;
}


static int visor_write (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	struct urb *urb = NULL;
	unsigned char *buffer = NULL;
	int status;
	int i;

	dbg(__FUNCTION__ " - port %d", port->number);

	if (count == 0) {
		dbg(__FUNCTION__ " - write request of 0 bytes");
		return 0;
	}

	/* try to find a free urb in our list of them */
	for (i = 0; i < NUM_URBS; ++i) {
		if (write_urb_pool[i]->status != -EINPROGRESS) {
			urb = write_urb_pool[i];
			break;
		}
	}
	if (urb == NULL) {
		dbg (__FUNCTION__ " - no free urbs");
		return 0;
	}

	count = (count > port->bulk_out_size) ? port->bulk_out_size : count;

#ifdef DEBUG
	printk (KERN_DEBUG __FILE__ ": " __FUNCTION__ " - length = %d, data = ", count);
	for (i = 0; i < count; ++i) {
		printk ("%.2x ", buf[i]);
	}
	printk ("\n");
#endif

	if (urb->transfer_buffer != NULL)
		kfree(urb->transfer_buffer);
	buffer = kmalloc (count, GFP_KERNEL);
	if (buffer == NULL) {
		err(__FUNCTION__" no more kernel memory...");
		return 0;
	}
	
	if (from_user) {
		copy_from_user(buffer, buf, count);
	}
	else {
		memcpy (buffer, buf, count);
	}  

	/* build up our urb */
	FILL_BULK_URB (urb, serial->dev, usb_sndbulkpipe(serial->dev, port->bulk_out_endpointAddress),
			buffer, count, visor_write_bulk_callback, port);
	urb->transfer_flags |= USB_QUEUE_BULK;

	/* send it down the pipe */
	status = usb_submit_urb(urb);
	if (status)
		dbg(__FUNCTION__ " - usb_submit_urb(write bulk) failed with status = %d", status);

	return (count);
} 


static void visor_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;

	if (port_paranoia_check (port, __FUNCTION__))
		return;
	
	dbg(__FUNCTION__ " - port %d", port->number);
	
	if (urb->status) {
		dbg(__FUNCTION__ " - nonzero write bulk status received: %d", urb->status);
		return;
	}

	queue_task(&port->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	
	return;
}


static void visor_throttle (struct usb_serial_port *port)
{
	dbg(__FUNCTION__ " - port %d", port->number);

	usb_unlink_urb (port->read_urb);

	return;
}


static void visor_unthrottle (struct usb_serial_port *port)
{
	dbg(__FUNCTION__ " - port %d", port->number);

	if (usb_submit_urb (port->read_urb))
		dbg(__FUNCTION__ " - usb_submit_urb(read bulk) failed");

	return;
}


static int  visor_startup (struct usb_serial *serial)
{
	int response;
	int i;
	unsigned char *transfer_buffer =  kmalloc (256, GFP_KERNEL);

	if (!transfer_buffer) {
		err(__FUNCTION__ " - kmalloc(%d) failed.", 256);
		return -ENOMEM;
	}

	dbg(__FUNCTION__);

	dbg(__FUNCTION__ " - Set config to 1");
	usb_set_configuration (serial->dev, 1);

	/* send a get connection info request */
	response = usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_GET_CONNECTION_INFORMATION,
					0xc2, 0x0000, 0x0000, transfer_buffer, 0x12, 300);
	if (response < 0) {
		err(__FUNCTION__ " - error getting connection information");
	} else {
		struct visor_connection_info *connection_info = (struct visor_connection_info *)transfer_buffer;
		char *string;
		info("%s: Number of ports: %d", serial->type->name, connection_info->num_ports);
		for (i = 0; i < connection_info->num_ports; ++i) {
			switch (connection_info->connections[i].port_function_id) {
				case VISOR_FUNCTION_GENERIC:
					string = "Generic";
					break;
				case VISOR_FUNCTION_DEBUGGER:
					string = "Debugger";
					break;
				case VISOR_FUNCTION_HOTSYNC:
					string = "HotSync";
					break;
				case VISOR_FUNCTION_CONSOLE:
					string = "Console";
					break;
				case VISOR_FUNCTION_REMOTE_FILE_SYS:
					string = "Remote File System";
					break;
				default:
					string = "unknown";
					break;	
			}
			info("%s: port %d, is for %s use and is bound to ttyUSB%d", serial->type->name, connection_info->connections[i].port, string, serial->minor + i);
		}
	}

	/* ask for the number of bytes available, but ignore the response as it is broken */
	response = usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_REQUEST_BYTES_AVAILABLE,
					0xc2, 0x0000, 0x0005, transfer_buffer, 0x02, 300);
	if (response < 0) {
		err(__FUNCTION__ " - error getting bytes available request");
	}

	kfree (transfer_buffer);

	/* continue on with initialization */
	return (0);
}


static int visor_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	dbg(__FUNCTION__ " - port %d, cmd 0x%.4x", port->number, cmd);

	return -ENOIOCTLCMD;
}


/* This function is all nice and good, but we don't change anything based on it :) */
static void visor_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	unsigned int cflag = port->tty->termios->c_cflag;

	dbg(__FUNCTION__ " - port %d", port->number);

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(port->tty->termios->c_iflag) == RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg(__FUNCTION__ " - nothing to change...");
			return;
		}
	}

	if ((!port->tty) || (!port->tty->termios)) {
		dbg(__FUNCTION__" - no tty structures");
		return;
	}

	/* get the byte size */
	switch (cflag & CSIZE) {
		case CS5:	dbg(__FUNCTION__ " - data bits = 5");   break;
		case CS6:	dbg(__FUNCTION__ " - data bits = 6");   break;
		case CS7:	dbg(__FUNCTION__ " - data bits = 7");   break;
		default:
		case CS8:	dbg(__FUNCTION__ " - data bits = 8");   break;
	}
	
	/* determine the parity */
	if (cflag & PARENB)
		if (cflag & PARODD)
			dbg(__FUNCTION__ " - parity = odd");
		else
			dbg(__FUNCTION__ " - parity = even");
	else
		dbg(__FUNCTION__ " - parity = none");

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		dbg(__FUNCTION__ " - stop bits = 2");
	else
		dbg(__FUNCTION__ " - stop bits = 1");

	
	/* figure out the flow control settings */
	if (cflag & CRTSCTS)
		dbg(__FUNCTION__ " - RTS/CTS is enabled");
	else
		dbg(__FUNCTION__ " - RTS/CTS is disabled");
	
	/* determine software flow control */
	if (I_IXOFF(port->tty))
		dbg(__FUNCTION__ " - XON/XOFF is enabled, XON = %2x, XOFF = %2x", START_CHAR(port->tty), STOP_CHAR(port->tty));
	else
		dbg(__FUNCTION__ " - XON/XOFF is disabled");

	/* get the baud rate wanted */
	dbg(__FUNCTION__ " - baud rate = %d", tty_get_baud_rate(port->tty));

	return;
}


int visor_init (void)
{
	int i;

	usb_serial_register (&handspring_device);
	
	/* create our write urb pool */ 
	for (i = 0; i < NUM_URBS; ++i) {
		struct urb  *urb = usb_alloc_urb(0);
		if (urb == NULL) {
			err("No more urbs???");
			continue;
		}
		urb->transfer_buffer = NULL;
		write_urb_pool[i] = urb;
	}
	
	return 0;
}


void visor_exit (void)
{
	int i;

	usb_serial_deregister (&handspring_device);
	
	for (i = 0; i < NUM_URBS; ++i) {
		usb_unlink_urb(write_urb_pool[i]);
		if (write_urb_pool[i]->transfer_buffer)
			kfree(write_urb_pool[i]->transfer_buffer);
		usb_free_urb (write_urb_pool[i]);
	}
}


module_init(visor_init);
module_exit(visor_exit);

