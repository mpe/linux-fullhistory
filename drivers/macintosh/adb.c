/*
 * Device driver for the Apple Desktop Bus
 * and the /dev/adb device on macintoshes.
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
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/uaccess.h>
#include <asm/hydra.h>

enum adb_hw adb_hardware;
int (*adb_send_request)(struct adb_request *req, int sync);
int (*adb_autopoll)(int on);

static struct adb_handler {
	void (*handler)(unsigned char *, int, struct pt_regs *, int);
} adb_handler[16];

static int adb_nodev(void)
{
	return -1;
}

void adb_init(void)
{
	adb_hardware = ADB_NONE;
	adb_send_request = (void *) adb_nodev;
	adb_autopoll = (void *) adb_nodev;
	via_cuda_init();
	macio_adb_init();
	if (adb_hardware == ADB_NONE)
		printk(KERN_WARNING "Warning: no ADB interface detected\n");
	else
		adb_autopoll(1);
}

int
adb_request(struct adb_request *req, void (*done)(struct adb_request *),
	    int flags, int nbytes, ...)
{
	va_list list;
	int i;
	struct adb_request sreq;

	if (req == NULL) {
		req = &sreq;
		flags |= ADBREQ_SYNC;
	}
	req->nbytes = nbytes;
	req->done = done;
	req->reply_expected = flags & ADBREQ_REPLY;
	va_start(list, nbytes);
	for (i = 0; i < nbytes; ++i)
		req->data[i] = va_arg(list, int);
	va_end(list);
	return adb_send_request(req, flags & ADBREQ_SYNC);
}

/* Ultimately this should return the number of devices with
   the given default id. */
int
adb_register(int default_id,
	     void (*handler)(unsigned char *, int, struct pt_regs *, int))
{
	if (adb_handler[default_id].handler != 0)
		panic("Two handlers for ADB device %d\n", default_id);
	adb_handler[default_id].handler = handler;
	return 1;
}

void
adb_input(unsigned char *buf, int nb, struct pt_regs *regs, int autopoll)
{
	int i, id;
	static int dump_adb_input = 0;

	id = buf[0] >> 4;
	if (dump_adb_input) {
		printk(KERN_INFO "adb packet: ");
		for (i = 0; i < nb; ++i)
			printk(" %x", buf[i]);
		printk(", id = %d\n", id);
	}
	if (adb_handler[id].handler != 0) {
		(*adb_handler[id].handler)(buf, nb, regs, autopoll);
	}
}

/*
 * /dev/adb device driver.
 */

#define ADB_MAJOR	56	/* major number for /dev/adb */

extern void adbdev_init(void);

struct adbdev_state {
	struct adb_request req;
};

static struct wait_queue *adb_wait;

static int adb_wait_reply(struct adbdev_state *state, struct file *file)
{
	int ret = 0;
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&adb_wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (!state->req.complete) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&adb_wait, &wait);

	return ret;
}

static void adb_write_done(struct adb_request *req)
{
	if (!req->complete) {
		req->reply_len = 0;
		req->complete = 1;
	}
	wake_up_interruptible(&adb_wait);
}

static int adb_open(struct inode *inode, struct file *file)
{
	struct adbdev_state *state;

	if (MINOR(inode->i_rdev) > 0 || adb_hardware == ADB_NONE)
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
		if (state->req.reply_expected && !state->req.complete)
			if (adb_wait_reply(state, file))
				return 0;
		kfree(state);
	}
	return 0;
}

static long long adb_lseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t adb_read(struct file *file, char *buf,
			size_t count, loff_t *ppos)
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

	state->req.reply_expected = 0;
	ret = state->req.reply_len;
	if (copy_to_user(buf, state->req.reply, ret))
		return -EFAULT;

	return ret;
}

static ssize_t adb_write(struct file *file, const char *buf,
			 size_t count, loff_t *ppos)
{
	int ret, i;
	struct adbdev_state *state = file->private_data;

	if (count < 2 || count > sizeof(state->req.data))
		return -EINVAL;
	ret = verify_area(VERIFY_READ, buf, count);
	if (ret)
		return ret;

	if (state->req.reply_expected && !state->req.complete) {
		/* A previous request is still being processed.
		   Wait for it to finish. */
		ret = adb_wait_reply(state, file);
		if (ret)
			return ret;
	}

	state->req.nbytes = count;
	state->req.done = adb_write_done;
	state->req.complete = 0;
	if (copy_from_user(state->req.data, buf, count))
		return -EFAULT;

	switch (adb_hardware) {
	case ADB_NONE:
		return -ENXIO;
	case ADB_VIACUDA:
		state->req.reply_expected = 1;
		cuda_send_request(&state->req);
		break;
	default:
		if (state->req.data[0] != ADB_PACKET)
			return -EINVAL;
		for (i = 1; i < state->req.nbytes; ++i)
			state->req.data[i] = state->req.data[i+1];
		state->req.reply_expected =
			((state->req.data[0] & 0xc) == 0xc);
		adb_send_request(&state->req, 0);
		break;
	}

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
