/*
 * $Id: time.c,v 1.17 1997/12/28 22:47:21 paulus Exp $
 * Common time routines among all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/time.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>

#include "time.h"

/* this is set to the appropriate pmac/prep/chrp func in init_IRQ() */
int (*set_rtc_time)(unsigned long);

/* keep track of when we need to update the rtc */
unsigned long last_rtc_update = 0;

/* The decrementer counts down by 128 every 128ns on a 601. */
#define DECREMENTER_COUNT_601	(1000000000 / HZ)
#define COUNT_PERIOD_NUM_601	1
#define COUNT_PERIOD_DEN_601	1000

unsigned decrementer_count;	/* count value for 1e6/HZ microseconds */
unsigned count_period_num;	/* 1 decrementer count equals */
unsigned count_period_den;	/* count_period_num / count_period_den us */

/*
 * timer_interrupt - gets called when the decrementer overflows,
 * with interrupts disabled.
 * We set it up to overflow again in 1/HZ seconds.
 */
void timer_interrupt(struct pt_regs * regs)
{
	int dval, d;
	while ((dval = get_dec()) < 0) {
		/*
		 * Wait for the decrementer to change, then jump
		 * in and add decrementer_count to its value
		 * (quickly, before it changes again!)
		 */
		while ((d = get_dec()) == dval)
			;
		set_dec(d + decrementer_count);
		do_timer(regs);
		/*
		 * update the rtc when needed
		 */
		if ( xtime.tv_sec > last_rtc_update + 660 )
			if (set_rtc_time(xtime.tv_sec) == 0)
				last_rtc_update = xtime.tv_sec;
			else
				last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
}

/*
 * This version of gettimeofday has microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	*tv = xtime;
	tv->tv_usec += (decrementer_count - get_dec())
	    * count_period_num / count_period_den;
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
	restore_flags(flags);
}

void do_settimeofday(struct timeval *tv)
{
	unsigned long flags;
	int frac_tick;
	
	last_rtc_update = 0; /* so the rtc gets updated soon */
	
	frac_tick = tv->tv_usec % (1000000 / HZ);
	save_flags(flags);
	cli();
	xtime.tv_sec = tv->tv_sec;
	xtime.tv_usec = tv->tv_usec - frac_tick;
	set_dec(frac_tick * count_period_den / count_period_num);
	restore_flags(flags);
}


void
time_init(void)
{
	if ((_get_PVR() >> 16) == 1) {
		/* 601 processor: dec counts down by 128 every 128ns */
		decrementer_count = DECREMENTER_COUNT_601;
		count_period_num = COUNT_PERIOD_NUM_601;
		count_period_den = COUNT_PERIOD_DEN_601;
	}

	switch (_machine) {
	case _MACH_Pmac:
		/* can't call pmac_get_rtc_time() yet,
		   because via-cuda isn't initialized yet. */
		if ((_get_PVR() >> 16) != 1)
			pmac_calibrate_decr();
		set_rtc_time = pmac_set_rtc_time;
		break;
	case _MACH_chrp:
		chrp_time_init();
		xtime.tv_sec = chrp_get_rtc_time();
		if ((_get_PVR() >> 16) != 1)
			chrp_calibrate_decr();
		set_rtc_time = chrp_set_rtc_time;
		break;
	case _MACH_prep:
		xtime.tv_sec = prep_get_rtc_time();
		prep_calibrate_decr();
		set_rtc_time = prep_set_rtc_time;
		break;
	}
	xtime.tv_usec = 0;

	/*
	 * mark the rtc/on-chip timer as in sync
	 * so we don't update right away
	 */
	last_rtc_update = xtime.tv_sec;

	set_dec(decrementer_count);
}

/*
 * Uses the on-board timer to calibrate the on-chip decrementer register
 * for prep systems.  On the pmac the OF tells us what the frequency is
 * but on prep we have to figure it out.
 * -- Cort
 */
int calibrate_done = 0;
volatile int *done_ptr = &calibrate_done;
void prep_calibrate_decr(void)
{
	unsigned long flags;
	
	save_flags(flags);

#define TIMER0_COUNT 0x40
#define TIMER_CONTROL 0x43
	/* set timer to periodic mode */
	outb_p(0x34,TIMER_CONTROL);/* binary, mode 2, LSB/MSB, ch 0 */
	/* set the clock to ~100 Hz */
	outb_p(LATCH & 0xff , TIMER0_COUNT);	/* LSB */
	outb(LATCH >> 8 , TIMER0_COUNT);	/* MSB */
	
	if (request_irq(0, prep_calibrate_decr_handler, 0, "timer", NULL) != 0)
		panic("Could not allocate timer IRQ!");
	__sti();
	while ( ! *done_ptr ) /* nothing */; /* wait for calibrate */
        restore_flags(flags);
	free_irq( 0, NULL);
}

void prep_calibrate_decr_handler(int irq, void *dev, struct pt_regs * regs)
{
	unsigned long freq, divisor;
	static unsigned long t1 = 0, t2 = 0;

	if ( !t1 )
		t1 = get_dec();
	else if (!t2)
	{
		t2 = get_dec();
		t2 = t1-t2;  /* decr's in 1/HZ */
		t2 = t2*HZ;  /* # decrs in 1s - thus in Hz */
if ( (t2>>20) > 100 )
{
  printk("Decrementer frequency too high: %luMHz.  Using 15MHz.\n",t2>>20);
  t2 = 998700000/60;
}
		freq = t2 * 60;	/* try to make freq/1e6 an integer */
		divisor = 60;
		printk("time_init: decrementer frequency = %lu/%lu (%luMHz)\n",
		       freq, divisor,t2>>20);
		decrementer_count = freq / HZ / divisor;
		count_period_num = divisor;
		count_period_den = freq / 1000000;
		*done_ptr = 1;
	}
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
unsigned long mktime(unsigned int year, unsigned int mon,
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

#define TICK_SIZE tick
#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

void to_tm(int tim, struct rtc_time * tm)
{
	register int    i;
	register long   hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;
}
