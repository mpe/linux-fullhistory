/***********************************************************************
 *	FILE NAME : DC390.H					       *
 *	     BY   : C.L. Huang					       *
 *	Description: Device Driver for Tekram DC-390(T) PCI SCSI       *
 *		     Bus Master Host Adapter			       *
 ***********************************************************************/

#include <linux/version.h>

/*
 * AMD 53C974 driver, header file
 */

#ifndef DC390_H
#define DC390_H

#if defined(HOSTS_C) || defined(MODULE)

#include <scsi/scsicam.h>

extern int DC390_detect(Scsi_Host_Template *psht);
extern int DC390_queue_command(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *));
extern int DC390_abort(Scsi_Cmnd *cmd);
extern int DC390_reset(Scsi_Cmnd *cmd, unsigned int resetFlags);
extern int DC390_bios_param(Disk *disk, kdev_t devno, int geom[]);

#ifdef MODULE
static int DC390_release(struct Scsi_Host *);
#else
# define DC390_release NULL
#endif

extern struct proc_dir_entry proc_scsi_tmscsim;
extern int tmscsim_proc_info(char *buffer, char **start, off_t offset, int length, int hostno, int inout);

#define DC390_T    {			          \
   proc_dir:       &proc_scsi_tmscsim,            \
   proc_info:      tmscsim_proc_info,             \
   name:           "Tekram DC390(T) V1.12 Feb-25-1998",\
   detect:         DC390_detect,   		  \
   release:        DC390_release,		  \
   queuecommand:   DC390_queue_command,	          \
   abort:          DC390_abort,    		  \
   reset:          DC390_reset,    		  \
   bios_param:     DC390_bios_param,		  \
   can_queue:      10,                 	          \
   this_id:        7,                             \
   sg_tablesize:   SG_ALL,            		  \
   cmd_per_lun:    2,                 		  \
   use_clustering: DISABLE_CLUSTERING 		  \
   }

#endif /* defined(HOSTS_C) || defined(MODULE) */

#endif /* DC390_H */
