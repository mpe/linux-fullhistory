/*
 * include/asm-mips/bitops.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994 by Ralf Baechle
 */
#ifndef __ASM_MIPS_BITOPS_H
#define __ASM_MIPS_BITOPS_H

#include <asm/mipsregs.h>

#if defined(__R4000__)

/*
 * The following functions will only work for the R4000!
 */
extern __inline__ int set_bit(int nr, void *addr)
{
	int	mask, retval, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
		retval = (mask & mw) != 0;
		}
	while (!store_conditional(addr, mw|mask));

	return retval;
}

extern __inline__ int clear_bit(int nr, void *addr)
{
	int	mask, retval, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
		retval = (mask & mw) != 0;
		}
	while (!store_conditional(addr, mw & ~mask));

	return retval;
}

extern __inline__ int change_bit(int nr, void *addr)
{
	int	mask, retval, mw;

	addr += ((nr >> 3) & ~3);
	mask = 1 << (nr & 0x1f);
	do {
		mw = load_linked(addr);
		retval = (mask & mw) != 0;
		}
	while (!store_conditional(addr, mw ^ mask));

	return retval;
}

extern __inline__ int find_first_zero_bit (void *addr, unsigned size)
{
	int res;

	if (!size)
		return 0;

	__asm__(".set\tnoreorder\n\t"
		".set\tnoat\n"
		"1:\tsubu\t$1,%2,%0\n\t"
		"blez\t$1,2f\n\t"
		"lw\t$1,(%4)\n\t"
		"addiu\t%4,%4,4\n\t"
		"beql\t%1,$1,1b\n\t"
		"addiu\t%0,%0,32\n\t"
		"li\t%1,1\n"
		"1:\tand\t%4,$1,%1\n\t"
		"beq\t$0,%4,2f\n\t"
		"sll\t%1,%1,1\n\t"
		"bne\t$0,%1,1b\n\t"
		"add\t%0,%0,1\n\t"
		".set\tat\n\t"
		".set\treorder\n"
		"2:"
		: "=d" (res)
		: "d" ((unsigned int) 0xffffffff),
		  "d" (size),
		  "0" ((signed int) 0),
		  "d" (addr)
		: "$1");

	return res;
}

#else /* !defined(__R4000__) */

#define __USE_PORTABLE_STRINGS_H

#define __USE_GENERIC_set_bit
#define __USE_GENERIC_clear_bit
#define __USE_GENERIC_change_bit
#define __USE_GENERIC_find_first_zero_bit

#endif /* !defined(__R4000__) */

extern __inline__ int test_bit(int nr, void *addr)
{
	int	mask;
	unsigned long	*a;

	a = addr;
	addr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *a) != 0);
}

extern __inline__ int find_next_zero_bit (void * addr, int size, int offset)
{
	unsigned long * p = ((unsigned long *) addr) + (offset >> 5);
	int set = 0, bit = offset & 31, res;
	
	if (bit) {
		/*
		 * Look for zero in first byte
		 */
		__asm__(".set\tnoreorder\n\t"
			".set\tnoat\n"
			"1:\tand\t$1,%2,%1\n\t"
			"beq\t$0,$1,2f\n\t"
			"sll\t%2,%2,1\n\t"
			"bne\t$0,%2,1b\n\t"
			"addiu\t%0,%0,1\n\t"
			".set\tat\n\t"
			".set\treorder\n"
			: "=r" (set)
			: "r" (*p >> bit),
			  "r" (1),
			  "0" (0)
			: "$1");
		if (set < (32 - bit))
			return set + offset;
		set = 32 - bit;
		p++;
	}
	/*
	 * No zero yet, search remaining full bytes for a zero
	 */
	res = find_first_zero_bit (p, size - 32 * (p - (unsigned long *) addr));
	return (offset + set + res);
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned int	__res;
	unsigned int	mask = 1;

	__asm__ __volatile__ (
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"li\t%2,1\n"
		"1:\tand\t$1,%2,%1\n\t"
		"beq\t$0,$1,2f\n\t"
		"sll\t%2,%2,1\n\t"
		"bne\t$0,%2,1b\n\t"
		"add\t%0,%0,1\n\t"
		".set\tat\n\t"
		".set\treorder\n"
		"2:\n\t"
		: "=r" (__res), "=r" (word), "=r" (mask)
		: "1" (~(word)),
		  "2" (mask),
		  "0" (0)
		: "$1");

	return __res;
}

#ifdef __USE_PORTABLE_BITOPS_H
#include <asm-generic/bitops.h>
#endif

#endif /* __ASM_MIPS_BITOPS_H */
