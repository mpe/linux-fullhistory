/*
 * linux/include/asm-arm/arch-ebsa285/time.h
 *
 * Copyright (c) 1998 Russell King.
 * Copyright (c) 1998 Phil Blundell
 *
 * CATS has a real-time clock, though the evaluation board doesn't.
 *
 * Changelog:
 *  21-Mar-1998	RMK	Created
 *  27-Aug-1998	PJB	CATS support
 *  28-Dec-1998	APH	Made leds optional
 */

#define RTC_PORT(x)		(0x72+(x))
#define RTC_ALWAYS_BCD		1

#include <linux/config.h>
#include <asm/leds.h>
#include <asm/system.h>
#include <linux/mc146818rtc.h>

extern __inline__ unsigned long gettimeoffset (void)
{
	unsigned long value = LATCH - *CSR_TIMER1_VALUE;

	return (tick * value) / LATCH;
}

extern __inline__ int reset_timer (void)
{
	*CSR_TIMER1_CLR = 0;

#ifdef CONFIG_LEDS
	/*
	 * Do the LEDs thing on EBSA-285 hardware.
	 */
	if (!machine_is_cats()) {
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
	}
#endif
	
	return 1;
}

/*
 * We don't have a RTC to update!
 */
#define update_rtc()

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
extern __inline__ unsigned long setup_timer (void)
{
	int year, mon, day, hour, min, sec;

	/*
	 * Default the date to 1 Jan 1970 0:0:0
	 */
	year = 1970; mon = 1; day = 1;
	hour = 0; min = 0; sec = 0;

	*CSR_TIMER1_CLR  = 0;
	*CSR_TIMER1_LOAD = LATCH;
	*CSR_TIMER1_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_AUTORELOAD | TIMER_CNTL_DIV16;

	if (machine_is_cats()) 
	{
		int i;
		/*
		 * Read the real time from the Dallas chip.  (Code borrowed
		 * from arch/i386/kernel/time.c).
		 */
		
		/* The Linux interpretation of the CMOS clock register contents:
		 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
		 * RTC registers show the second which has precisely just started.
		 * Let's hope other operating systems interpret the RTC the same way.
		 */

		/* read RTC exactly on falling edge of update flag */
		for (i = 0 ; i < 1000000 ; i++)	/* may take up to 1 second... */
			if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
				break;
		for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms */
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
		if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		{
			BCD_TO_BIN(sec);
			BCD_TO_BIN(min);
			BCD_TO_BIN(hour);
			BCD_TO_BIN(day);
			BCD_TO_BIN(mon);
			BCD_TO_BIN(year);
		}
		if ((year += 1900) < 1970)
			year += 100;
	}

	return mktime(year, mon, day, hour, min, sec);
}
