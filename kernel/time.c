/*
 *  linux/kernel/time.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  This file contains the interface functions for the various
 *  time related system calls: time, stime, gettimeofday, settimeofday,
 *			       adjtime
 */
/*
 * Modification history kernel/time.c
 * 
 * 1993-09-02    Philip Gladstone
 *      Created file with time related functions from sched.c and adjtimex() 
 * 1993-10-08    Torsten Duwe
 *      adjtime interface update and CMOS clock write code
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 *
 * to do: adjtimex() has to be updated to recent (1994-12-13) revision
 *        of David Mill's kernel clock model. For more information, check
 *        <ftp://louie.udel.edu/pub/ntp/kernel.tar.Z>. 
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/io.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

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
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;
}

/* 
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz = { 0, 0};

asmlinkage int sys_time(long * tloc)
{
	int i, error;

	i = CURRENT_TIME;
	if (tloc) {
		error = verify_area(VERIFY_WRITE, tloc, 4);
		if (error)
			return error;
		put_fs_long(i,(unsigned long *)tloc);
	}
	return i;
}

asmlinkage int sys_stime(unsigned long * tptr)
{
	int error;
	unsigned long value;

	if (!suser())
		return -EPERM;
	error = verify_area(VERIFY_READ, tptr, sizeof(*tptr));
	if (error)
		return error;
	value = get_fs_long(tptr);
	cli();
	xtime.tv_sec = value;
	xtime.tv_usec = 0;
	time_status = TIME_BAD;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	sti();
	return 0;
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

static inline unsigned long do_gettimeoffset(void)
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

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	*tv = xtime;
#if defined (__i386__) || defined (__mips__)
	tv->tv_usec += do_gettimeoffset();
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
#endif /* !defined (__i386__) && !defined (__mips__) */
	restore_flags(flags);
}

asmlinkage int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int error;

	if (tv) {
		struct timeval ktv;
		error = verify_area(VERIFY_WRITE, tv, sizeof *tv);
		if (error)
			return error;
		do_gettimeofday(&ktv);
		put_fs_long(ktv.tv_sec, (unsigned long *) &tv->tv_sec);
		put_fs_long(ktv.tv_usec, (unsigned long *) &tv->tv_usec);
	}
	if (tz) {
		error = verify_area(VERIFY_WRITE, tz, sizeof *tz);
		if (error)
			return error;
		put_fs_long(sys_tz.tz_minuteswest, (unsigned long *) tz);
		put_fs_long(sys_tz.tz_dsttime, ((unsigned long *) tz)+1);
	}
	return 0;
}

/*
 * Adjust the time obtained from the CMOS to be UTC time instead of
 * local time.
 * 
 * This is ugly, but preferable to the alternatives.  Otherwise we
 * would either need to write a program to do it in /etc/rc (and risk
 * confusion if the program gets run more than once; it would also be 
 * hard to make the program warp the clock precisely n hours)  or
 * compile in the timezone information into the kernel.  Bad, bad....
 *
 *              				- TYT, 1992-01-01
 *
 * The best thing to do is to keep the CMOS clock in universal time (UTC)
 * as real UNIX machines always do it. This avoids all headaches about
 * daylight saving times and warping kernel clocks.
 */
inline static void warp_clock(void)
{
	cli();
	xtime.tv_sec += sys_tz.tz_minuteswest * 60;
	sti();
}

/*
 * In case for some reason the CMOS clock has not already been running
 * in UTC, but in some local time: The first time we set the timezone,
 * we will warp the clock so that it is ticking UTC time instead of
 * local time. Presumably, if someone is setting the timezone then we
 * are running in an environment where the programs understand about
 * timezones. This should be done at boot time in the /etc/rc script,
 * as soon as possible, so that the clock can be set right. Otherwise,
 * various programs will get confused when the clock gets warped.
 */
asmlinkage int sys_settimeofday(struct timeval *tv, struct timezone *tz)
{
	static int	firsttime = 1;
	struct timeval	new_tv;
	struct timezone new_tz;

	if (!suser())
		return -EPERM;
	if (tv) {
		int error = verify_area(VERIFY_READ, tv, sizeof(*tv));
		if (error)
			return error;
		memcpy_fromfs(&new_tv, tv, sizeof(*tv));
	}
	if (tz) {
		int error = verify_area(VERIFY_READ, tz, sizeof(*tz));
		if (error)
			return error;
		memcpy_fromfs(&new_tz, tz, sizeof(*tz));
	}
	if (tz) {
		sys_tz = new_tz;
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				warp_clock();
		}
	}
	if (tv) {
		cli();
		/* This is revolting. We need to set the xtime.tv_usec
		 * correctly. However, the value in this location is
		 * is value at the last tick.
		 * Discover what correction gettimeofday
		 * would have done, and then undo it!
		 */
		new_tv.tv_usec -= do_gettimeoffset();

		if (new_tv.tv_usec < 0) {
			new_tv.tv_usec += 1000000;
			new_tv.tv_sec--;
		}

		xtime = new_tv;
		time_status = TIME_BAD;
		time_maxerror = 0x70000000;
		time_esterror = 0x70000000;
		sti();
	}
	return 0;
}

/* adjtimex mainly allows reading (and writing, if superuser) of
 * kernel time-keeping variables. used by xntpd.
 */
asmlinkage int sys_adjtimex(struct timex *txc_p)
{
        long ltemp, mtemp, save_adjust;
	int error;

	/* Local copy of parameter */
	struct timex txc;

	error = verify_area(VERIFY_WRITE, txc_p, sizeof(struct timex));
	if (error)
	  return error;

	/* Copy the user data space into the kernel copy
	 * structure. But bear in mind that the structures
	 * may change
	 */
	memcpy_fromfs(&txc, txc_p, sizeof(struct timex));

	/* In order to modify anything, you gotta be super-user! */
	if (txc.mode && !suser())
		return -EPERM;

	/* Now we validate the data before disabling interrupts
	 */

	if (txc.mode != ADJ_OFFSET_SINGLESHOT && (txc.mode & ADJ_OFFSET))
	  /* Microsec field limited to -131000 .. 131000 usecs */
	  if (txc.offset <= -(1 << (31 - SHIFT_UPDATE))
	      || txc.offset >= (1 << (31 - SHIFT_UPDATE)))
	    return -EINVAL;

	/* time_status must be in a fairly small range */
	if (txc.mode & ADJ_STATUS)
	  if (txc.status < TIME_OK || txc.status > TIME_BAD)
	    return -EINVAL;

	/* if the quartz is off by more than 10% something is VERY wrong ! */
	if (txc.mode & ADJ_TICK)
	  if (txc.tick < 900000/HZ || txc.tick > 1100000/HZ)
	    return -EINVAL;

	cli();

	/* Save for later - semantics of adjtime is to return old value */
	save_adjust = time_adjust;

	/* If there are input parameters, then process them */
	if (txc.mode)
	{
	    if (time_status == TIME_BAD)
		time_status = TIME_OK;

	    if (txc.mode & ADJ_STATUS)
		time_status = txc.status;

	    if (txc.mode & ADJ_FREQUENCY)
		time_freq = txc.frequency << (SHIFT_KF - 16);

	    if (txc.mode & ADJ_MAXERROR)
		time_maxerror = txc.maxerror;

	    if (txc.mode & ADJ_ESTERROR)
		time_esterror = txc.esterror;

	    if (txc.mode & ADJ_TIMECONST)
		time_constant = txc.time_constant;

	    if (txc.mode & ADJ_OFFSET)
	      if (txc.mode == ADJ_OFFSET_SINGLESHOT)
		{
		  time_adjust = txc.offset;
		}
	      else /* XXX should give an error if other bits set */
		{
		  time_offset = txc.offset << SHIFT_UPDATE;
		  mtemp = xtime.tv_sec - time_reftime;
		  time_reftime = xtime.tv_sec;
		  if (mtemp > (MAXSEC+2) || mtemp < 0)
		    mtemp = 0;

		  if (txc.offset < 0)
		    time_freq -= (-txc.offset * mtemp) >>
		      (time_constant + time_constant);
		  else
		    time_freq += (txc.offset * mtemp) >>
		      (time_constant + time_constant);

		  ltemp = time_tolerance << SHIFT_KF;

		  if (time_freq > ltemp)
		    time_freq = ltemp;
		  else if (time_freq < -ltemp)
		    time_freq = -ltemp;
		}
	    if (txc.mode & ADJ_TICK)
	      tick = txc.tick;

	}
	txc.offset	   = save_adjust;
	txc.frequency	   = ((time_freq+1) >> (SHIFT_KF - 16));
	txc.maxerror	   = time_maxerror;
	txc.esterror	   = time_esterror;
	txc.status	   = time_status;
	txc.time_constant  = time_constant;
	txc.precision	   = time_precision;
	txc.tolerance	   = time_tolerance;
	txc.time	   = xtime;
	txc.tick	   = tick;

	sti();

	memcpy_tofs(txc_p, &txc, sizeof(struct timex));
	return time_status;
}

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 */
int set_rtc_mmss(unsigned long nowtime)
{
  int retval = 0;
  int real_seconds, real_minutes, cmos_minutes;
  unsigned char save_control, save_freq_select;

  save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
  CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

  save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
  CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

  cmos_minutes = CMOS_READ(RTC_MINUTES);
  if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
    BCD_TO_BIN(cmos_minutes);

  /* since we're only adjusting minutes and seconds,
   * don't interfere with hour overflow. This avoids
   * messing with unknown time zones but requires your
   * RTC not to be off by more than 15 minutes
   */
  real_seconds = nowtime % 60;
  real_minutes = nowtime / 60;
  if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
    real_minutes += 30;		/* correct for half hour time zone */
  real_minutes %= 60;

  if (abs(real_minutes - cmos_minutes) < 30)
    {
      if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
	  BIN_TO_BCD(real_seconds);
	  BIN_TO_BCD(real_minutes);
	}
      CMOS_WRITE(real_seconds,RTC_SECONDS);
      CMOS_WRITE(real_minutes,RTC_MINUTES);
    }
  else
    retval = -1;

  /* The following flags have to be released exactly in this order,
   * otherwise the DS12887 (popular MC146818A clone with integrated
   * battery and quartz) will not reset the oscillator and will not
   * update precisely 500 ms later. You won't find this mentioned in
   * the Dallas Semiconductor data sheets, but who believes data
   * sheets anyway ...                           -- Markus Kuhn
   */
  CMOS_WRITE(save_control, RTC_CONTROL);
  CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

  return retval;
}
