
/*
 **********************************************************************
 *     voicemgr.c - Voice manager for emu10k1 driver
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

struct emu_voice *emu10k1_voice_alloc(struct voice_manager *voicemgr, struct voice_allocdesc *voiceallocdesc)
{
	struct emu10k1_card *card = voicemgr->card;
	struct emu_voice *voice_tmp = voicemgr->voice;
	struct emu_voice *voice = NULL;
	int i;
	unsigned long flags;

	DPF(2, "emu10k1_voice_alloc()\n");

	spin_lock_irqsave(&voicemgr->lock, flags);

	if (voiceallocdesc->flags & VOICEMGR_FLAGS_MONO) {
		for (i = 0; i < NUM_G; i++)
			if (voice_tmp[i].usage == VOICEMGR_USAGE_FREE) {
				voice_tmp[i].flags = VOICEMGR_FLAGS_VOICEMASTER | voiceallocdesc->flags;
				voice_tmp[i].usage = voiceallocdesc->usage;
				voice = &voice_tmp[i];
				break;
			}
	} else {
		for (i = 0; i < NUM_G; i += 2)
			if ((voice_tmp[i].usage == VOICEMGR_USAGE_FREE)
			    && (voice_tmp[i + 1].usage == VOICEMGR_USAGE_FREE)) {
				voice_tmp[i].linked_voice = &voice_tmp[i + 1];
				voice_tmp[i].flags = VOICEMGR_FLAGS_VOICEMASTER | voiceallocdesc->flags;
				voice_tmp[i].usage = voiceallocdesc->usage;
				voice_tmp[i + 1].flags = VOICEMGR_FLAGS_STEREOSLAVE | voiceallocdesc->flags;
				voice_tmp[i + 1].usage = voiceallocdesc->usage;
				voice = &voice_tmp[i];
				break;
			}
	}

	spin_unlock_irqrestore(&voicemgr->lock, flags);

	voice_tmp = voice;

	while (voice_tmp != NULL) {

		DPD(2, " voice allocated -> %d\n", voice_tmp->num);

		sblive_writeptr(card, IFATN, voice_tmp->num, 0xffff);
		sblive_writeptr(card, DCYSUSV, voice_tmp->num, ENV_OFF);
		sblive_writeptr(card, VTFT, voice_tmp->num, 0xffff);
		sblive_writeptr(card, PTRX, voice_tmp->num, 0);

		voice_tmp = voice_tmp->linked_voice;
	}

	return voice;
}

void emu10k1_voice_free(struct voice_manager *voicemgr, struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	struct emu_voice *voice_tmp;
	unsigned dcysusv;
	u32 cra, sample;
	int i;
	unsigned long flags;

	DPF(2, "emu10k1_voice_free()\n");

	voice_tmp = voice;

	while (voice_tmp != NULL) {

		DPD(2, " voice freed -> %d\n", voice_tmp->num);

		sblive_writeptr(card, IFATN, voice_tmp->num, IFATN_FILTERCUTOFF_MASK | IFATN_ATTENUATION_MASK);
		sblive_writeptr(card, IP, voice_tmp->num, 0);

		dcysusv = sblive_readptr(card, DCYSUSV, voice_tmp->num) & (DCYSUSV_PHASE1_MASK | DCYSUSV_SUSTAINLEVEL_MASK | DCYSUSV_DECAYTIME_MASK);
		sblive_writeptr(card, DCYSUSV, voice_tmp->num, dcysusv | ENV_OFF);

		sblive_writeptr(card, VTFT, voice_tmp->num, VTFT_FILTERTARGET_MASK);
		sblive_writeptr(card, PTRX_PITCHTARGET, voice_tmp->num, 0);
		sblive_writeptr(card, CVCF, voice_tmp->num, CVCF_CURRENTFILTER_MASK);
		sblive_writeptr(card, CPF, voice_tmp->num, 0);

		sample = (voice_tmp->flags & VOICEMGR_FLAGS_16BIT) ? 0 : 0x80808080;
		cra = sblive_readptr(card, CCR, voice_tmp->num) & CCR_READADDRESS_MASK;
		sblive_writeptr(card, CCR, voice_tmp->num, cra);
		cra = (cra >> 18) & 0xf;
		sblive_writeptr(card, CD0 + cra, voice_tmp->num, sample);
		cra = (cra + 0x1) & 0xf;
		sblive_writeptr(card, CD0 + cra, voice_tmp->num, sample);

		for (i = 0; i < NUM_FXSENDS; i++)
			voice_tmp->sendhandle[i] = 0;

		voice_tmp->flags = 0;

		spin_lock_irqsave(&voicemgr->lock, flags);
		voice_tmp->usage = VOICEMGR_USAGE_FREE;

		voice_tmp = voice_tmp->linked_voice;
		voice->linked_voice = NULL;
		spin_unlock_irqrestore(&voicemgr->lock, flags);
	}

	return;
}

/*       Sets up a voices for Wave Playback */

void emu10k1_voice_playback_setup(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	u32 sample, cra = 0, start = 0;

	DPF(2, "emu10k1_voice_playback_setup()\n");

	while (voice != NULL) {
		sblive_writeptr(card, DCYSUSV, voice->num, ENV_OFF);
		sblive_writeptr(card, VTFT, voice->num, VTFT_FILTERTARGET_MASK);
		sblive_writeptr(card, CVCF, voice->num, CVCF_CURRENTFILTER_MASK);
		sblive_writeptr(card, FXRT, voice->num, (voice->flags & VOICEMGR_FLAGS_FXRT2) ? 0xd23c0000 : 0xd01c0000);

		/* Stop CA */
		/* Assumption that PT is alreadt 0 so no harm overwriting */
		sblive_writeptr(card, PTRX, voice->num, (voice->params.send_a << 8) | voice->params.send_b);

		if (voice->flags & VOICEMGR_FLAGS_VOICEMASTER) {
			if (voice->linked_voice != NULL) {
				/* Set stereo bit */
				cra = 64;
				sblive_writeptr(card, CPF, voice->num, CPF_STEREO_MASK);
				sblive_writeptr(card, CPF, voice->num + 1, CPF_STEREO_MASK);
			} else {
				cra = 32;
				sblive_writeptr(card, CPF, voice->num, 0);
			}

			if (voice->flags & VOICEMGR_FLAGS_16BIT)
				sample = 0;
			else {
				cra = cra * 2;
				sample = 0x80808080;
			}
			cra -= 4;

			if (voice->linked_voice != NULL) {
				/* CCR_READADDRESS_MASK */
				sblive_writeptr(card, CCR, voice->num, 0x3c << 16);
				sblive_writeptr(card, CCR, voice->num + 1, cra << 16);
				sblive_writeptr(card, CDE, voice->num + 1, sample);
				sblive_writeptr(card, CDF, voice->num + 1, sample);
				start = voice->params.start + cra / 2;
			} else {
				sblive_writeptr(card, CCR, voice->num, 0x1c << 16);	/* FIXME: Is 0x1c correct? */
				sblive_writeptr(card, CDE, voice->num, sample);
				sblive_writeptr(card, CDF, voice->num, sample);
				start = voice->params.start + cra;
			}

			if (start > voice->params.endloop) {
				start -= voice->params.endloop;

				if (voice->linked_voice != NULL)
					cra = (cra << 25) | 0x1bc0000 | ((cra - start) << 9);
				else
					cra = (cra << 25) | 0x11c0000 | ((cra - start) << 9);

				start += voice->params.startloop;

				if (start >= voice->params.endloop)
					start = voice->params.endloop - 1;
			} else if (voice->linked_voice != NULL)
				cra = (cra << 25) | (0x3c << 16);
			else
				cra = (cra << 25) | (0x1c << 16);

			start |= CCCA_INTERPROM_0;
		}

		/* CSL, ST, CA */
		sblive_writeptr(card, DSL, voice->num, voice->params.endloop | (voice->params.send_d << 24));
		sblive_writeptr(card, PSST, voice->num, voice->params.startloop | (voice->params.send_c << 24));

		if (voice->flags & VOICEMGR_FLAGS_16BIT)
			sblive_writeptr(card, CCCA, voice->num, start);
		else
			sblive_writeptr(card, CCCA, voice->num, start | CCCA_8BITSELECT);

		/* Clear filter delay memory */
		sblive_writeptr(card, Z1, voice->num, 0);
		sblive_writeptr(card, Z2, voice->num, 0);

		/* Invalidate maps */
		sblive_writeptr(card, MAPA, voice->num, MAP_PTI_MASK | (card->silentpage->busaddx * 2));
		sblive_writeptr(card, MAPB, voice->num, MAP_PTI_MASK | (card->silentpage->busaddx * 2));

		/* Fill cache */
		if (voice->flags & VOICEMGR_FLAGS_VOICEMASTER)
			sblive_writeptr(card, CCR, voice->num, cra);

		sblive_writeptr(card, ATKHLDV, voice->num, ATKHLDV_HOLDTIME_MASK | ATKHLDV_ATTACKTIME_MASK);
		sblive_writeptr(card, LFOVAL1, voice->num, 0x8000);
		sblive_writeptr(card, ATKHLDM, voice->num, 0);
		sblive_writeptr(card, DCYSUSM, voice->num, DCYSUSM_DECAYTIME_MASK);
		sblive_writeptr(card, LFOVAL2, voice->num, 0x8000);
		sblive_writeptr(card, IP, voice->num, voice->params.initial_pitch);
		sblive_writeptr(card, PEFE, voice->num, 0x7f);
		sblive_writeptr(card, FMMOD, voice->num, 0);
		sblive_writeptr(card, TREMFRQ, voice->num, 0);
		sblive_writeptr(card, FM2FRQ2, voice->num, 0);
		sblive_writeptr(card, ENVVAL, voice->num, 0xbfff);
		sblive_writeptr(card, ENVVOL, voice->num, 0xbfff);

#ifdef PRIVATE_PCM_VOLUME
		{
			int i;

			for (i = 0; i < MAX_PCM_CHANNELS; i++) {
				if (sblive_pcm_volume[i].channel_l == voice->num) {
					voice->params.initial_attn = (sblive_pcm_volume[i].channel_r < NUM_G) ? sblive_pcm_volume[i].attn_l :
					    // test for mono channel (reverse logic is correct here!)
					    (sblive_pcm_volume[i].attn_r >
					     sblive_pcm_volume[i].attn_l) ? sblive_pcm_volume[i].attn_l : sblive_pcm_volume[i].attn_r;
					DPD(2, "set left volume  %d\n", voice->params.initial_attn);
					break;
				} else if (sblive_pcm_volume[i].channel_r == voice->num) {
					voice->params.initial_attn = sblive_pcm_volume[i].attn_r;
					DPD(2, "set right volume  %d\n", voice->params.initial_attn);
					break;
				}
			}
		}
#endif
		sblive_writeptr(card, IFATN, voice->num, IFATN_FILTERCUTOFF_MASK | voice->params.initial_attn);

		voice->params.FC_target = 0xffff;
		voice->params.pitch_target = (u16) (IP_TO_CP(voice->params.initial_pitch) >> 16);

		voice = voice->linked_voice;
	}

	return;
}

void emu10k1_voice_start(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;

	DPF(2, "emu10k1_voice_start()\n");

	while (voice != NULL) {
		sblive_writeptr(card, PTRX_PITCHTARGET, voice->num, voice->params.pitch_target);

		if (voice->flags & VOICEMGR_FLAGS_VOICEMASTER)
			sblive_writeptr(card, CPF_CURRENTPITCH, voice->num, voice->params.pitch_target);

		sblive_writeptr(card, VTFT, voice->num, ((u32) voice->params.volume_target << 16)
				| voice->params.FC_target);
		sblive_writeptr(card, CVCF, voice->num, ((u32) voice->params.volume_target << 16)
				| voice->params.FC_target);
		sblive_writeptr(card, DCYSUSV, voice->num, (voice->params.byampl_env_sustain << 8)
				| ENV_ON | voice->params.byampl_env_decay);

		/* Using StopOnLoop for MIDI stops the playback
		   too early, which may cause a DC level to be played
		   until the note is released. */

		if (voice->usage == VOICEMGR_USAGE_MIDI)
			emu10k1_clear_stop_on_loop(card, voice->num);
		else {
			if (voice->params.startloop > voice->params.end)
				emu10k1_set_stop_on_loop(card, voice->num);
			else
				emu10k1_clear_stop_on_loop(card, voice->num);
		}
		voice = voice->linked_voice;
	}

	return;
}

void emu10k1_voice_stop(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;

	DPF(2, "emu10k1_voice_stop()\n");

	while (voice != NULL) {
		sblive_writeptr(card, IFATN, voice->num, 0xffff);
		sblive_writeptr(card, IP, voice->num, 0);
		sblive_writeptr(card, VTFT, voice->num, 0xffff);
		sblive_writeptr(card, PTRX_PITCHTARGET, voice->num, 0);
		voice = voice->linked_voice;
	}

	return;
}

void emu10k1_voice_setcontrol(struct emu_voice *voice, struct voice_cntlset *setting, u32 numparam)
{
	struct emu10k1_card *card = voice->card;
	int count;

	for (count = 0; count < numparam; count++)
		sblive_writeptr(card, setting[count].paramID, voice->num, setting[count].value);

	return;
}

void emu10k1_voice_getcontrol(struct emu_voice *voice, u32 controlid, u32 * value)
{
	struct emu10k1_card *card = voice->card;

	*value = sblive_readptr(card, controlid, voice->num);

	return;
}
