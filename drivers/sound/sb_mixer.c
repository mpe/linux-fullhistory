
/*
 * sound/sb_mixer.c
 *
 * The low level mixer driver for the Sound Blaster compatible cards.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer		: ioctl code reworked (vmalloc/vfree removed)
 * Rolf Fokkens (Dec 20 1998)	: ES188x recording level support on a per
 *				  input basis.
 *              (Dec 24 1998)	: Recognition of ES1788, ES1887, ES1888,
 *				  ES1868, ES1869 and ES1878. Could be used for
 *				  specific handling in the future. All except
 *				  ES1887 and ES1888 and ES688 are handled like
 *				  ES1688.
 *              (Dec 27 1998)	: RECLEV for all (?) ES1688+ chips. ES188x now
 *				  have the "Dec 20" support + RECLEV
 */

/*
 * About the documentation
 *
 * I don't know if the chips all are OK, but the documentation is buggy. 'cause
 * I don't have all the cips myself, there's a lot I cannot verify. I'll try to
 * keep track of my latest insights about his here. If you have additional info,
 * please enlighten me (fokkensr@vertis.nl)!
 *
 * I had the impression that ES1688 also has 6 bit master volume control. The
 * documentation about ES1888 (rev C, october '95) claims that ES1888 has
 * the following features ES1688 doesn't have:
 * - 6 bit master volume
 * - Full Duplex
 * So ES1688 apparently doesn't have 6 bit master volume control, but the
 * ES1688 does have RECLEV control. Makes me wonder: does ES688 have it too?
 * Without RECLEV ES688 won't be much fun I guess.
 *
 * From the ES1888 (rev C, october '95) documentation I got the impression
 * that registers 0x68 to 0x6e don't exist which means: no recording volume
 * controls. To my surprise the ES888 documentation (1/14/96) claims that
 * ES888 does have these record mixer registers, but that ES1888 doesn't have
 * 0x69 and 0x6b. So the rest should be there.
 * 
 */

/*
 * About recognition of ESS chips
 *
 * The distinction of ES688, ES1688, ES1788, ES1887 and ES1888 is described in
 * a (preliminary ??) datasheet on ES1887. It's aim is to identify ES1887, but
 * during detection the text claims that "this chip may be ..." when a step
 * fails. This scheme is used to distinct between the above chips.
 * It appears however that some PnP chips like ES1868 are recognized as ES1788
 * by the ES1887 detection scheme. These PnP chips can be detected in another
 * way however: ES1868, ES1869 and ES1878 can be recognized (full proof I think)
 * by repeatedly reading mixer register 0x40. This is done by ess_identify in
 * sb_common.c.
 * This results in the following detection steps:
 * - distinct between ES688 and ES1688+ (as always done in this driver)
 *   if ES688 we're ready
 * - try to detect ES1868, ES1869 or ES1878
 *   if successful we're ready
 * - try to detect ES1888, ES1887 or ES1788
 *   if successful we're ready
 * - Dunno. Must be 1688. Will do in general
 *
 * About RECLEV support:
 *
 * The existing ES1688 support didn't take care of the ES1688+ recording
 * levels very well. Whenever a device was selected (recmask) for recording
 * it's recording level was loud, and it couldn't be changed. The fact that
 * internal register 0xb4 could take care of RECLEV, didn't work meaning until
 * it's value was restored every time the chip was reset; this reset the
 * value of 0xb4 too. I guess that's what 4front also had (have?) trouble with.
 *
 * About ES188x support:
 *
 * The ES188x has separate registers to control the recording levels, for all
 * inputs. The ES188x specific software makes these levels the same as their
 * corresponding playback levels, unless recmask says they aren't recorded. In
 * the latter case the recording volumes are 0.
 * Now recording levels of inputs can be controlled, by changing the playback
 * levels. Futhermore several devices can be recorded together (which is not
 * possible with the ES1688.
 * Besides the separate recording level control for each input, the common
 * recordig level can also be controlled by RECLEV as described above.
 */

#include <linux/config.h>
#include "sound_config.h"

#ifdef CONFIG_SBDSP
#define __SB_MIXER_C__

#include "sb.h"
#include "sb_mixer.h"

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

static mixer_tab sbpro_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,	0x22, 7, 4, 0x22, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	0x26, 7, 4, 0x26, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,	0x04, 7, 4, 0x04, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x2e, 7, 4, 0x2e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,	0x0a, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_CD,		0x28, 7, 4, 0x28, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x00, 0, 0, 0x00, 0, 0)
};

static mixer_tab es688_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,	0x32, 7, 4, 0x32, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,	0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,	0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,		0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_IGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,	0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,	0x00, 0, 0, 0x00, 0, 0)
};

/*
 * The ES1688 specifics... hopefully correct...
 * - 6 bit master volume
 *   I was wrong, ES1888 docs say ES1688 didn't have it.
 * - RECLEV control
 * These may apply to ES688 too. I have no idea.
 */
static mixer_tab es1688_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,	0x32, 7, 4, 0x32, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,	0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,	0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,		0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0xb4, 7, 4, 0xb4, 3, 4),
MIX_ENT(SOUND_MIXER_IGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,	0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,	0x00, 0, 0, 0x00, 0, 0)
};

static mixer_tab es1688later_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,     0x60, 5, 6, 0x62, 5, 6),
MIX_ENT(SOUND_MIXER_BASS,       0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,     0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,      0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,        0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,    0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,       0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,        0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,         0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,       0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,     0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,     0xb4, 7, 4, 0xb4, 3, 4),
MIX_ENT(SOUND_MIXER_IGAIN,      0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,      0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,      0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,      0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,      0x00, 0, 0, 0x00, 0, 0)
};

/*
 * The ES188x specifics.
 * Note that de master volume unlike ES688 is now controlled by two 6 bit
 * registers. These seem to work OK on 1868 too.
 * Also Note that the recording levels (ES188X_MIXER_REC...) have own 
 * entries as if they were playback devices. They are used internally in the
 * driver only!
 */
static mixer_tab es188x_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,	0x60, 5, 6, 0x62, 5, 6),
MIX_ENT(SOUND_MIXER_BASS,       0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,     0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,      0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,        0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,    0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,       0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,        0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,         0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,       0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,     0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0xb4, 7, 4, 0xb4, 3, 4),
MIX_ENT(SOUND_MIXER_IGAIN,      0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,      0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,      0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,      0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,      0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECSYNTH,	0x6b, 7, 4, 0x6b, 3, 4),
MIX_ENT(ES188X_MIXER_RECPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECSPEAKER,0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECLINE,	0x6e, 7, 4, 0x6e, 3, 4),
MIX_ENT(ES188X_MIXER_RECMIC,	0x68, 7, 4, 0x68, 3, 4),
MIX_ENT(ES188X_MIXER_RECCD,	0x6a, 7, 4, 0x6a, 3, 4),
MIX_ENT(ES188X_MIXER_RECIMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECRECLEV,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECIGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECOGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECLINE1,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES188X_MIXER_RECLINE2,	0x6c, 7, 4, 0x6c, 3, 4),
MIX_ENT(ES188X_MIXER_RECLINE3,	0x00, 0, 0, 0x00, 0, 0)
};

#ifdef	__SGNXPRO__
#if 0
static mixer_tab sgnxpro_mix = { 	/* not used anywhere */
MIX_ENT(SOUND_MIXER_VOLUME,	0x22, 7, 4, 0x22, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,	0x46, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	0x44, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	0x26, 7, 4, 0x26, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,	0x04, 7, 4, 0x04, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x42, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x2e, 7, 4, 0x2e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,	0x0a, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_CD,		0x28, 7, 4, 0x28, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_IGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,	0x00, 0, 0, 0x00, 0, 0)
};
#endif
#endif

static mixer_tab sb16_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,	0x30, 7, 5, 0x31, 7, 5),
MIX_ENT(SOUND_MIXER_BASS,	0x46, 7, 4, 0x47, 7, 4),
MIX_ENT(SOUND_MIXER_TREBLE,	0x44, 7, 4, 0x45, 7, 4),
MIX_ENT(SOUND_MIXER_SYNTH,	0x34, 7, 5, 0x35, 7, 5),
MIX_ENT(SOUND_MIXER_PCM,	0x32, 7, 5, 0x33, 7, 5),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x3b, 7, 2, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x38, 7, 5, 0x39, 7, 5),
MIX_ENT(SOUND_MIXER_MIC,	0x3a, 7, 5, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_CD,		0x36, 7, 5, 0x37, 7, 5),
MIX_ENT(SOUND_MIXER_IMIX,	0x3c, 0, 1, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x3f, 7, 2, 0x40, 7, 2), /* Obsolete. Use IGAIN */
MIX_ENT(SOUND_MIXER_IGAIN,	0x3f, 7, 2, 0x40, 7, 2),
MIX_ENT(SOUND_MIXER_OGAIN,	0x41, 7, 2, 0x42, 7, 2)
};

static mixer_tab als007_mix = 
{
MIX_ENT(SOUND_MIXER_VOLUME,	0x62, 7, 4, 0x62, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,	0x66, 7, 4, 0x66, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,	0x64, 7, 4, 0x64, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	0x6e, 7, 4, 0x6e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,	0x6a, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_CD,		0x68, 7, 4, 0x68, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,	0x00, 0, 0, 0x00, 0, 0), /* Obsolete. Use IGAIN */
MIX_ENT(SOUND_MIXER_IGAIN,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,	0x00, 0, 0, 0x00, 0, 0)
};


/* SM_GAMES          Master volume is lower and PCM & FM volumes
			     higher than with SB Pro. This improves the
			     sound quality */

static int smg_default_levels[32] =
{
  0x2020,			/* Master Volume */
  0x4b4b,			/* Bass */
  0x4b4b,			/* Treble */
  0x6464,			/* FM */
  0x6464,			/* PCM */
  0x4b4b,			/* PC Speaker */
  0x4b4b,			/* Ext Line */
  0x0000,			/* Mic */
  0x4b4b,			/* CD */
  0x4b4b,			/* Recording monitor */
  0x4b4b,			/* SB PCM */
  0x4b4b,			/* Recording level */
  0x4b4b,			/* Input gain */
  0x4b4b,			/* Output gain */
  0x4040,			/* Line1 */
  0x4040,			/* Line2 */
  0x1515			/* Line3 */
};

static int sb_default_levels[32] =
{
  0x5a5a,			/* Master Volume */
  0x4b4b,			/* Bass */
  0x4b4b,			/* Treble */
  0x4b4b,			/* FM */
  0x4b4b,			/* PCM */
  0x4b4b,			/* PC Speaker */
  0x4b4b,			/* Ext Line */
  0x1010,			/* Mic */
  0x4b4b,			/* CD */
  0x0000,			/* Recording monitor */
  0x4b4b,			/* SB PCM */
  0x4b4b,			/* Recording level */
  0x4b4b,			/* Input gain */
  0x4b4b,			/* Output gain */
  0x4040,			/* Line1 */
  0x4040,			/* Line2 */
  0x1515			/* Line3 */
};

static unsigned char sb16_recmasks_L[SOUND_MIXER_NRDEVICES] =
{
	0x00,	/* SOUND_MIXER_VOLUME	*/
	0x00,	/* SOUND_MIXER_BASS	*/
	0x00,	/* SOUND_MIXER_TREBLE	*/
	0x40,	/* SOUND_MIXER_SYNTH	*/
	0x00,	/* SOUND_MIXER_PCM	*/
	0x00,	/* SOUND_MIXER_SPEAKER	*/
	0x10,	/* SOUND_MIXER_LINE	*/
	0x01,	/* SOUND_MIXER_MIC	*/
	0x04,	/* SOUND_MIXER_CD	*/
	0x00,	/* SOUND_MIXER_IMIX	*/
	0x00,	/* SOUND_MIXER_ALTPCM	*/
	0x00,	/* SOUND_MIXER_RECLEV	*/
	0x00,	/* SOUND_MIXER_IGAIN	*/
	0x00	/* SOUND_MIXER_OGAIN	*/
};

static unsigned char sb16_recmasks_R[SOUND_MIXER_NRDEVICES] =
{
	0x00,	/* SOUND_MIXER_VOLUME	*/
	0x00,	/* SOUND_MIXER_BASS	*/
	0x00,	/* SOUND_MIXER_TREBLE	*/
	0x20,	/* SOUND_MIXER_SYNTH	*/
	0x00,	/* SOUND_MIXER_PCM	*/
	0x00,	/* SOUND_MIXER_SPEAKER	*/
	0x08,	/* SOUND_MIXER_LINE	*/
	0x01,	/* SOUND_MIXER_MIC	*/
	0x02,	/* SOUND_MIXER_CD	*/
	0x00,	/* SOUND_MIXER_IMIX	*/
	0x00,	/* SOUND_MIXER_ALTPCM	*/
	0x00,	/* SOUND_MIXER_RECLEV	*/
	0x00,	/* SOUND_MIXER_IGAIN	*/
	0x00	/* SOUND_MIXER_OGAIN	*/
};

static char     smw_mix_regs[] =	/* Left mixer registers */
{
  0x0b,				/* SOUND_MIXER_VOLUME */
  0x0d,				/* SOUND_MIXER_BASS */
  0x0d,				/* SOUND_MIXER_TREBLE */
  0x05,				/* SOUND_MIXER_SYNTH */
  0x09,				/* SOUND_MIXER_PCM */
  0x00,				/* SOUND_MIXER_SPEAKER */
  0x03,				/* SOUND_MIXER_LINE */
  0x01,				/* SOUND_MIXER_MIC */
  0x07,				/* SOUND_MIXER_CD */
  0x00,				/* SOUND_MIXER_IMIX */
  0x00,				/* SOUND_MIXER_ALTPCM */
  0x00,				/* SOUND_MIXER_RECLEV */
  0x00,				/* SOUND_MIXER_IGAIN */
  0x00,				/* SOUND_MIXER_OGAIN */
  0x00,				/* SOUND_MIXER_LINE1 */
  0x00,				/* SOUND_MIXER_LINE2 */
  0x00				/* SOUND_MIXER_LINE3 */
};

static int      sbmixnum = 1;

static void     sb_mixer_reset(sb_devc * devc);

inline void sb_mixer_bits
	(sb_devc * devc, unsigned int reg, unsigned int mask, unsigned int val)
{
	int value;

	value = sb_getmixer(devc, reg);
	value = (value & ~mask) | (val & mask);
	sb_setmixer(devc, reg, value);
}


void sb_mixer_set_stereo(sb_devc * devc, int mode)
{
	sb_mixer_bits(devc, OUT_FILTER, STEREO_DAC, (mode ? STEREO_DAC : MONO_DAC));
}

static int detect_mixer(sb_devc * devc)
{
	/* Just trust the mixer is there */
	return 1;
}

static void change_bits(sb_devc * devc, unsigned char *regval, int dev, int chn, int newval)
{
	unsigned char mask;
	int shift;

	mask = (1 << (*devc->iomap)[dev][chn].nbits) - 1;
	newval = (int) ((newval * mask) + 50) / 100;	/* Scale */

	shift = (*devc->iomap)[dev][chn].bitoffs - (*devc->iomap)[dev][LEFT_CHN].nbits + 1;

	*regval &= ~(mask << shift);	/* Mask out previous value */
	*regval |= (newval & mask) << shift;	/* Set the new value */
}

static int sb_mixer_get(sb_devc * devc, int dev)
{
	if (!((1 << dev) & devc->supported_devices))
		return -EINVAL;
	return devc->levels[dev];
}

void smw_mixer_init(sb_devc * devc)
{
	int i;

	sb_setmixer(devc, 0x00, 0x18);	/* Mute unused (Telephone) line */
	sb_setmixer(devc, 0x10, 0x38);	/* Config register 2 */

	devc->supported_devices = 0;
	for (i = 0; i < sizeof(smw_mix_regs); i++)
		if (smw_mix_regs[i] != 0)
			devc->supported_devices |= (1 << i);

	devc->supported_rec_devices = devc->supported_devices &
		~(SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_PCM | SOUND_MASK_VOLUME);
	sb_mixer_reset(devc);
}

static int common_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	int regoffs;
	unsigned char val;

	regoffs = (*devc->iomap)[dev][LEFT_CHN].regno;

	if (regoffs == 0)
		return -EINVAL;

	val = sb_getmixer(devc, regoffs);
	change_bits(devc, &val, dev, LEFT_CHN, left);

	if ((*devc->iomap)[dev][RIGHT_CHN].regno != regoffs)	/*
								 * Change register
								 */
	{
		sb_setmixer(devc, regoffs, val);	/*
							 * Save the old one
							 */
		regoffs = (*devc->iomap)[dev][RIGHT_CHN].regno;

		if (regoffs == 0)
			return left | (left << 8);	/*
							 * Just left channel present
							 */

		val = sb_getmixer(devc, regoffs);	/*
							 * Read the new one
							 */
	}
	change_bits(devc, &val, dev, RIGHT_CHN, right);

	sb_setmixer(devc, regoffs, val);

	return left | (right << 8);
}

/*
 * After a sb_dsp_reset extended register 0xb4 (RECLEV) is reset too. After
 * sb_dsp_reset RECLEV has to be restored. This is where ess_mixer_reload
 * helps.
 */
void ess_mixer_reload (sb_devc * devc, int dev)
{
	int left, right, value;

	value = devc->levels[dev];
	left  = value & 0x000000ff;
    	right = (value & 0x0000ff00) >> 8;

	common_mixer_set(devc, dev, left, right);
}

/*
 * Changing playback levels at ES188x means having to take care of recording
 * levels of recorded inputs (devc->recmask) too!
 */
static int es188x_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	if (devc->recmask & (1 << dev)) {
		common_mixer_set(devc, dev + ES188X_MIXER_RECDIFF, left, right);
	}
	return common_mixer_set(devc, dev, left, right);
}

static int smw_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	int reg, val;

	switch (dev)
	{
		case SOUND_MIXER_VOLUME:
			sb_setmixer(devc, 0x0b, 96 - (96 * left / 100));	/* 96=mute, 0=max */
			sb_setmixer(devc, 0x0c, 96 - (96 * right / 100));
			break;

		case SOUND_MIXER_BASS:
		case SOUND_MIXER_TREBLE:
			devc->levels[dev] = left | (right << 8);
			/* Set left bass and treble values */
			val = ((devc->levels[SOUND_MIXER_TREBLE] & 0xff) * 16 / (unsigned) 100) << 4;
			val |= ((devc->levels[SOUND_MIXER_BASS] & 0xff) * 16 / (unsigned) 100) & 0x0f;
			sb_setmixer(devc, 0x0d, val);

			/* Set right bass and treble values */
			val = (((devc->levels[SOUND_MIXER_TREBLE] >> 8) & 0xff) * 16 / (unsigned) 100) << 4;
			val |= (((devc->levels[SOUND_MIXER_BASS] >> 8) & 0xff) * 16 / (unsigned) 100) & 0x0f;
			sb_setmixer(devc, 0x0e, val);
		
			break;

		default:
			reg = smw_mix_regs[dev];
			if (reg == 0)
				return -EINVAL;
			sb_setmixer(devc, reg, (24 - (24 * left / 100)) | 0x20);	/* 24=mute, 0=max */
			sb_setmixer(devc, reg + 1, (24 - (24 * right / 100)) | 0x40);
	}

	devc->levels[dev] = left | (right << 8);
	return left | (right << 8);
}

static int sb_mixer_set(sb_devc * devc, int dev, int value)
{
	int left = value & 0x000000ff;
	int right = (value & 0x0000ff00) >> 8;
	int retval;

	if (left > 100)
		left = 100;
	if (right > 100)
		right = 100;

	if (dev > 31)
		return -EINVAL;

	if (!(devc->supported_devices & (1 << dev)))	/*
							 * Not supported
							 */
		return -EINVAL;

	/* Differentiate depending on the chipsets */
	switch (devc->model) {
	case MDL_SMW:
		retval = smw_mixer_set(devc, dev, left, right);
		break;
	case MDL_ESS:
		if (devc->submodel == SUBMDL_ES188X) {
			retval = es188x_mixer_set(devc, dev, left, right);
		} else {
			retval = common_mixer_set(devc, dev, left, right);
		}
		break;
	default:
		retval = common_mixer_set(devc, dev, left, right);
	}
	if (retval >= 0) devc->levels[dev] = retval;

	return retval;
}

/*
 * set_recsrc doesn't apply to ES188x
 */
static void set_recsrc(sb_devc * devc, int src)
{
	sb_setmixer(devc, RECORD_SRC, (sb_getmixer(devc, RECORD_SRC) & ~7) | (src & 0x7));
}

/*
 * Changing the recmask on a ES188x means:
 * (1) Find the differences
 * (2) For "turned-on"  inputs: make the recording level the playback level
 * (3) For "turned-off" inputs: make the recording level zero
 */
static int es188x_set_recmask(sb_devc * devc, int mask)
{
	int i, i_mask, cur_mask, diff_mask;
	int value, left, right;

	cur_mask  = devc->recmask;
	diff_mask = (cur_mask ^ mask);

	for (i = 0; i < 32; i++) {
		i_mask = (1 << i);
		if (diff_mask & i_mask) {	/* Difference? (1)	*/
			if (mask & i_mask) {	/* Turn it on  (2)	*/
				value = devc->levels[i];
				left  = value & 0x000000ff;
    				right = (value & 0x0000ff00) >> 8;
			} else {		/* Turn it off (3)	*/
				left  = 0;
				right = 0;
			}
			common_mixer_set(devc, i + ES188X_MIXER_RECDIFF, left, right);
		}
	}
	return mask;
}

static int set_recmask(sb_devc * devc, int mask)
{
	int devmask, i;
	unsigned char  regimageL, regimageR;

	devmask = mask & devc->supported_rec_devices;

	switch (devc->model)
	{
		case MDL_SBPRO:
		case MDL_ESS:
		case MDL_JAZZ:
		case MDL_SMW:
			if (devc->model == MDL_ESS &&
				devc->submodel == SUBMDL_ES188X) {
				/*
				 * ES188x needs a separate approach
				 */
				devmask = es188x_set_recmask(devc, devmask);
				break;
			};

			if (devmask != SOUND_MASK_MIC &&
				devmask != SOUND_MASK_LINE &&
				devmask != SOUND_MASK_CD)
			{
				/*
				 * More than one device selected. Drop the
				 * previous selection
				 */
				devmask &= ~devc->recmask;
			}
			if (devmask != SOUND_MASK_MIC &&
				devmask != SOUND_MASK_LINE &&
				devmask != SOUND_MASK_CD)
			{
				/*
				 * More than one device selected. Default to
				 * mic
				 */
				devmask = SOUND_MASK_MIC;
			}
			if (devmask ^ devc->recmask)	/*
							 *	Input source changed
							 */
			{
				switch (devmask)
				{
					case SOUND_MASK_MIC:
						set_recsrc(devc, SRC__MIC);
						break;

					case SOUND_MASK_LINE:
						set_recsrc(devc, SRC__LINE);
						break;

					case SOUND_MASK_CD:
						set_recsrc(devc, SRC__CD);
						break;

					default:
						set_recsrc(devc, SRC__MIC);
				}
			}
			break;

		case MDL_SB16:
			if (!devmask)
				devmask = SOUND_MASK_MIC;

			if (devc->submodel == SUBMDL_ALS007) 
			{
				switch (devmask) 
				{
					case SOUND_MASK_LINE:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_LINE);
						break;
					case SOUND_MASK_CD:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_CD);
						break;
					case SOUND_MASK_SYNTH:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_SYNTH);
						break;
					default:           /* Also takes care of SOUND_MASK_MIC case */
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_MIC);
						break;
				}
			}
			else
			{
				regimageL = regimageR = 0;
				for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				{
					if ((1 << i) & devmask)
					{
						regimageL |= sb16_recmasks_L[i];
						regimageR |= sb16_recmasks_R[i];
					}
					sb_setmixer (devc, SB16_IMASK_L, regimageL);
					sb_setmixer (devc, SB16_IMASK_R, regimageR);
				}
			}
			break;
	}
	devc->recmask = devmask;
	return devc->recmask;
}

static int set_outmask(sb_devc * devc, int mask)
{
	int devmask, i;
	unsigned char  regimage;

	devmask = mask & devc->supported_out_devices;

	switch (devc->model)
	{
		case MDL_SB16:
			if (devc->submodel == SUBMDL_ALS007) 
				break;
			else
			{
				regimage = 0;
				for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				{
					if ((1 << i) & devmask)
					{
						regimage |= (sb16_recmasks_L[i] | sb16_recmasks_R[i]);
					}
					sb_setmixer (devc, SB16_OMASK, regimage);
				}
			}
			break;
		default:
			break;
	}

	devc->outmask = devmask;
	return devc->outmask;
}

static int sb_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	sb_devc *devc = mixer_devs[dev]->devc;
	int val, ret;

	/*
	 * Use ioctl(fd, SOUND_MIXER_PRIVATE1, &mode) to turn AGC off (0) or on (1).
	 */
	if (cmd == SOUND_MIXER_PRIVATE1 && devc->model == MDL_SB16) 
	{
		if (get_user(val, (int *)arg))
			return -EFAULT;
		sb_setmixer(devc, 0x43, (~val) & 0x01);
		return 0;
	}
	if (((cmd >> 8) & 0xff) == 'M') 
	{
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) 
		{
			if (get_user(val, (int *)arg))
				return -EFAULT;
			switch (cmd & 0xff) 
			{
				case SOUND_MIXER_RECSRC:
					ret = set_recmask(devc, val);
					break;

				case SOUND_MIXER_OUTSRC:
					ret = set_outmask(devc, val);
					break;

				default:
					ret = sb_mixer_set(devc, cmd & 0xff, val);
			}
		}
		else switch (cmd & 0xff) 
		{
			case SOUND_MIXER_RECSRC:
				ret = devc->recmask;
				break;
				  
			case SOUND_MIXER_OUTSRC:
				ret = devc->outmask;
				break;
				  
			case SOUND_MIXER_DEVMASK:
				ret = devc->supported_devices;
				break;
				  
			case SOUND_MIXER_STEREODEVS:
				ret = devc->supported_devices;
				/* The ESS seems to have stereo mic controls */
				if (devc->model == MDL_ESS)
					ret &= ~(SOUND_MASK_SPEAKER|SOUND_MASK_IMIX);
				else if (devc->model != MDL_JAZZ && devc->model != MDL_SMW)
					ret &= ~(SOUND_MASK_MIC | SOUND_MASK_SPEAKER | SOUND_MASK_IMIX);
				break;
				  
			case SOUND_MIXER_RECMASK:
				ret = devc->supported_rec_devices;
				break;
				  
			case SOUND_MIXER_OUTMASK:
				ret = devc->supported_out_devices;
				break;
				  
			case SOUND_MIXER_CAPS:
				ret = devc->mixer_caps;
				break;
				    
			default:
				ret = sb_mixer_get(devc, cmd & 0xff);
				break;
		}
		return put_user(ret, (int *)arg); 
	} else
		return -EINVAL;
}

static struct mixer_operations sb_mixer_operations =
{
	"SB",
	"Sound Blaster",
	sb_mixer_ioctl
};

static struct mixer_operations als007_mixer_operations =
{
	"ALS007",
	"Avance ALS-007",
	sb_mixer_ioctl
};

static void sb_mixer_reset(sb_devc * devc)
{
	char name[32];
	int i;
	extern int sm_games;

	sprintf(name, "SB_%d", devc->sbmixnum);

	if (sm_games)
		devc->levels = load_mixer_volumes(name, smg_default_levels, 1);
	else
		devc->levels = load_mixer_volumes(name, sb_default_levels, 1);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		sb_mixer_set(devc, i, devc->levels[i]);

	/*
	 * Separate actions for ES188x:
	 * Change registers 7a and 1c to make the record mixer the
	 * actual recording source.
	 * Then call set_recmask twice to do extra ES188x initializations
	 */
	if (devc->model == MDL_ESS && devc->submodel == SUBMDL_ES188X) {
		sb_mixer_bits(devc, 0x7a, 0x18, 0x08);
		sb_mixer_bits(devc, 0x1c, 0x07, 0x07);

		set_recmask(devc, ES188X_RECORDING_DEVICES);
		set_recmask(devc, 0);
	}
	set_recmask(devc, SOUND_MASK_MIC);
}

int sb_mixer_init(sb_devc * devc)
{
	int mixer_type = 0;
	int m;

	devc->sbmixnum = sbmixnum++;
	devc->levels = NULL;

	sb_setmixer(devc, 0x00, 0);	/* Reset mixer */

	if (!(mixer_type = detect_mixer(devc)))
		return 0;	/* No mixer. Why? */

	switch (devc->model)
	{
		case MDL_SBPRO:
		case MDL_AZTECH:
		case MDL_JAZZ:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;
			devc->supported_devices = SBPRO_MIXER_DEVICES;
			devc->supported_rec_devices = SBPRO_RECORDING_DEVICES;
			devc->iomap = &sbpro_mix;
			break;

		case MDL_ESS:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;

			/*
			 * Take care of ES188x specifics...
			 */
			switch (devc->submodel) {
			case SUBMDL_ES188X:
				devc->supported_devices
					= ES188X_MIXER_DEVICES;
				devc->supported_rec_devices
					= ES188X_RECORDING_DEVICES;
				devc->iomap = &es188x_mix;
				break;
			default:
				if (devc->submodel < 8) {
					devc->supported_devices
						= ES688_MIXER_DEVICES;
					devc->supported_rec_devices
						= ES688_RECORDING_DEVICES;
					devc->iomap = &es688_mix;
				} else {
					/*
					 * es1688 has 4 bits master vol.
					 * later chips have 6 bits (?)
					 */
					devc->supported_devices
						= ES1688_MIXER_DEVICES;
					devc->supported_rec_devices
						= ES1688_RECORDING_DEVICES;
					if (devc->submodel < 0x10) {
						devc->iomap = &es1688_mix;
					} else {
						devc->iomap = &es1688later_mix;
					}
				}
			}

			break;

		case MDL_SMW:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;
			devc->supported_devices = 0;
			devc->supported_rec_devices = 0;
			devc->iomap = &sbpro_mix;
			smw_mixer_init(devc);
			break;

		case MDL_SB16:
			devc->mixer_caps = 0;
			devc->supported_rec_devices = SB16_RECORDING_DEVICES;
			devc->supported_out_devices = SB16_OUTFILTER_DEVICES;
			if (devc->submodel != SUBMDL_ALS007)
			{
				devc->supported_devices = SB16_MIXER_DEVICES;
				devc->iomap = &sb16_mix;
			}
			else
			{
				devc->supported_devices = ALS007_MIXER_DEVICES;
				devc->iomap = &als007_mix;
			}
			break;

		default:
			printk(KERN_WARNING "sb_mixer: Unsupported mixer type %d\n", devc->model);
			return 0;
	}

	m = sound_alloc_mixerdev();
	if (m == -1)
		return 0;

	mixer_devs[m] = (struct mixer_operations *)kmalloc(sizeof(struct mixer_operations), GFP_KERNEL);
	if (mixer_devs[m] == NULL)
	{
		printk(KERN_ERR "sb_mixer: Can't allocate memory\n");
		sound_unload_mixerdev(m);
		return 0;
	}

	if (devc->submodel != SUBMDL_ALS007)
		memcpy ((char *) mixer_devs[m], (char *) &sb_mixer_operations, sizeof (struct mixer_operations));
	else
		memcpy ((char *) mixer_devs[m], (char *) &als007_mixer_operations, sizeof (struct mixer_operations));

	mixer_devs[m]->devc = devc;
	devc->my_mixerdev = m;
	sb_mixer_reset(devc);
	return 1;
}

#endif
