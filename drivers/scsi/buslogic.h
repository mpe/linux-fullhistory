/*
 *	buslogic.h	(C) 1993 David B. Gentzel
 *	Low-level scsi driver for BusLogic adapters
 *	by David B. Gentzel, Whitfield Software Services, Carnegie, PA
 *	    (gentzel@nova.enet.dec.com)
 *	Thanks to BusLogic for providing the necessary documentation
 *
 *	The original version of this driver was derived from aha1542.[ch] which
 *	is Copyright (C) 1992 Tommy Thorn.  Much has been reworked, but most of
 *	basic structure and substantial chunks of code still remain.
 */

#ifndef _BUSLOGIC_H

int buslogic_detect(Scsi_Host_Template *);
int buslogic_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int buslogic_abort(Scsi_Cmnd *);
const char *buslogic_info(void);
int buslogic_reset(Scsi_Cmnd *);
int buslogic_biosparam(Disk *, int, int *);

#define BUSLOGIC_CMDLUN 1	/* ??? */

#define BUSLOGIC { NULL, 			\
		   "BusLogic",			\
		   buslogic_detect,		\
		   NULL,			\
		   buslogic_info,		\
		   0,	/* no command func */	\
		   buslogic_queuecommand,	\
		   buslogic_abort,		\
		   buslogic_reset,		\
		   0,	/* slave_attach NYI */	\
		   buslogic_biosparam,		\
		   0,	/* set by driver */	\
		   0,	/* set by driver */	\
		   0,	/* set by driver */	\
		   BUSLOGIC_CMDLUN,		\
		   0,				\
		   0,	/* set by driver */	\
		   ENABLE_CLUSTERING		\
		 }

#ifdef BUSLOGIC_PRIVATE_H

/* ??? These don't really belong here */
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#define ARRAY_SIZE(arr) (sizeof (arr) / sizeof (arr)[0])

#define PACKED __attribute__((packed))

#define BD_ABORT	0x0001
#define BD_COMMAND	0x0002
#define BD_DETECT	0x0004
#define BD_INTERRUPT	0x0008
#define BD_RESET	0x0010

/* I/O Port interface */
/* READ */
#define STATUS(base) (base)
#define DACT 0x80		/* Diagnostic Active */
#define DFAIL 0x40		/* Diagonostic Failure */
#define INREQ 0x20		/* Initialization Required */
#define HARDY 0x10		/* Host Adapter Ready */
#define CPRBSY 0x08		/* Command/Parameter Register Busy */
#define DIRRDY 0x04		/* Data In Register Ready */
#define CMDINV 0x01		/* Command Invalid */
#define STATMASK 0xFD		/* 0x02 is reserved */

#define DATA_IN(base) (STATUS(base) + 1)

#define INTERRUPT(base) (STATUS(base) + 2)
#define INTV 0x80		/* Interrupt Valid */
#define RSTS 0x08		/* SCSI Reset State */
#define CMDC 0x04		/* Command Complete */
#define MBOR 0x02		/* Mailbox Out Ready */
#define IMBL 0x01		/* Incoming Mailbox Loaded */
#define INTRMASK 0x8F		/* 0x70 are reserved */

/* WRITE */
#define CONTROL(base) STATUS(base)
#define RHARD 0x80		/* Hard Reset */
#define RSOFT 0x40		/* Soft Reset */
#define RINT 0x20		/* Interrupt Reset */
#define RSBUS 0x10		/* SCSI Bus Reset */

#define COMMAND_PARAMETER(base) (STATUS(base) + 1)
#define CMD_TSTCMDCINT 0x00	/* Test CMDC Interrupt */
#define CMD_INITMB 0x01		/* Initialize Mailbox */
#define CMD_START_SCSI 0x02	/* Start Mailbox */
#define CMD_START_BIOS 0x03	/* Start BIOS */
#define CMD_INQUIRY 0x04	/* Inquire Board ID */
#define CMD_ENBOMBRINT 0x05	/* Enable OMBR Interrupt */
#define CMD_SETSELTIMOUT 0x06	/* Set SCSI Selection Time-Out */
#define CMD_BUSON_TIME 0x07	/* Set Bus-On Time */
#define CMD_BUSOFF_TIME 0x08	/* Set Bus-Off Time */
#define CMD_BUSXFR_RATE 0x09	/* Set Bus Transfer Rate */
#define CMD_INQ_DEVICES 0x0A	/* Inquire Installed Devices */
#define CMD_RETCONF 0x0B	/* Return Configuration */
#define CMD_TARGET_MODE 0x0C	/* Set Target Mode */
#define CMD_INQ_SETUP_INFO 0x0D	/* Inquire Set-up Information */
#define CMD_WRITE_LCL_RAM 0x1A	/* Write Adapter Local RAM */
#define CMD_READ_LCL_RAM 0x1B	/* Read Adapter Local RAM */
#define CMD_WRITE_BM_FIFO 0x1C	/* Write Bus Master Chip FIFO */
#define CMD_READ_BM_FIFO 0x1D	/* Read Bus Master Chip FIFO */
#define CMD_ECHO 0x1F		/* Echo Data Byte */
#define CMD_HA_DIAG 0x20	/* Host Adapter Diagnostic */
#define CMD_HA_OPTIONS 0x21	/* Host Adapter Options */
#define CMD_INITEXTMB 0x81	/* Initialize Extended Mailbox */
#define CMD_INQEXTSETUP 0x8D	/* Inquire Extended Set-up Information */
#define CMD_WRITE_INQ_BUF 0x9A	/* Write Inquery Data Buffer
				   (Target Mode Only) */
#define CMD_READ_INQ_BUF 0x9B	/* Read Inquery Data Buffer
				   (Target Mode Only) */

#define MBX_NOT_IN_USE 0x00
#define MBX_ACTION_START 0x01
#define MBX_ACTION_ABORT 0x02
#define MBX_COMPLETION_OK 0x01
#define MBX_COMPLETION_ABORTED 0x02
#define MBX_COMPLETION_NOT_FOUND 0x03
#define MBX_COMPLETION_ERROR 0x04

/* Mailbox Definition */
struct mailbox {
    void *ccbptr;		/* lsb, ..., msb */
    unsigned char btstat;
    unsigned char sdstat;
    unsigned char reserved;
    unsigned char status;	/* Command/Status */
};

/* This is used with scatter-gather */
struct chain {
    unsigned long datalen;	/* Size of this part of chain */
    void *dataptr;		/* Location of data */
};

#define MAX_CDB 12

struct ccb {			/* Command Control Block */
    unsigned char op;		/* Command Control Block Operation Code */
    unsigned char dir;
    unsigned char cdblen;	/* SCSI Command Length */
    unsigned char rsalen;	/* Request Sense Allocation Length/Disable */
    unsigned long datalen;	/* Data Length (msb, ..., lsb) */
    void *dataptr;		/* Data Pointer */
    unsigned char reserved[2];
    unsigned char hastat;	/* Host Adapter Status (HASTAT) */
    unsigned char tarstat;	/* Target Device Status */
    unsigned char id;
    unsigned char lun;
    unsigned char cdb[MAX_CDB];
    unsigned char ccbcontrol;
    unsigned char commlinkid;	/* Command Linking Identifier */
    void *linkptr;		/* Link Pointer */
    void *senseptr;
};

#define CCB_OP_INIT 0x00	/* Initiator CCB */
#define CCB_OP_TARG 0x01	/* Target CCB */
#define CCB_OP_INIT_SG 0x02	/* Initiator CCB with scatter-gather */
#define CCB_OP_INIT_R 0x03	/* Initiator CCB with residual data length
				   returned */
#define CCB_OP_INIT_SG_R 0x04	/* Initiator CCB with scatter-gather and
				   residual data length returned */
#define CCB_OP_BUS_RESET 0x81	/* SCSI bus device reset */

#endif

#endif
