/*
 * sound/awe_compat.h
 *
 * Compat defines for the AWE32/SB32/AWE64 wave table synth driver.
 *   version 0.4.3; Oct. 1, 1998
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

#include "awe_config.h"

#define ASC_LINUX_VERSION(V,P,S)    (((V) * 65536) + ((P) * 256) + (S))

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

/* linux version check */
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(2,0,0)
#define AWE_OBSOLETE_VOXWARE
#endif

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(2,1,0)
#define AWE_NEW_KERNEL_INTERFACE
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(2,1,80)
#define AWE_MODULE_SUPPORT
#endif
#endif

#ifdef AWE_OBSOLETE_VOXWARE
#include "soundvers.h"
#else
#include "../soundvers.h"
#endif

#if defined(SOUND_INTERNAL_VERSION) && SOUND_INTERNAL_VERSION >= 0x30803
/* OSS/Free-3.8 */
#define AWE_NO_PATCHMGR
#define AWE_OSS38
#define HAS_LOWLEVEL_H
#endif

/*================================================================
 * INCLUDE OTHER HEADER FILES
 *================================================================*/

/* set up module */

#if defined(AWE_MODULE_SUPPORT) && defined(MODULE)
#include <linux/config.h>
#include <linux/string.h>
#include <linux/module.h>
#include "../soundmodule.h"
#endif


/* reading configuration of sound driver */

#ifdef AWE_OBSOLETE_VOXWARE

#include "sound_config.h"
#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_AWE32)
#define CONFIG_AWE32_SYNTH
#endif

#else /* AWE_OBSOLETE_VOXWARE */

#ifdef HAS_LOWLEVEL_H
#include "lowlevel.h"
#endif

#include "../sound_config.h"

#endif /* AWE_OBSOLETE_VOXWARE */


/*================================================================
 * include AWE header files
 *================================================================*/

#if defined(CONFIG_AWE32_SYNTH) || defined(CONFIG_AWE32_SYNTH_MODULE)

#include "awe_hw.h"
#include "awe_version.h"
#include <linux/awe_voice.h>

#ifdef AWE_HAS_GUS_COMPATIBILITY
/* include finetune table */
#ifdef AWE_OBSOLETE_VOXWARE
#  include "tuning.h"
#else
#  include "../tuning.h"
#endif
#include <linux/ultrasound.h>
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
  
/* use inline prefix */
#define INLINE	inline

/*----------------------------------------------------------------
 * memory management for linux
 *----------------------------------------------------------------*/

#ifdef AWE_OBSOLETE_VOXWARE
/* old type linux system */

/* i/o requests; nothing */
#define awe_check_port()	0	/* always false */
#define awe_request_region()	/* nothing */
#define awe_release_region()	/* nothing */

static int _mem_start;  /* memory pointer for permanent buffers */

#define my_malloc_init(memptr)	_mem_start = (memptr)
#define my_malloc_memptr()	_mem_start
#define my_free(ptr)	/* do nothing */

/* allocate buffer only once */
#define INIT_TABLE(buffer,index,nums,type) {\
PERMANENT_MALLOC(buffer, char*, size, _mem_start); index = (nums);\
}

#else

#define AWE_DYNAMIC_BUFFER

#define my_malloc_init(ptr)	/* nothing */
#define my_malloc_memptr()	0
#define my_malloc(size)		vmalloc(size)
#define my_free(ptr)		if (ptr) {vfree(ptr);}

/* do not allocate buffer at beginning */
#define INIT_TABLE(buffer,index,nums,type) {buffer=NULL; index=0;}

/* old type macro */
#define RET_ERROR(err)		-err

#endif

/*----------------------------------------------------------------
 * i/o interfaces for linux
 *----------------------------------------------------------------*/

#define OUTW(data,addr)		outw(data, addr)

#ifdef AWE_NEW_KERNEL_INTERFACE
#define COPY_FROM_USER(target,source,offs,count) \
	copy_from_user(target, (source)+(offs), count)
#define GET_BYTE_FROM_USER(target,addr,offs) \
	get_user(target, (unsigned char*)&((addr)[offs]))
#define GET_SHORT_FROM_USER(target,addr,offs) \
	get_user(target, (unsigned short*)&((addr)[offs]))
#ifdef AWE_OSS38
#define IOCTL_TO_USER(target,offs,source,count) \
	memcpy(target, (source)+(offs), count)
#define IO_WRITE_CHECK(cmd)	(_SIOC_DIR(cmd) & _IOC_WRITE)
#else
#define IOCTL_TO_USER(target,offs,source,count) \
	copy_to_user(target, (source)+(offs), count)
#define IO_WRITE_CHECK(cmd)	(_IOC_DIR(cmd) & _IOC_WRITE)
#endif /* AWE_OSS38 */
#define COPY_TO_USER	IOCTL_TO_USER
#define IOCTL_IN(arg)	(*(int*)(arg))
#define IOCTL_OUT(arg,val) (*(int*)(arg) = (val))

#else /* old type i/o */
#define COPY_FROM_USER(target,source,offs,count) \
	memcpy_fromfs(target, (source)+(offs), (count))
#define GET_BYTE_FROM_USER(target,addr,offs) \
	*((char  *)&(target)) = get_fs_byte((addr)+(offs))
#define GET_SHORT_FROM_USER(target,addr,offs) \
	*((short *)&(target)) = get_fs_word((addr)+(offs))
#ifdef AWE_OSS38
#define IOCTL_TO_USER(target,offs,source,count) \
	memcpy(target, (source)+(offs), count)
#define COPY_TO_USER(target,offs,source,count)  \
	memcpy_tofs(target, (source)+(offs), (count))
#define IOCTL_IN(arg)	(*(int*)(arg))
#define IOCTL_OUT(arg,val) (*(int*)(arg) = (val))
#define IO_WRITE_CHECK(cmd)	(_SIOC_DIR(cmd) & _IOC_WRITE)
#else /* AWE_OSS38 */
#define IOCTL_TO_USER(target,offs,source,count) \
	memcpy_tofs(target, (source)+(offs), (count))
#define COPY_TO_USER	IOCTL_TO_USER
#define IOCTL_IN(arg)		get_fs_long((long *)(arg))
#define IOCTL_OUT(arg,ret)	snd_ioctl_return((int *)arg, ret)
#define IO_WRITE_CHECK(cmd)	(cmd & IOC_IN)
#endif /* AWE_OSS38 */

#endif /* AWE_NEW_KERNEL_INTERFACE */

#define BZERO(target,len)	memset(target, 0, len)
#define MEMCPY(dst,src,len)	memcpy(dst, src, len)

/* old style device tables (not modulized) */
#ifndef AWE_MODULE_SUPPORT

#define sound_alloc_synthdev() \
	(num_synths >= MAX_SYNTH_DEV ? -1 : num_synths++)
#define sound_alloc_mixerdev() \
	(num_mixers >= MAX_MIXER_DEV ? -1 : num_mixers++)
#define sound_alloc_mididev() \
	(num_midis >= MAX_MIXER_DEV ? -1 : num_midis++)
#define sound_unload_synthdev(dev)	/**/
#define sound_unload_mixerdev(dev)	/**/
#define sound_unload_mididev(dev)	/**/

#endif /* AWE_MODULE_SUPPORT */

#endif /* CONFIG_AWE32_SYNTH */

#endif /* AWE_COMPAT_H_DEF */
