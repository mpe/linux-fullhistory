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

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mouse.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>

/*
 * Head entry for the doubly linked mouse list
 */
static struct mouse mouse_list = { 0, "head", NULL, &mouse_list, &mouse_list };

#ifndef MODULE
extern int bus_mouse_init(void);
extern int psaux_init(void);
extern int ms_bus_mouse_init(void);
extern int atixl_busmouse_init(void);
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
	MOD_INC_USE_COUNT;
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
	MOD_DEC_USE_COUNT;
	mouse->prev->next = mouse->next;
	mouse->next->prev = mouse->prev;
	mouse->next = NULL;
	mouse->prev = NULL;
	return 0;
}

#ifdef MODULE

#define mouse_init init_module

void cleanup_module(void)
{
	unregister_chrdev(MOUSE_MAJOR, "mouse");
}

#endif

static struct symbol_table mouse_syms = {
/* Should this be surrounded with "#ifdef CONFIG_MODULES" ? */
#include <linux/symtab_begin.h>
	X(mouse_register),
	X(mouse_deregister),
#include <linux/symtab_end.h>
};

int mouse_init(void)
{
#ifndef MODULE
#ifdef CONFIG_BUSMOUSE
	bus_mouse_init();
#endif
#if defined CONFIG_PSMOUSE || defined CONFIG_82C710_MOUSE
	psaux_init();
#endif
#ifdef CONFIG_MS_BUSMOUSE
	ms_bus_mouse_init();
#endif
#ifdef CONFIG_ATIXL_BUSMOUSE
 	atixl_busmouse_init();
#endif
#ifdef CONFIG_SOFT_WATCHDOG
	watchdog_init();
#endif	
#endif /* !MODULE */
	if (register_chrdev(MOUSE_MAJOR,"mouse",&mouse_fops)) {
	  printk("unable to get major %d for mouse devices\n",
		 MOUSE_MAJOR);
		return -EIO;
	}

	return register_symtab(&mouse_syms);
}
