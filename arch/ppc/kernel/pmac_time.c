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

#include "time.h"


/* Apparently the RTC stores seconds since 1 Jan 1904 */
#define RTC_OFFSET	2082844800

/*
 * Query the OF and get the decr frequency.
 * This was taken from the pmac time_init() when merging the prep/pmac
 * time functions.
 */
void pmac_calibrate_decr(void)
{
	struct device_node *cpu;
	int freq, *fp, divisor;

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

unsigned long
pmac_get_rtc_time(void)
{
	struct cuda_request req;

	/* Get the time from the RTC */
	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_GET_TIME);
	while (!req.got_reply)
		cuda_poll();
	if (req.reply_len != 7)
		printk(KERN_ERR "pmac_get_rtc_time: got %d byte reply\n",
		      req.reply_len);
	return (req.reply[3] << 24) + (req.reply[4] << 16)
		+ (req.reply[5] << 8) + req.reply[6] - RTC_OFFSET;
}

int pmac_set_rtc_time(unsigned long nowtime)
{
	return 0;
}

/*
 * We can't do this in time_init, because via_cuda_init hasn't
 * been called at that stage.
 */
void
pmac_read_rtc_time(void)
{
	xtime.tv_sec = pmac_get_rtc_time();
	xtime.tv_usec = 0;
}
