/*
 * linux/include/asm-arm/arch-arc/time.h
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  24-Sep-1996	RMK	Created
 *  10-Oct-1996	RMK	Brought up to date with arch-sa110eval
 *  04-Dec-1997	RMK	Updated for new arch/arm/time.c
 */
#include <asm/ioc.h>

static long last_rtc_update = 0;	/* last time the cmos clock got updated */

extern __inline__ unsigned long gettimeoffset (void)
{
	unsigned int count1, count2, status1, status2;
	unsigned long offset = 0;

	status1 = inb(IOC_IRQREQA);
	barrier ();
	outb (0, IOC_T0LATCH);
	barrier ();
	count1 = inb(IOC_T0CNTL) | (inb(IOC_T0CNTH) << 8);
	barrier ();
	status2 = inb(IOC_IRQREQA);
	barrier ();
	outb (0, IOC_T0LATCH);
	barrier ();
	count2 = inb(IOC_T0CNTL) | (inb(IOC_T0CNTH) << 8);

	if (count2 < count1) {
		/*
		 * This means that we haven't just had an interrupt
		 * while reading into status2.
		 */
		if (status2 & (1 << 5))
			offset = tick;
		count1 = count2;
	} else if (count2 > count1) {
		/*
		 * We have just had another interrupt while reading
		 * status2.
		 */
		offset += tick;
		count1 = count2;
	}

	count1 = LATCH - count1;
	/*
	 * count1 = number of clock ticks since last interrupt
	 */
	offset += count1 * tick / LATCH;
	return offset;
}

extern int iic_control (unsigned char, int, char *, int);

static int set_rtc_time(unsigned long nowtime)
{
	char buf[5], ctrl;

	if (iic_control(0xa1, 0, &ctrl, 1) != 0)
		printk("RTC: failed to read control reg\n");

	/*
	 * Reset divider
	 */
	ctrl |= 0x80;

	if (iic_control(0xa0, 0, &ctrl, 1) != 0)
		printk("RTC: failed to stop the clock\n");

	/*
	 * We only set the time - we don't set the date.
	 * This means that there is the possibility once
	 * a day for the correction to disrupt the date.
	 * We really ought to write the time and date, or
	 * nothing at all.
	 */
	buf[0] = 0;
	buf[1] = nowtime % 60;		nowtime /= 60;
	buf[2] = nowtime % 60;		nowtime /= 60;
	buf[3] = nowtime % 24;

	BIN_TO_BCD(buf[1]);
	BIN_TO_BCD(buf[2]);
	BIN_TO_BCD(buf[3]);

	if (iic_control(0xa0, 1, buf, 4) != 0)
		printk("RTC: Failed to set the time\n");

	/*
	 * Re-enable divider
	 */
	ctrl &= ~0x80;

	if (iic_control(0xa0, 0, &ctrl, 1) != 0)
		printk("RTC: failed to start the clock\n");

	return 0;
}

extern __inline__ unsigned long get_rtc_time(void)
{
	unsigned int year, i;
	char buf[8];

	/*
	 * The year is not part of the RTC counter
	 * registers, and is stored in RAM.  This
	 * means that it will not be automatically
	 * updated.
	 */
	if (iic_control(0xa1, 0xc0, buf, 1) != 0)
		printk("RTC: failed to read the year\n");

	/*
	 * If the year is before 1970, then the year
	 * is actually 100 in advance.  This gives us
	 * a year 2070 bug...
	 */
	year = 1900 + buf[0];
	if (year < 1970)
		year += 100;

	/*
	 * Read the time and date in one go - this
	 * will ensure that we don't get any effects
	 * due to carry (the RTC latches the counters
	 * during a read).
	 */
	if (iic_control(0xa1, 2, buf, 5) != 0) {
		printk("RTC: failed to read the time and date\n");
		memset(buf, 0, sizeof(buf));
	}

	/*
	 * The RTC combines years with date and weekday
	 * with month.  We need to mask off this extra
	 * information before converting the date to
	 * binary.
	 */
	buf[4] &= 0x1f;
	buf[3] &= 0x3f;

	for (i = 0; i < 5; i++)
		BCD_TO_BIN(buf[i]);

	return mktime(year, buf[4], buf[3], buf[2], buf[1], buf[0]);
}

static void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);

	/* If we have an externally synchronized linux clock, then update
	 * CMOS clock accordingly every ~11 minutes.  Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec >= 50000 - (tick >> 1) &&
	    xtime.tv_usec < 50000 + (tick >> 1)) {
		if (set_rtc_time(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
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
	outb(LATCH & 255, IOC_T0LTCHL);
	outb(LATCH >> 8, IOC_T0LTCHH);
	outb(0, IOC_T0GO);

	xtime.tv_sec = get_rtc_time();

	setup_arm_irq(IRQ_TIMER, &timerirq);
}
