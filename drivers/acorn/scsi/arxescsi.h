/*
 * ARXE SCSI card driver
 *
 * Copyright (C) 1997 Russell King
 * Changes to support ARXE 16-bit SCSI card by Stefan Hanske
 */
#ifndef ARXE_SCSI_H
#define ARXE_SCSI_H

#define MANU_ARXE 	0x0041
#define PROD_ARXE_SCSI	0x00be

extern int arxescsi_detect (Scsi_Host_Template *);
extern int arxescsi_release (struct Scsi_Host *);
extern const char *arxescsi_info (struct Scsi_Host *);
extern int arxescsi_proc_info (char *buffer, char **start, off_t offset,
				int length, int hostno, int inout);

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef CAN_QUEUE
/*
 * Default queue size
 */
#define CAN_QUEUE	1
#endif

#ifndef CMD_PER_LUN
#define CMD_PER_LUN	1
#endif

#ifndef SCSI_ID
/*
 * Default SCSI host ID
 */
#define SCSI_ID		7
#endif

#include <scsi/scsicam.h>

#ifndef HOSTS_C
#include "fas216.h"
#endif

#define ARXEScsi {							\
proc_info:	arxescsi_proc_info,						\
name:		"ARXE SCSI card",						\
detect:		arxescsi_detect,		/* detect		*/	\
release:	arxescsi_release,		/* release		*/	\
info:		arxescsi_info,			/* info			*/	\
command:	fas216_command,			/* command		*/	\
queuecommand:	fas216_queue_command,		/* queuecommand		*/	\
abort:		fas216_abort,			/* abort		*/	\
reset:		fas216_reset,			/* reset		*/	\
bios_param:	scsicam_bios_param,		/* biosparam		*/	\
can_queue:	CAN_QUEUE,			/* can queue		*/	\
this_id:	SCSI_ID,			/* scsi host id		*/	\
sg_tablesize:	SG_ALL,				/* sg_tablesize		*/	\
cmd_per_lun:	CMD_PER_LUN,			/* cmd per lun		*/	\
use_clustering:	DISABLE_CLUSTERING						\
	}

#ifndef HOSTS_C

typedef struct {
    FAS216_Info info;

    /* other info... */
    unsigned int	cstatus;	/* card status register	*/
    unsigned int	dmaarea;	/* Pseudo DMA area	*/
} ARXEScsi_Info;

#define CSTATUS_IRQ	(1 << 0)
#define CSTATUS_DRQ	(1 << 0)

#endif /* HOSTS_C */

#endif /* ARXE_SCSI_H */
