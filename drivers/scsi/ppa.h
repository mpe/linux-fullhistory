/*  Driver for the PPA3 parallel port SCSI HBA embedded in 
 * the Iomega ZIP drive
 * 
 * (c) 1996     Grant R. Guenther  grant@torque.net
 *		David Campbell	   campbell@torque.net
 *
 *	All comments to David.
 */

#ifndef _PPA_H
#define _PPA_H

#define   PPA_VERSION   "1.39a"

/* Use the following to enable certain chipset support
 * Default is PEDANTIC = 3
 */
#include <linux/config.h> /* for CONFIG_SCSI_PPA_HAVE_PEDANTIC */
#ifndef CONFIG_SCSI_PPA_HAVE_PEDANTIC
#define CONFIG_SCSI_PPA_HAVE_PEDANTIC	3
#endif

/* 
 * this driver has been hacked by Matteo Frigo (athena@theory.lcs.mit.edu)
 * to support EPP and scatter-gather.                        [0.26-athena]
 *
 * additional hacks by David Campbell
 * in response to this driver "mis-behaving" on his machine.
 *      Fixed EPP to handle "software" changing of EPP port data direction.
 *      Chased down EPP timeouts
 *      Made this driver "kernel version friendly"           [0.28-athena]
 *
 * [ Stuff removed ]
 *
 * Compiled against 2.1.53.
 *	Rebuilt ppa_abort() function, should handle unplugged cable.
 *							[1.35s]
 *
 * PPA now auto probes for EPP on base address which are aligned on
 * 8 byte boundaries (0x278 & 0x378) using the attached devices.
 * This hopefully avoids the nasty problem of trying to detect EPP.
 *	Tested on 2.1.53				[1.36]
 *
 * The id_probe utility no longer performs read/write tests.
 * Additional code included for checking the Intel ECP bug
 * (Bit 0 of STR stuck low which fools the EPP detection routine)
 *							[1.37]
 *
 * Oops! Got the bit sign mixed up for the Intel bug check.
 * Found that an additional delay is required during SCSI resets
 * to allow devices to settle down.
 *							[1.38]
 *
 * Fixed all problems in the parport sharing scheme. Now ppa can be safe
 * used with lp or other parport devices on the same parallel port.
 *		1997 by Andrea Arcangeli <arcangeli@mbox.queen.it>
 *							[1.39]
 *
 * Little fix in ppa engine to ensure that ppa don' t release parport
 * or disconnect in wrong cases.
 *		1997 by Andrea Arcangeli <arcangeli@mbox.queen.it>
 *							[1.39a]
 */
/* ------ END OF USER CONFIGURABLE PARAMETERS ----- */

#ifdef PPA_CODE
#include  <linux/stddef.h>
#include  <linux/module.h>
#include  <linux/kernel.h>
#include  <linux/tqueue.h>
#include  <linux/ioport.h>
#include  <linux/delay.h>
#include  <linux/proc_fs.h>
#include  <linux/stat.h>
#include  <linux/blk.h>
#include  <linux/sched.h>
#include  <linux/interrupt.h>

#include  <asm/io.h>
#include  "sd.h"
#include  "hosts.h"
/* batteries not included :-) */

/*
 * modes in which the driver can operate 
 */
#define   PPA_AUTODETECT        0	/* Autodetect mode                */
#define   PPA_NIBBLE            1	/* work in standard 4 bit mode    */
#define   PPA_PS2               2	/* PS/2 byte mode         */
#define   PPA_EPP_8             3	/* EPP mode, 8 bit                */
#define   PPA_EPP_16            4	/* EPP mode, 16 bit               */
#define   PPA_EPP_32            5	/* EPP mode, 32 bit               */
#define   PPA_UNKNOWN           6	/* Just in case...                */

static char *PPA_MODE_STRING[] =
{
    "Autodetect",
    "SPP",
    "PS/2",
    "EPP 8 bit",
    "EPP 16 bit",
    "EPP 32 bit",
    "Unknown"};

/* This is a global option */
int ppa_sg = SG_ALL;		/* enable/disable scatter-gather. */

/* other options */
#define PPA_CAN_QUEUE   1	/* use "queueing" interface */
#define PPA_BURST_SIZE	512	/* data burst size */
#define PPA_SELECT_TMO  5000	/* how long to wait for target ? */
#define PPA_SPIN_TMO    50000	/* ppa_wait loop limiter */
#define PPA_DEBUG	0	/* debuging option */
#define IN_EPP_MODE(x) (x == PPA_EPP_8 || x == PPA_EPP_16 || x == PPA_EPP_32)

/* args to ppa_connect */
#define CONNECT_EPP_MAYBE 1
#define CONNECT_NORMAL  0

#define r_dtr(x)        (unsigned char)inb((x))
#define r_str(x)        (unsigned char)inb((x)+1)
#define r_ctr(x)        (unsigned char)inb((x)+2)
#define r_epp(x)        (unsigned char)inb((x)+4)
#define r_fifo(x)       (unsigned char)inb((x)+0x400)
#define r_ecr(x)        (unsigned char)inb((x)+0x402)

#define w_dtr(x,y)      outb(y, (x))
#define w_str(x,y)      outb(y, (x)+1)
#define w_ctr(x,y)      outb(y, (x)+2)
#define w_epp(x,y)      outb(y, (x)+4)
#define w_fifo(x,y)     outb(y, (x)+0x400)
#define w_ecr(x,y)      outb(y, (x)+0x402)

static int ppa_engine(ppa_struct *, Scsi_Cmnd *);
static int ppa_in(int, char *, int);
static int ppa_init(int);
static void ppa_interrupt(void *);
static int ppa_out(int, char *, int);

struct proc_dir_entry proc_scsi_ppa =
{PROC_SCSI_PPA, 3, "ppa", S_IFDIR | S_IRUGO | S_IXUGO, 2};
#else
extern struct proc_dir_entry proc_scsi_ppa;
#define ppa_release 0
#endif

int ppa_detect(Scsi_Host_Template *);
const char *ppa_info(struct Scsi_Host *);
int ppa_command(Scsi_Cmnd *);
int ppa_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int ppa_abort(Scsi_Cmnd *);
int ppa_reset(Scsi_Cmnd *, unsigned int);
int ppa_proc_info(char *, char **, off_t, int, int, int);
int ppa_biosparam(Disk *, kdev_t, int *);

#define PPA {	proc_dir:		&proc_scsi_ppa,			\
		proc_info:		ppa_proc_info,			\
		name:			"Iomega parport ZIP drive",	\
		detect:			ppa_detect,			\
		release:		ppa_release,			\
		command:		ppa_command,			\
		queuecommand:		ppa_queuecommand,		\
		abort:			ppa_abort,			\
		reset:			ppa_reset,			\
		bios_param:		ppa_biosparam,			\
		this_id:		-1,				\
		sg_tablesize:		SG_ALL,				\
		cmd_per_lun:		1,				\
		use_clustering:		ENABLE_CLUSTERING		\
}
#endif				/* _PPA_H */
