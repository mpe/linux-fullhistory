/*+M*************************************************************************
 * Adaptec AIC7xxx device driver for Linux.
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * $Id: aic7xxx.h,v 3.2 1996/07/23 03:37:26 deang Exp $
 *-M*************************************************************************/
#ifndef _aic7xxx_h
#define _aic7xxx_h

#define AIC7XXX_H_VERSION  "3.2.4"

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

#if defined(__i386__)
#  define AIC7XXX_BIOSPARAM aic7xxx_biosparam
#else
#  define AIC7XXX_BIOSPARAM NULL
#endif

/*
 * Scsi_Host_Template (see hosts.h) for AIC-7xxx - some fields
 * to do with card config are filled in after the card is detected.
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,65)
#define AIC7XXX	{						\
	next: NULL,						\
	module: NULL,						\
	proc_dir: NULL,						\
	proc_info: aic7xxx_proc_info,				\
	name: NULL,						\
	detect: aic7xxx_detect,					\
	release: aic7xxx_release,				\
	info: aic7xxx_info,					\
	command: NULL,						\
	queuecommand: aic7xxx_queue,				\
	eh_strategy_handler: NULL,				\
	eh_abort_handler: NULL,					\
	eh_device_reset_handler: NULL,				\
	eh_bus_reset_handler: NULL,				\
	eh_host_reset_handler: NULL,				\
	abort: aic7xxx_abort,					\
	reset: aic7xxx_reset,					\
	slave_attach: NULL,					\
	bios_param: AIC7XXX_BIOSPARAM,				\
	can_queue: 255,		/* max simultaneous cmds      */\
	this_id: -1,		/* scsi id of host adapter    */\
	sg_tablesize: 0,	/* max scatter-gather cmds    */\
	cmd_per_lun: 3,		/* cmds per lun (linked cmds) */\
	present: 0,		/* number of 7xxx's present   */\
	unchecked_isa_dma: 0,	/* no memory DMA restrictions */\
	use_clustering: ENABLE_CLUSTERING,			\
	use_new_eh_code: 0					\
}
#else
#define AIC7XXX	{						\
	next: NULL,						\
	usage_count: NULL,					\
	proc_dir: NULL, 					\
	proc_info: aic7xxx_proc_info,				\
	name: NULL,						\
	detect: aic7xxx_detect,					\
	release: aic7xxx_release,				\
	info: aic7xxx_info,					\
	command: NULL,						\
	queuecommand: aic7xxx_queue,				\
	abort: aic7xxx_abort,					\
	reset: aic7xxx_reset,					\
	slave_attach: NULL,					\
	bios_param: AIC7XXX_BIOSPARAM,				\
	can_queue: 255,		/* max simultaneous cmds      */\
	this_id: -1,		/* scsi id of host adapter    */\
	sg_tablesize: 0,	/* max scatter-gather cmds    */\
	cmd_per_lun: 3,		/* cmds per lun (linked cmds) */\
	present: 0,		/* number of 7xxx's present   */\
	unchecked_isa_dma: 0,	/* no memory DMA restrictions */\
	use_clustering: ENABLE_CLUSTERING			\
}
#endif

extern int aic7xxx_queue(Scsi_Cmnd *, void (*)(Scsi_Cmnd *));
extern int aic7xxx_biosparam(Disk *, kdev_t, int[]);
extern int aic7xxx_detect(Scsi_Host_Template *);
extern int aic7xxx_command(Scsi_Cmnd *);
extern int aic7xxx_reset(Scsi_Cmnd *, unsigned int);
extern int aic7xxx_abort(Scsi_Cmnd *);
extern int aic7xxx_release(struct Scsi_Host *);

extern const char *aic7xxx_info(struct Scsi_Host *);

extern int aic7xxx_proc_info(char *, char **, off_t, int, int, int);

#endif /* _aic7xxx_h */
