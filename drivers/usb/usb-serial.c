/*
 * USB Serial Converter driver
 *
 *	(C) Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * This driver was originally based on the ACM driver by Armin Fuerst (which was 
 * based on a driver by Brad Keryan)
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 * (02/21/2000) gkh
 *	Made it so that any serial devices only have to specify which functions
 *	they want to overload from the generic function calls (great, 
 *	inheritance in C, in a driver, just what I wanted...)
 *	Added support for set_termios and ioctl function calls. No drivers take
 *	advantage of this yet.
 *	Removed the #ifdef MODULE, now there is no module specific code.
 *	Cleaned up a few comments in usb-serial.h that were wrong (thanks again
 *	to Miles Lott).
 *	Small fix to get_free_serial.
 *
 * (02/14/2000) gkh
 *	Removed the Belkin and Peracom functionality from the driver due to
 *	the lack of support from the vendor, and me not wanting people to 
 *	accidenatly buy the device, expecting it to work with Linux.
 *	Added read_bulk_callback and write_bulk_callback to the type structure
 *	for the needs of the FTDI and WhiteHEAT driver.
 *	Changed all reverences to FTDI to FTDI_SIO at the request of Bill
 *	Ryder.
 *	Changed the output urb size back to the max endpoint size to make
 *	the ftdi_sio driver have it easier, and due to the fact that it didn't
 *	really increase the speed any.
 *
 * (02/11/2000) gkh
 *	Added VISOR_FUNCTION_CONSOLE to the visor startup function. This was a
 *	patch from Miles Lott (milos@insync.net).
 *	Fixed bug with not restoring the minor range that a device grabs, if
 *	the startup function fails (thanks Miles for finding this).
 *
 * (02/05/2000) gkh
 *	Added initial framework for the Keyspan PDA serial converter so that
 *	Brian Warner has a place to put his code.
 *	Made the ezusb specific functions generic enough that different
 *	devices can use them (whiteheat and keyspan_pda both need them).
 *	Split out a whole bunch of structure and other stuff to a seperate
 *	usb-serial.h file.
 *	Made the Visor connection messages a little more understandable, now
 *	that Miles Lott (milos@insync.net) has gotten the Generic channel to
 *	work. Also made them always show up in the log file.
 * 
 * (01/25/2000) gkh
 *	Added initial framework for FTDI serial converter so that Bill Ryder
 *	has a place to put his code.
 *	Added the vendor specific info from Handspring. Now we can print out
 *	informational debug messages as well as understand what is happening.
 *
 * (01/23/2000) gkh
 *	Fixed problem of crash when trying to open a port that didn't have a
 *	device assigned to it. Made the minor node finding a little smarter,
 *	now it looks to find a continous space for the new device.
 *
 * (01/21/2000) gkh
 *	Fixed bug in visor_startup with patch from Miles Lott (milos@insync.net)
 *	Fixed get_serial_by_minor which was all messed up for multi port 
 *	devices. Fixed multi port problem for generic devices. Now the number
 *	of ports is determined by the number of bulk out endpoints for the
 *	generic device.
 *
 * (01/19/2000) gkh
 *	Removed lots of cruft that was around from the old (pre urb) driver 
 *	interface.
 *	Made the serial_table dynamic. This should save lots of memory when
 *	the number of minor nodes goes up to 256.
 *	Added initial support for devices that have more than one port. 
 *	Added more debugging comments for the Visor, and added a needed 
 *	set_configuration call.
 *
 * (01/17/2000) gkh
 *	Fixed the WhiteHEAT firmware (my processing tool had a bug)
 *	and added new debug loader firmware for it.
 *	Removed the put_char function as it isn't really needed.
 *	Added visor startup commands as found by the Win98 dump.
 * 
 * (01/13/2000) gkh
 *	Fixed the vendor id for the generic driver to the one I meant it to be.
 *
 * (01/12/2000) gkh
 *	Forget the version numbering...that's pretty useless...
 *	Made the driver able to be compiled so that the user can select which
 *	converter they want to use. This allows people who only want the Visor
 *	support to not pay the memory size price of the WhiteHEAT.
 *	Fixed bug where the generic driver (idVendor=0000 and idProduct=0000)
 *	grabbed the root hub. Not good.
 * 
 * version 0.4.0 (01/10/2000) gkh
 *	Added whiteheat.h containing the firmware for the ConnectTech WhiteHEAT
 *	device. Added startup function to allow firmware to be downloaded to
 *	a device if it needs to be.
 *	Added firmware download logic to the WhiteHEAT device.
 *	Started to add #defines to split up the different drivers for potential
 *	configuration option.
 *	
 * version 0.3.1 (12/30/99) gkh
 *      Fixed problems with urb for bulk out.
 *      Added initial support for multiple sets of endpoints. This enables
 *      the Handspring Visor to be attached successfully. Only the first
 *      bulk in / bulk out endpoint pair is being used right now.
 *
 * version 0.3.0 (12/27/99) gkh
 *	Added initial support for the Handspring Visor based on a patch from
 *	Miles Lott (milos@sneety.insync.net)
 *	Cleaned up the code a bunch and converted over to using urbs only.
 *
 * version 0.2.3 (12/21/99) gkh
 *	Added initial support for the Connect Tech WhiteHEAT converter.
 *	Incremented the number of ports in expectation of getting the
 *	WhiteHEAT to work properly (4 ports per connection).
 *	Added notification on insertion and removal of what port the
 *	device is/was connected to (and what kind of device it was).
 *
 * version 0.2.2 (12/16/99) gkh
 *	Changed major number to the new allocated number. We're legal now!
 *
 * version 0.2.1 (12/14/99) gkh
 *	Fixed bug that happens when device node is opened when there isn't a
 *	device attached to it. Thanks to marek@webdesign.no for noticing this.
 *
 * version 0.2.0 (11/10/99) gkh
 *	Split up internals to make it easier to add different types of serial 
 *	converters to the code.
 *	Added a "generic" driver that gets it's vendor and product id
 *	from when the module is loaded. Thanks to David E. Nelson (dnelson@jump.net)
 *	for the idea and sample code (from the usb scanner driver.)
 *	Cleared up any licensing questions by releasing it under the GNU GPL.
 *
 * version 0.1.2 (10/25/99) gkh
 * 	Fixed bug in detecting device.
 *
 * version 0.1.1 (10/05/99) gkh
 * 	Changed the major number to not conflict with anything else.
 *
 * version 0.1 (09/28/99) gkh
 * 	Can recognize the two different devices and start up a read from
 *	device when asked to. Writes also work. No control signals yet, this
 *	all is vendor specific data (i.e. no spec), also no control for
 *	different baud rates or other bit settings.
 *	Currently we are using the same devid as the acm driver. This needs
 *	to change.
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

#define DEBUG

#include "usb.h"

#ifdef CONFIG_USB_SERIAL_WHITEHEAT
#include "whiteheat.h"		/* firmware for the ConnectTech WhiteHEAT device */
#endif

#include "usb-serial.h"

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

/* local function prototypes */
static int  serial_open (struct tty_struct *tty, struct file * filp);
static void serial_close (struct tty_struct *tty, struct file * filp);
static int  serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count);
static int  serial_write_room (struct tty_struct *tty);
static int  serial_chars_in_buffer (struct tty_struct *tty);
static void serial_throttle (struct tty_struct * tty);
static void serial_unthrottle (struct tty_struct * tty);
static int  serial_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg);
static void serial_set_termios (struct tty_struct *tty, struct termios * old);

static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_serial_disconnect(struct usb_device *dev, void *ptr);

static struct usb_driver usb_serial_driver = {
	"serial",
	usb_serial_probe,
	usb_serial_disconnect,
	{ NULL, NULL }
};

static int			serial_refcount;
static struct tty_struct *	serial_tty[SERIAL_TTY_MINORS];
static struct termios *		serial_termios[SERIAL_TTY_MINORS];
static struct termios *		serial_termios_locked[SERIAL_TTY_MINORS];
static struct usb_serial	*serial_table[SERIAL_TTY_MINORS] = {NULL, };



static struct usb_serial *get_serial_by_minor (int minor)
{
	int i;

	dbg("get_serial_by_minor %d", minor);

	if (serial_table[minor] == NULL)
		return (NULL);

	if (serial_table[minor] != SERIAL_PTR_EMPTY)
		return (serial_table[minor]);

	i = minor;
	while (serial_table[i] == SERIAL_PTR_EMPTY) {
		if (i == 0)
			return (NULL);
		--i;
		}
	return (serial_table[i]);
}


static struct usb_serial *get_free_serial (int num_ports, int *minor)
{
	struct usb_serial *serial = NULL;
	int i, j;
	int good_spot;

	dbg("get_free_serial %d", num_ports);

	*minor = 0;
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		if (serial_table[i])
			continue;

		good_spot = 1;
		for (j = 1; j <= num_ports-1; ++j)
			if (serial_table[i+j])
				good_spot = 0;
		if (good_spot == 0)
			continue;
			
		if (!(serial = kmalloc(sizeof(struct usb_serial), GFP_KERNEL))) {
			err("Out of memory");
			return NULL;
		}
		memset(serial, 0, sizeof(struct usb_serial));
		serial_table[i] = serial;
		*minor = i;
		dbg("minor base = %d", *minor);
		for (i = *minor+1; (i < (*minor + num_ports)) && (i < SERIAL_TTY_MINORS); ++i)
			serial_table[i] = SERIAL_PTR_EMPTY;
		return (serial);
		}
	return (NULL);
}


static void return_serial (struct usb_serial *serial)
{
	int i;

	dbg("return_serial");

	if (serial == NULL)
		return;

	for (i = 0; i < serial->num_ports; ++i) {
		serial_table[serial->minor + i] = NULL;
	}

	return;
}


#ifdef USES_EZUSB_FUNCTIONS
/* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */
#define CPUCS_REG    0x7F92

static int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest)
{
	int result;
	unsigned char *transfer_buffer =  kmalloc (length, GFP_KERNEL);

//	dbg("ezusb_writememory %x, %d", address, length);

	if (!transfer_buffer) {
		err("ezusb_writememory: kmalloc(%d) failed.", length);
		return -ENOMEM;
	}
	memcpy (transfer_buffer, data, length);
	result = usb_control_msg (serial->dev, usb_sndctrlpipe(serial->dev, 0), bRequest, 0x40, address, 0, transfer_buffer, length, 300);
	kfree (transfer_buffer);
	return result;
}


static int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit)
{
	int	response;
	dbg("ezusb_set_reset: %d", reset_bit);
	response = ezusb_writememory (serial, CPUCS_REG, &reset_bit, 1, 0xa0);
	if (response < 0) {
		err("ezusb_set_reset %d failed", reset_bit);
	}
	return (response);
}

#endif	/* USES_EZUSB_FUNCTIONS */


/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int serial_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial;
	
	dbg("serial_open");

	/* initialize the pointer incase something fails */
	tty->driver_data = NULL;

	/* get the serial object associated with this tty pointer */
	serial = get_serial_by_minor (MINOR(tty->device));

	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return (-ENODEV);
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return (-ENODEV);
	}

	/* make the tty driver remember our serial object, and us it */
	tty->driver_data = serial;
	serial->tty = tty;
	 
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->open) {
		return (serial->type->open(tty, filp));
	} else {
		return (generic_serial_open(tty, filp));
	}
}


static void serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port;	

	dbg("serial_close");

	if (!serial) {
		dbg("serial == NULL!");
		return;
	}

	port = MINOR(tty->device) - serial->minor;

	dbg("serial_close port %d", port);
	
	/* do some sanity checking that we really have a device present */
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->active[port]) {
		dbg ("device already open");
		return;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->close) {
		serial->type->close(tty, filp);
	} else {
		generic_serial_close(tty, filp);
	}
}	


static int serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;
	
	dbg("serial_write port %d, %d byte(s)", port, count);

	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return (-ENODEV);
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return (-ENODEV);
	}
	if (!serial->active[port]) {
		dbg ("device not opened");
		return (-EINVAL);
	}
	
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->write) {
		return (serial->type->write(tty, from_user, buf, count));
	} else {
		return (generic_serial_write(tty, from_user, buf, count));
	}
}


static int serial_write_room (struct tty_struct *tty) 
{
	struct usb_serial *serial = (struct usb_serial *)tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("serial_write_room port %d", port);
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return (-ENODEV);
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return (-ENODEV);
	}
	if (!serial->active[port]) {
		dbg ("device not open");
		return (-EINVAL);
	}
	
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->write_room) {
		return (serial->type->write_room(tty));
	} else {
		return (generic_write_room(tty));
	}
}


static int serial_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial *serial = (struct usb_serial *)tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("serial_chars_in_buffer port %d", port);
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return (-ENODEV);
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return (-ENODEV);
	}
	if (!serial->active[port]) {
		dbg ("device not open");
		return (-EINVAL);
	}
	
	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->chars_in_buffer) {
		return (serial->type->chars_in_buffer(tty));
	} else {
		return (generic_chars_in_buffer(tty));
	}
}


static void serial_throttle (struct tty_struct * tty)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("serial_throttle port %d", port);
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return;
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->active[port]) {
		dbg ("device not open");
		return;
	}

	/* pass on to the driver specific version of this function */
	if (serial->type->throttle) {
		serial->type->throttle(tty);
	} else {
		generic_throttle(tty);
	}

	return;
}


static void serial_unthrottle (struct tty_struct * tty)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("serial_unthrottle port %d", port);
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return;
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->active[port]) {
		dbg ("device not open");
		return;
	}

	/* pass on to the driver specific version of this function */
	if (serial->type->unthrottle) {
		serial->type->unthrottle(tty);
	} else {
		generic_unthrottle(tty);
	}

	return;
}


static int serial_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port;	

	dbg("serial_ioctl");

	if (!serial) {
		dbg("serial == NULL!");
		return -ENODEV;
	}

	port = MINOR(tty->device) - serial->minor;

	dbg("serial_ioctl port %d", port);
	
	/* do some sanity checking that we really have a device present */
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return -ENODEV;
	}
	if (!serial->active[port]) {
		dbg ("device not open");
		return -ENODEV;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->ioctl) {
		return (serial->type->ioctl(tty, file, cmd, arg));
	} else {
		return (generic_ioctl (tty, file, cmd, arg));
	}
}


static void serial_set_termios (struct tty_struct *tty, struct termios * old)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port;	

	dbg("serial_set_termios");

	if (!serial) {
		dbg("serial == NULL!");
		return;
	}

	port = MINOR(tty->device) - serial->minor;

	dbg("serial_set_termios port %d", port);
	
	/* do some sanity checking that we really have a device present */
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->active[port]) {
		dbg ("device not open");
		return;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (serial->type->set_termios) {
		serial->type->set_termios(tty, old);
	} else {
		generic_set_termios (tty, old);
	}
	
	return;
}


#ifdef CONFIG_USB_SERIAL_WHITEHEAT
/*****************************************************************************
 * Connect Tech's White Heat specific driver functions
 *****************************************************************************/
static int whiteheat_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("whiteheat_serial_open port %d", port);

	if (serial->active[port]) {
		dbg ("device already open");
		return -EINVAL;
	}
	serial->active[port] = 1;
 
	/*Start reading from the device*/
	if (usb_submit_urb(&serial->read_urb[port]))
		dbg("usb_submit_urb(read bulk) failed");

	/* Need to do device specific setup here (control lines, baud rate, etc.) */
	/* FIXME!!! */

	return (0);
}


static void whiteheat_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("whiteheat_serial_close port %d", port);
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb[port]);
	usb_unlink_urb (&serial->read_urb[port]);
	serial->active[port] = 0;
}


static void whiteheat_set_termios (struct tty_struct *tty, struct termios *old_termios)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;
	unsigned int cflag = tty->termios->c_cflag;

	dbg("whiteheat_set_termios port %d", port);

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(tty->termios->c_iflag) == RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg("nothing to change...");
			return;
		}
	}

	/* do the parsing of the cflag to see what to set the line to */
	/* FIXME!! */

	return;
}

static void whiteheat_throttle (struct tty_struct * tty)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("whiteheat_throttle port %d", port);

	/* Change the control signals */
	/* FIXME!!! */

	return;
}


static void whiteheat_unthrottle (struct tty_struct * tty)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("whiteheat_unthrottle port %d", port);

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


#ifdef CONFIG_USB_SERIAL_VISOR
/******************************************************************************
 * Handspring Visor specific driver functions
 ******************************************************************************/
static int visor_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data;
	int port = MINOR(tty->device) - serial->minor;

	dbg("visor_serial_open port %d", port);

	if (serial->active[port]) {
		dbg ("device already open");
		return -EINVAL;
	}

	serial->active[port] = 1;

	/*Start reading from the device*/
	if (usb_submit_urb(&serial->read_urb[port]))
		dbg("usb_submit_urb(read bulk) failed");

	return (0);
}

static void visor_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data;
	int port = MINOR(tty->device) - serial->minor;
	unsigned char *transfer_buffer =  kmalloc (0x12, GFP_KERNEL);
	
	dbg("visor_serial_close port %d", port);
			 
	if (!transfer_buffer) {
		err("visor_serial_close: kmalloc(%d) failed.", 0x12);
	} else {
		/* send a shutdown message to the device */
		usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_CLOSE_NOTIFICATION,
				0xc2, 0x0000, 0x0000, transfer_buffer, 0x12, 300);
	}

	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb[port]);
	usb_unlink_urb (&serial->read_urb[port]);
	serial->active[port] = 0;
}


static void visor_throttle (struct tty_struct * tty)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data;
	int port = MINOR(tty->device) - serial->minor;

	dbg("visor_throttle port %d", port);

	usb_unlink_urb (&serial->read_urb[port]);

	return;
}


static void visor_unthrottle (struct tty_struct * tty)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data;
	int port = MINOR(tty->device) - serial->minor;

	dbg("visor_unthrottle port %d", port);

	if (usb_unlink_urb (&serial->read_urb[port]))
		dbg("usb_submit_urb(read bulk) failed");

	return;
}


static int  visor_startup (struct usb_serial *serial)
{
	int response;
	int i;
	unsigned char *transfer_buffer =  kmalloc (256, GFP_KERNEL);

	if (!transfer_buffer) {
		err("visor_startup: kmalloc(%d) failed.", 256);
		return -ENOMEM;
	}

	dbg("visor_startup");

	dbg("visor_setup: Set config to 1");
	usb_set_configuration (serial->dev, 1);

	/* send a get connection info request */
	response = usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_GET_CONNECTION_INFORMATION,
					0xc2, 0x0000, 0x0000, transfer_buffer, 0x12, 300);
	if (response < 0) {
		err("visor_startup: error getting connection information");
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
		err("visor_startup: error getting bytes available request");
	}

	kfree (transfer_buffer);

	/* continue on with initialization */
	return (0);
}


#endif	/* CONFIG_USB_SERIAL_VISOR*/


#ifdef CONFIG_USB_SERIAL_FTDI_SIO
/******************************************************************************
 * FTDI SIO Serial Converter specific driver functions
 ******************************************************************************/

/* Bill Ryder - bryder@sgi.com - wrote the FTDI_SIO implementation */
/* Thanx to FTDI for so kindly providing details of the protocol required */
/*   to talk to the device */

#include "ftdi_sio.h"

static int  ftdi_sio_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	char buf[1]; /* Needed for the usb_control_msg I think */
	int port = MINOR(tty->device) - serial->minor;

	dbg("ftdi_sio_serial_open port %d", port);

	if (serial->active[port]) {
		dbg ("device already open");
		return -EINVAL;
	}
	serial->active[port] = 1;

	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			FTDI_SIO_RESET_REQUEST, FTDI_SIO_RESET_REQUEST_TYPE, 
			FTDI_SIO_RESET_SIO, 
			0, buf, 0, HZ * 5);

	/* FIXME - Should I really purge the buffers? */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			FTDI_SIO_RESET_REQUEST, FTDI_SIO_RESET_REQUEST_TYPE, 
			FTDI_SIO_RESET_PURGE_RX, 
			0, buf, 0, HZ * 5);

	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			FTDI_SIO_RESET_REQUEST, FTDI_SIO_RESET_REQUEST_TYPE, 
			FTDI_SIO_RESET_PURGE_TX, 
			0, buf, 0, HZ * 5);	


	/* As per usb_serial_init s/be CS8, B9600, 1 STOP BIT */
	if ( usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     FTDI_SIO_SET_BAUDRATE_REQUEST,
			     FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE,
			     ftdi_sio_b9600, 0, 
			     buf, 0, HZ * 5) < 0){
		dbg("Error from baudrate urb");
		return(-EINVAL);
	}

	if ( usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     FTDI_SIO_SET_DATA_REQUEST, 
			     FTDI_SIO_SET_DATA_REQUEST_TYPE,
			     8 | FTDI_SIO_SET_DATA_PARITY_NONE | 
			     FTDI_SIO_SET_DATA_STOP_BITS_1, 0,
			     buf, 0, HZ * 5) < 0){
		dbg("Error from cs8/noparity/1 stopbit urb");
		return(-EINVAL);
	}

	/* Disable flow control */
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
			    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
			    0, 0, 
			    buf, 0, HZ * 5) < 0) {
		dbg("error from flowcontrol urb");
		return(-EINVAL);
	}	    

	/* Turn on RTS and DTR since we are not flow controlling*/
	/* FIXME - check for correct behaviour clocal vs non clocal */
	/* FIXME - might be able to do both simultaneously */
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			    (unsigned)FTDI_SIO_SET_DTR_HIGH, 0, 
			    buf, 0, HZ * 5) < 0) {
		dbg("Error from DTR HIGH urb");
	}
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			    (unsigned)FTDI_SIO_SET_RTS_HIGH, 0, 
			    buf, 0, HZ * 5) < 0) {
		dbg("Error from RTS HIGH urb");
	}
	
	/*Start reading from the device*/
	if (usb_submit_urb(&serial->read_urb[port]))
		dbg("usb_submit_urb(read bulk) failed");


	return (0);
}


static void ftdi_sio_serial_close (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	char buf[1];
	int port = MINOR(tty->device) - serial->minor;

	dbg("ftdi_sio_serial_close port %d", port);
	
	/* FIXME - might be able to do both simultaneously */
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			    (unsigned)FTDI_SIO_SET_DTR_LOW, 0, 
			    buf, 0, HZ * 5) < 0) {
		dbg("Error from DTR LOW urb");
	}
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			    (unsigned)FTDI_SIO_SET_RTS_LOW, 0, 
			    buf, 0, HZ * 5) < 0) {
		dbg("Error from RTS LOW urb");
	}	

	/* FIXME Should I flush the device here? - not doing it for now */

	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb[port]);
	usb_unlink_urb (&serial->read_urb[port]);
	serial->active[port] = 0;
}


  
/* The ftdi_sio requires the first byte to have:
   B0 1
   B1 0
   B2..7 length of message excluding byte 0
*/
static int ftdi_sio_serial_write (struct tty_struct * tty, int from_user, 
				  const unsigned char *buf, int count)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;
	const int data_offset = 1;

	dbg("ftdi_sio_serial_write port %d, %d bytes", port, count);

	if (count == 0) {
		dbg("write request of 0 bytes");
		return (0);
	}

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		unsigned char *first_byte = serial->write_urb[port].transfer_buffer;

		if (serial->write_urb[port].status == -EINPROGRESS) {
			dbg ("already writing");
			return (0);
		}

		count += data_offset;
		count = (count > serial->bulk_out_size[port]) ? 
			serial->bulk_out_size[port] : count;


		/* Copy in the data to send */
		if (from_user) {
			copy_from_user(serial->write_urb[port].transfer_buffer + data_offset , 
				       buf, count - data_offset );
		}
		else {
			memcpy(serial->write_urb[port].transfer_buffer + data_offset,
			       buf, count - data_offset );
		}  

		/* Write the control byte at the front of the packet*/
		first_byte = serial->write_urb[port].transfer_buffer;
		*first_byte = 1 | ((count-data_offset) << 2) ; 

#ifdef DEBUG
		dbg("Bytes: %d, Control Byte: 0o%03o",count, first_byte[0]);

		if (count) {
			int i;
			printk (KERN_DEBUG __FILE__ ": data written - length = %d, data = ", count);
			for (i = 0; i < count; ++i) {
				printk ("0x%02x ", first_byte[i]);
				if (first_byte[i] > ' ' && first_byte[i] < '~') {
					printk("%c ", first_byte[i]);
				} else {
					printk("  ");
				}
			}

		     
			printk ("\n");
		}

#endif


		/* send the data out the bulk port */
		serial->write_urb[port].transfer_buffer_length = count;

		if (usb_submit_urb(&serial->write_urb[port]))
			dbg("usb_submit_urb(write bulk) failed");

		dbg("write returning: %d",count - data_offset);
		return (count - data_offset);
	}
	
	/* no bulk out, so return 0 bytes written */
	return (0);
} 


static void ftdi_sio_read_bulk_callback (struct urb *urb)
{ /* ftdi_sio_serial_buld_callback */
	struct usb_serial *serial = (struct usb_serial *)urb->context;
       	struct tty_struct *tty = serial->tty; 
       	unsigned char *data = urb->transfer_buffer;
	const int data_offset = 2;
	int i;

	dbg("ftdi_sio_read_bulk_callback");

	if (urb->status) {
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

#ifdef DEBUG
	if (urb->actual_length > 2) {
		printk (KERN_DEBUG __FILE__ ": data read - length = %d, data = ", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i) {
			printk ("0x%.2x ", data[i]);
			if (data[i] > ' ' && data[i] < '~') {
				printk("%c ", data[i]);
			} else {
				printk("  ");
			}
		}
		printk ("\n");
	}
#endif
	

	if (urb->actual_length > data_offset) {
		for (i = data_offset ; i < urb->actual_length ; ++i) {
			tty_insert_flip_char(tty, data[i], 0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	if (usb_submit_urb(urb))
		dbg("failed resubmitting read urb");

	return;
} /* ftdi_sio_serial_read_bulk_callback */

static void ftdi_sio_set_termios (struct tty_struct *tty, struct termios *old_termios)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;
	unsigned int cflag = tty->termios->c_cflag;
	__u16 urb_value; /* Will hold the new flags */
	char buf[1]; /* Perhaps I should dynamically alloc this? */
	dbg("ftdi_sio_set_termios port %d", port);

	/* FIXME - we should keep the old termios really */
	/* FIXME -For this cut I don't care if the line is really changing or 
	   not  - so just do the change regardless */
	
	/* Set number of data bits, parity, stop bits */
	
	urb_value = 0;
	urb_value |= (cflag & CSTOPB ? FTDI_SIO_SET_DATA_STOP_BITS_2 :
		      FTDI_SIO_SET_DATA_STOP_BITS_1);
	urb_value |= (cflag & PARENB ? 
		      (cflag & PARODD ? FTDI_SIO_SET_DATA_PARITY_ODD : 
		       FTDI_SIO_SET_DATA_PARITY_EVEN) :
		      FTDI_SIO_SET_DATA_PARITY_NONE);
	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
		case CS5: urb_value |= 5; dbg("Setting CS5"); break;
		case CS6: urb_value |= 6; dbg("Setting CS6"); break;
		case CS7: urb_value |= 7; dbg("Setting CS7"); break;
		case CS8: urb_value |= 8; dbg("Setting CS8"); break;
		default:
			dbg("CSIZE was set but not CS5-CS8");
		}
	}
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_DATA_REQUEST, 
			    FTDI_SIO_SET_DATA_REQUEST_TYPE,
			    urb_value , 0,
			    buf, 0, 100) < 0) {
		dbg("FAILED to set databits/stopbits/parity");
	}	   

	/* Now do the baudrate */
	/* FIXME - should drop lines on B0 */
	/* FIXME Should also handle CLOCAL here  */
     
	switch(cflag & CBAUD){
	case B300: urb_value = ftdi_sio_b300; dbg("Set to 300"); break;
	case B600: urb_value = ftdi_sio_b600; dbg("Set to 600") ; break;
	case B1200: urb_value = ftdi_sio_b1200; dbg("Set to 1200") ; break;
	case B2400: urb_value = ftdi_sio_b2400; dbg("Set to 2400") ; break;
	case B4800: urb_value = ftdi_sio_b4800; dbg("Set to 4800") ; break;
	case B9600: urb_value = ftdi_sio_b9600; dbg("Set to 9600") ; break;
	case B19200: urb_value = ftdi_sio_b19200; dbg("Set to 19200") ; break;
	case B38400: urb_value = ftdi_sio_b38400; dbg("Set to 38400") ; break;
	case B57600: urb_value = ftdi_sio_b57600; dbg("Set to 57600") ; break;
	case B115200: urb_value = ftdi_sio_b115200; dbg("Set to 115200") ; break;
	default: dbg("FTDI_SIO does not support the baudrate requested"); 
		/* FIXME - how to return an error for this? */ break;
	}
	/* Send the URB */
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_BAUDRATE_REQUEST, 
			    FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE,
			    urb_value, 0, 
			    buf, 0, 100) < 0) {
		dbg("urb failed to set baurdrate");
	}
	return;
}

/*FIXME - the beginnings of this implementation - not even hooked into the driver yet */
static int ftdi_sio_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;
	__u16 urb_value=0; /* Will hold the new flags */
	char buf[1];
	int  ret, mask;
	dbg("ftdi_sio_ioctl port %d", port);

	/* Based on code from acm.c */
	switch (cmd) {

		case TIOCMGET:
			/* Request the status from the device */
			if ((ret = usb_control_msg(serial->dev, 
					    usb_sndctrlpipe(serial->dev, 0),
					    FTDI_SIO_GET_MODEM_STATUS_REQUEST, 
					    FTDI_SIO_GET_MODEM_STATUS_REQUEST_TYPE,
					    0, 0, 
					    buf, 1, HZ * 5)) < 0 ) {
				dbg("Get not get modem status of device");
				return(ret);
			}

			return put_user((buf[0] & FTDI_SIO_DSR_MASK ? TIOCM_DSR : 0) |
					(buf[0] & FTDI_SIO_CTS_MASK ? TIOCM_CTS : 0) |
					(buf[0]  & FTDI_SIO_RI_MASK  ? TIOCM_RI  : 0) |
					(buf[0]  & FTDI_SIO_RLSD_MASK ? TIOCM_CD  : 0),
					(unsigned long *) arg);
			break;

		case TIOCMSET:
		case TIOCMBIS:
		case TIOCMBIC:
			if ((ret = get_user(mask, (unsigned long *) arg))) return ret;

			/* FIXME Need to remember if we have set DTR or RTS since we
			   can't ask the device  */
			/* FIXME - also need to find the meaning of TIOCMBIS/BIC/SET */
			if (mask & TIOCM_DTR) {
				switch(cmd) {
				case TIOCMSET:
					urb_value = FTDI_SIO_SET_DTR_HIGH | FTDI_SIO_SET_RTS_LOW;
					break;

				case TIOCMBIS:
					/* Will leave RTS alone and set DTR */
					urb_value =  FTDI_SIO_SET_DTR_HIGH;
					break;
					
				case TIOCMBIC:
					urb_value = FTDI_SIO_SET_DTR_LOW;
					break;
				}
			}

			if (mask & TIOCM_RTS) {
				switch(cmd) {
				case TIOCMSET:
					urb_value = FTDI_SIO_SET_DTR_LOW | FTDI_SIO_SET_RTS_HIGH;
					break;

				case TIOCMBIS:
					/* Will leave DTR and set RTS */
					urb_value = FTDI_SIO_SET_RTS_HIGH;
					break;

				case TIOCMBIC:
					/* Will unset RTS */
					urb_value = FTDI_SIO_SET_RTS_LOW;
					break;
				}
			}	

			
			return(usb_control_msg(serial->dev, 
					       usb_sndctrlpipe(serial->dev, 0),
					       FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
					       FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
					       urb_value , 0,
					       buf, 0, HZ * 5));
	}

	return -ENOIOCTLCMD;
}

#endif	/* CONFIG_USB_SERIAL_FTDI_SIO */


#ifdef CONFIG_USB_SERIAL_KEYSPAN_PDA
/*****************************************************************************
 * Keyspan PDA specific driver functions
 *****************************************************************************/
static int keyspan_pda_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("keyspan_pda_serial_open port %d", port);

	if (serial->active[port]) {
		dbg ("device already open");
		return -EINVAL;
	}
	serial->active[port] = 1;
 
	/*Start reading from the device*/
	if (usb_submit_urb(&serial->read_urb[port]))
		dbg("usb_submit_urb(read bulk) failed");

	/* Need to do device specific setup here (control lines, baud rate, etc.) */
	/* FIXME */

	return (0);
}


static void keyspan_pda_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("keyspan_pda_serial_close port %d", port);
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb[port]);
	usb_unlink_urb (&serial->read_urb[port]);
	serial->active[port] = 0;
}


static int  keyspan_pda_startup (struct usb_serial *serial)
{
	dbg("keyspan_pda_startup");

	/* download the firmware here ... */
	/* FIXME */

	/* we want this device to fail to have a driver assigned to it. */
	return (1);
}
#endif	/* CONFIG_USB_SERIAL_KEYSPAN_PDA */


/*****************************************************************************
 * generic devices specific driver functions
 *****************************************************************************/
static int generic_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("generic_serial_open port %d", port);

	if (serial->active[port]) {
		dbg ("device already open");
		return -EINVAL;
	}
	serial->active[port] = 1;
 
	/* if we have a bulk interrupt, start reading from it */
	if (serial->num_bulk_in) {
		/*Start reading from the device*/
		if (usb_submit_urb(&serial->read_urb[port]))
			dbg("usb_submit_urb(read bulk) failed");
	}

	return (0);
}


static void generic_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("generic_serial_close port %d", port);
	
	/* shutdown any bulk reads that might be going on */
	if (serial->num_bulk_out) {
		usb_unlink_urb (&serial->write_urb[port]);
	}
	if (serial->num_bulk_in) {
		usb_unlink_urb (&serial->read_urb[port]);
	}

	serial->active[port] = 0;
}


static int generic_serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("generic_serial_write port %d", port);

	if (count == 0) {
		dbg("write request of 0 bytes");
		return (0);
	}

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		if (serial->write_urb[port].status == -EINPROGRESS) {
			dbg ("already writing");
			return (0);
		}

		count = (count > serial->bulk_out_size[port]) ? serial->bulk_out_size[port] : count;

		if (from_user) {
			copy_from_user(serial->write_urb[port].transfer_buffer, buf, count);
		}
		else {
			memcpy (serial->write_urb[port].transfer_buffer, buf, count);
		}  

		/* send the data out the bulk port */
		serial->write_urb[port].transfer_buffer_length = count;

		if (usb_submit_urb(&serial->write_urb[port]))
			dbg("usb_submit_urb(write bulk) failed");

		return (count);
	}
	
	/* no bulk out, so return 0 bytes written */
	return (0);
} 


static int generic_write_room (struct tty_struct *tty) 
{
	struct usb_serial *serial = (struct usb_serial *)tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;
	int room;

	dbg("generic_write_room port %d", port);
	
	if (serial->num_bulk_out) {
		if (serial->write_urb[port].status == -EINPROGRESS)
			room = 0;
		else
			room = serial->bulk_out_size[port];
		dbg("generic_write_room returns %d", room);
		return (room);
	}
	
	return (0);
}


static int generic_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial *serial = (struct usb_serial *)tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("generic_chars_in_buffer port %d", port);
	
	if (serial->num_bulk_out) {
		if (serial->write_urb[port].status == -EINPROGRESS) {
			return (serial->bulk_out_size[port]);
		}
	}

	return (0);
}


static void generic_throttle (struct tty_struct *tty) 
{
	/* do nothing for the generic device */
	return;
}


static void generic_unthrottle (struct tty_struct *tty) 
{
	/* do nothing for the generic device */
	return;
}


static int generic_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	/* generic driver doesn't support any ioctls yet */
	return -ENOIOCTLCMD;
}


static void generic_set_termios (struct tty_struct *tty, struct termios * old)
{
	/* generic driver doesn't really care about setting any line settings */
	return;
}


static void generic_read_bulk_callback (struct urb *urb)
{
	struct usb_serial *serial = (struct usb_serial *)urb->context;
       	struct tty_struct *tty = serial->tty; 
       	unsigned char *data = urb->transfer_buffer;
	int i;

	dbg("generic_read_bulk_callback");

	if (urb->status) {
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

#ifdef DEBUG
	if (urb->actual_length) {
		printk (KERN_DEBUG __FILE__ ": data read - length = %d, data = ", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i) {
			printk ("%.2x ", data[i]);
		}
		printk ("\n");
	}
#endif
	
	if (urb->actual_length) {
		for (i = 0; i < urb->actual_length ; ++i) {
			 tty_insert_flip_char(tty, data[i], 0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	if (usb_submit_urb(urb))
		dbg("failed resubmitting read urb");

	return;
}


static void generic_write_bulk_callback (struct urb *urb)
{
	struct usb_serial *serial = (struct usb_serial *) urb->context; 
       	struct tty_struct *tty = serial->tty; 

	dbg("generic_write_bulk_callback");

	if (urb->status) {
		dbg("nonzero write bulk status received: %d", urb->status);
		return;
	}

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);
	
	return;
}



static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_serial *serial = NULL;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_out_endpoint[MAX_NUM_PORTS];
	struct usb_serial_device_type *type;
	int device_num;
	int minor;
	int buffer_size;
	int i;
	char interrupt_pipe;
	char bulk_in_pipe;
	char bulk_out_pipe;
	int num_interrupt_in = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	int num_ports;
	
	/* loop through our list of known serial converters, and see if this device matches */
	device_num = 0;
	while (usb_serial_devices[device_num] != NULL) {
		type = usb_serial_devices[device_num];
		dbg ("Looking at %s Vendor id=%.4x Product id=%.4x", type->name, *(type->idVendor), *(type->idProduct));

		/* look at the device descriptor */
		if ((dev->descriptor.idVendor == *(type->idVendor)) &&
		    (dev->descriptor.idProduct == *(type->idProduct))) {

			dbg("descriptor matches...looking at the endpoints");

			/* descriptor matches, let's try to find the endpoints needed */
			interrupt_pipe = bulk_in_pipe = bulk_out_pipe = HAS_NOT;
			
			/* check out the endpoints */
			interface = &dev->actconfig->interface[ifnum].altsetting[0];
			for (i = 0; i < interface->bNumEndpoints; ++i) {
				endpoint = &interface->endpoint[i];
		
				if ((endpoint->bEndpointAddress & 0x80) &&
				    ((endpoint->bmAttributes & 3) == 0x02)) {
					/* we found a bulk in endpoint */
					dbg("found bulk in");
					bulk_in_pipe = HAS;
					bulk_in_endpoint[num_bulk_in] = endpoint;
					++num_bulk_in;
				}

				if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
				    ((endpoint->bmAttributes & 3) == 0x02)) {
					/* we found a bulk out endpoint */
					dbg("found bulk out");
					bulk_out_pipe = HAS;
					bulk_out_endpoint[num_bulk_out] = endpoint;
					++num_bulk_out;
				}
		
				if ((endpoint->bEndpointAddress & 0x80) &&
				    ((endpoint->bmAttributes & 3) == 0x03)) {
					/* we found a interrupt in endpoint */
					dbg("found interrupt in");
					interrupt_pipe = HAS;
					interrupt_in_endpoint[num_interrupt_in] = endpoint;
					++num_interrupt_in;
				}

			}
	
			/* verify that we found all of the endpoints that we need */
			if ((interrupt_pipe & type->needs_interrupt_in) &&
			    (bulk_in_pipe & type->needs_bulk_in) &&
			    (bulk_out_pipe & type->needs_bulk_out)) {
				/* found all that we need */
				info("%s converter detected", type->name);

#ifdef CONFIG_USB_SERIAL_GENERIC
				if (type == &generic_device)
					num_ports = num_bulk_out;
				else
#endif
					num_ports = type->num_ports;

				serial = get_free_serial (num_ports, &minor);
				if (serial == NULL) {
					err("No more free serial devices");
					return NULL;
				}
	
			       	serial->dev = dev;
				serial->type = type;
				serial->minor = minor;
				serial->num_ports = num_ports;
				serial->num_bulk_in = num_bulk_in;
				serial->num_bulk_out = num_bulk_out;
				serial->num_interrupt_in = num_interrupt_in;

				/* if this device type has a startup function, call it */
				if (type->startup) {
					if (type->startup (serial)) {
						return_serial (serial);
						return NULL;
					}
				}

				/* set up the endpoint information */
				for (i = 0; i < num_bulk_in; ++i) {
					buffer_size = bulk_in_endpoint[i]->wMaxPacketSize;
					serial->bulk_in_buffer[i] = kmalloc (buffer_size, GFP_KERNEL);
					if (!serial->bulk_in_buffer[i]) {
						err("Couldn't allocate bulk_in_buffer");
						goto probe_error;
					}
					if (serial->type->read_bulk_callback) {
						FILL_BULK_URB(&serial->read_urb[i], dev, usb_rcvbulkpipe (dev, bulk_in_endpoint[i]->bEndpointAddress),
								serial->bulk_in_buffer[i], buffer_size, serial->type->read_bulk_callback, serial);
					} else {
						FILL_BULK_URB(&serial->read_urb[i], dev, usb_rcvbulkpipe (dev, bulk_in_endpoint[i]->bEndpointAddress),
								serial->bulk_in_buffer[i], buffer_size, generic_read_bulk_callback, serial);
					}
				}

				for (i = 0; i < num_bulk_out; ++i) {
					serial->bulk_out_size[i] = bulk_out_endpoint[i]->wMaxPacketSize;
					serial->bulk_out_buffer[i] = kmalloc (serial->bulk_out_size[i], GFP_KERNEL);
					if (!serial->bulk_out_buffer[i]) {
						err("Couldn't allocate bulk_out_buffer");
						goto probe_error;
					}
					if (serial->type->write_bulk_callback) {
						FILL_BULK_URB(&serial->write_urb[i], dev, usb_sndbulkpipe (dev, bulk_out_endpoint[i]->bEndpointAddress),
								serial->bulk_out_buffer[i], serial->bulk_out_size[i], serial->type->write_bulk_callback, serial);
					} else {
						FILL_BULK_URB(&serial->write_urb[i], dev, usb_sndbulkpipe (dev, bulk_out_endpoint[i]->bEndpointAddress),
								serial->bulk_out_buffer[i], serial->bulk_out_size[i], generic_write_bulk_callback, serial);
					}
				}

#if 0 /* use this code when WhiteHEAT is up and running */
				for (i = 0; i < num_interrupt_in; ++i) {
					buffer_size = interrupt_in_endpoint[i]->wMaxPacketSize;
					serial->interrupt_in_buffer[i] = kmalloc (buffer_size, GFP_KERNEL);
					if (!serial->interrupt_in_buffer[i]) {
						err("Couldn't allocate interrupt_in_buffer");
						goto probe_error;
					}
					FILL_INT_URB(&serial->control_urb[i], dev, usb_rcvintpipe (dev, interrupt_in_endpoint[i]->bEndpointAddress),
							serial->interrupt_in_buffer[i], buffer_size, serial_control_irq,
							serial, interrupt_in_endpoint[i]->bInterval);
				}
#endif

				for (i = 0; i < serial->num_ports; ++i) {
					info("%s converter now attached to ttyUSB%d", type->name, serial->minor + i);
				}

				MOD_INC_USE_COUNT;

				return serial;
			} else {
				info("descriptors matched, but endpoints did not");
			}
		}

		/* look at the next type in our list */
		++device_num;
	}

probe_error:
	if (serial) {
		for (i = 0; i < num_bulk_in; ++i)
			if (serial->bulk_in_buffer[i])
				kfree (serial->bulk_in_buffer[i]);
		for (i = 0; i < num_bulk_out; ++i)
			if (serial->bulk_out_buffer[i])
				kfree (serial->bulk_out_buffer[i]);
		for (i = 0; i < num_interrupt_in; ++i)
			if (serial->interrupt_in_buffer[i])
				kfree (serial->interrupt_in_buffer[i]);
	}
	return NULL;
}


static void usb_serial_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_serial *serial = (struct usb_serial *) ptr;
	int i;

	if (serial) {
		/* need to stop any transfers...*/
		for (i = 0; i < serial->num_ports; ++i) {
			usb_unlink_urb (&serial->write_urb[i]);
			usb_unlink_urb (&serial->read_urb[i]);
			serial->active[i] = 0;
		}

		/* free up any memory that we allocated */
		for (i = 0; i < serial->num_bulk_in; ++i)
			if (serial->bulk_in_buffer[i])
				kfree (serial->bulk_in_buffer[i]);
		for (i = 0; i < serial->num_bulk_out; ++i)
			if (serial->bulk_out_buffer[i])
				kfree (serial->bulk_out_buffer[i]);
		for (i = 0; i < serial->num_interrupt_in; ++i)
			if (serial->interrupt_in_buffer[i])
				kfree (serial->interrupt_in_buffer[i]);

		for (i = 0; i < serial->num_ports; ++i) {
			info("%s converter now disconnected from ttyUSB%d", serial->type->name, serial->minor + i);
		}

		/* return the minor range that this device had */
		return_serial (serial);

		/* free up any memory that we allocated */
		kfree (serial);

	} else {
		info("device disconnected");
	}
	
	MOD_DEC_USE_COUNT;
}


static struct tty_driver serial_tty_driver = {
	magic:			TTY_DRIVER_MAGIC,
	driver_name:		"usb",
	name:			"ttyUSB",
	major:			SERIAL_TTY_MAJOR,
	minor_start:		0,
	num:			SERIAL_TTY_MINORS,
	type:			TTY_DRIVER_TYPE_SERIAL,
	subtype:		SERIAL_TYPE_NORMAL,
	flags:			TTY_DRIVER_REAL_RAW,
	refcount:		&serial_refcount,
	table:			serial_tty,
	proc_entry:		NULL,
	other:			NULL,
	termios:		serial_termios,
	termios_locked:		serial_termios_locked,
	
	open:			serial_open,
	close:			serial_close,
	write:			serial_write,
	put_char:		NULL,
	flush_chars:		NULL,
	write_room:		serial_write_room,
	ioctl:			serial_ioctl,
	set_termios:		serial_set_termios,
	set_ldisc:		NULL, 
	throttle:		serial_throttle,
	unthrottle:		serial_unthrottle,
	stop:			NULL,
	start:			NULL,
	hangup:			NULL,
	break_ctl:		NULL,
	wait_until_sent:	NULL,
	send_xchar:		NULL,
	read_proc:		NULL,
	chars_in_buffer:	serial_chars_in_buffer,
	flush_buffer:		NULL
};


int usb_serial_init(void)
{
	int i;

	/* Initalize our global data */
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		serial_table[i] = NULL;
	}

	/* register the tty driver */
	serial_tty_driver.init_termios		= tty_std_termios;
	serial_tty_driver.init_termios.c_cflag	= B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	if (tty_register_driver (&serial_tty_driver)) {
		err("failed to register tty driver");
		return -EPERM;
	}
	
	/* register the USB driver */
	if (usb_register(&usb_serial_driver) < 0) {
		tty_unregister_driver(&serial_tty_driver);
		return -1;
	}

	info("support registered");
	return 0;
}


void usb_serial_exit(void)
{
	tty_unregister_driver(&serial_tty_driver);
	usb_deregister(&usb_serial_driver);
}


module_init(usb_serial_init);
module_exit(usb_serial_exit);


