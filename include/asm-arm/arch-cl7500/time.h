/*
 * linux/include/asm-arm/arch-cl7500/time.h
 *
 * Copyright (c) 1996 Russell King.
 * Copyright (C) 1999 Nexus Electronics Ltd.
 *
 * Changelog:
 *  24-Sep-1996	RMK	Created
 *  10-Oct-1996	RMK	Brought up to date with arch-sa110eval
 *  04-Dec-1997	RMK	Updated for new arch/arm/time.c
 *  10-Aug-1999	PJB	Converted for CL7500
 */
#include <asm/iomd.h>

static long last_rtc_update = 0;	/* last time the cmos clock got updated */

extern __inline__ unsigned long gettimeoffset (void)
{
	unsigned long offset = 0;
	unsigned int count1, count2, status1, status2;

	status1 = IOMD_IRQREQA;
	barrier ();
	outb(0, IOMD_T0LATCH);
	barrier ();
	count1 = inb(IOMD_T0CNTL) | (inb(IOMD_T0CNTH) << 8);
	barrier ();
	status2 = inb(IOMD_IRQREQA);
	barrier ();
	outb(0, IOMD_T0LATCH);
	barrier ();
	count2 = inb(IOMD_T0CNTL) | (inb(IOMD_T0CNTH) << 8);

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

extern __inline__ unsigned long get_rtc_time(void)
{
	return mktime(1976, 06, 24, 0, 0, 0);
}

static int set_rtc_time(unsigned long nowtime)
{
	return 0;
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

	{
		/* Twinkle the lights. */
		static int count, bit = 8, dir = 1;
		if (count-- == 0) {
			bit += dir;
			if (bit == 8 || bit == 15) dir = -dir;
			count = 5;
			*((volatile unsigned int *)(0xe002ba00)) = 1 << bit;
		}
	}

	if (!user_mode(regs))
		do_profile(instruction_pointer(regs));
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
	outb(LATCH & 255, IOMD_T0LTCHL);
	outb(LATCH >> 8, IOMD_T0LTCHH);
	outb(0, IOMD_T0GO);

	xtime.tv_sec = get_rtc_time();

	setup_arm_irq(IRQ_TIMER, &timerirq);
}
