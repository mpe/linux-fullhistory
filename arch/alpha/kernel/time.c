/*
 *  linux/arch/alpha/kernel/time.c
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
 * 1997-01-09    Adrian Sun
 *      use interval timer if CONFIG_RTC=y
 * 1997-10-29    John Bowman (bowman@math.ualberta.ca)
 *      fixed tick loss calculation in timer_interrupt
 *      (round system clock to nearest tick instead of truncating)
 *      fixed algorithm in time_init for getting time from CMOS clock
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/ioport.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/hwrpb.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

#include "proto.h"

#ifdef CONFIG_RTC 
#define TIMER_IRQ 0  /* using pit for timer */
#else 
#define TIMER_IRQ 8  /* using rtc for timer */
#endif

static int set_rtc_mmss(unsigned long);


/*
 * Shift amount by which scaled_ticks_per_cycle is scaled.  Shifting
 * by 48 gives us 16 bits for HZ while keeping the accuracy good even
 * for large CPU clock rates.
 */
#define FIX_SHIFT	48

/* lump static variables together for more efficient access: */
static struct {
	/* cycle counter last time it got invoked */
	__u32 last_time;
	/* ticks/cycle * 2^48 */
	unsigned long scaled_ticks_per_cycle;
	/* last time the CMOS clock got updated */
	time_t last_rtc_update;
} state;

unsigned long est_cycle_freq;


static inline __u32 rpcc(void)
{
    __u32 result;
    asm volatile ("rpcc %0" : "=r"(result));
    return result;
}


/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
void timer_interrupt(int irq, void *dev, struct pt_regs * regs)
{
	const unsigned long half = 1UL << (FIX_SHIFT - 1);
	const unsigned long mask = (1UL << (FIX_SHIFT + 1)) - 1;
	unsigned long delta;
	__u32 now;
	long nticks;

#ifdef __SMP__
	extern void smp_percpu_timer_interrupt(struct pt_regs *);
	extern unsigned int boot_cpu_id;
	/* when SMP, do this for *all* CPUs, 
	   but only do the rest for the boot CPU */
	smp_percpu_timer_interrupt(regs);
	if (smp_processor_id() != boot_cpu_id)
	  return;
#endif

	/*
	 * Estimate how many ticks have passed since the last update.
	 * Round the result, .5 to even.  When we loose ticks due to
	 * say using IDE, the clock has been seen to run up to 15% slow
	 * if we truncate.
	 */
	now = rpcc();
	delta = now - state.last_time;
	state.last_time = now;
	delta = delta * state.scaled_ticks_per_cycle;
	if ((delta & mask) != half)
		delta += half;
	nticks = delta >> FIX_SHIFT;

	do {
		do_timer(regs);
	} while (--nticks > 0);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (time_state != TIME_BAD
	    && xtime.tv_sec > state.last_rtc_update + 660
	    && xtime.tv_usec >= 500000 - (tick >> 1)
	    && xtime.tv_usec <= 500000 + (tick >> 1)) {
		int tmp = set_rtc_mmss(xtime.tv_sec);
		state.last_rtc_update = xtime.tv_sec - (tmp ? 600 : 0);
	}
}

/*
 * Converts Gregorian date to seconds since 1970-01-01 00:00:00.
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

/*
 * Initialize Programmable Interval Timers with standard values.  Some
 * drivers depend on them being initialized (e.g., joystick driver).
 */

/* It is (normally) only counter 0 that presents config problems, so
   provide this support function to do the rest of the job.  */

void inline
init_pit_rest(void)
{
#if 0
	/* Leave refresh timer alone---nobody should depend on a
	   particular value anyway. */
	outb(0x54, 0x43);	/* counter 1: refresh timer */
	outb(0x18, 0x41);
#endif

	outb(0xb6, 0x43);	/* counter 2: speaker */
	outb(0x31, 0x42);
	outb(0x13, 0x42);
}

#ifdef CONFIG_RTC
static inline void
rtc_init_pit (void)
{
	unsigned char control;

	/* Setup interval timer if /dev/rtc is being used */
	outb(0x34, 0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb(LATCH & 0xff, 0x40);	/* LSB */
	outb(LATCH >> 8, 0x40);		/* MSB */
	request_region(0x40, 0x20, "timer"); /* reserve pit */

	/* Turn off RTC interrupts before /dev/rtc is initialized */
	control = CMOS_READ(RTC_CONTROL);
        control &= ~(RTC_PIE | RTC_AIE | RTC_UIE);
        CMOS_WRITE(control, RTC_CONTROL);
        CMOS_READ(RTC_INTR_FLAGS);

	init_pit_rest();
}
#endif

void
generic_init_pit (void)
{
	unsigned char x;

        /* Reset periodic interrupt frequency.  */
	x = CMOS_READ(RTC_FREQ_SELECT) & 0x3f;
	if (x != 0x26 && x != 0x19 && x != 0x06) {
		printk("Setting RTC_FREQ to 1024 Hz (%x)\n", x);
		CMOS_WRITE(0x26, RTC_FREQ_SELECT);
	}

	/* Turn on periodic interrupts.  */
	x = CMOS_READ(RTC_CONTROL);
	if (!(x & RTC_PIE)) {
		printk("Turning on RTC interrupts.\n");
		x |= RTC_PIE;
		x &= ~(RTC_AIE | RTC_UIE);
		CMOS_WRITE(x, RTC_CONTROL);
	}
	CMOS_READ(RTC_INTR_FLAGS);

	request_region(RTC_PORT(0), 0x10, "timer"); /* reserve rtc */

	/* Turn off the PIT.  */
	outb(0x36, 0x43);	/* counter 0: system timer */
	outb(0x00, 0x40);
	outb(0x00, 0x40);

	init_pit_rest();
}

/* This probably isn't Right, but it is what the old code did.  */
#if defined(CONFIG_RTC)
# define init_pit	rtc_init_pit
#else
# define init_pit	alpha_mv.init_pit
#endif


void
time_init(void)
{
        void (*irq_handler)(int, void *, struct pt_regs *);
	unsigned int year, mon, day, hour, min, sec, cc1, cc2;
	unsigned long cycle_freq;

	/* Initialize the timers.  */
	init_pit();

	/*
	 * The Linux interpretation of the CMOS clock register contents:
	 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
	 * RTC registers show the second which has precisely just started.
	 * Let's hope other operating systems interpret the RTC the same way.
	 */
	do { } while (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP));
	do { } while (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP);

	/* Read cycle counter exactly on falling edge of update flag */
	cc1 = rpcc();

	/* If our cycle frequency isn't valid, go another round and give
	   a guess at what it should be.  */
	cycle_freq = hwrpb->cycle_freq;
	if (cycle_freq == 0) {
		printk("HWRPB cycle frequency bogus.  Estimating... ");

		do { } while (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP));
		do { } while (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP);
		cc2 = rpcc();
		est_cycle_freq = cycle_freq = cc2 - cc1;
		cc1 = cc2;

		printk("%lu Hz\n", cycle_freq);
	}

	/* From John Bowman <bowman@math.ualberta.ca>: allow the values
	   to settle, as the Update-In-Progress bit going low isn't good
	   enough on some hardware.  2ms is our guess; we havn't found 
	   bogomips yet, but this is close on a 500Mhz box.  */
	__delay(1000000);

	sec = CMOS_READ(RTC_SECONDS);
	min = CMOS_READ(RTC_MINUTES);
	hour = CMOS_READ(RTC_HOURS);
	day = CMOS_READ(RTC_DAY_OF_MONTH);
	mon = CMOS_READ(RTC_MONTH);
	year = CMOS_READ(RTC_YEAR);

	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);
	}
#ifdef ALPHA_PRE_V1_2_SRM_CONSOLE
	/*
	 * The meaning of life, the universe, and everything. Plus
	 * this makes the year come out right on SRM consoles earlier
	 * than v1.2.
	 */
	year -= 42;
#endif
	if ((year += 1900) < 1970)
		year += 100;
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;

	if (HZ > (1<<16)) {
		extern void __you_loose (void);
		__you_loose();
	}

	state.last_time = cc1;
	state.scaled_ticks_per_cycle
		= ((unsigned long) HZ << FIX_SHIFT) / cycle_freq;
	state.last_rtc_update = 0;

	/* setup timer */ 
        irq_handler = timer_interrupt;
        if (request_irq(TIMER_IRQ, irq_handler, 0, "timer", NULL))
		panic("Could not allocate timer IRQ!");
}

/*
 * Use the cycle counter to estimate an displacement from the last time
 * tick.  Unfortunately the Alpha designers made only the low 32-bits of
 * the cycle counter active, so we overflow on 8.2 seconds on a 500MHz
 * part.  So we can't do the "find absolute time in terms of cycles" thing
 * that the other ports do.
 */
void
do_gettimeofday(struct timeval *tv)
{
	unsigned long flags, now, delta_cycles, delta_usec;
	unsigned long sec, usec;

	now = rpcc();
	save_and_cli(flags);
	sec = xtime.tv_sec;
	usec = xtime.tv_usec;
	delta_cycles = now - state.last_time;
	restore_flags(flags);

	/*
	 * usec = cycles * ticks_per_cycle * 2**48 * 1e6 / (2**48 * ticks)
	 *	= cycles * (s_t_p_c) * 1e6 / (2**48 * ticks)
	 *	= cycles * (s_t_p_c) * 15625 / (2**42 * ticks)
	 *
	 * which, given a 600MHz cycle and a 1024Hz tick, has a
	 * dynamic range of about 1.7e17, which is less than the
	 * 1.8e19 in an unsigned long, so we are safe from overflow.
	 *
	 * Round, but with .5 up always, since .5 to even is harder
	 * with no clear gain.
	 */

	delta_usec = delta_cycles * state.scaled_ticks_per_cycle * 15625;
	delta_usec = ((delta_usec / ((1UL << (FIX_SHIFT-6)) * HZ)) + 1) / 2;

	usec += delta_usec;
	if (usec >= 1000000) {
		sec += 1;
		usec -= 1000000;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void
do_settimeofday(struct timeval *tv)
{
	cli();
	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	sti();
}


/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 */
static int
set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

	/* Tell the clock it's being set */
	save_control = CMOS_READ(RTC_CONTROL);
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	/* Stop and reset prescaler */
	save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
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
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1) {
		/* correct for half hour time zone */
		real_minutes += 30;
	}
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
