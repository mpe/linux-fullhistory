/*
 *	Real Time Clock interface for Linux on the BVME6000
 *
 * Based on the PC driver by Paul Gortmaker.
 */

#define RTC_VERSION		"1.00"

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/mc146818rtc.h>	/* For struct rtc_time and ioctls, etc */
#include <asm/bvme6000hw.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/setup.h>

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

#define BCD2BIN(val) (((val)&15) + ((val)>>4)*10)
#define BIN2BCD(val) ((((val)/10)<<4) + (val)%10)

static unsigned char days_in_mo[] =
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static char rtc_status = 0;

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	volatile RtcPtr_t rtc = (RtcPtr_t)BVME_RTC_BASE;
	unsigned char msr;
	unsigned long flags;
	struct rtc_time wtime; 

	switch (cmd) {
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		save_flags(flags);
		cli();
		/* Ensure clock and real-time-mode-register are accessible */
		msr = rtc->msr & 0xc0;
		rtc->msr = 0x40;
		do {
			wtime.tm_sec =  BCD2BIN(rtc->bcd_sec);
			wtime.tm_min =  BCD2BIN(rtc->bcd_min);
			wtime.tm_hour = BCD2BIN(rtc->bcd_hr);
			wtime.tm_mday =  BCD2BIN(rtc->bcd_dom);
			wtime.tm_mon =  BCD2BIN(rtc->bcd_mth)-1;
			wtime.tm_year = BCD2BIN(rtc->bcd_year);
			if (wtime.tm_year < 70)
				wtime.tm_year += 100;
			wtime.tm_wday = BCD2BIN(rtc->bcd_dow)-1;
		} while (wtime.tm_sec != BCD2BIN(rtc->bcd_sec));
		rtc->msr = msr;
		restore_flags(flags);
		return copy_to_user((void *)arg, &wtime, sizeof wtime) ?
								-EFAULT : 0;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		unsigned char leap_yr;
		struct rtc_time rtc_tm;

		if (!suser())
			return -EACCES;

		if (copy_from_user(&rtc_tm, (struct rtc_time*)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		leap_yr = ((!(rtc_tm.tm_year % 4) && (rtc_tm.tm_year % 100)) || !(rtc_tm.tm_year % 400));

		if ((rtc_tm.tm_mon > 12) || (rtc_tm.tm_mday == 0))
			return -EINVAL;

		if (rtc_tm.tm_mday > (days_in_mo[rtc_tm.tm_mon] + ((rtc_tm.tm_mon == 2) && leap_yr)))
			return -EINVAL;
			
		if ((rtc_tm.tm_hour >= 24) || (rtc_tm.tm_min >= 60) || (rtc_tm.tm_sec >= 60))
			return -EINVAL;

		save_flags(flags);
		cli();
		/* Ensure clock and real-time-mode-register are accessible */
		msr = rtc->msr & 0xc0;
		rtc->msr = 0x40;

		rtc->t0cr_rtmr = rtc_tm.tm_year%4;
		rtc->bcd_tenms = 0;
		rtc->bcd_sec = BIN2BCD(rtc_tm.tm_sec);
		rtc->bcd_min = BIN2BCD(rtc_tm.tm_min);
		rtc->bcd_hr  = BIN2BCD(rtc_tm.tm_hour);
		rtc->bcd_dom = BIN2BCD(rtc_tm.tm_mday);
		rtc->bcd_mth = BIN2BCD(rtc_tm.tm_mon + 1);
		rtc->bcd_year = BIN2BCD(rtc_tm.tm_year%100);
		if (rtc_tm.tm_wday >= 0)
			rtc->bcd_dow = BIN2BCD(rtc_tm.tm_wday+1);
		rtc->t0cr_rtmr = rtc_tm.tm_year%4 | 0x08;

		rtc->msr = msr;
		restore_flags(flags);
		return 0;
	}
	default:
		return -EINVAL;
	}
}

/*
 *	We enforce only one user at a time here with the open/close.
 *	Also clear the previous interrupt data on an open, and clean
 *	up things on a close.
 */

static int rtc_open(struct inode *inode, struct file *file)
{
	if(rtc_status)
		return -EBUSY;

	rtc_status = 1;
	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	rtc_status = 0;
	return 0;
}

/*
 *	The various file operations we support.
 */

static struct file_operations rtc_fops = {
	NULL,
	NULL,
	NULL,		/* No write */
	NULL,		/* No readdir */
	NULL,
	rtc_ioctl,
	NULL,		/* No mmap */
	rtc_open,
	NULL,		/* flush */
	rtc_release
};

static struct miscdevice rtc_dev=
{
	RTC_MINOR,
	"rtc",
	&rtc_fops
};

__initfunc(int rtc_DP8570A_init(void))
{
	if (!MACH_IS_BVME6000)
		return -ENODEV;

	printk(KERN_INFO "DP8570A Real Time Clock Driver v%s\n", RTC_VERSION);
	misc_register(&rtc_dev);
	return 0;
}

