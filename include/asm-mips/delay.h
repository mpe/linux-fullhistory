#ifndef _MIPS_DELAY_H
#define _MIPS_DELAY_H

extern __inline__ void __delay(int loops)
{
	__asm__(".align 3\n"
		"1:\tbeq\t$0,%0,1b\n\t"
		"addiu\t%0,%0,-1\n\t"
		:
		:"d" (loops));
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
		:"=d" (usecs)
		:"0" (usecs),"d" (loops_per_sec)
		:"ax");
	__delay(usecs);
}

#endif /* defined(_MIPS_DELAY_H) */
