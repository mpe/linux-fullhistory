/* $Id: ds1286.h,v 1.1 1998/07/09 20:01:30 ralf Exp $
 *
 * mc146818rtc.h - register definitions for the Real-Time-Clock / CMOS RAM
 * Copyright Torsten Duwe <duwe@informatik.uni-erlangen.de> 1993
 * derived from Data Sheet, Copyright Motorola 1984 (!).
 * It was written to be part of the Linux operating system.
 */
/* permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */
#ifndef _MC146818RTC_H
#define _MC146818RTC_H

#include <asm/mc146818rtc.h>

/**********************************************************************
 * register summary
 **********************************************************************/
#define RTC_HUNDREDTH_SECOND	0
#define RTC_SECONDS		1
#define RTC_MINUTES		2
#define RTC_MINUTES_ALARM	3
#define RTC_HOURS		4
#define RTC_HOURS_ALARM		5
#define RTC_DAY			6
#define RTC_DAY_ALARM		7
#define RTC_DATE		8
#define RTC_MONTH		9
#define RTC_YEAR		10
#define RTC_CMD			11
#define RTC_WHSEC		12
#define RTC_WSEC		13
#define RTC_UNUSED		14

/* RTC_*_alarm is always true if 2 MSBs are set */
# define RTC_ALARM_DONT_CARE 	0xC0


/*
 * Bits in the month register
 */
#define RTC_EOSC		0x80
#define RTC_ESQW		0x40

/*
 * Bits in the Command register
 */
#define RTC_TDF			0x01
#define RTC_WAF			0x02
#define RTC_TDM			0x04
#define RTC_WAM			0x08
#define RTC_PU_LVL		0x10
#define RTC_IBH_LO		0x20
#define RTC_IPSW		0x40
#define RTC_TE			0x80

/*
 * Conversion between binary and BCD.
 */
#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

#endif /* _MC146818RTC_H */
