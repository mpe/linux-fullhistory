/*
 *	Industrial Computer Source WDT500/501 driver for Linux 1.3.x
 *
 *	(c) Copyright 1995	CymruNET Ltd
 *				Innovation Centre
 *				Singleton Park
 *				Swansea
 *				Wales
 *				UK
 *				SA2 8PP
 *
 *	http://www.cymru.net
 *
 *	This driver is provided under the GNU public license, incorporated
 *	herein by reference. The driver is provided without warranty or 
 *	support.
 *
 *	Release 0.05.
 *
 *	Some changes by Dave Gregorich to fix modularisation and minor bugs.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include "wd501p.h"
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

static int wdt_is_open=0;

/*
 *	You must set these - there is no sane way to probe for this board.
 */
 
int io=0x240;
int irq=14;

#define WD_TIMO (100*60)		/* 1 minute */

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
 
static void wdt_interrupt(int irq, void *dev_id, struct pt_regs *regs)
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
		hard_reset_now();
#endif		
#else
		printk(KERN_CRIT "Reset in 5ms.\n");
#endif		
}


static int wdt_lseek(struct inode *inode, struct file *file, off_t offset, 
	int origin)
{
	return -ESPIPE;
}

static int wdt_write(struct inode *inode, struct file *file, const char *buf, int count)
{
	/* Write a watchdog value */
	inb_p(WDT_DC);
	wdt_ctr_mode(1,2);
	wdt_ctr_load(1,WD_TIMO);		/* Timeout */
	outb_p(0, WDT_DC);
	return count;
}

/*
 *	Read reports the temperature in farenheit
 */
 
static int wdt_read(struct inode *inode, struct file *file, char *buf, int count)
{
	unsigned short c=inb_p(WDT_RT);
	unsigned char cp;
	int err;
	
	switch(MINOR(inode->i_rdev))
	{
		case TEMP_MINOR:
			err=verify_area(VERIFY_WRITE, buf, 1);
			if(err)
				return err;
			c*=11;
			c/=15;
			cp=c+7;
			memcpy_tofs(buf,&cp,1);
			return 1;
		default:
			return -EINVAL;
	}
}

static int wdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	return -EINVAL;
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

static void wdt_release(struct inode *inode, struct file *file)
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
}

/*
 *	Kernel Interfaces
 */
 
 
static struct file_operations wdt_fops = {
	wdt_lseek,
	wdt_read,
	wdt_write,
	NULL,		/* No Readdir */
	NULL,		/* No Select */
	wdt_ioctl,
	NULL,		/* No mmap */
	wdt_open,
	wdt_release
};

static struct miscdevice wdt_miscdev=
{
	WATCHDOG_MINOR,
	"wdt",
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

#ifdef MODULE

int init_module(void)
{
	printk("WDT501-P module at %X(Interrupt %d)\n", io,irq);
	if(request_irq(irq, wdt_interrupt, SA_INTERRUPT, "wdt501p", NULL))
	{
		printk("IRQ %d is not free.\n", irq);
		return -EIO;
	}
	misc_register(&wdt_miscdev);
#ifdef CONFIG_WDT_501	
	misc_register(&temp_miscdev);
#endif	
	request_region(io, 8, "wdt501");
	return 0;
}

void cleanup_module(void)
{
	misc_deregister(&wdt_miscdev);
#ifdef CONFIG_WDT_501	
	misc_deregister(&temp_miscdev);
#endif	
	release_region(io,8);
	free_irq(irq, NULL);
}

#else

int wdt_init(void)
{
	printk("WDT500/501-P driver at %X(Interrupt %d)\n", io,irq);
	if(request_irq(irq, wdt_interrupt, SA_INTERRUPT, "wdt501p", NULL))
	{
		printk("IRQ %d is not free.\n", irq);
		return -EIO;
	}
	misc_register(&wdt_miscdev);
#ifdef CONFIG_WDT_501	
	misc_register(&temp_miscdev);
#endif	
	request_region(io, 8, "wdt501");
	return 0;
}

#endif
