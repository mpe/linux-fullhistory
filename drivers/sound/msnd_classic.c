/*********************************************************************
 *
 * msnd_classic.c - Support for Turtle Beach Classic/Monterey/Tahiti
 *
 * Turtle Beach MultiSound Sound Card Driver for Linux
 *
 * Copyright (C) 1998 Andrew Veliath
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: msnd_classic.c,v 1.2 1998/06/09 20:37:39 andrewtv Exp $
 *
 ********************************************************************/

#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include "sound_config.h"
#include "sound_firmware.h"
#define SLOWIO
#include "msnd.h"
#include "msnd_classic.h"

#define LOGNAME				"msnd_classic"
#define DEVNAME				dev.name
#define MIXERMINOR			dev.mixer_minor
#define DSPMINOR			dev.dsp_minor

multisound_dev_t			dev;

#ifndef HAVE_DSPCODEH
static char				*dspini, *permini;
static int				sizeof_dspini, sizeof_permini;
#endif

static void reset_play_queue(void)
{
	int n;
	LPDAQD lpDAQ;

	msnd_fifo_make_empty(&dev.DAPF);
	dev.DAPQ->wHead = 0;
	dev.DAPQ->wTail = PCTODSP_OFFSET(2 * DAPQ_STRUCT_SIZE);
	dev.CurDAQD = (LPDAQD)(dev.base + 1 * DAPQ_DATA_BUFF);
	outb(HPBLKSEL_0, dev.io + HP_BLKS);
	memset_io(dev.base, 0, DAP_BUFF_SIZE * 3);

	for (n = 0, lpDAQ = dev.CurDAQD; n < 3; ++n, ++lpDAQ) {
               
		writew(PCTODSP_BASED((DWORD)(DAP_BUFF_SIZE * n)), &lpDAQ->wStart);
		writew(DAP_BUFF_SIZE, &lpDAQ->wSize);
		writew(1, &lpDAQ->wFormat);
		writew(dev.sample_size, &lpDAQ->wSampleSize);
		writew(dev.channels, &lpDAQ->wChannels);
		writew(dev.sample_rate, &lpDAQ->wSampleRate);
		writew(HIMT_PLAY_DONE * 0x100 + n, &lpDAQ->wIntMsg);
		writew(n + 1, &lpDAQ->wFlags);

	}

	dev.lastbank = -1;
}

static void reset_record_queue(void)
{
	int n;
	LPDAQD lpDAQ;

	msnd_fifo_make_empty(&dev.DARF);
	dev.DARQ->wHead = 0;
	dev.DARQ->wTail = PCTODSP_OFFSET(2 * DARQ_STRUCT_SIZE);
	dev.CurDARQD = (LPDAQD)(dev.base + 1 * DARQ_DATA_BUFF);
	outb(HPBLKSEL_1, dev.io + HP_BLKS);
	memset_io(dev.base, 0, DAR_BUFF_SIZE * 3);
	outb(HPBLKSEL_0, dev.io + HP_BLKS);

	for (n = 0, lpDAQ = dev.CurDARQD; n < 3; ++n, ++lpDAQ) {

		writew(PCTODSP_BASED((DWORD)(DAR_BUFF_SIZE * n)) + 0x4000, &lpDAQ->wStart);
		writew(DAR_BUFF_SIZE, &lpDAQ->wSize);
		writew(1, &lpDAQ->wFormat);
		writew(dev.sample_size, &lpDAQ->wSampleSize);
		writew(dev.channels, &lpDAQ->wChannels);
		writew(dev.sample_rate, &lpDAQ->wSampleRate);
		writew(HIMT_RECORD_DONE * 0x100 + n, &lpDAQ->wIntMsg);
		writew(n + 1, &lpDAQ->wFlags);

	}
}

static void reset_queues(void)
{
	dev.DSPQ->wHead = dev.DSPQ->wTail = 0;
	reset_play_queue();
	reset_record_queue();
}

static int dsp_ioctl(unsigned int cmd, unsigned long arg)
{
	int val, i, data;
	LPDAQD lpDAQ, lpDARQ;

	lpDAQ = (LPDAQD)(dev.base + DAPQ_DATA_BUFF);
	lpDARQ = (LPDAQD)(dev.base + DARQ_DATA_BUFF);

	switch (cmd) {
	case SNDCTL_DSP_SUBDIVIDE:
	case SNDCTL_DSP_SETFRAGMENT:
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETIPTR:
	case SNDCTL_DSP_GETOPTR:
	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
		return -EINVAL;

	case SNDCTL_DSP_SYNC:
	case SNDCTL_DSP_RESET:

		reset_play_queue();
		reset_record_queue();

		return 0;
		
	case SNDCTL_DSP_GETBLKSIZE:

		if (put_user(dev.fifosize / 4, (int *)arg))
			return -EFAULT;

		return 0;

	case SNDCTL_DSP_NONBLOCK:

		dev.mode |= O_NONBLOCK;

		return 0;

	case SNDCTL_DSP_GETCAPS:

		val = DSP_CAP_DUPLEX | DSP_CAP_BATCH;
		if (put_user(val, (int *)arg))
			return -EFAULT;

		return 0;

	case SNDCTL_DSP_SAMPLESIZE:
			
		if (get_user(val, (int *)arg))
			return -EFAULT;

		switch (val) {
		case 16:
		case 8:
			data = val;
			break;
		default:
			data = DEFSAMPLESIZE;
			break;
		}

		for (i = 0; i < 3; ++i, ++lpDAQ, ++lpDARQ) {

			lpDAQ->wSampleSize = data;
			lpDARQ->wSampleSize = data;
		}
		
		dev.sample_size = data;
		
		if (put_user(data, (int *)arg))
			return -EFAULT;

		return 0;

	case SNDCTL_DSP_SPEED:
			
		if (get_user(val, (int *)arg))
			return -EFAULT;

		if (val < 8000)
			val = 8000;

		if (val > 48000)
			val = 48000;

		data = val;

		for (i = 0; i < 3; ++i, ++lpDAQ, ++lpDARQ) {

			lpDAQ->wSampleRate = data;
			lpDARQ->wSampleRate = data;
		}
		
		dev.sample_rate = data;

		if (put_user(data, (int *)arg))
			return -EFAULT;

		return 0;

	case SNDCTL_DSP_CHANNELS:
			
		if (get_user(val, (int *)arg))
			return -EFAULT;
			
		switch (val) {
		case 1:
		case 2:
			data = val;
			break;
		default:
			val = data = 2;
			break;
		}
									
		for (i = 0; i < 3; ++i, ++lpDAQ, ++lpDARQ) {

			lpDAQ->wChannels = data;
			lpDARQ->wChannels = data;
		}

		dev.channels = data;

		if (put_user(val, (int *)arg))
			return -EFAULT;

		return 0;

	case SNDCTL_DSP_STEREO:
			
		if (get_user(val, (int *)arg))
			return -EFAULT;
			
		switch (val) {
		case 0:
			data = 1;
			break;
		default:
			val = 1;
		case 1:
			data = 2;
			break;
		}
									
		for (i = 0; i < 3; ++i, ++lpDAQ, ++lpDARQ) {

			lpDAQ->wChannels = data;
			lpDARQ->wChannels = data;
		}

		dev.channels = data;

		if (put_user(val, (int *)arg))
			return -EFAULT;

		return 0;
	}

	return -EINVAL;
}

static int mixer_get(int d)
{
	if (d > 31)
		return -EINVAL;

	switch (d) {
	case SOUND_MIXER_VOLUME:
	case SOUND_MIXER_SYNTH:
	case SOUND_MIXER_PCM:
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_MIC:
	case SOUND_MIXER_IMIX:
	case SOUND_MIXER_LINE1:
		return (dev.left_levels[d] >> 8) * 100 / 0xff | 
			(((dev.right_levels[d] >> 8) * 100 / 0xff) << 8);
	default:
		return 0;
	}
}

#define update_vol(a,b,s)								\
	writew(dev.left_levels[a] * readw(&dev.SMA->wCurrMastVolLeft) / 0xffff / s,	\
	       &dev.SMA->b##Left);							\
	writew(dev.right_levels[a] * readw(&dev.SMA->wCurrMastVolRight) / 0xffff / s,	\
	       &dev.SMA->b##Right);

static int mixer_set(int d, int value)
{
	int left = value & 0x000000ff;
	int right = (value & 0x0000ff00) >> 8;
	int bLeft, bRight;
	int wLeft, wRight;

	if (d > 31)
		return -EINVAL;

	bLeft = left * 0xff / 100;
	wLeft = left * 0xffff / 100;

	bRight = right * 0xff / 100;
	wRight = right * 0xffff / 100;

	dev.left_levels[d] = wLeft;
	dev.right_levels[d] = wRight;

	switch (d) {
	case SOUND_MIXER_VOLUME:		/* master volume */
		writew(wLeft / 2, &dev.SMA->wCurrMastVolLeft);
		writew(wRight / 2, &dev.SMA->wCurrMastVolRight);
		break;

		/* pot controls */
	case SOUND_MIXER_LINE:			/* aux pot control */
		writeb(bLeft, &dev.SMA->bInPotPosLeft);
		writeb(bRight, &dev.SMA->bInPotPosRight);
		if (msnd_send_word(&dev, 0, 0, HDEXAR_IN_SET_POTS) == 0)
			msnd_send_dsp_cmd(&dev, HDEX_AUX_REQ);
		break;

	case SOUND_MIXER_LINE1:			/* line pot control */
		writeb(bLeft, &dev.SMA->bAuxPotPosLeft);
		writeb(bRight, &dev.SMA->bAuxPotPosRight);
		if (msnd_send_word(&dev, 0, 0, HDEXAR_AUX_SET_POTS) == 0)
			msnd_send_dsp_cmd(&dev, HDEX_AUX_REQ);
		break;

		/* digital controls */
	case SOUND_MIXER_SYNTH:			/* synth vol (dsp mix) */
	case SOUND_MIXER_PCM:			/* pcm vol (dsp mix) */
	case SOUND_MIXER_IMIX:			/* input monitor (dsp mix) */
		break;

	default:
		return 0;
	}

	/* update digital controls for master volume */
	update_vol(SOUND_MIXER_PCM, wCurrPlayVol, 1);
	update_vol(SOUND_MIXER_IMIX, wCurrInVol, 1);
	
	return mixer_get(d);
}

static unsigned long set_recsrc(unsigned long recsrc)
{
#ifdef HAVE_NORECSRC
	if (recsrc == 0)
		dev.recsrc = 0;
	else
#endif
		dev.recsrc ^= recsrc;

	return dev.recsrc;
}

static int mixer_ioctl(unsigned int cmd, unsigned long arg)
{
	int val = 0;

	if (((cmd >> 8) & 0xff) == 'M') {
		
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) {

			switch (cmd & 0xff) {
			case SOUND_MIXER_RECSRC:
				if (get_user(val, (int *)arg))
					return -EFAULT;
				val = set_recsrc(val);
				break;
				
			default:
				if (get_user(val, (int *)arg))
					return -EFAULT;
				val = mixer_set(cmd & 0xff, val);
				break;
			}

			return put_user(val, (int *)arg);
		}
		else {
			switch (cmd & 0xff) {
			case SOUND_MIXER_RECSRC:
				val = dev.recsrc;
				break;
				
			case SOUND_MIXER_DEVMASK:
			case SOUND_MIXER_STEREODEVS:
				val =   SOUND_MASK_VOLUME |
					SOUND_MASK_PCM |
					SOUND_MASK_LINE |
					SOUND_MASK_IMIX |
					SOUND_MASK_LINE1;
				break;
				  
			case SOUND_MIXER_RECMASK:
				val = 0;
				break;
				  
			case SOUND_MIXER_CAPS:
				val =   SOUND_CAP_EXCL_INPUT;
				break;
				
			default:
				if ((val = mixer_get(cmd & 0xff)) < 0)
					return -EINVAL;
				break;
			}
		}

		return put_user(val, (int *)arg); 
	}

	return -EINVAL;
}

static int dev_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int minor = MINOR(inode->i_rdev);

	if (minor == DSPMINOR)
		return dsp_ioctl(cmd, arg);
	else if (minor == MIXERMINOR)
		return mixer_ioctl(cmd, arg);

	return -EINVAL;
}

static void dsp_halt(void)
{
	mdelay(1);
	if (test_and_clear_bit(F_READING, &dev.flags)) {

		msnd_send_dsp_cmd(&dev, HDEX_RECORD_STOP);
		msnd_disable_irq(&dev);

	}
	mdelay(1);
	if (test_and_clear_bit(F_WRITING, &dev.flags)) {

		msnd_send_dsp_cmd(&dev, HDEX_PLAY_STOP);
		msnd_disable_irq(&dev);

	}
	mdelay(1);
	reset_queues();
}

static int dsp_open(struct file *file)
{
	dev.mode = file->f_mode;
	set_bit(F_AUDIO_INUSE, &dev.flags);
	reset_queues();
	return 0;
}

static int dsp_close(void)
{
	dsp_halt();
	clear_bit(F_AUDIO_INUSE, &dev.flags);
	return 0;
}

static int dev_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	int err = 0;

	if (minor == DSPMINOR) {

		if (test_bit(F_AUDIO_INUSE, &dev.flags))
			return -EBUSY;

		err = dsp_open(file);
	}
	else if (minor == MIXERMINOR) {
		/* nothing */
	} else
		err = -EINVAL;
	
	if (err >= 0)
		MOD_INC_USE_COUNT;

	return err;
}

static int dev_close(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	int err = 0;

	if (minor == DSPMINOR) {
		err = dsp_close();
	}
	else if (minor == MIXERMINOR) {
		/* nothing */
	} else
		err = -EINVAL;

	if (err >= 0)
		MOD_DEC_USE_COUNT;
	
	return err;
}

static int DAPF_to_bank(int bank)
{
	return msnd_fifo_read(&dev.DAPF, dev.base + bank * DAP_BUFF_SIZE, DAP_BUFF_SIZE, 0);
}

static int bank_to_DARF(int bank)
{
	return msnd_fifo_write(&dev.DARF, dev.base + bank * DAR_BUFF_SIZE, DAR_BUFF_SIZE, 0);
}

static int dsp_read(char *buf, size_t len)
{
	int err = 0;
	int count = len;

	while (count > 0) {
		
		int n;

		if ((n = msnd_fifo_read(&dev.DARF, buf, count, 1)) < 0) {

			printk(KERN_WARNING LOGNAME ": FIFO read error\n");
			return n;
		}

		buf += n;
		count -= n;
		
		if (!test_and_set_bit(F_READING, &dev.flags) && (dev.mode & FMODE_READ)) {

			reset_record_queue();
			msnd_enable_irq(&dev);
			msnd_send_dsp_cmd(&dev, HDEX_RECORD_START);

		}

		if (dev.mode & O_NONBLOCK)
			return count == len ? -EAGAIN : len - count;

		if (count > 0) {

			set_bit(F_READBLOCK, &dev.flags);
			interruptible_sleep_on(&dev.readblock);
			clear_bit(F_READBLOCK, &dev.flags);

			if (signal_pending(current))
				err = -EINTR;

		}

		if (err != 0)
			return err;
	}

	return len - count;
}

static int dsp_write(const char *buf, size_t len)
{
	int err = 0;
	int count = len;

	while (count > 0) {

		int n;

		if ((n = msnd_fifo_write(&dev.DAPF, buf, count, 1)) < 0) {

			printk(KERN_WARNING LOGNAME ": FIFO write error\n");
			return n;
		}

		buf += n;
		count -= n;

		if (!test_and_set_bit(F_WRITING, &dev.flags) && (dev.mode & FMODE_WRITE)) {
			
			reset_play_queue();
			msnd_enable_irq(&dev);
			msnd_send_dsp_cmd(&dev, HDEX_PLAY_START);

		}

		if (dev.mode & O_NONBLOCK)
			return count == len ? -EAGAIN : len - count;

		if (count > 0) {
			
			set_bit(F_WRITEBLOCK, &dev.flags);
			interruptible_sleep_on(&dev.writeblock);
			clear_bit(F_WRITEBLOCK, &dev.flags);

			if (signal_pending(current))
				err = -EINTR;

		}

		if (err != 0)
			return err;
	}
	
	return len - count;
}

static ssize_t dev_read(struct file *file, char *buf, size_t count, loff_t *off)
{
	int minor = MINOR(file->f_dentry->d_inode->i_rdev);

	if (minor == DSPMINOR) {

		return dsp_read(buf, count);

	} else
		return -EINVAL;
}

static ssize_t dev_write(struct file *file, const char *buf, size_t count, loff_t *off)
{
	int minor = MINOR(file->f_dentry->d_inode->i_rdev);

	if (minor == DSPMINOR) {

		return dsp_write(buf, count);

	} else
		return -EINVAL;
}

static void eval_dsp_msg(WORD wMessage)
{
	switch (HIBYTE(wMessage)) {
	case HIMT_PLAY_DONE:
		
		if (dev.lastbank == LOBYTE(wMessage))
			break;
		
		dev.lastbank = LOBYTE(wMessage);

		dev.CurDAQD->wSize = DAP_BUFF_SIZE;

		if ((dev.DAPQ->wTail += PCTODSP_OFFSET(DAPQ_STRUCT_SIZE)) > dev.DAPQ->wSize)
			dev.DAPQ->wTail = 0;

		if (++dev.CurDAQD > (LPDAQD)(dev.base + DAPQ_DATA_BUFF + 2 * DAPQ_STRUCT_SIZE))
			dev.CurDAQD = (LPDAQD)(dev.base + DAPQ_DATA_BUFF);

		if (dev.lastbank < 3) {

			if (DAPF_to_bank(dev.lastbank) > 0) {

				mdelay(1);
				msnd_send_dsp_cmd(&dev, HDEX_PLAY_START);

			} 
			else if (!test_bit(F_WRITEBLOCK, &dev.flags)) {

				memset_io(dev.base, 0, DAP_BUFF_SIZE * 3);
				clear_bit(F_WRITING, &dev.flags);
				msnd_disable_irq(&dev);

			}
		}

		if (test_bit(F_WRITEBLOCK, &dev.flags))
			wake_up_interruptible(&dev.writeblock);
		
		break;

	case HIMT_RECORD_DONE: {

		WORD wTemp;
		
		wTemp = dev.DARQ->wTail + (DARQ_STRUCT_SIZE / 2);

		if (wTemp > dev.DARQ->wSize)
			wTemp = 0;

		while (wTemp == dev.DARQ->wHead);

		dev.DARQ->wTail = wTemp;

		outb(HPBLKSEL_1, dev.io + HP_BLKS);
		if (bank_to_DARF(LOBYTE(wMessage)) == 0 &&
		    !test_bit(F_READBLOCK, &dev.flags)) {

			memset_io(dev.base, 0, DAR_BUFF_SIZE * 3);
			clear_bit(F_READING, &dev.flags);
			msnd_disable_irq(&dev);

		}
		outb(HPBLKSEL_0, dev.io + HP_BLKS);

		if (test_bit(F_READBLOCK, &dev.flags))
			wake_up_interruptible(&dev.readblock);

		} break;

	case HIMT_DSP:
		switch (LOBYTE(wMessage)) {
		case HIDSP_INT_PLAY_UNDER:
			printk(KERN_INFO LOGNAME ": Write underflow\n");
			reset_play_queue();
			break;

		case HIDSP_INT_RECORD_OVER:
			printk(KERN_INFO LOGNAME ": Read overflow\n");
			reset_record_queue();
			break;

		default:
			printk(KERN_INFO LOGNAME ": DSP message %u\n", LOBYTE(wMessage));
			break;
		}
		break;

        case HIMT_MIDI_IN_UCHAR:
		if (dev.midi_in_interrupt)
			(*dev.midi_in_interrupt)(&dev);
		break;

	default:
		break;
	}
}

static void intr(int irq, void *dev_id, struct pt_regs *regs)
{
	if (test_bit(F_INTERRUPT, &dev.flags) || 
	    ((multisound_dev_t *)dev_id != &dev))
		return;

	set_bit(F_INTERRUPT, &dev.flags);
	
	if (test_bit(F_BANKONE, &dev.flags))
		outb(HPBLKSEL_0, dev.io + HP_BLKS);

	inb(dev.io + HP_RXL);
 
	while (dev.DSPQ->wTail != dev.DSPQ->wHead) {

		eval_dsp_msg(*(dev.pwDSPQData + dev.DSPQ->wHead));

		if (++dev.DSPQ->wHead > dev.DSPQ->wSize)
			dev.DSPQ->wHead = 0;
	}

	if (test_bit(F_BANKONE, &dev.flags))
		outb(HPBLKSEL_1, dev.io + HP_BLKS);

	clear_bit(F_INTERRUPT, &dev.flags);
}

static struct file_operations dev_fileops = {
	NULL,
	dev_read,
	dev_write,
	NULL,
	NULL,
	dev_ioctl,
	NULL,
	dev_open,
	dev_close,
};

__initfunc(static int reset_dsp(void))
{
	int timeout = 20000;
		
	outb(HPDSPRESET_ON, dev.io + HP_DSPR);
	
	mdelay(1);

	dev.info = inb(dev.io + HP_INFO);

	outb(HPDSPRESET_OFF, dev.io + HP_DSPR);

	mdelay(1);

	while (timeout-- > 0) {

		if (inb(dev.io + HP_CVR) == HP_CVR_DEF)
			return 0;
		
		mdelay(1);
	}

	printk(KERN_ERR LOGNAME ": Cannot reset DSP\n");

	return -EIO;
}

__initfunc(static int probe_multisound(void))
{
	if (check_region(dev.io, dev.numio)) {
		
		printk(KERN_ERR LOGNAME ": I/O port conflict\n");
		return -ENODEV;
	}

	request_region(dev.io, dev.numio, "probing");

	if (reset_dsp() < 0) {
		
		release_region(dev.io, dev.numio);
		return -ENODEV;
	}

	printk(KERN_INFO LOGNAME ": DSP reset successful\n");

	dev.name = "Classic/Tahiti/Monterey";

	printk(KERN_INFO LOGNAME ": Turtle Beach %s, "
	       "I/O 0x%x-0x%x, IRQ %d, memory mapped to 0x%p-0x%p\n",
	       dev.name,
	       dev.io, dev.io + dev.numio - 1,
	       dev.irq,
	       dev.base, dev.base + 0x7fff);

	release_region(dev.io, dev.numio);
	
	return 0;
}

__initfunc(static int init_sma(void))
{
	int n;
	LPDAQD lpDAQ;

	outb(dev.memid, dev.io + HP_MEMM);

	outb(HPBLKSEL_0, dev.io + HP_BLKS);
	memset_io(dev.base, 0, 0x8000);
	
	outb(HPBLKSEL_1, dev.io + HP_BLKS);
	memset_io(dev.base, 0, 0x8000);
	
	outb(HPBLKSEL_0, dev.io + HP_BLKS);

	dev.DAPQ = (struct JobQueueStruct *)(dev.base + DAPQ_OFFSET);
	dev.DARQ = (struct JobQueueStruct *)(dev.base + DARQ_OFFSET);
	dev.MODQ = (struct JobQueueStruct *)(dev.base + MODQ_OFFSET);
	dev.MIDQ = (struct JobQueueStruct *)(dev.base + MIDQ_OFFSET);
	dev.DSPQ = (struct JobQueueStruct *)(dev.base + DSPQ_OFFSET);

	dev.SMA = (struct SMA0_CommonData *)(dev.base + SMA_STRUCT_START);

	dev.CurDAQD = (LPDAQD)(dev.base + DAPQ_DATA_BUFF);
	dev.CurDARQD = (LPDAQD)(dev.base + DARQ_DATA_BUFF);

	dev.sample_size = DEFSAMPLESIZE;
	dev.sample_rate = DEFSAMPLERATE;
	dev.channels = DEFCHANNELS;

	for (n = 0, lpDAQ = dev.CurDAQD; n < 3; ++n, ++lpDAQ) {
		
		writew(PCTODSP_BASED((DWORD)(DAP_BUFF_SIZE * n)), &lpDAQ->wStart);
		writew(DAP_BUFF_SIZE, &lpDAQ->wSize);
		writew(1, &lpDAQ->wFormat);
		writew(dev.sample_size, &lpDAQ->wSampleSize);
		writew(dev.channels, &lpDAQ->wChannels);
		writew(dev.sample_rate, &lpDAQ->wSampleRate);
		writew(HIMT_PLAY_DONE * 0x100 + n, &lpDAQ->wIntMsg);
		writew(n + 1, &lpDAQ->wFlags);
	}

	for (n = 0, lpDAQ = dev.CurDARQD; n < 3; ++n, ++lpDAQ) {

		writew(PCTODSP_BASED((DWORD)(DAR_BUFF_SIZE * n)) + 0x4000, &lpDAQ->wStart);
		writew(DAR_BUFF_SIZE, &lpDAQ->wSize);
		writew(1, &lpDAQ->wFormat);
		writew(dev.sample_size, &lpDAQ->wSampleSize);
		writew(dev.channels, &lpDAQ->wChannels);
		writew(dev.sample_rate, &lpDAQ->wSampleRate);
		writew(HIMT_RECORD_DONE * 0x100 + n, &lpDAQ->wIntMsg);
		writew(n + 1, &lpDAQ->wFlags);

	}	

	dev.pwDSPQData = (WORD *)(dev.base + DSPQ_DATA_BUFF);
	dev.pwMODQData = (WORD *)(dev.base + MODQ_DATA_BUFF);
	dev.pwMIDQData = (WORD *)(dev.base + MIDQ_DATA_BUFF);

	writew(PCTODSP_BASED(MIDQ_DATA_BUFF), &dev.MIDQ->wStart);
	writew(PCTODSP_OFFSET(MIDQ_BUFF_SIZE) - 1, &dev.MIDQ->wSize);
	writew(0, &dev.MIDQ->wHead);
	writew(0, &dev.MIDQ->wTail);

	writew(PCTODSP_BASED(MODQ_DATA_BUFF), &dev.MODQ->wStart);
	writew(PCTODSP_OFFSET(MODQ_BUFF_SIZE) - 1, &dev.MODQ->wSize);
	writew(0, &dev.MODQ->wHead);
	writew(0, &dev.MODQ->wTail);

	writew(PCTODSP_BASED(DAPQ_DATA_BUFF), &dev.DAPQ->wStart);
	writew(PCTODSP_OFFSET(DAPQ_BUFF_SIZE) - 1, &dev.DAPQ->wSize);
	writew(0, &dev.DAPQ->wHead);
	writew(0, &dev.DAPQ->wTail);

	writew(PCTODSP_BASED(DARQ_DATA_BUFF), &dev.DARQ->wStart);
	writew(PCTODSP_OFFSET(DARQ_BUFF_SIZE) - 1, &dev.DARQ->wSize);
	writew(0, &dev.DARQ->wHead);
	writew(0, &dev.DARQ->wTail);

	writew(PCTODSP_BASED(DSPQ_DATA_BUFF), &dev.DSPQ->wStart);
	writew(PCTODSP_OFFSET(DSPQ_BUFF_SIZE) - 1, &dev.DSPQ->wSize);
	writew(0, &dev.DSPQ->wHead);
	writew(0, &dev.DSPQ->wTail);

	writew(0, &dev.SMA->wCurrPlayBytes);
	writew(0, &dev.SMA->wCurrRecordBytes);

	writew(0, &dev.SMA->wCurrPlayVolLeft);
	writew(0, &dev.SMA->wCurrPlayVolRight);

	writew(0, &dev.SMA->wCurrInVolLeft);
	writew(0, &dev.SMA->wCurrInVolRight);

	writew(0, &dev.SMA->wCurrMastVolLeft);
	writew(0, &dev.SMA->wCurrMastVolRight);

	writew(0x0000, &dev.SMA->wCurrDSPStatusFlags);
	writew(0x0000, &dev.SMA->wCurrHostStatusFlags);

	writew(0x303, &dev.SMA->wCurrInputTagBits);
	writew(0, &dev.SMA->wCurrLeftPeak);
	writew(0, &dev.SMA->wCurrRightPeak);

	writeb(0, &dev.SMA->bInPotPosRight);
	writeb(0, &dev.SMA->bInPotPosLeft);

	writeb(0, &dev.SMA->bAuxPotPosRight);
	writeb(0, &dev.SMA->bAuxPotPosLeft);

	writew(dev.sample_rate, &dev.SMA->wCalFreqAtoD);

	return 0;
}

__initfunc(static int calibrate_adc(WORD srate))
{
	if (!dev.calibrate_signal) {

		printk(KERN_INFO LOGNAME ": ADC calibration to board ground ");
		writew(readw(&dev.SMA->wCurrHostStatusFlags)
		       | 0x0001, &dev.SMA->wCurrHostStatusFlags);
	}
	else {

		printk(KERN_INFO LOGNAME ": ADC calibration to signal ground ");
		writew(readw(&dev.SMA->wCurrHostStatusFlags)
		       & ~0x0001, &dev.SMA->wCurrHostStatusFlags);
	}
	
	writew(srate, &dev.SMA->wCalFreqAtoD);

	if (msnd_send_word(&dev, 0, 0, HDEXAR_CAL_A_TO_D) == 0 &&
	    msnd_send_dsp_cmd(&dev, HDEX_AUX_REQ) == 0) {

		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + HZ;
		schedule();
		current->timeout = 0;
		printk("successful\n");
		return 0;
	}

	printk("failed\n");

	return -EIO;
}

__initfunc(static int upload_dsp_code(void))
{
	outb(HPBLKSEL_0, dev.io + HP_BLKS);

#ifdef HAVE_DSPCODEH
	printk(KERN_INFO LOGNAME ": Using resident Turtle Beach DSP code\n");
#else	
	printk(KERN_INFO LOGNAME ": Loading Turtle Beach DSP code\n");
	INITCODESIZE = mod_firmware_load(INITCODEFILE, &INITCODE);
	if (!INITCODE) {
		printk(KERN_ERR LOGNAME ": Error loading " INITCODEFILE);
		return -EBUSY;
	}

	PERMCODESIZE = mod_firmware_load(PERMCODEFILE, &PERMCODE);
	if (!PERMCODE) {
		printk(KERN_ERR LOGNAME ": Error loading " PERMCODEFILE);
		vfree(INITCODE);
		return -EBUSY;
	}
#endif
	memcpy_toio(dev.base, PERMCODE, PERMCODESIZE);

	if (msnd_upload_host(&dev, INITCODE, INITCODESIZE) < 0) {

		printk(KERN_WARNING LOGNAME ": Error uploading to DSP\n");
		return -ENODEV;
	}

#ifndef HAVE_DSPCODEH
	vfree(INITCODE);
	vfree(PERMCODE);
#endif

	return 0;
}

__initfunc(static void reset_proteus(void))
{
	outb(HPPRORESET_ON, dev.io + HP_PROR);
	mdelay(TIME_PRO_RESET);
	outb(HPPRORESET_OFF, dev.io + HP_PROR);
	mdelay(TIME_PRO_RESET_DONE);
}

__initfunc(static int initialize(void))
{
	int err, timeout;

	outb(HPWAITSTATE_0, dev.io + HP_WAIT);
	outb(HPBITMODE_16, dev.io + HP_BITM);

	reset_proteus();

	if ((err = init_sma()) < 0) {

		printk(KERN_WARNING LOGNAME ": Cannot initialize SMA\n");
		return err;
	}

	if ((err = reset_dsp()) < 0)
		return err;
	
	if ((err = upload_dsp_code()) < 0) {

		printk(KERN_WARNING LOGNAME ": Cannot upload DSP code\n");
		return err;

	} else
		printk(KERN_INFO LOGNAME ": DSP upload successful\n");

	timeout = 2000;

	while (readw(dev.base)) {
		
		mdelay(1);
		if (--timeout < 0)
			return -EIO;
	}

	return 0;
}

__initfunc(static int attach_multisound(void))
{
	int err;

	printk(KERN_DEBUG LOGNAME ": Intializing DSP\n");

	if ((err = request_irq(dev.irq, intr, SA_SHIRQ, DEVNAME, &dev)) < 0) {
		
		printk(KERN_ERR LOGNAME ": Couldn't grab IRQ %d\n", dev.irq);
		return err;
	
	}

	request_region(dev.io, dev.numio, DEVNAME);

        if ((err = initialize()) < 0) {

		printk(KERN_WARNING LOGNAME ": Initialization failure\n");
		release_region(dev.io, dev.numio);
		free_irq(dev.irq, &dev);
		return err;

	}

	if ((err = msnd_register(&dev)) < 0) {

		printk(KERN_ERR LOGNAME ": Unable to register MultiSound\n");
		release_region(dev.io, dev.numio);
		free_irq(dev.irq, &dev);
		return err;
	}

	if ((DSPMINOR = register_sound_dsp(&dev_fileops)) < 0) {

		printk(KERN_ERR LOGNAME ": Unable to register DSP operations\n");
		msnd_unregister(&dev);
		release_region(dev.io, dev.numio);
		free_irq(dev.irq, &dev);
		return DSPMINOR;
	}

	if ((MIXERMINOR = register_sound_mixer(&dev_fileops)) < 0) {

		printk(KERN_ERR LOGNAME ": Unable to register mixer operations\n");
		unregister_sound_mixer(MIXERMINOR);
		msnd_unregister(&dev);
		release_region(dev.io, dev.numio);
		free_irq(dev.irq, &dev);
		return MIXERMINOR;
	}
	printk(KERN_INFO LOGNAME ": Using DSP minor %d, mixer minor %d\n", MIXERMINOR, DSPMINOR);

	calibrate_adc(dev.sample_rate);
	set_recsrc(0);
	
	return 0;
}

static void unload_multisound(void)
{
	release_region(dev.io, dev.numio);
	free_irq(dev.irq, &dev);
	unregister_sound_mixer(MIXERMINOR);
	unregister_sound_dsp(DSPMINOR);
	msnd_unregister(&dev);
}

static void mod_inc_ref(void)
{
	MOD_INC_USE_COUNT;
}

static void mod_dec_ref(void)
{
	MOD_DEC_USE_COUNT;
}

#ifdef MODULE
MODULE_AUTHOR				("Andrew Veliath <andrewtv@usa.net>");
MODULE_DESCRIPTION			("Turtle Beach " LONGNAME " Linux Driver");
MODULE_PARM				(io, "i");
MODULE_PARM				(irq, "i");
MODULE_PARM				(mem, "i");
MODULE_PARM				(major, "i");
MODULE_PARM				(fifosize, "i");
MODULE_PARM				(calibrate_signal, "i");

static int io __initdata =		-1;
static int irq __initdata =		-1;
static int mem __initdata =		-1;
static int fifosize __initdata =	DEFFIFOSIZE;
static int calibrate_signal __initdata;

int init_module(void)
#else
static int io __initdata =		CONFIG_MSNDCLAS_IO;
static int irq __initdata =		CONFIG_MSNDCLAS_IRQ;
static int mem __initdata =		CONFIG_MSNDCLAS_MEM;
static int fifosize __initdata =	DEFFIFOSIZE;
static int calibrate_signal __initdata;

__initfunc(msnd_classic_init(void))
#endif
{
	int err;

	printk(KERN_INFO LOGNAME ": Turtle Beach " LONGNAME " Linux Driver Version "
	       VERSION ", Copyright (C) 1998 Andrew Veliath\n");
	
	if (io == -1 || irq == -1 || mem == -1) {

		printk(KERN_WARNING LOGNAME ": io, irq and mem must be set\n");
	}
		
	if (io == -1 ||
	    !(io == 0x290 ||
	      io == 0x260 ||
	      io == 0x250 ||
	      io == 0x240 ||
	      io == 0x230 ||
	      io == 0x220 ||
	      io == 0x210 ||
	      io == 0x3e0)) {

		printk(KERN_ERR LOGNAME ": \"io\" - DSP I/O base must be set\n");
		return -EINVAL;
	}
	
	if (irq == -1 ||
	    !(irq == 5 ||
	      irq == 7 ||
	      irq == 9 ||
	      irq == 10 ||
	      irq == 11 ||
	      irq == 12)) {
		
		printk(KERN_ERR LOGNAME ": \"irq\" - must be set to 5, 7, 9, 10, 11 or 12\n");
		return -EINVAL;
	}

	if (mem == -1 ||
	    !(mem == 0xb0000 ||
	      mem == 0xc8000 ||
	      mem == 0xd0000 ||
	      mem == 0xd8000 ||
	      mem == 0xe0000 ||
	      mem == 0xe8000)) {
		
		printk(KERN_ERR LOGNAME ": \"mem\" - must be set to "
		       "0xb0000, 0xc8000, 0xd0000, 0xd8000, 0xe0000 or 0xe8000\n");
		return -EINVAL;
	}

	switch (irq) {
	case 5: dev.irqid = HPIRQ_5; break;
	case 7: dev.irqid = HPIRQ_7; break;
	case 9: dev.irqid = HPIRQ_9; break;
	case 10: dev.irqid = HPIRQ_10; break;
	case 11: dev.irqid = HPIRQ_11; break;
	case 12: dev.irqid = HPIRQ_12; break;
	}

	switch (mem) {
	case 0xb0000: dev.memid = HPMEM_B000; break;
	case 0xc8000: dev.memid = HPMEM_C800; break;
	case 0xd0000: dev.memid = HPMEM_D000; break;
	case 0xd8000: dev.memid = HPMEM_D800; break;
	case 0xe0000: dev.memid = HPMEM_E000; break;
	case 0xe8000: dev.memid = HPMEM_E800; break;
	}

	if (fifosize < 16)
		fifosize = 16;

	if (fifosize > 768)
		fifosize = 768;

	dev.type = msndClassic;
	dev.io = io;
	dev.numio = DSP_NUMIO;
	dev.irq = irq;
	dev.base = phys_to_virt(mem);
	dev.fifosize = fifosize * 1024;
	dev.calibrate_signal = calibrate_signal ? 1 : 0;
	dev.recsrc = 0;
	dev.inc_ref = mod_inc_ref;
	dev.dec_ref = mod_dec_ref;

	init_waitqueue(&dev.writeblock);
	init_waitqueue(&dev.readblock);
	msnd_fifo_init(&dev.DAPF);
	msnd_fifo_init(&dev.DARF);
	spin_lock_init(&dev.lock);

	printk(KERN_INFO LOGNAME ": Using %u byte digital audio FIFOs (x2)\n", dev.fifosize);

	if ((err = msnd_fifo_alloc(&dev.DAPF, dev.fifosize)) < 0) {
		
		printk(KERN_ERR LOGNAME ": Couldn't allocate write FIFO\n");
		return err;
	}

	if ((err = msnd_fifo_alloc(&dev.DARF, dev.fifosize)) < 0) {
		
		printk(KERN_ERR LOGNAME ": Couldn't allocate read FIFO\n");
		msnd_fifo_free(&dev.DAPF);
		return err;
	}

	if ((err = probe_multisound()) < 0) {

		printk(KERN_ERR LOGNAME ": Probe failed\n");
		msnd_fifo_free(&dev.DAPF);
		msnd_fifo_free(&dev.DARF);
		return err;

	}
	
	if ((err = attach_multisound()) < 0) {

		printk(KERN_ERR LOGNAME ": Attach failed\n");
		msnd_fifo_free(&dev.DAPF);
		msnd_fifo_free(&dev.DARF);
		return err;

	}

	return 0;
}

#ifdef MODULE

void cleanup_module(void)
{
	printk(KERN_INFO LOGNAME ": Unloading\n");

	unload_multisound();

	msnd_fifo_free(&dev.DAPF);
	msnd_fifo_free(&dev.DARF);

}
#endif
