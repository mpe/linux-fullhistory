/*
 * Support for periodic interrupts (100 per second) and for getting
 * the current time from the RTC on Power Macintoshes.
 *
 * We use the decrementer register for our periodic interrupts.
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
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/prom.h>
#include <asm/system.h>

#include "time.h"

/* Apparently the RTC stores seconds since 1 Jan 1904 */
#define RTC_OFFSET	2082844800

/*
 * Calibrate the decrementer frequency with the VIA timer 1.
 */
#define VIA_TIMER_FREQ_6	4700000	/* time 1 frequency * 6 */

/* VIA registers */
#define RS		0x200		/* skip between registers */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define ACR		(11*RS)		/* Auxiliary control register */
#define IFR		(13*RS)		/* Interrupt flag register */

/* Bits in ACR */
#define T1MODE		0xc0		/* Timer 1 mode */
#define T1MODE_CONT	0x40		/*  continuous interrupts */

/* Bits in IFR and IER */
#define T1_INT		0x40		/* Timer 1 interrupt */

static int via_calibrate_decr(void)
{
	struct device_node *vias;
	volatile unsigned char *via;
	int count = VIA_TIMER_FREQ_6 / HZ;
	unsigned int dstart, dend;

	vias = find_devices("via-cuda");
	if (vias == 0)
		vias = find_devices("via-pmu");
	if (vias == 0 || vias->n_addrs == 0)
		return 0;
	via = (volatile unsigned char *) vias->addrs[0].address;

	/* set timer 1 for continuous interrupts */
	out_8(&via[ACR], (via[ACR] & ~T1MODE) | T1MODE_CONT);
	/* set the counter to a small value */
	out_8(&via[T1CH], 2);
	/* set the latch to `count' */
	out_8(&via[T1LL], count);
	out_8(&via[T1LH], count >> 8);
	/* wait until it hits 0 */
	while ((in_8(&via[IFR]) & T1_INT) == 0)
		;
	dstart = get_dec();
	/* clear the interrupt & wait until it hits 0 again */
	in_8(&via[T1CL]);
	while ((in_8(&via[IFR]) & T1_INT) == 0)
		;
	dend = get_dec();

	decrementer_count = (dstart - dend) / 6;
	count_period_num = 60;
	count_period_den = decrementer_count * 6 * HZ / 100000;

	printk(KERN_INFO "via_calibrate_decr: decrementer_count = %u (%u ticks)\n",
	       decrementer_count, dstart - dend);

	return 1;
}

/*
 * Query the OF and get the decr frequency.
 * This was taken from the pmac time_init() when merging the prep/pmac
 * time functions.
 */
void pmac_calibrate_decr(void)
{
	struct device_node *cpu;
	int freq, *fp, divisor;

	if (via_calibrate_decr())
		return;

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
	struct adb_request req;

	/* Get the time from the RTC */
	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_GET_TIME);
	while (!req.complete)
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
	last_rtc_update = xtime.tv_sec;
}
