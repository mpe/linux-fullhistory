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

struct timeval {
	time_t		tv_sec;		/* seconds */
	suseconds_t	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};

#ifdef __KERNEL__

#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/timex.h>
#include <asm/div64.h>
#ifndef div_long_long_rem

#define div_long_long_rem(dividend,divisor,remainder) ({ \
		       u64 result = dividend;		\
		       *remainder = do_div(result,divisor); \
		       result; })

#endif

/*
 * Have the 32 bit jiffies value wrap 5 minutes after boot
 * so jiffies wrap bugs show up earlier.
 */
#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))

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

/*
 * We want to do realistic conversions of time so we need to use the same
 * values the update wall clock code uses as the jiffie size.  This value
 * is: TICK_NSEC (both of which are defined in timex.h).  This 
 * is a constant and is in nanoseconds.  We will used scaled math and
 * with a scales defined here as SEC_JIFFIE_SC,  USEC_JIFFIE_SC and 
 * NSEC_JIFFIE_SC.  Note that these defines contain nothing but
 * constants and so are computed at compile time.  SHIFT_HZ (computed in
 * timex.h) adjusts the scaling for different HZ values.
 */
#define SEC_JIFFIE_SC (30 - SHIFT_HZ)
#define NSEC_JIFFIE_SC (SEC_JIFFIE_SC + 30)
#define USEC_JIFFIE_SC (SEC_JIFFIE_SC + 20)
#define SEC_CONVERSION ((unsigned long)(((u64)NSEC_PER_SEC << SEC_JIFFIE_SC) /\
				(u64)TICK_NSEC))
#define NSEC_CONVERSION ((unsigned long)(((u64)1 << NSEC_JIFFIE_SC) /\
				(u64)TICK_NSEC))
#define USEC_CONVERSION ((unsigned long)(((u64)NSEC_PER_USEC << USEC_JIFFIE_SC)/\
				(u64)TICK_NSEC))
#if BITS_PER_LONG < 64
# define MAX_SEC_IN_JIFFIES \
	(long)((u64)((u64)MAX_JIFFY_OFFSET * TICK_NSEC) / NSEC_PER_SEC)
#else	/* take care of overflow on 64 bits machines */
# define MAX_SEC_IN_JIFFIES \
	(SH_DIV((MAX_JIFFY_OFFSET >> SEC_JIFFIE_SC) * TICK_NSEC, NSEC_PER_SEC, 1) - 1)

#endif

static __inline__ unsigned long
timespec_to_jiffies(struct timespec *value)
{
	unsigned long sec = value->tv_sec;
	long nsec = value->tv_nsec + TICK_NSEC - 1;

	if (sec >= MAX_SEC_IN_JIFFIES){
		sec = MAX_SEC_IN_JIFFIES;
		nsec = 0;
	}
	return (((u64)sec * SEC_CONVERSION) +
		(((u64)nsec * NSEC_CONVERSION) >>
		 (NSEC_JIFFIE_SC - SEC_JIFFIE_SC))) >> SEC_JIFFIE_SC;

}

static __inline__ void
jiffies_to_timespec(unsigned long jiffies, struct timespec *value)
{
	/*
	 * Convert jiffies to nanoseconds and seperate with
	 * one divide.
	 */
	u64 nsec = (u64)jiffies * TICK_NSEC; 
	value->tv_sec = div_long_long_rem(nsec, NSEC_PER_SEC, &value->tv_nsec);
}

/* Same for "timeval" */
static __inline__ unsigned long
timeval_to_jiffies(struct timeval *value)
{
	unsigned long sec = value->tv_sec;
	long usec = value->tv_usec 
		+ ((TICK_NSEC + 1000UL/2) / 1000UL) - 1;

	if (sec >= MAX_SEC_IN_JIFFIES){
		sec = MAX_SEC_IN_JIFFIES;
		usec = 0;
	}
	return (((u64)sec * SEC_CONVERSION) +
		(((u64)usec * USEC_CONVERSION) >>
		 (USEC_JIFFIE_SC - SEC_JIFFIE_SC))) >> SEC_JIFFIE_SC;
}

static __inline__ void
jiffies_to_timeval(unsigned long jiffies, struct timeval *value)
{
	/*
	 * Convert jiffies to nanoseconds and seperate with
	 * one divide.
	 */
	u64 nsec = (u64)jiffies * TICK_NSEC; 
	value->tv_sec = div_long_long_rem(nsec, NSEC_PER_SEC, &value->tv_usec);
	value->tv_usec /= NSEC_PER_USEC;
}

static __inline__ int timespec_equal(struct timespec *a, struct timespec *b) 
{ 
	return (a->tv_sec == b->tv_sec) && (a->tv_nsec == b->tv_nsec);
} 

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static inline unsigned long
mktime (unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;		/* Puts Feb last since it has leap day */
		year -= 1;
	}

	return (((
		(unsigned long) (year/4 - year/100 + year/400 + 367*mon/12 + day) +
			year*365 - 719499
	    )*24 + hour /* now have hours */
	  )*60 + min /* now have minutes */
	)*60 + sec; /* finally seconds */
}

extern struct timespec xtime;
extern struct timespec wall_to_monotonic;
extern seqlock_t xtime_lock;

static inline unsigned long get_seconds(void)
{ 
	return xtime.tv_sec;
}

struct timespec current_kernel_time(void);

#define CURRENT_TIME (current_kernel_time())

#endif /* __KERNEL__ */

#define NFDBITS			__NFDBITS

#ifdef __KERNEL__
extern void do_gettimeofday(struct timeval *tv);
extern int do_settimeofday(struct timespec *tv);
extern int do_sys_settimeofday(struct timespec *tv, struct timezone *tz);
extern void clock_was_set(void); // call when ever the clock is set
extern int do_posix_clock_monotonic_gettime(struct timespec *tp);
extern long do_nanosleep(struct timespec *t);
extern long do_utimes(char __user * filename, struct timeval * times);
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
 * The IDs of the various system clocks (for POSIX.1b interval timers).
 */
#define CLOCK_REALTIME		  0
#define CLOCK_MONOTONIC	  1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID	 3
#define CLOCK_REALTIME_HR	 4
#define CLOCK_MONOTONIC_HR	  5

#define MAX_CLOCKS 6
#define CLOCKS_MASK  (CLOCK_REALTIME | CLOCK_MONOTONIC | \
                     CLOCK_REALTIME_HR | CLOCK_MONOTONIC_HR)
#define CLOCKS_MONO (CLOCK_MONOTONIC & CLOCK_MONOTONIC_HR)

/*
 * The various flags for setting POSIX.1b interval timers.
 */

#define TIMER_ABSTIME 0x01


#endif
