/* $Id: bitops.h,v 1.23 1996/04/20 07:54:35 davem Exp $
 * bitops.h: Bit string operations on the Sparc.
 *
 * Copyright 1995, David S. Miller (davem@caip.rutgers.edu).
 */

#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

#include <linux/kernel.h>

#ifdef __KERNEL__
#include <asm/system.h>
#endif

#ifdef __SMP__

#define SMPVOL volatile

#else

#define SMPVOL

#endif

/* Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */

extern __inline__ unsigned long set_bit(unsigned long nr, SMPVOL void *addr)
{
	int mask, flags;
	unsigned long *ADDR = (unsigned long *) addr;
	unsigned long oldbit;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	save_flags(flags); cli();
	oldbit = (mask & *ADDR);
	*ADDR |= mask;
	restore_flags(flags);
	return oldbit != 0;
}

extern __inline__ unsigned long clear_bit(unsigned long nr, SMPVOL void *addr)
{
	int mask, flags;
	unsigned long *ADDR = (unsigned long *) addr;
	unsigned long oldbit;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	save_flags(flags); cli();
	oldbit = (mask & *ADDR);
	*ADDR &= ~mask;
	restore_flags(flags);
	return oldbit != 0;
}

extern __inline__ unsigned long change_bit(unsigned long nr, SMPVOL void *addr)
{
	int mask, flags;
	unsigned long *ADDR = (unsigned long *) addr;
	unsigned long oldbit;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	save_flags(flags); cli();
	oldbit = (mask & *ADDR);
	*ADDR ^= mask;
	restore_flags(flags);
	return oldbit != 0;
}

/* The following routine need not be atomic. */
extern __inline__ unsigned long test_bit(int nr, const SMPVOL void *addr)
{
	return ((1UL << (nr & 31)) & (((const unsigned int *) addr)[nr >> 5])) != 0;
}

/* The easy/cheese version for now. */
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;

	while(word & 1) {
		result++;
		word >>= 1;
	}
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
	tmp |= ~0UL >> size;
found_middle:
	return result + ffz(tmp);
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

/* Now for the ext2 filesystem bit operations and helper routines. */

extern __inline__ int ext2_set_bit(int nr,void * addr)
{
	int		mask, retval, flags;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	save_flags(flags); cli();
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	restore_flags(flags);
	return retval;
}

extern __inline__ int ext2_clear_bit(int nr, void * addr)
{
	int		mask, retval, flags;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	save_flags(flags); cli();
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	restore_flags(flags);
	return retval;
}

extern __inline__ int ext2_test_bit(int nr, const void * addr)
{
	int			mask;
	const unsigned char	*ADDR = (const unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)

extern __inline__ unsigned long ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		tmp = *(p++);
		tmp |= ~0UL << (32-offset);
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
found_middle:
	tmp = ((tmp>>24) | ((tmp>>8)&0xff00) | ((tmp<<8)&0xff0000) | (tmp<<24));
	return result + ffz(tmp);
}

#endif /* defined(_SPARC_BITOPS_H) */

