/*
*  Digi AccelePort USB-4 Serial Converter
*
*  Copyright 2000 by Digi International
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  Shamelessly based on Brian Warner's keyspan_pda.c and Greg Kroah-Hartman's
*  usb-serial driver.
*
*  Peter Berger (pberger@brimson.com)
*  Al Borchers (borchers@steinerpoint.com)
*
*  (5/3/2000) pberger and borchers
*  First alpha version of the driver--many known limitations and bugs.
*
*  $Id: digi_acceleport.c,v 1.28 2000/05/04 01:47:08 root Exp root $
*/

#include <linux/config.h>

#ifdef CONFIG_USB_SERIAL_DIGI_ACCELEPORT

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


/* Defines */

/* port buffer length -- must be <= transfer buffer length - 2 */
/* so we can be sure to send the full buffer in one urb */
#define DIGI_PORT_BUF_LEN			16

/* AccelePort USB Defines */

/* ids */
#define DIGI_VENDOR_ID				0x05c5
#define DIGI_ID					0x0004

/* commands */
#define DIGI_CMD_SET_BAUD_RATE			0
#define DIGI_CMD_SET_WORD_SIZE			1
#define DIGI_CMD_SET_PARITY			2
#define DIGI_CMD_SET_STOP_BITS			3
#define DIGI_CMD_SET_INPUT_FLOW_CONTROL		4
#define DIGI_CMD_SET_OUTPUT_FLOW_CONTROL	5
#define DIGI_CMD_SET_DTR_SIGNAL			6
#define DIGI_CMD_SET_RTS_SIGNAL			7
#define DIGI_CMD_RECEIVE_ENABLE			10
#define DIGI_CMD_BREAK_CONTROL			11
#define DIGI_CMD_LOCAL_LOOPBACK			12
#define DIGI_CMD_TRANSMIT_IDLE			13
#define DIGI_CMD_WRITE_UART_REGISTER		15
#define DIGI_CMD_AND_UART_REGISTER		16
#define DIGI_CMD_OR_UART_REGISTER		17
#define DIGI_CMD_SEND_DATA			18

/* baud rates */
#define DIGI_BAUD_50				0
#define DIGI_BAUD_75				1
#define DIGI_BAUD_110				2
#define DIGI_BAUD_150				3
#define DIGI_BAUD_200				4
#define DIGI_BAUD_300				5
#define DIGI_BAUD_600				6
#define DIGI_BAUD_1200				7
#define DIGI_BAUD_1800				8
#define DIGI_BAUD_2400				9
#define DIGI_BAUD_4800				10
#define DIGI_BAUD_7200				11
#define DIGI_BAUD_9600				12
#define DIGI_BAUD_14400				13
#define DIGI_BAUD_19200				14
#define DIGI_BAUD_28800				15
#define DIGI_BAUD_38400				16
#define DIGI_BAUD_57600				17
#define DIGI_BAUD_76800				18
#define DIGI_BAUD_115200			19
#define DIGI_BAUD_153600			20
#define DIGI_BAUD_230400			21
#define DIGI_BAUD_460800			22

/* flow control arguments */
#define DIGI_ENABLE_IXON_IXOFF_FLOW_CONTROL	1
#define DIGI_ENABLE_RTS_CTS_FLOW_CONTROL	2
#define DIGI_ENABLE_DTR_DSR_FLOW_CONTROL	4

/* macros */
#define MAX(a,b)	(((a)>(b))?(a):(b))
#define MIN(a,b)	(((a)<(b))?(a):(b))


/* Structures */

typedef struct digi_private {
	spinlock_t dp_port_lock;
	int dp_buf_len;
	char dp_buf[32];
} digi_private_t;

struct s_digiusb {
	u8 opcode;
	u8 length;
	u8 val;
	u8 pad;
};


/* Local Function Declarations */

static void digi_send_cmd( char *mes, struct usb_serial_port *port, int opcode,
	int length, int val );
static void digi_send_oob( char *mes, int opcode, int linenum, int data1, int data2 );
static void digi_rx_throttle (struct usb_serial_port *port);
static void digi_rx_unthrottle (struct usb_serial_port *port);
static int digi_setbaud( struct usb_serial_port *port, int baud );
static void digi_set_termios( struct usb_serial_port *port, 
	struct termios *old_termios );
static void digi_break_ctl( struct usb_serial_port *port, int break_state );
static int digi_get_modem_info( struct usb_serial *serial,
	unsigned char *value );
static int digi_set_modem_info( struct usb_serial *serial,
	unsigned char value );
static int digi_ioctl( struct usb_serial_port *port, struct file *file,
	unsigned int cmd, unsigned long arg );
static int digi_write( struct usb_serial_port *port, int from_user,
	const unsigned char *buf, int count );
static void digi_write_bulk_callback( struct urb *urb );
static int digi_write_room( struct usb_serial_port *port );
static int digi_chars_in_buffer( struct usb_serial_port *port );
static int digi_open( struct usb_serial_port *port, struct file *filp );
static void digi_close( struct usb_serial_port *port, struct file *filp );
static int digi_startup (struct usb_serial *serial);
static void digi_shutdown( struct usb_serial *serial );
static void digi_read_bulk_callback( struct urb *urb );


/* Statics */

/* device info needed for the Digi serial converter */
static __u16	digi_vendor_id		= DIGI_VENDOR_ID;
static __u16	digi_product_id		= DIGI_ID;

/* out of band port */
static int oob_port_num;					/* index of out-of-band port */
static struct usb_serial_port *oob_port;	/* out-of-band control port */
static int oob_read_started = 0;

/* config lock -- used to protect digi statics and globals, like oob vars */
spinlock_t config_lock;


/* Globals */

struct usb_serial_device_type digi_acceleport_device = {
	name:			"Digi USB",
	idVendor:		&digi_vendor_id,
	idProduct:		&digi_product_id,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	0,
	num_bulk_in:		5,
	num_bulk_out:		5,
	num_ports:		4,
	open:			digi_open,
	close:			digi_close,
	write:			digi_write,
	write_room:		digi_write_room,
	write_bulk_callback:	digi_write_bulk_callback,
	read_bulk_callback:	digi_read_bulk_callback,
	chars_in_buffer:	digi_chars_in_buffer,
	throttle:		digi_rx_throttle,
	unthrottle:		digi_rx_unthrottle,
	ioctl:			digi_ioctl,
	set_termios:		digi_set_termios,
	break_ctl:		digi_break_ctl,
	startup:		digi_startup,
	shutdown:		digi_shutdown,
};


/* Functions */

/* Send message on the out-of-Band endpoint */
static void digi_send_oob( char *mes, int opcode, int linenum, int data1, int data2 )
{
	int ret;
	struct s_digiusb digiusb;
	digi_private_t *priv = (digi_private_t *)(oob_port->private);


dbg( "digi_send_oob: TOP: from '%s', opcode: %d, linenum:%d, data1: %d, data2: %d", mes, opcode, linenum, data1, data2 );

	digiusb.opcode = (u8)opcode;
	digiusb.length = (u8)linenum;
	digiusb.val = (u8)data1;
	digiusb.pad = (u8)data2;

	spin_lock( &priv->dp_port_lock );

	while (oob_port->write_urb->status == -EINPROGRESS) {
dbg( "digi_send_oob: opcode:%d already writing...", opcode );
		spin_unlock( &priv->dp_port_lock );
		interruptible_sleep_on(&oob_port->write_wait);
		if (signal_pending(current)) {
			return;
		}
		spin_lock( &priv->dp_port_lock );
	}

	memcpy( oob_port->write_urb->transfer_buffer, &digiusb, sizeof(digiusb) );
	oob_port->write_urb->transfer_buffer_length = sizeof(digiusb);
	if( (ret=usb_submit_urb(oob_port->write_urb)) != 0 ) {
		dbg(
		"digi_send_oob: usb_submit_urb(write bulk) failed, opcode=%d, ret=%d",
		opcode, ret );
	}

	spin_unlock( &priv->dp_port_lock );

dbg( "digi_send_oob: opcode %d done", opcode );

}


static void digi_send_cmd( char *mes, struct usb_serial_port *port, int opcode,
	int length, int val )
{

	int ret;
	struct s_digiusb digiusb;
	digi_private_t *priv = (digi_private_t *)(port->private);


dbg( "digi_send_cmd: TOP: from '%s', opcode: %d, val: %d", mes, opcode, val );

	digiusb.opcode = (u8)opcode;
	digiusb.length = (u8)length;
	digiusb.val = (u8)val;
	digiusb.pad = 0;

	spin_lock( &priv->dp_port_lock );

	while( port->write_urb->status == -EINPROGRESS ) {
dbg( "digi_send_cmd: opcode=%d already writing...", opcode );
		spin_unlock( &priv->dp_port_lock );
		interruptible_sleep_on( &port->write_wait );
		if( signal_pending(current) ) {
			return;
		}
		spin_lock( &priv->dp_port_lock );
	}

	memcpy( port->write_urb->transfer_buffer, &digiusb, sizeof(digiusb) );
	port->write_urb->transfer_buffer_length = sizeof(digiusb);
	if( (ret=usb_submit_urb(port->write_urb)) != 0 )
		dbg(
		"digi_send_cmd: usb_submit_urb(write bulk) failed, opcode=%d, ret=%d",
		opcode, ret );

dbg( "digi_send_cmd: opcode %d done", opcode );

	spin_unlock( &priv->dp_port_lock );

}


static void digi_rx_throttle( struct usb_serial_port *port )
{

dbg( "digi_rx_throttle: TOP: port=%d", port->number );

	/* stop receiving characters. We just turn off the URB request, and
	   let chars pile up in the device. If we're doing hardware
	   flowcontrol, the device will signal the other end when its buffer
	   fills up. If we're doing XON/XOFF, this would be a good time to
	   send an XOFF, although it might make sense to foist that off
	   upon the device too. */

	// usb_unlink_urb(port->interrupt_in_urb);

}


static void digi_rx_unthrottle( struct usb_serial_port *port )
{

dbg( "digi_rx_unthrottle: TOP: port=%d", port->number );

	/* just restart the receive interrupt URB */
	//if (usb_submit_urb(port->interrupt_in_urb))
	//	dbg( "digi_rx_unthrottle: usb_submit_urb(read urb) failed" );

}


static int digi_setbaud( struct usb_serial_port *port, int baud )
{

	int bindex;


dbg( "digi_setbaud: TOP: port=%d", port->number );

	switch( baud ) {
		case 50: bindex = DIGI_BAUD_50; break;
		case 75: bindex = DIGI_BAUD_75; break;
		case 110: bindex = DIGI_BAUD_110; break;
		case 150: bindex = DIGI_BAUD_150; break;
		case 200: bindex = DIGI_BAUD_200; break;
		case 300: bindex = DIGI_BAUD_300; break;
		case 600: bindex = DIGI_BAUD_600; break;
		case 1200: bindex = DIGI_BAUD_1200; break;
		case 1800: bindex = DIGI_BAUD_1800; break;
		case 2400: bindex = DIGI_BAUD_2400; break;
		case 4800: bindex = DIGI_BAUD_4800; break;
		case 7200: bindex = DIGI_BAUD_7200; break;
		case 9600: bindex = DIGI_BAUD_9600; break;
		case 14400: bindex = DIGI_BAUD_14400; break;
		case 19200: bindex = DIGI_BAUD_19200; break;
		case 28800: bindex = DIGI_BAUD_28800; break;
		case 38400: bindex = DIGI_BAUD_38400; break;
		case 57600: bindex = DIGI_BAUD_57600; break;
		case 76800: bindex = DIGI_BAUD_76800; break;
		case 115200: bindex = DIGI_BAUD_115200; break;
		case 153600: bindex = DIGI_BAUD_153600; break;
		case 230400: bindex = DIGI_BAUD_230400; break;
		case 460800: bindex = DIGI_BAUD_460800; break;
		default:
			dbg( "digi_setbaud: can't handle requested baud rate %d", baud );
			return( -EINVAL );
			break;
	}

	digi_send_cmd( "digi_setbaud:", port, DIGI_CMD_SET_BAUD_RATE, 2, bindex );

	return( 0 );	/* FIX -- send_cmd should return a value??, return it */

}


static void digi_set_termios( struct usb_serial_port *port, 
	struct termios *old_termios )
{

	unsigned int iflag = port->tty->termios->c_iflag;
	unsigned int old_iflag = old_termios->c_iflag;
	unsigned int cflag = port->tty->termios->c_cflag;
	unsigned int old_cflag = old_termios->c_cflag;
	int arg;


dbg( "digi_set_termios: TOP: port=%d, iflag=0x%x, old_iflag=0x%x, cflag=0x%x, old_cflag=0x%x", port->number, iflag, old_iflag, cflag, old_cflag );

	/* set baud rate */
	/* if( (cflag&CBAUD) != (old_cflag&CBAUD) ) */ {
		switch( (cflag&CBAUD) ) {
			case B50: digi_setbaud(port, 50); break;
			case B75: digi_setbaud(port, 75); break;
			case B110: digi_setbaud(port, 110); break;
			case B150: digi_setbaud(port, 150); break;
			case B200: digi_setbaud(port, 200); break;
			case B300: digi_setbaud(port, 300); break;
			case B600: digi_setbaud(port, 600); break;
			case B1200: digi_setbaud(port, 1200); break;
			case B1800: digi_setbaud(port, 1800); break;
			case B2400: digi_setbaud(port, 2400); break;
			case B4800: digi_setbaud(port, 4800); break;
			case B9600: digi_setbaud(port, 9600); break;
			case B19200: digi_setbaud(port, 19200); break;
			case B38400: digi_setbaud(port, 38400); break;
			case B57600: digi_setbaud(port, 57600); break;
			case B115200: digi_setbaud(port, 115200); break;
			default:
				dbg( "digi_set_termios: can't handle baud rate 0x%x",
					(cflag&CBAUD) );
				break;
		}
	}

	/* set input flow control */
	/* if( (iflag&IXOFF) != (old_iflag&IXOFF)
	|| (cflag&CRTSCTS) != (old_cflag&CRTSCTS) ) */ {

		arg = 0;

		if( (iflag&IXOFF) )
			arg |= DIGI_ENABLE_IXON_IXOFF_FLOW_CONTROL;
		if( (cflag&CRTSCTS) )
			arg |= DIGI_ENABLE_RTS_CTS_FLOW_CONTROL;

		digi_send_cmd( "digi_termios: set input flow control:", port,
			DIGI_CMD_SET_INPUT_FLOW_CONTROL, 2, arg );

	}

	/* set output flow control */
	/* if( (iflag&IXON) != (old_iflag&IXON)
	|| (cflag&CRTSCTS) != (old_cflag&CRTSCTS) ) */ {

		arg = 0;

		if( (iflag&IXON) )
			arg |= DIGI_ENABLE_IXON_IXOFF_FLOW_CONTROL;
		if( (cflag&CRTSCTS) )
			arg |= DIGI_ENABLE_RTS_CTS_FLOW_CONTROL;

		digi_send_cmd( "digi_set_termios: set output flow control:", port,
			DIGI_CMD_SET_OUTPUT_FLOW_CONTROL, 2, arg );

	}

}


static void digi_break_ctl( struct usb_serial_port *port, int break_state )
{
dbg( "digi_break_ctl: TOP: port=%d", port->number );
}


/* modem control pins: DTR and RTS are outputs and can be controlled;
   DCD, RI, DSR, CTS are inputs and can be read */

static int digi_get_modem_info( struct usb_serial *serial,
	unsigned char *value )
{
dbg( "digi_get_modem_info: TOP" );
	return( 0 );
}


static int digi_set_modem_info( struct usb_serial *serial,
	unsigned char value )
{
dbg( "digi_set_modem_info: TOP" );
	return( 0 );
}


static int digi_ioctl( struct usb_serial_port *port, struct file *file,
	unsigned int cmd, unsigned long arg )
{
	struct usb_serial *serial = port->serial;
	int rc;
	unsigned int value;
	unsigned char status, mask;

dbg( "digi_ioctl: TOP: port=%d, cmd=0x%x", port->number, cmd );
return( -ENOIOCTLCMD );

	switch (cmd) {
	case TIOCMGET: /* get modem pins state */
		rc = digi_get_modem_info(serial, &status);
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
		rc = digi_set_modem_info(serial, status);
		if (rc < 0)
			return rc;
		return 0;
	case TIOCMBIS: /* set bits in bitmask <arg> */
	case TIOCMBIC: /* clear bits from bitmask <arg> */
		if (copy_from_user(&value, (unsigned int *)arg, sizeof(int)))
			return -EFAULT;
		rc = digi_get_modem_info(serial, &status);
		if (rc < 0)
			return rc;
		mask =
			((value & TIOCM_RTS) ? (1<<2) : 0) |
			((value & TIOCM_DTR) ? (1<<7) : 0);
		if (cmd == TIOCMBIS)
			status |= mask;
		else
			status &= ~mask;
		rc = digi_set_modem_info(serial, status);
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


static int digi_write( struct usb_serial_port *port, int from_user,
	const unsigned char *buf, int count )
{

	int i,ret,data_len,new_len;
	digi_private_t *priv = (digi_private_t *)(port->private);


dbg( "digi_write: TOP: port=%d, count=%d, from_user=%d, in_interrupt=%d",
port->number, count, from_user, in_interrupt() );

	/* be sure only one write proceeds at a time */
	/* there are races on the port private buffer */
	/* and races to check write_urb->status */
	spin_lock( &priv->dp_port_lock );

	/* wait for urb status clear to submit another urb */
	if( port->write_urb->status == -EINPROGRESS ) {

dbg( "digi_write: -EINPROGRESS set" );

		/* buffer the data if possible */
		new_len = MIN( count, DIGI_PORT_BUF_LEN-priv->dp_buf_len );
		memcpy( priv->dp_buf+priv->dp_buf_len, buf, new_len );
		priv->dp_buf_len += new_len;

		/* unlock and return number of bytes buffered */
		spin_unlock( &priv->dp_port_lock );
dbg( "digi_write: buffering, return %d", new_len );
		return( new_len );

	}

	/* allow space for any buffered data and for new data, up to */
	/* transfer buffer size - 2 (for command and length bytes) */
	new_len = MIN( count, port->bulk_out_size-2-priv->dp_buf_len );
	data_len = new_len + priv->dp_buf_len;

dbg( "digi_write: counts: new data %d, buf data %d, total data %d (max %d)", new_len, priv->dp_buf_len, data_len, port->bulk_out_size-2 );

	/* nothing to send */
	if( data_len == 0 ) {
		spin_unlock( &priv->dp_port_lock );
		return( 0 );
	}

	/* set command and length bytes */
	*((u8 *)(port->write_urb->transfer_buffer)) = (u8)DIGI_CMD_SEND_DATA;
	*((u8 *)(port->write_urb->transfer_buffer)+1) = (u8)data_len;

	/* set total transfer buffer length */
	port->write_urb->transfer_buffer_length = data_len+2;

	/* copy in buffered data first */
	memcpy( port->write_urb->transfer_buffer+2, priv->dp_buf,
		priv->dp_buf_len );

	/* copy in new data */
	if( from_user ) {
		copy_from_user( port->write_urb->transfer_buffer+2+priv->dp_buf_len,
			buf, new_len );
	}
	else {
		memcpy( port->write_urb->transfer_buffer+2+priv->dp_buf_len,
			buf, new_len );
	}  

#ifdef DEBUG
	printk( KERN_DEBUG __FILE__ ": digi_write: length=%d, data=",
		port->write_urb->transfer_buffer_length );
	for( i=0; i<port->write_urb->transfer_buffer_length; ++i ) {
		printk( "%.2x ",
			((unsigned char *)port->write_urb->transfer_buffer)[i] );
	}
	printk( "\n" );
#endif

	/* submit urb */
	if( (ret=usb_submit_urb(port->write_urb)) == 0 ) {
		/* submit successful, return length of new data written */
		ret = new_len;
		/* clear buffer */
		priv->dp_buf_len = 0;
	}
	else {
		dbg( "digi_write: usb_submit_urb(write bulk) failed, ret=%d", ret );
		/* no bytes written - should we return the error code or 0? */
		ret = 0;
	}

	/* return length of new data written, or error */
dbg( "digi_write: returning %d", ret );
	spin_unlock( &priv->dp_port_lock );
	return( ret );

} 


static void digi_write_bulk_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty = port->tty;
	digi_private_t *priv = (digi_private_t *)(port->private);
	int ret;


dbg( "digi_write_bulk_callback: TOP: port=%d", port->number );

	/* handle callback on out-of-band port */
	if( port->number == oob_port_num ) {
		dbg( "digi_write_bulk_callback: oob callback" );
		wake_up_interruptible( &port->write_wait );
		return;
	}

	/* sanity checks */
	if( port_paranoia_check( port, "digi_write_bulk_callback" )
	|| serial_paranoia_check( serial, "digi_write_bulk_callback" ) ) {
		return;
	}

	/* try to send any buffered data on this port */
	spin_lock( &priv->dp_port_lock );
	if( port->write_urb->status != -EINPROGRESS && priv->dp_buf_len > 0 ) {

		/* set command and length bytes */
		*((u8 *)(port->write_urb->transfer_buffer))
			= (u8)DIGI_CMD_SEND_DATA;
		*((u8 *)(port->write_urb->transfer_buffer)+1)
			= (u8)priv->dp_buf_len;

		/* set total transfer buffer length */
		port->write_urb->transfer_buffer_length = priv->dp_buf_len+2;

		/* copy in buffered data */
		memcpy( port->write_urb->transfer_buffer+2, priv->dp_buf,
			priv->dp_buf_len );

		/* submit urb */
dbg( "digi_write_bulk_callback: submit urb to write buffer, data len=%d",
priv->dp_buf_len );
		if( (ret=usb_submit_urb(port->write_urb)) == 0 ) {
			/* successful, clear buffer */
			priv->dp_buf_len = 0;
		}
		else {
			dbg( "digi_write_bulk_callback: usb_submit_urb(write bulk) failed, ret=%d", ret );
		}

	}
	spin_unlock( &priv->dp_port_lock );

	/* wake up port processes */
	wake_up_interruptible( &port->write_wait );

	/* wake up line discipline */
	tty = port->tty;
	if( (tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup )
		(tty->ldisc.write_wakeup)(tty);

	/* wake up other tty processes */
	wake_up_interruptible( &tty->write_wait );

}


static int digi_write_room( struct usb_serial_port *port )
{

	int room;
	digi_private_t *priv = (digi_private_t *)(port->private);


dbg( "digi_write_room: TOP: port=%d", port->number );

	spin_lock( &priv->dp_port_lock );

	if( port->write_urb->status == -EINPROGRESS )
		room = 0;
	else
		room = port->bulk_out_size - 2 - priv->dp_buf_len;

	spin_unlock( &priv->dp_port_lock );

dbg( "digi_write_room: return room=%d", room );
	return( room );

}


static int digi_chars_in_buffer( struct usb_serial_port *port )
{

	digi_private_t *priv = (digi_private_t *)(port->private);


dbg( "digi_chars_in_buffer: TOP: port=%d", port->number );

	if( port->write_urb->status == -EINPROGRESS ) {
dbg( "digi_chars_in_buffer: return=%d", port->bulk_out_size );
		return( port->bulk_out_size );
	}
	else {
dbg( "digi_chars_in_buffer: return=%d", priv->dp_buf_len );
		return( priv->dp_buf_len );
	}

}


static int digi_open( struct usb_serial_port *port, struct file *filp )
{

	int ret;
	digi_private_t *priv = (digi_private_t *)(port->private);


dbg( "digi_open: TOP: port %d", port->number );

	/* if port is already open, just return */
	/* be sure exactly one open succeeds */
	spin_lock( &priv->dp_port_lock );
	if( port->active ) {
		return( 0 );
	}
	port->active = 1;
	spin_unlock( &priv->dp_port_lock );
 
	/* start reading from the out-of-band port for the device */
	/* be sure this happens exactly once */
	spin_lock( &config_lock );
	if( !oob_read_started ) {
		if( (ret=usb_submit_urb(oob_port->read_urb)) != 0 ) {
			dbg( "digi_open: usb_submit_urb(read bulk) for oob failed, ret=%d",
				ret );
			spin_unlock( &config_lock );
			return( -ENXIO );
		}
		else {
dbg( "digi_open: usb_submit_urb(read bulk) for oob succeeded" );
			oob_read_started = 1;
		}
	}
	spin_unlock( &config_lock );

	/* initialize port */
dbg( "digi_open: init..." );
	/* set 9600, 8N1, DTR, RTS, RX enable, no input or output flow control */
	digi_setbaud( port, 9600 );
	digi_send_cmd( "digi_open: wordsize", port, DIGI_CMD_SET_WORD_SIZE, 2, 3 );
	digi_send_cmd( "digi_open: parity", port, DIGI_CMD_SET_PARITY, 2, 0 );
	digi_send_cmd( "digi_open: stopbits", port, DIGI_CMD_SET_STOP_BITS, 2, 0 );
	digi_send_cmd( "digi_open: DTR on", port, DIGI_CMD_SET_DTR_SIGNAL, 2, 1 );
	digi_send_cmd( "digi_open: RTS on", port, DIGI_CMD_SET_RTS_SIGNAL, 2, 1 );
	digi_send_cmd( "digi_open: RX enable on", port, DIGI_CMD_RECEIVE_ENABLE, 2,
		1 );
	digi_send_cmd( "digi_open: input flow control off", port,
		DIGI_CMD_SET_INPUT_FLOW_CONTROL, 2, 0 );
	digi_send_cmd( "digi_open: output flow control off", port,
		DIGI_CMD_SET_OUTPUT_FLOW_CONTROL, 2, 0 );

	/* start reading from the device */
	if( (ret=usb_submit_urb(port->read_urb)) != 0 ) {
		dbg( "digi_open: usb_submit_urb(read bulk) failed, ret=%d", ret );
		return( -ENXIO );
	}

dbg( "digi_open: done" );
	return( 0 );

}


static void digi_close( struct usb_serial_port *port, struct file *filp )
{

dbg( "digi_close: TOP: port %d", port->number );
	
	/* Need to change the control lines here */
	/* TODO */
dbg( "digi_close: wanna clear DTR and RTS..." );

//digi_send_cmd( "digi_close DTR off", port, 6, 2, 0);	// clear DTR
//digi_send_cmd( "digi_close RTS off", port, 7, 2, 0);	// clear RTS
//digi_send_cmd( "digi_close RX disable", port, 10, 2, 0);	// Rx Disable

digi_send_oob( "digi_close RTS off", DIGI_CMD_SET_RTS_SIGNAL,
	port->number, 0, 0 );	// clear RTS
digi_send_oob( "digi_close DTR off", DIGI_CMD_SET_DTR_SIGNAL,
	port->number, 0, 0 );	// clear DTR

	while( oob_port->write_urb->status == -EINPROGRESS ) {
dbg ("digi_close: waiting for final writes to complete on oob port %d...", oob_port->number );
		interruptible_sleep_on( &oob_port->write_wait );
		if( signal_pending(current) ) {
			break;
		}
	}
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (port->write_urb);
	usb_unlink_urb (port->read_urb);

	port->active = 0;

}


static int digi_startup (struct usb_serial *serial)
{

	int i;
	digi_private_t *priv;


dbg( "digi_startup: TOP" );

	/* initialize config lock */
	spin_lock_init( &config_lock );

	/* allocate the private data structures for all ports */
	/* number of regular ports + 1 for the out-of-band port */
	for( i=0; i<digi_acceleport_device.num_ports+1; i++ ) {

		/* allocate private structure */
		priv = serial->port[i].private =
			(digi_private_t *)kmalloc( sizeof(struct digi_private),
			GFP_KERNEL );
		if( priv == (digi_private_t *)0 )
			return( 1 );				/* error */

		/* initialize private structure */
		priv->dp_buf_len = 0;
		spin_lock_init( &priv->dp_port_lock );

		/* initialize write wait queue for this port */
		init_waitqueue_head(&serial->port[i].write_wait);

	}

	/* initialize out of band port info */
	oob_port_num = digi_acceleport_device.num_ports;
	oob_port = &serial->port[oob_port_num];
	oob_read_started = 0;

	return( 0 );

}


static void digi_shutdown( struct usb_serial *serial )
{

	int i;


dbg( "digi_shutdown: TOP" );

	/* stop writing and reading from the out-of-band port */
	usb_unlink_urb( oob_port->write_urb );
	usb_unlink_urb( oob_port->read_urb );
	oob_read_started = 0;

	/* free the private data structures for all ports */
	/* number of regular ports + 1 for the out-of-band port */
	for( i=0; i<digi_acceleport_device.num_ports+1; i++ )
		kfree( serial->port[i].private );

}


static void digi_read_bulk_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty = port->tty;
	unsigned char *data = urb->transfer_buffer;
	int ret,i;


dbg( "digi_read_bulk_callback: TOP: port=%d", port->number );

	/* handle oob callback */
	if( port->number == oob_port_num ) {
dbg( "digi_read_bulk_callback: oob_port callback, opcode=%d, line=%d, status=%d, ret=%d", data[0], data[1], data[2], data[3] );
		if( urb->status ) {
			dbg( "digi_read_bulk_callback: nonzero read bulk status on oob: %d",
				urb->status );
		}
		if( (ret=usb_submit_urb(urb)) != 0 ) {
			dbg( "digi_read_bulk_callback: failed resubmitting oob urb, ret=%d",
				ret );
		}
		return;
	}

	/* sanity checks */
	if( port_paranoia_check( port, "digi_read_bulk_callback" )
	|| serial_paranoia_check( serial, "digi_read_bulk_callback" ) ) {
		return;
	}

	/* check status */
	if( urb->status ) {
		dbg( "digi_read_bulk_callback: nonzero read bulk status: %d",
			urb->status );
		return;
	}

#ifdef DEBUG
	if (urb->actual_length) {
		printk( KERN_DEBUG __FILE__ ": digi_read_bulk_callback: length=%d, data=",
			urb->actual_length );
		for( i=0; i<urb->actual_length; ++i ) {
			printk( "%.2x ", data[i] );
		}
		printk( "\n" );
	}
#endif

	/* Digi read packets are:												 */
	/* 0         1      2         3       4    ...     3+length-1 == 2+length*/
	/* opcode, length, status, data[0], data[1]...data[length-2]			 */
	if( urb->actual_length > 3 ) {
		for( i=3; i<2+data[1]; ++i ) {
			 tty_insert_flip_char(tty, data[i], 0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue trying to read */
	if( (ret=usb_submit_urb(urb)) != 0 )
		dbg( "digi_read_bulk_callback: failed resubmitting read urb, ret=%d",
			ret );

}

#endif	/* CONFIG_USB_SERIAL_DIGI_ACCELEPORT */

