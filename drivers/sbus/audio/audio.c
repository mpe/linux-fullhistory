/*
 * drivers/sbus/audio/audio.c
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 *
 * This is the audio midlayer that sits between the VFS character
 * devices and the low-level audio hardware device drivers.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/tqueue.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include "audio.h"


/*
 *	Low-level driver interface.
 */

/* We only support one low-level audio driver currently. */
static struct sparcaudio_driver *driver = NULL;

int register_sparcaudio_driver(struct sparcaudio_driver *drv)
{
	int i;

	/* If a driver is already present, don't allow the register. */
	if (driver)
		return -EIO;

	/* Ensure that the driver has a proper operations structure. */
	if (!drv->ops || !drv->ops->start_output || !drv->ops->stop_output)
		return -EINVAL;

	/* Setup the circular queue of output buffers. */
	drv->num_output_buffers = 32;
	drv->output_front = 0;
	drv->output_rear = 0;
	drv->output_count = 0;
	drv->output_active = 0;
	drv->output_buffers = kmalloc(32 * sizeof(__u8 *), GFP_KERNEL);
	drv->output_sizes = kmalloc(32 * sizeof(size_t), GFP_KERNEL);
	if (!drv->output_buffers || !drv->output_sizes) {
		if (drv->output_buffers)
			kfree(drv->output_buffers);
		if (drv->output_sizes)
			kfree(drv->output_sizes);
		return -ENOMEM;
	}

	/* Allocate the pages for each output buffer. */
	for (i = 0; i < drv->num_output_buffers; i++) {
		drv->output_buffers[i] = (void *) __get_free_page(GFP_KERNEL);
		if (!drv->output_buffers[i]) {
			int j;
			for (j = 0; j < i; j++)
				free_page((unsigned long) drv->output_buffers[j]);
			kfree(drv->output_buffers);
			kfree(drv->output_sizes);
			return -ENOMEM;
		}
	}

	/* Ensure that the driver is marked as not being open. */
	drv->flags = 0;

	MOD_INC_USE_COUNT;

	driver = drv;
	return 0;
}

int unregister_sparcaudio_driver(struct sparcaudio_driver *drv)
{
	int i;

	/* Make sure that the current driver is unregistering. */
	if (driver != drv)
		return -EIO;

	/* Deallocate the queue of output buffers. */
	for (i = 0; i < driver->num_output_buffers; i++)
		free_page((unsigned long) driver->output_buffers[i]);
	kfree(driver->output_buffers);
	kfree(driver->output_sizes);

	MOD_DEC_USE_COUNT;

	driver = NULL;
	return 0;
}

static void sparcaudio_output_done_task(void * arg)
{
	struct sparcaudio_driver *drv = (struct sparcaudio_driver *)arg;
	unsigned long flags;

	save_and_cli(flags);
	drv->ops->start_output(drv,
			       drv->output_buffers[drv->output_front],
			       drv->output_sizes[drv->output_front]);
	drv->output_active = 1;
	restore_flags(flags);
}

void sparcaudio_output_done(struct sparcaudio_driver * drv)
{
	/* Point the queue after the "done" buffer. */
	drv->output_front = (drv->output_front + 1) % drv->num_output_buffers;
	drv->output_count--;

	/* If the output queue is empty, shutdown the driver. */
	if (drv->output_count == 0) {
		/* Stop the lowlevel driver from outputing. */
		drv->ops->stop_output(drv);
		drv->output_active = 0;

		/* Wake up any waiting writers or syncers and return. */
		wake_up_interruptible(&drv->output_write_wait);
		wake_up_interruptible(&drv->output_drain_wait);
		return;
	}

	/* Otherwise, queue a task to give the driver the next buffer. */
	drv->tqueue.next = NULL;
	drv->tqueue.sync = 0;
	drv->tqueue.routine = sparcaudio_output_done_task;
	drv->tqueue.data = drv;

	queue_task(&drv->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	/* Wake up any tasks that are waiting. */
	wake_up_interruptible(&drv->output_write_wait);
}

void sparcaudio_input_done(struct sparcaudio_driver * drv)
{
	/* XXX Implement! */
}



/*
 *	VFS layer interface
 */

static int sparcaudio_lseek(struct inode * inode, struct file * file,
			    off_t offset, int origin)
{
	return -ESPIPE;
}

static int sparcaudio_read(struct inode * inode, struct file * file,
			   char *buf, int count)
{
	/* XXX Implement me! */
	return -EINVAL;
}

static int sparcaudio_write(struct inode * inode, struct file * file,
			    const char *buf, int count)
{
	unsigned long flags;
	int bytes_written = 0, bytes_to_copy, err;

	/* Ensure that we have something to write. */
	if (count < 1)
		return 0;

	/* Loop until all output is written to device. */
	while (count > 0) {
		/* Check to make sure that an output buffer is available. */
		if (driver->output_count == driver->num_output_buffers) {
			interruptible_sleep_on(&driver->output_write_wait);
			if (current->signal & ~current->blocked)
				return bytes_written > 0 ? bytes_written : -EINTR;
		}

		/* Determine how much we can copy in this iteration. */
		bytes_to_copy = count;
		if (bytes_to_copy > PAGE_SIZE)
			bytes_to_copy = PAGE_SIZE;

		copy_from_user_ret(driver->output_buffers[driver->output_rear],
			       buf, bytes_to_copy, -EFAULT);

		/* Update the queue pointers. */
		buf += bytes_to_copy;
		count -= bytes_to_copy;
		bytes_written += bytes_to_copy;
		driver->output_sizes[driver->output_rear] = bytes_to_copy;
		driver->output_rear = (driver->output_rear + 1) % driver->num_output_buffers;
		driver->output_count++;

		/* If the low-level driver is not active, activate it. */
		save_and_cli(flags);
		if (! driver->output_active) {
			driver->ops->start_output(driver, driver->output_buffers[driver->output_front],
						  driver->output_sizes[driver->output_front]);
			driver->output_active = 1;
		}
		restore_flags(flags);
	}

	/* Return the number of bytes written to the caller. */
	return bytes_written;
}

static int sparcaudio_ioctl(struct inode * inode, struct file * file,
			    unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	switch (cmd) {
	case AUDIO_DRAIN:
		if (driver->output_count > 0) {
			interruptible_sleep_on(&driver->output_drain_wait);
			retval = (current->signal & ~current->blocked) ? -EINTR : 0;
		}
		break;

	case AUDIO_GETDEV:
		if (driver->ops->sunaudio_getdev) {
			audio_device_t tmp;

			driver->ops->sunaudio_getdev(driver, &tmp);

			copy_to_user_ret((audio_device_t *)arg, &tmp, sizeof(tmp), -EFAULT);
		} else
			retval = -EINVAL;
		break;

	default:
		if (driver->ops->ioctl)
			retval = driver->ops->ioctl(inode,file,cmd,arg,driver);
		else
			retval = -EINVAL;
	}

	return retval;
}

static int sparcaudio_open(struct inode * inode, struct file * file)
{
	int err;

	/* A low-level audio driver must exist. */
	if (!driver)
		return -ENODEV;

	/* We only support minor #4 (/dev/audio) right now. */
	if (MINOR(inode->i_rdev) != 4)
		return -ENXIO;

	/* If the driver is busy, then wait to get through. */
	retry_open:
	if (file->f_mode & FMODE_READ && driver->flags & SDF_OPEN_READ) {
		if (file->f_flags & O_NONBLOCK)
			return -EBUSY;

		interruptible_sleep_on(&driver->open_wait);
		if (current->signal & ~current->blocked)
			return -EINTR;
		goto retry_open;
	}
	if (file->f_mode & FMODE_WRITE && driver->flags & SDF_OPEN_WRITE) {
		if (file->f_flags & O_NONBLOCK)
			return -EBUSY;

		interruptible_sleep_on(&driver->open_wait);
		if (current->signal & ~current->blocked)
			return -EINTR;
		goto retry_open;
	}

	/* Mark the driver as locked for read and/or write. */
	if (file->f_mode & FMODE_READ)
		driver->flags |= SDF_OPEN_READ;
	if (file->f_mode & FMODE_WRITE) {
		driver->output_front = 0;
		driver->output_rear = 0;
		driver->output_count = 0;
		driver->output_active = 0;
		driver->flags |= SDF_OPEN_WRITE;
	}  

	/* Allow the low-level driver to initialize itself. */
	if (driver->ops->open) {
		err = driver->ops->open(inode,file,driver);
		if (err < 0)
			return err;
	}

	MOD_INC_USE_COUNT;

	/* Success! */
	return 0;
}

static void sparcaudio_release(struct inode * inode, struct file * file)
{
	/* Wait for any output still in the queue to be played. */
	if (driver->output_count > 0)
		interruptible_sleep_on(&driver->output_drain_wait);

	/* Force any output to be stopped. */
	driver->ops->stop_output(driver);
	driver->output_active = 0;

	/* Let the low-level driver do any release processing. */
	if (driver->ops->release)
		driver->ops->release(inode,file,driver);

	if (file->f_mode & FMODE_READ)
		driver->flags &= ~(SDF_OPEN_READ);

	if (file->f_mode & FMODE_WRITE)
		driver->flags &= ~(SDF_OPEN_WRITE);

	MOD_DEC_USE_COUNT;

	wake_up_interruptible(&driver->open_wait);
}

static struct file_operations sparcaudio_fops = {
	sparcaudio_lseek,
	sparcaudio_read,
	sparcaudio_write,
	NULL,			/* sparcaudio_readdir */
	NULL,			/* sparcaudio_select */
	sparcaudio_ioctl,
	NULL,			/* sparcaudio_mmap */
	sparcaudio_open,
	sparcaudio_release
};

EXPORT_SYMBOL(register_sparcaudio_driver);
EXPORT_SYMBOL(unregister_sparcaudio_driver);
EXPORT_SYMBOL(sparcaudio_output_done);
EXPORT_SYMBOL(sparcaudio_input_done);

#ifdef MODULE
int init_module(void)
#else
__initfunc(int sparcaudio_init(void))
#endif
{
	/* Register our character device driver with the VFS. */
	if (register_chrdev(SOUND_MAJOR, "sparcaudio", &sparcaudio_fops))
		return -EIO;

#ifdef CONFIG_SPARCAUDIO_AMD7930
	amd7930_init();
#endif

#ifdef CONFIG_SPARCAUDIO_CS4231
	cs4231_init();
#endif

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	unregister_chrdev(SOUND_MAJOR, "sparcaudio");
}
#endif
