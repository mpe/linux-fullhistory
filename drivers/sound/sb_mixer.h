/*
 * sound/sb_mixer.h
 * 
 * Definitions for the SB Pro and SB16 mixers
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

/*
 * Modified:
 *	Hunyue Yau	Jan 6 1994
 *	Added defines for the Sound Galaxy NX Pro mixer.
 *
 *	Rolf Fokkens	Dec 20 1998
 *	Added defines for some ES188x chips.
 *
 *	Rolf Fokkens	Dec 27 1998
 *	Moved static stuff to sb_mixer.c
 *
 */
#include <linux/config.h>
#include "legacy.h"

#ifdef CONFIG_SBDSP

#define SBPRO_RECORDING_DEVICES	(SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD)

/* Same as SB Pro, unless I find otherwise */
#define SGNXPRO_RECORDING_DEVICES SBPRO_RECORDING_DEVICES

#define SBPRO_MIXER_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_VOLUME)

/* SG NX Pro has treble and bass settings on the mixer. The 'speaker'
 * channel is the COVOX/DisneySoundSource emulation volume control
 * on the mixer. It does NOT control speaker volume. Should have own
 * mask eventually?
 */
#define SGNXPRO_MIXER_DEVICES	(SBPRO_MIXER_DEVICES|SOUND_MASK_BASS| \
				 SOUND_MASK_TREBLE|SOUND_MASK_SPEAKER )

#define SB16_RECORDING_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD)

#define SB16_OUTFILTER_DEVICES		(SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD)

#define ES688_RECORDING_DEVICES SBPRO_RECORDING_DEVICES
#define ES688_MIXER_DEVICES (SBPRO_MIXER_DEVICES|SOUND_MASK_LINE2|SOUND_MASK_SPEAKER)

#define ES1688_RECORDING_DEVICES ES688_RECORDING_DEVICES
#define ES1688_MIXER_DEVICES (ES688_MIXER_DEVICES|SOUND_MASK_RECLEV)

#define ES188X_RECORDING_DEVICES	(ES1688_RECORDING_DEVICES | SOUND_MASK_LINE2 \
					 |SOUND_MASK_SYNTH)
#define ES188X_MIXER_DEVICES (ES1688_MIXER_DEVICES)

#define SB16_MIXER_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | \
					 SOUND_MASK_IGAIN | SOUND_MASK_OGAIN | \
					 SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE | \
					SOUND_MASK_IMIX)

/* These are the only devices that are working at the moment.  Others could
 * be added once they are identified and a method is found to control them.
 */
#define ALS007_MIXER_DEVICES	(SOUND_MASK_SYNTH | SOUND_MASK_LINE | \
				 SOUND_MASK_PCM | SOUND_MASK_MIC | \
				 SOUND_MASK_CD | \
				 SOUND_MASK_VOLUME)
/*
 * Mixer registers
 * 
 * NOTE!	RECORD_SRC == IN_FILTER
 */

/* 
 * Mixer registers of SB Pro
 */
#define VOC_VOL		0x04
#define MIC_VOL		0x0A
#define MIC_MIX		0x0A
#define RECORD_SRC	0x0C
#define IN_FILTER	0x0C
#define OUT_FILTER	0x0E
#define MASTER_VOL	0x22
#define FM_VOL		0x26
#define CD_VOL		0x28
#define LINE_VOL	0x2E
#define IRQ_NR		0x80
#define DMA_NR		0x81
#define IRQ_STAT	0x82
#define OPSW		0x3c

/*
 * Additional registers on the SG NX Pro 
 */
#define COVOX_VOL	0x42
#define TREBLE_LVL	0x44
#define BASS_LVL	0x46

#define FREQ_HI         (1 << 3)/* Use High-frequency ANFI filters */
#define FREQ_LOW        0	/* Use Low-frequency ANFI filters */
#define FILT_ON         0	/* Yes, 0 to turn it on, 1 for off */
#define FILT_OFF        (1 << 5)

#define MONO_DAC	0x00
#define STEREO_DAC	0x02

/*
 * Mixer registers of SB16
 */
#define SB16_OMASK	0x3c
#define SB16_IMASK_L	0x3d
#define SB16_IMASK_R	0x3e

#define LEFT_CHN	0
#define RIGHT_CHN	1

/*
 * Mixer registers of ES188x
 *
 * These registers specifically take care of recording levels. To make the
 * mapping from playback devices to recording devices every recording
 * devices = playback device + ES188X_MIXER_RECDIFF
 */
#define ES188X_MIXER_RECBASE	(SOUND_MIXER_LINE3 + 1)
#define ES188X_MIXER_RECDIFF	(ES188X_MIXER_RECBASE - SOUND_MIXER_SYNTH)

#define ES188X_MIXER_RECSYNTH	(SOUND_MIXER_SYNTH	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECPCM	(SOUND_MIXER_PCM	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECSPEAKER	(SOUND_MIXER_SPEAKER	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECLINE	(SOUND_MIXER_LINE	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECMIC	(SOUND_MIXER_MIC	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECCD	(SOUND_MIXER_CD		+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECIMIX	(SOUND_MIXER_IMIX	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECALTPCM	(SOUND_MIXER_ALTPCM	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECRECLEV	(SOUND_MIXER_RECLEV	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECIGAIN	(SOUND_MIXER_IGAIN	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECOGAIN	(SOUND_MIXER_OGAIN	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECLINE1	(SOUND_MIXER_LINE1	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECLINE2	(SOUND_MIXER_LINE2	+ ES188X_MIXER_RECDIFF)
#define ES188X_MIXER_RECLINE3	(SOUND_MIXER_LINE3	+ ES188X_MIXER_RECDIFF)

/*
 * Mixer registers of ALS007
 */
#define ALS007_RECORD_SRC	0x6c
#define ALS007_OUTPUT_CTRL1	0x3c
#define ALS007_OUTPUT_CTRL2	0x4c

#define MIX_ENT(name, reg_l, bit_l, len_l, reg_r, bit_r, len_r)	\
	{{reg_l, bit_l, len_l}, {reg_r, bit_r, len_r}}

/*
 *	Recording sources (SB Pro)
 */

#define SRC__MIC         1	/* Select Microphone recording source */
#define SRC__CD          3	/* Select CD recording source */
#define SRC__LINE        7	/* Use Line-in for recording source */

/*
 *	Recording sources for ALS-007
 */

#define ALS007_MIC	4
#define ALS007_LINE	6
#define ALS007_CD	2
#define ALS007_SYNTH	7

#endif
