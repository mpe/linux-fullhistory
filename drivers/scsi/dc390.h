/***********************************************************************
 *	FILE NAME : DC390.H					       *
 *	     BY   : C.L. Huang					       *
 *	Description: Device Driver for Tekram DC-390(T) PCI SCSI       *
 *		     Bus Master Host Adapter			       *
 ***********************************************************************/
/* $Id: dc390.h,v 2.12 1998/12/25 17:33:27 garloff Exp $ */

#include <linux/version.h>

/*
 * DC390/AMD 53C974 driver, header file
 */

#ifndef DC390_H
#define DC390_H

#define DC390_BANNER "Tekram DC390/AM53C974"
#define DC390_VERSION "2.0d 1998/12/25"

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

extern struct proc_dir_entry DC390_proc_scsi_tmscsim;
extern int DC390_proc_info(char *buffer, char **start, off_t offset, int length, int hostno, int inout);

#define DC390_T    {					\
   proc_dir:       &DC390_proc_scsi_tmscsim,		\
   proc_info:      DC390_proc_info,			\
   name:           DC390_BANNER " V" DC390_VERSION,	\
   detect:         DC390_detect,			\
   release:        DC390_release,			\
   queuecommand:   DC390_queue_command,			\
   abort:          DC390_abort,				\
   reset:          DC390_reset,				\
   bios_param:     DC390_bios_param,			\
   can_queue:      17,					\
   this_id:        7,					\
   sg_tablesize:   SG_ALL,				\
   cmd_per_lun:    8,					\
   use_clustering: DISABLE_CLUSTERING			\
   }

#endif /* defined(HOSTS_C) || defined(MODULE) */

#endif /* DC390_H */
