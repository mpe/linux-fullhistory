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

#include <linux/cdrom.h>
#include <linux/ucdrom.h>

#define FM_WRITE	0x2                 /* file mode write bit */

#define VERSION "$Id: cdrom.c,v 0.4 1996/04/17 20:47:50 david Exp david $"

/* Not-exported routines. */
int cdrom_open(struct inode *ip, struct file *fp);
void cdrom_release(struct inode *ip, struct file *fp);
int cdrom_ioctl(struct inode *ip, struct file *fp,
				unsigned int cmd, unsigned long arg);
int cdrom_media_changed(kdev_t dev);

struct file_operations cdrom_fops =
{
        NULL,                           /* lseek */
        block_read,                     /* read - general block-dev read */
        block_write,                    /* write - general block-dev write */
        NULL,                           /* readdir */
        NULL,                           /* select */
        cdrom_ioctl,                    /* ioctl */
        NULL,                           /* mmap */
        cdrom_open,                     /* open */
        cdrom_release,                  /* release */
        NULL,                           /* fsync */
        NULL,                           /* fasync */
        cdrom_media_changed,            /* media_change */
        NULL                            /* revalidate */
};

static struct cdrom_device_ops *cdromdevs[MAX_BLKDEV] = {
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
int register_cdrom(int major, char *name, struct cdrom_device_ops *cdo)
{
        int *change_capability = &cdo->capability; /* hack, gcc complains OK */
	
        if (major < 0 || major >= MAX_BLKDEV)
                return -1;
        if (cdo->open_files == NULL || cdo->open == NULL || 
            cdo->release == NULL)
                return -2;
        ENSURE(tray_move, CDC_CLOSE_TRAY | CDC_OPEN_TRAY);
        ENSURE(lock_door, CDC_LOCK);
        ENSURE(select_speed, CDC_SELECT_SPEED);
        ENSURE(select_disc, CDC_SELECT_DISC);
        ENSURE(get_last_session, CDC_MULTI_SESSION);
        ENSURE(audio_ioctl, CDC_PLAY_AUDIO);
        ENSURE(media_changed, CDC_MEDIA_CHANGED);
        cdromdevs[major] = cdo;
        cdo->options = CDO_AUTO_CLOSE | CDO_USE_FFLAGS | CDO_LOCK;
        cdo->mc_flags = 0;
        return 0;
}
#undef ENSURE

int unregister_cdrom(int major, char *name)
{
        if (major < 0 || major >= MAX_BLKDEV)
                return -1;
        if (cdromdevs[major] == NULL)
                return -2;
        cdromdevs[major] = NULL;
        return 0;
}

/* We need our own cdrom error types! This is a temporary solution. */

#define ENOMEDIUM EAGAIN				/* no medium in removable device */

/* We use the open-option O_NONBLOCK to indicate that the
 * purpose of opening is only for subsequent ioctl() calls; no device
 * integrity checks are performed.
 *
 * We hope that all cd-player programs will adopt this convention. It
 * is in their own interest: device control becomes a lot easier
 * this way.
 */
int open_for_data(struct cdrom_device_ops *, kdev_t);

int cdrom_open(struct inode *ip, struct file *fp)
{
	kdev_t dev = ip->i_rdev;
        struct cdrom_device_ops *cdo = cdromdevs[MAJOR(dev)];
        int purpose = !!(fp->f_flags & O_NONBLOCK);

        if (cdo == NULL || MINOR(dev) >= cdo->minors)
                return -ENODEV;
        if (fp->f_mode & FM_WRITE)
                return -EROFS;				
        purpose = purpose || !(cdo->options & CDO_USE_FFLAGS);
        if (cdo->open_files(dev) || purpose) 
                return cdo->open(dev, purpose); 
        else 
                return open_for_data(cdo, dev);
}

int open_for_data(struct cdrom_device_ops * cdo, kdev_t dev)
{
        int ret;
        if (cdo->drive_status != NULL) {
                int ds = cdo->drive_status(dev);
                if (ds == CDS_TRAY_OPEN) {
                        /* can/may i close it? */
                        if (cdo->capability & ~cdo->mask & CDC_CLOSE_TRAY &&
                            cdo->options & CDO_AUTO_CLOSE) {
                                if (cdo->tray_move(dev, 0))
                                        return -EIO;
                        } else
                                return -ENOMEDIUM; /* can't close: too bad */
                        ds = cdo->drive_status(dev);
                        if (ds == CDS_NO_DISC)
                                return -ENOMEDIUM;
                }
        }
        if (cdo->disc_status != NULL) {
                int ds = cdo->disc_status(dev);
                if (ds == CDS_NO_DISC)
                        return -ENOMEDIUM;
                if (cdo->options & CDO_CHECK_TYPE &&
                    ds != CDS_DATA_1)
                        return -ENODATA;
        }
        /* all is well, we can open the device */
        ret = cdo->open(dev, 0); /* open for data */
        if (cdo->capability & ~cdo->mask & CDC_LOCK &&
            cdo->options & CDO_LOCK)
                cdo->lock_door(dev, 1);
        return ret;
}

/* Admittedly, the logic below could be performed in a nicer way. */
void cdrom_release(struct inode *ip, struct file *fp)
{
        kdev_t dev = ip->i_rdev;
        struct cdrom_device_ops *cdo = cdromdevs[MAJOR(dev)];
	
        if (cdo == NULL || MINOR(dev) >= cdo->minors)
                return;
        if (cdo->open_files(dev) == 1 && /* last process that closes dev */
            cdo->options & CDO_LOCK &&  
            cdo->capability & ~cdo->mask & CDC_LOCK) 
                cdo->lock_door(dev, 0);
        cdo->release(dev);
        if (cdo->open_files(dev) == 0) { /* last process that closes dev */
                sync_dev(dev);
                invalidate_buffers(dev);
                if (cdo->options & CDO_AUTO_EJECT &&
                    cdo->capability & ~cdo->mask & CDC_OPEN_TRAY)
                        cdo->tray_move(dev, 1);
        }
}

/* We want to make media_changed accessible to the user through an
 * ioctl. The main problem now is that we must double-buffer the
 * low-level implementation, to assure that the VFS and the user both
 * see a medium change once.
 *
 * For now, i've implemented only 16 minor devs (half a long), i have to
 * think of a better solution... $Queue$ is either 0 or 1. Queue 0 is
 * in the lower 16 bits, queue 1 in the higher 16 bits.
 */

int media_changed(kdev_t dev, int queue)
{
        unsigned int major = MAJOR(dev);
        unsigned int minor = MINOR(dev);
        struct cdrom_device_ops *cdo = cdromdevs[major];
        int ret;
        unsigned long mask = 1 << (16 * queue + minor);

        queue &= 1;
        if (cdo == NULL || minor >= 16)
                return -1;
        ret = !!(cdo->mc_flags & mask); /* changed since last call? */
        if (cdo->media_changed(dev)) {
                cdo->mc_flags |= 0x10001 << minor; /* set bit on both queues */
                ret |= 1;
        }
        cdo->mc_flags &= ~mask;         /* clear bit */
        return ret;
}

int cdrom_media_changed(kdev_t dev)
{
        struct cdrom_device_ops *cdo = cdromdevs[MAJOR(dev)];
        if (cdo == NULL || MINOR(dev) >= cdo->minors)
                return -ENODEV;
        if (cdo->media_changed == NULL)
                return -EINVAL;
        return media_changed(dev, 0);
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

#undef current							/* set in sched.h */

void sanitize_format(union cdrom_addr *addr,
                     u_char * current, u_char requested)
{
        if (*current == requested)
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
        *current = requested;
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
	    memcpy_fromfs(&x, (type *) arg, sizeof x); }
#define PUTARG(type, x) { \
	    int ret=verify_area(VERIFY_WRITE, (void *) arg, sizeof x); \
	    if (ret) return ret; \
	    memcpy_tofs((type *) arg, &x, sizeof x); }

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
int cdrom_ioctl(struct inode *ip, struct file *fp,
                unsigned int cmd, unsigned long arg)
{
        kdev_t dev = ip->i_rdev;
        struct cdrom_device_ops *cdo = cdromdevs[MAJOR(dev)];

        if (cdo == NULL || MINOR(dev) >= cdo->minors)
                return -ENODEV;
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
                cdo->get_last_session(dev, &ms_info);
                sanitize_format(&ms_info.addr, &ms_info.addr_format, 
                                requested_format);
                PUTARG(struct cdrom_multisession, ms_info);
                return 0;
        }
                
        case CDROMEJECT:
                if (cdo->open_files(dev) == 1 && 
                    cdo->capability & ~cdo->mask & CDC_OPEN_TRAY)
                        return cdo->tray_move(dev, 1);
                else
                        return -EINVAL;
		
        case CDROMCLOSETRAY:
                if (cdo->open_files(dev) == 1 && 
                    cdo->capability & ~cdo->mask & CDC_CLOSE_TRAY)
                        return cdo->tray_move(dev, 0);
                else
                        return -EINVAL;
                
        case CDROMEJECT_SW:
                cdo->options &= ~(CDO_AUTO_CLOSE | CDO_AUTO_EJECT);
                if (arg)
                        cdo->options |= CDO_AUTO_CLOSE | CDO_AUTO_EJECT;
                return 0;

        case CDROM_MEDIA_CHANGED:
                if (cdo->capability & ~cdo->mask & CDC_MEDIA_CHANGED)
                        return media_changed(dev, 1);
                else
                        return -EINVAL;
                
        case CDROM_SET_OPTIONS:
                cdo->options |= (int) arg;
                return cdo->options;
		
        case CDROM_CLEAR_OPTIONS:
                cdo->options &= ~(int) arg;
                return cdo->options;
                
        case CDROM_SELECT_SPEED:
                if (0 <= arg && arg < (int) (cdo->speed + 0.5) &&
                    cdo->capability & ~cdo->mask & CDC_SELECT_SPEED)
                        return cdo->select_speed(dev, arg);
                else
                        return -EINVAL;
                
        case CDROM_SELECT_DISC:
                if (0 <= arg && arg <= cdo->capacity &&
                    cdo->capability & ~cdo->mask & CDC_SELECT_DISC)
                        return cdo->select_disc(dev, arg);
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
                if (!cdo->get_mcn(dev, &mcn)) {
                        PUTARG(struct cdrom_mcn, mcn);
                        return 0;
                }
                return -EINVAL;
        }
                
        case CDROM_DRIVE_STATUS:
                if (cdo->drive_status == NULL)
			return -EINVAL;
		else
			return cdo->drive_status(dev);
		
        case CDROM_DISC_STATUS:
		if (cdo->disc_status == NULL)
			return -EINVAL;
		else
			return cdo->disc_status(dev);
		
/* The following is not implemented, because there are too many
 * different data type. We could support /1/ raw mode, that is large
 * enough to hold everything.
 */
		
#if 0
        case CDROMREADMODE1: {
                struct cdrom_msf msf;
                char buf[CD_FRAMESIZE];
                GETARG(struct cdrom_msf, msf);
                if (!cdo->read_audio(dev, cmd, &msf, &buf)) {
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
                        if (!cdo->audio_ioctl(dev, cmd, &q)) {
                                back = q.cdsc_format; /* local copy */
                                sanitize_format(&q.cdsc_absaddr, &back, requested);
                                sanitize_format(&q.cdsc_reladdr, &q.cdsc_format, requested);
                                PUTARG(struct cdrom_subchnl, q);
                                return 0;
                        } else
                                return -EINVAL;
                }
                case CDROMREADTOCHDR: {
                        struct cdrom_tochdr header;
                        GETARG(struct cdrom_tochdr, header);
                        if (!cdo->audio_ioctl(dev, cmd, &header)) {
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
                        if (!(cdo->audio_ioctl(dev, cmd, &entry))) {
                                sanitize_format(&entry.cdte_addr, &entry.cdte_format, requested_format);
                                PUTARG(struct cdrom_tocentry, entry);
                                return 0;
                        } else
                                return -EINVAL;
                }
                case CDROMPLAYMSF: {
                        struct cdrom_msf msf;
                        GETARG(struct cdrom_mdf, msf);
                        return cdo->audio_ioctl(dev, cmd, &msf);
                }
                case CDROMPLAYTRKIND: {
                        struct cdrom_ti track_index;
                        GETARG(struct cdrom_ti, track_index);
                        return cdo->audio_ioctl(dev, cmd, &track_index);
                }
                case CDROMVOLCTRL: {
                        struct cdrom_volctrl volume;
                        GETARG(struct cdrom_volctl, volume);
                        return cdo->audio_ioctl(dev, cmd, &volume);
                }
                case CDROMVOLREAD: {
                        struct cdrom_volctrl volume;
                        if (!cdo->audio_ioctl(dev, cmd, &volume)) {
                                PUTARG(struct cdrom_volctl, volume);
                                return 0;
                        }
                        return -EINVAL;
                }
                case CDROMSTART:
                case CDROMSTOP:
                case CDROMPAUSE:
                case CDROMRESUME:
			return cdo->audio_ioctl(dev, cmd, NULL);
		} /* switch */

	if (cdo->dev_ioctl != NULL)     /* device specific ioctls? */
		return cdo->dev_ioctl(dev, cmd, arg);
	return -EINVAL;
}

#ifdef MODULE
int init_module(void)
{
	printk(KERN_INFO "Module inserted " VERSION "\n");
	return 0;
}

void cleanup_module(void)
{
	printk(KERN_INFO "Module cdrom removed\n");
}

#endif
/*
 * Local variables:
 * comment-column: 40
 * compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux-obj/include -Wall -Wstrict-prototypes -O2 -m486 -c cdrom.c -o cdrom.o"
 * End:
 */
