#ifndef _M68K_BITOPS_H
#define _M68K_BITOPS_H
/*
 * Copyright 1992, Linus Torvalds.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

/*
 * Require 68020 or better.
 *
 * They don't use the standard m680x0 bit ordering.
 * Instead, the use the standard m680x0 bitfield ordering.
 *
 * Thus, bit 0 is the MSB of addr; bit 32 is the MSB of (addr+1).
 */

extern __inline__ int set_bit(int nr,void * vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfset %2@{%1:#1}; sne %0"
	     : "=d" (retval) : "d" (nr), "a" (vaddr));

	return retval;
}

extern __inline__ int clear_bit(int nr, void * vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfclr %2@{%1:#1}; sne %0"
	     : "=d" (retval) : "d" (nr), "a" (vaddr));

	return retval;
}

extern __inline__ int change_bit(int nr, void * vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfchg %2@{%1:#1}; sne %0"
	     : "=d" (retval) : "d" (nr), "a" (vaddr));

	return retval;
}

extern __inline__ int test_bit(int nr, const void * vaddr)
{
	char retval;

	__asm__ __volatile__ ("bftst %2@{%1:#1}; sne %0"
	     : "=d" (retval) : "d" (nr), "a" (vaddr));

	return retval;
}

extern inline int find_first_zero_bit(void * vaddr, unsigned size)
{
	unsigned long res;
	unsigned long *p;
	unsigned long *addr = vaddr;

	if (!size)
		return 0;
	__asm__ __volatile__ ("    moveq #-1,d0\n\t"
			      "1:"
			      "    cmpl  %1@+,d0\n\t"
			      "    bne   2f\n\t"
			      "    subql #1,%0\n\t"
			      "    bne   1b\n\t"
			      "    bra   5f\n\t"
			      "2:"
			      "    movel %1@-,d0\n\t"
			      "    notl  d0\n\t"
			      "    bfffo d0{#0,#0},%0\n\t"
			      "5:"
			      : "=d" (res), "=a" (p)
			      : "0" ((size + 31) >> 5), "1" (addr)
			      : "d0");
	return ((p - addr) << 5) + res;
}

static inline int find_next_zero_bit (void *vaddr, int size,
				      int offset)
{
	unsigned long *addr = vaddr;
	unsigned long *p = addr + (offset >> 5);
	int set = 0, bit = offset & 31, res;

	if (bit) {
		/* Look for zero in first longword */
		__asm__("bfffo %1{#0,#0},%0"
			: "=d" (set)
			: "d" (~*p << bit));
		if (set < (32 - bit))
			return set + offset;
                set = 32 - bit;
		p++;
	}
	/* No zero yet, search remaining full bytes for a zero */
	res = find_first_zero_bit (p, size - 32 * (p - addr));
	return (offset + set + res);
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
extern inline unsigned long ffz(unsigned long word)
{
	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (word)
			      : "d" (~(word)));
	return word;
}

#endif /* _M68K_BITOPS_H */
