/*
 *  include/asm-s390/string.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#ifndef _S390_STRING_H_
#define _S390_STRING_H_

#ifdef __KERNEL__

#ifndef _LINUX_TYPES_H
#include <linux/types.h>
#endif

#define __HAVE_ARCH_MEMCHR
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_STRCAT
#define __HAVE_ARCH_STRCMP
#define __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRLEN
#define __HAVE_ARCH_STRNCPY

#undef __HAVE_ARCH_MEMMOVE
#undef __HAVE_ARCH_STRNICMP
#undef __HAVE_ARCH_STRNCAT
#undef __HAVE_ARCH_STRNCMP
#undef __HAVE_ARCH_STRCHR
#undef __HAVE_ARCH_STRRCHR
#undef __HAVE_ARCH_STRNLEN
#undef __HAVE_ARCH_STRSPN
#undef __HAVE_ARCH_STRPBRK
#undef __HAVE_ARCH_STRTOK
#undef __HAVE_ARCH_BCOPY
#undef __HAVE_ARCH_MEMCMP
#undef __HAVE_ARCH_MEMSCAN
#undef __HAVE_ARCH_STRSTR

extern void *memset(void *, int, size_t);
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern char *strncpy(char *, const char *, size_t);
extern int strcmp(const char *,const char *);

static inline void * memchr(const void * cs,int c,size_t count)
{
    void *ptr;

    __asm__ __volatile__ (
#ifndef __s390x__
                          "   lr    0,%2\n"
                          "   lr    1,%1\n"
                          "   la    %0,0(%3,%1)\n"
                          "0: srst  %0,1\n"
                          "   jo    0b\n"
                          "   brc   13,1f\n"
                          "   slr   %0,%0\n"
#else /* __s390x__ */
                          "   lgr   0,%2\n"
                          "   lgr   1,%1\n"
                          "   la    %0,0(%3,%1)\n"
                          "0: srst  %0,1\n"
                          "   jo    0b\n"
                          "   brc   13,1f\n"
                          "   slgr  %0,%0\n"
#endif /* __s390x__ */
                          "1:"
                          : "=&a" (ptr) : "a" (cs), "d" (c), "d" (count)
                          : "cc", "0", "1" );
    return ptr;
}

static __inline__ char *strcpy(char *dest, const char *src)
{
    char *tmp = dest;

    __asm__ __volatile__ (
#ifndef __s390x__
                          "   sr    0,0\n"
                          "0: mvst  %0,%1\n"
                          "   jo    0b"
#else /* __s390x__ */
                          "   slgr  0,0\n"
                          "0: mvst  %0,%1\n"
                          "   jo    0b"
#endif /* __s390x__ */
                          : "+&a" (dest), "+&a" (src) :
                          : "cc", "memory", "0" );
    return tmp;
}

static __inline__ size_t strlen(const char *s)
{
    size_t len;

    __asm__ __volatile__ (
#ifndef __s390x__
                          "   sr    0,0\n"
                          "   lr    %0,%1\n"
                          "0: srst  0,%0\n"
                          "   jo    0b\n"
                          "   lr    %0,0\n"
                          "   sr    %0,%1"
#else /* __s390x__ */
                          "   slgr  0,0\n"
                          "   lgr   %0,%1\n"
                          "0: srst  0,%0\n"
                          "   jo    0b\n"
                          "   lgr   %0,0\n"
                          "   sgr   %0,%1"
#endif /* __s390x__ */
                          : "=&a" (len) : "a" (s) 
                          : "cc", "0" );
    return len;
}

static __inline__ char *strcat(char *dest, const char *src)
{
    char *tmp = dest;

    __asm__ __volatile__ (
#ifndef __s390x__
                          "   sr    0,0\n"
                          "0: srst  0,%0\n"
                          "   jo    0b\n"
                          "   lr    %0,0\n"
                          "   sr    0,0\n"
                          "1: mvst  %0,%1\n"
                          "   jo    1b"
#else /* __s390x__ */
                          "   slgr  0,0\n"
                          "0: srst  0,%0\n"
                          "   jo    0b\n"
                          "   lgr   %0,0\n"
                          "   slgr  0,0\n"
                          "1: mvst  %0,%1\n"
                          "   jo    1b"
#endif /* __s390x__ */
                          : "+&a" (dest), "+&a" (src) :
                          : "cc", "memory", "0" );
    return tmp;
}

extern void *alloca(size_t);
#endif /* __KERNEL__ */

#endif /* __S390_STRING_H_ */
