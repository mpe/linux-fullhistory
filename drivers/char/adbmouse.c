/*
 * Macintosh ADB Mouse driver for Linux
 *
 * 27 Oct 1997 Michael Schmitz
 * logitech fixes by anthony tong
 * further hacking by Paul Mackerras
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
#include <asm/uaccess.h>
#ifdef __powerpc__
#include <asm/processor.h>
#endif
#ifdef __mc68000__
#include <asm/setup.h>
#endif

static struct mouse_status mouse;
static unsigned char adb_mouse_buttons[16];

extern void (*adb_mouse_interrupt_hook)(unsigned char *, int);
extern int adb_emulate_buttons;
extern int adb_button2_keycode;
extern int adb_button3_keycode;

extern int console_loglevel;

/*
 *	XXX: need to figure out what ADB mouse packets mean ... 
 *	This is the stuff stolen from the Atari driver ...
 */
static void adb_mouse_interrupt(unsigned char *buf, int nb)
{
    int buttons, id;

/*
    Handler 1 -- 100cpi original Apple mouse protocol.
    Handler 2 -- 200cpi original Apple mouse protocol.

    For Apple's standard one-button mouse protocol the data array will
    contain the following values:

                BITS    COMMENTS
    data[0] = dddd 1100 ADB command: Talk, register 0, for device dddd.
    data[1] = bxxx xxxx First button and x-axis motion.
    data[2] = byyy yyyy Second button and y-axis motion.

    Handler 4 -- Apple Extended mouse protocol.

    For Apple's 3-button mouse protocol the data array will contain the
    following values:

		BITS    COMMENTS
    data[0] = dddd 1100 ADB command: Talk, register 0, for device dddd.
    data[1] = bxxx xxxx Left button and x-axis motion.
    data[2] = byyy yyyy Second button and y-axis motion.
    data[3] = byyy bxxx Third button and fourth button.  
    	      Y is additional high bits of y-axis motion.  
    	      X is additional high bits of x-axis motion.

    This procedure also gets called from the keyboard code if we
    are emulating mouse buttons with keys.  In this case data[0] == 0
    (data[0] cannot be 0 for a real ADB packet).

    'buttons' here means 'button down' states!
    Button 1 (left)  : bit 2, busmouse button 3
    Button 2 (middle): bit 1, busmouse button 2
    Button 3 (right) : bit 0, busmouse button 1
*/

    /* x/y and buttons swapped */
    
    if (console_loglevel >= 8)
	printk("KERN_DEBUG adb_mouse: %s data; ", buf[0]? "real": "fake"); 

    id = (buf[0] >> 4) & 0xf;
    buttons = adb_mouse_buttons[id];

    /* button 1 (left, bit 2) */
    buttons = (buttons&3) | (buf[1] & 0x80 ? 4 : 0); /* 1+2 unchanged */

    /* button 2 (middle) */
    buttons = (buttons&5) | (buf[2] & 0x80 ? 2 : 0); /* 2+3 unchanged */

    /* button 3 (right) present?
     *  on a logitech mouseman, the right and mid buttons sometimes behave
     *  strangely until they both have been pressed after booting. */
    /* data valid only if extended mouse format ! */
    if (nb == 4)
	buttons = (buttons&6) | (buf[3] & 0x80 ? 1 : 0); /* 1+3 unchanged */

    add_mouse_randomness(((~buttons&7) << 16) + ((buf[2]&0x7f) << 8) + (buf[1]&0x7f));

    adb_mouse_buttons[id] = buttons;
    /* a button is down if it is down on any mouse */
    for (id = 0; id < 16; ++id)
	buttons &= adb_mouse_buttons[id];

    mouse.buttons = buttons;
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

    adb_mouse_interrupt_hook = NULL;
    MOD_DEC_USE_COUNT;
    return 0;
}

static int open_mouse(struct inode *inode, struct file *file)
{
    int id;

    if (mouse.active++)
	return 0;
	
    mouse.ready = 0;

    mouse.dx = mouse.dy = 0;
    for (id = 0; id < 16; ++id)
	adb_mouse_buttons[id] = 7;	/* all buttons up */
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
    NULL,		/* flush */
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

#ifdef __powerpc__
    if ( (_machine != _MACH_chrp) && (_machine != _MACH_Pmac) )
	    return -ENODEV;
#endif
#ifdef __mc68000__
    if (!MACH_IS_MAC)
	return -ENODEV;
#endif
    printk(KERN_INFO "Macintosh ADB mouse driver installed.\n");
    misc_register(&adb_mouse);
    return 0;
}


/*
 * XXX this function is misnamed.
 * It is called if the kernel is booted with the adb_buttons=xxx
 * option, which is about using ADB keyboard buttons to emulate
 * mouse buttons. -- paulus
 */
__initfunc(void adb_mouse_setup(char *str, int *ints))
{
	if (ints[0] >= 1) {
		adb_emulate_buttons = ints[1] > 0;
		if (ints[1] > 1)
			adb_button2_keycode = ints[1];
		if (ints[0] >= 2)
			adb_button3_keycode = ints[2];
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
