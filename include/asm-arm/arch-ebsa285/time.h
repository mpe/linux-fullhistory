/*
 * linux/include/asm-arm/arch-ebsa285/time.h
 *
 * Copyright (c) 1998 Russell King.
 *
 * No real time clock on the evalulation board!
 *
 * Changelog:
 *  21-Mar-1998	RMK	Created
 */

#include <asm/leds.h>

extern __inline__ unsigned long gettimeoffset (void)
{
	return 0;
}

extern __inline__ int reset_timer (void)
{
	static unsigned int count = 50;
	static int last_pid;

	*CSR_TIMER1_CLR = 0;

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
	*CSR_TIMER1_CLR  = 0;
	*CSR_TIMER1_LOAD = LATCH;
	*CSR_TIMER1_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_AUTORELOAD | TIMER_CNTL_DIV16;

	return mktime(1970, 1, 1, 0, 0, 0);
}
