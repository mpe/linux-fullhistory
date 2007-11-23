/*
 * programming the msp34* sound processor family
 *
 * (c) 1997,1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 * what works and what doesn't:
 *
 *  AM mono
 *      probably doesn't (untested)
 *
 *  FM-mono
 *      should work. FM stereo modes are backward-compatible to mono.
 *      Therefore FM mono should always be available.
 *
 *  FM stereo (B/G, used in Germany)
 *      should work, with autodetect
 *
 *  FM stereo (satellite)
 *      should work, no autodetect (i.e. default is mono, but you can
 *      switch to stereo -- untested)
 *
 *  NICAM (B/G, used in UK, Scandinavia and Spain)
 *      should work, with autodetect. Support for NICAM was added by
 *      Pekka Pietikainen <pp@netppl.fi>
 *
 *
 * TODO:
 *   - better SAT support
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/malloc.h>

/* kernel_thread */
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#include <linux/i2c.h>
#include <linux/videodev.h>

#include "msp3400.h"


/* sound mixer stuff */ 
#include <linux/config.h>

#if LINUX_VERSION_CODE > 0x020140 /* need modular sound driver */
# if defined(CONFIG_SOUND) || defined(CONFIG_SOUND_MODULE)
#  define REGISTER_MIXER 1
# endif
#endif


static int debug = 0; /* insmod parameter */

struct msp3400c {
	struct i2c_bus     *bus;

	int nicam;
	int mode;
	int norm;
	int stereo;

	int mixer;
	int left, right; /* volume */
	int bass, treble;

	/* thread */
	struct task_struct  *thread;
	struct semaphore    *wait;
	struct semaphore    *notify;
	int                  active,restart,rmmod;

	int                  watch_stereo;
	struct timer_list    wake_stereo;
};

#define VIDEO_MODE_RADIO 16      /* norm magic for radio mode */

/* ---------------------------------------------------------------------- */

#define dprintk     if (debug) printk

MODULE_PARM(debug,"i");

/* ---------------------------------------------------------------------- */

#define I2C_MSP3400C       0x80
#define I2C_MSP3400C_DEM   0x10
#define I2C_MSP3400C_DFP   0x12

/* ----------------------------------------------------------------------- */
/* functions for talking to the MSP3400C Sound processor                   */

static int msp3400c_reset(struct i2c_bus *bus)
{
	int ret = 0;
    
	mdelay(2);
	i2c_start(bus);
	i2c_sendbyte(bus, I2C_MSP3400C,2000);
	i2c_sendbyte(bus, 0x00,0);
	i2c_sendbyte(bus, 0x80,0);
	i2c_sendbyte(bus, 0x00,0);
	i2c_stop(bus);
	mdelay(2);
	i2c_start(bus);
	if (0 != i2c_sendbyte(bus, I2C_MSP3400C,2000) ||
	    0 != i2c_sendbyte(bus, 0x00,0) ||
	    0 != i2c_sendbyte(bus, 0x00,0) ||
	    0 != i2c_sendbyte(bus, 0x00,0)) {
		ret = -1;
		printk(KERN_ERR "msp3400: chip reset failed, penguin on i2c bus?\n");
	}
	i2c_stop(bus);
	mdelay(2);
	return ret;
}

static int
msp3400c_read(struct i2c_bus *bus, int dev, int addr)
{
	int ret=0;
	short val = 0;
	i2c_start(bus);
	if (0 != i2c_sendbyte(bus, I2C_MSP3400C,2000) ||
	    0 != i2c_sendbyte(bus, dev+1,       0)    ||
	    0 != i2c_sendbyte(bus, addr >> 8,   0)    ||
	    0 != i2c_sendbyte(bus, addr & 0xff, 0)) {
		ret = -1;
	} else {
		i2c_start(bus);
		if (0 != i2c_sendbyte(bus, I2C_MSP3400C+1,2000)) {
			ret = -1;
		} else {
			val |= (int)i2c_readbyte(bus,0) << 8;
			val |= (int)i2c_readbyte(bus,1);
		}
	}
	i2c_stop(bus);
	if (-1 == ret) {
		printk(KERN_WARNING "msp3400: I/O error, trying reset (read %s 0x%x)\n",
		       (dev == I2C_MSP3400C_DEM) ? "Demod" : "Audio", addr);
		msp3400c_reset(bus);
	}
	return val;
}

static int
msp3400c_write(struct i2c_bus *bus, int dev, int addr, int val)
{
	int ret = 0;
    
	i2c_start(bus);
	if (0 != i2c_sendbyte(bus, I2C_MSP3400C,2000) ||
	    0 != i2c_sendbyte(bus, dev,         0)    ||
	    0 != i2c_sendbyte(bus, addr >> 8,   0)    ||
	    0 != i2c_sendbyte(bus, addr & 0xff, 0)    ||
	    0 != i2c_sendbyte(bus, val >> 8,    0)    ||
	    0 != i2c_sendbyte(bus, val & 0xff,  0))
		ret = -1;
	i2c_stop(bus);
	if (-1 == ret) {
		printk(KERN_WARNING "msp3400: I/O error, trying reset (write %s 0x%x)\n",
		       (dev == I2C_MSP3400C_DEM) ? "Demod" : "Audio", addr);
		msp3400c_reset(bus);
	}
	return ret;
}

/* ------------------------------------------------------------------------ */

/* This macro is allowed for *constants* only, gcc must calculate it
   at compile time.  Remember -- no floats in kernel mode */
#define MSP_CARRIER(freq) ((int)((float)(freq/18.432)*(1<<24)))

#define MSP_MODE_AM_DETECT   0
#define MSP_MODE_FM_RADIO    2
#define MSP_MODE_FM_TERRA    3
#define MSP_MODE_FM_SAT      4
#define MSP_MODE_FM_NICAM1   5
#define MSP_MODE_FM_NICAM2   6

static struct MSP_INIT_DATA_DEM {
	int fir1[6];
	int fir2[6];
	int cdo1;
	int cdo2;
	int ad_cv;
	int mode_reg;
	int dfp_src;
	int dfp_matrix;
} msp_init_data[] = {
	/* AM (for carrier detect / msp3400) */
	{ { 75, 19, 36, 35, 39, 40 }, { 75, 19, 36, 35, 39, 40 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0500,   0x0020, 0x3000},

	/* AM (for carrier detect / msp3410) */
	{ { -1, -1, -8, 2, 59, 126 }, { -1, -1, -8, 2, 59, 126 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0100,   0x0020, 0x3000},

	/* FM Radio */
	{ { -8, -8, 4, 6, 78, 107 }, { -8, -8, 4, 6, 78, 107 },
	  MSP_CARRIER(10.7), MSP_CARRIER(10.7),
	  0x00d0, 0x0480, 0x0020, 0x3002 },

	/* Terrestial FM-mono */
	{ {  3, 18, 27, 48, 66, 72 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0480,   0x0030, 0x3000},

	/* Sat FM-mono */
	{ {  1,  9, 14, 24, 33, 37 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(6.5), MSP_CARRIER(6.5),
	  0x00c6, 0x0480,   0x0000, 0x3000},

	/* NICAM B/G, D/K */
	{ { -2, -8, -10, 10, 50, 86 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0040,   0x0120, 0x3000},

	/* NICAM I */
	{ {  2, 4, -6, -4, 40, 94 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0040,   0x0120, 0x3000},
};

struct CARRIER_DETECT {
	int   cdo;
	char *name;
};

static struct CARRIER_DETECT carrier_detect_main[] = {
	/* main carrier */
	{ MSP_CARRIER(4.5),        "4.5   NTSC"                   }, 
	{ MSP_CARRIER(5.5),        "5.5   PAL B/G"                }, 
	{ MSP_CARRIER(6.0),        "6.0   PAL I"                  },
	{ MSP_CARRIER(6.5),        "6.5   PAL SAT / SECAM"        }
};

static struct CARRIER_DETECT carrier_detect_55[] = {
	/* PAL B/G */
	{ MSP_CARRIER(5.7421875),  "5.742 PAL B/G FM-stereo"     }, 
	{ MSP_CARRIER(5.85),       "5.85  PAL B/G NICAM"         }
};

static struct CARRIER_DETECT carrier_detect_65[] = {
	/* PAL SAT / SECAM */
	{ MSP_CARRIER(7.02),       "7.02  PAL SAT FM-stereo s/b" },
	{ MSP_CARRIER(7.20),       "7.20  PAL SAT FM-stereo s"   },
	{ MSP_CARRIER(7.38),       "7.38  PAL SAT FM-stereo b"   },
};

#define CARRIER_COUNT(x) (sizeof(x)/sizeof(struct CARRIER_DETECT))

/* ------------------------------------------------------------------------ */

static void msp3400c_setcarrier(struct i2c_bus *bus, int cdo1, int cdo2)
{
	msp3400c_write(bus,I2C_MSP3400C_DEM, 0x0093, cdo1 & 0xfff);
	msp3400c_write(bus,I2C_MSP3400C_DEM, 0x009b, cdo1 >> 12);
	msp3400c_write(bus,I2C_MSP3400C_DEM, 0x00a3, cdo2 & 0xfff);
	msp3400c_write(bus,I2C_MSP3400C_DEM, 0x00ab, cdo2 >> 12);
}

static void msp3400c_setvolume(struct i2c_bus *bus, int left, int right)
{
	int vol,val,balance;

	vol     = (left > right) ? left : right;
	val     = (vol * 0x73 / 65535) << 8;
	balance = 0;
	if (vol > 0)
		balance = ((right-left) * 127) / vol;

	dprintk("msp3400: setvolume: %d:%d 0x%02x 0x%02x\n",
		left,right,val>>8,balance);
	msp3400c_write(bus,I2C_MSP3400C_DFP, 0x0000, val); /* loudspeaker */
	msp3400c_write(bus,I2C_MSP3400C_DFP, 0x0006, val); /* headphones  */
	/* scart - on/off only */
	msp3400c_write(bus,I2C_MSP3400C_DFP, 0x0007, val ? 0x4000 : 0);
	msp3400c_write(bus,I2C_MSP3400C_DFP, 0x0001, balance << 8);
}

static void msp3400c_setbass(struct i2c_bus *bus, int bass)
{
	int val = ((bass-32768) * 0x60 / 65535) << 8;

	dprintk("msp3400: setbass: %d 0x%02x\n",bass, val>>8);
	msp3400c_write(bus,I2C_MSP3400C_DFP, 0x0002, val); /* loudspeaker */
}

static void msp3400c_settreble(struct i2c_bus *bus, int treble)
{
	int val = ((treble-32768) * 0x60 / 65535) << 8;

	dprintk("msp3400: settreble: %d 0x%02x\n",treble, val>>8);
	msp3400c_write(bus,I2C_MSP3400C_DFP, 0x0003, val); /* loudspeaker */
}

static void msp3400c_setmode(struct msp3400c *msp, int type)
{
	int i;
	
	dprintk("msp3400: setmode: %d\n",type);
	msp->mode   = type;
	msp->stereo = VIDEO_SOUND_MONO;

	msp3400c_write(msp->bus,I2C_MSP3400C_DEM, 0x00bb,          /* ad_cv */
		       msp_init_data[type].ad_cv);
    
	for (i = 5; i >= 0; i--)                                   /* fir 1 */
		msp3400c_write(msp->bus,I2C_MSP3400C_DEM, 0x0001,
			       msp_init_data[type].fir1[i]);
    
	msp3400c_write(msp->bus,I2C_MSP3400C_DEM, 0x0005, 0x0004); /* fir 2 */
	msp3400c_write(msp->bus,I2C_MSP3400C_DEM, 0x0005, 0x0040);
	msp3400c_write(msp->bus,I2C_MSP3400C_DEM, 0x0005, 0x0000);
	for (i = 5; i >= 0; i--)
		msp3400c_write(msp->bus,I2C_MSP3400C_DEM, 0x0005,
			       msp_init_data[type].fir2[i]);
    
	msp3400c_write(msp->bus,I2C_MSP3400C_DEM, 0x0083,          /* MODE_REG */
		       msp_init_data[type].mode_reg);
    
	msp3400c_setcarrier(msp->bus, msp_init_data[type].cdo1,
			    msp_init_data[type].cdo2);
    
	msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0008,
		       msp_init_data[type].dfp_src);
	msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0009,
		       msp_init_data[type].dfp_src);
	msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x000a,
		       msp_init_data[type].dfp_src);
	msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x000e,
		       msp_init_data[type].dfp_matrix);

	if (msp->nicam) {
		/* msp3410 needs some more initialization */
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0010, 0x3000);
	}
}

static void msp3400c_setstereo(struct msp3400c *msp, int mode)
{
	int nicam=0; /* channel source: FM/AM or nicam */

	/* switch demodulator */
	switch (msp->mode) {
	case MSP_MODE_FM_TERRA:
		dprintk("msp3400: B/G setstereo: %d\n",mode);
		msp->stereo = mode;
		msp3400c_setcarrier(msp->bus,MSP_CARRIER(5.7421875),MSP_CARRIER(5.5));
		switch (mode) {
		case VIDEO_SOUND_STEREO:
			msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x000e, 0x3001);
			break;
		case VIDEO_SOUND_MONO:
		case VIDEO_SOUND_LANG1:
		case VIDEO_SOUND_LANG2:
			msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x000e, 0x3000);
			break;
		}
		break;
	case MSP_MODE_FM_SAT:
		dprintk("msp3400: sat setstereo: %d\n",mode);
		msp->stereo = mode;
		switch (mode) {
		case VIDEO_SOUND_MONO:
			msp3400c_setcarrier(msp->bus, MSP_CARRIER(6.5), MSP_CARRIER(6.5));
			break;
		case VIDEO_SOUND_STEREO:
			msp3400c_setcarrier(msp->bus, MSP_CARRIER(7.2), MSP_CARRIER(7.02));
			break;
		case VIDEO_SOUND_LANG1:
			msp3400c_setcarrier(msp->bus, MSP_CARRIER(7.38), MSP_CARRIER(7.02));
			break;
		case VIDEO_SOUND_LANG2:
			msp3400c_setcarrier(msp->bus, MSP_CARRIER(7.38), MSP_CARRIER(7.02));
			break;
		}
		break;
	case MSP_MODE_FM_NICAM1:
		dprintk("msp3400: NICAM1 setstereo: %d\n",mode);
		msp->stereo = mode;
		msp3400c_setcarrier(msp->bus,MSP_CARRIER(5.85),MSP_CARRIER(5.5));
		nicam=0x0100;
		break;
	default:
		/* can't do stereo - abort here */
		return;
	}

	/* switch audio */
	switch (mode) {
	case VIDEO_SOUND_STEREO:
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0008,0x0020|nicam);
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0009,0x0020|nicam);
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x000a,0x0020|nicam);
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0005,0x4000);
		break;
	case VIDEO_SOUND_MONO:
	case VIDEO_SOUND_LANG1:
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0008,0x0000|nicam);
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0009,0x0000|nicam);
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x000a,0x0000|nicam);
		break;
	case VIDEO_SOUND_LANG2:
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0008,0x0010|nicam);
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0009,0x0010|nicam);
		msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x000a,0x0010|nicam);
		break;
	}
}

/* ----------------------------------------------------------------------- */

struct REGISTER_DUMP {
	int   addr;
	char *name;
};

struct REGISTER_DUMP d1[] = {
	{ 0x007e, "autodetect" },
	{ 0x0023, "C_AD_BITS " },
	{ 0x0038, "ADD_BITS  " },
	{ 0x003e, "CIB_BITS  " },
	{ 0x0057, "ERROR_RATE" },
};

/*
 * A kernel thread for msp3400 control -- we don't want to block the
 * in the ioctl while doing the sound carrier & stereo detect
 */

static void msp3400c_stereo_wake(unsigned long data)
{
	struct msp3400c *msp = (struct msp3400c*)data;   /* XXX alpha ??? */

	if (!msp->active)
		up(msp->wait);    
}

static int msp3400c_thread(void *data)
{
	unsigned long flags;
	struct msp3400c *msp = data;
	struct semaphore sem = MUTEX_LOCKED;
    
	struct CARRIER_DETECT *cd;
	int                   count, max1,max2,val1,val2, val,this;
	int                   newstereo;
    
	/* lock_kernel(); */
    
	exit_mm(current);
	current->session = 1;
	current->pgrp = 1;
	sigfillset(&current->blocked);
	current->fs->umask = 0;
	strcpy(current->comm,"msp3400");

	msp->wait   = &sem;
	msp->thread = current;

	/* unlock_kernel(); */

	dprintk("msp3400: thread: start\n");
	if(msp->notify != NULL)
		up(msp->notify);
		
	for (;;) {
		if (msp->rmmod)
			goto done;
		dprintk("msp3400: thread: sleep\n");
		down_interruptible(&sem);
		dprintk("msp3400: thread: wakeup\n");
		if (msp->rmmod || signal_pending(current))
			goto done;

		if (VIDEO_MODE_RADIO == msp->norm)
			continue;  /* nothing to do */
	
		msp->active = 1;

		if (msp->watch_stereo) {
			/* do that stereo/multilang handling */
			LOCK_I2C_BUS(msp->bus);
			newstereo = msp->stereo;
			switch (msp->mode) {
			case MSP_MODE_FM_TERRA:
				val = msp3400c_read(msp->bus, I2C_MSP3400C_DFP, 0x18);
				dprintk("msp3400: stereo detect register: %d\n",val);
		
				if (val > 4096) {
					newstereo = VIDEO_SOUND_STEREO;
				} else if (val < -4096) {
					newstereo = VIDEO_SOUND_LANG1;
				} else {
					newstereo = VIDEO_SOUND_MONO;
				}
				break;
			case MSP_MODE_FM_NICAM1:
				val = msp3400c_read(msp->bus, I2C_MSP3400C_DEM, 0x23);
				switch ((val & 0x1e) >> 1)  {
				case 0:
				case 8:
					newstereo = VIDEO_SOUND_STEREO;
					break;
				default:
					newstereo = VIDEO_SOUND_MONO;
					break;
				}
				break;
			}
			if (msp->stereo != newstereo) {
				dprintk("msp3400: watch: stereo %d ==> %d\n",
					msp->stereo,newstereo);
				msp3400c_setstereo(msp,newstereo);
			}
			UNLOCK_I2C_BUS(msp->bus);
			if (msp->watch_stereo) {
				del_timer(&msp->wake_stereo);
				msp->wake_stereo.expires = jiffies + 5*HZ;
				add_timer(&msp->wake_stereo);
			}

			msp->active = 0;
			continue;
		}
	
	restart:
		LOCK_I2C_BUS(msp->bus);
		msp3400c_setvolume(msp->bus, 0, 0);
		msp3400c_setmode(msp, MSP_MODE_AM_DETECT);
		val1 = val2 = 0;
		max1 = max2 = -1;
		del_timer(&msp->wake_stereo);
		msp->watch_stereo = 0;

		/* carrier detect pass #1 -- main carrier */
		cd = carrier_detect_main; count = CARRIER_COUNT(carrier_detect_main);
		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(msp->bus, cd[this].cdo,cd[this].cdo);
			UNLOCK_I2C_BUS(msp->bus);

			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(HZ/25);
			if (signal_pending(current))
				goto done;
			if (msp->restart) {
				msp->restart = 0;
				goto restart;
			}

			LOCK_I2C_BUS(msp->bus);
			val = msp3400c_read(msp->bus, I2C_MSP3400C_DFP, 0x1b);
			if (val1 < val)
				val1 = val, max1 = this;
			dprintk("msp3400: carrier1 val: %5d / %s\n", val,cd[this].name);
		}

		/* carrier detect pass #2 -- second (stereo) carrier */
		switch (max1) {
		case 1: /* 5.5 */
			cd = carrier_detect_55; count = CARRIER_COUNT(carrier_detect_55);
			break;
		case 3: /* 6.5 */
			cd = carrier_detect_65; count = CARRIER_COUNT(carrier_detect_65);
			break;
		case 0: /* 4.5 */
		case 2: /* 6.0 */
		default:
			cd = NULL; count = 0;
			break;
		}
		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(msp->bus, cd[this].cdo,cd[this].cdo);
			UNLOCK_I2C_BUS(msp->bus);

			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(HZ/25);
			if (signal_pending(current))
				goto done;
			if (msp->restart) {
				msp->restart = 0;
				goto restart;
			}

			LOCK_I2C_BUS(msp->bus);
			val = msp3400c_read(msp->bus, I2C_MSP3400C_DFP, 0x1b);
			if (val2 < val)
				val2 = val, max2 = this;
			dprintk("msp3400: carrier2 val: %5d / %s\n", val,cd[this].name);
		}

		/* program the msp3400 according to the results */
		switch (max1) {
		case 0: /* 4.5 */
		case 1: /* 5.5 */
			msp3400c_setmode(msp, MSP_MODE_FM_TERRA);
			msp3400c_setcarrier(msp->bus, carrier_detect_main[max1].cdo,
					    carrier_detect_main[max1].cdo);
			if (max2 == 0) {
				/* B/G FM-stereo */
				msp3400c_setstereo(msp, VIDEO_SOUND_MONO);
				msp->watch_stereo = 1;
			}
			if (max2 == 1 && msp->nicam) {
				/* B/G NICAM */
				msp3400c_setmode(msp, MSP_MODE_FM_NICAM1);
				/* msp3400c_write(msp->bus, I2C_MSP3400C_DFP, 0x21, 0x01); */
				msp3400c_setcarrier(msp->bus, MSP_CARRIER(5.85),
						    MSP_CARRIER(5.5));
				msp->watch_stereo = 1;
			}
			break;
		case 2: /* 6.0 */
		case 3: /* 6.5 */
		default:
			msp3400c_setmode(msp, MSP_MODE_FM_TERRA);
			msp3400c_setcarrier(msp->bus, carrier_detect_main[max1].cdo,
					    carrier_detect_main[max1].cdo);
			msp3400c_setstereo(msp, VIDEO_SOUND_STEREO);
			break;
		}

		/* unmute */
		msp3400c_setvolume(msp->bus, msp->left, msp->right);
		UNLOCK_I2C_BUS(msp->bus);

		if (msp->watch_stereo) {
			del_timer(&msp->wake_stereo);
			msp->wake_stereo.expires = jiffies + HZ;
			add_timer(&msp->wake_stereo);
		}
		msp->active = 0;
	}

done:
	dprintk("msp3400: thread: exit\n");
	msp->wait   = NULL;
	msp->active = 0;
	msp->thread = NULL;

	if(msp->notify != NULL)
		up(msp->notify);
	return 0;
}


#if 0 /* not finished yet */

static int msp3410d_thread(void *data)
{
	unsigned long flags;
	struct msp3400c *msp = data;
	struct semaphore sem = MUTEX_LOCKED;
	int              i, val;

	/* lock_kernel(); */
    
	exit_mm(current);
	current->session = 1;
	current->pgrp = 1;
	sigfillset(&current->blocked);
	current->fs->umask = 0;
	strcpy(current->comm,"msp3410 (nicam)");

	msp->wait   = &sem;
	msp->thread = current;

	/* unlock_kernel(); */

	dprintk("msp3410: thread: start\n");
	if(msp->notify != NULL)
		up(msp->notify);
		
	for (;;) {
		if (msp->rmmod)
			goto done;
		dprintk("msp3410: thread: sleep\n");
		down_interruptible(&sem);
		dprintk("msp3410: thread: wakeup\n");
		if (msp->rmmod)
			goto done;
	
		if (VIDEO_MODE_RADIO == msp->norm)
			continue;  /* nothing to do */
	
		msp->active = 1;

	restart:
		LOCK_I2C_BUS(msp->bus);
		/* mute */
		msp3400c_setvolume(msp->bus, 0);
		/* quick & dirty hack:
		   get the audio proccessor into some useful state */
		msp3400c_setmode(msp, MSP_MODE_FM_NICAM1);
		/* kick autodetect */
		msp3400c_write(msp->bus, I2C_MSP3400C_DFP, 0x20, 0x01);
		msp3400c_write(msp->bus, I2C_MSP3400C_DFP, 0x21, 0x01);
		UNLOCK_I2C_BUS(msp->bus);

		/* wait 1 sec */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ);
		if (signal_pending(current))
			goto done;
		if (msp->restart) {
			msp->restart = 0;
			goto restart;
		}
	
		LOCK_I2C_BUS(msp->bus);
		/* debug register dump */
		for (i = 0; i < sizeof(d1)/sizeof(struct REGISTER_DUMP); i++) {
			val = msp3400c_read(msp->bus,I2C_MSP3400C_DEM,d1[i].addr);
			printk(KERN_DEBUG "msp3400: %s = 0x%x\n",d1[i].name,val);
		}	
		/* unmute */
		msp3400c_setvolume(msp->bus, msp->volume);
		UNLOCK_I2C_BUS(msp->bus);

		msp->active = 0;
	}

done:
	dprintk("msp3410: thread: exit\n");
	msp->wait   = NULL;
	msp->active = 0;
	msp->thread = NULL;

	if(msp->notify != NULL)
		up(msp->notify);
	return 0;
}
#endif

/* ----------------------------------------------------------------------- */
/* mixer stuff -- with the modular sound driver in 2.1.x we can easily     */
/* register the msp3400 as mixer device                                    */

#ifdef REGISTER_MIXER

#include <linux/soundcard.h>
#include <../drivers/sound/sound_config.h>
#include <../drivers/sound/dev_table.h>

static int mix_to_v4l(int i)
{
	int r;

	r = ((i & 0xff) * 65536 + 50) / 100;
	if (r > 65535) r = 65535;
	if (r <     0) r =     0;
	return r;
}

static int v4l_to_mix(int i)
{
	int r;

	r = (i * 100 + 32768) / 65536;
	if (r > 100) r = 100;
	if (r <   0) r =   0;
	return r | (r << 8);
}

static int v4l_to_mix2(int l, int r)
{
	r = (r * 100 + 32768) / 65536;
	if (r > 100) r = 100;
	if (r <   0) r =   0;
	l = (l * 100 + 32768) / 65536;
	if (l > 100) l = 100;
	if (l <   0) l =   0;
	return (r << 8) | l;
}

static int msp3400c_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	struct msp3400c *msp = mixer_devs[dev]->devc;
	unsigned long flags;
	int ret,val = 0;

	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
		if (get_user(val, (int *)arg))
			return -EFAULT;
    
	switch (cmd) {
	case MIXER_READ(SOUND_MIXER_RECMASK):
	case MIXER_READ(SOUND_MIXER_CAPS):
	case MIXER_READ(SOUND_MIXER_RECSRC):
	case MIXER_WRITE(SOUND_MIXER_RECSRC):
		ret = 0;
		break;

	case MIXER_READ(SOUND_MIXER_STEREODEVS):
		ret = SOUND_MASK_VOLUME;
		break;
	case MIXER_READ(SOUND_MIXER_DEVMASK):
		ret = SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE;
		break;

	case MIXER_WRITE(SOUND_MIXER_VOLUME):
		msp->left  = mix_to_v4l(val);
		msp->right = mix_to_v4l(val >> 8);
		LOCK_I2C_BUS(msp->bus);
		msp3400c_setvolume(msp->bus,msp->left,msp->right);
		UNLOCK_I2C_BUS(msp->bus);
		/* fall */
	case MIXER_READ(SOUND_MIXER_VOLUME):
		ret = v4l_to_mix2(msp->left, msp->right);
		break;

	case MIXER_WRITE(SOUND_MIXER_BASS):
		msp->bass = mix_to_v4l(val);
		LOCK_I2C_BUS(msp->bus);
		msp3400c_setbass(msp->bus,msp->bass);
		UNLOCK_I2C_BUS(msp->bus);
		/* fall */
	case MIXER_READ(SOUND_MIXER_BASS):
		ret = v4l_to_mix(msp->bass);
		break;

	case MIXER_WRITE(SOUND_MIXER_TREBLE):
		msp->treble = mix_to_v4l(val);
		LOCK_I2C_BUS(msp->bus);
		msp3400c_settreble(msp->bus,msp->treble);
		UNLOCK_I2C_BUS(msp->bus);
		/* fall */
	case MIXER_READ(SOUND_MIXER_TREBLE):
		ret = v4l_to_mix(msp->treble);
		break;

	default:
		return -EINVAL;
	}
	if (put_user(ret, (int *)arg))
		return -EFAULT;
	return 0;
}

struct mixer_operations msp3400c_mixer = {
	"video4linux",
	"TV card sound (msp3400)",
	msp3400c_mixer_ioctl
};

static int msp3400c_mixer_init(struct msp3400c *msp)
{
	int m;
    
	msp->mixer = m = sound_alloc_mixerdev();
	if (m == -1)
		return -1;

	mixer_devs[m] = (struct mixer_operations *)
		kmalloc(sizeof(struct mixer_operations), GFP_KERNEL);
	if (mixer_devs[m] == NULL) {
		printk(KERN_ERR "msp3400c: can't allocate memory\n");
		sound_unload_mixerdev(m);
		return -1;
	}
	memcpy(mixer_devs[m],&msp3400c_mixer,sizeof(struct mixer_operations));
	mixer_devs[m]->devc = msp;
	return 0;
}

static int msp3400c_mixer_close(struct msp3400c *msp)
{
	int m = msp->mixer;

	if (m != -1 ) {
		sound_unload_mixerdev(m);
		kfree(mixer_devs[m]);
	}
	return 0;
}

#endif

/* ----------------------------------------------------------------------- */

static int msp3400c_attach(struct i2c_device *device)
{
	unsigned long flags;
	struct semaphore sem = MUTEX_LOCKED;
	struct msp3400c *msp;
	int              rev1,rev2;

	device->data = msp = kmalloc(sizeof(struct msp3400c),GFP_KERNEL);
	if (NULL == msp)
		return -ENOMEM;
	memset(msp,0,sizeof(struct msp3400c));
	msp->bus = device->bus;
	msp->left   = 65535;
	msp->right  = 65535;
	msp->bass   = 32768;
	msp->treble = 32768;

	LOCK_I2C_BUS(msp->bus);
	if (-1 == msp3400c_reset(msp->bus)) {
		UNLOCK_I2C_BUS(msp->bus);
		kfree(msp);
		return -EIO;
	}
    
	msp3400c_setmode(msp, MSP_MODE_FM_TERRA);
	msp3400c_setvolume(msp->bus, msp->left, msp->right);
	msp3400c_setbass(msp->bus, msp->bass);
	msp3400c_settreble(msp->bus, msp->treble);
    
	rev1 = msp3400c_read(msp->bus, I2C_MSP3400C_DFP, 0x1e);
	rev2 = msp3400c_read(msp->bus, I2C_MSP3400C_DFP, 0x1f);

#if 0
	/* this will turn on a 1kHz beep - might be useful for debugging... */
	msp3400c_write(msp->bus,I2C_MSP3400C_DFP, 0x0014, 0x1040);
#endif
	UNLOCK_I2C_BUS(msp->bus);

	sprintf(device->name,"MSP34%02d%c-%c%d",
		(rev2>>8)&0xff, (rev1&0xff)+'@', ((rev1>>8)&0xff)+'@', rev2&0x1f);
	msp->nicam = (((rev2>>8)&0xff) == 10) ? 1 : 0;

	/* timer for stereo checking */
	msp->wake_stereo.function = msp3400c_stereo_wake;
	msp->wake_stereo.data     = (unsigned long)msp;

	/* startup control thread */
	MOD_INC_USE_COUNT;
	msp->notify = &sem;
	kernel_thread(msp3400c_thread, (void *)msp, 0);
	down(&sem);
	msp->notify = NULL;
	if (!msp->active)
		up(msp->wait);

	printk(KERN_INFO "msp3400: init: chip=%s",device->name);
	if (msp->nicam)
		printk(", has NICAM support");
#ifdef REGISTER_MIXER
	if (0 == msp3400c_mixer_init(msp))
		printk(", registered as sound mixer");
#endif
	printk("\n");
	return 0;
}

static int msp3400c_detach(struct i2c_device *device)
{
	unsigned long flags;
	struct semaphore sem = MUTEX_LOCKED;
	struct msp3400c *msp  = (struct msp3400c*)device->data;
    
#ifdef REGISTER_MIXER
	msp3400c_mixer_close(msp);
#endif

	/* shutdown control thread */
	del_timer(&msp->wake_stereo);
	if (msp->thread) 
	{
		msp->notify = &sem;
		msp->rmmod = 1;
		if (!msp->active)
			up(msp->wait);
		down(&sem);
		msp->notify = NULL;
	}
    
	LOCK_I2C_BUS(msp->bus);
	msp3400c_reset(msp->bus);
	UNLOCK_I2C_BUS(msp->bus);

	kfree(msp);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int msp3400c_command(struct i2c_device *device,
		 unsigned int cmd, void *arg)
{
	unsigned long flags;
	struct msp3400c *msp  = (struct msp3400c*)device->data;
	int             *iarg = (int*)arg;

	switch (cmd) {
	case MSP_SET_RADIO:
		msp->norm = VIDEO_MODE_RADIO;
		msp->watch_stereo=0;
		del_timer(&msp->wake_stereo);
		LOCK_I2C_BUS(msp->bus);
		msp3400c_setmode(msp,MSP_MODE_FM_RADIO);
		msp3400c_setcarrier(msp->bus, MSP_CARRIER(10.7),MSP_CARRIER(10.7));
		UNLOCK_I2C_BUS(msp->bus);
		break;
	case MSP_SET_TVNORM:
		msp->norm = *iarg;
		break;
	case MSP_NEWCHANNEL:
		msp->watch_stereo=0;
		del_timer(&msp->wake_stereo);
		if (!msp->active)
			up(msp->wait);
		else
			msp->restart = 1;
		break;

	case MSP_GET_VOLUME:
		*iarg = (msp->left > msp->right) ? msp->left : msp->right;
		break;
	case MSP_SET_VOLUME:
		msp->left = msp->right = *iarg;
		LOCK_I2C_BUS(msp->bus);
		msp3400c_setvolume(msp->bus,msp->left, msp->right);
		UNLOCK_I2C_BUS(msp->bus);
		break;

	case MSP_GET_BASS:
		*iarg = msp->bass;
		break;
	case MSP_SET_BASS:
		msp->bass = *iarg;
		LOCK_I2C_BUS(msp->bus);
		msp3400c_setbass(msp->bus,msp->bass);
		UNLOCK_I2C_BUS(msp->bus);
		break;

	case MSP_GET_TREBLE:
		*iarg = msp->treble;
		break;
	case MSP_SET_TREBLE:
		msp->treble = *iarg;
		LOCK_I2C_BUS(msp->bus);
		msp3400c_settreble(msp->bus,msp->treble);
		UNLOCK_I2C_BUS(msp->bus);
		break;

	case MSP_GET_STEREO:
		*iarg = msp->stereo;
		break;
	case MSP_SET_STEREO:
		if (*iarg) {
			msp->watch_stereo=0;
			del_timer(&msp->wake_stereo);
			LOCK_I2C_BUS(msp->bus);
			msp3400c_setstereo(msp,*iarg);
			UNLOCK_I2C_BUS(msp->bus);
		}
		break;

	case MSP_GET_DC:
		LOCK_I2C_BUS(msp->bus);
		*iarg = (int)msp3400c_read(msp->bus, I2C_MSP3400C_DFP, 0x1b) +
			(int)msp3400c_read(msp->bus, I2C_MSP3400C_DFP, 0x1c);
		UNLOCK_I2C_BUS(msp->bus);
		break;
		
	case MSP_GET_UNIT:
		*iarg = msp->mixer;
		break;
	
	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

struct i2c_driver i2c_driver_msp = {
	"msp3400",                    /* name       */
	I2C_DRIVERID_MSP3400,         /* ID         */
	I2C_MSP3400C, I2C_MSP3400C,   /* addr range */

	msp3400c_attach,
	msp3400c_detach,
	msp3400c_command
};

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
     int msp3400c_init(void)
#endif
{
	i2c_register_driver(&i2c_driver_msp);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	i2c_unregister_driver(&i2c_driver_msp);
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
