/* $Id: bitops.h,v 1.13 1997/05/27 06:47:16 davem Exp $
 * bitops.h: Bit string operations on the V9.
 *
 * Copyright 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_BITOPS_H
#define _SPARC64_BITOPS_H

#include <asm/asi.h>         /* For the little endian spaces. */

/* These can all be exported to userland, because the atomic
 * primitives used are not privileged.
 */

/* Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */

extern __inline__ unsigned long test_and_set_bit(unsigned long nr, void *addr)
{
	unsigned long oldbit;
	unsigned long temp0, temp1;
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__("
	lduw		[%4], %0
1:
	andcc		%0, %3, %2
	bne,pn		%%icc, 2f
	 xor		%0, %3, %1
	cas 		[%4], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%4], %0
2:
"	: "=&r" (temp0), "=&r" (temp1), "=&r" (oldbit)
	: "HIr" (1UL << (nr & 31)), "r" (m)
	: "cc");
	return oldbit != 0;
}

extern __inline__ void set_bit(unsigned long nr, void *addr)
{
	(void) test_and_set_bit(nr, addr);
}

extern __inline__ unsigned long test_and_clear_bit(unsigned long nr, void *addr)
{
	unsigned long oldbit;
	unsigned long temp0, temp1;
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__("
	lduw		[%4], %0
1:
	andcc		%0, %3, %2
	be,pn		%%icc, 2f
	 xor		%0, %3, %1
	cas 		[%4], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%4], %0
2:
"	: "=&r" (temp0), "=&r" (temp1), "=&r" (oldbit)
	: "HIr" (1UL << (nr & 31)), "r" (m)
	: "cc");
	return oldbit != 0;
}

extern __inline__ void clear_bit(unsigned long nr, void *addr)
{
	(void) test_and_clear_bit(nr, addr);
}

extern __inline__ unsigned long test_and_change_bit(unsigned long nr, void *addr)
{
	unsigned long oldbit;
	unsigned long temp0, temp1;
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__("
	lduw		[%4], %0
1:
	and		%0, %3, %2
	xor		%0, %3, %1
	cas 		[%4], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%4], %0
"	: "=&r" (temp0), "=&r" (temp1), "=&r" (oldbit)
	: "HIr" (1UL << (nr & 31)), "r" (m)
	: "cc");
	return oldbit != 0;
}

extern __inline__ void change_bit(unsigned long nr, void *addr)
{
	(void) test_and_change_bit(nr, addr);
}

extern __inline__ unsigned long test_bit(int nr, __const__ void *addr)
{
	return 1UL & (((__const__ int *) addr)[nr >> 5] >> (nr & 31));
}

/* The easy/cheese version for now. */
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result;

#ifdef ULTRA_HAS_POPULATION_COUNT	/* Thanks for nothing Sun... */
	__asm__ __volatile__("
	brz,pn	%0, 1f
	 neg	%0, %%g1
	xnor	%0, %%g1, %%g2
	popc	%%g2, %0
1:	" : "=&r" (result)
	  : "0" (word)
	  : "g1", "g2");
#else
	result = 0;
	while(word & 1) {
		result++;
		word >>= 1;
	}
#endif
	return result;
}

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

extern __inline__ unsigned long find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
found_middle:
	return result + ffz(tmp);
}

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

/* Now for the ext2 filesystem bit operations and helper routines.
 * Note the usage of the little endian ASI's, werd, V9 is supreme.
 */
extern __inline__ int set_le_bit(int nr,void * addr)
{
	unsigned long oldbit;
	unsigned long temp0, temp1;
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__("
	lduwa		[%4] %5, %0
1:
	andcc		%0, %3, %2
	bne,pn		%%icc, 2f
	 xor		%0, %3, %1
	casa 		[%4] %5, %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduwa		[%4] %5, %0
2:
"	: "=&r" (temp0), "=&r" (temp1), "=&r" (oldbit)
	: "HIr" (1UL << (nr & 31)), "r" (m), "i" (ASI_PL)
	: "cc");
	return oldbit != 0;
}

extern __inline__ int clear_le_bit(int nr, void * addr)
{
	unsigned long oldbit;
	unsigned long temp0, temp1;
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);

	__asm__ __volatile__("
	lduwa		[%4] %5, %0
1:
	andcc		%0, %3, %2
	be,pn		%%icc, 2f
	 xor		%0, %3, %1
	casa 		[%4] %5, %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduwa		[%4] %5, %0
2:
"	: "=&r" (temp0), "=&r" (temp1), "=&r" (oldbit)
	: "HIr" (1UL << (nr & 31)), "r" (m), "i" (ASI_PL)
	: "cc");
	return oldbit != 0;
}

extern __inline__ int test_le_bit(int nr, __const__ void * addr)
{
	int			mask;
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#define find_first_zero_le_bit(addr, size) \
        find_next_zero_le_bit((addr), (size), 0)

extern __inline__ unsigned long __swab64(unsigned long value)
{
	return (((value>>56) & 0x00000000000000ff) |
		((value>>40) & 0x000000000000ff00) |
		((value>>24) & 0x0000000000ff0000) |
		((value>>8)  & 0x00000000ff000000) |
		((value<<8)  & 0x000000ff00000000) |
		((value<<24) & 0x0000ff0000000000) |
		((value<<40) & 0x00ff000000000000) |
		((value<<56) & 0xff00000000000000));
}     

extern __inline__ unsigned long find_next_zero_le_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~63UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 63UL;
	if(offset) {
		tmp = *(p++);
		tmp |= __swab64((~0UL >> (64-offset)));
		if(size < 64)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 64;
		result += 64;
	}
	while(size & ~63UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 64;
		size -= 64;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	return result + ffz(__swab64(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab64(tmp));
}

#ifdef __KERNEL__

#define ext2_set_bit			set_le_bit
#define ext2_clear_bit			clear_le_bit
#define ext2_test_bit  			test_le_bit
#define ext2_find_first_zero_bit	find_first_zero_le_bit
#define ext2_find_next_zero_bit		find_next_zero_le_bit

/* Bitmap functions for the minix filesystem.  */
#define minix_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */

#endif /* defined(_SPARC64_BITOPS_H) */
