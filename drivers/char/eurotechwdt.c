/*
 *	Eurotech CPU-1220/1410 on board WDT driver for Linux 2.4.x
 *
 *	(c) Copyright 2001 Ascensit <support@ascensit.com>
 *	(c) Copyright 2001 Rodolfo Giometti <giometti@ascensit.com>
 *
 *	Based on wdt.c.
 *	Original copyright messages:
 *
 *      (c) Copyright 1996-1997 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *                              http://www.redhat.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      Neither Alan Cox nor CymruNet Ltd. admit liability nor provide
 *      warranty for any of this software. This material is provided
 *      "AS-IS" and at no charge.
 *
 *      (c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>*
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>

static int eurwdt_is_open;
static int eurwdt_timeout; 
static spinlock_t eurwdt_lock;
 
/*
 *      You must set these - there is no sane way to probe for this board.
 *      You can use wdt=x,y to set these now.
 */
 
static int io = 0x3f0;
static int irq = 10;
static char *ev = "int";
 
#define WDT_TIMEOUT		60                /* 1 minute */


/*
 * Some symbolic names 
 */

#define WDT_CTRL_REG		0x30
#define WDT_OUTPIN_CFG		0xe2
   #define WDT_EVENT_INT	   0x00
   #define WDT_EVENT_REBOOT	   0x08
#define WDT_UNIT_SEL		0xf1
   #define WDT_UNIT_SECS	   0x80
#define WDT_TIMEOUT_VAL		0xf2
#define WDT_TIMER_CFG		0xf3
 

#ifndef MODULE

/**
 *      eurwdt_setup:
 *      @str: command line string
 *
 *      Setup options. The board isn't really probe-able so we have to
 *      get the user to tell us the configuration. Sane people build it
 *      modular but the others come here.
 */
 
static int __init eurwdt_setup(char *str)
{
   int ints[4];
 
   str = get_options (str, ARRAY_SIZE(ints), ints);
 
   if (ints[0] > 0) {
      io = ints[1];
      if (ints[0] > 1)
         irq = ints[2];
   }
 
   return 1;
}
 
__setup("wdt=", eurwdt_setup);

#endif /* !MODULE */
 
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "Eurotech WDT io port (default=0x3f0)");
MODULE_PARM(irq, "i");
MODULE_PARM_DESC(irq, "Eurotech WDT irq (default=10)");
MODULE_PARM(ev, "s");
MODULE_PARM_DESC(ev, "Eurotech WDT event type (default is `reboot')");


/*
 *      Programming support
 */

static inline void eurwdt_write_reg(u8 index, u8 data)
{
   outb(index, io);
   outb(data, io+1);
}

static inline void eurwdt_lock_chip(void)
{
   outb(0xaa, io);
}

static inline void eurwdt_unlock_chip(void)
{
   outb(0x55, io);
   eurwdt_write_reg(0x07, 0x08);   /* set the logical device */
}

static inline void eurwdt_set_timeout(int timeout)
{
   eurwdt_write_reg(WDT_TIMEOUT_VAL, (u8) timeout);
}

static inline void eurwdt_disable_timer(void)
{
   eurwdt_set_timeout(0);
}
 
static void eurwdt_activate_timer(void)
{
   eurwdt_disable_timer();
   eurwdt_write_reg(WDT_CTRL_REG, 0x01);      /* activate the WDT */
   eurwdt_write_reg(WDT_OUTPIN_CFG, !strcmp("int", ev) ?
                                    WDT_EVENT_INT : WDT_EVENT_REBOOT);
   /* Setting interrupt line */
   if (irq == 2 || irq > 15 || irq < 0) {
      printk(KERN_ERR ": invalid irq number\n");
      irq = 0;   /* if invalid we disable interrupt */
   }
   if (irq == 0)
      printk(KERN_INFO ": interrupt disabled\n");
   eurwdt_write_reg(WDT_TIMER_CFG, irq<<4);

   eurwdt_write_reg(WDT_UNIT_SEL, WDT_UNIT_SECS);   /* we use seconds */
   eurwdt_set_timeout(0);                           /* the default timeout */ 
}


/*
 *      Kernel methods.
 */
 
void eurwdt_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
   printk(KERN_CRIT "timeout WDT timeout\n");
 
#ifdef ONLY_TESTING
   printk(KERN_CRIT "Would Reboot.\n");
#else
   printk(KERN_CRIT "Initiating system reboot.\n");
   machine_restart(NULL);
#endif
}


/**
 *      eurwdt_ping:
 *
 *      Reload counter one with the watchdog timeout.
 */
 
static void eurwdt_ping(void)
{
   /* Write the watchdog default value */
   eurwdt_set_timeout(eurwdt_timeout);
}
 
/**
 *      eurwdt_write:
 *      @file: file handle to the watchdog
 *      @buf: buffer to write (unused as data does not matter here
 *      @count: count of bytes
 *      @ppos: pointer to the position to write. No seeks allowed
 *
 *      A write to a watchdog device is defined as a keepalive signal. Any
 *      write of data will do, as we we don't define content meaning.
 */
 
static ssize_t eurwdt_write(struct file *file, const char *buf, size_t count,
loff_t *ppos)
{
   /*  Can't seek (pwrite) on this device  */
   if (ppos != &file->f_pos)
      return -ESPIPE;
 
   if (count) {
      eurwdt_ping();   /* the default timeout */
      return 1;
   }

   return 0;
}

/**
 *      eurwdt_ioctl:
 *      @inode: inode of the device
 *      @file: file handle to the device
 *      @cmd: watchdog command
 *      @arg: argument pointer
 *
 *      The watchdog API defines a common set of functions for all watchdogs
 *      according to their available features.
 */
 
static int eurwdt_ioctl(struct inode *inode, struct file *file,
        unsigned int cmd, unsigned long arg)
{
   static struct watchdog_info ident = {
      options		: WDIOF_CARDRESET,
      firmware_version	: 1,
      identity		: "WDT Eurotech CPU-1220/1410"
   };

   int time;
 
   switch(cmd) {
      default:
         return -ENOTTY;

      case WDIOC_GETSUPPORT:
         return copy_to_user((struct watchdog_info *)arg, &ident,
               sizeof(ident)) ? -EFAULT : 0;
 
      case WDIOC_GETBOOTSTATUS:
         return put_user(0, (int *) arg);

      case WDIOC_KEEPALIVE:
         eurwdt_ping();
         return 0;

      case WDIOC_SETTIMEOUT:
         if (copy_from_user(&time, (int *) arg, sizeof(int)))
            return -EFAULT;

         /* Sanity check */
         if (time < 0 || time > 255)
            return -EINVAL;

         eurwdt_timeout = time; 
         eurwdt_set_timeout(time); 
         return 0;
   }
}

/**
 *      eurwdt_open:
 *      @inode: inode of device
 *      @file: file handle to device
 *
 *      The misc device has been opened. The watchdog device is single
 *      open and on opening we load the counter.
 */
 
static int eurwdt_open(struct inode *inode, struct file *file)
{
   switch (MINOR(inode->i_rdev)) {
      case WATCHDOG_MINOR:
         spin_lock(&eurwdt_lock);
         if (eurwdt_is_open) {
            spin_unlock(&eurwdt_lock);
            return -EBUSY;
         }

         eurwdt_is_open = 1;
         eurwdt_timeout = WDT_TIMEOUT;   /* initial timeout */

         /* Activate the WDT */
         eurwdt_activate_timer(); 
            
         spin_unlock(&eurwdt_lock);

         MOD_INC_USE_COUNT;

         return 0;

         case TEMP_MINOR:
            return 0;

         default:
            return -ENODEV;
   }
}
 
/**
 *      eurwdt_release:
 *      @inode: inode to board
 *      @file: file handle to board
 *
 *      The watchdog has a configurable API. There is a religious dispute
 *      between people who want their watchdog to be able to shut down and
 *      those who want to be sure if the watchdog manager dies the machine
 *      reboots. In the former case we disable the counters, in the latter
 *      case you have to open it again very soon.
 */
 
static int eurwdt_release(struct inode *inode, struct file *file)
{
   if (MINOR(inode->i_rdev) == WATCHDOG_MINOR) {
#ifndef CONFIG_WATCHDOG_NOWAYOUT
      eurwdt_disable_timer();
#endif
      eurwdt_is_open = 0;

      MOD_DEC_USE_COUNT;
   }

   return 0;
}
 
/**
 *      eurwdt_notify_sys:
 *      @this: our notifier block
 *      @code: the event being reported
 *      @unused: unused
 *
 *      Our notifier is called on system shutdowns. We want to turn the card
 *      off at reboot otherwise the machine will reboot again during memory
 *      test or worse yet during the following fsck. This would suck, in fact
 *      trust me - if it happens it does suck.
 */
 
static int eurwdt_notify_sys(struct notifier_block *this, unsigned long code,
        void *unused)
{
   if (code == SYS_DOWN || code == SYS_HALT) {
      /* Turn the card off */
      eurwdt_disable_timer();
   }

   return NOTIFY_DONE;
}
 
/*
 *      Kernel Interfaces
 */
 
 
static struct file_operations eurwdt_fops = {
        owner:          THIS_MODULE,
        llseek:         no_llseek,
        write:          eurwdt_write,
        ioctl:          eurwdt_ioctl,
        open:           eurwdt_open,
        release:        eurwdt_release,
};

static struct miscdevice eurwdt_miscdev =
{
        WATCHDOG_MINOR,
        "watchdog",
        &eurwdt_fops
};
 
/*
 *      The WDT card needs to learn about soft shutdowns in order to
 *      turn the timebomb registers off.
 */
 
static struct notifier_block eurwdt_notifier =
{
        eurwdt_notify_sys,
        NULL,
        0
};
 
/**
 *      cleanup_module:
 *
 *      Unload the watchdog. You cannot do this with any file handles open.
 *      If your watchdog is set to continue ticking on close and you unload
 *      it, well it keeps ticking. We won't get the interrupt but the board
 *      will not touch PC memory so all is fine. You just have to load a new
 *      module in 60 seconds or reboot.
 */
 
static void __exit eurwdt_exit(void)
{
   eurwdt_lock_chip();

   misc_deregister(&eurwdt_miscdev);

   unregister_reboot_notifier(&eurwdt_notifier);
   release_region(io, 2);
   free_irq(irq, NULL);
}
 
/**
 *      eurwdt_init:
 *
 *      Set up the WDT watchdog board. After grabbing the resources 
 *      we require we need also to unlock the device.
 *      The open() function will actually kick the board off.
 */
 
static int __init eurwdt_init(void)
{
   int ret;
 
   ret = misc_register(&eurwdt_miscdev);
   if (ret) {
      printk(KERN_ERR "eurwdt: can't misc_register on minor=%d\n",
            WATCHDOG_MINOR);
      goto out;
   }

   ret = request_irq(irq, eurwdt_interrupt, SA_INTERRUPT, "eurwdt", NULL);
   if(ret) {
      printk(KERN_ERR "eurwdt: IRQ %d is not free.\n", irq);
      goto outmisc;
   }

   if (!request_region(io, 2, "eurwdt")) {
       printk(KERN_ERR "eurwdt: IO %X is not free.\n", io);
       ret = -EBUSY;
       goto outirq;
   }

   ret = register_reboot_notifier(&eurwdt_notifier);
   if (ret) {
      printk(KERN_ERR "eurwdt: can't register reboot notifier (err=%d)\n", ret);
      goto outreg;
   }

   eurwdt_unlock_chip();
 
   ret = 0;
   printk(KERN_INFO "Eurotech WDT driver 0.01 at %X (Interrupt %d)"
                    " - timeout event: %s\n", 
         io, irq, (!strcmp("int", ev) ? "int" : "reboot"));

   spin_lock_init(&eurwdt_lock);

   out:
      return ret;
 
   outreg:
      release_region(io, 2);

   outirq:
      free_irq(irq, NULL);

   outmisc:
      misc_deregister(&eurwdt_miscdev);
      goto out;
}
 
module_init(eurwdt_init);
module_exit(eurwdt_exit);
 
MODULE_AUTHOR("Rodolfo Giometti");
MODULE_DESCRIPTION("Driver for Eurotech CPU-1220/1410 on board watchdog");
MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;
