#ifndef _LINUX_XD_H
#define _LINUX_XD_H

/*
 * This file contains the definitions for the IO ports and errors etc. for XT hard disk controllers (at least the DTC 5150X).
 *
 * Author: Pat Mackinlay, smackinla@cc.curtin.edu.au
 * Date: 29/09/92
 *
 * Revised: 01/01/93, ...
 *
 * Ref: DTC 5150X Controller Specification (thanks to Kevin Fowler, kevinf@agora.rain.com)
 * Also thanks to: Salvador Abreu, Dave Thaler, Risto Kankkunen and Wim Van Dorst.
 */

/* XT hard disk controller registers */
#define XD_DATA		(xd_iobase + 0x00)	/* data RW register */
#define XD_RESET	(xd_iobase + 0x01)	/* reset WO register */
#define XD_STATUS	(xd_iobase + 0x01)	/* status RO register */
#define XD_SELECT	(xd_iobase + 0x02)	/* select WO register */
#define XD_JUMPER	(xd_iobase + 0x02)	/* jumper RO register */
#define XD_CONTROL	(xd_iobase + 0x03)	/* DMAE/INTE WO register */
#define XD_RESERVED	(xd_iobase + 0x03)	/* reserved */

/* XT hard disk controller commands (incomplete list) */
#define CMD_TESTREADY	0x00	/* test drive ready */
#define CMD_RECALIBRATE	0x01	/* recalibrate drive */
#define CMD_SENSE	0x03	/* request sense */
#define CMD_FORMATDRV	0x04	/* format drive */
#define CMD_VERIFY	0x05	/* read verify */
#define CMD_FORMATTRK	0x06	/* format track */
#define CMD_FORMATBAD	0x07	/* format bad track */
#define CMD_READ	0x08	/* read */
#define CMD_WRITE	0x0A	/* write */
#define CMD_SEEK	0x0B	/* seek */

/* Controller specific commands */
#define CMD_DTCSETPARAM	0x0C	/* set drive parameters (DTC 5150X only?) */
#define CMD_DTCGETECC	0x0D	/* get ecc error length (DTC 5150X only?) */
#define CMD_DTCREADBUF	0x0E	/* read sector buffer (DTC 5150X only?) */
#define CMD_DTCWRITEBUF 0x0F	/* write sector buffer (DTC 5150X only?) */
#define CMD_DTCREMAPTRK	0x11	/* assign alternate track (DTC 5150X only?) */
#define CMD_DTCGETPARAM	0xFB	/* get drive parameters (DTC 5150X only?) */
#define CMD_DTCSETSTEP	0xFC	/* set step rate (DTC 5150X only?) */
#define CMD_DTCSETGEOM	0xFE	/* set geometry data (DTC 5150X only?) */
#define CMD_DTCGETGEOM	0xFF	/* get geometry data (DTC 5150X only?) */
#define CMD_ST11GETGEOM 0xF8	/* get geometry data (Seagate ST11R/M only?) */
#define CMD_WDSETPARAM	0x0C	/* set drive parameters (WD 1004A27X only?) */

/* Bits for command status byte */
#define CSB_ERROR	0x02	/* error */
#define CSB_LUN		0x20	/* logical Unit Number */

/* XT hard disk controller status bits */
#define STAT_READY	0x01	/* controller is ready */
#define STAT_INPUT	0x02	/* data flowing from controller to host */
#define STAT_COMMAND	0x04	/* controller in command phase */
#define STAT_SELECT	0x08	/* controller is selected */
#define STAT_REQUEST	0x10	/* controller requesting data */
#define STAT_INTERRUPT	0x20	/* controller requesting interrupt */

/* XT hard disk controller control bits */
#define PIO_MODE	0x00	/* control bits to set for PIO */
#define DMA_MODE	0x03	/* control bits to set for DMA & interrupt */

#define XD_MAXDRIVES	2	/* maximum 2 drives */
#define XD_TIMEOUT	100	/* 1 second timeout */
#define XD_RETRIES	4	/* maximum 4 retries */

#undef DEBUG			/* define for debugging output */
#undef XD_OVERRIDE		/* define to override auto-detection */

#ifdef DEBUG
	#define DEBUG_STARTUP	/* debug driver initialisation */
	#define DEBUG_OVERRIDE	/* debug override geometry detection */
	#define DEBUG_READWRITE	/* debug each read/write command */
	#define DEBUG_OTHER	/* debug misc. interrupt/DMA stuff */
	#define DEBUG_COMMAND	/* debug each controller command */
#endif DEBUG

/* this structure defines the XT drives and their types */
typedef struct {
	u_char heads;
	u_short cylinders;
	u_char sectors;
	u_char control;
} XD_INFO;

#define	HDIO_GETGEO	0x0301		/* get drive geometry */

/* this structure is returned to the HDIO_GETGEO ioctl */
typedef struct {
	u_char heads;
	u_char sectors;
	u_short cylinders;
	u_long start;
} XD_GEOMETRY;

/* this structure defines a ROM BIOS signature */
typedef struct {
	u_long offset;
	char *string;
	void (*init_controller)(u_char *address);
	void (*init_drive)(u_char drive);
	char *name;
} XD_SIGNATURE;

extern void resetup_one_dev (struct gendisk *dev,unsigned int drive);

u_long xd_init(u_long mem_start,u_long mem_end);
static u_char xd_detect (u_char *controller,u_char **address);
static u_char xd_initdrives (void (*init_drive)(u_char drive));
static void xd_geninit (void);

static int xd_open (struct inode *inode,struct file *file);
static void do_xd_request (void);
static int xd_ioctl (struct inode *inode,struct file *file,unsigned int cmd,unsigned long arg);
static void xd_release (struct inode *inode,struct file *file);
static int xd_reread_partitions (int dev);
static int xd_readwrite (u_char operation,u_char drive,char *buffer,u_int block,u_int count);
static void xd_recalibrate (u_char drive);

static void xd_interrupt_handler (int unused);
static u_char xd_setup_dma (u_char opcode,u_char *buffer,u_int count);
static u_char *xd_build (u_char *cmdblk,u_char command,u_char drive,u_char head,u_short cylinder,u_char sector,u_char count,u_char control);
static inline u_char xd_waitport (u_short port,u_char flags,u_char mask,u_long timeout);
static u_int xd_command (u_char *command,u_char mode,u_char *indata,u_char *outdata,u_char *sense,u_long timeout);

/* card specific setup and geometry gathering code */
#ifndef XD_OVERRIDE
static void xd_dtc5150x_init_controller (u_char *address);
static void xd_dtc5150x_init_drive (u_char drive);
static void xd_wd1004a27x_init_controller (u_char *address);
static void xd_wd1004a27x_init_drive (u_char drive);
static void xd_seagate11_init_controller (u_char *address);
static void xd_seagate11_init_drive (u_char drive);
static void xd_setparam (u_char command,u_char drive,u_char heads,u_short cylinders,u_short rwrite,u_short wprecomp,u_char ecc);
#endif XD_OVERRIDE

static void xd_override_init_controller (u_char *address);
static void xd_override_init_drive (u_char drive);

#endif _LINUX_XD_H
