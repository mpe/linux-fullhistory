/*
 *	Industrial Computer Source WDT500/501 driver for Linux 2.1.x
 *
 *	(c) Copyright 1996-1997 Alan Cox <alan@cymru.net>, All Rights Reserved.
 *				http://www.cymru.net
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide 
 *	warranty for any of this software. This material is provided 
 *	"AS-IS" and at no charge.	
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Release 0.07.
 *
 *	Fixes
 *		Dave Gregorich	:	Modularisation and minor bugs
 *		Alan Cox	:	Added the watchdog ioctl() stuff
 *		Alan Cox	:	Fixed the reboot problem (as noted by
 *					Matt Crocker).
 *		Alan Cox	:	Added wdt= boot option
 *		Alan Cox	:	Cleaned up copy/user stuff
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include "wd501p.h"
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

static int wdt_is_open=0;

/*
 *	You must set these - there is no sane way to probe for this board.
 *	You can use wdt=x,y to set these now.
 */
 
static int io=0x240;
static int irq=11;

#define WD_TIMO (100*60)		/* 1 minute */

/*
 *	Setup options
 */
 
__initfunc(void wdt_setup(char *str, int *ints))
{
	if(ints[0]>0)
	{
		io=ints[1];
		if(ints[0]>1)
			irq=ints[2];
	}
}
 
/*
 *	Programming support
 */
 
static void wdt_ctr_mode(int ctr, int mode)
{
	ctr<<=6;
	ctr|=0x30;
	ctr|=(mode<<1);
	outb_p(ctr, WDT_CR);
}

static void wdt_ctr_load(int ctr, int val)
{
	outb_p(val&0xFF, WDT_COUNT0+ctr);
	outb_p(val>>8, WDT_COUNT0+ctr);
}

/*
 *	Kernel methods.
 */
 
static int wdt_status(void)
{
	/*
	 *	Status register to bit flags
	 */
	 
	int flag=0;
	unsigned char status=inb_p(WDT_SR);
	status|=FEATUREMAP1;
	status&=~FEATUREMAP2;	
	
	if(!(status&WDC_SR_TGOOD))
		flag|=WDIOF_OVERHEAT;
	if(!(status&WDC_SR_PSUOVER))
		flag|=WDIOF_POWEROVER;
	if(!(status&WDC_SR_PSUUNDR))
		flag|=WDIOF_POWERUNDER;
	if(!(status&WDC_SR_FANGOOD))
		flag|=WDIOF_FANFAULT;
	if(status&WDC_SR_ISOI0)
		flag|=WDIOF_EXTERN1;
	if(status&WDC_SR_ISII1)
		flag|=WDIOF_EXTERN2;
	return flag;
}

void wdt_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/*
	 *	Read the status register see what is up and
	 *	then printk it.
	 */
	 
	unsigned char status=inb_p(WDT_SR);
	
	status|=FEATUREMAP1;
	status&=~FEATUREMAP2;	
	
	printk(KERN_CRIT "WDT status %d\n", status);
	
	if(!(status&WDC_SR_TGOOD))
		printk(KERN_CRIT "Overheat alarm.(%d)\n",inb_p(WDT_RT));
	if(!(status&WDC_SR_PSUOVER))
		printk(KERN_CRIT "PSU over voltage.\n");
	if(!(status&WDC_SR_PSUUNDR))
		printk(KERN_CRIT "PSU under voltage.\n");
	if(!(status&WDC_SR_FANGOOD))
		printk(KERN_CRIT "Possible fan fault.\n");
	if(!(status&WDC_SR_WCCR))
#ifdef SOFTWARE_REBOOT
#ifdef ONLY_TESTING
		printk(KERN_CRIT "Would Reboot.\n");
#else		
		printk(KERN_CRIT "Initiating system reboot.\n");
		machine_restart(NULL);
#endif		
#else
		printk(KERN_CRIT "Reset in 5ms.\n");
#endif		
}


static long long wdt_llseek(struct file *file, long long offset, int origin)
{
	return -ESPIPE;
}

static void wdt_ping(void)
{
	/* Write a watchdog value */
	inb_p(WDT_DC);
	wdt_ctr_mode(1,2);
	wdt_ctr_load(1,WD_TIMO);		/* Timeout */
	outb_p(0, WDT_DC);
}

static ssize_t wdt_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if(count)
	{
		wdt_ping();
		return 1;
	}
	return 0;
}

/*
 *	Read reports the temperature in degrees Fahrenheit.
 */
 
static ssize_t wdt_read(struct file *file, char *buf, size_t count, loff_t *ptr)
{
	unsigned short c=inb_p(WDT_RT);
	unsigned char cp;
	
	/*  Can't seek (pread) on this device  */
	if (ptr != &file->f_pos)
		return -ESPIPE;

	switch(MINOR(file->f_dentry->d_inode->i_rdev))
	{
		case TEMP_MINOR:
			c*=11;
			c/=15;
			cp=c+7;
			if(copy_to_user(buf,&cp,1))
				return -EFAULT;
			return 1;
		default:
			return -EINVAL;
	}
}

static int wdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	static struct watchdog_info ident=
	{
		WDIOF_OVERHEAT|WDIOF_POWERUNDER|WDIOF_POWEROVER
			|WDIOF_EXTERN1|WDIOF_EXTERN2|WDIOF_FANFAULT,
		1,
		"WDT500/501"
	};
	
	ident.options&=WDT_OPTION_MASK;	/* Mask down to the card we have */
	switch(cmd)
	{
		default:
			return -ENOIOCTLCMD;
		case WDIOC_GETSUPPORT:
			return copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident))?-EFAULT:0;

		case WDIOC_GETSTATUS:
			return put_user(wdt_status(),(int *)arg);
		case WDIOC_GETBOOTSTATUS:
			return put_user(0, (int *)arg);
		case WDIOC_KEEPALIVE:
			wdt_ping();
			return 0;
	}
}

static int wdt_open(struct inode *inode, struct file *file)
{
	switch(MINOR(inode->i_rdev))
	{
		case WATCHDOG_MINOR:
			if(wdt_is_open)
				return -EBUSY;
			MOD_INC_USE_COUNT;
			/*
			 *	Activate 
			 */
	 
			wdt_is_open=1;
			inb_p(WDT_DC);		/* Disable */
			wdt_ctr_mode(0,3);
			wdt_ctr_mode(1,2);
			wdt_ctr_mode(2,0);
			wdt_ctr_load(0, 8948);		/* count at 100Hz */
			wdt_ctr_load(1,WD_TIMO);	/* Timeout 120 seconds */
			wdt_ctr_load(2,65535);
			outb_p(0, WDT_DC);	/* Enable */
			return 0;
		case TEMP_MINOR:
			MOD_INC_USE_COUNT;
			return 0;
		default:
			return -ENODEV;
	}
}

static int wdt_release(struct inode *inode, struct file *file)
{
	if(MINOR(inode->i_rdev)==WATCHDOG_MINOR)
	{
#ifndef CONFIG_WATCHDOG_NOWAYOUT	
		inb_p(WDT_DC);		/* Disable counters */
		wdt_ctr_load(2,0);	/* 0 length reset pulses now */
#endif		
		wdt_is_open=0;
	}
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Notifier for system down
 */

static int wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if(code==SYS_DOWN || code==SYS_HALT)
	{
		/* Turn the card off */
		inb_p(WDT_DC);
		wdt_ctr_load(2,0);
	}
	return NOTIFY_DONE;
}
 
/*
 *	Kernel Interfaces
 */
 
 
static struct file_operations wdt_fops = {
	wdt_llseek,
	wdt_read,
	wdt_write,
	NULL,		/* No Readdir */
	NULL,		/* No Select */
	wdt_ioctl,
	NULL,		/* No mmap */
	wdt_open,
	NULL,		/* flush */
	wdt_release
};

static struct miscdevice wdt_miscdev=
{
	WATCHDOG_MINOR,
	"watchdog",
	&wdt_fops
};

#ifdef CONFIG_WDT_501
static struct miscdevice temp_miscdev=
{
	TEMP_MINOR,
	"temperature",
	&wdt_fops
};
#endif

/*
 *	The WDT card needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off. 
 */
 
static struct notifier_block wdt_notifier=
{
	wdt_notify_sys,
	NULL,
	0
};

#ifdef MODULE

#define wdt_init init_module

void cleanup_module(void)
{
	misc_deregister(&wdt_miscdev);
#ifdef CONFIG_WDT_501	
	misc_deregister(&temp_miscdev);
#endif	
	unregister_reboot_notifier(&wdt_notifier);
	release_region(io,8);
	free_irq(irq, NULL);
}

#endif

__initfunc(int wdt_init(void))
{
	printk("WDT500/501-P driver 0.07 at %X (Interrupt %d)\n", io,irq);
	if(request_irq(irq, wdt_interrupt, SA_INTERRUPT, "wdt501p", NULL))
	{
		printk("IRQ %d is not free.\n", irq);
		return -EIO;
	}
	misc_register(&wdt_miscdev);
#ifdef CONFIG_WDT_501	
	misc_register(&temp_miscdev);
#endif	
	request_region(io, 8, "wdt501p");
	register_reboot_notifier(&wdt_notifier);
	return 0;
}

