
/*
 **********************************************************************
 *     recmgr.c -- Recording manager for emu10k1 driver
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

void emu10k1_start_record(struct record *rec_ptr)
{
	struct emu10k1_card *hw_ptr = rec_ptr->card;

	DPF(2, "emu10k1_start_record()\n");
	DPD(2, "bus addx: %lx\n", rec_ptr->busaddx);

	sblive_writeptr(hw_ptr, rec_ptr->bufaddrreg, 0, rec_ptr->busaddx);
	sblive_writeptr(hw_ptr, rec_ptr->bufsizereg, 0, rec_ptr->bufsize);

	if (rec_ptr->adcctl)
		sblive_writeptr(hw_ptr, ADCCR, 0, rec_ptr->adcctl);

	return;
}

void emu10k1_stop_record(struct record *rec_ptr)
{
	struct emu10k1_card *hw_ptr = rec_ptr->card;

	DPF(2, "emu10k1_stop_record()\n");

	/* Disable record transfer */
	if (rec_ptr->adcctl)
		sblive_writeptr(hw_ptr, ADCCR, 0, 0);

	sblive_writeptr(hw_ptr, rec_ptr->bufsizereg, 0, ADCBS_BUFSIZE_NONE);

	return;
}

void emu10k1_set_record_src(struct record *rec_ptr, u8 recsrc)
{
	DPF(2, "emu10k1_set_record_src()\n");

	switch (recsrc) {

	case WAVERECORD_AC97:
		DPF(2, "recording source: AC97\n");
		rec_ptr->bufsizereg = ADCBS;
		rec_ptr->bufaddrreg = ADCBA;
		rec_ptr->bufidxreg = ADCIDX_IDX;

		switch (rec_ptr->samplingrate) {
		case 0xBB80:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_48;
			break;
		case 0xAC44:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_44;
			break;
		case 0x7D00:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_32;
			break;
		case 0x5DC0:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_24;
			break;
		case 0x5622:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_22;
			break;
		case 0x3E80:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_16;
			break;
		case 0x2B11:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_11;
			break;
		case 0x1F40:
			rec_ptr->adcctl = ADCCR_SAMPLERATE_8;
			break;
		default:
			break;
		}

		rec_ptr->adcctl |= ADCCR_LCHANENABLE;

		if (rec_ptr->is_stereo)
			rec_ptr->adcctl |= ADCCR_RCHANENABLE;

		//      rec_ptr->fxwc = 0;

		break;

	case WAVERECORD_MIC:
		DPF(2, "recording source: MIC\n");
		rec_ptr->bufsizereg = MICBS;
		rec_ptr->bufaddrreg = MICBA;
		rec_ptr->bufidxreg = MICIDX_IDX;
		rec_ptr->adcctl = 0;
		//      rec_ptr->fxwc = 0;
		break;

	case WAVERECORD_FX:
		DPF(2, "recording source: FX\n");
		rec_ptr->bufsizereg = FXBS;
		rec_ptr->bufaddrreg = FXBA;
		rec_ptr->bufidxreg = FXIDX_IDX;
		rec_ptr->adcctl = 0;
		//      rec_ptr->fxwc = 0x000ffff;
		break;
	default:
		break;
	}

	return;
}
