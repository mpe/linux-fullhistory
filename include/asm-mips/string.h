/*
 * include/asm-mips/string.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994 by Ralf Baechle
 */

#ifndef _ASM_MIPS_STRING_H_
#define _ASM_MIPS_STRING_H_

#define __USE_PORTABLE_STRINGS_H_

extern inline char * strcpy(char * dest,const char *src)
{
  char *xdest = dest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%1,%1,1\n\t"
	"sb\t$1,(%0)\n\t"
	"bne\t$0,$1,1b\n\t"
	"addiu\t%0,%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
	: "=d" (dest), "=d" (src)
        : "0" (dest), "1" (src)
	: "$1","memory");

  return xdest;
}

extern inline char * strncpy(char *dest, const char *src, size_t n)
{
  char *xdest = dest;

  if (n == 0)
    return xdest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%2,%2,-1\n\t"
	"sb\t$1,(%0)\n\t"
	"beq\t$0,$1,2f\n\t"
	"addiu\t%0,%0,1\n\t"
	"bne\t$0,%2,1b\n\t"
	"addiu\t%1,%1,1\n"
	"2:\n\t"
	".set\tat\n\t"
	".set\treorder\n\t"
        : "=d" (dest), "=d" (src), "=d" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "$1","memory");

  return dest;
}

#define __USE_PORTABLE_strcat
#define __USE_PORTABLE_strncat

extern inline int strcmp(const char * cs,const char * ct)
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
	"bne\t$0,%2,1b\n\t"
	"lbu\t%2,(%0)\n"
	"move\t%2,$1\n"
	"2:\tsub\t%2,%2,$1\n"
	"3:\t.set\tat\n\t"
	".set\treorder\n\t"
	: "=d" (cs), "=d" (ct), "=d" (__res)
	: "0" (cs), "1" (ct)
	: "$1");

  return __res;
}

extern inline int strncmp(const char * cs,const char * ct,size_t count)
{
  char __res;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
       	"1:\tlbu\t%3,(%0)\n\t"
	"beq\t$0,%2,2f\n\t"
        "lbu\t$1,(%1)\n\t"
       	"addiu\t%2,%2,-1\n\t"
        "bne\t$1,%3,3f\n\t"
        "addiu\t%0,%0,1\n\t"
        "bne\t$0,%3,1b\n\t"
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

#define __USE_PORTABLE_strchr
#define __USE_PORTABLE_strlen
#define __USE_PORTABLE_strspn
#define __USE_PORTABLE_strpbrk
#define __USE_PORTABLE_strtok

extern inline void * memset(void * s,char c,size_t count)
{
  void *xs = s;

  if (!count)
    return xs;
  __asm__ __volatile__(
	".set\tnoreorder\n"
	"1:\tsb\t%3,(%0)\n\t"
	"addiu\t%1,%1,-1\n\t"
	"bne\t$0,%1,1b\n\t"
	"addiu\t%3,%3,1\n\t"
	".set\treorder"
	: "=d" (s), "=d" (count)
        : "0" (s), "d" (c), "1" (count)
	: "memory");

  return xs;
}

extern inline void * memcpy(void * to, const void * from, size_t n)
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
	"addiu\t%2,%2,-1\n\t"
	"bne\t$0,%2,1b\n\t"
	"addiu\t%0,%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=d" (to), "=d" (from), "=d" (n)
        : "0" (to), "1" (from), "2" (n)
        : "$1","memory" );
  return xto;
}

extern inline void * memmove(void * dest,const void * src, size_t n)
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
	"addiu\t%2,%2,-1\n\t"
	"bne\t$0,%2,1b\n\t"
	"addiu\t%0,%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=d" (dest), "=d" (src), "=d" (n)
        : "0" (dest), "1" (src), "2" (n)
        : "$1","memory" );
  else
    __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,-1(%1)\n\t"
	"addiu\t%1,%1,-1\n\t"
	"sb\t$1,-1(%0)\n\t"
	"addiu\t%2,%2,-1\n\t"
	"bne\t$0,%2,1b\n\t"
	"addiu\t%0,%0,-1\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=d" (dest), "=d" (src), "=d" (n)
        : "0" (dest+n), "1" (src+n), "2" (n)
        : "$1","memory" );
  return xdest;
}

#define __USE_PORTABLE_memcmp

#endif /* _ASM_MIPS_STRING_H_ */
