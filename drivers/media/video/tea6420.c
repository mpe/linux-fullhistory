/*
 * for the TEA6420 chip (only found on 3DFX (STB) TV/FM cards to the best
 * of my knowledge)
 * Copyright (C) 2000 Dave Stuart <justdave@ynn.com>
 * This code is placed under the terms of the GNU General Public License
 * Code liberally copied from tea6300 by . . .
 *
 * Copyright (c) 1998 Greg Alexander <galexand@acm.org>
 * This code is placed under the terms of the GNU General Public License
 * Code liberally copied from msp3400.c, which is by Gerd Knorr
 *
 * Changes:
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 08/14/2000
 * - resource allocation fixes in tea6300_attach
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
#define I2C_TEA6420	0x98
static unsigned short normal_i2c[] = {
    I2C_TEA6420 >> 1,
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


struct tea6420 {
	int mode;		/* set to AUDIO_{OFF,TUNER,RADIO,EXTERN} */
	int stereo;
};

static struct i2c_driver driver;
static struct i2c_client client_template;

#define TEA6420_S_SA       0x00  /* stereo A input */
#define TEA6420_S_SB       0x01  /* stereo B */
#define TEA6420_S_SC       0x02  /* stereo C */
#define TEA6420_S_SD       0x03  /* stereo D */
#define TEA6420_S_SE       0x04  /* stereo E */
#define TEA6420_S_GMU      0x05  /* general mute */


/* ******************************** *
 * functions for talking to TEA6420 *
 * ******************************** */

static int tea6420_write(struct i2c_client *client, int val)
{
	unsigned char buffer[2];
	int result;

/*	buffer[0] = addr; */
	buffer[0] = val;
	result = i2c_master_send(client,buffer,1);
	if (1 != result) {
		printk(KERN_WARNING "tea6420: I/O error, trying (write
0x%x) result = %d\n", val, result);
		return -1;
	}
	return 0;
}


static void do_tea6420_init(struct i2c_client *client)
{
	struct tea6420 *tea = client->data;
	
	tea->mode=AUDIO_OFF;
	tea->stereo=1;
	tea6420_write(client, TEA6420_S_GMU); /* mute */
}

static void tea6420_audio(struct i2c_client *client, int mode)
{
	struct tea6420 *tea = client->data;
	
	/* valid for AUDIO_TUNER, RADIO, EXTERN, OFF */
	dprintk(KERN_DEBUG "tea6420_audio:%d (T,R,E,I,O)\n",mode);
	tea->mode=mode;
	if (mode==AUDIO_OFF) {	/* just mute it */
		tea6420_write(client, TEA6420_S_GMU);
		return;
	}
	switch(mode) {
		case AUDIO_TUNER:
			tea6420_write(client, TEA6420_S_SA);
			break;
		case AUDIO_RADIO:
			tea6420_write(client, TEA6420_S_SB);
			break;
		case AUDIO_EXTERN:
			tea6420_write(client, TEA6420_S_SC);
			break;
	}
}


/* *********************** *
 * i2c interface functions *
 * *********************** */

static int tea6420_attach(struct i2c_adapter *adap, int addr,
			  unsigned short flags, int kind)
{
	struct tea6420 *tea;
	struct i2c_client *client;

	client = kmalloc(sizeof *client,GFP_KERNEL);
	if (!client)
		return -ENOMEM;		
        memcpy(client,&client_template,sizeof(struct i2c_client));
        client->adapter = adap;
        client->addr = addr;

	client->data = tea = kmalloc(sizeof *tea,GFP_KERNEL);
	if (!tea) {
		kfree(client);
		return -ENOMEM;
	}
	memset(tea,0,sizeof *tea);
	do_tea6420_init(client);

	MOD_INC_USE_COUNT;
	strcpy(client->name,"TEA6420");
	printk(KERN_INFO "tea6420: initialized\n");

	i2c_attach_client(client);
	return 0;
}

static int tea6420_probe(struct i2c_adapter *adap)
{	
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, tea6420_attach);
	return 0;
}

static int tea6420_detach(struct i2c_client *client)
{
	struct tea6420 *tea  = client->data;
    
	do_tea6420_init(client);
	i2c_detach_client(client);

	kfree(tea);
	kfree(client);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
tea6420_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
        __u16 *sarg = arg;

	switch (cmd) {
	case AUDC_SET_RADIO:
		tea6420_audio(client,AUDIO_RADIO);
		break;
	case AUDC_SET_INPUT:
		tea6420_audio(client,*sarg);
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
/*		va->volume=MAX(tea->left,tea->right);
		va->balance=(32768*MIN(tea->left,tea->right))/
			(va->volume ? va->volume : 1);
		va->balance=(tea->left<tea->right)?
			(65535-va->balance) : va->balance;
		va->bass = tea->bass;
		va->treble = tea->treble;
*/		break;
	}
	case VIDIOCSAUDIO:
	{

/*		tea->left = (MIN(65536 - va->balance,32768) *
			     va->volume) / 32768;
		tea->right = (MIN(va->balance,32768) *
			      va->volume) / 32768;
		tea->bass = va->bass;
		tea->treble = va->treble;
		tea6420_set(client);
*/		break;
	}

default:
		/* nothing */
	}
	return 0;
}

static struct i2c_driver driver = {
        "i2c tea6420 driver",
        I2C_DRIVERID_TEA6420,
        I2C_DF_NOTIFY,
	tea6420_probe,
        tea6420_detach,
        tea6420_command,
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
int tea6420_init(void)
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
