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
static void visor_throttle	(struct usb_serial_port *port);
static void visor_unthrottle	(struct usb_serial_port *port);
static int  visor_startup	(struct usb_serial *serial);

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
};


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


static void visor_throttle (struct usb_serial_port *port)
{
	dbg(__FUNCTION__ " - port %d", port->number);

	usb_unlink_urb (port->read_urb);

	return;
}


static void visor_unthrottle (struct usb_serial_port *port)
{
	dbg(__FUNCTION__ " - port %d", port->number);

	if (usb_unlink_urb (port->read_urb))
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

