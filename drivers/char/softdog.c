/*
 *	SoftDog	0.02:	A Software Watchdog Device
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
 */
 
 
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mouse.h>
#define WATCHDOG_MINOR	130
#define TIMER_MARGIN	(60*HZ)		/* Allow 1 minute */

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
	hard_reset_now();
	printk("WATCHDOG: Reboot didn't ?????\n");
}

/*
 *	Allow only one person to hold it open
 */
 
static int softdog_open(struct inode *inode, struct file *file)
{
	if(timer_alive)
		return -EBUSY;
	/*
	 *	Activate timer
	 */
	watchdog_ticktock.expires=jiffies+TIMER_MARGIN;
	add_timer(&watchdog_ticktock);
	timer_alive++;
	return 0;
}

static void softdog_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 */
	del_timer(&watchdog_ticktock);
	timer_alive=0;
}

static int softdog_write(struct inode *inode, struct file *file, const char *data, int len)
{
	/*
	 *	Refresh the timer.
	 */
	del_timer(&watchdog_ticktock);
	watchdog_ticktock.expires=jiffies+TIMER_MARGIN;
	add_timer(&watchdog_ticktock);
	return 1;
}

/*
 *	The mouse stuff ought to be renamed misc_register etc before 1.4...
 */
 
void watchdog_init(void)
{
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
	static struct mouse softdog_mouse={
		WATCHDOG_MINOR,
		"softdog",
		&softdog_fops
	};

	mouse_register(&softdog_mouse);
	init_timer(&watchdog_ticktock);
	watchdog_ticktock.function=watchdog_fire;
	printk("Software Watchdog Timer: 0.03\n");
}	
