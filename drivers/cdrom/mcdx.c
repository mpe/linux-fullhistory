/*
 * The Mitsumi CDROM interface
 * Copyright (C) 1995 Heiko Schlittermann <heiko@lotte.sax.de>
 * VERSION: 2.3
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Thanks to
 *  The Linux Community at all and ...
 *  Martin Harriss (he wrote the first Mitsumi Driver)
 *  Eberhard Moenkeberg (he gave me much support and the initial kick)
 *  Bernd Huebner, Ruediger Helsch (Unifix-Software GmbH, they
 *      improved the original driver)
 *  Jon Tombs, Bjorn Ekwall (module support)
 *  Daniel v. Mosnenck (he sent me the Technical and Programming Reference)
 *  Gerd Knorr (he lent me his PhotoCD)
 *  Nils Faerber and Roger E. Wolff (extensively tested the LU portion)
 *  Andreas Kies (testing the mysterious hangups)
 *  ... somebody forgotten?
 * 
 * 2.1  1996/04/29 Marcin Dalecki <dalecki@namu03.gwdg.de>
 *      Far too many bugfixes/changes to mention them all separately.
 * 2.2  1996/05/06 Marcin Dalecki <dalecki@namu03.gwdg.de>
 *      Mostly fixes to some silly bugs in the previous release :-).
 *      (Hi Michael Thimm! Thank's for lending me Your's double speed drive.)
 * 2.3  1996/05/15 Marcin Dalecki <dalecki@namu03.gwdg.de>
 *	Fixed stereo support. 
 * NOTE:
 *	There will be probably a 3.0 adhering to the new generic non ATAPI
 *	cdrom interface in the unforeseen future.
 */
#define VERSION "2.3"

#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <linux/major.h>
#define MAJOR_NR MITSUMI_X_CDROM_MAJOR
#include <linux/blk.h>

/* 
 * for compatible parameter passing with "insmod" 
 */
#define	mcdx_drive_map mcdx
#include <linux/mcdx.h>

#define REQUEST_SIZE	200
#define DIRECT_SIZE	200

enum drivemodes {
	TOC, DATA, RAW, COOKED
};

#define	MODE0 		0x00
#define MODE1 		0x01
#define MODE2 		0x02

#define DOOR_LOCK 	0x01
#define DOOR_UNLOCK 	0x00

/*
 * Structs used to gather info reported by the drive.
 */

struct s_subqcode {
	u_char adr:4;
	u_char ctrl:4;
	u_char tno;
	u_char index;
	struct cdrom_msf0 tt;
	u_char dummy;		/* padding for matching the returned struct */
	struct cdrom_msf0 dt;
};

struct s_multi {
	unsigned char multi;
	struct cdrom_msf0 msf_last;
};

struct s_play {
	struct cdrom_msf0 start;
	struct cdrom_msf0 stop;
};

/* 
 * Per drive/controller stuff.
 */

struct s_drive_stuff {
	struct wait_queue *busyq;
	
	/* flags */
	u_char introk:1;	/* status of last irq operation */
	u_char busy:1;		/* drive performs an operation */
	u_char eject_sw:1;	/* 1 - eject on last close (default 0) */
	u_char autoclose:1;	/* 1 - close the door on open (default 1) */
	u_char xxx:1;		/* set if changed, reset while open */
	u_char xa:1;		/* 1 if xa disk */
	u_char audio:1;		/* 1 if audio disk */
	u_char eom:1;		/* end of media reached during read request */

	/* drives capabilities */
	u_char door:1;		/* can close/lock tray */
	u_char multi_cap:1;	/* multisession capable */
	u_char double_speed:1;	/* double speed drive */

	/* cd infos */
	unsigned int n_first;
	unsigned int n_last;
	struct cdrom_msf0 msf_leadout;
	struct s_multi multi;

	struct s_subqcode *toc;	/* first entry of the toc array */
	struct s_play resume;	/* where to resume after pause */

	int audiostatus;

	/* `buffer' control */
	unsigned int valid:1;
	int pending;
	int off_direct;
	int off_requested;

	int irq;		/* irq used by this drive */
	unsigned int base;	/* base for all registers of the drive */
	int users;		/* keeps track of open/close */
	int lastsector;		/* last accessible blocks */
};

/*
 * Macros for accessing interface registers
 */

#define DATA_REG	(stuffp->base)
#define RESET_REG	(stuffp->base+1)
#define STAT_REG	(stuffp->base+1)
#define CHAN_REG	(stuffp->base+3)

/* 
 * declared in blk.h 
 */
int mcdx_init(void);
void do_mcdx_request(void);

/* 
 * already declared in init/main 
 */
void mcdx_setup(char *, int *);

/*      
 * Indirect exported functions. These functions are exported by their
 * addresses, such as mcdx_open and mcdx_close in the 
 *  structure fops. 
 */

/* 
 * ???  exported by the mcdx_sigaction struct 
 */
static void mcdx_intr(int, void *, struct pt_regs *);

/* 
   * exported by file_ops 
 */
static int mcdx_open(struct inode *, struct file *);
static void mcdx_close(struct inode *, struct file *);
static int mcdx_ioctl(struct inode *, struct file *,
		      unsigned int, unsigned long);
static int mcdx_media_change(kdev_t);


static int mcdx_blocksizes[MCDX_NDRIVES];
static int mcdx_drive_map[][2] = MCDX_DRIVEMAP;
static struct s_drive_stuff *mcdx_stuffp[MCDX_NDRIVES];
static struct s_drive_stuff *mcdx_irq_map[16] =
{0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0};

static struct file_operations mcdx_fops =
{
	NULL,			/* lseek - use kernel default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* no readdir */
	NULL,			/* no select */
	mcdx_ioctl,		/* ioctl() */
	NULL,			/* no mmap */
	mcdx_open,		/* open() */
	mcdx_close,		/* close() */
	NULL,			/* fsync */
	NULL,			/* fasync */
	mcdx_media_change,	/* media_change */
	NULL			/* revalidate */
};

/*     
 * Misc number converters 
 */

static unsigned int bcd2uint(unsigned char c)
{
	return (c >> 4) * 10 + (c & 0x0f);
}

static unsigned int uint2bcd(unsigned int ival)
{
	return ((ival / 10) << 4) | (ival % 10);
}

static unsigned int msf2log(const struct cdrom_msf0 *pmsf)
{
	return bcd2uint(pmsf->frame)
	    + bcd2uint(pmsf->second) * 75
	    + bcd2uint(pmsf->minute) * 4500
	    - CD_BLOCK_OFFSET;
}

/*      
 * Access to elements of the mcdx_drive_map members 
 */
static inline unsigned int port(int *ip)
{
	return (unsigned int) ip[0];
}

static inline int irq(int *ip)
{
	return ip[1];
}


/*
 * Low level hardware related functions.
 */

/*
 * Return drives status in case of success, -1 otherwise.
 *
 * First we try to get the status information quickly.
 * Then we sleep repeatedly for about 10 usecs, before we finally reach the 
 * timeout. For this reason this command must be called with the drive being 
 * locked!
 */
static int get_status(struct s_drive_stuff *stuffp,
		      unsigned long timeout)
{
	unsigned long bang = jiffies + 2;
	timeout += jiffies;

	do {
		if (!(inb(STAT_REG) & MCDX_RBIT_STEN)) {
			return (inb(DATA_REG) & 0xff);
		}
	} while (jiffies < bang);

	while (inb(STAT_REG) & MCDX_RBIT_STEN) {
		if (jiffies > timeout)
			return -1;
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + HZ / 2;
		schedule();
	}
	return (inb(DATA_REG) & 0xff);
}

/* Send a command to the drive, wait for the result.
 * returns -1 on timeout, drive status otherwise.
 * If buffer is not zero, the result (length size) is stored there.
 * If buffer is zero the size should be the number of bytes to read
 * from the drive.  These bytes are discarded.
 */
static int talk(struct s_drive_stuff *stuffp,
		const unsigned char command,
		void *pars, size_t parslen,
		void *buffer, size_t size,
		unsigned int timeout)
{
	int st;

	while (stuffp->busy) {
		interruptible_sleep_on(&stuffp->busyq);
	}
	stuffp->busy = 1;
	stuffp->valid = 0;
	outb(command, DATA_REG);
	if (parslen)
		outsb(DATA_REG, pars, parslen);

	if (-1 == (st = get_status(stuffp, timeout))) {
		goto end_talk;
	}
	if (st & MCDX_RBIT_CMDERR) {
		printk(KERN_ERR MCDX ": error in command 0x%2x\n", command);
		st = -1;
		goto end_talk;
	}
	/* audio status? */
	if (stuffp->audiostatus == CDROM_AUDIO_INVALID) {
		stuffp->audiostatus =
		    (st & MCDX_RBIT_AUDIOBS) ? CDROM_AUDIO_PLAY : CDROM_AUDIO_NO_STATUS;
	} else if (stuffp->audiostatus == CDROM_AUDIO_PLAY
		   && !(st & MCDX_RBIT_AUDIOBS)) {
		stuffp->audiostatus = CDROM_AUDIO_COMPLETED;
	}
	/* media change? */
	if (st & MCDX_RBIT_CHANGED) {
		stuffp->xxx = 1;
		if (stuffp->toc) {
			kfree(stuffp->toc);
			stuffp->toc = 0;
		}
	}
	/* now actually get the data */
	while (size--) {
		if (-1 == (st = get_status(stuffp, timeout))) {
			break;
		}
		*((char *) buffer) = st;
		buffer++;
	}

      end_talk:
	stuffp->busy = 0;
	wake_up_interruptible(&stuffp->busyq);

	return st;
}

static int issue_command(struct s_drive_stuff *stuffp,
			 unsigned char command,
			 unsigned int timeout)
{
	return talk(stuffp, command, 0, 0, NULL, 0, timeout);
}

static inline int set_command(struct s_drive_stuff *stuffp,
			      const unsigned char command,
			      void *pars, size_t parlen,
			      unsigned int timeout)
{
	return talk(stuffp, command, pars, parlen, NULL, 0, timeout);
}

static inline int get_command(struct s_drive_stuff *stuffp,
			      const unsigned char command,
			      void *buffer, size_t size,
			      unsigned int timeout)
{
	return talk(stuffp, command, NULL, 0, buffer, size, timeout);
}

static int request_subq_code(struct s_drive_stuff *stuffp,
			     struct s_subqcode *sub)
{
	return get_command(stuffp, MCDX_CMD_GET_SUBQ_CODE,
			   sub, sizeof(struct s_subqcode), 2 * HZ);
}

static int request_toc_data(struct s_drive_stuff *stuffp)
{
	char buf[8];
	int ans;

	ans = get_command(stuffp, MCDX_CMD_GET_TOC, buf, sizeof(buf), 2 * HZ);
	if (ans == -1) {
		stuffp->n_first = 0;
		stuffp->n_last = 0;
	} else {
		stuffp->n_first = bcd2uint(buf[0]);
		stuffp->n_last = bcd2uint(buf[1]);
		memcpy(&(stuffp->msf_leadout), buf + 2, 3);
	}
	return ans;
}

static int set_drive_mode(struct s_drive_stuff *stuffp, enum drivemodes mode)
{
	char value;
	if (-1 == get_command(stuffp, MCDX_CMD_GET_DRIVE_MODE,
			      &value, 1, 5 * HZ))
		return -1;
	switch (mode) {
	case TOC:
		value |= 0x04;
		break;
	case DATA:
		value &= ~0x04;
		break;
	case RAW:
		value |= 0x40;
		break;
	case COOKED:
		value &= ~0x40;
		break;
	default:
		break;
	}
	return set_command(stuffp, MCDX_CMD_SET_DRIVE_MODE, &value, 1, 5 * HZ);
}

static int config_drive(struct s_drive_stuff *stuffp)
{
	unsigned char buf[2];
	buf[0] = 0x10;		/* irq enable */
	buf[1] = 0x05;		/* pre, err irq enable */

	if (-1 == set_command(stuffp, MCDX_CMD_CONFIG, buf,
			      sizeof(buf), 1 * HZ))
		return -1;

	buf[0] = 0x02;		/* dma select */
	buf[1] = 0x00;		/* no dma */

	return set_command(stuffp, MCDX_CMD_CONFIG, buf, sizeof(buf), 1 * HZ);
}

/*  
 * Read the toc entries from the CD.
 * Return: -1 on failure, else 0 
 */
int read_toc(struct s_drive_stuff *stuffp)
{
	int trk;
	int retries;

	if (stuffp->toc)
		return 0;
	if (-1 == issue_command(stuffp, MCDX_CMD_HOLD, 2 * HZ))
		return -1;
	if (-1 == set_drive_mode(stuffp, TOC))
		return -EIO;

	/* all seems to be ok so far ... malloc */
	stuffp->toc = kmalloc(sizeof(struct s_subqcode) *
		     (stuffp->n_last - stuffp->n_first + 2), GFP_KERNEL);
	if (!stuffp->toc) {
		printk(KERN_ERR MCDX ": malloc for toc failed\n");
		set_drive_mode(stuffp, DATA);
		return -EIO;
	}
	/* now read actually the index tracks */
	for (trk = 0;
	     trk < (stuffp->n_last - stuffp->n_first + 1);
	     trk++)
		stuffp->toc[trk].index = 0;

	for (retries = 300; retries; retries--) {	/* why 300? */
		struct s_subqcode q;
		unsigned int idx;

		if (-1 == request_subq_code(stuffp, &q)) {
			set_drive_mode(stuffp, DATA);
			return -EIO;
		}
		idx = bcd2uint(q.index);

		if ((idx > 0)
		    && (idx <= stuffp->n_last)
		    && (q.tno == 0)
		    && (stuffp->toc[idx - stuffp->n_first].index == 0)) {
			stuffp->toc[idx - stuffp->n_first] = q;
			trk--;
		}
		if (trk == 0)
			break;
	}
	memset(&stuffp->toc[stuffp->n_last - stuffp->n_first + 1],
	       0, sizeof(stuffp->toc[0]));
	stuffp->toc[stuffp->n_last - stuffp->n_first + 1].dt
	    = stuffp->msf_leadout;

	/* unset toc mode */
	if (-1 == set_drive_mode(stuffp, DATA))
		return -EIO;

	return 0;
}

/*
 * Return 0 on success, error value -1 otherwise.
 */
static int play_track(struct s_drive_stuff *stuffp, const struct cdrom_ti *ti)
{
	struct s_play times;

	if (ti) {
		if (-1 == read_toc(stuffp)) {
			stuffp->audiostatus = CDROM_AUDIO_ERROR;
			return -EIO;
		}
		times.start = stuffp->toc[ti->cdti_trk0 - stuffp->n_first].dt;
		times.stop = stuffp->resume.stop =
		    stuffp->toc[ti->cdti_trk1 - stuffp->n_first + 1].dt;
	} else {
		times = stuffp->resume;
	}
	if (-1 == set_command(stuffp, MCDX_CMD_PLAY,
			      &times, sizeof(times), 5 * HZ)) {
		printk(KERN_WARNING MCDX ": play track timeout\n");
		stuffp->audiostatus = CDROM_AUDIO_ERROR;
		return -EIO;
	}
	stuffp->audiostatus = CDROM_AUDIO_PLAY;

	return 0;
}

static int lock_door(struct s_drive_stuff *stuffp, u_char lock)
{
	if (stuffp->door)
		return set_command(stuffp, MCDX_CMD_LOCK_DOOR,
				   &lock, sizeof(lock), 5 * HZ);
	return 0;
}				/* 
				 * KERNEL INTERFACE FUNCTIONS
				 */
static int mcdx_ioctl(struct inode *ip, struct file *fp,
		      unsigned int command, unsigned long arg)
{
	int ans;
	struct cdrom_ti ti;
	struct cdrom_msf msf;
	struct cdrom_tocentry entry;
	struct s_subqcode *tp = NULL;
	struct cdrom_subchnl sub;
	struct s_subqcode q;
	struct cdrom_tochdr toc;
	struct cdrom_multisession ms;
	struct cdrom_volctrl volctrl;
	struct s_drive_stuff *stuffp = mcdx_stuffp[MINOR(ip->i_rdev)];

	MCDX_TRACE_IOCTL(("mcdx_ioctl():\n"));

	if (!stuffp)
		return -ENXIO;
	if (!ip)
		return -EINVAL;

	/*
	 * Update disk information, when necessary.
	 * This part will only work, when the new disk is of the same type as 
	 * the one which was previously there, esp. also for audio disks.
	 * This doesn't hurt us, since otherwise the mounting/unmounting scheme 
	 * will ensure correct operation.
	 */
	if (stuffp->xxx) {	/* disk changed */
		if ((-1 == request_toc_data(stuffp)) ||
		    (-1 == read_toc(stuffp)))
			return -EIO;
		stuffp->xxx = 0;
	}
	switch (command) {
	case CDROMSTART:	/* spin up the drive */
		MCDX_TRACE_IOCTL(("CDROMSTART\n"));
		/* Don't think we can do this.  Even if we could,
		 * I think the drive times out and stops after a while
		 * anyway.  For now, ignore it.
		 */
		return 0;

	case CDROMSTOP:
		MCDX_TRACE_IOCTL(("CDROMSTOP\n"));
		stuffp->audiostatus = CDROM_AUDIO_INVALID;
		if (-1 == issue_command(stuffp, MCDX_CMD_STOP, 2 * HZ))
			return -EIO;
		return 0;

	case CDROMPLAYTRKIND:
		MCDX_TRACE_IOCTL(("CDROMPLAYTRKIND\n"));

		if ((ans = verify_area(VERIFY_READ, (void *) arg, sizeof(ti))))
			return ans;
		memcpy_fromfs(&ti, (void *) arg, sizeof(ti));
		if ((ti.cdti_trk0 < stuffp->n_first)
		    || (ti.cdti_trk0 > stuffp->n_last)
		    || (ti.cdti_trk1 < stuffp->n_first))
			return -EINVAL;
		if (ti.cdti_trk1 > stuffp->n_last)
			ti.cdti_trk1 = stuffp->n_last;
		return play_track(stuffp, &ti);

	case CDROMPLAYMSF:
		MCDX_TRACE_IOCTL(("CDROMPLAYMSF "));

		if ((ans = verify_area(VERIFY_READ, (void *) arg,
				       sizeof(struct cdrom_msf))))
			 return ans;
		memcpy_fromfs(&msf, (void *) arg, sizeof msf);
		msf.cdmsf_min0 = uint2bcd(msf.cdmsf_min0);
		msf.cdmsf_sec0 = uint2bcd(msf.cdmsf_sec0);
		msf.cdmsf_frame0 = uint2bcd(msf.cdmsf_frame0);
		msf.cdmsf_min1 = uint2bcd(msf.cdmsf_min1);
		msf.cdmsf_sec1 = uint2bcd(msf.cdmsf_sec1);
		msf.cdmsf_frame1 = uint2bcd(msf.cdmsf_frame1);
		stuffp->resume.stop.minute = msf.cdmsf_min1;
		stuffp->resume.stop.second = msf.cdmsf_sec1;
		stuffp->resume.stop.frame = msf.cdmsf_frame1;
		if (-1 == set_command(stuffp, MCDX_CMD_PLAY,
				      &msf, sizeof(msf), 3 * HZ)) {
			return -1;
		}
		stuffp->audiostatus = CDROM_AUDIO_PLAY;
		return 0;

	case CDROMPAUSE:
		MCDX_TRACE_IOCTL(("CDROMPAUSE\n"));

		if (stuffp->audiostatus != CDROM_AUDIO_PLAY)
			return -EINVAL;

		if (-1 == issue_command(stuffp, MCDX_CMD_STOP, 2 * HZ))
			return -EIO;
		if (-1 == request_subq_code(stuffp, &q)) {
			stuffp->audiostatus = CDROM_AUDIO_NO_STATUS;
			return 0;
		}
		stuffp->resume.start = q.dt;
		stuffp->audiostatus = CDROM_AUDIO_PAUSED;
		return 0;

	case CDROMRESUME:
		MCDX_TRACE_IOCTL(("CDROMRESUME\n"));

		if (stuffp->audiostatus != CDROM_AUDIO_PAUSED)
			return -EINVAL;
		return play_track(stuffp, NULL);
	case CDROMREADTOCENTRY:
		MCDX_TRACE_IOCTL(("CDROMREADTOCENTRY\n"));

		if (-1 == read_toc(stuffp))
			return -1;
		if ((ans = verify_area(VERIFY_READ, (void *) arg, sizeof(entry))))
			return ans;
		memcpy_fromfs(&entry, (void *) arg, sizeof(entry));

		if (entry.cdte_track == CDROM_LEADOUT)
			tp = &stuffp->toc[stuffp->n_last - stuffp->n_first + 1];
		else if (entry.cdte_track > stuffp->n_last
			 || entry.cdte_track < stuffp->n_first)
			return -EINVAL;
		else
			tp = &stuffp->toc[entry.cdte_track - stuffp->n_first];

		if (NULL == tp)
			printk(KERN_ERR MCDX ": FATAL.\n");

		entry.cdte_adr = tp->adr;
		entry.cdte_ctrl = tp->ctrl;

		if (entry.cdte_format == CDROM_MSF) {
			entry.cdte_addr.msf.minute = bcd2uint(tp->dt.minute);
			entry.cdte_addr.msf.second = bcd2uint(tp->dt.second);
			entry.cdte_addr.msf.frame = bcd2uint(tp->dt.frame);
		} else if (entry.cdte_format == CDROM_LBA)
			entry.cdte_addr.lba = msf2log(&tp->dt);
		else
			return -EINVAL;

		if ((ans = verify_area(VERIFY_WRITE, (void *) arg, sizeof(entry))))
			return ans;
		memcpy_tofs((void *) arg, &entry, sizeof(entry));

		return 0;

	case CDROMSUBCHNL:
		MCDX_TRACE_IOCTL(("CDROMSUBCHNL\n"));

		if ((ans = verify_area(VERIFY_READ,
				       (void *) arg, sizeof(sub))))
			return ans;

		memcpy_fromfs(&sub, (void *) arg, sizeof(sub));

		if (-1 == request_subq_code(stuffp, &q))
			return -EIO;

		sub.cdsc_audiostatus = stuffp->audiostatus;
		sub.cdsc_adr = q.adr;
		sub.cdsc_ctrl = q.ctrl;
		sub.cdsc_trk = bcd2uint(q.tno);
		sub.cdsc_ind = bcd2uint(q.index);

		if (sub.cdsc_format == CDROM_LBA) {
			sub.cdsc_absaddr.lba = msf2log(&q.dt);
			sub.cdsc_reladdr.lba = msf2log(&q.tt);
		} else if (sub.cdsc_format == CDROM_MSF) {
			sub.cdsc_absaddr.msf.minute = bcd2uint(q.dt.minute);
			sub.cdsc_absaddr.msf.second = bcd2uint(q.dt.second);
			sub.cdsc_absaddr.msf.frame = bcd2uint(q.dt.frame);
			sub.cdsc_reladdr.msf.minute = bcd2uint(q.tt.minute);
			sub.cdsc_reladdr.msf.second = bcd2uint(q.tt.second);
			sub.cdsc_reladdr.msf.frame = bcd2uint(q.tt.frame);
		} else
			return -EINVAL;

		if ((ans = verify_area(VERIFY_WRITE,
				       (void *) arg, sizeof(sub))))
			return ans;
		memcpy_tofs((void *) arg, &sub, sizeof(sub));

		return 0;

	case CDROMREADTOCHDR:
		MCDX_TRACE_IOCTL(("CDROMREADTOCHDR\n"));

		if ((ans = verify_area(VERIFY_WRITE, (void *) arg, sizeof toc)))
			return ans;

		toc.cdth_trk0 = stuffp->n_first;
		toc.cdth_trk1 = stuffp->n_last;
		memcpy_tofs((void *) arg, &toc, sizeof toc);
		return 0;

	case CDROMMULTISESSION:
		MCDX_TRACE_IOCTL(("CDROMMULTISESSION\n"));

		if (0 != (ans = verify_area(VERIFY_READ, (void *) arg,
				     sizeof(struct cdrom_multisession))))
			 return ans;

		memcpy_fromfs(&ms,
			(void *) arg, sizeof(struct cdrom_multisession));
		if (ms.addr_format == CDROM_MSF) {
			ms.addr.msf.minute =
			    bcd2uint(stuffp->multi.msf_last.minute);
			ms.addr.msf.second =
			    bcd2uint(stuffp->multi.msf_last.second);
			ms.addr.msf.frame =
			    bcd2uint(stuffp->multi.msf_last.frame);
		} else if (ms.addr_format == CDROM_LBA)
			ms.addr.lba = msf2log(&stuffp->multi.msf_last);
		else
			return -EINVAL;
		ms.xa_flag = !!stuffp->multi.multi;

		if (0 != (ans = verify_area(VERIFY_WRITE, (void *) arg,
				     sizeof(struct cdrom_multisession))))
			 return ans;

		memcpy_tofs((void *) arg,
			    &ms, sizeof(struct cdrom_multisession));
		return 0;

	case CDROMEJECT:
		MCDX_TRACE_IOCTL(("CDROMEJECT\n"));
		if (stuffp->users > 1)
			return -EBUSY;
		if (stuffp->door) {
			if (-1 == issue_command(stuffp, MCDX_CMD_EJECT, 5 * HZ))
				return -EIO;
		}
		/*
		 * Force rereading of toc next time the disk gets accessed!
		 */
		if (stuffp->toc) {
			kfree(stuffp->toc);
			stuffp->toc = 0;
		}
		return 0;

	case CDROMEJECT_SW:
		MCDX_TRACE_IOCTL(("CDROMEJECT_SW\n"));

		stuffp->eject_sw = !!arg;
		return 0;

	case CDROMVOLCTRL:
		MCDX_TRACE_IOCTL(("CDROMVOLCTRL\n"));

		if ((ans = verify_area(VERIFY_READ,
				       (void *) arg,
				       sizeof(volctrl))))
			return ans;

		memcpy_fromfs(&volctrl, (char *) arg, sizeof(volctrl));
		/* Adjust for the weirdness of workman. */
		volctrl.channel2 = volctrl.channel1;
		volctrl.channel1 = volctrl.channel3 = 0x00;
		return talk(stuffp, MCDX_CMD_SET_ATTENATOR,
			    &volctrl, sizeof(volctrl),
			    &volctrl, sizeof(volctrl), 2 * HZ);

	default:
		printk(KERN_WARNING MCDX
		       ": unknown ioctl request 0x%04x\n", command);
		return -EINVAL;
	}
}

/*   
 * This does actually the transfer from the drive.
 * Return:      -1 on timeout or other error
 * else status byte (as in stuff->st) 
 * FIXME: the excessive jumping through wait queues degrades the
 * performance significantly.
 */
static int transfer_data(struct s_drive_stuff *stuffp,
			 char *p, int sector, int nr_sectors)
{
	int off;
	int done = 0;

	if (stuffp->valid
	    && (sector >= stuffp->pending)
	    && (sector < stuffp->off_direct)) {
		off = stuffp->off_requested < (off = sector + nr_sectors)
		    ? stuffp->off_requested : off;

		do {
			/* wait for the drive become idle, but first
			 * check for possible occurred errors --- the drive
			 * seems to report them asynchronously
			 */
			current->timeout = jiffies + 5 * HZ;
			while (stuffp->introk && stuffp->busy
			       && current->timeout) {
				interruptible_sleep_on(&stuffp->busyq);
			}

			/* test for possible errors */
			if (current->timeout == 0 || !stuffp->introk) {
				if (current->timeout == 0) {
					printk(KERN_ERR MCDX ": transfer timeout\n");
				} else if (!stuffp->introk) {
					printk(KERN_ERR MCDX
					       ": error via irq in transfer reported\n");
				}

				stuffp->busy = 0;
				stuffp->valid = 0;
				stuffp->introk = 1;
				return -1;
			}
			/* test if it's the first sector of a block,
			 * there we have to skip some bytes as we read raw data 
			 */
			if (stuffp->xa && (0 == (stuffp->pending & 3))) {
				insb(DATA_REG, p,
				     CD_FRAMESIZE_RAW - CD_XA_TAIL - CD_FRAMESIZE);
			}
			/* now actually read the data */
			insb(DATA_REG, p, 512);

			/* test if it's the last sector of a block,
			 * if so, we have to expect an interrupt and to skip 
			 * some data too 
			 */
			if ((stuffp->busy = (3 == (stuffp->pending & 3)))
			    && stuffp->xa) {
				int i;
				for (i = 0; i < CD_XA_TAIL; ++i)
					inb(DATA_REG);
			}
			if (stuffp->pending == sector) {
				p += 512;
				done++;
				sector++;
			}
		} while (++(stuffp->pending) < off);
	} else {
		unsigned char cmd[6];
		stuffp->valid = 1;
		stuffp->pending = sector & ~3;

		/* do some sanity checks */
		if (stuffp->pending > stuffp->lastsector) {
			printk(KERN_ERR MCDX
			": sector %d transfer from nirvana requested.\n",
			       stuffp->pending);
			stuffp->eom = 1;
			stuffp->valid = 0;
			return -1;
		}
		if ((stuffp->off_direct = stuffp->pending + DIRECT_SIZE)
		    > stuffp->lastsector + 1)
			stuffp->off_direct = stuffp->lastsector + 1;
		if ((stuffp->off_requested = stuffp->pending + REQUEST_SIZE)
		    > stuffp->lastsector + 1)
			stuffp->off_requested = stuffp->lastsector + 1;
		{
			unsigned int l = (stuffp->pending / 4)
			+ CD_BLOCK_OFFSET;

			cmd[0] = uint2bcd(l / 4500), l %= 4500;
			/* minute */
			cmd[1] = uint2bcd(l / 75);	/* second */
			cmd[2] = uint2bcd(l % 75);	/* frame */
		}

		stuffp->busy = 1;
		/*
		 * FIXME: What about the ominous frame length?!
		 */
		cmd[3] = ~0;
		cmd[4] = ~0;
		cmd[5] = ~0;

		outb(stuffp->double_speed ? MCDX_CMD_PLAY_2X : MCDX_CMD_PLAY,
		     DATA_REG);
		outsb(DATA_REG, cmd, 6);
	}

	stuffp->off_direct =
	    (stuffp->off_direct += done) < stuffp->off_requested
	    ? stuffp->off_direct
	    : stuffp->off_requested;

	return done;
}

void do_mcdx_request()
{
	int dev;
	struct s_drive_stuff *stuffp;

      again:

	if ((CURRENT == NULL) || (CURRENT->rq_status == RQ_INACTIVE)) {
		return;
	}
	stuffp = mcdx_stuffp[MINOR(CURRENT->rq_dev)];

	INIT_REQUEST;
	dev = MINOR(CURRENT->rq_dev);

	if ((dev < 0) || (dev >= MCDX_NDRIVES) || (!stuffp)) {
		printk(KERN_WARNING MCDX ": bad device requested: %s\n",
		       kdevname(CURRENT->rq_dev));
		end_request(0);
		goto again;
	}
	if (stuffp->audio) {
		printk(KERN_WARNING MCDX ": attempt to read from audio cd\n");
		end_request(0);
		goto again;
	}
	switch (CURRENT->cmd) {
	case WRITE:
		printk(KERN_ERR MCDX ": attempt to write to cd!!\n");
		end_request(0);
		break;

	case READ:
		stuffp->eom = 0;	/* clear end of media flag */
		while (CURRENT->nr_sectors) {
			int i;

			if (-1 == (i = transfer_data(stuffp,
						     CURRENT->buffer,
						     CURRENT->sector,
						 CURRENT->nr_sectors))) {
				if (stuffp->eom) {
					CURRENT->sector += CURRENT->nr_sectors;
					CURRENT->nr_sectors = 0;
				} else {
					/*
					 * FIXME: TRY SOME ERROR RECOVERY HERE!
					 */
				}
				end_request(0);
				goto again;
			}
			CURRENT->sector += i;
			CURRENT->nr_sectors -= i;
			CURRENT->buffer += (i * 512);
		}
		end_request(1);
		break;

	default:
		panic(MCDX "do_request: unknown command.\n");
		break;
	}

	goto again;
}

/*  
 * actions done on open:
 * 1)   get the drives status 
 */
static int mcdx_open(struct inode *ip, struct file *fp)
{
	struct s_drive_stuff *stuffp = mcdx_stuffp[MINOR(ip->i_rdev)];
	int st = 0;
	unsigned long bang;

	MCDX_TRACE(("mcdx_open()\n"));

	if (!stuffp)
		return -ENXIO;

	/* close the door, if necessary (get the door information
	 * from the hardware status register). 
	 * If we can't read the CD after an autoclose
	 * no further autocloses will be tried 
	 */
	if (inb(STAT_REG) & MCDX_RBIT_DOOR) {
		if (stuffp->autoclose && (stuffp->door))
			issue_command(stuffp, MCDX_CMD_CLOSE_DOOR, 10 * HZ);
		else
			return -EIO;
	}
	/*
	 * Check if a disk is in.
	 */ 
	bang = jiffies + 10 * HZ;
	while (jiffies < bang) {
		st = issue_command(stuffp, MCDX_CMD_GET_STATUS, 5 * HZ);
		if (st != -1 && (st & MCDX_RBIT_DISKSET))
			break;
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + 1 * HZ;
		schedule();
	}
	if (st == -1 || (st & MCDX_RBIT_DISKSET) == 0) {
		printk(KERN_INFO MCDX ": no disk in drive\n");
		return -EIO;
	}
	/* if the media changed we will have to do a little more 
	 * FIXME: after removing of the mcdx_requestmultisession() it is showed
	 * that the logics of this may be broken.
	 */
	if (stuffp->xxx) {
		/* but wait - the time of media change will be set at the 
		 * very last of this block.
		 */

		stuffp->audiostatus = CDROM_AUDIO_INVALID;
		stuffp->autoclose = 1;

		/* get the multisession information */

		if (stuffp->multi_cap) {
			int i = 6;	/* number of retries */
			while (i && (-1 == get_command(stuffp,
						 MCDX_CMD_GET_MDISK_INFO,
			&stuffp->multi, sizeof(struct s_multi), 2 * HZ)))
				--i;
			if (!i) {
				stuffp->autoclose = 0;
				/*
				 * No multidisk info
				 */
			}
		} else
			stuffp->multi.multi = 0;

		if (stuffp->autoclose) {
			/* we succeeded, so on next open(2) we could try  
			 * auto close again 
			 */

			/* multisession ? */
			if (!stuffp->multi.multi)
				stuffp->multi.msf_last.second = 2;
		}		/* got multisession information */
		/* request the disks table of contents (aka diskinfo) */
		if (-1 == request_toc_data(stuffp)) {
			stuffp->lastsector = -1;
		} else {
			stuffp->lastsector = (CD_FRAMESIZE / 512)
			    * msf2log(&stuffp->msf_leadout) - 1;
		}

		if (stuffp->toc) {
			kfree(stuffp->toc);
			stuffp->toc = 0;
		}
		if (-1 == config_drive(stuffp))
			return -EIO;

		/* try to get the first sector, iff any ... */
		if (stuffp->lastsector >= 0) {
			char buf[512];
			int ans;
			int tries;

			stuffp->xa = 0;
			stuffp->audio = 0;

			for (tries = 6; tries; tries--) {
				unsigned char c;
				stuffp->introk = 1;

				/* set data mode */
				c = stuffp->xa ? MODE2 : MODE1;
				ans = set_command(stuffp,
						  MCDX_CMD_SET_DATA_MODE,
						  &c, sizeof(c), 5 * HZ);

				if (-1 == ans) {
					/* return -EIO; */
					stuffp->xa = 0;
					break;
				} else if (ans & MCDX_RBIT_AUDIOTR) {
					stuffp->audio = 1;
					break;
				}
				 
				while (0 == (ans = transfer_data(stuffp, buf,
								 0, 1)));

				if (ans == 1)
					break;
				stuffp->xa = !stuffp->xa;
			}
			/* if (!tries) return -EIO; */
		}
		/* xa disks will be read in raw mode, others not */
		if (-1 == set_drive_mode(stuffp, stuffp->xa ? RAW : COOKED))
			return -EIO;
		stuffp->xxx = 0;
	}
	/* lock the door if not already done */
	if (0 == stuffp->users && (-1 == lock_door(stuffp, DOOR_LOCK)))
		return -EIO;

	stuffp->users++;
	MOD_INC_USE_COUNT;
	return 0;
}

static void mcdx_close(struct inode *ip, struct file *fp)
{
	struct s_drive_stuff *stuffp = mcdx_stuffp[MINOR(ip->i_rdev)];

	MCDX_TRACE(("mcdx_close()\n"));

	if (0 == --stuffp->users) {
		sync_dev(ip->i_rdev);	/* needed for r/o device? */

		/* invalidate_inodes(ip->i_rdev); */
		invalidate_buffers(ip->i_rdev);
		lock_door(stuffp, DOOR_UNLOCK);

		/* eject if wished and possible */
		if (stuffp->eject_sw && (stuffp->door)) {
			issue_command(stuffp, MCDX_CMD_EJECT, 5 * HZ);
		}
	}
	MOD_DEC_USE_COUNT;

	return;
}

/*      
 * Return: 1 if media changed since last call to this function
 * 0 otherwise 
 */
static int mcdx_media_change(kdev_t full_dev)
{
	struct s_drive_stuff *stuffp;

	MCDX_TRACE(("mcdx_media_change()\n"));

	/*
	 * FIXME: probably this is unneeded or should be simplified!
	 */
	issue_command(stuffp = mcdx_stuffp[MINOR(full_dev)],
		      MCDX_CMD_GET_STATUS, 5 * HZ);

	return stuffp->xxx;
}

/* Interrupt handler routine.
 * This function is called, when during transfer the end of a physical 2048
 * byte block is reached.
 */
static void mcdx_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct s_drive_stuff *stuffp;
	u_char b;

	if (!(stuffp = mcdx_irq_map[irq])) {
		return;		/* huh? */
	}
	
	/* NOTE: We only should get interrupts if data were requested.
	 * But the drive seems to generate ``asynchronous'' interrupts
	 * on several error conditions too.  (Despite the err int enable
	 * setting during initialisation) 
	 */

	/* get the interrupt status */
	b = inb(STAT_REG);	
	if (!(b & MCDX_RBIT_DTEN)) {
		stuffp->introk = 1;
	} else {
		stuffp->introk = 0;
		if (!(b & MCDX_RBIT_STEN)) {
			printk(KERN_DEBUG MCDX ": irq %d status 0x%02x\n",
			       irq, inb(DATA_REG));
		} else {
			MCDX_TRACE(("irq %d ambiguous hw status\n", irq));
		}
	}
	stuffp->busy = 0;
	wake_up_interruptible(&stuffp->busyq);
}

/*
 * FIXME!
 * This seems to hang badly, when the driver is loaded with inappropriate
 * port/irq settings!
 */
int mcdx_init(void)
{
	int drive;

#ifdef MODULE
	printk(KERN_INFO "Mitsumi driver version " VERSION " for %s\n",
	       kernel_version);
#else
	printk(KERN_INFO "Mitsumi driver version " VERSION "\n");
#endif
	for (drive = 0; drive < MCDX_NDRIVES; drive++) {
		struct {
			u_char code;
			u_char version;
		} firmware;
		int i;
		struct s_drive_stuff *stuffp;
		int size;

		mcdx_blocksizes[drive] = 0;
		mcdx_stuffp[drive] = 0;

		size = sizeof(*stuffp);

		if (!(stuffp = kmalloc(size, GFP_KERNEL))) {
			printk(KERN_ERR MCDX
			       ": malloc of drives data failed!\n");
			break;
		}
		/* set default values */ memset(stuffp, 0, sizeof(*stuffp));
		stuffp->autoclose = 1;	/* close the door on open(2) */

		stuffp->irq = irq(mcdx_drive_map[drive]);
		stuffp->base = port(mcdx_drive_map[drive]);

		/* check if i/o addresses are available */
		if (check_region(stuffp->base, MCDX_IO_SIZE)) {
			printk(KERN_WARNING
			       "Init failed. I/O ports (0x%3x..0x%3x) "
			       "already in use.\n",
			  stuffp->base, stuffp->base + MCDX_IO_SIZE - 1);
			kfree(stuffp);
			continue;	/* next drive */
		}
		/*
		 * Hardware reset.
		 */
		outb(0, CHAN_REG);	/* no dma, no irq -> hardware */
		outb(0, RESET_REG);	/* hw reset */

		i = 10;		/* number of retries */
		while (-1 == get_command(stuffp, MCDX_CMD_GET_FIRMWARE,
				    &firmware, sizeof(firmware), 2 * HZ))
			--i;
		if (!i) {
			/* failed, next drive */
			printk(KERN_WARNING
			"%s=0x%3x,%d: Init failed. Can't get version.\n",
			       MCDX, stuffp->base, stuffp->irq);
			kfree(stuffp);
			continue;
		}
		switch (firmware.code) {
		case 'D':
			stuffp->double_speed = stuffp->door =
			    stuffp->multi_cap = 1;
			break;
		case 'F':
			stuffp->door = stuffp->multi_cap = 1;
			break;
		case 'M':
			break;
		default:
			kfree(stuffp);
		}

		if (!stuffp)
			continue;	/* next drive */

		if (register_blkdev(MAJOR_NR, DEVICE_NAME, &mcdx_fops)) {
			kfree(stuffp);
			continue;	/* next drive */
		}
		blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
		read_ahead[MAJOR_NR] = READ_AHEAD;

		blksize_size[MAJOR_NR] = mcdx_blocksizes;

		mcdx_irq_map[stuffp->irq] = stuffp;
		if (request_irq(stuffp->irq, mcdx_intr,
				SA_INTERRUPT, DEVICE_NAME, NULL)) {
			stuffp->irq = 0;
			kfree(stuffp);
			continue;	/* next drive */
		}
		request_region(stuffp->base, MCDX_IO_SIZE, DEVICE_NAME);

		/* get junk after some delay.
		 */
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + HZ / 2;
		schedule();
		for (i = 100; i; i--)
			(void) inb(STAT_REG);

#if WE_KNOW_WHY
		outb(0x50, CHAN_REG);	/* irq 11 -> channel register */
#endif

		config_drive(stuffp);

		printk(KERN_INFO MCDX "%d: at 0x%3x, irq %d, firmware: %c %x\n",
		       drive, stuffp->base, stuffp->irq,
		       firmware.code, firmware.version);
		mcdx_stuffp[drive] = stuffp;
	}

	return 0;
}

#ifdef MODULE

int init_module(void)
{
	int i;

	mcdx_init();
	for (i = 0; i < MCDX_NDRIVES; i++) {
		if (mcdx_stuffp[i]) {
			register_symtab(0);
			return 0;
		}
	}
	return -EIO;
}

void cleanup_module(void)
{
	int i;

	unregister_blkdev(MAJOR_NR, DEVICE_NAME);

	for (i = 0; i < MCDX_NDRIVES; i++) {
		struct s_drive_stuff *stuffp;
		stuffp = mcdx_stuffp[i];
		if (!stuffp)
			continue;
		release_region(stuffp->base, MCDX_IO_SIZE);
		free_irq(stuffp->irq, NULL);
		if (stuffp->toc) {
			kfree(stuffp->toc);
		}
		mcdx_stuffp[i] = NULL;
		kfree(stuffp);
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
}

#else				/* MODULE */

/*
 * This function is used by the kernel in init/main.c to provide semantics
 * for the corresponding kernel options. It's unused otherwise.
 */
void mcdx_setup(char *str, int *pi)
{
	if (pi[0] > 0)
		mcdx_drive_map[0][0] = pi[1];
	if (pi[0] > 1)
		mcdx_drive_map[0][1] = pi[2];
}
#endif				/* MODULE */
