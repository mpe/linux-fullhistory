/*
 * Device driver for the /dev/adb device on macintoshes.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/prom.h>
#include <asm/cuda.h>
#include <asm/uaccess.h>

#define ADB_MAJOR	56	/* major number for /dev/adb */

extern void adbdev_init(void);

struct adbdev_state {
	struct cuda_request req;
};

static struct wait_queue *adb_wait;

static int adb_wait_reply(struct adbdev_state *state, struct file *file)
{
	int ret = 0;
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&adb_wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (!state->req.got_reply) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (current->signal & ~current->blocked) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&adb_wait, &wait);

	return ret;
}

static void adb_write_done(struct cuda_request *req)
{
	if (!req->got_reply) {
		req->reply_len = 0;
		req->got_reply = 1;
	}
	wake_up_interruptible(&adb_wait);
}

static int adb_open(struct inode *inode, struct file *file)
{
	struct adbdev_state *state;

	if (MINOR(inode->i_rdev) > 0)
		return -ENXIO;
	state = kmalloc(sizeof(struct adbdev_state), GFP_KERNEL);
	if (state == 0)
		return -ENOMEM;
	file->private_data = state;
	state->req.reply_expected = 0;
	return 0;
}

static int adb_release(struct inode *inode, struct file *file)
{
	struct adbdev_state *state = file->private_data;

	if (state) {
		file->private_data = NULL;
		if (state->req.reply_expected && !state->req.got_reply)
			if (adb_wait_reply(state, file))
				return 0;
		kfree(state);
	}
	return 0;
}

static long long adb_lseek(struct inode *inode, struct file *file,
			   long long offset, int origin)
{
	return -ESPIPE;
}

static long adb_read(struct inode *inode, struct file *file,
		     char *buf, unsigned long count)
{
	int ret;
	struct adbdev_state *state = file->private_data;

	if (count < 2)
		return -EINVAL;
	if (count > sizeof(state->req.reply))
		count = sizeof(state->req.reply);
	ret = verify_area(VERIFY_WRITE, buf, count);
	if (ret)
		return ret;

	if (!state->req.reply_expected)
		return 0;

	ret = adb_wait_reply(state, file);
	if (ret)
		return ret;

	ret = state->req.reply_len;
	copy_to_user(buf, state->req.reply, ret);
	state->req.reply_expected = 0;

	return ret;
}

static long adb_write(struct inode *inode, struct file *file,
		      const char *buf, unsigned long count)
{
	int ret;
	struct adbdev_state *state = file->private_data;

	if (count < 2 || count > sizeof(state->req.data))
		return -EINVAL;
	ret = verify_area(VERIFY_READ, buf, count);
	if (ret)
		return ret;

	if (state->req.reply_expected && !state->req.got_reply) {
		/* A previous request is still being processed.
		   Wait for it to finish. */
		ret = adb_wait_reply(state, file);
		if (ret)
			return ret;
	}

	state->req.nbytes = count;
	state->req.done = adb_write_done;
	copy_from_user(state->req.data, buf, count);
	state->req.reply_expected = 1;
	state->req.got_reply = 0;
	cuda_send_request(&state->req);

	return count;
}

static struct file_operations adb_fops = {
	adb_lseek,
	adb_read,
	adb_write,
	NULL,		/* no readdir */
	NULL,		/* no poll yet */
	NULL,		/* no ioctl yet */
	NULL,		/* no mmap */
	adb_open,
	adb_release
};

void adbdev_init()
{
	if (register_chrdev(ADB_MAJOR, "adb", &adb_fops))
		printk(KERN_ERR "adb: unable to get major %d\n", ADB_MAJOR);
}
