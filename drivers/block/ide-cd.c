/*
 * linux/drivers/block/ide-cd.c
 *
 * 1.00  Oct 31, 1994 -- Initial version.
 * 1.01  Nov  2, 1994 -- Fixed problem with starting request in
 *                       cdrom_check_status.
 * 1.03  Nov 25, 1994 -- leaving unmask_intr[] as a user-setting (as for disks)
 * (from mlord)       -- minor changes to cdrom_setup()
 *                    -- renamed ide_dev_s to ide_drive_t, enable irq on command
 * 2.00  Nov 27, 1994 -- Generalize packet command interface;
 *                       add audio ioctls.
 * 2.01  Dec  3, 1994 -- Rework packet command interface to handle devices
 *                       which send an interrupt when ready for a command.
 * 2.02  Dec 11, 1994 -- Cache the TOC in the driver.
 *                       Don't use SCMD_PLAYAUDIO_TI; it's not included
 *                       in the current version of ATAPI.
 *                       Try to use LBA instead of track or MSF addressing
 *                       when possible.
 *                       Don't wait for READY_STAT.
 * 2.03  Jan 10, 1995 -- Rewrite block read routines to handle block sizes
 *                       other than 2k and to move multiple sectors in a
 *                       single transaction.
 * 2.04  Apr 21, 1995 -- Add work-around for Creative Labs CD220E drives.
 *                       Thanks to Nick Saw <cwsaw@pts7.pts.mot.com> for
 *                       help in figuring this out.  Ditto for Acer and
 *                       Aztech drives, which seem to have the same problem.
 * 2.04b May 30, 1995 -- Fix to match changes in ide.c version 3.16 -ml
 * 2.05  Jun  8, 1995 -- Don't attempt to retry after an illegal request
 *                        or data protect error.
 *                       Use HWIF and DEV_HWIF macros as in ide.c.
 *                       Always try to do a request_sense after
 *                        a failed command.
 *                       Include an option to give textual descriptions
 *                        of ATAPI errors.
 *                       Fix a bug in handling the sector cache which
 *                        showed up if the drive returned data in 512 byte
 *                        blocks (like Pioneer drives).  Thanks to
 *                        Richard Hirst <srh@gpt.co.uk> for diagnosing this.
 *                       Properly supply the page number field in the
 *                        MODE_SELECT command.
 *                       PLAYAUDIO12 is broken on the Aztech; work around it.
 * 2.05x Aug 11, 1995 -- lots of data structure renaming/restructuring in ide.c
 *                       (my apologies to Scott, but now ide-cd.c is independent)
 *
 * FIX ME!!  A day-one bug exists when the ide.c "serialize" option is used.
 * For this to always work correctly, ide_set_handler() must be called
 * *just before* the final trigger is given to the drive (to cause it to go
 * off and get data and then interrupt us again).  Otherwise, we may get the
 * interrupt before set_handler() has actually run, resulting in "unexpected_intr".
 *
 * This can only happen in scenarios where we handle a "final" interrupt
 * for one IDE port on, say irq14, and then initiate a new request for the
 * other port on, say irq15, from the irq14 interrupt handler.  If we are
 * running with "unmask" on, or have done sti(), then Whammo -- we're exposed.
 *
 * Places where this needs fixing have been identified in the code with "BUG".
 * -ml  August 11, 1995
 *
 *
 * ATAPI cd-rom driver.  To be used with ide.c.
 *
 * Copyright (C) 1994, 1995  scott snyder  <snyder@fnald0.fnal.gov>
 * May be copied or modified under the terms of the GNU General Public License
 * (../../COPYING).
 */


/***************************************************************************/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/cdrom.h>
#include <asm/irq.h>

#define _IDE_CD_C	/* used in blk.h */
#include "ide.h"

/* Turn this on to have the driver print out the meanings of the
   ATAPI error codes.  This will use up additional kernel-space
   memory, though. */

#ifndef VERBOSE_IDE_CD_ERRORS
#define VERBOSE_IDE_CD_ERRORS 0
#endif

#define SECTOR_SIZE 512
#define SECTOR_BITS 9
#define SECTORS_PER_FRAME (CD_FRAMESIZE / SECTOR_SIZE)

#define MIN(a,b) ((a) < (b) ? (a) : (b))

#if 1	/* "old" method */
#define OUT_WORDS(b,n)  outsw (IDE_DATA_REG, (b), (n))
#define IN_WORDS(b,n)   insw  (IDE_DATA_REG, (b), (n))
#else	/* "new" method -- should really fix each instance instead of this */
#define OUT_WORDS(b,n)	output_ide_data(drive,b,(n)/2)
#define IN_WORDS(b,n)	input_ide_data(drive,b,(n)/2)
#endif

/* special command codes for strategy routine. */
#define PACKET_COMMAND 4315
#define REQUEST_SENSE_COMMAND 4316

#define WIN_PACKETCMD 0xa0  /* Send a packet command. */

/* Some ATAPI command opcodes (just like SCSI).
   (Some other cdrom-specific codes are in cdrom.h.) */
#define TEST_UNIT_READY         0x00
#define REQUEST_SENSE           0x03
#define START_STOP              0x1b
#define ALLOW_MEDIUM_REMOVAL    0x1e
#define READ_10                 0x28
#define MODE_SENSE_10           0x5a
#define MODE_SELECT_10          0x55


/* ATAPI sense keys (mostly copied from scsi.h). */

#define NO_SENSE                0x00
#define RECOVERED_ERROR         0x01
#define NOT_READY               0x02
#define MEDIUM_ERROR            0x03
#define HARDWARE_ERROR          0x04
#define ILLEGAL_REQUEST         0x05
#define UNIT_ATTENTION          0x06
#define DATA_PROTECT            0x07
#define ABORTED_COMMAND         0x0b
#define MISCOMPARE              0x0e

/* We want some additional flags for cd-rom drives.
   To save space in the ide_drive_t struct, use one of the fields which
   doesn't make sense for cd-roms -- `bios_sect'. */

struct ide_cd_flags {
  unsigned drq_interrupt : 1; /* Device sends an interrupt when ready
                                 for a packet command. */
  unsigned no_playaudio12: 1; /* The PLAYAUDIO12 command is not supported. */

  unsigned media_changed : 1; /* Driver has noticed a media change. */
  unsigned toc_valid     : 1; /* Saved TOC information is current. */
  unsigned no_lba_toc    : 1; /* Drive cannot return TOC info in LBA format. */
  unsigned msf_as_bcd    : 1; /* Drive uses BCD in PLAYAUDIO_MSF. */
  unsigned reserved : 2;
};

#define CDROM_FLAGS(drive) ((struct ide_cd_flags *)&((drive)->bios_sect))

#define SECTOR_BUFFER_SIZE CD_FRAMESIZE


/****************************************************************************
 * Descriptions of ATAPI error codes.
 */

#define ARY_LEN(a) ((sizeof(a) / sizeof(a[0])))

#if VERBOSE_IDE_CD_ERRORS

/* From Table 124 of the ATAPI 1.2 spec. */

char *sense_key_texts[16] = {
  "No sense data",
  "Recovered error",
  "Not ready",
  "Medium error",
  "Hardware error",
  "Illegal request",
  "Unit attention",
  "Data protect",
  "(reserved)",
  "(reserved)",
  "(reserved)",
  "Aborted command",
  "(reserved)",
  "(reserved)",
  "Miscompare",
  "(reserved)",
};


/* From Table 125 of the ATAPI 1.2 spec. */

struct {
  short asc_ascq;
  char *text;
} sense_data_texts[] = {
  { 0x0000, "No additional sense information" },
  { 0x0011, "Audio play operation in progress" },
  { 0x0012, "Audio play operation paused" },
  { 0x0013, "Audio play operation successfully completed" },
  { 0x0014, "Audio play operation stopped due to error" },
  { 0x0015, "No current audio status to return" },

  { 0x0200, "No seek complete" },

  { 0x0400, "Logical unit not ready - cause not reportable" },
  { 0x0401, "Logical unit not ready - in progress (sic) of becoming ready" },
  { 0x0402, "Logical unit not ready - initializing command required" },
  { 0x0403, "Logical unit not ready - manual intervention required" },

  { 0x0600, "No reference position found" },

  { 0x0900, "Track following error" },
  { 0x0901, "Tracking servo failure" },
  { 0x0902, "Focus servo failure" },
  { 0x0903, "Spindle servo failure" },

  { 0x1100, "Unrecovered read error" },
  { 0x1106, "CIRC unrecovered error" },

  { 0x1500, "Random positioning error" },
  { 0x1501, "Mechanical positioning error" },
  { 0x1502, "Positioning error detected by read of medium" },

  { 0x1700, "Recovered data with no error correction applied" },
  { 0x1701, "Recovered data with retries" },
  { 0x1702, "Recovered data with positive head offset" },
  { 0x1703, "Recovered data with negative head offset" },
  { 0x1704, "Recovered data with retries and/or CIRC applied" },
  { 0x1705, "Recovered data using previous sector ID" },

  { 0x1800, "Recovered data with error correction applied" },
  { 0x1801, "Recovered data with error correction and retries applied" },
  { 0x1802, "Recovered data - the data was auto-reallocated" },
  { 0x1803, "Recovered data with CIRC" },
  { 0x1804, "Recovered data with L-EC" },
  { 0x1805, "Recovered data - recommend reassignment" },
  { 0x1806, "Recovered data - recommend rewrite" },

  { 0x1a00, "Parameter list length error" },

  { 0x2000, "Invalid command operation code" },

  { 0x2100, "Logical block address out of range" },

  { 0x2400, "Invalid field in command packet" },

  { 0x2600, "Invalid field in parameter list" },
  { 0x2601, "Parameter not supported" },
  { 0x2602, "Parameter value invalid" },
  { 0x2603, "Threshold parameters not supported" },

  { 0x2800, "Not ready to ready transition, medium may have changed" },

  { 0x2900, "Power on, reset or bus device reset occurred" },

  { 0x2a00, "Parameters changed" },
  { 0x2a01, "Mode parameters changed" },

  { 0x3000, "Incompatible medium installed" },
  { 0x3001, "Cannot read medium - unknown format" },
  { 0x3002, "Cannot read medium - incompatible format" },

  { 0x3700, "Rounded parameter" },

  { 0x3900, "Saving parameters not supported" },

  { 0x3a00, "Medium not present" },

  { 0x3f00, "ATAPI CD-ROM drive operating conditions have changed" },
  { 0x3f01, "Microcode has been changed" },
  { 0x3f02, "Changed operating definition" },
  { 0x3f03, "Inquiry data has changed" },

  { 0x4000, "Diagnostic failure on component (ASCQ)" },

  { 0x4400, "Internal ATAPI CD-ROM drive failure" },

  { 0x4e00, "Overlapped commands attempted" },

  { 0x5300, "Media load or eject failed" },
  { 0x5302, "Medium removal prevented" },

  { 0x5700, "Unable to recover table of contents" },

  { 0x5a00, "Operator request or state change input (unspecified)" },
  { 0x5a01, "Operator medium removal request" },

  { 0x5b00, "Threshold condition met" },

  { 0x5c00, "Status change" },

  { 0x6300, "End of user area encountered on this track" },

  { 0x6400, "Illegal mode for this track" },

  { 0xbf00, "Loss of streaming" },
};
#endif



/****************************************************************************
 * Generic packet command support routines.
 */


static
void cdrom_analyze_sense_data (ide_drive_t *drive, 
			       struct atapi_request_sense *reqbuf,
			       struct packet_command *failed_command)
{
  /* Don't print not ready or unit attention errors for READ_SUBCHANNEL.
     Workman (and probably other programs) uses this command to poll
     the drive, and we don't want to fill the syslog with useless errors. */
  if (failed_command &&
      failed_command->c[0] == SCMD_READ_SUBCHANNEL &&
      (reqbuf->sense_key == 2 || reqbuf->sense_key == 6))
    return;

#if VERBOSE_IDE_CD_ERRORS
  {
    int i;
    char *s;
    char buf[80];

    printk ("ATAPI device %s:\n", drive->name);

    printk ("  Error code: %x\n", reqbuf->error_code);

    if (reqbuf->sense_key >= 0 &&
	reqbuf->sense_key < ARY_LEN (sense_key_texts))
      s = sense_key_texts[reqbuf->sense_key];
    else
      s = "(bad sense key)";

    printk ("  Sense key: %x - %s\n", reqbuf->sense_key, s);

    if (reqbuf->asc == 0x40) {
      sprintf (buf, "Diagnostic failure on component %x", reqbuf->ascq);
      s = buf;
    }

    else {
      int lo, hi;
      int key = (reqbuf->asc << 8);
      if ( ! (reqbuf->ascq >= 0x80 && reqbuf->ascq <= 0xdd) )
	key |= reqbuf->ascq;

      lo = 0;
      hi = ARY_LEN (sense_data_texts);
      s = NULL;

      while (hi > lo) {
	int mid = (lo + hi) / 2;
	if (sense_data_texts[mid].asc_ascq == key) {
	  s = sense_data_texts[mid].text;
	  break;
	}
	else if (sense_data_texts[mid].asc_ascq > key)
	  hi = mid;
	else
	  lo = mid+1;
      }
    }

    if (s == NULL) {
      if (reqbuf->asc > 0x80)
	s = "(vendor-specific error)";
      else
	s = "(reserved error code)";
    }

    printk ("  Additional sense data: %x, %x  - %s\n",
	    reqbuf->asc, reqbuf->ascq, s);

    if (failed_command != NULL) {
      printk ("  Failed packet command: ");
      for (i=0; i<sizeof (failed_command->c); i++)
	printk ("%02x ", failed_command->c[i]);
      printk ("\n");
    }
  }

#else
  printk ("%s: code: %x  key: %x  asc: %x  ascq: %x\n",
	  drive->name,
	  reqbuf->error_code, reqbuf->sense_key, reqbuf->asc, reqbuf->ascq);
#endif
}


/* Fix up a possibly partially-processed request so that we can
   start it over entirely, or even put it back on the request queue. */
static void restore_request (struct request *rq)
{
  if (rq->buffer != rq->bh->b_data)
    {
      int n = (rq->buffer - rq->bh->b_data) / SECTOR_SIZE;
      rq->buffer = rq->bh->b_data;
      rq->nr_sectors += n;
      rq->sector -= n;
    }
  rq->current_nr_sectors = rq->bh->b_size >> SECTOR_BITS;
}


static void cdrom_queue_request_sense (ide_drive_t *drive)
{
  struct request *rq;
  struct packet_command *pc;
  struct atapi_request_sense *reqbuf;
  unsigned long flags;

  int major = HWIF(drive)->major;

  save_flags (flags);
  cli ();  /* safety */

  rq = HWGROUP(drive)->rq;

  /* If we're processing a request, put it back on the request queue. */
  if (rq != NULL)
    {
      restore_request (rq);
      rq->next = blk_dev[major].current_request;
      blk_dev[major].current_request = rq;
      HWGROUP(drive)->rq = NULL;
    }

  restore_flags (flags);

  /* Make up a new request to retrieve sense information. */
  reqbuf = &drive->cdrom_info.sense_data;

  pc = &HWIF(drive)->request_sense_pc;
  memset (pc, 0, sizeof (*pc));

  pc->c[0] = REQUEST_SENSE;
  pc->c[4] = sizeof (*reqbuf);
  pc->buffer = (char *)reqbuf;
  pc->buflen = sizeof (*reqbuf);
  
  rq = &HWIF(drive)->request_sense_request;
  rq->dev = MKDEV (major, (drive->select.b.unit) << PARTN_BITS);
  rq->cmd = REQUEST_SENSE_COMMAND;
  rq->errors = 0;
  rq->sector = 0;
  rq->nr_sectors = 0;
  rq->current_nr_sectors = 0;
  rq->buffer = (char *)pc;
  rq->sem = NULL;
  rq->bh = NULL;
  rq->bhtail = NULL;
  rq->next = NULL;

  save_flags (flags);
  cli ();  /* safety */

  /* Stick it onto the front of the queue. */
  rq->next = blk_dev[major].current_request;
  blk_dev[major].current_request = rq;

  restore_flags (flags);
}


static void cdrom_end_request (int uptodate, ide_drive_t *drive)
{
  struct request *rq = HWGROUP(drive)->rq;

  /* The code in blk.h can screw us up on error recovery if the block
     size is larger than 1k.  Fix that up here. */
  if (!uptodate && rq->bh != 0)
    {
      int adj = rq->current_nr_sectors - 1;
      rq->current_nr_sectors -= adj;
      rq->sector += adj;
    }

  if (rq->cmd == REQUEST_SENSE_COMMAND && uptodate)
    {
      struct atapi_request_sense *reqbuf;
      reqbuf = &drive->cdrom_info.sense_data;
      cdrom_analyze_sense_data (drive, reqbuf, NULL);
    }

  ide_end_request (uptodate, HWGROUP(drive));
}


/* Mark that we've seen a media change, and invalidate our internal
   buffers. */
static void cdrom_saw_media_change (ide_drive_t *drive)
{
  CDROM_FLAGS (drive)->media_changed = 1;
  CDROM_FLAGS (drive)->toc_valid = 0;
  drive->cdrom_info.nsectors_buffered = 0;
}


/* Returns 0 if the request should be continued.
   Returns 1 if the request was ended. */
static int cdrom_decode_status (ide_drive_t *drive, int good_stat, int *stat_ret)
{
  struct request *rq = HWGROUP(drive)->rq;
  int stat, err, sense_key, cmd;

  /* Check for errors. */
  stat = GET_STAT();
  *stat_ret = stat;

  if (OK_STAT (stat, good_stat, BAD_R_STAT))
    return 0;

  /* Got an error. */
  err = IN_BYTE (IDE_ERROR_REG);
  sense_key = err >> 4;

  if (rq == NULL)
    printk ("%s : missing request in cdrom_decode_status\n", drive->name);
  else
    {
      cmd = rq->cmd;

      /* Check for tray open */
      if (sense_key == NOT_READY)
	{
	  struct packet_command *pc;
	  cdrom_saw_media_change (drive);

	  /* Fail the request if this is a read command. */
	  if (cmd == READ)
	    {
	      printk ("%s : tray open\n", drive->name);
	      cdrom_end_request (0, drive);
	    }

	  else
	    {
	      /* Otherwise, it's some other packet command.
		 Print an error message to the syslog.
		 Exception: don't print anything if this is a read subchannel
		 command.  This is because workman constantly polls the drive
		 with this command, and we don't want to uselessly fill up
		 the syslog. */
	      pc = (struct packet_command *)rq->buffer;
	      if (pc->c[0] != SCMD_READ_SUBCHANNEL)
		printk ("%s : tray open\n", drive->name);

	      /* Set the error flag and complete the request. */
	      pc->stat = 1;
	      cdrom_end_request (1, drive);
	    }
	}

      /* Check for media change. */
      else if (sense_key == UNIT_ATTENTION)
	{
	  cdrom_saw_media_change (drive);
	  printk ("%s: media changed\n", drive->name);

	  /* Return failure for a packet command, so that
	     cdrom_queue_packet_command can do a request sense before
	     the command gets retried. */

	  if (cmd == PACKET_COMMAND)
	    {
	      struct packet_command *pc = (struct packet_command *)rq->buffer;
	      pc->stat = 1;
	      cdrom_end_request (1, drive);
	    }

	  /* Otherwise, it's a block read.  Arrange to retry it.
	     But be sure to give up if we've retried too many times. */
	  else if ((++rq->errors > ERROR_MAX))
	    {
	      cdrom_end_request (0, drive);
	    }
	}

      /* Don't attempt to retry if this was a packet command. */
      else if (cmd == PACKET_COMMAND)
	{
	  struct packet_command *pc = (struct packet_command *)rq->buffer;
	  ide_dump_status (drive, "packet command error", stat);
	  pc->stat = 1;  /* signal error */
	  cdrom_end_request (1, drive);
	}

      /* No point in retrying after an illegal request or data protect error.*/
      else if (sense_key == ILLEGAL_REQUEST || sense_key == DATA_PROTECT)
	{
	  ide_dump_status (drive, "command error", stat);
	  cdrom_end_request (0, drive);
	}

      /* If there were other errors, go to the default handler. */
      else if ((err & ~ABRT_ERR) != 0)
	{
	  if (ide_error (drive, "cdrom_decode_status", stat))
	    return 1;
	}

      /* Else, abort if we've racked up too many retries. */
      else if ((++rq->errors > ERROR_MAX))
	{
	  cdrom_end_request (0, drive);
	}

      /* If we got a CHECK_STATUS condition, and this was a READ request,
	 queue a request sense command to try to find out more about
	 what went wrong (and clear a unit attention)?  For packet commands,
	 this is done separately in cdrom_queue_packet_command. */
      if ((stat & ERR_STAT) != 0 && cmd == READ)
	cdrom_queue_request_sense (drive);
    }

  /* Retry, or handle the next request. */
  IDE_DO_REQUEST;
  return 1;
}


/* Set up the device registers for transferring a packet command on DEV,
   expecting to later transfer XFERLEN bytes.  This should be followed
   by a call to cdrom_transfer_packet_command; however, if this is a
   drq_interrupt device, one must wait for an interrupt first. */
static int cdrom_start_packet_command (ide_drive_t *drive, int xferlen)
{
  /* Wait for the controller to be idle. */
  if (ide_wait_stat (drive, 0, BUSY_STAT, WAIT_READY)) return 1;

  /* Set up the controller registers. */
  OUT_BYTE (0, IDE_FEATURE_REG);
  OUT_BYTE (0, IDE_NSECTOR_REG);
  OUT_BYTE (0, IDE_SECTOR_REG);

  OUT_BYTE (xferlen & 0xff, IDE_LCYL_REG);
  OUT_BYTE (xferlen >> 8  , IDE_HCYL_REG);
  OUT_BYTE (drive->ctl, IDE_CONTROL_REG);
  OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG); /* packet command */

  return 0;
}


/* Send a packet command to DEV described by CMD_BUF and CMD_LEN.
   The device registers must have already been prepared
   by cdrom_start_packet_command. */
static int cdrom_transfer_packet_command (ide_drive_t *drive,
                                          char *cmd_buf, int cmd_len)
{
  if (CDROM_FLAGS (drive)->drq_interrupt)
    {
      /* Here we should have been called after receiving an interrupt
         from the device.  DRQ should how be set. */
      int stat_dum;

      /* Check for errors. */
      if (cdrom_decode_status (drive, DRQ_STAT, &stat_dum)) return 1;
    }
  else
    {
      /* Otherwise, we must wait for DRQ to get set. */
      if (ide_wait_stat (drive, DRQ_STAT, BUSY_STAT, WAIT_READY)) return 1;
    }

  /* Send the command to the device. */
  OUT_WORDS (cmd_buf, cmd_len/2);

  return 0;
}



/****************************************************************************
 * Block read functions.
 */

/*
 * Buffer up to SECTORS_TO_TRANSFER sectors from the drive in our sector
 * buffer.  Once the first sector is added, any subsequent sectors are
 * assumed to be continuous (until the buffer is cleared).  For the first
 * sector added, SECTOR is its sector number.  (SECTOR is then ignored until
 * the buffer is cleared.)
 */
static void cdrom_buffer_sectors (ide_drive_t *drive, unsigned long sector,
                                  int sectors_to_transfer)
{
  struct cdrom_info *info = &drive->cdrom_info;

  /* Number of sectors to read into the buffer. */
  int sectors_to_buffer = MIN (sectors_to_transfer,
                               (SECTOR_BUFFER_SIZE >> SECTOR_BITS) -
                                 info->nsectors_buffered);

  char *dest;

  /* If we don't yet have a sector buffer, try to allocate one.
     If we can't get one atomically, it's not fatal -- we'll just throw
     the data away rather than caching it. */
  if (info->sector_buffer == NULL)
    {
      info->sector_buffer = (char *) kmalloc (SECTOR_BUFFER_SIZE, GFP_ATOMIC);

      /* If we couldn't get a buffer, don't try to buffer anything... */
      if (info->sector_buffer == NULL)
        sectors_to_buffer = 0;
    }

  /* If this is the first sector in the buffer, remember its number. */
  if (info->nsectors_buffered == 0)
    info->sector_buffered = sector;

  /* Read the data into the buffer. */
  dest = info->sector_buffer + info->nsectors_buffered * SECTOR_SIZE;
  while (sectors_to_buffer > 0)
    {
      IN_WORDS (dest, SECTOR_SIZE / 2);
      --sectors_to_buffer;
      --sectors_to_transfer;
      ++info->nsectors_buffered;
      dest += SECTOR_SIZE;
    }

  /* Throw away any remaining data. */
  while (sectors_to_transfer > 0)
    {
      char dum[SECTOR_SIZE];
      IN_WORDS (dum, sizeof (dum) / 2);
      --sectors_to_transfer;
    }
}


/*
 * Check the contents of the interrupt reason register from the cdrom
 * and attempt to recover if there are problems.  Returns  0 if everything's
 * ok; nonzero if the request has been terminated.
 */
static inline
int cdrom_read_check_ireason (ide_drive_t *drive, int len, int ireason)
{
  ireason &= 3;
  if (ireason == 2) return 0;

  if (ireason == 0)
    {
      /* Whoops... The drive is expecting to receive data from us! */
      printk ("%s: cdrom_read_intr: "
              "Drive wants to transfer data the wrong way!\n",
              drive->name);

      /* Throw some data at the drive so it doesn't hang
         and quit this request. */
      while (len > 0)
        {
          short dum = 0;
          OUT_WORDS (&dum, 1);
          len -= 2;
        }
    }

  else
    {
      /* Drive wants a command packet, or invalid ireason... */
      printk ("%s: cdrom_read_intr: bad interrupt reason %d\n",
              drive->name, ireason);
    }

  cdrom_end_request (0, drive);
  IDE_DO_REQUEST;
  return -1;
}


/*
 * Interrupt routine.  Called when a read request has completed.
 */
static void cdrom_read_intr (ide_drive_t *drive)
{
  int stat;
  int ireason, len, sectors_to_transfer, nskip;

  struct request *rq = HWGROUP(drive)->rq;

  /* Check for errors. */
  if (cdrom_decode_status (drive, 0, &stat)) return;

  /* Read the interrupt reason and the transfer length. */
  ireason = IN_BYTE (IDE_NSECTOR_REG);
  len = IN_BYTE (IDE_LCYL_REG) + 256 * IN_BYTE (IDE_HCYL_REG);

  /* If DRQ is clear, the command has completed. */
  if ((stat & DRQ_STAT) == 0)
    {
      /* If we're not done filling the current buffer, complain.
         Otherwise, complete the command normally. */
      if (rq->current_nr_sectors > 0)
        {
          printk ("%s: cdrom_read_intr: data underrun (%ld blocks)\n",
                  drive->name, rq->current_nr_sectors);
          cdrom_end_request (0, drive);
        }
      else
        cdrom_end_request (1, drive);

      IDE_DO_REQUEST;
      return;
    }

  /* Check that the drive is expecting to do the same thing that we are. */
  if (cdrom_read_check_ireason (drive, len, ireason)) return;

  /* Assume that the drive will always provide data in multiples of at least
     SECTOR_SIZE, as it gets hairy to keep track of the transfers otherwise. */
  if ((len % SECTOR_SIZE) != 0)
    {
      printk ("%s: cdrom_read_intr: Bad transfer size %d\n",
              drive->name, len);
      printk ("  This drive is not supported by this version of the driver\n");
      cdrom_end_request (0, drive);
      IDE_DO_REQUEST;
      return;
    }

  /* The number of sectors we need to read from the drive. */
  sectors_to_transfer = len / SECTOR_SIZE;

  /* First, figure out if we need to bit-bucket any of the leading sectors. */
  nskip = MIN ((int)(rq->current_nr_sectors - (rq->bh->b_size >> SECTOR_BITS)),
               sectors_to_transfer);

  while (nskip > 0)
    {
      /* We need to throw away a sector. */
      char dum[SECTOR_SIZE];
      IN_WORDS (dum, sizeof (dum) / 2);

      --rq->current_nr_sectors;
      --nskip;
      --sectors_to_transfer;
    }

  /* Now loop while we still have data to read from the drive. */
  while (sectors_to_transfer > 0)
    {
      int this_transfer;

      /* If we've filled the present buffer but there's another chained
         buffer after it, move on. */
      if (rq->current_nr_sectors == 0 &&
          rq->nr_sectors > 0)
        cdrom_end_request (1, drive);

      /* If the buffers are full, cache the rest of the data in our
         internal buffer. */
      if (rq->current_nr_sectors == 0)
        {
          cdrom_buffer_sectors (drive, rq->sector, sectors_to_transfer);
          sectors_to_transfer = 0;
        }
      else
        {
          /* Transfer data to the buffers.
             Figure out how many sectors we can transfer
             to the current buffer. */
          this_transfer = MIN (sectors_to_transfer,
                               rq->current_nr_sectors);

          /* Read this_transfer sectors into the current buffer. */
          while (this_transfer > 0)
            {
              IN_WORDS (rq->buffer, SECTOR_SIZE / 2);
              rq->buffer += SECTOR_SIZE;
              --rq->nr_sectors;
              --rq->current_nr_sectors;
              ++rq->sector;
              --this_transfer;
              --sectors_to_transfer;
            }
        }
    }

  /* Done moving data!
     Wait for another interrupt. */
  ide_set_handler(drive, &cdrom_read_intr);	/* this one is okay */
}


/*
 * Try to satisfy some of the current read request from our cached data.
 * Returns nonzero if the request has been completed, zero otherwise.
 */
static int cdrom_read_from_buffer (ide_drive_t *drive)
{
  struct cdrom_info *info = &drive->cdrom_info;
  struct request *rq = HWGROUP(drive)->rq;

  /* Can't do anything if there's no buffer. */
  if (info->sector_buffer == NULL) return 0;

  /* Loop while this request needs data and the next block is present
     in our cache. */
  while (rq->nr_sectors > 0 &&
         rq->sector >= info->sector_buffered &&
         rq->sector < info->sector_buffered + info->nsectors_buffered)
    {
      if (rq->current_nr_sectors == 0)
        cdrom_end_request (1, drive);

      memcpy (rq->buffer,
              info->sector_buffer +
                (rq->sector - info->sector_buffered) * SECTOR_SIZE,
              SECTOR_SIZE);
      rq->buffer += SECTOR_SIZE;
      --rq->current_nr_sectors;
      --rq->nr_sectors;
      ++rq->sector;
    }

  /* If we've satisfied the current request, terminate it successfully. */
  if (rq->nr_sectors == 0)
    {
      cdrom_end_request (1, drive);
      return -1;
    }

  /* Move on to the next buffer if needed. */
  if (rq->current_nr_sectors == 0)
    cdrom_end_request (1, drive);

  /* If this condition does not hold, then the kluge i use to
     represent the number of sectors to skip at the start of a transfer
     will fail.  I think that this will never happen, but let's be
     paranoid and check. */
  if (rq->current_nr_sectors < (rq->bh->b_size >> SECTOR_BITS) &&
      (rq->sector % SECTORS_PER_FRAME) != 0)
    {
      printk ("%s: cdrom_read_from_buffer: buffer botch (%ld)\n",
              drive->name, rq->sector);
      cdrom_end_request (0, drive);
      return -1;
    }

  return 0;
}



/*
 * Routine to send a read packet command to the drive.
 * This is usually called directly from cdrom_start_read.
 * However, for drq_interrupt devices, it is called from an interrupt
 * when the drive is ready to accept the command.
 */
static int cdrom_start_read_continuation (ide_drive_t *drive)
{
  struct packet_command pc;
  struct request *rq = HWGROUP(drive)->rq;

  int nsect, sector, nframes, frame, nskip;

  /* Number of sectors to transfer. */
  nsect = rq->nr_sectors;

  /* Starting sector. */
  sector = rq->sector;

  /* If the requested sector doesn't start on a cdrom block boundary,
     we must adjust the start of the transfer so that it does,
     and remember to skip the first few sectors.  If the CURRENT_NR_SECTORS
     field is larger than the size of the buffer, it will mean that
     we're to skip a number of sectors equal to the amount by which
     CURRENT_NR_SECTORS is larger than the buffer size. */
  nskip = (sector % SECTORS_PER_FRAME);
  if (nskip > 0)
    {
      /* Sanity check... */
      if (rq->current_nr_sectors != (rq->bh->b_size >> SECTOR_BITS))
        {
          printk ("%s: cdrom_start_read_continuation: buffer botch (%ld)\n",
                  drive->name, rq->current_nr_sectors);
          cdrom_end_request (0, drive);
          IDE_DO_REQUEST;
          return 1;
        }

      sector -= nskip;
      nsect += nskip;
      rq->current_nr_sectors += nskip;
    }

  /* Convert from sectors to cdrom blocks, rounding up the transfer
     length if needed. */
  nframes = (nsect + SECTORS_PER_FRAME-1) / SECTORS_PER_FRAME;
  frame = sector / SECTORS_PER_FRAME;

  /* Largest number of frames was can transfer at once is 64k-1. */
  nframes = MIN (nframes, 65535);

  /* Set up the command */
  memset (&pc.c, 0, sizeof (pc.c));
  pc.c[0] = READ_10;
  pc.c[7] = (nframes >> 8);
  pc.c[8] = (nframes & 0xff);

  /* Write the sector address into the command image. */
  {
    union {
      struct {unsigned char b0, b1, b2, b3;} b;
      struct {unsigned long l0;} l;
    } conv;
    conv.l.l0 = frame;
    pc.c[2] = conv.b.b3;
    pc.c[3] = conv.b.b2;
    pc.c[4] = conv.b.b1;
    pc.c[5] = conv.b.b0;
  }

  if (cdrom_transfer_packet_command (drive, pc.c, sizeof (pc.c)))
    return 1;

  /* Set up our interrupt handler and return. */
  ide_set_handler(drive, &cdrom_read_intr); /* BUG: do this BEFORE triggering drive */

  return 0;
}


/*
 * Start a read request from the CD-ROM.
 * Returns 0 if the request was started successfully,
 *  1 if there was an error and we should either retry or move on to the
 *  next request.
 */
static int cdrom_start_read (ide_drive_t *drive, unsigned int block)
{
  struct request *rq = HWGROUP(drive)->rq;

  /* We may be retrying this request after an error.
     Fix up any weirdness which might be present in the request packet. */
  restore_request (rq);

  /* Satisfy whatever we can of this request from our cached sector. */
  if (cdrom_read_from_buffer (drive))
    return 1;

  /* Clear the local sector buffer. */
  drive->cdrom_info.nsectors_buffered = 0;

  if (cdrom_start_packet_command (drive, 32768))
    return 1;

  if (CDROM_FLAGS (drive)->drq_interrupt)
    ide_set_handler(drive, (ide_handler_t *)&cdrom_start_read_continuation); /* BUG: do this BEFORE triggering drive */
  else
    {
      if (cdrom_start_read_continuation (drive))
        return 1;
    }

  return 0;
}




/****************************************************************************
 * Execute all other packet commands.
 */

/* Forward declaration */
static int
cdrom_request_sense (ide_drive_t *drive, struct atapi_request_sense *reqbuf);


/* Interrupt routine for packet command completion. */
static void cdrom_pc_intr (ide_drive_t *drive)
{
  int ireason, len, stat, thislen;
  struct request *rq = HWGROUP(drive)->rq;
  struct packet_command *pc = (struct packet_command *)rq->buffer;

  /* Check for errors. */
  if (cdrom_decode_status (drive, 0, &stat)) return;

  /* Read the interrupt reason and the transfer length. */
  ireason = IN_BYTE (IDE_NSECTOR_REG);
  len = IN_BYTE (IDE_LCYL_REG) + 256 * IN_BYTE (IDE_HCYL_REG);

  /* If DRQ is clear, the command has completed.
     Complain if we still have data left to transfer. */
  if ((stat & DRQ_STAT) == 0)
    {
      /* Some of the trailing request sense fields are optional, and
	 some drives don't send them.  Sigh. */
      if (pc->c[0] == REQUEST_SENSE && pc->buflen > 0 && pc->buflen <= 5) {
	while (pc->buflen > 0) {
	  *pc->buffer++ = 0;
	  --pc->buflen;
	}
      }

      if (pc->buflen == 0)
        cdrom_end_request (1, drive);
      else
        {
          printk ("%s: cdrom_pc_intr: data underrun %d\n",
                  drive->name, pc->buflen);
          pc->stat = 1;
          cdrom_end_request (1, drive);
        }
      IDE_DO_REQUEST;
      return;
    }

  /* Figure out how much data to transfer. */
  thislen = pc->buflen;
  if (thislen < 0) thislen = -thislen;
  if (thislen > len) thislen = len;

  /* The drive wants to be written to. */
  if ((ireason & 3) == 0)
    {
      /* Check that we want to write. */
      if (pc->buflen > 0)
        {
          printk ("%s: cdrom_pc_intr: Drive wants to transfer data the wrong way!\n",
                  drive->name);
          pc->stat = 1;
          thislen = 0;
        }

      /* Transfer the data. */
      OUT_WORDS (pc->buffer, thislen / 2);

      /* If we haven't moved enough data to satisfy the drive,
         add some padding. */
      while (len > thislen)
        {
          short dum = 0;
          OUT_WORDS (&dum, 1);
          len -= 2;
        }

      /* Keep count of how much data we've moved. */
      pc->buffer += thislen;
      pc->buflen += thislen;
    }

  /* Same drill for reading. */
  else if ((ireason & 3) == 2)
    {
      /* Check that we want to read. */
      if (pc->buflen < 0)
        {
          printk ("%s: cdrom_pc_intr: Drive wants to transfer data the wrong way!\n",
                  drive->name);
          pc->stat = 1;
          thislen = 0;
        }

      /* Transfer the data. */
      IN_WORDS (pc->buffer, thislen / 2);

      /* If we haven't moved enough data to satisfy the drive,
         add some padding. */
      while (len > thislen)
        {
          short dum = 0;
          IN_WORDS (&dum, 1);
          len -= 2;
        }

      /* Keep count of how much data we've moved. */
      pc->buffer += thislen;
      pc->buflen -= thislen;
    }

  else
    {
      printk ("%s: cdrom_pc_intr: The drive appears confused (ireason = 0x%2x)\n",
              drive->name, ireason);
      pc->stat = 1;
    }

  /* Now we wait for another interrupt. */
  ide_set_handler(drive, &cdrom_pc_intr);	/* this one is okay */
}


static int cdrom_do_pc_continuation (ide_drive_t *drive)
{
  struct request *rq = HWGROUP(drive)->rq;
  struct packet_command *pc = (struct packet_command *)rq->buffer;

  if (cdrom_transfer_packet_command (drive, pc->c, sizeof (pc->c)))
    return 1;

  /* Set up our interrupt handler and return. */
  ide_set_handler(drive, &cdrom_pc_intr); /* BUG: do this BEFORE triggering drive */

  return 0;
}


static int cdrom_do_packet_command (ide_drive_t *drive)
{
  int len;
  struct request *rq = HWGROUP(drive)->rq;
  struct packet_command *pc = (struct packet_command *)rq->buffer;

  len = pc->buflen;
  if (len < 0) len = -len;

  pc->stat = 0;

  if (cdrom_start_packet_command (drive, len))
    return 1;

  if (CDROM_FLAGS (drive)->drq_interrupt)
    ide_set_handler(drive, (ide_handler_t *)&cdrom_do_pc_continuation); /* BUG: do this BEFORE triggering drive */
  else
    {
      if (cdrom_do_pc_continuation (drive))
        return 1;
    }

  return 0;
}


static
int cdrom_queue_packet_command (ide_drive_t *drive, struct packet_command *pc)
{
  int retries = 3;
  unsigned long flags;
  struct request req, **p, **pfirst;
  struct semaphore sem = MUTEX_LOCKED;
  int major = HWIF(drive)->major;

 retry:
  req.dev = MKDEV (major, (drive->select.b.unit) << PARTN_BITS);
  req.cmd = PACKET_COMMAND;
  req.errors = 0;
  req.sector = 0;
  req.nr_sectors = 0;
  req.current_nr_sectors = 0;
  req.buffer = (char *)pc;
  req.sem = &sem;
  req.bh = NULL;
  req.bhtail = NULL;
  req.next = NULL;

  save_flags (flags);
  cli ();

  p = &blk_dev[major].current_request;
  pfirst = p;
  while ((*p) != NULL)
    {
      p = &((*p)->next);
    }
  *p = &req;
  if (p == pfirst)
    blk_dev[major].request_fn ();

  restore_flags (flags);

  down (&sem);

  if (pc->stat != 0)
    {
      /* The request failed.  Try to do a request sense to get more information
	 about the error; store the result in the cdrom_info struct
	 for this drive.  Check to be sure that it wasn't a request sense
	 request that failed, though, to prevent infinite loops. */
      
      struct atapi_request_sense *reqbuf = &drive->cdrom_info.sense_data;

      if (pc->c[0] == REQUEST_SENSE || cdrom_request_sense (drive, reqbuf))
	{
	  memset (reqbuf, 0, sizeof (*reqbuf));
	  reqbuf->asc = 0xff;
	}
      cdrom_analyze_sense_data (drive, reqbuf, pc);

      /* If the error was a unit attention (usually means media was changed),
	 retry the command. */
      if (reqbuf->sense_key == UNIT_ATTENTION && retries > 0)
	{
	  --retries;
	  goto retry;
	}

      return -EIO;
    }
  else
    return 0;
}



/****************************************************************************
 * cdrom driver request routine.
 */

void ide_do_rw_cdrom (ide_drive_t *drive, unsigned long block)
{
  struct request *rq = HWGROUP(drive)->rq;

  if (rq -> cmd == PACKET_COMMAND || rq -> cmd == REQUEST_SENSE_COMMAND)
    cdrom_do_packet_command (drive);
  else if (rq -> cmd != READ)
    {
      printk ("ide-cd: bad cmd %d\n", rq -> cmd);
      cdrom_end_request (0, drive);
    }
  else
    cdrom_start_read (drive, block);
}



/****************************************************************************
 * ioctl handling.
 */

static inline
void byte_swap_word (unsigned short *x)
{
  char *c = (char *)x;
  char d = c[0];
  c[0] = c[1];
  c[1] = d;
}


static inline
void byte_swap_long (unsigned *x)
{
  char *c = (char *)x;
  char d = c[0];
  c[0] = c[3];
  c[3] = d;
  d = c[1];
  c[1] = c[2];
  c[2] = d;
}


static
int bin2bcd (int x)
{
  return (x%10) | ((x/10) << 4);
}


static inline
void lba_to_msf (int lba, byte *m, byte *s, byte *f)
{
  lba += CD_BLOCK_OFFSET;
  lba &= 0xffffff;  /* negative lbas use only 24 bits */
  *m = lba / (CD_SECS * CD_FRAMES);
  lba %= (CD_SECS * CD_FRAMES);
  *s = lba / CD_FRAMES;
  *f = lba % CD_FRAMES;
}


static inline
int msf_to_lba (byte m, byte s, byte f)
{
  return (((m * CD_SECS) + s) * CD_FRAMES + f) - CD_BLOCK_OFFSET;
}


static void
cdrom_check_status (ide_drive_t  *drive)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = TEST_UNIT_READY;

  (void) cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_request_sense (ide_drive_t *drive, struct atapi_request_sense *reqbuf)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = REQUEST_SENSE;
  pc.c[4] = sizeof (*reqbuf);
  pc.buffer = (char *)reqbuf;
  pc.buflen = sizeof (*reqbuf);

  return cdrom_queue_packet_command (drive, &pc);
}


#if 0
/* Lock the door if LOCKFLAG is nonzero; unlock it otherwise. */
static int
cdrom_lockdoor (ide_drive_t *drive, int lockflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = ALLOW_MEDIUM_REMOVAL;
  pc.c[4] = (lockflag != 0);
  return cdrom_queue_packet_command (drive, &pc);
}
#endif


/* Eject the disk if EJECTFLAG is 0.
   If EJECTFLAG is 1, try to reload the disk. */
static int
cdrom_eject (ide_drive_t *drive, int ejectflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = START_STOP;
  pc.c[4] = 2 + (ejectflag != 0);
  return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_pause (ide_drive_t *drive, int pauseflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = SCMD_PAUSE_RESUME;
  pc.c[8] = !pauseflag;
  return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_startstop (ide_drive_t *drive, int startflag)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = START_STOP;
  pc.c[1] = 1;
  pc.c[4] = startflag;
  return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_read_tocentry (ide_drive_t *drive, int trackno, int msf_flag,
                     char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = buflen;
  pc.c[0] = SCMD_READ_TOC;
  pc.c[6] = trackno;
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  if (msf_flag) pc.c[1] = 2;
  return cdrom_queue_packet_command (drive, &pc);
}


/* Try to read the entire TOC for the disk into our internal buffer. */
static int
cdrom_read_toc (ide_drive_t *drive)
{
  int msf_flag;
  int stat, ntracks, i;
  struct atapi_toc *toc = drive->cdrom_info.toc;

  if (toc == NULL)
    {
      /* Try to allocate space. */
      toc = (struct atapi_toc *) kmalloc (sizeof (struct atapi_toc),
                                          GFP_KERNEL);
      drive->cdrom_info.toc = toc;
    }

  if (toc == NULL)
    {
      printk ("%s: No cdrom TOC buffer!\n", drive->name);
      return -EIO;
    }

  /* Check to see if the existing data is still valid.
     If it is, just return. */
  if (CDROM_FLAGS (drive)->toc_valid)
    cdrom_check_status (drive);

  if (CDROM_FLAGS (drive)->toc_valid) return 0;

  /* Some drives can't return TOC data in LBA format. */
  msf_flag = (CDROM_FLAGS (drive)->no_lba_toc);

  /* First read just the header, so we know how long the TOC is. */
  stat = cdrom_read_tocentry (drive, 0, msf_flag, (char *)toc,
                              sizeof (struct atapi_toc_header) +
                              sizeof (struct atapi_toc_entry));
  if (stat) return stat;

  ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
  if (ntracks <= 0) return -EIO;
  if (ntracks > MAX_TRACKS) ntracks = MAX_TRACKS;

  /* Now read the whole schmeer. */
  stat = cdrom_read_tocentry (drive, 0, msf_flag, (char *)toc,
                              sizeof (struct atapi_toc_header) +
                              (ntracks+1) * sizeof (struct atapi_toc_entry));
  if (stat) return stat;
  byte_swap_word (&toc->hdr.toc_length);
  for (i=0; i<=ntracks; i++)
    {
      if (msf_flag)
	{
	  byte *adr = (byte *)&(toc->ent[i].lba);
	  toc->ent[i].lba = msf_to_lba (adr[1], adr[2], adr[3]);
	}
      else
	byte_swap_long (&toc->ent[i].lba);
    }

  /* Remember that we've read this stuff. */
  CDROM_FLAGS (drive)->toc_valid = 1;

  return 0;
}


static int
cdrom_read_subchannel (ide_drive_t *drive,
                       char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = buflen;
  pc.c[0] = SCMD_READ_SUBCHANNEL;
  pc.c[2] = 0x40;  /* request subQ data */
  pc.c[3] = 0x01;  /* Format 1: current position */
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  return cdrom_queue_packet_command (drive, &pc);
}


/* modeflag: 0 = current, 1 = changeable mask, 2 = default, 3 = saved */
static int
cdrom_mode_sense (ide_drive_t *drive, int pageno, int modeflag,
                  char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = buflen;
  pc.c[0] = MODE_SENSE_10;
  pc.c[2] = pageno | (modeflag << 6);
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_mode_select (ide_drive_t *drive, int pageno, char *buf, int buflen)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.buffer =  buf;
  pc.buflen = - buflen;
  pc.c[0] = MODE_SELECT_10;
  pc.c[1] = 0x10;
  pc.c[2] = pageno;
  pc.c[7] = (buflen >> 8);
  pc.c[8] = (buflen & 0xff);
  return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_play_lba_range_play12 (ide_drive_t *drive, int lba_start, int lba_end)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = SCMD_PLAYAUDIO12;
  *(int *)(&pc.c[2]) = lba_start;
  *(int *)(&pc.c[6]) = lba_end - lba_start;
  byte_swap_long ((int *)(&pc.c[2]));
  byte_swap_long ((int *)(&pc.c[6]));

  return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_play_lba_range_msf (ide_drive_t *drive, int lba_start, int lba_end)
{
  struct packet_command pc;

  memset (&pc, 0, sizeof (pc));

  pc.c[0] = SCMD_PLAYAUDIO_MSF;
  lba_to_msf (lba_start, &pc.c[3], &pc.c[4], &pc.c[5]);
  lba_to_msf (lba_end-1, &pc.c[6], &pc.c[7], &pc.c[8]);

  if (CDROM_FLAGS (drive)->msf_as_bcd)
    {
      pc.c[3] = bin2bcd (pc.c[3]);
      pc.c[4] = bin2bcd (pc.c[4]);
      pc.c[5] = bin2bcd (pc.c[5]);
      pc.c[6] = bin2bcd (pc.c[6]);
      pc.c[7] = bin2bcd (pc.c[7]);
      pc.c[8] = bin2bcd (pc.c[8]);
    }

  return cdrom_queue_packet_command (drive, &pc);
}


/* Play audio starting at LBA LBA_START and finishing with the
   LBA before LBA_END. */
static int
cdrom_play_lba_range (ide_drive_t *drive, int lba_start, int lba_end)
{
  /* This is rather annoying.
     My NEC-260 won't recognize group 5 commands such as PLAYAUDIO12;
     the only way to get it to play more than 64k of blocks at once
     seems to be the PLAYAUDIO_MSF command.  However, the parameters
     the NEC 260 wants for the PLAYMSF command are incompatible with
     the new version of the spec.

     So what i'll try is this.  First try for PLAYAUDIO12.  If it works,
     great.  Otherwise, if the drive reports an illegal command code,
     try PLAYAUDIO_MSF using the NEC 260-style bcd parameters. */

  if (CDROM_FLAGS (drive)->no_playaudio12)
    return cdrom_play_lba_range_msf (drive, lba_start, lba_end);
  else
    {
      int stat;
      struct atapi_request_sense *reqbuf;

      stat = cdrom_play_lba_range_play12 (drive, lba_start, lba_end);
      if (stat == 0) return 0;

      /* It failed.  Try to find out why. */
      reqbuf = &drive->cdrom_info.sense_data;
      if (reqbuf->sense_key == 0x05 && reqbuf->asc == 0x20)
        {
          /* The drive didn't recognize the command.
             Retry with the MSF variant. */
          printk ("%s: Drive does not support PLAYAUDIO12; "
                  "trying PLAYAUDIO_MSF\n", drive->name);
          CDROM_FLAGS (drive)->no_playaudio12 = 1;
          CDROM_FLAGS (drive)->msf_as_bcd = 1;
          return cdrom_play_lba_range_msf (drive, lba_start, lba_end);
        }

      /* Failed for some other reason.  Give up. */
      return stat;
    }
}


static
int cdrom_get_toc_entry (ide_drive_t *drive, int track,
                         struct atapi_toc_entry **ent)
{
  int stat, ntracks;
  struct atapi_toc *toc;

  /* Make sure our saved TOC is valid. */
  stat = cdrom_read_toc (drive);
  if (stat) return stat;

  toc = drive->cdrom_info.toc;

  /* Check validity of requested track number. */
  ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
  if (track == CDROM_LEADOUT)
    *ent = &toc->ent[ntracks];
  else if (track < toc->hdr.first_track ||
           track > toc->hdr.last_track)
    return -EINVAL;
  else
    *ent = &toc->ent[track - toc->hdr.first_track];

  return 0;
}


int ide_cdrom_ioctl (ide_drive_t *drive, struct inode *inode,
                        struct file *file, unsigned int cmd, unsigned long arg)
{
  switch (cmd)
    {
    case CDROMEJECT:
      return cdrom_eject (drive, 0);

    case CDROMPAUSE:
      return cdrom_pause (drive, 1);

    case CDROMRESUME:
      return cdrom_pause (drive, 0);

    case CDROMSTART:
      return cdrom_startstop (drive, 1);

    case CDROMSTOP:
      return cdrom_startstop (drive, 0);

    case CDROMPLAYMSF:
      {
        struct cdrom_msf msf;
        int stat, lba_start, lba_end;

        stat = verify_area (VERIFY_READ, (void *)arg, sizeof (msf));
        if (stat) return stat;

        memcpy_fromfs (&msf, (void *) arg, sizeof(msf));

        lba_start = msf_to_lba (msf.cdmsf_min0, msf.cdmsf_sec0,
                                msf.cdmsf_frame0);
        lba_end = msf_to_lba (msf.cdmsf_min1, msf.cdmsf_sec1,
                              msf.cdmsf_frame1) + 1;

        if (lba_end <= lba_start) return -EINVAL;

        return cdrom_play_lba_range (drive, lba_start, lba_end);
      }

    /* Like just about every other Linux cdrom driver, we ignore the
       index part of the request here. */
    case CDROMPLAYTRKIND:
      {
        int stat, lba_start, lba_end;
        struct cdrom_ti ti;
        struct atapi_toc_entry *first_toc, *last_toc;

        stat = verify_area (VERIFY_READ, (void *)arg, sizeof (ti));
        if (stat) return stat;

        memcpy_fromfs (&ti, (void *) arg, sizeof(ti));

        stat = cdrom_get_toc_entry (drive, ti.cdti_trk0, &first_toc);
        if (stat) return stat;
        stat = cdrom_get_toc_entry (drive, ti.cdti_trk1, &last_toc);
        if (stat) return stat;

        if (ti.cdti_trk1 != CDROM_LEADOUT) ++last_toc;
        lba_start = first_toc->lba;
        lba_end   = last_toc->lba;

        if (lba_end <= lba_start) return -EINVAL;

        return cdrom_play_lba_range (drive, lba_start, lba_end);
      }

    case CDROMREADTOCHDR:
      {
        int stat;
        struct cdrom_tochdr tochdr;
        struct atapi_toc *toc;

        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (tochdr));
        if (stat) return stat;

        /* Make sure our saved TOC is valid. */
        stat = cdrom_read_toc (drive);
        if (stat) return stat;

        toc = drive->cdrom_info.toc;
        tochdr.cdth_trk0 = toc->hdr.first_track;
        tochdr.cdth_trk1 = toc->hdr.last_track;

        memcpy_tofs ((void *) arg, &tochdr, sizeof (tochdr));

        return stat;
      }

    case CDROMREADTOCENTRY:
      {
        int stat;
        struct cdrom_tocentry tocentry;
        struct atapi_toc_entry *toce;

        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (tocentry));
        if (stat) return stat;
        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (tocentry));
        if (stat) return stat;

        memcpy_fromfs (&tocentry, (void *) arg, sizeof (tocentry));

        stat = cdrom_get_toc_entry (drive, tocentry.cdte_track, &toce);
        if (stat) return stat;

        tocentry.cdte_ctrl = toce->control;
        tocentry.cdte_adr  = toce->adr;

        if (tocentry.cdte_format == CDROM_MSF)
          {
            /* convert to MSF */
            lba_to_msf (toce->lba,
                        &tocentry.cdte_addr.msf.minute,
                        &tocentry.cdte_addr.msf.second,
                        &tocentry.cdte_addr.msf.frame);
          }
        else
          tocentry.cdte_addr.lba = toce->lba;

        memcpy_tofs ((void *) arg, &tocentry, sizeof (tocentry));

        return stat;
      }

    case CDROMSUBCHNL:
      {
        char buffer[16];
        int stat, abs_lba, rel_lba;
        struct cdrom_subchnl subchnl;

        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (subchnl));
        if (stat) return stat;
        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (subchnl));
        if (stat) return stat;

        memcpy_fromfs (&subchnl, (void *) arg, sizeof (subchnl));

        stat = cdrom_read_subchannel (drive, buffer, sizeof (buffer));
        if (stat) return stat;

        abs_lba = *(int *)&buffer[8];
        rel_lba = *(int *)&buffer[12];
        byte_swap_long (&abs_lba);
        byte_swap_long (&rel_lba);

        if (subchnl.cdsc_format == CDROM_MSF)
          {
            lba_to_msf (abs_lba,
                        &subchnl.cdsc_absaddr.msf.minute,
                        &subchnl.cdsc_absaddr.msf.second,
                        &subchnl.cdsc_absaddr.msf.frame);
            lba_to_msf (rel_lba,
                        &subchnl.cdsc_reladdr.msf.minute,
                        &subchnl.cdsc_reladdr.msf.second,
                        &subchnl.cdsc_reladdr.msf.frame);
          }
        else
          {
            subchnl.cdsc_absaddr.lba = abs_lba;
            subchnl.cdsc_reladdr.lba = rel_lba;
          }

        subchnl.cdsc_audiostatus = buffer[1];
        subchnl.cdsc_ctrl = buffer[5] & 0xf;
        subchnl.cdsc_trk = buffer[6];
        subchnl.cdsc_ind = buffer[7];

        memcpy_tofs ((void *) arg, &subchnl, sizeof (subchnl));

        return stat;
      }

    case CDROMVOLCTRL:
      {
        struct cdrom_volctrl volctrl;
        char buffer[24], mask[24];
        int stat;

        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (volctrl));
        if (stat) return stat;
        memcpy_fromfs (&volctrl, (void *) arg, sizeof (volctrl));

        stat = cdrom_mode_sense (drive, 0x0e, 0, buffer, sizeof (buffer));
        if (stat) return stat;
        stat = cdrom_mode_sense (drive, 0x0e, 1, mask  , sizeof (buffer));
        if (stat) return stat;

        buffer[1] = buffer[2] = 0;

        buffer[17] = volctrl.channel0 & mask[17];
        buffer[19] = volctrl.channel1 & mask[19];
        buffer[21] = volctrl.channel2 & mask[21];
        buffer[23] = volctrl.channel3 & mask[23];

        return cdrom_mode_select (drive, 0x0e, buffer, sizeof (buffer));
      }

#ifdef TEST
    case 0x1234:
      {
        int stat;
        struct packet_command pc;

        memset (&pc, 0, sizeof (pc));

        stat = verify_area (VERIFY_READ, (void *) arg, sizeof (pc.c));
        if (stat) return stat;
        memcpy_fromfs (&pc.c, (void *) arg, sizeof (pc.c));

        return cdrom_queue_packet_command (drive, &pc);
      }

    case 0x1235:
      {
        int stat;
        struct atapi_request_sense reqbuf;

        stat = verify_area (VERIFY_WRITE, (void *) arg, sizeof (reqbuf));
        if (stat) return stat;

        stat = cdrom_request_sense (drive, &reqbuf);

        memcpy_tofs ((void *) arg, &reqbuf, sizeof (reqbuf));

        return stat;
      }
#endif

    default:
      return -EPERM;
    }

}



/****************************************************************************
 * Other driver requests (open, close, check media change).
 */

int ide_cdrom_check_media_change (ide_drive_t *drive)
{
  int retval;

  cdrom_check_status (drive);

  retval = CDROM_FLAGS (drive)->media_changed;
  CDROM_FLAGS (drive)->media_changed = 0;

  return retval;
}


int ide_cdrom_open (struct inode *ip, struct file *fp, ide_drive_t *drive)
{
  /* no write access */
  if (fp->f_mode & 2) return -EROFS;

#if 0 /* With this, one cannot eject a disk with workman */
  /* If this is the first open, lock the door. */
  if (drive->usage == 1)
    (void) cdrom_lockdoor (drive, 1);
#endif

  /* Should check that there's a disk in the drive? */
  return 0;
}


/*
 * Close down the device.  Invalidate all cached blocks.
 */

void ide_cdrom_release (struct inode *inode, struct file *file, ide_drive_t *drive)
{
  if (drive->usage == 0)
    {
      invalidate_buffers (inode->i_rdev);

#if 0
      /* Unlock the door. */
      (void) cdrom_lockdoor (drive, 0);
#endif
    }
}



/****************************************************************************
 * Device initialization.
 */

void ide_cdrom_setup (ide_drive_t *drive)
{
  blksize_size[HWIF(drive)->major][drive->select.b.unit << PARTN_BITS] = CD_FRAMESIZE;

  drive->special.all = 0;
  drive->ready_stat = 0;

  CDROM_FLAGS (drive)->media_changed = 0;
  CDROM_FLAGS (drive)->toc_valid     = 0;

  CDROM_FLAGS (drive)->no_playaudio12 = 0;
  CDROM_FLAGS (drive)->no_lba_toc = 0;
  CDROM_FLAGS (drive)->msf_as_bcd = 0;
  CDROM_FLAGS (drive)->drq_interrupt = ((drive->id->config & 0x0060) == 0x20);

  /* Accommodate some broken drives... */
  if (strcmp (drive->id->model, "CD220E") == 0)  /* Creative Labs */
    CDROM_FLAGS (drive)->no_lba_toc = 1;

  else if (strcmp (drive->id->model, "TO-ICSLYAL") == 0 ||  /* Acer CD525E */
           strcmp (drive->id->model, "OTI-SCYLLA") == 0)
    CDROM_FLAGS (drive)->no_lba_toc = 1;

  else if (strcmp (drive->id->model, "CDA26803I SE") == 0) /* Aztech */
    {
      CDROM_FLAGS (drive)->no_lba_toc = 1;

      /* This drive _also_ does not implement PLAYAUDIO12 correctly. */
      CDROM_FLAGS (drive)->no_playaudio12 = 1;
    }

  drive->cdrom_info.toc               = NULL;
  drive->cdrom_info.sector_buffer     = NULL;
  drive->cdrom_info.sector_buffered   = 0;
  drive->cdrom_info.nsectors_buffered = 0;
}


#undef MIN
#undef SECTOR_SIZE
#undef SECTOR_BITS


/*
 * TODO:
 *  Read actual disk capacity.
 *  Multisession support.
 *  Direct reading of audio data.
 *  Eject-on-dismount.
 *  Lock door while there's a mounted volume.
 *  Establish interfaces for an IDE port driver, and break out the cdrom
 *   code into a loadable module.
 */

