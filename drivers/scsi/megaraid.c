/*===================================================================
 *
 *                    Linux MegaRAID device driver
 * 
 * Copyright 1998 American Megatrends Inc.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Version : 0.92
 * 
 * Description: Linux device driver for AMI MegaRAID controller
 *
 * History:
 *
 * Version 0.90:
 *     Works and has been tested with the MegaRAID 428 controller, and
 *     the MegaRAID 438 controller.  Probably works with the 466 also,
 *     but not tested.
 *
 * Version 0.91:
 *     Aligned mailbox area on 16-byte boundry.
 *     Added schedule() at the end to properly clean up.
 *     Made improvements for conformity to linux driver standards.
 *
 * Version 0.92:
 *     Added support for 2.1 kernels.
 *         Reads from pci_dev struct, so it's not dependent on pcibios.
 *         Added some missing virt_to_bus() translations.
 *     Added support for SMP.
 *         Changed global cli()'s to spinlocks for 2.1, and simulated
 *          spinlocks for 2.0.
 *     Removed setting of SA_INTERRUPT flag when requesting Irq.
 *
 * Version 0.92ac:
 *	Small changes to the comments/formatting. Plus a couple of
 *	added notes. Returned to the authors. No actual code changes
 *	save printk levels.
 *	8 Oct 98	Alan Cox <alan.cox@linux.org>
 *
 *     Merged with 2.1.131 source tree.
 *     12 Dec 98       K. Baranowski <kgb@knm.org.pl>
 *
 * BUGS:
 *     Tested with 2.1.90, but unfortunately there is a bug in pci.c which
 *     fails to detect our controller.  Does work with 2.1.118--don't know
 *     which kernel in between it was fixed in.
 *     With SMP enabled under 2.1.118 with more than one processor, gets an
 *     error message "scsi_end_request: buffer-list destroyed" under heavy
 *     IO, but doesn't seem to affect operation, or data integrity.  The
 *     message doesn't occur without SMP enabled, or with one proccessor with
 *     SMP enabled, or under any combination under 2.0 kernels.
 *
 *===================================================================*/
#define QISR 1

#define CRLFSTR "\n"

#define MULTIQ 1

#include <linux/version.h>

#ifdef MODULE
#include <linux/module.h>

#if LINUX_VERSION_CODE >= 0x20100
char kernel_version[] = UTS_RELEASE;

/* originally ported by Dell Corporation; updated, released, and maintained by
   American Megatrends */
MODULE_AUTHOR("American Megatrends Inc."); 
MODULE_DESCRIPTION("AMI MegaRAID driver");    
#endif
#endif

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/wait.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>

#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/malloc.h>	/* for kmalloc() */
#if LINUX_VERSION_CODE < 0x20100
#include <linux/bios32.h>
#else
#include <asm/spinlock.h>
#endif

#include <asm/io.h>
#include <asm/irq.h>

#include "sd.h"
#include "scsi.h"
#include "hosts.h"

#include "megaraid.h"

/*================================================================
 *
 *                          #Defines
 *
 *================================================================*/

#if LINUX_VERSION_CODE < 0x020100
#define ioremap vremap
#define iounmap vfree

/* simulate spin locks */
typedef struct {volatile char lock;} spinlock_t;
#define spin_lock_init(x) { (x)->lock = 0;}
#define spin_lock_irqsave(x,flags) { while ((x)->lock) barrier();\
                                        (x)->lock=1; save_flags(flags);\
                                        cli();}
#define spin_unlock_irqrestore(x,flags) { (x)->lock=0; restore_flags(flags);}

#endif

#if LINUX_VERSION_CODE >= 0x020100
#define queue_task_irq(a,b)     queue_task(a,b)
#define queue_task_irq_off(a,b) queue_task(a,b)
#endif

#define MAX_SERBUF 160
#define COM_BASE 0x2f8

#define ENQUEUE(obj,type,list,next) \
{ type **node; long cpuflag; \
  spin_lock_irqsave(&mega_lock,cpuflag);\
  for(node=&(list); *node; node=(type **)&(*node)->##next); \
  (*node) = obj; \
  (*node)->##next = NULL; \
  spin_unlock_irqrestore(&mega_lock,cpuflag);\
};

#define DEQUEUE(obj,type,list,next) \
{ long cpuflag; \
  spin_lock_irqsave(&mega_lock,cpuflag);\
  if ((obj=list) != NULL) {\
    list = (type *)(list)->##next; \
  } \
  spin_unlock_irqrestore(&mega_lock,cpuflag);\
};

u_long RDINDOOR(mega_host_config *megaCfg)
{
  return readl(megaCfg->base + 0x20);
}

void WRINDOOR(mega_host_config *megaCfg, u_long value)
{
  writel(value,megaCfg->base+0x20);
}

u_long RDOUTDOOR(mega_host_config *megaCfg)
{
  return readl(megaCfg->base+0x2C);
}

void WROUTDOOR(mega_host_config *megaCfg, u_long value)
{
  writel(value,megaCfg->base+0x2C);
}

/*================================================================
 *
 *                    Function prototypes
 *
 *================================================================*/
static int  MegaIssueCmd(mega_host_config *megaCfg,
			 u_char *mboxData,
			 mega_scb *scb,
			 int intr);
static int  build_sglist(mega_host_config *megaCfg, mega_scb *scb, 
			 u_long *buffer, u_long *length);

static void mega_runque(void *);
static void mega_rundoneq(void);
static void mega_cmd_done(mega_host_config *,mega_scb *, int);

/* set SERDEBUG to 1 to enable serial debugging */
#define SERDEBUG 0
#if SERDEBUG
static void ser_init(void);
static void ser_puts(char *str);
static void ser_putc(char c);
static int  ser_printk(const char *fmt, ...);
#endif

/*================================================================
 *
 *                    Global variables
 *
 *================================================================*/
static int               numCtlrs = 0;
static mega_host_config *megaCtlrs[4] = { 0 };

/* Change this to 0 if you want to see the raw drives */
static int use_raid   = 1;

/* Queue of pending/completed SCBs */
static mega_scb  *qPending   = NULL;
static Scsi_Cmnd *qCompleted = NULL;

volatile static spinlock_t mega_lock;
static struct tq_struct runq = {0,0,mega_runque,NULL};

struct proc_dir_entry proc_scsi_megaraid = {
  PROC_SCSI_MEGARAID, 8, "megaraid",
  S_IFDIR | S_IRUGO | S_IXUGO, 2
};

#if SERDEBUG
static char strbuf[MAX_SERBUF+1];

static void ser_init()
{
    unsigned port=COM_BASE;

    outb(0x80,port+3);
    outb(0,port+1);
    /* 9600 Baud, if 19200: outb(6,port) */
    outb(12, port);
    outb(3,port+3);
    outb(0,port+1);
}

static void ser_puts(char *str)
{
    char *ptr;

    ser_init();
    for (ptr=str;*ptr;++ptr)
        ser_putc(*ptr);
}

static void ser_putc(char c)
{
    unsigned port=COM_BASE;

    while ((inb(port+5) & 0x20)==0);
    outb(c,port);
    if (c==0x0a)
    {
        while ((inb(port+5) & 0x20)==0);
        outb(0x0d,port);
    }
}

static int ser_printk(const char *fmt, ...)
{
    va_list args;
    int i;
    long flags;

    spin_lock_irqsave(mega_lock,flags);
    va_start(args,fmt);
    i = vsprintf(strbuf,fmt,args);
    ser_puts(strbuf);
    va_end(args);
    spin_unlock_irqrestore(&mega_lock,flags);

    return i;
}

#define TRACE(a)    { ser_printk a;}

#else
#define TRACE(A)
#endif

void callDone(Scsi_Cmnd *SCpnt)
{
  if (SCpnt->result) {
    TRACE(("*** %.08lx %.02x <%d.%d.%d> = %x\n", SCpnt->serial_number, 
	   SCpnt->cmnd[0], SCpnt->channel, SCpnt->target, SCpnt->lun, 
	   SCpnt->result));
  }
  SCpnt->scsi_done(SCpnt);
}

/*-------------------------------------------------------------------------
 *
 *                      Local functions
 *
 *-------------------------------------------------------------------------*/

/*================================================
 * Initialize SCB structures
 *================================================*/
static void initSCB(mega_host_config *megaCfg)
{
  int idx;

  for(idx=0; idx<megaCfg->max_cmds; idx++) {
    megaCfg->scbList[idx].idx    = -1;
    megaCfg->scbList[idx].flag   = 0;
    megaCfg->scbList[idx].sgList = NULL;
    megaCfg->scbList[idx].SCpnt  = NULL;
  }
}

/*===========================
 * Allocate a SCB structure
 *===========================*/
static mega_scb *allocateSCB(mega_host_config *megaCfg,Scsi_Cmnd *SCpnt)
{
  int        idx;
  long       flags;

  spin_lock_irqsave(&mega_lock,flags);
  for(idx=0; idx<megaCfg->max_cmds; idx++) {
    if (megaCfg->scbList[idx].idx < 0) {

      /* Set Index and SCB pointer */ 
      megaCfg->scbList[idx].flag  = 0;
      megaCfg->scbList[idx].idx   = idx;
      megaCfg->scbList[idx].SCpnt = SCpnt;
      megaCfg->scbList[idx].next  = NULL;
      spin_unlock_irqrestore(&mega_lock,flags);

      if (megaCfg->scbList[idx].sgList == NULL) {
	megaCfg->scbList[idx].sgList =
                  kmalloc(sizeof(mega_sglist)*MAX_SGLIST,GFP_ATOMIC|GFP_DMA);
      }

      return &megaCfg->scbList[idx];
    }
  }
  spin_unlock_irqrestore(&mega_lock,flags);

  printk(KERN_WARNING "Megaraid: Could not allocate free SCB!!!\n");
  
  return NULL;
}

/*=======================
 * Free a SCB structure
 *=======================*/
static void freeSCB(mega_scb *scb)
{
  long flags;

  spin_lock_irqsave(&mega_lock,flags);
  scb->flag  = 0;
  scb->idx   = -1;
  scb->next  = NULL;
  scb->SCpnt = NULL;
  spin_unlock_irqrestore(&mega_lock,flags);
}

/* Run through the list of completed requests */
static void mega_rundoneq()
{
  mega_host_config *megaCfg;
  Scsi_Cmnd        *SCpnt;
  long              islogical;

  while(1) {
    DEQUEUE(SCpnt, Scsi_Cmnd, qCompleted, host_scribble);
    if (SCpnt == NULL) return;

    megaCfg = (mega_host_config *)SCpnt->host->hostdata;

    /* Check if we're allowing access to RAID drives or physical
     *  if use_raid == 1 and this wasn't a disk on the max channel or
     *  if use_raid == 0 and this was a disk on the max channel
     *  then fail.
     */
    islogical = (SCpnt->channel == megaCfg->host->max_channel) ? 1 : 0;
    if (SCpnt->cmnd[0] == INQUIRY &&
	((((u_char*)SCpnt->request_buffer)[0] & 0x1F) == TYPE_DISK) &&
	(islogical != use_raid)) {
       SCpnt->result = 0xF0;
    }

    /* Convert result to error */
    switch(SCpnt->result) {
    case 0x00: case 0x02:
      SCpnt->result |= (DID_OK << 16);
      break;
    case 0x8:
      SCpnt->result |= (DID_BUS_BUSY << 16);
      break;
    default:
      SCpnt->result |= (DID_BAD_TARGET << 16);
      break;
    }

    /* Callback */
    callDone(SCpnt);
  }
}

/* Add command to the list of completed requests */
static void mega_cmd_done(mega_host_config *megaCfg,mega_scb *pScb, int status)
{
  pScb->SCpnt->result = status;
  ENQUEUE(pScb->SCpnt, Scsi_Cmnd, qCompleted, host_scribble);
  freeSCB(pScb);
}

/*----------------------------------------------------
 * Process pending queue list
 *
 * Run as a scheduled task 
 *----------------------------------------------------*/
static void mega_runque(void *dummy)
{
  mega_host_config *megaCfg;
  mega_scb         *pScb;
  long              flags;

  /* Take care of any completed requests */
  mega_rundoneq();

  DEQUEUE(pScb,mega_scb,qPending,next);

  if (pScb) {
    megaCfg = (mega_host_config *)pScb->SCpnt->host->hostdata;

    if (megaCfg->mbox->busy || megaCfg->flag & (IN_ISR|PENDING)) {
      TRACE(("%.08lx %.02x <%d.%d.%d> intr%d busy%d isr%d pending%d\n",
	     pScb->SCpnt->serial_number,
	     pScb->SCpnt->cmnd[0],
	     pScb->SCpnt->channel,
	     pScb->SCpnt->target,
	     pScb->SCpnt->lun,
	     intr_count,
	     megaCfg->mbox->busy,
	     (megaCfg->flag & IN_ISR)  ? 1 : 0,
	     (megaCfg->flag & PENDING) ? 1 : 0));
    }

    if (MegaIssueCmd(megaCfg, pScb->mboxData, pScb, 1)) {
      /* We're BUSY... come back later */
      spin_lock_irqsave(&mega_lock,flags);
      pScb->next = qPending;
      qPending   = pScb;
      spin_unlock_irqrestore(&mega_lock,flags);

      if (!(megaCfg->flag & PENDING)) { /* If PENDING, irq will schedule task */
          queue_task(&runq, &tq_scheduler);
      }
    }
  }
}

/*-------------------------------------------------------------------
 *
 *                 Build a SCB from a Scsi_Cmnd
 *
 * Returns a SCB pointer, or NULL
 * If NULL is returned, the scsi_done function MUST have been called
 *
 *-------------------------------------------------------------------*/
static mega_scb *mega_build_cmd(mega_host_config *megaCfg, Scsi_Cmnd *SCpnt)
{
  mega_scb      *pScb;
  mega_mailbox  *mbox;
  mega_passthru *pthru;
  long           seg;

  /* We don't support multi-luns */
  if (SCpnt->lun != 0) {
    SCpnt->result = (DID_BAD_TARGET << 16);
    callDone(SCpnt);
    return NULL;
  }

  /*-----------------------------------------------------
   *
   *               Logical drive commands
   *
   *-----------------------------------------------------*/
  if (SCpnt->channel == megaCfg->host->max_channel) {
    switch(SCpnt->cmnd[0]) {
    case TEST_UNIT_READY:
      memset(SCpnt->request_buffer, 0, SCpnt->request_bufflen);
      SCpnt->result = (DID_OK << 16);
      callDone(SCpnt);
      return NULL;

    case MODE_SENSE:
      memset(SCpnt->request_buffer, 0, SCpnt->cmnd[4]);
      SCpnt->result = (DID_OK << 16);
      callDone(SCpnt);
      return NULL;

    case READ_CAPACITY:
    case INQUIRY:
      /* Allocate a SCB and initialize passthru */
      if ((pScb = allocateSCB(megaCfg,SCpnt)) == NULL) {
	SCpnt->result = (DID_ERROR << 16);
	callDone(SCpnt);
	return NULL;
      }
      pthru = &pScb->pthru;
      mbox  = (mega_mailbox *)&pScb->mboxData;

      memset(mbox,  0, sizeof(pScb->mboxData));
      memset(pthru, 0, sizeof(mega_passthru));
      pthru->timeout      = 0;
      pthru->ars          = 0;
      pthru->islogical    = 1;
      pthru->logdrv       = SCpnt->target;
      pthru->cdblen       = SCpnt->cmd_len;
      pthru->dataxferaddr = virt_to_bus(SCpnt->request_buffer);
      pthru->dataxferlen  = SCpnt->request_bufflen;
      memcpy(pthru->cdb, SCpnt->cmnd, SCpnt->cmd_len);

      /* Initialize mailbox area */
      mbox->cmd      = MEGA_MBOXCMD_PASSTHRU;
      mbox->xferaddr = virt_to_bus(pthru);

      return pScb;

    case READ_6:
    case WRITE_6:
    case READ_10:
    case WRITE_10:
      /* Allocate a SCB and initialize mailbox */
      if ((pScb = allocateSCB(megaCfg,SCpnt)) == NULL) {
	SCpnt->result = (DID_ERROR << 16);
	callDone(SCpnt);
	return NULL;
      }
      mbox = (mega_mailbox *)&pScb->mboxData;

      memset(mbox, 0, sizeof(pScb->mboxData));
      mbox->logdrv = SCpnt->target;
      mbox->cmd    = (*SCpnt->cmnd == READ_6 || *SCpnt->cmnd == READ_10) ?
	MEGA_MBOXCMD_LREAD : MEGA_MBOXCMD_LWRITE;
      
      /* 6-byte */
      if (*SCpnt->cmnd == READ_6 || *SCpnt->cmnd == WRITE_6) {
	mbox->numsectors = 
	  (u_long)SCpnt->cmnd[4];
	mbox->lba = 
	  ((u_long)SCpnt->cmnd[1] << 16) |
	  ((u_long)SCpnt->cmnd[2] << 8) |
	  (u_long)SCpnt->cmnd[3];
	mbox->lba &= 0x1FFFFF;
      }
      
      /* 10-byte */
      if (*SCpnt->cmnd == READ_10 || *SCpnt->cmnd == WRITE_10) {
	mbox->numsectors = 
	  (u_long)SCpnt->cmnd[8] |
	  ((u_long)SCpnt->cmnd[7] << 8);
	mbox->lba =
	  ((u_long)SCpnt->cmnd[2] << 24) |
	  ((u_long)SCpnt->cmnd[3] << 16) |
	  ((u_long)SCpnt->cmnd[4] << 8) |
	  (u_long)SCpnt->cmnd[5];
      }
      
      /* Calculate Scatter-Gather info */
      mbox->numsgelements = build_sglist(megaCfg, pScb, 
					 (u_long*)&mbox->xferaddr,
					 (u_long*)&seg);

      return pScb;
      
    default:
      SCpnt->result = (DID_BAD_TARGET << 16);
      callDone(SCpnt);
      return NULL;
    }
  }
  /*-----------------------------------------------------
   *
   *               Passthru drive commands
   *
   *-----------------------------------------------------*/
  else {
    /* Allocate a SCB and initialize passthru */
    if ((pScb = allocateSCB(megaCfg,SCpnt)) == NULL) {
      SCpnt->result = (DID_ERROR << 16);
      callDone(SCpnt);
      return NULL;
    }
    pthru = &pScb->pthru;
    mbox  = (mega_mailbox *)pScb->mboxData;
    
    memset(mbox,  0, sizeof(pScb->mboxData));
    memset(pthru, 0, sizeof(mega_passthru));
    pthru->timeout   = 0;
    pthru->ars       = 0;
    pthru->islogical = 0;
    pthru->channel   = SCpnt->channel;
    pthru->target    = SCpnt->target;
    pthru->cdblen    = SCpnt->cmd_len;
    memcpy(pthru->cdb, SCpnt->cmnd, SCpnt->cmd_len);
   
    pthru->numsgelements = build_sglist(megaCfg, pScb,
					(u_long *)&pthru->dataxferaddr,
					(u_long *)&pthru->dataxferlen);
    
    /* Initialize mailbox */
    mbox->cmd      = MEGA_MBOXCMD_PASSTHRU;
    mbox->xferaddr = virt_to_bus(pthru);

    return pScb;
  }
  return NULL;
}

/*--------------------------------------------------------------------
 * Interrupt service routine
 *--------------------------------------------------------------------*/
static void megaraid_isr(int irq, void *devp, struct pt_regs *regs)
{
  mega_host_config *megaCfg;
  u_char            byte, idx, sIdx;
  u_long            dword;
  mega_mailbox     *mbox;
  mega_scb         *pScb;
  long              flags;
  int               qCnt, qStatus;

  megaCfg = (mega_host_config *)devp;
  mbox    = (mega_mailbox *)megaCfg->mbox;

  if (megaCfg->host->irq == irq) {
    spin_lock_irqsave(&mega_lock,flags);

    if (megaCfg->flag & IN_ISR) {
      TRACE(("ISR called reentrantly!!\n"));
    }

    megaCfg->flag |= IN_ISR;

    /* Check if a valid interrupt is pending */
    if (megaCfg->flag & BOARD_QUARTZ) {
        dword = RDOUTDOOR(megaCfg);
        if (dword != 0x10001234) {
            /* Spurious interrupt */
            megaCfg->flag &= ~IN_ISR;
            spin_unlock_irqrestore(&mega_lock,flags);
            return;
        }
        WROUTDOOR(megaCfg,dword);
    } else {
        byte = READ_PORT(megaCfg->host->io_port, INTR_PORT);
        if ((byte & VALID_INTR_BYTE) == 0) {
          /* Spurious interrupt */
          megaCfg->flag &= ~IN_ISR;
          spin_unlock_irqrestore(&mega_lock,flags);
          return;
        }
        WRITE_PORT(megaCfg->host->io_port, INTR_PORT, byte);
    }
    
    qCnt    = mbox->numstatus;
    qStatus = mbox->status;

    if (qCnt > 1) {TRACE(("ISR: Received %d status\n", qCnt))
        printk(KERN_DEBUG "Got numstatus = %d\n",qCnt);
    }
    
    for(idx=0; idx<qCnt; idx++) {
      sIdx = mbox->completed[idx];
      if (sIdx > 0) {
	pScb = &megaCfg->scbList[sIdx-1];
        spin_unlock_irqrestore(&mega_lock,flags); /* locks within cmd_done */
	mega_cmd_done(megaCfg,&megaCfg->scbList[sIdx-1], qStatus);
        spin_lock_irqsave(&mega_lock,flags);
      }
    }
    if (megaCfg->flag & BOARD_QUARTZ) {
        WRINDOOR(megaCfg,virt_to_bus(megaCfg->mbox)|0x2);
        while (RDINDOOR(megaCfg) & 0x02);
    } else {
        CLEAR_INTR(megaCfg->host->io_port);
    }

    megaCfg->flag &= ~IN_ISR;
    megaCfg->flag &= ~PENDING;

    spin_unlock_irqrestore(&mega_lock,flags);

    spin_lock_irqsave(&io_request_lock, flags);
    mega_runque(NULL);
    spin_unlock_irqrestore(&io_request_lock,flags);              

#if 0
    /* Queue as a delayed ISR routine */
    queue_task_irq_off(&runq, &tq_immediate);
    mark_bh(IMMEDIATE_BH);
    spin_unlock_irqrestore(&mega_lock,flags);
#endif

  }
}

/*==================================================*/
/* Wait until the controller's mailbox is available */
/*==================================================*/
static int busyWaitMbox(mega_host_config *megaCfg)
{
  mega_mailbox *mbox = (mega_mailbox *)megaCfg->mbox;
  long          counter;

  for(counter=0; counter<0xFFFFFF; counter++) {
    if (!mbox->busy) return 0;
  }
  return -1;
}

/*=====================================================
 * Post a command to the card
 *
 * Arguments:
 *   mega_host_config *megaCfg - Controller structure
 *   u_char *mboxData - Mailbox area, 16 bytes
 *   mega_scb *pScb   - SCB posting (or NULL if N/A)
 *   int intr         - if 1, interrupt, 0 is blocking
 *=====================================================*/
static int MegaIssueCmd(mega_host_config *megaCfg,
			u_char *mboxData,
			mega_scb *pScb,
			int intr)
{
  mega_mailbox *mbox = (mega_mailbox *)megaCfg->mbox;
  long          flags;
  u_char        byte;
  u_long        cmdDone;

  mboxData[0x1] = (pScb ? pScb->idx+1 : 0x00);  /* Set cmdid */
  mboxData[0xF] = 1;                            /* Set busy */

  /* one bad report of problem when issuing a command while pending.
   * Wasn't able to duplicate, but it doesn't really affect performance
   * anyway, so don't allow command while PENDING
   */
  if (megaCfg->flag & PENDING) {
    return -1;
  }

  /* Wait until mailbox is free */
  if (busyWaitMbox(megaCfg)) {
    if (pScb) {
      TRACE(("Mailbox busy %.08lx <%d.%d.%d>\n", pScb->SCpnt->serial_number,
	     pScb->SCpnt->channel, pScb->SCpnt->target, pScb->SCpnt->lun));
    }
    return -1;
  }

  /* Copy mailbox data into host structure */
  spin_lock_irqsave(&mega_lock,flags);
  memset(mbox, 0, sizeof(mega_mailbox));
  memcpy(mbox, mboxData, 16);
  spin_unlock_irqrestore(&mega_lock,flags);

  /* Kick IO */
  megaCfg->flag |= PENDING;
  if (intr) {
    /* Issue interrupt (non-blocking) command */
    if (megaCfg->flag & BOARD_QUARTZ) {
        mbox->mraid_poll = 0; 
        mbox->mraid_ack = 0; 
        WRINDOOR(megaCfg, virt_to_bus(megaCfg->mbox) | 0x1);
    } else {
        ENABLE_INTR(megaCfg->host->io_port);
        ISSUE_COMMAND(megaCfg->host->io_port);
    }
  }
  else {      /* Issue non-ISR (blocking) command */

    if (megaCfg->flag & BOARD_QUARTZ) {

      mbox->mraid_poll = 0; 
      mbox->mraid_ack = 0; 
      WRINDOOR(megaCfg, virt_to_bus(megaCfg->mbox) | 0x1);

      while((cmdDone=RDOUTDOOR(megaCfg)) != 0x10001234);
      WROUTDOOR(megaCfg, cmdDone);

      if (pScb) {
	mega_cmd_done(megaCfg,pScb, mbox->status);
	mega_rundoneq();
      }

      WRINDOOR(megaCfg,virt_to_bus(megaCfg->mbox) | 0x2);
      while(RDINDOOR(megaCfg) & 0x2);

      megaCfg->flag &= ~PENDING;
    }
    else {
      DISABLE_INTR(megaCfg->host->io_port);
      ISSUE_COMMAND(megaCfg->host->io_port);
      
      while(!((byte=READ_PORT(megaCfg->host->io_port,INTR_PORT))&INTR_VALID));
      WRITE_PORT(megaCfg->host->io_port, INTR_PORT, byte);
      
      ENABLE_INTR(megaCfg->host->io_port);
      CLEAR_INTR(megaCfg->host->io_port);
      
      if (pScb) {
	mega_cmd_done(megaCfg,pScb, mbox->status);
	mega_rundoneq();
      }
      megaCfg->flag &= ~PENDING;
    }
  }

  return 0;
}

/*-------------------------------------------------------------------
 * Copies data to SGLIST
 *-------------------------------------------------------------------*/
static int build_sglist(mega_host_config *megaCfg, mega_scb *scb, 
			u_long *buffer, u_long *length)
{
  struct scatterlist *sgList;
  int idx;

  /* Scatter-gather not used */
  if (scb->SCpnt->use_sg == 0) {
    *buffer = virt_to_bus(scb->SCpnt->request_buffer);
    *length = (u_long)scb->SCpnt->request_bufflen;
    return 0;
  }

  sgList = (struct scatterlist *)scb->SCpnt->buffer;
  if (scb->SCpnt->use_sg == 1) {
    *buffer = virt_to_bus(sgList[0].address);
    *length = (u_long)sgList[0].length;
    return 0;
  }

  /* Copy Scatter-Gather list info into controller structure */
  for(idx=0; idx<scb->SCpnt->use_sg; idx++) {
    scb->sgList[idx].address = virt_to_bus(sgList[idx].address);
    scb->sgList[idx].length  = (u_long)sgList[idx].length;
  }
  
  /* Reset pointer and length fields */
  *buffer = virt_to_bus(scb->sgList);
  *length = 0;

  /* Return count of SG requests */
  return scb->SCpnt->use_sg;
}
    
/*--------------------------------------------------------------------
 * Initializes the address of the controller's mailbox register
 *  The mailbox register is used to issue commands to the card.
 *  Format of the mailbox area:
 *   00 01 command
 *   01 01 command id
 *   02 02 # of sectors
 *   04 04 logical bus address
 *   08 04 physical buffer address
 *   0C 01 logical drive #
 *   0D 01 length of scatter/gather list
 *   0E 01 reserved
 *   0F 01 mailbox busy
 *   10 01 numstatus byte
 *   11 01 status byte
 *--------------------------------------------------------------------*/
static int mega_register_mailbox(mega_host_config *megaCfg, u_long paddr)
{
  /* align on 16-byte boundry */
  megaCfg->mbox = &megaCfg->mailbox;
  megaCfg->mbox = (mega_mailbox *) ((((ulong)megaCfg->mbox) + 16)&0xfffffff0);
  paddr = (paddr+16)&0xfffffff0;

  /* Register mailbox area with the firmware */
  if (megaCfg->flag & BOARD_QUARTZ) {
  }
  else {
    WRITE_PORT(megaCfg->host->io_port, MBOX_PORT0, paddr         & 0xFF);
    WRITE_PORT(megaCfg->host->io_port, MBOX_PORT1, (paddr >>  8) & 0xFF);
    WRITE_PORT(megaCfg->host->io_port, MBOX_PORT2, (paddr >> 16) & 0xFF);
    WRITE_PORT(megaCfg->host->io_port, MBOX_PORT3, (paddr >> 24) & 0xFF);
    WRITE_PORT(megaCfg->host->io_port, ENABLE_MBOX_REGION, ENABLE_MBOX_BYTE);
    
    CLEAR_INTR(megaCfg->host->io_port);
    ENABLE_INTR(megaCfg->host->io_port);
  }
  return 0;
}

/*-------------------------------------------------------------------
 * Issue an adapter info query to the controller
 *-------------------------------------------------------------------*/
static int mega_i_query_adapter(mega_host_config *megaCfg)
{
  mega_RAIDINQ *adapterInfo;
  mega_mailbox *mbox;
  u_char        mboxData[16];
  u_long        paddr;

  spin_lock_init(&mega_lock);
  /* Initialize adapter inquiry */
  paddr = virt_to_bus(megaCfg->mega_buffer);
  mbox  = (mega_mailbox *)mboxData;

  memset((void *)megaCfg->mega_buffer, 0, sizeof(megaCfg->mega_buffer));
  memset(mbox, 0, 16);

  /* Initialize mailbox registers */
  mbox->cmd      = MEGA_MBOXCMD_ADAPTERINQ;
  mbox->xferaddr = paddr;

  /* Issue a blocking command to the card */
  MegaIssueCmd(megaCfg, mboxData, NULL, 0);
  
  /* Initialize host/local structures with Adapter info */
  adapterInfo = (mega_RAIDINQ *)megaCfg->mega_buffer;
  megaCfg->host->max_channel = adapterInfo->AdpInfo.ChanPresent;
  megaCfg->host->max_id      = adapterInfo->AdpInfo.MaxTargPerChan;
  megaCfg->numldrv           = adapterInfo->LogdrvInfo.NumLDrv;

#if 0
  printk(KERN_DEBUG "---- Logical drive info ----\n");
  for(i=0; i<megaCfg->numldrv; i++) {
    printk(KERN_DEBUG "%d: size: %ld prop: %x state: %x\n",i,
	   adapterInfo->LogdrvInfo.LDrvSize[i],
	   adapterInfo->LogdrvInfo.LDrvProp[i],
	   adapterInfo->LogdrvInfo.LDrvState[i]);
  }
  printk(KERN_DEBUG "---- Physical drive info ----\n");
  for(i=0; i<MAX_PHYSICAL_DRIVES; i++) {
    if (i && !(i % 8)) printk("\n");
    printk("%d: %x   ", i, adapterInfo->PhysdrvInfo.PDrvState[i]);
  }
  printk("\n");
#endif

  megaCfg->max_cmds = adapterInfo->AdpInfo.MaxConcCmds;

#ifdef HP            /* use HP firmware and bios version encoding */
      sprintf(megaCfg->fwVer,"%c%d%d.%d%d",
          adapterInfo->AdpInfo.FwVer[2],
          adapterInfo->AdpInfo.FwVer[1] >> 8,
          adapterInfo->AdpInfo.FwVer[1] & 0x0f,
          adapterInfo->AdpInfo.FwVer[2] >> 8,
          adapterInfo->AdpInfo.FwVer[2] & 0x0f);
      sprintf(megaCfg->biosVer,"%c%d%d.%d%d",
          adapterInfo->AdpInfo.BiosVer[2],
          adapterInfo->AdpInfo.BiosVer[1] >> 8,
          adapterInfo->AdpInfo.BiosVer[1] & 0x0f,
          adapterInfo->AdpInfo.BiosVer[2] >> 8,
          adapterInfo->AdpInfo.BiosVer[2] & 0x0f);
#else
      memcpy(megaCfg->fwVer, adapterInfo->AdpInfo.FwVer, 4);
      megaCfg->fwVer[4] = 0;

      memcpy(megaCfg->biosVer, adapterInfo->AdpInfo.BiosVer, 4);
      megaCfg->biosVer[4] = 0;
#endif

  printk(KERN_INFO "megaraid: [%s:%s] detected %d logical drives" CRLFSTR,
	 megaCfg->fwVer,
	 megaCfg->biosVer,
	 megaCfg->numldrv);
  return 0;
}

/*-------------------------------------------------------------------------
 *
 *                      Driver interface functions
 *
 *-------------------------------------------------------------------------*/

/*----------------------------------------------------------
 * Returns data to be displayed in /proc/scsi/megaraid/X
 *----------------------------------------------------------*/
int megaraid_proc_info(char *buffer, char **start, off_t offset,
		       int length, int inode, int inout)
{
  *start = buffer;
  return 0;
}

int findCard(Scsi_Host_Template *pHostTmpl, 
	     u_short pciVendor, u_short pciDev,
	     long flag)
{
  mega_host_config *megaCfg;
  struct Scsi_Host *host;
  u_char            pciBus, pciDevFun, megaIrq;
  u_long            megaBase;
  u_short           pciIdx = 0;

#if LINUX_VERSION_CODE < 0x20100
  while(!pcibios_find_device(pciVendor, pciDev, pciIdx,&pciBus,&pciDevFun)) {
#else
  struct pci_dev   *pdev=pci_devices;

  while((pdev = pci_find_device(pciVendor, pciDev, pdev))) {
    pciBus = pdev->bus->number;
    pciDevFun = pdev->devfn;
#endif
    printk(KERN_INFO "megaraid: found 0x%4.04x:0x%4.04x:idx %d:bus %d:slot %d:fun %d\n",
	   pciVendor, 
	   pciDev, 
	   pciIdx, pciBus, 
	   PCI_SLOT(pciDevFun), 
	   PCI_FUNC(pciDevFun));
    
    /* Read the base port and IRQ from PCI */
#if LINUX_VERSION_CODE < 0x20100
    pcibios_read_config_dword(pciBus, pciDevFun,
			     PCI_BASE_ADDRESS_0,
			     (u_int *)&megaBase);
    pcibios_read_config_byte(pciBus, pciDevFun,			       
			     PCI_INTERRUPT_LINE,
			     &megaIrq);
#else
    megaBase = pdev->base_address[0];
    megaIrq = pdev->irq;
#endif
    pciIdx++;

    if (flag & BOARD_QUARTZ) {
      megaBase &= PCI_BASE_ADDRESS_MEM_MASK;
      megaBase = (long) ioremap(megaBase,128);
    }
    else {
      megaBase &= PCI_BASE_ADDRESS_IO_MASK;
      megaBase += 0x10;
    }

    /* Initialize SCSI Host structure */
    host    = scsi_register(pHostTmpl, sizeof(mega_host_config));
    megaCfg = (mega_host_config *)host->hostdata;
    memset(megaCfg, 0, sizeof(mega_host_config));

    printk(KERN_INFO " scsi%d: Found a MegaRAID controller at 0x%x, IRQ: %d" CRLFSTR, 
	   host->host_no, (u_int)megaBase, megaIrq);
    
    /* Copy resource info into structure */
    megaCfg->flag            = flag;
    megaCfg->host            = host;
    megaCfg->base            = megaBase;
    megaCfg->host->irq       = megaIrq;
    megaCfg->host->io_port   = megaBase;
    megaCfg->host->n_io_port = 16;
    megaCfg->host->unique_id = (pciBus << 8) | pciDevFun;
    megaCtlrs[numCtlrs++]    = megaCfg;

    if (flag != BOARD_QUARTZ) {
      /* Request our IO Range */
      if (check_region(megaBase, 16)) {
	printk(KERN_WARNING "megaraid: Couldn't register I/O range!" CRLFSTR);
	scsi_unregister(host);
	continue;
      }
      request_region(megaBase, 16, "megaraid");
    }

    /* Request our IRQ */
    if (request_irq(megaIrq, megaraid_isr, SA_SHIRQ, 
		    "megaraid", megaCfg)) {
      printk(KERN_WARNING "megaraid: Couldn't register IRQ %d!" CRLFSTR,
	     megaIrq);
      scsi_unregister(host);
      continue;
    }

    mega_register_mailbox(megaCfg, virt_to_bus((void*)&megaCfg->mailbox));
    mega_i_query_adapter(megaCfg);

    /* Initialize SCBs */
    initSCB(megaCfg);

  }
  return pciIdx;
}

/*---------------------------------------------------------
 * Detects if a megaraid controller exists in this system
 *---------------------------------------------------------*/
int megaraid_detect(Scsi_Host_Template *pHostTmpl)
{
  int count = 0;

  pHostTmpl->proc_dir = &proc_scsi_megaraid;

#if LINUX_VERSION_CODE < 0x20100
  if (!pcibios_present()) 
    {
      printk(KERN_WARNING "megaraid: PCI bios not present." CRLFSTR);
      return 0;
    }
#endif

  count += findCard(pHostTmpl, 0x101E, 0x9010, 0);
  count += findCard(pHostTmpl, 0x101E, 0x9060, 0);
  count += findCard(pHostTmpl, 0x8086, 0x1960, BOARD_QUARTZ);

  return count;
}

/*---------------------------------------------------------------------
 * Release the controller's resources
 *---------------------------------------------------------------------*/
int megaraid_release(struct Scsi_Host *pSHost)
{
  mega_host_config *megaCfg;
  mega_mailbox         *mbox;
  u_char                mboxData[16];

  megaCfg = (mega_host_config*)pSHost->hostdata;
  mbox    = (mega_mailbox *)mboxData;

  /* Flush cache to disk */
  memset(mbox, 0, 16);
  mboxData[0] = 0xA;

  /* Issue a blocking (interrupts disabled) command to the card */
  MegaIssueCmd(megaCfg, mboxData, NULL, 0);

  schedule();

  /* Free our resources */
  if (megaCfg->flag & BOARD_QUARTZ) {
      iounmap((void *)megaCfg->base);
  } else {
      release_region(megaCfg->host->io_port, 16);
  }
  free_irq(megaCfg->host->irq, megaCfg); /* Must be freed first, otherwise
                                            extra interrupt is generated */
  scsi_unregister(pSHost);

  return 0;
}

/*----------------------------------------------
 * Get information about the card/driver 
 *----------------------------------------------*/
const char *megaraid_info(struct Scsi_Host *pSHost)
{
  static char           buffer[512];
  mega_host_config  *megaCfg;
  mega_RAIDINQ          *adapterInfo;

  megaCfg     = (mega_host_config *)pSHost->hostdata;
  adapterInfo = (mega_RAIDINQ *)megaCfg->mega_buffer;

  sprintf(buffer, "AMI MegaRAID %s %d commands %d targs %d chans",
	  megaCfg->fwVer,
	  adapterInfo->AdpInfo.MaxConcCmds,
	  megaCfg->host->max_id,
	  megaCfg->host->max_channel);
  return buffer;
}

/*-----------------------------------------------------------------
 * Perform a SCSI command
 * Mailbox area:
 *   00 01 command
 *   01 01 command id
 *   02 02 # of sectors
 *   04 04 logical bus address
 *   08 04 physical buffer address
 *   0C 01 logical drive #
 *   0D 01 length of scatter/gather list
 *   0E 01 reserved
 *   0F 01 mailbox busy
 *   10 01 numstatus byte
 *   11 01 status byte 
 *-----------------------------------------------------------------*/
int megaraid_queue(Scsi_Cmnd *SCpnt, void (*pktComp)(Scsi_Cmnd *))
{
  mega_host_config *megaCfg;
  mega_scb         *pScb;

  megaCfg = (mega_host_config *)SCpnt->host->hostdata;

  if (!(megaCfg->flag & (1L << SCpnt->channel))) {
    printk(KERN_INFO "scsi%d: scanning channel %c for devices.\n",
	   megaCfg->host->host_no,
	   SCpnt->channel + 'A');
    megaCfg->flag |= (1L << SCpnt->channel);
  }

  SCpnt->scsi_done = pktComp;

  /* Allocate and build a SCB request */
  if ((pScb = mega_build_cmd(megaCfg, SCpnt)) != NULL) {
    /* Add SCB to the head of the pending queue */
    ENQUEUE(pScb, mega_scb, qPending, next);

    /* Issue the command to the card */
      mega_runque(NULL);
  }

  return 0;
}

/*----------------------------------------------------------------------
 * Issue a blocking command to the controller
 *
 * Note - this isnt 2.0.x SMP safe
 *----------------------------------------------------------------------*/
volatile static int internal_done_flag    = 0;
volatile static int internal_done_errcode = 0;

static void internal_done(Scsi_Cmnd *SCpnt)
{
  internal_done_errcode = SCpnt->result;
  internal_done_flag++;
}

/*
 *	This seems dangerous in an SMP environment because 
 *	while spinning on internal_done_flag in 2.0.x SMP
 *	no IRQ's will be taken, including those that might
 *	be needed to clear this.
 *
 *	I think this should be using a wait queue ?
 *				-- AC
 */
 
int megaraid_command(Scsi_Cmnd *SCpnt)
{
  internal_done_flag = 0;

  /* Queue command, and wait until it has completed */
  megaraid_queue(SCpnt, internal_done);

  while(!internal_done_flag)
    barrier();

  return internal_done_errcode;
}

/*---------------------------------------------------------------------
 * Abort a previous SCSI request
 *---------------------------------------------------------------------*/
int megaraid_abort(Scsi_Cmnd *SCpnt)
{
  mega_host_config *megaCfg;
  int       idx;
  long      flags;

  spin_lock_irqsave(&mega_lock,flags);

  megaCfg = (mega_host_config *)SCpnt->host->hostdata;

  TRACE(("ABORT!!! %.08lx %.02x <%d.%d.%d>\n",
	 SCpnt->serial_number, SCpnt->cmnd[0], SCpnt->channel, SCpnt->target, 
	 SCpnt->lun));
  /*
   * Walk list of SCBs for any that are still outstanding
   */
  for(idx=0; idx<megaCfg->max_cmds; idx++) {
    if (megaCfg->scbList[idx].idx >= 0) {
      if (megaCfg->scbList[idx].SCpnt == SCpnt) {
	freeSCB(&megaCfg->scbList[idx]);

	SCpnt->result = (DID_RESET << 16) | (SUGGEST_RETRY<<24);
	callDone(SCpnt);
      }
    }
  }
  spin_unlock_irqrestore(&mega_lock,flags);
  return SCSI_ABORT_SNOOZE;
}

/*---------------------------------------------------------------------
 * Reset a previous SCSI request
 *---------------------------------------------------------------------*/
int megaraid_reset(Scsi_Cmnd *SCpnt, unsigned int rstflags)
{
  mega_host_config *megaCfg;
  int       idx;
  long      flags;

  spin_lock_irqsave(&mega_lock,flags);

  megaCfg = (mega_host_config *)SCpnt->host->hostdata;

  TRACE(("RESET: %.08lx %.02x <%d.%d.%d>\n",
	 SCpnt->serial_number, SCpnt->cmnd[0], SCpnt->channel, SCpnt->target, 
	 SCpnt->lun));

  /*
   * Walk list of SCBs for any that are still outstanding
   */
  for(idx=0; idx<megaCfg->max_cmds; idx++) {
    if (megaCfg->scbList[idx].idx >= 0) {
      SCpnt = megaCfg->scbList[idx].SCpnt;
      freeSCB(&megaCfg->scbList[idx]);
      SCpnt->result = (DID_RESET << 16) | (SUGGEST_RETRY<<24);
      callDone(SCpnt);
    }
  }
  spin_unlock_irqrestore(&mega_lock,flags);
  return SCSI_RESET_PUNT;
} 

/*-------------------------------------------------------------
 * Return the disk geometry for a particular disk
 * Input:
 *   Disk *disk - Disk geometry
 *   kdev_t dev - Device node
 *   int *geom  - Returns geometry fields
 *     geom[0] = heads
 *     geom[1] = sectors
 *     geom[2] = cylinders
 *-------------------------------------------------------------*/
int megaraid_biosparam(Disk *disk, kdev_t dev, int *geom)
{
  int                   heads, sectors, cylinders;
  mega_host_config *megaCfg;

  /* Get pointer to host config structure */
  megaCfg = (mega_host_config *)disk->device->host->hostdata;

  /* Default heads (64) & sectors (32) */
  heads     = 64;
  sectors   = 32;
  cylinders = disk->capacity / (heads * sectors);

  /* Handle extended translation size for logical drives > 1Gb */
  if (disk->capacity >= 0x200000) {
    heads     = 255;
    sectors   = 63;
    cylinders = disk->capacity / (heads * sectors);
  }

  /* return result */
  geom[0] = heads;
  geom[1] = sectors;
  geom[2] = cylinders;

  return 0;
}

#ifdef MODULE
Scsi_Host_Template driver_template = MEGARAID;

#include "scsi_module.c"
#endif
