/*
 * linux/include/asm-arm/arch-ebsa110/time.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 *
 * No real time clock on the evalulation board!
 *
 * Changelog:
 *  10-Oct-1996	RMK	Created
 *  04-Dec-1997	RMK	Updated for new arch/arm/time.c
 */

#define MCLK_47_8

#if defined(MCLK_42_3)
#define PIT1_COUNT 0xecbe
#elif defined(MCLK_47_8)
/*
 * This should be 0x10AE1, but that doesn't exactly fit.
 * We run the timer interrupt at 5ms, and then divide it by
 * two in software...  This is so that the user processes
 * see exactly the same model whichever ARM processor they're
 * running on.
 */
#define PIT1_COUNT 0x8570
#define DIVISOR 2
#endif
 
extern __inline__ unsigned long gettimeoffset (void)
{
	return 0;
}

#ifndef DIVISOR
extern __inline__ int reset_timer (void)
{
	*PIT_T1 = (PIT1_COUNT) & 0xff;
	*PIT_T1 = (PIT1_COUNT) >> 8;
	return 1;
}
#else
extern __inline__ int reset_timer (void)
{
	static unsigned int divisor;
	static int count = 50;

	*PIT_T1 = (PIT1_COUNT) & 0xff;
	*PIT_T1 = (PIT1_COUNT) >> 8;

	if (--count == 0) {
		count = 50;
		*(volatile unsigned char *)0xf2400000 ^= 128;
	}

	if (divisor == 0) {
		divisor = DIVISOR - 1;
		return 1;
	}
	divisor -= 1;
	return 0;
}
#endif

/*
 * We don't have a RTC to update!
 */
#define update_rtc()

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
extern __inline__ unsigned long setup_timer (void)
{
	/*
	 * Timer 1, mode 0, 16-bit, autoreload
	 */
	*PIT_CTRL = 0x70;
	/*
	 * Refresh counter clocked at 47.8MHz/7 = 146.4ns
	 * We want centi-second interrupts
	 */
	reset_timer ();
	/*
	 * Default the date to 1 Jan 1970 0:0:0
	 * You will have to run a time daemon to set the
	 * clock correctly at bootup
	 */
	return mktime(1970, 1, 1, 0, 0, 0);
}
