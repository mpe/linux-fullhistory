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

 *
 *      This file contains the non-read/write ftape functions
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/ftape.h>
#include <asm/segment.h>

#include "tracing.h"
#include "ftape-eof.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-write.h"
#include "ftape-read.h"
#include "ftape-rw.h"
#include "qic117.h"
#include "ftape-bsm.h"


/*      Global vars.
 */
int segments_per_track = 102;
int segments_per_head = 1020;
int segments_per_cylinder = 4;
int tracks_per_tape = 20;
int ftape_failure = 1;
int ftape_seg_pos = 0;
int first_data_segment = -1;
int ftape_state = idle;		/* use buffer_state_enum */
history_record history;
int write_protected;
int ftape_offline = 0;
int no_tape = 1;
int formatted = 0;
int ftape_data_rate = 0;
int going_offline = 0;
int read_only = 0;

/*      Local vars.
 */
static int ftape_last_error = 0;
static const vendor_struct vendors[] = QIC117_VENDORS;
static const wakeup_method methods[] = WAKEUP_METHODS;
static int init_drive_needed = 1;


static int ftape_not_operational(int status)
{
	/* return true if status indicates tape can not be used.
	 */
	return ((status ^ QIC_STATUS_CARTRIDGE_PRESENT) &
		(QIC_STATUS_ERROR |
		 QIC_STATUS_CARTRIDGE_PRESENT |
		 QIC_STATUS_NEW_CARTRIDGE));
}

int ftape_seek_to_eot(void)
{
	TRACE_FUN(8, "ftape_seek_to_eot");
	int result;
	int status;

	result = ftape_ready_wait(timeout.pause, &status);
	while ((status & QIC_STATUS_AT_EOT) == 0) {
		if (result < 0) {
			TRACE(1, "failed");
			TRACE_EXIT;
			return result;
		}
		if (ftape_not_operational(status)) {
			TRACE_EXIT;
			return -EIO;
		}
		result = ftape_command_wait(QIC_PHYSICAL_FORWARD,
					    timeout.rewind, &status);
	}
	TRACE_EXIT;
	return 0;
}

int ftape_seek_to_bot(void)
{
	TRACE_FUN(8, "ftape_seek_to_bot");
	int result;
	int status;

	result = ftape_ready_wait(timeout.pause, &status);
	while ((status & QIC_STATUS_AT_BOT) == 0) {
		if (result < 0) {
			TRACE(1, "failed");
			TRACE_EXIT;
			return result;
		}
		if (ftape_not_operational(status)) {
			TRACE_EXIT;
			return -EIO;
		}
		result = ftape_command_wait(QIC_PHYSICAL_REVERSE,
					    timeout.rewind, &status);
	}
	TRACE_EXIT;
	return 0;
}

void ftape_reset_position(void)
{
	ftape_seg_pos = first_data_segment;
	reset_eof_list();
}

int ftape_new_cartridge(void)
{
	location.track = -1;	/* force seek on first access */
	first_data_segment = -1;	/* unknown */
	ftape_zap_read_buffers();
	ftape_zap_write_buffers();
	ftape_reset_position();
	return 0;
}

int ftape_abort_operation(void)
{
	TRACE_FUN(5, "ftape_abort_operation");
	int result = 0;
	int i;
	int status;

	if (runner_status == running) {
		TRACE(5, "aborting runner, waiting");
		runner_status = do_abort;
		/* set timeout so that the tape will run to logical EOT
		 * if we missed the last sector and there are no queue pulses.
		 */
		result = ftape_dumb_stop();
		if (result == 0) {
			runner_status = idle;
		}
	}
	if (runner_status != idle) {
		if (runner_status == do_abort) {
			TRACE(5, "forcing runner abort");
		}
		TRACE(5, "stopping tape");
		result = ftape_command_wait(QIC_STOP_TAPE, timeout.stop, &status);
		location.known = 0;
		runner_status = idle;
	}
	for (i = 0; i < NR_BUFFERS; ++i) {
		buffer[i].status = waiting;
	}
	head = tail = 0;
	TRACE_EXIT;
	return result;
}

int lookup_vendor_id(int vendor_id)
{
	int i = 0;

	while (vendors[i].vendor_id != vendor_id) {
		if (++i >= NR_ITEMS(vendors)) {
			return -1;
		}
	}
	return i;
}

void ftape_detach_drive(void)
{
	TRACE_FUN(8, "ftape_detach_drive");

	TRACE(5, "disabling tape drive and fdc");
	ftape_put_drive_to_sleep(drive_type);
	fdc_catch_stray_interrupts(1);	/* one always comes */
	fdc_disable();
	fdc_release_irq_and_dma();
	TRACE_EXIT;
}

static void clear_history(void)
{
	history.used = 0;
	history.id_am_errors =
	    history.id_crc_errors =
	    history.data_am_errors =
	    history.data_crc_errors =
	    history.overrun_errors =
	    history.no_data_errors =
	    history.retries =
	    history.crc_errors =
	    history.crc_failures =
	    history.ecc_failures =
	    history.corrected =
	    history.defects =
	    history.rewinds = 0;
}

int ftape_activate_drive(vendor_struct * drive_type)
{
	TRACE_FUN(5, "ftape_activate_drive");
	int result = 0;

	/* If we already know the drive type, wake it up.
	 * Else try to find out what kind of drive is attached.
	 */
	if (drive_type->wake_up != unknown_wake_up) {
		TRACE(5, "enabling tape drive and fdc");
		result = ftape_wakeup_drive(drive_type->wake_up);
		if (result < 0) {
			TRACE(1, "known wakeup method failed");
		}
	} else {
		int old_tracing = tracing;
		wake_up_types method;

		/*  Try to awaken the drive using all known methods.
		 *  Lower tracing for a while.
		 */
		if (tracing <= 4) {
			tracing = 0;
		}
		for (method = no_wake_up; method < NR_ITEMS(methods); ++method) {
			drive_type->wake_up = method;
#if 0
			/*  Test setup for dual drive configuration in dodo.
			 *  /dev/rft2 uses mountain wakeup only -> Archive QIC-80
			 *  /dev/rft3 uses colorado wakeup only -> Jumbo QIC-40
			 *  Other systems will use the normal scheme.
			 */
			if ((FTAPE_UNIT < 2) ||
			(FTAPE_UNIT == 2 && method == wake_up_mountain) ||
			(FTAPE_UNIT == 3 && method == wake_up_colorado)) {
				result = ftape_wakeup_drive(drive_type->wake_up);
			} else {
				result = -EIO;
			}
#else
			result = ftape_wakeup_drive(drive_type->wake_up);
#endif
			if (result >= 0) {
				int tracing = old_tracing;	/* fool TRACE */
				TRACEx1(2, "drive wakeup method: %s",
				      methods[drive_type->wake_up].name);
				break;
			}
		}
		tracing = old_tracing;
		if (method >= NR_ITEMS(methods)) {
			/* no response at all, cannot open this drive */
			drive_type->wake_up = unknown_wake_up;
			TRACE(1, "no tape drive found !");
			tracing = old_tracing;
			result = -ENODEV;
		}
	}
	TRACE_EXIT;
	return result;
}

int ftape_get_drive_status(int *new_tape, int *no_tape, int *wp_tape)
{
	TRACE_FUN(5, "ftape_get_drive_status");
	int result;
	int status;

	*no_tape =
	    *wp_tape = 0;
	/*    Tape drive is activated now.
	 *    First clear error status if present.
	 */
	do {
		result = ftape_ready_wait(timeout.reset, &status);
		if (result < 0) {
			if (result == -ETIME) {
				TRACE(1, "ftape_ready_wait timeout");
			} else if (result == -EINTR) {
				TRACE(1, "ftape_ready_wait aborted");
			} else {
				TRACE(1, "ftape_ready_wait failed");
			}
			result = -EIO;
			break;
		}
		/*  Clear error condition (drive is ready !)
		 */
		if (status & QIC_STATUS_ERROR) {
			int error;
			int command;

			TRACE(1, "error status set");
			result = ftape_report_error(&error, &command, 1);
			if (result < 0) {
				TRACEi(1, "report_error_code failed:", result);
				ftape_reset_drive();	/* hope it's working next time */
				init_drive_needed = 1;
				result = -EIO;
				break;
			} else if (error != 0) {
				TRACEi(4, "error code   :", error);
				TRACEi(4, "error command:", command);
			}
		}
		if (status & QIC_STATUS_NEW_CARTRIDGE) {
			int error;
			int command;
			int old_tracing = tracing;

			/*  Undocumented feature: Must clear (not present!) error 
			 *  here or we'll fail later.
			 */
			tracing = 0;
			ftape_report_error(&error, &command, 1);
			tracing = old_tracing;
			TRACE(3, "status: new cartridge");
			*new_tape = 1;
		}
	} while (status & QIC_STATUS_ERROR);

	*no_tape = !(status & QIC_STATUS_CARTRIDGE_PRESENT);
	*wp_tape = (status & QIC_STATUS_WRITE_PROTECT);
	if (*no_tape) {
		TRACE(1, "no cartridge present");
	} else {
		if (*wp_tape) {
			TRACE(2, "Write protected cartridge");
		}
	}
	TRACE_EXIT;
	return result;
}

void ftape_log_vendor_id(void)
{
	TRACE_FUN(5, "ftape_log_vendor_id");
	int vendor_index;

	ftape_report_vendor_id(&drive_type.vendor_id);
	vendor_index = lookup_vendor_id(drive_type.vendor_id);
	if (drive_type.vendor_id == UNKNOWN_VENDOR &&
	    drive_type.wake_up == wake_up_colorado) {
		vendor_index = 0;
		drive_type.vendor_id = 0;	/* hack to get rid of all this mail */
	}
	if (vendor_index < 0) {
		/* Unknown vendor id, first time opening device.
		 * The drive_type remains set to type found at wakeup time, this
		 * will probably keep the driver operating for this new vendor.
		 */
		TRACE(-1, "============ unknown vendor id ===========");
		TRACE(-1, "A new, yet unsupported tape drive is found");
		TRACE(-1, "Please report the following values:");
		TRACEx1(-1, "   Vendor id     : 0x%04x", drive_type.vendor_id);
		TRACEx1(-1, "   Wakeup method : %s", methods[drive_type.wake_up].name);
		TRACE(-1, "And a description of your tape drive to:");
		TRACE(-1, "Kai Harrekilde-Petersen <khp@dolphinics.no>");
		TRACE(-1, "==========================================");
		drive_type.speed = 500;		/* deci-ips: very safe value */
	} else {
		drive_type.name = vendors[vendor_index].name;
		drive_type.speed = vendors[vendor_index].speed;
		TRACEx1(3, "tape drive type: %s", drive_type.name);
		/* scan all methods for this vendor_id in table */
		while (drive_type.wake_up != vendors[vendor_index].wake_up) {
			if (vendor_index < NR_ITEMS(vendors) - 1 &&
			    vendors[vendor_index + 1].vendor_id == drive_type.vendor_id) {
				++vendor_index;
			} else {
				break;
			}
		}
		if (drive_type.wake_up != vendors[vendor_index].wake_up) {
			TRACE(-1, "==========================================");
			TRACE(-1, "wakeup type mismatch:");
			TRACEx2(-1, "found: %s, expected: %s",
				methods[drive_type.wake_up].name,
			    methods[vendors[vendor_index].wake_up].name);
			TRACE(-1, "please report this to <khp@dolphinics.no>");
			TRACE(-1, "==========================================");
		}
	}
	TRACE_EXIT;
}

void ftape_calc_timeouts(void)
{
	TRACE_FUN(8, "ftape_calc_timeouts");
	int speed;		/* deci-ips ! */
	int length;

	/*                           tape transport speed
	 *  data rate:        QIC-40   QIC-80   QIC-3010 QIC-3020
	 *
	 *    250 Kbps        25 ips     n/a      n/a      n/a
	 *    500 Kbps        50 ips   34 ips   22.6 ips   n/a
	 *      1 Mbps          n/a    68 ips   45.2 ips 22.6 ips
	 *      2 Mbps          n/a      n/a      n/a    45.2 ips
	 *
	 *  fast tape transport speed is at least 68 ips.
	 */
	switch (qic_std) {
	case QIC_TAPE_QIC40:
		speed = (ftape_data_rate == 3) ? 250 : 500;
		break;
	case QIC_TAPE_QIC80:
		speed = (ftape_data_rate == 2) ? 340 : 680;
		break;
	case QIC_TAPE_QIC3010:
		speed = (ftape_data_rate == 2) ? 226 : 452;
		break;
	case QIC_TAPE_QIC3020:
		speed = (ftape_data_rate == 1) ? 226 : 452;
		break;
	default:
		TRACE(-1, "Unknown qic_std (bug) ?");
		speed = 500;
		break;
	}
	if (tape_len <= 0) {
		/*  Handle unknown length tapes as 1100 ft ones (worst case)
		 */
		TRACE(1, "Unknown tape length, using worst case timing values!");
		length = 1100;
	} else {
		length = tape_len;
	}
	if (drive_type.speed == 0) {
		unsigned long t0;
		int dt;

		ftape_seek_to_bot();
		t0 = jiffies;
		ftape_seek_to_eot();
		ftape_seek_to_bot();
		dt = (int) ((jiffies - t0) * MSPT);
		drive_type.speed = (2 * 12 * length * 1000) / dt;
		TRACE(-1, "==========================================");
		TRACEx1(-1, "drive : %s", drive_type.name);
		TRACEx2(-1, "delta time = %d, length = %d", dt, length);
		TRACEx1(-1, "has max tape speed of %d ips", drive_type.speed);
		TRACE(-1, "please report this to <khp@dolphinics.no>");
		TRACE(-1, "==========================================");
	}
	/*  time to go from bot to eot at normal speed (data rate):
	 *  time = (1+delta) * length (ft) * 12 (inch/ft) / speed (ips)
	 *  delta = 10 % for seek speed, 20 % for rewind speed.
	 */
	timeout.seek = (length * 132 * SECOND) / speed;
	timeout.rewind = (length * 144 * SECOND) / (10 * drive_type.speed);
	timeout.reset = 20 * SECOND + timeout.rewind;
	TRACEx2(4, "speed = %d, length = %d", speed, length);
	TRACEx1(4, "seek timeout: %d sec", (timeout.seek + 500) / 1000);
	TRACEx1(4, "rewind timeout: %d sec", (timeout.rewind + 500) / 1000);
	TRACE_EXIT;
}

int ftape_init_drive(int *formatted)
{
	TRACE_FUN(5, "ftape_init_drive");
	int result = 0;
	int status;

	result = ftape_report_raw_drive_status(&status);
	if (result >= 0 && (status & QIC_STATUS_CARTRIDGE_PRESENT)) {
		if (!(status & QIC_STATUS_AT_BOT)) {
			/*  Antique drives will get here after a soft reset,
			 *  modern ones only if the driver is loaded when the
			 *  tape wasn't rewound properly.
			 */
			ftape_seek_to_bot();
		}
		if (!(status & QIC_STATUS_REFERENCED)) {
			TRACE(5, "starting seek_load_point");
			result = ftape_command_wait(QIC_SEEK_LOAD_POINT,
						 timeout.reset, &status);
			if (result < 0) {
				TRACE(1, "seek_load_point failed (command)");
			}
		}
	}
	if (result >= 0) {
		int rate;

		*formatted = (status & QIC_STATUS_REFERENCED);
		if (!*formatted) {
			TRACE(1, "Warning: tape is not formatted !");
		}
		/*  Select highest rate supported by both fdc and drive.
		 *  Start with highest rate supported by the fdc.
		 */
		if (fdc.type >= i82078_1)
			rate = 0;
		else if (fdc.type >= i82077)
			rate = 1;
		else
			rate = 2;
		do {
			result = ftape_set_data_rate(rate);
			if (result >= 0) {
				ftape_calc_timeouts();
				break;
			}
			++rate;
		} while (rate < 4);
		if (result < 0) {
			result = -EIO;
		}
	}
	if (result >= 0) {
		/* Tape should be at bot if new cartridge ! */
		ftape_new_cartridge();
	}
	init_drive_needed = 0;
	TRACE_EXIT;
	return result;
}

/*      OPEN routine called by kernel-interface code
 */
int _ftape_open(void)
{
	TRACE_FUN(8, "_ftape_open");
	int result;
	static int new_tape = 1;

	result = fdc_init();
	if (result >= 0) {
		result = ftape_activate_drive(&drive_type);
		if (result < 0) {
			fdc_disable();
			fdc_release_irq_and_dma();

		} else {
			result = ftape_get_drive_status(&new_tape, &no_tape, &write_protected);
			if (result < 0) {
				ftape_detach_drive();
			} else {
				if (drive_type.vendor_id == UNKNOWN_VENDOR) {
					ftape_log_vendor_id();
				}
				if (no_tape) {
					ftape_offline = 1;
				} else if (new_tape) {
					ftape_offline = 0;
					init_drive_needed = 1;
					read_only = 0;	/* enable writes again */
				}
				if (!ftape_offline && init_drive_needed) {
					result = ftape_init_drive(&formatted);
					if (result >= 0) {
						new_tape = 0;
					} else {
						ftape_detach_drive();
					}
				}
				if (result >= 0) {
					clear_history();
				}
			}
		}
	}
	TRACE_EXIT;
	return result;
}

/*      RELEASE routine called by kernel-interface code
 */
int _ftape_close(void)
{
	TRACE_FUN(8, "_ftape_close");
	int result = 0;
	int last_segment = 0;

	if (!ftape_offline) {
		result = ftape_flush_buffers();
		last_segment = ftape_seg_pos - 1;
		if (!(ftape_unit & FTAPE_NO_REWIND)) {
			if (result >= 0) {
				result = ftape_update_header_segments(NULL, 1);
				if (result < 0) {
					TRACE(1, "error: update of header segments failed");
				}
			} else {
				TRACE(1, "error: unable to update header segments");
			}
		}
		ftape_abort_operation();
		if (!(ftape_unit & FTAPE_NO_REWIND)) {
			if (!no_tape) {
				TRACE(5, "rewinding tape");
				result = ftape_seek_to_bot();
			}
			ftape_reset_position();
			ftape_zap_read_buffers();
			ftape_zap_write_buffers();
		}
	}
	ftape_detach_drive();
	fdc_uninit();
	if (history.used) {
		TRACE(3, "== Non-fatal errors this run: ==");
		TRACE(3, "fdc isr statistics:");
		TRACEi(3, " id_am_errors     :", history.id_am_errors);
		TRACEi(3, " id_crc_errors    :", history.id_crc_errors);
		TRACEi(3, " data_am_errors   :", history.data_am_errors);
		TRACEi(3, " data_crc_errors  :", history.data_crc_errors);
		TRACEi(3, " overrun_errors   :", history.overrun_errors);
		TRACEi(3, " no_data_errors   :", history.no_data_errors);
		TRACEi(3, " retries          :", history.retries);
		if (history.used & 1) {
			TRACE(3, "ecc statistics:");
			TRACEi(3, " crc_errors       :", history.crc_errors);
			TRACEi(3, " crc_failures     :", history.crc_failures);
			TRACEi(3, " ecc_failures     :", history.ecc_failures);
			TRACEi(3, " sectors corrected:", history.corrected);
		}
		TRACEx2(3, "media defects     : %d%s", history.defects,
			history.defects ? " !!!" : "");
		TRACEi(3, "repositions       :", history.rewinds);
		TRACEi(3, "last segment      :", last_segment);
	}
	if (going_offline) {
		going_offline = 0;
		ftape_offline = 1;
	}
	TRACE_EXIT;
	return result;
}

/*      IOCTL routine called by kernel-interface code
 */
int _ftape_ioctl(unsigned int command, void *arg)
{
	TRACE_FUN(8, "ftape_ioctl");
	int result = EINVAL;
	union {
		struct mtop mtop;
		struct mtget mtget;
	} krnl_arg;
	int arg_size = (command & IOCSIZE_MASK) >> IOCSIZE_SHIFT;

	/* This check will only catch arguments that are too large !
	 */
	if ((command & IOC_INOUT) && arg_size > sizeof(krnl_arg)) {
		TRACEi(1, "bad argument size:", arg_size);
		TRACE_EXIT;
		return -EINVAL;
	}
	if (command & IOC_IN) {
		int error = verify_area(VERIFY_READ, arg, arg_size);
		if (error) {
			TRACE_EXIT;
			return error;
		}
		memcpy_fromfs(&krnl_arg.mtop, arg, arg_size);
	}
	TRACEx1(5, "called with ioctl command: 0x%08x", command);
	switch (command) {
		/* cpio compatibility
		 * mtrasx and mtreset are mt extension by Hennus Bergman
		 * mtseek and mttell are mt extension by eddy olk
		 */
	case MTIOCTOP:
		TRACEx1(5, "calling MTIOCTOP command: 0x%08x", krnl_arg.mtop.mt_op);
		switch (krnl_arg.mtop.mt_op) {
		case MTNOP:
			/* gnu mt calls MTNOP before MTIOCGET to set status */
			result = 0;
			break;
		case MTRESET:
			result = ftape_reset_drive();
			init_drive_needed = 1;
			if (result < 0 || ftape_offline) {
				break;
			}
			result = ftape_seek_to_bot();
			ftape_reset_position();
			break;
		case MTREW:
		case MTOFFL:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			ftape_flush_buffers();
			ftape_update_header_segments(NULL, 1);
			result = ftape_seek_to_bot();
			ftape_reset_position();
			if (krnl_arg.mtop.mt_op == MTOFFL) {
				going_offline = 1;
				TRACE(4, "Putting tape drive offline");
			}
			result = 0;
			break;
		case MTRETEN:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			result = ftape_seek_to_eot();
			if (result >= 0) {
				result = ftape_seek_to_bot();
			}
			ftape_reset_position();
			break;
		case MTERASE:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			result = ftape_erase();
			break;
		case MTEOM:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			result = ftape_seek_eom();
			break;
		case MTFSFM:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			eof_mark = 1;	/* position ready to extend */
		case MTFSF:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			result = ftape_seek_eof(krnl_arg.mtop.mt_count);
			break;
		case MTBSFM:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			eof_mark = 1;	/* position ready to extend */
		case MTBSF:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			result = ftape_seek_eof(-krnl_arg.mtop.mt_count);
			break;
		case MTFSR:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			tracing = krnl_arg.mtop.mt_count;
			TRACEx1(2, "tracing set to %d", tracing);
			result = 0;
			break;
		case MTBSR:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
#if 0
			result = ftape_fix();
#else
			result = 0;
#endif
			break;
		case MTWEOF:
			if (ftape_offline) {
				result = -EIO;
				break;
			}
			result = ftape_weof(krnl_arg.mtop.mt_count, ftape_seg_pos, 1);
			if (result >= 0) {
				ftape_seg_pos += krnl_arg.mtop.mt_count - 1;
			}
			break;
			/* MTRASx and MTRESET are mt extension by Hennus Bergman
			 */
		case MTRAS1:
		case MTRAS2:
		case MTRAS3:
		case MTSEEK:
		case MTTELL:
		default:
			TRACEi(1, "MTIOCTOP sub-command not implemented:", krnl_arg.mtop.mt_op);
			result = -EIO;
			break;
		}
		break;
	case MTIOCGET:
		krnl_arg.mtget.mt_type = drive_type.vendor_id + 0x800000;
		krnl_arg.mtget.mt_resid = 0;	/* not implemented */
		krnl_arg.mtget.mt_dsreg = 0;	/* status register */
		krnl_arg.mtget.mt_gstat =	/* device independent status */
		    ((ftape_offline) ? 0 : GMT_ONLINE(-1L)) |
		    ((write_protected) ? GMT_WR_PROT(-1L) : 0) |
		    ((no_tape) ? GMT_DR_OPEN(-1L) : 0);
		krnl_arg.mtget.mt_erreg = ftape_last_error;	/* error register */
		result = ftape_file_no(&krnl_arg.mtget.mt_fileno,
				       &krnl_arg.mtget.mt_blkno);
		break;
	case MTIOCPOS:
		TRACE(5, "Mag tape ioctl command: MTIOCPOS");
		TRACE(1, "MTIOCPOS command not implemented");
		break;
	default:
		result = -EINVAL;
		break;
	}
	if (command & IOC_OUT) {
		int error = verify_area(VERIFY_WRITE, arg, arg_size);
		if (error) {
			TRACE_EXIT;
			return error;
		}
		memcpy_tofs(arg, &krnl_arg, arg_size);
	}
	TRACE_EXIT;
	return result;
}

void ftape_init_driver(void)
{
	drive_type.vendor_id = UNKNOWN_VENDOR;
	drive_type.speed = 0;
	drive_type.wake_up = unknown_wake_up;
	drive_type.name = "Unknown";

	timeout.seek = 650 * SECOND;
	timeout.reset = 670 * SECOND;
	timeout.rewind = 650 * SECOND;
	timeout.head_seek = 15 * SECOND;
	timeout.stop = 5 * SECOND;
	timeout.pause = 16 * SECOND;

	qic_std = -1;
	tape_len = -1;
	current_command = 0;
	current_cylinder = -1;

	segments_per_track = 102;
	segments_per_head = 1020;
	segments_per_cylinder = 4;
	tracks_per_tape = 20;
	ftape_failure = 1;
	ftape_seg_pos = 0;
	first_data_segment = -1;
	ftape_state = idle;
	no_tape = 1;
	formatted = 0;
	ftape_data_rate = 0;
	going_offline = 0;
	read_only = 0;

	init_drive_needed = 1;
	header_segment_1 = -1;
	header_segment_2 = -1;
	used_header_segment = -1;
	location.track = -1;
	location.known = 0;
	tape_running = 0;
	might_be_off_track = 1;

	ftape_new_cartridge();	/* init some tape related variables */
	ftape_init_bsm();
}
