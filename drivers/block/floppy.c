/*
 *  linux/kernel/floppy.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1993, 1994  Alain Knaff
 */

/* Configuration */
/* The following does some extra sanity checks */
#define SANITY

/* Undefine the following if you have to floppy disk controllers:
 * This works at least for me; if you get two controllers working, with
 * drives attached to both, please mail me: Alain.Knaff@imag.fr */
/* #define HAVE_2_CONTROLLERS */

/* Undefine the following if you have problems accessing ED disks, but don't
 * have problems accessing them with the stock driver. If that is the case,
 * please mail me: Alain.Knaff@imag.fr */
/* #define FDC_FIFO_BUG */

/* End of configuration */

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


#define REALLY_SLOW_IO
#define FLOPPY_IRQ 6
#define FLOPPY_DMA 2

#define DEBUGT 2

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#define FDPATCHES
#include <linux/fdreg.h>
#include <linux/fd.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/delay.h>

#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR FLOPPY_MAJOR
#include "blk.h"

static unsigned int changed_floppies = 0, fake_change = 0;
static int initialising=1;


#ifdef HAVE_2_CONTROLLERS
#define N_FDC 2
#define N_DRIVE 8
#else
#define N_FDC 1
#define N_DRIVE 4
#endif

#define TYPE(x) ( ((x)>>2) & 0x1f )
#define DRIVE(x) ( ((x)&0x03) | (((x)&0x80 ) >> 5))
#define UNIT(x) ( (x) & 0x03 )		/* drive on fdc */
#define FDC(x) ( ((x) & 0x04) >> 2 )  /* fdc of drive */
#define REVDRIVE(fdc, unit) ( (unit) + ((fdc) << 2 ))
				/* reverse mapping from unit and fdc to drive */
#define DP (&drive_params[current_drive])
#define DRS (&drive_state[current_drive])
#define FDCS (&fdc_state[fdc])

#define UDP (&drive_params[drive])
#define UDRS (&drive_state[drive])
#define UFDCS (&fdc_state[FDC(drive)])

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
 */
#define MAX_DISK_SIZE 3984



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
  |     |   |   |     |    Time needed for spinup time (jiffies)
  |     |   |   |     |    |    Timeout for spinning down (jiffies)
  |     |   |   |     |    |    |   Spindown offset (where disk stops)
  |     |   |   |     |    |    |   |  Select delay
  |     |   |   |     |    |    |   |  |  RPS
  |     |   |   |     |    |    |   |  |  |    Max number of tracks
  |     |   |   |     |    |    |   |  |  |    |     Interrupt timeout
  |     |   |   |     |    |    |   |  |  |    |     |   Max nonintlv. sectors
  |     |   |   |     |    |    |   |  |  |    |     |   | -Max Errors- flags */
{{0,  500, 16, 16, 8000, 100, 300,  0, 2, 5,  80, 3*HZ, 20, {3,1,2,0,2}, 0,
      0, { 7, 4, 8, 2, 1, 5, 3,10}, 150, 0 }, "unknown" },

{{1,  300, 16, 16, 8000, 100, 300,  0, 2, 5,  40, 3*HZ, 17, {3,1,2,0,2}, 0,
      0, { 1, 0, 0, 0, 0, 0, 0, 0}, 150, 1 }, "360K PC" }, /*5 1/4 360 KB PC*/

{{2,  500, 16, 16, 6000,  40, 300, 14, 2, 6,  83, 3*HZ, 17, {3,1,2,0,2}, 0,
      0, { 2, 5, 6,23,10,20,11, 0}, 150, 2 }, "1.2M" }, /*5 1/4 HD AT*/

{{3,  250, 16, 16, 3000, 100, 300,  0, 2, 5,  83, 3*HZ, 20, {3,1,2,0,2}, 0,
      0, { 4,22,21,30, 3, 0, 0, 0}, 150, 4 }, "720k" }, /*3 1/2 DD*/

{{4,  500, 16, 16, 4000,  40, 300, 10, 2, 5,  83, 3*HZ, 20, {3,1,2,0,2}, 0,
      0, { 7, 4,25,22,31,21,29,11}, 150, 7 }, "1.44M" }, /*3 1/2 HD*/

{{5, 1000, 15,  8, 3000,  40, 300, 10, 2, 5,  83, 3*HZ, 40, {3,1,2,0,2}, 0,
      0, { 7, 8, 4,25,28,22,31,21}, 150, 8 }, "2.88M AMI BIOS" }, /*3 1/2 ED*/

{{6, 1000, 15,  8, 3000,  40, 300, 10, 2, 5,  83, 3*HZ, 40, {3,1,2,0,2}, 0,
      0, { 7, 8, 4,25,28,22,31,21}, 150, 8 }, "2.88M" } /*3 1/2 ED*/
/*    |  ---autodetected formats--   |   |      |
      read_track                     |   |    Name printed when booting
                                     |  Native format
                                   Frequency of disk change checks */
};

static struct floppy_drive_params drive_params[N_DRIVE];
static struct floppy_drive_struct volatile drive_state[N_DRIVE];
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
	{ 3360,21,2,80,0,0x1C,0x00,0xCF,0x6C,"H1680" }, /* 11 1.68MB 3.5"   */
	{  820,10,2,41,1,0x25,0x01,0xDF,0x2E,"h410"  },	/* 12 410KB 5.25"   */
	{ 1640,10,2,82,0,0x25,0x02,0xDF,0x2E,"H820"  },	/* 13 820KB 3.5"    */
	{ 2952,18,2,82,0,0x25,0x00,0xDF,0x02,"h1476" },	/* 14 1.48MB 5.25"  */
	{ 3444,21,2,82,0,0x25,0x00,0xDF,0x0C,"H1722" },	/* 15 1.72MB 3.5"   */
	{  840,10,2,42,1,0x25,0x01,0xDF,0x2E,"h420"  },	/* 16 420KB 5.25"   */
	{ 1660,10,2,83,0,0x25,0x02,0xDF,0x2E,"H830"  },	/* 17 830KB 3.5"    */
	{ 2988,18,2,83,0,0x25,0x00,0xDF,0x02,"h1494" },	/* 18 1.49MB 5.25"  */
	{ 3486,21,2,83,0,0x25,0x00,0xDF,0x0C,"H1743" }, /* 19 1.74 MB 3.5"  */

	{ 1760,11,2,80,0,0x1C,0x09,0xCF,0x6C,"d880"  }, /* 20 880KB 5.25"   */
	{ 2080,13,2,80,0,0x1C,0x0A,0xCF,0x6C,"D1040" }, /* 21 1.04MB 3.5"   */
	{ 2240,14,2,80,0,0x1C,0x1A,0xCF,0x6C,"D1120" }, /* 22 1.12MB 3.5"   */
	{ 3200,20,2,80,0,0x1C,0x20,0xCF,0x6C,"h1600" }, /* 23 1.6MB 5.25"   */
	{ 3520,22,2,80,0,0x1C,0x08,0xCF,0x6C,"H1760" }, /* 24 1.76MB 3.5"   */
	{ 3840,24,2,80,0,0x1C,0x18,0xCF,0x6C,"H1920" }, /* 25 1.92MB 3.5"   */
	{ 6400,40,2,80,0,0x25,0x5B,0xCF,0x6C,"E3200" }, /* 26 3.20MB 3.5"   */
	{ 7040,44,2,80,0,0x25,0x5B,0xCF,0x6C,"E3520" }, /* 27 3.52MB 3.5"   */
	{ 7680,48,2,80,0,0x25,0x63,0xCF,0x6C,"E3840" }, /* 28 3.84MB 3.5"   */

	{ 3680,23,2,80,0,0x1C,0x10,0xCF,0x6C,"H1840" }, /* 29 1.84MB 3.5"   */
	{ 1600,10,2,80,0,0x25,0x02,0xDF,0x2E,"H800"  },	/* 30 800KB 3.5"    */
	{ 3200,20,2,80,0,0x1C,0x00,0xCF,0x6C,"H1600" }, /* 31 1.6MB 3.5"    */
};

#define	NUMBER(x)	(sizeof(x) / sizeof(*(x)))
#define SECTSIZE ( _FD_SECTSIZE(*floppy))

/* Auto-detection: Disk type used until the next media change occurs. */
struct floppy_struct *current_type[N_DRIVE] = { NULL, NULL, NULL, NULL
#ifdef HAVE_2_CONTROLLERS
				          ,NULL, NULL, NULL, NULL
#endif
					};

/*
 * User-provided type information. current_type points to
 * the respective entry of this array.
 */
struct floppy_struct user_params[N_DRIVE];

static int floppy_sizes[256];

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
static void recalibrate_floppy(void);
static void seek_floppy(void);
static void floppy_shutdown(void);

int floppy_grab_irq_and_dma(void);
void floppy_release_irq_and_dma(void);

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

/* buffer related variables */
static int buffer_track = -1;
static int buffer_drive = -1;
static int buffer_min = -1;
static int buffer_max = -1;

#ifdef FDC_FIFO_BUG
static int force=0;
#endif

/* fdc related variables, should end up in a struct */
static struct floppy_fdc_state fdc_state[N_FDC];
int fdc; /* current fdc */

static struct floppy_struct * floppy = floppy_type;
static unsigned char current_drive = 255;
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
static int set_dor(int fdc, char mask, char data)
{
	register unsigned char drive, unit, newdor,olddor;

	cli();
	olddor = FDCS->dor;
	newdor =  (olddor & mask) | data;
	if ( newdor != olddor ){
		unit = olddor & 0x3;
		drive = REVDRIVE(fdc,unit);
		if ( olddor & ( 0x10 << unit )){
			if ( inb_p( FD_DIR ) & 0x80 )
				UDRS->flags |= FD_VERIFY;
			else
				UDRS->last_checked=jiffies;
		}
		FDCS->dor = newdor;
		outb_p( newdor, FD_DOR);
	}
	sti();
	return olddor;
}

static void twaddle(void)
{
	cli();
	outb_p(FDCS->dor & ~(0x10<<UNIT(current_drive)),FD_DOR);
	outb_p(FDCS->dor, FD_DOR);
	sti();
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

/* selects the fdc and drive, and enables the fdc's its input/dma. */
static void set_fdc(int drive)
{
	if ( drive >= 0 ){
		fdc = FDC(drive);
		current_drive = drive;
	}
	set_dor(fdc,~0,8);
#ifdef HAVE_2_CONTROLLERS
	set_dor(1-fdc, ~8, 0);
#endif
	if ( FDCS->rawcmd == 2 )
		reset_fdc_info(1);
	if( inb_p(FD_STATUS) != STATUS_READY )
		FDCS->reset = 1;
}

/* locks the driver */
static void lock_fdc(int drive)
{
	cli();
	while (fdc_busy) sleep_on(&fdc_wait);
	fdc_busy = 1;
	sti();
	command_status = FD_COMMAND_NONE;
	set_fdc(drive);
	if ( drive >= 0 ){
		timer_table[FLOPPY_TIMER].expires = jiffies + DP->timeout;
		timer_active |= 1 << FLOPPY_TIMER;
	}
}

/* unlocks the driver */
static inline int unlock_fdc(void)
{
	if (current_drive < N_DRIVE)
		floppy_off(current_drive);
	if (!fdc_busy)
		printk(DEVICE_NAME ": FDC access conflict!\n");

	if ( DEVICE_INTR )
		printk(DEVICE_NAME
		       ":device interrupt still active at FDC release: %p!\n",
		       DEVICE_INTR);
	command_status = FD_COMMAND_NONE;
	timer_active &= ~(1 << FLOPPY_TIMER);
	fdc_busy = 0;
	wake_up(&fdc_wait);
	return 0;
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
	{ NULL, NULL, 0, 3, motor_off_callback }
#ifdef HAVE_2_CONTROLLERS
	,{ NULL, NULL, 0, 4, motor_off_callback },
	{ NULL, NULL, 0, 5, motor_off_callback },
	{ NULL, NULL, 0, 6, motor_off_callback },
	{ NULL, NULL, 0, 7, motor_off_callback }
#endif
};

/* schedules motor off */
static void floppy_off(unsigned int nr)
{
	unsigned long volatile delta;

	cli();
	del_timer(motor_off_timer+nr);

	/* make spindle stop in a position which minimizes spinup time
	 * next time */
	if ( drive_params[nr].rps ){
		delta = jiffies - drive_state[nr].first_read_date + HZ -
			drive_params[nr].spindown_offset;
		delta = (( delta * drive_params[nr].rps) % HZ ) /
			drive_params[nr].rps;
		motor_off_timer[nr].expires = drive_params[nr].spindown - delta;
	}
	add_timer(motor_off_timer+nr);
	sti();
}

/*
 * cycle through all N_DRIVE floppy drives, for disk change testing.
 * stopping at current drive. This is done before any long operation, to
 * be sure to have up to date disk change information.
 */
static void scandrives(void)
{
	int i, drive, saved_drive;

	saved_drive = current_drive % N_DRIVE;
	for(i=0; i< N_DRIVE; i++){
		drive = (saved_drive + i + 1 ) % N_DRIVE;
		if ( UDRS->fd_ref == 0 )
			continue; /* skip closed drives */
		set_fdc(drive);
		UDRS->select_date = jiffies;
		if(! (set_dor( fdc, ~3, UNIT(drive) | ( 0x10 << UNIT(drive))) &
		      (0x10 << UNIT(drive))))
			/* switch the motor off again, if it was off to
			 * begin with */
			set_dor( fdc, ~( 0x10 << UNIT(drive) ), 0 );
	}
	current_drive = saved_drive;
}

typedef void (*timeout_fn)(unsigned long);
static struct timer_list fd_timer ={ NULL, NULL, 0, 0, 0 };

/* this function makes sure that the disk stays in the drive during the
 * transfer */
static void fd_watchdog(void)
{
	if ( inb_p( FD_DIR ) & 0x80 ){
		floppy_shutdown();
	} else {
		cli();
		del_timer(&fd_timer);
		fd_timer.function = (timeout_fn) fd_watchdog;
		fd_timer.expires = 10;
		add_timer(&fd_timer);
		sti();
	}
}

static void main_command_interrupt(void)
{
	del_timer(&fd_timer);
	cont->interrupt();
}

/* waits for a delay (spinup or select) to pass */
static int wait_for_completion(int nr, int delay, timeout_fn function)
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

static void setup_DMA(void)
{
#ifdef SANITY
	if ((!CURRENT ||
	     CURRENT->buffer != current_addr ||
	     raw_cmd.length > 512 * CURRENT->nr_sectors) &&
	    (current_addr < floppy_track_buffer ||
	     current_addr + raw_cmd.length >
	     floppy_track_buffer + 1024 * MAX_BUFFER_SECTORS)){
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
}

/* sends a command byte to the fdc */
static int output_byte(char byte)
{
	int counter;
	unsigned char status;

	if (FDCS->reset)
		return -1;
	for(counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS) &(STATUS_READY|STATUS_DIR|STATUS_DMA);
		if (!(status & STATUS_READY))
			continue;
		if (status == STATUS_READY
#ifdef FDC_FIFO_BUG
		    || ((status == STATUS_READY|STATUS_DIR|STATUS_BUSY) &&force)
#endif
		    )
		{
			outb_p(byte,FD_DATA);
			return 0;
		} else
			break;
	}
	FDCS->reset = 1;
	if ( !initialising )
		printk(DEVICE_NAME ": Unable to send byte to FDC %d (%x)\n",
		       fdc, status);
	return -1;
}
#define LAST_OUT(x) if(output_byte(x)){ reset_fdc();return;}

#ifdef FDC_FIFO_BUG
#define output_byte_force(x) force=1;output_byte(x);force=0;
#else
#define output_byte_force(x) output_byte(x);
#endif

/* gets the response from the fdc */
static int result(void)
{
	int i = 0, counter, status;

	if (FDCS->reset)
		return -1;
	for (counter = 0 ; counter < 10000 ; counter++) {
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
				printk(DEVICE_NAME
				       ": floppy_stat reply overrun\n");
				break;
			}
			reply_buffer[i++] = inb_p(FD_DATA);
		}
	}
	FDCS->reset = 1;
	if ( !initialising )
		printk(DEVICE_NAME ": Getstatus times out (%x) on fdc %d [%d]\n",
		       status, fdc,i);
	return -1;
}

/* Set perpendicular mode as required, based on data rate, if supported.
 * 82077 Untested! 1Mbps data rate only possible with 82077-1.
 * TODO: increase MAX_BUFFER_SECTORS, add floppy_type entries.
 */
static inline void perpendicular_mode(void)
{
	unsigned char perp_mode;

	if (!floppy)
		return;
	if (floppy->rate & 0x40){
		switch(raw_cmd.rate){
		case 0:
			perp_mode=/*2*/3;
			break;
		case 3:
			perp_mode=3;
			break;
		default:
			printk(DEVICE_NAME
			       ": Invalid data rate for perpendicular mode!\n");
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
		output_byte_force(perp_mode);
		FDCS->perp_mode = perp_mode;
	} else if (perp_mode) {
		printk(DEVICE_NAME
		       ": perpendicular mode not supported by this FDC.\n");
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
		output_byte_force(0);
		output_byte(0x1A);	/* FIFO on, polling off, 10 byte threshold */
		output_byte_force(0);		/* precompensation from track 0 upwards */
		if ( FDCS->reset ){
			FDCS->has_fifo=0;
			return;
		}
		FDCS->need_configure = 0;
		/*printk(DEVICE_NAME ": FIFO enabled\n");*/
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
#define fd_disable_dma 0
	spec2 = (hlt << 1) | fd_disable_dma;

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
static void fdc_dtr(void)
{
	/* If data rate not already set to desired value, set it. */
	if ( raw_cmd.rate == FDCS->dtr)
		return;
	
	/* Set dtr */
	outb_p(raw_cmd.rate, FD_DCR);
	
	/* TODO: some FDC/drive combinations (C&T 82C711 with TEAC 1.2MB)
	 * need a stabilization period of several milliseconds to be
	 * enforced after data rate changes before R/W operations.
	 * Pause 5 msec to avoid trouble.
	 */
	udelay(5000);
	FDCS->dtr = raw_cmd.rate;
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
		printk(DEVICE_NAME ": -- FDC reply error");
		FDCS->reset = 1;
		return 1;
	}

	/* check IC to find cause of interrupt */
	switch ((ST0 & ST0_INTR)>>6) {
		case 1:	/* error occured during command execution */
			bad = 1;
			if (ST1 & ST1_WP) {
				printk(DEVICE_NAME ": Drive %d is write protected\n", current_drive);
				DRS->flags &= ~FD_DISK_WRITABLE;
				cont->done(0);
				bad = 2;
			} else if (ST1 & ST1_ND) {
				DRS->flags |= FD_NEED_TWADDLE;
			} else if (ST1 & ST1_OR) {
				if (DP->flags & FTD_MSG )
					printk(DEVICE_NAME ": Over/Underrun - retrying\n");
				/* could continue from where we stopped, but ... */
				bad = 0;
			}else if(*errors >= DP->max_errors.reporting){
				printk(DEVICE_NAME " %d: ", ST0 & ST0_DS);
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
					printk("unknown error. ST[0..3] are: 0x%x 0x%x 0x%x 0x%x\n", ST0, ST1, ST2, ST3);
				}
				printk("\n");

			}
			if ( ST2 & ST2_WC || ST2 & ST2_BC)
				/* wrong cylinder => recal */
				DRS->track = NEED_2_RECAL;
			return bad;
		case 2: /* invalid command given */
			printk(DEVICE_NAME ": Invalid FDC command given!\n");
			cont->done(0);
			return 2;
		case 3:
			printk(DEVICE_NAME ": Abnormal termination caused by polling\n");
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

	if (flags & FD_RAW_SPIN){
		ready_date = DRS->spinup_date + DP->spinup;		
		/* If spinup will take a long time, rerun scandrives
		 * again just before spinup completion. Beware that
		 * after scandrives, we must again wait for selection.
		 */
		if ( ready_date > jiffies + DP->select_delay){
			ready_date -= DP->select_delay;
			function = (timeout_fn) floppy_on;
		} else
			function = (timeout_fn) setup_rw_floppy;

		/* wait until the floppy is spinning fast enough */
		if (wait_for_completion(current_drive,ready_date,function))
			return;
	}
	dflags = DRS->flags;

	if ( (flags & FD_RAW_READ) || (flags & FD_RAW_WRITE)){
		if ( flags & FD_RAW_USER_SUPPLIED )
			buffer_track = -1;
		setup_DMA();
	}
	
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
		printk(DEVICE_NAME ": seek failed\n");
		DRS->track = NEED_2_RECAL;
		cont->error();
		cont->redo();
		return;
	}
	if ( DRS->track >= 0 && DRS->track != ST1 )
		DRS->flags &= ~FD_DISK_NEWCHANGE;
	DRS->track = ST1;
	seek_floppy();
}

static void seek_floppy(void)
{
	int track;

	if ((raw_cmd.flags & FD_RAW_NEED_DISK) &&
	    !(DRS->flags & FD_DISK_NEWCHANGE ) &&
	    (inb_p(FD_DIR) & 0x80)){
		/* the media changed flag should be cleared after the seek.
		 * If it isn't, this means that there is really no disk in
		 * the drive.
		 */
		cont->done(0);
		cont->redo();
		return;
	}
	if ( DRS->track <= NEED_1_RECAL ){
		recalibrate_floppy();
		return;
	} else if ((DRS->flags & FD_DISK_NEWCHANGE) &&
		   (DRS->track <= NO_TRACK || DRS->track == raw_cmd.track)) {
		/* we seek to clear the media-changed condition. Does anybody
		 * know a more elegant way, which works on all drives? */
		if ( raw_cmd.track )
			track = raw_cmd.track - 1;
		else
			track = 1;
	} else if (raw_cmd.track != DRS->track)
		track = raw_cmd.track;
	else {
		setup_rw_floppy();
		return;
	}

	if ( !track && DRS->track >= 0 && DRS->track < 80 ){
		DRS->flags &= ~FD_DISK_NEWCHANGE;
		/* if we go to track 0 anyways, we can just as well use
		 * recalibrate */
		recalibrate_floppy();
	} else {
		SET_INTR(seek_interrupt);
		output_byte(FD_SEEK);
		output_byte(UNIT(current_drive));
		LAST_OUT(track);
#ifdef DEBUGT
		debugt("seek command:");
#endif
	}
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
			/* after a second recalibrate, we still haven't
			 * reached track 0. Probably no drive */
			cont->error();
			cont->redo();
			return;
		case NEED_2_RECAL:
			/* If we already did a recalibrate, and we are not at
			 * track 0, this means we have moved. (The only way
			 * not to move at recalibration is to be already at
			 * track 0.) Clear the new change flag
			 */
			DRS->flags &= ~FD_DISK_NEWCHANGE;
			/* fall through */
		default:
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
	seek_floppy();
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
	printk(DEVICE_NAME ": unexpected interrupt\n");
	if ( inr >= 0 )
		for(i=0; i<inr; i++)
			printk("%d %x\n", i, reply_buffer[i] );
	while(1){
		output_byte(FD_SENSEI);
		inr=result();
		if ( inr != 2 )
			break;
		printk("sensei\n");
		for(i=0; i<inr; i++)
			printk("%d %x\n", i, reply_buffer[i] );
	}
	FDCS->reset = 1;
}

/* interrupt handler */
static void floppy_interrupt(int unused)
{
	void (*handler)(void) = DEVICE_INTR;

	CLEAR_INTR;
	if ( fdc >= N_FDC ) /* we don't even know which FDC is the culprit */
		return;
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
	sti(); /* this should help improve interrupt latency. */
	handler();
}

static void recalibrate_floppy(void)
{
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
		cli();
		outb_p(FDCS->dor & ~0x04, FD_DOR);
		udelay(FD_RESET_DELAY);
		outb(FDCS->dor, FD_DOR);
		sti();
	}
}

static void floppy_shutdown(void)
{
	cli();
	if ( !DEVICE_INTR ){ /* no interrupt pending. Probably has just been
			      * served */
		sti();
		return;
	}
	CLEAR_INTR;
	sti();
	if ( !initialising )
		printk(DEVICE_NAME ": timeout\n");
	FDCS->reset = 1;
	cont->done(0);
	cont->redo(); /* this will recall reset when needed */
}

/* start motor, check media-changed condition and write protection */
static int start_motor(void)
{
	int cnt;
	int dir;

	if ( (FDCS->dor & 0x03) != UNIT(current_drive) )
		/* notes select time if floppy is not yet selected */
		DRS->select_date = jiffies;

	if ( ! ( FDCS->dor & ( 0x10 << UNIT(current_drive) ) )){
		set_debugt();
		/* no read since this drive is running */
		DRS->first_read_date = 0;
		/* note motor start time if motor is not yet running */
		DRS->spinup_date = jiffies;
	}

	/* starts motor and selects floppy */
	del_timer(motor_off_timer + current_drive);
	set_dor( fdc, 0xfc,
		( 0x10 << UNIT(current_drive) ) | UNIT(current_drive) );

	dir = inb_p( FD_DIR) & 0x80;
	if ( ! (dir & 0x80) )
		DRS->last_checked =jiffies;
	if ( dir || ( DRS->flags & FD_VERIFY )) {
		DRS->flags &= FD_DRIVE_PRESENT | FD_DISK_NEWCHANGE;
		DRS->flags |= FD_DISK_WRITABLE;

		/* check write protection */
		output_byte( FD_GETSTATUS );
		output_byte( UNIT(current_drive) );
		if ( (cnt=result()) != 1 ){
			changed_floppies |= 1 << current_drive;
			FDCS->reset = 1;
			DRS->flags |= FD_VERIFY;
			return -1;
		}
		if ( ( ST3  & 0x60 ) == 0x60 )
			DRS->flags &= ~FD_DISK_WRITABLE;

		if ( ! ( DRS->flags & FD_DISK_NEWCHANGE) ){
			/* the following code is only executed the first time
			 * a particular disk change has been detected */
			changed_floppies |= 1 << current_drive;
			if (DRS->keep_data >= 0) {
				if ((DP->flags & FTD_MSG) &&
				    current_type[current_drive] != NULL)
					printk(DEVICE_NAME
					       ": Disk type is undefined after "
					       "disk change in fd%d\n",
					       current_drive);
				current_type[current_drive] = NULL;
				floppy_sizes[current_drive] = MAX_DISK_SIZE;
			}
			if ( ST3 & 0x10 )
				DRS->track = 0;
		}
	}
	if ( dir ) /* check if media changed is still on */
		DRS->flags |= FD_DISK_NEWCHANGE;
	else {
		DRS->flags &= ~FD_DISK_NEWCHANGE;
		DRS->last_checked =jiffies;
	}

	return DRS->flags;
}

static void floppy_ready(void)
{
	CHECK_RESET;
	start_motor();

	/* wait_for_completion also schedules reset if needed. */
	if(wait_for_completion(current_drive,
			       DRS->select_date+DP->select_delay,
			       (timeout_fn) floppy_ready))
		return;
	fdc_dtr();
	if ( raw_cmd.flags & FD_RAW_NEED_SEEK ){
		perpendicular_mode();
		fdc_specify(); /* must be done here because of hut, hlt ... */
		seek_floppy();
	} else
		setup_rw_floppy();
}

static void floppy_on(unsigned int drive)
{
	timer_table[FLOPPY_TIMER].expires = jiffies + DP->timeout;
	timer_active |= 1 << FLOPPY_TIMER;
	scandrives();
	floppy_ready();
}

/*
 * ========================================================================
 * here ends the bottom half. Exported routines are:
  * floppy_on, floppy_off, floppy_ready, lock_fdc, unlock_fdc, set_fdc,
 * start_motor, reset_fdc, reset_fdc_info, interpret_errors.
 * Initialisation also uses output_byte, result, set_dor, floppy_interrupt
 * and set_dor.
 * ========================================================================
 */
/*
 * General purpose continuations.
 * ==============================
 */
static void empty(void)
{
}

static void do_wakeup(void)
{
	timer_active &= ~(1 << FLOPPY_TIMER);
	command_status += 2;
	wake_up(&command_done);
	cont = 0;
}

static struct cont_t wakeup_cont={
	empty,
	do_wakeup,
	empty,
	(done_f)empty
};

static int wait_til_done(void)
{
	int ret;
	while(command_status < 2)
		if (current->pid)
			sleep_on( & command_done );
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
	while(1){
		probed_format = DRS->probed_format;
		if ( probed_format > N_DRIVE ||
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

static void setup_format_params(void)
{
	struct fparm {
		unsigned char track,head,sect,size;
	} *here = (struct fparm *)floppy_track_buffer;
	int ssize,il,n;
	int count,head_shift,track_shift;

	raw_cmd.flags = FD_RAW_WRITE | FD_RAW_INTR | FD_RAW_SPIN |
		FD_RAW_NEED_DISK | FD_RAW_NEED_SEEK;
	raw_cmd.rate = floppy->rate & 0x3;
	raw_cmd.cmd_count = NR_F;
	COMMAND = FD_FORMAT;
	DR_SELECT = UNIT(current_drive) + ( format_req.head << 2 );
	F_SIZECODE = FD_SIZECODE(floppy);
	ssize = 1 << ( F_SIZECODE - 2 );
	F_SECT_PER_TRACK = floppy->sect / ssize;
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
	if (floppy->sect > DP->interleave_sect && ssize==1)
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
	floppy_on(current_drive);
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
	int okay;

	lock_fdc(DRIVE(device));
	set_floppy(device);
	if (!floppy ||
	    tmp_format_req->track >= floppy->track ||
	    tmp_format_req->head >= floppy->head){
		unlock_fdc();
		return -EINVAL;
	}
	format_req = *tmp_format_req;
	format_errors = 0;
	cont = &format_cont;
	errors = &format_errors;
	redo_format();
	okay=wait_til_done();
	unlock_fdc();
	return okay;
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
	timer_active &= ~(1 << FLOPPY_TIMER);

	if (!CURRENT){
		printk(DEVICE_NAME
		       ": request list destroyed in floppy request done\n");
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
			printk(DEVICE_NAME "request list destroyed in floppy request done\n");

	} else {
#ifdef NO_WEIRD_UNLOCKED
		if ( CURRENT->bh )
			/* avoid those pesky "Weird unlocked ... errors" */
			CURRENT->bh->b_req = 0;
#endif
		end_request(0);
	}
}

/* Interrupt handler evaluating the result of the r/w operation */
static void rw_interrupt(void)
{
#if 0
	int i;
#endif
	int nr_sectors, ssize;
	char bad;

	if ( ! DRS->first_read_date )
		DRS->first_read_date = jiffies;

	nr_sectors = 0;
	ssize = 1 << (SIZECODE - 2);
	nr_sectors = ((R_TRACK-TRACK)*floppy->head+R_HEAD-HEAD) *
		floppy->sect + (R_SECTOR-SECTOR) * ssize -
			(sector_t % floppy->sect) % ssize;

#ifdef SANITY
	if ( nr_sectors > current_count_sectors + ssize -
	    (current_count_sectors + sector_t) % ssize +
	    sector_t % ssize){
		printk(DEVICE_NAME ": long rw: %x instead of %lx\n",
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
	if ( nr_sectors < current_count_sectors ){
#if 0
		printk(DEVICE_NAME ": short read got %d instead of %ld\n",
		       nr_sectors, current_count_sectors);
#endif
#if 0
		printk("command: ");
		for(i=0; i<raw_cmd.cmd_count; i++)
			printk("%x ", raw_cmd.cmd[i]);
		printk("rate=%x\n", raw_cmd.rate);
		printk("reply: ");
		for(i=0; i< inr; i++)
			printk("%x ", reply_buffer[i]);
		printk("\n");
#endif
		current_count_sectors = nr_sectors;
	}

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
			int i;
			printk(DEVICE_NAME ": dma problem?\n");
			for(i=0; i< inr ; i++)
				printk("%2x,", reply_buffer[i]);
			printk("\n");
			bad=1;
			cont->error();
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
			printk(DEVICE_NAME
			       ": Auto-detected floppy type %s in fd%d\n",
			       floppy->name,current_drive);
		current_type[current_drive] = floppy;
		floppy_sizes[DRIVE(current_drive) + (FDC(current_drive) << 7)] =
			floppy->size >> 1;
		probing = 0;
	}

	if ( COMMAND != FD_READ || current_addr == CURRENT->buffer ){
		/* transfer directly from buffer */
		cont->done(1);
	} else if ( COMMAND == FD_READ){
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

#ifdef SANITY
	if ( !bh ){
		printk(DEVICE_NAME ": null request in buffer_chain_size\n");
		return size >> 9;
	}
#endif

	bh = bh->b_reqnext;
	while ( bh && bh->b_data == base + size ){
		size += bh->b_size;
		bh = bh->b_reqnext;
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

	if (current_count_sectors <= 0 && COMMAND == FD_WRITE &&
	    buffer_max > sector_t + CURRENT->nr_sectors){
		current_count_sectors = buffer_max - sector_t;
		if ( current_count_sectors > CURRENT->nr_sectors )
			current_count_sectors = CURRENT->nr_sectors;
	}
	remaining = current_count_sectors << 9;
#ifdef SANITY
	if ((remaining >> 9) > CURRENT->nr_sectors  && COMMAND == 0xc5 ){
		printk(DEVICE_NAME ": in copy buffer\n");
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
#ifdef SANITY
		if (!bh){
			printk(DEVICE_NAME
			       ": bh=null in copy buffer before copy\n");
			break;
		}
		if (dma_buffer + size >
		    floppy_track_buffer + ( 2 * MAX_BUFFER_SECTORS << 9 ) ||
		    dma_buffer < floppy_track_buffer ){
			printk(DEVICE_NAME
			       ": buffer overrun in copy buffer %d\n",
			       (floppy_track_buffer - dma_buffer) >>9);
			printk("sector_t=%d buffer_min=%d\n",
			       sector_t, buffer_min);
			printk("current_count_sectors=%ld\n",
			       current_count_sectors);
			if ( COMMAND == FD_READ )
				printk("read\n");
			if ( COMMAND == FD_READ )
				printk("write\n");
			break;
		}
		if ( ((int)buffer) % 512 )
			printk(DEVICE_NAME ": %p buffer not aligned\n", buffer);
#endif
		if ( COMMAND == FD_READ )
			memcpy( buffer, dma_buffer, size);
		else
			memcpy( dma_buffer, buffer, size);
		remaining -= size;
		if ( !remaining)
			break;

		dma_buffer += size;
		bh = bh->b_reqnext;
#ifdef SANITY
		if ( !bh){
			printk(DEVICE_NAME
			       ": bh=null in copy buffer after copy\n");
			break;
		}
#endif
		size = bh->b_size;
		buffer = bh->b_data;
	}
#ifdef SANITY
	if ( remaining ){
		if ( remaining > 0 )
			max_sector -= remaining >> 9;
		printk(DEVICE_NAME
		       ": weirdness: remaining %d\n", remaining>>9);
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

	current_drive = DRIVE(CURRENT->dev);

	raw_cmd.flags = FD_RAW_SPIN | FD_RAW_NEED_DISK | FD_RAW_NEED_DISK |
		FD_RAW_NEED_SEEK;
	raw_cmd.cmd_count = NR_RW;
	if (CURRENT->cmd == READ){
		raw_cmd.flags |= FD_RAW_READ;
		COMMAND = FD_READ;
	} else if (CURRENT->cmd == WRITE){
		raw_cmd.flags |= FD_RAW_WRITE;
		COMMAND = FD_WRITE;
	} else {
		printk(DEVICE_NAME
		       ": make_raw_rw_request: unknown command\n");
		return 0;
	}

	max_sector = floppy->sect * floppy->head;
	TRACK = CURRENT->sector / max_sector;
	sector_t = CURRENT->sector % max_sector;
	if ( floppy->track && TRACK >= floppy->track )
		return 0;
	HEAD = sector_t / floppy->sect;

	if ( (DRS->flags & FD_NEED_TWADDLE) && sector_t < floppy->sect )
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

	SIZECODE2 = 0xff;
	raw_cmd.track = TRACK << floppy->stretch;
	DR_SELECT = UNIT(current_drive) + ( HEAD << 2 );
	GAP = floppy->gap;
	ssize = 1 << (SIZECODE - 2 );
	SECT_PER_TRACK = floppy->sect / ssize;
	SECTOR = (sector_t % floppy->sect) / ssize + 1;
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
		if (COMMAND == FD_READ) {
			copy_buffer(1, max_sector, buffer_max);
			return 1;
		}
	} else if (aligned_sector_t != sector_t || CURRENT->nr_sectors < ssize){
		if (COMMAND == FD_WRITE){
			if(sector_t + CURRENT->nr_sectors > ssize &&
			   sector_t + CURRENT->nr_sectors < ssize + ssize)
				max_size = ssize + ssize;
			else
				max_size = ssize;
		}
		raw_cmd.flags &= ~FD_RAW_WRITE;
		raw_cmd.flags |= FD_RAW_READ;
		COMMAND = FD_READ;
	} else if ((long)CURRENT->buffer <= LAST_DMA_ADDR ) {
		int direct, indirect;

		indirect= transfer_size(ssize,max_sector,MAX_BUFFER_SECTORS*2) -
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
		if ((indirect - sector_t) * 2 > (direct - sector_t) * 3 &&
		    *errors < DP->max_errors.read_track &&
		    /*!(DRS->flags & FD_NEED_TWADDLE) &&*/
		    ( ( !probing || (DP->read_track &
			   (1 <<DRS->probed_format))))){
			max_size = CURRENT->nr_sectors;
		} else {
			current_addr = CURRENT->buffer;
			raw_cmd.length = current_count_sectors << 9;
			return 2;
		}
	}

	if ( COMMAND == FD_READ )
		max_size = max_sector; /* unbounded */

	/* claim buffer track if needed */
	if (buffer_track != raw_cmd.track ||  /* bad track */
	    buffer_drive !=current_drive || /* bad drive */
	    sector_t < buffer_min ||
	    ((COMMAND == FD_READ ||
	      (aligned_sector_t == sector_t && CURRENT->nr_sectors >= ssize ))&&
	     max_sector > 2 * MAX_BUFFER_SECTORS + buffer_min &&
	     max_size + sector_t > 2 * MAX_BUFFER_SECTORS + buffer_min)
	    /* not enough space */ ){
		buffer_track = -1;
		buffer_drive = current_drive;
		buffer_max = buffer_min = aligned_sector_t;
	}
	current_addr = floppy_track_buffer +((aligned_sector_t-buffer_min )<<9);

	if ( COMMAND == FD_WRITE ){
		/* copy write buffer to track buffer.
		 * if we get here, we know that the write
		 * is either aligned or the data already in the buffer
		 * (buffer will be overwritten) */
#ifdef SANITY
		if (sector_t != aligned_sector_t && buffer_track == -1 )
			printk(DEVICE_NAME
			       ": internal error offset !=0 on write\n");
#endif
		buffer_track = raw_cmd.track;
		buffer_drive = current_drive;
		copy_buffer(ssize, max_sector, 2*MAX_BUFFER_SECTORS+buffer_min);
	} else
		transfer_size(ssize, max_sector,
			      2*MAX_BUFFER_SECTORS+buffer_min-aligned_sector_t);

	/* round up current_count_sectors to get dma xfer size */
	raw_cmd.length = sector_t+current_count_sectors-aligned_sector_t;
	raw_cmd.length = ((raw_cmd.length -1)|(ssize-1))+1;
	raw_cmd.length <<= 9;
#ifdef SANITY
	if ((raw_cmd.length < current_count_sectors << 9) ||
	    (current_addr != CURRENT->buffer && COMMAND == FD_WRITE &&
	     (aligned_sector_t + (raw_cmd.length >> 9) > buffer_max ||
	      aligned_sector_t < buffer_min )) ||
	    raw_cmd.length % ( 512 << ( SIZECODE -2 )) ||
	    raw_cmd.length <= 0 || current_count_sectors <= 0){
		printk(DEVICE_NAME ": fractionary current count b=%lx s=%lx\n",
		       raw_cmd.length, current_count_sectors);
		if ( current_addr != CURRENT->buffer )
			printk("addr=%d, length=%ld\n",
			       (current_addr - floppy_track_buffer ) >> 9,
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
		    floppy_track_buffer + ( 2 * MAX_BUFFER_SECTORS  << 9 )){
			printk(DEVICE_NAME
			       ": buffer overrun in schedule dma\n");
			printk("sector_t=%d buffer_min=%d current_count=%ld\n",
			       sector_t, buffer_min,
			       raw_cmd.length >> 9 );
			printk("current_count_sectors=%ld\n",
			       current_count_sectors);
			if ( COMMAND == FD_READ )
				printk("read\n");
			if ( COMMAND == FD_READ )
				printk("write\n");
			return 0;
		}
	} else if (raw_cmd.length > CURRENT->nr_sectors << 9 ||
		   current_count_sectors > CURRENT->nr_sectors){
		printk(DEVICE_NAME ": buffer overrun in direct transfer\n");
		return 0;
	} else if ( raw_cmd.length < current_count_sectors << 9 ){
		printk(DEVICE_NAME ": more sectors than bytes\n");
		printk("bytes=%ld\n", raw_cmd.length >> 9 );
		printk("sectors=%ld\n", current_count_sectors);
	}
#endif
	return 2;
}

static void redo_fd_request(void)
{
#define REPEAT {request_done(0); continue; }
	int device;
	int tmp;

	if (CURRENT && CURRENT->dev < 0) return;
	
	/* hooray, the goto is gone! */
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
		
		device = MINOR(CURRENT->dev);
		set_fdc( DRIVE(device));
		CHECK_RESET;
		start_motor();
		if (( changed_floppies | fake_change) & ( 1 << DRIVE(device))){
			printk(DEVICE_NAME
			       ": disk absent or changed during operation\n");
			REPEAT;
		}
		set_floppy(device);
		if (!floppy) { /* Autodetection */
			if (!probing){
				DRS->probed_format = 0;
				if ( next_valid_format() ){
					printk(DEVICE_NAME
					       ": no autodetectable formats\n");
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
		if ( DRS->flags & FD_NEED_TWADDLE )
			twaddle();
		floppy_on(current_drive);
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

void do_fd_request(void)
{
	lock_fdc(-1);
	cont = &rw_cont;
	redo_fd_request();
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

static int user_reset_fdc(int drive, int arg)
{
	int result;

	result=0;
	lock_fdc(drive);
	switch(arg){
	case FD_RESET_ALWAYS:
		FDCS->reset=1;
		break;
	case FD_RESET_IF_RAWCMD:
		if(FDCS->rawcmd == 2 )
			reset_fdc_info(1);
		break;
	}
	if ( FDCS->reset ){
		cont = &reset_cont;
		timer_table[FLOPPY_TIMER].expires = jiffies + 5;
		timer_active |= 1 << FLOPPY_TIMER;
		reset_fdc();
		result=wait_til_done();
	}
	unlock_fdc();
	return result;
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

static void poll_drive(int drive)
{
	lock_fdc(drive);
	start_motor();
	unlock_fdc();
}

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

static int raw_cmd_ioctl(int drive, void *param)
{
	int i, count, ret;

	if ( FDCS->rawcmd <= 1 )
		FDCS->rawcmd = 1;
	for ( i= 0; i < N_DRIVE; i++){
		if ( FDC(i) != fdc)
			continue;
		if ( i == drive ){
			if ( drive_state[i].fd_ref > 1 )
				return -EBUSY;
		} else if ( drive_state[i].fd_ref )
			return -EBUSY;
	}

	if(FDCS->reset)
		return -EIO;

	COPYIN(raw_cmd);
	raw_cmd.rate &= 0x03;	
	count = raw_cmd.length;
	if ((raw_cmd.flags & (FD_RAW_WRITE | FD_RAW_READ)) &&
	    count > MAX_BUFFER_SECTORS * 512 * 2 )
		return -ENOMEM;

	if ( raw_cmd.flags & FD_RAW_WRITE ){
		i = verify_area(VERIFY_READ, raw_cmd.data, count );
		if (i)
			return i;
		buffer_track = -1;
		memcpy_fromfs(floppy_track_buffer, raw_cmd.data, count);
	}

	current_addr = floppy_track_buffer;
	raw_cmd.flags |= FD_RAW_USER_SUPPLIED;
	cont =  &raw_cmd_cont;
	floppy_on(current_drive);
	ret=wait_til_done();

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
	cli();
	fake_change |= 1 << DRIVE(rdev);
	sti();
	unlock_fdc();
	check_disk_change(rdev);
	return 0;
}

static int fd_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
    unsigned long param)
{
#define IOCTL_MODE_BIT 8
#define IOCTL_ALLOWED (filp && (filp->f_mode & IOCTL_MODE_BIT))

	struct floppy_struct newparams;
	struct format_descr tmp_format_req;
	int i,device,drive,type,cnt;
	struct floppy_struct *this_floppy;
	char *name;

	device = MINOR(inode->i_rdev);
	switch (cmd) {
		RO_IOCTLS(device,param);
	}
	type = TYPE(MINOR(device));
	drive = DRIVE(MINOR(device));
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
		poll_drive(drive);
		/* fall through */
	case FDGETDRVSTAT:
		return COPYOUT(*UDRS);
	case FDGETFDCSTAT:
		return COPYOUT(*UFDCS);
	case FDGETDRVPRM:
		return COPYOUT(*UDP);
	}
	if (!IOCTL_ALLOWED)
		return -EPERM;
	switch (cmd) {
	case FDRAWCMD:
		if (type)
			return -EINVAL;
		lock_fdc(drive);
		set_floppy(device);
		i = raw_cmd_ioctl(drive, (void *) param);
		unlock_fdc();
		return i;
	case FDFMTTRK:
		if (UDRS->fd_ref != 1)
			return -EBUSY;
		if (! (UDRS->flags & FD_DRIVE_PRESENT ))
			return -ENXIO;
		COPYIN(tmp_format_req);
		return do_format(device, &tmp_format_req);
	case FDSETMAXERRS:
		return COPYIN(UDP->max_errors);
	case FDFMTBEG:
		return 0;
	case FDCLRPRM:
		lock_fdc(drive);
		current_type[drive] = NULL;
		floppy_sizes[drive] = 2;
		UDRS->keep_data = 0;
		return invalidate_drive(device);
	case FDFMTEND:
	case FDFLUSH:
		lock_fdc(drive);
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
			lock_fdc(-1);
			for ( cnt = 0; cnt < N_DRIVE; cnt++){
				if (TYPE(drive_state[cnt].fd_device) == type &&
				    drive_state[cnt].fd_ref){
					cli(); fake_change |= 1 << cnt; sti();
				}
			}
			floppy_type[type] = newparams;
			floppy_type[type].name="user format";
			for (cnt = type << 2 ;
			     cnt < (type << 2 ) + 4 ;
			     cnt++)
				floppy_sizes[cnt]=
#ifdef HAVE_2_CONTROLLERS
					floppy_sizes[cnt+0x80]=
#endif
						floppy_type[type].size>>1;
			unlock_fdc();
			for ( cnt = 0; cnt < N_DRIVE; cnt++){
				if (TYPE(drive_state[cnt].fd_device) == type &&
				    drive_state[cnt].fd_ref)
					check_disk_change(drive_state[cnt].
							  fd_device);
			}
			return 0;
		}

		lock_fdc(drive);
		if ( cmd != FDDEFPRM )
			/* notice a disk change immediately, else
			 * we loose our settings immediately*/
			start_motor();
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
			return invalidate_drive(device);
		else
			return unlock_fdc();
	case FDRESET:
		return user_reset_fdc( drive, (int)param);
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
		lock_fdc(drive);
		twaddle();
		unlock_fdc();
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

#define CMOS_READ(addr) ({ \
outb_p(addr,0x70); \
inb_p(0x71); \
})

static void set_base_type(int drive,int code)
{
	if (code > 0 && code <= NUMBER(default_drive_params)) {
		memcpy((char *) UDP,
		       (char *) (&default_drive_params[code].params),
		       sizeof( struct floppy_drive_params ));
		printk("fd%d is %s", drive, default_drive_params[code].name);
		return;
	} else if (!code)
		printk("fd%d is not installed", drive);
	else
		printk("fd%d is unknown type %d",drive,code);
}

static void config_types(void)
{
	int drive;

	for (drive=0; drive<N_DRIVE ; drive++){
		/* default type for unidentifiable drives */
		memcpy((char *) UDP, (char *) (&default_drive_params->params),
		       sizeof( struct floppy_drive_params ));
	}
	printk("Floppy drive(s): ");
	set_base_type(0, (CMOS_READ(0x10) >> 4) & 15);
	if (CMOS_READ(0x10) & 15) {
		printk(", ");
		set_base_type(1, CMOS_READ(0x10) & 15);
	}
	printk("\n");
}


static void maybe_check_change(int device)
{
	register int drive;

	drive = DRIVE(device);
	if (UDRS->last_checked + UDP->checkfreq < jiffies  ||
	    UDRS->flags & FD_VERIFY ||
	    (( changed_floppies | fake_change ) & ( 1 << drive)))
		check_disk_change(device);
}

int floppy_is_wp( int minor)
{
	maybe_check_change(minor + (MAJOR_NR << 8));
	return ! ( drive_state[ DRIVE(minor) ].flags & FD_DISK_WRITABLE );
}


#define WRAPPER(op) \
static int floppy_##op(struct inode * inode, struct file * filp, \
		     char * buf, int count) \
{ \
	maybe_check_change(inode->i_rdev);	\
	if ( changed_floppies & ( 1 << DRIVE(inode->i_rdev) )) \
		return -ENXIO; \
	return block_##op(inode, filp, buf, count); \
}

WRAPPER(read)
WRAPPER(write)

static int exclusive = 0;
static void floppy_release(struct inode * inode, struct file * filp)
{
	int drive= DRIVE(inode->i_rdev);

	fsync_dev(inode->i_rdev);
	if ( UDRS->fd_ref < 0)
		UDRS->fd_ref=0;
	else if (!UDRS->fd_ref--) {
		printk(DEVICE_NAME ": floppy_release with fd_ref == 0");
		UDRS->fd_ref = 0;
	}
	floppy_release_irq_and_dma();
	exclusive=0;
}

/*
 * floppy_open check for aliasing (/dev/fd0 can be the same as
 * /dev/PS0 etc), and disallows simultaneous access to the same
 * drive with different device numbers.
 */
#define RETERR(x) \
	do{floppy_release(inode,filp); \
	   return -(x);}while(0)
static int usage_count = 0;
static int floppy_open(struct inode * inode, struct file * filp)
{
	int drive;
	int old_dev;

	if (exclusive)
		return -EBUSY;

	if (!filp) {
		printk(DEVICE_NAME ": Weird, open called with filp=0\n");
		return -EIO;
	}

	drive = DRIVE(inode->i_rdev);
	if ( drive >= N_DRIVE )
		return -ENXIO;

	if (command_status == FD_COMMAND_DETECT && drive >= current_drive) {
		lock_fdc(-1);
		unlock_fdc();
	}

	if (TYPE(inode->i_rdev) >= NUMBER(floppy_type))
		return -ENXIO;

	if (filp->f_mode & 3) {
		if ( !(UDRS->flags & FD_DRIVE_PRESENT))
			return -ENXIO;
	}

	old_dev = UDRS->fd_device;
	if (UDRS->fd_ref && old_dev != inode->i_rdev)
		return -EBUSY;

	if (filp->f_flags & O_EXCL) {
		if (usage_count)
			return -EBUSY;
		else
			exclusive = 1;
	}

	if (floppy_grab_irq_and_dma())
		return -EBUSY;

	UDRS->fd_ref++;
	UDRS->fd_device = inode->i_rdev;

	if (old_dev && old_dev != inode->i_rdev) {
		if (buffer_drive == drive)
			buffer_track = -1;
		invalidate_buffers(old_dev);
	}

	/* Allow ioctls if we have write-permissions even if read-only open */
	if ((filp->f_mode & 2) || permission(inode,2))
		filp->f_mode |= IOCTL_MODE_BIT;

	if (UFDCS->rawcmd == 1)
	       UFDCS->rawcmd = 2;

	if (filp->f_flags & O_NDELAY)
		return 0;

	if (filp->f_mode && !(UDRS->flags & FD_DRIVE_PRESENT))
		RETERR(ENXIO);

	if (user_reset_fdc(drive, FD_RESET_IF_NEEDED))
		RETERR(EIO);

	if (filp->f_mode & 3) {
		check_disk_change(inode->i_rdev);
		if (changed_floppies & ( 1 << drive ))
			RETERR(ENXIO);
	}
	if ((filp->f_mode & 2) && !(UDRS->flags & FD_DISK_WRITABLE))
		RETERR(EROFS);
	return 0;
#undef RETERR
}

/*
 * Acknowledge disk change
 */
static int ack_change(int drive)
{
	unsigned int mask = 1 << drive;
	UDRS->maxblock = 0;
	UDRS->maxtrack = 0;
	if ( buffer_drive == drive )
		buffer_track = -1;
	fake_change &= ~mask;
	changed_floppies &= ~mask;
	return 1;
}

/*
 * Check if the disk has been changed or if a change has been faked.
 */
static int check_floppy_change(dev_t dev)
{
	int drive = DRIVE( dev );
	unsigned int mask = 1 << drive;

	if (MAJOR(dev) != MAJOR_NR) {
		printk(DEVICE_NAME ": floppy_changed: not a floppy\n");
		return 0;
	}

	if (fake_change & mask)
		return ack_change(drive);

	if ((UDRS->flags & FD_VERIFY ) || (changed_floppies & mask) ||
	    UDRS->last_checked + UDP->checkfreq <
	    jiffies){
		user_reset_fdc(drive, FD_RESET_IF_NEEDED);
		poll_drive(drive);
		if (changed_floppies & mask){
			UDRS->generation++;
			return ack_change(drive);
		}
	}
	return 0;
}

/* revalidate the floppy disk, i.e. trigger format autodetection by reading
 * the bootblock (block 0) */
static int floppy_revalidate(dev_t dev)
{
	struct buffer_head * bh;

	if ( TYPE(dev) || current_type[DRIVE(dev)] )
		return 0;
	if (!(bh = getblk(dev,0,1024)))
		return 1;
	if ( bh && ! bh->b_uptodate)
		ll_rw_block(READ, 1, &bh);
	brelse(bh);
	return 0;
}

static struct file_operations floppy_fops = {
	NULL,			/* lseek - default */
	floppy_read,		/* read - general block-dev read */
	floppy_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
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
} /* fdc_init */

/*
 * Drive detection routine. This runs in the background while the kernel
 * does other (non floppy related) initialisation work.
 */
static void detect_interrupt(void)
{
	if ( DRS->track == 0 )
		DRS->flags |= FD_DRIVE_PRESENT | FD_VERIFY;
	else if ( DRS->track != NEED_1_RECAL ){
		floppy_ready();
		return;
	}
	floppy_off(current_drive);
	current_drive++;
	if (current_drive == N_DRIVE ||
	    fdc_state[FDC(current_drive)].version == FDC_NONE){
		set_fdc(0);
		unlock_fdc();
		floppy_release_irq_and_dma();
		return;
	}
	set_fdc(current_drive);
	floppy_ready(); /* next */
}

static struct cont_t detect_cont={
	detect_interrupt,
	detect_interrupt,
	empty,
	(done_f) empty };

void floppy_init(void)
{
	int i;

	if (register_blkdev(MAJOR_NR,"fd",&floppy_fops)) {
		printk("Unable to get major %d for floppy\n",MAJOR_NR);
		return;
	}

	for(i=0; i<256; i++)
		if ( TYPE(i))
			floppy_sizes[i] = floppy_type[TYPE(i)].size >> 1;
		else
			floppy_sizes[i] = MAX_DISK_SIZE;

	blk_size[MAJOR_NR] = floppy_sizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	timer_table[FLOPPY_TIMER].fn = floppy_shutdown;
	timer_active &= ~(1 << FLOPPY_TIMER);
	config_types();

	fdc_state[0].address = 0x3f0;
#if N_FDC > 1
	fdc_state[1].address = 0x370;
#endif
	for(fdc = 0 ; fdc < N_FDC; fdc++){
		FDCS->dtr = -1;
		FDCS->dor = 0;
		FDCS->reset = 0;
		FDCS->version = FDC_NONE;
		set_dor(fdc, ~0, 0xc );
	}

	/* initialise drive state */
	for ( current_drive=0; current_drive < N_DRIVE ; current_drive++){
		DRS->flags = 0;
		DRS->generation = 0;
		DRS->keep_data = 0;
		DRS->fd_ref = 0;
		DRS->fd_device = 0;
	}

	floppy_grab_irq_and_dma();
	for(fdc = 0 ; fdc < N_FDC; fdc++){
		FDCS->rawcmd = 2;
		if(user_reset_fdc(-1,FD_RESET_IF_NEEDED))
			continue;
		/* Try to determine the floppy controller type */
		FDCS->version = get_fdc_version();
		if (FDCS->version == FDC_NONE)
			continue;

		/* Not all FDCs seem to be able to handle the version command
		 * properly, so force a reset for the standard FDC clones,
		 * to avoid interrupt garbage.
		 */
		FDCS->has_fifo = FDCS->version >= FDC_82077_ORIG;
		user_reset_fdc(-1,FD_RESET_ALWAYS);
	}
	fdc=0;
	current_drive = 0;
	lock_fdc(-1);
	initialising =0;
	command_status = FD_COMMAND_DETECT;
	cont = &detect_cont;
	raw_cmd.cmd_count = 0;
	raw_cmd.flags = FD_RAW_NEED_SEEK;
	raw_cmd.track = 0;
	floppy_ready();
	cli();
}

int floppy_grab_irq_and_dma(void)
{
	if (usage_count++)
		return 0;
	if (request_irq(FLOPPY_IRQ, floppy_interrupt, SA_INTERRUPT, "floppy")) {
		printk(DEVICE_NAME
		       ": Unable to grab IRQ%d for the floppy driver\n",
		       FLOPPY_IRQ);
		return -1;
	}
	if (request_dma(FLOPPY_DMA)) {
		printk(DEVICE_NAME
		       ": Unable to grab DMA%d for the floppy driver\n",
		       FLOPPY_DMA);
		free_irq(FLOPPY_IRQ);
		return -1;
	}
	enable_irq(FLOPPY_IRQ);
	return 0;
}

void floppy_release_irq_and_dma(void)
{
	if (--usage_count)
		return;
	disable_dma(FLOPPY_DMA);
	free_dma(FLOPPY_DMA);
	disable_irq(FLOPPY_IRQ);
	free_irq(FLOPPY_IRQ);
#ifdef HAVE_2_CONTROLLERS
	/* switch on first controller.
	 * This saves us trouble on the next reboot. */
	set_dor(0, ~0, 8 );
	set_dor(1, ~8, 0 );
#endif
}
