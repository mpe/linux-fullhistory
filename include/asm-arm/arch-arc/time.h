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

/*
 * No need to reset the timer at every irq
 */
#define reset_timer() 1

/*
 * Updating of the RTC.  We don't currently write the time to the
 * CMOS clock.
 */
#define update_rtc()

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
extern __inline__ unsigned long setup_timer (void)
{
	extern int iic_control (unsigned char, int, char *, int);
	unsigned int year, mon, day, hour, min, sec;
	char buf[8];

	outb(LATCH & 255, IOC_T0LTCHL);
	outb(LATCH >> 8, IOC_T0LTCHH);
	outb(0, IOC_T0GO);

	iic_control (0xa0, 0xc0, buf, 1);
	year = buf[0];
	if ((year += 1900) < 1970)
		year += 100;

	iic_control (0xa0, 2, buf, 5);
	mon  = buf[4] & 0x1f;
	day  = buf[3] & 0x3f;
	hour = buf[2];
	min  = buf[1];
	sec  = buf[0];
	BCD_TO_BIN(mon);
	BCD_TO_BIN(day);
	BCD_TO_BIN(hour);
	BCD_TO_BIN(min);
	BCD_TO_BIN(sec);

	return mktime(year, mon, day, hour, min, sec);
}
