/*
 * Macintosh ADB Mouse driver for Linux
 *
 * 27 Oct 1997 Michael Schmitz
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
 *
 * Converted to use new generic busmouse code.  5 Apr 1998
 *   Russell King <rmk@arm.uk.linux.org>
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mac_mouse.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include "busmouse.h"

static int msedev;
static int mac_mouse_x_threshold = 2, mac_mouse_y_threshold = 2;
static int mac_mouse_buttons = 0;

extern void (*mac_mouse_interrupt_hook) (char *, int);
extern int mac_emulate_button2;
extern int mac_emulate_button3;

extern int console_loglevel;

/*
 *	XXX: need to figure out what ADB mouse packets mean ... 
 *	This is the stuff stolen from the Atari driver ...
 */
static void mac_mouse_interrupt(char *buf, int nb)
{
    static int buttons = 7;	/* all mouse buttons _up_ !! */

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
    
    if (buf[0] ==  0)	{				/* real packet : use buttons? */
#ifdef DEBUG_ADBMOUSE
	if (console_loglevel >= 8)
	    printk("mac_mouse: real data; "); 
#endif
	/* button 1 (left, bit 2) : always significant ! */
	buttons = (buttons&3) | (buf[3] & 0x80 ? 4 : 0); /* 1+2 unchanged */
	/* button 2 (right, bit 0) present ? */
	if ( !mac_emulate_button2 ) 
	    buttons = (buttons&6) | (buf[4] & 0x80 ? 1 : 0); /* 2+3 unchanged */
	/* button 2 (middle) present? */
	/* data valid only if extended mouse format ! (buf[3] = 0 else)*/
	if ( !mac_emulate_button3 && buf[1]&0x40 )
	    buttons = (buttons&5) | (buf[5] & 0x80 ? 2 : 0); /* 1+3 unchanged */
    } else {					/* fake packet : use 2+3 */
#ifdef DEBUG_ADBMOUSE
	if (console_loglevel >= 8)
	    printk("mac_mouse: fake data; "); 
#endif
	/* we only see state changes here, but the fake driver takes care
	 * to preserve state... button 1 state must stay unchanged! */
	buttons = (buttons&4) | ((buf[4] & 0x80 ? 1 : 0) | (buf[5] & 0x80 ? 2 : 0));
    }

    busmouse_add_movementbuttons(msedev,
    				 ((buf[4]&0x7f) < 64 ? (buf[4]&0x7f) : (buf[4]&0x7f)-128 ),
    				 -((buf[3]&0x7f) < 64 ? (buf[3]&0x7f) : (buf[3]&0x7f)-128 ),
    				 buttons & 7);
}

static int release_mouse(struct inode *inode, struct file *file)
{
    mac_mouse_interrupt_hook = NULL;
    MOD_DEC_USE_COUNT;
    return 0;
}

static int open_mouse(struct inode *inode, struct file *file)
{
    MOD_INC_USE_COUNT;
    mac_mouse_interrupt_hook = mac_mouse_interrupt;
    return 0;
}

#define ADB_MOUSE_MINOR	10

static struct busmouse macmouse = {
	ADB_MOUSE_MINOR, "adbmouse", open_mouse, release_mouse, 0
};

__initfunc(int mac_mouse_init(void))
{
    if (!MACH_IS_MAC)
	return -ENODEV;

    msedev = register_busmouse(&macmouse);
    if (msedev < 0)
    	printk(KERN_WARNING "Unable to register ADB mouse driver.\n");
    else
	printk(KERN_INFO "Macintosh ADB mouse installed.\n");
    return msedev < 0 ? msedev : 0;
}


#define	MIN_THRESHOLD 1
#define	MAX_THRESHOLD 20	/* more seems not reasonable... */

__initfunc(void mac_mouse_setup(char *str, int *ints))
{
    if (ints[0] < 1) {
	printk( "mac_mouse_setup: no arguments!\n" );
	return;
    }
    else if (ints[0] > 2) {
	printk( "mac_mouse_setup: too many arguments\n" );
    }

    if (ints[1] < MIN_THRESHOLD || ints[1] > MAX_THRESHOLD)
	printk( "mac_mouse_setup: bad threshold value (ignored)\n" );
    else {
	mac_mouse_x_threshold = ints[1];
	mac_mouse_y_threshold = ints[1];
	if (ints[0] > 1) {
	    if (ints[2] < MIN_THRESHOLD || ints[2] > MAX_THRESHOLD)
		printk("mac_mouse_setup: bad threshold value (ignored)\n" );
	    else
		mac_mouse_y_threshold = ints[2];
	}
    }
	
}

#ifdef MODULE
#include <asm/setup.h>

int init_module(void)
{
    return mac_mouse_init();
}

void cleanup_module(void)
{
    unregister_busmouse(msedev);
}
#endif
