/*
 * $Id: time.c,v 1.39 1998/12/28 10:28:51 paulus Exp $
 * Common time routines among all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 * MPC8xx/MBX changes by Dan Malek (dmalek@jlc.net).
 *
 * Since the MPC8xx has a programmable interrupt timer, I decided to
 * use that rather than the decrementer.  Two reasons: 1.) the clock
 * frequency is low, causing 2.) a long wait in the timer interrupt
 *		while ((d = get_dec()) == dval)
 * loop.  The MPC8xx can be driven from a variety of input clocks,
 * so a number of assumptions have been made here because the kernel
 * parameter HZ is a constant.  We assume (correctly, today :-) that
 * the MPC8xx on the MBX board is driven from a 32.768 kHz crystal.
 * This is then divided by 4, providing a 8192 Hz clock into the PIT.
 * Since it is not possible to get a nice 100 Hz clock out of this, without
 * creating a software PLL, I have set HZ to 128.  -- Dan
 *
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/time.h>
#include <linux/init.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>
#include <asm/cache.h>
#ifdef CONFIG_MBX
#include <asm/mbx.h>
#endif
#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#endif

#include "time.h"

/* this is set to the appropriate pmac/prep/chrp func in init_IRQ() */
int (*set_rtc_time)(unsigned long);

void smp_local_timer_interrupt(struct pt_regs *);

/* keep track of when we need to update the rtc */
unsigned long last_rtc_update = 0;

/* The decrementer counts down by 128 every 128ns on a 601. */
#define DECREMENTER_COUNT_601	(1000000000 / HZ)
#define COUNT_PERIOD_NUM_601	1
#define COUNT_PERIOD_DEN_601	1000

unsigned decrementer_count;	/* count value for 1e6/HZ microseconds */
unsigned count_period_num;	/* 1 decrementer count equals */
unsigned count_period_den;	/* count_period_num / count_period_den us */

/*
 * timer_interrupt - gets called when the decrementer overflows,
 * with interrupts disabled.
 * We set it up to overflow again in 1/HZ seconds.
 */
void timer_interrupt(struct pt_regs * regs)
{
	int dval, d;
	unsigned long cpu = smp_processor_id();
	/* save the HID0 in case dcache was off - see idle.c
	 * this hack should leave for a better solution -- Cort */
	unsigned dcache_locked = unlock_dcache();
	
	hardirq_enter(cpu);
#ifdef __SMP__
	{
		unsigned int loops = 100000000;
		while (test_bit(0, &global_irq_lock)) {
			if (smp_processor_id() == global_irq_holder) {
				printk("uh oh, interrupt while we hold global irq lock!\n");
#ifdef CONFIG_XMON
				xmon(0);
#endif
				break;
			}
			if (loops-- == 0) {
				printk("do_IRQ waiting for irq lock (holder=%d)\n", global_irq_holder);
#ifdef CONFIG_XMON
				xmon(0);
#endif
			}
		}
	}
#endif /* __SMP__ */			
	
	while ((dval = get_dec()) < 0) {
		/*
		 * Wait for the decrementer to change, then jump
		 * in and add decrementer_count to its value
		 * (quickly, before it changes again!)
		 */
		while ((d = get_dec()) == dval)
			;
		set_dec(d + decrementer_count);
		if ( !smp_processor_id() )
		{
			do_timer(regs);
			/*
			 * update the rtc when needed
			 */
			if ( xtime.tv_sec > last_rtc_update + 660 )
			{
				if (set_rtc_time(xtime.tv_sec) == 0)
					last_rtc_update = xtime.tv_sec;
				else
					last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
			}
		}
	}
#ifdef __SMP__
	smp_local_timer_interrupt(regs);
#endif		
#ifdef CONFIG_APUS
	{
		extern void apus_heartbeat (void);
		apus_heartbeat ();
	}
#endif
	hardirq_exit(cpu);
	/* restore the HID0 in case dcache was off - see idle.c
	 * this hack should leave for a better solution -- Cort */
	lock_dcache(dcache_locked);
}

#ifdef CONFIG_MBX
/* A place holder for time base interrupts, if they are ever enabled.
*/
void timebase_interrupt(int irq, void * dev, struct pt_regs * regs)
{
	printk("timebase_interrupt()\n");
}

/* The RTC on the MPC8xx is an internal register.
 * We want to protect this during power down, so we need to unlock,
 * modify, and re-lock.
 */
static int
mbx_set_rtc_time(unsigned long time)
{
	((immap_t *)IMAP_ADDR)->im_sitk.sitk_rtck = KAPWR_KEY;
	((immap_t *)IMAP_ADDR)->im_sit.sit_rtc = time;
	((immap_t *)IMAP_ADDR)->im_sitk.sitk_rtck = ~KAPWR_KEY;
	return(0);
}
#endif /* CONFIG_MBX */

/*
 * This version of gettimeofday has microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	*tv = xtime;
	/* XXX we don't seem to have the decrementers synced properly yet */
#ifndef __SMP__
	tv->tv_usec += (decrementer_count - get_dec())
	    * count_period_num / count_period_den;
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
#endif
	restore_flags(flags);
}

void do_settimeofday(struct timeval *tv)
{
	unsigned long flags;
	int frac_tick;
	
	last_rtc_update = 0; /* so the rtc gets updated soon */
	
	frac_tick = tv->tv_usec % (1000000 / HZ);
	save_flags(flags);
	cli();
	xtime.tv_sec = tv->tv_sec;
	xtime.tv_usec = tv->tv_usec - frac_tick;
	set_dec(frac_tick * count_period_den / count_period_num);
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_state = TIME_ERROR;	/* p. 24, (a) */
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	restore_flags(flags);
}


__initfunc(void time_init(void))
{
#ifndef CONFIG_MBX
	if ((_get_PVR() >> 16) == 1) {
		/* 601 processor: dec counts down by 128 every 128ns */
		decrementer_count = DECREMENTER_COUNT_601;
		count_period_num = COUNT_PERIOD_NUM_601;
		count_period_den = COUNT_PERIOD_DEN_601;
	}

	switch (_machine) {
	case _MACH_Pmac:
		xtime.tv_sec = pmac_get_rtc_time();
		if ( (_get_PVR() >> 16) != 1 && (!smp_processor_id()) )
			pmac_calibrate_decr();
		if ( !smp_processor_id() )
			set_rtc_time = pmac_set_rtc_time;
		break;
	case _MACH_chrp:
		chrp_time_init();
		xtime.tv_sec = chrp_get_rtc_time();
		if ((_get_PVR() >> 16) != 1)
			chrp_calibrate_decr();
		set_rtc_time = chrp_set_rtc_time;
		break;
	case _MACH_prep:
		xtime.tv_sec = prep_get_rtc_time();
		prep_calibrate_decr();
		set_rtc_time = prep_set_rtc_time;
		break;
#ifdef CONFIG_APUS		
	case _MACH_apus:
	{
		xtime.tv_sec = apus_get_rtc_time();
		apus_calibrate_decr();
		set_rtc_time = apus_set_rtc_time;
 		break;
	}
#endif	
	}
	xtime.tv_usec = 0;
#else /* CONFIG_MBX */
	mbx_calibrate_decr();
	set_rtc_time = mbx_set_rtc_time;

	/* First, unlock all of the registers we are going to modify.
	 * To protect them from corruption during power down, registers
	 * that are maintained by keep alive power are "locked".  To
	 * modify these registers we have to write the key value to
	 * the key location associated with the register.
	 */
	((immap_t *)IMAP_ADDR)->im_sitk.sitk_tbscrk = KAPWR_KEY;
	((immap_t *)IMAP_ADDR)->im_sitk.sitk_rtcsck = KAPWR_KEY;


	/* Disable the RTC one second and alarm interrupts.
	*/
	((immap_t *)IMAP_ADDR)->im_sit.sit_rtcsc &=
						~(RTCSC_SIE | RTCSC_ALE);

	/* Enabling the decrementer also enables the timebase interrupts
	 * (or from the other point of view, to get decrementer interrupts
	 * we have to enable the timebase).  The decrementer interrupt
	 * is wired into the vector table, nothing to do here for that.
	 */
	((immap_t *)IMAP_ADDR)->im_sit.sit_tbscr =
				((mk_int_int_mask(DEC_INTERRUPT) << 8) |
					 (TBSCR_TBF | TBSCR_TBE));
	if (request_irq(DEC_INTERRUPT, timebase_interrupt, 0, "tbint", NULL) != 0)
		panic("Could not allocate timer IRQ!");

	/* Get time from the RTC.
	*/
	xtime.tv_sec = ((immap_t *)IMAP_ADDR)->im_sit.sit_rtc;
	xtime.tv_usec = 0;

#endif /* CONFIG_MBX */
	set_dec(decrementer_count);
	/* mark the rtc/on-chip timer as in sync
	 * so we don't update right away
	 */
	last_rtc_update = xtime.tv_sec;
}

#ifndef CONFIG_MBX
/*
 * Uses the on-board timer to calibrate the on-chip decrementer register
 * for prep systems.  On the pmac the OF tells us what the frequency is
 * but on prep we have to figure it out.
 * -- Cort
 */
int calibrate_done = 0;
volatile int *done_ptr = &calibrate_done;
__initfunc(void prep_calibrate_decr(void))
{
	unsigned long flags;

	/* the Powerstack II's have trouble with the timer so
	 * we use a default value -- Cort
	 */
	if ( (_prep_type == _PREP_Motorola) &&
	     ((inb(0x800) & 0xF0) & 0x40) )
	{
		unsigned long freq, divisor;
		static unsigned long t2 = 0;
		
		t2 = 998700000/60;
		freq = t2 * 60;	/* try to make freq/1e6 an integer */
		divisor = 60;
		printk("time_init: decrementer frequency = %lu/%lu (%luMHz)\n",
		       freq, divisor,t2>>20);
		decrementer_count = freq / HZ / divisor;
		count_period_num = divisor;
		count_period_den = freq / 1000000;
		return;
	}
	
	
	save_flags(flags);

#define TIMER0_COUNT 0x40
#define TIMER_CONTROL 0x43
	/* set timer to periodic mode */
	outb_p(0x34,TIMER_CONTROL);/* binary, mode 2, LSB/MSB, ch 0 */
	/* set the clock to ~100 Hz */
	outb_p(LATCH & 0xff , TIMER0_COUNT);	/* LSB */
	outb(LATCH >> 8 , TIMER0_COUNT);	/* MSB */
	
	if (request_irq(0, prep_calibrate_decr_handler, 0, "timer", NULL) != 0)
		panic("Could not allocate timer IRQ!");
	__sti();
	while ( ! *done_ptr ) /* nothing */; /* wait for calibrate */
        restore_flags(flags);
	free_irq( 0, NULL);
}

__initfunc(void prep_calibrate_decr_handler(int irq, void *dev, struct pt_regs * regs))
{
	unsigned long freq, divisor;
	static unsigned long t1 = 0, t2 = 0;
	
	if ( !t1 )
		t1 = get_dec();
	else if (!t2)
	{
		t2 = get_dec();
		t2 = t1-t2;  /* decr's in 1/HZ */
		t2 = t2*HZ;  /* # decrs in 1s - thus in Hz */
		freq = t2 * 60;	/* try to make freq/1e6 an integer */
		divisor = 60;
		printk("time_init: decrementer frequency = %lu/%lu (%luMHz)\n",
		       freq, divisor,t2>>20);
		decrementer_count = freq / HZ / divisor;
		count_period_num = divisor;
		count_period_den = freq / 1000000;
		*done_ptr = 1;
	}
}

#else /* CONFIG_MBX */

/* The decrementer counts at the system (internal) clock frequency divided by
 * sixteen, or external oscillator divided by four.  Currently, we only
 * support the MBX, which is system clock divided by sixteen.
 */
__initfunc(void mbx_calibrate_decr(void))
{
	bd_t	*binfo = (bd_t *)res;
	int freq, fp, divisor;

	if ((((immap_t *)IMAP_ADDR)->im_clkrst.car_sccr & 0x02000000) == 0)
		printk("WARNING: Wrong decrementer source clock.\n");

	/* The manual says the frequency is in Hz, but it is really
	 * as MHz.  The value 'fp' is the number of decrementer ticks
	 * per second.
	 */
	fp = (binfo->bi_intfreq * 1000000) / 16;
	freq = fp*60;	/* try to make freq/1e6 an integer */
        divisor = 60;
        printk("time_init: decrementer frequency = %d/%d\n", freq, divisor);
        decrementer_count = freq / HZ / divisor;
        count_period_num = divisor;
        count_period_den = freq / 1000000;
}
#endif /* CONFIG_MBX */

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
unsigned long mktime(unsigned int year, unsigned int mon,
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

#define TICK_SIZE tick
#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

void to_tm(int tim, struct rtc_time * tm)
{
	register int    i;
	register long   hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;
}



