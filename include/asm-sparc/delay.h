/* $Id: delay.h,v 1.10 1997/11/07 18:24:30 mj Exp $
 * delay.h: Linux delay routines on the Sparc.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu).
 */

#ifndef __SPARC_DELAY_H
#define __SPARC_DELAY_H

extern unsigned long loops_per_sec;

extern __inline__ void __delay(unsigned long loops)
{
	__asm__ __volatile__("cmp %0, 0\n\t"
			     "1: bne 1b\n\t"
			     "subcc %0, 1, %0\n" :
			     "=&r" (loops) :
			     "0" (loops) :
			     "cc");
}

/* This is too messy with inline asm on the Sparc. */
extern void udelay(unsigned long usecs);

#endif /* defined(__SPARC_DELAY_H) */
