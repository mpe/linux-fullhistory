/*
 *  sr.c Copyright (C) 1992 David Giller
 *	     Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *
 *  adapted from:
 *	sd.c Copyright (C) 1992 Drew Eckhardt
 *	Linux scsi disk driver by
 *		Drew Eckhardt <drew@colorado.edu>
 *
 *      Modified by Eric Youngdale ericy@cais.com to
 *      add scatter-gather, multiple outstanding request, and other
 *      enhancements.
 *
 *	    Modified by Eric Youngdale eric@aib.com to support loadable
 *	    low-level scsi drivers.
 *
 *	 Modified by Thomas Quinot thomas@melchior.cuivre.fdn.fr to
 *	 provide auto-eject.
 *
 *          Modified by Gerd Knorr <kraxel@cs.tu-berlin.de> to support the
 *          generic cdrom interface
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

#define MAJOR_NR SCSI_CDROM_MAJOR
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include "sr.h"
#include <scsi/scsi_ioctl.h>   /* For the door lock/unlock commands */
#include "constants.h"

MODULE_PARM(xa_test,"i"); /* see sr_ioctl.c */

#define MAX_RETRIES 3
#define SR_TIMEOUT (30 * HZ)

static int sr_init(void);
static void sr_finish(void);
static int sr_attach(Scsi_Device *);
static int sr_detect(Scsi_Device *);
static void sr_detach(Scsi_Device *);

struct Scsi_Device_Template sr_template = {NULL, "cdrom", "sr", NULL, TYPE_ROM,
                                           SCSI_CDROM_MAJOR, 0, 0, 0, 1,
                                           sr_detect, sr_init,
                                           sr_finish, sr_attach, sr_detach};

Scsi_CD * scsi_CDs = NULL;
static int * sr_sizes = NULL;

static int * sr_blocksizes = NULL;

static int sr_open(struct cdrom_device_info*, int);
void get_sectorsize(int);
void get_capabilities(int);

void requeue_sr_request (Scsi_Cmnd * SCpnt);
static int sr_media_change(struct cdrom_device_info*, int);

static void sr_release(struct cdrom_device_info *cdi)
{
	if (scsi_CDs[MINOR(cdi->dev)].sector_size > 2048)
		sr_set_blocklength(MINOR(cdi->dev),2048);
	sync_dev(cdi->dev);
	scsi_CDs[MINOR(cdi->dev)].device->access_count--;
	if (scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module)
		__MOD_DEC_USE_COUNT(scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module);
	if(sr_template.module)
        	__MOD_DEC_USE_COUNT(sr_template.module);
}

static struct cdrom_device_ops sr_dops = {
        sr_open,                      /* open */
        sr_release,                   /* release */
        sr_drive_status,              /* drive status */
        sr_media_change,              /* media changed */
        sr_tray_move,                 /* tray move */
        sr_lock_door,                 /* lock door */
        sr_select_speed,              /* select speed */
        NULL,                         /* select disc */
        sr_get_last_session,          /* get last session */
        sr_get_mcn,                   /* get universal product code */
        sr_reset,                     /* hard reset */
        sr_audio_ioctl,               /* audio ioctl */
        sr_dev_ioctl,                 /* device-specific ioctl */
        CDC_CLOSE_TRAY | CDC_OPEN_TRAY| CDC_LOCK | CDC_SELECT_SPEED |
        CDC_MULTI_SESSION | CDC_MCN | CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO |
        CDC_RESET | CDC_IOCTLS | CDC_DRIVE_STATUS,
        0
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

int sr_media_change(struct cdrom_device_info *cdi, int slot){
	int retval;

        if (CDSL_CURRENT != slot) {
                /* no changer support */
                return -EINVAL;
        }

	retval = scsi_ioctl(scsi_CDs[MINOR(cdi->dev)].device,
                            SCSI_IOCTL_TEST_UNIT_READY, 0);

	if(retval)
        {
                /* Unable to test, unit probably not ready.  This usually
		 * means there is no disc in the drive.  Mark as changed,
		 * and we will figure it out later once the drive is
		 * available again.  */

                scsi_CDs[MINOR(cdi->dev)].device->changed = 1;
                return 1; /* This will force a flush, if called from
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

                scsi_CDs[MINOR(cdi->dev)].sector_size = 2048;
        }
	return retval;
}

/*
 * rw_intr is the interrupt routine for the device driver.  It will be notified on the
 * end of a SCSI read / write, and will take on of several actions based on success or failure.
 */

static void rw_intr (Scsi_Cmnd * SCpnt)
{
	int result = SCpnt->result;
	int this_count = SCpnt->this_count;
	int good_sectors = (result == 0 ? this_count : 0);
	int block_sectors = 0;

#ifdef DEBUG
	printk("sr.c done: %x %x\n",result, SCpnt->request.bh->b_data);
#endif
    /*
      Handle MEDIUM ERRORs or VOLUME OVERFLOWs that indicate partial success.
      Since this is a relatively rare error condition, no care is taken to
      avoid unnecessary additional work such as memcpy's that could be avoided.
    */

	if (driver_byte(result) != 0 &&		    /* An error occurred */
	    SCpnt->sense_buffer[0] == 0xF0 &&	    /* Sense data is valid */
	    (SCpnt->sense_buffer[2] == MEDIUM_ERROR ||
	     SCpnt->sense_buffer[2] == VOLUME_OVERFLOW ||
	     SCpnt->sense_buffer[2] == ILLEGAL_REQUEST))
	  {
	    long error_sector = (SCpnt->sense_buffer[3] << 24) |
				(SCpnt->sense_buffer[4] << 16) |
				(SCpnt->sense_buffer[5] << 8) |
				SCpnt->sense_buffer[6];
	    int device_nr = DEVICE_NR(SCpnt->request.rq_dev);
	    if (SCpnt->request.bh != NULL)
	      block_sectors = SCpnt->request.bh->b_size >> 9;
	    if (block_sectors < 4) block_sectors = 4;
	    if (scsi_CDs[device_nr].sector_size == 2048)
	      error_sector <<= 2;
	    error_sector &= ~ (block_sectors - 1);
	    good_sectors = error_sector - SCpnt->request.sector;
	    if (good_sectors < 0 || good_sectors >= this_count)
	      good_sectors = 0;
	    /*
	      The SCSI specification allows for the value returned by READ
	      CAPACITY to be up to 75 2K sectors past the last readable
	      block.  Therefore, if we hit a medium error within the last
	      75 2K sectors, we decrease the saved size value.
	    */
	    if ((error_sector >> 1) < sr_sizes[device_nr] &&
		scsi_CDs[device_nr].capacity - error_sector < 4*75)
	      sr_sizes[device_nr] = error_sector >> 1;
	  }

	if (good_sectors > 0)
    { /* Some sectors were read successfully. */
	if (SCpnt->use_sg == 0) {
		    if (SCpnt->buffer != SCpnt->request.buffer)
	    {
		int offset;
		offset = (SCpnt->request.sector % 4) << 9;
		memcpy((char *)SCpnt->request.buffer,
		       (char *)SCpnt->buffer + offset,
		       good_sectors << 9);
		/* Even though we are not using scatter-gather, we look
		 * ahead and see if there is a linked request for the
		 * other half of this buffer.  If there is, then satisfy
		 * it. */
		if((offset == 0) && good_sectors == 2 &&
		   SCpnt->request.nr_sectors > good_sectors &&
		   SCpnt->request.bh &&
		   SCpnt->request.bh->b_reqnext &&
		   SCpnt->request.bh->b_reqnext->b_size == 1024) {
		    memcpy((char *)SCpnt->request.bh->b_reqnext->b_data,
			   (char *)SCpnt->buffer + 1024,
			   1024);
		    good_sectors += 2;
		};

		scsi_free(SCpnt->buffer, 2048);
	    }
	} else {
		    struct scatterlist * sgpnt;
		    int i;
		    sgpnt = (struct scatterlist *) SCpnt->buffer;
		    for(i=0; i<SCpnt->use_sg; i++) {
		if (sgpnt[i].alt_address) {
		    if (sgpnt[i].alt_address != sgpnt[i].address) {
			memcpy(sgpnt[i].alt_address, sgpnt[i].address, sgpnt[i].length);
		    };
		    scsi_free(sgpnt[i].address, sgpnt[i].length);
		};
		    };
		    scsi_free(SCpnt->buffer, SCpnt->sglist_len);  /* Free list of scatter-gather pointers */
		    if(SCpnt->request.sector % 4) good_sectors -= 2;
	    /* See   if there is a padding record at the end that needs to be removed */
		    if(good_sectors > SCpnt->request.nr_sectors)
		good_sectors -= 2;
	};

#ifdef DEBUG
		printk("(%x %x %x) ",SCpnt->request.bh, SCpnt->request.nr_sectors,
		       good_sectors);
#endif
		if (SCpnt->request.nr_sectors > this_count)
	{
			SCpnt->request.errors = 0;
			if (!SCpnt->request.bh)
			    panic("sr.c: linked page request (%lx %x)",
		      SCpnt->request.sector, this_count);
	}

	SCpnt = end_scsi_request(SCpnt, 1, good_sectors);  /* All done */
	if (result == 0)
	  {
	    requeue_sr_request(SCpnt);
	    return;
	  }
    }

    if (good_sectors == 0) {
	/* We only come through here if no sectors were read successfully. */

    /* Free up any indirection buffers we allocated for DMA purposes. */
	if (SCpnt->use_sg) {
	struct scatterlist * sgpnt;
	int i;
	sgpnt = (struct scatterlist *) SCpnt->buffer;
	for(i=0; i<SCpnt->use_sg; i++) {
	    if (sgpnt[i].alt_address) {
		scsi_free(sgpnt[i].address, sgpnt[i].length);
	    }
	}
	scsi_free(SCpnt->buffer, SCpnt->sglist_len);  /* Free list of scatter-gather pointers */
	} else {
	if (SCpnt->buffer != SCpnt->request.buffer)
	    scsi_free(SCpnt->buffer, SCpnt->bufflen);
	}

    }

	if (driver_byte(result) != 0) {
		if ((SCpnt->sense_buffer[0] & 0x7f) == 0x70) {
			if ((SCpnt->sense_buffer[2] & 0xf) == UNIT_ATTENTION) {
				/* detected disc change.  set a bit and quietly refuse
				 * further access.	*/

				scsi_CDs[DEVICE_NR(SCpnt->request.rq_dev)].device->changed = 1;
				SCpnt = end_scsi_request(SCpnt, 0, this_count);
				requeue_sr_request(SCpnt);
				return;
			}
		}

		if (SCpnt->sense_buffer[2] == ILLEGAL_REQUEST) {
			printk("sr%d: CD-ROM error: ",
                               DEVICE_NR(SCpnt->request.rq_dev));
			print_sense("sr", SCpnt);
			printk("command was: ");
			print_command(SCpnt->cmnd);
			if (scsi_CDs[DEVICE_NR(SCpnt->request.rq_dev)].ten) {
				scsi_CDs[DEVICE_NR(SCpnt->request.rq_dev)].ten = 0;
				requeue_sr_request(SCpnt);
				result = 0;
				return;
			} else {
				SCpnt = end_scsi_request(SCpnt, 0, this_count);
				requeue_sr_request(SCpnt); /* Do next request */
				return;
			}

		}

		if (SCpnt->sense_buffer[2] == NOT_READY) {
			printk(KERN_INFO "sr%d: CD-ROM not ready.  Make sure you have a disc in the drive.\n",
                               DEVICE_NR(SCpnt->request.rq_dev));
			SCpnt = end_scsi_request(SCpnt, 0, this_count);
			requeue_sr_request(SCpnt); /* Do next request */
			return;
		}

		if (SCpnt->sense_buffer[2] == MEDIUM_ERROR) {
		    printk("scsi%d: MEDIUM ERROR on "
			   "channel %d, id %d, lun %d, CDB: ",
			   SCpnt->host->host_no, (int) SCpnt->channel,
			   (int) SCpnt->target, (int) SCpnt->lun);
		    print_command(SCpnt->cmnd);
		    print_sense("sr", SCpnt);
		    SCpnt = end_scsi_request(SCpnt, 0, block_sectors);
		    requeue_sr_request(SCpnt);
		    return;
		}

		if (SCpnt->sense_buffer[2] == VOLUME_OVERFLOW) {
		    printk("scsi%d: VOLUME OVERFLOW on "
			   "channel %d, id %d, lun %d, CDB: ",
			   SCpnt->host->host_no, (int) SCpnt->channel,
			   (int) SCpnt->target, (int) SCpnt->lun);
		    print_command(SCpnt->cmnd);
		    print_sense("sr", SCpnt);
		    SCpnt = end_scsi_request(SCpnt, 0, block_sectors);
		    requeue_sr_request(SCpnt);
		    return;
		}
        }

	/* We only get this far if we have an error we have not recognized */
	if(result) {
	printk("SCSI CD error : host %d id %d lun %d return code = %03x\n",
	       scsi_CDs[DEVICE_NR(SCpnt->request.rq_dev)].device->host->host_no,
	       scsi_CDs[DEVICE_NR(SCpnt->request.rq_dev)].device->id,
	       scsi_CDs[DEVICE_NR(SCpnt->request.rq_dev)].device->lun,
	       result);

	if (status_byte(result) == CHECK_CONDITION)
	    print_sense("sr", SCpnt);

	SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.current_nr_sectors);
	requeue_sr_request(SCpnt);
    }
}

static int sr_open(struct cdrom_device_info *cdi, int purpose)
{
    check_disk_change(cdi->dev);

    if(   MINOR(cdi->dev) >= sr_template.dev_max 
       || !scsi_CDs[MINOR(cdi->dev)].device)
      {
	return -ENXIO;   /* No such device */
      }

    /*
     * If the device is in error recovery, wait until it is done.
     * If the device is offline, then disallow any access to it.
     */
    if( !scsi_block_when_processing_errors(scsi_CDs[MINOR(cdi->dev)].device) )
      {
        return -ENXIO;
      }

    scsi_CDs[MINOR(cdi->dev)].device->access_count++;
    if (scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module)
	__MOD_INC_USE_COUNT(scsi_CDs[MINOR(cdi->dev)].device->host->hostt->module);
    if(sr_template.module)
        __MOD_INC_USE_COUNT(sr_template.module);

    /* If this device did not have media in the drive at boot time, then
     * we would have been unable to get the sector size.  Check to see if
     * this is the case, and try again.
     */

    if(scsi_CDs[MINOR(cdi->dev)].needs_sector_size)
	get_sectorsize(MINOR(cdi->dev));

    return 0;
}

/*
 * do_sr_request() is the request handler function for the sr driver.
 * Its function in life is to take block device requests, and
 * translate them to SCSI commands.
 */

static void do_sr_request (void)
{
    Scsi_Cmnd * SCpnt = NULL;
    struct request * req = NULL;
    Scsi_Device * SDev;
    int flag = 0;

    while (1==1){
	if (CURRENT != NULL && CURRENT->rq_status == RQ_INACTIVE) {
	    return;
	};

	INIT_SCSI_REQUEST;

	SDev = scsi_CDs[DEVICE_NR(CURRENT->rq_dev)].device;

        /*
         * If the host for this device is in error recovery mode, don't
         * do anything at all here.  When the host leaves error recovery
         * mode, it will automatically restart things and start queueing
         * commands again.
         */
        if( SDev->host->in_recovery )
          {
            return;
          }

	/*
	 * I am not sure where the best place to do this is.  We need
	 * to hook in a place where we are likely to come if in user
	 * space.
	 */
	if( SDev->was_reset )
	{
 	    /*
 	     * We need to relock the door, but we might
 	     * be in an interrupt handler.  Only do this
 	     * from user space, since we do not want to
 	     * sleep from an interrupt.
 	     */
 	    if( SDev->removable && !in_interrupt() )
 	    {
		spin_unlock_irq(&io_request_lock);		/* FIXME!!!! */
		scsi_ioctl(SDev, SCSI_IOCTL_DOORLOCK, 0);
		spin_lock_irq(&io_request_lock);		/* FIXME!!!! */
		/* scsi_ioctl may allow CURRENT to change, so start over. */
		SDev->was_reset = 0;
		continue;
 	    }
 	    SDev->was_reset = 0;
	}

	/* we do lazy blocksize switching (when reading XA sectors,
	 * see CDROMREADMODE2 ioctl) */
	if (scsi_CDs[DEVICE_NR(CURRENT->rq_dev)].sector_size > 2048) {
	    if (!in_interrupt())
		sr_set_blocklength(DEVICE_NR(CURRENT->rq_dev),2048);
#if 1
            else
                printk("sr: can't switch blocksize: in interrupt\n");
#endif
	}

	if (flag++ == 0)
	    SCpnt = scsi_allocate_device(&CURRENT,
				    scsi_CDs[DEVICE_NR(CURRENT->rq_dev)].device, 0);
	else SCpnt = NULL;

	/* This is a performance enhancement.  We dig down into the request list and
	 * try to find a queueable request (i.e. device not busy, and host able to
	 * accept another command.  If we find one, then we queue it. This can
	 * make a big difference on systems with more than one disk drive.  We want
	 * to have the interrupts off when monkeying with the request list, because
	 * otherwise the kernel might try to slip in a request in between somewhere. */

	if (!SCpnt && sr_template.nr_dev > 1){
	    struct request *req1;
	    req1 = NULL;
	    req = CURRENT;
	    while(req){
		SCpnt = scsi_request_queueable(req,
					  scsi_CDs[DEVICE_NR(req->rq_dev)].device);
		if(SCpnt) break;
		req1 = req;
		req = req->next;
	    }
	    if (SCpnt && req->rq_status == RQ_INACTIVE) {
		if (req == CURRENT)
		    CURRENT = CURRENT->next;
		else
		    req1->next = req->next;
	    }
	}

	if (!SCpnt)
	    return; /* Could not find anything to do */

	wake_up(&wait_for_request);

	/* Queue command */
	requeue_sr_request(SCpnt);
    }  /* While */
}

void requeue_sr_request (Scsi_Cmnd * SCpnt)
{
	unsigned int dev, block, realcount;
	unsigned char cmd[10], *buffer, tries;
	int this_count, start, end_rec;

	tries = 2;

 repeat:
	if(!SCpnt || SCpnt->request.rq_status == RQ_INACTIVE) {
		do_sr_request();
		return;
	}

	dev =  MINOR(SCpnt->request.rq_dev);
	block = SCpnt->request.sector;
	buffer = NULL;
	this_count = 0;

	if (dev >= sr_template.nr_dev) {
		/* printk("CD-ROM request error: invalid device.\n");			*/
		SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
		tries = 2;
		goto repeat;
	}

	if (!scsi_CDs[dev].use) {
		/* printk("CD-ROM request error: device marked not in use.\n");		*/
		SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
		tries = 2;
		goto repeat;
	}

	if( !scsi_CDs[dev].device->online )
          {
            SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
            tries = 2;
            goto repeat;
          }

	if (scsi_CDs[dev].device->changed) {
	/*
	 * quietly refuse to do anything to a changed disc
	 * until the changed bit has been reset
	 */
		/* printk("CD-ROM has been changed.  Prohibiting further I/O.\n");	*/
		SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
		tries = 2;
		goto repeat;
	}

	switch (SCpnt->request.cmd)
    {
    case WRITE:
	SCpnt = end_scsi_request(SCpnt, 0, SCpnt->request.nr_sectors);
	goto repeat;
	break;
    case READ :
	cmd[0] = READ_6;
	break;
    default :
	panic ("Unknown sr command %d\n", SCpnt->request.cmd);
    }

	cmd[1] = (SCpnt->lun << 5) & 0xe0;

    /*
     * Now do the grungy work of figuring out which sectors we need, and
     * where in memory we are going to put them.
     *
     * The variables we need are:
     *
     * this_count= number of 512 byte sectors being read
     * block     = starting cdrom sector to read.
     * realcount = # of cdrom sectors to read
     *
     * The major difference between a scsi disk and a scsi cdrom
     * is that we will always use scatter-gather if we can, because we can
     * work around the fact that the buffer cache has a block size of 1024,
     * and we have 2048 byte sectors.  This code should work for buffers that
     * are any multiple of 512 bytes long.
     */

	SCpnt->use_sg = 0;

	if (SCpnt->host->sg_tablesize > 0 &&
	    (!scsi_need_isa_buffer ||
	 scsi_dma_free_sectors >= 10)) {
	struct buffer_head * bh;
	struct scatterlist * sgpnt;
	int count, this_count_max;
	bh = SCpnt->request.bh;
	this_count = 0;
	count = 0;
	this_count_max = (scsi_CDs[dev].ten ? 0xffff : 0xff) << 4;
	/* Calculate how many links we can use.  First see if we need
	 * a padding record at the start */
	this_count = SCpnt->request.sector % 4;
	if(this_count) count++;
	while(bh && count < SCpnt->host->sg_tablesize) {
	    if ((this_count + (bh->b_size >> 9)) > this_count_max) break;
	    this_count += (bh->b_size >> 9);
	    count++;
	    bh = bh->b_reqnext;
	};
	/* Fix up in case of an odd record at the end */
	end_rec = 0;
	if(this_count % 4) {
	    if (count < SCpnt->host->sg_tablesize) {
		count++;
		end_rec = (4 - (this_count % 4)) << 9;
		this_count += 4 - (this_count % 4);
	    } else {
		count--;
		this_count -= (this_count % 4);
	    };
	};
	SCpnt->use_sg = count;  /* Number of chains */
	/* scsi_malloc can only allocate in chunks of 512 bytes */
	count  = (SCpnt->use_sg * sizeof(struct scatterlist) + 511) & ~511;

	SCpnt->sglist_len = count;
	sgpnt = (struct scatterlist * ) scsi_malloc(count);
	if (!sgpnt) {
	    printk("Warning - running *really* short on DMA buffers\n");
	    SCpnt->use_sg = 0;  /* No memory left - bail out */
	} else {
	    buffer = (unsigned char *) sgpnt;
	    count = 0;
	    bh = SCpnt->request.bh;
	    if(SCpnt->request.sector % 4) {
		sgpnt[count].length = (SCpnt->request.sector % 4) << 9;
		sgpnt[count].address = (char *) scsi_malloc(sgpnt[count].length);
		if(!sgpnt[count].address) panic("SCSI DMA pool exhausted.");
		sgpnt[count].alt_address = sgpnt[count].address; /* Flag to delete
								    if needed */
		count++;
	    };
	    for(bh = SCpnt->request.bh; count < SCpnt->use_sg;
		count++, bh = bh->b_reqnext) {
		if (bh) { /* Need a placeholder at the end of the record? */
		    sgpnt[count].address = bh->b_data;
		    sgpnt[count].length = bh->b_size;
		    sgpnt[count].alt_address = NULL;
		} else {
		    sgpnt[count].address = (char *) scsi_malloc(end_rec);
		    if(!sgpnt[count].address) panic("SCSI DMA pool exhausted.");
		    sgpnt[count].length = end_rec;
		    sgpnt[count].alt_address = sgpnt[count].address;
		    if (count+1 != SCpnt->use_sg) panic("Bad sr request list");
		    break;
		};
		if (virt_to_phys(sgpnt[count].address) + sgpnt[count].length - 1 >
		    ISA_DMA_THRESHOLD && SCpnt->host->unchecked_isa_dma) {
		    sgpnt[count].alt_address = sgpnt[count].address;
		    /* We try to avoid exhausting the DMA pool, since it is easier
		     * to control usage here.  In other places we might have a more
		     * pressing need, and we would be screwed if we ran out */
		    if(scsi_dma_free_sectors < (sgpnt[count].length >> 9) + 5) {
			sgpnt[count].address = NULL;
		    } else {
			sgpnt[count].address = (char *) scsi_malloc(sgpnt[count].length);
		    };
		    /* If we start running low on DMA buffers, we abort the scatter-gather
		     * operation, and free all of the memory we have allocated.  We want to
		     * ensure that all scsi operations are able to do at least a non-scatter/gather
		     * operation */
		    if(sgpnt[count].address == NULL){ /* Out of dma memory */
			printk("Warning: Running low on SCSI DMA buffers\n");
			/* Try switching back to a non scatter-gather operation. */
			while(--count >= 0){
			    if(sgpnt[count].alt_address)
				scsi_free(sgpnt[count].address, sgpnt[count].length);
			};
			SCpnt->use_sg = 0;
			scsi_free(buffer, SCpnt->sglist_len);
			break;
		    }; /* if address == NULL */
		};  /* if need DMA fixup */
	    };  /* for loop to fill list */
#ifdef DEBUG
	    printk("SR: %d %d %d %d %d *** ",SCpnt->use_sg, SCpnt->request.sector,
		   this_count,
		   SCpnt->request.current_nr_sectors,
		   SCpnt->request.nr_sectors);
	    for(count=0; count<SCpnt->use_sg; count++)
		printk("SGlist: %d %x %x %x\n", count,
		       sgpnt[count].address,
		       sgpnt[count].alt_address,
		       sgpnt[count].length);
#endif
	};  /* Able to allocate scatter-gather list */
	};

	if (SCpnt->use_sg == 0){
	/* We cannot use scatter-gather.  Do this the old fashion way */
	if (!SCpnt->request.bh)
	    this_count = SCpnt->request.nr_sectors;
	else
	    this_count = (SCpnt->request.bh->b_size >> 9);

	start = block % 4;
	if (start)
	    {
	    this_count = ((this_count > 4 - start) ?
			  (4 - start) : (this_count));
	    buffer = (unsigned char *) scsi_malloc(2048);
	    }
	else if (this_count < 4)
	    {
	    buffer = (unsigned char *) scsi_malloc(2048);
	    }
	else
	    {
	    this_count -= this_count % 4;
	    buffer = (unsigned char *) SCpnt->request.buffer;
	    if (virt_to_phys(buffer) + (this_count << 9) > ISA_DMA_THRESHOLD &&
		SCpnt->host->unchecked_isa_dma)
		buffer = (unsigned char *) scsi_malloc(this_count << 9);
	    }
	};

	if (scsi_CDs[dev].sector_size == 2048)
	block = block >> 2; /* These are the sectors that the cdrom uses */
	else
	block = block & 0xfffffffc;

	realcount = (this_count + 3) / 4;

	if (scsi_CDs[dev].sector_size == 512) realcount = realcount << 2;

        /*
         * Note: The scsi standard says that READ_6 is *optional*, while
         * READ_10 is mandatory.   Thus there is no point in using
         * READ_6.
         */
	if (scsi_CDs[dev].ten)
          
    {
		if (realcount > 0xffff)
	{
			realcount = 0xffff;
			this_count = realcount * (scsi_CDs[dev].sector_size >> 9);
	}

		cmd[0] += READ_10 - READ_6 ;
		cmd[2] = (unsigned char) (block >> 24) & 0xff;
		cmd[3] = (unsigned char) (block >> 16) & 0xff;
		cmd[4] = (unsigned char) (block >> 8) & 0xff;
		cmd[5] = (unsigned char) block & 0xff;
		cmd[6] = cmd[9] = 0;
		cmd[7] = (unsigned char) (realcount >> 8) & 0xff;
		cmd[8] = (unsigned char) realcount & 0xff;
    }
	else
    {
	if (realcount > 0xff)
	{
	    realcount = 0xff;
	    this_count = realcount * (scsi_CDs[dev].sector_size >> 9);
	}

	cmd[1] |= (unsigned char) ((block >> 16) & 0x1f);
	cmd[2] = (unsigned char) ((block >> 8) & 0xff);
	cmd[3] = (unsigned char) block & 0xff;
	cmd[4] = (unsigned char) realcount;
	cmd[5] = 0;
    }

#ifdef DEBUG
    {
	int i;
	printk("ReadCD: %d %d %d %d\n",block, realcount, buffer, this_count);
	printk("Use sg: %d\n", SCpnt->use_sg);
	printk("Dumping command: ");
	for(i=0; i<12; i++) printk("%2.2x ", cmd[i]);
	printk("\n");
    };
#endif

    /* Some dumb host adapters can speed transfers by knowing the
     * minimum transfersize in advance.
     *
     * We shouldn't disconnect in the middle of a sector, but the cdrom
     * sector size can be larger than the size of a buffer and the
     * transfer may be split to the size of a buffer.  So it's safe to
     * assume that we can at least transfer the minimum of the buffer
     * size (1024) and the sector size between each connect / disconnect.
     */

    SCpnt->transfersize = (scsi_CDs[dev].sector_size > 1024) ?
	1024 : scsi_CDs[dev].sector_size;

	SCpnt->this_count = this_count;
	scsi_do_cmd (SCpnt, (void *) cmd, buffer,
		 realcount * scsi_CDs[dev].sector_size,
		 rw_intr, SR_TIMEOUT, MAX_RETRIES);
}

static int sr_detect(Scsi_Device * SDp){

    if(SDp->type != TYPE_ROM && SDp->type != TYPE_WORM) return 0;

    printk("Detected scsi CD-ROM sr%d at scsi%d, channel %d, id %d, lun %d\n",
	   sr_template.dev_noticed++,
	   SDp->host->host_no, SDp->channel, SDp->id, SDp->lun);

    return 1;
}

static int sr_attach(Scsi_Device * SDp){
    Scsi_CD * cpnt;
    int i;

    if(SDp->type != TYPE_ROM && SDp->type != TYPE_WORM) return 1;

    if (sr_template.nr_dev >= sr_template.dev_max)
    {
	SDp->attached--;
	return 1;
    }

    for(cpnt = scsi_CDs, i=0; i<sr_template.dev_max; i++, cpnt++)
	if(!cpnt->device) break;

    if(i >= sr_template.dev_max) panic ("scsi_devices corrupt (sr)");

    SDp->scsi_request_fn = do_sr_request;
    scsi_CDs[i].device = SDp;

    sr_template.nr_dev++;
    if(sr_template.nr_dev > sr_template.dev_max)
	panic ("scsi_devices corrupt (sr)");
    return 0;
}


static void sr_init_done (Scsi_Cmnd * SCpnt)
{
    struct request * req;

    req = &SCpnt->request;
    req->rq_status = RQ_SCSI_DONE; /* Busy, but indicate request done */

    if (req->sem != NULL) {
	up(req->sem);
    }
}

void get_sectorsize(int i){
    unsigned char cmd[10];
    unsigned char *buffer;
    int the_result, retries;
    Scsi_Cmnd * SCpnt;
    unsigned long flags;

    buffer = (unsigned char *) scsi_malloc(512);
    SCpnt = scsi_allocate_device(NULL, scsi_CDs[i].device, 1);

    retries = 3;
    do {
	cmd[0] = READ_CAPACITY;
	cmd[1] = (scsi_CDs[i].device->lun << 5) & 0xe0;
	memset ((void *) &cmd[2], 0, 8);
	SCpnt->request.rq_status = RQ_SCSI_BUSY;  /* Mark as really busy */
	SCpnt->cmd_len = 0;

	memset(buffer, 0, 8);

	/* Do the command and wait.. */
	{
	    struct semaphore sem = MUTEX_LOCKED;
	    SCpnt->request.sem = &sem;
	    spin_lock_irqsave(&io_request_lock, flags);
	    scsi_do_cmd (SCpnt,
			 (void *) cmd, (void *) buffer,
			 512, sr_init_done,  SR_TIMEOUT,
			 MAX_RETRIES);
	    spin_unlock_irqrestore(&io_request_lock, flags);
	    down(&sem);
	}

	the_result = SCpnt->result;
	retries--;

    } while(the_result && retries);


    wake_up(&SCpnt->device->device_wait);
    scsi_release_command(SCpnt);
    SCpnt = NULL;

    if (the_result) {
	scsi_CDs[i].capacity = 0x1fffff;
	scsi_CDs[i].sector_size = 2048;  /* A guess, just in case */
	scsi_CDs[i].needs_sector_size = 1;
    } else {
	scsi_CDs[i].capacity = 1 + ((buffer[0] << 24) |
				    (buffer[1] << 16) |
				    (buffer[2] << 8) |
				    buffer[3]);
	scsi_CDs[i].sector_size = (buffer[4] << 24) |
	    (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
	switch (scsi_CDs[i].sector_size) {
		/*
		 * HP 4020i CD-Recorder reports 2340 byte sectors
		 * Philips CD-Writers report 2352 byte sectors
		 *
		 * Use 2k sectors for them..
		 */
		case 0: case 2340: case 2352:
			scsi_CDs[i].sector_size = 2048;
			/* fall through */
		case 2048:
			scsi_CDs[i].capacity *= 4;
			/* fall through */
		case 512:
			break;
		default:
			printk ("sr%d: unsupported sector size %d.\n",
				i, scsi_CDs[i].sector_size);
			scsi_CDs[i].capacity = 0;
			scsi_CDs[i].needs_sector_size = 1;
	}

        /*
         * Add this so that we have the ability to correctly gauge
         * what the device is capable of.
         */
	scsi_CDs[i].needs_sector_size = 0;
	sr_sizes[i] = scsi_CDs[i].capacity >> (BLOCK_SIZE_BITS - 9);
    };
    scsi_free(buffer, 512);
}

void get_capabilities(int i){
    unsigned char cmd[6];
    unsigned char *buffer;
    int           rc,n;

    static char *loadmech[] = {
        "caddy",
        "tray",
        "pop-up",
        "",
        "changer",
        "changer",
        "",
        ""
    };          

    buffer = (unsigned char *) scsi_malloc(512);
    cmd[0] = MODE_SENSE;
    cmd[1] = (scsi_CDs[i].device->lun << 5) & 0xe0;
    cmd[2] = 0x2a;
    cmd[4] = 128;
    cmd[3] = cmd[5] = 0;
    rc = sr_do_ioctl(i, cmd, buffer, 128, 1);
    
    if (-EINVAL == rc) {
        /* failed, drive has'nt this mode page */
        scsi_CDs[i].cdi.speed      = 1;
        /* disable speed select, drive probably can't do this either */
        scsi_CDs[i].cdi.mask      |= CDC_SELECT_SPEED;
    } else {
        n = buffer[3]+4;
        scsi_CDs[i].cdi.speed    = ((buffer[n+8] << 8) + buffer[n+9])/176;
      	scsi_CDs[i].readcd_known = 1;
        scsi_CDs[i].readcd_cdda  = buffer[n+5] & 0x01;
        /* print some capability bits */
        printk("sr%i: scsi3-mmc drive: %dx/%dx %s%s%s%s%s\n",i,
               ((buffer[n+14] << 8) + buffer[n+15])/176,
               scsi_CDs[i].cdi.speed,
               buffer[n+3]&0x01 ? "writer " : "",   /* CD Writer */
               buffer[n+2]&0x02 ? "cd/rw " : "",    /* can read rewriteable */
               buffer[n+4]&0x20 ? "xa/form2 " : "", /* can read xa/from2 */
               buffer[n+5]&0x01 ? "cdda " : "",     /* can read audio data */
               loadmech[buffer[n+6]>>5]);
	if ((buffer[n+6] >> 5) == 0)
		/* caddy drives can't close tray... */
        	scsi_CDs[i].cdi.mask |= CDC_CLOSE_TRAY;
    }
    scsi_free(buffer, 512);
}

static int sr_registered = 0;

static int sr_init()
{
    int i;

    if(sr_template.dev_noticed == 0) return 0;

    if(!sr_registered) {
	if (register_blkdev(MAJOR_NR,"sr",&cdrom_fops)) {
	    printk("Unable to get major %d for SCSI-CD\n",MAJOR_NR);
	    return 1;
	}
	sr_registered++;
    }


    if (scsi_CDs) return 0;
    sr_template.dev_max =
            sr_template.dev_noticed + SR_EXTRA_DEVS;
    scsi_CDs = (Scsi_CD *) scsi_init_malloc(sr_template.dev_max * sizeof(Scsi_CD), GFP_ATOMIC);
    memset(scsi_CDs, 0, sr_template.dev_max * sizeof(Scsi_CD));

    sr_sizes = (int *) scsi_init_malloc(sr_template.dev_max * sizeof(int), GFP_ATOMIC);
    memset(sr_sizes, 0, sr_template.dev_max * sizeof(int));

    sr_blocksizes = (int *) scsi_init_malloc(sr_template.dev_max *
					 sizeof(int), GFP_ATOMIC);

    /*
     * These are good guesses for the time being.
     */
    for(i=0;i<sr_template.dev_max;i++) sr_blocksizes[i] = 2048;
    blksize_size[MAJOR_NR] = sr_blocksizes;
    return 0;
}

void sr_finish()
{
    int i;
    char name[6];

    blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
    blk_size[MAJOR_NR] = sr_sizes;

    for (i = 0; i < sr_template.nr_dev; ++i)
    {
	/* If we have already seen this, then skip it.  Comes up
	 * with loadable modules. */
	if (scsi_CDs[i].capacity) continue;
	scsi_CDs[i].capacity = 0x1fffff;
	scsi_CDs[i].sector_size = 2048;  /* A guess, just in case */
	scsi_CDs[i].needs_sector_size = 1;
	scsi_CDs[i].device->changed = 1; /* force recheck CD type */
#if 0
	/* seems better to leave this for later */
	get_sectorsize(i);
	printk("Scd sectorsize = %d bytes.\n", scsi_CDs[i].sector_size);
#endif
	scsi_CDs[i].use = 1;
	scsi_CDs[i].ten = 1;
	scsi_CDs[i].remap = 1;
      	scsi_CDs[i].readcd_known = 0;
      	scsi_CDs[i].readcd_cdda  = 0;
	sr_sizes[i] = scsi_CDs[i].capacity >> (BLOCK_SIZE_BITS - 9);

	scsi_CDs[i].cdi.ops        = &sr_dops;
	scsi_CDs[i].cdi.handle     = &scsi_CDs[i];
	scsi_CDs[i].cdi.dev        = MKDEV(MAJOR_NR,i);
	scsi_CDs[i].cdi.mask       = 0;
        scsi_CDs[i].cdi.capacity   = 1;
	get_capabilities(i);
	sr_vendor_init(i);

	sprintf(name, "sr%d", i);
	strcpy(scsi_CDs[i].cdi.name, name);
	register_cdrom(&scsi_CDs[i].cdi);
    }


    /* If our host adapter is capable of scatter-gather, then we increase
     * the read-ahead to 16 blocks (32 sectors).  If not, we use
     * a two block (4 sector) read ahead. */
    if(scsi_CDs[0].device && scsi_CDs[0].device->host->sg_tablesize)
	read_ahead[MAJOR_NR] = 32;  /* 32 sector read-ahead.  Always removable. */
    else
	read_ahead[MAJOR_NR] = 4;  /* 4 sector read-ahead */

    return;
}

static void sr_detach(Scsi_Device * SDp)
{
    Scsi_CD * cpnt;
    int i;

    for(cpnt = scsi_CDs, i=0; i<sr_template.dev_max; i++, cpnt++)
	if(cpnt->device == SDp) {
	    kdev_t devi = MKDEV(MAJOR_NR, i);
	    struct super_block * sb = get_super(devi);

	    /*
	     * Since the cdrom is read-only, no need to sync the device.
	     * We should be kind to our buffer cache, however.
	     */
	    if (sb) invalidate_inodes(sb);
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

int init_module(void) {
        sr_template.module = &__this_module;
        return scsi_register_module(MODULE_SCSI_DEV, &sr_template);
}

void cleanup_module( void)
{
    scsi_unregister_module(MODULE_SCSI_DEV, &sr_template);
    unregister_blkdev(MAJOR_NR, "sr");
    sr_registered--;
    if(scsi_CDs != NULL) {
	scsi_init_free((char *) scsi_CDs,
		       (sr_template.dev_noticed + SR_EXTRA_DEVS)
		       * sizeof(Scsi_CD));

	scsi_init_free((char *) sr_sizes, sr_template.dev_max * sizeof(int));
        sr_sizes = NULL;

	scsi_init_free((char *) sr_blocksizes, sr_template.dev_max * sizeof(int));
        sr_blocksizes = NULL;
    }

    blksize_size[MAJOR_NR] = NULL;
    blk_dev[MAJOR_NR].request_fn = NULL;
    blk_size[MAJOR_NR] = NULL;
    read_ahead[MAJOR_NR] = 0;

    sr_template.dev_max = 0;
}
#endif /* MODULE */

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
