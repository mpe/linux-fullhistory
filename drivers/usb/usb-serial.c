/*
 * USB Serial Converter driver
 *
 * Greg Kroah-Hartman (greg@kroah.com)
 *
 * This was based on the ACM driver by Armin Fuerst (which was based
 * on a driver by Brad Keryan)
 *
 * Currently only works for the Belkin and Peracom Serial converters.
 * Should also work on the Etek serial converter, if anyone knows the
 * vendor and device ids for that device.
 *
 * 
 * version 0.1.2 (10/25/99) gkh
 *  Fixed bug in detecting device.
 *
 * version 0.1.1 (10/05/99) gkh
 *  Changed the major number to not conflict with anything else.
 *
 * version 0.1 (09/28/99) gkh
 *  Can recognize the two different devices and start up a read from
 * device when asked to. Writes also work. No control signals yet, this
 * all is vendor specific data (i.e. no spec), also no control for
 * different baud rates or other bit settings.
 * Currently we are using the same devid as the acm driver. This needs
 * to change.
 * 
 * (C) Copyright 1999 Greg Kroah-Hartman (greg@kroah.com)
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


/* USB Serial devices vendor ids and device ids that this driver supports */
#define BELKIN_VENDOR_ID		0x056c
#define BELKIN_SERIAL_CONVERTER		0x8007
#define PERACOM_VENDOR_ID		0x0565
#define PERACOM_SERIAL_CONVERTER	0x0001


#define SERIAL_MAJOR	240

#define NUM_PORTS	4	/* Have to pick a number for now. Need to look */
				/* into dynamically creating them at insertion time. */


static void * usb_serial_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_serial_disconnect(struct usb_device *dev, void *ptr);

typedef enum {
	unknown = 0,
	Belkin = 1,
	Peracom = 2
	} SERIAL_TYPE;

struct usb_serial_state {
	struct usb_device *	dev;
	SERIAL_TYPE		type;		/* what manufacturer's type of converter */
	void *			irq_handle;
	unsigned int		irqpipe;
	struct tty_struct	*tty;		/* the coresponding tty for this device */
	char			present;
	char			active;

	char			interrupt_in_inuse;
	__u8			interrupt_in_endpoint;
	__u8			interrupt_in_interval;
	__u16			interrupt_in_size;
	unsigned int		interrupt_in_pipe;
	unsigned char *		interrupt_in_buffer;
	void *			interrupt_in_transfer;

	char			bulk_in_inuse;
	__u8			bulk_in_endpoint;
	__u8			bulk_in_interval;
	__u16			bulk_in_size;
	unsigned int		bulk_in_pipe;
	unsigned char *		bulk_in_buffer;
	void *			bulk_in_transfer;

	char			bulk_out_inuse;
	__u8			bulk_out_endpoint;
	__u8			bulk_out_interval;
	__u16			bulk_out_size;
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

	debug_info("USB: serial_read_irq\n");

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





/* tty interface functions */
static int serial_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial;
	
	debug_info("USB: serial_open\n");

	serial = &serial_state_table [MINOR(tty->device)-tty->driver.minor_start];
	tty->driver_data = serial;
	serial->tty = tty;
	 
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


static void serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	debug_info("USB: serial_close\n");
	
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}

	if (!serial->active) {
		debug_info ("USB Serial: device already open\n");
		return;
	}

	/* Need to change the control lines here */
	/* FIXME */
	
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


static int serial_write (struct tty_struct * tty, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 
	int written;
	
	debug_info("USB Serial: serial_write\n");

	if (!serial->present) {
		debug_info("USB Serial: device not registered\n");
		return (-EINVAL);
	}

	if (!serial->active) {
		debug_info ("USB Serial: device not opened\n");
		return (-EINVAL);
	}
	
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


static void serial_put_char (struct tty_struct *tty, unsigned char ch)
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 
	
	debug_info("USB Serial: serial_put_char\n");
	
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}

	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
		return;
	}

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


static int serial_write_room (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	debug_info("USB Serial: serial_write_room\n");
	
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return (-EINVAL);
	}

	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
		return (-EINVAL);
	}
	
	if (serial->bulk_out_inuse) {
		return (0);
	}

	return serial->bulk_out_size;
}


static int serial_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial_state *serial = (struct usb_serial_state *)tty->driver_data; 

	debug_info("USB Serial: serial_chars_in_buffer\n");
	
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return (-EINVAL);
	}

	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
		return (-EINVAL);
	}
	
	if (serial->bulk_out_inuse) {
		return (serial->bulk_out_size);
	}

	return (0);
}


static void serial_throttle (struct tty_struct * tty)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	debug_info("USB Serial: serial_throttle\n");
	
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}

	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
		return;
	}


	/* Change the control signals */
	/* FIXME!!! */

	return;
}


static void serial_unthrottle (struct tty_struct * tty)
{
	struct usb_serial_state *serial = (struct usb_serial_state *) tty->driver_data; 

	debug_info("USB Serial: serial_unthrottle\n");
	
	if (!serial->present) {
		debug_info("USB Serial: no device registered\n");
		return;
	}

	if (!serial->active) {
		debug_info ("USB Serial: device not open\n");
		return;
	}


	/* Change the control signals */
	/* FIXME!!! */

	return;
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
	struct usb_serial_state *serial;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	SERIAL_TYPE type;
	int serial_num;
//	int ret;
	int i;
	
	/* look at the device descriptor to see if it is a type that we	recognize */
	type = unknown;
	if ((dev->descriptor.idVendor == BELKIN_VENDOR_ID) &&
	    (dev->descriptor.idProduct == BELKIN_SERIAL_CONVERTER)) {
		/* This is the Belkin serial convertor */
		type = Belkin;
		}
	
	if ((dev->descriptor.idVendor == PERACOM_VENDOR_ID) &&
	    (dev->descriptor.idProduct == PERACOM_SERIAL_CONVERTER)) {
		/* This is the Peracom serial convertor */
		type = Peracom;
		}

	if (type == unknown)
		return NULL;	

	printk (KERN_INFO "USB serial converter detected.\n");

	if (0>(serial_num = Get_Free_Serial())) {
		debug_info("USB Serial: Too many devices connected\n");
		return NULL;
	}
	
	serial = &serial_state_table[serial_num];

       	memset(serial, 0, sizeof(serial));
       	serial->dev = dev;
	serial->type = type;

	/* we should have 1 bulk in, 1 bulk out, and 1 interrupt in endpoints */
	interface = &dev->actconfig->interface[ifnum].altsetting[0];
	for (i = 0; i < interface->bNumEndpoints; ++i) {
		endpoint = &interface->endpoint[i];
		
		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found the bulk in endpoint */
			serial->bulk_in_inuse = 0;
			serial->bulk_in_endpoint = endpoint->bEndpointAddress;
			serial->bulk_in_size = endpoint->wMaxPacketSize;
			serial->bulk_in_interval = endpoint->bInterval;
			serial->bulk_in_pipe = usb_rcvbulkpipe (dev, serial->bulk_in_endpoint);
			serial->bulk_in_buffer = kmalloc (serial->bulk_in_size, GFP_KERNEL);
			if (!serial->bulk_in_buffer) {
				printk("USB Serial: Couldn't allocate bulk_in_buffer\n");
				goto probe_error;
			}
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found the bulk out endpoint */
			serial->bulk_out_inuse = 0;
			serial->bulk_out_endpoint = endpoint->bEndpointAddress;
			serial->bulk_out_size = endpoint->wMaxPacketSize;
			serial->bulk_out_interval = endpoint->bInterval;
			serial->bulk_out_pipe = usb_rcvbulkpipe (dev, serial->bulk_out_endpoint);
			serial->bulk_out_buffer = kmalloc (serial->bulk_out_size, GFP_KERNEL);
			if (!serial->bulk_out_buffer) {
				printk("USB Serial: Couldn't allocate bulk_out_buffer\n");
				goto probe_error;
			}
		}
		
		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x03)) {
			/* we found the interrupt in endpoint */
			serial->interrupt_in_inuse = 0;
			serial->interrupt_in_endpoint = endpoint->bEndpointAddress;
			serial->interrupt_in_size = endpoint->wMaxPacketSize;
			serial->interrupt_in_interval = endpoint->bInterval;
			/* serial->interrupt_in_pipe = usb_rcvbulkpipe (dev, serial->bulk_in_endpoint); */
			serial->interrupt_in_buffer = kmalloc (serial->bulk_in_size, GFP_KERNEL);
			if (!serial->interrupt_in_buffer) {
				printk("USB Serial: Couldn't allocate interrupt_in_buffer\n");
				goto probe_error;
			}
		}

	}
	

	/* verify that we found all of the endpoints that we need */
	if ((!serial->bulk_in_buffer) || 
	    (!serial->bulk_out_buffer) ||
	    (!serial->interrupt_in_buffer)) {
		printk("USB Serial: did not find all of the required endpoints\n");
		goto probe_error;
	}
		

	/* set up an interrupt for out bulk in pipe */
	/* ask for a bulk read */
//	serial->bulk_in_inuse = 1;
//	serial->bulk_in_transfer = usb_request_bulk (serial->dev, serial->bulk_in_pipe, serial_read_irq, serial->bulk_in_buffer, serial->bulk_in_size, serial);

	/* set up our interrupt to be the time for the bulk in read */
//	ret = usb_request_irq (dev, serial->bulk_in_pipe, usb_serial_irq, serial->bulk_in_interval, serial, &serial->irq_handle);
//	if (ret) {
//		printk(KERN_INFO "USB Serial failed usb_request_irq (0x%x)\n", ret);
//		goto probe_error;
//	}
	
	serial->present = 1;
	MOD_INC_USE_COUNT;

	return serial;

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

	printk (KERN_INFO "USB Serial device disconnected.\n");
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
		printk(KERN_ERR "USB serial driver cannot register: "
			"minor number %d already in use\n",
			usb_serial_driver.minor);
		return -1;
	}

	printk(KERN_INFO "USB Serial support registered.\n");
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

