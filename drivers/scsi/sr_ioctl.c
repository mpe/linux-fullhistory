#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>

#include <linux/cdrom.h>
#include <linux/ucdrom.h>
#include "sr.h"

extern void get_sectorsize(int);

#define IOCTL_RETRIES 3
/* The CDROM is fairly slow, so we need a little extra time */
/* In fact, it is very slow if it has to spin up first */
#define IOCTL_TIMEOUT 3000

static void sr_ioctl_done(Scsi_Cmnd * SCpnt)
{
    struct request * req;
    
    req = &SCpnt->request;
    req->rq_status = RQ_SCSI_DONE; /* Busy, but indicate request done */
    
    if (req->sem != NULL) {
	up(req->sem);
    }
}

/* We do our own retries because we want to know what the specific
   error code is.  Normally the UNIT_ATTENTION code will automatically
   clear after one error */

int sr_do_ioctl(int target, unsigned char * sr_cmd, void * buffer, unsigned buflength)
{
    Scsi_Cmnd * SCpnt;
    int result;

    SCpnt = allocate_device(NULL, scsi_CDs[target].device, 1);
    {
	struct semaphore sem = MUTEX_LOCKED;
	SCpnt->request.sem = &sem;
	scsi_do_cmd(SCpnt,
		    (void *) sr_cmd, buffer, buflength, sr_ioctl_done, 
		    IOCTL_TIMEOUT, IOCTL_RETRIES);
	down(&sem);
    }
    
    result = SCpnt->result;
    
    /* Minimal error checking.  Ignore cases we know about, and report the rest. */
    if(driver_byte(result) != 0)
	switch(SCpnt->sense_buffer[2] & 0xf) {
	case UNIT_ATTENTION:
	    scsi_CDs[target].device->changed = 1;
	    printk("Disc change detected.\n");
	    break;
	case NOT_READY: /* This happens if there is no disc in drive */
	    printk(KERN_INFO "CDROM not ready.  Make sure there is a disc in the drive.\n");
	    break;
	case ILLEGAL_REQUEST:
	    printk("CDROM (ioctl) reports ILLEGAL REQUEST.\n");
	    break;
	default:
	    print_sense("sr", SCpnt);
	};
    
    result = SCpnt->result;
    SCpnt->request.rq_status = RQ_INACTIVE; /* Deallocate */
    wake_up(&SCpnt->device->device_wait);
    /* Wake up a process waiting for device*/
    return result;
}

/* ---------------------------------------------------------------------- */
/* interface to cdrom.c                                                   */

int sr_tray_move(struct cdrom_device_info *cdi, int pos)
{
        u_char  sr_cmd[10];

        sr_cmd[0] = START_STOP;
        sr_cmd[1] = ((scsi_CDs[MINOR(cdi->dev)].device -> lun) << 5);
        sr_cmd[2] = sr_cmd[3] = sr_cmd[5] = 0;
        sr_cmd[4] = (pos == 0) ? 0x03 /* close */ : 0x02 /* eject */;
	
        return sr_do_ioctl(MINOR(cdi->dev), sr_cmd, NULL, 255);
}

int sr_lock_door(struct cdrom_device_info *cdi, int lock)
{
        return scsi_ioctl (scsi_CDs[MINOR(cdi->dev)].device,
                           lock ? SCSI_IOCTL_DOORLOCK : SCSI_IOCTL_DOORUNLOCK,
                           0);
}

int sr_drive_status(struct cdrom_device_info *cdi, int slot)
{
        if (CDSL_CURRENT != slot) {
                /* we have no changer support */
                return -EINVAL;
        }

        if (!scsi_ioctl(scsi_CDs[MINOR(cdi->dev)].device,
                        SCSI_IOCTL_TEST_UNIT_READY,0))
                return CDS_DISC_OK;

#if 1
	/* Tell tray is open if the drive is not ready.  Seems there is
	 * no way to check whenever the tray is really open, but this way
	 * we get auto-close-on-open work. And it seems to have no ill
         * effects with caddy drives... */
        return CDS_TRAY_OPEN;
#else
        return CDS_NO_DISC;
#endif
}

int sr_disk_status(struct cdrom_device_info *cdi)
{
	struct cdrom_tochdr    toc_h;
	struct cdrom_tocentry  toc_e;
        int                    i;

        if (scsi_ioctl(scsi_CDs[MINOR(cdi->dev)].device,SCSI_IOCTL_TEST_UNIT_READY,0))
                return CDS_NO_DISC;

        /* if the xa-bit is on, we tell it is XA... */
        if (scsi_CDs[MINOR(cdi->dev)].xa_flag)
                return CDS_XA_2_1;
        
        /* ...else we look for data tracks */
        if (sr_audio_ioctl(cdi, CDROMREADTOCHDR, &toc_h))
                return CDS_NO_INFO;
        for (i = toc_h.cdth_trk0; i <= toc_h.cdth_trk1; i++) {
                toc_e.cdte_track  = i;
                toc_e.cdte_format = CDROM_LBA;
                if (sr_audio_ioctl(cdi, CDROMREADTOCENTRY, &toc_e))
                        return CDS_NO_INFO;
                if (toc_e.cdte_ctrl & CDROM_DATA_TRACK)
                        return CDS_DATA_1;
#if 0
                if (i == toc_h.cdth_trk0 && toc_e.cdte_addr.lba > 100)
                        /* guess: looks like a "hidden track" CD */
                        return CDS_DATA_1;
#endif
        }
        return CDS_AUDIO;
}

int sr_get_last_session(struct cdrom_device_info *cdi,
                        struct cdrom_multisession* ms_info)
{
        ms_info->addr.lba=scsi_CDs[MINOR(cdi->dev)].ms_offset;
        ms_info->xa_flag=scsi_CDs[MINOR(cdi->dev)].xa_flag ||
            scsi_CDs[MINOR(cdi->dev)].ms_offset > 0;

	return 0;
}

int sr_get_mcn(struct cdrom_device_info *cdi,struct cdrom_mcn *mcn)
{
        u_char  sr_cmd[10];
	char * buffer;
        int result;
        	
	sr_cmd[0] = SCMD_READ_SUBCHANNEL;
	sr_cmd[1] = ((scsi_CDs[MINOR(cdi->dev)].device->lun) << 5);
	sr_cmd[2] = 0x40;    /* I do want the subchannel info */
	sr_cmd[3] = 0x02;    /* Give me medium catalog number info */
	sr_cmd[4] = sr_cmd[5] = 0;
	sr_cmd[6] = 0;
	sr_cmd[7] = 0;
	sr_cmd[8] = 24;
	sr_cmd[9] = 0;
	
	buffer = (unsigned char*) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	result = sr_do_ioctl(MINOR(cdi->dev), sr_cmd, buffer, 24);
	
	memcpy (mcn->medium_catalog_number, buffer + 9, 13);
        mcn->medium_catalog_number[13] = 0;

	scsi_free(buffer, 512);
	
	return result;
}

int sr_reset(struct cdrom_device_info *cdi)
{
	invalidate_buffers(cdi->dev);
	return 0;        
}

/* ----------------------------------------------------------------------- */
/* this is called by the generic cdrom driver. arg is a _kernel_ pointer,  */
/* becauce the generic cdrom driver does the user access stuff for us.     */

int sr_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd, void* arg)
{
    u_char  sr_cmd[10];    
    int result, target;
    
    target = MINOR(cdi->dev);
    
    switch (cmd) 
    {
	/* Sun-compatible */
    case CDROMPAUSE:
	
	sr_cmd[0] = SCMD_PAUSE_RESUME;
	sr_cmd[1] = scsi_CDs[target].device->lun << 5;
	sr_cmd[2] = sr_cmd[3] = sr_cmd[4] = 0;
	sr_cmd[5] = sr_cmd[6] = sr_cmd[7] = 0;
	sr_cmd[8] = 0;
	sr_cmd[9] = 0;
	
	result = sr_do_ioctl(target, sr_cmd, NULL, 255);
        break;
	
    case CDROMRESUME:
	
	sr_cmd[0] = SCMD_PAUSE_RESUME;
	sr_cmd[1] = scsi_CDs[target].device->lun << 5;
	sr_cmd[2] = sr_cmd[3] = sr_cmd[4] = 0;
	sr_cmd[5] = sr_cmd[6] = sr_cmd[7] = 0;
	sr_cmd[8] = 1;
	sr_cmd[9] = 0;
	
	result = sr_do_ioctl(target, sr_cmd, NULL, 255);	
        break;
	
    case CDROMPLAYMSF:
    {
	struct cdrom_msf* msf = (struct cdrom_msf*)arg;

	sr_cmd[0] = SCMD_PLAYAUDIO_MSF;
	sr_cmd[1] = scsi_CDs[target].device->lun << 5;
	sr_cmd[2] = 0;
	sr_cmd[3] = msf->cdmsf_min0;
	sr_cmd[4] = msf->cdmsf_sec0;
	sr_cmd[5] = msf->cdmsf_frame0;
	sr_cmd[6] = msf->cdmsf_min1;
	sr_cmd[7] = msf->cdmsf_sec1;
	sr_cmd[8] = msf->cdmsf_frame1;
	sr_cmd[9] = 0;
	
	result = sr_do_ioctl(target, sr_cmd, NULL, 255);
        break;
    }

    case CDROMPLAYBLK:
    {
	struct cdrom_blk* blk = (struct cdrom_blk*)arg;

	sr_cmd[0] = SCMD_PLAYAUDIO10;
	sr_cmd[1] = scsi_CDs[target].device->lun << 5;
	sr_cmd[2] = blk->from >> 24;
	sr_cmd[3] = blk->from >> 16;
	sr_cmd[4] = blk->from >> 8;
	sr_cmd[5] = blk->from;
	sr_cmd[6] = 0;
	sr_cmd[7] = blk->len >> 8;
	sr_cmd[8] = blk->len;
	sr_cmd[9] = 0;
	
	result = sr_do_ioctl(target, sr_cmd, NULL, 255);
        break;
    }
		
    case CDROMPLAYTRKIND:
    {
	struct cdrom_ti* ti = (struct cdrom_ti*)arg;

	sr_cmd[0] = SCMD_PLAYAUDIO_TI;
	sr_cmd[1] = scsi_CDs[target].device->lun << 5;
	sr_cmd[2] = 0;
	sr_cmd[3] = 0;
	sr_cmd[4] = ti->cdti_trk0;
	sr_cmd[5] = ti->cdti_ind0;
	sr_cmd[6] = 0;
	sr_cmd[7] = ti->cdti_trk1;
	sr_cmd[8] = ti->cdti_ind1;
	sr_cmd[9] = 0;
	
	result = sr_do_ioctl(target, sr_cmd, NULL, 255);
        break;
    }
	
    case CDROMREADTOCHDR:
    {
	struct cdrom_tochdr* tochdr = (struct cdrom_tochdr*)arg;
	char * buffer;
	
	sr_cmd[0] = SCMD_READ_TOC;
	sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5);
	sr_cmd[2] = sr_cmd[3] = sr_cmd[4] = sr_cmd[5] = 0;
	sr_cmd[6] = 0;
	sr_cmd[7] = 0;              /* MSB of length (12) */
	sr_cmd[8] = 12;             /* LSB of length */
	sr_cmd[9] = 0;
	
	buffer = (unsigned char *) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	result = sr_do_ioctl(target, sr_cmd, buffer, 12);
	
	tochdr->cdth_trk0 = buffer[2];
	tochdr->cdth_trk1 = buffer[3];
	
	scsi_free(buffer, 512);
        break;
    }
	
    case CDROMREADTOCENTRY:
    {
	struct cdrom_tocentry* tocentry = (struct cdrom_tocentry*)arg;
	unsigned char * buffer;
	
	sr_cmd[0] = SCMD_READ_TOC;
	sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5) |
          (tocentry->cdte_format == CDROM_MSF ? 0x02 : 0);
	sr_cmd[2] = sr_cmd[3] = sr_cmd[4] = sr_cmd[5] = 0;
	sr_cmd[6] = tocentry->cdte_track;
	sr_cmd[7] = 0;             /* MSB of length (12)  */
	sr_cmd[8] = 12;            /* LSB of length */
	sr_cmd[9] = 0;
	
	buffer = (unsigned char *) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	result = sr_do_ioctl (target, sr_cmd, buffer, 12);
	
        tocentry->cdte_ctrl = buffer[5] & 0xf;	
        tocentry->cdte_adr = buffer[5] >> 4;
        tocentry->cdte_datamode = (tocentry->cdte_ctrl & 0x04) ? 1 : 0;
	if (tocentry->cdte_format == CDROM_MSF) {
	    tocentry->cdte_addr.msf.minute = buffer[9];
	    tocentry->cdte_addr.msf.second = buffer[10];
	    tocentry->cdte_addr.msf.frame = buffer[11];
	} else
	    tocentry->cdte_addr.lba = (((((buffer[8] << 8) + buffer[9]) << 8)
                                       + buffer[10]) << 8) + buffer[11];
	
	scsi_free(buffer, 512);
        break;
    }
	
    case CDROMSTOP:
	sr_cmd[0] = START_STOP;
	sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5) | 1;
	sr_cmd[2] = sr_cmd[3] = sr_cmd[5] = 0;
	sr_cmd[4] = 0;
	
	result = sr_do_ioctl(target, sr_cmd, NULL, 255);
        break;
	
    case CDROMSTART:
	sr_cmd[0] = START_STOP;
	sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5) | 1;
	sr_cmd[2] = sr_cmd[3] = sr_cmd[5] = 0;
	sr_cmd[4] = 1;
	
	result = sr_do_ioctl(target, sr_cmd, NULL, 255);
        break;
	
    case CDROMVOLCTRL:
    {
	char * buffer, * mask;
	struct cdrom_volctrl* volctrl = (struct cdrom_volctrl*)arg;
	
	/* First we get the current params so we can just twiddle the volume */
	
	sr_cmd[0] = MODE_SENSE;
	sr_cmd[1] = (scsi_CDs[target].device -> lun) << 5;
	sr_cmd[2] = 0xe;    /* Want mode page 0xe, CDROM audio params */
	sr_cmd[3] = 0;
	sr_cmd[4] = 28;
	sr_cmd[5] = 0;
	
	buffer = (unsigned char *) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	if ((result = sr_do_ioctl (target, sr_cmd, buffer, 28))) {
	    printk ("Hosed while obtaining audio mode page\n");
	    scsi_free(buffer, 512);
            break;
	}
	
	sr_cmd[0] = MODE_SENSE;
	sr_cmd[1] = (scsi_CDs[target].device -> lun) << 5;
	sr_cmd[2] = 0x4e;   /* Want the mask for mode page 0xe */
	sr_cmd[3] = 0;
	sr_cmd[4] = 28;
	sr_cmd[5] = 0;
	
	mask = (unsigned char *) scsi_malloc(512);
	if(!mask) {
	    scsi_free(buffer, 512);
	    result = -ENOMEM;
            break;
	};

	if ((result = sr_do_ioctl (target, sr_cmd, mask, 28))) {
	    printk ("Hosed while obtaining mask for audio mode page\n");
	    scsi_free(buffer, 512);
	    scsi_free(mask, 512);
	    break;
	}
	
	/* Now mask and substitute our own volume and reuse the rest */
	buffer[0] = 0;  /* Clear reserved field */
	
	buffer[21] = volctrl->channel0 & mask[21];
	buffer[23] = volctrl->channel1 & mask[23];
	buffer[25] = volctrl->channel2 & mask[25];
	buffer[27] = volctrl->channel3 & mask[27];
	
	sr_cmd[0] = MODE_SELECT;
	sr_cmd[1] = ((scsi_CDs[target].device -> lun) << 5) | 0x10;    /* Params are SCSI-2 */
	sr_cmd[2] = sr_cmd[3] = 0;
	sr_cmd[4] = 28;
	sr_cmd[5] = 0;
	
	result = sr_do_ioctl (target, sr_cmd, buffer, 28);
	scsi_free(buffer, 512);
	scsi_free(mask, 512);
        break;
    }
	
    case CDROMVOLREAD:
    {
	char * buffer;
	struct cdrom_volctrl* volctrl = (struct cdrom_volctrl*)arg;
	
	/* Get the current params */
	
	sr_cmd[0] = MODE_SENSE;
	sr_cmd[1] = (scsi_CDs[target].device -> lun) << 5;
	sr_cmd[2] = 0xe;    /* Want mode page 0xe, CDROM audio params */
	sr_cmd[3] = 0;
	sr_cmd[4] = 28;
	sr_cmd[5] = 0;
	
	buffer = (unsigned char *) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	if ((result = sr_do_ioctl (target, sr_cmd, buffer, 28))) {
	    printk ("(CDROMVOLREAD) Hosed while obtaining audio mode page\n");
	    scsi_free(buffer, 512);
            break;
	}

	volctrl->channel0 = buffer[21];
	volctrl->channel1 = buffer[23];
	volctrl->channel2 = buffer[25];
	volctrl->channel3 = buffer[27];

	scsi_free(buffer, 512);
        break;
    }
	
    case CDROMSUBCHNL:
    {
	struct cdrom_subchnl* subchnl = (struct cdrom_subchnl*)arg;
	char * buffer;
	
	sr_cmd[0] = SCMD_READ_SUBCHANNEL;
	sr_cmd[1] = ((scsi_CDs[target].device->lun) << 5) | 0x02;    /* MSF format */
	sr_cmd[2] = 0x40;    /* I do want the subchannel info */
	sr_cmd[3] = 0x01;    /* Give me current position info */
	sr_cmd[4] = sr_cmd[5] = 0;
	sr_cmd[6] = 0;
	sr_cmd[7] = 0;
	sr_cmd[8] = 16;
	sr_cmd[9] = 0;
	
	buffer = (unsigned char*) scsi_malloc(512);
	if(!buffer) return -ENOMEM;
	
	result = sr_do_ioctl(target, sr_cmd, buffer, 16);
	
	subchnl->cdsc_audiostatus = buffer[1];
	subchnl->cdsc_format = CDROM_MSF;
	subchnl->cdsc_ctrl = buffer[5] & 0xf;
	subchnl->cdsc_trk = buffer[6];
	subchnl->cdsc_ind = buffer[7];
	
	subchnl->cdsc_reladdr.msf.minute = buffer[13];
	subchnl->cdsc_reladdr.msf.second = buffer[14];
	subchnl->cdsc_reladdr.msf.frame = buffer[15];
	subchnl->cdsc_absaddr.msf.minute = buffer[9];
	subchnl->cdsc_absaddr.msf.second = buffer[10];
	subchnl->cdsc_absaddr.msf.frame = buffer[11];
	
	scsi_free(buffer, 512);
        break;
    }
    default:
        return -EINVAL;
    }

#if 0
    if (result)
        printk("DEBUG: sr_audio: result for ioctl %x: %x\n",cmd,result);
#endif
    
    return result;
}
	
int sr_dev_ioctl(struct cdrom_device_info *cdi,
                 unsigned int cmd, unsigned long arg)
{
    int target, err;
    
    target = MINOR(cdi->dev);
    
    switch (cmd) {
    /* these are compatible with the ide-cd driver */
    case CDROMREADRAW:
    case CDROMREADMODE1:
    case CDROMREADMODE2:

#if CONFIG_BLK_DEV_SR_VENDOR 
    {
	unsigned char      *raw;
        struct cdrom_msf   msf;
        int                blocksize, lba, rc;

        if (cmd == CDROMREADMODE1)
                blocksize = CD_FRAMESIZE;       /* 2048 */
        else if (cmd == CDROMREADMODE2)
                blocksize = CD_FRAMESIZE_RAW0;  /* 2336 */
        else
		/* some SCSI drives do not allow this one */
                blocksize = CD_FRAMESIZE_RAW;   /* 2352 */

	if (copy_from_user(&msf,(void*)arg,sizeof(msf)))
		return -EFAULT;
	if (!(raw = scsi_malloc(2048+512)))
        	return -ENOMEM;

	lba = (((msf.cdmsf_min0 * CD_SECS) + msf.cdmsf_sec0)
			* CD_FRAMES + msf.cdmsf_frame0) - CD_BLOCK_OFFSET;
	rc = sr_read_sector(target, lba, blocksize, raw);
	if (!rc)
		if (copy_to_user((void*)arg, raw, blocksize))
			rc = -EFAULT;

	scsi_free(raw,2048+512);
	return rc;
    }
#else
	return -EINVAL;
#endif

	
    case BLKRAGET:
	if (!arg)
		return -EINVAL;
	err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
	if (err)
		return err;
	put_user(read_ahead[MAJOR(cdi->dev)], (long *) arg);
	return 0;

    case BLKRASET:
	if(!suser())
        	return -EACCES;
	if(!(cdi->dev))
        	return -EINVAL;
	if(arg > 0xff)
        	return -EINVAL;
	read_ahead[MAJOR(cdi->dev)] = arg;
	return 0;

    RO_IOCTLS(cdi->dev,arg);

    default:
	return scsi_ioctl(scsi_CDs[target].device,cmd,(void *) arg);
    }
}

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
