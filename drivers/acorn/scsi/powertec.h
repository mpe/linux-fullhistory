/*
 * PowerTec SCSI driver
 *
 * Copyright (C) 1997-1998 Russell King
 */
#ifndef POWERTECSCSI_H
#define POWERTECSCSI_H

extern int powertecscsi_detect (Scsi_Host_Template *);
extern int powertecscsi_release (struct Scsi_Host *);
extern const char *powertecscsi_info (struct Scsi_Host *);
extern int powertecscsi_proc_info (char *buffer, char **start, off_t offset,
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

#include "fas216.h"

#define POWERTECSCSI {							\
proc_info:	powertecscsi_proc_info,					\
name:		"PowerTec SCSI",					\
detect:		powertecscsi_detect,	/* detect		*/	\
release:	powertecscsi_release,	/* release		*/	\
info:		powertecscsi_info,	/* info			*/	\
command:	fas216_command,		/* command		*/	\
queuecommand:	fas216_queue_command,	/* queuecommand		*/	\
abort:		fas216_abort,		/* abort		*/	\
reset:		fas216_reset,		/* reset		*/	\
bios_param:	scsicam_bios_param,	/* biosparam		*/	\
can_queue:	CAN_QUEUE,		/* can queue		*/	\
this_id:	SCSI_ID,		/* scsi host id		*/	\
sg_tablesize:	SG_ALL,			/* sg_tablesize		*/	\
cmd_per_lun:	CMD_PER_LUN,		/* cmd per lun		*/	\
use_clustering:	ENABLE_CLUSTERING,					\
eh_strategy_handler:		NULL,					\
eh_host_reset_handler:		fas216_eh_host_reset,			\
eh_bus_reset_handler:		fas216_eh_bus_reset,			\
eh_device_reset_handler:	fas216_eh_device_reset,			\
eh_abort_handler:		fas216_eh_abort,			\
use_new_eh_code:		0					\
	}

#ifndef HOSTS_C

#include <asm/dma.h>

#define NR_SG	256

typedef struct {
	FAS216_Info info;

	struct {
		unsigned int term_port;
		unsigned int terms;
	} control;

	/* other info... */
	dmasg_t		dmasg[NR_SG];	/* Scatter DMA list	*/
} PowerTecScsi_Info;

#endif /* HOSTS_C */

#endif /* POWERTECSCSI_H */
