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

 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-io.c,v $
 $Author: bas $
 *
 $Revision: 1.58 $
 $Date: 1995/05/27 08:54:21 $
 $State: Beta $
 *
 *      This file contains the general control functions
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ftape.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/ioctl.h>
#include <linux/mtio.h>

#include "tracing.h"
#include "fdc-io.h"
#include "qic117.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-write.h"
#include "ftape-read.h"
#include "ftape-eof.h"
#include "kernel-interface.h"
#include "calibr.h"

/*      Global vars.
 */
/* NOTE: sectors start numbering at 1, all others at 0 ! */
timeout_table timeout;
vendor_struct drive_type;
int qic_std;
int tape_len;
volatile int current_command;
const struct qic117_command_table qic117_cmds[] = QIC117_COMMANDS;
int might_be_off_track;

/*      Local vars.
 */
static int command_parameter = 0;
/*      command-restrictions is a table according
 *      to the QIC-117 specs specifying the state
 *      the drive status should be in at command execution.
 */
static const ftape_error ftape_errors[] = QIC117_ERRORS;
static int ftape_udelay_count;
static int ftape_udelay_time;
static const struct {
	char *text;
	int fdc_code;
	byte drive_code;
	int precomp;
} rates[4] = {

#if defined(FDC_82078SL)
	{
		"2 M", -1 /* unsupported */ , QIC_CONFIG_RATE_2000, 0
	},
#else
	{
		"2 M", fdc_data_rate_2000, QIC_CONFIG_RATE_2000, 0
	},
#endif
	{
		"1 M", fdc_data_rate_1000, QIC_CONFIG_RATE_1000, 42
	},
	{
		"500 K", fdc_data_rate_500, QIC_CONFIG_RATE_500, 125
	},
	{
		"250 K", fdc_data_rate_250, QIC_CONFIG_RATE_250, 250
	},
};
typedef enum {
	prehistoric, pre_qic117c, post_qic117b, post_qic117d
} qic_model;


void udelay(int usecs)
{
	volatile int count = (1 + (usecs * ftape_udelay_count - 1) /
			      ftape_udelay_time);
	volatile int i;

	while (count-- > 0) {
		for (i = 0; i < 20; ++i);
	}
}

int udelay_calibrate(void)
{
	return calibrate("udelay", udelay, &ftape_udelay_count, &ftape_udelay_time);
}

/*      Delay (msec) routine.
 */
void ftape_sleep(unsigned int time)
{
	TRACE_FUN(8, "ftape_sleep");
	unsigned long flags;
	int ticks = 1 + (time + MSPT - 1) / MSPT;

	/*    error in range [0..1] MSPT
	 */
	if (time < MSPT) {
		/*  Time too small for scheduler, do a busy wait ! */
		udelay(1000 * time);
	} else {
		TRACEx2(8, "%d msec, %d ticks", time, ticks);
		current->timeout = jiffies + ticks;
		current->state = TASK_INTERRUPTIBLE;
		save_flags(flags);
		sti();
		do {
			while (current->state != TASK_RUNNING) {
				schedule();
			}
			if (current->signal & ~current->blocked) {
				TRACE(1, "awoken by non-blocked signal :-(");
				break;	/* exit on signal */
			}
		} while (current->timeout > 0);
		restore_flags(flags);
	}
	TRACE_EXIT;
}

/* forward */ int ftape_report_raw_drive_status(int *status);

/*      Issue a tape command:
 *      Generate command # of step pulses.
 */
int ftape_command(int command)
{
	TRACE_FUN(8, "ftape_command");
	int result = 0;
	int track;
	int old_tracing = tracing;
	static int level = 0;
	int status = -1;

	if (++level > 5) {
		/*  This is a bug we'll want to know about.
		 */
		TRACEx1(1, "bug - recursion for command: %d", command);
		result = -EIO;
	} else if (command_parameter) {
		/*  Don't check restrictions for parameters.
		 */
		TRACEx1(5, "called with parameter = %d", command - 2);
	} else if (command <= 0 || command > NR_ITEMS(qic117_cmds)) {
		/*  This is a bug we'll want to know about too.
		 */
		TRACEx1(-1, "bug - bad command: %d", command);
		result = -EIO;
	} else {
		/*  disable logging and restriction check for some commands,
		 *  check all other commands that have a prescribed starting status.
		 */
		if (command == QIC_REPORT_DRIVE_STATUS) {
			TRACE(8, "report drive status called");
			tracing = 0;
		} else if (command == QIC_REPORT_NEXT_BIT) {
			tracing = 0;
		} else {
			TRACEx1(5, "%s", qic117_cmds[command].name);
			/*  A new motion command during an uninterruptible (motion)
			 *  command requires a ready status before the new command
			 *  can be issued. Otherwise a new motion command needs to
			 *  be checked against required status.
			 */
			if (qic117_cmds[command].cmd_type == motion &&
			    qic117_cmds[current_command].non_intr) {
				ftape_report_raw_drive_status(&status);
				if ((status & QIC_STATUS_READY) == 0) {
					TRACEx2(4, "motion cmd (%d) during non-intr cmd (%d)",
						command, current_command);
					TRACE(4, "waiting until drive gets ready");
					ftape_ready_wait(timeout.seek, &status);
				}
			}
			if (qic117_cmds[command].mask != 0) {
				byte difference;

				/*  Some commands do require a certain status:
				 */
				if (status == -1) {	/* not yet set */
					ftape_report_raw_drive_status(&status);
				}
				difference = ((status ^ qic117_cmds[command].state) &
					      qic117_cmds[command].mask);
				/*  Wait until the drive gets ready. This may last forever
				 *  if the drive never gets ready...
				 */
				while ((difference & QIC_STATUS_READY) != 0) {
					TRACEx1(4, "command %d issued while not ready", command);
					TRACE(4, "waiting until drive gets ready");
					ftape_ready_wait(timeout.seek, &status);
					difference = ((status ^ qic117_cmds[command].state) &
					      qic117_cmds[command].mask);
					/*  Bail out on signal !
					 */
					if (current->signal & _DONT_BLOCK) {
						result = -EINTR;
						break;
					}
				}
				while (result == 0 && (difference & QIC_STATUS_ERROR) != 0) {
					int err;
					int cmd;

					TRACEx1(4, "command %d issued while error pending", command);
					TRACE(4, "clearing error status");
					ftape_report_error(&err, &cmd, 1);
					ftape_report_raw_drive_status(&status);
					difference = ((status ^ qic117_cmds[command].state) &
					      qic117_cmds[command].mask);
					/*  Bail out on fatal signal !
					 */
					if (current->signal & _DONT_BLOCK) {
						result = -EINTR;
						break;
					}
				}
				if (result == 0 && difference) {
					/*  Any remaining difference can't be solved here.
					 */
					if (difference & (QIC_STATUS_CARTRIDGE_PRESENT |
					       QIC_STATUS_NEW_CARTRIDGE |
						QIC_STATUS_REFERENCED)) {
						TRACE(1, "Fatal: tape removed or reinserted !");
						ftape_failure = 1;
					} else {
						TRACEx2(1, "wrong state: 0x%02x should be: 0x%02x",
							status & qic117_cmds[command].mask,
							qic117_cmds[command].state);
					}
					result = -EIO;
				}
				if (~status & QIC_STATUS_READY & qic117_cmds[command].mask) {
					TRACE(1, "Bad: still busy!");
					result = -EBUSY;
				}
			}
		}
	}
	tracing = old_tracing;
	/*  Now all conditions are met or result is < 0.
	 */
	if (result >= 0) {
		/*  Always wait for a command_timeout period to separate
		 *  individuals commands and/or parameters.
		 */
		ftape_sleep(3 * MILLISECOND);
		/*  Keep cylinder nr within range, step towards home if possible.
		 */
		if (current_cylinder >= command) {
			track = current_cylinder - command;
		} else {
			track = current_cylinder + command;
		}
		result = fdc_seek(track);
		/*  position is no longer valid after any of these commands
		 *  have completed.
		 */
		if (qic117_cmds[command].cmd_type == motion &&
		    command != QIC_LOGICAL_FORWARD && command != QIC_STOP_TAPE) {
			location.known = 0;
		}
		command_parameter = 0;	/* always turned off for next command */
		current_command = command;
	}
	--level;
	TRACE_EXIT;
	return result;
}

/*      Send a tape command parameter:
 *      Generates command # of step pulses.
 *      Skips tape-status call !
 */
int ftape_parameter(int command)
{
	command_parameter = 1;
	return ftape_command(command + 2);
}

/*      Wait for the drive to get ready.
 *      timeout time in milli-seconds
 *      Returned status is valid if result != -EIO
 */
int ftape_ready_wait(int timeout, int *status)
{
	TRACE_FUN(8, "ftape_ready_wait");
	int result;
	unsigned long t0;
	const int poll_delay = 100 * MILLISECOND;

	for (;;) {
		t0 = jiffies;
		result = ftape_report_raw_drive_status(status);
		if (result < 0) {
			TRACE(1, "ftape_report_raw_drive_status failed");
			result = -EIO;
			break;
		}
		if (*status & QIC_STATUS_READY) {
			result = 0;
			break;
		}
		if (timeout >= 0) {
			/* this will fail when jiffies wraps around about
			 * once every year :-)
			 */
			timeout -= ((jiffies - t0) * SECOND) / HZ;
			if (timeout <= 0) {
				TRACE(1, "timeout");
				result = -ETIME;
				break;
			}
			ftape_sleep(poll_delay);
			timeout -= poll_delay;
		} else {
			ftape_sleep(poll_delay);
		}
		if (current->signal & _NEVER_BLOCK) {
			TRACE(1, "interrupted by fatal signal");
			result = -EINTR;
			break;	/* exit on signal */
		}
	}
	TRACE_EXIT;
	return result;
}

/*      Issue command and wait up to timeout seconds for drive ready
 */
int ftape_command_wait(int command, int timeout, int *status)
{
	TRACE_FUN(8, "ftape_command_wait");
	int result;

	/* Drive should be ready, issue command
	 */
	result = ftape_command(command);
	if (result >= 0) {
		result = ftape_ready_wait(timeout, status);
	}
	TRACE_EXIT;
	return result;
}

int ftape_parameter_wait(int command, int timeout, int *status)
{
	TRACE_FUN(8, "ftape_parameter_wait");
	int result;

	/* Drive should be ready, issue command
	 */
	result = ftape_parameter(command);
	if (result >= 0) {
		result = ftape_ready_wait(timeout, status);
	}
	TRACE_EXIT;
	return result;
}

/*--------------------------------------------------------------------------
 *      Report operations
 */

/* Query the drive about its status.  The command is sent and
   result_length bits of status are returned (2 extra bits are read
   for start and stop). */

static int ftape_report_operation(int *status, int command, int result_length)
{
	TRACE_FUN(8, "ftape_report_operation");
	int i, st3;
	int result;
	unsigned int t0, t1, dt;

	result = ftape_command(command);
	if (result < 0) {
		TRACE(1, "ftape_command failed");
		TRACE_EXIT;
		return result;
	}
	t0 = timestamp();
	dt = 0;
	i = 0;
	do {
		++i;
		ftape_sleep(3 * MILLISECOND);	/* see remark below */
		result = fdc_sense_drive_status(&st3);
		if (result < 0) {
			TRACE(1, "fdc_sense_drive_status failed");
			TRACE_EXIT;
			return result;
		}
		/*  Calculate time difference every iteration because timer may
		 *  wrap around (but only one !) and timediff will account for this.
		 *  Note that the sleep above must be < 1/HZ or we'll lose ticks !
		 */
		t1 = timestamp();
		dt += timediff(t0, t1);
		t0 = t1;
		/*  Ack should be asserted within Ttimout + Tack = 6 msec.
		 *  Looks like some drives fail to do this so extend this
		 *  period to 300 msec.
		 */
	} while (!(st3 & ST3_TRACK_0) && dt < 300000);
	if (st3 & ST3_TRACK_0) {
		/*  dt may be larger than expected because of other tasks
		 *  scheduled while we were sleeping.
		 */
		if (i > 1 && dt > 6000) {
			TRACEx2(1, "Acknowledge after %u msec. (%i iter)", dt / 1000, i);
		}
	} else {
		TRACEx2(1, "No acknowledge after %u msec. (%i iter)", dt / 1000, i);
		TRACE(1, "timeout on Acknowledge");
		TRACE_EXIT;
		return -EIO;
	}
	*status = 0;
	for (i = 0; i < result_length + 1; i++) {
		result = ftape_command(QIC_REPORT_NEXT_BIT);
		if (result < 0) {
			TRACE(1, "report next bit failed");
			TRACE_EXIT;
			return result;
		}
#if 1
		/*  fdc_seek does interrupt wait, so why should we ?
		 *  (it will only fail causing fdc to be reset...)
		 *  It's only purpose may be the delay, we'll have to find out!
		 */
#else
		fdc_interrupt_wait(25 * MILLISECOND);	/* fails only if hw fails */
#endif
		result = fdc_sense_drive_status(&st3);
		if (result < 0) {
			TRACE(1, "fdc_sense_drive_status (2) failed");
			TRACE_EXIT;
			return result;
		}
		if (i < result_length) {
			*status |= ((st3 & ST3_TRACK_0) ? 1 : 0) << i;
		} else {
			if ((st3 & ST3_TRACK_0) == 0) {
				TRACE(1, "missing status stop bit");
				TRACE_EXIT;
				return -EIO;
			}
		}
	}
	/* this command will put track zero and index back into normal state */
	result = ftape_command(QIC_REPORT_NEXT_BIT);
	TRACE_EXIT;
	return 0;
}

/* Report the current drive status. */

int ftape_report_raw_drive_status(int *status)
{
	TRACE_FUN(8, "ftape_report_raw_drive_status");
	int result;
	int count = 0;

	do {
		result = ftape_report_operation(status, QIC_REPORT_DRIVE_STATUS, 8);
	} while (result < 0 && ++count <= 3);
	if (result < 0) {
		TRACE(1, "report_operation failed");
		result = -EIO;
	} else if (*status & QIC_STATUS_READY) {
		current_command = 0;	/* completed */
	}
	TRACE_EXIT;
	return result;
}

int ftape_report_drive_status(int *status)
{
	TRACE_FUN(8, "ftape_report_drive_status");
	int result;

	result = ftape_report_raw_drive_status(status);
	if (result < 0) {
		TRACE(1, "ftape_report_raw_drive_status failed");
		TRACE_EXIT;
		return result;
	}
	if (*status & QIC_STATUS_NEW_CARTRIDGE ||
	    !(*status & QIC_STATUS_CARTRIDGE_PRESENT)) {
		ftape_failure = 1;	/* will inhibit further operations */
		TRACE_EXIT;
		return -EIO;
	}
	if (*status & QIC_STATUS_READY && *status & QIC_STATUS_ERROR) {
		/*  Let caller handle all errors */
		TRACE(2, "warning: error status set!");
		result = 1;
	}
	TRACE_EXIT;
	return result;
}

int ftape_report_error(int *error, int *command, int report)
{
	TRACE_FUN(8, "ftape_report_error");
	int code;
	int result;

	result = ftape_report_operation(&code, QIC_REPORT_ERROR_CODE, 16);
	if (result < 0) {
		result = -EIO;
	} else {
		*error = code & 0xff;
		*command = (code >> 8) & 0xff;
		if (report) {
			if (*error != 0) {
				TRACEi(3, "errorcode:", *error);
			} else {
				TRACE(3, "No error");
			}
		}
		if (report && *error != 0 && tracing > 3) {
			if (*error >= 0 && *error < NR_ITEMS(ftape_errors)) {
				TRACEx1(-1, "%sFatal ERROR:",
					(ftape_errors[*error].fatal ? "" : "Non-"));
				TRACEx1(-1, "%s ...", ftape_errors[*error].message);
			} else {
				TRACE(-1, "Unknown ERROR !");
			}
			if (*command >= 0 && *command < NR_ITEMS(qic117_cmds) &&
			    qic117_cmds[*command].name != NULL) {
				TRACEx1(-1, "... caused by command \'%s\'",
					qic117_cmds[*command].name);
			} else {
				TRACEi(-1, "... caused by unknown command", *command);
			}
		}
	}
	TRACE_EXIT;
	return result;
}

int ftape_in_error_state(int status)
{
	TRACE_FUN(8, "ftape_in_error_state");
	int result = 0;

	if ((status & QIC_STATUS_READY) && (status & QIC_STATUS_ERROR)) {
		TRACE(2, "warning: error status set!");
		result = 1;
	}
	TRACE_EXIT;
	return result;
}

static int ftape_report_configuration(qic_model * model, int *rate,
				      int *qic_std, int *tape_len)
{
	int result;
	int config;
	int status;

	TRACE_FUN(8, "ftape_report_configuration");
	result = ftape_report_operation(&config, QIC_REPORT_DRIVE_CONFIGURATION, 8);
	if (result < 0) {
		*model = prehistoric;
		*rate = QIC_CONFIG_RATE_500;
		*qic_std = QIC_TAPE_QIC40;
		*tape_len = 205;
		result = 0;
	} else {
		*rate = (config & QIC_CONFIG_RATE_MASK) >> QIC_CONFIG_RATE_SHIFT;
		result = ftape_report_operation(&status, QIC_REPORT_TAPE_STATUS, 8);
		if (result < 0) {
			/* pre- QIC117 rev C spec. drive, QIC_CONFIG_80 bit is valid.
			 */
			*qic_std = (config & QIC_CONFIG_80) ? QIC_TAPE_QIC80 : QIC_TAPE_QIC40;
			*tape_len = (config & QIC_CONFIG_LONG) ? 307 : 205;
			*model = pre_qic117c;
			result = 0;
		} else {
			*model = post_qic117b;
			TRACEx1(8, "report tape status result = %02x", status);
			/* post- QIC117 rev C spec. drive, QIC_CONFIG_80 bit is invalid.
			 */
			switch (status & QIC_TAPE_STD_MASK) {
			case QIC_TAPE_QIC40:
			case QIC_TAPE_QIC80:
			case QIC_TAPE_QIC3020:
			case QIC_TAPE_QIC3010:
				*qic_std = status & QIC_TAPE_STD_MASK;
				break;
			default:
				*qic_std = -1;
				break;
			}
			switch (status & QIC_TAPE_LEN_MASK) {
			case QIC_TAPE_205FT:
				/* Unfortunately the new QIC-117 rev G standard shows
				 * no way to discriminate between 205 and 425 ft tapes.
				 * The obvious way seems not to be used: the QIC_CONFIG_LONG
				 * bit isn't used for this (on all drives ?).
				 */
				if (config & QIC_CONFIG_LONG) {
					*tape_len = 425;	/* will this ever execute ??? */
				} else {
					*tape_len = 0;	/* length unknown: 205 or 425 ft. */
				}
				break;
			case QIC_TAPE_307FT:
				*tape_len = 307;
				break;
			case QIC_TAPE_400FT:
				/*
				 * Trouble! Iomega Ditto 800 and Conner TST800R drives reports
				 * 400ft for 750ft tapes. Yuck, yuck, yuck.  Since the value
				 * is only used to compute a timeout value, the largest of the
				 * two is used.
				 */
				*tape_len = 750;	/* either 400 or 750 ft. */
				break;
			case QIC_TAPE_1100FT:
				*tape_len = 1100;
				break;
			case QIC_TAPE_FLEX:
				*tape_len = 0;
				break;
			default:
				*tape_len = -1;
				break;
			}
			if (*qic_std == -1 || *tape_len == -1) {
				TRACE(2, "post qic-117b spec drive with unknown tape");
				result = -EIO;
			} else {
				result = 0;
			}
		}
	}
	TRACE_EXIT;
	return (result < 0) ? -EIO : 0;
}

int ftape_report_rom_version(int *version)
{
	int result;

	result = ftape_report_operation(version, QIC_REPORT_ROM_VERSION, 8);
	return (result < 0) ? -EIO : 0;
}

int ftape_report_signature(int *signature)
{
	int result;

	result = ftape_command(28);
	result = ftape_report_operation(signature, 9, 8);
	result = ftape_command(30);
	return (result < 0) ? -EIO : 0;
}

void ftape_report_vendor_id(unsigned int *id)
{
	TRACE_FUN(8, "ftape_report_vendor_id");
	int result;

	/*
	 *    We'll try to get a vendor id from the drive.
	 *    First according to the QIC-117 spec, a 16-bit id is requested.
	 *    If that fails we'll try an 8-bit version, otherwise we'll try
	 *    an undocumented query.
	 */
	result = ftape_report_operation((int *) id, QIC_REPORT_VENDOR_ID, 16);
	if (result < 0) {
		result = ftape_report_operation((int *) id, QIC_REPORT_VENDOR_ID, 8);
		if (result < 0) {
			/*  The following is an undocumented call found in the CMS code.
			 */
			result = ftape_report_operation((int *) id, 24, 8);
			if (result < 0) {
				*id = UNKNOWN_VENDOR;
			} else {
				TRACEx1(4, "got old 8 bit id: %04x", *id);
				*id |= 0x20000;
			}
		} else {
			TRACEx1(4, "got 8 bit id: %04x", *id);
			*id |= 0x10000;
		}
	} else {
		TRACEx1(4, "got 16 bit id: %04x", *id);
	}
	if (*id == 0x0047) {
		int version;
		int sign;

		result = ftape_report_rom_version(&version);
		if (result < 0) {
			TRACE(-1, "report rom version failed");
			TRACE_EXIT;
			return;
		}
		TRACEx1(4, "CMS rom version: %d", version);
		ftape_command(QIC_ENTER_DIAGNOSTIC_1);
		ftape_command(QIC_ENTER_DIAGNOSTIC_1);
		result = ftape_report_operation(&sign, 9, 8);
		if (result < 0) {
			int error, command;

			ftape_report_error(&error, &command, 1);
			ftape_command(QIC_ENTER_PRIMARY_MODE);
			TRACE_EXIT;
			return;	/* faalt hier ! */
		} else {
			TRACEx1(4, "CMS signature: %02x", sign);
		}
		if (sign == 0xa5) {
			result = ftape_report_operation(&sign, 37, 8);
			if (result < 0) {
				if (version >= 63) {
					*id = 0x8880;
					TRACE(4, "This is an Iomega drive !");
				} else {
					*id = 0x0047;
					TRACE(4, "This is a real CMS drive !");
				}
			} else {
				*id = 0x0047;
				TRACEx1(4, "CMS status: %d", sign);
			}
		} else {
			*id = UNKNOWN_VENDOR;
		}
		ftape_command(QIC_ENTER_PRIMARY_MODE);
	}
	TRACE_EXIT;
}

void ftape_set_rate_test(int *supported)
{
	TRACE_FUN(8, "ftape_set_rate_test");
	int error;
	int command;
	int i;
	int result;
	int status;

	/*  Check if the drive does support the select rate command by testing
	 *  all different settings.
	 *  If any one is accepted we assume the command is supported, else not.
	 */
	*supported = 0;
	for (i = 0; i < NR_ITEMS(rates); ++i) {
		result = ftape_command(QIC_SELECT_RATE);
		if (result >= 0) {
			result = ftape_parameter_wait(rates[i].drive_code,
						    1 * SECOND, &status);
			if (result >= 0) {
				if (status & QIC_STATUS_ERROR) {
					result = ftape_report_error(&error, &command, 0);
				} else {
					*supported = 1;		/* did accept a request */
				}
			}
		}
	}
	TRACEx1(4, "Select Rate command is%s supported",
		*supported ? "" : " not");
	TRACE_EXIT;
}

int ftape_set_data_rate(int new_rate)
{
	TRACE_FUN(8, "ftape_set_data_rate");
	int status;
	int result;
	int data_rate;
	qic_model model;
	int supported;
	static int first_time = 1;

	if (first_time) {
		ftape_set_rate_test(&supported);
		first_time = 0;
	}
	if (rates[new_rate].fdc_code == -1) {
		TRACEx1(4, "%sb/s data rate not supported by the fdc",
			rates[new_rate].text);
		result = -EINVAL;
	} else {
		int error = 0;
		int command;

		result = ftape_command(QIC_SELECT_RATE);
		if (result >= 0) {
			result = ftape_parameter_wait(rates[new_rate].drive_code,
						    1 * SECOND, &status);
			result = ftape_report_raw_drive_status(&status);
			if (result >= 0 && (status & QIC_STATUS_ERROR)) {
				result = ftape_report_error(&error, &command, 0);
				if (result >= 0 && supported &&
				    error == 31 && command == QIC_SELECT_RATE) {
					result = -EINVAL;
				}
			}
		}
		if (result >= 0) {
			result = ftape_report_configuration(&model, &data_rate,
						    &qic_std, &tape_len);
			if (result >= 0 && data_rate != rates[new_rate].drive_code) {
				result = -EINVAL;
			}
		}
		if (result < 0) {
			TRACEx1(4, "could not set %sb/s data rate", rates[new_rate].text);
		} else {
			TRACEx2(2, "%s drive @ %sb/s",
				(model == prehistoric) ? "prehistoric" :
				((model == pre_qic117c) ? "pre QIC-117C" :
				 ((model == post_qic117b) ? "post QIC-117B" : "post QIC-117D")),
				rates[new_rate].text);
			if (tape_len == 0) {
				TRACEx1(2, "unknown length QIC-%s tape",
				     (qic_std == QIC_TAPE_QIC40) ? "40" :
				    ((qic_std == QIC_TAPE_QIC80) ? "80" :
				     ((qic_std == QIC_TAPE_QIC3010) ? "3010" : "3020")));
			} else {
				TRACEx2(2, "%d ft. QIC-%s tape",
					tape_len,
				     (qic_std == QIC_TAPE_QIC40) ? "40" :
				    ((qic_std == QIC_TAPE_QIC80) ? "80" :
				     ((qic_std == QIC_TAPE_QIC3010) ? "3010" : "3020")));
			}
			/*
			 *  Set data rate and write precompensation as specified:
			 *
			 *            |  QIC-40/80  | QIC-3010/3020
			 *   rate     |   precomp   |    precomp
			 *  ----------+-------------+--------------
			 *  250 Kbps. |   250 ns.   |     0 ns.
			 *  500 Kbps. |   125 ns.   |     0 ns.
			 *    1 Mbps. |    42 ns.   |     0 ns.
			 *    2 Mbps  |      N/A    |     0 ns.
			 */
			if (qic_std == QIC_TAPE_QIC40 || qic_std == QIC_TAPE_QIC80) {
				fdc_set_write_precomp(rates[new_rate].precomp);
			} else {
				fdc_set_write_precomp(0);
			}
			fdc_set_data_rate(rates[new_rate].fdc_code);
			ftape_data_rate = new_rate;	/* store rate set */
		}
	}
	if (result < 0 && result != -EINVAL) {
		result = -EIO;
	}
	TRACE_EXIT;
	return result;
}

/*      Seek the head to the specified track.
 */
int ftape_seek_head_to_track(int track)
{
	TRACE_FUN(8, "ftape_seek_head_to_track");
	int status;
	int result;

	location.track = -1;	/* remains set in case of error */
	if (track < 0 || track >= tracks_per_tape) {
		TRACE(-1, "track out of bounds");
		result = -EINVAL;
	} else {
		TRACEx1(5, "seeking track %d", track);
		result = ftape_command(QIC_SEEK_HEAD_TO_TRACK);
		if (result < 0) {
			TRACE(1, "ftape_command failed");
		} else {
			result = ftape_parameter_wait(track, timeout.head_seek, &status);
			if (result < 0) {
				TRACE(1, "ftape_parameter_wait failed");
			} else {
				location.track = track;
				might_be_off_track = 0;
			}
		}
	}
	TRACE_EXIT;
	return result;
}

int ftape_wakeup_drive(wake_up_types method)
{
	TRACE_FUN(8, "ftape_wakeup_drive");
	int result;
	int status;
	int motor_on = 0;

	switch (method) {
	case wake_up_colorado:
		result = ftape_command(QIC_PHANTOM_SELECT);
		if (result == 0) {
			result = ftape_parameter( /* unit */ 0);
		}
		break;
	case wake_up_mountain:
		result = ftape_command(QIC_SOFT_SELECT);
		if (result == 0) {
			ftape_sleep(MILLISECOND);	/* NEEDED */
			result = ftape_parameter(18);
		}
		break;
	case wake_up_insight:
		ftape_sleep(100 * MILLISECOND);
		motor_on = 1;
		fdc_motor(motor_on);	/* enable is done by motor-on */
	case no_wake_up:
		result = 0;
		break;
	default:
		result = -ENODEV;	/* unknown wakeup method */
	}
	/*  If wakeup succeeded we shouldn't get an error here..
	 */
	if (result == 0) {
		result = ftape_report_raw_drive_status(&status);
		if (result < 0 && motor_on) {
			fdc_motor(0);	/* motor off if failed */
		}
	}
	TRACE_EXIT;
	return result;
}

int ftape_put_drive_to_sleep(vendor_struct drive_type)
{
	TRACE_FUN(8, "ftape_put_drive_to_sleep");
	int result;

	switch (drive_type.wake_up) {
	case wake_up_colorado:
		result = ftape_command(QIC_PHANTOM_DESELECT);
		break;
	case wake_up_mountain:
		result = ftape_command(QIC_SOFT_DESELECT);
		break;
	case wake_up_insight:
		fdc_motor(0);	/* enable is done by motor-on */
	case no_wake_up:	/* no wakeup / no sleep ! */
		result = 0;
		break;
	default:
		result = -ENODEV;	/* unknown wakeup method */
	}
	TRACE_EXIT;
	return result;
}

int ftape_reset_drive(void)
{
	TRACE_FUN(8, "ftape_reset_drive");
	int result = 0;
	int status;
	int err_code;
	int err_command;
	int i;

	/*    We want to re-establish contact with our drive.
	 *    Fire a number of reset commands (single step pulses)
	 *    and pray for success.
	 */
	for (i = 0; i < 2; ++i) {
		TRACE(5, "Resetting fdc");
		fdc_reset();
		ftape_sleep(10 * MILLISECOND);
		TRACE(5, "Reset command to drive");
		result = ftape_command(QIC_RESET);
		if (result == 0) {
			ftape_sleep(1 * SECOND);	/* drive not accessible during 1 second */
			TRACE(5, "Re-selecting drive");
			/*  Strange, the QIC-117 specs don't mention this but the
			 *  drive gets deselected after a soft reset !
			 *  So we need to enable it again.
			 */
			result = ftape_wakeup_drive(drive_type.wake_up);
			if (result < 0) {
				TRACE(1, "Wakeup failed !");
			}
			TRACE(5, "Waiting until drive gets ready");
			result = ftape_ready_wait(timeout.reset, &status);
			if (result == 0 && status & QIC_STATUS_ERROR) {
				result = ftape_report_error(&err_code, &err_command, 1);
				if (result == 0 && err_code == 27) {
					/* Okay, drive saw reset command and responded as it should
					 */
					break;
				} else {
					result = -EIO;
				}
			} else {
				result = -EIO;
			}
		}
		if (current->signal & _DONT_BLOCK) {
			TRACE(1, "aborted by non-blockable signal");
			result = -EINTR;
			break;	/* exit on signal */
		}
	}
	if (result != 0) {
		TRACE(1, "General failure to reset tape drive");
	} else {
		/*  Restore correct settings
		 */
		ftape_set_data_rate(ftape_data_rate);	/* keep original rate */
	}
	TRACE_EXIT;
	return result;
}
