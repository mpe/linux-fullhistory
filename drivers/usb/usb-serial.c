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
 * See Documentation/usb-serial.txt for more information on using this driver.
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

#ifdef CONFIG_USB_SERIAL_WHITEHEAT
#include "whiteheat.h"		/* firmware for the ConnectTech WhiteHEAT device */
#endif

#define DEBUG

#include "usb.h"

/* Module information */
MODULE_AUTHOR("Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux-usb/");
MODULE_DESCRIPTION("USB Serial Driver");

#ifdef CONFIG_USB_SERIAL_GENERIC
static __u16	vendor	= 0x05f9;
static __u16	product	= 0xffff;
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");
#endif

/* USB Serial devices vendor ids and device ids that this driver supports */
#define BELKIN_VENDOR_ID		0x056c
#define BELKIN_SERIAL_CONVERTER		0x8007
#define PERACOM_VENDOR_ID		0x0565
#define PERACOM_SERIAL_CONVERTER	0x0001
#define CONNECT_TECH_VENDOR_ID		0x0710
#define CONNECT_TECH_FAKE_WHITE_HEAT_ID	0x0001
#define CONNECT_TECH_WHITE_HEAT_ID	0x8001
#define HANDSPRING_VENDOR_ID		0x082d
#define HANDSPRING_VISOR_ID		0x0100


#define SERIAL_MAJOR	188	/* Nice legal number now */
#define NUM_PORTS	16	/* Actually we are allowed 255, but this is good for now */


static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_serial_disconnect(struct usb_device *dev, void *ptr);


#define MAX_ENDPOINTS	8

struct usb_serial_state {
	struct usb_device *		dev;
	struct usb_serial_device_type *	type;
	void *				irq_handle;
	unsigned int			irqpipe;
	struct tty_struct *		tty;		/* the coresponding tty for this device */
	unsigned char			number;
	char				present;
	char				active;

	char			num_interrupt_in;	/* number of interrupt in endpoints we have */
	char			interrupt_in_inuse;	/* if the interrupt in endpoint is in use */
	__u8			interrupt_in_endpoint[MAX_ENDPOINTS];
	__u8			interrupt_in_interval[MAX_ENDPOINTS];
	__u16			interrupt_in_size[MAX_ENDPOINTS];	/* the size of the interrupt in endpoint */
	unsigned int		interrupt_in_pipe[MAX_ENDPOINTS];
	unsigned char *		interrupt_in_buffer[MAX_ENDPOINTS];
	void *			interrupt_in_transfer[MAX_ENDPOINTS];
	struct urb		control_urb;

	char			num_bulk_in;		/* number of bulk in endpoints we have */
	__u8			bulk_in_endpoint[MAX_ENDPOINTS];
	__u8			bulk_in_interval[MAX_ENDPOINTS];
	__u16			bulk_in_size[MAX_ENDPOINTS];		/* the size of the bulk in endpoint */
	unsigned int		bulk_in_pipe[MAX_ENDPOINTS];
	unsigned char *		bulk_in_buffer[MAX_ENDPOINTS];
	void *			bulk_in_transfer[MAX_ENDPOINTS];
	struct urb		read_urb;

	char			num_bulk_out;		/* number of bulk out endpoints we have */
	__u8			bulk_out_endpoint[MAX_ENDPOINTS];
	__u8			bulk_out_interval[MAX_ENDPOINTS];
	__u16			bulk_out_size[MAX_ENDPOINTS];		/* the size of the bulk out endpoint */
	unsigned int		bulk_out_pipe[MAX_ENDPOINTS];
	unsigned char *		bulk_out_buffer[MAX_ENDPOINTS];
	void *			bulk_out_transfer[MAX_ENDPOINTS];
	struct urb		write_urb;
};


#define MUST_HAVE_NOT	0x01
#define MUST_HAVE	0x02
#define DONT_CARE	0x03

#define	HAS		0x02
#define HAS_NOT		0x01

#define NUM_DONT_CARE	(-1)

/* local function prototypes */
static int serial_open (struct tty_struct *tty, struct file * filp);
static void serial_close (struct tty_struct *tty, struct file * filp);
static int serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count);
static void serial_put_char (struct tty_struct *tty, unsigned char ch);
static int serial_write_room (struct tty_struct *tty);
static int serial_chars_in_buffer (struct tty_struct *tty);
static void serial_throttle (struct tty_struct * tty);
static void serial_unthrottle (struct tty_struct * tty);


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

	/* function call to make before accepting driver */
	int (*startup) (struct usb_serial_state *serial);	/* return 0 to continue initialization, anything else to abort */
	
	/* serial function calls */
	int  (*open)(struct tty_struct * tty, struct file * filp);
	void (*close)(struct tty_struct * tty, struct file * filp);
	int  (*write)(struct tty_struct * tty, int from_user,const unsigned char *buf, int count);
	void (*put_char)(struct tty_struct *tty, unsigned char ch);
	int  (*write_room)(struct tty_struct *tty);
	int  (*chars_in_buffer)(struct tty_struct *tty);
	void (*throttle)(struct tty_struct * tty);
	void (*unthrottle)(struct tty_struct * tty);
};


/* function prototypes for a "generic" type serial converter (no flow control, not all endpoints needed) */
/* need to always compile these in, as some of the other devices use these functions as their own. */
static int  generic_serial_open		(struct tty_struct *tty, struct file *filp);
static void generic_serial_close	(struct tty_struct *tty, struct file *filp);
static int  generic_serial_write	(struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
static void generic_serial_put_char	(struct tty_struct *tty, unsigned char ch);
static int  generic_write_room		(struct tty_struct *tty);
static int  generic_chars_in_buffer	(struct tty_struct *tty);

#ifdef CONFIG_USB_SERIAL_GENERIC
/* All of the device info needed for the Generic Serial Converter */
static struct usb_serial_device_type generic_device = {
	name:			"Generic",
	idVendor:		&vendor,		/* use the user specified vendor id */
	idProduct:		&product,		/* use the user specified product id */
	needs_interrupt_in:	DONT_CARE,		/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,		/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,		/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	open:			generic_serial_open,
	close:			generic_serial_close,
	write:			generic_serial_write,
	put_char:		generic_serial_put_char,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
};
#endif

#if defined(CONFIG_USB_SERIAL_BELKIN) || defined(CONFIG_USB_SERIAL_PERACOM)
/* function prototypes for the eTek type converters (this includes Belkin and Peracom) */
static int  etek_serial_open		(struct tty_struct *tty, struct file *filp);
static void etek_serial_close		(struct tty_struct *tty, struct file *filp);
#endif

#ifdef CONFIG_USB_SERIAL_BELKIN
/* All of the device info needed for the Belkin Serial Converter */
static __u16	belkin_vendor_id	= BELKIN_VENDOR_ID;
static __u16	belkin_product_id	= BELKIN_SERIAL_CONVERTER;
static struct usb_serial_device_type belkin_device = {
	name:			"Belkin",
	idVendor:		&belkin_vendor_id,	/* the Belkin vendor id */
	idProduct:		&belkin_product_id,	/* the Belkin serial converter product id */
	needs_interrupt_in:	MUST_HAVE,		/* this device must have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	open:			etek_serial_open,
	close:			etek_serial_close,
	write:			generic_serial_write,
	put_char:		generic_serial_put_char,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
};
#endif


#ifdef CONFIG_USB_SERIAL_PERACOM
/* All of the device info needed for the Peracom Serial Converter */
static __u16	peracom_vendor_id	= PERACOM_VENDOR_ID;
static __u16	peracom_product_id	= PERACOM_SERIAL_CONVERTER;
static struct usb_serial_device_type peracom_device = {
	name:			"Peracom",
	idVendor:		&peracom_vendor_id,	/* the Peracom vendor id */
	idProduct:		&peracom_product_id,	/* the Peracom serial converter product id */
	needs_interrupt_in:	MUST_HAVE,		/* this device must have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	open:			etek_serial_open,
	close:			etek_serial_close,
	write:			generic_serial_write,
	put_char:		generic_serial_put_char,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
};
#endif


#ifdef CONFIG_USB_SERIAL_WHITEHEAT
/* function prototypes for the Connect Tech WhiteHEAT serial converter */
static int  whiteheat_serial_open	(struct tty_struct *tty, struct file *filp);
static void whiteheat_serial_close	(struct tty_struct *tty, struct file *filp);
static void whiteheat_throttle		(struct tty_struct *tty);
static void whiteheat_unthrottle	(struct tty_struct *tty);
static int  whiteheat_startup		(struct usb_serial_state *serial);

/* All of the device info needed for the Connect Tech WhiteHEAT */
static __u16	connecttech_vendor_id			= CONNECT_TECH_VENDOR_ID;
static __u16	connecttech_whiteheat_fake_product_id	= CONNECT_TECH_FAKE_WHITE_HEAT_ID;
static __u16	connecttech_whiteheat_product_id	= CONNECT_TECH_WHITE_HEAT_ID;
static struct usb_serial_device_type whiteheat_fake_device = {
	name:			"Connect Tech - WhiteHEAT - (prerenumeration)",
	idVendor:		&connecttech_vendor_id,			/* the Connect Tech vendor id */
	idProduct:		&connecttech_whiteheat_fake_product_id,	/* the White Heat initial product id */
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	startup:		whiteheat_startup	
};
static struct usb_serial_device_type whiteheat_device = {
	name:			"Connect Tech - WhiteHEAT",
	idVendor:		&connecttech_vendor_id,			/* the Connect Tech vendor id */
	idProduct:		&connecttech_whiteheat_product_id,	/* the White Heat real product id */
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	open:			whiteheat_serial_open,
	close:			whiteheat_serial_close,
	write:			generic_serial_write,
	put_char:		generic_serial_put_char,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
	throttle:		whiteheat_throttle,
	unthrottle:		whiteheat_unthrottle
};
#endif


#ifdef CONFIG_USB_SERIAL_VISOR
/* function prototypes for a handspring visor */
static int  visor_serial_open		(struct tty_struct *tty, struct file *filp);
static void visor_serial_close		(struct tty_struct *tty, struct file *filp);
static void visor_throttle		(struct tty_struct *tty);
static void visor_unthrottle		(struct tty_struct *tty);

/* All of the device info needed for the Handspring Visor */
static __u16	handspring_vendor_id	= HANDSPRING_VENDOR_ID;
static __u16	handspring_product_id	= HANDSPRING_VISOR_ID;
static struct usb_serial_device_type handspring_device = {
	name:			"Handspring Visor",
	idVendor:		&handspring_vendor_id,	/* the Handspring vendor ID */
	idProduct:		&handspring_product_id,	/* the Handspring Visor product id */
	needs_interrupt_in:	MUST_HAVE_NOT,		/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		2,
	num_bulk_out:		2,
	open:			visor_serial_open,
	close:			visor_serial_close,
	write:			generic_serial_write,
	put_char:		generic_serial_put_char,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
	throttle:		visor_throttle,
	unthrottle:		visor_unthrottle
};
#endif

/* To add support for another serial converter, create a usb_serial_device_type
   structure for that device, and add it to this list, making sure that the last
   entry is NULL. */
static struct usb_serial_device_type *usb_serial_devices[] = {
#ifdef CONFIG_USB_SERIAL_GENERIC
	&generic_device,
#endif
#ifdef CONFIG_USB_SERIAL_WHITEHEAT
	&whiteheat_fake_device,
	&whiteheat_device,
#endif
#ifdef CONFIG_USB_SERIAL_BELKIN
	&belkin_device,
#endif
#ifdef CONFIG_USB_SERIAL_PERACOM
	&peracom_device,
#endif
#ifdef CONFIG_USB_SERIAL_VISOR
	&handspring_device,
#endif
	NULL
};


static struct usb_driver usb_serial_driver = {
	"serial",
	usb_serial_probe,
	usb_serial_disconnect,
	{ NULL, NULL }
};

static int			serial_refcount;
static struct tty_struct *	serial_tty[NUM_PORTS];
static struct termios *		serial_termios[NUM_PORTS];
static struct termios *		serial_termios_locked[NUM_PORTS];
static struct usb_serial_state	serial_state_table[NUM_PORTS];



static void serial_read_bulk (struct urb *urb)
{
	struct usb_serial_state *serial = (struct usb_serial_state *)urb->context;
       	struct tty_struct *tty = serial->tty; 
       	unsigned char *data = urb->transfer_buffer;
	int i;

	dbg("serial_read_irq");

	if (urb->status) {
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

	if (urb->actual_length)
		dbg("%d %s", urb->actual_length, data);

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


static void serial_write_bulk (struct urb *urb)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) urb->context; 
       	struct tty_struct *tty = serial->tty; 

	dbg("serial_write_irq");

	if (urb->status) {
		dbg("nonzero write bulk status received: %d", urb->status);
		return;
	}

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);
	
	return;
}



/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int serial_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial;
	
	dbg("serial_open");

	/* assign a serial object to the tty pointer */
	serial = &serial_state_table [MINOR(tty->device)-tty->driver.minor_start];

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
	 
	/* pass on to the driver specific version of this function */
	if (serial->type->open) {
		return (serial->type->open(tty, filp));
	}
		
	return (0);
}


static void serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	dbg("serial_close");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return;
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->present) {
		dbg("no device registered");
		return;
	}
	if (!serial->active) {
		dbg ("device already open");
		return;
	}

	/* pass on to the driver specific version of this function */
	if (serial->type->close) {
		serial->type->close(tty, filp);
	}
}	


static int serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	
	dbg("serial_write");

	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return (-ENODEV);
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return (-ENODEV);
	}
	if (!serial->present) {
		dbg("device not registered");
		return (-EINVAL);
	}
	if (!serial->active) {
		dbg ("device not opened");
		return (-EINVAL);
	}
	
	/* pass on to the driver specific version of this function */
	if (serial->type->write) {
		return (serial->type->write(tty, from_user, buf, count));
	}

	/* no specific driver, so return that we didn't write anything */
	return (0);
}


static void serial_put_char (struct tty_struct *tty, unsigned char ch)
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 
	
	dbg("serial_put_char");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return;
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->present) {
		dbg("no device registered");
		return;
	}
	if (!serial->active) {
		dbg ("device not open");
		return;
	}

	/* pass on to the driver specific version of this function */
	if (serial->type->put_char) {
		serial->type->put_char(tty, ch);
	}

	return;
}	


static int serial_write_room (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	dbg("serial_write_room");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return (-ENODEV);
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return (-ENODEV);
	}
	if (!serial->present) {
		dbg("no device registered");
		return (-EINVAL);
	}
	if (!serial->active) {
		dbg ("device not open");
		return (-EINVAL);
	}
	
	/* pass on to the driver specific version of this function */
	if (serial->type->write_room) {
		return (serial->type->write_room(tty));
	}

	return (0);
}


static int serial_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	dbg("serial_chars_in_buffer");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return (-ENODEV);
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return (-ENODEV);
	}
	if (!serial->present) {
		dbg("no device registered");
		return (-EINVAL);
	}
	if (!serial->active) {
		dbg ("device not open");
		return (-EINVAL);
	}
	
	/* pass on to the driver specific version of this function */
	if (serial->type->chars_in_buffer) {
		return (serial->type->chars_in_buffer(tty));
	}

	return (0);
}


static void serial_throttle (struct tty_struct * tty)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	dbg("serial_throttle");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return;
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->present) {
		dbg("no device registered");
		return;
	}
	if (!serial->active) {
		dbg ("device not open");
		return;
	}

	/* pass on to the driver specific version of this function */
	if (serial->type->throttle) {
		serial->type->throttle(tty);
	}

	return;
}


static void serial_unthrottle (struct tty_struct * tty)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	dbg("serial_unthrottle");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		dbg("serial == NULL!");
		return;
	}
	if (!serial->type) {
		dbg("serial->type == NULL!");
		return;
	}
	if (!serial->present) {
		dbg("no device registered");
		return;
	}
	if (!serial->active) {
		dbg ("device not open");
		return;
	}


	/* pass on to the driver specific version of this function */
	if (serial->type->unthrottle) {
		serial->type->unthrottle(tty);
	}

	return;
}


#if defined(CONFIG_USB_SERIAL_BELKIN) || defined(CONFIG_USB_SERIAL_PERACOM)
/*****************************************************************************
 * eTek specific driver functions
 *****************************************************************************/
static int etek_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	dbg("etek_serial_open");

	if (!serial->present) {
		dbg("no device registered");
		return -EINVAL;
	}

	if (serial->active) {
		dbg ("device already open");
		return -EINVAL;
	}
	serial->active = 1;
 
	/*Start reading from the device*/
	if (usb_submit_urb(&serial->read_urb))
		dbg("usb_submit_urb(read bulk) failed");

	/* Need to do device specific setup here (control lines, baud rate, etc.) */
	/* FIXME!!! */

	return (0);
}


static void etek_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	dbg("etek_serial_close");
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb);
	usb_unlink_urb (&serial->read_urb);
	serial->active = 0;
}
#endif	/* defined(CONFIG_USB_SERIAL_BELKIN) || defined(CONFIG_USB_SERIAL_PERACOM) */



#ifdef CONFIG_USB_SERIAL_WHITEHEAT
/*****************************************************************************
 * Connect Tech's White Heat specific driver functions
 *****************************************************************************/
static int whiteheat_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	dbg("whiteheat_serial_open");

	if (!serial->present) {
		dbg("no device registered");
		return -EINVAL;
	}

	if (serial->active) {
		dbg ("device already open");
		return -EINVAL;
	}
	serial->active = 1;
 
	/*Start reading from the device*/
	if (usb_submit_urb(&serial->read_urb))
		dbg("usb_submit_urb(read bulk) failed");

	/* Need to do device specific setup here (control lines, baud rate, etc.) */
	/* FIXME!!! */

	return (0);
}


static void whiteheat_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	dbg("whiteheat_serial_close");
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb);
	usb_unlink_urb (&serial->read_urb);
	serial->active = 0;
}


static void whiteheat_throttle (struct tty_struct * tty)
{
	dbg("whiteheat_throttle");

	/* Change the control signals */
	/* FIXME!!! */

	return;
}


static void whiteheat_unthrottle (struct tty_struct * tty)
{
	dbg("whiteheat_unthrottle");

	/* Change the control signals */
	/* FIXME!!! */

	return;
}


static int whiteheat_writememory (struct usb_serial_state *serial, int address, unsigned char *data, int length, __u8 bRequest)
{
	int result;
	unsigned char *transfer_buffer =  kmalloc (length, GFP_KERNEL);
	if (!transfer_buffer) {
		err("whiteheat_writememory: kmalloc(%d) failed.\n", length);
		return -ENOMEM;
	}
	memcpy (transfer_buffer, data, length);
	result = usb_control_msg (serial->dev, usb_sndctrlpipe(serial->dev, 0), bRequest, 0x40, address, 0, transfer_buffer, length, 300);
	kfree (transfer_buffer);
	return result;
}

/* EZ-USB Control and Status Register.  Bit 0 controls 8051 reset */
#define CPUCS_REG    0x7F92

static int whiteheat_set_reset (struct usb_serial_state *serial, unsigned char reset_bit)
{
	dbg("whiteheat_set_reset: %d", reset_bit);
	return (whiteheat_writememory (serial, CPUCS_REG, &reset_bit, 1, 0xa0));
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
static int  whiteheat_startup (struct usb_serial_state *serial)
{
	int response;
	const struct whiteheat_hex_record *record;
	
	dbg("whiteheat_startup\n");
	
	response = whiteheat_set_reset (serial, 1);

	record = &whiteheat_loader[0];
	while (record->address != 0xffff) {
		response = whiteheat_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("whiteheat_writememory failed for loader (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = whiteheat_set_reset (serial, 0);

	record = &whiteheat_firmware[0];
	while (record->address < 0x8000) {
		++record;
	}
	while (record->address != 0xffff) {
		response = whiteheat_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa3);
		if (response < 0) {
			err("whiteheat_writememory failed for first firmware step (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}
	
	response = whiteheat_set_reset (serial, 1);

	record = &whiteheat_firmware[0];
	while (record->address < 0x8000) {
		response = whiteheat_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("whiteheat_writememory failed for first firmware step (%d %04X %p %d)\n", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = whiteheat_set_reset (serial, 0);

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
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data;
	dbg("visor_serial_open");

	if (!serial->present) {
		dbg("no device registered");
		return -EINVAL;
	}

	if (serial->active) {
		dbg ("device already open");
		return -EINVAL;
	}

	serial->active = 1;

	/*Start reading from the device*/
	if (usb_submit_urb(&serial->read_urb))
		dbg("usb_submit_urb(read bulk) failed");

	return (0);
}

static void visor_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data;
	
	dbg("USB: visor_serial_close");
			 
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb);
	usb_unlink_urb (&serial->read_urb);
	serial->active = 0;
}


static void visor_throttle (struct tty_struct * tty)
{
/*	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; */

	dbg("visor_throttle");

	/* Change the control signals */
	/* FIXME!!! */

	return;
}

static void visor_unthrottle (struct tty_struct * tty)
{
/*	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; */

	dbg("visor_unthrottle");

	/* Change the control signals */
	/* FIXME!!! */

	return;
}
#endif	/* CONFIG_USB_SERIAL_VISOR*/


/*****************************************************************************
 * generic devices specific driver functions
 *****************************************************************************/
static int generic_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	dbg("generic_serial_open");

	if (!serial->present) {
		dbg("no device registered");
		return -EINVAL;
	}

	if (serial->active) {
		dbg ("device already open");
		return -EINVAL;
	}
	serial->active = 1;
 
	/* if we have a bulk interrupt, start reading from it */
	if (serial->num_bulk_in) {
		/*Start reading from the device*/
		if (usb_submit_urb(&serial->read_urb))
			dbg("usb_submit_urb(read bulk) failed");
	}

	return (0);
}


static void generic_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	dbg("generic_serial_close");
	
	/* shutdown any bulk reads that might be going on */
	if (serial->num_bulk_out) {
		usb_unlink_urb (&serial->write_urb);
	}
	if (serial->num_bulk_in) {
		usb_unlink_urb (&serial->read_urb);
	}

	serial->active = 0;
}


static int generic_serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	
	dbg("generic_serial_write");

	if (count == 0) {
		dbg("write request of 0 bytes");
		return (0);
	}

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		if (serial->write_urb.status == -EINPROGRESS) {
			dbg ("already writing");
			return (0);
		}

		count = (count > serial->bulk_out_size[0]) ? serial->bulk_out_size[0] : count;

		if (from_user) {
			copy_from_user(serial->write_urb.transfer_buffer, buf, count);
		}
		else {
			memcpy (serial->write_urb.transfer_buffer, buf, count);
		}  

		/* send the data out the bulk port */
		serial->write_urb.transfer_buffer_length = count;

		if (usb_submit_urb(&serial->write_urb))
			dbg("usb_submit_urb(write bulk) failed");

		return (count);
	}
	
	/* no bulk out, so return 0 bytes written */
	return (0);
} 


static void generic_serial_put_char (struct tty_struct *tty, unsigned char ch)
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 
	
	dbg("generic_serial_put_char");
	
	/* if we have a bulk out endpoint, then shove a character out it */
	if (serial->num_bulk_out) {
		/* send the single character out the bulk port */
		memcpy (serial->write_urb.transfer_buffer, &ch, 1);
		serial->write_urb.transfer_buffer_length = 1;

		if (usb_submit_urb(&serial->write_urb))
			dbg("usb_submit_urb(write bulk) failed");

	}

	return;
}


static int generic_write_room (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 
	int room;

	dbg("generic_write_room");
	
	if (serial->num_bulk_out) {
		if (serial->write_urb.status == -EINPROGRESS)
			room = 0;
		else
			room = serial->bulk_out_size[0];
		dbg("generic_write_room returns %d", room);
		return (room);
	}
	
	return (0);
}


static int generic_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	dbg("generic_chars_in_buffer");
	
	if (serial->num_bulk_out) {
		if (serial->write_urb.status == -EINPROGRESS) {
			return (serial->bulk_out_size[0]);
		}
	}

	return (0);
}


static int Get_Free_Serial (void)
{
	int i;
 
	for (i=0; i < NUM_PORTS; ++i) {
		if (!serial_state_table[i].present)
			return (i);
	}
	return (-1);
}


static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_serial_state *serial = NULL;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[MAX_ENDPOINTS];
	struct usb_endpoint_descriptor *bulk_in_endpoint[MAX_ENDPOINTS];
	struct usb_endpoint_descriptor *bulk_out_endpoint[MAX_ENDPOINTS];
	struct usb_serial_device_type *type;
	int device_num;
	int serial_num;
	int i;
	char interrupt_pipe;
	char bulk_in_pipe;
	char bulk_out_pipe;
	int num_interrupt_in = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	
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

				if (0>(serial_num = Get_Free_Serial())) {
					dbg("Too many devices connected");
					return NULL;
				}
	
				serial = &serial_state_table[serial_num];

			       	memset(serial, 0, sizeof(struct usb_serial_state));
			       	serial->dev = dev;
				serial->type = type;
				serial->number = serial_num;
				serial->num_bulk_in = num_bulk_in;
				serial->num_bulk_out = num_bulk_out;
				serial->num_interrupt_in = num_interrupt_in;

				/* if this device type has a startup function, call it */
				if (type->startup) {
					if (type->startup (serial))
						return NULL;
				}

				/* set up the endpoint information */
				for (i = 0; i < num_bulk_in; ++i) {
					serial->bulk_in_endpoint[i] = bulk_in_endpoint[i]->bEndpointAddress;
					serial->bulk_in_size[i] = bulk_in_endpoint[i]->wMaxPacketSize;
					serial->bulk_in_interval[i] = bulk_in_endpoint[i]->bInterval;
					serial->bulk_in_pipe[i] = usb_rcvbulkpipe (dev, serial->bulk_in_endpoint[i]);
					serial->bulk_in_buffer[i] = kmalloc (serial->bulk_in_size[i], GFP_KERNEL);
					if (!serial->bulk_in_buffer[i]) {
						err("Couldn't allocate bulk_in_buffer");
						goto probe_error;
					}
				}
				if (num_bulk_in)
					FILL_BULK_URB(&serial->read_urb, dev, usb_rcvbulkpipe (dev, serial->bulk_in_endpoint[0]),
							serial->bulk_in_buffer[0], serial->bulk_in_size[0], serial_read_bulk, serial);

				for (i = 0; i < num_bulk_out; ++i) {
					serial->bulk_out_endpoint[i] = bulk_out_endpoint[i]->bEndpointAddress;
					serial->bulk_out_size[i] = bulk_out_endpoint[i]->wMaxPacketSize;
					serial->bulk_out_interval[i] = bulk_out_endpoint[i]->bInterval;
					serial->bulk_out_pipe[i] = usb_rcvbulkpipe (dev, serial->bulk_out_endpoint[i]);
					serial->bulk_out_buffer[i] = kmalloc (serial->bulk_out_size[i], GFP_KERNEL);
					if (!serial->bulk_out_buffer[i]) {
						err("Couldn't allocate bulk_out_buffer");
						goto probe_error;
					}
				}
				if (num_bulk_out)
					FILL_BULK_URB(&serial->write_urb, dev, usb_sndbulkpipe (dev, serial->bulk_in_endpoint[0]),
							serial->bulk_in_buffer[0], serial->bulk_in_size[0], serial_write_bulk, serial);

				for (i = 0; i < num_interrupt_in; ++i) {
					serial->interrupt_in_inuse = 0;
					serial->interrupt_in_endpoint[i] = interrupt_in_endpoint[i]->bEndpointAddress;
					serial->interrupt_in_size[i] = interrupt_in_endpoint[i]->wMaxPacketSize;
					serial->interrupt_in_interval[i] = interrupt_in_endpoint[i]->bInterval;
					/* serial->interrupt_in_pipe = usb_rcvbulkpipe (dev, serial->bulk_in_endpoint); */
					serial->interrupt_in_buffer[i] = kmalloc (serial->bulk_in_size[i], GFP_KERNEL);
					if (!serial->interrupt_in_buffer[i]) {
						err("Couldn't allocate interrupt_in_buffer");
						goto probe_error;
					}
				}

				#if 0
				/* set up an interrupt for out bulk in pipe */
				/* ask for a bulk read */
				serial->bulk_in_inuse = 1;
				serial->bulk_in_transfer = usb_request_bulk (serial->dev, serial->bulk_in_pipe, serial_read_irq, serial->bulk_in_buffer, serial->bulk_in_size, serial);

				/* set up our interrupt to be the time for the bulk in read */
				ret = usb_request_irq (dev, serial->bulk_in_pipe, usb_serial_irq, serial->bulk_in_interval, serial, &serial->irq_handle);
				if (ret) {
					info("failed usb_request_irq (0x%x)", ret);
					goto probe_error;
				}
				#endif

				serial->present = 1;
				MOD_INC_USE_COUNT;

				info("%s converter now attached to ttyUSB%d", type->name, serial_num);
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
	struct usb_serial_state *serial = (struct usb_serial_state *) ptr;
	int i;

	if (serial) {
		if (!serial->present) {
			/* something strange is going on */
			dbg("disconnect but not present?");
			return;
			}

		/* need to stop any transfers...*/
		usb_unlink_urb (&serial->write_urb);
		usb_unlink_urb (&serial->read_urb);

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

		serial->present = 0;
		serial->active = 0;

		info("%s converter now disconnected from ttyUSB%d", serial->type->name, serial->number);

	} else {
		info("device disconnected");
	}
	
	MOD_DEC_USE_COUNT;

}


static struct tty_driver serial_tty_driver = {
	magic:			TTY_DRIVER_MAGIC,
	driver_name:		"usb",
	name:			"ttyUSB",
	major:			SERIAL_MAJOR,
	minor_start:		0,
	num:			NUM_PORTS,
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
	put_char:		serial_put_char,
	flush_chars:		NULL,
	write_room:		serial_write_room,
	ioctl:			NULL,
	set_termios:		NULL,
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
	for (i = 0; i < NUM_PORTS; ++i) {
		memset(&serial_state_table[i], 0x00, sizeof(struct usb_serial_state));
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


#ifdef MODULE
int init_module(void)
{
	return usb_serial_init();
}

void cleanup_module(void)
{
	tty_unregister_driver(&serial_tty_driver);
	usb_deregister(&usb_serial_driver);
}

#endif

