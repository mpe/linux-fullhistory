/*
 * $Id: time.c,v 1.57 1999/10/21 03:08:16 cort Exp $
 * Common time routines among all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 * MPC8xx/MBX changes by Dan Malek (dmalek@jlc.net).
 *
 * Since the MPC8xx has a programmable interrupt timer, I decided to
 * use that rather than the decrementer.  Two reasons: 1.) the clock
 * frequency is low, causing 2.) a long wait in the timer interrupt
 *		while ((d = get_dec()) == dval)
 * loop.  The MPC8xx can be driven from a variety of input clocks,
 * so a number of assumptions have been made here because the kernel
 * parameter HZ is a constant.  We assume (correctly, today :-) that
 * the MPC8xx on the MBX board is driven from a 32.768 kHz crystal.
 * This is then divided by 4, providing a 8192 Hz clock into the PIT.
 * Since it is not possible to get a nice 100 Hz clock out of this, without
 * creating a software PLL, I have set HZ to 128.  -- Dan
 *
 * 1997-09-10  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 */

#include <linux/config.h>
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
#include <linux/init.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>
#include <asm/cache.h>
#include <asm/8xx_immap.h>
#include <asm/machdep.h>

#include "time.h"

void smp_local_timer_interrupt(struct pt_regs *);

/* keep track of when we need to update the rtc */
time_t last_rtc_update = 0;
extern rwlock_t xtime_lock;

/* The decrementer counts down by 128 every 128ns on a 601. */
#define DECREMENTER_COUNT_601	(1000000000 / HZ)
#define COUNT_PERIOD_NUM_601	1
#define COUNT_PERIOD_DEN_601	1000

unsigned decrementer_count;	/* count value for 1e6/HZ microseconds */
unsigned count_period_num;	/* 1 decrementer count equals */
unsigned count_period_den;	/* count_period_num / count_period_den us */
unsigned long last_tb;

/*
 * timer_interrupt - gets called when the decrementer overflows,
 * with interrupts disabled.
 * We set it up to overflow again in 1/HZ seconds.
 */
int timer_interrupt(struct pt_regs * regs)
{
	int dval, d;
	unsigned long flags;
	unsigned long cpu = smp_processor_id();
	
	hardirq_enter(cpu);
#ifdef CONFIG_SMP
	{
		unsigned int loops = 100000000;
		while (test_bit(0, &global_irq_lock)) {
			if (smp_processor_id() == global_irq_holder) {
				printk("uh oh, interrupt while we hold global irq lock!\n");
#ifdef CONFIG_XMON
				xmon(0);
#endif
				break;
			}
			if (loops-- == 0) {
				printk("do_IRQ waiting for irq lock (holder=%d)\n", global_irq_holder);
#ifdef CONFIG_XMON
				xmon(0);
#endif
			}
		}
	}
#endif /* CONFIG_SMP */			
	
	dval = get_dec();
	/*
	 * Wait for the decrementer to change, then jump
	 * in and add decrementer_count to its value
	 * (quickly, before it changes again!)
	 */
	while ((d = get_dec()) == dval)
		;
	asm volatile("mftb 	%0" : "=r" (last_tb) );
	/*
	 * Don't play catchup between the call to time_init()
	 * and sti() in init/main.c.
	 *
	 * This also means if we're delayed for > HZ
	 * we lose those ticks.  If we're delayed for > HZ
	 * then we have something wrong anyway, though.
	 *
	 * -- Cort
	 */
	if ( d < (-1*decrementer_count) )
		d = 0;
	set_dec(d + decrementer_count);
	if ( !smp_processor_id() )
	{
		do_timer(regs);
		/*
		 * update the rtc when needed
		 */
		read_lock_irqsave(&xtime_lock, flags);
		if ( (time_status & STA_UNSYNC) &&
		     ((xtime.tv_sec > last_rtc_update + 60) ||
		      (xtime.tv_sec < last_rtc_update)) )
		{
			if (ppc_md.set_rtc_time(xtime.tv_sec) == 0)
				last_rtc_update = xtime.tv_sec;
			else
				/* do it again in 60 s */
				last_rtc_update = xtime.tv_sec;
		}
		read_unlock_irqrestore(&xtime_lock, flags);
	}
#ifdef CONFIG_SMP
	smp_local_timer_interrupt(regs);
#endif		
	
	if ( ppc_md.heartbeat && !ppc_md.heartbeat_count--)
		ppc_md.heartbeat();
	
	hardirq_exit(cpu);
	return 1; /* lets ret_from_int know we can do checks */
}

/*
 * This version of gettimeofday has microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags, diff;

	save_flags(flags);
	cli();
	read_lock_irqsave(&xtime_lock, flags);
	*tv = xtime;
	read_unlock_irqrestore(&xtime_lock, flags);
	/* XXX we don't seem to have the decrementers synced properly yet */
#ifndef CONFIG_SMP
	asm volatile("mftb %0" : "=r" (diff) );
	diff -= last_tb;
	tv->tv_usec += diff * count_period_num / count_period_den;
	tv->tv_sec += tv->tv_usec / 1000000;
	tv->tv_usec = tv->tv_usec % 1000000;
#endif
	
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
	write_lock_irqsave(&xtime_lock, flags);
	xtime.tv_sec = tv->tv_sec;
	xtime.tv_usec = tv->tv_usec - frac_tick;
	write_unlock_irqrestore(&xtime_lock, flags);
	set_dec(frac_tick * count_period_den / count_period_num);
	time_adjust = 0;                /* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_state = TIME_ERROR;        /* p. 24, (a) */
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	restore_flags(flags);
}


void __init time_init(void)
{
	unsigned long flags;
        if (ppc_md.time_init != NULL)
        {
                ppc_md.time_init();
        }

	if ((_get_PVR() >> 16) == 1) {
		/* 601 processor: dec counts down by 128 every 128ns */
		decrementer_count = DECREMENTER_COUNT_601;
		count_period_num = COUNT_PERIOD_NUM_601;
		count_period_den = COUNT_PERIOD_DEN_601;
        } else if (!smp_processor_id()) {
                ppc_md.calibrate_decr();
	}

	write_lock_irqsave(&xtime_lock, flags);
	xtime.tv_sec = ppc_md.get_rtc_time();
	xtime.tv_usec = 0;
	write_unlock_irqrestore(&xtime_lock, flags);

	set_dec(decrementer_count);
	/* allow setting the time right away */
	last_rtc_update = 0;
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

/*
 * This only works for the Gregorian calendar - i.e. after 1752 (in the UK)
 */
void GregorianDay(struct rtc_time * tm)
{
	int leapsToDate;
	int lastYear;
	int day;
	int MonthOffset[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	lastYear=tm->tm_year-1;

	/*
	 * Number of leap corrections to apply up to end of last year
	 */
	leapsToDate = lastYear/4 - lastYear/100 + lastYear/400;

	/*
	 * This year is a leap year if it is divisible by 4 except when it is
	 * divisible by 100 unless it is divisible by 400
	 *
	 * e.g. 1904 was a leap year, 1900 was not, 1996 is, and 2000 will be
	 */
	if((tm->tm_year%4==0) &&
	   ((tm->tm_year%100!=0) || (tm->tm_year%400==0)) &&
	   (tm->tm_mon>2))
	{
		/*
		 * We are past Feb. 29 in a leap year
		 */
		day=1;
	}
	else
	{
		day=0;
	}

	day += lastYear*365 + leapsToDate + MonthOffset[tm->tm_mon-1] +
		   tm->tm_mday;

	tm->tm_wday=day%7;
}

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

	/*
	 * Determine the day of week
	 */
	GregorianDay(tm);
}
