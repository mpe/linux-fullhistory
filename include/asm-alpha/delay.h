#ifndef __ALPHA_DELAY_H
#define __ALPHA_DELAY_H

#include <asm/smp.h>

/*
 * Copyright (C) 1993 Linus Torvalds
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */

extern __inline__ void
__delay(unsigned long loops)
{
	register unsigned long r0 __asm__("$0") = loops;
#ifdef MODULE
	__asm__ __volatile__("lda $28,___delay; jsr $28,($28),0"
			     : "=r"(r0) : "r"(r0) : "$28");
#else
	__asm__ __volatile__("bsr $28,___delay" : "=r"(r0) : "r"(r0) : "$28");
#endif
}

/*
 * division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant).
 *
 * Optimize small constants further by exposing the second multiplication
 * to the compiler.  In addition, mulq is 2 cycles faster than umulh.
 */

extern __inline__ void
__udelay(unsigned long usecs, unsigned long lps)
{
	/* compute (usecs * 2**64 / 10**6) * loops_per_sec / 2**64 */

	usecs *= 0x000010c6f7a0b5edUL;		/* 2**64 / 1000000 */
	__asm__("umulh %1,%2,%0" :"=r" (usecs) :"r" (usecs),"r" (lps));
	__delay(usecs);
}

extern __inline__ void
__small_const_udelay(unsigned long usecs, unsigned long lps)
{
	/* compute (usecs * 2**32 / 10**6) * loops_per_sec / 2**32 */

        usecs *= 0x10c6;                /* 2^32 / 10^6 */
	usecs *= lps;
	usecs >>= 32;
	__delay(usecs);
}

#ifdef __SMP__
#define udelay(usecs)						\
	(__builtin_constant_p(usecs) && usecs < 0x100000000UL	\
	 ? __small_const_udelay(usecs,				\
		cpu_data[smp_processor_id()].loops_per_sec)	\
	 : __udelay(usecs,					\
		cpu_data[smp_processor_id()].loops_per_sec))
#else
#define udelay(usecs)						\
	(__builtin_constant_p(usecs) && usecs < 0x100000000UL	\
	 ? __small_const_udelay(usecs, loops_per_sec)		\
	 : __udelay(usecs, loops_per_sec))
#endif


#endif /* defined(__ALPHA_DELAY_H) */
