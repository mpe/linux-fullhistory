/*
 * include/asm-mips/segment.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Ralf Baechle
 *
 * Note that the quad functions are only being used for the 64 bit kernel and
 * therefore it isn't really important that they will be miscompiled for
 * 32-bit kernels.
 */
#ifndef __ASM_MIPS_SEGMENT_H
#define __ASM_MIPS_SEGMENT_H

#ifndef __LANGUAGE_ASSEMBLY__
/*
 * For memcpy()
 */
#include <linux/string.h>

/*
 * This is a gcc optimization barrier, which essentially
 * inserts a sequence point in the gcc RTL tree that gcc
 * can't move code around. This is needed when we enter
 * or exit a critical region (in this case around user-level
 * accesses that may sleep, and we can't let gcc optimize
 * global state around them).
 */
#define __gcc_barrier() __asm__ __volatile__("": : :"memory")

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 */
#define put_user(x,ptr) __put_user((unsigned long)(x),(ptr),sizeof(*(ptr)))
#define get_user(ptr) ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr))))

/*
 * This is a silly but good way to make sure that
 * the __put_user function is indeed always optimized,
 * and that we use the correct sizes..
 */
extern int bad_user_access_length(void);

/* I should make this use unaligned transfers etc.. */
static inline void __put_user(unsigned long x, void * y, int size)
{
	__gcc_barrier();
	switch (size) {
		case 1:
			*(char *) y = x;
			break;
		case 2:
			*(short *) y = x;
			break;
		case 4:
			*(int *) y = x;
			break;
		case 8:
			*(long *) y = x;
			break;
		default:
			bad_user_access_length();
	}
	__gcc_barrier();
}

/* I should make this use unaligned transfers etc.. */
static inline unsigned long __get_user(const void * y, int size)
{
	unsigned long result;

	__gcc_barrier();
	switch (size) {
		case 1:
			result = *(unsigned char *) y;
			break;
		case 2:
			result = *(unsigned short *) y;
			break;
		case 4:
			result = *(unsigned int *) y;
			break;
		case 8:
			result = *(unsigned long *) y;
			break;
		default:
			result = bad_user_access_length();
			break;
	}
	__gcc_barrier();

	return result;
}

#define get_fs_byte(addr) get_user((unsigned char *)(addr))
#define get_fs_word(addr) get_user((unsigned short *)(addr))
#define get_fs_long(addr) get_user((unsigned int *)(addr))
#define get_fs_quad(addr) get_user((unsigned long *)(addr))

#define put_fs_byte(x,addr) put_user((x),(char *)(addr))
#define put_fs_word(x,addr) put_user((x),(short *)(addr))
#define put_fs_long(x,addr) put_user((x),(int *)(addr))
#define put_fs_quad(x,addr) put_user((x),(long *)(addr))

static inline void memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	__gcc_barrier();
	memcpy(to, from, n);
	__gcc_barrier();
}

static inline void memcpy_tofs(void * to, const void * from, unsigned long n)
{
	__gcc_barrier();
	memcpy(to, from, n);
	__gcc_barrier();
}

/*
 * For segmented architectures, these are used to specify which segment
 * to use for the above functions.
 *
 * MIPS is not segmented, so these are just dummies.
 */

#define KERNEL_DS 0
#define USER_DS 1

static inline unsigned long get_fs(void)
{
	return USER_DS;
}

static inline unsigned long get_ds(void)
{
	return KERNEL_DS;
}

static inline void set_fs(unsigned long val)
{
}

#endif /* !__LANGUAGE_ASSEMBLY__ */

/*
 * Memory segments (32bit kernel mode addresses)
 */
#define KUSEG                   0x00000000
#define KSEG0                   0x80000000
#define KSEG1                   0xa0000000
#define KSEG2                   0xc0000000
#define KSEG3                   0xe0000000

/*
 * Returns the kernel segment base of a given address
 */
#define KSEGX(a)                (((unsigned long)(a)) & 0xe0000000)

/*
 * Returns the physical address of a KSEG0/KSEG1 address
 */
#define PHYSADDR(a)		(((unsigned long)(a)) & 0x1fffffff)

/*
 * Map an address to a certain kernel segment
 */
#define KSEG0ADDR(a)		((((unsigned long)(a)) & 0x1fffffff) | KSEG0)
#define KSEG1ADDR(a)		((((unsigned long)(a)) & 0x1fffffff) | KSEG1)
#define KSEG2ADDR(a)		((((unsigned long)(a)) & 0x1fffffff) | KSEG2)
#define KSEG3ADDR(a)		((((unsigned long)(a)) & 0x1fffffff) | KSEG3)

/*
 * Memory segments (64bit kernel mode addresses)
 */
#define XKUSEG                  0x0000 0000 0000 0000
#define XKSSEG                  0x4000 0000 0000 0000
#define XKPHYS                  0x8000 0000 0000 0000
#define XKSEG                   0xc000 0000 0000 0000
#define CKSEG0                  0xffff ffff 8000 0000
#define CKSEG1                  0xffff ffff a000 0000
#define CKSSEG                  0xffff ffff c000 0000
#define CKSEG3                  0xffff ffff e000 0000

#endif /* __ASM_MIPS_SEGMENT_H */
