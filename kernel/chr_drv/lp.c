/*
 $Header: /usr/src/linux/kernel/chr_drv/lp.c,v 1.9 1992/01/06 16:11:19
  james_r_wiegand Exp james_r_wiegand $
*/

/*
 * Edited by Linus - cleaner interface etc. Still not using interrupts, so
 * it eats more resources than necessary, but it was easy to code this way...
 */

#include <linux/sched.h>
#define __LP_C__
#include <linux/lp.h>

static int lp_reset(int minor)
{
	int testvalue;

	/* reset value */
	outb(0, LP_B(minor)+2);
	for (testvalue = 0 ; testvalue < LP_DELAY ; testvalue++)
		;
	outb(LP_PSELECP | LP_PINITP, LP_B(minor)+2);
	return LP_S(minor);
}

static int lp_char(char lpchar, int minor)
{
	int retval = 0;
	unsigned long count  = 0; 

	outb(lpchar, LP_B(minor));
	do {
		retval = LP_S(minor);
		schedule(); 
		count ++;
	} while(!(retval & LP_PBUSY) && count < LP_TIMEOUT);
	if (count == LP_TIMEOUT) {
		printk("lp%d timeout\n\r", minor);
		return 0;
	}
  /* control port pr_table[0]+2 take strobe high */
	outb(( LP_PSELECP | LP_PINITP | LP_PSTROBE ), ( LP_B( minor ) + 2 ));
  /* take strobe low */
	outb(( LP_PSELECP | LP_PINITP ), ( LP_B( minor ) + 2 ));
  /* get something meaningful for return value */
	return LP_S(minor);
}

static int lp_write(struct inode * inode, struct file * file, char * buf, int count)
{
	int  retval;
	unsigned int minor = MINOR(inode->i_rdev);
	char c, *temp = buf;

	temp = buf;
	while (count > 0) {
		c = get_fs_byte(temp++);
		retval = lp_char(c, minor);
		count--;
		if (retval & LP_POUTPA) {
			LP_F(minor) |= LP_NOPA;
			return temp-buf?temp-buf:-ENOSPC;
		} else
			LP_F(minor) &= ~LP_NOPA;

		if (!(retval & LP_PSELECD)) {
			LP_F(minor) &= ~LP_SELEC;
			return temp-buf?temp-buf:-EFAULT;
		} else
			LP_F(minor) &= ~LP_SELEC;

    /* not offline or out of paper. on fire? */
		if (!(retval & LP_PERRORP)) {
			LP_F(minor) |= LP_ERR;
			return temp-buf?temp-buf:-EIO;
		} else
			LP_F(minor) &= ~LP_SELEC;
	}
	return temp-buf;
}

static int lp_read(struct inode * inode, struct file * file, char * buf, int count)
{
	return -EINVAL;
}

static int lp_lseek(struct inode * inode, struct file * file, off_t offset, int origin)
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

static struct file_operations lp_fops = {
	lp_lseek,
	lp_read,
	lp_write,
	NULL,		/* lp_readdir */
	NULL,		/* lp_select */
	NULL,		/* lp_ioctl */
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
