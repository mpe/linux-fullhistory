/*
 * For the TDA9855 chip (afaik, only the Diamond DTV2000 has this)
 * This driver will not complain if used with a TDA9850 or any 
 * other i2c device with the same address.
 *
 * Copyright (c) 1999 Steve VanDeBogart (vandebo@uclink.berkeley.edu)
 * This code is placed under the terms of the GNU General Public License
 * Based on tda8425.c by Greg Alexander (c) 1998
 *
 * TODO:
 *   Fix channel change bug - sound goes out when changeing channels, mute
 *                            and unmote to fix.
 *   Fine tune sound
 *   Get rest of capabilities into video_audio struct...
 *
 *  Revision: 0.1 
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
static int debug = 0; /* insmod parameter */

/* Addresses to scan */
#define I2C_TDA9855_L        0xb4
#define I2C_TDA9855_H        0xb6
static unsigned short normal_i2c[] = {I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {
    I2C_TDA9855_L >> 1,
    I2C_TDA9855_H >> 1,
    I2C_CLIENT_END};
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

struct tda9855 {
	int addr;
	int rvol, lvol;
	int bass, treble, sub;
	int c1, c2, c3;
	int a1, a2, a3;
};

static struct i2c_driver driver;
static struct i2c_client client_template;


#define dprintk  if (debug) printk

                                 /* subaddresses */
#define TDA9855_VR	0x00 /* Volume, right */
#define TDA9855_VL	0x01 /* Volume, left */
#define TDA9855_BA	0x02 /* Bass */
#define TDA9855_TR	0x03 /* Treble */
#define TDA9855_SW	0x04 /* Subwoofer - not connected on DTV2000 */
#define TDA9855_C1	0x05 /* Control 1 */
#define TDA9855_C2	0x06 /* Control 2 */
#define TDA9855_C3	0x07 /* Control 3 */
#define TDA9855_A1	0x08 /* Alignmnet 1*/
#define TDA9855_A2	0x09 /* Alignmnet 2*/
#define TDA9855_A3	0x0a /* Alignmnet 3*/
				 /* Masks for bits in subaddresses */
/* VR */ /* VL */
/* lower 7 bits control gain from -71dB (0x28) to 16dB (0x7f)
 * in 1dB steps - mute is 0x27 */

/* BA */ 
/* lower 5 bits control bass gain from -12dB (0x06) to 16.5dB (0x19)
 * in .5dB steps - 0 is 0x0E */

/* TR */
/* 4 bits << 1 control treble gain from -12dB (0x3) to 12dB (0xb)
 * in 3dB steps - 0 is 0x7 */

/* SW */
/* 4 bits << 2 control subwoofer/surraound gain from -14db (0x1) to 14db (0xf)
 * in 3dB steps - mute is 0x0 */

/* C1 */
#define TDA9855_MUTE	1<<7 /* GMU, Mute at outputs */
#define TDA9855_AVL	1<<6 /* AVL, Automatic Volume Level */
#define TDA9855_LOUD	1<<5 /* Loudness, 1==off */
#define TDA9855_SUR	1<<3 /* Surround / Subwoofer 1==.5(L-R) 0==.5(L+R) */
				/* Bits 0 to 3 select various combinations
                                 * of line in and line out, only the 
                                 * interesting ones are defined */
#define TDA9855_EXT	1<<2 /* Selects inputs LIR and LIL.  Pins 41 & 12 */
#define TDA9855_INT	0    /* Selects inputs LOR and LOL.  (internal) */

/* C2 */
#define TDA9855_SAP	3<<6 /* Selects SAP output, mute if not received */
#define TDA9855_STEREO	1<<6 /* Selects Stereo ouput, mono if not received */
#define TDA9855_MONO	0    /* Forces Mono output */
#define TDA9855_TZCM	1<<5 /* If set, don't mute till zero crossing */
#define TDA9855_VZCM	1<<4 /* If set, don't change volume till zero crossing*/
#define TDA9855_LMU	1<<3 /* Mute at LOR and LOL */
#define TDA9855_LINEAR	0    /* Linear Stereo */
#define TDA9855_PSEUDO	1    /* Pseudo Stereo */
#define TDA9855_SPAT_30	2    /* Spatial Stereo, 30% anti-phase crosstalk */
#define TDA9855_SPAT_50	3    /* Spatial Stereo, 52% anti-phase crosstalk */
#define TDA9855_E_MONO	7    /* Forced mono - mono select elseware, so useless*/

/* C3 */
/* lower 4 bits control input gain from -3.5dB (0x0) to 4dB (0xF)
 * in .5dB steps -  0 is 0x7 */

/* A1 and A2 (read/write) */
/* lower 5 bites are wideband and spectral expander alignment
 * from 0x00 to 0x1f - nominal at 0x0f and 0x10 (read/write) */
#define TDA9855_STP	1<<5 /* Stereo Pilot/detect (read-only) */
#define TDA9855_SAPP	1<<6 /* SAP Pilot/detect (read-only) */
#define TDA9855_STS	1<<7 /* Stereo trigger 1= <35mV 0= <30mV (write-only)*/

/* A3 */
/* lower 3 bits control timing current for alignment: -30% (0x0), -20% (0x1),
 * -10% (0x2), nominal (0x3), +10% (0x6), +20% (0x5), +30% (0x4) */
/* 2 bits << 5 control AVL attack time: 420ohm (0x0), 730ohm (0x2), 
 * 1200ohm (0x1), 2100ohm (0x3) */
#define TDA9855_ADJ	1<<7 /* Stereo adjust on/off (wideband and spectral) */


/* Begin code */

static int tda9855_write(struct i2c_client *client, int subaddr, int val)
{
	unsigned char buffer[2];
	
	buffer[0] = subaddr;
	buffer[1] = val;
	if (2 != i2c_master_send(client,buffer,2)) {
		printk(KERN_WARNING "tda9855: I/O error, trying (write %d 0x%x)\n",
		       subaddr, val);
		return -1;
	}
	return 0;
}

static int tda9855_read(struct i2c_client *client)
{
	unsigned char buffer;
	
	if (1 != i2c_master_recv(client,&buffer,1)) {
		printk(KERN_WARNING "tda9855: I/O error, trying (read)\n");
		return -1;
	}
	return buffer;
}

static int tda9855_set(struct i2c_client *client)
{
	struct tda9855 *t = client->data;
	unsigned char buf[16];
	
	dprintk(KERN_INFO "tda9855_set(0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x)\n",t->rvol,t->lvol,t->bass,t->treble,t->sub,
		t->c1,t->c2,t->c3,t->a1,t->a2,t->a3);
	buf[0]  = TDA9855_VR;
	buf[1]  = t->rvol;
	buf[2]  = t->lvol;
	buf[3]  = t->bass;
	buf[4]  = t->treble;
	buf[5]  = t->sub;
	buf[6]  = t->c1;
	buf[7]  = t->c2;
	buf[8]  = t->c3;
	buf[9]  = t->a1;
	buf[10] = t->a2;
	buf[11] = t->a3;
	if (12 != i2c_master_send(client,buf,12)) {
		printk(KERN_WARNING "tda9855: I/O error, trying tda9855_set\n");
		return -1;
	}
	return 0;
}

static void tda9855_init(struct i2c_client *client)
{
	struct tda9855 *t = client->data;

	t->rvol=0x6f;         /* 0dB */
	t->lvol=0x6f;		/* 0dB */
	t->bass=0x0e;         /* 0dB */
	t->treble=(0x07 << 1);  /* 0dB */
	t->sub=0x8 << 2;      /* 0dB */
	t->c1=TDA9855_MUTE | TDA9855_AVL | TDA9855_LOUD | TDA9855_INT;  
	/* Set Mute, AVL, Loudness off, Internal sound */
	t->c2=TDA9855_STEREO | TDA9855_LINEAR; /* Set Stereo liner mode */
	t->c3=0x07;           /* 0dB input gain */
	t->a1=0x10;	 	/* Select nominal wideband expander */
	t->a2=0x10;		/* Select nominal spectral expander and 30mV trigger */
	t->a3=0x3;            /* Set: nominal timinig current, 420ohm AVL attack */
	tda9855_write(client, TDA9855_C1, TDA9855_MUTE); /* mute */
	tda9855_set(client);
}

/* *********************** *
 * i2c interface functions *
 * *********************** */

static int tda9855_attach(struct i2c_adapter *adap, int addr,
			  unsigned short flags, int kind)
{
	struct tda9855 *t;
	struct i2c_client *client;

	client = kmalloc(sizeof *client,GFP_KERNEL);
	if (!client)
		return -ENOMEM;		
        memcpy(client,&client_template,sizeof(struct i2c_client));
        client->adapter = adap;
        client->addr = addr;
	
	client->data = t = kmalloc(sizeof *t,GFP_KERNEL);
	if (!t)
		return -ENOMEM;
	memset(t,0,sizeof *t);
	tda9855_init(client);
	MOD_INC_USE_COUNT;
	strcpy(client->name,"TDA9855");
	printk(KERN_INFO "tda9855: init\n");

	i2c_attach_client(client);
	return 0;
}

static int tda9855_probe(struct i2c_adapter *adap)
{
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, tda9855_attach);
	return 0;
}

static int tda9855_detach(struct i2c_client *client)
{
	struct tda9855 *t  = client->data;

	tda9855_init(client);
	i2c_detach_client(client);
	
	kfree(t);
	kfree(client);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int tda9855_command(struct i2c_client *client,
			   unsigned int cmd, void *arg)
{
	struct tda9855 *t = client->data;
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
		va->bass = (t->bass-0x6)*0xccc; /* min 0x6 max is 0x19 */
		va->treble = ((t->treble>>1)-0x3)*0x1c71;

		va->mode = ((TDA9855_STP | TDA9855_SAPP) & 
			    tda9855_read(client)) >> 4;
		if (0 == va->mode)
			va->mode = VIDEO_SOUND_MONO;
		break;
	}
	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;
		int left,right;

		left = (MIN(65536 - va->balance,32768) *
			va->volume) / 32768;
		right = (MIN(va->balance,32768) *
			 va->volume) / 32768;
		t->lvol = left/0x2e8+0x27;
		t->rvol = right/0x2e8+0x27;
		t->bass = va->bass/0xccc+0x6;
		t->treble = (va->treble/0x1c71+0x3)<<1;
		tda9855_write(client,TDA9855_VL,t->lvol);
		tda9855_write(client,TDA9855_VR,t->rvol);
		tda9855_write(client,TDA9855_BA, t->bass);
		tda9855_write(client,TDA9855_TR,t->treble);

		switch (va->mode) {
		case VIDEO_SOUND_MONO:
			t->c2= TDA9855_MONO | (t->c2 & 0x3f);
			break;
		case VIDEO_SOUND_STEREO:
			t->c2= TDA9855_STEREO | (t->c2 & 0x3f); 
			break;
		case VIDEO_SOUND_LANG2:
			t->c2= TDA9855_SAP | (t->c2 & 0x3f); 
			break;
		}
		tda9855_write(client,TDA9855_C2,t->c2);
		break;
	}

#if 0
	/* --- old, obsolete interface --- */
	case AUDC_GET_VOLUME_LEFT:
		*sarg = (t->lvol-0x27)*0x2e8; /* min is 0x27 max is 0x7f, vstep is 2e8 */
		break;
	case AUDC_GET_VOLUME_RIGHT:
		*sarg = (t->rvol-0x27)*0x2e8;
		break;
	case AUDC_SET_VOLUME_LEFT:
		t->lvol = *sarg/0x2e8+0x27;
		break;
	case AUDC_SET_VOLUME_RIGHT:
		t->rvol = *sarg/0x2e8+0x27;
		break;
	case AUDC_GET_BASS:
		*sarg = (t->bass-0x6)*0xccc; /* min 0x6 max is 0x19 */
		break;
	case AUDC_SET_BASS:
		t->bass = *sarg/0xccc+0x6;
		tda9855_write(client,TDA9855_BA, t->bass);
		break;
	case AUDC_GET_TREBLE:
		*sarg = ((t->treble>>1)-0x3)*0x1c71;
		break;
	case AUDC_SET_TREBLE:
		t->treble = (*sarg/0x1c71+0x3)<<1;
		tda9855_write(client,TDA9855_TR,t->treble);
		break;
	case AUDC_GET_STEREO:
		*sarg = ((TDA9855_STP | TDA9855_SAPP) & 
			 tda9855_read(client)) >> 4;
		if(*sarg==0) *sarg=VIDEO_SOUND_MONO;
		break;
	case AUDC_SET_STEREO:
		if(*sarg==VIDEO_SOUND_MONO)
			t->c2= TDA9855_MONO | (t->c2 & 0x3f); 
		/* Mask out the sap and stereo bits and set mono */
		else if(*sarg==VIDEO_SOUND_STEREO)
			t->c2= TDA9855_STEREO | (t->c2 & 0x3f); 
		/* Mask out the sap and stereo bits and set stereo */
		else if(*sarg==VIDEO_SOUND_LANG2)
			t->c2= TDA9855_SAP | (t->c2 & 0x3f); 
		/* Mask out the sap and stereo bits and set sap */
		tda9855_write(client,TDA9855_C2,t->c2);
		break;
	case AUDC_SET_INPUT:
		dprintk(KERN_INFO "tda9855: SET_INPUT with 0x%04x\n",*sarg);
		if((*sarg & (AUDIO_MUTE | AUDIO_OFF))!=0)
			t->c1|=TDA9855_MUTE;
		else
			t->c1= t->c1 & 0x7f;  /* won't work -->  (~TDA9855_MUTE); */
		if((*sarg & AUDIO_INTERN) == AUDIO_INTERN)
			t->c1=(t->c1 & ~0x7) | TDA9855_INT;  /* 0x7 is a mask for the int/ext */
		if((*sarg & AUDIO_EXTERN) == AUDIO_EXTERN)
			t->c1=(t->c1 & ~0x7) | TDA9855_EXT;  /* 0x7 is a mask for the int/ext */
		tda9855_write(client,TDA9855_C1,t->c1);
		break;
    case AUDC_SWITCH_MUTE:
	    if((t->c1 & ~TDA9855_MUTE) == 0)
		    t->c1|=TDA9855_MUTE;
	    else
		    t->c1&=~TDA9855_MUTE;
	    tda9855_write(client,TDA9855_C1,t->c1);
	    break;
	    
/* TDA9855 unsupported: */
/*	case AUDC_NEWCHANNEL:
	case AUDC_SET_RADIO:
	case AUDC_GET_DC:
*/
#endif
	default:
		/* nothing */
	}
	return 0;
}


static struct i2c_driver driver = {
        "i2c tda9855 driver",
        I2C_DRIVERID_TDA9855, /* FIXME */
        I2C_DF_NOTIFY,
	tda9855_probe,
        tda9855_detach,
        tda9855_command,
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
int tda9855_init(void)
#endif
{
	i2c_add_driver(&driver);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	i2c_add_driver(&driver);
}
#endif

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
