#ifndef __ASM_SH_DELAY_H
#define __ASM_SH_DELAY_H

/*
 * Copyright (C) 1999  Niibe Yutaka
 */

extern __inline__ void __delay(unsigned long loops)
{
	unsigned long __dummy;
	__asm__ __volatile__(
 		"1:\t"
		"dt	%0\n\t"
		"bf	1b"
		:"=r" (__dummy)
		:"0" (loops));
}

extern __inline__ void __udelay(unsigned long usecs, unsigned long lps)
{
	usecs *= 0x000010c6;		/* 2**32 / 1000000 */
	__asm__("mul.l	%0,%2\n\t"
		"sts	macl,%0"
		: "=&r" (usecs)
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

extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	return (a*b)/c;
}

#endif /* __ASM_SH_DELAY_H */
