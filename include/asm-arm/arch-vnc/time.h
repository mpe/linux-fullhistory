/*
 * linux/include/asm-arm/arch-vnc/time.h
 *
 * Copyright (c) 1997 Corel Computer Corp.
 * Slight modifications to bring in line with ebsa285 port.
 *  -- Russell King.
 */

extern __inline__ unsigned long gettimeoffset (void)
{
	return 0;
}

extern __inline__ int reset_timer (void)
{
	*CSR_TIMER1_CLR = 0;
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
	*CSR_TIMER1_CLR  = 1;
	*CSR_TIMER1_LOAD = LATCH;
	*CSR_TIMER1_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_AUTORELOAD | TIMER_CNTL_DIV16;

	/*
	 * Default the date to 1 Jan 1970 00:00:00
	 * You will have to run a time daemon to set the
	 * clock correctly at bootup
	 */
	return mktime(1970, 1, 1, 0, 0, 0);
}
