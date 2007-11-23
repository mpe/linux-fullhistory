/*
 *  evdev.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Event char devices, giving access to raw input device events.
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

#define EVDEV_MINOR_BASE	64
#define EVDEV_BUFFER_SIZE	64

#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>

struct evdev {
	char name[32];
	int used;
	struct input_handle handle;
	struct miscdevice misc;
	wait_queue_head_t wait;
	struct evdev_list *list;
};

struct evdev_list {
	struct input_event buffer[EVDEV_BUFFER_SIZE];
	int head;
	int tail;
	struct fasync_struct *fasync;
	struct evdev *evdev;
	struct evdev_list *next;
};

static unsigned long evdev_miscbits = 0;
static struct evdev *evdev_base[BITS_PER_LONG];

static void evdev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct evdev *evdev = handle->private;
	struct evdev_list *list = evdev->list;

	while (list) {

		get_fast_time(&list->buffer[list->head].time);
		list->buffer[list->head].type = type;
		list->buffer[list->head].code = code;
		list->buffer[list->head].value = value;
		list->head = (list->head + 1) & (EVDEV_BUFFER_SIZE - 1);
		
		if (list->fasync)
			kill_fasync(list->fasync, SIGIO, POLL_IN);

		list = list->next;
	}

	wake_up_interruptible(&evdev->wait);
}

static int evdev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct evdev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static int evdev_release(struct inode * inode, struct file * file)
{
	struct evdev_list *list = file->private_data;
	struct evdev_list **listptr = &list->evdev->list;

	evdev_fasync(-1, file, 0);

	while (*listptr && (*listptr != list))
		listptr = &((*listptr)->next);
	*listptr = (*listptr)->next;
	
	if (!--list->evdev->used) {
		clear_bit(list->evdev->misc.minor - EVDEV_MINOR_BASE, &evdev_miscbits);
		misc_deregister(&list->evdev->misc);
		kfree(list->evdev);
	}

	kfree(list);

	MOD_DEC_USE_COUNT;
	return 0;
}

static int evdev_open(struct inode * inode, struct file * file)
{
	struct evdev_list *list;
	int i = MINOR(inode->i_rdev) - EVDEV_MINOR_BASE;

	if (i > BITS_PER_LONG || !test_bit(i, &evdev_miscbits))
		return -ENODEV;

	if (!(list = kmalloc(sizeof(struct evdev_list), GFP_KERNEL)))
		return -ENOMEM;

	memset(list, 0, sizeof(struct evdev_list));

	list->evdev = evdev_base[i];
	list->next = evdev_base[i]->list;
	evdev_base[i]->list = list;

	file->private_data = list;

	list->evdev->used++;

	MOD_INC_USE_COUNT;
	return 0;
}

static ssize_t evdev_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t evdev_read(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct evdev_list *list = file->private_data;
	int retval = 0;

	if (list->head == list->tail) {

		add_wait_queue(&list->evdev->wait, &wait);
		current->state = TASK_INTERRUPTIBLE;

		while (list->head == list->tail) {

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
		remove_wait_queue(&list->evdev->wait, &wait);
	}

	if (retval)
		return retval;

	while (list->head != list->tail && retval + sizeof(struct input_event) <= count) {
		if (copy_to_user(buffer + retval, list->buffer + list->tail,
			 sizeof(struct input_event))) return -EFAULT;
		list->tail = (list->tail + 1) & (EVDEV_BUFFER_SIZE - 1);
		retval += sizeof(struct input_event);
	}

	return retval;	
}

static unsigned int evdev_poll(struct file *file, poll_table *wait)
{
	struct evdev_list *list = file->private_data;
	poll_wait(file, &list->evdev->wait, wait);
	if (list->head != list->tail)
		return POLLIN | POLLRDNORM;
	return 0;
}

static struct file_operations evdev_fops = {
	read:		evdev_read,
	write:		evdev_write,
	poll:		evdev_poll,
	open:		evdev_open,
	release:	evdev_release,
	fasync:		evdev_fasync,
};

static int evdev_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct evdev *evdev;

	if (!(evdev = kmalloc(sizeof(struct evdev), GFP_KERNEL)))
		return -1;

	memset(evdev, 0, sizeof(struct evdev));

	init_waitqueue_head(&evdev->wait);

	evdev->misc.minor = ffz(evdev_miscbits);
	set_bit(evdev->misc.minor, &evdev_miscbits);
	evdev_base[evdev->misc.minor] = evdev;

	sprintf(evdev->name, "evdev%d", evdev->misc.minor);
	evdev->misc.name = evdev->name;
	evdev->misc.minor += EVDEV_MINOR_BASE;
	evdev->misc.fops = &evdev_fops;

	evdev->handle.dev = dev;
	evdev->handle.handler = handler;
	evdev->handle.private = evdev;

	evdev->used = 1;

	misc_register(&evdev->misc);
	input_open_device(&evdev->handle);

	printk("%s: Event device for input%d on misc%d - /dev/input%d\n",
		evdev->name, dev->number, evdev->misc.minor, evdev->misc.minor - EVDEV_MINOR_BASE);

	return 0;
}

static void evdev_disconnect(struct input_handle *handle)
{
	struct evdev *evdev = handle->private;

	input_close_device(handle);

	if (!--evdev->used) {
		clear_bit(evdev->misc.minor - EVDEV_MINOR_BASE, &evdev_miscbits);
		misc_deregister(&evdev->misc);
		kfree(evdev);
	}
}
	
static struct input_handler evdev_handler = {
	event:		evdev_event,
	connect:	evdev_connect,
	disconnect:	evdev_disconnect,
};

static int __init evdev_init(void)
{
	input_register_handler(&evdev_handler);
	return 0;
}

static void __exit evdev_exit(void)
{
	input_unregister_handler(&evdev_handler);
}

module_init(evdev_init);
module_exit(evdev_exit);
