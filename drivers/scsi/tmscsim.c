/***********************************************************************
 *	FILE NAME : TMSCSIM.C					       *
 *	     BY   : C.L. Huang,  ching@tekram.com.tw		       *
 *	Description: Device Driver for Tekram DC-390(T) PCI SCSI       *
 *		     Bus Master Host Adapter			       *
 * (C)Copyright 1995-1996 Tekram Technology Co., Ltd.		       *
 ***********************************************************************/
/*	Minor enhancements and bugfixes by				*
 *	Kurt Garloff <K.Garloff@ping.de>				*
 ***********************************************************************/
/*	HISTORY:							*
 *									*
 *	REV#	DATE	NAME	DESCRIPTION				*
 *	1.00  04/24/96	CLH	First release				*
 *	1.01  06/12/96	CLH	Fixed bug of Media Change for Removable *
 *				Device, scan all LUN. Support Pre2.0.10 *
 *	1.02  06/18/96	CLH	Fixed bug of Command timeout ...	*
 *	1.03  09/25/96	KG	Added tmscsim_proc_info()		*
 *	1.04  10/11/96	CLH	Updating for support KV 2.0.x		*
 *	1.05  10/18/96	KG	Fixed bug in DC390_abort(null ptr deref)*
 *	1.06  10/25/96	KG	Fixed module support			*
 *	1.07  11/09/96	KG	Fixed tmscsim_proc_info()		*
 *	1.08  11/18/96	KG	Fixed null ptr in DC390_Disconnect()	*
 *	1.09  11/30/96	KG	Added register the allocated IO space	*
 *	1.10  12/05/96	CLH	Modified tmscsim_proc_info(), and reset *
 *				pending interrupt in DC390_detect()	*
 * 	1.11  02/05/97	KG/CLH	Fixeds problem with partitions greater	*
 * 				than 1GB				*
 *      1.12  15/02/98  MJ      Rewritten PCI probing			*
 ***********************************************************************/


#define DC390_DEBUG

#define SCSI_MALLOC

#ifdef MODULE
# include <linux/module.h>
#endif

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/blk.h>

#include "scsi.h"
#include "hosts.h"
#include "tmscsim.h"
#include "constants.h"
#include "sd.h"
#include <linux/stat.h>

#include "dc390.h"

#define PCI_DEVICE_ID_AMD53C974 	PCI_DEVICE_ID_AMD_SCSI

struct proc_dir_entry	proc_scsi_tmscsim ={
       PROC_SCSI_DC390T, 7 ,"tmscsim",
       S_IFDIR | S_IRUGO | S_IXUGO, 2
       };

static USHORT DC390_StartSCSI( PACB pACB, PDCB pDCB, PSRB pSRB );
static void DC390_DataOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_DataIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_Command_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_Status_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_MsgOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_MsgIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_DataOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_DataInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_CommandPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_StatusPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_MsgOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_MsgInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_Nop_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void DC390_Nop_1( PACB pACB, PSRB pSRB, PUCHAR psstatus);

static void SetXferRate( PACB pACB, PDCB pDCB );
static void DC390_Disconnect( PACB pACB );
static void DC390_Reselect( PACB pACB );
static void SRBdone( PACB pACB, PDCB pDCB, PSRB pSRB );
static void DoingSRB_Done( PACB pACB );
static void DC390_ScsiRstDetect( PACB pACB );
static void DC390_ResetSCSIBus( PACB pACB );
static void RequestSense( PACB pACB, PDCB pDCB, PSRB pSRB );
static void EnableMsgOut2( PACB pACB, PSRB pSRB );
static void EnableMsgOut( PACB pACB, PSRB pSRB );
static void DC390_InvalidCmd( PACB pACB );

int    DC390_initAdapter( PSH psh, ULONG io_port, UCHAR Irq, USHORT index );
void   DC390_initDCB( PACB pACB, PDCB pDCB, PSCSICMD cmd );

#ifdef MODULE
static int DC390_release(struct Scsi_Host *host);
static int DC390_shutdown (struct Scsi_Host *host);
#endif


static PSHT	pSHT_start = NULL;
static PSH	pSH_start = NULL;
static PSH	pSH_current = NULL;
static PACB	pACB_start= NULL;
static PACB	pACB_current = NULL;
static PDCB	pPrevDCB = NULL;
static USHORT	adapterCnt = 0;
static USHORT	InitialTime = 0;
static USHORT	CurrSyncOffset = 0;

static PVOID DC390_phase0[]={
       DC390_DataOut_0,
       DC390_DataIn_0,
       DC390_Command_0,
       DC390_Status_0,
       DC390_Nop_0,
       DC390_Nop_0,
       DC390_MsgOut_0,
       DC390_MsgIn_0,
       DC390_Nop_1
       };

static PVOID DC390_phase1[]={
       DC390_DataOutPhase,
       DC390_DataInPhase,
       DC390_CommandPhase,
       DC390_StatusPhase,
       DC390_Nop_0,
       DC390_Nop_0,
       DC390_MsgOutPhase,
       DC390_MsgInPhase,
       DC390_Nop_1,
       };

UCHAR  eepromBuf[MAX_ADAPTER_NUM][128];


UCHAR  clock_period1[] = {4, 5, 6, 7, 8, 10, 13, 20};

UCHAR  baddevname1[2][28] ={
       "SEAGATE ST3390N         9546",
       "HP      C3323-300       4269"};

#define BADDEVCNT	2


/***********************************************************************
 *
 *
 *
 **********************************************************************/
static void
QLinkcmd( PSCSICMD cmd, PDCB pDCB )
{
    ULONG  flags;
    PSCSICMD  pcmd;

    save_flags(flags);
    cli();

    if( !pDCB->QIORBCnt )
    {
	pDCB->pQIORBhead = cmd;
	pDCB->pQIORBtail = cmd;
	pDCB->QIORBCnt++;
	cmd->next = NULL;
    }
    else
    {
	pcmd = pDCB->pQIORBtail;
	pcmd->next = cmd;
	pDCB->pQIORBtail = cmd;
	pDCB->QIORBCnt++;
	cmd->next = NULL;
    }

    restore_flags(flags);
}


static PSCSICMD
Getcmd( PDCB pDCB )
{
    ULONG  flags;
    PSCSICMD  pcmd;

    save_flags(flags);
    cli();

    pcmd = pDCB->pQIORBhead;
    pDCB->pQIORBhead = pcmd->next;
    pcmd->next = NULL;
    pDCB->QIORBCnt--;

    restore_flags(flags);
    return( pcmd );
}


static PSRB
GetSRB( PACB pACB )
{
    ULONG  flags;
    PSRB   pSRB;

    save_flags(flags);
    cli();

    pSRB = pACB->pFreeSRB;
    if( pSRB )
    {
	pACB->pFreeSRB = pSRB->pNextSRB;
	pSRB->pNextSRB = NULL;
    }
    restore_flags(flags);
    return( pSRB );
}


static void
RewaitSRB0( PDCB pDCB, PSRB pSRB )
{
    PSRB   psrb1;
    ULONG  flags;

    save_flags(flags);
    cli();

    if( (psrb1 = pDCB->pWaitingSRB) )
    {
	pSRB->pNextSRB = psrb1;
	pDCB->pWaitingSRB = pSRB;
    }
    else
    {
	pSRB->pNextSRB = NULL;
	pDCB->pWaitingSRB = pSRB;
	pDCB->pWaitLast = pSRB;
    }
    restore_flags(flags);
}


static void
RewaitSRB( PDCB pDCB, PSRB pSRB )
{
    PSRB   psrb1;
    ULONG  flags;
    UCHAR  bval;

    save_flags(flags);
    cli();
    pDCB->GoingSRBCnt--;
    psrb1 = pDCB->pGoingSRB;
    if( pSRB == psrb1 )
    {
	pDCB->pGoingSRB = psrb1->pNextSRB;
    }
    else
    {
	while( pSRB != psrb1->pNextSRB )
	    psrb1 = psrb1->pNextSRB;
	psrb1->pNextSRB = pSRB->pNextSRB;
	if( pSRB == pDCB->pGoingLast )
	    pDCB->pGoingLast = psrb1;
    }
    if( (psrb1 = pDCB->pWaitingSRB) )
    {
	pSRB->pNextSRB = psrb1;
	pDCB->pWaitingSRB = pSRB;
    }
    else
    {
	pSRB->pNextSRB = NULL;
	pDCB->pWaitingSRB = pSRB;
	pDCB->pWaitLast = pSRB;
    }

    bval = pSRB->TagNumber;
    pDCB->TagMask &= (~(1 << bval));	  /* Free TAG number */
    restore_flags(flags);
}


static void
DoWaitingSRB( PACB pACB )
{
    ULONG  flags;
    PDCB   ptr, ptr1;
    PSRB   pSRB;

    save_flags(flags);
    cli();

    if( !(pACB->pActiveDCB) && !(pACB->ACBFlag & (RESET_DETECT+RESET_DONE+RESET_DEV) ) )
    {
	ptr = pACB->pDCBRunRobin;
	if( !ptr )
	{
	    ptr = pACB->pLinkDCB;
	    pACB->pDCBRunRobin = ptr;
	}
	ptr1 = ptr;
	for( ;ptr1; )
	{
	    pACB->pDCBRunRobin = ptr1->pNextDCB;
	    if( !( ptr1->MaxCommand > ptr1->GoingSRBCnt ) ||
		!( pSRB = ptr1->pWaitingSRB ) )
	    {
		if(pACB->pDCBRunRobin == ptr)
		    break;
		ptr1 = ptr1->pNextDCB;
	    }
	    else
	    {
		if( !DC390_StartSCSI(pACB, ptr1, pSRB) )
		{
		    ptr1->GoingSRBCnt++;
		    if( ptr1->pWaitLast == pSRB )
		    {
			ptr1->pWaitingSRB = NULL;
			ptr1->pWaitLast = NULL;
		    }
		    else
		    {
			ptr1->pWaitingSRB = pSRB->pNextSRB;
		    }
		    pSRB->pNextSRB = NULL;

		    if( ptr1->pGoingSRB )
			ptr1->pGoingLast->pNextSRB = pSRB;
		    else
			ptr1->pGoingSRB = pSRB;
		    ptr1->pGoingLast = pSRB;
		}
		break;
	    }
	}
    }
    restore_flags(flags);
    return;
}


static void
SRBwaiting( PDCB pDCB, PSRB pSRB)
{
    if( pDCB->pWaitingSRB )
    {
	pDCB->pWaitLast->pNextSRB = pSRB;
	pDCB->pWaitLast = pSRB;
	pSRB->pNextSRB = NULL;
    }
    else
    {
	pDCB->pWaitingSRB = pSRB;
	pDCB->pWaitLast = pSRB;
    }
}


static void
SendSRB( PSCSICMD pcmd, PACB pACB, PSRB pSRB )
{
    ULONG  flags;
    PDCB   pDCB;

    save_flags(flags);
    cli();

    pDCB = pSRB->pSRBDCB;
    if( !(pDCB->MaxCommand > pDCB->GoingSRBCnt) || (pACB->pActiveDCB) ||
	(pACB->ACBFlag & (RESET_DETECT+RESET_DONE+RESET_DEV)) )
    {
	SRBwaiting(pDCB, pSRB);
	goto SND_EXIT;
    }

    if( pDCB->pWaitingSRB )
    {
	SRBwaiting(pDCB, pSRB);
/*	pSRB = GetWaitingSRB(pDCB); */
	pSRB = pDCB->pWaitingSRB;
	pDCB->pWaitingSRB = pSRB->pNextSRB;
	pSRB->pNextSRB = NULL;
    }

    if( !DC390_StartSCSI(pACB, pDCB, pSRB) )
    {
	pDCB->GoingSRBCnt++;
	if( pDCB->pGoingSRB )
	{
	    pDCB->pGoingLast->pNextSRB = pSRB;
	    pDCB->pGoingLast = pSRB;
	}
	else
	{
	    pDCB->pGoingSRB = pSRB;
	    pDCB->pGoingLast = pSRB;
	}
    }
    else
	RewaitSRB0( pDCB, pSRB );

SND_EXIT:
    restore_flags(flags);
    return;
}


/***********************************************************************
 * Function : static int DC390_queue_command (Scsi_Cmnd *cmd,
 *					       void (*done)(Scsi_Cmnd *))
 *
 * Purpose : enqueues a SCSI command
 *
 * Inputs : cmd - SCSI command, done - function called on completion, with
 *	    a pointer to the command descriptor.
 *
 * Returns : 0
 *
 ***********************************************************************/

int
DC390_queue_command (Scsi_Cmnd *cmd, void (* done)(Scsi_Cmnd *))
{
    USHORT ioport, i;
    Scsi_Cmnd *pcmd;
    struct Scsi_Host *psh;
    PACB   pACB;
    PDCB   pDCB;
    PSRB   pSRB;
    ULONG  flags;
    PUCHAR ptr,ptr1;

    psh = cmd->host;
    pACB = (PACB ) psh->hostdata;
    ioport = pACB->IOPortBase;

#ifdef DC390_DEBUG0
/*  if(pACB->scan_devices) */
	printk("Cmd=%2x,ID=%d,LUN=%d,",cmd->cmnd[0],cmd->target,cmd->lun);
#endif

    if( (pACB->scan_devices == END_SCAN) && (cmd->cmnd[0] != INQUIRY) )
    {
	pACB->scan_devices = 0;
	pPrevDCB->pNextDCB = pACB->pLinkDCB;
    }
    else if( (pACB->scan_devices) && (cmd->cmnd[0] == 8) )
    {
	pACB->scan_devices = 0;
	pPrevDCB->pNextDCB = pACB->pLinkDCB;
    }

    if ( ( cmd->target > pACB->max_id ) || (cmd->lun > pACB->max_lun) )
    {
/*	printk("DC390: Ignore target %d lun %d\n",
		cmd->target, cmd->lun); */
	cmd->result = (DID_BAD_TARGET << 16);
	done(cmd);
	return( 0 );
    }

    if( (pACB->scan_devices) && !(pACB->DCBmap[cmd->target] & (1 << cmd->lun)) )
    {
	if( pACB->DeviceCnt < MAX_DEVICES )
	{
	    pACB->DCBmap[cmd->target] |= (1 << cmd->lun);
	    pDCB = pACB->pDCB_free;
#ifdef DC390_DEBUG0
	    printk("pDCB=%8x,ID=%2x,", (UINT) pDCB, cmd->target);
#endif
	    DC390_initDCB( pACB, pDCB, cmd );
	}
	else	/* ???? */
	{
/*	    printk("DC390: Ignore target %d lun %d\n",
		    cmd->target, cmd->lun); */
	    cmd->result = (DID_BAD_TARGET << 16);
	    done(cmd);
	    return(0);
	}
    }
    else if( !(pACB->scan_devices) && !(pACB->DCBmap[cmd->target] & (1 << cmd->lun)) )
    {
/*	printk("DC390: Ignore target %d lun %d\n",
		cmd->target, cmd->lun); */
	cmd->result = (DID_BAD_TARGET << 16);
	done(cmd);
	return(0);
    }
    else
    {
	pDCB = pACB->pLinkDCB;
	while( (pDCB->UnitSCSIID != cmd->target) ||
	       (pDCB->UnitSCSILUN != cmd->lun) )
	{
	    pDCB = pDCB->pNextDCB;
	}
#ifdef DC390_DEBUG0
	    printk("pDCB=%8x,ID=%2x,", (UINT) pDCB, cmd->target);
#endif
    }

    cmd->scsi_done = done;
    cmd->result = 0;

    save_flags(flags);
    cli();

    if( pDCB->QIORBCnt )
    {
	QLinkcmd( cmd, pDCB );
	pcmd = Getcmd( pDCB );
    }
    else
	pcmd = cmd;

    pSRB = GetSRB( pACB );

    if( !pSRB )
    {
	QLinkcmd( pcmd, pDCB );
	restore_flags(flags);
	return(0);
    }

/*  BuildSRB(pSRB); */

    pSRB->pSRBDCB = pDCB;
    pSRB->pcmd = pcmd;
    ptr = (PUCHAR) pSRB->CmdBlock;
    ptr1 = (PUCHAR) pcmd->cmnd;
    pSRB->ScsiCmdLen = pcmd->cmd_len;
    for(i=0; i< pcmd->cmd_len; i++)
    {
	*ptr = *ptr1;
	ptr++;
	ptr1++;
    }
    if( pcmd->use_sg )
    {
	pSRB->SGcount = (UCHAR) pcmd->use_sg;
	pSRB->pSegmentList = (PSGL) pcmd->request_buffer;
    }
    else if( pcmd->request_buffer )
    {
	pSRB->SGcount = 1;
	pSRB->pSegmentList = (PSGL) &pSRB->Segmentx;
	pSRB->Segmentx.address = (PUCHAR) pcmd->request_buffer;
	pSRB->Segmentx.length = pcmd->request_bufflen;
    }
    else
	pSRB->SGcount = 0;

    pSRB->SGIndex = 0;
    pSRB->AdaptStatus = 0;
    pSRB->TargetStatus = 0;
    pSRB->MsgCnt = 0;
    if( pDCB->DevType != TYPE_TAPE )
	pSRB->RetryCnt = 1;
    else
	pSRB->RetryCnt = 0;
    pSRB->SRBStatus = 0;
    pSRB->SRBFlag = 0;
    pSRB->SRBState = 0;
    pSRB->TotalXferredLen = 0;
    pSRB->SGPhysAddr = 0;
    pSRB->SGToBeXferLen = 0;
    pSRB->ScsiPhase = 0;
    pSRB->EndMessage = 0;
    SendSRB( pcmd, pACB, pSRB );

    restore_flags(flags);
    return(0);
}


static void
DoNextCmd( PACB pACB, PDCB pDCB )
{
    Scsi_Cmnd *pcmd;
    PSRB   pSRB;
    ULONG  flags;
    PUCHAR ptr,ptr1;
    USHORT i;


    if( pACB->ACBFlag & (RESET_DETECT+RESET_DONE+RESET_DEV) )
	return;
    save_flags(flags);
    cli();

    pcmd = Getcmd( pDCB );
    pSRB = GetSRB( pACB );
    if( !pSRB )
    {
	QLinkcmd( pcmd, pDCB );
	restore_flags(flags);
	return;
    }

    pSRB->pSRBDCB = pDCB;
    pSRB->pcmd = pcmd;
    ptr = (PUCHAR) pSRB->CmdBlock;
    ptr1 = (PUCHAR) pcmd->cmnd;
    pSRB->ScsiCmdLen = pcmd->cmd_len;
    for(i=0; i< pcmd->cmd_len; i++)
    {
	*ptr = *ptr1;
	ptr++;
	ptr1++;
    }
    if( pcmd->use_sg )
    {
	pSRB->SGcount = (UCHAR) pcmd->use_sg;
	pSRB->pSegmentList = (PSGL) pcmd->request_buffer;
    }
    else if( pcmd->request_buffer )
    {
	pSRB->SGcount = 1;
	pSRB->pSegmentList = (PSGL) &pSRB->Segmentx;
	pSRB->Segmentx.address = (PUCHAR) pcmd->request_buffer;
	pSRB->Segmentx.length = pcmd->request_bufflen;
    }
    else
	pSRB->SGcount = 0;

    pSRB->SGIndex = 0;
    pSRB->AdaptStatus = 0;
    pSRB->TargetStatus = 0;
    pSRB->MsgCnt = 0;
    if( pDCB->DevType != TYPE_TAPE )
	pSRB->RetryCnt = 1;
    else
	pSRB->RetryCnt = 0;
    pSRB->SRBStatus = 0;
    pSRB->SRBFlag = 0;
    pSRB->SRBState = 0;
    pSRB->TotalXferredLen = 0;
    pSRB->SGPhysAddr = 0;
    pSRB->SGToBeXferLen = 0;
    pSRB->ScsiPhase = 0;
    pSRB->EndMessage = 0;
    SendSRB( pcmd, pACB, pSRB );

    restore_flags(flags);
    return;
}


/***********************************************************************
 * Function:
 *   DC390_bios_param
 *
 * Description:
 *   Return the disk geometry for the given SCSI device.
 ***********************************************************************/
int DC390_bios_param(Disk *disk, kdev_t devno, int geom[])
{
    int heads, sectors, cylinders;
    PACB pACB;

    pACB = (PACB) disk->device->host->hostdata;
    heads = 64;
    sectors = 32;
    cylinders = disk->capacity / (heads * sectors);

    if ( (pACB->Gmode2 & GREATER_1G) && (cylinders > 1024) )
    {
      heads = 255;
      sectors = 63;
      cylinders = disk->capacity / (heads * sectors);
    }

    geom[0] = heads;
    geom[1] = sectors;
    geom[2] = cylinders;

    return (0);
}


/***********************************************************************
 * Function : int DC390_abort (Scsi_Cmnd *cmd)
 *
 * Purpose : Abort an errant SCSI command
 *
 * Inputs : cmd - command to abort
 *
 * Returns : 0 on success, -1 on failure.
 ***********************************************************************/

int
DC390_abort (Scsi_Cmnd *cmd)
{
    ULONG flags;
    PACB  pACB;
    PDCB  pDCB, pdcb;
    PSRB  pSRB, psrb;
    USHORT count, i;
    PSCSICMD  pcmd, pcmd1;
    int   status;


#ifdef DC390_DEBUG0
    printk("DC390 : Abort Cmd.");
#endif

    save_flags(flags);
    cli();

    pACB = (PACB) cmd->host->hostdata;
    pDCB = pACB->pLinkDCB;
    pdcb = pDCB;
    while( (pDCB->UnitSCSIID != cmd->target) ||
	   (pDCB->UnitSCSILUN != cmd->lun) )
    {
	pDCB = pDCB->pNextDCB;
	if( pDCB == pdcb )
	    goto  NOT_RUN;
    }

    if( pDCB->QIORBCnt )
    {
	pcmd = pDCB->pQIORBhead;
	if( pcmd == cmd )
	{
	    pDCB->pQIORBhead = pcmd->next;
	    pcmd->next = NULL;
	    pDCB->QIORBCnt--;
	    status = SCSI_ABORT_SUCCESS;
	    goto  ABO_X;
	}
	for( count = pDCB->QIORBCnt, i=0; i<count-1; i++)
	{
	    if( pcmd->next == cmd )
	    {
		pcmd1 = pcmd->next;
		pcmd->next = pcmd1->next;
		pcmd1->next = NULL;
		pDCB->QIORBCnt--;
		status = SCSI_ABORT_SUCCESS;
		goto  ABO_X;
	    }
	    else
	    {
		pcmd = pcmd->next;
	    }
	}
    }

    pSRB = pDCB->pWaitingSRB;
    if( !pSRB )
	goto  ON_GOING;
    if( pSRB->pcmd == cmd )
    {
	pDCB->pWaitingSRB = pSRB->pNextSRB;
	goto  IN_WAIT;
    }
    else
    {
	psrb = pSRB;
	if( !(psrb->pNextSRB) )
	    goto ON_GOING;
	while( psrb->pNextSRB->pcmd != cmd )
	{
	    psrb = psrb->pNextSRB;
	    if( !(psrb->pNextSRB) )
		goto ON_GOING;
	}
	pSRB = psrb->pNextSRB;
	psrb->pNextSRB = pSRB->pNextSRB;
	if( pSRB == pDCB->pWaitLast )
	    pDCB->pWaitLast = psrb; /* No check for psrb == NULL ? */
IN_WAIT:
	pSRB->pNextSRB = pACB->pFreeSRB;
	pACB->pFreeSRB = pSRB;
	cmd->next = NULL;
	status = SCSI_ABORT_SUCCESS;
	goto  ABO_X;
    }

ON_GOING:
    pSRB = pDCB->pGoingSRB;
    for( count = pDCB->GoingSRBCnt, i=0; i<count; i++)
    {
	if( pSRB->pcmd != cmd )
	    pSRB = pSRB->pNextSRB;
	else
	{
	    if( (pACB->pActiveDCB == pDCB) && (pDCB->pActiveSRB == pSRB) )
	    {
		status = SCSI_ABORT_BUSY;
		goto  ABO_X;
	    }
	    else
	    {
		status = SCSI_ABORT_SNOOZE;
		goto  ABO_X;
	    }
	}
    }

NOT_RUN:
    status = SCSI_ABORT_NOT_RUNNING;

ABO_X:
    cmd->result = DID_ABORT << 16;
    cmd->scsi_done(cmd);
    restore_flags(flags);
    return( status );
}


static void
ResetDevParam( PACB pACB )
{
    PDCB   pDCB, pdcb;

    pDCB = pACB->pLinkDCB;
    if( pDCB == NULL )
	return;
    pdcb = pDCB;
    do
    {
	pDCB->SyncMode &= ~SYNC_NEGO_DONE;
	pDCB->SyncPeriod = 0;
	pDCB->SyncOffset = 0;
	pDCB->CtrlR3 = FAST_CLK;
	pDCB->CtrlR4 &= NEGATE_REQACKDATA;
	pDCB->CtrlR4 |= EATER_25NS;
	pDCB = pDCB->pNextDCB;
    }
    while( pdcb != pDCB );
}


static void
RecoverSRB( PACB pACB )
{
    PDCB   pDCB, pdcb;
    PSRB   psrb, psrb2;
    USHORT cnt, i;

    pDCB = pACB->pLinkDCB;
    if( pDCB == NULL )
	return;
    pdcb = pDCB;
    do
    {
	cnt = pdcb->GoingSRBCnt;
	psrb = pdcb->pGoingSRB;
	for (i=0; i<cnt; i++)
	{
	    psrb2 = psrb;
	    psrb = psrb->pNextSRB;
/*	    RewaitSRB( pDCB, psrb ); */
	    if( pdcb->pWaitingSRB )
	    {
		psrb2->pNextSRB = pdcb->pWaitingSRB;
		pdcb->pWaitingSRB = psrb2;
	    }
	    else
	    {
		pdcb->pWaitingSRB = psrb2;
		pdcb->pWaitLast = psrb2;
		psrb2->pNextSRB = NULL;
	    }
	}
	pdcb->GoingSRBCnt = 0;
	pdcb->pGoingSRB = NULL;
	pdcb->TagMask = 0;
	pdcb = pdcb->pNextDCB;
    }
    while( pdcb != pDCB );
}


/***********************************************************************
 * Function : int DC390_reset (Scsi_Cmnd *cmd, ...)
 *
 * Purpose : perform a hard reset on the SCSI bus
 *
 * Inputs : cmd - command which caused the SCSI RESET
 *
 * Returns : 0 on success.
 ***********************************************************************/

int DC390_reset(Scsi_Cmnd *cmd, unsigned int resetFlags)
{
    USHORT   ioport;
    unsigned long flags;
    PACB  pACB;
    UCHAR    bval;
    USHORT  i;


#ifdef DC390_DEBUG1
    printk("DC390: RESET,");
#endif

    pACB = (PACB ) cmd->host->hostdata;
    ioport = pACB->IOPortBase;
    save_flags(flags);
    cli();
    bval = inb(ioport+CtrlReg1);
    bval |= DIS_INT_ON_SCSI_RST;
    outb(bval,ioport+CtrlReg1);  /* disable interrupt */
    DC390_ResetSCSIBus( pACB );
    for( i=0; i<500; i++ )
	udelay(1000);
    bval = inb(ioport+CtrlReg1);
    bval &= ~DIS_INT_ON_SCSI_RST;
    outb(bval,ioport+CtrlReg1); /* re-enable interrupt */

    bval = DMA_IDLE_CMD;
    outb(bval,ioport+DMA_Cmd);
    bval = CLEAR_FIFO_CMD;
    outb(bval,ioport+ScsiCmd);

    ResetDevParam( pACB );
    DoingSRB_Done( pACB );
    pACB->pActiveDCB = NULL;

    pACB->ACBFlag = 0;
    DoWaitingSRB( pACB );

    restore_flags(flags);
#ifdef DC390_DEBUG1
    printk("DC390: RESET1,");
#endif
    return( SCSI_RESET_SUCCESS );
}


#include "scsiiom.c"


/***********************************************************************
 * Function : static void DC390_initDCB
 *
 * Purpose :  initialize the internal structures for a given DCB
 *
 * Inputs : cmd - pointer to this scsi cmd request block structure
 *
 ***********************************************************************/
void DC390_initDCB( PACB pACB, PDCB pDCB, PSCSICMD cmd )
{
    PEEprom	prom;
    UCHAR	bval;
    USHORT	index;

    if( pACB->DeviceCnt == 0 )
    {
	pACB->pLinkDCB = pDCB;
	pACB->pDCBRunRobin = pDCB;
	pDCB->pNextDCB = pDCB;
	pPrevDCB = pDCB;
    }
    else
	pPrevDCB->pNextDCB = pDCB;

    pDCB->pDCBACB = pACB;
    pDCB->QIORBCnt = 0;
    pDCB->UnitSCSIID = cmd->target;
    pDCB->UnitSCSILUN = cmd->lun;
    pDCB->pWaitingSRB = NULL;
    pDCB->pGoingSRB = NULL;
    pDCB->GoingSRBCnt = 0;
    pDCB->pActiveSRB = NULL;
    pDCB->TagMask = 0;
    pDCB->MaxCommand = 1;
    pDCB->AdaptIndex = pACB->AdapterIndex;
    index = pACB->AdapterIndex;
    pDCB->DCBFlag = 0;

    prom = (PEEprom) &eepromBuf[index][cmd->target << 2];
    pDCB->DevMode = prom->EE_MODE1;
    pDCB->AdpMode = eepromBuf[index][EE_MODE2];

    if( pDCB->DevMode & EN_DISCONNECT_ )
	bval = 0xC0;
    else
	bval = 0x80;
    bval |= cmd->lun;
    pDCB->IdentifyMsg = bval;

    pDCB->SyncMode = 0;
    if( pDCB->DevMode & SYNC_NEGO_ )
    {
	if( !(cmd->lun) || CurrSyncOffset )
	    pDCB->SyncMode = SYNC_ENABLE;
    }

    pDCB->SyncPeriod = 0;
    pDCB->SyncOffset = 0;
    pDCB->NegoPeriod = (clock_period1[prom->EE_SPEED] * 25) >> 2;

    pDCB->CtrlR1 = pACB->AdaptSCSIID;
    if( pDCB->DevMode & PARITY_CHK_ )
	pDCB->CtrlR1 |= PARITY_ERR_REPO;

    pDCB->CtrlR3 = FAST_CLK;

    pDCB->CtrlR4 = EATER_25NS;
    if( pDCB->AdpMode & ACTIVE_NEGATION)
	pDCB->CtrlR4 |= NEGATE_REQACKDATA;
}


/***********************************************************************
 * Function : static void DC390_initSRB
 *
 * Purpose :  initialize the internal structures for a given SRB
 *
 * Inputs : psrb - pointer to this scsi request block structure
 *
 ***********************************************************************/
void DC390_initSRB( PSRB psrb )
{
#ifdef DC390_DEBUG0
   printk("DC390 init: %08lx %08lx,",(ULONG)psrb,(ULONG)virt_to_bus(psrb));
#endif
	psrb->PhysSRB = virt_to_bus( psrb );
}


void DC390_linkSRB( PACB pACB )
{
    USHORT  count, i;
    PSRB    psrb;

    count = pACB->SRBCount;

    for( i=0; i< count; i++)
    {
	if( i != count - 1)
	    pACB->SRB_array[i].pNextSRB = &pACB->SRB_array[i+1];
	else
	    pACB->SRB_array[i].pNextSRB = NULL;
	psrb = (PSRB) &pACB->SRB_array[i];
	DC390_initSRB( psrb );
    }
}


/***********************************************************************
 * Function : static void DC390_initACB
 *
 * Purpose :  initialize the internal structures for a given SCSI host
 *
 * Inputs : psh - pointer to this host adapter's structure
 *
 ***********************************************************************/
__initfunc(void DC390_initACB( PSH psh, ULONG io_port, UCHAR Irq, USHORT index ))
{
    PACB    pACB;
    USHORT  i;

    psh->can_queue = MAX_CMD_QUEUE;
    psh->cmd_per_lun = MAX_CMD_PER_LUN;
    psh->this_id = (int) eepromBuf[index][EE_ADAPT_SCSI_ID];
    psh->io_port = io_port;
    psh->n_io_port = 0x80;
    psh->irq = Irq;

    pACB = (PACB) psh->hostdata;

    psh->max_id = 8;
#ifdef	CONFIG_SCSI_MULTI_LUN
    if( eepromBuf[index][EE_MODE2] & LUN_CHECK )
	psh->max_lun = 8;
    else
#endif
	psh->max_lun = 1;

    pACB->max_id = 7;
    if( pACB->max_id == eepromBuf[index][EE_ADAPT_SCSI_ID] )
	pACB->max_id--;
#ifdef	CONFIG_SCSI_MULTI_LUN
    if( eepromBuf[index][EE_MODE2] & LUN_CHECK )
	pACB->max_lun = 7;
    else
#endif
	pACB->max_lun = 0;

    pACB->pScsiHost = psh;
    pACB->IOPortBase = (USHORT) io_port;
    pACB->pLinkDCB = NULL;
    pACB->pDCBRunRobin = NULL;
    pACB->pActiveDCB = NULL;
    pACB->pFreeSRB = pACB->SRB_array;
    pACB->SRBCount = MAX_SRB_CNT;
    pACB->AdapterIndex = index;
    pACB->status = 0;
    pACB->AdaptSCSIID = eepromBuf[index][EE_ADAPT_SCSI_ID];
    pACB->HostID_Bit = (1 << pACB->AdaptSCSIID);
    pACB->AdaptSCSILUN = 0;
    pACB->DeviceCnt = 0;
    pACB->IRQLevel = Irq;
    pACB->TagMaxNum = eepromBuf[index][EE_TAG_CMD_NUM] << 2;
    pACB->ACBFlag = 0;
    pACB->scan_devices = 1;
    pACB->Gmode2 = eepromBuf[index][EE_MODE2];
    if( eepromBuf[index][EE_MODE2] & LUN_CHECK )
	pACB->LUNchk = 1;
    pACB->pDCB_free = &pACB->DCB_array[0];
    DC390_linkSRB( pACB );
    pACB->pTmpSRB = &pACB->TmpSRB;
    DC390_initSRB( pACB->pTmpSRB );
    for(i=0; i<MAX_SCSI_ID; i++)
	pACB->DCBmap[i] = 0;
}


/***********************************************************************
 * Function : static int DC390_initAdapter
 *
 * Purpose :  initialize the SCSI chip ctrl registers
 *
 * Inputs : psh - pointer to this host adapter's structure
 *
 ***********************************************************************/
__initfunc(int DC390_initAdapter( PSH psh, ULONG io_port, UCHAR Irq, USHORT index))
{
    USHORT ioport;
    UCHAR  bval;
    PACB   pACB, pacb;
    USHORT used_irq = 0;

    pacb = pACB_start;
    if( pacb != NULL )
    {
	for ( ; (pacb != (PACB) -1) ; )
	{
	    if( pacb->IRQLevel == Irq )
	    {
		used_irq = 1;
		break;
	    }
	    else
		pacb = pacb->pNextACB;
	}
    }

    if( !used_irq )
    {
	if( request_irq(Irq, DC390_Interrupt, SA_INTERRUPT | SA_SHIRQ, "tmscsim", NULL))
	{
	    printk("DC390: register IRQ error!\n");
	    return( -1 );
	}
    }

    request_region(io_port,psh->n_io_port,"tmscsim");

    ioport = (USHORT) io_port;

    pACB = (PACB) psh->hostdata;
    bval = SEL_TIMEOUT; 		/* 250ms selection timeout */
    outb(bval,ioport+Scsi_TimeOut);

    bval = CLK_FREQ_40MHZ;		/* Conversion factor = 0 , 40MHz clock */
    outb(bval,ioport+Clk_Factor);

    bval = NOP_CMD;			/* NOP cmd - clear command register */
    outb(bval,ioport+ScsiCmd);

    bval = EN_FEATURE+EN_SCSI2_CMD;	/* Enable Feature and SCSI-2 */
    outb(bval,ioport+CtrlReg2);

    bval = FAST_CLK;			/* fast clock */
    outb(bval,ioport+CtrlReg3);

    bval = EATER_25NS;
    if( eepromBuf[index][EE_MODE2] & ACTIVE_NEGATION )
	 bval |= NEGATE_REQACKDATA;
    outb(bval,ioport+CtrlReg4);

    bval = DIS_INT_ON_SCSI_RST; 	/* Disable SCSI bus reset interrupt */
    outb(bval,ioport+CtrlReg1);

    return(0);
}


void
DC390_EnDisableCE( UCHAR mode, struct pci_dev *pdev, PUCHAR regval )
{

    UCHAR bval;

    bval = 0;
    if(mode == ENABLE_CE)
	*regval = 0xc0;
    else
	*regval = 0x80;
    pci_write_config_byte(pdev, *regval, bval);
    if(mode == DISABLE_CE)
	pci_write_config_byte(pdev, *regval, bval);
    udelay(160);
}


void
DC390_EEpromOutDI( struct pci_dev *pdev, PUCHAR regval, USHORT Carry )
{
    UCHAR bval;

    bval = 0;
    if(Carry)
    {
	bval = 0x40;
	*regval = 0x80;
	pci_write_config_byte(pdev, *regval, bval);
    }
    udelay(160);
    bval |= 0x80;
    pci_write_config_byte(pdev, *regval, bval);
    udelay(160);
    bval = 0;
    pci_write_config_byte(pdev, *regval, bval);
    udelay(160);
}


UCHAR
DC390_EEpromInDO( struct pci_dev *pdev )
{
    UCHAR bval;

    pci_write_config_byte(pdev, 0x80, 0x80);
    udelay(160);
    pci_write_config_byte(pdev, 0x80, 0x40);
    udelay(160);
    pci_read_config_byte(pdev, 0x00, &bval);
    if(bval == 0x22)
	return(1);
    else
	return(0);
}


USHORT
EEpromGetData1( struct pci_dev *pdev )
{
    UCHAR i;
    UCHAR carryFlag;
    USHORT wval;

    wval = 0;
    for(i=0; i<16; i++)
    {
	wval <<= 1;
	carryFlag = DC390_EEpromInDO(pdev);
	wval |= carryFlag;
    }
    return(wval);
}


void
DC390_Prepare( struct pci_dev *pdev, PUCHAR regval, UCHAR EEpromCmd )
{
    UCHAR i,j;
    USHORT carryFlag;

    carryFlag = 1;
    j = 0x80;
    for(i=0; i<9; i++)
    {
	DC390_EEpromOutDI(pdev,regval,carryFlag);
	carryFlag = (EEpromCmd & j) ? 1 : 0;
	j >>= 1;
    }
}


void
DC390_ReadEEprom( struct pci_dev *pdev, int index )
{
    UCHAR   regval,cmd;
    PUSHORT ptr;
    USHORT  i;

    ptr = (PUSHORT) &eepromBuf[index][0];
    cmd = EEPROM_READ;
    for(i=0; i<0x40; i++)
    {
	DC390_EnDisableCE(ENABLE_CE, pdev, &regval);
	DC390_Prepare(pdev, &regval, cmd);
	*ptr = EEpromGetData1(pdev);
	ptr++;
	cmd++;
	DC390_EnDisableCE(DISABLE_CE, pdev, &regval);
    }
}


USHORT
DC390_CheckEEpromCheckSum( struct pci_dev *pdev, int index )
{
    USHORT wval, rc, *ptr;
    UCHAR  i;

    DC390_ReadEEprom( pdev, index );
    wval = 0;
    ptr = (PUSHORT) &eepromBuf[index][0];
    for(i=0; i<128 ;i+=2, ptr++)
	wval += *ptr;
    if( wval == 0x1234 )
	rc = 0;
    else
	rc = -1;
    return( rc );
}


/***********************************************************************
 * Function : static int DC390_init (struct Scsi_Host *host)
 *
 * Purpose :  initialize the internal structures for a given SCSI host
 *
 * Inputs : host - pointer to this host adapter's structure/
 *
 * Preconditions : when this function is called, the chip_type
 *	field of the pACB structure MUST have been set.
 ***********************************************************************/

__initfunc(static int
DC390_init (PSHT psht, ULONG io_port, UCHAR Irq, struct pci_dev *pdev, int index))
{
    PSH   psh;
    PACB  pACB;

    if( !DC390_CheckEEpromCheckSum( pdev, index ) )
    {
	psh = scsi_register( psht, sizeof(DC390_ACB) );
	if( !psh )
	    return( -1 );
	if( !pSH_start )
	{
	    pSH_start = psh;
	    pSH_current = psh;
	}
	else
	{
	    pSH_current->next = psh;
	    pSH_current = psh;
	}

#ifdef DC390_DEBUG0
	printk("DC390: pSH = %8x,", (UINT) psh);
	printk("DC390: Index %02i,", index);
#endif

	DC390_initACB( psh, io_port, Irq, index );
	if( !DC390_initAdapter( psh, io_port, Irq, index ) )
	{
	    pACB = (PACB) psh->hostdata;
	    if( !pACB_start )
	    {
		pACB_start = pACB;
		pACB_current = pACB;
		pACB->pNextACB = (PACB) -1;
	    }
	    else
	    {
		pACB_current->pNextACB = pACB;
		pACB_current = pACB;
		pACB->pNextACB = (PACB)  -1;
	    }

#ifdef DC390_DEBUG0
	printk("DC390: pACB = %8x, pDCB_array = %8x, pSRB_array = %8x\n",
	      (UINT) pACB, (UINT) pACB->DCB_array, (UINT) pACB->SRB_array);
	printk("DC390: ACB size= %4x, DCB size= %4x, SRB size= %4x\n",
	      sizeof(DC390_ACB), sizeof(DC390_DCB), sizeof(DC390_SRB) );
#endif

	}
	else
	{
	    pSH_start = NULL;
	    scsi_unregister( psh );
	    return( -1 );
	}
	return( 0 );
    }
    else
    {
	printk("DC390_init: EEPROM reading error!\n");
	return( -1 );
    }
}


/***********************************************************************
 * Function : int DC390_detect(Scsi_Host_Template *psht)
 *
 * Purpose : detects and initializes AMD53C974 SCSI chips
 *	     that were autoprobed, overridden on the LILO command line,
 *	     or specified at compile time.
 *
 * Inputs : psht - template for this SCSI adapter
 *
 * Returns : number of host adapters detected
 *
 ***********************************************************************/

__initfunc(int
DC390_detect(Scsi_Host_Template *psht))
{
    struct pci_dev *pdev = NULL;
    UINT    irq;
    UINT    io_port;
    USHORT  adaptCnt = 0;	/* Number of boards detected */

    psht->proc_dir = &proc_scsi_tmscsim;

    InitialTime = 1;
    pSHT_start = psht;
    pACB_start = NULL;

    if ( pci_present() )
	while ((pdev = pci_find_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD53C974, pdev)))
	{
	    io_port = pdev->base_address[0] & PCI_BASE_ADDRESS_IO_MASK;
	    irq = pdev->irq;
#ifdef DC390_DEBUG0
	    printk("DC390: IO_PORT=%4x,IRQ=%x,\n",(UINT) io_port, irq);
#endif
	    if( !DC390_init(psht, io_port, irq, pdev, adaptCnt))
		adaptCnt++;
	}

    InitialTime = 0;
    adapterCnt = adaptCnt;
    return( adaptCnt );
}


/********************************************************************
 * Function: tmscsim_set_info()
 *
 * Purpose: Set adapter info (!)
 *
 * Not yet implemented
 *
 *******************************************************************/

int tmscsim_set_info(char *buffer, int length, struct Scsi_Host *shpnt)
{
  return(-ENOSYS);  /* Currently this is a no-op */
}

/********************************************************************
 * Function: tmscsim_proc_info(char* buffer, char **start,
 *			     off_t offset, int length, int hostno, int inout)
 *
 * Purpose: return SCSI Adapter/Device Info
 *
 * Input: buffer: Pointer to a buffer where to write info
 *	  start :
 *	  offset:
 *	  hostno: Host adapter index
 *	  inout : Read (=0) or set(!=0) info
 *
 * Output: buffer: contains info
 *	   length; length of info in buffer
 *
 * return value: length
 *
 ********************************************************************/

/* KG: proc_info taken from driver aha152x.c */

#undef SPRINTF
#define SPRINTF(args...) pos += sprintf(pos, ## args)

#define YESNO(YN)\
if (YN) SPRINTF(" Yes ");\
else SPRINTF(" No  ")

int tmscsim_proc_info(char *buffer, char **start,
		      off_t offset, int length, int hostno, int inout)
{
  int dev, spd, spd1;
  char *pos = buffer;
  PSH shpnt;
  PACB acbpnt;
  PDCB dcbpnt;
  unsigned long flags;
/*  Scsi_Cmnd *ptr; */

  acbpnt = pACB_start;

  while(acbpnt != (PACB)-1)
     {
	shpnt = acbpnt->pScsiHost;
	if (shpnt->host_no == hostno) break;
	acbpnt = acbpnt->pNextACB;
     }

  if (acbpnt == (PACB)-1) return(-ESRCH);
  if(!shpnt) return(-ESRCH);

  if(inout) /* Has data been written to the file ? */
    return(tmscsim_set_info(buffer, length, shpnt));

  SPRINTF("Tekram DC390(T) PCI SCSI Host Adadpter, ");
  SPRINTF("Driver Version 1.12, 1998/02/25\n");

  save_flags(flags);
  cli();

  SPRINTF("SCSI Host Nr %i, ", shpnt->host_no);
  SPRINTF("DC390 Adapter Nr %i\n", acbpnt->AdapterIndex);
  SPRINTF("IOPortBase 0x%04x, ", acbpnt->IOPortBase);
  SPRINTF("IRQLevel 0x%02x\n", acbpnt->IRQLevel);

  SPRINTF("MaxID %i, MaxLUN %i, ",acbpnt->max_id, acbpnt->max_lun);
  SPRINTF("AdapterID %i, AdapterLUN %i\n", acbpnt->AdaptSCSIID, acbpnt->AdaptSCSILUN);

  SPRINTF("TagMaxNum %i, Status %i\n", acbpnt->TagMaxNum, acbpnt->status);

  SPRINTF("Nr of attached devices: %i\n", acbpnt->DeviceCnt);

  SPRINTF("Un ID LUN Prty Sync DsCn SndS TagQ NegoPeriod SyncSpeed SyncOffs\n");

  dcbpnt = acbpnt->pLinkDCB;
  for (dev = 0; dev < acbpnt->DeviceCnt; dev++)
     {
      SPRINTF("%02i %02i  %02i ", dev, dcbpnt->UnitSCSIID, dcbpnt->UnitSCSILUN);
      YESNO(dcbpnt->DevMode & PARITY_CHK_);
      YESNO(dcbpnt->SyncMode & SYNC_NEGO_DONE);
      YESNO(dcbpnt->DevMode & EN_DISCONNECT_);
      YESNO(dcbpnt->DevMode & SEND_START_);
      YESNO(dcbpnt->SyncMode & EN_TAG_QUEUING);
      SPRINTF("  %03i ns ", (dcbpnt->NegoPeriod) << 2);
      if (dcbpnt->SyncOffset & 0x0f)
      {
	 spd = 1000/(dcbpnt->NegoPeriod <<2);
	 spd1 = 1000%(dcbpnt->NegoPeriod <<2);
	 spd1 = (spd1 * 10)/(dcbpnt->NegoPeriod <<2);
	 SPRINTF("   %2i.%1i M      %02i\n", spd, spd1, (dcbpnt->SyncOffset & 0x0f));
      }
      else SPRINTF("\n");
      /* Add more info ...*/
      dcbpnt = dcbpnt->pNextDCB;
     }

  restore_flags(flags);
  *start = buffer + offset;

  if (pos - buffer < offset)
    return 0;
  else if (pos - buffer - offset < length)
    return pos - buffer - offset;
  else
    return length;
}


#ifdef MODULE

/***********************************************************************
 * Function : static int DC390_shutdown (struct Scsi_Host *host)
 *
 * Purpose : does a clean (we hope) shutdown of the SCSI chip.
 *	     Use prior to dumping core, unloading the driver, etc.
 *
 * Returns : 0 on success
 ***********************************************************************/
static int
DC390_shutdown (struct Scsi_Host *host)
{
    UCHAR    bval;
    USHORT   ioport;
    unsigned long flags;
    PACB pACB = (PACB)(host->hostdata);

    ioport = (unsigned int) pACB->IOPortBase;

    save_flags (flags);
    cli();

/*  pACB->soft_reset(host); */

#ifdef DC390_DEBUG0
    printk("DC390: shutdown,");
#endif

    bval = inb(ioport+CtrlReg1);
    bval |= DIS_INT_ON_SCSI_RST;
    outb(bval,ioport+CtrlReg1);  /* disable interrupt */
    DC390_ResetSCSIBus( pACB );

    restore_flags (flags);
    return( 0 );
}


int DC390_release(struct Scsi_Host *host)
{
    int irq_count;
    struct Scsi_Host *tmp;

    DC390_shutdown (host);

    if (host->irq != IRQ_NONE)
    {
	for (irq_count = 0, tmp = pSH_start; tmp; tmp = tmp->next)
	{
	    if ( tmp->irq == host->irq )
		++irq_count;
	}
	if (irq_count == 1)
	 {
#ifdef DC390_DEBUG0
	    printk("DC390: Free IRQ %i.",host->irq);
#endif
	    free_irq(host->irq,NULL);
	 }
    }

    release_region(host->io_port,host->n_io_port);

    return( 1 );
}

Scsi_Host_Template driver_template = DC390_T;
#include "scsi_module.c"
#endif /* def MODULE */

