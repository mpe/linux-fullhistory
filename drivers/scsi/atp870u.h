#ifndef _ATP870U_H

/* $Id: atp870u.h,v 1.0 1997/05/07 15:09:00 root Exp root $

 * Header file for the ACARD 870U/W driver for Linux
 *
 * $Log: atp870u.h,v $
 * Revision 1.0  1997/05/07  15:09:00  root
 * Initial revision
 *
 */

#include <linux/types.h>

/* I/O Port */

#define MAX_CDB 12
#define MAX_SENSE 14

static int atp870u_detect(Scsi_Host_Template *);
static int atp870u_command(Scsi_Cmnd *);
static int atp870u_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int atp870u_abort(Scsi_Cmnd *);
static int atp870u_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int *);
static int atp870u_release(struct Scsi_Host *);

#define qcnt		32
#define ATP870U_SCATTER 128
#define ATP870U_CMDLUN 1

#ifndef NULL
#define NULL 0
#endif

extern const char *atp870u_info(struct Scsi_Host *);

extern int atp870u_proc_info(char *, char **, off_t, int, int, int);

#define ATP870U {						\
	proc_info: atp870u_proc_info,				\
	detect: atp870u_detect, 				\
	release: atp870u_release,				\
	info: atp870u_info,					\
	command: atp870u_command,				\
	queuecommand: atp870u_queuecommand,			\
	eh_abort_handler: atp870u_abort, 			\
	bios_param: atp870u_biosparam,				\
	can_queue: qcnt,	 /* max simultaneous cmds      */\
	this_id: 7,	       /* scsi id of host adapter    */\
	sg_tablesize: ATP870U_SCATTER,	/* max scatter-gather cmds    */\
	cmd_per_lun: ATP870U_CMDLUN,	/* cmds per lun (linked cmds) */\
	present: 0,		/* number of 7xxx's present   */\
	unchecked_isa_dma: 0,	/* no memory DMA restrictions */\
	use_clustering: ENABLE_CLUSTERING,			\
}

#endif
