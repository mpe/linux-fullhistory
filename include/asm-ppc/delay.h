#ifndef _PPC_DELAY_H
#define _PPC_DELAY_H


extern __inline__ void __delay(unsigned long );
extern __inline__ void __udelay(unsigned long );


extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	return (a*b)/c;
}

#endif /* defined(_PPC_DELAY_H) */

