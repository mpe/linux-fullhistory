#include <linux/config.h>
#include <linux/sched.h> /* for udelay's use of smp_processor_id */
#include <asm/param.h>
#include <asm/smp.h>
#include <linux/delay.h>

/*
 * Copyright (C) 1993, 2000 Linus Torvalds
 *
 * Delay routines, using a pre-computed "loops_per_jiffy" value.
 */

/*
 * Use only for very small delays (< 1 msec). 
 *
 * The active part of our cycle counter is only 32-bits wide, and
 * we're treating the difference between two marks as signed.  On
 * a 1GHz box, that's about 2 seconds.
 */

void __delay(int loops)
{
	int tmp;
	__asm__ __volatile__(
		"	rpcc %0\n"
		"	addl %1,%0,%1\n"
		"1:	rpcc %0\n"
		"	subl %1,%0,%0\n"
		"	bgt %0,1b"
		: "=&r" (tmp), "=r" (loops) : "1"(loops));
}

void __udelay(unsigned long usecs, unsigned long lpj)
{
	usecs *= (((unsigned long)HZ << 32) / 1000000) * lpj;
	__delay((long)usecs >> 32);
}

void udelay(unsigned long usecs)
{
#ifdef CONFIG_SMP
	__udelay(usecs, cpu_data[smp_processor_id()].loops_per_jiffy);
#else
	__udelay(usecs, loops_per_jiffy);
#endif
}

