#ifndef _ASM_IA64_TYPES_H
#define _ASM_IA64_TYPES_H

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#ifdef __ASSEMBLY__
# define __IA64_UL(x)		x
# define __IA64_UL_CONST(x)	x
#else
# define __IA64_UL(x)		((unsigned long)x)
# define __IA64_UL_CONST(x)	x##UL
#endif

#ifndef __ASSEMBLY__

typedef unsigned int umode_t;

/*
 * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space
 */

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

/*
 * There are 32-bit compilers for the ia-64 out there..
 */
# if ((~0UL) == 0xffffffff)
#  if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#  endif
# else
typedef __signed__ long __s64;
typedef unsigned long __u64;
# endif

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
# ifdef __KERNEL__

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

/*
 * There are 32-bit compilers for the ia-64 out there...  (don't rely
 * on cpp because that may cause su problem in a 32->64 bit
 * cross-compilation environment).
 */
#  ifdef __LP64__

typedef signed long s64;
typedef unsigned long u64;
#define BITS_PER_LONG 64

#  else

typedef signed long long s64;
typedef unsigned long long u64;
#define BITS_PER_LONG 32

#  endif

/* DMA addresses are 64-bits wide, in general.  */

typedef u64 dma_addr_t;

# endif /* __KERNEL__ */
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_TYPES_H */
