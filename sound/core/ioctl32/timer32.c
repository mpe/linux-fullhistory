/*
 *   32bit -> 64bit ioctl wrapper for timer API
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

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/timer.h>
#include <asm/uaccess.h>
#include "ioctl32.h"

struct sndrv_timer_info32 {
	u32 flags;
	s32 card;
	unsigned char id[64];
	unsigned char name[80];
	u32 ticks;
	u32 resolution;
	unsigned char reserved[64];
};

#define CVT_sndrv_timer_info()\
{\
	COPY(flags);\
	COPY(card);\
	memcpy(dst->id, src->id, sizeof(src->id));\
	memcpy(dst->name, src->name, sizeof(src->name));\
	COPY(ticks);\
	COPY(resolution);\
}

struct timeval32 {
	s32 tv_sec;
	s32 tv_usec;
};

struct sndrv_timer_status32 {
	struct timeval32 tstamp;
	u32 resolution;
	u32 lost;
	u32 overrun;
	u32 queue;
	unsigned char reserved[64];
};

#define CVT_sndrv_timer_status()\
{\
	COPY(tstamp.tv_sec);\
	COPY(tstamp.tv_usec);\
	COPY(resolution);\
	COPY(lost);\
	COPY(overrun);\
	COPY(queue);\
}

DEFINE_ALSA_IOCTL(timer_info);
DEFINE_ALSA_IOCTL(timer_status);

DEFINE_ALSA_IOCTL_ENTRY(timer_info, timer_info, SNDRV_TIMER_IOCTL_INFO);
DEFINE_ALSA_IOCTL_ENTRY(timer_status, timer_status, SNDRV_TIMER_IOCTL_STATUS);

/*
 */

#define AP(x) snd_ioctl32_##x

enum {
	SNDRV_TIMER_IOCTL_INFO32 = _IOR('T', 0x11, struct sndrv_timer_info32),
	SNDRV_TIMER_IOCTL_STATUS32 = _IOW('T', 0x14, struct sndrv_timer_status32),
};

struct ioctl32_mapper timer_mappers[] = {
	{ SNDRV_TIMER_IOCTL_PVERSION, NULL },
	{ SNDRV_TIMER_IOCTL_NEXT_DEVICE, NULL },
	{ SNDRV_TIMER_IOCTL_SELECT, NULL },
	{ SNDRV_TIMER_IOCTL_INFO32, AP(timer_info) },
	{ SNDRV_TIMER_IOCTL_PARAMS, NULL },
	{ SNDRV_TIMER_IOCTL_STATUS32, AP(timer_status) },
	{ SNDRV_TIMER_IOCTL_START, NULL },
	{ SNDRV_TIMER_IOCTL_STOP, NULL },
	{ SNDRV_TIMER_IOCTL_CONTINUE, NULL },
	{ 0 },
};
