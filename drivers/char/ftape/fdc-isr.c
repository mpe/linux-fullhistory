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

 $Source: /home/bas/distr/ftape-2.03b/RCS/fdc-isr.c,v $
 $Author: bas $
 *
 $Revision: 1.36 $
 $Date: 1995/05/27 08:54:21 $
 $State: Beta $
 *
 *      This file contains the interrupt service routine and associated
 *      code for the QIC-40/80 tape streamer device driver.
 */

#include <linux/ftape.h>
#include <asm/io.h>
#include <asm/dma.h>

#define volatile		/* */

#include "tracing.h"
#include "fdc-isr.h"
#include "qic117.h"
#include "fdc-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-io.h"
#include "calibr.h"
#include "ftape-bsm.h"

/*      Global vars.
 */
volatile int expected_stray_interrupts = 0;
volatile int seek_completed = 0;
volatile int interrupt_seen = 0;
volatile int expect_stray_interrupt = 0;
int random_rw = 0;

/*      Local vars.
 */
typedef enum {
	no_error = 0, id_am_error = 0x01, id_crc_error = 0x02,
	data_am_error = 0x04, data_crc_error = 0x08,
	no_data_error = 0x10, overrun_error = 0x20,
} error_cause;
static int hide_interrupt;
static int stop_read_ahead = 0;


static void print_error_cause(int cause)
{
	TRACE_FUN(8, "print_error_cause");

	switch (cause) {
	case no_data_error:
		TRACE(4, "no data error");
		break;
	case id_am_error:
		TRACE(4, "id am error");
		break;
	case id_crc_error:
		TRACE(4, "id crc error");
		break;
	case data_am_error:
		TRACE(4, "data am error");
		break;
	case data_crc_error:
		TRACE(4, "data crc error");
		break;
	case overrun_error:
		TRACE(4, "overrun error");
		break;
	default:
	}
	TRACE_EXIT;
}

static char *
get_fdc_mode_text(fdc_mode_enum fdc_mode)
{
	switch (fdc_mode) {
	case fdc_idle:
		return "fdc_idle";
	case fdc_reading_data:
		return "fdc_reading_data";
	case fdc_seeking:
		return "fdc_seeking";
	case fdc_writing_data:
		return "fdc_writing_data";
	case fdc_reading_id:
		return "fdc_reading_id";
	case fdc_recalibrating:
		return "fdc_recalibrating";
	default:
		return "unknown";
	}
}

static void
decode_irq_cause(fdc_mode_enum fdc_mode, byte st[],
		 char **fdc_mode_txt, error_cause * cause)
{
	TRACE_FUN(8, "decode_irq_cause");

	/*  Valid st[], decode cause of interrupt.
	 */
	*fdc_mode_txt = get_fdc_mode_text(fdc_mode);
	switch (st[0] & ST0_INT_MASK) {
	case FDC_INT_NORMAL:
		TRACEx1(fdc_mode == fdc_reading_id ? 6 : 5,
			"normal completion: %s", *fdc_mode_txt);
		*cause = no_error;
		break;
	case FDC_INT_ABNORMAL:
		TRACEx1(5, "abnormal completion %s", *fdc_mode_txt);
		TRACEx3(6, "ST0: 0x%02x, ST1: 0x%02x, ST2: 0x%02x",
			st[0], st[1], st[2]);
		TRACEx4(6, "C: 0x%02x, H: 0x%02x, R: 0x%02x, N: 0x%02x",
			st[3], st[4], st[5], st[6]);
		if (st[1] & 0x01) {
			if (st[2] & 0x01) {
				*cause = data_am_error;
			} else {
				*cause = id_am_error;
			}
		} else if (st[1] & 0x20) {
			if (st[2] & 0x20) {
				*cause = data_crc_error;
			} else {
				*cause = id_crc_error;
			}
		} else if (st[1] & 0x04) {
			*cause = no_data_error;
		} else if (st[1] & 0x10) {
			*cause = overrun_error;
		}
		print_error_cause(*cause);
		break;
	case FDC_INT_INVALID:
		TRACEx1(5, "invalid completion %s", *fdc_mode_txt);
		*cause = no_error;
		break;
	case FDC_INT_READYCH:
		TRACEx1(5, "ready change %s", *fdc_mode_txt);
		*cause = no_error;
		break;
	default:
	}
	TRACE_EXIT;
}

static void update_history(error_cause cause)
{
	switch (cause) {
	case id_am_error:
		history.id_am_errors++;
		break;
	case id_crc_error:
		history.id_crc_errors++;
		break;
	case data_am_error:
		history.data_am_errors++;
		break;
	case data_crc_error:
		history.data_crc_errors++;
		break;
	case overrun_error:
		history.overrun_errors++;
		break;
	case no_data_error:
		history.no_data_errors++;
		break;
	default:
	}
}

static void skip_bad_sector(buffer_struct * buff)
{
	TRACE_FUN(8, "skip_bad_sector");

	/* Mark sector as soft error and skip it
	 */
	if (buff->remaining > 0) {
		++buff->sector_offset;
		++buff->data_offset;
		--buff->remaining;
		buff->ptr += SECTOR_SIZE;
		buff->bad_sector_map >>= 1;
	} else {
		++buff->sector_offset;	/* hack for error maps */
		TRACE(1, "skipping last sector in segment");
	}
	TRACE_EXIT;
}

static void update_error_maps(buffer_struct * buff, unsigned error_offset)
{
	TRACE_FUN(8, "update_error_maps");
	int hard = 0;

	/* error_offset is a sector offset !
	 */
	if (buff->retry < SOFT_RETRIES) {
		buff->soft_error_map |= (1 << error_offset);
	} else {
		buff->hard_error_map |= (1 << error_offset);
		buff->soft_error_map &= ~buff->hard_error_map;
		buff->retry = -1;	/* will be set to 0 in setup_segment */
		hard = 1;
	}
	TRACEx2(4, "sector %d : %s error", SECTOR(error_offset),
		hard ? "hard" : "soft");
	TRACEx2(5, "hard map: 0x%08lx, soft map: 0x%08lx",
		buff->hard_error_map, buff->soft_error_map);
	TRACE_EXIT;
}

/*
 *  Error cause:   Amount xferred:  Action:
 *
 *  id_am_error         0           mark bad and skip
 *  id_crc_error        0           mark bad and skip
 *  data_am_error       0           mark bad and skip
 *  data_crc_error    % 1024        mark bad and skip
 *  no_data_error       0           retry on write
 *                                  mark bad and skip on read
 *  overrun_error  [ 0..all-1 ]     mark bad and skip
 *  no_error           all          continue
 */
static void determine_progress(buffer_struct * buff, error_cause cause, int mode)
{
	TRACE_FUN(8, "determine_progress");
	unsigned nr_not_xferred;
	unsigned nr_xferred;
	unsigned dma_residue;

	/*  Using less preferred order of disable_dma and get_dma_residue
	 *  because this seems to fail on at least one system if reversed!
	 */
	dma_residue = get_dma_residue(fdc.dma);
	disable_dma(fdc.dma);
	nr_xferred = buff->sector_count * SECTOR_SIZE - dma_residue;
	if (cause == no_error && dma_residue == 0) {
		nr_not_xferred = 0;
	} else {
		if (cause == no_error) {
			TRACEx1(4, "unexpected DMA residue: 0x%04x", dma_residue);
		} else {
			TRACEx1(6, "DMA residue = 0x%04x", dma_residue);
		}
		nr_not_xferred = ((dma_residue + (SECTOR_SIZE - 1)) / SECTOR_SIZE);
		buff->sector_count -= nr_not_xferred;	/* adjust to actual value */
	}
	/*  Update var's influenced by the DMA operation.
	 */
	if (buff->sector_count > 0) {
		buff->sector_offset += buff->sector_count;
		buff->data_offset += buff->sector_count;
		buff->ptr += buff->sector_count * SECTOR_SIZE;
		buff->remaining -= buff->sector_count;
		buff->bad_sector_map >>= buff->sector_count;
	}
	if (cause == no_error) {
		TRACEx1(5, "%d Sector(s) transferred", buff->sector_count);
	} else if (cause == no_data_error) {
		TRACEx1(5, "Sector %d not found", SECTOR(buff->sector_offset));
	} else if (nr_xferred > 0 || cause == id_crc_error ||
		   cause == id_am_error || cause == data_am_error) {
		TRACEx1(5, "Error in sector %d", SECTOR(buff->sector_offset));
	} else if (cause == overrun_error) {
		/* got an overrun error on the first byte, must be a hardware problem
		 */
		TRACE(-1, "Unexpected error: failing DMA controller ?");
	} else {
		TRACEx1(4, "Unexpected error at sector %d", SECTOR(buff->sector_offset));
	}
	/*  Sector_offset points to the problem area, except if we got
	 *  a data_crc_error. In that case it points one past the failing
	 *  sector.
	 *  Now adjust sector_offset so it always points one past he
	 *  failing sector. I.e. skip the bad sector.
	 */
	if (cause != no_error) {
		if (cause != data_crc_error) {
			skip_bad_sector(buff);
		}
		update_error_maps(buff, buff->sector_offset - 1);
	}
	TRACE_EXIT;
}

static int calc_steps(int cmd)
{
	if (current_cylinder > cmd) {
		return current_cylinder - cmd;
	} else {
		return current_cylinder + cmd;
	}
}

static void pause_tape(unsigned segment, int retry, int fdc_mode)
{
	TRACE_FUN(8, "pause_tape");
	int result;
	/*  The 3rd initializer needs to be explicit or else gcc will
	 *  generate a reference to memset :-(
	 */
	byte out[3] =
	{FDC_SEEK, FTAPE_UNIT, 0};

	/*  We'll use a raw seek command to get the tape to rewind
	 *  and stop for a retry.
	 */
	++history.rewinds;
	if (qic117_cmds[current_command].non_intr) {
		TRACE(2, "motion command may be issued too soon");
	}
	if (retry && (fdc_mode == fdc_reading_data || fdc_mode == fdc_reading_id)) {
		current_command = QIC_MICRO_STEP_PAUSE;
		might_be_off_track = 1;
	} else {
		current_command = QIC_PAUSE;
	}
	out[2] = calc_steps(current_command);
	result = fdc_command(out, 3);	/* issue QIC_117 command */
	if (result < 0) {
		TRACEx1(4, "qic-pause failed, status = %d", result);
	} else {
		location.known = 0;
		runner_status = idle;
		hide_interrupt = 1;
		tape_running = 0;
	}
	TRACE_EXIT;
}

static void stop_tape(unsigned segment)
{
	TRACE_FUN(8, "stop_tape");
	int result;
	byte out[3] =
	{FDC_SEEK, FTAPE_UNIT, calc_steps(QIC_STOP_TAPE)};

	if (qic117_cmds[current_command].non_intr) {
		TRACE(2, "motion command may be issued too soon");
	}
	current_command = QIC_STOP_TAPE;
	/*  We'll use a raw seek command to get the tape to stop
	 */
	result = fdc_command(out, 3);	/* issue QIC_117 command */
	if (result < 0) {
		TRACEx1(4, "qic-stop failed, status = %d", result);
	} else {
		runner_status = idle;
		hide_interrupt = 1;
		tape_running = 0;
	}
	TRACE_EXIT;
}

static void continue_xfer(buffer_struct ** p_buff, error_cause cause,
			  int fdc_mode, unsigned skip)
{
	TRACE_FUN(8, "continue_xfer");
	buffer_struct *buff = *p_buff;
	int write = (fdc_mode == fdc_writing_data);
	byte fdc_op = (write) ? FDC_WRITE : FDC_READ;

	if (skip > 0) {
		/* This part can be removed if it never happens
		 */
		if (runner_status != running ||
		    (buff->status != (write ? writing : reading))) {
			TRACEx2(1, "unexpected runner/buffer state %d/%d",
				runner_status, buff->status);
			buff->status = error;
			*p_buff = next_buffer(&head);	/* finish this buffer */
			runner_status = aborting;
			fdc_mode = fdc_idle;
		}
	}
	if (buff->remaining > 0 && calc_next_cluster(&buffer[head]) > 0) {
		/* still sectors left in current segment, continue with this segment
		 */
		if (setup_fdc_and_dma(&buffer[head], fdc_op) < 0) {
			/* failed, abort operation
			 */
			buff->bytes = buff->ptr - buff->address;
			buff->status = error;
			buff = *p_buff = next_buffer(&head);	/* finish this buffer */
			runner_status = aborting;
			fdc_mode = fdc_idle;
		}
	} else {
		/* current segment completed
		 */
		unsigned last_segment = buff->segment_id;
		int eot = ((last_segment + 1) % segments_per_track) == 0;
		int next = buff->next_segment;	/* 0 means stop ! */

		buff->bytes = buff->ptr - buff->address;
		buff->status = done;
		buff = *p_buff = next_buffer(&head);
		if (eot) {
			/* finished last segment on current track, can't continue
			 */
			runner_status = logical_eot;
			fdc_mode = fdc_idle;
		} else if (next > 0) {
			/* continue with next segment
			 */
			if (buff->status == waiting) {
				if (write && next != buff->segment_id) {
					TRACE(5, "segments out of order, aborting write");
					runner_status = do_abort;
					fdc_mode = fdc_idle;
				} else {
					setup_new_segment(&buffer[head], next, 0);
					if (stop_read_ahead) {
						buff->next_segment = 0;
						stop_read_ahead = 0;
					}
					if (calc_next_cluster(&buffer[head]) == 0 ||
					    setup_fdc_and_dma(&buffer[head], fdc_op) != 0) {
						TRACEx1(1, "couldn't start %s-ahead", (write) ? "write" : "read");
						runner_status = do_abort;
						fdc_mode = fdc_idle;
					} else {
						buff->status = (write) ? writing : reading;	/* keep on going */
					}
				}
			} else {
				TRACEx1(5, "all input buffers %s, pausing tape",
					(write) ? "empty" : "full");
				pause_tape(last_segment, 0, fdc_mode);
				runner_status = idle;	/* not quite true until next irq */
			}
		} else {
			/* don't continue with next segment
			 */
			TRACEx1(5, "no %s allowed, stopping tape",
				(write) ? "write next" : "read ahead");
			if (random_rw) {
				stop_tape(last_segment);
			} else {
				pause_tape(last_segment, 0, fdc_mode);
			}
			runner_status = idle;	/* not quite true until next irq */
		}
	}
	TRACE_EXIT;
	return;
}

static void
retry_sector(buffer_struct ** p_buff, error_cause cause, int fdc_mode,
	     unsigned skip)
{
	TRACE_FUN(8, "retry_sector");
	buffer_struct *buff = *p_buff;

	TRACEx1(4, "%s error, will retry",
		(fdc_mode == fdc_writing_data) ? "write" : "read");
	pause_tape(buff->segment_id, 1, fdc_mode);
	runner_status = aborting;
	buff->status = error;
	buff->skip = skip;
	TRACE_EXIT;
}

static unsigned
find_resume_point(buffer_struct * buff)
{
	TRACE_FUN(8, "find_resume_point");
	int i = 0;
	unsigned long mask;
	unsigned long map;

	/*  This function is to be called after all variables have been
	 *  updated to point past the failing sector.
	 *  If there are any soft errors before the failing sector,
	 *  find the first soft error and return the sector offset.
	 *  Otherwise find the last hard error.
	 *  Note: there should always be at least one hard or soft error !
	 */
	if (buff->sector_offset < 1 || buff->sector_offset > 32) {
		TRACEx1(1, "bug: sector_offset = %d", buff->sector_offset);
	} else {
		if (buff->sector_offset >= 32) {	/* C-limitation on shift ! */
			mask = 0xffffffff;
		} else {
			mask = (1 << buff->sector_offset) - 1;
		}
		map = buff->soft_error_map & mask;
		if (map) {
			while ((map & (1 << i)) == 0) {
				++i;
			}
			TRACEx1(4, "at sector %d", SECTOR(i));
		} else {
			map = buff->hard_error_map & mask;
			i = buff->sector_offset - 1;
			if (map) {
				while ((map & (1 << i)) == 0) {
					--i;
				}
				TRACEx1(4, "after sector %d", SECTOR(i));
				++i;	/* first sector after last hard error */
			} else {
				TRACE(1, "bug: no soft or hard errors");
			}
		}
	}
	TRACE_EXIT;
	return i;
}

/*      FDC interrupt service routine.
 */
void
fdc_isr(void)
{
	TRACE_FUN(8, "fdc_isr");
	int result;
	int status;
	error_cause cause = no_error;
	byte in[7];
	static int isr_active = 0;
	int t0;
	buffer_struct *buff = &buffer[head];
	int skip;

	t0 = timestamp();
	if (isr_active) {
		TRACE(-1, "nested interrupt, not good !");
		*fdc.hook = fdc_isr;	/* hook our handler into the fdc code again */
		TRACE_EXIT;
		return;
	}
	++isr_active;
	sti();			/* enables interrupts again */
	status = inb_p(fdc.msr);
	if (status & FDC_BUSY) {	/*  Entering Result Phase */
		hide_interrupt = 0;
		result = fdc_result(in, 7);	/* better get it fast ! */
		if (result < 0) {
			/*  Entered unknown state...
			 */
			TRACE(1, "probably fatal error during FDC Result Phase");
			TRACE(1, "drive may hang until (power) reset :-(");
			/*  what to do next ????
			 */
		} else {
			int i;
			char *fdc_mode_txt;

			decode_irq_cause(fdc_mode, in, &fdc_mode_txt, &cause);
			for (i = 0; i < NR_BUFFERS; ++i) {
				TRACEx3(8, "buffer[%d] status: %d, segment_id: %d",
					i, buffer[i].status, buffer[i].segment_id);
			}
			switch (fdc_mode) {

			case fdc_reading_data:{

					if (cause == no_error) {
						TRACEi(5, "reading segment", buff->segment_id);
					} else {
						TRACEi(4, "error reading segment", buff->segment_id);
					}
					if (runner_status == aborting || runner_status == do_abort) {
						TRACEx1(4, "aborting %s", fdc_mode_txt);
						break;
					}
					if (buff->retry > 0) {
						TRACEx1(5, "this is retry nr %d", buff->retry);
					}
					if (buff->bad_sector_map == FAKE_SEGMENT) {
						/* This condition occurs when reading a `fake' sector that's
						 * not accessible. Doesn't really matter as we would have
						 * ignored it anyway !
						 * Chance is that we're past the next segment now, so the
						 * next operation may fail and result in a retry.
						 */
						TRACE(4, "skipping empty segment (read)");
						buff->remaining = 0;	/* skip failing sector */
						continue_xfer(&buff, no_error, fdc_mode, 1);	/* fake success */
					} else {
						switch (cause) {
						case no_error:{
								determine_progress(buff, cause, fdc_reading_data);
								if (in[2] & 0x40) {
									/*  Handle deleted data in header segments.
									 *  Skip segment and force read-ahead.
									 */
									TRACEx1(2, "deleted data in sector %d",
										SECTOR(buff->sector_offset - 1));
									buff->deleted = 1;
									buff->remaining = 0;	/* abort transfer */
									buff->soft_error_map |= (-1L << buff->sector_offset);
									if (buff->segment_id == 0) {
										stop_read_ahead = 1;	/* stop on next segment */
									}
									buff->next_segment = buff->segment_id + 1;	/* force read-ahead */
									skip = (SECTORS_PER_SEGMENT - buff->sector_offset);
								} else {
									skip = 0;
								}
								continue_xfer(&buff, cause, fdc_mode, skip);
								break;
							}
						case no_data_error:
							/*  Tape started too far ahead of or behind the right sector.
							 *  This may also happen in the middle of a segment !
							 *  Handle no-data as soft error. If next sector fails too,
							 *  a retry (with needed reposition) will follow.
							 */
						case id_am_error:
						case id_crc_error:
						case data_am_error:
						case data_crc_error:
						case overrun_error:{
								int first_error = (buff->soft_error_map == 0 &&
										   buff->hard_error_map == 0);

								update_history(cause);
								determine_progress(buff, cause, fdc_reading_data);
								if (first_error) {
									skip = buff->sector_offset;
								} else {
									skip = find_resume_point(buff);
								}
								/*  Try to resume with next sector on single errors (let ecc
								 *  correct it), but retry on no_data (we'll be past the
								 *  target when we get here so we cannot retry) or on multiple
								 *  errors (reduce chance on ecc failure).
								 */
								if (first_error && cause != no_data_error) {
									continue_xfer(&buff, cause, fdc_mode, skip);
								} else {
									retry_sector(&buff, cause, fdc_mode, skip);
								}
								break;
							}
						default:{
								/*  Don't know why this could happen but find out.
								 */
								TRACE(1, "unexpected error");
								determine_progress(buff, cause, fdc_reading_data);
								retry_sector(&buff, cause, fdc_mode, 0);
								break;
							}
						}
					}
					break;
				}

			case fdc_reading_id:{

					if (cause == no_error) {
						fdc_cyl = in[3];
						fdc_head = in[4];
						fdc_sect = in[5];
						TRACEx3(6, "id read: C: 0x%02x, H: 0x%02x, R: 0x%02x",
							fdc_cyl, fdc_head, fdc_sect);
					} else {	/* no valid information, use invalid sector */
						fdc_cyl =
						    fdc_head =
						    fdc_sect = 0;
						TRACE(5, "Didn't find valid sector Id");
					}
					fdc_mode = fdc_idle;
					break;
				}

			case fdc_writing_data:{

					if (cause == no_error) {
						TRACEi(5, "writing segment", buff->segment_id);
					} else {
						TRACEi(4, "error writing segment", buff->segment_id);
					}
					if (runner_status == aborting || runner_status == do_abort) {
						TRACEx1(5, "aborting %s", fdc_mode_txt);
						break;
					}
					if (buff->retry > 0) {
						TRACEx1(5, "this is retry nr %d", buff->retry);
					}
					if (buff->bad_sector_map == FAKE_SEGMENT) {
						/* This condition occurs when trying to write to a `fake'
						 * sector that's not accessible. Doesn't really matter as
						 * it isn't used anyway ! Might be located at wrong segment,
						 * then we'll fail on the next segment.
						 */
						TRACE(4, "skipping empty segment (write)");
						buff->remaining = 0;	/* skip failing sector */
						continue_xfer(&buff, no_error, fdc_mode, 1);	/* fake success */
					} else {
						switch (cause) {
						case no_error:{
								determine_progress(buff, cause, fdc_writing_data);
								continue_xfer(&buff, cause, fdc_mode, 0);
								break;
							}
						case no_data_error:
						case id_am_error:
						case id_crc_error:
						case data_am_error:
						case overrun_error:{
								update_history(cause);
								determine_progress(buff, cause, fdc_writing_data);
								skip = find_resume_point(buff);
								retry_sector(&buff, cause, fdc_mode, skip);
								break;
							}
						default:{
								if (in[1] & 0x02) {
									TRACE(1, "media not writable");
								} else {
									TRACE(-1, "unforeseen write error");
								}
								fdc_mode = fdc_idle;
								break;
							}
						}
					}
					break;
				}
			default:

				TRACEx1(1, "Warning: unexpected irq during: %s",
					fdc_mode_txt);
				fdc_mode = fdc_idle;
				break;
			}
		}
		if (runner_status == do_abort) {
			/*      cease operation, remember tape position
			 */
			TRACE(5, "runner aborting");
			runner_status = aborting;
			++expected_stray_interrupts;
		}
	} else { /* !FDC_BUSY  */
		/*  clear interrupt, cause should be gotten by issuing
		 *  a Sense Interrupt Status command.
		 */
		if (fdc_mode == fdc_recalibrating || fdc_mode == fdc_seeking) {
			if (hide_interrupt) {
				int st0;
				int pcn;

				result = fdc_sense_interrupt_status(&st0, &pcn);
				current_cylinder = pcn;
				TRACE(5, "handled hidden interrupt");
			}
			seek_completed = 1;
			fdc_mode = fdc_idle;
		} else if (!wait_intr) {
			if (expected_stray_interrupts == 0) {
				TRACE(2, "unexpected stray interrupt");
			} else {
				TRACE(5, "expected stray interrupt");
				--expected_stray_interrupts;
			}
		} else {
			if (fdc_mode == fdc_reading_data || fdc_mode == fdc_writing_data ||
			    fdc_mode == fdc_reading_id) {
				byte status = inb_p(fdc.msr);
				if (status & FDC_BUSY) {
					TRACE(-1, "***** FDC failure, busy too late");
				} else {
					TRACE(-1, "***** FDC failure, no busy");
				}
			} else {
				TRACE(6, "awaited stray interrupt");
			}
		}
		hide_interrupt = 0;
	}
	/*    Handle sleep code.
	 */
	if (!hide_interrupt) {
		++interrupt_seen;
		if (wait_intr) {
			wake_up_interruptible(&wait_intr);
		}
	} else {
		TRACEx1(5, "hiding interrupt while %s", wait_intr ? "waiting" : "active");
	}
	t0 = timediff(t0, timestamp());
	if (t0 >= 1000) {	/* only tell us about long calls */
		TRACEx1(7, "isr() duration: %5d usec", t0);
	}
	*fdc.hook = fdc_isr;	/* hook our handler into the fdc code again */
	TRACE_EXIT;
	--isr_active;
}
