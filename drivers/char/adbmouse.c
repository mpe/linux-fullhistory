/*
 * Macintosh ADB Mouse driver for Linux
 *
 * 27 Oct 1997 Michael Schmitz
 * logitech fixes by anthony tong
 *
 * Apple mouse protocol according to:
 *
 * Device code shamelessly stolen from:
 */
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

#include <asm/adb_mouse.h>
#include <asm/segment.h>
#include <asm/processor.h>

static struct mouse_status mouse;
static int adb_mouse_x_threshold = 2, adb_mouse_y_threshold = 2;
static int adb_mouse_buttons = 0;

extern void (*adb_mouse_interrupt_hook) (char *, int);

extern int console_loglevel;

/*
 *	XXX: need to figure out what ADB mouse packets mean ... 
 *	This is the stuff stolen from the Atari driver ...
 */
static void adb_mouse_interrupt(char *buf, int nb)
{
    static int buttons = 7;

  /*
    Handler 1 -- 100cpi original Apple mouse protocol.
    Handler 2 -- 200cpi original Apple mouse protocol.

    For Apple's standard one-button mouse protocol the data array will
    contain the following values:

                BITS    COMMENTS
    data[0] = 0000 0000 ADB packet identifer.
    data[1] = ???? ???? (?)
    data[2] = ???? ??00 Bits 0-1 should be zero for a mouse device.
    data[3] = bxxx xxxx First button and x-axis motion.
    data[4] = byyy yyyy Second button and y-axis motion.

    NOTE: data[0] is confirmed by the parent function and need not be
    checked here.
  */

  /*
    Handler 4 -- Apple Extended mouse protocol.

    For Apple's 3-button mouse protocol the data array will contain the
    following values:

		BITS    COMMENTS
    data[0] = 0000 0000 ADB packet identifer.
    data[1] = 0100 0000 Extended protocol register.
	      Bits 6-7 are the device id, which should be 1.
	      Bits 4-5 are resolution which is in "units/inch".
	      The Logitech MouseMan returns these bits clear but it has
	      200/300cpi resolution.
	      Bits 0-3 are unique vendor id.
    data[2] = 0011 1100 Bits 0-1 should be zero for a mouse device.
	      Bits 2-3 should be 8 + 4.
		      Bits 4-7 should be 3 for a mouse device.
    data[3] = bxxx xxxx Left button and x-axis motion.
    data[4] = byyy yyyy Second button and y-axis motion.
    data[5] = byyy bxxx Third button and fourth button.  
    	      Y is additiona. high bits of y-axis motion.  
    	      X is additional high bits of x-axis motion.

    NOTE: data[0] and data[2] are confirmed by the parent function and
    need not be checked here.
  */

    /*
     * 'buttons' here means 'button down' states!
     * Button 1 (left)  : bit 2, busmouse button 3
     * Button 2 (right) : bit 0, busmouse button 1
     * Button 3 (middle): bit 1, busmouse button 2
     */

    /* x/y and buttons swapped */
    
    if (nb > 0)	{				/* real packet : use buttons? */
	if (console_loglevel >= 8)
	    printk("adb_mouse: real data; "); 
	/* button 1 (left, bit 2) : always significant ! */
	buttons = (buttons&3) | (buf[1] & 0x80 ? 4 : 0); /* 1+2 unchanged */
	/* button 2 (middle) */
	buttons = (buttons&5) | (buf[2] & 0x80 ? 2 : 0); /* 2+3 unchanged */
	/* button 3 (right) present?
	 *  on a logitech mouseman, the right and mid buttons sometimes behave
	 *  strangely until they both have been pressed after booting. */
	/* data valid only if extended mouse format ! (buf[3] = 0 else) */
	if ( nb == 6 )
	    buttons = (buttons&6) | (buf[3] & 0x80 ? 1 : 0); /* 1+3 unchanged */
    } else {					/* fake packet : use 2+3 */
	if (console_loglevel >= 8)
	    printk("adb_mouse: fake data; "); 
	/* we only see state changes here, but the fake driver takes care
	 * to preserve state... button 1 state must stay unchanged! */
	buttons = (buttons&4) | ((buf[2] & 0x80 ? 1 : 0) |
		  (buf[3] & 0x80 ? 2 : 0));
    }

    add_mouse_randomness(((~buttons & 7) << 16) + ((buf[2]&0x7f) << 8) + (buf[1]&0x7f));
    mouse.buttons = buttons & 7;
    mouse.dx += ((buf[2]&0x7f) < 64 ? (buf[2]&0x7f) : (buf[2]&0x7f)-128 );
    mouse.dy -= ((buf[1]&0x7f) < 64 ? (buf[1]&0x7f) : (buf[1]&0x7f)-128 );

    if (console_loglevel >= 8)
        printk(" %X %X %X buttons %x dx %d dy %d \n", 
               buf[1], buf[2], buf[3], mouse.buttons, mouse.dx, mouse.dy);
 
    mouse.ready = 1;
    wake_up_interruptible(&mouse.wait);
    if (mouse.fasyncptr)
	kill_fasync(mouse.fasyncptr, SIGIO);

}

static int fasync_mouse(struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(filp, on, &mouse.fasyncptr);
	if (retval < 0)
		return retval;
	return 0;
}

static int release_mouse(struct inode *inode, struct file *file)
{
    fasync_mouse(file, 0);
    if (--mouse.active)
      return 0;

    adb_mouse_interrupt_hook = NULL;
    MOD_DEC_USE_COUNT;
    return 0;
}

static int open_mouse(struct inode *inode, struct file *file)
{
    if (mouse.active++)
	return 0;
	
    mouse.ready = 0;

    mouse.dx = mouse.dy = 0;
    adb_mouse_buttons = 0;
    MOD_INC_USE_COUNT;
    adb_mouse_interrupt_hook = adb_mouse_interrupt;
    return 0;
}

static ssize_t write_mouse(struct file *file, const char *buffer,
			   size_t count, loff_t *ppos)
{
    return -EINVAL;
}

static ssize_t read_mouse(struct file *file, char *buffer, size_t count,
			  loff_t *ppos)
{
    int dx, dy, buttons;

    if (count < 3)
	return -EINVAL;
    if (!mouse.ready)
	return -EAGAIN;
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

struct file_operations adb_mouse_fops = {
    NULL,		/* mouse_seek */
    read_mouse,
    write_mouse,
    NULL,		/* mouse_readdir */
    mouse_poll,
    NULL,		/* mouse_ioctl */
    NULL,		/* mouse_mmap */
    open_mouse,
    release_mouse,
    NULL,
    fasync_mouse,
};

#define ADB_MOUSE_MINOR	10

static struct miscdevice adb_mouse = {
    ADB_MOUSE_MINOR, "adbmouse", &adb_mouse_fops
};

__initfunc(int adb_mouse_init(void))
{
    mouse.active = 0;
    mouse.ready = 0;
    mouse.wait = NULL;

    if ( (_machine != _MACH_chrp) && (_machine != _MACH_Pmac) )
	    return -ENODEV;
    printk(KERN_INFO "Macintosh ADB mouse installed.\n");
    misc_register(&adb_mouse);
    return 0;
}


#define	MIN_THRESHOLD 1
#define	MAX_THRESHOLD 20	/* more seems not reasonable... */

__initfunc(void adb_mouse_setup(char *str, int *ints))
{
    if (ints[0] < 1) {
	printk( "adb_mouse_setup: no arguments!\n" );
	return;
    }
    else if (ints[0] > 2) {
	printk( "adb_mouse_setup: too many arguments\n" );
    }

    if (ints[1] < MIN_THRESHOLD || ints[1] > MAX_THRESHOLD)
	printk( "adb_mouse_setup: bad threshold value (ignored)\n" );
    else {
	adb_mouse_x_threshold = ints[1];
	adb_mouse_y_threshold = ints[1];
	if (ints[0] > 1) {
	    if (ints[2] < MIN_THRESHOLD || ints[2] > MAX_THRESHOLD)
		printk("adb_mouse_setup: bad threshold value (ignored)\n" );
	    else
		adb_mouse_y_threshold = ints[2];
	}
    }
	
}

#ifdef MODULE
#include <asm/setup.h>

int init_module(void)
{
    return adb_mouse_init();
}

void cleanup_module(void)
{
    misc_deregister(&adb_mouse);
}
#endif
