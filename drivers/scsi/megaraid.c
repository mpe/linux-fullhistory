/*===================================================================
 *
 *                    Linux MegaRAID device driver
 *
 * Copyright 1998 American Megatrends Inc.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Version : 1.00
 * 
 * Description: Linux device driver for AMI MegaRAID controller
 *
 * Supported controllers: MegaRAID 418, 428, 438, 466, 762
 * 
 * Maintainer: Jeff L Jones <jeffreyj@ami.com>
 *
 * History:
 *
 * Version 0.90:
 *     Original source contributed by Dell; integrated it into the kernel and
 *     cleaned up some things.  Added support for 438/466 controllers.
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
 *      Small changes to the comments/formatting. Plus a couple of
 *      added notes. Returned to the authors. No actual code changes
 *      save printk levels.
 *      8 Oct 98        Alan Cox <alan.cox@linux.org>
 *
 *     Merged with 2.1.131 source tree.
 *     12 Dec 98       K. Baranowski <kgb@knm.org.pl>                          
 *
 * Version 0.93:
 *     Added support for vendor specific ioctl commands (0x80+xxh)
 *     Changed some fields in MEGARAID struct to better values.
 *     Added signature check for Rp controllers under 2.0 kernels
 *     Changed busy-wait loop to be time-based
 *     Fixed SMP race condition in isr
 *     Added kfree (sgList) on release
 *     Added #include linux/version.h to megaraid.h for hosts.h
 *     Changed max_id to represent max logical drives instead of targets.
 *
 * Version 0.94:
 *     Got rid of some excess locking/unlocking
 *     Fixed slight memory corruption problem while memcpy'ing into mailbox
 *     Changed logical drives to be reported as luns rather than targets
 *     Changed max_id to 16 since it is now max targets/chan again.
 *     Improved ioctl interface for upcoming megamgr
 *
 * Version 0.95:
 *     Fixed problem of queueing multiple commands to adapter;
 *       still has some strange problems on some setups, so still
 *       defaults to single.  To enable parallel commands change
 *       #define MULTI_IO in megaraid.h
 *     Changed kmalloc allocation to be done in beginning.
 *     Got rid of C++ style comments
 *
 * Version 0.96:
 *     762 fully supported.
 * Version 0.97:
 *     Changed megaraid_command to use wait_queue.
 *     Fixed bug of undesirably detecting HP onboard controllers which
 *      are disabled.
 *     
 * Version 1.00:
 *     Checks to see if an irq ocurred while in isr, and runs through
 *        routine again.
 *     Copies mailbox to temp area before processing in isr
 *     Added barrier() in busy wait to fix volatility bug
 *     Uses separate list for freed Scbs, keeps track of cmd state
 *     Put spinlocks around entire queue function for now...
 *     Full multi-io commands working stablely without previous problems
 *     Added skipXX LILO option for Madrona motherboard support
 *
 *
 * BUGS:
 *     Some older 2.1 kernels (eg. 2.1.90) have a bug in pci.c that
 *     fails to detect the controller as a pci device on the system.
 *
 *     Timeout period for mid scsi layer is too short for
 *     this controller.  Must be increased or Aborts will occur.
 *
 *===================================================================*/

#define CRLFSTR "\n"

#include <linux/config.h>
#include <linux/version.h>

#ifdef MODULE
#include <linux/modversions.h>
#include <linux/module.h>

#if LINUX_VERSION_CODE >= 0x20100
char kernel_version[] = UTS_RELEASE;

MODULE_AUTHOR ("American Megatrends Inc.");
MODULE_DESCRIPTION ("AMI MegaRAID driver");
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
 *================================================================
 */

#if LINUX_VERSION_CODE < 0x020100
#define ioremap vremap
#define iounmap vfree

/* simulate spin locks */
typedef struct {
  volatile char lock;
} spinlock_t;

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
}

/* a non-locking version (if we already have the lock) */
#define ENQUEUE_NL(obj,type,list,next) \
{ type **node; \
  for(node=&(list); *node; node=(type **)&(*node)->##next); \
  (*node) = obj; \
  (*node)->##next = NULL; \
}

#define DEQUEUE(obj,type,list,next) \
{ long cpuflag; \
  spin_lock_irqsave(&mega_lock,cpuflag);\
  if ((obj=list) != NULL) {\
    list = (type *)(list)->##next; \
  } \
  spin_unlock_irqrestore(&mega_lock,cpuflag);\
};

u_long RDINDOOR (mega_host_config * megaCfg)
{
  return readl (megaCfg->base + 0x20);
}

void WRINDOOR (mega_host_config * megaCfg, u_long value)
{
  writel (value, megaCfg->base + 0x20);
}

u_long RDOUTDOOR (mega_host_config * megaCfg)
{
  return readl (megaCfg->base + 0x2C);
}

void WROUTDOOR (mega_host_config * megaCfg, u_long value)
{
  writel (value, megaCfg->base + 0x2C);
}

/*================================================================
 *
 *                    Function prototypes
 *
 *================================================================
 */
static int megaIssueCmd (mega_host_config * megaCfg,
			 u_char * mboxData,
			 mega_scb * scb,
			 int intr);
static int build_sglist (mega_host_config * megaCfg, mega_scb * scb,
			 u_long * buffer, u_long * length);

static int mega_busyWaitMbox(mega_host_config *);
static void mega_runpendq (mega_host_config *);
static void mega_rundoneq (void);
static void mega_cmd_done (mega_host_config *, mega_scb *, int);
static mega_scb *mega_ioctl (mega_host_config * megaCfg, Scsi_Cmnd * SCpnt);
static inline void freeSgList(mega_host_config *megaCfg);

/* set SERDEBUG to 1 to enable serial debugging */
#define SERDEBUG 0
#if SERDEBUG
static void ser_init (void);
static void ser_puts (char *str);
static void ser_putc (char c);
static int ser_printk (const char *fmt,...);
#endif

/*================================================================
 *
 *                    Global variables
 *
 *================================================================
 */

/*  Use "megaraid=skipXX" to prohibit driver from scanning XX scsi id
     on each channel.  Used for Madrona motherboard, where SAF_TE
     processor id cannot be scanned */
static char *megaraid;
#if LINUX_VERSION_CODE > 0x20100
#ifdef MODULE
MODULE_PARM(megaraid, "s");
#endif
#endif
static int skip_id;

static int numCtlrs = 0;
static mega_host_config *megaCtlrs[12] = {0};

#if DEBUG
static u_long maxCmdTime = 0;
#endif

static mega_scb *pLastScb = NULL;

/* Queue of pending/completed SCBs */
static Scsi_Cmnd *qCompleted = NULL;

#if SERDEBUG
volatile static spinlock_t serial_lock;
#endif
volatile static spinlock_t mega_lock;

struct proc_dir_entry proc_scsi_megaraid =
{
  PROC_SCSI_MEGARAID, 8, "megaraid",
  S_IFDIR | S_IRUGO | S_IXUGO, 2
};

#if SERDEBUG
static char strbuf[MAX_SERBUF + 1];

static void ser_init ()
{
  unsigned port = COM_BASE;

  outb (0x80, port + 3);
  outb (0, port + 1);
  /* 9600 Baud, if 19200: outb(6,port) */
  outb (12, port);
  outb (3, port + 3);
  outb (0, port + 1);
}

static void ser_puts (char *str)
{
  char *ptr;

  ser_init ();
  for (ptr = str; *ptr; ++ptr)
    ser_putc (*ptr);
}

static void ser_putc (char c)
{
  unsigned port = COM_BASE;

  while ((inb (port + 5) & 0x20) == 0);
  outb (c, port);
  if (c == 0x0a) {
    while ((inb (port + 5) & 0x20) == 0);
    outb (0x0d, port);
  }
}

static int ser_printk (const char *fmt,...)
{
  va_list args;
  int i;
  long flags;

  spin_lock_irqsave(&serial_lock,flags);
  va_start (args, fmt);
  i = vsprintf (strbuf, fmt, args);
  ser_puts (strbuf);
  va_end (args);
  spin_unlock_irqrestore(&serial_lock,flags);

  return i;
}

#define TRACE(a)    { ser_printk a;}

#else
#define TRACE(A)
#endif

void callDone (Scsi_Cmnd * SCpnt)
{
  if (SCpnt->result) {
    TRACE (("*** %.08lx %.02x <%d.%d.%d> = %x\n", SCpnt->serial_number,
	    SCpnt->cmnd[0], SCpnt->channel, SCpnt->target, SCpnt->lun,
	    SCpnt->result));
  }
  SCpnt->scsi_done (SCpnt);
}

/*-------------------------------------------------------------------------
 *
 *                      Local functions
 *
 *-------------------------------------------------------------------------*/

/*=======================
 * Free a SCB structure
 *=======================
 */
static void freeSCB (mega_host_config *megaCfg, mega_scb * pScb)
{
  mega_scb **ppScb;

  /* Unlink from pending queue */
  for(ppScb=&megaCfg->qPending; *ppScb; ppScb=&(*ppScb)->next) {
    if (*ppScb == pScb) {
	*ppScb = pScb->next;
	break;
    }
  }

  /* Link back into list */
  pScb->state = SCB_FREE;
  pScb->SCpnt = NULL;

  pScb->next     = megaCfg->qFree;
  megaCfg->qFree = pScb;
}

/*===========================
 * Allocate a SCB structure
 *===========================
 */
static mega_scb * allocateSCB (mega_host_config * megaCfg, Scsi_Cmnd * SCpnt)
{
  mega_scb *pScb;

  /* Unlink command from Free List */
  if ((pScb = megaCfg->qFree) != NULL) {
    megaCfg->qFree = pScb->next;
    
    pScb->isrcount = jiffies;
    pScb->next  = NULL;
    pScb->state = SCB_ACTIVE;
    pScb->SCpnt = SCpnt;

    return pScb;
  }

  printk (KERN_WARNING "Megaraid: Could not allocate free SCB!!!\n");

  return NULL;
}

/*================================================
 * Initialize SCB structures
 *================================================
 */
static int initSCB (mega_host_config * megaCfg)
{
  int idx;

  megaCfg->qFree = NULL;
  for (idx = megaCfg->max_cmds-1; idx >= 0; idx--) {
    megaCfg->scbList[idx].idx    = idx;
    megaCfg->scbList[idx].sgList = kmalloc(sizeof(mega_sglist) * MAX_SGLIST,
					   GFP_ATOMIC | GFP_DMA);
    if (megaCfg->scbList[idx].sgList == NULL) {
      printk(KERN_WARNING "Can't allocate sglist for id %d\n",idx);
      freeSgList(megaCfg);
      return -1;
    }
    
    if (idx < MAX_COMMANDS) {
      /* Link to free list */
      freeSCB(megaCfg, &megaCfg->scbList[idx]);
    }
  }
  return 0;
}

/* Run through the list of completed requests */
static void mega_rundoneq ()
{
  Scsi_Cmnd *SCpnt;

  while (1) {
    DEQUEUE (SCpnt, Scsi_Cmnd, qCompleted, host_scribble);
    if (SCpnt == NULL)
      return;

    /* Callback */
    callDone (SCpnt);
  }
}

/*
  Runs through the list of pending requests
  Assumes that mega_lock spin_lock has been acquired.
*/
static void mega_runpendq(mega_host_config *megaCfg)
{
  mega_scb *pScb;

  /* Issue any pending commands to the card */
  for(pScb=megaCfg->qPending; pScb; pScb=pScb->next) {
    if (pScb->state == SCB_ACTIVE) {
      megaIssueCmd(megaCfg, pScb->mboxData, pScb, 1);
    }
  }
}

/* Add command to the list of completed requests */
static void mega_cmd_done (mega_host_config * megaCfg, mega_scb * pScb, 
			   int status)
{
  int islogical;
  Scsi_Cmnd *SCpnt;

  if (pScb == NULL) {
	TRACE(("NULL pScb in mega_cmd_done!"));
	printk("NULL pScb in mega_cmd_done!");
  }

  SCpnt = pScb->SCpnt;
  freeSCB(megaCfg, pScb);

  if (SCpnt == NULL) {
	TRACE(("NULL SCpnt in mega_cmd_done!"));
	TRACE(("pScb->idx = ",pScb->idx));
	TRACE(("pScb->state = ",pScb->state));
	TRACE(("pScb->state = ",pScb->state));
	printk("Problem...!\n");
	while(1);
  }

  islogical = (SCpnt->channel == megaCfg->host->max_channel &&
	       SCpnt->target == 0);
  if (SCpnt->cmnd[0] == INQUIRY &&
      ((((u_char *) SCpnt->request_buffer)[0] & 0x1F) == TYPE_DISK) &&
      !islogical) {
    status = 0xF0;
  }
 
  SCpnt->result = 0;  /* clear result; otherwise, success returns corrupt
                         value */

  /* Convert MegaRAID status to Linux error code */
  switch (status) {
  case 0x00: /* SUCCESS */
  case 0x02: /* ERROR_ABORTED */
    SCpnt->result |= (DID_OK << 16);
    break;
  case 0x8:  /* ERR_DEST_DRIVE_FAILED */
    SCpnt->result |= (DID_BUS_BUSY << 16);
    break;
  default:
    SCpnt->result |= (DID_BAD_TARGET << 16);
    break;
  }

  /* Add Scsi_Command to end of completed queue */
  ENQUEUE_NL(SCpnt, Scsi_Cmnd, qCompleted, host_scribble);
}

/*-------------------------------------------------------------------
 *
 *                 Build a SCB from a Scsi_Cmnd
 *
 * Returns a SCB pointer, or NULL
 * If NULL is returned, the scsi_done function MUST have been called
 *
 *-------------------------------------------------------------------*/
static mega_scb * mega_build_cmd (mega_host_config * megaCfg, 
				  Scsi_Cmnd * SCpnt)
{
  mega_scb *pScb;
  mega_mailbox *mbox;
  mega_passthru *pthru;
  long seg;
  char islogical;

  if (SCpnt == NULL) {
	printk("NULL SCpnt in mega_build_cmd!\n");
	while(1);
  }

  if (SCpnt->cmnd[0] & 0x80)	/* ioctl from megamgr */
    return mega_ioctl (megaCfg, SCpnt);

  islogical = (SCpnt->channel == megaCfg->host->max_channel && SCpnt->target == 0);

  if (!islogical && SCpnt->lun != 0) {
    SCpnt->result = (DID_BAD_TARGET << 16);
    callDone (SCpnt);
    return NULL;
  }

  if (!islogical && SCpnt->target == skip_id) {
	SCpnt->result = (DID_BAD_TARGET << 16);
	callDone (SCpnt);
	return NULL;
  }

  /*-----------------------------------------------------
   *
   *               Logical drive commands
   *
   *-----------------------------------------------------*/
  if (islogical) {
    switch (SCpnt->cmnd[0]) {
    case TEST_UNIT_READY:
      memset (SCpnt->request_buffer, 0, SCpnt->request_bufflen);
      SCpnt->result = (DID_OK << 16);
      callDone (SCpnt);
      return NULL;

    case MODE_SENSE:
      memset (SCpnt->request_buffer, 0, SCpnt->cmnd[4]);
      SCpnt->result = (DID_OK << 16);
      callDone (SCpnt);
      return NULL;

    case READ_CAPACITY:
    case INQUIRY:
      /* Allocate a SCB and initialize passthru */
      if ((pScb = allocateSCB (megaCfg, SCpnt)) == NULL) {
	SCpnt->result = (DID_ERROR << 16);
	callDone (SCpnt);
	return NULL;
      }
      pthru = &pScb->pthru;
      mbox = (mega_mailbox *) & pScb->mboxData;

      memset (mbox, 0, sizeof (pScb->mboxData));
      memset (pthru, 0, sizeof (mega_passthru));
      pthru->timeout = 0;
      pthru->ars = 1;
      pthru->reqsenselen = 14;
      pthru->islogical = 1;
      pthru->logdrv = SCpnt->lun;
      pthru->cdblen = SCpnt->cmd_len;
      pthru->dataxferaddr = virt_to_bus (SCpnt->request_buffer);
      pthru->dataxferlen = SCpnt->request_bufflen;
      memcpy (pthru->cdb, SCpnt->cmnd, SCpnt->cmd_len);

      /* Initialize mailbox area */
      mbox->cmd = MEGA_MBOXCMD_PASSTHRU;
      mbox->xferaddr = virt_to_bus (pthru);

      return pScb;

    case READ_6:
    case WRITE_6:
    case READ_10:
    case WRITE_10:
      /* Allocate a SCB and initialize mailbox */
      if ((pScb = allocateSCB (megaCfg, SCpnt)) == NULL) {
	SCpnt->result = (DID_ERROR << 16);
	callDone (SCpnt);
	return NULL;
      }
      mbox = (mega_mailbox *) & pScb->mboxData;

      memset (mbox, 0, sizeof (pScb->mboxData));
      mbox->logdrv = SCpnt->lun;
      mbox->cmd = (*SCpnt->cmnd == READ_6 || *SCpnt->cmnd == READ_10) ?
	MEGA_MBOXCMD_LREAD : MEGA_MBOXCMD_LWRITE;

      /* 6-byte */
      if (*SCpnt->cmnd == READ_6 || *SCpnt->cmnd == WRITE_6) {
	mbox->numsectors =
	  (u_long) SCpnt->cmnd[4];
	mbox->lba =
	  ((u_long) SCpnt->cmnd[1] << 16) |
	  ((u_long) SCpnt->cmnd[2] << 8) |
	  (u_long) SCpnt->cmnd[3];
	mbox->lba &= 0x1FFFFF;
      }

      /* 10-byte */
      if (*SCpnt->cmnd == READ_10 || *SCpnt->cmnd == WRITE_10) {
	mbox->numsectors =
	  (u_long) SCpnt->cmnd[8] |
	  ((u_long) SCpnt->cmnd[7] << 8);
	mbox->lba =
	  ((u_long) SCpnt->cmnd[2] << 24) |
	  ((u_long) SCpnt->cmnd[3] << 16) |
	  ((u_long) SCpnt->cmnd[4] << 8) |
	  (u_long) SCpnt->cmnd[5];
      }

      /* Calculate Scatter-Gather info */
      mbox->numsgelements = build_sglist (megaCfg, pScb,
					  (u_long *) & mbox->xferaddr,
					  (u_long *) & seg);

      return pScb;

    default:
      SCpnt->result = (DID_BAD_TARGET << 16);
      callDone (SCpnt);
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
    if ((pScb = allocateSCB (megaCfg, SCpnt)) == NULL) {
      SCpnt->result = (DID_ERROR << 16);
      callDone (SCpnt);
      return NULL;
    }
    pthru = &pScb->pthru;
    mbox = (mega_mailbox *) pScb->mboxData;

    memset (mbox, 0, sizeof (pScb->mboxData));
    memset (pthru, 0, sizeof (mega_passthru));
    pthru->timeout = 0;
    pthru->ars = 1;
    pthru->reqsenselen = 14;
    pthru->islogical = 0;
    pthru->channel = SCpnt->channel;
    pthru->target = SCpnt->target;
    pthru->cdblen = SCpnt->cmd_len;
    memcpy (pthru->cdb, SCpnt->cmnd, SCpnt->cmd_len);

    pthru->numsgelements = build_sglist (megaCfg, pScb,
					 (u_long *) & pthru->dataxferaddr,
					 (u_long *) & pthru->dataxferlen);

    /* Initialize mailbox */
    mbox->cmd = MEGA_MBOXCMD_PASSTHRU;
    mbox->xferaddr = virt_to_bus (pthru);

    return pScb;
  }
  return NULL;
}

/*--------------------------------------------------------------------
 * build RAID commands for controller, passed down through ioctl()
 *--------------------------------------------------------------------*/
static mega_scb * mega_ioctl (mega_host_config * megaCfg, Scsi_Cmnd * SCpnt)
{
  mega_scb *pScb;
  mega_ioctl_mbox *mbox;
  mega_mailbox *mailbox;
  mega_passthru *pthru;
  long seg;
  unsigned char *data = (unsigned char *)SCpnt->request_buffer;
  int i;

  if ((pScb = allocateSCB (megaCfg, SCpnt)) == NULL) {
    SCpnt->result = (DID_ERROR << 16);
    callDone (SCpnt);
    return NULL;
  }

#if 0
  printk("\nBUF: ");
  for (i=0;i<18;i++) {
     printk(" %x",data[i]);
  }
  printk("......\n");
#endif

  mbox = (mega_ioctl_mbox *) & pScb->mboxData;
  mailbox = (mega_mailbox *) & pScb->mboxData;
  memset (mailbox, 0, sizeof (pScb->mboxData));

  if (data[0] == 0x03) {	/* passthrough command */
    unsigned char cdblen = data[2];

    pthru = &pScb->pthru;
    memset (pthru, 0, sizeof (mega_passthru));
    pthru->islogical = (data[cdblen+3] & 0x80) ? 1:0;
    pthru->timeout = data[cdblen+3] & 0x07;
    pthru->reqsenselen = 14;
    pthru->ars = (data[cdblen+3] & 0x08) ? 1:0;
    pthru->logdrv = data[cdblen+4];
    pthru->channel = data[cdblen+5];
    pthru->target = data[cdblen+6];
    pthru->cdblen = cdblen;
    memcpy (pthru->cdb, &data[3], cdblen);

    mailbox->cmd = MEGA_MBOXCMD_PASSTHRU;
    mailbox->xferaddr = virt_to_bus (pthru);


    pthru->numsgelements = build_sglist (megaCfg, pScb,
					 (u_long *) & pthru->dataxferaddr,
					 (u_long *) & pthru->dataxferlen);

    for (i=0;i<(SCpnt->request_bufflen-cdblen-7);i++) {
       data[i] = data[i+cdblen+7];
    }

    return pScb;
  }
  /* else normal (nonpassthru) command */

  mbox->cmd = data[0];
  mbox->channel = data[1];
  mbox->param = data[2];
  mbox->pad[0] = data[3];
  mbox->logdrv = data[4];

  mbox->numsgelements = build_sglist (megaCfg, pScb,
				      (u_long *) & mbox->xferaddr,
				      (u_long *) & seg);

  for (i=0;i<(SCpnt->request_bufflen-6);i++) {
     data[i] = data[i+6];
  }

  return (pScb);
}

#if DEBUG
static void showMbox(mega_scb *pScb)
{
  mega_mailbox *mbox;

  if (pScb == NULL) return;

  mbox = (mega_mailbox *)pScb->mboxData;
  printk("%u cmd:%x id:%x #scts:%x lba:%x addr:%x logdrv:%x #sg:%x\n",
	 pScb->SCpnt->pid, 
	 mbox->cmd, mbox->cmdid, mbox->numsectors,
	 mbox->lba, mbox->xferaddr, mbox->logdrv,
	 mbox->numsgelements);
}
#endif

/*--------------------------------------------------------------------
 * Interrupt service routine
 *--------------------------------------------------------------------*/
static void megaraid_isr (int irq, void *devp, struct pt_regs *regs)
{
  mega_host_config    *megaCfg;
  u_char byte, idx, sIdx, tmpBox[MAILBOX_SIZE];
  u_long dword;
  mega_mailbox *mbox;
  mega_scb *pScb;
  long flags;
  int qCnt, qStatus;

  megaCfg = (mega_host_config *) devp;
  mbox = (mega_mailbox *)tmpBox;

#if LINUX_VERSION_CODE >= 0x20100
  spin_lock_irqsave (&io_request_lock, flags);
#endif

  while (megaCfg->host->irq == irq) {

    spin_lock_irqsave (&mega_lock, flags);

    if (megaCfg->flag & IN_ISR) {
      TRACE (("ISR called reentrantly!!\n"));
    }

    megaCfg->flag |= IN_ISR;

    if (mega_busyWaitMbox(megaCfg)) {
	printk(KERN_WARNING "Error: mailbox busy in isr!\n");
    }


    /* Check if a valid interrupt is pending */
    if (megaCfg->flag & BOARD_QUARTZ) {
      dword = RDOUTDOOR (megaCfg);
      if (dword != 0x10001234) {
	/* Spurious interrupt */
	megaCfg->flag &= ~IN_ISR;
	spin_unlock_irqrestore (&mega_lock, flags);
	break;
      }
      WROUTDOOR (megaCfg, dword);

      /* Copy to temp location */
      memcpy(tmpBox, (mega_mailbox *)megaCfg->mbox, MAILBOX_SIZE);

      /* Acknowledge interrupt */
      WRINDOOR (megaCfg, virt_to_bus (megaCfg->mbox) | 0x2);
      while (RDINDOOR (megaCfg) & 0x02);
    }
    else {
      byte = READ_PORT (megaCfg->host->io_port, INTR_PORT);
      if ((byte & VALID_INTR_BYTE) == 0) {
	/* Spurious interrupt */
	megaCfg->flag &= ~IN_ISR;
	spin_unlock_irqrestore (&mega_lock, flags);
	break;
      }
      WRITE_PORT (megaCfg->host->io_port, INTR_PORT, byte);

      /* Copy to temp location */
      memcpy(tmpBox, (mega_mailbox *)megaCfg->mbox, MAILBOX_SIZE);

      /* Acknowledge interrupt */
      CLEAR_INTR (megaCfg->host->io_port);
    }

    qCnt = mbox->numstatus;
    qStatus = mbox->status;

    for (idx = 0; idx < qCnt; idx++) {
      sIdx = mbox->completed[idx];
      if (sIdx > 0) {
	pScb = &megaCfg->scbList[sIdx - 1];

	/* ASSERT(pScb->state == SCB_ISSUED); */

#if DEBUG
	if (((jiffies) - pScb->isrcount) > maxCmdTime) {
	  maxCmdTime = (jiffies) - pScb->isrcount;
	  printk("cmd time = %u\n", maxCmdTime);
	}
#endif

	if (pScb->state == SCB_ABORTED) {
	  printk("Received aborted SCB! %u\n", (int)((jiffies)-pScb->isrcount));
	}

	/* Mark command as completed */
	mega_cmd_done(megaCfg, pScb, qStatus);
      }

    }
    spin_unlock_irqrestore (&mega_lock, flags);

    megaCfg->flag &= ~IN_ISR;

    mega_rundoneq();

    /* Loop through any pending requests */
    spin_lock_irqsave(&mega_lock, flags);
    mega_runpendq(megaCfg);
    spin_unlock_irqrestore(&mega_lock,flags);
  }

#if LINUX_VERSION_CODE >= 0x20100
  spin_unlock_irqrestore (&io_request_lock, flags);
#endif
}

/*==================================================*/
/* Wait until the controller's mailbox is available */
/*==================================================*/
static int mega_busyWaitMbox (mega_host_config * megaCfg)
{
  mega_mailbox *mbox = (mega_mailbox *) megaCfg->mbox;
  long counter;

  for (counter = 0; counter < 10000; counter++) {
    if (!mbox->busy) {
      return 0;
    }
    udelay (100);
    barrier();
  }
  return -1;			/* give up after 1 second */
}

/*=====================================================
 * Post a command to the card
 *
 * Arguments:
 *   mega_host_config *megaCfg - Controller structure
 *   u_char *mboxData - Mailbox area, 16 bytes
 *   mega_scb *pScb   - SCB posting (or NULL if N/A)
 *   int intr         - if 1, interrupt, 0 is blocking
 *=====================================================
 */
static int megaIssueCmd (mega_host_config * megaCfg,
	      u_char * mboxData,
	      mega_scb * pScb,
	      int intr)
{
  mega_mailbox *mbox = (mega_mailbox *) megaCfg->mbox;
  u_char byte;
  u_long cmdDone;
  Scsi_Cmnd *SCpnt;
  
  mboxData[0x1] = (pScb ? pScb->idx + 1: 0x0);   /* Set cmdid */
  mboxData[0xF] = 1;		/* Set busy */

#if 0
  if (intr && mbox->busy) {
    return 0;
  }
#endif

  /* Wait until mailbox is free */
  while (mega_busyWaitMbox (megaCfg)) {
    printk("Blocked mailbox!!\n");
    udelay(1000);

#if DEBUG
    showMbox(pLastScb);
#endif
    
    /* Abort command */
    if (pScb == NULL) {
	printk("NULL pScb in megaIssue\n");
	TRACE(("NULL pScb in megaIssue\n"));
    }
    SCpnt = pScb->SCpnt;
    freeSCB(megaCfg, pScb);

    SCpnt->result = (DID_ABORT << 16);
    callDone(SCpnt);
    return 0;
  }
  pLastScb = pScb;

  
  /* Copy mailbox data into host structure */
  memcpy (mbox, mboxData, 16);

  /* Kick IO */
  if (intr) {

    /* Issue interrupt (non-blocking) command */
    if (megaCfg->flag & BOARD_QUARTZ) {
      mbox->mraid_poll = 0;
      mbox->mraid_ack = 0;
      WRINDOOR (megaCfg, virt_to_bus (megaCfg->mbox) | 0x1);
    }
    else {
      ENABLE_INTR (megaCfg->host->io_port);
      ISSUE_COMMAND (megaCfg->host->io_port);
    }
    pScb->state = SCB_ISSUED;
  }
  else {			/* Issue non-ISR (blocking) command */
    disable_irq(megaCfg->host->irq);
    if (megaCfg->flag & BOARD_QUARTZ) {
      mbox->mraid_poll = 0;
      mbox->mraid_ack = 0;
      WRINDOOR (megaCfg, virt_to_bus (megaCfg->mbox) | 0x1);

      while ((cmdDone = RDOUTDOOR (megaCfg)) != 0x10001234);
      WROUTDOOR (megaCfg, cmdDone);

      if (pScb) {
	mega_cmd_done (megaCfg, pScb, mbox->status);
	mega_rundoneq ();
      }

      WRINDOOR (megaCfg, virt_to_bus (megaCfg->mbox) | 0x2);
      while (RDINDOOR (megaCfg) & 0x2);

    }
    else {
      DISABLE_INTR (megaCfg->host->io_port);
      ISSUE_COMMAND (megaCfg->host->io_port);

      while (!((byte = READ_PORT (megaCfg->host->io_port, INTR_PORT)) & INTR_VALID));
      WRITE_PORT (megaCfg->host->io_port, INTR_PORT, byte);


      ENABLE_INTR (megaCfg->host->io_port);
      CLEAR_INTR (megaCfg->host->io_port);

      if (pScb) {
	mega_cmd_done (megaCfg, pScb, mbox->status);
	mega_rundoneq ();
      }
      else {
	TRACE (("Error: NULL pScb!\n"));
      }

    }
    enable_irq(megaCfg->host->irq);
  }
  while (mega_busyWaitMbox (megaCfg)) {
    printk("Blocked mailbox on exit!\n");
    udelay(1000);
  }

  return 0;
}

/*-------------------------------------------------------------------
 * Copies data to SGLIST
 *-------------------------------------------------------------------*/
static int build_sglist (mega_host_config * megaCfg, mega_scb * scb,
	      u_long * buffer, u_long * length)
{
  struct scatterlist *sgList;
  int idx;

  /* Scatter-gather not used */
  if (scb->SCpnt->use_sg == 0) {
    *buffer = virt_to_bus (scb->SCpnt->request_buffer);
    *length = (u_long) scb->SCpnt->request_bufflen;
    return 0;
  }

  sgList = (struct scatterlist *) scb->SCpnt->request_buffer;
  if (scb->SCpnt->use_sg == 1) {
    *buffer = virt_to_bus (sgList[0].address);
    *length = (u_long) sgList[0].length;
    return 0;
  }

  /* Copy Scatter-Gather list info into controller structure */
  for (idx = 0; idx < scb->SCpnt->use_sg; idx++) {
    scb->sgList[idx].address = virt_to_bus (sgList[idx].address);
    scb->sgList[idx].length = (u_long) sgList[idx].length;
  }

  /* Reset pointer and length fields */
  *buffer = virt_to_bus (scb->sgList);
  *length = 0;

  /* Return count of SG requests */
  return scb->SCpnt->use_sg;
}

/*--------------------------------------------------------------------
 * Initializes the adress of the controller's mailbox register
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
static int mega_register_mailbox (mega_host_config * megaCfg, u_long paddr)
{
  /* align on 16-byte boundry */
  megaCfg->mbox = &megaCfg->mailbox;
  megaCfg->mbox = (mega_mailbox *) ((((ulong) megaCfg->mbox) + 16) & 0xfffffff0);
  paddr = (paddr + 16) & 0xfffffff0;

  /* Register mailbox area with the firmware */
  if (megaCfg->flag & BOARD_QUARTZ) {
  }
  else {
    WRITE_PORT (megaCfg->host->io_port, MBOX_PORT0, paddr & 0xFF);
    WRITE_PORT (megaCfg->host->io_port, MBOX_PORT1, (paddr >> 8) & 0xFF);
    WRITE_PORT (megaCfg->host->io_port, MBOX_PORT2, (paddr >> 16) & 0xFF);
    WRITE_PORT (megaCfg->host->io_port, MBOX_PORT3, (paddr >> 24) & 0xFF);
    WRITE_PORT (megaCfg->host->io_port, ENABLE_MBOX_REGION, ENABLE_MBOX_BYTE);

    CLEAR_INTR (megaCfg->host->io_port);
    ENABLE_INTR (megaCfg->host->io_port);
  }
  return 0;
}

/*-------------------------------------------------------------------
 * Issue an adapter info query to the controller
 *-------------------------------------------------------------------*/
static int mega_i_query_adapter (mega_host_config * megaCfg)
{
  mega_RAIDINQ *adapterInfo;
  mega_mailbox *mbox;
  u_char mboxData[16];
  u_long paddr;

  spin_lock_init (&mega_lock);

  /* Initialize adapter inquiry */
  paddr = virt_to_bus (megaCfg->mega_buffer);
  mbox = (mega_mailbox *) mboxData;

  memset ((void *) megaCfg->mega_buffer, 0, sizeof (megaCfg->mega_buffer));
  memset (mbox, 0, 16);

  /* Initialize mailbox registers */
  mbox->cmd = MEGA_MBOXCMD_ADAPTERINQ;
  mbox->xferaddr = paddr;

  /* Issue a blocking command to the card */
  megaIssueCmd (megaCfg, mboxData, NULL, 0);

  /* Initialize host/local structures with Adapter info */
  adapterInfo = (mega_RAIDINQ *) megaCfg->mega_buffer;
  megaCfg->host->max_channel = adapterInfo->AdpInfo.ChanPresent;
/*  megaCfg->host->max_id = adapterInfo->AdpInfo.MaxTargPerChan; */
  megaCfg->host->max_id = 16; /* max targets/chan */
  megaCfg->numldrv = adapterInfo->LogdrvInfo.NumLDrv;

#if 0
  printk ("KERN_DEBUG ---- Logical drive info ----\n");
  for (i = 0; i < megaCfg->numldrv; i++) {
    printk ("%d: size: %ld prop: %x state: %x\n", i,
	    adapterInfo->LogdrvInfo.LDrvSize[i],
	    adapterInfo->LogdrvInfo.LDrvProp[i],
	    adapterInfo->LogdrvInfo.LDrvState[i]);
  }
  printk (KERN_DEBUG "---- Physical drive info ----\n");
  for (i = 0; i < MAX_PHYSICAL_DRIVES; i++) {
    if (i && !(i % 8))
      printk ("\n");
    printk ("%d: %x   ", i, adapterInfo->PhysdrvInfo.PDrvState[i]);
  }
  printk ("\n");
#endif

  megaCfg->max_cmds = adapterInfo->AdpInfo.MaxConcCmds;

#ifdef HP			/* use HP firmware and bios version encoding */
  sprintf (megaCfg->fwVer, "%c%d%d.%d%d",
	   adapterInfo->AdpInfo.FwVer[2],
	   adapterInfo->AdpInfo.FwVer[1] >> 8,
	   adapterInfo->AdpInfo.FwVer[1] & 0x0f,
	   adapterInfo->AdpInfo.FwVer[2] >> 8,
	   adapterInfo->AdpInfo.FwVer[2] & 0x0f);
  sprintf (megaCfg->biosVer, "%c%d%d.%d%d",
	   adapterInfo->AdpInfo.BiosVer[2],
	   adapterInfo->AdpInfo.BiosVer[1] >> 8,
	   adapterInfo->AdpInfo.BiosVer[1] & 0x0f,
	   adapterInfo->AdpInfo.BiosVer[2] >> 8,
	   adapterInfo->AdpInfo.BiosVer[2] & 0x0f);
#else
  memcpy (megaCfg->fwVer, adapterInfo->AdpInfo.FwVer, 4);
  megaCfg->fwVer[4] = 0;

  memcpy (megaCfg->biosVer, adapterInfo->AdpInfo.BiosVer, 4);
  megaCfg->biosVer[4] = 0;
#endif

  printk (KERN_INFO "megaraid: [%s:%s] detected %d logical drives" CRLFSTR,
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
int megaraid_proc_info (char *buffer, char **start, off_t offset,
		    int length, int host_no, int inout)
{
  *start = buffer;
  return 0;
}

int findCard (Scsi_Host_Template * pHostTmpl,
	  u_short pciVendor, u_short pciDev,
	  long flag)
{
  mega_host_config *megaCfg;
  struct Scsi_Host *host;
  u_char pciBus, pciDevFun, megaIrq;
  u_long megaBase;
  u_short jdx,pciIdx = 0;
  u_short numFound = 0;

#if LINUX_VERSION_CODE < 0x20100
  while (!pcibios_find_device (pciVendor, pciDev, pciIdx, &pciBus, &pciDevFun)) {

#if 0
  } /* keep auto-indenters happy */
#endif
#else
  
  struct pci_dev *pdev = pci_devices;
  
  while ((pdev = pci_find_device (pciVendor, pciDev, pdev))) {
    pciBus = pdev->bus->number;
    pciDevFun = pdev->devfn;
#endif
    if (flag & BOARD_QUARTZ) {
      u_short magic;
      pcibios_read_config_word (pciBus, pciDevFun,
				PCI_CONF_AMISIG,
				&magic);
      if (magic != AMI_SIGNATURE) {
        pciIdx++;
	continue;		/* not an AMI board */
      }
    }
    printk (KERN_INFO "megaraid: found 0x%4.04x:0x%4.04x:idx %d:bus %d:slot %d:fun %d\n",
	    pciVendor,
	    pciDev,
	    pciIdx, pciBus,
	    PCI_SLOT (pciDevFun),
	    PCI_FUNC (pciDevFun));

    /* Read the base port and IRQ from PCI */
#if LINUX_VERSION_CODE < 0x20100
    pcibios_read_config_dword (pciBus, pciDevFun,
			       PCI_BASE_ADDRESS_0,
			       (u_int *) & megaBase);
    pcibios_read_config_byte (pciBus, pciDevFun,
			      PCI_INTERRUPT_LINE,
			      &megaIrq);
#else
    megaBase = pdev->base_address[0];
    megaIrq  = pdev->irq;
#endif
    pciIdx++;

    if (flag & BOARD_QUARTZ) {
      megaBase &= PCI_BASE_ADDRESS_MEM_MASK;
      megaBase = (long) ioremap (megaBase, 128);
    }
    else {
      megaBase &= PCI_BASE_ADDRESS_IO_MASK;
      megaBase += 0x10;
    }

    /* Initialize SCSI Host structure */
    host = scsi_register (pHostTmpl, sizeof (mega_host_config));
    megaCfg = (mega_host_config *) host->hostdata;
    memset (megaCfg, 0, sizeof (mega_host_config));

    printk (" scsi%d: Found a MegaRAID controller at 0x%x, IRQ: %d" CRLFSTR,
	    host->host_no, (u_int) megaBase, megaIrq);

    /* Copy resource info into structure */
    megaCfg->qPending = NULL;
    megaCfg->qFree    = NULL;
    megaCfg->flag = flag;
    megaCfg->host = host;
    megaCfg->base = megaBase;
    megaCfg->host->irq = megaIrq;
    megaCfg->host->io_port = megaBase;
    megaCfg->host->n_io_port = 16;
    megaCfg->host->unique_id = (pciBus << 8) | pciDevFun;
    megaCtlrs[numCtlrs++] = megaCfg;

    if (flag != BOARD_QUARTZ) {
      /* Request our IO Range */
      if (check_region (megaBase, 16)) {
	printk (KERN_WARNING "megaraid: Couldn't register I/O range!" CRLFSTR);
	scsi_unregister (host);
	continue;
      }
      request_region (megaBase, 16, "megaraid");
    }

    /* Request our IRQ */
    if (request_irq (megaIrq, megaraid_isr, SA_SHIRQ,
		     "megaraid", megaCfg)) {
      printk (KERN_WARNING "megaraid: Couldn't register IRQ %d!" CRLFSTR,
	      megaIrq);
      scsi_unregister (host);
      continue;
    }

    mega_register_mailbox (megaCfg, virt_to_bus ((void *) &megaCfg->mailbox));
    mega_i_query_adapter (megaCfg);
    
    for(jdx=0; jdx<MAX_LOGICAL_DRIVES; jdx++) {
      megaCfg->nReads[jdx] = 0;
      megaCfg->nWrites[jdx] = 0;
    }

    /* Initialize SCBs */
    if (initSCB (megaCfg)) {
      scsi_unregister (host);
      continue;
    }

    numFound++;
  }
  return numFound;
}

/*---------------------------------------------------------
 * Detects if a megaraid controller exists in this system
 *---------------------------------------------------------*/
int megaraid_detect (Scsi_Host_Template * pHostTmpl)
{
  int count = 0;

  pHostTmpl->proc_dir = &proc_scsi_megaraid;

#if LINUX_VERSION_CODE < 0x20100
  if (!pcibios_present ()) {
    printk (KERN_WARNING "megaraid: PCI bios not present." CRLFSTR);
    return 0;
  }
#endif
  skip_id = -1;
  if (megaraid && !strncmp(megaraid,"skip",strlen("skip"))) {
      if (megaraid[4] != '\0') {
          skip_id = megaraid[4] - '0';
          if (megaraid[5] != '\0') {
              skip_id = (skip_id * 10) + (megaraid[5] - '0');
          }
      }
      skip_id = (skip_id > 15) ? -1 : skip_id;
  }

  count += findCard (pHostTmpl, 0x101E, 0x9010, 0);
  count += findCard (pHostTmpl, 0x101E, 0x9060, 0);
  count += findCard (pHostTmpl, 0x8086, 0x1960, BOARD_QUARTZ);

  return count;
}

/*---------------------------------------------------------------------
 * Release the controller's resources
 *---------------------------------------------------------------------*/
int megaraid_release (struct Scsi_Host *pSHost)
{
  mega_host_config *megaCfg;
  mega_mailbox *mbox;
  u_char mboxData[16];

  megaCfg = (mega_host_config *) pSHost->hostdata;
  mbox = (mega_mailbox *) mboxData;

  /* Flush cache to disk */
  memset (mbox, 0, 16);
  mboxData[0] = 0xA;

  free_irq (megaCfg->host->irq, megaCfg);/* Must be freed first, otherwise
					   extra interrupt is generated */

  /* Issue a blocking (interrupts disabled) command to the card */
  megaIssueCmd (megaCfg, mboxData, NULL, 0);

  /* Free our resources */
  if (megaCfg->flag & BOARD_QUARTZ) {
    iounmap ((void *) megaCfg->base);
  }
  else {
    release_region (megaCfg->host->io_port, 16);
  }

  freeSgList(megaCfg);
  scsi_unregister (pSHost);

  return 0;
}

static inline void freeSgList(mega_host_config *megaCfg)
{
  int i;

  for (i = 0; i < megaCfg->max_cmds; i++) {
    if (megaCfg->scbList[i].sgList)
      kfree (megaCfg->scbList[i].sgList);	/* free sgList */
  }
}

/*----------------------------------------------
 * Get information about the card/driver 
 *----------------------------------------------*/
const char * megaraid_info (struct Scsi_Host *pSHost)
{
  static char buffer[512];
  mega_host_config *megaCfg;
  mega_RAIDINQ *adapterInfo;

  megaCfg = (mega_host_config *) pSHost->hostdata;
  adapterInfo = (mega_RAIDINQ *) megaCfg->mega_buffer;

  sprintf (buffer, "AMI MegaRAID %s %d commands %d targs %d chans",
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
int megaraid_queue (Scsi_Cmnd * SCpnt, void (*pktComp) (Scsi_Cmnd *))
{
  mega_host_config *megaCfg;
  mega_scb *pScb;
  long flags;

  spin_lock_irqsave(&mega_lock,flags);

  megaCfg = (mega_host_config *) SCpnt->host->hostdata;

  if (!(megaCfg->flag & (1L << SCpnt->channel))) {
    printk (KERN_INFO "scsi%d: scanning channel %c for devices.\n",
	    megaCfg->host->host_no,
	    SCpnt->channel + 'A');
    megaCfg->flag |= (1L << SCpnt->channel);
  }

  SCpnt->scsi_done = pktComp;

  /* If driver in abort or reset.. cancel this command */
  if (megaCfg->flag & IN_ABORT) {
    SCpnt->result = (DID_ABORT << 16);
    ENQUEUE_NL(SCpnt, Scsi_Cmnd, qCompleted, host_scribble);

    spin_unlock_irqrestore(&mega_lock,flags);
    return 0;
  }
  else if (megaCfg->flag & IN_RESET) {
    SCpnt->result = (DID_RESET << 16);
    ENQUEUE_NL(SCpnt, Scsi_Cmnd, qCompleted, host_scribble);

    spin_unlock_irqrestore(&mega_lock,flags);
    return 0;
  }

  /* Allocate and build a SCB request */
  if ((pScb = mega_build_cmd (megaCfg, SCpnt)) != NULL) {
    /* Add SCB to the head of the pending queue */
    ENQUEUE_NL (pScb, mega_scb, megaCfg->qPending, next);

    /* Issue any pending command to the card if not in ISR */
    if (!(megaCfg->flag & IN_ISR)) {
      mega_runpendq(megaCfg);
    }
    else {
      printk("IRQ pend...\n");
    }
  }

  spin_unlock_irqrestore(&mega_lock,flags);

  return 0;
}

/*----------------------------------------------------------------------
 * Issue a blocking command to the controller
 *----------------------------------------------------------------------*/
volatile static int internal_done_flag = 0;
volatile static int internal_done_errcode = 0;
static struct wait_queue *internal_wait = NULL;

static void internal_done (Scsi_Cmnd * SCpnt)
{
  internal_done_errcode = SCpnt->result;
  internal_done_flag++;
  wake_up(&internal_wait);
}

/* shouldn't be used, but included for completeness */

int megaraid_command (Scsi_Cmnd * SCpnt)
{
  internal_done_flag = 0;

  /* Queue command, and wait until it has completed */
  megaraid_queue (SCpnt, internal_done);

  while (!internal_done_flag) {
	interruptible_sleep_on(&internal_wait);
  }

  return internal_done_errcode;
}

/*---------------------------------------------------------------------
 * Abort a previous SCSI request
 *---------------------------------------------------------------------*/
int megaraid_abort (Scsi_Cmnd * SCpnt)
{
  mega_host_config *megaCfg;
  int   rc, idx;
  long  flags;
  mega_scb *pScb;

  rc = SCSI_ABORT_SUCCESS;

  spin_lock_irqsave (&mega_lock, flags);

  megaCfg = (mega_host_config *) SCpnt->host->hostdata;

  megaCfg->flag |= IN_ABORT;

  for(pScb=megaCfg->qPending; pScb; pScb=pScb->next) {
    if (pScb->SCpnt == SCpnt) {
      /* Found an aborting command */
#if DEBUG
      showMbox(pScb);
#endif

      printk("Abort: %d %u\n", 
	     SCpnt->timeout_per_command,
	     (uint)((jiffies) - pScb->isrcount));

      switch(pScb->state) {
      case SCB_ABORTED: /* Already aborted */
	rc = SCSI_ABORT_SNOOZE;
	break;
      case SCB_ISSUED: /* Waiting on ISR result */
	rc = SCSI_ABORT_PENDING;
	pScb->state = SCB_ABORTED;
	break;
      }
    }
  }

#if 0
  TRACE (("ABORT!!! %.08lx %.02x <%d.%d.%d>\n",
	  SCpnt->serial_number, SCpnt->cmnd[0], SCpnt->channel, SCpnt->target,
	  SCpnt->lun));
  for(pScb=megaCfg->qPending; pScb; pScb=pScb->next) {
    if (pScb->SCpnt == SCpnt) { 
      ser_printk("** %d<%x>  %c\n", pScb->SCpnt->pid, pScb->idx+1,
		 pScb->state == SCB_ACTIVE ? 'A' : 'I');
#if DEBUG
      showMbox(pScb);
#endif
    }
  }
#endif

  /*
   * Walk list of SCBs for any that are still outstanding
   */
  for (idx = 0; idx < megaCfg->max_cmds; idx++) {
    if (megaCfg->scbList[idx].state != SCB_FREE) {
      if (megaCfg->scbList[idx].SCpnt == SCpnt) {
	freeSCB (megaCfg, &megaCfg->scbList[idx]);

	SCpnt->result = (DID_ABORT << 16) | (SUGGEST_RETRY << 24);
	ENQUEUE_NL(SCpnt, Scsi_Cmnd, qCompleted, host_scribble);
      }
    }
  }
  
  megaCfg->flag &= ~IN_ABORT;

  spin_unlock_irqrestore (&mega_lock, flags);

  mega_rundoneq();

  return rc;
}

/*---------------------------------------------------------------------
 * Reset a previous SCSI request
 *---------------------------------------------------------------------*/
int megaraid_reset (Scsi_Cmnd * SCpnt, unsigned int rstflags)
{
  mega_host_config *megaCfg;
  int idx;
  long flags;

  spin_lock_irqsave (&mega_lock, flags);

  megaCfg = (mega_host_config *) SCpnt->host->hostdata;

  megaCfg->flag |= IN_RESET;

  TRACE (("RESET: %.08lx %.02x <%d.%d.%d>\n",
	SCpnt->serial_number, SCpnt->cmnd[0], SCpnt->channel, SCpnt->target,
	  SCpnt->lun));

  /*
   * Walk list of SCBs for any that are still outstanding
   */
  for (idx = 0; idx < megaCfg->max_cmds; idx++) {
    if (megaCfg->scbList[idx].state != SCB_FREE) {
      SCpnt = megaCfg->scbList[idx].SCpnt;
      if (SCpnt != NULL) {
	freeSCB (megaCfg, &megaCfg->scbList[idx]);
	SCpnt->result = (DID_RESET << 16) | (SUGGEST_RETRY << 24);
	ENQUEUE_NL(SCpnt, Scsi_Cmnd, qCompleted, host_scribble);
      }
    }
  }

  megaCfg->flag &= ~IN_RESET;

  spin_unlock_irqrestore (&mega_lock, flags);

  mega_rundoneq();
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
int megaraid_biosparam (Disk * disk, kdev_t dev, int *geom)
{
  int heads, sectors, cylinders;
  mega_host_config *megaCfg;

  /* Get pointer to host config structure */
  megaCfg = (mega_host_config *) disk->device->host->hostdata;

  /* Default heads (64) & sectors (32) */
  heads = 64;
  sectors = 32;
  cylinders = disk->capacity / (heads * sectors);

  /* Handle extended translation size for logical drives > 1Gb */
  if (disk->capacity >= 0x200000) {
    heads = 255;
    sectors = 63;
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
