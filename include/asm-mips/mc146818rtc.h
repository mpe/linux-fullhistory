/*
 * Machine dependent access functions for RTC registers.
 */
#ifndef __ASM_MIPS_MC146818RTC_H
#define __ASM_MIPS_MC146818RTC_H

#include <asm/io.h>
#include <asm/vector.h>

#ifndef RTC_PORT
#define RTC_PORT(x)	(0x70 + (x))
#define RTC_ALWAYS_BCD	1
#endif

/*
 * The yet supported machines all access the RTC index register via
 * an ISA port access but the way to access the date register differs ...
 */
#define CMOS_READ(addr) ({ \
outb_p((addr),RTC_PORT(0)); \
feature->rtc_read_data(); \
})
#define CMOS_WRITE(val, addr) ({ \
outb_p((addr),RTC_PORT(0)); \
feature->rtc_write_data(val); \
})

#endif /* __ASM_MIPS_MC146818RTC_H */
