/*
 *	Precise Delay Loops for i386
 *
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	The __delay function must _NOT_ be inlined as its execution time
 *	depends wildly on alignment on many x86 processors. The additional
 *	jump magic is needed to get the timing stable on all the CPU's
 *	we have to worry about.
 */

#include <linux/sched.h>
#include <linux/delay.h>

#ifdef __SMP__
#include <asm/smp.h>
#endif

void __delay(unsigned long loops)
{
	__asm__ __volatile__(
		"\tjmp 1f\n"
		".align 16\n"
		"1:\tjmp 2f\n"
		".align 16\n"
		"2:\tdecl %0\n\tjns 2b"
		:/* no outputs */
		:"a" (loops)
		:"ax");
}

inline void __const_udelay(unsigned long xloops)
{
	__asm__("mull %0"
		:"=d" (xloops)
		:"a" (xloops),"0" (current_cpu_data.loops_per_sec)
		:"ax");
        __delay(xloops);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}
