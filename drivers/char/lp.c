/*
 * Copyright (C) 1992 by Jim Weigand and Linus Torvalds
 * Copyright (C) 1992,1993 by Michael K. Johnson
 * - Thanks much to Gunter Windau for pointing out to me where the error
 *   checking ought to be.
 * Copyright (C) 1993 by Nigel Gamble (added interrupt code)
 * Copyright (C) 1994 by Alan Cox (Modularised it)
 * LPCAREFUL, LPABORT, LPGETSTATUS added by Chris Metcalf, metcalf@lcs.mit.edu
 * Statistics and support for slow printers by Rob Janssen, rob@knoware.nl
 * "lp=" command line parameters added by Grant Guenther, grant@torque.net
 */

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/lp.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>

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
	{ 0x3bc, 0, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, 0, 0, 0, {0} },
	{ 0x378, 0, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, 0, 0, 0, {0} },
	{ 0x278, 0, 0, LP_INIT_CHAR, LP_INIT_TIME, LP_INIT_WAIT, NULL, NULL, 0, 0, 0, {0} },
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
 * Debug info moved into stats area, so this is no longer true (Rob Janssen)
 */

#undef LP_DEBUG

static int lp_reset(int minor)
{
	outb_p(LP_PSELECP, LP_C(minor));
	udelay(LP_DELAY);
	outb_p(LP_PSELECP | LP_PINITP, LP_C(minor));
	return LP_S(minor);
}

static inline int lp_char_polled(char lpchar, int minor)
{
	int status, wait = 0;
	unsigned long count  = 0;
	struct lp_stats *stats;

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
	outb_p(lpchar, LP_B(minor));
	stats = &LP_STAT(minor);
	stats->chars++;
	/* must wait before taking strobe high, and after taking strobe
	   low, according spec.  Some printers need it, others don't. */
	while(wait != LP_WAIT(minor)) wait++;
	/* control port takes strobe high */
	outb_p(( LP_PSELECP | LP_PINITP | LP_PSTROBE ), ( LP_C( minor )));
	while(wait) wait--;
	/* take strobe low */
	outb_p(( LP_PSELECP | LP_PINITP ), ( LP_C( minor )));
	/* update waittime statistics */
	if (count > stats->maxwait) {
#ifdef LP_DEBUG
	    printk(KERN_DEBUG "lp%d success after %d counts.\n",minor,count);
#endif
	    stats->maxwait = count;
	}
	count *= 256;
	wait = (count > stats->meanwait)? count - stats->meanwait :
					  stats->meanwait - count;
	stats->meanwait = (255*stats->meanwait + count + 128) / 256;
	stats->mdev = ((127 * stats->mdev) + wait + 64) / 128;

	return 1;
}

static inline int lp_char_interrupt(char lpchar, int minor)
{
	int wait;
	unsigned long count = 0;
	unsigned char status;
	struct lp_stats *stats;

	do {
	    if ((status = LP_S(minor)) & LP_PBUSY) {
		if (!LP_CAREFUL_READY(minor, status))
			return 0;
		outb_p(lpchar, LP_B(minor));
		stats = &LP_STAT(minor);
		stats->chars++;
		/* must wait before taking strobe high, and after taking strobe
		   low, according spec.  Some printers need it, others don't. */
		wait = 0;
		while(wait != LP_WAIT(minor)) wait++;
		/* control port takes strobe high */
		outb_p(( LP_PSELECP | LP_PINITP | LP_PSTROBE ), ( LP_C( minor )));
		while(wait) wait--;
		/* take strobe low */
		outb_p(( LP_PSELECP | LP_PINITP ), ( LP_C( minor )));
		/* update waittime statistics */
		if (count) {
		    if (count > stats->maxwait)
			stats->maxwait = count;
		    count *= 256;
		    wait = (count > stats->meanwait)? count - stats->meanwait :
						      stats->meanwait - count;
		    stats->meanwait = (255*stats->meanwait + count + 128) / 256;
		    stats->mdev = ((127 * stats->mdev) + wait + 64) / 128;
		}
		return 1;
	    }
	} while (count++ < LP_CHAR(minor));

	return 0;
}

static void lp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct lp_struct *lp = &lp_table[0];

	while (irq != lp->irq) {
		if (++lp >= &lp_table[LP_NO])
			return;
	}

	wake_up(&lp->lp_wait_q);
}

static inline int lp_write_interrupt(unsigned int minor, const char * buf, int count)
{
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
				lp_table[minor].runchars++;
			} else {
				int rc = total_bytes_written + bytes_written;
				if (lp_table[minor].runchars > LP_STAT(minor).maxrun)
					 LP_STAT(minor).maxrun = lp_table[minor].runchars;
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
				LP_STAT(minor).sleeps++;
				cli();
				outb_p((LP_PSELECP|LP_PINITP|LP_PINTEN), (LP_C(minor)));
				status = LP_S(minor);
				if ((!(status & LP_PACK) || (status & LP_PBUSY))
				  && LP_CAREFUL_READY(minor, status)) {
					outb_p((LP_PSELECP|LP_PINITP), (LP_C(minor)));
					sti();
					continue;
				}
				lp_table[minor].runchars=0;
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

static inline int lp_write_polled(unsigned int minor, const char * buf, int count)
{
	int  retval,status;
	char c;
	const char *temp;

	temp = buf;
	while (count > 0) {
		c = get_user(temp);
		retval = lp_char_polled(c, minor);
		/* only update counting vars if character was printed */
		if (retval) {
			count--; temp++;
			lp_table[minor].runchars++;
		} else { /* if printer timed out */
			if (lp_table[minor].runchars > LP_STAT(minor).maxrun)
				 LP_STAT(minor).maxrun = lp_table[minor].runchars;
			status = LP_S(minor);

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
			LP_STAT(minor).sleeps++;
#ifdef LP_DEBUG
			printk(KERN_DEBUG "lp%d sleeping at %d characters for %d jiffies\n",
				minor,lp_table[minor].runchars, LP_TIME(minor));
#endif
			lp_table[minor].runchars=0;
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + LP_TIME(minor);
			schedule();
		}
	}
	return temp-buf;
}

static int lp_write(struct inode * inode, struct file * file, const char * buf, int count)
{
	unsigned int minor = MINOR(inode->i_rdev);

	if (jiffies-lp_table[minor].lastcall > LP_TIME(minor))
		lp_table[minor].runchars = 0;
	lp_table[minor].lastcall = jiffies;

	if (LP_IRQ(minor))
		return lp_write_interrupt(minor, buf, count);
	else
		return lp_write_polled(minor, buf, count);
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
		return -ENXIO;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENXIO;
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

		ret = request_irq(irq, lp_interrupt, SA_INTERRUPT, "printer", NULL);
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
		free_irq(irq, NULL);
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
	printk(KERN_DEBUG "lp%d ioctl, cmd: 0x%x, arg: 0x%x\n", minor, cmd, arg);
#endif
	if (minor >= LP_NO)
		return -ENODEV;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENODEV;
	switch ( cmd ) {
		case LPTIME:
			LP_TIME(minor) = arg * HZ/100;
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
				free_irq(oldirq, NULL);
			}
			if (newirq) {
				/* Install new irq */
				if ((retval = request_irq(newirq, lp_interrupt, SA_INTERRUPT, "printer", NULL))) {
					if (oldirq) {
						/* restore old irq */
						request_irq(oldirq, lp_interrupt, SA_INTERRUPT, "printer", NULL);
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
		case LPGETSTATS:
			retval = verify_area(VERIFY_WRITE, (void *) arg,
			    sizeof(struct lp_stats));
		    	if (retval)
		    		return retval;
			else {
				memcpy_tofs((int *) arg, &LP_STAT(minor), sizeof(struct lp_stats));
				if (suser())
					memset(&LP_STAT(minor), 0, sizeof(struct lp_stats));
			}
			break;
 		case LPGETFLAGS:
 			retval = verify_area(VERIFY_WRITE, (void *) arg,
 			    sizeof(int));
 		    	if (retval)
 		    		return retval;
 			else {
 				int status = LP_F(minor);
				memcpy_tofs((int *) arg, &status, sizeof(int));
			}
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

static int lp_probe(int offset)
{
	int base, size;
	unsigned int testvalue;

	base = LP_B(offset);
	if (base == 0) 
		return -1;		/* de-configured by command line */
	if (LP_IRQ(offset) > 15) 
		return -1;		/* bogus interrupt value */
	size = (base == 0x3bc)? 3 : 8;
	if (check_region(base, size) < 0)
		return -1;
	/* write to port & read back to check */
	outb_p(LP_DUMMY, base);
	udelay(LP_DELAY);
	testvalue = inb_p(base);
	if (testvalue == LP_DUMMY) {
		LP_F(offset) |= LP_EXIST;
		lp_reset(offset);
		printk(KERN_INFO "lp%d at 0x%04x, ", offset, base);
		request_region(base, size, "lp");
		if (LP_IRQ(offset))
			printk("(irq = %d)\n", LP_IRQ(offset));
		else
			printk("(polling)\n");
		return 1;
	} else
		return 0;
}

/* Command line parameters:

   When the lp driver is built in to the kernel, you may use the
   LILO/LOADLIN command line to set the port addresses and interrupts
   that the driver will use.

   Syntax:	lp=port0[,irq0[,port1[,irq1[,port2[,irq2]]]]]

   For example:   lp=0x378,0   or   lp=0x278,5,0x378,7

   Note that if this feature is used, you must specify *all* the ports
   you want considered, there are no defaults.  You can disable a
   built-in driver with lp=0 .

*/

void	lp_setup(char *str, int *ints)

{	
        LP_B(0)   = ((ints[0] > 0) ? ints[1] : 0 );
        LP_IRQ(0) = ((ints[0] > 1) ? ints[2] : 0 );
        LP_B(1)   = ((ints[0] > 2) ? ints[3] : 0 );
        LP_IRQ(1) = ((ints[0] > 3) ? ints[4] : 0 );
        LP_B(2)   = ((ints[0] > 4) ? ints[5] : 0 );
        LP_IRQ(2) = ((ints[0] > 5) ? ints[6] : 0 );
}

#ifdef MODULE
static int io[] = {0, 0, 0};
static int irq[] = {0, 0, 0};

#define lp_init init_module
#endif

int lp_init(void)
{
	int offset = 0;
	int count = 0;
#ifdef MODULE
	int failed = 0;
#endif

	if (register_chrdev(LP_MAJOR,"lp",&lp_fops)) {
		printk("lp: unable to get major %d\n", LP_MAJOR);
		return -EIO;
	}
#ifdef MODULE
	/* When user feeds parameters, use them */
	for (offset=0; offset < LP_NO; offset++) {
		int specified=0;

		if (io[offset] != 0) {
			LP_B(offset) = io[offset];
			specified++;
		}
		if (irq[offset] != 0) {
			LP_IRQ(offset) = irq[offset];
			specified++;
		}
		if (specified) {
			if (lp_probe(offset) <= 0) {
				printk(KERN_INFO "lp%d: Not found\n", offset);
				failed++;
			} else
				count++;
		}
	}
	/* Successful specified devices increase count
	 * Unsuccessful specified devices increase failed
	 */
	if (count)
		return 0;
	if (failed) {
		printk(KERN_INFO "lp: No override devices found.\n");
		unregister_chrdev(LP_MAJOR,"lp");
		return -EIO;
	}
	/* Only get here if there were no specified devices. To continue 
	 * would be silly since the above code has scribbled all over the
	 * probe list.
	 */
#endif
	/* take on all known port values */
	for (offset = 0; offset < LP_NO; offset++) {
		int ret = lp_probe(offset);
		if (ret < 0)
			continue;
		count += ret;
	}
	if (count == 0)
		printk("lp: Driver configured but no interfaces found.\n");

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	int offset;

	unregister_chrdev(LP_MAJOR,"lp");
	for (offset = 0; offset < LP_NO; offset++) {
		int base, size;
		base = LP_B(offset);
		size = (base == 0x3bc)? 3 : 8;
		if (LP_F(offset) & LP_EXIST)
			release_region(LP_B(offset),size);
	}
}
#endif
