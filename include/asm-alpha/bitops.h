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
	return oldbit != 0;
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
	return oldbit != 0;
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
	return oldbit != 0;
}

extern __inline__ unsigned long test_bit(int nr, void * addr)
{
	return 1UL & (((unsigned long *) addr)[nr >> 6] >> (nr & 63));
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 *
 * This uses the cmpbge insn to check which byte contains the zero.
 * I don't know if that's actually a good idea, but it's fun and the
 * resulting LBS tests should be natural on the alpha.. Besides, I'm
 * just teaching myself the asm of the alpha anyway.
 */
extern inline unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;
	unsigned long tmp;

	__asm__("cmpbge %1,%0,%0"
		:"=r" (tmp)
		:"r" (word), "0" (~0UL));
	while (tmp & 1) {
		word >>= 8;
		tmp >>= 1;
		result += 8;
	}
	while (word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}

/*
 * Find next zero bit in a bitmap reasonably efficiently..
 */
extern inline unsigned long find_next_zero_bit(void * addr, unsigned long size, unsigned long offset)
{
	unsigned long * p = ((unsigned long *) addr) + (offset >> 6);
	unsigned long result = offset & ~63UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 63UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (64-offset);
		if (size < 64)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 64;
		result += 64;
	}
	while (size & ~63UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 64;
		size -= 64;
	}
	if (!size)
		return result;
	tmp = *p;
found_first:
	tmp |= ~0UL << size;
found_middle:
	return result + ffz(tmp);
}

/*
 * The optimizer actually does good code for this case..
 */
#define find_first_zero_bit(addr, size) \
	find_next_zero_bit((addr), (size), 0)

#endif /* _ALPHA_BITOPS_H */
