/*
 * $Id: time.c,v 1.8 1997/08/11 08:37:51 cort Exp $
 * Common time routines among all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
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

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>

#include "time.h"

/* this is set to the appropriate pmac/prep func in init_IRQ() */
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

/* Accessor functions for the decrementer register. */
inline unsigned long
get_dec(void)
{
	int ret;

	asm volatile("mfspr %0,22" : "=r" (ret) :);
	return ret;
}

inline void
set_dec(int val)
{
	asm volatile("mtspr 22,%0" : : "r" (val));
}

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
#ifdef CONFIG_PREP
	/* pmac hasn't yet called via_cuda_init() */
	if ( _machine != _MACH_Pmac ) /* prep */
	{
		xtime.tv_sec = prep_get_rtc_time();
		xtime.tv_usec = 0;
		/*
		 * mark the rtc/on-chip timer as in sync
		 * so we don't update right away
		 */
		last_rtc_update = xtime.tv_sec;
	}
#endif /* CONFIG_PREP */
	if ((_get_PVR() >> 16) == 1) {
		/* 601 processor: dec counts down by 128 every 128ns */
		decrementer_count = DECREMENTER_COUNT_601;
		count_period_num = COUNT_PERIOD_NUM_601;
		count_period_den = COUNT_PERIOD_DEN_601;
	} else {
		/*
		 * These should setup decrementer_count 
		 */
		if ( _machine == _MACH_Pmac )
			pmac_calibrate_decr();
#ifdef CONFIG_PREP
		else /* PReP */
			prep_calibrate_decr();
#endif /* CONFIG_PREP */
	}

	if ( _machine == _MACH_Pmac )
		set_rtc_time = pmac_set_rtc_time;
#ifdef CONFIG_PREP
	else /* prep */
		set_rtc_time = prep_set_rtc_time;
#endif /* CONFIG_PREP */
		
	set_dec(decrementer_count);
}

#ifdef CONFIG_PREP
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
	int freq, divisor;
	static unsigned long t1 = 0, t2 = 0;

	if ( !t1 )
		t1 = get_dec();
	else if (!t2)
	{
		t2 = get_dec();
		t2 = t1-t2;  /* decr's in 1/HZ */
		t2 = t2*HZ;  /* # decrs in 1s - thus in Hz */
		freq = t2 * 60;	/* try to make freq/1e6 an integer */
		divisor = 60;
		printk("time_init: decrementer frequency = %d/%d (%luMHz)\n",
		       freq, divisor,t2>>20);
		decrementer_count = freq / HZ / divisor;
		count_period_num = divisor;
		count_period_den = freq / 1000000;
		*done_ptr = 1;
	}
}
#endif /* CONFIG_PREP */
