/* linux/drivers/cdrom/cdrom.c. 
   Copyright (c) 1996, 1997 David A. van Leeuwen.
   Copyright (c) 1997, 1998 Erik Andersen <andersee@debian.org>
   Copyright (c) 1998, 1999 Jens Axboe

   May be copied or modified under the terms of the GNU General Public
   License.  See linux/COPYING for more information.

   Uniform CD-ROM driver for Linux.
   See Documentation/cdrom/cdrom-standard.tex for usage information.

   The routines in the file provide a uniform interface between the
   software that uses CD-ROMs and the various low-level drivers that
   actually talk to the hardware. Suggestions are welcome.
   Patches that work are more welcome though.  ;-)

 To Do List:
 ----------------------------------

 -- Modify sysctl/proc interface. I plan on having one directory per
 drive, with entries for outputing general drive information, and sysctl
 based tunable parameters such as whether the tray should auto-close for
 that drive. Suggestions (or patches) for this welcome!

 -- Change the CDROMREADMODE1, CDROMREADMODE2, CDROMREADAUDIO, and 
 CDROMREADRAW ioctls so they go through the Uniform CD-ROM driver.




 Revision History
 ----------------------------------
 1.00  Date Unknown -- David van Leeuwen <david@tm.tno.nl>
 -- Initial version by David A. van Leeuwen. I don't have a detailed
  changelog for the 1.x series, David?

2.00  Dec  2, 1997 -- Erik Andersen <andersee@debian.org>
  -- New maintainer! As David A. van Leeuwen has been too busy to activly
  maintain and improve this driver, I am now carrying on the torch. If
  you have a problem with this driver, please feel free to contact me.

  -- Added (rudimentary) sysctl interface. I realize this is really weak
  right now, and is _very_ badly implemented. It will be improved...

  -- Modified CDROM_DISC_STATUS so that it is now incorporated into
  the Uniform CD-ROM driver via the cdrom_count_tracks function.
  The cdrom_count_tracks function helps resolve some of the false
  assumptions of the CDROM_DISC_STATUS ioctl, and is also used to check
  for the correct media type when mounting or playing audio from a CD.

  -- Remove the calls to verify_area and only use the copy_from_user and
  copy_to_user stuff, since these calls now provide their own memory
  checking with the 2.1.x kernels.

  -- Major update to return codes so that errors from low-level drivers
  are passed on through (thanks to Gerd Knorr for pointing out this
  problem).

  -- Made it so if a function isn't implemented in a low-level driver,
  ENOSYS is now returned instead of EINVAL.

  -- Simplified some complex logic so that the source code is easier to read.

  -- Other stuff I probably forgot to mention (lots of changes).

2.01 to 2.11 Dec 1997-Jan 1998
  -- TO-DO!  Write changelogs for 2.01 to 2.12.

2.12  Jan  24, 1998 -- Erik Andersen <andersee@debian.org>
  -- Fixed a bug in the IOCTL_IN and IOCTL_OUT macros.  It turns out that
  copy_*_user does not return EFAULT on error, but instead returns the number 
  of bytes not copied.  I was returning whatever non-zero stuff came back from 
  the copy_*_user functions directly, which would result in strange errors.

2.13  July 17, 1998 -- Erik Andersen <andersee@debian.org>
  -- Fixed a bug in CDROM_SELECT_SPEED where you couldn't lower the speed
  of the drive.  Thanks to Tobias Ringstr|m <tori@prosolvia.se> for pointing
  this out and providing a simple fix.
  -- Fixed the procfs-unload-module bug with the fill_inode procfs callback.
  thanks to Andrea Arcangeli
  -- Fixed it so that the /proc entry now also shows up when cdrom is
  compiled into the kernel.  Before it only worked when loaded as a module.

  2.14 August 17, 1998 -- Erik Andersen <andersee@debian.org>
  -- Fixed a bug in cdrom_media_changed and handling of reporting that
  the media had changed for devices that _don't_ implement media_changed.  
  Thanks to Grant R. Guenther <grant@torque.net> for spotting this bug.
  -- Made a few things more pedanticly correct.

2.50 Oct 19, 1998 - Jens Axboe <axboe@image.dk>
  -- New maintainers! Erik was too busy to continue the work on the driver,
  so now Chris Zwilling <chris@cloudnet.com> and Jens Axboe <axboe@image.dk>
  will do their best to follow in his footsteps
  
  2.51 Dec 20, 1998 - Jens Axboe <axboe@image.dk>
  -- Check if drive is capable of doing what we ask before blindly changing
  cdi->options in various ioctl.
  -- Added version to proc entry.
  
  2.52 Jan 16, 1998 - Jens Axboe <axboe@image.dk>
  -- Fixed an error in open_for_data where we would sometimes not return
  the correct error value. Thanks Huba Gaspar <huba@softcell.hu>.
  -- Fixed module usage count - usage was based on /proc/sys/dev
  instead of /proc/sys/dev/cdrom. This could lead to an oops when other
  modules had entries in dev.

-------------------------------------------------------------------------*/

#define REVISION "Revision: 2.52"
#define VERSION "Id: cdrom.c 2.52 1999/01/16"

/* I use an error-log mask to give fine grain control over the type of
   messages dumped to the system logs.  The available masks include: */
#define CD_NOTHING      0x0
#define CD_WARNING	0x1
#define CD_REG_UNREG	0x2
#define CD_DO_IOCTL	0x4
#define CD_OPEN		0x8
#define CD_CLOSE	0x10
#define CD_COUNT_TRACKS 0x20

/* Define this to remove _all_ the debugging messages */
/* #define ERRLOGMASK CD_NOTHING */
#define ERRLOGMASK (CD_WARNING)
/* #define ERRLOGMASK (CD_WARNING|CD_OPEN|CD_COUNT_TRACKS|CD_CLOSE) */
/* #define ERRLOGMASK (CD_WARNING|CD_REG_UNREG|CD_DO_IOCTL|CD_OPEN|CD_CLOSE|CD_COUNT_TRACKS) */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h> 
#include <linux/cdrom.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <asm/fcntl.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

/* used to tell the module to turn on full debugging messages */
static int debug = 0;
/* default compatibility mode */
static int autoclose=1;
static int autoeject=0;
static int lockdoor = 1;
static int check_media_type = 0;
MODULE_PARM(debug, "i");
MODULE_PARM(autoclose, "i");
MODULE_PARM(autoeject, "i");
MODULE_PARM(lockdoor, "i");
MODULE_PARM(check_media_type, "i");

#if (ERRLOGMASK!=CD_NOTHING)
#define cdinfo(type, fmt, args...) \
        if ((ERRLOGMASK & type) || debug==1 ) \
            printk(KERN_INFO "cdrom: " fmt, ## args)
#else
#define cdinfo(type, fmt, args...) 
#endif

/* These are used to simplify getting data in from and back to user land */
#define IOCTL_IN(arg, type, in) { \
            if ( copy_from_user(&in, (type *) arg, sizeof in) ) \
            	return -EFAULT; }

#define IOCTL_OUT(arg, type, out) { \
            if ( copy_to_user((type *) arg, &out, sizeof out) ) \
            	return -EFAULT; }


#define FM_WRITE	0x2                 /* file mode write bit */

/* Not-exported routines. */
static int cdrom_open(struct inode *ip, struct file *fp);
static int cdrom_release(struct inode *ip, struct file *fp);
static int cdrom_ioctl(struct inode *ip, struct file *fp,
				unsigned int cmd, unsigned long arg);
static int cdrom_media_changed(kdev_t dev);
static int open_for_data(struct cdrom_device_info * cdi);
static int check_for_audio_disc(struct cdrom_device_info * cdi,
			 struct cdrom_device_ops * cdo);
static void sanitize_format(union cdrom_addr *addr, 
		u_char * curr, u_char requested);
#ifdef CONFIG_SYSCTL
static void cdrom_sysctl_register(void);
#endif /* CONFIG_SYSCTL */ 
static struct cdrom_device_info *topCdromPtr = NULL;

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
	NULL,				/* flush */
	cdrom_release,                  /* release */
	NULL,                           /* fsync */
	NULL,                           /* fasync */
	cdrom_media_changed,            /* media_change */
	NULL                            /* revalidate */
};

/* This macro makes sure we don't have to check on cdrom_device_ops
 * existence in the run-time routines below. Change_capability is a
 * hack to have the capability flags defined const, while we can still
 * change it here without gcc complaining at every line.
 */
#define ENSURE(call, bits) if (cdo->call == NULL) *change_capability &= ~(bits)

int register_cdrom(struct cdrom_device_info *cdi)
{
	static char banner_printed = 0;
	int major = MAJOR (cdi->dev);
        struct cdrom_device_ops *cdo = cdi->ops;
        int *change_capability = (int *)&cdo->capability; /* hack */

	cdinfo(CD_OPEN, "entering register_cdrom\n"); 

	if (major < 0 || major >= MAX_BLKDEV)
		return -1;
	if (cdo->open == NULL || cdo->release == NULL)
		return -2;
	if ( !banner_printed ) {
		printk(KERN_INFO "Uniform CDROM driver " REVISION "\n");
		banner_printed = 1;
#ifdef CONFIG_SYSCTL
		cdrom_sysctl_register();
#endif /* CONFIG_SYSCTL */ 
	}
	ENSURE(drive_status, CDC_DRIVE_STATUS );
	ENSURE(media_changed, CDC_MEDIA_CHANGED);
	ENSURE(tray_move, CDC_CLOSE_TRAY | CDC_OPEN_TRAY);
	ENSURE(lock_door, CDC_LOCK);
	ENSURE(select_speed, CDC_SELECT_SPEED);
	ENSURE(select_disc, CDC_SELECT_DISC);
	ENSURE(get_last_session, CDC_MULTI_SESSION);
	ENSURE(get_mcn, CDC_MCN);
	ENSURE(reset, CDC_RESET);
	ENSURE(audio_ioctl, CDC_PLAY_AUDIO);
	ENSURE(dev_ioctl, CDC_IOCTLS);
	cdi->mc_flags = 0;
	cdo->n_minors = 0;
        cdi->options = CDO_USE_FFLAGS;
	
	if (autoclose==1 && cdo->capability & ~cdi->mask & CDC_OPEN_TRAY)
		cdi->options |= (int) CDO_AUTO_CLOSE;
	if (autoeject==1 && cdo->capability & ~cdi->mask & CDC_OPEN_TRAY)
		cdi->options |= (int) CDO_AUTO_EJECT;
	if (lockdoor==1)
		cdi->options |= (int) CDO_LOCK;
	if (check_media_type==1)
		cdi->options |= (int) CDO_CHECK_TYPE;

	cdinfo(CD_REG_UNREG, "drive \"/dev/%s\" registered\n", cdi->name);
	cdi->next = topCdromPtr; 	
	topCdromPtr = cdi;
	return 0;
}
#undef ENSURE

int unregister_cdrom(struct cdrom_device_info *unreg)
{
	struct cdrom_device_info *cdi, *prev;
	int major = MAJOR (unreg->dev);

	cdinfo(CD_OPEN, "entering unregister_cdrom\n"); 

	if (major < 0 || major >= MAX_BLKDEV)
		return -1;

	prev = NULL;
	cdi = topCdromPtr;
	while (cdi != NULL && cdi->dev != unreg->dev) {
		prev = cdi;
		cdi = cdi->next;
	}

	if (cdi == NULL)
		return -2;
	if (prev)
		prev->next = cdi->next;
	else
		topCdromPtr = cdi->next;
	cdi->ops->n_minors--;
	cdinfo(CD_REG_UNREG, "drive \"/dev/%s\" unregistered\n", cdi->name);
	return 0;
}

static
struct cdrom_device_info *cdrom_find_device (kdev_t dev)
{
	struct cdrom_device_info *cdi;

	cdi = topCdromPtr;
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
int cdrom_open(struct inode *ip, struct file *fp)
{
	kdev_t dev = ip->i_rdev;
	struct cdrom_device_info *cdi = cdrom_find_device(dev);
	int purpose = !!(fp->f_flags & O_NONBLOCK);
	int ret=0;

	cdinfo(CD_OPEN, "entering cdrom_open\n"); 
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
	cdinfo(CD_OPEN, "Use count for \"/dev/%s\" now %d\n", cdi->name, cdi->use_count);
	/* Do this on open.  Don't wait for mount, because they might
	    not be mounting, but opening with O_NONBLOCK */
	check_disk_change(dev);
	return ret;
}

static
int open_for_data(struct cdrom_device_info * cdi)
{
	int ret;
	struct cdrom_device_ops *cdo = cdi->ops;
	tracktype tracks;
	cdinfo(CD_OPEN, "entering open_for_data\n");
	/* Check if the driver can report drive status.  If it can, we
	   can do clever things.  If it can't, well, we at least tried! */
	if (cdo->drive_status != NULL) {
		ret = cdo->drive_status(cdi, CDSL_CURRENT);
		cdinfo(CD_OPEN, "drive_status=%d\n", ret); 
		if (ret == CDS_TRAY_OPEN) {
			cdinfo(CD_OPEN, "the tray is open...\n"); 
			/* can/may i close it? */
			if (cdo->capability & ~cdi->mask & CDC_CLOSE_TRAY &&
			    cdi->options & CDO_AUTO_CLOSE) {
				cdinfo(CD_OPEN, "trying to close the tray.\n"); 
				ret=cdo->tray_move(cdi,0);
				if (ret) {
					cdinfo(CD_OPEN, "bummer. tried to close the tray but failed.\n"); 
					/* Ignore the error from the low
					level driver.  We don't care why it
					couldn't close the tray.  We only care 
					that there is no disc in the drive, 
					since that is the _REAL_ problem here.*/
					ret=-ENOMEDIUM;
					goto clean_up_and_return;
				}
			} else {
				cdinfo(CD_OPEN, "bummer. this drive can't close the tray.\n"); 
				ret=-ENOMEDIUM;
				goto clean_up_and_return;
			}
			/* Ok, the door should be closed now.. Check again */
			ret = cdo->drive_status(cdi, CDSL_CURRENT);
			if ((ret == CDS_NO_DISC) || (ret==CDS_TRAY_OPEN)) {
				cdinfo(CD_OPEN, "bummer. the tray is still not closed.\n"); 
				cdinfo(CD_OPEN, "tray might not contain a medium.\n");
				ret=-ENOMEDIUM;
				goto clean_up_and_return;
			}
			cdinfo(CD_OPEN, "the tray is now closed.\n"); 
		}
		if (ret!=CDS_DISC_OK) {
			ret = -ENOMEDIUM;
			goto clean_up_and_return;
		}
	}
	cdrom_count_tracks(cdi, &tracks);
	if (tracks.error == CDS_NO_DISC) {
		cdinfo(CD_OPEN, "bummer. no disc.\n");
		ret=-ENOMEDIUM;
		goto clean_up_and_return;
	}
	/* CD-Players which don't use O_NONBLOCK, workman
	 * for example, need bit CDO_CHECK_TYPE cleared! */
	if (tracks.data==0) {
		if (cdi->options & CDO_CHECK_TYPE) {
		    cdinfo(CD_OPEN, "bummer. wrong media type.\n"); 
		    ret=-EMEDIUMTYPE;
		    goto clean_up_and_return;
		}
		else {
		    cdinfo(CD_OPEN, "wrong media type, but CDO_CHECK_TYPE not set.\n");
		}
	}

	cdinfo(CD_OPEN, "all seems well, opening the device.\n"); 

	/* all seems well, we can open the device */
	ret = cdo->open(cdi, 0); /* open for data */
	cdinfo(CD_OPEN, "opening the device gave me %d.\n", ret); 
	/* After all this careful checking, we shouldn't have problems
	   opening the device, but we don't want the device locked if 
	   this somehow fails... */
	if (ret) {
		cdinfo(CD_OPEN, "open device failed.\n"); 
		goto clean_up_and_return;
	}
	if (cdo->capability & ~cdi->mask & CDC_LOCK && 
		cdi->options & CDO_LOCK) {
			cdo->lock_door(cdi, 1);
			cdinfo(CD_OPEN, "door locked.\n");
	}	
	cdinfo(CD_OPEN, "device opened successfully.\n"); 
	return ret;

	/* Something failed.  Try to unlock the drive, because some drivers
	(notably ide-cd) lock the drive after every command.  This produced
	a nasty bug where after mount failed, the drive would remain locked!  
	This ensures that the drive gets unlocked after a mount fails.  This 
	is a goto to avoid bloating the driver with redundant code. */ 
clean_up_and_return:
	cdinfo(CD_WARNING, "open failed.\n"); 
	if (cdo->capability & ~cdi->mask & CDC_LOCK && 
		cdi->options & CDO_LOCK) {
			cdo->lock_door(cdi, 0);
			cdinfo(CD_OPEN, "door unlocked.\n");
	}
	return ret;
}

/* This code is similar to that in open_for_data. The routine is called
   whenever an audio play operation is requested.
*/
int check_for_audio_disc(struct cdrom_device_info * cdi,
			 struct cdrom_device_ops * cdo)
{
        int ret;
	tracktype tracks;
	cdinfo(CD_OPEN, "entering check_for_audio_disc\n");
	if (!(cdi->options & CDO_CHECK_TYPE))
		return 0;
	if (cdo->drive_status != NULL) {
		ret = cdo->drive_status(cdi, CDSL_CURRENT);
		cdinfo(CD_OPEN, "drive_status=%d\n", ret); 
		if (ret == CDS_TRAY_OPEN) {
			cdinfo(CD_OPEN, "the tray is open...\n"); 
			/* can/may i close it? */
			if (cdo->capability & ~cdi->mask & CDC_CLOSE_TRAY &&
			    cdi->options & CDO_AUTO_CLOSE) {
				cdinfo(CD_OPEN, "trying to close the tray.\n"); 
				ret=cdo->tray_move(cdi,0);
				if (ret) {
					cdinfo(CD_OPEN, "bummer. tried to close tray but failed.\n"); 
					/* Ignore the error from the low
					level driver.  We don't care why it
					couldn't close the tray.  We only care 
					that there is no disc in the drive, 
					since that is the _REAL_ problem here.*/
					return -ENOMEDIUM;
				}
			} else {
				cdinfo(CD_OPEN, "bummer. this driver can't close the tray.\n"); 
				return -ENOMEDIUM;
			}
			/* Ok, the door should be closed now.. Check again */
			ret = cdo->drive_status(cdi, CDSL_CURRENT);
			if ((ret == CDS_NO_DISC) || (ret==CDS_TRAY_OPEN)) {
				cdinfo(CD_OPEN, "bummer. the tray is still not closed.\n"); 
				return -ENOMEDIUM;
			}	
			if (ret!=CDS_DISC_OK) {
				cdinfo(CD_OPEN, "bummer. disc isn't ready.\n"); 
				return -EIO;
			}	
			cdinfo(CD_OPEN, "the tray is now closed.\n"); 
		}	
	}
	cdrom_count_tracks(cdi, &tracks);
	if (tracks.error) 
		return(tracks.error);

	if (tracks.audio==0)
		return -EMEDIUMTYPE;

	return 0;
}


/* Admittedly, the logic below could be performed in a nicer way. */
static
int cdrom_release(struct inode *ip, struct file *fp)
{
	kdev_t dev = ip->i_rdev;
	struct cdrom_device_info *cdi = cdrom_find_device (dev);
	struct cdrom_device_ops *cdo = cdi->ops;
	int opened_for_data;

	cdinfo(CD_CLOSE, "entering cdrom_release\n"); 
	if (cdi == NULL)
		return 0;
	if (cdi->use_count > 0) cdi->use_count--;
	if (cdi->use_count == 0)
		cdinfo(CD_CLOSE, "Use count for \"/dev/%s\" now zero\n", cdi->name);
	if (cdi->use_count == 0 &&      /* last process that closes dev*/
	    cdo->capability & CDC_LOCK) {
		cdinfo(CD_CLOSE, "Unlocking door!\n");
		cdo->lock_door(cdi, 0);
		}
	opened_for_data = !(cdi->options & CDO_USE_FFLAGS) ||
		!(fp && fp->f_flags & O_NONBLOCK);
	cdo->release(cdi);
	if (cdi->use_count == 0) {      /* last process that closes dev*/
		struct super_block *sb;
		sync_dev(dev);
		sb = get_super(dev);
		if (sb) invalidate_inodes(sb);
		invalidate_buffers(dev);
		if (opened_for_data &&
		    cdi->options & CDO_AUTO_EJECT &&
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

	if (!(cdi->ops->capability & ~cdi->mask & CDC_MEDIA_CHANGED))
	    return ret;
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
	/* This talks to the VFS, which doesn't like errors - just 1 or 0.  
	 * Returning "0" is always safe (media hasn't been changed). Do that 
	 * if the low-level cdrom driver dosn't support media changed. */ 
	if (cdi == NULL)
		return 0;
	if (cdi->ops->media_changed == NULL)
		return 0;
	if (!(cdi->ops->capability & ~cdi->mask & CDC_MEDIA_CHANGED))
	    return 0;
	return (media_changed(cdi, 0));
}

void cdrom_count_tracks(struct cdrom_device_info *cdi, tracktype* tracks)
{
	struct cdrom_tochdr header;
	struct cdrom_tocentry entry;
	int ret, i;
	tracks->data=0;
	tracks->audio=0;
	tracks->cdi=0;
	tracks->xa=0;
	tracks->error=0;
	cdinfo(CD_COUNT_TRACKS, "entering cdrom_count_tracks\n"); 
        if (!(cdi->ops->capability & CDC_PLAY_AUDIO)) { 
                tracks->error=CDS_NO_INFO;
                return;
        }        
	/* Grab the TOC header so we can see how many tracks there are */
	ret=cdi->ops->audio_ioctl(cdi, CDROMREADTOCHDR, &header);
	if (ret) {
		tracks->error=(ret == -ENOMEDIUM) ? CDS_NO_DISC : CDS_NO_INFO;
		return;
	}	
	/* check what type of tracks are on this disc */
	entry.cdte_format = CDROM_MSF;
	for (i = header.cdth_trk0; i <= header.cdth_trk1; i++) {
		entry.cdte_track  = i;
		if (cdi->ops->audio_ioctl(cdi, CDROMREADTOCENTRY, &entry)) {
			tracks->error=CDS_NO_INFO;
			return;
		}	
		if (entry.cdte_ctrl & CDROM_DATA_TRACK) {
		    if (entry.cdte_format == 0x10)
			tracks->cdi++;
		    else if (entry.cdte_format == 0x20) 
			tracks->xa++;
		    else
			tracks->data++;
		} else
		    tracks->audio++;
		cdinfo(CD_COUNT_TRACKS, "track %d: format=%d, ctrl=%d\n",
		       i, entry.cdte_format, entry.cdte_ctrl);
	}	
	cdinfo(CD_COUNT_TRACKS, "disc has %d tracks: %d=audio %d=data %d=Cd-I %d=XA\n", 
		header.cdth_trk1, tracks->audio, tracks->data, 
		tracks->cdi, tracks->xa);
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

	/* the first few commands do not deal with audio drive_info, but
	   only with routines in cdrom device operations. */
	switch (cmd) {
		/* maybe we should order cases after statistics of use? */

	case CDROMMULTISESSION: {
		int ret;
		struct cdrom_multisession ms_info;
		u_char requested_format;
		cdinfo(CD_DO_IOCTL, "entering CDROMMULTISESSION\n"); 
                if (!(cdo->capability & CDC_MULTI_SESSION))
                        return -ENOSYS;
		IOCTL_IN(arg, struct cdrom_multisession, ms_info);
		requested_format = ms_info.addr_format;
		if (!((requested_format == CDROM_MSF) ||
			(requested_format == CDROM_LBA)))
				return -EINVAL;
		ms_info.addr_format = CDROM_LBA;
		if ((ret=cdo->get_last_session(cdi, &ms_info)))
			return ret;
		sanitize_format(&ms_info.addr, &ms_info.addr_format,
				requested_format);
		IOCTL_OUT(arg, struct cdrom_multisession, ms_info);
		cdinfo(CD_DO_IOCTL, "CDROMMULTISESSION successful\n"); 
		return 0;
		}

	case CDROMEJECT: {
		int ret;
		cdinfo(CD_DO_IOCTL, "entering CDROMEJECT\n"); 
		if (!(cdo->capability & ~cdi->mask & CDC_OPEN_TRAY))
			return -ENOSYS;
		if (cdi->use_count != 1) 
			return -EBUSY;
		if (cdo->capability & ~cdi->mask & CDC_LOCK) {
			if ((ret=cdo->lock_door(cdi, 0)))
				return ret;
			}
		return cdo->tray_move(cdi, 1);
		}

	case CDROMCLOSETRAY:
		cdinfo(CD_DO_IOCTL, "entering CDROMCLOSETRAY\n"); 
		if (!(cdo->capability & ~cdi->mask & CDC_OPEN_TRAY))
			return -ENOSYS;
		return cdo->tray_move(cdi, 0);

	case CDROMEJECT_SW:
		cdinfo(CD_DO_IOCTL, "entering CDROMEJECT_SW\n"); 
		if (!(cdo->capability & ~cdi->mask & CDC_OPEN_TRAY))
			return -ENOSYS;
		cdi->options &= ~(CDO_AUTO_CLOSE | CDO_AUTO_EJECT);
		if (arg)
			cdi->options |= CDO_AUTO_CLOSE | CDO_AUTO_EJECT;
		return 0;

	case CDROM_MEDIA_CHANGED: {
		cdinfo(CD_DO_IOCTL, "entering CDROM_MEDIA_CHANGED\n"); 
		if (!(cdo->capability & ~cdi->mask & CDC_MEDIA_CHANGED))
			return -ENOSYS;
		if (!(cdo->capability & ~cdi->mask & CDC_SELECT_DISC)
		    || arg == CDSL_CURRENT)
			/* cannot select disc or select current disc */
			return media_changed(cdi, 1);
		if ((unsigned int)arg >= cdi->capacity)
			return -EINVAL;
		return cdo->media_changed (cdi, arg);
		}

	case CDROM_SET_OPTIONS:
		cdinfo(CD_DO_IOCTL, "entering CDROM_SET_OPTIONS\n"); 
		if (cdo->capability & arg & ~cdi->mask)
			return -ENOSYS;
		cdi->options |= (int) arg;
		return cdi->options;

	case CDROM_CLEAR_OPTIONS:
		cdinfo(CD_DO_IOCTL, "entering CDROM_CLEAR_OPTIONS\n"); 
		cdi->options &= ~(int) arg;
		return cdi->options;

	case CDROM_SELECT_SPEED: {
		cdinfo(CD_DO_IOCTL, "entering CDROM_SELECT_SPEED\n"); 
		if (!(cdo->capability & ~cdi->mask & CDC_SELECT_SPEED))
			return -ENOSYS;
		return cdo->select_speed(cdi, arg);
		}

	case CDROM_SELECT_DISC: {
		cdinfo(CD_DO_IOCTL, "entering CDROM_SELECT_DISC\n"); 
		if (!(cdo->capability & ~cdi->mask & CDC_SELECT_DISC))
			return -ENOSYS;
                if ((arg == CDSL_CURRENT) || (arg == CDSL_NONE)) 
			return cdo->select_disc(cdi, arg);
		if ((int)arg >= cdi->capacity)
			return -EDRIVE_CANT_DO_THIS;
		return cdo->select_disc(cdi, arg);
		}

/* The following function is implemented, although very few audio
 * discs give Universal Product Code information, which should just be
 * the Medium Catalog Number on the box.  Note, that the way the code
 * is written on the CD is /not/ uniform across all discs!
 */
	case CDROM_GET_MCN: {
		int ret;
		struct cdrom_mcn mcn;
		cdinfo(CD_DO_IOCTL, "entering CDROM_GET_MCN\n"); 
		if (!(cdo->capability & CDC_MCN))
			return -ENOSYS;
		if ((ret=cdo->get_mcn(cdi, &mcn)))
			return ret;
		IOCTL_OUT(arg, struct cdrom_mcn, mcn);
		cdinfo(CD_DO_IOCTL, "CDROM_GET_MCN successful\n"); 
		return 0;
		}

	case CDROM_DRIVE_STATUS: {
		cdinfo(CD_DO_IOCTL, "entering CDROM_DRIVE_STATUS\n"); 
		if (!(cdo->capability & CDC_DRIVE_STATUS))
			return -ENOSYS;
                if ((arg == CDSL_CURRENT) || (arg == CDSL_NONE)) 
			return cdo->drive_status(cdi, arg);
                if (((int)arg > cdi->capacity))
			return -EINVAL;
		return cdo->drive_status(cdi, arg);
		}

	/* Ok, this is where problems start.  The current interface for the
	   CDROM_DISC_STATUS ioctl is flawed.  It makes the false assumption
	   that CDs are all CDS_DATA_1 or all CDS_AUDIO, etc.  Unfortunatly,
	   while this is often the case, it is also very common for CDs to
	   have some tracks with data, and some tracks with audio.  Just 
	   because I feel like it, I declare the following to be the best
	   way to cope.  If the CD has ANY data tracks on it, it will be
	   returned as a data CD.  If it has any XA tracks, I will return
	   it as that.  Now I could simplify this interface by combining these 
	   returns with the above, but this more clearly demonstrates
	   the problem with the current interface.  Too bad this wasn't 
	   designed to use bitmasks...         -Erik 

	   Well, now we have the option CDS_MIXED: a mixed-type CD. 
	   User level programmers might feel the ioctl is not very useful.
	   					---david
	*/
	case CDROM_DISC_STATUS: {
		tracktype tracks;
		cdinfo(CD_DO_IOCTL, "entering CDROM_DISC_STATUS\n"); 
		cdrom_count_tracks(cdi, &tracks);
		if (tracks.error) 
			return(tracks.error);

		/* Policy mode on */
		if (tracks.audio > 0) {
			if (tracks.data==0 && tracks.cdi==0 && tracks.xa==0) 
				return CDS_AUDIO;
			else return CDS_MIXED;
		}
		if (tracks.cdi > 0) return CDS_XA_2_2;
		if (tracks.xa > 0) return CDS_XA_2_1;
		if (tracks.data > 0) return CDS_DATA_1;
		/* Policy mode off */

		cdinfo(CD_WARNING,"This disc doesn't have any tracks I recognise!\n");
		return CDS_NO_INFO;
		}

	case CDROM_CHANGER_NSLOTS:
		cdinfo(CD_DO_IOCTL, "entering CDROM_CHANGER_NSLOTS\n"); 
	return cdi->capacity;

/* The following is not implemented, because there are too many
 * different data types. We could support /1/ raw mode, that is large
 * enough to hold everything.
 */

#if 0
	case CDROMREADMODE1: {
		int ret;
		struct cdrom_msf msf;
		char buf[CD_FRAMESIZE];
		cdinfo(CD_DO_IOCTL, "entering CDROMREADMODE1\n"); 
		IOCTL_IN(arg, struct cdrom_msf, msf);
		if (ret=cdo->read_audio(dev, cmd, &msf, &buf, cdi))
			return ret;
		IOCTL_OUT(arg, __typeof__(buf), buf);
		return 0;
		}
#endif
	} /* switch */

/* Now all the audio-ioctls follow, they are all routed through the
   same call audio_ioctl(). */

#define CHECKAUDIO if ((ret=check_for_audio_disc(cdi, cdo))) return ret

	if (!(cdo->capability & CDC_PLAY_AUDIO))
		return -ENOSYS;
	else {
		switch (cmd) {
		case CDROMSUBCHNL: {
			int ret;
			struct cdrom_subchnl q;
			u_char requested, back;
			/* comment out the cdinfo calls here because they
			   fill up the sys logs when CD players poll the drive*/
			/* cdinfo(CD_DO_IOCTL,"entering CDROMSUBCHNL\n");*/ 
			IOCTL_IN(arg, struct cdrom_subchnl, q);
			requested = q.cdsc_format;
                        if (!((requested == CDROM_MSF) ||
                                (requested == CDROM_LBA)))
                                        return -EINVAL;
			q.cdsc_format = CDROM_MSF;
			if ((ret=cdo->audio_ioctl(cdi, cmd, &q)))
				return ret;
			back = q.cdsc_format; /* local copy */
			sanitize_format(&q.cdsc_absaddr, &back, requested);
			sanitize_format(&q.cdsc_reladdr, &q.cdsc_format, requested);
			IOCTL_OUT(arg, struct cdrom_subchnl, q);
			/* cdinfo(CD_DO_IOCTL, "CDROMSUBCHNL successful\n"); */ 
			return 0;
			}
		case CDROMREADTOCHDR: {
			int ret;
			struct cdrom_tochdr header;
			/* comment out the cdinfo calls here because they
			   fill up the sys logs when CD players poll the drive*/
			/* cdinfo(CD_DO_IOCTL, "entering CDROMREADTOCHDR\n"); */ 
			IOCTL_IN(arg, struct cdrom_tochdr, header);
			if ((ret=cdo->audio_ioctl(cdi, cmd, &header)))
				return ret;
			IOCTL_OUT(arg, struct cdrom_tochdr, header);
			/* cdinfo(CD_DO_IOCTL, "CDROMREADTOCHDR successful\n"); */ 
			return 0;
			}
		case CDROMREADTOCENTRY: {
			int ret;
			struct cdrom_tocentry entry;
			u_char requested_format;
			/* comment out the cdinfo calls here because they
			   fill up the sys logs when CD players poll the drive*/
			/* cdinfo(CD_DO_IOCTL, "entering CDROMREADTOCENTRY\n"); */ 
			IOCTL_IN(arg, struct cdrom_tocentry, entry);
			requested_format = entry.cdte_format;
			if (!((requested_format == CDROM_MSF) || 
				(requested_format == CDROM_LBA)))
					return -EINVAL;
			/* make interface to low-level uniform */
			entry.cdte_format = CDROM_MSF;
			if ((ret=cdo->audio_ioctl(cdi, cmd, &entry)))
				return ret;
			sanitize_format(&entry.cdte_addr,
			&entry.cdte_format, requested_format);
			IOCTL_OUT(arg, struct cdrom_tocentry, entry);
			/* cdinfo(CD_DO_IOCTL, "CDROMREADTOCENTRY successful\n"); */ 
			return 0;
			}
		case CDROMPLAYMSF: {
			int ret;
			struct cdrom_msf msf;
			cdinfo(CD_DO_IOCTL, "entering CDROMPLAYMSF\n"); 
			IOCTL_IN(arg, struct cdrom_msf, msf);
			CHECKAUDIO;
			return cdo->audio_ioctl(cdi, cmd, &msf);
			}
		case CDROMPLAYTRKIND: {
			int ret;
			struct cdrom_ti ti;
			cdinfo(CD_DO_IOCTL, "entering CDROMPLAYTRKIND\n"); 
			IOCTL_IN(arg, struct cdrom_ti, ti);
			CHECKAUDIO;
			return cdo->audio_ioctl(cdi, cmd, &ti);
			}
		case CDROMVOLCTRL: {
			struct cdrom_volctrl volume;
			cdinfo(CD_DO_IOCTL, "entering CDROMVOLCTRL\n"); 
			IOCTL_IN(arg, struct cdrom_volctrl, volume);
			return cdo->audio_ioctl(cdi, cmd, &volume);
			}
		case CDROMVOLREAD: {
			int ret;
			struct cdrom_volctrl volume;
			cdinfo(CD_DO_IOCTL, "entering CDROMVOLREAD\n"); 
			if ((ret=cdo->audio_ioctl(cdi, cmd, &volume)))
				return ret;
			IOCTL_OUT(arg, struct cdrom_volctrl, volume);
			return 0;
			}
		case CDROMSTART:
		case CDROMSTOP:
		case CDROMPAUSE:
		case CDROMRESUME: {
			int ret;
			cdinfo(CD_DO_IOCTL, "doing audio ioctl (start/stop/pause/resume)\n"); 
			CHECKAUDIO;
			return cdo->audio_ioctl(cdi, cmd, NULL);
			}
		} /* switch */
	}

	/* device specific ioctls? */
	if (!(cdo->capability & CDC_IOCTLS))
		return -ENOSYS;
	else 
		return cdo->dev_ioctl(cdi, cmd, arg);
}

EXPORT_SYMBOL(cdrom_count_tracks);
EXPORT_SYMBOL(register_cdrom);
EXPORT_SYMBOL(unregister_cdrom);
EXPORT_SYMBOL(cdrom_fops);

#ifdef CONFIG_SYSCTL

#define CDROM_STR_SIZE 1000

static char cdrom_drive_info[CDROM_STR_SIZE]="info\n";

int cdrom_sysctl_info(ctl_table *ctl, int write, struct file * filp,
                           void *buffer, size_t *lenp)
{
        int pos;
	struct cdrom_device_info *cdi;
	
	if (!*lenp || (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}

	pos = sprintf(cdrom_drive_info, "CD-ROM information, " VERSION "\n");
	
	pos += sprintf(cdrom_drive_info+pos, "\ndrive name:\t");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%s", cdi->name);

	pos += sprintf(cdrom_drive_info+pos, "\ndrive speed:\t");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d", cdi->speed);

	pos += sprintf(cdrom_drive_info+pos, "\ndrive # of slots:");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d", cdi->capacity);

	pos += sprintf(cdrom_drive_info+pos, "\nCan close tray:\t");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_CLOSE_TRAY)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nCan open tray:\t");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_OPEN_TRAY)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nCan lock tray:\t");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_LOCK)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nCan change speed:");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_SELECT_SPEED)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nCan select disk:");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_SELECT_DISC)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nCan read multisession:");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_MULTI_SESSION)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nCan read MCN:\t");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_MCN)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nReports media changed:");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_MEDIA_CHANGED)!=0));

	pos += sprintf(cdrom_drive_info+pos, "\nCan play audio:\t");
	for (cdi=topCdromPtr;cdi!=NULL;cdi=cdi->next)
	    pos += sprintf(cdrom_drive_info+pos, "\t%d",
			   ((cdi->ops->capability & CDC_PLAY_AUDIO)!=0));

        strcpy(cdrom_drive_info+pos,"\n\n");
	*lenp=pos+3;

        return proc_dostring(ctl, write, filp, buffer, lenp);
}

/* Place files in /proc/sys/dev/cdrom */
ctl_table cdrom_table[] = {
	{DEV_CDROM_INFO, "info", &cdrom_drive_info, 
		CDROM_STR_SIZE, 0444, NULL, &cdrom_sysctl_info},
	{0}
	};

ctl_table cdrom_cdrom_table[] = {
	{DEV_CDROM, "cdrom", NULL, 0, 0555, cdrom_table},
	{0}
	};

/* Make sure that /proc/sys/dev is there */
ctl_table cdrom_root_table[] = {
	{CTL_DEV, "dev", NULL, 0, 0555, cdrom_cdrom_table},
	{0}
	};

static struct ctl_table_header *cdrom_sysctl_header;

/*
 * This is called as the fill_inode function when an inode
 * is going into (fill = 1) or out of service (fill = 0).
 * We use it here to manage the module use counts.
 *
 * Note: only the top-level directory needs to do this; if
 * a lower level is referenced, the parent will be as well.
 */
static void cdrom_procfs_modcount(struct inode *inode, int fill)
{
	if (fill) {
		MOD_INC_USE_COUNT;
	} else {
		MOD_DEC_USE_COUNT;
	}
}

static void cdrom_sysctl_register(void)
{
	static int initialized = 0;

	if (initialized == 1)
		return;

	cdrom_sysctl_header = register_sysctl_table(cdrom_root_table, 1);
	cdrom_root_table->de->fill_inode = &cdrom_procfs_modcount;

	initialized = 1;
}

#ifdef MODULE
static void cdrom_sysctl_unregister(void)
{
	unregister_sysctl_table(cdrom_sysctl_header);
}
#endif /* endif MODULE */
#endif /* endif CONFIG_SYSCTL */

#ifdef MODULE

int init_module(void)
{
#ifdef CONFIG_SYSCTL
	cdrom_sysctl_register();
#endif /* CONFIG_SYSCTL */ 
	return 0;
}

void cleanup_module(void)
{
	printk(KERN_INFO "Uniform CD-ROM driver unloaded\n");
#ifdef CONFIG_SYSCTL
	cdrom_sysctl_unregister();
#endif /* CONFIG_SYSCTL */ 
}

#endif /* endif MODULE */



/*
 * Local variables:
 * comment-column: 40
 * compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe -fno-strength-reduce -m486 -DCPU=486 -DMODULE -DMODVERSIONS -include /usr/src/linux/include/linux/modversions.h  -c -o cdrom.o cdrom.c"
 * End:
 */
