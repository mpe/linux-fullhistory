/*
 * USB Keyspan PDA Converter driver
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
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
 * 
 */


#include <linux/config.h>

#ifdef CONFIG_USB_SERIAL_KEYSPAN_PDA

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

struct ezusb_hex_record {
	__u16 address;
	__u8 data_size;
	__u8 data[16];
};

#include "keyspan_pda_fw.h"

#include "usb-serial.h"

struct keyspan_pda_private {
	int			tx_room;
	int			tx_throttled;
};

#define KEYSPAN_VENDOR_ID		0x06cd
#define KEYSPAN_PDA_FAKE_ID		0x0103
#define KEYSPAN_PDA_ID			0x0104 /* no clue */

/* All of the device info needed for the Keyspan PDA serial converter */
static __u16	keyspan_vendor_id		= KEYSPAN_VENDOR_ID;
static __u16	keyspan_pda_fake_product_id	= KEYSPAN_PDA_FAKE_ID;
static __u16	keyspan_pda_product_id		= KEYSPAN_PDA_ID;



static void keyspan_pda_rx_interrupt (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int i;
	struct keyspan_pda_private *priv;
	priv = (struct keyspan_pda_private *)(port->private);

	/* the urb might have been killed. */
	if (urb->status)
		return;
	
	if (port_paranoia_check (port, "keyspan_pda_rx_interrupt")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "keyspan_pda_rx_interrupt")) {
		return;
	}
	
 	/* see if the message is data or a status interrupt */
	switch (data[0]) {
	case 0:
		/* rest of message is rx data */
		if (urb->actual_length) {
			tty = serial->port[0].tty;
			for (i = 1; i < urb->actual_length ; ++i) {
				tty_insert_flip_char(tty, data[i], 0);
			}
			tty_flip_buffer_push(tty);
		}
		break;
	case 1:
		/* status interrupt */
		dbg(" rx int, d1=%d, d2=%d", data[1], data[2]);
		switch (data[1]) {
		case 1: /* modemline change */
			break;
		case 2: /* tx unthrottle interrupt */
			tty = serial->port[0].tty;
			priv->tx_throttled = 0;
			wake_up(&port->write_wait); /* wake up writer */
			wake_up(&tty->write_wait); /* them too */
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	/* INT urbs are automatically re-submitted */
}


static void keyspan_pda_rx_throttle (struct usb_serial_port *port)
{
	/* stop receiving characters. We just turn off the URB request, and
	   let chars pile up in the device. If we're doing hardware
	   flowcontrol, the device will signal the other end when its buffer
	   fills up. If we're doing XON/XOFF, this would be a good time to
	   send an XOFF, although it might make sense to foist that off
	   upon the device too. */

	dbg("keyspan_pda_rx_throttle port %d", port->number);
	usb_unlink_urb(port->interrupt_in_urb);
}


static void keyspan_pda_rx_unthrottle (struct usb_serial_port *port)
{
	/* just restart the receive interrupt URB */
	dbg("keyspan_pda_rx_unthrottle port %d", port->number);
	if (usb_submit_urb(port->interrupt_in_urb))
		dbg(" usb_submit_urb(read urb) failed");
	return;
}


static int keyspan_pda_setbaud (struct usb_serial *serial, int baud)
{
	int rc;
	int bindex;

	switch(baud) {
		case 110: bindex = 0; break;
		case 300: bindex = 1; break;
		case 1200: bindex = 2; break;
		case 2400: bindex = 3; break;
		case 4800: bindex = 4; break;
		case 9600: bindex = 5; break;
		case 19200: bindex = 6; break;
		case 38400: bindex = 7; break;
		case 57600: bindex = 8; break;
		case 115200: bindex = 9; break;
		default: return -EINVAL;
	}

	/* rather than figure out how to sleep while waiting for this
	   to complete, I just use the "legacy" API. */
	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     0, /* set baud */
			     USB_TYPE_VENDOR 
			     | USB_RECIP_INTERFACE
			     | USB_DIR_OUT, /* type */
			     bindex, /* value */
			     0, /* index */
			     NULL, /* &data */
			     0, /* size */
			     2*HZ); /* timeout */
	return(rc);
}


static void keyspan_pda_break_ctl (struct usb_serial_port *port, int break_state)
{
	struct usb_serial *serial = port->serial;
	int value;
	if (break_state == -1)
		value = 1; /* start break */
	else
		value = 0; /* clear break */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			4, /* set break */
			USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_DIR_OUT,
			value, 0, NULL, 0, 2*HZ);
	/* there is something funky about this.. the TCSBRK that 'cu' performs
	   ought to translate into a break_ctl(-1),break_ctl(0) pair HZ/4
	   seconds apart, but it feels like the break sent isn't as long as it
	   is on /dev/ttyS0 */
}


static void keyspan_pda_set_termios (struct usb_serial_port *port, 
				     struct termios *old_termios)
{
	struct usb_serial *serial = port->serial;
	unsigned int cflag = port->tty->termios->c_cflag;

	/* cflag specifies lots of stuff: number of stop bits, parity, number
	   of data bits, baud. What can the device actually handle?:
	   CSTOPB (1 stop bit or 2)
	   PARENB (parity)
	   CSIZE (5bit .. 8bit)
	   There is minimal hw support for parity (a PSW bit seems to hold the
	   parity of whatever is in the accumulator). The UART either deals
	   with 10 bits (start, 8 data, stop) or 11 bits (start, 8 data,
	   1 special, stop). So, with firmware changes, we could do:
	   8N1: 10 bit
	   8N2: 11 bit, extra bit always (mark?)
	   8[EOMS]1: 11 bit, extra bit is parity
	   7[EOMS]1: 10 bit, b0/b7 is parity
	   7[EOMS]2: 11 bit, b0/b7 is parity, extra bit always (mark?)

	   HW flow control is dictated by the tty->termios->c_cflags & CRTSCTS
	   bit.

	   For now, just do baud. */

	switch (cflag & CBAUD) {
		/* we could support more values here, just need to calculate
		   the necessary divisors in the firmware. <asm/termbits.h>
		   has the Bnnn constants. */
		case B110: keyspan_pda_setbaud(serial, 110); break;
		case B300: keyspan_pda_setbaud(serial, 300); break;
		case B1200: keyspan_pda_setbaud(serial, 1200); break;
		case B2400: keyspan_pda_setbaud(serial, 2400); break;
		case B4800: keyspan_pda_setbaud(serial, 4800); break;
		case B9600: keyspan_pda_setbaud(serial, 9600); break;
		case B19200: keyspan_pda_setbaud(serial, 19200); break;
		case B38400: keyspan_pda_setbaud(serial, 38400); break;
		case B57600: keyspan_pda_setbaud(serial, 57600); break;
		case B115200: keyspan_pda_setbaud(serial, 115200); break;
		default: dbg("can't handle requested baud rate"); break;
	}
}


/* modem control pins: DTR and RTS are outputs and can be controlled.
   DCD, RI, DSR, CTS are inputs and can be read. All outputs can also be
   read. The byte passed is: DTR(b7) DCD RI DSR CTS RTS(b2) unused unused */

static int keyspan_pda_get_modem_info(struct usb_serial *serial,
				      unsigned char *value)
{
	int rc;
	unsigned char data;
	rc = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			     3, /* get pins */
			     USB_TYPE_VENDOR|USB_RECIP_INTERFACE|USB_DIR_IN,
			     0, 0, &data, 1, 2*HZ);
	if (rc > 0)
		*value = data;
	return rc;
}


static int keyspan_pda_set_modem_info(struct usb_serial *serial,
				      unsigned char value)
{
	int rc;
	rc = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			     3, /* set pins */
			     USB_TYPE_VENDOR|USB_RECIP_INTERFACE|USB_DIR_OUT,
			     value, 0, NULL, 0, 2*HZ);
	return rc;
}


static int keyspan_pda_ioctl(struct usb_serial_port *port, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	struct usb_serial *serial = port->serial;
	int rc;
	unsigned int value;
	unsigned char status, mask;

	switch (cmd) {
	case TIOCMGET: /* get modem pins state */
		rc = keyspan_pda_get_modem_info(serial, &status);
		if (rc < 0)
			return rc;
		value =
			((status & (1<<7)) ? TIOCM_DTR : 0) |
			((status & (1<<6)) ? TIOCM_CAR : 0) |
			((status & (1<<5)) ? TIOCM_RNG : 0) |
			((status & (1<<4)) ? TIOCM_DSR : 0) |
			((status & (1<<3)) ? TIOCM_CTS : 0) |
			((status & (1<<2)) ? TIOCM_RTS : 0);
		if (copy_to_user((unsigned int *)arg, &value, sizeof(int)))
			return -EFAULT;
		return 0;
	case TIOCMSET: /* set a state as returned by MGET */
		if (copy_from_user(&value, (unsigned int *)arg, sizeof(int)))
			return -EFAULT;
		status =
			((value & TIOCM_DTR) ? (1<<7) : 0) |
			((value & TIOCM_CAR) ? (1<<6) : 0) |
			((value & TIOCM_RNG) ? (1<<5) : 0) |
			((value & TIOCM_DSR) ? (1<<4) : 0) |
			((value & TIOCM_CTS) ? (1<<3) : 0) |
			((value & TIOCM_RTS) ? (1<<2) : 0);
		rc = keyspan_pda_set_modem_info(serial, status);
		if (rc < 0)
			return rc;
		return 0;
	case TIOCMBIS: /* set bits in bitmask <arg> */
	case TIOCMBIC: /* clear bits from bitmask <arg> */
		if (copy_from_user(&value, (unsigned int *)arg, sizeof(int)))
			return -EFAULT;
		rc = keyspan_pda_get_modem_info(serial, &status);
		if (rc < 0)
			return rc;
		mask =
			((value & TIOCM_RTS) ? (1<<2) : 0) |
			((value & TIOCM_DTR) ? (1<<7) : 0);
		if (cmd == TIOCMBIS)
			status |= mask;
		else
			status &= ~mask;
		rc = keyspan_pda_set_modem_info(serial, status);
		if (rc < 0)
			return rc;
		return 0;
	case TIOCMIWAIT:
		/* wait for any of the 4 modem inputs (DCD,RI,DSR,CTS)*/
		/* TODO */
	case TIOCGICOUNT:
		/* return count of modemline transitions */
		return 0; /* TODO */
	}
	
	return -ENOIOCTLCMD;
}

static int keyspan_pda_write(struct usb_serial_port *port, int from_user, 
			     const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	int request_unthrottle = 0;
	int rc = 0;
	struct keyspan_pda_private *priv;
	DECLARE_WAITQUEUE(wait, current);

	priv = (struct keyspan_pda_private *)(port->private);
	/* guess how much room is left in the device's ring buffer, and if we
	   want to send more than that, check first, updating our notion of
	   what is left. If our write will result in no room left, ask the
	   device to give us an interrupt when the room available rises above
	   a threshold, and hold off all writers (eventually, those using
	   select() or poll() too) until we receive that unthrottle interrupt.
	   Block if we can't write anything at all, otherwise write as much as
	   we can. */
	dbg("keyspan_pda_write(%d)",count);
	if (count == 0) {
		dbg(" write request of 0 bytes");
		return (0);
	}

	/* we might block because of:
	   the TX urb is in-flight (wait until it completes)
	   the device is full (wait until it says there is room)
	*/
	while (port->write_urb->status == -EINPROGRESS) {
		if (0 /* file->f_flags & O_NONBLOCK */) {
			rc = -EAGAIN;
			goto err;
		}
		interruptible_sleep_on(&port->write_wait);
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			goto err;
		}
	}

	/* at this point the URB is in our control, nobody else can submit it
	   again (the only sudden transition was the one from EINPROGRESS to
	   finished) */

	/* the next potential block is that our TX process might be throttled.
	   The transition from throttled->not happens because of an Rx
	   interrupt, and the wake_up occurs during the same interrupt, so we
	   have to be careful to avoid a race that would cause us to sleep
	   forever. */

	add_wait_queue(&port->write_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	while (priv->tx_throttled) {
		/* device can't accomodate any more characters. Sleep until it
		   can. Woken up by an Rx interrupt message, which clears
		   tx_throttled first. */
		dbg(" tx_throttled, going to sleep");
		if (signal_pending(current)) {
			current->state = TASK_RUNNING;
			remove_wait_queue(&port->write_wait, &wait);
			dbg(" woke up because of signal");
			rc = -ERESTARTSYS;
			goto err;
		}
		schedule();
		dbg(" woke up");
	}
	remove_wait_queue(&port->write_wait, &wait);
	set_current_state(TASK_RUNNING);

	count = (count > port->bulk_out_size) ? port->bulk_out_size : count;
	if (count > priv->tx_room) {
		unsigned char room;
		/* Looks like we might overrun the Tx buffer. Ask the device
		   how much room it really has */
		rc = usb_control_msg(serial->dev, 
				     usb_rcvctrlpipe(serial->dev, 0),
				     6, /* write_room */
				     USB_TYPE_VENDOR | USB_RECIP_INTERFACE
				     | USB_DIR_IN,
				     0, /* value: 0 means "remaining room" */
				     0, /* index */
				     &room,
				     1,
				     2*HZ);
		if (rc < 0) {
			dbg(" roomquery failed");
			return rc; /* failed */
		}
		if (rc == 0) {
			dbg(" roomquery returned 0 bytes");
			return -EIO; /* device didn't return any data */
		}
		dbg(" roomquery says %d", room);
		priv->tx_room = room;
		if (count > priv->tx_room) {
			/* we're about to completely fill the Tx buffer, so
			   we'll be throttled afterwards. */
			count = priv->tx_room;
			request_unthrottle = 1;
		}
	}
	priv->tx_room -= count;

	if (count) {
		/* now transfer data */
		if (from_user) {
			copy_from_user(port->write_urb->transfer_buffer, buf, count);
		}
		else {
			memcpy (port->write_urb->transfer_buffer, buf, count);
		}  
		/* send the data out the bulk port */
		port->write_urb->transfer_buffer_length = count;
		
		if (usb_submit_urb(port->write_urb))
			dbg(" usb_submit_urb(write bulk) failed");
	}
	else {
		/* There wasn't any room left, so we are throttled until
		   the buffer empties a bit */
		request_unthrottle = 1;
	}

	if (request_unthrottle) {
		dbg(" request_unthrottle");
		/* ask the device to tell us when the tx buffer becomes
		   sufficiently empty */
		priv->tx_throttled = 1; /* block writers */
		rc = usb_control_msg(serial->dev, 
				     usb_sndctrlpipe(serial->dev, 0),
				     7, /* request_unthrottle */
				     USB_TYPE_VENDOR | USB_RECIP_INTERFACE
				     | USB_DIR_OUT,
				     16, /* value: threshold */
				     0, /* index */
				     NULL,
				     0,
				     2*HZ);
	}

	return (count);
 err:
	return (rc);
}


static void keyspan_pda_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty;

	if (port_paranoia_check (port, "keyspan_pda_rx_interrupt")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "keyspan_pda_rx_interrupt")) {
		return;
	}
	
	wake_up_interruptible(&port->write_wait);

	tty = port->tty;
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && 
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);
}


static int keyspan_pda_write_room (struct usb_serial_port *port)
{
	struct keyspan_pda_private *priv;
	priv = (struct keyspan_pda_private *)(port->private);

	/* used by n_tty.c for processing of tabs and such. Giving it our
	   conservative guess is probably good enough, but needs testing by
	   running a console through the device. */

	return (priv->tx_room);
}


static int keyspan_pda_chars_in_buffer (struct usb_serial_port *port)
{
	struct keyspan_pda_private *priv;
	priv = (struct keyspan_pda_private *)(port->private);
	
	/* when throttled, return at least WAKEUP_CHARS to tell select() (via
	   n_tty.c:normal_poll() ) that we're not writeable. */
	if (priv->tx_throttled)
		return 256;
	return 0;
}


static int keyspan_pda_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	unsigned char room;
	int rc;
	struct keyspan_pda_private *priv;
	priv = (struct keyspan_pda_private *)(port->private);

	if (port->active) {
		return -EINVAL;
	}
	port->active = 1;
 
	/* find out how much room is in the Tx ring */
	rc = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			     6, /* write_room */
			     USB_TYPE_VENDOR | USB_RECIP_INTERFACE
			     | USB_DIR_IN,
			     0, /* value */
			     0, /* index */
			     &room,
			     1,
			     2*HZ);
	if (rc < 0) {
		dbg(" roomquery failed");
		return rc; /* failed */
	}
	if (rc == 0) {
		dbg(" roomquery returned 0 bytes");
		return -EIO; /* device didn't return any data */
	}
	priv->tx_room = room;
	priv->tx_throttled = room ? 0 : 1;

	/* the normal serial device seems to always turn on DTR and RTS here,
	   so do the same */
	if (port->tty->termios->c_cflag & CBAUD)
		keyspan_pda_set_modem_info(serial, (1<<7) | (1<<2) );
	else
		keyspan_pda_set_modem_info(serial, 0);

	/*Start reading from the device*/
	if (usb_submit_urb(port->interrupt_in_urb))
		dbg(" usb_submit_urb(read int) failed");

	return (0);
}


static void keyspan_pda_close(struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;

	/* the normal serial device seems to always shut off DTR and RTS now */
	if (port->tty->termios->c_cflag & HUPCL)
		keyspan_pda_set_modem_info(serial, 0);

	/* shutdown our bulk reads and writes */
	usb_unlink_urb (port->write_urb);
	usb_unlink_urb (port->interrupt_in_urb);
	port->active = 0;
}


/* download the firmware to a "fake" device (pre-renumeration) */
static int keyspan_pda_fake_startup (struct usb_serial *serial)
{
	int response;
	const struct ezusb_hex_record *record;

	/* download the firmware here ... */
	response = ezusb_set_reset(serial, 1);

	record = &keyspan_pda_firmware[0];
	while(record->address != 0xffff) {
		response = ezusb_writememory(serial, record->address,
					     (unsigned char *)record->data,
					     record->data_size, 0xa0);
		if (response < 0) {
			err("ezusb_writememory failed for Keyspan PDA "
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

static int keyspan_pda_startup (struct usb_serial *serial)
{
	/* allocate the private data structures for all ports. Well, for all
	   one ports. */

	serial->port[0].private = kmalloc(sizeof(struct keyspan_pda_private),
					   GFP_KERNEL);
	if (!serial->port[0].private)
		return (1); /* error */
	init_waitqueue_head(&serial->port[0].write_wait);
	return (0);
}

static void keyspan_pda_shutdown (struct usb_serial *serial)
{
	kfree(serial->port[0].private);
}

struct usb_serial_device_type keyspan_pda_fake_device = {
	name:			"Keyspan PDA - (prerenumeration)",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_pda_fake_product_id,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		keyspan_pda_fake_startup,
};

struct usb_serial_device_type keyspan_pda_device = {
	name:			"Keyspan PDA",
	idVendor:		&keyspan_vendor_id,
	idProduct:		&keyspan_pda_product_id,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		0,
	num_bulk_out:		1,
	num_ports:		1,
	open:			keyspan_pda_open,
	close:			keyspan_pda_close,
	write:			keyspan_pda_write,
	write_room:		keyspan_pda_write_room,
	write_bulk_callback: 	keyspan_pda_write_bulk_callback,
	read_int_callback:	keyspan_pda_rx_interrupt,
	chars_in_buffer:	keyspan_pda_chars_in_buffer,
	throttle:		keyspan_pda_rx_throttle,
	unthrottle:		keyspan_pda_rx_unthrottle,
	ioctl:			keyspan_pda_ioctl,
	set_termios:		keyspan_pda_set_termios,
	break_ctl:		keyspan_pda_break_ctl,
	startup:		keyspan_pda_startup,
	shutdown:		keyspan_pda_shutdown,
};

#endif	/* CONFIG_USB_SERIAL_KEYSPAN_PDA */
