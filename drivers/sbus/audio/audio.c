/*
 * drivers/sbus/audio/audio.c
 *
 * Copyright (C) 1996,1997 Thomas K. Dyas (tdyas@eden.rutgers.edu)
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
#include <linux/soundcard.h>
#include <asm/uaccess.h>

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

	/* Setup the circular queues of output and input buffers
	 *
	 * Each buffer is a single page, but output buffers might
	 * be partially filled (by a write with count < PAGE_SIZE),
	 * so each output buffer also has a paired output size.
	 *
	 * Input buffers, on the other hand, always fill completely,
	 * so we don't need input counts - each contains PAGE_SIZE
	 * bytes of audio data.
	 *
	 * TODO: Make number of input/output buffers tunable parameters
	 */

	drv->num_output_buffers = 32;
	drv->output_front = 0;
	drv->output_rear = 0;
	drv->output_count = 0;
	drv->output_active = 0;
	drv->output_buffers = kmalloc(drv->num_output_buffers * sizeof(__u8 *), GFP_KERNEL);
	drv->output_sizes = kmalloc(drv->num_output_buffers * sizeof(size_t), GFP_KERNEL);
	if (!drv->output_buffers || !drv->output_sizes) goto kmalloc_failed1;

	/* Allocate the pages for each output buffer. */
	for (i = 0; i < drv->num_output_buffers; i++) {
		drv->output_buffers[i] = (void *) __get_free_page(GFP_KERNEL);
		if (!drv->output_buffers[i]) goto kmalloc_failed2;
	}

	/* Setup the circular queue of input buffers. */
	drv->num_input_buffers = 32;
	drv->input_front = 0;
	drv->input_rear = 0;
	drv->input_count = 0;
	drv->input_active = 0;
	drv->input_buffers = kmalloc(drv->num_input_buffers * sizeof(__u8 *), GFP_KERNEL);
	if (!drv->input_buffers) goto kmalloc_failed3;

	/* Allocate the pages for each input buffer. */
	for (i = 0; i < drv->num_input_buffers; i++) {
		drv->input_buffers[i] = (void *) __get_free_page(GFP_KERNEL);
		if (!drv->input_buffers[i]) goto kmalloc_failed4;
	}

	/* Ensure that the driver is marked as not being open. */
	drv->flags = 0;

	MOD_INC_USE_COUNT;

	driver = drv;
	return 0;


kmalloc_failed4:
	for (i--; i >= 0; i--)
		free_page((unsigned long) drv->input_buffers[i]);

kmalloc_failed3:
	if (drv->input_buffers)
		kfree(drv->input_buffers);
	i = drv->num_output_buffers;

kmalloc_failed2:
	for (i--; i >= 0; i--)
		free_page((unsigned long) drv->output_buffers[i]);

kmalloc_failed1:
	if (drv->output_buffers)
		kfree(drv->output_buffers);
	if (drv->output_sizes)
		kfree(drv->output_sizes);

	return -ENOMEM;
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

        /* Deallocate the queue of input buffers. */
        for (i = 0; i < driver->num_input_buffers; i++)
                free_page((unsigned long) driver->input_buffers[i]);
        kfree(driver->input_buffers);

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
	drv->output_size -= drv->output_sizes[drv->output_front];
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
	/* Point the queue after the "done" buffer. */
	drv->input_front = (drv->input_front + 1) % drv->num_input_buffers;
	drv->input_count++;

	/* If the input queue is full, shutdown the driver. */
	if (drv->input_count == drv->num_input_buffers) {
		/* Stop the lowlevel driver from inputing. */
		drv->ops->stop_input(drv);
		drv->input_active = 0;
	} else {
		/* Otherwise, give the driver the next buffer. */
		drv->ops->start_input(drv, drv->input_buffers[drv->input_front],
				      PAGE_SIZE);
	}

	/* Wake up any tasks that are waiting. */
	wake_up_interruptible(&drv->input_read_wait);
}



/*
 *	VFS layer interface
 */

static loff_t sparcaudio_llseek(struct file * file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t sparcaudio_read(struct file * file,
			       char *buf, size_t count, loff_t *ppos)
{
	int bytes_to_copy;

	if (! file->f_mode & FMODE_READ)
		return -EINVAL;

	if (driver->input_count == 0) {
		interruptible_sleep_on(&driver->input_read_wait);
		if (signal_pending(current))
			return -EINTR;
	}

	bytes_to_copy = PAGE_SIZE - driver->input_offset;
	if (bytes_to_copy > count)
		bytes_to_copy = count;

	copy_to_user_ret(buf, driver->input_buffers[driver->input_rear]+driver->input_offset,
			 bytes_to_copy, -EFAULT);
	driver->input_offset += bytes_to_copy;

	if (driver->input_offset >= PAGE_SIZE) {
		driver->input_rear = (driver->input_rear + 1) % driver->num_input_buffers;
		driver->input_count--;
		driver->input_offset = 0;
	}

	return bytes_to_copy;
}

static void sparcaudio_reorganize_buffers(struct sparcaudio_driver * driver)
{
  /* It may never matter but if it does this routine will pack */
  /* buffers to free space for more data */
}

static void sparcaudio_sync_output(struct sparcaudio_driver * driver)
{
	unsigned long flags;

	/* If the low-level driver is not active, activate it. */
	save_and_cli(flags);
	if (! driver->output_active) {
		driver->ops->start_output(driver,
				driver->output_buffers[driver->output_front],
				driver->output_sizes[driver->output_front]);
		driver->output_active = 1;
	}
	restore_flags(flags);
}

static ssize_t sparcaudio_write(struct file * file, const char *buf,
				size_t count, loff_t *ppos)
{
	int bytes_written = 0, bytes_to_copy;

	/* Ensure that we have something to write. */
	if (count < 1) {
		sparcaudio_sync_output(driver);
		return 0;
	}

	/* Loop until all output is written to device. */
	while (count > 0) {
		/* Check to make sure that an output buffer is available. */
		/* If not, make valiant attempt */
		if (driver->output_count == driver->num_output_buffers)
			sparcaudio_reorganize_buffers(driver);

		if (driver->output_count == driver->num_output_buffers) {
 			/* We need buffers, so... */
			sparcaudio_sync_output(driver);
 			interruptible_sleep_on(&driver->output_write_wait);
			if (signal_pending(current))
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
		driver->output_size += bytes_to_copy;

		/* Activate the driver if more than page of data is waiting. */
		if (driver->output_size > 4096)
			sparcaudio_sync_output(driver);
	}

	/* Return the number of bytes written to the caller. */
	return bytes_written;
}

static int sparcaudio_ioctl(struct inode * inode, struct file * file,
			    unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	struct audio_info ainfo;

	switch (cmd) {
	case SNDCTL_DSP_SYNC:
	case AUDIO_DRAIN:
		if (driver->output_count > 0) {
			interruptible_sleep_on(&driver->output_drain_wait);
			retval = signal_pending(current) ? -EINTR : 0;
		}
		break;

	case AUDIO_GETDEV:
		if (driver->ops->sunaudio_getdev) {
			audio_device_t tmp;

			driver->ops->sunaudio_getdev(driver, &tmp);

			copy_to_user_ret((audio_device_t *)arg, &tmp, sizeof(tmp), -EFAULT);
		} else
			retval = -EINVAL;

		printk(KERN_INFO "sparcaudio_ioctl: AUDIO_GETDEV\n");
		break;

	case AUDIO_GETDEV_SUNOS:
		if (driver->ops->sunaudio_getdev_sunos) {
			int tmp=driver->ops->sunaudio_getdev_sunos(driver);

			copy_to_user_ret((int *)arg, &tmp, sizeof(tmp), -EFAULT);
		} else
			retval = -EINVAL;

		printk(KERN_INFO "sparcaudio_ioctl: AUDIO_GETDEV_SUNOS\n");
		break;

	case AUDIO_GETINFO:

		AUDIO_INITINFO(&ainfo);

		if (driver->ops->get_input_rate)
		  ainfo.record.sample_rate =
		    driver->ops->get_input_rate(driver);
		if (driver->ops->get_input_channels)
		  ainfo.record.channels =
		    driver->ops->get_input_channels(driver);
		if (driver->ops->get_input_precision)
		  ainfo.record.precision =
		    driver->ops->get_input_precision(driver);
		if (driver->ops->get_input_encoding)
		  ainfo.record.encoding =
		    driver->ops->get_input_encoding(driver);
		if (driver->ops->get_input_volume)
		  ainfo.record.gain =
		    driver->ops->get_input_volume(driver);
		if (driver->ops->get_input_port)
		  ainfo.record.port =
		    driver->ops->get_input_port(driver);
		if (driver->ops->get_input_ports)
		  ainfo.record.avail_ports =
		    driver->ops->get_input_ports(driver);
		ainfo.record.buffer_size = PAGE_SIZE;
		ainfo.record.samples = 0;
		ainfo.record.eof = 0;
		ainfo.record.pause = 0;
		ainfo.record.error = 0;
		ainfo.record.waiting = 0;
		if (driver->ops->get_input_balance)
		  ainfo.record.balance =
		    driver->ops->get_input_balance(driver);
		ainfo.record.minordev = 4;
		ainfo.record.open = 1;
		ainfo.record.active = 0;

		if (driver->ops->get_output_rate)
		  ainfo.play.sample_rate =
		    driver->ops->get_output_rate(driver);
		if (driver->ops->get_output_channels)
		  ainfo.play.channels =
		    driver->ops->get_output_channels(driver);
		if (driver->ops->get_output_precision)
		  ainfo.play.precision =
		    driver->ops->get_output_precision(driver);
		if (driver->ops->get_output_encoding)
		  ainfo.play.encoding =
		    driver->ops->get_output_encoding(driver);
		if (driver->ops->get_output_volume)
		  ainfo.play.gain =
		    driver->ops->get_output_volume(driver);
		if (driver->ops->get_output_port)
		  ainfo.play.port =
		    driver->ops->get_output_port(driver);
		if (driver->ops->get_output_ports)
		  ainfo.play.avail_ports =
		    driver->ops->get_output_ports(driver);
		ainfo.play.buffer_size = PAGE_SIZE;
		ainfo.play.samples = 0;
		ainfo.play.eof = 0;
		ainfo.play.pause = 0;
		ainfo.play.error = 0;
		ainfo.play.waiting = waitqueue_active(&driver->open_wait);
		if (driver->ops->get_output_balance)
		  ainfo.play.balance =
		    driver->ops->get_output_balance(driver);
		ainfo.play.minordev = 4;
		ainfo.play.open = 1;
		ainfo.play.active = driver->output_active;

		if (driver->ops->get_monitor_volume)
		  ainfo.monitor_gain =
		    driver->ops->get_monitor_volume(driver);

		if (driver->ops->get_output_muted)
		  ainfo.output_muted =
		    driver->ops->get_output_muted(driver);

		printk("sparcaudio_ioctl: AUDIO_GETINFO\n");

		copy_to_user_ret((struct audio_info *)arg, &ainfo,
				 sizeof(ainfo), -EFAULT);

		break;

	case AUDIO_SETINFO:
	  {
	    audio_info_t curinfo;

	    copy_from_user_ret(&ainfo, (audio_info_t *) arg, sizeof(audio_info_t), -EFAULT);

	    /* Without these there's no point in trying */
	    if (!driver->ops->get_input_precision ||
		!driver->ops->get_input_channels ||
		!driver->ops->get_input_rate ||
		!driver->ops->get_input_encoding ||
		!driver->ops->get_output_precision ||
		!driver->ops->get_output_channels ||
                !driver->ops->get_output_rate ||
                !driver->ops->get_output_encoding) 
	      {
		retval = -EINVAL;
		break;
	      }

	    /* Do bounds checking for things which always apply.
	     * Follow with enforcement of basic tenets of certain
	     * encodings. Everything over and above generic is
	     * enforced by the driver, which can assume that
	     * Martian cases are taken care of here. */
	    if (Modify(ainfo.play.gain) && 
		((ainfo.play.gain > AUDIO_MAX_GAIN) || 
		 (ainfo.play.gain < AUDIO_MIN_GAIN))) {
	      /* Need to differentiate this from e.g. the above error */
	      retval = -EINVAL;
	      break;
	    }
	    if (Modify(ainfo.record.gain) &&
		((ainfo.record.gain > AUDIO_MAX_GAIN) ||
		 (ainfo.record.gain < AUDIO_MIN_GAIN))) {
	      retval = -EINVAL;
	      break;
	    }
	    if (Modify(ainfo.monitor_gain) &&
		((ainfo.monitor_gain > AUDIO_MAX_GAIN) ||
		 (ainfo.monitor_gain < AUDIO_MIN_GAIN))) {
	      retval = -EINVAL;
	      break;
	    }
	    /* Don't need to check less than zero on these */
	    if (Modifyc(ainfo.play.balance) &&
		(ainfo.play.balance > AUDIO_RIGHT_BALANCE)) {
	      retval = -EINVAL;
	      break;
	    }
	    if (Modifyc(ainfo.record.balance) &&
		(ainfo.record.balance > AUDIO_RIGHT_BALANCE)) {
	      retval = -EINVAL;
	      break;
	    }
	    
	    /* If any of these changed, record them all, then make
	     * changes atomically. If something fails, back it all out. */
	    if (Modify(ainfo.record.precision) || 
		Modify(ainfo.record.sample_rate) ||
		Modify(ainfo.record.channels) ||
		Modify(ainfo.record.encoding) || 
		Modify(ainfo.play.precision) || 
		Modify(ainfo.play.sample_rate) ||
		Modify(ainfo.play.channels) ||
		Modify(ainfo.play.encoding)) 
	      {
		/* If they're trying to change something we
		 * have no routine for, they lose */
		if ((!driver->ops->set_input_encoding && 
		    Modify(ainfo.record.encoding)) ||
		    (!driver->ops->set_input_rate && 
		    Modify(ainfo.record.sample_rate)) ||
		    (!driver->ops->set_input_precision && 
		    Modify(ainfo.record.precision)) ||
		    (!driver->ops->set_input_channels && 
		    Modify(ainfo.record.channels))) {
		  retval = -EINVAL;
		  break;
		}		  

		curinfo.record.encoding = (Modify(ainfo.record.encoding) ? 
					   ainfo.record.encoding :
					   driver->ops->get_input_encoding(driver));	    
		curinfo.record.sample_rate = (Modify(ainfo.record.sample_rate) ? 
					   ainfo.record.sample_rate :
					   driver->ops->get_input_rate(driver));	   
		curinfo.record.precision = (Modify(ainfo.record.precision) ? 
					   ainfo.record.precision :
					   driver->ops->get_input_precision(driver));	   
		curinfo.record.channels = (Modify(ainfo.record.channels) ? 
					   ainfo.record.channels :
					   driver->ops->get_input_channels(driver));	   
		switch (curinfo.record.encoding) {
		case AUDIO_ENCODING_ALAW:
		case AUDIO_ENCODING_ULAW:
		  if (Modify(ainfo.record.precision) && 
		      ainfo.record.precision != 8) {
		    retval = -EINVAL;
		    break;
		  }
		  if (Modify(ainfo.record.channels) && 
		      ainfo.record.channels != 1) {
		    retval = -EINVAL;
		    break;
		  }
		  break;
		case AUDIO_ENCODING_LINEAR:
		case AUDIO_ENCODING_LINEARLE:
		  if (Modify(ainfo.record.precision) && 
		      ainfo.record.precision != 16) {
		    retval = -EINVAL;
		    break;
		  }
		  if (Modify(ainfo.record.channels) && 
		      (ainfo.record.channels != 1 && 
		       ainfo.record.channels != 2)) 
		    {
		      retval = -EINVAL;
		      break;
		    }
		  break;
		case AUDIO_ENCODING_LINEAR8:
		  if (Modify(ainfo.record.precision) && 
		      ainfo.record.precision != 8) {
		    retval = -EINVAL;
		    break;
		  }
		  if (Modify(ainfo.record.channels) && 
		      (ainfo.record.channels != 1 && 
		       ainfo.record.channels != 2)) 
		    {
		      retval = -EINVAL;
		      break;
		    }
		}

		if (retval < 0)
		  break;

		/* If they're trying to change something we
		 * have no routine for, they lose */
		if ((!driver->ops->set_output_encoding && 
		    Modify(ainfo.play.encoding)) ||
		    (!driver->ops->set_output_rate && 
		    Modify(ainfo.play.sample_rate)) ||
		    (!driver->ops->set_output_precision && 
		    Modify(ainfo.play.precision)) ||
		    (!driver->ops->set_output_channels && 
		    Modify(ainfo.play.channels))) {
		  retval = -EINVAL;
		  break;
		}		  

		curinfo.play.encoding = (Modify(ainfo.play.encoding) ? 
					 ainfo.play.encoding : 
					 driver->ops->get_output_encoding(driver));
		curinfo.play.sample_rate = (Modify(ainfo.play.sample_rate) ? 
					   ainfo.play.sample_rate :
					   driver->ops->get_output_rate(driver));	   
		curinfo.play.precision = (Modify(ainfo.play.precision) ? 
					   ainfo.play.precision :
					   driver->ops->get_output_precision(driver));	   
		curinfo.play.channels = (Modify(ainfo.play.channels) ? 
					   ainfo.play.channels :
					   driver->ops->get_output_channels(driver));	   
		switch (curinfo.play.encoding) {
		case AUDIO_ENCODING_ALAW:
		case AUDIO_ENCODING_ULAW:
		  if (Modify(ainfo.play.precision) && 
		      ainfo.play.precision != 8) {
		    retval = -EINVAL;
		    break;
		  }
		  if (Modify(ainfo.play.channels) && 
		      ainfo.play.channels != 1) {
		    retval = -EINVAL;
		    break;
		  }
		  break;
		case AUDIO_ENCODING_LINEAR:
		case AUDIO_ENCODING_LINEARLE:
		  if (Modify(ainfo.play.precision) && 
		      ainfo.play.precision != 16) {
		    retval = -EINVAL;
		    break;
		  }
		  if (Modify(ainfo.play.channels) && 
		      (ainfo.play.channels != 1 && 
		       ainfo.play.channels != 2)) 
		    {
		      retval = -EINVAL;
		      break;
		    }
		  break;
		case AUDIO_ENCODING_LINEAR8:
		  if (Modify(ainfo.play.precision) && 
		      ainfo.play.precision != 8) {
		    retval = -EINVAL;
		    break;
		  }
		  if (Modify(ainfo.play.channels) && 
		      (ainfo.play.channels != 1 && 
		       ainfo.play.channels != 2)) 
		    {
		      retval = -EINVAL;
		      break;
		    }
		}
		
		if (retval < 0)
		  break;

		/* If we got this far, we're at least sane with
		 * respect to generics. Try the changes. */
		if ((driver->ops->set_input_precision(driver, ainfo.record.precision) < 0) ||
		    (driver->ops->set_output_precision(driver, ainfo.play.precision) < 0) ||
		    (driver->ops->set_input_channels(driver, ainfo.record.channels) < 0) ||
		    (driver->ops->set_output_channels(driver, ainfo.play.channels) < 0) ||
		    (driver->ops->set_input_rate(driver, ainfo.record.sample_rate) < 0) ||
		    (driver->ops->set_output_rate(driver, ainfo.play.sample_rate) < 0) ||
		    (driver->ops->set_input_encoding(driver, ainfo.record.encoding) < 0) ||
		    (driver->ops->set_output_encoding(driver, ainfo.play.encoding) < 0)) 
		  {
		    /* Pray we can set it all back. If not, uh... */
		    driver->ops->set_input_precision(driver, curinfo.record.precision);
		    driver->ops->set_output_precision(driver, curinfo.play.precision);
		    driver->ops->set_input_channels(driver, curinfo.record.channels);
		    driver->ops->set_output_channels(driver, curinfo.play.channels);
		    driver->ops->set_input_rate(driver, curinfo.record.sample_rate);
		    driver->ops->set_output_rate(driver, curinfo.play.sample_rate);
		    driver->ops->set_input_encoding(driver, curinfo.record.encoding); 
		    driver->ops->set_output_encoding(driver, curinfo.play.encoding);
		  }

	    }

	    printk("sparcaudio_ioctl: AUDIO_SETINFO\n");
	    break;
	  }

	default:
		if (driver->ops->ioctl)
			retval = driver->ops->ioctl(inode,file,cmd,arg,driver);
		else {
			retval = -EINVAL;

			printk("sparcaudio_ioctl: 0x%x\n", cmd);
		}
	}

	return retval;
}

static int sparcaudioctl_release(struct inode * inode, struct file * file)
{
	MOD_DEC_USE_COUNT;

	return 0;
}

static struct file_operations sparcaudioctl_fops = {
	NULL,
	NULL,
	NULL,
	NULL,			/* sparcaudio_readdir */
	NULL,			/* sparcaudio_select */
	sparcaudio_ioctl,
	NULL,			/* sparcaudio_mmap */
	NULL,
	sparcaudioctl_release
};

static int sparcaudio_open(struct inode * inode, struct file * file)
{
	int err;

	/* A low-level audio driver must exist. */
	if (!driver)
		return -ENODEV;

	if (MINOR(inode->i_rdev) == 5) {

		file->f_op = &sparcaudioctl_fops;

		MOD_INC_USE_COUNT;

		return 0;
	}

	/* We only support minor #4 (/dev/audio) right now. */
	if (MINOR(inode->i_rdev) != 4)
		return -ENXIO;

	/* If the driver is busy, then wait to get through. */
	retry_open:
	if (file->f_mode & FMODE_READ && driver->flags & SDF_OPEN_READ) {
		if (file->f_flags & O_NONBLOCK)
			return -EBUSY;

		interruptible_sleep_on(&driver->open_wait);
		if (signal_pending(current))
			return -EINTR;
		goto retry_open;
	}
	if (file->f_mode & FMODE_WRITE && driver->flags & SDF_OPEN_WRITE) {
		if (file->f_flags & O_NONBLOCK)
			return -EBUSY;

		interruptible_sleep_on(&driver->open_wait);
		if (signal_pending(current))
			return -EINTR;
		goto retry_open;
	}

	/* Mark the driver as locked for read and/or write. */
	if (file->f_mode & FMODE_READ) {
		driver->input_offset = 0;
		driver->input_front = 0;
		driver->input_rear = 0;
		driver->input_count = 0;
		driver->ops->start_input(driver, driver->input_buffers[driver->input_front],
					 PAGE_SIZE);
		driver->input_active = 1;
		driver->flags |= SDF_OPEN_READ;
	}
	if (file->f_mode & FMODE_WRITE) {
		driver->output_size = 0;
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

static int sparcaudio_release(struct inode * inode, struct file * file)
{
        /* Anything in the queue? */
        sparcaudio_sync_output(driver);

	/* Stop input */
	driver->ops->stop_input(driver);
	driver->input_active = 0;

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

	return 0;
}

static struct file_operations sparcaudio_fops = {
	sparcaudio_llseek,
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
