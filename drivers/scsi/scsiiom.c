/***********************************************************************
 *	FILE NAME : SCSIIOM.C					       *
 *	     BY   : C.L. Huang,    ching@tekram.com.tw		       *
 *	Description: Device Driver for Tekram DC-390 (T) PCI SCSI      *
 *		     Bus Master Host Adapter			       *
 ***********************************************************************/


static USHORT
DC390_StartSCSI( PACB pACB, PDCB pDCB, PSRB pSRB )
{
    USHORT ioport, rc;
    UCHAR  bval, bval1, i, cnt;
    PUCHAR ptr;
    ULONG  wlval;

    pSRB->TagNumber = 31;
    ioport = pACB->IOPortBase;
    bval = pDCB->UnitSCSIID;
    outb(bval,ioport+Scsi_Dest_ID);
    bval = pDCB->SyncPeriod;
    outb(bval,ioport+Sync_Period);
    bval = pDCB->SyncOffset;
    outb(bval,ioport+Sync_Offset);
    bval = pDCB->CtrlR1;
    outb(bval,ioport+CtrlReg1);
    bval = pDCB->CtrlR3;
    outb(bval,ioport+CtrlReg3);
    bval = pDCB->CtrlR4;
    outb(bval,ioport+CtrlReg4);
    bval = CLEAR_FIFO_CMD;	   /* Flush FIFO */
    outb(bval,ioport+ScsiCmd);

    pSRB->ScsiPhase = SCSI_NOP0;
    bval = pDCB->IdentifyMsg;
    if( !(pDCB->SyncMode & EN_ATN_STOP) )
    {
	if( (pSRB->CmdBlock[0] == INQUIRY) ||
	    (pSRB->CmdBlock[0] == REQUEST_SENSE) ||
	    (pSRB->SRBFlag & AUTO_REQSENSE) )
	{
	    bval &= 0xBF;	/* NO disconnection */
	    outb(bval,ioport+ScsiFifo);
	    bval1 = SELECT_W_ATN;
	    pSRB->SRBState = SRB_START_;
	    if( pDCB->SyncMode & SYNC_ENABLE )
	    {
		if( !(pDCB->IdentifyMsg & 7) ||
		    (pSRB->CmdBlock[0] != INQUIRY) )
		{
		    bval1 = SEL_W_ATN_STOP;
		    pSRB->SRBState = SRB_MSGOUT;
		}
	    }
	}
	else
	{
	    if(pDCB->SyncMode & EN_TAG_QUEUING)
	    {
		outb(bval,ioport+ScsiFifo);
		bval = MSG_SIMPLE_QTAG;
		outb(bval,ioport+ScsiFifo);
		wlval = 1;
		bval = 0;
		while( wlval & pDCB->TagMask )
		{
		    wlval = wlval << 1;
		    bval++;
		}
		outb(bval,ioport+ScsiFifo);
		pDCB->TagMask |= wlval;
		pSRB->TagNumber = bval;
		bval1 = SEL_W_ATN2;
		pSRB->SRBState = SRB_START_;
	    }
	    else
	    {
		outb(bval,ioport+ScsiFifo);
		bval1 = SELECT_W_ATN;
		pSRB->SRBState = SRB_START_;
	    }
	}

	if( pSRB->SRBFlag & AUTO_REQSENSE )
	{
	    bval = REQUEST_SENSE;
	    outb(bval,ioport+ScsiFifo);
	    bval = pDCB->IdentifyMsg << 5;
	    outb(bval,ioport+ScsiFifo);
	    bval = 0;
	    outb(bval,ioport+ScsiFifo);
	    outb(bval,ioport+ScsiFifo);
	    bval = sizeof(pSRB->pcmd->sense_buffer);
	    outb(bval,ioport+ScsiFifo);
	    bval = 0;
	    outb(bval,ioport+ScsiFifo);
	}
	else
	{
	    cnt = pSRB->ScsiCmdLen;
	    ptr = (PUCHAR) pSRB->CmdBlock;
	    for(i=0; i<cnt; i++)
	    {
		bval = *ptr++;
		outb(bval,ioport+ScsiFifo);
	    }
	}
    }
    else	/* ATN_STOP */
    {
	if( (pSRB->CmdBlock[0] == INQUIRY) ||
	    (pSRB->CmdBlock[0] == REQUEST_SENSE) ||
	    (pSRB->SRBFlag & AUTO_REQSENSE) )
	{
	    bval &= 0xBF;
	    outb(bval,ioport+ScsiFifo);
	    bval1 = SELECT_W_ATN;
	    pSRB->SRBState = SRB_START_;
	    if( pDCB->SyncMode & SYNC_ENABLE )
	    {
		if( !(pDCB->IdentifyMsg & 7) ||
		    (pSRB->CmdBlock[0] != INQUIRY) )
		{
		    bval1 = SEL_W_ATN_STOP;
		    pSRB->SRBState = SRB_MSGOUT;
		}
	    }
	}
	else
	{
	    if(pDCB->SyncMode & EN_TAG_QUEUING)
	    {
		outb(bval,ioport+ScsiFifo);
		pSRB->MsgOutBuf[0] = MSG_SIMPLE_QTAG;
		wlval = 1;
		bval = 0;
		while( wlval & pDCB->TagMask )
		{
		    wlval = wlval << 1;
		    bval++;
		}
		pDCB->TagMask |= wlval;
		pSRB->TagNumber = bval;
		pSRB->MsgOutBuf[1] = bval;
		pSRB->MsgCnt = 2;
		bval1 = SEL_W_ATN_STOP;
		pSRB->SRBState = SRB_START_;
	    }
	    else
	    {
		outb(bval,ioport+ScsiFifo);
		pSRB->MsgOutBuf[0] = MSG_NOP;
		pSRB->MsgCnt = 1;
		pSRB->SRBState = SRB_START_;
		bval1 = SEL_W_ATN_STOP;
	    }
	}
    }
    bval = inb( ioport+Scsi_Status );
    if( bval & INTERRUPT )
    {
	pSRB->SRBState = SRB_READY;
	pDCB->TagMask &= ~( 1 << pSRB->TagNumber );
	rc = 1;
    }
    else
    {
	pSRB->ScsiPhase = SCSI_NOP1;
	pACB->pActiveDCB = pDCB;
	pDCB->pActiveSRB = pSRB;
	rc = 0;
	outb(bval1,ioport+ScsiCmd);
    }
    return( rc );
}


#ifndef  VERSION_ELF_1_2_13
static void
DC390_Interrupt( int irq, void *dev_id, struct pt_regs *regs)
#else
static void
DC390_Interrupt( int irq, struct pt_regs *regs)
#endif
{
    PACB   pACB;
    PDCB   pDCB;
    PSRB   pSRB;
    USHORT ioport = 0;
    USHORT phase, i;
    void   (*stateV)( PACB, PSRB, PUCHAR );
    UCHAR  istate = 0;
    UCHAR  sstatus=0, istatus;

    pACB = pACB_start;
    if( pACB == NULL )
	return;
    for( i=0; i < adapterCnt; i++ )
    {
	if( pACB->IRQLevel == (UCHAR) irq )
	{
	     ioport = pACB->IOPortBase;
	     sstatus = inb( ioport+Scsi_Status );
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

#ifdef DC390_DEBUG1
	printk("sstatus=%2x,",sstatus);
#endif

    if( pACB == (PACB )-1 )
    {
	printk("DC390: Spurious interrupt detected!\n");
	return;
    }

    istate = inb( ioport+Intern_State );
    istatus = inb( ioport+INT_Status );

#ifdef DC390_DEBUG1
	printk("Istatus=%2x,",istatus);
#endif

    if(istatus &  DISCONNECTED)
    {
	DC390_Disconnect( pACB );
	return;
    }

    if(istatus &  RESELECTED)
    {
	DC390_Reselect( pACB );
	return;
    }

    if(istatus &  INVALID_CMD)
    {
	DC390_InvalidCmd( pACB );
	return;
    }

    if(istatus &  SCSI_RESET)
    {
	DC390_ScsiRstDetect( pACB );
	return;
    }

    if( istatus & (SUCCESSFUL_OP+SERVICE_REQUEST) )
    {
	pDCB = pACB->pActiveDCB;
	pSRB = pDCB->pActiveSRB;
	if( pDCB )
	{
	    if( pDCB->DCBFlag & ABORT_DEV_ )
		EnableMsgOut( pACB, pSRB );
	}

	phase = (USHORT) pSRB->ScsiPhase;
	stateV = (void *) DC390_phase0[phase];
	stateV( pACB, pSRB, &sstatus );

	pSRB->ScsiPhase = sstatus & 7;
	phase = (USHORT) sstatus & 7;
	stateV = (void *) DC390_phase1[phase];
	stateV( pACB, pSRB, &sstatus );
    }
}


static void
DC390_DataOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR   sstatus, bval;
    USHORT  ioport;
    PSGL    psgl;
    ULONG   ResidCnt, xferCnt;

    ioport = pACB->IOPortBase;
    sstatus = *psstatus;

    if( !(pSRB->SRBState & SRB_XFERPAD) )
    {
	if( sstatus & PARITY_ERR )
	    pSRB->SRBStatus |= PARITY_ERROR;

	if( sstatus & COUNT_2_ZERO )
	{
	    bval = inb(ioport+DMA_Status);
	    while( !(bval & DMA_XFER_DONE) )
		bval = inb(ioport+DMA_Status);
	    pSRB->TotalXferredLen += pSRB->SGToBeXferLen;
	    pSRB->SGIndex++;
	    if( pSRB->SGIndex < pSRB->SGcount )
	    {
		pSRB->pSegmentList++;
		psgl = pSRB->pSegmentList;

#ifndef VERSION_ELF_1_2_13
		pSRB->SGPhysAddr = virt_to_phys( psgl->address );
#else
		pSRB->SGPhysAddr = (ULONG) psgl->address;
#endif
		pSRB->SGToBeXferLen = (ULONG) psgl->length;
	    }
	    else
		pSRB->SGToBeXferLen = 0;
	}
	else
	{
	    bval = inb( ioport+Current_Fifo );
	    bval &= 0x1f;
	    ResidCnt = (ULONG) inb(ioport+CtcReg_High);
	    ResidCnt = ResidCnt << 8;
	    ResidCnt |= (ULONG) inb(ioport+CtcReg_Mid);
	    ResidCnt = ResidCnt << 8;
	    ResidCnt |= (ULONG) inb(ioport+CtcReg_Low);
	    ResidCnt += (ULONG) bval;

	    xferCnt = pSRB->SGToBeXferLen - ResidCnt;
	    pSRB->SGPhysAddr += xferCnt;
	    pSRB->TotalXferredLen += xferCnt;
	    pSRB->SGToBeXferLen = ResidCnt;
	}
    }
    bval = WRITE_DIRECTION+DMA_IDLE_CMD;
    outb( bval, ioport+DMA_Cmd);
}

static void
DC390_DataIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR   sstatus, bval;
    USHORT  i, ioport, residual;
    PSGL    psgl;
    ULONG   ResidCnt, xferCnt;
    PUCHAR  ptr;


    ioport = pACB->IOPortBase;
    sstatus = *psstatus;

    if( !(pSRB->SRBState & SRB_XFERPAD) )
    {
	if( sstatus & PARITY_ERR )
	    pSRB->SRBStatus |= PARITY_ERROR;

	if( sstatus & COUNT_2_ZERO )
	{
	    bval = inb(ioport+DMA_Status);
	    while( !(bval & DMA_XFER_DONE) )
		bval = inb(ioport+DMA_Status);

	    bval = READ_DIRECTION+DMA_IDLE_CMD;
	    outb( bval, ioport+DMA_Cmd);

	    pSRB->TotalXferredLen += pSRB->SGToBeXferLen;
	    pSRB->SGIndex++;
	    if( pSRB->SGIndex < pSRB->SGcount )
	    {
		pSRB->pSegmentList++;
		psgl = pSRB->pSegmentList;

#ifndef VERSION_ELF_1_2_13
		pSRB->SGPhysAddr = virt_to_phys( psgl->address );
#else
		pSRB->SGPhysAddr = (ULONG) psgl->address;
#endif
		pSRB->SGToBeXferLen = (ULONG) psgl->length;
	    }
	    else
		pSRB->SGToBeXferLen = 0;
	}
	else	/* phase changed */
	{
	    residual = 0;
	    bval = inb(ioport+Current_Fifo);
	    while( bval & 0x1f )
	    {
		if( (bval & 0x1f) == 1 )
		{
		    for(i=0; i< 0x100; i++)
		    {
			bval = inb(ioport+Current_Fifo);
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
		    bval = inb(ioport+Current_Fifo);
	    }
din_1:
	    bval = READ_DIRECTION+DMA_BLAST_CMD;
	    outb(bval, ioport+DMA_Cmd);
	    for(i=0; i<0x8000; i++)
	    {
		bval = inb(ioport+DMA_Status);
		if(bval & BLAST_COMPLETE)
		    break;
	    }
	    bval = READ_DIRECTION+DMA_IDLE_CMD;
	    outb(bval, ioport+DMA_Cmd);

	    ResidCnt = (ULONG) inb(ioport+CtcReg_High);
	    ResidCnt = ResidCnt << 8;
	    ResidCnt |= (ULONG) inb(ioport+CtcReg_Mid);
	    ResidCnt = ResidCnt << 8;
	    ResidCnt |= (ULONG) inb(ioport+CtcReg_Low);

	    xferCnt = pSRB->SGToBeXferLen - ResidCnt;
	    pSRB->SGPhysAddr += xferCnt;
	    pSRB->TotalXferredLen += xferCnt;
	    pSRB->SGToBeXferLen = ResidCnt;

	    if( residual )
	    {
		bval = inb(ioport+ScsiFifo);	    /* get residual byte */
#ifndef VERSION_ELF_1_2_13
		ptr = (PUCHAR) phys_to_virt( pSRB->SGPhysAddr );
#else
		ptr = (PUCHAR) pSRB->SGPhysAddr;
#endif
		*ptr = bval;
		pSRB->SGPhysAddr++;
		pSRB->TotalXferredLen++;
		pSRB->SGToBeXferLen--;
	    }
	}
    }
}

static void
DC390_Command_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
}

static void
DC390_Status_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR  bval;
    USHORT ioport;

    ioport = pACB->IOPortBase;
    bval = inb(ioport+ScsiFifo);
    pSRB->TargetStatus = bval;
    bval++;
    bval = inb(ioport+ScsiFifo);	/* get message */
    pSRB->EndMessage = bval;

    *psstatus = SCSI_NOP0;
    pSRB->SRBState = SRB_COMPLETED;
    bval = MSG_ACCEPTED_CMD;
    outb(bval, ioport+ScsiCmd);
}

static void
DC390_MsgOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    if( pSRB->SRBState & (SRB_UNEXPECT_RESEL+SRB_ABORT_SENT) )
	*psstatus = SCSI_NOP0;
}

static void
DC390_MsgIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR  bval;
    USHORT ioport, wval, wval1;
    PDCB   pDCB;
    PSRB   psrb;

    ioport = pACB->IOPortBase;
    pDCB = pACB->pActiveDCB;

    bval = inb( ioport+ScsiFifo );
    if( !(pSRB->SRBState & SRB_MSGIN_MULTI) )
    {
	if(bval == MSG_DISCONNECT)
	{
	    pSRB->SRBState = SRB_DISCONNECT;
	}
	else if( bval == MSG_SAVE_PTR )
	    goto  min6;
	else if( (bval == MSG_EXTENDED) || ((bval >= MSG_SIMPLE_QTAG) &&
					    (bval <= MSG_ORDER_QTAG)) )
	{
	    pSRB->SRBState |= SRB_MSGIN_MULTI;
	    pSRB->MsgInBuf[0] = bval;
	    pSRB->MsgCnt = 1;
	    pSRB->pMsgPtr = &pSRB->MsgInBuf[1];
	}
	else if(bval == MSG_REJECT_)
	{
	    bval = RESET_ATN_CMD;
	    outb(bval, ioport+ScsiCmd);
	    if( pSRB->SRBState & DO_SYNC_NEGO)
		goto  set_async;
	}
	else if( bval == MSG_RESTORE_PTR)
	    goto  min6;
	else
	    goto  min6;
    }
    else
    {	/* minx: */

	*pSRB->pMsgPtr = bval;
	pSRB->MsgCnt++;
	pSRB->pMsgPtr++;
	if( (pSRB->MsgInBuf[0] >= MSG_SIMPLE_QTAG) &&
	    (pSRB->MsgInBuf[0] <= MSG_ORDER_QTAG) )
	{
	    if( pSRB->MsgCnt == 2)
	    {
		pSRB->SRBState = 0;
		bval = pSRB->MsgInBuf[1];
		pSRB = pDCB->pGoingSRB;
		psrb = pDCB->pGoingLast;
		if( pSRB )
		{
		    for( ;; )
		    {
			if(pSRB->TagNumber != bval)
			{
			    if( pSRB == psrb )
				goto  mingx0;
			    pSRB = pSRB->pNextSRB;
			}
			else
			    break;
		    }
		    if( pDCB->DCBFlag & ABORT_DEV_ )
		    {
			pSRB->SRBState = SRB_ABORT_SENT;
			EnableMsgOut( pACB, pSRB );
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
		    EnableMsgOut2( pACB, pSRB );
		}
	    }
	}
	else if( (pSRB->MsgInBuf[0] == MSG_EXTENDED) && (pSRB->MsgCnt == 5) )
	{
	    pSRB->SRBState &= ~(SRB_MSGIN_MULTI+DO_SYNC_NEGO);
	    if( (pSRB->MsgInBuf[1] != 3) || (pSRB->MsgInBuf[2] != 1) )
	    {	/* reject_msg: */
		pSRB->MsgCnt = 1;
		pSRB->MsgInBuf[0] = MSG_REJECT_;
		bval = SET_ATN_CMD;
		outb(bval, ioport+ScsiCmd);
	    }
	    else if( !(pSRB->MsgInBuf[3]) || !(pSRB->MsgInBuf[4]) )
	    {
set_async:
		pDCB = pSRB->pSRBDCB;
		pDCB->SyncMode &= ~(SYNC_ENABLE+SYNC_NEGO_DONE);
		pDCB->SyncPeriod = 0;
		pDCB->SyncOffset = 0;
		pDCB->CtrlR3 = FAST_CLK;     /* ;non_fast */
		pDCB->CtrlR4 &= 0x3f;
		pDCB->CtrlR4 |= EATER_25NS;  /* ; 25ns glitch eater */
		goto  re_prog;
	    }
	    else
	    {	/* set_sync: */

		pDCB = pSRB->pSRBDCB;
		pDCB->SyncMode |= SYNC_ENABLE+SYNC_NEGO_DONE;
		pDCB->SyncOffset &= 0x0f0;
		pDCB->SyncOffset |= pSRB->MsgInBuf[4];
		pDCB->NegoPeriod = pSRB->MsgInBuf[3];
		wval = (USHORT) pSRB->MsgInBuf[3];
		wval = wval << 2;
		wval--;
		wval1 = wval / 25;
		if( (wval1 * 25) != wval)
		    wval1++;
		bval = FAST_CLK+FAST_SCSI;
		pDCB->CtrlR4 &= 0x3f;
		if(wval1 >= 8)
		{
		    wval1--;
		    bval = FAST_CLK;	    /* ;fast clock/normal scsi */
		    pDCB->CtrlR4 |= EATER_25NS;   /* ;25 ns glitch eater */
		}
		pDCB->CtrlR3 = bval;
		pDCB->SyncPeriod = (UCHAR)wval1;
re_prog:
		bval = pDCB->SyncPeriod;
		outb(bval, ioport+Sync_Period);
		bval = pDCB->SyncOffset;
		outb(bval, ioport+Sync_Offset);
		bval = pDCB->CtrlR3;
		outb(bval, ioport+CtrlReg3);
		bval = pDCB->CtrlR4;
		outb(bval, ioport+CtrlReg4);
		SetXferRate( pACB, pDCB);
	    }
	}
    }
min6:
    *psstatus = SCSI_NOP0;
    bval = MSG_ACCEPTED_CMD;
    outb(bval, ioport+ScsiCmd);
}

static void
DataIO_Comm( PACB pACB, PSRB pSRB, UCHAR ioDir)
{
    PSGL   psgl;
    UCHAR  bval;
    USHORT ioport;
    ULONG  lval;


    ioport = pACB->IOPortBase;
    if( pSRB->SGIndex < pSRB->SGcount )
    {
	bval = DMA_IDLE_CMD | ioDir;   /* ;+EN_DMA_INT */
	outb( bval, ioport+DMA_Cmd);
	if( !pSRB->SGToBeXferLen )
	{
	    psgl = pSRB->pSegmentList;
#ifndef VERSION_ELF_1_2_13
	    pSRB->SGPhysAddr = virt_to_phys( psgl->address );
#else
	    pSRB->SGPhysAddr = (ULONG) psgl->address;
#endif
	    pSRB->SGToBeXferLen = (ULONG) psgl->length;
	}
	lval = pSRB->SGToBeXferLen;
	bval = (UCHAR) lval;
	outb(bval,ioport+CtcReg_Low);
	lval = lval >> 8;
	bval = (UCHAR) lval;
	outb(bval,ioport+CtcReg_Mid);
	lval = lval >> 8;
	bval = (UCHAR) lval;
	outb(bval,ioport+CtcReg_High);

	lval = pSRB->SGToBeXferLen;
	outl(lval, ioport+DMA_XferCnt);

	lval = pSRB->SGPhysAddr;
	outl( lval, ioport+DMA_XferAddr);

	bval = DMA_COMMAND+INFO_XFER_CMD;
	outb(bval, ioport+ScsiCmd);

	pSRB->SRBState = SRB_DATA_XFER;

	bval = DMA_IDLE_CMD | ioDir;	/* ;+EN_DMA_INT */
	outb(bval, ioport+DMA_Cmd);

	bval = DMA_START_CMD | ioDir;	/* ;+EN_DMA_INT */
	outb(bval, ioport+DMA_Cmd);
    }
    else    /* xfer pad */
    {
	if( pSRB->SGcount )
	{
	    pSRB->AdaptStatus = H_OVER_UNDER_RUN;
	    pSRB->SRBStatus |= OVER_RUN;
	}
	bval = 0;
	outb(bval,ioport+CtcReg_Low);
	outb(bval,ioport+CtcReg_Mid);
	outb(bval,ioport+CtcReg_High);

	pSRB->SRBState |= SRB_XFERPAD;
	bval = DMA_COMMAND+XFER_PAD_BYTE;
	outb(bval, ioport+ScsiCmd);
/*
	bval = DMA_IDLE_CMD | ioDir;	;+EN_DMA_INT
	outb(bval, ioport+DMA_Cmd);
	bval = DMA_START_CMD | ioDir;	;+EN_DMA_INT
	outb(bval, ioport+DMA_Cmd);
*/
    }
}


static void
DC390_DataOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR  ioDir;

    ioDir = WRITE_DIRECTION;
    DataIO_Comm( pACB, pSRB, ioDir);
}

static void
DC390_DataInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR  ioDir;

    ioDir = READ_DIRECTION;
    DataIO_Comm( pACB, pSRB, ioDir);
}

static void
DC390_CommandPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    PDCB   pDCB;
    UCHAR  bval;
    PUCHAR ptr;
    USHORT ioport, i,  cnt;


    ioport = pACB->IOPortBase;
    bval = RESET_ATN_CMD;
    outb(bval, ioport+ScsiCmd);
    bval = CLEAR_FIFO_CMD;
    outb(bval, ioport+ScsiCmd);
    if( !(pSRB->SRBFlag & AUTO_REQSENSE) )
    {
	cnt = (USHORT) pSRB->ScsiCmdLen;
	ptr = (PUCHAR) pSRB->CmdBlock;
	for(i=0; i < cnt; i++)
	{
	    outb(*ptr, ioport+ScsiFifo);
	    ptr++;
	}
    }
    else
    {
	bval = REQUEST_SENSE;
	outb(bval, ioport+ScsiFifo);
	pDCB = pACB->pActiveDCB;
	bval = pDCB->IdentifyMsg << 5;
	outb(bval, ioport+ScsiFifo);
	bval = 0;
	outb(bval, ioport+ScsiFifo);
	outb(bval, ioport+ScsiFifo);
	bval = sizeof(pSRB->pcmd->sense_buffer);
	outb(bval, ioport+ScsiFifo);
	bval = 0;
	outb(bval, ioport+ScsiFifo);
    }
    pSRB->SRBState = SRB_COMMAND;
    bval = INFO_XFER_CMD;
    outb(bval, ioport+ScsiCmd);
}

static void
DC390_StatusPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR  bval;
    USHORT ioport;

    ioport = pACB->IOPortBase;
    bval = CLEAR_FIFO_CMD;
    outb(bval, ioport+ScsiCmd);
    pSRB->SRBState = SRB_STATUS;
    bval = INITIATOR_CMD_CMPLTE;
    outb(bval, ioport+ScsiCmd);
}

static void
DC390_MsgOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR   bval;
    USHORT  ioport, i, cnt;
    PUCHAR  ptr;
    PDCB    pDCB;

    ioport = pACB->IOPortBase;
    bval = CLEAR_FIFO_CMD;
    outb(bval, ioport+ScsiCmd);
    pDCB = pACB->pActiveDCB;
    if( !(pSRB->SRBState & SRB_MSGOUT) )
    {
	cnt = pSRB->MsgCnt;
	if( cnt )
	{
	    ptr = (PUCHAR) pSRB->MsgOutBuf;
	    for(i=0; i < cnt; i++)
	    {
		outb(*ptr, ioport+ScsiFifo);
		ptr++;
	    }
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
	    outb(bval, ioport+ScsiFifo);
	}
	bval = INFO_XFER_CMD;
	outb( bval, ioport+ScsiCmd);
    }
    else
    {
mop1:
	bval = MSG_EXTENDED;
	outb(bval, ioport+ScsiFifo);
	bval = 3;	/*    ;length of extended msg */
	outb(bval, ioport+ScsiFifo);
	bval = 1;	/*    ; sync nego */
	outb(bval, ioport+ScsiFifo);
	bval = pDCB->NegoPeriod;
	outb(bval, ioport+ScsiFifo);
	bval = SYNC_NEGO_OFFSET;
	outb(bval, ioport+ScsiFifo);
	pSRB->SRBState |= DO_SYNC_NEGO;
	bval = INFO_XFER_CMD;
	outb(bval, ioport+ScsiCmd);
    }
}

static void
DC390_MsgInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
    UCHAR  bval;
    USHORT ioport;

    ioport = pACB->IOPortBase;
    bval = CLEAR_FIFO_CMD;
    outb(bval, ioport+ScsiCmd);
    if( !(pSRB->SRBState & SRB_MSGIN) )
    {
	pSRB->SRBState &= SRB_DISCONNECT;
	pSRB->SRBState |= SRB_MSGIN;
    }
    bval = INFO_XFER_CMD;
    outb(bval, ioport+ScsiCmd);
}

static void
DC390_Nop_0( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
}

static void
DC390_Nop_1( PACB pACB, PSRB pSRB, PUCHAR psstatus)
{
}


static void
SetXferRate( PACB pACB, PDCB pDCB )
{
    UCHAR  bval;
    USHORT cnt, i;
    PDCB   ptr;

    if( !(pDCB->IdentifyMsg & 0x07) )
    {
	if( pACB->scan_devices )
	{
	    CurrSyncOffset = pDCB->SyncOffset;
	}
	else
	{
	    ptr = pACB->pLinkDCB;
	    cnt = pACB->DeviceCnt;
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


static void
DC390_Disconnect( PACB pACB )
{
    PDCB   pDCB;
    PSRB   pSRB, psrb;
    ULONG  flags;
    USHORT ioport, i, cnt;
    UCHAR  bval;

#ifdef DC390_DEBUG0
    printk("DISC,");
#endif

    save_flags(flags);
    cli();
    ioport = pACB->IOPortBase;
    pDCB = pACB->pActiveDCB;
    if (!pDCB)
     {
#ifdef DC390_DEBUG0
	printk("ACB:%08lx->ActiveDCB:%08lx !,",(ULONG)pACB,(ULONG)pDCB);
#endif
	restore_flags(flags); return;
     }
    pSRB = pDCB->pActiveSRB;
    pACB->pActiveDCB = 0;
    pSRB->ScsiPhase = SCSI_NOP0;
    bval = EN_SEL_RESEL;
    outb(bval, ioport+ScsiCmd);
    if( pSRB->SRBState & SRB_UNEXPECT_RESEL )
    {
	pSRB->SRBState = 0;
	DoWaitingSRB( pACB );
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
	DoWaitingSRB( pACB );
    }
    else
    {
	if( (pSRB->SRBState & (SRB_START_+SRB_MSGOUT)) ||
	   !(pSRB->SRBState & (SRB_DISCONNECT+SRB_COMPLETED)) )
	{	/* Selection time out */
	    if( !(pACB->scan_devices) )
	    {
		pSRB->SRBState = SRB_READY;
		RewaitSRB( pDCB, pSRB);
	    }
	    else
	    {
		pSRB->TargetStatus = SCSI_STAT_SEL_TIMEOUT;
		goto  disc1;
	    }
	}
	else if( pSRB->SRBState & SRB_DISCONNECT )
	{
	    DoWaitingSRB( pACB );
	}
	else if( pSRB->SRBState & SRB_COMPLETED )
	{
disc1:
	    if(pDCB->MaxCommand > 1)
	    {
	       bval = pSRB->TagNumber;
	       pDCB->TagMask &= (~(1 << bval));   /* free tag mask */
	    }
	    pDCB->pActiveSRB = 0;
	    pSRB->SRBState = SRB_FREE;
	    SRBdone( pACB, pDCB, pSRB);
	}
    }
    restore_flags(flags);
    return;
}


static void
DC390_Reselect( PACB pACB )
{
    PDCB   pDCB, pdcb;
    PSRB   pSRB;
    USHORT ioport, wval;
    UCHAR  bval, bval1;


#ifdef DC390_DEBUG0
    printk("RSEL,");
#endif
    ioport = pACB->IOPortBase;
    pDCB = pACB->pActiveDCB;
    if( pDCB )
    {	/* Arbitration lost but Reselection win */
	pSRB = pDCB->pActiveSRB;
	if( !( pACB->scan_devices ) )
	{
	    pSRB->SRBState = SRB_READY;
	    RewaitSRB( pDCB, pSRB);
	}
    }
    bval = inb(ioport+ScsiFifo);	/* get ID */
    bval = bval ^ pACB->HostID_Bit;
    wval = 0;
    bval1 = 1;
    for(;;)
    {
	if( !(bval & bval1) )
	{
	    bval1 = bval1 << 1;
	    wval++;
	}
	else
	    break;
    }
    wval |=  ( (USHORT) inb(ioport+ScsiFifo) & 7) << 8;  /* get LUN */
    pDCB = pACB->pLinkDCB;
    pdcb = pDCB;
    while( wval != *((PUSHORT) &pDCB->UnitSCSIID) )
    {
	pDCB = pDCB->pNextDCB;
	if( pDCB == pdcb )
	    return;
    }
    pACB->pActiveDCB = pDCB;
    if( pDCB->SyncMode & EN_TAG_QUEUING )
    {
	pSRB = pACB->pTmpSRB;
	pDCB->pActiveSRB = pSRB;
    }
    else
    {
	pSRB = pDCB->pActiveSRB;
	if( !pSRB || !(pSRB->SRBState & SRB_DISCONNECT) )
	{
	    pSRB= pACB->pTmpSRB;
	    pSRB->SRBState = SRB_UNEXPECT_RESEL;
	    pDCB->pActiveSRB = pSRB;
	    EnableMsgOut( pACB, pSRB );
	}
	else
	{
	    if( pDCB->DCBFlag & ABORT_DEV_ )
	    {
		pSRB->SRBState = SRB_ABORT_SENT;
		EnableMsgOut( pACB, pSRB );
	    }
	    else
		pSRB->SRBState = SRB_DATA_XFER;
	}
    }
    pSRB->ScsiPhase = SCSI_NOP0;
    bval = pDCB->UnitSCSIID;
    outb( bval, ioport+Scsi_Dest_ID);
    bval = pDCB->SyncPeriod;
    outb(bval, ioport+Sync_Period);
    bval = pDCB->SyncOffset;
    outb( bval, ioport+Sync_Offset);
    bval = pDCB->CtrlR1;
    outb(bval, ioport+CtrlReg1);
    bval = pDCB->CtrlR3;
    outb(bval, ioport+CtrlReg3);
    bval = pDCB->CtrlR4;	/* ; Glitch eater */
    outb(bval, ioport+CtrlReg4);
    bval = MSG_ACCEPTED_CMD;	/* ;to rls the /ACK signal */
    outb(bval, ioport+ScsiCmd);
}


static void
SRBdone( PACB pACB, PDCB pDCB, PSRB pSRB )
{
    PSRB   psrb;
    UCHAR  bval, bval1, i, j, status;
    PSCSICMD pcmd;
    PSCSI_INQDATA  ptr;
    USHORT disable_tag;
    ULONG  flags;
    PSGL   ptr2;
    ULONG  swlval;

    pcmd = pSRB->pcmd;
    status = pSRB->TargetStatus;
    if(pSRB->SRBFlag & AUTO_REQSENSE)
    {
	pSRB->SRBFlag &= ~AUTO_REQSENSE;
	pSRB->AdaptStatus = 0;
	pSRB->TargetStatus = SCSI_STAT_CHECKCOND;
	if(status == SCSI_STAT_CHECKCOND)
	{
	    pcmd->result = DID_BAD_TARGET << 16;
	    goto ckc_e;
	}
	if(pSRB->RetryCnt == 0)
	{
	    *((PULONG) &(pSRB->CmdBlock[0])) = pSRB->Segment0[0];
	    pSRB->TotalXferredLen = pSRB->Segment1[1];
	    if( (pSRB->TotalXferredLen) &&
		(pSRB->TotalXferredLen >= pcmd->underflow) )
		pcmd->result |= (DID_OK << 16);
	    else
		pcmd->result = (DRIVER_SENSE << 24) | (DRIVER_OK << 16) |
				SCSI_STAT_CHECKCOND;
#ifdef DC390_DEBUG0
	printk("Cmd=%2x,Result=%8x,XferL=%8x,",pSRB->CmdBlock[0],
		(UINT) pcmd->result, (UINT) pSRB->TotalXferredLen);
#endif
	    goto ckc_e;
	}
	else
	{
	    pSRB->RetryCnt--;
	    pSRB->AdaptStatus = 0;
	    pSRB->TargetStatus = 0;
	    *((PULONG) &(pSRB->CmdBlock[0])) = pSRB->Segment0[0];
	    *((PULONG) &(pSRB->CmdBlock[4])) = pSRB->Segment0[1];
	    if( pSRB->CmdBlock[0] == TEST_UNIT_READY )
	    {
		pcmd->result = (DRIVER_SENSE << 24) | (DRIVER_OK << 16) |
				SCSI_STAT_CHECKCOND;
		goto ckc_e;
	    }
	    pcmd->result |= (DRIVER_SENSE << 24);
	    pSRB->SGcount		 = (UCHAR) pSRB->Segment1[0];
	    pSRB->ScsiCmdLen		 = (UCHAR) (pSRB->Segment1[0] >> 8);
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
	    if( DC390_StartSCSI( pACB, pDCB, pSRB ) )
		RewaitSRB( pDCB, pSRB );
	    return;
	}
    }
    if( status )
    {
	if( status == SCSI_STAT_CHECKCOND)
	{
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
#ifdef	DC390_DEBUG0
		printk("XferredLen=%8x,NotXferLen=%8x,",
			(UINT) pSRB->TotalXferredLen, (UINT) swlval);
#endif
	    }
	    RequestSense( pACB, pDCB, pSRB );
	    return;
	}
	else if( status == SCSI_STAT_QUEUEFULL )
	{
	    bval = (UCHAR) pDCB->GoingSRBCnt;
	    bval--;
	    pDCB->MaxCommand = bval;
	    RewaitSRB( pDCB, pSRB );
	    pSRB->AdaptStatus = 0;
	    pSRB->TargetStatus = 0;
	    return;
	}
	else if(status == SCSI_STAT_SEL_TIMEOUT)
	{
	    pSRB->AdaptStatus = H_SEL_TIMEOUT;
	    pSRB->TargetStatus = 0;
	    pcmd->result = DID_BAD_TARGET << 16;
	}
	else
	{
	    pSRB->AdaptStatus = 0;
	    if( pSRB->RetryCnt )
	    {
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
		if( DC390_StartSCSI( pACB, pDCB, pSRB ) )
		    RewaitSRB( pDCB, pSRB );
		return;
	    }
	    else
	    {
		pcmd->result |= (DID_ERROR << 16) | (ULONG) (pSRB->EndMessage << 8) |
			       (ULONG) status;
	    }
	}
    }
    else
    {
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
	    if(pcmd->result != (DID_OK << 16))
	    {
		if( pcmd->result & SCSI_STAT_CHECKCOND )
		{
		    goto RTN_OK;
		}
		else
		{
		    pACB->DCBmap[pcmd->target] &= ~(1 << pcmd->lun);
		    pPrevDCB->pNextDCB = pACB->pLinkDCB;
		    if( (pcmd->target == pACB->max_id) &&
		       ((pcmd->lun == 0) || (pcmd->lun == pACB->max_lun)) )
		    {
			pACB->scan_devices = 0;
		    }
		}
	    }
	    else
	    {
RTN_OK:
		pPrevDCB->pNextDCB = pDCB;
		pDCB->pNextDCB = pACB->pLinkDCB;
		if( (pcmd->target == pACB->max_id) && (pcmd->lun == pACB->max_lun) )
		    pACB->scan_devices = END_SCAN;
	    }
	}
	else if( pSRB->CmdBlock[0] == INQUIRY )
	{
	    if( (pcmd->target == pACB->max_id) &&
		(pcmd->lun == pACB->max_lun) )
	    {
		pACB->scan_devices = 0;
	    }
	    ptr = (PSCSI_INQDATA) (pcmd->request_buffer);
	    if( pcmd->use_sg )
		ptr = (PSCSI_INQDATA) (((PSGL) ptr)->address);
	    bval1 = ptr->DevType & SCSI_DEVTYPE;
	    if(bval1 == SCSI_NODEV)
	    {
		pACB->DCBmap[pcmd->target] &= ~(1 << pcmd->lun);
		pPrevDCB->pNextDCB = pACB->pLinkDCB;
	    }
	    else
	    {
		pACB->DeviceCnt++;
		pPrevDCB = pDCB;
		pACB->pDCB_free = (PDCB) ((ULONG) (pACB->pDCB_free) + sizeof( DC390_DCB ));
		pDCB->DevType = bval1;
		if(bval1 == TYPE_DISK || bval1 == TYPE_MOD)
		{
		    if( (((ptr->Vers & 0x07) >= 2) || ((ptr->RDF & 0x0F) == 2)) &&
			(ptr->Flags & SCSI_INQ_CMDQUEUE) &&
			(pDCB->DevMode & TAG_QUEUING_) &&
			(pDCB->DevMode & EN_DISCONNECT_) )
		    {
			disable_tag = 0;
			for(i=0; i<BADDEVCNT; i++)
			{
			    for(j=0; j<28; j++)
			    {
				if( ((PUCHAR)ptr)[8+j] != baddevname1[i][j])
				    break;
			    }
			    if(j == 28)
			    {
				disable_tag = 1;
				break;
			    }
			}

			if( !disable_tag )
			{
			    pDCB->MaxCommand = pACB->TagMaxNum;
			    pDCB->SyncMode |= EN_TAG_QUEUING;
			    pDCB->TagMask = 0;
			}
			else
			{
			    pDCB->SyncMode |= EN_ATN_STOP;
			}
		    }
		}
	    }
	}
    }

    save_flags( flags );
    cli();
/*  ReleaseSRB( pDCB, pSRB ); */

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

    DoWaitingSRB( pACB );
    restore_flags(flags);

/*  Notify cmd done */
    pcmd->scsi_done( pcmd );

    if( pDCB->QIORBCnt )
	DoNextCmd( pACB, pDCB );
    return;
}


static void
DoingSRB_Done( PACB pACB )
{
    PDCB  pDCB, pdcb;
    PSRB  psrb, psrb2;
    USHORT  cnt, i;
    PSCSICMD pcmd;

    pDCB = pACB->pLinkDCB;
    pdcb = pDCB;
    do
    {
	cnt = pdcb->GoingSRBCnt;
	psrb = pdcb->pGoingSRB;
	for( i=0; i<cnt; i++)
	{
	    psrb2 = psrb->pNextSRB;
	    pcmd = psrb->pcmd;
	    pcmd->result = DID_RESET << 16;

/*	    ReleaseSRB( pDCB, pSRB ); */

	    psrb->pNextSRB = pACB->pFreeSRB;
	    pACB->pFreeSRB = psrb;

	    pcmd->scsi_done( pcmd );
	    psrb  = psrb2;
	}
	pdcb->GoingSRBCnt = 0;;
	pdcb->pGoingSRB = NULL;
	pdcb->TagMask = 0;
	pdcb = pdcb->pNextDCB;
    }
    while( pdcb != pDCB );
}


static void
DC390_ResetSCSIBus( PACB pACB )
{
    USHORT ioport;
    UCHAR  bval;
    ULONG  flags;

    save_flags(flags);
    cli();
    pACB->ACBFlag |= RESET_DEV;
    ioport = pACB->IOPortBase;

    bval = DMA_IDLE_CMD;
    outb(bval,ioport+DMA_Cmd);

    bval = RST_SCSI_BUS_CMD;
    outb(bval,ioport+ScsiCmd);

    restore_flags(flags);
    return;
}


static void
DC390_ScsiRstDetect( PACB pACB )
{
    ULONG wlval, flags;
    USHORT ioport;
    UCHAR  bval;

#ifdef DC390_DEBUG0
    printk("RST_DETEC");
#endif
    save_flags(flags);
    sti();
    wlval = jiffies + HZ;
    while( jiffies < wlval );	 /* delay 1 sec */

    cli();
    ioport = pACB->IOPortBase;
    bval = DMA_IDLE_CMD;
    outb(bval,ioport+DMA_Cmd);
    bval = CLEAR_FIFO_CMD;
    outb(bval,ioport+ScsiCmd);

    if( pACB->ACBFlag & RESET_DEV )
	pACB->ACBFlag |= RESET_DONE;
    else
    {
	pACB->ACBFlag |= RESET_DETECT;

	ResetDevParam( pACB );
/*	DoingSRB_Done( pACB ); ???? */
	RecoverSRB( pACB );
	pACB->pActiveDCB = NULL;
	pACB->ACBFlag = 0;
	DoWaitingSRB( pACB );
    }
    restore_flags(flags);
    return;
}


static void
RequestSense( PACB pACB, PDCB pDCB, PSRB pSRB )
{
    PSCSICMD  pcmd;

    pSRB->SRBFlag |= AUTO_REQSENSE;
    pSRB->Segment0[0] = *((PULONG) &(pSRB->CmdBlock[0]));
    pSRB->Segment0[1] = *((PULONG) &(pSRB->CmdBlock[4]));
    pSRB->Segment1[0] = (ULONG) ((pSRB->ScsiCmdLen << 8) + pSRB->SGcount);
    pSRB->Segment1[1] = pSRB->TotalXferredLen;
    pSRB->AdaptStatus = 0;
    pSRB->TargetStatus = 0;

    pcmd = pSRB->pcmd;

    pSRB->Segmentx.address = (PUCHAR) &(pcmd->sense_buffer);
    pSRB->Segmentx.length = sizeof(pcmd->sense_buffer);
    pSRB->pSegmentList = &pSRB->Segmentx;
    pSRB->SGcount = 1;
    pSRB->SGIndex = 0;

    *((PULONG) &(pSRB->CmdBlock[0])) = 0x00000003;
    pSRB->CmdBlock[1] = pDCB->IdentifyMsg << 5;
    *((PUSHORT) &(pSRB->CmdBlock[4])) = sizeof(pcmd->sense_buffer);
    pSRB->ScsiCmdLen = 6;

    pSRB->TotalXferredLen = 0;
    pSRB->SGToBeXferLen = 0;
    if( DC390_StartSCSI( pACB, pDCB, pSRB ) )
	RewaitSRB( pDCB, pSRB );
}


static void
EnableMsgOut2( PACB pACB, PSRB pSRB )
{
    USHORT ioport;
    UCHAR  bval;

    ioport = pACB->IOPortBase;
    pSRB->MsgCnt = 1;
    bval = SET_ATN_CMD;
    outb(bval, ioport+ScsiCmd);
}


static void
EnableMsgOut( PACB pACB, PSRB pSRB )
{
    pSRB->MsgOutBuf[0] = MSG_ABORT;
    EnableMsgOut2( pACB, pSRB );
}


static void
DC390_InvalidCmd( PACB pACB )
{
   UCHAR bval;
   USHORT ioport;
   PSRB   pSRB;

    pSRB = pACB->pActiveDCB->pActiveSRB;
    if( pSRB->SRBState & (SRB_START_+SRB_MSGOUT) )
    {
	ioport = pACB->IOPortBase;
	bval = CLEAR_FIFO_CMD;
	outb(bval,(ioport+ScsiCmd));
    }
}

