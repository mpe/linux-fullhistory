/*
 * Atari Joystick Driver for Linux
 * by Robert de Vries (robert@and.nl) 19Jul93
 *
 * 16 Nov 1994 Andreas Schwab
 * Support for three button mouse (shamelessly stolen from MiNT)
 * third button wired to one of the joystick directions on joystick 1
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/major.h>

#include <asm/atarikb.h>
#include <asm/atari_joystick.h>
#include <asm/atari_mouse.h>
#include <asm/uaccess.h>

#define MAJOR_NR    JOYSTICK_MAJOR

#define	ANALOG_JOY(n)	(!(n & 0x80))
#define	DIGITAL_JOY(n)	(n & 0x80)
#define	DEVICE_NR(n)	(MINOR(n) & 0x7f)


static struct joystick_status joystick[2];
int atari_mouse_buttons; /* for three-button mouse */

void atari_joystick_interrupt(char *buf)
{
    int j;
/*    ikbd_joystick_disable(); */

    j = buf[0] & 0x1;
    joystick[j].dir   = buf[1] & 0xF;
    joystick[j].fire  = (buf[1] & 0x80) >> 7;
    joystick[j].ready = 1;
    wake_up_interruptible(&joystick[j].wait);

    /* For three-button mouse emulation fake a mouse packet */
    if (atari_mouse_interrupt_hook &&
	j == 1 && (buf[1] & 1) != ((atari_mouse_buttons & 2) >> 1))
      {
	char faked_packet[3];

	atari_mouse_buttons = (atari_mouse_buttons & 5) | ((buf[1] & 1) << 1);
	faked_packet[0] = (atari_mouse_buttons & 1) | 
			  (atari_mouse_buttons & 4 ? 2 : 0);
	faked_packet[1] = 0;
	faked_packet[2] = 0;
	atari_mouse_interrupt_hook (faked_packet);
      }

/*    ikbd_joystick_event_on(); */
}

static void release_joystick(struct inode *inode, struct file *file)
{
    int minor = DEVICE_NR(inode->i_rdev);

    joystick[minor].active = 0;
    joystick[minor].ready = 0;

    if ((joystick[0].active == 0) && (joystick[1].active == 0))
	ikbd_joystick_disable();
}

static int open_joystick(struct inode *inode, struct file *file)
{
    int minor = DEVICE_NR(inode->i_rdev);

    if (!DIGITAL_JOY(inode->i_rdev) || minor > 1)
	return -ENODEV;
    if (joystick[minor].active)
	return -EBUSY;
    joystick[minor].active = 1;
    joystick[minor].ready = 0;
    ikbd_joystick_event_on();
    return 0;
}

static long write_joystick(struct inode *inode, struct file *file,
			   const char *buffer, unsigned long count)
{
    return -EINVAL;
}

static long read_joystick(struct inode *inode, struct file *file,
			  char *buffer, unsigned long count)
{
    int minor = DEVICE_NR(inode->i_rdev);
    int i;

    if (count < 2)
	return -EINVAL;
    if (!joystick[minor].ready)
	return -EAGAIN;
    put_user(joystick[minor].fire, buffer++);
    put_user(joystick[minor].dir, buffer++);
    for (i = 0; i < count; i++)
	put_user(0, buffer++);
    joystick[minor].ready = 0;

    return i;
}

static int joystick_select(struct inode *inode, struct file *file, int sel_type, select_table *wait)
{
    int minor = DEVICE_NR(inode->i_rdev);

    if (sel_type != SEL_IN)
	return 0;
    if (joystick[minor].ready)
	return 1;
    select_wait(&joystick[minor].wait, wait);
    return 0;
}

struct file_operations atari_joystick_fops = {
	NULL,		/* joystick_seek */
	read_joystick,
	write_joystick,
	NULL,		/* joystick_readdir */
	joystick_select,
	NULL,		/* joystick_ioctl */
	NULL,		/* joystick_mmap */
	open_joystick,
	release_joystick
};

int atari_joystick_init(void)
{
    joystick[0].active = joystick[1].active = 0;
    joystick[0].ready = joystick[1].ready = 0;
    joystick[0].wait = joystick[1].wait = NULL;

    if (register_chrdev(MAJOR_NR, "joystick", &atari_joystick_fops))
	printk("unable to get major %d for joystick devices\n", MAJOR_NR);

    return 0;
}
