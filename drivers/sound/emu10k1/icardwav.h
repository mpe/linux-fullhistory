/*     
 **********************************************************************
 *     icardwav.h
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

#ifndef _ICARDWAV_H
#define _ICARDWAV_H

/* Enumeration for SetControl */
enum
{
        WAVECURPOS = 0x10,
};

struct wave_format 
{
	u32 samplingrate;
	u32 bitsperchannel;
	u32 channels;		/* 1 = Mono, 2 = Stereo */
};

/* emu10k1_wave states */
#define CARDWAVE_STATE_STOPPED     0x0001
#define CARDWAVE_STATE_STARTED     0x0002

#endif /* _ICARDWAV_H */
