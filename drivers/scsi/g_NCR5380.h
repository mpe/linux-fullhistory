/*
 * Generic Generic NCR5380 driver defines
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
 * $Log: generic_NCR5380.h,v $
 */

#ifndef GENERIC_NCR5380_H
#define GENERIC_NCR5380_H

#define GENERIC_NCR5380_PUBLIC_RELEASE 1


#ifndef ASM
int generic_NCR5380_abort(Scsi_Cmnd *);
int generic_NCR5380_detect(Scsi_Host_Template *);
int generic_NCR5380_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int generic_NCR5380_reset(Scsi_Cmnd *);


#ifndef NULL
#define NULL 0
#endif

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#ifdef HOSTS_C

#define GENERIC_NCR5380 {NULL, NULL, "Trantor T128/T128F/T228", 	\
	generic_NCR5380_detect, NULL, NULL, NULL, 	\
	generic_NCR5380_queue_command, generic_NCR5380_abort, 		\
	generic_NCR5380_reset, NULL, 					\
	NULL, /* can queue */ CAN_QUEUE, /* id */ 7, SG_ALL,		\
	/* cmd per lun */ CMD_PER_LUN , 0, 0, DISABLE_CLUSTERING}

#else
#define NCR5380_implementation_fields \
    int port

#define NCR5380_local_declare() \
    register int port

#define NCR5380_setup(instance) \
    port = (instance)->io_port

#define NCR5380_read(reg) (inb(port + (reg)))
#define NCR5380_write(reg, value) (outb((value), (port + (reg))))

#define NCR5380_intr generic_NCR5380_intr
#define NCR5380_queue_command generic_NCR5380_queue_command
#define NCR5380_abort generic_NCR5380_abort
#define NCR5380_reset generic_NCR5380_reset

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#endif /* else def HOSTS_C */
#endif /* ndef ASM */
#endif /* GENERIC_NCR5380_H */

