/*
 *	Acquire Single Board Computer Watchdog Timer driver for Linux 2.1.x
 *
 *      Based on wdt.c. Original copyright messages:
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
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

static int acq_is_open=0;

/*
 *	You must set these - there is no sane way to probe for this board.
 */
 
#define WDT_STOP 0x43
#define WDT_START 0x443

#define WD_TIMO (100*60)		/* 1 minute */


/*
 *	Kernel methods.
 */
 

static void acq_ping(void)
{
	/* Write a watchdog value */
	inb_p(WDT_START);
}

static ssize_t acq_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	if(count)
	{
		acq_ping();
		return 1;
	}
	return 0;
}

static ssize_t acq_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}



static int acq_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	static struct watchdog_info ident=
	{
		WDIOF_KEEPALIVEPING, 1, "Acquire WDT"
	};
	
	switch(cmd)
	{
	case WDIOC_GETSUPPORT:
	  if (copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident)))
	    return -EFAULT;
	  break;
	  
	case WDIOC_GETSTATUS:
	  if (copy_to_user((int *)arg, &acq_is_open,  sizeof(int)))
	    return -EFAULT;
	  break;

	case WDIOC_KEEPALIVE:
	  acq_ping();
	  break;

	default:
	  return -ENOIOCTLCMD;
	}
	return 0;
}

static int acq_open(struct inode *inode, struct file *file)
{
	switch(MINOR(inode->i_rdev))
	{
		case WATCHDOG_MINOR:
			if(acq_is_open)
				return -EBUSY;
			MOD_INC_USE_COUNT;
			/*
			 *	Activate 
			 */
	 
			acq_is_open=1;
			inb_p(WDT_START);      
			return 0;
		default:
			return -ENODEV;
	}
}

static int acq_close(struct inode *inode, struct file *file)
{
	if(MINOR(inode->i_rdev)==WATCHDOG_MINOR)
	{
#ifndef CONFIG_WATCHDOG_NOWAYOUT	
		inb_p(WDT_STOP);
#endif		
		acq_is_open=0;
	}
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Notifier for system down
 */

static int acq_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if(code==SYS_DOWN || code==SYS_HALT)
	{
		/* Turn the card off */
		inb_p(WDT_STOP);
	}
	return NOTIFY_DONE;
}
 
/*
 *	Kernel Interfaces
 */
 
 
static struct file_operations acq_fops = {
	NULL,
	acq_read,
	acq_write,
	NULL,		/* No Readdir */
	NULL,		/* No Select */
	acq_ioctl,
	NULL,		/* No mmap */
	acq_open,
	NULL,		/* flush */
	acq_close
};

static struct miscdevice acq_miscdev=
{
	WATCHDOG_MINOR,
	"watchdog",
	&acq_fops
};


/*
 *	The WDT card needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off. 
 */
 
static struct notifier_block acq_notifier=
{
	acq_notify_sys,
	NULL,
	0
};

#ifdef MODULE

#define acq_init init_module

void cleanup_module(void)
{
	misc_deregister(&acq_miscdev);
	unregister_reboot_notifier(&acq_notifier);
	release_region(WDT_STOP,1);
	release_region(WDT_START,1);
}

#endif

__initfunc(int acq_init(void))
{
	printk("WDT driver for Acquire single board computer initialising.\n");

	misc_register(&acq_miscdev);
	request_region(WDT_STOP, 1, "Acquire WDT");
	request_region(WDT_START, 1, "Acquire WDT");
	unregister_reboot_notifier(&acq_notifier);
	return 0;
}

