/* $Id: mostek.h,v 1.5 1996/04/25 06:13:17 davem Exp $
 * mostek.h:  Describes the various Mostek time of day clock registers.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_MOSTEK_H
#define _SPARC_MOSTEK_H

#include <asm/idprom.h>

/* First the Mostek 48t02 clock chip.  The registers other than the
 * control register are in binary coded decimal.
 */
struct mostek48t02 {
	char eeprom[2008];            /* This is the eeprom, don't touch! */
	struct idp_struct idprom;     /* The idprom lives here. */
	volatile unsigned char creg;  /* Control register */
	volatile unsigned char sec;   /* Seconds (0-59) */
	volatile unsigned char min;   /* Minutes (0-59) */
	volatile unsigned char hour;  /* Hour (0-23) */
	volatile unsigned char dow;   /* Day of the week (1-7) */
	volatile unsigned char dom;   /* Day of the month (1-31) */
	volatile unsigned char mnth;  /* Month of year (1-12) */
	volatile unsigned char yr;    /* Year (0-99) */
};

extern struct mostek48t02 *mstk48t02_regs;

/* Control register values. */
#define MSTK_CREG_WRITE    0x80   /* Must set this before placing values. */
#define MSTK_CREG_READ     0x40   /* Stop the clock, I want to fetch values. */
#define MSTK_CREG_SIGN     0x20   /* Grrr... what's this??? */

#define MSTK_YR_ZERO       1968   /* If year reg has zero, it is 1968 */
#define MSTK_CVT_YEAR(yr)  ((yr) + MSTK_YR_ZERO)

/* Fun with masks. */
#define MSTK_SEC_MASK      0x7f
#define MSTK_MIN_MASK      0x7f
#define MSTK_HOUR_MASK     0x3f
#define MSTK_DOW_MASK      0x07
#define MSTK_DOM_MASK      0x3f
#define MSTK_MNTH_MASK     0x1f
#define MSTK_YR_MASK       0xff

/* Conversion routines. */
#define MSTK_REGVAL_TO_DECIMAL(x)  (((x) & 0xf) + 0xa * ((x) >> 0x4))
#define MSTK_DECIMAL_TO_REGVAL(x)  ((((x) / 0xa) << 0x4) + ((x) % 0xa))

/* Macros to make register access easier on our fingers. These give you
 * the decimal value of the register requested if applicable.  You pass
 * the a pointer to a 'struct mostek48t02'.
 */
#define MSTK_REG_CREG(ptr)  (ptr->creg)
#define MSTK_REG_SEC(ptr)   (MSTK_REGVAL_TO_DECIMAL((ptr->sec & MSTK_SEC_MASK)))
#define MSTK_REG_MIN(ptr)   (MSTK_REGVAL_TO_DECIMAL((ptr->min & MSTK_MIN_MASK)))
#define MSTK_REG_HOUR(ptr)  (MSTK_REGVAL_TO_DECIMAL((ptr->hour & MSTK_HOUR_MASK)))
#define MSTK_REG_DOW(ptr)   (MSTK_REGVAL_TO_DECIMAL((ptr->dow & MSTK_DOW_MASK)))
#define MSTK_REG_DOM(ptr)   (MSTK_REGVAL_TO_DECIMAL((ptr->dom & MSTK_DOM_MASK)))
#define MSTK_REG_MNTH(ptr)  (MSTK_REGVAL_TO_DECIMAL((ptr->mnth & MSTK_MNTH_MASK)))
#define MSTK_REG_YR(ptr)    (MSTK_REGVAL_TO_DECIMAL((ptr->yr & MSTK_YR_MASK)))

/* The Mostek 48t02 clock chip.  Found on Sun4m's I think.  It has the
 * same (basically) layout of the 48t02 chip.
 */
struct mostek48t08 {
	char offset[6*1024];         /* Magic things may be here, who knows? */
	struct mostek48t02 regs;     /* Here is what we are interested in.   */
};
extern struct mostek48t08 *mstk48t08_regs;

enum sparc_clock_type {	MSTK48T02, MSTK48T08, MSTK_INVALID };
extern enum sparc_clock_type sp_clock_typ;

#endif /* !(_SPARC_MOSTEK_H) */
