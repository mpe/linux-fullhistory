/*
 * linux/radio.h
 *
 * Include for radio card support under linux
 * Another pointless suid-binary removal utility... :-)
 */

#ifndef _LINUX_RADIO_H
#define _LINUX_RADIO_H

#include <linux/ioctl.h>

/*
 * Constants
 */
/* Various types of Radio card... */
/* (NB.  I've made this a bit-field.  It might make the difference one day.) */
#define	RADIO_TYPE_UNSUP	0x0000
#define	RADIO_TYPE_RTRACK	0x0001	/* AIMSlab RadioTrack (RadioReveal) card -- basic, to say the least */
#define	RADIO_TYPE_WINRADIO	0x0002	/* Dunno, but made by someone */
#define	RADIO_TYPE_TYPHOON	0x0004	/* It exists... */

/* waveband types */
#define	RADIO_PROTOCOL_AM	0x0010	/* AM "protocol" */
#define	RADIO_PROTOCOL_FM	0x0020	/* FM "protocol" */
#define	RADIO_PROTOCOL_SSB	0x0040	/* SSB */
/* and no doubt some other stuff, too (Brian?) */


/* the following are _very_ inaccurate; essentially, all that
 * they do is provide a "name" for client programs
 */
#define	RADIO_BAND_UNKNOWN	0x0000	/* other */
#define	RADIO_BAND_AM_SW	0x0100	/* short wave (?) */
#define	RADIO_BAND_AM_MW	0x0200	/* medium wave (540 - 1600) */
#define	RADIO_BAND_AM_LW	0x0400	/* long wave (150 - 270) */
#define	RADIO_BAND_FM_STD	0x1000	/* "standard" FM band (i.e.  88 - 108 or so) */


/* Since floating-point stuff is illegal in the kernel, we use these
 * pairs of macros to convert to, and from userland floats
 * (I hope these are general enough!)
 */
/* Remember to make sure that all of these are integral... */
/* Also remember to pass sensible things in here (MHz for FM, kHz for AM) */
#define	RADIO_FM_RES		100	/* 0.01 MHZ */
#define	RADIO_FM_FRTOINT(fl)	((int)(((float)(fl))*RADIO_FM_RES))
#define	RADIO_FM_INTTOFR(fr)	((float)(((int)(fr))/RADIO_FM_RES))

/* Old RadioTrack definitions
#define RADIO_FM_FRTOINT(fl)	((int)(((float)(fl)-88.0)*40)+0xf6c)
#define RADIO_FM_INTTOFR(fr)	((float)(((fr)-0xf6c)/40)+88.0)
*/

#define	RADIO_AM_RES		1	/* 1 kHz */
#define	RADIO_AM_FRTOINT(fl)	((int)(((float)(fl))*RADIO_AM_RES))
#define	RADIO_AM_INTTOFR(fr)	((float)(((int)(fr))/RADIO_AM_RES))


/*
 * Structures 
 */
/* query structures */
struct radio_cap {
	int dev_num;			/* device index */
	int type;			/* device type (see above) */
	int num_bwidths;		/* number of "bandwidths" supported */
	int volmin, volmax;		/* min/max in steps of one */
};

struct radio_band {
	int dev_num;			/* device index (IN) */
	int index;			/* "bandwidth" index (IN) */
	int proto;			/* protocol (AM, FM, SSB, etc) (OUT) */
	int types;			/* see RADIO_BAND_* above */
	int freqmin,freqmax;		/* encoded according to the macros above */
	int strmin,strmax;		/* min/max signal strength (steps of 1) */
};

/* Previously, this was in four separate structures:
 * radio_vol, radio_freq, radio_band and radio_sigstr,
 * That was foolish, but now it's not so obvious what's going on.
 * Be careful.
 */

struct radio_ctl {
	int dev_num;			/* device index (IN) */
	int value;			/* volume, frequency, band, sigstr */
};


/*
 * ioctl numbers
 */
/* You have _how_ many radio devices? =) */
#define	RADIO_NUMDEVS	_IOR(0x8c, 0x00, int)
#define	RADIO_GETCAPS	_IOR(0x8c, 0x01, struct radio_cap)
#define	RADIO_GETBNDCAP	_IOR(0x8c, 0x02, struct radio_band)

#define	RADIO_SETVOL	_IOW(0x8c, 0x10, struct radio_ctl)
#define	RADIO_GETVOL	_IOR(0x8c, 0x11, struct radio_ctl)
#define	RADIO_SETBAND	_IOW(0x8c, 0x12, struct radio_ctl)
#define	RADIO_GETBAND	_IOR(0x8c, 0x13, struct radio_ctl)
#define	RADIO_SETFREQ	_IOW(0x8c, 0x14, struct radio_ctl)
#define	RADIO_GETFREQ	_IOR(0x8c, 0x15, struct radio_ctl)

#define	RADIO_GETSIGSTR	_IOR(0x8c, 0x30, struct radio_ctl)

/* kernel specific stuff... */
#ifdef __KERNEL__
/* Try to keep the number of function pointers to a minimum.
 * Devices are responsible for updating, or otherwise, the
 * variables here, not the outside wrapper.
 */
struct radio_device;

int radio_add_device(struct radio_device *newdev);

struct radio_device {
	struct radio_cap *cap;
	struct radio_band *bands;	/* pointer to array of radio_bands */
	int (*setvol)(struct radio_device*,int);
	int curvol;
	int (*setband)(struct radio_device*,int);
	int curband;
	int (*setfreq)(struct radio_device*,int);
	int curfreq;
	int (*getsigstr)(struct radio_device*);
	struct radio_device *next;
	void *misc;		/* device internal storage... (eg i/o addresses, etc */
};
#endif /* __KERNEL__ */

#endif /* _LINUX_RADIO_H */
