/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2220I device driver for Linux.
 *
 * pci2220i.h - Linux Host Driver for PCI-2220i EIDE Adapters
 *
 * Copyright (c) 1997-1999 Perceptive Solutions, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * Technical updates and product information at:
 *  http://www.psidisk.com
 *
 * Please send questions, comments, bug reports to:
 *  tech@psidisk.com Technical Support
 *
 ****************************************************************************/
#ifndef _PCI2220I_H
#define _PCI2220I_H

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif 
#define	LINUXVERSION(v,p,s)    (((v)<<16) + ((p)<<8) + (s))

// function prototypes
int Pci2220i_Detect			(Scsi_Host_Template *tpnt);
int Pci2220i_Command		(Scsi_Cmnd *SCpnt);
int Pci2220i_QueueCommand	(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *));
int Pci2220i_Abort			(Scsi_Cmnd *SCpnt);
int Pci2220i_Reset			(Scsi_Cmnd *SCpnt, unsigned int flags);
int Pci2220i_Release		(struct Scsi_Host *pshost);
int Pci2220i_BiosParam		(Disk *disk, kdev_t dev, int geom[]);

#ifndef NULL
	#define NULL 0
#endif

extern struct proc_dir_entry Proc_Scsi_Pci2220i;

#if LINUX_VERSION_CODE >= LINUXVERSION(2,1,75)
#define PCI2220I {															\
		next:						NULL,									\
		module:						NULL,									\
		proc_dir:					&Proc_Scsi_Pci2220i,					\
		proc_info:					NULL,	/* let's not bloat the kernel */\
		name:						"PCI-2220I/PCI-2240I",					\
		detect:						Pci2220i_Detect,						\
		release:					Pci2220i_Release,						\
		info:						NULL,	/* let's not bloat the kernel */\
		command:					Pci2220i_Command,						\
		queuecommand:				Pci2220i_QueueCommand,					\
		eh_strategy_handler:		NULL,									\
		eh_abort_handler:			NULL,									\
		eh_device_reset_handler:	NULL,									\
		eh_bus_reset_handler:		NULL,									\
		eh_host_reset_handler:		NULL,									\
		abort:						Pci2220i_Abort,							\
		reset:						Pci2220i_Reset,							\
		slave_attach:				NULL,									\
		bios_param:					Pci2220i_BiosParam,						\
		can_queue:					1,										\
		this_id:					-1,										\
		sg_tablesize:				SG_ALL,								\
		cmd_per_lun:				1,										\
		present:					0,										\
		unchecked_isa_dma:			0,										\
		use_clustering:				DISABLE_CLUSTERING,						\
		use_new_eh_code:			0										\
		}
#else
#define PCI2220I { NULL, NULL,						\
			&Proc_Scsi_Pci2220i,/* proc_dir_entry */\
			NULL,		                			\
			"PCI-2220I/PCI-2240I",					\
			Pci2220i_Detect,						\
			Pci2220i_Release,						\
			NULL,	 								\
			Pci2220i_Command,						\
			Pci2220i_QueueCommand,					\
			Pci2220i_Abort,							\
			Pci2220i_Reset,							\
			NULL,									\
			Pci2220i_BiosParam,                 	\
			1, 										\
			-1, 									\
			SG_ALL,		 						\
			1, 										\
			0, 										\
			0, 										\
			DISABLE_CLUSTERING }
#endif

#endif
