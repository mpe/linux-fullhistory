/* $Id: ip27-timer.c,v 1.3 2000/02/18 09:54:40 ulfc Exp $
 *
 * Copytight (C) 1999 Ralf Baechle (ralf@gnu.org)
 * Copytight (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/param.h>
#include <linux/timex.h>
#include <linux/mm.h>		

#include <asm/pgtable.h>
#include <asm/sgialib.h>
#include <asm/ioc3.h>
#include <asm/m48t35.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/arch.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn0/ip27.h>
#include <asm/sn/sn0/hub.h>

/* This is a hack; we really need to figure these values out dynamically
 * 
 * Since 800 ns works very well with various HUB frequencies, such as
 * 360, 380, 390 and 400 MHZ, we use 800 ns rtc cycle time.
 *
 * Ralf: which clock rate is used to feed the counter?
 */
#define NSEC_PER_CYCLE		800
#define NSEC_PER_SEC		1000000000
#define CYCLES_PER_SEC		(NSEC_PER_SEC/NSEC_PER_CYCLE)
#define CYCLES_PER_JIFFY	(CYCLES_PER_SEC/HZ)

static unsigned long ct_cur;		/* What counter should be at next timer irq */
static long last_rtc_update = 0;	/* Last time the rtc clock got updated */

extern rwlock_t xtime_lock;


static int set_rtc_mmss(unsigned long nowtime)
{               
        int retval = 0;
        int real_seconds, real_minutes, cmos_minutes;
        unsigned char save_control, save_freq_select;
	struct m48t35_rtc *rtc;
	nasid_t nid;

	nid = get_nasid();
	rtc = (struct m48t35_rtc *)
	    KL_CONFIG_CH_CONS_INFO(nid)->memory_base + IOC3_BYTEBUS_DEV0;
                
	rtc->control |= M48T35_RTC_READ;
	cmos_minutes = rtc->min;
	BCD_TO_BIN(cmos_minutes);
	rtc->control &= ~M48T35_RTC_READ;
         
        /*      
         * Since we're only adjusting minutes and seconds,
         * don't interfere with hour overflow. This avoids
         * messing with unknown time zones but requires your
         * RTC not to be off by more than 15 minutes
         */     
        real_seconds = nowtime % 60;
        real_minutes = nowtime / 60;
        if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
                real_minutes += 30;             /* correct for half hour time zone */           
        real_minutes %= 60;
                
        if (abs(real_minutes - cmos_minutes) < 30) {
		BIN_TO_BCD(real_seconds);
		BIN_TO_BCD(real_minutes);
		rtc->control |= M48T35_RTC_SET;
                rtc->sec = real_seconds;
                rtc->min = real_minutes;
		rtc->control &= ~M48T35_RTC_SET;
        } else {
                printk(KERN_WARNING
                       "set_rtc_mmss: can't update from %d to %d\n",
                       cmos_minutes, real_minutes);
                retval = -1;
        }

                
        return retval;
}

void rt_timer_interrupt(struct pt_regs *regs)
{
	int irq = 7;				/* XXX Assign number */

	write_lock(&xtime_lock);

again:
	LOCAL_HUB_S(PI_RT_PEND_A, 0);		/* Ack  */
	ct_cur += CYCLES_PER_JIFFY;
	LOCAL_HUB_S(PI_RT_COMPARE_A, ct_cur);

	if (LOCAL_HUB_L(PI_RT_COUNT) >= ct_cur)
		goto again;

	kstat.irqs[0][irq]++;
	do_timer(regs);
	
        /*
         * If we have an externally synchronized Linux clock, then update
         * RTC clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
         * called as close as possible to when a second starts.
         */

        if ((time_status & STA_UNSYNC) == 0 &&
            xtime.tv_sec > last_rtc_update + 660) {
		if (xtime.tv_usec >= 1000000 - ((unsigned) tick) / 2) {
			if (set_rtc_mmss(xtime.tv_sec + 1) == 0)
				last_rtc_update = xtime.tv_sec;
			else    
				last_rtc_update = xtime.tv_sec - 600;
		} else if (xtime.tv_usec <= ((unsigned) tick) / 2) {
			if (set_rtc_mmss(xtime.tv_sec) == 0)
				last_rtc_update = xtime.tv_sec;
			else    
				last_rtc_update = xtime.tv_sec - 600;
		}
        }

	write_unlock(&xtime_lock);
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	read_lock_irqsave(&xtime_lock, flags);
	*tv = xtime;
	read_unlock_irqrestore(&xtime_lock, flags);
}

void do_settimeofday(struct timeval *tv)
{
	write_lock_irq(&xtime_lock);
	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = MAXPHASE;
	time_esterror = MAXPHASE;
	write_unlock_irq(&xtime_lock);
}

/* Includes for ioc3_init().  */
#include <linux/init.h>
#include <asm/sn/types.h>
#include <asm/sn/sn0/addrs.h>
#include <asm/sn/sn0/hubni.h>
#include <asm/sn/sn0/hubio.h>
#include <asm/sn/klconfig.h>
#include <asm/ioc3.h>
#include <asm/pci/bridge.h>

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
        if (0 >= (int) (mon -= 2)) {    /* 1..12 -> 11,12,1..10 */
                mon += 12;      /* Puts Feb last since it has leap day */
                year -= 1;
        }
        return (((
            (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
              year*365 - 719499
            )*24 + hour /* now have hours */
           )*60 + min /* now have minutes */
          )*60 + sec; /* finally seconds */
}

#define DEBUG_RTC

static unsigned long __init get_m48t35_time(void)
{       
        unsigned int year, month, date, hour, min, sec;
	struct m48t35_rtc *rtc;
	nasid_t nid;

	nid = get_nasid();
	rtc = KL_CONFIG_CH_CONS_INFO(nid)->memory_base + IOC3_BYTEBUS_DEV0;

        rtc->control |= M48T35_RTC_READ;
	sec = rtc->sec;
	min = rtc->min;
	hour = rtc->hour;
	date = rtc->date;
	month = rtc->month;
	year = rtc->year;
	rtc->control &= ~M48T35_RTC_READ;

        BCD_TO_BIN(sec);
        BCD_TO_BIN(min);
        BCD_TO_BIN(hour);
        BCD_TO_BIN(date);
        BCD_TO_BIN(month);
        BCD_TO_BIN(year);

        year += 1970;

        return mktime(year, month, date, hour, min, sec);
}

extern void ioc3_eth_init(void);

void __init time_init(void)
{
	lboard_t *board;
	klcpu_t *cpu;
	int cpuid;
	
	xtime.tv_sec = get_m48t35_time();
	xtime.tv_usec = 0;

	/* Don't use ARCS.  ARCS is fragile.  Klconfig is simple and sane.  */
	board = find_lboard(KLTYPE_IP27);
	if (!board)
		panic("Can't find board info for myself.");

	cpuid = LOCAL_HUB_L(PI_CPU_NUM) ? IP27_CPU0_INDEX : IP27_CPU1_INDEX;
	cpu = (klcpu_t *) KLCF_COMP(board, cpuid);
	if (!cpu)
		panic("No information about myself?");

	printk("CPU clock is %dMHz.\n", cpu->cpu_speed);

	/* Don't worry about second CPU, it's disabled.  */
	LOCAL_HUB_S(PI_RT_EN_A, 1);
	LOCAL_HUB_S(PI_PROF_EN_A, 0);
	ct_cur = CYCLES_PER_JIFFY;
	LOCAL_HUB_S(PI_RT_COMPARE_A, ct_cur);
	LOCAL_HUB_S(PI_RT_COUNT, 0);
	LOCAL_HUB_S(PI_RT_PEND_A, 0);

	set_cp0_status(SRB_TIMOCLK, SRB_TIMOCLK);
}
