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
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <asm/irq.h>

#ifdef CONFIG_AMIGA
#include <asm/amigaints.h>
#ifdef CONFIG_MULTIFACE_III_LP
#include <linux/lp_mfc.h>
#endif
#endif
#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#include <asm/atariints.h>
#endif

#include <linux/lp_m68k.h>
#include <linux/lp_intern.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>

#include <asm/segment.h>
#include <asm/system.h>


/*
 *  why bother around with the pio driver when the interrupt works;
 *  so, for "security" reasons only, it's configurable here.
 *  saves some bytes, at least ...
 */
#define FORCE_POLLING	 0
#define FORCE_INTERRUPT	 1
#define PREFER_INTERRUPT 2

#define WHICH_DRIVER	FORCE_INTERRUPT

#define MAX_LP 3 /* the maximum number of devices */

struct lp_struct lp_table[MAX_LP] = {{0,},};

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
		if(need_resched)
			schedule();
	} while (lp_table[dev].lp_is_busy(dev) && count < lp_table[dev].chars);

	if (count == lp_table[dev].chars) {
		return 0;
		/* we timed out, and the character was /not/ printed */
	}
#ifdef LP_DEBUG
	if (count > lp_max_count) {
		printk("lp success after %d counts.\n",count);
		lp_max_count = count;
	}
#endif
	lp_table[dev].lp_out(lpchar, dev);
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
	if (!lp_table[dev].lp_is_busy(dev)) {
		lp_table[dev].lp_out(lpchar,dev);
		return 1;
	}
	return 0;
}

static int lp_error;

static void lp_interrupt(int irq, struct pt_regs *fp, void *dummy)
{
    unsigned long flags;
    int dev;

    for (dev = 0; dev < max_lp; dev++) {
	if (lp_table[dev].lp_my_interrupt(dev) != 0)
	  if (lp_table[dev].do_print)
	  {
		  if (lp_table[dev].copy_size)
		  {
			  save_flags(flags);
			  cli();
			  if (lp_char_interrupt(lp_table[dev].lp_buffer[lp_table[dev].bytes_written], dev)) {
				  --lp_table[dev].copy_size;
				  ++lp_table[dev].bytes_written;
				  restore_flags(flags);
			  }
			  else
			  {
				  lp_table[dev].do_print = 0;
				  restore_flags(flags);
				  lp_error = 1;
				  wake_up_interruptible(&lp_table[dev].lp_wait_q);
			  }
		  }
		  else
		  {
			  lp_table[dev].do_print = 0;
			  lp_error = 0;
			  wake_up_interruptible(&lp_table[dev].lp_wait_q);
		  }

	  }
    }
}

#if WHICH_DRIVER == FORCE_INTERRUPT
static int lp_write(struct inode *inode, struct file *file,
		    const char *buf, int count)
#else
static int lp_write_interrupt(struct inode *inode, struct file *file,
			      const char *buf, int count)
#endif
{
  unsigned long total_bytes_written = 0;
  unsigned int flags;
  int rc;
  int dev = MINOR(inode->i_rdev);

  do {
    lp_table[dev].do_print = 0;		/* disable lp_interrupt()   */
    lp_table[dev].bytes_written = 0;	/* init buffer read-pointer */
    lp_error = 0;
    lp_table[dev].copy_size = (count <= LP_BUFFER_SIZE ? count : LP_BUFFER_SIZE);
    memcpy_fromfs(lp_table[dev].lp_buffer, buf, lp_table[dev].copy_size);
    while (lp_table[dev].copy_size) {
      save_flags(flags);
      cli();				/* no interrupts now */
      lp_table[dev].do_print = 1;	/* enable lp_interrupt() */
      if (lp_char_interrupt(lp_table[dev].lp_buffer[lp_table[dev].bytes_written], dev)) {
	++lp_table[dev].bytes_written;
	--lp_table[dev].copy_size;
	lp_error = 0;
      } else {				/* something went wrong   */
	lp_table[dev].do_print = 0;	/* disable lp_interrupt() */
	lp_error = 1;			/* printer caused error   */
      }
      if (lp_error) {

	  /* something blocked printing, so we don't want to sleep too long,
	     in case we have to rekick the interrupt */

	  current->timeout = jiffies + LP_TIMEOUT_POLLED;
      } else {
	  current->timeout = jiffies + LP_TIMEOUT_INTERRUPT;
      }
  
      interruptible_sleep_on(&lp_table[dev].lp_wait_q);
      restore_flags(flags);
  
      /* we're up again and running. we first disable lp_interrupt(), then
	 check what happened meanwhile */

      lp_table[dev].do_print = 0;
      rc = total_bytes_written + lp_table[dev].bytes_written;

      if (current->signal & ~current->blocked) {
	if (rc)
	  return rc;
	else
	  return -EINTR;
      }
      if (lp_error) {

	/* an error has occurred, maybe in lp_interrupt().
	   figure out the type of error, exit on request or if nothing has 
	   been printed at all. */
	
	if (lp_table[dev].lp_has_pout(dev)) {
	  printk(KERN_NOTICE "lp%d: paper-out\n",dev);
	  if (!rc) rc = -ENOSPC;
	} else if (!lp_table[dev].lp_is_online(dev)) {
	  printk(KERN_NOTICE "lp%d: off-line\n",dev);
	  if (!rc) rc = -EIO;
	} else if (lp_table[dev].lp_is_busy(dev)) {
	  printk(KERN_NOTICE "lp%d: on fire\n",dev);
	  if (!rc) rc = -EIO;
	}
	if (lp_table[dev].flags & LP_ABORT)
	  return rc;
      }
      /* check if our buffer was completely printed, if not, most likely
	 an unsolved error blocks the printer. As we can`t do anything
	 against, we start all over again. Else we set the read-pointer
	 of the buffer and count the printed characters */
      
      if (!lp_table[dev].copy_size) {
	total_bytes_written += lp_table[dev].bytes_written;
	buf += lp_table[dev].bytes_written;
	count -= lp_table[dev].bytes_written;
      }
    }
  } while (count > 0);
  return total_bytes_written;
}
#endif

#if WHICH_DRIVER != FORCE_INTERRUPT
#if WHICH_DRIVER == FORCE_POLLING
static int lp_write(struct inode *inode, struct file *file,
		    const char *buf, int count)
#else
static int lp_write_polled(struct inode *inode, struct file *file,
			   const char *buf, int count)
#endif
{
	char *temp = buf;
	int dev = MINOR(inode->i_rdev);

#ifdef LP_DEBUG
	if (jiffies-lp_last_call > lp_table[dev].time) {
		lp_total_chars = 0;
		lp_max_count = 1;
	}
	lp_last_call = jiffies;
#endif

	temp = buf;
	while (count > 0) {
		if (lp_char_polled(get_user(temp), dev)) {
			/* only update counting vars if character was printed */
			count--; temp++;
#ifdef LP_DEBUG
			lp_total_chars++;
#endif
		} else { /* if printer timed out */
			if (lp_table[dev].lp_has_pout(dev)) {
				printk(KERN_NOTICE "lp%d: out of paper\n",dev);
				if (lp_table[dev].flags & LP_ABORT)
					return temp - buf ? temp-buf : -ENOSPC;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT_POLLED;
				schedule();
			} else if (!lp_table[dev].lp_is_online(dev)) {
				printk(KERN_NOTICE "lp%d: off-line\n",dev);
				if (lp_table[dev].flags & LP_ABORT)
					return temp - buf ? temp-buf : -EIO;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT_POLLED;
				schedule();
			} else
	                /* not offline or out of paper. on fire? */
			if (lp_table[dev].lp_is_busy(dev)) {
				printk(KERN_NOTICE "lp%d: on fire\n",dev);
				if (lp_table[dev].flags & LP_ABORT)
					return temp - buf ? temp-buf : -EFAULT;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT_POLLED;
				schedule();
			}

			/* check for signals before going to sleep */
			if (current->signal & ~current->blocked) {
				if (temp != buf)
					return temp-buf;
				else
					return -EINTR;
			}
#ifdef LP_DEBUG
			printk("lp sleeping at %d characters for %d jiffies\n",
				lp_total_chars, lp_table[dev].time);
			lp_total_chars = 0;
#endif
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + lp_table[dev].time;
			schedule();
		}
	}
	return temp - buf;
}
#endif

static unsigned int lp_irq = 0;

#if WHICH_DRIVER == PREFER_INTERRUPT
static int lp_write(struct inode *inode, struct file *file,
		    const char *buf, int count)
{
	if (lp_irq)
		return lp_write_interrupt(inode, file, buf, count);
	else
		return lp_write_polled(inode, file, buf, count);
}
#endif

static int lp_lseek(struct inode *inode, struct file *file,
		    off_t offset, int origin)
{
	return -ESPIPE;
}

static int lp_open(struct inode *inode, struct file *file)
{
	int dev = MINOR(inode->i_rdev);

	if (dev >= max_lp)
		return -ENODEV;
	if (!(lp_table[dev].flags & LP_EXIST))
		return -ENODEV;
	if (lp_table[dev].flags & LP_BUSY)
		return -EBUSY;

	lp_table[dev].flags |= LP_BUSY;

	return 0;
}

static void lp_release(struct inode *inode, struct file *file)
{
	lp_table[MINOR(inode->i_rdev)].flags &= ~LP_BUSY;
}


static int lp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int retval = 0;

#ifdef LP_DEBUG
	printk("lp%d ioctl, cmd: 0x%x, arg: 0x%x\n", minor, cmd, arg);
#endif
	if (minor >= max_lp)
		return -ENODEV;
	if (!(lp_table[minor].flags & LP_EXIST))
		return -ENODEV;
	switch (cmd) {
	case LPTIME:
		lp_table[minor].time = arg;
		break;
	case LPCHAR:
		lp_table[minor].chars = arg;
		break;
	case LPABORT:
		if (arg)
			lp_table[minor].flags |= LP_ABORT;
		else
			lp_table[minor].flags &= ~LP_ABORT;
		break;
	case LPWAIT:
		lp_table[minor].wait = arg;
		break;
	case LPSETIRQ:
	case LPGETIRQ:
	        retval = lp_irq;
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}


static struct file_operations lp_fops = {
	lp_lseek,
	NULL,		/* lp_read */
	lp_write,
	NULL,		/* lp_readdir */
	NULL,		/* lp_select */
	lp_ioctl,
	NULL,		/* lp_mmap */
	lp_open,
	lp_release
};


int lp_init(void)
{
	extern char m68k_debug_device[];

	if (!strcmp( m68k_debug_device, "par" ))
		return -EBUSY;

	if (register_chrdev(LP_MAJOR,"lp", &lp_fops)) {
		printk("unable to get major %d for line printer\n", LP_MAJOR);
		return -EBUSY;
	}

#if WHICH_DRIVER == FORCE_POLLING
	lp_irq = 0;
	printk(KERN_INFO "lp_init: lp using polling driver\n");
#else

#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA && AMIGAHW_PRESENT(AMI_PARALLEL))
		lp_irq = add_isr(IRQ_AMIGA_CIAA_FLG, lp_interrupt, 0,
				 NULL, "printer");
#endif
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI)
		lp_irq = add_isr(IRQ_MFP_BUSY, lp_interrupt, IRQ_TYPE_SLOW,
				 NULL, "printer");
#endif

	if (lp_irq)
		printk(KERN_INFO "lp_init: lp using interrupt\n");
	else

#if WHICH_DRIVER == PREFER_INTERRUPT
		printk(KERN_INFO "lp_init: lp using polling driver\n");
#else
		printk(KERN_WARNING "lp_init: can't get interrupt, and polling driver not configured\n");
#endif
#endif

	max_lp = 0;
	max_lp += lp_internal_init(lp_table, max_lp, MAX_LP, WHICH_DRIVER);
#ifdef CONFIG_MULTIFACE_III_LP
	max_lp += lp_mfc_init(lp_table, max_lp, MAX_LP, WHICH_DRIVER);
#if WHICH_DRIVER != FORCE_POLLING
	add_isr(IRQ_AMIGA_PORTS, lp_interrupt, 0, NULL,
		"Multiface III printer");
#endif
#endif
	return 0;
}

/*
 * Currently we do not accept any lp-parameters, but that may change.
 */
void	lp_setup(char *str, int *ints)
{	
}
