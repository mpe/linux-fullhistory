/*
 * Atari Mouse Driver for Linux
 * by Robert de Vries (robert@and.nl) 19Jul93
 *
 * 16 Nov 1994 Andreas Schwab
 * Compatibility with busmouse
 * Support for three button mouse (shamelessly stolen from MiNT)
 * third button wired to one of the joystick directions on joystick 1
 *
 * 1996/02/11 Andreas Schwab
 * Module support
 * Allow multiple open's
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/busmouse.h>

#include <asm/setup.h>
#include <asm/atarikb.h>
#include <asm/uaccess.h>

static struct mouse_status mouse;
static int mouse_threshold[2] = {2,2};
MODULE_PARM(mouse_threshold, "2i");
extern int atari_mouse_buttons;

static void atari_mouse_interrupt(char *buf)
{
    int buttons;

/*    ikbd_mouse_disable(); */

    buttons = ((buf[0] & 1 ? 1 : 0)
	       | (buf[0] & 2 ? 4 : 0)
	       | (atari_mouse_buttons & 2));
    atari_mouse_buttons = buttons;
    add_mouse_randomness((buttons << 16) + (buf[2] << 8) + buf[1]);
    mouse.buttons = ~buttons & 7;
    mouse.dx += buf[1];
    mouse.dy -= buf[2];
    mouse.ready = 1;
    wake_up_interruptible(&mouse.wait);
    if (mouse.fasyncptr)
	kill_fasync(mouse.fasyncptr, SIGIO);

/*    ikbd_mouse_rel_pos(); */
}

static int fasync_mouse(int fd, struct file *filp, int on)
{
	int retval;
	retval = fasync_helper(fd, filp, on, &mouse.fasyncptr);
	if (retval < 0)
		return retval;
	return 0;
}

static int release_mouse(struct inode *inode, struct file *file)
{
    fasync_mouse(-1, file, 0);
    if (--mouse.active)
      return 0;
    ikbd_mouse_disable();

    atari_mouse_interrupt_hook = NULL;
    MOD_DEC_USE_COUNT;
    return 0;
}

static int open_mouse(struct inode *inode, struct file *file)
{
    if (mouse.active++)
	return 0;
    mouse.ready = 0;
    mouse.dx = mouse.dy = 0;
    atari_mouse_buttons = 0;
    ikbd_mouse_y0_top ();
    ikbd_mouse_thresh (mouse_threshold[0], mouse_threshold[1]);
    ikbd_mouse_rel_pos();
    MOD_INC_USE_COUNT;
    atari_mouse_interrupt_hook = atari_mouse_interrupt;
    return 0;
}

static ssize_t write_mouse(struct file *file, const char *buffer,
			  size_t count, loff_t *ppos)
{
    return -EINVAL;
}

static ssize_t read_mouse(struct file * file, char * buffer,
			  size_t count, loff_t *ppos)
{
    int dx, dy, buttons;

    if (count < 3)
	return -EINVAL;
    if (!mouse.ready)
	return -EAGAIN;
    /* ikbd_mouse_disable */
    dx = mouse.dx;
    dy = mouse.dy;
    buttons = mouse.buttons;
    if (dx > 127)
      dx = 127;
    else if (dx < -128)
      dx = -128;
    if (dy > 127)
      dy = 127;
    else if (dy < -128)
      dy = -128;
    mouse.dx -= dx;
    mouse.dy -= dy;
    if (mouse.dx == 0 && mouse.dy == 0)
      mouse.ready = 0;
    /* ikbd_mouse_rel_pos(); */
    if (put_user(buttons | 0x80, buffer++) ||
	put_user((char) dx, buffer++) ||
	put_user((char) dy, buffer++))
	return -EFAULT;
    if (count > 3)
	if (clear_user(buffer, count - 3))
	    return -EFAULT;
    return count;
}

static unsigned int mouse_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &mouse.wait, wait);
	if (mouse.ready)
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations atari_mouse_fops = {
    NULL,		/* mouse_seek */
    read_mouse,
    write_mouse,
    NULL,		/* mouse_readdir */
    mouse_poll,
    NULL,		/* mouse_ioctl */
    NULL,		/* mouse_mmap */
    open_mouse,
    NULL,		/* flush */
    release_mouse,
    NULL,
    fasync_mouse,
};

static struct miscdevice atari_mouse = {
    ATARIMOUSE_MINOR, "atarimouse", &atari_mouse_fops
};

__initfunc(int atari_mouse_init(void))
{
	int r;

	if (!MACH_IS_ATARI)
		return -ENODEV;

	mouse.active = 0;
	mouse.ready = 0;
	mouse.wait = NULL;

	r = misc_register(&atari_mouse);
	if (r)
		return r;

	printk(KERN_INFO "Atari mouse installed.\n");
	return 0;
}


#define	MIN_THRESHOLD 1
#define	MAX_THRESHOLD 20	/* more seems not reasonable... */

__initfunc(void atari_mouse_setup( char *str, int *ints ))
{
    if (ints[0] < 1) {
	printk( "atari_mouse_setup: no arguments!\n" );
	return;
    }
    else if (ints[0] > 2) {
	printk( "atari_mouse_setup: too many arguments\n" );
    }

    if (ints[1] < MIN_THRESHOLD || ints[1] > MAX_THRESHOLD)
	printk( "atari_mouse_setup: bad threshold value (ignored)\n" );
    else {
	mouse_threshold[0] = ints[1];
	mouse_threshold[1] = ints[1];
	if (ints[0] > 1) {
	    if (ints[2] < MIN_THRESHOLD || ints[2] > MAX_THRESHOLD)
		printk("atari_mouse_setup: bad threshold value (ignored)\n" );
	    else
		mouse_threshold[1] = ints[2];
	}
    }
	
}

#ifdef MODULE
int init_module(void)
{
	return atari_mouse_init();
}

void cleanup_module(void)
{
  misc_deregister(&atari_mouse);
}
#endif
