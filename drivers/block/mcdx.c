/*
 * The Mitsumi CDROM interface
 * Copyright (C) 1995 Heiko Schlittermann
 * VERSION: @VERSION@
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
 *  John Tombs, Bjorn Ekwall (module support)
 *  Daniel v. Mosnenck (he sent me the Technical and Programming Reference)
 *  Gerd Knorr (he lent me his PhotoCD)
 *  Nils Faerber and Roger E. Wolff (extensivly tested the LU portion)
 *  ... somebody forgotten?
 *  
 */


#if RCS
static const char *mcdx_c_version
		= "$Id: mcdx.c,v 1.2 1995/06/18 18:00:53 heiko Exp $";
#endif

#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#ifndef CONFIG_MODVERSIONS
char kernel_version[]=UTS_RELEASE;
#endif
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#define MOD_IN_USE 1
#endif MODULE

#include <linux/errno.h>
#include <linux/signal.h>
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

#define MAJOR_NR MITSUMI_X_CDROM_MAJOR
#include "blk.h"
#define	mcdx_drive_map mcdx /* for compatible parameter passing with "insmod" */
#include <linux/mcdx.h>

/* CONSTANTS *******************************************************/

const int REQUEST_SIZE = 200;
const int DIRECT_SIZE = 200;

enum drivemodes { TOC, DATA, RAW, COOKED };
enum datamodes { MODE0, MODE1, MODE2 };
enum resetmodes { SOFT, HARD };

const int SINGLE = 0x01;
const int DOUBLE = 0x02;
const int DOOR   = 0x04;
const int MULTI  = 0x08;

const unsigned char READSSPEED = 0xc0;
const unsigned char READDSPEED = 0xc1;


/* DECLARATIONS ****************************************************/ 
struct s_msf {
	unsigned char minute;
	unsigned char second;
	unsigned char frame;
};

struct s_subqcode {
	unsigned char control;
	unsigned char tno;
	unsigned char index;
	struct s_msf tt;
	struct s_msf dt;
};

struct s_diskinfo {
	unsigned int n_first;
	unsigned int n_last;
	struct s_msf msf_leadout;
	struct s_msf msf_first;
};
	
struct s_multi {
	unsigned char multi;
	struct s_msf msf_last;
};

struct s_version {
	unsigned char code;
	unsigned char ver;
};

/* Per drive/controller stuff **************************************/

struct s_drive_stuff {
	/* waitquenes */
    struct wait_queue *busyq;
    struct wait_queue *lockq;
    struct wait_queue *sleepq;

 	/* flags */
    volatile int introk;	/* status of last irq operation */
    volatile int busy;		/* drive performs an operation */
    volatile int lock;		/* exclusive usage */

	/* cd infos */
	struct s_diskinfo di;
	struct s_multi multi;
	struct s_subqcode* toc;	/* first enty of the toc array */
	struct s_subqcode start;
	struct s_subqcode stop;
	int xa;					/* 1 if xa disk */
	int audio;				/* 1 if audio disk */
	int audiostatus;			

	/* `buffer' control */
    volatile int valid;
    volatile int pending;
    volatile int off_direct;
    volatile int off_requested;

	/* adds and odds */
	void* wreg_data;	/* w data */
	void* wreg_reset;	/* w hardware reset */
	void* wreg_hcon;	/* w hardware conf */
	void* wreg_chn;		/* w channel */
	void* rreg_data;	/* r data */
	void* rreg_status;	/* r status */

    int irq;			/* irq used by this drive */
    int minor;			/* minor number of this drive */
    int present;	    /* drive present and its capabilities */
    char readcmd;		/* read cmd depends on single/double speed */
    unsigned long changed;	/* last jiff the media was changed */
    int users;				/* keeps track of open/close */
    int lastsector;			/* last block accessible */
    int errno;				/* last operation's error */

};

/* Prototypes ******************************************************/ 

/*	The following prototypes are already declared elsewhere.  They are
 	repeated here to show what's going on.  And to sense, if they're
	changed elsewhere. */

/* declared in blk.h */
unsigned long mcdx_init(unsigned long mem_start, unsigned long mem_end);
void do_mcdx_request(void);

int check_mcdx_media_change(dev_t);

/* already declared in init/main */
void mcdx_setup(char *, int *);

/*	Indirect exported functions. These functions are exported by their
	addresses, such as mcdx_open and mcdx_close in the 
	structure fops. */

/* ???  exported by the mcdx_sigaction struct */
static void mcdx_intr(int, struct pt_regs*);

/* exported by file_ops */
static int mcdx_open(struct inode*, struct file*);
static void mcdx_close(struct inode*, struct file*);
static int mcdx_ioctl(struct inode*, struct file*, unsigned int, unsigned long);

/* misc internal support functions */
static void log2msf(unsigned int, struct s_msf*);
static unsigned int msf2log(const struct s_msf*);
static unsigned int uint2bcd(unsigned int);
static unsigned int bcd2uint(unsigned char);
#if MCDX_DEBUG
static void TRACE((int level, const char* fmt, ...));
#endif
#ifndef NOWARN
static void WARN((const char* fmt, ...));
#endif
static char *port(int*);
static int irq(int*);
static void mcdx_delay(struct s_drive_stuff*, long jifs);
static int mcdx_transfer(struct s_drive_stuff*, char* buf, int sector, int nr_sectors);

static int mcdx_config(struct s_drive_stuff*, int);
static int mcdx_closedoor(struct s_drive_stuff*, int);
static int mcdx_requestversion(struct s_drive_stuff*, struct s_version*, int);
static int mcdx_lockdoor(struct s_drive_stuff*, int, int);
#if 0
static int mcdx_getstatus(struct s_drive_stuff*, int);
#endif
static int mcdx_stop(struct s_drive_stuff*, int);
static int mcdx_reset(struct s_drive_stuff*, enum resetmodes, int);
static int mcdx_eject(struct s_drive_stuff*, int);
static int mcdx_setdrivemode(struct s_drive_stuff*, enum drivemodes, int);
static int mcdx_setdatamode(struct s_drive_stuff*, enum datamodes, int);
static int mcdx_requestsubqcode(struct s_drive_stuff*, struct s_subqcode*, int);
static int mcdx_requestmultidiskinfo(struct s_drive_stuff*, struct s_multi*, int);
static int mcdx_requesttocdata(struct s_drive_stuff*, struct s_diskinfo*, int);

static int mcdx_talk(struct s_drive_stuff*, 
		const unsigned char* cmd, size_t, void *buffer,
		size_t size, unsigned int timeout, int);
static int mcdx_readtoc(struct s_drive_stuff*);

/* static variables ************************************************/

static int dummy0;
static int mcdx_drive_map[][2] = MCDX_DRIVEMAP;
static struct s_drive_stuff* mcdx_stuffp[MCDX_NDRIVES];
static struct s_drive_stuff* mcdx_irq_map[16] =
		{0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0};

static struct file_operations mcdx_fops = {
	NULL,			/* lseek - use kernel default */
	block_read,		/* read - general block-dev read */
	block_write,	/* write - general block-dev write */
	NULL,			/* no readdir */
	NULL,			/* no select */
	mcdx_ioctl,		/* ioctl() */
	NULL,			/* no mmap */
	mcdx_open,		/* open() */
	mcdx_close,		/* close() */
	NULL,			/* fsync */
	NULL,                   /* fasync */
	check_mcdx_media_change, /* media_change */
	NULL                    /* revalidate */
};

/* KERNEL INTERFACE FUNCTIONS **************************************/ 

static int 
mcdx_ioctl(
	struct inode* ip, struct file* fp, 
	unsigned int cmd, unsigned long arg)
{ 
	struct s_drive_stuff *stuffp = mcdx_stuffp[MINOR(ip->i_rdev)];

	if (!stuffp->present) return -ENXIO;
	if (!ip) return -EINVAL;

	switch (cmd) {
		case CDROMSTART: {
			TRACE((IOCTL, "ioctl() START\n"));
			return 0;
		}
		case CDROMSTOP: {
			TRACE((IOCTL, "ioctl() STOP\n"));
			if (-1 == mcdx_stop(stuffp, 1))
				return -EIO;
			return 0;
		}
		case CDROMPLAYTRKIND: {
			return -EINVAL;
			/*
			int ans;
			struct cdrom_ti ti;
			TRACE((IOCTL, "ioctl() PLAYTRKIND\n"));
			if (ans = verify_area(VERIFY_READ, (void*) arg, sizeof(ti));
				return ans;
			memcpy_fromfs(&ti, (void*) arg, sizeof(ti));
			if ((ti.cdti_trk0 < stuffp->di.n_first)
					|| (ti.cdti_trk0 > stuffp->di.n_last)
					|| (ti.cdti_trk1 < stuffp->di.n_first))
				return -EINVAL;
			if (ti.cdti_trk1 > stuffp->di.n_last) ti.cdti_trk1 = stuffp->di.n_last;
			*/
		}

		case CDROMREADTOCENTRY: {
			struct cdrom_tocentry entry;
			struct s_subqcode *tp = NULL;
			int ans;

			TRACE((IOCTL, "ioctl() READTOCENTRY\n"));

			if ((stuffp->toc == NULL) && (0 != (ans = mcdx_readtoc(stuffp)))) return ans;

			if ((ans = verify_area(VERIFY_READ, (void *) arg, sizeof(entry)))) return ans;
			memcpy_fromfs(&entry, (void *) arg, sizeof(entry));

			if (entry.cdte_track == CDROM_LEADOUT) 
				tp = &stuffp->toc[stuffp->di.n_last - stuffp->di.n_first + 1];
			else if (entry.cdte_track > stuffp->di.n_last 
					|| entry.cdte_track < stuffp->di.n_first) return -EINVAL;
			else tp = &stuffp->toc[entry.cdte_track - stuffp->di.n_first];

			if (NULL == tp) WARN(("FATAL.\n"));

			entry.cdte_adr = tp->control;
			entry.cdte_ctrl = tp->control >> 4;

			if (entry.cdte_format == CDROM_MSF) {
				entry.cdte_addr.msf.minute = bcd2uint(tp->dt.minute);
				entry.cdte_addr.msf.second = bcd2uint(tp->dt.second);
				entry.cdte_addr.msf.frame = bcd2uint(tp->dt.frame);
			} else if (entry.cdte_format == CDROM_LBA)
				entry.cdte_addr.lba = msf2log(&tp->dt);
			else return -EINVAL;

			if ((ans = verify_area(VERIFY_WRITE, (void*) arg, sizeof(entry)))) return ans;
			memcpy_tofs((void*) arg, &entry, sizeof(entry));

			return 0;
		}

		case CDROMSUBCHNL: {
			int ans;
			struct cdrom_subchnl sub;
			struct s_subqcode q;
			TRACE((IOCTL, "ioctl() SUBCHNL\n"));

			if ((ans = verify_area(VERIFY_READ, (void*) arg, sizeof(sub)))) return ans;
			memcpy_fromfs(&sub, (void*) arg, sizeof(sub));

			if (-1 == mcdx_requestsubqcode(stuffp, &q, 1)) return -EIO;

            TRACE((IOCTL, "audiostatus: %x\n", stuffp->audiostatus));
			sub.cdsc_audiostatus = stuffp->audiostatus;
			sub.cdsc_adr = q.control;
			sub.cdsc_ctrl = q.control >> 4;
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
			} else return -EINVAL;

			if ((ans = verify_area(VERIFY_WRITE, (void*) arg, sizeof(sub))))
				return ans;
			memcpy_tofs((void*) arg, &sub, sizeof(sub));

			return 0;
		}

		case CDROMREADTOCHDR: {
			struct cdrom_tochdr toc;
			int ans;
			TRACE((IOCTL, "ioctl() READTOCHDR\n"));
			if ((ans = verify_area(VERIFY_WRITE, (void*) arg, sizeof toc)))
				return ans;
			toc.cdth_trk0 = stuffp->di.n_first;
			toc.cdth_trk1 = stuffp->di.n_last;
			memcpy_tofs((void*) arg, &toc, sizeof toc);
			TRACE((IOCTL, "ioctl() track0 = %d, track1 = %d\n",
					stuffp->di.n_first, stuffp->di.n_last));
			return 0;
		}

		case CDROMPAUSE: {
			TRACE((IOCTL, "ioctl() PAUSE\n"));
			if (stuffp->audiostatus != CDROM_AUDIO_PLAY) return -EINVAL;
			if (-1 == mcdx_stop(stuffp, 1)) return -EIO;
			if (-1 == mcdx_requestsubqcode(stuffp, &stuffp->start, 1))
				return -EIO;
			return 0;
		}
		case CDROMMULTISESSION: {
			int ans;
			struct cdrom_multisession ms;
			TRACE((IOCTL, "ioctl() MULTISESSION\n"));
			if (0 != (ans = verify_area(VERIFY_READ, (void*) arg, 
					sizeof(struct cdrom_multisession))))
				return ans;
				
			memcpy_fromfs(&ms, (void*) arg, sizeof(struct cdrom_multisession));
			if (ms.addr_format == CDROM_MSF) {
				ms.addr.msf.minute = bcd2uint(stuffp->multi.msf_last.minute);
				ms.addr.msf.second = bcd2uint(stuffp->multi.msf_last.second);
				ms.addr.msf.frame = bcd2uint(stuffp->multi.msf_last.frame);
			} else if (ms.addr_format == CDROM_LBA)
				ms.addr.lba = msf2log(&stuffp->multi.msf_last);
			else
				return -EINVAL;
			ms.xa_flag = stuffp->xa;

			if (0 != (ans = verify_area(VERIFY_WRITE, (void*) arg,
					sizeof(struct cdrom_multisession))))
				return ans;

			memcpy_tofs((void*) arg, &ms, sizeof(struct cdrom_multisession));
			if (ms.addr_format == CDROM_MSF) 
				TRACE((IOCTL, 
						"ioctl() (%d, %02x:%02x.%02x [%02x:%02x.%02x])\n",
						ms.xa_flag, 
						ms.addr.msf.minute,
						ms.addr.msf.second,
						ms.addr.msf.frame,
						stuffp->multi.msf_last.minute,
						stuffp->multi.msf_last.second,
						stuffp->multi.msf_last.frame));
			else
			  {
			    dummy0=0;
			    TRACE((IOCTL, 
					"ioctl() (%d, 0x%08x [%02x:%02x.%02x])\n",
					ms.xa_flag,
					ms.addr.lba,
					stuffp->multi.msf_last.minute,
					stuffp->multi.msf_last.second,
					stuffp->multi.msf_last.frame));
			  }
			return 0;
		}

		case CDROMEJECT: {
			TRACE((IOCTL, "ioctl() EJECT\n"));
			if (stuffp->users > 1) return -EBUSY;
			if (-1 == mcdx_eject(stuffp, 1)) return -EIO;
			return 0;
		}

		default:
			WARN(("ioctl(): unknown request 0x%04x\n", cmd));
	    	return -EINVAL;
	}
}

void do_mcdx_request()
{
    int dev;
    struct s_drive_stuff *stuffp;

  again:

	TRACE((REQUEST, "do_request()\n"));

	if ((CURRENT == NULL) || (CURRENT->dev < 0))  {
		TRACE((REQUEST, "do_request() done\n"));
		return;
	}

    stuffp = mcdx_stuffp[MINOR(CURRENT->dev)];
	TRACE((REQUEST, "do_request() stuffp = %p\n", stuffp));

    INIT_REQUEST;
    dev = MINOR(CURRENT->dev);

	if ((dev < 0) || (dev >= MCDX_NDRIVES) || (!stuffp->present)) {
		printk(MCD "do_request(): bad device: 0x%04x\n", CURRENT->dev);
		end_request(0);
		goto again;
    }

	if (stuffp->audio) {
		WARN(("do_request() attempt to read from audio cd\n"));
		end_request(0);
		goto again;
	}

    switch (CURRENT->cmd) {
      case WRITE:
	  printk(MCD ": do_request(): attempt to write to cd!!\n");
	  end_request(0);
	  break;

      case READ:
	  stuffp->errno = 0;
	  while (CURRENT->nr_sectors) {
	      int i;

	      if (-1 == (i = mcdx_transfer(
				      stuffp,
				      CURRENT->buffer,
				      CURRENT->sector,
				      CURRENT->nr_sectors))) {
		  printk(MCD " read error\n");
		  if (stuffp->errno == MCDX_EOM) {
		      CURRENT->sector += CURRENT->nr_sectors;
		      CURRENT->nr_sectors = 0;
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
	  panic(MCD "do_request: unknown command.\n");
	  break;
    }

    goto again;
}

static int 
mcdx_open(struct inode *ip, struct file *fp)
{
    struct s_drive_stuff *stuffp;
	static unsigned long changed = 0;

	TRACE((OPENCLOSE, "open()\n"));
    stuffp = mcdx_stuffp[MINOR(ip->i_rdev)];
    if (!stuffp->present) return -ENXIO;

	/* close the door, if necessary (get the door information
    from the hardware status register) */
	if (inb((unsigned int) stuffp->rreg_status) & MCDX_RBIT_DOOR)  
		mcdx_closedoor(stuffp, 1);

	/* if the media changed we will have to little more */
	if (changed < stuffp->changed) {

		TRACE((OPENCLOSE, "open() media changed\n"));
        /* but wait - the time of media change will be set at the 
        very last of this block - it seems, some of the following
        talk() will detect a media change ... (I think, config()
        is the reason. */

		/* get the multisession information */
		{
			TRACE((OPENCLOSE, "open() Request multisession info\n"));
			if (-1 == mcdx_requestmultidiskinfo(stuffp, &stuffp->multi, 6))
				return -EIO;
		
			if (stuffp->multi.multi > 2)
				WARN(("open() unknown multisession value (%d)\n", stuffp->multi.multi));

			/* multisession ? */
			if (!stuffp->multi.multi)
				stuffp->multi.msf_last.second = 2;

			TRACE((OPENCLOSE, "open() MS: %d, last @ %02x:%02x.%02x\n",
					stuffp->multi.multi,
					stuffp->multi.msf_last.minute,
					stuffp->multi.msf_last.second,
					stuffp->multi.msf_last.frame));
		} /* got multisession information */


		/* request the disks table of contents (aka diskinfo) */
		if (-1 == mcdx_requesttocdata(stuffp, &stuffp->di, 1)) return -EIO;

		stuffp->lastsector = (CD_FRAMESIZE / 512) * msf2log(&stuffp->di.msf_leadout) - 1;

		TRACE((OPENCLOSE, "open() start %d (%02x:%02x.%02x) %d\n",
				stuffp->di.n_first,
				stuffp->di.msf_first.minute,
				stuffp->di.msf_first.second,
				stuffp->di.msf_first.frame,
				msf2log(&stuffp->di.msf_first)));
		TRACE((OPENCLOSE, "open() last %d (%02x:%02x.%02x) %d\n",
				stuffp->di.n_last,
				stuffp->di.msf_leadout.minute,
				stuffp->di.msf_leadout.second,
				stuffp->di.msf_leadout.frame,
				msf2log(&stuffp->di.msf_leadout)));

		if (stuffp->toc) {
			TRACE((MALLOC, "open() free toc @ %p\n", stuffp->toc));
			kfree(stuffp->toc);
		}
		stuffp->toc = NULL;

		TRACE((OPENCLOSE, "open() init irq generation\n"));
		if (-1 == mcdx_config(stuffp, 1)) return -EIO;

		/* try to get the first sector ... */
		{
			char buf[512];
			int ans;
			int tries;

			stuffp->xa = 0;
			stuffp->audio = 0;

			for (tries = 6; tries; tries--) {
				TRACE((OPENCLOSE, "open() try as %s\n",
					stuffp->xa ? "XA" : "normal"));

				/* set data mode */
				if (-1 == (ans = mcdx_setdatamode(stuffp, 
						stuffp->xa ? MODE2 : MODE1, 1)))
					return -EIO;

				if ((stuffp->audio = e_audio(ans))) break; 

				while (0 == (ans = mcdx_transfer(stuffp, buf, 0, 1))) 
					;

				if (ans == 1) break;
				stuffp->xa = !stuffp->xa; 
			}
			if (!tries) return -EIO;
		}

		/* xa disks will be read in raw mode, others not */
		if (-1 == mcdx_setdrivemode(stuffp, 
				stuffp->xa ? RAW : COOKED, 1))
			return -EIO;

		if (stuffp->audio) {
			WARN(("open() audio disk found\n"));
		} else {
			WARN(("open() %s%s disk found\n",
					stuffp->xa ? "XA / " : "",
					stuffp->multi.multi ? "Multi Session" : "Single Session"));
		}

        changed = stuffp->changed;
	}

	if (-1 == mcdx_lockdoor(stuffp, 1, 1)) return -EIO;

    stuffp->users++;
    MOD_INC_USE_COUNT;
    return 0;
}

static void 
mcdx_close(struct inode *ip, struct file *fp)
{
    struct s_drive_stuff *stuffp;

    TRACE((OPENCLOSE, "close()\n"));

    stuffp = mcdx_stuffp[MINOR(ip->i_rdev)];

    if (0 == --stuffp->users) {
		sync_dev(ip->i_rdev);	/* needed for r/o device? */

		/* invalidate_inodes(ip->i_rdev); */
		invalidate_buffers(ip->i_rdev);

		if (-1 == mcdx_lockdoor(stuffp, 0, 1))
				printk(MCD ": Cannot unlock the door\n");
    }
    MOD_DEC_USE_COUNT;

    return;
}

int check_mcdx_media_change(dev_t full_dev)
/*	Return: 1 if media changed since last call to 
			  this function
			0 else
	Setting flag to 0 resets the changed state. */

{
    printk(MCD ":: check_mcdx_media_change(0x%x) called\n", full_dev);
    return 0;
}

void mcdx_setup(char *str, int *pi)
{
#if MCDX_DEBUG
    printk(MCD ":: setup(%s, %d) called\n",
	    str, pi[0]);
#endif
}

/* DIRTY PART ******************************************************/ 

static void mcdx_delay(struct s_drive_stuff *stuff, long jifs)
/*	This routine is used for sleeping while initialisation - it seems that
 	there are no other means available. May be we could use a simple count
 	loop w/ jumps to itself, but I wanna make this independend of cpu
 	speed.  */
{
    unsigned long tout = jiffies + jifs;

    if (jifs < 0) return;

#ifdef MODULE
	/*	timer are available */
    current->timeout = tout;
    while (current->timeout)
		interruptible_sleep_on(&stuff->sleepq);
#else
	/* timer are _not_ available at initialisation time */
    if (stuff->present) {
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = tout;
		interruptible_sleep_on(&stuff->sleepq);
    } else
		while (jiffies < tout) {
	    current->timeout = jiffies;
	    schedule();
	}
#endif MODULE
}

static void 
mcdx_intr(int irq, struct pt_regs* regs)
{
    struct s_drive_stuff *stuffp;
	unsigned char x;

    stuffp = mcdx_irq_map[irq];

    if (!stuffp->busy) {
		WARN(("intr() unexpected interrupt @ irq %d\n", irq));
		return;
    }

	/* if not ok read the next byte as the drives status */
	if (0 == (stuffp->introk = 
			(~(x = inb((unsigned int) stuffp->rreg_status)) & MCDX_RBIT_DTEN))) 
		TRACE((IRQ, "intr() irq %d failed, status %02x %02x\n",
				irq, x, inb((unsigned int) stuffp->rreg_data)));
	else
	  {
	    dummy0=0;
	    TRACE((IRQ, "irq() irq %d ok, status %02x\n", irq, x));
	  }
    stuffp->busy = 0;
    wake_up_interruptible(&stuffp->busyq);
}


static int 
mcdx_talk (
		struct s_drive_stuff *stuffp, 
		const unsigned char *cmd, size_t cmdlen,
		void *buffer, size_t size, 
		unsigned int timeout, int tries)
/* Send a command to the drive, wait for the result.
 * returns -1 on timeout, drive status otherwise
 */
{
    const DELAY = 1;				/* minimum delay */
	char c;
	int st;

	if (!buffer || size == 0) buffer = &c, size = 1;

    while (stuffp->lock)
		interruptible_sleep_on(&stuffp->lockq);

    stuffp->lock = 1;
    stuffp->valid = 0;	

#if MCDX_DEBUG & TALK
	{ 
		unsigned char i;
		TRACE((TALK, "talk() res.size %d, command 0x%02x", 
				size, (unsigned char) cmd[0]));
		for (i = 1; i < cmdlen; i++) printk(" 0x%02x", cmd[i]);
		printk("\n");
	}
#endif

	for (st = -1; st == -1 && tries; tries--) {

		int sz = size;
		unsigned char* bp = buffer;

		outsb((unsigned int) stuffp->wreg_data, cmd, cmdlen);

		while (sz--) {
			int to = 0;

			/* wait for the status bit */
			{
				unsigned long limit = jiffies + timeout;
				while ((inb((unsigned int) stuffp->rreg_status)) & MCDX_RBIT_STEN) {
					if ((to = (jiffies > limit))) break;
					mcdx_delay(stuffp, DELAY);
				}
			}

			if (!to) {

				/* status read? */
				if (st == -1) {

					/* read the status byte */
					*bp++ = st = (unsigned char) inb((unsigned int) stuffp->rreg_data);

					TRACE((TALK, "talk() got status 0x%02x\n", st));

					/* audio status is handled here */
					stuffp->audiostatus = e_audiobusy(st) ? 
							CDROM_AUDIO_PLAY : CDROM_AUDIO_NO_STATUS;

					/* media change is handled here ... */
					if (e_changed(st)) {
						WARN(("talk() media changed\n"));
						stuffp->changed = jiffies;
					}

					/* command error is handled here ... */
					if (e_cmderr(st)) {
						WARN(("command error\n"));
						st = -1;
						break;
					}
				} else {
					/* read the answer */
					 *bp++ = (unsigned char) inb((unsigned int) stuffp->rreg_data); 
				}
			} else {
				WARN(("talk() timed out, %1d %s left\n", 
					tries - 1, (tries -1 ) > 1 ? "tries" : "try"));
				st = -1;
				break;
			}
		}
	}

	if (tries) WARN(("talk() giving up\n"));


    stuffp->lock = 0;
    wake_up_interruptible(&stuffp->lockq);

	TRACE((TALK, "talk() done with 0x%02x\n", st));
    return st;
}

/* MODULE STUFF ***********************************************************/
#ifdef MODULE

int init_module(void)
{
	int i;
	int drives = 0;

	mcdx_init(0, 0);
	for (i = 0; i < MCDX_NDRIVES; i++)  {
		if (mcdx_stuffp[i]) {
		TRACE((INIT, "init_module() drive %d stuff @ %p\n",
				i, mcdx_stuffp[i]));
			drives++;
		}
	}

    if (!drives) {
		WARN(("init_module() cannot init any drive\n"));
		return -EIO;
    }

    return 0;
}

void cleanup_module(void)
{
    int i;

	WARN(("cleanup_module called\n"));
	
    for (i = 0; i < MCDX_NDRIVES; i++) {
		struct s_drive_stuff *stuffp;
		stuffp = mcdx_stuffp[i];
		if (!stuffp) continue;
		release_region((unsigned long) stuffp->wreg_data, MCDX_IO_SIZE);
		free_irq(stuffp->irq);
		if (stuffp->toc) {
			TRACE((MALLOC, "cleanup_module() free toc @ %p\n", stuffp->toc));
			kfree(stuffp->toc);
		}
		TRACE((MALLOC, "cleanup_module() free stuffp @ %p\n", stuffp));
		mcdx_stuffp[i] = NULL;
		kfree(stuffp);
    }

    if (unregister_blkdev(MAJOR_NR, MCD) != 0) 
		printk(MCD ": cleanup_module failed.\n");
    else 
		printk(MCD ": cleanup_module succeeded.\n");
}

#endif MODULE

/* Support functions ************************************************/

#if MCDX_DEBUG
void trace(int level, const char* fmt, ...)
{
	char s[255];
	va_list args;
	if (level < 1) return;
	va_start(args, fmt);
	if (sizeof(s) < vsprintf(s, fmt, args))
		printk(MCD ":: dprintf exeeds limit!!\n");
	else printk(MCD ":: %s", s);
	va_end(args);
}
#endif

#ifndef NOWARN
void warn(const char* fmt, ...)
{
	char s[255];
	va_list args;
	va_start(args, fmt);
	if (sizeof(s) < vsprintf(s, fmt, args))
		printk(MCD ":: dprintf exeeds limit!!\n");
	else printk(MCD ": %s", s);
	va_end(args);
}
#endif


unsigned long mcdx_init(unsigned long mem_start, unsigned long mem_end)
{
	int drive;

	printk(MCD 
			": Version @VERSION@ "
			"$Id: mcdx.c,v 1.2 1995/06/18 18:00:53 heiko Exp $ \n");

	/* zero the pointer array */
	for (drive = 0; drive < MCDX_NDRIVES; drive++)
		mcdx_stuffp[drive] = NULL;

	/* do the initialisation */
	for (drive = 0; drive < MCDX_NDRIVES; drive++) { 
		struct s_version version;
		struct s_drive_stuff* stuffp;
		
		TRACE((INIT, "init() try drive %d\n", drive));
		TRACE((MALLOC, "init() malloc %d bytes\n", sizeof(*stuffp)));

		if (!(stuffp = kmalloc(sizeof(*stuffp), GFP_KERNEL))) {
			WARN(("init() malloc failed\n"));
			break; 
		}

		TRACE((INIT, "init() got %d bytes for drive stuff @ %p\n", sizeof(*stuffp), stuffp));

		/* zero the stuff */
		memset(stuffp, 0, sizeof(*stuffp));

		stuffp->present = 0;		/* this should be 0 already */
		stuffp->toc = NULL;			/* this should be NULL already */
		stuffp->changed = jiffies;

		/* setup our irq and i/o addresses */
		stuffp->irq = irq(mcdx_drive_map[drive]);
		stuffp->wreg_data = stuffp->rreg_data = port(mcdx_drive_map[drive]);
		stuffp->wreg_reset = stuffp->rreg_status = stuffp->wreg_data + 1;
		stuffp->wreg_hcon = stuffp->wreg_reset + 1;
		stuffp->wreg_chn = stuffp->wreg_hcon + 1;

		/* check if i/o addresses are available */
		if (0 != check_region((unsigned int) stuffp->wreg_data, MCDX_IO_SIZE)) {
			WARN(("init() i/o ports at 0x%3p .. 0x%3p not available\n",
					stuffp->wreg_data, stuffp->wreg_data + MCDX_IO_SIZE - 1));
					stuffp->wreg_data = 0;
			TRACE((MALLOC, "init() free stuffp @ %p\n", stuffp));
			kfree(stuffp);
			TRACE((INIT, "init() continue at next drive\n"));
			continue; /* next drive */
		}

		TRACE((INIT, "init() i/o port is available at 0x%3p\n", stuffp->wreg_data));

		TRACE((INIT, "init() hardware reset\n"));
		mcdx_reset(stuffp, HARD, 1);

		TRACE((INIT, "init() get version\n"));
		if (-1 == mcdx_requestversion(stuffp, &version, 2)) {
			/* failed, next drive */
			TRACE((MALLOC, "init() free stuffp @ %p\n", stuffp));
			kfree(stuffp);
			TRACE((INIT, "init() continue at next drive\n"));
			continue;
		}
		switch (version.code) {
		case 'D': 
                stuffp->readcmd = READDSPEED; 
                stuffp->present = DOUBLE | DOOR | MULTI; 
                break;
		case 'F': 
                stuffp->readcmd = READSSPEED; 
                stuffp->present = SINGLE | DOOR | MULTI;
                break;
		case 'M': 
                stuffp->readcmd = READSSPEED;
                stuffp->present = SINGLE;
                break;
		default: 
                stuffp->present = 0; break;
		}


		if (!stuffp->present) {
			printk(MCD ": no drive found. continue at next drive\n");
			kfree(stuffp);
			continue; /* next drive */
		}

		TRACE((INIT, "init() register blkdev\n"));
		if (register_blkdev(MAJOR_NR, MCD, &mcdx_fops) != 0) {
			printk(MCD ": unable to get major %d for " DEVICE_NAME "\n",
					MAJOR_NR);
			kfree(stuffp);
			continue; /* next drive */
		}

		blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
		read_ahead[MAJOR_NR] = READ_AHEAD;

#if WE_KNOW_WHY
		 blksize_size[MAJOR_NR] = BLKSIZES;
#endif

		TRACE((INIT, "init() subscribe irq and i/o\n"));
		mcdx_irq_map[stuffp->irq] = stuffp;
		if (request_irq(stuffp->irq, mcdx_intr, SA_INTERRUPT, MCD)) {
			printk(MCD ": init() unable to get IRQ %d for "
					DEVICE_NAME "\n", stuffp->irq);
			stuffp->irq = 0;
			kfree(stuffp);
			continue;
		}
		request_region((unsigned long) (unsigned int) stuffp->wreg_data, MCDX_IO_SIZE, MCD); 

		TRACE((INIT, "init() get garbage\n"));
		{
			int i;
			mcdx_delay(stuffp, 50);
			for (i = 100; i; i--) (void) inb((unsigned int) stuffp->rreg_status);
		}


#if WE_KNOW_WHY
			outb(0x50, (unsigned int) stuffp->wreg_chn);	/* irq 11 -> channel register */
#endif

		TRACE((INIT, "init() set non dma but irq mode\n"));
		mcdx_config(stuffp, 1);

		stuffp->minor = drive;

		printk(MCD ": " DEVICE_NAME " installed at 0x%3p, irq %d.\n"
			   MCD ": Firmware version %c %x\n",
			   stuffp->wreg_data, stuffp->irq, version.code, version.ver);
		mcdx_stuffp[drive] = stuffp;
		TRACE((INIT, "init() mcdx_stuffp[%d] = %p\n", drive, stuffp));
	}

	return mem_start;
}


static int mcdx_transfer(struct s_drive_stuff *stuffp,
		char *p, int sector, int nr_sectors)
/*	This does actually the transfer from the drive.
	Return:	-1 on timeout or other error
			else status byte (as in stuff->st) */
{
    int off;
    int done = 0;

	TRACE((TRANSFER, "transfer() %d sectors at sector %d\n",
			nr_sectors, sector));

	if (stuffp->audio) {
		printk(MCD ": attempt to read from audio disk\n");
		return -1;
	}

    while (stuffp->lock)
		interruptible_sleep_on(&stuffp->lockq);

    if (stuffp->valid
			&& (sector >= stuffp->pending)
			&& (sector < stuffp->off_direct)) {


	off = stuffp->off_requested < (off = sector + nr_sectors)
			? stuffp->off_requested : off;

	stuffp->lock = current->pid;

	do {
	    int sig = 0;
	    int to = 0;

		/* wait for the drive become idle */
	    current->timeout = jiffies + 500;
	    while (stuffp->busy) {
			interruptible_sleep_on(&stuffp->busyq);
			if ((sig = (current->signal && ~current->blocked))
					|| (to = (current->timeout == 0))) {
				break;
			} 	
		}

	    current->timeout = 0;

		/* test for possible errors */
	    if (((stuffp->busy == 0) && !stuffp->introk)
				|| sig
				|| to) {
			if ((stuffp->busy == 0) && !stuffp->introk)
				printk(MCD ": failure in data request\n");
			else if (to)
				printk(MCD ": timeout\n");
			else if (sig)
				printk(MCD ": got signal 0x%lx\n", current->signal);

			stuffp->lock = 0;
			stuffp->busy = 0;
			wake_up_interruptible(&stuffp->lockq);
			wake_up_interruptible(&stuffp->busyq);
			stuffp->errno = MCDX_E;
			TRACE((TRANSFER, "transfer() done (-1)\n"));
			return -1;
	    }

		/* test if it's the first sector of a block,
		 * there we have to skip some bytes as we read raw data */
		if (stuffp->xa && (0 == (stuffp->pending & 3))) {
			const int HEAD = CD_FRAMESIZE_RAW - CD_XA_TAIL - CD_FRAMESIZE;
			TRACE((TRANSFER, "transfer() sector %d, skip %d header bytes\n",
				stuffp->pending, HEAD));
			insb((unsigned int) stuffp->rreg_data, p, HEAD);
		}

		/* now actually read the data */

		TRACE((TRANSFER, "transfer() read sector %d\n", stuffp->pending));
	    insb((unsigned int) stuffp->rreg_data, p, 512); 

		/* test if it's the last sector of a block,
		 * if so, we have to expect an interrupt and to skip some
		 * data too */
		if ((stuffp->busy = (3 == (stuffp->pending & 3))) && stuffp->xa) {
			char dummy[CD_XA_TAIL];
			TRACE((TRANSFER, "transfer() sector %d, skip %d trailer bytes\n",
					stuffp->pending, CD_XA_TAIL));
			insb((unsigned int) stuffp->rreg_data, &dummy[0], CD_XA_TAIL);
		}

	    if (stuffp->pending == sector) {
			p += 512;
			done++;
			sector++;
	    }
	}
	while (++(stuffp->pending) < off);

	stuffp->lock = 0;
	wake_up_interruptible(&stuffp->lockq);

    } else {

		static unsigned char cmd[] = {
			0,
			0, 0, 0,
			0, 0, 0
		};

		cmd[0] = stuffp->readcmd;

		stuffp->valid = 1;
		stuffp->pending = sector & ~3;

		/* do some sanity checks */
		TRACE((TRANSFER, "transfer() request sector %d\n", stuffp->pending));
		if (stuffp->pending > stuffp->lastsector) {
			printk(MCD ": transfer() sector %d from nirvana requested.\n",
				stuffp->pending);
			stuffp->errno = MCDX_EOM;
			TRACE((TRANSFER, "transfer() done (-1)\n"));
			return -1;
		}

		if ((stuffp->off_direct = stuffp->pending + DIRECT_SIZE)
			> stuffp->lastsector + 1)
			stuffp->off_direct = stuffp->lastsector + 1;
		if ((stuffp->off_requested = stuffp->pending + REQUEST_SIZE)
			> stuffp->lastsector + 1)
			stuffp->off_requested = stuffp->lastsector + 1;

		TRACE((TRANSFER, "transfer() pending %d\n", stuffp->pending));
		TRACE((TRANSFER, "transfer() off_dir %d\n", stuffp->off_direct));
		TRACE((TRANSFER, "transfer() off_req %d\n", stuffp->off_requested));

		{
			struct s_msf pending;
			log2msf(stuffp->pending / 4, &pending);
			cmd[1] = pending.minute;
			cmd[2] = pending.second;
			cmd[3] = pending.frame;
		}

		stuffp->busy = 1;
		cmd[6] = (unsigned char) (stuffp->off_requested - stuffp->pending) / 4;

		outsb((unsigned int) stuffp->wreg_data, cmd, sizeof cmd);

    }

    stuffp->off_direct = (stuffp->off_direct += done) < stuffp->off_requested
	    ? stuffp->off_direct : stuffp->off_requested;

	TRACE((TRANSFER, "transfer() done (%d)\n", done));
    return done;
}


/*	Access to elements of the mcdx_drive_map members */

static char* port(int *ip) { return (char*) ip[0]; }
static int irq(int *ip) { return ip[1]; }

/*	Misc number converters */

static unsigned int bcd2uint(unsigned char c)
{ return (c >> 4) * 10 + (c & 0x0f); }

static unsigned int uint2bcd(unsigned int ival)
{ return ((ival / 10) << 4) | (ival % 10); }

static void log2msf(unsigned int l, struct s_msf* pmsf)
{
    l += CD_BLOCK_OFFSET;
    pmsf->minute = uint2bcd(l / 4500), l %= 4500;
    pmsf->second = uint2bcd(l / 75);
    pmsf->frame = uint2bcd(l % 75);
}

static unsigned int msf2log(const struct s_msf* pmsf)
{
    return bcd2uint(pmsf->frame)
    + bcd2uint(pmsf->second) * 75
    + bcd2uint(pmsf->minute) * 4500
    - CD_BLOCK_OFFSET;
}
	
int mcdx_readtoc(struct s_drive_stuff* stuffp)
{

	TRACE((IOCTL, "ioctl() readtoc for %d tracks\n",
			stuffp->di.n_last - stuffp->di.n_first + 1));

#if MCDX_DEBUG && IOCTL
	if (stuffp->toc) {
		TRACE((IOCTL, "ioctl() toc already read\n"));
		return 0;
	}
#endif

	TRACE((IOCTL, "ioctl() tocmode\n"));
	if (-1 == mcdx_setdrivemode(stuffp, TOC, 1)) return -EIO;

	/* all seems to be ok so far ... malloc */
	{
		int size;
		size = sizeof(struct s_subqcode) * (stuffp->di.n_last - stuffp->di.n_first + 2);

		TRACE((MALLOC, "ioctl() malloc %d bytes\n", size));
		stuffp->toc = kmalloc(size, GFP_KERNEL);
		if (!stuffp->toc) {
			WARN(("Cannot malloc %s bytes for toc\n", size));
			mcdx_setdrivemode(stuffp, DATA, 1);
			return -EIO;
		}
	}

	/* now read actually the index */
	{
		int trk;
		int retries;

		for (trk = 0; 
				trk < (stuffp->di.n_last - stuffp->di.n_first + 1); 
				trk++)
			stuffp->toc[trk].index = 0;

		for (retries = 300; retries; retries--) { /* why 300? */
			struct s_subqcode q;
			unsigned int idx;
		
			if (-1 == mcdx_requestsubqcode(stuffp, &q, 1)) {
				mcdx_setdrivemode(stuffp, DATA, 1);
				return -EIO;
			}

			idx = bcd2uint(q.index);

			if ((idx > 0) 
					&& (idx <= stuffp->di.n_last) 
					&& (q.tno == 0)
					&& (stuffp->toc[idx - stuffp->di.n_first].index == 0)) {
				stuffp->toc[idx - stuffp->di.n_first] = q;
				trk--;
			}
			if (trk == 0) break;
		}
		memset(&stuffp->toc[stuffp->di.n_last - stuffp->di.n_first + 1], 
				0, sizeof(stuffp->toc[0]));
		stuffp->toc[stuffp->di.n_last - stuffp->di.n_first + 1].dt
				= stuffp->di.msf_leadout;
	}

	/* unset toc mode */
	TRACE((IOCTL, "ioctl() undo toc mode\n"));
	if (-1 == mcdx_setdrivemode(stuffp, DATA, 2))
		return -EIO;

#if MCDX_DEBUG && IOCTL
	{ int trk;
	for (trk = 0; 
			trk < (stuffp->di.n_last - stuffp->di.n_first + 2); 
			trk++)
		TRACE((IOCTL, "ioctl() %d readtoc %02x %02x %02x"
				"  %02x:%02x.%02x  %02x:%02x.%02x\n",
				trk + stuffp->di.n_first,
				stuffp->toc[trk].control, stuffp->toc[trk].tno, stuffp->toc[trk].index,
				stuffp->toc[trk].tt.minute, stuffp->toc[trk].tt.second, stuffp->toc[trk].tt.frame,
				stuffp->toc[trk].dt.minute, stuffp->toc[trk].dt.second, stuffp->toc[trk].dt.frame));
	}
#endif


	return 0;
}

/* Drive functions ************************************************/

static int 
mcdx_closedoor(struct s_drive_stuff *stuffp, int tries)
{
	if (stuffp->present & DOOR)
		return mcdx_talk(stuffp, "\xf8", 1, NULL, 0, 500, tries);
	else
		return 0;
}

static int 
mcdx_stop(struct s_drive_stuff *stuffp, int tries)
{ return mcdx_talk(stuffp, "\xf0", 1, NULL, 0, 200, tries); }

static int
mcdx_eject(struct s_drive_stuff *stuffp, int tries)
{
	if (stuffp->present & DOOR)
		return mcdx_talk(stuffp, "\xf6", 1, NULL, 0, 500, tries);
	else
		return 0;
}

static int
mcdx_requestsubqcode(struct s_drive_stuff *stuffp, struct s_subqcode *sub, int tries)
{
	char buf[11];
	int ans;
	ans = mcdx_talk(stuffp, "\x20", 1, buf, sizeof(buf), 200, tries);
	sub->control = buf[1];
	sub->tno = buf[2];
	sub->index = buf[3];
	sub->tt.minute = buf[4];
	sub->tt.second = buf[5];
	sub->tt.frame = buf[6];
	sub->dt.minute = buf[8];
	sub->dt.second = buf[9];
	sub->dt.frame = buf[10];
	return ans;
}

static int
mcdx_requestmultidiskinfo(struct s_drive_stuff *stuffp, struct s_multi *multi, int tries)
{
	char buf[5];
	int ans;

    if (stuffp->present & MULTI) {
        ans = mcdx_talk(stuffp, "\x11", 1, buf, sizeof(buf), 200, tries);
        multi->multi = buf[1];
        multi->msf_last.minute = buf[2];
        multi->msf_last.second = buf[3];
        multi->msf_last.frame = buf[4];
        return ans;
    } else {
        multi->multi = 0;
        return 0;
    }
}

static int 
mcdx_requesttocdata(struct s_drive_stuff *stuffp, struct s_diskinfo *info, int tries)
{
	char buf[9];
	int ans;
	ans = mcdx_talk(stuffp, "\x10", 1, buf, sizeof(buf), 200, tries);
	info->n_first = bcd2uint(buf[1]);
	info->n_last = bcd2uint(buf[2]);
	info->msf_leadout.minute = buf[3];
	info->msf_leadout.second = buf[4];
	info->msf_leadout.frame = buf[5];
	info->msf_first.minute = buf[6];
	info->msf_first.second = buf[7];
	info->msf_first.frame = buf[8];
	return ans;
}

static int
mcdx_setdrivemode(struct s_drive_stuff *stuffp, enum drivemodes mode, int tries)
{
	char cmd[2];
	int ans;

	TRACE((HW, "setdrivemode() %d\n", mode));

	if (-1 == (ans = mcdx_talk(stuffp, "\xc2", 1, cmd, sizeof(cmd), 500, tries)))
		return ans;

	switch (mode) {
	  case TOC: cmd[1] |= 0x04; break;
	  case DATA: cmd[1] &= ~0x04; break;
	  case RAW: cmd[1] |= 0x40; break;
	  case COOKED: cmd[1] &= ~0x40; break;
	  default: break;
	}
	cmd[0] = 0x50;
	return mcdx_talk(stuffp, cmd, 2, NULL, 0, 500, tries);
}


static int
mcdx_setdatamode(struct s_drive_stuff *stuffp, enum datamodes mode, int tries)
{
	unsigned char cmd[2] = { 0xa0 };
	TRACE((HW, "setdatamode() %d\n", mode));
	switch (mode) {
	  case MODE0: cmd[1] = 0x00; break;
	  case MODE1: cmd[1] = 0x01; break;
	  case MODE2: cmd[1] = 0x02; break;
	  default: return -EINVAL;
	}
	return mcdx_talk(stuffp, cmd, 2, NULL, 0, 500, tries);
}

static int
mcdx_config(struct s_drive_stuff *stuffp, int tries)
{
	char cmd[4];

	TRACE((HW, "config()\n"));

	cmd[0] = 0x90;

	cmd[1] = 0x10;		/* irq enable */
	cmd[2] = 0x05;		/* pre, err irq enable */

	if (-1 == mcdx_talk(stuffp, cmd, 3, NULL, 0, 100, tries))
		return -1;

	cmd[1] = 0x02;		/* dma select */
	cmd[2] = 0x00;		/* no dma */

	return mcdx_talk(stuffp, cmd, 3, NULL, 0, 100, tries);
}

static int
mcdx_requestversion(struct s_drive_stuff *stuffp, struct s_version *ver, int tries)
{
	char buf[3];
	int ans;

	if (-1 == (ans = mcdx_talk(stuffp, "\xdc", 1, buf, sizeof(buf), 200, tries)))
		return ans;

	ver->code = buf[1];
	ver->ver = buf[2];

	return ans;
}

static int
mcdx_reset(struct s_drive_stuff *stuffp, enum resetmodes mode, int tries)
{ 
	if (mode == HARD) {
		outb(0, (unsigned int) stuffp->wreg_chn);		/* no dma, no irq -> hardware */
		outb(0, (unsigned int) stuffp->wreg_reset);		/* hw reset */
		return 0;
	} else return mcdx_talk(stuffp, "\x60", 1, NULL, 0, 500, tries);
}

static int
mcdx_lockdoor(struct s_drive_stuff *stuffp, int lock, int tries)
{
	char cmd[2] = { 0xfe };
    if (stuffp->present & DOOR) {
        cmd[1] = lock ? 0x01 : 0x00;
        return mcdx_talk(stuffp, cmd, sizeof(cmd), NULL, 0, 500, tries);
    } else 
        return 0;
}

#if 0
static int
mcdx_getstatus(struct s_drive_stuff *stuffp, int tries)
{ return mcdx_talk(stuffp, "\x40", 1, NULL, 0, 500, tries); }
#endif
