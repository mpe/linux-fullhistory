#ifndef _ASM_PPC_BITOPS_H_
#define _ASM_PPC_BITOPS_H_

#include <asm/system.h>
#include <asm/byteorder.h>
#include <linux/kernel.h> /* for printk */

#define BIT(n) 1<<(n&0x1F)
typedef unsigned long BITFIELD;


/*
 * These are ifdef'd out here because using : "cc" as a constraing
 * results in errors from gcc. -- Cort
 */
#if 0
extern __inline__ int set_bit(int nr, void * addr)
{
	unsigned long old, t;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	
	if ((unsigned long)addr & 3)
		printk("set_bit(%lx, %p)\n", nr, addr);

	__asm__ __volatile__(
		"1:lwarx %0,0,%3 \n\t"
		"or	%1,%0,%2 \n\t"
		"stwcx.	%1,0,%3 \n\t"
		"bne	1b \n\t"
		: "=&r" (old), "=&r" (t)	/*, "=m" (*p)*/
		: "r" (mask), "r" (p)
		/*: "cc" */);

n	return (old & mask) != 0;
}

extern __inline__  unsigned long clear_bit(unsigned long nr, void *addr)
{
	unsigned long old, t;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	if ((unsigned long)addr & 3)
		printk("clear_bit(%lx, %p)\n", nr, addr);
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

	if ((unsigned long)addr & 3)
		printk("change_bit(%lx, %p)\n", nr, addr);
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

extern __inline__ int ffz(unsigned int x)
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

extern __inline__ unsigned long find_first_zero_bit(void * addr, unsigned long size)
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
extern __inline__ unsigned long find_next_zero_bit(void * addr, unsigned long size,
				 unsigned long offset)
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


#define _EXT2_HAVE_ASM_BITOPS_
#define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)


extern __inline__ int ext2_set_bit(int nr, void * addr)
{
#ifdef __KERNEL__
  int s = _disable_interrupts();
#endif
  int			mask;
  unsigned char	*ADDR = (unsigned char *) addr;
  int oldbit;
  
  ADDR += nr >> 3;
  mask = 1 << (nr & 0x07);
  oldbit = (*ADDR & mask) ? 1 : 0;
  *ADDR |= mask;
#ifdef __KERNEL__	
  _enable_interrupts(s);
#endif
  return oldbit;
}

extern __inline__ int ext2_clear_bit(int nr, void * addr)
{
#ifdef __KERNEL__
  int s = _disable_interrupts();
#endif
  int		mask;
  unsigned char	*ADDR = (unsigned char *) addr;
  int oldbit;
  
  ADDR += nr >> 3;
  mask = 1 << (nr & 0x07);
  oldbit = (*ADDR & mask) ? 1 : 0;
  *ADDR = *ADDR & ~mask;
#ifdef __KERNEL__	
  _enable_interrupts(s);
#endif
  return oldbit;
}


/* The following routine need not be atomic. */
extern __inline__ unsigned long test_bit(int nr, void *addr)
{
	return 1UL & (((__const__ unsigned int *) addr)[nr >> 5] >> (nr & 31));
}

extern __inline__ int ext2_test_bit(int nr, __const__ void * addr)
{
	int			mask;
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

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
		tmp |= le32_to_cpu(~0UL >> (32-offset));
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
	return result + ffz(le32_to_cpu(tmp) | (~0UL << size));
found_middle:
	return result + ffz(le32_to_cpu(tmp));
}

#endif /* _ASM_PPC_BITOPS_H */


