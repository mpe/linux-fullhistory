#ifndef _FTAPE_BSM_H
#define _FTAPE_BSM_H

/*
 * Copyright (C) 1994-1995 Bas Laarhoven.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-bsm.h,v $
 $Author: bas $
 *
 $Revision: 1.5 $
 $Date: 1995/04/22 07:30:15 $
 $State: Beta $
 *
 *      This file contains definitions for the bad sector map handling
 *      routines for the QIC-117 floppy-tape driver for Linux.
 */

#define EMPTY_SEGMENT           (0xffffffff)
#define FAKE_SEGMENT            (0xfffffffe)

/*  failed sector log size (only used if format code != 4).
 */
#define FAILED_SECTOR_LOG_SIZE  (2 * SECTOR_SIZE - 256)

/*  maximum (format code 4) bad sector map size (bytes).
 */
#define BAD_SECTOR_MAP_SIZE     (29 * SECTOR_SIZE - 256)

/*
 *      ftape-io.c defined global vars.
 */
extern bad_sector_map_changed;

/*
 *      ftape-io.c defined global functions.
 */
extern void update_bad_sector_map(byte * buffer);
extern void store_bad_sector_map(byte * buffer);
extern void extract_bad_sector_map(byte * buffer);
extern unsigned long get_bad_sector_entry(int segment_id);
extern void put_bad_sector_entry(int segment_id, unsigned long mask);
extern void add_segment_to_bad_sector_map(unsigned segment);
extern void clear_bad_sector_map(int count);
extern byte *find_end_of_bsm_list(byte * ptr, byte * limit);
extern void ftape_init_bsm(void);

#endif
