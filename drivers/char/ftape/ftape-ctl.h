#ifndef _FTAPE_CTL_H
#define _FTAPE_CTL_H

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-ctl.h,v $
 $Author: bas $
 *
 $Revision: 1.4 $
 $Date: 1995/05/03 18:04:03 $
 $State: Beta $
 *
 *      This file contains the non-standard IOCTL related definitions
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/ioctl.h>
#include <linux/mtio.h>

#include "vendors.h"


typedef struct {
	int used;		/* any reading or writing done */
	/* isr statistics */
	unsigned int id_am_errors;	/* id address mark not found */
	unsigned int id_crc_errors;	/* crc error in id address mark */
	unsigned int data_am_errors;	/* data address mark not found */
	unsigned int data_crc_errors;	/* crc error in data field */
	unsigned int overrun_errors;	/* fdc access timing problem */
	unsigned int no_data_errors;	/* sector not found */
	unsigned int retries;	/* number of tape retries */
	/* ecc statistics */
	unsigned int crc_errors;	/* crc error in data */
	unsigned int crc_failures;	/* bad data without crc error */
	unsigned int ecc_failures;	/* failed to correct */
	unsigned int corrected;	/* total sectors corrected */
	/* general statistics */
	unsigned int rewinds;	/* number of tape rewinds */
	unsigned int defects;	/* bad sectors due to media defects */
} history_record;

/*
 *      ftape-ctl.c defined global vars.
 */
extern int ftape_failure;
extern int write_protected;
extern ftape_offline;
extern int formatted;
extern int no_tape;
extern history_record history;
extern int ftape_data_rate;
extern int going_offline;
extern vendor_struct drive_type;
extern int segments_per_track;
extern int segments_per_head;
extern int segments_per_cylinder;
extern int tracks_per_tape;
extern int ftape_seg_pos;
extern int first_data_segment;
extern int ftape_state;
extern int read_only;

/*
 *      ftape-ctl.c defined global functions.
 */
extern int _ftape_open(void);
extern int _ftape_close(void);
extern int _ftape_ioctl(unsigned int command, void *arg);
extern int ftape_seek_to_bot(void);
extern int ftape_seek_to_eot(void);
extern int ftape_new_cartridge(void);
extern int ftape_abort_operation(void);
extern void ftape_reset_position(void);
extern void ftape_calc_timeouts(void);
extern void ftape_init_driver(void);

#endif
