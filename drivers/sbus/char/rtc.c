/* $Id: rtc.c,v 1.13 1998/08/26 10:29:44 davem Exp $
 *
 * Linux/SPARC Real Time Clock Driver
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * This is a little driver that lets a user-level program access
 * the SPARC Mostek real time clock chip. It is no use unless you
 * use the modified clock utility.
 *
 * Get the modified clock utility from:
 *   ftp://vger.rutgers.edu/pub/linux/Sparc/userland/clock.c
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <asm/mostek.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/rtc.h>

static int rtc_busy = 0;

/* Retrieve the current date and time from the real time clock. */
void get_rtc_time(struct rtc_time *t)
{
	register struct mostek48t02 *regs = mstk48t02_regs;
	unsigned long flags;

	save_flags(flags);
	cli();
	regs->creg |= MSTK_CREG_READ;

	t->sec = MSTK_REG_SEC(regs);
	t->min = MSTK_REG_MIN(regs);
	t->hour = MSTK_REG_HOUR(regs);
	t->dow = MSTK_REG_DOW(regs);
	t->dom = MSTK_REG_DOM(regs);
	t->month = MSTK_REG_MONTH(regs);
	t->year = MSTK_CVT_YEAR( MSTK_REG_YEAR(regs) );

	regs->creg &= ~MSTK_CREG_READ;
	restore_flags(flags);
}

/* Set the current date and time inthe real time clock. */
void set_rtc_time(struct rtc_time *t)
{
	register struct mostek48t02 *regs = mstk48t02_regs;
	unsigned long flags;

	save_flags(flags);
	cli();
	regs->creg |= MSTK_CREG_WRITE;

	MSTK_SET_REG_SEC(regs,t->sec);
	MSTK_SET_REG_MIN(regs,t->min);
	MSTK_SET_REG_HOUR(regs,t->hour);
	MSTK_SET_REG_DOW(regs,t->dow);
	MSTK_SET_REG_DOM(regs,t->dom);
	MSTK_SET_REG_MONTH(regs,t->month);
	MSTK_SET_REG_YEAR(regs,t->year - MSTK_YEAR_ZERO);

	regs->creg &= ~MSTK_CREG_WRITE;
	restore_flags(flags);
}

static long long rtc_lseek(struct file *file, long long offset, int origin)
{
	return -ESPIPE;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct rtc_time rtc_tm;

	switch (cmd)
	{
	case RTCGET:
		get_rtc_time(&rtc_tm);

		copy_to_user_ret((struct rtc_time*)arg, &rtc_tm, sizeof(struct rtc_time), -EFAULT);

		return 0;


	case RTCSET:
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		copy_from_user_ret(&rtc_tm, (struct rtc_time*)arg, sizeof(struct rtc_time), -EFAULT);

		set_rtc_time(&rtc_tm);

		return 0;

	default:
		return -EINVAL;
	}
}

static int rtc_open(struct inode *inode, struct file *file)
{
	if (rtc_busy)
		return -EBUSY;

	rtc_busy = 1;

	MOD_INC_USE_COUNT;

	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	rtc_busy = 0;
	return 0;
}

static struct file_operations rtc_fops = {
	rtc_lseek,
	NULL,		/* rtc_read */
	NULL,		/* rtc_write */
	NULL,		/* rtc_readdir */
	NULL,		/* rtc_poll */
	rtc_ioctl,
	NULL,		/* rtc_mmap */
	rtc_open,
	NULL,		/* flush */
	rtc_release
};

static struct miscdevice rtc_dev = { RTC_MINOR, "rtc", &rtc_fops };

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
__initfunc(int rtc_init(void))
#endif
{
	int error;

	error = misc_register(&rtc_dev);
	if (error) {
		printk(KERN_ERR "rtc: unable to get misc minor\n");
		return error;
	}

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	misc_deregister(&rtc_dev);
}
#endif
