/*
 * for the TDA8425 chip (I don't know which cards have this)
 * WARNING: THIS DRIVER WILL LOAD WITHOUT COMPLAINTS EVEN IF A DIFFERENT
 * CHIP IS AT ADDRESS 0x82 (it relies on i2c to make sure that there is a
 * device acknowledging that address)
 *
 * Copyright (c) 1998 Greg Alexander <galexand@acm.org>
 * This code is placed under the terms of the GNU General Public License
 * Code liberally copied from msp3400.c, which is by Gerd Knorr
 *
 * All of this should work, though it would be nice to eventually support
 * balance (different left,right values).  Also, the chip seems (?) to have
 * two stereo inputs, so if someone has this card, could they tell me if the
 * second one can be used for anything (i.e., does it have an external input
 * that you can't hear even if you set input to composite?)
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

/* Addresses to scan */
#define I2C_TDA8425        0x82
static unsigned short normal_i2c[] = {
    I2C_TDA8425 >> 1,
    I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {I2C_CLIENT_END};
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

MODULE_PARM(debug,"i");
static int debug = 0; /* insmod parameter */
#define dprintk  if (debug) printk


struct tda8425 {
	int mode;		/* set to AUDIO_{OFF,TUNER,RADIO,EXTERN} */
	int stereo;
	__u16 left,right;
	__u16 bass,treble;
};

static struct i2c_driver driver;
static struct i2c_client client_template;


#define TDA8425_VL         0x00  /* volume left */
#define TDA8425_VR         0x01  /* volume right */
#define TDA8425_BA         0x02  /* bass */
#define TDA8425_TR         0x03  /* treble */
#define TDA8425_S1         0x08  /* switch functions */
                                 /* values for those registers: */
#define TDA8425_S1_OFF     0xEE  /* audio off (mute on) */
#define TDA8425_S1_ON      0xCE  /* audio on (mute off) - "linear stereo" mode */


/* ******************************** *
 * functions for talking to TDA8425 *
 * ******************************** */

static int tda8425_write(struct i2c_client *client, int addr, int val)
{
	unsigned char buffer[2];
	
	buffer[0] = addr;
	buffer[1] = val;
	if (2 != i2c_master_send(client,buffer,2)) {
		printk(KERN_WARNING "tda8425: I/O error, trying (write %d 0x%x)\n",
		       addr, val);
		return -1;
	}
	return 0;
}

static void tda8425_set(struct i2c_client *client)
{
	struct tda8425 *tda = client->data;

	/* mode is ignored today */
	dprintk(KERN_DEBUG "tda8425_set(%04x,%04x,%04x,%04x)\n",tda->left>>10,tda->right>>10,tda->bass>>12,tda->treble>>12);
	tda8425_write(client, TDA8425_VL, tda->left>>10  |0xC0);
	tda8425_write(client, TDA8425_VR, tda->right>>10 |0xC0);
	tda8425_write(client, TDA8425_BA, tda->bass>>12  |0xF0);
	tda8425_write(client, TDA8425_TR, tda->treble>>12|0xF0);
}

static void tda8425_init(struct i2c_client *client)
{
	struct tda8425 *tda = client->data;

	tda->left=tda->right =61440;  /* 0dB */
	tda->bass=tda->treble=24576;  /* 0dB */
	tda->mode=AUDIO_OFF;
	tda->stereo=1;
	/* left=right=0x27<<10, bass=treble=0x07<<12 */
	tda8425_write(client, TDA8425_S1, TDA8425_S1_OFF); /* mute */
	tda8425_set(client);
}

static void tda8425_audio(struct i2c_client *client, int mode)
{
	struct tda8425 *tda = client->data;

	/* valid for AUDIO_TUNER, RADIO, EXTERN, OFF */
	dprintk(KERN_DEBUG "tda8425_audio:%d (T,R,E,I,O)\n",mode);
	tda->mode=mode;
	tda8425_write(client, TDA8425_S1,
	              (mode==AUDIO_OFF)?TDA8425_S1_OFF:TDA8425_S1_ON);
	/* this is the function we'll need to change if it turns out the
	 * input-selecting capabilities should be used. */
}


/* *********************** *
 * i2c interface functions *
 * *********************** */

static int tda8425_attach(struct i2c_adapter *adap, int addr,
			  unsigned short flags, int kind)
{
	struct tda8425 *tda;
	struct i2c_client *client;

	client = kmalloc(sizeof *client,GFP_KERNEL);
	if (!client)
		return -ENOMEM;		
        memcpy(client,&client_template,sizeof(struct i2c_client));
        client->adapter = adap;
        client->addr = addr;

	client->data = tda = kmalloc(sizeof *tda,GFP_KERNEL);
	if (!tda)
		return -ENOMEM;
	memset(tda,0,sizeof *tda);
	tda8425_init(client);
	MOD_INC_USE_COUNT;
	strcpy(client->name,"TDA8425");
	printk(KERN_INFO "tda8425: init\n");

	i2c_attach_client(client);
	return 0;
}

static int tda8425_probe(struct i2c_adapter *adap)
{	
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, tda8425_attach);
	return 0;
}


static int tda8425_detach(struct i2c_client *client)
{
	struct tda8425 *tda  = client->data;
    
	tda8425_init(client);
	i2c_detach_client(client);

	kfree(tda);
	kfree(client);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int tda8425_command(struct i2c_client *client,
			   unsigned int cmd, void *arg)
{
	struct tda8425 *tda = client->data;
        __u16 *sarg = arg;

	switch (cmd) {
	case AUDC_SET_RADIO:
		tda8425_audio(client,AUDIO_RADIO);
		break;
	case AUDC_SET_INPUT:
		tda8425_audio(client,*sarg);
		break;

	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;

		va->flags |= VIDEO_AUDIO_VOLUME |
			VIDEO_AUDIO_BASS |
			VIDEO_AUDIO_TREBLE;
		va->volume=MAX(tda->left,tda->right);
		va->balance=(32768*MIN(tda->left,tda->right))/
			(va->volume ? va->volume : 1);
		va->balance=(tda->left<tda->right)?
			(65535-va->balance) : va->balance;
		va->bass = tda->bass;
		va->treble = tda->treble;
		break;
	}
	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;

		tda->left = (MIN(65536 - va->balance,32768) *
			     va->volume) / 32768;
		tda->right = (MIN(va->balance,32768) *
			      va->volume) / 32768;
		tda->bass = va->bass;
		tda->treble = va->treble;
		tda8425_set(client);
		break;
	}

#if 0
	/* --- old, obsolete interface --- */
	case AUDC_GET_VOLUME_LEFT:
		*sarg = tda->left;
		break;
	case AUDC_GET_VOLUME_RIGHT:
		*sarg = tda->right;
		break;
	case AUDC_SET_VOLUME_LEFT:
		tda->left = *sarg;
		tda8425_set(client);
		break;
	case AUDC_SET_VOLUME_RIGHT:
		tda->right = *sarg;
		tda8425_set(client);
		break;

	case AUDC_GET_BASS:
		*sarg = tda->bass;
		break;
	case AUDC_SET_BASS:
		tda->bass = *sarg;
		tda8425_set(client);
		break;

	case AUDC_GET_TREBLE:
		*sarg = tda->treble;
		break;
	case AUDC_SET_TREBLE:
		tda->treble = *sarg;
		tda8425_set(client);
		break;

	case AUDC_GET_STEREO:
		*sarg = tda->stereo?VIDEO_SOUND_STEREO:VIDEO_SOUND_MONO;
		break;
	case AUDC_SET_STEREO:
		tda->stereo=(*sarg==VIDEO_SOUND_MONO)?0:1;
		/* TODO: make this write to the TDA9850? */
		break;

/*	case AUDC_SWITCH_MUTE:	someday, maybe -- not a lot of point to
	case AUDC_NEWCHANNEL:	it and it would require preserving state
	case AUDC_GET_DC:	huh?? (not used by bttv.c)
*/
#endif
	default:
		/* nothing */
	}
	return 0;
}


static struct i2c_driver driver = {
        "i2c tda8424 driver",
        I2C_DRIVERID_TDA8425,
        I2C_DF_NOTIFY,
	tda8425_probe,
        tda8425_detach,
        tda8425_command,
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
int tda8425_init(void)
#endif
{
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
