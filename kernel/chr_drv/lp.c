/* Copyright (C) 1992 by Jim Weigand, Linus Torvalds, and Michael K. Johnson
*/

#include <linux/lp.h>
/* sched.h is included from lp.h */

/* 
 * All my debugging code assumes that you debug with only one printer at
 * a time. RWWH
 */

#undef LP_DEBUG

static int lp_reset(int minor)
{
	int testvalue;

	/* reset value */
	outb(0, LP_C(minor));
	for (testvalue = 0 ; testvalue < LP_DELAY ; testvalue++)
		;
	outb(LP_PSELECP | LP_PINITP, LP_C(minor));
	return LP_S(minor);
}

#ifdef LP_DEBUG
static int lp_max_count = 1;
#endif

static int lp_char(char lpchar, int minor)
{
	int retval = 0, wait = 0;
	unsigned long count  = 0; 

	outb(lpchar, LP_B(minor));
	do {
		retval = LP_S(minor);
		count ++;
		if(need_resched)
			schedule();
	} while(!(retval & LP_PBUSY) && count < LP_CHAR(minor));

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
	/* must wait before taking strobe high, and after taking strobe
	   low, according spec.  Some printers need it, others don't. */
	while(wait != LP_WAIT(minor)) wait++;
        /* control port takes strobe high */
	outb(( LP_PSELECP | LP_PINITP | LP_PSTROBE ), ( LP_C( minor )));
	while(wait) wait--;
        /* take strobe low */
	outb(( LP_PSELECP | LP_PINITP ), ( LP_C( minor )));
       /* get something meaningful for return value */
	return LP_S(minor);
}

#ifdef LP_DEBUG
	unsigned int lp_total_chars = 0;
	unsigned int lp_last_call = 0;
#endif

static int lp_write(struct inode * inode, struct file * file, char * buf, int count)
{
	int  retval;
	unsigned int minor = MINOR(inode->i_rdev);
	char c, *temp = buf;

#ifdef LP_DEBUG
	if (jiffies-lp_last_call > LP_TIME(minor)) {
		lp_total_chars = 0;
		lp_max_count = 1;
	}
	lp_last_call = jiffies;
#endif

	temp = buf;
	while (count > 0) {
		c = get_fs_byte(temp);
		retval = lp_char(c, minor);
		/* only update counting vars if character was printed */
		if (retval) { count--; temp++;
#ifdef LP_DEBUG
			lp_total_chars++;
#endif
		}
		if (!retval) { /* if printer timed out */
			/* check for signals before going to sleep */
			if (current->signal & ~current->blocked) {
				if (count > 0) return -EINTR;
			}
#ifdef LP_DEBUG
			printk("lp sleeping at %d characters for %d jiffies\n",
				lp_total_chars, LP_TIME(minor));
			lp_total_chars=0;
#endif
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + LP_TIME(minor);
			schedule();

			/* If nothing is getting to the printer
			   for a considerable length of time,
			   someone oughtta know.  */
			if (!(LP_S(minor) & LP_BUSY)) {
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT;
				schedule();
				if (!(LP_S(minor) & LP_BUSY))
					printk("lp%d timeout\n", minor);
			}
		} else {
			if (retval & LP_POUTPA) {
				printk("lp%d out of paper\n", minor);
				if(LP_F(minor) && LP_ABORT)
					return temp-buf?temp-buf:-ENOSPC;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT;
				schedule();
			} else

			if (!(retval & LP_PSELECD)) {
				printk("lp%d off-line\n", minor);
				if(LP_F(minor) && LP_ABORT)
					return temp-buf?temp-buf:-EIO;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT;
				schedule();
			} else

	                /* not offline or out of paper. on fire? */
			if (!(retval & LP_PERRORP)) {
				printk("lp%d on fire\n", minor);
				if(LP_F(minor) && LP_ABORT)
					return temp-buf?temp-buf:-EFAULT;
				current->state = TASK_INTERRUPTIBLE;
				current->timeout = jiffies + LP_TIMEOUT;
				schedule();
			}
		}
	}
	return temp-buf;
}


static int lp_lseek(struct inode * inode, struct file * file,
		    off_t offset, int origin)
{
	return -EINVAL;
}

static int lp_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	if (minor >= LP_NO)
		return -ENODEV;
	if ((LP_F(minor) & LP_EXIST) == 0)
		return -ENODEV;
	if (LP_F(minor) & LP_BUSY)
		return -EBUSY;
	LP_F(minor) |= LP_BUSY;
	return 0;
}

static void lp_release(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	LP_F(minor) &= ~LP_BUSY;
}


static int lp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned int arg)
{
	unsigned int minor = MINOR(inode->i_rdev);

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
			if(arg)
				LP_F(minor) |= LP_ABORT;
			else	LP_F(minor) &= ~LP_ABORT;
			break;
		case LPWAIT:
			LP_WAIT(minor) = arg;
			break;
		default: arg = -EINVAL;
	}
	return arg;
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

long lp_init(long kmem_start)
{
	int offset = 0;
	unsigned int testvalue = 0;
	int count = 0;

	chrdev_fops[6] = &lp_fops;
	/* take on all known port values */
	for (offset = 0; offset < LP_NO; offset++) {
		/* write to port & read back to check */
		outb( LP_DUMMY, LP_B(offset));
		for (testvalue = 0 ; testvalue < LP_DELAY ; testvalue++)
			;
		testvalue = inb(LP_B(offset));
		if (testvalue != 255) {
			LP_F(offset) |= LP_EXIST;
			lp_reset(offset);
			printk("lp_init: lp%d exists (%d)\n", offset, testvalue);
			count++;
		}
	}
	if (count == 0)
		printk("lp_init: no lp devices found\n");
	return kmem_start;
}
