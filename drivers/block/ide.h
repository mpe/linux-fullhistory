/*
 *  linux/drivers/block/ide.h
 *
 *  Copyright (C) 1994, 1995  Linus Torvalds & authors
 */

/*
 * This is the multiple IDE interface driver, as evolved from hd.c.  
 * It supports up to four IDE interfaces, on one or more IRQs (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * Primary i/f:    ide0: major=3;  (hda)         minor=0; (hdb)         minor=64
 * Secondary i/f:  ide1: major=22; (hdc or hd1a) minor=0; (hdd or hd1b) minor=64
 * Tertiary i/f:   ide2: major=33; (hde)         minor=0; (hdf)         minor=64
 * Quaternary i/f: ide3: major=34; (hdg)         minor=0; (hdh)         minor=64
 */

/******************************************************************************
 * IDE driver configuration options (play with these as desired):
 */
#undef REALLY_SLOW_IO			/* most systems can safely undef this */
#include <asm/io.h>

#define	REALLY_FAST_IO			/* define if ide ports are perfect */
#define INITIAL_MULT_COUNT	0	/* off=0; on=2,4,8,16,32, etc.. */

#ifndef DISK_RECOVERY_TIME		/* off=0; on=access_delay_time */
#define DISK_RECOVERY_TIME	0	/*  for hardware that needs it */
#endif
#ifndef OK_TO_RESET_CONTROLLER		/* 1 needed for good error recovery */
#define OK_TO_RESET_CONTROLLER	1	/* 0 for use with AH2372A/B interface */
#endif
#ifndef SUPPORT_DTC2278			/* 1 to support DTC2278 chipset */
#define SUPPORT_DTC2278		1	/* 0 to reduce kernel size */
#endif
#ifndef FANCY_STATUS_DUMPS		/* 1 for human-readable drive errors */
#define FANCY_STATUS_DUMPS	1	/* 0 to reduce kernel size */
#endif

/*
 * IDE_DRIVE_CMD is used to implement many features of the hdparm utility
 */
#define IDE_DRIVE_CMD		99	/* (magic) undef to reduce kernel size*/

/*
 *  "No user-serviceable parts" beyond this point  :)
 *****************************************************************************/

typedef unsigned char	byte;	/* used everywhere */

/*
 * Probably not wise to fiddle with these
 */
#define ERROR_MAX	8	/* Max read/write errors per sector */
#define ERROR_RESET	3	/* Reset controller every 4th retry */
#define ERROR_RECAL	1	/* Recalibrate every 2nd retry */

/*
 * Ensure that various configuration flags have compatible settings
 */
#ifdef REALLY_SLOW_IO
#undef REALLY_FAST_IO
#endif

/*
 * Definitions for accessing IDE controller registers
 */

#define HWIF(drive)		((ide_hwif_t *)drive->hwif)
#define HWGROUP(drive)		((ide_hwgroup_t *)(HWIF(drive)->hwgroup))

#define IDE_DATA_REG		(HWIF(drive)->io_base)
#define IDE_ERROR_REG		(HWIF(drive)->io_base+1)
#define IDE_NSECTOR_REG		(HWIF(drive)->io_base+2)
#define IDE_SECTOR_REG		(HWIF(drive)->io_base+3)
#define IDE_LCYL_REG		(HWIF(drive)->io_base+4)
#define IDE_HCYL_REG		(HWIF(drive)->io_base+5)
#define IDE_SELECT_REG		(HWIF(drive)->io_base+6)
#define IDE_STATUS_REG		(HWIF(drive)->io_base+7)
#define IDE_CONTROL_REG		(HWIF(drive)->ctl_port)
#define IDE_FEATURE_REG		IDE_ERROR_REG
#define IDE_COMMAND_REG		IDE_STATUS_REG
#define IDE_ALTSTATUS_REG	IDE_CONTROL_REG

#ifdef REALLY_FAST_IO
#define OUT_BYTE(b,p)		outb((b),p)
#define IN_BYTE(p)		(byte)inb(p)
#else
#define OUT_BYTE(b,p)		outb_p((b),p)
#define IN_BYTE(p)		(byte)inb_p(p)
#endif /* REALLY_FAST_IO */

#define GET_ERR()		IN_BYTE(IDE_ERROR_REG)
#define GET_STAT()		IN_BYTE(IDE_STATUS_REG)
#define OK_STAT(stat,good,bad)	(((stat)&((good)|(bad)))==(good))
#define BAD_R_STAT		(BUSY_STAT   | ERR_STAT)
#define BAD_W_STAT		(BAD_R_STAT  | WRERR_STAT)
#define BAD_STAT		(BAD_R_STAT  | DRQ_STAT)
#define DRIVE_READY		(READY_STAT  | SEEK_STAT)
#define DATA_READY		(DRIVE_READY | DRQ_STAT)

/*
 * Some more useful definitions
 */
#define IDE_MAJOR_NAME	"ide"	/* the same for all i/f; see also genhd.c */
#define MAJOR_NAME	IDE_MAJOR_NAME
#define PARTN_BITS	6	/* number of minor dev bits for partitions */
#define PARTN_MASK	((1<<PARTN_BITS)-1)	/* a useful bit mask */
#define MAX_DRIVES	2	/* per interface; 2 assumed by lots of code */
#define MAX_HWIFS	4	/* an arbitrary, but realistic limit */
#define SECTOR_WORDS	(512 / 4)	/* number of 32bit words per sector */

/*
 * Timeouts for various operations:
 */
#define WAIT_DRQ	(5*HZ/100)	/* 50msec - spec allows up to 20ms */
#define WAIT_READY	(3*HZ/100)	/* 30msec - should be instantaneous */
#define WAIT_PIDENTIFY	(1*HZ)	/* 1sec   - should be less than 3ms (?) */
#define WAIT_WORSTCASE	(30*HZ)	/* 30sec  - worst case when spinning up */
#define WAIT_CMD	(10*HZ)	/* 10sec  - maximum wait for an IRQ to happen */

#ifdef CONFIG_BLK_DEV_IDECD

struct packet_command {
  char *buffer;
  int buflen;
  int stat;
  unsigned char c[12];
};


struct atapi_request_sense {
  unsigned char error_code : 7;
  unsigned char valid      : 1;
  byte reserved1;
  unsigned char sense_key  : 4;
  unsigned char reserved2  : 1;
  unsigned char ili        : 1;
  unsigned char reserved3  : 2;
  byte info[4];
  byte sense_len;
  byte command_info[4];
  byte asc;
  byte ascq;
  byte fru;
  byte sense_key_specific[3];
};

/* Space to hold the disk TOC. */

#define MAX_TRACKS 99
struct atapi_toc_header {
  unsigned short toc_length;
  byte first_track;
  byte last_track;
};

struct atapi_toc_entry {
  byte reserved1;
  unsigned control : 4;
  unsigned adr     : 4;
  byte track;
  byte reserved2;
  unsigned lba;
};

struct atapi_toc {
  struct atapi_toc_header hdr;
  struct atapi_toc_entry  ent[MAX_TRACKS+1];  /* One extra for the leadout. */
};

/* Extra per-device info for cdrom drives. */
struct cdrom_info {

  /* Buffer for table of contents.  NULL if we haven't allocated
     a TOC buffer for this device yet. */

  struct atapi_toc *toc;

  /* Sector buffer.  If a read request wants only the first part of a cdrom
     block, we cache the rest of the block here, in the expectation that that
     data is going to be wanted soon.  SECTOR_BUFFERED is the number of the
     first buffered sector, and NSECTORS_BUFFERED is the number of sectors
     in the buffer.  Before the buffer is allocated, we should have
     SECTOR_BUFFER == NULL and NSECTORS_BUFFERED == 0. */

  unsigned long sector_buffered;
  unsigned long nsectors_buffered;
  char *sector_buffer;

  /* The result of the last successful request sense command
     on this device. */
  struct atapi_request_sense sense_data;
};

#endif /* CONFIG_BLK_DEV_IDECD */

/*
 * Now for the data we need to maintain per-drive:  ide_drive_t
 */
typedef enum {disk, cdrom} media_t;

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
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned bit5		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit7		: 1;	/* always 1 */
	} b;
	} select_t;

typedef struct ide_drive_s {
	special_t	special;	/* special action flags */
	unsigned present	: 1;	/* drive is physically present */
	unsigned noprobe 	: 1;	/* from:  hdx=noprobe */
	unsigned keep_settings  : 1;	/* restore settings after drive reset */
	unsigned busy		: 1;	/* currently doing revalidate_disk() */
	unsigned vlb_32bit	: 1;	/* use 32bit in/out for data */
	unsigned vlb_sync	: 1;	/* needed for some 32bit chip sets */
	unsigned removeable	: 1;	/* 1 if need to do check_media_change */
	unsigned dma_capable	: 1;	/* for Intel Triton chipset, others.. */
	select_t	select;		/* basic drive/head select reg value */
	byte		unmask;		/* flag: okay to unmask other irqs */
	void		*hwif;		/* actually (ide_hwif_t *) */
	byte		ctl;		/* "normal" value for IDE_CONTROL_REG */
	byte		ready_stat;	/* min status value for drive ready */
	byte		wpcom;		/* ignored by all IDE drives */
	media_t		media;		/* disk, cdrom, tape */
	byte		mult_count;	/* current multiple sector setting */
	byte 		mult_req;	/* requested multiple sector setting */
	byte		chipset;	/* interface chipset access method */
	byte		bad_wstat;	/* used for ignoring WRERR_STAT */
	byte		sect0;		/* offset of first sector for DM6:DDO */
	byte 		usage;		/* current "open()" count for drive */
	byte 		head;		/* "real" number of heads */
	byte		sect;		/* "real" sectors per track */
	byte		bios_head;	/* BIOS/fdisk/LILO number of heads */
	byte		bios_sect;	/* BIOS/fdisk/LILO sectors per track */
	unsigned short	bios_cyl;	/* BIOS/fdisk/LILO number of cyls */
	unsigned short	cyl;		/* "real" number of cyls */
	struct wait_queue *wqueue;	/* used to wait for drive in open() */
	struct hd_driveid *id;		/* drive model identification info */
	struct hd_struct  *part;	/* drive partition table */
	char		name[4];	/* drive name, such as "hda" */
#ifdef CONFIG_BLK_DEV_IDECD
	struct cdrom_info cdrom_info;	/* from ide-cd.c */
#endif /* CONFIG_BLK_DEV_IDECD */
	} ide_drive_t;

typedef struct hwif_s {
	struct hwif_s	*next;		/* for linked-list in ide_hwgroup_t */
	void		*hwgroup;	/* actually (ide_hwgroup_t *) */
	unsigned short	io_base;	/* base io port addr */
	unsigned short	ctl_port;	/* usually io_base+0x206 */
	ide_drive_t		drives[MAX_DRIVES];	/* drive info */
	struct gendisk	*gd;		/* gendisk structure */
	byte		irq;		/* our irq number */
	byte		major;		/* our major number */
	byte		drivecount;	/* how many drives attached */
	char 		name[5];	/* name of interface, eg. "ide0" */
	unsigned	noprobe : 1;	/* don't probe for this interface */
	unsigned	present : 1;	/* this interface exists */
	unsigned long	reset_timeout;	/* timeout value during ide resets */
#if (DISK_RECOVERY_TIME > 0)
	unsigned long	last_timer;	/* time when previous rq was done */
#endif
#ifdef CONFIG_BLK_DEV_IDECD
	struct request request_sense_request;	/* from ide-cd.c */
	struct packet_command request_sense_pc;	/* from ide-cd.c */
#endif /* CONFIG_BLK_DEV_IDECD */
	} ide_hwif_t;

/*
 *  our internal interrupt handler type
 */
typedef void (ide_handler_t)(ide_drive_t *);

typedef struct hwgroup_s {
	ide_handler_t		*handler;/* irq handler, if active */
	ide_drive_t		*drive;	/* current drive */
	ide_hwif_t		*hwif;	/* ptr to current hwif in linked-list */
	struct request		*rq;	/* current request */
	struct timer_list	timer;	/* failsafe timer */
	struct request		wrq;	/* local copy of current write rq */
	} ide_hwgroup_t;

/*
 * One final include file, which references some of the data/defns from above
 */
#define IDE_DRIVER	/* "parameter" for blk.h */
#include "blk.h"

#if (DISK_RECOVERY_TIME > 0)
void ide_set_recovery_timer (ide_hwif_t *);
#define SET_RECOVERY_TIMER(drive) ide_set_recovery_timer (drive)
#else
#define SET_RECOVERY_TIMER(drive)
#endif

/*
 * The main (re-)entry point for handling a new request is IDE_DO_REQUEST.
 * Note that IDE_DO_REQUEST should *only* ever be invoked from an interrupt
 * handler.  All others, such as a timer expiry handler, should call
 * do_hwgroup_request() instead (currently local to ide.c).
 */
void ide_do_request (ide_hwgroup_t *);
#define IDE_DO_REQUEST { SET_RECOVERY_TIMER(HWIF(drive)); ide_do_request(HWGROUP(drive)); }


/*
 * This is used for (nearly) all data transfers from the IDE interface
 */
void ide_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount);

/*
 * This is used for (nearly) all data transfers to the IDE interface
 */
void ide_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount);

/*
 * This is used on exit from the driver, to designate the next irq handler
 * and also to start the safety timer.
 */
void ide_set_handler (ide_drive_t *drive, ide_handler_t *handler);

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
byte ide_dump_status (ide_drive_t *drive, const char *msg, byte stat);

/*
 * ide_error() takes action based on the error returned by the controller.
 *
 * Returns 1 if an ide reset operation has been initiated, in which case
 * the caller MUST simply return from the driver (through however many levels).
 * Returns 0 otherwise.
 */
int ide_error (ide_drive_t *drive, const char *msg, byte stat);

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return 1 after invoking ide_error()
 *
 */
int ide_wait_stat (ide_drive_t *drive, byte good, byte bad, unsigned long timeout);

/*
 * This is called from genhd.c to correct DiskManager/EZ-Drive geometries
 */
int ide_xlate_1024(dev_t, int, const char *);

#ifdef CONFIG_BLK_DEV_IDECD
/*
 * These are routines in ide-cd.c invoked from ide.c
 */
void ide_do_rw_cdrom (ide_drive_t *, unsigned long);
int ide_cdrom_ioctl (ide_drive_t *, struct inode *, struct file *, unsigned int, unsigned long);
int ide_cdrom_check_media_change (ide_drive_t *);
int ide_cdrom_open (struct inode *, struct file *, ide_drive_t *);
void ide_cdrom_release (struct inode *, struct file *, ide_drive_t *);
void ide_cdrom_setup (ide_drive_t *);
#endif /* CONFIG_BLK_DEV_IDECD */
