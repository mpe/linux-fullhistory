/* $Id: aha1740.c,v 1.1 1992/07/24 06:27:38 root Exp root $
 *  linux/kernel/aha1740.c
 *
 *  Based loosely on aha1542.c which is
 *  Copyright (C) 1992  Tommy Thorn
 *  and
 *  Modified by Eric Youngdale
 *
 *  This file is aha1740.c, written and
 *  Copyright (C) 1992  Brad McLean
 *  
 * aha1740_makecode needs more work
 */

#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/types.h>
#include <linux/string.h>

#include <linux/sched.h>
#include <asm/dma.h>

#include <asm/system.h>
#include <asm/io.h>
#include "../blk.h"
#include "scsi.h"
#include "hosts.h"

#include "aha1740.h"
/* #define DEBUG */
#ifdef DEBUG
#define DEB(x) x
#else
#define DEB(x)
#endif
/*
static const char RCSid[] = "$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/aha1740.c,v 1.1 1992/07/24 06:27:38 root Exp root $";
*/

static unsigned int slot, base;
static unsigned char irq_level;

static struct ecb ecb[AHA1740_ECBS];	/* One for each queued operation */

static int aha1740_last_ecb_used  = 0;	/* optimization */

int aha1740_makecode(unchar *sense, unchar *status)
{
    struct statusword {
	ushort	don:1,	/* Command Done - No Error */
		du:1,	/* Data underrun */
	:1,	qf:1,	/* Queue full */
		sc:1,	/* Specification Check */
		dor:1,	/* Data overrun */
		ch:1,	/* Chaining Halted */
		intr:1,	/* Interrupt issued */
		asa:1,	/* Additional Status Available */
		sns:1,	/* Sense information Stored */
	:1,	ini:1,	/* Initialization Required */
		me:1,	/* Major error or exception */
	:1,	eca:1, :1;  /* Extended Contingent alliance */
	} status_word;
    int retval = DID_OK;

    status_word = * (struct statusword *) status;
#ifdef DEBUG
printk("makecode from %x,%x,%x,%x %x,%x,%x,%x",status[0],status[1],status[2],status[3],
sense[0],sense[1],sense[2],sense[3]);
#endif
    if ( status_word.don )
	return 0;
/*
    if ( status_word.du && status[2] != 0x11 )
	return 0;
*/
    if ( status[2] == 0x11 )
		retval = DID_TIME_OUT;
    else
		retval = DID_ERROR;    
    return status[3] | retval << 16; /* OKAY, SO I'M LAZY! I'll fix it later... */
}

int aha1740_test_port()
{
    char    name[4],tmp;

    /* Okay, look for the EISA ID's */
    name[0]= 'A' -1 + ((tmp = inb(HID0)) >> 2); /* First character */
    name[1]= 'A' -1 + ((tmp & 3) << 3);
    name[1]+= ((tmp = inb(HID1)) >> 5)&0x7;	/* Second Character */
    name[2]= 'A' -1 + (tmp & 0x1f);		/* Third Character */
    name[3]=0;
    tmp = inb(HID2);
    if ( strcmp ( name, HID_MFG ) || inb(HID2) != HID_PRD )
	return 0;   /* Not an Adaptec 174x */

/*    if ( inb(HID3) < HID_REV ) */
    if ( inb(HID3) != HID_REV )
	printk("aha1740: Warning; board revision of %d; expected %d\n",
	    inb(HID3),HID_REV);

    if ( inb(EBCNTRL) != EBCNTRL_VALUE )
    {
	printk("aha1740: Board detected, but EBCNTRL = %x, so disabled it.\n",
	    inb(EBCNTRL));
	return 0;
    }

    if ( inb(PORTADR) & PORTADDR_ENH )
	return 1;   /* Okay, we're all set */
    printk("aha1740: Board detected, but not in enhanced mode, so disabled it.\n");
    return 0;
}

/* What's this little function for? */
const char *aha1740_info(void)
{
    static char buffer[] = "";			/* looks nicer without anything here */
    return buffer;
}

/* A "high" level interrupt handler */
void aha1740_intr_handle(int foo)
{
    void (*my_done)(Scsi_Cmnd *);
    int errstatus, adapstat;
    int number_serviced;
    struct ecb *ecbptr;
    Scsi_Cmnd *SCtmp;

    number_serviced = 0;

    while(inb(G2STAT) & G2STAT_INTPEND){
	DEB(printk("aha1740_intr top of loop.\n"));
	adapstat = inb(G2INTST);
	outb(G2CNTRL_IRST,G2CNTRL); /* interrupt reset */
      
        switch ( adapstat & G2INTST_MASK )
	{
	case	G2INTST_CCBRETRY:
	    printk("aha1740 complete with retry!\n");
	case	G2INTST_CCBERROR:
	case	G2INTST_CCBGOOD:
	    ecbptr = (void *) ( ((ulong) inb(MBOXIN0)) +
				((ulong) inb(MBOXIN1) <<8) +
				((ulong) inb(MBOXIN2) <<16) +
				((ulong) inb(MBOXIN3) <<24) );
	    outb(G2CNTRL_HRDY,G2CNTRL); /* Host Ready -> Mailbox in complete */
	    SCtmp = ecbptr->SCpnt;
	    if (SCtmp->host_scribble)
		scsi_free(SCtmp->host_scribble, 512);
	  /* Fetch the sense data, and tuck it away, in the required slot.  The
	     Adaptec automatically fetches it, and there is no guarantee that
	     we will still have it in the cdb when we come back */
	    if ( (adapstat & G2INTST_MASK) == G2INTST_CCBERROR )
	      {
		memcpy(SCtmp->sense_buffer, ecbptr->sense, 
		       sizeof(SCtmp->sense_buffer));
		errstatus = aha1740_makecode(ecbptr->sense,ecbptr->status);
	      }
	    else
		errstatus = 0;
	    DEB(if (errstatus) printk("aha1740_intr_handle: returning %6x\n", errstatus));
	    SCtmp->result = errstatus;
	    my_done = ecbptr->done;
	    memset(ecbptr,0,sizeof(struct ecb)); 
	    if ( my_done )
		my_done(SCtmp);
	    break;
	case	G2INTST_HARDFAIL:
	    printk("aha1740 hardware failure!\n");
	    panic("aha1740.c");	/* Goodbye */
	case	G2INTST_ASNEVENT:
	    printk("aha1740 asynchronous event: %02x %02x %02x %02x %02x\n",adapstat,
		inb(MBOXIN0),inb(MBOXIN1),inb(MBOXIN2),inb(MBOXIN3)); /* Say What? */
	    outb(G2CNTRL_HRDY,G2CNTRL); /* Host Ready -> Mailbox in complete */
	    break;
	case	G2INTST_CMDGOOD:
	    /* set immediate command success flag here: */
	    break;
	case	G2INTST_CMDERROR:
	    /* Set immediate command failure flag here: */
	    break;
	}
      number_serviced++;
    };
}

int aha1740_queuecommand(Scsi_Cmnd * SCpnt, void (*done)(Scsi_Cmnd *))
{
    unchar direction;
    unchar *cmd = (unchar *) SCpnt->cmnd;
    unchar target = SCpnt->target;
    void *buff = SCpnt->request_buffer;
    int bufflen = SCpnt->request_bufflen;
    int ecbno;
    DEB(int i);

    DEB(if (target > 0 || SCpnt->lun > 0) {
      SCpnt->result = DID_TIME_OUT << 16;
      done(SCpnt); return 0;});
    
    if(*cmd == REQUEST_SENSE){
#ifndef DEBUG
      if (bufflen != 16) {
	printk("Wrong buffer length supplied for request sense (%d)\n",bufflen);
	panic("aha1740.c");
      };
#endif
      SCpnt->result = 0;
      done(SCpnt); 
      return 0;
    };

#ifdef DEBUG
    if (*cmd == READ_10 || *cmd == WRITE_10)
      i = xscsi2int(cmd+2);
    else if (*cmd == READ_6 || *cmd == WRITE_6)
      i = scsi2int(cmd+2);
    else
      i = -1;
    if (done)
      printk("aha1740_queuecommand: dev %d cmd %02x pos %d len %d ", target, *cmd, i, bufflen);
    else
      printk("aha1740_command: dev %d cmd %02x pos %d len %d ", target, *cmd, i, bufflen);
    printk("scsi cmd:");
    for (i = 0; i < (*cmd<=0x1f?6:10); i++) printk("%02x ", cmd[i]);
    printk("\n");
#ifdef 0
    if (*cmd == WRITE_10 || *cmd == WRITE_6)
      return 0; /* we are still testing, so *don't* write */
#endif
#endif

/* locate an available ecb */

    cli();
    ecbno = aha1740_last_ecb_used + 1;
    if (ecbno >= AHA1740_ECBS) ecbno = 0;

    do{
      if( ! ecb[ecbno].cmdw )
	break;
      ecbno++;
      if (ecbno >= AHA1740_ECBS ) ecbno = 0;
    } while (ecbno != aha1740_last_ecb_used);

    if( ecb[ecbno].cmdw )
      panic("Unable to find empty ecb for aha1740.\n");

    ecb[ecbno].cmdw = AHA1740CMD_INIT;	/* SCSI Initiator Command to reserve*/

    aha1740_last_ecb_used = ecbno;    
    sti();

#ifdef DEBUG
    printk("Sending command (%d %x)...",ecbno, done);
#endif

    ecb[ecbno].cdblen = (*cmd<=0x1f)?6:10;	/* SCSI Command Descriptor Block Length */

    direction = 0;
    if (*cmd == READ_10 || *cmd == READ_6)
	direction = 1;
    else if (*cmd == WRITE_10 || *cmd == WRITE_6)
	direction = 0;

    memcpy(ecb[ecbno].cdb, cmd, ecb[ecbno].cdblen);

    if (SCpnt->use_sg) {
      struct scatterlist * sgpnt;
      struct aha1740_chain * cptr;
#ifdef DEBUG
      unsigned char * ptr;
#endif
      int i;
      ecb[ecbno].sg = 1;	  /* SCSI Initiator Command  w/scatter-gather*/
      SCpnt->host_scribble = scsi_malloc(512);
      sgpnt = (struct scatterlist *) SCpnt->request_buffer;
      cptr = (struct aha1740_chain *) SCpnt->host_scribble; 
      if (cptr == NULL) panic("aha1740.c: unable to allocate DMA memory\n");
      for(i=0; i<SCpnt->use_sg; i++) {
	cptr[i].dataptr = (long) sgpnt[i].address;
	cptr[i].datalen = sgpnt[i].length;
      };
      ecb[ecbno].datalen = SCpnt->use_sg * sizeof(struct aha1740_chain);
      ecb[ecbno].dataptr = (long) cptr;
#ifdef DEBUG
      printk("cptr %x: ",cptr);
      ptr = (unsigned char *) cptr;
      for(i=0;i<24;i++) printk("%02x ", ptr[i]);
#endif
    } else {
      SCpnt->host_scribble = NULL;
      ecb[ecbno].datalen = bufflen;
      ecb[ecbno].dataptr = (long) buff;
    };
    ecb[ecbno].lun = SCpnt->lun;
    ecb[ecbno].ses = 1;	/* Suppress underrun errors */
/*    ecb[ecbno].dat=1;	*/ /* Yes, check the data direction */
    ecb[ecbno].dir= direction;
    ecb[ecbno].ars=1;  /* Yes, get the sense on an error */
    ecb[ecbno].senselen = 12;	/* Why 12? Eric? MAXSENSE? */
    ecb[ecbno].senseptr = (long) ecb[ecbno].sense;
    ecb[ecbno].statusptr = (long) ecb[ecbno].status;
    ecb[ecbno].done = done;
    ecb[ecbno].SCpnt = SCpnt;
#ifdef DEBUG
    { int i;
    printk("aha1740_command: sending.. ");
    for (i = 0; i < sizeof(ecb[ecbno])-10; i++)
      printk("%02x ", ((unchar *)&ecb[ecbno])[i]);
    };
    printk("\n");
#endif
    if (done) { ulong adrs;

	if ( ! (inb(G2STAT) & G2STAT_MBXOUT) )  /* Spec claim's it's so fast */
	  printk("aha1740_mbxout wait!\n");	/* that this is okay? It seems */
	while ( ! (inb(G2STAT) & G2STAT_MBXOUT) ); /* to work, so I'll leave it */
	adrs = (ulong) &(ecb[ecbno]);
	outb((char) (adrs&0xff), MBOXOUT0);
	outb((char) ((adrs>>8)&0xff), MBOXOUT1);
	outb((char) ((adrs>>16)&0xff), MBOXOUT2);
	outb((char) ((adrs>>24)&0xff), MBOXOUT3);
	if ( inb(G2STAT) & G2STAT_BUSY )	/* Again, allegedly fast */
	  printk("aha1740_attn wait!\n");
	while ( inb(G2STAT) & G2STAT_BUSY );
	outb(ATTN_START | (target & 7), ATTN);	/* Start it up */
    }
    else
      printk("aha1740_queuecommand: done can't be NULL\n");
    
    return 0;
}

static volatile int internal_done_flag = 0;
static volatile int internal_done_errcode = 0;
static void internal_done(Scsi_Cmnd * SCpnt)
{
    internal_done_errcode = SCpnt->result;
    ++internal_done_flag;
}

int aha1740_command(Scsi_Cmnd * SCpnt)
{
    aha1740_queuecommand(SCpnt, internal_done);

    while (!internal_done_flag);
    internal_done_flag = 0;
    return internal_done_errcode;
}

/* Query the board for it's port addresses, etc.  Actually, the irq_level is
    all we care about for this board, since it's EISA */

static int aha1740_getconfig()
{
  int iop, bios, scsi, dma;
  static int iotab[] = { 0, 0, 0x130, 0x134, 0x230, 0x234, 0x330, 0x334 };
  static int intab[] = { 9,10,11,12,0,14,15,0 };
  static int dmatab[] = { 0,5,6,7 };

  iop = iotab [ inb(PORTADR)&0x7 ];
  bios = inb(BIOSADR);
  irq_level = intab [ inb(INTDEF)&0x7 ];
  scsi = inb(SCSIDEF);
  dma = dmatab[ (inb(BUSDEF)>>2) & 0x3 ];
  return 0;
}

int aha1740_detect(int hostnum)
{
    memset(ecb,0,sizeof(ecb));
    DEB(printk("aha1740_detect: \n"));
    
    for ( slot=MINEISA; slot <= MAXEISA; slot++ )
    {
	base = SLOTBASE(slot);
	if ( aha1740_test_port(base))  break;
    }
    if ( slot > MAXEISA )
	return 0;

    if (aha1740_getconfig() == -1)
	return 0;

    if ( (inb(G2STAT) & (G2STAT_MBXOUT | G2STAT_BUSY) ) != G2STAT_MBXOUT )
    {
        outb(G2CNTRL_HRST,G2CNTRL);
	/* 10 Msec Delay Here */
        outb(0,G2CNTRL);    
    }

    printk("Configuring Adaptec at IO:%x, IRQ %d\n",base,
	   irq_level);

    DEB(printk("aha1740_detect: enable interrupt channel %d\n", irq_level));

    if (request_irq(irq_level,aha1740_intr_handle)) {
      printk("Unable to allocate IRQ for adaptec controller.\n");
      return 0;
    };
    return 1;
}

int aha1740_abort(Scsi_Cmnd * SCpnt, int i)
{
    DEB(printk("aha1740_abort\n"));
    return 0;
}

int aha1740_reset(void)
{
    DEB(printk("aha1740_reset called\n"));
    return 0;
}

int aha1740_biosparam(int size, int dev, int* info){
DEB(printk("aha1740_biosparam\n"));
  info[0] = 32;
  info[1] = 64;
  info[2] = (size + 2047) >> 11;
  if (info[2] >= 1024) info[2] = 1024;
  return 0;
}


