/*
 * sound/awe_config.h
 *
 * Configuration of AWE32/SB32/AWE64 wave table synth driver.
 *   version 0.4.3; Mar. 1, 1998
 *
 * Copyright (C) 1996-1998 Takashi Iwai
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef AWE_CONFIG_H_DEF
#define AWE_CONFIG_H_DEF

/*----------------------------------------------------------------
 * system configuration
 *----------------------------------------------------------------*/

/* if your kernel support module for each soundcard, define this.
 * NOTE: it will be automatically set on linux-2.1.x kernels.
 *       only define here if you have moduler sound system on
 *       2.0.x kernel (like RedHat).
 */
#undef AWE_MODULE_SUPPORT


/*----------------------------------------------------------------
 * chorus & reverb effects send for FM chip: from 0 to 0xff
 * larger numbers often cause weird sounds.
 *----------------------------------------------------------------*/

#define DEF_FM_CHORUS_DEPTH	0x10
#define DEF_FM_REVERB_DEPTH	0x10


/*----------------------------------------------------------------*
 * other compile conditions
 *----------------------------------------------------------------*/

/* initialize FM passthrough even without extended RAM */
#undef AWE_ALWAYS_INIT_FM

/* debug on */
#define AWE_DEBUG_ON

/* GUS compatible mode */
#define AWE_HAS_GUS_COMPATIBILITY

/* add MIDI emulation by wavetable */
#define CONFIG_AWE32_MIDIEMU

/* add mixer control of emu8000 equalizer */
#undef CONFIG_AWE32_MIXER

/* use new volume calculation method as default */
#define AWE_USE_NEW_VOLUME_CALC

/* check current volume target for searching empty voices */
#define AWE_CHECK_VTARGET

/* allow sample sharing */
#define AWE_ALLOW_SAMPLE_SHARING

/*================================================================
 * Usually, you don't have to touch the following options.
 *================================================================*/

/*----------------------------------------------------------------
 * AWE32 card configuration:
 * uncomment the following lines *ONLY* when auto detection doesn't
 * work properly on your machine.
 *----------------------------------------------------------------*/

/*#define AWE_DEFAULT_BASE_ADDR	0x620*/	/* base port address */
/*#define AWE_DEFAULT_MEM_SIZE	512*/	/* kbytes */

/*----------------------------------------------------------------
 * maximum size of soundfont list table
 *----------------------------------------------------------------*/

#define AWE_MAX_SF_LISTS 16

/*----------------------------------------------------------------
 * chunk size of sample and voice tables
 *----------------------------------------------------------------*/

#define AWE_MAX_SAMPLES 400
#define AWE_MAX_INFOS 800

#endif  /* AWE_CONFIG_H_DEF */
