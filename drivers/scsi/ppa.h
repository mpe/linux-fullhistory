#ifndef _PPA_H
#define _PPA_H

/*  Driver for the PPA3 parallel port SCSI HBA embedded in 
    the Iomega ZIP drive

	(c) 1996	Grant R. Guenther  grant@torque.net
*/

#define	PPA_INITIATOR	7

int ppa_detect(Scsi_Host_Template * );
const char * ppa_info(struct Scsi_Host *);
int ppa_command(Scsi_Cmnd *);
int ppa_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int ppa_abort(Scsi_Cmnd *);
int ppa_reset(Scsi_Cmnd *);
int ppa_biosparam(Disk *, kdev_t, int[]);

#define PPA {			\
	0,			\
	0,			\
	0,			\
	0,		        \
	0,			\
	ppa_detect,		\
	0,			\
	ppa_info,		\
	ppa_command, 		\
	ppa_queuecommand,	\
	ppa_abort,		\
	ppa_reset,		\
	0,			\
	ppa_biosparam,		\
	0,			\
	PPA_INITIATOR,		\
	SG_NONE,		\
	1,			\
	0,			\
	0,			\
	DISABLE_CLUSTERING	\
}

#endif /* _PPA_H */
