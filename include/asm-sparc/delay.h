/* $Id: delay.h,v 1.7 1995/11/25 02:31:32 davem Exp $
 * delay.h: Linux delay routines on the Sparc.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu).
 */

#ifndef __SPARC_DELAY_H
#define __SPARC_DELAY_H

extern __inline__ void __delay(unsigned long loops)
{
	__asm__ __volatile__("cmp %0, 0\n\t"
			     "1: bne 1b\n\t"
			     "subcc %0, 1, %0\n" :
			     "=&r" (loops) :
			     "0" (loops));
}

/* udelay(usecs) is used for very short delays up to 1 millisecond. On
 * the Sparc (both sun4c and sun4m) we have a free running usec counter
 * available to us already.
 */
extern volatile unsigned int *master_l10_counter;

extern __inline__ void udelay(unsigned int usecs)
{
	unsigned int ccnt;

	if(!master_l10_counter)
		return;
	ccnt=*master_l10_counter;
	for(usecs+=1; usecs; usecs--, ccnt=*master_l10_counter)
		while(*master_l10_counter == ccnt)
			__asm__("": : :"memory");
}

/* calibrate_delay() wants this... */
#define muldiv(a, b, c)    (((a)*(b))/(c))

#endif /* defined(__SPARC_DELAY_H) */

