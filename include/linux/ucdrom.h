/* ucdrom.h. Uniform cdrom data structures for cdrom.c. 	-*- linux-c -*-
   Copyright (c) 1996 David van Leeuwen. 
 */

#ifndef LINUX_UCDROM_H
#define LINUX_UCDROM_H

#ifdef __KERNEL__
struct cdrom_device_ops {
/* routines */
	int (*open) (kdev_t, int);
	void (*release) (kdev_t);
	int (*open_files) (kdev_t);      /* number of open files */
	int (*drive_status) (kdev_t);
	int (*disc_status) (kdev_t);
	int (*media_changed) (kdev_t);
	int (*tray_move) (kdev_t, int);
	int (*lock_door) (kdev_t, int);
	int (*select_speed) (kdev_t, int);
	int (*select_disc) (kdev_t, int);
	int (*get_last_session) (kdev_t, struct cdrom_multisession *);
	int (*get_mcn) (kdev_t, struct cdrom_mcn *);
	int (*reset) (kdev_t dev);       /* hard reset device */
	int (*audio_ioctl) (kdev_t, unsigned int, void *); /* play stuff */
	int (*dev_ioctl) (kdev_t, unsigned int, unsigned long); /* dev-specific */
/* specifications */
	const int capability;           /* capability flags */
	int mask;                       /* mask of capability: disables them */
	const int speed;                /* maximum speed for reading data */
	const int minors;               /* number of minor devs supported */
	const int capacity;             /* number of discs in jukebox */
/* device-related storage */
	int options;                    /* options flags */
	long mc_flags;                  /* media change buffer flags (2*16) */
};
#endif

/* capability flags */
#define CDC_CLOSE_TRAY	0x1             /* caddy systems _can't_ close */
#define CDC_OPEN_TRAY	0x2             /* but _can_ eject.  */
#define CDC_LOCK	0x4             /* disable manual eject */
#define CDC_SELECT_SPEED 0x8            /* programmable speed */
#define CDC_SELECT_DISC	0x10            /* select disc from juke-box */
#define CDC_MULTI_SESSION 0x20          /* read sessions>1 */
#define CDC_MCN		0x40            /* Medium Catalog Number */
#define CDC_MEDIA_CHANGED 0x80          /* media changed */
#define CDC_PLAY_AUDIO	0x100           /* audio functions */

/* drive status possibilities */
#define CDS_NO_INFO	0               /* if not implemented */
#define CDS_NO_DISC	1
#define CDS_TRAY_OPEN	2
#define CDS_DRIVE_NOT_READY	3
#define CDS_DISC_OK	4

/* disc status possibilities, other than CDS_NO_DISC */
#define CDS_AUDIO	100
#define CDS_DATA_1	101
#define CDS_DATA_2	102
#define CDS_XA_2_1	103
#define CDS_XA_2_2	104

/* User-configurable behavior options */
#define CDO_AUTO_CLOSE	0x1             /* close tray on first open() */
#define CDO_AUTO_EJECT	0x2             /* open tray on last release() */
#define CDO_USE_FFLAGS	0x4             /* use O_NONBLOCK information on open */
#define CDO_LOCK	0x8             /* lock tray on open files */
#define CDO_CHECK_TYPE	0x10            /* check type on open for data */

/* Some more ioctls to control these options */
#define CDROM_SET_OPTIONS	0x5320
#define CDROM_CLEAR_OPTIONS	0x5321
#define CDROM_SELECT_SPEED	0x5322  /* head-speed */
#define CDROM_SELECT_DISC	0x5323  /* for juke-boxes */
#define CDROM_MEDIA_CHANGED	0x5325
#define CDROM_DRIVE_STATUS	0x5326  /* tray position, etc. */
#define CDROM_DISC_STATUS	0x5327  /* disc type etc. */

/* Rename an old ioctl */
#define CDROM_GET_MCN	CDROM_GET_UPC	/* medium catalog number */

#ifdef __KERNEL__
/* the general file operations structure: */
extern struct file_operations cdrom_fops;

extern int register_cdrom(int major, char *name,
                          struct cdrom_device_ops *cdo);
extern int unregister_cdrom(int major, char *name);
#endif

#endif	/* LINUX_UCDROM_H */
/*
 * Local variables:
 * comment-column: 40
 * End:
 */
