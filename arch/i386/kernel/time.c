/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the PC-specific time handling details:
 * reading the RTC at bootup, etc..
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>
#include <linux/config.h>

extern int setup_x86_irq(int, struct irqaction *);

#ifndef	CONFIG_APM	/* cycle counter may be unreliable */
/* Cycle counter value at the previous timer interrupt.. */
static struct {
	unsigned long low;
	unsigned long high;
} init_timer_cc, last_timer_cc;

/*
 * This is more assembly than C, but it's also rather
 * timing-critical and we have to use assembler to get
 * reasonable 64-bit arithmetic
 */
static unsigned long do_fast_gettimeoffset(void)
{
	register unsigned long eax asm("ax");
	register unsigned long edx asm("dx");
	unsigned long tmp, quotient, low_timer, missing_time;

	/* Last jiffy when do_fast_gettimeoffset() was called.. */
	static unsigned long last_jiffies=0;

	/* Cached "clocks per usec" value.. */
	static unsigned long cached_quotient=0;

	/* The "clocks per usec" value is calculated once each jiffy */
	tmp = jiffies;
	quotient = cached_quotient;
	low_timer = last_timer_cc.low;
	missing_time = 0;
	if (last_jiffies != tmp) {
		last_jiffies = tmp;
		/*
		 * test for hanging bottom handler (this means xtime is not 
		 * updated yet)
		 */
		if (test_bit(TIMER_BH, &bh_active) )
		{
			missing_time = 997670/HZ;
		}

		/* Get last timer tick in absolute kernel time */
		eax = low_timer;
		edx = last_timer_cc.high;
		__asm__("subl "SYMBOL_NAME_STR(init_timer_cc)",%0\n\t"
			"sbbl "SYMBOL_NAME_STR(init_timer_cc)"+4,%1"
			:"=a" (eax), "=d" (edx)
			:"0" (eax), "1" (edx));

		/*
		 * Divide the 64-bit time with the 32-bit jiffy counter,
		 * getting the quotient in clocks.
		 *
		 * Giving quotient = "average internal clocks per usec"
		 */
		__asm__("divl %2"
			:"=a" (eax), "=d" (edx)
			:"r" (tmp),
			 "0" (eax), "1" (edx));

		edx = 997670/HZ;
		tmp = eax;
		eax = 0;

		__asm__("divl %2"
			:"=a" (eax), "=d" (edx)
			:"r" (tmp),
			 "0" (eax), "1" (edx));
		cached_quotient = eax;
		quotient = eax;
	}

	/* Read the time counter */
	__asm__(".byte 0x0f,0x31"
		:"=a" (eax), "=d" (edx));

	/* .. relative to previous jiffy (32 bits is enough) */
	edx = 0;
	eax -= low_timer;

	/*
	 * Time offset = (997670/HZ * time_low) / quotient.
	 */

	__asm__("mull %2"
		:"=a" (eax), "=d" (edx)
		:"r" (quotient),
		 "0" (eax), "1" (edx));

	/*
 	 * Due to rounding errors (and jiffies inconsistencies),
	 * we need to check the result so that we'll get a timer
	 * that is monotonic.
	 */
	if (edx >= 997670/HZ)
		edx = 997670/HZ-1;

	eax = edx + missing_time;
	return eax;
}
#endif

/* This function must be called with interrupts disabled 
 * It was inspired by Steve McCanne's microtime-i386 for BSD.  -- jrs
 * 
 * However, the pc-audio speaker driver changes the divisor so that
 * it gets interrupted rather more often - it loads 64 into the
 * counter rather than 11932! This has an adverse impact on
 * do_gettimeoffset() -- it stops working! What is also not
 * good is that the interval that our timer function gets called
 * is no longer 10.0002 ms, but 9.9767 ms. To get around this
 * would require using a different timing source. Maybe someone
 * could use the RTC - I know that this can interrupt at frequencies
 * ranging from 8192Hz to 2Hz. If I had the energy, I'd somehow fix
 * it so that at startup, the timer code in sched.c would select
 * using either the RTC or the 8253 timer. The decision would be
 * based on whether there was any other device around that needed
 * to trample on the 8253. I'd set up the RTC to interrupt at 1024 Hz,
 * and then do some jiggery to have a version of do_timer that 
 * advanced the clock by 1/1024 s. Every time that reached over 1/100
 * of a second, then do all the old code. If the time was kept correct
 * then do_gettimeoffset could just return 0 - there is no low order
 * divider that can be accessed.
 *
 * Ideally, you would be able to use the RTC for the speaker driver,
 * but it appears that the speaker driver really needs interrupt more
 * often than every 120 us or so.
 *
 * Anyway, this needs more thought....		pjsg (1993-08-28)
 * 
 * If you are really that interested, you should be reading
 * comp.protocols.time.ntp!
 */

#define TICK_SIZE tick

static unsigned long do_slow_gettimeoffset(void)
{
	int count;
	static int count_p = 0;
	unsigned long offset = 0;
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off. 
	 */
	unsigned long jiffies_t;

	/* timer count may underflow right here */
	outb_p(0x00, 0x43);	/* latch the count ASAP */
	count = inb_p(0x40);	/* read the latched count */
	count |= inb(0x40) << 8;

 	jiffies_t = jiffies;

	/*
	 * avoiding timer inconsistencies (they are rare, but they happen)...
	 * there are three kinds of problems that must be avoided here:
	 *  1. the timer counter underflows
	 *  2. hardware problem with the timer, not giving us continuous time,
	 *     the counter does small "jumps" upwards on some Pentium systems,
	 *     thus causes time warps
	 *  3. we are after the timer interrupt, but the bottom half handler
	 *     hasn't executed yet.
	 */
	if( count > count_p ) {
		if( jiffies_t == jiffies_p ) {
			if( count > LATCH-LATCH/100 )
				offset = TICK_SIZE;
			else
				/*
				 * argh, the timer is bugging we cant do nothing 
				 * but to give the previous clock value.
				 */
				count = count_p;
		} else {
			if( test_bit(TIMER_BH, &bh_active) ) {
				/*
				 * we have detected a counter underflow.
			 	 */
				offset = TICK_SIZE;
				count_p = count;		
			} else {
				count_p = count;
				jiffies_p = jiffies_t;
			}
		}
	} else {
		count_p = count;
		jiffies_p = jiffies_t;
 	}


	count = ((LATCH-1) - count) * TICK_SIZE;
	count = (count + LATCH/2) / LATCH;

	return offset + count;
}

static unsigned long (*do_gettimeoffset)(void) = do_slow_gettimeoffset;

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	*tv = xtime;
	tv->tv_usec += do_gettimeoffset();
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
	restore_flags(flags);
}

void do_settimeofday(struct timeval *tv)
{
	cli();
	/* This is revolting. We need to set the xtime.tv_usec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
	tv->tv_usec -= do_gettimeoffset();

	if (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = MAXPHASE;
	time_esterror = MAXPHASE;
	sti();
}


/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 */
static int set_rtc_mmss(unsigned long nowtime)
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

/* last time the cmos clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	/* As we return to user mode fire off the other CPU schedulers.. this is 
	   basically because we don't yet share IRQ's around. This message is
	   rigged to be safe on the 386 - basically it's a hack, so don't look
	   closely for now.. */
	/*smp_message_pass(MSG_ALL_BUT_SELF, MSG_RESCHEDULE, 0L, 0); */
	    
}

#ifndef	CONFIG_APM	/* cycle counter may be unreliable */
/*
 * This is the same as the above, except we _also_ save the current
 * cycle counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
static void pentium_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/* read Pentium cycle counter */
	__asm__(".byte 0x0f,0x31"
		:"=a" (last_timer_cc.low),
		 "=d" (last_timer_cc.high));
	timer_interrupt(irq, NULL, regs);
}
#endif

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static inline unsigned long mktime(unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	return (((
	    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
	      year*365 - 719499
	    )*24 + hour /* now have hours */
	   )*60 + min /* now have minutes */
	  )*60 + sec; /* finally seconds */
}

unsigned long get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	int i;

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
	return mktime(year, mon, day, hour, min, sec);
}

static struct irqaction irq0  = { timer_interrupt, 0, 0, "timer", NULL, NULL};

void time_init(void)
{
	xtime.tv_sec = get_cmos_time();
	xtime.tv_usec = 0;

	/* If we have the CPU hardware time counters, use them */
#ifndef CONFIG_APM
				/* Don't use them if a suspend/resume could
                                   corrupt the timer value.  This problem
                                   needs more debugging. */
	if (x86_capability & 16) {
		do_gettimeoffset = do_fast_gettimeoffset;
		/* read Pentium cycle counter */
		__asm__(".byte 0x0f,0x31"
			:"=a" (init_timer_cc.low),
			 "=d" (init_timer_cc.high));
		irq0.handler = pentium_timer_interrupt;
	}
#endif
	setup_x86_irq(0, &irq0);
}
