/*
 *  sr.c Copyright (C) 1992 David Giller
 *           Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *
 *  adapted from:
 *      sd.c Copyright (C) 1992 Drew Eckhardt
 *      Linux scsi disk driver by
 *              Drew Eckhardt <drew@colorado.edu>
 *
 *      Modified by Eric Youngdale ericy@andante.org to
 *      add scatter-gather, multiple outstanding request, and other
 *      enhancements.
 *
 *          Modified by Eric Youngdale eric@andante.org to support loadable
 *          low-level scsi drivers.
 *
 *       Modified by Thomas Quinot thomas@melchior.cuivre.fdn.fr to
 *       provide auto-eject.
 *
 *          Modified by Gerd Knorr <kraxel@cs.tu-berlin.de> to support the
 *          generic cdrom interface
 *
 *       Modified by Jens Axboe <axboe@image.dk> - Uniform sr_packet()
 *       interface, capabilities probe additions, ioctl cleanups, etc.
 *
 *       Modified by Richard Gooch <rgooch@atnf.csiro.au> to support devfs
 *
 *       Modified by Jens Axboe <axboe@suse.de> - support DVD-RAM
 *	 transparently and loose the GHOST hack
 *
 */

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define MAJOR_NR SCSI_CDROM_MAJOR
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include "sr.h"
#include <scsi/scsi_ioctl.h>	/* For the door lock/unlock commands */
#include "constants.h"

#ifdef MODULE
MODULE_PARM(xa_test, "i");	/* see sr_ioctl.c */
#endif

#define MAX_RETRIES	3
#define SR_TIMEOUT	(30 * HZ)

static int sr_init(void);
static void sr_finish(void);
static int sr_attach(Scsi_Device *);
static int sr_detect(Scsi_Device *);
static void sr_detach(Scsi_Device *);

static int sr_init_command(Scsi_Cmnd *);

struct Scsi_Device_Template sr_template =
{
	name:"cdrom",
	tag:"sr",
	scsi_type:TYPE_ROM,
	major:SCSI_CDROM_MAJOR,
	blk:1,
	detect:sr_detect,
	init:sr_init,
	finish:sr_finish,
	attach:sr_attach,
	detach:sr_detach,
	init_command:sr_init_command
};

Scsi_CD *scsi_CDs = NULL;
static int *sr_sizes = NULL;

static int *sr_blocksizes = NULL;
static int *sr_hardsizes = NULL;

static int sr_open(struct cdrom_device_info *, int);
void get_sectorsize(int);
void get_capabilities(int);

static int sr_media_change(struct cdrom_device_info *, int);
static int sr_packet(struct cdrom_device_info *, struct cdrom_generic_command *);

static void sr_release(struct cdrom_device_info *cdi)
{
	if (scsi_CDs[MINOR(cdi->dev)].device->sector_size > 2048)
		sr_set_blocklength(MINOR(cdi->dev), 2048);
	sync_dev(cdi->dev);
	scsi_CDs[MINOR(cdi->dev)].device->access_count--;
	if (scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module)
		__MOD_DEC_USE_COUNT(scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module);
	if (sr_template.module)
		__MOD_DEC_USE_COUNT(sr_template.module);
}

static struct cdrom_device_ops sr_dops =
{
	sr_open,		/* open */
	sr_release,		/* release */
	sr_drive_status,	/* drive status */
	sr_media_change,	/* media changed */
	sr_tray_move,		/* tray move */
	sr_lock_door,		/* lock door */
	sr_select_speed,	/* select speed */
	NULL,			/* select disc */
	sr_get_last_session,	/* get last session */
	sr_get_mcn,		/* get universal product code */
	sr_reset,		/* hard reset */
	sr_audio_ioctl,		/* audio ioctl */
	sr_dev_ioctl,		/* device-specific ioctl */
	CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK | CDC_SELECT_SPEED |
      CDC_SELECT_DISC | CDC_MULTI_SESSION | CDC_MCN | CDC_MEDIA_CHANGED |
	CDC_PLAY_AUDIO | CDC_RESET | CDC_IOCTLS | CDC_DRIVE_STATUS |
	CDC_CD_R | CDC_CD_RW | CDC_DVD | CDC_DVD_R | CDC_DVD_RAM |
	CDC_GENERIC_PACKET,
	0,
	sr_packet
};

/*
 * This function checks to see if the media has been changed in the
 * CDROM drive.  It is possible that we have already sensed a change,
 * or the drive may have sensed one and not yet reported it.  We must
 * be ready for either case. This function always reports the current
 * value of the changed bit.  If flag is 0, then the changed bit is reset.
 * This function could be done as an ioctl, but we would need to have
 * an inode for that to work, and we do not always have one.
 */

int sr_media_change(struct cdrom_device_info *cdi, int slot)
{
	int retval;

	if (CDSL_CURRENT != slot) {
		/* no changer support */
		return -EINVAL;
	}
	retval = scsi_ioctl(scsi_CDs[MINOR(cdi->dev)].device,
			    SCSI_IOCTL_TEST_UNIT_READY, 0);

	if (retval) {
		/* Unable to test, unit probably not ready.  This usually
		 * means there is no disc in the drive.  Mark as changed,
		 * and we will figure it out later once the drive is
		 * available again.  */

		scsi_CDs[MINOR(cdi->dev)].device->changed = 1;
		return 1;	/* This will force a flush, if called from
				 * check_disk_change */
	};

	retval = scsi_CDs[MINOR(cdi->dev)].device->changed;
	scsi_CDs[MINOR(cdi->dev)].device->changed = 0;
	/* If the disk changed, the capacity will now be different,
	 * so we force a re-read of this information */
	if (retval) {
		/* check multisession offset etc */
		sr_cd_check(cdi);

		/* 
		 * If the disk changed, the capacity will now be different,
		 * so we force a re-read of this information 
		 * Force 2048 for the sector size so that filesystems won't
		 * be trying to use something that is too small if the disc
		 * has changed.
		 */
		scsi_CDs[MINOR(cdi->dev)].needs_sector_size = 1;

		scsi_CDs[MINOR(cdi->dev)].device->sector_size = 2048;
	}
	return retval;
}

/*
 * rw_intr is the interrupt routine for the device driver.  It will be notified on the
 * end of a SCSI read / write, and will take on of several actions based on success or failure.
 */

static void rw_intr(Scsi_Cmnd * SCpnt)
{
	int result = SCpnt->result;
	int this_count = SCpnt->bufflen >> 9;
	int good_sectors = (result == 0 ? this_count : 0);
	int block_sectors = 0;

#ifdef DEBUG
	printk("sr.c done: %x %x\n", result, SCpnt->request.bh->b_data);
#endif
	/*
	   Handle MEDIUM ERRORs or VOLUME OVERFLOWs that indicate partial success.
	   Since this is a relatively rare error condition, no care is taken to
	   avoid unnecessary additional work such as memcpy's that could be avoided.
	 */


	if (driver_byte(result) != 0 &&		/* An error occurred */
	    SCpnt->sense_buffer[0] == 0xF0 &&	/* Sense data is valid */
	    (SCpnt->sense_buffer[2] == MEDIUM_ERROR ||
	     SCpnt->sense_buffer[2] == VOLUME_OVERFLOW ||
	     SCpnt->sense_buffer[2] == ILLEGAL_REQUEST)) {
		long error_sector = (SCpnt->sense_buffer[3] << 24) |
		(SCpnt->sense_buffer[4] << 16) |
		(SCpnt->sense_buffer[5] << 8) |
		SCpnt->sense_buffer[6];
		int device_nr = DEVICE_NR(SCpnt->request.rq_dev);
		if (SCpnt->request.bh != NULL)
			block_sectors = SCpnt->request.bh->b_size >> 9;
		if (block_sectors < 4)
			block_sectors = 4;
		if (scsi_CDs[device_nr].device->sector_size == 2048)
			error_sector <<= 2;
		error_sector &= ~(block_sectors - 1);
		good_sectors = error_sector - SCpnt->request.sector;
		if (good_sectors < 0 || good_sectors >= this_count)
			good_sectors = 0;
		/*
		 * The SCSI specification allows for the value returned by READ
		 * CAPACITY to be up to 75 2K sectors past the last readable
		 * block.  Therefore, if we hit a medium error within the last
		 * 75 2K sectors, we decrease the saved size value.
		 */
		if ((error_sector >> 1) < sr_sizes[device_nr] &&
		    scsi_CDs[device_nr].capacity - error_sector < 4 * 75)
			sr_sizes[device_nr] = error_sector >> 1;
	}
	/*
	 * This calls the generic completion function, now that we know
	 * how many actual sectors finished, and how many sectors we need
	 * to say have failed.
	 */
	scsi_io_completion(SCpnt, good_sectors, block_sectors);
}


static request_queue_t *sr_find_queue(kdev_t dev)
{
	/*
	 * No such device
	 */
	if (MINOR(dev) >= sr_template.dev_max || !scsi_CDs[MINOR(dev)].device)
		return NULL;

	return &scsi_CDs[MINOR(dev)].device->request_queue;
}

static int sr_init_command(Scsi_Cmnd * SCpnt)
{
	int dev, devm, block, this_count;

	devm = MINOR(SCpnt->request.rq_dev);
	dev = DEVICE_NR(SCpnt->request.rq_dev);

	block = SCpnt->request.sector;
	this_count = SCpnt->request_bufflen >> 9;

	if (!SCpnt->request.bh) {
		/*
		 * Umm, yeah, right.   Swapping to a cdrom.  Nice try.
		 */
		return 0;
	}
	SCSI_LOG_HLQUEUE(1, printk("Doing sr request, dev = %d, block = %d\n", devm, block));

	if (dev >= sr_template.nr_dev ||
	    !scsi_CDs[dev].device ||
	    !scsi_CDs[dev].device->online) {
		SCSI_LOG_HLQUEUE(2, printk("Finishing %ld sectors\n", SCpnt->request.nr_sectors));
		SCSI_LOG_HLQUEUE(2, printk("Retry with 0x%p\n", SCpnt));
		return 0;
	}
	if (scsi_CDs[dev].device->changed) {
		/*
		 * quietly refuse to do anything to a changed disc until the changed
		 * bit has been reset
		 */
		/* printk("SCSI disk has been changed. Prohibiting further I/O.\n"); */
		return 0;
	}
	/*
	 * we do lazy blocksize switching (when reading XA sectors,
	 * see CDROMREADMODE2 ioctl) 
	 */
	if (scsi_CDs[dev].device->sector_size > 2048) {
		if (!in_interrupt())
			sr_set_blocklength(DEVICE_NR(CURRENT->rq_dev), 2048);
		else
			printk("sr: can't switch blocksize: in interrupt\n");
	}

	if ((SCpnt->request.cmd == WRITE) && !scsi_CDs[dev].device->writeable)
		return 0;

	if (scsi_CDs[dev].device->sector_size == 1024) {
		if ((block & 1) || (SCpnt->request.nr_sectors & 1)) {
			printk("sr.c:Bad 1K block number requested (%d %ld)",
                               block, SCpnt->request.nr_sectors);
			return 0;
		} else {
			block = block >> 1;
			this_count = this_count >> 1;
		}
	}
	if (scsi_CDs[dev].device->sector_size == 2048) {
		if ((block & 3) || (SCpnt->request.nr_sectors & 3)) {
			printk("sr.c:Bad 2K block number requested (%d %ld)",
                               block, SCpnt->request.nr_sectors);
			return 0;
		} else {
			block = block >> 2;
			this_count = this_count >> 2;
		}
	}
	switch (SCpnt->request.cmd) {
	case WRITE:
		SCpnt->cmnd[0] = WRITE_10;
		SCpnt->sc_data_direction = SCSI_DATA_WRITE;
		break;
	case READ:
		SCpnt->cmnd[0] = READ_10;
		SCpnt->sc_data_direction = SCSI_DATA_READ;
		break;
	default:
		panic("Unknown sr command %d\n", SCpnt->request.cmd);
	}

	SCSI_LOG_HLQUEUE(2, printk("sr%d : %s %d/%ld 512 byte blocks.\n",
                                   devm,
		   (SCpnt->request.cmd == WRITE) ? "writing" : "reading",
				 this_count, SCpnt->request.nr_sectors));

	SCpnt->cmnd[1] = (SCpnt->lun << 5) & 0xe0;

	if (this_count > 0xffff)
		this_count = 0xffff;

	SCpnt->cmnd[2] = (unsigned char) (block >> 24) & 0xff;
	SCpnt->cmnd[3] = (unsigned char) (block >> 16) & 0xff;
	SCpnt->cmnd[4] = (unsigned char) (block >> 8) & 0xff;
	SCpnt->cmnd[5] = (unsigned char) block & 0xff;
	SCpnt->cmnd[6] = SCpnt->cmnd[9] = 0;
	SCpnt->cmnd[7] = (unsigned char) (this_count >> 8) & 0xff;
	SCpnt->cmnd[8] = (unsigned char) this_count & 0xff;

	/*
	 * We shouldn't disconnect in the middle of a sector, so with a dumb
	 * host adapter, it's safe to assume that we can at least transfer
	 * this many bytes between each connect / disconnect.
	 */
	SCpnt->transfersize = scsi_CDs[dev].device->sector_size;
	SCpnt->underflow = this_count << 9;

	SCpnt->allowed = MAX_RETRIES;
	SCpnt->timeout_per_command = SR_TIMEOUT;

	/*
	 * This is the completion routine we use.  This is matched in terms
	 * of capability to this function.
	 */
	SCpnt->done = rw_intr;

	/*
	 * This indicates that the command is ready from our end to be
	 * queued.
	 */
	return 1;
}

static int sr_open(struct cdrom_device_info *cdi, int purpose)
{
	check_disk_change(cdi->dev);

	if (MINOR(cdi->dev) >= sr_template.dev_max
	    || !scsi_CDs[MINOR(cdi->dev)].device) {
		return -ENXIO;	/* No such device */
	}
	/*
	 * If the device is in error recovery, wait until it is done.
	 * If the device is offline, then disallow any access to it.
	 */
	if (!scsi_block_when_processing_errors(scsi_CDs[MINOR(cdi->dev)].device)) {
		return -ENXIO;
	}
	scsi_CDs[MINOR(cdi->dev)].device->access_count++;
	if (scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module)
		__MOD_INC_USE_COUNT(scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module);
	if (sr_template.module)
		__MOD_INC_USE_COUNT(sr_template.module);

	/* If this device did not have media in the drive at boot time, then
	 * we would have been unable to get the sector size.  Check to see if
	 * this is the case, and try again.
	 */

	if (scsi_CDs[MINOR(cdi->dev)].needs_sector_size)
		get_sectorsize(MINOR(cdi->dev));

	return 0;
}

/*
 * do_sr_request() is the request handler function for the sr driver.
 * Its function in life is to take block device requests, and
 * translate them to SCSI commands.
 */


static int sr_detect(Scsi_Device * SDp)
{

	if (SDp->type != TYPE_ROM && SDp->type != TYPE_WORM)
		return 0;

	printk("Detected scsi CD-ROM sr%d at scsi%d, channel %d, id %d, lun %d\n",
	       sr_template.dev_noticed++,
	       SDp->host->host_no, SDp->channel, SDp->id, SDp->lun);

	return 1;
}

static int sr_attach(Scsi_Device * SDp)
{
	Scsi_CD *cpnt;
	int i;

	if (SDp->type != TYPE_ROM && SDp->type != TYPE_WORM)
		return 1;

	if (sr_template.nr_dev >= sr_template.dev_max) {
		SDp->attached--;
		return 1;
	}
	for (cpnt = scsi_CDs, i = 0; i < sr_template.dev_max; i++, cpnt++)
		if (!cpnt->device)
			break;

	if (i >= sr_template.dev_max)
		panic("scsi_devices corrupt (sr)");


	scsi_CDs[i].device = SDp;

	sr_template.nr_dev++;
	if (sr_template.nr_dev > sr_template.dev_max)
		panic("scsi_devices corrupt (sr)");
	return 0;
}


void get_sectorsize(int i)
{
	unsigned char cmd[10];
	unsigned char *buffer;
	int the_result, retries;
	int sector_size;
	Scsi_Request *SRpnt;

	buffer = (unsigned char *) scsi_malloc(512);


	SRpnt = scsi_allocate_request(scsi_CDs[i].device);

	retries = 3;
	do {
		cmd[0] = READ_CAPACITY;
		cmd[1] = (scsi_CDs[i].device->lun << 5) & 0xe0;
		memset((void *) &cmd[2], 0, 8);
		SRpnt->sr_request.rq_status = RQ_SCSI_BUSY;	/* Mark as really busy */
		SRpnt->sr_cmd_len = 0;

		memset(buffer, 0, 8);

		/* Do the command and wait.. */

		SRpnt->sr_data_direction = SCSI_DATA_READ;
		scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
			      8, SR_TIMEOUT, MAX_RETRIES);

		the_result = SRpnt->sr_result;
		retries--;

	} while (the_result && retries);


	scsi_release_request(SRpnt);
	SRpnt = NULL;

	if (the_result) {
		scsi_CDs[i].capacity = 0x1fffff;
		sector_size = 2048;	/* A guess, just in case */
		scsi_CDs[i].needs_sector_size = 1;
	} else {
#if 0
		if (cdrom_get_last_written(MKDEV(MAJOR_NR, i),
					 (long *) &scsi_CDs[i].capacity))
#endif
			scsi_CDs[i].capacity = 1 + ((buffer[0] << 24) |
						    (buffer[1] << 16) |
						    (buffer[2] << 8) |
						    buffer[3]);
		sector_size = (buffer[4] << 24) |
		    (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
		switch (sector_size) {
			/*
			 * HP 4020i CD-Recorder reports 2340 byte sectors
			 * Philips CD-Writers report 2352 byte sectors
			 *
			 * Use 2k sectors for them..
			 */
		case 0:
		case 2340:
		case 2352:
			sector_size = 2048;
			/* fall through */
		case 2048:
			scsi_CDs[i].capacity *= 4;
			/* fall through */
		case 512:
			break;
		default:
			printk("sr%d: unsupported sector size %d.\n",
			       i, sector_size);
			scsi_CDs[i].capacity = 0;
			scsi_CDs[i].needs_sector_size = 1;
		}

		scsi_CDs[i].device->sector_size = sector_size;

		/*
		 * Add this so that we have the ability to correctly gauge
		 * what the device is capable of.
		 */
		scsi_CDs[i].needs_sector_size = 0;
		sr_sizes[i] = scsi_CDs[i].capacity >> (BLOCK_SIZE_BITS - 9);
	};
	scsi_free(buffer, 512);
}

void get_capabilities(int i)
{
	unsigned char cmd[6];
	unsigned char *buffer;
	int rc, n;

	static char *loadmech[] =
	{
		"caddy",
		"tray",
		"pop-up",
		"",
		"changer",
		"cartridge changer",
		"",
		""
	};

	buffer = (unsigned char *) scsi_malloc(512);
	cmd[0] = MODE_SENSE;
	cmd[1] = (scsi_CDs[i].device->lun << 5) & 0xe0;
	cmd[2] = 0x2a;
	cmd[4] = 128;
	cmd[3] = cmd[5] = 0;
	rc = sr_do_ioctl(i, cmd, buffer, 128, 1, SCSI_DATA_READ);

	if (-EINVAL == rc) {
		/* failed, drive has'nt this mode page */
		scsi_CDs[i].cdi.speed = 1;
		/* disable speed select, drive probably can't do this either */
		scsi_CDs[i].cdi.mask |= CDC_SELECT_SPEED;
		scsi_free(buffer, 512);
		return;
	}
	n = buffer[3] + 4;
	scsi_CDs[i].cdi.speed = ((buffer[n + 8] << 8) + buffer[n + 9]) / 176;
	scsi_CDs[i].readcd_known = 1;
	scsi_CDs[i].readcd_cdda = buffer[n + 5] & 0x01;
	/* print some capability bits */
	printk("sr%i: scsi3-mmc drive: %dx/%dx %s%s%s%s%s%s\n", i,
	       ((buffer[n + 14] << 8) + buffer[n + 15]) / 176,
	       scsi_CDs[i].cdi.speed,
	       buffer[n + 3] & 0x01 ? "writer " : "",	/* CD Writer */
	       buffer[n + 3] & 0x20 ? "dvd-ram " : "",
	       buffer[n + 2] & 0x02 ? "cd/rw " : "",	/* can read rewriteable */
	       buffer[n + 4] & 0x20 ? "xa/form2 " : "",		/* can read xa/from2 */
	       buffer[n + 5] & 0x01 ? "cdda " : "",	/* can read audio data */
	       loadmech[buffer[n + 6] >> 5]);
	if ((buffer[n + 6] >> 5) == 0)
		/* caddy drives can't close tray... */
		scsi_CDs[i].cdi.mask |= CDC_CLOSE_TRAY;
	if ((buffer[n + 2] & 0x8) == 0)
		/* not a DVD drive */
		scsi_CDs[i].cdi.mask |= CDC_DVD;
	if ((buffer[n + 3] & 0x20) == 0) {
		/* can't write DVD-RAM media */
		scsi_CDs[i].cdi.mask |= CDC_DVD_RAM;
	} else {
		scsi_CDs[i].device->writeable = 1;
	}
	if ((buffer[n + 3] & 0x10) == 0)
		/* can't write DVD-R media */
		scsi_CDs[i].cdi.mask |= CDC_DVD_R;
	if ((buffer[n + 3] & 0x2) == 0)
		/* can't write CD-RW media */
		scsi_CDs[i].cdi.mask |= CDC_CD_RW;
	if ((buffer[n + 3] & 0x1) == 0)
		/* can't write CD-R media */
		scsi_CDs[i].cdi.mask |= CDC_CD_R;
	if ((buffer[n + 6] & 0x8) == 0)
		/* can't eject */
		scsi_CDs[i].cdi.mask |= CDC_OPEN_TRAY;

	if ((buffer[n + 6] >> 5) == mechtype_individual_changer ||
	    (buffer[n + 6] >> 5) == mechtype_cartridge_changer)
		scsi_CDs[i].cdi.capacity =
		    cdrom_number_of_slots(&(scsi_CDs[i].cdi));
	if (scsi_CDs[i].cdi.capacity <= 1)
		/* not a changer */
		scsi_CDs[i].cdi.mask |= CDC_SELECT_DISC;
	/*else    I don't think it can close its tray
	   scsi_CDs[i].cdi.mask |= CDC_CLOSE_TRAY; */

	scsi_free(buffer, 512);
}

/*
 * sr_packet() is the entry point for the generic commands generated
 * by the Uniform CD-ROM layer. 
 */
static int sr_packet(struct cdrom_device_info *cdi, struct cdrom_generic_command *cgc)
{
	Scsi_Request *SRpnt;
	Scsi_Device *device = scsi_CDs[MINOR(cdi->dev)].device;
	unsigned char *buffer = cgc->buffer;
	int buflen;

	/* get the device */
	SRpnt = scsi_allocate_request(device);
	if (SRpnt == NULL)
		return -ENODEV;	/* this just doesn't seem right /axboe */

	/* use buffer for ISA DMA */
	buflen = (cgc->buflen + 511) & ~511;
	if (cgc->buffer && SRpnt->sr_host->unchecked_isa_dma &&
	    (virt_to_phys(cgc->buffer) + cgc->buflen - 1 > ISA_DMA_THRESHOLD)) {
		buffer = scsi_malloc(buflen);
		if (buffer == NULL) {
			printk("sr: SCSI DMA pool exhausted.");
			return -ENOMEM;
		}
		memcpy(buffer, cgc->buffer, cgc->buflen);
	}
	/* set the LUN */
	cgc->cmd[1] |= device->lun << 5;

	/* do the locking and issue the command */
	SRpnt->sr_request.rq_dev = cdi->dev;
	/* scsi_wait_req sets the command length */
	SRpnt->sr_cmd_len = 0;

	SRpnt->sr_data_direction = cgc->data_direction;
	scsi_wait_req(SRpnt, (void *) cgc->cmd, (void *) buffer, cgc->buflen,
		      SR_TIMEOUT, MAX_RETRIES);

	if ((cgc->stat = SRpnt->sr_result))
		cgc->sense = (struct request_sense *) SRpnt->sr_sense_buffer;

	/* release */
	SRpnt->sr_request.rq_dev = MKDEV(0, 0);
	scsi_release_request(SRpnt);
	SRpnt = NULL;

	/* write DMA buffer back if used */
	if (buffer && (buffer != cgc->buffer)) {
		memcpy(cgc->buffer, buffer, cgc->buflen);
		scsi_free(buffer, buflen);
	}


	return cgc->stat;
}

static int sr_registered = 0;

static int sr_init()
{
	int i;

	if (sr_template.dev_noticed == 0)
		return 0;

	if (!sr_registered) {
		if (devfs_register_blkdev(MAJOR_NR, "sr", &cdrom_fops)) {
			printk("Unable to get major %d for SCSI-CD\n", MAJOR_NR);
			return 1;
		}
		sr_registered++;
	}
	if (scsi_CDs)
		return 0;
	sr_template.dev_max =
	    sr_template.dev_noticed + SR_EXTRA_DEVS;
	scsi_CDs = (Scsi_CD *) kmalloc(sr_template.dev_max * sizeof(Scsi_CD), GFP_ATOMIC);
	memset(scsi_CDs, 0, sr_template.dev_max * sizeof(Scsi_CD));

	sr_sizes = (int *) kmalloc(sr_template.dev_max * sizeof(int), GFP_ATOMIC);
	memset(sr_sizes, 0, sr_template.dev_max * sizeof(int));

	sr_blocksizes = (int *) kmalloc(sr_template.dev_max *
					sizeof(int), GFP_ATOMIC);

	sr_hardsizes = (int *) kmalloc(sr_template.dev_max *
				       sizeof(int), GFP_ATOMIC);
	/*
	 * These are good guesses for the time being.
	 */
	for (i = 0; i < sr_template.dev_max; i++)
        {
		sr_blocksizes[i] = 2048;
		sr_hardsizes[i] = 2048;
        }
	blksize_size[MAJOR_NR] = sr_blocksizes;
        hardsect_size[MAJOR_NR] = sr_hardsizes;
	return 0;
}

void sr_finish()
{
	int i;
	char name[6];

	blk_dev[MAJOR_NR].queue = sr_find_queue;
	blk_size[MAJOR_NR] = sr_sizes;

	for (i = 0; i < sr_template.nr_dev; ++i) {
		/* If we have already seen this, then skip it.  Comes up
		 * with loadable modules. */
		if (scsi_CDs[i].capacity)
			continue;
		scsi_CDs[i].capacity = 0x1fffff;
		scsi_CDs[i].device->sector_size = 2048;		/* A guess, just in case */
		scsi_CDs[i].needs_sector_size = 1;
		scsi_CDs[i].device->changed = 1;	/* force recheck CD type */
#if 0
		/* seems better to leave this for later */
		get_sectorsize(i);
		printk("Scd sectorsize = %d bytes.\n", scsi_CDs[i].sector_size);
#endif
		scsi_CDs[i].use = 1;

		scsi_CDs[i].device->ten = 1;
		scsi_CDs[i].device->remap = 1;
		scsi_CDs[i].readcd_known = 0;
		scsi_CDs[i].readcd_cdda = 0;
		sr_sizes[i] = scsi_CDs[i].capacity >> (BLOCK_SIZE_BITS - 9);

		scsi_CDs[i].cdi.ops = &sr_dops;
		scsi_CDs[i].cdi.handle = &scsi_CDs[i];
		scsi_CDs[i].cdi.dev = MKDEV(MAJOR_NR, i);
		scsi_CDs[i].cdi.mask = 0;
		scsi_CDs[i].cdi.capacity = 1;
		get_capabilities(i);
		sr_vendor_init(i);

		sprintf(name, "sr%d", i);
		strcpy(scsi_CDs[i].cdi.name, name);
                scsi_CDs[i].cdi.de =
                    devfs_register (scsi_CDs[i].device->de, "cd",
                                    DEVFS_FL_DEFAULT, MAJOR_NR, i,
                                    S_IFBLK | S_IRUGO | S_IWUGO,
                                    &cdrom_fops, NULL);
		register_cdrom(&scsi_CDs[i].cdi);
	}


	/* If our host adapter is capable of scatter-gather, then we increase
	 * the read-ahead to 16 blocks (32 sectors).  If not, we use
	 * a two block (4 sector) read ahead. */
	if (scsi_CDs[0].device && scsi_CDs[0].device->host->sg_tablesize)
		read_ahead[MAJOR_NR] = 32;	/* 32 sector read-ahead.  Always removable. */
	else
		read_ahead[MAJOR_NR] = 4;	/* 4 sector read-ahead */

	return;
}

static void sr_detach(Scsi_Device * SDp)
{
	Scsi_CD *cpnt;
	int i;

	for (cpnt = scsi_CDs, i = 0; i < sr_template.dev_max; i++, cpnt++)
		if (cpnt->device == SDp) {
			kdev_t devi = MKDEV(MAJOR_NR, i);
			struct super_block *sb = get_super(devi);

			/*
			 * Since the cdrom is read-only, no need to sync the device.
			 * We should be kind to our buffer cache, however.
			 */
			if (sb)
				invalidate_inodes(sb);
			invalidate_buffers(devi);

			/*
			 * Reset things back to a sane state so that one can re-load a new
			 * driver (perhaps the same one).
			 */
			unregister_cdrom(&(cpnt->cdi));
			cpnt->device = NULL;
			cpnt->capacity = 0;
			SDp->attached--;
			sr_template.nr_dev--;
			sr_template.dev_noticed--;
			sr_sizes[i] = 0;
			return;
		}
	return;
}


#ifdef MODULE

int init_module(void)
{
	sr_template.module = &__this_module;
	return scsi_register_module(MODULE_SCSI_DEV, &sr_template);
}

void cleanup_module(void)
{
	scsi_unregister_module(MODULE_SCSI_DEV, &sr_template);
	devfs_unregister_blkdev(MAJOR_NR, "sr");
	sr_registered--;
	if (scsi_CDs != NULL) {
		kfree((char *) scsi_CDs);

		kfree((char *) sr_sizes);
		sr_sizes = NULL;

		kfree((char *) sr_blocksizes);
		sr_blocksizes = NULL;
		kfree((char *) sr_hardsizes);
		sr_hardsizes = NULL;
	}
	blksize_size[MAJOR_NR] = NULL;
        hardsect_size[MAJOR_NR] = NULL;
	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
	blk_size[MAJOR_NR] = NULL;
	read_ahead[MAJOR_NR] = 0;

	sr_template.dev_max = 0;
}

#endif				/* MODULE */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
