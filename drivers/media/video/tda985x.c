/*
 * For the TDA9850 and TDA9855 chips
 * (The TDA9855 is used on the Diamond DTV2000 and the TDA9850 is used 
 * on STB cards.  Other cards probably use these chips as well.)
 * This driver will not complain if used with any 
 * other i2c device with the same address.
 *
 * Copyright (c) 1999 Gerd Knorr
 * TDA9850 code and TDA9855.c merger by Eric Sandeen (eric_sandeen@bigfoot.com) 
 * This code is placed under the terms of the GNU General Public License
 * Based on tda9855.c by Steve VanDeBogart (vandebo@uclink.berkeley.edu)
 * Which was based on tda8425.c by Greg Alexander (c) 1998
 *
 * Contributors:
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * OPTIONS:
 * debug   - set to 1 if you'd like to see debug messages
 *         - set to 2 if you'd like to be flooded with debug messages
 * chip    - set to 9850 or 9855 to select your chip (default 9855)
 *
 * TODO:
 *   Fix channel change bug - sound goes out when changeing channels, mute
 *                            and unmote to fix. - Is this still here?
 *   Fine tune sound
 *   Get rest of capabilities into video_audio struct...
 *
 *  Revision  0.6 - resource allocation fixes in tda985x_attach (08/14/2000)
 *  Revision  0.5 - cleaned up debugging messages, added debug level=2 
 *  Revision: 0.4 - check for correct chip= insmod value
 *                  also cleaned up comments a bit
 *  Revision: 0.3 - took out extraneous tda985x_write in tda985x_command
 *  Revision: 0.2 - added insmod option chip=
 *  Revision: 0.1 - original version
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/videodev.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include "bttv.h"
#include "audiochip.h"

MODULE_PARM(debug,"i");
MODULE_PARM(chip,"i");
MODULE_PARM_DESC(chip, "Type of chip to handle: 9850 or 9855");

static int debug = 0;	/* insmod parameter */
static int chip = 9855;	/* insmod parameter */

/* Addresses to scan */
#define I2C_TDA985x_L        0xb4
#define I2C_TDA985x_H        0xb6
static unsigned short normal_i2c[] = {I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {
	I2C_TDA985x_L >> 1,
	I2C_TDA985x_H >> 1,
	I2C_CLIENT_END
};
static unsigned short probe[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short probe_range[2]  = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore[2]       = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore_range[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short force[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static struct i2c_client_address_data addr_data = {
	normal_i2c, normal_i2c_range, 
	probe, probe_range, 
	ignore, ignore_range, 
	force
};

/* This is a superset of the TDA9850 and TDA9855 members */

struct tda985x {
	int addr;
	int rvol, lvol;
	int bass, treble, sub;
	int c4, c5, c6, c7;
	int a1, a2, a3;
};

static struct i2c_driver driver;
static struct i2c_client client_template;

#define dprintk  if (debug) printk
#define d2printk if (debug == 2) printk

/* The TDA9850 and TDA9855 are both made by Philips Semiconductor
 * http://www.semiconductors.philips.com
 * TDA9850: I2C-bus controlled BTSC stereo/SAP decoder
 * TDA9855: I2C-bus controlled BTSC stereo/SAP decoder and audio processor
 *
 * The TDA9850 has more or less a subset of the functions that the TDA9855
 * has.  As a result, we can re-use many of these defines.  Anything with
 * TDA9855 is specific to that chip, anything with TDA9850 is specific
 * to that chip, and anything with TDA985x is valid for either.
 *
 * To complicate things further, the TDA9850 uses labels C1 through C4
 * for subaddresses 0x04 through 0x07, while the TDA9855 uses
 * C1 through C3 for subadresses 0x05 through 0x07 - quite confusing.
 * To help keep things straight, I have renamed the various C[1,4] labels
 * to C[4,7] so that the numerical label matches the hex value of the
 * subaddress for both chips.  At least the A[1,3] labels line up.  :)
 */ 

		/* subaddresses for TDA9855 */
#define TDA9855_VR	0x00 /* Volume, right */
#define TDA9855_VL	0x01 /* Volume, left */
#define TDA9855_BA	0x02 /* Bass */
#define TDA9855_TR	0x03 /* Treble */
#define TDA9855_SW	0x04 /* Subwoofer - not connected on DTV2000 */

		/* subaddresses for TDA9850 */
#define TDA9850_C4	0x04 /* Control 1 for TDA9850 */

		/* subaddesses for both chips */
#define TDA985x_C5	0x05 /* Control 2 for TDA9850, Control 1 for TDA9855 */
#define TDA985x_C6	0x06 /* Control 3 for TDA9850, Control 2 for TDA9855 */
#define TDA985x_C7	0x07 /* Control 4 for TDA9850, Control 3 for TDA9855 */
#define TDA985x_A1	0x08 /* Alignment 1 for both chips */
#define TDA985x_A2	0x09 /* Alignment 2 for both chips */
#define TDA985x_A3	0x0a /* Alignment 3 for both chips */

		/* Masks for bits in TDA9855 subaddresses */
/* 0x00 - VR in TDA9855 */
/* 0x01 - VL in TDA9855 */
/* lower 7 bits control gain from -71dB (0x28) to 16dB (0x7f)
 * in 1dB steps - mute is 0x27 */


/* 0x02 - BA in TDA9855 */ 
/* lower 5 bits control bass gain from -12dB (0x06) to 16.5dB (0x19)
 * in .5dB steps - 0 is 0x0E */


/* 0x03 - TR in TDA9855 */
/* 4 bits << 1 control treble gain from -12dB (0x3) to 12dB (0xb)
 * in 3dB steps - 0 is 0x7 */

		/* Masks for bits in both chips' subaddresses */
/* 0x04 - SW in TDA9855, C4/Control 1 in TDA9850 */
/* Unique to TDA9855: */
/* 4 bits << 2 control subwoofer/surround gain from -14db (0x1) to 14db (0xf)
 * in 3dB steps - mute is 0x0 */
 
/* Unique to TDA9850: */
/* lower 4 bits control stereo noise threshold, over which stereo turns off
 * set to values of 0x00 through 0x0f for Ster1 through Ster16 */


/* 0x05 - C5 - Control 1 in TDA9855 , Control 2 in TDA9850*/
/* Unique to TDA9855: */
#define TDA9855_MUTE	1<<7 /* GMU, Mute at outputs */
#define TDA9855_AVL	1<<6 /* AVL, Automatic Volume Level */
#define TDA9855_LOUD	1<<5 /* Loudness, 1==off */
#define TDA9855_SUR	1<<3 /* Surround / Subwoofer 1==.5(L-R) 0==.5(L+R) */
			     /* Bits 0 to 3 select various combinations
                              * of line in and line out, only the 
                              * interesting ones are defined */
#define TDA9855_EXT	1<<2 /* Selects inputs LIR and LIL.  Pins 41 & 12 */
#define TDA9855_INT	0    /* Selects inputs LOR and LOL.  (internal) */

/* Unique to TDA9850:  */
/* lower 4 bits contol SAP noise threshold, over which SAP turns off
 * set to values of 0x00 through 0x0f for SAP1 through SAP16 */


/* 0x06 - C6 - Control 2 in TDA9855, Control 3 in TDA9850 */
/* Common to TDA9855 and TDA9850: */
#define TDA985x_SAP	3<<6 /* Selects SAP output, mute if not received */
#define TDA985x_STEREO	1<<6 /* Selects Stereo ouput, mono if not received */
#define TDA985x_MONO	0    /* Forces Mono output */
#define TDA985x_LMU	1<<3 /* Mute (LOR/LOL for 9855, OUTL/OUTR for 9850) */

/* Unique to TDA9855: */
#define TDA9855_TZCM	1<<5 /* If set, don't mute till zero crossing */
#define TDA9855_VZCM	1<<4 /* If set, don't change volume till zero crossing*/
#define TDA9855_LINEAR	0    /* Linear Stereo */
#define TDA9855_PSEUDO	1    /* Pseudo Stereo */
#define TDA9855_SPAT_30	2    /* Spatial Stereo, 30% anti-phase crosstalk */
#define TDA9855_SPAT_50	3    /* Spatial Stereo, 52% anti-phase crosstalk */
#define TDA9855_E_MONO	7    /* Forced mono - mono select elseware, so useless*/


/* 0x07 - C7 - Control 3 in TDA9855, Control 4 in TDA9850 */
/* Common to both TDA9855 and TDA9850: */
/* lower 4 bits control input gain from -3.5dB (0x0) to 4dB (0xF)
 * in .5dB steps -  0dB is 0x7 */


/* 0x08, 0x09 - A1 and A2 (read/write) */
/* Common to both TDA9855 and TDA9850: */
/* lower 5 bites are wideband and spectral expander alignment
 * from 0x00 to 0x1f - nominal at 0x0f and 0x10 (read/write) */
#define TDA985x_STP	1<<5 /* Stereo Pilot/detect (read-only) */
#define TDA985x_SAPP	1<<6 /* SAP Pilot/detect (read-only) */
#define TDA985x_STS	1<<7 /* Stereo trigger 1= <35mV 0= <30mV (write-only)*/


/* 0x0a - A3 */
/* Common to both TDA9855 and TDA9850: */
/* lower 3 bits control timing current for alignment: -30% (0x0), -20% (0x1),
 * -10% (0x2), nominal (0x3), +10% (0x6), +20% (0x5), +30% (0x4) */
#define TDA985x_ADJ	1<<7 /* Stereo adjust on/off (wideband and spectral */

/* Unique to TDA9855: */
/* 2 bits << 5 control AVL attack time: 420ohm (0x0), 730ohm (0x2), 
 * 1200ohm (0x1), 2100ohm (0x3) */


/* Begin code */

static int tda985x_write(struct i2c_client *client, int subaddr, int val)
{
	unsigned char buffer[2];
	d2printk("tda985x: In tda985x_write\n");
	dprintk("tda985x: Writing %d 0x%x\n", subaddr, val);
	buffer[0] = subaddr;
	buffer[1] = val;
	if (2 != i2c_master_send(client,buffer,2)) {
		printk(KERN_WARNING "tda985x: I/O error, trying (write %d 0x%x)\n",
		       subaddr, val);
		return -1;
	}
	return 0;
}

static int tda985x_read(struct i2c_client *client)
{
	unsigned char buffer;
	d2printk("tda985x: In tda985x_read\n");
	if (1 != i2c_master_recv(client,&buffer,1)) {
		printk(KERN_WARNING "tda985x: I/O error, trying (read)\n");
		return -1;
	}
	dprintk("tda985x: Read 0x%02x\n", buffer); 
	return buffer;
}

static int tda985x_set(struct i2c_client *client)
{
	struct tda985x *t = client->data;
	unsigned char buf[16];
	d2printk("tda985x: In tda985x_set\n");
	
	if (chip == 9855)
	{
		dprintk(KERN_INFO 
			"tda985x: tda985x_set(0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x)\n",
			t->rvol,t->lvol,t->bass,t->treble,t->sub,
			t->c5,t->c6,t->c7,t->a1,t->a2,t->a3);
		buf[0]  = TDA9855_VR;
		buf[1]  = t->rvol;
		buf[2]  = t->lvol;
		buf[3]  = t->bass;
		buf[4]  = t->treble;
		buf[5]  = t->sub;
		buf[6]  = t->c5;
		buf[7]  = t->c6;
		buf[8]  = t->c7;
		buf[9]  = t->a1;
		buf[10] = t->a2;
		buf[11] = t->a3;
		if (12 != i2c_master_send(client,buf,12)) {
			printk(KERN_WARNING "tda985x: I/O error, trying tda985x_set\n");
			return -1;
		}
	}

	else if (chip == 9850)
	{
        	dprintk(KERN_INFO 
			"tda986x: tda985x_set(0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x)\n",
	               	 t->c4,t->c5,t->c6,t->c7,t->a1,t->a2,t->a3);
		buf[0]  = TDA9850_C4;
		buf[1]  = t->c4;
		buf[2]  = t->c5;
		buf[3]  = t->c6;
		buf[4]  = t->c7;
		buf[5]  = t->a1;
		buf[6]  = t->a2;
		buf[7]  = t->a3;
		if (8 != i2c_master_send(client,buf,8)) {
			printk(KERN_WARNING "tda985x: I/O error, trying tda985x_set\n");
			return -1;
	        }
	}

	return 0;
}

static void do_tda985x_init(struct i2c_client *client)
{
	struct tda985x *t = client->data;
	d2printk("tda985x: In tda985x_init\n");

	if (chip == 9855)
	{
		printk("tda985x: Using tda9855 options\n");
		t->rvol = 0x6f;		/* 0dB */
		t->lvol = 0x6f;		/* 0dB */
		t->bass = 0x0e;		/* 0dB */
		t->treble = (0x07 << 1);	/* 0dB */
		t->sub = 0x8 << 2;	/* 0dB */
		t->c5 = TDA9855_MUTE | TDA9855_AVL | 
			TDA9855_LOUD | TDA9855_INT;  
		/* Set Mute, AVL, Loudness off, Internal sound */
		t->c6 = TDA985x_STEREO | TDA9855_LINEAR |
			TDA9855_TZCM | TDA9855_VZCM;
		/* Stereo linear mode, also wait til zero crossings  */
		t->c7 = 0x07;		/* 0dB input gain */
	}

	else if (chip == 9850)
	{
		printk("tda985x: Using tda9850 options\n");
		t->c4 = 0x08;		/* Set stereo noise thresh to nominal */
		t->c5 = 0x08;		/* Set SAP noise threshold to nominal */
		t->c6 = TDA985x_STEREO;	/* Select Stereo mode for decoder */
		t->c7 = 0x07;		/* 0dB input gain */
	}

	/* The following is valid for both chip types */	
	t->a1 = 0x10;	/* Select nominal wideband expander */
	t->a2 = 0x10;	/* Select nominal spectral expander and 30mV trigger */
	t->a3 = 0x3;	/* Set: nominal timing current, 420ohm AVL attack */

	tda985x_set(client);
}

/* *********************** *
 * i2c interface functions *
 * *********************** */

static int tda985x_attach(struct i2c_adapter *adap, int addr,
			  unsigned short flags, int kind)
{
	struct tda985x *t;
	struct i2c_client *client;
	d2printk("tda985x: In tda985x_attach\n");
	client = kmalloc(sizeof *client,GFP_KERNEL);
	if (!client)
		return -ENOMEM;		
        memcpy(client,&client_template,sizeof(struct i2c_client));
        client->adapter = adap;
        client->addr = addr;
	
	client->data = t = kmalloc(sizeof *t,GFP_KERNEL);
	if (!t) {
		kfree(client);
		return -ENOMEM;
	}
	memset(t,0,sizeof *t);
	do_tda985x_init(client);
	MOD_INC_USE_COUNT;
	strcpy(client->name,"TDA985x");
	printk(KERN_INFO "tda985x: init\n");

	i2c_attach_client(client);
	return 0;
}

static int tda985x_probe(struct i2c_adapter *adap)
{
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, tda985x_attach);
	return 0;
}

static int tda985x_detach(struct i2c_client *client)
{
	struct tda985x *t  = client->data;

	do_tda985x_init(client);
	i2c_detach_client(client);
	
	kfree(t);
	kfree(client);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int tda985x_command(struct i2c_client *client,
			   unsigned int cmd, void *arg)
{
	struct tda985x *t = client->data;
	d2printk("tda985x: In tda985x_command\n");
#if 0
	__u16 *sarg = arg;
#endif

	switch (cmd) {
	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;
		dprintk("tda985x: VIDIOCGAUDIO\n");
		if (chip == 9855)
		{
			int left,right;

			va->flags |= VIDEO_AUDIO_VOLUME |
				VIDEO_AUDIO_BASS |
				VIDEO_AUDIO_TREBLE;

			/* min is 0x27 max is 0x7f, vstep is 2e8 */
			left = (t->lvol-0x27)*0x2e8;
			right = (t->rvol-0x27)*0x2e8;
			va->volume=MAX(left,right);
			va->balance=(32768*MIN(left,right))/
				(va->volume ? va->volume : 1);
			va->balance=(left<right)?
				(65535-va->balance) : va->balance;
			va->bass = (t->bass-0x6)*0xccc; /* min 0x6 max 0x19 */
			va->treble = ((t->treble>>1)-0x3)*0x1c71;
		}

		/* Valid for both chips: */
		{
			va->mode = ((TDA985x_STP | TDA985x_SAPP) & 
				    tda985x_read(client)) >> 4;
			/* Add mono mode regardless of SAP and stereo */
			/* Allows forced mono */
			va->mode |= VIDEO_SOUND_MONO;
		}

		break; /* VIDIOCGAUDIO case */
	}

	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;
		dprintk("tda985x: VIDEOCSAUDIO\n");
		if (chip == 9855)
		{
			int left,right;

			left = (MIN(65536 - va->balance,32768) *
				va->volume) / 32768;
			right = (MIN(va->balance,32768) *
				 va->volume) / 32768;
			t->lvol = left/0x2e8+0x27;
			t->rvol = right/0x2e8+0x27;
			t->bass = va->bass/0xccc+0x6;
			t->treble = (va->treble/0x1c71+0x3)<<1;
			tda985x_write(client,TDA9855_VL,t->lvol);
			tda985x_write(client,TDA9855_VR,t->rvol);
			tda985x_write(client,TDA9855_BA, t->bass);
			tda985x_write(client,TDA9855_TR,t->treble);
		}

		/* The following is valid for both chips */

		switch (va->mode) {
			case VIDEO_SOUND_MONO:
				dprintk("tda985x: VIDEO_SOUND_MONO\n");
				t->c6= TDA985x_MONO | (t->c6 & 0x3f);
				tda985x_write(client,TDA985x_C6,t->c6);
				break;
			case VIDEO_SOUND_STEREO:
				dprintk("tda985x: VIDEO_SOUND_STEREO\n");
				t->c6= TDA985x_STEREO | (t->c6 & 0x3f);
				tda985x_write(client,TDA985x_C6,t->c6); 
				break;
			case VIDEO_SOUND_LANG1:
				dprintk("tda985x: VIDEO_SOUND_LANG1\n");
				t->c6= TDA985x_SAP | (t->c6 & 0x3f);
				tda985x_write(client,TDA985x_C6,t->c6);
				break;
		} /* End of (va->mode) switch */
		
		break;

	} /* end of VIDEOCSAUDIO case */

	default: /* Not VIDEOCGAUDIO or VIDEOCSAUDIO */

		/* nothing */
		d2printk("tda985x: Default\n");

	} /* end of (cmd) switch */

	return 0;
}


static struct i2c_driver driver = {
        "i2c tda985x driver",
        I2C_DRIVERID_TDA9855, /* Get new one for TDA985x? */
        I2C_DF_NOTIFY,
	tda985x_probe,
        tda985x_detach,
        tda985x_command,
};

static struct i2c_client client_template =
{
        "(unset)",		/* name */
        -1,
        0,
        0,
        NULL,
        &driver
};

#ifdef MODULE
int init_module(void)
#else
int tda985x_init(void)
#endif
{
	if ( (chip != 9850) && (chip != 9855) )
	{
		printk(KERN_ERR "tda985x: chip parameter must be 9850 or 9855\n");
		return -EINVAL;
	}
	i2c_add_driver(&driver);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	i2c_del_driver(&driver);
}
#endif

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
