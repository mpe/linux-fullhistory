/*
 * sound/awe_compat.h
 *
 * Compat defines for the AWE32/SB32/AWE64 wave table synth driver.
 *   version 0.4.3; Nov. 1, 1998
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

#ifndef AWE_COMPAT_H_DEF
#define AWE_COMPAT_H_DEF

/*================================================================
 * version check
 *================================================================*/

/* FreeBSD version check */
#include <i386/isa/sound/awe_config.h>

#define AWE_OBSOLETE_VOXWARE
#if __FreeBSD__ >= 2
#  include <osreldate.h>
#  if __FreeBSD_version >= 300000
#    undef AWE_OBSOLETE_VOXWARE
#  endif
#endif
#ifdef __linux__
#  include <linux/config.h>
#endif


/*================================================================
 * INCLUDE OTHER HEADER FILES
 *================================================================*/

/* reading configuration of sound driver */

#ifdef AWE_OBSOLETE_VOXWARE

#include <i386/isa/sound/sound_config.h>
#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_AWE32)
#define CONFIG_AWE32_SYNTH
#endif

#else /* AWE_OBSOLETE_VOXWARE */

#ifdef HAS_LOWLEVEL_H
#include "lowlevel.h"
#endif

#include <i386/isa/sound/sound_config.h>
#if defined(CONFIGURE_SOUNDCARD) && defined(CONFIG_AWE32)
#  define CONFIG_AWE32_SYNTH
#endif

#endif /* AWE_OBSOLETE_VOXWARE */


/*================================================================
 * include AWE header files
 *================================================================*/

#if defined(CONFIG_AWE32_SYNTH) || defined(CONFIG_AWE32_SYNTH_MODULE)

#include <i386/isa/sound/awe_hw.h>
#include <i386/isa/sound/awe_version.h>
#include <i386/isa/sound/awe_voice.h>

#ifdef AWE_HAS_GUS_COMPATIBILITY
/* include finetune table */
#ifdef AWE_OBSOLETE_VOXWARE
#  define SEQUENCER_C
#endif
#include <i386/isa/sound/tuning.h>
#include <machine/ultrasound.h>
#endif /* AWE_HAS_GUS_COMPATIBILITY */


/*----------------------------------------------------------------
 * compatibility macros for AWE32 driver
 *----------------------------------------------------------------*/

/* redefine following macros */
#undef IOCTL_IN
#undef IOCTL_OUT
#undef OUTW
#undef COPY_FROM_USER
#undef COPY_TO_USER
#undef GET_BYTE_FROM_USER
#undef GET_SHORT_FROM_USER
#undef IOCTL_TO_USER
  
/* inline is not checked yet.. maybe it'll work */
#define INLINE	/*inline*/

#define KERN_WARNING /**/

/*----------------------------------------------------------------
 * memory management for freebsd
 *----------------------------------------------------------------*/

/* i/o requests; nothing */
#define awe_check_port()	0	/* always false */
#define awe_request_region()	/* nothing */
#define awe_release_region()	/* nothing */

#define AWE_DYNAMIC_BUFFER

#define my_malloc_init(ptr)	/* nothing */
#define my_malloc_memptr()	0
#define my_malloc(size)		malloc(size, M_TEMP, M_WAITOK)
#define my_free(ptr)		if (ptr) {free(ptr, M_TEMP);}

#define INIT_TABLE(buffer,index,nums,type) {buffer=NULL; index=0;}

/*----------------------------------------------------------------
 * i/o interfaces for freebsd
 *----------------------------------------------------------------*/

/* according to linux rule; the arguments are swapped */
#define OUTW(data,addr)		outw(addr, data)

#define COPY_FROM_USER(target,source,offs,count) \
	uiomove(((caddr_t)(target)),(count),((struct uio *)(source)))
#define COPY_TO_USER(target,source,offs,count) \
	uiomove(((caddr_t)(source)),(count),((struct uio *)(target)))
#define GET_BYTE_FROM_USER(target,addr,offs) \
	uiomove(((char*)&(target)), 1, ((struct uio *)(addr)))
#define GET_SHORT_FROM_USER(target,addr,offs) \
	uiomove(((char*)&(target)), 2, ((struct uio *)(addr)))
#define IOCTL_TO_USER(target,offs,source,count) \
	memcpy(&((target)[offs]), (source), (count))
#define IO_WRITE_CHECK(cmd)	(cmd & IOC_IN)
#define IOCTL_IN(arg)		(*(int*)(arg))
#define IOCTL_OUT(arg,val)	(*(int*)(arg) = (val))
#define BZERO(target,len)	bzero((caddr_t)target, len)
#define MEMCPY(dst,src,len)	bcopy((caddr_t)src, (caddr_t)dst, len)

#ifndef AWE_OBSOLETE_VOXWARE
#  define printk printf
#  define RET_ERROR(err)		-err
#endif


/* old style device tables (not modulized) */
#define sound_alloc_synthdev() \
	(num_synths >= MAX_SYNTH_DEV ? -1 : num_synths++)
#define sound_alloc_mixerdev() \
	(num_mixers >= MAX_MIXER_DEV ? -1 : num_mixers++)
#define sound_alloc_mididev() \
	(num_midis >= MAX_MIXER_DEV ? -1 : num_midis++)
#define sound_unload_synthdev(dev)	/**/
#define sound_unload_mixerdev(dev)	/**/
#define sound_unload_mididev(dev)	/**/


#endif /* CONFIG_AWE32_SYNTH */

#endif /* AWE_COMPAT_H_DEF */
