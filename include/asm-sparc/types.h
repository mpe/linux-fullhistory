/* $Id: types.h,v 1.8 1995/11/25 02:33:08 davem Exp $ */
#ifndef _SPARC_TYPES_H
#define _SPARC_TYPES_H

/*
 * _xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space   <-- Linus sez this
 */

#ifndef _SIZE_T
#define _SIZE_T
#ifdef __svr4__
typedef unsigned int size_t;      /* solaris sucks */
#else
typedef long unsigned int size_t; /* sunos is much better */
#endif /* !(__svr4__) */
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
typedef int ssize_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef long int ptrdiff_t;
#endif

#ifndef _TIME_T
#define _TIME_T
typedef long time_t;
#endif

#ifndef _CLOCK_T
#define _CLOCK_T
typedef long clock_t;
#endif

typedef int pid_t;
typedef unsigned short uid_t;
typedef unsigned short gid_t;
typedef unsigned short dev_t;
typedef unsigned long ino_t;
typedef unsigned short mode_t;
typedef unsigned short umode_t;
typedef short nlink_t;
typedef long daddr_t;
typedef long off_t;

typedef signed char __s8;
typedef unsigned char __u8;

typedef signed short __s16;
typedef unsigned short __u16;

typedef signed int __s32;
typedef unsigned int __u32;

typedef signed long long __s64;
typedef unsigned long long __u64;

#ifdef __KERNEL__

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long long s64;
typedef unsigned long long u64;

#endif /* __KERNEL__ */

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
 * This will unroll the loop for the normal constant cases (4 or 8 longs,
 * for 256 and 512-bit fd_sets respectively)
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
			case 4:
				tmp[0] = 0; tmp[1] = 0; tmp[2] = 0; tmp[3] = 0;
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

#endif /* defined(_SPARC_TYPES_H) */
