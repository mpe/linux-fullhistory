/*
 * linux/include/asm-arm/arch-ebsa110/time.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 *
 * No real time clock on the evalulation board!
 *
 * Changelog:
 *  10-Oct-1996	RMK	Created
 *  04-Dec-1997	RMK	Updated for new arch/arm/kernel/time.c
 *  07-Aug-1998	RMK	Updated for arch/arm/kernel/leds.c
 *  28-Dec-1998	APH	Made leds code optional
 */

#include <linux/config.h>
#include <asm/leds.h>

#define IRQ_TIMER IRQ_EBSA110_TIMER0

#define MCLK_47_8

#if defined(MCLK_42_3)
#define PIT1_COUNT 0xecbe
#elif defined(MCLK_47_8)
/*
 * This should be 0x10B43, but that doesn't exactly fit.
 * We run the timer interrupt at 5ms, and then divide it by
 * two in software...  This is so that the user processes
 * see exactly the same model whichever ARM processor they're
 * running on.
 */
#define PIT1_COUNT 0x85A1
#define DIVISOR 2
#endif
 
extern __inline__ unsigned long gettimeoffset (void)
{
	return 0;
}

static void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	*PIT_T1 = (PIT1_COUNT) & 0xff;
	*PIT_T1 = (PIT1_COUNT) >> 8;

#ifdef CONFIG_LEDS
	{
		static int count = 50;
		if (--count == 0) {
			count = 50;
			leds_event(led_timer);
		}
	}
#endif

	{
#ifdef DIVISOR
		static unsigned int divisor;

		if (divisor-- == 0) {
			divisor = DIVISOR - 1;
#else
		{
#endif
			do_timer(regs);
		}
	}
}

static struct irqaction timerirq = {
	timer_interrupt,
	0,
	0,
	"timer",
	NULL,
	NULL
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
extern __inline__ void setup_timer(void)
{
	/*
	 * Timer 1, mode 0, 16-bit, autoreload
	 */
	*PIT_CTRL = 0x70;

	/*
	 * Refresh counter clocked at 47.8MHz/7 = 146.4ns
	 * We want centi-second interrupts
	 */
	*PIT_T1 = (PIT1_COUNT) & 0xff;
	*PIT_T1 = (PIT1_COUNT) >> 8;

	/*
	 * Default the date to 1 Jan 1970 0:0:0
	 * You will have to run a time daemon to set the
	 * clock correctly at bootup
	 */
	xtime.tv_sec = mktime(1970, 1, 1, 0, 0, 0);

	setup_arm_irq(IRQ_TIMER, &timerirq);
}
