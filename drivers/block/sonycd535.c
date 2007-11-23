/*
 * Sony CDU-535 interface device driver
 *
 * This is a modified version of the CDU-31A device driver (see below).
 * Changes were made using documentation for the CDU-531 (which Sony
 * assures me is very similar to the 535) and partial disassembly of the
 * DOS driver.  I used Minyard's driver and replaced the the CDU-31A
 * commands with the CDU-531 commands.  This was complicated by a different
 * interface protocol with the drive.  The driver is still polled.
 *
 * Data transfer rate is about 110 Kb/sec, theoretical maximum is 150 Kb/sec.
 * I tried polling without the sony_sleep during the data transfers but
 * it did not speed things up any.
 *
 *  5/23/93 (rgj) changed the major number to 21 to get rid of conflict
 * with CDU-31A driver.  This is the also the number from the Linux
 * Device Driver Registry for the Sony Drive.  Hope nobody else is using it.
 *
 *  8/29/93 (rgj) remove the configuring of the interface board address
 * from the top level configuration, you have to modify it in this file.
 *
 * 1/26/95 Made module-capable (Joel Katz <Stimpson@Panix.COM>)
 *
 * Things to do:
 *  - handle errors and status better, put everything into a single word
 *  - use interrupts (code mostly there, but a big hole still missing)
 *  - handle multi-session CDs?
 *  - use DMA?
 *
 *  Known Bugs:
 *  -
 *
 *   Ken Pizzini (ken@halcyon.com)
 *
 * Original by:
 *   Ron Jeppesen (ronj.an@site007.saic.com)
 *
 *
 *------------------------------------------------------------------------
 * Sony CDROM interface device driver.
 *
 * Corey Minyard (minyard@wf-rch.cirr.com) (CDU-535 complaints to Ken above)
 *
 * Colossians 3:17
 *
 * The Sony interface device driver handles Sony interface CDROM
 * drives and provides a complete block-level interface as well as an
 * ioctl() interface compatible with the Sun (as specified in
 * include/linux/cdrom.h).  With this interface, CDROMs can be
 * accessed and standard audio CDs can be played back normally.
 *
 * This interface is (unfortunately) a polled interface.  This is
 * because most Sony interfaces are set up with DMA and interrupts
 * disables.  Some (like mine) do not even have the capability to
 * handle interrupts or DMA.  For this reason you will see a lot of
 * the following:
 *
 *   retry_count = jiffies+ SONY_JIFFIES_TIMEOUT;
 *   while ((retry_count > jiffies) && (! <some condition to wait for))
 *   {
 *      while (handle_sony_cd_attention())
 *         ;
 *
 *      sony_sleep();
 *   }
 *   if (the condition not met)
 *   {
 *      return an error;
 *   }
 *
 * This ugly hack waits for something to happen, sleeping a little
 * between every try.  it also handles attentions, which are
 * asynchronous events from the drive informing the driver that a disk
 * has been inserted, removed, etc.
 *
 * One thing about these drives: They talk in MSF (Minute Second Frame) format.
 * There are 75 frames a second, 60 seconds a minute, and up to 75 minutes on a
 * disk.  The funny thing is that these are sent to the drive in BCD, but the
 * interface wants to see them in decimal.  A lot of conversion goes on.
 *
 *  Copyright (C) 1993  Corey Minyard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#include <linux/config.h>
#if defined(CONFIG_CDU535) || defined(MODULE)

#ifdef MODULE
# include <linux/module.h>
# include <linux/malloc.h>
# include <linux/version.h>
# ifndef CONFIG_MODVERSIONS
	char kernel_version[]= UTS_RELEASE;
# endif
#endif

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/mm.h>

#define REALLY_SLOW_IO
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <linux/cdrom.h>
#include <linux/sonycd535.h>

#define MAJOR_NR CDU535_CDROM_MAJOR

#ifdef MODULE
# include "/usr/src/linux/drivers/block/blk.h"
#else
# include "blk.h"
# define MOD_INC_USE_COUNT
# define MOD_DEC_USE_COUNT
#endif

/*
 * this is the base address of the interface card for the Sony CDU-535
 * CDROM drive.  If your jumpers are set for an address other than
 * this one (the default), change the following line to the
 * proper address.
 */
#ifndef CDU535_ADDRESS
# define CDU535_ADDRESS			0x340
#endif
#ifndef CDU535_INTERRUPT
# define CDU535_INTERRUPT		0
#endif
#ifndef CDU535_HANDLE
# define CDU535_HANDLE			"cdu535"
#endif
#ifndef CDU535_MESSAGE_NAME
# define CDU535_MESSAGE_NAME	"Sony CDU-535"
#endif

#ifndef DEBUG
# define DEBUG	1
#endif

/*
 *  SONY535_BUFFER_SIZE determines the size of internal buffer used
 *  by the drive.  It must be at least 2K and the larger the buffer
 *  the better the transfer rate.  It does however take system memory.
 *  On my system I get the following transfer rates using dd to read
 *  10 Mb off /dev/cdrom.
 *
 *    8K buffer      43 Kb/sec
 *   16K buffer      66 Kb/sec
 *   32K buffer      91 Kb/sec
 *   64K buffer     111 Kb/sec
 *  128K buffer     123 Kb/sec
 *  512K buffer     123 Kb/sec
 */
#define SONY535_BUFFER_SIZE	(64*1024)

/*
 *  if LOCK_DOORS is defined then the eject button is disabled while
 * the device is open.
 */
#ifndef NO_LOCK_DOORS
# define LOCK_DOORS
#endif

static int read_subcode(void);
static void sony_get_toc(void);
static int cdu_open(struct inode *inode, struct file *filp);
static inline unsigned int int_to_bcd(unsigned int val);
static unsigned int bcd_to_int(unsigned int bcd);
static int do_sony_cmd(Byte * cmd, int nCmd, Byte status[2],
					   Byte * response, int n_response, int ignoreStatusBit7);

/* The base I/O address of the Sony Interface.  This is a variable (not a
   #define) so it can be easily changed via some future ioctl() */
#ifndef MODULE
static
#endif
unsigned short sony535_cd_base_io = CDU535_ADDRESS;

/*
 * The following are I/O addresses of the various registers for the drive.  The
 * comment for the base address also applies here.
 */
static unsigned short select_unit_reg;
static unsigned short result_reg;
static unsigned short command_reg;
static unsigned short read_status_reg;
static unsigned short data_reg;

static int initialized = 0;			/* Has the drive been initialized? */
static int sony_disc_changed = 1;	/* Has the disk been changed
									   since the last check? */
static int sony_toc_read = 0;		/* Has the table of contents been
									   read? */
static unsigned int sony_buffer_size;	/* Size in bytes of the read-ahead
										   buffer. */
static unsigned int sony_buffer_sectors;	/* Size (in 2048 byte records) of
											   the read-ahead buffer. */
static unsigned int sony_usage = 0;	/* How many processes have the
									   drive open. */

static int sony_first_block = -1;	/* First OS block (512 byte) in
									   the read-ahead buffer */
static int sony_last_block = -1;	/* Last OS block (512 byte) in
									   the read-ahead buffer */

static struct s535_sony_toc *sony_toc;	/* Points to the table of
										   contents. */
static struct s535_sony_subcode *last_sony_subcode;		/* Points to the last
														   subcode address read */
#ifndef MODULE
static Byte *sony_buffer;		/* Points to the read-ahead buffer */
#else
static Byte **sony_buffer;		/* Points to the pointers
								   to the sector buffers */
#endif
static int sony_inuse = 0;		/* is the drive in use? Only one
								   open at a time allowed */

/*
 * The audio status uses the values from read subchannel data as specified
 * in include/linux/cdrom.h.
 */
static int sony_audio_status = CDROM_AUDIO_NO_STATUS;

/*
 * The following are a hack for pausing and resuming audio play.  The drive
 * does not work as I would expect it, if you stop it then start it again,
 * the drive seeks back to the beginning and starts over.  This holds the
 * position during a pause so a resume can restart it.  It uses the
 * audio status variable above to tell if it is paused.
 *   I just kept the CDU-31A driver behavior rather than using the PAUSE
 * command on the CDU-535.
 */
static Byte cur_pos_msf[3] = {0, 0, 0};
static Byte final_pos_msf[3] = {0, 0, 0};

/* What IRQ is the drive using?  0 if none. */
#ifndef MODULE
static
#endif
int sony535_irq_used = CDU535_INTERRUPT;

/* The interrupt handler will wake this queue up when it gets an interrupt. */
static struct wait_queue *cdu535_irq_wait = NULL;


/*
 * This routine returns 1 if the disk has been changed since the last
 * check or 0 if it hasn't.  Setting flag to 0 resets the changed flag.
 */
static int
cdu535_check_media_change(dev_t full_dev)
{
	int retval;

	if (MINOR(full_dev) != 0) {
		printk(CDU535_MESSAGE_NAME " request error: invalid device.\n");
		return 0;
	}

	/* if driver is not initialized, always return 0 */
	retval = initialized ? sony_disc_changed : 0;
	sony_disc_changed = 0;
	return retval;
}

static inline void
enable_interrupts(void)
{
#ifdef USE_IRQ
	/* this code snarfed from cdu31a.c; it will not
	 * directly work for the cdu535 as written...
	 */
	curr_control_reg |= ( SONY_ATTN_INT_EN_BIT
						| SONY_RES_RDY_INT_EN_BIT
						| SONY_DATA_RDY_INT_EN_BIT);
	outb(curr_control_reg, sony_cd_control_reg);
#endif
}

static inline void
disable_interrupts(void)
{
#ifdef USE_IRQ
	/* this code snarfed from cdu31a.c; it will not
	 * directly work for the cdu535 as written...
	 */
	curr_control_reg &= ~(SONY_ATTN_INT_EN_BIT
						| SONY_RES_RDY_INT_EN_BIT
						| SONY_DATA_RDY_INT_EN_BIT);
	outb(curr_control_reg, sony_cd_control_reg);
#endif
}

static void
cdu535_interrupt(int irq, struct pt_regs *regs)
{
	disable_interrupts();
	if (cdu535_irq_wait != NULL)
		wake_up(&cdu535_irq_wait);
	else
		printk(CDU535_MESSAGE_NAME
				": Got an interrupt but nothing was waiting\n");
}


/*
 * Wait a little while (used for polling the drive).  If in initialization,
 * setting a timeout doesn't work, so just loop for a while.  (We trust
 * that the sony_sleep() call is protected by a test for proper jiffies count.)
 */
static inline void
sony_sleep(void)
{
	if (sony535_irq_used <= 0) {	/* poll */
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies;
		schedule();
	} else {	/* Interrupt driven */
		cli();
		enable_interrupts();
		interruptible_sleep_on(&cdu535_irq_wait);
		sti();
	}
}

/*------------------start of SONY CDU535 very specific ---------------------*/

/****************************************************************************
 * void select_unit( int unit_no )
 *
 *  Select the specified unit (0-3) so that subsequent commands reference it
 ****************************************************************************/
static void
select_unit(int unit_no)
{
	unsigned int select_mask = ~(1 << unit_no);
	outb(select_mask, select_unit_reg);
}

/***************************************************************************
 * int read_result_reg( Byte *data_ptr )
 *
 *  Read a result byte from the Sony CDU controller, store in location pointed
 * to by data_ptr.  Return zero on success, TIME_OUT if we did not receive
 * data.
 ***************************************************************************/
static int
read_result_reg(Byte *data_ptr)
{
	int retry_count;
	int read_status;

	retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
	while (jiffies < retry_count) {
		if (((read_status = inb(read_status_reg)) & SONY535_RESULT_NOT_READY_BIT) == 0) {
#if DEBUG > 1
			printk(CDU535_MESSAGE_NAME
					": read_result_reg(): readStatReg = 0x%x\n", read_status);
#endif
			*data_ptr = inb(result_reg);
			return 0;
		} else {
			sony_sleep();
		}
	}
	printk(CDU535_MESSAGE_NAME " read_result_reg: TIME OUT!\n");
	return TIME_OUT;
}

/****************************************************************************
 * int read_exec_status( Byte status[2] )
 *
 *  Read the execution status of the last command and put into status.
 * Handles reading second status word if available.  Returns 0 on success,
 * TIME_OUT on failure.
 ****************************************************************************/
static int
read_exec_status(Byte status[2])
{
	status[1] = 0;
	if (read_result_reg(&(status[0])) != 0)
		return TIME_OUT;
	if ((status[0] & 0x80) != 0) {	/* byte two follows */
		if (read_result_reg(&(status[1])) != 0)
			return TIME_OUT;
	}
#if DEBUG > 1
	printk(CDU535_MESSAGE_NAME ": read_exec_status: read 0x%x 0x%x\n",
			status[0], status[1]);
#endif
	return 0;
}

/****************************************************************************
 * int check_drive_status( void )
 *
 *  Check the current drive status.  Using this before executing a command
 * takes care of the problem of unsolicited drive status-2 messages.
 * Add a check of the audio status if we think the disk is playing.
 ****************************************************************************/
static int
check_drive_status(void)
{
	Byte status, e_status[2];
	int  CDD, ATN;
	Byte cmd;

	select_unit(0);
	if (sony_audio_status == CDROM_AUDIO_PLAY) {	/* check status */
		outb(SONY535_REQUEST_AUDIO_STATUS, command_reg);
		if (read_result_reg(&status) == 0) {
			switch (status) {
			case 0x0:
				break;		/* play in progress */
			case 0x1:
				break;		/* paused */
			case 0x3:		/* audio play completed */
			case 0x5:		/* play not requested */
				sony_audio_status = CDROM_AUDIO_COMPLETED;
				read_subcode();
				break;
			case 0x4:		/* error during play */
				sony_audio_status = CDROM_AUDIO_ERROR;
				break;
			}
		}
	}
	/* now check drive status */
	outb(SONY535_REQUEST_DRIVE_STATUS_2, command_reg);
	if (read_result_reg(&status) != 0)
		return TIME_OUT;

#if DEBUG > 1
	printk(CDU535_MESSAGE_NAME ": check_drive_status() got 0x%x\n", status);
#endif

	if (status == 0)
		return 0;

	ATN = status & 0xf;
	CDD = (status >> 4) & 0xf;

	switch (ATN) {
	case 0x0:
		break;					/* go on to CDD stuff */
	case SONY535_ATN_BUSY:
		if (initialized)
			printk(CDU535_MESSAGE_NAME " error: drive busy\n");
		return CD_BUSY;
	case SONY535_ATN_EJECT_IN_PROGRESS:
		printk(CDU535_MESSAGE_NAME " error: eject in progress\n");
		sony_audio_status = CDROM_AUDIO_INVALID;
		return CD_BUSY;
	case SONY535_ATN_RESET_OCCURRED:
	case SONY535_ATN_DISC_CHANGED:
	case SONY535_ATN_RESET_AND_DISC_CHANGED:
#if DEBUG > 0
		printk(CDU535_MESSAGE_NAME " notice: reset occurred or disc changed\n");
#endif
		sony_disc_changed = 1;
		sony_toc_read = 0;
		sony_audio_status = CDROM_AUDIO_NO_STATUS;
		sony_first_block = -1;
		sony_last_block = -1;
		if (initialized) {
			cmd = SONY535_SPIN_UP;
			do_sony_cmd(&cmd, 1, e_status, NULL, 0, 0);
			sony_get_toc();
		}
		return 0;
	default:
		printk(CDU535_MESSAGE_NAME " error: drive busy (ATN=0x%x)\n", ATN);
		return CD_BUSY;
	}
	switch (CDD) {			/* the 531 docs are not helpful in decoding this */
	case 0x0:				/* just use the values from the DOS driver */
	case 0x2:
	case 0xa:
		break;				/* no error */
	case 0xc:
		printk(CDU535_MESSAGE_NAME
				": check_drive_status(): CDD = 0xc! Not properly handled!\n");
		return CD_BUSY;		/* ? */
	default:
		return CD_BUSY;
	}
	return 0;
}	/* check_drive_status() */

/*****************************************************************************
 * int do_sony_cmd( Byte *cmd, int n_cmd, Byte status[2],
 *                Byte *response, int n_response, int ignore_status_bit7 )
 *
 *  Generic routine for executing commands.  The command and its parameters
 *  should be placed in the cmd[] array, number of bytes in the command is
 *  stored in nCmd.  The response from the command will be stored in the
 *  response array.  The number of bytes you expect back (excluding status)
 *  should be passed in n_response.  Finally, some
 *  commands set bit 7 of the return status even when there is no second
 *  status byte, on these commands set ignoreStatusBit7 TRUE.
 *    If the command was sent and data received back, then we return 0,
 *  else we return TIME_OUT.  You still have to check the status yourself.
 *    You should call check_drive_status() before calling this routine
 *  so that you do not lose notifications of disk changes, etc.
 ****************************************************************************/
static int
do_sony_cmd(Byte * cmd, int n_cmd, Byte status[2],
			Byte * response, int n_response, int ignore_status_bit7)
{
	int i;

	/* write out the command */
	for (i = 0; i < n_cmd; i++)
		outb(cmd[i], command_reg);

	/* read back the status */
	if (read_result_reg(status) != 0)
		return TIME_OUT;
	if (!ignore_status_bit7 && ((status[0] & 0x80) != 0)) {
		/* get second status byte */
		if (read_result_reg(status + 1) != 0)
			return TIME_OUT;
	} else {
		status[1] = 0;
	}
#if DEBUG > 2
	printk(CDU535_MESSAGE_NAME ": do_sony_cmd %x: %x %x\n",
			*cmd, status[0], status[1]);
#endif

	/* do not know about when I should read set of data and when not to */
	if ((status[0] & ((ignore_status_bit7 ? 0x7f : 0xff) & 0x8f)) != 0)
		return 0;

	/* else, read in rest of data */
	for (i = 0; 0 < n_response; n_response--, i++)
		if (read_result_reg(response + i) != 0)
			return TIME_OUT;
	return 0;
}	/* do_sony_cmd() */

/**************************************************************************
 * int set_drive_mode( int mode, Byte status[2] )
 *
 *  Set the drive mode to the specified value (mode=0 is audio, mode=e0
 * is mode-1 CDROM
 **************************************************************************/
static int
set_drive_mode(int mode, Byte status[2])
{
	Byte cmd_buff[2];
	Byte ret_buff[1];

	cmd_buff[0] = SONY535_SET_DRIVE_MODE;
	cmd_buff[1] = mode;
	return do_sony_cmd(cmd_buff, 2, status, ret_buff, 1, 1);
}

/***************************************************************************
 * int seek_and_read_N_blocks( Byte params[], int n_blocks, Byte status[2],
 *                             Byte *data_buff, int buff_size )
 *
 *  Read n_blocks of data from the CDROM starting at position params[0:2],
 *  number of blocks in stored in params[3:5] -- both these are already
 *  int bcd format.
 *  Transfer the data into the buffer pointed at by data_buff.  buff_size
 *  gives the number of bytes available in the buffer.
 *    The routine returns number of bytes read in if successful, otherwise
 *  it returns one of the standard error returns.
 ***************************************************************************/
static int
#ifndef MODULE
seek_and_read_N_blocks(Byte params[], int n_blocks, Byte status[2],
					   Byte * data_buff, int buf_size)
#else
seek_and_read_N_blocks(Byte params[], int n_blocks, Byte status[2],
					   Byte **buff, int buf_size)
#endif
{
	const int block_size = 2048;
	Byte cmd_buff[7];
	int  i;
	int  read_status;
	int  retry_count;
#ifndef MODULE
	Byte *start_pos = data_buff;
#else
	Byte *data_buff;
	int  sector_count = 0;
#endif

	if (buf_size < ((long)block_size) * n_blocks)
		return NO_ROOM;

	set_drive_mode(SONY535_CDROM_DRIVE_MODE, status);

	/* send command to read the data */
	cmd_buff[0] = SONY535_SEEK_AND_READ_N_BLOCKS_1;
	for (i = 0; i < 6; i++)
		cmd_buff[i + 1] = params[i];
	for (i = 0; i < 7; i++)
		outb(cmd_buff[i], command_reg);

	/* read back the data one block at a time */
	while (0 < n_blocks--) {
		/* wait for data to be ready */
		retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
		while (jiffies < retry_count) {
			read_status = inb(read_status_reg);
			if ((read_status & SONY535_RESULT_NOT_READY_BIT) == 0) {
				read_exec_status(status);
				return BAD_STATUS;
			}
			if ((read_status & SONY535_DATA_NOT_READY_BIT) == 0) {
				/* data is ready, read it */
#ifdef MODULE
				data_buff = buff[sector_count++];
#endif
				for (i = 0; i < block_size; i++)
					*data_buff++ = inb(data_reg);	/* unrolling this loop does not seem to help */
				break;			/* exit the timeout loop */
			}
			sony_sleep();		/* data not ready, sleep a while */
		}
		if (retry_count <= jiffies)
			return TIME_OUT;	/* if we reach this stage */
	}

	/* read all the data, now read the status */
	if ((i = read_exec_status(status)) != 0)
		return i;
#ifndef MODULE
	return data_buff - start_pos;
#else
	return block_size * sector_count;
#endif
}	/* seek_and_read_N_blocks() */

/****************************************************************************
 * int request_toc_data( Byte status[2], struct s535_sony_toc *toc )
 *
 *  Read in the table of contents data.  Converts all the bcd data
 * into integers in the toc structure.
 ****************************************************************************/
static int
request_toc_data(Byte status[2], struct s535_sony_toc *toc)
{
	int  to_status;
	int  i, j, n_tracks, track_no;
	int  first_track_num, last_track_num;
	Byte cmd_no = 0xb2;
	Byte track_address_buffer[5];

	/* read the fixed portion of the table of contents */
	if ((to_status = do_sony_cmd(&cmd_no, 1, status, (Byte *) toc, 15, 1)) != 0)
		return to_status;

	/* convert the data into integers so we can use them */
	first_track_num = bcd_to_int(toc->first_track_num);
	last_track_num = bcd_to_int(toc->last_track_num);
	n_tracks = last_track_num - first_track_num + 1;

	/* read each of the track address descriptors */
	for (i = 0; i < n_tracks; i++) {
		/* read the descriptor into a temporary buffer */
		for (j = 0; j < 5; j++) {
			if (read_result_reg(track_address_buffer + j) != 0)
				return TIME_OUT;
			if (j == 1)		/* need to convert from bcd */
				track_no = bcd_to_int(track_address_buffer[j]);
		}
		/* copy the descriptor to proper location - sonycd.c just fills */
		memcpy(toc->tracks + i, track_address_buffer, 5);
	}
	return 0;
}	/* request_toc_data() */

/***************************************************************************
 * int spin_up_drive( Byte status[2] )
 *
 *  Spin up the drive (unless it is already spinning).
 ***************************************************************************/
static int
spin_up_drive(Byte status[2])
{
	Byte cmd;

	/* first see if the drive is already spinning */
	cmd = SONY535_REQUEST_DRIVE_STATUS_1;
	if (do_sony_cmd(&cmd, 1, status, NULL, 0, 0) != 0)
		return TIME_OUT;
	if ((status[0] & SONY535_STATUS1_NOT_SPINNING) == 0)
		return 0;	/* its already spinning */

	/* otherwise, give the spin-up command */
	cmd = SONY535_SPIN_UP;
	return do_sony_cmd(&cmd, 1, status, NULL, 0, 0);
}

/*--------------------end of SONY CDU535 very specific ---------------------*/

/* Convert from an integer 0-99 to BCD */
static inline unsigned int
int_to_bcd(unsigned int val)
{
	int retval;

	retval = (val / 10) << 4;
	retval = retval | val % 10;
	return retval;
}


/* Convert from BCD to an integer from 0-99 */
static unsigned int
bcd_to_int(unsigned int bcd)
{
	return (((bcd >> 4) & 0x0f) * 10) + (bcd & 0x0f);
}


/*
 * Convert a logical sector value (like the OS would want to use for
 * a block device) to an MSF format.
 */
static void
log_to_msf(unsigned int log, Byte *msf)
{
	log = log + LOG_START_OFFSET;
	msf[0] = int_to_bcd(log / 4500);
	log = log % 4500;
	msf[1] = int_to_bcd(log / 75);
	msf[2] = int_to_bcd(log % 75);
}


/*
 * Convert an MSF format to a logical sector.
 */
static unsigned int
msf_to_log(Byte *msf)
{
	unsigned int log;


	log = bcd_to_int(msf[2]);
	log += bcd_to_int(msf[1]) * 75;
	log += bcd_to_int(msf[0]) * 4500;
	log = log - LOG_START_OFFSET;

	return log;
}


/*
 * Take in integer size value and put it into a buffer like
 * the drive would want to see a number-of-sector value.
 */
static void
size_to_buf(unsigned int size, Byte *buf)
{
	buf[0] = size / 65536;
	size = size % 65536;
	buf[1] = size / 256;
	buf[2] = size % 256;
}


/*
 * The OS calls this to perform a read or write operation to the drive.
 * Write obviously fail.  Reads to a read ahead of sony_buffer_size
 * bytes to help speed operations.  This especially helps since the OS
 * uses 1024 byte blocks and the drive uses 2048 byte blocks.  Since most
 * data access on a CD is done sequentially, this saves a lot of operations.
 */
static void
do_cdu535_request(void)
{
	unsigned int dev;
	unsigned int read_size;
	int  block;
	int  nsect;
	int  copyoff;
	int  spin_up_retry;
	Byte params[10];
	Byte status[2];
	Byte cmd[2];


	if (!sony_inuse) {
		cdu_open(NULL, NULL);
	}
	while (1) {
		/*
		 * The beginning here is stolen from the hard disk driver.  I hope
		 * its right.
		 */
		if (!(CURRENT) || CURRENT->dev < 0) {
			return;
		}
		INIT_REQUEST;
		dev = MINOR(CURRENT->dev);
		block = CURRENT->sector;
		nsect = CURRENT->nr_sectors;
		if (dev != 0) {
			end_request(0);
			continue;
		}
		switch (CURRENT->cmd) {
		case READ:
			/*
			 * If the block address is invalid or the request goes beyond the end of
			 * the media, return an error.
			 */

			if (sony_toc->lead_out_start_lba <= (block / 4)) {
				end_request(0);
				return;
			}
			if (sony_toc->lead_out_start_lba <= ((block + nsect) / 4)) {
				end_request(0);
				return;
			}
			while (0 < nsect) {
				/*
				 * If the requested sector is not currently in the read-ahead buffer,
				 * it must be read in.
				 */
				if ((block < sony_first_block) || (sony_last_block < block)) {
					sony_first_block = (block / 4) * 4;
					log_to_msf(block / 4, params);

					/*
					 * If the full read-ahead would go beyond the end of the media, trim
					 * it back to read just till the end of the media.
					 */
					if (sony_toc->lead_out_start_lba <= ((block / 4) + sony_buffer_sectors)) {
						sony_last_block = (sony_toc->lead_out_start_lba * 4) - 1;
						read_size = sony_toc->lead_out_start_lba - (block / 4);
					} else {
						sony_last_block = sony_first_block + (sony_buffer_sectors * 4) - 1;
						read_size = sony_buffer_sectors;
					}
					size_to_buf(read_size, &params[3]);

					/*
					 * Read the data.  If the drive was not spinning,
					 * spin it up and try once more.
					 */
					spin_up_retry = 0;
					for (;;) {
#if DEBUG > 1
						if (check_drive_status() != 0) {
							/* drive not ready */
							sony_first_block = -1;
							sony_last_block = -1;
							end_request(0);
							return;
						}
#endif
						if (0 <= seek_and_read_N_blocks(params, read_size,
									status, sony_buffer, (read_size * 2048)))
							break;
						if (!(status[0] & SONY535_STATUS1_NOT_SPINNING) ||
															spin_up_retry) {
							printk(CDU535_MESSAGE_NAME " Read error: 0x%.2x\n",
									status[0]);
							sony_first_block = -1;
							sony_last_block = -1;
							end_request(0);
							return;
						}
						printk(CDU535_MESSAGE_NAME
							" debug: calling spin up when reading data!\n");
						cmd[0] = SONY535_SPIN_UP;
						do_sony_cmd(cmd, 1, status, NULL, 0, 0);
						spin_up_retry = 1;
					}
				}
				/*
				 * The data is in memory now, copy it to the buffer and advance to the
				 * next block to read.
				 */
#ifndef MODULE
				copyoff = (block - sony_first_block) * 512;
				memcpy(CURRENT->buffer, sony_buffer + copyoff, 512);
#else
				copyoff = block - sony_first_block;
				memcpy(CURRENT->buffer,
					   sony_buffer[copyoff / 4] + 512 * (copyoff % 4), 512);
#endif

				block += 1;
				nsect -= 1;
				CURRENT->buffer += 512;
			}

			end_request(1);
			break;

		case WRITE:
			end_request(0);
			break;

		default:
			panic("Unknown SONY CD cmd");
		}
	}
}


/*
 * Read the table of contents from the drive and set sony_toc_read if
 * successful.
 */
static void
sony_get_toc(void)
{
	Byte status[2];
	if (!sony_toc_read) {
		/* do not call check_drive_status() from here since it can call this routine */
		if (request_toc_data(status, sony_toc) < 0)
			return;
		sony_toc->lead_out_start_lba = msf_to_log(sony_toc->lead_out_start_msf);
		sony_toc_read = 1;
	}
}


/*
 * Search for a specific track in the table of contents.  track is
 * passed in bcd format
 */
static int
find_track(int track)
{
	int i;
	int num_tracks;


	num_tracks = bcd_to_int(sony_toc->last_track_num) -
		bcd_to_int(sony_toc->first_track_num) + 1;
	for (i = 0; i < num_tracks; i++) {
		if (sony_toc->tracks[i].track == track) {
			return i;
		}
	}

	return -1;
}

/*
 * Read the subcode and put it int last_sony_subcode for future use.
 */
static int
read_subcode(void)
{
	Byte cmd = SONY535_REQUEST_SUB_Q_DATA;
	Byte status[2];
	int  dsc_status;

	if (check_drive_status() != 0)
		return -EIO;

	if ((dsc_status = do_sony_cmd(&cmd, 1, status, (Byte *) last_sony_subcode,
							   sizeof(struct s535_sony_subcode), 1)) != 0) {
		printk(CDU535_MESSAGE_NAME " error 0x%.2x, %d (read_subcode)\n",
				status[0], dsc_status);
		return -EIO;
	}
	return 0;
}


/*
 * Get the subchannel info like the CDROMSUBCHNL command wants to see it.  If
 * the drive is playing, the subchannel needs to be read (since it would be
 * changing).  If the drive is paused or completed, the subcode information has
 * already been stored, just use that.  The ioctl call wants things in decimal
 * (not BCD), so all the conversions are done.
 */
static int
sony_get_subchnl_info(long arg)
{
	struct cdrom_subchnl schi;


	/* Get attention stuff */
	if (check_drive_status() != 0)
		return -EIO;

	sony_get_toc();
	if (!sony_toc_read) {
		return -EIO;
	}
	verify_area(VERIFY_WRITE /* and read */ , (char *)arg, sizeof schi);

	memcpy_fromfs(&schi, (char *)arg, sizeof schi);

	switch (sony_audio_status) {
	case CDROM_AUDIO_PLAY:
		if (read_subcode() < 0) {
			return -EIO;
		}
		break;

	case CDROM_AUDIO_PAUSED:
	case CDROM_AUDIO_COMPLETED:
		break;

	case CDROM_AUDIO_NO_STATUS:
		schi.cdsc_audiostatus = sony_audio_status;
		memcpy_tofs((char *)arg, &schi, sizeof schi);
		return 0;
		break;

	case CDROM_AUDIO_INVALID:
	case CDROM_AUDIO_ERROR:
	default:
		return -EIO;
	}

	schi.cdsc_audiostatus = sony_audio_status;
	schi.cdsc_adr = last_sony_subcode->address;
	schi.cdsc_ctrl = last_sony_subcode->control;
	schi.cdsc_trk = bcd_to_int(last_sony_subcode->track_num);
	schi.cdsc_ind = bcd_to_int(last_sony_subcode->index_num);
	if (schi.cdsc_format == CDROM_MSF) {
		schi.cdsc_absaddr.msf.minute = bcd_to_int(last_sony_subcode->abs_msf[0]);
		schi.cdsc_absaddr.msf.second = bcd_to_int(last_sony_subcode->abs_msf[1]);
		schi.cdsc_absaddr.msf.frame = bcd_to_int(last_sony_subcode->abs_msf[2]);

		schi.cdsc_reladdr.msf.minute = bcd_to_int(last_sony_subcode->rel_msf[0]);
		schi.cdsc_reladdr.msf.second = bcd_to_int(last_sony_subcode->rel_msf[1]);
		schi.cdsc_reladdr.msf.frame = bcd_to_int(last_sony_subcode->rel_msf[2]);
	} else if (schi.cdsc_format == CDROM_LBA) {
		schi.cdsc_absaddr.lba = msf_to_log(last_sony_subcode->abs_msf);
		schi.cdsc_reladdr.lba = msf_to_log(last_sony_subcode->rel_msf);
	}
	memcpy_tofs((char *)arg, &schi, sizeof schi);
	return 0;
}


/*
 * The big ugly ioctl handler.
 */
static int
cdu_ioctl(struct inode *inode,
		  struct file *file,
		  unsigned int cmd,
		  unsigned long arg)
{
	unsigned int dev;
	Byte status[2];
	Byte cmd_buff[10], params[10];
	int  i, dsc_status;


	if (!inode) {
		return -EINVAL;
	}
	dev = MINOR(inode->i_rdev) >> 6;
	if (dev != 0) {
		return -EINVAL;
	}
	if (check_drive_status() != 0)
		return -EIO;

	switch (cmd) {
	case CDROMSTART:			/* Spin up the drive */
		if (spin_up_drive(status) < 0) {
			printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMSTART)\n",
					status[0]);
			return -EIO;
		}
		return 0;
		break;

	case CDROMSTOP:			/* Spin down the drive */
		cmd_buff[0] = SONY535_HOLD;
		do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0);

		/*
		 * Spin the drive down, ignoring the error if the disk was
		 * already not spinning.
		 */
		sony_audio_status = CDROM_AUDIO_NO_STATUS;
		cmd_buff[0] = SONY535_SPIN_DOWN;
		dsc_status = do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0);
		if (((dsc_status < 0) && (dsc_status != BAD_STATUS)) ||
			((status[0] & ~(SONY535_STATUS1_NOT_SPINNING)) != 0)) {
			printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMSTOP)\n",
					status[0]);
			return -EIO;
		}
		return 0;
		break;

	case CDROMPAUSE:			/* Pause the drive */
		cmd_buff[0] = SONY535_HOLD;		/* CDU-31 driver uses AUDIO_STOP, not pause */
		if (do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0) != 0) {
			printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMPAUSE)\n",
					status[0]);
			return -EIO;
		}
		/* Get the current position and save it for resuming */
		if (read_subcode() < 0) {
			return -EIO;
		}
		cur_pos_msf[0] = last_sony_subcode->abs_msf[0];
		cur_pos_msf[1] = last_sony_subcode->abs_msf[1];
		cur_pos_msf[2] = last_sony_subcode->abs_msf[2];
		sony_audio_status = CDROM_AUDIO_PAUSED;
		return 0;
		break;

	case CDROMRESUME:			/* Start the drive after being paused */
		set_drive_mode(SONY535_AUDIO_DRIVE_MODE, status);

		if (sony_audio_status != CDROM_AUDIO_PAUSED) {
			return -EINVAL;
		}
		spin_up_drive(status);

		/* Start the drive at the saved position. */
		cmd_buff[0] = SONY535_PLAY_AUDIO;
		cmd_buff[1] = 0;		/* play back starting at this address */
		cmd_buff[2] = cur_pos_msf[0];
		cmd_buff[3] = cur_pos_msf[1];
		cmd_buff[4] = cur_pos_msf[2];
		cmd_buff[5] = SONY535_PLAY_AUDIO;
		cmd_buff[6] = 2;		/* set ending address */
		cmd_buff[7] = final_pos_msf[0];
		cmd_buff[8] = final_pos_msf[1];
		cmd_buff[9] = final_pos_msf[2];
		if ((do_sony_cmd(cmd_buff, 5, status, NULL, 0, 0) != 0) ||
			(do_sony_cmd(cmd_buff + 5, 5, status, NULL, 0, 0) != 0)) {
			printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMRESUME)\n",
					status[0]);
			return -EIO;
		}
		sony_audio_status = CDROM_AUDIO_PLAY;
		return 0;
		break;

	case CDROMPLAYMSF:			/* Play starting at the given MSF address. */
		verify_area(VERIFY_READ, (char *)arg, 6);
		spin_up_drive(status);
		set_drive_mode(SONY535_AUDIO_DRIVE_MODE, status);
		memcpy_fromfs(params, (void *)arg, 6);

		/* The parameters are given in int, must be converted */
		for (i = 0; i < 3; i++) {
			cmd_buff[2 + i] = int_to_bcd(params[i]);
			cmd_buff[7 + i] = int_to_bcd(params[i + 3]);
		}
		cmd_buff[0] = SONY535_PLAY_AUDIO;
		cmd_buff[1] = 0;		/* play back starting at this address */
		/* cmd_buff[2-4] are filled in for loop above */
		cmd_buff[5] = SONY535_PLAY_AUDIO;
		cmd_buff[6] = 2;		/* set ending address */
		/* cmd_buff[7-9] are filled in for loop above */
		if ((do_sony_cmd(cmd_buff, 5, status, NULL, 0, 0) != 0) ||
			(do_sony_cmd(cmd_buff + 5, 5, status, NULL, 0, 0) != 0)) {
			printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMPLAYMSF)\n",
					status[0]);
			return -EIO;
		}
		/* Save the final position for pauses and resumes */
		final_pos_msf[0] = cmd_buff[7];
		final_pos_msf[1] = cmd_buff[8];
		final_pos_msf[2] = cmd_buff[9];
		sony_audio_status = CDROM_AUDIO_PLAY;
		return 0;
		break;

	case CDROMREADTOCHDR:		/* Read the table of contents header */
		{
			struct cdrom_tochdr *hdr;
			struct cdrom_tochdr loc_hdr;

			sony_get_toc();
			if (!sony_toc_read)
				return -EIO;
			hdr = (struct cdrom_tochdr *)arg;
			verify_area(VERIFY_WRITE, hdr, sizeof *hdr);
			loc_hdr.cdth_trk0 = bcd_to_int(sony_toc->first_track_num);
			loc_hdr.cdth_trk1 = bcd_to_int(sony_toc->last_track_num);
			memcpy_tofs(hdr, &loc_hdr, sizeof *hdr);
		}
		return 0;
		break;

	case CDROMREADTOCENTRY:	/* Read a given table of contents entry */
		{
			struct cdrom_tocentry *entry;
			struct cdrom_tocentry loc_entry;
			int  track_idx;
			Byte *msf_val = NULL;

			sony_get_toc();
			if (!sony_toc_read) {
				return -EIO;
			}
			entry = (struct cdrom_tocentry *)arg;
			verify_area(VERIFY_WRITE /* and read */ , entry, sizeof *entry);

			memcpy_fromfs(&loc_entry, entry, sizeof loc_entry);

			/* Lead out is handled separately since it is special. */
			if (loc_entry.cdte_track == CDROM_LEADOUT) {
				loc_entry.cdte_adr = 0 /*sony_toc->address2 */ ;
				loc_entry.cdte_ctrl = sony_toc->control2;
				msf_val = sony_toc->lead_out_start_msf;
			} else {
				track_idx = find_track(int_to_bcd(loc_entry.cdte_track));
				if (track_idx < 0)
					return -EINVAL;
				loc_entry.cdte_adr = 0 /*sony_toc->tracks[track_idx].address */ ;
				loc_entry.cdte_ctrl = sony_toc->tracks[track_idx].control;
				msf_val = sony_toc->tracks[track_idx].track_start_msf;
			}

			/* Logical buffer address or MSF format requested? */
			if (loc_entry.cdte_format == CDROM_LBA) {
				loc_entry.cdte_addr.lba = msf_to_log(msf_val);
			} else if (loc_entry.cdte_format == CDROM_MSF) {
				loc_entry.cdte_addr.msf.minute = bcd_to_int(*msf_val);
				loc_entry.cdte_addr.msf.second = bcd_to_int(*(msf_val + 1));
				loc_entry.cdte_addr.msf.frame = bcd_to_int(*(msf_val + 2));
			}
			memcpy_tofs(entry, &loc_entry, sizeof *entry);
		}
		return 0;
		break;

	case CDROMPLAYTRKIND:		/* Play a track.  This currently ignores index. */
		{
			struct cdrom_ti ti;
			int track_idx;

			sony_get_toc();
			if (!sony_toc_read)
				return -EIO;
			verify_area(VERIFY_READ, (char *)arg, sizeof ti);

			memcpy_fromfs(&ti, (char *)arg, sizeof ti);
			if ((ti.cdti_trk0 < sony_toc->first_track_num)
				|| (sony_toc->last_track_num < ti.cdti_trk0)
				|| (ti.cdti_trk1 < ti.cdti_trk0)) {
				return -EINVAL;
			}
			track_idx = find_track(int_to_bcd(ti.cdti_trk0));
			if (track_idx < 0)
				return -EINVAL;
			params[1] = sony_toc->tracks[track_idx].track_start_msf[0];
			params[2] = sony_toc->tracks[track_idx].track_start_msf[1];
			params[3] = sony_toc->tracks[track_idx].track_start_msf[2];
			/*
			 * If we want to stop after the last track, use the lead-out
			 * MSF to do that.
			 */
			if (bcd_to_int(sony_toc->last_track_num) <= ti.cdti_trk1) {
				log_to_msf(msf_to_log(sony_toc->lead_out_start_msf) - 1,
						   &(params[4]));
			} else {
				track_idx = find_track(int_to_bcd(ti.cdti_trk1 + 1));
				if (track_idx < 0)
					return -EINVAL;
				log_to_msf(msf_to_log(sony_toc->tracks[track_idx].track_start_msf) - 1,
						   &(params[4]));
			}
			params[0] = 0x03;

			spin_up_drive(status);

			set_drive_mode(SONY535_AUDIO_DRIVE_MODE, status);

			/* Start the drive at the saved position. */
			cmd_buff[0] = SONY535_PLAY_AUDIO;
			cmd_buff[1] = 0;	/* play back starting at this address */
			cmd_buff[2] = params[1];
			cmd_buff[3] = params[2];
			cmd_buff[4] = params[3];
			cmd_buff[5] = SONY535_PLAY_AUDIO;
			cmd_buff[6] = 2;	/* set ending address */
			cmd_buff[7] = params[4];
			cmd_buff[8] = params[5];
			cmd_buff[9] = params[6];
			if ((do_sony_cmd(cmd_buff, 5, status, NULL, 0, 0) != 0) ||
				(do_sony_cmd(cmd_buff + 5, 5, status, NULL, 0, 0) != 0)) {
				printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMPLAYTRKIND)\n",
						status[0]);
				printk("... Params: %x %x %x %x %x %x %x\n",
						params[0], params[1], params[2],
						params[3], params[4], params[5], params[6]);
				return -EIO;
			}
			/* Save the final position for pauses and resumes */
			final_pos_msf[0] = params[4];
			final_pos_msf[1] = params[5];
			final_pos_msf[2] = params[6];
			sony_audio_status = CDROM_AUDIO_PLAY;
			return 0;
		}

	case CDROMSUBCHNL:			/* Get subchannel info */
		return sony_get_subchnl_info(arg);

	case CDROMVOLCTRL:			/* Volume control.  What volume does this change, anyway? */
		{
			struct cdrom_volctrl volctrl;

			verify_area(VERIFY_READ, (char *)arg, sizeof volctrl);

			memcpy_fromfs(&volctrl, (char *)arg, sizeof volctrl);
			cmd_buff[0] = SONY535_SET_VOLUME;
			cmd_buff[1] = volctrl.channel0;
			cmd_buff[2] = volctrl.channel1;
			if (do_sony_cmd(cmd_buff, 3, status, NULL, 0, 0) != 0) {
				printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMVOLCTRL)\n",
						status[0]);
				return -EIO;
			}
		}
		return 0;

	case CDROMEJECT:			/* Eject the drive */
		cmd_buff[0] = SONY535_STOP;
		do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0);
		cmd_buff[0] = SONY535_SPIN_DOWN;
		do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0);

		sony_audio_status = CDROM_AUDIO_INVALID;
		cmd_buff[0] = SONY535_EJECT_CADDY;
		if (do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0) != 0) {
			printk(CDU535_MESSAGE_NAME " error 0x%.2x (CDROMEJECT)\n",
					status[0]);
			return -EIO;
		}
		return 0;
		break;

	default:
		return -EINVAL;
	}
}


/*
 * Open the drive for operations.  Spin the drive up and read the table of
 * contents if these have not already been done.
 */
static int
cdu_open(struct inode *inode,
		 struct file *filp)
{
	Byte status[2], cmd_buff[2];


	if (sony_inuse)
		return -EBUSY;
	if (check_drive_status() != 0)
		return -EIO;
	sony_inuse = 1;
	MOD_INC_USE_COUNT;

	if (spin_up_drive(status) != 0) {
		printk(CDU535_MESSAGE_NAME " error 0x%.2x (cdu_open, spin up)\n",
				status[0]);
		sony_inuse = 0;
		MOD_DEC_USE_COUNT;
		return -EIO;
	}
	sony_get_toc();
	if (!sony_toc_read) {
		cmd_buff[0] = SONY535_SPIN_DOWN;
		do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0);
		sony_inuse = 0;
		MOD_DEC_USE_COUNT;
		return -EIO;
	}
	if (inode) {
		check_disk_change(inode->i_rdev);
	}
	sony_usage++;

#ifdef LOCK_DOORS
	/* disable the eject button while mounted */
	cmd_buff[0] = SONY535_DISABLE_EJECT_BUTTON;
	do_sony_cmd(cmd_buff, 1, status, NULL, 0, 0);
#endif

	return 0;
}


/*
 * Close the drive.  Spin it down if no task is using it.  The spin
 * down will fail if playing audio, so audio play is OK.
 */
static void
cdu_release(struct inode *inode,
			struct file *filp)
{
	Byte status[2], cmd_no;

	sony_inuse = 0;
	MOD_DEC_USE_COUNT;

	if (0 < sony_usage) {
		sony_usage--;
	}
	if (sony_usage == 0) {
		sync_dev(inode->i_rdev);
		check_drive_status();

		if (sony_audio_status != CDROM_AUDIO_PLAY) {
			cmd_no = SONY535_SPIN_DOWN;
			do_sony_cmd(&cmd_no, 1, status, NULL, 0, 0);
		}
#ifdef LOCK_DOORS
		/* enable the eject button after umount */
		cmd_no = SONY535_ENABLE_EJECT_BUTTON;
		do_sony_cmd(&cmd_no, 1, status, NULL, 0, 0);
#endif
	}
}


static struct file_operations cdu_fops =
{
	NULL,						/* lseek - default */
	block_read,					/* read - general block-dev read */
	block_write,				/* write - general block-dev write */
	NULL,						/* readdir - bad */
	NULL,						/* select */
	cdu_ioctl,					/* ioctl */
	NULL,						/* mmap */
	cdu_open,					/* open */
	cdu_release,				/* release */
	NULL,						/* fsync */
	NULL,						/* fasync */
	cdu535_check_media_change,	/* check media change */
	NULL						/* revalidate */
};

/*
 * Initialize the driver.
 */
#ifndef MODULE
unsigned long
sony535_init(unsigned long mem_start, unsigned long mem_end)
#else
int
init_module(void)
#endif
{
	struct s535_sony_drive_config drive_config;
	Byte cmd_buff[3];
	Byte ret_buff[2];
	Byte status[2];
	int  retry_count;
	int  tmp_irq;
#ifdef MODULE
	int  i;
#endif

	/* Setting the base I/O address to 0xffff will disable it. */
	if (sony535_cd_base_io == 0xffff)
		goto bail;

	/* Set up all the register locations */
	result_reg = sony535_cd_base_io;
	command_reg = sony535_cd_base_io;
	data_reg = sony535_cd_base_io + 1;
	read_status_reg = sony535_cd_base_io + 2;
	select_unit_reg = sony535_cd_base_io + 3;

#ifndef USE_IRQ
	sony535_irq_used = 0;	/* polling only until this is ready... */
#endif
	/* we need to poll until things get initialized */
	tmp_irq = sony535_irq_used;
	sony535_irq_used = 0;

#if DEBUG > 0
	printk(CDU535_MESSAGE_NAME ": probing base address %03X\n",
			sony535_cd_base_io);
#endif
	if (check_region(sony535_cd_base_io,4)) {
		printk(CDU535_MESSAGE_NAME ": my base address is not free!\n");
#ifndef MODULE
		return mem_start;
#else
		return -EIO;
#endif
	}
	/* look for the CD-ROM, follows the procedure in the DOS driver */
	inb(select_unit_reg);
	retry_count = jiffies + 2 * HZ;
	while (jiffies < retry_count)
		sony_sleep();			/* wait for 40 18 Hz ticks (from DOS driver) */
	inb(result_reg);

	outb(0, read_status_reg);	/* does a reset? */
	retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
	while (jiffies < retry_count) {
		select_unit(0);
		if (inb(result_reg) != 0xff)
			break;
		sony_sleep();
	}

	if ((jiffies < retry_count) && (check_drive_status() != TIME_OUT)) {
		/* CD-ROM drive responded --  get the drive configuration */
		cmd_buff[0] = SONY535_INQUIRY;
		if (do_sony_cmd(cmd_buff, 1, status,
						(Byte *)&drive_config, 28, 1) == 0) {
			/* was able to get the configuration,
			 * set drive mode as rest of init
			 */
#if DEBUG > 0
			/* 0x50 == CADDY_NOT_INSERTED | NOT_SPINNING */
			if ( (status[0] & 0x7f) != 0 && (status[0] & 0x7f) != 0x50 )
				printk(CDU535_MESSAGE_NAME
						"Inquiry command returned status = 0x%x\n", status[0]);
#endif
			/* now ready to use interrupts, if available */
			sony535_irq_used = tmp_irq;
#ifndef MODULE
/* This code is not in MODULEs by default, since the autoirq stuff might
 * not be in the module-accessible symbol table.
 */
			/* A negative sony535_irq_used will attempt an autoirq. */
			if (sony535_irq_used < 0) {
				autoirq_setup(0);
				enable_interrupts();
				outb(0, read_status_reg);	/* does a reset? */
				sony535_irq_used = autoirq_report(10);
				disable_interrupts();
			}
#endif
			if (sony535_irq_used > 0) {
			    if (request_irq(sony535_irq_used, cdu535_interrupt,
								SA_INTERRUPT, CDU535_HANDLE)) {
					printk("Unable to grab IRQ%d for the " CDU535_MESSAGE_NAME
							" driver; polling instead.\n", sony535_irq_used);
					sony535_irq_used = 0;
				}
			}
			cmd_buff[0] = SONY535_SET_DRIVE_MODE;
			cmd_buff[1] = 0x0;	/* default audio */
			if (do_sony_cmd(cmd_buff, 2, status, ret_buff, 1, 1) == 0) {
				/* set the drive mode successful, we are set! */
				sony_buffer_size = SONY535_BUFFER_SIZE;
				sony_buffer_sectors = sony_buffer_size / 2048;

				printk(CDU535_MESSAGE_NAME " I/F CDROM : %8.8s %16.16s %4.4s",
					   drive_config.vendor_id,
					   drive_config.product_id,
					   drive_config.product_rev_level);
				printk("  base address %03X, ", sony535_cd_base_io);
				if (tmp_irq > 0)
					printk("IRQ%d, ", tmp_irq);
				printk("using %d byte buffer\n", sony_buffer_size);

				if (register_blkdev(MAJOR_NR, CDU535_HANDLE, &cdu_fops)) {
					printk("Unable to get major %d for %s\n",
							MAJOR_NR, CDU535_MESSAGE_NAME);
#ifndef MODULE
					return mem_start;
#else
					return -EIO;
#endif
				}
				blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
				read_ahead[MAJOR_NR] = 8;	/* 8 sector (4kB) read-ahead */

#ifndef MODULE
				sony_toc = (struct s535_sony_toc *)mem_start;
				mem_start += sizeof *sony_toc;
				last_sony_subcode = (struct s535_sony_subcode *)mem_start;
				mem_start += sizeof *last_sony_subcode;
				sony_buffer = (Byte *)mem_start;
				mem_start += sony_buffer_size;

#else /* MODULE */
				sony_toc = (struct s535_sony_toc *)
					kmalloc(sizeof *sony_toc, GFP_KERNEL);
				last_sony_subcode = (struct s535_sony_subcode *)
					kmalloc(sizeof *last_sony_subcode, GFP_KERNEL);
				sony_buffer = (Byte **)
					kmalloc(4 * sony_buffer_sectors, GFP_KERNEL);
				for (i = 0; i < sony_buffer_sectors; i++)
					sony_buffer[i] = (Byte *)kmalloc(2048, GFP_KERNEL);
#endif /* MODULE */
				initialized = 1;
			}
		}
	}

	if (!initialized) {
		printk("Did not find a " CDU535_MESSAGE_NAME " drive\n");
#ifdef MODULE
		return -EIO;
#endif
	} else {
		request_region(sony535_cd_base_io, 4, CDU535_HANDLE);
	}
bail:
#ifndef MODULE
	return mem_start;
#else
	return 0;
#endif
}

#ifndef MODULE
/*
 * accept "kernel command line" parameters
 * (added by emoenke@gwdg.de)
 *
 * use: tell LILO:
 *                 sonycd535=0x320
 *
 * the address value has to be the existing CDROM port address.
 */
void
sonycd535_setup(char *strings, int *ints)
{
	/* if IRQ change and default io base desired,
	 * then call with io base of 0
	 */
	if (ints[0] > 0)
		if (ints[0] != 0)
			sony535_cd_base_io = ints[1];
	if (ints[0] > 1)
		sony535_irq_used = ints[2];
	if ((strings != NULL) && (*strings != '\0'))
		printk(CDU535_MESSAGE_NAME
				": Warning: Unknown interface type: %s\n", strings);
}

#else /* MODULE */

void
cleanup_module(void)
{
	int i;
	if (MOD_IN_USE) {
		printk(CDU535_HANDLE " module in use, cannot remove\n");
		return;
	}
	release_region(sony535_cd_base_io, 4);
	for (i = 0; i < sony_buffer_sectors; i++)
		kfree_s(sony_buffer[i], 2048);
	kfree_s(sony_buffer, 4 * sony_buffer_sectors);
	kfree_s(last_sony_subcode, sizeof *last_sony_subcode);
	kfree_s(sony_toc, sizeof *sony_toc);
	if (unregister_blkdev(MAJOR_NR, CDU535_HANDLE) == -EINVAL)
		printk("Uh oh, couldn't unregister " CDU535_HANDLE "\n");
	else
		printk(CDU535_HANDLE " module released\n");
}
#endif	/* MODULE */

#endif	/* CONFIG_CDU535 */
