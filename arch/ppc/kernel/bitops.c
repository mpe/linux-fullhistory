/*
 * Copyright (C) 1996 Paul Mackerras.
 */

#include <linux/kernel.h>
#include <asm/bitops.h>

/*
 * I left these here since the problems with "cc" make it difficult to keep
 * them in bitops.h -- Cort
 */
void set_bit(int nr, volatile void *addr)
{
	unsigned int t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	if ((unsigned long)addr & 3)
		printk(KERN_ERR "set_bit(%x, %p)\n", nr, addr);
	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2
	or	%0,%0,%1
	stwcx.	%0,0,%2
	bne	1b"
	: "=&r" (t)		/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
	: "cc");
}

void clear_bit(int nr, volatile void *addr)
{
	unsigned int t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	if ((unsigned long)addr & 3)
		printk(KERN_ERR "clear_bit(%x, %p)\n", nr, addr);
	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2
	andc	%0,%0,%1
	stwcx.	%0,0,%2
	bne	1b"
	: "=&r" (t)		/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
	: "cc");
}

void change_bit(int nr, volatile void *addr)
{
	unsigned int t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	if ((unsigned long)addr & 3)
		printk(KERN_ERR "change_bit(%x, %p)\n", nr, addr);
	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2
	xor	%0,%0,%1
	stwcx.	%0,0,%2
	bne	1b"
	: "=&r" (t)		/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
	: "cc");
}

int test_and_set_bit(int nr, volatile void *addr)
{
	unsigned int old, t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	if ((unsigned long)addr & 3)
		printk(KERN_ERR "test_and_set_bit(%x, %p)\n", nr, addr);
	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3
	or	%1,%0,%2
	stwcx.	%1,0,%3
	bne	1b"
	: "=&r" (old), "=&r" (t)	/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
	: "cc");

	return (old & mask) != 0;
}

int test_and_clear_bit(int nr, volatile void *addr)
{
	unsigned int old, t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	if ((unsigned long)addr & 3)
		printk(KERN_ERR "test_and_clear_bit(%x, %p)\n", nr, addr);
	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3
	andc	%1,%0,%2
	stwcx.	%1,0,%3
	bne	1b"
	: "=&r" (old), "=&r" (t)	/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
	: "cc");

	return (old & mask) != 0;
}

int test_and_change_bit(int nr, volatile void *addr)
{
	unsigned int old, t;
	unsigned int mask = 1 << (nr & 0x1f);
	volatile unsigned int *p = ((volatile unsigned int *)addr) + (nr >> 5);

	if ((unsigned long)addr & 3)
		printk(KERN_ERR "test_and_change_bit(%x, %p)\n", nr, addr);
	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3
	xor	%1,%0,%2
	stwcx.	%1,0,%3
	bne	1b"
	: "=&r" (old), "=&r" (t)	/*, "=m" (*p)*/
	: "r" (mask), "r" (p)
	: "cc");

	return (old & mask) != 0;
}

/* I put it in bitops.h -- Cort */
#if 0
int ffz(unsigned int x)
{
	int n;

	x = ~x & (x+1);		/* set LS zero to 1, other bits to 0 */
	__asm__ ("cntlzw %0,%1" : "=r" (n) : "r" (x));
	return 31 - n;
}

/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */

int find_first_zero_bit(void * addr, int size)
{
	unsigned int * p = ((unsigned int *) addr);
	unsigned int result = 0;
	unsigned int tmp;

	if (size == 0)
		return 0;
	while (size & ~31UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;
	tmp |= ~0UL << size;
found_middle:
	return result + ffz(tmp);
}

/*
 * Find next zero bit in a bitmap reasonably efficiently..
 */
int find_next_zero_bit(void * addr, int size, int offset)
{
	unsigned int * p = ((unsigned int *) addr) + (offset >> 5);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

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
#endif
