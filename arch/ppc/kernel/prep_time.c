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
#include <asm/nvram.h>

#include "time.h"

inline unsigned long mktime(unsigned int, unsigned int,unsigned int,
				   unsigned int, unsigned int, unsigned int);

/*
 * The motorola uses the m48t18 rtc (includes DS1643) whose registers
 * are at a higher end of nvram (1ff8-1fff) than the ibm mc146818
 * rtc (ds1386) which has regs at addr 0-d).  The intel gets
 * past this because the bios emulates the mc146818.
 *
 * Why in the world did they have to use different clocks?
 *
 * Right now things are hacked to check which machine we're on then
 * use the appropriate macro.  This is very very ugly and I should
 * probably have a function that checks which machine we're on then
 * does things correctly transparently or a function pointer which
 * is setup at boot time to use the correct addresses.
 * -- Cort
 */
/*
 * translate from mc146818 to m48t18  addresses
 */
unsigned int clock_transl[] = { MOTO_RTC_SECONDS,0 /* alarm */,
		       MOTO_RTC_MINUTES,0 /* alarm */,
		       MOTO_RTC_HOURS,0 /* alarm */,                 /*  4,5 */
		       MOTO_RTC_DAY_OF_WEEK,
		       MOTO_RTC_DAY_OF_MONTH,
		       MOTO_RTC_MONTH,
		       MOTO_RTC_YEAR,                    /* 9 */
		       MOTO_RTC_CONTROLA, MOTO_RTC_CONTROLB /* 10,11 */
};

int prep_cmos_clock_read(int addr)
{
	if ( _machine == _MACH_IBM )
		return CMOS_READ(addr);
	else if ( _machine == _MACH_Motorola )
	{
		outb(clock_transl[addr]>>8, NVRAM_AS1);
		outb(clock_transl[addr], NVRAM_AS0);
		return (inb(NVRAM_DATA));
	}

	printk("Unknown machine in prep_cmos_clock_read()!\n");
	return -1;
}

void prep_cmos_clock_write(unsigned long val, int addr)
{
	if ( _machine == _MACH_IBM )
	{
		CMOS_WRITE(val,addr);
		return;
	}
	else if ( _machine == _MACH_Motorola )
	{
		outb(clock_transl[addr]>>8, NVRAM_AS1);
		outb(clock_transl[addr], NVRAM_AS0);
		outb(val,NVRAM_DATA);
		return;
	}
	printk("Unknown machine in prep_cmos_clock_write()!\n");
}

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

#if 0
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

#endif

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
int prep_set_rtc_time(unsigned long nowtime)
{
	unsigned char save_control, save_freq_select;
	struct rtc_time tm;

	to_tm(nowtime, &tm);

	save_control = prep_cmos_clock_read(RTC_CONTROL); /* tell the clock it's being set */

	prep_cmos_clock_write((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = prep_cmos_clock_read(RTC_FREQ_SELECT); /* stop and reset prescaler */
	
	prep_cmos_clock_write((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

        tm.tm_year -= 1900;
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_mon);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}
	prep_cmos_clock_write(tm.tm_sec,RTC_SECONDS);
	prep_cmos_clock_write(tm.tm_min,RTC_MINUTES);
	prep_cmos_clock_write(tm.tm_hour,RTC_HOURS);
	prep_cmos_clock_write(tm.tm_mon,RTC_MONTH);
	prep_cmos_clock_write(tm.tm_mday,RTC_DAY_OF_MONTH);
	prep_cmos_clock_write(tm.tm_year,RTC_YEAR);
	
	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	prep_cmos_clock_write(save_control, RTC_CONTROL);
	prep_cmos_clock_write(save_freq_select, RTC_FREQ_SELECT);

	if ( (time_state == TIME_ERROR) || (time_state == TIME_BAD) )
		time_state = TIME_OK;
	return 0;
}

unsigned long prep_get_rtc_time(void)
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
		if (prep_cmos_clock_read(RTC_FREQ_SELECT) & RTC_UIP)
			break;
	for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms */
		if (!(prep_cmos_clock_read(RTC_FREQ_SELECT) & RTC_UIP))
			break;
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
		sec = prep_cmos_clock_read(RTC_SECONDS);
		min = prep_cmos_clock_read(RTC_MINUTES);
		hour = prep_cmos_clock_read(RTC_HOURS);
		day = prep_cmos_clock_read(RTC_DAY_OF_MONTH);
		mon = prep_cmos_clock_read(RTC_MONTH);
		year = prep_cmos_clock_read(RTC_YEAR);
	} while (sec != prep_cmos_clock_read(RTC_SECONDS));
	if (!(prep_cmos_clock_read(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
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
inline unsigned long mktime(unsigned int year, unsigned int mon,
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

#if 0
void time_init(void)
{
	void (*irq_handler)(int, void *,struct pt_regs *);
	
	xtime.tv_sec = prep_get_rtc_time();
	xtime.tv_usec = 0;
	
	prep_calibrate_decr();
	
	/* If we have the CPU hardware time counters, use them */
	irq_handler = timer_interrupt;
	if (request_irq(TIMER_IRQ, irq_handler, 0, "timer", NULL) != 0)
		panic("Could not allocate timer IRQ!");
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void timer_interrupt(int irq, void *dev, struct pt_regs * regs)
{
	prep_calibrate_decr_handler(irq,dev,regs);
	do_timer(regs);

	/* update the hw clock if:
	 * the time is marked out of sync (TIME_ERROR)
	 * or ~11 minutes have expired since the last update -- Cort
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. prep_set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ( time_state == TIME_BAD ||
	     xtime.tv_sec > last_rtc_update + 660 )
	/*if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))*/
		if (prep_set_rtc_time(xtime.tv_sec) == 0)
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
#endif
