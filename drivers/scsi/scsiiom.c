/***********************************************************************
 *	FILE NAME : SCSIIOM.C					       *
 *	     BY   : C.L. Huang,    ching@tekram.com.tw		       *
 *	Description: Device Driver for Tekram DC-390 (T) PCI SCSI      *
 *		     Bus Master Host Adapter			       *
 ***********************************************************************/
/* $Id: scsiiom.c,v 2.15 1998/12/25 17:33:27 garloff Exp $ */

UCHAR
dc390_StartSCSI( PACB pACB, PDCB pDCB, PSRB pSRB )
{
    USHORT wlval;
    UCHAR  bval, bval1;

    pSRB->TagNumber = 31;
    DC390_write8 (Scsi_Dest_ID, pDCB->UnitSCSIID);
    DC390_write8 (Sync_Period, pDCB->SyncPeriod);
    DC390_write8 (Sync_Offset, pDCB->SyncOffset);
    DC390_write8 (CtrlReg1, pDCB->CtrlR1);
    DC390_write8 (CtrlReg3, pDCB->CtrlR3);
    DC390_write8 (CtrlReg4, pDCB->CtrlR4);
    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);		/* Flush FIFO */
    DEBUG1(printk (KERN_INFO "DC390: Start SCSI command: %02x (Sync:%02x)\n",\
	    pSRB->CmdBlock[0], pDCB->SyncMode);)
    pSRB->ScsiPhase = SCSI_NOP0;
    //pSRB->MsgOutBuf[0] = MSG_NOP;
    //pSRB->MsgCnt = 0;
    bval = pDCB->IdentifyMsg;
    if( !(pDCB->SyncMode & EN_ATN_STOP) )	/* Don't always try send Extended messages on arbitration */
    {
	if( (pSRB->CmdBlock[0] == INQUIRY) ||
	    (pSRB->CmdBlock[0] == REQUEST_SENSE) ||
	    (pSRB->SRBFlag & AUTO_REQSENSE) )
	{
	    bval &= 0xBF;	/* No DisConn */
	    DC390_write8 (ScsiFifo, bval);
	    bval1 = SEL_W_ATN;
	    pSRB->SRBState = SRB_START_;
	    DEBUG1(printk (KERN_DEBUG "DC390: No DisCn, No TagQ (%02x, %02x)\n", bval, bval1);)
	    if( pDCB->SyncMode & SYNC_ENABLE )
		{
		if( !(pDCB->IdentifyMsg & 7) ||		/* LUN == 0 || Cmd != INQUIRY */
		    (pSRB->CmdBlock[0] != INQUIRY) )
		{
		    bval1 = SEL_W_ATN_STOP;	/* Try to establish SYNC nego */
		    pSRB->SRBState = SRB_MSGOUT;
		}
	    }
	}
	else	/* TagQ ? */
	{
	    DC390_write8 (ScsiFifo, bval);
	    if(pDCB->SyncMode & EN_TAG_QUEUEING)
	    {
	        DC390_write8 (ScsiFifo, MSG_SIMPLE_QTAG);
		DEBUG1(printk (KERN_DEBUG "DC390: %sDisCn, TagQ (%02x, %02x, %08lx)\n", (bval&0x40?"":"No "), bval, SEL_W_ATN3, pDCB->TagMask);)
	        bval = 0; wlval = 1;
	        while (wlval & pDCB->TagMask)
		  { bval++; wlval <<= 1; };
		pDCB->TagMask |= wlval;
	        DC390_write8 (ScsiFifo, bval);
		pSRB->TagNumber = bval;
		DEBUG1(printk (KERN_DEBUG "DC390: SRB %p (Cmd %li), Tag %02x queued\n", pSRB, pSRB->pcmd->pid, bval);)
		bval1 = SEL_W_ATN3;
		pSRB->SRBState = SRB_START_;
	    }
	    else	/* No TagQ */
	    {
		bval1 = SEL_W_ATN;
		DEBUG1(printk (KERN_DEBUG "DC390: %sDisCn, No TagQ (%02x, %02x, %08lx)\n", (bval&0x40?"":"No "), bval, bval1, pDCB->TagMask);)
		pSRB->SRBState = SRB_START_;
	    }
	}

    }
    else	/* ATN_STOP: Always try to establish Sync nego */
    {
	if( (pSRB->CmdBlock[0] == INQUIRY) ||
	    (pSRB->CmdBlock[0] == REQUEST_SENSE) ||
	    (pSRB->SRBFlag & AUTO_REQSENSE) )
	{
	    bval &= 0xBF;	/* No DisConn */
	    DC390_write8 (ScsiFifo, bval);
	    bval1 = SEL_W_ATN;
	    DEBUG1(printk (KERN_DEBUG "DC390: No DisCn, No TagQ (%02x, %02x)\n", bval, bval1);)
	    pSRB->SRBState = SRB_START_;
	      /* ??? */
	    if( pDCB->SyncMode & SYNC_ENABLE )
	    {
		if( !(pDCB->IdentifyMsg & 7) ||		/* LUN == 0 || Cmd != INQUIRY */
		    (pSRB->CmdBlock[0] != INQUIRY) )
		{
		    bval1 = SEL_W_ATN_STOP;	/* Try to establish Sync nego */
		    pSRB->SRBState = SRB_MSGOUT;
		}
	    }
	}
	else	/* TagQ ? */
	{
	    DC390_write8 (ScsiFifo, bval);
	    if(pDCB->SyncMode & EN_TAG_QUEUEING)
	    {
		pSRB->MsgOutBuf[0] = MSG_SIMPLE_QTAG;
		DEBUG1(printk (KERN_DEBUG "DC390: %sDisCn, TagQ (%02x, %02x, %08lx)\n", (bval&0x40?"":"No "), bval, SEL_W_ATN_STOP, pDCB->TagMask);)
		bval = 0; wlval = 1;
	        while (wlval & pDCB->TagMask)
		  { bval++; wlval <<= 1; };
		pDCB->TagMask |= wlval;
		pSRB->TagNumber = bval;
		DEBUG1(printk (KERN_DEBUG "DC390: SRB %p (Cmd %li), Tag %02x queued\n", pSRB, pSRB->pcmd->pid, bval);)
		pSRB->MsgOutBuf[1] = bval;
		pSRB->MsgCnt = 2;
		bval1 = SEL_W_ATN_STOP;
		pSRB->SRBState = SRB_START_;	/* ?? */
	    }
	    else	/* No TagQ */
	    {
		pSRB->MsgOutBuf[0] = MSG_NOP;
		pSRB->MsgCnt = 1;
		pSRB->SRBState = SRB_START_;
		bval1 = SEL_W_ATN_STOP;
		DEBUG1(printk (KERN_DEBUG "DC390: %sDisCn, No TagQ (%02x, %02x, %08lx)\n", (bval&0x40?"":"No "), bval, bval1, pDCB->TagMask);)
	    };
	}
    }
    if (bval1 != SEL_W_ATN_STOP)
      {	/* Command is written in CommandPhase, if SEL_W_ATN_STOP ... */
	if( pSRB->SRBFlag & AUTO_REQSENSE )
	  {
	    bval = 0;
	    DC390_write8 (ScsiFifo, REQUEST_SENSE);
	    DC390_write8 (ScsiFifo, pDCB->IdentifyMsg << 5);
	    DC390_write8 (ScsiFifo, bval);
	    DC390_write8 (ScsiFifo, bval);
	    DC390_write8 (ScsiFifo, sizeof(pSRB->pcmd->sense_buffer));
	    DC390_write8 (ScsiFifo, bval);
	    DEBUG1(printk (KERN_DEBUG "DC390: AutoReqSense !\n");)
	  }
	else	/* write cmnd to bus */ 
	  {
	    PUCHAR ptr; UCHAR i;
	    ptr = (PUCHAR) pSRB->CmdBlock;
	    for (i=0; i<pSRB->ScsiCmdLen; i++)
	      DC390_write8 (ScsiFifo, *(ptr++));
	  };
      }

    /* Check if we can't win arbitration */
    if (DC390_read8 (Scsi_Status) & INTERRUPT)
    {
	pSRB->SRBState = SRB_READY;
	pDCB->TagMask &= ~( 1 << pSRB->TagNumber );
	DEBUG0(printk (KERN_WARNING "DC390: Interrupt during StartSCSI!\n");)
	return 1;
    }
    else
    {
	pSRB->ScsiPhase = SCSI_NOP1;
	DEBUG0(if (pACB->pActiveDCB)	\
		    printk (KERN_WARNING "DC390: ActiveDCB != 0\n");)
	DEBUG0(if (pDCB->pActiveSRB)	\
		    printk (KERN_WARNING "DC390: ActiveSRB != 0\n");)
	pACB->pActiveDCB = pDCB;
	pDCB->pActiveSRB = pSRB;
	//DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
	DC390_write8 (ScsiCmd, bval1);
	return 0;
    }
}

//#define DMA_INT EN_DMA_INT /*| EN_PAGE_INT*/
#define DMA_INT 0

#if DMA_INT
/* This is similar to AM53C974.c ... */
static UCHAR 
dc390_dma_intr (PACB pACB)
{
  PSRB pSRB;
  UCHAR dstate;
  DEBUG0(USHORT pstate;PDEVDECL1;)
  
  DEBUG0(PDEVSET1;)
  DEBUG0(PCI_READ_CONFIG_WORD (PDEV, PCI_STATUS, &pstate);)
  DEBUG0(if (pstate & (PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY))\
	{ printk(KERN_WARNING "DC390: PCI state = %04x!\n", pstate); \
	  PCI_WRITE_CONFIG_WORD (PDEV, PCI_STATUS, (PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY));};)

  dstate = DC390_read8 (DMA_Status); 

  if (! pACB->pActiveDCB || ! pACB->pActiveDCB->pActiveSRB) return dstate;
  else pSRB  = pACB->pActiveDCB->pActiveSRB;
  
  if (dstate & (DMA_XFER_ABORT | DMA_XFER_ERROR | POWER_DOWN | PCI_MS_ABORT))
    {
	printk (KERN_ERR "DC390: DMA error (%02x)!\n", dstate);
	return dstate;
    };
  if (dstate & DMA_XFER_DONE)
    {
	ULONG residual, xferCnt; int ctr = 5000000;
	if (! (DC390_read8 (DMA_Cmd) & READ_DIRECTION))
	  {
	    do
	      {
		DEBUG1(printk (KERN_DEBUG "DC390: read residual bytes ... \n");)
		dstate = DC390_read8 (DMA_Status);
		residual = DC390_read8 (CtcReg_Low) | DC390_read8 (CtcReg_Mid) << 8 |
		  DC390_read8 (CtcReg_High) << 16;
		residual += DC390_read8 (Current_Fifo) & 0x1f;
	      } while (residual && ! (dstate & SCSI_INTERRUPT) && --ctr);
	    if (!ctr) printk (KERN_CRIT "DC390: dma_intr: DMA aborted unfinished: %06x bytes remain!!\n", DC390_read32 (DMA_Wk_ByteCntr));
	    /* residual =  ... */
	  }
	else
	    residual = 0;
	
	/* ??? */
	
	xferCnt = pSRB->SGToBeXferLen - residual;
	pSRB->SGBusAddr += xferCnt;
	pSRB->TotalXferredLen += xferCnt;
	pSRB->SGToBeXferLen = residual;
# ifdef DC390_DEBUG0
	printk (KERN_INFO "DC390: DMA: residual = %i, xfer = %i\n", 
		(unsigned int)residual, (unsigned int)xferCnt);
# endif
	
	DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
    }
  dc390_laststatus &= ~0xff000000; dc390_laststatus |= dstate << 24;
  return dstate;
};
#endif

void __inline__
DC390_Interrupt( int irq, void *dev_id, struct pt_regs *regs)
{
    PACB   pACB;
    PDCB   pDCB;
    PSRB   pSRB;
    UCHAR  sstatus=0;
    UCHAR  phase, i;
    void   (*stateV)( PACB, PSRB, PUCHAR );
    UCHAR  istate, istatus;
#if DMA_INT
    UCHAR  dstatus;
#endif
    DC390_AFLAGS DC390_IFLAGS DC390_DFLAGS

    pACB = dc390_pACB_start;

    if (pACB == 0)
    {
	printk(KERN_WARNING "DC390: Interrupt on uninitialized adapter!\n");
	return;
    }
    DC390_LOCK_DRV;

    for( i=0; i < dc390_adapterCnt; i++ )
    {
	if( pACB->IRQLevel == (UCHAR) irq )
	{
	     sstatus = DC390_read8 (Scsi_Status);
	     if( sstatus & INTERRUPT )
		 break;
	     else
		pACB = pACB->pNextACB;
	}
	else
	{
	    pACB = pACB->pNextACB;
	}
    }

    DEBUG1(printk (KERN_DEBUG "sstatus=%02x,", sstatus);)

    if( !pACB ) { DC390_UNLOCK_DRV; return; };

#if DMA_INT
    DC390_LOCK_IO;
    DC390_LOCK_ACB;
    dstatus = dc390_dma_intr (pACB);
    DC390_UNLOCK_ACB;
    DC390_UNLOCK_IO;

    DEBUG1(printk (KERN_DEBUG "dstatus=%02x,", dstatus);)
    if (! (dstatus & SCSI_INTERRUPT))
      {
	DEBUG0(printk (KERN_WARNING "DC390 Int w/o SCSI actions (only DMA?)\n");)
	DC390_UNLOCK_DRV;
	return;
      };
#else
    //DC390_write32 (DMA_ScsiBusCtrl, WRT_ERASE_DMA_STAT | EN_INT_ON_PCI_ABORT);
    //dstatus = DC390_read8 (DMA_Status);
    //DC390_write32 (DMA_ScsiBusCtrl, EN_INT_ON_PCI_ABORT);
#endif

    DC390_LOCK_IO;
    DC390_LOCK_ACB;
    DC390_UNLOCK_DRV_NI; /* Allow _other_ CPUs to process IRQ (useful for shared IRQs) */

    istate = DC390_read8 (Intern_State);
    istatus = DC390_read8 (INT_Status); /* This clears Scsi_Status, Intern_State and INT_Status ! */

    DEBUG1(printk (KERN_INFO "Istatus(Res,Inv,Dis,Serv,Succ,ReS,SelA,Sel)=%02x,",istatus);)
    dc390_laststatus &= ~0x00ffffff;
    dc390_laststatus |= /* dstatus<<24 | */ sstatus<<16 | istate<<8 | istatus;

    if (sstatus & ILLEGAL_OP_ERR)
    {
	printk ("DC390: Illegal Operation detected (%08lx)!\n", dc390_laststatus);
	dc390_dumpinfo (pACB, pACB->pActiveDCB, pACB->pActiveDCB->pActiveSRB);
    };
	
    if(istatus &  DISCONNECTED)
    {
	dc390_Disconnect( pACB );
	goto unlock;
    }

    if(istatus &  RESELECTED)
    {
	dc390_Reselect( pACB );
	goto unlock;
    }

    if( istatus & (SUCCESSFUL_OP|SERVICE_REQUEST) )
    {
	pDCB = pACB->pActiveDCB;
	if (!pDCB)
	{
		printk (KERN_ERR "DC390: Suc. op/ Serv. req: pActiveDCB = 0!\n");
		goto unlock;
	};
	pSRB = pDCB->pActiveSRB;
	if( pDCB->DCBFlag & ABORT_DEV_ )
	  dc390_EnableMsgOut_Abort (pACB, pSRB);

	phase = pSRB->ScsiPhase;
	DEBUG1(printk (KERN_INFO "DC390: [%i]%s(0) (%02x)\n", phase, dc390_p0_str[phase], sstatus);)
	stateV = (void *) dc390_phase0[phase];
	( *stateV )( pACB, pSRB, &sstatus );

	pSRB->ScsiPhase = sstatus & 7;
	phase = (UCHAR) sstatus & 7;
	DEBUG1(printk (KERN_INFO "DC390: [%i]%s(1) (%02x)\n", phase, dc390_p1_str[phase], sstatus);)
	stateV = (void *) dc390_phase1[phase];
	( *stateV )( pACB, pSRB, &sstatus );
	goto unlock;
    }

    if(istatus &  INVALID_CMD)
    {
	dc390_InvalidCmd( pACB );
	goto unlock;
    }

    if(istatus &  SCSI_RESET)
    {
	dc390_ScsiRstDetect( pACB );
	goto unlock;
    }

 unlock:
    DC390_LOCK_DRV_NI;
    DC390_UNLOCK_ACB;
    DC390_UNLOCK_IO;
    DC390_UNLOCK_DRV; /* Restore initial flags */
}

void
do_DC390_Interrupt( int irq, void *dev_id, struct pt_regs *regs)
{
    DEBUG1(printk (KERN_INFO "DC390: Irq (%i) caught: ", irq);)
    /* Locking is done in DC390_Interrupt */
    DC390_Interrupt(irq, dev_id, regs);
    DEBUG1(printk (".. IRQ returned\n");)
}

void
dc390_DataOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR   sstatus;
    PSGL    psgl;
    ULONG   ResidCnt, xferCnt;
    UCHAR   dstate = 0;

    sstatus = *psstatus;

    if( !(pSRB->SRBState & SRB_XFERPAD) )
    {
	if( sstatus & (PARITY_ERR | ILLEGAL_OP_ERR) )
	    pSRB->SRBStatus |= PARITY_ERROR;

	if( sstatus & COUNT_2_ZERO )
	{
	    int ctr = 5000000; /* only try for about a tenth of a second */
	    while( --ctr && !((dstate = DC390_read8 (DMA_Status)) & DMA_XFER_DONE) && pSRB->SGToBeXferLen );
	    if (!ctr) printk (KERN_CRIT "DC390: Deadlock in DataOut_0: DMA aborted unfinished: %06x bytes remain!!\n", DC390_read32 (DMA_Wk_ByteCntr));
	    dc390_laststatus &= ~0xff000000; dc390_laststatus |= dstate << 24;
	    pSRB->TotalXferredLen += pSRB->SGToBeXferLen;
	    pSRB->SGIndex++;
	    if( pSRB->SGIndex < pSRB->SGcount )
	    {
		pSRB->pSegmentList++;
		psgl = pSRB->pSegmentList;

		pSRB->SGBusAddr = virt_to_bus( psgl->address );
		pSRB->SGToBeXferLen = (ULONG) psgl->length;
	    }
	    else
		pSRB->SGToBeXferLen = 0;
	}
	else
	{
	    ResidCnt = (ULONG) DC390_read8 (Current_Fifo) & 0x1f;
	    ResidCnt |= (ULONG) DC390_read8 (CtcReg_High) << 16;
	    ResidCnt |= (ULONG) DC390_read8 (CtcReg_Mid) << 8; 
	    ResidCnt += (ULONG) DC390_read8 (CtcReg_Low);

	    xferCnt = pSRB->SGToBeXferLen - ResidCnt;
	    pSRB->SGBusAddr += xferCnt;
	    pSRB->TotalXferredLen += xferCnt;
	    pSRB->SGToBeXferLen = ResidCnt;
	}
    }
    DC390_write8 (DMA_Cmd, WRITE_DIRECTION+DMA_IDLE_CMD); /* | DMA_INT */
}

void
dc390_DataIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR   sstatus, residual, bval;
    PSGL    psgl;
    ULONG   ResidCnt, xferCnt, i;
    PUCHAR  ptr;

    sstatus = *psstatus;

    if( !(pSRB->SRBState & SRB_XFERPAD) )
    {
	if( sstatus & (PARITY_ERR | ILLEGAL_OP_ERR))
	    pSRB->SRBStatus |= PARITY_ERROR;

	if( sstatus & COUNT_2_ZERO )
	{
	    int ctr = 5000000; /* only try for about a tenth of a second */
	    int dstate = 0;
	    while( --ctr && !((dstate = DC390_read8 (DMA_Status)) & DMA_XFER_DONE) && pSRB->SGToBeXferLen );
	    if (!ctr) printk (KERN_CRIT "DC390: Deadlock in DataIn_0: DMA aborted unfinished: %06x bytes remain!!\n", DC390_read32 (DMA_Wk_ByteCntr));
	    if (!ctr) printk (KERN_CRIT "DC390: DataIn_0: DMA State: %i\n", dstate);
	    dc390_laststatus &= ~0xff000000; dc390_laststatus |= dstate << 24;
	    DEBUG1(ResidCnt = ((ULONG) DC390_read8 (CtcReg_High) << 16)	\
		+ ((ULONG) DC390_read8 (CtcReg_Mid) << 8)		\
		+ ((ULONG) DC390_read8 (CtcReg_Low));)
	    DEBUG1(printk (KERN_DEBUG "Count_2_Zero (ResidCnt=%li,ToBeXfer=%li),", ResidCnt, pSRB->SGToBeXferLen);)

	    DC390_write8 (DMA_Cmd, READ_DIRECTION+DMA_IDLE_CMD); /* | DMA_INT */

	    pSRB->TotalXferredLen += pSRB->SGToBeXferLen;
	    pSRB->SGIndex++;
	    if( pSRB->SGIndex < pSRB->SGcount )
	    {
		pSRB->pSegmentList++;
		psgl = pSRB->pSegmentList;

		pSRB->SGBusAddr = virt_to_bus( psgl->address );
		pSRB->SGToBeXferLen = (ULONG) psgl->length;
	    }
	    else
		pSRB->SGToBeXferLen = 0;
	}
	else	/* phase changed */
	{
	    residual = 0;
	    bval = DC390_read8 (Current_Fifo);
	    while( bval & 0x1f )
	    {
		DEBUG1(printk (KERN_DEBUG "Check for residuals,");)
		if( (bval & 0x1f) == 1 )
		{
		    for(i=0; i < 0x100; i++)
		    {
			bval = DC390_read8 (Current_Fifo);
			if( !(bval & 0x1f) )
			    goto din_1;
			else if( i == 0x0ff )
			{
			    residual = 1;   /* ;1 residual byte */
			    goto din_1;
			}
		    }
		}
		else
		    bval = DC390_read8 (Current_Fifo);
	    }
din_1:
	    DC390_write8 (DMA_Cmd, READ_DIRECTION+DMA_BLAST_CMD);
	    for (i = 0xa000; i; i--)
	    {
		bval = DC390_read8 (DMA_Status);
		if (bval & BLAST_COMPLETE)
		    break;
	    }
	    /* It seems a DMA Blast abort isn't that bad ... */
	    if (!i) printk (KERN_ERR "DC390: DMA Blast aborted unfinished!\n");
	    //DC390_write8 (DMA_Cmd, READ_DIRECTION+DMA_IDLE_CMD); /* | DMA_INT */
	    dc390_laststatus &= ~0xff000000; dc390_laststatus |= bval << 24;

	    DEBUG1(printk (KERN_DEBUG "Blast: Read %li times DMA_Status %02x", 0xa000-i, bval);)
	    ResidCnt = (ULONG) DC390_read8 (CtcReg_High);
	    ResidCnt <<= 8;
	    ResidCnt |= (ULONG) DC390_read8 (CtcReg_Mid);
	    ResidCnt <<= 8;
	    ResidCnt |= (ULONG) DC390_read8 (CtcReg_Low);

	    xferCnt = pSRB->SGToBeXferLen - ResidCnt;
	    pSRB->SGBusAddr += xferCnt;
	    pSRB->TotalXferredLen += xferCnt;
	    pSRB->SGToBeXferLen = ResidCnt;

	    if( residual )
	    {
		bval = DC390_read8 (ScsiFifo);	    /* get one residual byte */
		ptr = (PUCHAR) bus_to_virt( pSRB->SGBusAddr );
		*ptr = bval;
		pSRB->SGBusAddr++; xferCnt++;
		pSRB->TotalXferredLen++;
		pSRB->SGToBeXferLen--;
	    }
	    DEBUG1(printk (KERN_DEBUG "Xfered: %li, Total: %li, Remaining: %li\n", xferCnt,\
			   pSRB->TotalXferredLen, pSRB->SGToBeXferLen);)

	}
    }
}

static void
dc390_Command_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
}

static void
dc390_Status_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{

    pSRB->TargetStatus = DC390_read8 (ScsiFifo);
    //udelay (1);
    pSRB->EndMessage = DC390_read8 (ScsiFifo);	/* get message */

    *psstatus = SCSI_NOP0;
    pSRB->SRBState = SRB_COMPLETED;
    DC390_write8 (ScsiCmd, MSG_ACCEPTED_CMD);
}

static void
dc390_MsgOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    if( pSRB->SRBState & (SRB_UNEXPECT_RESEL+SRB_ABORT_SENT) )
	*psstatus = SCSI_NOP0;
    //DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
}


static void __inline__
dc390_reprog (PACB pACB, PDCB pDCB)
{
  DC390_write8 (Sync_Period, pDCB->SyncPeriod);
  DC390_write8 (Sync_Offset, pDCB->SyncOffset);
  DC390_write8 (CtrlReg3, pDCB->CtrlR3);
  DC390_write8 (CtrlReg4, pDCB->CtrlR4);
  dc390_SetXferRate (pACB, pDCB);
};


#ifdef DC390_DEBUG0
static void
dc390_printMsg (UCHAR *MsgBuf, UCHAR len)
{
  int i;
  printk (" %02x", MsgBuf[0]);
  for (i = 1; i < len; i++)
    printk (" %02x", MsgBuf[i]);
  printk ("\n");
};
#endif

#define DC390_ENABLE_MSGOUT DC390_write8 (ScsiCmd, SET_ATN_CMD)

/* reject_msg */
static void __inline__
dc390_MsgIn_reject (PACB pACB, PSRB pSRB)
{
  pSRB->MsgOutBuf[0] = MSG_REJECT_;
  pSRB->MsgCnt = 1; DC390_ENABLE_MSGOUT;
  DEBUG0 (printk (KERN_INFO "DC390: Reject message\n");)
}

/* abort command */
static void __inline__
dc390_EnableMsgOut_Abort ( PACB pACB, PSRB pSRB )
{
    pSRB->MsgOutBuf[0] = MSG_ABORT; 
    pSRB->MsgCnt = 1; DC390_ENABLE_MSGOUT;
    pSRB->pSRBDCB->DCBFlag &= ~ABORT_DEV_;
}

static PSRB
dc390_MsgIn_QTag (PACB pACB, PDCB pDCB, UCHAR tag)
{
  PSRB lastSRB = pDCB->pGoingLast;
  PSRB pSRB = pDCB->pGoingSRB;

  if (pSRB)
    {
      for( ;pSRB ; )
	{
	  if (pSRB->TagNumber == tag) break;
	  if (pSRB == lastSRB) goto mingx0;
	  pSRB = pSRB->pNextSRB;
	}

      if( pDCB->DCBFlag & ABORT_DEV_ )
	{
	  pSRB->SRBState = SRB_ABORT_SENT;
	  dc390_EnableMsgOut_Abort( pACB, pSRB );
	}

      if( !(pSRB->SRBState & SRB_DISCONNECT) )
	goto  mingx0;

      pDCB->pActiveSRB = pSRB;
      pSRB->SRBState = SRB_DATA_XFER;
    }
  else
    {
    mingx0:
      pSRB = pACB->pTmpSRB;
      pSRB->SRBState = SRB_UNEXPECT_RESEL;
      pDCB->pActiveSRB = pSRB;
      pSRB->MsgOutBuf[0] = MSG_ABORT_TAG;
      pSRB->MsgCnt = 1; DC390_ENABLE_MSGOUT;
    }
  return pSRB;
}


/* set async transfer mode */
static void 
dc390_MsgIn_set_async (PACB pACB, PSRB pSRB)
{
  PDCB pDCB = pSRB->pSRBDCB;
  if (!(pSRB->SRBState & DO_SYNC_NEGO)) 
    printk ("DC390: Target %i initiates Non-Sync?\n", pDCB->UnitSCSIID);
  pSRB->SRBState &= ~DO_SYNC_NEGO;
  pDCB->SyncMode &= ~(SYNC_ENABLE+SYNC_NEGO_DONE);
  pDCB->SyncPeriod = 0;
  pDCB->SyncOffset = 0;
  //pDCB->NegoPeriod = 50; /* 200ns <=> 5 MHz */
  pDCB->CtrlR3 = FAST_CLK;	/* fast clock / normal scsi */
  pDCB->CtrlR4 &= 0x3f;
  pDCB->CtrlR4 |= pACB->glitch_cfg;	/* glitch eater */
  dc390_reprog (pACB, pDCB);
}

/* set sync transfer mode */
static void
dc390_MsgIn_set_sync (PACB pACB, PSRB pSRB)
{
  UCHAR bval;
  USHORT wval, wval1;
  PDCB pDCB = pSRB->pSRBDCB;
  UCHAR oldsyncperiod = pDCB->SyncPeriod;
  UCHAR oldsyncoffset = pDCB->SyncOffset;
  
  if (!(pSRB->SRBState & DO_SYNC_NEGO))
    {
      printk ("DC390: Target %i initiates Sync: %ins %i ... answer ...\n", 
	      pDCB->UnitSCSIID, pSRB->MsgInBuf[3]<<2, pSRB->MsgInBuf[4]);

      /* reject */
      //dc390_MsgIn_reject (pACB, pSRB);
      //return dc390_MsgIn_set_async (pACB, pSRB);

      /* Reply with corrected SDTR Message */
      if (pSRB->MsgInBuf[4] > 15)
	{ 
	  printk ("DC390: Lower Sync Offset to 15\n");
	  pSRB->MsgInBuf[4] = 15;
	}
      if (pSRB->MsgInBuf[3] < pDCB->NegoPeriod)
	{
	  printk ("DC390: Set sync nego period to %ins\n", pDCB->NegoPeriod << 2);
	  pSRB->MsgInBuf[3] = pDCB->NegoPeriod;
	};
      memcpy (pSRB->MsgOutBuf, pSRB->MsgInBuf, 5);
      pSRB->MsgCnt = 5;
      DC390_ENABLE_MSGOUT;
    };

  pSRB->SRBState &= ~DO_SYNC_NEGO;
  pDCB->SyncMode |= SYNC_ENABLE+SYNC_NEGO_DONE;
  pDCB->SyncOffset &= 0x0f0;
  pDCB->SyncOffset |= pSRB->MsgInBuf[4];
  pDCB->NegoPeriod = pSRB->MsgInBuf[3];

  wval = (USHORT) pSRB->MsgInBuf[3];
  wval = wval << 2; wval -= 3; wval1 = wval / 25;	/* compute speed */
  if( (wval1 * 25) != wval) wval1++;
  bval = FAST_CLK+FAST_SCSI;	/* fast clock / fast scsi */

  pDCB->CtrlR4 &= 0x3f;		/* Glitch eater: 12ns less than normal */
  if (pACB->glitch_cfg != NS_TO_GLITCH(0))
    pDCB->CtrlR4 |= NS_TO_GLITCH(((GLITCH_TO_NS(pACB->glitch_cfg)) - 1));
  else
    pDCB->CtrlR4 |= NS_TO_GLITCH(0);
  if (wval1 < 4) pDCB->CtrlR4 |= NS_TO_GLITCH(0); /* Ultra */

  if (wval1 >= 8)
    {
      wval1--;	/* Timing computation differs by 1 from FAST_SCSI */
      bval = FAST_CLK;		/* fast clock / normal scsi */
      pDCB->CtrlR4 |= pACB->glitch_cfg; 	/* glitch eater */
    }

  pDCB->CtrlR3 = bval;
  pDCB->SyncPeriod = (UCHAR)wval1;
  
  if ((oldsyncperiod != wval1 || oldsyncoffset != pDCB->SyncOffset) && pDCB->UnitSCSILUN == 0)
    {
      if (! (bval & FAST_SCSI)) wval1++;
      printk ("DC390: Target %i: Sync transfer %i.%1i MHz, Offset %i\n", pDCB->UnitSCSIID, 
	      40/wval1, ((40%wval1)*10+wval1/2)/wval1, pDCB->SyncOffset & 0x0f);
    }
  
  dc390_reprog (pACB, pDCB);
};


/* According to the docs, the AM53C974 reads the message and 
 * generates a Succesful Operation IRQ before asserting ACK for
 * the last byte (how does it know whether it's the last ?) */
/* The old code handled it in another way, indicating, that on
 * every message byte an IRQ is generated and every byte has to
 * be manually ACKed. Hmmm ?  (KG, 98/11/28) */
/* The old implementation was correct. Sigh! */

/* Check if the message is complete */
static UCHAR __inline__
dc390_MsgIn_complete (UCHAR *msgbuf, ULONG len)
{ 
  if (*msgbuf == MSG_EXTENDED)
    {
      if (len < 2) return 0;
      if (len < msgbuf[1] + 2) return 0;
    }
  else if (*msgbuf >= 0x20 && *msgbuf <= 0x2f) // two byte messages
    if (len < 2) return 0;
  return 1;
}



/* read and eval received messages */
void
dc390_MsgIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    PDCB   pDCB = pACB->pActiveDCB;

    /* Read the msg */

    pSRB->MsgInBuf[pACB->MsgLen++] = DC390_read8 (ScsiFifo);
    //pSRB->SRBState = 0;

    /* Msg complete ? */
    if (dc390_MsgIn_complete (pSRB->MsgInBuf, pACB->MsgLen))
      {
	DEBUG0 (printk (KERN_INFO "DC390: MsgIn:"); dc390_printMsg (pSRB->MsgInBuf, pACB->MsgLen);)
	/* Now eval the msg */
	switch (pSRB->MsgInBuf[0]) 
	  {
	  case MSG_DISCONNECT: 
	    pSRB->SRBState = SRB_DISCONNECT; break;
	    
	  case MSG_SIMPLE_QTAG:
	  case MSG_HEAD_QTAG:
	  case MSG_ORDER_QTAG:
	    pSRB = dc390_MsgIn_QTag (pACB, pDCB, pSRB->MsgInBuf[1]);
	    break;
	    
	  case MSG_REJECT_: 
	    DC390_write8 (ScsiCmd, RESET_ATN_CMD);
	    pDCB->NegoPeriod = 50; /* 200ns <=> 5 MHz */
	    if( pSRB->SRBState & DO_SYNC_NEGO)
	      dc390_MsgIn_set_async (pACB, pSRB);
	    break;
	    
	  case MSG_EXTENDED:
	    /* reject every extended msg but SDTR */
	    if (pSRB->MsgInBuf[1] != 3 || pSRB->MsgInBuf[2] != EXTENDED_SDTR)
	      dc390_MsgIn_reject (pACB, pSRB);
	    else
	      {
		if (pSRB->MsgInBuf[3] == 0 || pSRB->MsgInBuf[4] == 0)
		  dc390_MsgIn_set_async (pACB, pSRB);
		else
		  dc390_MsgIn_set_sync (pACB, pSRB);
	      };
	    
	    // nothing has to be done
	  case MSG_COMPLETE: break;
	    
	    // SAVE POINTER my be ignored as we have the PSRB associated with the
	    // scsi command. Thanks, Gerard, for pointing it out.
	  case MSG_SAVE_PTR: break;
	    // The device might want to restart transfer with a RESTORE
	  case MSG_RESTORE_PTR: 
	    printk ("DC390: RESTORE POINTER message received ... reject\n");
	    // fall through

	    // reject unknown messages
	  default: dc390_MsgIn_reject (pACB, pSRB);
	  }
	
	/* Clear counter and MsgIn state */
	pSRB->SRBState &= ~SRB_MSGIN;
	pACB->MsgLen = 0;
      };

    *psstatus = SCSI_NOP0;
    DC390_write8 (ScsiCmd, MSG_ACCEPTED_CMD);
    //DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
}


void
dc390_DataIO_Comm( PACB pACB, PSRB pSRB, UCHAR ioDir)
{
    PSGL   psgl;
    ULONG  lval;

    if( pSRB->SGIndex < pSRB->SGcount )
    {
	DC390_write8 (DMA_Cmd, DMA_IDLE_CMD | ioDir /* | DMA_INT */);
	if( !pSRB->SGToBeXferLen )
	{
	    psgl = pSRB->pSegmentList;
	    pSRB->SGBusAddr = virt_to_bus( psgl->address );
	    pSRB->SGToBeXferLen = (ULONG) psgl->length;
	    DEBUG1(printk (KERN_DEBUG " DC390: Next SG segment.");)
	}
	lval = pSRB->SGToBeXferLen;
	DEBUG1(printk (KERN_DEBUG " DC390: Transfer %li bytes (address %08lx)\n", lval, pSRB->SGBusAddr);)
	DC390_write8 (CtcReg_Low, (UCHAR) lval);
	lval >>= 8;
	DC390_write8 (CtcReg_Mid, (UCHAR) lval);
	lval >>= 8;
	DC390_write8 (CtcReg_High, (UCHAR) lval);

	DC390_write32 (DMA_XferCnt, pSRB->SGToBeXferLen);
	DC390_write32 (DMA_XferAddr, pSRB->SGBusAddr);

	//DC390_write8 (DMA_Cmd, DMA_IDLE_CMD | ioDir); /* | DMA_INT; */
	pSRB->SRBState = SRB_DATA_XFER;

	DC390_write8 (ScsiCmd, DMA_COMMAND+INFO_XFER_CMD);

	DC390_write8 (DMA_Cmd, DMA_START_CMD | ioDir | DMA_INT);
	//DEBUG1(DC390_write32 (DMA_ScsiBusCtrl, WRT_ERASE_DMA_STAT | EN_INT_ON_PCI_ABORT);)
	//DEBUG1(printk (KERN_DEBUG "DC390: DMA_Status: %02x\n", DC390_read8 (DMA_Status));)
	//DEBUG1(DC390_write32 (DMA_ScsiBusCtrl, EN_INT_ON_PCI_ABORT);)
    }
    else    /* xfer pad */
    {
	if( pSRB->SGcount )
	{
	    pSRB->AdaptStatus = H_OVER_UNDER_RUN;
	    pSRB->SRBStatus |= OVER_RUN;
	    DEBUG0(printk (KERN_WARNING " DC390: Overrun -");)
	}
	DEBUG0(printk (KERN_WARNING " Clear transfer pad \n");)
	DC390_write8 (CtcReg_Low, 0);
	DC390_write8 (CtcReg_Mid, 0);
	DC390_write8 (CtcReg_High, 0);

	pSRB->SRBState |= SRB_XFERPAD;
	DC390_write8 (ScsiCmd, DMA_COMMAND+XFER_PAD_BYTE);
/*
	DC390_write8 (DMA_Cmd, DMA_IDLE_CMD | ioDir); // | DMA_INT;
	DC390_write8 (DMA_Cmd, DMA_START_CMD | ioDir | DMA_INT);
*/
    }
}


static void
dc390_DataOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    dc390_DataIO_Comm (pACB, pSRB, WRITE_DIRECTION);
}

static void
dc390_DataInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    dc390_DataIO_Comm (pACB, pSRB, READ_DIRECTION);
}

void
dc390_CommandPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    PDCB   pDCB;
    UCHAR  i, cnt;
    PUCHAR ptr;

    DC390_write8 (ScsiCmd, RESET_ATN_CMD);
    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
    if( !(pSRB->SRBFlag & AUTO_REQSENSE) )
    {
	cnt = (UCHAR) pSRB->ScsiCmdLen;
	ptr = (PUCHAR) pSRB->CmdBlock;
	for(i=0; i < cnt; i++)
	    DC390_write8 (ScsiFifo, *(ptr++));
    }
    else
    {
	UCHAR bval = 0;
	DC390_write8 (ScsiFifo, REQUEST_SENSE);
	pDCB = pACB->pActiveDCB;
	DC390_write8 (ScsiFifo, pDCB->IdentifyMsg << 5);
	DC390_write8 (ScsiFifo, bval);
	DC390_write8 (ScsiFifo, bval);
	DC390_write8 (ScsiFifo, sizeof(pSRB->pcmd->sense_buffer));
	DC390_write8 (ScsiFifo, bval);
    }
    pSRB->SRBState = SRB_COMMAND;
    DC390_write8 (ScsiCmd, INFO_XFER_CMD);
}

static void
dc390_StatusPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
    pSRB->SRBState = SRB_STATUS;
    DC390_write8 (ScsiCmd, INITIATOR_CMD_CMPLTE);
    //DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
}

void
dc390_MsgOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR   bval, i, cnt;
    PUCHAR  ptr;
    PDCB    pDCB;

    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
    pDCB = pACB->pActiveDCB;
    if( !(pSRB->SRBState & SRB_MSGOUT) )
    {
	cnt = pSRB->MsgCnt;
	if( cnt )
	{
	    ptr = (PUCHAR) pSRB->MsgOutBuf;
	    for(i=0; i < cnt; i++)
		DC390_write8 (ScsiFifo, *(ptr++));
	    pSRB->MsgCnt = 0;
	    if( (pDCB->DCBFlag & ABORT_DEV_) &&
		(pSRB->MsgOutBuf[0] == MSG_ABORT) )
		pSRB->SRBState = SRB_ABORT_SENT;
	}
	else
	{
	    bval = MSG_ABORT;	/* ??? MSG_NOP */
	    if( (pSRB->CmdBlock[0] == INQUIRY ) ||
		(pSRB->CmdBlock[0] == REQUEST_SENSE) ||
		(pSRB->SRBFlag & AUTO_REQSENSE) )
	    {
		if( pDCB->SyncMode & SYNC_ENABLE )
		    goto  mop1;
	    }
	    DC390_write8 (ScsiFifo, bval);
	}
	DC390_write8 (ScsiCmd, INFO_XFER_CMD);
    }
    else
    {
mop1:
	//printk ("DC390: Send SDTR message to %i %i ... \n", pDCB->UnitSCSIID, pDCB->UnitSCSILUN);
	DC390_write8 (ScsiFifo, MSG_EXTENDED);
	DC390_write8 (ScsiFifo, 3);	/*    ;length of extended msg */
	DC390_write8 (ScsiFifo, EXTENDED_SDTR);	/*    ; sync nego */
	DC390_write8 (ScsiFifo, pDCB->NegoPeriod);
	if (pDCB->SyncOffset & 0x0f)
		    DC390_write8 (ScsiFifo, pDCB->SyncOffset);
	else
		    DC390_write8 (ScsiFifo, SYNC_NEGO_OFFSET);		    
	pSRB->SRBState |= DO_SYNC_NEGO;
	DC390_write8 (ScsiCmd, INFO_XFER_CMD);
    }
}

static void
dc390_MsgInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
    if( !(pSRB->SRBState & SRB_MSGIN) )
    {
	pSRB->SRBState &= ~SRB_DISCONNECT;
	pSRB->SRBState |= SRB_MSGIN;
    }
    DC390_write8 (ScsiCmd, INFO_XFER_CMD);
    //DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
}

static void
dc390_Nop_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
}

static void
dc390_Nop_1( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
}


static void
dc390_SetXferRate( PACB pACB, PDCB pDCB )
{
    UCHAR  bval, i, cnt;
    PDCB   ptr;

    if( !(pDCB->IdentifyMsg & 0x07) )
    {
	if( pACB->scan_devices )
	{
	    dc390_CurrSyncOffset = pDCB->SyncOffset;
	}
	else
	{
	    ptr = pACB->pLinkDCB;
	    cnt = pACB->DCBCnt;
	    bval = pDCB->UnitSCSIID;
	    for(i=0; i<cnt; i++)
	    {
		if( ptr->UnitSCSIID == bval )
		{
		    ptr->SyncPeriod = pDCB->SyncPeriod;
		    ptr->SyncOffset = pDCB->SyncOffset;
		    ptr->CtrlR3 = pDCB->CtrlR3;
		    ptr->CtrlR4 = pDCB->CtrlR4;
		    ptr->SyncMode = pDCB->SyncMode;
		}
		ptr = ptr->pNextDCB;
	    }
	}
    }
    return;
}


void
dc390_Disconnect( PACB pACB )
{
    PDCB   pDCB;
    PSRB   pSRB, psrb;
    UCHAR  i, cnt;

    DEBUG0(printk(KERN_INFO "DISC,");)

    pDCB = pACB->pActiveDCB;
    if (!pDCB)
     {
	int j = 400;
	DEBUG0(printk(KERN_WARNING "ACB:%08lx->ActiveDCB:%08lx IOPort:%04x IRQ:%02x !\n",\
	       (ULONG)pACB,(ULONG)pDCB,pACB->IOPortBase,pACB->IRQLevel);)
	while (--j) udelay (1000);
	DC390_read8 (INT_Status);	/* Reset Pending INT */
	DC390_write8 (ScsiCmd, EN_SEL_RESEL);
	return;
     }
    pSRB = pDCB->pActiveSRB;
    pACB->pActiveDCB = 0;
    pSRB->ScsiPhase = SCSI_NOP0;
    DC390_write8 (ScsiCmd, EN_SEL_RESEL);
    if( pSRB->SRBState & SRB_UNEXPECT_RESEL )
    {
	pSRB->SRBState = 0;
	dc390_DoWaitingSRB( pACB );
    }
    else if( pSRB->SRBState & SRB_ABORT_SENT )
    {
	pDCB->TagMask = 0;
	pDCB->DCBFlag = 0;
	cnt = pDCB->GoingSRBCnt;
	pDCB->GoingSRBCnt = 0;
	pSRB = pDCB->pGoingSRB;
	for( i=0; i < cnt; i++)
	{
	    psrb = pSRB->pNextSRB;
	    pSRB->pNextSRB = pACB->pFreeSRB;
	    pACB->pFreeSRB = pSRB;
	    pSRB = psrb;
	}
	pDCB->pGoingSRB = 0;
	dc390_DoWaitingSRB( pACB );
    }
    else
    {
	if( (pSRB->SRBState & (SRB_START_+SRB_MSGOUT)) ||
	   !(pSRB->SRBState & (SRB_DISCONNECT+SRB_COMPLETED)) )
	{	/* Selection time out */
	    if( !(pACB->scan_devices) )
	    {
		pSRB->SRBState = SRB_READY;
		dc390_RewaitSRB( pDCB, pSRB);
	    }
	    else
	    {
		pSRB->TargetStatus = SCSI_STAT_SEL_TIMEOUT;
		goto  disc1;
	    }
	}
	else if( pSRB->SRBState & SRB_DISCONNECT )
	{
	    dc390_DoWaitingSRB( pACB );
	}
	else if( pSRB->SRBState & SRB_COMPLETED )
	{
disc1:
	    if(pDCB->MaxCommand > 1)
	    {
	       pDCB->TagMask &= (~(1 << pSRB->TagNumber));   /* free tag mask */
	    }
	    pDCB->pActiveSRB = 0;
	    pSRB->SRBState = SRB_FREE;
	    dc390_SRBdone( pACB, pDCB, pSRB);
	}
    }
    pACB->MsgLen = 0;
}


void
dc390_Reselect( PACB pACB )
{
    PDCB   pDCB;
    PSRB   pSRB;
    USHORT wval;
    UCHAR  bval;

    DEBUG0(printk(KERN_INFO "RSEL,");)
    pDCB = pACB->pActiveDCB;
    if( pDCB )
    {	/* Arbitration lost but Reselection won */
	DEBUG0(printk ("(ActiveDCB != 0)");)
	pSRB = pDCB->pActiveSRB;
	if( !( pACB->scan_devices ) )
	{
	    pSRB->SRBState = SRB_READY;
	    dc390_RewaitSRB( pDCB, pSRB);
	}
    }
    bval = DC390_read8 (ScsiFifo);	/* get ID */
    DEBUG0(printk ("Dev %02x,", bval);)
    bval ^= 1 << pACB->pScsiHost->this_id; /* Mask AdapterID */
    wval = 0;
    while (bval >>= 1) wval++;
    wval |=  ( (USHORT) DC390_read8 (ScsiFifo) & 7) << 8;  /* get LUN */
    DEBUG0(printk ("(ID %02x, LUN %02x),", wval & 0xff, (wval & 0xff00) >> 8);)
    pDCB = pACB->pLinkDCB;
    while( wval != *((PUSHORT) &pDCB->UnitSCSIID) )
    {
	pDCB = pDCB->pNextDCB;
	if( pDCB == pACB->pLinkDCB )
	 {
	    printk (KERN_ERR "DC390: Reselect from non existing device (ID %02x, LUN %02x)\n",
		    wval & 0xff, (wval & 0xff00) >> 8);
	    return;
	 }
    }
    pACB->pActiveDCB = pDCB;
    if( pDCB->SyncMode & EN_TAG_QUEUEING )
    {
	pSRB = pACB->pTmpSRB; /* ?? */
	pDCB->pActiveSRB = pSRB;
    }
    else
    {
	pSRB = pDCB->pActiveSRB;
	if( !pSRB || !(pSRB->SRBState & SRB_DISCONNECT) )
	{
	    pSRB= pACB->pTmpSRB;
	    pSRB->SRBState = SRB_UNEXPECT_RESEL;
	    printk (KERN_ERR "DC390: Reselect without outstanding cmnd (ID %02x, LUN %02x)\n",
		    wval & 0xff, (wval & 0xff00) >> 8);
	    pDCB->pActiveSRB = pSRB;
	    dc390_EnableMsgOut_Abort ( pACB, pSRB );
	}
	else
	{
	    if( pDCB->DCBFlag & ABORT_DEV_ )
	    {
		pSRB->SRBState = SRB_ABORT_SENT;
		printk (KERN_INFO "DC390: Reselect: Abort (ID %02x, LUN %02x)\n",
			wval & 0xff, (wval & 0xff00) >> 8);
		dc390_EnableMsgOut_Abort( pACB, pSRB );
	    }
	    else
		pSRB->SRBState = SRB_DATA_XFER;
	}
    }

    DEBUG1(printk (KERN_DEBUG "Resel SRB(%p): TagNum (%02x)\n", pSRB, pSRB->TagNumber);)
    pSRB->ScsiPhase = SCSI_NOP0;
    DC390_write8 (Scsi_Dest_ID, pDCB->UnitSCSIID);
    DC390_write8 (Sync_Period, pDCB->SyncPeriod);
    DC390_write8 (Sync_Offset, pDCB->SyncOffset);
    DC390_write8 (CtrlReg1, pDCB->CtrlR1);
    DC390_write8 (CtrlReg3, pDCB->CtrlR3);
    DC390_write8 (CtrlReg4, pDCB->CtrlR4);	/* ; Glitch eater */
    DC390_write8 (ScsiCmd, MSG_ACCEPTED_CMD);	/* ;to release the /ACK signal */
}


static void 
dc390_remove_dev (PACB pACB, PDCB pDCB)
{
   PDCB pPrevDCB = pACB->pLinkDCB;

   pACB->DCBmap[pDCB->UnitSCSIID] &= ~(1 << pDCB->UnitSCSILUN);
   if (pDCB->GoingSRBCnt > 1)
     {
	DCBDEBUG(printk (KERN_INFO "DC390: Driver won't free DCB (ID %i, LUN %i): 0x%08x because of SRBCnt %i\n",\
		pDCB->UnitSCSIID, pDCB->UnitSCSILUN, (int)pDCB, pDCB->GoingSRBCnt);)
	return;
     };
   
   if (pDCB == pACB->pLinkDCB) 
     {
	if (pDCB->pNextDCB == pDCB) pDCB->pNextDCB = 0;
	pACB->pLinkDCB = pDCB->pNextDCB;
	pACB->pLastDCB->pNextDCB = pDCB->pNextDCB;
     }
   else
     {
	while (pPrevDCB->pNextDCB != pDCB) pPrevDCB = pPrevDCB->pNextDCB;
	pPrevDCB->pNextDCB = pDCB->pNextDCB;
	if (pDCB == pACB->pLastDCB) pACB->pLastDCB = pPrevDCB;
     }

   DCBDEBUG(printk (KERN_INFO "DC390: Driver about to free DCB (ID %i, LUN %i): 0x%08x\n",\
	   pDCB->UnitSCSIID, pDCB->UnitSCSILUN, (int)pDCB);)
   kfree (pDCB); if (pDCB == pACB->pActiveDCB) pACB->pActiveDCB = 0;
   pACB->DCBCnt--;
   /* pACB->DeviceCnt--; */
};


static UCHAR __inline__
dc390_tagq_blacklist (char* name)
{
   UCHAR i;
   for(i=0; i<BADDEVCNT; i++)
     if (memcmp (name, dc390_baddevname1[i], 28) == 0)
	return 1;
   return 0;
};
   

static void 
dc390_disc_tagq_set (PDCB pDCB, PSCSI_INQDATA ptr)
{
   /* Check for SCSI format (ANSI and Response data format) */
   if ( (ptr->Vers & 0x07) >= 2 || (ptr->RDF & 0x0F) == 2 )
   {
	if ( (ptr->Flags & SCSI_INQ_CMDQUEUE) &&
	    (pDCB->DevMode & TAG_QUEUEING_) &&
	    /* ((pDCB->DevType == TYPE_DISK) 
		|| (pDCB->DevType == TYPE_MOD)) &&*/
	    !dc390_tagq_blacklist (((char*)ptr)+8) )
	  {
	     pDCB->MaxCommand = pDCB->pDCBACB->TagMaxNum;
	     pDCB->SyncMode |= EN_TAG_QUEUEING /* | EN_ATN_STOP */;
	     pDCB->TagMask = 0;
	  }
	else
	  {
	     /* Do we really need to check for DevType here ? */
	     if ( 0 /*(pDCB->DevMode & EN_DISCONNECT_)*/
		 /* && ((pDCB->DevType == TYPE_DISK) 
		     || (pDCB->DevType == TYPE_MOD))*/ )
		pDCB->SyncMode |= EN_ATN_STOP;
	     else
	       //pDCB->SyncMode &= ~EN_ATN_STOP;
	       pDCB->SyncMode &= ~0;
	  }
     }
};


static void 
dc390_add_dev (PACB pACB, PDCB pDCB, PSCSI_INQDATA ptr)
{
   UCHAR bval1 = ptr->DevType & SCSI_DEVTYPE;
   pDCB->DevType = bval1;
   /* if (bval1 == TYPE_DISK || bval1 == TYPE_MOD) */
	dc390_disc_tagq_set (pDCB, ptr);
};


void
dc390_SRBdone( PACB pACB, PDCB pDCB, PSRB pSRB )
{
    PSRB   psrb;
    UCHAR  bval, status, i;
    PSCSICMD pcmd;
    PSCSI_INQDATA  ptr;
    PSGL   ptr2;
    ULONG  swlval;

    pcmd = pSRB->pcmd;
    status = pSRB->TargetStatus;
    DEBUG0(printk (" SRBdone (%02x,%08x), SRB %p, pid %li\n", status, pcmd->result,\
		pSRB, pcmd->pid);)
    if(pSRB->SRBFlag & AUTO_REQSENSE)
    {	/* Last command was a Request Sense */
	pSRB->SRBFlag &= ~AUTO_REQSENSE;
	pSRB->AdaptStatus = 0;
	pSRB->TargetStatus = SCSI_STAT_CHECKCOND;
#ifdef DC390_REMOVABLEDEBUG
	switch (pcmd->sense_buffer[2] & 0x0f)
	{	    
	 case NOT_READY: printk (KERN_INFO "DC390: ReqSense: NOT_READY (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i)\n",
				 pcmd->cmnd[0], pDCB->UnitSCSIID, pDCB->UnitSCSILUN,
				 status, pACB->scan_devices); break;
	 case UNIT_ATTENTION: printk (KERN_INFO "DC390: ReqSense: UNIT_ATTENTION (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i)\n",
				      pcmd->cmnd[0], pDCB->UnitSCSIID, pDCB->UnitSCSILUN,
				      status, pACB->scan_devices); break;
	 case ILLEGAL_REQUEST: printk (KERN_INFO "DC390: ReqSense: ILLEGAL_REQUEST (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i)\n",
				       pcmd->cmnd[0], pDCB->UnitSCSIID, pDCB->UnitSCSILUN,
				       status, pACB->scan_devices); break;
	 case MEDIUM_ERROR: printk (KERN_INFO "DC390: ReqSense: MEDIUM_ERROR (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i)\n",
				    pcmd->cmnd[0], pDCB->UnitSCSIID, pDCB->UnitSCSILUN,
				    status, pACB->scan_devices); break;
	 case HARDWARE_ERROR: printk (KERN_INFO "DC390: ReqSense: HARDWARE_ERROR (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i)\n",
				      pcmd->cmnd[0], pDCB->UnitSCSIID, pDCB->UnitSCSILUN,
				      status, pACB->scan_devices); break;
	}
#endif
	//pcmd->result = DRIVER_SENSE << 24 | DID_OK << 16 | status;
	if(status == SCSI_STAT_CHECKCOND)
	{
	    pcmd->result = DID_BAD_TARGET << 16;
	    goto ckc_e;
	}
	if(pSRB->RetryCnt == 0)
	{
	    (ULONG)(pSRB->CmdBlock[0]) = pSRB->Segment0[0];
	    pSRB->TotalXferredLen = pSRB->Segment1[1];
	    if( (pSRB->TotalXferredLen) &&
		(pSRB->TotalXferredLen >= pcmd->underflow) )
		pcmd->result |= (DID_OK << 16);
	    else
		pcmd->result = (DRIVER_SENSE << 24) | (DRIVER_OK << 16) |
				SCSI_STAT_CHECKCOND;
	    REMOVABLEDEBUG(printk(KERN_INFO "Cmd=%02x,Result=%08x,XferL=%08x\n",pSRB->CmdBlock[0],\
		(UINT) pcmd->result, (UINT) pSRB->TotalXferredLen);)
	    goto ckc_e;
	}
	else /* Retry */
	{
	    pSRB->RetryCnt--;
	    pSRB->AdaptStatus = 0;
	    pSRB->TargetStatus = 0;
	    *((PULONG) &(pSRB->CmdBlock[0])) = pSRB->Segment0[0];
	    *((PULONG) &(pSRB->CmdBlock[4])) = pSRB->Segment0[1];
	    /* Don't retry on TEST_UNIT_READY */
	    if( pSRB->CmdBlock[0] == TEST_UNIT_READY /* || pSRB->CmdBlock[0] == START_STOP */)
	    {
		pcmd->result = (DRIVER_SENSE << 24) | (DRIVER_OK << 16) 
			    | SCSI_STAT_CHECKCOND;
		REMOVABLEDEBUG(printk(KERN_INFO "Cmd=%02x, Result=%08x, XferL=%08x\n",pSRB->CmdBlock[0],\
		       (UINT) pcmd->result, (UINT) pSRB->TotalXferredLen);)
		goto ckc_e;
	    }
	    pcmd->result |= (DRIVER_SENSE << 24);
	    pSRB->SGcount	 = (UCHAR) pSRB->Segment1[0];
	    pSRB->ScsiCmdLen	 = (UCHAR) (pSRB->Segment1[0] >> 8);
	    pSRB->SGIndex = 0;
	    pSRB->TotalXferredLen = 0;
	    pSRB->SGToBeXferLen = 0;
	    if( pcmd->use_sg )
		pSRB->pSegmentList = (PSGL) pcmd->request_buffer;
	    else if( pcmd->request_buffer )
	    {
		pSRB->pSegmentList = (PSGL) &pSRB->Segmentx;
		pSRB->Segmentx.address = (PUCHAR) pcmd->request_buffer;
		pSRB->Segmentx.length = pcmd->request_bufflen;
	    }
	    if( dc390_StartSCSI( pACB, pDCB, pSRB ) )
		dc390_RewaitSRB( pDCB, pSRB );
	    return;
	}
    }
    if( status )
    {
	if( status == SCSI_STAT_CHECKCOND)
	{
	    REMOVABLEDEBUG(printk (KERN_INFO "DC390: Scsi_Stat_CheckCond (Cmd %02x, Id %02x, LUN %02x)\n",\
		    pcmd->cmnd[0], pDCB->UnitSCSIID, pDCB->UnitSCSILUN);)
	    if( (pSRB->SGIndex < pSRB->SGcount) && (pSRB->SGcount) && (pSRB->SGToBeXferLen) )
	    {
		bval = pSRB->SGcount;
		swlval = 0;
		ptr2 = pSRB->pSegmentList;
		for( i=pSRB->SGIndex; i < bval; i++)
		{
		    swlval += ptr2->length;
		    ptr2++;
		}
		REMOVABLEDEBUG(printk(KERN_INFO "XferredLen=%08x,NotXferLen=%08x\n",\
			(UINT) pSRB->TotalXferredLen, (UINT) swlval);)
	    }
	    dc390_RequestSense( pACB, pDCB, pSRB );
	    return;
	}
	else if( status == SCSI_STAT_QUEUEFULL )
	{
	    bval = (UCHAR) pDCB->GoingSRBCnt;
	    bval--;
	    pDCB->MaxCommand = bval;
	    dc390_RewaitSRB( pDCB, pSRB );
	    pSRB->AdaptStatus = 0;
	    pSRB->TargetStatus = 0;
	    return;
	}
	else if(status == SCSI_STAT_SEL_TIMEOUT)
	{
	    pSRB->AdaptStatus = H_SEL_TIMEOUT;
	    pSRB->TargetStatus = 0;
	    pcmd->result = DID_BAD_TARGET << 16;
	    /* Devices are removed below ... */
	}
	else if (status == SCSI_STAT_BUSY && 
		 (pSRB->CmdBlock[0] == TEST_UNIT_READY || pSRB->CmdBlock[0] == INQUIRY) &&
		 pACB->scan_devices)
	{
	    pSRB->AdaptStatus = 0;
	    pSRB->TargetStatus = status;
	    pcmd->result = (ULONG) (pSRB->EndMessage << 8)
				/* | (ULONG) status */;
	}
	else
	{   /* Another error */
	    pSRB->AdaptStatus = 0;
	    if( pSRB->RetryCnt )
	    {	/* Retry */
		pSRB->RetryCnt--;
		pSRB->TargetStatus = 0;
		pSRB->SGIndex = 0;
		pSRB->TotalXferredLen = 0;
		pSRB->SGToBeXferLen = 0;
		if( pcmd->use_sg )
		    pSRB->pSegmentList = (PSGL) pcmd->request_buffer;
		else if( pcmd->request_buffer )
		{
		    pSRB->pSegmentList = (PSGL) &pSRB->Segmentx;
		    pSRB->Segmentx.address = (PUCHAR) pcmd->request_buffer;
		    pSRB->Segmentx.length = pcmd->request_bufflen;
		}
		if( dc390_StartSCSI( pACB, pDCB, pSRB ) )
		    dc390_RewaitSRB( pDCB, pSRB );
		return;
	    }
	    else
	    {	/* Report error */
		pcmd->result |= (DID_ERROR << 16) | (ULONG) (pSRB->EndMessage << 8) |
			       (ULONG) status;
	    }
	}
    }
    else
    {	/*  Target status == 0 */
	status = pSRB->AdaptStatus;
	if(status & H_OVER_UNDER_RUN)
	{
	    pSRB->TargetStatus = 0;
	    pcmd->result |= (DID_OK << 16) | (pSRB->EndMessage << 8);
	}
	else if( pSRB->SRBStatus & PARITY_ERROR)
	{
	    pcmd->result |= (DID_PARITY << 16) | (pSRB->EndMessage << 8);
	}
	else		       /* No error */
	{
	    pSRB->AdaptStatus = 0;
	    pSRB->TargetStatus = 0;
	    pcmd->result |= (DID_OK << 16);
	}
    }

ckc_e:
    if( pACB->scan_devices )
    {
	if( pSRB->CmdBlock[0] == TEST_UNIT_READY )
	{
#ifdef DC390_DEBUG0
	    printk (KERN_INFO "DC390: Test_Unit_Ready: result: %08x", pcmd->result);
	    if (pcmd->result & DRIVER_SENSE << 24) printk (" (sense: %02x %02x %02x %02x)\n",
				   pcmd->sense_buffer[0], pcmd->sense_buffer[1],
				   pcmd->sense_buffer[2], pcmd->sense_buffer[3]);
	    else printk ("\n");
#endif
	    if((pcmd->result != (DID_OK << 16) && !(pcmd->result & SCSI_STAT_CHECKCOND) && !(pcmd->result & SCSI_STAT_BUSY)) ||
	       ((pcmd->result & DRIVER_SENSE << 24) && (pcmd->sense_buffer[0] & 0x70) == 0x70 &&
		(pcmd->sense_buffer[2] & 0xf) == ILLEGAL_REQUEST) || pcmd->result & DID_ERROR << 16)
	    {
	       /* device not present: remove */ 
	       dc390_remove_dev (pACB, pDCB);
	       
	       if( (pcmd->target == pACB->pScsiHost->max_id - 1) &&
		  ((pcmd->lun == 0) || (pcmd->lun == pACB->pScsiHost->max_lun - 1)) )
		 pACB->scan_devices = 0;
	    }
	    else
	    {
	        /* device present: add */
		if( (pcmd->target == pACB->pScsiHost->max_id - 1) && 
		    (pcmd->lun == pACB->pScsiHost->max_lun - 1) )
		    pACB->scan_devices = END_SCAN ;
	        /* pACB->DeviceCnt++; */ /* Dev is added on INQUIRY */
	    }
	}
    }
   
   if( pSRB->CmdBlock[0] == INQUIRY && 
      (pcmd->result == DID_OK << 16 || pcmd->result & SCSI_STAT_CHECKCOND) )
     {
	ptr = (PSCSI_INQDATA) (pcmd->request_buffer);
	if( pcmd->use_sg )
	  ptr = (PSCSI_INQDATA) (((PSGL) ptr)->address);
	if ((ptr->DevType & SCSI_DEVTYPE) == TYPE_NODEV)
	  {
	     /* device not present: remove */
	     dc390_remove_dev (pACB, pDCB);
	  }
	else
	  {
	     /* device found: add */ 
	     dc390_add_dev (pACB, pDCB, ptr);
	     if (pACB->scan_devices) pACB->DeviceCnt++;
	  }
	if( (pcmd->target == pACB->pScsiHost->max_id - 1) &&
	    (pcmd->lun == pACB->pScsiHost->max_lun - 1) )
	  pACB->scan_devices = 0;
     };
/*  dc390_ReleaseSRB( pDCB, pSRB ); */

    if(pSRB == pDCB->pGoingSRB )
    {
	pDCB->pGoingSRB = pSRB->pNextSRB;
    }
    else
    {
	psrb = pDCB->pGoingSRB;
	while( psrb->pNextSRB != pSRB )
	    psrb = psrb->pNextSRB;
	psrb->pNextSRB = pSRB->pNextSRB;
	if( pSRB == pDCB->pGoingLast )
	    pDCB->pGoingLast = psrb;
    }
    pSRB->pNextSRB = pACB->pFreeSRB;
    pACB->pFreeSRB = pSRB;
    pDCB->GoingSRBCnt--;

    dc390_DoWaitingSRB( pACB );

    DC390_UNLOCK_ACB_NI;
    pcmd->scsi_done( pcmd );
    DC390_LOCK_ACB_NI;

    if( pDCB->QIORBCnt )
	dc390_DoNextCmd( pACB, pDCB );
    return;
}


/* Remove all SRBs and tell midlevel code DID_RESET */
void
dc390_DoingSRB_Done( PACB pACB )
{
    PDCB   pDCB, pdcb;
    PSRB   psrb, psrb2;
    UCHAR  i;
    PSCSICMD pcmd;

    pDCB = pACB->pLinkDCB;
    pdcb = pDCB;
    if (! pdcb) return;
    do
    {
	psrb = pdcb->pGoingSRB;
	for( i=0; i<pdcb->GoingSRBCnt; i++)
	{
	    psrb2 = psrb->pNextSRB;
	    pcmd = psrb->pcmd;
	    pcmd->result = DID_RESET << 16;

/*	    ReleaseSRB( pDCB, pSRB ); */

	    psrb->pNextSRB = pACB->pFreeSRB;
	    pACB->pFreeSRB = psrb;

	    DC390_UNLOCK_ACB_NI;
	    pcmd->scsi_done( pcmd );
	    DC390_LOCK_ACB_NI;
	    psrb  = psrb2;
	}
	pdcb->GoingSRBCnt = 0;;
	pdcb->pGoingSRB = NULL;
	pdcb->TagMask = 0;
	pdcb = pdcb->pNextDCB;
    } while( pdcb != pDCB );
}


static void
dc390_ResetSCSIBus( PACB pACB )
{
    pACB->ACBFlag |= RESET_DEV;

    DC390_write8 (ScsiCmd, RST_DEVICE_CMD);
    udelay (250);
    DC390_write8 (ScsiCmd, NOP_CMD);

    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
    DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
    DC390_write8 (ScsiCmd, RST_SCSI_BUS_CMD);

    return;
}

static void
dc390_ScsiRstDetect( PACB pACB )
{
    printk ("DC390: Rst_Detect: laststat = %08lx\n", dc390_laststatus);
    //DEBUG0(printk(KERN_INFO "RST_DETECT,");)

    DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
    /* Unlock before ? */
    /* delay a second */
    { unsigned int msec = 1*1000; while (--msec) udelay(1000); }
    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);

    if( pACB->ACBFlag & RESET_DEV )
	pACB->ACBFlag |= RESET_DONE;
    else
    {
	pACB->ACBFlag |= RESET_DETECT;

	dc390_ResetDevParam( pACB );
/*	dc390_DoingSRB_Done( pACB ); ???? */
	dc390_RecoverSRB( pACB );
	pACB->pActiveDCB = NULL;
	pACB->ACBFlag = 0;
	dc390_DoWaitingSRB( pACB );
    }
    return;
}


static void __inline__
dc390_RequestSense( PACB pACB, PDCB pDCB, PSRB pSRB )
{
    PSCSICMD  pcmd;

    REMOVABLEDEBUG(printk (KERN_INFO "DC390: RequestSense (Cmd %02x, Id %02x, LUN %02x)\n",\
	    pSRB->CmdBlock[0], pDCB->UnitSCSIID, pDCB->UnitSCSILUN);)

    pSRB->SRBFlag |= AUTO_REQSENSE;
    pSRB->Segment0[0] = (ULONG) pSRB->CmdBlock[0];
    pSRB->Segment0[1] = (ULONG) pSRB->CmdBlock[4];
    pSRB->Segment1[0] = (ULONG) ((pSRB->ScsiCmdLen << 8) + pSRB->SGcount);
    pSRB->Segment1[1] = pSRB->TotalXferredLen;
    pSRB->AdaptStatus = 0;
    pSRB->TargetStatus = 0; /* SCSI_STAT_CHECKCOND; */

    pcmd = pSRB->pcmd;

    pSRB->Segmentx.address = (PUCHAR) &(pcmd->sense_buffer);
    pSRB->Segmentx.length = sizeof(pcmd->sense_buffer);
    pSRB->pSegmentList = &pSRB->Segmentx;
    pSRB->SGcount = 1;
    pSRB->SGIndex = 0;

    pSRB->CmdBlock[0] = REQUEST_SENSE;
    pSRB->CmdBlock[1] = pDCB->IdentifyMsg << 5;
    (USHORT) pSRB->CmdBlock[2] = 0;
    (USHORT) pSRB->CmdBlock[4] = sizeof(pcmd->sense_buffer);
    pSRB->ScsiCmdLen = 6;

    pSRB->TotalXferredLen = 0;
    pSRB->SGToBeXferLen = 0;
    if( dc390_StartSCSI( pACB, pDCB, pSRB ) )
	dc390_RewaitSRB( pDCB, pSRB );
}



static void __inline__
dc390_InvalidCmd( PACB pACB )
{
    if( pACB->pActiveDCB->pActiveSRB->SRBState & (SRB_START_+SRB_MSGOUT) )
	DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
}

