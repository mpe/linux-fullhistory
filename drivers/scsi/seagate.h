/*
 *	seagate.h Copyright (C) 1992 Drew Eckhardt 
 *	low level scsi driver header for ST01/ST02 by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 */

#ifndef _SEAGATE_H
	#define SEAGATE_H
/*
	$Header
*/
#ifndef ASM
int seagate_st0x_detect(Scsi_Host_Template *);
int seagate_st0x_command(Scsi_Cmnd *);
int seagate_st0x_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));

int seagate_st0x_abort(Scsi_Cmnd *);
const char *seagate_st0x_info(struct Scsi_Host *);
int seagate_st0x_reset(Scsi_Cmnd *, unsigned int); 

#ifndef NULL
	#define NULL 0
#endif

#include <linux/kdev_t.h>
int seagate_st0x_biosparam(Disk *, kdev_t, int*);

#define SEAGATE_ST0X  {  NULL, NULL, NULL, NULL, \
			 NULL, seagate_st0x_detect, 	\
			 NULL, 						\
			 seagate_st0x_info, seagate_st0x_command,  	\
			 seagate_st0x_queue_command, seagate_st0x_abort, \
			 seagate_st0x_reset, NULL, seagate_st0x_biosparam, \
			 1, 7, SG_ALL, 1, 0, 0, DISABLE_CLUSTERING}
#endif /* ASM */

#endif /* _SEAGATE_H */
