/*     
 **********************************************************************
 *     cardwo.h -- header file for card wave out functions
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

#ifndef _CARDWO_H
#define _CARDWO_H

#include "icardwav.h"

struct wave_xferbuf 
{
	u32     xferpos;
	u32     silence_xferpos;
	u32     xferbufsize;     /* transfer buffer size */
	u32     numpages;        /* number of pages in transfer buffer */
	void    **xferbuffer;    /* pointer to the transfer buffer */
	int     is_stereo;
	int     is_16bit;
	int	bytespersample;
	u32     stopposition;
};

struct wave_out
{
    u32             state;
    struct emu_voice *voice;
    int             emupageindex;
    struct emu_timer *timer;
    struct wave_xferbuf *wavexferbuf;
    void 	    **pagetable;
    u32             callbacksize;
    u32             localvol;
    u32             localreverb;
    u32             localchorus;
    u32             globalvolFactor;
    u32             globalreverbFactor;
    u32             globalchorusFactor;
    int             setpos;
    u32             position;
    struct wave_format      wave_fmt;
    int             fill_silence;
};

/* setting this to other than a power of two
   may break some applications */
#define WAVEOUT_MAXBUFSIZE          32768 
#define WAVEOUT_MINBUFSIZE	    64

#define WAVEOUT_DEFAULTFRAGLEN      100 /* Time to play a fragment in ms (latency) */
#define WAVEOUT_DEFAULTBUFLEN       1000 /* Time to play the entire buffer in ms */

#define WAVEOUT_MINFRAGSHIFT	4

struct woinst 
{
        struct wave_out *wave_out;
        struct wave_format wave_fmt;
        u16 ossfragshift;
        u32 fragment_size;
        u32 numfrags;
        wait_queue_head_t wait_queue;
        int mapped;
        u32 total_copied;
        u32 total_played;
        u32 blocks;
	u32 curpos;
	u32 device;
	spinlock_t lock;
};

struct emu10k1_waveout
{
	u32 globalvol;
	u32 mute;
	u32 left;
	u32 right;
	u32 globalreverb;
	u32 globalchorus;
};

int emu10k1_waveout_open(struct emu10k1_wavedevice *);
void emu10k1_waveout_close(struct emu10k1_wavedevice *);
int emu10k1_waveout_start(struct emu10k1_wavedevice *);
void emu10k1_waveout_stop(struct emu10k1_wavedevice *);
void emu10k1_waveout_getxfersize(struct wave_out *, u32 *, u32 *, u32 *);
void emu10k1_waveout_xferdata(struct woinst*, u8*, u32 *);
void emu10k1_waveout_fillsilence(struct woinst*);
int emu10k1_waveout_setformat(struct emu10k1_wavedevice*);
int emu10k1_waveout_getcontrol(struct wave_out*, u32, u32 *);

#endif /* _CARDWO_H */
