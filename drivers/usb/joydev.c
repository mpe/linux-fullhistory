/*
 *  joydev.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik                                       
 *  Copyright (c) 1999 Colin Van Dyke 
 *
 *  Joystick device driver for the input driver suite.
 *
 *  Sponsored by SuSE and Intel
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

#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/init.h>

#define JOYDEV_MAJOR            15
#define JOYDEV_BUFFER_SIZE	64

struct joydev {
	char name[32];
	int used;
	struct input_handle handle;
	int minor;
	wait_queue_head_t wait;
	struct joydev *next;
	struct joydev_list *list;
	struct js_corr corr[ABS_MAX];
	struct JS_DATA_SAVE_TYPE glue;
	int nabs;
	int nkey;
	__u16 keymap[KEY_MAX - BTN_MISC];
	__u16 keypam[KEY_MAX - BTN_MISC];
	__u8 absmap[ABS_MAX];
	__u8 abspam[ABS_MAX];
};

struct joydev_list {
	struct js_event buffer[JOYDEV_BUFFER_SIZE];
	int head;
	int tail;
	int startup;
	struct fasync_struct *fasync;
	struct joydev *joydev;
	struct joydev_list *next;
};

static unsigned long joydev_minors = 0;
static struct joydev *joydev_base[BITS_PER_LONG];

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_SUPPORTED_DEVICE("js");

static int joydev_correct(int value, struct js_corr *corr)
{
	switch (corr->type) {
		case JS_CORR_NONE:
			break;
		case JS_CORR_BROKEN:
			value = value > corr->coef[0] ? (value < corr->coef[1] ? 0 :
				((corr->coef[3] * (value - corr->coef[1])) >> 14)) :
				((corr->coef[2] * (value - corr->coef[0])) >> 14);
			break;
		default:
			return 0;
	}

	if (value < -32767) return -32767;
	if (value >  32767) return  32767;

	return value;
}

static void joydev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct joydev *joydev = handle->private;
	struct joydev_list *list = joydev->list;
	struct js_event event;

	switch (type) {

		case EV_KEY:
			if (code < BTN_MISC || value == 2) return;
			event.type = JS_EVENT_BUTTON;
			event.number = joydev->keymap[code - BTN_MISC];
			event.value = value;
			break;

		case EV_ABS:
			event.type = JS_EVENT_AXIS;
			event.number = joydev->absmap[code];
			event.value = joydev_correct(value, &joydev->corr[event.number]);
			break;

		default:
			return;
	}  

	event.time = jiffies * (1000 / HZ);

	while (list) {

		memcpy(list->buffer + list->head, &event, sizeof(struct js_event));

		if (list->startup == joydev->nabs + joydev->nkey)
			if (list->tail == (list->head = (list->head + 1) & (JOYDEV_BUFFER_SIZE - 1)))
				list->startup = 0;

		if (list->fasync)
			kill_fasync(list->fasync, SIGIO, POLL_IN);

		list = list->next;
	}

	wake_up_interruptible(&joydev->wait);
}

static int joydev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct joydev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static int joydev_release(struct inode * inode, struct file * file)
{
	struct joydev_list *list = file->private_data;
	struct joydev_list **listptr = &list->joydev->list;

	joydev_fasync(-1, file, 0);

	while (*listptr && (*listptr != list))
		listptr = &((*listptr)->next);
	*listptr = (*listptr)->next;
	
	if (!--list->joydev->used) {
		clear_bit(list->joydev->minor, &joydev_minors);
		kfree(list->joydev);
	}

	kfree(list);

	MOD_DEC_USE_COUNT;
	return 0;
}

static int joydev_open(struct inode *inode, struct file *file)
{
	struct joydev_list *list;
	int i = MINOR(inode->i_rdev);

	if (MAJOR(inode->i_rdev) != JOYSTICK_MAJOR)
		return -EINVAL;

	if (i > BITS_PER_LONG || !test_bit(i, &joydev_minors))
		return -ENODEV;

	if (!(list = kmalloc(sizeof(struct joydev_list), GFP_KERNEL)))
		return -ENOMEM;

	memset(list, 0, sizeof(struct joydev_list));

	list->joydev = joydev_base[i];
	list->next = joydev_base[i]->list;
	joydev_base[i]->list = list;	

	file->private_data = list;

	list->joydev->used++;

	MOD_INC_USE_COUNT;
	return 0;
}

static ssize_t joydev_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t joydev_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct joydev_list *list = file->private_data;
	struct joydev *joydev = list->joydev;
	struct input_dev *input = joydev->handle.dev;
	int retval = 0;

	if (count < sizeof(struct js_event))
		return -EINVAL;

	if (count == sizeof(struct JS_DATA_TYPE)) {

		struct JS_DATA_TYPE data;

		data.buttons =  (joydev->nkey > 0 && test_bit(joydev->keypam[0], input->key)) ? 1 : 0 |
				(joydev->nkey > 1 && test_bit(joydev->keypam[1], input->key)) ? 2 : 0;
		data.x = ((joydev_correct(input->abs[ABS_X], &joydev->corr[0]) / 256) + 128) >> joydev->glue.JS_CORR.x;
		data.y = ((joydev_correct(input->abs[ABS_Y], &joydev->corr[1]) / 256) + 128) >> joydev->glue.JS_CORR.y;

		if (copy_to_user(buf, &data, sizeof(struct JS_DATA_TYPE)))
			return -EFAULT;

		list->startup = 0;
		list->tail = list->head;

		return sizeof(struct JS_DATA_TYPE);
	}

	if (list->head == list->tail && list->startup == joydev->nabs + joydev->nkey) {

		add_wait_queue(&list->joydev->wait, &wait);
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
		remove_wait_queue(&list->joydev->wait, &wait);
	}

	if (retval)
		return retval;

	while (list->startup < joydev->nabs + joydev->nkey && retval + sizeof(struct js_event) <= count) {

		struct js_event event;

		event.time = jiffies * (1000/HZ);

		if (list->startup < joydev->nkey) {
			event.type = JS_EVENT_BUTTON | JS_EVENT_INIT;
			event.value = !!test_bit(joydev->keypam[list->startup], input->key);
			event.number = list->startup;
		} else {
			event.type = JS_EVENT_AXIS | JS_EVENT_INIT;
			event.value = joydev_correct(input->abs[joydev->abspam[list->startup - joydev->nkey]],
							&joydev->corr[list->startup - joydev->nkey]);
			event.number = list->startup - joydev->nkey;
		}

		if (copy_to_user(buf + retval, &event, sizeof(struct js_event)))
			return -EFAULT;

		list->startup++;
		retval += sizeof(struct js_event);
	}

	while (list->head != list->tail && retval + sizeof(struct js_event) <= count) {

		if (copy_to_user(buf + retval, list->buffer + list->tail, sizeof(struct js_event)))
			return -EFAULT;

		list->tail = (list->tail + 1) & (JOYDEV_BUFFER_SIZE - 1);
		retval += sizeof(struct js_event);
	}

	return retval;
}

static unsigned int joydev_poll(struct file *file, poll_table *wait)
{
	struct joydev_list *list = file->private_data;
	poll_wait(file, &list->joydev->wait, wait);
	if (list->head != list->tail || list->startup < list->joydev->nabs + list->joydev->nkey)
		return POLLIN | POLLRDNORM;
	return 0;
}

static int joydev_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct joydev_list *list = file->private_data;
	struct joydev *joydev = list->joydev;

	switch (cmd) {

		case JS_SET_CAL:
			return copy_from_user(&joydev->glue.JS_CORR, (struct JS_DATA_TYPE *) arg,
				sizeof(struct JS_DATA_TYPE)) ? -EFAULT : 0;
		case JS_GET_CAL:
			return copy_to_user((struct JS_DATA_TYPE *) arg, &joydev->glue.JS_CORR,
				sizeof(struct JS_DATA_TYPE)) ? -EFAULT : 0;
		case JS_SET_TIMEOUT:
			return get_user(joydev->glue.JS_TIMEOUT, (int *) arg);
		case JS_GET_TIMEOUT:
			return put_user(joydev->glue.JS_TIMEOUT, (int *) arg);
		case JS_SET_TIMELIMIT:
			return get_user(joydev->glue.JS_TIMELIMIT, (long *) arg);
		case JS_GET_TIMELIMIT:
			return put_user(joydev->glue.JS_TIMELIMIT, (long *) arg);
		case JS_SET_ALL:
			return copy_from_user(&joydev->glue, (struct JS_DATA_SAVE_TYPE *) arg,
						sizeof(struct JS_DATA_SAVE_TYPE)) ? -EFAULT : 0;
		case JS_GET_ALL:
			return copy_to_user((struct JS_DATA_SAVE_TYPE *) arg, &joydev->glue,
						sizeof(struct JS_DATA_SAVE_TYPE)) ? -EFAULT : 0;

		case JSIOCGVERSION:
			return put_user(JS_VERSION, (__u32 *) arg);
		case JSIOCGAXES:
			return put_user(joydev->nabs, (__u8 *) arg);
		case JSIOCGBUTTONS:
			return put_user(joydev->nkey, (__u8 *) arg);
		case JSIOCSCORR:
			return copy_from_user(joydev->corr, (struct js_corr *) arg,
						sizeof(struct js_corr) * joydev->nabs) ? -EFAULT : 0;
		case JSIOCGCORR:
			return copy_to_user((struct js_corr *) arg, joydev->corr,
						sizeof(struct js_corr) * joydev->nabs) ? -EFAULT : 0;
		default:
			if ((cmd & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT)) == JSIOCGNAME(0)) {
				int len = strlen(joydev->name) + 1;
				if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
				if (copy_to_user((char *) arg, joydev->name, len)) return -EFAULT;
				return len;
			}
	}
	return -EINVAL;
}

static struct file_operations joydev_fops = {
	read:		joydev_read,
	write:		joydev_write,
	poll:		joydev_poll,
	open:		joydev_open,
	release:	joydev_release,
	ioctl:		joydev_ioctl,
	fasync:		joydev_fasync,
};

static int joydev_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct joydev *joydev;
	int i, j;

	if (!(test_bit(EV_KEY, dev->evbit) && test_bit(EV_ABS, dev->evbit) &&
	      test_bit(ABS_X, dev->absbit) && test_bit(ABS_Y, dev->absbit) &&
	     (test_bit(BTN_TRIGGER, dev->keybit) || test_bit(BTN_A, dev->keybit)
		|| test_bit(BTN_1, dev->keybit)))) return -1;

	if (!(joydev = kmalloc(sizeof(struct joydev), GFP_KERNEL)))
		return -1;

	memset(joydev, 0, sizeof(struct joydev));

	init_waitqueue_head(&joydev->wait);

	if (joydev_minors == -1) {
		printk("Can't register new joystick - 32 devices already taken.\n");
		return -1;
	}

	sprintf(joydev->name, "joydev%d", joydev->minor);

	joydev->handle.dev = dev;
	joydev->handle.handler = handler;
	joydev->handle.private = joydev;

	joydev->used = 1;

	for (i = 0; i < ABS_MAX; i++)
		if (test_bit(i, dev->absbit)) {
			joydev->absmap[i] = joydev->nabs;
			joydev->abspam[joydev->nabs] = i;
			joydev->nabs++;
		}

	for (i = BTN_JOYSTICK - BTN_MISC; i < KEY_MAX - BTN_MISC; i++)
		if (test_bit(i + BTN_MISC, dev->keybit)) {
			joydev->keymap[i] = joydev->nkey;
			joydev->keypam[joydev->nkey] = i + BTN_MISC;
			joydev->nkey++;
		}

	for (i = 0; i < BTN_JOYSTICK - BTN_MISC; i++)
		if (test_bit(i + BTN_MISC, dev->keybit)) {
			joydev->keymap[i] = joydev->nkey;
			joydev->keypam[joydev->nkey] = i + BTN_MISC;
			joydev->nkey++;
		}

	joydev->minor = ffz(joydev_minors);
	set_bit(joydev->minor, &joydev_minors);
	joydev_base[joydev->minor] = joydev;

	for (i = 0; i < joydev->nabs; i++) {
		j = joydev->abspam[i];
		if (dev->absmax[j] == dev->absmin[j]) {
			joydev->corr[i].type = JS_CORR_NONE;
			continue;
		}
		joydev->corr[i].type = JS_CORR_BROKEN;
		joydev->corr[i].prec = dev->absfuzz[j];
		joydev->corr[i].coef[0] = (dev->absmax[j] + dev->absmin[j]) / 2 - dev->absflat[j];
		joydev->corr[i].coef[1] = (dev->absmax[j] + dev->absmin[j]) / 2 + dev->absflat[j];
		joydev->corr[i].coef[2] = (1 << 29) / ((dev->absmax[j] - dev->absmin[j]) / 2 - 2 * dev->absflat[j]);
		joydev->corr[i].coef[3] = (1 << 29) / ((dev->absmax[j] - dev->absmin[j]) / 2 - 2 * dev->absflat[j]);
	}

	input_open_device(&joydev->handle);	

	printk("%s: Joystick device for input%d on /dev/js%d\n", joydev->name, dev->number, joydev->minor);

	return 0;
}

static void joydev_disconnect(struct input_handle *handle)
{
	struct joydev *joydev = handle->private;

	input_close_device(handle);

	if (!--joydev->used) {
		clear_bit(joydev->minor, &joydev_minors);
		kfree(joydev);
	}
}

static struct input_handler joydev_handler = {
	event:		joydev_event,
	connect:	joydev_connect,
	disconnect:	joydev_disconnect,
};

static int __init joydev_init(void)
{
	if (register_chrdev(JOYDEV_MAJOR, "js", &joydev_fops)) {
		printk(KERN_ERR "joydev: unable to get major %d for joystick\n", JOYDEV_MAJOR);
		return -EBUSY;
	}
	input_register_handler(&joydev_handler);
	return 0;
}

static void __exit joydev_exit(void)
{
	input_unregister_handler(&joydev_handler);
	if (unregister_chrdev(JOYSTICK_MAJOR, "js"))
		printk(KERN_ERR "js: can't unregister device\n");
}

module_init(joydev_init);
module_exit(joydev_exit);
