/* @(#)aha274x.h 1.11 94/09/06 jda */

/*
 * Adaptec 274x device driver for Linux.
 * Copyright (c) 1994 The University of Calgary Department of Computer Science.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef aha274x_h
#define aha274x_h

#define	AHA274X_MAXSCB		4
#define AHA274X_H_VERSION	"1.11"

/*
 *  Scsi_Host_Template (see hosts.h) for 274x - some fields
 *  to do with card config are filled in after the card is
 *  detected.
 */
#define AHA274X	{						\
	NULL,							\
	NULL,							\
	NULL,							\
	aha274x_detect,						\
	NULL,							\
	aha274x_info,						\
	aha274x_command,					\
	aha274x_queue,						\
	aha274x_abort,						\
	aha274x_reset,						\
	NULL,							\
	aha274x_biosparam,					\
	AHA274X_MAXSCB,		/* max simultaneous cmds      */\
	-1,			/* scsi id of host adapter    */\
	SG_ALL,			/* max scatter-gather cmds    */\
	1,			/* cmds per lun (linked cmds) */\
	0,			/* number of 274x's present   */\
	0,			/* no memory DMA restrictions */\
	DISABLE_CLUSTERING					\
}

extern int aha274x_queue(Scsi_Cmnd *, void (*)(Scsi_Cmnd *));
extern int aha274x_biosparam(Disk *, int, int[]);
extern int aha274x_detect(Scsi_Host_Template *);
extern int aha274x_command(Scsi_Cmnd *);
extern int aha274x_abort(Scsi_Cmnd *);
extern int aha274x_reset(Scsi_Cmnd *);
extern const char *aha274x_info(struct Scsi_Host *);

#endif
