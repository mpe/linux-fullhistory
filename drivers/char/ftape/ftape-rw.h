#ifndef _FTAPE_RW_H
#define _FTAPE_RW_H

/*
 * Copyright (C) 1993-1995 Bas Laarhoven.

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-rw.h,v $
 $Author: bas $
 *
 $Revision: 1.33 $
 $Date: 1995/04/22 07:30:15 $
 $State: Beta $
 *
 *      This file contains the definitions for the read and write
 *      functions for the QIC-117 floppy-tape driver for Linux.
 *
 */

#include "fdc-io.h"
#include "kernel-interface.h"

#define GET2( address, offset) *(short*)(address + offset)
#define GET4( address, offset) *(long*)(address + offset)
#define PUT2( address, offset, value) *(short*)(address + offset) = value
#define PUT4( address, offset, value) *(long*)(address + offset) = value

enum runner_status_enum {
	idle = 0,
	running,
	do_abort,
	aborting,
	logical_eot,
	end_of_tape,
	buffer_overrun,
	buffer_underrun,
};

typedef struct {
	byte *address;
	volatile buffer_state_enum status;
	volatile byte *ptr;
	volatile unsigned bytes;
	volatile unsigned segment_id;

	/* bitmap for remainder of segment not yet handled.
	 * one bit set for each bad sector that must be skipped.
	 */
	volatile unsigned long bad_sector_map;

	/* bitmap with bad data blocks in data buffer.
	 * the errors in this map may be retried.
	 */
	volatile unsigned long soft_error_map;

	/* bitmap with bad data blocks in data buffer
	 * the errors in this map may not be retried.
	 */
	volatile unsigned long hard_error_map;

	/* retry counter for soft errors.
	 */
	volatile int retry;

	/* sectors to skip on retry ???
	 */
	volatile unsigned int skip;

	/* nr of data blocks in data buffer
	 */
	volatile unsigned data_offset;

	/* offset in segment for first sector to be handled.
	 */
	volatile unsigned sector_offset;

	/* size of cluster of good sectors to be handled.
	 */
	volatile unsigned sector_count;

	/* size of remaining part of segment to be handled.
	 */
	volatile unsigned remaining;

	/* points to next segment (contiguous) to be handled,
	 * or is zero if no read-ahead is allowed.
	 */
	volatile unsigned next_segment;

	/* flag being set if deleted data was read.
	 */
	volatile int deleted;

	volatile byte head;
	volatile byte cyl;
	volatile byte sect;
} buffer_struct;

typedef struct {
	int active;
	int error;
	int offset;
} ftape_fast_start_struct;

typedef struct {
	int id;
	int size;
	int free;
} ftape_last_segment_struct;

typedef struct {
	int track;		/* tape head position */
	volatile int known;	/* validates bot, segment, sector */
	volatile int bot;	/* logical begin of track */
	volatile int eot;	/* logical end of track */
	volatile int segment;	/* current segment */
	volatile int sector;	/* sector offset within current segment */
} location_record;

typedef enum {
	fmt_normal = 2, fmt_1100ft = 3, fmt_wide = 4, fmt_425ft = 5
} format_type;

/*      ftape-rw.c defined global vars.
 */
extern int tracing;
extern byte trace_id;
extern buffer_struct buffer[];
extern location_record location;
extern volatile ftape_fast_start_struct ftape_fast_start;
extern byte deblock_buffer[(SECTORS_PER_SEGMENT - 3) * SECTOR_SIZE];
extern byte scratch_buffer[(SECTORS_PER_SEGMENT - 3) * SECTOR_SIZE];
extern ftape_last_segment_struct ftape_last_segment;
extern int header_segment_1;
extern int header_segment_2;
extern int used_header_segment;
extern unsigned int fast_seek_segment_time;
extern volatile int tape_running;
extern format_type format_code;

/*      ftape-rw.c defined global functions.
 */
extern int count_ones(unsigned long mask);
extern int valid_segment_no(unsigned segment);
extern int setup_new_segment(buffer_struct * buff, unsigned int segment_id,
			     int offset);
extern int calc_next_cluster(buffer_struct * buff);
extern buffer_struct *next_buffer(volatile int *x);
extern int ftape_read_id(void);
extern void ftape_tape_parameters(byte drive_configuration);
extern int wait_segment(buffer_state_enum state);
extern int ftape_dumb_stop(void);
extern int ftape_start_tape(int segment_id, int offset);

/*      fdc-io.c defined global functions.
 */
extern int setup_fdc_and_dma(buffer_struct * buff, byte operation);

#endif				/* _FTAPE_RW_H */
