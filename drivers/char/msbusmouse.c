/*
 * Microsoft busmouse driver based on Logitech driver (see busmouse.c)
 *
 * Microsoft BusMouse support by Teemu Rantanen (tvr@cs.hut.fi) (02AUG92)
 *
 * Microsoft Bus Mouse support modified by Derrick Cole (cole@concert.net)
 *    8/28/92
 *
 * Microsoft Bus Mouse support folded into 0.97pl4 code
 *    by Peter Cervasio (pete%q106fm.uucp@wupost.wustl.edu) (08SEP92)
 * Changes:  Logitech and Microsoft support in the same kernel.
 *           Defined new constants in busmouse.h for MS mice.
 *           Added int mse_busmouse_type to distinguish busmouse types
 *           Added a couple of new functions to handle differences in using
 *             MS vs. Logitech (where the int variable wasn't appropriate).
 *
 * Modified by Peter Cervasio (address above) (26SEP92)
 * Changes:  Included code to (properly?) detect when a Microsoft mouse is
 *           really attached to the machine.  Don't know what this does to
 *           Logitech bus mice, but all it does is read ports.
 *
 * Modified by Christoph Niemann (niemann@rubdv15.etdv.ruhr-uni-bochum.de)
 * Changes:  Better interrupt-handler (like in busmouse.c).
 *	     Some changes to reduce code-size.
 *	     Changed detection code to use inb_p() instead of doing empty
 *	     loops to delay i/o.
 *
 * Modularised 8-Sep-95 Philip Blundell <pjb27@cam.ac.uk>
 *
 * version 0.3b
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/busmouse.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/random.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/irq.h>

static struct mouse_status mouse;
static int mouse_irq = MOUSE_IRQ;

void msmouse_setup(char *str, int *ints)
{
	if (ints[0] > 0)
		mouse_irq=ints[1];
}

static void ms_mouse_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
        char dx, dy;
	unsigned char buttons;

	outb(MS_MSE_COMMAND_MODE, MS_MSE_CONTROL_PORT);
	outb((inb(MS_MSE_DATA_PORT) | 0x20), MS_MSE_DATA_PORT);

	outb(MS_MSE_READ_X, MS_MSE_CONTROL_PORT);
	dx = inb(MS_MSE_DATA_PORT);

	outb(MS_MSE_READ_Y, MS_MSE_CONTROL_PORT);
	dy = inb(MS_MSE_DATA_PORT);

	outb(MS_MSE_READ_BUTTONS, MS_MSE_CONTROL_PORT);
	buttons = ~(inb(MS_MSE_DATA_PORT)) & 0x07;

	outb(MS_MSE_COMMAND_MODE, MS_MSE_CONTROL_PORT);
	outb((inb(MS_MSE_DATA_PORT) & 0xdf), MS_MSE_DATA_PORT);

	if (dx != 0 || dy != 0 || buttons != mouse.buttons || ((~buttons) & 0x07)) {
		add_mouse_randomness((buttons << 16) + (dy << 8) + dx);
		mouse.buttons = buttons;
		mouse.dx += dx;
		mouse.dy += dy;
		mouse.ready = 1;
		wake_up_interruptible(&mouse.wait);
		if (mouse.fasyncptr)
			kill_fasync(mouse.fasyncptr, SIGIO);
	}
}

static int fasync_mouse(struct inode *inode, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(inode, filp, on, &mouse.fasyncptr);
	if (retval < 0)
		return retval;
	return 0;
}

static void release_mouse(struct inode * inode, struct file * file)
{
	fasync_mouse(inode, file, 0);
	if (--mouse.active)
		return;
	MS_MSE_INT_OFF();
	mouse.ready = 0; 
	free_irq(mouse_irq, NULL);
	MOD_DEC_USE_COUNT;
}

static int open_mouse(struct inode * inode, struct file * file)
{
	if (!mouse.present)
		return -EINVAL;
	if (mouse.active++)
		return 0;
	if (request_irq(mouse_irq, ms_mouse_interrupt, 0, "MS Busmouse", NULL)) {
		mouse.active--;
		return -EBUSY;
	}
	mouse.ready = mouse.dx = mouse.dy = 0;	
	mouse.buttons = 0x80;
	outb(MS_MSE_START, MS_MSE_CONTROL_PORT);
	MOD_INC_USE_COUNT;
	MS_MSE_INT_ON();	
	return 0;
}


static int write_mouse(struct inode * inode, struct file * file, const char * buffer, int count)
{
	return -EINVAL;
}

static int read_mouse(struct inode * inode, struct file * file, char * buffer, int count)
{
	int i, dx, dy;

	if (count < 3)
		return -EINVAL;
	if (!mouse.ready)
		return -EAGAIN;
	put_user(mouse.buttons | 0x80, buffer);
	dx = mouse.dx < -127 ? -127 : mouse.dx > 127 ?  127 :  mouse.dx;
	dy = mouse.dy < -127 ?  127 : mouse.dy > 127 ? -127 : -mouse.dy;
	put_user((char)dx, buffer + 1);
	put_user((char)dy, buffer + 2);
	for (i = 3; i < count; i++)
		put_user(0x00, buffer + i);
	mouse.dx -= dx;
	mouse.dy += dy;
	mouse.ready = 0;
	return i;	
}

static int mouse_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	if (sel_type != SEL_IN)
		return 0;
	if (mouse.ready) 
		return 1;
	select_wait(&mouse.wait,wait);
	return 0;
}

struct file_operations ms_bus_mouse_fops = {
	NULL,		/* mouse_seek */
	read_mouse,
	write_mouse,
	NULL, 		/* mouse_readdir */
	mouse_select, 	/* mouse_select */
	NULL, 		/* mouse_ioctl */
	NULL,		/* mouse_mmap */
	open_mouse,
	release_mouse,
	NULL,
	fasync_mouse,
};

static struct miscdevice ms_bus_mouse = {
	MICROSOFT_BUSMOUSE, "msbusmouse", &ms_bus_mouse_fops
};

int ms_bus_mouse_init(void)
{
	int mse_byte, i;

	mouse.present = mouse.active = mouse.ready = 0;
	mouse.buttons = 0x80;
	mouse.dx = mouse.dy = 0;
	mouse.wait = NULL;

	if (check_region(MS_MSE_CONTROL_PORT, 0x04))
		return -ENODEV;

	if (inb_p(MS_MSE_SIGNATURE_PORT) == 0xde) {

		mse_byte = inb_p(MS_MSE_SIGNATURE_PORT);

		for (i = 0; i < 4; i++) {
			if (inb_p(MS_MSE_SIGNATURE_PORT) == 0xde) {
				if (inb_p(MS_MSE_SIGNATURE_PORT) == mse_byte)
					mouse.present = 1;
				else
					mouse.present = 0;
			} else
				mouse.present = 0;
		}
	}
	if (mouse.present == 0)
		return -EIO;
	MS_MSE_INT_OFF();
	request_region(MS_MSE_CONTROL_PORT, 0x04, "MS Busmouse");
	printk(KERN_INFO "Microsoft BusMouse detected and installed.\n");
	misc_register(&ms_bus_mouse);
	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return ms_bus_mouse_init();
}

void cleanup_module(void)
{
	misc_deregister(&ms_bus_mouse);
	release_region(MS_MSE_CONTROL_PORT, 0x04);
}
#endif

