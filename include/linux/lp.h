#ifndef _LINUX_LP_H
#define _LINUX_LP_H

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/segment.h>

/*
 * usr/include/linux/lp.h c.1991-1992 James Wiegand
 * many modifications copyright (C) 1992 Michael K. Johnson
 */

/*
 * Per POSIX guidelines, this module reserves the LP and lp prefixes
 * These are the lp_table[minor].flags flags...
 */
#define LP_EXIST 0x0001
#define LP_SELEC 0x0002
#define LP_BUSY	 0x0004
#define LP_OFFL	 0x0008
#define LP_NOPA  0x0010
#define LP_ERR   0x0020
#define LP_ABORT 0x0040

/* timeout for each character.  This is relative to bus cycles -- it
 * is the count in a busy loop.  THIS IS THE VALUE TO CHANGE if you
 * have extremely slow printing, or if the machine seems to slow down
 * a lot when you print.  If you have slow printing, increase this
 * number and recompile, and if your system gets bogged down, decrease
 * this number.  This can be changed with the tunelp(8) command.
 */

#define LP_INIT_CHAR 250

/* The parallel port specs apparently say that there needs to be
 * a .5usec wait before and after the strobe.  Since there are wildly
 * different computers running linux, I can't come up with a perfect
 * value, but since it worked well on most printers before without,
 * and I have seen some improvement on my computer by making it a
 * small number, I'll initialize it to 2.
 */

#define LP_INIT_WAIT 2

/* This is the amount of time that the driver waits for the printer to
 * catch up when the printer's buffer appears to be filled.  If you
 * want to tune this and have a fast printer (i.e. HPIIIP), decrease
 * this number, and if you have a slow printer, increase this number.
 * This is in hundredths of a second, the default 10 being .1 second.
 * Or use the tunelp(8) command, which is especially nice if you want
 * change back and forth between character and graphics printing, which
 * are wildly different...
 */

#define LP_INIT_TIME 10

/* IOCTL numbers */
#define LPCHAR   0x0001  /* corresponds to LP_INIT_CHAR */
#define LPTIME   0x0002  /* corresponds to LP_INIT_TIME */
#define LPABORT  0x0004  /* call with TRUE arg to abort on error,
			    FALSE to retry.  Default is retry.  */
#define LPWAIT   0x0008  /* corresponds to LP_INIT_WAIT */

/* timeout for printk'ing a timeout, in jiffies (100ths of a second).
   This is also used for re-checking error conditions if LP_ABORT is
   not set.  This is the default behavior. */

#define LP_TIMEOUT 1000

#define LP_B(minor)	lp_table[(minor)].base		/* IO address */
#define LP_F(minor)	lp_table[(minor)].flags		/* flags for busy, etc. */
#define LP_S(minor)	inb(LP_B((minor)) + 1)		/* status port */
#define LP_C(minor)	(lp_table[(minor)].base + 2)	/* control port */
#define LP_CHAR(minor)	lp_table[(minor)].chars		/* busy timeout */
#define LP_TIME(minor)	lp_table[(minor)].time		/* wait time */
#define LP_WAIT(minor)	lp_table[(minor)].wait		/* strobe wait */

/* 
since we are dealing with a horribly slow device
I don't see the need for a queue
*/
struct lp_struct {
	int base;
	int flags;
	unsigned int chars;
	unsigned int time;
	unsigned int wait;
};

/* the BIOS manuals say there can be up to 4 lpt devices
 * but I have not seen a board where the 4th address is listed
 * if you have different hardware change the table below 
 * please let me know if you have different equipment
 * if you have more than 3 printers, remember to increase LP_NO
 */
struct lp_struct lp_table[] = {
	{ 0x3bc, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, },
	{ 0x378, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, },
	{ 0x278, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, },
}; 
#define LP_NO 3

/* 
 * bit defines for 8255 status port
 * base + 1
 * accessed with LP_S(minor), which gets the byte...
 */
#define LP_PBUSY	0x80 /* active low */
#define LP_PACK		0x40 /* active low */
#define LP_POUTPA	0x20
#define LP_PSELECD	0x10
#define LP_PERRORP	0x08 /* active low*/

/* 
 * defines for 8255 control port
 * base + 2 
 * accessed with LP_C(minor)
 */
#define LP_PSELECP	0x08
#define LP_PINITP	0x04  /* active low */
#define LP_PAUTOLF	0x02
#define LP_PSTROBE	0x01

/* 
 * the value written to ports to test existence. PC-style ports will 
 * return the value written. AT-style ports will return 0. so why not
 * make them the same ? 
 */
#define LP_DUMMY	0x00

/*
 * This is the port delay time.  Your mileage may vary.
 * It is used only in the lp_init() routine.
 */
#define LP_DELAY 	150000

/*
 * function prototypes
 */

extern long lp_init(long);

#endif
