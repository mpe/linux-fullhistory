
/*
 **********************************************************************
 *     cardwi.c - PCM input HAL for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
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
#include "recmgr.h"
#include "audio.h"
#include "cardwi.h"

void query_format(int recsrc, struct wave_format *wave_fmt)
{

	switch (recsrc) {
	case WAVERECORD_AC97:

		if ((wave_fmt->channels != 2) && (wave_fmt->channels != 1))
			wave_fmt->channels = 2;

		if (wave_fmt->samplingrate >= (0xBB80 + 0xAC44) / 2)
			wave_fmt->samplingrate = 0xBB80;
		else if (wave_fmt->samplingrate >= (0xAC44 + 0x7D00) / 2)
			wave_fmt->samplingrate = 0xAC44;
		else if (wave_fmt->samplingrate >= (0x7D00 + 0x5DC0) / 2)
			wave_fmt->samplingrate = 0x7D00;
		else if (wave_fmt->samplingrate >= (0x5DC0 + 0x5622) / 2)
			wave_fmt->samplingrate = 0x5DC0;
		else if (wave_fmt->samplingrate >= (0x5622 + 0x3E80) / 2)
			wave_fmt->samplingrate = 0x5622;
		else if (wave_fmt->samplingrate >= (0x3E80 + 0x2B11) / 2)
			wave_fmt->samplingrate = 0x3E80;
		else if (wave_fmt->samplingrate >= (0x2B11 + 0x1F40) / 2)
			wave_fmt->samplingrate = 0x2B11;
		else
			wave_fmt->samplingrate = 0x1F40;

		if ((wave_fmt->bitsperchannel != 16) && (wave_fmt->bitsperchannel != 8))
			wave_fmt->bitsperchannel = 16;

		break;

	case WAVERECORD_MIC:
		wave_fmt->channels = 1;
		wave_fmt->samplingrate = 0x1F40;
		wave_fmt->bitsperchannel = 8;
		break;

	case WAVERECORD_FX:
		wave_fmt->channels = 2;
		wave_fmt->samplingrate = 0xBB80;
		wave_fmt->bitsperchannel = 16;
		break;

	default:
		break;
	}

	return;
}

static int alloc_recbuffer(struct wave_in *wave_in, u32 * bufsize, u8 ** buffer)
{
	u32 reqsize;
	int i, j;
	u32 size[4];

	/* NOTE: record buffer size only can be certain sizes.  If the requested
	 * size is not a nice size, use the smaller nearest size. The minimum size is 1k. */
	if (!wave_in->rec_ptr->is_16bit)
		*bufsize <<= 1;

	if (*bufsize >= 0x10000) {
		*bufsize = reqsize = 0x10000;
		wave_in->rec_ptr->bufsize = 31;
	} else {
		reqsize = 0;
		size[0] = 384;
		size[1] = 448;
		size[2] = 512;
		size[3] = 640;

		for (i = 0; i < 8; i++)
			for (j = 0; j < 4; j++)
				if (*bufsize >= size[j]) {
					reqsize = size[j];
					size[j] = size[j] * 2;
					wave_in->rec_ptr->bufsize = i * 4 + j + 1;
				} else
					goto exitloop;
	      exitloop:
		if (reqsize == 0) {
			reqsize = 384;
			wave_in->rec_ptr->bufsize = 1;
		}

		*bufsize = reqsize;
	}

	DPD(2, "bufsizereg: %x\n", wave_in->rec_ptr->bufsize);

	/* Recording buffer must be continuous and page-aligned */
	if ((wave_in->memhandle = emu10k1_alloc_memphysical(reqsize)) == NULL)
		return CTSTATUS_ERROR;

	DPD(2, "recbufsize: %x\n", *bufsize);

	*buffer = (u8 *) wave_in->memhandle->virtaddx;

	return CTSTATUS_SUCCESS;
}

static int get_recbuffer(struct emu10k1_card *card, struct wave_in *wave_in, u32 * size)
{
	u8 *buffer;

	wave_in->rec_ptr->card = card;
	wave_in->rec_ptr->recpos = 0;
	wave_in->rec_ptr->samplingrate = wave_in->wave_fmt.samplingrate;
	wave_in->rec_ptr->is_stereo = (wave_in->wave_fmt.channels == 2) ? 1 : 0;
	wave_in->rec_ptr->is_16bit = (wave_in->wave_fmt.bitsperchannel == 16) ? 1 : 0;

	/* Allocate buffer here */
	if (alloc_recbuffer(wave_in, size, &buffer) != CTSTATUS_SUCCESS) {
		ERROR();
		return CTSTATUS_ERROR;
	}

	/* recbufsize contains actual record buffer size          */
	/* for 8 bit samples the size is twice the requested      */
	/* value since we only make use of one in every two bytes */
	wave_in->rec_ptr->recbufsize = *size;
	wave_in->rec_ptr->recbuffer = buffer;
	wave_in->rec_ptr->busaddx = wave_in->memhandle->busaddx;

	return CTSTATUS_SUCCESS;
}

static void dealloc_recbuffer(struct wave_in *wave_in)
{
	emu10k1_free_memphysical(wave_in->memhandle);
	return;
}

int emu10k1_wavein_open(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wiinst *wiinst = wave_dev->wiinst;
	struct wave_in *wave_in;
	struct wave_in **wave_in_tmp = NULL;
	u32 buffsize, bytespersec, delay;
	unsigned long flags;

	DPF(2, "emu10k1_wavein_open()\n");

	if ((wave_in = (struct wave_in *) kmalloc(sizeof(struct wave_in), GFP_KERNEL)) == NULL) {
		ERROR();
		return CTSTATUS_ERROR;
	}

	wave_in->state = CARDWAVE_STATE_STOPPED;
	wave_in->wave_fmt = wiinst->wave_fmt;
	wave_in->memhandle = NULL;
	wave_in->timer = NULL;

	switch (wiinst->recsrc) {
	case WAVERECORD_AC97:
		wave_in_tmp = &card->wavein->ac97;
		break;
	case WAVERECORD_MIC:
		wave_in_tmp = &card->wavein->mic;
		break;
	case WAVERECORD_FX:
		wave_in_tmp = &card->wavein->fx;
		break;
	default:
		break;
	}

	spin_lock_irqsave(&card->lock, flags);
	if (*wave_in_tmp != NULL) {
		spin_unlock_irqrestore(&card->lock, flags);
		kfree(wave_in);
		return CTSTATUS_ERROR;
	}

	*wave_in_tmp = wave_in;
	spin_unlock_irqrestore(&card->lock, flags);

	wiinst->wave_in = wave_in;

	if ((wave_in->rec_ptr = (struct record *) kmalloc(sizeof(struct record), GFP_KERNEL)) == NULL) {
		ERROR();
		emu10k1_wavein_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	buffsize = wiinst->fragment_size * wiinst->numfrags;

	if (get_recbuffer(card, wave_in, &buffsize) != CTSTATUS_SUCCESS) {
		ERROR();
		emu10k1_wavein_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	wiinst->fragment_size = buffsize / wiinst->numfrags;

	/* This callback size returned is the size in the play buffer.
	 * For 8-bit samples, callbacksize of user buffer should be
	 * half of the callbacksize in play buffer. */
	if (wave_in->wave_fmt.bitsperchannel == 8)
		wiinst->fragment_size >>= 1;

	wave_in->callbacksize = wiinst->fragment_size;

	emu10k1_set_record_src(wave_in->rec_ptr, wiinst->recsrc);

	bytespersec = wave_in->wave_fmt.channels * (wave_in->wave_fmt.bitsperchannel >> 3) * (wave_in->wave_fmt.samplingrate);
	delay = (48000 * wave_in->callbacksize) / bytespersec;

	if ((wave_in->timer = emu10k1_timer_install(card, emu10k1_wavein_bh, (unsigned long) wave_dev, delay / 2)) == NULL) {
		ERROR();
		emu10k1_wavein_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	return CTSTATUS_SUCCESS;
}

void emu10k1_wavein_close(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wave_in *wave_in = wave_dev->wiinst->wave_in;
	unsigned long flags;

	if (wave_in->state != CARDWAVE_STATE_STOPPED)
		emu10k1_wavein_stop(wave_dev);

	if (wave_in->timer != NULL)
		emu10k1_timer_uninstall(card, wave_in->timer);

	if (wave_in->memhandle != NULL)
		dealloc_recbuffer(wave_in);

	if (wave_in->rec_ptr != NULL)
		kfree(wave_in->rec_ptr);

	spin_lock_irqsave(&card->lock, flags);
	switch (wave_dev->wiinst->recsrc) {
	case WAVERECORD_AC97:
		card->wavein->ac97 = NULL;
		break;
	case WAVERECORD_MIC:
		card->wavein->mic = NULL;
		break;
	case WAVERECORD_FX:
		card->wavein->fx = NULL;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&card->lock, flags);

	kfree(wave_in);
	wave_dev->wiinst->wave_in = NULL;

	return;
}

void emu10k1_wavein_start(struct emu10k1_wavedevice *wave_dev)
{
	struct wave_in *wave_in = wave_dev->wiinst->wave_in;

	DPF(2, "emu10k1_wavein_start()\n");

	if (wave_in->state == CARDWAVE_STATE_STARTED)
		return;

	emu10k1_start_record(wave_in->rec_ptr);
	wave_in->state = CARDWAVE_STATE_STARTED;

	emu10k1_timer_enable(wave_dev->card, wave_in->timer);

	return;
}

void emu10k1_wavein_stop(struct emu10k1_wavedevice *wave_dev)
{
	struct wave_in *wave_in = wave_dev->wiinst->wave_in;

	DPF(2, "emu10k1_wavein_stop()\n");

	emu10k1_stop_record(wave_in->rec_ptr);
	emu10k1_timer_disable(wave_dev->card, wave_in->timer);

	wave_in->rec_ptr->recpos = 0;
	wave_in->state = CARDWAVE_STATE_STOPPED;

	return;
}

int emu10k1_wavein_setformat(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wiinst *wiinst = wave_dev->wiinst;
	struct wave_in *wave_in = wiinst->wave_in;
	u32 bytespersec, delay;

	DPF(2, "emu10k1_wavein_setformat()\n");

	query_format(wiinst->recsrc, &wiinst->wave_fmt);

	if (!wave_in)
		return CTSTATUS_SUCCESS;

	if (wave_in->state == CARDWAVE_STATE_STARTED) {
		wiinst->wave_fmt = wave_in->wave_fmt;
		return CTSTATUS_SUCCESS;
	}

	if ((wave_in->wave_fmt.samplingrate != wiinst->wave_fmt.samplingrate)
	    || (wave_in->wave_fmt.bitsperchannel != wiinst->wave_fmt.bitsperchannel)
	    || (wave_in->wave_fmt.channels != wiinst->wave_fmt.channels)) {

		emu10k1_timer_uninstall(card, wave_in->timer);

		wave_in->wave_fmt = wiinst->wave_fmt;

		bytespersec = wave_in->wave_fmt.channels * (wave_in->wave_fmt.bitsperchannel >> 3) * (wave_in->wave_fmt.samplingrate);
		delay = (48000 * wave_in->callbacksize) / bytespersec;

		if ((wave_in->timer = emu10k1_timer_install(card, emu10k1_wavein_bh, (unsigned long) wave_dev, delay / 2)) == NULL) {
			ERROR();
			emu10k1_wavein_close(wave_dev);
			return CTSTATUS_ERROR;
		}
	}

	return CTSTATUS_SUCCESS;
}

void emu10k1_wavein_getxfersize(struct wave_in *wave_in, u32 * size, u32 * curpos)
{
	struct record *rec_ptr = wave_in->rec_ptr;

	/* Get position of current address, this is in no. of bytes in play buffer */
	emu10k1_wavein_getcontrol(wave_in, WAVECURPOS, curpos);

	*size = *curpos - rec_ptr->recpos;

	/* Recpos is the actual position in user buffer and play buffer */
	if (*curpos < rec_ptr->recpos)
		*size += rec_ptr->recbufsize;

	if (!rec_ptr->is_16bit)
		*size >>= 1;

	return;
}

static void copy_s16_to_u8(u8 * dstbuf, s16 * srcbuf, u32 size)
{
	u16 sample;
	u8 byte;

	while (size--) {
		sample = (*srcbuf) + 32767;
		byte = (u8) (sample >> 8);
		copy_to_user(dstbuf, &byte, 1);
		dstbuf++;
		srcbuf++;
	}
}

/* transfer the data from the wave device.                    */
void emu10k1_wavein_xferdata(struct wiinst *wiinst, u8 * data, u32 * size)
{
	struct wave_in *wave_in = wiinst->wave_in;
	struct record *rec_ptr = wave_in->rec_ptr;
	u32 sizetocopy, sizetocopy_now, start;
	unsigned long flags;

	sizetocopy = min(rec_ptr->recbufsize * (rec_ptr->is_16bit + 1) / 2, *size);
	*size = sizetocopy;

	if (!sizetocopy)
		return;

	spin_lock_irqsave(&wiinst->lock, flags);

	sizetocopy_now = (rec_ptr->recbufsize - rec_ptr->recpos) * (rec_ptr->is_16bit + 1) / 2;

	start = rec_ptr->recpos;

	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;
		rec_ptr->recpos = sizetocopy * 2 / (rec_ptr->is_16bit + 1);

		spin_unlock_irqrestore(&wiinst->lock, flags);

		if (rec_ptr->is_16bit) {
			copy_to_user(data, rec_ptr->recbuffer + start, sizetocopy_now);
			copy_to_user(data + sizetocopy_now, rec_ptr->recbuffer, sizetocopy);
		} else {
			copy_s16_to_u8(data, (s16 *) (rec_ptr->recbuffer + start), sizetocopy_now);
			copy_s16_to_u8(data + sizetocopy_now, (s16 *) rec_ptr->recbuffer, sizetocopy);
		}
	} else {
		if (sizetocopy == sizetocopy_now)
			rec_ptr->recpos = 0;
		else
			rec_ptr->recpos += sizetocopy * 2 / (rec_ptr->is_16bit + 1);

		spin_unlock_irqrestore(&wiinst->lock, flags);

		if (rec_ptr->is_16bit)
			copy_to_user(data, rec_ptr->recbuffer + start, sizetocopy);
		else
			copy_s16_to_u8(data, (s16 *) (rec_ptr->recbuffer + start), sizetocopy);
	}

	return;
}

/* get the specified control value of the wave device. */

int emu10k1_wavein_getcontrol(struct wave_in *wave_in, u32 ctrlid, u32 * value)
{
	switch (ctrlid) {
	case WAVECURPOS:
		/* There is no actual start yet */
		if (wave_in->state == CARDWAVE_STATE_STOPPED) {
			*value = 0;
		} else {
			/* value is in byte units */
			*value = sblive_readptr(wave_in->rec_ptr->card, wave_in->rec_ptr->bufidxreg, 0);
		}

		break;

	default:
		return CTSTATUS_ERROR;
	}

	return CTSTATUS_SUCCESS;
}
