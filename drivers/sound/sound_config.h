/* sound_config.h
 *
 * A driver for sound cards, misc. configuration parameters.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


#ifndef  _SOUND_CONFIG_H_
#define  _SOUND_CONFIG_H_

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/sound.h>

#include "legacy.h"
#include "os.h"
#include "soundvers.h"


#ifndef SND_DEFAULT_ENABLE
#define SND_DEFAULT_ENABLE	1
#endif

#ifndef MAX_REALTIME_FACTOR
#define MAX_REALTIME_FACTOR	4
#endif

/*
 * Use always 64k buffer size. There is no reason to use shorter.
 */
#undef DSP_BUFFSIZE
#define DSP_BUFFSIZE		(64*1024)

#ifndef DSP_BUFFCOUNT
#define DSP_BUFFCOUNT		1	/* 1 is recommended. */
#endif

#define FM_MONO		0x388	/* This is the I/O address used by AdLib */

#ifndef CONFIG_PAS_BASE
#define CONFIG_PAS_BASE	0x388
#endif

#if defined(CONFIG_SB16_DMA) && !defined(CONFIG_SB_DMA2)
#  define CONFIG_SB_DMA2 CONFIG_SB16_DMA
#endif

#if defined(SB16MIDI_BASE) && !defined(CONFIG_SB_MPU_BASE)
#   define CONFIG_SB_MPU_BASE SB16MIDI_BASE
#endif

#ifndef CONFIG_SB_MPU_IRQ
#  define CONFIG_SB_MPU_IRQ CONFIG_SB_IRQ
#endif

/* SEQ_MAX_QUEUE is the maximum number of sequencer events buffered by the
   driver. (There is no need to alter this) */
#define SEQ_MAX_QUEUE	1024

#define SBFM_MAXINSTR		(256)	/* Size of the FM Instrument bank */
/* 128 instruments for general MIDI setup and 16 unassigned	 */

/*
 * Minor numbers for the sound driver.
 *
 * Unfortunately Creative called the codec chip of SB as a DSP. For this
 * reason the /dev/dsp is reserved for digitized audio use. There is a
 * device for true DSP processors but it will be called something else.
 * In v3.0 it's /dev/sndproc but this could be a temporary solution.
 */

#define SND_NDEVS	256	/* Number of supported devices */
#define SND_DEV_CTL	0	/* Control port /dev/mixer */
#define SND_DEV_SEQ	1	/* Sequencer output /dev/sequencer (FM
				   synthesizer and MIDI output) */
#define SND_DEV_MIDIN	2	/* Raw midi access */
#define SND_DEV_DSP	3	/* Digitized voice /dev/dsp */
#define SND_DEV_AUDIO	4	/* Sparc compatible /dev/audio */
#define SND_DEV_DSP16	5	/* Like /dev/dsp but 16 bits/sample */
#define SND_DEV_STATUS	6	/* /dev/sndstat */
#define SND_DEV_AWFM	7	/* Reserved */
#define SND_DEV_SEQ2	8	/* /dev/sequencer, level 2 interface */
#define SND_DEV_SNDPROC 9	/* /dev/sndproc for programmable devices */
#define SND_DEV_PSS	SND_DEV_SNDPROC

#define DSP_DEFAULT_SPEED	8000

#define MAX_AUDIO_DEV	5
#define MAX_MIXER_DEV	5
#define MAX_SYNTH_DEV	5
#define MAX_MIDI_DEV	6
#define MAX_TIMER_DEV	4

struct address_info {
	int io_base;
	int irq;
	int dma;
	int dma2;
	int always_detect;	/* 1=Trust me, it's there */
	char *name;
	int driver_use_1;	/* Driver defined field 1 */
	int driver_use_2;	/* Driver defined field 2 */
	int *osp;	/* OS specific info */
	int card_subtype;	/* Driver specific. Usually 0 */
	void *memptr;           /* Module memory chainer */
	int slots[6];           /* To remember driver slot ids */
};

#define SYNTH_MAX_VOICES	32

struct voice_alloc_info {
		int max_voice;
		int used_voices;
		int ptr;		/* For device specific use */
		unsigned short map[SYNTH_MAX_VOICES]; /* (ch << 8) | (note+1) */
		int timestamp;
		int alloc_times[SYNTH_MAX_VOICES];
	};

struct channel_info {
		int pgm_num;
		int bender_value;
		int bender_range;
		unsigned char controllers[128];
	};

/*
 * Process wakeup reasons
 */
#define WK_NONE		0x00
#define WK_WAKEUP	0x01
#define WK_TIMEOUT	0x02
#define WK_SIGNAL	0x04
#define WK_SLEEP	0x08
#define WK_SELECT	0x10
#define WK_ABORT	0x20

#define OPEN_READ	PCM_ENABLE_INPUT
#define OPEN_WRITE	PCM_ENABLE_OUTPUT
#define OPEN_READWRITE	(OPEN_READ|OPEN_WRITE)

#if OPEN_READ == FMODE_READ && OPEN_WRITE == FMODE_WRITE

extern __inline__ int translate_mode(struct file *file)
{
	return file->f_mode;
}

#else

extern __inline__ int translate_mode(struct file *file)
{
	return ((file->f_mode & FMODE_READ) ? OPEN_READ : 0) |
		((file->f_mode & FMODE_WRITE) ? OPEN_WRITE : 0);
}

#endif


#include "sound_calls.h"
#include "dev_table.h"

#ifndef DEB
#define DEB(x)
#endif

#ifndef DDB
#define DDB(x) {}
#endif

#ifndef MDB
#ifdef MODULE
#define MDB(x) x
#else
#define MDB(x)
#endif
#endif

#define TIMER_ARMED	121234
#define TIMER_NOT_ARMED	1

#endif
