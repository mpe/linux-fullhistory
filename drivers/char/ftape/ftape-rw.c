/*
 *      Copyright (C) 1993-1995 Bas Laarhoven.

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

 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-rw.c,v $
 $Author: bas $
 *
 $Revision: 1.54 $
 $Date: 1995/05/27 08:55:27 $
 $State: Beta $
 *
 *      This file contains some common code for the segment read and segment
 *      write routines for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ftape.h>

#include "tracing.h"
#include "ftape-rw.h"
#include "fdc-io.h"
#include "kernel-interface.h"
#include "qic117.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-read.h"
#include "ftape-eof.h"
#include "ecc.h"
#include "ftape-bsm.h"

/*      Global vars.
 */
volatile enum runner_status_enum runner_status = idle;
byte deblock_buffer[(SECTORS_PER_SEGMENT - 3) * SECTOR_SIZE];
byte scratch_buffer[(SECTORS_PER_SEGMENT - 3) * SECTOR_SIZE];
buffer_struct buffer[NR_BUFFERS];
struct wait_queue *wait_intr = NULL;
volatile int head;
volatile int tail;		/* not volatile but need same type as head */
int fdc_setup_error;
ftape_last_segment_struct ftape_last_segment;
int header_segment_1 = -1;
int header_segment_2 = -1;
int used_header_segment = -1;
location_record location =
{-1, 0};
volatile int tape_running = 0;
format_type format_code;

/*      Local vars.
 */
static int overrun_count_offset = 0;
static int inhibit_correction = 0;


/*      Increment cyclic buffer nr.
 */
buffer_struct *
 next_buffer(volatile int *x)
{
	if (++*x >= NR_BUFFERS) {
		*x = 0;
	}
	return &buffer[*x];
}

int valid_segment_no(unsigned segment)
{
	return (segment >= first_data_segment && segment <= ftape_last_segment.id);
}

/*      Count nr of 1's in pattern.
 */
int count_ones(unsigned long mask)
{
	int bits;

	for (bits = 0; mask != 0; mask >>= 1) {
		if (mask & 1) {
			++bits;
		}
	}
	return bits;
}

/*      Calculate Floppy Disk Controller and DMA parameters for a segment.
 *      head:   selects buffer struct in array.
 *      offset: number of physical sectors to skip (including bad ones).
 *      count:  number of physical sectors to handle (including bad ones).
 */
static int setup_segment(buffer_struct * buff, unsigned int segment_id,
	unsigned int sector_offset, unsigned int sector_count, int retry)
{
	TRACE_FUN(8, "setup_segment");
	unsigned long offset_mask;
	unsigned long mask;

	buff->segment_id = segment_id;
	buff->sector_offset = sector_offset;
	buff->remaining = sector_count;
	buff->head = segment_id / segments_per_head;
	buff->cyl = (segment_id % segments_per_head) / segments_per_cylinder;
	buff->sect = (segment_id % segments_per_cylinder) * SECTORS_PER_SEGMENT + 1;
	buff->deleted = 0;
	offset_mask = (1 << buff->sector_offset) - 1;
	mask = get_bad_sector_entry(segment_id) & offset_mask;
	while (mask) {
		if (mask & 1) {
			offset_mask >>= 1;	/* don't count bad sector */
		}
		mask >>= 1;
	}
	buff->data_offset = count_ones(offset_mask);	/* good sectors to skip */
	buff->ptr = buff->address + buff->data_offset * SECTOR_SIZE;
	TRACEx1(5, "data offset = %d sectors", buff->data_offset);
	if (retry) {
		buff->soft_error_map &= offset_mask;	/* keep skipped part */
	} else {
		buff->hard_error_map = buff->soft_error_map = 0;
	}
	buff->bad_sector_map = get_bad_sector_entry(buff->segment_id);
	if (buff->bad_sector_map != 0) {
		TRACEx2(4, "segment: %d, bad sector map: %08lx",
			buff->segment_id, buff->bad_sector_map);
	} else {
		TRACEx1(5, "segment: %d", buff->segment_id);
	}
	if (buff->sector_offset > 0) {
		buff->bad_sector_map >>= buff->sector_offset;
	}
	if (buff->sector_offset != 0 || buff->remaining != SECTORS_PER_SEGMENT) {
		TRACEx2(5, "sector offset = %d, count = %d",
			buff->sector_offset, buff->remaining);
	}
	/*
	 *    Segments with 3 or less sectors are not written with
	 *    valid data because there is no space left for the ecc.
	 *    The data written is whatever happens to be in the buffer.
	 *    Reading such a segment will return a zero byte-count.
	 *    To allow us to read/write segments with all bad sectors
	 *    we fake one readable sector in the segment. This prevents
	 *    having to handle these segments in a very special way.
	 *    It is not important if the reading of this bad sector
	 *    fails or not (the data is ignored). It is only read to
	 *    keep the driver running.
	 *    The QIC-40/80 spec. has no information on how to handle
	 *    this case, so this is my interpretation.
	 */
	if (buff->bad_sector_map == EMPTY_SEGMENT) {
		TRACE(5, "empty segment, fake first sector good");
		buff->bad_sector_map = FAKE_SEGMENT;
	}
	fdc_setup_error = 0;
	buff->next_segment = segment_id + 1;
	TRACE_EXIT;
	return 0;
}

/*      Calculate Floppy Disk Controller and DMA parameters for a new segment.
 */
int setup_new_segment(buffer_struct * buff, unsigned int segment_id, int skip)
{
	TRACE_FUN(5, "setup_new_segment");
	int result = 0;
	static int old_segment_id = -1;
	static int old_ftape_state = idle;
	int retry = 0;
	unsigned offset = 0;
	int count = SECTORS_PER_SEGMENT;

	TRACEx3(5, "%s segment %d (old = %d)",
		(ftape_state == reading) ? "reading" : "writing",
		segment_id, old_segment_id);
	if (ftape_state != old_ftape_state) {	/* when verifying */
		old_segment_id = -1;
		old_ftape_state = ftape_state;
	}
	if (segment_id == old_segment_id) {
		++buff->retry;
		++history.retries;
		TRACEx1(5, "setting up for retry nr %d", buff->retry);
		retry = 1;
		if (skip && buff->skip > 0) {	/* allow skip on retry */
			offset = buff->skip;
			count -= offset;
			TRACEx1(5, "skipping %d sectors", offset);
		}
	} else {
		buff->retry = 0;
		buff->skip = 0;
		old_segment_id = segment_id;
	}
	result = setup_segment(buff, segment_id, offset, count, retry);
	TRACE_EXIT;
	return result;
}

/*      Determine size of next cluster of good sectors.
 */
int calc_next_cluster(buffer_struct * buff)
{
	/* Skip bad sectors.
	 */
	while (buff->remaining > 0 && (buff->bad_sector_map & 1) != 0) {
		buff->bad_sector_map >>= 1;
		++buff->sector_offset;
		--buff->remaining;
	}
	/* Find next cluster of good sectors
	 */
	if (buff->bad_sector_map == 0) {	/* speed up */
		buff->sector_count = buff->remaining;
	} else {
		unsigned long map = buff->bad_sector_map;

		buff->sector_count = 0;
		while (buff->sector_count < buff->remaining && (map & 1) == 0) {
			++buff->sector_count;
			map >>= 1;
		}
	}
	return buff->sector_count;
}

int check_bot_eot(int status)
{
	TRACE_FUN(5, "check_bot_eot");

	if (status & (QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT)) {
		location.bot = ((location.track & 1) == 0 ?
				(status & QIC_STATUS_AT_BOT) :
				(status & QIC_STATUS_AT_EOT));
		location.eot = !location.bot;
		location.segment = (location.track +
			(location.bot ? 0 : 1)) * segments_per_track - 1;
		location.sector = -1;
		location.known = 1;
		TRACEx1(5, "tape at logical %s", location.bot ? "bot" : "eot");
		TRACEx1(5, "segment = %d", location.segment);
	} else {
		location.known = 0;
	}
	TRACE_EXIT;
	return location.known;
}

/*      Read Id of first sector passing tape head.
 */
int ftape_read_id(void)
{
	TRACE_FUN(8, "ftape_read_id");
	int result;
	int status;
	byte out[2];

	/* Assume tape is running on entry, be able to handle
	 * situation where it stopped or is stopping.
	 */
	location.known = 0;	/* default is location not known */
	out[0] = FDC_READID;
	out[1] = FTAPE_UNIT;
	result = fdc_command(out, 2);
	if (result < 0) {
		TRACE(1, "fdc_command failed");
	} else {
		result = fdc_interrupt_wait(20 * SECOND);
		if (result == 0) {
			if (fdc_sect == 0) {
				result = ftape_report_drive_status(&status);
				if (result == 0) {
					if (status & QIC_STATUS_READY) {
						tape_running = 0;
						TRACE(5, "tape has stopped");
						check_bot_eot(status);
						if (!location.known) {
							result = -EIO;
						}
					} else {
						/*  If read-id failed because of a hard or soft
						 *  error, return an error. Higher level must retry!
						 */
						result = -EIO;
					}
				}
			} else {
				location.known = 1;
				location.segment = (segments_per_head * fdc_head
					+ segments_per_cylinder * fdc_cyl
				 + (fdc_sect - 1) / SECTORS_PER_SEGMENT);
				location.sector = (fdc_sect - 1) % SECTORS_PER_SEGMENT;
				location.eot =
				    location.bot = 0;
			}
		} else if (result == -ETIME) {
			/*  Didn't find id on tape, must be near end: Wait until stopped.
			 */
			result = ftape_ready_wait(FOREVER, &status);
			if (result >= 0) {
				tape_running = 0;
				TRACE(5, "tape has stopped");
				check_bot_eot(status);
				if (!location.known) {
					result = -EIO;
				}
			}
		} else {
			/* Interrupted or otherwise failing fdc_interrupt_wait()
			 */
			TRACE(1, "fdc_interrupt_wait failed :(");
			result = -EIO;
		}
	}
	if (!location.known) {
		TRACE(5, "no id found");
	} else {
		if (location.sector == 0) {
			TRACEx2(5, "passing segment %d/%d", location.segment, location.sector);
		} else {
			TRACEx2(6, "passing segment %d/%d", location.segment, location.sector);
		}
	}
	TRACE_EXIT;
	return result;
}

static int logical_forward(void)
{
	tape_running = 1;
	return ftape_command(QIC_LOGICAL_FORWARD);
}

static int stop_tape(int *pstatus)
{
	TRACE_FUN(5, "stop_tape");
	int retry = 0;
	int result;

	do {
		result = ftape_command_wait(QIC_STOP_TAPE, timeout.stop, pstatus);
		if (result == 0) {
			if ((*pstatus & QIC_STATUS_READY) == 0) {
				result = -EIO;
			} else {
				tape_running = 0;
			}
		}
	} while (result < 0 && ++retry <= 3);
	if (result < 0) {
		TRACE(1, "failed ! (fatal)");
	}
	TRACE_EXIT;
	return result;
}

int ftape_dumb_stop(void)
{
	TRACE_FUN(5, "ftape_dumb_stop");
	int result;
	int status;

	/* Abort current fdc operation if it's busy (probably read
	 * or write operation pending) with a reset.
	 */
	result = fdc_ready_wait(100 /* usec */ );
	if (result < 0) {
		TRACE(1, "aborting fdc operation");
		fdc_reset();
	}
	/*  Reading id's after the last segment on a track may fail
	 *  but eventually the drive will become ready (logical eot).
	 */
	result = ftape_report_drive_status(&status);
	location.known = 0;
	do {
		if (result == 0 && status & QIC_STATUS_READY) {
			/* Tape is not running any more.
			 */
			TRACE(5, "tape already halted");
			check_bot_eot(status);
			tape_running = 0;
		} else if (tape_running) {
			/* Tape is (was) still moving.
			 */
#ifdef TESTING
			ftape_read_id();
#endif
			result = stop_tape(&status);
		} else {
			/* Tape not yet ready but stopped.
			 */
			result = ftape_ready_wait(timeout.pause, &status);
		}
	} while (tape_running);
#ifndef TESTING
	location.known = 0;
#endif
	TRACE_EXIT;
	return result;
}

/*      Wait until runner has finished tail buffer.
 */
int wait_segment(buffer_state_enum state)
{
	TRACE_FUN(5, "wait_segment");
	int result = 0;

	while (buffer[tail].status == state) {
		/*  First buffer still being worked on, wait up to timeout.
		 */
		result = fdc_interrupt_wait(50 * SECOND);
		if (result < 0) {
			if (result != -EINTR) {
				TRACE(1, "fdc_interrupt_wait failed");
				result = -ETIME;
			}
			break;
		}
		if (fdc_setup_error) {
			TRACE(1, "setup error");
			/* recover... */
			result = -EIO;
			break;
		}
	}
	TRACE_EXIT;
	return result;
}

/* forward */ static int seek_forward(int segment_id);

int fast_seek(int count, int reverse)
{
	TRACE_FUN(5, "fast_seek");
	int result = 0;
	int status;

	if (count > 0) {
		/*  If positioned at begin or end of tape, fast seeking needs
		 *  special treatment.
		 *  Starting from logical bot needs a (slow) seek to the first
		 *  segment before the high speed seek. Most drives do this
		 *  automatically but some older don't, so we treat them
		 *  all the same.
		 *  Starting from logical eot is even more difficult because
		 *  we cannot (slow) reverse seek to the last segment.
		 *  TO BE IMPLEMENTED.
		 */
		inhibit_correction = 0;
		if (location.known &&
		    ((location.bot && !reverse) ||
		     (location.eot && reverse))) {
			if (!reverse) {
				/*  (slow) skip to first segment on a track
				 */
				seek_forward(location.track * segments_per_track);
				--count;
			} else {
				/*  When seeking backwards from end-of-tape the number
				 *  of erased gaps found seems to be higher than expected.
				 *  Therefor the drive must skip some more segments than
				 *  calculated, but we don't know how many.
				 *  Thus we will prevent the re-calculation of offset
				 *  and overshoot when seeking backwards.
				 */
				inhibit_correction = 1;
				count += 3;	/* best guess */
			}
		}
	} else {
		TRACEx1(5, "warning: zero or negative count: %d", count);
	}
	if (count > 0) {
		int i;
		int nibbles = count > 255 ? 3 : 2;

		if (count > 4095) {
			TRACE(4, "skipping clipped at 4095 segment");
			count = 4095;
		}
		/* Issue this tape command first. */
		if (!reverse) {
			TRACEx1(4, "skipping %d segment(s)", count);
			result = ftape_command(nibbles == 3 ?
			   QIC_SKIP_EXTENDED_FORWARD : QIC_SKIP_FORWARD);
		} else {
			TRACEx1(4, "backing up %d segment(s)", count);
			result = ftape_command(nibbles == 3 ?
			   QIC_SKIP_EXTENDED_REVERSE : QIC_SKIP_REVERSE);
		}
		if (result < 0) {
			TRACE(4, "Skip command failed");
		} else {
			--count;	/* 0 means one gap etc. */
			for (i = 0; i < nibbles; ++i) {
				if (result >= 0) {
					result = ftape_parameter(count & 15);
					count /= 16;
				}
			}
			result = ftape_ready_wait(timeout.rewind, &status);
			if (result >= 0) {
				tape_running = 0;
			}
		}
	}
	TRACE_EXIT;
	return result;
}

static int validate(int id)
{
	/*  Check to see if position found is off-track as reported once.
	 *  Because all tracks in one direction lie next to each other,
	 *  if off-track the error will be approximately 2 * segments_per_track.
	 */
	if (location.track == -1) {
		return 1;	/* unforeseen situation, don't generate error */
	} else {
		/*  Use margin of segments_per_track on both sides because ftape
		 *  needs some margin and the error we're looking for is much larger !
		 */
		int lo = (location.track - 1) * segments_per_track;
		int hi = (location.track + 2) * segments_per_track;

		return (id >= lo && id < hi);
	}
}

static int seek_forward(int segment_id)
{
	TRACE_FUN(5, "seek_forward");
	int failures = 0;
	int result = 0;
	int count;
	static int margin = 1;	/* fixed: stop this before target */
	static int overshoot = 1;
	static int min_count = 8;
	int expected = -1;
	int target = segment_id - margin;
	int fast_seeking;

	if (!location.known) {
		TRACE(1, "fatal: cannot seek from unknown location");
		result = -EIO;
	} else if (!validate(segment_id)) {
		TRACE(1, "fatal: head off track (bad hardware?)");
		ftape_sleep(1 * SECOND);
		ftape_failure = 1;
		result = -EIO;
	} else {
		int prev_segment = location.segment;

		TRACEx4(4, "from %d/%d to %d/0 - %d", location.segment,
			location.sector, segment_id, margin);
		count = target - location.segment - overshoot;
		fast_seeking = (count > min_count + (location.bot ? 1 : 0));
		if (fast_seeking) {
			TRACEx1(5, "fast skipping %d segments", count);
			expected = segment_id - margin;
			fast_seek(count, 0);
		}
		if (!tape_running) {
			logical_forward();
		}
		while (location.segment < segment_id) {
			/*  This requires at least one sector in a (bad) segment to
			 *  have a valid and readable sector id !
			 *  It looks like this is not guaranteed, so we must try
			 *  to find a way to skip an EMPTY_SEGMENT. !!! FIXME !!!
			 */
			if (ftape_read_id() < 0 || !location.known) {
				location.known = 0;
				if (!tape_running || ++failures > SECTORS_PER_SEGMENT ||
				    (current->signal & _DONT_BLOCK)) {
					TRACE(1, "read_id failed completely");
					result = -EIO;
					break;
				} else {
					TRACEx1(5, "read_id failed, retry (%d)", failures);
				}
			} else if (fast_seeking) {
				TRACEx4(4, "ended at %d/%d (%d,%d)", location.segment,
					location.sector, overshoot, inhibit_correction);
				if (!inhibit_correction &&
				    (location.segment < expected ||
				 location.segment > expected + margin)) {
					int error = location.segment - expected;
					TRACEx2(4, "adjusting overshoot from %d to %d",
					   overshoot, overshoot + error);
					overshoot += error;
					/*  All overshoots have the same direction, so it should
					 *  never become negative, but who knows.
					 */
					if (overshoot < -5 || overshoot > 10) {
						if (overshoot < 0) {
							overshoot = -5;		/* keep sane value */
						} else {
							overshoot = 10;		/* keep sane value */
						}
						TRACEx1(4, "clipped overshoot to %d", overshoot);
					}
				}
				fast_seeking = 0;
			}
			if (location.known) {
				if (location.segment > prev_segment + 1) {
					TRACEx1(4, "missed segment %d while skipping", prev_segment + 1);
				}
				prev_segment = location.segment;
			}
		}
		if (location.segment > segment_id) {
			TRACEx2(4, "failed: skip ended at segment %d/%d",
				location.segment, location.sector);
			result = -EIO;
		}
	}
	TRACE_EXIT;
	return result;
}

static int skip_reverse(int segment_id, int *pstatus)
{
	TRACE_FUN(5, "skip_reverse");
	int result = 0;
	int failures = 0;
	static int overshoot = 1;
	static int min_rewind = 2;	/* 1 + overshoot */
	static const int margin = 1;	/* stop this before target */
	int expected = 0;
	int count;
	int short_seek;
	int target = segment_id - margin;

	if (location.known && !validate(segment_id)) {
		TRACE(1, "fatal: head off track (bad hardware?)");
		ftape_sleep(1 * SECOND);
		ftape_failure = 1;
		result = -EIO;
	} else
		do {
			if (!location.known) {
				TRACE(-1, "warning: location not known");
			}
			TRACEx4(4, "from %d/%d to %d/0 - %d",
				location.segment, location.sector, segment_id, margin);
			/*  min_rewind == 1 + overshoot_when_doing_minimum_rewind
			 *  overshoot  == overshoot_when_doing_larger_rewind
			 *  Initially min_rewind == 1 + overshoot, optimization
			 *  of both values will be done separately.
			 *  overshoot and min_rewind can be negative as both are
			 *  sums of three components:
			 *  any_overshoot == rewind_overshoot - stop_overshoot - start_overshoot
			 */
			if (location.segment - target - (min_rewind - 1) < 1) {
				short_seek = 1;
			} else {
				count = location.segment - target - overshoot;
				short_seek = (count < 1);
			}
			if (short_seek) {
				count = 1;	/* do shortest rewind */
				expected = location.segment - min_rewind;
				if (expected / segments_per_track != location.track) {
					expected = location.track * segments_per_track;
				}
			} else {
				expected = target;
			}
			fast_seek(count, 1);
			logical_forward();
			result = ftape_read_id();
			if (result == 0 && location.known) {
				TRACEx5(4, "ended at %d/%d (%d,%d,%d)", location.segment,
					location.sector, min_rewind, overshoot, inhibit_correction);
				if (!inhibit_correction &&
				    (location.segment < expected ||
				 location.segment > expected + margin)) {
					int error = expected - location.segment;
					if (short_seek) {
						TRACEx2(4, "adjusting min_rewind from %d to %d",
							min_rewind, min_rewind + error);
						min_rewind += error;
						if (min_rewind < -5) {	/* is this right ? FIXME ! */
							min_rewind = -5;	/* keep sane value */
							TRACEx1(4, "clipped min_rewind to %d", min_rewind);
						}
					} else {
						TRACEx2(4, "adjusting overshoot from %d to %d",
							overshoot, overshoot + error);
						overshoot += error;
						if (overshoot < -5 || overshoot > 10) {
							if (overshoot < 0) {
								overshoot = -5;		/* keep sane value */
							} else {
								overshoot = 10;		/* keep sane value */
							}
							TRACEx1(4, "clipped overshoot to %d", overshoot);
						}
					}
				}
			} else {
				if ((!tape_running && !location.known) ||
				    ++failures > SECTORS_PER_SEGMENT) {
					TRACE(1, "read_id failed completely");
					result = -EIO;
					break;
				} else {
					TRACEx1(5, "ftape_read_id failed, retry (%d)", failures);
				}
				result = ftape_report_drive_status(pstatus);
				if (result < 0) {
					TRACEi(1, "ftape_report_drive_status failed with code", result);
					break;
				}
			}
		} while (location.segment > segment_id &&
			 (current->signal & _DONT_BLOCK) == 0);
	if (location.known) {
		TRACEx2(4, "current location: %d/%d", location.segment, location.sector);
	}
	TRACE_EXIT;
	return result;
}

static int determine_position(void)
{
	TRACE_FUN(5, "determine_position");
	int retry = 0;
	int fatal = 0;
	int status;
	int result;

	if (!tape_running) {
		/*  This should only happen if tape is stopped by isr.
		 */
		TRACE(5, "waiting for tape stop");
		result = ftape_ready_wait(timeout.pause, &status);
		if (result < 0) {
			TRACE(5, "drive still running (fatal)");
			tape_running = 1;	/* ? */
		}
	} else {
		ftape_report_drive_status(&status);
	}
	if (status & QIC_STATUS_READY) {
		/*  Drive must be ready to check error state !
		 */
		TRACE(5, "drive is ready");
		if (status & QIC_STATUS_ERROR) {
			int error;
			int command;

			/*  Report and clear error state, try to continue.
			 */
			TRACE(5, "error status set");
			ftape_report_error(&error, &command, 1);
			ftape_ready_wait(timeout.reset, &status);
			tape_running = 0;	/* ? */
		}
		if (check_bot_eot(status)) {
			if (location.bot) {
				if ((status & QIC_STATUS_READY) == 0) {
					/* tape moving away from bot/eot, let's see if we
					 * can catch up with the first segment on this track.
					 */
				} else {
					TRACE(5, "start tape from logical bot");
					logical_forward();	/* start moving */
				}
			} else {
				if ((status & QIC_STATUS_READY) == 0) {
					TRACE(4, "waiting for logical end of track");
					result = ftape_ready_wait(timeout.reset, &status);
					/* error handling needed ? */
				} else {
					TRACE(4, "tape at logical end of track");
				}
			}
		} else {
			TRACE(5, "start tape");
			logical_forward();	/* start moving */
			location.known = 0;	/* not cleared by logical forward ! */
		}
	}
	if (!location.known) {
		/* tape should be moving now, start reading id's
		 */
		TRACE(5, "location unknown");
		do {
			result = ftape_read_id();
			if (result < 0) {
				/*  read-id somehow failed, tape may have reached end
				 *  or some other error happened.
				 */
				TRACE(5, "read-id failed");
				ftape_report_drive_status(&status);
				if (status & QIC_STATUS_READY) {
					tape_running = 0;
					TRACEx1(4, "tape stopped for unknown reason ! status = 0x%02x",
						status);
					if (status & QIC_STATUS_ERROR) {
						fatal = 1;
					} else {
						if (check_bot_eot(status)) {
							result = 0;
						} else {
							fatal = 1;	/* oops, tape stopped but not at end ! */
						}
					}
				}
				result = -EIO;
			}
		} while (result < 0 && !fatal && ++retry < SECTORS_PER_SEGMENT);
	} else {
		result = 0;
	}
	TRACEx1(5, "tape is positioned at segment %d", location.segment);
	TRACE_EXIT;
	return result;
}

/*      Get the tape running and position it just before the
 *      requested segment.
 *      Seek tape-track and reposition as needed.
 */
int ftape_start_tape(int segment_id, int sector_offset)
{
	TRACE_FUN(5, "ftape_start_tape");
	int track = segment_id / segments_per_track;
	int result = -EIO;
	int status;
	static int last_segment = -1;
	static int bad_bus_timing = 0;
	/* number of segments passing the head between starting the tape
	 * and being able to access the first sector.
	 */
	static int start_offset = 1;
	int retry = 0;

	/* If sector_offset > 0, seek into wanted segment instead of
	 * into previous.
	 * This allows error recovery if a part of the segment is bad
	 * (erased) causing the tape drive to generate an index pulse
	 * thus causing a no-data error before the requested sector
	 * is reached.
	 */
	tape_running = 0;
	TRACEx3(4, "target segment: %d/%d%s", segment_id, sector_offset,
		buffer[head].retry > 0 ? " retry" : "");
	if (buffer[head].retry > 0) {	/* this is a retry */
		if (!bad_bus_timing && ftape_data_rate == 1 &&
		    history.overrun_errors - overrun_count_offset >= 8) {
			ftape_set_data_rate(ftape_data_rate + 1);
			bad_bus_timing = 1;
			TRACE(2, "reduced datarate because of excessive overrun errors");
		}
	}
	last_segment = segment_id;
	if (location.track != track || (might_be_off_track &&
					buffer[head].retry == 0)) {
		/* current track unknown or not equal to destination
		 */
		ftape_ready_wait(timeout.seek, &status);
		ftape_seek_head_to_track(track);
		overrun_count_offset = history.overrun_errors;
	}
	do {
		if (!location.known) {
			determine_position();
		}
		/*  Check if we are able to catch the requested segment in time.
		 */
		if (location.known && location.segment >= segment_id -
		    ((tape_running || location.bot) ? 0 : start_offset)) {
			/*  Too far ahead (in or past target segment).
			 */
			if (tape_running) {
				result = stop_tape(&status);
				if (result < 0) {
					TRACEi(1, "stop tape failed with code", result);
					break;
				}
				TRACE(5, "tape stopped");
				tape_running = 0;
			}
			TRACE(5, "repositioning");
			++history.rewinds;
			if (segment_id % segments_per_track < start_offset) {
				/*  If seeking to first segments on track better do a complete
				 *  rewind to logical begin of track to get a more steady tape
				 *  motion.
				 */
				result = ftape_command_wait((location.track & 1) ?
						   QIC_PHYSICAL_FORWARD :
						    QIC_PHYSICAL_REVERSE,
						timeout.rewind, &status);
				check_bot_eot(status);	/* update location */
			} else {
				result = skip_reverse(segment_id - start_offset, &status);
			}
		}
		if (!location.known) {
			TRACE(-1, "panic: location not known");
			result = -EIO;
			if ((current->signal & _DONT_BLOCK) || ftape_failure) {
				break;
			} else {
				continue;
			}
		}
		TRACEx2(4, "current segment: %d/%d", location.segment, location.sector);
		/*  We're on the right track somewhere before the wanted segment.
		 *  Start tape movement if needed and skip to just before or inside
		 *  the requested segment. Keep tape running.
		 */
		result = 0;
		if (location.segment < segment_id -
		    ((tape_running || location.bot) ? 0 : start_offset)) {
			if (sector_offset > 0) {
				result = seek_forward(segment_id);
			} else {
				result = seek_forward(segment_id - 1);
			}
		}
		if (result == 0 &&
		    location.segment != segment_id - (sector_offset > 0 ? 0 : 1)) {
			result = -EIO;
		}
	} while (result < 0 && !ftape_failure &&
		 (current->signal & _DONT_BLOCK) == 0 &&
		 ++retry <= 5);
	if (result < 0) {
		TRACE(1, "failed to reposition");
	}
	TRACE_EXIT;
	return result;
}
