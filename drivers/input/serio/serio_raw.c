/*
 * Raw serio device providing access to a raw byte stream from underlying
 * serio port. Closely emulates behavior of pre-2.6 /dev/psaux device
 *
 * Copyright (c) 2004 Dmitry Torokhov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>

#define DRIVER_DESC	"Raw serio driver"

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define SERIO_RAW_QUEUE_LEN	64
struct serio_raw {
	unsigned char queue[SERIO_RAW_QUEUE_LEN];
	unsigned int tail, head;

	char name[16];
	unsigned int refcnt;
	struct serio *serio;
	struct miscdevice dev;
	wait_queue_head_t wait;
	struct list_head list;
	struct list_head node;
};

struct serio_raw_list {
	struct fasync_struct *fasync;
	struct serio_raw *serio_raw;
	struct list_head node;
};

static DECLARE_MUTEX(serio_raw_sem);
static LIST_HEAD(serio_raw_list);
static unsigned int serio_raw_no;

/*********************************************************************
 *             Interface with userspace (file operations)            *
 *********************************************************************/

static int serio_raw_fasync(int fd, struct file *file, int on)
{
	struct serio_raw_list *list = file->private_data;
	int retval;

	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static struct serio_raw *serio_raw_locate(int minor)
{
	struct serio_raw *serio_raw;

	list_for_each_entry(serio_raw, &serio_raw_list, node) {
		if (serio_raw->dev.minor == minor)
			return serio_raw;
	}

	return NULL;
}

static int serio_raw_open(struct inode *inode, struct file *file)
{
	struct serio_raw *serio_raw;
	struct serio_raw_list *list;
	int retval = 0;

	retval = down_interruptible(&serio_raw_sem);
	if (retval)
		return retval;

	if (!(serio_raw = serio_raw_locate(iminor(inode)))) {
		retval = -ENODEV;
		goto out;
	}

	if (!serio_raw->serio) {
		retval = -ENODEV;
		goto out;
	}

	if (!(list = kmalloc(sizeof(struct serio_raw_list), GFP_KERNEL))) {
		retval = -ENOMEM;
		goto out;
	}

	memset(list, 0, sizeof(struct serio_raw_list));
	list->serio_raw = serio_raw;
	file->private_data = list;

	serio_raw->refcnt++;
	list_add_tail(&list->node, &serio_raw->list);

out:
	up(&serio_raw_sem);
	return retval;
}

static int serio_raw_cleanup(struct serio_raw *serio_raw)
{
	if (--serio_raw->refcnt == 0) {
		misc_deregister(&serio_raw->dev);
		list_del_init(&serio_raw->node);
		kfree(serio_raw);

		return 1;
	}

	return 0;
}

static int serio_raw_release(struct inode *inode, struct file *file)
{
	struct serio_raw_list *list = file->private_data;
	struct serio_raw *serio_raw = list->serio_raw;

	down(&serio_raw_sem);

	serio_raw_fasync(-1, file, 0);
	serio_raw_cleanup(serio_raw);

	up(&serio_raw_sem);
	return 0;
}

static int serio_raw_fetch_byte(struct serio_raw *serio_raw, char *c)
{
	unsigned long flags;
	int empty;

	spin_lock_irqsave(&serio_raw->serio->lock, flags);

	empty = serio_raw->head == serio_raw->tail;
	if (!empty) {
		*c = serio_raw->queue[serio_raw->tail];
		serio_raw->tail = (serio_raw->tail + 1) % SERIO_RAW_QUEUE_LEN;
	}

	spin_unlock_irqrestore(&serio_raw->serio->lock, flags);

	return !empty;
}

static ssize_t serio_raw_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct serio_raw_list *list = file->private_data;
	struct serio_raw *serio_raw = list->serio_raw;
	char c;
	ssize_t retval = 0;

	if (!serio_raw->serio)
		return -ENODEV;

	if (serio_raw->head == serio_raw->tail && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(list->serio_raw->wait,
					  serio_raw->head != serio_raw->tail || !serio_raw->serio);
	if (retval)
		return retval;

	if (!serio_raw->serio)
		return -ENODEV;

	while (retval < count && serio_raw_fetch_byte(serio_raw, &c)) {
		if (put_user(c, buffer++))
			return -EFAULT;
		retval++;
	}

	return retval;
}

static ssize_t serio_raw_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct serio_raw_list *list = file->private_data;
	ssize_t written = 0;
	int retval;
	unsigned char c;

	retval = down_interruptible(&serio_raw_sem);
	if (retval)
		return retval;

	if (!list->serio_raw->serio) {
		retval = -ENODEV;
		goto out;
	}

	if (count > 32)
		count = 32;

	while (count--) {
		if (get_user(c, buffer++)) {
			retval = -EFAULT;
			goto out;
		}
		if (serio_write(list->serio_raw->serio, c)) {
			retval = -EIO;
			goto out;
		}
		written++;
	};

out:
	up(&serio_raw_sem);
	return written;
}

static unsigned int serio_raw_poll(struct file *file, poll_table *wait)
{
	struct serio_raw_list *list = file->private_data;

	poll_wait(file, &list->serio_raw->wait, wait);

	if (list->serio_raw->head != list->serio_raw->tail)
		return POLLIN | POLLRDNORM;

	return 0;
}

struct file_operations serio_raw_fops = {
	.owner =	THIS_MODULE,
	.open =		serio_raw_open,
	.release =	serio_raw_release,
	.read =		serio_raw_read,
	.write =	serio_raw_write,
	.poll =		serio_raw_poll,
	.fasync =	serio_raw_fasync,
};


/*********************************************************************
 *                   Interface with serio port   	             *
 *********************************************************************/

static irqreturn_t serio_raw_interrupt(struct serio *serio, unsigned char data,
					unsigned int dfl, struct pt_regs *regs)
{
	struct serio_raw *serio_raw = serio->private;
	struct serio_raw_list *list;
	unsigned int head = serio_raw->head;

	/* we are holding serio->lock here so we are prootected */
	serio_raw->queue[head] = data;
	head = (head + 1) % SERIO_RAW_QUEUE_LEN;
	if (likely(head != serio_raw->tail)) {
		serio_raw->head = head;
		list_for_each_entry(list, &serio_raw->list, node)
			kill_fasync(&list->fasync, SIGIO, POLL_IN);
		wake_up_interruptible(&serio_raw->wait);
	}

	return IRQ_HANDLED;
}

static void serio_raw_connect(struct serio *serio, struct serio_driver *drv)
{
	struct serio_raw *serio_raw;
	int err;

	if ((serio->type & SERIO_TYPE) != SERIO_8042)
		return;

	if (!(serio_raw = kmalloc(sizeof(struct serio_raw), GFP_KERNEL))) {
		printk(KERN_ERR "serio_raw.c: can't allocate memory for a device\n");
		return;
	}

	down(&serio_raw_sem);

	memset(serio_raw, 0, sizeof(struct serio_raw));
	snprintf(serio_raw->name, sizeof(serio_raw->name), "serio_raw%d", serio_raw_no++);
	serio_raw->refcnt = 1;
	serio_raw->serio = serio;
	INIT_LIST_HEAD(&serio_raw->list);
	init_waitqueue_head(&serio_raw->wait);

	serio->private = serio_raw;
	if (serio_open(serio, drv))
		goto out_free;

	list_add_tail(&serio_raw->node, &serio_raw_list);

	serio_raw->dev.minor = PSMOUSE_MINOR;
	serio_raw->dev.name = serio_raw->name;
	serio_raw->dev.fops = &serio_raw_fops;

	err = misc_register(&serio_raw->dev);
	if (err) {
		serio_raw->dev.minor = MISC_DYNAMIC_MINOR;
		err = misc_register(&serio_raw->dev);
	}

	if (err) {
		printk(KERN_INFO "serio_raw: failed to register raw access device for %s\n",
			serio->phys);
		goto out_close;
	}

	printk(KERN_INFO "serio_raw: raw access enabled on %s (%s, minor %d)\n",
		serio->phys, serio_raw->name, serio_raw->dev.minor);
	goto out;

out_close:
	serio_close(serio);
	list_del_init(&serio_raw->node);
out_free:
	serio->private = NULL;
	kfree(serio_raw);
out:
	up(&serio_raw_sem);
}

static int serio_raw_reconnect(struct serio *serio)
{
	struct serio_raw *serio_raw = serio->private;
	struct serio_driver *drv = serio->drv;

	if (!drv || !serio_raw) {
		printk(KERN_DEBUG "serio_raw: reconnect request, but serio is disconnected, ignoring...\n");
		return -1;
	}

	/*
	 * Nothing needs to be done here, we just need this method to
	 * keep the same device.
	 */
	return 0;
}

static void serio_raw_disconnect(struct serio *serio)
{
	struct serio_raw *serio_raw;

	down(&serio_raw_sem);

	serio_raw = serio->private;

	serio_close(serio);
	serio->private = NULL;

	serio_raw->serio = NULL;
	if (!serio_raw_cleanup(serio_raw))
		wake_up_interruptible(&serio_raw->wait);

	up(&serio_raw_sem);
}

static struct serio_driver serio_raw_drv = {
	.driver		= {
		.name	= "serio_raw",
	},
	.description	= DRIVER_DESC,
	.interrupt	= serio_raw_interrupt,
	.connect	= serio_raw_connect,
	.reconnect	= serio_raw_reconnect,
	.disconnect	= serio_raw_disconnect,
	.manual_bind	= 1,
};

int __init serio_raw_init(void)
{
	serio_register_driver(&serio_raw_drv);
	return 0;
}

void __exit serio_raw_exit(void)
{
	serio_unregister_driver(&serio_raw_drv);
}

module_init(serio_raw_init);
module_exit(serio_raw_exit);
