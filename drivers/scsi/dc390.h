/***********************************************************************
 *	FILE NAME : DC390.H					       *
 *	     BY   : C.L. Huang					       *
 *	Description: Device Driver for Tekram DC-390(T) PCI SCSI       *
 *		     Bus Master Host Adapter			       *
 ***********************************************************************/

/* Kernel version autodetection */

#include <linux/version.h>
/* Convert Linux Version, Patch-level, Sub-level to LINUX_VERSION_CODE. */
#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))

#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,50)
#define VERSION_ELF_1_2_13
#elseif LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,95)
#define VERSION_1_3_85
#else
#define VERSION_2_0_0
#endif

/*
 * AMD 53C974 driver, header file
 */

#ifndef DC390_H
#define DC390_H

#if defined(HOSTS_C) || defined(MODULE)

#ifdef	VERSION_2_0_0
#include <scsi/scsicam.h>
#else
#include <linux/scsicam.h>
#endif

extern int DC390_detect(Scsi_Host_Template *psht);
extern int DC390_queue_command(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *));
extern int DC390_abort(Scsi_Cmnd *cmd);

#ifdef	VERSION_2_0_0
extern int DC390_reset(Scsi_Cmnd *cmd, unsigned int resetFlags);
#else
extern int DC390_reset(Scsi_Cmnd *cmd);
#endif

#ifdef	VERSION_ELF_1_2_13
extern int DC390_bios_param(Disk *disk, int devno, int geom[]);
#else
extern int DC390_bios_param(Disk *disk, kdev_t devno, int geom[]);
#endif

#ifdef MODULE
static int DC390_release(struct Scsi_Host *);
#else
#define DC390_release NULL
#endif

#ifndef VERSION_ELF_1_2_13
extern struct proc_dir_entry proc_scsi_tmscsim;
extern int tmscsim_proc_info(char *buffer, char **start, off_t offset, int length, int hostno, int inout);
#endif

#ifdef	VERSION_2_0_0

#define DC390_T    {			\
	NULL,	/* *next */		\
	NULL,	/* *usage_count */	\
	&proc_scsi_tmscsim,	/* *proc_dir */ 	\
	tmscsim_proc_info,	/* (*proc_info)() */	\
	"Tekram DC390(T) V1.10 Dec-05-1996",  /* *name */ \
	DC390_detect,			\
	DC390_release,	/* (*release)() */	\
	NULL,	/* *(*info)() */	\
	NULL,	/* (*command)() */	\
	DC390_queue_command,	\
	DC390_abort,		\
	DC390_reset,		\
	NULL, /* slave attach */\
	DC390_bios_param,	\
	10,/* can queue(-1) */	\
	7, /* id(-1) */ 	\
	SG_ALL, 		\
	2, /* cmd per lun(2) */ \
	0, /* present */	\
	0, /* unchecked isa dma */ \
	DISABLE_CLUSTERING	\
	}
#endif


#ifdef	VERSION_1_3_85

#define DC390_T    {			\
	NULL,	/* *next */		\
	NULL,	/* *usage_count */	\
	&proc_scsi_tmscsim,	/* *proc_dir */ 	\
	tmscsim_proc_info,	/* (*proc_info)() */	\
	"Tekram DC390(T) V1.10 Dec-05-1996",  /* *name */ \
	DC390_detect,			\
	DC390_release,	/* (*release)() */	\
	NULL,	/* *(*info)() */	\
	NULL,	/* (*command)() */	\
	DC390_queue_command,	\
	DC390_abort,		\
	DC390_reset,		\
	NULL, /* slave attach */\
	DC390_bios_param,	\
	10,/* can queue(-1) */	\
	7, /* id(-1) */ 	\
	SG_ALL, 		\
	2, /* cmd per lun(2) */ \
	0, /* present */	\
	0, /* unchecked isa dma */ \
	DISABLE_CLUSTERING	\
	}
#endif


#ifdef	VERSION_ELF_1_2_13

#define DC390_T     {		\
	NULL,			\
	NULL,			\
	"Tekram DC390(T) V1.10 Dec-05-1996",\
	DC390_detect,		\
	DC390_release,			\
	NULL, /* info */	\
	NULL, /* command, deprecated */ \
	DC390_queue_command,	\
	DC390_abort,		\
	DC390_reset,		\
	NULL, /* slave attach */\
	DC390_bios_param,	\
	10,/* can queue(-1) */	\
	7, /* id(-1) */ 	\
	16,/* old (SG_ALL) */	\
	2, /* cmd per lun(2) */ \
	0, /* present */	\
	0, /* unchecked isa dma */ \
	DISABLE_CLUSTERING	\
	}
#endif

#endif /* defined(HOSTS_C) || defined(MODULE) */

#endif /* DC390_H */
