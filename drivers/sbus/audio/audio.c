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
#include <linux/init.h>

#include "audio.h"


/*
 *	Low-level driver interface.
 */

/* We only support one low-level audio driver. */
static struct sparcaudio_driver *driver;

int register_sparcaudio_driver(struct sparcaudio_driver *drv)
{
	/* If a driver is already present, don't allow it to register. */
	if (driver)
		return -EIO;

	MOD_INC_USE_COUNT;

	driver = drv;
	return 0;
}

int unregister_sparcaudio_driver(struct sparcaudio_driver *drv)
{
	/* Make sure that the current driver is unregistering. */
	if (driver != drv)
		return -EIO;

	MOD_DEC_USE_COUNT;

	driver = NULL;
	return 0;
}

static void sparcaudio_output_done_task(void * unused)
{
	unsigned long flags;

	save_and_cli(flags);
	printk(KERN_DEBUG "sparcaudio: next buffer\n");
	driver->ops->start_output(driver, driver->output_buffers[driver->output_front],
				  driver->output_sizes[driver->output_front]);
	driver->output_active = 1;
	restore_flags(flags);
}

static struct tq_struct sparcaudio_output_tqueue = {
	0, 0, sparcaudio_output_done_task, 0 };

void sparcaudio_output_done(void)
{
	/* Point the queue after the "done" buffer. */
	driver->output_front++;
	driver->output_count--;

	/* If the output queue is empty, shutdown the driver. */
	if (driver->output_count == 0) {
		/* Stop the lowlevel driver from outputing. */
		printk(KERN_DEBUG "sparcaudio: lowlevel driver shutdown\n");
		driver->ops->stop_output(driver);
		driver->output_active = 0;
		return;
	}

	/* Otherwise, queue a task to give the driver the next buffer. */
	queue_task(&sparcaudio_output_tqueue, &tq_immediate);

	/* Wake up any tasks that are waiting. */
	wake_up_interruptible(&driver->output_write_wait);
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
			printk(KERN_DEBUG "sparcaudio: waiting for free buffer\n");
			interruptible_sleep_on(&driver->output_write_wait);
			if (current->signal & ~current->blocked)
				return bytes_written > 0 ? bytes_written : -EINTR;
		}

		/* Determine how much we can copy in this run. */
		bytes_to_copy = count;
		if (bytes_to_copy > PAGE_SIZE)
			bytes_to_copy = PAGE_SIZE;

		err = verify_area(VERIFY_READ, buf, bytes_to_copy);
		if (err)
			return err;

		memcpy_fromfs(driver->output_buffers[driver->output_rear], buf, bytes_to_copy);

		/* Update the queue pointers. */
		bytes_written += bytes_to_copy;
		driver->output_sizes[driver->output_rear] = bytes_to_copy;
		driver->output_rear = (driver->output_rear + 1) % driver->num_output_buffers;
		driver->output_count++;

		/* If the low-level driver is not active, activate it. */
		save_and_cli(flags);
		if (! driver->output_active) {
			printk(KERN_DEBUG "sparcaudio: activating lowlevel driver\n");
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
	switch (cmd) {
	default:
		if (driver->ops->ioctl)
			return driver->ops->ioctl(inode,file,cmd,arg,driver);
		else
			return -EINVAL;
	}
	return 0;
}

static int sparcaudio_open(struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int i;

	/* We only support minor #4 (/dev/audio right now. */
	if (minor != 4)
		return -ENXIO;

	/* Make sure that the driver is not busy. */
	if (driver->busy)
		return -EBUSY;

	/* Setup the queue of output buffers. */
	driver->num_output_buffers = 32;
	driver->output_front = 0;
	driver->output_rear = 0;
	driver->output_count = 0;
	driver->output_active = 0;
	driver->output_buffers = kmalloc(32 * sizeof(__u8 *), GFP_KERNEL);
	driver->output_sizes = kmalloc(32 * sizeof(__u8 *), GFP_KERNEL);
	if (!driver->output_buffers || !driver->output_sizes)
		return -ENOMEM;

	/* Allocate space for the output buffers. */
	for (i = 0; i < driver->num_output_buffers; i++) {
		driver->output_buffers[i] = (void *) __get_free_page(GFP_KERNEL);
		if (!driver->output_buffers[i])
			return -ENOMEM;
	}

	/* Allow the low-level driver to initialize itself. */
	if (driver->ops->open)
		driver->ops->open(inode,file,driver);


	/* Mark the driver as busy. */
	driver->busy = 1;

	MOD_INC_USE_COUNT;

	/* Success return. */
	return 0;
}

static void sparcaudio_release(struct inode * inode, struct file * file)
{
	int i;

	if (driver->ops->release)
		driver->ops->release(inode,file,driver);

	MOD_DEC_USE_COUNT;

	driver->busy = 0;

	for (i = 0; i < driver->num_output_buffers; i++)
		kfree(driver->output_buffers[i]);
	kfree(driver->output_buffers);
	kfree(driver->output_sizes);
}

static struct file_operations sparcaudio_fops = {
	sparcaudio_lseek,
	sparcaudio_read,
	sparcaudio_write,
	NULL,			/* sparcaudio_readdir */
	NULL,			/* sparcaudio_poll */
	sparcaudio_ioctl,
	NULL,			/* sparcaudio_mmap */
	sparcaudio_open,
	sparcaudio_release
};


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

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
}
#endif
