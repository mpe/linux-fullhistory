/*
 * USB ConnectTech WhiteHEAT driver
 *
 *	(C) Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
 * 
 */

#include <linux/config.h>

#ifdef CONFIG_USB_SERIAL_WHITEHEAT

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

#include "whiteheat.h"		/* firmware for the ConnectTech WhiteHEAT device */


#define CONNECT_TECH_VENDOR_ID		0x0710
#define CONNECT_TECH_FAKE_WHITE_HEAT_ID	0x0001
#define CONNECT_TECH_WHITE_HEAT_ID	0x8001

/* function prototypes for the Connect Tech WhiteHEAT serial converter */
static int  whiteheat_open		(struct usb_serial_port *port, struct file *filp);
static void whiteheat_close		(struct usb_serial_port *port, struct file *filp);
static void whiteheat_set_termios	(struct usb_serial_port *port, struct termios * old);
static void whiteheat_throttle		(struct usb_serial_port *port);
static void whiteheat_unthrottle	(struct usb_serial_port *port);
static int  whiteheat_startup		(struct usb_serial *serial);

/* All of the device info needed for the Connect Tech WhiteHEAT */
static __u16	connecttech_vendor_id			= CONNECT_TECH_VENDOR_ID;
static __u16	connecttech_whiteheat_fake_product_id	= CONNECT_TECH_FAKE_WHITE_HEAT_ID;
static __u16	connecttech_whiteheat_product_id	= CONNECT_TECH_WHITE_HEAT_ID;
struct usb_serial_device_type whiteheat_fake_device = {
	name:			"Connect Tech - WhiteHEAT - (prerenumeration)",
	idVendor:		&connecttech_vendor_id,			/* the Connect Tech vendor id */
	idProduct:		&connecttech_whiteheat_fake_product_id,	/* the White Heat initial product id */
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		whiteheat_startup	
};
struct usb_serial_device_type whiteheat_device = {
	name:			"Connect Tech - WhiteHEAT",
	idVendor:		&connecttech_vendor_id,			/* the Connect Tech vendor id */
	idProduct:		&connecttech_whiteheat_product_id,	/* the White Heat real product id */
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		4,
	open:			whiteheat_open,
	close:			whiteheat_close,
	throttle:		whiteheat_throttle,
	unthrottle:		whiteheat_unthrottle,
	set_termios:		whiteheat_set_termios,
};


/*****************************************************************************
 * Connect Tech's White Heat specific driver functions
 *****************************************************************************/
static int whiteheat_open (struct usb_serial_port *port, struct file *filp)
{
	dbg("whiteheat_open port %d", port->number);

	if (port->active) {
		dbg ("device already open");
		return -EINVAL;
	}
	port->active = 1;
 
	/*Start reading from the device*/
	if (usb_submit_urb(port->read_urb))
		dbg("usb_submit_urb(read bulk) failed");

	/* Need to do device specific setup here (control lines, baud rate, etc.) */
	/* FIXME!!! */

	return (0);
}


static void whiteheat_close(struct usb_serial_port *port, struct file * filp)
{
	dbg("whiteheat_close port %d", port->number);
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (port->write_urb);
	usb_unlink_urb (port->read_urb);
	port->active = 0;
}


static void whiteheat_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	unsigned int cflag = port->tty->termios->c_cflag;

	dbg("whiteheat_set_termios port %d", port->number);

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(port->tty->termios->c_iflag) == RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg("nothing to change...");
			return;
		}
	}

	/* do the parsing of the cflag to see what to set the line to */
	/* FIXME!! */

	return;
}

static void whiteheat_throttle (struct usb_serial_port *port)
{
	dbg("whiteheat_throttle port %d", port->number);

	/* Change the control signals */
	/* FIXME!!! */

	return;
}


static void whiteheat_unthrottle (struct usb_serial_port *port)
{
	dbg("whiteheat_unthrottle port %d", port->number);

	/* Change the control signals */
	/* FIXME!!! */

	return;
}


/* steps to download the firmware to the WhiteHEAT device:
 - hold the reset (by writing to the reset bit of the CPUCS register)
 - download the VEND_AX.HEX file to the chip using VENDOR_REQUEST-ANCHOR_LOAD
 - release the reset (by writing to the CPUCS register)
 - download the WH.HEX file for all addresses greater than 0x1b3f using
   VENDOR_REQUEST-ANCHOR_EXTERNAL_RAM_LOAD
 - hold the reset
 - download the WH.HEX file for all addresses less than 0x1b40 using
   VENDOR_REQUEST_ANCHOR_LOAD
 - release the reset
 - device renumerated itself and comes up as new device id with all
   firmware download completed.
*/
static int  whiteheat_startup (struct usb_serial *serial)
{
	int response;
	const struct whiteheat_hex_record *record;
	
	dbg("whiteheat_startup");
	
	response = ezusb_set_reset (serial, 1);

	record = &whiteheat_loader[0];
	while (record->address != 0xffff) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("ezusb_writememory failed for loader (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = ezusb_set_reset (serial, 0);

	record = &whiteheat_firmware[0];
	while (record->address < 0x1b40) {
		++record;
	}
	while (record->address != 0xffff) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("ezusb_writememory failed for first firmware step (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}
	
	response = ezusb_set_reset (serial, 1);

	record = &whiteheat_firmware[0];
	while (record->address < 0x1b40) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("ezusb_writememory failed for second firmware step (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = ezusb_set_reset (serial, 0);

	/* we want this device to fail to have a driver assigned to it. */
	return (1);
}

#endif	/* CONFIG_USB_SERIAL_WHITEHEAT */


