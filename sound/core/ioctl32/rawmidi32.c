/*
 *   32bit -> 64bit ioctl wrapper for raw MIDI API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/rawmidi.h>
#include <asm/uaccess.h>
#include "ioctl32.h"

struct sndrv_rawmidi_params32 {
	s32 stream;
	u32 buffer_size;
	u32 avail_min;
	unsigned int no_active_sensing: 1;
	unsigned char reserved[16];
};

#define CVT_sndrv_rawmidi_params()\
{\
	COPY(stream);\
	COPY(buffer_size);\
	COPY(avail_min);\
	COPY(no_active_sensing);\
}

struct timeval32 {
	s32 tv_sec;
	s32 tv_usec;
};

struct sndrv_rawmidi_status32 {
	s32 stream;
	struct timeval32 tstamp;
	u32 avail;
	u32 xruns;
	unsigned char reserved[16];
};

#define CVT_sndrv_rawmidi_status()\
{\
	COPY(stream);\
	COPY(tstamp.tv_sec);\
	COPY(tstamp.tv_usec);\
	COPY(avail);\
	COPY(xruns);\
}

DEFINE_ALSA_IOCTL(rawmidi_params);
DEFINE_ALSA_IOCTL(rawmidi_status);


#define AP(x) snd_ioctl32_##x

struct ioctl32_mapper rawmidi_mappers[] = {
	{ SNDRV_RAWMIDI_IOCTL_PVERSION, NULL },
	{ SNDRV_RAWMIDI_IOCTL_INFO, NULL },
	{ SNDRV_RAWMIDI_IOCTL_PARAMS, AP(rawmidi_params) },
	{ SNDRV_RAWMIDI_IOCTL_STATUS, AP(rawmidi_status) },
	{ SNDRV_RAWMIDI_IOCTL_DROP, NULL },
	{ SNDRV_RAWMIDI_IOCTL_DRAIN, NULL },

	{ SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE, NULL },
	{ SNDRV_CTL_IOCTL_RAWMIDI_INFO, NULL },
	{ SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE, NULL },

	{ 0 },
};
