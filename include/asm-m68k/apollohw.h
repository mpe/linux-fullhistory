/* apollohw.h : some structures to access apollo HW */

#ifndef _ASMm68k_APOLLOHW_H_
#define _ASMm68k_APOLLOHW_H_


/* 
   see scn2681 data sheet for more info. 
   member names are read_write.
*/

#define DECLARE_2681_FIELD(x) unsigned char x; unsigned char dummy##x

struct SCN2681 {

	DECLARE_2681_FIELD(mra);
	DECLARE_2681_FIELD(sra_csra);
	DECLARE_2681_FIELD(BRGtest_cra);
	DECLARE_2681_FIELD(rhra_thra);
	DECLARE_2681_FIELD(ipcr_acr);
	DECLARE_2681_FIELD(isr_imr);
	DECLARE_2681_FIELD(ctu_ctur);
	DECLARE_2681_FIELD(ctl_ctlr);
	DECLARE_2681_FIELD(mrb);
	DECLARE_2681_FIELD(srb_csrb);
	DECLARE_2681_FIELD(tst_crb);
	DECLARE_2681_FIELD(rhrb_thrb);
	DECLARE_2681_FIELD(reserved);
	DECLARE_2681_FIELD(ip_opcr);
	DECLARE_2681_FIELD(startCnt_setOutBit);
	DECLARE_2681_FIELD(stopCnt_resetOutBit);

};

#if 0
struct mc146818 {

	unsigned int second1:4, second2:4, alarm_second1:4, alarm_second2:4,
		     minute1:4, minute2:4, alarm_minute1:4, alarm_minute2:4;
	unsigned int hours1:4, hours2:4, alarm_hours1:4, alarm_hours2:4,
		     day_of_week1:4, day_of_week2:4, day_of_month1:4, day_of_month2:4;
	unsigned int month1:4, month2:4, year1:4, year2:4, :16;

};
#endif

struct mc146818 {
        unsigned char second, alarm_second;
        unsigned char minute, alarm_minute;
        unsigned char hours, alarm_hours;
        unsigned char day_of_week, day_of_month;
        unsigned char month, year;
};

#define IO_BASE 0x80000000

#define SIO01_PHYSADDR 0x10400
#define SIO23_PHYSADDR 0x10500
#define RTC_PHYSADDR 0x10900
#define PICA 0x11000
#define PICB 0x11100
#define sio01 ((*(volatile struct SCN2681 *)(IO_BASE + SIO01_PHYSADDR)))
#define sio23 ((*(volatile struct SCN2681 *)(IO_BASE + SIO01_PHYSADDR)))
#define rtc (((volatile struct mc146818 *)(IO_BASE + RTC_PHYSADDR)))

#define inb(addr) (*((volatile unsigned char *)(addr)))
#define outb(val,addr) (*((volatile unsigned char *)(addr)) = (val))
#define inw(addr) (*((volatile unsigned short *)(addr)))
#define outw(val,addr) (*((volatile unsigned short *)(addr)) = (val))

#endif
