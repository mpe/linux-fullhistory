


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

 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-write.c,v $
 $Author: bas $
 *
 $Revision: 1.26 $
 $Date: 1995/05/27 08:55:27 $
 $State: Beta $
 *
 *      This file contains the writing code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/ftape.h>
#include <asm/segment.h>

#include "tracing.h"
#include "ftape-write.h"
#include "ftape-read.h"
#include "qic117.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-eof.h"
#include "ecc.h"
#include "ftape-bsm.h"


/*      Global vars.
 */

/*      Local vars.
 */
static int buf_pos_wr = 0;
static int last_write_failed = 0;
static int need_flush = 0;

#define WRITE_MULTI  0
#define WRITE_SINGLE 1

void ftape_zap_write_buffers(void)
{
	int i;

	for (i = 0; i < NR_BUFFERS; ++i) {
		buffer[i].status = done;
	}
	need_flush = 0;
}

int copy_and_gen_ecc(char *destination, byte * source,
		     unsigned int bad_sector_map)
{
	TRACE_FUN(8, "copy_and_gen_ecc");
	int result;
	struct memory_segment mseg;
	int bads = count_ones(bad_sector_map);

	if (bads > 0) {
		TRACEi(4, "bad sectors in map:", bads);
	}
	if (bads + 3 >= SECTORS_PER_SEGMENT) {
		TRACE(4, "empty segment");
		mseg.blocks = 0;	/* skip entire segment */
		result = 0;	/* nothing written */
	} else {
		mseg.blocks = SECTORS_PER_SEGMENT - bads;
		mseg.data = destination;
		memcpy(mseg.data, source, (mseg.blocks - 3) * SECTOR_SIZE);
		result = ecc_set_segment_parity(&mseg);
		if (result < 0) {
			TRACE(1, "ecc_set_segment_parity failed");
		} else {
			result = (mseg.blocks - 3) * SECTOR_SIZE;
		}
	}
	TRACE_EXIT;
	return result;
}

void prevent_flush(void)
{
	need_flush = 0;
	ftape_state = idle;
}

int start_writing(int mode)
{
	TRACE_FUN(5, "start_writing");
	int result = 0;
	buffer_struct *buff = &buffer[head];
	int segment_id = buff->segment_id;

	if (ftape_state == writing && buff->status == waiting) {
		setup_new_segment(buff, segment_id, 1);
		if (mode == WRITE_SINGLE) {
			buffer[head].next_segment = 0;	/* stop tape instead of pause */
		}
		calc_next_cluster(buff);	/* prepare */
		buff->status = writing;
		if (runner_status == idle) {
			TRACEi(5, "starting runner for segment", segment_id);
			result = ftape_start_tape(segment_id, buff->sector_offset);
			if (result >= 0) {
				runner_status = running;
			}
		}
		if (result >= 0) {
			result = setup_fdc_and_dma(buff, FDC_WRITE);	/* go */
		}
		ftape_state = writing;
	}
	TRACE_EXIT;
	return result;
}

int loop_until_writes_done(void)
{
	TRACE_FUN(5, "loop_until_writes_done");
	int i;
	int result = 0;

	/*
	 *  Wait until all data is actually written to tape.
	 */
	while (ftape_state == writing && buffer[head].status != done) {
		TRACEx2(7, "tail: %d, head: %d", tail, head);
		for (i = 0; i < NR_BUFFERS; ++i) {
			TRACEx3(8, "buffer[ %d] segment_id: %d, status: %d",
			      i, buffer[i].segment_id, buffer[i].status);
		}
		result = fdc_interrupt_wait(5 * SECOND);
		if (result < 0) {
			TRACE(1, "fdc_interrupt_wait failed");
			last_write_failed = 1;
			break;
		}
		if (buffer[head].status == error) {
			/* Allow escape from loop when signaled !
			 */
			if (current->signal & _DONT_BLOCK) {
				TRACE(2, "interrupted by signal");
				TRACE_EXIT;
				result = -EINTR;	/* is this the right return value ? */
				break;
			}
			if (buffer[head].hard_error_map != 0) {
				/*  Implement hard write error recovery here
				 */
			}
			buffer[head].status = waiting;	/* retry this one */
			if (runner_status == aborting) {
				ftape_dumb_stop();
				runner_status = idle;
			}
			if (runner_status != idle) {
				TRACE(1, "unexpected state: runner_status != idle");
				result = -EIO;
				break;
			}
			start_writing(WRITE_MULTI);
		}
		TRACE(5, "looping until writes done");
		result = 0;	/* normal exit status */
	}
	TRACE_EXIT;
	return result;
}

/*      Write given segment from buffer at address onto tape.
 */
int write_segment(unsigned segment_id, byte * address, int flushing)
{
	TRACE_FUN(5, "write_segment");
	int result = 0;
	int bytes_written = 0;

	TRACEi(5, "segment_id =", segment_id);
	if (ftape_state != writing) {
		if (ftape_state == reading) {
			TRACE(5, "calling ftape_abort_operation");
			result = ftape_abort_operation();
			if (result < 0) {
				TRACE(1, "ftape_abort_operation failed");
			}
		}
		ftape_zap_read_buffers();
		ftape_zap_write_buffers();
		ftape_state = writing;
	}
	/*    if all buffers full we'll have to wait...
	 */
	wait_segment(writing);
	if (buffer[tail].status == error) {
		/*  setup for a retry
		 */
		buffer[tail].status = waiting;
		bytes_written = -EAGAIN;	/* force retry */
		if (buffer[tail].hard_error_map != 0) {
			TRACEx1(1, "warning: %d hard error(s) in written segment",
				count_ones(buffer[tail].hard_error_map));
			TRACEx1(4, "hard_error_map = 0x%08lx", buffer[tail].hard_error_map);
			/*  Implement hard write error recovery here
			 */
		}
	} else if (buffer[tail].status == done) {
		history.defects += count_ones(buffer[tail].hard_error_map);
	} else {
		TRACE(1, "wait for empty segment failed");
		result = -EIO;
	}
	/*    If just passed last segment on tape: wait for BOT or EOT mark.
	 */
	if (result >= 0 && runner_status == logical_eot) {
		int status;

		result = ftape_ready_wait(timeout.seek, &status);
		if (result < 0 || (status & (QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT)) == 0) {
			TRACE(1, "eot/bot not reached");
		} else {
			runner_status = end_of_tape;
		}
	}
	/*    should runner stop ?
	 */
	if (result >= 0 &&
	(runner_status == aborting || runner_status == buffer_underrun ||
	 runner_status == end_of_tape)) {
		if (runner_status != end_of_tape) {
			result = ftape_dumb_stop();
		}
		if (result >= 0) {
			if (runner_status == aborting) {
				if (buffer[head].status == writing) {
					buffer[head].status = done;	/* ????? */
				}
			}
			runner_status = idle;	/* aborted ? */
		}
	}
	/*  Don't start tape if runner idle and segment empty.
	 */
	if (result >= 0 && !(runner_status == idle &&
		    get_bad_sector_entry(segment_id) == EMPTY_SEGMENT)) {
		if (buffer[tail].status == done) {
			/*    now at least one buffer is empty, fill it with our data.
			 *    skip bad sectors and generate ecc.
			 *    copy_and_gen_ecc return nr of bytes written,
			 *    range 0..29 Kb inclusive !
			 */
			result = copy_and_gen_ecc(buffer[tail].address, address,
				       get_bad_sector_entry(segment_id));
			if (result >= 0) {
				bytes_written = result;
				buffer[tail].segment_id = segment_id;
				buffer[tail].status = waiting;
				next_buffer(&tail);
			}
		}
		/*    Start tape only if all buffers full or flush mode.
		 *    This will give higher probability of streaming.
		 */
		if (result >= 0 && runner_status != running &&
		    ((head == tail && buffer[tail].status == waiting) || flushing)) {
			result = start_writing(WRITE_MULTI);
		}
	}
	TRACE_EXIT;
	return (result < 0) ? result : bytes_written;
}

/*  Write as much as fits from buffer to the given segment on tape
 *  and handle retries.
 *  Return the number of bytes written (>= 0), or:
 *      -EIO          write failed
 *      -EINTR        interrupted by signal
 *      -ENOSPC       device full
 */
int _write_segment(unsigned int segment_id, byte * buffer, int flush)
{
	TRACE_FUN(5, "_write_segment");
	int retry = 0;
	int result;

	history.used |= 2;
	for (;;) {
		if (segment_id > ftape_last_segment.id && !flush) {
			result = -ENOSPC;	/* tape full */
			break;
		}
		result = write_segment(segment_id, buffer, flush);
		if (result < 0) {
			if (result == -EAGAIN) {
				if (++retry > 100) {
					TRACE(1, "write failed, >100 retries in segment");
					result = -EIO;	/* give up */
					break;
				} else {
					TRACEx1(2, "write error, retry %d", retry);
				}
			} else {
				TRACEi(1, "write_segment failed, error:", -result);
				break;
			}
		} else {	/* success */
			if (result == 0) {	/* empty segment */
				TRACE(4, "empty segment, nothing written");
			}
			break;
		}
		/* Allow escape from loop when signaled !
		 */
		if (current->signal & _DONT_BLOCK) {
			TRACE(2, "interrupted by signal");
			TRACE_EXIT;
			result = -EINTR;	/* is this the right return value ? */
			break;
		}
	}
	TRACE_EXIT;
	return result;
}

int update_header_segment(unsigned segment, byte * buffer)
{
	TRACE_FUN(5, "update_header_segment");
	int result = 0;
	int status;

	if (buffer == NULL) {
		TRACE(5, "no input buffer specified");
		buffer = deblock_buffer;
		result = read_segment(used_header_segment, buffer, &status, 0);
		if (bad_sector_map_changed) {
			store_bad_sector_map(buffer);
		}
		if (failed_sector_log_changed) {
			update_failed_sector_log(buffer);
		}
	}
	if (result >= 0 && GET4(buffer, 0) != 0xaa55aa55) {
		TRACE(1, "wrong header signature found, aborting");
		result = -EIO;
	}
	if (result >= 0) {
		result = _write_segment(segment, buffer, 0);
		if (result >= 0 && runner_status == idle) {
			/*  Force flush for single segment instead of relying on
			 *  flush in read_segment for multiple segments.
			 */
			result = start_writing(WRITE_SINGLE);
			if (result >= 0 && ftape_state == writing) {
				result = loop_until_writes_done();
				prevent_flush();
			}
		}
#ifdef VERIFY_HEADERS
		if (result >= 0) {	/* read back and verify */
			result = read_segment(segment, scratch_buffer, &status, 0);
			/*  Should retry if soft error during read !
			 *  TO BE IMPLEMENTED
			 */
			if (result >= 0) {
				if (memcmp(buffer, scratch_buffer, sizeof(buffer)) == 0) {
					result = 0;	/* verified */
					TRACE(5, "verified");
				} else {
					result = -EIO;	/* verify failed */
					TRACE(5, "verify failed");
				}
			}
		}
#endif
	}
	TRACE_EXIT;
	return result;
}

int ftape_write_header_segments(byte * buffer)
{
	TRACE_FUN(5, "ftape_write_header_segments");
	int result = 0;
	int retry = 0;
	int header_1_ok = 0;
	int header_2_ok = 0;

	do {
		if (!header_1_ok) {
			result = update_header_segment(header_segment_1, buffer);
			if (result < 0) {
				continue;
			}
			header_1_ok = 1;
		}
		if (!header_2_ok) {
			result = update_header_segment(header_segment_2, buffer);
			if (result < 0) {
				continue;
			}
			header_2_ok = 1;
		}
	} while (result < 0 && retry++ < 3);
	if (result < 0) {
		if (!header_1_ok) {
			TRACE(1, "update of first header segment failed");
		}
		if (!header_2_ok) {
			TRACE(1, "update of second header segment failed");
		}
		result = -EIO;
	}
	TRACE_EXIT;
	return result;
}

int ftape_update_header_segments(byte * buffer, int update)
{
	TRACE_FUN(5, "ftape_update_header_segments");
	int result = 0;
	int dummy;
	int header_changed = 1;

	if (ftape_state == writing) {
		result = loop_until_writes_done();
	}
	if (read_only) {
		result = 0;	/* exit and fake success */
		TRACE(4, "Tape set read-only: no update");
	} else if (result >= 0) {
		result = ftape_abort_operation();
		if (result >= 0) {
			if (buffer == NULL) {
				if (bad_sector_map_changed || failed_sector_log_changed) {
					ftape_seek_to_bot();	/* prevents extra rewind */
					buffer = deblock_buffer;
					result = read_segment(used_header_segment, buffer, &dummy, 0);
					if (result < 0) {
						TRACE_EXIT;
						return result;
					}
				}
				header_changed = 0;
			}
			if (update) {
				if (bad_sector_map_changed) {
					store_bad_sector_map(buffer);
					header_changed = 1;
				}
				if (failed_sector_log_changed) {
					update_failed_sector_log(buffer);
					header_changed = 1;
				}
			}
			if (header_changed) {
				ftape_seek_to_bot();	/* prevents extra rewind */
				result = ftape_write_header_segments(buffer);
			}
		}
	}
	TRACE_EXIT;
	return result;
}

int ftape_flush_buffers(void)
{
	TRACE_FUN(5, "ftape_flush_buffers");
	int result;
	int pad_count;
	int data_remaining;
	static int active = 0;

	if (active) {
		TRACE(5, "nested call, abort");
		TRACE_EXIT;
		return 0;
	}
	active = 1;
	TRACEi(5, "entered, ftape_state =", ftape_state);
	if (ftape_state != writing && !need_flush) {
		active = 0;
		TRACE(5, "no need for flush");
		TRACE_EXIT;
		return 0;
	}
	data_remaining = buf_pos_wr;
	buf_pos_wr = 0;		/* prevent further writes if this fails */
	TRACE(5, "flushing write buffers");
	if (last_write_failed) {
		ftape_zap_write_buffers();
		active = 0;
		TRACE_EXIT;
		return write_protected ? -EROFS : -EIO;
	}
	/*
	 *    If there is any data not written to tape yet, append zero's
	 *    up to the end of the sector. Then write the segment(s) to tape.
	 */
	if (data_remaining > 0) {
		int written;

		do {
			TRACEi(4, "remaining in buffer:", data_remaining);
			pad_count = sizeof(deblock_buffer) - data_remaining;
			TRACEi(7, "flush, padding count:", pad_count);
			memset(deblock_buffer + data_remaining, 0, pad_count);	/* pad buffer */
			result = _write_segment(ftape_seg_pos, deblock_buffer, 1);
			if (result < 0) {
				if (result != -ENOSPC) {
					last_write_failed = 1;
				}
				active = 0;
				TRACE_EXIT;
				return result;
			}
			written = result;
			clear_eof_mark_if_set(ftape_seg_pos, written);
			TRACEi(7, "flush, moved out buffer:", written);
			if (written > 0) {
				data_remaining -= written;
				if (data_remaining > 0) {
					/*  Need another segment for remaining data, move the remainder
					 *  to the beginning of the buffer
					 */
					memmove(deblock_buffer, deblock_buffer + written, data_remaining);
				}
			}
			++ftape_seg_pos;
		} while (data_remaining > 0);
		/*  Data written to last segment == data_remaining + written
		 *  value is in range [1..29K].
		 */
		TRACEx2(4, "last write: %d, netto pad-count: %d",
			data_remaining + written, -data_remaining);
		if (-1024 < data_remaining && data_remaining <= 0) {
			/*  Last sector of segment was used for data, so put eof mark
			 *  in next segment and position at second file mark.
			 */
			if (ftape_weof(2, ftape_seg_pos, 1) >= 0) {
				++ftape_seg_pos;	/* position between file marks */
			}
		} else {
			/*  Put eof mark in previous segment after data and position
			 *  at second file mark.
			 */
			ftape_weof(2, ftape_seg_pos - 1, 1 +
				   ((SECTOR_SIZE - 1 + result + data_remaining) / SECTOR_SIZE));
		}
	} else {
		TRACE(7, "deblock_buffer empty");
		if (ftape_weof(2, ftape_seg_pos, 1) >= 0) {
			++ftape_seg_pos;	/* position between file marks */
		}
		start_writing(WRITE_MULTI);
	}
	TRACE(7, "waiting");
	result = loop_until_writes_done();
	if (result < 0) {
		TRACE(1, "flush buffers failed");
	}
	ftape_state = idle;
	last_write_failed = 0;
	need_flush = 0;
	active = 0;
	TRACE_EXIT;
	return result;
}

int _ftape_write(const char *buff, int req_len)
{
	TRACE_FUN(5, "_ftape_write");
	int result = 0;
	int cnt;
	int written = 0;

	if (write_protected) {
		TRACE(1, "error: cartridge write protected");
		last_write_failed = 1;
		result = -EROFS;
	} else if (ftape_offline || !formatted || no_tape) {
		result = -EIO;
	} else if (first_data_segment == -1) {
		/*
		 *    If we haven't read the header segment yet, do it now.
		 *    This will verify the configuration, get the eof markers
		 *    and the bad sector table.
		 *    We'll use the deblock buffer for scratch.
		 */
		result = read_header_segment(deblock_buffer);
		if (result >= 0 && ftape_seg_pos > ftape_last_segment.id) {
			result = -ENOSPC;	/* full is full */
		}
	}
	if (result < 0) {
		TRACE_EXIT;
		return result;
	}
	/*
	 *    This part writes data blocks to tape until the
	 *    requested amount is written.
	 *    The data will go in a buffer until it's enough
	 *    for a segment without bad sectors. Then we'll write
	 *    that segment to tape.
	 *    The bytes written will be removed from the buffer
	 *    and the process is repeated until there is less
	 *    than one segment to write left in the buffer.
	 */
	while (req_len > 0) {
		int space_left = sizeof(deblock_buffer) - buf_pos_wr;

		TRACEi(7, "remaining req_len:", req_len);
		TRACEi(7, "          buf_pos:", buf_pos_wr);
		cnt = (req_len < space_left) ? req_len : space_left;
		if (cnt > 0) {
			result = verify_area(VERIFY_READ, buff, cnt);
			if (result) {
				TRACE(1, "verify_area failed");
				last_write_failed = 1;
				TRACE_EXIT;
				return result;
			}
			memcpy_fromfs(deblock_buffer + buf_pos_wr, buff, cnt);
			buff += cnt;
			req_len -= cnt;
			buf_pos_wr += cnt;
		}
		TRACEi(7, "moved into blocking buffer:", cnt);
		while (buf_pos_wr >= sizeof(deblock_buffer)) {
			/*  If this is the last buffer to be written, let flush handle it.
			 */
			if (ftape_seg_pos >= ftape_last_segment.id) {
				TRACEi(7, "remaining in blocking buffer:", buf_pos_wr);
				TRACEi(7, "just written bytes:", written + cnt);
				TRACE_EXIT;
				return written + cnt;
			}
			/* Got one full buffer, write it to disk
			 */
			result = _write_segment(ftape_seg_pos, deblock_buffer, 0);
			TRACEi(5, "_write_segment result =", result);
			if (result < 0) {
				if (result == -EAGAIN) {
					TRACE(5, "retry...");
					continue;	/* failed, retry same segment */
				}
				last_write_failed = 1;
				TRACE_EXIT;
				return result;
			} else {
				clear_eof_mark_if_set(ftape_seg_pos, result);
			}
			if (result > 0 && result < buf_pos_wr) {
				/* Partial write: move remainder in lower part of buffer
				 */
				memmove(deblock_buffer, deblock_buffer + result, buf_pos_wr - result);
			}
			TRACEi(7, "moved out of blocking buffer:", result);
			buf_pos_wr -= result;	/* remainder */
			++ftape_seg_pos;
			/* Allow us to escape from this loop with a signal !
			 */
			if (current->signal & _DONT_BLOCK) {
				TRACE(2, "interrupted by signal");
				last_write_failed = 1;
				TRACE_EXIT;
				return -EINTR;	/* is this the right return value ? */
			}
		}
		written += cnt;
	}
	TRACEi(7, "remaining in blocking buffer:", buf_pos_wr);
	TRACEi(7, "just written bytes:", written);
	last_write_failed = 0;
	if (!need_flush && written > 0) {
		need_flush = 1;
	}
	TRACE_EXIT;
	return written;		/* bytes written */
}

int ftape_fix(void)
{
	TRACE_FUN(5, "ftape_fix");
	int result = 0;
	int dummy;
	int status;

	if (write_protected) {
		result = -EROFS;
	} else {
		/*  This will copy header segment 2 to header segment 1
		 *  Spares us a tape format operation if header 2 is still good.
		 */
		header_segment_1 = 0;
		header_segment_2 = 1;
		first_data_segment = 2;
		result = read_segment(header_segment_2, scratch_buffer, &dummy, 0);
		result = ftape_ready_wait(timeout.pause, &status);
		result = ftape_write_header_segments(scratch_buffer);
	}
	TRACE_EXIT;
	return result;
}
