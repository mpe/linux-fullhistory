/*
 *	Precise Delay Loops for i386
 *
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	The __delay function must _NOT_ be inlined as its execution time
 *	depends wildly on alignment on many x86 processors.
 */

#include <linux/sched.h>
#include <asm/delay.h>

#ifdef __SMP__
#include <asm/smp.h>
#endif

#ifdef __SMP__
#define __udelay_val cpu_data[smp_processor_id()].udelay_val
#else
#define __udelay_val loops_per_sec
#endif

void __delay(unsigned long loops)
{
	__asm__ __volatile__(
 		"1:\tdecl %0\n\tjns 1b"
		:/* no outputs */
		:"a" (loops)
		:"ax");
}

inline void __const_udelay(unsigned long xloops)
{
	__asm__("mull %0"
		:"=d" (xloops)
		:"a" (xloops),"0" (__udelay_val)
		:"ax");
        __delay(xloops);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}
