/*
 * Sun3 SCSI stuff by Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * Adapted from mac_scsinew.h:
 */
/*
 * Cumana Generic NCR5380 driver defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

/*
 * $Log: cumana_NCR5380.h,v $
 */

#ifndef SUN3_NCR5380_H
#define SUN3_NCR5380_H

#define SUN3SCSI_PUBLIC_RELEASE 1

/*
 * Int: level 2 autovector
 * IO: type 1, base 0x00140000, 5 bits phys space: A<4..0>
 */
#define IRQ_SUN3_SCSI 2
#define IOBASE_SUN3_SCSI 0x00140000

int sun3scsi_abort (Scsi_Cmnd *);
int sun3scsi_detect (Scsi_Host_Template *);
int sun3scsi_release (struct Scsi_Host *);
const char *sun3scsi_info (struct Scsi_Host *);
int sun3scsi_reset(Scsi_Cmnd *, unsigned int);
int sun3scsi_queue_command (Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int sun3scsi_proc_info (char *buffer, char **start, off_t offset,
			int length, int hostno, int inout);

#ifndef NULL
#define NULL 0
#endif

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#ifndef SG_TABLESIZE
#define SG_TABLESIZE SG_NONE
#endif

#ifndef USE_TAGGED_QUEUING
#define	USE_TAGGED_QUEUING 0
#endif

#include <scsi/scsicam.h>

#define SUN3_NCR5380 {							\
name:			"Sun3 NCR5380 SCSI",				\
detect:			sun3scsi_detect,				\
release:		sun3scsi_release,	/* Release */		\
info:			sun3scsi_info,					\
queuecommand:		sun3scsi_queue_command,				\
abort:			sun3scsi_abort,			 		\
reset:			sun3scsi_reset,					\
bios_param:		scsicam_bios_param,	/* biosparam */		\
can_queue:		CAN_QUEUE,		/* can queue */		\
this_id:		7,			/* id */		\
sg_tablesize:		SG_ALL,			/* sg_tablesize */	\
cmd_per_lun:		CMD_PER_LUN,		/* cmd per lun */	\
unchecked_isa_dma:	0,			/* unchecked_isa_dma */	\
use_clustering:		DISABLE_CLUSTERING				\
	}

#ifndef HOSTS_C

#define NCR5380_implementation_fields \
    int port, ctrl

#define NCR5380_local_declare() \
        struct Scsi_Host *_instance

#define NCR5380_setup(instance) \
        _instance = instance

#define NCR5380_read(reg) sun3scsi_read(_instance, reg)
#define NCR5380_write(reg, value) sun3scsi_write(_instance, reg, value)

#define NCR5380_intr sun3scsi_intr
#define NCR5380_queue_command sun3scsi_queue_command
#define NCR5380_abort sun3scsi_abort
#define NCR5380_reset sun3scsi_reset
#define NCR5380_proc_info sun3scsi_proc_info

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#endif /* ndef HOSTS_C */
#endif /* SUN3_NCR5380_H */

