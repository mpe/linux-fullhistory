/*+M*************************************************************************
 * Adaptec 274x/284x/294x device driver for Linux.
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
 *  and other rework of the code.
 *
 *  Parts of this driver are based on the FreeBSD driver by Justin
 *  T. Gibbs.
 *
 *  A Boot time option was also added for not resetting the scsi bus.
 *
 *    Form:  aic7xxx=extended,no_reset
 *
 *    -- Daniel M. Eischen, deischen@iworks.InterWorks.org, 04/03/95
 *
 *  $Id: aic7xxx.c,v 1.49 1995/06/28 05:41:09 deang Exp $
 *-M*************************************************************************/

#ifdef MODULE
#include <linux/module.h>
#endif

#include <stdarg.h>
#include <asm/io.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/bios32.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include "../block/blk.h"
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "aic7xxx.h"

#define AIC7XXX_C_VERSION  "$Revision: 1.49 $"

#define NUMBER(arr)     (sizeof(arr) / sizeof(arr[0]))
#define MIN(a,b) ((a < b) ? a : b)

/*
 * Defines for PCI bus support, testing twin bus support, DMAing of
 * SCBs, and tagged queueing.
 *
 *   o PCI bus support - this has been implemented and working since
 *     the December 1, 1994 release of this driver. If you don't have
 *     a PCI bus and do not wish to configure your kernel with PCI
 *     support, then make sure this define is set to the cprrect
 *     define for PCI support (CONFIG_PCI) and configure your kernel
 *     without PCI support (make config).
 *
 *   o Twin bus support - this has been tested and does work.
 *
 *   o DMAing of SCBs - thanks to Kai Makisara, this now works
 *
 *   o Tagged queueing - this driver is capable of tagged queueing
 *     but I am unsure as to how well the higher level driver implements
 *     tagged queueing. Therefore, the maximum commands per lun is
 *     set to 2. If you want to implement tagged queueing, ensure
 *     this define is not commented out.
 *
 *   o Sharing IRQs - allowed for sharing of IRQs. This will allow
 *     for multiple aic7xxx host adapters sharing the same IRQ, but
 *     not for sharing IRQs with other devices. The higher level
 *     PCI code and interrupt handling needs to be modified to
 *     support this.
 *
 *  Daniel M. Eischen, deischen@iworks.InterWorks.org, 03/11/95
 */

/* Uncomment this for testing twin bus support. */
#define AIC7XXX_TWIN_SUPPORT

/* Uncomment this for DMAing of SCBs. */
#define AIC7XXX_USE_DMA

/* Uncomment this for tagged queueing. */
/* #define AIC7XXX_TAGGED_QUEUEING */

/* Uncomment this for allowing sharing of IRQs. */
#define AIC7XXX_SHARE_IRQS

/* Set this to the delay in seconds after SCSI bus reset. */
#define AIC7XXX_RESET_DELAY 15

/*
 * Uncomment this to always use scatter/gather lists.
 * *NOTE: The sequencer must be changed also!
 */
#define AIC7XXX_USE_SG

/*
 * Controller type and options
 */
typedef enum {
  AIC_NONE,
  AIC_274x,    /* EISA aic7770 */
  AIC_284x,    /* VLB  aic7770 */
  AIC_7870,    /* PCI  aic7870 */
  AIC_7850,    /* PCI  aic7850 */
  AIC_7872     /* PCI  aic7870 on 394x */
} aha_type;

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
 */
#define DID_RETRY_COMMAND DID_BUS_BUSY

/*
 * EISA/VL-bus stuff
 */
#define MINSLOT		1
#define MAXSLOT		15
#define SLOTBASE(x)	((x) << 12)
#define MAXIRQ		15

/*
 * Standard EISA Host ID regs  (Offset from slot base)
 */
#define HID0(x)         ((x) + 0xC80)   /* 0,1: msb of ID2, 2-7: ID1      */
#define HID1(x)         ((x) + 0xC81)   /* 0-4: ID3, 5-7: LSB ID2         */
#define HID2(x)         ((x) + 0xC82)   /* product                        */
#define HID3(x)         ((x) + 0xC83)   /* firmware revision              */

/*
 * AIC-7770 I/O range to reserve for a card
 */
#define MINREG(x)	((x) + 0xC00ul)
#define MAXREG(x)	((x) + 0xCBFul)

/* -------------------- AIC-7770 offset definitions ----------------------- */

/*
 * SCSI Sequence Control (p. 3-11).
 * Each bit, when set starts a specific SCSI sequence on the bus
 */
#define SCSISEQ(x)		((x) + 0xC00ul)
#define		TEMODEO		0x80
#define		ENSELO		0x40
#define		ENSELI		0x20
#define		ENRSELI		0x10
#define		ENAUTOATNO	0x08
#define		ENAUTOATNI	0x04
#define		ENAUTOATNP	0x02
#define		SCSIRSTO	0x01

/*
 * SCSI Transfer Control 1 Register (pp. 3-14,15).
 * Controls the SCSI module data path.
 */
#define SXFRCTL1(x)		((x) + 0xC02ul)
#define		BITBUCKET	0x80
#define		SWRAPEN		0x40
#define		ENSPCHK		0x20
#define		STIMESEL	0x18
#define		ENSTIMER	0x04
#define		ACTNEGEN	0x02
#define		STPWEN		0x01		/* Powered Termination */

/*
 * SCSI Control Signal Read Register (p. 3-15).
 * Reads the actual state of the SCSI bus pins
 */
#define SCSISIGI(x)		((x) + 0xC03ul)
#define		CDI		0x80
#define		IOI		0x40
#define		MSGI		0x20
#define		ATNI		0x10
#define		SELI		0x08
#define		BSYI		0x04
#define		REQI		0x02
#define		ACKI		0x01

/*
 * SCSI Contol Signal Write Register (p. 3-16).
 * Writing to this register modifies the control signals on the bus. Only
 * those signals that are allowed in the current mode (Initiator/Target) are
 * asserted.
 */
#define SCSISIGO(x)		((x) + 0xC03ul)
#define		CDO		0x80
#define		IOO		0x40
#define		MSGO		0x20
#define		ATNO		0x10
#define		SELO		0x08
#define		BSYO		0x04
#define		REQO		0x02
#define		ACKO		0x01

/*
 * SCSI Rate
 */
#define SCSIRATE(x)		((x) + 0xC04ul)

/*
 * SCSI ID (p. 3-18).
 * Contains the ID of the board and the current target on the
 * selected channel
 */
#define SCSIID(x)		((x) + 0xC05ul)
#define		TID		0xF0		/* Target ID mask */
#define		OID		0x0F		/* Our ID mask */

/*
 * SCSI Status 0 (p. 3-21)
 * Contains one set of SCSI Interrupt codes
 * These are most likely of interest to the sequencer
 */
#define SSTAT0(x)		((x) + 0xC0Bul)
#define		TARGET		0x80		/* Board is a target */
#define		SELDO		0x40		/* Selection Done */
#define		SELDI		0x20		/* Board has been selected */
#define		SELINGO		0x10		/* Selection In Progress */
#define		SWRAP		0x08		/* 24bit counter wrap */
#define		SDONE		0x04		/* STCNT = 0x000000 */
#define		SPIORDY		0x02		/* SCSI PIO Ready */
#define		DMADONE		0x01		/* DMA transfer completed */

/*
 * Clear SCSI Interrupt 1 (p. 3-23)
 * Writing a 1 to a bit clears the associated SCSI Interrupt in SSTAT1.
 */
#define CLRSINT1(x)		((x) + 0xC0Cul)
#define		CLRSELTIMEO	0x80
#define		CLRATNO		0x40
#define		CLRSCSIRSTI	0x20
/*  UNUSED			0x10 */
#define		CLRBUSFREE	0x08
#define		CLRSCSIPERR	0x04
#define		CLRPHASECHG	0x02
#define		CLRREQINIT	0x01

/*
 * SCSI Status 1 (p. 3-24)
 * These interrupt bits are of interest to the kernel driver
 */
#define SSTAT1(x)		((x) + 0xC0Cul)
#define		SELTO		0x80
#define		ATNTARG 	0x40
#define		SCSIRSTI	0x20
#define		PHASEMIS	0x10
#define		BUSFREE		0x08
#define		SCSIPERR	0x04
#define		PHASECHG	0x02
#define		REQINIT		0x01

/*
 * SCSI Interrrupt Mode 1 (pp. 3-28,29).
 * Set bits in this register enable the corresponding
 * interrupt source.
 */
#define	SIMODE1(x)		((x) + 0xC11ul)
#define		ENSELTIMO	0x80
#define		ENATNTARG	0x40
#define		ENSCSIRST	0x20
#define		ENPHASEMIS	0x10
#define		ENBUSFREE	0x08
#define		ENSCSIPERR	0x04
#define		ENPHASECHG	0x02
#define		ENREQINIT	0x01

/*
 * Selection/Reselection ID (p. 3-31)
 * Upper four bits are the device id. The ONEBIT is set when the re/selecting
 * device did not set its own ID.
 */
#define SELID(x)		((x) + 0xC19ul)
#define		SELID_MASK	0xF0
#define		ONEBIT		0x08
/*  UNUSED			0x07 */

/*
 * Serial EEPROM Control (p. 4-92 in 7870 Databook)
 * Controls the reading and writing of an external serial 1-bit
 * EEPROM Device.  In order to access the serial EEPROM, you must
 * first set the SEEMS bit that generates a request to the memory
 * port for access to the serial EEPROM device.  When the memory
 * port is not busy servicing another request, it reconfigures
 * to allow access to the serial EEPROM.  When this happens, SEERDY
 * gets set high to verify that the memory port access has been
 * granted.  See aic7xxx_read_eprom for detailed information on
 * the protocol necessary to read the serial EEPROM.
 */
#define SEECTL(x)		((x) + 0xC1Eul)
#define		EXTARBACK	0x80
#define		EXTARBREQ	0x40
#define		SEEMS		0x20
#define		SEERDY		0x10
#define		SEECS		0x08
#define		SEECK		0x04
#define		SEEDO		0x02
#define		SEEDI		0x01

/*
 * SCSI Block Control (p. 3-32)
 * Controls Bus type and channel selection. In a twin channel configuration
 * addresses 0x00-0x1E are gated to the appropriate channel based on this
 * register. SELWIDE allows for the coexistence of 8bit and 16bit devices
 * on a wide bus.
 */
#define SBLKCTL(x)		((x) + 0xC1Ful)
/*  UNUSED			0xC0 */
#define		AUTOFLUSHDIS	0x20		/* used for Rev C check */
/*  UNUSED			0x10 */
#define		SELBUSB		0x08
/*  UNUSED			0x04 */
#define		SELWIDE		0x02
/*  UNUSED			0x01 */
#define		SELSINGLE	0x00

/*
 * Sequencer Control (p. 3-33)
 * Error detection mode and speed configuration
 */
#define SEQCTL(x)		((x) + 0xC60ul)
#define		PERRORDIS	0x80
#define		PAUSEDIS	0x40
#define		FAILDIS		0x20
#define 	FASTMODE	0x10
#define		BRKADRINTEN	0x08
#define		STEP		0x04
#define		SEQRESET	0x02
#define		LOADRAM		0x01

/*
 * Sequencer RAM Data (p. 3-34)
 * Single byte window into the Scratch Ram area starting at the address
 * specified by SEQADDR0 and SEQADDR1. To write a full word, simply write
 * four bytes in sucessesion. The SEQADDRs will increment after the most
 * significant byte is written
 */
#define SEQRAM(x)		((x) + 0xC61ul)

/*
 * Sequencer Address Registers (p. 3-35)
 * Only the first bit of SEQADDR1 holds addressing information
 */
#define SEQADDR0(x)		((x) + 0xC62ul)
#define SEQADDR1(x)		((x) + 0xC63ul)

#define ACCUM(x)		((x) + 0xC64ul)		/* accumulator */

/*
 * Board Control (p. 3-43)
 */
#define BCTL(x)		((x) + 0xC84ul)
/*   RSVD			0xF0 */
#define		ACE		0x08	/* Support for external processors */
/*   RSVD			0x06 */
#define		ENABLE		0x01

#define BUSSPD(x)		((x) + 0xC86ul)	/* FIFO threshold bits ? */

/*
 * Host Control (p. 3-47) R/W
 * Overal host control of the device.
 */
#define HCNTRL(x)		((x) + 0xC87ul)
/*    UNUSED			0x80 */
#define		POWRDN		0x40
/*    UNUSED			0x20 */
#define		SWINT		0x10
#define		IRQMS		0x08
#define		PAUSE		0x04
#define		INTEN		0x02
#define		CHIPRST		0x01
#define		REQ_PAUSE	IRQMS | PAUSE | INTEN
#define		UNPAUSE_274X	IRQMS | INTEN
#define		UNPAUSE_284X	INTEN
#define		UNPAUSE_294X	IRQMS | INTEN

/*
 * SCB Pointer (p. 3-49)
 * Gate one of the four SCBs into the SCBARRAY window.
 */
#define SCBPTR(x)		((x) + 0xC90ul)

/*
 * Interrupt Status (p. 3-50)
 * Status for system interrupts
 */
#define INTSTAT(x)		((x) + 0xC91ul)
#define		SEQINT_MASK	0xF0		/* SEQINT Status Codes */
#define			BAD_PHASE	0x00
#define			SEND_REJECT	0x10
#define			NO_IDENT	0x20
#define			NO_MATCH	0x30
#define			MSG_SDTR	0x40
#define			MSG_WDTR	0x50
#define			MSG_REJECT	0x60
#define			BAD_STATUS	0x70
#define			RESIDUAL	0x80
#define			ABORT_TAG	0x90
#define			AWAITING_MSG	0xa0
#define 	BRKADRINT 0x08
#define		SCSIINT	  0x04
#define		CMDCMPLT  0x02
#define		SEQINT    0x01
#define		INT_PEND  (BRKADRINT | SEQINT | SCSIINT | CMDCMPLT)

/*
 * Hard Error (p. 3-53)
 * Reporting of catastrophic errors. You usually cannot recover from
 * these without a full board reset.
 */
#define ERROR(x)		((x) + 0xC92ul)
/*    UNUSED			0xF0 */
#define		PARERR		0x08
#define		ILLOPCODE	0x04
#define		ILLSADDR	0x02
#define		ILLHADDR	0x01

/*
 * Clear Interrupt Status (p. 3-52)
 */
#define CLRINT(x)		((x) + 0xC92ul)
#define		CLRBRKADRINT	0x08
#define		CLRSCSIINT	0x04
#define		CLRCMDINT 	0x02
#define		CLRSEQINT 	0x01

/*
 * SCB Auto Increment (p. 3-59)
 * Byte offset into the SCB Array and an optional bit to allow auto
 * incrementing of the address during download and upload operations
 */
#define SCBCNT(x)		((x) + 0xC9Aul)
#define		SCBAUTO		0x80
#define		SCBCNT_MASK	0x1F

/*
 * Queue In FIFO (p. 3-60)
 * Input queue for queued SCBs (commands that the seqencer has yet to start)
 */
#define QINFIFO(x)		((x) + 0xC9Bul)

/*
 * Queue In Count (p. 3-60)
 * Number of queued SCBs
 */
#define QINCNT(x)		((x) + 0xC9Cul)

/*
 * Queue Out FIFO (p. 3-61)
 * Queue of SCBs that have completed and await the host
 */
#define QOUTFIFO(x)		((x) + 0xC9Dul)

/*
 * Queue Out Count (p. 3-61)
 * Number of queued SCBs in the Out FIFO
 */
#define QOUTCNT(x)		((x) + 0xC9Eul)

#define SCBARRAY(x)		((x) + 0xCA0ul)

/* ---------------- END AIC-7770 Register Definitions ----------------- */

/* --------------------- AIC-7870-only definitions -------------------- */

#define DSPCISTATUS(x)	 	((x) + 0xC86ul)
#define 	DFTHRESH        0xC0

/* Scratch RAM offset definitions */

/* ---------------------- Scratch RAM Offsets ------------------------- */
/* These offsets are either to values that are initialized by the board's
 * BIOS or are specified by the Linux sequencer code. If I can figure out
 * how to read the EISA configuration info at probe time, the cards could
 * be run without BIOS support installed
 */

/*
 * 1 byte per target starting at this address for configuration values
 */
#define HA_TARG_SCRATCH(x)	((x) + 0xC20ul)

/*
 * The sequencer will stick the first byte of any rejected message here so
 * we can see what is getting thrown away.
 */
#define HA_REJBYTE(x)		((x) + 0xC31ul)

/*
 * Bit vector of targets that have disconnection disabled.
 */
#define	HA_DISC_DSB		((x) + 0xc32ul)

/*
 * Length of pending message
 */
#define HA_MSG_LEN(x)		((x) + 0xC34ul)

/*
 * Outgoing Message Body
 */
#define HA_MSG_START(x)		((x) + 0xC35ul)

/*
 * These are offsets into the card's scratch ram. Some of the values are
 * specified in the AHA2742 technical reference manual and are initialized
 * by the BIOS at boot time.
 */
#define HA_ARG_1(x)		((x) + 0xC4Aul)	/* sdtr <-> rate parameters */
#define HA_RETURN_1(x)		((x) + 0xC4Aul)
#define		SEND_SENSE	0x80
#define		SEND_SDTR 	0x80
#define		SEND_WDTR 	0x80
#define		SEND_REJ	0x40

#define HA_SIGSTATE(x)		((x) + 0xC4Bul)	/* value in SCSISIGO */
#define HA_SCBCOUNT(x)		((x) + 0xC52ul)	/* number of hardware SCBs */

#define HA_FLAGS(x)		((x) + 0xC53ul)	/* TWIN and WIDE bus flags */
#define		SINGLE_BUS	0x00
#define		TWIN_BUS	0x01
#define		WIDE_BUS	0x02
#define		ACTIVE_MSG	0x20
#define		IDENTIFY_SEEN	0x40
#define		RESELECTING	0x80

#define HA_ACTIVE0(x)		((x) + 0xC54ul)	/* Active bits; targets 0-7 */
#define HA_ACTIVE1(x)		((x) + 0xC55ul)	/* Active bits; targets 8-15 */
#define	SAVED_TCL(x)		((x) + 0xC56ul)	/* Saved target, channel, LUN */
#define WAITING_SCBH(x)		((x) + 0xC57ul) /* Head of disconnected targets list. */
#define WAITING_SCBT(x)		((x) + 0xC58ul) /* Tail of disconnected targets list. */

#define HA_SCSICONF(x)		((x) + 0xC5Aul)	/* SCSI config register */
#define HA_INTDEF(x)		((x) + 0xC5Cul)	/* interrupt def'n register */
#define HA_HOSTCONF(x)		((x) + 0xC5Dul)	/* host config def'n register */

#define MSG_ABORT		0x06
#define	MSG_BUS_DEVICE_RESET	0x0c
#define BUS_8_BIT		0x00
#define BUS_16_BIT		0x01
#define BUS_32_BIT		0x02


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
#define CFWIDEB		0x0020		/* wide bus device */
/* UNUSED		0x00C0 */
#define CFSTART		0x0100		/* send start unit SCSI command */
#define CFINCBIOS	0x0200		/* include in BIOS scan */
#define CFRNFOUND	0x0400		/* report even if not found */
/* UNUSED		0xF800 */
  unsigned short device_flags[16];	/* words 0-15 */

/*
 * BIOS Control Bits
 */
#define CFSUPREM	0x0001		/* support all removeable drives */
#define CFSUPREMB	0x0002		/* support removeable drives for boot only */
#define CFBIOSEN	0x0004		/* BIOS enabled */
/* UNUSED		0x0008 */
#define CFSM2DRV	0x0010		/* support more than two drives */
/* UNUSED		0x0060 */
#define CFEXTEND	0x0080		/* extended translation enabled */
/* UNUSED		0xFF00 */
  unsigned short bios_control;		/* word 16 */

/*
 * Host Adapter Control Bits
 */
/* UNUSED		0x0003 */
#define CFWSTERM	0x0008		/* SCSI high byte termination (wide card) */
#define CFSTERM		0x0004		/* SCSI low byte termination (non-wide cards) */
#define CFSPARITY	0x0010		/* SCSI parity */
/* UNUSED		0x0020 */
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


#define AIC7XXX_DEBUG

/*
 * Pause the sequencer and wait for it to actually stop - this
 * is important since the sequencer can disable pausing for critical
 * sections.
 */
#define PAUSE_SEQUENCER(p) \
  outb(p->pause, HCNTRL(p->base));			\
  while ((inb(HCNTRL(p->base)) & PAUSE) == 0)		\
    ;							\

/*
 * Unpause the sequencer. Unremarkable, yet done often enough to
 * warrant an easy way to do it.
 */
#define UNPAUSE_SEQUENCER(p) \
  outb(p->unpause, HCNTRL(p->base))

/*
 * Restart the sequencer program from address zero
 */
#define RESTART_SEQUENCER(p) \
  do {							\
    outb(SEQRESET | FASTMODE, SEQCTL(p->base));	\
  } while (inb(SEQADDR0(p->base)) != 0 &&		\
	   inb(SEQADDR1(p->base)) != 0);		\
  UNPAUSE_SEQUENCER(p);

/*
 * If an error occurs during a data transfer phase, run the comand
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
 * Since the sequencer code DMAs the scatter-gather structures
 * directly from memory, we use this macro to assert that the
 * kernel structure hasn't changed.
 */
#define SG_STRUCT_CHECK(sg) \
  ((char *)&(sg).address - (char *)&(sg) != 0 ||  \
   (char *)&(sg).length  - (char *)&(sg) != 8 ||  \
   sizeof((sg).address) != 4 ||                   \
   sizeof((sg).length)  != 4 ||                   \
   sizeof(sg)           != 12)

/*
 * "Static" structures. Note that these are NOT initialized
 * to zero inside the kernel - we have to initialize them all
 * explicitly.
 *
 * We support a maximum of one adapter card per IRQ level (see the
 * rationale for this above). On an interrupt, use the IRQ as an
 * index into aic7xxx_boards[] to locate the card information.
 */
static struct Scsi_Host *aic7xxx_boards[MAXIRQ + 1];

/*
 * The driver keeps up to four scb structures per card in memory. Only the
 * first 26 bytes of the structure are valid for the hardware, the rest used
 * for driver level bookeeping. The driver is further optimized
 * so that we only have to download the first 19 bytes since as long
 * as we always use S/G, the last fields should be zero anyway.
 */
#ifdef AIC7XXX_USE_SG
#define SCB_DOWNLOAD_SIZE	19	/* amount to actually download */
#else
#define SCB_DOWNLOAD_SIZE	26
#endif

#define SCB_UPLOAD_SIZE		19	/* amount to actually upload */

struct aic7xxx_scb {
/* ------------    Begin hardware supported fields    ---------------- */
/*1 */  unsigned char control;
#define SCB_NEEDWDTR 0x80                       /* Initiate Wide Negotiation */
#define SCB_NEEDSDTR 0x40                       /* Initiate Sync Negotiation */
#define SCB_NEEDDMA  0x08                       /* SCB needs to be DMA'd from
						 * from host memory
						 */
#define SCB_REJ_MDP      0x80                   /* Reject MDP message */
#define SCB_DISEN        0x40                   /* SCB Disconnect enable */
#define SCB_TE           0x20                   /* Tag enable */
/*      RESERVED         0x10 */
#define SCB_WAITING      0x08                   /* Waiting */
#define SCB_DIS          0x04                   /* Disconnected */
#define SCB_TAG_TYPE     0x03
#define         SIMPLE_QUEUE 0x00               /* Simple Queue */
#define         HEAD_QUEUE   0x01               /* Head of Queue */
#define         ORD_QUEUE    0x02               /* Ordered Queue */
/*              ILLEGAL      0x03 */
/*2 */  unsigned char target_channel_lun;       /* 4/1/3 bits */
/*3 */  unsigned char SG_segment_count;
/*7 */  unsigned char SG_list_pointer[4] __attribute__ ((packed));
/*11*/  unsigned char SCSI_cmd_pointer[4] __attribute__ ((packed));
/*12*/  unsigned char SCSI_cmd_length;
/*14*/  unsigned char RESERVED[2];              /* must be zero */
/*15*/  unsigned char target_status;
/*18*/  unsigned char residual_data_count[3];
/*19*/  unsigned char residual_SG_segment_count;
/*23*/  unsigned char data_pointer[4] __attribute__ ((packed));
/*26*/  unsigned char data_count[3];
/*30*/  unsigned char host_scb[4] __attribute__ ((packed));
/*31*/  u_char next_waiting;            /* Used to thread SCBs awaiting selection. */
#define SCB_LIST_NULL 0x10              /* SCB list equivelent to NULL */
#if 0
	/*
	 *  No real point in transferring this to the
	 *  SCB registers.
	 */
	unsigned char RESERVED[1];
#endif

	/*-----------------end of hardware supported fields----------------*/
	struct aic7xxx_scb *next;	/* next ptr when in free list */
	Scsi_Cmnd          *cmd;	/* Scsi_Cmnd for this scb */
	int                 state;	/* current state of scb */
#define SCB_FREE               0x00
#define SCB_ACTIVE             0x01
#define SCB_ABORTED            0x02
#define SCB_DEVICE_RESET       0x04
#define SCB_IMMED              0x08
#define SCB_SENSE              0x10
	unsigned int        position;       /* Position in scb array */
#ifdef AIC7XXX_USE_SG
	struct scatterlist  sg;
	struct scatterlist  sense_sg;
#endif
	unsigned char       sense_cmd[6];   /* Allocate 6 characters for sense command */
};

static struct {
  unsigned char errno;
  char *errmesg;
} hard_error[] = {
  { ILLHADDR,  "Illegal Host Access" },
  { ILLSADDR,  "Illegal Sequencer Address referrenced" },
  { ILLOPCODE, "Illegal Opcode in sequencer program" },
  { PARERR,    "Sequencer Ram Parity Error" }
};

static unsigned char
generic_sense[] = { REQUEST_SENSE, 0, 0, 0, 255, 0 };

/*
 * The maximum number of SCBs we could have for ANY type
 * of card. DON'T FORGET TO CHANGE THE SCB MASK IN THE
 * SEQUENCER CODE IF THIS IS MODIFIED!
 */
#define AIC7XXX_MAXSCB	16

/*
 * Define a structure used for each host adapter, only one per IRQ.
 */
struct aic7xxx_host {
  int                      base;             /* card base address */
  int                      maxscb;           /* hardware SCBs */
  int                      numscb;           /* current number of scbs */
  int                      extended;         /* extended xlate? */
  aha_type                 type;             /* card type */
  aha_bus_type             bus_type;         /* normal/twin/wide bus */
  unsigned char            a_scanned;        /* 0 not scanned, 1 scanned */
  unsigned char            b_scanned;        /* 0 not scanned, 1 scanned */
  unsigned int             isr_count;        /* Interrupt count */
  volatile unsigned char   unpause;          /* unpause value for HCNTRL */
  volatile unsigned char   pause;            /* pause value for HCNTRL */
  volatile unsigned short  needsdtr_copy;    /* default config */
  volatile unsigned short  needsdtr;
  volatile unsigned short  sdtr_pending;
  volatile unsigned short  needwdtr_copy;    /* default config */
  volatile unsigned short  needwdtr;
  volatile unsigned short  wdtr_pending;
  struct seeprom_config    seeprom;
  int                      have_seeprom;
  struct Scsi_Host        *next;             /* allow for multiple IRQs */
  struct aic7xxx_scb       scb_array[AIC7XXX_MAXSCB];  /* active commands */
  struct aic7xxx_scb      *free_scb;         /* list of free SCBs */
};

struct aic7xxx_host_config {
  int              irq;        /* IRQ number */
  int              base;       /* I/O base */
  int              maxscb;     /* hardware SCBs */
  int              unpause;    /* unpause value for HCNTRL */
  int              pause;      /* pause value for HCNTRL */
  int              scsi_id;    /* host SCSI ID */
  int              scsi_id_b;  /* host SCSI ID B channel for twin cards */
  int              extended;   /* extended xlate? */
  int              busrtime;   /* bus release time */
  aha_type         type;       /* card type */
  aha_bus_type     bus_type;   /* normal/twin/wide bus */
  aha_status_type  parity;     /* bus parity enabled/disabled */
  aha_status_type  low_term;   /* bus termination low byte */
  aha_status_type  high_term;  /* bus termination high byte (wide cards only) */
};

/*
 * Valid SCSIRATE values. (p. 3-17)
 * Provides a mapping of tranfer periods in ns to the proper value to
 * stick in the scsiscfr reg to use that transfer rate.
 */
static struct {
  short period;
  short rate;
  char *english;
} aic7xxx_syncrates[] = {
  { 100,   0,  "10.0" },
  { 125,   1,  "8.0"  },
  { 150,   2,  "6.67" },
  { 175,   3,  "5.7"  },
  { 200,   4,  "5.0"  },
  { 225,   5,  "4.4"  },
  { 250,   6,  "4.0"  },
  { 275,   7,  "3.6"  }
};

static int num_aic7xxx_syncrates =
    sizeof(aic7xxx_syncrates) / sizeof(aic7xxx_syncrates[0]);

#ifdef AIC7XXX_DEBUG
extern int vsprintf(char *, const char *, va_list);

static void
debug(const char *fmt, ...)
{
  va_list ap;
  char buf[256];

  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);
  printk(buf);
  va_end(ap);
}

static void
debug_config(struct aic7xxx_host_config *p)
{
  int host_conf, scsi_conf;
  unsigned char brelease;
  unsigned char dfthresh;

  static int DFT[] = { 0, 50, 75, 100 };
  static int SST[] = { 256, 128, 64, 32 };
  static char *BUSW[] = { "", "-TWIN", "-WIDE" };

  host_conf = inb(HA_HOSTCONF(p->base));
  scsi_conf = inb(HA_SCSICONF(p->base));

  /*
   * The 7870 gets the bus release time and data FIFO threshold
   * from the serial EEPROM (stored in the config structure) and
   * scsi_conf register respectively.  The 7770 gets the bus
   * release time and data FIFO threshold from the scsi_conf and
   * host_conf registers respectively.
   */
  if ((p->type == AIC_274x) || (p->type == AIC_284x))
  {
    brelease = scsi_conf & 0x3F;
    dfthresh = host_conf >> 6;
  }
  else
  {
    brelease = p->busrtime;
    dfthresh = scsi_conf >> 6;
  }
  if (brelease == 0)
  {
    brelease = 2;
  }

  switch (p->type)
  {
    case AIC_274x:
      printk("AIC7770%s AT EISA SLOT %d:\n", BUSW[p->bus_type], p->base >> 12);
      break;

    case AIC_284x:
      printk("AIC7770%s AT VLB SLOT %d:\n", BUSW[p->bus_type], p->base >> 12);
      break;

    case AIC_7870:
      printk("AIC7870%s (PCI-bus):\n", BUSW[p->bus_type]);
      break;

    case AIC_7850:
      printk("AIC7850%s (PCI-bus):\n", BUSW[p->bus_type]);
      break;

    case AIC_7872:
      printk("AIC7872%s (PCI-bus):\n", BUSW[p->bus_type]);
      break;

    default:
      panic("aic7xxx debug_config: internal error\n");
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

  if (((p->type == AIC_274x) || (p->type == AIC_284x)) && p->parity == AIC_UNKNOWN)
  { /* Set the parity for 7770 based cards. */
    p->parity = (scsi_conf & 0x20) ? AIC_ENABLED : AIC_DISABLED;
  }
  if (p->parity != AIC_UNKNOWN)
  {
    printk("        scsi bus parity %sabled\n",
	   (p->parity == AIC_ENABLED) ? "en" : "dis");
  }

  if (p->type == AIC_274x)
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
#else
#  define debug(fmt, args...)
#  define debug_config(x)
#endif AIC7XXX_DEBUG

/*
 * XXX - these options apply unilaterally to _all_ 274x/284x/294x
 *       cards in the system. This should be fixed, but then,
 *       does anyone really have more than one in a machine?
 */
static int aic7xxx_extended = 0;	/* extended translation on? */
static int aic7xxx_no_reset = 0;	/* no resetting of SCSI bus */

/*+F*************************************************************************
 * Function:
 *   aic7xxx_setup
 *
 * Description:
 *   Handle Linux boot parameters.
 *-F*************************************************************************/
void
aic7xxx_setup(char *s, int *dummy)
{
  int   i;
  char *p;

  static struct {
    char *name;
    int *flag;
  } options[] = {
    { "extended",    &aic7xxx_extended },
    { "no_reset",    &aic7xxx_no_reset },
    { NULL,          NULL}
  };

  for (p = strtok(s, ","); p; p = strtok(NULL, ","))
  {
    for (i = 0; options[i].name; i++)
    {
      if (!strcmp(options[i].name, p))
      {
	*(options[i].flag) = !0;
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
  outb(PERRORDIS | SEQRESET | LOADRAM, SEQCTL(base));

  asm volatile("cld\n\t"
	       "rep\n\t"
	       "outsb"
	       : /* no output */
	       :"S" (seqprog), "c" (sizeof(seqprog)), "d" (SEQRAM(base))
	       :"si", "cx", "dx");

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
    outb(SEQRESET | FASTMODE, SEQCTL(base));
  } while ((inb(SEQADDR0(base)) != 0) && (inb(SEQADDR1(base)) != 0));
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

  i = jiffies + (seconds * 100);  /* compute time to stop */

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
 *   an Id or Revison RCS clause.
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
 *   aic7xxx_putscb
 *
 * Description:
 *   Transfer a SCB to the controller.
 *-F*************************************************************************/
static void
aic7xxx_putscb(int base, struct aic7xxx_scb *scb)
{
#ifdef AIC7XXX_USE_DMA
  /*
   * All we need to do, is to output the position
   * of the SCB in the SCBARRAY to the QINFIFO
   * of the host adapter.
   */
  outb(scb->position, QINFIFO(base));
#else
  /*
   * By turning on the SCB auto increment, any reference
   * to the SCB I/O space postincrements the SCB address
   * we're looking at. So turn this on and dump the relevant
   * portion of the SCB to the card.
   */
  outb(SCBAUTO, SCBCNT(base));

  asm volatile("cld\n\t"
	       "rep\n\t"
	       "outsb"
	       : /* no output */
	       :"S" (scb), "c" (SCB_DOWNLOAD_SIZE), "d" (SCBARRAY(base))
	       :"si", "cx", "dx");

  outb(0, SCBCNT(base));
#endif
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_putdmascb
 *
 * Description:
 *   DMA a SCB to the controller.
 *-F*************************************************************************/
static void
aic7xxx_putdmascb(int base, struct aic7xxx_scb *scb)
{
  /*
   * By turning on the SCB auto increment, any reference
   * to the SCB I/O space postincrements the SCB address
   * we're looking at. So turn this on and dump the relevant
   * portion of the SCB to the card.
   */
  outb(SCBAUTO, SCBCNT(base));

  asm volatile("cld\n\t"
	       "rep\n\t"
	       "outsb"
	       : /* no output */
	       :"S" (scb), "c" (31), "d" (SCBARRAY(base))
	       :"si", "cx", "dx");

  outb(0, SCBCNT(base));
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_getscb
 *
 * Description:
 *   Get a SCB from the controller.
 *-F*************************************************************************/
static void
aic7xxx_getscb(int base, struct aic7xxx_scb *scb)
{
  /*
   * This is almost identical to aic7xxx_putscb().
   */
  outb(SCBAUTO, SCBCNT(base));

  asm volatile("cld\n\t"
	       "rep\n\t"
	       "insb"
	       : /* no output */
	       :"D" (scb), "c" (SCB_UPLOAD_SIZE), "d" (SCBARRAY(base))
	       :"di", "cx", "dx");

  outb(0, SCBCNT(base));
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
  sg = (struct scatterlist *) cmd->buffer;

  if (cmd->use_sg)
  {
    for (i = length = 0; i < cmd->use_sg && i < segments; i++)
    {
      length += sg[i].length;
    }
  }
  else
  {
    length = cmd->request_bufflen;
  }

  return(length);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_scsirate
 *
 * Description:
 *   Look up the valid period to SCSIRATE conversion in our table
 *-F*************************************************************************/
static void
aic7xxx_scsirate(unsigned char *scsirate, unsigned char period,
		 unsigned char offset, int target)
{
  int i;

  for (i = 0; i < num_aic7xxx_syncrates; i++)
  {
    if ((aic7xxx_syncrates[i].period - period) >= 0)
    {
      *scsirate = (aic7xxx_syncrates[i].rate << 4) | (offset & 0x0F);
      printk("aic7xxx: target %d now synchronous at %sMb/s, offset = 0x%x\n",
	     target, aic7xxx_syncrates[i].english, offset);
      return;
    }
  }

  /*
   * Default to asyncronous transfer
   */
  *scsirate = 0;
  printk("aic7xxx: target %d using asynchronous transfers\n", target);
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
aic7xxx_isr(int irq, struct pt_regs * regs)
{
  int base, intstat;
  struct aic7xxx_host *p;
  struct aic7xxx_scb *scb;
  unsigned char active, ha_flags, transfer;
  unsigned char scsi_id, bus_width;
  unsigned char offset, rate, scratch;
  unsigned char max_offset;
  unsigned char head, tail;
  unsigned short target_mask;
  long flags;
  void *addr;
  int actual;
  int target, tcl;
  int scbptr;
  Scsi_Cmnd *cmd;
#if 0
static int_count = 0;
#endif

  p = (struct aic7xxx_host *) aic7xxx_boards[irq]->hostdata;
#ifdef AIC7XXX_SHARE_IRQS
  /*
   * Search for the host with a pending interrupt.
   */
  while ((p != NULL) && !(inb(INTSTAT(p->base)) & INT_PEND))
  {
    p = (struct aic7xxx_host *) p->next->hostdata;
  }
  if (p == NULL)
  {
    printk("aic7xxx_isr: Encountered spurious interrupt.\n");
    return;
  }
#endif
  base = p->base;
  if (p->isr_count == 0xffffffff)
  {
    p->isr_count = 0;
  }
  else
  {
    p->isr_count = p->isr_count + 1;
  }
  if ((p->a_scanned == 0) && (p->isr_count == 1))
  {
    /* Allow for one interrupt when the card is enabled. */
    return;
  }

  /*
   * Handle all the interrupt sources - especially for SCSI
   * interrupts, we won't get a second chance at them.
   */
  intstat = inb(INTSTAT(base));

  if (intstat & BRKADRINT)
  {
    int i;
    unsigned char errno = inb(ERROR(base));

    printk("aic7xxx_isr: brkadrint (0x%x):\n", errno);
    for (i = 0; i < NUMBER(hard_error); i++)
    {
      if (errno & hard_error[i].errno)
      {
	printk("  %s\n", hard_error[i].errmesg);
      }
    }

    panic("aic7xxx_isr: brkadrint, error = 0x%x, seqaddr = 0x%x\n",
	  inb(ERROR(base)),
	  inb(SEQADDR1(base)) << 8 | inb(SEQADDR0(base)));
  }

  if (intstat & SEQINT)
  {
    /*
     * Although the sequencer is paused immediately on
     * a SEQINT, an interrupt for a SCSIINT or a CMDCMPLT
     * condition will have unpaused the sequencer before
     * this point.
     */
    PAUSE_SEQUENCER(p);

    switch (intstat & SEQINT_MASK)
    {
      case BAD_PHASE:
	panic("aic7xxx_isr: unknown scsi bus phase\n");

      case SEND_REJECT:
	debug("aic7xxx_isr warning: issuing message reject, 1st byte 0x%x\n",
	      inb(HA_REJBYTE(base)));
	break;

      case NO_IDENT:
	panic("aic7xxx_isr: reconnecting target %d at seqaddr 0x%x "
	      "didn't issue IDENTIFY message\n",
	      (inb(SELID(base)) >> 4) & 0x0F,
	      (inb(SEQADDR1(base)) << 8) | inb(SEQADDR0(base)));
	break;

      case NO_MATCH:
	tcl = inb(SCBARRAY(base) + 1);
	target = (tcl >> 4) & 0x0F;
	/* Purposefully mask off the top bit of targets 8-15. */
	target_mask = 0x01 << (target & 0x07);

	debug("aic7xxx_isr: sequencer couldn't find match "
	      "for reconnecting target %d, channel %d, lun %d - "
	      "issuing ABORT\n", target, (tcl & 0x08) >> 3, tcl & 0x07);
	if (tcl & 0x88)
	{
	  /* Second channel stores its info in byte
	   * two of HA_ACTIVE
	   */
	  active = inb(HA_ACTIVE1(base));
	  active = active & ~(target_mask);
	  outb(active, HA_ACTIVE1(base));
	}
	else
	{
	  active = inb(HA_ACTIVE0(base));
	  active = active & ~(target_mask);
	  outb(active, HA_ACTIVE0(base));
	}
#ifdef AIC7XXX_USE_DMA
	outb(SCB_NEEDDMA, SCBARRAY(base));
#endif

	/*
	 * Check out why this use to be outb(0x80, CLRINT(base))
	 * clear the timeout
	 */
	outb(CLRSELTIMEO, CLRSINT1(base));
	RESTART_SEQUENCER(p);
	break;

      case MSG_SDTR:
	/*
	 * Help the sequencer to translate the negotiated
	 * transfer rate. Transfer is 1/4 the period
	 * in ns as is returned by the sync negotiation
	 * message. So, we must multiply by four.
	 */
	transfer = (inb(HA_ARG_1(base)) << 2);
	offset = inb(ACCUM(base));
	scsi_id = inb(SCSIID(base)) >> 0x04;
	if (inb(SBLKCTL(base)) & 0x08)
	{
	  scsi_id = scsi_id + 8;  /* B channel */
	}
	target_mask = (0x01 << scsi_id);
	scratch = inb(HA_TARG_SCRATCH(base) + scsi_id);
	/*
	 * The maximum offset for a wide device is 0x08; for a
	 * 8-bit bus device the maximum offset is 0x0f.
	 */
	if (scratch & 0x80)
	{
	  max_offset = 0x08;
	}
	else
	{
	  max_offset = 0x0f;
	}
	aic7xxx_scsirate(&rate, transfer, MIN(offset, max_offset), scsi_id);
	/*
	 * Preserve the wide transfer flag.
	 */
	rate = rate | (scratch & 0x80);
	outb(rate, HA_TARG_SCRATCH(base) + scsi_id);
	outb(rate, SCSIRATE(base));
	if ((rate & 0xf) == 0)
	{ /*
	   * The requested rate was so low that asynchronous transfers
	   * are faster (not to mention the controller won't support
	   * them), so we issue a reject to ensure we go to asynchronous
	   * transfers.
	   */
	   outb(SEND_REJ, HA_RETURN_1(base));
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
	    outb(0, HA_RETURN_1(base));
	  }
	  else
	  {
	    /*
	     * Send our own SDTR in reply.
	     */
	    printk("Sending SDTR!!\n");
	    outb(SEND_SDTR, HA_RETURN_1(base));
	  }
	}
	/*
	 * Clear the flags.
	 */
	p->needsdtr = p->needsdtr & ~target_mask;
	p->sdtr_pending = p->sdtr_pending & ~target_mask;
	break;

      case MSG_WDTR:
      {
	bus_width = inb(ACCUM(base));
	scsi_id = inb(SCSIID(base)) >> 0x04;
	if (inb(SBLKCTL(base)) & 0x08)
	{
	  scsi_id = scsi_id + 8;  /* B channel */
	}
	printk("Received MSG_WDTR, scsi_id = %d, "
	       "needwdtr = 0x%x\n", scsi_id, p->needwdtr);
	scratch = inb(HA_TARG_SCRATCH(base) + scsi_id);

	target_mask = (0x01 << scsi_id);
	if (p->wdtr_pending & target_mask)
	{
	  /*
	   * Don't send an WDTR back to the target, since we asked first.
	   */
	  outb(0, HA_RETURN_1(base));
	  switch (bus_width)
	  {
	    case BUS_8_BIT:
	      scratch = scratch & 0x7F;
	      break;

	    case BUS_16_BIT:
	      printk("aic7xxx_isr: target %d using 16 bit transfers\n",
		     scsi_id);
	      scratch = scratch | 0x80;
	      break;
	  }
	}
	else
	{
	  /*
	   * Send our own WDTR in reply.
	   */
	  printk("Will send WDTR!!\n");
	  switch (bus_width)
	  {
	    case BUS_8_BIT:
	      scratch = scratch & 0x7F;
	      break;

	    case BUS_32_BIT:
	      /* Negotiate 16 bits. */
	      bus_width = BUS_16_BIT;
	      /* Yes, we mean to fall thru here */

	    case BUS_16_BIT:
	      printk("aic7xxx_isr: target %d using 16 bit transfers\n",
		     scsi_id);
	      scratch = scratch | 0x80;
	      break;
	  }
	  outb(bus_width | SEND_WDTR, HA_RETURN_1(base));
	}
	p->needwdtr = p->needwdtr & ~target_mask;
	p->wdtr_pending = p->wdtr_pending & ~target_mask;
	outb(scratch, HA_TARG_SCRATCH(base) + scsi_id);
	outb(scratch, SCSIRATE(base));
	break;
      }

      case MSG_REJECT:
      {
	/*
	 * What we care about here is if we had an
	 * outstanding SDTR or WDTR message for this
	 * target. If we did, this is a signal that
	 * the target is refusing negotiation.
	 */

	unsigned char targ_scratch, scsi_id;
	unsigned short mask;

	scsi_id = inb(SCSIID(base)) >> 0x04;
	if (inb(SBLKCTL(base)) & 0x08)
	{
	  scsi_id = scsi_id + 8;
	}

	mask = (0x01 << scsi_id);

	targ_scratch = inb(HA_TARG_SCRATCH(base) + scsi_id);

	if (p->wdtr_pending & mask)
	{
	  /*
	   * note 8bit xfers and clear flag
	   */
	  targ_scratch = targ_scratch & 0x7F;
	  p->needwdtr = p->needwdtr & ~mask;
	  p->wdtr_pending = p->wdtr_pending & ~mask;
	  outb(targ_scratch, HA_TARG_SCRATCH(base) + scsi_id);
	  printk("aic7xxx: target %d refusing WIDE negotiation. Using "
		 "8 bit transfers\n", scsi_id);
	}
	else
	{
	  if (p->sdtr_pending & mask)
	  {
	    /*
	     * note asynch xfers and clear flag
	     */
	    targ_scratch = targ_scratch & 0xF0;
	    p->needsdtr = p->needsdtr & ~mask;
	    p->sdtr_pending = p->sdtr_pending & ~mask;
	    outb(targ_scratch, HA_TARG_SCRATCH(base) + scsi_id);
	    printk("aic7xxx: target %d refusing syncronous negotiation. Using "
		   "asyncronous transfers\n", scsi_id);
	  }
	  /*
	   * Otherwise, we ignore it.
	   */
	}
	outb(targ_scratch, HA_TARG_SCRATCH(base) + scsi_id);
	outb(targ_scratch, SCSIRATE(base));
	break;
      }

      case BAD_STATUS:
	scsi_id = inb(SCSIID(base)) >> 0x04;
	scbptr = inb(SCBPTR(base));
	scb = &(p->scb_array[scbptr]);
	outb(0, HA_RETURN_1(base));   /* CHECK_CONDITION may change this */
	if ((scb->state != SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk("aic7xxx_isr: referenced scb not valid "
		 "during seqint 0x%x scb(%d) state(%x), cmd(%x)\n",
		 intstat, scbptr, scb->state, (unsigned int) scb->cmd);
	}
	else
	{
	  cmd = scb->cmd;
	  aic7xxx_getscb(base, scb);
	  aic7xxx_status(cmd) = scb->target_status;

	  cmd->result = cmd->result | scb->target_status;

	  /*
	   * This test is just here for debugging purposes.
	   * It will go away when the timeout problem is resolved.
	   */
	  switch (status_byte(scb->target_status))
	  {
	    case GOOD:
	      break;

	    case CHECK_CONDITION:
	      if ((aic7xxx_error(cmd) == 0) && !(cmd->flags & WAS_SENSE))
	      {
		void         *req_buf;
#ifndef AIC7XXX_USE_SG
		unsigned int  req_buflen;
#endif

		/* Update the timeout for the SCSI command. */
/*                update_timeout(cmd, SENSE_TIMEOUT); */

		/* Send a sense command to the requesting target. */
		cmd->flags = cmd->flags | WAS_SENSE;
		memcpy((void *) scb->sense_cmd, (void *) generic_sense,
		       sizeof(generic_sense));

		scb->sense_cmd[1] = cmd->lun << 5;
		scb->sense_cmd[4] = sizeof(cmd->sense_buffer);

#ifdef AIC7XXX_USE_SG
		scb->sense_sg.address = (char *) &cmd->sense_buffer;
		scb->sense_sg.length = sizeof(cmd->sense_buffer);
		req_buf = &scb->sense_sg;
#else
		req_buf = &cmd->sense_buffer;
		req_buflen = sizeof(cmd->sense_buffer);
#endif
		cmd->cmd_len = COMMAND_SIZE(cmd->cmnd[0]);
		memset(scb, 0, SCB_DOWNLOAD_SIZE);
		scb->target_channel_lun = ((cmd->target << 4) & 0xF0) |
		    ((cmd->channel & 0x01) << 3) | (cmd->lun & 0x07);
		addr = scb->sense_cmd;
		scb->SCSI_cmd_length = COMMAND_SIZE(scb->sense_cmd[0]);
		memcpy(scb->SCSI_cmd_pointer, &addr,
		       sizeof(scb->SCSI_cmd_pointer));
#ifdef AIC7XXX_USE_SG
		scb->SG_segment_count = 1;
		memcpy (scb->SG_list_pointer, &req_buf,
			sizeof(scb->SG_list_pointer));
#else
		scb->SG_segment_count = 0;
		memcpy (scb->data_pointer, &req_buf,
			sizeof(scb->data_pointer));
		memcpy (scb->data_count, &req_buflen, 3);
#endif

		outb(SCBAUTO, SCBCNT(base));
		asm volatile("cld\n\t"
			     "rep\n\t"
			     "outsb"
			     : /* no output */
			     :"S" (scb), "c" (SCB_DOWNLOAD_SIZE), "d" (SCBARRAY(base))
			     :"si", "cx", "dx");
		outb(0, SCBCNT(base));
		outb(SCB_LIST_NULL, (SCBARRAY(base) + 30));

		/*
		 * Add this SCB to the "waiting for selection" list.
		 */
		head = inb(WAITING_SCBH(base));
		tail = inb(WAITING_SCBT(base));
		if (head & SCB_LIST_NULL)
		{ /* list is empty */
		  head = scb->position;
		  tail = SCB_LIST_NULL;
		}
		else
		{
		  if (tail & SCB_LIST_NULL)
		  { /* list has one element */
		    tail = scb->position;
		    outb(head, SCBPTR(base));
		    outb(tail, (SCBARRAY(base) + 30));
		  }
		  else
		  { /* list has more than one element */
		    outb(tail, SCBPTR(base));
		    tail = scb->position;
		    outb(tail, (SCBARRAY(base) + 30));
		  }
		}
		outb(head, WAITING_SCBH(base));
		outb(tail, WAITING_SCBT(base));
		outb(SEND_SENSE, HA_RETURN_1(base));
	      }  /* first time sense, no errors */
	      else
	      {
		/*
		 * Indicate that we asked for sense, have the sequencer do
		 * a normal command complete, and have the scsi driver handle
		 * this condition.
		 */
		cmd->flags = cmd->flags | ASKED_FOR_SENSE;
	      }
	      break;

	    case BUSY:
	      printk("aic7xxx_isr: Target busy\n");
	      if (!aic7xxx_error(cmd))
	      {
		aic7xxx_error(cmd) = DID_BUS_BUSY;
	      }
	      break;

	    case QUEUE_FULL:
	      printk("aic7xxx_isr: Queue full\n");
	      if (!aic7xxx_error(cmd))
	      {
		aic7xxx_error(cmd) = DID_RETRY_COMMAND;
	      }
	      break;

	    default:
	      printk("aic7xxx_isr: Unexpected target status 0x%x\n",
		     scb->target_status);
	      if (!aic7xxx_error(cmd))
	      {
		aic7xxx_error(cmd) = DID_RETRY_COMMAND;
	      }
	      break;
	  }  /* end switch */
	}  /* end else of */
	break;

      case RESIDUAL:
	scbptr = inb(SCBPTR(base));
	scb = &(p->scb_array[scbptr]);
	if ((scb->state != SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk("aic7xxx_isr: referenced scb not valid "
		 "during seqint 0x%x scb(%d) state(%x), cmd(%x)\n",
		 intstat, scbptr, scb->state, (unsigned int) scb->cmd);
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

	    actual -= ((inb(SCBARRAY(base + 17)) << 16) |
		       (inb(SCBARRAY(base + 16)) <<  8) |
		       inb(SCBARRAY(base + 15)));

	    if (actual < cmd->underflow)
	    {
	      printk("aic7xxx: target %d underflow - "
		     "wanted (at least) %u, got %u\n",
		     cmd->target, cmd->underflow, actual);

	      aic7xxx_error(cmd) = DID_RETRY_COMMAND;
	      aic7xxx_status(cmd) = scb->target_status;
	    }
	  }
	}
	break;

      case ABORT_TAG:
	scbptr = inb(SCBPTR(base));
	scb = &(p->scb_array[scbptr]);
	if ((scb->state != SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk("aic7xxx_isr: referenced scb not valid "
		 "during seqint 0x%x scb(%d) state(%x), cmd(%x)\n",
		 intstat, scbptr, scb->state, (unsigned int) scb->cmd);
	}
	else
	{
	  cmd = scb->cmd;
	  /*
	   * We didn't recieve a valid tag back from the target
	   * on a reconnect.
	   */
	  printk("aic7xxx_isr: invalid tag recieved on channel %c "
		 "target %d, lun %d -- sending ABORT_TAG\n",
		  (cmd->channel & 0x01) ? 'B':'A',
		  cmd->target, cmd->lun & 0x07);
	  /*
	   *  This is a critical section, since we don't want the
	   *  queue routine mucking with the host data.
	   */
	  save_flags(flags);
	  cli();

	  /*
	   *  Process the command after marking the scb as free
	   *  and adding it to the free list.
	   */
	  scb->state = SCB_FREE;
	  scb->cmd = NULL;
	  scb->next = p->free_scb;      /* preserve next pointer */
	  p->free_scb = scb;            /* add at head of list */

	  restore_flags (flags);
	  cmd->result = (DID_RETRY_COMMAND << 16);
	  cmd->scsi_done(cmd);
	}
	break;

      case AWAITING_MSG:
	scbptr = inb(SCBPTR(base));
	scb = &(p->scb_array[scbptr]);
	if ((scb->state != SCB_ACTIVE) || (scb->cmd == NULL))
	{
	  printk("aic7xxx_isr: referenced scb not valid "
		 "during seqint 0x%x scb(%d) state(%x), cmd(%x)\n",
		 intstat, scbptr, scb->state, (unsigned int) scb->cmd);
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
	     outb(MSG_BUS_DEVICE_RESET, HA_MSG_START(base));
	     outb(1, HA_MSG_LEN(base));
	   }
	   else
	   {
	     panic ("aic7xxx_isr: AWAITING_SCB for an SCB that does "
		    "not have a waiting message");
	   }
	}
	break;

      default:               /* unknown */
	debug("aic7xxx_isr: seqint, intstat = 0x%x, scsisigi = 0x%x\n",
	      intstat, inb(SCSISIGI(base)));
	break;
    }
    outb(CLRSEQINT, CLRINT(base));
    UNPAUSE_SEQUENCER(p);
  }

  if (intstat & SCSIINT)
  {
    int status = inb(SSTAT1(base));

    scbptr = inb(SCBPTR(base));
    scb = &p->scb_array[scbptr];
    if ((scb->state != SCB_ACTIVE) || (scb->cmd == NULL))
    {
      printk("aic7xxx_isr: no command for scb (scsiint)\n");
      /*
       * Turn off the interrupt and set status
       * to zero, so that it falls through the
       * reset of the SCSIINT code.
       */
      outb(status, CLRSINT1(base));
      UNPAUSE_SEQUENCER(p);
      outb(CLRSCSIINT, CLRINT(base));
      status = 0;
      scb = NULL;
    }
    else
    {
      cmd = scb->cmd;

      /*
       * Only the SCSI Status 1 register has information
       * about exceptional conditions that we'd have a
       * SCSIINT about; anything in SSTAT0 will be handled
       * by the sequencer. Note that there can be multiple
       * bits set.
       */
      if (status & SELTO)
      {
	unsigned char target_mask = (1 << (cmd->target & 0x07));
	unsigned char waiting;

	/*
	 * Hardware selection timer has expired. Turn
	 * off SCSI selection sequence.
	 */
	outb(ENRSELI, SCSISEQ(base));
	cmd->result = (DID_TIME_OUT << 16);
	/*
	 * Clear an pending messages for the timed out
	 * target and mark the target as free.
	 */
	ha_flags = inb(HA_FLAGS(base));
	outb(ha_flags & ~ACTIVE_MSG, HA_FLAGS(base));

	if (scb->target_channel_lun & 0x88)
	{
	  active = inb(HA_ACTIVE1(base));
	  active = active & ~(target_mask);
	  outb(active, HA_ACTIVE1(base));
	}
	else
	{
	  active = inb(HA_ACTIVE0(base));
	  active = active & ~(target_mask);
	  outb(active, HA_ACTIVE0(base));
	}

#ifdef AIC7XXX_USE_DMA
	outb(SCB_NEEDDMA, SCBARRAY(base));
#endif

	/*
	 * Shut off the offending interrupt sources, reset
	 * the sequencer address to zero and unpause it,
	 * then call the high-level SCSI completion routine.
	 *
	 * WARNING!  This is a magic sequence!  After many
	 * hours of guesswork, turning off the SCSI interrupts
	 * in CLRSINT? does NOT clear the SCSIINT bit in
	 * INTSTAT. By writing to the (undocumented, unused
	 * according to the AIC-7770 manual) third bit of
	 * CLRINT, you can clear INTSTAT. But, if you do it
	 * while the sequencer is paused, you get a BRKADRINT
	 * with an Illegal Host Address status, so the
	 * sequencer has to be restarted first.
	 */
	outb(CLRSELTIMEO, CLRSINT1(base));

	outb(CLRSCSIINT, CLRINT(base));

	/* Shift the waiting for selection queue forward */
	waiting = inb(WAITING_SCBH(base));
	outb(waiting, SCBPTR(base));
	waiting = inb(SCBARRAY(base) + 30);
	outb(waiting, WAITING_SCBH(base));

	RESTART_SEQUENCER(p);
	/*
	 * This is a critical section, since we don't want the
	 * queue routine mucking with the host data.
	 */
	save_flags(flags);
	cli();

	/*
	 * Process the command after marking the scb as free
	 * and adding it to the free list.
	 */
	scb->state = SCB_FREE;
	scb->cmd = NULL;
	scb->next = p->free_scb;        /* preserve next pointer */
	p->free_scb = scb;              /* add at head of list */

	restore_flags(flags);

	cmd->scsi_done(cmd);
#if 0
  printk("aic7xxx_isr: SELTO scb(%d) state(%x), cmd(%x)\n",
	 scb->position, scb->state, (unsigned int) scb->cmd);
#endif
      }
      else
      {
	if (status & SCSIPERR)
	{
	  /*
	   * A parity error has occurred during a data
	   * transfer phase. Flag it and continue.
	   */
	  printk("aic7xxx: parity error on target %d, "
		 "channel %d, lun %d\n",
		 cmd->target,
		 cmd->channel & 0x01,
		 cmd->lun & 0x07);
	  aic7xxx_error(cmd) = DID_PARITY;

	  /*
	   * Clear interrupt and resume as above.
	   */
	  outb(CLRSCSIPERR, CLRSINT1(base));
	  UNPAUSE_SEQUENCER(p);

	  outb(CLRSCSIINT, CLRINT(base));
	  scb = NULL;
	}
	else
	{
	  if (! (status & BUSFREE))
	  {
	     /*
	      * We don't know what's going on. Turn off the
	      * interrupt source and try to continue.
	      */
	     printk("aic7xxx_isr: sstat1 = 0x%x\n", status);
	     outb(status, CLRSINT1(base));
	     UNPAUSE_SEQUENCER(p);
	     outb(CLRSCSIINT, CLRINT(base));
	     scb = NULL;
	  }
	}
      }
    }  /* else */
  }

  if (intstat & CMDCMPLT)
  {
    int complete;

    /*
     * The sequencer will continue running when it
     * issues this interrupt. There may be >1 commands
     * finished, so loop until we've processed them all.
     */
    do {
      complete = inb(QOUTFIFO(base));

      scb = &(p->scb_array[complete]);
      if ((scb->state != SCB_ACTIVE) || (scb->cmd == NULL))
      {
	printk("aic7xxx warning: "
	       "no command for scb %d (cmdcmplt)\n"
	       "QOUTCNT = %d, SCB state = 0x%x, CMD = 0x%x\n",
	       complete, inb(QOUTFIFO(base)),
	       scb->state, (unsigned int) scb->cmd);
	outb(CLRCMDINT, CLRINT(base));
	continue;
      }
      cmd = scb->cmd;

      cmd->result = (aic7xxx_error(cmd) << 16) | aic7xxx_status(cmd);
      if ((cmd->flags & WAS_SENSE) && !(cmd->flags & ASKED_FOR_SENSE))
      { /* Got sense information. */
	cmd->flags = cmd->flags & ASKED_FOR_SENSE;
      }
#if 0
      printk("aic7xxx_intr: (complete) state = %d, cmd = 0x%x, free = 0x%x\n",
	     scb->state, (unsigned int) scb->cmd, (unsigned int) p->free_scb);
#endif
      /*
       * This is a critical section, since we don't want the
       * queue routine mucking with the host data.
       */
      save_flags(flags);
      cli();

      scb->state = SCB_FREE;
      scb->next = p->free_scb;
      scb->cmd = NULL;
      p->free_scb = &(p->scb_array[scb->position]);

      restore_flags(flags);
#if 0
  if (scb != &p->scb_array[scb->position])
  {
    printk("aic7xxx_isr: (complete) address mismatch, pos %d\n", scb->position);
  }
  printk("aic7xxx_isr: (complete) state = %d, cmd = 0x%x, free = 0x%x\n",
	 scb->state, (unsigned int) scb->cmd, (unsigned int) p->free_scb);
#endif

      cmd->scsi_done(cmd);

      /*
       * Clear interrupt status before checking
       * the output queue again. This eliminates
       * a race condition whereby a command could
       * complete between the queue poll and the
       * interrupt clearing, so notification of the
       * command being complete never made it back
       * up to the kernel.
       */
      outb(CLRCMDINT, CLRINT(base));
    } while (inb(QOUTCNT(base)));
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
aic7xxx_probe(int slot, int base)
{
  int i;
  unsigned char buf[4];

  static struct {
    int n;
    unsigned char signature[sizeof(buf)];
    aha_type type;
  } AIC7xxx[] = {
    { 4, { 0x04, 0x90, 0x77, 0x71 }, AIC_274x },  /* host adapter 274x */
    { 4, { 0x04, 0x90, 0x77, 0x70 }, AIC_274x },  /* motherboard 274x  */
    { 4, { 0x04, 0x90, 0x77, 0x56 }, AIC_284x },  /* 284x, BIOS enabled */
    { 4, { 0x04, 0x90, 0x77, 0x57 }, AIC_284x }   /* 284x, BIOS disabled */
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
	return(AIC7xxx[i].type);
      }

      printk("aic7xxx disabled at slot %d, ignored\n", slot);
    }
  }

  return(AIC_NONE);
}

/*+F*************************************************************************
 * Function:
 *   read_seeprom
 *
 * Description:
 *   Reads the serial EEPROM and returns 1 if successful and 0 if
 *   not successful.
 *
 *   The instruction set of the 93C46 chip is as follows:
 *
 *               Start  OP
 *     Function   Bit  Code  Address    Data     Description
 *     -------------------------------------------------------------------
 *     READ        1    10   A5 - A0             Reads data stored in memory,
 *                                               starting at specified address
 *     EWEN        1    00   11XXXX              Write enable must preceed
 *                                               all programming modes
 *     ERASE       1    11   A5 - A0             Erase register A5A4A3A2A1A0
 *     WRITE       1    01   A5 - A0   D15 - D0  Writes register
 *     ERAL        1    00   10XXXX              Erase all registers
 *     WRAL        1    00   01XXXX    D15 - D0  Writes to all registers
 *     EWDS        1    00   00XXXX              Disables all programming
 *                                               instructions
 *     *Note: A value of X for address is a don't care condition.
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
read_seeprom(int base, struct seeprom_config *sc)
{
  int i = 0, k = 0;
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
  while ((inb(SEECTL(base)) & SEERDY) == 0)	\
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
  outb(SEEMS, SEECTL(base));
  timeout = jiffies + 100;  /* 1 second timeout */
  while ((jiffies < timeout) && ((inb(SEECTL(base)) & SEERDY) == 0))
  {
    ;  /* Do nothing!  Wait for access to be granted. */
  }
  if ((inb(SEECTL(base)) & SEERDY) == 0)
  {
    outb (0, SEECTL(base));
    return (0);
  }

  /*
   * Read the first 32 registers of the seeprom.  For the 7870,
   * the 93C46 SEEPROM is a 1024-bit device with 64 16-bit registers
   * but only the first 32 are used by Adaptec BIOS.  The loop
   * will range from 0 to 31.
   */
  for (k = 0; k < (sizeof(*sc) / 2); k = k + 1)
  {
    /* Send chip select for one clock cycle. */
    outb(SEEMS | SEECK | SEECS, SEECTL(base));
    CLOCK_PULSE(base);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i = i + 1)
    {
      temp = SEEMS | SEECS | (seeprom_read.bits[i] << 1);
      outb(temp, SEECTL(base));
      CLOCK_PULSE(base);
      temp = temp ^ SEECK;
      outb(temp, SEECTL(base));
      CLOCK_PULSE(base);
    }
    /* Send the 6 bit address (MSB first, LSB last). */
    for (i = 5; i >= 0; i = i - 1)
    {
      temp = k;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = SEEMS | SEECS | (temp << 1);
      outb(temp, SEECTL(base));
      CLOCK_PULSE(base);
      temp = temp ^ SEECK;
      outb(temp, SEECTL(base));
      CLOCK_PULSE(base);
    }

    /*
     * Now read the 16 bit register.  An initial 0 precedes the
     * register contents which begins with bit 15 (MSB) and ends
     * with bit 0 (LSB).  The initial 0 will be shifted off the
     * top of our word as we let the loop run from 0 to 16.
     */
    for (i = 0; i <= 16; i = i + 1)
    {
      temp = SEEMS | SEECS;
      outb(temp, SEECTL(base));
      CLOCK_PULSE(base);
      temp = temp ^ SEECK;
      seeprom[k] = (seeprom[k] << 1) | (inb(SEECTL(base)) & SEEDI);
      outb(temp, SEECTL(base));
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

    /* Reset the chip select for the next command cycle. */
    outb(SEEMS, SEECTL(base));
    CLOCK_PULSE(base);
    outb(SEEMS | SEECK, SEECTL(base));
    CLOCK_PULSE(base);
    outb(SEEMS, SEECTL(base));
    CLOCK_PULSE(base);
  }

  if (checksum != sc->checksum)
  {
    printk ("aic7xxx : SEEPROM checksum error, ignoring SEEPROM settings.\n");
    return (0);
  }

#if 0
  printk ("Computed checksum 0x%x, checksum read 0x%x\n", checksum, sc->checksum);
  printk ("Serial EEPROM:");
  for (k = 0; k < (sizeof(*sc) / 2); k = k + 1)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk ("\n              ");
    }
    printk (" 0x%x", seeprom[k]);
  }
  printk ("\n");
#endif

  /* Release access to the memory port and the serial EEPROM. */
  outb(0, SEECTL(base));
  return (1);
}

/*+F*************************************************************************
 * Function:
 *   detect_maxscb
 *
 * Description:
 *   Return the maximum number of SCB's allowed for a given controller.
 *-F*************************************************************************/
static int
detect_maxscb(aha_type type, int base)
{
  unsigned char sblkctl_reg;
  int maxscb = 0;

  switch (type)
  {
    case AIC_274x:
    case AIC_284x:
      /*
       * Check for Rev C or E boards. Rev E boards can supposedly have
       * more than 4 SCBs, while the Rev C boards are limited to 4 SCBs.
       * Until we know how to access more than 4 SCBs for the Rev E chips,
       * we limit them, along with the Rev C chips, to 4 SCBs.
       *
       * The Rev E boards have a read/write autoflush bit in the
       * SBLKCTL registor, while in the Rev C boards it is read only.
       */
      sblkctl_reg = inb(SBLKCTL(base)) ^ AUTOFLUSHDIS;
      outb(sblkctl_reg, SBLKCTL(base));
      if (inb(SBLKCTL(base)) == sblkctl_reg)
      {  /* We detected a Rev E board. */
	printk("aic7770: Rev E and subsequent; using 4 SCB's\n");
	outb(sblkctl_reg ^ AUTOFLUSHDIS, SBLKCTL(base));
	maxscb = 4;
      }
      else
      {
	printk("aic7770: Rev C and previous; using 4 SCB's\n");
	maxscb = 4;
      }
      break;

    case AIC_7850:
      maxscb = 3;
      break;

    case AIC_7870:
      maxscb = 16;
      break;

    case AIC_7872:
      /*
       * Really has 255, but we'll wait to verify that we access
       * them the same way and do not have to set the card to
       * use the memory port to access external SCB RAM.
       */
      maxscb = 16;
      break;

    case AIC_NONE:
      /*
       * This should never happen... But just in case.
       */
      break;
  }

  return(maxscb);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_register
 *
 * Description:
 *   Register a Adaptec aic7xxx chip SCSI controller with the kernel.
 *-F*************************************************************************/
static int
aic7xxx_register(Scsi_Host_Template *template, aha_type type,
		 int base, unsigned char irq)
{
  static char * board_name[] = {"", "274x", "284x", "7870", "7850", "7872"};
  int i;
  unsigned char sblkctl;
  int max_targets;
  int found = 1;
  unsigned char target_settings;
  unsigned char scsi_conf;
  int have_seeprom = 0;
  struct Scsi_Host *host;
  struct aic7xxx_host *p;
  struct aic7xxx_host_config config;
  struct seeprom_config sc;

  config.type = type;
  config.base = base;
  config.irq = irq;
  config.parity = AIC_UNKNOWN;
  config.low_term = AIC_UNKNOWN;
  config.high_term = AIC_UNKNOWN;
  config.busrtime = 0;

  /*
   * Lock out other contenders for our i/o space.
   */
  request_region(MINREG(base), MAXREG(base) - MINREG(base), "aic7xxx");

  switch (type)
  {
    case AIC_274x:
#if 1
      printk("aha274x: aic7770 hcntrl=0x%x\n", inb(HCNTRL(config.base)));
#endif
      /*
       * For some 274x boards, we must clear the CHIPRST bit
       * and pause the sequencer. For some reason, this makes
       * the driver work. For 284x boards, we give it a
       * CHIPRST just like the 294x boards.
       *
       * Use the BIOS settings to determine the interrupt
       * trigger type (level or edge) and use this value
       * for pausing and unpausing the sequencer.
       */
      config.unpause = (inb(HCNTRL(config.base)) & IRQMS) | INTEN;
      config.pause = config.unpause | PAUSE;
      config.extended = aic7xxx_extended;

      /*
       * I don't think we need to kick the reset again, the initial probe
       * does a reset, it seems that this is kicking a dead horse here.
       * So... I will try to just verify that the chip has come out of the
       * reset state and continue the same as the 284x.
       * In the Calgary version of the driver:
       *   1) Chip Reset
       *   2) Set unpause to IRQMS | INTEN
       *   3) If an interrupt occured without any commands queued, the
       *      unpause was set to just INTEN
       * I changed the initial reset code to just mask in the CHIPRST bit
       * and try to leave the other settings alone.
       *
       * I don't think we need the warning about chip reset not being clear.
       * On both my test machines (2842 & 2940), they work just fine with a
       * HCNTRL() of 0x5 (PAUSE | CHIPRST). Notice though, the 274x also
       * adds the INTEN flag, where neither the 284x or 294x do.
       */
      outb(config.pause | CHIPRST, HCNTRL(config.base));
      aic7xxx_delay(1);
      if (inb(HCNTRL(config.base)) & CHIPRST)
      {
	printk("aic7xxx_register: Chip reset not cleared; clearing manually.\n");
      }
      outb(config.pause, HCNTRL(config.base));

      /*
       * Just to be on the safe side with the 274x, we will re-read the irq
       * since there was some issue about reseting the board.
       */
      config.irq = inb(HA_INTDEF(config.base)) & 0x0F;
      config.busrtime = inb(HA_SCSICONF(config.base)) & 0x3C;

      /*
       * A reminder until this can be detected automatically.
       */
      printk("aha274x: extended translation %sabled\n",
	     config.extended ? "en" : "dis");
      break;

    case AIC_284x:
#if 1
      printk("aha284x: aic7770 hcntrl=0x%x\n", inb(HCNTRL(config.base)));
#endif
      outb(CHIPRST, HCNTRL(config.base));
      config.unpause = UNPAUSE_284X;
      config.pause = REQ_PAUSE; /* DWG would like to be like the rest */
      config.extended = aic7xxx_extended;
      config.irq = inb(HA_INTDEF(config.base)) & 0x0F;

      /*
       * A reminder until this can be detected automatically.
       */
      printk("aha284x: extended translation %sabled\n",
	     config.extended ? "en" : "dis");
      break;

    case AIC_7850:
    case AIC_7870:
    case AIC_7872:
#if 1
      printk("aic%s hcntrl=0x%x\n", board_name[type], inb(HCNTRL(config.base)));
#endif

      outb(CHIPRST, HCNTRL(config.base));
      config.unpause = UNPAUSE_294X;
      config.pause = config.unpause | PAUSE;
      config.extended = aic7xxx_extended;
      config.scsi_id = 7;

      printk ("aic78xx: Reading SEEPROM... ");
      have_seeprom = read_seeprom(base, &sc);
      if (! have_seeprom)
      {
	printk ("Unable to read SEEPROM\n");
      }
      else
      {
	printk ("done\n");
	config.extended = (sc.bios_control & CFEXTEND) >> 7;
	config.scsi_id = (sc.brtime_id & CFSCSIID);
	config.parity = (sc.adapter_control & CFSPARITY) ?
			 AIC_ENABLED : AIC_DISABLED;
	config.low_term = (sc.adapter_control & CFSTERM) ?
			      AIC_ENABLED : AIC_DISABLED;
	config.high_term = (sc.adapter_control & CFWSTERM) ?
			      AIC_ENABLED : AIC_DISABLED;
	config.busrtime = (sc.brtime_id & CFBRTIME) >> 8;
      }

      /*
       * XXX - force data fifo threshold to 100%. Why does this
       *       need to be done?
       */
      outb(inb(DSPCISTATUS(config.base)) | DFTHRESH, DSPCISTATUS(config.base));
      outb(config.scsi_id | DFTHRESH, HA_SCSICONF(config.base));

      /*
       * In case we are a wide card, place scsi ID in second conf byte.
       */
      outb(config.scsi_id, (HA_SCSICONF(config.base) + 1));

      /*
       * A reminder until this can be detected automatically.
       */
      printk("aic%s: extended translation %sabled\n", board_name[type],
	     config.extended ? "en" : "dis");
      break;

    default:
      panic("aic7xxx_register: internal error\n");
  }

  config.maxscb = detect_maxscb(type, base);

  if ((config.type == AIC_274x) || (config.type == AIC_284x))
  {
    if (config.pause & IRQMS)
    {
      printk("aic7xxx: Using Level Sensitive Interrupts\n");
    }
    else
    {
      printk("aic7xxx: Using Edge Triggered Interrupts\n");
    }
  }

  /*
   * Read the bus type from the SBLKCTL register. Set the FLAGS
   * register in the sequencer for twin and wide bus cards.
   */
  sblkctl = inb(SBLKCTL(base)) & 0x0F;  /* mask out upper two bits */
  switch (sblkctl)
  {
    case 0:     /* narrow/normal bus */
      config.scsi_id = inb(HA_SCSICONF(base)) & 0x07;
      config.bus_type = AIC_SINGLE;
      outb(0, HA_FLAGS(base));
      break;

    case 2:     /* Wide bus */
      config.scsi_id = inb(HA_SCSICONF(base) + 1) & 0x0F;
      config.bus_type = AIC_WIDE;
      printk("aic7xxx : Enabling wide channel of %s-Wide\n",
	     board_name[config.type]);
      outb(WIDE_BUS, HA_FLAGS(base));
      break;

    case 8:     /* Twin bus */
      config.scsi_id = inb(HA_SCSICONF(base)) & 0x07;
#ifdef AIC7XXX_TWIN_SUPPORT
      config.scsi_id_b = inb(HA_SCSICONF(base) + 1) & 0x07;
      config.bus_type = AIC_TWIN;
      printk("aic7xxx : Enabled channel B of %s-Twin\n",
	     board_name[config.type]);
      outb(TWIN_BUS, HA_FLAGS(base));
#else
      config.bus_type = AIC_SINGLE;
      printk("aic7xxx : Channel B of %s-Twin will be ignored\n",
	     board_name[config.type]);
      outb(0, HA_FLAGS(base));
#endif
      break;

    default:
      printk("aic7xxx is an unsupported type 0x%x, please "
	     "mail deang@ims.com\n", inb(SBLKCTL(base)));
      outb(0, HA_FLAGS(base));
      return(0);
  }

  /*
   * Clear the upper two bits. For the 294x cards, clearing the
   * upper two bits, will take the card out of diagnostic mode
   * and make the host adatper LED follow bus activity (will not
   * always be on).
   */
  outb(sblkctl, SBLKCTL(base));

  /*
   * The IRQ level in i/o port 4 maps directly onto the real
   * IRQ number. If it's ok, register it with the kernel.
   *
   * NB. the Adaptec documentation says the IRQ number is only
   *     in the lower four bits; the ECU information shows the
   *     high bit being used as well. Which is correct?
   *
   * The 294x cards (PCI) get their interrupt from PCI BIOS.
   */
  if (((config.type == AIC_274x) || (config.type == AIC_284x))
      && (config.irq < 9 || config.irq > 15))
  {
    printk("aic7xxx uses unsupported IRQ level, ignoring\n");
    return(0);
  }

  /*
   * Check the IRQ to see if it is shared by another aic7xxx
   * controller. If it is and sharing of IRQs is not defined,
   * then return 0 hosts found. If sharing of IRQs is allowed
   * or the IRQ is not shared by another host adapter, then
   * proceed.
   */
#ifndef AIC7XXX_SHARE_IRQS
   if (aic7xxx_boards[config.irq] != NULL)
   {
     printk("aic7xxx_register: Sharing of IRQs is not configured.\n");
     return(0);
   }
#endif

  /*
   * Print out debugging information before re-enabling
   * the card - a lot of registers on it can't be read
   * when the sequencer is active.
   */
  debug_config(&config);

  /*
   * Before registry, make sure that the offsets of the
   * struct scatterlist are what the sequencer will expect,
   * otherwise disable scatter-gather altogether until someone
   * can fix it. This is important since the sequencer will
   * DMA elements of the SG array in while executing commands.
   */
  if (template->sg_tablesize != SG_NONE)
  {
    struct scatterlist sg;

    if (SG_STRUCT_CHECK(sg))
    {
      printk("aic7xxx warning: kernel scatter-gather "
	     "structures changed, disabling it\n");
      template->sg_tablesize = SG_NONE;
    }
  }

  /*
   * Register each "host" and fill in the returned Scsi_Host
   * structure as best we can. Some of the parameters aren't
   * really relevant for bus types beyond ISA, and none of the
   * high-level SCSI code looks at it anyway. Why are the fields
   * there? Also save the pointer so that we can find the
   * information when an IRQ is triggered.
   */
  host = scsi_register(template, sizeof(struct aic7xxx_host));
  host->can_queue = config.maxscb;
#ifdef AIC7XXX_TAGGED_QUEUEING
  host->cmd_per_lun = 2;
#else
  host->cmd_per_lun = 1;
#endif
  host->this_id = config.scsi_id;
  host->irq = config.irq;
  if (config.bus_type == AIC_WIDE)
  {
    host->max_id = 16;
  }
  if (config.bus_type == AIC_TWIN)
  {
    host->max_channel = 1;
  }

  p = (struct aic7xxx_host *) host->hostdata;

  /* Initialize the scb array by setting the state to free. */
  for (i = 0; i < AIC7XXX_MAXSCB; i = i + 1)
  {
    p->scb_array[i].state = SCB_FREE;
    p->scb_array[i].next = NULL;
    p->scb_array[i].cmd = NULL;
  }

  p->isr_count = 0;
  p->a_scanned = 0;
  p->b_scanned = 0;
  p->base = config.base;
  p->maxscb = config.maxscb;
  p->numscb = 0;
  p->extended = config.extended;
  p->type = config.type;
  p->bus_type = config.bus_type;
  p->have_seeprom = have_seeprom;
  p->seeprom = sc;
  p->free_scb = NULL;
  p->next = NULL;

  p->unpause = config.unpause;
  p->pause = config.pause;

  if (aic7xxx_boards[config.irq] == NULL)
  {
    /*
     * Register IRQ with the kernel.
     */
    if (request_irq(config.irq, aic7xxx_isr, SA_INTERRUPT, "aic7xxx"))
    {
      printk("aic7xxx couldn't register irq %d, ignoring\n", config.irq);
      return(0);
    }
    aic7xxx_boards[config.irq] = host;
  }
  else
  {
    /*
     * We have found a host adapter sharing an IRQ of a previously
     * registered host adapter. Add this host adapter's Scsi_Host
     * to the beginning of the linked list of hosts at the same IRQ.
     */
    p->next = aic7xxx_boards[config.irq];
    aic7xxx_boards[config.irq] = host;
  }

  /*
   * Load the sequencer program, then re-enable the board -
   * resetting the AIC-7770 disables it, leaving the lights
   * on with nobody home. On the PCI bus you *may* be home,
   * but then your mailing address is dynamically assigned
   * so no one can find you anyway :-)
   */
  printk("aic7xxx: Downloading sequencer code..");
  aic7xxx_loadseq(base);

  /* Set Fast Mode and Enable the board */
  outb(FASTMODE, SEQCTL(base));

  if ((p->type == AIC_274x || p->type == AIC_284x))
  {
    outb(ENABLE, BCTL(base));
  }

  printk("done.\n");

  /*
   * Set the SCSI Id, SXFRCTL1, and SIMODE1, for both channels
   */
  if (p->bus_type == AIC_TWIN)
  {
    /*
     * The device is gated to channel B after a chip reset,
     * so set those values first.
     */
    outb(config.scsi_id_b, SCSIID(base));
    scsi_conf = inb(HA_SCSICONF(base) + 1) & (ENSPCHK | STIMESEL);
    scsi_conf = scsi_conf | ENSTIMER | ACTNEGEN | STPWEN;
    outb(scsi_conf, SXFRCTL1(base));
    outb(ENSELTIMO | ENSCSIPERR, SIMODE1(base));
    /* Select Channel A */
    outb(0, SBLKCTL(base));
  }
  outb(config.scsi_id, SCSIID(base));
  scsi_conf = inb(HA_SCSICONF(base)) & (ENSPCHK | STIMESEL);
  outb(scsi_conf | ENSTIMER | ACTNEGEN | STPWEN, SXFRCTL1(base));
  outb(ENSELTIMO | ENSCSIPERR, SIMODE1(base));

  /* Look at the information that board initialization or the board
   * BIOS has left us. In the lower four bits of each target's
   * scratch space any value other than 0 indicates that we should
   * initiate synchronous transfers. If it's zero, the user or the
   * BIOS has decided to disable synchronous negotiation to that
   * target so we don't activate the needsdtr flag.
   */
  p->needsdtr_copy = 0;
  p->sdtr_pending = 0;
  p->needwdtr_copy = 0;
  p->wdtr_pending = 0;
  if (p->bus_type == AIC_SINGLE)
  {
    max_targets = 8;
  }
  else
  {
    max_targets = 16;
  }

  for (i = 0; i < max_targets; i = i + 1)
  {
    if (have_seeprom)
    {
      target_settings = (sc.device_flags[i] & CFXFER) << 4;
      if (sc.device_flags[i] & CFSYNCH)
      {
	p->needsdtr_copy = p->needsdtr_copy | (0x01 << i);
      }
      if ((sc.device_flags[i] & CFWIDEB) && (p->bus_type == AIC_WIDE))
      {
	p->needwdtr_copy = p->needwdtr_copy | (0x01 << i);
      }
    }
    else
    {
      target_settings = inb(HA_TARG_SCRATCH(base) + i);
      if (target_settings & 0x0F)
      {
	p->needsdtr_copy = p->needsdtr_copy | (0x01 << i);
	/*
	 * Default to asynchronous transfers (0 offset)
	 */
	target_settings = target_settings & 0xF0;
      }
      /*
       * If we are not wide, forget WDTR. This makes the driver
       * work on some cards that don't leave these fields cleared
       * when BIOS is not installed.
       */
      if ((target_settings & 0x80) && (p->bus_type == AIC_WIDE))
      {
	p->needwdtr_copy = p->needwdtr_copy | (0x01 << i);
	target_settings = target_settings & 0x7F;
      }
    }
    outb(target_settings, (HA_TARG_SCRATCH(base) + i));
  }

  p->needsdtr = p->needsdtr_copy;
  p->needwdtr = p->needwdtr_copy;
  printk("NeedSdtr = 0x%x, 0x%x\n", p->needsdtr_copy, p->needsdtr);
  printk("NeedWdtr = 0x%x, 0x%x\n", p->needwdtr_copy, p->needwdtr);

  /* 
   * Clear the control byte for every SCB so that the sequencer
   * doesn't get confused and think that one of them is valid
   */
  for (i = 0; i < config.maxscb; i = i + 1)
  {
    outb(i, SCBPTR(base));
    outb(0, SCBARRAY(base));
  }

  /*
   * For reconnecting targets, the sequencer code needs to
   * know how many SCBs it has to search through.
   */
  outb(config.maxscb, HA_SCBCOUNT(base));

  /*
   * Clear the active flags - no targets are busy.
   */
  outb(0, HA_ACTIVE0(base));
  outb(0, HA_ACTIVE1(base));

  /* We don't have any waiting selections */
  outb (SCB_LIST_NULL, WAITING_SCBH(base));
  outb (SCB_LIST_NULL, WAITING_SCBT(base));

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
    printk("Resetting the SCSI bus...\n");
    outb(SCSIRSTO, SCSISEQ(base));
    udelay(1000);
    outb(0, SCSISEQ(base));
    aic7xxx_delay(AIC7XXX_RESET_DELAY);
  }

  /*
   * Unpause the sequencer before returning and enable
   * interrupts - we shouldn't get any until the first
   * command is sent to us by the high-level SCSI code.
   */
  UNPAUSE_SEQUENCER(p);
  return(found);
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
  aha_type type = AIC_NONE;
  int found = 0, slot, base;
  unsigned char irq = 0;
  int i;

  /*
   * Since we may allow sharing of IRQs, it is imperative
   * that we "null-out" the aic7xxx_boards array. It is
   * not guaranteed to be initialized to 0 (NULL). We use
   * a NULL entry to indicate that no prior hosts have
   * been found/registered for that IRQ.
   */
  for (i = 0; i <= MAXIRQ; i++)
  {
    aic7xxx_boards[i] = NULL;
  }

  /*
   * EISA/VL-bus card signature probe.
   */
  for (slot = MINSLOT; slot <= MAXSLOT; slot++)
  {
    base = SLOTBASE(slot);

    if (check_region(MINREG(base), MAXREG(base) - MINREG(base)))
    {
      /*
       * Some other driver has staked a
       * claim to this i/o region already.
       */
      continue;
    }

    type = aic7xxx_probe(slot, HID0(base));
    if (type != AIC_NONE)
    {
      printk("aic7xxx: hcntrl=0x%x\n", inb(HCNTRL(base)));
#if 0
      outb(inb(HCNTRL(base)) | CHIPRST, HCNTRL(base));
      irq = inb(HA_INTDEF(base)) & 0x0F;
#endif

      /*
       * We "find" a AIC-7770 if we locate the card
       * signature and we can set it up and register
       * it with the kernel without incident.
       */
      found += aic7xxx_register(template, type, base, irq);
    }
  }

#ifdef CONFIG_PCI

#define DEVREVID  0x08
#define DEVCONFIG 0x40
#define DEVSTATUS 0x41
#define RAMPSM    0x02

/* This should be defined in pci.h */
#define PCI_DEVICE_ID_ADAPTEC_7850	0x5078
#define PCI_DEVICE_ID_ADAPTEC_7872	0x7278

  /*
   * PCI-bus probe.
   */
  if (pcibios_present())
  {
    int error;
    int done = 0;
    unsigned int io_port;
    unsigned short index = 0;
    unsigned char pci_bus, pci_device_fn;
    unsigned char devrevid, devconfig, devstatus;
    char rev_id[] = {'B', 'C', 'D'};

    while (!done)
    {
      if ((!pcibios_find_device(PCI_VENDOR_ID_ADAPTEC,
				PCI_DEVICE_ID_ADAPTEC_294x,
				index, &pci_bus, &pci_device_fn)) ||
	   (!pcibios_find_device(PCI_VENDOR_ID_ADAPTEC,
				PCI_DEVICE_ID_ADAPTEC_2940,
				index, &pci_bus, &pci_device_fn)))
      {
	type = AIC_7870;
      }
      else
      {
	if (!pcibios_find_device(PCI_VENDOR_ID_ADAPTEC,
				PCI_DEVICE_ID_ADAPTEC_7850,
				index, &pci_bus, &pci_device_fn))
	{
	  type = AIC_7850;
	}
	else
	{
	  if (!pcibios_find_device(PCI_VENDOR_ID_ADAPTEC,
				  PCI_DEVICE_ID_ADAPTEC_7872,
				  index, &pci_bus, &pci_device_fn))
	  {
	    type = AIC_7872;
	  }
	  else
	  {
	    type = AIC_NONE;
	    done = 1;
	  }
	}
      }

      if (!done)
      {
	/*
	 * Read esundry information from PCI BIOS.
	 */
	error = pcibios_read_config_dword(pci_bus, pci_device_fn,
					  PCI_BASE_ADDRESS_0, &io_port);

	if (error)
	{
	  panic("aic7xxx_detect: error 0x%x reading i/o port.\n", error);
	}

	error = pcibios_read_config_byte(pci_bus, pci_device_fn,
					 PCI_INTERRUPT_LINE, &irq);
	if (error)
	{
	  panic("aic7xxx_detect: error %d reading irq.\n", error);
	}

	/*
	 * Make the base I/O register look like EISA and VL-bus.
	 */
	base = io_port - 0xC01;

	printk("aic7xxx: hcntrl=0x%x\n", inb(HCNTRL(base)));
	outb(inb(HCNTRL(base)) | CHIPRST, HCNTRL(base));

	error = pcibios_read_config_byte(pci_bus, pci_device_fn,
					 DEVREVID, &devrevid);
	if (devrevid < 3)
	{
	  printk ("aic7xxx_detect: AIC-7870 Rev %c\n", rev_id[devrevid]);
	}
	error = pcibios_read_config_byte(pci_bus, pci_device_fn,
					 DEVCONFIG, &devconfig);
	error = pcibios_read_config_byte(pci_bus, pci_device_fn,
					 DEVSTATUS, &devstatus);
	printk ("aic7xxx_detect: devconfig 0x%x, devstatus 0x%x\n",
		devconfig, devstatus);
	if (devstatus & RAMPSM)
	{
	  printk ("aic7xxx_detect: detected external SCB RAM, "
		  "mail deang@ims.com for test patch");
	}

	found += aic7xxx_register(template, type, base, irq);
	index += 1;
      }
    }
  }
#endif CONFIG_PCI

  template->name = (char *) aic7xxx_info(NULL);
  return(found);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_buildscb
 *
 * Description:
 *   Build a SCB.
 *-F*************************************************************************/
static void
aic7xxx_buildscb(struct aic7xxx_host *p,
		 Scsi_Cmnd *cmd,
		 struct aic7xxx_scb *scb)
{
  void *addr;
  unsigned length;
  unsigned short mask;

  /*
   * Setup the control byte if we need negotiation and have not
   * already requested it.
   */
#ifdef AIC7XXX_TAGGED_QUEUEING
  if (cmd->device->tagged_supported)
  {
    if (cmd->device->tagged_queue == 0)
    {
      printk ("aic7xxx_buildscb: Enabling tagged queuing for target %d, "
	      "channel %d\n", cmd->target, cmd->channel);
      cmd->device->tagged_queue = 1;
      cmd->device->current_tag = 1;  /* enable tagging */
    }
    cmd->tag = cmd->device->current_tag;
    cmd->device->current_tag = cmd->device->current_tag + 1;
    scb->control = scb->control | SCB_TE;
  }
#endif
  mask = (0x01 << cmd->target);
  if ((p->needwdtr & mask) && !(p->wdtr_pending & mask))
  {
    p->wdtr_pending = p->wdtr_pending | mask;
    scb->control = scb->control | SCB_NEEDWDTR;
#if 0
    printk("Sending WDTR request to target %d.\n", cmd->target);
#endif
  }
  else
  {
    if ((p->needsdtr & mask) && !(p->sdtr_pending & mask))
    {
      p->sdtr_pending = p->sdtr_pending | mask;
      scb->control = scb->control | SCB_NEEDSDTR;
#if 0
      printk("Sending SDTR request to target %d.\n", cmd->target);
#endif
    }
  }

#if 0
  printk("aic7xxx_queue: target %d, cmd 0x%x (size %u), wdtr 0x%x, mask 0x%x\n",
	 cmd->target, cmd->cmnd[0], cmd->cmd_len, p->needwdtr, mask);
#endif
  scb->target_channel_lun = ((cmd->target << 4) & 0xF0) |
	((cmd->channel & 0x01) << 3) | (cmd->lun & 0x07);

  /*
   * The interpretation of request_buffer and request_bufflen
   * changes depending on whether or not use_sg is zero; a
   * non-zero use_sg indicates the number of elements in the
   * scatter-gather array.
   *
   * The AIC-7770 can't support transfers of any sort larger
   * than 2^24 (three-byte count) without backflips. For what
   * the kernel is doing, this shouldn't occur. I hope.
   */
  length = aic7xxx_length(cmd, 0);

  if (length > 0xFFFFFF)
  {
    panic("aic7xxx_buildscb: can't transfer > 2^24 - 1 bytes\n");
  }

  /*
   * XXX - this relies on the host data being stored in a
   *       little-endian format.
   */
  addr = cmd->cmnd;
  scb->SCSI_cmd_length = cmd->cmd_len;
  memcpy(scb->SCSI_cmd_pointer, &addr, sizeof(scb->SCSI_cmd_pointer));

  if (cmd->use_sg)
  {
#if 0
    debug("aic7xxx_buildscb: SG used, %d segments, length %u\n",
	  cmd->use_sg, length);
#endif
    scb->SG_segment_count = cmd->use_sg;
    memcpy(scb->SG_list_pointer, &cmd->request_buffer,
	   sizeof(scb->SG_list_pointer));
  }
  else
  {
#if 0
    debug ("aic7xxx_buildscb: Creating scatterlist, addr=0x%lx, length=%d.\n",
	   (unsigned long) cmd->request_buffer, cmd->request_bufflen);
#endif
#ifdef AIC7XXX_USE_SG
    scb->SG_segment_count = 1;
    scb->sg.address = (char *) cmd->request_buffer;
    scb->sg.length = cmd->request_bufflen;
    addr = &scb->sg;
    memcpy(scb->SG_list_pointer, &addr, sizeof(scb->SG_list_pointer));
#else
    scb->SG_segment_count = 0;
    memcpy(scb->data_pointer, &cmd->request_buffer, sizeof(scb->data_pointer));
    memcpy(scb->data_count, &cmd->request_bufflen, 3);
#endif
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
  long flags;
#ifndef AIC7XXX_USE_DMA
  int old_scbptr;
#endif
  struct aic7xxx_host *p;
  struct aic7xxx_scb *scb;
  unsigned char curscb;

  p = (struct aic7xxx_host *) cmd->host->hostdata;

  /* Check to see if channel was scanned. */
 if (!p->a_scanned && (cmd->channel == 0))
  {
    printk("aic7xxx: Scanning channel A for devices.\n");
    p->a_scanned = 1;
  }
  else
  {
    if (!p->b_scanned && (cmd->channel == 1))
    {
      printk("aic7xxx: Scanning channel B for devices.\n");
      p->b_scanned = 1;
    }
  }

#if 0
  debug("aic7xxx_queue: cmd 0x%x (size %u), target %d, channel %d, lun %d\n",
	cmd->cmnd[0], cmd->cmd_len, cmd->target, cmd->channel,
	cmd->lun & 0x07);
#endif

  /*
   * This is a critical section, since we don't want the
   * interrupt routine mucking with the host data or the
   * card. Since the kernel documentation is vague on
   * whether or not we are in a cli/sti pair already, save
   * the flags to be on the safe side.
   */
  save_flags(flags);
  cli();

  /*
   * Find a free slot in the SCB array to load this command
   * into. Since can_queue is set to the maximum number of
   * SCBs for the card, we should always find one.
   *
   * First try to find an scb in the free list. If there are
   * none in the free list, then check the current number of
   * of scbs and take an unused one from the scb array.
   */
  scb = p->free_scb;
  if (scb != NULL)
  { /* found one in the free list */
    p->free_scb = scb->next;   /* remove and update head of list */
    /*
     * Warning! For some unknown reason, the scb at the head
     * of the free list is not the same address that it should
     * be. That's why we set the scb pointer taken by the
     * position in the array. The scb at the head of the list
     * should match this address, but it doesn't.
     */
    scb = &(p->scb_array[scb->position]);
    scb->control = 0;
    scb->state = SCB_ACTIVE;
  }
  else
  {
    if (p->numscb >= p->maxscb)
    {
      panic("aic7xxx_queue: couldn't find a free scb\n");
    }
    else
    {
      /*
       * Initialize the scb within the scb array. The
       * position within the array is the position on
       * the board that it will be loaded.
       */
      scb = &(p->scb_array[p->numscb]);
      memset(scb, 0, sizeof(*scb));

      scb->position = p->numscb;
      p->numscb = p->numscb + 1;
      scb->state = SCB_ACTIVE;
      scb->next_waiting = SCB_LIST_NULL;
      memcpy(scb->host_scb, &scb, sizeof(scb));
#ifdef AIC7XXX_USE_DMA
      scb->control = SCB_NEEDDMA;
#endif
      PAUSE_SEQUENCER(p);
      curscb = inb(SCBPTR(p->base));
      outb(scb->position, SCBPTR(p->base));
      aic7xxx_putdmascb(p->base, scb);
      outb(curscb, SCBPTR(p->base));
      UNPAUSE_SEQUENCER(p);
      scb->control = 0;
    }
  }

  scb->cmd = cmd;
  aic7xxx_position(cmd) = scb->position;

  /*
   * Construct the SCB beforehand, so the sequencer is
   * paused a minimal amount of time.
   */
  aic7xxx_buildscb(p, cmd, scb);

#if 0
  if (scb != &p->scb_array[scb->position])
  {
    printk("aic7xxx_queue: address of scb by position does not match scb address\n");
  }
  printk("aic7xxx_queue: SCB pos=%d, cmdptr=0x%x, state=%d, freescb=0x%x\n",
	 scb->position, (unsigned int) scb->cmd,
	 scb->state, (unsigned int) p->free_scb);
#endif
  /*
   * Pause the sequencer so we can play with its registers -
   * wait for it to acknowledge the pause.
   *
   * XXX - should the interrupts be left on while doing this?
   */
  PAUSE_SEQUENCER(p);

  /*
   * Save the SCB pointer and put our own pointer in - this
   * selects one of the four banks of SCB registers. Load
   * the SCB, then write its pointer into the queue in FIFO
   * and restore the saved SCB pointer.
   */
#ifdef AIC7XXX_USE_DMA
  aic7xxx_putscb(p->base, scb);
#else
  old_scbptr = inb(SCBPTR(p->base));
  outb(scb->position, SCBPTR(p->base));

  aic7xxx_putscb(p->base, scb);

  outb(scb->position, QINFIFO(p->base));
  outb(old_scbptr, SCBPTR(p->base));
#endif
  /*
   * Make sure the Scsi_Cmnd pointer is saved, the struct it
   * points to is set up properly, and the parity error flag
   * is reset, then unpause the sequencer and watch the fun
   * begin.
   */
  cmd->scsi_done = fn;
  aic7xxx_error(cmd) = DID_OK;
  aic7xxx_status(cmd) = 0;

  cmd->result = 0;
  memset (&cmd->sense_buffer, 0, sizeof (cmd->sense_buffer));

  UNPAUSE_SEQUENCER(p);
  restore_flags(flags);
  return(0);
}

/* return values from aic7xxx_kill */
typedef enum {
  k_ok,             /* scb found and message sent */
  k_busy,           /* message already present */
  k_absent,         /* couldn't locate scb */
  k_disconnect,     /* scb found, but disconnected */
} k_state;

/*+F*************************************************************************
 * Function:
 *   aic7xxx_kill
 *
 * Description:
 *   This must be called with interrupts disabled - it's going to
 *   be messing around with the host data, and an interrupt being
 *   fielded in the middle could get ugly.
 *
 *   Since so much of the abort and reset code is shared, this
 *   function performs more magic than it really should. If the
 *   command completes ok, then it will call scsi_done with the
 *   result code passed in. The unpause parameter controls whether
 *   or not the sequencer gets unpaused - the reset function, for
 *   instance, may want to do something more aggressive.
 *
 *   Note that the command is checked for in our SCB_array first
 *   before the sequencer is paused, so if k_absent is returned,
 *   then the sequencer is NOT paused.
 *-F*************************************************************************/
static k_state
aic7xxx_kill(Scsi_Cmnd *cmd, unsigned char message,
	     unsigned int result, int unpause)
{
  struct aic7xxx_host *p;
  struct aic7xxx_scb *scb;
  int i, active_scb, found, queued;
  unsigned char scbsave[AIC7XXX_MAXSCB];
  unsigned char flags;
  int scb_control;
  k_state status;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  scb = &p->scb_array[aic7xxx_position(cmd)];

#if 0
  printk("aic7xxx_kill: In the kill function...\n");
#endif
  PAUSE_SEQUENCER(p);

  /*
   * Case 1: In the QINFIFO
   *
   * This is the best case, really. Check to see if the
   * command is still in the sequencer's input queue. If
   * so, simply remove it. Reload the queue afterward.
   */
  queued = inb(QINCNT(p->base));

  for (i = found = 0; i < (queued - found); i++)
  {
    scbsave[i] = inb(QINFIFO(p->base));

    if (scbsave[i] == scb->position)
    {
      found = 1;
      i = i - 1;
    }
  }

  for (queued = 0; queued < i; queued++)
  {
    outb(scbsave[queued], QINFIFO(p->base));
  }

  if (found)
  {
    status = k_ok;
    goto complete;
  }

  active_scb = inb(SCBPTR(p->base));
  /*
   * Case 2: Not the active command
   *
   * Check the current SCB bank. If it's not the one belonging
   * to the command we want to kill, select the scb we want to
   * abort and turn off the disconnected bit. The driver will
   * then abort the command and notify us of the abort.
   */
  if (active_scb != scb->position)
  {
    outb(scb->position, SCBPTR(p->base));
    scb_control = inb(SCBARRAY(p->base));
    scb_control = scb_control & ~SCB_DIS;
    outb(scb_control, SCBARRAY(p->base));
    outb(active_scb, SCBPTR(p->base));
    status = k_disconnect;
    goto complete;
  }

  scb_control = inb(SCBARRAY(p->base));
  if (scb_control & SCB_DIS)
  {
    scb_control = scb_control & ~SCB_DIS;
    outb(scb_control, SCBARRAY(p->base));
    status = k_disconnect;
    goto complete;
  }

  /*
   * Presumably at this point our target command is active. Check
   * to see if there's a message already in effect. If not, place
   * our message in and assert ATN so the target goes into MESSAGE
   * OUT phase.
   */
  flags = inb(HA_FLAGS(p->base));
  if (flags & ACTIVE_MSG)
  {
    /*
     * If there is a message in progress, reset the bus
     * and have all devices renegotiate.
     */
    if (cmd->channel & 0x01)
    {
      p->needsdtr = p->needsdtr_copy & 0xFF00;
      p->sdtr_pending = p->sdtr_pending & 0x00FF;
      outb(0, HA_ACTIVE1(p->base));
    }
    else
    {
      if (p->bus_type == AIC_WIDE)
      {
	p->needsdtr = p->needsdtr_copy;
	p->needwdtr = p->needwdtr_copy;
	p->sdtr_pending = 0;
	p->wdtr_pending = 0;
	outb(0, HA_ACTIVE0(p->base));
	outb(0, HA_ACTIVE1(p->base));
      }
      else
      {
	p->needsdtr = p->needsdtr_copy & 0x00FF;
	p->sdtr_pending = p->sdtr_pending & 0xFF00;
	outb(0, HA_ACTIVE0(p->base));
      }
    }
    /* Reset the bus. */
    outb(SCSIRSTO, SCSISEQ(p->base));
    udelay(1000);
    outb(0, SCSISEQ(p->base));
    aic7xxx_delay(AIC7XXX_RESET_DELAY);

    status = k_busy;
    goto complete;
  }

  outb(flags | ACTIVE_MSG, HA_FLAGS(p->base));    /* active message */
  outb(1, HA_MSG_LEN(p->base));                   /* length = 1 */
  outb(message, HA_MSG_START(p->base));           /* message body */

  /*
   * Assert ATN. Use the value of SCSISIGO saved by the
   * sequencer code so we don't alter its contents radically
   * in the middle of something critical.
   */
  outb(inb(HA_SIGSTATE(p->base)) | 0x10, SCSISIGO(p->base));

  status = k_ok;

  /*
   * The command has been killed. Do the bookkeeping, unpause
   * the sequencer, and notify the higher-level SCSI code.
   */
complete:
  if (unpause)
  {
    UNPAUSE_SEQUENCER(p);
  }

  /*
   * Mark the scb as free and clear the scbs command pointer.
   * Add the scb to the head of the free list being careful
   * to preserve the next pointers.
   */
  scb->state = SCB_FREE;          /* mark the scb as free */
  scb->cmd = NULL;                /* clear the command pointer */
  scb->next = p->free_scb;        /* preserve next pointer */
  p->free_scb = scb;              /* add at head of free list */
  cmd->result = cmd->result << 16;
  cmd->scsi_done(cmd);
  return(status);
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
  int rv;
  long flags;

  save_flags(flags);
  cli();

  switch (aic7xxx_kill(cmd, ABORT, DID_ABORT, !0))
  {
    case k_ok:          rv = SCSI_ABORT_SUCCESS;        break;
    case k_busy:        rv = SCSI_ABORT_BUSY;           break;
    case k_absent:      rv = SCSI_ABORT_NOT_RUNNING;    break;
    case k_disconnect:  rv = SCSI_ABORT_SNOOZE;         break;
    default:            panic("aic7xxx_abort: internal error\n");
  }

  restore_flags(flags);
  return(rv);
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
aic7xxx_reset(Scsi_Cmnd *cmd)
{
  long flags;
  struct aic7xxx_host *p;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  save_flags(flags);
  cli();

  switch (aic7xxx_kill(cmd, BUS_DEVICE_RESET, DID_RESET, 0))
  {
    case k_ok:
      /*
       * The RESET message was sent to the target
       * with no problems. Flag that target as
       * needing a SDTR negotiation on the next
       * connection and restart the sequencer.
       */
      p->needsdtr = p->needsdtr & (1 << cmd->target);
      UNPAUSE_SEQUENCER(p);
      break;

    case k_absent:
      /*
       * The sequencer will not be paused if aic7xxx_kill()
       * couldn't find the command.
       */
      PAUSE_SEQUENCER(p);
      /* falls through */

    case k_busy:
      cmd->result = DID_RESET << 16;  /* return reset code */
      cmd->scsi_done(cmd);
      break;

    case k_disconnect:
      /*
       * Do a hard reset of the SCSI bus. According to the
       * SCSI-2 draft specification, reset has to be asserted
       * for at least 25us. I'm invoking the kernel delay
       * function for 30us since I'm not totally trusting of
       * the busy loop timing.
       *
       * XXX - I'm not convinced this works. I tried resetting
       *       the bus before, trying to get the devices on the
       *       bus to revert to asynchronous transfer, and it
       *       never seemed to work.
       */
      debug("aic7xxx: attempting to reset scsi bus and card\n");

      outb(SCSIRSTO, SCSISEQ(p->base));
      udelay(1000);
      outb(0, SCSISEQ(p->base));
      aic7xxx_delay(AIC7XXX_RESET_DELAY);

      UNPAUSE_SEQUENCER(p);

      /*
       * Locate the command and return a "reset" status
       * for it. This is not completely correct and will
       * probably return to haunt me later.
       */
      cmd->result = DID_RESET << 16;  /* return reset code */
      cmd->scsi_done(cmd);
      break;

    default:
      panic("aic7xxx_reset: internal error\n");
  }

  restore_flags(flags);
  return(SCSI_RESET_SUCCESS);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_biosparam
 *
 * Description:
 *   Return the disk geometry for the given SCSI device.
 *-F*************************************************************************/
int
aic7xxx_biosparam(Disk *disk, int devno, int geom[])
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

  if (p->extended && cylinders > 1024)
  {
    heads = 255;
    sectors = 63;
    cylinders = disk->capacity / (255 * 63);
  }

  geom[0] = heads;
  geom[1] = sectors;
  geom[2] = cylinders;

  return(0);
}

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = AIC7XXX;

#include "scsi_module.c"
#endif

