/*
 * PreP compliant NVRAM access
 */

#ifndef _PPC_NVRAM_H
#define _PPC_NVRAM_H

#define NVRAM_AS0  0x74
#define NVRAM_AS1  0x75
#define NVRAM_DATA 0x77

/* RTC Offsets */

#define RTC_SECONDS		0x1FF9
#define RTC_MINUTES		0x1FFA
#define RTC_HOURS		0x1FFB
#define RTC_DAY_OF_WEEK		0x1FFC
#define RTC_DAY_OF_MONTH	0x1FFD
#define RTC_MONTH		0x1FFE
#define RTC_YEAR		0x1FFF

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

#endif
