/*
 *	eata.h - used by low-level scsi driver for EISA EATA controllers.
 *
 */
#ifndef _EISA_EATA_H
#define _EISA_EATA_H

#define EATA_VERSION "1.07.00"

int eata_detect(Scsi_Host_Template *);
int eata_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int eata_abort(Scsi_Cmnd *);
const char *eata_info(void);
int eata_reset(Scsi_Cmnd *);
int eata_bios_param(Disk *, int, int*);

#define EATA {  NULL, /* Ptr for modules */                    \
                "EISA EATA 2.0A rev. " EATA_VERSION " by "     \
                "Dario_Ballabio@milano.europe.dg.com.",\
                eata_detect,	        	               \
                NULL, /* Release */     	               \
		eata_info,                                     \
		NULL,    			       	       \
		eata_queuecommand,			       \
		eata_abort,				       \
		eata_reset,				       \
	        NULL,		                               \
		eata_bios_param,   			       \
		0,   /* can_queue, reset by detect */          \
                7,   /* this_id, reset by detect */            \
                0,   /* sg_tablesize, reset by detect */       \
                0,   /* cmd_per_lun, reset by detect */        \
		0,   /* number of boards present */            \
                0,   /* unchecked isa dma */                   \
                ENABLE_CLUSTERING                              \
                }
#endif
