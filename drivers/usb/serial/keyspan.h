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
  
  See keyspan.c for update history.

*/

#ifndef __LINUX_USB_SERIAL_KEYSPAN_H
#define __LINUX_USB_SERIAL_KEYSPAN_H

#include <linux/config.h>

	/* Function prototypes for Keyspan serial converter */
static int  keyspan_open		(struct usb_serial_port *port,
					 struct file *filp);
static void keyspan_close		(struct usb_serial_port *port,
					 struct file *filp);
static int  keyspan_startup		(struct usb_serial *serial);
static void keyspan_shutdown		(struct usb_serial *serial);
static void keyspan_rx_throttle		(struct usb_serial_port *port);
static void keyspan_rx_unthrottle	(struct usb_serial_port *port);
static int  keyspan_write_room		(struct usb_serial_port *port);
static int  keyspan_write		(struct usb_serial_port *port,
					 int from_user,
					 const unsigned char *buf,
					 int count);
static void keyspan_write_bulk_callback (struct urb *urb);
static void keyspan_read_bulk_callback  (struct urb *urb);
static int  keyspan_chars_in_buffer 	(struct usb_serial_port *port);
static int  keyspan_ioctl		(struct usb_serial_port *port,
					 struct file *file,
					 unsigned int cmd,
					 unsigned long arg);
static void keyspan_set_termios		(struct usb_serial_port *port,
					 struct termios *old);
static void keyspan_break_ctl		(struct usb_serial_port *port,
					 int break_state);
static int  keyspan_fake_startup	(struct usb_serial *serial);

static int  keyspan_usa19_calc_baud	(u32 baud_rate, u8 *rate_hi, 
					 u8 *rate_low);
static void keyspan_usa19_setup_urbs	(struct usb_serial *serial);
static int  keyspan_usa19_send_setup	(struct usb_serial *serial,
					 struct usb_serial_port *port);


	/* Functions from usbserial.c for ezusb firmware handling */
extern int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit);
extern int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest);

	/* Struct used for firmware */
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


	/* Values used for baud rate calculation - device specific */
#define	KEYSPAN_INVALID_BAUD_RATE		(-1)
#define	KEYSPAN_BAUD_RATE_OK			(0)
#define	USA19_BAUDCLK				(12000000L)

	/* Device info for the Keyspan serial converter */
#define KEYSPAN_VENDOR_ID			(0x06cd)
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
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		3,
	num_bulk_out:		4,
	num_ports:		1,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
	write_bulk_callback: 	keyspan_write_bulk_callback,
	read_int_callback:	keyspan_read_bulk_callback,
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
	write:			keyspan_write,
	write_room:		keyspan_write_room,
	write_bulk_callback: 	keyspan_write_bulk_callback,
	read_int_callback:	keyspan_read_bulk_callback,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,

				

};



#endif
