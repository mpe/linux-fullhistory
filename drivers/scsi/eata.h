/*
 *	eata.h - used by the low-level driver for EATA/DMA SCSI host adapters.
 *
 */
#ifndef _EATA_H
#define _EATA_H

#include <linux/scsicam.h>

#define EATA_VERSION "1.11.00"

int eata_detect(Scsi_Host_Template *);
int eata_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int eata_abort(Scsi_Cmnd *);
int eata_reset(Scsi_Cmnd *);

#define EATA {  NULL, /* Ptr for modules */                    \
                NULL, /* usage count for modules */	       \
                "EATA/DMA 2.0A rev. " EATA_VERSION " by "      \
                "Dario_Ballabio@milano.europe.dg.com.",        \
                eata_detect,	        	               \
                NULL, /* Release */     	               \
		NULL,	                                       \
		NULL,    			       	       \
		eata_queuecommand,			       \
		eata_abort,				       \
		eata_reset,				       \
	        NULL,		                               \
		scsicam_bios_param,   			       \
		0,   /* can_queue, reset by detect */          \
                7,   /* this_id, reset by detect */            \
                0,   /* sg_tablesize, reset by detect */       \
                0,   /* cmd_per_lun, reset by detect */        \
		0,   /* number of boards present */            \
                0,   /* unchecked isa dma, reset by detect */  \
                ENABLE_CLUSTERING                              \
                }
#endif
