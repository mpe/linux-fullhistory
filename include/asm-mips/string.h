/*
 * include/asm-mips/string.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994, 1995  Waldorf Electronics
 * written by Ralf Baechle
 */

#ifndef __ASM_MIPS_STRING_H
#define __ASM_MIPS_STRING_H

#include <asm/mipsregs.h>

extern __inline__ char * strcpy(char * dest, const char *src)
{
  char *xdest = dest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%1,%1,1\n\t"
	"sb\t$1,(%0)\n\t"
	"bnez\t$1,1b\n\t"
	"addiu\t%0,%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
	: "=r" (dest), "=r" (src)
        : "0" (dest), "1" (src)
	: "$1","memory");

  return xdest;
}

extern __inline__ char * strncpy(char *dest, const char *src, size_t n)
{
  char *xdest = dest;

  if (n == 0)
    return xdest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"subu\t%2,%2,1\n\t"
	"sb\t$1,(%0)\n\t"
	"beqz\t$1,2f\n\t"
	"addiu\t%0,%0,1\n\t"
	"bnez\t%2,1b\n\t"
	"addiu\t%1,%1,1\n"
	"2:\n\t"
	".set\tat\n\t"
	".set\treorder\n\t"
        : "=r" (dest), "=r" (src), "=r" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "$1","memory");

  return dest;
}

extern __inline__ int strcmp(const char * cs, const char * ct)
{
  int __res;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n\t"
	"lbu\t%2,(%0)\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%0,%0,1\n\t"
	"bne\t$1,%2,2f\n\t"
	"addiu\t%1,%1,1\n\t"
	"bnez\t%2,1b\n\t"
	"lbu\t%2,(%0)\n\t"
	STR(FILL_LDS) "\n\t"
	"move\t%2,$1\n"
	"2:\tsub\t%2,%2,$1\n"
	"3:\t.set\tat\n\t"
	".set\treorder\n\t"
	: "=d" (cs), "=d" (ct), "=d" (__res)
	: "0" (cs), "1" (ct)
	: "$1");

  return __res;
}

extern __inline__ int strncmp(const char * cs, const char * ct, size_t count)
{
  char __res;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
       	"1:\tlbu\t%3,(%0)\n\t"
	"beqz\t%2,2f\n\t"
        "lbu\t$1,(%1)\n\t"
       	"addiu\t%2,%2,-1\n\t"
        "bne\t$1,%3,3f\n\t"
        "addiu\t%0,%0,1\n\t"
        "bnez\t%3,1b\n\t"
        "addiu\t%1,%1,1\n"
	"2:\tmove\t%3,$1\n"
	"3:\tsub\t%3,%3,$1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=d" (cs), "=d" (ct), "=d" (count), "=d" (__res)
        : "0" (cs), "1" (ct), "2" (count)
	: "$1");

  return __res;
}

extern __inline__ void * memset(void * s, int c, size_t count)
{
  void *xs = s;

  if (!count)
    return xs;
  __asm__ __volatile__(
	".set\tnoreorder\n"
	"1:\tsb\t%3,(%0)\n\t"
	"bne\t%0,%1,1b\n\t"
	"addiu\t%0,%0,1\n\t"
	".set\treorder"
	: "=r" (s), "=r" (count)
        : "0" (s), "r" (c), "1" (s + count - 1)
	: "memory");

  return xs;
}

extern __inline__ void * memcpy(void * to, const void * from, size_t n)
{
  void *xto = to;

  if (!n)
    return xto;
  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%1,%1,1\n\t"
	"sb\t$1,(%0)\n\t"
	"subu\t%2,%2,1\n\t"
	"bnez\t%2,1b\n\t"
	"addiu\t%0,%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=r" (to), "=r" (from), "=r" (n)
        : "0" (to), "1" (from), "2" (n)
        : "$1","memory" );
  return xto;
}

extern __inline__ void * memmove(void * dest,const void * src, size_t n)
{
  void *xdest = dest;

  if (!n)
    return xdest;

  if (dest < src)
    __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%1,%1,1\n\t"
	"sb\t$1,(%0)\n\t"
	"subu\t%2,%2,1\n\t"
	"bnez\t%2,1b\n\t"
	"addiu\t%0,%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=r" (dest), "=r" (src), "=r" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "$1","memory" );
  else
    __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,-1(%1)\n\t"
	"subu\t%1,%1,1\n\t"
	"sb\t$1,-1(%0)\n\t"
	"subu\t%2,%2,1\n\t"
	"bnez\t%2,1b\n\t"
	"subu\t%0,%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=r" (dest), "=r" (src), "=r" (n)
        : "0" (dest+n), "1" (src+n), "2" (n)
        : "$1","memory" );
  return xdest;
}

extern __inline__ void * memscan(void * addr, int c, size_t size)
{
	if (!size)
		return addr;
	__asm__(".set\tnoreorder\n\t"
		".set\tnoat\n"
		"1:\tbeq\t$0,%1,2f\n\t"
		"lbu\t$1,(%0)\n\t"
		"subu\t%1,%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,%0,1\n\t"
		".set\tat\n\t"
		".set\treorder\n"
		"2:"
		: "=r" (addr), "=r" (size)
		: "0" (addr), "1" (size), "r" (c)
		: "$1");

	return addr;
}

#endif /* __ASM_MIPS_STRING_H */
