/*
 * $Id: bitops.h,v 1.7 1997/08/03 00:12:07 paulus Exp $
 * bitops.h: Bit string operations on the ppc
 */

#ifndef _ASM_PPC_BITOPS_H_
#define _ASM_PPC_BITOPS_H_

#include <asm/system.h>
#include <asm/byteorder.h>

extern void set_bit(int nr, volatile void *addr);
extern void clear_bit(int nr, volatile void *addr);
extern void change_bit(int nr, volatile void *addr);
extern int test_and_set_bit(int nr, volatile void *addr);
extern int test_and_clear_bit(int nr, volatile void *addr);
extern int test_and_change_bit(int nr, volatile void *addr);

/*
 * These are if'd out here because using : "cc" as a constraint
 * results in errors from gcc. -- Cort
 * Besides, they need to be changed so we have both set_bit
 * and test_and_set_bit, etc.
 */
#if 0
extern __inline__ int set_bit(int nr, void * addr)
{
	unsigned long old, t;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	
	__asm__ __volatile__(
		"1:lwarx %0,0,%3 \n\t"
		"or	%1,%0,%2 \n\t"
		"stwcx.	%1,0,%3 \n\t"
		"bne	1b \n\t"
		: "=&r" (old), "=&r" (t)	/*, "=m" (*p)*/
		: "r" (mask), "r" (p)
		/*: "cc" */);

	return (old & mask) != 0;
}

extern __inline__  unsigned long clear_bit(unsigned long nr, void *addr)
{
	unsigned long old, t;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3
	andc	%1,%0,%2
	stwcx.	%1,0,%3
	bne	1b"
	: "=&r" (old), "=&r" (t)	/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
      /*: "cc"*/);

	return (old & mask) != 0;
}

extern __inline__ unsigned long change_bit(unsigned long nr, void *addr)
{
	unsigned long old, t;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3
	xor	%1,%0,%2
	stwcx.	%1,0,%3
	bne	1b"
	: "=&r" (old), "=&r" (t)	/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
      /*: "cc"*/);

	return (old & mask) != 0;
}
#endif

extern __inline__ unsigned long test_bit(int nr, __const__ volatile void *addr)
{
	__const__ unsigned int *p = (__const__ unsigned int *) addr;

	return (p[nr >> 5] >> (nr & 0x1f)) & 1UL;
}

extern __inline__ int ffz(unsigned int x)
{
	int n;

	if (x == ~0)
		return 32;
	x = ~x & (x+1);		/* set LS zero to 1, other bits to 0 */
	__asm__ ("cntlzw %0,%1" : "=r" (n) : "r" (x));
	return 31 - n;
}

/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */
#define find_first_zero_bit(addr, size) \
	find_next_zero_bit((addr), (size), 0)

extern __inline__ unsigned long find_next_zero_bit(void * addr,
	unsigned long size, unsigned long offset)
{
	unsigned int * p = ((unsigned int *) addr) + (offset >> 5);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *p++;
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (tmp != ~0U)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = *p++) != ~0U)
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


#define _EXT2_HAVE_ASM_BITOPS_

#ifdef __KERNEL__
/*
 * test_and_{set,clear}_bit guarantee atomicity without
 * disabling interrupts.
 */
#define ext2_set_bit(nr, addr)		test_and_set_bit((nr) ^ 0x18, addr)
#define ext2_clear_bit(nr, addr)	test_and_clear_bit((nr) ^ 0x18, addr)

#else
extern __inline__ int ext2_set_bit(int nr, void * addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;
	int oldbit;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	oldbit = (*ADDR & mask) ? 1 : 0;
	*ADDR |= mask;
	return oldbit;
}

extern __inline__ int ext2_clear_bit(int nr, void * addr)
{
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;
	int oldbit;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	oldbit = (*ADDR & mask) ? 1 : 0;
	*ADDR = *ADDR & ~mask;
	return oldbit;
}
#endif	/* __KERNEL__ */

extern __inline__ int ext2_test_bit(int nr, __const__ void * addr)
{
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	return (ADDR[nr >> 3] >> (nr & 7)) & 1;
}

/*
 * This implementation of ext2_find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h and modified for a big-endian machine.
 */

#define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)

extern __inline__ unsigned long ext2_find_next_zero_bit(void *addr,
	unsigned long size, unsigned long offset)
{
	unsigned int *p = ((unsigned int *) addr) + (offset >> 5);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = cpu_to_le32p(p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (tmp != ~0U)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = cpu_to_le32p(p++)) != ~0U)
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = cpu_to_le32p(p);
found_first:
	tmp |= ~0U << size;
found_middle:
	return result + ffz(tmp);
}

/* Bitmap functions for the minix filesystem.  */
#define minix_set_bit(nr,addr) ext2_set_bit(nr,addr)
#define minix_clear_bit(nr,addr) ext2_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) ext2_test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) ext2_find_first_zero_bit(addr,size)

#endif /* _ASM_PPC_BITOPS_H */
