/*
 * Cumana SCSI II driver
 *
 * Copyright (C) 1997-1998 Russell King
 */
#ifndef CUMANA_2_H
#define CUMANA_2_H

extern int cumanascsi_2_detect (Scsi_Host_Template *);
extern int cumanascsi_2_release (struct Scsi_Host *);
extern const char *cumanascsi_2_info (struct Scsi_Host *);
extern int cumanascsi_2_proc_info (char *buffer, char **start, off_t offset,
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

#define CUMANASCSI_2 {							\
proc_info:		cumanascsi_2_proc_info,				\
name:			"Cumana SCSI II",				\
detect:			cumanascsi_2_detect,	/* detect		*/	\
release:		cumanascsi_2_release,	/* release		*/	\
info:			cumanascsi_2_info,	/* info			*/	\
command:		fas216_command,		/* command		*/	\
queuecommand:		fas216_queue_command,	/* queuecommand		*/	\
abort:			fas216_abort,		/* abort		*/	\
reset:			fas216_reset,		/* reset		*/	\
bios_param:		scsicam_bios_param,	/* biosparam		*/	\
can_queue:		CAN_QUEUE,		/* can queue		*/	\
this_id:		SCSI_ID,		/* scsi host id		*/	\
sg_tablesize:		SG_ALL,			/* sg_tablesize		*/	\
cmd_per_lun:		CAN_QUEUE,		/* cmd per lun		*/	\
unchecked_isa_dma:	0,			/* unchecked isa dma	*/	\
use_clustering:		DISABLE_CLUSTERING					\
	}

#ifndef HOSTS_C

#include <asm/dma.h>

#define NR_SG	256

typedef struct {
	FAS216_Info info;

	/* other info... */
	unsigned int	status;		/* card status register	*/
	unsigned int	alatch;		/* Control register	*/
	unsigned int	terms;		/* Terminator state	*/
	unsigned int	dmaarea;	/* Pseudo DMA area	*/
	dmasg_t		dmasg[NR_SG];	/* Scatter DMA list	*/
} CumanaScsi2_Info;

#define CSTATUS_IRQ	(1 << 0)
#define CSTATUS_DRQ	(1 << 1)

#endif /* HOSTS_C */

#endif /* CUMANASCSI_2_H */
