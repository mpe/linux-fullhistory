/*
 *  linux/arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *
 *  Some code taken from i386 version.
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>

#include <linux/timex.h>
#include <linux/config.h>
#include <linux/irq.h>

#define TMU_TOCR	0xfffffe90	/* Byte access */
#define TMU_TSTR	0xfffffe92	/* Byte access */

#define TMU0_TCOR	0xfffffe94	/* Long access */
#define TMU0_TCNT	0xfffffe98	/* Long access */
#define TMU0_TCR	0xfffffe9c	/* Word access */

#define TMU_TOCR_INIT	0x00
#define TMU0_TCR_INIT	0x0020
#define TMU_TSTR_INIT	1

#define CLOCK_MHZ	(60/4)
#define INTERVAL	37500 /* (1000000*CLOCK_MHZ/HZ/2) ??? */

extern rwlock_t xtime_lock;
#define TICK_SIZE tick

void do_gettimeofday(struct timeval *tv)
{
	extern volatile unsigned long lost_ticks;
	unsigned long flags;
	unsigned long usec, sec;

	read_lock_irqsave(&xtime_lock, flags);
	usec = 0;
	{
		unsigned long lost = lost_ticks;
		if (lost)
			usec += lost * (1000000 / HZ);
	}
	sec = xtime.tv_sec;
	usec += xtime.tv_usec;
	read_unlock_irqrestore(&xtime_lock, flags);

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void do_settimeofday(struct timeval *tv)
{
	write_lock_irq(&xtime_lock);
	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_unlock_irq(&xtime_lock);
}

/*
 */
static int set_rtc_time(unsigned long nowtime)
{
/* XXX  should be implemented XXXXXXXXXX */
	int retval = -1;

	return retval;
}

/* last time the RTC clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void do_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);

#if 0
	if (!user_mode(regs))
		sh_do_profile(regs->pc);
#endif

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * RTC clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec >= 500000 - ((unsigned) tick) / 2 &&
	    xtime.tv_usec <= 500000 + ((unsigned) tick) / 2) {
		if (set_rtc_time(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
}

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
static void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long __dummy;

	/* Clear UNF bit */
	asm volatile("mov.w	%1,%0\n\t"
		     "and	%2,%0\n\t"
		     "mov.w	%0,%1"
		     : "=&z" (__dummy)
		     : "m" (__m(TMU0_TCR)), "r" (~0x100));

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_lock(&xtime_lock);

	do_timer_interrupt(irq, NULL, regs);

	write_unlock(&xtime_lock);
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
static inline unsigned long mktime(unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	return (((
	    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
	      year*365 - 719499
	    )*24 + hour /* now have hours */
	   )*60 + min /* now have minutes */
	  )*60 + sec; /* finally seconds */
}

static unsigned long get_rtc_time(void)
{
/* XXX not implemented yet */
	return 0;
}

static struct irqaction irq0  = { timer_interrupt, SA_INTERRUPT, 0, "timer", NULL, NULL};

void __init time_init(void)
{
	unsigned long __dummy;

	xtime.tv_sec = get_rtc_time();
	xtime.tv_usec = 0;

	set_ipr_data(TIMER_IRQ, TIMER_IRP_OFFSET, TIMER_PRIORITY);
	setup_irq(TIMER_IRQ, &irq0);

	/* Start TMU0 */
	asm volatile("mov	%1,%0\n\t"
		     "mov.b	%0,%2		! external clock input\n\t"
		     "mov	%3,%0\n\t"
		     "mov.w	%0,%4		! enable timer0 interrupt\n\t"
		     "mov.l	%5,%6\n\t"
		     "mov.l	%5,%7\n\t"
		     "mov	%8,%0\n\t"
		     "mov.b	%0,%9"
		     : "=&z" (__dummy)
		     : "i" (TMU_TOCR_INIT), "m" (__m(TMU_TOCR)),
		       "i" (TMU0_TCR_INIT), "m" (__m(TMU0_TCR)),
		       "r" (INTERVAL), "m" (__m(TMU0_TCOR)), "m" (__m(TMU0_TCNT)),
		       "i" (TMU_TSTR_INIT), "m" (__m(TMU_TSTR)));
#if 0
	/* Start RTC */
	asm volatile("");
#endif
}
