/*
 * bluetooth.c   Version 0.1
 *
 * Copyright (c) 2000 Greg Kroah-Hartman	<greg@kroah.com>
 *
 * USB Bluetooth driver, based on the Bluetooth Spec version 1.0B
 *
 *
 * (07/09/2000) Version 0.1 gkh
 *	Initial release. Has support for sending ACL data (which is really just
 *	a HCI frame.) Raw HCI commands and HCI events are not supported.
 *	A ioctl will probably be needed for the HCI commands and events in the
 *	future. All isoch endpoints are ignored at this time also.
 *	This driver should work for all currently shipping USB Bluetooth 
 *	devices at this time :)
 * 
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
#include <linux/list.h>
#include <linux/smp_lock.h>

#define DEBUG
#include <linux/usb.h>

/* Module information */
MODULE_AUTHOR("Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux-usb/");
MODULE_DESCRIPTION("USB Bluetooth driver");


/* Class, SubClass, and Protocol codes that describe a Bluetooth device */
#define WIRELESS_CLASS_CODE			0xe0
#define RF_SUBCLASS_CODE			0x01
#define BLUETOOTH_PROGRAMMING_PROTOCOL_CODE	0x01


#define BLUETOOTH_TTY_MAJOR	240	/* Prototype number for now */
#define BLUETOOTH_TTY_MINORS	8

#define USB_BLUETOOTH_MAGIC	0x6d02	/* magic number for bluetooth struct */

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))



struct usb_bluetooth {
	int			magic;
	struct usb_device *	dev;
	struct tty_driver *	tty_driver;	/* the tty_driver for this device */
	struct tty_struct *	tty;		/* the coresponding tty for this port */
	
	unsigned char		minor;		/* the starting minor number for this device */
	char			active;		/* someone has this device open */

	unsigned char *		interrupt_in_buffer;
	struct urb *		interrupt_in_urb;

	unsigned char *		bulk_in_buffer;
	struct urb *		read_urb;

	unsigned char *		bulk_out_buffer;
	int			bulk_out_size;
	struct urb *		write_urb;

	wait_queue_head_t	write_wait;

	struct tq_struct	tqueue;		/* task queue for line discipline waking up */
};



/* local function prototypes */
static int  bluetooth_open		(struct tty_struct *tty, struct file *filp);
static void bluetooth_close		(struct tty_struct *tty, struct file *filp);
static int  bluetooth_write		(struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
static int  bluetooth_write_room	(struct tty_struct *tty);
static int  bluetooth_chars_in_buffer	(struct tty_struct *tty);
static void bluetooth_throttle		(struct tty_struct *tty);
static void bluetooth_unthrottle	(struct tty_struct *tty);
static int  bluetooth_ioctl		(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static void bluetooth_set_termios	(struct tty_struct *tty, struct termios *old);

static void * usb_bluetooth_probe	(struct usb_device *dev, unsigned int ifnum);
static void usb_bluetooth_disconnect	(struct usb_device *dev, void *ptr);

static struct usb_driver usb_bluetooth_driver = {
	name:		"bluetooth",
	probe:		usb_bluetooth_probe,
	disconnect:	usb_bluetooth_disconnect,
};

static int			bluetooth_refcount;
static struct tty_driver	bluetooth_tty_driver;
static struct tty_struct *	bluetooth_tty[BLUETOOTH_TTY_MINORS];
static struct termios *		bluetooth_termios[BLUETOOTH_TTY_MINORS];
static struct termios *		bluetooth_termios_locked[BLUETOOTH_TTY_MINORS];
static struct usb_bluetooth	*bluetooth_table[BLUETOOTH_TTY_MINORS] = {NULL, };


static inline int bluetooth_paranoia_check (struct usb_bluetooth *bluetooth, const char *function)
{
	if (!bluetooth) {
		dbg("%s - bluetooth == NULL", function);
		return -1;
	}
	if (bluetooth->magic != USB_BLUETOOTH_MAGIC) {
		dbg("%s - bad magic number for bluetooth", function);
		return -1;
	}
	
	return 0;
}


static inline struct usb_bluetooth* get_usb_bluetooth (struct usb_bluetooth *bluetooth, const char *function) 
{ 
	if (!bluetooth || 
		bluetooth_paranoia_check (bluetooth, function)) {
		/* then say that we dont have a valid usb_bluetooth thing, which will
		 * end up genrating -ENODEV return values */ 
		return NULL;
	}

	return bluetooth;
}


static inline struct usb_bluetooth *get_bluetooth_by_minor (int minor)
{
	return bluetooth_table[minor];
}



/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int bluetooth_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_bluetooth *bluetooth;
	
	dbg(__FUNCTION__);

	/* initialize the pointer incase something fails */
	tty->driver_data = NULL;

	/* get the bluetooth object associated with this tty pointer */
	bluetooth = get_bluetooth_by_minor (MINOR(tty->device));

	if (bluetooth_paranoia_check (bluetooth, __FUNCTION__)) {
		return -ENODEV;
	}

	if (bluetooth->active) {
		dbg (__FUNCTION__ " - device already open");
		return -EINVAL;
	}
	
	/* set up our structure making the tty driver remember our object, and us it */
	tty->driver_data = bluetooth;
	bluetooth->tty = tty;
	 
	bluetooth->active = 1;
 
	/*Start reading from the device*/
	if (usb_submit_urb(bluetooth->read_urb))
		dbg(__FUNCTION__ " - usb_submit_urb(read bulk) failed");

	return 0;
}


static void bluetooth_close (struct tty_struct *tty, struct file * filp)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg(__FUNCTION__);
	
	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not opened");
		return;
	}

	/* shutdown any bulk reads that might be going on */
	usb_unlink_urb (bluetooth->write_urb);
	usb_unlink_urb (bluetooth->read_urb);

	bluetooth->active = 0;
}


static int bluetooth_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);
	
	if (!bluetooth) {
		return -ENODEV;
	}
	
	dbg(__FUNCTION__ " - %d byte(s)", count);

	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not opened");
		return -EINVAL;
	}
	
	if (count == 0) {
		dbg(__FUNCTION__ " - write request of 0 bytes");
		return (0);
	}

	if (bluetooth->write_urb->status == -EINPROGRESS) {
		dbg (__FUNCTION__ " - already writing");
		return (0);
	}

	count = (count > bluetooth->bulk_out_size) ? bluetooth->bulk_out_size : count;

#ifdef DEBUG
	{
		int i;
		printk (KERN_DEBUG __FILE__ ": " __FUNCTION__ " - length = %d, data = ", count);
		for (i = 0; i < count; ++i) {
			printk ("%.2x ", buf[i]);
		}
		printk ("\n");
	}
#endif

	if (from_user) {
		copy_from_user(bluetooth->write_urb->transfer_buffer, buf, count);
	}
	else {
		memcpy (bluetooth->write_urb->transfer_buffer, buf, count);
	}  

	/* send the data out the bulk bluetooth */
	bluetooth->write_urb->transfer_buffer_length = count;

	if (usb_submit_urb(bluetooth->write_urb))
		dbg(__FUNCTION__ " - usb_submit_urb(write bulk) failed");

	return count;
} 


static int bluetooth_write_room (struct tty_struct *tty) 
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);
	int room = 0;

	if (!bluetooth) {
		return -ENODEV;
	}

	dbg(__FUNCTION__);
	
	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not open");
		return -EINVAL;
	}

	if (bluetooth->write_urb->status != -EINPROGRESS)
		room = bluetooth->bulk_out_size;
	
	dbg(__FUNCTION__ " - returns %d", room);
	return room;
}


static int bluetooth_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);
	int chars = 0;

	if (!bluetooth) {
		return -ENODEV;
	}

	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not open");
		return -EINVAL;
	}


	if (bluetooth->write_urb->status == -EINPROGRESS)
		chars = bluetooth->write_urb->transfer_buffer_length;

	dbg (__FUNCTION__ " - returns %d", chars);
	return chars;
}


static void bluetooth_throttle (struct tty_struct * tty)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg(__FUNCTION__);

	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not open");
		return;
	}

	/* FIXME!!! */
	
	return;
}


static void bluetooth_unthrottle (struct tty_struct * tty)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg(__FUNCTION__);

	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not open");
		return;
	}

	/* FIXME!!! */

	return;
}


static int bluetooth_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return -ENODEV;
	}

	dbg(__FUNCTION__ " - cmd 0x%.4x", cmd);

	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not open");
		return -ENODEV;
	}

	/* FIXME!!! */
	
	return -ENOIOCTLCMD;
}


static void bluetooth_set_termios (struct tty_struct *tty, struct termios * old)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg(__FUNCTION__);

	if (!bluetooth->active) {
		dbg (__FUNCTION__ " - device not open");
		return;
	}

	/* FIXME!!! */
	
	return;
}


/*****************************************************************************
 * urb callback functions
 *****************************************************************************/

static void bluetooth_int_callback (struct urb *urb)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)urb->context, __FUNCTION__);
	unsigned char *data = urb->transfer_buffer;
	int i;

	dbg(__FUNCTION__);
	
	if (!bluetooth) {
		dbg(__FUNCTION__ " - bad bluetooth pointer, exiting");
		return;
	}

	if (urb->status) {
		dbg(__FUNCTION__ " - nonzero read bulk status received: %d", urb->status);
		return;
	}

#ifdef DEBUG
	if (urb->actual_length) {
		printk (KERN_DEBUG __FILE__ ": " __FUNCTION__ "- length = %d, data = ", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i) {
			printk ("%.2x ", data[i]);
		}
		printk ("\n");
	}
#endif

	/* Don't really know what else to do with this data yet. */
	/* FIXME!!! */
	
	return;
}


static void bluetooth_read_bulk_callback (struct urb *urb)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)urb->context, __FUNCTION__);
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int i;

	dbg(__FUNCTION__);
	
	if (!bluetooth) {
		dbg(__FUNCTION__ " - bad bluetooth pointer, exiting");
		return;
	}

	if (urb->status) {
		dbg(__FUNCTION__ " - nonzero read bulk status received: %d", urb->status);
		return;
	}

#ifdef DEBUG
	if (urb->actual_length) {
		printk (KERN_DEBUG __FILE__ ": " __FUNCTION__ "- length = %d, data = ", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i) {
			printk ("%.2x ", data[i]);
		}
		printk ("\n");
	}
#endif

	tty = bluetooth->tty;
	if (urb->actual_length) {
		for (i = 0; i < urb->actual_length ; ++i) {
			 tty_insert_flip_char(tty, data[i], 0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	if (usb_submit_urb(urb))
		dbg(__FUNCTION__ " - failed resubmitting read urb");

	return;
}


static void bluetooth_write_bulk_callback (struct urb *urb)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)urb->context, __FUNCTION__);

	dbg(__FUNCTION__);
	
	if (!bluetooth) {
		dbg(__FUNCTION__ " - bad bluetooth pointer, exiting");
		return;
	}

	if (urb->status) {
		dbg(__FUNCTION__ " - nonzero write bulk status received: %d", urb->status);
		return;
	}

	queue_task(&bluetooth->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	
	return;
}


static void bluetooth_softint(void *private)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)private, __FUNCTION__);
	struct tty_struct *tty;

	dbg(__FUNCTION__);
	
	if (!bluetooth) {
		return;
	}
 	
	tty = bluetooth->tty;
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup) {
		dbg(__FUNCTION__ " - write wakeup call.");
		(tty->ldisc.write_wakeup)(tty);
	}

	wake_up_interruptible(&tty->write_wait);
}


static void * usb_bluetooth_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_bluetooth *bluetooth = NULL;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[8];
	struct usb_endpoint_descriptor *bulk_in_endpoint[8];
	struct usb_endpoint_descriptor *bulk_out_endpoint[8];
	int minor;
	int buffer_size;
	int i;
	int num_interrupt_in = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	
	/* see if this device has the proper class signature */
	if ((dev->descriptor.bDeviceClass != WIRELESS_CLASS_CODE) || 
	    (dev->descriptor.bDeviceSubClass != RF_SUBCLASS_CODE) ||
	    (dev->descriptor.bDeviceProtocol != BLUETOOTH_PROGRAMMING_PROTOCOL_CODE)) {
		dbg (__FUNCTION__ " - class signature %d, %d, %d did not match", 
			dev->descriptor.bDeviceClass, dev->descriptor.bDeviceSubClass,
			dev->descriptor.bDeviceProtocol);
		return NULL;
	}

	/* find the endpoints that we need */
	interface = &dev->actconfig->interface[ifnum].altsetting[0];
	for (i = 0; i < interface->bNumEndpoints; ++i) {
		endpoint = &interface->endpoint[i];
		
		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk in endpoint */
			dbg("found bulk in");
			bulk_in_endpoint[num_bulk_in] = endpoint;
			++num_bulk_in;
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk out endpoint */
			dbg("found bulk out");
			bulk_out_endpoint[num_bulk_out] = endpoint;
			++num_bulk_out;
		}
		
		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x03)) {
			/* we found a interrupt in endpoint */
			dbg("found interrupt in");
			interrupt_in_endpoint[num_interrupt_in] = endpoint;
			++num_interrupt_in;
		}
	}
	
	/* according to the spec, we can only have 1 bulk_in, 1 bulk_out, and 1 interrupt_in endpoints */
	if ((num_bulk_in != 1) ||
	    (num_bulk_out != 1) ||
	    (num_interrupt_in != 1)) {
		dbg (__FUNCTION__ " - improper number of endpoints. Bluetooth driver not bound.");
		return NULL;
	}
	
	MOD_INC_USE_COUNT;
	info("USB Bluetooth converter detected");

	for (minor = 0; minor < BLUETOOTH_TTY_MINORS && bluetooth_table[minor]; ++minor)
		;
	if (bluetooth_table[minor]) {
		err("No more free Bluetooth devices");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if (!(bluetooth = kmalloc(sizeof(struct usb_bluetooth), GFP_KERNEL))) {
		err("Out of memory");
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	
	memset(bluetooth, 0, sizeof(struct usb_bluetooth));
	
	bluetooth->dev = dev;
	bluetooth->minor = minor;
	bluetooth->tqueue.routine = bluetooth_softint;
	bluetooth->tqueue.data = bluetooth;

	/* set up the endpoint information */
	endpoint = bulk_in_endpoint[0];
	bluetooth->read_urb = usb_alloc_urb (0);
	if (!bluetooth->read_urb) {
		err("No free urbs available");
		goto probe_error;
	}
	buffer_size = endpoint->wMaxPacketSize;
	bluetooth->bulk_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
	if (!bluetooth->bulk_in_buffer) {
		err("Couldn't allocate bulk_in_buffer");
		goto probe_error;
	}
	FILL_BULK_URB(bluetooth->read_urb, dev, usb_rcvbulkpipe(dev, endpoint->bEndpointAddress),
		      bluetooth->bulk_in_buffer, buffer_size, bluetooth_read_bulk_callback, bluetooth);

	endpoint = bulk_out_endpoint[0];
	bluetooth->write_urb = usb_alloc_urb(0);
	if (!bluetooth->write_urb) {
		err("No free urbs available");
		goto probe_error;
	}
	buffer_size = endpoint->wMaxPacketSize;
	bluetooth->bulk_out_size = buffer_size;
	bluetooth->bulk_out_buffer = kmalloc (buffer_size, GFP_KERNEL);
	if (!bluetooth->bulk_out_buffer) {
		err("Couldn't allocate bulk_out_buffer");
		goto probe_error;
	}
	FILL_BULK_URB(bluetooth->write_urb, dev, usb_sndbulkpipe(dev, endpoint->bEndpointAddress),
		      bluetooth->bulk_out_buffer, buffer_size, bluetooth_write_bulk_callback, bluetooth);

	endpoint = interrupt_in_endpoint[0];
	bluetooth->interrupt_in_urb = usb_alloc_urb(0);
	if (!bluetooth->interrupt_in_urb) {
		err("No free urbs available");
		goto probe_error;
	}
	buffer_size = endpoint->wMaxPacketSize;
	bluetooth->interrupt_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
	if (!bluetooth->interrupt_in_buffer) {
		err("Couldn't allocate interrupt_in_buffer");
		goto probe_error;
	}
	FILL_INT_URB(bluetooth->interrupt_in_urb, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		     bluetooth->interrupt_in_buffer, buffer_size, bluetooth_int_callback,
		     bluetooth, endpoint->bInterval);

	/* initialize the devfs nodes for this device and let the user know what bluetooths we are bound to */
	tty_register_devfs (&bluetooth_tty_driver, 0, minor);
	info("Bluetooth converter now attached to ttyBLUE%d (or usb/ttblue/%d for devfs)", minor, minor);
	
	return bluetooth; /* success */

probe_error:
	if (bluetooth->read_urb)
		usb_free_urb (bluetooth->read_urb);
	if (bluetooth->bulk_in_buffer)
		kfree (bluetooth->bulk_in_buffer);
	if (bluetooth->write_urb)
		usb_free_urb (bluetooth->write_urb);
	if (bluetooth->bulk_out_buffer)
		kfree (bluetooth->bulk_out_buffer);
	if (bluetooth->interrupt_in_urb)
		usb_free_urb (bluetooth->interrupt_in_urb);
	if (bluetooth->interrupt_in_buffer)
		kfree (bluetooth->interrupt_in_buffer);
		
	bluetooth_table[minor] = NULL;

	/* free up any memory that we allocated */
	kfree (bluetooth);
	MOD_DEC_USE_COUNT;
	return NULL;
}


static void usb_bluetooth_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_bluetooth *bluetooth = (struct usb_bluetooth *) ptr;

	if (bluetooth) {
		bluetooth->active = 0;

		if (bluetooth->read_urb) {
			usb_unlink_urb (bluetooth->read_urb);
			usb_free_urb (bluetooth->read_urb);
		}
		if (bluetooth->bulk_in_buffer)
			kfree (bluetooth->bulk_in_buffer);
			
		if (bluetooth->write_urb) {
			usb_unlink_urb (bluetooth->write_urb);
			usb_free_urb (bluetooth->write_urb);
		}
		if (bluetooth->bulk_out_buffer)
			kfree (bluetooth->bulk_out_buffer);
		
		if (bluetooth->interrupt_in_urb) {
			usb_unlink_urb (bluetooth->interrupt_in_urb);
			usb_free_urb (bluetooth->interrupt_in_urb);
		}
		if (bluetooth->interrupt_in_buffer)
			kfree (bluetooth->interrupt_in_buffer);

		tty_unregister_devfs (&bluetooth_tty_driver, bluetooth->minor);
		
		info("Bluetooth converter now disconnected from ttyBLUE%d", bluetooth->minor);

		bluetooth_table[bluetooth->minor] = NULL;

		/* free up any memory that we allocated */
		kfree (bluetooth);

	} else {
		info("device disconnected");
	}
	
	MOD_DEC_USE_COUNT;
}


static struct tty_driver bluetooth_tty_driver = {
	magic:			TTY_DRIVER_MAGIC,
	driver_name:		"usb-bluetooth",
	name:			"usb/ttblue/%d",
	major:			BLUETOOTH_TTY_MAJOR,
	minor_start:		0,
	num:			BLUETOOTH_TTY_MINORS,
	type:			TTY_DRIVER_TYPE_SERIAL,
	subtype:		SERIAL_TYPE_NORMAL,
	flags:			TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS,
	
	refcount:		&bluetooth_refcount,
	table:			bluetooth_tty,
	termios:		bluetooth_termios,
	termios_locked:		bluetooth_termios_locked,
	
	open:			bluetooth_open,
	close:			bluetooth_close,
	write:			bluetooth_write,
	write_room:		bluetooth_write_room,
	ioctl:			bluetooth_ioctl,
	set_termios:		bluetooth_set_termios,
	throttle:		bluetooth_throttle,
	unthrottle:		bluetooth_unthrottle,
	chars_in_buffer:	bluetooth_chars_in_buffer,
};


int usb_bluetooth_init(void)
{
	int i;
	int result;

	/* Initalize our global data */
	for (i = 0; i < BLUETOOTH_TTY_MINORS; ++i) {
		bluetooth_table[i] = NULL;
	}

	info ("USB Bluetooth support registered");

	/* register the tty driver */
	bluetooth_tty_driver.init_termios          = tty_std_termios;
	bluetooth_tty_driver.init_termios.c_cflag  = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	if (tty_register_driver (&bluetooth_tty_driver)) {
		err(__FUNCTION__ " - failed to register tty driver");
		return -1;
	}

	/* register the USB driver */
	result = usb_register(&usb_bluetooth_driver);
	if (result < 0) {
		tty_unregister_driver(&bluetooth_tty_driver);
		err("usb_register failed for the USB bluetooth driver. Error number %d", result);
		return -1;
	}
	
	return 0;
}


void usb_bluetooth_exit(void)
{
	usb_deregister(&usb_bluetooth_driver);
	tty_unregister_driver(&bluetooth_tty_driver);
}


module_init(usb_bluetooth_init);
module_exit(usb_bluetooth_exit);


