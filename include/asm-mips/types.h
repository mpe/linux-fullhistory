/*
 * include/asm-mips/types.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 by Waldorf GMBH
 * written by Ralf Baechle
 */
#ifndef __ASM_MIPS_TYPES_H
#define __ASM_MIPS_TYPES_H

#ifndef _SIZE_T
#define _SIZE_T
typedef __SIZE_TYPE__ size_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
typedef __SSIZE_TYPE__ ssize_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#endif

#ifndef _TIME_T
#define _TIME_T
typedef long time_t;
#endif

#ifndef _CLOCK_T
#define _CLOCK_T
typedef long clock_t;
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

typedef __signed int s32;
typedef unsigned int u32;

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

typedef __s32 pid_t;
typedef __s32 uid_t;
typedef __s32 gid_t;
typedef __u32 dev_t;
typedef __u32 ino_t;
typedef __u32 mode_t;
typedef __u32 umode_t;
typedef __u32 nlink_t;
typedef long daddr_t;
typedef long off_t;

#undef __FD_SET
static __inline__ void __FD_SET(unsigned long fd, fd_set *fdsetp)
{
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	fdsetp->fds_bits[_tmp] |= (1UL<<_rem);
}

#undef __FD_CLR
static __inline__ void __FD_CLR(unsigned long fd, fd_set *fdsetp)
{
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	fdsetp->fds_bits[_tmp] &= ~(1UL<<_rem);
}

#undef __FD_ISSET
static __inline__ int __FD_ISSET(unsigned long fd, fd_set *p)
{ 
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	return (p->fds_bits[_tmp] & (1UL<<_rem)) != 0;
}

/*
 * This will unroll the loop for the normal constant case (8 ints,
 * for a 256-bit fd_set)
 */
#undef __FD_ZERO
static __inline__ void __FD_ZERO(fd_set *p)
{
	unsigned int *tmp = p->fds_bits;
	int i;

	if (__builtin_constant_p(__FDSET_INTS)) {
		switch (__FDSET_INTS) {
			case 8:
				tmp[0] = 0; tmp[1] = 0; tmp[2] = 0; tmp[3] = 0;
				tmp[4] = 0; tmp[5] = 0; tmp[6] = 0; tmp[7] = 0;
				return;
		}
	}
	i = __FDSET_INTS;
	while (i) {
		i--;
		*tmp = 0;
		tmp++;
	}
}

#endif /* __ASM_MIPS_TYPES_H */
