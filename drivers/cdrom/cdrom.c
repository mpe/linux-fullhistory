/* cdrom.c. Common ioctl and open routines for various Linux cdrom drivers. -*- linux-c -*-
   Copyright (c) 1996 David van Leeuwen. 

   The routines in the file should provide an interface between
   software accessing cdroms and the various drivers that implement
   specific hardware devices. 

 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/fcntl.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include <linux/cdrom.h>
#include <linux/ucdrom.h>

#define FM_WRITE	0x2                 /* file mode write bit */

#define VERSION "Generic CD-ROM driver, v 1.21 1996/11/08 03:24:49"

/* Not-exported routines. */
static int cdrom_open(struct inode *ip, struct file *fp);
static int cdrom_release(struct inode *ip, struct file *fp);
static int cdrom_ioctl(struct inode *ip, struct file *fp,
				unsigned int cmd, unsigned long arg);
static int cdrom_media_changed(kdev_t dev);

struct file_operations cdrom_fops =
{
        NULL,                           /* lseek */
        block_read,                     /* read - general block-dev read */
        block_write,                    /* write - general block-dev write */
        NULL,                           /* readdir */
        NULL,                           /* poll */
        cdrom_ioctl,                    /* ioctl */
        NULL,                           /* mmap */
        cdrom_open,                     /* open */
        cdrom_release,                  /* release */
        NULL,                           /* fsync */
        NULL,                           /* fasync */
        cdrom_media_changed,            /* media_change */
        NULL                            /* revalidate */
};

static struct cdrom_device_info *cdromdevs[MAX_BLKDEV] = {
	NULL,
};

/* This macro makes sure we don't have to check on cdrom_device_ops
 * existence in the run-time routines below. Change_capability is a
 * hack to have the capability flags defined const, while we can still
 * change it here without gcc complaining at every line.
 */

#define ENSURE(call, bits) if (cdo->call == NULL) *change_capability &= ~(bits)

/* We don't use $name$ yet, but it could be used for the /proc
 * filesystem in the future, or for other purposes.  
 */
int register_cdrom(struct cdrom_device_info *cdi, char *name)
{
        int major = MAJOR (cdi->dev);
        struct cdrom_device_ops *cdo = cdi->ops;
        int *change_capability = (int *)&cdo->capability; /* hack */

        if (major < 0 || major >= MAX_BLKDEV)
                return -1;
        if (cdo->open == NULL || cdo->release == NULL)
                return -2;
        ENSURE(tray_move, CDC_CLOSE_TRAY | CDC_OPEN_TRAY);
        ENSURE(lock_door, CDC_LOCK);
        ENSURE(select_speed, CDC_SELECT_SPEED);
        ENSURE(select_disc, CDC_SELECT_DISC);
        ENSURE(get_last_session, CDC_MULTI_SESSION);
        ENSURE(audio_ioctl, CDC_PLAY_AUDIO);
        ENSURE(media_changed, CDC_MEDIA_CHANGED);
        if (cdromdevs[major]==NULL) cdo->n_minors = 0;
        else cdo->n_minors++; 
        cdi->next = cdromdevs[major];
        cdromdevs[major] = cdi;
        cdi->options = CDO_AUTO_CLOSE | CDO_USE_FFLAGS | CDO_LOCK;
        cdi->mc_flags = 0;
        return 0;
}
#undef ENSURE

int unregister_cdrom(struct cdrom_device_info *unreg)
{
        struct cdrom_device_info *cdi, *prev;
        int major = MAJOR (unreg->dev);

        if (major < 0 || major >= MAX_BLKDEV)
                return -1;

        prev = NULL;
        cdi = cdromdevs[major];
        while (cdi != NULL && cdi->dev != unreg->dev) {
                prev = cdi;
                cdi = cdi->next;
        }

        if (cdi == NULL)
                return -2;
        if (prev)
                prev->next = cdi->next;
        else
                cdromdevs[major] = cdi->next;
        cdi->ops->n_minors--;
        return 0;
}

static
struct cdrom_device_info *cdrom_find_device (kdev_t dev)
{
        struct cdrom_device_info *cdi = cdromdevs[MAJOR (dev)];

        while (cdi != NULL && cdi->dev != dev)
                cdi = cdi->next;
        return cdi;
}

/* We use the open-option O_NONBLOCK to indicate that the
 * purpose of opening is only for subsequent ioctl() calls; no device
 * integrity checks are performed.
 *
 * We hope that all cd-player programs will adopt this convention. It
 * is in their own interest: device control becomes a lot easier
 * this way.
 */
static
int open_for_data(struct cdrom_device_info * cdi);

static
int cdrom_open(struct inode *ip, struct file *fp)
{
	kdev_t dev = ip->i_rdev;
        struct cdrom_device_info *cdi = cdrom_find_device(dev);
        int purpose = !!(fp->f_flags & O_NONBLOCK);
        int ret=0;

        if (cdi == NULL)
                return -ENODEV;
        if (fp->f_mode & FM_WRITE)
                return -EROFS;				
        purpose = purpose || !(cdi->options & CDO_USE_FFLAGS);
        if (cdi->use_count || purpose) 
                ret = cdi->ops->open(cdi, purpose); 
        else 
                ret = open_for_data(cdi);
        if (!ret) cdi->use_count++;
        return ret;
}

static
int open_for_data(struct cdrom_device_info * cdi)
{
        int ret;
        struct cdrom_device_ops *cdo = cdi->ops;
        if (cdo->drive_status != NULL) {
                int ds = cdo->drive_status(cdi, CDSL_CURRENT);
                if (ds == CDS_TRAY_OPEN) {
                        /* can/may i close it? */
                        if (cdo->capability & ~cdi->mask & CDC_CLOSE_TRAY &&
                            cdi->options & CDO_AUTO_CLOSE) {
                                if (cdo->tray_move(cdi,0))
                                        return -EIO;
                        } else
                                return -ENXIO; /* can't close: too bad */
                        ds = cdo->drive_status(cdi, CDSL_CURRENT);
                        if (ds == CDS_NO_DISC)
                                return -ENXIO;
                }
        }
        if (cdo->disc_status != NULL) {
                int ds = cdo->disc_status(cdi);
                if (ds == CDS_NO_DISC)
                        return -ENXIO;
                if (cdi->options & CDO_CHECK_TYPE &&
                    ds != CDS_DATA_1)
                        return -ENODATA;
        }
        /* all is well, we can open the device */
        ret = cdo->open(cdi, 0); /* open for data */
        if (cdo->capability & ~cdi->mask & CDC_LOCK &&
            cdi->options & CDO_LOCK)
                cdo->lock_door(cdi, 1);
        return ret;
}

/* Admittedly, the logic below could be performed in a nicer way. */
static
int cdrom_release(struct inode *ip, struct file *fp)
{
        kdev_t dev = ip->i_rdev;
        struct cdrom_device_info *cdi = cdrom_find_device (dev);
        struct cdrom_device_ops *cdo;
	
        if (cdi == NULL)
                return 0;
        cdo = cdi->ops;        
        if (cdi->use_count == 1 &&      /* last process that closes dev*/
            cdi->options & CDO_LOCK &&  
            cdo->capability & ~cdi->mask & CDC_LOCK) 
                cdo->lock_door(cdi, 0);
        cdo->release(cdi);
        if (cdi->use_count > 0) cdi->use_count--;
        if (cdi->use_count == 0) {      /* last process that closes dev*/
                sync_dev(dev);
                invalidate_buffers(dev);
                if (cdi->options & CDO_AUTO_EJECT &&
                    cdo->capability & ~cdi->mask & CDC_OPEN_TRAY)
                        cdo->tray_move(cdi, 1);
        }
	return 0;
}

/* We want to make media_changed accessible to the user through an
 * ioctl. The main problem now is that we must double-buffer the
 * low-level implementation, to assure that the VFS and the user both
 * see a medium change once.
 */

static
int media_changed(struct cdrom_device_info *cdi, int queue)
{
        unsigned int mask = (1 << (queue & 1));
        int ret = !!(cdi->mc_flags & mask);

        /* changed since last call? */
        if (cdi->ops->media_changed(cdi, CDSL_CURRENT)) {
                cdi->mc_flags = 0x3;    /* set bit on both queues */
                ret |= 1;
        }
        cdi->mc_flags &= ~mask;         /* clear bit */
        return ret;
}

static
int cdrom_media_changed(kdev_t dev)
{
        struct cdrom_device_info *cdi = cdrom_find_device (dev);
        if (cdi == NULL)
                return -ENODEV;
        if (cdi->ops->media_changed == NULL)
                return -EINVAL;
        return media_changed(cdi, 0);
}

/* Requests to the low-level drivers will /always/ be done in the
   following format convention: 

   CDROM_LBA: all data-related requests.
   CDROM_MSF: all audio-related requests. 

   However, a low-level implementation is allowed to refuse this
   request, and return information in its own favorite format.  

   It doesn't make sense /at all/ to ask for a play_audio in LBA
   format, or ask for multi-session info in MSF format. However, for
   backward compatibility these format requests will be satisfied, but
   the requests to the low-level drivers will be sanitized in the more
   meaningful format indicated above.
 */

static
void sanitize_format(union cdrom_addr *addr,
                     u_char * curr, u_char requested)
{
        if (*curr == requested)
                return;                 /* nothing to be done! */
        if (requested == CDROM_LBA) {
                addr->lba = (int) addr->msf.frame +
                        75 * (addr->msf.second - 2 + 60 * addr->msf.minute);
        } else {                        /* CDROM_MSF */
                int lba = addr->lba;
                addr->msf.frame = lba % 75;
                lba /= 75;
                lba += 2;
                addr->msf.second = lba % 60;
                addr->msf.minute = lba / 60;
        }
        *curr = requested;
}

/* All checking and format change makes this code really hard to read!
 * So let's make some check and memory move macros.  These macros are
 * a little inefficient when both used in the same piece of code, as
 * verify_area is used twice, but who cares, as ioctl() calls
 * shouldn't be in inner loops.
 */
#define GETARG(type, x) { \
        int ret=verify_area(VERIFY_READ, (void *) arg, sizeof x); \
	    if (ret) return ret; \
	    copy_from_user(&x, (type *) arg, sizeof x); }
#define PUTARG(type, x) { \
	    int ret=verify_area(VERIFY_WRITE, (void *) arg, sizeof x); \
	    if (ret) return ret; \
	    copy_to_user((type *) arg, &x, sizeof x); }

/* Some of the cdrom ioctls are not implemented here, because these
 * appear to be either too device-specific, or it is not clear to me
 * what use they are. These are (number of drivers that support them
 * in parenthesis): CDROMREADMODE1 (2+ide), CDROMREADMODE2 (2+ide),
 * CDROMREADAUDIO (2+ide), CDROMREADRAW (2), CDROMREADCOOKED (2),
 * CDROMSEEK (2), CDROMPLAYBLK (scsi), CDROMREADALL (1). Read-audio,
 * OK (although i guess the record companies aren't too happy with
 * this, most drives therefore refuse to transport audio data).  But
 * why are there 5 different READs defined? For now, these functions
 * are left over to the device-specific ioctl routine,
 * cdo->dev_ioctl. Note that as a result of this, no
 * memory-verification is performed for these ioctls.
 */
static
int cdrom_ioctl(struct inode *ip, struct file *fp,
                unsigned int cmd, unsigned long arg)
{
        kdev_t dev = ip->i_rdev;
        struct cdrom_device_info *cdi = cdrom_find_device (dev);
        struct cdrom_device_ops *cdo;

        if (cdi == NULL)
                return -ENODEV;
        cdo = cdi->ops;
        /* the first few commands do not deal with audio capabilities, but
           only with routines in cdrom device operations. */
        switch (cmd) {
                /* maybe we should order cases after statistics of use? */

        case CDROMMULTISESSION: 
        {
                struct cdrom_multisession ms_info;
                u_char requested_format;
                if (!(cdo->capability & CDC_MULTI_SESSION))
                        return -EINVAL;
                GETARG(struct cdrom_multisession, ms_info);
                requested_format = ms_info.addr_format;
                ms_info.addr_format = CDROM_LBA;
                cdo->get_last_session(cdi, &ms_info);
                sanitize_format(&ms_info.addr, &ms_info.addr_format, 
                                requested_format);
                PUTARG(struct cdrom_multisession, ms_info);
                return 0;
        }
                
        case CDROMEJECT:
                if (cdo->capability & ~cdi->mask & CDC_OPEN_TRAY) {
			if (cdi->use_count == 1) {
			        if (cdo->capability & ~cdi->mask & CDC_LOCK)
					cdo->lock_door(cdi, 0);
				return cdo->tray_move(cdi, 1);
			} else
				return -EBUSY;
		} else
                        return -EINVAL;
		
        case CDROMCLOSETRAY:
                if (cdo->capability & ~cdi->mask & CDC_CLOSE_TRAY)
			return cdo->tray_move(cdi, 0);
                else
                        return -EINVAL;
                
        case CDROMEJECT_SW:
                cdi->options &= ~(CDO_AUTO_CLOSE | CDO_AUTO_EJECT);
                if (arg)
                        cdi->options |= CDO_AUTO_CLOSE | CDO_AUTO_EJECT;
                return 0;

        case CDROM_MEDIA_CHANGED:
        	if (cdo->capability & ~cdi->mask & CDC_MEDIA_CHANGED) {
                	if (arg == CDSL_CURRENT)
                        	return media_changed(cdi, 1);
                	else if ((int)arg < cdi->capacity &&
                                 cdo->capability & ~cdi->mask
                                 &CDC_SELECT_DISC)
                        	return cdo->media_changed (cdi, arg);
                        else
                        	return -EINVAL;
                }
                else
                        return -EINVAL;
                
        case CDROM_SET_OPTIONS:
                cdi->options |= (int) arg;
                return cdi->options;
		
        case CDROM_CLEAR_OPTIONS:
                cdi->options &= ~(int) arg;
                return cdi->options;
                
        case CDROM_SELECT_SPEED:
                if ((int)arg <= cdi->speed &&
                    cdo->capability & ~cdi->mask & CDC_SELECT_SPEED)
                        return cdo->select_speed(cdi, arg);
                else
                        return -EINVAL;

        case CDROM_SELECT_DISC:
                if ((arg == CDSL_CURRENT) || (arg == CDSL_NONE))
                	return cdo->select_disc(cdi, arg);
                if ((int)arg < cdi->capacity &&
                    cdo->capability & ~cdi->mask & CDC_SELECT_DISC)
			return cdo->select_disc(cdi, arg);
                else
			return -EINVAL;
		
/* The following function is implemented, although very few audio
 * discs give Universal Product Code information, which should just be
 * the Medium Catalog Number on the box.  Note, that the way the code
 * is written on the CD is /not/ uniform across all discs!
 */
        case CDROM_GET_MCN: {
                struct cdrom_mcn mcn;
                if (!(cdo->capability & CDC_MCN))
                        return -EINVAL;
                if (!cdo->get_mcn(cdi, &mcn)) {
                        PUTARG(struct cdrom_mcn, mcn);
                        return 0;
                }
                return -EINVAL;
        }
                
        case CDROM_DRIVE_STATUS:
                if ((arg == CDSL_CURRENT) || (arg == CDSL_NONE))
                	return cdo->drive_status(cdi, arg);
                if (cdo->drive_status == NULL ||
                            ! (cdo->capability & ~cdi->mask & CDC_SELECT_DISC
                            && (int)arg < cdi->capacity)) 
                	return -EINVAL;
                else
			return cdo->drive_status(cdi, arg);
		
        case CDROM_DISC_STATUS:
		if (cdo->disc_status == NULL)
			return -EINVAL;
		else
			return cdo->disc_status(cdi);

        case CDROM_CHANGER_NSLOTS:
        	return cdi->capacity;

/* The following is not implemented, because there are too many
 * different data type. We could support /1/ raw mode, that is large
 * enough to hold everything.
 */
		
#if 0
        case CDROMREADMODE1: {
                struct cdrom_msf msf;
                char buf[CD_FRAMESIZE];
                GETARG(struct cdrom_msf, msf);
                if (!cdo->read_audio(dev, cmd, &msf, &buf, cdi)) {
                        PUTARG(char *, buf);
                        return 0;
                }
                return -EINVAL;
        }
#endif
	} /* switch */
	
/* Now all the audio-ioctls follow, they are all routed through the
   same call audio_ioctl(). */

	if (cdo->capability & CDC_PLAY_AUDIO)
		switch (cmd) {
                case CDROMSUBCHNL: 
                {
                        struct cdrom_subchnl q;
                        u_char requested, back;
                        GETARG(struct cdrom_subchnl, q);
                        requested = q.cdsc_format;
                        q.cdsc_format = CDROM_MSF;
                        if (!cdo->audio_ioctl(cdi, cmd, &q)) {
                                back = q.cdsc_format; /* local copy */
                                sanitize_format(&q.cdsc_absaddr, &back,
                                requested);
                                sanitize_format(&q.cdsc_reladdr,
                                &q.cdsc_format, requested);
                                PUTARG(struct cdrom_subchnl, q);
                                return 0;
                        } else
                                return -EINVAL;
                }
                case CDROMREADTOCHDR: {
                        struct cdrom_tochdr header;
                        GETARG(struct cdrom_tochdr, header);
                        if (!cdo->audio_ioctl(cdi, cmd, &header)) {
                                PUTARG(struct cdrom_tochdr, header);
                                return 0;
                        } else
                                return -EINVAL;
                }
                case CDROMREADTOCENTRY: {
                        struct cdrom_tocentry entry;
                        u_char requested_format;
                        GETARG(struct cdrom_tocentry, entry);
                        requested_format = entry.cdte_format;
                        /* make interface to low-level uniform */
                        entry.cdte_format = CDROM_MSF; 
                        if (!(cdo->audio_ioctl(cdi, cmd, &entry))) {
                                sanitize_format(&entry.cdte_addr,
                                &entry.cdte_format, requested_format);
                                PUTARG(struct cdrom_tocentry, entry);
                                return 0;
                        } else
                                return -EINVAL;
                }
                case CDROMPLAYMSF: {
                        struct cdrom_msf msf;
                        GETARG(struct cdrom_msf, msf);
                        return cdo->audio_ioctl(cdi, cmd, &msf);
                }
                case CDROMPLAYTRKIND: {
                        struct cdrom_ti track_index;
                        GETARG(struct cdrom_ti, track_index);
                        return cdo->audio_ioctl(cdi, cmd, &track_index);
                }
                case CDROMVOLCTRL: {
                        struct cdrom_volctrl volume;
                        GETARG(struct cdrom_volctrl, volume);
                        return cdo->audio_ioctl(cdi, cmd, &volume);
                }
                case CDROMVOLREAD: {
                        struct cdrom_volctrl volume;
                        if (!cdo->audio_ioctl(cdi, cmd, &volume)) {
                                PUTARG(struct cdrom_volctrl, volume);
                                return 0;
                        }
                        return -EINVAL;
                }
                case CDROMSTART:
                case CDROMSTOP:
                case CDROMPAUSE:
                case CDROMRESUME:
			return cdo->audio_ioctl(cdi, cmd, NULL);
		} /* switch */

	if (cdo->dev_ioctl != NULL)     /* device specific ioctls? */
		return cdo->dev_ioctl(cdi, cmd, arg);
	return -EINVAL;
}

#ifdef MODULE
int init_module(void)
{
	printk(KERN_INFO "Module inserted: " VERSION "\n");
	return 0;
}

void cleanup_module(void)
{
	/*
	printk(KERN_INFO "Module cdrom removed\n");
	*/
}

#endif
/*
 * Local variables:
 * comment-column: 40
 * compile-command: "gcc -DMODULE -D__KERNEL__ -I. -I/usr/src/linux-obj/include -Wall -Wstrict-prototypes -O2 -m486 -c cdrom.c -o cdrom.o"
 * End:
 */
