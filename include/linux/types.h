#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <linux/posix_types.h>
#include <asm/types.h>

#ifndef _LINUX_TYPES_DONT_EXPORT

typedef __fd_set	fd_set;
typedef __dev_t		dev_t;
typedef __ino_t		ino_t;
typedef __mode_t	mode_t;
typedef __nlink_t	nlink_t;
typedef __off_t		off_t;
typedef __pid_t		pid_t;
typedef __uid_t		uid_t;
typedef __gid_t		gid_t;
typedef __daddr_t	daddr_t;

/* bsd */

typedef __u_char	u_char;
typedef __u_short	u_short;
typedef __u_int		u_int;
typedef __u_long	u_long;

/*
 * The following typedefs are also protected by individual ifdefs for
 * historical reasons:
 */
#ifndef _SIZE_T
#define _SIZE_T
typedef __size_t	size_t;
#endif

#ifndef _SSIZE_T
#define _SSIZE_T
typedef __ssize_t	ssize_t;
#endif

#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef __ptrdiff_t	ptrdiff_t;
#endif

#ifndef _TIME_T
#define _TIME_T
typedef __time_t	time_t;
#endif

#ifndef _CLOCK_T
#define _CLOCK_T
typedef __clock_t	clock_t;
#endif

#ifndef _CADDR_T
#define _CADDR_T
typedef __caddr_t	caddr_t;
#endif

/* sysv */
typedef unsigned char	unchar;
typedef unsigned short	ushort;
typedef unsigned int	uint;
typedef unsigned long	ulong;

#endif /* _LINUX_TYPES_DONT_EXPORT */

/*
 * Below are truly Linux-specific types that should never collide with
 * any application/library that wants linux/types.h.
 */

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)

typedef long long	__loff_t;

#ifndef _LINUX_TYPES_DONT_EXPORT
#define _LOFF_T
typedef __loff_t	loff_t;
#endif

#endif

struct ustat {
	__daddr_t	f_tfree;
	__ino_t		f_tinode;
	char		f_fname[6];
	char		f_fpack[6];
};

#endif /* _LINUX_TYPES_H */
