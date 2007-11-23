/*
 *  linux/kernel/floppy.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1993, 1994  Alain Knaff
 */
/*
 * 02.12.91 - Changed to static variables to indicate need for reset
 * and recalibrate. This makes some things easier (output_byte reset
 * checking etc), and means less interrupt jumping in case of errors,
 * so the code is hopefully easier to understand.
 */

/*
 * This file is certainly a mess. I've tried my best to get it working,
 * but I don't like programming floppies, and I have only one anyway.
 * Urgel. I should check for more errors, and do more graceful error
 * recovery. Seems there are problems with several drives. I've tried to
 * correct them. No promises.
 */

/*
 * As with hd.c, all routines within this file can (and will) be called
 * by interrupts, so extreme caution is needed. A hardware interrupt
 * handler may not sleep, or a kernel panic will happen. Thus I cannot
 * call "floppy-on" directly, but have to set a special timer interrupt
 * etc.
 */

/*
 * 28.02.92 - made track-buffering routines, based on the routines written
 * by entropy@wintermute.wpi.edu (Lawrence Foard). Linus.
 */

/*
 * Automatic floppy-detection and formatting written by Werner Almesberger
 * (almesber@nessie.cs.id.ethz.ch), who also corrected some problems with
 * the floppy-change signal detection.
 */

/*
 * 1992/7/22 -- Hennus Bergman: Added better error reporting, fixed
 * FDC data overrun bug, added some preliminary stuff for vertical
 * recording support.
 *
 * 1992/9/17: Added DMA allocation & DMA functions. -- hhb.
 *
 * TODO: Errors are still not counted properly.
 */

/* 1992/9/20
 * Modifications for ``Sector Shifting'' by Rob Hooft (hooft@chem.ruu.nl)
 * modelled after the freeware MS/DOS program fdformat/88 V1.8 by
 * Christoph H. Hochst\"atter.
 * I have fixed the shift values to the ones I always use. Maybe a new
 * ioctl() should be created to be able to modify them.
 * There is a bug in the driver that makes it impossible to format a
 * floppy as the first thing after bootup.
 */

/*
 * 1993/4/29 -- Linus -- cleaned up the timer handling in the kernel, and
 * this helped the floppy driver as well. Much cleaner, and still seems to
 * work.
 */

/* 1994/6/24 --bbroad-- added the floppy table entries and made
 * minor modifications to allow 2.88 floppies to be run.
 */

/* 1994/7/13 -- Paul Vojta -- modified the probing code to allow three or more
 * disk types.
 */

/*
 * 1994/8/8 -- Alain Knaff -- Switched to fdpatch driver: Support for bigger
 * format bug fixes, but unfortunately some new bugs too...
 */

/* 1994/9/17 -- Koen Holtman -- added logging of physical floppy write 
 * errors to allow safe writing by specialized programs.
 */

#define CONFIG_FLOPPY_SANITY
#undef  CONFIG_FLOPPY_SILENT_DCL_CLEAR

#define REALLY_SLOW_IO

#define DEBUGT 2
#define DCL_DEBUG /* debug disk change line */

#include <linux/config.h>

/* do print messages for unexpected interrupts */
static int print_unex=1;

#ifndef FD_MODULE
/* the following is the mask of allowed drives. By default units 2 and
 * 3 of both floppy controllers are disabled, because switching on the
 * motor of these drives causes system hangs on some PCI computers. drive
 * 0 is the low bit (0x1), and drive 7 is the high bit (0x80). Bits are on if
 * a drive is allowed. */
static int ALLOWED_DRIVE_MASK=0x33;

#define FLOPPY_IRQ 6
#define FLOPPY_DMA 2
#define FDC1 0x3f0
static int FDC2=-1;
#endif

#define MODULE_AWARE_DRIVER

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/tqueue.h>
#define FDPATCHES
#include <linux/fdreg.h>
#include <linux/fd.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h> /* CMOS defines */

#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR FLOPPY_MAJOR
#include "blk.h"

static unsigned int fake_change = 0;
static int initialising=1;

#define FLOPPY0_TYPE	((CMOS_READ(0x10) >> 4) & 15)
#define FLOPPY1_TYPE	(CMOS_READ(0x10) & 15)

/*
 * Again, the CMOS information doesn't work on the alpha..
 */
#ifdef __alpha__
#undef FLOPPY0_TYPE
#undef FLOPPY1_TYPE
#define FLOPPY0_TYPE 6
#define FLOPPY1_TYPE 0
#endif

#define N_FDC 2
#define N_DRIVE 8

#define TYPE(x) ( ((x)>>2) & 0x1f )
#define DRIVE(x) ( ((x)&0x03) | (((x)&0x80 ) >> 5))
#define UNIT(x) ( (x) & 0x03 )		/* drive on fdc */
#define FDC(x) ( ((x) & 0x04) >> 2 )  /* fdc of drive */
#define REVDRIVE(fdc, unit) ( (unit) + ((fdc) << 2 ))
				/* reverse mapping from unit and fdc to drive */
#define DP (&drive_params[current_drive])
#define DRS (&drive_state[current_drive])
#define DRWE (&write_errors[current_drive])
#define FDCS (&fdc_state[fdc])
#define CLEARF(x) (clear_bit(x##_BIT, &DRS->flags))
#define SETF(x) (set_bit(x##_BIT, &DRS->flags))
#define TESTF(x) (test_bit(x##_BIT, &DRS->flags))

#define UDP (&drive_params[drive])
#define UDRS (&drive_state[drive])
#define UDRWE (&write_errors[drive])
#define UFDCS (&fdc_state[FDC(drive)])
#define UCLEARF(x) (clear_bit(x##_BIT, &UDRS->flags))
#define USETF(x) (set_bit(x##_BIT, &UDRS->flags))
#define UTESTF(x) (test_bit(x##_BIT, &UDRS->flags))

#define DPRINT(x) printk(DEVICE_NAME "%d: " x,current_drive)

#define DPRINT1(x,x1) \
printk(DEVICE_NAME "%d: " x,current_drive,(x1))

#define DPRINT2(x,x1,x2) \
printk(DEVICE_NAME "%d: " x,current_drive,(x1),(x2))

#define DPRINT3(x,x1,x2,x3) \
printk(DEVICE_NAME "%d: " x,current_drive,(x1),(x2),(x3))

/* read/write */
#define COMMAND raw_cmd.cmd[0]
#define DR_SELECT raw_cmd.cmd[1]
#define TRACK raw_cmd.cmd[2]
#define HEAD raw_cmd.cmd[3]
#define SECTOR raw_cmd.cmd[4]
#define SIZECODE raw_cmd.cmd[5]
#define SECT_PER_TRACK raw_cmd.cmd[6]
#define GAP raw_cmd.cmd[7]
#define SIZECODE2 raw_cmd.cmd[8]
#define NR_RW 9

/* format */
#define F_SIZECODE raw_cmd.cmd[2]
#define F_SECT_PER_TRACK raw_cmd.cmd[3]
#define F_GAP raw_cmd.cmd[4]
#define F_FILL raw_cmd.cmd[5]
#define NR_F 6

/*
 * Maximum disk size (in kilobytes). This default is used whenever the
 * current disk size is unknown.
 * [Now it is rather a minimum]
 */
#define MAX_DISK_SIZE 2 /* 3984*/



/*
 * The DMA channel used by the floppy controller cannot access data at
 * addresses >= 16MB
 *
 * Went back to the 1MB limit, as some people had problems with the floppy
 * driver otherwise. It doesn't matter much for performance anyway, as most
 * floppy accesses go through the track buffer.
 */
#define LAST_DMA_ADDR	(0x1000000)
#define K_64 (0x10000) /* 64 k */

/*
 * globals used by 'result()'
 */
#define MAX_REPLIES 10
static unsigned char reply_buffer[MAX_REPLIES];
static int inr; /* size of reply buffer, when called from interrupt */
#define ST0 (reply_buffer[0])
#define ST1 (reply_buffer[1])
#define ST2 (reply_buffer[2])
#define ST3 (reply_buffer[0]) /* result of GETSTATUS */
#define R_TRACK (reply_buffer[3])
#define R_HEAD (reply_buffer[4])
#define R_SECTOR (reply_buffer[5])
#define R_SIZECODE (reply_buffer[6])

#define SEL_DLY (2*HZ/100)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof( (x)[0] ))
/*
 * this struct defines the different floppy drive types.
 */
static struct {
	struct floppy_drive_params params;
	char *name; /* name printed while booting */
} default_drive_params[]= {
/* NOTE: the time values in jiffies should be in msec!
 CMOS drive type
  |     Maximum data rate supported by drive type
  |     |   Head load time, msec
  |     |   |   Head unload time, msec (not used)
  |     |   |   |     Step rate interval, usec
  |     |   |   |     |       Time needed for spinup time (jiffies)
  |     |   |   |     |       |      Timeout for spinning down (jiffies)
  |     |   |   |     |       |      |   Spindown offset (where disk stops)
  |     |   |   |     |       |      |   |     Select delay
  |     |   |   |     |       |      |   |     |     RPS
  |     |   |   |     |       |      |   |     |     |    Max number of tracks
  |     |   |   |     |       |      |   |     |     |    |     Interrupt timeout
  |     |   |   |     |       |      |   |     |     |    |     |   Max nonintlv. sectors
  |     |   |   |     |       |      |   |     |     |    |     |   | -Max Errors- flags */
{{0,  500, 16, 16, 8000,    1*HZ, 3*HZ,  0, SEL_DLY, 5,  80, 3*HZ, 20, {3,1,2,0,2}, 0,
      0, { 7, 4, 8, 2, 1, 5, 3,10}, 3*HZ/2, 0 }, "unknown" },

{{1,  300, 16, 16, 8000,    1*HZ, 3*HZ,  0, SEL_DLY, 5,  40, 3*HZ, 17, {3,1,2,0,2}, 0,
      0, { 1, 0, 0, 0, 0, 0, 0, 0}, 3*HZ/2, 1 }, "360K PC" }, /*5 1/4 360 KB PC*/

{{2,  500, 16, 16, 6000, 4*HZ/10, 3*HZ, 14, SEL_DLY, 6,  83, 3*HZ, 17, {3,1,2,0,2}, 0,
      0, { 2, 5, 6,23,10,20,11, 0}, 3*HZ/2, 2 }, "1.2M" }, /*5 1/4 HD AT*/

{{3,  250, 16, 16, 3000,    1*HZ, 3*HZ,  0, SEL_DLY, 5,  83, 3*HZ, 20, {3,1,2,0,2}, 0,
      0, { 4,22,21,30, 3, 0, 0, 0}, 3*HZ/2, 4 }, "720k" }, /*3 1/2 DD*/

{{4,  500, 16, 16, 4000, 4*HZ/10, 3*HZ, 10, SEL_DLY, 5,  83, 3*HZ, 20, {3,1,2,0,2}, 0,
      0, { 7, 4,25,22,31,21,29,11}, 3*HZ/2, 7 }, "1.44M" }, /*3 1/2 HD*/

{{5, 1000, 15,  8, 3000, 4*HZ/10, 3*HZ, 10, SEL_DLY, 5,  83, 3*HZ, 40, {3,1,2,0,2}, 0,
      0, { 7, 8, 4,25,28,22,31,21}, 3*HZ/2, 8 }, "2.88M AMI BIOS" }, /*3 1/2 ED*/

{{6, 1000, 15,  8, 3000, 4*HZ/10, 3*HZ, 10, SEL_DLY, 5,  83, 3*HZ, 40, {3,1,2,0,2}, 0,
      0, { 7, 8, 4,25,28,22,31,21}, 3*HZ/2, 8 }, "2.88M" } /*3 1/2 ED*/
/*    |  ---autodetected formats--   |      |      |
      read_track                     |      |    Name printed when booting
                                     |     Native format
                                   Frequency of disk change checks */
};

static struct floppy_drive_params drive_params[N_DRIVE];
static struct floppy_drive_struct drive_state[N_DRIVE];
static struct floppy_write_errors write_errors[N_DRIVE];
static struct floppy_raw_cmd raw_cmd;

/*
 * This struct defines the different floppy types.
 *
 * The 'stretch' tells if the tracks need to be doubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 */
static struct floppy_struct floppy_type[32] = {
	{    0, 0,0, 0,0,0x00,0x00,0x00,0x00,NULL    },	/*  0 no testing    */
	{  720, 9,2,40,0,0x2A,0x02,0xDF,0x50,"d360"  }, /*  1 360KB PC      */
	{ 2400,15,2,80,0,0x1B,0x00,0xDF,0x54,"h1200" },	/*  2 1.2MB AT      */
	{  720, 9,1,80,0,0x2A,0x02,0xDF,0x50,"D360"  },	/*  3 360KB SS 3.5" */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF,0x50,"D720"  },	/*  4 720KB 3.5"    */
	{  720, 9,2,40,1,0x23,0x01,0xDF,0x50,"h360"  },	/*  5 360KB AT      */
	{ 1440, 9,2,80,0,0x23,0x01,0xDF,0x50,"h720"  },	/*  6 720KB AT      */
	{ 2880,18,2,80,0,0x1B,0x00,0xCF,0x6C,"H1440" },	/*  7 1.44MB 3.5"   */
	{ 5760,36,2,80,0,0x1B,0x43,0xAF,0x54,"E2880" },	/*  8 2.88MB 3.5"   */
	{ 5760,36,2,80,0,0x1B,0x43,0xAF,0x54,"CompaQ"},	/*  9 2.88MB 3.5"   */

	{ 2880,18,2,80,0,0x25,0x00,0xDF,0x02,"h1440" }, /* 10 1.44MB 5.25"  */
	{ 3360,21,2,80,0,0x1C,0x00,0xCF,0x0C,"H1680" }, /* 11 1.68MB 3.5"   */
	{  820,10,2,41,1,0x25,0x01,0xDF,0x2E,"h410"  },	/* 12 410KB 5.25"   */
	{ 1640,10,2,82,0,0x25,0x02,0xDF,0x2E,"H820"  },	/* 13 820KB 3.5"    */
	{ 2952,18,2,82,0,0x25,0x00,0xDF,0x02,"h1476" },	/* 14 1.48MB 5.25"  */
	{ 3444,21,2,82,0,0x25,0x00,0xDF,0x0C,"H1722" },	/* 15 1.72MB 3.5"   */
	{  840,10,2,42,1,0x25,0x01,0xDF,0x2E,"h420"  },	/* 16 420KB 5.25"   */
	{ 1660,10,2,83,0,0x25,0x02,0xDF,0x2E,"H830"  },	/* 17 830KB 3.5"    */
	{ 2988,18,2,83,0,0x25,0x00,0xDF,0x02,"h1494" },	/* 18 1.49MB 5.25"  */
	{ 3486,21,2,83,0,0x25,0x00,0xDF,0x0C,"H1743" }, /* 19 1.74 MB 3.5"  */

	{ 1760,11,2,80,0,0x1C,0x09,0xCF,0x00,"h880"  }, /* 20 880KB 5.25"   */
	{ 2080,13,2,80,0,0x1C,0x01,0xCF,0x00,"D1040" }, /* 21 1.04MB 3.5"   */
	{ 2240,14,2,80,0,0x1C,0x19,0xCF,0x00,"D1120" }, /* 22 1.12MB 3.5"   */
	{ 3200,20,2,80,0,0x1C,0x20,0xCF,0x2C,"h1600" }, /* 23 1.6MB 5.25"   */
	{ 3520,22,2,80,0,0x1C,0x08,0xCF,0x2e,"H1760" }, /* 24 1.76MB 3.5"   */
	{ 3840,24,2,80,0,0x1C,0x20,0xCF,0x00,"H1920" }, /* 25 1.92MB 3.5"   */
	{ 6400,40,2,80,0,0x25,0x5B,0xCF,0x00,"E3200" }, /* 26 3.20MB 3.5"   */
	{ 7040,44,2,80,0,0x25,0x5B,0xCF,0x00,"E3520" }, /* 27 3.52MB 3.5"   */
	{ 7680,48,2,80,0,0x25,0x63,0xCF,0x00,"E3840" }, /* 28 3.84MB 3.5"   */

	{ 3680,23,2,80,0,0x1C,0x10,0xCF,0x00,"H1840" }, /* 29 1.84MB 3.5"   */
	{ 1600,10,2,80,0,0x25,0x02,0xDF,0x2E,"D800"  },	/* 30 800KB 3.5"    */
	{ 3200,20,2,80,0,0x1C,0x00,0xCF,0x2C,"H1600" }, /* 31 1.6MB 3.5"    */
};

#define	NUMBER(x)	(sizeof(x) / sizeof(*(x)))
#define SECTSIZE ( _FD_SECTSIZE(*floppy))

/* Auto-detection: Disk type used until the next media change occurs. */
struct floppy_struct *current_type[N_DRIVE] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

/*
 * User-provided type information. current_type points to
 * the respective entry of this array.
 */
struct floppy_struct user_params[N_DRIVE];

static int floppy_sizes[256];
static int floppy_blocksizes[256] = { 0, };

/*
 * The driver is trying to determine the correct media format
 * while probing is set. rw_interrupt() clears it after a
 * successful access.
 */
static int probing = 0;

/* Synchronization of FDC access. */
#define FD_COMMAND_DETECT -2
#define FD_COMMAND_NONE -1
#define FD_COMMAND_ERROR 2
#define FD_COMMAND_OKAY 3

static volatile int command_status = FD_COMMAND_NONE, fdc_busy = 0;
static struct wait_queue *fdc_wait = NULL, *command_done = NULL;
#define NO_SIGNAL (!(current->signal & ~current->blocked) || !interruptible)
#define CALL(x) if( (x) == -EINTR) return -EINTR;
#define _WAIT(x,i) CALL(ret=wait_til_done((x),i))
#define WAIT(x) _WAIT((x),interruptible)
#define IWAIT(x) _WAIT((x),1)

/* Errors during formatting are counted here. */
static int format_errors;

/* Format request descriptor. */
static struct format_descr format_req;

/*
 * Rate is 0 for 500kb/s, 1 for 300kbps, 2 for 250kbps
 * Spec1 is 0xSH, where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 */

/*
 * Track buffer
 * Because these are written to by the DMA controller, they must
 * not contain a 64k byte boundary crossing, or data will be
 * corrupted/lost. Alignment of these is enforced in boot/head.S.
 * Note that you must not change the sizes below without updating head.S.
 */
extern char floppy_track_buffer[512*2*MAX_BUFFER_SECTORS];
#define max_buffer_sectors MAX_BUFFER_SECTORS

int *errors;
typedef void (*done_f)(int);
struct cont_t {
void (*interrupt)(void); /* this is called after the interrupt of the
			  * main command */
void (*redo)(void); /* this is called to retry the operation */
void (*error)(void); /* this is called to tally an error */
done_f done; /* this is called to say if the operation has succeeded/failed */
} *cont;

static void floppy_ready(void);
static void floppy_start(void);
static void process_fd_request(void);
static void recalibrate_floppy(void);
static void floppy_shutdown(void);

static int floppy_grab_irq_and_dma(void);
static void floppy_release_irq_and_dma(void);

/*
 * The "reset" variable should be tested whenever an interrupt is scheduled,
 * after the commands have been sent. This is to ensure that the driver doesn't
 * get wedged when the interrupt doesn't come because of a failed command.
 * reset doesn't need to be tested before sending commands, because
 * output_byte is automatically disabled when reset is set.
 */
#define CHECK_RESET { if ( FDCS->reset ){ reset_fdc(); return ; } }
static void reset_fdc(void);

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
#define NO_TRACK -1
#define NEED_1_RECAL -2
#define NEED_2_RECAL -3

/* */
static int usage_count = 0;


/* buffer related variables */
static int buffer_track = -1;
static int buffer_drive = -1;
static int buffer_min = -1;
static int buffer_max = -1;

/* fdc related variables, should end up in a struct */
static struct floppy_fdc_state fdc_state[N_FDC];
static int fdc; /* current fdc */

static struct floppy_struct * floppy = floppy_type;
static unsigned char current_drive = 0;
static long current_count_sectors = 0;
static char *current_addr = 0;
static unsigned char sector_t; /* sector in track */

#ifdef DEBUGT
long unsigned debugtimer;
#endif

/*
 * Debugging
 * =========
 */
static inline void set_debugt(void)
{
#ifdef DEBUGT
	debugtimer = jiffies;
#endif
}

static inline void debugt(char *message)
{
#ifdef DEBUGT
  if ( DP->flags & DEBUGT )
	printk("%s dtime=%lu\n", message, jiffies-debugtimer );
#endif
}

/*
 * Bottom half floppy driver.
 * ==========================
 *
 * This part of the file contains the code talking directly to the hardware,
 * and also the main service loop (seek-configure-spinup-command)
 */

/*
 * disk change.
 * This routine is responsible for maintaining the FD_DISK_CHANGE flag,
 * and the last_checked date.
 *
 * last_checked is the date of the last check which showed 'no disk change'
 * FD_DISK_CHANGE is set under two conditions:
 * 1. The floppy has been changed after some i/o to that floppy already
 *    took place.
 * 2. No floppy disk is in the drive. This is done in order to ensure that
 *    requests are quickly flushed in case there is no disk in the drive. It
 *    follows that FD_DISK_CHANGE can only be cleared if there is a disk in
 *    the drive.
 *
 * For 1., maxblock is observed. Maxblock is 0 if no i/o has taken place yet.
 * For 2., FD_DISK_NEWCHANGE is watched. FD_DISK_NEWCHANGE is cleared on
 *  each seek. If a disk is present, the disk change line should also be
 *  cleared on each seek. Thus, if FD_DISK_NEWCHANGE is clear, but the disk
 *  change line is set, this means either that no disk is in the drive, or
 *  that it has been removed since the last seek.
 *
 * This means that we really have a third possibility too:
 *  The floppy has been changed after the last seek.
 */

static int disk_change(int drive)
{
	int fdc=FDC(drive);
#ifdef CONFIG_FLOPPY_SANITY
	if(jiffies < UDP->select_delay + UDRS->select_date)
		DPRINT("WARNING disk change called early\n");
	if(! (FDCS->dor & (0x10 << UNIT(drive))) ||
	   (FDCS->dor & 3) != UNIT(drive) ||
	   fdc != FDC(drive)){
		DPRINT("probing disk change on unselected drive\n");
		DPRINT3("drive=%d fdc=%d dor=%x\n",drive, FDC(drive),
			FDCS->dor);
	}
#endif

#ifdef DCL_DEBUG
	if (UDP->flags & FD_DEBUG){
		DPRINT1("checking disk change line for drive %d\n",drive);
		DPRINT1("jiffies=%ld\n", jiffies);
		DPRINT1("disk change line=%x\n",inb_p(FD_DIR)&0x80);
		DPRINT1("flags=%x\n",UDRS->flags);
	}
#endif
	if (UDP->flags & FD_BROKEN_DCL)
		return UTESTF(FD_DISK_CHANGED);
	if( (inb_p(FD_DIR) ^ UDP->flags) & 0x80){
		USETF(FD_VERIFY); /* verify write protection */		
		if(UDRS->maxblock){
			/* mark it changed */
			USETF(FD_DISK_CHANGED);

			/* invalidate its geometry */
			if (UDRS->keep_data >= 0) {
				if ((UDP->flags & FTD_MSG) &&
				    current_type[drive] != NULL)
					DPRINT("Disk type is undefined after "
					       "disk change\n");
				current_type[drive] = NULL;
				floppy_sizes[DRIVE(current_drive) + (FDC(current_drive) << 7)] = MAX_DISK_SIZE;
			}
		}
		/*USETF(FD_DISK_NEWCHANGE);*/
		return 1;
	} else {
		UDRS->last_checked=jiffies;
		UCLEARF(FD_DISK_NEWCHANGE);
	}
	return 0;
}

static inline int is_selected(int dor, int unit)
{
	return ( (dor  & (0x10 << unit)) && (dor &3) == unit);
}

static int set_dor(int fdc, char mask, char data)
{
	register unsigned char drive, unit, newdor,olddor;

	if(FDCS->address == -1)
		return -1;

	olddor = FDCS->dor;
	newdor =  (olddor & mask) | data;
	if ( newdor != olddor ){
		unit = olddor & 0x3;
		if(is_selected(olddor, unit) && !is_selected(newdor,unit)){
			drive = REVDRIVE(fdc,unit);
#ifdef DCL_DEBUG
	if (UDP->flags & FD_DEBUG){
		DPRINT("calling disk change from set_dor\n");
	}
#endif
			disk_change(drive);
		}
		FDCS->dor = newdor;
		outb_p(newdor, FD_DOR);

		unit = newdor & 0x3;
		if(!is_selected(olddor, unit) && is_selected(newdor,unit)){
			drive = REVDRIVE(fdc,unit);
			UDRS->select_date = jiffies;
		}
	}
	if ( newdor & 0xf0 )
		floppy_grab_irq_and_dma();
	if( olddor & 0xf0 )
		floppy_release_irq_and_dma();
	return olddor;
}

static void twaddle(void)
{
	if (DP->select_delay)
		return;
	outb_p(FDCS->dor & ~(0x10<<UNIT(current_drive)),FD_DOR);
	outb_p(FDCS->dor, FD_DOR);
	DRS->select_date = jiffies;
}

/* reset all driver information about the current fdc. This is needed after
 * a reset, and after a raw command. */
static void reset_fdc_info(int mode)
{
	int drive;

	FDCS->spec1 = FDCS->spec2 = -1;
	FDCS->need_configure = 1;
	FDCS->perp_mode = 1;
	FDCS->rawcmd = 0;
	for ( drive = 0; drive < N_DRIVE; drive++)
		if (FDC(drive) == fdc &&
		    ( mode || UDRS->track != NEED_1_RECAL))
			UDRS->track = NEED_2_RECAL;
}

/* selects the fdc and drive, and enables the fdc's input/dma. */
static void set_fdc(int drive)
{
	if (drive >= 0 && drive < N_DRIVE){
		fdc = FDC(drive);
		current_drive = drive;
	}
	set_dor(fdc,~0,8);
	set_dor(1-fdc, ~8, 0);
	if ( FDCS->rawcmd == 2 )
		reset_fdc_info(1);
	if( inb_p(FD_STATUS) != STATUS_READY )
		FDCS->reset = 1;
}

/* locks the driver */
static int lock_fdc(int drive, int interruptible)
{
	if(!usage_count){
		printk("trying to lock fdc while usage count=0\n");
		return -1;
	}
	floppy_grab_irq_and_dma();
	if (!current->pid)
		run_task_queue(&tq_timer);
	cli();
	while (fdc_busy && NO_SIGNAL)
		interruptible_sleep_on(&fdc_wait);
	if(fdc_busy){
		sti();
		return -EINTR;
	}
	fdc_busy = 1;
	sti();
	command_status = FD_COMMAND_NONE;
	set_fdc(drive);
	return 0;
}

#define LOCK_FDC(drive,interruptible) \
if(lock_fdc(drive,interruptible)) return -EINTR;

typedef void (*timeout_fn)(unsigned long);
static struct timer_list fd_timeout ={ NULL, NULL, 0, 0, 
					       (timeout_fn) floppy_shutdown };

/* unlocks the driver */
static inline void unlock_fdc(void)
{
	if (!fdc_busy)
		DPRINT("FDC access conflict!\n");

	if ( DEVICE_INTR )
		DPRINT1("device interrupt still active at FDC release: %p!\n",
			DEVICE_INTR);
	command_status = FD_COMMAND_NONE;
	del_timer(&fd_timeout);
	fdc_busy = 0;
	floppy_release_irq_and_dma();
	wake_up(&fdc_wait);
}

/* switches the motor off after a given timeout */
static void motor_off_callback(unsigned long nr)
{
	unsigned char mask = ~(0x10 << UNIT(nr));

	set_dor( FDC(nr), mask, 0 );
}

static struct timer_list motor_off_timer[N_DRIVE] = {
	{ NULL, NULL, 0, 0, motor_off_callback },
	{ NULL, NULL, 0, 1, motor_off_callback },
	{ NULL, NULL, 0, 2, motor_off_callback },
	{ NULL, NULL, 0, 3, motor_off_callback },
	{ NULL, NULL, 0, 4, motor_off_callback },
	{ NULL, NULL, 0, 5, motor_off_callback },
	{ NULL, NULL, 0, 6, motor_off_callback },
	{ NULL, NULL, 0, 7, motor_off_callback }
};

/* schedules motor off */
static void floppy_off(unsigned int drive)
{
	unsigned long volatile delta;
	register int fdc=FDC(drive);

	if( !(FDCS->dor & ( 0x10 << UNIT(drive))))
		return;

	del_timer(motor_off_timer+drive);

	/* make spindle stop in a position which minimizes spinup time
	 * next time */
	if (UDP->rps ){
		delta = jiffies - UDRS->first_read_date + HZ -
			UDP->spindown_offset;
		delta = (( delta * UDP->rps) % HZ ) / UDP->rps;
		motor_off_timer[drive].expires = UDP->spindown - delta;
	}
	add_timer(motor_off_timer+drive);
}

/*
 * cycle through all N_DRIVE floppy drives, for disk change testing.
 * stopping at current drive. This is done before any long operation, to
 * be sure to have up to date disk change information.
 */
static void scandrives(void)
{
	int i, drive, saved_drive;
	
	if (DP->select_delay)
		return;

	saved_drive = current_drive;
	for(i=0; i< N_DRIVE; i++){
		drive = (saved_drive + i + 1 ) % N_DRIVE;
		if ( UDRS->fd_ref == 0 || UDP->select_delay != 0)
			continue; /* skip closed drives */
		set_fdc(drive);
		if(! (set_dor( fdc, ~3, UNIT(drive) | ( 0x10 << UNIT(drive))) &
		      (0x10 << UNIT(drive))))
			/* switch the motor off again, if it was off to
			 * begin with */
			set_dor( fdc, ~( 0x10 << UNIT(drive) ), 0 );
	}
	set_fdc(saved_drive);
}

static struct timer_list fd_timer ={ NULL, NULL, 0, 0, 0 };

/* this function makes sure that the disk stays in the drive during the
 * transfer */
static void fd_watchdog(void)
{
#ifdef DCL_DEBUG
	if (DP->flags & FD_DEBUG){
		DPRINT("calling disk change from watchdog\n");
	}
#endif

	if ( disk_change(current_drive) ){
		DPRINT("disk removed during i/o\n");
		floppy_shutdown();
	} else {		
		del_timer(&fd_timer);
		fd_timer.function = (timeout_fn) fd_watchdog;
		fd_timer.expires = 10;
		add_timer(&fd_timer);
	}
}

static void main_command_interrupt(void)
{
	del_timer(&fd_timer);
	cont->interrupt();
}

/* waits for a delay (spinup or select) to pass */
static int wait_for_completion(int delay, timeout_fn function)
{
	if ( FDCS->reset ){
		reset_fdc(); /* do the reset during sleep to win time
			      * if we don't need to sleep, it's a good
			      * occasion anyways */
		return 1;
	}

	if ( jiffies < delay ){
		del_timer(&fd_timer);
		fd_timer.function = function;
		fd_timer.expires = delay  - jiffies;
		add_timer(&fd_timer);
		return 1;
	}
	return 0;
}

static int hlt_disabled=0;
static void floppy_disable_hlt(void)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	if(!hlt_disabled){
		hlt_disabled=1;
#ifdef HAVE_DISABLE_HLT
		disable_hlt();
#endif
	}
	restore_flags(flags);
}

static void floppy_enable_hlt(void)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	if(hlt_disabled){
		hlt_disabled=0;
#ifdef HAVE_DISABLE_HLT
		enable_hlt();
#endif
	}
	restore_flags(flags);
}
		

static void setup_DMA(void)
{
#ifdef CONFIG_FLOPPY_SANITY
	if (raw_cmd.length == 0){
		int i;

		printk("zero dma transfer size:");
		for(i=0; i< raw_cmd.cmd_count; i++)
			printk("%x,", raw_cmd.cmd[i]);
		printk("\n");
		cont->done(0);
		FDCS->reset = 1;
		return;
	}
	if ((!CURRENT ||
	     CURRENT->buffer != current_addr ||
	     raw_cmd.length > 512 * CURRENT->nr_sectors) &&
	    (current_addr < floppy_track_buffer ||
	     current_addr + raw_cmd.length >
	     floppy_track_buffer + 1024 * max_buffer_sectors)){
		printk("bad address. start=%p lg=%lx tb=%p\n",
		       current_addr, raw_cmd.length, floppy_track_buffer);
		if ( CURRENT ){
			printk("buffer=%p nr=%lx cnr=%lx\n",
			       CURRENT->buffer, CURRENT->nr_sectors,
			       CURRENT->current_nr_sectors);
		}
		cont->done(0);
		FDCS->reset=1;
		return;
	}
	if ((long) current_addr % 512 ){
		printk("non aligned address: %p\n", current_addr );
		cont->done(0);
		FDCS->reset=1;
		return;
	}
	if ( ( (long)current_addr & ~(64*1024-1) ) !=
	    ((long)(current_addr + raw_cmd.length-1)  & ~(64*1024-1))){
		printk("DMA crossing 64-K boundary %p-%p\n",
		       current_addr, current_addr + raw_cmd.length);
		cont->done(0);
		FDCS->reset=1;
		return;
	}

#endif
	cli();
	disable_dma(FLOPPY_DMA);
	clear_dma_ff(FLOPPY_DMA);
	set_dma_mode(FLOPPY_DMA,
		     (raw_cmd.flags & FD_RAW_READ)?
		     DMA_MODE_READ : DMA_MODE_WRITE);
	set_dma_addr(FLOPPY_DMA, (long) current_addr);
	set_dma_count(FLOPPY_DMA, raw_cmd.length);
	enable_dma(FLOPPY_DMA);
	sti();
	floppy_disable_hlt();
}

/* sends a command byte to the fdc */
static int output_byte(char byte)
{
	int counter;
	unsigned char status;

	if (FDCS->reset)
		return -1;
	for(counter = 0 ; counter < 10000 && !FDCS->reset ; counter++) {
		status = inb_p(FD_STATUS) &(STATUS_READY|STATUS_DIR|STATUS_DMA);
		if (!(status & STATUS_READY))
			continue;
		if (status == STATUS_READY){
			outb_p(byte,FD_DATA);
			return 0;
		} else
			break;
	}
	FDCS->reset = 1;
	if ( !initialising )
		DPRINT2("Unable to send byte %x to FDC. Status=%x\n",
			byte, status);
	return -1;
}
#define LAST_OUT(x) if(output_byte(x)){ reset_fdc();return;}

/* gets the response from the fdc */
static int result(void)
{
	int i = 0, counter, status;

	if (FDCS->reset)
		return -1;
	for (counter = 0 ; counter < 10000 && !FDCS->reset ; counter++) {
		status = inb_p(FD_STATUS)&
			(STATUS_DIR|STATUS_READY|STATUS_BUSY|STATUS_DMA);
		if (!(status & STATUS_READY))
			continue;
		if (status == STATUS_READY)
			return i;
		if (status & STATUS_DMA )
			break;
		if (status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)) {
			if (i >= MAX_REPLIES) {
				DPRINT("floppy_stat reply overrun\n");
				break;
			}
			reply_buffer[i++] = inb_p(FD_DATA);
		}
	}
	FDCS->reset = 1;
	if ( !initialising )
		DPRINT3("Getstatus times out (%x) on fdc %d [%d]\n",
			status, fdc, i);
	return -1;
}

/* Set perpendicular mode as required, based on data rate, if supported.
 * 82077 Now tested. 1Mbps data rate only possible with 82077-1.
 */
static inline void perpendicular_mode(void)
{
	unsigned char perp_mode;

	if (!floppy)
		return;
	if (floppy->rate & 0x40){
		switch(raw_cmd.rate){
		case 0:
			perp_mode=2;
			break;
		case 3:
			perp_mode=3;
			break;
		default:
			DPRINT("Invalid data rate for perpendicular mode!\n");
			cont->done(0);
			FDCS->reset = 1; /* convenient way to return to
					  * redo without to much hassle (deep
					  * stack et al. */
			return;
		}
	} else
		perp_mode = 0;
			
	if ( FDCS->perp_mode == perp_mode )
		return;
	if (FDCS->version >= FDC_82077_ORIG && FDCS->has_fifo) {
		output_byte(FD_PERPENDICULAR);
		output_byte(perp_mode);
		FDCS->perp_mode = perp_mode;
	} else if (perp_mode) {
		DPRINT("perpendicular mode not supported by this FDC.\n");
	}
} /* perpendicular_mode */

#define NOMINAL_DTR 500

/* Issue a "SPECIFY" command to set the step rate time, head unload time,
 * head load time, and DMA disable flag to values needed by floppy.
 *
 * The value "dtr" is the data transfer rate in Kbps.  It is needed
 * to account for the data rate-based scaling done by the 82072 and 82077
 * FDC types.  This parameter is ignored for other types of FDCs (i.e.
 * 8272a).
 *
 * Note that changing the data transfer rate has a (probably deleterious)
 * effect on the parameters subject to scaling for 82072/82077 FDCs, so
 * fdc_specify is called again after each data transfer rate
 * change.
 *
 * srt: 1000 to 16000 in microseconds
 * hut: 16 to 240 milliseconds
 * hlt: 2 to 254 milliseconds
 *
 * These values are rounded up to the next highest available delay time.
 */
static void fdc_specify(void)
{
	unsigned char spec1, spec2;
	int srt, hlt, hut;
	unsigned long dtr = NOMINAL_DTR;
	unsigned long scale_dtr = NOMINAL_DTR;
	int hlt_max_code = 0x7f;
	int hut_max_code = 0xf;

	if (FDCS->need_configure && FDCS->has_fifo) {
		if ( FDCS->reset )
			return;
		/* Turn on FIFO for 82077-class FDC (improves performance) */
		/* TODO: lock this in via LOCK during initialization */
		output_byte(FD_CONFIGURE);
		output_byte(0);
		output_byte(0x2A);	/* FIFO on, polling off, 10 byte threshold */
		output_byte(0);		/* precompensation from track 0 upwards */
		if ( FDCS->reset ){
			FDCS->has_fifo=0;
			return;
		}
		FDCS->need_configure = 0;
		/*DPRINT("FIFO enabled\n");*/
	}

	switch (raw_cmd.rate & 0x03) {
	case 3:
		dtr = 1000;
		break;
	case 1:
		dtr = 300;
		break;
	case 2:
		dtr = 250;
		break;
	}

	if (FDCS->version >= FDC_82072) {
		scale_dtr = dtr;
		hlt_max_code = 0x00; /* 0==256msec*dtr0/dtr (not linear!) */
		hut_max_code = 0x0; /* 0==256msec*dtr0/dtr (not linear!) */
	}

	/* Convert step rate from microseconds to milliseconds and 4 bits */
	srt = 16 - (DP->srt*scale_dtr/1000 + NOMINAL_DTR - 1)/NOMINAL_DTR;
	if (srt > 0xf)
		srt = 0xf;
	else if (srt < 0)
		srt = 0;

	hlt = (DP->hlt*scale_dtr/2 + NOMINAL_DTR - 1)/NOMINAL_DTR;
	if (hlt < 0x01)
		hlt = 0x01;
	else if (hlt > 0x7f)
		hlt = hlt_max_code;

	hut = (DP->hut*scale_dtr/16 + NOMINAL_DTR - 1)/NOMINAL_DTR;
	if (hut < 0x1)
		hut = 0x1;
	else if (hut > 0xf)
		hut = hut_max_code;

	spec1 = (srt << 4) | hut;
	spec2 = (hlt << 1);

	/* If these parameters did not change, just return with success */
	if (FDCS->spec1 != spec1 || FDCS->spec2 != spec2) {
		/* Go ahead and set spec1 and spec2 */
		output_byte(FD_SPECIFY);
		output_byte(FDCS->spec1 = spec1);
		output_byte(FDCS->spec2 = spec2);
	}
} /* fdc_specify */

/* Set the FDC's data transfer rate on behalf of the specified drive.
 * NOTE: with 82072/82077 FDCs, changing the data rate requires a reissue
 * of the specify command (i.e. using the fdc_specify function).
 */
static int fdc_dtr(void)
{
	/* If data rate not already set to desired value, set it. */
	if ( raw_cmd.rate == FDCS->dtr)
		return 0;
	
	/* Set dtr */
	outb_p(raw_cmd.rate, FD_DCR);
	
	/* TODO: some FDC/drive combinations (C&T 82C711 with TEAC 1.2MB)
	 * need a stabilization period of several milliseconds to be
	 * enforced after data rate changes before R/W operations.
	 * Pause 5 msec to avoid trouble. (Needs to be 2 jiffies)
	 */
	FDCS->dtr = raw_cmd.rate;
	return(wait_for_completion(jiffies+2,
				   (timeout_fn) floppy_ready));
} /* fdc_dtr */

static void tell_sector(void)
{
	printk(": track %d, head %d, sector %d, size %d",
	       R_TRACK, R_HEAD, R_SECTOR, R_SIZECODE);
} /* tell_sector */


/*
 * Ok, this error interpreting routine is called after a
 * DMA read/write has succeeded
 * or failed, so we check the results, and copy any buffers.
 * hhb: Added better error reporting.
 * ak: Made this into a separate routine.
 */
static int interpret_errors(void)
{
	char bad;

	if (inr!=7) {
		DPRINT("-- FDC reply error");
		FDCS->reset = 1;
		return 1;
	}

	/* check IC to find cause of interrupt */
	switch ((ST0 & ST0_INTR)>>6) {
		case 1:	/* error occurred during command execution */
			bad = 1;
			if (ST1 & ST1_WP) {
				DPRINT("Drive is write protected\n");
				CLEARF(FD_DISK_WRITABLE);
				cont->done(0);
				bad = 2;
			} else if (ST1 & ST1_ND) {
				SETF(FD_NEED_TWADDLE);
			} else if (ST1 & ST1_OR) {
				if (DP->flags & FTD_MSG )
					DPRINT("Over/Underrun - retrying\n");
				bad = 0;
			}else if(*errors >= DP->max_errors.reporting){
				DPRINT("");
				if (ST0 & ST0_ECE) {
					printk("Recalibrate failed!");
				} else if (ST2 & ST2_CRC) {
					printk("data CRC error");
					tell_sector();
				} else if (ST1 & ST1_CRC) {
					printk("CRC error");
					tell_sector();
				} else if ((ST1 & (ST1_MAM|ST1_ND)) || (ST2 & ST2_MAM)) {
					if (!probing) {
						printk("sector not found");
						tell_sector();
					} else
						printk("probe failed...");
				} else if (ST2 & ST2_WC) {	/* seek error */
					printk("wrong cylinder");
				} else if (ST2 & ST2_BC) {	/* cylinder marked as bad */
					printk("bad cylinder");
				} else {
					printk("unknown error. ST[0..2] are: 0x%x 0x%x 0x%x", ST0, ST1, ST2);
					tell_sector();
				}
				printk("\n");

			}
			if ( ST2 & ST2_WC || ST2 & ST2_BC)
				/* wrong cylinder => recal */
				DRS->track = NEED_2_RECAL;
			return bad;
		case 2: /* invalid command given */
			DPRINT("Invalid FDC command given!\n");
			cont->done(0);
			return 2;
		case 3:
			DPRINT("Abnormal termination caused by polling\n");
			cont->error();
			return 2;
		default: /* (0) Normal command termination */
			return 0;
	}
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on, the correct floppy is
 * selected, and the head is sitting on the right track).
 */
static void setup_rw_floppy(void)
{
	int i,ready_date,r, flags,dflags;
	timeout_fn function;

	flags = raw_cmd.flags;
	if ( flags & ( FD_RAW_READ | FD_RAW_WRITE))
		flags |= FD_RAW_INTR;

	if ((flags & FD_RAW_SPIN) && !(flags & FD_RAW_NO_MOTOR)){
		ready_date = DRS->spinup_date + DP->spinup;		
		/* If spinup will take a long time, rerun scandrives
		 * again just before spinup completion. Beware that
		 * after scandrives, we must again wait for selection.
		 */
		if ( ready_date > jiffies + DP->select_delay){
			ready_date -= DP->select_delay;
			function = (timeout_fn) floppy_start;
		} else
			function = (timeout_fn) setup_rw_floppy;

		/* wait until the floppy is spinning fast enough */
		if (wait_for_completion(ready_date,function))
			return;
	}
	dflags = DRS->flags;

	if ( (flags & FD_RAW_READ) || (flags & FD_RAW_WRITE))
		setup_DMA();
	
	if ( flags & FD_RAW_INTR )
		SET_INTR(main_command_interrupt);

	r=0;
	for(i=0; i< raw_cmd.cmd_count; i++)
		r|=output_byte( raw_cmd.cmd[i] );

#ifdef DEBUGT
	debugt("rw_command: ");
#endif
	if ( r ){
		reset_fdc();
		return;
	}

	if ( ! ( flags & FD_RAW_INTR )){
		inr = result();
		cont->interrupt();
	} else if ( flags & FD_RAW_NEED_DISK )
		fd_watchdog();
}

static int blind_seek;

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller.
 */
static void seek_interrupt(void)
{
#ifdef DEBUGT
	debugt("seek interrupt:");
#endif
	if (inr != 2 || (ST0 & 0xF8) != 0x20 ) {
		DPRINT("seek failed\n");
		DRS->track = NEED_2_RECAL;
		cont->error();
		cont->redo();
		return;
	}
	if (DRS->track >= 0 && DRS->track != ST1 && !blind_seek){
#ifdef DCL_DEBUG
		if (DP->flags & FD_DEBUG){
			DPRINT("clearing NEWCHANGE flag because of effective seek\n");
			DPRINT1("jiffies=%ld\n", jiffies);
		}
#endif
		CLEARF(FD_DISK_NEWCHANGE); /* effective seek */
		DRS->select_date = jiffies;
	}
	DRS->track = ST1;
	floppy_ready();
}

static void check_wp(void)
{
	if (TESTF(FD_VERIFY)) {
		/* check write protection */
		output_byte( FD_GETSTATUS );
		output_byte( UNIT(current_drive) );
		if ( result() != 1 ){
			FDCS->reset = 1;
			return;
		}
		CLEARF(FD_VERIFY);
		CLEARF(FD_NEED_TWADDLE);
#ifdef DCL_DEBUG
		if (DP->flags & FD_DEBUG){
			DPRINT("checking whether disk is write protected\n");
			DPRINT1("wp=%x\n",ST3 & 0x40);
		}
#endif
		if (!( ST3  & 0x40))
			SETF(FD_DISK_WRITABLE);
		else
			CLEARF(FD_DISK_WRITABLE);
	}
}

static void seek_floppy(void)
{
	int track;

	blind_seek=0;

#ifdef DCL_DEBUG
	if (DP->flags & FD_DEBUG){
		DPRINT("calling disk change from seek\n");
	}
#endif

	if (!TESTF(FD_DISK_NEWCHANGE) &&
	    disk_change(current_drive) &&
	    (raw_cmd.flags & FD_RAW_NEED_DISK)){
		/* the media changed flag should be cleared after the seek.
		 * If it isn't, this means that there is really no disk in
		 * the drive.
		 */
		SETF(FD_DISK_CHANGED);
		cont->done(0);
		cont->redo();
		return;
	}
	if ( DRS->track <= NEED_1_RECAL ){
		recalibrate_floppy();
		return;
	} else if (TESTF(FD_DISK_NEWCHANGE) &&
		   (raw_cmd.flags & FD_RAW_NEED_DISK) &&
		   (DRS->track <= NO_TRACK || DRS->track == raw_cmd.track)) {
		/* we seek to clear the media-changed condition. Does anybody
		 * know a more elegant way, which works on all drives? */
		if ( raw_cmd.track )
			track = raw_cmd.track - 1;
		else {
			if(DP->flags & FD_SILENT_DCL_CLEAR){
				set_dor(fdc, ~(0x10 << UNIT(current_drive)), 0);
				blind_seek = 1;
				raw_cmd.flags |= FD_RAW_NEED_SEEK;
			}
			track = 1;
		}
	} else {
		check_wp();
		if (raw_cmd.track != DRS->track &&
		    (raw_cmd.flags & FD_RAW_NEED_SEEK))
			track = raw_cmd.track;
		else {
			setup_rw_floppy();
			return;
		}
	}

	SET_INTR(seek_interrupt);
	output_byte(FD_SEEK);
	output_byte(UNIT(current_drive));
	LAST_OUT(track);
#ifdef DEBUGT
	debugt("seek command:");
#endif
}

static void recal_interrupt(void)
{
#ifdef DEBUGT
	debugt("recal interrupt:");
#endif
	if (inr !=2 )
		FDCS->reset = 1;
	else if (ST0 & ST0_ECE) {
	       	switch(DRS->track){
	       	case NEED_1_RECAL:
#ifdef DEBUGT
			debugt("recal interrupt need 1 recal:");
#endif
			/* after a second recalibrate, we still haven't
			 * reached track 0. Probably no drive. Raise an
			 * error, as failing immediately might upset 
			 * computers possessed by the Devil :-) */
			cont->error();
			cont->redo();
			return;
		case NEED_2_RECAL:
#ifdef DEBUGT
			debugt("recal interrupt need 2 recal:");
#endif
			/* If we already did a recalibrate, and we are not at
			 * track 0, this means we have moved. (The only way
			 * not to move at recalibration is to be already at
			 * track 0.) Clear the new change flag
			 */
#ifdef DCL_DEBUG
			if (DP->flags & FD_DEBUG){
				DPRINT("clearing NEWCHANGE flag because of second recalibrate\n");
			}
#endif

			CLEARF(FD_DISK_NEWCHANGE);
			DRS->select_date = jiffies;
			/* fall through */
		default:
#ifdef DEBUGT
			debugt("recal interrupt default:");
#endif
			/* Recalibrate moves the head by at most 80 steps. If
			 * after one recalibrate we don't have reached track
			 * 0, this might mean that we started beyond track 80.
			 * Try again.
			 */
			DRS->track = NEED_1_RECAL;
			break;
		}
	} else
		DRS->track = ST1;
	floppy_ready();
}

/*
 * Unexpected interrupt - Print as much debugging info as we can...
 * All bets are off...
 */
static void unexpected_floppy_interrupt(void)
{
	int i;
	if ( initialising )
		return;
	if(print_unex){
		DPRINT("unexpected interrupt\n");
		if ( inr >= 0 )
			for(i=0; i<inr; i++)
				printk("%d %x\n", i, reply_buffer[i] );
	}
	while(1){
		output_byte(FD_SENSEI);
		inr=result();
		if ( inr != 2 )
			break;
		if(print_unex){
			printk("sensei\n");
			for(i=0; i<inr; i++)
				printk("%d %x\n", i, reply_buffer[i] );
		}
	}
	FDCS->reset = 1;
}

struct tq_struct floppy_tq = 
{ 0, 0, (void *) (void *) unexpected_floppy_interrupt, 0 };

/* interrupt handler */
static void floppy_interrupt(int irq, struct pt_regs * regs)
{
	void (*handler)(void) = DEVICE_INTR;

	floppy_enable_hlt();
	CLEAR_INTR;
	if ( fdc >= N_FDC || FDCS->address == -1){
		/* we don't even know which FDC is the culprit */
		printk("DOR0=%x\n", fdc_state[0].dor);
		printk("floppy interrupt on bizarre fdc %d\n",fdc);
		printk("handler=%p\n", handler);
		return;
	}
	inr = result();
	if (!handler){
		unexpected_floppy_interrupt();
		return;
	}
	if ( inr == 0 ){
		do {
			output_byte(FD_SENSEI);
			inr = result();
		} while ( (ST0 & 0x83) != UNIT(current_drive) && inr == 2);
	}
	floppy_tq.routine = (void *)(void *) handler;
	queue_task_irq(&floppy_tq, &tq_timer);
}

static void recalibrate_floppy(void)
{
#ifdef DEBUGT
	debugt("recalibrate floppy:");
#endif
	SET_INTR(recal_interrupt);
	output_byte(FD_RECALIBRATE);
	LAST_OUT(UNIT(current_drive));
}

/*
 * Must do 4 FD_SENSEIs after reset because of ``drive polling''.
 */
static void reset_interrupt(void)
{
#ifdef DEBUGT
	debugt("reset interrupt:");
#endif
	fdc_specify();		/* reprogram fdc */
	result();		/* get the status ready for set_fdc */
	if ( FDCS->reset )
		cont->error(); /* a reset just after a reset. BAD! */
	cont->redo();
}

/*
 * reset is done by pulling bit 2 of DOR low for a while (old FDC's),
 * or by setting the self clearing bit 7 of STATUS (newer FDC's)
 */
static void reset_fdc(void)
{
	SET_INTR(reset_interrupt);
	FDCS->reset = 0;
	reset_fdc_info(0);
	if ( FDCS->version >= FDC_82077 )
		outb_p(0x80 | ( FDCS->dtr &3), FD_STATUS);
	else {
		outb_p(FDCS->dor & ~0x04, FD_DOR);
		udelay(FD_RESET_DELAY);
		outb(FDCS->dor, FD_DOR);
	}
}

static void empty(void)
{
}

void show_floppy(void)
{
	int i;

	printk("\n");
	printk("floppy driver state\n");
	printk("-------------------\n");
	for(i=0; i<N_FDC; i++){
		if(FDCS->address != -1){
			printk("dor %d = %x\n", i, fdc_state[i].dor );
			outb_p(fdc_state[i].address+2, fdc_state[i].dor);
			udelay(1000); /* maybe we'll catch an interrupt... */
		}
	}
	printk("status=%x\n", inb_p(FD_STATUS));
	printk("fdc_busy=%d\n", fdc_busy);
	if( DEVICE_INTR)
		printk("DEVICE_INTR=%p\n", DEVICE_INTR);
	if(floppy_tq.sync)
		printk("floppy_tq.routine=%p\n", floppy_tq.routine);
	if(fd_timer.prev)
		printk("fd_timer.function=%p\n", fd_timer.function);
	if(fd_timeout.prev){
		printk("timer_table=%p\n",fd_timeout.function);
		printk("expires=%ld\n",fd_timeout.expires);
		printk("now=%ld\n",jiffies);
	}
	printk("cont=%p\n", cont);
	printk("CURRENT=%p\n", CURRENT);
	printk("command_status=%d\n", command_status);
	printk("\n");
}

static void floppy_shutdown(void)
{
	CLEAR_INTR;
	floppy_tq.routine = (void *)(void *) empty;
	del_timer( &fd_timer);

	floppy_enable_hlt();
	disable_dma(FLOPPY_DMA);
	/* avoid dma going to a random drive after shutdown */

	if(!initialising)
		DPRINT("floppy timeout\n");
	FDCS->reset = 1;
	cont->done(0);
	cont->redo(); /* this will recall reset when needed */
}
/*typedef void (*timeout_fn)(unsigned long);*/

/* start motor, check media-changed condition and write protection */
static int start_motor( void (*function)(void)  )
{
	int mask, data;

	mask = 0xfc;
	data = UNIT(current_drive);
	if (!(raw_cmd.flags & FD_RAW_NO_MOTOR)){
		if(!(FDCS->dor & ( 0x10 << UNIT(current_drive) ) )){
			set_debugt();
			/* no read since this drive is running */
			DRS->first_read_date = 0;
			/* note motor start time if motor is not yet running */
			DRS->spinup_date = jiffies;
			data |= (0x10 << UNIT(current_drive));
		}
	} else
		if (FDCS->dor & ( 0x10 << UNIT(current_drive) ) )
			mask &= ~(0x10 << UNIT(current_drive));

	/* starts motor and selects floppy */
	del_timer(motor_off_timer + current_drive);
	set_dor( fdc, mask, data);

	/* wait_for_completion also schedules reset if needed. */
	return(wait_for_completion(DRS->select_date+DP->select_delay,
				   (timeout_fn) function));
}

static void floppy_ready(void)
{
	CHECK_RESET;
	if(start_motor(floppy_ready)) return;
	if(fdc_dtr()) return;

#ifdef DCL_DEBUG
	if (DP->flags & FD_DEBUG){
		DPRINT("calling disk change from floppy_ready\n");
	}
#endif

	if(!(raw_cmd.flags & FD_RAW_NO_MOTOR) &&
	   disk_change(current_drive) &&
	   !DP->select_delay)
		twaddle(); /* this clears the dcl on certain drive/controller
			    * combinations */

	if ( raw_cmd.flags & (FD_RAW_NEED_SEEK | FD_RAW_NEED_DISK)){
		perpendicular_mode();
		fdc_specify(); /* must be done here because of hut, hlt ... */
		seek_floppy();
	} else
		setup_rw_floppy();
}

static void floppy_start(void)
{
	del_timer(&fd_timeout);
	fd_timeout.expires = DP->timeout;
	add_timer(&fd_timeout);

	scandrives();
#ifdef DCL_DEBUG
		if (DP->flags & FD_DEBUG){
			DPRINT("setting NEWCHANGE in floppy_start\n");
		}
#endif
	SETF(FD_DISK_NEWCHANGE);
	floppy_ready();
}

/*
 * ========================================================================
 * here ends the bottom half. Exported routines are:
  * floppy_start, floppy_off, floppy_ready, lock_fdc, unlock_fdc, set_fdc,
 * start_motor, reset_fdc, reset_fdc_info, interpret_errors.
 * Initialisation also uses output_byte, result, set_dor, floppy_interrupt
 * and set_dor.
 * ========================================================================
 */
/*
 * General purpose continuations.
 * ==============================
 */

static void do_wakeup(void)
{
	del_timer(&fd_timeout);
	cont = 0;
	command_status += 2;
	wake_up(&command_done);
}

static struct cont_t wakeup_cont={
	empty,
	do_wakeup,
	empty,
	(done_f)empty
};

static int wait_til_done( void (*handler)(void ), int interruptible )
{
	int ret;

	floppy_tq.routine = (void *)(void *) handler;
	queue_task(&floppy_tq, &tq_timer);

	cli();
	while(command_status < 2 && NO_SIGNAL)
		if (current->pid)
			interruptible_sleep_on(&command_done);
		else {
			sti();
			run_task_queue(&tq_timer);
			cli();
		}
	if(command_status < 2){
		sti();
		floppy_shutdown();
		process_fd_request();
		return -EINTR;
	}
	sti();

	if ( FDCS->reset )
		command_status = FD_COMMAND_ERROR;
	if ( command_status == FD_COMMAND_OKAY )
		ret=0;
	else
		ret=-EIO;
	command_status = FD_COMMAND_NONE;
	return ret;
}

static void generic_done(int result)
{
	command_status = result;
	cont = &wakeup_cont;
}

static void generic_success(void)
{
	generic_done(1);
}

static void generic_failure(void)
{
	generic_done(0);
}

static void success_and_wakeup(void)
{
	generic_success();
	do_wakeup();
}

static void failure_and_wakeup(void)
{
	generic_failure();
	do_wakeup();
}

/*
 * formatting and rw support.
 * ==========================
 */

static int next_valid_format(void)
{
	int probed_format;

	probed_format = DRS->probed_format;
	while(1){
		if ( probed_format >= 8 ||
		    ! DP->autodetect[probed_format] ){
			DRS->probed_format = 0;
			return 1;
		}
		if ( floppy_type[DP->autodetect[probed_format]].sect ){
			DRS->probed_format = probed_format;
			return 0;
		}
		probed_format++;
	}
}

static void bad_flp_intr(void)
{
	if ( probing ){
		DRS->probed_format++;
		if ( !next_valid_format())
			return;
	}
	(*errors)++;
	if (*errors > DRWE->badness)
	        DRWE->badness = *errors;
	if (*errors > DP->max_errors.abort)
		cont->done(0);
	if (*errors > DP->max_errors.reset)
		FDCS->reset = 1;
	else if (*errors > DP->max_errors.recal)
		DRS->track = NEED_2_RECAL;
}

static void set_floppy(int device)
{
	if (TYPE(device))
		floppy = TYPE(device) + floppy_type;
	else
		floppy = current_type[ DRIVE(device) ];
}

/*
 * formatting and support.
 * =======================
 */
static void format_interrupt(void)
{
	switch (interpret_errors()){
	case 1:
		cont->error();
	case 2:
		break;
	case 0:
		cont->done(1);
	}
	cont->redo();
}

#define CODE2SIZE (ssize = ( ( 1 << SIZECODE ) + 3 ) >> 2)
#define FM_MODE(x,y) ((y) & ~(((x)->rate & 0x80 ) >>1))
#define CT(x) ( (x) | 0x40 )
static void setup_format_params(void)
{
	struct fparm {
		unsigned char track,head,sect,size;
	} *here = (struct fparm *)floppy_track_buffer;
	int il,n;
	int count,head_shift,track_shift;

	raw_cmd.flags = FD_RAW_WRITE | FD_RAW_INTR | FD_RAW_SPIN |
		/*FD_RAW_NEED_DISK |*/ FD_RAW_NEED_SEEK;
	raw_cmd.rate = floppy->rate & 0x3;
	raw_cmd.cmd_count = NR_F;
	COMMAND = FM_MODE(floppy,FD_FORMAT);
	DR_SELECT = UNIT(current_drive) + ( format_req.head << 2 );
	F_SIZECODE = FD_SIZECODE(floppy);
	F_SECT_PER_TRACK = floppy->sect << 2 >> F_SIZECODE;
	F_GAP = floppy->fmt_gap;
	F_FILL = FD_FILL_BYTE;

	current_addr = floppy_track_buffer;
	raw_cmd.length = 4 * F_SECT_PER_TRACK;

	/* allow for about 30ms for data transport per track */
	head_shift  = (F_SECT_PER_TRACK + 5) / 6;

	/* a ``cylinder'' is two tracks plus a little stepping time */
	track_shift = 2 * head_shift + 1;

	/* position of logical sector 1 on this track */
	n = (track_shift * format_req.track + head_shift * format_req.head )
		% F_SECT_PER_TRACK;

	/* determine interleave */
	il = 1;
	if (floppy->sect > DP->interleave_sect && F_SIZECODE == 2)
		il++;

	/* initialize field */
	for (count = 0; count < F_SECT_PER_TRACK; ++count) {
		here[count].track = format_req.track;
		here[count].head = format_req.head;
		here[count].sect = 0;
		here[count].size = F_SIZECODE;
	}
	/* place logical sectors */
	for (count = 1; count <= F_SECT_PER_TRACK; ++count) {
		here[n].sect = count;
		n = (n+il) % F_SECT_PER_TRACK;
		if (here[n].sect) { /* sector busy, find next free sector */
			++n;
			if (n>= F_SECT_PER_TRACK) {
				n-=F_SECT_PER_TRACK;
				while (here[n].sect) ++n;
			}
		}
	}
}

static void redo_format(void)
{
	raw_cmd.track = format_req.track << floppy->stretch;
	buffer_track = -1;
	setup_format_params();
	floppy_start();
#ifdef DEBUGT
	debugt("queue format request");
#endif
}

static struct cont_t format_cont={
	format_interrupt,
	redo_format,
	bad_flp_intr,
	generic_done };

static int do_format(int device, struct format_descr *tmp_format_req)
{
	int ret;
	int drive=DRIVE(device);

	LOCK_FDC(drive,1);
	set_floppy(device);
	if (!floppy ||
	    floppy->track > DP->tracks ||
	    tmp_format_req->track >= floppy->track ||
	    tmp_format_req->head >= floppy->head ||
	    (floppy->sect << 2) % (1 <<  FD_SIZECODE(floppy)) ||
	    !floppy->fmt_gap) {
		process_fd_request();
		return -EINVAL;
	}
	format_req = *tmp_format_req;
	format_errors = 0;
	cont = &format_cont;
	errors = &format_errors;
	IWAIT(redo_format);
	process_fd_request();
	return ret;
}

/*
 * Buffer read/write and support
 * =============================
 */

/* new request_done. Can handle physical sectors which are smaller than a
 * logical buffer */
static void request_done(int uptodate)
{
	int block;

	probing = 0;
	del_timer(&fd_timeout);

	if (!CURRENT){
		DPRINT("request list destroyed in floppy request done\n");
		return;
	}
	if (uptodate){
		/* maintain values for invalidation on geometry
		   change */
		block = current_count_sectors + CURRENT->sector;
		if (block > DRS->maxblock)
			DRS->maxblock=block;
		if ( block > floppy->sect)
			DRS->maxtrack = 1;

		/* unlock chained buffers */
		while (current_count_sectors && CURRENT &&
		       current_count_sectors >= CURRENT->current_nr_sectors ){
			current_count_sectors -= CURRENT->current_nr_sectors;
			CURRENT->nr_sectors -= CURRENT->current_nr_sectors;
			CURRENT->sector += CURRENT->current_nr_sectors;
			end_request(1);
		}
		if ( current_count_sectors && CURRENT){
			/* "unlock" last subsector */
			CURRENT->buffer += current_count_sectors <<9;
			CURRENT->current_nr_sectors -= current_count_sectors;
			CURRENT->nr_sectors -= current_count_sectors;
			CURRENT->sector += current_count_sectors;
			return;
		}

		if ( current_count_sectors && ! CURRENT )
			DPRINT("request list destroyed in floppy request done\n");

	} else {
		if(CURRENT->cmd == WRITE) {
			/* record write error information */
			DRWE->write_errors++;
			if(DRWE->write_errors == 1) {
				DRWE->first_error_sector = CURRENT->sector;
				DRWE->first_error_generation = DRS->generation;
			}
			DRWE->last_error_sector = CURRENT->sector;
			DRWE->last_error_generation = DRS->generation;
		}
		end_request(0);
	}
}

/* Interrupt handler evaluating the result of the r/w operation */
static void rw_interrupt(void)
{
	int nr_sectors, ssize;

	if ( ! DRS->first_read_date )
		DRS->first_read_date = jiffies;

	nr_sectors = 0;
	CODE2SIZE;
	nr_sectors = ((R_TRACK-TRACK)*floppy->head+R_HEAD-HEAD) *
		floppy->sect + ((R_SECTOR-SECTOR) <<  SIZECODE >> 2) -
			(sector_t % floppy->sect) % ssize;

#ifdef CONFIG_FLOPPY_SANITY
	if ( nr_sectors > current_count_sectors + ssize -
	    (current_count_sectors + sector_t) % ssize +
	    sector_t % ssize){
		DPRINT2("long rw: %x instead of %lx\n",
			nr_sectors, current_count_sectors);
		printk("rs=%d s=%d\n", R_SECTOR, SECTOR);
		printk("rh=%d h=%d\n", R_HEAD, HEAD);
		printk("rt=%d t=%d\n", R_TRACK, TRACK);
		printk("spt=%d st=%d ss=%d\n", SECT_PER_TRACK,
		       sector_t, ssize);
	}
#endif
	if ( nr_sectors < 0 )
		nr_sectors = 0;
	if ( nr_sectors < current_count_sectors )
		current_count_sectors = nr_sectors;

	switch (interpret_errors()){
	case 2:
		cont->redo();
		return;
	case 1:
		if (  !current_count_sectors){
			cont->error();
			cont->redo();
			return;
		}
		break;
	case 0:
		if (  !current_count_sectors){
			cont->redo();
			return;
		}
		current_type[current_drive] = floppy;
		floppy_sizes[DRIVE(current_drive) + (FDC(current_drive) << 7)] =
			floppy->size >> 1;
		break;
	}

	if (probing) {
		if (DP->flags & FTD_MSG)
			DPRINT2("Auto-detected floppy type %s in fd%d\n",
				floppy->name,current_drive);
		current_type[current_drive] = floppy;
		floppy_sizes[DRIVE(current_drive) + (FDC(current_drive) << 7)] =
			floppy->size >> 1;
		probing = 0;
	}

	if ( CT(COMMAND) != FD_READ || current_addr == CURRENT->buffer ){
		/* transfer directly from buffer */
		cont->done(1);
	} else if ( CT(COMMAND) == FD_READ){
		buffer_track = raw_cmd.track;
		buffer_drive = current_drive;
		if ( nr_sectors + sector_t > buffer_max )
			buffer_max = nr_sectors + sector_t;
	}
	cont->redo();
}

/* Compute maximal contiguous buffer size. */
static int buffer_chain_size(void)
{
	struct buffer_head *bh;
	int size;
	char *base;

	base = CURRENT->buffer;
	size = CURRENT->current_nr_sectors << 9;
	bh = CURRENT->bh;

	if (bh){
		bh = bh->b_reqnext;
		while ( bh && bh->b_data == base + size ){
			size += bh->b_size;
			bh = bh->b_reqnext;
		}
	}
	return size >> 9;
}

/* Compute the maximal transfer size */
static int transfer_size(int ssize, int max_sector, int max_size)
{
	if ( max_sector > sector_t + max_size)
		max_sector = sector_t + max_size;

	/* alignment */
	max_sector -= (max_sector % floppy->sect ) % ssize;

	/* transfer size, beginning not aligned */
	current_count_sectors = max_sector - sector_t ;

	return max_sector;
}

/*
 * Move data from/to the track buffer to/from the buffer cache.
 */
static void copy_buffer(int ssize, int max_sector, int max_sector_2)
{
	int remaining; /* number of transferred 512-byte sectors */
	struct buffer_head *bh;
	char *buffer, *dma_buffer;
	int size;

	if ( max_sector > max_sector_2 )
		max_sector = max_sector_2;

	max_sector = transfer_size(ssize, max_sector, CURRENT->nr_sectors);

	if (current_count_sectors <= 0 && CT(COMMAND) == FD_WRITE &&
	    buffer_max > sector_t + CURRENT->nr_sectors){
		current_count_sectors = buffer_max - sector_t;
		if ( current_count_sectors > CURRENT->nr_sectors )
			current_count_sectors = CURRENT->nr_sectors;
	}
	remaining = current_count_sectors << 9;
#ifdef CONFIG_FLOPPY_SANITY
	if ((remaining >> 9) > CURRENT->nr_sectors  && 
	    CT(COMMAND) == FD_WRITE ){
		DPRINT("in copy buffer\n");
		printk("current_count_sectors=%ld\n", current_count_sectors);
		printk("remaining=%d\n", remaining >> 9);
		printk("CURRENT->nr_sectors=%ld\n",CURRENT->nr_sectors);
		printk("CURRENT->current_nr_sectors=%ld\n",
		       CURRENT->current_nr_sectors);
		printk("max_sector=%d\n", max_sector);
		printk("ssize=%d\n", ssize);
	}
#endif

	if ( max_sector > buffer_max )
		buffer_max = max_sector;

	dma_buffer = floppy_track_buffer + ((sector_t - buffer_min) << 9);

	bh = CURRENT->bh;
	size = CURRENT->current_nr_sectors << 9;
	buffer = CURRENT->buffer;

	while ( remaining > 0){
		if ( size > remaining )
			size = remaining;
#ifdef CONFIG_FLOPPY_SANITY
		if (dma_buffer + size >
		    floppy_track_buffer + (max_buffer_sectors << 10) ||
		    dma_buffer < floppy_track_buffer ){
			DPRINT1("buffer overrun in copy buffer %d\n",
				(int) ((floppy_track_buffer - dma_buffer) >>9));
			printk("sector_t=%d buffer_min=%d\n",
			       sector_t, buffer_min);
			printk("current_count_sectors=%ld\n",
			       current_count_sectors);
			if ( CT(COMMAND) == FD_READ )
				printk("read\n");
			if ( CT(COMMAND) == FD_READ )
				printk("write\n");
			break;
		}
		if ( ((unsigned long)buffer) % 512 )
			DPRINT1("%p buffer not aligned\n", buffer);
#endif
		if ( CT(COMMAND) == FD_READ )
			memcpy( buffer, dma_buffer, size);
		else
			memcpy( dma_buffer, buffer, size);
		remaining -= size;
		if ( !remaining)
			break;

		dma_buffer += size;
		bh = bh->b_reqnext;
#ifdef CONFIG_FLOPPY_SANITY
		if ( !bh){
			DPRINT("bh=null in copy buffer after copy\n");
			break;
		}
#endif
		size = bh->b_size;
		buffer = bh->b_data;
	}
#ifdef CONFIG_FLOPPY_SANITY
	if ( remaining ){
		if ( remaining > 0 )
			max_sector -= remaining >> 9;
		DPRINT1("weirdness: remaining %d\n", remaining>>9);
	}
#endif
}

/*
 * Formulate a read/write request.
 * this routine decides where to load the data (directly to buffer, or to
 * tmp floppy area), how much data to load (the size of the buffer, the whole
 * track, or a single sector)
 * All floppy_track_buffer handling goes in here. If we ever add track buffer
 * allocation on the fly, it should be done here. No other part should need
 * modification.
 */

static int make_raw_rw_request(void)
{
	int aligned_sector_t;
	int max_sector, max_size, tracksize, ssize;

	set_fdc(DRIVE(CURRENT->dev));

	raw_cmd.flags = FD_RAW_SPIN | FD_RAW_NEED_DISK | FD_RAW_NEED_DISK |
		FD_RAW_NEED_SEEK;
	raw_cmd.cmd_count = NR_RW;
	if (CURRENT->cmd == READ){
		raw_cmd.flags |= FD_RAW_READ;
		COMMAND = FM_MODE(floppy,FD_READ);
	} else if (CURRENT->cmd == WRITE){
		raw_cmd.flags |= FD_RAW_WRITE;
		COMMAND = FM_MODE(floppy,FD_WRITE);
	} else {
		DPRINT("make_raw_rw_request: unknown command\n");
		return 0;
	}

	max_sector = floppy->sect * floppy->head;
	TRACK = CURRENT->sector / max_sector;
	sector_t = CURRENT->sector % max_sector;
	if ( floppy->track && TRACK >= floppy->track )
		return 0;
	HEAD = sector_t / floppy->sect;

	if ( TESTF( FD_NEED_TWADDLE) && sector_t < floppy->sect )
		max_sector = floppy->sect;

	/* 2M disks have phantom sectors on the first track */
	if ( (floppy->rate & FD_2M ) && (!TRACK) && (!HEAD)){
		max_sector = 2 * floppy->sect / 3;
		if (sector_t >= max_sector){
			current_count_sectors =  (floppy->sect - sector_t);
			if ( current_count_sectors > CURRENT->nr_sectors )
				current_count_sectors = CURRENT->nr_sectors;
			return 1;
		}
		SIZECODE = 2;
	} else
		SIZECODE = FD_SIZECODE(floppy);
	raw_cmd.rate = floppy->rate & 3;
	if ((floppy->rate & FD_2M) &&
	    (TRACK || HEAD ) &&
	    raw_cmd.rate == 2)
		raw_cmd.rate = 1;

	if ( SIZECODE )
		SIZECODE2 = 0xff;
	else
		SIZECODE2 = 0x80;
	raw_cmd.track = TRACK << floppy->stretch;
	DR_SELECT = UNIT(current_drive) + ( HEAD << 2 );
	GAP = floppy->gap;
	CODE2SIZE;
	SECT_PER_TRACK = floppy->sect << 2 >> SIZECODE;
	SECTOR = ((sector_t % floppy->sect) << 2 >> SIZECODE) + 1;
	tracksize = floppy->sect - floppy->sect % ssize;
	if ( tracksize < floppy->sect ){
		SECT_PER_TRACK ++;
		if (  tracksize <= sector_t % floppy->sect)
			SECTOR--;
		while ( tracksize <= sector_t % floppy->sect){
			while( tracksize + ssize > floppy->sect ){
				SIZECODE--;
				ssize >>= 1;
			}
			SECTOR++; SECT_PER_TRACK ++;
			tracksize += ssize;
		}
		max_sector = HEAD * floppy->sect + tracksize;
	} else if ( !TRACK && !HEAD && !( floppy->rate & FD_2M ) && probing)
		max_sector = floppy->sect;

	aligned_sector_t = sector_t - ( sector_t % floppy->sect ) % ssize;
	max_size = CURRENT->nr_sectors;
	if ((raw_cmd.track == buffer_track) && (current_drive == buffer_drive) &&
	    (sector_t >= buffer_min) && (sector_t < buffer_max)) {
		/* data already in track buffer */
		if (CT(COMMAND) == FD_READ) {
			copy_buffer(1, max_sector, buffer_max);
			return 1;
		}
	} else if (aligned_sector_t != sector_t || CURRENT->nr_sectors < ssize){
		if (CT(COMMAND) == FD_WRITE){
			if(sector_t + CURRENT->nr_sectors > ssize &&
			   sector_t + CURRENT->nr_sectors < ssize + ssize)
				max_size = ssize + ssize;
			else
				max_size = ssize;
		}
		raw_cmd.flags &= ~FD_RAW_WRITE;
		raw_cmd.flags |= FD_RAW_READ;
		COMMAND = FM_MODE(floppy,FD_READ);
	} else if ((long)CURRENT->buffer <= LAST_DMA_ADDR ) {
		int direct, indirect;

		indirect= transfer_size(ssize,max_sector,max_buffer_sectors*2) -
			sector_t;

		max_size = buffer_chain_size();
		if ( max_size > ( LAST_DMA_ADDR - ((long) CURRENT->buffer))>>9)
			max_size=(LAST_DMA_ADDR - ((long)CURRENT->buffer))>>9;
		/* 64 kb boundaries */
		if ( ((max_size << 9) + ((long) CURRENT->buffer)) / K_64 !=
		     ((long) CURRENT->buffer ) / K_64 )
			max_size = ( K_64 - ((long) CURRENT->buffer) % K_64)>>9;
		direct = transfer_size(ssize,max_sector,max_size) - sector_t;
		/*
		 * We try to read tracks, but if we get too many errors, we
		 * go back to reading just one sector at a time.
		 *
		 * This means we should be able to read a sector even if there
		 * are other bad sectors on this track.
		 */
		if (!direct ||
		    (indirect * 2 > direct * 3 &&
		     *errors < DP->max_errors.read_track &&
		     /*!TESTF( FD_NEED_TWADDLE) &&*/
		     ((!probing || (DP->read_track&(1<<DRS->probed_format)))))){
			max_size = CURRENT->nr_sectors;
		} else {
			current_addr = CURRENT->buffer;
			raw_cmd.length = current_count_sectors << 9;
			if (raw_cmd.length == 0){
				DPRINT("zero dma transfer attempted from make_raw_request\n");
				DPRINT3("indirect=%d direct=%d sector_t=%d",
					indirect, direct, sector_t);
				return 0;
			}
			return 2;
		}
	}

	if ( CT(COMMAND) == FD_READ )
		max_size = max_sector; /* unbounded */

	/* claim buffer track if needed */
	if (buffer_track != raw_cmd.track ||  /* bad track */
	    buffer_drive !=current_drive || /* bad drive */
	    sector_t > buffer_max ||
	    sector_t < buffer_min ||
	    ((CT(COMMAND) == FD_READ ||
	      (aligned_sector_t == sector_t && CURRENT->nr_sectors >= ssize ))&&
	     max_sector > 2 * max_buffer_sectors + buffer_min &&
	     max_size + sector_t > 2 * max_buffer_sectors + buffer_min)
	    /* not enough space */ ){
		buffer_track = -1;
		buffer_drive = current_drive;
		buffer_max = buffer_min = aligned_sector_t;
	}
	current_addr = floppy_track_buffer +((aligned_sector_t-buffer_min )<<9);

	if ( CT(COMMAND) == FD_WRITE ){
		/* copy write buffer to track buffer.
		 * if we get here, we know that the write
		 * is either aligned or the data already in the buffer
		 * (buffer will be overwritten) */
#ifdef CONFIG_FLOPPY_SANITY
		if (sector_t != aligned_sector_t && buffer_track == -1 )
			DPRINT("internal error offset !=0 on write\n");
#endif
		buffer_track = raw_cmd.track;
		buffer_drive = current_drive;
		copy_buffer(ssize, max_sector, 2*max_buffer_sectors+buffer_min);
	} else
		transfer_size(ssize, max_sector,
			      2*max_buffer_sectors+buffer_min-aligned_sector_t);

	/* round up current_count_sectors to get dma xfer size */
	raw_cmd.length = sector_t+current_count_sectors-aligned_sector_t;
	raw_cmd.length = ((raw_cmd.length -1)|(ssize-1))+1;
	raw_cmd.length <<= 9;
#ifdef CONFIG_FLOPPY_SANITY
	if ((raw_cmd.length < current_count_sectors << 9) ||
	    (current_addr != CURRENT->buffer &&
	     CT(COMMAND) == FD_WRITE &&
	     (aligned_sector_t + (raw_cmd.length >> 9) > buffer_max ||
	      aligned_sector_t < buffer_min )) ||
	    raw_cmd.length % ( 128 << SIZECODE ) ||
	    raw_cmd.length <= 0 || current_count_sectors <= 0){
		DPRINT2("fractionary current count b=%lx s=%lx\n",
			raw_cmd.length, current_count_sectors);
		if ( current_addr != CURRENT->buffer )
			printk("addr=%d, length=%ld\n",
			       (int) ((current_addr - floppy_track_buffer ) >> 9),
			       current_count_sectors);
		printk("st=%d ast=%d mse=%d msi=%d\n",
		       sector_t, aligned_sector_t, max_sector, max_size);
		printk("ssize=%x SIZECODE=%d\n", ssize, SIZECODE);
		printk("command=%x SECTOR=%d HEAD=%d, TRACK=%d\n",
		       COMMAND, SECTOR, HEAD, TRACK);
		printk("buffer drive=%d\n", buffer_drive);
		printk("buffer track=%d\n", buffer_track);
		printk("buffer_min=%d\n", buffer_min );
		printk("buffer_max=%d\n", buffer_max );
		return 0;
	}

	if (current_addr != CURRENT->buffer ){
		if (current_addr < floppy_track_buffer ||
		    current_count_sectors < 0 ||
		    raw_cmd.length < 0 ||
		    current_addr + raw_cmd.length >
		    floppy_track_buffer + (max_buffer_sectors  << 10)){
			DPRINT("buffer overrun in schedule dma\n");
			printk("sector_t=%d buffer_min=%d current_count=%ld\n",
			       sector_t, buffer_min,
			       raw_cmd.length >> 9 );
			printk("current_count_sectors=%ld\n",
			       current_count_sectors);
			if ( CT(COMMAND) == FD_READ )
				printk("read\n");
			if ( CT(COMMAND) == FD_READ )
				printk("write\n");
			return 0;
		}
	} else if (raw_cmd.length > CURRENT->nr_sectors << 9 ||
		   current_count_sectors > CURRENT->nr_sectors){
		DPRINT("buffer overrun in direct transfer\n");
		return 0;
	} else if ( raw_cmd.length < current_count_sectors << 9 ){
		DPRINT("more sectors than bytes\n");
		printk("bytes=%ld\n", raw_cmd.length >> 9 );
		printk("sectors=%ld\n", current_count_sectors);
	}
	if (raw_cmd.length == 0){
		DPRINT("zero dma transfer attempted from make_raw_request\n");
		return 0;
	}
#endif
	return 2;
}

static void redo_fd_request(void)
{
#define REPEAT {request_done(0); continue; }
	int device;
	int tmp;
	int error;

	error = -1;
	if (current_drive < N_DRIVE)
		floppy_off(current_drive);

	if (CURRENT && CURRENT->dev < 0) return;

	while(1){
		if (!CURRENT) {
			CLEAR_INTR;
			unlock_fdc();
			return;
		}
		if (MAJOR(CURRENT->dev) != MAJOR_NR)
			panic(DEVICE_NAME ": request list destroyed");
		if (CURRENT->bh && !CURRENT->bh->b_lock)
			panic(DEVICE_NAME ": block not locked");
#if 0
		if (!CURRENT->bh->b_count && 
		    (CURRENT->errors || error == CURRENT->dev)){
			error=CURRENT->dev;
			DPRINT("skipping read ahead buffer\n");
			REPEAT;
		}
#endif
		error=-1;
		device = CURRENT->dev;
		set_fdc( DRIVE(device));

		del_timer(&fd_timeout);
		fd_timeout.expires = DP->timeout;
		add_timer(&fd_timeout);

		set_floppy(device);
		if(start_motor(redo_fd_request)) return;
		if(test_bit(current_drive, &fake_change) ||
		   TESTF(FD_DISK_CHANGED)){
			DPRINT("disk absent or changed during operation\n");
			REPEAT;
		}
		if (!floppy) { /* Autodetection */
			if (!probing){
				DRS->probed_format = 0;
				if ( next_valid_format() ){
					DPRINT("no autodetectable formats\n");
					floppy = NULL;
					REPEAT;
				}
			}
			probing = 1;
			floppy = floppy_type+DP->autodetect[DRS->probed_format];
		} else
			probing = 0;
		errors = & (CURRENT->errors);
		tmp = make_raw_rw_request();
		if ( tmp < 2 ){
			request_done(tmp);
			continue;
		}

		if (TESTF(FD_NEED_TWADDLE))
			twaddle();
		floppy_tq.routine = (void *)(void *) floppy_start;
		queue_task(&floppy_tq, &tq_timer);
#ifdef DEBUGT
		debugt("queue fd request");
#endif
		return;
	}
#undef REPEAT
}

static struct cont_t rw_cont={
	rw_interrupt,
	redo_fd_request,
	bad_flp_intr,
	request_done };

struct tq_struct request_tq = 
{ 0, 0, (void *) (void *) redo_fd_request, 0 };

static void process_fd_request(void)
{
	cont = &rw_cont;
	queue_task(&request_tq, &tq_timer);
}

static void do_fd_request(void)
{
	if (fdc_busy)
		/* fdc busy, this new request will be treated when the
		   current one is done */
		return;
	/* fdc_busy cannot be set by an interrupt or a bh */
	floppy_grab_irq_and_dma();
	fdc_busy=1;
	process_fd_request();
}

static struct cont_t poll_cont={
	success_and_wakeup,
	floppy_ready,
	generic_failure,
	generic_done };

static int poll_drive(int interruptible, int flag){
	int ret;
	/* no auto-sense, just clear dcl */
	raw_cmd.flags= flag;
	raw_cmd.track=0;
	raw_cmd.cmd_count=0;
	cont = &poll_cont;
#ifdef DCL_DEBUG
		if (DP->flags & FD_DEBUG){
			DPRINT("setting NEWCHANGE in poll_drive\n");
		}
#endif
	SETF(FD_DISK_NEWCHANGE);
	WAIT(floppy_ready);
	return ret;
}

/*
 * User triggered reset
 * ====================
 */

static void reset_intr(void)
{
	printk("weird, reset interrupt called\n");
}

static struct cont_t reset_cont={
	reset_intr,
	success_and_wakeup,
	generic_failure,
	generic_done };

static int user_reset_fdc(int drive, int arg, int interruptible)
{
	int ret;

	ret=0;
	if(arg == FD_RESET_IF_NEEDED && !FDCS->reset)
		return 0;
	LOCK_FDC(drive,interruptible);
	if(arg == FD_RESET_ALWAYS)
		FDCS->reset=1;
	if ( FDCS->reset ){
		cont = &reset_cont;
		del_timer(&fd_timeout);
		fd_timeout.expires = DP->timeout;
		add_timer(&fd_timeout);
		WAIT(reset_fdc);
	}
	process_fd_request();
	return ret;
}

/*
 * Misc Ioctl's and support
 * ========================
 */
static int fd_copyout(void *param, volatile void *address, int size)
{
	int i;

	i = verify_area(VERIFY_WRITE,param,size);
	if (i)
		return i;
	memcpy_tofs(param,(void *) address, size);
	return 0;
}

#define COPYOUT(x) (fd_copyout( (void *)param, &(x), sizeof(x)))
#define COPYIN(x) (memcpy_fromfs( &(x), (void *) param, sizeof(x)),0)

static char *drive_name(int type, int drive )
{
	struct floppy_struct *floppy;	

	if ( type )
		floppy = floppy_type + type;
	else {
		if ( UDP->native_format )
			floppy = floppy_type + UDP->native_format;
		else
			return "(null)";
	}
	if ( floppy->name )
		return floppy->name;
	else
		return "(null)";
}

/* raw commands */
static struct cont_t raw_cmd_cont={
	success_and_wakeup,
	failure_and_wakeup,
	generic_failure,
	generic_done };

static int raw_cmd_ioctl(void *param)
{
	int i, drive, count, ret;

	if ( FDCS->rawcmd <= 1 )
		FDCS->rawcmd = 1;
	for ( drive= 0; drive < N_DRIVE; drive++){
		if ( FDC(drive) != fdc)
			continue;
		if ( drive == current_drive ){
			if ( UDRS->fd_ref > 1 ){
				FDCS->rawcmd = 2;
				break;
			}
		} else if ( UDRS->fd_ref ){
			FDCS->rawcmd = 2;
			break;
		}
	}

	if(FDCS->reset)
		return -EIO;

	COPYIN(raw_cmd);
	raw_cmd.rate &= 0x03;	
	count = raw_cmd.length;
	if (raw_cmd.flags & (FD_RAW_WRITE | FD_RAW_READ)){
		if(count > max_buffer_sectors * 1024 )
			return -ENOMEM;
		if(count == 0){
			printk("attempt to do a 0 byte dma transfer\n");
			return -EINVAL;
		}
		buffer_track = -1;
	}
	if ( raw_cmd.flags & FD_RAW_WRITE ){
		i = verify_area(VERIFY_READ, raw_cmd.data, count );
		if (i)
			return i;
		memcpy_fromfs(floppy_track_buffer, raw_cmd.data, count);
	}

	current_addr = floppy_track_buffer;
	cont = &raw_cmd_cont;
	IWAIT(floppy_start);
#ifdef DCL_DEBUG
	if (DP->flags & FD_DEBUG){
		DPRINT("calling disk change from raw_cmd ioctl\n");
	}
#endif
	if( disk_change(current_drive) )
		raw_cmd.flags |= FD_RAW_DISK_CHANGE;
	else
		raw_cmd.flags &= ~FD_RAW_DISK_CHANGE;
	if(raw_cmd.flags & FD_RAW_NO_MOTOR_AFTER)
		motor_off_callback(current_drive);	

	if ( !ret && !FDCS->reset ){
		raw_cmd.reply_count = inr;
		for( i=0; i< raw_cmd.reply_count; i++)
			raw_cmd.reply[i] = reply_buffer[i];
		if ( raw_cmd.flags & ( FD_RAW_READ | FD_RAW_WRITE ))
			raw_cmd.length = get_dma_residue(FLOPPY_DMA);
	} else
		ret = -EIO;
	DRS->track = NO_TRACK;
	if ( ret )
		return ret;

	if ( raw_cmd.flags & FD_RAW_READ ){
		i=fd_copyout( raw_cmd.data, floppy_track_buffer, count);
		if (i)
			return i;
	}
       
	return COPYOUT(raw_cmd);
}

static int invalidate_drive(int rdev)
{
	/* invalidate the buffer track to force a reread */
	set_bit( DRIVE(rdev), &fake_change);
	process_fd_request();
	check_disk_change(rdev);
	return 0;
}

static int fd_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
    unsigned long param)
{
#define IOCTL_MODE_BIT 8
#define OPEN_WRITE_BIT 16
#define IOCTL_ALLOWED (filp && (filp->f_mode & IOCTL_MODE_BIT))

	struct floppy_struct newparams;
	struct format_descr tmp_format_req;
	int i,device,drive,type,cnt;
	struct floppy_struct *this_floppy;
	char *name;

	device = inode->i_rdev;
	switch (cmd) {
		RO_IOCTLS(device,param);
	}
	type = TYPE(device);
	drive = DRIVE(device);
	switch (cmd) {
	case FDGETDRVTYP:
		i=verify_area(VERIFY_WRITE,(void *) param,16);
		if (i)
			return i;
		name = drive_name(type,drive);
		for ( cnt=0; cnt<16; cnt++){
			put_fs_byte(name[cnt],
				    ((char*)param)+cnt);
			if ( ! *name )
				break;
		}
		return 0;
	case FDGETMAXERRS:
		return COPYOUT(UDP->max_errors);
	case FDGETPRM:
		if (type)
			this_floppy = &floppy_type[type];
		else if ((this_floppy = current_type[drive]) ==
			 NULL)
			return -ENODEV;
		return COPYOUT(this_floppy[0]);
	case FDPOLLDRVSTAT:
		LOCK_FDC(drive,1);
		CALL(poll_drive(1, FD_RAW_NEED_DISK));
		process_fd_request();
		/* fall through */
	case FDGETDRVSTAT:
		return COPYOUT(*UDRS);
	case FDGETFDCSTAT:
		return COPYOUT(*UFDCS);
	case FDGETDRVPRM:
		return COPYOUT(*UDP);
	case FDWERRORGET:
		return COPYOUT(*UDRWE);
	}
	if (!IOCTL_ALLOWED)
		return -EPERM;
	switch (cmd) {
	case FDWERRORCLR:
                UDRWE->write_errors = 0;
                UDRWE->first_error_sector = 0;
                UDRWE->first_error_generation = 0;
                UDRWE->last_error_sector = 0;
                UDRWE->last_error_generation = 0;
                UDRWE->badness = 0;
                return 0;
	case FDRAWCMD:
		if (type)
			return -EINVAL;
		LOCK_FDC(drive,1);
		set_floppy(device);
		CALL(i = raw_cmd_ioctl((void *) param));
		process_fd_request();
		return i;
	case FDFMTTRK:
		if (UDRS->fd_ref != 1)
			return -EBUSY;
		COPYIN(tmp_format_req);
		return do_format(device, &tmp_format_req);
	case FDSETMAXERRS:
		return COPYIN(UDP->max_errors);
	case FDFMTBEG:
		return 0;
	case FDCLRPRM:
		LOCK_FDC(drive,1);
		current_type[drive] = NULL;
		floppy_sizes[drive] = MAX_DISK_SIZE;
		UDRS->keep_data = 0;
		return invalidate_drive(device);
	case FDFMTEND:
	case FDFLUSH:
		LOCK_FDC(drive,1);
		return invalidate_drive(device);
	case FDSETPRM:
	case FDDEFPRM:
		COPYIN(newparams);
		/* sanity checking for parameters.*/
		if(newparams.sect <= 0 ||
		   newparams.head <= 0 ||
		   newparams.track <= 0 ||
		   newparams.track >
		   UDP->tracks>>newparams.stretch)
			return -EINVAL;
		if ( type){
			if ( !suser() )
				return -EPERM;
			LOCK_FDC(drive,1);
			for ( cnt = 0; cnt < N_DRIVE; cnt++){
				if (TYPE(drive_state[cnt].fd_device) == type &&
				    drive_state[cnt].fd_ref)
					set_bit(drive, &fake_change);
			}
			floppy_type[type] = newparams;
			floppy_type[type].name="user format";
			for (cnt = type << 2 ;
			     cnt < (type << 2 ) + 4 ;
			     cnt++)
				floppy_sizes[cnt]=
					floppy_sizes[cnt+0x80]=
						floppy_type[type].size>>1;
			process_fd_request();
			for ( cnt = 0; cnt < N_DRIVE; cnt++){
				if (TYPE(drive_state[cnt].fd_device) == type &&
				    drive_state[cnt].fd_ref)
					check_disk_change(drive_state[cnt].
							  fd_device);
			}
			return 0;
		}

		LOCK_FDC(drive,1);
		if ( cmd != FDDEFPRM )
			/* notice a disk change immediately, else
			 * we loose our settings immediately*/
			CALL(poll_drive(1,0));
		user_params[drive] = newparams;
		if (buffer_drive == drive &&
		    buffer_max > user_params[drive].sect)
			buffer_max=user_params[drive].sect;
		current_type[drive] = &user_params[drive];
		floppy_sizes[drive] = user_params[drive].size >> 1;
		if (cmd == FDDEFPRM)
			DRS->keep_data = -1;
		else
			DRS->keep_data = 1;
		/* invalidation. Invalidate only when needed, i.e.
		 * when there are already sectors in the buffer cache
		 * whose number will change. This is useful, because
		 * mtools often changes the geometry of the disk after
		 * looking at the boot block */
		if (DRS->maxblock >
		    user_params[drive].sect ||
		    DRS->maxtrack )
			invalidate_drive(device);
		else
			process_fd_request();
		return 0;
	case FDRESET:
		return user_reset_fdc( drive, (int)param, 1);
	case FDMSGON:
		UDP->flags |= FTD_MSG;
		return 0;
	case FDMSGOFF:
		UDP->flags &= ~FTD_MSG;
		return 0;
	case FDSETEMSGTRESH:
		UDP->max_errors.reporting =
			(unsigned short) (param & 0x0f);
		return 0;
	case FDTWADDLE:
		LOCK_FDC(drive,1);
		twaddle();
		process_fd_request();
	}
	if ( ! suser() )
		return -EPERM;
	switch(cmd){
	case FDSETDRVPRM:
		return COPYIN(*UDP);
	default:
		return -EINVAL;
	}
	return 0;
#undef IOCTL_ALLOWED
}

static void config_types(void)
{
	int first=1;
	int drive;

	/* read drive info out of physical cmos */
	drive=0;
	if (!UDP->cmos )
		UDP->cmos= FLOPPY0_TYPE;
	drive=1;
	if (!UDP->cmos && FLOPPY1_TYPE)
		UDP->cmos = FLOPPY1_TYPE;

	/* XXX */
	/* additional physical CMOS drive detection should go here */

	for (drive=0; drive < N_DRIVE; drive++){
		if (UDP->cmos >= 0 && UDP->cmos <= NUMBER(default_drive_params))
			memcpy((char *) UDP,
			       (char *) (&default_drive_params[(int)UDP->cmos].params),
			       sizeof(struct floppy_drive_params));
		if (UDP->cmos){
			if (first)
				printk("Floppy drive(s): ");
			else
				printk(", ");
			first=0;
			if (UDP->cmos > 0 ){
				ALLOWED_DRIVE_MASK |= 1 << drive;
				printk("fd%d is %s", drive,
				       default_drive_params[(int)UDP->cmos].name);
			} else
				printk("fd%d is unknown type %d",drive,
				       UDP->cmos);
		}
	}
	if(!first)
		printk("\n");
}

static int floppy_read(struct inode * inode, struct file * filp,
		       char * buf, int count)
{
	int drive = DRIVE(inode->i_rdev);

	check_disk_change(inode->i_rdev);
	if (UTESTF(FD_DISK_CHANGED))
		return -ENXIO;
	return block_read(inode, filp, buf, count);
}

static int floppy_write(struct inode * inode, struct file * filp,
			char * buf, int count)
{
	int block;
	int ret;
	int drive = DRIVE(inode->i_rdev);

	if(!UDRS->maxblock)
		UDRS->maxblock=1;/* make change detectable */
	check_disk_change(inode->i_rdev);
	if (UTESTF(FD_DISK_CHANGED)) 
		return -ENXIO;
	if(!UTESTF(FD_DISK_WRITABLE))
		return -EROFS;
	block = (filp->f_pos + count) >> 9;
	if(block > UDRS->maxblock)
		UDRS->maxblock = block;
	ret= block_write(inode, filp, buf, count);
	return ret;
}

static void floppy_release(struct inode * inode, struct file * filp)
{
	int drive;
	
	drive = DRIVE(inode->i_rdev);

	if( !filp || (filp->f_mode & (2 | OPEN_WRITE_BIT)))
		/* if the file is mounted OR (writable now AND writable at
		 * open time) Linus: Does this cover all cases? */
		block_fsync(inode,filp);
			
	if (UDRS->fd_ref < 0)
		UDRS->fd_ref=0;
	else if (!UDRS->fd_ref--) {
		DPRINT("floppy_release with fd_ref == 0");
		UDRS->fd_ref = 0;
	}
	floppy_release_irq_and_dma();
}

/*
 * floppy_open check for aliasing (/dev/fd0 can be the same as
 * /dev/PS0 etc), and disallows simultaneous access to the same
 * drive with different device numbers.
 */
#define RETERR(x) \
	do{floppy_release(inode,filp); \
	   return -(x);}while(0)

static int floppy_open(struct inode * inode, struct file * filp)
{
	int drive;
	int old_dev;

	if (!filp) {
		DPRINT("Weird, open called with filp=0\n");
		return -EIO;
	}

	drive = DRIVE(inode->i_rdev);
	if (drive >= N_DRIVE || 
	    !( ALLOWED_DRIVE_MASK & ( 1 << drive)) ||
	    fdc_state[FDC(drive)].version == FDC_NONE)
		return -ENXIO;

	if (TYPE(inode->i_rdev) >= NUMBER(floppy_type))
		return -ENXIO;
	old_dev = UDRS->fd_device;
	if (UDRS->fd_ref && old_dev != inode->i_rdev)
		return -EBUSY;

	if(!UDRS->fd_ref && (UDP->flags & FD_BROKEN_DCL)){
		USETF(FD_DISK_CHANGED);
		USETF(FD_VERIFY);
	}

	if(UDRS->fd_ref == -1 ||
	   (UDRS->fd_ref && (filp->f_flags & O_EXCL)))
		return -EBUSY;

	if (floppy_grab_irq_and_dma())
		return -EBUSY;

	if (filp->f_flags & O_EXCL)
		UDRS->fd_ref = -1;
	else
		UDRS->fd_ref++;

	UDRS->fd_device = inode->i_rdev;

	if (old_dev && old_dev != inode->i_rdev) {
		if (buffer_drive == drive)
			buffer_track = -1;
		invalidate_buffers(old_dev);
	}

	/* Allow ioctls if we have write-permissions even if read-only open */
	if ((filp->f_mode & 2) || (permission(inode,2) == 0))
		filp->f_mode |= IOCTL_MODE_BIT;
	if (filp->f_mode & 2)
		filp->f_mode |= OPEN_WRITE_BIT;

	if (UFDCS->rawcmd == 1)
	       UFDCS->rawcmd = 2;

	if (filp->f_flags & O_NDELAY)
		return 0;
	if (filp->f_mode & 3) {
		UDRS->last_checked = 0;
		check_disk_change(inode->i_rdev);
		if (UTESTF(FD_DISK_CHANGED))
			RETERR(ENXIO);
	}
	if ((filp->f_mode & 2) && !(UTESTF(FD_DISK_WRITABLE)))
		RETERR(EROFS);
	return 0;
#undef RETERR
}

/*
 * Check if the disk has been changed or if a change has been faked.
 */
static int check_floppy_change(dev_t dev)
{
	int drive = DRIVE( dev );

	if (MAJOR(dev) != MAJOR_NR) {
		DPRINT("floppy_changed: not a floppy\n");
		return 0;
	}

	if (UTESTF(FD_DISK_CHANGED))
		return 1;

	if(UDRS->last_checked + UDP->checkfreq < jiffies){
		lock_fdc(drive,0);
		poll_drive(0,0);
		process_fd_request();
	}
		
	if(UTESTF(FD_DISK_CHANGED) ||
	   test_bit(drive, &fake_change) ||
	   (!TYPE(dev) && !current_type[drive]))
		return 1;
	return 0;
}

/* revalidate the floppy disk, i.e. trigger format autodetection by reading
 * the bootblock (block 0). "Autodetection" is also needed to check whether
 * there is a disk in the drive at all... Thus we also do it for fixed
 * geometry formats */
static int floppy_revalidate(dev_t dev)
{
#define NO_GEOM (!current_type[drive] && !TYPE(dev))
	struct buffer_head * bh;
	int drive=DRIVE(dev);
	int cf;

	if(UTESTF(FD_DISK_CHANGED) || test_bit(drive, &fake_change) || NO_GEOM){
		lock_fdc(drive,0);
		cf = UTESTF(FD_DISK_CHANGED);
		if(! (cf || test_bit(drive, &fake_change) || NO_GEOM)){
			process_fd_request(); /*already done by another thread*/
			return 0;
		}
		UDRS->maxblock = 0;
		UDRS->maxtrack = 0;
		if ( buffer_drive == drive)
			buffer_track = -1;
		clear_bit(drive, &fake_change);
		UCLEARF(FD_DISK_CHANGED);
		if(cf)
			UDRS->generation++;
		if(NO_GEOM){
			/* auto-sensing */
			int size = floppy_blocksizes[MINOR(dev)];
			if (!size)
				size = 1024;
			if (!(bh = getblk(dev,0,size))){
				process_fd_request();
				return 1;
			}
			if ( bh && ! bh->b_uptodate)
				ll_rw_block(READ, 1, &bh);
			process_fd_request();
			wait_on_buffer(bh);
			brelse(bh);
			return 0;
		} 
		if(cf)
				poll_drive(0, FD_RAW_NEED_DISK);
		process_fd_request();
	}
	return 0;
}

static struct file_operations floppy_fops = {
	NULL,			/* lseek - default */
	floppy_read,		/* read - general block-dev read */
	floppy_write,		/* write - general block-dev write */
	NULL,		       	/* readdir - bad */
	NULL,			/* select */
	fd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	floppy_open,		/* open */
	floppy_release,		/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	check_floppy_change,	/* media_change */
	floppy_revalidate,	/* revalidate */
};

/*
 * Floppy Driver initialisation
 * =============================
 */

/* Determine the floppy disk controller type */
/* This routine was written by David C. Niemi */
static char get_fdc_version(void)
{
	int r;

	output_byte(FD_DUMPREGS);	/* 82072 and better know DUMPREGS */
	if ( FDCS->reset )
		return FDC_NONE;
	if ( (r = result()) <= 0x00)
		return FDC_NONE;	/* No FDC present ??? */
	if ((r==1) && (reply_buffer[0] == 0x80)){
		printk("FDC %d is a 8272A\n",fdc);
		return FDC_8272A;		/* 8272a/765 don't know DUMPREGS */
	}
	if (r != 10) {
		printk("FDC init: DUMPREGS: unexpected return of %d bytes.\n", r);
		return FDC_UNKNOWN;
	}
	output_byte(FD_VERSION);
	r = result();
	if ((r == 1) && (reply_buffer[0] == 0x80)){
		printk("FDC %d is a 82072\n",fdc);
		return FDC_82072;		/* 82072 doesn't know VERSION */
	}
	if ((r != 1) || (reply_buffer[0] != 0x90)) {
		printk("FDC init: VERSION: unexpected return of %d bytes.\n", r);
		return FDC_UNKNOWN;
	}
	output_byte(FD_UNLOCK);
	r = result();
	if ((r == 1) && (reply_buffer[0] == 0x80)){
		printk("FDC %d is a pre-1991 82077\n", fdc);
		return FDC_82077_ORIG;	/* Pre-1991 82077 doesn't know LOCK/UNLOCK */
	}
	if ((r != 1) || (reply_buffer[0] != 0x00)) {
		printk("FDC init: UNLOCK: unexpected return of %d bytes.\n", r);
		return FDC_UNKNOWN;
	}
	printk("FDC %d is a post-1991 82077\n",fdc);
	return FDC_82077;	/* Revised 82077AA passes all the tests */
} /* get_fdc_version */

/* lilo configuration */

/* we make the invert_dcl function global. One day, somebody might
want to centralize all thinkpad related options into one lilo option,
there are just so many thinkpad related quirks! */
void floppy_invert_dcl(int *ints,int param)
{
	int i;
	
	for (i=0; i < ARRAY_SIZE(default_drive_params); i++){
		if (param)
			default_drive_params[i].params.flags |= 0x80;
		else
			default_drive_params[i].params.flags &= ~0x80;
	}
	DPRINT("Configuring drives for inverted dcl\n");
}

static void daring(int *ints,int param)
{
	int i;

	for (i=0; i < ARRAY_SIZE(default_drive_params); i++){
		if (param){
			default_drive_params[i].params.select_delay = 0;
			default_drive_params[i].params.flags |= FD_SILENT_DCL_CLEAR;
		} else {
			default_drive_params[i].params.select_delay = 2*HZ/100;
			default_drive_params[i].params.flags &= ~FD_SILENT_DCL_CLEAR;
		}
	}
	DPRINT1("Assuming %s floppy hardware\n", param ? "standard" : "broken");
}

static void allow_drives(int *ints, int param)
{
	ALLOWED_DRIVE_MASK=param;
	DPRINT1("setting allowed_drive_mask to 0x%x\n", param);
}

static void fdc2_adr(int *ints, int param)
{
	FDC2 = param;
	if(param)
		DPRINT1("enabling second fdc at address 0x%3x\n", FDC2);
	else
		DPRINT("disabling second fdc\n");
}

static void unex(int *ints,int param)
{
	print_unex = param;
	DPRINT1("%sprinting messages for unexpected interrupts\n",
		param ? "" : "not ");
}

static void set_cmos(int *ints, int dummy)
{
	int current_drive=0;

	if ( ints[0] != 2 ){
		DPRINT("wrong number of parameter for cmos\n");
		return;
	}
	current_drive = ints[1];
	if (current_drive < 0 || current_drive >= 8 ){
		DPRINT("bad drive for set_cmos\n");
		return;
	}
	if(ints[2] <= 0 || ints[2] >= NUMBER(default_drive_params)){
		DPRINT1("bad cmos code %d\n", ints[2]);
		return;
	}
	DP->cmos = ints[2];
	DPRINT1("setting cmos code to %d\n", ints[2]);
}
		
static struct param_table {
	char *name;
	void (*fn)(int *ints, int param);
	int def_param;
} config_params[]={
{ "allowed_drive_mask", allow_drives, 0xff },
{ "all_drives", allow_drives, 0xff },
{ "asus_pci", allow_drives, 0x33 },

{ "daring", daring, 1},

{ "two_fdc", fdc2_adr, 0x370 },
{ "one_fdc", fdc2_adr, 0 },

{ "thinkpad", floppy_invert_dcl, 1 },

{ "cmos", set_cmos, 0 },

{ "unexpected_interrupts", unex, 1 },
{ "no_unexpected_interrupts", unex, 0 },
{ "L40SX", unex, 0 } };

#define FLOPPY_SETUP
void floppy_setup(char *str, int *ints)
{
	int i;
	int param;
	if(!str)
		return;
	for(i=0; i< ARRAY_SIZE(config_params); i++){
		if (strcmp(str,config_params[i].name) == 0 ){
			if (ints[0] )
				param = ints[1];
			else
				param = config_params[i].def_param;
			config_params[i].fn(ints,param);
			return;
		}
	}
	DPRINT1("unknown floppy option %s\n", str);
	DPRINT("allowed options are:");
	for(i=0; i< ARRAY_SIZE(config_params); i++)
		printk(" %s",config_params[i].name);
	printk("\n");
	DPRINT("Read linux/drivers/block/README.fd\n");
}

#ifdef FD_MODULE
static
#endif
int new_floppy_init(void)
{
	int i,drive;
	int have_no_fdc=0;

	sti();

	if (register_blkdev(MAJOR_NR,"fd",&floppy_fops)) {
		printk("Unable to get major %d for floppy\n",MAJOR_NR);
		return -EBUSY;
	}

	for(i=0; i<256; i++)
		if ( TYPE(i))
			floppy_sizes[i] = floppy_type[TYPE(i)].size >> 1;
		else
			floppy_sizes[i] = MAX_DISK_SIZE;

	blk_size[MAJOR_NR] = floppy_sizes;
	blksize_size[MAJOR_NR] = floppy_blocksizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	del_timer(&fd_timeout);
	config_types();

	fdc_state[0].address = FDC1;
	fdc_state[0].dor = 0;
#if N_FDC > 1
	fdc_state[1].address = FDC2;
	fdc_state[1].dor = 0;
#endif

	for (i = 0 ; i < N_FDC ; i++) {
		fdc = i;
		FDCS->dtr = -1;
		FDCS->dor = 0x4;
		FDCS->reset = 0;
		FDCS->version = FDC_NONE;
	}

	if(floppy_grab_irq_and_dma()){
		unregister_blkdev(MAJOR_NR,"fd");
		return -EBUSY;
	}

	/* initialise drive state */
	for (drive = 0; drive < N_DRIVE ; drive++) {
		UDRS->flags = FD_VERIFY | FD_DISK_NEWCHANGE | FD_DISK_CHANGED;
		UDRS->generation = 0;
		UDRS->keep_data = 0;
		UDRS->fd_ref = 0;
		UDRS->fd_device = 0;
		UDRWE->write_errors = 0;
		UDRWE->first_error_sector = 0;
		UDRWE->first_error_generation = 0;
		UDRWE->last_error_sector = 0;
		UDRWE->last_error_generation = 0;
		UDRWE->badness = 0;
	}

	for (i = 0 ; i < N_FDC ; i++) {
		fdc = i;
		if (FDCS->address == -1 )
			continue;
		FDCS->rawcmd = 2;
		if(user_reset_fdc(-1,FD_RESET_IF_NEEDED,0)){
			FDCS->address = -1;
			continue;
		}
		/* Try to determine the floppy controller type */
		FDCS->version = get_fdc_version();
		if (FDCS->version == FDC_NONE){
			FDCS->address = -1;
			continue;
		}

		have_no_fdc = 0;
		/* Not all FDCs seem to be able to handle the version command
		 * properly, so force a reset for the standard FDC clones,
		 * to avoid interrupt garbage.
		 */
		FDCS->has_fifo = FDCS->version >= FDC_82077_ORIG;
		user_reset_fdc(-1,FD_RESET_ALWAYS,0);
	}
	fdc=0;
	current_drive = 0;
	floppy_release_irq_and_dma();
	initialising=0;
	if(have_no_fdc)
		unregister_blkdev(MAJOR_NR,"fd");
	return have_no_fdc;
}

/* stupid compatibility hack... */
void floppy_init(void)
{
	new_floppy_init();
}

static int floppy_grab_irq_and_dma(void)
{
	int i;
	cli();
	if (usage_count++){
		sti();
		return 0;
	}
	sti();
#ifdef FD_MODULE
	MOD_INC_USE_COUNT;
#endif
	for(i=0; i< N_FDC; i++){
		if(FDCS->address != -1){	
			fdc = i;
			reset_fdc_info(1);
			outb_p(FDCS->dor, FD_DOR);
		}
	}
	set_dor(0, ~0, 8);  /* avoid immediate interrupt */

	if (request_irq(FLOPPY_IRQ, floppy_interrupt, SA_INTERRUPT, "floppy")) {
		DPRINT1("Unable to grab IRQ%d for the floppy driver\n",
			FLOPPY_IRQ);
		return -1;
	}
	if (request_dma(FLOPPY_DMA,"floppy")) {
		DPRINT1("Unable to grab DMA%d for the floppy driver\n",
			FLOPPY_DMA);
		free_irq(FLOPPY_IRQ);
		return -1;
	}
	for(fdc = 0; fdc < N_FDC ; fdc++)
		if(FDCS->address != -1)
			outb_p(FDCS->dor, FD_DOR);
	fdc = 0;
	enable_irq(FLOPPY_IRQ);
	return 0;
}

static void floppy_release_irq_and_dma(void)
{
#ifdef CONFIG_FLOPPY_SANITY
	int drive;
#endif
	cli();
	if (--usage_count){
		sti();
		return;
	}
	sti();
#ifdef FD_MODULE
	MOD_DEC_USE_COUNT;
#endif
	disable_dma(FLOPPY_DMA);
	free_dma(FLOPPY_DMA);
	disable_irq(FLOPPY_IRQ);
	free_irq(FLOPPY_IRQ);

	set_dor(0, ~0, 8);
#if N_FDC > 1
	set_dor(1, ~8, 0);
#endif
	floppy_enable_hlt();
#ifdef CONFIG_FLOPPY_SANITY
	for(drive=0; drive < N_FDC * 4; drive++)
		if( motor_off_timer[drive].next )
			printk("motor off timer %d still active\n", drive);
	
	if(fd_timeout.next)
		printk("floppy timer still active\n");
	if (fd_timer.next)
		printk("auxiliary floppy timer still active\n");
	if(floppy_tq.sync)
		printk("task queue still active\n");
#endif
}

