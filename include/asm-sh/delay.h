#ifndef __ASM_SH_DELAY_H
#define __ASM_SH_DELAY_H

/*
 * Copyright (C) 1999  Kaz Kojima
 */

extern __inline__ void __delay(unsigned long loops)
{
	__asm__ __volatile__(
		"tst	%0,%0\n\t"
		"1:\t"
		"bf/s	1b\n\t"
		" dt	%0"
		: "=r" (loops)
		: "0" (loops));
}

extern __inline__ void __udelay(unsigned long usecs, unsigned long lps)
{
	usecs *= 0x000010c6;		/* 2**32 / 1000000 */
	__asm__("dmulu.l	%0,%2\n\t"
		"sts	mach,%0"
		: "=r" (usecs)
		: "0" (usecs), "r" (lps)
		: "macl", "mach");
        __delay(usecs);
}


#ifdef __SMP__
#define __udelay_val cpu_data[smp_processor_id()].udelay_val
#else
#define __udelay_val loops_per_sec
#endif

#define udelay(usecs) __udelay((usecs),__udelay_val)

#endif /* __ASM_SH_DELAY_H */
