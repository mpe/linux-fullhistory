/*
 * Support for periodic interrupts (100 per second) and for getting
 * the current time from the RTC on Power Macintoshes.
 *
 * We use the decrementer register for our periodic interrupts.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/pmu.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>

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

__pmac

unsigned long pmac_get_rtc_time(void)
{
	struct adb_request req;

	/* Get the time from the RTC */
	switch (adb_hardware) {
	case ADB_VIACUDA:
		if (cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_GET_TIME) < 0)
			return 0;
		while (!req.complete)
			cuda_poll();
		if (req.reply_len != 7)
			printk(KERN_ERR "pmac_get_rtc_time: got %d byte reply\n",
			       req.reply_len);
		return (req.reply[3] << 24) + (req.reply[4] << 16)
			+ (req.reply[5] << 8) + req.reply[6] - RTC_OFFSET;
	case ADB_VIAPMU:
		if (pmu_request(&req, NULL, 1, PMU_READ_RTC) < 0)
			return 0;
		while (!req.complete)
			pmu_poll();
		if (req.reply_len != 5)
			printk(KERN_ERR "pmac_get_rtc_time: got %d byte reply\n",
			       req.reply_len);
		return (req.reply[1] << 24) + (req.reply[2] << 16)
			+ (req.reply[3] << 8) + req.reply[4] - RTC_OFFSET;
	default:
		return 0;
	}
}

int pmac_set_rtc_time(unsigned long nowtime)
{
	return 0;
}

/*
 * Calibrate the decrementer register using VIA timer 1.
 * This is used both on powermacs and CHRP machines.
 */
__initfunc(int via_calibrate_decr(void))
{
	struct device_node *vias;
	volatile unsigned char *via;
	int count = VIA_TIMER_FREQ_6 / HZ;
	unsigned int dstart, dend;

	vias = find_devices("via-cuda");
	if (vias == 0)
		vias = find_devices("via-pmu");
	if (vias == 0)
		vias = find_devices("via");
	if (vias == 0 || vias->n_addrs == 0)
		return 0;
	via = (volatile unsigned char *)
		ioremap(vias->addrs[0].address, vias->addrs[0].size);

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

#ifdef CONFIG_PMAC_PBOOK
/*
 * Reset the time after a sleep.
 */
static int time_sleep_notify(struct notifier_block *this, unsigned long event,
			     void *x)
{
	static unsigned long time_diff;

	switch (event) {
	case PBOOK_SLEEP:
		time_diff = xtime.tv_sec - pmac_get_rtc_time();
		break;
	case PBOOK_WAKE:
		xtime.tv_sec = pmac_get_rtc_time() + time_diff;
		xtime.tv_usec = 0;
		set_dec(decrementer_count);
		last_rtc_update = xtime.tv_sec;
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block time_sleep_notifier = {
	time_sleep_notify, NULL, 100
};
#endif /* CONFIG_PMAC_PBOOK */

/*
 * Query the OF and get the decr frequency.
 * This was taken from the pmac time_init() when merging the prep/pmac
 * time functions.
 */
__initfunc(void pmac_calibrate_decr(void))
{
	struct device_node *cpu;
	int freq, *fp, divisor;

#ifdef CONFIG_PMAC_PBOOK
	notifier_chain_register(&sleep_notifier_list, &time_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */

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

