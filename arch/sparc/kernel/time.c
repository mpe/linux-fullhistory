/* $Id: time.c,v 1.12 1996/04/04 16:30:30 tridge Exp $
 * linux/arch/sparc/kernel/time.c
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * This file handles the Sparc specific time handling details.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/timex.h>

#include <asm/oplib.h>
#include <asm/segment.h>
#include <asm/timer.h>
#include <asm/mostek.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>

enum sparc_clock_type sp_clock_typ;
struct mostek48t02 *mstk48t02_regs = 0;
struct mostek48t08 *mstk48t08_regs = 0;
static int set_rtc_mmss(unsigned long);

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
void timer_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;

	clear_clock_irq();

	do_timer(regs);

	/* XXX I don't know if this is right for the Sparc yet. XXX */
	if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
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

/* Clock probing, we probe the timers here also. */
volatile unsigned int foo_limit;

static void clock_probe(void)
{
	char node_str[128];
	register int node, type;
	struct linux_prom_registers clk_reg[2];

	/* This will basically traverse the node-tree of the prom to see
	 * which timer chip is on this machine.
	 */
	node = 0;
	if(sparc_cpu_model == sun4) {
		printk("clock_probe: No SUN4 Clock/Timer support yet...\n");
		return;
	}
	if(sparc_cpu_model == sun4c)
		node = prom_getchild(prom_root_node);
	else
		if(sparc_cpu_model == sun4m)
			node=prom_getchild(prom_searchsiblings(prom_getchild(prom_root_node), "obio"));
	type = 0;
	sp_clock_typ = MSTK_INVALID;
	for(;;) {
		prom_getstring(node, "model", node_str, sizeof(node_str));
		if(strcmp(node_str, "mk48t02") == 0) {
			sp_clock_typ = MSTK48T02;
			if(prom_getproperty(node, "reg", (char *) clk_reg, sizeof(clk_reg)) == -1) {
				printk("clock_probe: FAILED!\n");
				halt();
			}
			prom_apply_obio_ranges(clk_reg, 1);
			/* Map the clock register io area read-only */
			mstk48t02_regs = (struct mostek48t02 *) 
				sparc_alloc_io((void *) clk_reg[0].phys_addr,
					       (void *) 0, sizeof(*mstk48t02_regs),
					       "clock", clk_reg[0].which_io, 0x0);
			mstk48t08_regs = 0;  /* To catch weirdness */
			break;
		}

		if(strcmp(node_str, "mk48t08") == 0) {
			sp_clock_typ = MSTK48T08;
			if(prom_getproperty(node, "reg", (char *) clk_reg,
					    sizeof(clk_reg)) == -1) {
				printk("clock_probe: FAILED!\n");
				halt();
			}
			prom_apply_obio_ranges(clk_reg, 1);
			/* Map the clock register io area read-only */
			mstk48t08_regs = (struct mostek48t08 *)
				sparc_alloc_io((void *) clk_reg[0].phys_addr,
					       (void *) 0, sizeof(*mstk48t08_regs),
					       "clock", clk_reg[0].which_io, 0x0);

			mstk48t02_regs = &mstk48t08_regs->regs;
			break;
		}

		node = prom_getsibling(node);
		if(node == 0) {
			printk("Aieee, could not find timer chip type\n");
			return;
		}
	}
}

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) (((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((((val)/10)<<4) + (val)%10)
#endif

void time_init(void)
{
	unsigned int year, mon, day, hour, min, sec;
	struct mostek48t02 *mregs;

#if CONFIG_AP1000
	init_timers(timer_interrupt);
        return;
#endif

	clock_probe();
	init_timers(timer_interrupt);

	mregs = mstk48t02_regs;
	if(!mregs) {
		prom_printf("Something wrong, clock regs not mapped yet.\n");
		prom_halt();
	}		
	mregs->creg |= MSTK_CREG_READ;
	sec = BCD_TO_BIN(mregs->sec);
	min = BCD_TO_BIN(mregs->min);
	hour = BCD_TO_BIN(mregs->hour);
	day = BCD_TO_BIN(mregs->dom);
	mon = BCD_TO_BIN(mregs->mnth);
	year = (BCD_TO_BIN(mregs->yr) + MSTK_YR_ZERO);
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;
	mregs->creg &= ~MSTK_CREG_READ;
	return;
}

/* Nothing fancy on the Sparc yet. */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_flags(flags);
	cli();
#if CONFIG_AP1000
	ap_gettimeofday(&xtime);
#endif
	*tv = xtime;
	restore_flags(flags);
}

void do_settimeofday(struct timeval *tv)
{
	cli();
	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	sti();
}

static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, mostek_minutes;
	struct mostek48t02 *mregs = mstk48t02_regs;

	if(!mregs)
		retval = -1;
	else {
		mregs->creg |= MSTK_CREG_READ;
		mostek_minutes = BCD_TO_BIN(mregs->min);
		mregs->creg &= ~MSTK_CREG_READ;

		real_seconds = nowtime % 60;
		real_minutes = nowtime / 60;
		if (((abs(real_minutes - mostek_minutes) + 15)/30) & 1)
			real_minutes += 30;
		real_minutes %= 60;
		if (abs(real_minutes - mostek_minutes) < 30) {
			mregs->creg |= MSTK_CREG_WRITE;
			mregs->sec = real_seconds;
			mregs->min = real_minutes;
			mregs->creg &= ~MSTK_CREG_WRITE;
		} else
			retval = -1;
	}

	return retval;
}
