/*
 * Support for periodic interrupts (100 per second) and for getting
 * the current time from the RTC on Power Macintoshes.
 *
 * At present, we use the decrementer register in the 601 CPU
 * for our periodic interrupts.  This will probably have to be
 * changed for other processors.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/cuda.h>
#include <asm/prom.h>
#include <asm/system.h>

static int get_dec(void);
static void set_dec(int);
static unsigned long get_rtc_time(void);

/* Apparently the RTC stores seconds since 1 Jan 1904 */
#define RTC_OFFSET	2082844800

/* Accessor functions for the decrementer register. */
static inline int
get_dec()
{
    int ret;

    asm volatile("mfspr %0,22" : "=r" (ret) :);
    return ret;
}

static inline void
set_dec(int val)
{
    asm volatile("mtspr 22,%0" : : "r" (val));
}

/* The decrementer counts down by 128 every 128ns on a 601. */
#define DECREMENTER_COUNT_601	(1000000000 / HZ)
#define COUNT_PERIOD_NUM_601	1
#define COUNT_PERIOD_DEN_601	1000

unsigned decrementer_count;	/* count value for 1e6/HZ microseconds */
unsigned count_period_num;	/* 1 decrementer count equals */
unsigned count_period_den;	/* count_period_num / count_period_den us */

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

	frac_tick = tv->tv_usec % (1000000 / HZ);
	save_flags(flags);
	cli();
	xtime.tv_sec = tv->tv_sec;
	xtime.tv_usec = tv->tv_usec - frac_tick;
	set_dec(frac_tick * count_period_den / count_period_num);
	restore_flags(flags);
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
	}

}

void
time_init(void)
{
	struct device_node *cpu;
	int freq, *fp, divisor;

	if ((_get_PVR() >> 16) == 1) {
		/* 601 processor: dec counts down by 128 every 128ns */
		decrementer_count = DECREMENTER_COUNT_601;
		count_period_num = COUNT_PERIOD_NUM_601;
		count_period_den = COUNT_PERIOD_DEN_601;
	} else {
		/*
		 * The cpu node should have a timebase-frequency property
		 * to tell us the rate at which the decrementer counts.
		 */
		cpu = find_type_devices("cpu");
		if (cpu == 0)
			panic("can't find cpu node in time_init");
		fp = (int *) get_property(cpu, "timebase-frequency", NULL);
		if (fp == 0)
			panic("can't get cpu timebase frequency");
		freq = *fp * 60;	/* try to make freq/1e6 an integer */
		divisor = 60;
		printk("time_init: decrementer frequency = %d/%d\n",
		       freq, divisor);
		decrementer_count = freq / HZ / divisor;
		count_period_num = divisor;
		count_period_den = freq / 1000000;
	}
	set_dec(decrementer_count);
}

/*
 * We can't do this in time_init, because via_cuda_init hasn't
 * been called at that stage.
 */
void
read_rtc_time(void)
{
	xtime.tv_sec = get_rtc_time();
	xtime.tv_usec = 0;
}

static unsigned long
get_rtc_time()
{
	struct cuda_request req;

	/* Get the time from the RTC */
	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_GET_TIME);
	while (!req.got_reply)
		cuda_poll();
	if (req.reply_len != 7)
		panic("get_rtc_time: didn't expect %d byte reply",
		      req.reply_len);
	return (req.reply[3] << 24) + (req.reply[4] << 16)
		+ (req.reply[5] << 8) + req.reply[6] - RTC_OFFSET;
}
