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

/*
 * The following struture is used to access the MK48T18
 */
typedef volatile struct _MK48T18 {
	unsigned char	ucNvRAM[0x3ff8]; /* NvRAM locations */
	unsigned char	ucControl;
	unsigned char	ucSecond;	/* 0-59 */
	unsigned char	ucMinute;	/* 0-59 */
	unsigned char	ucHour;		/* 0-23 */
	unsigned char	ucDay;		/* 1-7 */
	unsigned char	ucDate;		/* 1-31 */
	unsigned char	ucMonth;	/* 1-12 */
	unsigned char	ucYear;		/* 0-99 */
} MK48T18, *PMK48T18;

/*
 * The control register contains a 5 bit calibration value plus sign
 * and read/write enable bits
 */
#define MK48T18_CTRL_CAL_MASK	0x1f
#define MK48T18_CTRL_CAL_SIGN	0x20
#define MK48T18_CTRL_READ	0x40
#define MK48T18_CTRL_WRITE	0x80
/*
 * The STOP bit is the most significant bit of the seconds location
 */
#define MK48T18_SEC_MASK	0x7f
#define MK48T18_SEC_STOP	0x80
/*
 * The day location also contains the frequency test bit which should
 * be zero for normal operation
 */
#define MK48T18_DAY_MASK	0x07
#define MK48T18_DAY_FT		0x40

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
        else if ( _prep_type == _PREP_Radstone )
                return CMOS_READ(addr);

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
        else if ( _prep_type == _PREP_Radstone )
        {
                CMOS_WRITE(val,addr);
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
		BIN_TO_BCD(tm.tm_wday);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}
	prep_cmos_clock_write(tm.tm_sec,RTC_SECONDS);
	prep_cmos_clock_write(tm.tm_min,RTC_MINUTES);
	prep_cmos_clock_write(tm.tm_hour,RTC_HOURS);
	prep_cmos_clock_write(tm.tm_mon,RTC_MONTH);
	prep_cmos_clock_write(tm.tm_wday+1,RTC_DAY_OF_WEEK);
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

	/*
	 * Radstone Technology PPC1a boards use an MK48T18 device
	 * as the "master" RTC but also have a DS1287 equivalent incorporated
	 * into the PCI-ISA bridge device. The DS1287 is initialised by the boot
	 * firmware to reflect the value held in the MK48T18 and thus the
	 * time may be read from this device both here and in the rtc driver.
	 * Whenever we set the time, however, if it is to be preserved across
	 * boots we must also update the "master" RTC.
	 */
	if((_prep_type==_PREP_Radstone) && (ucSystemType==RS_SYS_TYPE_PPC1a))
	{
		PMK48T18 pMk48t18=(PMK48T18)(_ISA_MEM_BASE+0x00800000);

		/*
		 * Set the write enable bit
		 */
		pMk48t18->ucControl|=MK48T18_CTRL_WRITE;
		eieio();
		/*
		 * Update the clock
		 */
		pMk48t18->ucSecond=tm.tm_sec;
		pMk48t18->ucMinute=tm.tm_min;
		pMk48t18->ucHour=tm.tm_hour;
		pMk48t18->ucMonth=tm.tm_mon;
		pMk48t18->ucDay=tm.tm_wday+1;
		pMk48t18->ucDate=tm.tm_mday;
		pMk48t18->ucYear=tm.tm_year;

		eieio();
		/*
		 * Clear the write enable bit
		 */
		pMk48t18->ucControl&=~MK48T18_CTRL_WRITE;
	}

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
