/*

   linux/drivers/char/joystick.c
   Copyright (C) 1992, 1993 Arthur C. Smith
   Joystick driver for Linux running on an IBM compatible computer.

VERSION INFO:
01/08/93	ACS	0.1: Works but needs multi-joystick support
01/13/93	ACS	0.2: Added multi-joystick support (minor 0 and 1)
		  	     Added delay between measuring joystick axis
		   	     Added scaling ioctl
02/16/93	ACS	0.3: Modified scaling to use ints to prevent kernel
			     panics 8-)
02/28/93	ACS	0.4: Linux99.6 and fixed race condition in js_read.
			     After looking at a schematic of a joystick card
                             it became apparent that any write to the joystick
			     port started ALL the joystick one shots. If the
			     one that we are reading is short enough and the
			     first one to be read, the second one will return
			     bad data if it's one shot has not expired when
			     the joystick port is written for the second time.
			     Thus solves the mystery delay problem in 0.2!
05/05/93       ACS/Eyal 0.5: Upgraded the driver to the 99.9 kernel, added
			     joystick support to the make config options,
			     updated the driver to return the buttons as
			     positive logic, and read both axis at once
			     (thanks Eyal!), and added some new ioctls.
02/12/94   Jeff Tranter 0.6: Made necessary changes to work with 0.99pl15
                             kernel (and hopefully 1.0). Also did some
			     cleanup: indented code, fixed some typos, wrote
			     man page, etc...
05/17/95 Dan Fandrich 0.7.3: Added I/O port registration, cleaned up code
04/03/96    Matt Rhoten 0.8: many minor changes:
			     new read loop from Hal Maney <maney@norden.com>
                             cleaned up #includes to allow #include of 
                             joystick.h with gcc -Wall and from g++
			     made js_init fail if it finds zero joysticks
			     general source/comment cleanup
			     use of MOD_(INC|DEC)_USE_COUNT
			     changes from Bernd Schmidt <crux@Pool.Informatik.RWTH-Aachen.DE>
			     to compile correctly under 1.3 in kernel or as module
06/30/97       Alan Cox 0.9: Ported to 2.1.x
			     Reformatted to resemble Linux coding standard
			     Removed semaphore bug (we can dump the lot I think)
			     Fixed xntp timer adjust during joystick timer0 bug
			     Changed variable names to lower case. Kept binary
			     	compatibility.
			     Better ioctl names. Kept binary compatibility.
			     Removed 'save_busy'. Just set busy to 1.
11/03/97  Brian Gerst 0.9.1: Fixed bug which caused driver to always time out 
                             but never report a timeout (broken while loop).
                             Fixed js_read for new VFS code.
*/

#include <linux/module.h>
#include <linux/joystick.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/uaccess.h>

static struct js_config js_data[JS_MAX];	/* misc data */
static int js_exist;			/* which joysticks' axis exist? */
static int js_read_semaphore;	/* to prevent two processes from trying
				   to read different joysticks at the
				   same time */

/* 
 *	get_timer0():
 *	returns the current value of timer 0. This is a 16 bit counter that starts
 *	at LATCH and counts down to 0 
 */
   
extern inline int get_timer0(void)
{
	unsigned long flags;
	int t0, t1;
	save_flags(flags);
	cli();
	outb (0, PIT_MODE);
	t0 = (int) inb (PIT_COUNTER_0);
	t1 = ((int) inb (PIT_COUNTER_0) << 8) + t0;
	restore_flags(flags);
	return (t1);
}

/*
 *	find_axes():
 *
 *	returns which axes are hooked up, in a bitfield. 2^n is set if
 *	axis n is hooked up, for 0 <= n < 4.
 *
 *	REVIEW: should update this to handle eight-axis (four-stick) game port
 *	cards. anyone have one of these to test on? mattrh 3/23/96 
 */
 
extern inline int find_axes(void)
{
	int j;
	outb (0xff, JS_PORT);		/* trigger oneshots */
					/* and see what happens */
	for (j = JS_DEF_TIMEOUT; (0x0f & inb (JS_PORT)) && j; j--);
				/* do nothing; wait for the timeout */
	js_exist = inb (JS_PORT) & 0x0f; /* get joystick status byte */
	js_exist = (~js_exist) & 0x0f;
/*	printk("find_axes: js_exist is %d (0x%04X)\n", js_exist, js_exist);*/
	return js_exist;
}

static int js_ioctl (struct inode *inode,
		     struct file *file,
		     unsigned int cmd,
		     unsigned long arg)
{
	unsigned int minor = MINOR (inode->i_rdev);
	if (minor >= JS_MAX)
		return -ENODEV;
		
	if ((((inb (JS_PORT) & 0x0f) >> (minor << 1)) & 0x03) == 0x03)	/*js minor exists?*/
		return -ENODEV;
	switch (cmd) 
	{
	
		case JSIOCSCAL:	/*from struct *arg to js_data[minor]*/
			if(copy_from_user(&js_data[minor].js_corr, 
				(void *)arg, sizeof(struct js_status)))
					return -EFAULT;
			break;
		case JSIOCGCAL:	/*to struct *arg from js_data[minor]*/
			if(copy_to_user((void *) arg, &js_data[minor].js_corr, 
				sizeof(struct js_status)))
				return -EFAULT;
			break;
		case JSIOCSTIMEOUT:
			if(copy_from_user(&js_data[minor].js_timeout,
				(void *)arg, sizeof(js_data[0].js_timeout)))
					return -EFAULT;
			break;
		case JSIOCGTIMEOUT:
			if(copy_to_user((void *)arg, &js_data[minor].js_timeout,
				sizeof(js_data[0].js_timeout)))
					return -EFAULT;
			break;
		case JSIOCSTIMELIMIT:
			if(copy_from_user(&js_data[minor].js_timelimit,
				(void *)arg, sizeof(js_data[0].js_timelimit)))
					return -EFAULT;
			break;
		case JSIOCGTIMELIMIT:
			if(copy_to_user((void *)arg, &js_data[minor].js_timelimit,
				sizeof(js_data[0].js_timelimit)))
					return -EFAULT;
			break;
		case JSIOCGCONFIG:
			if(copy_to_user((void *)arg, &js_data[minor],
				sizeof(struct js_config)))
					return -EFAULT;
			break;
		case JSIOCSCONFIG:
			if(copy_from_user(&js_data[minor], (void *)arg, 
				sizeof(struct js_config)))
					return -EFAULT;
			/* Must be busy to do this ioctl! */
			js_data[minor].busy = 1;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

/*
 *	js_open():
 *	device open routine. increments module usage count, initializes
 *	data for that joystick.
 *
 *	returns: 0 or
 *	-ENODEV: asked for joystick other than #0 or #1
 *	-ENODEV: asked for joystick on axis where there is none
 *	-EBUSY: attempt to open joystick already open
 */
 
static int js_open (struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR (inode->i_rdev);
	int j;

	if (minor >= JS_MAX)
		return -ENODEV;	/*check for joysticks*/

	for (j = JS_DEF_TIMEOUT; (js_exist & inb (JS_PORT)) && j; j--);
	cli();			/*block js_read while js_exist is being modified*/
	/*js minor exists?*/
	if ((((js_exist = inb (JS_PORT)) >> (minor << 1)) & 0x03) == 0x03) {
		js_exist = (~js_exist) & 0x0f;
		sti();
		return -ENODEV;
	}
	js_exist = (~js_exist) & 0x0f;
	sti();

	if (js_data[minor].busy)
		return -EBUSY;
	js_data[minor].busy = JS_TRUE;
	js_data[minor].js_corr.x = JS_DEF_CORR;	/*default scale*/
	js_data[minor].js_corr.y = JS_DEF_CORR;
	js_data[minor].js_timeout = JS_DEF_TIMEOUT;
	js_data[minor].js_timelimit = JS_DEF_TIMELIMIT;
	js_data[minor].js_expiretime = jiffies;

	MOD_INC_USE_COUNT;
	return 0;
}

static int js_release (struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR (inode->i_rdev);
	inode->i_atime = CURRENT_TIME;
	js_data[minor].busy = JS_FALSE;
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	js_read() reads the buttons x, and y axis from both joysticks if a
 *	given interval has expired since the last read or is equal to
 *	-1l. The buttons are in port 0x201 in the high nibble. The axis are
 *	read by writing to 0x201 and then measuring the time it takes the
 *	one shots to clear.
 */

static ssize_t js_read (struct file *file, char *buf, 
		     size_t count, loff_t *ppos)
{
	int j, chk, jsmask;
	int t0, t_x0, t_y0, t_x1, t_y1;
	unsigned int minor;
	int buttons;
	struct inode *inode=file->f_dentry->d_inode;
	
	if (count != JS_RETURN)
		return -EINVAL;
	minor = MINOR (inode->i_rdev); 
	inode->i_atime = CURRENT_TIME;
	if (jiffies >= js_data[minor].js_expiretime) 
	{
		j = js_data[minor].js_timeout;
		for (; (js_exist & inb (JS_PORT)) && j; j--);
		if (j == 0)
			return -ENODEV;	/*no joystick here*/
		/*Make sure no other proc is using port*/
	
		cli();
		js_read_semaphore++;
		sti();
		
		buttons = ~(inb (JS_PORT) >> 4);
		js_data[0].js_save.buttons = buttons & 0x03;
		js_data[1].js_save.buttons = (buttons >> 2) & 0x03;
		j = js_data[minor].js_timeout;
		jsmask = 0;

		cli();		/*no interrupts!*/
		outb (0xff, JS_PORT);	/*trigger one-shots*/
		/*get init timestamp*/
		t_x0 = t_y0 = t_x1 = t_y1 = t0 = get_timer0 ();
		/*wait for an axis' bit to clear or timeout*/
		do {
			chk = (inb (JS_PORT) & js_exist) | jsmask;
			if (!(chk & JS_X_0)) {
				t_x0 = get_timer0();
				jsmask |= JS_X_0;
			}
			if (!(chk & JS_Y_0)) {
				t_y0 = get_timer0();
				jsmask |= JS_Y_0;
			}
			if (!(chk & JS_X_1)) {
				t_x1 = get_timer0();
				jsmask |= JS_X_1;
			}
			if (!(chk & JS_Y_1)) {
				t_y1 = get_timer0();
				jsmask |= JS_Y_1;
			}
		} while (--j && jsmask != js_exist);
		sti();					/* allow interrupts */

		js_read_semaphore = 0;	/* allow other reads to progress */
		if (j == 0)
			return -ENODEV;	/*read timed out*/
		js_data[0].js_expiretime = jiffies +
			js_data[0].js_timelimit;	/*update data*/
		js_data[1].js_expiretime = jiffies +
			js_data[1].js_timelimit;
		js_data[0].js_save.x = DELTA_TIME (t0, t_x0) >>
			js_data[0].js_corr.x;
		js_data[0].js_save.y = DELTA_TIME (t0, t_y0) >>
			js_data[0].js_corr.y;
		js_data[1].js_save.x = DELTA_TIME (t0, t_x1) >>
			js_data[1].js_corr.x;
		js_data[1].js_save.y = DELTA_TIME (t0, t_y1) >>
			js_data[1].js_corr.y;
	}

	if(copy_to_user(buf, &js_data[minor].js_save, JS_RETURN))
		return -EFAULT;
	return JS_RETURN;
}


static struct file_operations js_fops =
{
	NULL,			/* js_lseek*/
	js_read,		/* js_read */
	NULL,			/* js_write*/
	NULL,			/* js_readaddr*/
	NULL,			/* js_select */
	js_ioctl,		/* js_ioctl*/
	NULL,			/* js_mmap */
	js_open,		/* js_open*/
	js_release,		/* js_release*/
	NULL			/* js_fsync */
};

#ifdef MODULE

#define joystick_init init_module

void cleanup_module (void)
{
	if (unregister_chrdev (JOYSTICK_MAJOR, "joystick"))
		printk ("joystick: cleanup_module failed\n");
	release_region(JS_PORT, 1);
}

#endif /* MODULE */

int joystick_init(void)
{
	int js_num;
	int js_count;

	if (check_region(JS_PORT, 1)) {
		printk("js_init: port already in use\n");
		return -EBUSY;
	}

	js_num = find_axes();
	js_count = !!(js_num & 0x3) + !!(js_num & 0xC);


	if (js_count == 0) 
	{
		printk("No joysticks found.\n");
		return -ENODEV;
		/* if the user boots the machine, which runs insmod, and THEN
		   decides to hook up the joystick, well, then we do the wrong
		   thing. But it's a good idea to avoid giving out a false sense
		   of security by letting the module load otherwise. */
	}

	if (register_chrdev (JOYSTICK_MAJOR, "joystick", &js_fops)) {
		printk ("Unable to get major=%d for joystick\n",
					JOYSTICK_MAJOR);
		return -EBUSY;
	}
	request_region(JS_PORT, 1, "joystick");
		
	for (js_num = 0; js_num < JS_MAX; js_num++)
		  js_data[js_num].busy = JS_FALSE;
	js_read_semaphore = 0;

	printk (KERN_INFO "Found %d joystick%c.\n",
			js_count,
		    (js_num == 1) ? ' ' : 's');
	return 0;
}

