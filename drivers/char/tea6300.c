/*
 * for the TEA6300 chip (only found on Gateway STB TV/FM cards tho the best
 * of my knowledge)
 * WARNING: THIS DRIVER WILL LOAD WITHOUT COMPLAINTS EVEN IF THE WRONG
 * CHIP (i.e., an MSP3400) IS ON I2C ADDRESS 0x80 (it relies on i2c to
 * make sure that there is a device acknowledging that address).  This
 * is a potential problem because the MSP3400 is very popular and does
 * use this address!  You have been warned!
 *
 * Copyright (c) 1998 Greg Alexander <galexand@acm.org>
 * This code is placed under the terms of the GNU General Public License
 * Code liberally copied from msp3400.c, which is by Gerd Knorr
 *
 * All of this should work, though it would be nice to eventually support
 * balance (different left,right values) and, if someone ever finds a card
 * with the support (or if you're careful with a soldering iron), fade
 * (front/back).
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
#define I2C_TEA6300	0x80
static unsigned short normal_i2c[] = {
    I2C_TEA6300 >> 1,
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


struct tea6300 {
	int mode;		/* set to AUDIO_{OFF,TUNER,RADIO,EXTERN} */
	int stereo;
	__u16 left,right;
	__u16 bass,treble;
};

static struct i2c_driver driver;
static struct i2c_client client_template;

#define TEA6300_VL         0x00  /* volume left */
#define TEA6300_VR         0x01  /* volume right */
#define TEA6300_BA         0x02  /* bass */
#define TEA6300_TR         0x03  /* treble */
#define TEA6300_FA         0x04  /* fader control */
#define TEA6300_S          0x05  /* switch register */
                                 /* values for those registers: */
#define TEA6300_S_SA       0x01  /* stereo A input */
#define TEA6300_S_SB       0x02  /* stereo B */
#define TEA6300_S_SC       0x04  /* stereo C */
#define TEA6300_S_GMU      0x80  /* general mute */


/* ******************************** *
 * functions for talking to TEA6300 *
 * ******************************** */

static int tea6300_write(struct i2c_client *client, int addr, int val)
{
	unsigned char buffer[2];

	buffer[0] = addr;
	buffer[1] = val;
	if (2 != i2c_master_send(client,buffer,2)) {
		printk(KERN_WARNING "tea6300: I/O error, trying (write %d 0x%x)\n",
		       addr, val);
		return -1;
	}
	return 0;
}

static void tea6300_set(struct i2c_client *client)
{
	struct tea6300 *tea = client->data;

	/* mode is ignored today */
	dprintk(KERN_DEBUG "tea6300_set(%04x,%04x,%04x,%04x)\n",tea->left>>10,tea->right>>10,tea->bass>>12,tea->treble>>12);
	tea6300_write(client, TEA6300_VL, tea->left>>10  );
	tea6300_write(client, TEA6300_VR, tea->right>>10 );
	tea6300_write(client, TEA6300_BA, tea->bass>>12  );
	tea6300_write(client, TEA6300_TR, tea->treble>>12);
}

static void tea6300_init(struct i2c_client *client)
{
	struct tea6300 *tea = client->data;
	
	tea->left=tea->right =49152;  /* -10dB (loud enough, but not beyond
	                                 normal line levels - so as to avoid
	                                 clipping */
	tea->bass=tea->treble=28672;  /* 0dB */
	tea->mode=AUDIO_OFF;
	tea->stereo=1;
	/* left=right=0x27<<10, bass=treble=0x07<<12 */
	tea6300_write(client, TEA6300_FA, 0x3f         ); /* fader off */
	tea6300_write(client, TEA6300_S , TEA6300_S_GMU); /* mute */
	tea6300_set(client);
}

static void tea6300_audio(struct i2c_client *client, int mode)
{
	struct tea6300 *tea = client->data;
	
	/* valid for AUDIO_TUNER, RADIO, EXTERN, OFF */
	dprintk(KERN_DEBUG "tea6300_audio:%d (T,R,E,I,O)\n",mode);
	tea->mode=mode;
	if (mode==AUDIO_OFF) {	/* just mute it */
		tea6300_write(client, TEA6300_S, TEA6300_S_GMU);
		return;
	}
	switch(mode) {
		case AUDIO_TUNER:
			tea6300_write(client, TEA6300_S, TEA6300_S_SA);
			break;
		case AUDIO_RADIO:
			tea6300_write(client, TEA6300_S, TEA6300_S_SB);
			break;
		case AUDIO_EXTERN:
			tea6300_write(client, TEA6300_S, TEA6300_S_SC);
			break;
	}
}


/* *********************** *
 * i2c interface functions *
 * *********************** */

static int tea6300_attach(struct i2c_adapter *adap, int addr,
			  unsigned short flags, int kind)
{
	struct tea6300 *tea;
	struct i2c_client *client;

	client = kmalloc(sizeof *client,GFP_KERNEL);
	if (!client)
		return -ENOMEM;		
        memcpy(client,&client_template,sizeof(struct i2c_client));
        client->adapter = adap;
        client->addr = addr;

	client->data = tea = kmalloc(sizeof *tea,GFP_KERNEL);
	if (!tea)
		return -ENOMEM;
	memset(tea,0,sizeof *tea);
	tea6300_init(client);

	MOD_INC_USE_COUNT;
	strcpy(client->name,"TEA6300T");
	printk(KERN_INFO "tea6300: initialized\n");

	i2c_attach_client(client);
	return 0;
}

static int tea6300_probe(struct i2c_adapter *adap)
{	
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, tea6300_attach);
	return 0;
}

static int tea6300_detach(struct i2c_client *client)
{
	struct tea6300 *tea  = client->data;
    
	tea6300_init(client);
	i2c_detach_client(client);

	kfree(tea);
	kfree(client);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
tea6300_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tea6300 *tea = client->data;
        __u16 *sarg = arg;

	switch (cmd) {
	case AUDC_SET_RADIO:
		tea6300_audio(client,AUDIO_RADIO);
		break;
	case AUDC_SET_INPUT:
		tea6300_audio(client,*sarg);
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
		va->volume=MAX(tea->left,tea->right);
		va->balance=(32768*MIN(tea->left,tea->right))/
			(va->volume ? va->volume : 1);
		va->balance=(tea->left<tea->right)?
			(65535-va->balance) : va->balance;
		va->bass = tea->bass;
		va->treble = tea->treble;
		break;
	}
	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;

		tea->left = (MIN(65536 - va->balance,32768) *
			     va->volume) / 32768;
		tea->right = (MIN(va->balance,32768) *
			      va->volume) / 32768;
		tea->bass = va->bass;
		tea->treble = va->treble;
		tea6300_set(client);
		break;
	}
#if 0
	/* --- old, obsolete interface --- */
	case AUDC_GET_VOLUME_LEFT:
		*sarg = tea->left;
		break;
	case AUDC_GET_VOLUME_RIGHT:
		*sarg = tea->right;
		break;
	case AUDC_SET_VOLUME_LEFT:
		tea->left = *sarg;
		tea6300_set(client);
		break;
	case AUDC_SET_VOLUME_RIGHT:
		tea->right = *sarg;
		tea6300_set(client);
		break;

	case AUDC_GET_BASS:
		*sarg = tea->bass;
		break;
	case AUDC_SET_BASS:
		tea->bass = *sarg;
		tea6300_set(client);
		break;

	case AUDC_GET_TREBLE:
		*sarg = tea->treble;
		break;
	case AUDC_SET_TREBLE:
		tea->treble = *sarg;
		tea6300_set(client);
		break;

	case AUDC_GET_STEREO:
		*sarg = tea->stereo?VIDEO_SOUND_STEREO:VIDEO_SOUND_MONO;
		break;
	case AUDC_SET_STEREO:
		tea->stereo=(*sarg==VIDEO_SOUND_MONO)?0:1;
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
        "i2c tea6300 driver",
        I2C_DRIVERID_TEA6300,
        I2C_DF_NOTIFY,
	tea6300_probe,
        tea6300_detach,
        tea6300_command,
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
int tea6300_init(void)
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
