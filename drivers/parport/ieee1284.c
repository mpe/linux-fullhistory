/* $Id: parport_ieee1284.c,v 1.4 1997/10/19 21:37:21 philip Exp $
 * IEEE-1284 implementation for parport.
 *
 * Authors: Phil Blundell <Philip.Blundell@pobox.com>
 *          Carsten Gross <carsten@sol.wohnheim.uni-ulm.de>
 *	    Jose Renau <renau@acm.org>
 *          Tim Waugh <tim@cyberelk.demon.co.uk> (largely rewritten)
 *
 * This file is responsible for IEEE 1284 negotiation, and for handing
 * read/write requests to low-level drivers.
 */

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>

#undef DEBUG /* undef me for production */

#ifdef CONFIG_LP_CONSOLE
#undef DEBUG /* Don't want a garbled console */
#endif

#ifdef DEBUG
#define DPRINTK(stuff...) printk (stuff)
#else
#define DPRINTK(stuff...)
#endif

/* Make parport_wait_peripheral wake up.
 * It will be useful to call this from an interrupt handler. */
void parport_ieee1284_wakeup (struct parport *port)
{
	up (&port->physport->ieee1284.irq);
}

static struct parport *port_from_cookie[PARPORT_MAX];
static void timeout_waiting_on_port (unsigned long cookie)
{
	parport_ieee1284_wakeup (port_from_cookie[cookie % PARPORT_MAX]);
}

/* Wait for a parport_ieee1284_wakeup.
 * 0:      success
 * <0:     error (exit as soon as possible)
 * >0:     timed out
 */
int parport_wait_event (struct parport *port, signed long timeout)
{
	int ret;
	struct timer_list timer;

	if (!port->physport->cad->timeout)
		/* Zero timeout is special, and we can't down() the
		   semaphore. */
		return 1;

	init_timer (&timer);
	timer.expires = jiffies + timeout;
	timer.function = timeout_waiting_on_port;
	port_from_cookie[port->number % PARPORT_MAX] = port;
	timer.data = port->number;

	add_timer (&timer);
	ret = down_interruptible (&port->physport->ieee1284.irq);
	if (!del_timer (&timer) && !ret)
		/* Timed out. */
		ret = 1;

	return ret;
}

/* Wait for Status line(s) to change in 35 ms - see IEEE1284-1994 page 24 to
 * 25 for this. After this time we can create a timeout because the
 * peripheral doesn't conform to IEEE1284.  We want to save CPU time: we are
 * waiting a maximum time of 500 us busy (this is for speed).  If there is
 * not the right answer in this time, we call schedule and other processes
 * are able to eat the time up to 40ms.
 */ 

int parport_wait_peripheral(struct parport *port,
			    unsigned char mask, 
			    unsigned char result)
{
	int counter;
	long deadline;
	unsigned char status;

	counter = port->physport->spintime; /* usecs of fast polling */
	if (!port->physport->cad->timeout)
		/* A zero timeout is "special": busy wait for the
		   entire 35ms. */
		counter = 35000;

	/* Fast polling.
	 *
	 * This should be adjustable.
	 * How about making a note (in the device structure) of how long
	 * it takes, so we know for next time?
	 */
	for (counter /= 5; counter > 0; counter--) {
		status = parport_read_status (port);
		if ((status & mask) == result)
			return 0;
		if (signal_pending (current))
			return -EINTR;
		if (current->need_resched)
			break;
		udelay(5);
	}

	if (!port->physport->cad->timeout)
		/* We may be in an interrupt handler, so we can't poll
		 * slowly anyway. */
		return 1;

	/* 40ms of slow polling. */
	deadline = jiffies + (HZ + 24) / 25;
	while (time_before (jiffies, deadline)) {
		int ret;

		if (signal_pending (current))
			return -EINTR;

		/* Wait for 10ms (or until an interrupt occurs if
		 * the handler is set) */
		if ((ret = parport_wait_event (port, (HZ + 99) / 100)) < 0)
			return ret;

		status = parport_read_status (port);
		if ((status & mask) == result)
			return 0;

		if (!ret) {
			/* parport_wait_event didn't time out, but the
			 * peripheral wasn't actually ready either.
			 * Wait for another 10ms. */
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout ((HZ+ 99) / 100);
		}
	}

	return 1;
}

#ifdef CONFIG_PARPORT_1284
/* Terminate a negotiated mode. */
static void parport_ieee1284_terminate (struct parport *port)
{
	port = port->physport;

	port->ieee1284.phase = IEEE1284_PH_TERMINATE;

	/* EPP terminates differently. */
	switch (port->ieee1284.mode) {
	case IEEE1284_MODE_EPP:
	case IEEE1284_MODE_EPPSL:
	case IEEE1284_MODE_EPPSWE:
		/* Terminate from EPP mode. */

		/* Event 68: Set nInit low */
		parport_frob_control (port, PARPORT_CONTROL_INIT, 0);
		udelay (50);

		/* Event 69: Set nInit high, nSelectIn low */
		parport_frob_control (port,
				      PARPORT_CONTROL_SELECT
				      | PARPORT_CONTROL_INIT,
				      PARPORT_CONTROL_SELECT
				      | PARPORT_CONTROL_INIT);
		break;
		
	default:
		/* Terminate from all other modes. */

		/* Event 22: Set nSelectIn low, nAutoFd high */
		parport_frob_control (port,
				      PARPORT_CONTROL_SELECT
				      | PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_SELECT);

		/* Event 24: nAck goes low */
		parport_wait_peripheral (port, PARPORT_STATUS_ACK, 0);

		/* Event 25: Set nAutoFd low */
		parport_frob_control (port,
				      PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_AUTOFD);

		/* Event 27: nAck goes high */
		parport_wait_peripheral (port,
					 PARPORT_STATUS_ACK, 
					 PARPORT_STATUS_ACK);

		/* Event 29: Set nAutoFd high */
		parport_frob_control (port, PARPORT_CONTROL_AUTOFD, 0);
	}

	port->ieee1284.mode = IEEE1284_MODE_COMPAT;
	port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;

	DPRINTK (KERN_DEBUG "%s: In compatibility (forward idle) mode\n",
		 port->name);
}		
#endif /* IEEE1284 support */

/* Negotiate an IEEE 1284 mode.
 * return values are:
 *   0 - handshake OK; IEEE1284 peripheral and mode available
 *  -1 - handshake failed; peripheral is not compliant (or none present)
 *   1 - handshake OK; IEEE1284 peripheral present but mode not available
 */
int parport_negotiate (struct parport *port, int mode)
{
#ifndef CONFIG_PARPORT_1284
	if (mode == IEEE1284_MODE_COMPAT)
		return 0;
	printk (KERN_ERR "parport: IEEE1284 not supported in this kernel\n");
	return -1;
#else
	int m = mode & ~IEEE1284_ADDR;
	unsigned char xflag;

	port = port->physport;

	/* Is there anything to do? */
	if (port->ieee1284.mode == mode)
		return 0;

	/* Is the difference just an address-or-not bit? */
	if ((port->ieee1284.mode & ~IEEE1284_ADDR) == (mode & ~IEEE1284_ADDR)){
		port->ieee1284.mode = mode;
		return 0;
	}

	/* Go to compability forward idle mode */
	if (port->ieee1284.mode != IEEE1284_MODE_COMPAT)
		parport_ieee1284_terminate (port);

	if (mode == IEEE1284_MODE_COMPAT)
		/* Compatibility mode: no negotiation. */
		return 0; 

	switch (mode) {
	case IEEE1284_MODE_ECPSWE:
		m = IEEE1284_MODE_ECP;
		break;
	case IEEE1284_MODE_EPPSL:
	case IEEE1284_MODE_EPPSWE:
		m = IEEE1284_MODE_EPP;
		break;
	case IEEE1284_MODE_BECP:
		return -ENOSYS; /* FIXME (implement BECP) */
	}

	port->ieee1284.phase = IEEE1284_PH_NEGOTIATION;

	/* Start off with nStrobe and nAutoFd high, and nSelectIn low */
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE
			      | PARPORT_CONTROL_AUTOFD
			      | PARPORT_CONTROL_SELECT,
			      PARPORT_CONTROL_SELECT);
	udelay(1);

	/* Event 0: Set data */
	parport_write_data (port, m);
	udelay (400); /* Shouldn't need to wait this long. */

	/* Event 1: Set nSelectIn high, nAutoFd low */
	parport_frob_control (port,
			      PARPORT_CONTROL_SELECT
			      | PARPORT_CONTROL_AUTOFD,
			      PARPORT_CONTROL_AUTOFD);

	/* Event 2: PError, Select, nFault go high, nAck goes low */
	if (parport_wait_peripheral (port,
				     PARPORT_STATUS_ERROR
				     | PARPORT_STATUS_SELECT
				     | PARPORT_STATUS_PAPEROUT
				     | PARPORT_STATUS_ACK,
				     PARPORT_STATUS_ERROR
				     | PARPORT_STATUS_SELECT
				     | PARPORT_STATUS_PAPEROUT)) {
		/* Timeout */
		parport_frob_control (port,
				      PARPORT_CONTROL_SELECT
				      | PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_SELECT);
		DPRINTK (KERN_DEBUG
			 "%s: Peripheral not IEEE1284 compliant (0x%02X)\n",
			 port->name, parport_read_status (port));
		port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;
		return -1; /* Not IEEE1284 compliant */
	}

	/* Event 3: Set nStrobe low */
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE,
			      PARPORT_CONTROL_STROBE);

	/* Event 4: Set nStrobe and nAutoFd high */
	udelay (5);
	parport_frob_control (port,
			      PARPORT_CONTROL_STROBE
			      | PARPORT_CONTROL_AUTOFD,
			      0);

	/* Event 6: nAck goes high */
	if (parport_wait_peripheral (port,
				     PARPORT_STATUS_ACK,
				     PARPORT_STATUS_ACK)) {
		/* This shouldn't really happen with a compliant device. */
		DPRINTK (KERN_DEBUG
			 "%s: Mode 0x%02x not supported? (0x%02x)\n",
			 port->name, mode, port->ops->read_status (port));
		parport_ieee1284_terminate (port);
		return 1;
	}

	xflag = parport_read_status (port) & PARPORT_STATUS_SELECT;

	/* xflag should be high for all modes other than nibble (0). */
	if (mode && !xflag) {
		/* Mode not supported. */
		DPRINTK (KERN_DEBUG "%s: Mode 0x%02x not supported\n",
			 port->name, mode);
		parport_ieee1284_terminate (port);
		return 1;
	}

	/* Mode is supported */
	DPRINTK (KERN_DEBUG "%s: In mode 0x%02x\n", port->name, mode);
	port->ieee1284.mode = mode;

	/* But ECP is special */
	if (mode & IEEE1284_MODE_ECP) {
		port->ieee1284.phase = IEEE1284_PH_ECP_SETUP;

		/* Event 30: Set nAutoFd low */
		parport_frob_control (port,
				      PARPORT_CONTROL_AUTOFD,
				      PARPORT_CONTROL_AUTOFD);

		/* Event 31: PError goes high. */
		parport_wait_peripheral (port,
					 PARPORT_STATUS_PAPEROUT,
					 PARPORT_STATUS_PAPEROUT);
		/* (Should check that this works..) */

		port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;
		DPRINTK (KERN_DEBUG "%s: ECP direction: forward\n",
			 port->name);
	} else switch (mode) {
	case IEEE1284_MODE_NIBBLE:
	case IEEE1284_MODE_BYTE:
		port->ieee1284.phase = IEEE1284_PH_REV_IDLE;
		break;
	default:
		port->ieee1284.phase = IEEE1284_PH_FWD_IDLE;
	}


	return 0;
#endif /* IEEE1284 support */
}

/* Acknowledge that the peripheral has data available.
 * Events 18-20, in order to get from Reverse Idle phase
 * to Host Busy Data Available.
 * This will most likely be called from an interrupt.
 * Returns zero if data was available.
 */
#ifdef CONFIG_PARPORT_1284
static int parport_ieee1284_ack_data_avail (struct parport *port)
{
	if (parport_read_status (port) & PARPORT_STATUS_ERROR)
		/* Event 18 didn't happen. */
		return -1;

	/* Event 20: nAutoFd goes high. */
	port->ops->frob_control (port, PARPORT_CONTROL_AUTOFD, 0);
	port->ieee1284.phase = IEEE1284_PH_HBUSY_DAVAIL;
	return 0;
}
#endif /* IEEE1284 support */

/* Handle an interrupt. */
void parport_ieee1284_interrupt (int which, void *handle, struct pt_regs *regs)
{
	struct parport *port = handle;
	parport_ieee1284_wakeup (port);

#ifdef CONFIG_PARPORT_1284
	if (port->ieee1284.phase == IEEE1284_PH_REV_IDLE) {
		/* An interrupt in this phase means that data
		 * is now available. */
		DPRINTK (KERN_DEBUG "%s: Data available\n", port->name);
		parport_ieee1284_ack_data_avail (port);
	}
#endif /* IEEE1284 support */
}

/* Write a block of data. */
ssize_t parport_write (struct parport *port, const void *buffer, size_t len)
{
#ifndef CONFIG_PARPORT_1284
	return port->ops->compat_write_data (port, buffer, len, 0);
#else
	ssize_t retval;
	int mode = port->ieee1284.mode;
	int addr = mode & IEEE1284_ADDR;
	size_t (*fn) (struct parport *, const void *, size_t, int);

	/* Ignore the device-ID-request bit and the address bit. */
	mode &= ~(IEEE1284_DEVICEID | IEEE1284_ADDR);

	/* Use the mode we're in. */
	switch (mode) {
	case IEEE1284_MODE_NIBBLE:
		parport_negotiate (port, IEEE1284_MODE_COMPAT);
	case IEEE1284_MODE_COMPAT:
		DPRINTK (KERN_DEBUG "%s: Using compatibility mode\n",
			 port->name);
		fn = port->ops->compat_write_data;
		break;

	case IEEE1284_MODE_EPP:
		DPRINTK (KERN_DEBUG "%s: Using EPP mode\n", port->name);
		if (addr)
			fn = port->ops->epp_write_addr;
		else
			fn = port->ops->epp_write_data;
		break;

	case IEEE1284_MODE_ECP:
	case IEEE1284_MODE_ECPRLE:
		DPRINTK (KERN_DEBUG "%s: Using ECP mode\n", port->name);
		if (addr)
			fn = port->ops->ecp_write_addr;
		else
			fn = port->ops->ecp_write_data;
		break;

	case IEEE1284_MODE_ECPSWE:
		DPRINTK (KERN_DEBUG "%s: Using software-emulated ECP mode\n",
			 port->name);
		/* The caller has specified that it must be emulated,
		 * even if we have ECP hardware! */
		if (addr)
			fn = parport_ieee1284_ecp_write_addr;
		else
			fn = parport_ieee1284_ecp_write_data;
		break;

	default:
		DPRINTK (KERN_DEBUG "%s: Unknown mode 0x%02x\n", port->name,
			port->ieee1284.mode);
		return -ENOSYS;
	}

	retval = (*fn) (port, buffer, len, 0);
	DPRINTK (KERN_DEBUG "%s: wrote %d/%d bytes\n", port->name, retval,
		 len);
	return retval;
#endif /* IEEE1284 support */
}

/* Read a block of data. */
ssize_t parport_read (struct parport *port, void *buffer, size_t len)
{
#ifndef CONFIG_PARPORT_1284
	printk (KERN_ERR "parport: IEEE1284 not supported in this kernel\n");
	return -ENODEV;
#else
	int mode = port->physport->ieee1284.mode;
	int addr = mode & IEEE1284_ADDR;
	size_t (*fn) (struct parport *, void *, size_t, int);

	/* Ignore the device-ID-request bit and the address bit. */
	mode &= ~(IEEE1284_DEVICEID | IEEE1284_ADDR);

	/* Use the mode we're in. */
	switch (mode) {
	case IEEE1284_MODE_COMPAT:
		if (parport_negotiate (port, IEEE1284_MODE_NIBBLE))
			return -EIO;
	case IEEE1284_MODE_NIBBLE:
		DPRINTK (KERN_DEBUG "%s: Using nibble mode\n", port->name);
		fn = port->ops->nibble_read_data;
		break;

	case IEEE1284_MODE_BYTE:
		DPRINTK (KERN_DEBUG "%s: Using byte mode\n", port->name);
		fn = port->ops->byte_read_data;
		break;

	case IEEE1284_MODE_EPP:
		DPRINTK (KERN_DEBUG "%s: Using EPP mode\n", port->name);
		if (addr)
			fn = port->ops->epp_read_addr;
		else
			fn = port->ops->epp_read_data;
		break;

	case IEEE1284_MODE_ECP:
	case IEEE1284_MODE_ECPRLE:
		DPRINTK (KERN_DEBUG "%s: Using ECP mode\n", port->name);
		fn = port->ops->ecp_read_data;
		break;

	case IEEE1284_MODE_ECPSWE:
		DPRINTK (KERN_DEBUG "%s: Using software-emulated ECP mode\n",
			 port->name);
		fn = parport_ieee1284_ecp_read_data;
		break;

	default:
		DPRINTK (KERN_DEBUG "%s: Unknown mode 0x%02x\n", port->name,
			 port->physport->ieee1284.mode);
		return -ENOSYS;
	}

	return (*fn) (port, buffer, len, 0);
#endif /* IEEE1284 support */
}

/* Set the amount of time we wait while nothing's happening. */
long parport_set_timeout (struct pardevice *dev, long inactivity)
{
	long int old = dev->timeout;

	dev->timeout = inactivity;

	if (dev->port->physport->cad == dev)
		parport_ieee1284_wakeup (dev->port);

	return old;
}
