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

#include <asm/atarikb.h>
#include <asm/atari_mouse.h>
#include <asm/segment.h>
#include <asm/bootinfo.h>

static struct mouse_status mouse;
static int atari_mouse_x_threshold = 2, atari_mouse_y_threshold = 2;
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

static int fasync_mouse(struct inode *inode, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(inode, filp, on, &mouse.fasyncptr);
	if (retval < 0)
		return retval;
	return 0;
}

static void release_mouse(struct inode *inode, struct file *file)
{
    fasync_mouse(inode, file, 0);
    if (--mouse.active)
      return;
    ikbd_mouse_disable();

    atari_mouse_interrupt_hook = NULL;
    MOD_DEC_USE_COUNT;
}

static int open_mouse(struct inode *inode, struct file *file)
{
    if (mouse.active++)
	return 0;
    mouse.ready = 0;
    mouse.dx = mouse.dy = 0;
    atari_mouse_buttons = 0;
    ikbd_mouse_y0_top ();
    ikbd_mouse_thresh (atari_mouse_x_threshold, atari_mouse_y_threshold);
    ikbd_mouse_rel_pos();
    MOD_INC_USE_COUNT;
    atari_mouse_interrupt_hook = atari_mouse_interrupt;
    return 0;
}

static int write_mouse(struct inode *inode, struct file *file, const char *buffer, int count)
{
    return -EINVAL;
}

static int read_mouse(struct inode *inode, struct file *file, char *buffer, int count)
{
    int dx, dy, buttons;
    int r;

    if (count < 3)
	return -EINVAL;
    if ((r = verify_area(VERIFY_WRITE, buffer, count)))
	return r;
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
    put_user(buttons | 0x80, buffer);
    put_user((char) dx, buffer + 1);
    put_user((char) dy, buffer + 2);
    for (r = 3; r < count; r++)
      put_user (0, buffer + r);
    return r;
}

static int mouse_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
	if (sel_type == SEL_IN) {
	    	if (mouse.ready)
			return 1;
		select_wait(&mouse.wait, wait);
	}
	return 0;
}

struct file_operations atari_mouse_fops = {
    NULL,		/* mouse_seek */
    read_mouse,
    write_mouse,
    NULL,		/* mouse_readdir */
    mouse_select,
    NULL,		/* mouse_ioctl */
    NULL,		/* mouse_mmap */
    open_mouse,
    release_mouse,
    NULL,
    fasync_mouse,
};

static struct miscdevice atari_mouse = {
    ATARIMOUSE_MINOR, "atarimouse", &atari_mouse_fops
};

int atari_mouse_init(void)
{
    mouse.active = 0;
    mouse.ready = 0;
    mouse.wait = NULL;

    if (!MACH_IS_ATARI)
	return -ENODEV;
    printk(KERN_INFO "Atari mouse installed.\n");
    misc_register(&atari_mouse);
    return 0;
}


#define	MIN_THRESHOLD 1
#define	MAX_THRESHOLD 20	/* more seems not reasonable... */

void atari_mouse_setup( char *str, int *ints )
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
	atari_mouse_x_threshold = ints[1];
	atari_mouse_y_threshold = ints[1];
	if (ints[0] > 1) {
	    if (ints[2] < MIN_THRESHOLD || ints[2] > MAX_THRESHOLD)
		printk("atari_mouse_setup: bad threshold value (ignored)\n" );
	    else
		atari_mouse_y_threshold = ints[2];
	}
    }
	
}

#ifdef MODULE
#include <asm/bootinfo.h>

int init_module(void)
{
	return atari_mouse_init();
}

void cleanup_module(void)
{
  misc_deregister(&atari_mouse);
}
#endif
