/*
 * PCF8563 RTC
 *
 * From Phillips' datasheet:
 *
 * The PCF8563 is a CMOS real-time clock/calendar optimized for low power
 * consumption. A programmable clock output, interupt output and voltage
 * low detector are also provided. All address and data are transferred
 * serially via two-line bidirectional I2C-bus. Maximum bus speed is
 * 400 kbits/s. The built-in word address register is incremented
 * automatically after each written or read bute.
 *
 * Copyright (c) 2002, Axis Communications AB
 * All rights reserved.
 *
 * Author: Tobias Anderberg <tobiasa@axis.com>.
 *
 * $Id: pcf8563.c,v 1.1 2002/12/12 08:27:26 starvik Exp $
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/arch/svinto.h>
#include <asm/rtc.h>
#include "i2c.h"

#define PCF8563_MAJOR 121		/* Local major number. */
#define DEVICE_NAME "rtc"		/* Name which is registered in /proc/devices. */
#define PCF8563_NAME "PCF8563"
#define DRIVER_VERSION "$Revision: 1.1 $"

/* I2C bus slave registers. */
#define RTC_I2C_READ		0xa3
#define RTC_I2C_WRITE		0xa2

/* Two simple wrapper macros, saves a few keystrokes. */
#define rtc_read(x) i2c_readreg(RTC_I2C_READ, x)
#define rtc_write(x,y) i2c_writereg(RTC_I2C_WRITE, x, y)
	
static const unsigned char days_in_month[] =
	{ 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

int pcf8563_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
int pcf8563_open(struct inode *, struct file *);
int pcf8563_release(struct inode *, struct file *);

static struct file_operations pcf8563_fops = {
	.owner = THIS_MODULE,
	.ioctl = pcf8563_ioctl,
	.open = pcf8563_open,
	.release = pcf8563_release,
};

unsigned char
pcf8563_readreg(int reg) 
{
	unsigned char res = i2c_readreg(RTC_I2C_READ, reg);

	/* The PCF8563 does not return 0 for unimplemented bits */
	switch(reg)
	{
		case RTC_SECONDS:
		case RTC_MINUTES:
		     res &= 0x7f;
		     break;
		case RTC_HOURS:
		case RTC_DAY_OF_MONTH:
		     res &= 0x3f;
		     break;
		case RTC_MONTH:
		     res = (res & 0x1f) - 1;  /* PCF8563 returns month in range 1-12 */
		     break;
	}
	return res;
}

void
pcf8563_writereg(int reg, unsigned char val) 
{
	i2c_writereg(RTC_I2C_WRITE,reg,val);
}

void
get_rtc_time(struct rtc_time *tm)
{
	tm->tm_sec = rtc_read(RTC_SECONDS);
	tm->tm_min = rtc_read(RTC_MINUTES);
	tm->tm_hour = rtc_read(RTC_HOURS);
	tm->tm_mday = rtc_read(RTC_DAY_OF_MONTH);
	tm->tm_mon = rtc_read(RTC_MONTH);
	tm->tm_year = rtc_read(RTC_YEAR);

	if (tm->tm_sec & 0x80)
		printk(KERN_WARNING "%s: RTC Low Voltage - date/time is not reliable!\n", PCF8563_NAME);

	tm->tm_year = BCD_TO_BIN(tm->tm_year) + ((tm->tm_mon & 0x80) ? 100 : 0);
	tm->tm_sec &= 0x7f;
	tm->tm_min &= 0x7f;
	tm->tm_hour &= 0x3f;
	tm->tm_mday &= 0x3f;
	tm->tm_mon &= 0x1f;

	BCD_TO_BIN(tm->tm_sec);
	BCD_TO_BIN(tm->tm_min);
	BCD_TO_BIN(tm->tm_hour);
	BCD_TO_BIN(tm->tm_mday);
	BCD_TO_BIN(tm->tm_mon);
	tm->tm_mon--; /* Month is 1..12 in RTC but 0..11 in linux */
}

int __init
pcf8563_init(void)
{
	unsigned char ret;
	/*
	 * First of all we need to reset the chip. This is done by
	 * clearing control1, control2 and clk freq, clear the 
	 * Voltage Low bit, and resetting all alarms.
	 */
	if (rtc_write(RTC_CONTROL1, 0x00) < 0)
		goto err;

	if (rtc_write(RTC_CONTROL2, 0x00) < 0)
		goto err;

	if (rtc_write(RTC_CLOCKOUT_FREQ, 0x00) < 0)
		goto err;

	/* Clear the VL bit in the seconds register. */
	ret = rtc_read(RTC_SECONDS);
	
	if (rtc_write(RTC_SECONDS, (ret & 0x7f)) < 0)
		goto err;
		
	/* Reset the alarms. */
	if (rtc_write(RTC_MINUTE_ALARM, 0x00) < 0)
		goto err;
	
	if (rtc_write(RTC_HOUR_ALARM, 0x00) < 0)
		goto err;
	
	if (rtc_write(RTC_DAY_ALARM, 0x00) < 0)
		goto err;
	
	if (rtc_write(RTC_WEEKDAY_ALARM, 0x00) < 0)
		goto err;

	if (register_chrdev(PCF8563_MAJOR, DEVICE_NAME, &pcf8563_fops) < 0) {
		printk(KERN_INFO "%s: Unable to get major numer %d for RTC device.\n", 
		       PCF8563_NAME, PCF8563_MAJOR);
		return -1;
	}

	printk(KERN_INFO "%s Real-Time Clock Driver, %s\n", PCF8563_NAME, DRIVER_VERSION);
        
	/* Check for low voltage, and warn about it.. */
	if (rtc_read(RTC_SECONDS) & 0x80)
		printk(KERN_WARNING "%s: RTC Low Voltage - date/time is not reliable!\n", PCF8563_NAME);
	
	return 0;

err:
	printk(KERN_INFO "%s: Error initializing chip.\n", PCF8563_NAME);
	return -1;
}

void __exit
pcf8563_exit(void)
{
	if (unregister_chrdev(PCF8563_MAJOR, DEVICE_NAME) < 0) {
		printk(KERN_INFO "%s: Unable to unregister device.\n", PCF8563_NAME);
	}
}

/*
 * ioctl calls for this driver. Why return -ENOTTY upon error? Because
 * POSIX says so!
 */
int
pcf8563_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* Some sanity checks. */
	if (_IOC_TYPE(cmd) != RTC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > RTC_MAX_IOCTL)
		return -ENOTTY;

	switch (cmd) {
		case RTC_RD_TIME:
			{
				struct rtc_time tm;

				get_rtc_time(&tm);

				if (copy_to_user((struct rtc_time *) arg, &tm, sizeof(struct rtc_time))) {
					return -EFAULT;
				}

				return 0;
			}
			break;
		case RTC_SET_TIME:
			{
				int leap;
				int century;
				unsigned long flags;
				struct rtc_time tm;

				if (!capable(CAP_SYS_TIME))
					return -EPERM;

				if (copy_from_user(&tm, (struct rtc_time *) arg, sizeof(struct rtc_time)))
					return -EFAULT;

				/* Convert from struct tm to struct rtc_time. */
				tm.tm_year += 1900;
				tm.tm_mon += 1;
				
				leap = ((tm.tm_mon == 2) && ((tm.tm_year % 4) == 0)) ? 1 : 0;

				/* Perform some sanity checks. */
				if ((tm.tm_year < 1970) ||
				    (tm.tm_mon > 12) ||
				    (tm.tm_mday == 0) ||
				    (tm.tm_mday > days_in_month[tm.tm_mon] + leap) ||
				    (tm.tm_hour >= 24) ||
				    (tm.tm_min >= 60) ||
				    (tm.tm_sec >= 60))
					return -EINVAL;

				century = (tm.tm_year >= 2000) ? 0x80 : 0;
				tm.tm_year = tm.tm_year % 100;

				BIN_TO_BCD(tm.tm_year);
				BIN_TO_BCD(tm.tm_mday);
				BIN_TO_BCD(tm.tm_hour);
				BIN_TO_BCD(tm.tm_min);
				BIN_TO_BCD(tm.tm_sec);
				tm.tm_mon |= century;
				
				rtc_write(RTC_YEAR, tm.tm_year);
				rtc_write(RTC_MONTH, tm.tm_mon);
				rtc_write(RTC_DAY_OF_MONTH, tm.tm_mday);
				rtc_write(RTC_HOURS, tm.tm_hour);
				rtc_write(RTC_MINUTES, tm.tm_min);
				rtc_write(RTC_SECONDS, tm.tm_sec);

				return 0;
			}
			break;
		default:
				return -ENOTTY;
	}

	return 0;
}

int 
pcf8563_open(struct inode *inode, struct file *filp)
{
	MOD_INC_USE_COUNT;
	return 0;
}

int
pcf8563_release(struct inode *inode, struct file *filp)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

module_init(pcf8563_init);
module_exit(pcf8563_exit);
