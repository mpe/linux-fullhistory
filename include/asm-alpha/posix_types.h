#ifndef _ALPHA_POSIX_TYPES_H
#define _ALPHA_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned int	__dev_t;
typedef unsigned int	__ino_t;
typedef unsigned int	__mode_t;
typedef unsigned short	__nlink_t;
typedef long		__off_t;
typedef int		__pid_t;
typedef unsigned int	__uid_t;
typedef unsigned int	__gid_t;
typedef unsigned long	__size_t;
typedef long		__ssize_t;
typedef long		__ptrdiff_t;
typedef long		__time_t;
typedef long		__clock_t;
typedef int		__daddr_t;
typedef char *		__caddr_t;

typedef struct {
	int	val[2];
} __fsid_t;

#ifndef __GNUC__

#define	__FD_SET(d, set)	((set)->fds_bits[__FDELT(d)] |= __FDMASK(d))
#define	__FD_CLR(d, set)	((set)->fds_bits[__FDELT(d)] &= ~__FDMASK(d))
#define	__FD_ISSET(d, set)	((set)->fds_bits[__FDELT(d)] & __FDMASK(d))
#define	__FD_ZERO(set)	\
  ((void) memset ((__ptr_t) (set), 0, sizeof (__fd_set)))

#else /* __GNUC__ */

/* With GNU C, use inline functions instead so args are evaluated only once: */

#undef __FD_SET
static __inline__ void __FD_SET(unsigned long fd, __fd_set *fdsetp)
{
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	fdsetp->fds_bits[_tmp] |= (1UL<<_rem);
}

#undef __FD_CLR
static __inline__ void __FD_CLR(unsigned long fd, __fd_set *fdsetp)
{
	unsigned long _tmp = fd / __NFDBITS;
	unsigned long _rem = fd % __NFDBITS;
	fdsetp->fds_bits[_tmp] &= ~(1UL<<_rem);
}

#undef __FD_ISSET
static __inline__ int __FD_ISSET(unsigned long fd, __fd_set *p)
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
static __inline__ void __FD_ZERO(__fd_set *p)
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

#endif /* __GNUC__ */

#endif /* _ALPHA_POSIX_TYPES_H */
