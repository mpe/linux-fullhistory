/*
 * USB FTDI SIO driver
 *
 * 	(C) Copyright (C) 1999, 2000
 * 	    Greg Kroah-Hartman (greg@kroah.com)
 *          Bill Ryder (bryder@sgi.com)
 *
 * 	This program is free software; you can redistribute it and/or modify
 * 	it under the terms of the GNU General Public License as published by
 * 	the Free Software Foundation; either version 2 of the License, or
 * 	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * (04/04/2000) Bill Ryder 
 *         Fixed bugs in TCGET/TCSET ioctls (by removing them - they are 
 *             handled elsewhere in the serial driver chain).
 *
 * (03/30/2000) Bill Ryder 
 *         Implemented lots of ioctls
 * 	Fixed a race condition in write
 * 	Changed some dbg's to errs
 *
 * (03/26/2000) gkh
 * 	Split driver up into device specific pieces.
 *
 */

/* Bill Ryder - bryder@sgi.com - wrote the FTDI_SIO implementation */
/* Thanx to FTDI for so kindly providing details of the protocol required */
/*   to talk to the device */


#include <linux/config.h>

#ifdef CONFIG_USB_SERIAL_FTDI_SIO

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

#include "ftdi_sio.h"

#define FTDI_VENDOR_ID			0x0403
#define FTDI_SIO_SERIAL_CONVERTER_ID	0x8372

/* function prototypes for a FTDI serial converter */
static int  ftdi_sio_startup		(struct usb_serial *serial);
static int  ftdi_sio_open		(struct usb_serial_port *port, struct file *filp);
static void ftdi_sio_close		(struct usb_serial_port *port, struct file *filp);
static int  ftdi_sio_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static void ftdi_sio_write_bulk_callback (struct urb *urb);
static void ftdi_sio_read_bulk_callback	(struct urb *urb);
static void ftdi_sio_set_termios	(struct usb_serial_port *port, struct termios * old);
static int  ftdi_sio_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);

/* All of the device info needed for the FTDI SIO serial converter */
static __u16	ftdi_vendor_id		= FTDI_VENDOR_ID;
static __u16	ftdi_sio_product_id	= FTDI_SIO_SERIAL_CONVERTER_ID;
struct usb_serial_device_type ftdi_sio_device = {
	name:			"FTDI SIO",
	idVendor:		&ftdi_vendor_id,	/* the FTDI vendor ID */
	idProduct:		&ftdi_sio_product_id,	/* the FTDI SIO product id */
	needs_interrupt_in:	MUST_HAVE_NOT,		/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			ftdi_sio_open,
	close:			ftdi_sio_close,
	write:			ftdi_sio_write,
	read_bulk_callback:	ftdi_sio_read_bulk_callback,
	write_bulk_callback:	ftdi_sio_write_bulk_callback,
	ioctl:			ftdi_sio_ioctl,
	set_termios:		ftdi_sio_set_termios,
	startup:		ftdi_sio_startup,
};


/*
 * ***************************************************************************
 * FTDI SIO Serial Converter specific driver functions
 * ***************************************************************************
 *
 * Bill Ryder bryder@sgi.com of Silicon Graphics, Inc. did the FTDI_SIO code
 * Thanx to FTDI for so kindly providing details of the protocol required
 *   to talk to the device - http://www.ftdi.co.uk
 *
 * Tested as at this version - other stuff might work
 * 23 March 2000
 *     Works:
 *      Baudrates - 9600, 38400,19200, 57600, 115200  
 *      TIOCMBIC - TIOCM_DTR / TIOCM_RTS 
 *      TIOCMBIS - TIOCM_DTR / TIOCM_RTS 
 *      TIOCMSET - DTR on/RTSon  / DTR off, RTS off 
 *      no parity:CS8 even parity:CS7 odd parity:CS7 
 *      CRTSCTS flow control 
 *     
 *      Pilot-xfer zillions of times
 *  
 *      cu works with dir option 
 *
 *   Not Tested (ie might not work): 
 *      xon/xoff flow control 
 *      ppp (modem handling in general) 
 *
 *   KNOWN BUGS:
 *    Multiple Opens
 *    ==============
 *      Seems to have problem when opening an already open port, 
 *      Get I/O error on first attempt, then it lets you in. 
 *      Need to do proper usage counting - keep registered callbacks for first opener.
 *     
 *     Reproduce with: 
 *       cu -l /dev/ttyUSB0 dir 
 *       whilst cu is running do:
 *        stty -a < /dev/ttyUSB0  
 *
 *     from stty get: 'bash: /dev/ttyUSB0: Invalid argument ' 
 *     from cu get 
 *        write: Invalid argument 
 *    
 *    Initialisation Problem
 *    ======================
 *    Pilot transfer required me to run the serial_loopback program before it would work.
 *    Still working on this. See the webpage http://reality.sgi.com/bryder_wellington/ftdi_sio
 *
 */

#define WDR_TIMEOUT (HZ * 5 ) /* default urb timeout */

/* do some startup allocations not currently performed by usb_serial_probe() */
static int ftdi_sio_startup (struct usb_serial *serial)
{
	init_waitqueue_head(&serial->port[0].write_wait);
	
	return (0);
}

static int  ftdi_sio_open (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_sio_open */
	struct termios tmp_termios;
	struct usb_serial *serial = port->serial;
	char buf[1]; /* Needed for the usb_control_msg I think */

	dbg("ftdi_sio_open port %d", port->number);

        /* FIXME - multiple concurrent opens cause trouble */
	if (port->active) {
		err ("port already open");
		return -EINVAL;
	}
	port->active = 1; /* FIXME - For multiple open this should increment */

	/* See ftdi_sio.h for description of what is reset */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			FTDI_SIO_RESET_REQUEST, FTDI_SIO_RESET_REQUEST_TYPE, 
			FTDI_SIO_RESET_SIO, 
			0, buf, 0, WDR_TIMEOUT);

	/* Setup termios */
	port->tty->termios->c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;

	
	ftdi_sio_set_termios(port, &tmp_termios);	

	/* Disable flow control */
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
			    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
			    0, 0, 
			    buf, 0, WDR_TIMEOUT) < 0) {
		err("error from flowcontrol urb");
		return(-EINVAL);
	}	    

	/* Turn on RTS and DTR since we are not flow controlling*/
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			    (unsigned)FTDI_SIO_SET_DTR_HIGH, 0, 
			    buf, 0, WDR_TIMEOUT) < 0) {
		err("Error from DTR HIGH urb");
	}
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			    (unsigned)FTDI_SIO_SET_RTS_HIGH, 0, 
			    buf, 0, WDR_TIMEOUT) < 0) {
		err("Error from RTS HIGH urb");
	}
	
	/*Start reading from the device*/
	if (usb_submit_urb(port->read_urb))
		err("usb_submit_urb(read bulk) failed");

	return (0);
} /* ftdi_sio_open */


static void ftdi_sio_close (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_sio_close */
	struct usb_serial *serial = port->serial;
	unsigned int c_cflag = port->tty->termios->c_cflag;
	char buf[1];

	dbg("ftdi_sio_close port %d", port->number);
	
	if (c_cflag & HUPCL){
		/* Disable flow control */
		if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("error from flowcontrol urb");
		}	    

		/* drop DTR */
 		if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
				    (unsigned)FTDI_SIO_SET_DTR_LOW, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("Error from DTR LOW urb");
		}
		/* drop RTS */
		if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
				    (unsigned)FTDI_SIO_SET_RTS_LOW, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("Error from RTS LOW urb");
		}	
	}

	/* shutdown our bulk reads and writes */
	usb_unlink_urb (port->write_urb);
	usb_unlink_urb (port->read_urb);
	port->active = 0;
} /* ftdi_sio_close */


  
/* The ftdi_sio requires the first byte to have:
 *  B0 1
 *  B1 0
 *  B2..7 length of message excluding byte 0
 */
static int ftdi_sio_write (struct usb_serial_port *port, int from_user, 
			   const unsigned char *buf, int count)
{ /* ftdi_sio_write */
	struct usb_serial *serial = port->serial;
	const int data_offset = 1;
	int rc; 
	DECLARE_WAITQUEUE(wait, current);
	
	dbg("ftdi_sio_serial_write port %d, %d bytes", port->number, count);

	if (count == 0) {
		err("write request of 0 bytes");
		return 0;
	}

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		unsigned char *first_byte = port->write_urb->transfer_buffer;

		/* Was seeing a race here, got a read callback, then write callback before
		   hitting interuptible_sleep_on  - so wrapping in add_wait_queue stuff */

		add_wait_queue(&port->write_wait, &wait);
		set_current_state (TASK_INTERRUPTIBLE);
		while (port->write_urb->status == -EINPROGRESS) {
			dbg("ftdi_sio - write in progress - retrying");
			if (0 /* file->f_flags & O_NONBLOCK */) {
				rc = -EAGAIN;
				goto err;
			}
			if (signal_pending(current)) {
				current->state = TASK_RUNNING;
				remove_wait_queue(&port->write_wait, &wait);
				rc = -ERESTARTSYS;
				goto err;
			}
			schedule();
		}		
		remove_wait_queue(&port->write_wait, &wait);
		set_current_state(TASK_RUNNING);

		count += data_offset;
		count = (count > port->bulk_out_size) ? port->bulk_out_size : count;
		if (count == 0) {
			return 0;
		}

		/* Copy in the data to send */
		if (from_user) {
			copy_from_user(port->write_urb->transfer_buffer + data_offset , 
				       buf, count - data_offset );
		}
		else {
			memcpy(port->write_urb->transfer_buffer + data_offset,
			       buf, count - data_offset );
		}  

		/* Write the control byte at the front of the packet*/
		first_byte = port->write_urb->transfer_buffer;
		*first_byte = 1 | ((count-data_offset) << 2) ; 

#ifdef CONFIG_USB_SERIAL_DEBUG
		dbg("Bytes: %d, Control Byte: 0o%03o",count, first_byte[0]);

		if (count) {
			int i;
			printk (KERN_DEBUG __FILE__ ": data written - length = %d, data = ", count);
			for (i = 0; i < count; ++i) {
				printk ( "0x%02x ", first_byte[i]);
				if (first_byte[i] > ' ' && first_byte[i] < '~') {
					printk( "%c ", first_byte[i]);
				} else {
					printk( "  ");
				}
			}

		     
			printk ( "\n");
		}

#endif
		/* send the data out the bulk port */
		port->write_urb->transfer_buffer_length = count;

		if (usb_submit_urb(port->write_urb))
			err("usb_submit_urb(write bulk) failed");

		dbg("write returning: %d", count - data_offset);
		return (count - data_offset);
	}
	
	/* no bulk out, so return 0 bytes written */
	return 0;
 err: /* error exit */
	return(rc);
} /* ftdi_sio_write */

static void ftdi_sio_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty = port->tty;

	dbg("ftdi_sio_write_bulk_callback");

	if (port_paranoia_check (port, "ftdi_sio_write_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "ftdi_sio_write_bulk_callback")) {
		return;
	}
	
	if (urb->status) {
		dbg("nonzero write bulk status received: %d", urb->status);
		return;
	}

	wake_up_interruptible(&port->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);
	
	return;
} /* ftdi_sio_write_bulk_callback */

static void ftdi_sio_read_bulk_callback (struct urb *urb)
{ /* ftdi_sio_serial_buld_callback */
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty = port->tty ;
       	unsigned char *data = urb->transfer_buffer;

	const int data_offset = 2;
	int i;

	dbg("ftdi_sio read callback");

	if (port_paranoia_check (port, "ftdi_sio_read_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "ftdi_sio_read_bulk_callback")) {
		return;
	}
	
	/* TO DO -- check for hung up line and handle appropriately: */
	/*   send hangup (need to find out how to do this) */
 

	if (urb->status) {
		/* This will happen at close every time so it is a dbg not an err */
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

#ifdef CONFIG_USB_SERIAL_DEBUG
	if (urb->actual_length > 2) {
		printk (KERN_DEBUG __FILE__ ": data read - length = %d, data = ", urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i) {
			printk ( "0x%.2x ", data[i]);
			if (data[i] > ' ' && data[i] < '~') {
				printk( "%c ", data[i]);
			} else {
				printk( "  ");
			}
		}
		printk ( "\n");
	} else {
                dbg("Just status");
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
		err("failed resubmitting read urb");

	return;
} /* ftdi_sio_serial_read_bulk_callback */

/* As I understand this - old_termios contains the original termios settings */
/*  and tty->termios contains the new setting to be used */
/* */
/*   WARNING: set_termios calls this with old_termios in kernel space */

static void ftdi_sio_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{ /* ftdi_sio_set_termios */
	struct usb_serial *serial = port->serial;
	unsigned int cflag = port->tty->termios->c_cflag;
	__u16 urb_value; /* Will hold the new flags */
	char buf[1]; /* Perhaps I should dynamically alloc this? */
	
	dbg("ftdi_sio_set_termios port %d", port->number);


	/* FIXME -For this cut I don't care if the line is really changing or 
	   not  - so just do the change regardless  - should be able to 
	   compare old_termios and tty->termios */
	/* NOTE These routines can get interrupted by 
	   ftdi_sio_read_bulk_callback  - need to examine what this 
           means - don't see any problems yet */
	
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
			err("CSIZE was set but not CS5-CS8");
		}
	}
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_DATA_REQUEST, 
			    FTDI_SIO_SET_DATA_REQUEST_TYPE,
			    urb_value , 0,
			    buf, 0, 100) < 0) {
		err("FAILED to set databits/stopbits/parity");
	}	   

	/* Now do the baudrate */

	switch(cflag & CBAUD){
	case B0: break; /* Handled below */
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
	if ((cflag & CBAUD) == B0 ) {
		/* Disable flow control */
		if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("error from disable flowcontrol urb");
		}	    
		/* Drop RTS and DTR */
		if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
				    (unsigned)FTDI_SIO_SET_DTR_LOW, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("Error from DTR LOW urb");
		}
		if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
				    (unsigned)FTDI_SIO_SET_RTS_LOW, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("Error from RTS LOW urb");
		}	
		
	} else {
		if (usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_BAUDRATE_REQUEST, 
				    FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE,
				    urb_value, 0, 
				    buf, 0, 100) < 0) {
			err("urb failed to set baurdrate");
		}
	}
	/* Set flow control */
	/* Note device also supports DTR/CD (ugh) and Xon/Xoff in hardware */
	if (cflag & CRTSCTS) {
		dbg("Setting to CRTSCTS flow control");
		if (usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0 , FTDI_SIO_RTS_CTS_HS,
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("urb failed to set to rts/cts flow control");
		}		
		
	} else { 
		/* CHECK Assuming XON/XOFF handled by stack - not by device */
		/* Disable flow control */
		dbg("Turning off hardware flow control");
		if (usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("urb failed to clear flow control");
		}				
		
	}
	return;
} /* ftdi_sio_set_termios */

static int ftdi_sio_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_serial *serial = port->serial;
	__u16 urb_value=0; /* Will hold the new flags */
	char buf[1];
	int  ret, mask;
	
	dbg("ftdi_sio_ioctl - cmd 0x%04x", cmd);

	/* Based on code from acm.c and others */
	switch (cmd) {

	case TIOCMGET:
		dbg("TIOCMGET");
		/* Request the status from the device */
		if ((ret = usb_control_msg(serial->dev, 
					   usb_rcvctrlpipe(serial->dev, 0),
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

	case TIOCMSET: /* Turns on and off the lines as specified by the mask */
		dbg("TIOCMSET");
		if ((ret = get_user(mask, (unsigned long *) arg))) return ret;
		urb_value = ((mask & TIOCM_DTR) ? FTDI_SIO_SET_DTR_HIGH : FTDI_SIO_SET_DTR_LOW);
		if ((ret = usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
				    urb_value , 0,
				    buf, 0, WDR_TIMEOUT)) < 0){
			err("Urb to set DTR failed");
			return(ret);
		}
		urb_value = ((mask & TIOCM_RTS) ? FTDI_SIO_SET_RTS_HIGH : FTDI_SIO_SET_RTS_LOW);
		if ((ret = usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
				    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
				    urb_value , 0,
				    buf, 0, WDR_TIMEOUT)) < 0){
			err("Urb to set RTS failed");
			return(ret);
		}
		break;
					
	case TIOCMBIS: /* turns on (Sets) the lines as specified by the mask */
		dbg("TIOCMBIS");
 	        if ((ret = get_user(mask, (unsigned long *) arg))) return ret;
  	        if (mask & TIOCM_DTR){
			if ((ret = usb_control_msg(serial->dev, 
					    usb_sndctrlpipe(serial->dev, 0),
					    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
					    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
					    FTDI_SIO_SET_DTR_HIGH , 0,
					    buf, 0, WDR_TIMEOUT)) < 0){
				err("Urb to set DTR failed");
				return(ret);
				}
			}
			if (mask & TIOCM_RTS) {
			if ((ret = usb_control_msg(serial->dev, 
					    usb_sndctrlpipe(serial->dev, 0),
					    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
					    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
					    FTDI_SIO_SET_RTS_HIGH , 0,
					    buf, 0, WDR_TIMEOUT)) < 0){
				err("Urb to set RTS failed");
				return(ret);
			}
		}
					break;

	case TIOCMBIC: /* turns off (Clears) the lines as specified by the mask */
		dbg("TIOCMBIC");
 	        if ((ret = get_user(mask, (unsigned long *) arg))) return ret;
  	        if (mask & TIOCM_DTR){
			if ((ret = usb_control_msg(serial->dev, 
					    usb_sndctrlpipe(serial->dev, 0),
					    FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
					    FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
					    FTDI_SIO_SET_DTR_LOW , 0,
					    buf, 0, WDR_TIMEOUT)) < 0){
				err("Urb to unset DTR failed");
				return(ret);
			}
		}	
		if (mask & TIOCM_RTS) {
			if ((ret = usb_control_msg(serial->dev, 
					       usb_sndctrlpipe(serial->dev, 0),
					       FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
					       FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
					    FTDI_SIO_SET_RTS_LOW , 0,
					    buf, 0, WDR_TIMEOUT)) < 0){
				err("Urb to unset RTS failed");
				return(ret);
			}
		}
		break;

		/*
		 * I had originally implemented TCSET{A,S}{,F,W} and
		 * TCGET{A,S} here separately, however when testing I
		 * found that the higher layers actually do the termios
		 * conversions themselves and pass the call onto
		 * ftdi_sio_set_termios. 
		 *
		 */

	default:
	  /* This is not an error - turns out the higher layers will do 
	   *  some ioctls itself (see comment above)
 	   */
		dbg("ftdi_sio ioctl arg not supported - it was 0x%04x",cmd);
		return(-ENOIOCTLCMD);
		break;
	}
	dbg("ftdi_sio_ioctl returning 0");
	return 0;
} /* ftdi_sio_ioctl */

#endif 	/* CONFIG_USB_SERIAL_FTDI_SIO */


