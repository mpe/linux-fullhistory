/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * Adapted for PowerPC (PreP) by Gary Thomas
 *
 * This file contains the PC-specific time handling details:
 * reading the RTC at bootup, etc..
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/nvram.h>
#include <asm/mc146818rtc.h>
#include <asm/processor.h>

#include <linux/timex.h>
#include <linux/config.h>

extern int isBeBox[];

#define TIMER_IRQ 0

/* Cycle counter value at the previous timer interrupt.. */
static unsigned long long last_timer_cc = 0;
static unsigned long long init_timer_cc = 0;

static inline int CMOS_READ(int addr)
{
	outb(addr>>8, NVRAM_AS1);
	outb(addr, NVRAM_AS0);
	return (inb(NVRAM_DATA));
}

static inline int CMOS_WRITE(int addr, int val)
{
	outb(addr>>8, NVRAM_AS1);
	outb(addr, NVRAM_AS0);
	return (outb(val, NVRAM_DATA));
}

/* This function must be called with interrupts disabled 
 * It was inspired by Steve McCanne's microtime-i386 for BSD.  -- jrs
 * 
 * However, the pc-audio speaker driver changes the divisor so that
 * it gets interrupted rather more often - it loads 64 into the
 * counter rather than 11932! This has an adverse impact on
 * do_gettimeoffset() -- it stops working! What is also not
 * good is that the interval that our timer function gets called
 * is no longer 10.0002 ms, but 9.9767 ms. To get around this
 * would require using a different timing source. Maybe someone
 * could use the RTC - I know that this can interrupt at frequencies
 * ranging from 8192Hz to 2Hz. If I had the energy, I'd somehow fix
 * it so that at startup, the timer code in sched.c would select
 * using either the RTC or the 8253 timer. The decision would be
 * based on whether there was any other device around that needed
 * to trample on the 8253. I'd set up the RTC to interrupt at 1024 Hz,
 * and then do some jiggery to have a version of do_timer that 
 * advanced the clock by 1/1024 s. Every time that reached over 1/100
 * of a second, then do all the old code. If the time was kept correct
 * then do_gettimeoffset could just return 0 - there is no low order
 * divider that can be accessed.
 *
 * Ideally, you would be able to use the RTC for the speaker driver,
 * but it appears that the speaker driver really needs interrupt more
 * often than every 120 us or so.
 *
 * Anyway, this needs more thought....		pjsg (1993-08-28)
 * 
 * If you are really that interested, you should be reading
 * comp.protocols.time.ntp!
 */

#define TICK_SIZE tick

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
	/* This is revolting. We need to set the xtime.tv_usec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
	tv->tv_usec -= do_gettimeoffset();

	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	set_rtc(xtime.tv_sec);
	sti();
}

static int      month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

struct _tm
{
	int             tm_sec;
	int             tm_min;
	int             tm_hour;
	int             tm_day;
	int             tm_month;
	int             tm_year;
};

static _to_tm(int tim, struct _tm * tm)
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
	tm->tm_month = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_day = day + 1;
}

/*
 * Set the time into the CMOS
 */
static void set_rtc(unsigned long nowtime)
{
  int retval = 0;
  struct _tm tm;
  unsigned char save_control, save_freq_select;
  
  /*if (_Processor != _PROC_IBM) return;*/
  
  _to_tm(nowtime, &tm);
  
  /* tell the clock it's being set */  
  save_control = CMOS_MCRTC_READ(MCRTC_CONTROL); 
  CMOS_MCRTC_WRITE((save_control|MCRTC_SET), MCRTC_CONTROL);
  /* stop and reset prescaler */  
  save_freq_select = CMOS_MCRTC_READ(MCRTC_FREQ_SELECT);
  CMOS_MCRTC_WRITE((save_freq_select|MCRTC_DIV_RESET2), MCRTC_FREQ_SELECT);

  printk("Set RTC H:M:S M/D/Y %d:%02d:%02d %d/%d/%d\n", 
       tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_month, tm.tm_day, tm.tm_year);
	if (!(save_control & MCRTC_DM_BINARY) || MCRTC_ALWAYS_BCD) {
		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_month);
		BIN_TO_BCD(tm.tm_day);
		BIN_TO_BCD(tm.tm_year);
	}

	CMOS_MCRTC_WRITE(tm.tm_sec,  MCRTC_SECONDS);
	CMOS_MCRTC_WRITE(tm.tm_min,  MCRTC_MINUTES);
	CMOS_MCRTC_WRITE(tm.tm_hour, MCRTC_HOURS);
	CMOS_MCRTC_WRITE(tm.tm_month,  MCRTC_MONTH);
	CMOS_MCRTC_WRITE(tm.tm_day,  MCRTC_MINUTES);
	CMOS_MCRTC_WRITE(tm.tm_year - 1900, MCRTC_MINUTES);

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_MCRTC_WRITE(save_control, MCRTC_CONTROL);
	CMOS_MCRTC_WRITE(save_freq_select, MCRTC_FREQ_SELECT);
}

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 */
static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

#ifdef __powerpc__
printk("%s: %d - set TOD\n", __FILE__, __LINE__);
return (-1);  /* Not implemented */
#else	

printk("%s: %d - set TOD\n", __FILE__, __LINE__);
	save_control = CMOS_MCRTC_READ(MCRTC_CONTROL); /* tell the clock it's being set */
	CMOS_MCRTC_WRITE((save_control|MCRTC_SET), MCRTC_CONTROL);

	save_freq_select = CMOS_MCRTC_READ(MCRTC_FREQ_SELECT); /* stop and reset prescaler */
	CMOS_MCRTC_WRITE((save_freq_select|MCRTC_DIV_RESET2), MCRTC_FREQ_SELECT);

	cmos_minutes = CMOS_MCRTC_READ(MCRTC_MINUTES);
	if (!(save_control & MCRTC_DM_BINARY) || MCRTC_ALWAYS_BCD)
		BCD_TO_BIN(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & MCRTC_DM_BINARY) || MCRTC_ALWAYS_BCD) {
			BIN_TO_BCD(real_seconds);
			BIN_TO_BCD(real_minutes);
		}
		CMOS_MCRTC_WRITE(real_seconds,MCRTC_SECONDS);
		CMOS_MCRTC_WRITE(real_minutes,MCRTC_MINUTES);
	} else
		retval = -1;

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_MCRTC_WRITE(save_control, MCRTC_CONTROL);
	CMOS_MCRTC_WRITE(save_freq_select, MCRTC_FREQ_SELECT);

	return retval;
#endif	
}

/* last time the cmos clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void timer_interrupt(int irq, void *dev, struct pt_regs * regs)
{
  static int timeints = 0;
  
  do_timer(regs);

  /*
   * If we have an externally synchronized Linux clock, then update
   * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
   * called as close as possible to 500 ms before the new second starts.
   */
  if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
      xtime.tv_usec > 500000 - (tick >> 1) &&
      xtime.tv_usec < 500000 + (tick >> 1))
    if (set_rtc_mmss(xtime.tv_sec) == 0)
      last_rtc_update = xtime.tv_sec;
    else
      last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */


  /* use hard disk LED as a heartbeat instead -- much more useful
     -- Cort */
  switch(timeints)
  {
    /* act like an actual heart beat -- ie thump-thump-pause... */
    case 0:
    case 20:
      hard_disk_LED(1);
      break;
    case 7:
    case 27:
      hard_disk_LED(0);
      break;
    case 100:
      timeints = -1;
      break;
  }
  timeints++;
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

unsigned long get_cmos_time(void)
{
  unsigned int year, mon, day, hour, min, sec;
  int i;
  
  if (_Processor == _PROC_IBM)
  {
    do { /* Isn't this overkill ? UIP above should guarantee consistency */
      sec = CMOS_MCRTC_READ(MCRTC_SECONDS);
      min = CMOS_MCRTC_READ(MCRTC_MINUTES);
      hour = CMOS_MCRTC_READ(MCRTC_HOURS);
      day = CMOS_MCRTC_READ(MCRTC_DAY_OF_MONTH);
      mon = CMOS_MCRTC_READ(MCRTC_MONTH);
      year = CMOS_MCRTC_READ(MCRTC_YEAR);
    } while (sec != CMOS_MCRTC_READ(MCRTC_SECONDS));
    BCD_TO_BIN(sec);
    BCD_TO_BIN(min);
    BCD_TO_BIN(hour);
    BCD_TO_BIN(day);
    BCD_TO_BIN(mon);
    BCD_TO_BIN(year);
  } else
    if (_Processor == _PROC_Be)
      {
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
	  sec = CMOS_MCRTC_READ(MCRTC_SECONDS);
	  min = CMOS_MCRTC_READ(MCRTC_MINUTES);
	  hour = CMOS_MCRTC_READ(MCRTC_HOURS);
	  day = CMOS_MCRTC_READ(MCRTC_DAY_OF_MONTH);
	  mon = CMOS_MCRTC_READ(MCRTC_MONTH);
	  year = CMOS_MCRTC_READ(MCRTC_YEAR);
	} while (sec != CMOS_MCRTC_READ(MCRTC_SECONDS));
      } else
	{ /* Motorola PowerStack etc. */
	  do { /* Isn't this overkill ? UIP above should guarantee consistency */
	    sec = CMOS_READ(RTC_SECONDS);
	    min = CMOS_READ(RTC_MINUTES);
	    hour = CMOS_READ(RTC_HOURS);
	    day = CMOS_READ(RTC_DAY_OF_MONTH);
	    mon = CMOS_READ(RTC_MONTH);
	    year = CMOS_READ(RTC_YEAR);
	  } while (sec != CMOS_READ(RTC_SECONDS));
	  BCD_TO_BIN(sec);
	  BCD_TO_BIN(min);
	  BCD_TO_BIN(hour);
	  BCD_TO_BIN(day);
	  BCD_TO_BIN(mon);
	  BCD_TO_BIN(year);
	}
#if 0	
printk("CMOS TOD - M/D/Y H:M:S = %d/%d/%d %d:%02d:%02d\n", mon, day, year, hour, min, sec);
#endif
	if ((year += 1900) < 1970)
		year += 100;
	return mktime(year, mon, day, hour, min, sec);
}

void time_init(void)
{
  void (*irq_handler)(int, struct pt_regs *);
  xtime.tv_sec = get_cmos_time();
  xtime.tv_usec = 0;
  
  /* If we have the CPU hardware time counters, use them */
  irq_handler = timer_interrupt;
  if (request_irq(TIMER_IRQ, irq_handler, 0, "timer", NULL) != 0)
    panic("Could not allocate timer IRQ!");
}

