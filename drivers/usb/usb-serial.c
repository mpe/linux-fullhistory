/*
 * USB Serial Converter driver
 *
 *	(C) Copyright (C) 1999
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
 * See README.serial for more information on using this driver.
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

#include "usb.h"

/*#define SERIAL_DEBUG 1*/

#ifdef SERIAL_DEBUG
	#define debug_info(message); printk(message);
#else
	#define debug_info(message);
#endif


/* Module information */
MODULE_AUTHOR("Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux-usb/");
MODULE_DESCRIPTION("USB Serial Driver");

static __u16	vendor	= 0;
static __u16	product	= 0;
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");


/* USB Serial devices vendor ids and device ids that this driver supports */
#define BELKIN_VENDOR_ID		0x056c
#define BELKIN_SERIAL_CONVERTER		0x8007
#define PERACOM_VENDOR_ID		0x0565
#define PERACOM_SERIAL_CONVERTER	0x0001


#define SERIAL_MAJOR	188	/* Nice legal number now */

#define NUM_PORTS	4	/* Have to pick a number for now. Need to look */
				/* into dynamically creating them at insertion time. */


static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_serial_disconnect(struct usb_device *dev, void *ptr);


#define MUST_HAVE_NOT	0x01
#define MUST_HAVE	0x02
#define DONT_CARE	0x03

#define	HAS		0x02
#define HAS_NOT		0x01


/* local function prototypes */
static int serial_open (struct tty_struct *tty, struct file * filp);
static void serial_close (struct tty_struct *tty, struct file * filp);
static int serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count);
static void serial_put_char (struct tty_struct *tty, unsigned char ch);
static int serial_write_room (struct tty_struct *tty);
static int serial_chars_in_buffer (struct tty_struct *tty);
static void serial_throttle (struct tty_struct * tty);
static void serial_unthrottle (struct tty_struct * tty);


/* function prototypes for the eTek type converters (this included Belkin and Peracom) */
static int etek_serial_open (struct tty_struct *tty, struct file * filp);
static void etek_serial_close (struct tty_struct *tty, struct file * filp);
static int etek_serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count);
static void etek_serial_put_char (struct tty_struct *tty, unsigned char ch);
static int etek_write_room (struct tty_struct *tty);
static int etek_chars_in_buffer (struct tty_struct *tty);
static void etek_throttle (struct tty_struct * tty);
static void etek_unthrottle (struct tty_struct * tty);


/* function prototypes for a "generic" type serial converter (no flow control, not all endpoints needed) */
static int generic_serial_open (struct tty_struct *tty, struct file * filp);
static void generic_serial_close (struct tty_struct *tty, struct file * filp);
static int generic_serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count);
static void generic_serial_put_char (struct tty_struct *tty, unsigned char ch);
static int generic_write_room (struct tty_struct *tty);
static int generic_chars_in_buffer (struct tty_struct *tty);


/* This structure defines the individual serial converter. */
struct usb_serial_device_type {
	char	*name;
	__u16	*idVendor;
	__u16	*idProduct;
	char	needs_interrupt_in;
	char	needs_bulk_in;
	char	needs_bulk_out;
	// add function calls

	int  (*open)(struct tty_struct * tty, struct file * filp);
	void (*close)(struct tty_struct * tty, struct file * filp);
	int  (*write)(struct tty_struct * tty, int from_user,const unsigned char *buf, int count);
	void (*put_char)(struct tty_struct *tty, unsigned char ch);
	int  (*write_room)(struct tty_struct *tty);
	int  (*chars_in_buffer)(struct tty_struct *tty);
	void (*throttle)(struct tty_struct * tty);
	void (*unthrottle)(struct tty_struct * tty);

};


/* All of the device info needed for the Belkin Serial Converter */
static __u16	belkin_vendor_id	= BELKIN_VENDOR_ID;
static __u16	belkin_product_id	= BELKIN_SERIAL_CONVERTER;
static struct usb_serial_device_type belkin_device = {
	"Belkin",
	&belkin_vendor_id,	/* the Belkin vendor id */
	&belkin_product_id,	/* the Belkin serial converter product id */
	MUST_HAVE,		/* this device must have an interrupt in endpoint */
	MUST_HAVE,		/* this device must have a bulk in endpoint */
	MUST_HAVE,		/* this device must have a bulk out endpoint */
	etek_serial_open,
	etek_serial_close,
	etek_serial_write,
	etek_serial_put_char,
	etek_write_room,
	etek_chars_in_buffer,
	etek_throttle,
	etek_unthrottle
};

/* All of the device info needed for the Peracom Serial Converter */
static __u16	peracom_vendor_id	= PERACOM_VENDOR_ID;
static __u16	peracom_product_id	= PERACOM_SERIAL_CONVERTER;
static struct usb_serial_device_type peracom_device = {
	"Peracom",
	&peracom_vendor_id,	/* the Peracom vendor id */
	&peracom_product_id,	/* the Peracom serial converter product id */
	MUST_HAVE,		/* this device must have an interrupt in endpoint */
	MUST_HAVE,		/* this device must have a bulk in endpoint */
	MUST_HAVE,		/* this device must have a bulk out endpoint */
	etek_serial_open,
	etek_serial_close,
	etek_serial_write,
	etek_serial_put_char,
	etek_write_room,
	etek_chars_in_buffer,
	etek_throttle,
	etek_unthrottle
};

/* All of the device info needed for the Generic Serial Converter */
static struct usb_serial_device_type generic_device = {
	"Generic",
	&vendor,		/* use the user specified vendor id */
	&product,		/* use the user specified product id */
	DONT_CARE,		/* don't have to have an interrupt in endpoint */
	DONT_CARE,		/* don't have to have a bulk in endpoint */
	DONT_CARE,		/* don't have to have a bulk out endpoint */
	generic_serial_open,
	generic_serial_close,
	generic_serial_write,
	generic_serial_put_char,
	generic_write_room,
	generic_chars_in_buffer,
	NULL,			/* generic driver does not implement any flow control */
	NULL			/* generic driver does not implement any flow control */
};


/* To add support for another serial converter, create a usb_serial_device_type
   structure for that device, and add it to this list, making sure that the last
   entry is NULL. */
static struct usb_serial_device_type *usb_serial_devices[] = {
	&generic_device,
	&belkin_device,
	&peracom_device,
	NULL
};



struct usb_serial_state {
	struct usb_device *		dev;
	struct usb_serial_device_type *	type;
	void *				irq_handle;
	unsigned int			irqpipe;
	struct tty_struct *		tty;		/* the coresponding tty for this device */
	char				present;
	char				active;

	char			has_interrupt_in;	/* if this device has an interrupt in pipe or not */
	char			interrupt_in_inuse;	/* if the interrupt in endpoint is in use */
	__u8			interrupt_in_endpoint;
	__u8			interrupt_in_interval;
	__u16			interrupt_in_size;	/* the size of the interrupt in endpoint */
	unsigned int		interrupt_in_pipe;
	unsigned char *		interrupt_in_buffer;
	void *			interrupt_in_transfer;

	char			has_bulk_in;		/* if thie device has a bulk in pipe or not */
	char			bulk_in_inuse;		/* if the bulk in endpoint is in use */
	__u8			bulk_in_endpoint;
	__u8			bulk_in_interval;
	__u16			bulk_in_size;		/* the size of the bulk in endpoint */
	unsigned int		bulk_in_pipe;
	unsigned char *		bulk_in_buffer;
	void *			bulk_in_transfer;

	char			has_bulk_out;		/* if this device has a bulk out pipe or not */
	char			bulk_out_inuse;		/* if the bulk out endpoint is in use */
	__u8			bulk_out_endpoint;
	__u8			bulk_out_interval;
	__u16			bulk_out_size;		/* the size of the bulk out endpoint */
	unsigned int		bulk_out_pipe;
	unsigned char *		bulk_out_buffer;
	void *			bulk_out_transfer;
};

static struct usb_driver usb_serial_driver = {
	"serial",
	usb_serial_probe,
	usb_serial_disconnect,
	{ NULL, NULL }
};

static int			serial_refcount;
static struct tty_driver 	serial_tty_driver;
static struct tty_struct *	serial_tty[NUM_PORTS];
static struct termios *		serial_termios[NUM_PORTS];
static struct termios *		serial_termios_locked[NUM_PORTS];
static struct usb_serial_state	serial_state_table[NUM_PORTS];



static int serial_read_irq (int state, void *buffer, int count, void *dev_id)
{
	struct usb_serial_state *serial = (struct usb_serial_state *)dev_id;
       	struct tty_struct *tty = serial->tty; 
       	unsigned char* data = buffer;
	int i;

	debug_info("USB serial: serial_read_irq\n");

#ifdef SERIAL_DEBUG
	if (count) {
		printk("%d %s\n", count, data);
	}
#endif

	if (count) {
		for (i=0;i<count;i++) {
			 tty_insert_flip_char(tty,data[i],0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue transfer */
	/* return (1); */

	/* No more transfer, let the irq schedule us again */
	serial->bulk_in_inuse = 0;
	return (0);
}


static int serial_write_irq (int state, void *buffer, int count, void *dev_id)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) dev_id; 
       	struct tty_struct *tty = serial->tty; 

	debug_info("USB Serial: serial_write_irq\n");

	if (!serial->bulk_out_inuse) {
		debug_info("USB Serial: write irq for a finished pipe?\n");
		return (0);
		}

	usb_terminate_bulk (serial->dev, serial->bulk_out_transfer);
	serial->bulk_out_inuse = 0;

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);
	
	return 0;
}


static int usb_serial_irq (int state, void *buffer, int len, void *dev_id)
{
//	struct usb_serial_state *serial = (struct usb_serial_state *) dev_id;

	debug_info("USB Serial: usb_serial_irq\n");

	/* ask for a bulk read */
//	serial->bulk_in_inuse = 1;
//	serial->bulk_in_transfer = usb_request_bulk (serial->dev, serial->bulk_in_pipe, serial_read_irq, serial->bulk_in_buffer, serial->bulk_in_size, serial);

	return (1);
}




/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int serial_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial;
	
	debug_info("USB Serial: serial_open\n");

	/* assign a serial object to the tty pointer */
	serial = &serial_state_table [MINOR(tty->device)-tty->driver.minor_start];

	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return (-ENODEV);
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
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
	debug_info("USB Serial: serial_close\n");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return;
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
		return;
	}
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}
	if (!serial->active) {
		debug_info ("USB Serial: device already open\n");
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
	
	debug_info("USB Serial: serial_write\n");

	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return (-ENODEV);
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
		return (-ENODEV);
	}
	if (!serial->present) {
		debug_info("USB Serial: device not registered\n");
		return (-EINVAL);
	}
	if (!serial->active) {
		debug_info ("USB Serial: device not opened\n");
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
	
	debug_info("USB Serial: serial_put_char\n");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return;
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
		return;
	}
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}
	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
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

	debug_info("USB Serial: serial_write_room\n");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return (-ENODEV);
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
		return (-ENODEV);
	}
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return (-EINVAL);
	}
	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
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

	debug_info("USB Serial: serial_chars_in_buffer\n");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return (-ENODEV);
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
		return (-ENODEV);
	}
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return (-EINVAL);
	}
	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
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

	debug_info("USB Serial: serial_throttle\n");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return;
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
		return;
	}
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}
	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
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

	debug_info("USB Serial: serial_unthrottle\n");
	
	/* do some sanity checking that we really have a device present */
	if (!serial) {
		debug_info("USB Serial: serial == NULL!\n");
		return;
	}
	if (!serial->type) {
		debug_info("USB Serial: serial->type == NULL!\n");
		return;
	}
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}
	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
		return;
	}


	/* pass on to the driver specific version of this function */
	if (serial->type->unthrottle) {
		serial->type->unthrottle(tty);
	}

	return;
}


/*****************************************************************************
 * eTek specific driver functions
 *****************************************************************************/
static int etek_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	debug_info("USB Serial: etek_serial_open\n");

	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return -EINVAL;
	}

	if (serial->active) {
		debug_info ("USB Serial: device already open\n");
		return -EINVAL;
	}
	serial->active = 1;
 
	/*Start reading from the device*/
	serial->bulk_in_inuse = 1;
	serial->bulk_in_transfer = usb_request_bulk (serial->dev, serial->bulk_in_pipe, serial_read_irq, serial->bulk_in_buffer, serial->bulk_in_size, serial);

	/* Need to do device specific setup here (control lines, baud rate, etc.) */
	/* FIXME!!! */

	return (0);
}


static void etek_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	debug_info("USB Serial: etek_serial_close\n");
	
	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	if (serial->bulk_out_inuse){
		usb_terminate_bulk (serial->dev, serial->bulk_out_transfer);
		serial->bulk_out_inuse = 0;
	}
	if (serial->bulk_in_inuse){
		usb_terminate_bulk (serial->dev, serial->bulk_in_transfer);
		serial->bulk_in_inuse = 0;
	}

	/* release the irq? */
	
	serial->active = 0;
}


static int etek_serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	int written;
	
	debug_info("USB Serial: etek_serial_write\n");

	if (serial->bulk_out_inuse) {
		debug_info ("USB Serial: already writing\n");
		return (0);
	}

	written = (count > serial->bulk_out_size) ? serial->bulk_out_size : count;
	  
	if (from_user) {
		copy_from_user(serial->bulk_out_buffer, buf, written);
	}
	else {
		memcpy (serial->bulk_out_buffer, buf, written);
	}  

	/* send the data out the bulk port */
	serial->bulk_out_inuse = 1;
	serial->bulk_out_transfer = usb_request_bulk (serial->dev, serial->bulk_out_pipe, serial_write_irq, serial->bulk_out_buffer, written, serial);

	return (written);
} 


static void etek_serial_put_char (struct tty_struct *tty, unsigned char ch)
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 
	
	debug_info("USB Serial: etek_serial_put_char\n");
	
	if (serial->bulk_out_inuse) {
		debug_info ("USB Serial: already writing\n");
		return;
	}

	/* send the single character out the bulk port */
	serial->bulk_out_buffer[0] = ch;
	serial->bulk_out_inuse = 1;
	serial->bulk_out_transfer = usb_request_bulk (serial->dev, serial->bulk_out_pipe, serial_write_irq, serial->bulk_out_buffer, 1, serial);

	return;
}


static int etek_write_room (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	debug_info("USB Serial: etek_write_room\n");
	
	if (serial->bulk_out_inuse) {
		return (0);
	}

	return (serial->bulk_out_size);
}


static int etek_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	debug_info("USB Serial: etek_chars_in_buffer\n");
	
	if (serial->bulk_out_inuse) {
		return (serial->bulk_out_size);
	}

	return (0);
}


static void etek_throttle (struct tty_struct * tty)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	debug_info("USB Serial: etek_throttle\n");
	
	/* Change the control signals */
	/* FIXME!!! */

	return;
}


static void etek_unthrottle (struct tty_struct * tty)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	debug_info("USB Serial: etek_unthrottle\n");
	
	/* Change the control signals */
	/* FIXME!!! */

	return;
}


/*****************************************************************************
 * generic devices specific driver functions
 *****************************************************************************/
static int generic_serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	debug_info("USB Serial: generic_serial_open\n");

	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return -EINVAL;
	}

	if (serial->active) {
		debug_info ("USB Serial: device already open\n");
		return -EINVAL;
	}
	serial->active = 1;
 
	/* if we have a bulk interrupt, start reading from it */
	if (serial->has_bulk_in) {
		/*Start reading from the device*/
		serial->bulk_in_inuse = 1;
		serial->bulk_in_transfer = usb_request_bulk (serial->dev, serial->bulk_in_pipe, serial_read_irq, serial->bulk_in_buffer, serial->bulk_in_size, serial);
	}

	return (0);
}


static void generic_serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	debug_info("USB Serial: generic_serial_close\n");
	
	/* shutdown any bulk reads that might be going on */
	if (serial->bulk_out_inuse){
		usb_terminate_bulk (serial->dev, serial->bulk_out_transfer);
		serial->bulk_out_inuse = 0;
	}
	if (serial->bulk_in_inuse){
		usb_terminate_bulk (serial->dev, serial->bulk_in_transfer);
		serial->bulk_in_inuse = 0;
	}

	serial->active = 0;
}


static int generic_serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	int written;
	
	debug_info("USB Serial: generic_serial_write\n");

	/* only do something if we have a bulk out endpoint */
	if (serial->has_bulk_out) {
		if (serial->bulk_out_inuse) {
			debug_info ("USB Serial: already writing\n");
			return (0);
		}

		written = (count > serial->bulk_out_size) ? serial->bulk_out_size : count;
	  
		if (from_user) {
			copy_from_user(serial->bulk_out_buffer, buf, written);
		}
		else {
			memcpy (serial->bulk_out_buffer, buf, written);
		}  

		/* send the data out the bulk port */
		serial->bulk_out_inuse = 1;
		serial->bulk_out_transfer = usb_request_bulk (serial->dev, serial->bulk_out_pipe, serial_write_irq, serial->bulk_out_buffer, written, serial);

		return (written);
	}
	
	/* no bulk out, so return 0 bytes written */
	return (0);
} 


static void generic_serial_put_char (struct tty_struct *tty, unsigned char ch)
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 
	
	debug_info("USB Serial: generic_serial_put_char\n");
	
	/* if we have a bulk out endpoint, then shove a character out it */
	if (serial->has_bulk_out) {
		if (serial->bulk_out_inuse) {
			debug_info ("USB Serial: already writing\n");
			return;
		}

		/* send the single character out the bulk port */
		serial->bulk_out_buffer[0] = ch;
		serial->bulk_out_inuse = 1;
		serial->bulk_out_transfer = usb_request_bulk (serial->dev, serial->bulk_out_pipe, serial_write_irq, serial->bulk_out_buffer, 1, serial);
	}

	return;
}


static int generic_write_room (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	debug_info("USB Serial: generic_write_room\n");
	
	if (serial->has_bulk_out) {
		if (serial->bulk_out_inuse) {
			return (0);
		}
		return (serial->bulk_out_size);
	}
	
	return (0);
}


static int generic_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	debug_info("USB Serial: generic_chars_in_buffer\n");
	
	if (serial->has_bulk_out) {
		if (serial->bulk_out_inuse) {
			return (serial->bulk_out_size);
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
	struct usb_endpoint_descriptor *interrupt_in_endpoint = NULL;
	struct usb_endpoint_descriptor *bulk_in_endpoint = NULL;
	struct usb_endpoint_descriptor *bulk_out_endpoint = NULL;
	struct usb_serial_device_type *type;
	int device_num;
	int serial_num;
	int i;
	char interrupt_pipe;
	char bulk_in_pipe;
	char bulk_out_pipe;
	
	/* loop through our list of known serial converters, and see if this device matches */
	device_num = 0;
	while (usb_serial_devices[device_num] != NULL) {
		type = usb_serial_devices[device_num];
		#ifdef SERIAL_DEBUG
			printk ("USB Serial: Looking at %s\nVendor id=%.4x\nProduct id=%.4x\n", type->name, *(type->idVendor), *(type->idProduct));
		#endif		

		/* look at the device descriptor */
		if ((dev->descriptor.idVendor == *(type->idVendor)) &&
		    (dev->descriptor.idProduct == *(type->idProduct))) {

			debug_info("USB Serial: descriptor matches...looking at the endpoints\n")

			/* descriptor matches, let's try to find the endpoints needed */
			interrupt_pipe = bulk_in_pipe = bulk_out_pipe = HAS_NOT;
			
			/* check out the endpoints */
			interface = &dev->actconfig->interface[ifnum].altsetting[0];
			for (i = 0; i < interface->bNumEndpoints; ++i) {
				endpoint = &interface->endpoint[i];
		
				if ((endpoint->bEndpointAddress & 0x80) &&
				    ((endpoint->bmAttributes & 3) == 0x02)) {
					/* we found a bulk in endpoint */
					debug_info("USB Serial: found bulk in\n");
					if (bulk_in_pipe == HAS) {
						printk("USB Serial: can't have more than one bulk in endpoint\n");
						goto probe_error;
						}
					bulk_in_pipe = HAS;
					bulk_in_endpoint = endpoint;
				}

				if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
				    ((endpoint->bmAttributes & 3) == 0x02)) {
					/* we found a bulk out endpoint */
					debug_info("USB Serial: found bulk out\n");
					if (bulk_out_pipe == HAS) {
						printk("USB Serial: can't have more than one bulk out endpoint\n");
						goto probe_error;
						}
					bulk_out_pipe = HAS;
					bulk_out_endpoint = endpoint;
				}
		
				if ((endpoint->bEndpointAddress & 0x80) &&
				    ((endpoint->bmAttributes & 3) == 0x03)) {
					/* we found a interrupt in endpoint */
					debug_info("USB Serial: found interrupt in\n");
					if (interrupt_pipe == HAS) {
						printk("USB Serial: can't have more than one interrupt in endpoint\n");
						goto probe_error;
						}
					interrupt_pipe = HAS;
					interrupt_in_endpoint = endpoint;
				}

			}
	
			/* verify that we found all of the endpoints that we need */
			if ((interrupt_pipe & type->needs_interrupt_in) &&
			    (bulk_in_pipe & type->needs_bulk_in) &&
			    (bulk_out_pipe & type->needs_bulk_out)) {
				/* found all that we need */
				printk (KERN_INFO "USB serial converter detected.\n");

				if (0>(serial_num = Get_Free_Serial())) {
					debug_info("USB Serial: Too many devices connected\n");
					return NULL;
				}
	
				serial = &serial_state_table[serial_num];

			       	memset(serial, 0, sizeof(struct usb_serial_state));
			       	serial->dev = dev;
				serial->type = type;

				/* set up the endpoint information */
				if (bulk_in_endpoint) {
					serial->has_bulk_in = 1;
					serial->bulk_in_inuse = 0;
					serial->bulk_in_endpoint = bulk_in_endpoint->bEndpointAddress;
					serial->bulk_in_size = bulk_in_endpoint->wMaxPacketSize;
					serial->bulk_in_interval = bulk_in_endpoint->bInterval;
					serial->bulk_in_pipe = usb_rcvbulkpipe (dev, serial->bulk_in_endpoint);
					serial->bulk_in_buffer = kmalloc (serial->bulk_in_size, GFP_KERNEL);
					if (!serial->bulk_in_buffer) {
						printk("USB Serial: Couldn't allocate bulk_in_buffer\n");
						goto probe_error;
					}
				}

				if (bulk_out_endpoint) {
					serial->has_bulk_out = 1;
					serial->bulk_out_inuse = 0;
					serial->bulk_out_endpoint = bulk_out_endpoint->bEndpointAddress;
					serial->bulk_out_size = bulk_out_endpoint->wMaxPacketSize;
					serial->bulk_out_interval = bulk_out_endpoint->bInterval;
					serial->bulk_out_pipe = usb_rcvbulkpipe (dev, serial->bulk_out_endpoint);
					serial->bulk_out_buffer = kmalloc (serial->bulk_out_size, GFP_KERNEL);
					if (!serial->bulk_out_buffer) {
						printk("USB Serial: Couldn't allocate bulk_out_buffer\n");
						goto probe_error;
					}
				}

				if (interrupt_in_endpoint) {
					serial->has_interrupt_in = 1;
					serial->interrupt_in_inuse = 0;
					serial->interrupt_in_endpoint = interrupt_in_endpoint->bEndpointAddress;
					serial->interrupt_in_size = interrupt_in_endpoint->wMaxPacketSize;
					serial->interrupt_in_interval = interrupt_in_endpoint->bInterval;
					/* serial->interrupt_in_pipe = usb_rcvbulkpipe (dev, serial->bulk_in_endpoint); */
					serial->interrupt_in_buffer = kmalloc (serial->bulk_in_size, GFP_KERNEL);
					if (!serial->interrupt_in_buffer) {
						printk("USB Serial: Couldn't allocate interrupt_in_buffer\n");
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
					printk(KERN_INFO "USB Serial: failed usb_request_irq (0x%x)\n", ret);
					goto probe_error;
				}
				#endif

				serial->present = 1;
				MOD_INC_USE_COUNT;

				return serial;
			} else {
				printk(KERN_INFO "USB Serial: descriptors matched, but endpoints did not\n");
			}
		}

		/* look at the next type in our list */
		++device_num;
	}

probe_error:
	if (serial) {
		if (serial->bulk_in_buffer)
			kfree (serial->bulk_in_buffer);
		if (serial->bulk_out_buffer)
			kfree (serial->bulk_out_buffer);
		if (serial->interrupt_in_buffer)
			kfree (serial->interrupt_in_buffer);
	}
	return NULL;
}


static void usb_serial_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) ptr;

	if (serial) {
		if (!serial->present) {
			/* something strange is going on */
			debug_info("USB Serial: disconnect but not present?\n")
			return;
			}

		/* need to stop any transfers...*/
		if (serial->bulk_in_inuse) {
			usb_terminate_bulk (serial->dev, serial->bulk_in_transfer);
			serial->bulk_in_inuse = 0;
		}
		if (serial->bulk_out_inuse) {
			usb_terminate_bulk (serial->dev, serial->bulk_out_transfer);
			serial->bulk_out_inuse = 0;
		}
		// usb_release_irq (serial->dev, serial->irq_handle, serial->bulk_in_pipe);
		if (serial->bulk_in_buffer)
			kfree (serial->bulk_in_buffer);
		if (serial->bulk_out_buffer)
			kfree (serial->bulk_out_buffer);
		if (serial->interrupt_in_buffer)
			kfree (serial->interrupt_in_buffer);

		serial->present = 0;
		serial->active = 0;
	}
	
	MOD_DEC_USE_COUNT;

	printk (KERN_INFO "USB Serial: device disconnected.\n");
}



int usb_serial_init(void)
{
	int i;

	/* Initalize our global data */
	for (i = 0; i < NUM_PORTS; ++i) {
		memset(&serial_state_table[i], 0x00, sizeof(struct usb_serial_state));
	}

	/* register the tty driver */
	memset (&serial_tty_driver, 0, sizeof(struct tty_driver));
	serial_tty_driver.magic			= TTY_DRIVER_MAGIC;
	serial_tty_driver.driver_name		= "usb";
	serial_tty_driver.name			= "ttyUSB";
	serial_tty_driver.major			= SERIAL_MAJOR;
	serial_tty_driver.minor_start		= 0;
	serial_tty_driver.num			= NUM_PORTS;
	serial_tty_driver.type			= TTY_DRIVER_TYPE_SERIAL;
	serial_tty_driver.subtype		= SERIAL_TYPE_NORMAL;
	serial_tty_driver.init_termios		= tty_std_termios;
	serial_tty_driver.init_termios.c_cflag	= B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_tty_driver.flags			= TTY_DRIVER_REAL_RAW;
	serial_tty_driver.refcount		= &serial_refcount;
	serial_tty_driver.table			= serial_tty;
	serial_tty_driver.proc_entry		= NULL;
	serial_tty_driver.other			= NULL;
	serial_tty_driver.termios		= serial_termios;
	serial_tty_driver.termios_locked	= serial_termios_locked;
	
	serial_tty_driver.open			= serial_open;
	serial_tty_driver.close			= serial_close;
	serial_tty_driver.write			= serial_write;
	serial_tty_driver.put_char		= serial_put_char;
	serial_tty_driver.flush_chars		= NULL; //serial_flush_chars;
	serial_tty_driver.write_room		= serial_write_room;
	serial_tty_driver.ioctl			= NULL; //serial_ioctl;
	serial_tty_driver.set_termios		= NULL; //serial_set_termios;
	serial_tty_driver.set_ldisc		= NULL; 
	serial_tty_driver.throttle		= serial_throttle;
	serial_tty_driver.unthrottle		= serial_unthrottle;
	serial_tty_driver.stop			= NULL; //serial_stop;
	serial_tty_driver.start			= NULL; //serial_start;
	serial_tty_driver.hangup		= NULL; //serial_hangup;
	serial_tty_driver.break_ctl		= NULL; //serial_break;
	serial_tty_driver.wait_until_sent	= NULL; //serial_wait_until_sent;
	serial_tty_driver.send_xchar		= NULL; //serial_send_xchar;
	serial_tty_driver.read_proc		= NULL; //serial_read_proc;
	serial_tty_driver.chars_in_buffer	= serial_chars_in_buffer;
	serial_tty_driver.flush_buffer		= NULL; //serial_flush_buffer;
	if (tty_register_driver (&serial_tty_driver)) {
		printk( "USB Serial: failed to register tty driver\n" );
		return -EPERM;
	}
	
	/* register the USB driver */
	if (usb_register(&usb_serial_driver) < 0) {
		tty_unregister_driver(&serial_tty_driver);
		return -1;
	}

	printk(KERN_INFO "USB Serial: support registered.\n");
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

