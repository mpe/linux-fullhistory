/*
 * Logitech Bus Mouse Driver for Linux
 * by James Banks
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
 * version 0.2
 */

#include	<linux/kernel.h>
#include	<linux/sched.h>
#include	<linux/busmouse.h>
#include	<linux/tty.h>
#include	<linux/signal.h>
#include	<linux/errno.h>

#include	<asm/io.h>
#include	<asm/segment.h>
#include	<asm/system.h>
#include	<asm/irq.h>


static struct mouse_status mouse;

static void mouse_interrupt(int unused)
{
	char dx, dy, buttons;

	MSE_INT_OFF();
	
	outb(MSE_READ_X_LOW, MSE_CONTROL_PORT);
	dx = (inb(MSE_DATA_PORT) & 0xf);
	
	outb(MSE_READ_X_HIGH, MSE_CONTROL_PORT);
	dx |= (inb(MSE_DATA_PORT) & 0xf) << 4;
	
	outb(MSE_READ_Y_LOW, MSE_CONTROL_PORT );
	dy = (inb(MSE_DATA_PORT) & 0xf);
	
	outb(MSE_READ_Y_HIGH, MSE_CONTROL_PORT);
	buttons = inb(MSE_DATA_PORT);

	dy |= (buttons & 0xf) << 4;
	buttons = ((buttons >> 5) & 0x07);

	mouse.buttons = buttons;
	mouse.latch_buttons |= buttons;
	mouse.dx += dx;
	mouse.dy += dy;
	mouse.ready = 1;
	if (mouse.inode && mouse.inode->i_wait)
		 wake_up(&mouse.inode->i_wait);
	
	MSE_INT_ON();
	
}

/*  Use separate function for MS mice - keep both short & fast */
static void ms_mouse_interrupt(int unused)
{
	char dx, dy, buttons;

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

	mouse.buttons = buttons;
	mouse.latch_buttons |= buttons;
	mouse.dx += dx;
	mouse.dy += dy;
	mouse.ready = 1;
	if (mouse.inode && mouse.inode->i_wait)
		 wake_up(&mouse.inode->i_wait);
	
}

static void release_mouse(struct inode * inode, struct file * file)
{
        if (mse_busmouse_type == LOGITECH_BUSMOUSE) {
	  MSE_INT_OFF();
	} else if (mse_busmouse_type == MICROSOFT_BUSMOUSE) {
	  MS_MSE_INT_OFF();
	} /* else if next mouse type, etc. */

	mouse.active = 0;
	mouse.ready = 0; 
	mouse.inode = NULL;
	free_irq(MOUSE_IRQ);
}

static int open_mouse(struct inode * inode, struct file * file)
{
	if (mouse.active)
		return -EBUSY;
	if (!mouse.present)
		return -EINVAL;
	mouse.active = 1;
	mouse.ready = 0;
	mouse.inode = inode;
	mouse.dx = 0;
	mouse.dy = 0;	
	mouse.buttons = mouse.latch_buttons = 0x80;

	if (mse_busmouse_type == LOGITECH_BUSMOUSE) {
	  if (request_irq(MOUSE_IRQ, mouse_interrupt)) {
	    /* once we get to here mouse is unused, IRQ is busy */
	    mouse.active = 0;  /* it's not active, fix it */
	    return -EBUSY;     /* IRQ is busy, so we're BUSY */
	  } /* if we can't get the IRQ and mouse not active */
	  MSE_INT_ON();

	} else if (mse_busmouse_type == MICROSOFT_BUSMOUSE) {

	  if (request_irq(MOUSE_IRQ, ms_mouse_interrupt)) {
	    /* once we get to here mouse is unused, IRQ is busy */
	    mouse.active = 0;  /* it's not active, fix it */
	    return -EBUSY;     /* IRQ is busy, so we're BUSY */
	  } /* if we can't get the IRQ and mouse not active */
	  outb(MS_MSE_START, MS_MSE_CONTROL_PORT);
	  MS_MSE_INT_ON();	

	}

	return 0;
}


static int write_mouse(struct inode * inode, struct file * file, char * buffer, int count)
{
	return -EINVAL;
}

static int read_mouse(struct inode * inode, struct file * file, char * buffer, int count)
{
	int i;

	if (count < 3) return -EINVAL;
	if (!mouse.ready) return -EAGAIN;

	if (mse_busmouse_type == LOGITECH_BUSMOUSE) {
	  MSE_INT_OFF();
	}
		
	put_fs_byte(mouse.latch_buttons | 0x80, buffer);
	
	if (mouse.dx < -127) mouse.dx = -127;
	if (mouse.dx >  127) mouse.dx =  127;
	
	put_fs_byte((char)mouse.dx, buffer + 1);
	
	if (mouse.dy < -127) mouse.dy = -127;
	if (mouse.dy >  127) mouse.dy =  127;
	
	put_fs_byte((char) -mouse.dy, buffer + 2);
	
	for (i = 3; i < count; i++)
		put_fs_byte(0x00, buffer + i);
		
	mouse.dx = 0;
	mouse.dy = 0;
	mouse.latch_buttons = mouse.buttons;
	mouse.ready = 0;
	
	if (mse_busmouse_type == LOGITECH_BUSMOUSE) {
	  MSE_INT_ON();
	}

	return i;	
}

static int mouse_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
	if (sel_type != SEL_IN)
		return 0;
	if (mouse.ready) 
		return 1;
	select_wait(&inode->i_wait,wait);
	return 0;
}

struct file_operations bus_mouse_fops = {
	NULL,		/* mouse_seek */
	read_mouse,
	write_mouse,
	NULL, 		/* mouse_readdir */
	mouse_select, 	/* mouse_select */
	NULL, 		/* mouse_ioctl */
	open_mouse,
	release_mouse,
};

long bus_mouse_init(long kmem_start)
{	
	int i;

	outb(MSE_CONFIG_BYTE, MSE_CONFIG_PORT);
	outb(MSE_SIGNATURE_BYTE, MSE_SIGNATURE_PORT);
	
	for (i = 0; i < 100000; i++); /* busy loop */
	if (inb(MSE_SIGNATURE_PORT) != MSE_SIGNATURE_BYTE) {
		printk("No Logitech bus mouse detected.\n");
		mouse.present = 0;
		return kmem_start;
	}
	outb(MSE_DEFAULT_MODE, MSE_CONFIG_PORT);
	
	MSE_INT_OFF();
	
	mouse.present = 1;
	mouse.active = 0;
	mouse.ready = 0;
	mouse.buttons = mouse.latch_buttons = 0x80;
	mouse.dx = 0;
	mouse.dy = 0;
	printk("Logitech Bus mouse detected and installed.\n");
	return kmem_start;
}

long ms_bus_mouse_init(long kmem_start)
{	
	
	MS_MSE_INT_OFF();
	
	mouse.present = 1;
	mouse.active = mouse.ready = 0;
	mouse.buttons = mouse.latch_buttons = 0x80;
	mouse.dx = mouse.dy = 0;
	printk("Microsoft Bus mouse detected and installed.\n");
	return kmem_start;
}
