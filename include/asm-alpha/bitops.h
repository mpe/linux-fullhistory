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
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%1\n\t"
		"and %0,%3,%2\n\t"
		"bne %2,2f\n\t"
		"xor %0,%3,%0\n\t"
		"stl_c %0,%1\n\t"
		"beq %0,1b\n"
		"2:"
		:"=&r" (temp),
		 "=m" (*m),
		 "=&r" (oldbit)
		:"Ir" (1UL << (nr & 31)),
		 "m" (*m));
	return oldbit != 0;
}

extern __inline__ unsigned long clear_bit(unsigned long nr, void * addr)
{
	unsigned long oldbit;
	unsigned long temp;
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%1\n\t"
		"and %0,%3,%2\n\t"
		"beq %2,2f\n\t"
		"xor %0,%3,%0\n\t"
		"stl_c %0,%1\n\t"
		"beq %0,1b\n"
		"2:"
		:"=&r" (temp),
		 "=m" (*m),
		 "=&r" (oldbit)
		:"Ir" (1UL << (nr & 31)),
		 "m" (*m));
	return oldbit != 0;
}

extern __inline__ unsigned long change_bit(unsigned long nr, void * addr)
{
	unsigned long oldbit;
	unsigned long temp;
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%1\n\t"
		"and %0,%3,%2\n\t"
		"xor %0,%3,%0\n\t"
		"stl_c %0,%1\n\t"
		"beq %0,1b\n"
		:"=&r" (temp),
		 "=m" (*m),
		 "=&r" (oldbit)
		:"Ir" (1UL << (nr & 31)),
		 "m" (*m));
	return oldbit != 0;
}

extern __inline__ unsigned long test_bit(int nr, const void * addr)
{
	return 1UL & (((const int *) addr)[nr >> 5] >> (nr & 31));
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 *
 * Do a binary search on the bits.  Due to the nature of large
 * constants on the alpha, it is worthwhile to split the search.
 */
extern inline unsigned long ffz_b(unsigned long x)
{
	unsigned long sum = 0;

	x = ~x & -~x;		/* set first 0 bit, clear others */
	if (x & 0xF0) sum += 4;
	if (x & 0xCC) sum += 2;
	if (x & 0xAA) sum += 1;

	return sum;
}

extern inline unsigned long ffz(unsigned long word)
{
	unsigned long bits, qofs, bofs;

	__asm__("cmpbge %1,%2,%0" : "=r"(bits) : "r"(word), "r"(~0UL));
	qofs = ffz_b(bits);
	__asm__("extbl %1,%2,%0" : "=r"(bits) : "r"(word), "r"(qofs));
	bofs = ffz_b(bits);

	return qofs*8 + bofs;
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
