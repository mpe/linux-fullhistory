/*
 **********************************************************************
 *     cardwi.h -- header file for card wave input functions
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
#ifndef _CARDWI_H
#define _CARDWI_H

#include "icardwav.h"

struct wave_in 
{
	struct list_head list;

	u32 state;
	struct record *rec_ptr;
	struct memhandle *memhandle;
	struct emu_timer *timer;
	u32 callbacksize;
	struct wave_format wave_fmt;
};

struct wiinst
{
	struct wave_in *wave_in;
	struct wave_format wave_fmt;
	u16 ossfragshift;
	u32 fragment_size;
	u32 numfrags;
	wait_queue_head_t wait_queue;
	int mapped;
	u32 total_recorded;
	u32 blocks;
	u32 curpos;
	spinlock_t lock;
	u8 recsrc;
};

struct emu10k1_wavein 
{
	struct wave_in *ac97;
	struct wave_in *mic;
	struct wave_in *fx;

	u8 recsrc;
};


#define WAVEIN_MAXBUFSIZE         65536
#define WAVEIN_MINBUFSIZE	  368

#define WAVEIN_DEFAULTFRAGLEN     100 
#define WAVEIN_DEFAULTBUFLEN      1000

#define WAVEIN_MINFRAGSHIFT   	  8 

int emu10k1_wavein_open(struct emu10k1_wavedevice *);
void emu10k1_wavein_close(struct emu10k1_wavedevice *);
void emu10k1_wavein_start(struct emu10k1_wavedevice *);
void emu10k1_wavein_stop(struct emu10k1_wavedevice *);
void emu10k1_wavein_getxfersize(struct wave_in *, u32 *, u32 *);
void emu10k1_wavein_xferdata(struct wiinst *, u8 *, u32 *);
int emu10k1_wavein_setformat(struct emu10k1_wavedevice *);
int emu10k1_wavein_getcontrol(struct wave_in *, u32, u32 *);


#endif /* _CARDWI_H */
