#ifndef __ASM_ARM_BITOPS_H
#define __ASM_ARM_BITOPS_H

/*
 * Copyright 1995, Russell King.
 * Various bits and pieces copyrights include:
 *  Linus Torvalds (test_bit).
 */

/*
 * These should be done with inline assembly.
 * All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

/*
 * Function prototypes to keep gcc -Wall happy
 */
extern void set_bit(int nr, volatile void * addr);
extern void clear_bit(int nr, volatile void * addr);
extern void change_bit(int nr, volatile void * addr);
extern int test_and_set_bit(int nr, volatile void * addr);
extern int test_and_clear_bit(int nr, volatile void * addr);
extern int test_and_change_bit(int nr, volatile void * addr);
extern int find_first_zero_bit(void * addr, unsigned size);
extern int find_next_zero_bit(void * addr, int size, int offset);

/*
 * This routine doesn't need to be atomic.
 */
extern __inline__ int test_bit(int nr, const void * addr)
{
    return ((unsigned char *) addr)[nr >> 3] & (1U << (nr & 7));
}	

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
extern __inline__ unsigned long ffz(unsigned long word)
{
	int k;

	word = ~word;
	k = 31;
	if (word & 0x0000ffff) { k -= 16; word <<= 16; }
	if (word & 0x00ff0000) { k -= 8;  word <<= 8;  }
	if (word & 0x0f000000) { k -= 4;  word <<= 4;  }
	if (word & 0x30000000) { k -= 2;  word <<= 2;  }
	if (word & 0x40000000) { k -= 1; }
        return k;
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

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#endif /* __KERNEL__ */

#ifdef __KERNEL__

#define ext2_set_bit			test_and_set_bit
#define ext2_clear_bit			test_and_clear_bit
#define ext2_test_bit			test_bit
#define ext2_find_first_zero_bit	find_first_zero_bit
#define ext2_find_next_zero_bit		find_next_zero_bit

/* Bitmap functions for the minix filesystem. */
#define minix_set_bit(nr,addr)		test_and_set_bit(nr,addr)
#define minix_clear_bit(nr,addr)	test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr)		test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size)	find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */

#endif /* _ARM_BITOPS_H */
