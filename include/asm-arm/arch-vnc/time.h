/*
 * linux/include/asm-arm/arch-vnc/time.h
 *
 * Copyright (c) 1997 Corel Computer Corp.
 * Slight modifications to bring in line with ebsa285 port.
 *  -- Russell King.
 *  Added LED driver (based on the ebsa285 code) - Alex Holden 28/12/98.
 */

#include <linux/config.h>
#include <linux/mc146818rtc.h>

#include <asm/leds.h>
#include <asm/system.h>

#undef IRQ_TIMER
#define IRQ_TIMER		IRQ_TIMER4

#define mSEC_10_from_14 ((14318180 + 100) / 200)

extern __inline__ unsigned long gettimeoffset (void)
{
	int count;

	static int count_p = (mSEC_10_from_14/6);    /* for the first call after boot */
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off. 
	 */
	unsigned long jiffies_t;

	/* timer count may underflow right here */
	outb_p(0x00, 0x43);	/* latch the count ASAP */

	count = inb_p(0x40);	/* read the latched count */

	/*
	 * We do this guaranteed double memory access instead of a _p 
	 * postfix in the previous port access. Wheee, hackady hack
	 */
 	jiffies_t = jiffies;

	count |= inb_p(0x40) << 8;

	/* Detect timer underflows.  If we haven't had a timer tick since 
	   the last time we were called, and time is apparently going
	   backwards, the counter must have wrapped during this routine. */
	if ((jiffies_t == jiffies_p) && (count > count_p))
		count -= (mSEC_10_from_14/6);
	else
		jiffies_p = jiffies_t;

	count_p = count;

	count = (((mSEC_10_from_14/6)-1) - count) * tick;
	count = (count + (mSEC_10_from_14/6)/2) / (mSEC_10_from_14/6);

	return count;
}

extern __inline__ int reset_timer (void)
{
#ifdef CONFIG_LEDS
	static unsigned int count = 50;
	static int last_pid;

	if (current->pid != last_pid) {
		last_pid = current->pid;
		if (last_pid)
			leds_event(led_idle_end);
		else
			leds_event(led_idle_start);
	}

	if (--count == 0) {
		count = 50;
		leds_event(led_timer);
	}
#endif
	return 1;
}

unsigned long set_rtc_mmss(unsigned long nowtime)
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

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			BIN_TO_BCD(real_seconds);
			BIN_TO_BCD(real_minutes);
		}
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else
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

/*
 * We don't have a RTC to update!
 */
extern __inline__ void update_rtc(void)
{
	static long last_rtc_update = 0;	/* last time the cmos clock got updated */

	/* If we have an externally synchronized linux clock, then update
	 * CMOS clock accordingly every ~11 minutes.  Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 50000 - (tick >> 1) &&
	    xtime.tv_usec < 50000 + (tick >> 1)) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
}

extern __inline__ unsigned long get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	int i;

	// check to see if the RTC makes sense.....
	if ((CMOS_READ(RTC_VALID) & RTC_VRT) == 0)
		return mktime(1970, 1, 1, 0, 0, 0);

	/* The Linux interpretation of the CMOS clock register contents:
	 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
	 * RTC registers show the second which has precisely just started.
	 * Let's hope other operating systems interpret the RTC the same way.
	 */
	/* read RTC exactly on falling edge of update flag */
	for (i = 0 ; i < 1000000 ; i++) /* may take up to 1 second... */
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
			break;

	for (i = 0 ; i < 1000000 ; i++) /* must try at least 2.228 ms */
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

	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);
	}
	if ((year += 1900) < 1970)
		year += 100;
	return mktime(year, mon, day, hour, min, sec);
}

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
extern __inline__ unsigned long setup_timer (void)
{
	unsigned int c;

	/* Turn on the RTC */
	outb(13, 0x70);
	if ((inb(0x71) & 0x80) == 0)
		printk("RTC: *** warning: CMOS battery bad\n");

	outb(10, 0x70);		/* select control reg */
	outb(32, 0x71);		/* make sure the divider is set */
	outb(11, 0x70);		/* select other control reg */
	c = inb(0x71) & 0xfb;	/* read it */
	outb(11, 0x70);
	outb(c | 2, 0x71);	/* turn on BCD counting and 24 hour clock mode */
	
	/* enable PIT timer */
	/* set for periodic (4) and LSB/MSB write (0x30) */
	outb(0x34, 0x43);
	outb((mSEC_10_from_14/6) & 0xFF, 0x40);
	outb((mSEC_10_from_14/6) >> 8, 0x40);

	/*
	 * Default the date to 1 Jan 1970 00:00:00
	 * You will have to run a time daemon to set the
	 * clock correctly at bootup
	 */
	return get_cmos_time();
}
