/* $Id: time.c,v 1.3 1999/10/17 10:30:15 gniibe Exp $
 *
 *  linux/arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *
 *  Some code taken from i386 version.
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#include <linux/config.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>

#include <linux/timex.h>
#include <linux/irq.h>

#define TMU_TOCR_INIT	0x00
#define TMU0_TCR_INIT	0x0020
#define TMU_TSTR_INIT	1

#if defined(__sh3__)
#define TMU_TOCR	0xfffffe90	/* Byte access */
#define TMU_TSTR	0xfffffe92	/* Byte access */

#define TMU0_TCOR	0xfffffe94	/* Long access */
#define TMU0_TCNT	0xfffffe98	/* Long access */
#define TMU0_TCR	0xfffffe9c	/* Word access */

#define INTERVAL	37500 /* (1000000*CLOCK_MHZ/HZ/2) ??? */

/* SH-3 RTC */
#define R64CNT  	0xfffffec0
#define RSECCNT 	0xfffffec2
#define RMINCNT 	0xfffffec4
#define RHRCNT  	0xfffffec6
#define RWKCNT  	0xfffffec8
#define RDAYCNT 	0xfffffeca
#define RMONCNT 	0xfffffecc
#define RYRCNT  	0xfffffece
#define RSECAR  	0xfffffed0
#define RMINAR  	0xfffffed2
#define RHRAR   	0xfffffed4
#define RWKAR   	0xfffffed6
#define RDAYAR  	0xfffffed8
#define RMONAR  	0xfffffeda
#define RCR1    	0xfffffedc
#define RCR2    	0xfffffede

#elif defined(__SH4__)
#define TMU_TOCR	0xffd80000	/* Byte access */
#define TMU_TSTR	0xffd80004	/* Byte access */

#define TMU0_TCOR	0xffd80008	/* Long access */
#define TMU0_TCNT	0xffd8000c	/* Long access */
#define TMU0_TCR	0xffd80010	/* Word access */

#define INTERVAL	83333

/* SH-4 RTC */
#define R64CNT  	0xffc80000
#define RSECCNT 	0xffc80004
#define RMINCNT 	0xffc80008
#define RHRCNT  	0xffc8000c
#define RWKCNT  	0xffc80010
#define RDAYCNT 	0xffc80014
#define RMONCNT 	0xffc80018
#define RYRCNT  	0xffc8001c  /* 16bit */
#define RSECAR  	0xffc80020
#define RMINAR  	0xffc80024
#define RHRAR   	0xffc80028
#define RWKAR   	0xffc8002c
#define RDAYAR  	0xffc80030
#define RMONAR  	0xffc80034
#define RCR1    	0xffc80038
#define RCR2    	0xffc8003c
#endif

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

extern rwlock_t xtime_lock;
#define TICK_SIZE tick

void do_gettimeofday(struct timeval *tv)
{
	extern volatile unsigned long lost_ticks;
	unsigned long flags;
	unsigned long usec, sec;

	read_lock_irqsave(&xtime_lock, flags);
	usec = 0;
	{
		unsigned long lost = lost_ticks;
		if (lost)
			usec += lost * (1000000 / HZ);
	}
	sec = xtime.tv_sec;
	usec += xtime.tv_usec;
	read_unlock_irqrestore(&xtime_lock, flags);

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void do_settimeofday(struct timeval *tv)
{
	write_lock_irq(&xtime_lock);
	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_unlock_irq(&xtime_lock);
}

static int set_rtc_time(unsigned long nowtime)
{
#ifdef CONFIG_SH_CPU_RTC
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;

	ctrl_outb(2, RCR2);  /* reset pre-scaler & stop RTC */

	cmos_minutes = ctrl_inb(RMINCNT);
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
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		BIN_TO_BCD(real_seconds);
		BIN_TO_BCD(real_minutes);
		ctrl_outb(real_seconds, RSECCNT);
		ctrl_outb(real_minutes, RMINCNT);
	} else {
		printk(KERN_WARNING
		       "set_rtc_time: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	ctrl_outb(2, RCR2);  /* start RTC */

	return retval;
#else
	/* XXX should support other clock devices? */
	return -1;
#endif
}

/* last time the RTC clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void do_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);

#if 0
	if (!user_mode(regs))
		sh_do_profile(regs->pc);
#endif

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * RTC clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec >= 500000 - ((unsigned) tick) / 2 &&
	    xtime.tv_usec <= 500000 + ((unsigned) tick) / 2) {
		if (set_rtc_time(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
}

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
static void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long timer_status;

	/* Clear UNF bit */
	timer_status = ctrl_inw(TMU0_TCR);
	timer_status &= ~0x100;
	ctrl_outw(timer_status, TMU0_TCR);

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_lock(&xtime_lock);

	do_timer_interrupt(irq, NULL, regs);

	write_unlock(&xtime_lock);
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

static unsigned long get_rtc_time(void)
{
#ifdef CONFIG_SH_CPU_RTC
	unsigned int sec, min, hr, wk, day, mon, yr, yr100;

 again:
	ctrl_outb(1, RCR1);  /* clear CF bit */
	do {
		sec = ctrl_inb(RSECCNT);
		min = ctrl_inb(RMINCNT);
		hr  = ctrl_inb(RHRCNT);
		wk  = ctrl_inb(RWKCNT);
		day = ctrl_inb(RDAYCNT);
		mon = ctrl_inb(RMONCNT);
#if defined(__SH4__)
		yr  = ctrl_inw(RYRCNT);
		yr100 = (yr >> 8);
		yr &= 0xff;
#else
		yr  = ctrl_inb(RYRCNT);
		yr100 = (yr == 0x99) ? 0x19 : 0x20;
#endif
	} while ((ctrl_inb(RCR1) & 0x80) != 0);

	BCD_TO_BIN(yr100);
	BCD_TO_BIN(yr);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(day);
	BCD_TO_BIN(hr);
	BCD_TO_BIN(min);
	BCD_TO_BIN(sec);

	if (yr > 99 || mon < 1 || mon > 12 || day > 31 || day < 1 ||
	    hr > 23 || min > 59 || sec > 59) {
		printk(KERN_ERR
		       "SH RTC: invalid value, resetting to 1 Jan 2000\n");
		ctrl_outb(2, RCR2);  /* reset, stop */
		ctrl_outb(0, RSECCNT);
		ctrl_outb(0, RMINCNT);
		ctrl_outb(0, RHRCNT);
		ctrl_outb(6, RWKCNT);
		ctrl_outb(1, RDAYCNT);
		ctrl_outb(1, RMONCNT);
#if defined(__SH4__)
		ctrl_outw(0x2000, RYRCNT);
#else
		ctrl_outb(0, RYRCNT);
#endif
		ctrl_outb(1, RCR2);  /* start */
		goto again;
	}

	return mktime(yr100 * 100 + yr, mon, day, hr, min, sec);
#else
	/* XXX should support other clock devices? */
	return 0;
#endif
}

static struct irqaction irq0  = { timer_interrupt, SA_INTERRUPT, 0, "timer", NULL, NULL};

void __init time_init(void)
{
	xtime.tv_sec = get_rtc_time();
	xtime.tv_usec = 0;

	set_ipr_data(TIMER_IRQ, TIMER_IRP_OFFSET, TIMER_PRIORITY);
	setup_irq(TIMER_IRQ, &irq0);

	/* Start TMU0 */
	ctrl_outb(TMU_TOCR_INIT,TMU_TOCR);
	ctrl_outw(TMU0_TCR_INIT,TMU0_TCR);
	ctrl_outl(INTERVAL,TMU0_TCOR);
	ctrl_outl(INTERVAL,TMU0_TCNT);
	ctrl_outb(TMU_TSTR_INIT,TMU_TSTR);

#if 0
	/* Start RTC */
	asm volatile("");
#endif
}
