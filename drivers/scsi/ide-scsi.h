/*
 * linux/drivers/scsi/ide-scsi.h
 *
 * Copyright (C) 1996, 1997 Gadi Oxman <gadio@netvision.net.il>
 */

#ifndef IDESCSI_H
#define IDESCSI_H

extern int idescsi_detect (Scsi_Host_Template *host_template);
extern int idescsi_release (struct Scsi_Host *host);
extern const char *idescsi_info (struct Scsi_Host *host);
extern int idescsi_queue (Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *));
extern int idescsi_abort (Scsi_Cmnd *cmd);
extern int idescsi_reset (Scsi_Cmnd *cmd, unsigned int resetflags);
extern int idescsi_bios (Disk *disk, kdev_t dev, int *parm);

#define IDESCSI								\
{	NULL,			/* next		*/			\
	NULL,			/* module	*/			\
	NULL,			/* proc_dir	*/			\
	NULL,			/* proc_info	*/			\
	"idescsi",		/* name		*/			\
	idescsi_detect,		/* detect	*/			\
	idescsi_release,	/* release	*/			\
	idescsi_info,		/* info		*/			\
	NULL,			/* command	*/			\
	idescsi_queue,		/* queuecommand */			\
	idescsi_abort,		/* abort	*/			\
	idescsi_reset,		/* reset	*/			\
	NULL,			/* slave_attach	*/			\
	idescsi_bios,		/* bios_param	*/			\
	10,			/* can_queue	*/			\
	-1,			/* this_id	*/			\
	256,			/* sg_tablesize	*/			\
	5,			/* cmd_per_lun	*/			\
	0,			/* present	*/			\
	0,			/* isa_dma	*/			\
	DISABLE_CLUSTERING	/* clustering	*/			\
}

#endif /* IDESCSI_H */
