/*
 *	Real Time Clock interface for Linux	
 *
 *	Copyright (C) 1996 Paul Gortmaker
 *
 *	This driver allows use of the real time clock (built into
 *	nearly all computers) from user space. It exports the /dev/rtc
 *	interface supporting various ioctl() and also the
 *	/proc/driver/rtc pseudo-file for status information.
 *
 *	The ioctls can be used to set the interrupt behaviour and
 *	generation rate from the RTC via IRQ 8. Then the /dev/rtc
 *	interface can be used to make use of these timer interrupts,
 *	be they interval or alarm based.
 *
 *	The /dev/rtc interface will block on reads until an interrupt
 *	has been received. If a RTC interrupt has already happened,
 *	it will output an unsigned long and then block. The output value
 *	contains the interrupt status in the low byte and the number of
 *	interrupts since the last read in the remaining high bytes. The 
 *	/dev/rtc interface can also be used with the select(2) call.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Based on other minimal char device drivers, like Alan's
 *	watchdog, Ted's random, etc. etc.
 *
 *	1.07	Paul Gortmaker.
 *	1.08	Miquel van Smoorenburg: disallow certain things on the
 *		DEC Alpha as the CMOS clock is also used for other things.
 *	1.09	Nikita Schmidt: epoch support and some Alpha cleanup.
 *	1.09a	Pete Zaitcev: Sun SPARC
 *	1.09b	Jeff Garzik: Modularize, init cleanup
 *	1.09c	Jeff Garzik: SMP cleanup
 *	1.10    Paul Barton-Davis: add support for async I/O
 *	1.10a	Andrea Arcangeli: Alpha updates
 *	1.10b	Andrew Morton: SMP lock fix
 */

#define RTC_VERSION		"1.10b"

#define RTC_IRQ 	8	/* Can't see this changing soon.	*/
#define RTC_IO_EXTENT	0x10	/* Only really two ports, but...	*/

/*
 *	Note that *all* calls to CMOS_READ and CMOS_WRITE are done with
 *	interrupts disabled. Due to the index-port/data-port (0x70/0x71)
 *	design of the RTC, we don't want two different things trying to
 *	get to it at once. (e.g. the periodic 11 min sync from time.c vs.
 *	this driver.)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#ifdef __sparc__
#include <asm/ebus.h>

static unsigned long rtc_port;
static int rtc_irq;
#endif

/*
 *	We sponge a minor off of the misc major. No need slurping
 *	up another valuable major dev number for this. If you add
 *	an ioctl, make sure you don't conflict with SPARC's RTC
 *	ioctls.
 */

static struct fasync_struct *rtc_async_queue;

static DECLARE_WAIT_QUEUE_HEAD(rtc_wait);

static spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;

static struct timer_list rtc_irq_timer;

static long long rtc_llseek(struct file *file, loff_t offset, int origin);

static ssize_t rtc_read(struct file *file, char *buf,
			size_t count, loff_t *ppos);

static int rtc_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg);

#ifndef __alpha__
static unsigned int rtc_poll(struct file *file, poll_table *wait);
#endif

static void get_rtc_time (struct rtc_time *rtc_tm);
static void get_rtc_alm_time (struct rtc_time *alm_tm);
#ifndef __alpha__
static void rtc_dropped_irq(unsigned long data);

static void set_rtc_irq_bit(unsigned char bit);
static void mask_rtc_irq_bit(unsigned char bit);
#endif

static inline unsigned char rtc_is_updating(void);

static int rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data);

/*
 *	Bits in rtc_status. (6 bits of room for future expansion)
 */

#define RTC_IS_OPEN		0x01	/* means /dev/rtc is in use	*/
#define RTC_TIMER_ON		0x02	/* missed irq timer active	*/

static atomic_t rtc_status = ATOMIC_INIT(0); /* bitmapped status byte.	*/
static unsigned long rtc_freq = 0;	/* Current periodic IRQ rate	*/
static unsigned long rtc_irq_data = 0;	/* our output to the world	*/

/*
 *	If this driver ever becomes modularised, it will be really nice
 *	to make the epoch retain its value across module reload...
 */

static unsigned long epoch = 1900;	/* year corresponding to 0x00	*/

static const unsigned char days_in_mo[] = 
{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#ifndef __alpha__
/*
 *	A very tiny interrupt handler. It runs with SA_INTERRUPT set,
 *	so that there is no possibility of conflicting with the
 *	set_rtc_mmss() call that happens during some timer interrupts.
 *	(See ./arch/XXXX/kernel/time.c for the set_rtc_mmss() function.)
 */

static void rtc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/*
	 *	Can be an alarm interrupt, update complete interrupt,
	 *	or a periodic interrupt. We store the status in the
	 *	low byte and the number of interrupts received since
	 *	the last read in the remainder of rtc_irq_data.
	 */

	spin_lock (&rtc_lock);
	rtc_irq_data += 0x100;
	rtc_irq_data &= ~0xff;
	rtc_irq_data |= (CMOS_READ(RTC_INTR_FLAGS) & 0xF0);
	spin_unlock (&rtc_lock);

	wake_up_interruptible(&rtc_wait);	

	if (rtc_async_queue)
		kill_fasync (rtc_async_queue, SIGIO, POLL_IN);

	if (atomic_read(&rtc_status) & RTC_TIMER_ON)
		mod_timer(&rtc_irq_timer, jiffies + HZ/rtc_freq + 2*HZ/100);
}
#endif

/*
 *	Now all the various file operations that we export.
 */

static long long rtc_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t rtc_read(struct file *file, char *buf,
			size_t count, loff_t *ppos)
{
#ifdef __alpha__
	return -EIO;
#else
	DECLARE_WAITQUEUE(wait, current);
	unsigned long data;
	ssize_t retval;
	
	if (count < sizeof(unsigned long))
		return -EINVAL;

	add_wait_queue(&rtc_wait, &wait);

	current->state = TASK_INTERRUPTIBLE;
		
	while ((data = xchg(&rtc_irq_data, 0)) == 0) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
		schedule();
	}

	retval = put_user(data, (unsigned long *)buf); 
	if (!retval)
		retval = sizeof(unsigned long); 
 out:
	current->state = TASK_RUNNING;
	remove_wait_queue(&rtc_wait, &wait);

	return retval;
#endif
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{

	unsigned long flags;
	struct rtc_time wtime; 

	switch (cmd) {
#ifndef __alpha__
	case RTC_AIE_OFF:	/* Mask alarm int. enab. bit	*/
	{
		mask_rtc_irq_bit(RTC_AIE);
		return 0;
	}
	case RTC_AIE_ON:	/* Allow alarm interrupts.	*/
	{
		set_rtc_irq_bit(RTC_AIE);
		return 0;
	}
	case RTC_PIE_OFF:	/* Mask periodic int. enab. bit	*/
	{
		mask_rtc_irq_bit(RTC_PIE);
		if (atomic_read(&rtc_status) & RTC_TIMER_ON) {
			del_timer(&rtc_irq_timer);
			atomic_set(&rtc_status,
				   atomic_read(&rtc_status) & ~RTC_TIMER_ON);
		}
		return 0;
	}
	case RTC_PIE_ON:	/* Allow periodic ints		*/
	{

		/*
		 * We don't really want Joe User enabling more
		 * than 64Hz of interrupts on a multi-user machine.
		 */
		if ((rtc_freq > 64) && (!capable(CAP_SYS_RESOURCE)))
			return -EACCES;

		if (!(atomic_read(&rtc_status) & RTC_TIMER_ON)) {
			atomic_set(&rtc_status,
				   atomic_read(&rtc_status) | RTC_TIMER_ON);
			rtc_irq_timer.expires = jiffies + HZ/rtc_freq + 2*HZ/100;
			add_timer(&rtc_irq_timer);
		}
		set_rtc_irq_bit(RTC_PIE);
		return 0;
	}
	case RTC_UIE_OFF:	/* Mask ints from RTC updates.	*/
	{
		mask_rtc_irq_bit(RTC_UIE);
		return 0;
	}
	case RTC_UIE_ON:	/* Allow ints for RTC updates.	*/
	{
		set_rtc_irq_bit(RTC_UIE);
		return 0;
	}
#endif
	case RTC_ALM_READ:	/* Read the present alarm time */
	{
		/*
		 * This returns a struct rtc_time. Reading >= 0xc0
		 * means "don't care" or "match all". Only the tm_hour,
		 * tm_min, and tm_sec values are filled in.
		 */

		get_rtc_alm_time(&wtime);
		break; 
	}
	case RTC_ALM_SET:	/* Store a time into the alarm */
	{
		/*
		 * This expects a struct rtc_time. Writing 0xff means
		 * "don't care" or "match all". Only the tm_hour,
		 * tm_min and tm_sec are used.
		 */
		unsigned char hrs, min, sec;
		struct rtc_time alm_tm;

		if (copy_from_user(&alm_tm, (struct rtc_time*)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		hrs = alm_tm.tm_hour;
		min = alm_tm.tm_min;
		sec = alm_tm.tm_sec;

		if (hrs >= 24)
			hrs = 0xff;

		if (min >= 60)
			min = 0xff;

		if (sec >= 60)
			sec = 0xff;

		spin_lock_irqsave(&rtc_lock, flags);
		if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) ||
		    RTC_ALWAYS_BCD)
		{
			BIN_TO_BCD(sec);
			BIN_TO_BCD(min);
			BIN_TO_BCD(hrs);
		}
		CMOS_WRITE(hrs, RTC_HOURS_ALARM);
		CMOS_WRITE(min, RTC_MINUTES_ALARM);
		CMOS_WRITE(sec, RTC_SECONDS_ALARM);
		spin_unlock_irqrestore(&rtc_lock, flags);

		return 0;
	}
	case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		get_rtc_time(&wtime);
		break;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		struct rtc_time rtc_tm;
		unsigned char mon, day, hrs, min, sec, leap_yr;
		unsigned char save_control, save_freq_select;
		unsigned int yrs;
		unsigned long flags;
			
		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		if (copy_from_user(&rtc_tm, (struct rtc_time*)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;

		yrs = rtc_tm.tm_year + 1900;
		mon = rtc_tm.tm_mon + 1;   /* tm_mon starts at zero */
		day = rtc_tm.tm_mday;
		hrs = rtc_tm.tm_hour;
		min = rtc_tm.tm_min;
		sec = rtc_tm.tm_sec;

		if (yrs < 1970)
			return -EINVAL;

		leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

		if ((mon > 12) || (day == 0))
			return -EINVAL;

		if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
			return -EINVAL;
			
		if ((hrs >= 24) || (min >= 60) || (sec >= 60))
			return -EINVAL;

		if ((yrs -= epoch) > 255)    /* They are unsigned */
			return -EINVAL;

		spin_lock_irqsave(&rtc_lock, flags);
		if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY)
		    || RTC_ALWAYS_BCD) {
			if (yrs > 169) {
				spin_unlock_irqrestore(&rtc_lock, flags);
				return -EINVAL;
			}
			if (yrs >= 100)
				yrs -= 100;

			BIN_TO_BCD(sec);
			BIN_TO_BCD(min);
			BIN_TO_BCD(hrs);
			BIN_TO_BCD(day);
			BIN_TO_BCD(mon);
			BIN_TO_BCD(yrs);
		}

		save_control = CMOS_READ(RTC_CONTROL);
		CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);
		save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
		CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

		CMOS_WRITE(yrs, RTC_YEAR);
		CMOS_WRITE(mon, RTC_MONTH);
		CMOS_WRITE(day, RTC_DAY_OF_MONTH);
		CMOS_WRITE(hrs, RTC_HOURS);
		CMOS_WRITE(min, RTC_MINUTES);
		CMOS_WRITE(sec, RTC_SECONDS);

		CMOS_WRITE(save_control, RTC_CONTROL);
		CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

		spin_unlock_irqrestore(&rtc_lock, flags);
		return 0;
	}
#ifndef __alpha__
	case RTC_IRQP_READ:	/* Read the periodic IRQ rate.	*/
	{
		return put_user(rtc_freq, (unsigned long *)arg);
	}
	case RTC_IRQP_SET:	/* Set periodic IRQ rate.	*/
	{
		int tmp = 0;
		unsigned char val;

		/* 
		 * The max we can do is 8192Hz.
		 */
		if ((arg < 2) || (arg > 8192))
			return -EINVAL;
		/*
		 * We don't really want Joe User generating more
		 * than 64Hz of interrupts on a multi-user machine.
		 */
		if ((arg > 64) && (!capable(CAP_SYS_RESOURCE)))
			return -EACCES;

		while (arg > (1<<tmp))
			tmp++;

		/*
		 * Check that the input was really a power of 2.
		 */
		if (arg != (1<<tmp))
			return -EINVAL;

		rtc_freq = arg;

		spin_lock_irqsave(&rtc_lock, flags);
		val = CMOS_READ(RTC_FREQ_SELECT) & 0xf0;
		val |= (16 - tmp);
		CMOS_WRITE(val, RTC_FREQ_SELECT);
		spin_unlock_irqrestore(&rtc_lock, flags);
		return 0;
	}
#else
	case RTC_EPOCH_READ:	/* Read the epoch.	*/
	{
		return put_user (epoch, (unsigned long *)arg);
	}
	case RTC_EPOCH_SET:	/* Set the epoch.	*/
	{
		/* 
		 * There were no RTC clocks before 1900.
		 */
		if (arg < 1900)
			return -EINVAL;

		if (!capable(CAP_SYS_TIME))
			return -EACCES;

		epoch = arg;
		return 0;
	}
#endif
	default:
		return -EINVAL;
	}
	return copy_to_user((void *)arg, &wtime, sizeof wtime) ? -EFAULT : 0;
}

/*
 *	We enforce only one user at a time here with the open/close.
 *	Also clear the previous interrupt data on an open, and clean
 *	up things on a close.
 */

static int rtc_open(struct inode *inode, struct file *file)
{
	unsigned long flags;

	if(atomic_read(&rtc_status) & RTC_IS_OPEN)
		return -EBUSY;

	MOD_INC_USE_COUNT;

	atomic_set(&rtc_status, atomic_read(&rtc_status) | RTC_IS_OPEN);

	spin_lock_irqsave (&rtc_lock, flags);
	rtc_irq_data = 0;
	spin_unlock_irqrestore (&rtc_lock, flags);
	return 0;
}

static int rtc_fasync (int fd, struct file *filp, int on)

{
	return fasync_helper (fd, filp, on, &rtc_async_queue);
}

static int rtc_release(struct inode *inode, struct file *file)
{
	unsigned long flags;
#ifndef __alpha__
	/*
	 * Turn off all interrupts once the device is no longer
	 * in use, and clear the data.
	 */

	unsigned char tmp;

	spin_lock_irqsave(&rtc_lock, flags);
	tmp = CMOS_READ(RTC_CONTROL);
	tmp &=  ~RTC_PIE;
	tmp &=  ~RTC_AIE;
	tmp &=  ~RTC_UIE;
	CMOS_WRITE(tmp, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);
	spin_unlock_irqrestore(&rtc_lock, flags);

	if (atomic_read(&rtc_status) & RTC_TIMER_ON) {
		atomic_set(&rtc_status, atomic_read(&rtc_status) & ~RTC_TIMER_ON);
		del_timer(&rtc_irq_timer);
	}

	if (file->f_flags & FASYNC) {
		rtc_fasync (-1, file, 0);
	}

#endif
	MOD_DEC_USE_COUNT;

	spin_lock_irqsave (&rtc_lock, flags);
	rtc_irq_data = 0;
	spin_unlock_irqrestore (&rtc_lock, flags);
	atomic_set(&rtc_status, atomic_read(&rtc_status) & ~RTC_IS_OPEN);
	return 0;
}

#ifndef __alpha__
static unsigned int rtc_poll(struct file *file, poll_table *wait)
{
	unsigned long l, flags;

	poll_wait(file, &rtc_wait, wait);

	spin_lock_irqsave (&rtc_lock, flags);
	l = rtc_irq_data;
	spin_unlock_irqrestore (&rtc_lock, flags);

	if (l != 0)
		return POLLIN | POLLRDNORM;
	return 0;
}
#endif

/*
 *	The various file operations we support.
 */

static struct file_operations rtc_fops = {
	llseek:		rtc_llseek,
	read:		rtc_read,
#ifndef __alpha__
	poll:		rtc_poll,
#endif
	ioctl:		rtc_ioctl,
	open:		rtc_open,
	release:	rtc_release,
	fasync:		rtc_fasync,
};

static struct miscdevice rtc_dev=
{
	RTC_MINOR,
	"rtc",
	&rtc_fops
};

static int __init rtc_init(void)
{
	unsigned long flags;
#ifdef __alpha__
	unsigned int year, ctrl;
	unsigned long uip_watchdog;
	char *guess = NULL;
#endif
#ifdef __sparc__
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
#endif

#ifdef __sparc__
	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if(strcmp(edev->prom_name, "rtc") == 0) {
				goto found;
			}
		}
	}
	printk("rtc_init: no PC rtc found\n");
	return -EIO;

found:
	rtc_port = edev->resource[0].start;
	rtc_irq = edev->irqs[0];
	/*
	 * XXX Interrupt pin #7 in Espresso is shared between RTC and
	 * PCI Slot 2 INTA# (and some INTx# in Slot 1). SA_INTERRUPT here
	 * is asking for trouble with add-on boards. Change to SA_SHIRQ.
	 */
	if(request_irq(rtc_irq, rtc_interrupt, SA_INTERRUPT, "rtc", (void *)&rtc_port)) {
		/*
		 * Standard way for sparc to print irq's is to use
		 * __irq_itoa(). I think for EBus it's ok to use %d.
		 */
		printk("rtc: cannot register IRQ %d\n", rtc_irq);
		return -EIO;
	}
#else
	if (check_region (RTC_PORT (0), RTC_IO_EXTENT))
	{
		printk(KERN_ERR "rtc: I/O port %d is not free.\n", RTC_PORT (0));
		return -EIO;
	}

#ifndef __alpha__
	if(request_irq(RTC_IRQ, rtc_interrupt, SA_INTERRUPT, "rtc", NULL))
	{
		/* Yeah right, seeing as irq 8 doesn't even hit the bus. */
		printk(KERN_ERR "rtc: IRQ %d is not free.\n", RTC_IRQ);
		return -EIO;
	}
#endif

	request_region(RTC_PORT(0), RTC_IO_EXTENT, "rtc");
#endif /* __sparc__ vs. others */

	misc_register(&rtc_dev);
	create_proc_read_entry ("driver/rtc", 0, 0, rtc_read_proc, NULL);

#ifdef __alpha__
	rtc_freq = HZ;
	
	/* Each operating system on an Alpha uses its own epoch.
	   Let's try to guess which one we are using now. */
	
	uip_watchdog = jiffies;
	if (rtc_is_updating() != 0)
		while (jiffies - uip_watchdog < 2*HZ/100)
			barrier();
	
	spin_lock_irqsave(&rtc_lock, flags);
	year = CMOS_READ(RTC_YEAR);
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irqrestore(&rtc_lock, flags);
	
	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		BCD_TO_BIN(year);       /* This should never happen... */
	
	if (year > 10 && year < 44) {
		epoch = 1980;
		guess = "ARC console";
	} else if (year < 96) {
		epoch = 1952;
		guess = "Digital UNIX";
	}
	if (guess)
		printk("rtc: %s epoch (%lu) detected\n", guess, epoch);
#endif
#ifndef __alpha__
	init_timer(&rtc_irq_timer);
	rtc_irq_timer.function = rtc_dropped_irq;
	spin_lock_irqsave(&rtc_lock, flags);
	/* Initialize periodic freq. to CMOS reset default, which is 1024Hz */
	CMOS_WRITE(((CMOS_READ(RTC_FREQ_SELECT) & 0xF0) | 0x06), RTC_FREQ_SELECT);
	spin_unlock_irqrestore(&rtc_lock, flags);
#endif
	rtc_freq = 1024;

	printk(KERN_INFO "Real Time Clock Driver v" RTC_VERSION "\n");

	return 0;
}

static void __exit rtc_exit (void)
{
	/* interrupts and maybe timer disabled at this point by rtc_release */

	if (atomic_read(&rtc_status) & RTC_TIMER_ON)
		del_timer(&rtc_irq_timer);

	remove_proc_entry ("driver/rtc", NULL);
	misc_deregister(&rtc_dev);

#ifdef __sparc__
	free_irq (rtc_irq, &rtc_port);
#else
	release_region (RTC_PORT (0), RTC_IO_EXTENT);
#ifndef __alpha__
	free_irq (RTC_IRQ, NULL);
#endif
#endif /* __sparc__ */
}

module_init(rtc_init);
module_exit(rtc_exit);
EXPORT_NO_SYMBOLS;

#ifndef __alpha__
/*
 * 	At IRQ rates >= 4096Hz, an interrupt may get lost altogether.
 *	(usually during an IDE disk interrupt, with IRQ unmasking off)
 *	Since the interrupt handler doesn't get called, the IRQ status
 *	byte doesn't get read, and the RTC stops generating interrupts.
 *	A timer is set, and will call this function if/when that happens.
 *	To get it out of this stalled state, we just read the status.
 *	At least a jiffy of interrupts (rtc_freq/HZ) will have been lost.
 *	(You *really* shouldn't be trying to use a non-realtime system 
 *	for something that requires a steady > 1KHz signal anyways.)
 */

static void rtc_dropped_irq(unsigned long data)
{
	unsigned long flags;

	printk(KERN_INFO "rtc: lost some interrupts at %ldHz.\n", rtc_freq);
	mod_timer(&rtc_irq_timer, jiffies + HZ/rtc_freq + 2*HZ/100);

	spin_lock_irqsave(&rtc_lock, flags);
	rtc_irq_data += ((rtc_freq/HZ)<<8);
	rtc_irq_data &= ~0xff;
	rtc_irq_data |= (CMOS_READ(RTC_INTR_FLAGS) & 0xF0);	/* restart */
	spin_unlock_irqrestore(&rtc_lock, flags);
}
#endif

/*
 *	Info exported via "/proc/driver/rtc".
 */

static int rtc_proc_output (char *buf)
{
#define YN(bit) ((ctrl & bit) ? "yes" : "no")
#define NY(bit) ((ctrl & bit) ? "no" : "yes")
	char *p;
	struct rtc_time tm;
	unsigned char batt, ctrl;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	batt = CMOS_READ(RTC_VALID) & RTC_VRT;
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irqrestore(&rtc_lock, flags);

	p = buf;

	get_rtc_time(&tm);

	/*
	 * There is no way to tell if the luser has the RTC set for local
	 * time or for Universal Standard Time (GMT). Probably local though.
	 */
	p += sprintf(p,
		     "rtc_time\t: %02d:%02d:%02d\n"
		     "rtc_date\t: %04d-%02d-%02d\n"
	 	     "rtc_epoch\t: %04lu\n",
		     tm.tm_hour, tm.tm_min, tm.tm_sec,
		     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, epoch);

	get_rtc_alm_time(&tm);

	/*
	 * We implicitly assume 24hr mode here. Alarm values >= 0xc0 will
	 * match any value for that particular field. Values that are
	 * greater than a valid time, but less than 0xc0 shouldn't appear.
	 */
	p += sprintf(p, "alarm\t\t: ");
	if (tm.tm_hour <= 24)
		p += sprintf(p, "%02d:", tm.tm_hour);
	else
		p += sprintf(p, "**:");

	if (tm.tm_min <= 59)
		p += sprintf(p, "%02d:", tm.tm_min);
	else
		p += sprintf(p, "**:");

	if (tm.tm_sec <= 59)
		p += sprintf(p, "%02d\n", tm.tm_sec);
	else
		p += sprintf(p, "**\n");

	p += sprintf(p,
		     "DST_enable\t: %s\n"
		     "BCD\t\t: %s\n"
		     "24hr\t\t: %s\n"
		     "square_wave\t: %s\n"
		     "alarm_IRQ\t: %s\n"
		     "update_IRQ\t: %s\n"
		     "periodic_IRQ\t: %s\n"
		     "periodic_freq\t: %ld\n"
		     "batt_status\t: %s\n",
		     YN(RTC_DST_EN),
		     NY(RTC_DM_BINARY),
		     YN(RTC_24H),
		     YN(RTC_SQWE),
		     YN(RTC_AIE),
		     YN(RTC_UIE),
		     YN(RTC_PIE),
		     rtc_freq,
		     batt ? "okay" : "dead");

	return  p - buf;
#undef YN
#undef NY
}

static int rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data)
{
        int len = rtc_proc_output (page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

/*
 * Returns true if a clock update is in progress
 */
static inline unsigned char rtc_is_updating(void)
{
	unsigned long flags;
	unsigned char uip;

	spin_lock_irqsave(&rtc_lock, flags);
	uip = (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return uip;
}

static void get_rtc_time(struct rtc_time *rtc_tm)
{

	unsigned long flags, uip_watchdog = jiffies;
	unsigned char ctrl;

	/*
	 * read RTC once any update in progress is done. The update
	 * can take just over 2ms. We wait 10 to 20ms. There is no need to
	 * to poll-wait (up to 1s - eeccch) for the falling edge of RTC_UIP.
	 * If you need to know *exactly* when a second has started, enable
	 * periodic update complete interrupts, (via ioctl) and then 
	 * immediately read /dev/rtc which will block until you get the IRQ.
	 * Once the read clears, read the RTC time (again via ioctl). Easy.
	 */

	if (rtc_is_updating() != 0)
		while (jiffies - uip_watchdog < 2*HZ/100)
			barrier();

	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Even though the
	 * RTC has RTC_DAY_OF_WEEK, we ignore it, as it is only updated
	 * by the RTC when initially set to a non-zero value.
	 */
	spin_lock_irqsave(&rtc_lock, flags);
	rtc_tm->tm_sec = CMOS_READ(RTC_SECONDS);
	rtc_tm->tm_min = CMOS_READ(RTC_MINUTES);
	rtc_tm->tm_hour = CMOS_READ(RTC_HOURS);
	rtc_tm->tm_mday = CMOS_READ(RTC_DAY_OF_MONTH);
	rtc_tm->tm_mon = CMOS_READ(RTC_MONTH);
	rtc_tm->tm_year = CMOS_READ(RTC_YEAR);
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irqrestore(&rtc_lock, flags);

	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
		BCD_TO_BIN(rtc_tm->tm_sec);
		BCD_TO_BIN(rtc_tm->tm_min);
		BCD_TO_BIN(rtc_tm->tm_hour);
		BCD_TO_BIN(rtc_tm->tm_mday);
		BCD_TO_BIN(rtc_tm->tm_mon);
		BCD_TO_BIN(rtc_tm->tm_year);
	}

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	if ((rtc_tm->tm_year += (epoch - 1900)) <= 69)
		rtc_tm->tm_year += 100;

	rtc_tm->tm_mon--;
}

static void get_rtc_alm_time(struct rtc_time *alm_tm)
{
	unsigned long flags;
	unsigned char ctrl;

	/*
	 * Only the values that we read from the RTC are set. That
	 * means only tm_hour, tm_min, and tm_sec.
	 */
	spin_lock_irqsave(&rtc_lock, flags);
	alm_tm->tm_sec = CMOS_READ(RTC_SECONDS_ALARM);
	alm_tm->tm_min = CMOS_READ(RTC_MINUTES_ALARM);
	alm_tm->tm_hour = CMOS_READ(RTC_HOURS_ALARM);
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irqrestore(&rtc_lock, flags);

	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
		BCD_TO_BIN(alm_tm->tm_sec);
		BCD_TO_BIN(alm_tm->tm_min);
		BCD_TO_BIN(alm_tm->tm_hour);
	}
}

#ifndef __alpha__
/*
 * Used to disable/enable interrupts for any one of UIE, AIE, PIE.
 * Rumour has it that if you frob the interrupt enable/disable
 * bits in RTC_CONTROL, you should read RTC_INTR_FLAGS, to
 * ensure you actually start getting interrupts. Probably for
 * compatibility with older/broken chipset RTC implementations.
 * We also clear out any old irq data after an ioctl() that
 * meddles with the interrupt enable/disable bits.
 */

static void mask_rtc_irq_bit(unsigned char bit)
{
	unsigned char val;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	val = CMOS_READ(RTC_CONTROL);
	val &=  ~bit;
	CMOS_WRITE(val, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);
	rtc_irq_data = 0;
	spin_unlock_irqrestore(&rtc_lock, flags);
}

static void set_rtc_irq_bit(unsigned char bit)
{
	unsigned char val;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	val = CMOS_READ(RTC_CONTROL);
	val |= bit;
	CMOS_WRITE(val, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);
	rtc_irq_data = 0;
	spin_unlock_irqrestore(&rtc_lock, flags);
}
#endif
