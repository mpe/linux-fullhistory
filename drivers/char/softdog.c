/*
 *	SoftDog	0.04:	A Software Watchdog Device
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Email us for quotes on Linux software and driver development. 
 *
 *			-----------------------
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *			-----------------------
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

#define WATCHDOG_MINOR	130
#define TIMER_MARGIN	60		/* (secs) Default is 1 minute */

static int soft_margin = TIMER_MARGIN;	/* in seconds */

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
	extern void hard_reset_now(void);
#ifdef ONLY_TESTING
		printk(KERN_CRIT "SOFTDOG: Would Reboot.\n");
#else
	printk(KERN_CRIT "SOFTDOG: Initiating system reboot.\n");
	hard_reset_now();
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
	timer_alive++;
	return 0;
}

static void softdog_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we defined ...NOWAYOUT
	 */
#ifndef CONFIG_WATCHDOG_NOWAYOUT	 
	del_timer(&watchdog_ticktock);
	MOD_DEC_USE_COUNT;
	timer_alive=0;
#endif	
}

static int softdog_write(struct inode *inode, struct file *file, const char *data, int len)
{
	/*
	 *	Refresh the timer.
	 */
	del_timer(&watchdog_ticktock);
	watchdog_ticktock.expires=jiffies + (soft_margin * HZ);
	add_timer(&watchdog_ticktock);
	return 1;
}

static struct file_operations softdog_fops=
{
	NULL,		/* Seek */
	NULL,		/* Read */
	softdog_write,	/* Write */
	NULL,		/* Readdir */
	NULL,		/* Select */
	NULL,		/* Ioctl */
	NULL,		/* MMap */
	softdog_open,
	softdog_release,
	NULL,		
	NULL		/* Fasync */
};

static struct miscdevice softdog_miscdev=
{
	WATCHDOG_MINOR,
	"softdog",
	&softdog_fops
};

void watchdog_init(void)
{
	misc_register(&softdog_miscdev);
	init_timer(&watchdog_ticktock);
	watchdog_ticktock.function=watchdog_fire;
	printk("Software Watchdog Timer: 0.04, timer margin: %d sec\n", soft_margin);
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
