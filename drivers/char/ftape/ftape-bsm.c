/*
 *      Copyright (C) 1994-1995 Bas Laarhoven.

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

 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-bsm.c,v $
 $Author: bas $
 *
 $Revision: 1.7 $
 $Date: 1995/04/30 13:15:14 $
 $State: Beta $
 *
 *      This file contains the bad-sector map handling code for
 *      the QIC-117 floppy tape driver for Linux.
 *      QIC-40, QIC-80, QIC-3010 and QIC-3020 maps are implemented.
 */

#include <linux/ftape.h>
#include <linux/string.h>

#include "tracing.h"
#include "ftape-bsm.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"


/*      Global vars.
 */
int bad_sector_map_changed = 0;

/*      Local vars.
 */
static byte bad_sector_map[BAD_SECTOR_MAP_SIZE];
typedef enum {
	forward, backward
} mode_type;

#if 0
/*  fix_tape converts a normal QIC-80 tape into a 'wide' tape.
 *  For testing purposes only !
 */
void fix_tape(byte * buffer)
{
	static byte list[BAD_SECTOR_MAP_SIZE];
	unsigned long *src_ptr = (unsigned long *) list;
	byte *dst_ptr = bad_sector_map;
	unsigned long map;
	unsigned sector = 1;
	int i;

	memcpy(list, bad_sector_map, sizeof(list));
	memset(bad_sector_map, 0, sizeof(bad_sector_map));
	while ((byte *) src_ptr - list < sizeof(list)) {
		map = *src_ptr++;
		if (map == EMPTY_SEGMENT) {
			*(unsigned long *) dst_ptr = 0x800000 + sector;
			dst_ptr += 3;
			sector += SECTORS_PER_SEGMENT;
		} else {
			for (i = 0; i < SECTORS_PER_SEGMENT; ++i) {
				if (map & 1) {
					*(unsigned long *) dst_ptr = sector;
					dst_ptr += 3;
				}
				map >>= 1;
				++sector;
			}
		}
	}
	bad_sector_map_changed = 1;
	*(buffer + 4) = 4;	/* put new format code */
	format_code = 4;
}

#endif

byte *
 find_end_of_bsm_list(byte * ptr, byte * limit)
{
	while (ptr + 2 < limit) {
		if (ptr[0] || ptr[1] || ptr[2]) {
			ptr += 3;
		} else {
			return ptr;
		}
	}
	return NULL;
}

void store_bad_sector_map(byte * buffer)
{
	TRACE_FUN(8, "store_bad_sector_map");
	size_t count;
	size_t offset;

	/*  Store the bad sector map in buffer.
	 */
	if (format_code == 4) {
		offset = 256;
		count = sizeof(bad_sector_map);
	} else {
		offset = 2 * SECTOR_SIZE;	/* skip failed sector log */
		count = sizeof(bad_sector_map) - (offset - 256);
	}
	memcpy(buffer + offset, bad_sector_map, count);
	TRACE_EXIT;
}

void put_sector(byte ** ptr, unsigned long sector)
{
	*(*ptr)++ = sector & 0xff;
	sector >>= 8;
	*(*ptr)++ = sector & 0xff;
	sector >>= 8;
	*(*ptr)++ = sector & 0xff;
}

unsigned long get_sector(byte ** ptr, mode_type mode)
{
	unsigned long sector;

	if (mode == forward) {
		sector = *(*ptr)++;
		sector += *(*ptr)++ << 8;
		sector += *(*ptr)++ << 16;
	} else {
		sector = *--(*ptr) << 16;
		sector += *--(*ptr) << 8;
		sector += *--(*ptr);
	}
	return sector;
}

void extract_bad_sector_map(byte * buffer)
{
	TRACE_FUN(8, "extract_bad_sector_map");

	/*  Fill the bad sector map with the contents of buffer.
	 */
	if (format_code == 4) {
		/* QIC-3010/3020 and wide QIC-80 tapes no longer have a failed
		 * sector log but use this area to extend the bad sector map.
		 */
		memcpy(bad_sector_map, buffer + 256, sizeof(bad_sector_map));
	} else {
		/* non-wide QIC-80 tapes have a failed sector log area that
		 * mustn't be included in the bad sector map.
		 */
		memcpy(bad_sector_map, buffer + 256 + FAILED_SECTOR_LOG_SIZE,
		       sizeof(bad_sector_map) - FAILED_SECTOR_LOG_SIZE);
	}
#if 0
	/* for testing of bad sector handling at end of tape
	 */
	((unsigned long *) bad_sector_map)[segments_per_track * tracks_per_tape - 3] = 0x000003e0;
	((unsigned long *) bad_sector_map)[segments_per_track * tracks_per_tape - 2] = 0xff3fffff;
	((unsigned long *) bad_sector_map)[segments_per_track * tracks_per_tape - 1] = 0xffffe000;
#endif
#if 0
	/*  Enable to test bad sector handling
	 */
	((unsigned long *) bad_sector_map)[30] = 0xfffffffe;
	((unsigned long *) bad_sector_map)[32] = 0x7fffffff;
	((unsigned long *) bad_sector_map)[34] = 0xfffeffff;
	((unsigned long *) bad_sector_map)[36] = 0x55555555;
	((unsigned long *) bad_sector_map)[38] = 0xffffffff;
	((unsigned long *) bad_sector_map)[50] = 0xffff0000;
	((unsigned long *) bad_sector_map)[51] = 0xffffffff;
	((unsigned long *) bad_sector_map)[52] = 0xffffffff;
	((unsigned long *) bad_sector_map)[53] = 0x0000ffff;
#endif
#if 0
	/*  Enable when testing multiple volume tar dumps.
	 */
	for (i = first_data_segment; i <= ftape_last_segment.id - 7; ++i) {
		((unsigned long *) bad_sector_map)[i] = EMPTY_SEGMENT;
	}
#endif
#if 0
	/*  Enable when testing bit positions in *_error_map
	 */
	for (i = first_data_segment; i <= ftape_last_segment.id; ++i) {
		((unsigned long *) bad_sector_map)[i] |= 0x00ff00ff;
	}
#endif
	if (tracing > 2) {
		unsigned int map;
		int good_sectors = 0;
		int bad_sectors;
		unsigned int total_bad = 0;
		int i;

		if (format_code == 4 || format_code == 3) {
			byte *ptr = bad_sector_map;
			unsigned sector;

			do {
				sector = get_sector(&ptr, forward);
				if (sector != 0) {
					if (format_code == 4 && sector & 0x800000) {
						total_bad += SECTORS_PER_SEGMENT - 3;
						TRACEx1(6, "bad segment at sector: %6d", sector & 0x7fffff);
					} else {
						++total_bad;
						TRACEx1(6, "bad sector: %6d", sector);
					}
				}
			} while (sector != 0);
			/*  Display end-of-file marks
			 */
			do {
				sector = *((unsigned short *) ptr)++;
				if (sector) {
					TRACEx2(4, "eof mark: %4d/%2d", sector,
					    *((unsigned short *) ptr)++);
				}
			} while (sector);
		} else {
			for (i = first_data_segment;
			 i < segments_per_track * tracks_per_tape; ++i) {
				map = ((unsigned long *) bad_sector_map)[i];
				bad_sectors = count_ones(map);
				if (bad_sectors > 0) {
					TRACEx2(6, "bsm for segment %4d: 0x%08x", i, map);
					if (bad_sectors > SECTORS_PER_SEGMENT - 3) {
						bad_sectors = SECTORS_PER_SEGMENT - 3;
					}
					total_bad += bad_sectors;
				}
			}
		}
		good_sectors = ((segments_per_track * tracks_per_tape - first_data_segment)
				* (SECTORS_PER_SEGMENT - 3)) - total_bad;
		TRACEx1(3, "%d Kb usable on this tape",
			good_sectors - ftape_last_segment.free);
		if (total_bad == 0) {
			TRACE(1, "WARNING: this tape has no bad blocks registered !");
		} else {
			TRACEx1(2, "%d bad sectors", total_bad);
		}
	}
	TRACE_EXIT;
}

unsigned long cvt2map(int sector)
{
	return 1 << (((sector & 0x7fffff) - 1) % SECTORS_PER_SEGMENT);
}

int cvt2segment(int sector)
{
	return ((sector & 0x7fffff) - 1) / SECTORS_PER_SEGMENT;
}

int forward_seek_entry(int segment_id, byte ** ptr, unsigned long *map)
{
	byte *tmp_ptr;
	unsigned long sector;
	int segment;
	int count;

	do {
		sector = get_sector(ptr, forward);
		segment = cvt2segment(sector);
	} while (sector != 0 && segment < segment_id);
	tmp_ptr = *ptr - 3;	/* point to first sector >= segment_id */
	/*  Get all sectors in segment_id
	 */
	if (format_code == 4 && (sector & 0x800000) && segment == segment_id) {
		*map = EMPTY_SEGMENT;
		count = 32;
	} else {
		*map = 0;
		count = 0;
		while (sector != 0 && segment == segment_id) {
			*map |= cvt2map(sector);
			sector = get_sector(ptr, forward);
			segment = cvt2segment(sector);
			++count;
		}
	}
	*ptr = tmp_ptr;
	return count;
}

int backwards_seek_entry(int segment_id, byte ** ptr, unsigned long *map)
{
	unsigned long sector;
	int segment;
	int count;

	*map = 0;
	if (*ptr > bad_sector_map) {
		do {
			sector = get_sector(ptr, backward);
			segment = cvt2segment(sector);
		} while (*ptr > bad_sector_map && segment > segment_id);
		count = 0;
		if (segment > segment_id) {
			/*  at start of list, no entry found */
		} else if (segment < segment_id) {
			/*  before smaller entry, adjust for overshoot */
			*ptr += 3;
		} else {
			/*  get all sectors in segment_id */
			if (format_code == 4 && (sector & 0x800000)) {
				*map = EMPTY_SEGMENT;
				count = 32;
			} else {
				do {
					*map |= cvt2map(sector);
					++count;
					if (*ptr <= bad_sector_map) {
						break;
					}
					sector = get_sector(ptr, backward);
					segment = cvt2segment(sector);
				} while (segment == segment_id);
				if (segment < segment_id) {
					*ptr += 3;
				}
			}
		}
	} else {
		count = 0;
	}
	return count;
}

void put_bad_sector_entry(int segment_id, unsigned long new_map)
{
	byte *ptr = bad_sector_map;
	int count;
	int new_count;
	unsigned long map;

	if (format_code == 3 || format_code == 4) {
		count = forward_seek_entry(segment_id, &ptr, &map);
		new_count = count_ones(new_map);
		/*  If format code == 4 put empty segment instead of 32 bad sectors.
		 */
		if (new_count == 32 && format_code == 4) {
			new_count = 1;
		}
		if (count != new_count) {
			/*  insert (or delete if < 0) new_count - count entries.
			 *  Move trailing part of list including terminating 0.
			 */
			byte *hi_ptr = ptr;

			do {
			} while (get_sector(&hi_ptr, forward) != 0);
			memmove(ptr + new_count, ptr + count, hi_ptr - (ptr + count));
		}
		if (new_count == 1 && new_map == EMPTY_SEGMENT) {
			put_sector(&ptr, 0x800001 + segment_id * SECTORS_PER_SEGMENT);
		} else {
			int i = 0;

			while (new_map) {
				if (new_map & 1) {
					put_sector(&ptr, 1 + segment_id * SECTORS_PER_SEGMENT + i);
				}
				++i;
				new_map >>= 1;
			}
		}
	} else {
		((unsigned long *) bad_sector_map)[segment_id] = new_map;
	}
	bad_sector_map_changed = 1;
}

unsigned long get_bad_sector_entry(int segment_id)
{
	TRACE_FUN(8, "get_bad_sector_entry");
	static unsigned long map = 0;

	if (used_header_segment == -1) {
		/*  When reading header segment we'll need a blank map.
		 */
		map = 0;
	} else if (format_code == 3 || format_code == 4) {
		/*  Invariants:
		 *    map - mask value returned on last call.
		 *    ptr - points to first sector greater or equal to
		 *          first sector in last_referenced segment.
		 *    last_referenced - segment id used in the last call,
		 *                      sector and map belong to this id.
		 *  This code is designed for sequential access and retries.
		 *  For true random access it may have to be redesigned.
		 */
		static int last_reference = -1;
		static byte *ptr = bad_sector_map;

		if (segment_id > last_reference) {
			/*  Skip all sectors before segment_id
			 */
			forward_seek_entry(segment_id, &ptr, &map);
		} else if (segment_id < last_reference) {
			/*  Skip backwards until begin of buffer or first sector in segment_id
			 */
			backwards_seek_entry(segment_id, &ptr, &map);
		}		/* segment_id == last_reference : keep map */
		last_reference = segment_id;
	} else {
		map = ((unsigned long *) bad_sector_map)[segment_id];
	}
	TRACE_EXIT;
	return map;
}

void ftape_init_bsm(void)
{
	memset(bad_sector_map, 0, sizeof(bad_sector_map));
}
