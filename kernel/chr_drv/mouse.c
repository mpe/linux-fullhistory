/*
 * linux/kernel/chr_drv/mouse.c
 *
 * Generic mouse open routine by Johan Myreen
 *
 * Based on code from Linus
 */

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mouse.h>

extern struct file_operations bus_mouse_fops;
extern struct file_operations psaux_fops;
extern long bus_mouse_init(long);
extern long psaux_init(long);

static int mouse_open(struct inode * inode, struct file * file)
{
        if (MINOR(inode->i_rdev) == BUSMOUSE_MINOR)
                file->f_op = &bus_mouse_fops;
        else if (MINOR(inode->i_rdev) == PSMOUSE_MINOR)
                file->f_op = &psaux_fops;
        else
                return -ENODEV;
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
	chrdev_fops[10] = &mouse_fops;
	return kmem_start;
}
