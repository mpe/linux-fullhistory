#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

struct timeval {
	long	tv_sec;		/* seconds */
	long	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};

#define NFDBITS			__NFDBITS

#ifdef __KERNEL__
void do_gettimeofday(struct timeval *tv);
#include <asm/bitops.h>
#include <linux/string.h>
#define FD_SETSIZE		__FD_SETSIZE
#define FD_SET(fd,fdsetp)	set_bit(fd,fdsetp)
#define FD_CLR(fd,fdsetp)	clear_bit(fd,fdsetp)
#define FD_ISSET(fd,fdsetp)	(0 != test_bit(fd,fdsetp))
#define FD_ZERO(fdsetp)		memset(fdsetp, 0, sizeof(struct fd_set))
#else
#define FD_SETSIZE		__FD_SETSIZE
#define FD_SET(fd,fdsetp)	__FD_SET(fd,fdsetp)
#define FD_CLR(fd,fdsetp)	__FD_CLR(fd,fdsetp)
#define FD_ISSET(fd,fdsetp)	__FD_ISSET(fd,fdsetp)
#define FD_ZERO(fdsetp)		__FD_ZERO(fdsetp)
#endif

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2

struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};

#endif
