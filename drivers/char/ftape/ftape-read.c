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

 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-read.c,v $
 $Author: bas $
 *
 $Revision: 1.30 $
 $Date: 1995/05/27 08:54:21 $
 $State: Beta $
 *
 *      This file contains the reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/ftape.h>
#include <asm/segment.h>

#include "tracing.h"
#include "ftape-read.h"
#include "qic117.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-write.h"
#include "ftape-eof.h"
#include "ecc.h"
#include "ftape-bsm.h"

/*      Global vars.
 */

/*      Local vars.
 */
int buf_pos_rd = 0;
int buf_len_rd = 0;

void ftape_zap_read_buffers(void)
{
	int i;

	for (i = 0; i < NR_BUFFERS; ++i) {
		/*
		 * changed to "fit" with dynamic allocation of tape_buffer. --khp
		 */
		buffer[i].address = tape_buffer[i];
		buffer[i].status = waiting;
		buffer[i].bytes = 0;
		buffer[i].skip = 0;
		buffer[i].retry = 0;
	}
	buf_len_rd = 0;
	buf_pos_rd = 0;
	eof_mark = 0;
	ftape_state = idle;
}

static unsigned long convert_sector_map(buffer_struct * buff)
{
	TRACE_FUN(8, "convert_sector_map");
	int i = 0;
	unsigned long bad_map = get_bad_sector_entry(buff->segment_id);
	unsigned long src_map = buff->soft_error_map | buff->hard_error_map;
	unsigned long dst_map = 0;

	if (bad_map || src_map) {
		TRACEx1(5, "bad_map = 0x%08lx", bad_map);
		TRACEx1(5, "src_map = 0x%08lx", src_map);
	}
	while (bad_map) {
		while ((bad_map & 1) == 0) {
			if (src_map & 1) {
				dst_map |= (1 << i);
			}
			src_map >>= 1;
			bad_map >>= 1;
			++i;
		}
		/* (bad_map & 1) == 1 */
		src_map >>= 1;
		bad_map >>= 1;
	}
	if (src_map) {
		dst_map |= (src_map << i);
	}
	if (dst_map) {
		TRACEx1(5, "dst_map = 0x%08lx", dst_map);
	}
	TRACE_EXIT;
	return dst_map;
}

int correct_and_copy(unsigned int tail, byte * destination)
{
	TRACE_FUN(8, "correct_and_copy");
	struct memory_segment mseg;
	int result;
	BAD_SECTOR read_bad;

	mseg.read_bad = convert_sector_map(&buffer[tail]);
	mseg.marked_bad = 0;	/* not used... */
	mseg.blocks = buffer[tail].bytes / SECTOR_SIZE;
	mseg.data = buffer[tail].address;
	/*    If there are no data sectors we can skip this segment.
	 */
	if (mseg.blocks <= 3) {
		TRACE(4, "empty segment");
		TRACE_EXIT;
		return 0;
	}
	read_bad = mseg.read_bad;
	history.crc_errors += count_ones(read_bad);
	result = ecc_correct_data(&mseg);
	if (read_bad != 0 || mseg.corrected != 0) {
		TRACElx(4, "crc error map:", read_bad);
		TRACElx(4, "corrected map:", mseg.corrected);
		history.corrected += count_ones(mseg.corrected);
	}
	if (result == ECC_CORRECTED || result == ECC_OK) {
		if (result == ECC_CORRECTED) {
			TRACEi(3, "ecc corrected segment:", buffer[tail].segment_id);
		}
		memcpy(destination, mseg.data, (mseg.blocks - 3) * SECTOR_SIZE);
		if ((read_bad ^ mseg.corrected) & mseg.corrected) {
			/* sectors corrected without crc errors set */
			history.crc_failures++;
		}
		TRACE_EXIT;
		return (mseg.blocks - 3) * SECTOR_SIZE;
	} else {
		TRACEi(1, "ecc failure on segment", buffer[tail].segment_id);
		history.ecc_failures++;
		TRACE_EXIT;
		return -EAGAIN;	/* should retry */
	}
	TRACE_EXIT;
	return 0;
}

/*      Read given segment into buffer at address.
 */
int read_segment(unsigned segment_id, byte * address, int *eof_mark,
		 int read_ahead)
{
	TRACE_FUN(5, "read_segment");
	int read_done = 0;
	int result = 0;
	int bytes_read = 0;
	int retry = 0;

	TRACEi(5, "segment_id =", segment_id);
	if (ftape_state != reading) {
		if (ftape_state == writing) {
			ftape_flush_buffers();	/* flush write buffer */
			TRACE(5, "calling ftape_abort_operation");
			result = ftape_abort_operation();
			if (result < 0) {
				TRACE(1, "ftape_abort_operation failed");
				TRACE_EXIT;
				return -EIO;
			}
		} else {
			/* clear remaining read buffers */
			ftape_zap_read_buffers();
		}
		ftape_state = reading;
	}
	if (segment_id >= segments_per_track * tracks_per_tape) {
		TRACE(5, "reading past end of tape");
		TRACE_EXIT;
		return -ENOSPC;
	}
	for (;;) {
		/*    Search all full buffers for the first matching the wanted segment.
		 *    Clear other buffers on the fly.
		 */
		while (!read_done && buffer[tail].status == done) {
			if (buffer[tail].segment_id == segment_id) {
				unsigned eof_sector;
				unsigned sector_count = 0;
				unsigned long bsm = get_bad_sector_entry(segment_id);
				int i;

				/*        If out buffer is already full, return its contents.
				 */
				if (buffer[tail].deleted) {
					TRACEi(5, "found segment in cache :", segment_id);
					TRACE_EXIT;
					/*  Return a value that read_header_segment understands.
					 *  As this should only occur when searching for the header
					 *  segments it shouldn't be misinterpreted elsewhere.
					 */
					return 0;
				}
				TRACEi(5, "found segment in cache :", segment_id);
				eof_sector = check_for_eof(segment_id);
				if (eof_sector > 0) {
					TRACEi(5, "end of file mark in sector:", eof_sector);
					for (i = 1; i < eof_sector; ++i) {
						if ((bsm & 1) == 0) {
							++sector_count;
						}
						bsm >>= 1;
					}
					*eof_mark = 1;
				}
				if (eof_sector != 1) {	/* not found or gt 1 */
					result = correct_and_copy(tail, address);
					TRACEi(5, "segment contains (bytes) :", result);
					if (result < 0) {
						if (result != -EAGAIN) {
							TRACE_EXIT;
							return result;
						}
						/* keep read_done == 0, will trigger ftape_abort_operation
						 * because reading wrong segment.
						 */
						TRACE(1, "ecc failed, retry");
						++retry;
					} else {
						read_done = 1;
					}
				} else {
					read_done = 1;
				}
				if (eof_sector > 0) {
					bytes_read = sector_count * SECTOR_SIZE;
					TRACEi(5, "partial read count:", bytes_read);
				} else {
					bytes_read = result;
				}
			} else {
				TRACEi(5, "zapping segment in cache :", buffer[tail].segment_id);
			}
			buffer[tail].status = waiting;
			next_buffer(&tail);
		}
		if (!read_done && buffer[tail].status == reading) {
			if (buffer[tail].segment_id == segment_id) {
				int result = wait_segment(reading);
				if (result < 0) {
					if (result == -EINTR) {
						TRACE_EXIT;
						return result;
					}
					TRACE(1, "wait_segment failed while reading");
					ftape_abort_operation();
				}
			} else {
				/*        We're reading the wrong segment, stop runner.
				 */
				ftape_abort_operation();
			}
		}
		/*    if just passed the last segment on a track, wait for BOT or EOT mark.
		 */
		if (runner_status == logical_eot) {
			int status;
			result = ftape_ready_wait(timeout.seek, &status);
			if (result < 0) {
				TRACE(1, "ftape_ready_wait waiting for eot/bot failed");
			}
			if ((status & (QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT)) == 0) {
				TRACE(1, "eot/bot not reached");
			}
			runner_status = end_of_tape;
		}
		/*    should runner stop ?
		 */
		if (runner_status == aborting || runner_status == buffer_overrun ||
		    runner_status == end_of_tape) {
			if (runner_status != end_of_tape &&
			 !(runner_status == aborting && !tape_running)) {
				ftape_dumb_stop();
			}
			if (runner_status == aborting) {
				if (buffer[head].status == reading || buffer[head].status == error) {
					if (buffer[head].status == error) {
						history.defects += count_ones(buffer[head].hard_error_map);
					}
					buffer[head].status = waiting;
				}
			}
			runner_status = idle;	/* aborted ? */
		}
		/*    If segment to read is empty, do not start runner for it,
		 *    but wait for next read call.
		 */
		if (get_bad_sector_entry(segment_id) == EMPTY_SEGMENT) {
			bytes_read = 0;		/* flag empty segment */
			read_done = 1;
		}
		/*  Allow escape from this loop on signal !
		 */
		if (current->signal & _DONT_BLOCK) {
			TRACE(2, "interrupted by non-blockable signal");
			TRACE_EXIT;
			return -EINTR;
		}
		/*    If we got a segment: quit, or else retry up to limit.
		 */
		if (read_done) {
			break;
		}
		if (retry > RETRIES_ON_ECC_ERROR) {
			history.defects++;
			TRACE(1, "too many retries on ecc failure");
			TRACE_EXIT;
			return -ENODATA;
		}
		/*    Now at least one buffer is empty !
		 *    Restart runner & tape if needed.
		 */
		TRACEx3(8, "head: %d, tail: %d, runner_status: %d",
			head, tail, runner_status);
		TRACEx2(8, "buffer[].status, [head]: %d, [tail]: %d",
			buffer[head].status, buffer[tail].status);
		if (buffer[tail].status == waiting) {
			setup_new_segment(&buffer[head], segment_id, -1);
			if (!read_ahead) {
				buffer[head].next_segment = 0;	/* disable read-ahead */
			}
			calc_next_cluster(&buffer[head]);
			if (runner_status == idle) {
				result = ftape_start_tape(segment_id,
					     buffer[head].sector_offset);
				if (result < 0) {
					TRACEx1(1, "Error: segment %d unreachable", segment_id);
					TRACE_EXIT;
					return result;
				}
				runner_status = running;
			}
			buffer[head].status = reading;
			setup_fdc_and_dma(&buffer[head], FDC_READ);
		}
	}
	if (read_done) {
		TRACE_EXIT;
		return bytes_read;
	} else {
		TRACE(1, "too many retries");
		TRACE_EXIT;
		return -EIO;
	}
}

int read_header_segment(byte * address)
{
	TRACE_FUN(5, "read_header_segment");
	int i;
	int result;
	int header_segment = -1;
	unsigned int max_floppy_side;
	unsigned int max_floppy_track;
	unsigned int max_floppy_sector;
	int first_failed = 0;
	int status;
	int new_tape_len;

	result = ftape_report_drive_status(&status);
	if (result < 0) {
		TRACE(1, "error: error_status or report failure");
		TRACE_EXIT;
		return -EIO;
	}
	TRACE(5, "reading...");
	ftape_last_segment.id = 68;	/* will allow us to read the header ! */
	/*  We're looking for the first header segment.
	 *  A header segment cannot contain bad sectors, therefor at the
	 *  tape start, segments with bad sectors are (according to QIC-40/80)
	 *  written with deleted data marks and must be skipped.
	 */
	used_header_segment = -1;
	result = 0;
	for (header_segment = 0;
	     header_segment < ftape_last_segment.id && result == 0;
	     ++header_segment) {
		/*  Set no read-ahead, the isr will force read-ahead whenever
		 *  it encounters deleted data !
		 */
		result = read_segment(header_segment, address, &status, 0);
		if (result < 0 && !first_failed) {
			TRACE(1, "header segment damaged, trying backup");
			first_failed = 1;
			result = 0;	/* force read of next (backup) segment */
		}
	}
	if (result < 0 || header_segment >= ftape_last_segment.id) {
		TRACE(1, "no readable header segment found");
		TRACE_EXIT;
		return -EIO;
	}
	result = ftape_abort_operation();
	if (result < 0) {
		TRACE(1, "ftape_abort_operation failed");
		TRACE_EXIT;
		return -EIO;
	}
	if (GET4(address, 0) != 0xaa55aa55) {
		TRACE(1, "wrong signature in header segment");
		TRACE_EXIT;
		return -EIO;
	}
	header_segment_1 = GET2(address, 6);
	header_segment_2 = GET2(address, 8);
	TRACEx2(2, "header segments are %d and %d",
		header_segment_1, header_segment_2);
	used_header_segment = (first_failed) ? header_segment_2 : header_segment_1;

	/*    Verify tape parameters...
	 *    QIC-40/80 spec:                 tape_parameters:
	 *
	 *    segments-per-track              segments_per_track
	 *    tracks-per-cartridge            tracks_per_tape
	 *    max-floppy-side                 (segments_per_track *
	 *                                    tracks_per_tape - 1) /
	 *                                    segments_per_head
	 *    max-floppy-track                segments_per_head /
	 *                                    segments_per_cylinder - 1
	 *    max-floppy-sector               segments_per_cylinder *
	 *                                    SECTORS_PER_SEGMENT
	 */
	format_code = (format_type) * (address + 4);
	segments_per_track = GET2(address, 24);
	tracks_per_tape = *(address + 26);
	max_floppy_side = *(address + 27);
	max_floppy_track = *(address + 28);
	max_floppy_sector = *(address + 29);
	TRACEx6(4, "(fmt/spt/tpc/fhm/ftm/fsm) = %d/%d/%d/%d/%d/%d",
		format_code, segments_per_track, tracks_per_tape,
		max_floppy_side, max_floppy_track, max_floppy_sector);
	new_tape_len = tape_len;
	switch (format_code) {
	case fmt_425ft:
		new_tape_len = 425;
		break;
	case fmt_normal:
		if (tape_len == 0) {	/* otherwise 307 ft */
			new_tape_len = 205;
		}
		break;
	case fmt_1100ft:
		new_tape_len = 1100;
		break;
	case fmt_wide:{
			int segments_per_1000_inch = 1;		/* non-zero default for switch */
			switch (qic_std) {
			case QIC_TAPE_QIC40:
				segments_per_1000_inch = 332;
				break;
			case QIC_TAPE_QIC80:
				segments_per_1000_inch = 488;
				break;
			case QIC_TAPE_QIC3010:
				segments_per_1000_inch = 730;
				break;
			case QIC_TAPE_QIC3020:
				segments_per_1000_inch = 1430;
				break;
			}
			new_tape_len = (1000 * segments_per_track +
					(segments_per_1000_inch - 1)) / segments_per_1000_inch;
			break;
		}
	default:
		TRACE(1, "unknown tape format, please report !");
		TRACE_EXIT;
		return -EIO;
	}
	if (new_tape_len != tape_len) {
		tape_len = new_tape_len;
		TRACEx1(1, "calculated tape length is %d ft", tape_len);
		ftape_calc_timeouts();
	}
	if (segments_per_track == 0 && tracks_per_tape == 0 &&
	    max_floppy_side == 0 && max_floppy_track == 0 &&
	    max_floppy_sector == 0) {
		/*  QIC-40 Rev E and earlier has no values in the header.
		 */
		segments_per_track = 68;
		tracks_per_tape = 20;
		max_floppy_side = 1;
		max_floppy_track = 169;
		max_floppy_sector = 128;
	}
	/*  This test will compensate for the wrong parameter on tapes
	 *  formatted by Conner software.
	 */
	if (segments_per_track == 150 &&
	    tracks_per_tape == 28 &&
	    max_floppy_side == 7 &&
	    max_floppy_track == 149 &&
	    max_floppy_sector == 128) {
		TRACE(-1, "the famous CONNER bug: max_floppy_side off by one !");
		max_floppy_side = 6;
	}
	/*  This test will compensate for the wrong parameter on tapes
	 *  formatted by Colorado Windows software.
	 */
	if (segments_per_track == 150 &&
	    tracks_per_tape == 28 &&
	    max_floppy_side == 6 &&
	    max_floppy_track == 150 &&
	    max_floppy_sector == 128) {
		TRACE(-1, "the famous Colorado bug: max_floppy_track off by one !");
		max_floppy_track = 149;
	}
	segments_per_head = ((max_floppy_sector / SECTORS_PER_SEGMENT) *
			     (max_floppy_track + 1));
	/*
	 *    Verify drive_configuration with tape parameters
	 */
	if (segments_per_head == 0 || segments_per_cylinder == 0 ||
	  ((segments_per_track * tracks_per_tape - 1) / segments_per_head
	   != max_floppy_side) ||
	    (segments_per_head / segments_per_cylinder - 1 != max_floppy_track) ||
	(segments_per_cylinder * SECTORS_PER_SEGMENT != max_floppy_sector)
#ifdef TESTING
	    || (format_code == 4 && (max_floppy_track != 254 || max_floppy_sector != 128))
#endif
	    ) {
		TRACE(1, "Tape parameters inconsistency, please report");
		TRACE_EXIT;
		return -EIO;
	}
	first_data_segment = GET2(address, 10);		/* first data segment */
	TRACEi(4, "first data segment:", first_data_segment);
	extract_bad_sector_map(address);
	/*  Find the highest segment id that allows still one full
	 *  deblock_buffer to be written to tape.
	 */
	ftape_last_segment.size = 0;
	for (i = segments_per_track * tracks_per_tape - 1; i >= 0; --i) {
		int space = SECTORS_PER_SEGMENT - 3 - count_ones(get_bad_sector_entry(i));
		if (space > 0) {
			ftape_last_segment.size += space;	/* sectors free */
			ftape_last_segment.free = (ftape_last_segment.size -
				   sizeof(deblock_buffer) / SECTOR_SIZE);
			if (ftape_last_segment.free >= 0) {
				ftape_last_segment.id = i;
				TRACEx2(4, "`last' segment is %d, %d Kb",
					ftape_last_segment.id, ftape_last_segment.size);
				break;
			}
		}
	}
	/* Copy the failed sector log into our local buffer.
	 */
	if (!ftape_validate_label(&deblock_buffer[30])) {
		TRACE(-1, "This tape has no `Linux raw format' label,\n"
		      "***** Use `mt' to erase this tape if you want to use file marks !");
	} else {
		extract_file_marks(address);
	}
	ftape_reset_position();
	TRACE_EXIT;
	return 0;
}

int _ftape_read(char *buff, int req_len)
{
	TRACE_FUN(5, "_ftape_read");
	int result = 0;
	int cnt;
	int to_do = req_len;
	static int remaining;
	int bytes_read = 0;

	if (ftape_offline || !formatted || no_tape) {
		TRACEx3(-1, "offline = %d, formatted = %d, no_tape = %d",
			ftape_offline, formatted, no_tape);
		result = -EIO;
	} else {
		history.used |= 1;
		if (first_data_segment == -1) {
			result = read_header_segment(deblock_buffer);
		}
	}
	if (result < 0) {
		TRACE_EXIT;
		return result;
	}
	/*  As GNU tar doesn't accept partial read counts when the multiple
	 *  volume flag is set, we make sure to return the requested amount
	 *  of data. Except, of course, at the end of the tape or file mark.
	 */
	while (to_do > 0) {	/* don't return with a partial count ! */
		/*  If we're reading the `last' segment(s) on tape, make sure we don't
		 *  get more than 29 Kb from it (As it only contains this much).
		 *  This works only for sequential access, so random access should
		 *  stay away from this `last' segment.
		 *  Note: ftape_seg_pos points to the next segment that will be
		 *        read, so it's one too high here!
		 */
		if (!eof_mark && ftape_seg_pos - 1 >= ftape_last_segment.id) {
			TRACEi(5, "remaining of last segment:", remaining);
			if (to_do > remaining) {
				to_do = remaining;	/* fake a smaller request */
				TRACE(5, "clipped request to remaining");
			}
		}
		while (!eof_mark && buf_len_rd == 0) {
			/*  When starting to read the `last' segment, set remaining
			 */
			if (ftape_seg_pos == ftape_last_segment.id) {
				remaining = sizeof(deblock_buffer);
				TRACEi(5, "remaining set to:", remaining);
			}
			result = read_segment(ftape_seg_pos, deblock_buffer, &eof_mark, 1);
			if (result < 0) {
				if (result == -ENODATA) {
					/*  Unable to recover tape data, return error and skip bad spot.
					 */
					++ftape_seg_pos;
				}
				TRACEx1(4, "read_segment result: %d", result);
				TRACE_EXIT;
				return result;
			}
			/*  Allow escape from this loop on signal !
			 */
			if (current->signal & _DONT_BLOCK) {
				TRACE(2, "interrupted by non-blockable signal");
				TRACE_EXIT;
				return -EINTR;
			}
			buf_pos_rd = 0;
			buf_len_rd = result;
			++ftape_seg_pos;
		}
		/*  Take as much as we can use
		 */
		cnt = (buf_len_rd < to_do) ? buf_len_rd : to_do;
		TRACEi(7, "nr bytes just read:", cnt);
		if (cnt > 0) {
			result = verify_area(VERIFY_WRITE, buff, cnt);
			if (result) {
				TRACEx1(1, "verify_area failed, exitcode = %d", result);
				TRACE_EXIT;
				return -EIO;
			}
			memcpy_tofs(buff, deblock_buffer + buf_pos_rd, cnt);
			buff += cnt;
			to_do -= cnt;	/* what's left from req_len */
			remaining -= cnt;	/* what remains on this tape */
			bytes_read += cnt;	/* what we got so far */
			buf_pos_rd += cnt;	/* index in buffer */
			buf_len_rd -= cnt;	/* remaining bytes in buffer */
		}
		if (eof_mark && buf_len_rd == 0) {	/* nothing left */
			TRACE(5, "partial count because of eof mark");
			if (bytes_read == 0) {
				eof_mark = 0;	/* no need for mark next read */
			}
			break;
		}
	}
	TRACE_EXIT;
	return bytes_read;
}
