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

  Wed Jul 19 14:00:42 EST 2000 gkh
	Added module_init and module_exit functions to handle the fact that this
	driver is a loadable module now.

  Tue Jul 18 16:14:52 EST 2000 Hugh
    Basic character input/output for USA-19 now mostly works,
    fixed at 9600 baud for the moment.

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

#ifdef CONFIG_USB_SERIAL_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#include "usb-serial.h"
#include "keyspan.h"

	/* Per device and per port private data */
struct keyspan_serial_private {
	struct urb	*in_urbs[8];
	struct urb	*out_urbs[8];
	char		out_buffer[64];
	char		in_buffer[64];
};

struct keyspan_port_private {
 		/* Keep track of which output endpoint to use */
	int		out_flip;

		/* Settings for the port */
	int		baud;
	int		old_baud;
	enum		{parity_none, parity_odd, parity_even} parity;
	enum		{flow_none, flow_cts, flow_xon} flow_control;
	int		rts_state;
	int		dtr_state;

};


	/* FIXME this will break if multiple physical interfaces used */
static wait_queue_head_t	out_wait;
	
	/* Include Keyspan message headers (not both yet, need some tweaks
	   to get clean build) */
/*#include "keyspan_usa26msg.h"*/
#include "keyspan_usa28msg.h"
	
	/* If you don't get debugging output, uncomment the following
	   two lines to enable cheat. */
#undef 	dbg
#define	dbg	printk

	/* Functions - mostly stubs for now */
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
	unsigned int		value;

	dbg("keyspan_ioctl_info");
	
	switch (cmd) {
	case TIOCMGET:
		value = TIOCM_DTR | TIOCM_RNG;
		if (copy_to_user((unsigned int *)arg, &value, sizeof(int))) {
			return -EFAULT;
		}
		else {
			return 0;
		}
		
	default:
		return -ENOIOCTLCMD;
	}

	return -ENOIOCTLCMD;
}

static int keyspan_write(struct usb_serial_port *port, int from_user, 
			     const unsigned char *buf, int count)
{
	struct usb_serial 		*serial = port->serial;
	struct keyspan_serial_private 	*s_priv;
	struct keyspan_port_private 	*p_priv;
	int				current_urb;
	int				i;

	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);
	
	if (p_priv->out_flip == 0) {
		current_urb = 0;
		p_priv->out_flip = 1;
	}
	else {
		current_urb = 1;
		p_priv->out_flip = 0;
	}

	dbg("keyspan_write called for port %d (%d) chars {", port->number, count);
	for (i = 0; i < count ; i++) {
		dbg("%02x ", buf[i]);
	}
	dbg("}\n");

	if (count == 0) {
		dbg("write request of 0 bytes");
		return (0);
	}

		/* only send data if we have a bulk out endpoint */
	if (s_priv->out_urbs[current_urb]) {
		while (s_priv->out_urbs[current_urb]->status == -EINPROGRESS) {
			dbg (__FUNCTION__ " INPROGRES\n");
			interruptible_sleep_on(&out_wait);
			if (signal_pending(current)) {
				dbg (__FUNCTION__ " signal\n");
				return (-ERESTARTSYS);
			}
		}
		/*if (s_priv->out_urbs[current_urb]->status == -EINPROGRESS) {
			dbg ("already writing");
			return (-EAGAIN);
		}*/
			/* First byte in buffer is "last flag" - unused so
			   for now so set to zero */
		memset(s_priv->out_urbs[current_urb]->transfer_buffer, 0, 1);

		if (from_user) {
			copy_from_user(s_priv->out_urbs[current_urb]->transfer_buffer + 1, buf, count);
		}
		else {
			memcpy (s_priv->out_urbs[current_urb]->transfer_buffer + 1, buf, count);
		}  

		/* send the data out the bulk port */
		s_priv->out_urbs[current_urb]->transfer_buffer_length = count + 1;

		if (usb_submit_urb(s_priv->out_urbs[current_urb])) {
			dbg("usb_submit_urb(write bulk) failed");
		}

		return (count);
	}
	
	/* no bulk out, so return 0 bytes written */
	return (0);
}


static void keyspan_write_bulk_callback (struct urb *urb)
{
	int		endpoint;
	
	endpoint = usb_pipeendpoint(urb->pipe);

	dbg("keyspan_write_bulk_callback for endpoint %d\n", endpoint);

		/* Only do wakeup if this callback is from one of the data
		   endpoints.  */
	if (endpoint == 2 || endpoint == 3) {
		wake_up_interruptible(&out_wait);
	}

}


static void keyspan_read_bulk_callback (struct urb *urb)
{
	int			i;
	int			endpoint;
	struct usb_serial	*serial;
	struct usb_serial_port	*port;
	struct tty_struct	*tty;
	unsigned char 		*data = urb->transfer_buffer;

	endpoint = usb_pipeendpoint(urb->pipe);


	if (urb->status) {
		dbg(__FUNCTION__ "nonzero status: %x on endpoint %d.\n",
			      		urb->status, endpoint);
		return;
	}

	switch (endpoint) {

			/* If this is one of the data endpoints, stuff it's
		   	   contents into the tty flip_buffer. */
		case 1:
		case 2: serial = (struct usb_serial *) urb->context;
			port = &serial->port[0];
			tty = port->tty;
			if (urb->actual_length) {
				for (i = 0; i < urb->actual_length ; ++i) {
					 tty_insert_flip_char(tty, data[i], 0);
				}
				tty_flip_buffer_push(tty);
			}
			break;

			/* INACK endpoint */	
		case 3: dbg(__FUNCTION__ " callback for INACK endpoint\n");
			break;

			/* INSTAT endpoint */
		case 4: dbg(__FUNCTION__ " callback for INSTAT endpoint\n");
			break;
		
		default:
		       	dbg(__FUNCTION__ " callback for unknown endpoint!\n");
			break;
	}
				
		/* Resubmit urb so we continue receiving */
	if (usb_submit_urb(urb)) {
		dbg(__FUNCTION__ "resubmit read urb failed.\n");
	}
	return;
	
}

static int keyspan_write_room (struct usb_serial_port *port)
{
//	dbg("keyspan_write_room called\n");
	return (32);

}


static int keyspan_chars_in_buffer (struct usb_serial_port *port)
{
	return (0);
}


static int keyspan_open (struct usb_serial_port *port, struct file *filp)
{
	struct keyspan_port_private 	*p_priv;
	struct keyspan_serial_private 	*s_priv;
	struct usb_serial 		*serial = port->serial;
	int				i;

	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);
	
	dbg("keyspan_open called.\n");
	
	if (port->active) {
		dbg(__FUNCTION__ "port->active already true!\n");
		return (-EINVAL);
	}

	p_priv = (struct keyspan_port_private *)(port->private);
	
	p_priv->out_flip = 0;
	port->active = 1;

		/* Start reading from port */
	for (i = 0; i < 4; i++) {
		if (s_priv->in_urbs[i]) {
			if (usb_submit_urb(s_priv->in_urbs[i])) {
				dbg(__FUNCTION__ " submit in urb %d failed", i);
			}
		}

	}
	
	keyspan_usa19_send_setup(serial, port);

	return (0);
}


static void keyspan_close(struct usb_serial_port *port, struct file *filp)
{
	int			i;
	struct usb_serial	*serial = port->serial; /* FIXME should so sanity check */
	struct keyspan_serial_private 	*s_priv;

	s_priv = (struct keyspan_serial_private *)(serial->private);
	
		/* Stop reading/writing urbs */
	for (i = 0; i < 4; i++) {
		if (s_priv->in_urbs[i]) {
			usb_unlink_urb(s_priv->in_urbs[i]);
		}

	}
	for (i = 0; i < 3; i++) {
		if (s_priv->out_urbs[i]) {
			usb_unlink_urb(s_priv->out_urbs[i]);
		}

	}
	port->active = 0;
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
		/* bring device out of reset. Renumeration will occur in a
		   moment and the new device will bind to the real driver */
	response = ezusb_set_reset(serial, 0);

		/* we don't want this device to have a driver assigned to it. */
	return (1);
}

	/* USA-19 uses three output endpoints and four input
	   endpoints.  First two output endpoints are for
	   data (used in an alternating fashion), the third is
	   output control.  First two input endpoints are for
	   data (again alternating), the third is the ACK
	   endpoint, the fourth is input status. */
static void keyspan_usa19_setup_urbs(struct usb_serial *serial)
{
	struct keyspan_serial_private 	*s_priv;
	int			i;

	s_priv = (struct keyspan_serial_private *)(serial->private);

		/* Output urbs first */
	dbg(__FUNCTION__ "Allocating output urbs.\n");
	for (i = 0; i < 3; i++) {

		s_priv->out_urbs[i] = usb_alloc_urb (0);	/* No ISO */
		if (!s_priv->out_urbs[i]) {
			dbg (__FUNCTION__ "Alloc for %d out urb failed.\n", i);
			return;
		}

		FILL_BULK_URB(s_priv->out_urbs[i], serial->dev, 
			      usb_sndbulkpipe(serial->dev, i + 1),
			      &s_priv->out_buffer[i], sizeof(s_priv->out_buffer[i]),
			      keyspan_write_bulk_callback, 
			      serial);
	}

		/* Now input urbs */
	dbg(__FUNCTION__ "Allocating input urbs.\n");
	for (i = 0; i < 4; i++) {

		s_priv->in_urbs[i] = usb_alloc_urb (0);	/* No ISO */
		if (!s_priv->in_urbs[i]) {
			dbg (__FUNCTION__ "Alloc for %d in urb failed.\n", i);
			return;
		}

		FILL_BULK_URB(s_priv->in_urbs[i], serial->dev, 
			      usb_rcvbulkpipe(serial->dev, i + 0x81),
			      &s_priv->in_buffer[i], sizeof(s_priv->in_buffer[i]),
			      keyspan_read_bulk_callback, 
			      serial);
	}
	
}

static int keyspan_usa19_calc_baud(u32 baud_rate, u8 *rate_hi, u8 *rate_low)
{
	u32 b16,	/* baud rate times 16 (actual rate used internally) */
		div,	/* divisor */	
		cnt;	/* inverse of divisor (programmed into 8051) */

		/* prevent divide by zero...  */
	if( (b16 = (baud_rate * 16L)) == 0) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}

		/* calculate the divisor and the counter (its inverse) */
	if( (div = (USA19_BAUDCLK / b16)) == 0) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}
	else {
		cnt = 0 - div;
	}

	if(div > 0xffff) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}

		/* return the counter values */
	*rate_low = (u8) (cnt & 0xff);
	*rate_hi = (u8) ((cnt >> 8) & 0xff);
	
	dbg(__FUNCTION__ " Baud rate of %d is %02x %02x.\n", baud_rate, *rate_hi, *rate_low);

	return (KEYSPAN_BAUD_RATE_OK);
}

static int keyspan_usa19_send_setup(struct usb_serial *serial, struct usb_serial_port *port)
{
	struct portControlMessage	msg;		
	struct keyspan_serial_private 	*s_priv;
	struct keyspan_port_private 	*p_priv;

	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);

	//memset(msg, 0, sizeof (struct portControlMessage));
		
	msg.setBaudRate = 1;
	if (keyspan_usa19_calc_baud(9600, &msg.baudHi, &msg.baudLo) ==
		KEYSPAN_INVALID_BAUD_RATE ) {
		dbg(__FUNCTION__ "Invalid baud rate requested %d.\n", 9600);
		msg.baudLo = 0xff;
		msg.baudHi = 0xb2;	/* Values for 9600 baud */
	}

		/* If parity is enabled, we must calculate it ourselves. */
	if (p_priv->parity) {
		msg.parity = 1;
	}
	else {
		msg.parity = 0;
	}

	msg.ctsFlowControl = 0;
	msg.xonFlowControl = 0;
	msg.rts = 1;
	msg.dtr = 0;
	
	msg.forwardingLength = 1;
	msg.forwardMs = 10;
	msg.breakThreshold = 45;
	msg.xonChar = 17;
	msg.xoffChar = 19;
	
	msg._txOn = 1;
	msg._txOff = 0;
	msg.txFlush = 0;
	msg.txForceXoff = 0;
	msg.txBreak = 0;
	msg.rxOn = 1;
	msg.rxOff = 0;
	msg.rxFlush = 0;
	msg.rxForward = 0;
	msg.returnStatus = 1;
	msg.resetDataToggle = 1;

		
	/* only do something if we have a bulk out endpoint */
	if (s_priv->out_urbs[2]) {
		if (s_priv->out_urbs[2]->status == -EINPROGRESS) {
			dbg (__FUNCTION__ " already writing");
			return(-1);
		}
		memcpy (s_priv->out_urbs[2]->transfer_buffer, &msg, sizeof(msg));
	
			/* send the data out the device on control endpoint */
		s_priv->out_urbs[2]->transfer_buffer_length = sizeof(msg);

		if (usb_submit_urb(s_priv->out_urbs[2])) {
			dbg(__FUNCTION__ " usb_submit_urb(setup) failed\n");
		}
		else {
			dbg(__FUNCTION__ " usb_submit_urb(setup) OK %d bytes\n", s_priv->out_urbs[2]->transfer_buffer_length);
		}
	
	}
	return (0);
}


	/* Gets called by the "real" driver (ie once firmware is loaded
	   and renumeration has taken place. */
static int keyspan_startup (struct usb_serial *serial)
{
	int			i;
	struct usb_serial_port	*port;

	dbg("keyspan_startup called.\n");

		/* Setup private data for serial driver */
	serial->private = kmalloc(sizeof(struct keyspan_serial_private), GFP_KERNEL);
	if (!serial->private) {
		dbg(__FUNCTION__ "kmalloc for keyspan_serial_private failed!.\n");
		return (1);
	}
	memset(serial->private, 0, sizeof(struct keyspan_serial_private));
		
	init_waitqueue_head(&out_wait);
	
		/* Now setup per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = &serial->port[i];
		port->private = kmalloc(sizeof(struct keyspan_port_private), GFP_KERNEL);
		if (!port->private) {
			dbg(__FUNCTION__ "kmalloc for keyspan_port_private (%d) failed!.\n", i);
			return (1);
		}
		memset(port->private, 0, sizeof(struct keyspan_port_private));
	}

	

	switch (serial->dev->descriptor.idProduct) {
		case 0x0107:	keyspan_usa19_setup_urbs(serial);
				//keyspan_send_usa19_setup(serial);
				break;

		default:	break;
	}

			
	return (0);
}

static void keyspan_shutdown (struct usb_serial *serial)
{
	int				i;
	struct usb_serial_port		*port;
	struct keyspan_serial_private 	*s_priv;

	dbg("keyspan_shutdown called freeing ");

	s_priv = (struct keyspan_serial_private *)(serial->private);

		/* Stop reading/writing urbs */
	for (i = 0; i < 4; i++) {
		if (s_priv->in_urbs[i]) {
			usb_unlink_urb(s_priv->in_urbs[i]);
		}

	}
	for (i = 0; i < 3; i++) {
		if (s_priv->out_urbs[i]) {
			usb_unlink_urb(s_priv->out_urbs[i]);
		}

	}
		/* Now free them */
	for (i = 0; i < 7; i ++) {
		if (s_priv->in_urbs[i] != NULL) {
			dbg("in%d ", i);
			usb_free_urb(s_priv->in_urbs[i]);
		}
		
		if (s_priv->out_urbs[i] != NULL) {
			dbg("out%d ", i);
			usb_free_urb(s_priv->out_urbs[i]);
		}
	}
	dbg("urbs.\n");

	dbg("Freeing serial->private.\n");	
	kfree(serial->private);

	dbg("Freeing port->private.\n");	
		/* Now free per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = &serial->port[i];
		kfree(port->private);
	}
	
}


static int __init keyspan_init (void)
{
	usb_serial_register (&keyspan_usa18x_pre_device);
	usb_serial_register (&keyspan_usa19_pre_device);
	usb_serial_register (&keyspan_usa19w_pre_device);
	usb_serial_register (&keyspan_usa28_pre_device);
	usb_serial_register (&keyspan_usa28x_pre_device);
	usb_serial_register (&keyspan_usa18x_device);
	usb_serial_register (&keyspan_usa19_device);
	usb_serial_register (&keyspan_usa19w_device);
	usb_serial_register (&keyspan_usa28_device);
	usb_serial_register (&keyspan_usa28x_device);
	return 0;
}


static void __exit keyspan_exit (void)
{
	usb_serial_deregister (&keyspan_usa18x_pre_device);
	usb_serial_deregister (&keyspan_usa19_pre_device);
	usb_serial_deregister (&keyspan_usa19w_pre_device);
	usb_serial_deregister (&keyspan_usa28_pre_device);
	usb_serial_deregister (&keyspan_usa28x_pre_device);
	usb_serial_deregister (&keyspan_usa18x_device);
	usb_serial_deregister (&keyspan_usa19_device);
	usb_serial_deregister (&keyspan_usa19w_device);
	usb_serial_deregister (&keyspan_usa28_device);
	usb_serial_deregister (&keyspan_usa28x_device);
}


module_init(keyspan_init);
module_exit(keyspan_exit);

MODULE_AUTHOR("Hugh Blemings <hugh@linuxcare.com>");
MODULE_DESCRIPTION("Keyspan USB to Serial Converter driver");
