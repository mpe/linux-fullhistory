#ifndef __ASM_MIPS_TYPES_H
#define __ASM_MIPS_TYPES_H

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
typedef long ssize_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef long ptrdiff_t;
#endif

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

#if ((~0UL) == 0xffffffff)

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif
  
#else
  
typedef __signed__ long __s64;
typedef unsigned long __u64;

#endif

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

typedef __signed char s8;
typedef unsigned char u8;

typedef __signed short s16;
typedef unsigned short u16;

typedef __signed long s32;
typedef unsigned long u32;

#if ((~0UL) == 0xffffffff)

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long s64;
typedef unsigned long long u64;
#endif
  
#else
  
typedef __signed__ long s64;
typedef unsigned long u64;

#endif

#endif /* __KERNEL__ */

/*
 * These definitions double the definitions from <gnu/types.h>.
 */
#undef  __FDELT
#define __FDELT(d)      ((d) / __NFDBITS)
#undef  __FDMASK
#define __FDMASK(d)     (1 << ((d) % __NFDBITS))
#undef  __FD_SET
#define __FD_SET(d, set)        ((set)->fds_bits[__FDELT(d)] |= __FDMASK(d))
#undef  __FD_CLR
#define __FD_CLR(d, set)        ((set)->fds_bits[__FDELT(d)] &= ~__FDMASK(d))
#undef  __FD_ISSET
#define __FD_ISSET(d, set)      ((set)->fds_bits[__FDELT(d)] & __FDMASK(d))
#undef  __FD_ZERO
#define __FD_ZERO(fdsetp) (memset (fdsetp, 0, sizeof(*(fd_set *)fdsetp)))

#endif /* __ASM_MIPS_TYPES_H */
