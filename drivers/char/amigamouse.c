/*
 * Amiga Mouse Driver for Linux 68k by Michael Rausch
 * based upon:
 *
 * Logitech Bus Mouse Driver for Linux
 * by James Banks
 *
 * Mods by Matthew Dillon
 *   calls verify_area()
 *   tracks better when X is busy or paging
 *
 * Heavily modified by David Giller
 *   changed from queue- to counter- driven
 *   hacked out a (probably incorrect) mouse_select
 *
 * Modified again by Nathan Laredo to interface with
 *   0.96c-pl1 IRQ handling changes (13JUL92)
 *   didn't bother touching select code.
 *
 * Modified the select() code blindly to conform to the VFS
 *   requirements. 92.07.14 - Linus. Somebody should test it out.
 *
 * Modified by Johan Myreen to make room for other mice (9AUG92)
 *   removed assignment chr_fops[10] = &mouse_fops; see mouse.c
 *   renamed mouse_fops => bus_mouse_fops, made bus_mouse_fops public.
 *   renamed this file mouse.c => busmouse.c
 *
 * Modified for use in the 1.3 kernels by Jes Sorensen.
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/random.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/irq.h>
#include <asm/amigamouse.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/bootinfo.h>

#define MSE_INT_ON()	mouseint_allowed = 1
#define MSE_INT_OFF()	mouseint_allowed = 0


static struct mouse_status mouse;

static int mouseint_allowed;

static void mouse_interrupt(int irq, struct pt_regs *fp, void *dummy)
{
	static int lastx=0, lasty=0;
	int dx, dy;
	int nx, ny;
	unsigned char buttons;

	unsigned short joy0dat, potgor;

	if(!mouseint_allowed)
		return;
	MSE_INT_OFF();

	/*
	 *  This routine assumes, just like Kickstart, that the mouse
	 *  has not moved more than 127 ticks since last VBL.
	 */

	joy0dat = custom.joy0dat;

	nx = joy0dat & 0xff;
	ny = joy0dat >> 8;

	dx = nx - lastx;
	if (dx < - 127)
		dx = (256 + nx) - lastx;

	if (dx > 127)
		dx = (nx - 256) - lastx;

	dy = ny - lasty;
	if (dy < - 127)
		dy = (256 + ny) - lasty;

	if (dy > 127)
		dy = (ny - 256) - lasty;

	lastx = nx;
	lasty = ny;

#if 0
	dx = -lastdx;
	dx += (lastdx = joy0dat & 0xff);
	if (dx < -127)
	    dx = -255-dx;		/* underrun */
	else
	if (dx > 127)
	    dx = 255-dx;		/* overflow */

	dy = -lastdy;
	dy += (lastdy = joy0dat >> 8);
	if (dy < -127)
	    dy = -255-dy;
	else
	if (dy > 127)
	    dy = 255-dy;
#endif


	potgor = custom.potgor;
	buttons = (ciaa.pra & 0x40 ? 4 : 0) |	/* left button; note that the bits are low-active, as are the expected results -> double negation */
#if 1
		  (potgor & 0x0100 ? 2 : 0) |	/* middle button; emulation goes here */
#endif
		  (potgor & 0x0400 ? 1 : 0);	/* right button */


	if (dx != 0 || dy != 0 || buttons != mouse.buttons) {
	  add_mouse_randomness((buttons << 16) + (dy << 8) + dx);
	  mouse.buttons = buttons;
	  mouse.dx += dx;
	  mouse.dy -= dy;
	  mouse.ready = 1;
	  wake_up_interruptible(&mouse.wait);

	  /*
	   * keep dx/dy reasonable, but still able to track when X (or
	   * whatever) must page or is busy (i.e. long waits between
	   * reads)
	   */
	  if (mouse.dx < -2048)
	      mouse.dx = -2048;
	  else
	  if (mouse.dx >  2048)
	      mouse.dx =  2048;

	  if (mouse.dy < -2048)
	      mouse.dy = -2048;
	  else
	  if (mouse.dy >  2048)
	      mouse.dy =  2048;

	  if (mouse.fasyncptr)
	      kill_fasync(mouse.fasyncptr, SIGIO);
	}
	MSE_INT_ON();
}

static int fasync_mouse(struct inode *inode, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(inode, filp, on, &mouse.fasyncptr);
	if (retval < 0)
		return retval;
	return 0;
}

/*
 * close access to the mouse
 */

static void close_mouse(struct inode * inode, struct file * file)
{
	fasync_mouse(inode, file, 0);
	if (--mouse.active)
	  return;
	MSE_INT_OFF();
	MOD_DEC_USE_COUNT;
}

/*
 * open access to the mouse, currently only one open is
 * allowed.
 */

static int open_mouse(struct inode * inode, struct file * file)
{
  if (!mouse.present)
    return -EINVAL;
  if (mouse.active++)
    return 0;
  mouse.ready = 0;
  mouse.dx = 0;
  mouse.dy = 0;
  mouse.buttons = 0x87;
  mouse.active = 1;
  MOD_INC_USE_COUNT;
  MSE_INT_ON();
  return 0;
}

/*
 * writes are disallowed
 */

static int write_mouse(struct inode * inode, struct file * file, const char * buffer, int count)
{
	return -EINVAL;
}

/*
 * read mouse data.  Currently never blocks.
 */

static int read_mouse(struct inode * inode, struct file * file, char * buffer, int count)
{
	int r;
	int dx;
	int dy;
	unsigned char buttons; 

	if (count < 3)
		return -EINVAL;
	if ((r = verify_area(VERIFY_WRITE, buffer, count)))
		return r;
	if (!mouse.ready)
		return -EAGAIN;

	/*
	 * Obtain the current mouse parameters and limit as appropriate for
	 * the return data format.  Interrupts are only disabled while 
	 * obtaining the parameters, NOT during the puts_fs_byte() calls,
	 * so paging in put_fs_byte() does not effect mouse tracking.
	 */

	MSE_INT_OFF();
	dx = mouse.dx;
	dy = mouse.dy;
	if (dx < -127)
	    dx = -127;
	else
	if (dx > 127)
	    dx = 127;
	if (dy < -127)
	    dy = -127;
	else
	if (dy > 127)
	    dy = 127;
	buttons = mouse.buttons;
	mouse.dx -= dx;
	mouse.dy -= dy;
	mouse.ready = 0;
	MSE_INT_ON();

	put_fs_byte(buttons | 0x80, buffer);
	put_fs_byte((char)dx, buffer + 1);
	put_fs_byte((char)dy, buffer + 2);
	for (r = 3; r < count; r++)
	    put_fs_byte(0x00, buffer + r);
	return r;
}

/*
 * select for mouse input
 */

static int mouse_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	if (sel_type == SEL_IN) {
	    	if (mouse.ready)
			return 1;
		select_wait(&mouse.wait, wait);
    	}
	return 0;
}

struct file_operations amiga_mouse_fops = {
	NULL,		/* mouse_seek */
	read_mouse,
	write_mouse,
	NULL, 		/* mouse_readdir */
	mouse_select, 	/* mouse_select */
	NULL, 		/* mouse_ioctl */
	NULL,		/* mouse_mmap */
	open_mouse,
	close_mouse,
	NULL,
	fasync_mouse,
};

static struct miscdevice amiga_mouse = {
	AMIGAMOUSE_MINOR, "amigamouse", &amiga_mouse_fops
};

int amiga_mouse_init(void)
{
	if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(AMI_MOUSE))
		return -ENODEV;

	custom.joytest = 0;	/* reset counters */

	MSE_INT_OFF();

	mouse.active = 0;
	mouse.ready = 0;
	mouse.buttons = 0x87;
	mouse.dx = 0;
	mouse.dy = 0;
	mouse.wait = NULL;

	/*
	 *  use VBL to poll mouse deltas
	 */

	if(!add_isr(IRQ_AMIGA_VERTB, mouse_interrupt, 0, NULL, "Amiga mouse"))
	{
		mouse.present = 0;
		printk(KERN_INFO "Installing Amiga mouse failed.\n");
		return -EIO;
	}

	mouse.present = 1;

	printk(KERN_INFO "Amiga mouse installed.\n");
	misc_register(&amiga_mouse);
	return 0;
}

#ifdef MODULE
#include <asm/bootinfo.h>

int init_module(void)
{
	return amiga_mouse_init();
}

void cleanup_module(void)
{
  remove_isr(IRQ_AMIGA_VERTB, mouse_interrupt, NULL);
  misc_deregister(&amiga_mouse);
}
#endif
