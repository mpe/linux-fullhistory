/*
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
 * Minor addition by Cliff Matthews
 *   added fasync support
 *
 * Modularised 6-Sep-95 Philip Blundell <pjb27@cam.ac.uk> 
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/busmouse.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/irq.h>

static struct mouse_status mouse;
static int mouse_irq = MOUSE_IRQ;

void bmouse_setup(char *str, int *ints)
{
	if (ints[0] > 0)
		mouse_irq=ints[1];
}

static void mouse_interrupt(int irq, struct pt_regs *regs)
{
	char dx, dy;
	unsigned char buttons;

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
	if (dx != 0 || dy != 0 || buttons != mouse.buttons) {
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
	  if (mouse.dx >  2048)
	      mouse.dx =  2048;

	  if (mouse.dy < -2048)
	      mouse.dy = -2048;
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
 * close access to the mouse (can deal with multiple
 * opens if allowed in the future)
 */

static void close_mouse(struct inode * inode, struct file * file)
{
	if (--mouse.active == 0) {
	    MSE_INT_OFF();
	    free_irq(mouse_irq);
	}
	fasync_mouse(inode, file, 0);
}

/*
 * open access to the mouse, currently only one open is
 * allowed.
 */

static int open_mouse(struct inode * inode, struct file * file)
{
	if (!mouse.present)
		return -EINVAL;
	if (mouse.active)
		return -EBUSY;
	mouse.ready = 0;
	mouse.dx = 0;
	mouse.dy = 0;
	mouse.buttons = 0x87;
	if (request_irq(mouse_irq, mouse_interrupt, 0, "Busmouse"))
		return -EBUSY;
	mouse.active = 1;
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
	 * so paging in put_user() does not effect mouse tracking.
	 */

	MSE_INT_OFF();
	dx = mouse.dx;
	dy = mouse.dy;
	if (dx < -127)
	    dx = -127;
	if (dx > 127)
	    dx = 127;
	if (dy < -127)
	    dy = -127;
	if (dy > 127)
	    dy = 127;
	buttons = mouse.buttons;
	mouse.dx -= dx;
	mouse.dy -= dy;
	mouse.ready = 0;
	MSE_INT_ON();

	put_user(buttons | 0x80, buffer);
	put_user((char)dx, buffer + 1);
	put_user((char)dy, buffer + 2);
	for (r = 3; r < count; r++)
	    put_user(0x00, buffer + r);
	return r;
}

/*
 * select for mouse input, must disable the mouse interrupt while checking
 * mouse.ready/select_wait() to avoid race condition (though in reality
 * such a condition is not fatal to the proper operation of the mouse since
 * multiple interrupts generally occur).
 */

static int mouse_select(struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
    int r = 0;

    if (sel_type == SEL_IN) {
    	MSE_INT_OFF();
    	if (mouse.ready) {
    	    r = 1;
    	} else {
	    select_wait(&mouse.wait, wait);
    	}
    	MSE_INT_ON();
    }
    return(r);
}

struct file_operations bus_mouse_fops = {
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

static struct mouse bus_mous = {
	LOGITECH_BUSMOUSE, "busmouse", &bus_mouse_fops
};

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;

int init_module(void)
#else
unsigned long bus_mouse_init(unsigned long kmem_start)
#endif
{
	int i;

	outb(MSE_CONFIG_BYTE, MSE_CONFIG_PORT);
	outb(MSE_SIGNATURE_BYTE, MSE_SIGNATURE_PORT);
	for (i = 0; i < 100000; i++)
		/* busy loop */;
	if (inb(MSE_SIGNATURE_PORT) != MSE_SIGNATURE_BYTE) {
		mouse.present = 0;
#ifdef MODULE
		return -EIO;
#else
		return kmem_start;
#endif
	}
	outb(MSE_DEFAULT_MODE, MSE_CONFIG_PORT);
	MSE_INT_OFF();
	mouse.present = 1;
	mouse.active = 0;
	mouse.ready = 0;
	mouse.buttons = 0x87;
	mouse.dx = 0;
	mouse.dy = 0;
	mouse.wait = NULL;
	printk("Logitech Bus mouse detected and installed with IRQ %d.\n",
	       mouse_irq);
	mouse_register(&bus_mouse);
#ifdef MODULE
	return 0;
#else
	return kmem_start;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
	if (MOD_IN_USE)
		printk("busmouse: in use - remove delayed\n");
	mouse_deregister(&bus_mouse);
}
#endif
