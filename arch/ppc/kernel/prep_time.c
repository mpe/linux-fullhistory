/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * Adapted for PowerPC (PreP) by Gary Thomas
 * Modified by Cort Dougan (cort@cs.nmt.edu)
 *  copied and modified from intel version
 *
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>

#include "time.h"

/*
 * The motorola uses the m48t18 rtc (includes DS1643) whose registers
 * are at a higher end of nvram (1ff8-1fff) than the ibm mc146818
 * rtc (ds1386) which has regs at addr 0-d).  The intel gets
 * past this because the bios emulates the mc146818.
 *
 * Why in the world did they have to use different clocks?
 *
 * Right now things are hacked to check which machine we're on then
 * use the appropriate macro.  This is very very ugly and I should
 * probably have a function that checks which machine we're on then
 * does things correctly transparently or a function pointer which
 * is setup at boot time to use the correct addresses.
 * -- Cort
 */
/*
 * translate from mc146818 to m48t18  addresses
 */
unsigned int clock_transl[] __prepdata = { MOTO_RTC_SECONDS,0 /* alarm */,
		       MOTO_RTC_MINUTES,0 /* alarm */,
		       MOTO_RTC_HOURS,0 /* alarm */,                 /*  4,5 */
		       MOTO_RTC_DAY_OF_WEEK,
		       MOTO_RTC_DAY_OF_MONTH,
		       MOTO_RTC_MONTH,
		       MOTO_RTC_YEAR,                    /* 9 */
		       MOTO_RTC_CONTROLA, MOTO_RTC_CONTROLB /* 10,11 */
};

__prep
int prep_cmos_clock_read(int addr)
{
	if ( _prep_type == _PREP_IBM )
		return CMOS_READ(addr);
	else if ( _prep_type == _PREP_Motorola )
	{
		outb(clock_transl[addr]>>8, NVRAM_AS1);
		outb(clock_transl[addr], NVRAM_AS0);
		return (inb(NVRAM_DATA));
	}

	printk("Unknown machine in prep_cmos_clock_read()!\n");
	return -1;
}

__prep
void prep_cmos_clock_write(unsigned long val, int addr)
{
	if ( _prep_type == _PREP_IBM )
	{
		CMOS_WRITE(val,addr);
		return;
	}
	else if ( _prep_type == _PREP_Motorola )
	{
		outb(clock_transl[addr]>>8, NVRAM_AS1);
		outb(clock_transl[addr], NVRAM_AS0);
		outb(val,NVRAM_DATA);
		return;
	}
	printk("Unknown machine in prep_cmos_clock_write()!\n");
}

/*
 * Set the hardware clock. -- Cort
 */
__prep
int prep_set_rtc_time(unsigned long nowtime)
{
	unsigned char save_control, save_freq_select;
	struct rtc_time tm;

	to_tm(nowtime, &tm);

	save_control = prep_cmos_clock_read(RTC_CONTROL); /* tell the clock it's being set */

	prep_cmos_clock_write((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = prep_cmos_clock_read(RTC_FREQ_SELECT); /* stop and reset prescaler */
	
	prep_cmos_clock_write((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

        tm.tm_year -= 1900;
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_mon);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}
	prep_cmos_clock_write(tm.tm_sec,RTC_SECONDS);
	prep_cmos_clock_write(tm.tm_min,RTC_MINUTES);
	prep_cmos_clock_write(tm.tm_hour,RTC_HOURS);
	prep_cmos_clock_write(tm.tm_mon,RTC_MONTH);
	prep_cmos_clock_write(tm.tm_mday,RTC_DAY_OF_MONTH);
	prep_cmos_clock_write(tm.tm_year,RTC_YEAR);
	
	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	prep_cmos_clock_write(save_control, RTC_CONTROL);
	prep_cmos_clock_write(save_freq_select, RTC_FREQ_SELECT);

	if ( (time_state == TIME_ERROR) || (time_state == TIME_BAD) )
		time_state = TIME_OK;
	return 0;
}

__prep
unsigned long prep_get_rtc_time(void)
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
		if (prep_cmos_clock_read(RTC_FREQ_SELECT) & RTC_UIP)
			break;
	for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms */
		if (!(prep_cmos_clock_read(RTC_FREQ_SELECT) & RTC_UIP))
			break;
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
		sec = prep_cmos_clock_read(RTC_SECONDS);
		min = prep_cmos_clock_read(RTC_MINUTES);
		hour = prep_cmos_clock_read(RTC_HOURS);
		day = prep_cmos_clock_read(RTC_DAY_OF_MONTH);
		mon = prep_cmos_clock_read(RTC_MONTH);
		year = prep_cmos_clock_read(RTC_YEAR);
	} while (sec != prep_cmos_clock_read(RTC_SECONDS));
	if (!(prep_cmos_clock_read(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
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
