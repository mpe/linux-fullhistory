/*
 * Copyright (C) 1992 by Jim Weigand and Linus Torvalds
 * Copyright (C) 1992,1993 by Michael K. Johnson
 * - Thanks much to Gunter Windau for pointing out to me where the error
 *   checking ought to be.
 * Copyright (C) 1993 by Nigel Gamble (added interrupt code)
 * Copyright (C) 1994 by Alan Cox (Modularised it)
 * LPCAREFUL, LPABORT, LPGETSTATUS added by Chris Metcalf, metcalf@lcs.mit.edu
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/lp.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>

/* the BIOS manuals say there can be up to 4 lpt devices
 * but I have not seen a board where the 4th address is listed
 * if you have different hardware change the table below 
 * please let me know if you have different equipment
 * if you have more than 3 printers, remember to increase LP_NO
 */
struct lp_struct lp_table[] = {
	{ 0x3bc, 0, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, },
	{ 0x378, 0, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, },
	{ 0x278, 0, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, },
}; 
#define LP_NO 3

/* Test if printer is ready (and optionally has no error conditions) */
#define LP_READY(minor, status) \
  ((LP_F(minor) & LP_CAREFUL) ? _LP_CAREFUL_READY(status) : (status & LP_PBUSY))
#define LP_CAREFUL_READY(minor, status) \
  ((LP_F(minor) & LP_CAREFUL) ? _LP_CAREFUL_READY(status) : 1)
#define _LP_CAREFUL_READY(status) \
   (status & (LP_PBUSY|LP_POUTPA|LP_PSELECD|LP_PERRORP)) == \
      (LP_PBUSY|LP_PSELECD|LP_PERRORP) 

/* 
 * All my debugging code assumes that you debug with only one printer at
 * a time. RWWH
 */

#undef LP_DEBUG

static int lp_reset(int minor)
{
	int testvalue;
	unsigned char command;

	command = LP_PSELECP | LP_PINITP;

	/* reset value */
	outb_p(0, LP_C(minor));
	for (testvalue = 0 ; testvalue < LP_DELAY ; testvalue++)
		;
	outb_p(command, LP_C(minor));
	return LP_S(minor);
}

#ifdef LP_DEBUG
static int lp_max_count = 1;
#endif

static int lp_char_polled(char lpchar, int minor)
{
	int status = 0, wait = 0;
	unsigned long count  = 0; 

	do {
		status = LP_S(minor);
		count ++;
		if(need_resched)
			schedule();
	} while(!LP_READY(minor,status) && count < LP_CHAR(minor));

	if (count == LP_CHAR(minor)) {
		return 0;
		/* we timed out, and the character was /not/ printed */
	}
#ifdef LP_DEBUG
	if (count > lp_max_count) {
		printk("lp success after %d counts.\n",count);
		lp_max_count=count;
	}
#endif
	outb_p(lpchar, LP_B(minor));
	/* must wait before taking strobe high, and after taking strobe
	   low, according spec.  Some printers need it, others don't. */
	while(wait != LP_WAIT(minor)) wait++;
        /* control port takes strobe high */
	outb_p(( LP_PSELECP | LP_PINITP | LP_PSTROBE ), ( LP_C( minor )));
	while(wait) wait--;
        /* take strobe low */
	outb_p(( LP_PSELECP | LP_PINITP ), ( LP_C( minor )));

	return 1;
}

static int lp_char_interrupt(char lpchar, int minor)
{
	int wait = 0;
	unsigned char status;


	if (!((status = LP_S(minor)) & LP_PACK) || (status & LP_PBUSY)
	|| !((status = LP_S(minor)) & LP_PACK) || (status & LP_PBUSY)
	|| !((status = LP_S(minor)) & LP_PACK) || (status & LP_PBUSY)) {

		if (!LP_CAREFUL_READY(minor, status))
			return 0;
		outb_p(lpchar, LP_B(minor));
		/* must wait before taking strobe high, and after taking strobe
		   low, according spec.  Some printers need it, others don't. */
		while(wait != LP_WAIT(minor)) wait++;
		/* control port takes strobe high */
		outb_p(( LP_PSELECP | LP_PINITP | LP_PSTROBE ), ( LP_C( minor )));
		while(wait) wait--;
		/* take strobe low */
		outb_p(( LP_PSELECP | LP_PINITP ), ( LP_C( minor )));
		return 1;
	}

	return 0;
}

#ifdef LP_DEBUG
	unsigned int lp_total_chars = 0;
	unsigned int lp_last_call = 0;
#endif

static void lp_interrupt(int irq, struct pt_regs *regs)
{
	struct lp_struct *lp = &lp_table[0];
	struct lp_struct *lp_end = &lp_table[LP_NO];

	while (irq != lp->irq) {
		if (++lp >= lp_end)
			return;
	}

	wake_up(&lp->lp_wait_q);
}

static int lp_write_interrupt(struct inode * inode, struct file * file, const char * buf, int count)
{
	unsigned int minor = MINOR(inode->i_rdev);
	unsigned long copy_size;
	unsigned long total_bytes_written = 0;
	unsigned long bytes_written;
	struct lp_struct *lp = &lp_table[minor];
	unsigned char status;

	do {
		bytes_written = 0;
		copy_size = (count <= LP_BUFFER_SIZE ? count : LP_BUFFER_SIZE);
		memcpy_fromfs(lp->lp_buffer, buf, copy_size);

		while (copy_size) {
			if (lp_char_interrupt(lp->lp_buffer[bytes_written], minor)) {
				--copy_size;
				++bytes_written;
			} else {
				int rc = total_bytes_written + bytes_written;
				status = LP_S(minor);
				if ((status & LP_POUTPA)) {
					printk(KERN_INFO "lp%d out of paper\n", minor);
					if (LP_F(minor) & LP_ABORT)
						return rc?rc:-ENOSPC;
				} else if (!(status & LP_PSELECD)) {
					printk(KERN_INFO "lp%d off-line\n", minor);
					if (LP_F(minor) & LP_ABORT)
						return rc?rc:-EIO;
				} else if (!(status & LP_PERRORP)) {
					printk(KERN_ERR "lp%d printer error\n", minor);
					if (LP_F(minor) & LP_ABORT)
						return rc?rc:-EIO;
				}
				cli();
				outb_p((LP_PSELECP|LP_PINITP|LP_PINTEN), (LP_C(minor)));
				status = LP_S(minor);
				if ((!(status & LP_PACK) || (status & LP_PBUSY))
				  && LP_CAREFUL_READY(minor, status)) {
					outb_p((LP_PSELECP|LP_PINITP), (LP_C(minor)));
					sti();
					continue;
				}
				current->timeout = jiffies + LP_TIMEOUT_INTERRUPT;
				interruptible_sleep_on(&lp->lp_wait_q);
				outb_p((LP_PSELECP|LP_PINITP), (LP_C(minor)));
				sti();
				if (current->signal & ~current->blocked) {
					if (total_bytes_written + bytes_written)
						return total_bytes_written + bytes_written;
					else
						return -EINTR;
				}
			}
		}

		total_bytes_written += bytes_written;
		buf += bytes_written;
		count -= bytes_written;

	} while (count > 0);

	return total_bytes_written;
}

static int lp_write_polled(struct inode * inode, struct file * file,
			   const char * buf, int count)
{
	int  retval;
	unsigned int minor = MINOR(inode->i_rdev);
	char c;
	const char *temp = buf;

#ifdef LP_DEBUG
	if (jiffies-lp_last_call > LP_TIME(minor)) {
		lp_total_chars = 0;
		lp_max_count = 1;
	}
	lp_last_call = jiffies;
#endif

	temp = buf;
	while (count > 0) {
		c = get_user(temp);
		retval = lp_char_polled(c, minor);
		/* only update counting vars if character was printed */
		if (retval) { count--; temp++;
#ifdef LP_DEBUG
			lp_total_chars++;
#endif
		}
		if (!retval) { /* if printer timed out */
			int status = LP_S(minor);

			if (status & LP_POUTPA) {
				printk(KERN_INFO "lp%d out of paper\n", minor);
				if(LP_F(minor) & LP_ABORT)
					return temp-buf?temp-buf:-ENOSPC;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT_POLLED;
				schedule();
			} else
			if (!(status & LP_PSELECD)) {
				printk(KERN_INFO "lp%d off-line\n", minor);
				if(LP_F(minor) & LP_ABORT)
					return temp-buf?temp-buf:-EIO;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT_POLLED;
				schedule();
			} else
	                /* not offline or out of paper. on fire? */
			if (!(status & LP_PERRORP)) {
				printk(KERN_ERR "lp%d reported invalid error status (on fire, eh?)\n", minor);
				if(LP_F(minor) & LP_ABORT)
					return temp-buf?temp-buf:-EIO;
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
				lp_total_chars, LP_TIME(minor));
			lp_total_chars=0;
#endif
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + LP_TIME(minor);
			schedule();
		}
	}
	return temp-buf;
}

static int lp_write(struct inode * inode, struct file * file, const char * buf, int count)
{
	if (LP_IRQ(MINOR(inode->i_rdev)))
		return lp_write_interrupt(inode, file, buf, count);
	else
		return lp_write_polled(inode, file, buf, count);
}

static int lp_lseek(struct inode * inode, struct file * file,
		    off_t offset, int origin)
{
	return -ESPIPE;
}

static int lp_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int ret;
	unsigned int irq;

	if (minor >= LP_NO)
		return -ENODEV;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENODEV;
	if (LP_F(minor) & LP_BUSY)
		return -EBUSY;

	MOD_INC_USE_COUNT;

	/* If ABORTOPEN is set and the printer is offline or out of paper,
	   we may still want to open it to perform ioctl()s.  Therefore we
	   have commandeered O_NONBLOCK, even though it is being used in
	   a non-standard manner.  This is strictly a Linux hack, and
	   should most likely only ever be used by the tunelp application. */
        if ((LP_F(minor) & LP_ABORTOPEN) && !(file->f_flags & O_NONBLOCK)) {
		int status = LP_S(minor);
		if (status & LP_POUTPA) {
			printk(KERN_INFO "lp%d out of paper\n", minor);
			MOD_DEC_USE_COUNT;
			return -ENOSPC;
		} else if (!(status & LP_PSELECD)) {
			printk(KERN_INFO "lp%d off-line\n", minor);
			MOD_DEC_USE_COUNT;
			return -EIO;
		} else if (!(status & LP_PERRORP)) {
			printk(KERN_ERR "lp%d printer error\n", minor);
			MOD_DEC_USE_COUNT;
			return -EIO;
		}
	}

	if ((irq = LP_IRQ(minor))) {
		lp_table[minor].lp_buffer = (char *) kmalloc(LP_BUFFER_SIZE, GFP_KERNEL);
		if (!lp_table[minor].lp_buffer) {
			MOD_DEC_USE_COUNT;
			return -ENOMEM;
		}

		ret = request_irq(irq, lp_interrupt, SA_INTERRUPT, "printer");
		if (ret) {
			kfree_s(lp_table[minor].lp_buffer, LP_BUFFER_SIZE);
			lp_table[minor].lp_buffer = NULL;
			printk("lp%d unable to use interrupt %d, error %d\n", minor, irq, ret);
			MOD_DEC_USE_COUNT;
			return ret;
		}
	}

	LP_F(minor) |= LP_BUSY;
	return 0;
}

static void lp_release(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	unsigned int irq;

	if ((irq = LP_IRQ(minor))) {
		free_irq(irq);
		kfree_s(lp_table[minor].lp_buffer, LP_BUFFER_SIZE);
		lp_table[minor].lp_buffer = NULL;
	}

	LP_F(minor) &= ~LP_BUSY;
	MOD_DEC_USE_COUNT;
}


static int lp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int retval = 0;

#ifdef LP_DEBUG
	printk("lp%d ioctl, cmd: 0x%x, arg: 0x%x\n", minor, cmd, arg);
#endif
	if (minor >= LP_NO)
		return -ENODEV;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENODEV;
	switch ( cmd ) {
		case LPTIME:
			LP_TIME(minor) = arg;
			break;
		case LPCHAR:
			LP_CHAR(minor) = arg;
			break;
		case LPABORT:
			if (arg)
				LP_F(minor) |= LP_ABORT;
			else
				LP_F(minor) &= ~LP_ABORT;
			break;
		case LPABORTOPEN:
			if (arg)
				LP_F(minor) |= LP_ABORTOPEN;
			else
				LP_F(minor) &= ~LP_ABORTOPEN;
			break;
		case LPCAREFUL:
			if (arg)
				LP_F(minor) |= LP_CAREFUL;
			else
				LP_F(minor) &= ~LP_CAREFUL;
			break;
		case LPWAIT:
			LP_WAIT(minor) = arg;
			break;
		case LPSETIRQ: {
			int oldirq;
			int newirq = arg;
			struct lp_struct *lp = &lp_table[minor];

			if (!suser())
				return -EPERM;

			oldirq = LP_IRQ(minor);

			/* Allocate buffer now if we are going to need it */
			if (!oldirq && newirq) {
				lp->lp_buffer = (char *) kmalloc(LP_BUFFER_SIZE, GFP_KERNEL);
				if (!lp->lp_buffer)
					return -ENOMEM;
			}

			if (oldirq) {
				free_irq(oldirq);
			}
			if (newirq) {
				/* Install new irq */
				if ((retval = request_irq(newirq, lp_interrupt, SA_INTERRUPT, "printer"))) {
					if (oldirq) {
						/* restore old irq */
						request_irq(oldirq, lp_interrupt, SA_INTERRUPT, "printer");
					} else {
						/* We don't need the buffer */
						kfree_s(lp->lp_buffer, LP_BUFFER_SIZE);
						lp->lp_buffer = NULL;
					}
					return retval;
				}
			}
			if (oldirq && !newirq) {
				/* We don't need the buffer */
				kfree_s(lp->lp_buffer, LP_BUFFER_SIZE);
				lp->lp_buffer = NULL;
			}
			LP_IRQ(minor) = newirq;
			lp_reset(minor);
			break;
		}
		case LPGETIRQ:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
			    sizeof(int));
		    	if (retval)
		    		return retval;
			memcpy_tofs((int *) arg, &LP_IRQ(minor), sizeof(int));
			break;
		case LPGETSTATUS:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
			    sizeof(int));
		    	if (retval)
		    		return retval;
			else {
				int status = LP_S(minor);
				memcpy_tofs((int *) arg, &status, sizeof(int));
			}
			break;
		case LPRESET:
			lp_reset(minor);
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

#ifdef MODULE
char kernel_version[]=UTS_RELEASE;

int init_module(void)
#else
long lp_init(long kmem_start)
#endif
{
	int offset = 0;
	unsigned int testvalue = 0;
	int count = 0;

	if (register_chrdev(LP_MAJOR,"lp",&lp_fops)) {
		printk("lp: unable to get major %d\n", LP_MAJOR);
#ifdef MODULE
		return -EIO;
#else		
		return kmem_start;
#endif
	}
	/* take on all known port values */
	for (offset = 0; offset < LP_NO; offset++) {
		if (check_region(LP_B(offset), 3))
			continue;
		/* write to port & read back to check */
		outb_p( LP_DUMMY, LP_B(offset));
		for (testvalue = 0 ; testvalue < LP_DELAY ; testvalue++)
			;
		testvalue = inb_p(LP_B(offset));
		if (testvalue == LP_DUMMY) {
			LP_F(offset) |= LP_EXIST;
			lp_reset(offset);
			printk("lp%d at 0x%04x, ", offset,LP_B(offset));
			request_region(LP_B(offset), 3, "lp");
			if (LP_IRQ(offset))
				printk("(irq = %d)\n", LP_IRQ(offset));
			else
				printk("(polling)\n");
			count++;
		}
	}
	if (count == 0)
		printk("lp: Driver configured but no interfaces found.\n");


#ifdef MODULE
	return 0;
#else	
	return kmem_start;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
        int offset;
	if(MOD_IN_USE)
               printk("lp: busy - remove delayed\n");
        else {
               unregister_chrdev(LP_MAJOR,"lp");
	       for (offset = 0; offset < LP_NO; offset++) 
			if (LP_F(offset) & LP_EXIST) 
		 		release_region(LP_B(offset),3);
	}
}

#endif
