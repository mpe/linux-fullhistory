#ifndef __ALPHA_DELAY_H
#define __ALPHA_DELAY_H

extern unsigned long loops_per_sec;

/*
 * Copyright (C) 1993 Linus Torvalds
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */

extern __inline__ void __delay(unsigned long loops)
{
	__asm__ __volatile__(".align 3\n"
		"1:\tsubq %0,1,%0\n\t"
		"bge %0,1b": "=r" (loops) : "0" (loops));
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
	usecs *= 0x000010c6f7a0b5edUL;		/* 2**64 / 1000000 */
	__asm__("umulh %1,%2,%0"
		:"=r" (usecs)
		:"r" (usecs),"r" (loops_per_sec));
	__delay(usecs);
}

/*
 * 64-bit integers means we don't have to worry about overflow as
 * on some other architectures..
 */
extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	return (a*b)/c;
}

#endif /* defined(__ALPHA_DELAY_H) */
