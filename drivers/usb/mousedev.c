/*
 *  mousedev.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Input driver to PS/2 or ImPS/2 device driver module.
 *
 *  Sponsored by SuSE
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#define MOUSEDEV_MINOR_BASE 	32

#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/config.h>

struct mousedev {
	char name[32];
	int used;
	struct input_handle handle;
	struct miscdevice misc;
	wait_queue_head_t wait;
	struct mousedev_list *list;
};

struct mousedev_list {
	struct fasync_struct *fasync;
	struct mousedev *mousedev;
	struct mousedev_list *next;
	int dx, dy, dz;
	unsigned char ps2[6];
	unsigned char buttons;
	unsigned char ready, buffer, bufsiz;
	unsigned char mode, genseq, impseq;
};

#define MOUSEDEV_GENIUS_LEN	5
#define MOUSEDEV_IMPS_LEN	6

static unsigned char mousedev_genius_seq[] = { 0xe8, 3, 0xe6, 0xe6, 0xe6 };
static unsigned char mousedev_imps_seq[] = { 0xf3, 200, 0xf3, 100, 0xf3, 80 };

#ifdef CONFIG_INPUT_MOUSEDEV_MIX
static struct mousedev mousedev_single;
#else
static unsigned long mousedev_miscbits = 0;
static struct mousedev *mousedev_base[BITS_PER_LONG];
#endif

static void mousedev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct mousedev *mousedev = handle->private;
	struct mousedev_list *list = mousedev->list;
	int index;

	while (list) {
		switch (type) {
			case EV_REL:
				switch (code) {
					case REL_X:	list->dx += value; break;
					case REL_Y:	list->dy -= value; break;
					case REL_WHEEL:	if (list->mode) list->dz += value; break;
				}
				break;

			case EV_KEY:
				switch (code) {
					case BTN_LEFT:   index = 0; break;
					case BTN_EXTRA:  if (list->mode > 1) { index = 4; break; }
					case BTN_RIGHT:  index = 1; break;
					case BTN_SIDE:   if (list->mode > 1) { index = 3; break; }
					case BTN_MIDDLE: index = 2; break;	
					default: index = 0;
				}
				switch (value) {
					case 0: clear_bit(index, &list->buttons); break;
					case 1: set_bit(index, &list->buttons); break;
					case 2: return;
				}
				break;
		}
				
		list->ready = 1;

		if (list->fasync)
			kill_fasync(list->fasync, SIGIO, POLL_IN);

		list = list->next;
	}

	wake_up_interruptible(&mousedev->wait);
}

static int mousedev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct mousedev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static int mousedev_release(struct inode * inode, struct file * file)
{
	struct mousedev_list *list = file->private_data;
	struct mousedev_list **listptr = &list->mousedev->list;

	mousedev_fasync(-1, file, 0);

	while (*listptr && (*listptr != list))
		listptr = &((*listptr)->next);
	*listptr = (*listptr)->next;
	
#ifndef CONFIG_INPUT_MOUSEDEV_MIX
	if (!--list->mousedev->used) {
		clear_bit(list->mousedev->misc.minor - MOUSEDEV_MINOR_BASE, &mousedev_miscbits);
		misc_deregister(&list->mousedev->misc);
		kfree(list->mousedev);
	}
#endif

	kfree(list);

	MOD_DEC_USE_COUNT;
	return 0;
}

static int mousedev_open(struct inode * inode, struct file * file)
{
	struct mousedev_list *list;

#ifndef CONFIG_INPUT_MOUSEDEV_MIX
	int i = MINOR(inode->i_rdev) - MOUSEDEV_MINOR_BASE;
	if (i > BITS_PER_LONG || !test_bit(i, &mousedev_miscbits))
		return -ENODEV;
#endif

	if (!(list = kmalloc(sizeof(struct mousedev_list), GFP_KERNEL)))
		return -ENOMEM;

	memset(list, 0, sizeof(struct mousedev_list));


#ifdef CONFIG_INPUT_MOUSEDEV_MIX
	list->mousedev = &mousedev_single;
	list->next = mousedev_single.list;
	mousedev_single.list = list;
#else
	list->mousedev = mousedev_base[i];
	list->next = mousedev_base[i]->list;
	mousedev_base[i]->list = list;
	list->mousedev->used++;
#endif

	file->private_data = list;

	MOD_INC_USE_COUNT;
	return 0;
}

static void mousedev_packet(struct mousedev_list *list, unsigned char off)
{
	list->ps2[off] = 0x08 | ((list->dx < 0) << 4) | ((list->dy < 0) << 5) | (list->buttons & 0x07);
	list->ps2[off + 1] = (list->dx > 127 ? 127 : (list->dx < -127 ? -127 : list->dx));
	list->ps2[off + 2] = (list->dy > 127 ? 127 : (list->dy < -127 ? -127 : list->dy));
	list->dx = list->dy = 0;
	list->bufsiz = off + 3;

	if (list->mode > 1)
		list->ps2[off] |= ((list->buttons & 0x30) << 2);
	
	if (list->mode) {
		list->ps2[off + 3] = (list->dz > 127 ? 127 : (list->dz < -127 ? -127 : list->dz));
		list->bufsiz++;
		list->dz = 0;
	}
	list->ready = 0;
	list->buffer = list->bufsiz;
}


static ssize_t mousedev_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	struct mousedev_list *list = file->private_data;
	unsigned char c;
	int i;

	for (i = 0; i < count; i++) {

		c = buffer[i];

#ifdef MOUSEDEV_DEBUG
		printk(KERN_DEBUG "mousedev: received char %#x\n", c);
#endif

		if (c == mousedev_genius_seq[list->genseq]) {
			if (++list->genseq == MOUSEDEV_GENIUS_LEN) {
				list->genseq = 0;
				list->ready = 1;
				list->mode = 2;
			}
		} else list->genseq = 0;

		if (c == mousedev_imps_seq[list->impseq]) {
			if (++list->impseq == MOUSEDEV_IMPS_LEN) {
				list->impseq = 0;
				list->ready = 1;
				list->mode = 1;
			}
		} else list->impseq = 0;

		list->ps2[0] = 0xfa;
		list->bufsiz = 1;

		switch (c) {

			case 0xeb: /* Poll */
				mousedev_packet(list, 1);
				break;

			case 0xf2: /* Get ID */
				list->ps2[1] = (list->mode == 1) ? 3 : 0;
				list->bufsiz = 2;
				break;

			case 0xe9: /* Get info */
				if (list->mode == 2) {
					list->ps2[1] = 0x00; list->ps2[2] = 0x33; list->ps2[3] = 0x55;
				} else {
					list->ps2[1] = 0x60; list->ps2[2] = 3; list->ps2[3] = 200;
				}
				list->bufsiz = 4;
				break;
		}

		list->buffer = list->bufsiz;
	}

	if (list->fasync)
		kill_fasync(list->fasync, SIGIO, POLL_IN);

	wake_up_interruptible(&list->mousedev->wait);
		
	return count;
}

static ssize_t mousedev_read(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct mousedev_list *list = file->private_data;
	int retval = 0;

	if (!list->ready && !list->buffer) {

		add_wait_queue(&list->mousedev->wait, &wait);
		current->state = TASK_INTERRUPTIBLE;

		while (!list->ready) {

			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				retval = -ERESTARTSYS;
				break;
			}

			schedule();
		}

		current->state = TASK_RUNNING;
		remove_wait_queue(&list->mousedev->wait, &wait);
	}

	if (retval)
		return retval;

	if (!list->buffer)
		mousedev_packet(list, 0);

	if (count > list->buffer)
		count = list->buffer;

	if (copy_to_user(buffer, list->ps2 + list->bufsiz - list->buffer, count))
		return -EFAULT;
	
	list->buffer -= count;

	return count;	
}

static unsigned int mousedev_poll(struct file *file, poll_table *wait)
{
	struct mousedev_list *list = file->private_data;
	poll_wait(file, &list->mousedev->wait, wait);
	if (list->ready || list->buffer)
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations mousedev_fops = {
	read:		mousedev_read,
	write:		mousedev_write,
	poll:		mousedev_poll,
	open:		mousedev_open,
	release:	mousedev_release,
	fasync:		mousedev_fasync,
};

static int mousedev_connect(struct input_handler *handler, struct input_dev *dev)
{

	if (!(test_bit(EV_KEY, dev->evbit) && test_bit(EV_REL, dev->evbit)))	/* The device must have both rels and keys */
		return -1;

	if (!(test_bit(REL_X, dev->relbit) && test_bit(REL_Y, dev->relbit)))	/* It must be a pointer device */
		return -1;
	
	if (!test_bit(BTN_LEFT, dev->keybit))				/* And have at least one mousebutton */
		return -1;

#ifdef CONFIG_INPUT_MOUSEDEV_MIX
	{
		struct input_handle *handle;

		if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL)))
			return -1;

		memset(handle, 0, sizeof(struct input_handle));

		handle->dev = dev;
		handle->handler = handler;
		handle->private = &mousedev_single;

		input_open_device(handle);

		printk("mousedev.c: Adding mouse: input%d\n", dev->number);
	}
#else
	{
		struct mousedev *mousedev;

		if (!(mousedev = kmalloc(sizeof(struct mousedev), GFP_KERNEL)))
			return -1;

		memset(mousedev, 0, sizeof(struct mousedev));

		mousedev->misc.minor = ffz(mousedev_miscbits);
		set_bit(mousedev->misc.minor, &mousedev_miscbits);
		mousedev_base[mousedev->misc.minor] = mousedev;

		sprintf(mousedev->name, "mousedev%d", mousedev->misc.minor);
		mousedev->misc.name = mousedev->name;
		mousedev->misc.minor += MOUSEDEV_MINOR_BASE;
		mousedev->misc.fops = &mousedev_fops;

		mousedev->handle.dev = dev;
		mousedev->handle.handler = handler;
		mousedev->handle.private = mousedev;

		init_waitqueue_head(&mousedev->wait);

		mousedev->used = 1;

		misc_register(&mousedev->misc);
		input_open_device(&mousedev->handle);

		printk("%s: PS/2 mouse device for input%d on misc%d\n",
			mousedev->name, dev->number, mousedev->misc.minor);
	}
#endif

	return 0;
}

static void mousedev_disconnect(struct input_handle *handle)
{
#ifdef CONFIG_INPUT_MOUSEDEV_MIX
	printk("mousedev.c: Removing mouse: input%d\n", handle->dev->number);
	input_close_device(handle);
	kfree(handle);
#else
	struct mousedev *mousedev = handle->private;
	input_close_device(handle);
	if (!--mousedev->used) {
		clear_bit(mousedev->misc.minor - MOUSEDEV_MINOR_BASE, &mousedev_miscbits);
		misc_deregister(&mousedev->misc);
		kfree(mousedev);
	}
#endif
}
	
static struct input_handler mousedev_handler = {
	event:		mousedev_event,
	connect:	mousedev_connect,
	disconnect:	mousedev_disconnect,
};


#ifdef MODULE
int init_module(void)
#else
int __init mousedev_init(void)
#endif
{
	input_register_handler(&mousedev_handler);

#ifdef CONFIG_INPUT_MOUSEDEV_MIX
	memset(&mousedev_single, 0, sizeof(struct mousedev));

	init_waitqueue_head(&mousedev_single.wait);
	mousedev_single.misc.minor = MOUSEDEV_MINOR_BASE;
	mousedev_single.misc.name = "mousedev";
	mousedev_single.misc.fops = &mousedev_fops;

	misc_register(&mousedev_single.misc);

	printk("mousedev: PS/2 mouse device on misc%d\n", mousedev_single.misc.minor);
#endif

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
#ifdef CONFIG_INPUT_MOUSEDEV_MIX
	misc_deregister(&mousedev_single.misc);
#endif

	input_unregister_handler(&mousedev_handler);
}
#endif
