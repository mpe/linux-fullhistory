/*
 * include/asm-mips/segment.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Ralf Baechle
 *
 */

#ifndef _ASM_MIPS_SEGMENT_H_
#define _ASM_MIPS_SEGMENT_H_

#include <linux/segment.h>

static inline unsigned char get_user_byte(const char * addr)
{
	register unsigned char _v;

	__asm__ ("lbu\t%0,%1":"=r" (_v):"r" (*addr));

	return _v;
}

#define get_fs_byte(addr) get_user_byte((char *)(addr))

static inline unsigned short get_user_word(const short *addr)
{
	unsigned short _v;

	__asm__ ("lhu\t%0,%1":"=r" (_v):"r" (*addr));

	return _v;
}

#define get_fs_word(addr) get_user_word((short *)(addr))

static inline unsigned long get_user_long(const int *addr)
{
	unsigned long _v;

	__asm__ ("lwu\t%0,%1":"=r" (_v):"r" (*addr)); \
	return _v;
}

#define get_fs_long(addr) get_user_long((int *)(addr))

static inline unsigned long get_user_dlong(const int *addr)
{
	unsigned long _v;

	__asm__ ("ld\t%0,%1":"=r" (_v):"r" (*addr)); \
	return _v;
}

#define get_fs_dlong(addr) get_user_dlong((int *)(addr))

static inline void put_user_byte(char val,char *addr)
{
__asm__ ("sb\t%0,%1": /* no outputs */ :"r" (val),"r" (*addr));
}

#define put_fs_byte(x,addr) put_user_byte((x),(char *)(addr))

static inline void put_user_word(short val,short * addr)
{
__asm__ ("sh\t%0,%1": /* no outputs */ :"r" (val),"r" (*addr));
}

#define put_fs_word(x,addr) put_user_word((x),(short *)(addr))

static inline void put_user_long(unsigned long val,int * addr)
{
__asm__ ("sw\t%0,%1": /* no outputs */ :"r" (val),"r" (*addr));
}

#define put_fs_long(x,addr) put_user_long((x),(int *)(addr))

static inline void put_user_dlong(unsigned long val,int * addr)
{
__asm__ ("sd\t%0,%1": /* no outputs */ :"r" (val),"r" (*addr));
}

#define put_fs_dlong(x,addr) put_user_dlong((x),(int *)(addr))

/*
 * These following two variables are defined in mips/head.S.
 */
extern unsigned long segment_fs;

static inline void __generic_memcpy_tofs(void * to, const void * from, unsigned long n)
{
  __asm__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%2)\n\t"
	"addiu\t%2,%2,1\n\t"
	"sb\t$1,(%1)\n\t"
	"addiu\t%0,%0,-1\n\t"
	"bne\t$0,%0,1b\n\t"
	"addiu\t%1,%1,1\n\t"
	".set\tat\n\t"
	".set\treorder"
	: /* no outputs */
	:"d" (n),"d" (((long) to)| segment_fs),"d" ((long) from)
	:"$1");
}

static inline void __constant_memcpy_tofs(void * to, const void * from, unsigned long n)
{
	/*
	 * Use put_user_byte to avoid trouble with alignment.
	 */
	switch (n) {
		case 0:
			return;
		case 1:
			put_user_byte(*(const char *) from, (char *) to);
			return;
		case 2:
			put_user_byte(*(const char *) from, (char *) to);
			put_user_byte(*(1+(const char *) from), 1+(char *) to);
			return;
		case 3:
			put_user_byte(*((const char *) from), (char *) to);
			put_user_byte(*(1+(const char *) from), 1+(char *) to);
			put_user_byte(*(2+(const char *) from), 2+(char *) to);
			return;
		case 4:
			put_user_byte(*((const char *) from), (char *) to);
			put_user_byte(*(1+(const char *) from), 1+(char *) to);
			put_user_byte(*(2+(const char *) from), 2+(char *) to);
			put_user_byte(*(3+(const char *) from), 3+(char *) to);
			return;
	}

	__generic_memcpy_tofs(to, from, n);

	return;
}

static inline void __generic_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
  __asm__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%2)\n\t"
	"addiu\t%2,%2,1\n\t"
	"sb\t$1,(%1)\n\t"
	"addiu\t%0,%0,-1\n\t"
	"bne\t$0,%0,1b\n\t"
	"addiu\t%1,%1,1\n\t"
	".set\tat\n\t"
	".set\treorder"
	: /* no outputs */
	:"d" (n),"d" ((long) to),"d" (((long) from | segment_fs))
	:"$1","memory");
}

static inline void __constant_memcpy_fromfs(void * to, const void * from, unsigned long n)
{
	/*
	 * Use put_user_byte to avoid trouble with alignment.
	 */
	switch (n) {
		case 0:
			return;
		case 1:
			*(char *)to = get_user_byte((const char *) from);
			return;
		case 2:
			*(char *) to = get_user_byte((const char *) from);
			*(char *) to = get_user_byte(1+(const char *) from);
			return;
		case 3:
			*(char *) to = get_user_byte((const char *) from);
			*(char *) to = get_user_byte(1+(const char *) from);
			*(char *) to = get_user_byte(2+(const char *) from);
			return;
		case 4:
			*(char *) to = get_user_byte((const char *) from);
			*(char *) to = get_user_byte(1+(const char *) from);
			*(char *) to = get_user_byte(2+(const char *) from);
			*(char *) to = get_user_byte(3+(const char *) from);
			return;
	}

	
	__generic_memcpy_fromfs(to, from, n);
	return;
}

#define memcpy_fromfs(to, from, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy_fromfs((to),(from),(n)) : \
 __generic_memcpy_fromfs((to),(from),(n)))

#define memcpy_tofs(to, from, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy_tofs((to),(from),(n)) : \
 __generic_memcpy_tofs((to),(from),(n)))

static inline unsigned long get_fs(void)
{
	return segment_fs;
}

static inline unsigned long get_ds(void)
{
	return KERNEL_DS;
}

static inline void set_fs(unsigned long val)
{
	segment_fs = val;
}

#endif /* _ASM_MIPS_SEGMENT_H_ */
