/*
 * sound/awe_version.h
 *
 * Version defines for the AWE32/SB32/AWE64 wave table synth driver.
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

/* AWE32 driver version number */

#ifndef AWE_VERSION_H_DEF
#define AWE_VERSION_H_DEF

#define AWE_MAJOR_VERSION	0
#define AWE_MINOR_VERSION	4
#define AWE_TINY_VERSION	3
#define AWE_VERSION_NUMBER	((AWE_MAJOR_VERSION<<16)|(AWE_MINOR_VERSION<<8)|AWE_TINY_VERSION)
#define AWEDRV_VERSION		"0.4.3"

#endif
