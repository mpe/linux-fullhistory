/* Yo, Emacs! we're -*- Linux-C -*-
 *
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
 *      This file contains the low-level floppy disk interface code
 *      for the QIC-40/80 tape streamer device driver.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/ftape.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

#include "tracing.h"
#include "fdc-io.h"
#include "fdc-isr.h"
#include "ftape-io.h"
#include "ftape-rw.h"
#include "calibr.h"
#include "fc-10.h"
#include "qic117.h"


/*      Global vars.
 */
int ftape_unit = -1;
int ftape_motor = 0;
int current_cylinder = -1;
fdc_mode_enum fdc_mode = fdc_idle;
fdc_config_info fdc = {0};

/*      Local vars.
 */
static int fdc_calibr_count;
static int fdc_calibr_time;
static int fdc_confused = 0;
static int fdc_status;
volatile byte fdc_head;		/* FDC head */
volatile byte fdc_cyl;		/* FDC track */
volatile byte fdc_sect;		/* FDC sector */
static int fdc_data_rate = 0;	/* default rate = 500 Kbps */
static int fdc_seek_rate = 14;	/* default rate = 2 msec @ 500 Kbps */
static void (*do_ftape) (void);
static int fdc_fifo_state;	/* original fifo setting - fifo enabled */
static int fdc_fifo_thr;	/* original fifo setting - threshold */
static int fdc_lock_state;	/* original lock setting - locked */
static int fdc_fifo_locked = 0;	/* has fifo && lock set ? */
static byte fdc_precomp = 0;	/* sets fdc to default precomp. value */
static byte fdc_drv_spec[4];	/* drive specification bytes for i82078 */
static int perpend_mode;	/* true if fdc is in perpendicular mode */

static char ftape_id[] = "ftape"; /* used by request irq and free irq */

void fdc_catch_stray_interrupts(unsigned count)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (count == 0) {
		expected_stray_interrupts = 0;
	} else {
		expected_stray_interrupts += count;
	}
	restore_flags(flags);
}

/*  Wait during a timeout period for a given FDC status.
 *  If usecs == 0 then just test status, else wait at least for usecs.
 *  Returns -ETIME on timeout. Function must be calibrated first !
 */
int fdc_wait(int usecs, byte mask, byte state)
{
	int count_1 = (fdc_calibr_count * usecs - 1) / fdc_calibr_time;

	do {
		fdc_status = inb_p(fdc.msr);
		if ((fdc_status & mask) == state) {
			return 0;
		}
	} while (count_1-- >= 0);
	return -ETIME;
}

int fdc_ready_wait(int usecs)
{
	return fdc_wait(usecs, FDC_DATA_READY, FDC_DATA_READY);
}

static void fdc_usec_wait(int usecs)
{
	fdc_wait(usecs, 0, 1);	/* will always timeout ! */
}

int fdc_ready_out_wait(int usecs)
{
	fdc_usec_wait(RQM_DELAY);	/* wait for valid RQM status */
	return fdc_wait(usecs, FDC_DATA_OUT_READY, FDC_DATA_OUT_READY);
}

int fdc_ready_in_wait(int usecs)
{
	fdc_usec_wait(RQM_DELAY);	/* wait for valid RQM status */
	return fdc_wait(usecs, FDC_DATA_OUT_READY, FDC_DATA_IN_READY);
}

int fdc_wait_calibrate(void)
{
	return calibrate("fdc_wait",
		     fdc_usec_wait, &fdc_calibr_count, &fdc_calibr_time);
}

/*  Wait for a (short) while for the FDC to become ready
 *  and transfer the next command byte.
 *  Return -ETIME on timeout on getting ready (depends on hardware!).
 */
int fdc_write(byte data)
{
	fdc_usec_wait(RQM_DELAY);	/* wait for valid RQM status */
	if (fdc_wait(150, FDC_DATA_READY_MASK, FDC_DATA_IN_READY) < 0) {
		return -ETIME;
	} else {
		outb(data, fdc.fifo);
		return 0;
	}
}

/*  Wait for a (short) while for the FDC to become ready
 *  and transfer the next result byte.
 *  Return -ETIME if timeout on getting ready (depends on hardware!).
 */
int fdc_read(byte * data)
{
	fdc_usec_wait(RQM_DELAY);	/* wait for valid RQM status */
	if (fdc_wait(150, FDC_DATA_READY_MASK, FDC_DATA_OUT_READY) < 0) {
		return -ETIME;
	} else {
		*data = inb(fdc.fifo);
		return 0;
	}
}

/*  Output a cmd_len long command string to the FDC.
 *  The FDC should be ready to receive a new command or
 *  an error (EBUSY) will occur.
 */
int fdc_command(byte * cmd_data, int cmd_len)
{
	TRACE_FUN(8, "fdc_command");
	int result = 0;
	unsigned long flags;
	int count = cmd_len;

	fdc_usec_wait(RQM_DELAY);	/* wait for valid RQM status */
	save_flags(flags);
	cli();
	fdc_status = inb(fdc.msr);
	if ((fdc_status & FDC_DATA_READY_MASK) == FDC_DATA_IN_READY) {
		int retry = 0;
		fdc_mode = *cmd_data;	/* used by isr */
		interrupt_seen = 0;
		while (count) {
			result = fdc_write(*cmd_data);
			if (result < 0) {
				TRACEx3(6, "fdc_mode = %02x, status = %02x at index %d",
					(int) fdc_mode, (int) fdc_status, cmd_len - count);
				if (++retry <= 3) {
					TRACE(2, "fdc_write timeout, retry");
				} else {
					TRACE(1, "fdc_write timeout, fatal");
					fdc_confused = 1;
					/* recover ??? */
					break;
				}
			} else {
				--count;
				++cmd_data;
			}
		}
	} else {
		TRACE(1, "fdc not ready");
		result = -EBUSY;
	}
	restore_flags(flags);
	TRACE_EXIT;
	return result;
}

/*  Input a res_len long result string from the FDC.
 *  The FDC should be ready to send the result or an error
 *  (EBUSY) will occur.
 */
int fdc_result(byte * res_data, int res_len)
{
	TRACE_FUN(8, "fdc_result");
	int result = 0;
	unsigned long flags;
	int count = res_len;

	save_flags(flags);
	cli();
	fdc_status = inb(fdc.msr);
	if ((fdc_status & FDC_DATA_READY_MASK) == FDC_DATA_OUT_READY) {
		int retry = 0;
		while (count) {
			if (!(fdc_status & FDC_BUSY)) {
				TRACE(1, "premature end of result phase");
			}
			result = fdc_read(res_data);
			if (result < 0) {
				TRACEx3(6, "fdc_mode = %02x, status = %02x at index %d",
					(int) fdc_mode, (int) fdc_status, res_len - count);
				if (++retry <= 3) {
					TRACE(2, "fdc_read timeout, retry");
				} else {
					TRACE(1, "fdc_read timeout, fatal");
					fdc_confused = 1;
					/* recover ??? */
					break;
				}
			} else {
				--count;
				++res_data;
			}
		}
	} else {
		TRACE(1, "fdc not ready");
		result = -EBUSY;
	}
	restore_flags(flags);
	fdc_usec_wait(RQM_DELAY);	/* allow FDC to negate BSY */
	TRACE_EXIT;
	return result;
}

/*      Handle command and result phases for
 *      commands without data phase.
 */
int fdc_issue_command(byte * out_data, int out_count,
		      byte * in_data, int in_count)
{
	TRACE_FUN(8, "fdc_issue_command");
	int result;
	int t0, t1;

	if (out_count > 0) {
		result = fdc_command(out_data, out_count);
		if (result < 0) {
			TRACE(1, "fdc_command failed");
			TRACE_EXIT;
			return result;
		}
	}
	/* will take 24 - 30 usec for fdc_sense_drive_status and
	 * fdc_sense_interrupt_status commands.
	 *    35 fails sometimes (5/9/93 SJL)
	 * On a loaded system it incidentally takes longer than
	 * this for the fdc to get ready ! ?????? WHY ??????
	 * So until we know what's going on use a very long timeout.
	 */
	t0 = timestamp();
	result = fdc_ready_out_wait(500 /* usec */ );
	t1 = timestamp();
	if (result < 0) {
		TRACEi(1, "fdc_ready_out_wait failed after:", timediff(t0, t1));
		TRACE_EXIT;
		return result;
	}
	if (in_count > 0) {
		result = fdc_result(in_data, in_count);
		if (result < 0) {
			TRACE(1, "result phase aborted");
			TRACE_EXIT;
			return result;
		}
	}
	TRACE_EXIT;
	return 0;
}

/*      Wait for FDC interrupt with timeout.
 *      Signals are blocked so the wait will not be aborted.
 *      Note: interrupts must be enabled ! (23/05/93 SJL)
 */
int fdc_interrupt_wait(int time)
{
	TRACE_FUN(8, "fdc_interrupt_wait");
	struct wait_queue wait =
	{current, NULL};
	int result = -ETIME;
	int need_cleanup = 0;
	int current_blocked = current->blocked;
	static int resetting = 0;

	if (wait_intr) {
		TRACE(1, "error: nested call");
		return -EIO;	/* return error... */
	}
	if (interrupt_seen == 0) {
		/* timeout time will be between 0 and MSPT milliseconds too long !
		 */
		current->timeout = jiffies + 1 + (time + MSPT - 1) / MSPT;
		current->state = TASK_INTERRUPTIBLE;
		current->blocked = _BLOCK_ALL;
		add_wait_queue(&wait_intr, &wait);
		do {
			schedule();	/* sets TASK_RUNNING on timeout */
		} while (!interrupt_seen && current->state != TASK_RUNNING);
		current->blocked = current_blocked;	/* restore */
		remove_wait_queue(&wait_intr, &wait);
		if (interrupt_seen) {
			current->timeout = 0;	/* interrupt hasn't cleared this */
			result = 0;
		} else {
#if 1
/*** remove me when sure this doesn't happen ***/
			if (current->timeout > 0) {
				TRACE(-1, "*** BUG: unexpected schedule exit ***");
				if (current->signal & ~current->blocked) {
					TRACE(4, "caused by signal ?");
				}
			}
#endif
			if (current->signal & ~current->blocked) {
				result = -EINTR;
			} else {
				result = -ETIME;
			}
			need_cleanup = 1;	/* missing interrupt, reset fdc. */
		}
	} else {
		result = 0;
	}
	/*  In first instance, next statement seems unnecessary since
	 *  it will be cleared in fdc_command. However, a small part of
	 *  the software seems to rely on this being cleared here
	 *  (ftape_close might fail) so stick to it until things get fixed !
	 */
	interrupt_seen = 0;	/* clear for next call */

	if (need_cleanup & !resetting) {
		resetting = 1;	/* break infinite recursion if reset fails */
		TRACE(8, "cleanup reset");
		fdc_reset();
		resetting = 0;
	}
	TRACE_EXIT;
	return result;
}

/*      Start/stop drive motor. Enable DMA mode.
 */
void fdc_motor(int motor)
{
	TRACE_FUN(8, "fdc_motor");
	int unit = FTAPE_UNIT;
	int data = unit | FDC_RESET_NOT | FDC_DMA_MODE;

	ftape_motor = motor;
	if (ftape_motor) {
		data |= FDC_MOTOR_0 << unit;
		TRACEx1(4, "turning motor %d on", unit);
	} else {
		TRACEx1(4, "turning motor %d off", unit);
	}
#ifdef MACH2
	outb_p(data, fdc.dor2);
#else
	outb_p(data, fdc.dor);
#endif
	ftape_sleep(10 * MILLISECOND);
	TRACE_EXIT;
}

static void fdc_update_dsr(void)
{
	TRACE_FUN(8, "fdc_update_dsr");

	TRACEx2(5, "rate = %d, precomp = %d", fdc_data_rate, fdc_precomp);
	if (fdc.type >= i82077) {
		outb_p((fdc_data_rate & 0x03) | fdc_precomp, fdc.dsr);
	} else {
		outb_p(fdc_data_rate, fdc.ccr);
	}
	TRACE_EXIT;
}

void fdc_set_write_precomp(int precomp)
{
	/*  write precompensation can be set in multiples of 41.67 nsec.
	 *  round the parameter to the nearest multiple and convert it
	 *  into a fdc setting. Note that 0 means default to the fdc,
	 *  7 is used instead of that.
	 */
	fdc_precomp = ((precomp + 21) / 42) << 2;
	if (fdc_precomp == 0) {
		fdc_precomp = 7 << 2;
	}
	fdc_update_dsr();
}

/* Read back the Drive Specification regs on a i82078, so that we
 * are able to restore them later
 */
void fdc_save_drive_specs(void)
{
	byte cmd1[] =
	{FDC_DRIVE_SPEC, 0x80};
	byte cmd2[] =
	{FDC_DRIVE_SPEC, 0x00, 0x00, 0x00, 0x00, 0xc0};
	int result;

	TRACE_FUN(8, "fdc_save_drive_specs");
	if (fdc.type >= i82078_1) {
		result = fdc_issue_command(cmd1, NR_ITEMS(cmd1), fdc_drv_spec, 4);
		if (result >= 0) {
			cmd2[1] = (fdc_drv_spec[0] & 0x03) | 0x04;
			cmd2[2] = (fdc_drv_spec[1] & 0x03) | 0x24;
			cmd2[3] = (fdc_drv_spec[2] & 0x03) | 0x44;
			cmd2[4] = (fdc_drv_spec[3] & 0x03) | 0x64;
			fdc_command(cmd2, NR_ITEMS(cmd2));
			if (result < 0) {
				TRACE(1, "Setting of drive specs failed");
				return;
			}
		} else {
			TRACE(2, "Save of drive specs failed");
		}
	}
	TRACE_EXIT;
}

/* Restore the previously saved Drive Specification values */
void fdc_restore_drive_specs(void)
{
	byte cmd[] =
	{FDC_DRIVE_SPEC, 0x00, 0x00, 0x00, 0x00, 0xc0};
	int result;

	TRACE_FUN(8, "fdc_restore_drive_specs");
	if (fdc.type > i82078_1) {
		cmd[1] = (fdc_drv_spec[0] & 0x1f) | 0x00;
		cmd[2] = (fdc_drv_spec[1] & 0x1f) | 0x20;
		cmd[3] = (fdc_drv_spec[2] & 0x1f) | 0x40;
		cmd[4] = (fdc_drv_spec[3] & 0x1f) | 0x60;
		result = fdc_command(cmd, NR_ITEMS(cmd));
		if (result < 0) {
			TRACE(2, "Restoration of drive specs failed");
		}
	}
	TRACE_EXIT;
}

/* Select clock for fdc, must correspond with tape drive setting !
 * This also influences the fdc timing so we must adjust some values.
 */
void fdc_set_data_rate(int rate)
{
	/* Select clock for fdc, must correspond with tape drive setting !
	 * This also influences the fdc timing so we must adjust some values.
	 */
	fdc_data_rate = rate;
	fdc_update_dsr();
	fdc_set_seek_rate(fdc_seek_rate);	/* re-adjust for changed clock */
}

/*      Reset the floppy disk controller. Leave the ftape_unit selected.
 */
void fdc_reset(void)
{
	TRACE_FUN(8, "fdc_reset");
	int unit = FTAPE_UNIT;
	byte fdc_ctl = unit | FDC_DMA_MODE;
	int st0;
	int i;
	int result;
	int dummy;

	if (ftape_motor) {
		fdc_ctl |= FDC_MOTOR_0 << unit;
	}
#ifdef MACH2
	outb_p(fdc_ctl & 0x0f, fdc.dor);
	outb_p(fdc_ctl, fdc.dor2);
#else
	outb_p(fdc_ctl, fdc.dor);	/* assert reset, keep unit selected */
#endif
	fdc_usec_wait(10 /* usec */ );	/* delay >= 14 fdc clocks */
	fdc_ctl |= FDC_RESET_NOT;
	fdc_mode = fdc_idle;
#ifdef MACH2
	outb_p(fdc_ctl & 0x0f, fdc.dor);
	outb_p(fdc_ctl, fdc.dor2);
#else
	outb_p(fdc_ctl, fdc.dor);	/* release reset */
#endif
	result = fdc_interrupt_wait(1 * SECOND);
	if (result < 0) {
		TRACE(1, "missing interrupt after reset");
	}
	fdc_set_data_rate(fdc_data_rate);	/* keep original setting */
	fdc_usec_wait(1000 /* usec */ );	/* don't know why, but needed */
	for (i = 0; i < 4; ++i) {	/* clear disk-change status */
		fdc_sense_interrupt_status(&st0, &dummy);
		if (i == unit) {
			current_cylinder = dummy;
		}
	}
	fdc_set_seek_rate(2);
	TRACE_EXIT;
}

/* When we're done, put the fdc into reset mode so that the regular
   floppy disk driver will figure out that something is wrong and
   initialize the controller the way it wants. */
void fdc_disable(void)
{
	TRACE_FUN(8, "fdc_disable");
	int result;
	byte cmd1[] = {FDC_CONFIGURE, 0x00, 0x00, 0x00};
	byte cmd2[] = {FDC_LOCK};
	byte cmd3[] = {FDC_UNLOCK};
	byte stat[1];

	if (CLK_48MHZ && fdc.type >= i82078)
		cmd1[0] |= FDC_CLK48_BIT;
	if (fdc_fifo_locked) {
		result = fdc_issue_command(cmd3, 1, stat, 1);
		if (result < 0 || stat[0] != 0x00) {
			TRACE(-1, "couldn't unlock fifo, configuration remains changed");
		} else {
			cmd1[2] = ((fdc_fifo_state) ? 0 : 0x20) + (fdc_fifo_thr - 1);
			result = fdc_command(cmd1, NR_ITEMS(cmd1));
			if (result < 0) {
				TRACE(-1, "couldn't reconfigure fifo to old state");
			} else if (fdc_lock_state) {
				result = fdc_issue_command(cmd2, 1, stat, 1);
				if (result < 0) {
					TRACE(-1, "couldn't lock old state again");
				}
			}
			TRACEx3(5, "fifo restored: %sabled, thr. %d, %slocked",
				fdc_fifo_state ? "en" : "dis",
			   fdc_fifo_thr, (fdc_lock_state) ? "" : "not ");
		}
		fdc_fifo_locked = 0;
	}
#ifdef MACH2
	outb_p(FTAPE_UNIT & 0x0f, fdc.dor);
	outb_p(FTAPE_UNIT, fdc.dor2);
	udelay(10);
	outb_p(FDC_RESET_NOT & 0x0f, fdc.dor);
	outb_p(FDC_RESET_NOT, fdc.dor2);
#else
	outb_p(FTAPE_UNIT, fdc.dor);
	udelay(10);
	outb_p(FDC_RESET_NOT, fdc.dor);
#endif
	TRACE_EXIT;
}

/*      Specify FDC seek-rate
 */
int fdc_set_seek_rate(int seek_rate)
{
	byte in[3];
	const int hut = 1;	/* minimize head unload time */
	const int hlt = 1;	/* minimize head load time */
	const int rates[] = {250, 2000, 500, 1000};

	in[0] = FDC_SPECIFY;
	in[1] = (((16 - (rates[fdc_data_rate & 0x03] * seek_rate) / 500) << 4) |
		 hut);
	in[2] = (hlt << 1) | 0;
	fdc_seek_rate = seek_rate;

	return fdc_command(in, 3);
}

/*      Sense drive status: get unit's drive status (ST3)
 */
int fdc_sense_drive_status(int *st3)
{
	TRACE_FUN(8, "fdc_sense_drive_status");
	int result;
	byte out[2];
	byte in[1];

	out[0] = FDC_SENSED;
	out[1] = FTAPE_UNIT;
	result = fdc_issue_command(out, 2, in, 1);
	if (result < 0) {
		TRACE(1, "issue_command failed");
	} else {
		*st3 = in[0];
		result = 0;
	}
	TRACE_EXIT;
	return result;
}

/*      Sense Interrupt Status command:
 *      should be issued at the end of each seek.
 *      get ST0 and current cylinder.
 */
int fdc_sense_interrupt_status(int *st0, int *current_cylinder)
{
	TRACE_FUN(8, "fdc_sense_interrupt_status");
	int result;
	byte out[1];
	byte in[2];

	out[0] = FDC_SENSEI;
	result = fdc_issue_command(out, 1, in, 2);
	if (result) {
		TRACE(1, "issue_command failed");
	} else {
		*st0 = in[0];
		*current_cylinder = in[1];
		result = 0;
	}
	TRACE_EXIT;
	return result;
}

/*      step to track
 */
int fdc_seek(int track)
{
	TRACE_FUN(8, "fdc_seek");
	int result;
	byte out[3];
	int st0, pcn;

	out[0] = FDC_SEEK;
	out[1] = FTAPE_UNIT;
	out[2] = track;
	seek_completed = 0;
	result = fdc_command(out, 3);
	if (result != 0) {
		TRACEi(1, "failed, status =", result);
		TRACEx1(4, "destination was: %d, resetting FDC...", track);
		/*  We really need this command to work !
		 */
		fdc_reset();
		TRACE_EXIT;
		return result;
	}
	/*    Handle interrupts until seek_completed or timeout.
	 */
	for (;;) {
		result = fdc_interrupt_wait(2 * SECOND);
		if (result < 0) {
			TRACEi(2, "fdc_interrupt_wait timeout, status =", result);
			TRACE_EXIT;
			return result;
		} else if (seek_completed) {
			result = fdc_sense_interrupt_status(&st0, &pcn);
			if (result != 0) {
				TRACEi(1, "fdc_sense_interrupt_status failed, status =", result);
				TRACE_EXIT;
				return result;
			}
			if ((st0 & ST0_SEEK_END) == 0) {
				TRACE(1, "no seek-end after seek completion !??");
				TRACE_EXIT;
				return -EIO;
			}
			break;
		}
	}
	/*    Verify whether we issued the right tape command.
	 */
	/* Verify that we seek to the proper track. */
	if (pcn != track) {
		TRACE(1, "bad seek..");
		TRACE_EXIT;
		return -EIO;
	}
	current_cylinder = pcn;
	TRACE_EXIT;
	return 0;
}

/*      Recalibrate and wait until home.
 */
int fdc_recalibrate(void)
{
	TRACE_FUN(8, "fdc_recalibrate");
	int result;
	byte out[2];
	int st0;
	int pcn;
	int retry;

	result = fdc_set_seek_rate(6);
	if (result) {
		TRACEi(1, "fdc_set_seek_rate failed, status =", result);
		TRACE_EXIT;
		return result;
	}
	out[0] = FDC_RECAL;
	out[1] = FTAPE_UNIT;
	seek_completed = 0;
	result = fdc_command(out, 2);
	if (result) {
		TRACEi(1, "fdc_command failed, status =", result);
		TRACE_EXIT;
		return result;
	}
	/*    Handle interrupts until seek_completed or timeout.
	 */
	for (retry = 0;; ++retry) {
		result = fdc_interrupt_wait(2 * SECOND);
		if (result < 0) {
			TRACE(1, "fdc_interrupt_wait failed");
			TRACE_EXIT;
			return result;
		} else if (result == 0 && seek_completed) {
			result = fdc_sense_interrupt_status(&st0, &pcn);
			if (result != 0) {
				TRACEi(1, "fdc_sense_interrupt_status failed, status =", result);
				TRACE_EXIT;
				return result;
			}
			if ((st0 & ST0_SEEK_END) == 0) {
				if (retry < 1) {
					continue;	/* some drives/fdc's give an extra interrupt */
				} else {
					TRACE(1, "no seek-end after seek completion !??");
					TRACE_EXIT;
					return -EIO;
				}
			}
			break;
		}
	}
	current_cylinder = pcn;
	if (pcn != 0) {
		TRACEi(1, "failed: resulting track =", pcn);
	}
	result = fdc_set_seek_rate(2);
	if (result != 0) {
		TRACEi(1, "fdc_set_seek_rate failed, status =", result);
		TRACE_EXIT;
		return result;
	}
	TRACE_EXIT;
	return 0;
}

/*      Setup Floppy Disk Controller and DMA to read or write the next cluster
 *      of good sectors from or to the current segment.
 */
int setup_fdc_and_dma(buffer_struct * buff, unsigned char operation)
{
	TRACE_FUN(8, "setup_fdc_and_dma");
	unsigned long flags;
	byte perpend[] = {FDC_PERPEND, 0x00};
	unsigned char out[9];
	int result;
	int dma_mode;

	if (operation == FDC_READ || operation == FDC_READ_DELETED) {
		dma_mode = DMA_MODE_READ;
		if (qic_std == QIC_TAPE_QIC3020) {
			if (fdc.type < i82077AA) {
				/* fdc does not support perpendicular mode. complain */
				TRACE(0, "Your FDC does not support QIC-3020.");
				return -EIO;
			}
			/* enable perpendicular mode */
			perpend[1] = 0x83 + (0x04 << FTAPE_UNIT);
			result = fdc_command(perpend, 2);
			if (result < 0) {
				TRACE(1, "Perpendicular mode entry failed!");
			} else {
				TRACE(4, "Perpendicular mode entered");
				perpend_mode = 1;
			}
		} else if (perpend_mode) {
			/* Turn off perpendicular mode */
			perpend[1] = 0x80;
			result = fdc_command(perpend, 2);
			if (result < 0) {
				TRACE(1, "Perpendicular mode exit failed!");
			} else {
				TRACE(4, "Perpendicular mode exited");
				perpend_mode = 0;
			}
		}
		TRACEx2(5, "xfer %d sectors to 0x%p", buff->sector_count, buff->ptr);
	} else if (operation == FDC_WRITE || operation == FDC_WRITE_DELETED) {
		dma_mode = DMA_MODE_WRITE;
		/* When writing QIC-3020 tapes, turn on perpendicular mode.
		 */
		if (qic_std == QIC_TAPE_QIC3020) {
			if (fdc.type < i82077AA) {
				/* fdc does not support perpendicular mode: complain */
				TRACE(0, "Your FDC does not support QIC-3020.");
				return -EIO;
			}
			perpend[1] = 0x83 + (0x4 << FTAPE_UNIT);
			result = fdc_command(perpend, 2);
			if (result < 0) {
				TRACE(1, "Perpendicular mode entry failed!");
			} else {
				TRACE(4, "Perpendicular mode entered");
				perpend_mode = 1;
			}
		} else if (perpend_mode) {
			perpend[1] = 0x80;
			result = fdc_command(perpend, 2);
			if (result < 0) {
				TRACE(1, "Perpendicular mode exit failed!");
			} else {
				TRACE(4, "Perpendicular mode exited");
				perpend_mode = 0;
			}
		}
		TRACEx2(5, "xfer %d sectors from 0x%p", buff->sector_count, buff->ptr);
	} else {
		TRACE(-1, "bug: illegal operation parameter");
		TRACE_EXIT;
		return -EIO;
	}
	/* Program the DMA controller.
	 */
	save_flags(flags);
	cli();			/* could be called from ISR ! */
	disable_dma(fdc.dma);
	clear_dma_ff(fdc.dma);
	set_dma_mode(fdc.dma, dma_mode);
	set_dma_addr(fdc.dma, (unsigned) buff->ptr);
	set_dma_count(fdc.dma, SECTOR_SIZE * buff->sector_count);
#ifdef GCC_2_4_5_BUG
	/*  This seemingly stupid construction confuses the gcc-2.4.5
	 *  code generator enough to create correct code.
	 */
	if (1) {
		int i;

		for (i = 0; i < 1; ++i) {
			udelay(1);
		}
	}
#endif
	enable_dma(fdc.dma);
	/* Issue FDC command to start reading/writing.
	 */
	out[0] = operation;
	out[1] = FTAPE_UNIT;
	out[2] = buff->cyl;
	out[3] = buff->head;
	out[4] = buff->sect + buff->sector_offset;
	out[5] = 3;		/* Sector size of 1K. */
	out[6] = out[4] + buff->sector_count - 1;	/* last sector */
	out[7] = 109;		/* Gap length. */
	out[8] = 0xff;		/* No limit to transfer size. */
	restore_flags(flags);
	TRACEx4(6, "C: 0x%02x, H: 0x%02x, R: 0x%02x, cnt: 0x%02x",
		out[2], out[3], out[4], out[6] - out[4] + 1);
	result = fdc_command(out, 9);
	if (result != 0) {
		fdc_mode = fdc_idle;
		TRACE(1, "fdc_command failed");
	}
	fdc_setup_error = result;
	TRACE_EXIT;
	return result;
}

int fdc_fifo_enable(void)
{
	TRACE_FUN(8, "fdc_fifo_enable");
	int result = 0;
	byte cmd0[] = {FDC_DUMPREGS};
	byte cmd1[] = {FDC_CONFIGURE, 0, 0x07, 0}; /* enable fifo, thr = 8 */
	byte cmd2[] = {FDC_LOCK};
	byte cmd3[] = {FDC_UNLOCK};
	byte stat;
	byte reg[10];
	int i;

	if (CLK_48MHZ && fdc.type >= i82078)
		cmd1[0] |= FDC_CLK48_BIT;
	if (!fdc_fifo_locked) {
		/*  Dump fdc internal registers for examination
		 */
		result = fdc_command(cmd0, NR_ITEMS(cmd0));
		if (result < 0) {
			TRACE(2, "FDC dumpreg command failed, fifo unchanged");
			result = -EIO;
		} else {
			/*  Now read fdc internal registers from fifo
			 */
			for (i = 0; i < NR_ITEMS(reg); ++i) {
				fdc_read(&reg[i]);
				TRACEx2(6, "Register %d = 0x%02x", i, reg[i]);
			}
			fdc_fifo_state = (reg[8] & 0x20) == 0;
			fdc_lock_state = reg[7] & 0x80;
			fdc_fifo_thr = 1 + (reg[8] & 0x0f);
			TRACEx3(5, "original fifo state: %sabled, threshold %d, %slocked",
				(fdc_fifo_state) ? "en" : "dis",
			   fdc_fifo_thr, (fdc_lock_state) ? "" : "not ");
			/*  If fdc is already locked, unlock it first !
			 */
			if (fdc_lock_state) {
				fdc_ready_wait(100);
				result = fdc_command(cmd3, NR_ITEMS(cmd3));
				if (result < 0) {
					TRACE(-1, "FDC unlock command failed, configuration unchanged");
					result = -EIO;
				}
			}
			/*  Enable fifo and set threshold at xx bytes to allow a
			 *  reasonably large latency and reduce number of dma bursts.
			 */
			fdc_ready_wait(100);
			result = fdc_command(cmd1, NR_ITEMS(cmd1));
			if (result < 0) {
				TRACE(-1, "FDC configure command failed, fifo unchanged");
				result = -EIO;
			} else {
				/*  Now lock configuration so reset will not change it
				 */
				result = fdc_issue_command(cmd2, NR_ITEMS(cmd2), &stat, 1);
				if (result < 0 || stat != 0x10) {
					TRACEx1(-1, "FDC lock command failed, stat = 0x%02x", stat);
					result = -EIO;
				} else {
					fdc_fifo_locked = 1;
					result = 0;
				}
			}
		}
	} else {
		TRACE(2, "Fifo not enabled because locked");
	}
	TRACE_EXIT;
	return result;
}

/*   Determine fd controller type 
 */
static byte fdc_save_state[2] = {0, 0};

int fdc_probe(void)
{
	TRACE_FUN(8, "fdc_probe");
	byte cmd[1];
	byte stat[16];		/* must be able to hold dumpregs & save results */
	int result;

	/*  Try to find out what kind of fd controller we have to deal with
	 *  Scheme borrowed from floppy driver:
	 *  first try if FDC_DUMPREGS command works
	 *  (this indicates that we have a 82072 or better)
	 *  then try the FDC_VERSION command (82072 doesn't support this)
	 *  then try the FDC_UNLOCK command (some older 82077's don't support this)
	 *  then try the FDC_PARTID command (82078's support this)
	 */
	cmd[0] = FDC_DUMPREGS;
	result = fdc_issue_command(cmd, 1, stat, 1);
	if (result == 0) {
		if (stat[0] == 0x80) {
			/* invalid command: must be pre 82072
			 */
			TRACE(2, "Type 8272A/765A compatible FDC found");
			result = i8272;
		} else {
			fdc_result(&stat[1], 9);
			fdc_save_state[0] = stat[7];
			fdc_save_state[1] = stat[8];
			cmd[0] = FDC_VERSION;
			result = fdc_issue_command(cmd, 1, stat, 1);
			if (result < 0 || stat[0] == 0x80) {
				TRACE(2, "Type 82072 FDC found");
				result = i8272;
			} else if (*stat == 0x90) {
				cmd[0] = FDC_UNLOCK;
				result = fdc_issue_command(cmd, 1, stat, 1);
				if (result < 0 || stat[0] != 0x00) {
					TRACE(2, "Type pre-1991 82077 FDC found, treating it like a 82072");
					result = i8272;
				} else {
					int i;

					if (fdc_save_state[0] & 0x80) { /* was locked */
						cmd[0] = FDC_LOCK; /* restore lock */
						result = fdc_issue_command(cmd, 1, stat, 1);
						TRACE(2, "FDC is already locked");
					}
					/* Test for a i82078 FDC */
					cmd[0] = FDC_PARTID;
					result = fdc_issue_command(cmd, 1, stat, 1);
					if (result < 0 || stat[0] == 0x80) {
						/* invalid command: not a i82078xx type FDC */
						result = no_fdc;
						for (i = 0; i < 4; ++i) {
							outb_p(i, fdc.tdr);
							if ((inb_p(fdc.tdr) & 0x03) != i) {
								result = i82077;
								break;
							}
						}
						if (result == no_fdc) {
							result = i82077AA;
							TRACE(2, "Type 82077AA FDC found");
						} else {
							TRACE(2, "Type 82077 FDC found");
						}
					} else {
						/* FDC_PARTID cmd succeeded */
						switch (stat[0] >> 5) {
						case 0x0:
							/* i82078SL or i82078-1.  The SL part cannot run at 2Mbps (the
							 * SL and -1 dies are identical; they are speed graded after
							 * production, according to Intel).  Some SL's can be detected
							 * by doing a SAVE cmd and look at bit 7 of the first byte (the
							 * SEL3V# bit).  If it is 0, the part runs off 3Volts, and hence
							 * it is a SL.
							 */
							cmd[0] = FDC_SAVE;
							result = fdc_issue_command(cmd, 1, stat, 16);
							if (result < 0) {
								TRACE(1, "FDC_SAVE failed. Dunno why");
								/* guess we better claim the fdc to be a i82078 */
								result = i82078;
								TRACE(2, "Type i82078 FDC (i suppose) found");
							} else {
								if ((stat[0] & FDC_SEL3V_BIT)) {
									/* fdc running off 5Volts; Pray that it's a i82078-1
									 */
									TRACE(2, "Type i82078-1 or 5Volt i82078SL FDC found");
									TRACE(2, "Treating it as an i82078-1 (2Mbps) FDC");
									result = i82078_1;
								} else {
									TRACE(2, "Type 3Volt i82078SL FDC (1Mbps) found");
									result = i82078;
								}
							}
							break;
						case 0x1:
						case 0x2: /* S82078B (?!) */
							/* 44pin i82078 found */
							result = i82078;
							TRACE(2, "Type i82078 FDC found");
							break;
						case 0x3: /* NSC PC8744 core; used in several super-IO chips */
							result = i82077AA;
							TRACE(2, "Type 82077AA compatible FDC found");
							break;
						default:
							TRACE(2, "A previously undetected FDC found");
							TRACEi(2, "Treating it as a 82077AA. Please report partid=",
							       stat[0]);
							result = i82077AA;
						} /* switch(stat[ 0] >> 5) */
					} /* if (result < 0 || stat[ 0] == 0x80) */
				}
			} else {
				TRACE(2, "Unknown FDC found");
				result = i8272;
			}
		}
	} else {
		TRACE(-1, "No FDC found");
		result = no_fdc;
	}
	TRACE_EXIT;
	return result;
}

void fdc_config_regs(unsigned fdc_base, unsigned fdc_irq, unsigned fdc_dma)
{
	fdc.irq = fdc_irq;
	fdc.dma = fdc_dma;
	fdc.sra = fdc_base;
	fdc.srb = fdc_base + 1;
	fdc.dor = fdc_base + 2;
	fdc.tdr = fdc_base + 3;
	fdc.msr = fdc.dsr = fdc_base + 4;
	fdc.fifo = fdc_base + 5;
#if defined MACH2 || defined PROBE_FC10
	fdc.dor2 = fdc_base + 6;
#endif
	fdc.dir = fdc.ccr = fdc_base + 7;
}

/*  If probing for a FC-10/20 controller the fdc base address, interrupt
 *  and dma channel must be specified.
 *  If using an alternate fdc controller, base address, interrupt and
 *  dma channel must be specified.
 */
#if defined PROBE_FC10 && !defined FDC_BASE
#error No FDC base address (FDC_BASE) specified in Makefile!
#endif
#if defined FDC_BASE && !defined FDC_IRQ
#error No interrupt (FDC_IRQ) specified in Makefile!
#endif
#if defined FDC_BASE && !defined FDC_DMA
#error No dma channel (FDC_DMA) specified in Makefile!
#endif

void fdc_config(void)
{
	TRACE_FUN(8, "fdc_config");
	static int already_done = 0;

	if (!already_done) {
#ifdef PROBE_FC10
		int fc_type;

		fdc_config_regs(FDC_BASE, FDC_IRQ, FDC_DMA);
		fc_type = fc10_enable();
		if (fc_type != 0) {
			TRACEx1(2, "FC-%c0 controller found", '0' + fc_type);
			fdc.type = fc10;
			fdc.hook = &do_ftape;
		} else {
			TRACE(2, "FC-10/20 controller not found");
			fdc.type = no_fdc;
			fdc.dor2 = 0;	/* not used with std fdc */
			fdc_config_regs(0x3f0, 6, 2);	/* back to std fdc again */
			fdc.hook = &do_ftape;
		}
#else
#ifdef FDC_BASE
		TRACE(2, "Using fdc controller at alternate address");
		fdc_config_regs(FDC_BASE, FDC_IRQ, FDC_DMA);
		fdc.hook = &do_ftape;
#else
		TRACE(2, "Using the standard fdc controller");
		fdc_config_regs(0x3f0, 6, 2);	/* std fdc */
		fdc.hook = &do_ftape;
#endif /* !FDC_BASE */
#endif /* !PROBE_FC10 */
	}
	*(fdc.hook) = fdc_isr;	/* hook our handler in */
	already_done = 1;
	TRACE_EXIT;
}

static void ftape_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	TRACE_FUN(8, "ftape_interrupt");
	void (*handler) (void) = *fdc.hook;

	*fdc.hook = NULL;
	if (handler) {
		handler();
	} else {
		TRACE(-1, "Unexpected ftape interrupt");
	}
	TRACE_EXIT;
}

int fdc_grab_irq_and_dma(void)
{
	TRACE_FUN(8, "fdc_grab_irq_and_dma");
	int result = 0;

	if (fdc.hook == &do_ftape) {
		/*  Get fast interrupt handler.
		 */
		result = request_irq(fdc.irq, ftape_interrupt, SA_INTERRUPT,
				     "ftape", ftape_id);
		if (result) {
			TRACEx1(-1, "Unable to grab IRQ%d for ftape driver", fdc.irq);
			result = -EIO;
		} else {
			result = request_dma(fdc.dma, ftape_id);
			if (result) {
				TRACEx1(-1, "Unable to grab DMA%d for ftape driver", fdc.dma);
				free_irq(fdc.irq, ftape_id);
				result = -EIO;
			} else {
				enable_irq(fdc.irq);
			}
		}
	}
#ifdef FDC_DMA
	if (result == 0 && FDC_DMA == 2) {
		/*  Using same dma channel as standard fdc, need to disable the
		 *  dma-gate on the std fdc. This couldn't be done in the floppy
		 *  driver as some laptops are using the dma-gate to enter a
		 *  low power or even suspended state :-(
		 */
		outb_p(FDC_RESET_NOT, 0x3f2);
		TRACE(2, "DMA-gate on standard fdc disabled");
	}
#endif
	TRACE_EXIT;
	return result;
}

int fdc_release_irq_and_dma(void)
{
	TRACE_FUN(8, "fdc_grab_irq_and_dma");
	int result = 0;

	if (fdc.hook == &do_ftape) {
		disable_dma(fdc.dma);	/* just in case... */
		free_dma(fdc.dma);
		disable_irq(fdc.irq);
		free_irq(fdc.irq, ftape_id);
	}
#ifdef FDC_DMA
	if (result == 0 && FDC_DMA == 2) {
		/*  Using same dma channel as standard fdc, need to disable the
		 *  dma-gate on the std fdc. This couldn't be done in the floppy
		 *  driver as some laptops are using the dma-gate to enter a
		 *  low power or even suspended state :-(
		 */
		outb_p(FDC_RESET_NOT | FDC_DMA_MODE, 0x3f2);
		TRACE(2, "DMA-gate on standard fdc enabled again");
	}
#endif
	TRACE_EXIT;
	return result;
}

int fdc_uninit(void)
{
	TRACE_FUN(8, "fdc_uninit");
	int result = 0;

	if (fdc.sra != 0) {
		if (fdc.dor2 == 0) {
			release_region(fdc.sra, 6);
			release_region(fdc.sra + 7, 1);
		} else {
			release_region(fdc.sra, 8);
		}
	}
	TRACE_EXIT;
	return result;
}

int fdc_init(void)
{
	TRACE_FUN(8, "fdc_init");
	int result = 0;

	fdc_config();
	if (fdc_grab_irq_and_dma() < 0) {
		result = -EBUSY;
	} else {
		ftape_motor = 0;
		fdc_catch_stray_interrupts(1);	/* one always comes */
		TRACE(5, "resetting fdc");
		fdc_reset();	/* init fdc & clear track counters */
		if (fdc.type == no_fdc) {	/* default, means no FC-10 or 20 found */
			fdc.type = fdc_probe();
		}
		if (fdc.type != no_fdc) {
			if (fdc.type >= i82077) {
				if (fdc_fifo_enable() < 0) {
					TRACE(2, "couldn't enable fdc fifo !");
				} else {
					TRACE(5, "fdc fifo enabled and locked");
				}
			}
		} else {
			fdc_release_irq_and_dma();
			result = -EIO;
		}
	}
	if (result >= 0) {
		if (fdc.dor2 == 0) {
			request_region(fdc.sra, 6, "fdc (ftape)");
			request_region(fdc.sra + 7, 1, "fdc (ftape)");
		} else {
			request_region(fdc.sra, 8, "fdc (ftape)");
		}
	}
	TRACE_EXIT;
	return result;
}
