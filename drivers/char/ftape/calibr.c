/* Yo, Emacs! we're -*- Linux-C -*-
 *
 *      Copyright (C) 1993-1995 Bas Laarhoven.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *      GP calibration routine for processor speed dependent
 *      functions.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/ftape.h>
#include <asm/system.h>
#include <asm/io.h>

#include "tracing.h"
#include "calibr.h"
#include "fdc-io.h"

#undef DEBUG

unsigned timestamp(void)
{
	unsigned count;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(0x00, 0x43);	/* latch the count ASAP */
	count = inb_p(0x40);	/* read the latched count */
	count |= inb(0x40) << 8;
	restore_flags(flags);
	return (LATCH - count);	/* normal: downcounter */
}

int timediff(int t0, int t1)
{
	/*  Calculate difference in usec for timestamp results t0 & t1.
	 *  Note that the maximum timespan allowed is 1/HZ or we'll lose ticks!
	 */
	if (t1 < t0) {
		t1 += LATCH;
	}
	return (1000 * (t1 - t0)) / ((CLOCK_TICK_RATE + 500) / 1000);
}

/*      To get an indication of the I/O performance,
 *      measure the duration of the inb() function.
 */
void time_inb(void)
{
	TRACE_FUN(8, "time_inb");
	int i;
	int t0, t1;
	unsigned long flags;
	int status;

	save_flags(flags);
	cli();
	t0 = timestamp();
	for (i = 0; i < 1000; ++i) {
		status = inb(fdc.msr);
	}
	t1 = timestamp();
	restore_flags(flags);
	if (t1 - t0 <= 0) {
		t1 += LATCH;
	}
	TRACEx1(4, "inb() duration: %d nsec", timediff(t0, t1));
	TRACE_EXIT;
}

/*  Haven't studied on why, but there sometimes is a problem
 *  with the tick timer readout. The two bytes get swapped.
 *  This hack solves that problem by doing one extra input.
 */
void fix_clock(void)
{
	TRACE_FUN(8, "fix_clock");
	int t;
	int i;

	for (i = 0; i < 1000; ++i) {
		t = timestamp();
		if (t < 0) {
			inb_p(0x40);	/* get in sync again */
			TRACE(2, "clock counter fixed");
			break;
		}
	}
	TRACE_EXIT;
}

/*
 *      Input:  function taking int count as parameter.
 *              pointers to calculated calibration variables.
 */
int calibrate(char *name, void (*fun) (int), int *calibr_count, int *calibr_time)
{
	TRACE_FUN(5, "calibrate");
	static int first_time = 1;
	int i;
	int old_tc = 0;
	int old_count = 1;
	int old_time = 1;

	if (first_time) {	/* get idea of I/O performance */
		fix_clock();
		time_inb();
		first_time = 0;
	}
	/*    value of timeout must be set so that on very slow systems
	 *    it will give a time less than one jiffy, and on
	 *    very fast systems it'll give reasonable precision.
	 */

	*calibr_count = 10;
	for (i = 0; i < 15; ++i) {
		int t0, t1;
		unsigned long flags;
		int once;
		int multiple;
		int tc;

		*calibr_time = *calibr_count;	/* set TC to 1 */
		fun(0);		/* dummy, get code into cache */
		save_flags(flags);
		cli();
		t0 = timestamp();
		fun(0);		/* overhead + one test */
		t1 = timestamp();
		if (t1 < t0) {
			t1 += LATCH;
		}
		once = t1 - t0;
		t0 = timestamp();
		fun(*calibr_count);	/* overhead + multiple tests */
		t1 = timestamp();
		if (t1 < t0) {
			t1 += LATCH;
		}
		multiple = t1 - t0;
		restore_flags(flags);
		*calibr_time = (10000 * (multiple - once)) / (CLOCK_TICK_RATE / 100);
		--*calibr_count;	/* because delta corresponds to this count */
		tc = (1000 * *calibr_time) / *calibr_count;
		TRACEx4(8, "once:%4d us,%5d times:%6d us, TC:%5d ns",
			(10000 * once) / (CLOCK_TICK_RATE / 100),
			*calibr_count,
			(10000 * multiple) / (CLOCK_TICK_RATE / 100),
			tc);
		/*
		 * increase the count until the resulting time nears 2/HZ,
		 * then the tc will drop sharply because we lose LATCH counts.
		 */
		if (tc <= old_tc / 2) {
			*calibr_time = old_time;
			*calibr_count = old_count;
			break;
		}
		old_tc = tc;
		old_count = *calibr_count;
		old_time = *calibr_time;
		*calibr_count *= 2;
	}
	TRACEx3(4, "TC for `%s()' = %d nsec (at %d counts)",
	     name, (1000 * *calibr_time) / *calibr_count, *calibr_count);
	TRACE_EXIT;
	return 0;
}
