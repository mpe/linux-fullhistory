/*
  Keyspan USB to Serial Converter driver
 
  (C) Copyright (C) 2000
      Hugh Blemings <hugh@linuxcare.com>
   
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  See http://www.linuxcare.com.au/hugh/keyspan.html for more
  information on this driver.
  
  Code in this driver inspired by and in a number of places taken
  from Brian Warner's original Keyspan-PDA driver.

  This driver has been put together with the support of Innosys, Inc.
  and Keyspan, Inc the manufacturers of the Keyspan USB-serial products.
  Thanks Guys :)
  
  Tip 'o the hat to Linuxcare for supporting staff in their work on
  open source projects.

  Sat Jul  8 11:11:48 EST 2000 Hugh
    First public release - nothing works except the firmware upload.
    Tested on PPC and x86 architectures, seems to behave...

*/


#include <linux/config.h>

#ifdef CONFIG_USB_SERIAL_KEYSPAN

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

struct ezusb_hex_record {
	__u16 address;
	__u8 data_size;
	__u8 data[16];
};

	/* Conditionally include firmware images, if they aren't
	   included create a null pointer instead.  Current 
	   firmware images aren't optimised to remove duplicate
	   addresses. */
#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA28
        #include "keyspan_usa28_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa28_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA28X
        #include "keyspan_usa28x_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa28x_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA19
        #include "keyspan_usa19_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa19_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA18X
        #include "keyspan_usa18x_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa18x_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA19W
        #include "keyspan_usa19w_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa19w_firmware = NULL;
#endif

	/* Include Keyspan message headers (not here yet, need some tweaks
	   to get clean build) */
/*#include "keyspan_usa26msg.h"*/
/*#include "keyspan_usa28msg.h"*/
	
	/* If you don't get debugging output, uncomment the following
	   two lines to enable cheat. */
#undef 	dbg
#define	dbg	printk


	/* function prototypes for Keyspan serial converter */
static int  keyspan_open		(struct usb_serial_port *port,
					 struct file *filp);
static void keyspan_close		(struct usb_serial_port *port,
					 struct file *filp);
static int  keyspan_startup		(struct usb_serial *serial);
static void keyspan_shutdown		(struct usb_serial *serial);
static void keyspan_rx_interrupt 	(struct urb *urb);
static void keyspan_rx_throttle		(struct usb_serial_port *port);
static void keyspan_rx_unthrottle	(struct usb_serial_port *port);
static int  keyspan_write_room		(struct usb_serial_port *port);
static int  keyspan_write		(struct usb_serial_port *port,
					 int from_user,
					 const unsigned char *buf,
					 int count);
static void keyspan_write_bulk_callback (struct urb *urb);
static int  keyspan_chars_in_buffer 	(struct usb_serial_port *port);
static int  keyspan_ioctl		(struct usb_serial_port *port,
					 struct file *file,
					 unsigned int cmd,
					 unsigned long arg);
static void keyspan_set_termios	(struct usb_serial_port *port,
					 struct termios *old);
static void keyspan_break_ctl	(struct usb_serial_port *port,
					 int break_state);
static int  keyspan_fake_startup	(struct usb_serial *serial);


	/* Functions - mostly stubs for now */

static void keyspan_rx_interrupt (struct urb *urb)
{

}


static void keyspan_rx_throttle (struct usb_serial_port *port)
{
	dbg("keyspan_rx_throttle port %d", port->number);
}


static void keyspan_rx_unthrottle (struct usb_serial_port *port)
{
	dbg("keyspan_rx_unthrottle port %d", port->number);
}


static void keyspan_break_ctl (struct usb_serial_port *port, int break_state)
{
	dbg("keyspan_break_ctl");
}


static void keyspan_set_termios (struct usb_serial_port *port, 
				     struct termios *old_termios)
{
	dbg("keyspan_set_termios");
}

static int keyspan_ioctl(struct usb_serial_port *port, struct file *file,
			     unsigned int cmd, unsigned long arg)
{

	dbg("keyspan_ioctl_info");

	return -ENOIOCTLCMD;
}

static int keyspan_write(struct usb_serial_port *port, int from_user, 
			     const unsigned char *buf, int count)
{
	dbg("keyspan_write called\n");
	return(count);
}


static void keyspan_write_bulk_callback (struct urb *urb)
{

	dbg("keyspan_write_bulk_callback called\n");
}


static int keyspan_write_room (struct usb_serial_port *port)
{
	dbg("keyspan_write_room called\n");
	return (1);

}


static int keyspan_chars_in_buffer (struct usb_serial_port *port)
{
	return (0);
}


static int keyspan_open (struct usb_serial_port *port, struct file *filp)
{
	dbg("keyspan_open called\n");
	return (0);
}


static void keyspan_close(struct usb_serial_port *port, struct file *filp)
{
	dbg("keyspan_close called\n");
}


	/* download the firmware to a pre-renumeration device */
static int keyspan_fake_startup (struct usb_serial *serial)
{
	int 				response;
	const struct ezusb_hex_record 	*record;
	char				*fw_name;

	dbg("Keyspan startup version %04x product %04x\n", serial->dev->descriptor.bcdDevice,
		       					serial->dev->descriptor.idProduct); 
	
	if ((serial->dev->descriptor.bcdDevice & 0x8000) != 0x8000) {
		dbg("Firmware already loaded.  Quitting.\n");
		return(1);
	}

		/* Select firmware image on the basis of idProduct */
	switch (serial->dev->descriptor.idProduct) {
		case 0x0101: record = &keyspan_usa28_firmware[0];
			     fw_name = "USA28";
			     break;
			     
		case 0x0102: record = &keyspan_usa28x_firmware[0];
			     fw_name = "USA28X";
			     break;

		case 0x0103: record = &keyspan_usa19_firmware[0];
			     fw_name = "USA19";
			     break;
			     
		case 0x0105: record = &keyspan_usa18x_firmware[0];
			     fw_name = "USA18X";
			     break;
			     
		case 0x0106: record = &keyspan_usa19w_firmware[0];
			     fw_name = "USA19W";
			     break;
		
		default:     record = NULL;
			     fw_name = "Unknown";
			     break;
	}

	if (record == NULL) {
		err("Required keyspan firmware image (%s) unavailable.\n", fw_name);
		return(1);
	}

	dbg("Uploading Keyspan %s firmware.\n", fw_name);

		/* download the firmware image */
	response = ezusb_set_reset(serial, 1);

	while(record->address != 0xffff) {
		response = ezusb_writememory(serial, record->address,
					     (unsigned char *)record->data,
					     record->data_size, 0xa0);
		if (response < 0) {
			err("ezusb_writememory failed for Keyspan"
			    "firmware (%d %04X %p %d)",
			    response, 
			    record->address, record->data, record->data_size);
			break;
		}
		record++;
	}
		/* bring device out of reset. Renumeration will occur in a moment
	   	   and the new device will bind to the real driver */
	response = ezusb_set_reset(serial, 0);

		/* we want this device to fail to have a driver assigned to it. */
	return (1);
}


	/* Gets called by the "real" driver (ie once firmware is loaded
	   and renumeration has taken place. */
static int keyspan_startup (struct usb_serial *serial)
{
	dbg("keyspan_startup called.\n");

	return (0);
}

static void keyspan_shutdown (struct usb_serial *serial)
{
	dbg("keyspan_shutdown called.\n");
	
}

	/* Miscellaneous defines, datastructures etc. */

#define KEYSPAN_VENDOR_ID		0x06cd

	/* Device info needed for the Keyspan serial converter */
static __u16	keyspan_vendor_id		= KEYSPAN_VENDOR_ID;

    /* Product IDs for the five products supported, pre-renumeration */
static __u16	keyspan_usa18x_pre_product_id	= 0x0105;	
static __u16	keyspan_usa19_pre_product_id	= 0x0103;	
static __u16	keyspan_usa19w_pre_product_id	= 0x0106;	
static __u16	keyspan_usa28_pre_product_id	= 0x0101;	
static __u16	keyspan_usa28x_pre_product_id	= 0x0102;	

    /* Product IDs post-renumeration */
static __u16	keyspan_usa18x_product_id	= 0x0112;	
static __u16	keyspan_usa19_product_id	= 0x0107;	
static __u16	keyspan_usa19w_product_id	= 0x0108;	
static __u16	keyspan_usa28_product_id	= 0x010f;	
static __u16	keyspan_usa28x_product_id	= 0x0110;	

    /* Structs for the devices, pre and post renumeration.
       These are incomplete at present - HAB 20000708  */
struct usb_serial_device_type keyspan_usa18x_pre_device = {
	name:			"Keyspan USA18X - (prerenumeration)",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa18x_pre_product_id,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		keyspan_fake_startup	
};

struct usb_serial_device_type keyspan_usa19_pre_device = {
	name:			"Keyspan USA19 - (prerenumeration)",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa19_pre_product_id,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		keyspan_fake_startup	
};


struct usb_serial_device_type keyspan_usa19w_pre_device = {
	name:			"Keyspan USA19W - (prerenumeration)",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa19w_pre_product_id,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		keyspan_fake_startup	
};


struct usb_serial_device_type keyspan_usa28_pre_device = {
	name:			"Keyspan USA28 - (prerenumeration)",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa28_pre_product_id,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	startup:		keyspan_fake_startup	
};

struct usb_serial_device_type keyspan_usa28x_pre_device = {
	name:			"Keyspan USA28X - (prerenumeration)",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa28x_pre_product_id,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	startup:		keyspan_fake_startup	
};


struct usb_serial_device_type keyspan_usa18x_device = {
	name:			"Keyspan USA18X",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa18x_product_id,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	open:			keyspan_open,
	close:			keyspan_close,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	set_termios:		keyspan_set_termios,
};

struct usb_serial_device_type keyspan_usa19_device = {
	name:			"Keyspan USA19",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa19_product_id,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
	write_bulk_callback: 	keyspan_write_bulk_callback,
	read_int_callback:	keyspan_rx_interrupt,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,
};


struct usb_serial_device_type keyspan_usa19w_device = {
	name:			"Keyspan USA19W",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa19w_product_id,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	open:			keyspan_open,
	close:			keyspan_close,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	set_termios:		keyspan_set_termios,
};


struct usb_serial_device_type keyspan_usa28_device = {
	name:			"Keyspan USA28",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa28_product_id,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	open:			keyspan_open,
	close:			keyspan_close,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	set_termios:		keyspan_set_termios,
};


struct usb_serial_device_type keyspan_usa28x_device = {
	name:			"Keyspan USA28X",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_usa28x_product_id,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	open:			keyspan_open,
	close:			keyspan_close,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	set_termios:		keyspan_set_termios,
};




#endif	/* CONFIG_USB_SERIAL_KEYSPAN */

