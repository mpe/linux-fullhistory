/*
 * split in two parts for better support of different hardware
 * by Joerg Dorchain (dorchain@mpi-sb.mpg.de)
 *
 * Amiga printer device by Michael Rausch (linux@uni-koblenz.de);
 * Atari support added by Andreas Schwab (schwab@ls5.informatik.uni-dortmund.de);
 * based upon work from
 *
 * Copyright (C) 1992 by Jim Weigand and Linus Torvalds
 * Copyright (C) 1992,1993 by Michael K. Johnson
 * - Thanks much to Gunter Windau for pointing out to me where the error
 *   checking ought to be.
 * Copyright (C) 1993 by Nigel Gamble (added interrupt code)
 */

/* 01/17/95: Matthias Welwarsky (dg8y@rs11.hrz.th-darmstadt.de)
 * lp_write(): rewritten from scratch
 * lp_interrupt(): fixed cli()/sti()-bug
 * 
 * 95/05/28: Andreas Schwab (schwab@issan.informatik.uni-dortmund.de)
 * lp_write() fixed to make it work again.
 * 95/08/18: Andreas Schwab
 * lp_write_interrupt: fix race condition
 *
 *  * CAUTION, please do check! *    
 * 
 *  on 68000-based machines sti() must NEVER appear in interrupt driven
 *  code. The 68k-CPU has a priority-based interrupt scheme. while an interrupt
 *  with a certain priority is executed, all requests with lower or same
 *  priority get locked out. executing the sti()-macro allows ANY interrupt
 *  to be served. this really causes BIG trouble!
 *  to protect an interrupt driven routine against being interrupted 
 *  (if absolutely needed!) one should use save_flags();cli()/restore_flags()!
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/kmod.h>

#ifdef CONFIG_AMIGA
#ifdef CONFIG_MULTIFACE_III_LP
#include <linux/lp_mfc.h>
#endif
#endif

#include <linux/lp_m68k.h>
#include <linux/lp_intern.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/system.h>


/*
 *  why bother around with the pio driver when the interrupt works;
 *  so, for "security" reasons only, it's configurable here.
 *  saves some bytes, at least ...
 */
#define FORCE_POLLING	 0
#define FORCE_INTERRUPT	 1
/*
 *  PREFER_INTERRUPT doesn't make much sense on m68k.
 *  it is preserved here in case of joining with the i386 driver
 *
#define PREFER_INTERRUPT 2
 */

#define WHICH_DRIVER	FORCE_INTERRUPT

struct lp_struct *lp_table[MAX_LP] = {NULL,};

static int max_lp; /* the real number of devices */

/* 
 * All my debugging code assumes that you debug with only one printer at
 * a time. RWWH
 */

#define LP_DEBUG 
#undef LP_DEBUG 


#if WHICH_DRIVER != FORCE_INTERRUPT
#ifdef LP_DEBUG
static int lp_max_count = 1;
#endif

static int lp_char_polled(char lpchar, int dev)
{
	unsigned long count  = 0; 

	do {
		count ++;
		if (current->need_resched)
			schedule();
	} while (lp_table[dev]->lp_is_busy(dev) && count < lp_table[dev]->chars);

	if (count == lp_table[dev]->chars) {
		return 0;
		/* we timed out, and the character was /not/ printed */
	}
#ifdef LP_DEBUG
	if (count > lp_max_count) {
		printk("lp success after %d counts.\n",count);
		lp_max_count = count;
	}
#endif
	lp_table[dev]->lp_out(lpchar, dev);
	return 1;
}
#endif


#ifdef LP_DEBUG
unsigned int lp_total_chars = 0;
unsigned int lp_last_call = 0;
#endif


#if WHICH_DRIVER != FORCE_POLLING
static __inline__ int lp_char_interrupt(char lpchar, int dev)
{
	if (!lp_table[dev]->lp_is_busy(dev)) {
		lp_table[dev]->lp_out(lpchar,dev);
		return 1;
	}
	return 0;
}

static int lp_error;

void lp_interrupt(int dev)
{
    if (dev >= 0 && dev < MAX_LP && lp_table[dev]->do_print)
    {
	if (lp_table[dev]->copy_size)
	{
    		unsigned long flags;
		save_flags(flags);
		cli();
		if (lp_char_interrupt(lp_table[dev]->lp_buffer[lp_table[dev]->bytes_written], dev)) {
			--lp_table[dev]->copy_size;
			++lp_table[dev]->bytes_written;
			restore_flags(flags);
		}
		else
		{
			lp_table[dev]->do_print = 0;
			restore_flags(flags);
			lp_error = 1;
			wake_up_interruptible(&lp_table[dev]->lp_wait_q);
		}
	}
	else
	{
		lp_table[dev]->do_print = 0;
		lp_error = 0;
		wake_up_interruptible(&lp_table[dev]->lp_wait_q);
	}

    }
}

#if WHICH_DRIVER == FORCE_INTERRUPT
static ssize_t lp_write(struct file *file, const char *buf,
			size_t count, loff_t *ppos)
#else
static ssize_t lp_write_interrupt(struct file *file, const char *buf,
				  size_t count, loff_t *ppos)
#endif
{
  struct inode *inode = file->f_dentry->d_inode;
  unsigned long total_bytes_written = 0;
  unsigned int flags;
  long timeout;
  int rc;
  int dev = MINOR(inode->i_rdev);

  do {
    lp_table[dev]->do_print = 0;		/* disable lp_interrupt()   */
    lp_table[dev]->bytes_written = 0;	/* init buffer read-pointer */
    lp_error = 0;
    lp_table[dev]->copy_size = (count <= LP_BUFFER_SIZE ? count : LP_BUFFER_SIZE);
    if (copy_from_user(lp_table[dev]->lp_buffer, buf,
		       lp_table[dev]->copy_size))
      return -EFAULT;
    while (lp_table[dev]->copy_size) {
      save_flags(flags);
      cli();				/* no interrupts now */
      lp_table[dev]->do_print = 1;	/* enable lp_interrupt() */
      if (lp_char_interrupt(lp_table[dev]->lp_buffer[lp_table[dev]->bytes_written], dev)) {
	++lp_table[dev]->bytes_written;
	--lp_table[dev]->copy_size;
	lp_error = 0;
      } else {				/* something went wrong   */
	lp_table[dev]->do_print = 0;	/* disable lp_interrupt() */
	lp_error = 1;			/* printer caused error   */
      }
      if (lp_error) {

	  /* something blocked printing, so we don't want to sleep too long,
	     in case we have to rekick the interrupt */

	  timeout = LP_TIMEOUT_POLLED;
      } else {
	  timeout = LP_TIMEOUT_INTERRUPT;
      }
  
      interruptible_sleep_on_timeout(&lp_table[dev]->lp_wait_q, timeout);
      restore_flags(flags);
  
      /* we're up again and running. we first disable lp_interrupt(), then
	 check what happened meanwhile */

      lp_table[dev]->do_print = 0;
      rc = total_bytes_written + lp_table[dev]->bytes_written;

      if (signal_pending(current)) {
	if (rc == 0)
	  rc = -EINTR;
	return rc;
      }
      if (lp_error) {

	/* an error has occurred, maybe in lp_interrupt().
	   figure out the type of error, exit on request or if nothing has 
	   been printed at all. */
	
	if (lp_table[dev]->lp_has_pout(dev)) {
	  printk(KERN_NOTICE "lp%d: paper-out\n",dev);
	  if (!rc) rc = -ENOSPC;
	} else if (!lp_table[dev]->lp_is_online(dev)) {
	  printk(KERN_NOTICE "lp%d: off-line\n",dev);
	  if (!rc) rc = -EIO;
	} else if (lp_table[dev]->lp_is_busy(dev)) {
	  printk(KERN_NOTICE "lp%d: on fire\n",dev);
	  if (!rc) rc = -EIO;
	}
	if (lp_table[dev]->flags & LP_ABORT)
	  return rc;
      }
      /* check if our buffer was completely printed, if not, most likely
	 an unsolved error blocks the printer. As we can`t do anything
	 against, we start all over again. Else we set the read-pointer
	 of the buffer and count the printed characters */
      
      if (!lp_table[dev]->copy_size) {
	total_bytes_written += lp_table[dev]->bytes_written;
	buf += lp_table[dev]->bytes_written;
	count -= lp_table[dev]->bytes_written;
      }
    }
  } while (count > 0);
  return total_bytes_written;
}
#else
void (*lp_interrupt)() = NULL;
#endif

#if WHICH_DRIVER != FORCE_INTERRUPT
#if WHICH_DRIVER == FORCE_POLLING
static ssize_t lp_write(struct file *file, const char *buf,
			size_t count, loff_t *ppos)
#else
static ssize_t lp_write_polled(struct file *file, const char *buf,
			size_t count, loff_t *ppos)
#endif
{
	struct inode *inode = file->f_dentry->d_inode;
	char *temp = buf;
	int dev = MINOR(inode->i_rdev);

#ifdef LP_DEBUG
	if (time_after(jiffies, lp_last_call + lp_table[dev]->time)) {
		lp_total_chars = 0;
		lp_max_count = 1;
	}
	lp_last_call = jiffies;
#endif

	temp = buf;
	while (count > 0) {
		int c;
		if (get_user(c, temp))
			return -EFAULT;
		if (lp_char_polled(c, dev)) {
			/* only update counting vars if character was printed */
			count--; temp++;
#ifdef LP_DEBUG
			lp_total_chars++;
#endif
		} else { /* if printer timed out */
			unsigned long timeout = LP_TIMEOUT_POLLED;
			int error = 0;
			if (lp_table[dev]->lp_has_pout(dev)) {
				printk(KERN_NOTICE "lp%d: out of paper\n",dev);
				if (lp_table[dev]->flags & LP_ABORT)
					error = -ENOSPC;
			} else if (!lp_table[dev]->lp_is_online(dev)) {
				printk(KERN_NOTICE "lp%d: off-line\n",dev);
				if (lp_table[dev]->flags & LP_ABORT)
					error = -EIO;
			} else
	                /* not offline or out of paper. on fire? */
			if (lp_table[dev]->lp_is_busy(dev)) {
				printk(KERN_NOTICE "lp%d: on fire\n",dev);
				if (lp_table[dev]->flags & LP_ABORT)
					error = -EIO;
			}
			else
				timeout = lp_table[dev]->time;

			/* check for signals before going to sleep */
			if (error == 0 && signal_pending(current))
				error = -EINTR;
			if (error) {
				if (temp != buf)
					return temp-buf;
				else
					return error;
			}

#ifdef LP_DEBUG
			printk("lp sleeping at %d characters for %d jiffies\n",
				lp_total_chars, timeout);
			lp_total_chars = 0;
#endif
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(timeout);
		}
	}
	return temp - buf;
}
#endif

unsigned int lp_irq = 0;

#if WHICH_DRIVER == PREFER_INTERRUPT
static ssize_t lp_write(struct file *file, const char *buf, size_t count,
			loff_t *ppos)
{
	if (lp_irq)
		return lp_write_interrupt(file, buf, count, ppos);
	else
		return lp_write_polled(file, buf, count, ppos);
}
#endif

static long long lp_lseek(struct file * file, long long offset, int origin)
{
	return -ESPIPE;
}

static int lp_open(struct inode *inode, struct file *file)
{
	int dev = MINOR(inode->i_rdev);
	int ret;

	MOD_INC_USE_COUNT;

	ret = -ENODEV;
	if (dev >= MAX_LP)
		goto out_err;

	if (!lp_table[dev]) {
		char modname[30];

		sprintf(modname, "char-major-%d-%d", LP_MAJOR, dev);
		request_module(modname);
	}
	if (!lp_table[dev])
		goto out_err;
	if (!(lp_table[dev]->flags & LP_EXIST))
		goto out_err;
	ret = -EBUSY;
	if (lp_table[dev]->flags & LP_BUSY)
		goto out_err;

	lp_table[dev]->flags |= LP_BUSY;

	ret = lp_table[dev]->lp_open(dev);
	if (ret != 0) {
		lp_table[dev]->flags &= ~LP_BUSY;
		goto out_err;
	}
	return ret;

out_err:
	MOD_DEC_USE_COUNT;
	return ret;
}

static int lp_release(struct inode *inode, struct file *file)
{
	int dev =MINOR(inode->i_rdev);

	lp_table[dev]->flags &= ~LP_BUSY;
	lp_table[dev]->lp_release(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}


static int lp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int retval = -ENODEV;

#ifdef LP_DEBUG
	printk("lp%d ioctl, cmd: 0x%x, arg: 0x%x\n", minor, cmd, arg);
#endif
	if (minor >= max_lp)
		goto out;
	if (!(lp_table[minor]->flags & LP_EXIST))
		goto out;
	retval = 0;
	switch (cmd) {
	case LPTIME:
		lp_table[minor]->time = arg;
		break;
	case LPCHAR:
		lp_table[minor]->chars = arg;
		break;
	case LPABORT:
		if (arg)
			lp_table[minor]->flags |= LP_ABORT;
		else
			lp_table[minor]->flags &= ~LP_ABORT;
		break;
	case LPWAIT:
		lp_table[minor]->wait = arg;
		break;
	case LPSETIRQ:
	case LPGETIRQ:
	        retval = lp_irq;
		break;
	default:
		retval = -EINVAL;
		if (lp_table[minor]->lp_ioctl)
			retval = lp_table[minor]->lp_ioctl(minor, cmd, arg);
	}
out:
	return retval;
}


static struct file_operations lp_fops = {
	lp_lseek,
	NULL,		/* lp_read */
	lp_write,
	NULL,		/* lp_readdir */
	NULL,		/* lp_poll */
	lp_ioctl,
	NULL,		/* lp_mmap */
	lp_open,
	NULL,		/* flush */
	lp_release
};

EXPORT_SYMBOL(lp_table);
EXPORT_SYMBOL(lp_irq);
EXPORT_SYMBOL(lp_interrupt);
EXPORT_SYMBOL(register_parallel);
EXPORT_SYMBOL(unregister_parallel);

__initfunc(int lp_m68k_init(void))
{
	extern char m68k_debug_device[];

	if (!strcmp( m68k_debug_device, "par" ))
		return -EBUSY;

	if (register_chrdev(LP_MAJOR,"lp", &lp_fops)) {
		printk(KERN_ERR "unable to get major %d for line printer\n", LP_MAJOR);
		return -ENXIO;
	}

#if WHICH_DRIVER == FORCE_POLLING
	lp_irq = 0;
	printk(KERN_INFO "lp_init: lp using polling driver\n");
#else

	lp_irq = 1;
	printk(KERN_INFO "lp_init: lp using interrupt driver\n");
#endif

#ifndef MODULE
	lp_internal_init();
#ifdef CONFIG_MULTIFACE_III_LP
	lp_mfc_init();
#endif
#endif
	return 0;
}

/*
 * Currently we do not accept any lp-parameters, but that may change.
 */
__initfunc(void lp_setup(char *str, int *ints))
{	
}

#ifdef MODULE
int init_module(void)
{
	return lp_m68k_init();
}

void cleanup_module(void)
{
	unregister_chrdev(LP_MAJOR, "lp");
}
#endif

/*
 * (un-)register for hardware drivers
 * tab is an inititalised lp_struct, dev the desired minor
 * if dev < 0, let the driver choose the first free minor
 * if successful return the minor, else -1
 */
int register_parallel(struct lp_struct *tab, int dev)
{
if (dev < 0) {
	dev = 0;
	while ((dev < MAX_LP) && (lp_table[dev] != NULL))
		dev++;
}
if (dev > MAX_LP)
	return -1;
if (lp_table[dev] != NULL)
	return -1;
lp_table[dev] = tab;
printk(KERN_INFO "lp%d: %s at 0x%08lx\n", dev, tab->name, (long)tab->base);
return dev;
}

#ifdef CONFIG_MODULES
void unregister_parallel(int dev)
{
if ((dev < 0) || (dev > MAX_LP) || (lp_table[dev] == NULL))
	printk(KERN_ERR "WARNING: unregister_parallel for non-existant device ignored!\n");
else
	lp_table[dev] = NULL;
}
#endif
