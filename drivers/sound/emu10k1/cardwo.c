
/*
 **********************************************************************
 *     cardwo.c - PCM output HAL for emu10k1 driver
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
#include "cardwo.h"
#include "audio.h"

/* Volume calcs */

static int set_volume_instance(struct emu10k1_waveout *card_waveout, struct wave_out *wave_out, struct voice_param *left)
{
	/* only applicable for playback */
	u32 volL, volR, vol = 0;

	volL = (wave_out->localvol & 0xffff);
	volR = ((wave_out->localvol >> 16) & 0xffff);

	if (wave_out->globalvolFactor) {
		volL = ((u32) (((u16) card_waveout->globalvol & 0xffff) * (u16) volL)) / 0xffff;
		volR = ((u32) (((u16) (card_waveout->globalvol >> 16) & 0xffff) * ((u16) volR))) / 0xffff;
	}

	/* BIG ASSUMPTION HERE THAT DEFAULT WAVE PAN/AUX IS 0xff/0xff */
	/* New volume and pan */

	if (volL == volR) {
		vol = volL;
		left->send_c = 0xff;
		left->send_b = 0xff;
	} else {
		if (volL > volR) {
			vol = volL;
			left->send_c = 0xff;
			left->send_b = (char) ((volR * 255) / vol);
		} else {
			vol = volR;
			left->send_b = 0xff;
			left->send_c = (char) ((volL * 255) / vol);
		}
	}

	left->initial_attn = 0xff & sumVolumeToAttenuation(vol * 2);

	return vol;
}

static void query_format(struct wave_format *wave_fmt)
{
	if ((wave_fmt->channels != 1) && (wave_fmt->channels != 2))
		wave_fmt->channels = 2;

	if (wave_fmt->samplingrate >= 0x2EE00)
		wave_fmt->samplingrate = 0x2EE00;

	if ((wave_fmt->bitsperchannel != 8) && (wave_fmt->bitsperchannel != 16))
		wave_fmt->bitsperchannel = 16;

	return;
}

static int alloc_xferbuffer(struct emu10k1_card *card, struct wave_out *wave_out, u32 * size, void ***buffer)
{
	u32 numpages, reqsize, pageindex, pagecount;
	struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;
	unsigned long busaddx;
	int i;

	reqsize = *size;
	numpages = reqsize / PAGE_SIZE;

	/* If size is not a multiple of PAGE_SIZE then we need to round up */
	if (reqsize % PAGE_SIZE)
		numpages += 1;

	DPD(2, "requested pages is: %d\n", numpages);

	wavexferbuf->numpages = numpages;

	/* Only for playback, request for emu address space */
	/* Support non page-aligned buffer, don't need interpolation page */

	if ((wave_out->emupageindex = emu10k1_addxmgr_alloc(numpages * PAGE_SIZE, card)) < 0)
		return CTSTATUS_ERROR;

	if ((wave_out->pagetable = (void **) kmalloc(sizeof(void *) * numpages, GFP_KERNEL)) == NULL)
		return CTSTATUS_ERROR;

	/* Fill in virtual memory table */
	for (pagecount = 0; pagecount < numpages; pagecount++) {
		if ((wave_out->pagetable[pagecount] = (void *) __get_free_page(GFP_KERNEL)) == NULL) {
			wavexferbuf->numpages = pagecount;
			return CTSTATUS_ERROR;
		}

		DPD(2, "Virtual Addx: %p\n", wave_out->pagetable[pagecount]);

		for (i = 0; i < PAGE_SIZE / EMUPAGESIZE; i++) {
			busaddx = virt_to_bus((u8 *) wave_out->pagetable[pagecount] + i * EMUPAGESIZE);

			DPD(3, "Bus Addx: %lx\n", busaddx);

			pageindex = wave_out->emupageindex + pagecount * PAGE_SIZE / EMUPAGESIZE + i;

			((u32 *) card->virtualpagetable->virtaddx)[pageindex] = ((u32) busaddx * 2) | pageindex;
		}
	}

	*buffer = wave_out->pagetable;

	return CTSTATUS_SUCCESS;
}

static int get_xferbuffer(struct emu10k1_card *card, struct wave_out *wave_out, u32 * size)
{
	struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;
	void **buffer;

	wavexferbuf->xferpos = 0;
	wavexferbuf->silence_xferpos = 0;
	wavexferbuf->stopposition = 0;
	wavexferbuf->is_stereo = (wave_out->wave_fmt.channels == 2) ? 1 : 0;
	wavexferbuf->is_16bit = (wave_out->wave_fmt.bitsperchannel == 16) ? 1 : 0;
	wavexferbuf->bytespersample = (wavexferbuf->is_stereo + 1) * (wavexferbuf->is_16bit + 1);

	if (alloc_xferbuffer(card, wave_out, size, &buffer) != CTSTATUS_SUCCESS)
		return CTSTATUS_ERROR;

	/* xferbufsize contains actual transfer buffer size */
	wavexferbuf->xferbufsize = *size;
	wavexferbuf->xferbuffer = buffer;

	return CTSTATUS_SUCCESS;
}

static void dealloc_xferbuffer(struct emu10k1_card *card, struct wave_out *wave_out)
{
	u32 pagecount, pageindex;
	int i;

	if (wave_out->pagetable != NULL) {
		for (pagecount = 0; pagecount < wave_out->wavexferbuf->numpages; pagecount++) {
			free_page((unsigned long) wave_out->pagetable[pagecount]);

			for (i = 0; i < PAGE_SIZE / EMUPAGESIZE; i++) {
				pageindex = wave_out->emupageindex + pagecount * PAGE_SIZE / EMUPAGESIZE + i;
				((u32 *) card->virtualpagetable->virtaddx)[pageindex] = (card->silentpage->busaddx * 2) | pageindex;
			}
		}
		kfree(wave_out->pagetable);
	}

	emu10k1_addxmgr_free(card, wave_out->emupageindex);

	return;
}

static int get_voice(struct emu10k1_card *card, struct wave_out *wave_out, int device)
{
	struct emu10k1_waveout *card_waveout = card->waveout;
	struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;
	struct voice_allocdesc voice_allocdesc;
	struct voice_param *left, *right;
	u32 size;

	/* Allocate voices here, if no voices available, return error.
	 * Init voice_allocdesc first.*/

	voice_allocdesc.usage = VOICEMGR_USAGE_PLAYBACK;

	voice_allocdesc.flags = 0;

	if (device == 1)
		voice_allocdesc.flags |= VOICEMGR_FLAGS_FXRT2;

	if (wave_out->wave_fmt.channels == 1)
		voice_allocdesc.flags |= VOICEMGR_FLAGS_MONO;

	if (wave_out->wave_fmt.bitsperchannel == 16)
		voice_allocdesc.flags |= VOICEMGR_FLAGS_16BIT;

	if ((wave_out->voice = emu10k1_voice_alloc(&card->voicemgr, &voice_allocdesc)) == NULL)
		return CTSTATUS_ERROR;

	/* voice initialization */

	left = &wave_out->voice->params;

	/* Calculate pitch */
	left->initial_pitch = (u16) (srToPitch(wave_out->wave_fmt.samplingrate) >> 8);

	DPD(2, "Initial pitch --> %x\n", left->initial_pitch);

	/* Easy way out.. gotta calculate value */
	left->pitch_target = 0;
	left->volume_target = 0;
	left->FC_target = 0;

	left->byampl_env_sustain = 0x7f;
	left->byampl_env_decay = 0x7f;

	if (wave_out->globalreverbFactor) {
		u8 t = (card_waveout->globalreverb & 0xff) + (wave_out->localreverb & 0xff);

		left->send_a = (t > 255) ? 255 : t;
	} else {
		left->send_a = 0;
	}

	if (wave_out->globalchorusFactor) {
		u8 t = (card_waveout->globalchorus & 0xff) + (wave_out->localchorus & 0xff);

		left->send_d = (t > 255) ? 255 : t;
	} else {
		left->send_d = 0;
	}

	set_volume_instance(card_waveout, wave_out, left);

	left->pan_target = left->send_c;
	left->aux_target = left->send_b;

	size = wavexferbuf->xferbufsize / wavexferbuf->bytespersample;
	left->start = 2 * (wave_out->emupageindex << 11) / wavexferbuf->bytespersample;
	left->end = left->start + size;
	left->startloop = left->start;
	left->endloop = left->end;

	if (wave_out->voice->linked_voice) {
		DPF(2, "is stereo\n");
		right = &wave_out->voice->linked_voice->params;

		right->initial_pitch = left->initial_pitch;

		/* Easy way out.. gotta calculate value */
		right->pitch_target = 0;
		right->volume_target = 0;
		right->FC_target = 0;

		right->byampl_env_sustain = 0x7f;
		right->byampl_env_decay = 0x7f;

		right->send_d = left->send_d;
		right->send_a = left->send_a;

		/* Left output of right channel is always zero */
		right->send_c = 0;

		/* Update right channel aux */
		right->pan_target = 0;
		right->send_b = left->send_b;
		right->aux_target = right->send_b;

		/* Zero out right output of left channel */
		left->send_b = 0;
		left->aux_target = 0;

		/* Update right channel attenuation */
		right->initial_attn = left->initial_attn;

		right->start = left->start;
		right->end = left->end;
		right->startloop = left->startloop;
		right->endloop = left->endloop;

	}

	DPD(2, "voice: start=%x, end=%x, startloop=%x, endloop=%x\n", left->start, left->end, left->startloop, left->endloop);

	return CTSTATUS_SUCCESS;
}

int emu10k1_waveout_open(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	struct wave_out *wave_out;
	u32 bytespersec, delay;
	u32 buffsize;

	DPF(2, "emu10k1_waveout_open()\n");

	if ((wave_out = (struct wave_out *) kmalloc(sizeof(struct wave_out), GFP_KERNEL)) == NULL) {
		ERROR();
		emu10k1_waveout_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	woinst->wave_out = wave_out;

	/* Init channel object */
	wave_out->state = CARDWAVE_STATE_STOPPED;
	wave_out->wave_fmt = woinst->wave_fmt;
	wave_out->voice = NULL;
	wave_out->emupageindex = -1;
	wave_out->wavexferbuf = NULL;
	wave_out->pagetable = NULL;
	wave_out->timer = NULL;

	/* Assign default local volume */
	/* FIXME: Should we be maxing the initial values like this? */
	wave_out->localvol = 0xffffffff;
	wave_out->localreverb = 0xffffffff;
	wave_out->localchorus = 0xffffffff;
	wave_out->globalvolFactor = 0xffff;
	wave_out->globalreverbFactor = 0xffff;
	wave_out->globalchorusFactor = 0xffff;

	wave_out->setpos = 0;
	wave_out->position = 0;

	wave_out->fill_silence = 0;

	if ((wave_out->wavexferbuf = (struct wave_xferbuf *) kmalloc(sizeof(struct wave_xferbuf), GFP_KERNEL)) == NULL) {
		ERROR();
		emu10k1_waveout_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	buffsize = woinst->fragment_size * woinst->numfrags;

	if (get_xferbuffer(card, wave_out, &buffsize) != CTSTATUS_SUCCESS) {
		ERROR();
		emu10k1_waveout_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	woinst->fragment_size = buffsize / woinst->numfrags;
	wave_out->callbacksize = woinst->fragment_size;

	if (get_voice(card, wave_out, woinst->device) != CTSTATUS_SUCCESS) {
		ERROR();
		emu10k1_waveout_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	bytespersec = wave_out->wave_fmt.channels * (wave_out->wave_fmt.bitsperchannel >> 3) * (wave_out->wave_fmt.samplingrate);
	delay = (48000 * wave_out->callbacksize) / bytespersec;

	if ((wave_out->timer = emu10k1_timer_install(card, emu10k1_waveout_bh, (unsigned long) wave_dev, delay / 2)) == NULL) {
		ERROR();
		emu10k1_waveout_close(wave_dev);
		return CTSTATUS_ERROR;
	}

	return CTSTATUS_SUCCESS;
}

void emu10k1_waveout_close(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wave_out *wave_out = wave_dev->woinst->wave_out;

	DPF(2, "emu10k1_waveout_close()\n");

	if (wave_out->state != CARDWAVE_STATE_STOPPED)
		emu10k1_waveout_stop(wave_dev);

	if (wave_out->timer != NULL)
		emu10k1_timer_uninstall(card, wave_out->timer);

	if (wave_out->voice != NULL)
		emu10k1_voice_free(&card->voicemgr, wave_out->voice);

	if (wave_out->emupageindex >= 0)
		dealloc_xferbuffer(card, wave_out);

	if (wave_out->wavexferbuf != NULL)
		kfree(wave_out->wavexferbuf);

	kfree(wave_out);
	wave_dev->woinst->wave_out = NULL;

	return;
}

int emu10k1_waveout_start(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wave_out *wave_out = wave_dev->woinst->wave_out;
	u32 start, startPosition;

	DPF(2, "emu10k1_waveout_start()\n");

	/* If already started, return success */
	if (wave_out->state == CARDWAVE_STATE_STARTED)
		return CTSTATUS_SUCCESS;

	if (wave_out->state == CARDWAVE_STATE_STOPPED && wave_out->setpos)
		startPosition = wave_out->position / (wave_out->wavexferbuf->bytespersample);
	else
		startPosition = wave_out->wavexferbuf->stopposition;

	start = wave_out->voice->params.start;
	wave_out->voice->params.start += startPosition;

	DPD(2, "CA is %x\n", wave_out->voice->params.start);

	emu10k1_voice_playback_setup(wave_out->voice);

	wave_out->voice->params.start = start;

	/* Actual start */
	emu10k1_voice_start(wave_out->voice);

	wave_out->state = CARDWAVE_STATE_STARTED;
	wave_out->setpos = 0;

	emu10k1_timer_enable(card, wave_out->timer);

	return CTSTATUS_SUCCESS;
}

int emu10k1_waveout_setformat(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	struct wave_out *wave_out = woinst->wave_out;
	u32 bytespersec, delay;

	DPF(2, "emu10k1_waveout_setformat()\n");

	query_format(&woinst->wave_fmt);

	if (wave_out == NULL)
		return CTSTATUS_SUCCESS;

	if (wave_out->state == CARDWAVE_STATE_STARTED) {
		woinst->wave_fmt = wave_out->wave_fmt;
		return CTSTATUS_SUCCESS;
	}

	if ((wave_out->wave_fmt.samplingrate != woinst->wave_fmt.samplingrate)
	    || (wave_out->wave_fmt.bitsperchannel != woinst->wave_fmt.bitsperchannel)
	    || (wave_out->wave_fmt.channels != woinst->wave_fmt.channels)) {
		struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;

		emu10k1_timer_uninstall(card, wave_out->timer);

		emu10k1_voice_free(&card->voicemgr, wave_out->voice);

		wave_out->wave_fmt = woinst->wave_fmt;
		wave_out->timer = NULL;

		wavexferbuf->xferpos = 0;
		wavexferbuf->silence_xferpos = 0;
		wavexferbuf->stopposition = 0;
		wavexferbuf->is_stereo = (wave_out->wave_fmt.channels == 2) ? 1 : 0;
		wavexferbuf->is_16bit = (wave_out->wave_fmt.bitsperchannel == 16) ? 1 : 0;
		wavexferbuf->bytespersample = (wavexferbuf->is_stereo + 1) * (wavexferbuf->is_16bit + 1);

		if (get_voice(card, wave_out, woinst->device) != CTSTATUS_SUCCESS) {
			ERROR();
			emu10k1_waveout_close(wave_dev);
			return CTSTATUS_ERROR;
		}

		bytespersec = wave_out->wave_fmt.channels * (wave_out->wave_fmt.bitsperchannel >> 3) * (wave_out->wave_fmt.samplingrate);
		delay = (48000 * wave_out->callbacksize) / bytespersec;

		if ((wave_out->timer = emu10k1_timer_install(card, emu10k1_waveout_bh, (unsigned long) wave_dev, delay / 2)) == NULL) {
			ERROR();
			emu10k1_waveout_close(wave_dev);
			return CTSTATUS_ERROR;
		}
	}

	return CTSTATUS_SUCCESS;
}

void emu10k1_waveout_stop(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wave_out *wave_out = wave_dev->woinst->wave_out;
	struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;
	u32 samples = 32;
	u32 position;

	DPF(2, "emu10k1_waveout_stop()\n");

	if (wave_out->state == CARDWAVE_STATE_STOPPED)
		return;

	emu10k1_timer_disable(card, wave_out->timer);

	/* Stop actual voice */
	emu10k1_voice_stop(wave_out->voice);

	/* Save the stop position */
	emu10k1_voice_getcontrol(wave_out->voice, CCCA_CURRADDR, &wavexferbuf->stopposition);

	wavexferbuf->stopposition -= wave_out->voice->params.start;

	/* Refer to voicemgr.c, CA is not started at zero.  We need to take this into account. */
	position = wavexferbuf->stopposition * wavexferbuf->bytespersample;

	if (!wavexferbuf->is_16bit)
		samples <<= 1;

	if (wavexferbuf->is_stereo)
		samples <<= 1;

	samples -= 4;

	if (position >= samples * (wavexferbuf->is_16bit + 1))
		position -= samples * (wavexferbuf->is_16bit + 1);
	else
		position += wavexferbuf->xferbufsize - samples * (wavexferbuf->is_16bit + 1);

	wavexferbuf->stopposition = position / wavexferbuf->bytespersample;

	DPD(2, "position is %x\n", wavexferbuf->stopposition);

	wave_out->state = CARDWAVE_STATE_STOPPED;
	wave_out->setpos = 0;
	wave_out->position = 0;

	return;
}

void emu10k1_waveout_getxfersize(struct wave_out *wave_out, u32 * size, u32 * pending, u32 * curpos)
{
	struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;

	/* Get position of current address, this is in no. of bytes in play buffer */
	emu10k1_waveout_getcontrol(wave_out, WAVECURPOS, curpos);

	if ((*curpos > wavexferbuf->silence_xferpos)
	    || ((*curpos == wavexferbuf->silence_xferpos)
		&& (wave_out->state == CARDWAVE_STATE_STARTED))
	    || ((*curpos == wavexferbuf->silence_xferpos) && (wavexferbuf->silence_xferpos != 0)
		&& (wave_out->state == CARDWAVE_STATE_STOPPED))) {
		*size = *curpos - wavexferbuf->silence_xferpos;
		*pending = wavexferbuf->xferbufsize - *size;
	} else {
		*pending = wavexferbuf->silence_xferpos - *curpos;
		*size = wavexferbuf->xferbufsize - *pending;
	}

	if (wavexferbuf->silence_xferpos != wavexferbuf->xferpos) {
		if (*pending < wave_out->callbacksize) {
			wave_out->fill_silence = 2;
			*pending = 0;
			*size = wavexferbuf->xferbufsize;
			wavexferbuf->xferpos = *curpos;
		} else {
			if (wave_out->fill_silence == 2) {
				*pending = 0;
				*size = wavexferbuf->xferbufsize;
				wavexferbuf->xferpos = *curpos;
			} else {
				*pending -= wave_out->callbacksize;
				*size += wave_out->callbacksize;
			}
		}
	} else {
		if (*pending < wave_out->callbacksize)
			wave_out->fill_silence = 1;
		else
			wave_out->fill_silence = 0;
	}

	return;
}

static void copy_block(u32 dst, u8 * src, u32 len, void **pt)
{
	int i, j, k;

	i = dst / PAGE_SIZE;
	j = dst % PAGE_SIZE;
	k = (len > PAGE_SIZE - j) ? PAGE_SIZE - j : len;
	copy_from_user(pt[i] + j, src, k);
	len -= k;
	while (len >= PAGE_SIZE) {
		copy_from_user(pt[++i], src + k, PAGE_SIZE);
		k += PAGE_SIZE;
		len -= PAGE_SIZE;
	}
	copy_from_user(pt[++i], src + k, len);

	return;
}

static void fill_block(u32 dst, u8 val, u32 len, void **pt)
{
	int i, j, k;

	i = dst / PAGE_SIZE;
	j = dst % PAGE_SIZE;
	k = (len > PAGE_SIZE - j) ? PAGE_SIZE - j : len;
	memset(pt[i] + j, val, k);
	len -= k;
	while (len >= PAGE_SIZE) {
		memset(pt[++i], val, PAGE_SIZE);
		len -= PAGE_SIZE;
	}
	memset(pt[++i], val, len);

	return;
}

void emu10k1_waveout_xferdata(struct woinst *woinst, u8 * data, u32 * size)
{
	struct wave_out *wave_out = woinst->wave_out;
	struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;
	u32 sizetocopy, sizetocopy_now, start;
	unsigned long flags;

	sizetocopy = min(wavexferbuf->xferbufsize, *size);
	*size = sizetocopy;

	if (!sizetocopy)
		return;

	spin_lock_irqsave(&woinst->lock, flags);

	sizetocopy_now = wavexferbuf->xferbufsize - wavexferbuf->xferpos;

	start = wavexferbuf->xferpos;

	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;
		wavexferbuf->xferpos = sizetocopy;
		wavexferbuf->silence_xferpos = wavexferbuf->xferpos;
		spin_unlock_irqrestore(&woinst->lock, flags);

		copy_block(start, data, sizetocopy_now, wavexferbuf->xferbuffer);
		copy_block(0, data + sizetocopy_now, sizetocopy, wavexferbuf->xferbuffer);
	} else {
		if (sizetocopy == sizetocopy_now)
			wavexferbuf->xferpos = 0;
		else
			wavexferbuf->xferpos += sizetocopy;

		wavexferbuf->silence_xferpos = wavexferbuf->xferpos;
		spin_unlock_irqrestore(&woinst->lock, flags);

		copy_block(start, data, sizetocopy, wavexferbuf->xferbuffer);
	}

	return;
}

void emu10k1_waveout_fillsilence(struct woinst *woinst)
{
	struct wave_out *wave_out = woinst->wave_out;
	struct wave_xferbuf *wavexferbuf = wave_out->wavexferbuf;
	u16 filldata;
	u32 sizetocopy, sizetocopy_now, start;
	unsigned long flags;

	sizetocopy = wave_out->callbacksize;

	if (wave_out->wave_fmt.bitsperchannel == 8)
		filldata = 0x8080;
	else
		filldata = 0x0000;

	spin_lock_irqsave(&woinst->lock, flags);

	sizetocopy_now = wavexferbuf->xferbufsize - wavexferbuf->silence_xferpos;
	start = wavexferbuf->silence_xferpos;

	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;
		wavexferbuf->silence_xferpos = sizetocopy;
		spin_unlock_irqrestore(&woinst->lock, flags);
		fill_block(start, filldata, sizetocopy_now, wavexferbuf->xferbuffer);
		fill_block(0, filldata, sizetocopy, wavexferbuf->xferbuffer);
	} else {
		if (sizetocopy == sizetocopy_now)
			wavexferbuf->silence_xferpos = 0;
		else
			wavexferbuf->silence_xferpos += sizetocopy;

		spin_unlock_irqrestore(&woinst->lock, flags);

		fill_block(start, filldata, sizetocopy, wavexferbuf->xferbuffer);
	}

	return;
}

/* get the specified control value of the wave device. */

int emu10k1_waveout_getcontrol(struct wave_out *wave_out, u32 ctrl_id, u32 * value)
{
	switch (ctrl_id) {
	case WAVECURPOS:
		/* There is no actual start yet */
		if (wave_out->state == CARDWAVE_STATE_STOPPED) {
			if (wave_out->setpos)
				*value = wave_out->position;
			else
				*value = wave_out->wavexferbuf->stopposition * wave_out->wavexferbuf->bytespersample;
		} else {
			emu10k1_voice_getcontrol(wave_out->voice, CCCA_CURRADDR, value);

			*value -= wave_out->voice->params.start;

			/* Get number of bytes in play buffer per channel.
			 * If 8 bit mode is enabled, this needs to be changed. */
			{
				u32 samples = 64 * (wave_out->wavexferbuf->is_stereo + 1);

				*value *= wave_out->wavexferbuf->bytespersample;

				/* Refer to voicemgr.c, CA is not started at zero.
				 * We need to take this into account. */

				samples -= 4 * (wave_out->wavexferbuf->is_16bit + 1);

				if (*value >= samples)
					*value -= samples;
				else
					*value += wave_out->wavexferbuf->xferbufsize - samples;
			}
		}

		break;
	default:
		return CTSTATUS_ERROR;
	}

	return CTSTATUS_SUCCESS;
}
