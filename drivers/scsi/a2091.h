#ifndef A2091_H

/* $Id: a2091.h,v 1.4 1996/04/25 20:57:48 root Exp root $
 *
 * Header file for the Commodore A2091 Zorro II SCSI controller for Linux
 *
 * Written and (C) 1993, Hamish Macdonald, see a2091.c for more info
 *
 */

#include <linux/types.h>

int a2091_detect(Scsi_Host_Template *);
const char *wd33c93_info(void);
int wd33c93_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int wd33c93_abort(Scsi_Cmnd *);
int wd33c93_reset(Scsi_Cmnd *, unsigned int);

#ifndef NULL
#define NULL 0
#endif

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#ifdef HOSTS_C

extern struct proc_dir_entry proc_scsi_a2091;

#define A2091_SCSI {  /* next */                NULL,            \
		      /* usage_count */         NULL,	         \
		      /* proc_dir_entry */      &proc_scsi_a2091, \
		      /* proc_info */           NULL,            \
		      /* name */                "Commodore A2091/A590 SCSI", \
		      /* detect */              a2091_detect,    \
		      /* release */             NULL,            \
		      /* info */                NULL,	         \
		      /* command */             NULL,            \
		      /* queuecommand */        wd33c93_queuecommand, \
		      /* abort */               wd33c93_abort,   \
		      /* reset */               wd33c93_reset,   \
		      /* slave_attach */        NULL,            \
		      /* bios_param */          NULL, 	         \
		      /* can_queue */           CAN_QUEUE,       \
		      /* this_id */             7,               \
		      /* sg_tablesize */        SG_ALL,          \
		      /* cmd_per_lun */	        CMD_PER_LUN,     \
		      /* present */             0,               \
		      /* unchecked_isa_dma */   0,               \
		      /* use_clustering */      DISABLE_CLUSTERING }
#else

/*
 * if the transfer address ANDed with this results in a non-zero
 * result, then we can't use DMA.
 */ 
#define A2091_XFER_MASK  (0xff000001)

typedef struct {
             unsigned char      pad1[64];
    volatile unsigned short     ISTR;
    volatile unsigned short     CNTR;
             unsigned char      pad2[60];
    volatile unsigned int       WTC;
    volatile unsigned long      ACR;
             unsigned char      pad3[6];
    volatile unsigned short     DAWR;
             unsigned char      pad4;
    volatile unsigned char      SASR;
             unsigned char      pad5;
    volatile unsigned char      SCMD;
             unsigned char      pad6[76];
    volatile unsigned short     ST_DMA;
    volatile unsigned short     SP_DMA;
    volatile unsigned short     CINT;
             unsigned char      pad7[2];
    volatile unsigned short     FLUSH;
} a2091_scsiregs;

#define DAWR_A2091		(3)

/* CNTR bits. */
#define CNTR_TCEN		(1<<7)
#define CNTR_PREST		(1<<6)
#define CNTR_PDMD		(1<<5)
#define CNTR_INTEN		(1<<4)
#define CNTR_DDIR		(1<<3)

/* ISTR bits. */
#define ISTR_INTX		(1<<8)
#define ISTR_INT_F		(1<<7)
#define ISTR_INTS		(1<<6)
#define ISTR_E_INT		(1<<5)
#define ISTR_INT_P		(1<<4)
#define ISTR_UE_INT		(1<<3)
#define ISTR_OE_INT		(1<<2)
#define ISTR_FF_FLG		(1<<1)
#define ISTR_FE_FLG		(1<<0)

#endif /* else def HOSTS_C */

#endif /* A2091_H */
