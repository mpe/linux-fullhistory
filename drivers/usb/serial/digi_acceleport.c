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
*  (6/27/2000) pberger and borchers
*    -- Zeroed out sync field in the wakeup_task before first use;
*       otherwise the uninitialized value might prevent the task from
*       being scheduled.
*    -- Initialized ret value to 0 in write_bulk_callback, otherwise
*       the uninitialized value could cause a spurious debugging message.
*
*  (6/22/2000) pberger and borchers
*    -- Made cond_wait_... inline--apparently on SPARC the flags arg
*       to spin_lock_irqsave cannot be passed to another function
*       to call spin_unlock_irqrestore.  Thanks to Pauline Middelink.
*    -- In digi_set_modem_signals the inner nested spin locks use just
*       spin_lock() rather than spin_lock_irqsave().  The old code
*       mistakenly left interrupts off.  Thanks to Pauline Middelink.
*    -- copy_from_user (which can sleep) is no longer called while a
*       spinlock is held.  We copy to a local buffer before getting
*       the spinlock--don't like the extra copy but the code is simpler.
*    -- Printk and dbg are no longer called while a spin lock is held.
*
*  (6/4/2000) pberger and borchers
*    -- Replaced separate calls to spin_unlock_irqrestore and
*       interruptible_sleep_on_interruptible with a new function
*       cond_wait_interruptible_timeout_irqrestore.  This eliminates
*       the race condition where the wake up could happen after
*       the unlock and before the sleep.
*    -- Close now waits for output to drain.
*    -- Open waits until any close in progress is finished.
*    -- All out of band responses are now processed, not just the
*       first in a USB packet.
*    -- Fixed a bug that prevented the driver from working when the
*       first Digi port was not the first USB serial port--the driver
*       was mistakenly using the external USB serial port number to
*       try to index into its internal ports.
*    -- Fixed an SMP bug -- write_bulk_callback is called directly from
*       an interrupt, so spin_lock_irqsave/spin_unlock_irqrestore are
*       needed for locks outside write_bulk_callback that are also
*       acquired by write_bulk_callback to prevent deadlocks.
*    -- Fixed support for select() by making digi_chars_in_buffer()
*       return 256 when -EINPROGRESS is set, as the line discipline
*       code in n_tty.c expects.
*    -- Fixed an include file ordering problem that prevented debugging
*       messages from working.
*    -- Fixed an intermittent timeout problem that caused writes to
*       sometimes get stuck on some machines on some kernels.  It turns
*       out in these circumstances write_chan() (in n_tty.c) was
*       asleep waiting for our wakeup call.  Even though we call
*       wake_up_interruptible() in digi_write_bulk_callback(), there is
*       a race condition that could cause the wakeup to fail: if our
*       wake_up_interruptible() call occurs between the time that our
*       driver write routine finishes and write_chan() sets current->state
*       to TASK_INTERRUPTIBLE, the effect of our wakeup setting the state
*       to TASK_RUNNING will be lost and write_chan's subsequent call to
*       schedule() will never return (unless it catches a signal).
*       This race condition occurs because write_bulk_callback() (and thus
*       the wakeup) are called asynchonously from an interrupt, rather than
*       from the scheduler.  We can avoid the race by calling the wakeup
*       from the scheduler queue and that's our fix:  Now, at the end of
*       write_bulk_callback() we queue up a wakeup call on the scheduler
*       task queue.  We still also invoke the wakeup directly since that
*       squeezes a bit more performance out of the driver, and any lost
*       race conditions will get cleaned up at the next scheduler run.
*
*       NOTE:  The problem also goes away if you comment out
*       the two code lines in write_chan() where current->state
*       is set to TASK_RUNNING just before calling driver.write() and to
*       TASK_INTERRUPTIBLE immediately afterwards.  This is why the
*       problem did not show up with the 2.2 kernels -- they do not
*       include that code.
*
*  (5/16/2000) pberger and borchers
*    -- Added timeouts to sleeps, to defend against lost wake ups.
*    -- Handle transition to/from B0 baud rate in digi_set_termios.
*
*  (5/13/2000) pberger and borchers
*    -- All commands now sent on out of band port, using
*       digi_write_oob_command.
*    -- Get modem control signals whenever they change, support TIOCMGET/
*       SET/BIS/BIC ioctls.
*    -- digi_set_termios now supports parity, word size, stop bits, and
*       receive enable.
*    -- Cleaned up open and close, use digi_set_termios and
*       digi_write_oob_command to set port parameters.
*    -- Added digi_startup_device to start read chains on all ports.
*    -- Write buffer is only used when count==1, to be sure put_char can
*       write a char (unless the buffer is full).
*
*  (5/10/2000) pberger and borchers
*    -- Added MOD_INC_USE_COUNT/MOD_DEC_USE_COUNT calls on open/close.
*    -- Fixed problem where the first incoming character is lost on
*       port opens after the first close on that port.  Now we keep
*       the read_urb chain open until shutdown.
*    -- Added more port conditioning calls in digi_open and digi_close.
*    -- Convert port->active to a use count so that we can deal with multiple
*       opens and closes properly.
*    -- Fixed some problems with the locking code.
*
*  (5/3/2000) pberger and borchers
*    -- First alpha version of the driver--many known limitations and bugs.
*
*
*  Locking and SMP
*
*  - Each port, including the out-of-band port, has a lock used to
*    serialize all access to the port's private structure.
*  - The port lock is also used to serialize all writes and access to
*    the port's URB.
*  - The port lock is also used for the port write_wait condition
*    variable.  Holding the port lock will prevent a wake up on the
*    port's write_wait; this can be used with cond_wait_... to be sure
*    the wake up is not lost in a race when dropping the lock and
*    sleeping waiting for the wakeup.
*  - digi_write() does not sleep, since it is sometimes called on
*    interrupt time.
*  - digi_write_bulk_callback() and digi_read_bulk_callback() are
*    called directly from interrupts.  Hence spin_lock_irqsave()
*    and spin_lock_irqrestore() are used in the rest of the code
*    for any locks they acquire.
*  - digi_write_bulk_callback() gets the port lock before waking up
*    processes sleeping on the port write_wait.  It also schedules
*    wake ups so they happen from the scheduler, because the tty
*    system can miss wake ups from interrupts.
*  - All sleeps use a timeout of DIGI_RETRY_TIMEOUT before looping to
*    recheck the condition they are sleeping on.  This is defensive,
*    in case a wake up is lost.
*  - Following Documentation/DocBook/kernel-locking.pdf no spin locks
*    are held when calling copy_to/from_user or printk.
*    
*  $Id: digi_acceleport.c,v 1.63 2000/06/28 18:28:31 root Exp root $
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
#include <linux/tqueue.h>

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
#define DIGI_PORT_BUF_LEN		8

/* retry timeout while sleeping */
#define DIGI_RETRY_TIMEOUT		(HZ/10)

/* timeout while waiting for tty output to drain in close */
/* this delay is used twice in close, so the total delay could */
/* be twice this value */
#define DIGI_CLOSE_TIMEOUT		(5*HZ)


/* AccelePort USB Defines */

/* ids */
#define DIGI_VENDOR_ID			0x05c5
#define DIGI_ID				0x0004

/* commands
 * "INB": can be used on the in-band endpoint
 * "OOB": can be used on the out-of-band endpoint
 */
#define DIGI_CMD_SET_BAUD_RATE			0	/* INB, OOB */
#define DIGI_CMD_SET_WORD_SIZE			1	/* INB, OOB */
#define DIGI_CMD_SET_PARITY			2	/* INB, OOB */
#define DIGI_CMD_SET_STOP_BITS			3	/* INB, OOB */
#define DIGI_CMD_SET_INPUT_FLOW_CONTROL		4	/* INB, OOB */
#define DIGI_CMD_SET_OUTPUT_FLOW_CONTROL	5	/* INB, OOB */
#define DIGI_CMD_SET_DTR_SIGNAL			6	/* INB, OOB */
#define DIGI_CMD_SET_RTS_SIGNAL			7	/* INB, OOB */
#define DIGI_CMD_READ_INPUT_SIGNALS		8	/*      OOB */
#define DIGI_CMD_IFLUSH_FIFO			9	/*      OOB */
#define DIGI_CMD_RECEIVE_ENABLE			10	/* INB, OOB */
#define DIGI_CMD_BREAK_CONTROL			11	/* INB, OOB */
#define DIGI_CMD_LOCAL_LOOPBACK			12	/* INB, OOB */
#define DIGI_CMD_TRANSMIT_IDLE			13	/* INB, OOB */
#define DIGI_CMD_READ_UART_REGISTER		14	/*      OOB */
#define DIGI_CMD_WRITE_UART_REGISTER		15	/* INB, OOB */
#define DIGI_CMD_AND_UART_REGISTER		16	/* INB, OOB */
#define DIGI_CMD_OR_UART_REGISTER		17	/* INB, OOB */
#define DIGI_CMD_SEND_DATA			18	/* INB      */
#define DIGI_CMD_RECEIVE_DATA			19	/* INB      */
#define DIGI_CMD_RECEIVE_DISABLE		20	/* INB      */
#define DIGI_CMD_GET_PORT_TYPE			21	/*      OOB */

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

/* arguments */
#define DIGI_WORD_SIZE_5			0
#define DIGI_WORD_SIZE_6			1
#define DIGI_WORD_SIZE_7			2
#define DIGI_WORD_SIZE_8			3

#define DIGI_PARITY_NONE			0
#define DIGI_PARITY_ODD				1
#define DIGI_PARITY_EVEN			2
#define DIGI_PARITY_MARK			3
#define DIGI_PARITY_SPACE			4

#define DIGI_STOP_BITS_1			0
#define DIGI_STOP_BITS_2			1

#define DIGI_INPUT_FLOW_CONTROL_XON_XOFF	1
#define DIGI_INPUT_FLOW_CONTROL_RTS		2
#define DIGI_INPUT_FLOW_CONTROL_DTR		4

#define DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF	1
#define DIGI_OUTPUT_FLOW_CONTROL_CTS		2
#define DIGI_OUTPUT_FLOW_CONTROL_DSR		4

#define DIGI_DTR_INACTIVE			0
#define DIGI_DTR_ACTIVE				1
#define DIGI_DTR_INPUT_FLOW_CONTROL		2

#define DIGI_RTS_INACTIVE			0
#define DIGI_RTS_ACTIVE				1
#define DIGI_RTS_INPUT_FLOW_CONTROL		2
#define DIGI_RTS_TOGGLE				3

#define DIGI_FLUSH_TX				1
#define DIGI_FLUSH_RX				2
#define DIGI_RESUME_TX				4 /* clears xoff condition */

#define DIGI_TRANSMIT_NOT_IDLE			0
#define DIGI_TRANSMIT_IDLE			1

#define DIGI_DISABLE				0
#define DIGI_ENABLE				1

#define DIGI_DEASSERT				0
#define DIGI_ASSERT				1

/* in band status codes */
#define DIGI_OVERRUN_ERROR			4
#define DIGI_PARITY_ERROR			8
#define DIGI_FRAMING_ERROR			16
#define DIGI_BREAK_ERROR			32

/* out of band status */
#define DIGI_NO_ERROR				0
#define DIGI_BAD_FIRST_PARAMETER		1
#define DIGI_BAD_SECOND_PARAMETER		2
#define DIGI_INVALID_LINE			3
#define DIGI_INVALID_OPCODE			4

/* input signals */
#define DIGI_READ_INPUT_SIGNALS_SLOT		1
#define DIGI_READ_INPUT_SIGNALS_ERR		2
#define DIGI_READ_INPUT_SIGNALS_BUSY		4
#define DIGI_READ_INPUT_SIGNALS_PE		8
#define DIGI_READ_INPUT_SIGNALS_CTS		16
#define DIGI_READ_INPUT_SIGNALS_DSR		32
#define DIGI_READ_INPUT_SIGNALS_RI		64
#define DIGI_READ_INPUT_SIGNALS_DCD		128


/* macros */
#define MAX(a,b)	(((a)>(b))?(a):(b))
#define MIN(a,b)	(((a)<(b))?(a):(b))


/* Structures */

typedef struct digi_private {
	int dp_port_num;
	spinlock_t dp_port_lock;
	int dp_buf_len;
	unsigned char dp_buf[DIGI_PORT_BUF_LEN];
	unsigned int dp_modem_signals;
	int dp_transmit_idle;
	int dp_in_close;
	wait_queue_head_t dp_close_wait;	/* wait queue for close */
	struct tq_struct dp_wakeup_task;
} digi_private_t;


/* Local Function Declarations */

static void digi_wakeup_write( struct usb_serial_port *port );
static void digi_wakeup_write_lock( struct usb_serial_port *port );
static int digi_write_oob_command( unsigned char *buf, int count );
static int digi_write_inb_command( struct usb_serial_port *port,
	unsigned char *buf, int count ) __attribute__((unused));
static int digi_set_modem_signals( struct usb_serial_port *port,
	unsigned int modem_signals );
static int digi_transmit_idle( struct usb_serial_port *port,
	unsigned long timeout );
static void digi_rx_throttle (struct usb_serial_port *port);
static void digi_rx_unthrottle (struct usb_serial_port *port);
static void digi_set_termios( struct usb_serial_port *port, 
	struct termios *old_termios );
static void digi_break_ctl( struct usb_serial_port *port, int break_state );
static int digi_ioctl( struct usb_serial_port *port, struct file *file,
	unsigned int cmd, unsigned long arg );
static int digi_write( struct usb_serial_port *port, int from_user,
	const unsigned char *buf, int count );
static void digi_write_bulk_callback( struct urb *urb );
static int digi_write_room( struct usb_serial_port *port );
static int digi_chars_in_buffer( struct usb_serial_port *port );
static int digi_open( struct usb_serial_port *port, struct file *filp );
static void digi_close( struct usb_serial_port *port, struct file *filp );
static int digi_startup_device( struct usb_serial *serial );
static int digi_startup( struct usb_serial *serial );
static void digi_shutdown( struct usb_serial *serial );
static void digi_read_bulk_callback( struct urb *urb );
static void digi_read_oob_callback( struct urb *urb );


/* Statics */

/* device info needed for the Digi serial converter */
static u16 digi_vendor_id = DIGI_VENDOR_ID;
static u16 digi_product_id = DIGI_ID;

/* out of band port */
static int oob_port_num;			/* index of out-of-band port */
static struct usb_serial_port *oob_port;	/* out-of-band port */
static int device_startup = 0;

spinlock_t startup_lock;			/* used by startup_device */

static wait_queue_head_t modem_change_wait;
static wait_queue_head_t transmit_idle_wait;


/* Globals */

struct usb_serial_device_type digi_acceleport_device = {
	name:				"Digi USB",
	idVendor:			&digi_vendor_id,
	idProduct:			&digi_product_id,
	needs_interrupt_in:		DONT_CARE,
	needs_bulk_in:			MUST_HAVE,
	needs_bulk_out:			MUST_HAVE,
	num_interrupt_in:		0,
	num_bulk_in:			5,
	num_bulk_out:			5,
	num_ports:			4,
	open:				digi_open,
	close:				digi_close,
	write:				digi_write,
	write_room:			digi_write_room,
	write_bulk_callback: 		digi_write_bulk_callback,
	read_bulk_callback:		digi_read_bulk_callback,
	chars_in_buffer:		digi_chars_in_buffer,
	throttle:			digi_rx_throttle,
	unthrottle:			digi_rx_unthrottle,
	ioctl:				digi_ioctl,
	set_termios:			digi_set_termios,
	break_ctl:			digi_break_ctl,
	startup:			digi_startup,
	shutdown:			digi_shutdown,
};


/* Functions */

/*
*  Cond Wait Interruptible Timeout Irqrestore
*
*  Do spin_unlock_irqrestore and interruptible_sleep_on_timeout
*  so that wake ups are not lost if they occur between the unlock
*  and the sleep.  In other words, spin_lock_irqrestore and
*  interruptible_sleep_on_timeout are "atomic" with respect to
*  wake ups.  This is used to implement condition variables.
*/

static inline long cond_wait_interruptible_timeout_irqrestore(
	wait_queue_head_t *q, long timeout,
	spinlock_t *lock, unsigned long flags )
{

	wait_queue_t wait;


	init_waitqueue_entry( &wait, current );

	set_current_state( TASK_INTERRUPTIBLE );

	add_wait_queue( q, &wait );

	spin_unlock_irqrestore( lock, flags );

	timeout = schedule_timeout(timeout);

	set_current_state( TASK_RUNNING );

	remove_wait_queue( q, &wait );

	return( timeout );

}


/*
*  Digi Wakeup Write
*
*  Wake up port, line discipline, and tty processes sleeping
*  on writes.
*/

static void digi_wakeup_write_lock( struct usb_serial_port *port )
{

	unsigned long flags;
	digi_private_t *priv = (digi_private_t *)(port->private);


	spin_lock_irqsave( &priv->dp_port_lock, flags );
	digi_wakeup_write( port );
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

}

static void digi_wakeup_write( struct usb_serial_port *port )
{

	struct tty_struct *tty = port->tty;


	/* wake up port processes */
	wake_up_interruptible( &port->write_wait );

	/* wake up line discipline */
	if( (tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
	&& tty->ldisc.write_wakeup )
		(tty->ldisc.write_wakeup)(tty);

	/* wake up other tty processes */
	wake_up_interruptible( &tty->write_wait );
	/* For 2.2.16 backport -- wake_up_interruptible( &tty->poll_wait ); */

}


/*
*  Digi Write OOB Command
*
*  Write commands on the out of band port.  Commands are 4
*  bytes each, multiple commands can be sent at once, and
*  no command will be split across USB packets.  Returns 0
*  if successful, -EINTR if interrupted while sleeping, or
*  a negative error returned by usb_submit_urb.
*/

static int digi_write_oob_command( unsigned char *buf, int count )
{

	int ret = 0;
	int len;
	digi_private_t *oob_priv = (digi_private_t *)(oob_port->private);
	unsigned long flags = 0;


dbg( "digi_write_oob_command: TOP: port=%d, count=%d", oob_port_num, count );

	spin_lock_irqsave( &oob_priv->dp_port_lock, flags );

	while( count > 0 ) {

		while( oob_port->write_urb->status == -EINPROGRESS ) {
			cond_wait_interruptible_timeout_irqrestore(
				&oob_port->write_wait, DIGI_RETRY_TIMEOUT,
				&oob_priv->dp_port_lock, flags );
			if( signal_pending(current) ) {
				return( -EINTR );
			}
			spin_lock_irqsave( &oob_priv->dp_port_lock, flags );
		}

		/* len must be a multiple of 4, so commands are not split */
		len = MIN( count, oob_port->bulk_out_size );
		if( len > 4 )
			len &= ~3;

		memcpy( oob_port->write_urb->transfer_buffer, buf, len );
		oob_port->write_urb->transfer_buffer_length = len;

		if( (ret=usb_submit_urb(oob_port->write_urb)) == 0 ) {
			count -= len;
			buf += len;
		}

	}

	spin_unlock_irqrestore( &oob_priv->dp_port_lock, flags );

	if( ret ) {
		dbg( "digi_write_oob_command: usb_submit_urb failed, ret=%d",
		ret );
	}

	return( ret );

}


/*
*  Digi Write In Band Command
*
*  Write commands on the given port.  Commands are 4
*  bytes each, multiple commands can be sent at once, and
*  no command will be split across USB packets.  Returns 0
*  if successful, or a negative error returned by digi_write.
*/

static int digi_write_inb_command( struct usb_serial_port *port,
	unsigned char *buf, int count )
{

	int ret = 0;
	int len;
	digi_private_t *priv = (digi_private_t *)(port->private);
	unsigned char *data = port->write_urb->transfer_buffer;
	unsigned long flags = 0;


dbg( "digi_write_inb_command: TOP: port=%d, count=%d", priv->dp_port_num,
count );

	spin_lock_irqsave( &priv->dp_port_lock, flags );

	while( count > 0 ) {

		while( port->write_urb->status == -EINPROGRESS ) {
			cond_wait_interruptible_timeout_irqrestore(
				&port->write_wait, DIGI_RETRY_TIMEOUT,
				&priv->dp_port_lock, flags );
			if( signal_pending(current) ) {
				return( -EINTR );
			}
			spin_lock_irqsave( &priv->dp_port_lock, flags );
		}

		/* len must be a multiple of 4 and small enough to */
		/* guarantee the write will send buffered data first, */
		/* so commands are in order with data and not split */
		len = MIN( count, port->bulk_out_size-2-priv->dp_buf_len );
		if( len > 4 )
			len &= ~3;

		/* write any buffered data first */
		if( priv->dp_buf_len > 0 ) {
			data[0] = DIGI_CMD_SEND_DATA;
			data[1] = priv->dp_buf_len;
			memcpy( data+2, priv->dp_buf, priv->dp_buf_len );
			memcpy( data+2+priv->dp_buf_len, buf, len );
			port->write_urb->transfer_buffer_length
				= priv->dp_buf_len+2+len;
		} else {
			memcpy( data, buf, len );
			port->write_urb->transfer_buffer_length = len;
		}

		if( (ret=usb_submit_urb(port->write_urb)) == 0 ) {
			priv->dp_buf_len = 0;
			count -= len;
			buf += len;
		}

	}

	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	if( ret ) {
		dbg( "digi_write_inb_command: usb_submit_urb failed, ret=%d",
		ret );
	}

	return( ret );

}


/*
*  Digi Set Modem Signals
*
*  Sets or clears DTR and RTS on the port, according to the
*  modem_signals argument.  Use TIOCM_DTR and TIOCM_RTS flags
*  for the modem_signals argument.  Returns 0 if successful,
*  -EINTR if interrupted while sleeping, or a non-zero error
*  returned by usb_submit_urb.
*/

static int digi_set_modem_signals( struct usb_serial_port *port,
	unsigned int modem_signals )
{

	int ret;
	unsigned char *data = oob_port->write_urb->transfer_buffer;
	digi_private_t *port_priv = (digi_private_t *)(port->private);
	digi_private_t *oob_priv = (digi_private_t *)(oob_port->private);
	unsigned long flags = 0;


dbg( "digi_set_modem_signals: TOP: port=%d, modem_signals=0x%x",
port_priv->dp_port_num, modem_signals );

	spin_lock_irqsave( &oob_priv->dp_port_lock, flags );
	spin_lock( &port_priv->dp_port_lock );

	while( oob_port->write_urb->status == -EINPROGRESS ) {
		spin_unlock( &port_priv->dp_port_lock );
		cond_wait_interruptible_timeout_irqrestore(
			&oob_port->write_wait, DIGI_RETRY_TIMEOUT,
			&oob_priv->dp_port_lock, flags );
		if( signal_pending(current) ) {
			return( -EINTR );
		}
		spin_lock_irqsave( &oob_priv->dp_port_lock, flags );
		spin_lock( &port_priv->dp_port_lock );
	}

	data[0] = DIGI_CMD_SET_DTR_SIGNAL;
	data[1] = port_priv->dp_port_num;
	data[2] = (modem_signals&TIOCM_DTR) ?
		DIGI_DTR_ACTIVE : DIGI_DTR_INACTIVE;
	data[3] = 0;

	data[4] = DIGI_CMD_SET_RTS_SIGNAL;
	data[5] = port_priv->dp_port_num;
	data[6] = (modem_signals&TIOCM_RTS) ?
		DIGI_RTS_ACTIVE : DIGI_RTS_INACTIVE;
	data[7] = 0;

	oob_port->write_urb->transfer_buffer_length = 8;

	if( (ret=usb_submit_urb(oob_port->write_urb)) == 0 ) {
		port_priv->dp_modem_signals =
			(port_priv->dp_modem_signals&~(TIOCM_DTR|TIOCM_RTS))
			| (modem_signals&(TIOCM_DTR|TIOCM_RTS));
	}

	spin_unlock( &port_priv->dp_port_lock );
	spin_unlock_irqrestore( &oob_priv->dp_port_lock, flags );

	if( ret ) {
		dbg( "digi_set_modem_signals: usb_submit_urb failed, ret=%d",
		ret );
	}

	return( ret );

}


/*
*  Digi Transmit Idle
*
*  Digi transmit idle waits, up to timeout ticks, for the transmitter
*  to go idle.  It returns 0 if successful or a negative error.
*
*  There are race conditions here if more than one process is calling
*  digi_transmit_idle on the same port at the same time.  However, this
*  is only called from close, and only one process can be in close on a
*  port at a time, so its ok.
*/

static int digi_transmit_idle( struct usb_serial_port *port,
	unsigned long timeout )
{

	int ret;
	unsigned char buf[2];
	digi_private_t *priv = (digi_private_t *)(port->private);
	unsigned long flags = 0;


	spin_lock_irqsave( &priv->dp_port_lock, flags );
	priv->dp_transmit_idle = 0;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	buf[0] = DIGI_CMD_TRANSMIT_IDLE;
	buf[1] = 0;

	if( (ret=digi_write_inb_command( port, buf, 2 )) != 0 )
		return( ret );

	timeout += jiffies;

	spin_lock_irqsave( &priv->dp_port_lock, flags );

	while( jiffies < timeout && !priv->dp_transmit_idle ) {
		cond_wait_interruptible_timeout_irqrestore(
			&transmit_idle_wait, DIGI_RETRY_TIMEOUT,
			&priv->dp_port_lock, flags );
		if( signal_pending(current) ) {
			return( -EINTR );
		}
		spin_lock_irqsave( &priv->dp_port_lock, flags );
	}

	priv->dp_transmit_idle = 0;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	return( 0 );

}


static void digi_rx_throttle( struct usb_serial_port *port )
{

#ifdef DEBUG
	digi_private_t *priv = (digi_private_t *)(port->private);
#endif


dbg( "digi_rx_throttle: TOP: port=%d", priv->dp_port_num );

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

#ifdef DEBUG
	digi_private_t *priv = (digi_private_t *)(port->private);
#endif


dbg( "digi_rx_unthrottle: TOP: port=%d", priv->dp_port_num );

	/* just restart the receive interrupt URB */
	//if (usb_submit_urb(port->interrupt_in_urb))
	//	dbg( "digi_rx_unthrottle: usb_submit_urb failed" );

}


static void digi_set_termios( struct usb_serial_port *port, 
	struct termios *old_termios )
{

	digi_private_t *priv = (digi_private_t *)(port->private);
	unsigned int iflag = port->tty->termios->c_iflag;
	unsigned int cflag = port->tty->termios->c_cflag;
	unsigned int old_iflag = old_termios->c_iflag;
	unsigned int old_cflag = old_termios->c_cflag;
	unsigned char buf[32];
	int arg,ret;
	int i = 0;


dbg( "digi_set_termios: TOP: port=%d, iflag=0x%x, old_iflag=0x%x, cflag=0x%x, old_cflag=0x%x", priv->dp_port_num, iflag, old_iflag, cflag, old_cflag );

	/* set baud rate */
	if( (cflag&CBAUD) != (old_cflag&CBAUD) ) {

		arg = -1;

		/* reassert DTR and (maybe) RTS on transition from B0 */
		if( (old_cflag&CBAUD) == B0 ) {
			/* don't set RTS if using hardware flow control */
			/* and throttling input -- not implemented yet */
			digi_set_modem_signals( port, TIOCM_DTR|TIOCM_RTS );
		}

		switch( (cflag&CBAUD) ) {
			/* drop DTR and RTS on transition to B0 */
		case B0: digi_set_modem_signals( port, 0 ); break;
		case B50: arg = DIGI_BAUD_50; break;
		case B75: arg = DIGI_BAUD_75; break;
		case B110: arg = DIGI_BAUD_110; break;
		case B150: arg = DIGI_BAUD_150; break;
		case B200: arg = DIGI_BAUD_200; break;
		case B300: arg = DIGI_BAUD_300; break;
		case B600: arg = DIGI_BAUD_600; break;
		case B1200: arg = DIGI_BAUD_1200; break;
		case B1800: arg = DIGI_BAUD_1800; break;
		case B2400: arg = DIGI_BAUD_2400; break;
		case B4800: arg = DIGI_BAUD_4800; break;
		case B9600: arg = DIGI_BAUD_9600; break;
		case B19200: arg = DIGI_BAUD_19200; break;
		case B38400: arg = DIGI_BAUD_38400; break;
		case B57600: arg = DIGI_BAUD_57600; break;
		case B115200: arg = DIGI_BAUD_115200; break;
		case B230400: arg = DIGI_BAUD_230400; break;
		case B460800: arg = DIGI_BAUD_460800; break;
		default:
			dbg( "digi_set_termios: can't handle baud rate 0x%x",
				(cflag&CBAUD) );
			break;
		}

		if( arg != -1 ) {
			buf[i++] = DIGI_CMD_SET_BAUD_RATE;
			buf[i++] = priv->dp_port_num;
			buf[i++] = arg;
			buf[i++] = 0;
		}

	}

	/* set parity */
	if( (cflag&(PARENB|PARODD)) != (old_cflag&(PARENB|PARODD)) ) {

		if( (cflag&PARENB) ) {
			if( (cflag&PARODD) )
				arg = DIGI_PARITY_ODD;
			else
				arg = DIGI_PARITY_EVEN;
		} else {
			arg = DIGI_PARITY_NONE;
		}

		buf[i++] = DIGI_CMD_SET_PARITY;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	/* set word size */
	if( (cflag&CSIZE) != (old_cflag&CSIZE) ) {

		arg = -1;

		switch( (cflag&CSIZE) ) {
		case CS5: arg = DIGI_WORD_SIZE_5; break;
		case CS6: arg = DIGI_WORD_SIZE_6; break;
		case CS7: arg = DIGI_WORD_SIZE_7; break;
		case CS8: arg = DIGI_WORD_SIZE_8; break;
		default:
			dbg( "digi_set_termios: can't handle word size %d",
				(cflag&CSIZE) );
			break;
		}

		if( arg != -1 ) {
			buf[i++] = DIGI_CMD_SET_WORD_SIZE;
			buf[i++] = priv->dp_port_num;
			buf[i++] = arg;
			buf[i++] = 0;
		}

	}

	/* set stop bits */
	if( (cflag&CSTOPB) != (old_cflag&CSTOPB) ) {

		if( (cflag&CSTOPB) )
			arg = DIGI_STOP_BITS_2;
		else
			arg = DIGI_STOP_BITS_1;

		buf[i++] = DIGI_CMD_SET_STOP_BITS;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	/* set input flow control */
	if( (iflag&IXOFF) != (old_iflag&IXOFF)
	|| (cflag&CRTSCTS) != (old_cflag&CRTSCTS) ) {

		arg = 0;

		if( (iflag&IXOFF) )
			arg |= DIGI_INPUT_FLOW_CONTROL_XON_XOFF;
		else
			arg &= ~DIGI_INPUT_FLOW_CONTROL_XON_XOFF;

		if( (cflag&CRTSCTS) )
			arg |= DIGI_INPUT_FLOW_CONTROL_RTS;
		else
			arg &= ~DIGI_INPUT_FLOW_CONTROL_RTS;

		buf[i++] = DIGI_CMD_SET_INPUT_FLOW_CONTROL;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	/* set output flow control */
	/*if( (iflag&IXON) != (old_iflag&IXON)
	|| (cflag&CRTSCTS) != (old_cflag&CRTSCTS) )*/ {

		arg = 0;

		if( (iflag&IXON) )
			arg |= DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF;
		else
			arg &= ~DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF;

		if( (cflag&CRTSCTS) )
			arg |= DIGI_OUTPUT_FLOW_CONTROL_CTS;
		else
			arg &= ~DIGI_OUTPUT_FLOW_CONTROL_CTS;

		buf[i++] = DIGI_CMD_SET_OUTPUT_FLOW_CONTROL;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	/* set receive enable/disable */
	if( (cflag&CREAD) != (old_cflag&CREAD) ) {

		if( (cflag&CREAD) )
			arg = DIGI_ENABLE;
		else
			arg = DIGI_DISABLE;

		buf[i++] = DIGI_CMD_RECEIVE_ENABLE;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	if( (ret=digi_write_oob_command( buf, i )) != 0 )
		dbg( "digi_set_termios: write oob failed, ret=%d", ret );

}


static void digi_break_ctl( struct usb_serial_port *port, int break_state )
{

#ifdef DEBUG
	digi_private_t *priv = (digi_private_t *)(port->private);
#endif


dbg( "digi_break_ctl: TOP: port=%d", priv->dp_port_num );

}


static int digi_ioctl( struct usb_serial_port *port, struct file *file,
	unsigned int cmd, unsigned long arg )
{

	digi_private_t *priv = (digi_private_t *)(port->private);
	unsigned int val;
	unsigned long flags = 0;


dbg( "digi_ioctl: TOP: port=%d, cmd=0x%x", priv->dp_port_num, cmd );

	switch (cmd) {

	case TIOCMGET:
		spin_lock_irqsave( &priv->dp_port_lock, flags );
		val = priv->dp_modem_signals;
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		if( copy_to_user((unsigned int *)arg, &val, sizeof(int)) )
			return( -EFAULT );
		return( 0 );

	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
		if( copy_from_user(&val, (unsigned int *)arg, sizeof(int)) )
			return( -EFAULT );
		spin_lock_irqsave( &priv->dp_port_lock, flags );
		if( cmd == TIOCMBIS )
			val = priv->dp_modem_signals | val;
		else if( cmd == TIOCMBIC )
			val = priv->dp_modem_signals & ~val;
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		return( digi_set_modem_signals( port, val ) );

	case TIOCMIWAIT:
		/* wait for any of the 4 modem inputs (DCD,RI,DSR,CTS)*/
		/* TODO */
		return( 0 );

	case TIOCGICOUNT:
		/* return count of modemline transitions */
		/* TODO */
		return 0;

	}

	return( -ENOIOCTLCMD );

}


static int digi_write( struct usb_serial_port *port, int from_user,
	const unsigned char *buf, int count )
{

	int ret,data_len,new_len;
	digi_private_t *priv = (digi_private_t *)(port->private);
	unsigned char *data = port->write_urb->transfer_buffer;
	unsigned char user_buf[64];	/* 64 bytes is max USB bulk packet */
	unsigned long flags = 0;


dbg( "digi_write: TOP: port=%d, count=%d, from_user=%d, in_interrupt=%d",
priv->dp_port_num, count, from_user, in_interrupt() );

	/* copy user data (which can sleep) before getting spin lock */
	count = MIN( 64, MIN( count, port->bulk_out_size-2 ) );
	if( from_user && copy_from_user( user_buf, buf, count ) ) {
		return( -EFAULT );
	}

	/* be sure only one write proceeds at a time */
	/* there are races on the port private buffer */
	/* and races to check write_urb->status */
	spin_lock_irqsave( &priv->dp_port_lock, flags );

	/* wait for urb status clear to submit another urb */
	if( port->write_urb->status == -EINPROGRESS ) {

		/* buffer data if count is 1 (probably put_char) if possible */
		if( count == 1 ) {
			new_len = MIN( count,
				DIGI_PORT_BUF_LEN-priv->dp_buf_len );
			memcpy( priv->dp_buf+priv->dp_buf_len, buf, new_len );
			priv->dp_buf_len += new_len;
		} else {
			new_len = 0;
		}

		spin_unlock_irqrestore( &priv->dp_port_lock, flags );

		return( new_len );

	}

	/* allow space for any buffered data and for new data, up to */
	/* transfer buffer size - 2 (for command and length bytes) */
	new_len = MIN( count, port->bulk_out_size-2-priv->dp_buf_len );
	data_len = new_len + priv->dp_buf_len;

	if( data_len == 0 ) {
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		return( 0 );
	}

	port->write_urb->transfer_buffer_length = data_len+2;

	*data++ = DIGI_CMD_SEND_DATA;
	*data++ = data_len;

	/* copy in buffered data first */
	memcpy( data, priv->dp_buf, priv->dp_buf_len );
	data += priv->dp_buf_len;

	/* copy in new data */
	memcpy( data, from_user ? user_buf : buf, new_len );

	if( (ret=usb_submit_urb(port->write_urb)) == 0 ) {
		ret = new_len;
		priv->dp_buf_len = 0;
	}

	/* return length of new data written, or error */
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );
	if( ret < 0 ) {
		dbg( "digi_write: usb_submit_urb failed, ret=%d", ret );
	}
dbg( "digi_write: returning %d", ret );
	return( ret );

} 


static void digi_write_bulk_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
	digi_private_t *priv = (digi_private_t *)(port->private);
	int ret = 0;


dbg( "digi_write_bulk_callback: TOP: port=%d", priv->dp_port_num );

	/* handle callback on out-of-band port */
	if( priv->dp_port_num == oob_port_num ) {
		dbg( "digi_write_bulk_callback: oob callback" );
		spin_lock( &priv->dp_port_lock );
		wake_up_interruptible( &port->write_wait );
		spin_unlock( &priv->dp_port_lock );
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

		*((unsigned char *)(port->write_urb->transfer_buffer))
			= (unsigned char)DIGI_CMD_SEND_DATA;
		*((unsigned char *)(port->write_urb->transfer_buffer)+1)
			= (unsigned char)priv->dp_buf_len;

		port->write_urb->transfer_buffer_length = priv->dp_buf_len+2;

		memcpy( port->write_urb->transfer_buffer+2, priv->dp_buf,
			priv->dp_buf_len );

		if( (ret=usb_submit_urb(port->write_urb)) == 0 ) {
			priv->dp_buf_len = 0;
		}

	}

	/* wake up processes sleeping on writes immediately */
	digi_wakeup_write( port );

	/* also queue up a wakeup at scheduler time, in case we */
	/* lost the race in write_chan(). */
	queue_task( &priv->dp_wakeup_task, &tq_scheduler );

	spin_unlock( &priv->dp_port_lock );

	if( ret ) {
		dbg( "digi_write_bulk_callback: usb_submit_urb failed, ret=%d",
		ret );
	}

}


static int digi_write_room( struct usb_serial_port *port )
{

	int room;
	digi_private_t *priv = (digi_private_t *)(port->private);
	unsigned long flags = 0;


	spin_lock_irqsave( &priv->dp_port_lock, flags );

	if( port->write_urb->status == -EINPROGRESS )
		room = 0;
	else
		room = port->bulk_out_size - 2 - priv->dp_buf_len;

	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

dbg( "digi_write_room: port=%d, room=%d", priv->dp_port_num, room );
	return( room );

}


static int digi_chars_in_buffer( struct usb_serial_port *port )
{

	digi_private_t *priv = (digi_private_t *)(port->private);


	if( port->write_urb->status == -EINPROGRESS ) {
dbg( "digi_chars_in_buffer: port=%d, chars=%d", priv->dp_port_num, port->bulk_out_size - 2 );
		/* return( port->bulk_out_size - 2 ); */
		return( 256 );
	} else {
dbg( "digi_chars_in_buffer: port=%d, chars=%d", priv->dp_port_num, priv->dp_buf_len );
		return( priv->dp_buf_len );
	}

}


static int digi_open( struct usb_serial_port *port, struct file *filp )
{

	int ret;
	unsigned char buf[32];
	digi_private_t *priv = (digi_private_t *)(port->private);
	struct termios not_termios;
	unsigned long flags = 0;


dbg( "digi_open: TOP: port %d, active:%d", priv->dp_port_num, port->active );

	/* be sure the device is started up */
	if( digi_startup_device( port->serial ) != 0 )
		return( -ENXIO );

	/* if port is already open, just return */
	/* be sure exactly one open proceeds */
	spin_lock_irqsave( &priv->dp_port_lock, flags );
	if( port->active >= 1 && !priv->dp_in_close ) {
		++port->active;
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		MOD_INC_USE_COUNT;
		return( 0 );
	}

	/* don't wait on a close in progress for non-blocking opens */
	if( priv->dp_in_close && (filp->f_flags&(O_NDELAY|O_NONBLOCK)) == 0 ) {
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		return( -EAGAIN );
	}

	/* prevent other opens from proceeding, before giving up lock */
	++port->active;

	/* wait for close to finish */
	while( priv->dp_in_close ) {
		cond_wait_interruptible_timeout_irqrestore(
			&priv->dp_close_wait, DIGI_RETRY_TIMEOUT,
			&priv->dp_port_lock, flags );
		if( signal_pending(current) ) {
			--port->active;
			return( -EINTR );
		}
		spin_lock_irqsave( &priv->dp_port_lock, flags );
	}

	spin_unlock_irqrestore( &priv->dp_port_lock, flags );
	MOD_INC_USE_COUNT;
 
	/* read modem signals automatically whenever they change */
	buf[0] = DIGI_CMD_READ_INPUT_SIGNALS;
	buf[1] = priv->dp_port_num;
	buf[2] = DIGI_ENABLE;
	buf[3] = 0;

	/* flush fifos */
	buf[4] = DIGI_CMD_IFLUSH_FIFO;
	buf[5] = priv->dp_port_num;
	buf[6] = DIGI_FLUSH_TX | DIGI_FLUSH_RX;
	buf[7] = 0;

	if( (ret=digi_write_oob_command( buf, 8 )) != 0 )
		dbg( "digi_open: write oob failed, ret=%d", ret );

	/* set termios settings */
	not_termios.c_cflag = ~port->tty->termios->c_cflag;
	not_termios.c_iflag = ~port->tty->termios->c_iflag;
	digi_set_termios( port, &not_termios );

	/* set DTR and RTS */
	digi_set_modem_signals( port, TIOCM_DTR|TIOCM_RTS );

	return( 0 );

}


static void digi_close( struct usb_serial_port *port, struct file *filp )
{

	int ret;
	unsigned char buf[32];
	struct tty_struct *tty = port->tty;
	digi_private_t *priv = (digi_private_t *)(port->private);
	unsigned long flags = 0;


dbg( "digi_close: TOP: port %d, active:%d", priv->dp_port_num, port->active );


	/* do cleanup only after final close on this port */
	spin_lock_irqsave( &priv->dp_port_lock, flags );
	if( port->active > 1 ) {
		--port->active;
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		MOD_DEC_USE_COUNT;
		return;
	} else if( port->active <= 0 ) {
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		return;
	}
	priv->dp_in_close = 1;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	/* tell line discipline to process only XON/XOFF */
        tty->closing = 1;

	/* wait for output to drain */
	if( (filp->f_flags&(O_NDELAY|O_NONBLOCK)) == 0 ) {
		tty_wait_until_sent( tty, DIGI_CLOSE_TIMEOUT );
	}

	/* flush driver and line discipline buffers */
	if( tty->driver.flush_buffer )
		tty->driver.flush_buffer( tty );
	if( tty->ldisc.flush_buffer )
		tty->ldisc.flush_buffer( tty );

	/* wait for transmit idle */
	if( (filp->f_flags&(O_NDELAY|O_NONBLOCK)) == 0 ) {
		digi_transmit_idle( port, DIGI_CLOSE_TIMEOUT );
	}

	/* drop DTR and RTS */
	digi_set_modem_signals( port, 0 );

	/* disable input flow control */
	buf[0] = DIGI_CMD_SET_INPUT_FLOW_CONTROL;
	buf[1] = priv->dp_port_num;
	buf[2] = DIGI_DISABLE;
	buf[3] = 0;

	/* disable output flow control */
	buf[4] = DIGI_CMD_SET_OUTPUT_FLOW_CONTROL;
	buf[5] = priv->dp_port_num;
	buf[6] = DIGI_DISABLE;
	buf[7] = 0;

	/* disable reading modem signals automatically */
	buf[8] = DIGI_CMD_READ_INPUT_SIGNALS;
	buf[9] = priv->dp_port_num;
	buf[10] = DIGI_DISABLE;
	buf[11] = 0;

	/* flush fifos */
	buf[12] = DIGI_CMD_IFLUSH_FIFO;
	buf[13] = priv->dp_port_num;
	buf[14] = DIGI_FLUSH_TX | DIGI_FLUSH_RX;
	buf[15] = 0;

	/* disable receive */
	buf[16] = DIGI_CMD_RECEIVE_ENABLE;
	buf[17] = priv->dp_port_num;
	buf[18] = DIGI_DISABLE;
	buf[19] = 0;

	if( (ret=digi_write_oob_command( buf, 20 )) != 0 )
		dbg( "digi_close: write oob failed, ret=%d", ret );

	/* wait for final commands on oob port to complete */
	while( oob_port->write_urb->status == -EINPROGRESS ) {
		interruptible_sleep_on_timeout( &oob_port->write_wait,
			DIGI_RETRY_TIMEOUT );
		if( signal_pending(current) ) {
			break;
		}
	}

	/* shutdown any outstanding bulk writes */
	usb_unlink_urb (port->write_urb);

	tty->closing = 0;

	spin_lock_irqsave( &priv->dp_port_lock, flags );
	--port->active;
	priv->dp_in_close = 0;
	wake_up_interruptible( &priv->dp_close_wait );
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	MOD_DEC_USE_COUNT;

dbg( "digi_close: done" );
}


/*
*  Digi Startup Device
*
*  Starts reads on all ports.  Must be called AFTER startup, with
*  urbs initialized.  Returns 0 if successful, non-zero error otherwise.
*/

static int digi_startup_device( struct usb_serial *serial )
{

	int i,ret = 0;


	/* be sure this happens exactly once */
	spin_lock( &startup_lock );
	if( device_startup ) {
		spin_unlock( &startup_lock );
		return( 0 );
	}
	device_startup = 1;
	spin_unlock( &startup_lock );

	/* start reading from each bulk in endpoint for the device */
	for( i=0; i<digi_acceleport_device.num_ports+1; i++ ) {

		if( (ret=usb_submit_urb(serial->port[i].read_urb)) != 0 ) {
			dbg( "digi_startup_device: usb_submit_urb failed, port=%d, ret=%d",
				i, ret );
			break;
		}

	}

	return( ret );

}


static int digi_startup( struct usb_serial *serial )
{

	int i;
	digi_private_t *priv;


dbg( "digi_startup: TOP" );

	spin_lock_init( &startup_lock );
	init_waitqueue_head( &modem_change_wait );
	init_waitqueue_head( &transmit_idle_wait );

	/* allocate the private data structures for all ports */
	/* number of regular ports + 1 for the out-of-band port */
	for( i=0; i<digi_acceleport_device.num_ports+1; i++ ) {

		serial->port[i].active = 0;

		/* allocate private structure */
		priv = serial->port[i].private =
			(digi_private_t *)kmalloc( sizeof(digi_private_t),
			GFP_KERNEL );
		if( priv == (digi_private_t *)0 )
			return( 1 );			/* error */

		/* initialize private structure */
		priv->dp_port_num = i;
		priv->dp_buf_len = 0;
		priv->dp_modem_signals = 0;
		priv->dp_transmit_idle = 0;
		priv->dp_in_close = 0;
		init_waitqueue_head( &priv->dp_close_wait );
		priv->dp_wakeup_task.next = NULL;
		priv->dp_wakeup_task.sync = 0;
		priv->dp_wakeup_task.routine = (void *)digi_wakeup_write_lock;
		priv->dp_wakeup_task.data = (void *)(&serial->port[i]);
		spin_lock_init( &priv->dp_port_lock );

		/* initialize write wait queue for this port */
		init_waitqueue_head(&serial->port[i].write_wait);

	}

	/* initialize out of band port info */
	oob_port_num = digi_acceleport_device.num_ports;
	oob_port = &serial->port[oob_port_num];
	device_startup = 0;

	return( 0 );

}


static void digi_shutdown( struct usb_serial *serial )
{

	int i;


dbg( "digi_shutdown: TOP" );

	/* stop reads and writes on all ports */
	for( i=0; i<digi_acceleport_device.num_ports+1; i++ ) {
		usb_unlink_urb (serial->port[i].read_urb);
		usb_unlink_urb (serial->port[i].write_urb);
	}

	device_startup = 0;

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
	digi_private_t *priv = (digi_private_t *)(port->private);
	int opcode = ((unsigned char *)urb->transfer_buffer)[0];
	int len = ((unsigned char *)urb->transfer_buffer)[1];
	int status = ((unsigned char *)urb->transfer_buffer)[2];
	unsigned char *data = ((unsigned char *)urb->transfer_buffer)+3;
	int ret,i;


dbg( "digi_read_bulk_callback: TOP: port=%d", priv->dp_port_num );

	/* handle oob callback */
	if( priv->dp_port_num == oob_port_num ) {
		digi_read_oob_callback( urb );
		return;
	}

	/* sanity checks */
	if( port_paranoia_check( port, "digi_read_bulk_callback" )
	|| serial_paranoia_check( serial, "digi_read_bulk_callback" ) ) {
		goto resubmit;
	}

	if( urb->status ) {
		dbg( "digi_read_bulk_callback: nonzero read bulk status: %d",
			urb->status );
		if( urb->status == -ENOENT )
			return;
		goto resubmit;
	}

	if( urb->actual_length != len + 2 )
     		err( KERN_INFO "digi_read_bulk_callback: INCOMPLETE PACKET, port=%d, opcode=%d, len=%d, actual_length=%d, status=%d", priv->dp_port_num, opcode, len, urb->actual_length, status );

	/* receive data */
	if( opcode == DIGI_CMD_RECEIVE_DATA && urb->actual_length > 3 ) {
		len = MIN( len, urb->actual_length-3 );
		for( i=0; i<len; ++i ) {
			 tty_insert_flip_char(tty, data[i], 0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* continue read */
resubmit:
	if( (ret=usb_submit_urb(urb)) != 0 ) {
		dbg( "digi_read_bulk_callback: failed resubmitting urb, ret=%d",
			ret );
	}

}


static void digi_read_oob_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
	digi_private_t *priv;
	int opcode, line, status, val;
	int i,ret;


dbg( "digi_read_oob_callback: len=%d", urb->actual_length );

	if( urb->status ) {
		dbg( "digi_read_oob_callback: nonzero read bulk status on oob: %d",
			urb->status );
		if( urb->status == -ENOENT )
			return;
		goto resubmit;
	}

	for( i=0; i<urb->actual_length-3; ) {

		opcode = ((unsigned char *)urb->transfer_buffer)[i++];
		line = ((unsigned char *)urb->transfer_buffer)[i++];
		status = ((unsigned char *)urb->transfer_buffer)[i++];
		val = ((unsigned char *)urb->transfer_buffer)[i++];

dbg( "digi_read_oob_callback: opcode=%d, line=%d, status=%d, val=%d", opcode, line, status, val );

		if( status != 0 )
			continue;

		priv = serial->port[line].private;

		if( opcode == DIGI_CMD_READ_INPUT_SIGNALS ) {

			spin_lock( &priv->dp_port_lock );

			/* convert from digi flags to termiox flags */
			if( val & DIGI_READ_INPUT_SIGNALS_CTS )
				priv->dp_modem_signals |= TIOCM_CTS;
			else
				priv->dp_modem_signals &= ~TIOCM_CTS;
			if( val & DIGI_READ_INPUT_SIGNALS_DSR )
				priv->dp_modem_signals |= TIOCM_DSR;
			else
				priv->dp_modem_signals &= ~TIOCM_DSR;
			if( val & DIGI_READ_INPUT_SIGNALS_RI )
				priv->dp_modem_signals |= TIOCM_RI;
			else
				priv->dp_modem_signals &= ~TIOCM_RI;
			if( val & DIGI_READ_INPUT_SIGNALS_DCD )
				priv->dp_modem_signals |= TIOCM_CD;
			else
				priv->dp_modem_signals &= ~TIOCM_CD;

			wake_up_interruptible( &modem_change_wait );
			spin_unlock( &priv->dp_port_lock );

		} else if( opcode == DIGI_CMD_TRANSMIT_IDLE ) {

			spin_lock( &priv->dp_port_lock );
			priv->dp_transmit_idle = 1;
			wake_up_interruptible( &transmit_idle_wait );
			spin_unlock( &priv->dp_port_lock );

		}

	}


resubmit:
	if( (ret=usb_submit_urb(urb)) != 0 ) {
		dbg( "digi_read_oob_callback: failed resubmitting oob urb, ret=%d",
		ret );
	}

}
