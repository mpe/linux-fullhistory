/*
 * linux/drivers/char/mouse.c
 *
 * Generic mouse open routine by Johan Myreen
 *
 * Based on code from Linus
 *
 * Teemu Rantanen's Microsoft Busmouse support and Derrick Cole's
 *   changes incorporated into 0.97pl4
 *   by Peter Cervasio (pete%q106fm.uucp@wupost.wustl.edu) (08SEP92)
 *   See busmouse.c for particulars.
 *
 * Made things a lot mode modular - easy to compile in just one or two
 * of the mouse drivers, as they are now completely independent. Linus.
 *
 * Support for loadable modules. 8-Sep-95 Philip Blundell <pjb27@cam.ac.uk>
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mouse.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>

#include "mouse.h"

/*
 * Head entry for the doubly linked mouse list
 */
static struct mouse mouse_list = { 0, "head", NULL, &mouse_list, &mouse_list };

#ifndef MODULE
extern unsigned long bus_mouse_init(unsigned long);
extern unsigned long psaux_init(unsigned long);
extern unsigned long ms_bus_mouse_init(unsigned long);
extern unsigned long atixl_busmouse_init(unsigned long);
#endif

static int mouse_open(struct inode * inode, struct file * file)
{
	int minor = MINOR(inode->i_rdev);
	struct mouse *c = mouse_list.next;
	file->f_op = NULL;

	while (c != &mouse_list) {
		if (c->minor == minor) {
			file->f_op = c->fops;
			break;
		}
		c = c->next;
	}

	if (file->f_op == NULL)
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
	NULL,		/* mmap */
        mouse_open,
        NULL		/* release */
};

int mouse_register(struct mouse * mouse)
{
	if (mouse->next || mouse->prev)
		return -EBUSY;
	mouse->next = &mouse_list;
	mouse->prev = mouse_list.prev;
	mouse->prev->next = mouse;
	mouse->next->prev = mouse;
	return 0;
}

int mouse_deregister(struct mouse * mouse)
{
	if (!mouse->next || !mouse->prev)
		return -EINVAL;
	mouse->prev->next = mouse->next;
	mouse->next->prev = mouse->prev;
	mouse->next = NULL;
	mouse->prev = NULL;
	return 0;
}

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;

int init_module(void)
#else
unsigned long mouse_init(unsigned long kmem_start)
#endif
{
#ifndef MODULE
#ifdef CONFIG_BUSMOUSE
	kmem_start = bus_mouse_init(kmem_start);
#endif
#if defined CONFIG_PSMOUSE || defined CONFIG_82C710_MOUSE
	kmem_start = psaux_init(kmem_start);
#endif
#ifdef CONFIG_MS_BUSMOUSE
	kmem_start = ms_bus_mouse_init(kmem_start);
#endif
#ifdef CONFIG_ATIXL_BUSMOUSE
 	kmem_start = atixl_busmouse_init(kmem_start);
#endif
#endif /* !MODULE */
	if (register_chrdev(MOUSE_MAJOR,"mouse",&mouse_fops)) {
	  printk("unable to get major %d for mouse devices\n",
		 MOUSE_MAJOR);
#ifdef MODULE
		return -EIO;
#endif
	}
#ifdef MODULE
	return 0;
#else
	return kmem_start;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
	mouse_data *c = mouse_list, *n;
	if (MOD_IN_USE) {
		printk("mouse: in use, remove delayed\n");
		return;
	}
	unregister_chrdev(MOUSE_MAJOR, "mouse");
	while (c) {
		n = c->next;
		kfree(c);
		c = n;
	}
}
#endif
