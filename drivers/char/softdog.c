/*
 *	SoftDog	0.05:	A Software Watchdog Device
 *
 *	(c) Copyright 1996 Alan Cox <alan@cymru.net>, All Rights Reserved.
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
 *	Software only watchdog driver. Unlike its big brother the WDT501P
 *	driver this won't always recover a failed machine.
 *
 *  03/96: Angelo Haritsis <ah@doc.ic.ac.uk> :
 *	Modularised.
 *	Added soft_margin; use upon insmod to change the timer delay.
 *	NB: uses same minor as wdt (WATCHDOG_MINOR); we could use separate
 *	    minors.
 */
 
#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#define TIMER_MARGIN	60		/* (secs) Default is 1 minute */

static int soft_margin = TIMER_MARGIN;	/* in seconds */

#ifdef MODULE
MODULE_PARM(soft_margin,"i");
#endif

/*
 *	Our timer
 */
 
struct timer_list watchdog_ticktock;
static int timer_alive = 0;


/*
 *	If the timer expires..
 */
 
static void watchdog_fire(unsigned long data)
{
#ifdef ONLY_TESTING
		printk(KERN_CRIT "SOFTDOG: Would Reboot.\n");
#else
	printk(KERN_CRIT "SOFTDOG: Initiating system reboot.\n");
	machine_restart(NULL);
	printk("WATCHDOG: Reboot didn't ?????\n");
#endif
}

/*
 *	Allow only one person to hold it open
 */
 
static int softdog_open(struct inode *inode, struct file *file)
{
	if(timer_alive)
		return -EBUSY;
	MOD_INC_USE_COUNT;
	/*
	 *	Activate timer
	 */
	del_timer(&watchdog_ticktock);
	watchdog_ticktock.expires=jiffies + (soft_margin * HZ);
	add_timer(&watchdog_ticktock);
	timer_alive=1;
	return 0;
}

static int softdog_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we defined ...NOWAYOUT
	 */
#ifndef CONFIG_WATCHDOG_NOWAYOUT	 
	del_timer(&watchdog_ticktock);
	MOD_DEC_USE_COUNT;
#endif	
	timer_alive=0;
	return 0;
}

static void softdog_ping(void)
{
	/*
	 *	Refresh the timer.
	 */
	del_timer(&watchdog_ticktock);
	watchdog_ticktock.expires=jiffies + (soft_margin * HZ);
	add_timer(&watchdog_ticktock);
	return;
}

static ssize_t softdog_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	/*
	 *	Refresh the timer.
	 */
	if(len)
	{
		softdog_ping();
		return 1;
	}
	return 0;
}

static int softdog_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int i;
	static struct watchdog_info ident=
	{
		0,
		0,
		"Software Watchdog"
	};
	switch(cmd)
	{
		default:
			return -ENOIOCTLCMD;
		case WDIOC_GETSUPPORT:
			i = verify_area(VERIFY_WRITE, (void*) arg, sizeof(struct watchdog_info));
			if (i)
				return i;
			else
				return copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident));
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0,(int *)arg);
		case WDIOC_KEEPALIVE:
			softdog_ping();
			return 0;
	}
}

static struct file_operations softdog_fops=
{
	NULL,		/* Seek */
	NULL,		/* Read */
	softdog_write,	/* Write */
	NULL,		/* Readdir */
	NULL,		/* Select */
	softdog_ioctl,	/* Ioctl */
	NULL,		/* MMap */
	softdog_open,
	NULL,		/* flush */
	softdog_release,
	NULL,		
	NULL		/* Fasync */
};

static struct miscdevice softdog_miscdev=
{
	WATCHDOG_MINOR,
	"watchdog",
	&softdog_fops
};

__initfunc(void watchdog_init(void))
{
	misc_register(&softdog_miscdev);
	init_timer(&watchdog_ticktock);
	watchdog_ticktock.function=watchdog_fire;
	printk("Software Watchdog Timer: 0.05, timer margin: %d sec\n", soft_margin);
}	

#ifdef MODULE
int init_module(void)
{
	watchdog_init();
	return 0;
}

void cleanup_module(void)
{
	misc_deregister(&softdog_miscdev);
}
#endif
