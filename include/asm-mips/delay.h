#ifndef __ASM_MIPS_DELAY_H
#define __ASM_MIPS_DELAY_H

extern __inline__ void __delay(int loops)
{
	__asm__ __volatile__ (
		".set\tnoreorder\n"
		"1:\tbnez\t%0,1b\n\t"
		"subu\t%0,1\n\t"
		".set\treorder"
		:"=r" (loops)
		:"0" (loops));
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
extern __inline__ void __udelay(unsigned long usecs, unsigned long lps)
{
	usecs *= 0x000010c6;		/* 2**32 / 1000000 */
	__asm__("multu\t%0,%1\n\t"
		"mfhi\t%0"
		:"=r" (usecs)
		:"0" (usecs),"r" (lps));
	__delay(usecs);
}

#ifdef __SMP__
#define __udelay_val cpu_data[smp_processor_id()].udelay_val
#else
#define __udelay_val loops_per_sec
#endif

#define udelay(usecs) __udelay((usecs),__udelay_val)

/*
 * The different variants for 32/64 bit are pure paranoia. The typical
 * range of numbers that appears for MIPS machines avoids overflows.
 */
extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	return (a*b)/c;
}

#endif /* __ASM_MIPS_DELAY_H */
