/* Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Paul Eggert (eggert@twinsun.com).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/*
 * dgb 10/2/98: ripped this from glibc source to help convert timestamps to unix time 
 *     10/4/98: added new table-based lookup after seeing how ugly the gnu code is
 */

/* Assume that leap seconds are possible, unless told otherwise.
   If the host has a `zic' command with a `-L leapsecondfilename' option,
   then it supports leap seconds; otherwise it probably doesn't.  */
#ifndef LEAP_SECONDS_POSSIBLE
#define LEAP_SECONDS_POSSIBLE 1
#endif

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#include <linux/kernel.h>
#else
#include <stdio.h>
#include <sys/types.h>
#endif

#include "udfdecl.h"

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

#ifndef INT_MIN
#define INT_MIN (~0 << (sizeof (int) * CHAR_BIT - 1))
#endif
#ifndef INT_MAX
#define INT_MAX (~0 - INT_MIN)
#endif

#ifndef TIME_T_MIN
#define TIME_T_MIN (0 < (time_t) -1 ? (time_t) 0 \
		    : ~ (time_t) 0 << (sizeof (time_t) * CHAR_BIT - 1))
#endif
#ifndef TIME_T_MAX
#define TIME_T_MAX (~ (time_t) 0 - TIME_T_MIN)
#endif

#define TM_YEAR_BASE 1900
#define EPOCH_YEAR 1970

#ifndef __isleap
/* Nonzero if YEAR is a leap year (every 4 years,
   except every 100th isn't, and every 400th is).  */
#define	__isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))
#endif

/* How many days come before each month (0-12).  */
const unsigned short int __mon_yday[2][13] =
  {
    /* Normal years.  */
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    /* Leap years.  */
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
  };

time_t udf_converttime (struct ktm *);
#ifndef USE_GNU_MKTIME_METHOD

#define MAX_YEAR_SECONDS	68

time_t year_seconds[MAX_YEAR_SECONDS]= {
	0, 
	/*1971:*/ 31554000, /*1972:*/ 63090000, /*1973:*/ 94712400,
 	/*1974:*/ 126248400, /*1975:*/ 157784400, /*1976:*/ 189320400,
 	/*1977:*/ 220942800, /*1978:*/ 252478800, /*1979:*/ 284014800,
 	/*1980:*/ 315550800, /*1981:*/ 347173200, /*1982:*/ 378709200,
 	/*1983:*/ 410245200, /*1984:*/ 441781200, /*1985:*/ 473403600,
 	/*1986:*/ 504939600, /*1987:*/ 536475600, /*1988:*/ 568011600,
 	/*1989:*/ 599634000, /*1990:*/ 631170000, /*1991:*/ 662706000,
 	/*1992:*/ 694242000, /*1993:*/ 725864400, /*1994:*/ 757400400,
 	/*1995:*/ 788936400, /*1996:*/ 820472400, /*1997:*/ 852094800,
 	/*1998:*/ 883630800, /*1999:*/ 915166800, /*2000:*/ 946702800,
 	/*2001:*/ 978325200, /*2002:*/ 1009861200, /*2003:*/ 1041397200,
 	/*2004:*/ 1072933200, /*2005:*/ 1104555600, /*2006:*/ 1136091600,
 	/*2007:*/ 1167627600, /*2008:*/ 1199163600, /*2009:*/ 1230786000,
 	/*2010:*/ 1262322000, /*2011:*/ 1293858000, /*2012:*/ 1325394000,
 	/*2013:*/ 1357016400, /*2014:*/ 1388552400, /*2015:*/ 1420088400,
 	/*2016:*/ 1451624400, /*2017:*/ 1483246800, /*2018:*/ 1514782800,
 	/*2019:*/ 1546318800, /*2020:*/ 1577854800, /*2021:*/ 1609477200,
 	/*2022:*/ 1641013200, /*2023:*/ 1672549200, /*2024:*/ 1704085200,
 	/*2025:*/ 1735707600, /*2026:*/ 1767243600, /*2027:*/ 1798779600,
 	/*2028:*/ 1830315600, /*2029:*/ 1861938000, /*2030:*/ 1893474000,
 	/*2031:*/ 1925010000, /*2032:*/ 1956546000, /*2033:*/ 1988168400,
 	/*2034:*/ 2019704400, /*2035:*/ 2051240400, /*2036:*/ 2082776400,
 	/*2037:*/ 2114398800
};

time_t udf_converttime (struct ktm *tm)
{
    time_t r;
    int yday;

	if ( !tm )
		return -1;
	if ( (tm->tm_year+TM_YEAR_BASE < EPOCH_YEAR) || 
		 (tm->tm_year+TM_YEAR_BASE > EPOCH_YEAR+MAX_YEAR_SECONDS) )
		return -1;
	r = year_seconds[tm->tm_year-70];

	yday = ((__mon_yday[__isleap (tm->tm_year + TM_YEAR_BASE)]
	       [tm->tm_mon-1])
	      + tm->tm_mday - 1);
	r += ( ( (yday* 24) + (tm->tm_hour-1) ) * 60 + tm->tm_min ) * 60 + tm->tm_sec;
	return r;
}

#ifdef __KERNEL__

extern struct timezone sys_tz;

#define SECS_PER_HOUR   (60 * 60)
#define SECS_PER_DAY    (SECS_PER_HOUR * 24)

timestamp *
udf_time_to_stamp(timestamp *dest, time_t tv_sec, long tv_usec)
{
	long int days, rem, y;
	const unsigned short int *ip;
	int offset = (-sys_tz.tz_minuteswest + (sys_tz.tz_dsttime ? 60 : 0));

    if (!dest)
        return NULL;

	dest->typeAndTimezone = 0x1000 | (offset & 0x0FFF);

	tv_sec += offset * 60;
	days = tv_sec / SECS_PER_DAY;
	rem = tv_sec % SECS_PER_DAY;
	dest->hour = rem / SECS_PER_HOUR;
	rem %= SECS_PER_HOUR;
	dest->minute = rem / 60;
	dest->second = rem % 60;
	y = 1970;

#define DIV(a,b) ((a) / (b) - ((a) % (b) < 0))
#define LEAPS_THRU_END_OF(y) (DIV (y, 4) - DIV (y, 100) + DIV (y, 400))

	while (days < 0 || days >= (__isleap(y) ? 366 : 365))
	{
		long int yg = y + days / 365 - (days % 365 < 0);

		/* Adjust DAYS and Y to match the guessed year.  */
		days -= ((yg - y) * 365
			+ LEAPS_THRU_END_OF (yg - 1)
			- LEAPS_THRU_END_OF (y - 1));
		y = yg;
	}
	dest->year = y;
	ip = __mon_yday[__isleap(y)];
	for (y = 11; days < (long int) ip[y]; --y)
		continue;
	days -= ip[y];
	dest->month = y + 1;
	dest->day = days + 1;

	dest->centiseconds = tv_usec / 10000;
	dest->hundredsOfMicroseconds = (tv_usec - dest->centiseconds * 10000) / 100;
	dest->microseconds = (tv_usec - dest->centiseconds * 10000 -
		dest->hundredsOfMicroseconds * 100);
    return dest;
}
#endif

#else

static time_t ydhms_tm_diff (int, int, int, int, int, const struct ktm *);


/* Yield the difference between (YEAR-YDAY HOUR:MIN:SEC) and (*TP),
   measured in seconds, ignoring leap seconds.
   YEAR uses the same numbering as TM->tm_year.
   All values are in range, except possibly YEAR.
   If overflow occurs, yield the low order bits of the correct answer.  */
static time_t
ydhms_tm_diff (int year, int yday, int hour, int min, int sec, const struct ktm *tp)
{
  time_t result;

  /* Compute intervening leap days correctly even if year is negative.
     Take care to avoid int overflow.  time_t overflow is OK, since
     only the low order bits of the correct time_t answer are needed.
     Don't convert to time_t until after all divisions are done, since
     time_t might be unsigned.  */
  int a4 = (year >> 2) + (TM_YEAR_BASE >> 2) - ! (year & 3);
  int b4 = (tp->tm_year >> 2) + (TM_YEAR_BASE >> 2) - ! (tp->tm_year & 3);
  int a100 = a4 / 25 - (a4 % 25 < 0);
  int b100 = b4 / 25 - (b4 % 25 < 0);
  int a400 = a100 >> 2;
  int b400 = b100 >> 2;
  int intervening_leap_days = (a4 - b4) - (a100 - b100) + (a400 - b400);
  time_t years = year - (time_t) tp->tm_year;
  time_t days = (365 * years + intervening_leap_days);
  result= (60 * (60 * (24 * days + (hour - tp->tm_hour))
		+ (min - tp->tm_min))
	  + (sec - tp->tm_sec));
#ifdef __KERNEL__
  printk(KERN_ERR "udf: ydhms_tm_diff(%d,%d,%d,%d,%d,) returning %ld\n",
	year, yday, hour, min, sec, result);
#endif
  return result;
}


/* Convert *TP to a time_t value, inverting
   the monotonic and mostly-unit-linear conversion function CONVERT.
   Use *OFFSET to keep track of a guess at the offset of the result,
   compared to what the result would be for UTC without leap seconds.
   If *OFFSET's guess is correct, only one CONVERT call is needed.  */
time_t
udf_converttime (struct ktm *tp)
{
  time_t t, dt, t0;
  struct ktm tm;

  /* The maximum number of probes (calls to CONVERT) should be enough
     to handle any combinations of time zone rule changes, solar time,
     and leap seconds.  Posix.1 prohibits leap seconds, but some hosts
     have them anyway.  */
  int remaining_probes = 4;

  /* Time requested.  Copy it in case CONVERT modifies *TP; this can
     occur if TP is localtime's returned value and CONVERT is localtime.  */
  int sec = tp->tm_sec;
  int min = tp->tm_min;
  int hour = tp->tm_hour;
  int mday = tp->tm_mday;
  int mon = tp->tm_mon;
  int year_requested = tp->tm_year;
  int isdst = tp->tm_isdst;

  /* Ensure that mon is in range, and set year accordingly.  */
  int mon_remainder = mon % 12;
  int negative_mon_remainder = mon_remainder < 0;
  int mon_years = mon / 12 - negative_mon_remainder;
  int year = year_requested + mon_years;

  /* The other values need not be in range:
     the remaining code handles minor overflows correctly,
     assuming int and time_t arithmetic wraps around.
     Major overflows are caught at the end.  */

  /* Calculate day of year from year, month, and day of month.
     The result need not be in range.  */
  int yday = ((__mon_yday[__isleap (year + TM_YEAR_BASE)]
	       [mon_remainder + 12 * negative_mon_remainder])
	      + mday - 1);

#if LEAP_SECONDS_POSSIBLE
  /* Handle out-of-range seconds specially,
     since ydhms_tm_diff assumes every minute has 60 seconds.  */
  int sec_requested = sec;
  if (sec < 0)
    sec = 0;
  if (59 < sec)
    sec = 59;
#endif

  /* Invert CONVERT by probing.  First assume the same offset as last time.
     Then repeatedly use the error to improve the guess.  */

  tm.tm_year = EPOCH_YEAR - TM_YEAR_BASE;
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  /*
  t0 = ydhms_tm_diff (year, yday, hour, min, sec, &tm);

  for (t = t0; 
       (dt = ydhms_tm_diff (year, yday, hour, min, sec, &tm));
       t += dt)
    if (--remaining_probes == 0)
      return -1;
  */

  /* Check whether tm.tm_isdst has the requested value, if any.  */
  if (0 <= isdst && 0 <= tm.tm_isdst)
    {
      int dst_diff = (isdst != 0) - (tm.tm_isdst != 0);
      if (dst_diff)
	{
	  /* Move two hours in the direction indicated by the disagreement,
	     probe some more, and switch to a new time if found.
	     The largest known fallback due to daylight savings is two hours:
	     once, in Newfoundland, 1988-10-30 02:00 -> 00:00.  */
	  time_t ot = t - 2 * 60 * 60 * dst_diff;
	  while (--remaining_probes != 0)
	    {
	      struct ktm otm;
	      if (! (dt = ydhms_tm_diff (year, yday, hour, min, sec,
					 &otm)))
		{
		  t = ot;
		  tm = otm;
		  break;
		}
	      if ((ot += dt) == t)
		break;  /* Avoid a redundant probe.  */
	    }
	}
    }


#if LEAP_SECONDS_POSSIBLE
  if (sec_requested != tm.tm_sec)
    {
      /* Adjust time to reflect the tm_sec requested, not the normalized value.
	 Also, repair any damage from a false match due to a leap second.  */
      t += sec_requested - sec + (sec == 0 && tm.tm_sec == 60);
    }
#endif

  if (TIME_T_MAX / INT_MAX / 366 / 24 / 60 / 60 < 3)
    {
      /* time_t isn't large enough to rule out overflows in ydhms_tm_diff,
	 so check for major overflows.  A gross check suffices,
	 since if t has overflowed, it is off by a multiple of
	 TIME_T_MAX - TIME_T_MIN + 1.  So ignore any component of
	 the difference that is bounded by a small value.  */

      double dyear = (double) year_requested + mon_years - tm.tm_year;
      double dday = 366 * dyear + mday;
      double dsec = 60 * (60 * (24 * dday + hour) + min) + sec_requested;

      if (TIME_T_MAX / 3 - TIME_T_MIN / 3 < (dsec < 0 ? - dsec : dsec))
	return -1;
    }

  *tp = tm;
#ifdef __KERNEL__
  udf_debug("returning %ld\n", t);
#endif
  return t;
}
#endif

#ifdef INCLUDE_PRINT_KTM
static void
print_ktm (struct ktm *tp)
{
#ifdef __KERNEL__
  udf_debug(
#else
  printf(
#endif
	"%04d-%02d-%02d %02d:%02d:%02d isdst %d",
	  tp->tm_year + TM_YEAR_BASE, tp->tm_mon + 1, tp->tm_mday,
	  tp->tm_hour, tp->tm_min, tp->tm_sec, tp->tm_isdst);
}
#endif

/* EOF */
