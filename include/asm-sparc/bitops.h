/* $Id: bitops.h,v 1.39 1996/12/10 06:06:35 davem Exp $
 * bitops.h: Bit string operations on the Sparc.
 *
 * Copyright 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1996 Eddie C. Dost   (ecd@skynet.be)
 */

#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

#include <linux/kernel.h>

#ifndef __KERNEL__

/* User mode bitops, defined here for convenience. Note: these are not
 * atomic, so packages like nthreads should do some locking around these
 * themself.
 */

#define __SMPVOL

extern __inline__ unsigned long set_bit(unsigned long nr, void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	ld	[%0], %%g3
	or	%%g3, %2, %%g2
	st	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (unsigned long) ADDR;
}

extern __inline__ unsigned long clear_bit(unsigned long nr, void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	ld	[%0], %%g3
	andn	%%g3, %2, %%g2
	st	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (unsigned long) ADDR;
}

extern __inline__ unsigned long change_bit(unsigned long nr, void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	ld	[%0], %%g3
	xor	%%g3, %2, %%g2
	st	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (unsigned long) ADDR;
}

#else /* __KERNEL__ */

#include <asm/system.h>

#ifdef __SMP__
#define __SMPVOL volatile
#else
#define __SMPVOL
#endif

/* Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */

extern __inline__ unsigned long set_bit(unsigned long nr, __SMPVOL void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	rd	%%psr, %%g3
	nop
	nop
	nop
	andcc	%%g3, %3, %%g0
	bne	1f
	 nop
	wr	%%g3, %3, %%psr
	nop
	nop
	nop
1:
	ld	[%0], %%g4
	or	%%g4, %2, %%g2
	andcc	%%g3, %3, %%g0
	st	%%g2, [%0]
	bne	1f
	 nop
	wr	%%g3, 0x0, %%psr
	nop
	nop
	nop
1:
	and	%%g4, %2, %0
"	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask), "i" (PSR_PIL)
	: "g2", "g3", "g4");

	return (unsigned long) ADDR;
}

extern __inline__ unsigned long clear_bit(unsigned long nr, __SMPVOL void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	rd	%%psr, %%g3
	nop
	nop
	nop
	andcc	%%g3, %3, %%g0
	bne	1f
	 nop
	wr	%%g3, %3, %%psr
	nop
	nop
	nop
1:
	ld	[%0], %%g4
	andn	%%g4, %2, %%g2
	andcc	%%g3, %3, %%g0
	st	%%g2, [%0]
	bne	1f
	 nop
	wr	%%g3, 0x0, %%psr
	nop
	nop
	nop
1:
	and	%%g4, %2, %0
"	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask), "i" (PSR_PIL)
	: "g2", "g3", "g4");

	return (unsigned long) ADDR;
}

extern __inline__ unsigned long change_bit(unsigned long nr, __SMPVOL void *addr)
{
	int mask;
	unsigned long *ADDR = (unsigned long *) addr;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	__asm__ __volatile__("
	rd	%%psr, %%g3
	nop
	nop
	nop
	andcc	%%g3, %3, %%g0
	bne	1f
	 nop
	wr	%%g3, %3, %%psr
	nop
	nop
	nop
1:
	ld	[%0], %%g4
	xor	%%g4, %2, %%g2
	andcc	%%g3, %3, %%g0
	st	%%g2, [%0]
	bne	1f
	 nop
	wr	%%g3, 0x0, %%psr
	nop
	nop
	nop
1:
	and	%%g4, %2, %0
"	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask), "i" (PSR_PIL)
	: "g2", "g3", "g4");

	return (unsigned long) ADDR;
}

#endif /* __KERNEL__ */

/* The following routine need not be atomic. */
extern __inline__ unsigned long test_bit(int nr, __const__ __SMPVOL void *addr)
{
	return 1UL & (((__const__ unsigned int *) addr)[nr >> 5] >> (nr & 31));
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
	tmp |= ~0UL << size;
found_middle:
	return result + ffz(tmp);
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

#ifndef __KERNEL__

extern __inline__ int set_le_bit(int nr, void *addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	ldub	[%0], %%g3
	or	%%g3, %2, %%g2
	stb	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (int) ADDR;
}

extern __inline__ int clear_le_bit(int nr, void *addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	ldub	[%0], %%g3
	andn	%%g3, %2, %%g2
	stb	%%g2, [%0]
	and	%%g3, %2, %0
	"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g2", "g3");

	return (int) ADDR;
}

#else /* __KERNEL__ */

/* Now for the ext2 filesystem bit operations and helper routines. */

extern __inline__ int set_le_bit(int nr,void * addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	rd	%%psr, %%g3
	nop
	nop
	nop
	andcc	%%g3, %3, %%g0
	bne	1f
	 nop
	wr	%%g3, %3, %%psr
	nop
	nop
	nop
1:
	ldub	[%0], %%g4
	or	%%g4, %2, %%g2
	andcc	%%g3, %3, %%g0
	stb	%%g2, [%0]
	bne	1f
	 nop
	wr	%%g3, 0x0, %%psr
	nop
	nop
	nop
1:
	and	%%g4, %2, %0
"	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask), "i" (PSR_PIL)
	: "g2", "g3", "g4");

	return (int) ADDR;
}

extern __inline__ int clear_le_bit(int nr, void * addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("
	rd	%%psr, %%g3
	nop
	nop
	nop
	andcc	%%g3, %3, %%g0
	bne	1f
	 nop
	wr	%%g3, %3, %%psr
	nop
	nop
	nop
1:
	ldub	[%0], %%g4
	andn	%%g4, %2, %%g2
	andcc	%%g3, %3, %%g0
	stb	%%g2, [%0]
	bne	1f
	 nop
	wr	%%g3, 0x0, %%psr
	nop
	nop
	nop
1:
	and	%%g4, %2, %0
"	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask), "i" (PSR_PIL)
	: "g2", "g3", "g4");

	return (int) ADDR;
}

#endif /* __KERNEL__ */

extern __inline__ int test_le_bit(int nr, __const__ void * addr)
{
	int			mask;
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#ifdef __KERNEL__

#define ext2_set_bit   set_le_bit
#define ext2_clear_bit clear_le_bit
#define ext2_test_bit  test_le_bit

#endif /* __KERNEL__ */

#define find_first_zero_le_bit(addr, size) \
        find_next_zero_le_bit((addr), (size), 0)

extern __inline__ unsigned long __swab32(unsigned long value)
{
	return((value>>24) | ((value>>8)&0xff00) |
	       ((value<<8)&0xff0000) | (value<<24));
}     

extern __inline__ unsigned long find_next_zero_le_bit(void *addr, unsigned long size, unsigned long offset)
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
		tmp |= __swab32(~0UL >> (32-offset));
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
	return result + ffz(__swab32(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab32(tmp));
}

#ifdef __KERNEL__

#define ext2_find_first_zero_bit     find_first_zero_le_bit
#define ext2_find_next_zero_bit      find_next_zero_le_bit

#endif /* __KERNEL__ */

#endif /* defined(_SPARC_BITOPS_H) */
