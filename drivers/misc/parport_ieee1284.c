/* $Id$ 
 * IEEE-1284 implementation for parport.
 *
 * Authors: Philip Blundell <pjb27@cam.ac.uk>
 *          Carsten Gross <carsten@sol.wohnheim.uni-ulm.de>
 *	    Jose Renau <renau@acm.org>
 */

#include <linux/tasks.h>

#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

/* The following read functions are an implementation of a status readback
 * and device id request confirming to IEEE1284-1994.
 *
 * These probably ought to go in some seperate file, so people like the SPARC
 * don't have to pull them in.
 */

/* Wait for Status line(s) to change in 35 ms - see IEEE1284-1994 page 24 to
 * 25 for this. After this time we can create a timeout because the
 * peripheral doesn't conform to IEEE1284. We want to save CPU time: we are
 * waiting a maximum time of 500 us busy (this is for speed). If there is
 * not the right answer in this time, we call schedule and other processes
 * are able "to eat" the time up to 30ms.  So the maximum load avarage can't
 * get above 5% for a read even if the peripheral is really slow. (but your
 * read gets very slow then - only about 10 characters per second. This
 * should be tuneable). Thanks to Andreas who pointed me to this and ordered
 * the documentation.
 */ 

int parport_wait_peripheral(struct parport *port, unsigned char mask, 
	unsigned char result)
{
	int counter=0;
	unsigned char status; 
	
	do {
		status = parport_read_status(port);
		udelay(25);
		counter++;
		if (need_resched)
			schedule();
	} while ( ((status & mask) != result) && (counter < 20) );
	if ( (counter == 20) && ((status & mask) != result) ) { 
		current->state=TASK_INTERRUPTIBLE;
		current->timeout=jiffies+4;
		schedule(); /* wait for 4 scheduler runs (40ms) */
		status = parport_read_status(port);
		if ((status & mask) != result) return 1; /* timeout */
	}
	return 0; /* okay right response from device */
}		

/* Test if nibble mode for status readback is okay. Returns the value false
 * if the printer doesn't support readback at all. If it supports readbacks
 * and printer data is available the function returns 1, otherwise 2. The
 * only valid values for "mode" are 0 and 4. 0 requests normal nibble mode,
 * 4 is for "request device id using nibble mode". The request for the
 * device id is best done in an ioctl (or at bootup time).  There is no
 * check for an invalid value, the only function using this call at the
 * moment is lp_read and the ioctl LPGETDEVICEID both fixed calls from
 * trusted kernel.
 */
int parport_ieee1284_nibble_mode_ok(struct parport *port, unsigned char mode) 
{
	parport_write_data(port, mode);
	udelay(5);		
	parport_write_control(port, parport_read_control(port) & ~8);  /* SelectIN  low */
	parport_write_control(port, parport_read_control(port) | 2); /* AutoFeed  high */
	if (parport_wait_peripheral(port, 0x78, 0x38)) { /* timeout? */
		parport_write_control(port, (parport_read_control(port) & ~2) | 8);
		return 0; /* first stage of negotiation failed, 
                           * no IEEE1284 compliant device on this port 
                           */ 
	}
	parport_write_control(port, parport_read_control(port) | 1);      /* Strobe  high */
	udelay(5);				     /* Strobe wait */
	parport_write_control(port, parport_read_control(port) & ~1);     /* Strobe  low */
	udelay(5);
	parport_write_control(port, parport_read_control(port) & ~2);     /*  AutoFeed low */
	return (parport_wait_peripheral(port, 0x20, 0))?2:1;
}
