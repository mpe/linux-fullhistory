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
 * 1995-08-13    Torsten Duwe
 *      kernel PLL updated to 1994-12-13 specs (rfc-1489)
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/timex.h>

#include <asm/segment.h>

/* 
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz = { 0, 0};

#ifndef __alpha__

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */
asmlinkage int sys_time(int * tloc)
{
	int i;

	i = CURRENT_TIME;
	if (tloc) {
		int error = verify_area(VERIFY_WRITE, tloc, sizeof(*tloc));
		if (error)
			return error;
		put_user(i,tloc);
	}
	return i;
}

/*
 * sys_stime() can be implemented in user-level using
 * sys_settimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */
asmlinkage int sys_stime(int * tptr)
{
	int error, value;

	if (!suser())
		return -EPERM;
	error = verify_area(VERIFY_READ, tptr, sizeof(*tptr));
	if (error)
		return error;
	value = get_user(tptr);
	cli();
	xtime.tv_sec = value;
	xtime.tv_usec = 0;
	time_state = TIME_ERROR;
	time_maxerror = MAXPHASE;
	time_esterror = MAXPHASE;
	sti();
	return 0;
}

#endif

asmlinkage int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int error;

	if (tv) {
		struct timeval ktv;
		error = verify_area(VERIFY_WRITE, tv, sizeof *tv);
		if (error)
			return error;
		do_gettimeofday(&ktv);
		memcpy_tofs(tv, &ktv, sizeof(ktv));
	}
	if (tz) {
		error = verify_area(VERIFY_WRITE, tz, sizeof *tz);
		if (error)
			return error;
		memcpy_tofs(tz, &sys_tz, sizeof(sys_tz));
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
	if (tv)
		do_settimeofday(&new_tv);
	return 0;
}

long pps_offset = 0;		/* pps time offset (us) */
long pps_jitter = MAXTIME;	/* time dispersion (jitter) (us) */

long pps_freq = 0;		/* frequency offset (scaled ppm) */
long pps_stabil = MAXFREQ;	/* frequency dispersion (scaled ppm) */

long pps_valid = PPS_VALID;	/* pps signal watchdog counter */

int pps_shift = PPS_SHIFT;	/* interval duration (s) (shift) */

long pps_jitcnt = 0;		/* jitter limit exceeded */
long pps_calcnt = 0;		/* calibration intervals */
long pps_errcnt = 0;		/* calibration errors */
long pps_stbcnt = 0;		/* stability limit exceeded */

/* hook for a loadable hardpps kernel module */
void (*hardpps_ptr)(struct timeval *) = (void (*)(struct timeval *))0;

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
	if (txc.modes && !suser())
		return -EPERM;

	/* Now we validate the data before disabling interrupts
	 */

	if (txc.modes != ADJ_OFFSET_SINGLESHOT && (txc.modes & ADJ_OFFSET))
	  /* adjustment Offset limited to +- .512 seconds */
	  if (txc.offset <= - MAXPHASE || txc.offset >= MAXPHASE )
	    return -EINVAL;

	/* if the quartz is off by more than 10% something is VERY wrong ! */
	if (txc.modes & ADJ_TICK)
	  if (txc.tick < 900000/HZ || txc.tick > 1100000/HZ)
	    return -EINVAL;

	cli();

	/* Save for later - semantics of adjtime is to return old value */
	save_adjust = time_adjust;

	/* If there are input parameters, then process them */
	if (txc.modes)
	{
	    if (time_state == TIME_BAD)
		time_state = TIME_OK;

	    if (txc.modes & ADJ_STATUS)
		time_status = txc.status;

	    if (txc.modes & ADJ_FREQUENCY)
		time_freq = txc.freq;

	    if (txc.modes & ADJ_MAXERROR)
		time_maxerror = txc.maxerror;

	    if (txc.modes & ADJ_ESTERROR)
		time_esterror = txc.esterror;

	    if (txc.modes & ADJ_TIMECONST)
		time_constant = txc.constant;

	    if (txc.modes & ADJ_OFFSET)
	      if ((txc.modes == ADJ_OFFSET_SINGLESHOT)
		  || !(time_status & STA_PLL))
		{
		  time_adjust = txc.offset;
		}
	      else if ((time_status & STA_PLL)||(time_status & STA_PPSTIME))
		{
		  ltemp = (time_status & STA_PPSTIME &&
			   time_status & STA_PPSSIGNAL) ?
		    pps_offset : txc.offset;

		  /*
		   * Scale the phase adjustment and
		   * clamp to the operating range.
		   */
		  if (ltemp > MAXPHASE)
		    time_offset = MAXPHASE << SHIFT_UPDATE;
		  else if (ltemp < -MAXPHASE)
		    time_offset = -(MAXPHASE << SHIFT_UPDATE);
		  else
		    time_offset = ltemp << SHIFT_UPDATE;

		  /*
		   * Select whether the frequency is to be controlled and in which
		   * mode (PLL or FLL). Clamp to the operating range. Ugly
		   * multiply/divide should be replaced someday.
		   */

		  if (time_status & STA_FREQHOLD || time_reftime == 0)
		    time_reftime = xtime.tv_sec;
		  mtemp = xtime.tv_sec - time_reftime;
		  time_reftime = xtime.tv_sec;
		  if (time_status & STA_FLL)
		    {
		      if (mtemp >= MINSEC)
			{
			  ltemp = ((time_offset / mtemp) << (SHIFT_USEC -
							     SHIFT_UPDATE));
			  if (ltemp < 0)
			    time_freq -= -ltemp >> SHIFT_KH;
			  else
			    time_freq += ltemp >> SHIFT_KH;
			}
		    } 
		  else 
		    {
		      if (mtemp < MAXSEC)
			{
			  ltemp *= mtemp;
			  if (ltemp < 0)
			    time_freq -= -ltemp >> (time_constant +
						    time_constant + SHIFT_KF -
						    SHIFT_USEC);
			  else
			    time_freq += ltemp >> (time_constant +
						   time_constant + SHIFT_KF -
						   SHIFT_USEC);
			}
		    }
		  if (time_freq > time_tolerance)
		    time_freq = time_tolerance;
		  else if (time_freq < -time_tolerance)
		    time_freq = -time_tolerance;
		} /* STA_PLL || STA_PPSTIME */
	    if (txc.modes & ADJ_TICK)
	      tick = txc.tick;

	}
	txc.offset	   = save_adjust;
	txc.freq	   = time_freq;
	txc.maxerror	   = time_maxerror;
	txc.esterror	   = time_esterror;
	txc.status	   = time_status;
	txc.constant	   = time_constant;
	txc.precision	   = time_precision;
	txc.tolerance	   = time_tolerance;
	txc.time	   = xtime;
	txc.tick	   = tick;
	txc.ppsfreq	   = pps_freq;
	txc.jitter	   = pps_jitter;
	txc.shift	   = pps_shift;
	txc.stabil	   = pps_stabil;
	txc.jitcnt	   = pps_jitcnt;
	txc.calcnt	   = pps_calcnt;
	txc.errcnt	   = pps_errcnt;
	txc.stbcnt	   = pps_stbcnt;

	sti();

	memcpy_tofs(txc_p, &txc, sizeof(struct timex));
	return time_state;
}
