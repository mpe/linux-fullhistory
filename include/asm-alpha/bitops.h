#ifndef _ALPHA_BITOPS_H
#define _ALPHA_BITOPS_H

/*
 * Copyright 1994, Linus Torvalds.
 */

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 64 is the LSB of (addr+1).
 */

extern __inline__ unsigned long set_bit(unsigned long nr, void * addr)
{
	unsigned long oldbit;
	unsigned long temp;

	__asm__ __volatile__(
		"\n1:\t"
		"ldq_l %0,%1\n\t"
		"and %0,%3,%2\n\t"
		"bne %2,2f\n\t"
		"xor %0,%3,%0\n\t"
		"stq_c %0,%1\n\t"
		"beq %0,1b\n"
		"2:"
		:"=&r" (temp),
		 "=m" (((unsigned long *) addr)[nr >> 6]),
		 "=&r" (oldbit)
		:"r" (1UL << (nr & 63)),
		 "m" (((unsigned long *) addr)[nr >> 6]));
	return oldbit;
}

extern __inline__ unsigned long clear_bit(unsigned long nr, void * addr)
{
	unsigned long oldbit;
	unsigned long temp;

	__asm__ __volatile__(
		"\n1:\t"
		"ldq_l %0,%1\n\t"
		"and %0,%3,%2\n\t"
		"beq %2,2f\n\t"
		"xor %0,%3,%0\n\t"
		"stq_c %0,%1\n\t"
		"beq %0,1b\n"
		"2:"
		:"=&r" (temp),
		 "=m" (((unsigned long *) addr)[nr >> 6]),
		 "=&r" (oldbit)
		:"r" (1UL << (nr & 63)),
		 "m" (((unsigned long *) addr)[nr >> 6]));
	return oldbit;
}

extern __inline__ unsigned long change_bit(unsigned long nr, void * addr)
{
	unsigned long oldbit;
	unsigned long temp;

	__asm__ __volatile__(
		"\n1:\t"
		"ldq_l %0,%1\n\t"
		"and %0,%3,%2\n\t"
		"xor %0,%3,%0\n\t"
		"stq_c %0,%1\n\t"
		"beq %0,1b\n"
		:"=&r" (temp),
		 "=m" (((unsigned long *) addr)[nr >> 6]),
		 "=&r" (oldbit)
		:"r" (1UL << (nr & 63)),
		 "m" (((unsigned long *) addr)[nr >> 6]));
	return oldbit;
}

extern __inline__ unsigned long test_bit(int nr, void * addr)
{
	return (1UL << (nr & 63)) & ((unsigned long *) addr)[nr >> 6];
}

#endif /* _ALPHA_BITOPS_H */
