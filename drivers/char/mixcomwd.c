/*
 * MixCom Watchdog: A Simple Hardware Watchdog Device
 * Based on Softdog driver by Alan Cox and PC Watchdog driver by Ken Hollis
 *
 * Author: Gergely Madarasz <gorgo@itc.hu>
 *
 * Copyright (c) 1999 ITConsult-Pro Co. <info@itc.hu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Version 0.1 (99/04/15):
 *		- first version
 *
 * Version 0.2 (99/06/16):
 *		- added kernel timer watchdog ping after close
 *		  since the hardware does not support watchdog shutdown
 *
 * Version 0.3 (99/06/21):
 *		- added WDIOC_GETSTATUS and WDIOC_GETSUPPORT ioctl calls
 *
 * Version 0.3.1 (99/06/22):
 *		- allow module removal while internal timer is active,
 *		  print warning about probable reset
 *	
 */

#define VERSION "0.3.1" 
  
#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/io.h>

static int mixcomwd_ioports[] = { 0x180, 0x280, 0x380, 0x000 };

#define MIXCOM_WATCHDOG_OFFSET 0xc10
#define MIXCOM_ID1 0x11
#define MIXCOM_ID2 0x13

static int mixcomwd_opened;
static int mixcomwd_port;

#ifndef CONFIG_WATCHDOG_NOWAYOUT
static int mixcomwd_timer_alive;
static struct timer_list mixcomwd_timer;
#endif

static void mixcomwd_ping(void)
{
	outb_p(55,mixcomwd_port+MIXCOM_WATCHDOG_OFFSET);
	return;
}

#ifndef CONFIG_WATCHDOG_NOWAYOUT
static void mixcomwd_timerfun(unsigned long d)
{
	mixcomwd_ping();
	
	mod_timer(&mixcomwd_timer,jiffies+ 5*HZ);
}
#endif

/*
 *	Allow only one person to hold it open
 */
 
static int mixcomwd_open(struct inode *inode, struct file *file)
{
	if(test_and_set_bit(0,&mixcomwd_opened)) {
		return -EBUSY;
	}
	mixcomwd_ping();
	
#ifndef CONFIG_WATCHDOG_NOWAYOUT
	if(mixcomwd_timer_alive) {
		del_timer(&mixcomwd_timer);
		mixcomwd_timer_alive=0;
	} 
#endif
	MOD_INC_USE_COUNT;

	return 0;
}

static int mixcomwd_release(struct inode *inode, struct file *file)
{

#ifndef CONFIG_WATCHDOG_NOWAYOUT
	if(mixcomwd_timer_alive) {
		printk(KERN_ERR "mixcomwd: release called while internal timer alive");
		return -EBUSY;
	}
	init_timer(&mixcomwd_timer);
	mixcomwd_timer.expires=jiffies + 5 * HZ;
	mixcomwd_timer.function=mixcomwd_timerfun;
	mixcomwd_timer.data=0;
	mixcomwd_timer_alive=1;
	add_timer(&mixcomwd_timer);
#endif
	MOD_DEC_USE_COUNT;

	clear_bit(0,&mixcomwd_opened);
	return 0;
}


static ssize_t mixcomwd_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	if (ppos != &file->f_pos) {
		return -ESPIPE;
	}

	if(len)
	{
		mixcomwd_ping();
		return 1;
	}
	return 0;
}

static int mixcomwd_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int status;
        static struct watchdog_info ident = {
		WDIOF_KEEPALIVEPING, 1, "MixCOM watchdog"
	};
                                        
	switch(cmd)
	{
		case WDIOC_GETSTATUS:
			status=mixcomwd_opened;
#ifndef CONFIG_WATCHDOG_NOWAYOUT
			status|=mixcomwd_timer_alive;
#endif
			if (copy_to_user((int *)arg, &status, sizeof(int))) {
				return -EFAULT;
			}
			break;
		case WDIOC_GETSUPPORT:
			if (copy_to_user((struct watchdog_info *)arg, &ident, 
			    sizeof(ident))) {
				return -EFAULT;
			}
			break;
		case WDIOC_KEEPALIVE:
			mixcomwd_ping();
			break;
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static struct file_operations mixcomwd_fops=
{
	NULL,		/* Seek */
	NULL,		/* Read */
	mixcomwd_write,	/* Write */
	NULL,		/* Readdir */
	NULL,		/* Select */
	mixcomwd_ioctl,	/* Ioctl */
	NULL,		/* MMap */
	mixcomwd_open,
	NULL,		/* flush */
	mixcomwd_release,
	NULL,		
	NULL		/* Fasync */
};

static struct miscdevice mixcomwd_miscdev=
{
	WATCHDOG_MINOR,
	"watchdog",
	&mixcomwd_fops
};

static int __init mixcomwd_checkcard(int port)
{
	int id;

	if(check_region(port,1)) {
		return 0;
	}
	
	id=inb_p(port + MIXCOM_WATCHDOG_OFFSET) & 0x3f;
	if(id!=MIXCOM_ID1 && id!=MIXCOM_ID2) {
		return 0;
	}
	return 1;
}


void __init mixcomwd_init(void)
{
	int i;
	int found=0;

	for (i = 0; mixcomwd_ioports[i] != 0; i++) {
		if (mixcomwd_checkcard(mixcomwd_ioports[i])) {
			found = 1;
			mixcomwd_port = mixcomwd_ioports[i];
			break;
		}
	}

	if (!found) {
		printk("mixcomwd: No card detected, or port not available.\n");
		return;
	}

	request_region(mixcomwd_port+MIXCOM_WATCHDOG_OFFSET,1,"MixCOM watchdog");
	
	misc_register(&mixcomwd_miscdev);
	printk("MixCOM watchdog driver v%s, MixCOM card at 0x%3x\n",VERSION,mixcomwd_port);
}	

#ifdef MODULE
int init_module(void)
{
	mixcomwd_init();
	return 0;
}

void cleanup_module(void)
{
#ifndef CONFIG_WATCHDOG_NOWAYOUT
	if(mixcomwd_timer_alive) {
		printk(KERN_WARNING "mixcomwd: I quit now, hardware will"
			" probably reboot!\n");
		del_timer(&mixcomwd_timer);
		mixcomwd_timer_alive=0;
	}
#endif
	release_region(mixcomwd_port+MIXCOM_WATCHDOG_OFFSET,1);
	misc_deregister(&mixcomwd_miscdev);
}
#endif
