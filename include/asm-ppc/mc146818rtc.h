/* mc146818rtc.h - register definitions for the Real-Time-Clock / CMOS RAM
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
#include <asm/io.h>

#ifndef MCRTC_PORT
#define MCRTC_PORT(x)	(0x70 + (x))
#define MCRTC_ALWAYS_BCD	1
#endif

#define CMOS_MCRTC_READ(addr) ({ \
outb_p((addr),MCRTC_PORT(0)); \
inb_p(MCRTC_PORT(1)); \
})
#define CMOS_MCRTC_WRITE(val, addr) ({ \
outb_p((addr),MCRTC_PORT(0)); \
outb_p((val),MCRTC_PORT(1)); \
})

/**********************************************************************
 * register summary
 **********************************************************************/
#define MCRTC_SECONDS		0
#define MCRTC_SECONDS_ALARM	1
#define MCRTC_MINUTES		2
#define MCRTC_MINUTES_ALARM	3
#define MCRTC_HOURS		4
#define MCRTC_HOURS_ALARM		5
/* RTC_*_alarm is always true if 2 MSBs are set */
# define MCRTC_ALARM_DONT_CARE 	0xC0

#define MCRTC_DAY_OF_WEEK		6
#define MCRTC_DAY_OF_MONTH	7
#define MCRTC_MONTH		8
#define MCRTC_YEAR		9

/* control registers - Moto names
 */
#define MCRTC_REG_A		10
#define MCRTC_REG_B		11
#define MCRTC_REG_C		12
#define MCRTC_REG_D		13

/**********************************************************************
 * register details
 **********************************************************************/
#define MCRTC_FREQ_SELECT	MCRTC_REG_A

/* update-in-progress  - set to "1" 244 microsecs before RTC goes off the bus,
 * reset after update (may take 1.984ms @ 32768Hz RefClock) is complete,
 * totalling to a max high interval of 2.228 ms.
 */
# define MCRTC_UIP		0x80
# define MCRTC_DIV_CTL		0x70
   /* divider control: refclock values 4.194 / 1.049 MHz / 32.768 kHz */
#  define MCRTC_REF_CLCK_4MHZ	0x00
#  define MCRTC_REF_CLCK_1MHZ	0x10
#  define MCRTC_REF_CLCK_32KHZ	0x20
   /* 2 values for divider stage reset, others for "testing purposes only" */
#  define MCRTC_DIV_RESET1	0x60
#  define MCRTC_DIV_RESET2	0x70
  /* Periodic intr. / Square wave rate select. 0=none, 1=32.8kHz,... 15=2Hz */
# define MCRTC_RATE_SELECT 	0x0F

/**********************************************************************/
#define MCRTC_CONTROL	MCRTC_REG_B
# define MCRTC_SET 0x80		/* disable updates for clock setting */
# define MCRTC_PIE 0x40		/* periodic interrupt enable */
# define MCRTC_AIE 0x20		/* alarm interrupt enable */
# define MCRTC_UIE 0x10		/* update-finished interrupt enable */
# define MCRTC_SQWE 0x08		/* enable square-wave output */
# define MCRTC_DM_BINARY 0x04	/* all time/date values are BCD if clear */
# define MCRTC_24H 0x02		/* 24 hour mode - else hours bit 7 means pm */
# define MCRTC_DST_EN 0x01	/* auto switch DST - works f. USA only */

/**********************************************************************/
#define MCRTC_INTR_FLAGS	MCRTC_REG_C
/* caution - cleared by read */
# define MCRTC_IRQF 0x80		/* any of the following 3 is active */
# define MCRTC_PF 0x40
# define MCRTC_AF 0x20
# define MCRTC_UF 0x10

/**********************************************************************/
#define MCRTC_VALID	MCRTC_REG_D
# define MCRTC_VRT 0x80		/* valid RAM and time */
/**********************************************************************/

/* example: !(CMOS_READ(MCRTC_CONTROL) & MCRTC_DM_BINARY) 
 * determines if the following two #defines are needed
 */
#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

#endif /* _MC146818RTC_H */
