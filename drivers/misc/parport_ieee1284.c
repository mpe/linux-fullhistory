/* $Id: parport_ieee1284.c,v 1.4 1997/10/19 21:37:21 philip Exp $
 * IEEE-1284 implementation for parport.
 *
 * Authors: Phil Blundell <Philip.Blundell@pobox.com>
 *          Carsten Gross <carsten@sol.wohnheim.uni-ulm.de>
 *	    Jose Renau <renau@acm.org>
 */

#include <linux/tasks.h>
#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/kernel.h>

/* Wait for Status line(s) to change in 35 ms - see IEEE1284-1994 page 24 to
 * 25 for this. After this time we can create a timeout because the
 * peripheral doesn't conform to IEEE1284.  We want to save CPU time: we are
 * waiting a maximum time of 500 us busy (this is for speed).  If there is
 * not the right answer in this time, we call schedule and other processes
 * are able to eat the time up to 40ms.
 */ 

int parport_wait_peripheral(struct parport *port, unsigned char mask, 
	unsigned char result)
{
	int counter;
	unsigned char status; 
	
	for (counter = 0; counter < 20; counter++) {
		status = parport_read_status(port);
		if ((status & mask) == result)
			return 0;
		udelay(25);
		if (current->need_resched)
			schedule();
	}
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ/25);				/* wait for 40ms */
	status = parport_read_status(port);
	return ((status & mask) == result)?0:1;
}		

/* Test if the peripheral is IEEE 1284 compliant.
 * return values are:
 *   0 - handshake failed; peripheral is not compliant (or none present)
 *   1 - handshake OK; IEEE1284 peripheral present but no data available
 *   2 - handshake OK; IEEE1284 peripheral and data available
 */
int parport_ieee1284_nibble_mode_ok(struct parport *port, unsigned char mode) 
{
	/* make sure it's a valid state, set nStrobe & nAutoFeed high */
	parport_write_control(port, (parport_read_control(port) \
		& ~1 ) & ~2);
	udelay(1);
	parport_write_data(port, mode);
	udelay(1);
	/* nSelectIn high, nAutoFd low */
	parport_write_control(port, (parport_read_control(port) & ~8) | 2);
	if (parport_wait_peripheral(port, 0x78, 0x38)) {
		parport_write_control(port, 
				      (parport_read_control(port) & ~2) | 8);
		return 0; 
	}
	/* nStrobe low */
	parport_write_control(port, parport_read_control(port) | 1);
	udelay(1);				     /* Strobe wait */
	/* nStrobe high, nAutoFeed low, last step before transferring 
	 *  reverse data */
	parport_write_control(port, (parport_read_control(port) \
		& ~1) & ~2);
	udelay(1);
	/* Data available? */
	return (parport_wait_peripheral(port, 0x20, 0))?1:2;
}
