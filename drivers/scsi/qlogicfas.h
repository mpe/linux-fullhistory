#ifndef _QLOGICFAS_H
#define _QLOGICFAS_H

int qlogicfas_detect(Scsi_Host_Template * );
const char * qlogicfas_info(struct Scsi_Host *);
int qlogicfas_command(Scsi_Cmnd *);
int qlogicfas_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int qlogicfas_abort(Scsi_Cmnd *);
int qlogicfas_reset(Scsi_Cmnd *);
int qlogicfas_biosparam(Disk *, kdev_t, int[]);

#ifndef NULL
#define NULL (0)
#endif

#define QLOGICFAS {		\
	NULL,			\
	NULL,			\
	NULL,			\
	NULL,		        \
	NULL,			\
	qlogicfas_detect,		\
	NULL,			\
	qlogicfas_info,		\
	qlogicfas_command, 	\
	qlogicfas_queuecommand,	\
	qlogicfas_abort,		\
	qlogicfas_reset,		\
	NULL,			\
	qlogicfas_biosparam,	\
	0,			\
	-1,			\
	SG_ALL,			\
	1,			\
	0,			\
	0,			\
	DISABLE_CLUSTERING	\
}

#endif /* _QLOGICFAS_H */



