#ifndef _M68K_DELAY_H
#define _M68K_DELAY_H

/*
 * Copyright (C) 1994 Hamish Macdonald
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */

extern __inline__ void __delay(int loops)
{
	__asm__ __volatile__ ("\n\tmovel %0,%/d0\n1:\tsubql #1,%/d0\n\t"
			      "bpls 1b\n"
		: /* no outputs */
		: "g" (loops)
		: "d0");
}

/*
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)  
 */
extern __inline__ void udelay(unsigned long usecs)
{
	usecs *= 0x000010c6;		/* 2**32 / 1000000 */

	__asm__ __volatile__ ("mulul %1,%0:%2"
	     : "=d" (usecs)
	     : "d" (usecs),
	       "d" (loops_per_sec));
	__delay(usecs);
}

extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	__asm__ ("mulul %1,%/d0:%0\n\tdivul %2,%/d0:%0"
		 :"=d" (a)
		 :"d" (b),
		 "d" (c),
		 "0" (a)
		 :"d0");
	return a;
}

#endif /* defined(_M68K_DELAY_H) */
