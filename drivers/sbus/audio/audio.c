/*
 * drivers/sbus/audio/audio.c
 *
 * Copyright (C) 1996,1997 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 * Copyright (C) 1997 Derrick J. Brashear (shadow@dementia.org)
 * Copyright (C) 1997 Brent Baccala (baccala@freesoft.org)
 * 
 * Mixer code adapted from code contributed by and
 * Copyright (C) 1998 Michael Mraka (michael@fi.muni.cz)
 *
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
#include <asm/pgtable.h>

#include <asm/audioio.h>


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
	 * be partially filled (by a write with count < 4096),
	 * so each output buffer also has a paired output size.
	 *
	 * Input buffers, on the other hand, always fill completely,
	 * so we don't need input counts - each contains 4096
	 * bytes of audio data.
	 *
	 * TODO: Make number of input/output buffers tunable parameters
	 */

	drv->num_output_buffers = 32;
        drv->playing_count = 0;
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
        drv->recording_count = 0;
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

void sparcaudio_output_done(struct sparcaudio_driver * drv, int reclaim)
{
    /* Reclaim a buffer unless it's still in the DMA pipe */
    if (reclaim) {
        if (drv->output_count > 0) 
            drv->output_count--;
        else 
            if (drv->playing_count > 0) 
                drv->playing_count--;
    } else 
        drv->playing_count++;

	/* Point the queue after the "done" buffer. */
	drv->output_size -= drv->output_sizes[drv->output_front];
	drv->output_front = (drv->output_front + 1) % drv->num_output_buffers;

	/* If the output queue is empty, shutdown the driver. */
	if (drv->output_count == 0) {
            if (drv->playing_count == 0) {
		/* Stop the lowlevel driver from outputing. */
		drv->ops->stop_output(drv);
		drv->output_active = 0;

		/* Wake up any waiting writers or syncers and return. */
		wake_up_interruptible(&drv->output_write_wait);
		wake_up_interruptible(&drv->output_drain_wait);
		return;
            }
	}

    /* If we got back a buffer, see if anyone wants to write to it */
    if (reclaim || ((drv->output_count + drv->playing_count) 
                    < drv->num_output_buffers))
        wake_up_interruptible(&drv->output_write_wait);
    
    drv->ops->start_output(drv, drv->output_buffers[drv->output_front],
                           drv->output_sizes[drv->output_front]);

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
				      4096);
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

	bytes_to_copy = 4096 - driver->input_offset;
	if (bytes_to_copy > count)
		bytes_to_copy = count;

	copy_to_user_ret(buf, driver->input_buffers[driver->input_rear]+driver->input_offset,
			 bytes_to_copy, -EFAULT);
	driver->input_offset += bytes_to_copy;

	if (driver->input_offset >= 4096) {
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
        if ((!driver->output_active) && (driver->output_count > 0)) {
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

        if (! file->f_mode & FMODE_WRITE)
            return -EINVAL;

	/* Loop until all output is written to device. */
	while (count > 0) {
            /* Check to make sure that an output buffer is available. */
            /* If not, make valiant attempt */
            if (driver->num_output_buffers == 
                (driver->output_count + driver->playing_count))
                sparcaudio_reorganize_buffers(driver);
            
            if (driver->num_output_buffers == 
                (driver->output_count + driver->playing_count)) {
                /* We need buffers, so... */
                sparcaudio_sync_output(driver);
                interruptible_sleep_on(&driver->output_write_wait);
                if (signal_pending(current))
                    return bytes_written > 0 ? bytes_written : -EINTR;
		}

            /* No buffers were freed. Go back to sleep */
            if (driver->num_output_buffers == 
                (driver->output_count + driver->playing_count)) 
                continue;

            /* Determine how much we can copy in this iteration. */
            bytes_to_copy = count;
            if (bytes_to_copy > 4096)
                bytes_to_copy = 4096;

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
	}
        sparcaudio_sync_output(driver);

	/* Return the number of bytes written to the caller. */
	return bytes_written;
}

#define COPY_IN(arg, get) get_user(get, (int *)arg)
#define COPY_OUT(arg, ret) put_user(ret, (int *)arg)

/* Add these in as new devices are supported. Belongs in audioio.h, actually */
#define SUPPORTED_MIXER_DEVICES         (SOUND_MASK_VOLUME)
#define MONO_DEVICES (SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_SPEAKER | SOUND_MASK_MIC)

static inline int sparcaudio_mixer_ioctl(struct inode * inode, struct file * file,
					 unsigned int cmd, unsigned long arg)
{
	int i = 0, j = 0;
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		/* For any missing routines, pretend we changed things anyhow for now */
		switch (cmd & 0xff) {
		case SOUND_MIXER_VOLUME:
			if (driver->ops->get_output_channels)
				j = driver->ops->get_output_channels(driver);
			COPY_IN(arg, i);
			if (j == 1) {
				i = s_to_m(i);
				if (driver->ops->set_output_volume)
					driver->ops->set_output_volume(driver, i * 255/100);
				if (driver->ops->get_output_volume)
					i = driver->ops->get_output_volume(driver);
				i = m_to_s(i);
			} else {
				/* there should be stuff here which calculates balance and
				   volume on a stereo device. will do it eventually */
				i = s_to_g(i);
				if (driver->ops->set_output_volume)
					driver->ops->set_output_volume(driver, i * 255/100);
				if (driver->ops->get_output_volume)
					i = driver->ops->get_output_volume(driver);
				j = s_to_b(i);
				if (driver->ops->set_output_balance)
					driver->ops->set_output_balance(driver, j);
				if (driver->ops->get_output_balance)
					j = driver->ops->get_output_balance(driver);
				i = b_to_s(i,j);
			}
			return COPY_OUT(arg, i);
		default:
			/* Play like we support other things */
			return COPY_OUT(arg, i);
		}
	} else {
		switch (cmd & 0xff) {
		case SOUND_MIXER_RECSRC:
			if (driver->ops->get_input_port)
				i = driver->ops->get_input_port(driver);
			/* only one should ever be selected */
			if (i & AUDIO_ANALOG_LOOPBACK) j = SOUND_MASK_IMIX; /* ? */
			if (i & AUDIO_CD) j = SOUND_MASK_CD;
			if (i & AUDIO_LINE_IN) j = SOUND_MASK_LINE;
			if (i & AUDIO_MICROPHONE) j = SOUND_MASK_MIC;

			return COPY_OUT(arg, j);

		case SOUND_MIXER_RECMASK:
			if (driver->ops->get_input_ports)
				i = driver->ops->get_input_ports(driver);
			/* what do we support? */
			if (i & AUDIO_MICROPHONE) j |= SOUND_MASK_MIC;
			if (i & AUDIO_LINE_IN) j |= SOUND_MASK_LINE;
			if (i & AUDIO_CD) j |= SOUND_MASK_CD;
			if (i & AUDIO_ANALOG_LOOPBACK) j |= SOUND_MASK_IMIX; /* ? */

			return COPY_OUT(arg, j);

		case SOUND_MIXER_CAPS: /* mixer capabilities */
			i = SOUND_CAP_EXCL_INPUT;
			return COPY_OUT(arg, i);

		case SOUND_MIXER_DEVMASK: /* all supported devices */
		case SOUND_MIXER_STEREODEVS: /* what supports stereo */
			if (driver->ops->get_input_ports)
				i = driver->ops->get_input_ports(driver);
			/* what do we support? */
			if (i & AUDIO_MICROPHONE) j |= SOUND_MASK_MIC;
			if (i & AUDIO_LINE_IN) j |= SOUND_MASK_LINE;
			if (i & AUDIO_CD) j |= SOUND_MASK_CD;
			if (i & AUDIO_ANALOG_LOOPBACK) j |= SOUND_MASK_IMIX; /* ? */

			if (driver->ops->get_output_ports)
				i = driver->ops->get_output_ports(driver);
			if (i & AUDIO_SPEAKER) j |= SOUND_MASK_SPEAKER;
			if (i & AUDIO_HEADPHONE) j |= SOUND_MASK_LINE; /* ? */
			if (i & AUDIO_LINE_OUT) j |= SOUND_MASK_LINE;
			
			j |= SOUND_MASK_VOLUME;

			if ((cmd & 0xff) == SOUND_MIXER_STEREODEVS)
			j &= ~(MONO_DEVICES);
			return COPY_OUT(arg, j);

		case SOUND_MIXER_VOLUME:
			if (driver->ops->get_output_channels)
				j = driver->ops->get_output_channels(driver);
			if (j == 1) {
				if (driver->ops->get_output_volume)
					i = driver->ops->get_output_volume(driver);
				i = m_to_s(i);
			} else {
				/* there should be stuff here which calculates balance and
				   volume on a stereo device. will do it eventually */
				if (driver->ops->get_output_volume)
					i = driver->ops->get_output_volume(driver);
				if (driver->ops->get_output_balance)
					j = driver->ops->get_output_balance(driver);
				i = b_to_s(i,j);
			}
			return COPY_OUT(arg, i);

		default:
			/* Play like we support other things */
			return COPY_OUT(arg, i);
		}
	}
}

static int sparcaudio_ioctl(struct inode * inode, struct file * file,
			    unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	struct audio_info ainfo;

        if (((cmd >> 8) & 0xff) == 'M') {
            return sparcaudio_mixer_ioctl(inode, file, cmd, arg);
        }

	switch (cmd) {
	case SNDCTL_DSP_SYNC:
	case AUDIO_DRAIN:
		if (driver->output_count > 0) {
			interruptible_sleep_on(&driver->output_drain_wait);
			retval = signal_pending(current) ? -EINTR : 0;
		}
		break;

        case AUDIO_FLUSH:
                if (driver->output_active && (file->f_mode & FMODE_WRITE)) {
                    wake_up_interruptible(&driver->output_write_wait);
                    driver->ops->stop_output(driver);
                    driver->output_active = 0;
                    driver->output_front = 0;
                    driver->output_rear = 0;
                    driver->output_count = 0;
                    driver->output_size = 0;
                    driver->playing_count = 0;
                }
                if (driver->input_active && (file->f_mode & FMODE_READ)) {
                    wake_up_interruptible(&driver->input_read_wait);
                    driver->ops->stop_input(driver);
                    driver->input_active = 0;
                    driver->input_front = 0;
                    driver->input_rear = 0;
                    driver->input_count = 0;
                    driver->recording_count = 0;
                }
                if ((file->f_mode & FMODE_READ) && 
                    !(driver->flags & SDF_OPEN_READ)) {
                    driver->ops->start_input(driver, 
                                             driver->input_buffers[driver->input_front],
                                             4096);
                    driver->input_active = 1;
                    }
                if ((file->f_mode & FMODE_WRITE) && 
                    !(driver->flags & SDF_OPEN_WRITE)) {
                    sparcaudio_sync_output(driver);
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

	case AUDIO_GETDEV_SUNOS:
		if (driver->ops->sunaudio_getdev_sunos) {
			int tmp=driver->ops->sunaudio_getdev_sunos(driver);

			if (put_user(tmp, (int *)arg))
				retval = -EFAULT;
		} else
			retval = -EINVAL;

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
		ainfo.record.buffer_size = 4096;
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
                /* This is not defined in the play context in Solaris */
		ainfo.play.buffer_size = 0;
		ainfo.play.samples = 0;
		ainfo.play.eof = 0;
		ainfo.play.pause = 0;
		ainfo.play.error = 0;
		ainfo.play.waiting = waitqueue_active(&driver->open_wait);
		if (driver->ops->get_output_balance)
		  ainfo.play.balance =
                      (unsigned char)driver->ops->get_output_balance(driver);
		ainfo.play.minordev = 4;
		ainfo.play.open = 1;
		ainfo.play.active = driver->output_active;

		if (driver->ops->get_monitor_volume)
		  ainfo.monitor_gain =
		    driver->ops->get_monitor_volume(driver);

		if (driver->ops->get_output_muted)
		  ainfo.output_muted =
                      (unsigned char)driver->ops->get_output_muted(driver);

		copy_to_user_ret((struct audio_info *)arg, &ainfo,
				 sizeof(ainfo), -EFAULT);

		break;

	case AUDIO_SETINFO:
	  {
	    audio_info_t curinfo, newinfo;

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

                curinfo.record.encoding = driver->ops->get_input_encoding(driver);
                curinfo.record.sample_rate = driver->ops->get_input_rate(driver);
                curinfo.record.precision = driver->ops->get_input_precision(driver);
                curinfo.record.channels = driver->ops->get_input_channels(driver);
                newinfo.record.encoding = Modify(ainfo.record.encoding) ?
                  ainfo.record.encoding : curinfo.record.encoding;
                newinfo.record.sample_rate = Modify(ainfo.record.sample_rate)?
                  ainfo.record.sample_rate : curinfo.record.sample_rate;
                newinfo.record.precision = Modify(ainfo.record.precision) ?
                  ainfo.record.precision : curinfo.record.precision;
                newinfo.record.channels = Modify(ainfo.record.channels) ?
                  ainfo.record.channels : curinfo.record.channels;

		switch (newinfo.record.encoding) {
		case AUDIO_ENCODING_ALAW:
		case AUDIO_ENCODING_ULAW:
                  if (newinfo.record.precision != 8) {
                    retval = -EINVAL;
                    break;
                  }
                  if (newinfo.record.channels != 1) {
                    retval = -EINVAL;
                    break;
                  }
                  break;
                case AUDIO_ENCODING_LINEAR:
                case AUDIO_ENCODING_LINEARLE:
                  if (newinfo.record.precision != 16) {
                    retval = -EINVAL;
                    break;
                  }
                  if (newinfo.record.channels != 1 &&
                       newinfo.record.channels != 2)
                    {
                      retval = -EINVAL;
                      break;
                    }
                  break;
                case AUDIO_ENCODING_LINEAR8:
                  if (newinfo.record.precision != 8) {
                    retval = -EINVAL;
                    break;
                  }
                  if (newinfo.record.channels != 1 &&
                       newinfo.record.channels != 2)
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

                curinfo.play.encoding = driver->ops->get_output_encoding(driver);
                curinfo.play.sample_rate = driver->ops->get_output_rate(driver);
                curinfo.play.precision = driver->ops->get_output_precision(driver);
                curinfo.play.channels = driver->ops->get_output_channels(driver);
                newinfo.play.encoding = Modify(ainfo.play.encoding) ?
                  ainfo.play.encoding : curinfo.play.encoding;
                newinfo.play.sample_rate = Modify(ainfo.play.sample_rate) ?
                  ainfo.play.sample_rate : curinfo.play.sample_rate;
                newinfo.play.precision = Modify(ainfo.play.precision) ?
                  ainfo.play.precision : curinfo.play.precision;
                newinfo.play.channels = Modify(ainfo.play.channels) ?
                  ainfo.play.channels : curinfo.play.channels;

		switch (newinfo.play.encoding) {
		case AUDIO_ENCODING_ALAW:
		case AUDIO_ENCODING_ULAW:
                  if (newinfo.play.precision != 8) {
                    retval = -EINVAL;
                    break;
                  }
                  if (newinfo.play.channels != 1) {
                    retval = -EINVAL;
                    break;
                  }
                  break;
                case AUDIO_ENCODING_LINEAR:
                case AUDIO_ENCODING_LINEARLE:
                  if (newinfo.play.precision != 16) {
                    retval = -EINVAL;
                    break;
                  }
                  if (newinfo.play.channels != 1 &&
                       newinfo.play.channels != 2)
                    {
                      retval = -EINVAL;
                      break;
                    }
                  break;
                case AUDIO_ENCODING_LINEAR8:
                  if (newinfo.play.precision != 8) {
                    retval = -EINVAL;
                    break;
                  }
                  if (newinfo.play.channels != 1 &&
                       newinfo.play.channels != 2)
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
            /* Maybe this should be a routine instead of a macro */
#define IF_SET_DO(x,y) if ((x) && Modify(y)) x(driver, y)
#define IF_SETC_DO(x,y) if ((x) && Modifyc(y)) x(driver, y)
            IF_SETC_DO(driver->ops->set_input_balance, (int)ainfo.record.balance);
            IF_SETC_DO(driver->ops->set_output_balance, (int)ainfo.play.balance);
            IF_SET_DO(driver->ops->set_input_volume, ainfo.record.gain);
            IF_SET_DO(driver->ops->set_output_volume, ainfo.play.gain);
            IF_SET_DO(driver->ops->set_input_port, ainfo.record.port);
            IF_SET_DO(driver->ops->set_output_port, ainfo.play.port);
            IF_SET_DO(driver->ops->set_monitor_volume, ainfo.monitor_gain);
            IF_SETC_DO(driver->ops->set_output_muted, (int)ainfo.output_muted);
#undef IF_SET_DO
#undef IF_SETC_DO

	    break;
	  }

	default:
		if (driver->ops->ioctl)
			retval = driver->ops->ioctl(inode,file,cmd,arg,driver);
		else {
			retval = -EINVAL;
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
	NULL,			/* flush */
	sparcaudioctl_release
};

static int sparcaudio_open(struct inode * inode, struct file * file)
{
    int minor = MINOR(inode->i_rdev);
    int err;

    /* A low-level audio driver must exist. */
    if (!driver)
        return -ENODEV;

    switch (minor) {
    case SPARCAUDIO_AUDIOCTL_MINOR:
        file->f_op = &sparcaudioctl_fops;
        break;

    case SPARCAUDIO_DSP16_MINOR:
    case SPARCAUDIO_DSP_MINOR:
    case SPARCAUDIO_AUDIO_MINOR:
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

	/* Allow the low-level driver to initialize itself. */
	if (driver->ops->open) {
            err = driver->ops->open(inode,file,driver);
            if (err < 0)
                return err;
	}
        
	/* Mark the driver as locked for read and/or write. */
	if (file->f_mode & FMODE_READ) {
            driver->input_offset = 0;
            driver->input_front = 0;
            driver->input_rear = 0;
            driver->input_count = 0;
            driver->recording_count = 0;
            driver->ops->start_input(driver, driver->input_buffers[driver->input_front],
                                     4096);
            driver->input_active = 1;
            driver->flags |= SDF_OPEN_READ;
	}
	if (file->f_mode & FMODE_WRITE) {
            driver->playing_count = 0;
            driver->output_size = 0;
            driver->output_front = 0;
            driver->output_rear = 0;
            driver->output_count = 0;
            driver->output_active = 0;
            driver->flags |= SDF_OPEN_WRITE;
	}  
        break;
    case SPARCAUDIO_MIXER_MINOR:     
        file->f_op = &sparcaudioctl_fops;
        break;

    default:
        return -ENXIO;
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

#ifdef CONFIG_SPARCAUDIO_DBRI
	dbri_init();
#endif

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	unregister_chrdev(SOUND_MAJOR, "sparcaudio");
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
