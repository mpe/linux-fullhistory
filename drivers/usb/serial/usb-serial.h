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
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 */


#ifndef __LINUX_USB_SERIAL_H
#define __LINUX_USB_SERIAL_H

#include <linux/config.h>

#define SERIAL_TTY_MAJOR	188	/* Nice legal number now */
#define SERIAL_TTY_MINORS	16	/* Actually we are allowed 255, but this is good for now */

#define MAX_NUM_PORTS		8	/* The maximum number of ports one device can grab at once */

#define USB_SERIAL_MAGIC	0x6702	/* magic number for usb_serial struct */
#define USB_SERIAL_PORT_MAGIC	0x7301	/* magic number for usb_serial_port struct */

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))


struct usb_serial_port {
	int			magic;
	struct usb_serial	*serial;	/* pointer back to the owner of this port */
	struct tty_struct *	tty;		/* the coresponding tty for this port */
	unsigned char		minor;
	unsigned char		number;
	char			active;		/* someone has this device open */

	struct usb_endpoint_descriptor * interrupt_in_endpoint;
	__u8			interrupt_in_interval;
	unsigned char *		interrupt_in_buffer;
	struct urb *		control_urb;

	unsigned char *		bulk_in_buffer;
	struct urb *		read_urb;

	unsigned char *		bulk_out_buffer;
	int			bulk_out_size;
	struct urb *		write_urb;
	void *			private;	/* data private to the specific driver */
};

struct usb_serial {
	int				magic;
	struct usb_device *		dev;
	struct usb_serial_device_type *	type;			/* the type of usb serial device this is */
	struct tty_driver *		tty_driver;		/* the tty_driver for this device */
	unsigned char			minor;			/* the starting minor number for this device */
	unsigned char			num_ports;		/* the number of ports this device has */
	char				num_interrupt_in;	/* number of interrupt in endpoints we have */
	char				num_bulk_in;		/* number of bulk in endpoints we have */
	char				num_bulk_out;		/* number of bulk out endpoints we have */
	struct usb_serial_port		port[MAX_NUM_PORTS];

	/* FIXME! These should move to the private area of the keyspan driver */
	int			tx_room;
	int			tx_throttled;
	wait_queue_head_t 	write_wait;

	void *			private;		/* data private to the specific driver */
};


#define MUST_HAVE_NOT	0x01
#define MUST_HAVE	0x02
#define DONT_CARE	0x03

#define	HAS		0x02
#define HAS_NOT		0x01

#define NUM_DONT_CARE	(-1)


/* This structure defines the individual serial converter. */
struct usb_serial_device_type {
	char	*name;
	__u16	*idVendor;
	__u16	*idProduct;
	char	needs_interrupt_in;
	char	needs_bulk_in;
	char	needs_bulk_out;
	char	num_interrupt_in;
	char	num_bulk_in;
	char	num_bulk_out;
	char	num_ports;		/* number of serial ports this device has */

	/* function call to make before accepting driver */
	int (*startup) (struct usb_serial *serial);	/* return 0 to continue initialization, anything else to abort */
	
	/* serial function calls */
	int  (*open)		(struct usb_serial_port *port, struct file * filp);
	void (*close)		(struct usb_serial_port *port, struct file * filp);
	int  (*write)		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
	int  (*write_room)	(struct usb_serial_port *port);
	int  (*ioctl)		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
	void (*set_termios)	(struct usb_serial_port *port, struct termios * old);
	void (*break_ctl)	(struct usb_serial_port *port, int break_state);
	int  (*chars_in_buffer)	(struct usb_serial_port *port);
	void (*throttle)	(struct usb_serial_port *port);
	void (*unthrottle)	(struct usb_serial_port *port);
	
	void (*read_bulk_callback)(struct urb *urb);
	void (*write_bulk_callback)(struct urb *urb);
};



extern struct usb_serial_device_type handspring_device;
extern struct usb_serial_device_type whiteheat_fake_device;
extern struct usb_serial_device_type whiteheat_device;
extern struct usb_serial_device_type ftdi_sio_device;
extern struct usb_serial_device_type keyspan_pda_fake_device;
extern struct usb_serial_device_type keyspan_pda_device;


/* determine if we should include the EzUSB loader functions */
#if defined(CONFIG_USB_SERIAL_KEYSPAN_PDA) || defined(CONFIG_USB_SERIAL_WHITEHEAT)
	#define	USES_EZUSB_FUNCTIONS
	extern int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest);
	extern int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit);
#else
	#undef 	USES_EZUSB_FUNCTIONS
#endif


/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int serial_paranoia_check (struct usb_serial *serial, const char *function)
{
	if (!serial) {
		dbg("%s - serial == NULL", function);
		return -1;
	}
	if (serial->magic != USB_SERIAL_MAGIC) {
		dbg("%s - bad magic number for serial", function);
		return -1;
	}
	if (!serial->type) {
		dbg("%s - serial->type == NULL!", function);
		return -1;
	}

	return 0;
}


static inline int port_paranoia_check (struct usb_serial_port *port, const char *function)
{
	if (!port) {
		dbg("%s - port == NULL", function);
		return -1;
	}
	if (port->magic != USB_SERIAL_PORT_MAGIC) {
		dbg("%s - bad magic number for port", function);
		return -1;
	}
	if (!port->serial) {
		dbg("%s - port->serial == NULL", function);
		return -1;
	}
	if (!port->tty) {
		dbg("%s - port->tty == NULL", function);
		return -1;
	}

	return 0;
}


#endif	/* ifdef __LINUX_USB_SERIAL_H */

