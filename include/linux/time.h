#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

#include <asm/param.h>
#include <linux/types.h>

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
	time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* nanoseconds */
};
#endif /* _STRUCT_TIMESPEC */

/*
 * Change timeval to jiffies, trying to avoid the
 * most obvious overflows..
 *
 * And some not so obvious.
 *
 * Note that we don't want to return MAX_LONG, because
 * for various timeout reasons we often end up having
 * to wait "jiffies+1" in order to guarantee that we wait
 * at _least_ "jiffies" - so "jiffies+1" had better still
 * be positive.
 */
#define MAX_JIFFY_OFFSET ((~0UL >> 1)-1)

/* Parameters used to convert the timespec values */
#ifndef USEC_PER_SEC
#define USEC_PER_SEC (1000000L)
#endif

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC (1000000000L)
#endif

#ifndef NSEC_PER_USEC
#define NSEC_PER_USEC (1000L)
#endif

static __inline__ unsigned long
timespec_to_jiffies(struct timespec *value)
{
	unsigned long sec = value->tv_sec;
	long nsec = value->tv_nsec;

	if (sec >= (MAX_JIFFY_OFFSET / HZ))
		return MAX_JIFFY_OFFSET;
	nsec += NSEC_PER_SEC / HZ - 1;
	nsec /= NSEC_PER_SEC / HZ;
	return HZ * sec + nsec;
}

static __inline__ void
jiffies_to_timespec(unsigned long jiffies, struct timespec *value)
{
	value->tv_nsec = (jiffies % HZ) * (NSEC_PER_SEC / HZ);
	value->tv_sec = jiffies / HZ;
}
 
struct timeval {
	time_t		tv_sec;		/* seconds */
	suseconds_t	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};

#define NFDBITS			__NFDBITS

#ifdef __KERNEL__
extern void do_gettimeofday(struct timeval *tv);
extern void do_settimeofday(struct timeval *tv);
extern void get_fast_time(struct timeval *tv);
extern void (*do_get_fast_time)(struct timeval *);
#endif

#define FD_SETSIZE		__FD_SETSIZE
#define FD_SET(fd,fdsetp)	__FD_SET(fd,fdsetp)
#define FD_CLR(fd,fdsetp)	__FD_CLR(fd,fdsetp)
#define FD_ISSET(fd,fdsetp)	__FD_ISSET(fd,fdsetp)
#define FD_ZERO(fdsetp)		__FD_ZERO(fdsetp)

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2

struct  itimerspec {
        struct  timespec it_interval;    /* timer period */
        struct  timespec it_value;       /* timer expiration */
};

struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};


/* 
 * Data types for POSIX.1b interval timers.
 */
typedef int clockid_t;
typedef int timer_t;

/*
 * The IDs of the various system clocks (for POSIX.1b interval timers).
 */
#define CLOCK_REALTIME 0

/*
 * The various flags for setting POSIX.1b interval timers.
 */

#define TIMER_ABSTIME 0x01


#endif
