/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * Adapted for PowerPC (PreP) by Gary Thomas
 * Modified by Cort Dougan (cort@cs.nmt.edu)
 *  copied and modified from intel version
 *
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

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>

/* last time the cmos clock got updated */
static long last_rtc_update = 0;
static int set_rtc_mmss(unsigned long nowtime);
unsigned long get_cmos_time(void);
static inline unsigned long mktime(unsigned int, unsigned int,unsigned int,
				   unsigned int, unsigned int, unsigned int);
#define TIMER_IRQ 0

/* Cycle counter value at the previous timer interrupt.. */
static unsigned long long last_timer_cc = 0;
static unsigned long long init_timer_cc = 0;

#define TICK_SIZE tick
#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int      month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static unsigned long do_slow_gettimeoffset(void)
{
	int count;
	unsigned long offset = 0;

	/* timer count may underflow right here */
	outb_p(0x00, 0x43);	/* latch the count ASAP */
	count = inb_p(0x40);	/* read the latched count */
	count |= inb(0x40) << 8;
	/* we know probability of underflow is always MUCH less than 1% */
	if (count > (LATCH - LATCH/100)) {
		/* check for pending timer interrupt */
		outb_p(0x0a, 0x20);
		if (inb(0x20) & 1)
			offset = TICK_SIZE;
	}
	count = ((LATCH-1) - count) * TICK_SIZE;
	count = (count + LATCH/2) / LATCH;
	return offset + count;
}

static unsigned long (*do_gettimeoffset)(void) = do_slow_gettimeoffset;

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	*tv = xtime;
	tv->tv_usec += do_gettimeoffset();
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
	restore_flags(flags);
}

void do_settimeofday(struct timeval *tv)
{
	cli();
	tv->tv_usec -= do_gettimeoffset();
  
	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}
  
	xtime = *tv;
	time_state = TIME_ERROR;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	sti();
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
}

/*
 * Set the hardware clock. -- Cort
 */
static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;
	struct rtc_time tm;

	to_tm(nowtime, &tm);

	save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

        tm.tm_year -= 1900;
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_mon);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}
	CMOS_WRITE(tm.tm_sec,RTC_SECONDS);
	CMOS_WRITE(tm.tm_min,RTC_MINUTES);
	CMOS_WRITE(tm.tm_hour,RTC_HOURS);
	CMOS_WRITE(tm.tm_mon,RTC_MONTH);
	CMOS_WRITE(tm.tm_mday,RTC_DAY_OF_MONTH);
	CMOS_WRITE(tm.tm_year,RTC_YEAR);
	
	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

	if ( (time_state == TIME_ERROR) || (time_state == TIME_BAD) )
		time_state = TIME_OK;
	return 0;
}

unsigned long get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	int i;

	/* The Linux interpretation of the CMOS clock register contents:
	 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
	 * RTC registers show the second which has precisely just started.
	 * Let's hope other operating systems interpret the RTC the same way.
	 */
	/* read RTC exactly on falling edge of update flag */
	for (i = 0 ; i < 1000000 ; i++)	/* may take up to 1 second... */
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
			break;
	for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms */
		if (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
			break;
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
	} while (sec != CMOS_READ(RTC_SECONDS));
	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	  {
	    BCD_TO_BIN(sec);
	    BCD_TO_BIN(min);
	    BCD_TO_BIN(hour);
	    BCD_TO_BIN(day);
	    BCD_TO_BIN(mon);
	    BCD_TO_BIN(year);
	  }
	if ((year += 1900) < 1970)
		year += 100;
	return mktime(year, mon, day, hour, min, sec);
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void timer_interrupt(int irq, void *dev, struct pt_regs * regs)
{
	do_timer(regs);

	/* update the hw clock if:
	 * the time is marked out of sync (TIME_ERROR)
	 * or ~11 minutes have expired since the last update -- Cort
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ( time_state == TIME_BAD ||
	     xtime.tv_sec > last_rtc_update + 660 )
	/*if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))*/
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */


#ifdef CONFIG_HEARTBEAT
	/* use hard disk LED as a heartbeat instead -- much more useful
	   for debugging -- Cort */
	switch(kstat.interrupts[0] % 101)
	{
	/* act like an actual heart beat -- ie thump-thump-pause... */
	case 0:
	case 20:
		outb(1,IBM_HDD_LED);
		break;
	case 7:
	case 27:
		outb(0,IBM_HDD_LED);
		break;
	case 100:
		break;
	}
#endif
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

void time_init(void)
{
	void (*irq_handler)(int, void *,struct pt_regs *);
	xtime.tv_sec = get_cmos_time();
	xtime.tv_usec = 0;

	/* If we have the CPU hardware time counters, use them */
	irq_handler = timer_interrupt;
	if (request_irq(TIMER_IRQ, irq_handler, 0, "timer", NULL) != 0)
		panic("Could not allocate timer IRQ!");
}
