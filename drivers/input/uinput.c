/*
 *  User level driver support for input subsystem
 *
 * Heavily based on evdev.c by Vojtech Pavlik
 *
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
 * Author: Aristeu Sergio Rozanski Filho <aris@cathedrallabs.org>
 * 
 * Changes/Revisions:
 *	0.1	20/06/2002
 *		- first public version
 */
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uinput.h>

static int uinput_dev_open(struct input_dev *dev)
{
	return 0;
}

static void uinput_dev_close(struct input_dev *dev)
{

}

static int uinput_dev_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct uinput_device	*udev;

	udev = (struct uinput_device *)dev->private;

	udev->head = (udev->head + 1) & 0xF;
	udev->buff[udev->head].type = type;
	udev->buff[udev->head].code = code;
	udev->buff[udev->head].value = value;
	do_gettimeofday(&udev->buff[udev->head].time);

	wake_up_interruptible(&udev->waitq);

	return 0;
}

static int uinput_dev_upload_effect(struct input_dev *dev, struct ff_effect *effect)
{
	return 0;
}

static int uinput_dev_erase_effect(struct input_dev *dev, int effect_id)
{
	return 0;
}					

static int uinput_create_device(struct uinput_device *udev)
{
	if (!udev->dev->name) {
		printk(KERN_DEBUG "%s: write device info first\n", UINPUT_NAME);
		return -EINVAL;
	}

	udev->dev->open = uinput_dev_open;
	udev->dev->close = uinput_dev_close;
	udev->dev->event = uinput_dev_event;
	udev->dev->upload_effect = uinput_dev_upload_effect;
	udev->dev->erase_effect = uinput_dev_erase_effect;

	init_waitqueue_head(&(udev->waitq));

	input_register_device(udev->dev);

	udev->state |= UIST_CREATED;

	return 0;
}

static int uinput_destroy_device(struct uinput_device *udev)
{
	if (!(udev->state & UIST_CREATED)) {
		printk(KERN_WARNING "%s: create the device first\n", UINPUT_NAME);
		return -EINVAL;
	}

	input_unregister_device(udev->dev);

	clear_bit(UIST_CREATED, &(udev->state));

	return 0;
}

static int uinput_open(struct inode *inode, struct file *file)
{
	struct uinput_device	*newdev;
	struct input_dev	*newinput;

	MOD_INC_USE_COUNT;

	newdev = kmalloc(sizeof(struct uinput_device), GFP_KERNEL);
	if (!newdev)
		goto error;
	memset(newdev, 0, sizeof(struct uinput_device));

	newinput = kmalloc(sizeof(struct input_dev), GFP_KERNEL);
	if (!newinput)
		goto cleanup;
	memset(newinput, 0, sizeof(struct input_dev));

	newdev->dev = newinput;
	
	file->private_data = newdev;

	return 0;
cleanup:
	kfree(newdev);
error:
	MOD_DEC_USE_COUNT;
	return -ENOMEM;
}

static int uinput_alloc_device(struct file *file, const char *buffer, size_t count)
{
	struct uinput_user_dev	user_dev;
	struct input_dev	*dev;
	struct uinput_device	*udev;
	int			size,
				retval;

	retval = count;

	if (copy_from_user(&user_dev, buffer, sizeof(struct uinput_user_dev))) {
		retval = -EFAULT;
		goto exit;
	}

	udev = (struct uinput_device *)file->private_data;
	dev = udev->dev;

	size = strnlen(user_dev.name, UINPUT_MAX_NAME_SIZE);
	dev->name = kmalloc(size + 1, GFP_KERNEL);
	if (!dev->name) {
		retval = -ENOMEM;
		goto exit;
	}

	strncpy(dev->name, user_dev.name, size);
	dev->name[size] = '\0';
	dev->id.bustype	= user_dev.id.bustype;
	dev->id.vendor	= user_dev.id.vendor;
	dev->id.product	= user_dev.id.product;
	dev->id.version	= user_dev.id.version;
	dev->ff_effects_max = user_dev.ff_effects_max;

	size = sizeof(unsigned long) * NBITS(ABS_MAX + 1);
	memcpy(dev->absmax, user_dev.absmax, size);
	memcpy(dev->absmin, user_dev.absmin, size);
	memcpy(dev->absfuzz, user_dev.absfuzz, size);
	memcpy(dev->absflat, user_dev.absflat, size);

	/* check if absmin/absmax/absfuzz/absflat are filled as
	 * told in Documentation/input/input-programming.txt */
	if (test_bit(EV_ABS, dev->evbit)) {
		unsigned int cnt;
		for (cnt = 1; cnt < ABS_MAX; cnt++)
			if (test_bit(cnt, dev->absbit) &&
					(!dev->absmin[cnt] ||
					 !dev->absmax[cnt] ||
					 !dev->absfuzz[cnt] ||
					 !dev->absflat[cnt])) {
				printk(KERN_DEBUG "%s: set abs fields "
					"first\n", UINPUT_NAME);
				retval = -EINVAL;
				goto free_name;
			}
	}

exit:
	return retval;
free_name:
	kfree(dev->name);
	goto exit;
}

static int uinput_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct uinput_device	*udev = file->private_data;
	

	if (udev->state & UIST_CREATED) {
		struct input_event	ev;

		if (copy_from_user(&ev, buffer, sizeof(struct input_event)))
			return -EFAULT;
		input_event(udev->dev, ev.type, ev.code, ev.value);
	}
	else
		count = uinput_alloc_device(file, buffer, count);

	return count;
}

static ssize_t uinput_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct uinput_device	*udev;
	int retval = 0;
	DECLARE_WAITQUEUE(waitq, current);

	udev = (struct uinput_device *)file->private_data;

	if (udev->head == udev->tail) {
		add_wait_queue(&udev->waitq, &waitq);
		current->state = TASK_INTERRUPTIBLE;

		while (udev->head == udev->tail) {
			if (!(udev->state & UIST_CREATED)) {
				retval = -ENODEV;
				break;
			}
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
		remove_wait_queue(&udev->waitq, &waitq);
	}

	if (retval)
		return retval;

	while (udev->head != udev->tail && retval + sizeof(struct uinput_device) <= count) {
		if (copy_to_user(buffer + retval, &(udev->buff[udev->tail]),
		    sizeof(struct input_event)))
			return -EFAULT;
		udev->tail = (udev->tail + 1)%(UINPUT_BUFFER_SIZE - 1);
		retval += sizeof(struct input_event);
	}

	return retval;
}

static unsigned int uinput_poll(struct file *file, poll_table *wait)
{
        struct uinput_device *udev = file->private_data;

	poll_wait(file, &udev->waitq, wait);

	if (udev->head != udev->tail)
		return POLLIN | POLLRDNORM;

	return 0;			
}

static int uinput_burn_device(struct uinput_device *udev)
{
	if (udev->state & UIST_CREATED)
		uinput_destroy_device(udev);

	kfree(udev->dev);
	kfree(udev);

	return 0;
}

static int uinput_close(struct inode *inode, struct file *file)
{
	int	retval;

	retval = uinput_burn_device((struct uinput_device *)file->private_data);
	MOD_DEC_USE_COUNT;
	return retval;
}

static int uinput_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int			retval = 0;
	struct uinput_device	*udev;

	udev = (struct uinput_device *)file->private_data;

	if (cmd >= UI_SET_EVBIT && (udev->state & UIST_CREATED))
		return -EINVAL;

	switch (cmd) {
		case UI_DEV_CREATE:
			retval = uinput_create_device(udev);

			break;
		case UI_DEV_DESTROY:
			retval = uinput_destroy_device(udev);

			break;
		case UI_SET_EVBIT:
			set_bit(arg, udev->dev->evbit);

		break;
		case UI_SET_KEYBIT:
			set_bit(arg, udev->dev->keybit);

		break;
		case UI_SET_RELBIT:
			set_bit(arg, udev->dev->relbit);

		break;
		case UI_SET_ABSBIT:
			set_bit(arg, udev->dev->absbit);

		break;
		case UI_SET_MSCBIT:
			set_bit(arg, udev->dev->mscbit);

		break;
		case UI_SET_LEDBIT:
			set_bit(arg, udev->dev->ledbit);

		break;
		case UI_SET_SNDBIT:
			set_bit(arg, udev->dev->sndbit);

		break;
		case UI_SET_FFBIT:
			set_bit(arg, udev->dev->ffbit);

		break;
		default:
			retval = -EFAULT;
	}
	return retval;
}

struct file_operations uinput_fops = {
	.owner =	THIS_MODULE,
	.open =		uinput_open,
	.release =	uinput_close,
	.read =		uinput_read,
	.write =	uinput_write,
	.poll =		uinput_poll,
	.ioctl =	uinput_ioctl,
};

static struct miscdevice uinput_misc = {
	.fops =		&uinput_fops,
	.minor =	UINPUT_MINOR,
	.name =		UINPUT_NAME,
};

static int __init uinput_init(void)
{
	return misc_register(&uinput_misc);
}

static void __exit uinput_exit(void)
{
	misc_deregister(&uinput_misc);
}

MODULE_AUTHOR("Aristeu Sergio Rozanski Filho");
MODULE_DESCRIPTION("User level driver support for input subsystem");
MODULE_LICENSE("GPL");

module_init(uinput_init);
module_exit(uinput_exit);

