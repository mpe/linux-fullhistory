/*
 *  linux/drivers/block/ide.c	Version 3.16  May 30, 1995
 *
 *  Copyright (C) 1994, 1995  Linus Torvalds & authors (see below)
 */

/*
 * This is the dual IDE interface driver, as evolved from hd.c.  
 * It supports up to two IDE interfaces, on one or two IRQs (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * Primary   i/f: ide0: major=3;  (hda)         minor=0, (hdb)         minor=64
 * Secondary i/f: ide1: major=22; (hdc or hd1a) minor=0, (hdd or hd1b) minor=64
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@bnr.ca).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@bnr.ca)			(IDE Perf.Pkg)
 *	Delman Lee	(delman@mipg.upenn.edu)		("Mr. atdisk2")
 *	Petri Mattila	(ptjmatti@kruuna.helsinki.fi)	(EIDE stuff)
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with 
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 *
 *  Version 1.0 ALPHA	initial code, primary i/f working okay
 *  Version 1.1 ALPHA	fixes for dual i/f
 *  Version 1.2 ALPHA	first serious attempt at sharing irqs
 *  Version 1.3 BETA	dual i/f on shared irq tested & working!
 *  Version 1.4 BETA	added auto probing for irq(s)
 *  Version 1.5 BETA	added ALPHA (untested) support for IDE cd-roms,
 *			fixed hd.c coexistence bug, other minor stuff
 *  Version 1.6 BETA	fix link error when cd-rom not configured
 *  Version 2.0 BETA	lots of minor fixes; remove annoying messages; ...
 *  Version 2.2 BETA	fixed reset_drives; major overhaul of autoprobing
 *  Version 2.3 BETA	set DEFAULT_UNMASK_INTR to 0 again; cosmetic changes
 *  Version 2.4 BETA	added debounce on reading of drive status reg,
 *			added config flags to remove unwanted features
 *  Version 2.5 BETA	fixed problem with leftover phantom IRQ after probe,
 *			allow "set_geometry" even when in LBA (as per spec(?)),
 *			assorted miscellaneous tweaks.
 *  Version 2.6 BETA	more config flag stuff, another probing tweak,
 *  (not released)	multmode now defaults to status quo from boot time,
 *			moved >16heads check to init time, rearranged reset code
 *			added HDIO_DRIVE_CMD, removed standby/xfermode stuff
 *			hopefully fixed ATAPI probing code, added hdx=cdrom
 *  Version 2.7 BETA	fixed invocation of cdrom_setup()
 *  Version 2.8 BETA	fixed compile error for DISK_RECOVERY_TIME>0
 *			fixed incorrect drive selection in DO_DRIVE_CMD (Bug!)
 *  Version 2.9 BETA	more work on ATAPI CDROM recognition
 *  (not released)	changed init order so partition checks go in sequence
 *  Version 3.0 BETA	included ide-cd.c update from Steve with Mitsumi fixes
 *			attempt to fix byte-swap problem with Mitsumi id_info
 *			ensure drives on second i/f get initialized on boot
 *			preliminary compile-time support for 32bit IDE i/f chips
 *			added check_region() and snarf_region() to probes
 *  Version 3.1 BETA	ensure drives on *both* i/f get initialized on boot
 *			fix byte-swap problem with Mitsumi id_info
 *			changed ide_timermask into ide_timerbit
 *			get rid of unexpected interrupts after probing
 *			don't wait for READY_STAT on cdrom drives
 *  Version 3.2 BETA	Ooops.. mistakenly left VLB_32BIT_IDE on by default
 *			new ide-cd.c from Scott
 *  Version 3.3 BETA	fix compiling with PROBE_FOR_IRQS==0
 *  (sent to Linus)	tweak in do_probe() to fix Delman's DRDY problem
 *  Version 3.4 BETA	removed "444" debug message
 *  (sent to Linus)
 *  Version 3.5		correct the bios_cyl field if it's too small
 *  (linux 1.1.76)	 (to help fdisk with brain-dead BIOSs)
 *  Version 3.6		cosmetic corrections to comments and stuff
 *  (linux 1.1.77)	reorganise probing code to make it understandable
 *			added halfway retry to probing for drive identification
 *			added "hdx=noprobe" command line option
 *			allow setting multmode even when identification fails
 *  Version 3.7		move set_geometry=1 from do_identify() to ide_init()
 *			increase DRQ_WAIT to eliminate nuisance messages
 *			wait for DRQ_STAT instead of DATA_READY during probing
 *			  (courtesy of Gary Thomas gary@efland.UU.NET)
 *  Version 3.8		fixed byte-swapping for confused Mitsumi cdrom drives
 *			update of ide-cd.c from Scott, allows blocksize=1024
 *			cdrom probe fixes, inspired by jprang@uni-duisburg.de
 *  Version 3.9		don't use LBA if lba_capacity looks funny
 *			correct the drive capacity calculations
 *			fix probing for old Seagates without HD_ALTSTATUS
 *			fix byte-ordering for some NEC cdrom drives
 *  Version 3.10	disable multiple mode by default; was causing trouble
 *  Version 3.11	fix mis-identification of old WD disks as cdroms
 *  Version 3,12	simplify logic for selecting initial mult_count
 *			  (fixes problems with buggy WD drives)
 *  Version 3.13	remove excess "multiple mode disabled" messages
 *  Version 3.14	fix ide_error() handling of BUSY_STAT
 *			fix byte-swapped cdrom strings (again.. arghh!)
 *			ignore INDEX bit when checking the ALTSTATUS reg
 *  Version 3.15	add SINGLE_THREADED flag for use with dual-CMD i/f
 *			ignore WRERR_STAT for non-write operations
 *			added VLB_SYNC support for DC-2000A & others,
 *			 (incl. some Promise chips), courtesy of Frank Gockel
 *  Version 3.16	convert VLB_32BIT and VLB_SYNC into runtime flags
 *			add ioctls to get/set VLB flags (HDIO_[SG]ET_CHIPSET)
 *			rename SINGLE_THREADED to SUPPORT_SERIALIZE,
 *			add boot flag to "serialize" operation for CMD i/f
 *			add optional support for DTC2278 interfaces,
 *			 courtesy of andy@cercle.cts.com (Dyan Wile).
 *			add boot flag to enable "dtc2278" probe
 *			add probe to avoid EATA (SCSI) interfaces,
 *			 courtesy of neuffer@goofy.zdv.uni-mainz.de.
 *
 *  To do:
 *	- improved CMD support:  tech info is supposedly "in the mail"
 *	- special 32-bit controller-type detection & support
 *	- figure out how to support oddball "intelligent" caching cards
 *	- reverse-engineer 3/4 drive support on fancy "Promise" cards
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <asm/bitops.h>
#include <asm/irq.h>
#include <asm/segment.h>
#include <asm/system.h>

/*****************************************************************************
 * IDE driver configuration options (play with these as desired):
 */
#define REALLY_SLOW_IO			/* most systems can safely undef this */
#include <asm/io.h>

#undef	REALLY_FAST_IO			/* define if ide ports are perfect */
#define INITIAL_MULT_COUNT	0	/* off=0; on=2,4,8,16,32, etc.. */
#ifndef SUPPORT_VLB_32BIT		/* 1 to support 32bit I/O on VLB */
#define SUPPORT_VLB_32BIT	1	/* 0 to reduce kernel size */
#endif
#ifndef SUPPORT_VLB_SYNC		/* 1 to support weird 32-bit chips */
#define SUPPORT_VLB_SYNC	1	/* 0 to reduce kernel size */
#endif
#ifndef DISK_RECOVERY_TIME		/* off=0; on=access_delay_time */
#define DISK_RECOVERY_TIME	0	/*  for hardware that needs it */
#endif
#ifndef OK_TO_RESET_CONTROLLER		/* 1 needed for good error recovery */
#define OK_TO_RESET_CONTROLLER	1	/* 0 for use with AH2372A/B interface */
#endif
#ifndef SUPPORT_TWO_INTERFACES		/* 1 to support one/two interfaces */
#define SUPPORT_TWO_INTERFACES	1	/* 0 for a smaller, faster kernel */
#endif
#ifndef OPTIMIZE_IRQS			/* 1 for slightly faster code */
#define OPTIMIZE_IRQS		1	/* 0 to reduce kernel size */
#endif
#ifndef SUPPORT_SERIALIZE		/* 1 to support CMD dual interfaces */
#define SUPPORT_SERIALIZE	1	/* 0 to reduce kernel size */
#endif
#ifndef SUPPORT_SHARING_IRQ		/* 1 to allow two IDE i/f on one IRQ */
#define SUPPORT_SHARING_IRQ	1	/* 0 to reduce kernel size */
#endif
#ifndef SUPPORT_DTC2278			/* 1 to support DTC2278 chipset */
#define SUPPORT_DTC2278		1	/* 0 to reduce kernel size */
#endif
#ifndef FANCY_STATUS_DUMPS		/* 1 for human-readable drive errors */
#define FANCY_STATUS_DUMPS	1	/* 0 to reduce kernel size */
#endif
#define PROBE_FOR_IRQS		1	/* 0 to force use of defaults below */
#define DEFAULT_IDE0_IRQ	14	/* in case irq-probe fails */
#define DEFAULT_IDE1_IRQ	15	/* in case irq-probe fails */

/* IDE_DRIVE_CMD is used to implement many features of the hdparm utility */
#define IDE_DRIVE_CMD		99	/* (magic) undef to reduce kernel size*/

/*
 *  "No user-serviceable parts" beyond this point  :)
 ******************************************************************************
 */

/*
 * Need to change these elsewhere in the kernel (someday)
 */
#ifndef IDE0_TIMER
#define IDE0_TIMER		HD_TIMER
#define IDE1_TIMER		HD_TIMER2
#endif

/*
 * Ensure that various configuration flags have compatible settings
 */
#ifdef REALLY_SLOW_IO
#undef REALLY_FAST_IO
#endif
#ifdef CONFIG_BLK_DEV_HD
#undef  SUPPORT_TWO_INTERFACES
#define SUPPORT_TWO_INTERFACES	0
#endif	/* CONFIG_BLK_DEV_HD */

#if SUPPORT_TWO_INTERFACES
#define HWIF			hwif
#define DEV_HWIF		(dev->hwif)
#if SUPPORT_SERIALIZE
#undef	SUPPORT_SHARING_IRQ
#define	SUPPORT_SHARING_IRQ	1
#endif
#else
#undef	SUPPORT_SERIALIZE
#define SUPPORT_SERIALIZE		0
#undef	OPTIMIZE_IRQS
#define OPTIMIZE_IRQS		0
#undef	SUPPORT_SHARING_IRQ
#define SUPPORT_SHARING_IRQ	0
#ifdef CONFIG_BLK_DEV_HD
#define HWIF			1
#else
#define HWIF			0
#endif	/* CONFIG_BLK_DEV_HD */
#define DEV_HWIF		HWIF
#endif	/* SUPPORT_TWO_INTERFACES */

/*
 * Definitions for accessing IDE controller registers
 */
typedef unsigned char		byte;	/* used everywhere */
#define IDE_PORT(p,hwif) ((p)^((hwif)<<7)) /* IDE0: p^0x00 , IDE1: p^0x80 */

#ifdef REALLY_FAST_IO
#define OUT_BYTE(b,p)		outb((b),IDE_PORT(p,DEV_HWIF))
#define IN_BYTE(p,hwif)		(byte)inb(IDE_PORT(p,hwif))
#else
#define OUT_BYTE(b,p)		outb_p((b),IDE_PORT(p,DEV_HWIF))
#define IN_BYTE(p,hwif)		(byte)inb_p(IDE_PORT(p,hwif))
#endif /* REALLY_FAST_IO */

#if SUPPORT_VLB_32BIT
#if SUPPORT_VLB_SYNC
#define VLB_SYNC __asm__ __volatile__ ("pusha\n movl $0x01f2,%edx\n inb (%dx),%al\n inb (%dx),%al\n inb (%dx),%al\n popa\n")
#endif	/* SUPPORT_VLB_SYNC */
#endif	/* SUPPORT_VLB_32BIT */

#if SUPPORT_DTC2278
static uint probe_dtc2278 = 0;
#endif

#define GET_ERR(hwif)		IN_BYTE(HD_ERROR,hwif)
#define GET_STAT(hwif)		IN_BYTE(HD_STATUS,hwif)
#define OK_STAT(stat,good,bad)	(((stat)&((good)|(bad)))==(good))
#define BAD_R_STAT		(BUSY_STAT   | ERR_STAT)
#define BAD_W_STAT		(BUSY_STAT   | ERR_STAT | WRERR_STAT)
#define BAD_STAT		(BAD_R_STAT  | DRQ_STAT)
#define DRIVE_READY		(READY_STAT  | SEEK_STAT)
#define DATA_READY		(DRIVE_READY | DRQ_STAT)

/*
 * Some more useful definitions
 */
#define BIOS_SECTORS(dev)	(dev->bios_head*dev->bios_sect*dev->bios_cyl)
#define HD_NAME		"hd"	/* the same for both i/f;  see also genhd.c */
#define PARTN_BITS	6	/* number of minor dev bits for partitions */
#define PARTN_MASK	((1<<PARTN_BITS)-1)	/* a useful bit mask */
#define MAX_DRIVES	2	/* per interface; 2 assumed by lots of code */
#define SECTOR_WORDS	(512 / 4)	/* number of 32bit words per sector */

/*
 * Timeouts for various operations:
 */
#define WAIT_DRQ	5	/* 50msec - spec allows up to 20ms */
#define WAIT_READY	3	/* 30msec - should be instantaneous */
#define WAIT_PIDENTIFY	100	/* 1sec   - should be less than 3ms (?) */
#define WAIT_WORSTCASE	3000	/* 30sec  - worst case when spinning up */
#define WAIT_CMD	1000	/* 10sec  - maximum wait for an IRQ to happen */

/*
 * Now for the data we need to maintain per-device:  ide_dev_t
 *
 * For fast indexing, sizeof(ide_dev_t) = 32 = power_of_2;
 * Everything is carefully aligned on appropriate boundaries,
 *  and several fields are placed for optimal (gcc) access.
 */
typedef enum {disk, cdrom} dev_type;

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned set_geometry	: 1;	/* respecify drive geometry */
		unsigned recalibrate	: 1;	/* seek to cyl 0      */
		unsigned set_multmode	: 1;	/* set multmode count */
		unsigned reserved	: 5;	/* unused */
		} b;
	} special_t;

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned head		: 4;	/* always zeros here */
		unsigned drive		: 1;	/* drive number */
		unsigned bit5		: 1;	/* always 1 */
		unsigned lba		: 1;	/* LBA instead of CHS */
		unsigned bit7		: 1;	/* always 1 */
	} b;
	} select_t;

typedef struct {
	byte     hwif;			/* first field gets very fast access */
	byte     unmask;		/* pretty quick access to this also */
	dev_type type		: 1;	/* disk or cdrom (or tape, floppy..) */
	unsigned present	: 1;	/* drive is physically present */
	unsigned dont_probe 	: 1;	/* from:  hdx=noprobe */
	unsigned keep_settings  : 1;	/* restore settings after drive reset */
	unsigned busy		: 1;	/* mutex for ide_open, revalidate_.. */
	unsigned vlb_32bit	: 1;	/* use 32bit in/out for data */
	unsigned vlb_sync	: 1;	/* needed for some 32bit chip sets */
	unsigned reserved0	: 1;	/* unused */
	special_t special;		/* special action flags */
	select_t  select;		/* basic drive/head select reg value */
	byte mult_count, chipset, reserved2;
	byte usage, mult_req, wpcom, ctl;
	byte head, sect, bios_head, bios_sect;
	unsigned short cyl, bios_cyl;
	const char *name;
	struct hd_driveid *id;
	struct wait_queue *wqueue;
	} ide_dev_t;

/*
 * Stuff prefixed by "ide_" is indexed by the IDE interface number: 0 or 1
 */
static const byte	ide_major    [2] = {IDE0_MAJOR,IDE1_MAJOR};
static byte		ide_irq      [2] = {DEFAULT_IDE0_IRQ,DEFAULT_IDE1_IRQ};
static struct hd_struct	ide_hd       [2][MAX_DRIVES<<PARTN_BITS] = {{{0,0},},};
static int		ide_sizes    [2][MAX_DRIVES<<PARTN_BITS] = {{0,},};
static int		ide_blksizes [2][MAX_DRIVES<<PARTN_BITS] = {{0,},};
static unsigned long	ide_capacity [2][MAX_DRIVES] = {{0,},};
static ide_dev_t	ide_dev      [2][MAX_DRIVES] = {{{0,},},};
static ide_dev_t	*ide_cur_dev [2] = {NULL,NULL};
static void		(*ide_handler[2])(ide_dev_t *) = {NULL,NULL};
static struct request	*ide_cur_rq  [2] = {NULL,NULL}; /* current request */
static struct request	ide_write_rq [2];  /* copy of *ide_cur_rq for WRITEs */
static const int	ide_timer    [2] = {IDE0_TIMER,IDE1_TIMER};
static const int	ide_timerbit[2] = {(1<<IDE0_TIMER),(1<<IDE1_TIMER)};
static const char	*ide_name    [2] = {"ide0", "ide1"};
static const char	*ide_devname [2][MAX_DRIVES] = /* for printk()'s */
	{{HD_NAME "a", HD_NAME "b"}, {HD_NAME "c", HD_NAME "d"}};
static const char	*unsupported = " not supported by this kernel\n";

static byte		single_threaded    = 0;
#if SUPPORT_SHARING_IRQ
static byte		sharing_single_irq = 0;	/* for two i/f on one IRQ */
static volatile byte 	current_hwif = 0;	/* for single_threaded==1 */
#endif /* SUPPORT_SHARING_IRQ */

/*
 * This structure is used to register our block device(s) with the kernel:
 */
static void ide0_geninit(void), ide1_geninit(void);
static struct gendisk	ide_gendisk  [2] =
	{{
		IDE0_MAJOR,	/* major number */	
		HD_NAME,	/* same as below; see genhd.c before changing */
		PARTN_BITS,	/* minor_shift (to extract minor number) */
		1 << PARTN_BITS,/* max_p (number of partitions per real) */
		MAX_DRIVES,	/* maximum number of real drives */
		ide0_geninit,	/* init function */
		ide_hd[0],	/* hd_struct */
		ide_sizes[0],	/* block sizes */
		0,		/* nr_real (number of drives present) */
		ide_dev[0],	/* ptr to internal data structure */
		NULL		/* next */
	},{
		IDE1_MAJOR,	/* major number */	
		HD_NAME,	/* same as above; see genhd.c before changing */
		PARTN_BITS,	/* minor_shift (to extract minor number) */
		1 << PARTN_BITS,/* max_p (number of partitions per real) */
		MAX_DRIVES,	/* maximum number of real drives */
		ide1_geninit,	/* init function */
		ide_hd[1],	/* hd_struct */
		ide_sizes[1],	/* block sizes */
		0,		/* nr_real (number of drives present) */
		ide_dev[1],	/* ptr to internal data structure */
		NULL		/* next */
	}};

/*
 * One final include file, which references some of the data/defns from above
 */
#define IDE_DRIVER	/* "parameter" for blk.h */
#include "blk.h"

/*
 * For really screwy hardware (hey, at least it *can* be used with Linux!
 */
#if (DISK_RECOVERY_TIME > 0)
static unsigned long	ide_lastreq[] = {0,0}; /* completion time of last I/O */
#define SET_DISK_RECOVERY_TIMER  ide_lastreq[DEV_HWIF] = read_timer();
static unsigned long read_timer(void)
{
	unsigned long t, flags;
	int i;

	save_flags(flags);
	cli();
	t = jiffies * 11932;
    	outb_p(0, 0x43);
	i = inb_p(0x40);
	i |= inb(0x40) << 8;
	restore_flags(flags);
	return (t - i);
}
#else
#define SET_DISK_RECOVERY_TIMER	/* nothing */
#endif /* DISK_RECOVERY_TIME */

/*
 * The heart of the driver, referenced from lots of other routines:
 */
static void do_request (byte hwif);
#define DO_REQUEST {SET_DISK_RECOVERY_TIMER do_request(DEV_HWIF);}

/*
 * This is a macro rather than an inline to permit better gcc code.
 * Caller MUST do sti() before invoking WAIT_STAT() (for jiffies to work).
 *
 * This routine should get fixed to not hog the cpu during extra long waits..
 * That could be done by busy-waiting for the first jiffy or two, and then
 * setting a timer to wake up at half second intervals thereafter,
 * until WAIT_WORSTCASE is achieved, before timing out.
 */
#define WAIT_STAT(dev,good,bad,timeout,msg,label)			\
{									\
	byte stat;							\
	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */	\
	if (GET_STAT(DEV_HWIF) & BUSY_STAT) {				\
		unsigned long timer = jiffies + timeout;		\
		do {							\
			if ((GET_STAT(DEV_HWIF) & BUSY_STAT) == 0)	\
				break;					\
		} while (timer > jiffies);				\
	}								\
	udelay(1);	/* spec allows 400ns for status to stabilize */	\
	if (!OK_STAT(stat=GET_STAT(DEV_HWIF), good, bad)) {		\
		ide_error(dev, msg " error", stat);			\
		goto label;						\
	}								\
}

/*
 * This is used for all data transfers *from* the IDE interface
 */
void input_ide_data (ide_dev_t *dev, void *buffer, uint wcount)
{
#if SUPPORT_VLB_32BIT
	if (dev->vlb_32bit) {
#if SUPPORT_VLB_SYNC
		if (dev->vlb_sync) {
			cli();
			VLB_SYNC;
			insl(IDE_PORT(HD_DATA,DEV_HWIF), buffer, wcount);
			if (dev->unmask)
				sti();
		} else
#endif /* SUPPORT_VLB_SYNC */
			insl(IDE_PORT(HD_DATA,DEV_HWIF), buffer, wcount);
	} else
#endif /* SUPPORT_VLB_32BIT */
		insw(IDE_PORT(HD_DATA,DEV_HWIF), buffer, wcount<<1);
}

/*
 * This is used for all data transfers *to* the IDE interface
 */
void output_ide_data (ide_dev_t *dev, void *buffer, uint wcount)
{
#if SUPPORT_VLB_32BIT
	if (dev->vlb_32bit) {
#if SUPPORT_VLB_SYNC
		if (dev->vlb_sync) {
			cli();
			VLB_SYNC;
			outsl(IDE_PORT(HD_DATA,DEV_HWIF), buffer, wcount);
			if (dev->unmask)
				sti();
		} else
			outsl(IDE_PORT(HD_DATA,DEV_HWIF), buffer, wcount);
#endif /* SUPPORT_VLB_SYNC */
	} else
#endif /* SUPPORT_VLB_32BIT */
		outsw(IDE_PORT(HD_DATA,DEV_HWIF), buffer, wcount<<1);
}

/*
 * This should get invoked on every exit path from the driver.
 */
static inline void start_ide_timer (byte hwif)
{
	if (ide_handler[HWIF] != NULL) {  	/* waiting for an irq? */
		timer_table[ide_timer[HWIF]].expires = jiffies + WAIT_CMD;
		timer_active |= ide_timerbit[HWIF];
	}
}

static void do_ide_reset (ide_dev_t *dev)
{
	byte tmp;
	unsigned long timer, flags;

	save_flags(flags);
	sti();
	for (tmp = 0; tmp < MAX_DRIVES; tmp++) {
		ide_dev_t *rdev = &ide_dev[DEV_HWIF][tmp];
		rdev->special.b.set_geometry = 1;
		rdev->special.b.recalibrate  = 1;
		rdev->special.b.set_multmode = 0;
		if (OK_TO_RESET_CONTROLLER)
			rdev->mult_count = 0;
		if (!rdev->keep_settings) {
			rdev->mult_req = 0;
			rdev->unmask = 0;
		}
		if (rdev->mult_req != rdev->mult_count)
			rdev->special.b.set_multmode = 1;
	}

#if OK_TO_RESET_CONTROLLER
	cli();
	OUT_BYTE(dev->ctl|6,HD_CMD);	/* set nIEN, set SRST */
	udelay(10);			/* more than enough time */
	OUT_BYTE(dev->ctl|2,HD_CMD);	/* clear SRST */
	udelay(10);			/* more than enough time */
	sti();				/* needed for jiffies */
	for (timer = jiffies + WAIT_WORSTCASE; timer > jiffies;) {
		if ((GET_STAT(DEV_HWIF) & BUSY_STAT) == 0)
			break;
	}
	printk("%s: do_ide_reset: ", ide_name[DEV_HWIF]);
	/* ATAPI devices usually do *not* assert READY after a reset */
	if (!OK_STAT(tmp=GET_STAT(DEV_HWIF), 0, BUSY_STAT)) {
		printk("timed-out, status=0x%02x\n", tmp);
	} else  {
		if ((tmp = GET_ERR(DEV_HWIF)) == 1)
			printk("success\n");
		else {
			printk("%s: ", ide_devname[DEV_HWIF][0]);
			switch (tmp & 0x7f) {
				case 1: printk("passed");
					break;
				case 2: printk("formatter device error");
					break;
				case 3: printk("sector buffer error");
					break;
				case 4: printk("ECC circuitry error");
					break;
				case 5: printk("controlling MPU error");
					break;
				default:printk("error (0x%02x?)", tmp);
			}
			if (tmp & 0x80)
				printk("; %s: error", ide_devname[DEV_HWIF][1]);
			printk("\n");
		}
	}
#endif	/* OK_TO_RESET_CONTROLLER */
	restore_flags(flags);
}

/*
 * Clean up after success/failure of an explicit (ioctl) drive cmd
 */
static void end_drive_cmd (ide_dev_t *dev, byte stat, byte err)
{
	unsigned long flags;
	struct request *rq = ide_cur_rq[DEV_HWIF];
	byte *args = (byte *) rq->buffer;

	rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);
	if (args) {
		args[0] = stat;
		args[1] = err;
		args[2] = IN_BYTE(HD_NSECTOR,DEV_HWIF);
	}
	save_flags(flags);
	cli();
	up(rq->sem);
	ide_cur_rq[DEV_HWIF] = NULL;
	restore_flags(flags);
}

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
static byte dump_status (byte hwif, const char *msg, byte stat)
{
	unsigned long flags;
	byte err = 0;
	ide_dev_t *dev = ide_cur_dev[HWIF];
	const char *name = dev ? dev->name : ide_name[HWIF];

	save_flags (flags);
	sti();
	printk("%s: %s: status=0x%02x", name, msg, stat);
#if FANCY_STATUS_DUMPS
	if (dev && dev->type == disk) {
		printk(" { ");
		if (stat & BUSY_STAT)
			printk("Busy ");
		else {
			if (stat & READY_STAT)	printk("DriveReady ");
			if (stat & WRERR_STAT)	printk("WriteFault ");
			if (stat & SEEK_STAT)	printk("SeekComplete ");
			if (stat & DRQ_STAT)	printk("DataRequest ");
			if (stat & ECC_STAT)	printk("CorrectedError ");
			if (stat & INDEX_STAT)	printk("Index ");
			if (stat & ERR_STAT)	printk("Error ");
		}
		printk("}");
	}
#endif	/* FANCY_STATUS_DUMPS */
	printk("\n");
	if ((stat & (BUSY_STAT|ERR_STAT)) == ERR_STAT) {
		err = GET_ERR(HWIF);
		printk("%s: %s: error=0x%02x", name, msg, err);
#if FANCY_STATUS_DUMPS
		if (dev && dev->type == disk) {
			printk(" { ");
			if (err & BBD_ERR)	printk("BadSector ");
			if (err & ECC_ERR)	printk("UncorrectableError ");
			if (err & ID_ERR)	printk("SectorIdNotFound ");
			if (err & ABRT_ERR)	printk("DriveStatusError ");
			if (err & TRK0_ERR)	printk("TrackZeroNotFound ");
			if (err & MARK_ERR)	printk("AddrMarkNotFound ");
			printk("}");
			if (err & (BBD_ERR|ECC_ERR|ID_ERR|MARK_ERR)) {
				byte cur = IN_BYTE(HD_CURRENT,HWIF);
				if (cur & 0x40) {	/* using LBA? */
					printk(", LBAsect=%ld", (unsigned long)
					 ((cur&0xf)<<24)
					 |(IN_BYTE(HD_HCYL,HWIF)<<16)
					 |(IN_BYTE(HD_LCYL,HWIF)<<8)
					 | IN_BYTE(HD_SECTOR,HWIF));
				} else {
					printk(", CHS=%d/%d/%d",
					 (IN_BYTE(HD_HCYL,HWIF)<<8) +
					  IN_BYTE(HD_LCYL,HWIF),
					  cur & 0xf,
					  IN_BYTE(HD_SECTOR,HWIF));
				}
				if (ide_cur_rq[HWIF])
					printk(", sector=%ld", ide_cur_rq[HWIF]->sector);
			}
		}
#endif	/* FANCY_STATUS_DUMPS */
		printk("\n");
	}
	restore_flags (flags);
	return err;
}

/*
 * ide_error() takes action based on the error returned by the controller.
 */
#define ERROR_MAX	8	/* Max read/write errors per sector */
#define ERROR_RESET	3	/* Reset controller every 4th retry */
#define ERROR_RECAL	1	/* Recalibrate every 2nd retry */
static void ide_error (ide_dev_t *dev, const char *msg, byte stat)
{
	struct request *rq;
	byte err;

	err = dump_status(DEV_HWIF, msg, stat);
	if ((rq = ide_cur_rq[DEV_HWIF]) == NULL || dev == NULL)
		return;
#ifdef IDE_DRIVE_CMD
	if (rq->cmd == IDE_DRIVE_CMD) {	/* never retry an explicit DRIVE_CMD */
		end_drive_cmd(dev, stat, err);
		return;
	}
#endif	/* IDE_DRIVE_CMD */
	if (stat & BUSY_STAT) {		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else {
		if (dev->type == disk && (stat & ERR_STAT)) {
			/* err has different meaning on cdrom */
			if (err & BBD_ERR)		/* retries won't help this! */
				rq->errors = ERROR_MAX;
			else if (err & TRK0_ERR)	/* help it find track zero */
				rq->errors |= ERROR_RECAL;
		}
		if ((stat & DRQ_STAT) && rq->cmd == READ) {
			int i = dev->mult_count ? dev->mult_count<<8 : 1<<8;
			while (i-- > 0)			/* try to flush data */
				(void) IN_BYTE(HD_DATA, dev->hwif);
		}
	}
	if (GET_STAT(dev->hwif) & (BUSY_STAT|DRQ_STAT))
		rq->errors |= ERROR_RESET;	/* Mmmm.. timing problem */

	if (rq->errors >= ERROR_MAX)
		end_request(0, DEV_HWIF);
	else {
		if ((rq->errors & ERROR_RESET) == ERROR_RESET)
			do_ide_reset(dev);
		else if ((rq->errors & ERROR_RECAL) == ERROR_RECAL)
			dev->special.b.recalibrate = 1;
		++rq->errors;
	}
}

static void read_intr (ide_dev_t *dev)
{
	byte stat;
	int i;
	unsigned int msect, nsect;
	struct request *rq;

	if (!OK_STAT(stat=GET_STAT(DEV_HWIF),DATA_READY,BAD_R_STAT)) {
		sti();
		ide_error(dev, "read_intr", stat);
		DO_REQUEST;
		return;
	}
	msect = dev->mult_count;
read_next:
	rq = ide_cur_rq[DEV_HWIF];
	if (msect) {
		if ((nsect = rq->current_nr_sectors) > msect)
			nsect = msect;
		msect -= nsect;
	} else
		nsect = 1;
	input_ide_data(dev, rq->buffer, nsect * SECTOR_WORDS);
#ifdef DEBUG
	printk("%s:  read: sectors(%ld-%ld), buffer=0x%08lx, remaining=%ld\n",
		dev->name, rq->sector, rq->sector+nsect-1,
		(unsigned long) rq->buffer+(nsect<<9), rq->nr_sectors-nsect);
#endif
	rq->sector += nsect;
	rq->buffer += nsect<<9;
	rq->errors = 0;
	i = (rq->nr_sectors -= nsect);
	if ((rq->current_nr_sectors -= nsect) <= 0)
		end_request(1, DEV_HWIF);
	if (i > 0) {
		if (msect)
			goto read_next;
		ide_handler[DEV_HWIF] = &read_intr;
		return;
	}
	/* (void) GET_STAT(DEV_HWIF); */	/* hd.c did this */
	DO_REQUEST;
}

static void write_intr (ide_dev_t *dev)
{
	byte stat;
	int i;
	struct request *rq = ide_cur_rq[DEV_HWIF];

	if (OK_STAT(stat=GET_STAT(DEV_HWIF),DRIVE_READY,BAD_W_STAT)) {
#ifdef DEBUG
		printk("%s: write: sector %ld, buffer=0x%08lx, remaining=%ld\n",
			dev->name, rq->sector, (unsigned long) rq->buffer,
			rq->nr_sectors-1);
#endif
		if ((rq->nr_sectors == 1) ^ ((stat & DRQ_STAT) != 0)) {
			rq->sector++;
			rq->buffer += 512;
			rq->errors = 0;
			i = --rq->nr_sectors;
			--rq->current_nr_sectors;
			if (rq->current_nr_sectors <= 0)
				end_request(1, DEV_HWIF);
			if (i > 0) {
				ide_handler[DEV_HWIF] = &write_intr;
				output_ide_data(dev, rq->buffer, SECTOR_WORDS);
				return;
			}
			DO_REQUEST;
			return;
		}
	}
	sti();
	ide_error(dev, "write_intr", stat);
	DO_REQUEST;
}

static void multwrite (ide_dev_t *dev)
{
	struct request *rq = &ide_write_rq[DEV_HWIF];
	unsigned int mcount = dev->mult_count;

	do {
		unsigned int nsect = rq->current_nr_sectors;
		if (nsect > mcount)
			nsect = mcount;
		mcount -= nsect;

		output_ide_data(dev, rq->buffer, nsect<<7);
#ifdef DEBUG
		printk("%s: multwrite: sector %ld, buffer=0x%08lx, count=%d, remaining=%ld\n",
			dev->name, rq->sector, (unsigned long) rq->buffer,
			nsect, rq->nr_sectors - nsect);
#endif
		if ((rq->nr_sectors -= nsect) <= 0)
			break;
		if ((rq->current_nr_sectors -= nsect) == 0) {
			if ((rq->bh = rq->bh->b_reqnext) != NULL) {
				rq->current_nr_sectors = rq->bh->b_size>>9;
				rq->buffer             = rq->bh->b_data;
			} else {
				panic("%s: buffer list corrupted\n", dev->name);
				break;
			}
		} else {
			rq->buffer += nsect << 9;
		}
	} while (mcount);
}

static void multwrite_intr (ide_dev_t *dev)
{
	byte stat;
	int i;
	struct request *rq = &ide_write_rq[DEV_HWIF];

	if (OK_STAT(stat=GET_STAT(DEV_HWIF),DRIVE_READY,BAD_W_STAT)) {
		if (stat & DRQ_STAT) {
			if (rq->nr_sectors) {
				if (dev->mult_count)
					multwrite(dev);
				ide_handler[DEV_HWIF] = &multwrite_intr;
				return;
			}
		} else {
			if (!rq->nr_sectors) {	/* all done? */
				rq = ide_cur_rq[DEV_HWIF];
				for (i = rq->nr_sectors; i > 0;){
					i -= rq->current_nr_sectors;
					end_request(1, DEV_HWIF);
				}
				DO_REQUEST;
				return;
			}
		}
	}
	sti();
	ide_error(dev, "multwrite_intr", stat);
	DO_REQUEST;
}

/*
 * Issue a simple drive command
 * The drive must be selected beforehand.
 */
static inline void ide_cmd(ide_dev_t *dev, byte cmd, byte nsect,
				void (*handler)(ide_dev_t *dev))
{
	OUT_BYTE(dev->ctl,HD_CMD);
	OUT_BYTE(nsect,HD_NSECTOR);
	OUT_BYTE(cmd,HD_COMMAND);
	ide_handler[DEV_HWIF] = handler;
}

static void set_multmode_intr (ide_dev_t *dev)
{
	byte stat = GET_STAT(DEV_HWIF);

	sti();
	if (!OK_STAT(stat,READY_STAT,BAD_STAT)) {
		dev->mult_req = dev->mult_count = 0;
		dev->special.b.recalibrate = 1;
		(void) dump_status(DEV_HWIF, "set_multmode", stat);
	} else {
		if ((dev->mult_count = dev->mult_req))
			printk ("  %s: enabled %d-sector multiple mode\n",
				dev->name, dev->mult_count);
		else
			printk ("  %s: multiple mode turned off\n", dev->name);
	}
	DO_REQUEST;
}

static void set_geometry_intr (ide_dev_t *dev)
{
	byte stat = GET_STAT(DEV_HWIF);

	sti();
	if (!OK_STAT(stat,READY_STAT,BAD_STAT))
		ide_error(dev, "set_geometry_intr", stat);
	DO_REQUEST;
}

static void recal_intr (ide_dev_t *dev)
{
	byte stat = GET_STAT(DEV_HWIF);

	sti();
	if (!OK_STAT(stat,READY_STAT,BAD_STAT))
		ide_error(dev, "recal_intr", stat);
	DO_REQUEST;
}

static void drive_cmd_intr (ide_dev_t *dev)
{
	byte stat = GET_STAT(DEV_HWIF);

	sti();
	if (!OK_STAT(stat,READY_STAT,BAD_STAT))
		ide_error(dev, "drive_cmd", stat); /* calls end_drive_cmd() */
	else
		end_drive_cmd (dev, stat, GET_ERR(DEV_HWIF));
	DO_REQUEST;
}

static void timer_expiry (byte hwif)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	if (ide_handler[HWIF] == NULL || (timer_active & ide_timerbit[HWIF])) {
		/* The drive must have responded just as the timer expired */
		sti();
		printk("%s: marginal timeout\n", ide_name[HWIF]);
	} else {
		ide_handler[HWIF] = NULL;
		disable_irq(ide_irq[HWIF]);
#if SUPPORT_SERIALIZE
		if (single_threaded && ide_irq[HWIF] != ide_irq[HWIF^1])
			disable_irq(ide_irq[HWIF^1]);
#endif /* SUPPORT_SERIALIZE */
		sti();
		ide_error(ide_cur_dev[HWIF], "timeout", GET_STAT(HWIF));
		do_request(HWIF);
#if SUPPORT_SHARING_IRQ
		if (single_threaded)	/* this line is indeed necessary */
			hwif = current_hwif;
#endif /* SUPPORT_SHARING_IRQ */
		cli();
		start_ide_timer(HWIF);
		enable_irq(ide_irq[HWIF]);
#if SUPPORT_SERIALIZE
		if (single_threaded && ide_irq[HWIF] != ide_irq[HWIF^1])
			enable_irq(ide_irq[HWIF^1]);
#endif /* SUPPORT_SERIALIZE */
	}
	restore_flags(flags);
}

static void ide0_timer_expiry (void)		/* invoked from sched.c */
{
	timer_expiry (0);
}

static void ide1_timer_expiry (void)		/* invoked from sched.c */
{
	timer_expiry (1);
}

static int do_special (ide_dev_t *dev)
{
	special_t *s = &dev->special;
#ifdef DEBUG
	printk("%s: do_special: 0x%02x\n", dev->name, s->all);
#endif
	if (s->b.set_geometry) {
		s->b.set_geometry = 0;
		if (dev->type == disk) {
			OUT_BYTE(dev->sect,HD_SECTOR);
			OUT_BYTE(dev->cyl,HD_LCYL);
			OUT_BYTE(dev->cyl>>8,HD_HCYL);
			OUT_BYTE(((dev->head-1)|dev->select.all)&0xBF,HD_CURRENT);
			ide_cmd(dev, WIN_SPECIFY, dev->sect, &set_geometry_intr);
		}
	} else if (s->b.recalibrate) {
		s->b.recalibrate = 0;
		if (dev->type == disk)
			ide_cmd(dev,WIN_RESTORE,dev->sect,&recal_intr);
	} else if (s->b.set_multmode) {
		if (dev->type == disk) {
			if (dev->id && dev->mult_req > dev->id->max_multsect)
				dev->mult_req = dev->id->max_multsect;
			ide_cmd(dev,WIN_SETMULT,dev->mult_req,&set_multmode_intr);
		} else {
			dev->mult_req = 0;
			printk("%s: multmode not supported by this device\n", dev->name);
		}
		s->b.set_multmode = 0;
	} else {
		if (s->all) {
			printk("%s: bad special flag: 0x%02x\n", dev->name, s->all);
			s->all = 0;
		}
	}
	return (ide_handler[DEV_HWIF] == NULL) ? 1 : 0;
}

#ifdef CONFIG_BLK_DEV_IDECD
static byte wait_stat (ide_dev_t *dev, byte good, byte bad, unsigned long timeout)
{
	unsigned long flags;

	save_flags(flags);
	sti();
	WAIT_STAT(dev, good, bad, timeout, "status", error);
	restore_flags(flags);
	return 0;
error:
	restore_flags(flags);
	return 1;
}

#include "ide-cd.c"
#endif	/* CONFIG_BLK_DEV_IDECD */

static inline int do_rw_disk (ide_dev_t *dev, struct request *rq, unsigned long block)
{
	OUT_BYTE(dev->ctl,HD_CMD);
	OUT_BYTE(rq->nr_sectors,HD_NSECTOR);
	if (dev->select.b.lba) {
#ifdef DEBUG
		printk("%s: %sing: LBAsect=%ld, sectors=%ld, buffer=0x%08lx\n",
			dev->name, (rq->cmd==READ)?"read":"writ", 
			block, rq->nr_sectors, (unsigned long) rq->buffer);
#endif
		OUT_BYTE(block,HD_SECTOR);
		OUT_BYTE(block>>=8,HD_LCYL);
		OUT_BYTE(block>>=8,HD_HCYL);
		OUT_BYTE(((block>>8)&0x0f)|dev->select.all,HD_CURRENT);
	} else {
		unsigned int sect,head,cyl,track;
		track = block / dev->sect;
		sect  = block % dev->sect + 1;
		OUT_BYTE(sect,HD_SECTOR);
		head  = track % dev->head;
		cyl   = track / dev->head;
		OUT_BYTE(cyl,HD_LCYL);
		OUT_BYTE(cyl>>8,HD_HCYL);
		OUT_BYTE(head|dev->select.all,HD_CURRENT);
#ifdef DEBUG
		printk("%s: %sing: CHS=%d/%d/%d, sectors=%ld, buffer=0x%08lx\n",
			dev->name, (rq->cmd==READ)?"read":"writ", cyl,
			head, sect, rq->nr_sectors, (unsigned long) rq->buffer);
#endif
	}
	if (rq->cmd == READ) {
		OUT_BYTE(dev->mult_count ? WIN_MULTREAD : WIN_READ, HD_COMMAND);
		ide_handler[DEV_HWIF] = &read_intr;
		return 0;
	}
	if (rq->cmd == WRITE) {
		OUT_BYTE(dev->wpcom,HD_PRECOMP);	/* for ancient drives */
		OUT_BYTE(dev->mult_count ? WIN_MULTWRITE : WIN_WRITE, HD_COMMAND);
		WAIT_STAT(dev, DATA_READY, BAD_W_STAT, WAIT_DRQ, "DRQ", error);
		if (!dev->unmask)
			cli();
		if (dev->mult_count) {
			ide_write_rq[DEV_HWIF] = *rq; /* scratchpad */
			multwrite(dev);
			ide_handler[DEV_HWIF] = &multwrite_intr;
		} else {
			output_ide_data(dev, rq->buffer, SECTOR_WORDS);
			ide_handler[DEV_HWIF] = &write_intr;
		}
		return 0;
	}
#ifdef IDE_DRIVE_CMD
	if (rq->cmd == IDE_DRIVE_CMD) {
		byte *args = rq->buffer;
		if (args) {
			OUT_BYTE(args[2],HD_FEATURE);
			ide_cmd(dev, args[0], args[1], &drive_cmd_intr);
			printk("%s: DRIVE_CMD cmd=0x%02x sc=0x%02x fr=0x%02x\n",
			 dev->name, args[0], args[1], args[2]);
			return 0;
		} else {
#ifdef DEBUG
			printk("%s: DRIVE_CMD (null)\n", dev->name);
#endif
			end_drive_cmd(dev,GET_STAT(DEV_HWIF),GET_ERR(DEV_HWIF));
			return 1;
		}
	}
#endif	/* IDE_DRIVE_CMD */
	printk("%s: bad command: %d\n", dev->name, rq->cmd);
	end_request(0, DEV_HWIF);
error:
	return 1;
}

/*
 * The driver enables interrupts as much as possible.  In order to do this,
 * (a) the device-interrupt is always masked before entry, and
 * (b) the timeout-interrupt is always disabled before entry.
 *
 * Interrupts are still masked (by default) whenever we are exchanging
 * data/cmds with a drive, because some drives seem to have very poor
 * tolerance for latency during I/O.  For devices which don't suffer from
 * this problem (most don't), the ide_dev[][].unmask flag can be set to permit
 * other interrupts during data/cmd transfers by using the "hdparm" utility.
 */
static void do_request (byte hwif)
{
	unsigned int minor, drive;
	unsigned long block, blockend;
	struct request *rq;
	ide_dev_t *dev;
repeat:
	sti();
#if SUPPORT_SHARING_IRQ
	current_hwif = hwif;	/* used *only* when single_threaded==1 */
#endif /* SUPPORT_SHARING_IRQ */
	if ((rq = ide_cur_rq[HWIF]) == NULL) {
		rq = blk_dev[ide_major[HWIF]].current_request;
		if ((rq == NULL) || (rq->dev < 0)) {
#if SUPPORT_SHARING_IRQ
			if (single_threaded) {
				if (sharing_single_irq && (dev = ide_cur_dev[hwif])) /* disable irq */
					OUT_BYTE(dev->ctl|2,HD_CMD);
				rq = blk_dev[ide_major[hwif^=1]].current_request;
				if ((rq != NULL) && (rq->dev >= 0))
					goto repeat;
			}
#endif /* SUPPORT_SHARING_IRQ */
			return;
		}
		blk_dev[ide_major[HWIF]].current_request = rq->next;
		ide_cur_rq[HWIF] = rq;
	}
#ifdef DEBUG
	printk("%s: do_request: current=0x%08lx\n",ide_name[HWIF],(unsigned long)rq);
#endif
	minor = MINOR(rq->dev);
	drive = minor >> PARTN_BITS;
	ide_cur_dev[HWIF] = dev = &ide_dev[HWIF][drive];
	if ((MAJOR(rq->dev) != ide_major[HWIF]) || (drive >= MAX_DRIVES)) {
		printk("%s: bad device number: 0x%04x\n", ide_name[HWIF], rq->dev);
		end_request(0, HWIF);
		goto repeat;
	}
	if (rq->bh && !rq->bh->b_lock) {
		printk("%s: block not locked\n", ide_name[HWIF]);
		end_request(0, HWIF);
		goto repeat;
	}
	block    = rq->sector;
	blockend = block + rq->nr_sectors;
	if ((blockend < block) || (blockend > ide_hd[HWIF][minor].nr_sects)) {
		printk("%s: bad access: block=%ld, count=%ld\n",
			dev->name, block, rq->nr_sectors);
		end_request(0, HWIF);
		goto repeat;
	}
	block += ide_hd[HWIF][minor].start_sect;
#if (DISK_RECOVERY_TIME > 0)
	while ((read_timer() - ide_lastreq[HWIF]) < DISK_RECOVERY_TIME);
#endif
	OUT_BYTE(dev->select.all,HD_CURRENT);
#ifdef CONFIG_BLK_DEV_IDECD
	WAIT_STAT(dev, (dev->type == cdrom) ? 0 : READY_STAT,
		BUSY_STAT|DRQ_STAT, WAIT_READY, "DRDY", repeat);
#else
	WAIT_STAT(dev, READY_STAT, BUSY_STAT|DRQ_STAT, WAIT_READY, "DRDY", repeat);
#endif	/* CONFIG_BLK_DEV_IDECD */
	if (!dev->special.all) {
#ifdef CONFIG_BLK_DEV_IDECD
		if (dev->type == disk) {
#endif	/* CONFIG_BLK_DEV_IDECD */
			if (do_rw_disk(dev, rq, block))
				goto repeat;
#ifdef CONFIG_BLK_DEV_IDECD
		} else {
			if (do_rw_cdrom(dev, block))
				goto repeat;
		}
#endif	/* CONFIG_BLK_DEV_IDECD */
	} else {
		if (do_special(dev))
			goto repeat;
	}
}

/*
 * This is a macro rather than an inline function to
 * prevent gcc from over-optimizing accesses to current_hwif,
 * which may have a different value on exit from do_request().
 */
#define DO_IDE_REQUEST(hwif)			\
{						\
	if (ide_handler[hwif] == NULL) {	\
		disable_irq(ide_irq[hwif]);	\
		if (single_threaded && ide_irq[hwif] != ide_irq[hwif^1]) \
			disable_irq(ide_irq[hwif^1]); \
		do_request(hwif);		\
		cli();				\
		start_ide_timer(hwif);		\
		enable_irq(ide_irq[hwif]);	\
		if (single_threaded && ide_irq[hwif] != ide_irq[hwif^1]) \
			enable_irq(ide_irq[hwif^1]); \
	}					\
}

#if SUPPORT_TWO_INTERFACES
static void do_ide0_request (void)	/* invoked with cli() */
{
	DO_IDE_REQUEST(0);
}

static void do_ide1_request (void)	/* invoked with cli() */
{
	DO_IDE_REQUEST(1);
}
#else
#define do_ide1_request	do_ide0_request
static void do_ide0_request (void)	/* invoked with cli() */
{
	DO_IDE_REQUEST(HWIF);
}
#endif	/* SUPPORT_TWO_INTERFACES */

#if SUPPORT_SHARING_IRQ
static void do_shared_request (void)	/* invoked with cli() */
{
	DO_IDE_REQUEST(current_hwif);
}
#endif /* SUPPORT_SHARING_IRQ */

/*
 * There's nothing really useful we can do with an unexpected interrupt,
 * other than reading the status register (to clear it), and logging it.
 * There should be no way that an irq can happen before we're ready for it,
 * so we needn't worry much about losing an "important" interrupt here.
 *
 * On laptops (and "green" PCs), an unexpected interrupt occurs whenever the
 * drive enters "idle", "standby", or "sleep" mode, so if the status looks
 * "good", we just ignore the interrupt completely.
 */
static void unexpected_intr (byte hwif)
{
	byte stat;

	if (!OK_STAT(stat=GET_STAT(HWIF), DRIVE_READY, BAD_STAT))
		(void) dump_status(HWIF, "unexpected_intr", stat);
	outb_p(2,IDE_PORT(HD_CMD,hwif));	/* disable device irq */
#if SUPPORT_SHARING_IRQ
	if (single_threaded && ide_irq[hwif] == ide_irq[hwif^1]) {
		if (!OK_STAT(stat=GET_STAT(hwif^1), DRIVE_READY, BAD_STAT))
			(void) dump_status(hwif^1, "unexpected_intr", stat);
		outb_p(2,IDE_PORT(HD_CMD,hwif^1));	/* disable device irq */
	}
#endif /* SUPPORT_SHARING_IRQ */
}

/*
 * This is a macro rather than an inline function to
 * prevent gcc from over-optimizing accesses to current_hwif,
 * which may have a different value on exit from handler().
 */
#define IDE_INTR(hwif)					\
{							\
	ide_dev_t *dev;					\
	void (*handler)(ide_dev_t *);			\
							\
	timer_active &= ~ide_timerbit[hwif];		\
	if ((handler = ide_handler[hwif]) != NULL) {	\
		ide_handler[hwif] = NULL;		\
		dev = ide_cur_dev[hwif];		\
		if (dev->unmask)			\
			sti();				\
		handler(dev);				\
	} else						\
		unexpected_intr(hwif);			\
	cli();						\
}

#if SUPPORT_SERIALIZE
/* entry point for all interrupts when single_threaded==1 */
static void ide_seq_intr (int irq, struct pt_regs *regs)
{
	byte hwif = (irq != ide_irq[0]);
	IDE_INTR(HWIF);
	start_ide_timer(current_hwif);
}
#endif /* SUPPORT_SERIALIZE */

#if OPTIMIZE_IRQS

/* entry point for all interrupts on ide0 when single_threaded==0 */
static void ide0_intr (int irq, struct pt_regs *regs)
{
	IDE_INTR(0);
	start_ide_timer(0);
}

/* entry point for all interrupts on ide1 when single_threaded==0 */
static void ide1_intr (int irq, struct pt_regs *regs)
{
	IDE_INTR(1);
	start_ide_timer(1);
}

#else	/* OPTIMIZE_IRQS */

#define ide0_intr	ide_intr
#define ide1_intr	ide_intr

/* entry point for all interrupts when single_threaded==0 */
static void ide_intr (int irq, struct pt_regs *regs)
{
#if SUPPORT_TWO_INTERFACES
	byte hwif = (irq != ide_irq[0]);
#endif	/* SUPPORT_TWO_INTERFACES */
	IDE_INTR(HWIF);
	start_ide_timer(HWIF);
}

#endif	/* OPTIMIZE_IRQS */

#if SUPPORT_SHARING_IRQ
/* entry point for all interrupts on ide0/ide1 when sharing_single_irq==1 */
static void ide_shared_intr (int irq, struct pt_regs * regs)
{
	IDE_INTR(current_hwif);
	start_ide_timer(current_hwif);
}
#endif /* SUPPORT_SHARING_IRQ */

static ide_dev_t *get_info_ptr (int i_rdev)
{
	unsigned int drive = DEVICE_NR(i_rdev);
	ide_dev_t *dev;

	if (drive < MAX_DRIVES) {
		switch (MAJOR(i_rdev)) {
			case IDE0_MAJOR:	dev = &ide_dev[0][drive];
						if (dev->present) return dev;
						break;
			case IDE1_MAJOR:	dev = &ide_dev[1][drive];
						if (dev->present) return dev;
						break;
		}
	}
	return NULL;
}

static int ide_open(struct inode * inode, struct file * filp)
{
	ide_dev_t *dev;
	unsigned long flags;

	if ((dev = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENODEV;
	save_flags(flags);
	cli();
	while (dev->busy)
		sleep_on(&dev->wqueue);
	dev->usage++;
	restore_flags(flags);
#ifdef CONFIG_BLK_DEV_IDECD
	if (dev->type == cdrom)
		return cdrom_open (inode, filp, dev);
#endif	/* CONFIG_BLK_DEV_IDECD */
	return 0;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static void ide_release(struct inode * inode, struct file * file)
{
	ide_dev_t *dev;

	if ((dev = get_info_ptr(inode->i_rdev)) != NULL) {
		sync_dev(inode->i_rdev);
		dev->usage--;
#ifdef CONFIG_BLK_DEV_IDECD
		if (dev->type == cdrom)
			cdrom_release (inode, file, dev);
#endif	/* CONFIG_BLK_DEV_IDECD */
	}
}

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
static int revalidate_disk(int i_rdev)
{
	unsigned int i, major, start, drive = DEVICE_NR(i_rdev);
	ide_dev_t *dev;
	struct gendisk *gd;
	long flags;

	if ((dev = get_info_ptr(i_rdev)) == NULL)
		return -ENODEV;

	save_flags(flags);
	cli();
	if (dev->busy || (dev->usage > 1)) {
		restore_flags(flags);
		return -EBUSY;
	};
	dev->busy = 1;
	restore_flags(flags);

	gd    = &ide_gendisk[DEV_HWIF];
	major = ide_major[DEV_HWIF] << 8;
	start = drive << PARTN_BITS;

	for (i = 0; i < (1<<PARTN_BITS); i++) {
		unsigned int minor = start + i;
		sync_dev           (major | minor);
		invalidate_inodes  (major | minor);
		invalidate_buffers (major | minor);
		gd->part[minor].start_sect = 0;
		gd->part[minor].nr_sects   = 0;
	};

	gd->part[start].nr_sects = ide_capacity[DEV_HWIF][drive];
	resetup_one_dev(gd, drive);

	dev->busy = 0;
	wake_up(&dev->wqueue);
	return 0;
}

#ifdef IDE_DRIVE_CMD
/*
 * This function issues a specific IDE drive command onto the
 * tail of the request queue, and waits for it to be completed.
 * If arg is NULL, it goes through all the motions,
 * but without actually sending a command to the drive.
 */
static int do_drive_cmd(int dev, char *args)
{
	unsigned long flags;
	unsigned int major = MAJOR(dev);
	struct request rq, *cur_rq;
	struct blk_dev_struct *bdev;
	struct semaphore sem = MUTEX_LOCKED;

	/* build up a special request, and add it to the queue */
	rq.buffer = args;
	rq.cmd = IDE_DRIVE_CMD;
	rq.errors = 0;
	rq.sector = 0;
	rq.nr_sectors = 0;
	rq.current_nr_sectors = 0;
	rq.sem = &sem;
	rq.bh = NULL;
	rq.bhtail = NULL;
	rq.next = NULL;
	rq.dev = dev;
	bdev = &blk_dev[major];

	save_flags(flags);
	cli();
	cur_rq = bdev->current_request;
	if (cur_rq == NULL) {			/* empty request list? */
		bdev->current_request = &rq;	/* service ours immediately */
		bdev->request_fn();
	} else {
		while (cur_rq->next != NULL)	/* find end of request list */
			cur_rq = cur_rq->next;
		cur_rq->next = &rq;		/* add rq to the end */
	}

	down(&sem);				/* wait for it to be serviced */
	restore_flags(flags);
	return rq.errors ? -EIO : 0;		/* return -EIO if errors */
}
#endif	/* IDE_DRIVE_CMD */

static int write_fs_long (unsigned long useraddr, long value)
{
	int err;

	if (NULL == (long *)useraddr)
		return -EINVAL;
	if ((err = verify_area(VERIFY_WRITE, (long *)useraddr, sizeof(long))))
		return err;
	put_fs_long((unsigned)value, (long *) useraddr);
	return 0;
}

static int ide_ioctl (struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct hd_geometry *loc = (struct hd_geometry *) arg;
	int err;
	ide_dev_t *dev;
	unsigned long flags;

	if (!inode || !inode->i_rdev)
		return -EINVAL;
	if ((dev = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENODEV;
	switch (cmd) {
		case HDIO_GETGEO:
			if (!loc || dev->type != disk) return -EINVAL;
			err = verify_area(VERIFY_WRITE, loc, sizeof(*loc));
			if (err) return err;
			put_fs_byte(dev->bios_head,
				(char *) &loc->heads);
			put_fs_byte(dev->bios_sect,
				(char *) &loc->sectors);
			put_fs_word(dev->bios_cyl,
				(short *) &loc->cylinders);
			put_fs_long((unsigned)ide_hd[DEV_HWIF][MINOR(inode->i_rdev)].start_sect,
				(long *) &loc->start);
			return 0;

		case BLKFLSBUF:
			if(!suser()) return -EACCES;
			fsync_dev(inode->i_rdev);
			invalidate_buffers(inode->i_rdev);
			return 0;

		case BLKRASET:
			if(!suser()) return -EACCES;
			if(arg > 0xff) return -EINVAL;
			read_ahead[MAJOR(inode->i_rdev)] = arg;
			return 0;

		case BLKRAGET:
			return write_fs_long(arg, read_ahead[MAJOR(inode->i_rdev)]);

         	case BLKGETSIZE:   /* Return device size */
			return write_fs_long(arg, ide_hd[DEV_HWIF][MINOR(inode->i_rdev)].nr_sects);
		case BLKRRPART: /* Re-read partition tables */
			return revalidate_disk(inode->i_rdev);

                case HDIO_GET_KEEPSETTINGS:
			return write_fs_long(arg, dev->keep_settings);

                case HDIO_GET_UNMASKINTR:
			return write_fs_long(arg, dev->unmask);

                case HDIO_GET_CHIPSET:
			return write_fs_long(arg, dev->chipset);

                case HDIO_GET_MULTCOUNT:
			return write_fs_long(arg, dev->mult_count);

		case HDIO_GET_IDENTITY:
			if (!arg || (MINOR(inode->i_rdev) & PARTN_MASK))
				return -EINVAL;
			if (dev->id == NULL)
				return -ENOMSG;
			err = verify_area(VERIFY_WRITE, (char *)arg, sizeof(*dev->id));
			if (err) return err;
			memcpy_tofs((char *)arg, (char *)dev->id, sizeof(*dev->id));
			return 0;

		case HDIO_SET_KEEPSETTINGS:
		case HDIO_SET_UNMASKINTR:
			if (!suser()) return -EACCES;
			if ((arg > 1) || (MINOR(inode->i_rdev) & PARTN_MASK))
				return -EINVAL;
			save_flags(flags);
			cli();
			if (cmd == HDIO_SET_KEEPSETTINGS)
				dev->keep_settings = arg;
			else
				dev->unmask = arg;
			restore_flags(flags);
			return 0;

		case HDIO_SET_CHIPSET:
			if (!suser()) return -EACCES;
			if ((arg > 3) || (MINOR(inode->i_rdev) & PARTN_MASK))
				return -EINVAL;
			save_flags(flags);
			cli();
			dev->chipset   = arg;
			dev->vlb_sync  = (arg & 2) >> 1;
			dev->vlb_32bit = (arg & 1);
			restore_flags(flags);
			return 0;

		case HDIO_SET_MULTCOUNT:
			if (!suser()) return -EACCES;
			if (MINOR(inode->i_rdev) & PARTN_MASK)
				return -EINVAL;
			if ((dev->id != NULL) && (arg > dev->id->max_multsect))
				return -EINVAL;
			save_flags(flags);
			cli();
			if (dev->special.b.set_multmode) {
				restore_flags(flags);
				return -EBUSY;
			}
			dev->mult_req = arg;
			dev->special.b.set_multmode = 1;
			restore_flags(flags);
#ifdef IDE_DRIVE_CMD
			do_drive_cmd (inode->i_rdev, NULL);
			return (dev->mult_count == arg) ? 0 : -EIO;
#else
			return 0;
#endif	/* IDE_DRIVE_CMD */

#ifdef IDE_DRIVE_CMD
		case HDIO_DRIVE_CMD:
		{
			unsigned long args;

			if (NULL == (long *) arg)
				err = do_drive_cmd(inode->i_rdev,NULL);
			else {
				if (!(err = verify_area(VERIFY_WRITE,(long *)arg,sizeof(long))))
				{
					args = get_fs_long((long *)arg);
					err = do_drive_cmd(inode->i_rdev,(char *)&args);
					put_fs_long(args,(long *)arg);
				}
			}
			return err;
		}
#endif /* IDE_DRIVE_CMD */

		RO_IOCTLS(inode->i_rdev, arg);

		default:
#ifdef CONFIG_BLK_DEV_IDECD
			if (dev->type == cdrom)
				return ide_cdrom_ioctl(dev, inode, file, cmd, arg);
#endif /* CONFIG_BLK_DEV_IDECD */
			return -EPERM;
	}
}

#ifdef CONFIG_BLK_DEV_IDECD
static int ide_check_media_change (dev_t full_dev)
{
	ide_dev_t *dev;

	if ((dev = get_info_ptr(full_dev)) == NULL)
		return -ENODEV;
	if (dev->type != cdrom)
		return 0;
	return cdrom_check_media_change (dev);
}
#endif	/* CONFIG_BLK_DEV_IDECD */


static void fixstring (byte *s, int bytecount, int byteswap)
{
	byte *p, *end = &s[bytecount &= ~1];	/* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to little-endian */
		for (p = end ; p != s;) {
			unsigned short *pp = (unsigned short *) (p -= 2);
			*pp = (*pp >> 8) | (*pp << 8);
		}
	}
	p = s;

	/* strip leading blanks */
	while (s != end && *s == ' ')
		++s;

	/* compress internal blanks and strip trailing blanks */
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}

	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}

static int lba_capacity_is_ok (struct hd_driveid *id)
/*
 * Returns:	1 if lba_capacity looks sensible
 *		0 otherwise
 */
{
	unsigned long lba_sects   = id->lba_capacity;
	unsigned long chs_sects   = id->cyls * id->heads * id->sectors;
	unsigned long _10_percent = chs_sects / 10;

	/* perform a rough sanity check on lba_sects:  within 10% is "okay" */
	if ((lba_sects - chs_sects) < _10_percent)
		return 1;	/* lba_capacity is good */

	/* some drives have the word order reversed */
	lba_sects = (lba_sects << 16) | (lba_sects >> 16);
	if ((lba_sects - chs_sects) < _10_percent) {
		id->lba_capacity = lba_sects;	/* fix it */
		return 1;	/* lba_capacity is (now) good */
	}
	return 0;	/* lba_capacity value is bad */
}

static unsigned long probe_mem_start;	/* used by drive/irq probing routines */

static void do_identify (ide_dev_t *dev, byte cmd)
{
	int bswap;
	struct hd_driveid *id;
	unsigned long capacity, check;

	id = dev->id = (struct hd_driveid *) probe_mem_start; /* kmalloc() */
	probe_mem_start += 512;
	input_ide_data(dev, id, SECTOR_WORDS);	/* read 512 bytes of id info */
	sti();

	/*
	 * EATA SCSI controllers do a hardware ATA emulation:  ignore them
	 */
	if ((id->model[0] == 'P' && id->model[1] == 'M')
	 || (id->model[0] == 'S' && id->model[1] == 'K')) {
		printk("%s: EATA SCSI HBA %.10s\n", dev->name, id->model);
		dev->present = 0;
		return;
	}

	/*
	 *  WIN_IDENTIFY returns little-endian info,
	 *  WIN_PIDENTIFY *usually* returns little-endian info.
	 */
	bswap = 1;
	if (cmd == WIN_PIDENTIFY) {
		if ((id->model[0] == 'N' && id->model[1] == 'E')
		 || (id->model[0] == 'F' && id->model[1] == 'X'))
			bswap = 0;	/* NEC and *some* Mitsumi units */
	}				/* Vertos drives may still be weird */
	fixstring (id->model,     sizeof(id->model),     bswap);
	fixstring (id->fw_rev,    sizeof(id->fw_rev),    bswap);
	fixstring (id->serial_no, sizeof(id->serial_no), bswap);

	/*
	 * Check for an ATAPI device
	 */
	if (cmd == WIN_PIDENTIFY) {
#ifdef CONFIG_BLK_DEV_IDECD
		byte type = (id->config >> 8) & 0x0f;
#endif	/* CONFIG_BLK_DEV_IDECD */
		printk("%s: %s, ATAPI,", dev->name, id->model);
#ifdef CONFIG_BLK_DEV_IDECD
		if (type == 0 || type == 5)
			printk(" CDROM drive\n");
		else
			printk(" UNKNOWN device\n");
		dev->type = cdrom;	/* until we do it "correctly" above */
		dev->present = 1;
#else
		printk(unsupported);
#endif	/* CONFIG_BLK_DEV_IDECD */
		return;
	}

	dev->type = disk;
	/* Extract geometry if we did not already have one for the drive */
	if (!dev->present) {
		dev->present = 1;
		dev->cyl     = dev->bios_cyl  = id->cyls;
		dev->head    = dev->bios_head = id->heads;
		dev->sect    = dev->bios_sect = id->sectors; 
	}
	/* Handle logical geometry translation by the drive */
	if ((id->field_valid & 1) && id->cur_cyls && id->cur_heads
	 && (id->cur_heads <= 16) && id->cur_sectors)
	{
		/*
		 * Extract the physical drive geometry for our use.
		 * Note that we purposely do *not* update the bios info.
		 * This way, programs that use it (like fdisk) will 
		 * still have the same logical view as the BIOS does,
		 * which keeps the partition table from being screwed.
		 *
		 * An exception to this is the cylinder count,
		 * which we reexamine later on to correct for 1024 limitations.
		 */
		dev->cyl  = id->cur_cyls;
		dev->head = id->cur_heads;
		dev->sect = id->cur_sectors;
		capacity  = dev->cyl * dev->head * dev->sect;

		/* check for word-swapped "capacity" field in id information */
		check = (id->cur_capacity0 << 16) | id->cur_capacity1;
		if (check == capacity)		/* was it swapped? */
			*((int *)&id->cur_capacity0) = capacity; /* fix it */
	}
	/* Use physical geometry if what we have still makes no sense */
	if ((!dev->head || dev->head > 16) && id->heads && id->heads <= 16) {
		dev->cyl  = id->cyls;
		dev->head = id->heads;
		dev->sect = id->sectors; 
	}
	/* Correct the number of cyls if the bios value is too small */
	if (dev->sect == dev->bios_sect && dev->head == dev->bios_head) {
		if (dev->cyl > dev->bios_cyl)
			dev->bios_cyl = dev->cyl;
	}
	/* Determine capacity, and use LBA if the drive properly supports it */
	if ((id->capability & 2) && lba_capacity_is_ok(id)) {
		dev->select.b.lba = 1;
		capacity = id->lba_capacity;
	} else {
		capacity = dev->cyl * dev->head * dev->sect;
	}

	ide_capacity[DEV_HWIF][dev->select.b.drive] = capacity;
	printk ("%s: %.40s, %ldMB w/%dKB Cache, %sCHS=%d/%d/%d",
	 dev->name, id->model, capacity/2048L, id->buf_size/2,
	 dev->select.b.lba ? "LBA, " : "",
	 dev->bios_cyl, dev->bios_head, dev->bios_sect);

	dev->mult_count = 0;
	if (id->max_multsect) {
		dev->mult_req = INITIAL_MULT_COUNT;
		if (dev->mult_req > id->max_multsect)
			dev->mult_req = id->max_multsect;
		if (dev->mult_req || ((id->multsect_valid & 1) && id->multsect))
			dev->special.b.set_multmode = 1;
		printk(", MaxMult=%d", id->max_multsect);
	}
	printk("\n");
}

static void delay_10ms (void)
{
	unsigned long timer = jiffies + 2;
	while (timer > jiffies);
}


static int try_to_identify (ide_dev_t *dev, byte cmd)
/*
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 */
{
	int hd_status, rc;
	unsigned long timeout;
#if PROBE_FOR_IRQS
	int irqs = 0;
	static byte irq_probed[2] = {0,0};
#endif	/* PROBE_FOR_IRQS */

	OUT_BYTE(dev->ctl|2,HD_CMD);		/* disable device irq */
#if PROBE_FOR_IRQS
	if (!irq_probed[DEV_HWIF]) {		/* already probed for IRQ? */
		irqs = probe_irq_on();		/* start monitoring irqs */
		OUT_BYTE(dev->ctl,HD_CMD);	/* enable device irq */
	}
#endif	/* PROBE_FOR_IRQS */
	delay_10ms();				/* take a deep breath */
	if ((IN_BYTE(HD_ALTSTATUS,DEV_HWIF) ^ IN_BYTE(HD_STATUS,DEV_HWIF)) & ~INDEX_STAT) {
		hd_status = HD_STATUS;		/* an ancient Seagate drive */
		printk("%s: probing with STATUS instead of ALTSTATUS\n", dev->name);
	} else
		hd_status = HD_ALTSTATUS;	/* use non-intrusive polling */
	OUT_BYTE(cmd,HD_COMMAND);		/* ask drive for ID */
	timeout = ((cmd == WIN_IDENTIFY) ? WAIT_WORSTCASE : WAIT_PIDENTIFY) / 2;
	timeout += jiffies;
	do {
		if (jiffies > timeout) {
#if PROBE_FOR_IRQS
			if (!irq_probed[DEV_HWIF])
				(void) probe_irq_off(irqs);
#endif	/* PROBE_FOR_IRQS */
			return 1;	/* drive timed-out */
		}
		delay_10ms();		/* give drive a breather */
	} while (IN_BYTE(hd_status,DEV_HWIF) & BUSY_STAT);
	delay_10ms();		/* wait for IRQ and DRQ_STAT */
	if (OK_STAT(GET_STAT(DEV_HWIF),DRQ_STAT,BAD_R_STAT)) {
		cli();			/* some systems need this */
		do_identify(dev, cmd);	/* drive returned ID */
		rc = 0;			/* success */
	} else
		rc = 2;			/* drive refused ID */
#if PROBE_FOR_IRQS
	if (!irq_probed[DEV_HWIF]) {
		irqs = probe_irq_off(irqs);	/* get irq number */
		if (irqs > 0) {
			irq_probed[DEV_HWIF] = 1;
			ide_irq[DEV_HWIF] = irqs;
		} else				/* Mmmm.. multiple IRQs */
			printk("%s: IRQ probe failed (%d)\n", dev->name, irqs);
	}
#endif	/* PROBE_FOR_IRQS */
	return rc;
}

/*
 * This routine has the difficult job of finding a drive if it exists,
 * without getting hung up if it doesn't exist, without trampling on
 * ethernet cards, and without leaving any IRQs dangling to haunt us later.
 *
 * If a drive is "known" to exist (from CMOS or kernel parameters),
 * but does not respond right away, the probe will "hang in there"
 * for the maximum wait time (about 30 seconds), otherwise it will
 * exit much more quickly.
 */
static int do_probe (ide_dev_t *dev, byte cmd)
/*
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 *		3  bad status from device (possible for ATAPI drives)
 *		4  probe was not attempted
 */
{
	int rc;

#ifdef CONFIG_BLK_DEV_IDECD
	if (dev->present) {	/* avoid waiting for inappropriate probes */
		if ((dev->type == disk) ^ (cmd == WIN_IDENTIFY))
			return 4;
	}
#endif	/* CONFIG_BLK_DEV_IDECD */
#if DEBUG
	printk("probing for %s: present=%d, type=%s, probetype=%s\n",
		dev->name, dev->present, dev->type ? "cdrom":"disk",
		(cmd == WIN_IDENTIFY) ? "ATA" : "ATAPI");
#endif
	OUT_BYTE(dev->select.all,HD_CURRENT);	/* select target drive */
	delay_10ms();				/* wait for BUSY_STAT */
	if (IN_BYTE(HD_CURRENT,DEV_HWIF) != dev->select.all && !dev->present) {
		OUT_BYTE(0xa0,HD_CURRENT);	/* exit with drive0 selected */
		return 3;    /* no i/f present: avoid killing ethernet cards */
	}

	if (OK_STAT(GET_STAT(DEV_HWIF),READY_STAT,BUSY_STAT)
	 || dev->present || cmd == WIN_PIDENTIFY)
	{
		if ((rc = try_to_identify(dev, cmd)))  /* send cmd and wait */
			rc = try_to_identify(dev, cmd);	/* failed: try again */
		if (rc == 1)
			printk("%s: no response (status = 0x%02x)\n",
			 dev->name, GET_STAT(DEV_HWIF));
		OUT_BYTE(dev->ctl|2,HD_CMD);	/* disable device irq */
		delay_10ms();
		(void) GET_STAT(DEV_HWIF);	/* ensure drive irq is clear */
	} else {
		rc = 3;				/* not present or maybe ATAPI */
	}
	if (dev->select.b.drive == 1) {
		OUT_BYTE(0xa0,HD_CURRENT);	/* exit with drive0 selected */
		delay_10ms();
		OUT_BYTE(dev->ctl|2,HD_CMD);	/* disable device irq */
		delay_10ms();
		(void) GET_STAT(DEV_HWIF);	/* ensure drive irq is clear */
	}
	return rc;
}

static byte probe_for_drive (ide_dev_t *dev)
/*
 * Returns:	0  no device was found
 *		1  device was found (note: dev->present might still be 0)
 */
{
	if (dev->dont_probe)			/* skip probing? */
		return dev->present;
	if (do_probe(dev, WIN_IDENTIFY) >= 2) {	/* if !(success || timed-out) */
#ifdef CONFIG_BLK_DEV_IDECD
		(void) do_probe(dev, WIN_PIDENTIFY); /* look for ATAPI device */
#endif	/* CONFIG_BLK_DEV_IDECD */
	}
	if (!dev->present)
		return 0;			/* drive not found */
	if (dev->id == NULL) {			/* identification failed? */
		if (dev->type == disk) {
			printk ("%s: non-IDE device, CHS=%d/%d/%d\n",
			 dev->name, dev->cyl, dev->head, dev->sect);
		}
#ifdef CONFIG_BLK_DEV_IDECD
		else if (dev->type == cdrom) {
			printk("%s: ATAPI cdrom (?)\n", dev->name);
		}
#endif	/* CONFIG_BLK_DEV_IDECD */
		else {
			dev->present = 0;	/* nuke it */
			return 1;		/* drive was found */
		}
	}
#ifdef CONFIG_BLK_DEV_IDECD
	if (dev->type == cdrom)
		cdrom_setup(dev);
#endif	/* CONFIG_BLK_DEV_IDECD */
	if (dev->type == disk && !dev->select.b.lba) {
		if (!dev->head || dev->head > 16) {
			printk("%s: cannot handle disk with %d physical heads\n",
			 dev->name, dev->head);
			dev->present = 0;
		}
	}
	return 1;	/* drive was found */
}

static void probe_for_drives (byte hwif)
{
	ide_dev_t *devs = &ide_dev[HWIF][0];	/* for convenience */

	if (check_region(IDE_PORT(HD_DATA,HWIF),8)
	 || check_region(IDE_PORT(HD_CMD,HWIF),1))
	{
		if (devs[0].present || devs[1].present)
			printk("ERROR: ");
		printk("%s: port(s) already in use\n", ide_name[HWIF]);
		devs[0].present = 0;
		devs[1].present = 0;
	} else {
		unsigned long flags;
		save_flags(flags);
		sti();	/* needed for jiffies and irq probing */

		/* second drive should only exist if first drive was found */
		if (probe_for_drive(&devs[0]) || devs[1].present)
			(void) probe_for_drive(&devs[1]);
#if PROBE_FOR_IRQS
		(void) probe_irq_off(probe_irq_on()); /* clear dangling irqs */
#endif	/* PROBE_FOR_IRQS */
		if (devs[0].present || devs[1].present) {
			request_region(IDE_PORT(HD_DATA,HWIF),8,ide_name[HWIF]);
			request_region(IDE_PORT(HD_CMD,HWIF),1,ide_name[HWIF]);
		}
		restore_flags(flags);
	}
}

static int next_drive = 0;	/* used by the ide_setup() routines below */

void ide_setup(char *str, int *ints)
{
	ide_dev_t *dev;
	const char *p[] = {"cyls","heads","sects","wpcom","irq"};
	int i, hwif, drive = next_drive++;
#ifdef CONFIG_BLK_DEV_HD
	extern void hd_setup(char *, int *);

	if (drive < 2) {
		hd_setup (str, ints);
		return;
	}
#endif /* CONFIG_BLK_DEV_HD */
	hwif = (drive > 1);
	printk("%s: ", ide_name[hwif]);
	if (drive > 3) {
		printk("too many drives defined\n");
		return;
	}
	drive = drive & 1;
	printk("%s: ", ide_devname[hwif][drive]);
	if (!SUPPORT_TWO_INTERFACES && hwif != HWIF) {
		printk(unsupported);
		return;
	}
	dev = &ide_dev[hwif][drive];
	if (dev->present)
		printk("(redefined) ");
	if (ints[0] == 0) {
#if SUPPORT_DTC2278
		if (!strcmp(str,"dtc2278")) {
			printk("%s\n",str);
			probe_dtc2278 = 1;	/* try to init DTC-2278 at boot */
			return;
		}
#endif /* SUPPORT_DTC2278 */
#if SUPPORT_SERIALIZE
		if (!strcmp(str,"serialize") || !strcmp(str,"cmd")) {
			printk("%s\n",str);
			single_threaded = 1;	/* serialize all drive access */
			return;
		}
#endif /* SUPPORT_SERIALIZE */
		if (!strcmp(str,"noprobe")) {
			printk("%s\n",str);
			dev->dont_probe = 1;	/* don't probe for this drive */
			return;
		}
#ifdef CONFIG_BLK_DEV_IDECD
		if (!strcmp(str,"cdrom")) {
			printk("cdrom\n");
			dev->present = 1;	/* force autoprobe to find it */
			dev->type = cdrom;
			return;
		}
#endif	/* CONFIG_BLK_DEV_IDECD */
	}
	if (ints[0] < 3 || ints[0] > 5) {
		printk("bad parms, expected: cyls,heads,sects[,wpcom[,irq]]\n");
	} else {
		for (i=0; i++ < ints[0];)
			printk("%s=%d%c",p[i-1],ints[i],i<ints[0]?',':'\n');
		dev->type    = disk;
		dev->cyl     = dev->bios_cyl  = ints[1];
		dev->head    = dev->bios_head = ints[2];
		dev->ctl     = (ints[2] > 8 ? 8 : 0);
		dev->sect    = dev->bios_sect = ints[3];
		dev->wpcom   = (ints[0] >= 4) ? ints[4] : 0;
		if (ints[0] >= 5)
			ide_irq[HWIF] = ints[5];
		ide_capacity[HWIF][drive] = BIOS_SECTORS(dev);
		dev->present = 1;
	}
}

void hda_setup(char *str, int *ints)
{
	next_drive = 0;
	ide_setup (str, ints);
}

void hdb_setup(char *str, int *ints)
{
	next_drive = 1;
	ide_setup (str, ints);
}

void hdc_setup(char *str, int *ints)
{
	next_drive = 2;
	ide_setup (str, ints);
}

void hdd_setup(char *str, int *ints)
{
	next_drive = 3;
	ide_setup (str, ints);
}

#ifndef CONFIG_BLK_DEV_HD
/*
 * We query CMOS about hard disks : it could be that we have a SCSI/ESDI/etc
 * controller that is BIOS compatible with ST-506, and thus showing up in our
 * BIOS table, but not register compatible, and therefore not present in CMOS.
 *
 * Furthermore, we will assume that our ST-506 drives <if any> are the primary
 * drives in the system -- the ones reflected as drive 1 or 2.  The first
 * drive is stored in the high nibble of CMOS byte 0x12, the second in the low
 * nibble.  This will be either a 4 bit drive type or 0xf indicating use byte
 * 0x19 for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.  A non-zero value 
 * means we have an AT controller hard disk for that drive.
 */
extern struct drive_info_struct drive_info;
static void probe_cmos_for_drives (void)
{
	byte drive, cmos_disks, *BIOS = (byte *) &drive_info;

	outb_p(0x12,0x70);		/* specify CMOS address 0x12 */
	cmos_disks = inb_p(0x71);	/* read the data from 0x12 */
	/* Extract drive geometry from CMOS+BIOS if not already setup */
	for (drive = 0; drive < MAX_DRIVES; drive++) {
		ide_dev_t *dev = &ide_dev[0][drive];
		if ((cmos_disks & (0xf0 >> (drive*4))) && !dev->present) {
			dev->cyl     = dev->bios_cyl  = *(unsigned short *)BIOS;
			dev->head    = dev->bios_head = * (BIOS+2);
			dev->sect    = dev->bios_sect = * (BIOS+14);
			dev->wpcom   = (*(unsigned short *)(BIOS+5))>>2;
			dev->ctl     = *(BIOS+8);
			dev->wpcom   = 0;
			dev->type    = disk;
			dev->present = 1;
			ide_capacity[0][drive] = BIOS_SECTORS(dev);
		}
		BIOS += 16;
	}
}
#endif	/* CONFIG_BLK_DEV_HD */

static void init_ide_data (byte hwif)
{
	int drive;

	for (drive = 0; drive < (MAX_DRIVES<<PARTN_BITS); drive++)
		ide_blksizes[hwif][drive] = 1024;
	blksize_size[ide_major[hwif]] = ide_blksizes[hwif];

	/* Initialize non-geometry fields -- ide_setup() runs before we do */
	for (drive = 0; drive < MAX_DRIVES; drive++) {
		ide_dev_t *dev = &ide_dev[hwif][drive];
		dev->select.all			= (drive<<4)|0xa0;
		dev->hwif			= hwif;
		dev->unmask			= 0;
		dev->busy			= 0;
		dev->mult_count			= 0; /* set by do_identify() */
		dev->mult_req			= 0; /* set by do_identify() */
		dev->usage			= 0;
		dev->vlb_32bit			= 0;
		dev->vlb_sync			= 0;
		dev->id				= NULL;
		dev->ctl			= 0x08;
		dev->wqueue			= NULL;
		dev->special.all		= 0;
		dev->special.b.recalibrate	= 1;
		dev->special.b.set_geometry	= 1;
		dev->keep_settings		= 0;
		ide_hd[hwif][drive<<PARTN_BITS].start_sect = 0;
		dev->name = ide_devname[hwif][drive];
	}
}

/*
 * This is the harddisk IRQ description. The SA_INTERRUPT in sa_flags
 * means we enter the IRQ-handler with interrupts disabled: this is bad for
 * interrupt latency, but anything else has led to problems on some
 * machines.  We enable interrupts as much as we can safely do in most places.
 */
static byte setup_irq (byte hwif)
{
	static byte rc = 0;
	unsigned long flags;
	const char *msg = "", *primary_secondary[] = {"primary", "secondary"};
	void (*handler)(int, struct pt_regs *) = HWIF ? &ide1_intr : &ide0_intr;

#if SUPPORT_SHARING_IRQ
	if (sharing_single_irq) {
		if (HWIF != 0 && !rc) {	/* IRQ already allocated? */
			msg = " (shared with ide0)";
			goto done;
		}
		handler = &ide_shared_intr;
	}
#if SUPPORT_SERIALIZE
	else if (single_threaded) {
		handler = &ide_seq_intr;
		if (HWIF != 0)
			msg = " (single-threaded with ide0)";
	}
#endif /* SUPPORT_SERIALIZE */
#endif /* SUPPORT_SHARING_IRQ */
	save_flags(flags);
	cli();
	if ((rc = request_irq(ide_irq[HWIF],handler,SA_INTERRUPT,ide_name[HWIF])))
		msg = ":  FAILED! unable to allocate IRQ";
	restore_flags(flags);
#if SUPPORT_SHARING_IRQ
done:
#endif /* SUPPORT_SHARING_IRQ */
	printk("%s: %s interface on irq %d%s\n",
	 ide_name[HWIF], primary_secondary[HWIF], ide_irq[HWIF], msg);
	return rc;
}

static void ide_geninit(byte hwif)
{
	static int drive;
	
	for (drive = 0; drive < MAX_DRIVES; drive++) {
		ide_dev_t *dev = &ide_dev[HWIF][drive];
		if (dev->present) {
			ide_hd[HWIF][drive<<PARTN_BITS].nr_sects = ide_capacity[HWIF][drive];
			/* Skip partition check for cdroms. */
			if (dev->type == cdrom)
				ide_hd[HWIF][drive<<PARTN_BITS].start_sect = -1;
		}
	}
}

static void ide0_geninit(void)
{
	ide_geninit(0);
}

static void ide1_geninit(void)
{
	ide_geninit(1);
}

static struct file_operations ide_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	ide_ioctl,		/* ioctl */
	NULL,			/* mmap */
	ide_open,		/* open */
	ide_release,		/* release */
	block_fsync		/* fsync */
#ifdef CONFIG_BLK_DEV_IDECD
	,NULL,			/* fasync */
	ide_check_media_change,	/* check_media_change */
	NULL			/* revalidate */
#endif CONFIG_BLK_DEV_IDECD
};


#if SUPPORT_DTC2278
/*
 * From: andy@cercle.cts.com (Dyan Wile)
 *
 * Below is a patch for DTC-2278 - alike software-programmable controllers
 * The code enables the secondary IDE controller and the PIO4 (3?) timings on
 * the primary (EIDE). You may probably have to enable the 32-bit support to
 * get the full speed. You better get the disk interrupts disabled ( hdparm -u0 
 * /dev/hd.. ) for the drives connected to the EIDE interface. (I get my 
 * filesystem  corrupted with -u 1, but under heavy disk load only :-)  
 */

static void sub22 (char b, char c)
{
	int i;

	for(i = 0; i < 3; i++) {
		__inb(0x3f6);
		outb_p(b,0xb0);
		__inb(0x3f6);
		outb_p(c,0xb4);
		__inb(0x3f6);
		if(__inb(0xb4) == c) {
			outb_p(7,0xb0);
			__inb(0x3f6);
			return;	/* success */
		}
	}
}

static void try_to_init_dtc2278 (void)
{
/* This (presumably) enables PIO mode4 (3?) on the first interface */
	cli();
	sub22(1,0xc3);
	sub22(0,0xa0);
	sti();

/* This enables the second interface */

	outb_p(4,0xb0);
	__inb(0x3f6);
	outb_p(0x20,0xb4);
	__inb(0x3f6);
}
#endif /* SUPPORT_DTC2278 */

/*
 * This is gets invoked once during initialization, to set *everything* up
 */
unsigned long ide_init (unsigned long mem_start, unsigned long mem_end)
{
	byte hwif;

#if SUPPORT_DTC2278
	if (probe_dtc2278)
		try_to_init_dtc2278();
#endif /* SUPPORT_DTC2278 */
	/* single_threaded = 0; */	/* zero by default, override at boot */
	for (hwif = 0; hwif < 2; hwif++) {
		init_ide_data (hwif);
		if (SUPPORT_TWO_INTERFACES || hwif == HWIF) {
			if (hwif == 0)
#ifdef CONFIG_BLK_DEV_HD
				continue;
#else
				probe_cmos_for_drives ();
#endif /* CONFIG_BLJ_DEV_HD */
			probe_mem_start = (mem_start + 3uL) & ~3uL;
			probe_for_drives (hwif);
			mem_start = probe_mem_start;
		}
	}

	/* At this point, all methods of drive detection have completed */
	ide_gendisk[0].nr_real = ide_dev[0][0].present + ide_dev[0][1].present;
	ide_gendisk[1].nr_real = ide_dev[1][0].present + ide_dev[1][1].present;
	if (ide_gendisk[1].nr_real && (ide_irq[0] == ide_irq[1])) {
		if (!ide_gendisk[0].nr_real) {
			ide_irq[0] = 0;	/* needed by ide_intr() */
		} else {
#if SUPPORT_SHARING_IRQ
			sharing_single_irq = 1;
			single_threaded = 1;
#else /* SUPPORT_SHARING_IRQ */
			printk("%s: ide irq-sharing%s", ide_name[1], unsupported);
			return mem_start;
#endif /* SUPPORT_SHARING_IRQ */
		}
	}
#ifdef CONFIG_BLK_DEV_HD
#if SUPPORT_SHARING_IRQ
	if (ide_irq[1] == 14 || sharing_single_irq) {
#else
	if (ide_irq[1] == 14) {
#endif /* SUPPORT_SHARING_IRQ */
		printk("%s: irq-sharing not possible with old harddisk driver (hd.c)\n", ide_name[1]);
		return mem_start;
	}
#endif /* CONFIG_BLK_DEV_HD */

	for (hwif = 2; hwif-- > 0;) {
		if (ide_gendisk[hwif].nr_real != 0 && !setup_irq(hwif)) {
			const char *name = ide_name[HWIF];
			unsigned int major = ide_major[HWIF];
			if (register_blkdev(major, name, &ide_fops)) {
				printk("%s: unable to get major number %d\n", name, major);
			} else {
				timer_table[ide_timer[HWIF]].fn
					= HWIF ? ide1_timer_expiry : ide0_timer_expiry;
#if SUPPORT_SHARING_IRQ
				if (single_threaded)
					blk_dev[major].request_fn = &do_shared_request;
				else
#endif /* SUPPORT_SHARING_IRQ */
				blk_dev[major].request_fn =
				 HWIF ? &do_ide1_request : &do_ide0_request;
				read_ahead[major] = 8;	/* (4kB) */
				ide_gendisk[HWIF].next = gendisk_head;
				gendisk_head = &ide_gendisk[HWIF];
			}
		}
	}
	return mem_start;
}
