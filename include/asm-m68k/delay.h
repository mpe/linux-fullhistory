#ifndef _M68K_DELAY_H
#define _M68K_DELAY_H

/*
 * Copyright (C) 1994 Hamish Macdonald
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */

extern __inline__ void __delay(int loops)
{
	__asm__("\n\tmovel %0,d0\n1:\tsubql #1,d0\n\tbpls 1b\n"
		: /* no outputs */
		: "g" (loops)
		: "d0");
}

/*
 * division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)  
 */
extern __inline__ void udelay(unsigned long usecs)
{
	asm ("mulul %1,d0,%0\n\t"
	     "divul  %2,d0,%0"
	     : "=d" (usecs)
	     : "d" (usecs),
	       "i" (1000000),
	       "0" (loops_per_sec)
	     : "d0");
	__delay(usecs);
}

#endif /* defined(_M68K_DELAY_H) */
