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


static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_serial_disconnect(struct usb_device *dev, void *ptr);


/* USB Serial devices vendor ids and device ids that this driver supports */
#define BELKIN_VENDOR_ID		0x056c
#define BELKIN_SERIAL_CONVERTER_ID	0x8007
#define PERACOM_VENDOR_ID		0x0565
#define PERACOM_SERIAL_CONVERTER_ID	0x0001
#define CONNECT_TECH_VENDOR_ID		0x0710
#define CONNECT_TECH_FAKE_WHITE_HEAT_ID	0x0001
#define CONNECT_TECH_WHITE_HEAT_ID	0x8001
#define HANDSPRING_VENDOR_ID		0x082d
#define HANDSPRING_VISOR_ID		0x0100
#define FTDI_VENDOR_ID			0x0403
#define FTDI_SERIAL_CONVERTER_ID	0x8372

#define SERIAL_TTY_MAJOR	188	/* Nice legal number now */
#define SERIAL_TTY_MINORS	16	/* Actually we are allowed 255, but this is good for now */


#define MAX_NUM_PORTS	8	/* The maximum number of ports one device can grab at once */

struct usb_serial {
	struct usb_device *		dev;
	struct usb_serial_device_type *	type;
	void *				irq_handle;
	unsigned int			irqpipe;
	struct tty_struct *		tty;			/* the coresponding tty for this device */
	unsigned char			minor;
	unsigned char			num_ports;		/* the number of ports this device has */
	char				active[MAX_NUM_PORTS];	/* someone has this device open */

	char			num_interrupt_in;		/* number of interrupt in endpoints we have */
	__u8			interrupt_in_interval[MAX_NUM_PORTS];
	unsigned char *		interrupt_in_buffer[MAX_NUM_PORTS];
	struct urb		control_urb[MAX_NUM_PORTS];

	char			num_bulk_in;			/* number of bulk in endpoints we have */
	unsigned char *		bulk_in_buffer[MAX_NUM_PORTS];
	struct urb		read_urb[MAX_NUM_PORTS];

	char			num_bulk_out;			/* number of bulk out endpoints we have */
	unsigned char *		bulk_out_buffer[MAX_NUM_PORTS];
	int			bulk_out_size[MAX_NUM_PORTS];
	struct urb		write_urb[MAX_NUM_PORTS];
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
	char	num_ports;		/* number of serial ports this device has */

	/* function call to make before accepting driver */
	int (*startup) (struct usb_serial *serial);	/* return 0 to continue initialization, anything else to abort */
	
	/* serial function calls */
	int  (*open)(struct tty_struct * tty, struct file * filp);
	void (*close)(struct tty_struct * tty, struct file * filp);
	int  (*write)(struct tty_struct * tty, int from_user,const unsigned char *buf, int count);
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
	num_ports:		1,
	open:			generic_serial_open,
	close:			generic_serial_close,
	write:			generic_serial_write,
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
static __u16	belkin_product_id	= BELKIN_SERIAL_CONVERTER_ID;
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
	num_ports:		1,
	open:			etek_serial_open,
	close:			etek_serial_close,
	write:			generic_serial_write,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
};
#endif


#ifdef CONFIG_USB_SERIAL_PERACOM
/* All of the device info needed for the Peracom Serial Converter */
static __u16	peracom_vendor_id	= PERACOM_VENDOR_ID;
static __u16	peracom_product_id	= PERACOM_SERIAL_CONVERTER_ID;
static struct usb_serial_device_type peracom_device = {
	name:			"Peracom",
	idVendor:		&peracom_vendor_id,	/* the Peracom vendor id */
	idProduct:		&peracom_product_id,	/* the Peracom serial converter product id */
	needs_interrupt_in:	MUST_HAVE,		/* this device must have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_ports:		1,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	open:			etek_serial_open,
	close:			etek_serial_close,
	write:			generic_serial_write,
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
static int  whiteheat_startup		(struct usb_serial *serial);

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
	num_ports:		4,
	open:			whiteheat_serial_open,
	close:			whiteheat_serial_close,
	write:			generic_serial_write,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
	throttle:		whiteheat_throttle,
	unthrottle:		whiteheat_unthrottle
};
#endif


#ifdef CONFIG_USB_SERIAL_VISOR

/****************************************************************************
 * Handspring Visor Vendor specific request codes (bRequest values)
 * A big thank you to Handspring for providing the following information.
 * If anyone wants the original file where these values and structures came
 * from, send email to <greg@kroah.com>.
 ****************************************************************************/

/****************************************************************************
 * VISOR_REQUEST_BYTES_AVAILABLE asks the visor for the number of bytes that
 * are available to be transfered to the host for the specified endpoint.
 * Currently this is not used, and always returns 0x0001
 ****************************************************************************/
#define VISOR_REQUEST_BYTES_AVAILABLE		0x01

/****************************************************************************
 * VISOR_CLOSE_NOTIFICATION is set to the device to notify it that the host
 * is now closing the pipe. An empty packet is sent in response.
 ****************************************************************************/
#define VISOR_CLOSE_NOTIFICATION		0x02

/****************************************************************************
 * VISOR_GET_CONNECTION_INFORMATION is sent by the host during enumeration to
 * get the endpoints used by the connection.
 ****************************************************************************/
#define VISOR_GET_CONNECTION_INFORMATION	0x03


/****************************************************************************
 * VISOR_GET_CONNECTION_INFORMATION returns data in the following format
 ****************************************************************************/
struct visor_connection_info {
	__u16	num_ports;
	struct {
		__u8	port_function_id;
		__u8	port;
	} connections[2];
};


/* struct visor_connection_info.connection[x].port defines: */
#define VISOR_ENDPOINT_1		0x01
#define VISOR_ENDPOINT_2		0x02

/* struct visor_connection_info.connection[x].port_function_id defines: */
#define VISOR_FUNCTION_GENERIC		0x00
#define VISOR_FUNCTION_DEBUGGER		0x01
#define VISOR_FUNCTION_HOTSYNC		0x02
#define VISOR_FUNCTION_CONSOLE		0x03
#define VISOR_FUNCTION_REMOTE_FILE_SYS	0x04


/* function prototypes for a handspring visor */
static int  visor_serial_open		(struct tty_struct *tty, struct file *filp);
static void visor_serial_close		(struct tty_struct *tty, struct file *filp);
static void visor_throttle		(struct tty_struct *tty);
static void visor_unthrottle		(struct tty_struct *tty);
static int  visor_startup		(struct usb_serial *serial);

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
	num_ports:		2,
	open:			visor_serial_open,
	close:			visor_serial_close,
	write:			generic_serial_write,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer,
	throttle:		visor_throttle,
	unthrottle:		visor_unthrottle,
	startup:		visor_startup
};
#endif


#ifdef CONFIG_USB_SERIAL_FTDI
/* function prototypes for a FTDI serial converter */
static int  ftdi_serial_open	(struct tty_struct *tty, struct file *filp);
static void ftdi_serial_close	(struct tty_struct *tty, struct file *filp);

/* All of the device info needed for the Handspring Visor */
static __u16	ftdi_vendor_id	= FTDI_VENDOR_ID;
static __u16	ftdi_product_id	= FTDI_SERIAL_CONVERTER_ID;
static struct usb_serial_device_type ftdi_device = {
	name:			"FTDI",
	idVendor:		&ftdi_vendor_id,	/* the FTDI vendor ID */
	idProduct:		&ftdi_product_id,	/* the FTDI product id */
	needs_interrupt_in:	MUST_HAVE_NOT,		/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			ftdi_serial_open,
	close:			ftdi_serial_close,
	write:			generic_serial_write,
	write_room:		generic_write_room,
	chars_in_buffer:	generic_chars_in_buffer
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
#ifdef CONFIG_USB_SERIAL_FTDI
	&ftdi_device,
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
static struct tty_struct *	serial_tty[SERIAL_TTY_MINORS];
static struct termios *		serial_termios[SERIAL_TTY_MINORS];
static struct termios *		serial_termios_locked[SERIAL_TTY_MINORS];
static struct usb_serial	*serial_table[SERIAL_TTY_MINORS] = {NULL, };



#define SERIAL_PTR_EMPTY ((void *)(-1))

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
		for (j = 0; j < num_ports-1; ++j)
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


static void serial_read_bulk (struct urb *urb)
{
	struct usb_serial *serial = (struct usb_serial *)urb->context;
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
	struct usb_serial *serial = (struct usb_serial *) urb->context; 
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
	 
	/* pass on to the driver specific version of this function */
	if (serial->type->open) {
		return (serial->type->open(tty, filp));
	}
		
	return (0);
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

	/* pass on to the driver specific version of this function */
	if (serial->type->close) {
		serial->type->close(tty, filp);
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
	
	/* pass on to the driver specific version of this function */
	if (serial->type->write) {
		return (serial->type->write(tty, from_user, buf, count));
	}

	/* no specific driver, so return that we didn't write anything */
	return (0);
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
	
	/* pass on to the driver specific version of this function */
	if (serial->type->write_room) {
		return (serial->type->write_room(tty));
	}

	return (0);
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
	
	/* pass on to the driver specific version of this function */
	if (serial->type->chars_in_buffer) {
		return (serial->type->chars_in_buffer(tty));
	}

	return (0);
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
	}

	return;
}


#if defined(CONFIG_USB_SERIAL_BELKIN) || defined(CONFIG_USB_SERIAL_PERACOM)
/*****************************************************************************
 * eTek specific driver functions
 *****************************************************************************/
static int etek_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("etek_serial_open port %d", port);

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


static void etek_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("etek_serial_close port %d", port);
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb[port]);
	usb_unlink_urb (&serial->read_urb[port]);
	serial->active[port] = 0;
}
#endif	/* defined(CONFIG_USB_SERIAL_BELKIN) || defined(CONFIG_USB_SERIAL_PERACOM) */



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


static int whiteheat_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest)
{
	int result;
	unsigned char *transfer_buffer =  kmalloc (length, GFP_KERNEL);

//	dbg("whiteheat_writememory %x, %d", address, length);

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

static int whiteheat_set_reset (struct usb_serial *serial, unsigned char reset_bit)
{
	int	response;
	dbg("whiteheat_set_reset: %d", reset_bit);
	response = whiteheat_writememory (serial, CPUCS_REG, &reset_bit, 1, 0xa0);
	if (response < 0) {
		err("whiteheat_set_reset %d failed", reset_bit);
	}
	return (response);
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
	while (record->address < 0x1b40) {
		++record;
	}
	while (record->address != 0xffff) {
		response = whiteheat_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("whiteheat_writememory failed for first firmware step (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}
	
	response = whiteheat_set_reset (serial, 1);

	record = &whiteheat_firmware[0];
	while (record->address < 0x1b40) {
		response = whiteheat_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err("whiteheat_writememory failed for second firmware step (%d %04X %p %d)\n", 
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
		err("visor_serial_close: kmalloc(%d) failed.\n", 0x12);
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
		err("visor_startup: kmalloc(%d) failed.\n", 256);
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
#ifdef DEBUG
		struct visor_connection_info *connection_info = (struct visor_connection_info *)transfer_buffer;
		char *string;
		dbg("%s: Number of ports: %d", serial->type->name, connection_info->num_ports);
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
				case VISOR_FUNCTION_REMOTE_FILE_SYS:
					string = "Remote File System";
					break;
				default:
					string = "unknown";
					break;	
			}
			dbg("%s: port %d, is for %s", serial->type->name, connection_info->connections[i].port, string);
		}
#endif
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


#ifdef CONFIG_USB_SERIAL_FTDI
/******************************************************************************
 * FTDI Serial Converter specific driver functions
 ******************************************************************************/
static int  ftdi_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("ftdi_serial_open port %d", port);

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


static void ftdi_serial_close (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial = (struct usb_serial *) tty->driver_data; 
	int port = MINOR(tty->device) - serial->minor;

	dbg("ftdi_serial_close port %d", port);
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (&serial->write_urb[port]);
	usb_unlink_urb (&serial->read_urb[port]);
	serial->active[port] = 0;
}


#endif


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
					if (type->startup (serial))
						return NULL;
				}

				/* set up the endpoint information */
				for (i = 0; i < num_bulk_in; ++i) {
					buffer_size = bulk_in_endpoint[i]->wMaxPacketSize;
					serial->bulk_in_buffer[i] = kmalloc (buffer_size, GFP_KERNEL);
					if (!serial->bulk_in_buffer[i]) {
						err("Couldn't allocate bulk_in_buffer");
						goto probe_error;
					}
					FILL_BULK_URB(&serial->read_urb[i], dev, usb_rcvbulkpipe (dev, bulk_in_endpoint[i]->bEndpointAddress),
							serial->bulk_in_buffer[i], buffer_size, serial_read_bulk, serial);
				}

				for (i = 0; i < num_bulk_out; ++i) {
					serial->bulk_out_size[i] = bulk_out_endpoint[i]->wMaxPacketSize * 2;
					serial->bulk_out_buffer[i] = kmalloc (serial->bulk_out_size[i], GFP_KERNEL);
					if (!serial->bulk_out_buffer[i]) {
						err("Couldn't allocate bulk_out_buffer");
						goto probe_error;
					}
					FILL_BULK_URB(&serial->write_urb[i], dev, usb_sndbulkpipe (dev, bulk_out_endpoint[i]->bEndpointAddress),
							serial->bulk_out_buffer[i], serial->bulk_out_size[i], serial_write_bulk, serial);
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
			serial_table[serial->minor + i] = NULL;
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

