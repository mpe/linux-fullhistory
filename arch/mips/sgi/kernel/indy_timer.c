/* $Id: indy_timer.c,v 1.9 1998/06/25 20:15:02 ralf Exp $
 *
 * indy_timer.c: Setting up the clock on the INDY 8254 controller.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copytight (C) 1997, 1998 Ralf Baechle (ralf@gnu.org)
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/sgi.h>
#include <asm/sgialib.h>
#include <asm/sgihpc.h>
#include <asm/sgint23.h>


/* Because of a bug in the i8254 timer we need to use the onchip r4k
 * counter as our system wide timer interrupt running at 100HZ.
 */
static unsigned long r4k_offset; /* Amount to increment compare reg each time */
static unsigned long r4k_cur;    /* What counter should be at next timer irq */

static inline void ack_r4ktimer(unsigned long newval)
{
	write_32bit_cp0_register(CP0_COMPARE, newval);
}

static int set_rtc_mmss(unsigned long nowtime)
{
	struct indy_clock *clock = (struct indy_clock *)INDY_CLOCK_REGS;
	int retval = 0;
	int real_seconds, real_minutes, clock_minutes;

#define FROB_FROM_CLOCK(x) (((x) & 0xf) | ((((x) & 0xf0) >> 4) * 10));
#define FROB_TO_CLOCK(x)  ((((((x) & 0xff) / 10)<<4) | (((x) & 0xff) % 10)) & 0xff)

	clock->cmd &= ~(0x80);
	clock_minutes = clock->min;
	clock->cmd |= (0x80);

	clock_minutes = FROB_FROM_CLOCK(clock_minutes);
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;

	if(((abs(real_minutes - clock_minutes) + 15)/30) & 1)
		real_minutes += 30; /* correct for half hour time zone */

	real_minutes %= 60;
	if(abs(real_minutes - clock_minutes) < 30) {
		/* Force clock oscillator to be on. */
		clock->month &= ~(0x80);

		/* Write real_seconds and real_minutes into the Dallas. */
		clock->cmd &= ~(0x80);
		clock->sec = real_seconds;
		clock->min = real_minutes;
		clock->cmd |= (0x80);
	} else
		return -1;

#undef FROB_FROM_CLOCK
#undef FROB_TO_CLOCK

	return retval;
}

static long last_rtc_update = 0;
unsigned long missed_heart_beats = 0;

void indy_timer_interrupt(struct pt_regs *regs)
{
	unsigned long count;
	int irq = 7;

	/* Ack timer and compute new compare. */
	count = read_32bit_cp0_register(CP0_COUNT);
	/* This has races.  */
	if ((count - r4k_cur) >= r4k_offset) {
		/* If this happens to often we'll need to compensate.  */
		missed_heart_beats++;
		r4k_cur = count + r4k_offset;
	}
        else
            r4k_cur += r4k_offset;
	ack_r4ktimer(r4k_cur);
	kstat.irqs[0][irq]++;
	do_timer(regs);

	/* We update the Dallas time of day approx. every 11 minutes,
	 * because of how the numbers work out we need to make
	 * absolutely sure we do this update within 500ms before the
	 * next second starts, thus the following code.
	 */
	if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
}

static unsigned long dosample(volatile unsigned char *tcwp,
                              volatile unsigned char *tc2p)
{
	unsigned long ct0, ct1;
	unsigned char msb, lsb;

	/* Start the counter. */
	*tcwp = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL | SGINT_TCWORD_MRGEN);
	*tc2p = (SGINT_TCSAMP_COUNTER & 0xff);
	*tc2p = (SGINT_TCSAMP_COUNTER >> 8);

	/* Get initial counter invariant */
	ct0 = read_32bit_cp0_register(CP0_COUNT);

	/* Latch and spin until top byte of counter2 is zero */
	do {
		*tcwp = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CLAT);
		lsb = *tc2p;
		msb = *tc2p;
		ct1 = read_32bit_cp0_register(CP0_COUNT);
	} while(msb);

	/* Stop the counter. */
	*tcwp = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL | SGINT_TCWORD_MSWST);

	/* Return the difference, this is how far the r4k counter increments
	 * for every one HZ.
	 */
	return ct1 - ct0;
}

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

__initfunc(static unsigned long get_indy_time(void))
{
	struct indy_clock *clock = (struct indy_clock *)INDY_CLOCK_REGS;
	unsigned int year, mon, day, hour, min, sec;

	/* Freeze it. */
	clock->cmd &= ~(0x80);

	/* Read regs. */
	sec = clock->sec;
	min = clock->min;
	hour = (clock->hr & 0x3f);
	day = (clock->date & 0x3f);
	mon = (clock->month & 0x1f);
	year = clock->year;

	/* Unfreeze clock. */
	clock->cmd |= 0x80;

	/* Frob the bits. */
#define FROB1(x)  (((x) & 0xf) + ((((x) & 0xf0) >> 4) * 10));
#define FROB2(x)  (((x) & 0xf) + (((((x) & 0xf0) >> 4) & 0x3) * 10));

	/* XXX Should really check that secs register is the same
	 * XXX as when we first read it and if not go back and
	 * XXX read the regs above again.
	 */
	sec = FROB1(sec); min = FROB1(min); day = FROB1(day);
	mon = FROB1(mon); year = FROB1(year);
	hour = FROB2(hour);

#undef FROB1
#undef FROB2

	/* Wheee... */
	if(year < 45)
		year += 30;
	if ((year += 1940) < 1970)
		year += 100;

	return mktime(year, mon, day, hour, min, sec);
}

#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)

__initfunc(void indy_timer_init(void))
{
	struct sgi_ioc_timers *p;
	volatile unsigned char *tcwp, *tc2p;

	/* Figure out the r4k offset, the algorithm is very simple
	 * and works in _all_ cases as long as the 8254 counter
	 * register itself works ok (as an interrupt driving timer
	 * it does not because of bug, this is why we are using
	 * the onchip r4k counter/compare register to serve this
	 * purpose, but for r4k_offset calculation it will work
	 * ok for us).  There are other very complicated ways
	 * of performing this calculation but this one works just
	 * fine so I am not going to futz around. ;-)
	 */
	p = ioc_timers;
	tcwp = &p->tcword;
	tc2p = &p->tcnt2;

	printk("calculating r4koff... ");
	dosample(tcwp, tc2p);			/* First sample. */
	dosample(tcwp, tc2p);			/* Eat one.	*/
	r4k_offset = dosample(tcwp, tc2p);	/* Second sample. */

	printk("%08lx(%d)\n", r4k_offset, (int) r4k_offset);

	r4k_cur = (read_32bit_cp0_register(CP0_COUNT) + r4k_offset);
	write_32bit_cp0_register(CP0_COMPARE, r4k_cur);
	set_cp0_status(ST0_IM, ALLINTS);
	sti();

	/* Read time from the dallas chipset. */
	xtime.tv_sec = get_indy_time();
	xtime.tv_usec = 0;
}

void indy_8254timer_irq(void)
{
	int cpu = smp_processor_id();
	int irq = 4;

	irq_enter(cpu, irq);
	kstat.irqs[0][irq]++;
	printk("indy_8254timer_irq: Whoops, should not have gotten this IRQ\n");
	prom_getchar();
	prom_imode();
	irq_exit(cpu, irq);
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_and_cli(flags);
	*tv = xtime;
	restore_flags(flags);
}

void do_settimeofday(struct timeval *tv)
{
	cli();
	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = MAXPHASE;
	time_esterror = MAXPHASE;
	sti();
}
