
/*
 **********************************************************************
 *     audio.c -- /dev/dsp interface for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999	    Alan Cox        cleaned up types/leaks
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#include "hwaccess.h"
#include "cardwo.h"
#include "cardwi.h"
#include "recmgr.h"
#include "audio.h"

static void calculate_ofrag(struct woinst *);
static void calculate_ifrag(struct wiinst *);

/* Audio file operations */
static loff_t emu10k1_audio_llseek(struct file *file, loff_t offset, int nOrigin)
{
	return -ESPIPE;
}

static ssize_t emu10k1_audio_read(struct file *file, char *buffer, size_t count, loff_t * ppos)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct wiinst *wiinst = wave_dev->wiinst;
	struct wave_in *wave_in;
	ssize_t ret = 0;
	unsigned long flags;

	DPD(4, "emu10k1_audio_read(), buffer=%p, count=%x\n", buffer, (u32) count);

	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	spin_lock_irqsave(&wiinst->lock, flags);

	if (wiinst->mapped) {
		spin_unlock_irqrestore(&wiinst->lock, flags);
		return -ENXIO;
	}

	if (!wiinst->wave_in) {
		calculate_ifrag(wiinst);

		while (emu10k1_wavein_open(wave_dev) != CTSTATUS_SUCCESS) {
			spin_unlock_irqrestore(&wiinst->lock, flags);

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			UP_INODE_SEM(&inode->i_sem);
			interruptible_sleep_on(&wave_dev->card->open_wait);
			DOWN_INODE_SEM(&inode->i_sem);

			if (signal_pending(current))
				return -ERESTARTSYS;

			spin_lock_irqsave(&wiinst->lock, flags);
		}
	}

	wave_in = wiinst->wave_in;

	spin_unlock_irqrestore(&wiinst->lock, flags);

	while (count > 0) {
		u32 bytestocopy, dummy;

		spin_lock_irqsave(&wiinst->lock, flags);

		if ((wave_in->state != CARDWAVE_STATE_STARTED)
		    && (wave_dev->enablebits & PCM_ENABLE_INPUT))
			emu10k1_wavein_start(wave_dev);

		emu10k1_wavein_getxfersize(wave_in, &bytestocopy, &dummy);

		spin_unlock_irqrestore(&wiinst->lock, flags);

		DPD(4, "bytestocopy --> %x\n", bytestocopy);

		if ((bytestocopy >= wiinst->fragment_size)
		    || (bytestocopy >= count)) {
			bytestocopy = min(bytestocopy, count);

			emu10k1_wavein_xferdata(wiinst, (u8 *) buffer, &bytestocopy);

			count -= bytestocopy;
			buffer += bytestocopy;
			ret += bytestocopy;
		}

		if (count > 0) {
			if ((file->f_flags & O_NONBLOCK)
			    || (!(wave_dev->enablebits & PCM_ENABLE_INPUT)))
				return (ret ? ret : -EAGAIN);

			UP_INODE_SEM(&inode->i_sem);
			interruptible_sleep_on(&wiinst->wait_queue);
			DOWN_INODE_SEM(&inode->i_sem);

			if (signal_pending(current))
				return (ret ? ret : -ERESTARTSYS);

		}
	}

	DPD(4, "bytes copied -> %x\n", (u32) ret);

	return ret;
}

static ssize_t emu10k1_audio_write(struct file *file, const char *buffer, size_t count, loff_t * ppos)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct woinst *woinst = wave_dev->woinst;
	struct wave_out *wave_out;
	ssize_t ret;
	unsigned long flags;

	GET_INODE_STRUCT();

	DPD(4, "emu10k1_audio_write(), buffer=%p, count=%x\n", buffer, (u32) count);

	if (ppos != &file->f_pos)
		return -ESPIPE;

	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;

	spin_lock_irqsave(&woinst->lock, flags);

	if (woinst->mapped) {
		spin_unlock_irqrestore(&woinst->lock, flags);
		return -ENXIO;
	}

	if (!woinst->wave_out) {
		calculate_ofrag(woinst);

		while (emu10k1_waveout_open(wave_dev) != CTSTATUS_SUCCESS) {
			spin_unlock_irqrestore(&woinst->lock, flags);

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			UP_INODE_SEM(&inode->i_sem);
			interruptible_sleep_on(&wave_dev->card->open_wait);
			DOWN_INODE_SEM(&inode->i_sem);

			if (signal_pending(current))
				return -ERESTARTSYS;

			spin_lock_irqsave(&woinst->lock, flags);
		}
	}

	wave_out = woinst->wave_out;

	spin_unlock_irqrestore(&woinst->lock, flags);

	ret = 0;
	while (count > 0) {
		u32 bytestocopy, pending, dummy;

		spin_lock_irqsave(&woinst->lock, flags);

		emu10k1_waveout_getxfersize(wave_out, &bytestocopy, &pending, &dummy);

		spin_unlock_irqrestore(&woinst->lock, flags);

		DPD(4, "bytestocopy --> %x\n", bytestocopy);

		if ((bytestocopy >= woinst->fragment_size)
		    || (bytestocopy >= count)) {

			bytestocopy = min(bytestocopy, count);

			emu10k1_waveout_xferdata(woinst, (u8 *) buffer, &bytestocopy);

			count -= bytestocopy;
			buffer += bytestocopy;
			ret += bytestocopy;

			spin_lock_irqsave(&woinst->lock, flags);
			woinst->total_copied += bytestocopy;

			if ((wave_out->state != CARDWAVE_STATE_STARTED)
			    && (wave_dev->enablebits & PCM_ENABLE_OUTPUT)
			    && (woinst->total_copied >= woinst->fragment_size)) {

				if (emu10k1_waveout_start(wave_dev) != CTSTATUS_SUCCESS) {
					spin_unlock_irqrestore(&woinst->lock, flags);
					ERROR();
					return -EFAULT;
				}
			}
			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (count > 0) {
			if ((file->f_flags & O_NONBLOCK)
			    || (!(wave_dev->enablebits & PCM_ENABLE_OUTPUT)))
				return (ret ? ret : -EAGAIN);

			UP_INODE_SEM(&inode->i_sem);
			interruptible_sleep_on(&woinst->wait_queue);
			DOWN_INODE_SEM(&inode->i_sem);

			if (signal_pending(current))
				return (ret ? ret : -ERESTARTSYS);
		}
	}

	DPD(4, "bytes copied -> %x\n", (u32) ret);

	return ret;
}

static int emu10k1_audio_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	int val = 0;
	struct woinst *woinst = NULL;
	struct wave_out *wave_out = NULL;
	struct wiinst *wiinst = NULL;
	struct wave_in *wave_in = NULL;
	u32 pending, bytestocopy, dummy;
	unsigned long flags;

	DPF(4, "emu10k1_audio_ioctl()\n");

	if (file->f_mode & FMODE_WRITE) {
		woinst = wave_dev->woinst;
		spin_lock_irqsave(&woinst->lock, flags);
		wave_out = woinst->wave_out;
		spin_unlock_irqrestore(&woinst->lock, flags);
	}

	if (file->f_mode & FMODE_READ) {
		wiinst = wave_dev->wiinst;
		spin_lock_irqsave(&wiinst->lock, flags);
		wave_in = wiinst->wave_in;
		spin_unlock_irqrestore(&wiinst->lock, flags);
	}

	switch (cmd) {
	case OSS_GETVERSION:
		DPF(2, "OSS_GETVERSION:\n");
		return put_user(SOUND_VERSION, (int *) arg);

	case SNDCTL_DSP_RESET:
		DPF(2, "SNDCTL_DSP_RESET:\n");
		wave_dev->enablebits = PCM_ENABLE_OUTPUT | PCM_ENABLE_INPUT;

		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			if (wave_out)
				emu10k1_waveout_close(wave_dev);

			woinst->total_copied = 0;
			woinst->total_played = 0;
			woinst->blocks = 0;
			woinst->curpos = 0;

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			if (wave_in)
				emu10k1_wavein_close(wave_dev);

			wiinst->total_recorded = 0;
			wiinst->blocks = 0;
			wiinst->curpos = 0;
			spin_unlock_irqrestore(&wiinst->lock, flags);
		}

		break;

	case SNDCTL_DSP_SYNC:
		DPF(2, "SNDCTL_DSP_SYNC:\n");

		if (file->f_mode & FMODE_WRITE) {

			if (wave_out) {
				spin_lock_irqsave(&woinst->lock, flags);

				if (wave_out->state == CARDWAVE_STATE_STARTED)
					while ((woinst->total_played < woinst->total_copied)
					       && !signal_pending(current)) {
						spin_unlock_irqrestore(&woinst->lock, flags);
						interruptible_sleep_on(&woinst->wait_queue);
						spin_lock_irqsave(&woinst->lock, flags);
					}

				emu10k1_waveout_close(wave_dev);
				woinst->total_copied = 0;
				woinst->total_played = 0;
				woinst->blocks = 0;
				woinst->curpos = 0;

				spin_unlock_irqrestore(&woinst->lock, flags);
			}
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			if (wave_in)
				emu10k1_wavein_close(wave_dev);

			wiinst->total_recorded = 0;
			wiinst->blocks = 0;
			wiinst->curpos = 0;
			spin_unlock_irqrestore(&wiinst->lock, flags);
		}

		break;

	case SNDCTL_DSP_SETDUPLEX:
		DPF(2, "SNDCTL_DSP_SETDUPLEX:\n");
		break;

	case SNDCTL_DSP_GETCAPS:
		DPF(2, "SNDCTL_DSP_GETCAPS:\n");
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP | DSP_CAP_COPROC, (int *) arg);

	case SNDCTL_DSP_SPEED:
		DPF(2, "SNDCTL_DSP_SPEED:\n");

		get_user_ret(val, (int *) arg, -EFAULT);
		DPD(2, "val is %d\n", val);

		if (val >= 0) {
			if (file->f_mode & FMODE_WRITE) {
				spin_lock_irqsave(&woinst->lock, flags);

				woinst->wave_fmt.samplingrate = val;

				if (emu10k1_waveout_setformat(wave_dev) != CTSTATUS_SUCCESS)
					return -EINVAL;

				val = woinst->wave_fmt.samplingrate;

				spin_unlock_irqrestore(&woinst->lock, flags);

				DPD(2, "set playback sampling rate -> %d\n", val);
			}

			if (file->f_mode & FMODE_READ) {
				spin_lock_irqsave(&wiinst->lock, flags);

				wiinst->wave_fmt.samplingrate = val;

				if (emu10k1_wavein_setformat(wave_dev) != CTSTATUS_SUCCESS)
					return -EINVAL;

				val = wiinst->wave_fmt.samplingrate;

				spin_unlock_irqrestore(&wiinst->lock, flags);

				DPD(2, "set recording sampling rate -> %d\n", val);
			}

			return put_user(val, (int *) arg);
		} else {
			if (file->f_mode & FMODE_READ)
				val = wiinst->wave_fmt.samplingrate;
			else if (file->f_mode & FMODE_WRITE)
				val = woinst->wave_fmt.samplingrate;

			return put_user(val, (int *) arg);
		}
		break;

	case SNDCTL_DSP_STEREO:
		DPF(2, "SNDCTL_DSP_STEREO:\n");

		get_user_ret(val, (int *) arg, -EFAULT);
		DPD(2, " val is %d\n", val);

		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			woinst->wave_fmt.channels = val ? 2 : 1;

			if (emu10k1_waveout_setformat(wave_dev) != CTSTATUS_SUCCESS)
				return -EINVAL;

			val = woinst->wave_fmt.channels - 1;

			spin_unlock_irqrestore(&woinst->lock, flags);

			DPD(2, "set playback stereo -> %d\n", val);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			wiinst->wave_fmt.channels = val ? 2 : 1;

			if (emu10k1_wavein_setformat(wave_dev) != CTSTATUS_SUCCESS)
				return -EINVAL;

			val = wiinst->wave_fmt.channels - 1;

			spin_unlock_irqrestore(&wiinst->lock, flags);
			DPD(2, "set recording stereo -> %d\n", val);
		}

		return put_user(val, (int *) arg);

		break;

	case SNDCTL_DSP_CHANNELS:
		DPF(2, "SNDCTL_DSP_CHANNELS:\n");

		get_user_ret(val, (int *) arg, -EFAULT);
		DPD(2, " val is %d\n", val);

		if (val != 0) {
			if (file->f_mode & FMODE_WRITE) {
				spin_lock_irqsave(&woinst->lock, flags);

				woinst->wave_fmt.channels = val;

				if (emu10k1_waveout_setformat(wave_dev) != CTSTATUS_SUCCESS)
					return -EINVAL;

				val = woinst->wave_fmt.channels;

				spin_unlock_irqrestore(&woinst->lock, flags);
				DPD(2, "set playback number of channels -> %d\n", val);
			}

			if (file->f_mode & FMODE_READ) {
				spin_lock_irqsave(&wiinst->lock, flags);

				wiinst->wave_fmt.channels = val;

				if (emu10k1_wavein_setformat(wave_dev) != CTSTATUS_SUCCESS)
					return -EINVAL;

				val = wiinst->wave_fmt.channels;

				spin_unlock_irqrestore(&wiinst->lock, flags);
				DPD(2, "set recording number of channels -> %d\n", val);
			}

			return put_user(val, (int *) arg);
		} else {
			if (file->f_mode & FMODE_READ)
				val = wiinst->wave_fmt.channels;
			else if (file->f_mode & FMODE_WRITE)
				val = woinst->wave_fmt.channels;

			return put_user(val, (int *) arg);
		}
		break;

	case SNDCTL_DSP_GETFMTS:
		DPF(2, "SNDCTL_DSP_GETFMTS:\n");

		if (file->f_mode & FMODE_READ)
			val = AFMT_S16_LE;
		else if (file->f_mode & FMODE_WRITE)
			val = AFMT_S16_LE | AFMT_U8;

		return put_user(val, (int *) arg);

	case SNDCTL_DSP_SETFMT:	/* Same as SNDCTL_DSP_SAMPLESIZE */
		DPF(2, "SNDCTL_DSP_SETFMT:\n");

		get_user_ret(val, (int *) arg, -EFAULT);
		DPD(2, " val is %d\n", val);

		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_WRITE) {
				spin_lock_irqsave(&woinst->lock, flags);

				woinst->wave_fmt.bitsperchannel = val;

				if (emu10k1_waveout_setformat(wave_dev) != CTSTATUS_SUCCESS)
					return -EINVAL;

				val = woinst->wave_fmt.bitsperchannel;

				spin_unlock_irqrestore(&woinst->lock, flags);
				DPD(2, "set playback sample size -> %d\n", val);
			}

			if (file->f_mode & FMODE_READ) {
				spin_lock_irqsave(&wiinst->lock, flags);

				wiinst->wave_fmt.bitsperchannel = val;

				if (emu10k1_wavein_setformat(wave_dev) != CTSTATUS_SUCCESS)
					return -EINVAL;

				val = wiinst->wave_fmt.bitsperchannel;

				spin_unlock_irqrestore(&wiinst->lock, flags);
				DPD(2, "set recording sample size -> %d\n", val);
			}

			return put_user((val == 16) ? AFMT_S16_LE : AFMT_U8, (int *) arg);
		} else {
			if (file->f_mode & FMODE_READ)
				val = wiinst->wave_fmt.bitsperchannel;
			else if (file->f_mode & FMODE_WRITE)
				val = woinst->wave_fmt.bitsperchannel;

			return put_user((val == 16) ? AFMT_S16_LE : AFMT_U8, (int *) arg);
		}
		break;

	case SOUND_PCM_READ_BITS:

		if (file->f_mode & FMODE_READ)
			val = wiinst->wave_fmt.bitsperchannel;
		else if (file->f_mode & FMODE_WRITE)
			val = woinst->wave_fmt.bitsperchannel;

		return put_user((val == 16) ? AFMT_S16_LE : AFMT_U8, (int *) arg);

	case SOUND_PCM_READ_RATE:

		if (file->f_mode & FMODE_READ)
			val = wiinst->wave_fmt.samplingrate;
		else if (file->f_mode & FMODE_WRITE)
			val = woinst->wave_fmt.samplingrate;

		return put_user(val, (int *) arg);

	case SOUND_PCM_READ_CHANNELS:

		if (file->f_mode & FMODE_READ)
			val = wiinst->wave_fmt.channels;
		else if (file->f_mode & FMODE_WRITE)
			val = woinst->wave_fmt.channels;

		return put_user(val, (int *) arg);

	case SOUND_PCM_WRITE_FILTER:
		DPF(2, "SOUND_PCM_WRITE_FILTER: not implemented\n");
		break;

	case SOUND_PCM_READ_FILTER:
		DPF(2, "SOUND_PCM_READ_FILTER: not implemented\n");
		break;

	case SNDCTL_DSP_SETSYNCRO:
		DPF(2, "SNDCTL_DSP_SETSYNCRO: not implemented\n");
		break;

	case SNDCTL_DSP_GETTRIGGER:
		DPF(2, "SNDCTL_DSP_GETTRIGGER:\n");

		if (file->f_mode & FMODE_WRITE && (wave_dev->enablebits & PCM_ENABLE_OUTPUT))
			val |= PCM_ENABLE_OUTPUT;
		if (file->f_mode & FMODE_READ && (wave_dev->enablebits & PCM_ENABLE_INPUT))
			val |= PCM_ENABLE_INPUT;

		return put_user(val, (int *) arg);

	case SNDCTL_DSP_SETTRIGGER:
		DPF(2, "SNDCTL_DSP_SETTRIGGER:\n");

		get_user_ret(val, (int *) arg, -EFAULT);

		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			if (val & PCM_ENABLE_OUTPUT) {
				wave_dev->enablebits |= PCM_ENABLE_OUTPUT;
				if (wave_out)
					emu10k1_waveout_start(wave_dev);
			} else {
				wave_dev->enablebits &= ~PCM_ENABLE_OUTPUT;
				if (wave_out)
					emu10k1_waveout_stop(wave_dev);
			}

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			if (val & PCM_ENABLE_INPUT) {
				wave_dev->enablebits |= PCM_ENABLE_INPUT;
				if (wave_in)
					emu10k1_wavein_start(wave_dev);
			} else {
				wave_dev->enablebits &= ~PCM_ENABLE_INPUT;
				if (wave_in)
					emu10k1_wavein_stop(wave_dev);
			}

			spin_unlock_irqrestore(&wiinst->lock, flags);
		}
		break;

	case SNDCTL_DSP_GETOSPACE:
		{
			audio_buf_info info;

			DPF(4, "SNDCTL_DSP_GETOSPACE:\n");

			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;

			if (wave_out) {
				spin_lock_irqsave(&woinst->lock, flags);
				emu10k1_waveout_getxfersize(wave_out, &bytestocopy, &pending, &dummy);
				spin_unlock_irqrestore(&woinst->lock, flags);

				info.bytes = bytestocopy;
			} else {
				spin_lock_irqsave(&woinst->lock, flags);
				calculate_ofrag(woinst);
				spin_unlock_irqrestore(&woinst->lock, flags);

				info.bytes = woinst->numfrags * woinst->fragment_size;
			}

			info.fragstotal = woinst->numfrags;
			info.fragments = info.bytes / woinst->fragment_size;
			info.fragsize = woinst->fragment_size;

			if (copy_to_user((int *) arg, &info, sizeof(info)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_GETISPACE:
		{
			audio_buf_info info;

			DPF(4, "SNDCTL_DSP_GETISPACE:\n");

			if (!(file->f_mode & FMODE_READ))
				return -EINVAL;

			if (wave_in) {
				spin_lock_irqsave(&wiinst->lock, flags);
				emu10k1_wavein_getxfersize(wave_in, &bytestocopy, &dummy);
				spin_unlock_irqrestore(&wiinst->lock, flags);

				info.bytes = bytestocopy;
			} else {
				spin_lock_irqsave(&wiinst->lock, flags);
				calculate_ifrag(wiinst);
				spin_unlock_irqrestore(&wiinst->lock, flags);

				info.bytes = 0;
			}

			info.fragstotal = wiinst->numfrags;
			info.fragments = info.bytes / wiinst->fragment_size;
			info.fragsize = wiinst->fragment_size;

			if (copy_to_user((int *) arg, &info, sizeof(info)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_NONBLOCK:
		DPF(2, "SNDCTL_DSP_NONBLOCK:\n");

		file->f_flags |= O_NONBLOCK;
		break;

	case SNDCTL_DSP_GETODELAY:
		DPF(4, "SNDCTL_DSP_GETODELAY:\n");

		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;

		if (wave_out) {
			spin_lock_irqsave(&woinst->lock, flags);
			emu10k1_waveout_getxfersize(wave_out, &bytestocopy, &pending, &dummy);
			spin_unlock_irqrestore(&woinst->lock, flags);

			val = pending;
		} else
			val = 0;

		return put_user(val, (int *) arg);

	case SNDCTL_DSP_GETIPTR:
		{
			count_info cinfo;

			DPF(4, "SNDCTL_DSP_GETIPTR: \n");

			if (!(file->f_mode & FMODE_READ))
				return -EINVAL;

			spin_lock_irqsave(&wiinst->lock, flags);

			if (wave_in) {
				emu10k1_wavein_getcontrol(wave_in, WAVECURPOS, (u32 *) & cinfo.ptr);
				cinfo.bytes =
				    cinfo.ptr + wiinst->total_recorded - wiinst->total_recorded % (wiinst->fragment_size * wiinst->numfrags);
				cinfo.blocks = cinfo.bytes / wiinst->fragment_size - wiinst->blocks;
				wiinst->blocks = cinfo.bytes / wiinst->fragment_size;
			} else {
				cinfo.ptr = 0;
				cinfo.bytes = 0;
				cinfo.blocks = 0;
				wiinst->blocks = 0;
			}

			spin_unlock_irqrestore(&wiinst->lock, flags);

			if (copy_to_user((void *) arg, &cinfo, sizeof(cinfo)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_GETOPTR:
		{
			count_info cinfo;

			DPF(4, "SNDCTL_DSP_GETOPTR:\n");

			if (!(file->f_mode & FMODE_WRITE))
				return -EINVAL;

			spin_lock_irqsave(&woinst->lock, flags);

			if (wave_out) {
				emu10k1_waveout_getcontrol(wave_out, WAVECURPOS, (u32 *) & cinfo.ptr);
				cinfo.bytes = cinfo.ptr + woinst->total_played - woinst->total_played % (woinst->fragment_size * woinst->numfrags);
				cinfo.blocks = cinfo.bytes / woinst->fragment_size - woinst->blocks;
				woinst->blocks = cinfo.bytes / woinst->fragment_size;
			} else {
				cinfo.ptr = 0;
				cinfo.bytes = 0;
				cinfo.blocks = 0;
				woinst->blocks = 0;
			}

			spin_unlock_irqrestore(&woinst->lock, flags);

			if (copy_to_user((void *) arg, &cinfo, sizeof(cinfo)))
				return -EFAULT;
		}
		break;

	case SNDCTL_DSP_GETBLKSIZE:
		DPF(2, "SNDCTL_DSP_GETBLKSIZE:\n");

		if (file->f_mode & FMODE_WRITE) {
			spin_lock_irqsave(&woinst->lock, flags);

			calculate_ofrag(woinst);
			val = woinst->fragment_size;

			spin_unlock_irqrestore(&woinst->lock, flags);
		}

		if (file->f_mode & FMODE_READ) {
			spin_lock_irqsave(&wiinst->lock, flags);

			calculate_ifrag(wiinst);
			val = wiinst->fragment_size;

			spin_unlock_irqrestore(&wiinst->lock, flags);
		}

		return put_user(val, (int *) arg);

		break;

	case SNDCTL_DSP_POST:
		DPF(2, "SNDCTL_DSP_POST: not implemented\n");
		break;

	case SNDCTL_DSP_SUBDIVIDE:
		DPF(2, "SNDCTL_DSP_SUBDIVIDE: not implemented\n");
		break;

	case SNDCTL_DSP_SETFRAGMENT:
		DPF(2, "SNDCTL_DSP_SETFRAGMENT:\n");

		get_user_ret(val, (int *) arg, -EFAULT);

		DPD(2, "val is %x\n", val);

		if (val == 0)
			return -EIO;

		if (file->f_mode & FMODE_WRITE) {
			if (wave_out)
				return -EINVAL;	/* too late to change */

			woinst->ossfragshift = val & 0xffff;
			woinst->numfrags = (val >> 16) & 0xffff;
		}

		if (file->f_mode & FMODE_READ) {
			if (wave_in)
				return -EINVAL;	/* too late to change */

			wiinst->ossfragshift = val & 0xffff;
			wiinst->numfrags = (val >> 16) & 0xffff;
		}

		break;

	case SNDCTL_COPR_LOAD:
		{
			copr_buffer buf;
			u32 i;

			DPF(2, "SNDCTL_COPR_LOAD:\n");

			if (copy_from_user(&buf, (copr_buffer *) arg, sizeof(buf)))
				return -EFAULT;

			if ((buf.command != 1) && (buf.command != 2))
				return -EINVAL;

			if (((buf.offs < 0x100) && (buf.command == 2))
			    || (buf.offs < 0x000)
			    || (buf.offs + buf.len > 0x800) || (buf.len > 1000))
				return -EINVAL;

			if (buf.command == 1) {
				for (i = 0; i < buf.len; i++)

					((u32 *) buf.data)[i] = sblive_readptr(wave_dev->card, buf.offs + i, 0);
				if (copy_to_user((copr_buffer *) arg, &buf, sizeof(buf)))
					return -EFAULT;
			} else {
				for (i = 0; i < buf.len; i++)
					sblive_writeptr(wave_dev->card, buf.offs + i, 0, ((u32 *) buf.data)[i]);
			}
			break;
		}

	default:		/* Default is unrecognized command */
		DPD(2, "default: %x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

static int emu10k1_audio_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;

	DPF(2, "emu10k1_audio_mmap()\n");

	if (vma_get_pgoff(vma) != 0)
		return -ENXIO;

	if (vma->vm_flags & VM_WRITE) {
		struct woinst *woinst = wave_dev->woinst;
		struct wave_out *wave_out;
		u32 size;
		unsigned long flags;
		int i;

		spin_lock_irqsave(&woinst->lock, flags);

		wave_out = woinst->wave_out;

		if (!wave_out) {
			calculate_ofrag(woinst);

			if (emu10k1_waveout_open(wave_dev) != CTSTATUS_SUCCESS) {
				spin_unlock_irqrestore(&woinst->lock, flags);
				ERROR();
				return -EINVAL;
			}

			wave_out = woinst->wave_out;

			/* Now mark the pages as reserved, otherwise remap_page_range doesn't do what we want */
			for (i = 0; i < wave_out->wavexferbuf->numpages; i++)
				set_bit(PG_reserved, &mem_map[MAP_NR(wave_out->pagetable[i])].flags);
		}

		size = vma->vm_end - vma->vm_start;

		if (size > (PAGE_SIZE * wave_out->wavexferbuf->numpages)) {
			spin_unlock_irqrestore(&woinst->lock, flags);
			return -EINVAL;
		}

		for (i = 0; i < wave_out->wavexferbuf->numpages; i++) {
			if (remap_page_range(vma->vm_start + (i * PAGE_SIZE), virt_to_phys(wave_out->pagetable[i]), PAGE_SIZE, vma->vm_page_prot)) {
				spin_unlock_irqrestore(&woinst->lock, flags);
				return -EAGAIN;
			}
		}

		woinst->mapped = 1;

		spin_unlock_irqrestore(&woinst->lock, flags);
	}

	if (vma->vm_flags & VM_READ) {
		struct wiinst *wiinst = wave_dev->wiinst;
		unsigned long flags;

		spin_lock_irqsave(&wiinst->lock, flags);
		wiinst->mapped = 1;
		spin_unlock_irqrestore(&wiinst->lock, flags);
	}

	return 0;
}

static int emu10k1_audio_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct emu10k1_card *card;
	struct list_head *entry;
	struct emu10k1_wavedevice *wave_dev;

	DPF(2, "emu10k1_audio_open()\n");

	/* Check for correct device to open */

	list_for_each(entry, &emu10k1_devs) {
		card = list_entry(entry, struct emu10k1_card, list);

		if (card->audio1_num == minor || card->audio2_num == minor)
			break;
	}

	if (entry == &emu10k1_devs)
		return -ENODEV;

	if ((wave_dev = (struct emu10k1_wavedevice *)
	     kmalloc(sizeof(struct emu10k1_wavedevice), GFP_KERNEL)) == NULL) {
		ERROR();
		return -EINVAL;
	}

	wave_dev->card = card;
	wave_dev->wiinst = NULL;
	wave_dev->woinst = NULL;
	wave_dev->enablebits = PCM_ENABLE_OUTPUT | PCM_ENABLE_INPUT;	/* Default */

	if (file->f_mode & FMODE_WRITE) {
		struct woinst *woinst;

		if ((woinst = (struct woinst *) kmalloc(sizeof(struct woinst), GFP_KERNEL)) == NULL) {
			ERROR();
			return -ENODEV;
		}

		woinst->wave_fmt.samplingrate = 8000;
		woinst->wave_fmt.bitsperchannel = 8;
		woinst->wave_fmt.channels = 1;
		woinst->ossfragshift = 0;
		woinst->fragment_size = 0;
		woinst->numfrags = 0;
		woinst->device = (card->audio2_num == minor);
		woinst->wave_out = NULL;

		init_waitqueue_head(&woinst->wait_queue);

		woinst->mapped = 0;
		woinst->total_copied = 0;
		woinst->total_played = 0;
		woinst->blocks = 0;
		woinst->curpos = 0;
		woinst->lock = SPIN_LOCK_UNLOCKED;
		wave_dev->woinst = woinst;

#ifdef PRIVATE_PCM_VOLUME
		{
			int i;
			int j = -1;

			/*
			 * find out if we've already been in this table
			 * xmms reopens dsp on every move of slider
			 * this way we keep the same local pcm for such
			 * process
			 */
			for (i = 0; i < MAX_PCM_CHANNELS; i++) {
				if (sblive_pcm_volume[i].files == current->files)
					break;
				// here we should select last used memeber
				// improve me in case its not sufficient
				if (j < 0 && !sblive_pcm_volume[i].opened)
					j = i;
			}
			// current task not found
			if (i == MAX_PCM_CHANNELS) {
				// add new entry
				if (j < 0)
					printk("TOO MANY WRITTERS!!!\n");
				i = (j >= 0) ? j : 0;
				DPD(2, "new pcm private %p\n", current->files);
				sblive_pcm_volume[i].files = current->files;
				sblive_pcm_volume[i].mixer = 0x6464;	// max
				sblive_pcm_volume[i].attn_l = 0;
				sblive_pcm_volume[i].attn_r = 0;
				sblive_pcm_volume[i].channel_l = NUM_G;
				sblive_pcm_volume[i].channel_r = NUM_G;
			}
			sblive_pcm_volume[i].opened++;
		}
#endif
	}

	if (file->f_mode & FMODE_READ) {
		/* Recording */
		struct wiinst *wiinst;

		if ((wiinst = (struct wiinst *) kmalloc(sizeof(struct wiinst), GFP_KERNEL)) == NULL) {
			ERROR();
			return -ENODEV;
		}

		switch (card->wavein->recsrc) {
		case WAVERECORD_AC97:
			wiinst->wave_fmt.samplingrate = 8000;
			wiinst->wave_fmt.bitsperchannel = 8;
			wiinst->wave_fmt.channels = 1;
			break;
		case WAVERECORD_MIC:
			wiinst->wave_fmt.samplingrate = 8000;
			wiinst->wave_fmt.bitsperchannel = 8;
			wiinst->wave_fmt.channels = 1;
			break;
		case WAVERECORD_FX:
			wiinst->wave_fmt.samplingrate = 48000;
			wiinst->wave_fmt.bitsperchannel = 16;
			wiinst->wave_fmt.channels = 2;
			break;
		default:
			break;
		}

		wiinst->recsrc = card->wavein->recsrc;
		wiinst->ossfragshift = 0;
		wiinst->fragment_size = 0;
		wiinst->numfrags = 0;
		wiinst->wave_in = NULL;

		init_waitqueue_head(&wiinst->wait_queue);

		wiinst->mapped = 0;
		wiinst->total_recorded = 0;
		wiinst->blocks = 0;
		wiinst->curpos = 0;
		wiinst->lock = SPIN_LOCK_UNLOCKED;
		wave_dev->wiinst = wiinst;
	}

	file->private_data = (void *) wave_dev;

	return 0;		/* Success? */
}

static int emu10k1_audio_release(struct inode *inode, struct file *file)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct emu10k1_card *card = wave_dev->card;
	unsigned long flags;

	DPF(2, "emu10k1_audio_release()\n");

	if (file->f_mode & FMODE_WRITE) {
		struct woinst *woinst = wave_dev->woinst;
		struct wave_out *wave_out;

		spin_lock_irqsave(&woinst->lock, flags);

		wave_out = woinst->wave_out;

		if (wave_out) {
			if ((wave_out->state == CARDWAVE_STATE_STARTED)
			    && !(file->f_flags & O_NONBLOCK)) {
				while (!signal_pending(current)
				       && (woinst->total_played < woinst->total_copied)) {
					DPF(4, "Buffer hasn't been totally played, sleep....\n");
					spin_unlock_irqrestore(&woinst->lock, flags);
					interruptible_sleep_on(&woinst->wait_queue);
					spin_lock_irqsave(&woinst->lock, flags);
				}
			}

			if (woinst->mapped && wave_out->pagetable) {
				int i;

				/* Undo marking the pages as reserved */
				for (i = 0; i < woinst->wave_out->wavexferbuf->numpages; i++)
					set_bit(PG_reserved, &mem_map[MAP_NR(woinst->wave_out->pagetable[i])].flags);
			}

			woinst->mapped = 0;
			emu10k1_waveout_close(wave_dev);
		}
#ifdef PRIVATE_PCM_VOLUME
		{
			int i;

			/* mark as closed
			 * NOTE: structure remains unchanged for next reopen */
			for (i = 0; i < MAX_PCM_CHANNELS; i++) {
				if (sblive_pcm_volume[i].files == current->files) {
					sblive_pcm_volume[i].opened--;
					break;
				}
			}
		}
#endif
		spin_unlock_irqrestore(&woinst->lock, flags);
		kfree(wave_dev->woinst);
	}

	if (file->f_mode & FMODE_READ) {
		struct wiinst *wiinst = wave_dev->wiinst;
		struct wave_in *wave_in;

		spin_lock_irqsave(&wiinst->lock, flags);

		wave_in = wiinst->wave_in;

		if (wave_in) {
			wiinst->mapped = 0;
			emu10k1_wavein_close(wave_dev);
		}
		spin_unlock_irqrestore(&wiinst->lock, flags);
		kfree(wave_dev->wiinst);
	}

	kfree(wave_dev);

	wake_up_interruptible(&card->open_wait);

	return 0;
}

static unsigned int emu10k1_audio_poll(struct file *file, struct poll_table_struct *wait)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct woinst *woinst = wave_dev->woinst;
	struct wiinst *wiinst = wave_dev->wiinst;
	unsigned int mask = 0;
	u32 bytestocopy, pending, dummy;
	unsigned long flags;

	DPF(4, "emu10k1_audio_poll()\n");

	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &woinst->wait_queue, wait);

	if (file->f_mode & FMODE_READ)
		poll_wait(file, &wiinst->wait_queue, wait);

	if (file->f_mode & FMODE_WRITE) {
		struct wave_out *wave_out;

		spin_lock_irqsave(&woinst->lock, flags);

		wave_out = woinst->wave_out;

		if (wave_out) {

			emu10k1_waveout_getxfersize(wave_out, &bytestocopy, &pending, &dummy);

			if (bytestocopy >= woinst->fragment_size)
				mask |= POLLOUT | POLLWRNORM;
		} else
			mask |= POLLOUT | POLLWRNORM;

		spin_unlock_irqrestore(&woinst->lock, flags);
	}

	if (file->f_mode & FMODE_READ) {
		struct wave_in *wave_in;

		spin_lock_irqsave(&wiinst->lock, flags);

		wave_in = wiinst->wave_in;

		if (!wave_in) {
			calculate_ifrag(wiinst);
			if (emu10k1_wavein_open(wave_dev) != CTSTATUS_SUCCESS) {
				spin_unlock_irqrestore(&wiinst->lock, flags);
				return (mask |= POLLERR);
			}

			wave_in = wiinst->wave_in;
		}

		if (wave_in->state != CARDWAVE_STATE_STARTED) {
			wave_dev->enablebits |= PCM_ENABLE_INPUT;
			emu10k1_wavein_start(wave_dev);
		}

		emu10k1_wavein_getxfersize(wave_in, &bytestocopy, &dummy);

		if (bytestocopy >= wiinst->fragment_size)
			mask |= POLLIN | POLLRDNORM;

		spin_unlock_irqrestore(&wiinst->lock, flags);
	}

	return mask;
}

static void calculate_ofrag(struct woinst *woinst)
{
	u32 fragsize, bytespersec;

	if (woinst->fragment_size)
		return;

	bytespersec = woinst->wave_fmt.channels * (woinst->wave_fmt.bitsperchannel >> 3) * woinst->wave_fmt.samplingrate;

	if (!woinst->ossfragshift) {
		fragsize = (bytespersec * WAVEOUT_DEFAULTFRAGLEN) / 1000 - 1;

		while (fragsize) {
			fragsize >>= 1;
			woinst->ossfragshift++;
		}
	}

	if (woinst->ossfragshift < WAVEOUT_MINFRAGSHIFT)
		woinst->ossfragshift = WAVEOUT_MINFRAGSHIFT;

	woinst->fragment_size = 1 << woinst->ossfragshift;

	if (!woinst->numfrags) {
		u32 numfrags;

		numfrags = (bytespersec * WAVEOUT_DEFAULTBUFLEN) / (woinst->fragment_size * 1000) - 1;

		woinst->numfrags = 1;

		while (numfrags) {
			numfrags >>= 1;
			woinst->numfrags <<= 1;
		}
	}

	if (woinst->numfrags < MINFRAGS)
		woinst->numfrags = MINFRAGS;

	if (woinst->numfrags * woinst->fragment_size > WAVEOUT_MAXBUFSIZE) {
		woinst->numfrags = WAVEOUT_MAXBUFSIZE / woinst->fragment_size;

		if (woinst->numfrags < MINFRAGS) {
			woinst->numfrags = MINFRAGS;
			woinst->fragment_size = WAVEOUT_MAXBUFSIZE / MINFRAGS;
		}

	} else if (woinst->numfrags * woinst->fragment_size < WAVEOUT_MINBUFSIZE)
		woinst->numfrags = WAVEOUT_MINBUFSIZE / woinst->fragment_size;

	DPD(2, " calculated playback fragment_size -> %d\n", woinst->fragment_size);
	DPD(2, " calculated playback numfrags -> %d\n", woinst->numfrags);
}

static void calculate_ifrag(struct wiinst *wiinst)
{
	u32 fragsize, bytespersec;

	if (wiinst->fragment_size)
		return;

	bytespersec = wiinst->wave_fmt.channels * (wiinst->wave_fmt.bitsperchannel >> 3) * wiinst->wave_fmt.samplingrate;

	if (!wiinst->ossfragshift) {
		fragsize = (bytespersec * WAVEIN_DEFAULTFRAGLEN) / 1000 - 1;

		while (fragsize) {
			fragsize >>= 1;
			wiinst->ossfragshift++;
		}
	}

	if (wiinst->ossfragshift < WAVEIN_MINFRAGSHIFT)
		wiinst->ossfragshift = WAVEIN_MINFRAGSHIFT;

	wiinst->fragment_size = 1 << wiinst->ossfragshift;

	if (!wiinst->numfrags)
		wiinst->numfrags = (bytespersec * WAVEIN_DEFAULTBUFLEN) / (wiinst->fragment_size * 1000) - 1;

	if (wiinst->numfrags < MINFRAGS)
		wiinst->numfrags = MINFRAGS;

	if (wiinst->numfrags * wiinst->fragment_size > WAVEIN_MAXBUFSIZE) {
		wiinst->numfrags = WAVEIN_MAXBUFSIZE / wiinst->fragment_size;

		if (wiinst->numfrags < MINFRAGS) {
			wiinst->numfrags = MINFRAGS;
			wiinst->fragment_size = WAVEIN_MAXBUFSIZE / MINFRAGS;
		}
	} else if (wiinst->numfrags * wiinst->fragment_size < WAVEIN_MINBUFSIZE)
		wiinst->numfrags = WAVEIN_MINBUFSIZE / wiinst->fragment_size;

	DPD(2, " calculated recording fragment_size -> %d\n", wiinst->fragment_size);
	DPD(2, " calculated recording numfrags -> %d\n", wiinst->numfrags);
}

void emu10k1_wavein_bh(unsigned long refdata)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) refdata;
	struct wiinst *wiinst = wave_dev->wiinst;
	struct wave_in *wave_in = wiinst->wave_in;
	u32 bytestocopy, curpos;
	unsigned long flags;

	spin_lock_irqsave(&wiinst->lock, flags);

	if (wave_in->state == CARDWAVE_STATE_STOPPED) {
		spin_unlock_irqrestore(&wiinst->lock, flags);
		return;
	}

	emu10k1_wavein_getxfersize(wave_in, &bytestocopy, &curpos);

	wiinst->total_recorded += curpos - wiinst->curpos;

	if (curpos < wiinst->curpos)
		wiinst->total_recorded += wiinst->fragment_size * wiinst->numfrags;

	wiinst->curpos = curpos;

	if (wiinst->mapped) {
		spin_unlock_irqrestore(&wiinst->lock, flags);
		return;
	}

	spin_unlock_irqrestore(&wiinst->lock, flags);

	if (bytestocopy >= wiinst->fragment_size)
		wake_up_interruptible(&wiinst->wait_queue);
	else
		DPD(4, "Not enough transfer size, %d\n", bytestocopy);

	return;
}

void emu10k1_waveout_bh(unsigned long refdata)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) refdata;
	struct woinst *woinst = wave_dev->woinst;
	struct wave_out *wave_out = woinst->wave_out;
	u32 bytestocopy, pending, curpos;
	unsigned long flags;

	spin_lock_irqsave(&woinst->lock, flags);

	if (wave_out->state == CARDWAVE_STATE_STOPPED) {
		spin_unlock_irqrestore(&woinst->lock, flags);
		return;
	}

	emu10k1_waveout_getxfersize(wave_out, &bytestocopy, &pending, &curpos);

	woinst->total_played += curpos - woinst->curpos;

	if (curpos < woinst->curpos)
		woinst->total_played += woinst->fragment_size * woinst->numfrags;

	woinst->curpos = curpos;

	if (woinst->mapped) {
		spin_unlock_irqrestore(&woinst->lock, flags);
		return;
	}

	if (wave_out->fill_silence) {
		spin_unlock_irqrestore(&woinst->lock, flags);
		emu10k1_waveout_fillsilence(woinst);
	} else
		spin_unlock_irqrestore(&woinst->lock, flags);

	if (bytestocopy >= woinst->fragment_size)
		wake_up_interruptible(&woinst->wait_queue);
	else
		DPD(4, "Not enough transfer size -> %x\n", bytestocopy);

	return;
}

struct file_operations emu10k1_audio_fops = {
	owner:THIS_MODULE,
	llseek:emu10k1_audio_llseek,
	read:emu10k1_audio_read,
	write:emu10k1_audio_write,
	poll:emu10k1_audio_poll,
	ioctl:emu10k1_audio_ioctl,
	mmap:emu10k1_audio_mmap,
	open:emu10k1_audio_open,
	release:emu10k1_audio_release,
};
