/*
 * include/asm-mips/string.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994, 1995, 1996 by Ralf Baechle
 */
#ifndef __ASM_MIPS_STRING_H
#define __ASM_MIPS_STRING_H

#define __HAVE_ARCH_STRCPY
extern __inline__ char *strcpy(char *__dest, __const__ char *__src)
{
  char *__xdest = __dest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%1,1\n\t"
	"sb\t$1,(%0)\n\t"
	"bnez\t$1,1b\n\t"
	"addiu\t%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
	: "=r" (__dest), "=r" (__src)
        : "0" (__dest), "1" (__src)
	: "$1","memory");

  return __xdest;
}

#define __HAVE_ARCH_STRNCPY
extern __inline__ char *strncpy(char *__dest, __const__ char *__src, size_t __n)
{
  char *__xdest = __dest;

  if (__n == 0)
    return __xdest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"subu\t%2,1\n\t"
	"sb\t$1,(%0)\n\t"
	"beqz\t$1,2f\n\t"
	"addiu\t%0,1\n\t"
	"bnez\t%2,1b\n\t"
	"addiu\t%1,1\n"
	"2:\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=r" (__dest), "=r" (__src), "=r" (__n)
        : "0" (__dest), "1" (__src), "2" (__n)
        : "$1","memory");

  return __dest;
}

#define __HAVE_ARCH_STRCMP
extern __inline__ int strcmp(__const__ char *__cs, __const__ char *__ct)
{
  int __res;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n\t"
	"lbu\t%2,(%0)\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%0,1\n\t"
	"bne\t$1,%2,2f\n\t"
	"addiu\t%1,1\n\t"
	"bnez\t%2,1b\n\t"
	"lbu\t%2,(%0)\n\t"
#if _MIPS_ISA == _MIPS_ISA_MIPS1
	"nop\n\t"
#endif
	"move\t%2,$1\n"
	"2:\tsubu\t%2,$1\n"
	"3:\t.set\tat\n\t"
	".set\treorder"
	: "=r" (__cs), "=r" (__ct), "=r" (__res)
	: "0" (__cs), "1" (__ct)
	: "$1");

  return __res;
}

#define __HAVE_ARCH_STRNCMP
extern __inline__ int strncmp(__const__ char *__cs, __const__ char *__ct, size_t __count)
{
  char __res;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
       	"1:\tlbu\t%3,(%0)\n\t"
	"beqz\t%2,2f\n\t"
        "lbu\t$1,(%1)\n\t"
       	"subu\t%2,1\n\t"
        "bne\t$1,%3,3f\n\t"
        "addiu\t%0,1\n\t"
        "bnez\t%3,1b\n\t"
        "addiu\t%1,1\n"
	"2:\tmove\t%3,$1\n"
	"3:\tsubu\t%3,$1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=r" (__cs), "=r" (__ct), "=r" (__count), "=r" (__res)
        : "0" (__cs), "1" (__ct), "2" (__count)
	: "$1");

  return __res;
}

#define __HAVE_ARCH_MEMSET
extern void *memset(void *__s, char __c, size_t __count);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *__to, __const__ void *__from, size_t __n);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *__dest, __const__ void *__src, size_t __n);

#define __HAVE_ARCH_BCOPY
extern char * bcopy(const char * src, char * dest, int count);

#define __HAVE_ARCH_MEMSCAN
extern __inline__ void *memscan(void *__addr, int __c, size_t __size)
{
	char *__end = (char *)__addr + __size;

	if (!__size)
		return __addr;
	__asm__(".set\tnoreorder\n\t"
		".set\tnoat\n"
		"1:\tlbu\t$1,(%0)\n\t"
#if _MIPS_ISA == _MIPS_ISA_MIPS1
		"nop\n\t"
#endif
		"beq\t$1,%3,2f\n\t"
		"addiu\t%0,1\n\t"
		"bne\t%0,%2,1b\n\t"
		"nop\n\t"
		".set\tat\n\t"
		".set\treorder\n"
		"2:"
		: "=r" (__addr)
		: "0" (__addr), "1" (__end), "r" (__c)
		: "$1");

	return __addr;
}

#endif /* __ASM_MIPS_STRING_H */
