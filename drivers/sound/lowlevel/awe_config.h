/*
 * sound/awe_config.h
 *
 * Configuration of AWE32 sound driver
 *   version 0.2.99e; Dec. 10, 1997
 *
 * Copyright (C) 1996,1997 Takashi Iwai
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

/* if you're using obsolete VoxWare 3.0.x on Linux 1.2.x (or FreeBSD),
 * define the following line.
 */
#undef AWE_OBSOLETE_VOXWARE

#ifdef __FreeBSD__
#  define AWE_OBSOLETE_VOXWARE
#endif

/* if you're using OSS-Lite on Linux 2.1.6 or later, define the
 * following line.
 */
#define AWE_NEW_KERNEL_INTERFACE

/* if you have lowlevel.h in the lowlevel directory (OSS-Lite), define
 * the following line.
 */
#define HAS_LOWLEVEL_H

/* if your system doesn't support patch manager (OSS 3.7 or newer),
 * define the following line.
 */
#define AWE_NO_PATCHMGR
 

/*----------------------------------------------------------------
 * AWE32 card configuration:
 * uncomment the following lines only when auto detection doesn't
 * work properly on your machine.
 *----------------------------------------------------------------*/

/*#define AWE_DEFAULT_BASE_ADDR	0x620*/	/* base port address */
/*#define AWE_DEFAULT_MEM_SIZE	512*/	/* kbytes */


/*----------------------------------------------------------------
 * maximum size of sample table:
 * the followings are for ROM GM and 512k GS samples.  if your have
 * additional DRAM and SoundFonts, increase these values.
 *----------------------------------------------------------------*/

#define AWE_MAX_SAMPLES 400
#define AWE_MAX_INFOS 1500


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

/* verify checksum for uploading samples */
#define AWE_CHECKSUM_DATA
#define AWE_CHECKSUM_MEMORY

/* GUS compatible mode */
#define AWE_HAS_GUS_COMPATIBILITY

/* accept all notes/sounds off controls */
#undef AWE_ACCEPT_ALL_SOUNDS_CONTROL


#ifdef linux
/* i tested this only on my linux */
#define INLINE  __inline__
#else
#define INLINE /**/
#endif


/*----------------------------------------------------------------*/

/* reading configuration of sound driver */

#ifdef AWE_OBSOLETE_VOXWARE

#ifdef __FreeBSD__
#  include <i386/isa/sound/sound_config.h>
#else
#  include "sound_config.h"
#endif

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_AWE32)
#define CONFIG_AWE32_SYNTH
#endif

#else /* AWE_OBSOLETE_VOXWARE */

#ifdef HAS_LOWLEVEL_H
#include "lowlevel.h"
#endif

#include "../sound_config.h"

#endif /* AWE_OBSOLETE_VOXWARE */


#endif  /* AWE_CONFIG_H_DEF */
