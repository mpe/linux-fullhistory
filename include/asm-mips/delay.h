#ifndef __ASM_MIPS_DELAY_H
#define __ASM_MIPS_DELAY_H

extern __inline__ void __delay(int loops)
{
	__asm__ __volatile__ (
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"1:\tbne\t$0,%0,1b\n\t"
		"subu\t%0,%0,1\n\t"
		".set\tat\n\t"
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
extern __inline__ void udelay(unsigned long usecs)
{
	usecs *= 0x000010c6;		/* 2**32 / 1000000 */
	__asm__("mul\t%0,%0,%1"
		:"=r" (usecs)
		:"0" (usecs),"r" (loops_per_sec));
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

#endif /* __ASM_MIPS_DELAY_H */
