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
 * version 0.1
 */

#include	<linux/kernel.h>
#include	<linux/sched.h>
#include	<linux/mouse.h>
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

static void release_mouse(struct inode * inode, struct file * file)
{
	MSE_INT_OFF();
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
	MSE_INT_ON();	
	if (request_irq(MOUSE_IRQ, mouse_interrupt)) {
		MSE_INT_OFF();
		mouse.active = 0;
		mouse.ready = 0;
		mouse.inode = NULL;
		return -EBUSY;
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
	
	MSE_INT_OFF();
		
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
	
	MSE_INT_ON();
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

static struct file_operations mouse_fops = {
	NULL,		/* mouse_seek */
	read_mouse,
	write_mouse,
	NULL, 		/* mouse_readdir */
	mouse_select, 	/* mouse_select */
	NULL, 		/* mouse_ioctl */
	open_mouse,
	release_mouse,
};

long mouse_init(long kmem_start)
{	
	int i;

	outb(MSE_CONFIG_BYTE, MSE_CONFIG_PORT);
	outb(MSE_SIGNATURE_BYTE, MSE_SIGNATURE_PORT);
	
	for (i = 0; i < 100000; i++); /* busy loop */
	if (inb(MSE_SIGNATURE_PORT) != MSE_SIGNATURE_BYTE) {
		printk("No bus mouse detected.\n");
		mouse.present = 0;
		return kmem_start;
	}
	chrdev_fops[10] = &mouse_fops;
	outb(MSE_DEFAULT_MODE, MSE_CONFIG_PORT);
	
	MSE_INT_OFF();
	
	mouse.present = 1;
	mouse.active = 0;
	mouse.ready = 0;
	mouse.buttons = mouse.latch_buttons = 0x80;
	mouse.dx = 0;
	mouse.dy = 0;
	printk("Bus mouse detected and installed.\n");
	return kmem_start;
}
