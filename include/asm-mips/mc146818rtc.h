/*
 * Machine dependent access functions for RTC registers.
 */
#ifndef __ASM_MIPS_MC146818RTC_H
#define __ASM_MIPS_MC146818RTC_H

#include <asm/io.h>
#include <asm/vector.h>

#ifndef RTC_PORT
#define RTC_PORT(x)	(0x70 + (x))
#define RTC_ALWAYS_BCD	0	/* RTC operates in binary mode */
#endif

/*
 * The yet supported machines all access the RTC index register via
 * an ISA port access but the way to access the date register differs ...
 */
#define CMOS_READ(addr) ({ \
feature->rtc_read_data(addr); \
})
#define CMOS_WRITE(val, addr) ({ \
feature->rtc_write_data(val, addr); \
})

#endif /* __ASM_MIPS_MC146818RTC_H */
