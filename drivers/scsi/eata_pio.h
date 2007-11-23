/********************************************************
* Header file for eata_pio.c Linux EATA-PIO SCSI driver *
* (c) 1993-96 Michael Neuffer  	                        *
*********************************************************
* last change: 96/05/05					*
********************************************************/


#ifndef _EATA_PIO_H
#define _EATA_PIO_H

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsicam.h>

#ifndef HOSTS_C
#include "eata_generic.h"

#define VER_MAJOR 0
#define VER_MINOR 0
#define VER_SUB	  "1b"

/************************************************************************
 * Here you can switch parts of the code on and of			*
 ************************************************************************/

#define VERBOSE_SETUP			/* show startup screen of 2001 */
#define ALLOW_DMA_BOARDS 1

/************************************************************************
 * Debug options.							* 
 * Enable DEBUG and whichever options you require.			*
 ************************************************************************/
#define DEBUG_EATA	1   /* Enable debug code.			*/
#define DPT_DEBUG	0   /* Bobs special				*/
#define DBG_DELAY	0   /* Build in delays so debug messages can be
                             * be read before they vanish of the top of
                             * the screen!
                             */
#define DBG_PROBE	0   /* Debug probe routines.			*/
#define DBG_ISA		0   /* Trace ISA routines			*/ 
#define DBG_EISA	0   /* Trace EISA routines			*/ 
#define DBG_PCI		0   /* Trace PCI routines			*/ 
#define DBG_PIO		0   /* Trace get_config_PIO			*/
#define DBG_COM		0   /* Trace command call			*/
#define DBG_QUEUE	0   /* Trace command queueing.			*/
#define DBG_INTR	0   /* Trace interrupt service routine.		*/
#define DBG_INTR2	0   /* Trace interrupt service routine.		*/
#define DBG_PROC	0   /* Debug proc-fs related statistics		*/
#define DBG_PROC_WRITE	0
#define DBG_REGISTER	0   /* */
#define DBG_ABNORM	1   /* Debug abnormal actions (reset, abort)	*/

#if DEBUG_EATA 
#define DBG(x, y)   if ((x)) {y;} 
#else
#define DBG(x, y)
#endif

#endif /* !HOSTS_C */

int eata_pio_detect(Scsi_Host_Template *);
const char *eata_pio_info(struct Scsi_Host *);
int eata_pio_command(Scsi_Cmnd *);
int eata_pio_queue(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int eata_pio_abort(Scsi_Cmnd *);
int eata_pio_reset(Scsi_Cmnd *, unsigned int);
int eata_pio_proc_info(char *, char **, off_t, int, int, int);
#ifdef MODULE
int eata_pio_release(struct Scsi_Host *);
#else
#define eata_pio_release NULL  
#endif


#define EATA_PIO {	         \
    NULL, NULL,                  \
    NULL,               /* proc_dir_entry */ \
    eata_pio_proc_info, /* procinfo	  */ \
    "EATA (Extended Attachment) PIO driver", \
    eata_pio_detect,		 \
    eata_pio_release,		 \
    NULL, NULL,			 \
    eata_pio_queue,		 \
    eata_pio_abort,		 \
    eata_pio_reset,		 \
    NULL,   /* Slave attach */	 \
    scsicam_bios_param,		 \
    0,	    /* Canqueue	    */	 \
    0,	    /* this_id	    */	 \
    0,	    /* sg_tablesize */	 \
    0,	    /* cmd_per_lun  */	 \
    0,	    /* present	    */	 \
    1,	    /* True if ISA  */	 \
    ENABLE_CLUSTERING }

#endif /* _EATA_PIO_H */

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * tab-width: 8
 * End:
 */
