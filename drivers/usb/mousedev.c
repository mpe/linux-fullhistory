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
#define MOUSEDEV_MINORS		32

#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/config.h>

#ifndef CONFIG_MOUSEDEV_SCREEN_X
#define CONFIG_MOUSEDEV_SCREEN_X	1024
#endif
#ifndef CONFIG_MOUSEDEV_SCREEN_Y
#define CONFIG_MOUSEDEV_SCREEN_Y	768
#endif

struct mousedev {
	int used;
	int minor;
	wait_queue_head_t wait;
	struct mousedev_list *list;
	devfs_handle_t devfs;
};

struct mousedev_list {
	struct fasync_struct *fasync;
	struct mousedev *mousedev;
	struct mousedev_list *next;
	int dx, dy, dz, oldx, oldy;
	char ps2[6];
	unsigned long buttons;
	unsigned char ready, buffer, bufsiz;
	unsigned char mode, genseq, impseq;
};

#define MOUSEDEV_GENIUS_LEN	5
#define MOUSEDEV_IMPS_LEN	6

static unsigned char mousedev_genius_seq[] = { 0xe8, 3, 0xe6, 0xe6, 0xe6 };
static unsigned char mousedev_imps_seq[] = { 0xf3, 200, 0xf3, 100, 0xf3, 80 };

static struct mousedev *mousedev_table[MOUSEDEV_MINORS];

static void mousedev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct mousedev *mousedev = handle->private;
	struct mousedev_list *list = mousedev->list;
	int index, size;

	while (list) {
		switch (type) {
			case EV_ABS:
				switch (code) {
					case ABS_X:	
						size = handle->dev->absmax[ABS_X] - handle->dev->absmin[ABS_X];
						list->dx += (value * CONFIG_MOUSEDEV_SCREEN_X - list->oldx) / size;
						list->oldx += list->dx * size;
						break;
					case ABS_Y:
						size = handle->dev->absmax[ABS_Y] - handle->dev->absmin[ABS_Y];
						list->dy -= (value * CONFIG_MOUSEDEV_SCREEN_Y - list->oldy) / size;
						list->oldy -= list->dy * size;
						break;
				}
				break;
			case EV_REL:
				switch (code) {
					case REL_X:	list->dx += value; break;
					case REL_Y:	list->dy -= value; break;
					case REL_WHEEL:	if (list->mode) list->dz -= value; break;
				}
				break;

			case EV_KEY:
				switch (code) {
					case BTN_0:
					case BTN_TOUCH:
					case BTN_LEFT:   index = 0; break;
					case BTN_4:
					case BTN_EXTRA:  if (list->mode > 1) { index = 4; break; }
					case BTN_STYLUS:
					case BTN_1:
					case BTN_RIGHT:  index = 1; break;
					case BTN_3:
					case BTN_SIDE:   if (list->mode > 1) { index = 3; break; }
					case BTN_2:
					case BTN_STYLUS2:
					case BTN_MIDDLE: index = 2; break;	
					default: return;
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
	
	if (!--list->mousedev->used) {
		input_unregister_minor(list->mousedev->devfs);
		mousedev_table[list->mousedev->minor] = NULL;
		kfree(list->mousedev);
	}

	kfree(list);

	MOD_DEC_USE_COUNT;
	return 0;
}

static int mousedev_open(struct inode * inode, struct file * file)
{
	struct mousedev_list *list;
	int i = MINOR(inode->i_rdev) - MOUSEDEV_MINOR_BASE;

	if (i > MOUSEDEV_MINORS || !mousedev_table[i])
		return -ENODEV;

	MOD_INC_USE_COUNT;

	if (!(list = kmalloc(sizeof(struct mousedev_list), GFP_KERNEL))) {
		MOD_DEC_USE_COUNT;
		return -ENOMEM;
	}
	memset(list, 0, sizeof(struct mousedev_list));

	list->mousedev = mousedev_table[i];
	list->next = mousedev_table[i]->list;
	mousedev_table[i]->list = list;
	list->mousedev->used++;

	file->private_data = list;

	return 0;
}

static void mousedev_packet(struct mousedev_list *list, unsigned char off)
{
	list->ps2[off] = 0x08 | ((list->dx < 0) << 4) | ((list->dy < 0) << 5) | (list->buttons & 0x07);
	list->ps2[off + 1] = (list->dx > 127 ? 127 : (list->dx < -127 ? -127 : list->dx));
	list->ps2[off + 2] = (list->dy > 127 ? 127 : (list->dy < -127 ? -127 : list->dy));
	list->dx -= list->ps2[off + 1];
	list->dy -= list->ps2[off + 2];
	list->bufsiz = off + 3;

	if (list->mode > 1)
		list->ps2[off] |= ((list->buttons & 0x30) << 2);
	
	if (list->mode) {
		list->ps2[off + 3] = (list->dz > 127 ? 127 : (list->dz < -127 ? -127 : list->dz));
		list->bufsiz++;
		list->dz -= list->ps2[off + 3];
	}
	if (!list->dx && !list->dy && (!list->mode || !list->dz)) list->ready = 0;
	list->buffer = list->bufsiz;
}


static ssize_t mousedev_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	struct mousedev_list *list = file->private_data;
	unsigned char c;
	int i;

	for (i = 0; i < count; i++) {

		c = buffer[i];

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
	struct mousedev *mousedev;
	struct input_handle *handle;
	int minor = 0;

	if (!test_bit(EV_KEY, dev->evbit) ||
	   (!test_bit(BTN_LEFT, dev->keybit) && !test_bit(BTN_TOUCH, dev->keybit)))
		return -1;

	if ((!test_bit(EV_REL, dev->evbit) || !test_bit(REL_X, dev->relbit)) &&
	    (!test_bit(EV_ABS, dev->evbit) || !test_bit(ABS_X, dev->absbit)))
		return -1;

#ifndef CONFIG_INPUT_MOUSEDEV_MIX
	for (minor = 0; minor < MOUSEDEV_MINORS && mousedev_table[minor]; minor++);
	if (mousedev_table[minor]) {
		printk(KERN_ERR "mousedev: no more free mousedev devices\n");
		return -1;
	}

	if (!(mousedev = kmalloc(sizeof(struct mousedev), GFP_KERNEL)))
		return -1;
	memset(mousedev, 0, sizeof(struct mousedev));
	init_waitqueue_head(&mousedev->wait);

	mousedev->devfs = input_register_minor("mouse%d", minor, MOUSEDEV_MINOR_BASE);
#else
	mousedev = mousedev_table[0];
#endif

	if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL))) {
		if (!mousedev->used) kfree(mousedev);
		return -1;
	}
	memset(handle, 0, sizeof(struct input_handle));

	mousedev->used++;
	mousedev->minor = minor;
	mousedev_table[minor] = mousedev;

	handle->dev = dev;
	handle->handler = handler;
	handle->private = mousedev;

	input_open_device(handle);

	printk("mouse%d: PS/2 mouse device for input%d\n", minor, dev->number);

	return 0;
}

static void mousedev_disconnect(struct input_handle *handle)
{
	struct mousedev *mousedev = handle->private;
	input_close_device(handle);
	kfree(handle);
	if (!--mousedev->used) {
		input_unregister_minor(mousedev->devfs);
		mousedev_table[mousedev->minor] = NULL;
		kfree(mousedev);
	}
}
	
static struct input_handler mousedev_handler = {
	event:		mousedev_event,
	connect:	mousedev_connect,
	disconnect:	mousedev_disconnect,
	fops:		&mousedev_fops,
	minor:		MOUSEDEV_MINOR_BASE,
};

static int __init mousedev_init(void)
{
	input_register_handler(&mousedev_handler);

#ifdef CONFIG_INPUT_MOUSEDEV_MIX
	if (!(mousedev_table[0] = kmalloc(sizeof(struct mousedev), GFP_KERNEL)))
		return -1;
	memset(mousedev_table[0], 0, sizeof(struct mousedev));
	init_waitqueue_head(&mousedev_table[0]->wait);
	mousedev_table[0]->devfs = input_register_minor("mouse%d", 0, MOUSEDEV_MINOR_BASE);
	mousedev_table[0]->used = 1;
#endif
	return 0;
}

static void __exit mousedev_exit(void)
{
#ifdef CONFIG_INPUT_MOUSEDEV_MIX
	input_unregister_minor(mousedev_table[0]->devfs);
	kfree(mousedev_table[0]);
#endif
	input_unregister_handler(&mousedev_handler);
}

module_init(mousedev_init);
module_exit(mousedev_exit);
