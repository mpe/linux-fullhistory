
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

 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-eof.c,v $
 $Author: bas $
 *
 $Revision: 1.21 $
 $Date: 1995/05/27 08:54:21 $
 $State: Beta $
 *
 *      This file contains the eof mark handling code
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/ftape.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "tracing.h"
#include "ftape-eof.h"
#include "ftape-write.h"
#include "ftape-read.h"
#include "ftape-rw.h"
#include "ftape-ctl.h"
#include "ftape-bsm.h"

/*      Global vars.
 */
int failed_sector_log_changed = 0;
int eof_mark = 0;

/*      Local vars.
 */
static struct failed_sector_entry {
	unsigned short segment;
	unsigned short sector;
} *eof_mark_ptr;

typedef union {
	struct failed_sector_entry mark;
	unsigned long entry;
} eof_mark_union;

/*  a copy of the failed sector log from the header segment.
 */
static eof_mark_union eof_map[(2048 - 256) / 4];

/*  index into eof_map table pointing to last found eof mark.
 */
static int eof_index;

/*  number of eof marks (entries in bad sector log) on tape.
 */
static int nr_of_eof_marks = -1;

static char linux_tape_label[] = "Linux raw format V";
enum {
	min_fmt_version = 1, max_fmt_version = 2
};
static unsigned ftape_fmt_version = 0;


/*  Ftape (mis)uses the bad sector log to record end-of-file marks.
 *  Initially (when the tape is erased) all entries in the bad sector
 *  log are added to the tape's bad sector map. The bad sector log
 *  then is cleared.
 *
 *  The bad sector log normally contains entries of the form:
 *  even 16-bit word: segment number of bad sector
 *   odd 16-bit word: encoded date
 *  There can be a total of 448 entries (1792 bytes).
 *
 *  My guess is that no program is using this bad sector log (the
 *  format seems useless as there is no indication of the bad sector
 *  itself, only the segment)
 *  However, if any program does use the bad sector log, the format
 *  used by ftape will let the program think there are some bad sectors
 *  and no harm is done.
 *  
 *  The eof mark entries that ftape stores in the bad sector log:
 *  even 16-bit word: segment number of eof mark
 *   odd 16-bit word: sector number of eof mark [1..32]
 *  
 *  The eof_map as maintained is a sorted list of eof mark entries.
 *
 *
 *  The tape name field in the header segments is used to store a
 *  linux tape identification string and a version number.
 *  This way the tape can be recognized as a Linux raw format
 *  tape when using tools under other OS's.
 *
 *  'Wide' QIC tapes (format code 4) don't have a failed sector list
 *  anymore. That space is used for the (longer) bad sector map that
 *  now is a variable length list too.
 *  We now store our end-of-file marker list after the bad-sector-map
 *  on tape. The list is delimited by a (long) 0 entry.
 */

int ftape_validate_label(char *label)
{
	TRACE_FUN(8, "ftape_validate_label");
	int result = 0;

	TRACEx1(4, "tape  label = `%s'", label);
	ftape_fmt_version = 0;
	if (memcmp(label, linux_tape_label, strlen(linux_tape_label)) == 0) {
		int pos = strlen(linux_tape_label);
		while (label[pos] >= '0' && label[pos] <= '9') {
			ftape_fmt_version *= 10;
			ftape_fmt_version = label[pos++] - '0';
		}
		result = (ftape_fmt_version >= min_fmt_version &&
			  ftape_fmt_version <= max_fmt_version);
	}
	TRACEx1(4, "format version = %d", ftape_fmt_version);
	TRACE_EXIT;
	return result;
}

static byte *
 find_end_of_eof_list(byte * ptr, byte * limit)
{
	while (ptr + 3 < limit) {
		if (*(unsigned long *) ptr) {
			++(unsigned long *) ptr;
		} else {
			return ptr;
		}
	}
	return NULL;
}

void reset_eof_list(void)
{
	TRACE_FUN(8, "reset_eof_list");

	eof_mark_ptr = &eof_map[0].mark;
	eof_index = 0;
	eof_mark = 0;
	TRACE_EXIT;
}

/*  Test if `segment' has an eof mark set (optimized for sequential access).
 *  return 0 if not eof mark or sector number (> 0) if eof mark set.
 */
int check_for_eof(unsigned segment)
{
	TRACE_FUN(8, "check_for_eof");
	static unsigned last_reference = INT_MAX;
	int result;

	if (segment < last_reference) {
		reset_eof_list();
	}
	last_reference = segment;
	while (eof_index < nr_of_eof_marks && segment > eof_mark_ptr->segment) {
		++eof_mark_ptr;
		++eof_index;
	}
	if (eof_index < nr_of_eof_marks && segment == eof_mark_ptr->segment) {
		TRACEx3(5, "hit mark %d/%d at index %d",
			eof_map[eof_index].mark.segment, eof_map[eof_index].mark.sector,
			eof_index);
		if (eof_mark_ptr->sector >= SECTORS_PER_SEGMENT) {
			TRACEx2(-1, "Bad file mark detected: %d/%d",
			    eof_mark_ptr->segment, eof_mark_ptr->sector);
			result = 0;	/* return bogus (but valid) value */
		} else {
			result = eof_mark_ptr->sector;
		}
	} else {
		result = 0;
	}
	TRACE_EXIT;
	return result;
}

void clear_eof_mark_if_set(unsigned segment, unsigned byte_count)
{
	TRACE_FUN(5, "clear_eof_mark_if_set");
	if (ftape_fmt_version != 0 &&
	    check_for_eof(segment) > 0 &&
	    byte_count >= eof_mark_ptr->sector * SECTOR_SIZE) {
		TRACEx3(5, "clearing mark %d/%d at index %d",
		 eof_mark_ptr->segment, eof_mark_ptr->sector, eof_index);
		memmove(&eof_map[eof_index], &eof_map[eof_index + 1],
			(nr_of_eof_marks - eof_index) * sizeof(*eof_map));
		--nr_of_eof_marks;
		failed_sector_log_changed = 1;
	}
	TRACE_EXIT;
}

void put_file_mark_in_map(unsigned segment, unsigned sector)
{
	TRACE_FUN(8, "put_file_mark_in_map");
	eof_mark_union new;
	int index;
	eof_mark_union *ptr;

	if (ftape_fmt_version != 0) {
		new.mark.segment = segment;
		new.mark.sector = sector;
		for (index = 0, ptr = &eof_map[0];
		  index < nr_of_eof_marks && ptr->mark.segment < segment;
		     ++index, ++ptr) {
		}
		if (index < nr_of_eof_marks) {
			if (ptr->mark.segment == segment) {
				/* overwrite */
				if (ptr->mark.sector == sector) {
					TRACEx2(5, "mark %d/%d already exists",
						new.mark.segment, new.mark.sector);
				} else {
					TRACEx5(5, "overwriting %d/%d at index %d with %d/%d",
						ptr->mark.segment, ptr->mark.sector, index,
						new.mark.segment, new.mark.sector);
					ptr->entry = new.entry;
					failed_sector_log_changed = 1;
				}
			} else {
				/* insert */
				TRACEx5(5, "inserting %d/%d at index %d before %d/%d",
				new.mark.segment, new.mark.sector, index,
				    ptr->mark.segment, ptr->mark.sector);
				memmove(ptr + 1, ptr, (nr_of_eof_marks - index) * sizeof(*eof_map));
				ptr->entry = new.entry;
				++nr_of_eof_marks;
				failed_sector_log_changed = 1;
			}
		} else {
			/* append */
			TRACEx3(5, "appending %d/%d at index %d",
				new.mark.segment, new.mark.sector, index);
			ptr->entry = new.entry;
			++nr_of_eof_marks;
			failed_sector_log_changed = 1;
		}
	}
	TRACE_EXIT;
}

/*  Write count file marks to tape starting at first non-bad
 *  sector following the given segment and sector.
 *  sector = base 1 !
 */
int ftape_weof(unsigned count, unsigned segment, unsigned sector)
{
	TRACE_FUN(5, "ftape_weof");
	int result = 0;
	unsigned long mask = get_bad_sector_entry(segment);
	unsigned sector_nr = 0;

	if (ftape_fmt_version != 0) {
		if (sector < 1 || sector > 29 ||
		    segment + count >= ftape_last_segment.id) {
			TRACEx3(5, "parameter out of range: %d, %d, %d", count, segment, sector);
			result = -EIO;
		} else {
			while (count-- > 0) {
				do {	/* count logical sectors */
					do {	/* skip until good sector */
						while (mask & 1) {	/* skip bad sectors */
							++sector_nr;
							mask >>= 1;
						}
						if (sector_nr >= 29) {
							if (++segment >= ftape_last_segment.id) {
								TRACEx1(5, "segment out of range: %d", segment);
								result = -EIO;
								break;
							}
							mask = get_bad_sector_entry(segment);
							sector_nr = 0;
						}
					} while (mask & 1);
					++sector_nr;	/* point to good sector */
					mask >>= 1;
				} while (--sector);
				if (result >= 0) {
					TRACEx2(5, "writing filemark %d/%d", segment, sector_nr);
					put_file_mark_in_map(segment, sector_nr);
					++segment;	/* next segment */
					sector_nr = 0;
					sector = 1;	/* first sector */
				}
			}
		}
	} else {
		result = -EPERM;
	}
	TRACE_EXIT;
	return result;
}

int ftape_erase(void)
{
	TRACE_FUN(5, "ftape_erase");
	int result = 0;
	int i;
	unsigned long now = 0;
	byte *buffer = deblock_buffer;

	if (write_protected) {
		result = -EROFS;
	} else {
		result = read_header_segment(buffer);
		if (result >= 0) {
			/*  Copy entries from bad-sector-log into bad-sector-map
			 */
			TRACEx1(5, "old label: `%s'", (char *) (buffer + 30));
			if (!ftape_validate_label((char *) &buffer[30])) {
				TRACE(5, "invalid label, overwriting with new");
				memset(buffer + 30, 0, 44);
				memcpy(buffer + 30, linux_tape_label, strlen(linux_tape_label));
				buffer[30 + strlen(linux_tape_label)] = '2';
				TRACEx1(5, "new label: `%s'", (char *) (buffer + 30));
				PUT4(buffer, 74, now);
				if (format_code != 4) {
					for (i = 0; i < nr_of_eof_marks; ++i) {
						unsigned failing_segment = eof_map[i].mark.segment;

						if (!valid_segment_no(failing_segment)) {
							TRACEi(4, "bad entry in failed sector log:", failing_segment);
						} else {
							put_bad_sector_entry(failing_segment, EMPTY_SEGMENT);
							TRACEx2(4, "moved entry %d from failed sector log (%d)",
								i, failing_segment);
						}
					}
				}
			}
			/*  Clear failed sector log: remove all tape marks
			 */
			failed_sector_log_changed = 1;
			memset(eof_map, 0, sizeof(eof_map));
			nr_of_eof_marks = 0;
			ftape_fmt_version = max_fmt_version;
#if 0
			fix_tape(buffer);	/* see ftape-bsm.c ! */
#endif
			result = ftape_update_header_segments(buffer, 1);
			prevent_flush();	/* prevent flush_buffers writing file marks */
			reset_eof_list();
		}
	}
	TRACE_EXIT;
	return result;
}

void extract_file_marks(byte * address)
{
	TRACE_FUN(8, "extract_file_marks");
	int i;

	if (format_code == 4) {
		byte *end;
		byte *start = find_end_of_bsm_list(address + 256,
					     address + 29 * SECTOR_SIZE);

		memset(eof_map, 0, sizeof(eof_map));
		nr_of_eof_marks = 0;
		if (start) {
			start += 3;	/* skip end of list mark */
			end = find_end_of_eof_list(start, address + 29 * SECTOR_SIZE);
			if (end && end - start <= sizeof(eof_map)) {
				nr_of_eof_marks = (end - start) / sizeof(unsigned long);
				memcpy(eof_map, start, end - start);
			} else {
				TRACE(1, "File Mark List is too long or damaged !");
			}
		} else {
			TRACE(1, "Bad Sector List is too long or damaged !");
		}
	} else {
		memcpy(eof_map, address + 256, sizeof(eof_map));
		nr_of_eof_marks = GET2(address, 144);
	}
	TRACEi(4, "number of file marks:", nr_of_eof_marks);
	if (ftape_fmt_version == 1) {
		TRACE(-1, "swapping version 1 fields");
		/*  version 1 format uses swapped sector and segment fields, correct that !
		 */
		for (i = 0; i < nr_of_eof_marks; ++i) {
			unsigned short tmp = eof_map[i].mark.segment;
			eof_map[i].mark.segment = eof_map[i].mark.sector;
			eof_map[i].mark.sector = tmp;
		}
	}
	for (i = 0; i < nr_of_eof_marks; ++i) {
		TRACEx2(4, "eof mark: %5d/%2d",
			eof_map[i].mark.segment, eof_map[i].mark.sector);
	}
	reset_eof_list();
	TRACE_EXIT;
}

int update_failed_sector_log(byte * buffer)
{
	TRACE_FUN(8, "update_failed_sector_log");

	if (ftape_fmt_version != 0 && failed_sector_log_changed) {
		if (ftape_fmt_version == 1) {
			TRACE(-1, "upgrading version 1 format to version 2");
			/*  version 1 will be upgraded to version 2 when written.
			 */
			buffer[30 + strlen(linux_tape_label)] = '2';
			ftape_fmt_version = 2;
			TRACEx1(-1, "new tape label = \"%s\"", &buffer[30]);
		}
		if (format_code == 4) {
			byte *dest = find_end_of_bsm_list(buffer + 256,
					  buffer + 29 * SECTOR_SIZE) + 3;

			if (dest) {
				TRACEx2(4, "eof_map at byte offset %6d, size %d",
					dest - buffer - 256, nr_of_eof_marks * sizeof(unsigned long));
				memcpy(dest, eof_map, nr_of_eof_marks * sizeof(unsigned long));
				PUT4(dest, nr_of_eof_marks * sizeof(unsigned long), 0);
			}
		} else {
			memcpy(buffer + 256, eof_map, sizeof(eof_map));
			PUT2(buffer, 144, nr_of_eof_marks);
		}
		failed_sector_log_changed = 0;
		return 1;
	}
	TRACE_EXIT;
	return 0;
}

int ftape_seek_eom(void)
{
	TRACE_FUN(5, "ftape_seek_eom");
	int result = 0;
	unsigned eom;

	if (first_data_segment == -1) {
		result = read_header_segment(deblock_buffer);
	}
	if (result >= 0 && ftape_fmt_version != 0) {
		eom = first_data_segment;
		eof_index = 0;
		eof_mark_ptr = &eof_map[0].mark;
		/*  If fresh tape, count should be zero but we don't
		 *  want to worry about the case it's one.
		 */
		for (eof_index = 1, eof_mark_ptr = &eof_map[1].mark;
		     eof_index < nr_of_eof_marks; ++eof_index, ++eof_mark_ptr) {
			/*  The eom is recorded as two eof marks in succeeding segments
			 *  where the second one is always at segment number 1.
			 */
			if (eof_mark_ptr->sector == 1) {
				if (eof_mark_ptr->segment == (eof_mark_ptr - 1)->segment + 1) {
					eom = eof_mark_ptr->segment;
					break;
				}
			}
		}
		ftape_seg_pos = eom;
		TRACEx1(5, "eom found at segment %d", eom);
	} else {
		TRACE(5, "Couldn't get eof mark table");
		result = -EIO;
	}
	TRACE_EXIT;
	return result;
}

int ftape_seek_eof(unsigned count)
{
	TRACE_FUN(5, "ftape_seek_eof");
	int result = 0;
	enum {
		not = 0, begin, end
	} bad_seek = not;

	if (first_data_segment == -1) {
		result = read_header_segment(deblock_buffer);
	}
	TRACEx1(5, "tape positioned at segment %d", ftape_seg_pos);
	if (ftape_fmt_version == 0) {
		result = -1;
	}
	if (result >= 0 && count != 0) {
		for (eof_index = 0; eof_index <= nr_of_eof_marks; ++eof_index) {
			if (eof_index == nr_of_eof_marks ||	/* start seeking after last mark */
			    ftape_seg_pos <= eof_map[eof_index].mark.segment) {
				eof_index += count;
				if (eof_index < 1) {	/* begin of tape */
					ftape_seg_pos = first_data_segment;
					if (eof_index < 0) {	/* `before' begin of tape */
						eof_index = 0;
						bad_seek = begin;
					}
				} else if (eof_index >= nr_of_eof_marks) {	/* `after' end of tape */
					ftape_seg_pos = segments_per_track * tracks_per_tape;
					if (eof_index > nr_of_eof_marks) {
						eof_index = nr_of_eof_marks;
						bad_seek = end;
					}
				} else {	/* after requested file mark */
					ftape_seg_pos = eof_map[eof_index - 1].mark.segment + 1;
				}
				eof_mark_ptr = &eof_map[eof_index].mark;
				break;
			}
		}
	}
	if (result < 0) {
		TRACE(5, "Couldn't get eof mark table");
		result = -EIO;
	} else if (bad_seek != not) {
		TRACEx1(1, "seek reached %s of tape",
			(bad_seek == begin) ? "begin" : "end");
		result = -EIO;
	} else {
		TRACEx1(5, "tape repositioned to segment %d", ftape_seg_pos);
	}
	TRACE_EXIT;
	return result;
}

int ftape_file_no(daddr_t * f_no, daddr_t * b_no)
{
	TRACE_FUN(5, "ftape_file_no");
	int result = 0;
	int i;

	*f_no = eof_index;
	*b_no = ftape_seg_pos;
	TRACEi(4, "number of file marks:", nr_of_eof_marks);
	for (i = 0; i < nr_of_eof_marks; ++i) {
		TRACEx2(4, "eof mark: %5d/%2d",
			eof_map[i].mark.segment, eof_map[i].mark.sector);
	}
	TRACE_EXIT;
	return result;
}
