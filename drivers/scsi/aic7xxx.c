/*+M*************************************************************************
 * Adaptec AIC7xxx device driver for Linux.
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Sources include the Adaptec 1740 driver (aha1740.c), the Ultrastor 24F
 * driver (ultrastor.c), various Linux kernel source, the Adaptec EISA
 * config file (!adp7771.cfg), the Adaptec AHA-2740A Series User's Guide,
 * the Linux Kernel Hacker's Guide, Writing a SCSI Device Driver for Linux,
 * the Adaptec 1542 driver (aha1542.c), the Adaptec EISA overlay file
 * (adp7770.ovl), the Adaptec AHA-2740 Series Technical Reference Manual,
 * the Adaptec AIC-7770 Data Book, the ANSI SCSI specification, the
 * ANSI SCSI-2 specification (draft 10c), ...
 *
 * ----------------------------------------------------------------
 *  Modified to include support for wide and twin bus adapters,
 *  DMAing of SCBs, tagged queueing, IRQ sharing, bug fixes,
 *  SCB paging, and other rework of the code.
 *
 *  Parts of this driver are based on the FreeBSD driver by Justin
 *  T. Gibbs.
 *
 *  A Boot time option was also added for not resetting the scsi bus.
 *
 *    Form:  aic7xxx=extended,no_reset
 *
 *    -- Daniel M. Eischen, deischen@iworks.InterWorks.org, 07/07/96
 *
 *  $Id: aic7xxx.c,v 4.0 1996/10/13 08:23:42 deang Exp $
 *-M*************************************************************************/

#ifdef MODULE
#include <linux/module.h>
#endif

#include <stdarg.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/bios32.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "aic7xxx.h"
#include "aic7xxx_reg.h"
#include <linux/stat.h>
#include <linux/malloc.h>	/* for kmalloc() */

#include <linux/config.h>	/* for CONFIG_PCI */

/*
 * To generate the correct addresses for the controller to issue
 * on the bus.  Originally added for DEC Alpha support.
 */
#define VIRT_TO_BUS(a) (unsigned int)virt_to_bus((void *)(a))

struct proc_dir_entry proc_scsi_aic7xxx = {
    PROC_SCSI_AIC7XXX, 7, "aic7xxx",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

#define AIC7XXX_C_VERSION  "$Revision: 4.0 $"

#define NUMBER(arr)     (sizeof(arr) / sizeof(arr[0]))
#define MIN(a,b)        ((a < b) ? a : b)
#define ALL_TARGETS -1
#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

/*
 * Defines for PCI bus support, testing twin bus support, DMAing of
 * SCBs, tagged queueing, commands (SCBs) per lun, and SCSI bus reset
 * delay time.
 *
 *   o PCI bus support - this has been implemented and working since
 *     the December 1, 1994 release of this driver. If you don't have
 *     a PCI bus, then you can configure your kernel without PCI
 *     support because all PCI dependent code is bracketed with
 *     "#ifdef CONFIG_PCI ... #endif CONFIG_PCI".
 *
 *   o Twin bus support - this has been tested and does work.
 *
 *   o DMAing of SCBs - thanks to Kai Makisara, this now works.
 *     This define is now taken out and DMAing of SCBs is always
 *     performed (8/12/95 - DE).
 *
 *   o Tagged queueing - this driver is capable of tagged queueing
 *     but I am unsure as to how well the higher level driver implements
 *     tagged queueing. Therefore, the maximum commands per lun is
 *     set to 2. If you want to implement tagged queueing, ensure
 *     this define is not commented out.
 *
 *   o Commands per lun - If tagged queueing is enabled, then you
 *     may want to try increasing AIC7XXX_CMDS_PER_LUN to more
 *     than 2.  By default, we limit the SCBs per LUN to 2 with
 *     or without tagged queueing enabled.  If tagged queueing is
 *     disabled, the sequencer will keep the 2nd SCB in the input
 *     queue until the first one completes - so it is OK to to have
 *     more than 1 SCB queued.  If tagged queueing is enabled, then
 *     the sequencer will attempt to send the 2nd SCB to the device
 *     while the first SCB is executing and the device is disconnected.
 *     For adapters limited to 4 SCBs, you may want to actually
 *     decrease the commands per LUN to 1, if you often have more
 *     than 2 devices active at the same time.  This will allocate
 *     1 SCB for each device and ensure that there will always be
 *     a free SCB for up to 4 devices active at the same time.
 *     When SCB paging is enabled, set the commands per LUN to 8
 *     or higher (see SCB paging support below).  Note that if
 *     AIC7XXX_CMDS_PER_LUN is not defined and tagged queueing is
 *     enabled, the driver will attempt to set the commands per
 *     LUN using its own heuristic based on the number of available
 *     SCBs.
 *
 *   o 3985 support - The 3985 adapter is much like the 3940, but
 *     has three 7870 controllers as opposed to two for the 3940.
 *     It will get probed and recognized as three different adapters,
 *     but all three controllers can share the same external bank of
 *     255 SCBs.  If you enable AIC7XXX_SHARE_SCBS, then the driver
 *     will attempt to share the common bank of SCBs between the three
 *     controllers of the 3985.  This is experimental and hasn't
 *     been tested.  By default, we do not share the bank of SCBs,
 *     and force the controllers to use their own internal bank of
 *     16 SCBs.  Please let us know if sharing the SCB array works.
 *
 *   o SCB paging support - SCB paging is enabled by defining
 *     AIC7XXX_PAGE_ENABLE.  Support for this was taken from the
 *     FreeBSD driver (by Justin Gibbs) and allows for up to 255
 *     active SCBs.  This will increase performance when tagged
 *     queueing is enabled.  Note that you should increase the
 *     AIC7XXX_CMDS_PER_LUN to 8 as most tagged queueing devices
 *     allow at least this many.
 *
 *  Note that sharing of IRQs is not an option any longer.  Linux supports
 *  it so we support it.
 *
 *  Daniel M. Eischen, deischen@iworks.InterWorks.org, 06/30/96
 */

/* Uncomment this for testing twin bus support. */
#define AIC7XXX_TWIN_SUPPORT

/* Uncomment this for tagged queueing. */
/* #define AIC7XXX_TAGGED_QUEUEING */

/*
 * You can try raising me if tagged queueing is enabled, or lowering
 * me if you only have 4 SCBs.
 */
/* #define AIC7XXX_CMDS_PER_LUN 8 */

/* Set this to the delay in seconds after SCSI bus reset. */
#define AIC7XXX_RESET_DELAY 15

/*
 * Uncomment the following define for collection of SCSI transfer statistics
 * for the /proc filesystem.
 *
 * NOTE: Do NOT enable this when running on kernels version 1.2.x and below.
 * NOTE: This does affect performance since it has to maintain statistics.
 */
/* #define AIC7XXX_PROC_STATS */

/*
 * Uncomment the following to enable SCB paging.
 */
/* #define AIC7XXX_PAGE_ENABLE */

/*
 * Uncomment the following to enable sharing of the external bank
 * of 255 SCBs for the 3985.
 */
#define AIC7XXX_SHARE_SCBS

/*
 * For debugging the abort/reset code.
 */
#define AIC7XXX_DEBUG_ABORT

/*
 * For general debug messages
 */
#define AIC7XXX_DEBUG

/*
 * Controller type and options
 */
typedef enum {
  AIC_NONE,
  AIC_7770,	/* EISA aic7770 on motherboard */
  AIC_7771,	/* EISA aic7771 on 274x */
  AIC_284x,	/* VLB  aic7770 on 284x, BIOS disabled */
  AIC_7850,	/* PCI  aic7850 */
  AIC_7855,	/* PCI  aic7855 */
  AIC_7860,	/* PCI  aic7860 (7850 Ultra) */
  AIC_7861,     /* PCI  aic7861 on 2940AU */
  AIC_7870,	/* PCI  aic7870 on motherboard */
  AIC_7871,	/* PCI  aic7871 on 294x */
  AIC_7872,	/* PCI  aic7872 on 3940 */
  AIC_7873,	/* PCI  aic7873 on 3985 */
  AIC_7874,	/* PCI  aic7874 on 294x Differential */
  AIC_7880,	/* PCI  aic7880 on motherboard */
  AIC_7881,	/* PCI  aic7881 on 294x Ultra */
  AIC_7882,	/* PCI  aic7882 on 3940 Ultra */
  AIC_7883,	/* PCI  aic7883 on 3985 Ultra */
  AIC_7884	/* PCI  aic7884 on 294x Ultra Differential */
} aha_type;

typedef enum {
  AIC_777x,	/* AIC-7770 based */
  AIC_785x,	/* AIC-7850 based */
  AIC_787x,	/* AIC-7870 based */
  AIC_788x	/* AIC-7880 based */
} aha_chip_type;

typedef enum {
  AIC_SINGLE,  /* Single Channel */
  AIC_TWIN,    /* Twin Channel */
  AIC_WIDE     /* Wide Channel */
} aha_bus_type;

typedef enum {
  AIC_UNKNOWN,
  AIC_ENABLED,
  AIC_DISABLED
} aha_status_type;

typedef enum {
  LIST_HEAD,
  LIST_SECOND
} insert_type;

typedef enum {
  ABORT_RESET_INACTIVE,
  ABORT_RESET_PENDING,
  ABORT_RESET_SUCCESS
} aha_abort_reset_type;

/*
 * Define an array of board names that can be indexed by aha_type.
 * Don't forget to change this when changing the types!
 */
static const char *board_names[] = {
  "<AIC-7xxx Unknown>",		/* AIC_NONE */
  "AIC-7770",			/* AIC_7770 */
  "AHA-2740",			/* AIC_7771 */
  "AHA-2840",			/* AIC_284x */
  "AIC-7850",			/* AIC_7850 */
  "AIC-7855",			/* AIC_7855 */
  "AIC-7850 Ultra",		/* AIC_7860 */
  "AHA-2940A Ultra",		/* AIC_7861 */
  "AIC-7870",			/* AIC_7870 */
  "AHA-2940",			/* AIC_7871 */
  "AHA-3940",			/* AIC_7872 */
  "AHA-3985",			/* AIC_7873 */
  "AHA-2940 Differential",	/* AIC_7874 */
  "AIC-7880 Ultra",		/* AIC_7880 */
  "AHA-2940 Ultra",		/* AIC_7881 */
  "AHA-3940 Ultra",		/* AIC_7882 */
  "AHA-3985 Ultra",		/* AIC_7883 */
  "AHA-2940 Ultra Differential"	/* AIC_7884 */
};

/*
 * There should be a specific return value for this in scsi.h, but
 * it seems that most drivers ignore it.
 */
#define DID_UNDERFLOW   DID_ERROR

/*
 *  What we want to do is have the higher level scsi driver requeue
 *  the command to us. There is no specific driver status for this
 *  condition, but the higher level scsi driver will requeue the
 *  command on a DID_BUS_BUSY error.
 *
 *  Upon further inspection and testing, it seems that DID_BUS_BUSY
 *  will *always* retry the command.  We can get into an infinite loop
 *  if this happens when we really want some sort of counter that
 *  will automatically abort/reset the command after so many retries.
 *  Using DID_ERROR will do just that.  (Made by a suggestion by
 *  Doug Ledford 8/1/96)
 */
#define DID_RETRY_COMMAND DID_ERROR

/*
 * EISA/VL-bus stuff
 */
#define MINSLOT		1
#define MAXSLOT		15
#define SLOTBASE(x)	((x) << 12)

/*
 * Standard EISA Host ID regs  (Offset from slot base)
 */
#define HID0		0x80   /* 0,1: msb of ID2, 2-7: ID1      */
#define HID1		0x81   /* 0-4: ID3, 5-7: LSB ID2         */
#define HID2		0x82   /* product                        */
#define HID3		0x83   /* firmware revision              */

/*
 * AIC-7770 I/O range to reserve for a card
 */
#define MINREG		0xC00
#define MAXREG		0xCBF

#define INTDEF		0x5C		/* Interrupt Definition Register */

/*
 * Some defines for the HCNTRL register.
 */
#define	REQ_PAUSE	IRQMS | INTEN | PAUSE
#define	UNPAUSE_274X	IRQMS | INTEN
#define	UNPAUSE_284X	INTEN
#define	UNPAUSE_294X	IRQMS | INTEN

/*
 * AIC-78X0 PCI registers
 */
#define	CLASS_PROGIF_REVID	0x08
#define		DEVREVID	0x000000FFul
#define		PROGINFC	0x0000FF00ul
#define		SUBCLASS	0x00FF0000ul
#define		BASECLASS	0xFF000000ul

#define	CSIZE_LATTIME		0x0C
#define		CACHESIZE	0x0000003Ful	/* only 5 bits */
#define		LATTIME		0x0000FF00ul

#define	DEVCONFIG		0x40
#define		MPORTMODE	0x00000400ul	/* aic7870 only */
#define		RAMPSM		0x00000200ul	/* aic7870 only */
#define		VOLSENSE	0x00000100ul
#define		SCBRAMSEL	0x00000080ul
#define		MRDCEN		0x00000040ul
#define		EXTSCBTIME	0x00000020ul	/* aic7870 only */
#define		EXTSCBPEN	0x00000010ul	/* aic7870 only */
#define		BERREN		0x00000008ul
#define		DACEN		0x00000004ul
#define		STPWLEVEL	0x00000002ul
#define		DIFACTNEGEN	0x00000001ul	/* aic7870 only */


/*
 * Define the different types of SEEPROMs on aic7xxx adapters
 * and make it also represent the address size used in accessing
 * its registers.  The 93C46 chips have 1024 bits organized into
 * 64 16-bit words, while the 93C56 chips have 2048 bits organized
 * into 128 16-bit words.  The C46 chips use 6 bits to address
 * each word, while the C56 and C66 (4096 bits) use 8 bits to
 * address each word.
 */
typedef enum {c46 = 6, c56_66 = 8} seeprom_chip_type;

/*
 *
 * Define the format of the SEEPROM registers (16 bits).
 *
 */
struct seeprom_config {

/*
 * SCSI ID Configuration Flags
 */
#define CFXFER		0x0007		/* synchronous transfer rate */
#define CFSYNCH		0x0008		/* enable synchronous transfer */
#define CFDISC		0x0010		/* enable disconnection */
#define CFWIDEB		0x0020		/* wide bus device (wide card) */
/* UNUSED		0x00C0 */
#define CFSTART		0x0100		/* send start unit SCSI command */
#define CFINCBIOS	0x0200		/* include in BIOS scan */
#define CFRNFOUND	0x0400		/* report even if not found */
/* UNUSED		0xF800 */
  unsigned short device_flags[16];	/* words 0-15 */

/*
 * BIOS Control Bits
 */
#define CFSUPREM	0x0001		/* support all removable drives */
#define CFSUPREMB	0x0002		/* support removable drives for boot only */
#define CFBIOSEN	0x0004		/* BIOS enabled */
/* UNUSED		0x0008 */
#define CFSM2DRV	0x0010		/* support more than two drives */
#define CF284XEXTEND	0x0020		/* extended translation (284x cards) */
/* UNUSED		0x0040 */
#define CFEXTEND	0x0080		/* extended translation enabled */
/* UNUSED		0xFF00 */
  unsigned short bios_control;		/* word 16 */

/*
 * Host Adapter Control Bits
 */
/* UNUSED               0x0001 */
#define CFULTRAEN       0x0002          /* Ultra SCSI speed enable (Ultra cards) */
#define CF284XSELTO     0x0003          /* Selection timeout (284x cards) */
#define CF284XFIFO      0x000C          /* FIFO Threshold (284x cards) */
#define CFSTERM         0x0004          /* SCSI low byte termination (non-wide cards) */
#define CFWSTERM        0x0008          /* SCSI high byte termination (wide card) */
#define CFSPARITY	0x0010		/* SCSI parity */
#define CF284XSTERM	0x0020		/* SCSI low byte termination (284x cards) */
#define CFRESETB	0x0040		/* reset SCSI bus at IC initialization */
/* UNUSED		0xFF80 */
  unsigned short adapter_control;	/* word 17 */

/*
 * Bus Release, Host Adapter ID
 */
#define CFSCSIID	0x000F		/* host adapter SCSI ID */
/* UNUSED		0x00F0 */
#define CFBRTIME	0xFF00		/* bus release time */
  unsigned short brtime_id;		/* word 18 */

/*
 * Maximum targets
 */
#define CFMAXTARG	0x00FF	/* maximum targets */
/* UNUSED		0xFF00 */
  unsigned short max_targets;		/* word 19 */

  unsigned short res_1[11];		/* words 20-30 */
  unsigned short checksum;		/* word 31 */
};


#define SCSI_RESET 0x040

/*
 * Pause the sequencer and wait for it to actually stop - this
 * is important since the sequencer can disable pausing for critical
 * sections.
 */
#define PAUSE_SEQUENCER(p) \
  outb(p->pause, HCNTRL + p->base);			\
  while ((inb(HCNTRL + p->base) & PAUSE) == 0)		\
    ;							\

/*
 * Unpause the sequencer. Unremarkable, yet done often enough to
 * warrant an easy way to do it.
 */
#define UNPAUSE_SEQUENCER(p) \
  outb(p->unpause, HCNTRL + p->base)

/*
 * Restart the sequencer program from address zero
 */
#define RESTART_SEQUENCER(p) \
  do {							\
    outb(SEQRESET | FASTMODE, SEQCTL + p->base);	\
  } while (inb(SEQADDR0 + p->base) != 0 &&		\
	   inb(SEQADDR1 + p->base) != 0);		\
  UNPAUSE_SEQUENCER(p);

/*
 * If an error occurs during a data transfer phase, run the command
 * to completion - it's easier that way - making a note of the error
 * condition in this location. This then will modify a DID_OK status
 * into an appropriate error for the higher-level SCSI code.
 */
#define aic7xxx_error(cmd)	((cmd)->SCp.Status)

/*
 * Keep track of the targets returned status.
 */
#define aic7xxx_status(cmd)	((cmd)->SCp.sent_command)

/*
 * The position of the SCSI commands scb within the scb array.
 */
#define aic7xxx_position(cmd)	((cmd)->SCp.have_data_in)

/*
 * "Static" structures. Note that these are NOT initialized
 * to zero inside the kernel - we have to initialize them all
 * explicitly.
 *
 * We support multiple adapter cards per interrupt, but keep a
 * linked list of Scsi_Host structures for each IRQ.  On an interrupt,
 * use the IRQ as an index into aic7xxx_boards[] to locate the card
 * information.
 */
static struct Scsi_Host *aic7xxx_boards[NR_IRQS + 1];

/*
 * When we detect and register the card, it is possible to
 * have the card raise a spurious interrupt.  Because we need
 * to support multiple cards, we cannot tell which card caused
 * the spurious interrupt.  And, we might not even have added
 * the card info to the linked list at the time the spurious
 * interrupt gets raised.  This variable is suppose to keep track
 * of when we are registering a card and how many spurious
 * interrupts we have encountered.
 *
 *   0 - do not allow spurious interrupts.
 *   1 - allow 1 spurious interrupt
 *   2 - have 1 spurious interrupt, do not allow any more.
 *
 * I've made it an integer instead of a boolean in case we
 * want to allow more than one spurious interrupt for debugging
 * purposes.  Otherwise, it could just go from true to false to
 * true (or something like that).
 *
 * When the driver detects the cards, we'll set the count to 1
 * for each card detection and registration.  After the registration
 * of a card completes, we'll set the count back to 0.  So far, it
 * seems to be enough to allow a spurious interrupt only during
 * card registration; if a spurious interrupt is going to occur,
 * this is where it happens.
 *
 * We should be able to find a way to avoid getting the spurious
 * interrupt.  But until we do, we have to keep this ugly code.
 */
static int aic7xxx_spurious_count;

/*
 * The driver keeps up to four scb structures per card in memory. Only the
 * first 25 bytes of the structure are valid for the hardware, the rest used
 * for driver level bookkeeping.
 */

/*
 * As of Linux 2.1, the mid-level SCSI code uses virtual addresses
 * in the scatter-gather lists.  We need to convert the virtual
 * addresses to physical addresses.
 */
struct hw_scatterlist {
  unsigned int address;
  unsigned int length;
};

/*
 * Maximum number of SG segments these cards can support.
 */
#define	MAX_SG 256

struct aic7xxx_scb {
/* ------------    Begin hardware supported fields    ---------------- */
/* 0*/  unsigned char control;
/* 1*/  unsigned char target_channel_lun;       /* 4/1/3 bits */
/* 2*/  unsigned char target_status;
/* 3*/  unsigned char SG_segment_count;
/* 4*/  unsigned char SG_list_pointer[4] __attribute__ ((packed));
/* 8*/  unsigned char residual_SG_segment_count;
/* 9*/  unsigned char residual_data_count[3] __attribute__ ((packed));
/*12*/  unsigned char data_pointer[4] __attribute__ ((packed));
/*16*/  unsigned int  data_count __attribute__ ((packed)); /* must be 32 bits */
/*20*/  unsigned char SCSI_cmd_pointer[4] __attribute__ ((packed));
/*24*/  unsigned char SCSI_cmd_length;
/*25*/	u_char tag;			/* Index into our kernel SCB array.
					 * Also used as the tag for tagged I/O
					 */
#define SCB_PIO_TRANSFER_SIZE	26 	/* amount we need to upload/download
					 * via PIO to initialize a transaction.
					 */
/*26*/  u_char next;                    /* Used to thread SCBs awaiting selection
                                         * or disconnected down in the sequencer.
                                         */
	/*-----------------end of hardware supported fields----------------*/
	Scsi_Cmnd          *cmd;	/* Scsi_Cmnd for this scb */
        struct aic7xxx_scb *q_next;     /* next scb in queue */
#define SCB_FREE               0x00
#define SCB_ACTIVE             0x01
#define SCB_ABORTED            0x02
#define SCB_DEVICE_RESET       0x04
#define SCB_IMMED              0x08
#define SCB_SENSE              0x10
#define SCB_QUEUED_FOR_DONE    0x40
#define SCB_PAGED_OUT          0x80
#define SCB_WAITINGQ           0x100
#define SCB_ASSIGNEDQ          0x200
#define SCB_SENTORDEREDTAG     0x400
#define SCB_IN_PROGRESS        (SCB_ACTIVE | SCB_PAGED_OUT | \
                                SCB_WAITINGQ | SCB_ASSIGNEDQ)
	int                 state;          /* current state of scb */
	unsigned int        position;       /* Position in scb array */
	struct hw_scatterlist  sg_list[MAX_SG]; /* SG list in adapter format */
	unsigned char       sense_cmd[6];   /* Allocate 6 characters for sense command */
};

/*
 * Define a linked list of SCBs.
 */
typedef struct {
  struct aic7xxx_scb *head;
  struct aic7xxx_scb *tail;
} scb_queue_type;

static struct {
  unsigned char errno;
  const char *errmesg;
} hard_error[] = {
  { ILLHADDR,  "Illegal Host Access" },
  { ILLSADDR,  "Illegal Sequencer Address referenced" },
  { ILLOPCODE, "Illegal Opcode in sequencer program" },
  { PARERR,    "Sequencer Ram Parity Error" }
};

static unsigned char
generic_sense[] = { REQUEST_SENSE, 0, 0, 0, 255, 0 };

typedef struct {
  scb_queue_type free_scbs;        /*
                                    * SCBs assigned to free slot on
                                    * card (no paging required)
                                    */
  int            numscbs;          /* current number of scbs */
  int            activescbs;       /* active scbs */
} scb_usage_type;

/*
 * The maximum number of SCBs we could have for ANY type
 * of card. DON'T FORGET TO CHANGE THE SCB MASK IN THE
 * SEQUENCER CODE IF THIS IS MODIFIED!
 */
#define AIC7XXX_MAXSCB	255

/*
 * Define a structure used for each host adapter, only one per IRQ.
 */
struct aic7xxx_host {
  struct Scsi_Host        *host;             /* pointer to scsi host */
  int                      host_no;          /* SCSI host number */
  int                      base;             /* card base address */
  int                      maxhscbs;         /* hardware SCBs */
  int                      maxscbs;          /* max SCBs (including pageable) */
#define A_SCANNED              0x0001
#define B_SCANNED              0x0002
#define EXTENDED_TRANSLATION   0x0004
#define HAVE_SEEPROM           0x0008
#define ULTRA_ENABLED          0x0010
#define PAGE_ENABLED           0x0020
#define IN_ISR                 0x0040
#define USE_DEFAULTS           0x0080
  unsigned int             flags;
  unsigned int             isr_count;        /* Interrupt count */
  unsigned short           needsdtr_copy;    /* default config */
  unsigned short           needsdtr;
  unsigned short           sdtr_pending;
  unsigned short           needwdtr_copy;    /* default config */
  unsigned short           needwdtr;
  unsigned short           wdtr_pending;
  unsigned short           orderedtag;
  unsigned short           discenable;	     /* Targets allowed to disconnect */
  aha_type                 type;             /* card type */
  aha_chip_type            chip_type;        /* chip base type */
  aha_bus_type             bus_type;         /* normal/twin/wide bus */
  char *                   mbase;            /* I/O memory address */
  unsigned char            chan_num;         /* for 3940/3985, channel number */
  unsigned char            unpause;          /* unpause value for HCNTRL */
  unsigned char            pause;            /* pause value for HCNTRL */
  unsigned char            qcntmask;
  struct seeprom_config    seeprom;
  struct Scsi_Host        *next;             /* allow for multiple IRQs */
  struct aic7xxx_scb      *scb_array[AIC7XXX_MAXSCB];  /* active commands */
  struct aic7xxx_scb      *pagedout_ntscbs[16];  /*
                                                  * paged-out, non-tagged scbs
                                                  * indexed by target.
                                                  */
  scb_queue_type           page_scbs;        /*
                                              * SCBs that will require paging
                                              * before use (no assigned slot)
                                              */
  scb_queue_type           waiting_scbs;     /*
                                              * SCBs waiting to be paged and
                                              * started.
                                              */
  scb_queue_type           assigned_scbs;    /*
                                              * SCBs that were waiting but have
                                              * have now been assigned a slot
                                              * by aic7xxx_free_scb
                                              */
  scb_usage_type           scb_usage;
  scb_usage_type          *scb_link;

  struct aic7xxx_cmd_queue {
    Scsi_Cmnd *head;
    Scsi_Cmnd *tail;
  } completeq;
  struct aic7xxx_device_status {
    long last_reset;
#define  DEVICE_SUCCESS                 0x01
#define  BUS_DEVICE_RESET_PENDING       0x02
    int  flags;
    int  commands_sent;
  } device_status[16];
#ifdef AIC7XXX_PROC_STATS
  /*
   * Statistics Kept:
   *
   * Total Xfers (count for each command that has a data xfer),
   * broken down further by reads && writes.
   *
   * Binned sizes, writes && reads:
   *    < 512, 512, 1-2K, 2-4K, 4-8K, 8-16K, 16-32K, 32-64K, 64K-128K, > 128K
   *
   * Total amounts read/written above 512 bytes (amts under ignored)
   */
  struct aic7xxx_xferstats {
    long xfers;                              /* total xfer count */
    long w_total;                            /* total writes */
    long w_total512;                         /* 512 byte blocks written */
    long w_bins[10];                         /* binned write */
    long r_total;                            /* total reads */
    long r_total512;                         /* 512 byte blocks read */
    long r_bins[10];                         /* binned reads */
  } stats[2][16][8];                         /* channel, target, lun */
#endif /* AIC7XXX_PROC_STATS */
};

struct aic7xxx_host_config {
  int              irq;        /* IRQ number */
  int              mbase;      /* memory base address*/
  int              base;       /* I/O base address*/
  int              maxhscbs;   /* hardware SCBs */
  int              maxscbs;    /* max SCBs (including pageable) */
  int              unpause;    /* unpause value for HCNTRL */
  int              pause;      /* pause value for HCNTRL */
  int              scsi_id;    /* host SCSI ID */
  int              scsi_id_b;  /* host SCSI ID B channel for twin cards */
  unsigned int     flags;      /* used the same as struct aic7xxx_host flags */
  int              chan_num;   /* for 3940/3985, channel number */
  unsigned char    busrtime;   /* bus release time */
  unsigned char    bus_speed;  /* bus speed */
  unsigned char    qcntmask;
  aha_type         type;       /* card type */
  aha_chip_type    chip_type;  /* chip base type */
  aha_bus_type     bus_type;   /* normal/twin/wide bus */
  aha_status_type  bios;       /* BIOS is enabled/disabled */
  aha_status_type  parity;     /* bus parity enabled/disabled */
  aha_status_type  low_term;   /* bus termination low byte */
  aha_status_type  high_term;  /* bus termination high byte (wide cards only) */
};

/*
 * Valid SCSIRATE values. (p. 3-17)
 * Provides a mapping of transfer periods in ns to the proper value to
 * stick in the scsiscfr reg to use that transfer rate.
 */
static struct {
  short period;
  /* Rates in Ultra mode have bit 8 of sxfr set */
#define		ULTRA_SXFR 0x100
  short rate;
  const char *english;
} aic7xxx_syncrates[] = {
  {  50,  0x100,  "20.0"  },
  {  62,  0x110,  "16.0"  },
  {  75,  0x120,  "13.4"  },
  { 100,  0x000,  "10.0"  },
  { 125,  0x010,   "8.0"  },
  { 150,  0x020,   "6.67" },
  { 175,  0x030,   "5.7"  },
  { 200,  0x040,   "5.0"  },
  { 225,  0x050,   "4.4"  },
  { 250,  0x060,   "4.0"  },
  { 275,  0x070,   "3.6"  }
};

static int num_aic7xxx_syncrates =
    sizeof(aic7xxx_syncrates) / sizeof(aic7xxx_syncrates[0]);

#ifdef CONFIG_PCI
static int number_of_3940s = 0;
static int number_of_3985s = 0;
#ifdef AIC7XXX_SHARE_SCBS
static scb_usage_type *shared_3985_scbs = NULL;
#endif
#endif CONFIG_PCI

#ifdef AIC7XXX_DEBUG

static void
debug_config(struct aic7xxx_host_config *p)
{
  int scsi_conf;
  unsigned char brelease;
  unsigned char dfthresh;

  static int DFT[] = { 0, 50, 75, 100 };
  static int SST[] = { 256, 128, 64, 32 };
  static const char *BUSW[] = { "", "-TWIN", "-WIDE" };

  scsi_conf = inb(SCSICONF + p->base);

  /*
   * Scale the Data FIFO Threshhold and the Bus Release Time; they are
   * stored in formats compatible for writing to sequencer registers.
   */
  dfthresh = p->bus_speed  >> 6;

  if (p->chip_type == AIC_777x)
  {
    brelease = p->busrtime >> 2;
  }
  else
  {
    brelease = p->busrtime;
  }
  if (brelease == 0)
  {
    brelease = 2;
  }

  switch (p->type)
  {
    case AIC_7770:
    case AIC_7771:
      printk("%s%s AT EISA SLOT %d:\n", board_names[p->type], BUSW[p->bus_type],
             p->base >> 12);
      break;

    case AIC_284x:
      printk("%s%s AT VLB SLOT %d:\n", board_names[p->type], BUSW[p->bus_type],
             p->base >> 12);
      break;

    case AIC_7850:
    case AIC_7855:
    case AIC_7860:
    case AIC_7861:
    case AIC_7870:
    case AIC_7871:
    case AIC_7872:
    case AIC_7873:
    case AIC_7874:
    case AIC_7880:
    case AIC_7881:
    case AIC_7882:
    case AIC_7883:
    case AIC_7884:
      printk("%s%s (PCI-bus), I/O 0x%x, Mem 0x%x:\n", board_names[p->type],
             BUSW[p->bus_type], p->base, p->mbase);
      break;

    default:
      panic("aic7xxx: (debug_config) internal error.\n");
  }

  printk("    irq %d\n"
	 "    bus release time %d bclks\n"
	 "    data fifo threshold %d%%\n",
	 p->irq,
	 brelease,
	 DFT[dfthresh]);

  printk("    SCSI CHANNEL A:\n"
	 "        scsi id %d\n"
	 "        scsi selection timeout %d ms\n"
	 "        scsi bus reset at power-on %sabled\n",
	 scsi_conf & 0x07,
	 SST[(scsi_conf >> 3) & 0x03],
	 (scsi_conf & 0x40) ? "en" : "dis");

  if ((p->chip_type == AIC_777x) && (p->parity == AIC_UNKNOWN))
  {
    /*
     * Set the parity for 7770 based cards.
     */
    p->parity = (scsi_conf & 0x20) ? AIC_ENABLED : AIC_DISABLED;
  }
  if (p->parity != AIC_UNKNOWN)
  {
    printk("        scsi bus parity %sabled\n",
	   (p->parity == AIC_ENABLED) ? "en" : "dis");
  }

  if ((p->type == AIC_7770) || (p->type == AIC_7771))
  {
    p->low_term = (scsi_conf & 0x80) ? AIC_ENABLED : AIC_DISABLED;
  }
  if (p->low_term != AIC_UNKNOWN)
  {
    printk("        scsi bus termination (low byte) %sabled\n",
	  (p->low_term == AIC_ENABLED) ? "en" : "dis");
  }
  if ((p->bus_type == AIC_WIDE) && (p->high_term != AIC_UNKNOWN))
  {
    printk("        scsi bus termination (high byte) %sabled\n",
	  (p->high_term == AIC_ENABLED) ? "en" : "dis");
  }
}

#if 0
static void
debug_scb(struct aic7xxx_scb *scb)
{
  printk("control 0x%x, tcl 0x%x, sg_count %d, sg_ptr 0x%x, cmdp 0x%x, cmdlen %d\n",
         scb->control, scb->target_channel_lun, scb->SG_segment_count,
         (scb->SG_list_pointer[3] << 24) | (scb->SG_list_pointer[2] << 16) |
         (scb->SG_list_pointer[1] << 8) | scb->SG_list_pointer[0],
         (scb->SCSI_cmd_pointer[3] << 24) | (scb->SCSI_cmd_pointer[2] << 16) |
         (scb->SCSI_cmd_pointer[1] << 8) | scb->SCSI_cmd_pointer[0],
         scb->SCSI_cmd_length);
  printk("reserved 0x%x, target status 0x%x, resid SG count %d, resid data count %d\n",
         (scb->RESERVED[1] << 8) | scb->RESERVED[0], scb->target_status,
         scb->residual_SG_segment_count,
         ((scb->residual_data_count[2] << 16) |
          (scb->residual_data_count[1] <<  8) |
          (scb->residual_data_count[0]));
  printk("data ptr 0x%x, data count %d, next waiting %d\n",
         (scb->data_pointer[3] << 24) | (scb->data_pointer[2] << 16) |
         (scb->data_pointer[1] << 8) | scb->data_pointer[0],
         scb->data_count, scb->next_waiting);
  printk("next ptr 0x%lx, Scsi Cmnd 0x%lx, state 0x%x, position %d\n",
         (unsigned long) scb->next, (unsigned long) scb->cmd, scb->state,
         scb->position);
}
#endif

#else
#  define debug_config(x)
#  define debug_scb(x)
#endif AIC7XXX_DEBUG

#define TCL_OF_SCB(x)  (((x)->target_channel_lun >> 4) & 0xf),  \
                       (((x)->target_channel_lun >> 3) & 0x01), \
                       ((x)->target_channel_lun & 0x07)

#define TARGET_INDEX(x)  ((x)->target | ((x)->channel << 3))

/*
 * XXX - these options apply unilaterally to _all_ 274x/284x/294x
 *       cards in the system. This should be fixed, but then,
 *       does anyone really have more than one in a machine?
 */
static unsigned int aic7xxx_extended = 0;    /* extended translation on? */
static unsigned int aic7xxx_no_reset = 0;    /* no resetting of SCSI bus */
static int aic7xxx_irq_trigger = -1;         /*
                                              * -1 use board setting
                                              *  0 use edge triggered
                                              *  1 use level triggered
                                              */
static int aic7xxx_enable_ultra = 0;         /* enable ultra SCSI speeds */

/*+F*************************************************************************
 * Function:
 *   aic7xxx_setup
 *
 * Description:
 *   Handle Linux boot parameters. This routine allows for assigning a value
 *   to a parameter with a ':' between the parameter and the value.
 *   ie. aic7xxx=unpause:0x0A,extended
 *-F*************************************************************************/
void
aic7xxx_setup(char *s, int *dummy)
{
  int   i, n;
  char *p;

  static struct {
    const char *name;
    unsigned int *flag;
  } options[] = {
    { "extended",    &aic7xxx_extended },
    { "no_reset",    &aic7xxx_no_reset },
    { "irq_trigger", &aic7xxx_irq_trigger },
    { "ultra",       &aic7xxx_enable_ultra },
    { NULL,          NULL }
  };

  for (p = strtok(s, ","); p; p = strtok(NULL, ","))
  {
    for (i = 0; options[i].name; i++)
    {
      n = strlen(options[i].name);
      if (!strncmp(options[i].name, p, n))
      {
        if (p[n] == ':')
        {
          *(options[i].flag) = simple_strtoul(p + n + 1, NULL, 0);
        }
        else
        {
          *(options[i].flag) = !0;
        }
      }
    }
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_loadseq
 *
 * Description:
 *   Load the sequencer code into the controller memory.
 *-F*************************************************************************/
static void
aic7xxx_loadseq(int base)
{
  static unsigned char seqprog[] = {
    /*
     * Each sequencer instruction is 29 bits
     * long (fill in the excess with zeroes)
     * and has to be loaded from least -> most
     * significant byte, so this table has the
     * byte ordering reversed.
     */
#   include "aic7xxx_seq.h"
  };

  /*
   * When the AIC-7770 is paused (as on chip reset), the
   * sequencer address can be altered and a sequencer
   * program can be loaded by writing it, byte by byte, to
   * the sequencer RAM port - the Adaptec documentation
   * recommends using REP OUTSB to do this, hence the inline
   * assembly. Since the address autoincrements as we load
   * the program, reset it back to zero afterward. Disable
   * sequencer RAM parity error detection while loading, and
   * make sure the LOADRAM bit is enabled for loading.
   */
  outb(PERRORDIS | SEQRESET | LOADRAM, SEQCTL + base);

  outsb(SEQRAM + base, seqprog, sizeof(seqprog));

  /*
   * WARNING!  This is a magic sequence!  After extensive
   * experimentation, it seems that you MUST turn off the
   * LOADRAM bit before you play with SEQADDR again, else
   * you will end up with parity errors being flagged on
   * your sequencer program. (You would also think that
   * turning off LOADRAM and setting SEQRESET to reset the
   * address to zero would work, but you need to do it twice
   * for it to take effect on the address. Timing problem?)
   */
  do {
    /*
     * Actually, reset it until
     * the address shows up as
     * zero just to be safe..
     */
    outb(SEQRESET | FASTMODE, SEQCTL + base);
  } while ((inb(SEQADDR0 + base) != 0) && (inb(SEQADDR1 + base) != 0));
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_delay
 *
 * Description:
 *   Delay for specified amount of time.
 *-F*************************************************************************/
static void
aic7xxx_delay(int seconds)
{
  unsigned long i;

  i = jiffies + (seconds * HZ);  /* compute time to stop */

  while (jiffies < i)
  {
    ;  /* Do nothing! */
  }
}

/*+F*************************************************************************
 * Function:
 *   rcs_version
 *
 * Description:
 *   Return a string containing just the RCS version number from either
 *   an Id or Revision RCS clause.
 *-F*************************************************************************/
const char *
rcs_version(const char *version_info)
{
  static char buf[10];
  char *bp, *ep;

  bp = NULL;
  strcpy(buf, "????");
  if (!strncmp(version_info, "$Id: ", 5))
  {
    if ((bp = strchr(version_info, ' ')) != NULL)
    {
      bp++;
      if ((bp = strchr(bp, ' ')) != NULL)
      {
	bp++;
      }
    }
  }
  else
  {
    if (!strncmp(version_info, "$Revision: ", 11))
    {
      if ((bp = strchr(version_info, ' ')) != NULL)
      {
	bp++;
      }
    }
  }

  if (bp != NULL)
  {
    if ((ep = strchr(bp, ' ')) != NULL)
    {
      register int len = ep - bp;

      strncpy(buf, bp, len);
      buf[len] = '\0';
    }
  }

  return buf;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_info
 *
 * Description:
 *   Return a string describing the driver.
 *-F*************************************************************************/
const char *
aic7xxx_info(struct Scsi_Host *notused)
{
  static char buffer[128];

  strcpy(buffer, "Adaptec AHA274x/284x/294x (EISA/VLB/PCI-Fast SCSI) ");
  strcat(buffer, rcs_version(AIC7XXX_C_VERSION));
  strcat(buffer, "/");
  strcat(buffer, rcs_version(AIC7XXX_H_VERSION));
  strcat(buffer, "/");
  strcat(buffer, rcs_version(AIC7XXX_SEQ_VER));

  return buffer;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_length
 *
 * Description:
 *   How much data should be transferred for this SCSI command? Stop
 *   at segment sg_last if it's a scatter-gather command so we can
 *   compute underflow easily.
 *-F*************************************************************************/
static unsigned
aic7xxx_length(Scsi_Cmnd *cmd, int sg_last)
{
  int i, segments;
  unsigned length;
  struct scatterlist *sg;

  segments = cmd->use_sg - sg_last;
  sg = (struct scatterlist *) cmd->request_buffer;

  if (cmd->use_sg)
  {
    for (i = length = 0; (i < cmd->use_sg) && (i < segments); i++)
    {
      length += sg[i].length;
    }
  }
  else
  {
    length = cmd->request_bufflen;
  }

  return (length);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_scsirate
 *
 * Description:
 *   Look up the valid period to SCSIRATE conversion in our table
 *-F*************************************************************************/
static void
aic7xxx_scsirate(struct aic7xxx_host *p, unsigned char *scsirate,
    short period, unsigned char offset, int target, char channel)
{
  int i;
  unsigned long ultra_enb_addr;
  unsigned char ultra_enb, sxfrctl0;

  /*
   * If the offset is 0, then the device is requesting asynchronous
   * transfers.
   */
  if (offset != 0)
  {
    for (i = 0; i < num_aic7xxx_syncrates; i++)
    {
      if ((aic7xxx_syncrates[i].period - period) >= 0)
      {
        /*
         * Watch out for Ultra speeds when ultra is not enabled and
         * vice-versa.
         */
        if (!(p->flags & ULTRA_ENABLED) &&
            (aic7xxx_syncrates[i].rate & ULTRA_SXFR))
        {
          /*
           * This should only happen if the drive is the first to negotiate
           * and chooses a high rate.   We'll just move down the table until
           * we hit a non ultra speed.
           */
          continue;
        }
        *scsirate = (aic7xxx_syncrates[i].rate) | (offset & 0x0F);

        /*
         * Ensure Ultra mode is set properly for this target.
         */
        ultra_enb_addr = ULTRA_ENB;
        if ((channel == 'B') || (target > 7))
        {
          ultra_enb_addr++;
        }
        ultra_enb = inb(p->base + ultra_enb_addr);
        sxfrctl0 = inb(p->base + SXFRCTL0);
        if (aic7xxx_syncrates[i].rate & ULTRA_SXFR)
        {
          ultra_enb |= 0x01 << (target & 0x07);
          sxfrctl0 |= ULTRAEN;
        }
        else
        {
          ultra_enb &= ~(0x01 << (target & 0x07));
          sxfrctl0 &= ~ULTRAEN;
        }
        outb(ultra_enb, p->base + ultra_enb_addr);
        outb(sxfrctl0, p->base + SXFRCTL0);

        printk("scsi%d: Target %d, channel %c, now synchronous at %sMHz, "
               "offset %d.\n", p->host_no, target, channel,
               aic7xxx_syncrates[i].english, offset);
        return;
      }
    }
  }

  /*
   * Default to asynchronous transfer
   */
  *scsirate = 0;
  printk("scsi%d: Target %d, channel %c, using asynchronous transfers.\n",
         p->host_no, target, channel);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_putscb
 *
 * Description:
 *   Transfer a SCB to the controller.
 *-F*************************************************************************/
static inline void
aic7xxx_putscb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  int base = p->base;

  outb(SCBAUTO, SCBCNT + base);

  /*
   * By turning on the SCB auto increment, any reference
   * to the SCB I/O space postincrements the SCB address
   * we're looking at. So turn this on and dump the relevant
   * portion of the SCB to the card.
   *
   * We can do 16bit transfers on all but 284x.
   */
  if (p->type == AIC_284x)
  {
    outsb(SCBARRAY + base, scb, SCB_PIO_TRANSFER_SIZE);
  }
  else
  {
    outsl(SCBARRAY + base, scb, (SCB_PIO_TRANSFER_SIZE + 3) / 4);
  }

  outb(0, SCBCNT + base);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_getscb
 *
 * Description:
 *   Get a SCB from the controller.
 *-F*************************************************************************/
static inline void
aic7xxx_getscb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  int base = p->base;

  /*
   * This is almost identical to aic7xxx_putscb().
   */
  outb(SCBAUTO, SCBCNT + base);
  insb(SCBARRAY + base, scb, SCB_PIO_TRANSFER_SIZE);
  outb(0, SCBCNT + base);
}

/*+F*************************************************************************
 * Function:
 *   scbq_init
 *
 * Description:
 *   SCB queue initialization.
 *
 *-F*************************************************************************/
static inline void
scbq_init(scb_queue_type *queue)
{
  queue->head = NULL;
  queue->tail = NULL;
}

/*+F*************************************************************************
 * Function:
 *   scbq_insert_head
 *
 * Description:
 *   Add an SCB to the head of the list.
 *
 *-F*************************************************************************/
static inline void
scbq_insert_head(scb_queue_type *queue, struct aic7xxx_scb *scb)
{
  scb->q_next = queue->head;
  queue->head = scb;
  if (queue->tail == NULL)       /* If list was empty, update tail. */
    queue->tail = queue->head;
}

/*+F*************************************************************************
 * Function:
 *   scbq_remove_head
 *
 * Description:
 *   Remove an SCB from the head of the list.
 *
 *-F*************************************************************************/
static inline void
scbq_remove_head(scb_queue_type *queue)
{
  if (queue->head != NULL)
    queue->head = queue->head->q_next;
  if (queue->head == NULL)       /* If list is now empty, update tail. */
    queue->tail = NULL;
}

/*+F*************************************************************************
 * Function:
 *   scbq_insert_tail
 *
 * Description:
 *   Add an SCB at the tail of the list.
 *
 *-F*************************************************************************/
static inline void
scbq_insert_tail(scb_queue_type *queue, struct aic7xxx_scb *scb)
{
  scb->q_next = NULL;
  if (queue->tail != NULL)       /* Add the scb at the end of the list. */
    queue->tail->q_next = scb;

  queue->tail = scb;             /* Update the tail. */
  if (queue->head == NULL)       /* If list was empty, update head. */
    queue->head = queue->tail;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_match_scb
 *
 * Description:
 *   Checks to see if an scb matches the target/channel as specified.
 *   If target is ALL_TARGETS (-1), then we're looking for any device
 *   on the specified channel; this happens when a channel is going
 *   to be reset and all devices on that channel must be aborted.
 *-F*************************************************************************/
static int
aic7xxx_match_scb(struct aic7xxx_scb *scb, int target, char channel)
{
  int targ = (scb->target_channel_lun >> 4) & 0x0F;
  char chan = (scb->target_channel_lun & SELBUSB) ? 'B' : 'A';

#ifdef AIC7XXX_DEBUG_ABORT
  printk("aic7xxx: (match_scb) comparing target/channel %d/%c to scb %d/%c\n",
         target, channel, targ, chan);
#endif
  if (target == ALL_TARGETS)
  {
    return (chan == channel);
  }
  else
  {
    return ((chan == channel) && (targ == target));
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_busy_target
 *
 * Description:
 *   Set the specified target active.
 *-F*************************************************************************/
static void
aic7xxx_busy_target(unsigned char target, char channel, int base)
{
  unsigned char active;
  unsigned long active_port = ACTIVE_A + base;

  if ((target > 0x07) || (channel == 'B'))
  {
    /*
     * targets on the Second channel or above id 7 store info in byte two
     * of ACTIVE
     */
    active_port++;
  }
  active = inb(active_port);
  active |= (0x01 << (target & 0x07));
  outb(active, active_port);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_unbusy_target
 *
 * Description:
 *   Set the specified target inactive.
 *-F*************************************************************************/
static void
aic7xxx_unbusy_target(unsigned char target, char channel, int base)
{
  unsigned char active;
  unsigned long active_port = ACTIVE_A + base;

  if ((target > 0x07) || (channel == 'B'))
  {
    /*
     * targets on the Second channel or above id 7 store info in byte two
     * of ACTIVE
     */
    active_port++;
  }
  active = inb(active_port);
  active &= ~(0x01 << (target & 0x07));
  outb(active, active_port);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_allocate_scb
 *
 * Description:
 *   Get a free SCB either from one already assigned to a hardware
 *   slot, or one that will require an SCB to be paged out before
 *   use.  If there are none, attempt to allocate a new one.
 *-F*************************************************************************/
static struct aic7xxx_scb *
aic7xxx_allocate_scb(struct aic7xxx_host *p)
{
  struct aic7xxx_scb *scbp = NULL;
  int maxscbs;

  scbp = p->scb_link->free_scbs.head;
  if (scbp != NULL)
  {
    scbq_remove_head(&p->scb_link->free_scbs);
  }
  else
  {
    /*
     * This should always be NULL if paging is not enabled.
     */
    scbp = p->page_scbs.head;
    if (scbp != NULL)
    {
      scbq_remove_head(&p->page_scbs);
    }
    else
    {
      /*
       * Set limit the SCB allocation to the maximum number of
       * hardware SCBs if paging is not enabled; otherwise use
       * the maximum (255).
       */
      if (p->flags & PAGE_ENABLED)
        maxscbs = p->maxscbs;
      else
        maxscbs = p->maxhscbs;
      if (p->scb_link->numscbs < maxscbs)
      {
        int scb_index = p->scb_link->numscbs;
        int scb_size = sizeof(struct aic7xxx_scb);

        p->scb_array[scb_index] = kmalloc(scb_size, GFP_ATOMIC | GFP_DMA);
        scbp = (p->scb_array[scb_index]);
        if (scbp != NULL)
        {
          memset(scbp, 0, sizeof(*scbp));
          scbp->tag = scb_index;
          if (scb_index < p->maxhscbs)
            scbp->position = scb_index;
          else
	    scbp->position = SCB_LIST_NULL;
          p->scb_link->numscbs++;
        }
      }
    }
  }
  if (scbp != NULL)
  {
#ifdef AIC7XXX_DEBUG
    p->scb_link->activescbs++;
#endif
  }
  return (scbp);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_queue_cmd_complete
 *
 * Description:
 *   Due to race conditions present in the SCSI subsystem, it is easier
 *   to queue completed commands, then call scsi_done() on them when
 *   we're finished.  This function queues the completed commands.
 *-F*************************************************************************/
static inline void
aic7xxx_queue_cmd_complete(struct aic7xxx_host *p, Scsi_Cmnd *cmd)
{
  if (p->completeq.tail == NULL)
    p->completeq.head = cmd;
  else
    p->completeq.tail->host_scribble = (char *) cmd;
  p->completeq.tail = cmd;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_done_cmds_complete
 *
 * Description:
 *   Process the completed command queue.
 *-F*************************************************************************/
static inline void
aic7xxx_done_cmds_complete(struct aic7xxx_host *p)
{
  Scsi_Cmnd *cmd;

  while (p->completeq.head != NULL)
  {
    cmd = p->completeq.head;
    p->completeq.head = (Scsi_Cmnd *)cmd->host_scribble;
    cmd->host_scribble = NULL;
    cmd->scsi_done(cmd);
  }
  p->completeq.tail = NULL;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_free_scb
 *
 * Description:
 *   Free the scb and update the page, waiting, free scb lists.
 *-F*************************************************************************/
static void
aic7xxx_free_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  struct aic7xxx_scb *wscb;

  scb->state = SCB_FREE;
  scb->cmd = NULL;
  scb->control = 0;
  scb->state = 0;

  if (scb->position == SCB_LIST_NULL)
  {
    scbq_insert_head(&p->page_scbs, scb);
  }
  else
  {
    /*
     * If there are any SCBS on the waiting queue, assign the slot of this
     * "freed" SCB to the first one.  We'll run the waiting queues after
     * all command completes for a particular interrupt are completed or
     * when we start another command.
     */
    wscb = p->waiting_scbs.head;
    if (wscb != NULL)
    {
      scbq_remove_head(&p->waiting_scbs);
      wscb->position = scb->position;
      scbq_insert_tail(&p->assigned_scbs, wscb);
      wscb->state = (wscb->state & ~SCB_WAITINGQ) | SCB_ASSIGNEDQ;

      /* 
       * The "freed" SCB will need to be assigned a slot before being
       * used, so put it in the page_scbs queue.
       */
      scb->position = SCB_LIST_NULL;
      scbq_insert_head(&p->page_scbs, scb);
    }
    else
    {
      scbq_insert_head(&p->scb_link->free_scbs, scb);
    }
#ifdef AIC7XXX_DEBUG
    p->scb_link->activescbs--;  /* For debugging purposes. */
#endif
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_done
 *
 * Description:
 *   Calls the higher level scsi done function and frees the scb.
 *-F*************************************************************************/
static void
aic7xxx_done(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  Scsi_Cmnd *cmd = scb->cmd;

  aic7xxx_free_scb(p, scb);
  aic7xxx_queue_cmd_complete(p, cmd);

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_done_aborted_scbs
 *
 * Description:
 *   Calls the scsi_done() for the Scsi_Cmnd of each scb in the
 *   aborted list, and adds each scb to the free list.
 *-F*************************************************************************/
static void
aic7xxx_done_aborted_scbs(struct aic7xxx_host *p)
{
  Scsi_Cmnd *cmd;
  struct aic7xxx_scb *scb;
  int i;

  for (i = 0; i < p->scb_link->numscbs; i++)
  {
    scb = (p->scb_array[i]);
    if (scb->state & SCB_QUEUED_FOR_DONE)
    {
#ifdef AIC7XXX_DEBUG_ABORT
      printk("aic7xxx: (done_aborted_scbs) Aborting scb %d, TCL=%d/%d/%d\n",
      scb->position, TCL_OF_SCB(scb));
#endif
      /*
       * Process the command after marking the scb as free
       * and adding it to the free list.
       */
      cmd = scb->cmd;
      p->device_status[TARGET_INDEX(cmd)].flags = 0;
      aic7xxx_free_scb(p, scb);
      cmd->scsi_done(cmd);  /* call the done function */
    }
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_add_waiting_scb
 *
 * Description:
 *   Add this SCB to the head of the "waiting for selection" list.
 *-F*************************************************************************/
static void
aic7xxx_add_waiting_scb(u_long base, struct aic7xxx_scb *scb)
{
  unsigned char next;
  unsigned char curscb;

  curscb = inb(SCBPTR + base);
  next = inb(WAITING_SCBH + base);

  outb(scb->position, SCBPTR + base);
  outb(next, SCB_NEXT + base);
  outb(scb->position, WAITING_SCBH + base);

  outb(curscb, SCBPTR + base);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_abort_waiting_scb
 *
 * Description:
 *   Manipulate the waiting for selection list and return the
 *   scb that follows the one that we remove.
 *-F*************************************************************************/
static unsigned char
aic7xxx_abort_waiting_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb,
    unsigned char prev)
{
  unsigned char curscb, next;
  int target = (scb->target_channel_lun >> 4) & 0x0F;
  char channel = (scb->target_channel_lun & SELBUSB) ? 'B' : 'A';
  int base = p->base;

  /*
   * Select the SCB we want to abort and pull the next pointer out of it.
   */
  curscb = inb(SCBPTR + base);
  outb(scb->position, SCBPTR + base);
  next = inb(SCB_NEXT + base);

  /*
   * Clear the necessary fields
   */
  outb(0, SCB_CONTROL + base);
  outb(SCB_LIST_NULL, SCB_NEXT + base);
  aic7xxx_unbusy_target(target, channel, base);

  /*
   * Update the waiting list
   */
  if (prev == SCB_LIST_NULL)
  {
    /*
     * First in the list
     */
    outb(next, WAITING_SCBH + base);
  }
  else
  {
    /*
     * Select the scb that pointed to us and update its next pointer.
     */
    outb(prev, SCBPTR + base);
    outb(next, SCB_NEXT + base);
  }
  /*
   * Point us back at the original scb position and inform the SCSI
   * system that the command has been aborted.
   */
  outb(curscb, SCBPTR + base);
  scb->state |= SCB_ABORTED | SCB_QUEUED_FOR_DONE;
  scb->cmd->result = (DID_RESET << 16);

  return (next);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_device
 *
 * Description:
 *   The device at the given target/channel has been reset.  Abort
 *   all active and queued scbs for that target/channel.
 *-F*************************************************************************/
static int
aic7xxx_reset_device(struct aic7xxx_host *p, int target, char channel)
{
  int base = p->base;
  struct aic7xxx_scb *scb;
  unsigned char active_scb;
  int i = 0;
  int found = 0;

  /*
   * Restore this when we're done
   */
  active_scb = inb(SCBPTR + base);

#ifdef AIC7XXX_DEBUG_ABORT
  printk("aic7xxx: (reset_device) target/channel %d/%c, active_scb %d\n",
         target, channel, active_scb);
#endif
  /*
   * Search the QINFIFO.
   */
  {
    int saved_queue[AIC7XXX_MAXSCB];
    int queued = inb(QINCNT + base) & p->qcntmask;

    for (i = 0; i < (queued - found); i++)
    {
      saved_queue[i] = inb(QINFIFO + base);
      outb(saved_queue[i], SCBPTR + base);
      scb = (p->scb_array[inb(SCB_TAG + base)]);
      if (aic7xxx_match_scb(scb, target, channel))
      {
        /*
         * We found an scb that needs to be aborted.
         */
#ifdef AIC7XXX_DEBUG_ABORT
        printk("aic7xxx: (reset_device) aborting SCB %d, TCL=%d/%d/%d\n",
               saved_queue[i], TCL_OF_SCB(scb));
#endif
        scb->state |= SCB_ABORTED | SCB_QUEUED_FOR_DONE;
        scb->cmd->result = (DID_RESET << 16);
        outb(0, SCB_CONTROL + base);
        i--;
        found++;
      }
    }
    /*
     * Now put the saved scbs back.
     */
    for (queued = 0; queued < i; queued++)
    {
      outb(saved_queue[queued], QINFIFO + base);
    }
  }

  /*
   * Search waiting for selection list.
   */
  {
    unsigned char next, prev;

    next = inb(WAITING_SCBH + base);  /* Start at head of list. */
    prev = SCB_LIST_NULL;

    while (next != SCB_LIST_NULL)
    {
      outb(next, SCBPTR + base);
      scb = (p->scb_array[inb(SCB_TAG + base)]);
      /*
       * Select the SCB.
       */
      if (aic7xxx_match_scb(scb, target, channel))
      {
        next = aic7xxx_abort_waiting_scb(p, scb, prev);
        found++;
      }
      else
      {
        prev = next;
        next = inb(SCB_NEXT + base);
      }
    }
  }

  /*
   * Go through the entire SCB array now and look for commands for
   * for this target that are active.  These are other (most likely
   * tagged) commands that were disconnected when the reset occurred.
   */
  for (i = 0; i < p->scb_link->numscbs; i++)
  {
    scb = (p->scb_array[i]);
    if ((scb->state & SCB_ACTIVE) && aic7xxx_match_scb(scb, target, channel))
    {
      /*
       * Ensure the target is "free"
       */
      aic7xxx_unbusy_target(target, channel, base);
      if (! (scb->state & SCB_PAGED_OUT))
      {
        outb(scb->position, SCBPTR + base);
        outb(0, SCB_CONTROL + base);
      }
      scb->state |= SCB_ABORTED | SCB_QUEUED_FOR_DONE;
      scb->cmd->result = (DID_RESET << 16);
      found++;
    }
  }

  outb(active_scb, SCBPTR + base);
  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_current_bus
 *
 * Description:
 *   Reset the current SCSI bus.
 *-F*************************************************************************/
static void
aic7xxx_reset_current_bus(int base)
{
  outb(SCSIRSTO, SCSISEQ + base);
  udelay(1000);
  outb(0, SCSISEQ + base);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_channel
 *
 * Description:
 *   Reset the channel.
 *-F*************************************************************************/
static int
aic7xxx_reset_channel(struct aic7xxx_host *p, char channel, int initiate_reset)
{
  int base = p->base;
  unsigned char sblkctl;
  char cur_channel;
  unsigned long offset, offset_max;
  int found;

  /*
   * Clean up all the state information for the
   * pending transactions on this bus.
   */
  found = aic7xxx_reset_device(p, ALL_TARGETS, channel);

  if (channel == 'B')
  {
    p->needsdtr |= (p->needsdtr_copy & 0xFF00);
    p->sdtr_pending &= 0x00FF;
    outb(0, ACTIVE_B + base);
    offset = TARG_SCRATCH + base + 8;
    offset_max = TARG_SCRATCH + base + 16;
  }
  else
  {
    if (p->bus_type == AIC_WIDE)
    {
      p->needsdtr = p->needsdtr_copy;
      p->needwdtr = p->needwdtr_copy;
      p->sdtr_pending = 0x0;
      p->wdtr_pending = 0x0;
      outb(0, ACTIVE_A + base);
      outb(0, ACTIVE_B + base);
      offset = TARG_SCRATCH + base;
      offset_max = TARG_SCRATCH + base + 16;
    }
    else
    {
      p->needsdtr |= (p->needsdtr_copy & 0x00FF);
      p->sdtr_pending &= 0xFF00;
      outb(0, ACTIVE_A + base);
      offset = TARG_SCRATCH + base;
      offset_max = TARG_SCRATCH + base + 8;
    }
  }
  while (offset < offset_max)
  {
    /*
     * Revert to async/narrow transfers
     * until we renegotiate.
     */
    u_char targ_scratch;
    targ_scratch = inb(offset);
    targ_scratch &= SXFR;
    outb(targ_scratch, offset);
    offset++;
  }

  /*
   * Reset the bus and unpause/restart the controller
   */

  /*
   * Case 1: Command for another bus is active
   */
  sblkctl = inb(SBLKCTL + base);
  cur_channel = (sblkctl & SELBUSB) ? 'B' : 'A';
  if (cur_channel != channel)
  {
#ifdef AIC7XXX_DEBUG_ABORT
    printk("aic7xxx: (reset_channel) Stealthily resetting channel %c\n",
           channel);
#endif
    /*
     * Stealthily reset the other bus without upsetting the current bus
     */
    outb(sblkctl ^ SELBUSB, SBLKCTL + base);
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(base);
    }
    outb(CLRSCSIRSTI | CLRSELTIMEO, CLRSINT1 + base);
    outb(CLRSCSIINT, CLRINT + base);
    outb(sblkctl, SBLKCTL + base);

    UNPAUSE_SEQUENCER(p);
  }
  /*
   * Case 2: A command from this bus is active or we're idle
   */
  else
  {
#ifdef AIC7XXX_DEBUG_ABORT
    printk("aic7xxx: (reset_channel) Resetting current channel %c\n",
           channel);
#endif
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(base);
    }
    outb(CLRSCSIRSTI | CLRSELTIMEO, CLRSINT1 + base);
    outb(CLRSCSIINT, CLRINT + base);
    RESTART_SEQUENCER(p);
#ifdef AIC7XXX_DEBUG_ABORT
    printk("aic7xxx: (reset_channel) Channel reset, sequencer restarted\n");
#endif
  }

  /*
   * Cause the mid-level SCSI code to delay any further 
   * queueing by the bus settle time for us.
   */
  p->host->last_reset = (jiffies + (AIC7XXX_RESET_DELAY * HZ));

  /*
   * Now loop through all the SCBs that have been marked for abortion,
   * and call the scsi_done routines.
   */
  aic7xxx_done_aborted_scbs(p);
  return found;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_page_scb
 *
 * Description:
 *   Swap in_scbp for out_scbp down in the cards SCB array.
 *   We assume that the SCB for out_scbp is already selected in SCBPTR.
 *
 *-F*************************************************************************/
static inline void
aic7xxx_page_scb(struct aic7xxx_host *p, struct aic7xxx_scb *out_scbp,
    struct aic7xxx_scb *in_scbp)
{
  int index;

  /* Page-out */
#if 0
printk("aic7xxx: Paging out target %d SCB and paging in target %d SCB\n",
       out_scbp->cmd->target, in_scbp->cmd->target);
#endif
  aic7xxx_getscb(p, out_scbp);
  out_scbp->state |= SCB_PAGED_OUT;
  if (!(out_scbp->control & TAG_ENB))
  {
    /* Stick in non-tagged array */
    index = (out_scbp->target_channel_lun >> 4) | 
            (out_scbp->target_channel_lun & SELBUSB);
    p->pagedout_ntscbs[index] = out_scbp;
  }

  /* Page-in */
  in_scbp->position = out_scbp->position;
  out_scbp->position = SCB_LIST_NULL;
  aic7xxx_putscb(p, in_scbp);
  in_scbp->state &= ~SCB_PAGED_OUT;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_run_waiting_queues
 *
 * Description:
 *   Scan the assigned_scbs and waiting_scbs queues.  For scbs in the
 *   assigned_scbs queue, we download and start them.  For scbs in the
 *   waiting_scbs queue, we page in as many as we can being careful
 *   not to cause a deadlock for a reconnecting target.
 *
 *-F*************************************************************************/
static inline void
aic7xxx_run_waiting_queues(struct aic7xxx_host *p)
{
  struct aic7xxx_scb *scb;
  u_char cur_scb, intstat;
  u_long base = p->base;
  long flags;

  if ((p->assigned_scbs.head == NULL) && (p->waiting_scbs.head == NULL))
    return;

  save_flags(flags);
  cli();

  PAUSE_SEQUENCER(p);
  cur_scb = inb(SCBPTR + base);
  intstat = inb(INTSTAT + base);

  /*
   * First handle SCBs that are waiting but have been assigned a slot.
   */
  scb = p->assigned_scbs.head;
  while (scb != NULL)
  {
    scbq_remove_head(&(p->assigned_scbs));
    outb(scb->position, SCBPTR + base);
    aic7xxx_putscb(p, scb);
    /* Mark this as an active command. */
    scb->state = (scb->state & ~SCB_ASSIGNEDQ) | SCB_ACTIVE;
    outb(scb->position, QINFIFO + base);
    scb = p->assigned_scbs.head;
  }

  /* Now deal with SCBs that require paging. */
  scb = p->waiting_scbs.head;
  if (scb != NULL)
  {
    u_char disc_scb = inb(DISCONNECTED_SCBH + base);
    u_char active = inb(FLAGS + base) & (SELECTED | IDENTIFY_SEEN);
    int count = 0;
    u_char next_scb;

    while (scb != NULL)
    {
      /* Attempt to page this SCB in */
      if (disc_scb == SCB_LIST_NULL)
        break;

      /*
       * Advance disc_scb to the next one in the list.
       */
      outb(disc_scb, SCBPTR + base);
      next_scb = inb(SCB_NEXT + base); 

      /*
       * We have to be careful about when we allow an SCB to be paged out. 
       * There must always be at least one slot availible for a reconnecting
       * target in case it references an SCB that has been paged out.  Our
       * heuristic is that either the disconnected list has at least two
       * entries in it or there is one entry and the sequencer is activily
       * working on an SCB which implies that it will either complete or
       * disconnect before another reconnection can occur.
       */
      if ((next_scb != SCB_LIST_NULL) || active)
      {
        u_char out_scbi;
        struct aic7xxx_scb *out_scbp;

        scbq_remove_head(&(p->waiting_scbs));

        /*
         * Find the in-core SCB for the one we're paging out.
         */
        out_scbi = inb(SCB_TAG + base); 
        out_scbp = (p->scb_array[out_scbi]);

        /* Do the page out and mark the paged in SCB as active. */
        aic7xxx_page_scb(p, out_scbp, scb);

        /* Mark this as an active command. */
        scb->state = (scb->state & ~SCB_WAITINGQ) | SCB_ACTIVE;

        /* Queue the command */
        outb(scb->position, QINFIFO + base);
        count++;

        /* Advance to the next disconnected SCB */
        disc_scb = next_scb;
        scb = p->waiting_scbs.head;
      }
      else
        scb = NULL;
    }

    if (count)
    {
      /* 
       * Update the head of the disconnected list.
       */
      outb(disc_scb, DISCONNECTED_SCBH + base);
      if (disc_scb != SCB_LIST_NULL)
      {
        outb(disc_scb, SCBPTR + base);
        outb(SCB_LIST_NULL, SCB_PREV + base);
      }
    }
  }
  /* Restore old position */
  outb(cur_scb, SCBPTR + base);

  /*
   * Guard against unpausing the sequencer if there is an interrupt
   * waiting to happen.
   */
  if (!(intstat & (BRKADRINT | SEQINT | SCSIINT)))
  {
    UNPAUSE_SEQUENCER(p);
  }

  restore_flags(flags);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_isr
 *
 * Description:
 *   SCSI controller interrupt handler.
 *
 *   NOTE: Since we declared this using SA_INTERRUPT, interrupts should
 *         be disabled all through this function unless we say otherwise.
 *-F*************************************************************************/
static void
aic7xxx_isr(int irq, void *dev_id, struct pt_regs *regs)
{
  int base, intstat, actual, scb_index, run_aborted_queue = FALSE;
  struct aic7xxx_host *p;
  struct aic7xxx_scb *scb = NULL;
  short         transfer;
  unsigned char ha_flags, scsi_id, bus_width;
  unsigned char offset, rate, scratch, scratch_offset;
  unsigned char max_offset, rej_byte;
  unsigned short target_mask;
  char channel;
  unsigned int addr; /* must be 32 bits */
  Scsi_Cmnd *cmd;

  p = (struct aic7xxx_host *) aic7xxx_boards[irq]->hostdata;

  /*
   * Search for the host with a pending interrupt.  If we can't find
   * one, then we've encountered a spurious interrupt.
   */
  while ((p != NULL) && !(inb(INTSTAT + p->base) & INT_PEND))
  {
    if (p->next == NULL)
    {
      p = NULL;
    }
    else
    {
      p = (struct aic7xxx_host *) p->next->hostdata;
    }
  }

  if (p == NULL)
    return;

  /*
   * Keep track of interrupts for /proc/scsi
   */
  p->isr_count++;

  if (!(p->flags & A_SCANNED) && (p->isr_count == 1))
  {
    /*
     * We must only have one card at this IRQ and it must have been
     * added to the board data before the spurious interrupt occurred.
     * It is sufficient that we check isr_count and not the spurious
     * interrupt count.
     */
    printk("aic7xxx: (aic7xxx_isr) Encountered spurious interrupt.\n");
    return;
  }

  base = p->base;
  /*
   * Handle all the interrupt sources - especially for SCSI
   * interrupts, we won't get a second chance at them.
   */
  intstat = inb(INTSTAT + base);

  /*
   * Indicate that we're in the interrupt handler.
   */
  p->flags |= IN_ISR;

  if (intstat & BRKADRINT)
  {
    int i;
    unsigned char errno = inb(ERROR + base);

    printk(KERN_ERR "scsi%d: BRKADRINT error(0x%x):\n", p->host_no, errno);
    for (i = 0; i < NUMBER(hard_error); i++)
    {
      if (errno & hard_error[i].errno)
      {
        printk(KERN_ERR "  %s\n", hard_error[i].errmesg);
      }
    }
    panic("scsi%d: BRKADRINT, error 0x%x, seqaddr 0x%x.\n", p->host_no,
          inb(ERROR + base), (inb(SEQADDR1 + base) << 8) | inb(SEQADDR0 + base));
  }

  if (intstat & SEQINT)
  {
    /*
     * Although the sequencer is paused immediately on
     * a SEQINT, an interrupt for a SCSIINT condition will
     * unpaused the sequencer before this point.
     */
    PAUSE_SEQUENCER(p);

    scsi_id = (inb(SCSIID + base) >> 4) & 0x0F;
    scratch_offset = scsi_id;
    channel = 'A';
    if (inb(SBLKCTL + base) & SELBUSB)
    {
      channel = 'B';
      scratch_offset += 8;
    }
    target_mask = (0x01 << scratch_offset);

    switch (intstat & SEQINT_MASK)
    {
      case NO_MATCH:
    	if (p->flags & PAGE_ENABLED)
        {
    	  /* SCB Page-in request */
    	  struct aic7xxx_scb *outscb;
    	  u_char arg_1 = inb(ARG_1 + base);
          int use_disconnected = FALSE;

          /*
           * The sequencer expects this value upon return.  Assume
           * we will find the paged out SCB and set the value now.
           * If we don't, and one of the methods used to acquire an
           * SCB calls aic7xxx_done(), we will end up in our queue
           * routine and unpause the sequencer without giving it the
           * correct return value, which causes a hang.
           */
    	  outb(SCB_PAGEDIN, RETURN_1 + base);
    	  if (arg_1 == SCB_LIST_NULL)
          {
    	    /* Non-tagged command */
    	    int index = scsi_id;
            if (channel == 'B')
            {
              index |= SELBUSB;
            }
    	    scb = p->pagedout_ntscbs[index];
    	  }
    	  else
    	    scb = (p->scb_array[arg_1]);

          if (!(scb->state & SCB_PAGED_OUT))
          {
  	    printk(KERN_WARNING "scsi%d: No active paged-out SCB for reconnecting "
  	  	  "target %d, channel %c - Issuing ABORT. SAVED_TCL(0x%x).\n",
    		  p->host_no, scsi_id, channel, inb(SAVED_TCL + base));
    	    aic7xxx_unbusy_target(scsi_id, channel, base);
  	    outb(CLRSELTIMEO, CLRSINT1 + base);
            outb(0, RETURN_1 + base);
            break;
          }

    	  /*
    	   * Now to pick the SCB to page out.  Either take a free SCB, an
           * assigned SCB, an SCB that just completed, or the first one
           * on the disconnected SCB list.
    	   */
    	  if (p->scb_link->free_scbs.head != NULL)
          {
    	    outscb = p->scb_link->free_scbs.head;
    	    scbq_remove_head(&p->scb_link->free_scbs);
    	    scb->position = outscb->position;
    	    outscb->position = SCB_LIST_NULL;
    	    scbq_insert_head(&p->page_scbs, outscb);
    	    outb(scb->position, SCBPTR + base);
    	    aic7xxx_putscb(p, scb);
    	    scb->state &= ~SCB_PAGED_OUT;
    	  }
    	  else if (p->assigned_scbs.head != NULL)
          {
            outscb = p->assigned_scbs.head;
            scbq_remove_head(&p->assigned_scbs);
            scb->position = outscb->position;
            outscb->position = SCB_LIST_NULL;
            scbq_insert_head(&p->waiting_scbs, outscb);
            outscb->state = (outscb->state & ~SCB_ASSIGNEDQ) | SCB_WAITINGQ;
            outb(scb->position, SCBPTR + base);
    	    aic7xxx_putscb(p, scb);
            scb->state &= ~SCB_PAGED_OUT;
          }
          else if (intstat & CMDCMPLT)
          {
            int scb_index;

            outb(CLRCMDINT, CLRINT + base);
            scb_index = inb(QOUTFIFO + base);
            if (!(inb(QOUTCNT + base) & p->qcntmask))
            {
              intstat &= ~CMDCMPLT;
            }
            outscb = (p->scb_array[scb_index]);
            if (!(outscb->state & SCB_ACTIVE))
            {
	      printk(KERN_WARNING "scsi%d: No command for completed SCB %d "
	             "during NO_MATCH interrupt\n", scb_index, p->host_no);
              use_disconnected = TRUE;
            }
            else
            {
              scb->position = outscb->position;
              outscb->position = SCB_LIST_NULL;
              outb(scb->position, SCBPTR + base);
              aic7xxx_putscb(p, scb);
              scb->state &= ~SCB_PAGED_OUT;
              outscb->cmd->result |= (aic7xxx_error(outscb->cmd) << 16);
              if ((outscb->cmd->flags & WAS_SENSE) && 
                 !(outscb->cmd->flags & ASKED_FOR_SENSE))
              {
                /*
                 * Got sense information.
                 */
	        outscb->cmd->flags &= ASKED_FOR_SENSE;
              }
              p->device_status[TARGET_INDEX(outscb->cmd)].flags
                |= DEVICE_SUCCESS;
              aic7xxx_done(p, outscb);
            }
          }
          else
          {
            use_disconnected = TRUE;
          }
          if (use_disconnected)
          {
    	    u_char tag;
    	    u_char next;
    	    u_char disc_scb = inb(DISCONNECTED_SCBH + base);
    	    if (disc_scb != SCB_LIST_NULL)
            {
    	      outb(disc_scb, SCBPTR + base);
    	      tag = inb(SCB_TAG + base);
    	      outscb = (p->scb_array[tag]);
    	      next = inb(SCB_NEXT + base);
    	      if (next != SCB_LIST_NULL)
              {
    	        outb(next, SCBPTR + base);
    	        outb(SCB_LIST_NULL, SCB_PREV + base);
    	        outb(disc_scb, SCBPTR + base);
    	      }
    	      outb(next, DISCONNECTED_SCBH + base);
    	      aic7xxx_page_scb(p, outscb, scb);
    	    }
            else if (inb(QINCNT + base) & p->qcntmask)
            {
              /* Pull one of our queued commands as a last resort. */
              disc_scb = inb(QINFIFO + base);
              outb(disc_scb, SCBPTR + base);
              tag = inb(SCB_TAG + base);
              outscb = (p->scb_array[tag]);
              if ((outscb->control & 0x23) != TAG_ENB)
              {
                /*
                 * This is not a simple tagged command so its position
                 * in the queue matters.  Take the command at the end of
                 * the queue instead.
                 */
                int i;
                int saved_queue[AIC7XXX_MAXSCB];
                int queued = inb(QINCNT + base) & p->qcntmask;

                /* Count the command we removed already */
                saved_queue[0] = disc_scb;
                queued++;

                /* Empty the input queue. */
                for (i = 1; i < queued; i++)
                {
                  saved_queue[i] = inb(QINFIFO + base);
                }

                /* Put everyone back but the last entry. */
                queued--;
                for (i = 0; i < queued; i++)
                {
                  outb(saved_queue[i], QINFIFO + base);
                }

                outb(saved_queue[queued], SCBPTR + base);
                tag = inb(SCB_TAG + base);
                outscb = (p->scb_array[tag]);
              }
              scb->position = outscb->position;
              outscb->position = SCB_LIST_NULL;
              scbq_insert_head(&p->waiting_scbs, outscb);
              outscb->state |= SCB_WAITINGQ;
              aic7xxx_putscb(p, scb);
              scb->state &= ~SCB_PAGED_OUT;
            }
            else
            {
  	      printk(KERN_WARNING "scsi%d: Page-in request with no candidates "
  	  	    "target %d, channel %c - Issuing ABORT. SAVED_TCL(0x%x).\n",
    		    p->host_no, scsi_id, channel, inb(SAVED_TCL + base));
              aic7xxx_unbusy_target(scsi_id, channel, base);
              outb(CLRSELTIMEO, CLRSINT1 + base);
              outb(0, RETURN_1 + base);
            }
          }
    	}
    	else
        {
  	  printk(KERN_WARNING "scsi%d: No active SCB for reconnecting "
  	  	"target %d, channel %c - Issuing ABORT. SAVED_TCL(0x%x).\n",
    		p->host_no, scsi_id, channel, inb(SAVED_TCL + base));
    	  aic7xxx_unbusy_target(scsi_id, channel, base);
    	  outb(0, SCB_CONTROL + base);
  	  outb(CLRSELTIMEO, CLRSINT1 + base);
          outb(0, RETURN_1 + base);
        }
  	break;

      case BAD_PHASE:
	panic("scsi%d: Unknown scsi bus phase.\n", p->host_no);
	break;

      case SEND_REJECT:
        rej_byte = inb(REJBYTE + base);
        if ((rej_byte & 0xF0) == 0x20)
        {
          scb_index = inb(SCB_TAG + base);
          scb = (p->scb_array[scb_index]);
          printk(KERN_WARNING "scsi%d: Tagged message received without identify."
                 "Disabling tagged commands for target %d channel %c.\n",
                  p->host_no, scsi_id, channel);
          scb->cmd->device->tagged_supported = 0;
          scb->cmd->device->tagged_queue = 0;
        }
        else
        {
          printk(KERN_WARNING "scsi%d: Rejecting unknown message (0x%x) received "
                 "from target %d channel %c.\n",
                 p->host_no, rej_byte, scsi_id, channel);
        }
	break;

      case NO_IDENT:
	panic("scsi%d: Target %d, channel %c, did not send an IDENTIFY "
	      "message. SAVED_TCL 0x%x.\n",
              p->host_no, scsi_id, channel, inb(SAVED_TCL + base));
	break;

      case SDTR_MSG:
	/*
	 * Help the sequencer to translate the negotiated
	 * transfer rate. Transfer is 1/4 the period
	 * in ns as is returned by the sync negotiation
	 * message. So, we must multiply by four.
	 */
	transfer = (inb(ARG_1 + base) << 2);
	offset = inb(ACCUM + base);
	scratch = inb(TARG_SCRATCH + base + scratch_offset);
	/*
	 * The maximum offset for a wide device is 0x08; for a
	 * 8-bit bus device the maximum offset is 0x0F.
	 */
	if (scratch & WIDEXFER)
	{
	  max_offset = 0x08;
	}
	else
	{
	  max_offset = 0x0F;
	}
	aic7xxx_scsirate(p, &rate, transfer, MIN(offset, max_offset),
                         scsi_id, channel);
	/*
	 * Preserve the wide transfer flag.
	 */
	scratch = rate | (scratch & WIDEXFER);
	outb(scratch, TARG_SCRATCH + base + scratch_offset);
	outb(scratch, SCSIRATE + base);
	if ((scratch & 0x0F) == 0)
	{
          /*
           * One of two things happened.  Either the device requested
           * asynchronous data transfers, or it requested a synchronous
           * data transfer rate that was so low that asynchronous
           * transfers are faster (not to mention the controller won't
           * support them).  In both cases the synchronous data transfer
           * rate and the offset are set to 0 indicating asynchronous
           * transfers.
           *
           * If the device requested an asynchronous transfer, then
           * accept the request.  If the device is being forced to
           * asynchronous data transfers and this is the first time
           * we've seen the request, accept the request.  If we've
           * already seen the request, then attempt to force
           * asynchronous data transfers by rejecting the message.
           */
          if ((offset == 0) || (p->sdtr_pending & target_mask))
          {
            /*
             * Device requested asynchronous transfers or we're
             * forcing asynchronous transfers for the first time.
             */
            outb(0, RETURN_1 + base);
          }
          else
          {
            /*
	     * The first time in forcing asynchronous transfers
             * failed, so we try sending a reject message.
	     */
	    outb(SEND_REJ, RETURN_1 + base);
          }
	}
	else
	{
	  /*
	   * See if we initiated Sync Negotiation
	   */
	  if (p->sdtr_pending & target_mask)
	  {
	    /*
	     * Don't send an SDTR back to the target.
	     */
	    outb(0, RETURN_1 + base);
	  }
	  else
	  {
	    /*
	     * Send our own SDTR in reply.
	     */
	    printk("aic7xxx: Sending SDTR!!\n");
	    outb(SEND_SDTR, RETURN_1 + base);
	  }
	}
	/*
	 * Clear the flags.
	 */
	p->needsdtr &= ~target_mask;
	p->sdtr_pending &= ~target_mask;
	break;

      case WDTR_MSG:
      {
	bus_width = inb(ARG_1 + base);
	printk(KERN_INFO "scsi%d: Received MSG_WDTR, Target %d, channel %c "
	       "needwdtr(0x%x).\n", p->host_no, scsi_id, channel, p->needwdtr);
	scratch = inb(TARG_SCRATCH + base + scratch_offset);

	if (p->wdtr_pending & target_mask)
	{
	  /*
	   * Don't send an WDTR back to the target, since we asked first.
	   */
	  outb(0, RETURN_1 + base);
	  switch (bus_width)
	  {
	    case BUS_8_BIT:
	      scratch &= 0x7F;
	      break;

	    case BUS_16_BIT:
	      printk(KERN_INFO "scsi%d: Target %d, channel %c, using 16 bit "
                     "transfers.\n", p->host_no, scsi_id, channel);
	      scratch |= 0x80;
	      break;

	    case BUS_32_BIT:
	      outb(SEND_REJ, RETURN_1 + base);
	      printk(KERN_INFO "scsi%d: Target %d, channel %c, requesting 32 bit "
                     "transfers, rejecting...\n", p->host_no, scsi_id, channel);
	      break;
	  }
	}
	else
	{
	  /*
	   * Send our own WDTR in reply.
	   */
	  printk(KERN_INFO "scsi%d: Will send WDTR!!\n", p->host_no);
	  switch (bus_width)
	  {
	    case BUS_8_BIT:
	      scratch &= 0x7F;
	      break;

	    case BUS_32_BIT:
	      /*
               * Negotiate 16 bits.
               */
	      bus_width = BUS_16_BIT;
	      /* Yes, we mean to fall thru here. */

	    case BUS_16_BIT:
	      printk(KERN_INFO "scsi%d: Target %d, channel %c, using 16 bit "
                     "transfers.\n", p->host_no, scsi_id, channel);
	      scratch |= 0x80;
	      break;
	  }
	  outb(bus_width | SEND_WDTR, RETURN_1 + base);
	}
	p->needwdtr &= ~target_mask;
	p->wdtr_pending &= ~target_mask;
	outb(scratch, TARG_SCRATCH + base + scratch_offset);
	outb(scratch, SCSIRATE + base);
	break;
      }

      case REJECT_MSG:
      {
	/*
	 * What we care about here is if we had an
	 * outstanding SDTR or WDTR message for this
	 * target. If we did, this is a signal that
	 * the target is refusing negotiation.
	 */

	scratch = inb(TARG_SCRATCH + base + scratch_offset);

	if (p->wdtr_pending & target_mask)
	{
	  /*
	   * note 8bit xfers and clear flag
	   */
	  scratch &= 0x7F;
	  p->needwdtr &= ~target_mask;
	  p->wdtr_pending &= ~target_mask;
	  printk(KERN_WARNING "scsi%d: Target %d, channel %c, refusing WIDE "
                 "negotiation; using 8 bit transfers.\n",
                 p->host_no, scsi_id, channel);
	}
	else
	{
	  if (p->sdtr_pending & target_mask)
	  {
	    /*
	     * note asynch xfers and clear flag
	     */
	    scratch &= 0xF0;
	    p->needsdtr &= ~target_mask;
	    p->sdtr_pending &= ~target_mask;
	    printk(KERN_WARNING "scsi%d: Target %d, channel %c, refusing "
                   "synchronous negotiation; using asynchronous transfers.\n",
                   p->host_no, scsi_id, channel);
	  }
	  /*
	   * Otherwise, we ignore it.
	   */
	}
	outb(scratch, TARG_SCRATCH + base + scratch_offset);
	outb(scratch, SCSIRATE + base);
	break;
      }

      case BAD_STATUS:
        /* The sequencer will notify us when a command has an error that
         * would be of interest to the kernel.  This allows us to leave
         * the sequencerrunning in the common case of command completes
         * without error.
         */

	scb_index = inb(SCB_TAG + base);
	scb = (p->scb_array[scb_index]);
	outb(0, RETURN_1 + base);   /* CHECK_CONDITION may change this */
	if (!(scb->state & SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk(KERN_WARNING "scsi%d: Referenced SCB not valid during "
		 "SEQINT 0x%x, scb %d, state 0x%x, cmd 0x%lx.\n", p->host_no,
		 intstat, scb_index, scb->state, (unsigned long) scb->cmd);
	}
	else
	{
	  cmd = scb->cmd;
          scb->target_status = inb(SCB_TARGET_STATUS + base);
	  aic7xxx_status(cmd) = scb->target_status;

	  cmd->result |= scb->target_status;

	  switch (status_byte(scb->target_status))
	  {
	    case GOOD:
              printk(KERN_WARNING "aic7xxx: Interrupted for status of GOOD???\n");
	      break;

	    case CHECK_CONDITION:
	      if ((aic7xxx_error(cmd) == 0) && !(cmd->flags & WAS_SENSE))
	      {
                unsigned char tcl;
		unsigned int  req_buf; /* must be 32 bits */

                tcl = scb->target_channel_lun;

		/*
                 * Send a sense command to the requesting target.
                 */
		cmd->flags |= WAS_SENSE;
		memcpy((void *) scb->sense_cmd, (void *) generic_sense,
		       sizeof(generic_sense));

		scb->sense_cmd[1] = (cmd->lun << 5);
		scb->sense_cmd[4] = sizeof(cmd->sense_buffer);

		scb->sg_list[0].address = VIRT_TO_BUS(&cmd->sense_buffer);
		scb->sg_list[0].length = sizeof(cmd->sense_buffer);
		req_buf = VIRT_TO_BUS(&scb->sg_list[0]);
		cmd->cmd_len = COMMAND_SIZE(cmd->cmnd[0]);

                scb->control = scb->control & DISCENB;
		scb->target_channel_lun = tcl;
		addr = VIRT_TO_BUS(scb->sense_cmd);
		scb->SCSI_cmd_length = COMMAND_SIZE(scb->sense_cmd[0]);
		memcpy(scb->SCSI_cmd_pointer, &addr,
		       sizeof(scb->SCSI_cmd_pointer));
		scb->SG_segment_count = 1;
		memcpy(scb->SG_list_pointer, &req_buf,
		       sizeof(scb->SG_list_pointer));
                scb->data_count = scb->sg_list[0].length;
		memcpy(scb->data_pointer, &(scb->sg_list[0].address),
		       sizeof(scb->data_pointer));

                aic7xxx_putscb(p, scb);
                /*
                 * Ensure that the target is "BUSY" so we don't get overlapping
                 * commands if we happen to be doing tagged I/O.
                 */
		aic7xxx_busy_target(scsi_id, channel, base);

                aic7xxx_add_waiting_scb(base, scb);
		outb(SEND_SENSE, RETURN_1 + base);
	      }  /* first time sense, no errors */
              else
              {
                cmd->flags &= ~ASKED_FOR_SENSE;
	        if (aic7xxx_error(cmd) == 0)
                {
		  aic7xxx_error(cmd) = DID_RETRY_COMMAND;
                }
              }
	      break;

	    case BUSY:
	      printk(KERN_WARNING "scsi%d: Target busy, TCL=0x%x.\n",
                     p->host_no, scb->target_channel_lun);
	      if (!aic7xxx_error(cmd))
	      {
                /* The error code here used to be DID_BUS_BUSY,
                 * but after extensive testing, it has been determined
                 * that a DID_BUS_BUSY return is a waste of time.  If
                 * the problem is something that will go away, then it
                 * will, if it isn't, then you don't want the endless
                 * looping that you get with a DID_BUS_BUSY.  Better
                 * to be on the safe side and specify an error condition
                 * that will eventually lead to a reset or abort of some
                 * sort instead of an endless loop.
                 */
	        aic7xxx_error(cmd) = DID_RETRY_COMMAND;
	      }
	      break;

	    case QUEUE_FULL:
	      printk(KERN_WARNING "scsi%d: Queue full.\n", p->host_no);
              scb->state |= SCB_ASSIGNEDQ;
              scbq_insert_tail(&p->assigned_scbs, scb);
	      break;

	    default:
	      printk(KERN_WARNING "scsi%d: Unexpected target status 0x%x.\n",
		     p->host_no, scb->target_status);
	      if (!aic7xxx_error(cmd))
	      {
		aic7xxx_error(cmd) = DID_RETRY_COMMAND;
	      }
	      break;
	  }  /* end switch */
	}  /* end else of */
	break;

      case RESIDUAL:
	scb_index = inb(SCB_TAG + base);
	scb = (p->scb_array[scb_index]);
	if (!(scb->state & SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk(KERN_WARNING "scsi%d: Referenced SCB not valid during "
		 "SEQINT 0x%x, scb %d, state 0x%x, cmd 0x%lx.\n", p->host_no,
		 intstat, scb_index, scb->state, (unsigned long) scb->cmd);
	}
	else
	{
	  cmd = scb->cmd;
	  /*
	   *  Don't destroy valid residual information with
	   *  residual coming from a check sense operation.
	   */
	  if (!(cmd->flags & WAS_SENSE))
	  {
	    /*
	     *  We had an underflow. At this time, there's only
	     *  one other driver that bothers to check for this,
	     *  and cmd->underflow seems to be set rather half-
	     *  heartedly in the higher-level SCSI code.
	     */
	    actual = aic7xxx_length(cmd, scb->residual_SG_segment_count);

	    actual -= (inb(SCB_RESID_DCNT2 + base) << 16) |
		      (inb(SCB_RESID_DCNT1 + base) <<  8) |
		      inb(SCB_RESID_DCNT0 + base);

	    if (actual < cmd->underflow)
	    {
	      printk(KERN_WARNING "scsi%d: Target %d underflow - "
		     "Wanted at least %u, got %u, residual SG count %d.\n",
		     p->host_no, cmd->target, cmd->underflow, actual,
                     inb(SCB_RESID_SGCNT + base));
	      aic7xxx_error(cmd) = DID_RETRY_COMMAND;
	      aic7xxx_status(cmd) = scb->target_status;
	    }
	  }
	}
	break;

      case ABORT_TAG:
	scb_index = inb(SCB_TAG + base);
	scb = (p->scb_array[scb_index]);
	if (!(scb->state & SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk(KERN_WARNING "scsi%d: Referenced SCB not valid during "
		 "SEQINT 0x%x, scb %d, state 0x%x, cmd 0x%lx\n", p->host_no,
		 intstat, scb_index, scb->state, (unsigned long) scb->cmd);
	}
	else
	{
	  cmd = scb->cmd;
	  /*
	   * We didn't receive a valid tag back from the target
	   * on a reconnect.
	   */
	  printk("scsi%d: Invalid tag received on target %d, channel %c, "
                 "lun %d - Sending ABORT_TAG.\n", p->host_no,
		  scsi_id, channel, cmd->lun & 0x07);

	  cmd->result = (DID_RETRY_COMMAND << 16);
          aic7xxx_done(p, scb);
	}
	break;

      case AWAITING_MSG:
	scb_index = inb(SCB_TAG + base);
	scb = (p->scb_array[scb_index]);
	if (!(scb->state & SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk(KERN_WARNING "scsi%d: Referenced SCB not valid during "
		 "SEQINT 0x%x, scb %d, state 0x%x, cmd 0x%lx.\n", p->host_no,
		 intstat, scb_index, scb->state, (unsigned long) scb->cmd);
	}
	else
	{
	  /*
	   * This SCB had a zero length command, informing the sequencer
	   * that we wanted to send a special message to this target.
	   * We only do this for BUS_DEVICE_RESET messages currently.
	   */
	   if (scb->state & SCB_DEVICE_RESET)
	   {
#ifdef AIC7XXX_DEBUG_ABORT
  printk ("aic7xxx: (isr) sending bus device reset to target %d\n",
          scsi_id);
#endif
	     outb(MSG_BUS_DEVICE_RESET, MSG0 + base);
	     outb(1, MSG_LEN + base);
	   }
	   else
	   {
	     panic("scsi%d: AWAITING_SCB for an SCB that does "
		   "not have a waiting message.\n", p->host_no);
	   }
	}
	break;

      case IMMEDDONE:
        scb_index = inb(SCB_TAG + base);
	scb = (p->scb_array[scb_index]);
#ifdef AIC7XXX_DEBUG_ABORT
  printk("aic7xxx: received IMMEDDONE for target %d, scb %d, state %d\n",
         scsi_id, scb_index, scb->state);
#endif
        if (scb->state & SCB_DEVICE_RESET)
        {
          int found;

          /*
           * Go back to async/narrow transfers and renegotiate.
           */
          aic7xxx_unbusy_target(scsi_id, channel, base);
          p->needsdtr |= (p->needsdtr_copy & target_mask);
          p->needwdtr |= (p->needwdtr_copy & target_mask);
          p->sdtr_pending &= ~target_mask;
          p->wdtr_pending &= ~target_mask;
          scratch = inb(TARG_SCRATCH + base + scratch_offset);
          scratch &= SXFR;
          outb(scratch, TARG_SCRATCH + base + scratch_offset);
          found = aic7xxx_reset_device(p, (int) scsi_id, channel);
          printk(KERN_INFO "scsi%d: Bus Device Reset delivered, %d SCBs "
                 "aborted.\n", p->host_no, found);
          /* Indicate that we want to call aic7xxx_done_aborted_scbs() */
          run_aborted_queue = TRUE;
        }
        else
        {
          panic("scsi%d: Immediate complete for unknown operation.\n",
                p->host_no);
        }
        break;

      case DATA_OVERRUN:
      {
        unsigned int overrun;

        scb = (p->scb_array[inb(base + SCB_TAG)]);
        overrun = inb(base + STCNT0) | (inb(base + STCNT1) << 8) |
                  (inb(base + STCNT2) << 16);
        overrun =0x00FFFFFF - overrun;
        printk(KERN_WARNING "scsi%d: data overrun of %d bytes detected; forcing "
               "a retry.\n", p->host_no, overrun);
        aic7xxx_error(scb->cmd) = DID_RETRY_COMMAND;
        break;
      }

#if AIC7XXX_NOT_YET
      /* XXX Fill these in later */
      case MESG_BUFFER_BUSY:
        break;
      case MSGIN_PHASEMIS:
        break;
#endif

      default:               /* unknown */
	printk(KERN_WARNING "scsi%d: SEQINT, INTSTAT 0x%x, SCSISIGI 0x%x.\n",
	       p->host_no, intstat, inb(SCSISIGI + base));
	break;
    }

    /*
     * Clear the sequencer interrupt and unpause the sequencer.
     */
    outb(CLRSEQINT, CLRINT + base);
    UNPAUSE_SEQUENCER(p);
  }

  if (intstat & SCSIINT)
  {
    int status = inb(SSTAT1 + base);
    scsi_id = (inb(SCSIID + base) >> 4) & 0x0F;
    channel = 'A';
    if (inb(SBLKCTL + base) & SELBUSB)
    {
      channel = 'B';
    }

    scb_index = inb(SCB_TAG + base);
    scb = (p->scb_array[scb_index]);
    if (status & SCSIRSTI)
    {
      PAUSE_SEQUENCER(p);
      printk(KERN_WARNING "scsi%d: SCSIINT - Someone reset channel %c.\n",
             p->host_no, channel);
      /*
       * Go through and abort all commands for the channel, but do not
       * reset the channel again.
       */
      aic7xxx_reset_channel(p, channel, FALSE);
      run_aborted_queue = TRUE;
    }
    else if (!(scb->state & SCB_ACTIVE) || (scb->cmd == NULL))
    {
      printk(KERN_WARNING "scsi%d: SCSIINT - No command for SCB.\n", p->host_no);
      /*
       * Turn off the interrupt and set status to zero, so that it
       * falls through the rest of the SCSIINT code.
       */
      outb(status, CLRSINT1 + base);
      UNPAUSE_SEQUENCER(p);
      outb(CLRSCSIINT, CLRINT + base);
      scb = NULL;
    }
    else if (status & SCSIPERR)
    {
      char  *phase;
      unsigned char mesg_out = MSG_NOP;
      unsigned char lastphase = inb(LASTPHASE + base);

      cmd = scb->cmd;
      switch (lastphase)
      {
        case P_DATAOUT:
          phase = "Data-Out";
          break;
        case P_DATAIN:
          phase = "Data-In";
          mesg_out = MSG_INITIATOR_DET_ERROR;
          break;
        case P_COMMAND:
          phase = "Command";
          break;
        case P_MESGOUT:
          phase = "Message-Out";
          break;
        case P_STATUS:
          phase = "Status";
          mesg_out = MSG_INITIATOR_DET_ERROR;
          break;
        case P_MESGIN:
          phase = "Message-In";
          mesg_out = MSG_MSG_PARITY_ERROR;
          break;
        default:
          phase = "unknown";
          break;
      }

      /*
       * A parity error has occurred during a data
       * transfer phase. Flag it and continue.
       */
      printk(KERN_WARNING "scsi%d: Parity error during phase %s on target %d, "
             "channel %d, lun %d.\n", p->host_no, phase,
             cmd->target, cmd->channel & 0x01, cmd->lun & 0x07);

      /*
       * We've set the hardware to assert ATN if we get a parity
       * error on "in" phases, so all we need to do is stuff the
       * message buffer with the appropriate message. In phases
       * have set mesg_out to something other than MSG_NOP.
       */
      if (mesg_out != MSG_NOP)
      {
        outb(mesg_out, MSG0 + base);
        outb(1, MSG_LEN + base);
        cmd->result = DID_PARITY << 16;
      }
      else
      {
        /*
         * Should we allow the target to make this decision for us?
         */
        cmd->result = DID_RETRY_COMMAND << 16;
      }
      aic7xxx_done(p, scb);
    }
    else if (status & SELTO)
    {
      unsigned char waiting;

      cmd = scb->cmd;

      cmd->result = (DID_TIME_OUT << 16);
      /*
       * Clear an pending messages for the timed out
       * target and mark the target as free.
       */
      ha_flags = inb(FLAGS + base);
      outb(0, MSG_LEN + base);
      aic7xxx_unbusy_target(scsi_id, channel, base);
      /*
       * Stop the selection.
       */
      outb(0, SCSISEQ + base);
      outb(0, SCB_CONTROL + base);
      outb(CLRSELTIMEO, CLRSINT1 + base);
      outb(CLRSCSIINT, CLRINT + base);

      /*
       * Shift the waiting for selection queue forward
       */
      waiting = inb(WAITING_SCBH + base);
      outb(waiting, SCBPTR + base);
      waiting = inb(SCB_NEXT + base);
      outb(waiting, WAITING_SCBH + base);

      RESTART_SEQUENCER(p);
      aic7xxx_done(p, scb);
    }
    else if (!(status & BUSFREE))
    {
      /*
       * We don't know what's going on. Turn off the
       * interrupt source and try to continue.
       */
      printk(KERN_WARNING "aic7xxx: SSTAT1(0x%x).\n", status);
      outb(status, CLRSINT1 + base);
      UNPAUSE_SEQUENCER(p);
      outb(CLRSCSIINT, CLRINT + base);
    }
  }

  if (run_aborted_queue)
    aic7xxx_done_aborted_scbs(p);

  if (intstat & CMDCMPLT)
  {
    int complete;

    /*
     * The sequencer will continue running when it
     * issues this interrupt. There may be >1 commands
     * finished, so loop until we've processed them all.
     */
    do {
      complete = inb(QOUTFIFO + base);

      scb = (p->scb_array[complete]);
      if (!(scb->state & SCB_ACTIVE) || (scb->cmd == NULL))
      {
	printk(KERN_WARNING "scsi%d: CMDCMPLT without command for SCB %d.\n"
	       "       QOUTCNT %d, QINCNT %d, SCB state 0x%x, cmd 0x%lx, "
               "pos(%d).\n", p->host_no, complete, inb(QOUTCNT + base),
               inb(QINCNT + base), scb->state, (unsigned long) scb->cmd,
               scb->position);
	outb(CLRCMDINT, CLRINT + base);
	continue;
      }
      cmd = scb->cmd;
      cmd->result |= (aic7xxx_error(cmd) << 16);
      if ((cmd->flags & WAS_SENSE) && !(cmd->flags & ASKED_FOR_SENSE))
      {
        /*
         * Got sense information.
         */
	cmd->flags &= ASKED_FOR_SENSE;
      }
      p->device_status[TARGET_INDEX(cmd)].flags |= DEVICE_SUCCESS;

      /*
       * Clear interrupt status before checking the output queue again.
       * This eliminates a race condition whereby a command could
       * complete between the queue poll and the interrupt clearing,
       * so notification of the command being complete never made it
       * back up to the kernel.
       */
      outb(CLRCMDINT, CLRINT + base);
      aic7xxx_done(p, scb);

#ifdef AIC7XXX_PROC_STATS
      /*
       * XXX: we should actually know how much actually transferred
       * XXX: for each command, but apparently that's too difficult.
       */
      actual = aic7xxx_length(cmd, 0);
      if (!(cmd->flags & WAS_SENSE) && (actual > 0))
      {
        struct aic7xxx_xferstats *sp;
        long *ptr;
        int x;

        sp = &p->stats[cmd->channel & 0x01][cmd->target & 0x0F][cmd->lun & 0x07];
        sp->xfers++;

        if (cmd->request.cmd == WRITE)
        {
          sp->w_total++;
          sp->w_total512 += (actual >> 9);
          ptr = sp->w_bins;
        }
        else
        {
          sp->r_total++;
          sp->r_total512 += (actual >> 9);
          ptr = sp->r_bins;
        }
        for (x = 9; x <= 17; x++)
        {
          if (actual < (1 << x))
          {
            ptr[x - 9]++;
            break;
          }
        }
        if (x > 17)
        {
          ptr[x - 9]++;
        }
      }
#endif /* AIC7XXX_PROC_STATS */

    } while (inb(QOUTCNT + base) & p->qcntmask);
  }
  aic7xxx_done_cmds_complete(p);
  p->flags &= ~IN_ISR;
  aic7xxx_run_waiting_queues(p);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_select_queue_depth
 *
 * Description:
 *   Sets the queue depth for each SCSI device hanging off the input
 *   host adapter.  We use a queue depth of 2 for devices that do not
 *   support tagged queueing.  If AIC7XXX_CMDS_PER_LUN is defined, we
 *   use that for tagged queueing devices; otherwise we use our own
 *   algorithm for determining the queue depth based on the maximum
 *   SCBs for the controller.
 *-F*************************************************************************/
static void aic7xxx_select_queue_depth(struct Scsi_Host *host,
    Scsi_Device *scsi_devs)
{
  Scsi_Device *device = scsi_devs;
  int tq_depth = 2;
  struct aic7xxx_host *p = (struct aic7xxx_host *) host->hostdata;

#ifdef AIC7XXX_CMDS_PER_LUN
  tq_depth = AIC7XXX_CMDS_PER_LUN;
#else
  {
    if (p->maxhscbs <= 4)
    {
      tq_depth = 4;  /* Not many SCBs to work with. */
    }
    else
    {
      tq_depth = 8;
    }
  }
#endif

  for (device = scsi_devs; device != NULL; device = device->next)
  {
    if (device->host == host)
    {
      device->queue_depth = 2;
#ifdef AIC7XXX_TAGGED_QUEUEING
      if (device->tagged_supported)
      {
        unsigned short target_mask = (1 << device->id) | device->channel;

        if (!(p->discenable & target_mask))
        {
          printk(KERN_INFO "scsi%d: Disconnection disabled, unable to enable "
                 "tagged queueing for target %d, channel %d, LUN %d.\n",
                 host->host_no, device->id, device->channel, device->lun);
        }
        else
        {
          device->queue_depth = tq_depth;
          if (device->tagged_queue == 0)
          {
            printk(KERN_INFO "scsi%d: Enabled tagged queuing for target %d, "
	           "channel %d, LUN %d, queue depth %d.\n", host->host_no,
                   device->id, device->channel, device->lun,
                   device->queue_depth);
            device->tagged_queue = 1;
            device->current_tag = SCB_LIST_NULL;
          }
        }
      }
#endif
    }
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_probe
 *
 * Description:
 *   Probing for EISA boards: it looks like the first two bytes
 *   are a manufacturer code - three characters, five bits each:
 *
 *               BYTE 0   BYTE 1   BYTE 2   BYTE 3
 *              ?1111122 22233333 PPPPPPPP RRRRRRRR
 *
 *   The characters are baselined off ASCII '@', so add that value
 *   to each to get the real ASCII code for it. The next two bytes
 *   appear to be a product and revision number, probably vendor-
 *   specific. This is what is being searched for at each port,
 *   and what should probably correspond to the ID= field in the
 *   ECU's .cfg file for the card - if your card is not detected,
 *   make sure your signature is listed in the array.
 *
 *   The fourth byte's lowest bit seems to be an enabled/disabled
 *   flag (rest of the bits are reserved?).
 *-F*************************************************************************/
static aha_type
aic7xxx_probe(int slot, int base, aha_status_type *bios)
{
  int i;
  unsigned char buf[4];

  static struct {
    int n;
    unsigned char signature[sizeof(buf)];
    aha_type type;
    int bios_disabled;
  } AIC7xxx[] = {
    { 4, { 0x04, 0x90, 0x77, 0x71 }, AIC_7771, FALSE }, /* host adapter 274x */
    { 4, { 0x04, 0x90, 0x77, 0x70 }, AIC_7770, FALSE }, /* motherboard 7770  */
    { 4, { 0x04, 0x90, 0x77, 0x56 }, AIC_284x, FALSE }, /* 284x BIOS enabled */
    { 4, { 0x04, 0x90, 0x77, 0x57 }, AIC_284x, TRUE }   /* 284x BIOS disabled */
  };

  /*
   * The VL-bus cards need to be primed by
   * writing before a signature check.
   */
  for (i = 0; i < sizeof(buf); i++)
  {
    outb(0x80 + i, base);
    buf[i] = inb(base + i);
  }

  for (i = 0; i < NUMBER(AIC7xxx); i++)
  {
    /*
     * Signature match on enabled card?
     */
    if (!memcmp(buf, AIC7xxx[i].signature, AIC7xxx[i].n))
    {
      if (inb(base + 4) & 1)
      {
        if (AIC7xxx[i].bios_disabled)
        {
          *bios = AIC_DISABLED;
        }
        else
        {
          *bios = AIC_ENABLED;
        }
	return (AIC7xxx[i].type);
      }

      printk("aic7xxx: Disabled at slot %d, ignored.\n", slot);
    }
  }

  return (AIC_NONE);
}

/*+F*************************************************************************
 * Function:
 *   read_2840_seeprom
 *
 * Description:
 *   Reads the 2840 serial EEPROM and returns 1 if successful and 0 if
 *   not successful.
 *
 *   See read_seeprom (for the 2940) for the instruction set of the 93C46
 *   chip.
 *
 *   The 2840 interface to the 93C46 serial EEPROM is through the
 *   STATUS_2840 and SEECTL_2840 registers.  The CS_2840, CK_2840, and
 *   DO_2840 bits of the SEECTL_2840 register are connected to the chip
 *   select, clock, and data out lines respectively of the serial EEPROM.
 *   The DI_2840 bit of the STATUS_2840 is connected to the data in line
 *   of the serial EEPROM.  The EEPROM_TF bit of STATUS_2840 register is
 *   useful in that it gives us an 800 nsec timer.  After a read from the
 *   SEECTL_2840 register the timing flag is cleared and goes high 800 nsec
 *   later.
 *
 *-F*************************************************************************/
static int
read_2840_seeprom(int base, struct seeprom_config *sc)
{
  int i = 0, k = 0;
  unsigned char temp;
  unsigned short checksum = 0;
  unsigned short *seeprom = (unsigned short *) sc;
  struct seeprom_cmd {
    unsigned char len;
    unsigned char bits[3];
  };
  struct seeprom_cmd seeprom_read = {3, {1, 1, 0}};

#define CLOCK_PULSE(p) \
  while ((inb(STATUS_2840 + base) & EEPROM_TF) == 0)	\
  {						\
    ;  /* Do nothing */				\
  }						\
  (void) inb(SEECTL_2840 + base);

  /*
   * Read the first 32 registers of the seeprom.  For the 2840,
   * the 93C46 SEEPROM is a 1024-bit device with 64 16-bit registers
   * but only the first 32 are used by Adaptec BIOS.  The loop
   * will range from 0 to 31.
   */
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    /*
     * Send chip select for one clock cycle.
     */
    outb(CK_2840 | CS_2840, SEECTL_2840 + base);
    CLOCK_PULSE(base);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = CS_2840 | seeprom_read.bits[i];
      outb(temp, SEECTL_2840 + base);
      CLOCK_PULSE(base);
      temp = temp ^ CK_2840;
      outb(temp, SEECTL_2840 + base);
      CLOCK_PULSE(base);
    }
    /*
     * Send the 6 bit address (MSB first, LSB last).
     */
    for (i = 5; i >= 0; i--)
    {
      temp = k;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = CS_2840 | temp;
      outb(temp, SEECTL_2840 + base);
      CLOCK_PULSE(base);
      temp = temp ^ CK_2840;
      outb(temp, SEECTL_2840 + base);
      CLOCK_PULSE(base);
    }

    /*
     * Now read the 16 bit register.  An initial 0 precedes the
     * register contents which begins with bit 15 (MSB) and ends
     * with bit 0 (LSB).  The initial 0 will be shifted off the
     * top of our word as we let the loop run from 0 to 16.
     */
    for (i = 0; i <= 16; i++)
    {
      temp = CS_2840;
      outb(temp, SEECTL_2840 + base);
      CLOCK_PULSE(base);
      temp = temp ^ CK_2840;
      seeprom[k] = (seeprom[k] << 1) | (inb(STATUS_2840 + base) & DI_2840);
      outb(temp, SEECTL_2840 + base);
      CLOCK_PULSE(base);
    }
    /*
     * The serial EEPROM has a checksum in the last word.  Keep a
     * running checksum for all words read except for the last
     * word.  We'll verify the checksum after all words have been
     * read.
     */
    if (k < (sizeof(*sc) / 2) - 1)
    {
      checksum = checksum + seeprom[k];
    }

    /*
     * Reset the chip select for the next command cycle.
     */
    outb(0, SEECTL_2840 + base);
    CLOCK_PULSE(base);
    outb(CK_2840, SEECTL_2840 + base);
    CLOCK_PULSE(base);
    outb(0, SEECTL_2840 + base);
    CLOCK_PULSE(base);
  }

#if 0
  printk("Computed checksum 0x%x, checksum read 0x%x\n", checksum, sc->checksum);
  printk("Serial EEPROM:");
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk("\n              ");
    }
    printk(" 0x%x", seeprom[k]);
  }
  printk("\n");
#endif

  if (checksum != sc->checksum)
  {
    printk("aic7xxx: SEEPROM checksum error, ignoring SEEPROM settings.\n");
    return (0);
  }

  return (1);
#undef CLOCK_PULSE
}

/*+F*************************************************************************
 * Function:
 *   read_seeprom
 *
 * Description:
 *   Reads the serial EEPROM and returns 1 if successful and 0 if
 *   not successful.
 *
 *   The instruction set of the 93C46/56/66 chips is as follows:
 *
 *               Start  OP
 *     Function   Bit  Code  Address    Data     Description
 *     -------------------------------------------------------------------
 *     READ        1    10   A5 - A0             Reads data stored in memory,
 *                                               starting at specified address
 *     EWEN        1    00   11XXXX              Write enable must precede
 *                                               all programming modes
 *     ERASE       1    11   A5 - A0             Erase register A5A4A3A2A1A0
 *     WRITE       1    01   A5 - A0   D15 - D0  Writes register
 *     ERAL        1    00   10XXXX              Erase all registers
 *     WRAL        1    00   01XXXX    D15 - D0  Writes to all registers
 *     EWDS        1    00   00XXXX              Disables all programming
 *                                               instructions
 *     *Note: A value of X for address is a don't care condition.
 *     *Note: The 93C56 and 93C66 have 8 address bits.
 * 
 *
 *   The 93C46 has a four wire interface: clock, chip select, data in, and
 *   data out.  In order to perform one of the above functions, you need
 *   to enable the chip select for a clock period (typically a minimum of
 *   1 usec, with the clock high and low a minimum of 750 and 250 nsec
 *   respectively.  While the chip select remains high, you can clock in
 *   the instructions (above) starting with the start bit, followed by the
 *   OP code, Address, and Data (if needed).  For the READ instruction, the
 *   requested 16-bit register contents is read from the data out line but
 *   is preceded by an initial zero (leading 0, followed by 16-bits, MSB
 *   first).  The clock cycling from low to high initiates the next data
 *   bit to be sent from the chip.
 *
 *   The 7870 interface to the 93C46 serial EEPROM is through the SEECTL
 *   register.  After successful arbitration for the memory port, the
 *   SEECS bit of the SEECTL register is connected to the chip select.
 *   The SEECK, SEEDO, and SEEDI are connected to the clock, data out,
 *   and data in lines respectively.  The SEERDY bit of SEECTL is useful
 *   in that it gives us an 800 nsec timer.  After a write to the SEECTL
 *   register, the SEERDY goes high 800 nsec later.  The one exception
 *   to this is when we first request access to the memory port.  The
 *   SEERDY goes high to signify that access has been granted and, for
 *   this case, has no implied timing.
 *
 *-F*************************************************************************/
static int
read_seeprom(int base, int offset, struct seeprom_config *sc,
    seeprom_chip_type chip)
{
  int i = 0, k;
  unsigned long timeout;
  unsigned char temp;
  unsigned short checksum = 0;
  unsigned short *seeprom = (unsigned short *) sc;
  struct seeprom_cmd {
    unsigned char len;
    unsigned char bits[3];
  };
  struct seeprom_cmd seeprom_read = {3, {1, 1, 0}};

#define CLOCK_PULSE(p) \
  while ((inb(SEECTL + base) & SEERDY) == 0)	\
  {						\
    ;  /* Do nothing */				\
  }

  /*
   * Request access of the memory port.  When access is
   * granted, SEERDY will go high.  We use a 1 second
   * timeout which should be near 1 second more than
   * is needed.  Reason: after the 7870 chip reset, there
   * should be no contention.
   */
  outb(SEEMS, SEECTL + base);
  timeout = jiffies + 100;  /* 1 second timeout */
  while ((jiffies < timeout) && ((inb(SEECTL + base) & SEERDY) == 0))
  {
    ; /* Do nothing!  Wait for access to be granted.  */
  }
  if ((inb(SEECTL + base) & SEERDY) == 0)
  {
    outb(0, SEECTL + base);
    return (0);
  }

  /*
   * Read the first 32 registers of the seeprom.  For the 7870,
   * the 93C46 SEEPROM is a 1024-bit device with 64 16-bit registers
   * but only the first 32 are used by Adaptec BIOS.  The loop
   * will range from 0 to 31.
   */
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    /*
     * Send chip select for one clock cycle.
     */
    outb(SEEMS | SEECK | SEECS, SEECTL + base);
    CLOCK_PULSE(base);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = SEEMS | SEECS | (seeprom_read.bits[i] << 1);
      outb(temp, SEECTL + base);
      CLOCK_PULSE(base);
      temp = temp ^ SEECK;
      outb(temp, SEECTL + base);
      CLOCK_PULSE(base);
    }
    /*
     * Send the 6 bit address (MSB first, LSB last).
     */
    for (i = ((int) chip - 1); i >= 0; i--)
    {
      temp = k + offset;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = SEEMS | SEECS | (temp << 1);
      outb(temp, SEECTL + base);
      CLOCK_PULSE(base);
      temp = temp ^ SEECK;
      outb(temp, SEECTL + base);
      CLOCK_PULSE(base);
    }

    /*
     * Now read the 16 bit register.  An initial 0 precedes the
     * register contents which begins with bit 15 (MSB) and ends
     * with bit 0 (LSB).  The initial 0 will be shifted off the
     * top of our word as we let the loop run from 0 to 16.
     */
    for (i = 0; i <= 16; i++)
    {
      temp = SEEMS | SEECS;
      outb(temp, SEECTL + base);
      CLOCK_PULSE(base);
      temp = temp ^ SEECK;
      seeprom[k] = (seeprom[k] << 1) | (inb(SEECTL + base) & SEEDI);
      outb(temp, SEECTL + base);
      CLOCK_PULSE(base);
    }

    /*
     * The serial EEPROM has a checksum in the last word.  Keep a
     * running checksum for all words read except for the last
     * word.  We'll verify the checksum after all words have been
     * read.
     */
    if (k < (sizeof(*sc) / 2) - 1)
    {
      checksum = checksum + seeprom[k];
    }

    /*
     * Reset the chip select for the next command cycle.
     */
    outb(SEEMS, SEECTL + base);
    CLOCK_PULSE(base);
    outb(SEEMS | SEECK, SEECTL + base);
    CLOCK_PULSE(base);
    outb(SEEMS, SEECTL + base);
    CLOCK_PULSE(base);
  }

  /*
   * Release access to the memory port and the serial EEPROM.
   */
  outb(0, SEECTL + base);

#if 0
  printk("Computed checksum 0x%x, checksum read 0x%x\n", checksum, sc->checksum);
  printk("Serial EEPROM:");
  for (k = 0; k < (sizeof(*sc) / 2); k++)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk("\n              ");
    }
    printk(" 0x%x", seeprom[k]);
  }
  printk("\n");
#endif

  if (checksum != sc->checksum)
  {
    return (0);
  }

  return (1);
#undef CLOCK_PULSE
}

/*+F*************************************************************************
 * Function:
 *   detect_maxscb
 *
 * Description:
 *   Detects the maximum number of SCBs for the controller and returns
 *   the count and a mask in config (config->maxscbs, config->qcntmask).
 *-F*************************************************************************/
static void
detect_maxscb(struct aic7xxx_host_config *config)
{
  unsigned char sblkctl_reg;
  int base, i;

#ifdef AIC7XXX_PAGE_ENABLE
  config->flags |= PAGE_ENABLED;
#endif
  base = config->base;
  switch (config->type)
  {
    case AIC_7770:
    case AIC_7771:
    case AIC_284x:
      /*
       * Check for Rev C or E boards. Rev E boards can supposedly have
       * more than 4 SCBs, while the Rev C boards are limited to 4 SCBs.
       * It's still not clear extactly what is different about the Rev E
       * boards, but we think it allows 8 bit entries in the QOUTFIFO to
       * support "paging" SCBs (more than 4 commands can be active at once).
       *
       * The Rev E boards have a read/write autoflush bit in the
       * SBLKCTL register, while in the Rev C boards it is read only.
       */
      sblkctl_reg = inb(SBLKCTL + base) ^ AUTOFLUSHDIS;
      outb(sblkctl_reg, SBLKCTL + base);
      if (inb(SBLKCTL + base) == sblkctl_reg)
      {
        /*
         * We detected a Rev E board, we allow paging on this board.
         */
        printk(KERN_INFO "aic7xxx: %s Rev E and subsequent.\n",
               board_names[config->type]);
	outb(sblkctl_reg ^ AUTOFLUSHDIS, SBLKCTL + base);
      }
      else
      {
        /* Do not allow paging. */
        config->flags &= ~PAGE_ENABLED;
        printk(KERN_INFO "aic7xxx: %s Rev C and previous.\n",
               board_names[config->type]);
      }
      break;

    default:
      break;
  }

  /*
   * Walk the SCBs to determine how many there are.
   */
  i = 1;
  outb(0, SCBPTR + base);
  outb(0, SCBARRAY + base);

  while (i < AIC7XXX_MAXSCB)
  {
    outb(i, SCBPTR + base);
    outb(i, SCBARRAY + base);
    if (inb(SCBARRAY + base) != i)
      break;
    outb(0, SCBPTR + base);
    if (inb(SCBARRAY + base) != 0)
      break;

    outb(i, SCBPTR + base);      /* Clear the control byte. */
    outb(0, SCBARRAY + base);

    config->qcntmask |= i;       /* Update the count mask. */
    i++;
  }
  outb(i, SCBPTR + base);   /* Ensure we clear the control bytes. */
  outb(0, SCBARRAY + base);
  outb(0, SCBPTR + base); 
  outb(0, SCBARRAY + base);

  config->maxhscbs = i;
  config->qcntmask |= i;
  if ((config->flags & PAGE_ENABLED) && (config->maxhscbs < AIC7XXX_MAXSCB))
  {
    config->maxscbs = AIC7XXX_MAXSCB;
  }
  else
  {
    config->flags &= ~PAGE_ENABLED;  /* Disable paging if we have 255 SCBs!. */
    config->maxscbs = config->maxhscbs;
  }

  printk(KERN_INFO "aic7xxx: Memory check yields %d SCBs", config->maxhscbs);
  if (config->flags & PAGE_ENABLED)
    printk(", %d page-enabled SCBs.\n", config->maxscbs);
  else
    printk(", paging not enabled.\n");

}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_register
 *
 * Description:
 *   Register a Adaptec aic7xxx chip SCSI controller with the kernel.
 *-F*************************************************************************/
static int
aic7xxx_register(Scsi_Host_Template *template,
    struct aic7xxx_host_config *config)
{
  int i;
  unsigned char sblkctl, flags = 0;
  int max_targets;
  int found = 1;
  unsigned int sram, base;
  unsigned char target_settings;
  unsigned char scsi_conf, host_conf;
  unsigned short ultraenable = 0;
  int have_seeprom = FALSE;
  struct Scsi_Host *host;
  struct aic7xxx_host *p;
  struct seeprom_config sc;

  base = config->base;

  /*
   * Lock out other contenders for our i/o space.
   */
  request_region(base, MAXREG - MINREG, "aic7xxx");

  switch (config->type)
  {
    case AIC_7770:
    case AIC_7771:
      /*
       * Use the boot-time option for the interrupt trigger type.  If not
       * supplied (-1), then we use BIOS settings to determine the interrupt
       * trigger type (level or edge) and use this value for pausing and
       * unpausing the sequencer.
       */
      switch (aic7xxx_irq_trigger)
      {
        case  0: config->unpause = INTEN;          /* Edge */
                 break;
        case  1: config->unpause = IRQMS | INTEN;  /* Level */
                 break;
        case -1:
        default: config->unpause = (inb(HCNTRL + base) & IRQMS) | INTEN; 
                 break;
      }
      config->pause = config->unpause | PAUSE;

      /*
       * For some 274x boards, we must clear the CHIPRST bit and pause
       * the sequencer. For some reason, this makes the driver work.
       * For 284x boards, we give it a CHIPRST just like the 294x boards.
       */
      outb(config->pause | CHIPRST, HCNTRL + base);
      aic7xxx_delay(1);
      if (inb(HCNTRL + base) & CHIPRST)
      {
	printk(KERN_INFO "aic7xxx: Chip reset not cleared; clearing manually.\n");
      }
      outb(config->pause, HCNTRL + base);

      /*
       * Just to be on the safe side with the 274x, we will re-read the irq
       * since there was some issue about resetting the board.
       */
      config->irq = inb(INTDEF + base) & 0x0F;
      if ((config->type == AIC_7771) &&
          (inb(HA_274_BIOSCTRL + base) & BIOSMODE) == BIOSDISABLED)
      {
        config->bios = AIC_DISABLED;
        config->flags |= USE_DEFAULTS;
      }
      else
      {
        host_conf = inb(HOSTCONF + base);
        config->bus_speed = host_conf & DFTHRSH;
        config->busrtime = (host_conf << 2) & BOFF;
      }

      /*
       * Setup the FIFO threshold and the bus off time
       */
      outb(config->bus_speed & DFTHRSH, BUSSPD + base);
      outb(config->busrtime, BUSTIME + base);

      /*
       * A reminder until this can be detected automatically.
       */
      printk(KERN_INFO "aic7xxx: Extended translation %sabled.\n",
	     (config->flags & EXTENDED_TRANSLATION) ? "en" : "dis");
      break;

    case AIC_284x:
      outb(CHIPRST, HCNTRL + base);
      config->unpause = UNPAUSE_284X;
      config->pause = REQ_PAUSE; /* DWG would like to be like the rest */
      aic7xxx_delay(1);
      outb(config->pause, HCNTRL + base);

      config->parity = AIC_ENABLED;
      config->irq = inb(INTDEF + base) & 0x0F;
      host_conf = inb(HOSTCONF + base);

      printk(KERN_INFO "aic7xxx: Reading SEEPROM...");
      have_seeprom = read_2840_seeprom(base, &sc);
      if (!have_seeprom)
      {
	printk("aic7xxx: Unable to read SEEPROM.\n");
      }
      else
      {
	printk("done.\n");
        config->flags |= HAVE_SEEPROM;
        if (sc.bios_control & CF284XEXTEND)
          config->flags |= EXTENDED_TRANSLATION;
        if (!(sc.bios_control & CFBIOSEN))
        {
          /*
           * The BIOS is disabled; the values left over in scratch
           * RAM are still valid.  Do not use defaults as in the
           * AIC-7770 case.
           */
          config->bios = AIC_DISABLED;
        }
        else
        {
	  config->parity = (sc.adapter_control & CFSPARITY) ?
			   AIC_ENABLED : AIC_DISABLED;
	  config->low_term = (sc.adapter_control & CF284XSTERM) ?
			        AIC_ENABLED : AIC_DISABLED;
	  /*
	   * XXX - Adaptec *does* make 284x wide controllers, but the
	   *       documents do not say where the high byte termination
	   *       enable bit is located.
           */
        }
      }

      host_conf = inb(HOSTCONF + base);
      config->bus_speed = host_conf & DFTHRSH;
      config->busrtime = (host_conf << 2) & BOFF;

      /*
       * Setup the FIFO threshold and the bus off time
       */
      outb(config->bus_speed & DFTHRSH, BUSSPD + base);
      outb(config->busrtime, BUSTIME + base);

      printk(KERN_INFO "aic7xxx: Extended translation %sabled.\n",
	     (config->flags & EXTENDED_TRANSLATION) ? "en" : "dis");
      break;

    case AIC_7860:
    case AIC_7861:
    case AIC_7880:
    case AIC_7881:
    case AIC_7882:
    case AIC_7883:
    case AIC_7884:
      /*
       * Remember if Ultra was enabled in case there is no SEEPROM.
       * Fall through to the rest of the AIC_78xx code.
       */
      if ((inb(SXFRCTL0 + base) & ULTRAEN) || aic7xxx_enable_ultra)
        config->flags |= ULTRA_ENABLED;

    case AIC_7850:
    case AIC_7855:
    case AIC_7870:
    case AIC_7871:
    case AIC_7872:
    case AIC_7873:
    case AIC_7874:
      /*
       * Grab the SCSI ID before chip reset in case there is no SEEPROM.
       */
      config->scsi_id = inb(SCSIID + base) & OID;
      outb(CHIPRST, HCNTRL + base);
      config->unpause = UNPAUSE_294X;
      config->pause = config->unpause | PAUSE;
      aic7xxx_delay(1);
      outb(config->pause, HCNTRL + base);

      config->parity = AIC_ENABLED;

      printk(KERN_INFO "aic7xxx: Reading SEEPROM...");
      if ((config->type == AIC_7873) || (config->type == AIC_7883))
      {
        have_seeprom = read_seeprom(base, config->chan_num * (sizeof(sc) / 2),
                                    &sc, c56_66);
      }
      else
      {
        have_seeprom = read_seeprom(base, config->chan_num * (sizeof(sc) / 2),
                                    &sc, c46);
      }
      if (!have_seeprom)
      {
        for (sram = base + TARG_SCRATCH; sram < base + 0x60; sram++)
        {
          if (inb(sram) != 0x00)
            break;
        }
        if (sram == base + TARG_SCRATCH)
        {
          for (sram = base + TARG_SCRATCH; sram < base + 0x60; sram++)
          {
            if (inb(sram) != 0xFF)
              break;
          }
        }
        if ((sram != base + 0x60) && (config->scsi_id != 0))
        {
          config->flags &= ~USE_DEFAULTS;
	  printk("\naic7xxx: Unable to read SEEPROM; "
                 "using leftover BIOS values.\n");
        }
        else
        {
          printk("\n");
          printk(KERN_INFO "aic7xxx: Unable to read SEEPROM; using default "
                 "settings.\n");
          config->flags |= USE_DEFAULTS;
          config->flags &= ~ULTRA_ENABLED;
          config->scsi_id = 7;
        }
        scsi_conf = ENSPCHK | RESET_SCSI;
      }
      else
      {
	printk("done.\n");
        config->flags |= HAVE_SEEPROM;
        if (!(sc.bios_control & CFBIOSEN))
        {
          /*
           * The BIOS is disabled; the values left over in scratch
           * RAM are still valid.  Do not use defaults as in the
           * AIC-7770 case.
           */
          config->bios = AIC_DISABLED;
          scsi_conf = ENSPCHK | RESET_SCSI;
        }
        else
        {
          scsi_conf = 0;
          if (sc.adapter_control & CFRESETB)
            scsi_conf |= RESET_SCSI;
          if (sc.adapter_control & CFSPARITY)
            scsi_conf |= ENSPCHK;
	  if (sc.bios_control & CFEXTEND)
            config->flags |= EXTENDED_TRANSLATION;
	  config->scsi_id = (sc.brtime_id & CFSCSIID);
	  config->parity = (sc.adapter_control & CFSPARITY) ?
			     AIC_ENABLED : AIC_DISABLED;
	  config->low_term = (sc.adapter_control & CFSTERM) ?
			       AIC_ENABLED : AIC_DISABLED;
	  config->high_term = (sc.adapter_control & CFWSTERM) ?
			        AIC_ENABLED : AIC_DISABLED;
	  config->busrtime = ((sc.brtime_id & CFBRTIME) >> 8);
          if (((config->type == AIC_7880) || (config->type == AIC_7881) ||
               (config->type == AIC_7882) || (config->type == AIC_7883) ||
               (config->type == AIC_7884)) && (sc.adapter_control & CFULTRAEN))
          {
            printk(KERN_INFO "aic7xxx: Enabling support for Ultra SCSI "
                   "speed.\n");
            config->flags |= ULTRA_ENABLED;
          }
        }
      }

      outb(scsi_conf | (config->scsi_id & 0x07), SCSICONF + base);
      config->bus_speed = DFTHRSH_100;
      outb(config->bus_speed, DSPCISTATUS + base);

      /*
       * In case we are a wide card...
       */
      outb(config->scsi_id, SCSICONF + base + 1);

      printk(KERN_INFO "aic7xxx: Extended translation %sabled.\n",
	     (config->flags & EXTENDED_TRANSLATION) ? "en" : "dis");
      break;

    default:
      panic(KERN_WARNING "aic7xxx: (aic7xxx_register) Internal error.\n");
  }

  detect_maxscb(config);

  if (config->chip_type == AIC_777x)
  {
    if (config->pause & IRQMS)
    {
      printk(KERN_INFO "aic7xxx: Using level sensitive interrupts.\n");
    }
    else
    {
      printk(KERN_INFO "aic7xxx: Using edge triggered interrupts.\n");
    }
  }

  /*
   * Read the bus type from the SBLKCTL register. Set the FLAGS
   * register in the sequencer for twin and wide bus cards.
   */
  sblkctl = inb(SBLKCTL + base);
  if (config->flags & PAGE_ENABLED)
    flags = PAGESCBS;

  switch (sblkctl & SELBUS_MASK)
  {
    case SELNARROW:     /* narrow/normal bus */
      config->scsi_id = inb(SCSICONF + base) & 0x07;
      config->bus_type = AIC_SINGLE;
      outb(flags | SINGLE_BUS, FLAGS + base);
      break;

    case SELWIDE:     /* Wide bus */
      config->scsi_id = inb(SCSICONF + base + 1) & 0x0F;
      config->bus_type = AIC_WIDE;
      printk("aic7xxx: Enabling wide channel of %s-Wide.\n",
	     board_names[config->type]);
      outb(flags | WIDE_BUS, FLAGS + base);
      break;

    case SELBUSB:     /* Twin bus */
      config->scsi_id = inb(SCSICONF + base) & 0x07;
#ifdef AIC7XXX_TWIN_SUPPORT
      config->scsi_id_b = inb(SCSICONF + base + 1) & 0x07;
      config->bus_type = AIC_TWIN;
      printk(KERN_INFO "aic7xxx: Enabled channel B of %s-Twin.\n",
	     board_names[config->type]);
      outb(flags | TWIN_BUS, FLAGS + base);
#else
      config->bus_type = AIC_SINGLE;
      printk(KERN_INFO "aic7xxx: Channel B of %s-Twin will be ignored.\n",
	     board_names[config->type]);
      outb(flags, FLAGS + base);
#endif
      break;

    default:
      printk(KERN_WARNING "aic7xxx: Unsupported type 0x%x, please "
	     "mail deang@teleport.com\n", inb(SBLKCTL + base));
      outb(0, FLAGS + base);
      return (0);
  }

  /*
   * For the 294x cards, clearing DIAGLEDEN and DIAGLEDON, will
   * take the card out of diagnostic mode and make the host adapter
   * LED follow bus activity (will not always be on).
   */
  outb(sblkctl & ~(DIAGLEDEN | DIAGLEDON), SBLKCTL + base);

  /*
   * The IRQ level in i/o port 4 maps directly onto the real
   * IRQ number. If it's ok, register it with the kernel.
   *
   * NB. the Adaptec documentation says the IRQ number is only
   *     in the lower four bits; the ECU information shows the
   *     high bit being used as well. Which is correct?
   *
   * The PCI cards get their interrupt from PCI BIOS.
   */
  if ((config->chip_type == AIC_777x) && ((config->irq < 9) || (config->irq > 15)))
  {
    printk(KERN_WARNING "aic7xxx: Host adapter uses unsupported IRQ level, "
          "ignoring.\n");
    return (0);
  }

  /*
   * Print out debugging information before re-enabling
   * the card - a lot of registers on it can't be read
   * when the sequencer is active.
   */
  debug_config(config);

  /*
   * Register each "host" and fill in the returned Scsi_Host
   * structure as best we can. Some of the parameters aren't
   * really relevant for bus types beyond ISA, and none of the
   * high-level SCSI code looks at it anyway. Why are the fields
   * there? Also save the pointer so that we can find the
   * information when an IRQ is triggered.
   */
  host = scsi_register(template, sizeof(struct aic7xxx_host));
  host->can_queue = config->maxscbs;
  host->cmd_per_lun = 2;
  host->select_queue_depths = aic7xxx_select_queue_depth;
  host->this_id = config->scsi_id;
  host->io_port = config->base;
  host->n_io_port = 0xFF;
  host->base = (unsigned char *)config->mbase;
  host->irq = config->irq;
  if (config->bus_type == AIC_WIDE)
  {
    host->max_id = 16;
  }
  if (config->bus_type == AIC_TWIN)
  {
    host->max_channel = 1;
  }

  p = (struct aic7xxx_host *) host->hostdata;

  p->host = host;
  p->host_no = (int)host->host_no;
  p->isr_count = 0;
  p->base = base;
  p->maxscbs = config->maxscbs;
  p->maxhscbs = config->maxhscbs;
  p->qcntmask = config->qcntmask;
  p->mbase = (char *)config->mbase;
  p->type = config->type;
  p->chip_type = config->chip_type;
  p->flags = config->flags;
  p->chan_num = config->chan_num;
  p->scb_link = &(p->scb_usage);
#ifdef AIC7XXX_SHARE_SCBS
  if ((p->chan_num == 0) && ((p->type == AIC_7873) | (p->type == AIC_7883)))
  {
    shared_3985_scbs = &(p->scb_usage);
    p->scb_link = &(p->scb_usage);
  }
#endif
  p->scb_link->numscbs = 0;
  p->bus_type = config->bus_type;
  p->seeprom = sc;
  p->next = NULL;
  p->completeq.head = NULL;
  p->completeq.tail = NULL;
  scbq_init(&p->scb_link->free_scbs);
  scbq_init(&p->page_scbs);
  scbq_init(&p->waiting_scbs);
  scbq_init(&p->assigned_scbs);

  p->unpause = config->unpause;
  p->pause = config->pause;

  for (i = 0; i <= 15; i++)
  {
    p->device_status[i].commands_sent = 0;
    p->device_status[i].flags = 0;
    p->device_status[i].last_reset = 0;
  }
  if (aic7xxx_boards[config->irq] == NULL)
  {
    /*
     * Warning! This must be done before requesting the irq.  It is
     * possible for some boards to raise an interrupt as soon as
     * they are enabled.  So when we request the irq from the Linux
     * kernel, an interrupt is triggered immediately.  Therefore, we
     * must ensure the board data is correctly set before the request.
     */
    aic7xxx_boards[config->irq] = host;

    /*
     * Register IRQ with the kernel.
     */
    if (request_irq(config->irq, aic7xxx_isr, SA_INTERRUPT | SA_SHIRQ,
       "aic7xxx", NULL))
    {
      printk(KERN_WARNING "aic7xxx: Couldn't register IRQ %d, ignoring.\n",
             config->irq);
      aic7xxx_boards[config->irq] = NULL;
      return (0);
    }
  }
  else
  {
    /*
     * We have found a host adapter sharing an IRQ of a previously
     * registered host adapter. Add this host adapter's Scsi_Host
     * to the beginning of the linked list of hosts at the same IRQ.
     */
    p->next = aic7xxx_boards[config->irq];
    aic7xxx_boards[config->irq] = host;
  }

  /*
   * Load the sequencer program, then re-enable the board -
   * resetting the AIC-7770 disables it, leaving the lights
   * on with nobody home. On the PCI bus you *may* be home,
   * but then your mailing address is dynamically assigned
   * so no one can find you anyway :-)
   */
  printk(KERN_INFO "aic7xxx: Downloading sequencer code...");
  aic7xxx_loadseq(base);

  /*
   * Set Fast Mode and Enable the board
   */
  outb(FASTMODE, SEQCTL + base);

  if (p->chip_type == AIC_777x)
  {
    outb(ENABLE, BCTL + base);
  }

  printk("done.\n");

  /*
   * Set the SCSI Id, SXFRCTL0, SXFRCTL1, and SIMODE1, for both channels
   */
  if (p->bus_type == AIC_TWIN)
  {
    /*
     * Select Channel B.
     */
    outb((sblkctl & ~SELBUS_MASK) | SELBUSB, SBLKCTL + base);

    outb(config->scsi_id_b, SCSIID + base);
    scsi_conf = inb(SCSICONF + base + 1) & (ENSPCHK | STIMESEL);
    outb(scsi_conf | ENSTIMER | ACTNEGEN | STPWEN, SXFRCTL1 + base);
#if 1
    outb(ENSELTIMO | ENSCSIRST | ENSCSIPERR, SIMODE1 + base);
#else
    outb(ENSELTIMO, SIMODE1 + base);
#endif
    if (p->flags & ULTRA_ENABLED)
    {
      outb(DFON | SPIOEN | ULTRAEN, SXFRCTL0 + base);
    }
    else
    {
      outb(DFON | SPIOEN, SXFRCTL0 + base);
    }

    /*
     * Select Channel A
     */
    outb((sblkctl & ~SELBUS_MASK) | SELNARROW, SBLKCTL + base);
  }
  outb(config->scsi_id, SCSIID + base);
  scsi_conf = inb(SCSICONF + base) & (ENSPCHK | STIMESEL);
  outb(scsi_conf | ENSTIMER | ACTNEGEN | STPWEN, SXFRCTL1 + base);
#if 1
  outb(ENSELTIMO | ENSCSIRST | ENSCSIPERR, SIMODE1 + base);
#else
  outb(ENSELTIMO, SIMODE1 + base);
#endif
  if (p->flags & ULTRA_ENABLED)
  {
    outb(DFON | SPIOEN | ULTRAEN, SXFRCTL0 + base);
  }
  else
  {
    outb(DFON | SPIOEN, SXFRCTL0 + base);
  }

  /*
   * Look at the information that board initialization or the board
   * BIOS has left us. In the lower four bits of each target's
   * scratch space any value other than 0 indicates that we should
   * initiate synchronous transfers. If it's zero, the user or the
   * BIOS has decided to disable synchronous negotiation to that
   * target so we don't activate the needsdtr flag.
   */
  p->needsdtr_copy = 0x0;
  p->sdtr_pending = 0x0;
  p->needwdtr_copy = 0x0;
  p->wdtr_pending = 0x0;
  if (p->bus_type == AIC_SINGLE)
  {
    max_targets = 8;
  }
  else
  {
    max_targets = 16;
  }

  /*
   * Grab the disconnection disable table and invert it for our needs
   */
  if (have_seeprom)
  {
    p->discenable = 0x0;
  }
  else
  {
    if (config->bios == AIC_DISABLED)
    {
      printk(KERN_INFO "aic7xxx : Host adapter BIOS disabled. Using default SCSI "
             "device parameters.\n");
      p->discenable = 0xFFFF;
    }
    else
    {
      p->discenable = ~((inb(DISC_DSB + base + 1) << 8) |
          inb(DISC_DSB + base));
    }
  }

  for (i = 0; i < max_targets; i++)
  {
    if (config->flags & USE_DEFAULTS)
    {
      target_settings = 0;  /* 10 MHz */
      p->needsdtr_copy |= (0x01 << i);
      p->needwdtr_copy |= (0x01 << i);
    }
    else
    {
      if (have_seeprom)
      {
  	target_settings = ((sc.device_flags[i] & CFXFER) << 4);
  	if (sc.device_flags[i] & CFSYNCH)
  	{
  	  p->needsdtr_copy |= (0x01 << i);
  	}
  	if (sc.device_flags[i] & CFWIDEB)
  	{
  	  p->needwdtr_copy |= (0x01 << i);
  	}
  	if (sc.device_flags[i] & CFDISC)
        {
          p->discenable |= (0x01 << i);
        }
      }
      else
      {
        target_settings = inb(TARG_SCRATCH + base + i);
        if (target_settings & 0x0F)
        {
          p->needsdtr_copy |= (0x01 << i);
          /*
           * Default to asynchronous transfers (0 offset)
           */
          target_settings &= 0xF0;
        }
        if (target_settings & 0x80)
        {
          p->needwdtr_copy |= (0x01 << i);
          target_settings &= 0x7F;
        }
      }
      if (p->flags & ULTRA_ENABLED)
      {
        switch (target_settings & 0x70)
        {
          case 0x00:
          case 0x10:
          case 0x20:
            ultraenable |= (0x01 << i);
            break;
          case 0x40:
            target_settings &= ~(0x70);
            break;
          default:
            break;
        }
      }
    }
    outb(target_settings, (TARG_SCRATCH + base + i));
  }

  /*
   * If we are not wide, forget WDTR. This makes the driver
   * work on some cards that don't leave these fields cleared
   * when BIOS is not installed.
   */
  if (p->bus_type != AIC_WIDE)
  {
    p->needwdtr_copy = 0;
  }
  p->needsdtr = p->needsdtr_copy;
  p->needwdtr = p->needwdtr_copy;
  p->orderedtag = 0;
#if 0
  printk("NeedSdtr = 0x%x, 0x%x\n", p->needsdtr_copy, p->needsdtr);
  printk("NeedWdtr = 0x%x, 0x%x\n", p->needwdtr_copy, p->needwdtr);
#endif
  outb(ultraenable & 0xFF, ULTRA_ENB + base);
  outb((ultraenable >> 8) & 0xFF, ULTRA_ENB + base + 1);

  /*
   * Set the number of available SCBs.
   */
  outb(config->maxhscbs, SCBCOUNT + base);

  /*
   * 2s compliment of maximum tag value.
   */
  i = p->maxscbs;
  outb(-i & 0xFF, COMP_SCBCOUNT + base);

  /*
   * Set the QCNT (queue count) mask to deal with broken aic7850s that
   * sporatically get garbage in the upper bits of their QCNT registers.
   */
  outb(config->qcntmask, QCNTMASK + base);

  /*
   * Clear the active flags - no targets are busy.
   */
  outb(0, ACTIVE_A + base);
  outb(0, ACTIVE_B + base);

  /*
   * We don't have any waiting selections or disconnected SCBs.
   */
  outb(SCB_LIST_NULL, WAITING_SCBH + base);
  outb(SCB_LIST_NULL, DISCONNECTED_SCBH + base);

  /*
   * Message out buffer starts empty
   */
  outb(0, MSG_LEN + base);

  /*
   * Reset the SCSI bus. Is this necessary?
   *   There may be problems for a warm boot without resetting
   *   the SCSI bus. Either BIOS settings in scratch RAM
   *   will not get reinitialized, or devices may stay at
   *   previous negotiated settings (SDTR and WDTR) while
   *   the driver will think that no negotiations have been
   *   performed.
   *
   * Some devices need a long time to "settle" after a SCSI
   * bus reset.
   */
  if (!aic7xxx_no_reset)
  {
    printk("aic7xxx: Resetting the SCSI bus...");
    if (p->bus_type == AIC_TWIN)
    {
      /*
       * Select Channel B.
       */
      outb((sblkctl & ~SELBUS_MASK) | SELBUSB, SBLKCTL + base);

      outb(SCSIRSTO, SCSISEQ + base);
      udelay(1000);
      outb(0, SCSISEQ + base);

      /* Ensure we don't get a RSTI interrupt from this. */
      outb(CLRSCSIRSTI, CLRSINT1 + base);
      outb(CLRSCSIINT, CLRINT + base);

     /*
       * Select Channel A.
       */
      outb((sblkctl & ~SELBUS_MASK) | SELNARROW, SBLKCTL + base);
    }

    outb(SCSIRSTO, SCSISEQ + base);
    udelay(1000);
    outb(0, SCSISEQ + base);

    /* Ensure we don't get a RSTI interrupt from this. */
    outb(CLRSCSIRSTI, CLRSINT1 + base);
    outb(CLRSCSIINT, CLRINT + base);

    aic7xxx_delay(AIC7XXX_RESET_DELAY);

    printk("done.\n");
  }

  /*
   * Unpause the sequencer before returning and enable
   * interrupts - we shouldn't get any until the first
   * command is sent to us by the high-level SCSI code.
   */
  UNPAUSE_SEQUENCER(p);
  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_detect
 *
 * Description:
 *   Try to detect and register an Adaptec 7770 or 7870 SCSI controller.
 *-F*************************************************************************/
int
aic7xxx_detect(Scsi_Host_Template *template)
{
  int found = 0, slot, base;
  unsigned char irq = 0;
  int i;
  struct aic7xxx_host_config config;

  template->proc_dir = &proc_scsi_aic7xxx;
  config.chan_num = 0;

  /*
   * Since we may allow sharing of IRQs, it is imperative
   * that we "null-out" the aic7xxx_boards array. It is
   * not guaranteed to be initialized to 0 (NULL). We use
   * a NULL entry to indicate that no prior hosts have
   * been found/registered for that IRQ.
   */
  for (i = 0; i <= NUMBER(aic7xxx_boards); i++)
  {
    aic7xxx_boards[i] = NULL;
  }

  /*
   * Initialize the spurious count to 0.
   */
  aic7xxx_spurious_count = 0;

  /*
   * EISA/VL-bus card signature probe.
   */
  for (slot = MINSLOT; slot <= MAXSLOT; slot++)
  {
    base = SLOTBASE(slot) + MINREG;

    if (check_region(base, MAXREG - MINREG))
    {
      /*
       * Some other driver has staked a
       * claim to this i/o region already.
       */
      continue;
    }

    config.type = aic7xxx_probe(slot, HID0 + base, &(config.bios));
    if (config.type != AIC_NONE)
    {
      /*
       * We found a card, allow 1 spurious interrupt.
       */
      aic7xxx_spurious_count = 1;

      /*
       * We "find" a AIC-7770 if we locate the card
       * signature and we can set it up and register
       * it with the kernel without incident.
       */
      config.chip_type = AIC_777x;
      config.base = base;
      config.mbase = 0;
      config.irq = irq;
      config.parity = AIC_ENABLED;
      config.low_term = AIC_UNKNOWN;
      config.high_term = AIC_UNKNOWN;
      config.flags = 0;
      if (aic7xxx_extended)
        config.flags |= EXTENDED_TRANSLATION;
      config.bus_speed = DFTHRSH_100;
      config.busrtime = BOFF;
      found += aic7xxx_register(template, &config);

      /*
       * Disallow spurious interrupts.
       */
      aic7xxx_spurious_count = 0;
    }
  }

#ifdef CONFIG_PCI
  /*
   * PCI-bus probe.
   */
  if (pcibios_present())
  {
    struct
    {
      unsigned short vendor_id;
      unsigned short device_id;
      aha_type       card_type;
      aha_chip_type  chip_type;
    } const aic7xxx_pci_devices[] = {
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7850, AIC_7850, AIC_785x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7855, AIC_7855, AIC_785x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7860, AIC_7860, AIC_785x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7861, AIC_7861, AIC_785x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7870, AIC_7870, AIC_787x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7871, AIC_7871, AIC_787x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7872, AIC_7872, AIC_787x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7873, AIC_7873, AIC_787x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7874, AIC_7874, AIC_787x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7880, AIC_7880, AIC_788x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7881, AIC_7881, AIC_788x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7882, AIC_7882, AIC_788x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7883, AIC_7883, AIC_788x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7884, AIC_7884, AIC_788x}
    };

    int error;
    int done = 0;
    unsigned int iobase, mbase;
    unsigned short index = 0;
    unsigned char pci_bus, pci_device_fn;
    unsigned int  csize_lattime;
    unsigned int  class_revid;
    unsigned int  devconfig;
    char rev_id[] = {'B', 'C', 'D'};

    for (i = 0; i < NUMBER(aic7xxx_pci_devices); i++)
    {
      done = FALSE;
      while (!done)
      {
        if (pcibios_find_device(aic7xxx_pci_devices[i].vendor_id,
                                aic7xxx_pci_devices[i].device_id,
                                index, &pci_bus, &pci_device_fn))
        {
          index = 0;
          done = TRUE;
        }
        else  /* Found an Adaptec PCI device. */
        {
          config.type = aic7xxx_pci_devices[i].card_type;
          config.chip_type = aic7xxx_pci_devices[i].chip_type;
          config.chan_num = 0;
          config.bios = AIC_ENABLED;  /* Assume bios is enabled. */
          config.flags = 0;
          config.busrtime = 40;
          switch (config.type)
          {
            case AIC_7850:
            case AIC_7855:
            case AIC_7860:
            case AIC_7861:
              config.bios = AIC_DISABLED;
              config.flags |= USE_DEFAULTS;
              config.bus_speed = DFTHRSH_100;
              break;

            case AIC_7872:  /* 3940 */
            case AIC_7882:  /* 3940-Ultra */
              config.chan_num = number_of_3940s & 0x1;  /* Has 2 controllers */
              number_of_3940s++;
              break;

            case AIC_7873:  /* 3985 */
            case AIC_7883:  /* 3985-Ultra */
              config.chan_num = number_of_3985s;  /* Has 3 controllers */
              number_of_3985s++;
              if (number_of_3985s == 3)
              {
                number_of_3985s = 0;
              }
              break;

            default:
              break;
          }

          /*
           * Read sundry information from PCI BIOS.
           */
          error = pcibios_read_config_dword(pci_bus, pci_device_fn,
                                            PCI_BASE_ADDRESS_0, &iobase);
          error += pcibios_read_config_byte(pci_bus, pci_device_fn,
                                            PCI_INTERRUPT_LINE, &irq);
          error += pcibios_read_config_dword(pci_bus, pci_device_fn,
                                            PCI_BASE_ADDRESS_1, &mbase);

          /*
           * The first bit of PCI_BASE_ADDRESS_0 is always set, so
           * we mask it off.
           */
          iobase &= PCI_BASE_ADDRESS_IO_MASK;

          /*
           * Read the PCI burst size and latency timer.
           */
          error += pcibios_read_config_dword(pci_bus, pci_device_fn,
                                             CSIZE_LATTIME, &csize_lattime);
          printk(KERN_INFO "aic7xxx: BurstLen = %d DWDs, Latency Timer = %d "
                 "PCLKS\n", (int) (csize_lattime & CACHESIZE),
                 (csize_lattime >> 8) & 0x000000ff);

          error += pcibios_read_config_dword(pci_bus, pci_device_fn,
                                             CLASS_PROGIF_REVID, &class_revid);
          if ((class_revid & DEVREVID) < 3)
          {
            printk(KERN_INFO "aic7xxx: %s Rev %c.\n", board_names[config.type],
                   rev_id[class_revid & DEVREVID]);
          }

          error += pcibios_read_config_dword(pci_bus, pci_device_fn,
                                             DEVCONFIG, &devconfig);
          if (error)
          {
            panic("aic7xxx: (aic7xxx_detect) Error %d reading PCI registers.\n",
                  error);
          }

          printk(KERN_INFO "aic7xxx: devconfig = 0x%x.\n", devconfig);

          /*
           * I don't think we need to bother with allowing
           * spurious interrupts for the 787x/785x, but what
           * the hey.
           */
          aic7xxx_spurious_count = 1;

          config.base = iobase;
          config.mbase = mbase;
          config.irq = irq;
          config.parity = AIC_ENABLED;
          config.low_term = AIC_UNKNOWN;
          config.high_term = AIC_UNKNOWN;
          if (aic7xxx_extended)
            config.flags |= EXTENDED_TRANSLATION;
#ifdef AIC7XXX_SHARE_SCBs
          if (devconfig & RAMPSM)
#else
          if ((devconfig & RAMPSM) && (config.type != AIC_7873) &&
              (config.type != AIC_7883))
#endif
          {
            /*
             * External SRAM present.  The probe will walk the SCBs to see
             * how much SRAM we have and set the number of SCBs accordingly.
             * We have to turn off SCBRAMSEL to access the external SCB
             * SRAM.
             *
             * It seems that early versions of the aic7870 didn't use these
             * bits, hence the hack for the 3940 above.  I would guess that
             * recent 3940s using later aic7870 or aic7880 chips do actually
             * set RAMPSM.
             *
             * The documentation isn't clear, but it sounds like the value
             * written to devconfig must not have RAMPSM set.  The second
             * sixteen bits of the register are R/O anyway, so it shouldn't
             * affect RAMPSM either way.
             */
            printk(KERN_INFO "aic7xxx: External RAM detected; enabling RAM "
                   "access.\n");
            devconfig &= ~(RAMPSM | SCBRAMSEL);
            pcibios_write_config_dword(pci_bus, pci_device_fn,
                                       DEVCONFIG, devconfig);
          }
          found += aic7xxx_register(template, &config);

          /*
           * Disable spurious interrupts.
           */
          aic7xxx_spurious_count = 0;

          index++;
        }  /* Found an Adaptec PCI device. */
      }
    }
  }
#endif CONFIG_PCI

  template->name = aic7xxx_info(NULL);
  return (found);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_buildscb
 *
 * Description:
 *   Build a SCB.
 *-F*************************************************************************/
static void
aic7xxx_buildscb(struct aic7xxx_host *p, Scsi_Cmnd *cmd,
    struct aic7xxx_scb *scb)
{
  unsigned int addr; /* must be 32 bits */
  unsigned short mask;

  mask = (0x01 << TARGET_INDEX(cmd));
  /*
   * Setup the control byte if we need negotiation and have not
   * already requested it.
   */
#ifdef AIC7XXX_TAGGED_QUEUEING
  if (cmd->device->tagged_queue)
  {
    cmd->tag = scb->tag;
    cmd->device->current_tag = scb->tag;
    scb->control |= TAG_ENB;
    p->device_status[TARGET_INDEX(cmd)].commands_sent++;
    if (p->device_status[TARGET_INDEX(cmd)].commands_sent == 200)
    {
      scb->control |= 0x02;
      p->device_status[TARGET_INDEX(cmd)].commands_sent = 0;
    }
#if 0
    if (p->orderedtag & mask)
    {
      scb->control |= 0x02;
      p->orderedtag = p->orderedtag & ~mask;
    }
#endif
  }
#endif
  if (p->discenable & mask)
  {
    scb->control |= DISCENB;
  }
  if ((p->needwdtr & mask) && !(p->wdtr_pending & mask))
  {
    p->wdtr_pending |= mask;
    scb->control |= NEEDWDTR;
#if 0
    printk("aic7xxx: Sending WDTR request to target %d.\n", cmd->target);
#endif
  }
  else
  {
    if ((p->needsdtr & mask) && !(p->sdtr_pending & mask))
    {
      p->sdtr_pending |= mask;
      scb->control |= NEEDSDTR;
#if 0
      printk("aic7xxx: Sending SDTR request to target %d.\n", cmd->target);
#endif
    }
  }

#if 0
  printk("aic7xxx: (build_scb) Target %d, cmd(0x%x) size(%u) wdtr(0x%x) "
         "mask(0x%x).\n",
	 cmd->target, cmd->cmnd[0], cmd->cmd_len, p->needwdtr, mask);
#endif
  scb->target_channel_lun = ((cmd->target << 4) & 0xF0) |
	((cmd->channel & 0x01) << 3) | (cmd->lun & 0x07);

  /*
   * The interpretation of request_buffer and request_bufflen
   * changes depending on whether or not use_sg is zero; a
   * non-zero use_sg indicates the number of elements in the
   * scatter-gather array.
   */

  /*
   * XXX - this relies on the host data being stored in a
   *       little-endian format.
   */
  addr = VIRT_TO_BUS(cmd->cmnd);
  scb->SCSI_cmd_length = cmd->cmd_len;
  memcpy(scb->SCSI_cmd_pointer, &addr, sizeof(scb->SCSI_cmd_pointer));

  if (cmd->use_sg)
  {
    struct scatterlist *sg;  /* Must be mid-level SCSI code scatterlist */

    /*
     * We must build an SG list in adapter format, as the kernel's SG list
     * cannot be used directly because of data field size (__alpha__)
     * differences and the kernel SG list uses virtual addresses where
     * we need physical addresses.
     */
    int i;

    sg = (struct scatterlist *)cmd->request_buffer;
    for (i = 0; i < cmd->use_sg; i++)
    {
      scb->sg_list[i].address = VIRT_TO_BUS(sg[i].address);
      scb->sg_list[i].length = (unsigned int) sg[i].length;
    }
    scb->SG_segment_count = cmd->use_sg;
    addr = VIRT_TO_BUS(scb->sg_list);
    memcpy(scb->SG_list_pointer, &addr, sizeof(scb->SG_list_pointer));
    memcpy(scb->data_pointer, &(scb->sg_list[0].address),
           sizeof(scb->data_pointer));
    scb->data_count = scb->sg_list[0].length;
#if 0
    printk("aic7xxx: (build_scb) SG segs(%d), length(%u), sg[0].length(%d).\n",
           cmd->use_sg, aic7xxx_length(cmd, 0), scb->data_count);
#endif
  }
  else
  {
#if 0
  printk("aic7xxx: (build_scb) Creating scatterlist, addr(0x%lx) length(%d).\n",
	(unsigned long) cmd->request_buffer, cmd->request_bufflen);
#endif
    if (cmd->request_bufflen == 0)
    {
      /*
       * In case the higher level SCSI code ever tries to send a zero
       * length command, ensure the SCB indicates no data.  The driver
       * will interpret a zero length command as a Bus Device Reset.
       */
      scb->SG_segment_count = 0;
      memset(scb->SG_list_pointer, 0, sizeof(scb->SG_list_pointer));
      memset(scb->data_pointer, 0, sizeof(scb->data_pointer));
      scb->data_count = 0;
    }
    else
    {
      scb->SG_segment_count = 1;
      scb->sg_list[0].address = VIRT_TO_BUS(cmd->request_buffer);
      scb->sg_list[0].length = cmd->request_bufflen;
      addr = VIRT_TO_BUS(&scb->sg_list[0]);
      memcpy(scb->SG_list_pointer, &addr, sizeof(scb->SG_list_pointer));
      scb->data_count = scb->sg_list[0].length;
      addr = VIRT_TO_BUS(cmd->request_buffer);
      memcpy(scb->data_pointer, &addr, sizeof(scb->data_pointer));
    }
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_queue
 *
 * Description:
 *   Queue a SCB to the controller.
 *-F*************************************************************************/
int
aic7xxx_queue(Scsi_Cmnd *cmd, void (*fn)(Scsi_Cmnd *))
{
  long processor_flags;
  struct aic7xxx_host *p;
  struct aic7xxx_scb *scb;
  u_char curscb, intstat;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  if (p->host != cmd->host)
  {
    printk(KERN_INFO "scsi%d: Internal host structure != scsi.c host "
      "structure.\n", p->host_no);
  }

  /*
   * Check to see if channel was scanned.
   */
  if (!(p->flags & A_SCANNED) && (cmd->channel == 0))
  {
    printk(KERN_INFO "scsi%d: Scanning channel A for devices.\n", p->host_no);
    p->flags |= A_SCANNED;
  }
  else
  {
    if (!(p->flags & B_SCANNED) && (cmd->channel == 1))
    {
      printk(KERN_INFO "scsi%d: Scanning channel B for devices.\n", p->host_no);
      p->flags |= B_SCANNED;
    }
  }

#if 0
  printk("aic7xxx: (queue) cmd(0x%x) size(%u), target %d, channel %d, lun %d.\n",
	cmd->cmnd[0], cmd->cmd_len, cmd->target, cmd->channel,
	cmd->lun & 0x07);
#endif

  /*
   * This is a critical section, since we don't want the interrupt
   * routine mucking with the host data or the card.  For this reason
   * it is nice to know that this function can only be called in one
   * of two ways from scsi.c  First, as part of a routine queue command,
   * in which case, the irq for our card is disabled before this
   * function is called.  This doesn't help us if there is more than
   * one card using more than one IRQ in our system, therefore, we
   * should disable all interrupts on these grounds alone.  Second,
   * this can be called as part of the scsi_done routine, in which case
   * we are in the aic7xxx_isr routine already and interrupts are
   * disabled, therefore we should saveflags first, then disable the
   * interrupts, do our work, then restore the CPU flags. If it weren't
   * for the possibility of more than one card using more than one IRQ
   * in our system, we wouldn't have to touch the interrupt flags at all.
   */
  save_flags(processor_flags);
  cli();

  scb = aic7xxx_allocate_scb(p);
  if (scb == NULL)
  {
    panic("aic7xxx: (aic7xxx_free) Couldn't find a free SCB.\n");
  }
  else
  {
    scb->cmd = cmd;
    aic7xxx_position(cmd) = scb->tag;
#if 0
    debug_scb(scb);
#endif;

    /*
     * Construct the SCB beforehand, so the sequencer is
     * paused a minimal amount of time.
     */
    aic7xxx_buildscb(p, cmd, scb);

#if 0
    if (scb != (p->scb_array[scb->position]))
    {
      printk("aic7xxx: (queue) Address of SCB by position does not match SCB "
             "address.\n");
    }
    printk("aic7xxx: (queue) SCB pos(%d) cmdptr(0x%x) state(%d) freescb(0x%x)\n",
	   scb->position, (unsigned int) scb->cmd,
	   scb->state, (unsigned int) p->free_scb);
#endif

    /*
     * Make sure the Scsi_Cmnd pointer is saved, the struct it points to
     * is set up properly, and the parity error flag is reset, then send
     * the SCB to the sequencer and watch the fun begin.
     */
    cmd->scsi_done = fn;
    aic7xxx_error(cmd) = DID_OK;
    aic7xxx_status(cmd) = 0;
    cmd->result = 0;
    cmd->host_scribble = NULL;
    memset(&cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));

    if (scb->position != SCB_LIST_NULL)
    {
      /* We've got a valid slot, yeah! */
      if (p->flags & IN_ISR)
      {
        scbq_insert_tail(&p->assigned_scbs, scb);
        scb->state |= SCB_ASSIGNEDQ;
      }
      else
      {
        /*
         * Pause the sequencer so we can play with its registers -
         * wait for it to acknowledge the pause.
         *
         * XXX - should the interrupts be left on while doing this?
         */
        PAUSE_SEQUENCER(p);
        intstat = inb(INTSTAT + p->base);

        /*
         * Save the SCB pointer and put our own pointer in - this
         * selects one of the four banks of SCB registers. Load
         * the SCB, then write its pointer into the queue in FIFO
         * and restore the saved SCB pointer.
         */
        curscb = inb(SCBPTR + p->base);
        outb(scb->position, SCBPTR + p->base);
        aic7xxx_putscb(p, scb);
        outb(curscb, SCBPTR + p->base);
        outb(scb->position, QINFIFO + p->base);
        scb->state |= SCB_ACTIVE;

        /*
         * Guard against unpausing the sequencer if there is an interrupt
         * waiting to happen.
         */
        if (!(intstat & (BRKADRINT | SEQINT | SCSIINT)))
        {
          UNPAUSE_SEQUENCER(p);
        }
      }
    }
    else
    {
      scb->state |= SCB_WAITINGQ;
      scbq_insert_tail(&p->waiting_scbs, scb);
      if (!(p->flags & IN_ISR))
      {
        aic7xxx_run_waiting_queues(p);
      }
    }

#if 0
    printk("aic7xxx: (queue) After - cmd(0x%lx) scb->cmd(0x%lx) pos(%d).\n",
           (long) cmd, (long) scb->cmd, scb->position);
#endif;
    restore_flags(processor_flags);
  }
  return (0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_abort_reset
 *
 * Description:
 *   Abort or reset the current SCSI command(s).  If the scb has not
 *   previously been aborted, then we attempt to send a BUS_DEVICE_RESET
 *   message to the target.  If the scb has previously been unsuccessfully
 *   aborted, then we will reset the channel and have all devices renegotiate.
 *   Returns an enumerated type that indicates the status of the operation.
 *-F*************************************************************************/
static int
aic7xxx_bus_device_reset(struct aic7xxx_host *p, Scsi_Cmnd *cmd)
{
  struct aic7xxx_scb  *scb;
  unsigned char bus_state;
  int base, result = -1;
  char channel;

  scb = (p->scb_array[aic7xxx_position(cmd)]);
  base = p->base;

  channel = scb->target_channel_lun & SELBUSB ? 'B': 'A';
  if ((cmd == scb->cmd) && (scb->state & SCB_IN_PROGRESS))
  {

    if (scb->state & SCB_IN_PROGRESS)
    {
      /*
       * Ensure that the card doesn't do anything
       * behind our back.
       */
      PAUSE_SEQUENCER(p);

      printk(KERN_WARNING "aic7xxx: (abort_reset) scb state 0x%x, ", scb->state);
      bus_state = inb(LASTPHASE + p->base);

      switch (bus_state)
      {
	case P_DATAOUT:
          printk("Data-Out phase, ");
          break;
	case P_DATAIN:
          printk("Data-In phase, ");
          break;
	case P_COMMAND:
          printk("Command phase, ");
          break;
	case P_MESGOUT:
          printk("Message-Out phase, ");
          break;
	case P_STATUS:
          printk("Status phase, ");
          break;
	case P_MESGIN:
          printk("Message-In phase, ");
          break;
	default:
          printk("while idle, LASTPHASE = 0x%x, ", bus_state);
          /*
           * We're not in a valid phase, so assume we're idle.
           */
          bus_state = 0;
          break;
      }
      printk("SCSISIGI = 0x%x\n", inb(p->base + SCSISIGI));

      /*
       * First, determine if we want to do a bus reset or simply a bus device
       * reset.  If this is the first time that a transaction has timed out
       * and the SCB is not paged out, just schedule a bus device reset.
       * Otherwise, we reset the bus and abort all pending I/Os on that bus.
       */
      if (!(scb->state & (SCB_ABORTED | SCB_PAGED_OUT)))
      {
#if 0
	if (scb->control & TAG_ENB)
	{
          /*
           * We could be starving this command; try sending and ordered tag
           * command to the target we come from.
           */
          scb->state = scb->state | SCB_ABORTED | SCB_SENTORDEREDTAG;
          p->orderedtag = p->orderedtag | 0xFF;
          result = SCSI_RESET_PENDING;
          UNPAUSE_SEQUENCER(p);
          printk(KERN_WARNING "aic7xxx: (abort_reset) Ordered tag queued.\n");
	}
#endif
	unsigned char active_scb, control;
	struct aic7xxx_scb *active_scbp;

	/*
	 * Send a Bus Device Reset Message:
	 * The target we select to send the message to may be entirely
	 * different than the target pointed to by the scb that timed
	 * out.  If the command is in the QINFIFO or the waiting for
	 * selection list, its not tying up the bus and isn't responsible
	 * for the delay so we pick off the active command which should
	 * be the SCB selected by SCBPTR.  If its disconnected or active,
	 * we device reset the target scbp points to.  Although it may
	 * be that this target is not responsible for the delay, it may
	 * may also be that we're timing out on a command that just takes
	 * too much time, so we try the bus device reset there first.
	 */
	active_scb = inb(SCBPTR + base);
	active_scbp = (p->scb_array[inb(SCB_TAG + base)]);
	control = inb(SCB_CONTROL + base);

	/*
	 * Test to see if scbp is disconnected
	 */
	outb(scb->position, SCBPTR + base);
	if (inb(SCB_CONTROL + base) & DISCONNECTED)
	{
#ifdef AIC7XXX_DEBUG_ABORT
          printk("aic7xxx: (abort_scb) scb %d is disconnected; "
                 "bus device reset message queued.\n", scb->position);
#endif
          if (p->flags & PAGE_ENABLED)
          {
            /* Pull this SCB out of the disconnected list. */
            u_char prev = inb(SCB_PREV + base);
            u_char next = inb(SCB_NEXT + base);
            if (prev == SCB_LIST_NULL)
            {
              /* Head of list */
              outb(next, DISCONNECTED_SCBH + base);
            }
            else
            {
              outb(prev, SCBPTR + base);
              outb(next, SCB_NEXT + base);
              if (next != SCB_LIST_NULL)
              {
        	outb(next, SCBPTR + base);
        	outb(prev, SCB_PREV + base);
              }
              outb(scb->position, SCBPTR + base);
            }
          }
	  scb->state |= (SCB_DEVICE_RESET | SCB_ABORTED);
          scb->control = scb->control & DISCENB;
          scb->SCSI_cmd_length = 0;
	  scb->SG_segment_count = 0;
	  memset(scb->SG_list_pointer, 0, sizeof(scb->SG_list_pointer));
	  memset(scb->data_pointer, 0, sizeof(scb->data_pointer));
	  scb->data_count = 0;
	  aic7xxx_putscb(p, scb);
	  aic7xxx_add_waiting_scb(base, scb);
	  outb(active_scb, SCBPTR + base);
          result = SCSI_RESET_PENDING;
	  UNPAUSE_SEQUENCER(p);
	}
	else
	{
	  /*
	   * Is the active SCB really active?
	   */
	  if ((active_scbp->state & SCB_ACTIVE) && bus_state)
	  {
            /*
             * Load the message buffer and assert attention.
             */
            active_scbp->state |= (SCB_DEVICE_RESET | SCB_ABORTED);
            outb(1, MSG_LEN + base);
            outb(MSG_BUS_DEVICE_RESET, MSG0 + base);
            outb(bus_state | ATNO, SCSISIGO + base);
#ifdef AIC7XXX_DEBUG_ABORT
            printk("aic7xxx: (abort_scb) asserted ATN - "
                   "bus device reset in message buffer.\n");
#endif
            if (active_scbp != scb)
            {
              /*
               * XXX - We would like to increment the timeout on scb, but
               *       access to that routine is denied because it is hidden
               *       in scsi.c.  If we were able to do this, it would give
               *       scb a new lease on life.
               */
              ;
            }
            aic7xxx_error(scb->cmd) = DID_RESET;
            /*
             * Restore the active SCB and unpause the sequencer.
             */
            outb(active_scb, SCBPTR + base);
            if (active_scbp != scb)
            {
              /*
               * The mid-level SCSI code requested us to reset a command
               * different from the one that we actually reset.  Return
               * a "not running" indication and hope that the SCSI code
               * will Do the Right Thing (tm).
               */
              result = SCSI_RESET_NOT_RUNNING;
            }
            else
            {
              result = SCSI_RESET_PENDING;
            }
            UNPAUSE_SEQUENCER(p);
	  }
	}
      }
    }
  }
  /* Make sure the sequencer is unpaused upon return. */
  if (result == -1)
  {
    UNPAUSE_SEQUENCER(p);
  }
  return (result);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_abort
 *
 * Description:
 *   Abort the current SCSI command(s).
 *-F*************************************************************************/
int
aic7xxx_abort(Scsi_Cmnd *cmd)
{
  struct aic7xxx_scb  *scb = NULL;
  struct aic7xxx_host *p;
  int    base, result;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  scb = (p->scb_array[aic7xxx_position(cmd)]);
  base = p->base;

#ifdef AIC7XXX_DEBUG_ABORT
  printk("aic7xxx: (abort) Aborting scb %d, TCL %d/%d/%d\n",
         scb->position, TCL_OF_SCB(scb));
#endif

  if (cmd->serial_number != cmd->serial_number_at_timeout)
  {
    result = SCSI_ABORT_NOT_RUNNING;
  }
  else if (scb == NULL)
  {
    result = SCSI_ABORT_NOT_RUNNING;
  }
  else if ((scb->cmd != cmd) || (!(scb->state & SCB_IN_PROGRESS)))
  {
    result = SCSI_ABORT_NOT_RUNNING;
  }
  else
  {
    result = SCSI_ABORT_SNOOZE;
  }
  return (result);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset
 *
 * Description:
 *   Resetting the bus always succeeds - is has to, otherwise the
 *   kernel will panic! Try a surgical technique - sending a BUS
 *   DEVICE RESET message - on the offending target before pulling
 *   the SCSI bus reset line.
 *-F*************************************************************************/
int
aic7xxx_reset(Scsi_Cmnd *cmd, unsigned int flags)
{
  struct aic7xxx_scb *scb = NULL;
  struct aic7xxx_host *p;
  int    base, found, tindex, min_target, max_target, result = -1;
  char   channel = 'A';
  unsigned long processor_flags;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  scb = (p->scb_array[aic7xxx_position(cmd)]);
  base = p->base;
  channel = cmd->channel ? 'B': 'A';
  tindex = (cmd->channel << 4) | cmd->target;

#ifdef AIC7XXX_DEBUG_ABORT
  printk("aic7xxx: (reset) target/channel %d/%d\n", cmd->target, cmd->channel);
#endif

  /* 
   * This routine is called by scsi.c, in which case the interrupts
   * very well may be on when we are called.  As such, we need to save
   * the flags to be sure, then turn interrupts off, and then call our
   * various method funtions which all assume interrupts are off.
   */
  save_flags(processor_flags);
  cli();

  if (scb->cmd != cmd)
    scb = NULL;

  if (!(flags & (SCSI_RESET_SUGGEST_HOST_RESET | SCSI_RESET_SUGGEST_BUS_RESET)) 
      && (scb != NULL))
  {
    /*
     * Attempt a bus device reset if commands have completed successfully
     * since the last bus device reset, or it has been less than 100ms
     * since the last reset.
     */
    if ((p->flags & DEVICE_SUCCESS) ||
        ((jiffies - p->device_status[tindex].last_reset) < HZ/10))
    {
      if (cmd->serial_number != cmd->serial_number_at_timeout)
      {
        result = SCSI_RESET_NOT_RUNNING;
      }
      else
      {
        if (scb == NULL)
        {
          result = SCSI_RESET_NOT_RUNNING;
        }
        else if (flags & SCSI_RESET_ASYNCHRONOUS)
        {
          if (scb->state & SCB_ABORTED)
          {
            result = SCSI_RESET_PENDING;
          }
          else if (!(scb->state & SCB_IN_PROGRESS))
          {
            result = SCSI_RESET_NOT_RUNNING;
          }
        }

        if (result == -1)
        {
          if ((flags & SCSI_RESET_SYNCHRONOUS) &&
              (p->device_status[tindex].flags & BUS_DEVICE_RESET_PENDING))
          {
            scb->state |= SCB_ABORTED;
            result = SCSI_RESET_PENDING;
          }
          else
          {
            result = aic7xxx_bus_device_reset(p, cmd);
            if (result == 0)
              result = SCSI_RESET_PENDING;
          }
        }
      }
    }
  }

  if (result == -1)
  {
    /*
     * The bus device reset failed; try resetting the channel.
     */
    if (!(flags & (SCSI_RESET_SUGGEST_BUS_RESET | SCSI_RESET_SUGGEST_HOST_RESET))
        && (flags & SCSI_RESET_ASYNCHRONOUS))
    {
      if (scb == NULL)
      {
	result = SCSI_RESET_NOT_RUNNING;
      }
      else if (!(scb->state & SCB_IN_PROGRESS))
      {
	result = SCSI_RESET_NOT_RUNNING;
      }
      else if ((scb->state & SCB_ABORTED) &&
               (!(p->device_status[tindex].flags & BUS_DEVICE_RESET_PENDING)))
      {
	result = SCSI_RESET_PENDING;
      }
    }

    if (result == -1)
    {
      /*
       * The reset channel function assumes that the sequencer is paused.
       */
      PAUSE_SEQUENCER(p);
      found = aic7xxx_reset_channel(p, channel, TRUE);

      /*
       * If this is a synchronous reset and there is no SCB for this
       * command, perform completion processing.
       *
       */
      if ((flags & SCSI_RESET_SYNCHRONOUS) && (scb == NULL))
      {
	cmd->result = DID_RESET << 16;
	cmd->scsi_done(cmd);
      }

      switch (p->bus_type)
      {
	case AIC_TWIN:
	  if (channel == 'B')
	  {
            min_target = 8;
            max_target = 15;
	  }
	  else
	  {
            min_target = 0;
            max_target = 7;
	  }
	  break;

	case AIC_WIDE:
	  min_target = 0;
	  max_target = 15;
	  break;

	case AIC_SINGLE:
        default:
	  min_target = 0;
	  max_target = 7;
	  break;
      }

      for (tindex = min_target; tindex <= max_target; tindex++)
      {
	p->device_status[tindex].last_reset = jiffies;
      }

      result = SCSI_RESET_SUCCESS | SCSI_RESET_HOST_RESET;
    }
  }
  restore_flags(processor_flags);
  return (result);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_biosparam
 *
 * Description:
 *   Return the disk geometry for the given SCSI device.
 *-F*************************************************************************/
int
aic7xxx_biosparam(Disk *disk, kdev_t dev, int geom[])
{
  int heads, sectors, cylinders;
  struct aic7xxx_host *p;

  p = (struct aic7xxx_host *) disk->device->host->hostdata;

  /*
   * XXX - if I could portably find the card's configuration
   *       information, then this could be autodetected instead
   *       of left to a boot-time switch.
   */
  heads = 64;
  sectors = 32;
  cylinders = disk->capacity / (heads * sectors);

  if ((p->flags & EXTENDED_TRANSLATION) && (cylinders > 1024))
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

#include "aic7xxx_proc.c"

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = AIC7XXX;

#include "scsi_module.c"
#endif

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

