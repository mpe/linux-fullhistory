/*
 * linux/drivers/char/mouse.c
 *
 * Copyright (C) 1995 - 1998 Russell King
 *  Protocol taken from busmouse.c
 *  read() waiting taken from psaux.c
 *
 * Medium-level interface for quadrature or bus mice.
 *
 * Currently, the majority of kernel busmice drivers in the
 * kernel common code to talk to userspace.  This driver
 * attempts to rectify this situation by presenting a
 * simple and safe interface to the mice and user.
 *
 * This driver:
 *  - is SMP safe
 *  - handles multiple opens
 *  - handles the wakeups and locking
 *  - has optional blocking reads
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>

#include "busmouse.h"

/* Uncomment this if your mouse drivers expect the kernel to
 * return with EAGAIN if the mouse does not have any events
 * available, even if the mouse is opened in nonblocking mode.
 *
 * Should this be on a per-mouse basis?  If so, add an entry to
 * the struct busmouse structure and add the relevent flag to
 * the drivers.
 */
/*#define BROKEN_MOUSE*/

struct busmouse_data {
	struct miscdevice	miscdev;
	struct busmouse		*ops;
	spinlock_t		lock;

	wait_queue_head_t	wait;
	struct fasync_struct	*fasyncptr;
	char			active;
	char			buttons;
	char			latch_buttons;
	char			ready;
	int			dxpos;
	int			dypos;
};

#define NR_MICE			15
#define FIRST_MOUSE		0
#define DEV_TO_MOUSE(dev)	MINOR_TO_MOUSE(MINOR(dev))
#define MINOR_TO_MOUSE(minor)	((minor) - FIRST_MOUSE)

/*
 *	List of mice and guarding semaphore. You must take the semaphore
 *	before you take the misc device semaphore if you need both
 */
 
static struct busmouse_data *busmouse_data[NR_MICE];
static DECLARE_MUTEX(mouse_sem);

/* a mouse driver just has to interface with these functions
 *  These are !!!OLD!!!  Do not use!!!
 */
void add_mouse_movement(int dx, int dy)
{
	struct busmouse_data *mse = busmouse_data[MINOR_TO_MOUSE(6)];

	mse->dxpos += dx;
	mse->dypos += dy;
	mse->ready = 1;
	wake_up(&mse->wait);
}

int add_mouse_buttonchange(int set, int value)
{
	struct busmouse_data *mse = busmouse_data[MINOR_TO_MOUSE(6)];

	mse->buttons = (mse->buttons & ~set) ^ value;
	mse->ready = 1;
	wake_up(&mse->wait);
	return mse->buttons;
}

/* New interface.  !!! Use this one !!!
 * These routines will most probably be called from interrupt.
 */
void busmouse_add_movementbuttons(int mousedev, int dx, int dy, int buttons)
{
	struct busmouse_data *mse = busmouse_data[mousedev];
	int changed;

	spin_lock(&mse->lock);
	changed = (dx != 0 || dy != 0 || mse->buttons != buttons);

	if (changed) {
		add_mouse_randomness((buttons << 16) + (dy << 8) + dx);

		mse->buttons = buttons;
//		mse->latch_buttons |= buttons;
		mse->dxpos += dx;
		mse->dypos += dy;
		mse->ready = 1;

		/*
		 * keep dx/dy reasonable, but still able to track when X (or
		 * whatever) must page or is busy (i.e. long waits between
		 * reads)
		 */
		if (mse->dxpos < -2048)
			mse->dxpos = -2048;
		if (mse->dxpos > 2048)
			mse->dxpos = 2048;
		if (mse->dypos < -2048)
			mse->dypos = -2048;
		if (mse->dypos > 2048)
			mse->dypos = 2048;
	}

	spin_unlock(&mse->lock);

	if (changed) {
		wake_up(&mse->wait);

		if (mse->fasyncptr)
			kill_fasync(mse->fasyncptr, SIGIO, POLL_IN);
	}
}

void busmouse_add_movement(int mousedev, int dx, int dy)
{
	struct busmouse_data *mse = busmouse_data[mousedev];

	busmouse_add_movementbuttons(mousedev, dx, dy, mse->buttons);
}

void busmouse_add_buttons(int mousedev, int clear, int eor)
{
	struct busmouse_data *mse = busmouse_data[mousedev];

	busmouse_add_movementbuttons(mousedev, 0, 0, (mse->buttons & ~clear) ^ eor);
}

static int busmouse_fasync(int fd, struct file *filp, int on)
{
	struct busmouse_data *mse = (struct busmouse_data *)filp->private_data;
	int retval;

	retval = fasync_helper(fd, filp, on, &mse->fasyncptr);
	if (retval < 0)
		return retval;
	return 0;
}

static int busmouse_release(struct inode *inode, struct file *file)
{
	struct busmouse_data *mse = (struct busmouse_data *)file->private_data;
	int ret = 0;

	busmouse_fasync(-1, file, 0);

	if (--mse->active == 0) {
		if (mse->ops &&
		    mse->ops->release)
			ret = mse->ops->release(inode, file);

		mse->ready = 0;

		MOD_DEC_USE_COUNT;
	}

	return ret;
}

static int busmouse_open(struct inode *inode, struct file *file)
{
	struct busmouse_data *mse;
	unsigned long flags;
	unsigned int mousedev;
	int ret = -ENODEV;

	mousedev = DEV_TO_MOUSE(inode->i_rdev);
	if (mousedev >= NR_MICE)
		return -EINVAL;
	
	down(&mouse_sem);
	mse = busmouse_data[mousedev];
	if (!mse)
		/* shouldn't happen, but... */
		goto end;
	
	if (mse->ops &&
	    mse->ops->open)
		ret = mse->ops->open(inode, file);

	if (ret)
		goto end;

	file->private_data = mse;

	if (mse->active++)
		goto end;

	MOD_INC_USE_COUNT;

	spin_lock_irqsave(&mse->lock, flags);

	mse->ready   = 0;
	mse->dxpos   = 0;
	mse->dypos   = 0;
	if (mse->ops)
		mse->buttons = mse->ops->init_button_state;
	else
		mse->buttons = 7;

	spin_unlock_irqrestore(&mse->lock, flags);
end:
	up(&mouse_sem);
	return ret;
}

static ssize_t busmouse_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t busmouse_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct busmouse_data *mse = (struct busmouse_data *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int dxpos, dypos, buttons;

	if (count < 3)
		return -EINVAL;

	spin_lock_irqsave(&mse->lock, flags);

	if (!mse->ready) {
#ifdef BROKEN_MOUSE
		spin_unlock_irqrestore(&mse->lock, flags);
		return -EAGAIN;
#else
		if (file->f_flags & O_NONBLOCK) {
			spin_unlock_irqrestore(&mse->lock, flags);
			return -EAGAIN;
		}

		add_wait_queue(&mse->wait, &wait);
repeat:
		set_current_state(TASK_INTERRUPTIBLE);
		if (!mse->ready && !signal_pending(current)) {
			spin_unlock_irqrestore(&mse->lock, flags);
			schedule();
			spin_lock_irqsave(&mse->lock, flags);
			goto repeat;
		}

		current->state = TASK_RUNNING;
		remove_wait_queue(&mse->wait, &wait);

		if (signal_pending(current)) {
			spin_unlock_irqrestore(&mse->lock, flags);
			return -ERESTARTSYS;
		}
#endif
	}

	dxpos = mse->dxpos;
	dypos = mse->dypos;
	buttons = mse->buttons;
//	mse->latch_buttons = mse->buttons;

	if (dxpos < -127)
		dxpos =- 127;
	if (dxpos > 127)
		dxpos = 127;
	if (dypos < -127)
		dypos =- 127;
	if (dypos > 127)
		dypos = 127;

	mse->dxpos -= dxpos;
	mse->dypos -= dypos;

	/* This is something that many drivers have apparantly
	 * forgotten...  If the X and Y positions still contain
	 * information, we still have some info ready for the
	 * user program...
	 */
	mse->ready = mse->dxpos || mse->dypos;

	spin_unlock_irqrestore(&mse->lock, flags);

	/* Write out data to the user.  Format is:
	 *   byte 0 - identifer (0x80) and (inverted) mouse buttons
	 *   byte 1 - X delta position +/- 127
	 *   byte 2 - Y delta position +/- 127
	 */
	if (put_user((char)buttons | 128, buffer) ||
	    put_user((char)dxpos, buffer + 1) ||
	    put_user((char)dypos, buffer + 2))
		return -EFAULT;

	if (count > 3 && clear_user(buffer + 3, count - 3))
		return -EFAULT;

	file->f_dentry->d_inode->i_atime = CURRENT_TIME;

	return count;
}

/* No kernel lock held - fine */
static unsigned int busmouse_poll(struct file *file, poll_table *wait)
{
	struct busmouse_data *mse = (struct busmouse_data *)file->private_data;

	poll_wait(file, &mse->wait, wait);

	if (mse->ready)
		return POLLIN | POLLRDNORM;

	return 0;
}

struct file_operations busmouse_fops=
{
	read:		busmouse_read,
	write:		busmouse_write,
	poll:		busmouse_poll,
	open:		busmouse_open,
	release:	busmouse_release,
	fasync:		busmouse_fasync,
};

/**
 *	register_busmouse - register a bus mouse interface
 *	@ops: busmouse structure for the mouse
 *
 *	Registers a mouse with the driver. The return is mouse number on
 *	success and a negative errno code on an error. The passed ops
 *	structure most not be freed until the mouser is unregistered
 */
 
int register_busmouse(struct busmouse *ops)
{
	unsigned int msedev = MINOR_TO_MOUSE(ops->minor);
	struct busmouse_data *mse;
	int ret;

	if (msedev >= NR_MICE) {
		printk(KERN_ERR "busmouse: trying to allocate mouse on minor %d\n",
		       ops->minor);
		return -EINVAL;
	}

	mse = kmalloc(sizeof(*mse), GFP_KERNEL);
	if (!mse)
		return -ENOMEM;

	down(&mouse_sem);
	if (busmouse_data[msedev])
	{
		up(&mouse_sem);
		kfree(mse);
		return -EBUSY;
	}

	memset(mse, 0, sizeof(*mse));

	mse->miscdev.minor = ops->minor;
	mse->miscdev.name = ops->name;
	mse->miscdev.fops = &busmouse_fops;
	mse->ops = ops;
	mse->lock = (spinlock_t)SPIN_LOCK_UNLOCKED;
	init_waitqueue_head(&mse->wait);

	busmouse_data[msedev] = mse;

	ret = misc_register(&mse->miscdev);
	if (!ret)
		ret = msedev;
	up(&mouse_sem);
	
	return ret;
}

/**
 *	unregister_busmouse - unregister a bus mouse interface
 *	@mousedev: Mouse number to release
 *
 *	Unregister a previously installed mouse handler. The mousedev
 *	passed is the return code from a previous call to register_busmouse
 */
 

int unregister_busmouse(int mousedev)
{
	int err = -EINVAL;
	if (mousedev < 0)
		return 0;
	if (mousedev >= NR_MICE) {
		printk(KERN_ERR "busmouse: trying to free mouse on"
		       " mousedev %d\n", mousedev);
		return -EINVAL;
	}

	down(&mouse_sem);
	
	if (!busmouse_data[mousedev]) {
		printk(KERN_WARNING "busmouse: trying to free free mouse"
		       " on mousedev %d\n", mousedev);
		goto fail;
	}

	if (busmouse_data[mousedev]->active) {
		printk(KERN_ERR "busmouse: trying to free active mouse"
		       " on mousedev %d\n", mousedev);
		goto fail;
	}

	err=misc_deregister(&busmouse_data[mousedev]->miscdev);

	kfree(busmouse_data[mousedev]);
	busmouse_data[mousedev] = NULL;
fail:
	up(&mouse_sem);
	return err;
}

EXPORT_SYMBOL(busmouse_add_movementbuttons);
EXPORT_SYMBOL(busmouse_add_movement);
EXPORT_SYMBOL(busmouse_add_buttons);
EXPORT_SYMBOL(register_busmouse);
EXPORT_SYMBOL(unregister_busmouse);
