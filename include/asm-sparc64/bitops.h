/* $Id: bitops.h,v 1.25 1998/07/26 03:05:51 davem Exp $
 * bitops.h: Bit string operations on the V9.
 *
 * Copyright 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_BITOPS_H
#define _SPARC64_BITOPS_H

#include <asm/byteorder.h>
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
	unsigned long * m = ((unsigned long *) addr) + (nr >> 6);
	unsigned long oldbit;

	__asm__ __volatile__("
1:	ldx		[%2], %%g7
	andcc		%%g7, %1, %0
	bne,pn		%%xcc, 2f
	 xor		%%g7, %1, %%g5
	casx 		[%2], %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%xcc, 1b
	 nop
2:
"	: "=&r" (oldbit)
	: "HIr" (1UL << (nr & 63)), "r" (m)
	: "g5", "g7", "cc", "memory");
	return oldbit != 0;
}

extern __inline__ void set_bit(unsigned long nr, void *addr)
{
	unsigned long * m = ((unsigned long *) addr) + (nr >> 6);

	__asm__ __volatile__("
1:	ldx		[%1], %%g7
	andcc		%%g7, %0, %%g0
	bne,pn		%%xcc, 2f
	 xor		%%g7, %0, %%g5
	casx 		[%1], %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%xcc, 1b
	 nop
2:
"	: /* no outputs */
	: "HIr" (1UL << (nr & 63)), "r" (m)
	: "g5", "g7", "cc", "memory");
}

extern __inline__ unsigned long test_and_clear_bit(unsigned long nr, void *addr)
{
	unsigned long * m = ((unsigned long *) addr) + (nr >> 6);
	unsigned long oldbit;

	__asm__ __volatile__("
1:	ldx		[%2], %%g7
	andcc		%%g7, %1, %0
	be,pn		%%xcc, 2f
	 xor		%%g7, %1, %%g5
	casx 		[%2], %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%xcc, 1b
	 nop
2:
"	: "=&r" (oldbit)
	: "HIr" (1UL << (nr & 63)), "r" (m)
	: "g5", "g7", "cc", "memory");
	return oldbit != 0;
}

extern __inline__ void clear_bit(unsigned long nr, void *addr)
{
	unsigned long * m = ((unsigned long *) addr) + (nr >> 6);

	__asm__ __volatile__("
1:	ldx		[%1], %%g7
	andcc		%%g7, %0, %%g0
	be,pn		%%xcc, 2f
	 xor		%%g7, %0, %%g5
	casx 		[%1], %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%xcc, 1b
	 nop
2:
"	: /* no outputs */
	: "HIr" (1UL << (nr & 63)), "r" (m)
	: "g5", "g7", "cc", "memory");
}

extern __inline__ unsigned long test_and_change_bit(unsigned long nr, void *addr)
{
	unsigned long * m = ((unsigned long *) addr) + (nr >> 6);
	unsigned long oldbit;

	__asm__ __volatile__("
1:	ldx		[%2], %%g7
	and		%%g7, %1, %0
	xor		%%g7, %1, %%g5
	casx 		[%2], %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%xcc, 1b
	 nop
"	: "=&r" (oldbit)
	: "HIr" (1UL << (nr & 63)), "r" (m)
	: "g5", "g7", "cc", "memory");
	return oldbit != 0;
}

extern __inline__ void change_bit(unsigned long nr, void *addr)
{
	unsigned long * m = ((unsigned long *) addr) + (nr >> 6);

	__asm__ __volatile__("
1:	ldx		[%1], %%g7
	xor		%%g7, %0, %%g5
	casx 		[%1], %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%xcc, 1b
	 nop
"	: /* no outputs */
	: "HIr" (1UL << (nr & 63)), "r" (m)
	: "g5", "g7", "cc", "memory");
}

extern __inline__ unsigned long test_bit(int nr, __const__ void *addr)
{
	return 1UL & (((__const__ long *) addr)[nr >> 6] >> (nr & 63));
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
#if 1 /* def EASY_CHEESE_VERSION */
	result = 0;
	while(word & 1) {
		result++;
		word >>= 1;
	}
#else
	unsigned long tmp;

	result = 0;	
	tmp = ~word & -~word;
	if (!(unsigned)tmp) {
		tmp >>= 32;
		result = 32;
	}
	if (!(unsigned short)tmp) {
		tmp >>= 16;
		result += 16;
	}
	if (!(unsigned char)tmp) {
		tmp >>= 8;
		result += 8;
	}
	if (tmp & 0xf0) result += 4;
	if (tmp & 0xcc) result += 2;
	if (tmp & 0xaa) result ++;
#endif
#endif
	return result;
}

#ifdef __KERNEL__

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

#define ffs(x) generic_ffs(x)

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#ifdef ULTRA_HAS_POPULATION_COUNT

extern __inline__ unsigned int hweight32(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xffffffff));
	return res;
}

extern __inline__ unsigned int hweight16(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xffff));
	return res;
}

extern __inline__ unsigned int hweight8(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xff));
	return res;
}

#else

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#endif
#endif /* __KERNEL__ */

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

extern __inline__ unsigned long find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 6);
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

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

/* Now for the ext2 filesystem bit operations and helper routines.
 * Note the usage of the little endian ASI's, werd, V9 is supreme.
 */
extern __inline__ int set_le_bit(int nr,void * addr)
{
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);
	unsigned long oldbit;

	__asm__ __volatile__("
1:	lduwa		[%2] %3, %%g7
	andcc		%%g7, %1, %0
	bne,pn		%%icc, 2f
	 xor		%%g7, %1, %%g5
	casa 		[%2] %3, %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%icc, 1b
	 nop
2:
"	: "=&r" (oldbit)
	: "HIr" (1UL << (nr & 31)), "r" (m), "i" (ASI_PL)
	: "g5", "g7", "cc", "memory");
	return oldbit != 0;
}

extern __inline__ int clear_le_bit(int nr, void * addr)
{
	unsigned int * m = ((unsigned int *) addr) + (nr >> 5);
	unsigned long oldbit;

	__asm__ __volatile__("
1:	lduwa		[%2] %3, %%g7
	andcc		%%g7, %1, %0
	be,pn		%%icc, 2f
	 xor		%%g7, %1, %%g5
	casa 		[%2] %3, %%g7, %%g5
	cmp		%%g7, %%g5
	bne,pn		%%icc, 1b
	 nop
2:
"	: "=&r" (oldbit)
	: "HIr" (1UL << (nr & 31)), "r" (m), "i" (ASI_PL)
	: "g5", "g7", "cc", "memory");
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

extern __inline__ unsigned long find_next_zero_le_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 6);
	unsigned long result = offset & ~63UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 63UL;
	if(offset) {
		tmp = __swab64p(p++);
		tmp |= (~0UL >> (64-offset));
		if(size < 64)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 64;
		result += 64;
	}
	while(size & ~63) {
		if(~(tmp = __swab64p(p++)))
			goto found_middle;
		result += 64;
		size -= 64;
	}
	if(!size)
		return result;
	tmp = __swab64p(p);
found_first:
	tmp |= (~0UL << size);
found_middle:
	return result + ffz(tmp);
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
