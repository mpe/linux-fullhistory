/*
 * linux/drivers/block/ide-cd.c
 * Copyright (C) 1994, 1995, 1996  scott snyder  <snyder@fnald0.fnal.gov>
 * Copyright (C) 1996-1998  Erik Andersen <andersee@debian.org>
 * Copyright (C) 1998, 1999 Jens Axboe
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * ATAPI CD-ROM driver.  To be used with ide.c.
 * See Documentation/cdrom/ide-cd for usage information.
 *
 * Suggestions are welcome. Patches that work are more welcome though. ;-)
 * For those wishing to work on this driver, please be sure you download
 * and comply with the latest ATAPI standard. This document can be
 * obtained by anonymous ftp from:
 * ftp://fission.dt.wdc.com/pub/standards/SFF_atapi/spec/SFF8020-r2.6/PS/8020r26.ps
 *
 * Drives that deviate from the ATAPI standard will be accomodated as much
 * as possible via compile time or command-line options.  Since I only have
 * a few drives, you generally need to send me patches...
 *
 * ----------------------------------
 * TO DO LIST:
 * -Implement Microsoft Media Status Notification per the spec at
 *   http://www.microsoft.com/hwdev/respec/storspec.htm
 *   This will allow us to get automagically notified when the media changes
 *   on ATAPI drives (something the stock ATAPI spec is lacking).  Looks
 *   very cool.  I discovered its existance the other day at work...
 * -Query the drive to find what features are available before trying to
 *   use them (like trying to close the tray in drives that can't).
 * -Make it so that Pioneer CD DR-A24X and friends don't get screwed up on
 *   boot
 * -Integrate DVD-ROM support in driver. Thanks to Merete Gotsæd-Petersen
 *   of Pioneer Denmark for providing me with a drive for testing.
 *
 *
 * ----------------------------------
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
 * 3.00  Aug 22, 1995 -- Implement CDROMMULTISESSION ioctl.
 *                       Implement CDROMREADAUDIO ioctl (UNTESTED).
 *                       Use input_ide_data() and output_ide_data().
 *                       Add door locking.
 *                       Fix usage count leak in cdrom_open, which happened
 *                        when a read-write mount was attempted.
 *                       Try to load the disk on open.
 *                       Implement CDROMEJECT_SW ioctl (off by default).
 *                       Read total cdrom capacity during open.
 *                       Rearrange logic in cdrom_decode_status.  Issue
 *                        request sense commands for failed packet commands
 *                        from here instead of from cdrom_queue_packet_command.
 *                        Fix a race condition in retrieving error information.
 *                       Suppress printing normal unit attention errors and
 *                        some drive not ready errors.
 *                       Implement CDROMVOLREAD ioctl.
 *                       Implement CDROMREADMODE1/2 ioctls.
 *                       Fix race condition in setting up interrupt handlers
 *                        when the `serialize' option is used.
 * 3.01  Sep  2, 1995 -- Fix ordering of reenabling interrupts in
 *                        cdrom_queue_request.
 *                       Another try at using ide_[input,output]_data.
 * 3.02  Sep 16, 1995 -- Stick total disk capacity in partition table as well.
 *                       Make VERBOSE_IDE_CD_ERRORS dump failed command again.
 *                       Dump out more information for ILLEGAL REQUEST errs.
 *                       Fix handling of errors occurring before the
 *                        packet command is transferred.
 *                       Fix transfers with odd bytelengths.
 * 3.03  Oct 27, 1995 -- Some Creative drives have an id of just `CD'.
 *                       `DCI-2S10' drives are broken too.
 * 3.04  Nov 20, 1995 -- So are Vertos drives.
 * 3.05  Dec  1, 1995 -- Changes to go with overhaul of ide.c and ide-tape.c
 * 3.06  Dec 16, 1995 -- Add support needed for partitions.
 *                       More workarounds for Vertos bugs (based on patches
 *                        from Holger Dietze <dietze@aix520.informatik.uni-leipzig.de>).
 *                       Try to eliminate byteorder assumptions.
 *                       Use atapi_cdrom_subchnl struct definition.
 *                       Add STANDARD_ATAPI compilation option.
 * 3.07  Jan 29, 1996 -- More twiddling for broken drives: Sony 55D,
 *                        Vertos 300.
 *                       Add NO_DOOR_LOCKING configuration option.
 *                       Handle drive_cmd requests w/NULL args (for hdparm -t).
 *                       Work around sporadic Sony55e audio play problem.
 * 3.07a Feb 11, 1996 -- check drive->id for NULL before dereferencing, to fix
 *                        problem with "hde=cdrom" with no drive present.  -ml
 * 3.08  Mar  6, 1996 -- More Vertos workarounds.
 * 3.09  Apr  5, 1996 -- Add CDROMCLOSETRAY ioctl.
 *                       Switch to using MSF addressing for audio commands.
 *                       Reformat to match kernel tabbing style.
 *                       Add CDROM_GET_UPC ioctl.
 * 3.10  Apr 10, 1996 -- Fix compilation error with STANDARD_ATAPI.
 * 3.11  Apr 29, 1996 -- Patch from Heiko Eissfeldt <heiko@colossus.escape.de>
 *                       to remove redundant verify_area calls.
 * 3.12  May  7, 1996 -- Rudimentary changer support.  Based on patches
 *                        from Gerhard Zuber <zuber@berlin.snafu.de>.
 *                       Let open succeed even if there's no loaded disc.
 * 3.13  May 19, 1996 -- Fixes for changer code.
 * 3.14  May 29, 1996 -- Add work-around for Vertos 600.
 *                        (From Hennus Bergman <hennus@sky.ow.nl>.)
 * 3.15  July 2, 1996 -- Added support for Sanyo 3 CD changers
 *                        from Ben Galliart <bgallia@luc.edu> with 
 *                        special help from Jeff Lightfoot 
 *                        <jeffml@netcom.com>
 * 3.15a July 9, 1996 -- Improved Sanyo 3 CD changer identification
 * 3.16  Jul 28, 1996 -- Fix from Gadi to reduce kernel stack usage for ioctl.
 * 3.17  Sep 17, 1996 -- Tweak audio reads for some drives.
 *                       Start changing CDROMLOADFROMSLOT to CDROM_SELECT_DISC.
 * 3.18  Oct 31, 1996 -- Added module and DMA support.
 *                       
 *                       
 * 4.00  Nov 5, 1996   -- New ide-cd maintainer,
 *                                 Erik B. Andersen <andersee@debian.org>
 *                     -- Newer Creative drives don't always set the error
 *                          register correctly.  Make sure we see media changes
 *                          regardless.
 *                     -- Integrate with generic cdrom driver.
 *                     -- CDROMGETSPINDOWN and CDROMSETSPINDOWN ioctls, based on
 *                          a patch from Ciro Cattuto <>.
 *                     -- Call set_device_ro.
 *                     -- Implement CDROMMECHANISMSTATUS and CDROMSLOTTABLE
 *                          ioctls, based on patch by Erik Andersen
 *                     -- Add some probes of drive capability during setup.
 *
 * 4.01  Nov 11, 1996  -- Split into ide-cd.c and ide-cd.h
 *                     -- Removed CDROMMECHANISMSTATUS and CDROMSLOTTABLE 
 *                          ioctls in favor of a generalized approach 
 *                          using the generic cdrom driver.
 *                     -- Fully integrated with the 2.1.X kernel.
 *                     -- Other stuff that I forgot (lots of changes)
 *
 * 4.02  Dec 01, 1996  -- Applied patch from Gadi Oxman <gadio@netvision.net.il>
 *                          to fix the drive door locking problems.
 *
 * 4.03  Dec 04, 1996  -- Added DSC overlap support.
 * 4.04  Dec 29, 1996  -- Added CDROMREADRAW ioclt based on patch 
 *                          by Ales Makarov (xmakarov@sun.felk.cvut.cz)
 *
 * 4.05  Nov 20, 1997  -- Modified to print more drive info on init
 *                        Minor other changes
 *                        Fix errors on CDROMSTOP (If you have a "Dolphin",
 *                          you must define IHAVEADOLPHIN)
 *                        Added identifier so new Sanyo CD-changer works
 *                        Better detection if door locking isn't supported
 *
 * 4.06  Dec 17, 1997  -- fixed endless "tray open" messages  -ml
 * 4.07  Dec 17, 1997  -- fallback to set pc->stat on "tray open"
 * 4.08  Dec 18, 1997  -- spew less noise when tray is empty
 *                     -- fix speed display for ACER 24X, 18X
 * 4.09  Jan 04, 1998  -- fix handling of the last block so we return
 *                         an end of file instead of an I/O error (Gadi)
 * 4.10  Jan 24, 1998  -- fixed a bug so now changers can change to a new
 *                         slot when there is no disc in the current slot.
 *                     -- Fixed a memory leak where info->changer_info was
 *                         malloc'ed but never free'd when closing the device.
 *                     -- Cleaned up the global namespace a bit by making more
 *                         functions static that should already have been.
 * 4.11  Mar 12, 1998  -- Added support for the CDROM_SELECT_SPEED ioctl
 *                         based on a patch for 2.0.33 by Jelle Foks 
 *                         <jelle@scintilla.utwente.nl>, a patch for 2.0.33
 *                         by Toni Giorgino <toni@pcape2.pi.infn.it>, the SCSI
 *                         version, and my own efforts.  -erik
 *                     -- Fixed a stupid bug which egcs was kind enough to
 *                         inform me of where "Illegal mode for this track"
 *                         was never returned due to a comparison on data
 *                         types of limited range.
 * 4.12  Mar 29, 1998  -- Fixed bug in CDROM_SELECT_SPEED so write speed is 
 *                         now set ionly for CD-R and CD-RW drives.  I had 
 *                         removed this support because it produced errors.
 *                         It produced errors _only_ for non-writers. duh.
 * 4.13  May 05, 1998  -- Suppress useless "in progress of becoming ready"
 *                         messages, since this is not an error.
 *                     -- Change error messages to be const
 *                     -- Remove a "\t" which looks ugly in the syslogs
 * 4.14  July 17, 1998 -- Change to pointing to .ps version of ATAPI spec
 *                         since the .pdf version doesn't seem to work...
 *                     -- Updated the TODO list to something more current.
 *
 * 4.15  Aug 25, 1998  -- Updated ide-cd.h to respect mechine endianess, 
 *                         patch thanks to "Eddie C. Dost" <ecd@skynet.be>
 *
 * 4.50  Oct 19, 1998  -- New maintainers!
 *                         Jens Axboe <axboe@image.dk>
 *                         Chris Zwilling <chris@cloudnet.com>
 *
 * 4.51  Dec 23, 1998  -- Jens Axboe <axboe@image.dk>
 *                      - ide_cdrom_reset enabled since the ide subsystem
 *                         handles resets fine now. <axboe@image.dk>
 *                      - Transfer size fix for Samsung CD-ROMs, thanks to
 *                        "Ville Hallik" <ville.hallik@mail.ee>.
 *                      - other minor stuff.
 *
 * 4.52  Jan 19, 1999  -- Jens Axboe <axboe@image.dk>
 *                      - Detect DVD-ROM/RAM drives
 *
 *************************************************************************/

#define IDECD_VERSION "4.52"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include "ide.h"
#include "ide-cd.h"


/****************************************************************************
 * Generic packet command support and error handling routines.
 */


/* Mark that we've seen a media change, and invalidate our internal
   buffers. */
static void cdrom_saw_media_change (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	
	CDROM_STATE_FLAGS (drive)->media_changed = 1;
	CDROM_STATE_FLAGS (drive)->toc_valid = 0;
	info->nsectors_buffered = 0;
}


static
void cdrom_analyze_sense_data (ide_drive_t *drive, 
			       struct atapi_request_sense *reqbuf,
			       struct packet_command *failed_command)
{
	if (reqbuf->sense_key == NOT_READY ||
	    reqbuf->sense_key == UNIT_ATTENTION) {
		/* Make good and sure we've seen this potential media change.
		   Some drives (i.e. Creative) fail to present the correct
		   sense key in the error register. */
		cdrom_saw_media_change (drive);


		/* Don't print not ready or unit attention errors for
		   READ_SUBCHANNEL.  Workman (and probably other programs)
		   uses this command to poll the drive, and we don't want
		   to fill the syslog with useless errors. */
		if (failed_command &&
		    failed_command->c[0] == SCMD_READ_SUBCHANNEL)
			return;
	}
	if (reqbuf->error_code == 0x70 && reqbuf->sense_key  == 0x02
	 && ((reqbuf->asc        == 0x3a && reqbuf->ascq       == 0x00) ||
	     (reqbuf->asc        == 0x04 && reqbuf->ascq       == 0x01)))
	{
		/*
		 * Suppress the following errors:
		 * "Medium not present", and "in progress of becoming ready",
		 * to keep the noise level down to a dull roar.
		 */
		return;
	}

#if VERBOSE_IDE_CD_ERRORS
	{
		int i;
		const char *s;
		char buf[80];

		printk ("ATAPI device %s:\n", drive->name);
		if (reqbuf->error_code==0x70)
			printk("  Error: ");
		else if (reqbuf->error_code==0x71)
			printk("  Deferred Error: ");
		else
			printk("  Unknown Error Type: ");

		if ( reqbuf->sense_key < ARY_LEN (sense_key_texts))
			s = sense_key_texts[reqbuf->sense_key];
		else
			s = "bad sense key!";

		printk ("%s -- (Sense key=0x%02x)\n", s, reqbuf->sense_key);

		if (reqbuf->asc == 0x40) {
			sprintf (buf, "Diagnostic failure on component 0x%02x",
				 reqbuf->ascq);
			s = buf;
		} else {
			int lo=0, mid, hi=ARY_LEN (sense_data_texts);
			unsigned short key = (reqbuf->asc << 8);
			if ( ! (reqbuf->ascq >= 0x80 && reqbuf->ascq <= 0xdd) )
				key |= reqbuf->ascq;

			s = NULL;

			while (hi > lo) {
				mid = (lo + hi) / 2;
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

		printk ("  %s -- (asc=0x%02x, ascq=0x%02x)\n",
			s, reqbuf->asc, reqbuf->ascq);

		if (failed_command != NULL) {

			int lo=0, mid, hi= ARY_LEN (packet_command_texts);
			s = NULL;

			while (hi > lo) {
				mid = (lo + hi) / 2;
				if (packet_command_texts[mid].packet_command == failed_command->c[0]) {
					s = packet_command_texts[mid].text;
					break;
				}
				else if (packet_command_texts[mid].packet_command > failed_command->c[0])
					hi = mid;
				else
					lo = mid+1;
			}

			printk ("  The failed \"%s\" packet command was: \n  \"", s);
			for (i=0; i<sizeof (failed_command->c); i++)
				printk ("%02x ", failed_command->c[i]);
			printk ("\"\n");
		}

		if (reqbuf->sense_key == ILLEGAL_REQUEST &&
		    (reqbuf->sense_key_specific[0] & 0x80) != 0) {
			printk ("  Error in %s byte %d",
				(reqbuf->sense_key_specific[0] & 0x40) != 0
				? "command packet"
				: "command data",
				(reqbuf->sense_key_specific[1] << 8) +
				reqbuf->sense_key_specific[2]);

			if ((reqbuf->sense_key_specific[0] & 0x40) != 0) {
				printk (" bit %d",
					reqbuf->sense_key_specific[0] & 0x07);
			}

			printk ("\n");
		}
	}

#else /* not VERBOSE_IDE_CD_ERRORS */

	/* Suppress printing unit attention and `in progress of becoming ready'
	   errors when we're not being verbose. */

	if (reqbuf->sense_key == UNIT_ATTENTION ||
	    (reqbuf->sense_key == NOT_READY && (reqbuf->asc == 4 ||
						reqbuf->asc == 0x3a)))
		return;

	printk ("%s: error code: 0x%02x  sense_key: 0x%02x  asc: 0x%02x  ascq: 0x%02x\n",
		drive->name,
		reqbuf->error_code, reqbuf->sense_key,
		reqbuf->asc, reqbuf->ascq);
#endif /* not VERBOSE_IDE_CD_ERRORS */
}


/* Fix up a possibly partially-processed request so that we can
   start it over entirely, or even put it back on the request queue. */
static void restore_request (struct request *rq)
{
	if (rq->buffer != rq->bh->b_data) {
		int n = (rq->buffer - rq->bh->b_data) / SECTOR_SIZE;
		rq->buffer = rq->bh->b_data;
		rq->nr_sectors += n;
		rq->sector -= n;
	}
	rq->current_nr_sectors = rq->bh->b_size >> SECTOR_BITS;
}


static void cdrom_queue_request_sense (ide_drive_t *drive, 
				       struct semaphore *sem,
				       struct atapi_request_sense *reqbuf,
				       struct packet_command *failed_command)
{
	struct cdrom_info *info = drive->driver_data;
	struct request *rq;
	struct packet_command *pc;
	int len;

	/* If the request didn't explicitly specify where
	   to put the sense data, use the statically allocated structure. */
	if (reqbuf == NULL)
		reqbuf = &info->sense_data;

	/* Make up a new request to retrieve sense information. */

	pc = &info->request_sense_pc;
	memset (pc, 0, sizeof (*pc));

	/* The request_sense structure has an odd number of (16-bit) words,
	   which won't work well with 32-bit transfers.  However, we don't care
	   about the last two bytes, so just truncate the structure down
	   to an even length. */
	len = sizeof (*reqbuf) / 4;
	len *= 4;

	pc->c[0] = REQUEST_SENSE;
	pc->c[4] = (unsigned char) len;
	pc->buffer = (char *)reqbuf;
	pc->buflen = len;
	pc->sense_data = (struct atapi_request_sense *)failed_command;

	/* stuff the sense request in front of our current request */

	rq = &info->request_sense_request;
	ide_init_drive_cmd (rq);
	rq->cmd = REQUEST_SENSE_COMMAND;
	rq->buffer = (char *)pc;
	rq->sem = sem;
	(void) ide_do_drive_cmd (drive, rq, ide_preempt);
}


static void cdrom_end_request (int uptodate, ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;

	if (rq->cmd == REQUEST_SENSE_COMMAND && uptodate) {
		struct packet_command *pc = (struct packet_command *)
			                      rq->buffer;
		cdrom_analyze_sense_data (drive,
					  (struct atapi_request_sense *)
					  	(pc->buffer - pc->c[4]), 
					  (struct packet_command *)
					  	pc->sense_data);
	}
	if (rq->cmd == READ && !rq->current_nr_sectors)
		uptodate = 1;
	ide_end_request (uptodate, HWGROUP(drive));
}


/* Returns 0 if the request should be continued.
   Returns 1 if the request was ended. */
static int cdrom_decode_status (ide_drive_t *drive, int good_stat,
				int *stat_ret)
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
		printk ("%s: missing request in cdrom_decode_status\n",
			drive->name);
	else {
		cmd = rq->cmd;

		if (cmd == REQUEST_SENSE_COMMAND) {
			/* We got an error trying to get sense info
			   from the drive (probably while trying
			   to recover from a former error).  Just give up. */

			struct packet_command *pc = (struct packet_command *)
				                      rq->buffer;
			pc->stat = 1;
			cdrom_end_request (1, drive);
			ide_error (drive, "request sense failure", stat);
			return 1;

		} else if (cmd == PACKET_COMMAND) {
			/* All other functions, except for READ. */

			struct packet_command *pc = (struct packet_command *)
				                      rq->buffer;
			struct semaphore *sem = NULL;

			/* Check for tray open. */
			if (sense_key == NOT_READY) {
				cdrom_saw_media_change (drive);
#if 0	/* let the upper layers do the complaining */
				/* Print an error message to the syslog.
				   Exception: don't print anything if this
				   is a read subchannel command.  This is
				   because workman constantly polls the drive
				   with this command, and we don't want
				   to uselessly fill up the syslog. */
				if (pc->c[0] != SCMD_READ_SUBCHANNEL)
					printk ("%s: tray open or drive not ready\n", drive->name);
#endif
			} else if (sense_key == UNIT_ATTENTION) {
				/* Check for media change. */
				cdrom_saw_media_change (drive);
				/*printk("%s: media changed\n",drive->name);*/
				return 0;
			} else {
				/* Otherwise, print an error. */
				ide_dump_status (drive, "packet command error",
						 stat);
			}
			
			/* Set the error flag and complete the request.
			   Then, if we have a CHECK CONDITION status,
			   queue a request sense command.  We must be careful,
			   though: we don't want the thread in
			   cdrom_queue_packet_command to wake up until
			   the request sense has completed.  We do this
			   by transferring the semaphore from the packet
			   command request to the request sense request. */

			if ((stat & ERR_STAT) != 0) {
				sem = rq->sem;
				rq->sem = NULL;
			}

			pc->stat = 1;
			cdrom_end_request (1, drive);

			if ((stat & ERR_STAT) != 0)
				cdrom_queue_request_sense (drive, sem,
							   pc->sense_data, pc);
		} else {
			/* Handle errors from READ requests. */

			if (sense_key == NOT_READY) {
				/* Tray open. */
				cdrom_saw_media_change (drive);

				/* Fail the request. */
				printk ("%s: tray open\n", drive->name);
				cdrom_end_request (0, drive);
			} else if (sense_key == UNIT_ATTENTION) {
				/* Media change. */
				cdrom_saw_media_change (drive);

				/* Arrange to retry the request.
				   But be sure to give up if we've retried
				   too many times. */
				if (++rq->errors > ERROR_MAX)
					cdrom_end_request (0, drive);
			} else if (sense_key == ILLEGAL_REQUEST ||
				   sense_key == DATA_PROTECT) {
				/* No point in retrying after an illegal
				   request or data protect error.*/
				ide_dump_status (drive, "command error", stat);
				cdrom_end_request (0, drive);
			} else if ((err & ~ABRT_ERR) != 0) {
				/* Go to the default handler
				   for other errors. */
				ide_error (drive, "cdrom_decode_status", stat);
				return 1;
			} else if ((++rq->errors > ERROR_MAX)) {
				/* We've racked up too many retries.  Abort. */
				cdrom_end_request (0, drive);
			}

			/* If we got a CHECK_CONDITION status,
			   queue a request sense command. */
			if ((stat & ERR_STAT) != 0)
				cdrom_queue_request_sense (drive,
							   NULL, NULL, NULL);
		}
	}

	/* Retry, or handle the next request. */
	return 1;
}


/* Set up the device registers for transferring a packet command on DEV,
   expecting to later transfer XFERLEN bytes.  HANDLER is the routine
   which actually transfers the command to the drive.  If this is a
   drq_interrupt device, this routine will arrange for HANDLER to be
   called when the interrupt from the drive arrives.  Otherwise, HANDLER
   will be called immediately after the drive is prepared for the transfer. */

static int cdrom_start_packet_command (ide_drive_t *drive, int xferlen,
				       ide_handler_t *handler)
{
	struct cdrom_info *info = drive->driver_data;

	/* Wait for the controller to be idle. */
	if (ide_wait_stat (drive, 0, BUSY_STAT, WAIT_READY)) return 1;

	if (info->dma)
		info->dma = !HWIF(drive)->dmaproc(ide_dma_read, drive);

	/* Set up the controller registers. */
	OUT_BYTE (info->dma, IDE_FEATURE_REG);
	OUT_BYTE (0, IDE_NSECTOR_REG);
	OUT_BYTE (0, IDE_SECTOR_REG);

	OUT_BYTE (xferlen & 0xff, IDE_LCYL_REG);
	OUT_BYTE (xferlen >> 8  , IDE_HCYL_REG);
	OUT_BYTE (drive->ctl, IDE_CONTROL_REG);
 
	if (info->dma)
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));

	if (CDROM_CONFIG_FLAGS (drive)->drq_interrupt) {
		ide_set_handler (drive, handler, WAIT_CMD);
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG); /* packet command */
	} else {
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG); /* packet command */
		(*handler) (drive);
	}

	return 0;
}


/* Send a packet command to DRIVE described by CMD_BUF and CMD_LEN.
   The device registers must have already been prepared
   by cdrom_start_packet_command.
   HANDLER is the interrupt handler to call when the command completes
   or there's data ready. */
static int cdrom_transfer_packet_command (ide_drive_t *drive,
                                          char *cmd_buf, int cmd_len,
					  ide_handler_t *handler)
{
	if (CDROM_CONFIG_FLAGS (drive)->drq_interrupt) {
		/* Here we should have been called after receiving an interrupt
		   from the device.  DRQ should how be set. */
		int stat_dum;

		/* Check for errors. */
		if (cdrom_decode_status (drive, DRQ_STAT, &stat_dum))
			return 1;
	} else {
		/* Otherwise, we must wait for DRQ to get set. */
		if (ide_wait_stat (drive, DRQ_STAT, BUSY_STAT, WAIT_READY))
			return 1;
	}

	/* Arm the interrupt handler. */
	ide_set_handler (drive, handler, WAIT_CMD);

	/* Send the command to the device. */
	atapi_output_bytes (drive, cmd_buf, cmd_len);

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
	struct cdrom_info *info = drive->driver_data;

	/* Number of sectors to read into the buffer. */
	int sectors_to_buffer = MIN (sectors_to_transfer,
				     (SECTOR_BUFFER_SIZE >> SECTOR_BITS) -
				       info->nsectors_buffered);

	char *dest;

	/* If we don't yet have a sector buffer, try to allocate one.
	   If we can't get one atomically, it's not fatal -- we'll just throw
	   the data away rather than caching it. */
	if (info->sector_buffer == NULL) {
		info->sector_buffer = (char *) kmalloc (SECTOR_BUFFER_SIZE,
							GFP_ATOMIC);

		/* If we couldn't get a buffer,
		   don't try to buffer anything... */
		if (info->sector_buffer == NULL)
			sectors_to_buffer = 0;
	}

	/* If this is the first sector in the buffer, remember its number. */
	if (info->nsectors_buffered == 0)
		info->sector_buffered = sector;

	/* Read the data into the buffer. */
	dest = info->sector_buffer + info->nsectors_buffered * SECTOR_SIZE;
	while (sectors_to_buffer > 0) {
		atapi_input_bytes (drive, dest, SECTOR_SIZE);
		--sectors_to_buffer;
		--sectors_to_transfer;
		++info->nsectors_buffered;
		dest += SECTOR_SIZE;
	}

	/* Throw away any remaining data. */
	while (sectors_to_transfer > 0) {
		char dum[SECTOR_SIZE];
		atapi_input_bytes (drive, dum, sizeof (dum));
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

	if (ireason == 0) {
		/* Whoops... The drive is expecting to receive data from us! */
		printk ("%s: cdrom_read_intr: "
			"Drive wants to transfer data the wrong way!\n",
			drive->name);

		/* Throw some data at the drive so it doesn't hang
		   and quit this request. */
		while (len > 0) {
			int dum = 0;
			atapi_output_bytes (drive, &dum, sizeof (dum));
			len -= sizeof (dum);
		}
	} else {
		/* Drive wants a command packet, or invalid ireason... */
		printk ("%s: cdrom_read_intr: bad interrupt reason %d\n",
			drive->name, ireason);
	}

	cdrom_end_request (0, drive);
	return -1;
}


/*
 * Interrupt routine.  Called when a read request has completed.
 */
static void cdrom_read_intr (ide_drive_t *drive)
{
	int stat;
	int ireason, len, sectors_to_transfer, nskip;
	struct cdrom_info *info = drive->driver_data;
	int i, dma = info->dma, dma_error = 0;

	struct request *rq = HWGROUP(drive)->rq;

	/* Check for errors. */
	if (dma) {
		info->dma = 0;
		if ((dma_error = HWIF(drive)->dmaproc(ide_dma_end, drive)))
			HWIF(drive)->dmaproc(ide_dma_off, drive);
	}

	if (cdrom_decode_status (drive, 0, &stat))
		return;
 
	if (dma) {
		if (!dma_error) {
			for (i = rq->nr_sectors; i > 0;) {
				i -= rq->current_nr_sectors;
				ide_end_request(1, HWGROUP(drive));
			}
		} else
			ide_error (drive, "dma error", stat);
		return;
	}

	/* Read the interrupt reason and the transfer length. */
	ireason = IN_BYTE (IDE_NSECTOR_REG);
	len = IN_BYTE (IDE_LCYL_REG) + 256 * IN_BYTE (IDE_HCYL_REG);

	/* If DRQ is clear, the command has completed. */
	if ((stat & DRQ_STAT) == 0) {
		/* If we're not done filling the current buffer, complain.
		   Otherwise, complete the command normally. */
		if (rq->current_nr_sectors > 0) {
			printk ("%s: cdrom_read_intr: data underrun (%ld blocks)\n",
				drive->name, rq->current_nr_sectors);
			cdrom_end_request (0, drive);
		} else
			cdrom_end_request (1, drive);
		return;
	}

	/* Check that the drive is expecting to do the same thing we are. */
	if (cdrom_read_check_ireason (drive, len, ireason)) return;

	/* Assume that the drive will always provide data in multiples
	   of at least SECTOR_SIZE, as it gets hairy to keep track
	   of the transfers otherwise. */
	if ((len % SECTOR_SIZE) != 0) {
		printk ("%s: cdrom_read_intr: Bad transfer size %d\n",
			drive->name, len);
		if (CDROM_CONFIG_FLAGS (drive)->limit_nframes)
			printk ("  This drive is not supported by this version of the driver\n");
		else {
			printk ("  Trying to limit transfer sizes\n");
			CDROM_CONFIG_FLAGS (drive)->limit_nframes = 1;
		}
		cdrom_end_request (0, drive);
		return;
	}

	/* The number of sectors we need to read from the drive. */
	sectors_to_transfer = len / SECTOR_SIZE;

	/* First, figure out if we need to bit-bucket
	   any of the leading sectors. */
	nskip = MIN ((int)(rq->current_nr_sectors -
			   (rq->bh->b_size >> SECTOR_BITS)),
		     sectors_to_transfer);

	while (nskip > 0) {
		/* We need to throw away a sector. */
		char dum[SECTOR_SIZE];
		atapi_input_bytes (drive, dum, sizeof (dum));

		--rq->current_nr_sectors;
		--nskip;
		--sectors_to_transfer;
	}

	/* Now loop while we still have data to read from the drive. */
	while (sectors_to_transfer > 0) {
		int this_transfer;

		/* If we've filled the present buffer but there's another
		   chained buffer after it, move on. */
		if (rq->current_nr_sectors == 0 &&
		    rq->nr_sectors > 0)
			cdrom_end_request (1, drive);

		/* If the buffers are full, cache the rest of the data in our
		   internal buffer. */
		if (rq->current_nr_sectors == 0) {
			cdrom_buffer_sectors (drive,
					      rq->sector, sectors_to_transfer);
			sectors_to_transfer = 0;
		} else {
			/* Transfer data to the buffers.
			   Figure out how many sectors we can transfer
			   to the current buffer. */
			this_transfer = MIN (sectors_to_transfer,
					     rq->current_nr_sectors);

			/* Read this_transfer sectors
			   into the current buffer. */
			while (this_transfer > 0) {
				atapi_input_bytes (drive,
						   rq->buffer, SECTOR_SIZE);
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
	ide_set_handler (drive, &cdrom_read_intr, WAIT_CMD);
}


/*
 * Try to satisfy some of the current read request from our cached data.
 * Returns nonzero if the request has been completed, zero otherwise.
 */
static int cdrom_read_from_buffer (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;

	/* Can't do anything if there's no buffer. */
	if (info->sector_buffer == NULL) return 0;

	/* Loop while this request needs data and the next block is present
	   in our cache. */
	while (rq->nr_sectors > 0 &&
	       rq->sector >= info->sector_buffered &&
	       rq->sector < info->sector_buffered + info->nsectors_buffered) {
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

	/* If we've satisfied the current request,
	   terminate it successfully. */
	if (rq->nr_sectors == 0) {
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
	    (rq->sector % SECTORS_PER_FRAME) != 0) {
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
static void cdrom_start_read_continuation (ide_drive_t *drive)
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
	   and remember to skip the first few sectors.
	   If the CURRENT_NR_SECTORS field is larger than the size
	   of the buffer, it will mean that we're to skip a number
	   of sectors equal to the amount by which CURRENT_NR_SECTORS
	   is larger than the buffer size. */
	nskip = (sector % SECTORS_PER_FRAME);
	if (nskip > 0) {
		/* Sanity check... */
		if (rq->current_nr_sectors !=
		    (rq->bh->b_size >> SECTOR_BITS)) {
			printk ("%s: cdrom_start_read_continuation: buffer botch (%ld)\n",
				drive->name, rq->current_nr_sectors);
			cdrom_end_request (0, drive);
			return;
		}

		sector -= nskip;
		nsect += nskip;
		rq->current_nr_sectors += nskip;
	}

	/* Convert from sectors to cdrom blocks, rounding up the transfer
	   length if needed. */
	nframes = (nsect + SECTORS_PER_FRAME-1) / SECTORS_PER_FRAME;
	frame = sector / SECTORS_PER_FRAME;

	/* Largest number of frames was can transfer at once is 64k-1. For
	   some drives we need to limit this even more. */
	nframes = MIN (nframes, (CDROM_CONFIG_FLAGS (drive)->limit_nframes) ?
		(65534 / CD_FRAMESIZE) : 65535);

	/* Set up the command */
	memset (&pc.c, 0, sizeof (pc.c));
	pc.c[0] = READ_10;
	pc.c[7] = (nframes >> 8);
	pc.c[8] = (nframes & 0xff);
	put_unaligned(htonl (frame), (unsigned int *) &pc.c[2]);

	/* Send the command to the drive and return. */
	(void) cdrom_transfer_packet_command (drive, pc.c, sizeof (pc.c),
					      &cdrom_read_intr);
}

#define IDECD_SEEK_THRESHOLD	(1000)			/* 1000 blocks */
#define IDECD_SEEK_TIMER	(2 * WAIT_MIN_SLEEP)	/* 40 ms */
#define IDECD_SEEK_TIMEOUT     WAIT_CMD                /* 10 sec */

static void cdrom_seek_intr (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	int stat;
	static int retry = 10;

	if (cdrom_decode_status (drive, 0, &stat))
		return;
	CDROM_CONFIG_FLAGS(drive)->seeking = 1;

	if (retry && jiffies - info->start_seek > IDECD_SEEK_TIMER) {
		if (--retry == 0) {
			printk ("%s: disabled DSC seek overlap\n", drive->name);
			drive->dsc_overlap = 0;
		}
	}
}

static void cdrom_start_seek_continuation (ide_drive_t *drive)
{
	struct packet_command pc;
	struct request *rq = HWGROUP(drive)->rq;
	int sector, frame, nskip;

	sector = rq->sector;
	nskip = (sector % SECTORS_PER_FRAME);
	if (nskip > 0)
		sector -= nskip;
	frame = sector / SECTORS_PER_FRAME;

	memset (&pc.c, 0, sizeof (pc.c));
	pc.c[0] = SEEK;
	put_unaligned(htonl (frame), (unsigned int *) &pc.c[2]);
	(void) cdrom_transfer_packet_command (drive, pc.c, sizeof (pc.c), &cdrom_seek_intr);
}

static void cdrom_start_seek (ide_drive_t *drive, unsigned int block)
{
	struct cdrom_info *info = drive->driver_data;

	info->dma = 0;
	info->start_seek = jiffies;
	cdrom_start_packet_command (drive, 0, cdrom_start_seek_continuation);
}

/*
 * Start a read request from the CD-ROM.
 */
static void cdrom_start_read (ide_drive_t *drive, unsigned int block)
{
	struct cdrom_info *info = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	int minor = MINOR (rq->rq_dev);

	/* If the request is relative to a partition, fix it up to refer to the
	   absolute address.  */
	if ((minor & PARTN_MASK) != 0) {
		rq->sector = block;
		minor &= ~PARTN_MASK;
		rq->rq_dev = MKDEV (MAJOR(rq->rq_dev), minor);
	}

	/* We may be retrying this request after an error.  Fix up
	   any weirdness which might be present in the request packet. */
	restore_request (rq);

	/* Satisfy whatever we can of this request from our cached sector. */
	if (cdrom_read_from_buffer (drive))
		return;

	/* Clear the local sector buffer. */
	info->nsectors_buffered = 0;

	if (drive->using_dma && (rq->sector % SECTORS_PER_FRAME == 0) && (rq->nr_sectors % SECTORS_PER_FRAME == 0))
		info->dma = 1;
	else
		info->dma = 0;

	/* Start sending the read request to the drive. */
	cdrom_start_packet_command (drive, 32768,
				    cdrom_start_read_continuation);
}




/****************************************************************************
 * Execute all other packet commands.
 */

/* Forward declarations. */
static int
cdrom_lockdoor (ide_drive_t *drive, int lockflag,
		struct atapi_request_sense *reqbuf);



/* Interrupt routine for packet command completion. */
static void cdrom_pc_intr (ide_drive_t *drive)
{
	int ireason, len, stat, thislen;
	struct request *rq = HWGROUP(drive)->rq;
	struct packet_command *pc = (struct packet_command *)rq->buffer;

	/* Check for errors. */
	if (cdrom_decode_status (drive, 0, &stat))
		return;

	/* Read the interrupt reason and the transfer length. */
	ireason = IN_BYTE (IDE_NSECTOR_REG);
	len = IN_BYTE (IDE_LCYL_REG) + 256 * IN_BYTE (IDE_HCYL_REG);

	/* If DRQ is clear, the command has completed.
	   Complain if we still have data left to transfer. */
	if ((stat & DRQ_STAT) == 0) {
		/* Some of the trailing request sense fields are optional, and
		   some drives don't send them.  Sigh. */
		if (pc->c[0] == REQUEST_SENSE &&
		    pc->buflen > 0 &&
		    pc->buflen <= 5) {
			while (pc->buflen > 0) {
				*pc->buffer++ = 0;
				--pc->buflen;
			}
		}

		if (pc->buflen == 0)
			cdrom_end_request (1, drive);
		else {
			/* Comment this out, because this always happens 
			   right after a reset occurs, and it is annoying to 
			   always print expected stuff.  */
			/*
			printk ("%s: cdrom_pc_intr: data underrun %d\n",
				drive->name, pc->buflen);
			*/
			pc->stat = 1;
			cdrom_end_request (1, drive);
		}
		return;
	}

	/* Figure out how much data to transfer. */
	thislen = pc->buflen;
	if (thislen < 0) thislen = -thislen;
	if (thislen > len) thislen = len;

	/* The drive wants to be written to. */
	if ((ireason & 3) == 0) {
		/* Check that we want to write. */
		if (pc->buflen > 0) {
			printk ("%s: cdrom_pc_intr: Drive wants "
				"to transfer data the wrong way!\n",
				drive->name);
			pc->stat = 1;
			thislen = 0;
		}

		/* Transfer the data. */
		atapi_output_bytes (drive, pc->buffer, thislen);

		/* If we haven't moved enough data to satisfy the drive,
		   add some padding. */
		while (len > thislen) {
			int dum = 0;
			atapi_output_bytes (drive, &dum, sizeof (dum));
			len -= sizeof (dum);
		}

		/* Keep count of how much data we've moved. */
		pc->buffer += thislen;
		pc->buflen += thislen;
	}

	/* Same drill for reading. */
	else if ((ireason & 3) == 2) {
		/* Check that we want to read. */
		if (pc->buflen < 0) {
			printk ("%s: cdrom_pc_intr: Drive wants to "
				"transfer data the wrong way!\n",
				drive->name);
			pc->stat = 1;
			thislen = 0;
		}

		/* Transfer the data. */
		atapi_input_bytes (drive, pc->buffer, thislen);

		/* If we haven't moved enough data to satisfy the drive,
		   add some padding. */
		while (len > thislen) {
			int dum = 0;
			atapi_input_bytes (drive, &dum, sizeof (dum));
			len -= sizeof (dum);
		}

		/* Keep count of how much data we've moved. */
		pc->buffer += thislen;
		pc->buflen -= thislen;
	} else {
		printk ("%s: cdrom_pc_intr: The drive "
			"appears confused (ireason = 0x%2x)\n",
			drive->name, ireason);
		pc->stat = 1;
	}

	/* Now we wait for another interrupt. */
	ide_set_handler (drive, &cdrom_pc_intr, WAIT_CMD);
}


static void cdrom_do_pc_continuation (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	struct packet_command *pc = (struct packet_command *)rq->buffer;

	/* Send the command to the drive and return. */
	cdrom_transfer_packet_command (drive, pc->c,
				       sizeof (pc->c), &cdrom_pc_intr);
}


static void cdrom_do_packet_command (ide_drive_t *drive)
{
	int len;
	struct request *rq = HWGROUP(drive)->rq;
	struct packet_command *pc = (struct packet_command *)rq->buffer;
	struct cdrom_info *info = drive->driver_data;

	info->dma = 0;

	len = pc->buflen;
	if (len < 0) len = -len;

	pc->stat = 0;

	/* Start sending the command to the drive. */
	cdrom_start_packet_command (drive, len, cdrom_do_pc_continuation);
}


/* Sleep for TIME jiffies.
   Not to be called from an interrupt handler. */
static
void cdrom_sleep (int time)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(time);
}

static
int cdrom_queue_packet_command (ide_drive_t *drive, struct packet_command *pc)
{
	struct atapi_request_sense my_reqbuf;
	int retries = 10;
	struct request req;

	/* If our caller has not provided a place to stick any sense data,
	   use our own area. */
	if (pc->sense_data == NULL)
		pc->sense_data = &my_reqbuf;
	pc->sense_data->sense_key = 0;

	/* Start of retry loop. */
	do {
		ide_init_drive_cmd (&req);
		req.cmd = PACKET_COMMAND;
		req.buffer = (char *)pc;
		if (ide_do_drive_cmd (drive, &req, ide_wait)) {
			printk("%s: do_drive_cmd returned stat=%02x,err=%02x\n",
				drive->name, req.buffer[0], req.buffer[1]);
			/* FIXME: we should probably abort/retry or something */
		}
		if (pc->stat != 0) {
			/* The request failed.  Retry if it was due to a unit
			   attention status
			   (usually means media was changed). */
			struct atapi_request_sense *reqbuf = pc->sense_data;

			if (reqbuf->sense_key == UNIT_ATTENTION)
				cdrom_saw_media_change (drive);
			else if (reqbuf->sense_key == NOT_READY &&
				 reqbuf->asc == 4) {
				/* The drive is in the process of loading
				   a disk.  Retry, but wait a little to give
				   the drive time to complete the load. */
				cdrom_sleep (HZ);
			} else
				/* Otherwise, don't retry. */
				retries = 0;

			--retries;
		}

		/* End of retry loop. */
	} while (pc->stat != 0 && retries >= 0);


	/* Return an error if the command failed. */
	if (pc->stat != 0)
		return -EIO;
	else {
		/* The command succeeded.  If it was anything other than
		   a request sense, eject, or door lock command,
		   and we think that the door is presently, lock it again.
		   (The door was probably unlocked via an explicit
		   CDROMEJECT ioctl.) */
		if (CDROM_STATE_FLAGS (drive)->door_locked == 0 && drive->usage &&
		    (pc->c[0] != REQUEST_SENSE &&
		     pc->c[0] != ALLOW_MEDIUM_REMOVAL &&
		     pc->c[0] != START_STOP)) {
			(void) cdrom_lockdoor (drive, 1, NULL);
		}
		return 0;
	}
}


/****************************************************************************
 * cdrom driver request routine.
 */
static
void ide_do_rw_cdrom (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	if (rq -> cmd == PACKET_COMMAND || rq -> cmd == REQUEST_SENSE_COMMAND)
		cdrom_do_packet_command (drive);
	else if (rq -> cmd == RESET_DRIVE_COMMAND) {
		cdrom_end_request (1, drive);
		ide_do_reset (drive);
		return;
	} else if (rq -> cmd != READ) {
		printk ("ide-cd: bad cmd %d\n", rq -> cmd);
		cdrom_end_request (0, drive);
	} else {
		struct cdrom_info *info = drive->driver_data;

		if (CDROM_CONFIG_FLAGS(drive)->seeking) {
			unsigned long elpased = jiffies - info->start_seek;
			int stat = GET_STAT();

			if ((stat & SEEK_STAT) != SEEK_STAT) {
				if (elpased < IDECD_SEEK_TIMEOUT) {
					ide_stall_queue (drive, IDECD_SEEK_TIMER);
					return;
				}
				printk ("%s: DSC timeout\n", drive->name);
			}
			CDROM_CONFIG_FLAGS(drive)->seeking = 0;
		}
		if (IDE_LARGE_SEEK(info->last_block, block, IDECD_SEEK_THRESHOLD) && drive->dsc_overlap)
			cdrom_start_seek (drive, block);
		else
			cdrom_start_read (drive, block);
		info->last_block = block;
	}
}



/****************************************************************************
 * Ioctl handling.
 *
 * Routines which queue packet commands take as a final argument a pointer
 * to an atapi_request_sense struct.  If execution of the command results
 * in an error with a CHECK CONDITION status, this structure will be filled
 * with the results of the subsequent request sense command.  The pointer
 * can also be NULL, in which case no sense information is returned.
 */

#if ! STANDARD_ATAPI
static inline
int bin2bcd (int x)
{
	return (x%10) | ((x/10) << 4);
}


static inline
int bcd2bin (int x)
{
	return (x >> 4) * 10 + (x & 0x0f);
}

static
void msf_from_bcd (struct atapi_msf *msf)
{
	msf->minute = bcd2bin (msf->minute);
	msf->second = bcd2bin (msf->second);
	msf->frame  = bcd2bin (msf->frame);
}

#endif /* not STANDARD_ATAPI */


static inline
void lba_to_msf (int lba, byte *m, byte *s, byte *f)
{
	lba += CD_MSF_OFFSET;
	lba &= 0xffffff;  /* negative lbas use only 24 bits */
	*m = lba / (CD_SECS * CD_FRAMES);
	lba %= (CD_SECS * CD_FRAMES);
	*s = lba / CD_FRAMES;
	*f = lba % CD_FRAMES;
}


static inline
int msf_to_lba (byte m, byte s, byte f)
{
	return (((m * CD_SECS) + s) * CD_FRAMES + f) - CD_MSF_OFFSET;
}


static int
cdrom_check_status (ide_drive_t  *drive,
		    struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));

	pc.sense_data = reqbuf;
	pc.c[0] = TEST_UNIT_READY;

#if ! STANDARD_ATAPI
        /* the Sanyo 3 CD changer uses byte 7 of TEST_UNIT_READY to 
           switch CDs instead of supporting the LOAD_UNLOAD opcode   */

        pc.c[7] = CDROM_STATE_FLAGS (drive)->sanyo_slot % 3;
#endif /* not STANDARD_ATAPI */

	return cdrom_queue_packet_command (drive, &pc);
}


/* Lock the door if LOCKFLAG is nonzero; unlock it otherwise. */
static int
cdrom_lockdoor (ide_drive_t *drive, int lockflag,
		struct atapi_request_sense *reqbuf)
{
	struct atapi_request_sense my_reqbuf;
	int stat;
	struct packet_command pc;

	if (reqbuf == NULL)
		reqbuf = &my_reqbuf;

	/* If the drive cannot lock the door, just pretend. */
	if (CDROM_CONFIG_FLAGS (drive)->no_doorlock)
		stat = 0;
	else {
		memset (&pc, 0, sizeof (pc));
		pc.sense_data = reqbuf;

		pc.c[0] = ALLOW_MEDIUM_REMOVAL;
		pc.c[4] = (lockflag != 0);
		stat = cdrom_queue_packet_command (drive, &pc);
	}

	/* If we got an illegal field error, the drive
	   probably cannot lock the door. */
	if (stat != 0 &&
	    reqbuf->sense_key == ILLEGAL_REQUEST &&
	    (reqbuf->asc == 0x24 || reqbuf->asc == 0x20)) {
		printk ("%s: door locking not supported\n",
			drive->name);
		CDROM_CONFIG_FLAGS (drive)->no_doorlock = 1;
		stat = 0;
	}

	if (stat == 0)
		CDROM_STATE_FLAGS (drive)->door_locked = lockflag;

	return stat;
}


/* Eject the disk if EJECTFLAG is 0.
   If EJECTFLAG is 1, try to reload the disk. */
static int
cdrom_eject (ide_drive_t *drive, int ejectflag,
	     struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	if (CDROM_CONFIG_FLAGS (drive)->no_eject==1 && ejectflag==0)
		return -EDRIVE_CANT_DO_THIS;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.c[0] = START_STOP;
	pc.c[4] = 2 + (ejectflag != 0);
	return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_pause (ide_drive_t *drive, int pauseflag,
	     struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.c[0] = SCMD_PAUSE_RESUME;
	pc.c[8] = !pauseflag;
	return cdrom_queue_packet_command (drive, &pc);
}


static int
cdrom_startstop (ide_drive_t *drive, int startflag,
		 struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.c[0] = START_STOP;
	pc.c[1] = 1;
	pc.c[4] = startflag;
	return cdrom_queue_packet_command (drive, &pc);
}

static int
cdrom_read_capacity (ide_drive_t *drive, unsigned *capacity,
		     struct atapi_request_sense *reqbuf)
{
	struct {
		unsigned lba;
		unsigned blocklen;
	} capbuf;

	int stat;
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.c[0] = READ_CAPACITY;
	pc.buffer = (char *)&capbuf;
	pc.buflen = sizeof (capbuf);

	stat = cdrom_queue_packet_command (drive, &pc);
	if (stat == 0)
		*capacity = ntohl (capbuf.lba);

	return stat;
}


static int
cdrom_read_tocentry (ide_drive_t *drive, int trackno, int msf_flag,
                     int format, char *buf, int buflen,
		     struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.buffer =  buf;
	pc.buflen = buflen;
	pc.c[0] = SCMD_READ_TOC;
	pc.c[6] = trackno;
	pc.c[7] = (buflen >> 8);
	pc.c[8] = (buflen & 0xff);
	pc.c[9] = (format << 6);
	if (msf_flag) pc.c[1] = 2;
	return cdrom_queue_packet_command (drive, &pc);
}


/* Try to read the entire TOC for the disk into our internal buffer. */
static int
cdrom_read_toc (ide_drive_t *drive,
		struct atapi_request_sense *reqbuf)
{
	int stat, ntracks, i;
	struct cdrom_info *info = drive->driver_data;
	struct atapi_toc *toc = info->toc;
	struct {
		struct atapi_toc_header hdr;
		struct atapi_toc_entry  ent;
	} ms_tmp;

	if (toc == NULL) {
		/* Try to allocate space. */
		toc = (struct atapi_toc *) kmalloc (sizeof (struct atapi_toc),
						    GFP_KERNEL);
		info->toc = toc;
	}

	if (toc == NULL) {
		printk ("%s: No cdrom TOC buffer!\n", drive->name);
		return -EIO;
	}

	/* Check to see if the existing data is still valid.
	   If it is, just return. */
	if (CDROM_STATE_FLAGS (drive)->toc_valid)
		(void) cdrom_check_status (drive, NULL);

	if (CDROM_STATE_FLAGS (drive)->toc_valid) return 0;

	/* First read just the header, so we know how long the TOC is. */
	stat = cdrom_read_tocentry (drive, 0, 1, 0, (char *)&toc->hdr,
				    sizeof (struct atapi_toc_header) +
				    sizeof (struct atapi_toc_entry),
				    reqbuf);
	if (stat) return stat;

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd) {
		toc->hdr.first_track = bcd2bin (toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin (toc->hdr.last_track);
	}
#endif  /* not STANDARD_ATAPI */

	ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;
	if (ntracks <= 0) return -EIO;
	if (ntracks > MAX_TRACKS) ntracks = MAX_TRACKS;

	/* Now read the whole schmeer. */
	stat = cdrom_read_tocentry (drive, 0, 1, 0, (char *)&toc->hdr,
				    sizeof (struct atapi_toc_header) +
				    (ntracks+1) *
				      sizeof (struct atapi_toc_entry),
				    reqbuf);
	if (stat) return stat;
	toc->hdr.toc_length = ntohs (toc->hdr.toc_length);

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd) {
		toc->hdr.first_track = bcd2bin (toc->hdr.first_track);
		toc->hdr.last_track  = bcd2bin (toc->hdr.last_track);
	}
#endif  /* not STANDARD_ATAPI */

	for (i=0; i<=ntracks; i++) {
#if ! STANDARD_ATAPI
		if (CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd) {
			if (CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd)
				toc->ent[i].track = bcd2bin (toc->ent[i].track);
			msf_from_bcd (&toc->ent[i].addr.msf);
		}
#endif  /* not STANDARD_ATAPI */
		toc->ent[i].addr.lba = msf_to_lba (toc->ent[i].addr.msf.minute,
						   toc->ent[i].addr.msf.second,
						   toc->ent[i].addr.msf.frame);
	}

	/* Read the multisession information. */
	stat = cdrom_read_tocentry (drive, 0, 1, 1,
				    (char *)&ms_tmp, sizeof (ms_tmp),
				    reqbuf);
	if (stat) return stat;

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd)
		msf_from_bcd (&ms_tmp.ent.addr.msf);
#endif  /* not STANDARD_ATAPI */

	toc->last_session_lba = msf_to_lba (ms_tmp.ent.addr.msf.minute,
					    ms_tmp.ent.addr.msf.second,
					    ms_tmp.ent.addr.msf.frame);

	toc->xa_flag = (ms_tmp.hdr.first_track != ms_tmp.hdr.last_track);

	/* Now try to get the total cdrom capacity. */
	stat = cdrom_read_capacity (drive, &toc->capacity, reqbuf);
	if (stat) toc->capacity = 0x1fffff;

	HWIF(drive)->gd->sizes[drive->select.b.unit << PARTN_BITS]
		= (toc->capacity * SECTORS_PER_FRAME) >> (BLOCK_SIZE_BITS - 9);
	drive->part[0].nr_sects = toc->capacity * SECTORS_PER_FRAME;

	/* Remember that we've read this stuff. */
	CDROM_STATE_FLAGS (drive)->toc_valid = 1;

	return 0;
}


static int
cdrom_read_subchannel (ide_drive_t *drive, int format,
                       char *buf, int buflen,
		       struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.buffer =  buf;
	pc.buflen = buflen;
	pc.c[0] = SCMD_READ_SUBCHANNEL;
	pc.c[1] = 2;     /* MSF addressing */
	pc.c[2] = 0x40;  /* request subQ data */
	pc.c[3] = format;
	pc.c[7] = (buflen >> 8);
	pc.c[8] = (buflen & 0xff);
	return cdrom_queue_packet_command (drive, &pc);
}


/* modeflag: 0 = current, 1 = changeable mask, 2 = default, 3 = saved */
static int
cdrom_mode_sense (ide_drive_t *drive, int pageno, int modeflag,
                  char *buf, int buflen,
		  struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.buffer =  buf;
	pc.buflen = buflen;
	pc.c[0] = MODE_SENSE_10;
	pc.c[2] = pageno | (modeflag << 6);
	pc.c[7] = (buflen >> 8);
	pc.c[8] = (buflen & 0xff);
	return cdrom_queue_packet_command (drive, &pc);
}

static int
cdrom_mode_select (ide_drive_t *drive, int pageno, char *buf, int buflen,
		   struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.buffer =  buf;
	pc.buflen = - buflen;
	pc.c[0] = MODE_SELECT_10;
	pc.c[1] = 0x10;
	pc.c[2] = pageno;
	pc.c[7] = (buflen >> 8);
	pc.c[8] = (buflen & 0xff);
	return cdrom_queue_packet_command (drive, &pc);
}


/* ATAPI cdrom drives are free to select the speed you request or any slower
   rate :-( Requesting too fast a speed will _not_ produce an error. */
static int
cdrom_select_speed (ide_drive_t *drive, int speed,
                   struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;
	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	if (speed == 0)
	    speed = 0xffff; /* set to max */
	else
	    speed *= 177;   /* Nx to kbytes/s */

	pc.c[0] = SET_CD_SPEED;
	/* Read Drive speed in kbytes/second MSB */
	pc.c[2] = (speed >> 8) & 0xff;	
	/* Read Drive speed in kbytes/second LSB */
	pc.c[3] = speed & 0xff;
	if ( CDROM_CONFIG_FLAGS(drive)->cd_r ||
                   CDROM_CONFIG_FLAGS(drive)->cd_rw ) {
		/* Write Drive speed in kbytes/second MSB */
		pc.c[4] = (speed >> 8) & 0xff;
		/* Write Drive speed in kbytes/second LSB */
		pc.c[5] = speed & 0xff;
       }

	return cdrom_queue_packet_command (drive, &pc);
}

static int
cdrom_play_lba_range_1 (ide_drive_t *drive, int lba_start, int lba_end,
			    struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.c[0] = SCMD_PLAYAUDIO_MSF;
	lba_to_msf (lba_start, &pc.c[3], &pc.c[4], &pc.c[5]);
	lba_to_msf (lba_end-1, &pc.c[6], &pc.c[7], &pc.c[8]);

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd) {
		pc.c[3] = bin2bcd (pc.c[3]);
		pc.c[4] = bin2bcd (pc.c[4]);
		pc.c[5] = bin2bcd (pc.c[5]);
		pc.c[6] = bin2bcd (pc.c[6]);
		pc.c[7] = bin2bcd (pc.c[7]);
		pc.c[8] = bin2bcd (pc.c[8]);
	}
#endif /* not STANDARD_ATAPI */

	return cdrom_queue_packet_command (drive, &pc);
}


/* Play audio starting at LBA LBA_START and finishing with the
   LBA before LBA_END. */
static int
cdrom_play_lba_range (ide_drive_t *drive, int lba_start, int lba_end,
		      struct atapi_request_sense *reqbuf)
{
	int i, stat;
	struct atapi_request_sense my_reqbuf;

	if (reqbuf == NULL)
		reqbuf = &my_reqbuf;

	/* Some drives, will, for certain audio cds,
	   give an error if you ask them to play the entire cd using the
	   values which are returned in the TOC.  The play will succeed,
	   however, if the ending address is adjusted downwards
	   by a few frames. */
	for (i=0; i<75; i++) {
		stat = cdrom_play_lba_range_1 (drive, lba_start, lba_end,
					       reqbuf);

		if (stat == 0 ||
		    !(reqbuf->sense_key == ILLEGAL_REQUEST &&
		      reqbuf->asc == 0x24))
			return stat;

		--lba_end;
		if (lba_end <= lba_start) break;
	}

	return stat;
}


static
int cdrom_get_toc_entry (ide_drive_t *drive, int track,
                         struct atapi_toc_entry **ent,
			 struct atapi_request_sense *reqbuf)
{
	struct cdrom_info *info = drive->driver_data;
	int stat, ntracks;
	struct atapi_toc *toc;

	/* Make sure our saved TOC is valid. */
	stat = cdrom_read_toc (drive, reqbuf);
	if (stat) return stat;

	toc = info->toc;

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


static int
cdrom_read_block (ide_drive_t *drive, int format, int lba, int nblocks,
		  char *buf, int buflen,
		  struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;
	struct atapi_request_sense my_reqbuf;

	if (reqbuf == NULL)
		reqbuf = &my_reqbuf;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.buffer = buf;
	pc.buflen = buflen;

#if ! STANDARD_ATAPI
	if (CDROM_CONFIG_FLAGS (drive)->nec260)
		pc.c[0] = 0xd4;
	else
#endif  /* not STANDARD_ATAPI */
		pc.c[0] = READ_CD;

	pc.c[1] = (format << 2);
	put_unaligned(htonl(lba), (unsigned int *) &pc.c[2]);
	pc.c[8] = (nblocks & 0xff);
	pc.c[7] = ((nblocks>>8) & 0xff);
	pc.c[6] = ((nblocks>>16) & 0xff);
	if (format <= 1)
		pc.c[9] = 0xf8;         /* returns 2352 for any format */
	else
		pc.c[9] = 0x10;

	return cdrom_queue_packet_command (drive, &pc);
}


/* If SLOT<0, unload the current slot.  Otherwise, try to load SLOT. */
static int
cdrom_load_unload (ide_drive_t *drive, int slot,
		   struct atapi_request_sense *reqbuf)
{
#if ! STANDARD_ATAPI
	/* if the drive is a Sanyo 3 CD changer then TEST_UNIT_READY
           (used in the cdrom_check_status function) is used to 
           switch CDs instead of LOAD_UNLOAD */

	if (CDROM_STATE_FLAGS (drive)->sanyo_slot > 0) {

        	if ((slot == 1) || (slot == 2))
			CDROM_STATE_FLAGS (drive)->sanyo_slot = slot;
		else if (slot >= 0)
			CDROM_STATE_FLAGS (drive)->sanyo_slot = 3;
		else
			return 0;

		return cdrom_check_status (drive, reqbuf);

	}
	else
#endif /*not STANDARD_ATAPI */
	{

		/* ATAPI Rev. 2.2+ standard for requesting switching of
                   CDs in a multiplatter device */

		struct packet_command pc;

		memset (&pc, 0, sizeof (pc));
		pc.sense_data = reqbuf;

		pc.c[0] = LOAD_UNLOAD;
		pc.c[4] = 2 + (slot >= 0);
		pc.c[8] = slot;
		return cdrom_queue_packet_command (drive, &pc);

	}
}


/* This gets the mechanism status per ATAPI draft spec 2.6 */
static int
cdrom_read_mech_status (ide_drive_t *drive, char *buf, int buflen,
			struct atapi_request_sense *reqbuf)
{
	struct packet_command pc;

	memset (&pc, 0, sizeof (pc));
	pc.sense_data = reqbuf;

	pc.buffer = buf;
	pc.buflen = buflen;
	pc.c[0] = MECHANISM_STATUS;
	pc.c[8] = (buflen >> 8);
	pc.c[9] = (buflen & 0xff);
	return cdrom_queue_packet_command (drive, &pc);
}


/* Read the drive mechanism status and slot table into our internal buffer.
   If the buffer does not yet exist, allocate it. */
static int
cdrom_read_changer_info (ide_drive_t *drive)
{
	int nslots;
	struct cdrom_info *info = drive->driver_data;

	if (info->changer_info)
		nslots = info->changer_info->hdr.nslots;

	else {
		struct atapi_mechstat_header mechbuf;
		int stat;

		stat = cdrom_read_mech_status (drive,
					       (char *)&mechbuf,
					       sizeof (mechbuf),
					       NULL);
		if (stat)
			return stat;

		nslots = mechbuf.nslots;
		info->changer_info =
			(struct atapi_changer_info *)
			kmalloc (sizeof (struct atapi_changer_info) +
				 nslots * sizeof (struct atapi_slot),
				 GFP_KERNEL);

		if (info->changer_info == NULL)
			return -ENOMEM;
	}

	return cdrom_read_mech_status
		(drive,
		 (char *)&info->changer_info->hdr,
		 sizeof (struct atapi_mechstat_header) +
		 nslots * sizeof (struct atapi_slot),
		 NULL);
}


static
int ide_cdrom_dev_ioctl (struct cdrom_device_info *cdi,
			 unsigned int cmd, unsigned long arg)
			 
{
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct cdrom_info *info = drive->driver_data;


	switch (cmd) {
	case CDROMREADRAW:
	case CDROMREADMODE1:
	case CDROMREADMODE2: {
		struct cdrom_msf msf;
		int blocksize, format, stat, lba;
		struct atapi_toc *toc;
		char *buf;

		if (cmd == CDROMREADMODE1) {
			blocksize = CD_FRAMESIZE;
			format = 2;
		} else if (cmd == CDROMREADMODE2) {
			blocksize = CD_FRAMESIZE_RAW0;
			format = 3;
		} else {
			blocksize = CD_FRAMESIZE_RAW;
			format = 0;
		}
		stat = verify_area (VERIFY_WRITE, (char *)arg, blocksize);
		if (stat) return stat;

		copy_from_user (&msf, (void *)arg, sizeof (msf));

		lba = msf_to_lba (msf.cdmsf_min0,
				  msf.cdmsf_sec0,
				  msf.cdmsf_frame0);
	
		/* Make sure the TOC is up to date. */
		stat = cdrom_read_toc (drive, NULL);
		if (stat) return stat;

		toc = info->toc;

		if (lba < 0 || lba >= toc->capacity)
			return -EINVAL;

		buf = (char *) kmalloc (CD_FRAMESIZE_RAW, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;

		stat = cdrom_read_block (drive, format, lba, 1, buf, blocksize,
					 NULL);
		if (stat == 0)
			copy_to_user ((char *)arg, buf, blocksize);

		kfree (buf);
		return stat;
	}

	/* Read 2352 byte blocks from audio tracks. */
	case CDROMREADAUDIO: {
		int stat, lba;
		struct atapi_toc *toc;
		struct cdrom_read_audio ra;
		char *buf;

		/* Make sure the TOC is up to date. */
		stat = cdrom_read_toc (drive, NULL);
		if (stat) return stat;

		toc = info->toc;

		stat = verify_area (VERIFY_READ, (char *)arg, sizeof (ra));
		if (stat) return stat;

		copy_from_user (&ra, (void *)arg, sizeof (ra));

		if (ra.nframes < 0 || ra.nframes > toc->capacity)
			return -EINVAL;
		else if (ra.nframes == 0)
			return 0;

		stat = verify_area (VERIFY_WRITE, (char *)ra.buf,
				    ra.nframes * CD_FRAMESIZE_RAW);
		if (stat) return stat;

		if (ra.addr_format == CDROM_MSF)
			lba = msf_to_lba (ra.addr.msf.minute,
					  ra.addr.msf.second,
					  ra.addr.msf.frame);
		else if (ra.addr_format == CDROM_LBA)
			lba = ra.addr.lba;
		else
			return -EINVAL;

		if (lba < 0 || lba >= toc->capacity)
			return -EINVAL;

		buf = (char *) kmalloc (CDROM_NBLOCKS_BUFFER*CD_FRAMESIZE_RAW,
					GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;

		while (ra.nframes > 0) {
			int this_nblocks = ra.nframes;
			if (this_nblocks > CDROM_NBLOCKS_BUFFER)
				this_nblocks = CDROM_NBLOCKS_BUFFER;
			stat = cdrom_read_block
				(drive, 1, lba, this_nblocks,
				 buf, this_nblocks * CD_FRAMESIZE_RAW, NULL);
			if (stat) break;

			copy_to_user (ra.buf, buf,
				     this_nblocks * CD_FRAMESIZE_RAW);
			ra.buf += this_nblocks * CD_FRAMESIZE_RAW;
			ra.nframes -= this_nblocks;
			lba += this_nblocks;
		}

		kfree (buf);
		return stat;
	}

 	case CDROMSETSPINDOWN: {
 		char spindown;
 		char buffer[16];
 		int stat;
 
 		stat = verify_area (VERIFY_READ, (void *) arg,
 				    sizeof (char));
 		if (stat) return stat;
 
 		copy_from_user (&spindown, (void *) arg, sizeof(char));
 
 		stat = cdrom_mode_sense (drive, PAGE_CDROM, 0, buffer,
 					 sizeof (buffer), NULL);
 		if (stat) return stat;

 		buffer[11] = (buffer[11] & 0xf0) | (spindown & 0x0f);

 		return cdrom_mode_select (drive, PAGE_CDROM, buffer,
 					  sizeof (buffer), NULL);			
 	} 
 
 	case CDROMGETSPINDOWN: {
 		char spindown;
 		char buffer[16];
 		int stat;
 
 		stat = verify_area (VERIFY_WRITE, (void *) arg,
                                    sizeof (char));
 		if (stat) return stat;
 
 		stat = cdrom_mode_sense (drive, PAGE_CDROM, 0, buffer,
                                         sizeof (buffer), NULL);
 		if (stat) return stat;
 
 		spindown = buffer[11] & 0x0f;
 
 		copy_to_user ((void *) arg, &spindown, sizeof (char));
 
 		return 0;
 	}
  
#ifdef ALLOW_TEST_PACKETS
	case 0x1234: {
		int stat;
		struct packet_command pc;
		int len, lena;

		memset (&pc, 0, sizeof (pc));

		stat = verify_area (VERIFY_READ, (void *) arg, sizeof (pc.c));
		if (stat) return stat;
		copy_from_user (&pc.c, (void *) arg, sizeof (pc.c));
		arg += sizeof (pc.c);

		stat = verify_area (VERIFY_READ, (void *) arg, sizeof (len));
		if (stat) return stat;
		copy_from_user (&len, (void *) arg , sizeof (len));
		arg += sizeof (len);

		lena = len;
		if (lena  < 0) lena = -lena;

		{
			char buf[lena];
			if (len > 0) {
				stat = verify_area (VERIFY_WRITE,
						    (void *) arg, len);
				if (stat) return stat;
			}
			else if (len < 0) {
				stat = verify_area (VERIFY_READ,
						    (void *) arg, -len);
				if (stat) return stat;
				copy_from_user (buf, (void*)arg, -len);
			}

			if (len != 0) {
				pc.buflen = len;
				pc.buffer = buf;
			}

			stat = cdrom_queue_packet_command (drive, &pc);

			if (len > 0)
				copy_to_user ((void *)arg, buf, len);
		}

		return stat;
	}
#endif

	default:
		return -EINVAL;
	}

}



static
int ide_cdrom_audio_ioctl (struct cdrom_device_info *cdi,
			   unsigned int cmd, void *arg)
			   
{
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct cdrom_info *info = drive->driver_data;

	switch (cmd) {
	case CDROMSUBCHNL: {
		struct atapi_cdrom_subchnl scbuf;
		int stat;
		struct cdrom_subchnl *subchnl = (struct cdrom_subchnl *)arg;

		stat = cdrom_read_subchannel (drive, 1, /* current position */
					      (char *)&scbuf, sizeof (scbuf),
					      NULL);
		if (stat) return stat;

#if ! STANDARD_ATAPI
		if (CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd) {
			msf_from_bcd (&scbuf.acdsc_absaddr.msf);
			msf_from_bcd (&scbuf.acdsc_reladdr.msf);
		}
		if (CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd)
			scbuf.acdsc_trk = bcd2bin (scbuf.acdsc_trk);
#endif /* not STANDARD_ATAPI */

		subchnl->cdsc_absaddr.msf.minute =
			scbuf.acdsc_absaddr.msf.minute;
		subchnl->cdsc_absaddr.msf.second =
			scbuf.acdsc_absaddr.msf.second;
		subchnl->cdsc_absaddr.msf.frame =
			scbuf.acdsc_absaddr.msf.frame;

		subchnl->cdsc_reladdr.msf.minute =
			scbuf.acdsc_reladdr.msf.minute;
		subchnl->cdsc_reladdr.msf.second =
			scbuf.acdsc_reladdr.msf.second;
		subchnl->cdsc_reladdr.msf.frame =
			scbuf.acdsc_reladdr.msf.frame;

		subchnl->cdsc_audiostatus = scbuf.acdsc_audiostatus;
		subchnl->cdsc_ctrl = scbuf.acdsc_ctrl;
		subchnl->cdsc_trk  = scbuf.acdsc_trk;
		subchnl->cdsc_ind  = scbuf.acdsc_ind;

		return 0;
	}

	case CDROMREADTOCHDR: {
		int stat;
		struct cdrom_tochdr *tochdr = (struct cdrom_tochdr *) arg;
		struct atapi_toc *toc;

		/* Make sure our saved TOC is valid. */
		stat = cdrom_read_toc (drive, NULL);
		if (stat) return stat;

		toc = info->toc;
		tochdr->cdth_trk0 = toc->hdr.first_track;
		tochdr->cdth_trk1 = toc->hdr.last_track;

		return 0;
	}

	case CDROMREADTOCENTRY: {
		int stat;
		struct cdrom_tocentry *tocentry = (struct cdrom_tocentry*) arg;
		struct atapi_toc_entry *toce;

		stat = cdrom_get_toc_entry (drive, tocentry->cdte_track, &toce,
					    NULL);
		if (stat) return stat;

		tocentry->cdte_ctrl = toce->control;
		tocentry->cdte_adr  = toce->adr;
		tocentry->cdte_format = CDROM_LBA;
		tocentry->cdte_addr.lba = toce->addr.lba;

		return 0;
	}

	case CDROMPLAYMSF: {
		struct cdrom_msf *msf = (struct cdrom_msf *) arg;
		int lba_start, lba_end;

		lba_start = msf_to_lba (msf->cdmsf_min0, msf->cdmsf_sec0,
					msf->cdmsf_frame0);
		lba_end = msf_to_lba (msf->cdmsf_min1, msf->cdmsf_sec1,
				      msf->cdmsf_frame1) + 1;

		if (lba_end <= lba_start) return -EINVAL;

		return cdrom_play_lba_range (drive, lba_start, lba_end, NULL);
	}

	/* Like just about every other Linux cdrom driver, we ignore the
	   index part of the request here. */
	case CDROMPLAYTRKIND: {
		int stat, lba_start, lba_end;
		struct cdrom_ti *ti = (struct cdrom_ti *)arg;
		struct atapi_toc_entry *first_toc, *last_toc;

		stat = cdrom_get_toc_entry (drive, ti->cdti_trk0, &first_toc,
					    NULL);
		if (stat) return stat;
		stat = cdrom_get_toc_entry (drive, ti->cdti_trk1, &last_toc,
					    NULL);
		if (stat) return stat;

		if (ti->cdti_trk1 != CDROM_LEADOUT) ++last_toc;
		lba_start = first_toc->addr.lba;
		lba_end   = last_toc->addr.lba;

		if (lba_end <= lba_start) return -EINVAL;

		return cdrom_play_lba_range (drive, lba_start, lba_end, NULL);
	}

	case CDROMVOLCTRL: {
		struct cdrom_volctrl *volctrl = (struct cdrom_volctrl *) arg;
		char buffer[24], mask[24];
		int stat;

		stat = cdrom_mode_sense (drive, PAGE_AUDIO, 0, buffer,
					 sizeof (buffer), NULL);
		if (stat) return stat;
		stat = cdrom_mode_sense (drive, PAGE_AUDIO, 1, mask,
					 sizeof (buffer), NULL);
		if (stat) return stat;

		buffer[1] = buffer[2] = 0;

		buffer[17] = volctrl->channel0 & mask[17];
		buffer[19] = volctrl->channel1 & mask[19];
		buffer[21] = volctrl->channel2 & mask[21];
		buffer[23] = volctrl->channel3 & mask[23];

		return cdrom_mode_select (drive, PAGE_AUDIO, buffer,
					  sizeof (buffer), NULL);
	}

	case CDROMVOLREAD: {
		struct cdrom_volctrl *volctrl = (struct cdrom_volctrl *) arg;
		char buffer[24];
		int stat;

		stat = cdrom_mode_sense (drive, PAGE_AUDIO, 0, buffer,
					 sizeof (buffer), NULL);
		if (stat) return stat;

		volctrl->channel0 = buffer[17];
		volctrl->channel1 = buffer[19];
		volctrl->channel2 = buffer[21];
		volctrl->channel3 = buffer[23];

		return 0;
	}

	case CDROMSTART:
		return cdrom_startstop (drive, 1, NULL);

	case CDROMSTOP: {
#ifdef IHAVEADOLPHIN
               /*  Certain Drives require this.  Most don't
                   and will produce errors upon CDROMSTOP
                   pit says the Dolphin needs this.  If you
                   own a dolphin, just define IHAVEADOLPHIN somewhere */
                int stat;
                stat = cdrom_startstop (drive, 0, NULL);
                if (stat) return stat;
                return cdrom_eject (drive, 1, NULL);
#endif /* end of IHAVEADOLPHIN  */
               return cdrom_startstop (drive, 0, NULL);
	}

	case CDROMPAUSE:
		return cdrom_pause (drive, 1, NULL);

	case CDROMRESUME:
		return cdrom_pause (drive, 0, NULL);


	default:
		return -EINVAL;
	}
}


static
int ide_cdrom_reset (struct cdrom_device_info *cdi)
{

	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct request req;

	ide_init_drive_cmd (&req);
	req.cmd = RESET_DRIVE_COMMAND;
	return ide_do_drive_cmd (drive, &req, ide_wait);

}


static
int ide_cdrom_tray_move (struct cdrom_device_info *cdi, int position)
{
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;

	if (position) {
		int stat = cdrom_lockdoor (drive, 0, NULL);
		if (stat) return stat;
	}

	return cdrom_eject (drive, !position, NULL);
}


static
int ide_cdrom_lock_door (struct cdrom_device_info *cdi, int lock)
{
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	return cdrom_lockdoor (drive, lock, NULL);
}

static
int ide_cdrom_select_speed (struct cdrom_device_info *cdi, int speed)
{
        int stat, attempts = 3;
        struct {
                char pad[8];
                struct atapi_capabilities_page cap;
        } buf;
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct atapi_request_sense reqbuf;
	stat=cdrom_select_speed (drive, speed, &reqbuf);
	if (stat<0)
		return stat;

	/* Now with that done, update the speed fields */
        do {    /* we seem to get stat=0x01,err=0x00 the first time (??) */
                if (attempts-- <= 0)
                        return 0;
                stat = cdrom_mode_sense (drive, PAGE_CAPABILITIES, 0,
                                        (char *)&buf, sizeof (buf), NULL);
        } while (stat);

        /* The ACER/AOpen 24X cdrom has the speed fields byte-swapped */
        if (drive->id && !drive->id->model[0] && !strncmp(drive->id->fw_rev, "241N", 4)) {
                CDROM_STATE_FLAGS (drive)->current_speed  = 
			(((unsigned int)buf.cap.curspeed) + (176/2)) / 176;
                CDROM_CONFIG_FLAGS (drive)->max_speed = 
			(((unsigned int)buf.cap.maxspeed) + (176/2)) / 176;
        } else {
                CDROM_STATE_FLAGS (drive)->current_speed  = 
			(ntohs(buf.cap.curspeed) + (176/2)) / 176;
                CDROM_CONFIG_FLAGS (drive)->max_speed = 
			(ntohs(buf.cap.maxspeed) + (176/2)) / 176;
        }
        cdi->speed = CDROM_STATE_FLAGS (drive)->current_speed;
        return 0;
}


static
int ide_cdrom_select_disc (struct cdrom_device_info *cdi, int slot)
{
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct cdrom_info *info = drive->driver_data;

	struct atapi_request_sense my_reqbuf;
	int stat;
	int nslots, curslot;

	if ( ! CDROM_CONFIG_FLAGS (drive)->is_changer) 
		return -EDRIVE_CANT_DO_THIS;

#if ! STANDARD_ATAPI
	if (CDROM_STATE_FLAGS (drive)->sanyo_slot > 0) {
		nslots = 3;
		curslot = CDROM_STATE_FLAGS (drive)->sanyo_slot;
		if (curslot == 3)
			curslot = 0;
	} else
#endif /* not STANDARD_ATAPI */
	{
		stat = cdrom_read_changer_info (drive);
		if (stat)
			return stat;

		nslots = info->changer_info->hdr.nslots;
		curslot = info->changer_info->hdr.curslot;
	}

	if (slot == curslot)
		return curslot;

	if (slot == CDSL_CURRENT)
		return curslot;

	if (slot != CDSL_NONE && (slot < 0 || slot >= nslots))
		return -EINVAL;

	if (drive->usage > 1)
		return -EBUSY;

	if (slot == CDSL_NONE) {
		(void) cdrom_load_unload (drive, -1, NULL);
		cdrom_saw_media_change (drive);
		(void) cdrom_lockdoor (drive, 0, NULL);
		return 0;
	}
	else {
		int was_locked;

		if (
#if ! STANDARD_ATAPI
		    CDROM_STATE_FLAGS (drive)->sanyo_slot == 0 &&
#endif
		    info->changer_info->slots[slot].disc_present == 0) {
			return -ENOMEDIUM;
		}

		was_locked = CDROM_STATE_FLAGS (drive)->door_locked;
		if (was_locked)
			(void) cdrom_lockdoor (drive, 0, NULL);

		stat = cdrom_load_unload (drive, slot, NULL);
		cdrom_saw_media_change (drive);
		if (stat)
			return stat;
			
		stat = cdrom_check_status (drive, &my_reqbuf);
		if (stat && my_reqbuf.sense_key == NOT_READY)
			return -ENOENT;

		if (stat == 0 || my_reqbuf.sense_key == UNIT_ATTENTION) {
			stat = cdrom_read_toc (drive, &my_reqbuf);
			if (stat)
				return stat;
		}

		if (was_locked)
			(void) cdrom_lockdoor (drive, 1, NULL);

		return slot;
	}
}


static
int ide_cdrom_drive_status (struct cdrom_device_info *cdi, int slot_nr)
{
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct cdrom_info *info = drive->driver_data;

	if (slot_nr == CDSL_CURRENT) {

		struct atapi_request_sense my_reqbuf;
		int stat = cdrom_check_status (drive, &my_reqbuf);
		if (stat == 0 || my_reqbuf.sense_key == UNIT_ATTENTION)
			return CDS_DISC_OK;

		if (my_reqbuf.sense_key == NOT_READY) {
			/* ATAPI doesn't have anything that can help
			   us decide whether the drive is really
			   emtpy or the tray is just open. irk. */
			return CDS_TRAY_OPEN;
		}

		return CDS_DRIVE_NOT_READY;
	}

#if ! STANDARD_ATAPI
	else if (CDROM_STATE_FLAGS (drive)->sanyo_slot > 0)
		return CDS_NO_INFO;
#endif /* not STANDARD_ATAPI */

	else {
		struct atapi_changer_info *ci;
		int stat = cdrom_read_changer_info (drive);
		if (stat < 0)
			return stat;
		ci = info->changer_info;

		if (ci->slots[slot_nr].disc_present)
			return CDS_DISC_OK;
		else
			return CDS_NO_DISC;
	}
}

static
int ide_cdrom_get_last_session (struct cdrom_device_info *cdi,
				struct cdrom_multisession *ms_info)
{
	int stat;
	struct atapi_toc *toc;
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct cdrom_info *info = drive->driver_data;

	/* Make sure the TOC information is valid. */
	stat = cdrom_read_toc (drive, NULL);
	if (stat) return stat;

	toc = info->toc;
	ms_info->addr.lba = toc->last_session_lba;
	ms_info->xa_flag = toc->xa_flag;

	return 0;
}


static
int ide_cdrom_get_mcn (struct cdrom_device_info *cdi,
		       struct cdrom_mcn *mcn_info)
{
	int stat;
	char mcnbuf[24];
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;

	stat = cdrom_read_subchannel (drive, 2, /* get MCN */
				      mcnbuf, sizeof (mcnbuf),
				      NULL);
	if (stat) return stat;

	memcpy (mcn_info->medium_catalog_number, mcnbuf+9,
		sizeof (mcn_info->medium_catalog_number)-1);
	mcn_info->medium_catalog_number[sizeof (mcn_info->medium_catalog_number)-1]
		= '\0';

	return 0;
}



/****************************************************************************
 * Other driver requests (open, close, check media change).
 */

static
int ide_cdrom_check_media_change_real (struct cdrom_device_info *cdi,
				       int slot_nr)
{
	ide_drive_t *drive = (ide_drive_t*) cdi->handle;
	struct cdrom_info *info = drive->driver_data;

	int retval;

	if (slot_nr == CDSL_CURRENT) {
		(void) cdrom_check_status (drive, NULL);
		retval = CDROM_STATE_FLAGS (drive)->media_changed;
		CDROM_STATE_FLAGS (drive)->media_changed = 0;
	}

#if ! STANDARD_ATAPI
	else if (CDROM_STATE_FLAGS (drive)->sanyo_slot > 0) {
		retval = 0;
	}
#endif /* not STANDARD_ATAPI */

	else {
		struct atapi_changer_info *ci;
		int stat = cdrom_read_changer_info (drive);
		if (stat < 0)
			return stat;
		ci = info->changer_info;

		/* This test may be redundant with cdrom.c. */
		if (slot_nr < 0 || slot_nr >= ci->hdr.nslots)
			return -EINVAL;

		retval = ci->slots[slot_nr].change;
	}

	return retval;
}


static
int ide_cdrom_open_real (struct cdrom_device_info *cdi, int purpose)
{
	return 0;
}


/*
 * Close down the device.  Invalidate all cached blocks.
 */

static
void ide_cdrom_release_real (struct cdrom_device_info *cdi)
{
}



/****************************************************************************
 * Device initialization.
 */

static
struct cdrom_device_ops ide_cdrom_dops = {
	ide_cdrom_open_real,    /* open */
	ide_cdrom_release_real, /* release */
	ide_cdrom_drive_status, /* drive_status */
	ide_cdrom_check_media_change_real, /* media_changed */
	ide_cdrom_tray_move,    /* tray_move */
	ide_cdrom_lock_door,    /* lock_door */
	ide_cdrom_select_speed, /* select_speed */
	ide_cdrom_select_disc, /* select_disc */
	ide_cdrom_get_last_session, /* get_last_session */
	ide_cdrom_get_mcn, /* get_mcn */
	ide_cdrom_reset, /* reset */
	ide_cdrom_audio_ioctl, /* audio_ioctl */
	ide_cdrom_dev_ioctl,   /* dev_ioctl */
	CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK | CDC_SELECT_SPEED
	| CDC_SELECT_DISC | CDC_MULTI_SESSION | CDC_MCN
	| CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO | CDC_RESET 
	| CDC_IOCTLS | CDC_DRIVE_STATUS,  /* capability */
	0 /* n_minors */
};

static int ide_cdrom_register (ide_drive_t *drive, int nslots)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *devinfo = &info->devinfo;
	int minor = (drive->select.b.unit)<<PARTN_BITS;

	devinfo->dev = MKDEV (HWIF(drive)->major, minor);
	devinfo->ops = &ide_cdrom_dops;
	devinfo->mask = 0;
	*(int *)&devinfo->speed = CDROM_STATE_FLAGS (drive)->current_speed;
	*(int *)&devinfo->capacity = nslots;
	devinfo->handle = (void *) drive;
	strcpy(devinfo->name, drive->name);
	return register_cdrom (devinfo);
}


static
int ide_cdrom_probe_capabilities (ide_drive_t *drive)
{
	int stat, nslots = 0, attempts = 3;
 	struct {
		char pad[8];
		struct atapi_capabilities_page cap;
	} buf;

	if (CDROM_CONFIG_FLAGS (drive)->nec260)
		return nslots;

	do {	/* we seem to get stat=0x01,err=0x00 the first time (??) */
		if (attempts-- <= 0)
			return 0;
		stat = cdrom_mode_sense (drive, PAGE_CAPABILITIES, 0,
				 	(char *)&buf, sizeof (buf), NULL);
	} while (stat);

	if (buf.cap.lock == 0)
		CDROM_CONFIG_FLAGS (drive)->no_doorlock = 1;
	if (buf.cap.eject)
		CDROM_CONFIG_FLAGS (drive)->no_eject = 0;
	if (buf.cap.cd_r_write)
		CDROM_CONFIG_FLAGS (drive)->cd_r = 1;
	if (buf.cap.cd_rw_write)
		CDROM_CONFIG_FLAGS (drive)->cd_rw = 1;
	if (buf.cap.test_write)
		CDROM_CONFIG_FLAGS (drive)->test_write = 1;
	if (buf.cap.dvd_ram_read || buf.cap.dvd_r_read || buf.cap.dvd_rom)
		CDROM_CONFIG_FLAGS (drive)->dvd = 1;
	if (buf.cap.dvd_ram_write)
		CDROM_CONFIG_FLAGS (drive)->dvd_r = 1;
	if (buf.cap.dvd_r_write)
		CDROM_CONFIG_FLAGS (drive)->dvd_rw = 1;

#if ! STANDARD_ATAPI
	if (CDROM_STATE_FLAGS (drive)->sanyo_slot > 0) {
		CDROM_CONFIG_FLAGS (drive)->is_changer = 1;
		nslots = 3;
	}

	else
#endif /* not STANDARD_ATAPI */
	if (buf.cap.mechtype == mechtype_individual_changer ||
	    buf.cap.mechtype == mechtype_cartridge_changer) {
		struct atapi_mechstat_header mechbuf;

		stat = cdrom_read_mech_status (drive, (char*)&mechbuf,
					       sizeof (mechbuf), NULL);
		if (!stat) {
			CDROM_CONFIG_FLAGS (drive)->is_changer = 1;
			CDROM_CONFIG_FLAGS (drive)->supp_disc_present = 1;
			nslots = mechbuf.nslots;
		}
	}

	/* The ACER/AOpen 24X cdrom has the speed fields byte-swapped */
	if (drive->id && !drive->id->model[0] && !strncmp(drive->id->fw_rev, "241N", 4)) {
		CDROM_STATE_FLAGS (drive)->current_speed  = 
			(((unsigned int)buf.cap.curspeed) + (176/2)) / 176;
		CDROM_CONFIG_FLAGS (drive)->max_speed = 
			(((unsigned int)buf.cap.maxspeed) + (176/2)) / 176;
	} else {
		CDROM_STATE_FLAGS (drive)->current_speed  = 
			(ntohs(buf.cap.curspeed) + (176/2)) / 176;
		CDROM_CONFIG_FLAGS (drive)->max_speed = 
			(ntohs(buf.cap.maxspeed) + (176/2)) / 176;
	}

	printk ("%s: ATAPI %dX %s", 
        	drive->name, CDROM_CONFIG_FLAGS (drive)->max_speed,
		(CDROM_CONFIG_FLAGS (drive)->dvd) ? "DVD-ROM" : "CD-ROM");

	if (CDROM_CONFIG_FLAGS (drive)->dvd_r|CDROM_CONFIG_FLAGS (drive)->dvd_rw)
        	printk (" DVD%s%s", 
        	(CDROM_CONFIG_FLAGS (drive)->dvd_r)? "-RAM" : "", 
        	(CDROM_CONFIG_FLAGS (drive)->dvd_rw)? "/RW" : "");

        if (CDROM_CONFIG_FLAGS (drive)->cd_r|CDROM_CONFIG_FLAGS (drive)->cd_rw) 
        	printk (" CD%s%s", 
        	(CDROM_CONFIG_FLAGS (drive)->cd_r)? "-R" : "", 
        	(CDROM_CONFIG_FLAGS (drive)->cd_rw)? "/RW" : "");

        if (CDROM_CONFIG_FLAGS (drive)->is_changer) 
        	printk (" changer w/%d slots", nslots);
        else 	
        	printk (" drive");

	printk (", %dkB Cache\n", ntohs(buf.cap.buffer_size));

	return nslots;
}

static void ide_cdrom_add_settings(ide_drive_t *drive)
{
	int major = HWIF(drive)->major;
	int minor = drive->select.b.unit << PARTN_BITS;

	ide_add_setting(drive,	"breada_readahead",	SETTING_RW, BLKRAGET, BLKRASET, TYPE_INT, 0, 255, 1, 2, &read_ahead[major], NULL);
	ide_add_setting(drive,	"file_readahead",	SETTING_RW, BLKFRAGET, BLKFRASET, TYPE_INTA, 0, INT_MAX, 1, 1024, &max_readahead[major][minor],	NULL);
	ide_add_setting(drive,	"max_kb_per_request",	SETTING_RW, BLKSECTGET, BLKSECTSET, TYPE_INTA, 1, 255, 1, 2, &max_sectors[major][minor], NULL);
	ide_add_setting(drive,	"dsc_overlap",		SETTING_RW, -1, -1, TYPE_BYTE, 0, 1, 1,	1, &drive->dsc_overlap, NULL);
}

static
int ide_cdrom_setup (ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	int nslots;

	kdev_t dev = MKDEV (HWIF (drive)->major,
			    drive->select.b.unit << PARTN_BITS);

	set_device_ro (dev, 1);
	blksize_size[HWIF(drive)->major][drive->select.b.unit << PARTN_BITS] =
		CD_FRAMESIZE;

	drive->special.all = 0;
	drive->ready_stat = 0;

	CDROM_STATE_FLAGS (drive)->media_changed = 0;
	CDROM_STATE_FLAGS (drive)->toc_valid     = 0;
	CDROM_STATE_FLAGS (drive)->door_locked   = 0;

#if NO_DOOR_LOCKING
	CDROM_CONFIG_FLAGS (drive)->no_doorlock = 1;
#else
	CDROM_CONFIG_FLAGS (drive)->no_doorlock = 0;
#endif

	if (drive->id != NULL)
		CDROM_CONFIG_FLAGS (drive)->drq_interrupt =
			((drive->id->config & 0x0060) == 0x20);
	else
		CDROM_CONFIG_FLAGS (drive)->drq_interrupt = 0;

	CDROM_CONFIG_FLAGS (drive)->is_changer = 0;
	CDROM_CONFIG_FLAGS (drive)->cd_r = 0;
	CDROM_CONFIG_FLAGS (drive)->cd_rw = 0;
	CDROM_CONFIG_FLAGS (drive)->test_write = 0;
	CDROM_CONFIG_FLAGS (drive)->dvd = 0;
	CDROM_CONFIG_FLAGS (drive)->dvd_r = 0;
	CDROM_CONFIG_FLAGS (drive)->dvd_rw = 0;
	CDROM_CONFIG_FLAGS (drive)->no_eject = 1;
	CDROM_CONFIG_FLAGS (drive)->supp_disc_present = 0;
	
	/* limit transfer size per interrupt. currently only one Samsung
	   drive needs this. */
	CDROM_CONFIG_FLAGS (drive)->limit_nframes = 0;
	if (drive->id != NULL)
		if (strcmp (drive->id->model, "SAMSUNG CD-ROM SCR-2432") == 0)
			CDROM_CONFIG_FLAGS (drive)->limit_nframes = 1;

#if ! STANDARD_ATAPI
	/* by default Sanyo 3 CD changer support is turned off and
           ATAPI Rev 2.2+ standard support for CD changers is used */
	CDROM_STATE_FLAGS (drive)->sanyo_slot = 0;

	CDROM_CONFIG_FLAGS (drive)->nec260 = 0;
	CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd = 0;
	CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd = 0;
	CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 0;
	CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 0;

	if (drive->id != NULL) {
		if (strcmp (drive->id->model, "V003S0DS") == 0 &&
		    drive->id->fw_rev[4] == '1' &&
		    drive->id->fw_rev[6] <= '2') {
			/* Vertos 300.
			   Some versions of this drive like to talk BCD. */
			CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 1;
		}

		else if (strcmp (drive->id->model, "V006E0DS") == 0 &&
		    drive->id->fw_rev[4] == '1' &&
		    drive->id->fw_rev[6] <= '2') {
			/* Vertos 600 ESD. */
			CDROM_CONFIG_FLAGS (drive)->toctracks_as_bcd = 1;
		}

		else if (strcmp (drive->id->model,
				 "NEC CD-ROM DRIVE:260") == 0 &&
			 strncmp (drive->id->fw_rev, "1.01", 4) == 0) { /* FIXME */
			/* Old NEC260 (not R).
			   This drive was released before the 1.2 version
			   of the spec. */
			CDROM_CONFIG_FLAGS (drive)->tocaddr_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->nec260         = 1;
		}

		else if (strcmp (drive->id->model, "WEARNES CDD-120") == 0 &&
			 strncmp (drive->id->fw_rev, "A1.1", 4) == 0) { /* FIXME */
			/* Wearnes */
			CDROM_CONFIG_FLAGS (drive)->playmsf_as_bcd = 1;
			CDROM_CONFIG_FLAGS (drive)->subchan_as_bcd = 1;
		}

                /* Sanyo 3 CD changer uses a non-standard command
                    for CD changing */
                 else if ((strcmp(drive->id->model, "CD-ROM CDR-C3 G") == 0) ||
                         (strcmp(drive->id->model, "CD-ROM CDR-C3G") == 0) ||
                         (strcmp(drive->id->model, "CD-ROM CDR_C36") == 0)) {
                        /* uses CD in slot 0 when value is set to 3 */
                        CDROM_STATE_FLAGS (drive)->sanyo_slot = 3;
                }


	}
#endif /* not STANDARD_ATAPI */

	info->toc               = NULL;
	info->sector_buffer     = NULL;
	info->sector_buffered   = 0;
	info->nsectors_buffered = 0;
	info->changer_info      = NULL;

	nslots = ide_cdrom_probe_capabilities (drive);

	if (ide_cdrom_register (drive, nslots)) {
		printk ("%s: ide_cdrom_setup failed to register device with the cdrom driver.\n", drive->name);
		info->devinfo.handle = NULL;
		return 1;
	}
	ide_cdrom_add_settings(drive);
	return 0;
}

/* Forwarding functions to generic routines. */
static
int ide_cdrom_ioctl (ide_drive_t *drive,
		     struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	return cdrom_fops.ioctl (inode, file, cmd, arg);
}

static
int ide_cdrom_open (struct inode *ip, struct file *fp, ide_drive_t *drive)
{
	int rc;

	MOD_INC_USE_COUNT;
	rc = cdrom_fops.open (ip, fp);
	if (rc) {
		drive->usage--;
		MOD_DEC_USE_COUNT;
	}
	return rc;
}

static
void ide_cdrom_release (struct inode *inode, struct file *file,
			ide_drive_t *drive)
{
	cdrom_fops.release (inode, file);
	MOD_DEC_USE_COUNT;
}

static
int ide_cdrom_check_media_change (ide_drive_t *drive)
{
	return cdrom_fops.check_media_change
		(MKDEV (HWIF (drive)->major,
			(drive->select.b.unit)<<PARTN_BITS));
}


static
int ide_cdrom_cleanup(ide_drive_t *drive)
{
	struct cdrom_info *info = drive->driver_data;
	struct cdrom_device_info *devinfo = &info->devinfo;

	if (ide_unregister_subdriver (drive))
		return 1;
	if (info->sector_buffer != NULL)
		kfree (info->sector_buffer);
	if (info->toc != NULL)
		kfree (info->toc);
	if (info->changer_info != NULL)
		kfree (info->changer_info);
	if (devinfo->handle == drive && unregister_cdrom (devinfo))
		printk ("%s: ide_cdrom_cleanup failed to unregister device from the cdrom driver.\n", drive->name);
	kfree (info);
	drive->driver_data = NULL;
	return 0;
}

static ide_driver_t ide_cdrom_driver = {
	"ide-cdrom",			/* name */
	IDECD_VERSION,			/* version */
	ide_cdrom,			/* media */
	0,				/* busy */
	1,				/* supports_dma */
	1,				/* supports_dsc_overlap */
	ide_cdrom_cleanup,		/* cleanup */
	ide_do_rw_cdrom,		/* do_request */
	NULL,				/* ??? or perhaps cdrom_end_request? */
	ide_cdrom_ioctl,		/* ioctl */
	ide_cdrom_open,			/* open */
	ide_cdrom_release,		/* release */
	ide_cdrom_check_media_change,	/* media_change */
	NULL,				/* pre_reset */
	NULL,				/* capacity */
	NULL,				/* special */
	NULL				/* proc */
};

int ide_cdrom_init (void);
static ide_module_t ide_cdrom_module = {
	IDE_DRIVER_MODULE,
	ide_cdrom_init,
	&ide_cdrom_driver,
	NULL
};

#ifdef MODULE
int init_module (void)
{
	return ide_cdrom_init();
}

void cleanup_module(void)
{
	ide_drive_t *drive;
	int failed = 0;

	while ((drive = ide_scan_devices (ide_cdrom, ide_cdrom_driver.name, &ide_cdrom_driver, failed)) != NULL)
		if (ide_cdrom_cleanup (drive)) {
			printk ("%s: cleanup_module() called while still busy\n", drive->name);
			failed++;
		}
	ide_unregister_module (&ide_cdrom_module);
}
#endif /* MODULE */
 
int ide_cdrom_init (void)
{
	ide_drive_t *drive;
	struct cdrom_info *info;
	int failed = 0;

	MOD_INC_USE_COUNT;
	while ((drive = ide_scan_devices (ide_cdrom, ide_cdrom_driver.name, NULL, failed++)) != NULL) {
		info = (struct cdrom_info *) kmalloc (sizeof (struct cdrom_info), GFP_KERNEL);
		if (info == NULL) {
			printk ("%s: Can't allocate a cdrom structure\n", drive->name);
			continue;
		}
		if (ide_register_subdriver (drive, &ide_cdrom_driver, IDE_SUBDRIVER_VERSION)) {
			printk ("%s: Failed to register the driver with ide.c\n", drive->name);
			kfree (info);
			continue;
		}
		memset (info, 0, sizeof (struct cdrom_info));
		drive->driver_data = info;
		DRIVER(drive)->busy++;
		if (ide_cdrom_setup (drive)) {
			DRIVER(drive)->busy--;
			if (ide_cdrom_cleanup (drive))
				printk ("%s: ide_cdrom_cleanup failed in ide_cdrom_init\n", drive->name);
			continue;
		}
		DRIVER(drive)->busy--;
		failed--;
	}
	ide_register_module(&ide_cdrom_module);
	MOD_DEC_USE_COUNT;
	return 0;
}


/*==========================================================================*/
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
