/*
 * linux/kernel/chr_drv/mouse.c
 *
 * Generic mouse open routine by Johan Myreen
 *
 * Based on code from Linus
 *
 * Teemu Rantanen's Microsoft Busmouse support and Derrick Cole's
 *   changes incorporated into 0.97pl4
 *   by Peter Cervasio (pete%q106fm.uucp@wupost.wustl.edu) (08SEP92)
 *   See busmouse.c for particulars.
 */

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mouse.h>

extern struct file_operations bus_mouse_fops;
extern struct file_operations psaux_fops;
extern long bus_mouse_init(long);
extern long psaux_init(long);
extern long ms_bus_mouse_init(long);

int mse_busmouse_type;

static int mouse_open(struct inode * inode, struct file * file)
{
        if (MINOR(inode->i_rdev) == BUSMOUSE_MINOR)
                file->f_op = &bus_mouse_fops;
        else if (MINOR(inode->i_rdev) == PSMOUSE_MINOR)
                file->f_op = &psaux_fops;
	else if (MINOR(inode->i_rdev) == MS_BUSMOUSE_MINOR)
	        file->f_op = &bus_mouse_fops;
        else
                return -ENODEV;
	mse_busmouse_type = (int) MINOR(inode->i_rdev);
        return file->f_op->open(inode,file);
}

static struct file_operations mouse_fops = {
        NULL,		/* seek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	NULL,		/* ioctl */
        mouse_open,
        NULL		/* release */
};

long mouse_init(long kmem_start)
{
	kmem_start = bus_mouse_init(kmem_start);
	kmem_start = psaux_init(kmem_start);
	kmem_start = ms_bus_mouse_init(kmem_start);
	mse_busmouse_type = -1;
	chrdev_fops[10] = &mouse_fops;
	return kmem_start;
}
