/* $Id: delay.h,v 1.5 1997/06/18 12:36:23 jj Exp $
 * delay.h: Linux delay routines on the V9.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu).
 */

#ifndef __SPARC64_DELAY_H
#define __SPARC64_DELAY_H

extern unsigned long loops_per_sec;

extern __inline__ void __delay(unsigned long loops)
{
	__asm__ __volatile__("
	b,pt	%%xcc, 1f
	 cmp	%0, 0
	.align	32
1:
	bne,pt	%%xcc, 1b
	 subcc	%0, 1, %0
"	: "=&r" (loops)
	: "0" (loops)
	: "cc");
}

extern __inline__ void udelay(unsigned long usecs)
{
	usecs *= 0x00000000000010c6UL;		/* 2**32 / 1000000 */

	__asm__ __volatile__("
	mulx	%1, %2, %0
	srlx	%0, 32, %0
"	: "=r" (usecs)
	: "r" (usecs), "r" (loops_per_sec));

	__delay(usecs);
}

extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	return (a*b)/c;
}

#endif /* defined(__SPARC64_DELAY_H) */

