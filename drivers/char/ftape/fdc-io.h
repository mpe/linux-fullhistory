#ifndef _FDC_IO_H
#define _FDC_IO_H

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/fdc-io.h,v $
 $Author: bas $
 *
 $Revision: 1.38 $
 $Date: 1995/05/10 16:09:36 $
 $State: Beta $
 *
 *      This file contains the low level functions
 *      that communicate with the floppy disk controller,
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/fdreg.h>

#define FDC_SK_BIT      (0x20)
#define FDC_MT_BIT      (0x80)

#define FDC_READ        (FD_READ & ~(FDC_SK_BIT | FDC_MT_BIT))
#define FDC_WRITE       (FD_WRITE & ~FDC_MT_BIT)
#define FDC_READ_DELETED (0x4c)
#define FDC_WRITE_DELETED (0x49)
#define FDC_READID      (0x4a)
#define FDC_SENSED      (0x04)
#define FDC_SENSEI      (FD_SENSEI)
#define FDC_RECAL       (FD_RECALIBRATE)
#define FDC_SEEK        (FD_SEEK)
#define FDC_SPECIFY     (FD_SPECIFY)
#define FDC_RECALIBR    (FD_RECALIBRATE)
#define FDC_VERSION     (FD_VERSION)
#define FDC_PERPEND     (FD_PERPENDICULAR)
#define FDC_DUMPREGS    (FD_DUMPREGS)
#define FDC_LOCK        (FD_LOCK)
#define FDC_UNLOCK      (FD_UNLOCK)
#define FDC_CONFIGURE   (FD_CONFIGURE)
#define FDC_DRIVE_SPEC  (0x8e)	/* i82078 has this (any others?) */
#define FDC_PARTID      (0x18)	/* i82078 has this */
#define FDC_SAVE        (0x2e)	/* i82078 has this (any others?) */
#define FDC_RESTORE     (0x4e)	/* i82078 has this (any others?) */

#define FDC_STATUS_MASK (STATUS_BUSY | STATUS_DMA | STATUS_DIR | STATUS_READY)
#define FDC_DATA_READY  (STATUS_READY)
#define FDC_DATA_OUTPUT (STATUS_DIR)
#define FDC_DATA_READY_MASK (STATUS_READY | STATUS_DIR)
#define FDC_DATA_OUT_READY  (STATUS_READY | STATUS_DIR)
#define FDC_DATA_IN_READY   (STATUS_READY)
#define FDC_BUSY        (STATUS_BUSY)
#define FDC_CLK48_BIT   (0x80)
#define FDC_SEL3V_BIT   (0x40)

#define ST0_INT_MASK    (ST0_INTR)
#define FDC_INT_NORMAL  (ST0_INTR & 0x00)
#define FDC_INT_ABNORMAL (ST0_INTR & 0x40)
#define FDC_INT_INVALID (ST0_INTR & 0x80)
#define FDC_INT_READYCH (ST0_INTR & 0xC0)
#define ST0_SEEK_END    (ST0_SE)
#define ST3_TRACK_0     (ST3_TZ)

#define FDC_RESET_NOT   (0x04)
#define FDC_DMA_MODE    (0x08)
#define FDC_MOTOR_0     (0x10)
#define FDC_MOTOR_1     (0x20)

typedef struct {
	void (**hook) (void);	/* our wedge into the isr */
	enum {
		no_fdc, i8272, i82077, i82077AA, fc10,
		i82078, i82078_1
	} type;			/* FDC type */
	unsigned char irq;	/* FDC irq nr */
	unsigned char dma;	/* FDC dma channel nr */
	unsigned short sra;	/* Status register A (PS/2 only) */
	unsigned short srb;	/* Status register B (PS/2 only) */
	unsigned short dor;	/* Digital output register */
	unsigned short tdr;	/* Tape Drive Register (82077SL-1 &
				   82078 only) */
	unsigned short msr;	/* Main Status Register */
	unsigned short dsr;	/* Datarate Select Register (8207x only) */
	unsigned short fifo;	/* Data register / Fifo on 8207x */
	unsigned short dir;	/* Digital Input Register */
	unsigned short ccr;	/* Configuration Control Register */
	unsigned short dor2;	/* Alternate dor on MACH-2 controller,
				   also used with FC-10, meaning unknown */
} fdc_config_info;

typedef enum {
	fdc_data_rate_250 = 2,
	fdc_data_rate_500 = 0,
	fdc_data_rate_1000 = 3,
	fdc_data_rate_2000 = 1,	/* i82078-1: remember to use Data Rate Table #2 */
} fdc_data_rate_type;

typedef enum {
	waiting = 0,
	reading,
	writing,
	done,
	error,
} buffer_state_enum;

typedef volatile enum {
	fdc_idle = 0,
	fdc_reading_data = FDC_READ,
	fdc_seeking = FDC_SEEK,
	fdc_writing_data = FDC_WRITE,
	fdc_reading_id = FDC_READID,
	fdc_recalibrating = FDC_RECAL,
} fdc_mode_enum;

/*
 *      fdc-io.c defined public variables
 */
extern fdc_mode_enum fdc_mode;
extern volatile enum runner_status_enum runner_status;
extern int old_vfo;
extern volatile int head;
extern volatile int tail;
extern int fdc_setup_error;	/* outdated ??? */
extern struct wait_queue *wait_intr;
extern volatile unsigned int next_segment;	/* next segment for read ahead */
extern int ftape_unit;		/* fdc unit specified at ftape_open() */
extern int ftape_motor;		/* fdc motor line state */
extern int current_cylinder;	/* track nr the FDC thinks we're on */
extern volatile byte fdc_head;	/* FDC head */
extern volatile byte fdc_cyl;	/* FDC track */
extern volatile byte fdc_sect;	/* FDC sector */
extern fdc_config_info fdc;	/* FDC hardware configuration */

/*
 *      fdc-io.c defined public functions
 */
extern void fdc_catch_stray_interrupts(unsigned count);
extern int fdc_ready_wait(int timeout);
extern int fdc_write(byte data);
extern int fdc_read(byte * data);
extern int fdc_command(byte * cmd_data, int cmd_len);
extern int fdc_result(byte * res_data, int res_len);
extern int fdc_issue_command(byte * out_data, int out_count, \
			     byte * in_data, int in_count);
extern void fdc_isr(void);
extern int fdc_interrupt_wait(int time);
extern void fdt_sleep(unsigned int time);
extern int fdc_specify(int head_unload_time, int seek_rate,
		       int head_load_time, int non_dma);
extern int fdc_set_seek_rate(int seek_rate);
extern int fdc_seek(int track);
extern int fdc_sense_drive_status(int *st3);
extern void fdc_motor(int motor);
extern void fdc_reset(void);
extern int fdc_recalibrate(void);
extern void fdc_disable(void);
extern int fdc_wait_calibrate(void);
extern int fdc_sense_interrupt_status(int *st0, int *current_cylinder);
extern void fdc_save_drive_specs(void);
extern void fdc_restore_drive_specs(void);
extern void fdc_set_data_rate(int rate);
extern int fdc_release_irq_and_dma(void);
extern int fdc_init(void);
extern int fdc_uninit(void);
extern void fdc_set_write_precomp(int precomp);

#endif
