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
 * --------------------------------------------------------------------------
 *
 *  Modifications by Daniel M. Eischen (deischen@iworks.InterWorks.org):
 *
 *  Substantially modified to include support for wide and twin bus
 *  adapters, DMAing of SCBs, tagged queueing, IRQ sharing, bug fixes,
 *  SCB paging, and other rework of the code.
 *
 *  Parts of this driver were also based on the FreeBSD driver by
 *  Justin T. Gibbs.  His copyright follows:
 *
 * --------------------------------------------------------------------------
 * Copyright (c) 1994-1997 Justin Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      $Id: aic7xxx.c,v 1.119 1997/06/27 19:39:18 gibbs Exp $
 *---------------------------------------------------------------------------
 *
 *  Thanks also go to (in alphabetical order) the following:
 *
 *    Rory Bolt     - Sequencer bug fixes
 *    Jay Estabrook - Initial DEC Alpha support
 *    Doug Ledford  - Much needed abort/reset bug fixes
 *    Kai Makisara  - DMAing of SCBs
 *
 *  A Boot time option was also added for not resetting the scsi bus.
 *
 *    Form:  aic7xxx=extended
 *           aic7xxx=no_reset
 *           aic7xxx=ultra
 *           aic7xxx=irq_trigger:[0,1]  # 0 edge, 1 level
 *           aic7xxx=verbose
 *
 *  Daniel M. Eischen, deischen@iworks.InterWorks.org, 1/23/97
 *
 *  $Id: aic7xxx.c,v 4.1 1997/06/12 08:23:42 deang Exp $
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
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "aic7xxx.h"

#include "aic7xxx/sequencer.h"
#include "aic7xxx/scsi_message.h"
#include "aic7xxx_reg.h"
#include "aic7xxx_seq.h"
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
    S_IFDIR | S_IRUGO | S_IXUGO, 2,
    0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

#define AIC7XXX_C_VERSION  "$Revision: 4.1 $"

#define NUMBER(arr)     (sizeof(arr) / sizeof(arr[0]))
#define MIN(a,b)        (((a) < (b)) ? (a) : (b))
#define MAX(a,b)        (((a) > (b)) ? (a) : (b))
#define ALL_TARGETS -1
#define ALL_CHANNELS '\0'
#define ALL_LUNS -1
#define MAX_TARGETS  16
#define MAX_LUNS     8
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
 *   o Twin bus support - this has been tested and does work.  It is
 *     not an option anymore.
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
 *   o 3985 support - The 3985 adapter is much like the 3940, but has
 *     three 7870 controllers as opposed to two for the 3940.  It will
 *     be probed and recognized as three different adapters, but all
 *     three controllers can share the same external bank of 255 SCBs.
 *     If you enable AIC7XXX_USE_EXT_SCBRAM, then the driver will attempt
 *     to use and share the common bank of SCBs between the three
 *     controllers of the 3985.  This is experimental and hasn't been
 *     been tested.  By default, we do not use external SCB RAM, and
 *     force the controllers to use their own internal bank of 16 SCBs.
 *     Please let us know if using the external SCB array works.
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
 *  Daniel M. Eischen, deischen@iworks.InterWorks.org, 01/26/96
 */

/* Uncomment this for tagged queueing. */
#ifdef CONFIG_AIC7XXX_TAGGED_QUEUEING
#define AIC7XXX_TAGGED_QUEUEING
#endif

/*
 * You can try raising me if tagged queueing is enabled, or lowering
 * me if you only have 4 SCBs.
 */
#ifdef CONFIG_AIC7XXX_CMDS_PER_LUN
#define AIC7XXX_CMDS_PER_LUN CONFIG_AIC7XXX_CMDS_PER_LUN
#endif

/* Set this to the delay in seconds after SCSI bus reset. */
#ifdef CONFIG_AIC7XXX_RESET_DELAY
#define AIC7XXX_RESET_DELAY CONFIG_AIC7XXX_RESET_DELAY
#else
#define AIC7XXX_RESET_DELAY 15
#endif

/*
 * Control collection of SCSI transfer statistics for the /proc filesystem.
 *
 * NOTE: Do NOT enable this when running on kernels version 1.2.x and below.
 * NOTE: This does affect performance since it has to maintain statistics.
 */
#ifdef CONFIG_AIC7XXX_PROC_STATS
#define AIC7XXX_PROC_STATS
#endif

/*
 * Enable SCB paging.
 */
#ifdef CONFIG_AIC7XXX_PAGE_ENABLE
#define AIC7XXX_PAGE_ENABLE
#endif

/*
 * Uncomment the following to enable use of the external bank
 * of 255 SCBs.  For 3985 adapters, this will also enable sharing
 * of the SCB array across all three controllers.
 */
#ifdef CONFIG_AIC7XXX_USE_EXT_SCBRAM
#define AIC7XXX_USE_EXT_SCBRAM
#endif

/*
 * For debugging the abort/reset code.
 */
#define AIC7XXX_DEBUG_ABORT

/*
 * For general debug messages
 */
#define AIC7XXX_DEBUG

/*
 * Set this for defining the number of tagged commands on a device
 * by device, and controller by controller basis.  The first set
 * of tagged commands will be used for the first detected aic7xxx
 * controller, the second set will be used for the second detected
 * aic7xxx controller, and so on.  These values will *only* be used
 * for targets that are tagged queueing capable; these values will
 * be ignored in all other cases.  The tag_commands is an array of
 * 16 to allow for wide and twin adapters.  Twin adapters will use
 * indexes 0-7 for channel 0, and indexes 8-15 for channel 1.
 *
 * *** Determining commands per LUN ***
 * 
 * When AIC7XXX_CMDS_PER_LUN is not defined, the driver will use its
 * own algorithm to determine the commands/LUN.  If SCB paging is
 * enabled, the commands/LUN is 8.  When SCB paging is not enabled,
 * then commands/LUN is 8 for adapters with 16 or more hardware SCBs
 * and 4 commands/LUN for adapters with 3 or 4 SCBs.
 *
 */
/* #define AIC7XXX_TAGGED_QUEUEING_BY_DEVICE */

#ifdef AIC7XXX_TAGGED_QUEUEING_BY_DEVICE
typedef struct
{
  unsigned char tag_commands[16];   /* Allow for wide/twin channel adapters. */
} adapter_tag_info_t;

/*
 * Make a define that will tell the driver to use it's own algorithm
 * for determining commands/LUN (see Determining commands per LUN
 * above).
 */
#define DEFAULT_TAG_COMMANDS {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

/*
 * Modify this as you see fit for your system.  By setting tag_commands
 * to 0, the driver will use it's own algorithm for determining the
 * number of commands to use (see above).  When -1, the driver will
 * not enable tagged queueing for that particular device.  When positive
 * (> 0) the values in the array are used for the queue_depth.  Note
 * that the maximum value for an entry is 127.
 *
 * In this example, the first line will enable tagged queueing for all
 * the devices on the first probed aic7xxx adapter and tells the driver
 * to use it's own algorithm for determining commands/LUN.
 *
 * The second line enables tagged queueing with 4 commands/LUN for IDs
 * (1, 2-11, 13-15), disables tagged queueing for ID 12, and tells the
 * driver to use its own algorithm for ID 1.
 *
 * The third line is the same as the first line.
 *
 * The fourth line disables tagged queueing for devices 0 and 3.  It
 * enables tagged queueing for the other IDs, with 16 commands/LUN
 * for IDs 1 and 4, 127 commands/LUN for ID 8, and 4 commands/LUN for
 * IDs 2, 5-7, and 9-15.
 */
adapter_tag_info_t aic7xxx_tag_info[] =
{
  {DEFAULT_TAG_COMMANDS},
  {{4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, -1, 4, 4, 4}},
  {DEFAULT_TAG_COMMANDS},
  {{-1, 16, 4, -1, 16, 4, 4, 4, 127, 4, 4, 4, 4, 4, 4, 4}}
};
#endif

/*
 * Don't define this unless you have problems with the driver
 * interrupt handler.  The old method would register the drivers
 * interrupt handler as a "fast" type interrupt handler that would
 * lock out other interrupts.  Since this driver can spend a lot
 * of time in the interrupt handler, this is _not_ a good idea.
 * It also conflicts with some of the more common ethernet drivers
 * that don't use fast interrupts.  Currently, Linux does not allow
 * IRQ sharing unless both drivers can agree on the type of interrupt
 * handler.
 */
/* #define AIC7XXX_OLD_ISR_TYPE */


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
} aha_chip_type;

typedef enum {
  AIC_777x,	/* AIC-7770 based */
  AIC_785x,	/* AIC-7850 based (3 SCBs)*/
  AIC_786x,	/* AIC-7860 based (7850 ultra) */
  AIC_787x,	/* AIC-7870 based */
  AIC_788x	/* AIC-7880 based (ultra) */
} aha_chip_class_type;

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
  "AIC-7xxx Unknown",		                        /* AIC_NONE */
  "Adaptec AIC-7770 SCSI host adapter",			/* AIC_7770 */
  "Adaptec AHA-274X SCSI host adapter",			/* AIC_7771 */
  "Adaptec AHA-284X SCSI host adapter",			/* AIC_284x */
  "Adaptec AIC-7850 SCSI host adapter",			/* AIC_7850 */
  "Adaptec AIC-7855 SCSI host adapter",			/* AIC_7855 */
  "Adaptec AIC-7860 Ultra SCSI host adapter",		/* AIC_7860 */
  "Adaptec AHA-2940A Ultra SCSI host adapter",		/* AIC_7861 */
  "Adaptec AIC-7870 SCSI host adapter",			/* AIC_7870 */
  "Adaptec AHA-294X SCSI host adapter",			/* AIC_7871 */
  "Adaptec AHA-394X SCSI host adapter",			/* AIC_7872 */
  "Adaptec AHA-398X SCSI host adapter",			/* AIC_7873 */
  "Adaptec AHA-2944 SCSI host adapter",	                /* AIC_7874 */
  "Adaptec AIC-7880 Ultra SCSI host adapter",		/* AIC_7880 */
  "Adaptec AHA-294X Ultra SCSI host adapter",		/* AIC_7881 */
  "Adaptec AHA-394X Ultra SCSI host adapter",		/* AIC_7882 */
  "Adaptec AHA-398X Ultra SCSI host adapter",		/* AIC_7883 */
  "Adaptec AHA-2944 Ultra SCSI host adapter"	        /* AIC_7884 */
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

#define HSCSIID        0x07
#define HWSCSIID       0x0F
#define SCSI_RESET     0x040

/*
 * EISA/VL-bus stuff
 */
#define MINSLOT		1
#define MAXSLOT		15
#define SLOTBASE(x)	((x) << 12)
#define BASE_TO_SLOT(x) ((x) >> 12)

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
typedef enum {C46 = 6, C56_66 = 8} seeprom_chip_type;

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
#define CFAUTOTERM      0x0001          /* Perform Auto termination */
#define CFULTRAEN       0x0002          /* Ultra SCSI speed enable (Ultra cards) */
#define CF284XSELTO     0x0003          /* Selection timeout (284x cards) */
#define CF284XFIFO      0x000C          /* FIFO Threshold (284x cards) */
#define CFSTERM         0x0004          /* SCSI low byte termination */
#define CFWSTERM        0x0008          /* SCSI high byte termination (wide card) */
#define CFSPARITY	0x0010		/* SCSI parity */
#define CF284XSTERM	0x0020		/* SCSI low byte termination (284x cards) */
#define CFRESETB	0x0040		/* reset SCSI bus at boot */
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

#define SELBUS_MASK		0x0a
#define 	SELNARROW	0x00
#define 	SELBUSB		0x08
#define SINGLE_BUS		0x00

#define SCB_TARGET(scb)         \
       (((scb)->hscb->target_channel_lun & TID) >> 4)
#define SCB_LUN(scb)            \
       ((scb)->hscb->target_channel_lun & LID)
#define SCB_IS_SCSIBUS_B(scb)   \
       (((scb)->hscb->target_channel_lun & SELBUSB) != 0)

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
#define	AIC7XXX_MAX_SG 27

/*
 * The maximum number of SCBs we could have for ANY type
 * of card. DON'T FORGET TO CHANGE THE SCB MASK IN THE
 * SEQUENCER CODE IF THIS IS MODIFIED!
 */
#define AIC7XXX_MAXSCB	255


struct aic7xxx_hwscb {
/* ------------    Begin hardware supported fields    ---------------- */
/* 0*/  unsigned char control;
/* 1*/  unsigned char target_channel_lun;       /* 4/1/3 bits */
/* 2*/  unsigned char target_status;
/* 3*/  unsigned char SG_segment_count;
/* 4*/  unsigned int  SG_list_pointer;
/* 8*/  unsigned char residual_SG_segment_count;
/* 9*/  unsigned char residual_data_count[3];
/*12*/  unsigned int  data_pointer;
/*16*/  unsigned int  data_count;
/*20*/  unsigned int  SCSI_cmd_pointer;
/*24*/  unsigned char SCSI_cmd_length;
/*25*/	u_char tag;			/* Index into our kernel SCB array.
					 * Also used as the tag for tagged I/O
					 */
#define SCB_PIO_TRANSFER_SIZE	26 	/* amount we need to upload/download
					 * via PIO to initialize a transaction.
					 */
/*26*/  unsigned char next;             /* Used to thread SCBs awaiting selection
                                         * or disconnected down in the sequencer.
                                         */
/*27*/  unsigned char prev;
/*28*/  unsigned int pad;               /*
                                         * Unused by the kernel, but we require
                                         * the padding so that the array of
                                         * hardware SCBs is alligned on 32 byte
                                         * boundaries so the sequencer can index
                                         */
};

typedef enum {
	SCB_FREE		= 0x0000,
	SCB_ACTIVE		= 0x0001,
	SCB_ABORTED		= 0x0002,
	SCB_DEVICE_RESET	= 0x0004,
	SCB_SENSE		= 0x0008,
	SCB_TIMEDOUT		= 0x0010,
	SCB_QUEUED_FOR_DONE	= 0x0020,
	SCB_RECOVERY_SCB	= 0x0040,
	SCB_WAITINGQ		= 0x0080,
	SCB_ASSIGNEDQ		= 0x0100,
	SCB_SENTORDEREDTAG	= 0x0200,
	SCB_MSGOUT_SDTR		= 0x0400,
	SCB_MSGOUT_WDTR		= 0x0800,
	SCB_ABORT		= 0x1000,
	SCB_QUEUED_ABORT	= 0x2000
} scb_flag_type;

struct aic7xxx_scb {
        struct aic7xxx_hwscb  *hscb;          /* corresponding hardware scb */
	Scsi_Cmnd             *cmd;	      /* Scsi_Cmnd for this scb */
        struct aic7xxx_scb    *q_next;        /* next scb in queue */
	scb_flag_type          flags;         /* current state of scb */
	struct hw_scatterlist *sg_list;       /* SG list in adapter format */
	unsigned char          sense_cmd[6];  /*
                                               * Allocate 6 characters for
                                               * sense command.
                                               */
        unsigned char          sg_count;
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
  struct aic7xxx_hwscb *hscbs;
  scb_queue_type free_scbs;        /*
                                    * SCBs assigned to free slot on
                                    * card (no paging required)
                                    */
  unsigned char  numscbs;          /* current number of scbs */
  unsigned char  maxhscbs;         /* hardware scbs */
  unsigned char  maxscbs;          /* max scbs including pageable scbs */
  struct aic7xxx_scb   *scb_array[AIC7XXX_MAXSCB];
  unsigned int   reserve[100];
} scb_data_type;

/*
 * Define a structure used for each host adapter, only one per IRQ.
 */
struct aic7xxx_host {
  struct Scsi_Host        *host;             /* pointer to scsi host */
  struct aic7xxx_host     *next;             /* pointer to next aic7xxx device */
  int                      host_no;          /* SCSI host number */
  int                      instance;         /* aic7xxx instance number */
  int                      scsi_id;          /* host adapter SCSI ID */
  int                      scsi_id_b;        /*   channel B for twin adapters */
  int                      irq;              /* IRQ for this adapter */
  unsigned long            base;             /* card base address */
  unsigned long            mbase;            /* I/O memory address */
  volatile unsigned char  *maddr;            /* memory mapped address */
#define A_SCANNED               0x0001
#define B_SCANNED               0x0002
#define EXTENDED_TRANSLATION    0x0004
#define FLAGS_CHANNEL_B_PRIMARY 0x0008
#define MULTI_CHANNEL           0x0010
#define ULTRA_ENABLED           0x0020
#define PAGE_ENABLED            0x0040
#define USE_DEFAULTS            0x0080
#define BIOS_ENABLED            0x0100
#define IN_ISR                  0x0200
#define IN_TIMEOUT              0x0400
#define SHARED_SCBDATA          0x0800
#define HAVE_SEEPROM            0x1000
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
  aha_chip_type            chip_type;        /* card type */
  aha_chip_class_type      chip_class;
  aha_bus_type             bus_type;         /* normal/twin/wide bus */
  unsigned char            chan_num;         /* for 39xx, channel number */
  unsigned char            unpause;          /* unpause value for HCNTRL */
  unsigned char            pause;            /* pause value for HCNTRL */
  unsigned char            qcntmask;
  unsigned char            qfullcount;
  unsigned char            cmdoutcnt;
  unsigned char            curqincnt;
  unsigned char            activescbs;       /* active scbs */
  scb_queue_type           waiting_scbs;     /*
                                              * SCBs waiting for space in
                                              * the QINFIFO.
                                              */
  scb_data_type           *scb_data;

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
    int  active_cmds;
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
  } stats[MAX_TARGETS][MAX_LUNS];            /* [(channel << 3)|target][lun] */
#endif /* AIC7XXX_PROC_STATS */
};

/*
 * Valid SCSIRATE values. (p. 3-17)
 * Provides a mapping of transfer periods in ns/4 to the proper value to
 * stick in the SCSIRATE reg to use that transfer rate.
 */
static struct {
  short period;
  /* Rates in Ultra mode have bit 8 of sxfr set */
#define		ULTRA_SXFR 0x100
  short rate;
  const char *english;
} aic7xxx_syncrates[] = {
  { 12,  0x100,  "20.0"  },
  { 15,  0x110,  "16.0"  },
  { 18,  0x120,  "13.4"  },
  { 25,  0x000,  "10.0"  },
  { 31,  0x010,   "8.0"  },
  { 37,  0x020,   "6.67" },
  { 43,  0x030,   "5.7"  },
  { 50,  0x040,   "5.0"  },
  { 56,  0x050,   "4.4"  },
  { 62,  0x060,   "4.0"  },
  { 68,  0x070,   "3.6"  }
};

static int num_aic7xxx_syncrates =
    sizeof(aic7xxx_syncrates) / sizeof(aic7xxx_syncrates[0]);

#ifdef CONFIG_PCI
static int number_of_3940s = 0;
static int number_of_3985s = 0;
#endif /* CONFIG_PCI */

#ifdef AIC7XXX_DEBUG

#if 0
static void
debug_scb(struct aic7xxx_scb *scb)
{
  struct aic7xxx_hwscb *hscb = scb->hscb;

  printk("scb:%p control:0x%x tcl:0x%x cmdlen:%d cmdpointer:0x%lx\n",
    scb,
    hscb->control,
    hscb->target_channel_lun,
    hscb->SCSI_cmd_length,
    le32_to_cpu(hscb->SCSI_cmd_pointer) );
  printk("        datlen:%d data:0x%lx segs:0x%x segp:0x%lx\n",
    le32_to_cpu(hscb->data_count),
    le32_to_cpu(hscb->data_pointer),
    hscb->SG_segment_count,
    le32_to_cpu(hscb->SG_list_pointer));
  printk("        sg_addr:%lx sg_len:%ld\n",
    le32_to_cpu(hscb->sg_list[0].address),
    le32_to_cpu(hscb->sg_list[0].length));
}
#endif

#else
#  define debug_scb(x)
#endif AIC7XXX_DEBUG

#define TCL_OF_SCB(scb) (((scb->hscb)->target_channel_lun >> 4) & 0xf),  \
                        (((scb->hscb)->target_channel_lun >> 3) & 0x01), \
                        ((scb->hscb)->target_channel_lun & 0x07)

#define TC_OF_SCB(scb) (((scb->hscb)->target_channel_lun >> 4) & 0xf),  \
                       (((scb->hscb)->target_channel_lun >> 3) & 0x01)

#define CHAN_TO_INT(chan) ((chan) == 'A' ? 0 : 1)

#define TARGET_INDEX(cmd)  ((cmd)->target | ((cmd)->channel << 3))

/*
 * XXX - these options apply unilaterally to _all_ 274x/284x/294x
 *       cards in the system.  This should be fixed.
 */
static unsigned int aic7xxx_extended = 0;    /* extended translation on? */
static unsigned int aic7xxx_no_reset = 0;    /* no resetting of SCSI bus */
static int aic7xxx_irq_trigger = -1;         /*
                                              * -1 use board setting
                                              *  0 use edge triggered
                                              *  1 use level triggered
                                              */
static int aic7xxx_enable_ultra = 0;         /* enable ultra SCSI speeds */
static int aic7xxx_verbose = 0;	             /* verbose messages */
static struct aic7xxx_host *first_aic7xxx = NULL; /* list of all our devices */


/****************************************************************************
 *
 * These functions are not used yet, but when we do memory mapped
 * IO, we'll use them then.
 *
 ***************************************************************************/

static inline unsigned char
aic_inb(struct aic7xxx_host *p, long port)
{
  if (p->maddr != NULL)
    return (p->maddr[port]);
  else
    return (inb(p->base + port));
}

static inline void
aic_outb(struct aic7xxx_host *p, unsigned char val, long port)
{
  if (p->maddr != NULL)
    p->maddr[port] = val;
  else
    outb(val, p->base + port);
}

static inline void
aic_outsb(struct aic7xxx_host *p, long port, unsigned char *valp, size_t size)
{
  if (p->maddr != NULL)
  {
#if defined(__alpha__) || defined(__sparc_v9__) || defined(__powerpc__)
    int i;

    for (i=0; i < size; i++)
    {
      p->maddr[port] = valp[i];
    }
#else
    __asm __volatile("
      cld;
    1:  lodsb;
      movb %%al,(%0);
      loop 1b"      :
              :
      "r" (p->maddr + port),
      "S" (valp), "c" (size)  :
      "%esi", "%ecx", "%eax");
#endif
  }
  else
  {
    outsb(p->base + port, valp, size);
  }
}

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
    { "verbose",     &aic7xxx_verbose },
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
 *   pause_sequencer
 *
 * Description:
 *   Pause the sequencer and wait for it to actually stop - this
 *   is important since the sequencer can disable pausing for critical
 *   sections.
 *-F*************************************************************************/
static inline void
pause_sequencer(struct aic7xxx_host *p)
{
  outb(p->pause, p->base + HCNTRL);
  while ((inb(p->base + HCNTRL) & PAUSE) == 0)
  {
    ;
  }
}

/*+F*************************************************************************
 * Function:
 *   unpause_sequencer
 *
 * Description:
 *   Unpause the sequencer. Unremarkable, yet done often enough to
 *   warrant an easy way to do it.
 *-F*************************************************************************/
static inline void
unpause_sequencer(struct aic7xxx_host *p, int unpause_always)
{
  if (unpause_always ||
      ((inb(p->base + INTSTAT) & (SCSIINT | SEQINT | BRKADRINT)) == 0))
  {
    outb(p->unpause, p->base + HCNTRL);
  }
}

/*+F*************************************************************************
 * Function:
 *   restart_sequencer
 *
 * Description:
 *   Restart the sequencer program from address zero.  This assumes
 *   that the sequencer is already paused.
 *-F*************************************************************************/
static inline void
restart_sequencer(struct aic7xxx_host *p)
{
  /* Set the sequencer address to 0. */
  outb(0, p->base + SEQADDR0);
  outb(0, p->base + SEQADDR1);

  /*
   * Reset and unpause the sequencer.  The reset is suppose to
   * start the sequencer running, but we do an unpause to make
   * sure.
   */
  outb(SEQRESET | FASTMODE, p->base + SEQCTL);

  unpause_sequencer(p, /*unpause_always*/ TRUE);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_next_patch
 *
 * Description:
 *   Find the next patch to download.
 *-F*************************************************************************/
static struct patch *
aic7xxx_next_patch(struct patch *cur_patch, int options, int instrptr)
{
  while (cur_patch != NULL)
  {
    if ((((cur_patch->options & options) != 0) && (cur_patch->negative == FALSE))
      || (((cur_patch->options & options) == 0) && (cur_patch->negative == TRUE))
      || (instrptr >= cur_patch->end))
    {
      /*
       * Either we want to keep this section of code, or we have consumed
       * this patch.  Skip to the next patch.
       */
      cur_patch++;
      if (cur_patch->options == 0)
      {
        /* Out of patches. */
        cur_patch = NULL;
      }
    }
    else
    {
      /* Found an OK patch. */
      break;
    }
  }
  return (cur_patch);
}


/*+F*************************************************************************
 * Function:
 *   aic7xxx_download_instr
 *
 * Description:
 *   Find the next patch to download.
 *-F*************************************************************************/
static void
aic7xxx_download_instr(struct aic7xxx_host *p, int options, int instrptr)
{
  unsigned char opcode;
  struct ins_format3 *instr;

  instr = (struct ins_format3 *) &seqprog[instrptr * 4];
  /* Pull the opcode */
  opcode = instr->opcode_addr >> 1;
  switch (opcode)
  {
    case AIC_OP_JMP:
    case AIC_OP_JC:
    case AIC_OP_JNC:
    case AIC_OP_CALL:
    case AIC_OP_JNE:
    case AIC_OP_JNZ:
    case AIC_OP_JE:
    case AIC_OP_JZ:
    {
      int address_offset;
      struct ins_format3 new_instr;
      unsigned int address;
      struct patch *patch;
      int i;

      address_offset = 0;
      new_instr = *instr;  /* Strucure copy */
      address = new_instr.address;
      address |= (new_instr.opcode_addr & ADDR_HIGH_BIT) << 8;
      for (i = 0; i < NUMBER(patches); i++)
      {
        patch = &patches[i];
        if ((((patch->options & options) == 0) && (patch->negative == FALSE)) ||
            (((patch->options & options) != 0) && (patch->negative == TRUE)))
        {
          if (address >= patch->end)
          {
            address_offset += patch->end - patch->begin;
          }
        }
      }
      address -= address_offset;
      new_instr.address = address &0xFF;
      new_instr.opcode_addr &= ~ADDR_HIGH_BIT;
      new_instr.opcode_addr |= (address >> 8) & ADDR_HIGH_BIT;
      outsb(p->base + SEQRAM, &new_instr.immediate, 4);
      break;
    }

    case AIC_OP_OR:
    case AIC_OP_AND:
    case AIC_OP_XOR:
    case AIC_OP_ADD:
    case AIC_OP_ADC:
    case AIC_OP_ROL:
      outsb(p->base + SEQRAM, &instr->immediate, 4);
      break;

    default:
      panic("aic7xxx: Unknown opcode encountered in sequencer program.");
      break;
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
aic7xxx_loadseq(struct aic7xxx_host *p)
{
  int options;
  struct patch *cur_patch;
  int i;
  int downloaded;

  if (aic7xxx_verbose)
  {
    printk(KERN_INFO "aic7xxx: Downloading sequencer code...");
  }
  options = 1;  /* Code for all options. */
  downloaded = 0;
  if ((p->flags & ULTRA_ENABLED) != 0)
    options |= ULTRA;
  if (p->bus_type == AIC_TWIN)
    options |= TWIN_CHANNEL;
  if (p->scb_data->maxscbs > p->scb_data->maxhscbs)
    options |= SCB_PAGING;

  cur_patch = patches;
  outb(PERRORDIS | LOADRAM, p->base + SEQCTL);
  outb(0, p->base + SEQADDR0);
  outb(0, p->base + SEQADDR1);

  for (i = 0; i < sizeof(seqprog) / 4;  i++)
  {
    cur_patch = aic7xxx_next_patch(cur_patch, options, i);
    if (cur_patch && (cur_patch->begin <= i) && (cur_patch->end > i))
    {
      /* Skip this instruction for this configuration. */
      continue;
    }
    aic7xxx_download_instr(p, options, i);
    downloaded++;
  }

  outb(FASTMODE, p->base + SEQCTL);
  outb(0, p->base + SEQADDR0);
  outb(0, p->base + SEQADDR1);

  if (aic7xxx_verbose)
  {
     printk(" %d instructions downloaded\n", downloaded);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_delay
 *
 * Description:
 *   Delay for specified amount of time.  We use udelay because the timer
 *   interrupt is not guaranteed to be enabled.  This will cause an
 *   infinite loop since jiffies (clock ticks) is not updated.
 *-F*************************************************************************/
static void
aic7xxx_delay(int seconds)
{
  int i;

  /*                        
   * Call udelay() for 1 millisecond inside a loop for  
   * the requested amount of seconds.
   */
  for (i=0; i < seconds*1000; i++)
  {
    udelay(1000);  /* Delay for 1 millisecond. */
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
#if 0
  strcat(buffer, "/");
  strcat(buffer, rcs_version(AIC7XXX_SEQ_VER));
#endif

  return buffer;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_length
 *
 * Description:
 *   How much data should be transferred for this SCSI command?  Assume
 *   all segments are to be transferred except for the last sg_last
 *   segments.  This will allow us to compute underflow easily.  To
 *   calculate the total length of the command, use sg_last = 0.  To
 *   calculate the length of all but the last 2 SG segments, use
 *   sg_last = 2.
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
    for (i = length = 0; i < segments; i++)
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
    unsigned char *period, unsigned char *offset, int target, char channel)
{
  int i = num_aic7xxx_syncrates;
  unsigned long ultra_enb_addr;
  unsigned char ultra_enb, sxfrctl0;

  /*
   * If the offset is 0, then the device is requesting asynchronous
   * transfers.
   */
  if ((*period <= aic7xxx_syncrates[i - 1].period) && *offset != 0)
  {
    for (i = 0; i < num_aic7xxx_syncrates; i++)
    {
      if (*period <= aic7xxx_syncrates[i].period)
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
        *scsirate = (aic7xxx_syncrates[i].rate & 0xF0) | (*offset & 0x0F);
        *period = aic7xxx_syncrates[i].period;

        if (aic7xxx_verbose)
        {
          printk("scsi%d: Target %d, channel %c, now synchronous at %sMHz, "
                 "offset %d.\n", p->host_no, target, channel,
                 aic7xxx_syncrates[i].english, *offset);
        }
        break;
      }
    }
  }

  if (i >= num_aic7xxx_syncrates)
  {
    /*
     * Use asynchronous transfers.
     */
    *scsirate = 0;
    *period = 0;
    *offset = 0;
    if (aic7xxx_verbose)
    {
      printk("scsi%d: Target %d, channel %c, using asynchronous transfers.\n",
             p->host_no, target, channel);
    }
  }

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
  if ((*scsirate != 0) && (aic7xxx_syncrates[i].rate & ULTRA_SXFR))
  {
    ultra_enb |= 0x01 << (target & 0x07);
    sxfrctl0 |= FAST20;
  }
  else
  {
    ultra_enb &= ~(0x01 << (target & 0x07));
    sxfrctl0 &= ~FAST20;
  }
  outb(ultra_enb, p->base + ultra_enb_addr);
  outb(sxfrctl0, p->base + SXFRCTL0);
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
 *   scbq_remove
 *
 * Description:
 *   Removes an SCB from the list.
 *
 *-F*************************************************************************/
static inline void
scbq_remove(scb_queue_type *queue, struct aic7xxx_scb *scb)
{
  if (queue->head == scb)
  {
    /* At beginning of queue, remove from head. */
    scbq_remove_head(queue);
  }
  else
  {
    struct aic7xxx_scb *curscb = queue->head;

    /*
     * Search until the next scb is the one we're looking for, or
     * we run out of queue.
     */
    while ((curscb != NULL) && (curscb->q_next != scb))
    {
      curscb = curscb->q_next;
    }
    if (curscb != NULL)
    {
      /* Found it. */
      curscb->q_next = scb->q_next;
      if (scb->q_next == NULL)
      {
        /* Update the tail when removing the tail. */
        queue->tail = curscb;
      }
    }
  }
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
aic7xxx_match_scb(struct aic7xxx_scb *scb, int target, char channel,
    int lun, unsigned char tag)
{
  int targ = (scb->hscb->target_channel_lun >> 4) & 0x0F;
  char chan = (scb->hscb->target_channel_lun & SELBUSB) ? 'B' : 'A';
  int slun = scb->hscb->target_channel_lun & 0x07;
  int match;

#ifdef AIC7XXX_DEBUG_ABORT
  printk("scsi%d: (targ %d/chan %c) matching scb to (targ %d/chan %c)\n",
         scb->cmd->device->host->host_no, target, channel, targ, chan);
#endif
  match = ((chan == channel) || (channel == ALL_CHANNELS));
  if (match != 0)
    match = ((targ == target) || (target == ALL_TARGETS));
  if (match != 0)
    match = ((lun == slun) || (lun == ALL_LUNS));
  if (match != 0)
    match = ((tag == scb->hscb->tag) || (tag == SCB_LIST_NULL));

  return (match);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_add_curscb_to_free_list
 *
 * Description:
 *   Adds the current scb (in SCBPTR) to the list of free SCBs.
 *-F*************************************************************************/
static void
aic7xxx_add_curscb_to_free_list(struct aic7xxx_host *p)
{
  /*
   * Invalidate the tag so that aic7xxx_find_scb doesn't think
   * it's active
   */
  outb(SCB_LIST_NULL, p->base + SCB_TAG);

  outb(inb(p->base + FREE_SCBH), p->base + SCB_NEXT);
  outb(inb(p->base + SCBPTR), p->base + FREE_SCBH);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_rem_scb_from_disc_list
 *
 * Description:
 *   Removes the current SCB from the disconnected list and adds it
 *   to the free list.
 *-F*************************************************************************/
static unsigned char
aic7xxx_rem_scb_from_disc_list(struct aic7xxx_host *p, unsigned char scbptr)
{
  unsigned char next;
  unsigned char prev;

  outb(scbptr, p->base + SCBPTR);
  next = inb(p->base + SCB_NEXT);
  prev = inb(p->base + SCB_PREV);

  outb(0, p->base + SCB_CONTROL);

  aic7xxx_add_curscb_to_free_list(p);

  if (prev != SCB_LIST_NULL)
  {
    outb(prev, p->base + SCBPTR);
    outb(next, p->base + SCB_NEXT);
  }
  else
  {
    outb(next, p->base + DISCONNECTED_SCBH);
  }

  if (next != SCB_LIST_NULL)
  {
    outb(next, p->base + SCBPTR);
    outb(prev, p->base + SCB_PREV);
  }
  return next;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_busy_target
 *
 * Description:
 *   Set the specified target busy.
 *-F*************************************************************************/
static void
aic7xxx_busy_target(struct aic7xxx_host *p, unsigned char target,
    char channel, unsigned char scbid)
{
  unsigned char active_scb;
  unsigned char info_scb;
  unsigned int  scb_offset;

  info_scb = target / 4;
  if (channel == 'B')
    info_scb = info_scb + 2;

  active_scb = inb(p->base + SCBPTR);
  outb(info_scb, p->base + SCBPTR);
  scb_offset = SCB_BUSYTARGETS + (target & 0x03);
  outb(scbid, p->base + scb_offset);
  outb(active_scb, p->base + SCBPTR);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_index_busy_target
 *
 * Description:
 *   Returns the index of the busy target, and optionally sets the
 *   target inactive.
 *-F*************************************************************************/
static unsigned char
aic7xxx_index_busy_target(struct aic7xxx_host *p, unsigned char target,
    char channel, int unbusy)
{
  unsigned char active_scb;
  unsigned char info_scb;
  unsigned char busy_scbid;
  unsigned int  scb_offset;

  info_scb = target / 4;
  if (channel == 'B')
    info_scb = info_scb + 2;

  active_scb = inb(p->base + SCBPTR);
  outb(info_scb, p->base + SCBPTR);
  scb_offset = SCB_BUSYTARGETS + (target & 0x03);
  busy_scbid = inb(p->base + scb_offset);
  if (unbusy)
  {
    outb(SCB_LIST_NULL, p->base + scb_offset);
  }
  outb(active_scb, p->base + SCBPTR);
  return (busy_scbid);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_find_scb
 *
 * Description:
 *   Look through the SCB array of the card and attempt to find the
 *   hardware SCB that corresponds to the passed in SCB.  Return
 *   SCB_LIST_NULL if unsuccessful.  This routine assumes that the
 *   card is already paused.
 *-F*************************************************************************/
static unsigned char
aic7xxx_find_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  unsigned char saved_scbptr;
  unsigned char curindex;

  saved_scbptr = inb(p->base + SCBPTR);
  curindex = 0;
  for (curindex = 0; curindex < p->scb_data->maxhscbs; curindex++)
  {
    outb(curindex, p->base + SCBPTR);
    if (inb(p->base + SCB_TAG) == scb->hscb->tag)
    {
      break;
    }
  }
  outb(saved_scbptr, p->base + SCBPTR);
  if (curindex >= p->scb_data->maxhscbs)
  {
    curindex = SCB_LIST_NULL;
  }

  return (curindex);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_allocate_scb
 *
 * Description:
 *   Get an SCB from the free list or by allocating a new one.
 *-F*************************************************************************/
static struct aic7xxx_scb *
aic7xxx_allocate_scb(struct aic7xxx_host *p)
{
  struct aic7xxx_scb   *scbp = NULL;
  struct aic7xxx_hwscb *hscbp = NULL;
#ifdef AGRESSIVE
  long processor_flags;

  save_flags(processor_flags);
  cli();
#endif

  scbp = p->scb_data->free_scbs.head;
  if (scbp != NULL)
  {
    scbq_remove_head(&p->scb_data->free_scbs);
  }
  else
  {
    if (p->scb_data->numscbs < p->scb_data->maxscbs)
    {
      int scb_index = p->scb_data->numscbs;
      int scb_size = sizeof(struct aic7xxx_scb) +
                     sizeof (struct hw_scatterlist) * AIC7XXX_MAX_SG;

      scbp = kmalloc(scb_size, GFP_ATOMIC);
      if (scbp != NULL)
      {
        memset(scbp, 0, sizeof(struct aic7xxx_scb));
        hscbp = &p->scb_data->hscbs[scb_index];
        scbp->hscb = hscbp;
        scbp->sg_list = (struct hw_scatterlist *) &scbp[1];
        memset(hscbp, 0, sizeof(struct aic7xxx_hwscb));
        hscbp->tag = scb_index;
        p->scb_data->numscbs++;
        /*
         * Place in the scb array; never is removed
         */
        p->scb_data->scb_array[scb_index] = scbp;
      }
    }
  }
#ifdef AIC7XXX_DEBUG
  if (scbp != NULL)
  {
    p->activescbs++;
  }
#endif

#ifdef AGRESSIVE
  restore_flags(processor_flags);
#endif
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
    p->device_status[TARGET_INDEX(cmd)].active_cmds--;
    cmd->scsi_done(cmd);
  }
  p->completeq.tail = NULL;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_free_scb
 *
 * Description:
 *   Free the scb and insert into the free scb list.
 *-F*************************************************************************/
static void
aic7xxx_free_scb(struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  struct aic7xxx_hwscb *hscb;
  long flags;

  hscb = scb->hscb;
  save_flags(flags);
  cli();

  scb->flags = SCB_FREE;
  scb->cmd = NULL;
  hscb->control = 0;
  hscb->target_status = 0;

  scbq_insert_head(&p->scb_data->free_scbs, scb);
#ifdef AIC7XXX_DEBUG
  p->activescbs--;  /* For debugging purposes. */
#endif

  restore_flags(flags);
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

  if (scb->flags & SCB_RECOVERY_SCB)
  {
    p->flags &= ~IN_TIMEOUT;
  }
  if (cmd->result == DID_OK)
  {
    if (scb->flags & SCB_ABORTED)
    {
      cmd->result = (DID_RESET << 16);
    }
  }
  if ((scb->flags & (SCB_MSGOUT_WDTR | SCB_MSGOUT_SDTR)) != 0)
  {
    unsigned short mask;
    int message_error = FALSE;

    mask = 0x01 << TARGET_INDEX(scb->cmd);

    /*
     * Check to see if we get an invalid message or a message error
     * after failing to negotiate a wide or sync transfer message.
     */
    if ((scb->flags & SCB_SENSE) && 
          ((scb->cmd->sense_buffer[12] == 0x43) ||  /* INVALID_MESSAGE */
          (scb->cmd->sense_buffer[12] == 0x49))) /* MESSAGE_ERROR  */
    {
      message_error = TRUE;
    }

    if (scb->flags & SCB_MSGOUT_WDTR)
    {
      p->wdtr_pending &= ~mask;
      if (message_error)
      {
        p->needwdtr &= ~mask;
        p->needwdtr_copy &= ~mask;
      }
    }
    if (scb->flags & SCB_MSGOUT_SDTR)
    {
      p->sdtr_pending &= ~mask;
      if (message_error)
      {
        p->needsdtr &= ~mask;
        p->needsdtr_copy &= ~mask;
      }
    }
  }
  aic7xxx_free_scb(p, scb);
  aic7xxx_queue_cmd_complete(p, cmd);

#ifdef AIC7XXX_PROC_STATS
  if ( (cmd->cmnd[0] != TEST_UNIT_READY) &&
       (cmd->cmnd[0] != INQUIRY) )
  {
    int actual;

    /*
     * XXX: we should actually know how much actually transferred
     * XXX: for each command, but apparently that's too difficult.
     */
    actual = aic7xxx_length(cmd, 0);
    if (!(scb->flags & (SCB_ABORTED | SCB_SENSE)) && (actual > 0)
        && (aic7xxx_error(cmd) == 0))
    {
      struct aic7xxx_xferstats *sp;
      long *ptr;
      int x;

      sp = &p->stats[TARGET_INDEX(cmd)][cmd->lun & 0x7];
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
  }
#endif /* AIC7XXX_PROC_STATS */
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_run_done_queue
 *
 * Description:
 *   Calls the aic7xxx_done() for the Scsi_Cmnd of each scb in the
 *   aborted list, and adds each scb to the free list.  If complete
 *   is TRUE, we also process the commands complete list.
 *-F*************************************************************************/
static void
aic7xxx_run_done_queue(struct aic7xxx_host *p, /*complete*/ int complete)
{
  struct aic7xxx_scb *scb;
  int i;

  for (i = 0; i < p->scb_data->numscbs; i++)
  {
    scb = p->scb_data->scb_array[i];
    if (scb->flags & SCB_QUEUED_FOR_DONE)
    {
#ifdef AIC7XXX_DEBUG_ABORT
      printk("(scsi%d:%d:%d) Aborting scb %d\n",
             p->host_no, TC_OF_SCB(scb), scb->hscb->tag);
#endif
      aic7xxx_done(p, scb);
    }
  }
  if (complete)
  {
    aic7xxx_done_cmds_complete(p);
  }
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
    unsigned char scbpos, unsigned char prev)
{
  unsigned char curscb, next;

  /*
   * Select the SCB we want to abort and pull the next pointer out of it.
   */
  curscb = inb(p->base + SCBPTR);
  outb(scbpos, p->base + SCBPTR);
  next = inb(p->base + SCB_NEXT);

  /*
   * Clear the necessary fields
   */
  outb(0, p->base + SCB_CONTROL);

  aic7xxx_add_curscb_to_free_list(p);

  /*
   * Update the waiting list
   */
  if (prev == SCB_LIST_NULL)
  {
    /*
     * First in the list
     */
    outb(next, p->base + WAITING_SCBH);
  }
  else
  {
    /*
     * Select the scb that pointed to us and update its next pointer.
     */
    outb(prev, p->base + SCBPTR);
    outb(next, p->base + SCB_NEXT);
  }
  /*
   * Point us back at the original scb position and inform the SCSI
   * system that the command has been aborted.
   */
  outb(curscb, p->base + SCBPTR);
  scb->flags |= SCB_ABORTED | SCB_QUEUED_FOR_DONE;
  scb->flags &= ~SCB_ACTIVE;
  scb->cmd->result = (DID_RESET << 16);

  return (next);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_search_qinfifo
 *
 * Description:
 *   Search the queue-in FIFO for matching SCBs and conditionally
 *   requeue.  Returns the number of matching SCBs.
 *-F*************************************************************************/
static int
aic7xxx_search_qinfifo(struct aic7xxx_host *p, int target, char channel,
    int lun, unsigned char tag, int flags, int requeue)
{
  unsigned char saved_queue[AIC7XXX_MAXSCB];
  int      queued = inb(p->base + QINCNT) & p->qcntmask;
  int      i;
  int      found;
  struct aic7xxx_scb *scbp;
  scb_queue_type removed_scbs;

  found = 0;
  scbq_init (&removed_scbs);
  for (i = 0; i < (queued - found); i++)
  {
    saved_queue[i] = inb(p->base + QINFIFO);
    scbp = p->scb_data->scb_array[saved_queue[i]];
    if (aic7xxx_match_scb(scbp, target, channel, lun, tag))
    {
       /*
        * We found an scb that needs to be removed.
        */
       if (requeue)
       {
         scbq_insert_head(&removed_scbs, scbp);
       }
       else
       {
         scbp->flags = flags;
         scbp->flags &= ~SCB_ACTIVE;
         /*
          * XXX - Don't know what error to use here.
          */
         aic7xxx_error(scbp->cmd) = DID_RESET;
       }
       i--;
       found++;
    }
  }
  /* Now put the saved scbs back. */
  for (queued = 0; queued < i; queued++)
    outb(saved_queue[queued], p->base + QINFIFO);

  if (requeue)
  {
    scbp = removed_scbs.head;
    while (scbp != NULL)
    {
      scbq_remove_head(&removed_scbs);
      /*
       * XXX - Shouldn't we be adding this to the free list?
       */
      scbq_insert_head(&p->waiting_scbs, scbp);
      scbp->flags |= SCB_WAITINGQ;
      scbp = removed_scbs.head;
    }
  }

  return (found);
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
aic7xxx_reset_device(struct aic7xxx_host *p, int target, char channel,
                     int lun, unsigned char tag)
{
  struct aic7xxx_scb *scbp;
  unsigned char active_scb;
  int i = 0;
  int found;

  /*
   * Restore this when we're done
   */
  active_scb = inb(p->base + SCBPTR);

#ifdef AIC7XXX_DEBUG_ABORT
  printk("(scsi%d:%d:%d) Reset device, active_scb %d\n",
         p->host_no, target, CHAN_TO_INT(channel), active_scb);
#endif

  /*
   * Deal with the busy target and linked next issues.
   */
  {
    int min_target, max_target;
    unsigned char busy_scbid;

    /* Make all targets 'relative' to bus A. */
    if (target == ALL_TARGETS)
    {
      switch (channel)
      {
        case 'A':
  	  min_target = 0;
  	  max_target = (p->bus_type == AIC_SINGLE) ? 7 : 15;
  	  break;
        case 'B':
  	  min_target = 8;
  	  max_target = 15;
  	  break;
        case ALL_CHANNELS:
        default:
  	  min_target = 0;
  	  max_target = (p->bus_type == AIC_SINGLE) ? 7 : 15;
  	  break;
      }
    }
    else
    { 
      min_target = target + channel == 'B' ? 8 : 0;
      max_target = min_target;
    }

    for (i = min_target; i <= max_target; i++)
    {
      busy_scbid = aic7xxx_index_busy_target(p, i, 'A', /*unbusy*/FALSE);
      if (busy_scbid < p->scb_data->numscbs)
      {
  	struct aic7xxx_scb *busy_scb;
  	struct aic7xxx_scb *next_scb;
  	unsigned char next_scbid;

  	busy_scb = p->scb_data->scb_array[busy_scbid];
  
  	next_scbid = le32_to_cpu(busy_scb->hscb->data_count) >> 24;

  	if (next_scbid == SCB_LIST_NULL)
        {
  	  busy_scbid = aic7xxx_find_scb(p, busy_scb);

  	  if (busy_scbid != SCB_LIST_NULL)
          {
  	    outb(busy_scbid, p->base + SCBPTR);
  	    next_scbid = inb(p->base + SCB_LINKED_NEXT);
  	  }
  	}

  	if (aic7xxx_match_scb(busy_scb, target, channel, lun, tag))
        {
  	  aic7xxx_index_busy_target(p, i, 'A', /*unbusy*/TRUE);
  	}

  	if (next_scbid != SCB_LIST_NULL)
        {
  	  next_scb = p->scb_data->scb_array[next_scbid];
  	  if (aic7xxx_match_scb(next_scb, target, channel, lun, tag))
          {
  	    continue;
          }
  	  /* Requeue for later processing */
  	  scbq_insert_head(&p->waiting_scbs, next_scb);
  	  next_scb->flags |= SCB_WAITINGQ;
  	}
      }
    }
  }

  found = aic7xxx_search_qinfifo(p, target, channel, lun, tag,
      SCB_ABORTED | SCB_QUEUED_FOR_DONE, /* requeue */ FALSE);

  /*
   * Search waiting for selection list.
   */
  {
    unsigned char next, prev, scb_index;

    next = inb(p->base + WAITING_SCBH);  /* Start at head of list. */
    prev = SCB_LIST_NULL;

    while (next != SCB_LIST_NULL)
    {
      outb(next, p->base + SCBPTR);
      scb_index = inb(p->base + SCB_TAG);
      if (scb_index >= p->scb_data->numscbs)
      {
        panic("aic7xxx: Waiting List inconsistency; SCB index=%d, numscbs=%d\n",
              scb_index, p->scb_data->numscbs);
      }
      scbp = p->scb_data->scb_array[scb_index];
      if (aic7xxx_match_scb(scbp, target, channel, lun, tag))
      {
        unsigned char linked_next;

        next = aic7xxx_abort_waiting_scb(p, scbp, next, prev);
        linked_next = inb(p->base + SCB_LINKED_NEXT);
        if (linked_next != SCB_LIST_NULL)
        {
          struct aic7xxx_scb *next_scb;
          /*
           * Requeue the waiting SCB via the waiting list.
           */
          next_scb = p->scb_data->scb_array[linked_next];
          if (! aic7xxx_match_scb(next_scb, target, channel, lun, tag))
          {
            scbq_insert_head(&p->waiting_scbs, next_scb);
            next_scb->flags |= SCB_WAITINGQ;
          }
        }
        found++;
      }
      else
      {
        prev = next;
        next = inb(p->base + SCB_NEXT);
      }
    }
  }

  /*
   * Go through disconnected list and remove any entries we have queued
   * for completion, zeroing their control byte too.
   */
  {
    unsigned char next, prev, scb_index;

    next = inb(p->base + DISCONNECTED_SCBH);
    prev = SCB_LIST_NULL;

    while (next != SCB_LIST_NULL)
    {
      outb(next, p->base + SCBPTR);
      scb_index = inb(p->base + SCB_TAG);
      if (scb_index > p->scb_data->numscbs)
      {
        panic("aic7xxx: Disconnected List inconsistency, SCB index = %d, "
              "num scbs = %d.\n", scb_index, p->scb_data->numscbs);
      }
      scbp = p->scb_data->scb_array[scb_index];
      if (aic7xxx_match_scb(scbp, target, channel, lun, tag))
      {
        next = aic7xxx_rem_scb_from_disc_list(p, next);
      }
      else
      {
        prev = next;
        next = inb(p->base + SCB_NEXT);
      }
    }
  }

  /*
   * Go through the hardware SCB array looking for commands that
   * were active but not on any list.
   */
  for (i = 0; i < p->scb_data->maxhscbs; i++)
  {
    unsigned char scbid;

    outb(i, p->base + SCBPTR);
    scbid = inb(p->base + SCB_TAG);
    if (scbid < p->scb_data->numscbs)
    {
      scbp = p->scb_data->scb_array[scbid];
      if (aic7xxx_match_scb(scbp, target, channel, lun, tag))
      {
        aic7xxx_add_curscb_to_free_list(p);
      }
    }
  }

  /*
   * Go through the entire SCB array now and look for commands for
   * for this target that are stillactive.  These are other (most likely
   * tagged) commands that were disconnected when the reset occurred.
   */
  for (i = 0; i < p->scb_data->numscbs; i++)
  {
    scbp = p->scb_data->scb_array[i];
    if (((scbp->flags & SCB_ACTIVE) != 0) &&
        aic7xxx_match_scb(scbp, target, channel, lun, tag))
    {
      scbp->flags |= SCB_ABORTED | SCB_QUEUED_FOR_DONE;
      scbp->flags &= ~SCB_ACTIVE;
      aic7xxx_error(scbp->cmd) = DID_RESET;

      found++;

      if ((scbp->flags & SCB_WAITINGQ) != 0)
      {
        scbq_remove(&p->waiting_scbs, scbp);
        scbp->flags &= ~SCB_WAITINGQ;
      }
    }
  }

  outb(active_scb, p->base + SCBPTR);
  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_clear_intstat
 *
 * Description:
 *   Clears the interrupt status.
 *-F*************************************************************************/
static void
aic7xxx_clear_intstat(struct aic7xxx_host *p)
{
  /* Clear any interrupt conditions this may have caused. */
  outb(CLRSELDO | CLRSELDI | CLRSELINGO, p->base + CLRSINT0);
  outb(CLRSELTIMEO | CLRATNO | CLRSCSIRSTI | CLRBUSFREE | CLRSCSIPERR |
       CLRPHASECHG | CLRREQINIT, p->base + CLRSINT1);
  outb(CLRSCSIINT, p->base + CLRINT);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_reset_current_bus
 *
 * Description:
 *   Reset the current SCSI bus.
 *-F*************************************************************************/
static void
aic7xxx_reset_current_bus(struct aic7xxx_host *p)
{
  unsigned long processor_flags;
  unsigned char scsiseq;

  save_flags(processor_flags);
  cli();

  /* Disable reset interrupts. */
  outb(inb(p->base + SIMODE1) & ~ENSCSIRST, p->base + SIMODE1);

  /* Turn on the bus reset. */
  scsiseq = inb(p->base + SCSISEQ);
  outb(scsiseq | SCSIRSTO, p->base + SCSISEQ);

  udelay(1000);

  /* Turn off the bus reset. */
  outb(scsiseq & ~SCSIRSTO, p->base + SCSISEQ);

  aic7xxx_clear_intstat(p);

  /* Re-enable reset interrupts. */
  outb(inb(p->base + SIMODE1) | ENSCSIRST, p->base + SIMODE1);

  udelay(1000);

  restore_flags(processor_flags);
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
  unsigned long offset, offset_max;
  int found;
  unsigned char sblkctl;
  char cur_channel;

  pause_sequencer(p);
  /*
   * Clean up all the state information for the pending transactions
   * on this bus.
   */
  found = aic7xxx_reset_device(p, ALL_TARGETS, channel, ALL_LUNS, SCB_LIST_NULL);

  if (channel == 'B')
  {
    p->needsdtr |= (p->needsdtr_copy & 0xFF00);
    p->sdtr_pending &= 0x00FF;
    offset = TARG_SCRATCH + 8;
    offset_max = TARG_SCRATCH + 16;
  }
  else
  {
    if (p->bus_type == AIC_WIDE)
    {
      p->needsdtr = p->needsdtr_copy;
      p->needwdtr = p->needwdtr_copy;
      p->sdtr_pending = 0x0;
      p->wdtr_pending = 0x0;
      offset = TARG_SCRATCH;
      offset_max = TARG_SCRATCH + 16;
    }
    else
    {
      /* Channel A */
      p->needsdtr |= (p->needsdtr_copy & 0x00FF);
      p->sdtr_pending &= 0xFF00;
      offset = TARG_SCRATCH;
      offset_max = TARG_SCRATCH + 8;
    }
  }

  while (offset < offset_max)
  {
    /*
     * Revert to async/narrow transfers until we renegotiate.
     */
    u_char targ_scratch;

    targ_scratch = inb(p->base + offset);
    targ_scratch &= SXFR;
    outb(targ_scratch, p->base + offset);
    offset++;
  }

  /*
   * Reset the bus and unpause/restart the controller
   */
  sblkctl = inb(p->base + SBLKCTL);
  cur_channel = (sblkctl & SELBUSB) ? 'B' : 'A';
  if (cur_channel != channel)
  {
    /*
     * Case 1: Command for another bus is active
     */
#ifdef AIC7XXX_DEBUG_ABORT
    printk("scsi%d: Stealthily resetting channel %c\n",
           p->host_no, channel);
#endif
    /*
     * Stealthily reset the other bus without upsetting the current bus.
     */
    outb(sblkctl ^ SELBUSB, p->base + SBLKCTL);
    outb(inb(p->base + SIMODE1) & ~ENBUSFREE, p->base + SIMODE1);
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(p);
      /*
       * Cause the mid-level SCSI code to delay any further 
       * queueing by the bus settle time for us.
       */
      p->host->last_reset = (jiffies + (AIC7XXX_RESET_DELAY * HZ));
    }
    outb(0, p->base + SCSISEQ);
    aic7xxx_clear_intstat(p);
    outb(sblkctl, p->base + SBLKCTL);
    unpause_sequencer(p, /* unpause_always */ FALSE);
  }
  else
  {
    /*
     * Case 2: A command from this bus is active or we're idle.
     */
#ifdef AIC7XXX_DEBUG_ABORT
    printk("scsi%d: Resetting current channel %c\n",
           p->host_no, channel);
#endif
    outb(inb(p->base + SIMODE1) & ~ENBUSFREE, p->base + SIMODE1);
    if (initiate_reset)
    {
      aic7xxx_reset_current_bus(p);
      /*
       * Cause the mid-level SCSI code to delay any further 
       * queueing by the bus settle time for us.
       */
#if 0
      p->host->last_reset = (jiffies + (AIC7XXX_RESET_DELAY * HZ));
#endif
    }
    outb(0, p->base + SCSISEQ);
    aic7xxx_clear_intstat(p);
    restart_sequencer(p);
#ifdef AIC7XXX_DEBUG_ABORT
    printk("scsi%d: Channel reset, sequencer restarted\n", p->host_no);
#endif
  }

  /*
   * Now loop through all the SCBs that have been marked for abortion,
   * and call the scsi_done routines.
   */
  aic7xxx_run_done_queue(p, /*complete*/ TRUE);
  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_run_waiting_queues
 *
 * Description:
 *   Scan the awaiting_scbs queue downloading and starting as many
 *   scbs as we can.
 *-F*************************************************************************/
static inline void
aic7xxx_run_waiting_queues(struct aic7xxx_host *p)
{
  struct aic7xxx_scb *scb;

  if (p->waiting_scbs.head == NULL)
    return;

  pause_sequencer(p);
  /*
   * First handle SCBs that are waiting but have been assigned a slot.
   */
  scb = p->waiting_scbs.head;
  while (scb != NULL)
  {
    if (p->curqincnt >= p->qfullcount)
    {
      p->curqincnt = inb(p->base + QINCNT) & p->qcntmask;
      if (p->curqincnt >= p->qfullcount)
      {
        break;
      }
    }

    /*
     * We have some space.
     */
    scbq_remove_head(&(p->waiting_scbs));
    scb->flags &= ~SCB_WAITINGQ;

    outb(scb->hscb->tag, p->base + QINFIFO);

    if ((p->flags & PAGE_ENABLED) != 0)
    {
      /*
       * We only care about this statistic when paging
       * since it's impossible to overflow the qinfifo
       * in the non-paging case.
       */
      p->curqincnt++;
    }
    scb = p->waiting_scbs.head;
  }

  unpause_sequencer(p, FALSE);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_construct_sdtr
 *
 * Description:
 *   Constucts a synchronous data transfer message in the message
 *   buffer on the sequencer.
 *-F*************************************************************************/
static void
aic7xxx_construct_sdtr(struct aic7xxx_host *p, int start_byte,
    unsigned char period, unsigned char offset)
{
  outb(MSG_EXTENDED,     p->base + MSG_OUT + start_byte);
  outb(MSG_EXT_SDTR_LEN, p->base + MSG_OUT + 1 + start_byte);
  outb(MSG_EXT_SDTR,     p->base + MSG_OUT + 2 + start_byte);
  outb(period,           p->base + MSG_OUT + 3 + start_byte);
  outb(offset,           p->base + MSG_OUT + 4 + start_byte);
  outb(start_byte + 5,   p->base + MSG_LEN);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_construct_wdtr
 *
 * Description:
 *   Constucts a wide data transfer message in the message buffer
 *   on the sequencer.
 *-F*************************************************************************/
static void
aic7xxx_construct_wdtr(struct aic7xxx_host *p, int start_byte,
    unsigned char bus_width)
{
  outb(MSG_EXTENDED,     p->base + MSG_OUT + start_byte);
  outb(MSG_EXT_WDTR_LEN, p->base + MSG_OUT + 1 + start_byte);
  outb(MSG_EXT_WDTR,     p->base + MSG_OUT + 2 + start_byte);
  outb(bus_width,        p->base + MSG_OUT + 3 + start_byte);
  outb(start_byte + 4,   p->base + MSG_LEN);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_calc_residual
 *
 * Description:
 *   Calculate the residual data not yet transferred.
 *-F*************************************************************************/
static void
aic7xxx_calculate_residual (struct aic7xxx_host *p, struct aic7xxx_scb *scb)
{
  struct aic7xxx_hwscb *hscb;
  Scsi_Cmnd *cmd;
  int actual;

  cmd = scb->cmd;
  hscb = scb->hscb;

  /*
   *  Don't destroy valid residual information with
   *  residual coming from a check sense operation.
   */
  if (((scb->hscb->control & DISCONNECTED) == 0) &&
      (scb->flags & SCB_SENSE) == 0)
  {
    /*
     *  We had an underflow. At this time, there's only
     *  one other driver that bothers to check for this,
     *  and cmd->underflow seems to be set rather half-
     *  heartedly in the higher-level SCSI code.
     */
    actual = aic7xxx_length(cmd, hscb->residual_SG_segment_count);

    actual -= (hscb->residual_data_count[2] << 16) |
              (hscb->residual_data_count[1] <<  8) |
              hscb->residual_data_count[0];

    if (actual < cmd->underflow)
    {
      printk(KERN_WARNING "(scsi%d:%d:%d) Underflow - "
             "Wanted at least %u, got %u, residual SG count %d.\n",
             p->host_no, TC_OF_SCB(scb), cmd->underflow, actual,
             hscb->residual_SG_segment_count);
      aic7xxx_error(cmd) = DID_RETRY_COMMAND;
      aic7xxx_status(cmd) = hscb->target_status;
    }
  }

  /*
   * Clean out the residual information in the SCB for the
   * next consumer.
   */
  hscb->residual_data_count[2] = 0;
  hscb->residual_data_count[1] = 0;
  hscb->residual_data_count[0] = 0;
  hscb->residual_SG_segment_count = 0;
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_device_reset
 *
 * Description:
 *   Interrupt handler for sequencer interrupts (SEQINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_device_reset(struct aic7xxx_host *p, int target, char channel)
{
  unsigned short targ_mask;
  unsigned char  targ_scratch;
  int scratch_offset = target;
  int found;

  if (channel == 'B')
  {
    scratch_offset += 8;
  }
  targ_mask = (0x01 << scratch_offset);
  /*
   * Go back to async/narrow transfers and renegotiate.
   */
  p->needsdtr |= p->needsdtr_copy & targ_mask;
  p->needwdtr |= p->needwdtr_copy & targ_mask;
  p->sdtr_pending &= ~targ_mask;
  p->wdtr_pending &= ~targ_mask;
  targ_scratch = inb(p->base + TARG_SCRATCH + scratch_offset);
  targ_scratch &= SXFR;
  outb(targ_scratch, p->base + TARG_SCRATCH + scratch_offset);
  found = aic7xxx_reset_device(p, target, channel, ALL_LUNS, SCB_LIST_NULL);
  printk(KERN_WARNING "(scsi%d:%d:%d) Bus Device Reset delivered, "
         "%d SCBs aborted.\n", p->host_no, target, CHAN_TO_INT(channel), found);
  aic7xxx_run_done_queue(p, /*complete*/ TRUE);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_seqint
 *
 * Description:
 *   Interrupt handler for sequencer interrupts (SEQINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_seqint(struct aic7xxx_host *p, unsigned char intstat)
{
  struct aic7xxx_scb *scb;
  unsigned short target_mask;
  unsigned char target, scratch_offset;
  char channel;

  if ((inb(p->base + SEQ_FLAGS) & RESELECTED) != 0)
  {
    target = (inb(p->base + SELID) >> 4) & 0x0F;
  }
  else
  {
    target = (inb(p->base + SCSIID) >> 4) & 0x0F;
  }
  scratch_offset = target;
  channel = 'A';
  if (inb(p->base + SBLKCTL) & SELBUSB)
  {
    channel = 'B';
    scratch_offset += 8;
  }
  target_mask = (0x01 << scratch_offset);

  switch (intstat & SEQINT_MASK)
  {
    case NO_MATCH:
      {
        /*
         * This could be for a normal abort request.  Figure out
         * which SCB we were trying to find and only give an error
         * if we didn't ask for this to happen.
         */
        unsigned char scb_index;
        unsigned char busy_scbid;
        unsigned char arg1;

        busy_scbid = aic7xxx_index_busy_target(p, target, channel,
            /*unbusy*/ FALSE);
        arg1 = inb(p->base + ARG_1);

        if (arg1 == SCB_LIST_NULL)
        {
          /* untagged request */
          scb_index = busy_scbid;
        }
        else
        {
          scb_index = arg1;
        }

        if (scb_index < p->scb_data->numscbs)
        {
          scb = p->scb_data->scb_array[scb_index];
          if (scb->hscb->control & ABORT_SCB)
          {
            /*
             * We expected this.  Let the busfree handler take care
             * of this when we the abort is finially sent.  Set
             * IDENTIFY_SEEN so that the busfree handler knows that
             * there is an SCB to cleanup.
             */
            outb(inb(p->base + SEQ_FLAGS) | IDENTIFY_SEEN, p->base + SEQ_FLAGS);
            printk(KERN_INFO "(scsi%d:%d:%d) reconnect SCB abort successful\n",
                   p->host_no, TC_OF_SCB(scb));
            break;
          }
        }
        printk(KERN_WARNING "(scsi%d:%d:%d) No active SCB for reconnecting "
               "target - Issuing BUS DEVICE RESET.\n",
               p->host_no, target, CHAN_TO_INT(channel));

        printk(KERN_WARNING "      SAVED_TCL=0x%x, ARG_1=0x%x, SEQADDR=0x%x\n",
               inb(p->base + SAVED_TCL), arg1,
               (inb(p->base + SEQADDR1) << 8) | inb(p->base + SEQADDR0));
        aic7xxx_handle_device_reset(p, target, channel);
      }
      break;

    case NO_MATCH_BUSY:
      {
        /*
         * XXX - Leave this as a panic for the time being since it
         * indicates a bug in the timeout code for this to happen.
         */
        unsigned char scb_index;

        scb_index = inb(p->base + CUR_SCBID);
        scb = p->scb_data->scb_array[scb_index];

        panic("scsi%d:  Target %d, channel %c, Target busy link failure, "
              "but busy SCB exists!\n",
              p->host_no, target, channel);
      }
      break;

    case SEND_REJECT:
      {
        unsigned char rej_byte;

        rej_byte = inb(p->base + REJBYTE);
        printk(KERN_WARNING "(scsi%d:%d:%d) Rejecting unknown message (0x%x) "
               "received from target, SEQ_FLAGS=0x%x\n",
               p->host_no, target, CHAN_TO_INT(channel), rej_byte,
               inb(p->base + SEQ_FLAGS));
      }
      break;

    case NO_IDENT:
      {
        /*
         * The reconnecting target either did not send an identify
         * message, or did, but we didn't find and SCB to match and
         * before it could respond to our ATN/abort, it hit a dataphase.
         * The only safe thing to do is to blow it away with a bus
         * reset.
         */
        int found;

        printk(KERN_WARNING "(scsi%d:%d:%d): Target did not send an IDENTIFY "
               "message; LASTPHASE 0x%x, SAVED_TCL 0x%x\n",
               p->host_no, target, CHAN_TO_INT(channel),
               inb(p->base + LASTPHASE), inb(p->base + SAVED_TCL));

        found = aic7xxx_reset_channel(p, channel, /*initiate reset*/ TRUE);

        printk(KERN_WARNING "scsi%d: Issued channel %c bus reset; "
     	       "%d SCBs aborted\n", p->host_no, channel, found);
      }
      break;

    case BAD_PHASE:
      if (inb(p->base + LASTPHASE) == P_BUSFREE)
      {
        printk(KERN_WARNING "(scsi%d:%d:%d): Missed busfree.\n",
               p->host_no, CHAN_TO_INT(channel), target);
        restart_sequencer(p);
      }
      else
      {
        printk(KERN_WARNING "(scsi%d:%d:%d): Unknown scsi bus phase, attempting "
               "to continue\n", p->host_no, CHAN_TO_INT(channel), target);
      }
      break;

    case EXTENDED_MSG:
      {
	unsigned char message_length;
	unsigned char message_code;
        unsigned char scb_index;

	message_length = inb(p->base + MSGIN_EXT_LEN);
	message_code = inb(p->base + MSGIN_EXT_OPCODE);
        scb_index = inb(p->base + SCB_TAG);
        scb = p->scb_data->scb_array[scb_index];

	switch (message_code)
	{
          case MSG_EXT_SDTR:
          {
            unsigned char period;
            unsigned char offset;
            unsigned char saved_offset;
            unsigned char targ_scratch;
            unsigned char max_offset;
            unsigned char rate;

            if (message_length != MSG_EXT_SDTR_LEN)
            {
              outb(SEND_REJ, p->base + RETURN_1);
              break;
            }

            period = inb(p->base + MSGIN_EXT_BYTES);
            saved_offset = inb(p->base + MSGIN_EXT_BYTES + 1);
            targ_scratch = inb(p->base + TARG_SCRATCH + scratch_offset);

            if (targ_scratch & WIDEXFER)
              max_offset = MAX_OFFSET_16BIT;
            else
              max_offset = MAX_OFFSET_8BIT;
            offset = MIN(saved_offset, max_offset);

            aic7xxx_scsirate(p, &rate, &period, &offset, target, channel);

            /*
             * Preserve the WideXfer flag.
             */
            targ_scratch = rate | (targ_scratch & WIDEXFER);

            /*
             * Update both the target scratch area and current SCSIRATE.
             */
            outb(targ_scratch, p->base + TARG_SCRATCH + scratch_offset);
            outb(targ_scratch, p->base + SCSIRATE);

            /*
             * See if we initiated Sync Negotiation and didn't have
             * have to fall down to async transfers.
             */
            if ((scb->flags & SCB_MSGOUT_SDTR) != 0)
            {
              /* We started it. */
              if (saved_offset == offset)
              {
        	/*
        	 * Don't send an SDTR back to the target.
        	 */
        	outb(0, p->base + RETURN_1);
              }
              else
              {
        	/* We went too low - force async. */
        	outb(SEND_REJ, p->base + RETURN_1);
              }
            }
            else
            {
              /*
               * Send our own SDTR in reply.
               *
               * We want to see this message as we don't expect a target
               * to send us a SDTR request first.
               */
              printk(KERN_WARNING "scsi%d: Sending SDTR!!\n", p->host_no);
              aic7xxx_construct_sdtr(p, /* start byte */ 0, period, offset);
              outb(SEND_MSG, p->base + RETURN_1);
            }
            /*
             * Clear the flags.
             */
            p->needsdtr &= ~target_mask;
            break;
          }

          case MSG_EXT_WDTR:
          {
            unsigned char scratch, bus_width;

            if (message_length != MSG_EXT_WDTR_LEN)
            {
              outb(SEND_REJ, p->base + RETURN_1);
              break;
            }

            bus_width = inb(p->base + MSGIN_EXT_BYTES);
            scratch = inb(p->base + TARG_SCRATCH + scratch_offset);

            if ((scb->flags & SCB_MSGOUT_WDTR) != 0)
            {
              /*
               * Don't send an WDTR back to the target, since we asked first.
               */
              outb(0, p->base + RETURN_1);
              switch (bus_width)
              {
        	case BUS_8_BIT:
        	  scratch &= 0x7F;
        	  break;

        	case BUS_16_BIT:
                  if (aic7xxx_verbose)
                  {
        	    printk(KERN_INFO "scsi%d: Target %d, channel %c, using 16 "
  	  	         "bit transfers.\n", p->host_no, target, channel);
                  }
        	  scratch |= WIDEXFER;
        	  break;

        	case BUS_32_BIT:
        	  outb(SEND_REJ, p->base + RETURN_1);
                  /* No verbose here!  We want to see this condition. */
        	  printk(KERN_WARNING "scsi%d: Target %d, channel %c, "
  			"requesting 32 bit transfers, rejecting...\n",
                	 p->host_no, target, channel);
        	  break;

        	default:
        	  break;
              }
            }
            else
            {
              int send_reject = FALSE;

              /*
               * Send our own WDTR in reply.
               */
              switch (bus_width)
              {
        	case BUS_8_BIT:
        	  scratch &= 0x7F;
        	  break;

        	case BUS_32_BIT:
        	case BUS_16_BIT:
        	  if (p->bus_type == AIC_WIDE)
        	  {
                    printk(KERN_INFO "scsi%d: Target %d, channel %c, using 16 "
  			   "bit transfers.\n", p->host_no, target, channel);
                    bus_width = BUS_16_BIT;
                    scratch |= WIDEXFER;
        	  }
        	  else
        	  {
                    bus_width = BUS_8_BIT;
                    scratch &= 0x7F;  /* XXX - FreeBSD doesn't do this. */
                    send_reject = TRUE;
        	  }
        	  break;

        	default:
        	  break;
              }
              if (send_reject)
              {
                outb(SEND_REJ, p->base + RETURN_1);
                printk(KERN_WARNING "scsi%d: Target %d, channel %c, initiating "
                       "wide negotiation on a narrow bus - rejecting!",
                       p->host_no, target, channel);
              }
              else
              {
                aic7xxx_construct_wdtr(p, /* start byte */ 0, bus_width);
                outb(SEND_MSG, p->base + RETURN_1);
              }
            }
            p->needwdtr &= ~target_mask;
            outb(scratch, p->base + TARG_SCRATCH + scratch_offset);
            outb(scratch, p->base + SCSIRATE);
            break;
	  }  /* case MSG_EXT_WDTR */

          default:
            /*
             * Unknown extended message - reject it.
             */
            outb(SEND_REJ, p->base + RETURN_1);
            break;
	}  /* switch (message_code) */
      }  /* case EXTENDED_MSG */
      break;

    case REJECT_MSG:
      {
	/*
	 * What we care about here is if we had an outstanding SDTR
	 * or WDTR message for this target. If we did, this is a
	 * signal that the target is refusing negotiation.
	 */
	unsigned char targ_scratch;
        unsigned char scb_index;

        scb_index = inb(p->base + SCB_TAG);
        scb = p->scb_data->scb_array[scb_index];
	targ_scratch = inb(p->base + TARG_SCRATCH + scratch_offset);

	if ((scb->flags & SCB_MSGOUT_WDTR) != 0)
	{
          /*
           * note 8bit xfers and clear flag
           */
          targ_scratch &= 0x7F;
          p->needwdtr &= ~target_mask;
          printk(KERN_WARNING "scsi%d: Target %d, channel %c, refusing WIDE "
  		 "negotiation; using 8 bit transfers.\n",
  		 p->host_no, target, channel);
	}
	else
	{
          if ((scb->flags & SCB_MSGOUT_SDTR) != 0)
          {
            /*
             * note asynch xfers and clear flag
             */
            targ_scratch &= 0xF0;
            p->needsdtr &= ~target_mask;
            printk(KERN_WARNING "scsi%d: Target %d, channel %c, refusing "
  		   "synchronous negotiation; using asynchronous transfers.\n",
  		   p->host_no, target, channel);
          }
          /*
           * Otherwise, we ignore it.
           */
	}
        outb(targ_scratch, p->base + TARG_SCRATCH + scratch_offset);
        outb(targ_scratch, p->base + SCSIRATE);
      }
      break;

    case BAD_STATUS:
      {
	unsigned char scb_index;
	struct aic7xxx_hwscb *hscb;
	Scsi_Cmnd *cmd;

	/* The sequencer will notify us when a command has an error that
	 * would be of interest to the kernel.  This allows us to leave
	 * the sequencer running in the common case of command completes
	 * without error.  The sequencer will have DMA'd the SCB back
	 * up to us, so we can reference the drivers SCB array.
	 */
	scb_index = inb(p->base + SCB_TAG);
	scb = p->scb_data->scb_array[scb_index];
	hscb = scb->hscb;

	/*
	 * Set the default return value to 0 indicating not to send
	 * sense.  The sense code will change this if needed and this
	 * reduces code duplication.
	 */
	outb(0, p->base + RETURN_1);
	if (!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
	{
          printk(KERN_WARNING "scsi%d: Referenced SCB not valid during "
        	 "SEQINT 0x%x, scb %d, flags 0x%x, cmd 0x%lx.\n", p->host_no,
        	 intstat, scb_index, scb->flags, (unsigned long) scb->cmd);
	}
	else
	{
          cmd = scb->cmd;
  	  hscb->target_status = inb(p->base + SCB_TARGET_STATUS);
          aic7xxx_status(cmd) = hscb->target_status;

          cmd->result |= hscb->target_status;

          switch (status_byte(hscb->target_status))
          {
            case GOOD:
  	      printk(KERN_WARNING "(scsi%d:%d:%d) Interrupted for status of "
                     "GOOD???\n", p->host_no, TC_OF_SCB(scb));
              break;

            case CHECK_CONDITION:
              if ((aic7xxx_error(cmd) == 0) && !(scb->flags & SCB_SENSE))
              {
        	unsigned int addr;    /* must be 32 bits */
        	/*
        	 * XXX - How do we save the residual (if there is one).
        	 */
                aic7xxx_calculate_residual(p, scb);

        	/*
  		 * Send a sense command to the requesting target.
        	 * XXX - revisit this and get rid of the memcopys.
  		 */
        	memcpy((void *) scb->sense_cmd, (void *) generic_sense,
        	       sizeof(generic_sense));

        	scb->sense_cmd[1] = (cmd->lun << 5);
        	scb->sense_cmd[4] = sizeof(cmd->sense_buffer);

        	scb->sg_list[0].address = cpu_to_le32(VIRT_TO_BUS(&cmd->sense_buffer));
        	scb->sg_list[0].length = cpu_to_le32(sizeof(cmd->sense_buffer));
        	cmd->cmd_len = COMMAND_SIZE(cmd->cmnd[0]);

                /*
                 * XXX - We should allow disconnection, but can't as it
                 * might allow overlapped tagged commands.
                 */
  		/* hscb->control &= DISCENB; */
                hscb->control = 0;
        	hscb->target_status = 0;
        	hscb->SG_segment_count = 1;

        	addr = VIRT_TO_BUS(&scb->sg_list[0]);
                hscb->SG_list_pointer = cpu_to_le32(addr);
                hscb->data_pointer = scb->sg_list[0].address;

        	/* Maintain SCB_LINKED_NEXT */
        	hscb->data_count &= cpu_to_le32(0xFF000000);
  		hscb->data_count |= scb->sg_list[0].length;

        	addr = VIRT_TO_BUS(scb->sense_cmd);
                hscb->SCSI_cmd_pointer = cpu_to_le32(addr);
        	hscb->SCSI_cmd_length = COMMAND_SIZE(scb->sense_cmd[0]);

                scb->sg_count = hscb->SG_segment_count;
        	scb->flags |= SCB_SENSE;
                /*
                 * Ensure the target is busy since this will be an
                 * an untagged request.
                 */
                aic7xxx_busy_target(p, target, channel, hscb->tag);
        	outb(SEND_SENSE, p->base + RETURN_1);
              }  /* first time sense, no errors */
  	      else
  	      {
        	if (aic7xxx_error(cmd) == 0)
  		{
        	  aic7xxx_error(cmd) = DID_RETRY_COMMAND;
  		}
  	      }
              break;

            case QUEUE_FULL:
#ifdef NOT_YET
              if (scb->hscb->control & TAG_ENB)
              {
        	if (cmd->device->queue_depth > 2)
        	{
                  cmd->device->queue_depth--;  /* Not correct */
                  printk(KERN_WARNING "(scsi%d:%d:%d) Tagged queue depth "
                	 "reduced to %d\n", p->host_no,
                	 TC_OF_SCB(scb), cmd->device->queue_depth);
        	}
        	/*
        	 * XXX - Requeue this unconditionally?
        	 */

        	/*
        	 * We'd like to be able to give the SCB some more time
        	 * (untimeout, then timeout).
        	 */
        	break;
              }
#endif
              printk(KERN_WARNING "(scsi%d:%d:%d) Queue full received; "
                     "queue depth %d, active %d\n", p->host_no,
                     TC_OF_SCB(scb), cmd->device->queue_depth,
                     p->device_status[TARGET_INDEX(cmd)].active_cmds);

              /* Else treat this as if it was a BUSY condition. */
              scb->hscb->target_status = (BUSY << 1) |
                  (scb->hscb->target_status & 0x01);
              /* Fall through to the BUSY case. */

            case BUSY:
              printk(KERN_WARNING "(scsi%d:%d:%d) Target busy\n",
                     p->host_no, TC_OF_SCB(scb));
              if (!aic7xxx_error(cmd))
              {
  		/*
        	 * The mid-level SCSI code should be fixed to
        	 * retry the command at a later time instead of
        	 * trying right away.
        	 */
        	aic7xxx_error(cmd) = DID_BUS_BUSY | (SUGGEST_RETRY << 8);
              }
              udelay(1000);  /*  A small pause (1ms) to help the drive */
              break;

            default:
              printk(KERN_WARNING "(scsi%d:%d:%d) Unexpected target "
                     "status 0x%x.\n", p->host_no,
        	     TC_OF_SCB(scb), scb->hscb->target_status);
              if (!aic7xxx_error(cmd))
              {
        	aic7xxx_error(cmd) = DID_RETRY_COMMAND;
              }
              break;
          }  /* end switch */
	}  /* end else of */
      }
      break;

    case AWAITING_MSG:
      {
	unsigned char scb_index;
        unsigned char message_offset;

	scb_index = inb(p->base + SCB_TAG);
	scb = p->scb_data->scb_array[scb_index];

	/*
	 * This SCB had a MK_MESSAGE set in its control byte informing
	 * the sequencer that we wanted to send a special message to
	 * this target.
	 */
        message_offset = inb(p->base + MSG_LEN);
	if (scb->flags & SCB_DEVICE_RESET)
	{
          outb(MSG_BUS_DEV_RESET, p->base + MSG_OUT);
          outb(1, p->base + MSG_LEN);
          printk(KERN_INFO "(scsi%d:%d:%d) Bus device reset sent\n",
        	 p->host_no, TC_OF_SCB(scb));
	}
        else if (scb->flags & SCB_ABORT)
        {
          if ((scb->hscb->control & TAG_ENB) != 0)
          {
            outb(MSG_ABORT_TAG, p->base + MSG_OUT + message_offset);
          }
          else
          {
            outb(MSG_ABORT, p->base + MSG_OUT + message_offset);
          }
          outb(message_offset + 1, p->base + MSG_LEN);
          printk(KERN_WARNING "(scsi%d:%d:%d): Abort message sent.\n",
                 p->host_no, TC_OF_SCB(scb));
        }
	else if (scb->flags & SCB_MSGOUT_WDTR)
	{
          aic7xxx_construct_wdtr(p, message_offset, BUS_16_BIT);
        }
        else if (scb->flags & SCB_MSGOUT_SDTR)
        {
          unsigned char target_scratch;
          unsigned short ultra_enable;
          int i, sxfr;

          /*
           * Pull the user defined setting from scratch RAM.
           */
          target_scratch = inb(p->base + TARG_SCRATCH + scratch_offset);
          sxfr = target_scratch & SXFR;
          ultra_enable = inb(p->base + ULTRA_ENB) |
              (inb(p->base + ULTRA_ENB + 1) << 8);
          if (ultra_enable & target_mask)
          {
            sxfr |= 0x100;
          }
          for (i = 0; i < num_aic7xxx_syncrates; i++)
          {
            if (sxfr == aic7xxx_syncrates[i].rate)
            break;
          }
          aic7xxx_construct_sdtr(p, message_offset,
                                 aic7xxx_syncrates[i].period,
                                 target_scratch & WIDEXFER ?
                                 MAX_OFFSET_16BIT : MAX_OFFSET_8BIT);
        }
        else 
        {
          panic("aic7xxx: AWAITING_MSG for an SCB that does "
                "not have a waiting message.");
	}
      }
      break;

    case DATA_OVERRUN:
      {
	unsigned char scb_index = inb(p->base + SCB_TAG);
        unsigned char lastphase = inb(p->base + LASTPHASE);
	unsigned int i, overrun;

	scb = (p->scb_data->scb_array[scb_index]);
	overrun = inb(p->base + STCNT) | (inb(p->base + STCNT + 1) << 8) |
  		  (inb(p->base + STCNT + 2) << 16);
	overrun = 0x00FFFFFF - overrun;
	printk(KERN_WARNING "(scsi%d:%d:%d) Data overrun of %d bytes detected "
               "in %s phase, tag %d; forcing a retry.\n",
               p->host_no, TC_OF_SCB(scb), overrun,
               lastphase == P_DATAIN ? "Data-In" : "Data-Out",
               scb->hscb->tag);
        printk(KERN_WARNING "%s seen Data Phase.  Length = %d, NumSGs = %d.\n",
               inb(p->base + SEQ_FLAGS) & DPHASE ? "Have" : "Haven't",
               aic7xxx_length(scb->cmd, 0), scb->sg_count);
        for (i = 0; i < scb->sg_count; i++)
        {
          printk(KERN_INFO "     sg[%d] - Addr 0x%x : Length %d\n",
                 i,
                 le32_to_cpu(scb->sg_list[i].address),
                 le32_to_cpu(scb->sg_list[i].length));
        }
	/*
	 * XXX - What do we really want to do on an overrun?  The
	 *       mid-level SCSI code should handle this, but for now,
	 *       we'll just indicate that the command should retried.
	 */
	aic7xxx_error(scb->cmd) = DID_RETRY_COMMAND;
      }
      break;

/* #if AIC7XXX_NOT_YET */
    /* XXX Fill these in later */
    case MSG_BUFFER_BUSY:
      printk("aic7xxx: Message buffer busy.\n");
      break;
    case MSGIN_PHASEMIS:
      printk("aic7xxx: Message-in phasemis.\n");
      break;
/*#endif */

    case ABORT_CMDCMPLT:
      /* This interrupt serves to pause the sequencer until we can clean
       * up the QOUTFIFO allowing us to handle any abort SCBs that may
       * completed yet still have an SCB in the QINFIFO or waiting for
       * selection queue.  By the time we get here, we should have
       * already cleaned up the queues, so all we need to do is unpause
       * the sequencer.
       */
      break;

    default:		   /* unknown */
      printk(KERN_WARNING "scsi%d: SEQINT, INTSTAT 0x%x, SCSISIGI 0x%x.\n",
             p->host_no, intstat, inb(p->base + SCSISIGI));
      break;
  }

  /*
   * Clear the sequencer interrupt and unpause the sequencer.
   */
  outb(CLRSEQINT, p->base + CLRINT);
  unpause_sequencer(p, /* unpause always */ TRUE);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_handle_scsiint
 *
 * Description:
 *   Interrupt handler for SCSI interrupts (SCSIINT).
 *-F*************************************************************************/
static void
aic7xxx_handle_scsiint(struct aic7xxx_host *p, unsigned char intstat)
{
  unsigned char scb_index;
  unsigned char status;
  struct aic7xxx_scb *scb;

  scb_index = inb(p->base + SCB_TAG);
  status = inb(p->base + SSTAT1);

  if (scb_index < p->scb_data->numscbs)
  {
    scb = p->scb_data->scb_array[scb_index];
    if ((scb->flags & SCB_ACTIVE) == 0)
    {
      scb = NULL;
    }
  }
  else
  {
    scb = NULL;
  }

  if ((status & SCSIRSTI) != 0)
  {
    char channel;

    channel = (inb(p->base + SBLKCTL) & SELBUSB) ? 'B' : 'A';

    printk(KERN_WARNING "scsi%d: SCSIINT - Someone reset channel %c.\n",
           p->host_no, channel);
    /*
     * Go through and abort all commands for the channel, but do not
     * reset the channel again.
     */
    aic7xxx_reset_channel(p, channel, /* Initiate Reset */ FALSE);
    scb = NULL;
  }
  else if ( ((status & BUSFREE) != 0) && ((status & SELTO) == 0) )
  {
    /*
     * First look at what phase we were last in.  If it's message-out,
     * chances are pretty good that the bus free was in response to
     * one of our abort requests.
     */
    unsigned char lastphase = inb(p->base + LASTPHASE);
    unsigned char target = (inb(p->base + SAVED_TCL) >> 4) & 0x0F;
    char channel = (inb(p->base + SBLKCTL) & SELBUSB) ? 'B' : 'A';
    int printerror = TRUE;

    outb(0, p->base + SCSISEQ);
    if (lastphase == P_MESGOUT)
    {
      unsigned char sindex;
      unsigned char message;

      sindex = inb(p->base + SINDEX);
      message = inb(p->base + sindex - 1);

      if (message == MSG_ABORT)
      {
        printk(KERN_WARNING "(scsi%d:%d:%d) SCB %d abort completed.\n",
                   p->host_no, TC_OF_SCB(scb), scb->hscb->tag);
        aic7xxx_reset_device(p, target, channel, SCB_LUN(scb), SCB_LIST_NULL);
        aic7xxx_run_done_queue(p, /* complete */ TRUE);
        scb = NULL;
        printerror = 0;
      }
      else if (message == MSG_ABORT_TAG)
      {
        printk(KERN_WARNING "(scsi%d:%d:%d) SCB %d abort Tag completed.\n",
                   p->host_no, TC_OF_SCB(scb), scb->hscb->tag);
        aic7xxx_reset_device(p, target, channel, SCB_LUN(scb), scb->hscb->tag);
        aic7xxx_run_done_queue(p, /* complete */ TRUE);
        scb = NULL;
        printerror = 0;
      }
      else if (message == MSG_BUS_DEV_RESET)
      {
        aic7xxx_handle_device_reset(p, target, channel);
        scb = NULL;
        printerror = 0;
      }
    }
    if (printerror != 0)
    {
      if (scb != NULL)
      {
        unsigned char tag;

        if ((scb->hscb->control & TAG_ENB) != 0)
        {
          tag = scb->hscb->tag;
        }
        else
        {
          tag = SCB_LIST_NULL;
        }
        aic7xxx_reset_device(p, target, channel, SCB_LUN(scb), tag);
      }
      else
      {
        aic7xxx_reset_device(p, target, channel, ALL_LUNS, SCB_LIST_NULL);
      }
      printk(KERN_WARNING "scsi%d: Unexpected busfree, LASTPHASE = 0x%x, "
             "SEQADDR = 0x%x\n", p->host_no, lastphase,
             (inb(p->base + SEQADDR1) << 8) | inb(p->base + SEQADDR0));
    }
    outb(inb(p->base + SIMODE1) & ~ENBUSFREE, p->base + SIMODE1);
    outb(CLRBUSFREE, p->base + CLRSINT1);
    outb(CLRSCSIINT, p->base + CLRINT);
    restart_sequencer(p);
  }
  else if ((status & SELTO) != 0)
  {
    unsigned char scbptr;
    unsigned char nextscb;
    Scsi_Cmnd *cmd;

    scbptr = inb(p->base + WAITING_SCBH);
    outb(scbptr, p->base + SCBPTR);
    scb_index = inb(p->base + SCB_TAG);

    scb = NULL;
    if (scb_index < p->scb_data->numscbs)
    {
      scb = p->scb_data->scb_array[scb_index];
      if ((scb->flags & SCB_ACTIVE) == 0)
      {
        scb = NULL;
      }
    }
    if (scb == NULL)
    {
      printk(KERN_WARNING "scsi%d: Referenced SCB %d not valid during SELTO.\n",
             p->host_no, scb_index);
      printk(KERN_WARNING "        SCSISEQ = 0x%x SEQADDR = 0x%x SSTAT0 = 0x%x "
             "SSTAT1 = 0x%x\n", inb(p->base + SCSISEQ),
             inb(p->base + SEQADDR0) | (inb(p->base + SEQADDR1) << 8),
             inb(p->base + SSTAT0), inb(p->base + SSTAT1));
    }
    else
    {
      /*
       * XXX - If we queued an abort tag, go clean up the disconnected list.
       */
      cmd = scb->cmd;
      cmd->result = (DID_TIME_OUT << 16);

      /*
       * Clear an pending messages for the timed out
       * target and mark the target as free.
       */
      outb(0, p->base + MSG_LEN);
      aic7xxx_index_busy_target(p, cmd->target,
          cmd->channel ? 'B': 'A', /*unbusy*/ TRUE);
      outb(0, p->base + SCB_CONTROL);

      /*
       * Shift the waiting for selection queue forward
       */
      nextscb = inb(p->base + SCB_NEXT);
      outb(nextscb, p->base + WAITING_SCBH);

      /*
       * Put this SCB back on the free list.
       */
      aic7xxx_add_curscb_to_free_list(p);
    }
    /*
     * Stop the selection.
     */
    outb(0, p->base + SCSISEQ);
    outb(CLRSELTIMEO | CLRBUSFREE, p->base + CLRSINT1);
    outb(CLRSCSIINT, p->base + CLRINT);
    restart_sequencer(p);
  }
  else if (scb == NULL)
  {
    printk(KERN_WARNING "scsi%d: aic7xxx_isr - referenced scb not valid "
           "during scsiint 0x%x scb(%d)\n"
           "      SIMODE0 0x%x, SIMODE1 0x%x, SSTAT0 0x%x, SEQADDR 0x%x\n",
           p->host_no, status, scb_index, inb(p->base + SIMODE0),
           inb(p->base + SIMODE1), inb(p->base + SSTAT0),
           (inb(p->base + SEQADDR1) << 8) | inb(p->base + SEQADDR0));
    /*
     * Turn off the interrupt and set status to zero, so that it
     * falls through the rest of the SCSIINT code.
     */
    outb(status, p->base + CLRSINT1);
    outb(CLRSCSIINT, p->base + CLRINT);
    unpause_sequencer(p, /* unpause always */ TRUE);
    scb = NULL;
  }
  else if (status & SCSIPERR)
  {
    /*
     * Determine the bus phase and queue an appropriate message.
     */
    char  *phase;
    Scsi_Cmnd *cmd;
    unsigned char mesg_out = MSG_NOOP;
    unsigned char lastphase = inb(p->base + LASTPHASE);

    cmd = scb->cmd;
    switch (lastphase)
    {
      case P_DATAOUT:
        phase = "Data-Out";
        break;
      case P_DATAIN:
        phase = "Data-In";
        mesg_out = MSG_INITIATOR_DET_ERR;
        break;
      case P_COMMAND:
        phase = "Command";
        break;
      case P_MESGOUT:
        phase = "Message-Out";
        break;
      case P_STATUS:
        phase = "Status";
        mesg_out = MSG_INITIATOR_DET_ERR;
        break;
      case P_MESGIN:
        phase = "Message-In";
        mesg_out = MSG_PARITY_ERROR;
        break;
      default:
        phase = "unknown";
        break;
    }

    /*
     * A parity error has occurred during a data
     * transfer phase. Flag it and continue.
     */
    printk(KERN_WARNING "(scsi%d:%d:%d) Parity error during phase %s.\n",
           p->host_no, TC_OF_SCB(scb), phase);

    /*
     * We've set the hardware to assert ATN if we get a parity
     * error on "in" phases, so all we need to do is stuff the
     * message buffer with the appropriate message.  "In" phases
     * have set mesg_out to something other than MSG_NOP.
     */
    if (mesg_out != MSG_NOOP)
    {
      outb(mesg_out, p->base + MSG_OUT);
      outb(1, p->base + MSG_LEN);
      scb = NULL;
    }
    else
    {
      /*
       * Should we allow the target to make this decision for us?
       */
      cmd->result = DID_RETRY_COMMAND << 16;
    }
    outb(CLRSCSIPERR, p->base + CLRSINT1);
    outb(CLRSCSIINT, p->base + CLRINT);
    unpause_sequencer(p, /* unpause_always */ TRUE);
  }
  else
  {
    /*
     * We don't know what's going on. Turn off the
     * interrupt source and try to continue.
     */
    printk(KERN_WARNING "aic7xxx: SSTAT1(0x%x).\n", status);
    outb(status, p->base + CLRSINT1);
    outb(CLRSCSIINT, p->base + CLRINT);
    unpause_sequencer(p, /* unpause always */ TRUE);
    scb = NULL;
  }
  if (scb != NULL)
  {
    aic7xxx_done(p, scb);
    aic7xxx_done_cmds_complete(p);
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_isr
 *
 * Description:
 *   SCSI controller interrupt handler.
 *-F*************************************************************************/
static void
aic7xxx_isr(int irq, void *dev_id, struct pt_regs *regs)
{
  struct aic7xxx_host *p = (struct aic7xxx_host *) dev_id;
  unsigned char intstat;
  unsigned long flags;

  /*
   * Handle all the interrupt sources - especially for SCSI
   * interrupts, we won't get a second chance at them.
   */
  intstat = inb(p->base + INTSTAT);
  if (! (intstat & INT_PEND))	/* Interrupt for another device */
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
    printk("scsi%d: Encountered spurious interrupt.\n", p->host_no);
    if (intstat)
    {
      /* Try clearing all interrupts. */
      outb(CLRBRKADRINT | CLRSCSIINT | CLRCMDINT | CLRSEQINT, p->base + CLRINT);
    }
    return;
  }

  if (p->flags & IN_ISR)
  {
    printk(KERN_WARNING "scsi%d: Warning!! Interrupt routine called reentrantly!\n",
           p->host_no);
    return;
  }

  /*
   * Indicate that we're in the interrupt handler.
   */
  save_flags(flags);
  cli();
  p->flags |= IN_ISR;

  if (intstat & CMDCMPLT)
  {
    struct aic7xxx_scb *scb = NULL;
    Scsi_Cmnd *cmd;
    unsigned char qoutcnt;
    unsigned char scb_index;
    int i, interrupts_cleared = 0;

    /*
     * The sequencer will continue running when it
     * issues this interrupt. There may be >1 commands
     * finished, so loop until we've processed them all.
     */
    qoutcnt = inb(p->base + QOUTCNT) & p->qcntmask;

#if 1
  if (qoutcnt >= p->qfullcount - 1)
    printk(KERN_WARNING "aic7xxx: Command complete near Qfull count, "
           "qoutcnt = %d.\n", qoutcnt);
#endif
    while (qoutcnt > 0)
    {
      if ((p->flags & PAGE_ENABLED) != 0)
      {
        p->cmdoutcnt += qoutcnt;
        if (p->cmdoutcnt >= p->qfullcount)
        {
          /*
           * Since paging only occurs on aic78x0 chips, we can use
           * Auto Access Pause to clear the command count.
           */
          outb(0, p->base + CMDOUTCNT);
          p->cmdoutcnt = 0;
        }
      }
      for (i = 0; i < qoutcnt; i++)
      {
        scb_index = inb(p->base + QOUTFIFO);
        scb = p->scb_data->scb_array[scb_index];
        if (scb == NULL)
        {
	  printk(KERN_WARNING "scsi%d: CMDCMPLT with invalid SCB index %d, "
	         "QOUTCNT %d, QINCNT %d\n", p->host_no, scb_index,
                 inb(p->base + QOUTCNT), inb(p->base + QINCNT));
          continue;
        }
        else if (!(scb->flags & SCB_ACTIVE) || (scb->cmd == NULL))
        {
	  printk(KERN_WARNING "scsi%d: CMDCMPLT without command for SCB %d, "
	         "QOUTCNT %d, QINCNT %d, SCB flags 0x%x, cmd 0x%lx\n",
                 p->host_no, scb_index, inb(p->base + QOUTCNT),
                 inb(p->base + QINCNT), scb->flags, (unsigned long) scb->cmd);
	  continue;
        }
        cmd = scb->cmd;
        if (scb->hscb->residual_SG_segment_count != 0)
        {
          aic7xxx_calculate_residual(p, scb);
        }
        if ((scb->flags & SCB_QUEUED_ABORT) != 0)
        {
          /*
           * Have to clean up any possible entries in the
           * waiting queue and the QINFIFO.
           */
          int target;
          char channel;
          int lun;
          unsigned char tag;

          tag = SCB_LIST_NULL;
          target = cmd->target;
          lun = cmd->lun;
          channel = (scb->hscb->target_channel_lun & SELBUSB) ? 'B' : 'A';
          if (scb->hscb->control & TAG_ENB)
          {
            tag = scb->hscb->tag;
          }
          aic7xxx_reset_device(p, target, channel, lun, tag);
          /*
           * Run the done queue, but don't complete the commands; we
           * do this once at the end of the loop.
           */
          aic7xxx_run_done_queue(p, /*complete*/ FALSE);
        }
        cmd->result |= (aic7xxx_error(cmd) << 16);
        p->device_status[TARGET_INDEX(cmd)].flags |= DEVICE_SUCCESS;
        aic7xxx_done(p, scb);
      }
      /*
       * Clear interrupt status before checking the output queue again.
       * This eliminates a race condition whereby a command could
       * complete between the queue poll and the interrupt clearing,
       * so notification of the command being complete never made it
       * back up to the kernel.
       */
      outb(CLRCMDINT, p->base + CLRINT);
      interrupts_cleared++;
      qoutcnt = inb(p->base + QOUTCNT) & p->qcntmask;
    }

    if (interrupts_cleared == 0)
    {
      outb(CLRCMDINT, p->base + CLRINT);
    }

    aic7xxx_done_cmds_complete(p);
  }

  if (intstat & BRKADRINT)
  {
    int i;
    unsigned char errno = inb(p->base + ERROR);

    printk(KERN_ERR "scsi%d: BRKADRINT error(0x%x):\n", p->host_no, errno);
    for (i = 0; i < NUMBER(hard_error); i++)
    {
      if (errno & hard_error[i].errno)
      {
        printk(KERN_ERR "  %s\n", hard_error[i].errmesg);
      }
    }
    printk("scsi%d: BRKADRINT, error 0x%x, seqaddr 0x%x.\n", p->host_no,
           inb(p->base + ERROR),
           (inb(p->base + SEQADDR1) << 8) | inb(p->base + SEQADDR0));
    aic7xxx_reset_device(p, ALL_TARGETS, ALL_CHANNELS, ALL_LUNS, SCB_LIST_NULL);
    aic7xxx_run_done_queue(p, /*complete*/ TRUE);
  }

  if (intstat & SEQINT)
  {
    aic7xxx_handle_seqint(p, intstat);
  }

  if (intstat & SCSIINT)
  {
    aic7xxx_handle_scsiint(p, intstat);
  }

  if (p->waiting_scbs.head != NULL)
  {
    aic7xxx_run_waiting_queues(p);
  }

  p->flags &= ~IN_ISR;
  restore_flags(flags);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_device_queue_depth
 *
 * Description:
 *   Determines the queue depth for a given device.  There are two ways
 *   a queue depth can be obtained for a tagged queueing device.  One
 *   way is the default queue depth which is determined by whether
 *   AIC7XXX_CMDS_PER_LUN is defined.  If it is defined, then it is used
 *   as the default queue depth.  Otherwise, we use either 4 or 8 as the
 *   default queue depth (dependent on the number of hardware SCBs).
 *   The other way we determine queue depth is through the use of the
 *   aic7xxx_tag_info array which is enabled by defining
 *   AIC7XXX_TAGGED_QUEUEING_BY_DEVICE.  This array can be initialized
 *   with queue depths for individual devices.  It also allows tagged
 *   queueing to be [en|dis]abled for a specific adapter.
 *-F*************************************************************************/
static void
aic7xxx_device_queue_depth(struct aic7xxx_host *p, Scsi_Device *device)
{
  int default_depth = 2;

  device->queue_depth = default_depth;
#ifdef AIC7XXX_TAGGED_QUEUEING
  if (device->tagged_supported)
  {
    unsigned short target_mask;
    int tag_enabled = TRUE;

    target_mask = (1 << (device->id | (device->channel << 3)));

#ifdef AIC7XXX_CMDS_PER_LUN
    default_depth = AIC7XXX_CMDS_PER_LUN;
#else
    if (p->scb_data->maxhscbs <= 4)
    {
      default_depth = 4;  /* Not many SCBs to work with. */
    }
    else
    {
      default_depth = 8;
    }
#endif
 
    if (!(p->discenable & target_mask))
    {
      printk(KERN_INFO "(scsi%d:%d:%d) Disconnection disabled, unable to "
             "enable tagged queueing.\n",
             p->host_no, device->id, device->channel);
    }
    else
    {
#ifndef AIC7XXX_TAGGED_QUEUEING_BY_DEVICE
      device->queue_depth = default_depth;
#else
      if (p->instance >= NUMBER(aic7xxx_tag_info))
      {
        device->queue_depth = default_depth;
      }
      else
      {
        unsigned char  tindex;

        tindex = device->id | (device->channel << 3);
        if (aic7xxx_tag_info[p->instance].tag_commands[tindex] < 0)
        {
          tag_enabled = FALSE;
          device->queue_depth = 2;  /* Tagged queueing is disabled. */
        }
        else if (aic7xxx_tag_info[p->instance].tag_commands[tindex] == 0)
        {
          device->queue_depth = default_depth;
        }
        else
        {
          device->queue_depth =
            aic7xxx_tag_info[p->instance].tag_commands[tindex];
        }
      }
#endif
      if ((device->tagged_queue == 0) && tag_enabled)
      {
        if (aic7xxx_verbose)
        {
    	  printk(KERN_INFO "(scsi%d:%d:%d) Enabled tagged queuing, "
    	         "queue depth %d.\n", p->host_no,
    	         device->id, device->channel, device->queue_depth);
        }
        device->tagged_queue = 1;
        device->current_tag = SCB_LIST_NULL;
      }
    }
  }
#endif
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
static void
aic7xxx_select_queue_depth(struct Scsi_Host *host,
    Scsi_Device *scsi_devs)
{
  Scsi_Device *device;
  struct aic7xxx_host *p = (struct aic7xxx_host *) host->hostdata;

  for (device = scsi_devs; device != NULL; device = device->next)
  {
    if (device->host == host)
    {
      aic7xxx_device_queue_depth(p, device);
    }
  }
}

#if !defined(__sparc_v9__) && !defined(__powerpc__)
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
static aha_chip_type
aic7xxx_probe(int slot, int base, aha_status_type *bios)
{
  int i;
  unsigned char buf[4];

  static struct {
    int n;
    unsigned char signature[sizeof(buf)];
    aha_chip_type type;
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

      printk("aic7xxx: <Adaptec 7770 SCSI Host Adapter> "
             "disabled at slot %d, ignored.\n", slot);
    }
  }

  return (AIC_NONE);
}
#endif /* __sparc_v9__ or __powerpc__ */

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
 *-F*************************************************************************/
static int
read_284x_seeprom(struct aic7xxx_host *p, struct seeprom_config *sc)
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
  while ((inb(p->base + STATUS_2840) & EEPROM_TF) == 0)	\
  {						\
    ;  /* Do nothing */				\
  }						\
  (void) inb(p->base + SEECTL_2840);

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
    outb(CK_2840 | CS_2840, p->base + SEECTL_2840);
    CLOCK_PULSE(p);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = CS_2840 | seeprom_read.bits[i];
      outb(temp, p->base + SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      outb(temp, p->base + SEECTL_2840);
      CLOCK_PULSE(p);
    }
    /*
     * Send the 6 bit address (MSB first, LSB last).
     */
    for (i = 5; i >= 0; i--)
    {
      temp = k;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = CS_2840 | temp;
      outb(temp, p->base + SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      outb(temp, p->base + SEECTL_2840);
      CLOCK_PULSE(p);
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
      outb(temp, p->base + SEECTL_2840);
      CLOCK_PULSE(p);
      temp = temp ^ CK_2840;
      seeprom[k] = (seeprom[k] << 1) | (inb(p->base + STATUS_2840) & DI_2840);
      outb(temp, p->base + SEECTL_2840);
      CLOCK_PULSE(p);
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
    outb(0, p->base + SEECTL_2840);
    CLOCK_PULSE(p);
    outb(CK_2840, p->base + SEECTL_2840);
    CLOCK_PULSE(p);
    outb(0, p->base + SEECTL_2840);
    CLOCK_PULSE(p);
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
 *   acquire_seeprom
 *
 * Description:
 *   Acquires access to the memory port on PCI controllers.
 *-F*************************************************************************/
static inline int
acquire_seeprom(struct aic7xxx_host *p)
{
  int wait;

  /*
   * Request access of the memory port.  When access is
   * granted, SEERDY will go high.  We use a 1 second
   * timeout which should be near 1 second more than
   * is needed.  Reason: after the 7870 chip reset, there
   * should be no contention.
   */
  outb(SEEMS, p->base + SEECTL);
  wait = 1000;  /* 1000 msec = 1 second */
  while ((wait > 0) && ((inb(p->base + SEECTL) & SEERDY) == 0))
  {
    wait--;
    udelay(1000);  /* 1 msec */
  }
  if ((inb(p->base + SEECTL) & SEERDY) == 0)
  {
    outb(0, p->base + SEECTL);
    return (0);
  }
  return (1);
}

/*+F*************************************************************************
 * Function:
 *   release_seeprom
 *
 * Description:
 *   Releases access to the memory port on PCI controllers.
 *-F*************************************************************************/
static inline void
release_seeprom(struct aic7xxx_host *p)
{
  outb(0, p->base + SEECTL);
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
 *   The 78xx interface to the 93C46 serial EEPROM is through the SEECTL
 *   register.  After successful arbitration for the memory port, the
 *   SEECS bit of the SEECTL register is connected to the chip select.
 *   The SEECK, SEEDO, and SEEDI are connected to the clock, data out,
 *   and data in lines respectively.  The SEERDY bit of SEECTL is useful
 *   in that it gives us an 800 nsec timer.  After a write to the SEECTL
 *   register, the SEERDY goes high 800 nsec later.  The one exception
 *   to this is when we first request access to the memory port.  The
 *   SEERDY goes high to signify that access has been granted and, for
 *   this case, has no implied timing.
 *-F*************************************************************************/
static int
read_seeprom(struct aic7xxx_host *p, int offset, unsigned short *scarray,
    unsigned int len, seeprom_chip_type chip)
{
  int i = 0, k;
  unsigned char temp;
  unsigned short checksum = 0;
  struct seeprom_cmd {
    unsigned char len;
    unsigned char bits[3];
  };
  struct seeprom_cmd seeprom_read = {3, {1, 1, 0}};

#define CLOCK_PULSE(p) \
  while ((inb(p->base + SEECTL) & SEERDY) == 0)	\
  {						\
    ;  /* Do nothing */				\
  }

  /*
   * Request access of the memory port.
   */
  if (acquire_seeprom(p) == 0)
  {
    return (0);
  }

  /*
   * Read 'len' registers of the seeprom.  For the 7870, the 93C46
   * SEEPROM is a 1024-bit device with 64 16-bit registers but only
   * the first 32 are used by Adaptec BIOS.  Some adapters use the
   * 93C56 SEEPROM which is a 2048-bit device.  The loop will range
   * from 0 to 'len' - 1.
   */
  for (k = 0; k < len; k++)
  {
    /*
     * Send chip select for one clock cycle.
     */
    outb(SEEMS | SEECK | SEECS, p->base + SEECTL);
    CLOCK_PULSE(p);

    /*
     * Now we're ready to send the read command followed by the
     * address of the 16-bit register we want to read.
     */
    for (i = 0; i < seeprom_read.len; i++)
    {
      temp = SEEMS | SEECS | (seeprom_read.bits[i] << 1);
      outb(temp, p->base + SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      outb(temp, p->base + SEECTL);
      CLOCK_PULSE(p);
    }
    /*
     * Send the 6 or 8 bit address (MSB first, LSB last).
     */
    for (i = ((int) chip - 1); i >= 0; i--)
    {
      temp = k + offset;
      temp = (temp >> i) & 1;  /* Mask out all but lower bit. */
      temp = SEEMS | SEECS | (temp << 1);
      outb(temp, p->base + SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      outb(temp, p->base + SEECTL);
      CLOCK_PULSE(p);
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
      outb(temp, p->base + SEECTL);
      CLOCK_PULSE(p);
      temp = temp ^ SEECK;
      scarray[k] = (scarray[k] << 1) | (inb(p->base + SEECTL) & SEEDI);
      outb(temp, p->base + SEECTL);
      CLOCK_PULSE(p);
    }

    /*
     * The serial EEPROM should have a checksum in the last word.
     * Keep a running checksum for all words read except for the
     * last word.  We'll verify the checksum after all words have
     * been read.
     */
    if (k < (len - 1))
    {
      checksum = checksum + scarray[k];
    }

    /*
     * Reset the chip select for the next command cycle.
     */
    outb(SEEMS, p->base + SEECTL);
    CLOCK_PULSE(p);
    outb(SEEMS | SEECK, p->base + SEECTL);
    CLOCK_PULSE(p);
    outb(SEEMS, p->base + SEECTL);
    CLOCK_PULSE(p);
  }

  /*
   * Release access to the memory port and the serial EEPROM.
   */
  release_seeprom(p);

#if 0
  printk("Computed checksum 0x%x, checksum read 0x%x\n",
         checksum, scarray[len - 1]);
  printk("Serial EEPROM:");
  for (k = 0; k < len; k++)
  {
    if (((k % 8) == 0) && (k != 0))
    {
      printk("\n              ");
    }
    printk(" 0x%x", scarray[k]);
  }
  printk("\n");
#endif

  if (checksum != scarray[len - 1])
  {
    return (0);
  }

  return (1);
#undef CLOCK_PULSE
}

/*+F*************************************************************************
 * Function:
 *   write_brdctl
 *
 * Description:
 *   Writes a value to the BRDCTL register.
 *-F*************************************************************************/
static inline void
write_brdctl(struct aic7xxx_host *p, unsigned char value)
{
  unsigned char brdctl;

  brdctl = BRDCS | BRDSTB;
  outb(brdctl, p->base + BRDCTL);
  brdctl |= value;
  outb(brdctl, p->base + BRDCTL);
  brdctl &= ~BRDSTB;
  outb(brdctl, p->base + BRDCTL);
  brdctl &= ~BRDCS;
  outb(brdctl, p->base + BRDCTL);
}

/*+F*************************************************************************
 * Function:
 *   read_brdctl
 *
 * Description:
 *   Reads the BRDCTL register.
 *-F*************************************************************************/
static inline unsigned char
read_brdctl(struct aic7xxx_host *p)
{
  outb(BRDRW | BRDCS, p->base + BRDCTL);
  return (inb(p->base + BRDCTL));
}

/*+F*************************************************************************
 * Function:
 *   configure_termination
 *
 * Description:
 *   Configures the termination settings on PCI adapters that have
 *   SEEPROMs available.
 *-F*************************************************************************/
static void
configure_termination(struct aic7xxx_host *p, unsigned char *sxfrctl1,
    unsigned short adapter_control, unsigned char max_targ)
{
  unsigned char brdctl_int, brdctl_ext;
  int internal50_present;
  int internal68_present = 0;
  int external_present = 0;
  int eprom_present;
  int high_on;
  int low_on;
  int old_verbose;

  if (acquire_seeprom(p))
  {
    if (adapter_control & CFAUTOTERM)
    {
      old_verbose = aic7xxx_verbose;
      printk(KERN_INFO "aic7xxx: Warning - detected auto-termination.  Please "
                       "verify driver\n");
      printk(KERN_INFO "         detected settings and use manual termination "
                       "if necessary.\n"); 

      /* Configure auto termination. */
      outb(SEECS | SEEMS, p->base + SEECTL);

      /*
       * First read the status of our cables.  Set the rom bank to
       * 0 since the bank setting serves as a multiplexor for the
       * cable detection logic.  BRDDAT5 controls the bank switch.
       */
      write_brdctl(p, 0);

      /*
       * Now read the state of the internal connectors.  The
       * bits BRDDAT6 and BRDDAT7 are 0 when cables are present
       * set when cables are not present (BRDDAT6 is INT50 and
       * BRDDAT7 is INT68).
       */
      brdctl_int = read_brdctl(p);
      internal50_present = (brdctl_int & BRDDAT6) ? 0 : 1;
      if (max_targ > 8)
      {
        internal68_present = (brdctl_int & BRDDAT7) ? 0 : 1;
      }

      /*
       * Set the rom bank to 1 and determine
       * the other signals.
       */
      write_brdctl(p, BRDDAT5);

      /*
       * Now read the state of the external connectors.  BRDDAT6 is
       * 0 when an external cable is present, and BRDDAT7 (EPROMPS) is
       * set when the eprom is present.
       */
      brdctl_ext = read_brdctl(p);
      external_present = (brdctl_ext & BRDDAT6) ? 0 : 1;
      eprom_present = brdctl_ext & BRDDAT7;
      if (aic7xxx_verbose)
      {
        if (max_targ > 8)
        {
          printk(KERN_INFO "aic7xxx: Cables present (Int-50 %s, Int-68 %s, "
                 "Ext-68 %s)\n",
                 internal50_present ? "YES" : "NO",
                 internal68_present ? "YES" : "NO",
                 external_present ? "YES" : "NO");
        }
        else
        {
          printk(KERN_INFO "aic7xxx: Cables present (Int-50 %s, Ext-50 %s)\n",
                 internal50_present ? "YES" : "NO",
                 external_present ? "YES" : "NO");
        }
        printk(KERN_INFO "aic7xxx: eprom %s present, brdctl_int=0x%x, "
               "brdctl_ext=0x%x\n",
               eprom_present ? "is" : "not", brdctl_int, brdctl_ext);
      }

      /*
       * Now set the termination based on what we found.  BRDDAT6
       * controls wide termination enable.
       */
      high_on = FALSE;
      low_on = FALSE;
      if ((max_targ > 8) &&
          ((external_present == 0) || (internal68_present == 0)))
      {
        high_on = TRUE;
      }

      if ((internal50_present + internal68_present + external_present) <= 1)
      {
        low_on = TRUE;
      }
          
      if (internal50_present && internal68_present && external_present)
      {
        printk(KERN_WARNING "aic7xxx: Illegal cable configuration!!\n"
               "         Only two connectors on the adapter may be "
               "used at a time!\n");
      }

      if (high_on == TRUE)
        write_brdctl(p, BRDDAT6);
      else
        write_brdctl(p, 0);

      if (low_on == TRUE)
        *sxfrctl1 |= STPWEN;

      if (aic7xxx_verbose)
      {
        if (max_targ > 8)
        {
          printk(KERN_INFO "aic7xxx: Termination (Low %s, High %s)\n",
                 low_on ? "ON" : "OFF",
                 high_on ? "ON" : "OFF");
        }
        else
        {
          printk(KERN_INFO "aic7xxx: Termination %s\n", low_on ? "ON" : "OFF");
        }
      }
      aic7xxx_verbose = old_verbose;
    }
    else
    {
      if (adapter_control & CFSTERM)
      {
        *sxfrctl1 |= STPWEN;
      }
      outb(SEEMS | SEECS, p->base + SEECTL);
      /*
       * Configure high byte termination.
       */
      if (adapter_control & CFWSTERM)
      {
        write_brdctl(p, BRDDAT6);
      }
      else
      {
        write_brdctl(p, 0);
      }
      if (aic7xxx_verbose)
      {
        printk(KERN_INFO "aic7xxx: Termination (Low %s, High %s)\n",
               (adapter_control & CFSTERM) ? "ON" : "OFF",
               (adapter_control & CFWSTERM) ? "ON" : "OFF");
      }
    }
    release_seeprom(p);
  }
}

/*+F*************************************************************************
 * Function:
 *   detect_maxscb
 *
 * Description:
 *   Detects the maximum number of SCBs for the controller and returns
 *   the count and a mask in p (p->maxscbs, p->qcntmask).
 *-F*************************************************************************/
static void
detect_maxscb(struct aic7xxx_host *p)
{
  int i;
  unsigned char max_scbid = 255;

  /*
   * It's possible that we've already done this for multichannel
   * adapters.
   */
  if (p->scb_data->maxhscbs == 0)
  {
    /*
     * We haven't initialized the SCB settings yet.  Walk the SCBs to
     * determince how many there are.
     */
    outb(0, p->base + FREE_SCBH);

    for (i = 0; i < AIC7XXX_MAXSCB; i++)
    {
      outb(i, p->base + SCBPTR);
      outb(i, p->base + SCB_CONTROL);
      if (inb(p->base + SCB_CONTROL) != i)
        break;
      outb(0, p->base + SCBPTR);
      if (inb(p->base + SCB_CONTROL) != 0)
        break;

      outb(i, p->base + SCBPTR);
      outb(0, p->base + SCB_CONTROL);   /* Clear the control byte. */
      outb(i + 1, p->base + SCB_NEXT);  /* Set the next pointer. */
      outb(SCB_LIST_NULL, p->base + SCB_TAG);  /* Make the tag invalid. */

      /* Make the non-tagged targets not busy. */
      outb(SCB_LIST_NULL, p->base + SCB_BUSYTARGETS);
      outb(SCB_LIST_NULL, p->base + SCB_BUSYTARGETS + 1);
      outb(SCB_LIST_NULL, p->base + SCB_BUSYTARGETS + 2);
      outb(SCB_LIST_NULL, p->base + SCB_BUSYTARGETS + 3);
    }

    /* Make sure the last SCB terminates the free list. */
    outb(i - 1, p->base + SCBPTR);
    outb(SCB_LIST_NULL, p->base + SCB_NEXT);

    /* Ensure we clear the first (0) SCBs control byte. */
    outb(0, p->base + SCBPTR);
    outb(0, p->base + SCB_CONTROL);

    p->scb_data->maxhscbs = i;
  }

  if ((p->flags & PAGE_ENABLED) && (p->scb_data->maxhscbs < AIC7XXX_MAXSCB))
  {
    /* Determine the number of valid bits in the FIFOs. */
    outb(max_scbid, p->base + QINFIFO);
    max_scbid = inb(p->base + QINFIFO);
    p->scb_data->maxscbs = MIN(AIC7XXX_MAXSCB, max_scbid + 1);
  }
  else
  {
    p->scb_data->maxscbs = p->scb_data->maxhscbs;
  }
  if (p->scb_data->maxscbs == p->scb_data->maxhscbs)
  {
    /*
     * Disable paging if the QINFIFO doesn't allow more SCBs than
     * we have in hardware.
     */
    p->flags &= ~PAGE_ENABLED;
  }

  /*
   * Set the Queue Full Count.  Some cards have more queue space than
   * SCBs.
   */
  switch (p->chip_class)
  {
    case AIC_777x:
      p->qfullcount = 4;
      p->qcntmask = 0x07;
      break;
    case AIC_785x:
    case AIC_786x:
      p->qfullcount = 8;
      p->qcntmask = 0x0f;
      break;
    case AIC_787x:
    case AIC_788x:
      if (p->scb_data->maxhscbs == AIC7XXX_MAXSCB)
      {
        p->qfullcount = AIC7XXX_MAXSCB;
        p->qcntmask = 0xFF;
      }
      else
      {
        p->qfullcount = 16;
        p->qcntmask = 0x1F;
      }
      break;
  }
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_register
 *
 * Description:
 *   Register a Adaptec aic7xxx chip SCSI controller with the kernel.
 *-F*************************************************************************/
static int
aic7xxx_register(Scsi_Host_Template *template, struct aic7xxx_host *p)
{
  int i;
  unsigned char sblkctl, flags = 0;
  int max_targets, irq_flags = 0;
  int found = 1;
  char channel_ids[] = {'A', 'B', 'C'};
  unsigned char target_settings;
  unsigned char scsi_conf, sxfrctl1;
  unsigned short ultraenable = 0;
  struct Scsi_Host *host;

  /*
   * Lock out other contenders for our i/o space.
   */
  request_region(p->base, MAXREG - MINREG, "aic7xxx");

  /*
   * Read the bus type from the SBLKCTL register. Set the FLAGS
   * register in the sequencer for twin and wide bus cards.
   */
  sblkctl = inb(p->base + SBLKCTL);
  if (p->flags & PAGE_ENABLED)
    flags = PAGESCBS;

  switch (sblkctl & SELBUS_MASK)
  {
    case SELNARROW:     /* narrow/normal bus */
      p->scsi_id = inb(p->base + SCSICONF) & 0x07;
      p->bus_type = AIC_SINGLE;
      p->flags &= ~FLAGS_CHANNEL_B_PRIMARY;
      if (p->flags & MULTI_CHANNEL)
      {
        printk(KERN_INFO "aic7xxx: Channel %c, SCSI ID %d, ",
               channel_ids[p->chan_num], p->scsi_id);
      }
      else
      {
        printk (KERN_INFO "aic7xxx: Single Channel, SCSI ID %d, ",
                p->scsi_id);
      }
      outb(flags | SINGLE_BUS, p->base + SEQ_FLAGS);
      break;

    case SELWIDE:     /* Wide bus */
      p->scsi_id = inb(p->base + SCSICONF + 1) & HWSCSIID;
      p->bus_type = AIC_WIDE;
      p->flags &= ~FLAGS_CHANNEL_B_PRIMARY;
      if (p->flags & MULTI_CHANNEL)
      {
        printk(KERN_INFO "aic7xxx: Wide Channel %c, SCSI ID %d, ",
               channel_ids[p->chan_num], p->scsi_id);
      }
      else
      {
        printk (KERN_INFO "aic7xxx: Wide Channel, SCSI ID %d, ",
                p->scsi_id);
      }
      outb(flags | WIDE_BUS, p->base + SEQ_FLAGS);
      break;

    case SELBUSB:     /* Twin bus */
      p->scsi_id = inb(p->base + SCSICONF) & HSCSIID;
      p->scsi_id_b = inb(p->base + SCSICONF + 1) & HSCSIID;
      p->bus_type = AIC_TWIN;
      printk(KERN_INFO "aic7xxx: Twin Channel, A SCSI ID %d, B SCSI ID %d, ",
             p->scsi_id, p->scsi_id_b);
      outb(flags | TWIN_BUS, p->base + SEQ_FLAGS);
      break;

    default:
      printk(KERN_WARNING "aic7xxx: Unsupported type 0x%x, please "
	     "mail deang@teleport.com\n", inb(p->base + SBLKCTL));
      outb(0, p->base + SEQ_FLAGS);
      return (0);
  }

  /*
   * Detect SCB parameters and initialize the SCB array.
   */
  detect_maxscb(p);
  printk("%d/%d SCBs, QFull %d, QMask 0x%x\n",
         p->scb_data->maxhscbs, p->scb_data->maxscbs,
         p->qfullcount, p->qcntmask);

  host = p->host;

  host->can_queue = p->scb_data->maxscbs;
  host->cmd_per_lun = 2;
  host->sg_tablesize = AIC7XXX_MAX_SG;
  host->select_queue_depths = aic7xxx_select_queue_depth;
  host->this_id = p->scsi_id;
  host->io_port = p->base;
  host->n_io_port = 0xFF;
  host->base = (unsigned char *) p->mbase;
  host->irq = p->irq;
  if (p->bus_type == AIC_WIDE)
  {
    host->max_id = 16;
  }
  if (p->bus_type == AIC_TWIN)
  {
    host->max_channel = 1;
  }

  p->host = host;
  p->host_no = host->host_no;
  p->isr_count = 0;
  p->completeq.head = NULL;
  p->completeq.tail = NULL;
  scbq_init(&p->scb_data->free_scbs);
  scbq_init(&p->waiting_scbs);

  for (i = 0; i < NUMBER(p->device_status); i++)
  {
    p->device_status[i].commands_sent = 0;
    p->device_status[i].flags = 0;
    p->device_status[i].active_cmds = 0;
    p->device_status[i].last_reset = 0;
  }

  /*
   * Request an IRQ for the board. Only allow sharing IRQs with PCI devices.
   */
#ifdef AIC7XXX_OLD_ISR_TYPE
  irq_flags = SA_INTERRUPT;
#endif
  if (p->chip_class != AIC_777x)
    irq_flags |= SA_SHIRQ;
  if (request_irq(p->irq, aic7xxx_isr, irq_flags, "aic7xxx", p) < 0)
  {
    printk(KERN_WARNING "aic7xxx: Couldn't register IRQ %d, ignoring.\n",
           p->irq);
    return (0);
  }

  /*
   * Set the SCSI Id, SXFRCTL0, SXFRCTL1, and SIMODE1, for both channels
   */
  if (p->bus_type == AIC_TWIN)
  {
    /*
     * The controller is gated to channel B after a chip reset; set
     * bus B values first.
     */
    outb(p->scsi_id_b, p->base + SCSIID);
    scsi_conf = inb(p->base + SCSICONF + 1);
    sxfrctl1 = inb(p->base + SXFRCTL1);
    outb((scsi_conf & (ENSPCHK | STIMESEL)) | (sxfrctl1 & STPWEN) | 
         ENSTIMER | ACTNEGEN, p->base + SXFRCTL1);
    outb(ENSELTIMO | ENSCSIRST | ENSCSIPERR, p->base + SIMODE1);
    if (p->flags & ULTRA_ENABLED)
    {
      outb(DFON | SPIOEN | FAST20, p->base + SXFRCTL0);
    }
    else
    {
      outb(DFON | SPIOEN, p->base + SXFRCTL0);
    }

    if ((scsi_conf & RESET_SCSI) && (aic7xxx_no_reset == 0))
    {
      /* Reset SCSI bus B. */
      if (aic7xxx_verbose)
        printk(KERN_INFO "aic7xxx: Resetting channel B\n");

      aic7xxx_reset_current_bus(p);
    }

    /* Select channel A */
    outb(SELNARROW, p->base + SBLKCTL);
  }

  outb(p->scsi_id, p->base + SCSIID);
  scsi_conf = inb(p->base + SCSICONF);
  sxfrctl1 = inb(p->base + SXFRCTL1);
  outb((scsi_conf & (ENSPCHK | STIMESEL)) | (sxfrctl1 & STPWEN) | 
       ENSTIMER | ACTNEGEN, p->base + SXFRCTL1);
  outb(ENSELTIMO | ENSCSIRST | ENSCSIPERR, p->base + SIMODE1);
  if (p->flags & ULTRA_ENABLED)
  {
    outb(DFON | SPIOEN | FAST20, p->base + SXFRCTL0);
  }
  else
  {
    outb(DFON | SPIOEN, p->base + SXFRCTL0);
  }

  if ((scsi_conf & RESET_SCSI) && (aic7xxx_no_reset == 0))
  {
    /* Reset SCSI bus A. */
    if (aic7xxx_verbose)
      printk(KERN_INFO "aic7xxx: Resetting channel A\n");

    aic7xxx_reset_current_bus(p);

    /*
     * Delay for the reset delay.
     */
    aic7xxx_delay(AIC7XXX_RESET_DELAY);
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
  if (p->flags & USE_DEFAULTS)
  {
    printk(KERN_INFO "aic7xxx: Host adapter BIOS disabled. Using default SCSI "
           "device parameters.\n");
    p->discenable = 0xFFFF;
  }
  else
  {
    p->discenable = ~((inb(p->base + DISC_DSB + 1) << 8) |
        inb(p->base + DISC_DSB));
  }

  for (i = 0; i < max_targets; i++)
  {
    if (p->flags & USE_DEFAULTS)
    {
      target_settings = 0;  /* 10 or 20 MHz depending on Ultra enable */
      p->needsdtr_copy |= (0x01 << i);
      p->needwdtr_copy |= (0x01 << i);
      if ((p->chip_class == AIC_786x) || (p->chip_class == AIC_788x))
        ultraenable |= (0x01 << i);
    }
    else
    {
      target_settings = inb(p->base + TARG_SCRATCH + i);
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
        /*
         * Clear the wide flag. When wide negotiation is successful,
         * we'll enable it.
         */
        target_settings &= 0x7F;
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
          case 0x40:  /* treat 10MHz as 10MHz without Ultra enabled */
            target_settings &= ~(0x70);
            break;
          default:
            break;
        }
      }
    }
    outb(target_settings, p->base + TARG_SCRATCH + i);
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
  outb(ultraenable & 0xFF, p->base + ULTRA_ENB);
  outb((ultraenable >> 8) & 0xFF, p->base + ULTRA_ENB + 1);

  /*
   * Set the number of available hardware SCBs.
   */
  outb(p->scb_data->maxhscbs, p->base + SCBCOUNT);

  /*
   * 2s compliment of maximum tag value.
   */
  i = p->scb_data->maxscbs;
  outb(-i & 0xFF, p->base + COMP_SCBCOUNT);

  /*
   * Allocate enough hardware scbs to handle the maximum number of
   * concurrent transactions we can have.  We have to make sure that
   * the allocated memory is contiguous memory.  The Linux kmalloc
   * routine should only allocate contiguous memory, but note that
   * this could be a problem if kmalloc() is changed.
   */
  if (p->scb_data->hscbs == NULL)
  {
    size_t array_size;
    unsigned int hscb_physaddr;

    array_size = p->scb_data->maxscbs * sizeof(struct aic7xxx_hwscb);
    p->scb_data->hscbs = kmalloc(array_size, GFP_ATOMIC);
    if (p->scb_data->hscbs == NULL)
    {
      printk("aic7xxx: Unable to allocate hardware SCB array; "
             "failing detection.\n");
      release_region(p->base, MAXREG - MINREG);
      free_irq(p->irq, p);
      return(0);
    }
    /* At least the control byte of each SCB needs to be 0. */
    memset(p->scb_data->hscbs, 0, array_size);

    /* Tell the sequencer where it can find the hardware SCB array. */
    hscb_physaddr = VIRT_TO_BUS(p->scb_data->hscbs);
    outb(hscb_physaddr & 0xFF, p->base + HSCB_ADDR);
    outb((hscb_physaddr >> 8) & 0xFF, p->base + HSCB_ADDR + 1);
    outb((hscb_physaddr >> 16) & 0xFF, p->base + HSCB_ADDR + 2);
    outb((hscb_physaddr >> 24) & 0xFF, p->base + HSCB_ADDR + 3);
  }

  /*
   * QCount mask to deal with broken aic7850s that sporadically get
   * garbage in the upper bits of their QCNT registers.
    */
  outb(p->qcntmask, p->base + QCNTMASK);

  /*
   * Set FIFO depth and command out count.  These are only used when
   * paging is enabled and should not be touched for AIC-7770 based
   * adapters; FIFODEPTH and CMDOUTCNT overlay SCSICONF and SCSICONF+1
   * which are used to control termination.
   */
  if (p->flags & PAGE_ENABLED)
  {
    outb(p->qfullcount, p->base + FIFODEPTH);
    outb(0, p->base + CMDOUTCNT);
  }

  /*
   * We don't have any waiting selections or disconnected SCBs.
   */
  outb(SCB_LIST_NULL, p->base + WAITING_SCBH);
  outb(SCB_LIST_NULL, p->base + DISCONNECTED_SCBH);

  /*
   * Message out buffer starts empty
   */
  outb(0, p->base + MSG_LEN);

  /*
   * Load the sequencer program, then re-enable the board -
   * resetting the AIC-7770 disables it, leaving the lights
   * on with nobody home. On the PCI bus you *may* be home,
   * but then your mailing address is dynamically assigned
   * so no one can find you anyway :-)
   */
  aic7xxx_loadseq(p);

  if (p->chip_class == AIC_777x)
  {
    outb(ENABLE, p->base + BCTL);  /* Enable the boards BUS drivers. */
  }

  /*
   * Unpause the sequencer before returning and enable
   * interrupts - we shouldn't get any until the first
   * command is sent to us by the high-level SCSI code.
   */
  unpause_sequencer(p, /* unpause_always */ TRUE);

  /*
   * Add it to our list of adapters.
   */
  p->next = first_aic7xxx;
  first_aic7xxx = p;

  return (found);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_chip_reset
 *
 * Description:
 *   Perform a chip reset on the aic7xxx SCSI controller.  The controller
 *   is paused upon return.
 *-F*************************************************************************/
static void
aic7xxx_chip_reset(struct aic7xxx_host *p)
{
  unsigned char hcntrl;
  int wait;

  /* Retain the IRQ type across the chip reset. */
  hcntrl = (inb(p->base + HCNTRL) & IRQMS) | INTEN;

  /*
   * For some 274x boards, we must clear the CHIPRST bit and pause
   * the sequencer. For some reason, this makes the driver work.
   */
  outb(PAUSE | CHIPRST, p->base + HCNTRL);

  /*
   * In the future, we may call this function as a last resort for
   * error handling.  Let's be nice and not do any unecessary delays.
   */
  wait = 1000;  /* 1 second (1000 * 1000 usec) */
  while ((wait > 0) && ((inb(p->base + HCNTRL) & CHIPRSTACK) == 0))
  {
    udelay(1000);  /* 1 msec = 1000 usec */
    wait = wait - 1;
  }

  if ((inb(p->base + HCNTRL) & CHIPRSTACK) == 0)
  {
    printk(KERN_INFO "aic7xxx: Chip reset not cleared; clearing manually.\n");
  }

  outb(hcntrl | PAUSE, p->base + HCNTRL);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_alloc
 *
 * Description:
 *   Allocate and initialize a host structure.  Returns NULL upon error
 *   and a pointer to a aic7xxx_host struct upon success.
 *-F*************************************************************************/
static struct aic7xxx_host *
aic7xxx_alloc(Scsi_Host_Template *sht, unsigned long base, unsigned long mbase,
    aha_chip_type chip_type, int flags, scb_data_type *scb_data)
{
  struct aic7xxx_host *p = NULL;
  struct Scsi_Host *host;

  /*
   * Allocate a storage area by registering us with the mid-level
   * SCSI layer.
   */
  host = scsi_register(sht, sizeof(struct aic7xxx_host));

  if (host != NULL)
  {
    p = (struct aic7xxx_host *) host->hostdata;
    memset(p, 0, sizeof(struct aic7xxx_host));
    p->host = host;

    if (scb_data != NULL)
    {
      /*
       * We are sharing SCB data areas; use the SCB data pointer
       * provided.
       */
      p->scb_data = scb_data;
      p->flags |= SHARED_SCBDATA;
    }
    else
    {
      /*
       * We are not sharing SCB data; allocate one.
       */
      p->scb_data = kmalloc(sizeof(scb_data_type), GFP_ATOMIC);
      if (p->scb_data != NULL)
      {
        memset(p->scb_data, 0, sizeof(scb_data_type));
        scbq_init (&p->scb_data->free_scbs);
      }
      else
      {
        /*
         * For some reason we don't have enough memory.  Free the
         * allocated memory for the aic7xxx_host struct, and return NULL.
         */
        scsi_unregister(host);
        p = NULL;
      }
    }
    if (p != NULL)
    {
      p->host_no = host->host_no;
      p->base = base;
      p->mbase = mbase;
      p->maddr = NULL;
      p->flags = flags;
      p->chip_type = chip_type;
      p->unpause = (inb(p->base + HCNTRL) & IRQMS) | INTEN;
      p->pause = p->unpause | PAUSE;
    }
  }
  return (p);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_free
 *
 * Description:
 *   Frees and releases all resources associated with an instance of
 *   the driver (struct aic7xxx_host *).
 *-F*************************************************************************/
static void
aic7xxx_free (struct aic7xxx_host *p)
{
  int i;

  /*
   * We should be careful in freeing the scb_data area.  For those
   * adapters sharing external SCB RAM(398x), there will be only one
   * scb_data area allocated.  The flag SHARED_SCBDATA indicates if
   * one adapter is sharing anothers SCB RAM.
   */
  if (!(p->flags & SHARED_SCBDATA))
  {
    /*
     * Free the allocated hardware SCB space.
     */
    if (p->scb_data->hscbs != NULL)
    {
      kfree(p->scb_data->hscbs);
    }
    /*
     * Free the driver SCBs.  These were allocated on an as-need
     * basis.
     */
    for (i = 0; i < p->scb_data->numscbs; i++)
    {
      kfree(p->scb_data->scb_array[i]);
    }
    /*
     * Free the hardware SCBs.
     */
    if (p->scb_data->hscbs != NULL)
    {
      kfree(p->scb_data->hscbs);
    }

    /*
     * Free the SCB data area.
     */
    kfree(p->scb_data);
  }
  /*
   * Free the instance of the device structure.
   */
  scsi_unregister(p->host);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_load_seeprom
 *
 * Description:
 *   Load the seeprom and configure adapter and target settings.
 *   Returns 1 if the load was successful and 0 otherwise.
 *-F*************************************************************************/
static int
load_seeprom (struct aic7xxx_host *p, unsigned char *sxfrctl1)
{
  int have_seeprom = 0;
  int i, max_targets;
  unsigned char target_settings, scsi_conf;
  unsigned short scarray[128];
  struct seeprom_config *sc = (struct seeprom_config *) scarray;

  if (aic7xxx_verbose)
  {
    printk(KERN_INFO "aic7xxx: Loading serial EEPROM...");
  }
  switch (p->chip_type)
  {
    case AIC_7770:  /* None of these adapters have seeproms. */
    case AIC_7771:
    case AIC_7855:
      break;

    case AIC_284x:
      have_seeprom = read_284x_seeprom(p, (struct seeprom_config *) scarray);
      break;

    case AIC_7850:  /* The 2910B is a 7850 with a seeprom. */
    case AIC_7861:
    case AIC_7870:
    case AIC_7871:
    case AIC_7872:
    case AIC_7874:
    case AIC_7881:
    case AIC_7882:
    case AIC_7884:
      have_seeprom = read_seeprom(p, p->chan_num * (sizeof(*sc)/2),
                                  scarray, sizeof(*sc)/2, C46);
      break;

    case AIC_7860:  /* Motherboard Ultra controllers might have RAID port. */
    case AIC_7880:
      have_seeprom = read_seeprom(p, 0, scarray, sizeof(*sc)/2, C46);
      if (!have_seeprom)
      {
        have_seeprom = read_seeprom(p, 0, scarray, sizeof(scarray)/2, C56_66);
      }
      break;

    case AIC_7873:  /* The 3985 adapters use the 93c56 serial EEPROM. */
    case AIC_7883:
      have_seeprom = read_seeprom(p, p->chan_num * (sizeof(*sc)/2),
                                  scarray, sizeof(scarray)/2, C56_66);
      break;

    default:
      break;
  }

  if (!have_seeprom)
  {
    if (aic7xxx_verbose)
    {
      printk("\naic7xxx: No SEEPROM available; using defaults.\n");
    }
    p->flags |= USE_DEFAULTS;
  }
  else
  {
    if (aic7xxx_verbose)
    {
      printk("done\n");
    }
    p->flags |= HAVE_SEEPROM;

    /*
     * Update the settings in sxfrctl1 to match the termination settings.
     */
    *sxfrctl1 = 0;

    /*
     * First process the settings that are different between the VLB
     * and PCI adapter seeproms.
     */
    if (p->chip_class == AIC_777x)
    {
      /* VLB adapter seeproms */
      if (sc->bios_control & CF284XEXTEND)
        p->flags |= EXTENDED_TRANSLATION;

      if (sc->adapter_control & CF284XSTERM)
        *sxfrctl1 |= STPWEN;
      /*
       * The 284x SEEPROM doesn't have a max targets field.  We
       * set it to 16 to make sure we take care of the 284x-wide
       * adapters.  For narrow adapters, going through the extra
       * 8 target entries will not cause any harm since they will
       * will not be used.
       *
       * XXX - We should probably break out the bus detection
       *       from the register function so we can use it here
       *       to tell us how many targets there really are.
       */
      max_targets = 16;
    }
    else
    {
      /* PCI adapter seeproms */
      if (sc->bios_control & CFEXTEND)
        p->flags |= EXTENDED_TRANSLATION;

      if (sc->adapter_control & CFSTERM)
        *sxfrctl1 |= STPWEN;

      /* Limit to 16 targets just in case. */
      max_targets = MIN(sc->max_targets & CFMAXTARG, 16);
    }

    for (i = 0; i < max_targets; i++)
    {
      target_settings = (sc->device_flags[i] & CFXFER) << 4;
      if (sc->device_flags[i] & CFSYNCH)
        target_settings |= SOFS;
      if (sc->device_flags[i] & CFWIDEB)
        target_settings |= WIDEXFER;
      if (sc->device_flags[i] & CFDISC)
        p->discenable |= (0x01 << i);
      outb(target_settings, p->base + TARG_SCRATCH + i);
    }
    outb(~(p->discenable & 0xFF), p->base + DISC_DSB);
    outb(~((p->discenable >> 8) & 0xFF), p->base + DISC_DSB + 1);

    p->scsi_id = sc->brtime_id & CFSCSIID;

    scsi_conf = (p->scsi_id & 0x7);
    if (sc->adapter_control & CFSPARITY)
      scsi_conf |= ENSPCHK;
    /*
     * The 7850 controllers with a seeprom, do not honor the CFRESETB
     * flag in the seeprom.  Assume that we want to reset the SCSI bus.
     */
    if ((sc->adapter_control & CFRESETB) || (p->chip_class == AIC_7850))
      scsi_conf |= RESET_SCSI;

    if ((p->chip_class == AIC_786x) || (p->chip_class == AIC_788x))
    {
      /*
       * We allow the operator to override ultra enable through
       * the boot prompt.
       */
      if (!(sc->adapter_control & CFULTRAEN) && (aic7xxx_enable_ultra == 0))
      {
        /* Treat us as a non-ultra card */
        p->flags &= ~ULTRA_ENABLED;
      }
    }

    /* Set the host ID */
    outb(scsi_conf, p->base + SCSICONF);
    /* In case we are a wide card */
    outb(p->scsi_id, p->base + SCSICONF + 1);

    if (p->chip_class != AIC_777x)
    {
      /*
       * Update the settings in sxfrctl1 to match the termination
       * settings.
       */
      *sxfrctl1 = 0;
      configure_termination(p, sxfrctl1, sc->adapter_control,
        (unsigned char) sc->max_targets & CFMAXTARG);
    }
  }
  return (have_seeprom);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_detect
 *
 * Description:
 *   Try to detect and register an Adaptec 7770 or 7870 SCSI controller.
 *
 * XXX - This should really be called aic7xxx_probe().  A sequence of
 *       probe(), attach()/detach(), and init() makes more sense than
 *       one do-it-all function.  This may be useful when (and if) the
 *       mid-level SCSI code is overhauled.
 *-F*************************************************************************/
int
aic7xxx_detect(Scsi_Host_Template *template)
{
  int found = 0;
#if !defined(__sparc_v9__) && !defined(__powerpc__)
  aha_status_type adapter_bios;
  unsigned char hcntrl, hostconf, irq = 0;
  int slot, base;
#endif
  aha_chip_class_type chip_class;
  aha_chip_type chip_type;
  int chan_num = 0;
  unsigned char sxfrctl1, sblkctl;
  int i;
  struct aic7xxx_host *p;

  template->proc_dir = &proc_scsi_aic7xxx;
  template->name = aic7xxx_info(NULL);
  template->sg_tablesize = AIC7XXX_MAX_SG;

#if !defined(__sparc_v9__) && !defined(__powerpc__)
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

    chip_type = aic7xxx_probe(slot, base + HID0, &(adapter_bios));
    if (chip_type != AIC_NONE)
    {

      switch (chip_type)
      {
        case AIC_7770:
        case AIC_7771:
          printk("aic7xxx: <%s> at EISA %d\n",
                 board_names[chip_type], slot);
          break;
        case AIC_284x:
          printk("aic7xxx: <%s> at VLB %d\n",
                 board_names[chip_type], slot);
          break;
        default:
          break;
      }

      /*
       * Pause the card preserving the IRQ type.  Allow the operator
       * to override the IRQ trigger.
       */
      if (aic7xxx_irq_trigger == 1)
        hcntrl = IRQMS;  /* Level */
      else if (aic7xxx_irq_trigger == 0)
        hcntrl = 0;  /* Edge */
      else
        hcntrl = inb(base + HCNTRL) & IRQMS;  /* Default */
      outb(hcntrl | PAUSE, base + HCNTRL);

      p = aic7xxx_alloc(template, base, 0, chip_type, 0, NULL);
      if (p == NULL)
      {
        printk(KERN_WARNING "aic7xxx: Unable to allocate device space.\n");
        continue;
      }
      aic7xxx_chip_reset(p);

      irq = inb(INTDEF + base) & 0x0F;
      switch (irq)
      {
        case 9:
        case 10:
        case 11:
        case 12:
        case 14:
        case 15:
          break;

        default:
          printk(KERN_WARNING "aic7xxx: Host adapter uses unsupported IRQ "
          "level %d, ignoring.\n", irq);
          irq = 0;
          aic7xxx_free(p);
          break;
      }

      if (irq != 0)
      {
        p->irq = irq & 0x0F;
        p->chip_class = AIC_777x;
#ifdef AIC7XXX_PAGE_ENABLE
        p->flags |= PAGE_ENABLED;
#endif
        p->instance = found;
        if (aic7xxx_extended)
        {
          p->flags |= EXTENDED_TRANSLATION;
        }

        switch (p->chip_type)
        {
          case AIC_7770:
          case AIC_7771:
          {
            unsigned char biosctrl = inb(p->base + HA_274_BIOSCTRL);

            /*
             * Get the primary channel information.  Right now we don't
             * do anything with this, but someday we will be able to inform
             * the mid-level SCSI code which channel is primary.
             */
            if (biosctrl & CHANNEL_B_PRIMARY)
            {
              p->flags |= FLAGS_CHANNEL_B_PRIMARY;
            }

            if ((biosctrl & BIOSMODE) == BIOSDISABLED)
            {
              p->flags |= USE_DEFAULTS;
            }
            break;
          }

          case AIC_284x:
            if (!load_seeprom(p, &sxfrctl1))
            {
              if (aic7xxx_verbose)
                printk(KERN_INFO "aic7xxx: SEEPROM not available.\n");
            }
            break;

          default:  /* Won't get here. */
            break;
        }
        printk(KERN_INFO "aic7xxx: BIOS %sabled, IO Port 0x%lx, IRQ %d (%s), ",
               (p->flags & USE_DEFAULTS) ? "dis" : "en", p->base, p->irq,
               (p->pause & IRQMS) ? "level sensitive" : "edge triggered");
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
        sblkctl = inb(p->base + SBLKCTL) ^ AUTOFLUSHDIS;
        outb(sblkctl, p->base + SBLKCTL);
        if (inb(p->base + SBLKCTL) == sblkctl)
        {
          /*
           * We detected a Rev E board, we allow paging on this board.
           */
          printk("Revision >= E\n");
          outb(sblkctl & ~AUTOFLUSHDIS, base + SBLKCTL);
        }
        else
        {
          /* Do not allow paging. */
          p->flags &= ~PAGE_ENABLED;
          printk("Revision <= C\n");
        }

        if (aic7xxx_verbose)
          printk(KERN_INFO "aic7xxx: Extended translation %sabled.\n",
                 (p->flags & EXTENDED_TRANSLATION) ? "en" : "dis");

        /*
         * Set the FIFO threshold and the bus off time.
         */
        hostconf = inb(p->base + HOSTCONF);
        outb(hostconf & DFTHRSH, p->base + BUSSPD);
        outb((hostconf << 2) & BOFF, p->base + BUSTIME);

        /*
         * Try to initialize the card and register it with the kernel.
         */
        if (aic7xxx_register(template, p))
        {
          /*
           * We successfully found a board and registered it.
           */
          found = found + 1;
        }
        else
        {
          /*
           * Something went wrong; release and free all resources.
           */
          aic7xxx_free(p);
        }
      }
    }
  }
#endif /* __sparc_v9__ or __powerpc__ */

#ifdef CONFIG_PCI
  /*
   * PCI-bus probe.
   */
  if (pci_present())
  {
    struct
    {
      unsigned short      vendor_id;
      unsigned short      device_id;
      aha_chip_type       chip_type;
      aha_chip_class_type chip_class;
    } const aic7xxx_pci_devices[] = {
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7850, AIC_7850, AIC_785x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7855, AIC_7855, AIC_785x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7860, AIC_7860, AIC_786x},
      {PCI_VENDOR_ID_ADAPTEC, PCI_DEVICE_ID_ADAPTEC_7861, AIC_7861, AIC_786x},
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

    int error, flags;
    unsigned short index = 0;
    unsigned char ultra_enb = 0;
    unsigned int  devconfig, class_revid;
    scb_data_type *shared_scb_data = NULL;
    char rev_id[] = {'B', 'C', 'D'};
    struct pci_dev *pdev = NULL;
    unsigned long iobase, mbase;
    unsigned int irq;

    for (i = 0; i < NUMBER(aic7xxx_pci_devices); i++)
      while ((pdev = pci_find_device(aic7xxx_pci_devices[i].vendor_id,
                                     aic7xxx_pci_devices[i].device_id,
				    pdev)))
      {
        chip_class = aic7xxx_pci_devices[i].chip_class;
        chip_type = aic7xxx_pci_devices[i].chip_type;
        chan_num = 0;
        flags = 0;
        switch (aic7xxx_pci_devices[i].chip_type)
        {
          case AIC_7855:
            flags |= USE_DEFAULTS;
            break;

          case AIC_7872:  /* 3940 */
          case AIC_7882:  /* 3940-Ultra */
            flags |= MULTI_CHANNEL;
            chan_num = number_of_3940s & 0x1;  /* Has 2 controllers */
            number_of_3940s++;
            break;

          case AIC_7873:  /* 3985 */
          case AIC_7883:  /* 3985-Ultra */
            chan_num = number_of_3985s;  /* Has 3 controllers */
            flags |= MULTI_CHANNEL;
            number_of_3985s++;
            if (number_of_3985s == 3)
            {
              number_of_3985s = 0;
              shared_scb_data = NULL;
            }
            break;

          default:
            break;
        }

        /*
         * Read sundry information from PCI BIOS.
         */
	iobase = pdev->base_address[0];
	mbase = pdev->base_address[1];
	irq = pdev->irq;
        error = pci_read_config_dword(pdev, DEVCONFIG, &devconfig);
        error += pci_read_config_dword(pdev, CLASS_PROGIF_REVID, &class_revid);
        printk("aic7xxx: <%s> at PCI %d\n",
               board_names[chip_type], PCI_SLOT(pdev->devfn));

        /*
         * The first bit (LSB) of PCI_BASE_ADDRESS_0 is always set, so
         * we mask it off.
         */
	iobase &= PCI_BASE_ADDRESS_IO_MASK;
        p = aic7xxx_alloc(template, iobase, mbase, chip_type, flags, shared_scb_data);
        if(p) {
          unsigned short pci_command;

          /* Enable bus mastering since this thing must do DMA. */
          pci_read_config_word(pdev, PCI_COMMAND, &pci_command);
          pci_command |= PCI_COMMAND_MASTER;
#ifdef __powerpc__
          /* Enable I/O and memory-space access */
          pci_command |= PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
#endif
          pci_write_config_word(pdev, PCI_COMMAND, pci_command);
        } else {
          printk(KERN_WARNING "aic7xxx: Unable to allocate device space.\n");
          continue;
        }

        /* Remember to set the channel number, irq, and chip class. */
        p->chan_num = chan_num;
        p->irq = irq;
        p->chip_class = chip_class;
#ifdef AIC7XXX_PAGE_ENABLE
        p->flags |= PAGE_ENABLED;
#endif
        p->instance = found;

          /*
           * Remember how the card was setup in case there is no seeprom.
           */
          p->scsi_id = inb(p->base + SCSIID) & OID;
          if ((p->chip_class == AIC_786x) || (p->chip_class == AIC_788x))
          {
            p->flags |= ULTRA_ENABLED;
            ultra_enb = inb(p->base + SXFRCTL1) & FAST20;
          }
	  sxfrctl1 = inb(p->base + SXFRCTL1) & STPWEN;

          aic7xxx_chip_reset(p);

#ifdef AIC7XXX_USE_EXT_SCBRAM
          if (devconfig & RAMPSM)
          {
            printk(KERN_INFO "aic7xxx: External RAM detected; enabling RAM "
                   "access.\n");
            /*
             * XXX - Assume 9 bit SRAM and enable parity checking.
             */
            devconfig |= EXTSCBPEN;

            /*
             * XXX - Assume fast SRAM and only enable 2 cycle access if we
             *       are sharing the SRAM across multiple adapters (398x).
             */
            if ((devconfig & MPORTMODE) == 0)
            {
              devconfig |= EXTSCBTIME;
            }
            devconfig &= ~SCBRAMSEL;
            pcibios_write_config_dword(pci_bus, pci_device_fn,
                                       DEVCONFIG, devconfig);
          }
#endif

          if ((p->flags & USE_DEFAULTS) == 0)
          {
            load_seeprom(p, &sxfrctl1);
          }

          /*
           * Take the LED out of diagnostic mode
           */
          sblkctl = inb(p->base + SBLKCTL);
          outb((sblkctl & ~(DIAGLEDEN | DIAGLEDON)), p->base + SBLKCTL);

          /*
           * We don't know where this is set in the SEEPROM or by the
           * BIOS, so we default to 100%.
           */
          outb(DFTHRSH_100, p->base + DSPCISTATUS);

          if (p->flags & USE_DEFAULTS)
          {
            int j;
            /*
             * Default setup; should only be used if the adapter does
             * not have a SEEPROM.
             */
            /*
             * Check the target scratch area to see if someone set us
             * up already.  We are previously set up if the scratch
             * area contains something other than all zeroes and ones.
             */
            for (j = TARG_SCRATCH; j < 0x60; j++)
            {
              if (inb(p->base + j) != 0x00)      /* Check for all zeroes. */
                break;
            }
            if (j == TARG_SCRATCH)
            {
              for (j = TARG_SCRATCH; j < 0x60; j++)
              {
                if (inb(p->base + 1) != 0xFF)    /* Check for all ones. */
                  break;
              }
            }
            if ((j != 0x60) && (p->scsi_id != 0))
            {
              p->flags &= ~USE_DEFAULTS;
              if (aic7xxx_verbose)
              {
                printk(KERN_INFO "aic7xxx: Using leftover BIOS values.\n");
              }
            }
            else
            {
              if (aic7xxx_verbose)
              {
                printk(KERN_INFO "aic7xxx: No BIOS found; using default "
                       "settings.\n");
              }
              /*
               * Assume only one connector and always turn on
               * termination.
               */
              sxfrctl1 = STPWEN;
              p->scsi_id = 7;
            }
            outb((p->scsi_id & HSCSIID) | ENSPCHK | RESET_SCSI,
                 p->base + SCSICONF);
            /* In case we are a wide card. */
            outb(p->scsi_id, p->base + SCSICONF + 1);
            if ((ultra_enb == 0) && ((p->flags & USE_DEFAULTS) == 0))
            {
              /*
               * If there wasn't a BIOS or the board wasn't in this mode
               * to begin with, turn off Ultra.
               */
              p->flags &= ~ULTRA_ENABLED;
            }
          }

          /*
           * Print some additional information about the adapter.
           */
          printk(KERN_INFO "aic7xxx: BIOS %sabled, IO Port 0x%lx, "
                 "IO Mem 0x%lx, IRQ %x",
                 (p->flags & USE_DEFAULTS) ? "dis" : "en",
                 p->base, p->mbase, p->irq);
          if ((class_revid & DEVREVID) < 3)
          {
            printk(", Revision %c", rev_id[class_revid & DEVREVID]);
          }
          printk("\n");

          if (aic7xxx_extended)
            p->flags |= EXTENDED_TRANSLATION;

          if (aic7xxx_verbose)
            printk(KERN_INFO "aic7xxx: Extended translation %sabled.\n",
                   (p->flags & EXTENDED_TRANSLATION) ? "en" : "dis");

          /*
           * Put our termination setting into sxfrctl1 now that the
           * generic initialization is complete.
           */
          sxfrctl1 |= inb(p->base + SXFRCTL1);
          outb(sxfrctl1, p->base + SXFRCTL1);

          if (aic7xxx_register(template, p) == 0)
          {
            aic7xxx_free(p);
          }
          else
          {
            found = found + 1;

#ifdef AIC7XXX_USE_EXT_SCBRAM
            /*
             * Set the shared SCB data once we've successfully probed a
             * 398x adapter.
             *
             * Note that we can only do this if the use of external
             * SCB RAM is enabled.
             */
            if ((p->chip_type == AIC_7873) || (p->chip_type == AIC_7883))
            {
              if (shared_scb_data == NULL)
              {
                shared_scb_data = p->scb_data;
              }
            }
#endif
          }

          index++;
      }  /* Found an Adaptec PCI device. */
  }
#endif CONFIG_PCI

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
  unsigned short mask;
  struct aic7xxx_hwscb *hscb;

  mask = (0x01 << TARGET_INDEX(cmd));
  hscb = scb->hscb;

  /*
   * Setup the control byte if we need negotiation and have not
   * already requested it.
   */
  if (p->discenable & mask)
  {
    hscb->control |= DISCENB;
#ifdef AIC7XXX_TAGGED_QUEUEING
    if (cmd->device->tagged_queue)
    {
      cmd->tag = hscb->tag;
      p->device_status[TARGET_INDEX(cmd)].commands_sent++;
      if (p->device_status[TARGET_INDEX(cmd)].commands_sent < 75)
      {
        hscb->control |= MSG_SIMPLE_Q_TAG;
      }
      else
      {
        hscb->control |= MSG_ORDERED_Q_TAG;
        p->device_status[TARGET_INDEX(cmd)].commands_sent = 0;
      }
    }
#endif  /* Tagged queueing */
  }

  if ((p->needwdtr & mask) && !(p->wdtr_pending & mask))
  {
    p->wdtr_pending |= mask;
    hscb->control |= MK_MESSAGE;
    scb->flags |= SCB_MSGOUT_WDTR;
#if 0
    printk("scsi%d: Sending WDTR request to target %d.\n",
           p->host_no, cmd->target);
#endif
  }
  else
  {
    if ((p->needsdtr & mask) && !(p->sdtr_pending & mask))
    {
      p->sdtr_pending |= mask;
      hscb->control |= MK_MESSAGE;
      scb->flags |= SCB_MSGOUT_SDTR;
#if 0
      printk("scsi%d: Sending SDTR request to target %d.\n",
             p->host_no, cmd->target);
#endif
    }
  }
#if 0
  printk("aic7xxx: (build_scb) Target %d, cmd(0x%x) size(%u) wdtr(0x%x) "
         "mask(0x%x).\n",
	 cmd->target, cmd->cmnd[0], cmd->cmd_len, p->needwdtr, mask);
#endif
  hscb->target_channel_lun = ((cmd->target << 4) & 0xF0) |
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
   *
   * No longer is that an issue, I've "big-endian'ified" this driver. -DaveM
   */
  hscb->SCSI_cmd_length = cmd->cmd_len;
  hscb->SCSI_cmd_pointer = cpu_to_le32(VIRT_TO_BUS(cmd->cmnd));

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
      scb->sg_list[i].address = cpu_to_le32(VIRT_TO_BUS(sg[i].address));
      scb->sg_list[i].length = cpu_to_le32((unsigned int) sg[i].length);
    }
    hscb->SG_list_pointer = cpu_to_le32(VIRT_TO_BUS(scb->sg_list));
    hscb->SG_segment_count = cmd->use_sg;
    scb->sg_count = hscb->SG_segment_count;

    /* Copy the first SG into the data pointer area. */
    hscb->data_pointer = scb->sg_list[0].address;
    hscb->data_count = scb->sg_list[0].length | cpu_to_le32(SCB_LIST_NULL << 24);
#if 0
    printk("aic7xxx: (build_scb) SG segs(%d), length(%u), sg[0].length(%d).\n",
           cmd->use_sg, aic7xxx_length(cmd, 0), le32_to_cpu(hscb->data_count));
#endif
  }
  else
  {
#if 0
  printk("aic7xxx: (build_scb) Creating scatterlist, addr(0x%lx) length(%d).\n",
	(unsigned long) cmd->request_buffer, cmd->request_bufflen);
#endif
    if (cmd->request_bufflen)
    {
      hscb->SG_segment_count = 1;
      scb->sg_count = 1;
      scb->sg_list[0].address = cpu_to_le32(VIRT_TO_BUS(cmd->request_buffer));
      scb->sg_list[0].length = cpu_to_le32(cmd->request_bufflen);
      hscb->SG_list_pointer = cpu_to_le32(VIRT_TO_BUS(&scb->sg_list[0]));
      hscb->data_count = scb->sg_list[0].length | cpu_to_le32(SCB_LIST_NULL << 24);
      hscb->data_pointer = cpu_to_le32(VIRT_TO_BUS(cmd->request_buffer));
    }
    else
    {
      hscb->SG_segment_count = 0;
      scb->sg_count = 0;
      hscb->SG_list_pointer = 0;
      hscb->data_pointer = 0;
      hscb->data_count = cpu_to_le32(SCB_LIST_NULL << 24);
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

  if (p->device_status[TARGET_INDEX(cmd)].active_cmds
      > cmd->device->queue_depth)
  {
    printk(KERN_WARNING "(scsi%d:%d:%d) Commands queued exceeds queue depth\n",
           p->host_no, cmd->target, cmd->channel);
  }
  scb = aic7xxx_allocate_scb(p);
  if (scb == NULL)
  {
    panic("aic7xxx: (aic7xxx_queue) Couldn't find a free SCB.\n");
  }
  else
  {
    scb->cmd = cmd;
    aic7xxx_position(cmd) = scb->hscb->tag;
#if 0
    debug_scb(scb);
#endif;

    /*
     * Construct the SCB beforehand, so the sequencer is
     * paused a minimal amount of time.
     */
    aic7xxx_buildscb(p, cmd, scb);

#if 0
    if (scb != (p->scb_data->scb_array[scb->hscb->tag]))
    {
      printk("aic7xxx: (queue) Address of SCB by position does not match SCB "
             "address.\n");
    }
    printk("aic7xxx: (queue) SCB pos(%d) cmdptr(0x%x) state(%d) freescb(0x%x)\n",
	   scb->hscb->tag, (unsigned int) scb->cmd,
	   scb->flags, (unsigned int) p->free_scb);
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

    scb->flags |= SCB_ACTIVE | SCB_WAITINGQ;

    save_flags(processor_flags);
    cli();
    scbq_insert_tail(&p->waiting_scbs, scb);
    if ((p->flags & (IN_ISR | IN_TIMEOUT)) == 0)
    {
      aic7xxx_run_waiting_queues(p);
    }

    restore_flags(processor_flags);
#if 0
    printk("aic7xxx: (queue) After - cmd(0x%lx) scb->cmd(0x%lx) pos(%d).\n",
           (long) cmd, (long) scb->cmd, scb->hscb->tag);
#endif;
  }
  return (0);
}

/*+F*************************************************************************
 * Function:
 *   aic7xxx_bus_device_reset
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
  struct aic7xxx_scb   *scb;
  struct aic7xxx_hwscb *hscb;
  unsigned char bus_state;
  int result = -1;
  char channel;

  scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
  hscb = scb->hscb;

  /*
   * Ensure that the card doesn't do anything behind our back.
   * Also make sure that we didn't just miss an interrupt that
   * could affect this abort/reset.
   */
  pause_sequencer(p);
  while (inb(p->base + INTSTAT) & INT_PEND);
  {
    aic7xxx_isr(p->irq, (void *) p, (void *) NULL);
    pause_sequencer(p);
  } 
  if ((cmd != scb->cmd) || ((scb->flags & SCB_ACTIVE) == 0))
  {
    result = SCSI_RESET_NOT_RUNNING;
    unpause_sequencer(p, /* unpause_always */ TRUE);
    return(result);
  }


  printk(KERN_WARNING "(scsi%d:%d:%d) Abort_reset, scb flags 0x%x, ",
         p->host_no, TC_OF_SCB(scb), scb->flags);
  bus_state = inb(p->base + LASTPHASE);

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
      /*
       * We're not in a valid phase, so assume we're idle.
       */
      printk("while idle, LASTPHASE = 0x%x, ", bus_state);
      break;
  }
  printk("SCSISIGI 0x%x, SEQADDR 0x%x, SSTAT0 0x%x, SSTAT1 0x%x\n",
         inb(p->base + SCSISIGI),
         inb(p->base + SEQADDR0) | (inb(p->base + SEQADDR1) << 8),
         inb(p->base + SSTAT0), inb(p->base + SSTAT1));

  channel = hscb->target_channel_lun & SELBUSB ? 'B': 'A';
  /*
   * Determine our course of action.
   */
  if (scb->flags & SCB_ABORT)
  {
    /*
     * Been down this road before; do a full bus reset.
     */
    scb->flags |= SCB_RECOVERY_SCB;
    unpause_sequencer(p, /* unpause_always */ TRUE);
    result = -1;
  }
#if 0
  else if (hscb->control & TAG_ENB)
    {
      /*
       * We could be starving this command; try sending and ordered tag
       * command to the target we come from.
       */
      scb->flags |= SCB_SENTORDEREDTAG | SCB_RECOVERY_SCB;
      p->orderedtag = p->orderedtag | 0xFF;
      result = SCSI_RESET_PENDING;
      unpause_sequencer(p, /* unpause_always */ TRUE);
      printk(KERN_WARNING "scsi%d: Abort_reset, odered tag queued.\n",
             p->host_no);
    }
#endif
  else
  {
    unsigned char active_scb_index, saved_scbptr;
    struct aic7xxx_scb *active_scb;

    /*
     * Send an Abort Message:
     * The target that is holding up the bus may not be the same as
     * the one that triggered this timeout (different commands have
     * different timeout lengths).  Our strategy here is to queue an
     * abort message to the timed out target if it is disconnected.
     * Otherwise, if we have an active target we stuff the message buffer
     * with an abort message and assert ATN in the hopes that the target
     * will let go of the bus and go to the mesgout phase.  If this
     * fails, we'll get another timeout a few seconds later which will
     * attempt a bus reset.
     */
    saved_scbptr = inb(p->base + SCBPTR);
    active_scb_index = inb(p->base + SCB_TAG);
    active_scb = p->scb_data->scb_array[active_scb_index];

    if (bus_state != P_BUSFREE)
    {
      if (active_scb_index >= p->scb_data->numscbs)
      {
        /*
         * Perform a bus reset.
         *
         * XXX - We want to queue an abort for the timedout SCB
         *       instead.
         */
        result = -1;
        printk(KERN_WARNING "scsi%d: Invalid SCB ID %d is active, "
               "SCB flags = 0x%x.\n", p->host_no, scb->hscb->tag, scb->flags);
      }
      else
      {
        /* Send the abort message to the active SCB. */
        outb(1, p->base + MSG_LEN);
        if (active_scb->hscb->control & TAG_ENB)
        {
          outb(MSG_ABORT_TAG, p->base + MSG_OUT);
        }
        else
        {
          outb(MSG_ABORT, p->base + MSG_OUT);
        }
        outb(bus_state | ATNO, p->base + SCSISIGO);
        printk(KERN_WARNING "scsi%d: abort message in message buffer\n",
               p->host_no);
        active_scb->flags |= SCB_ABORT | SCB_RECOVERY_SCB;
        if (active_scb != scb)
        {
          /*
           * XXX - We would like to increment the timeout on scb, but
           *       access to that routine is denied because it is hidden
           *       in scsi.c.  If we were able to do this, it would give
           *       scb a new lease on life.
           */
          result = SCSI_RESET_PENDING;
          aic7xxx_error(active_scb->cmd) = DID_RESET;
        }
        else
        {
          aic7xxx_error(scb->cmd) = DID_RESET;
          result = SCSI_RESET_PENDING;
        }
        unpause_sequencer(p, /* unpause_always */ TRUE);
      }
    }
    else
    {
      unsigned char hscb_index, linked_next;
      int disconnected;

      disconnected = FALSE;
      hscb_index = aic7xxx_find_scb(p, scb);
      if (hscb_index == SCB_LIST_NULL)
      {
        disconnected = TRUE;
        linked_next = (le32_to_cpu(scb->hscb->data_count) >> 24) & 0xFF;
      }
      else
      {
        outb(hscb_index, p->base + SCBPTR);
        if (inb(p->base + SCB_CONTROL) & DISCONNECTED)
        {
          disconnected = TRUE;
        }
        linked_next = inb(p->base + SCB_LINKED_NEXT);
      }
      if (disconnected)
      {
        /*
         * Simply set the ABORT_SCB control bit and preserve the
         * linked next pointer.
         */
        scb->hscb->control |= ABORT_SCB | MK_MESSAGE;
        scb->hscb->data_count &= cpu_to_le32(~0xFF000000);
        scb->hscb->data_count |= cpu_to_le32(linked_next << 24);
        if ((p->flags & PAGE_ENABLED) == 0)
        {
          scb->hscb->control &= ~DISCONNECTED;
        }
        scb->flags |= SCB_QUEUED_ABORT | SCB_ABORT | SCB_RECOVERY_SCB;
        if (hscb_index != SCB_LIST_NULL)
        {
          unsigned char scb_control;

          scb_control = inb(p->base + SCB_CONTROL);
          outb(scb_control | MK_MESSAGE| ABORT_SCB, p->base + SCB_CONTROL);
        }
        /*
         * Actually requeue this SCB in case we can select the
         * device before it reconnects.  If the transaction we
         * want to abort is not tagged, unbusy it first so that
         * we don't get held back from sending the command.
         */
        if ((scb->hscb->control & TAG_ENB) == 0)
        {
          unsigned char target;
          int lun;

          target = scb->cmd->target;
          lun = scb->cmd->lun;
          aic7xxx_search_qinfifo(p, target, channel, lun, SCB_LIST_NULL,
              0, /* requeue */ TRUE);
        }
        printk(KERN_WARNING "(scsi%d:%d:%d) Queueing an Abort SCB.\n",
               p->host_no, TC_OF_SCB(scb));
        scbq_insert_head(&p->waiting_scbs, scb);
        scb->flags |= SCB_WAITINGQ;
        outb(saved_scbptr, p->base + SCBPTR);
        if ((p->flags & IN_ISR) == 0)
        {
          /*
           * Processing the waiting queue may unpause us.
           */
          aic7xxx_run_waiting_queues(p);
          /*
           * If we are using AAP, aic7xxx_run_waiting_queues() will not
           * unpause us, so ensure we are unpaused.
           */
          unpause_sequencer(p, /*unpause_always*/ FALSE);
        }
        else
        {
          unpause_sequencer(p, /*unpause_always*/ TRUE);
        }
        result = SCSI_RESET_PENDING;
      }
      else
      {
        scb->flags |= SCB_RECOVERY_SCB;
        unpause_sequencer(p, /* unpause_always */ TRUE);
        result = -1;
      }
    }
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
  unsigned long processor_flags;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
  base = p->base;

  save_flags(processor_flags);
  cli();

#ifdef AIC7XXX_DEBUG_ABORT
  if (scb != NULL)
  {
    printk("(scsi%d:%d:%d) Aborting scb %d, flags 0x%x\n",
           p->host_no, TC_OF_SCB(scb), scb->hscb->tag, scb->flags);
  }
  else
  {
    printk("aic7xxx: Abort called with no SCB for cmd.\n");
  }
#endif

  if (p->flags & IN_TIMEOUT)
  {
    /*
     * We've already started a recovery operation.
     */
    if ((scb->flags & SCB_RECOVERY_SCB) == 0)
    {
      restore_flags(processor_flags);
      return (SCSI_ABORT_PENDING);
    }
    else
    {
      /*
       * This is the second time we've tried to abort the recovery
       * SCB.  We want the mid-level SCSI code to call the reset
       * function to reset the SCSI bus.
       */
      restore_flags(processor_flags);
      return (SCSI_ABORT_NOT_RUNNING);
    }
  }
  if (cmd->serial_number != cmd->serial_number_at_timeout)
  {
    result = SCSI_ABORT_NOT_RUNNING;
  }
  else if (scb == NULL)
  {
    result = SCSI_ABORT_NOT_RUNNING;
  }
  else if ((scb->cmd != cmd) || (!(scb->flags & SCB_ACTIVE)))
  {
    result = SCSI_ABORT_NOT_RUNNING;
  }
  else
  {
    /*
     * XXX - Check use of IN_TIMEOUT to see if we're Doing the
     *       Right Thing with it.
     */
    p->flags |= IN_TIMEOUT;
    result = aic7xxx_bus_device_reset(p, scb->cmd);
    switch (result)
    {
      case SCSI_RESET_NOT_RUNNING:
        p->flags &= ~IN_TIMEOUT;
        result = SCSI_ABORT_NOT_RUNNING;
        break;
      case SCSI_RESET_PENDING:
        result = SCSI_ABORT_PENDING;
        break;
      default:
        p->flags &= ~IN_TIMEOUT;
        result = SCSI_ABORT_SNOOZE;
        break;
     }
  }
  restore_flags(processor_flags);
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
  unsigned long base;
  int    found, tindex, min_target, max_target;
  int    result = -1;
  char   channel = 'A';
  unsigned long processor_flags;

  p = (struct aic7xxx_host *) cmd->host->hostdata;
  scb = (p->scb_data->scb_array[aic7xxx_position(cmd)]);
  base = p->base;
  channel = cmd->channel ? 'B': 'A';
  tindex = TARGET_INDEX(cmd);

#if 0   /* AIC7XXX_DEBUG_ABORT */
  if (scb != NULL)
  {
    printk("(scsi%d:%d:%d) Reset called, scb %d, flags 0x%x\n",
           p->host_no, TC_OF_SCB(scb), scb->hscb->tag, scb->flags);
  }
  else
  {
    printk("aic7xxx: Reset called with no SCB for cmd.\n");
  }
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

  if (p->flags & IN_TIMEOUT)
  {
    /*
     * We've already started a recovery operation.
     */
    if ((scb->flags & SCB_RECOVERY_SCB) == 0)
    {
      restore_flags(processor_flags);
      return (SCSI_RESET_PENDING);
    }
  }
  else
  {
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
	else if (scb == NULL)
        {
          result = SCSI_RESET_NOT_RUNNING;
        }
        else if (flags & SCSI_RESET_ASYNCHRONOUS)
        {
          if (scb->flags & SCB_ABORTED)
          {
            result = SCSI_RESET_PENDING;
          }
          else if (!(scb->flags & SCB_ACTIVE))
          {
            result = SCSI_RESET_NOT_RUNNING;
          }
        }

        if (result == -1)
        {
          if ((flags & SCSI_RESET_SYNCHRONOUS) &&
              (p->device_status[tindex].flags & BUS_DEVICE_RESET_PENDING))
          {
            scb->flags |= SCB_ABORTED;
            result = SCSI_RESET_PENDING;
          }
          else
          {
            p->flags |= IN_TIMEOUT;
            result = aic7xxx_bus_device_reset(p, cmd);
            if (result == 0)
            {
              p->flags &= ~IN_TIMEOUT;
              result = SCSI_RESET_PENDING;
            }
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
      else if (!(scb->flags & SCB_ACTIVE))
      {
	result = SCSI_RESET_NOT_RUNNING;
      }
      else if ((scb->flags & SCB_ABORTED) &&
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
      pause_sequencer(p);
      found = aic7xxx_reset_channel(p, channel, TRUE);
      p->flags = p->flags & ~IN_TIMEOUT;

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
      p->flags &= ~IN_TIMEOUT;
    }
  }
  aic7xxx_run_waiting_queues(p);
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
