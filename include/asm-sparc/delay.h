#ifndef __SPARC_DELAY_H
#define __SPARC_DELAY_H

extern unsigned long loops_per_sec;

/*
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu).
 *
 * Delay quick inlined code using 'loops_per_second' which is
 * calculated in calibrate_delay() in main.c (ie. BogoMIPS :-)
 */

extern __inline__ void __delay(unsigned int loops)
{
  __asm__ __volatile__("\n1:\tcmp %0, 0\n\t"
		       "bne,a 1b\n\t"
		       "sub %0, 1, %0\n": "=&r" (loops) : "0" (loops));
}

/* udelay(usecs) is used for very short delays up to 1 millisecond. */

extern __inline__ void udelay(unsigned int usecs)
{
  usecs *= 0x000010c6;         /* Sparc is 32-bit just like ix86 */

  __delay(loops_per_sec*usecs);

}

/* calibrate_delay() wants this... */

extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	return ((a*b)/c);
}

#endif /* defined(__SPARC_DELAY_H) */

