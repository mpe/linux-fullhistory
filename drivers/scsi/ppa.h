/*  Driver for the PPA3 parallel port SCSI HBA embedded in 
 * the Iomega ZIP drive
 * 
 * (c) 1996     Grant R. Guenther  grant@torque.net
 */

#ifndef _PPA_H
#define _PPA_H

#define   PPA_VERSION   "Curtin 1-08-BETA"

/* This driver reqires a 1.3.37 kernel or higher!! */

/* Use the following to enable certain chipset support
 * Default is PEDANTIC = 3
 */

#include <linux/config.h>

#ifndef CONFIG_SCSI_PPA_HAVE_PEDANTIC
#define CONFIG_SCSI_PPA_HAVE_PEDANTIC	3
#endif
#ifndef CONFIG_SCSI_PPA_EPP_TIME
#define CONFIG_SCSI_PPA_EPP_TIME	64
#endif

/* ------ END OF USER CONFIGURABLE PARAMETERS ----- */

#include  <linux/stddef.h>
#include  <linux/module.h>
#include  <linux/kernel.h>
#include  <linux/tqueue.h>
#include  <linux/ioport.h>
#include  <linux/delay.h>
#include  <linux/proc_fs.h>
#include  <linux/stat.h>
#include  <linux/blk.h>

#include  <asm/io.h>
#include  "sd.h"
#include  "hosts.h"
#include  <linux/parport.h>
/* batteries not included :-) */

#define	PPA_INITIATOR	7

int ppa_detect(Scsi_Host_Template *);
const char *ppa_info(struct Scsi_Host *);
int ppa_command(Scsi_Cmnd *);
int ppa_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int ppa_abort(Scsi_Cmnd *);
int ppa_reset(Scsi_Cmnd *, unsigned int);
int ppa_proc_info(char *, char **, off_t, int, int, int);
int ppa_biosparam(Disk *, kdev_t, int*);
static int ppa_release(struct Scsi_Host *);

#ifndef	MODULE
#ifdef	PPA_CODE
#define SKIP_PROC_DIR
#endif
#endif

#ifndef SKIP_PROC_DIR
struct proc_dir_entry proc_scsi_ppa =
{PROC_SCSI_PPA, 3, "ppa", S_IFDIR | S_IRUGO | S_IXUGO, 2};
#endif /* !PPA_CODE => hosts.c */

#define PPA {	/* next */	 	0, \
		/* usage_count */	0, \
		/* proc_dir */		&proc_scsi_ppa, \
		/* proc_info */		ppa_proc_info, \
		/* name */		"Iomega ZIP/JAZ Traveller", \
		/* detect */		ppa_detect, \
		/* release */		ppa_release, \
		/* info */		0, \
		/* command */		ppa_command, \
		/* queuecommand */	ppa_queuecommand, \
		/* abort */		ppa_abort, \
		/* reset */		ppa_reset, \
		/* slave_attach */	0, \
		/* bios_param */	ppa_biosparam, \
		/* can_queue */		0, \
		/* this_id */		PPA_INITIATOR, \
		/* sg_tablesize */	SG_ALL, \
		/* cmd_per_lun */	1, \
		/* present */		0, \
		/* unchecked_isa_dma */	0, \
		/* use_clustering */	ENABLE_CLUSTERING \
}
#endif				/* _PPA_H */
