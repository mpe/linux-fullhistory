/* $Id: time.c,v 1.3 1995/11/25 00:58:45 davem Exp $
 * linux/arch/sparc/kernel/time.c
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * This file handles the Sparc specific time handling details.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/timex.h>

#include <asm/segment.h>
#include <asm/timer.h>

#define TIMER_IRQ  10    /* Also at level 14, but we ignore that one. */

static int set_rtc_mmss(unsigned long);

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
void timer_interrupt(int irq, struct pt_regs * regs)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;
	volatile unsigned int clear_intr;

	/* First, clear the interrupt. */
	clear_intr = *master_l10_limit;

	do_timer(regs);

	/* XXX I don't know if this is right for the Sparc yet. XXX */
	if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
}

void time_init(void)
{
	request_irq(TIMER_IRQ, timer_interrupt, SA_INTERRUPT, "timer");
	return;
}
/* Nothing fancy on the Sparc yet. */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	*tv = xtime;
	restore_flags(flags);
}

void do_settimeofday(struct timeval *tv)
{
	cli();
	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	sti();
}

/* XXX Mostek RTC code needs to be written XXX */
static int set_rtc_mmss(unsigned long nowtime)
{
	/* Just say we succeeded for now. */
	return 0;
}
