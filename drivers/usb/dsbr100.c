/* A driver for the D-Link DSB-R100 USB radio.  The R100 plugs
 into both the USB and an analog audio input, so this thing
 only deals with initialisation and frequency setting, the
 audio data has to be handled by a sound driver.

 Major issue: I can't find out where the device reports the signal
 strength, and indeed the windows software appearantly just looks
 at the stereo indicator as well.  So, scanning will only find
 stereo stations.  Sad, but I can't help it.

 Also, the windows program sends oodles of messages over to the
 device, and I couldn't figure out their meaning.  My suspicion
 is that they don't have any:-)

 You might find some interesting stuff about this module at
 http://unimut.fsk.uni-heidelberg.de/unimut/demi/dsbr

 Copyright (c) 2000 Markus Demleitner

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

 History:

 Version 0.21:
 	Markus Demleitner <msdemlei@tucana.harvard.edu>:
	Minor cleanup, warnings if something goes wrong, lame attempt
	to adhere to Documentation/CodingStyle

 Version 0.2: 
 	Brad Hards <bradh@dynamite.com.au>: Fixes to make it work as non-module
	Markus: Copyright clarification

 Version 0.01: Markus: initial release

*/


#include <linux/kernel.h>

#if CONFIG_MODVERSIONS==1
#define MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/input.h>
#include <linux/videodev.h>
#include <linux/usb.h>

#define DSB100_VENDOR 0x04b4
#define DSB100_PRODUCT 0x1002

#define TB_LEN 16

static void *usb_dsbr100_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_dsbr100_disconnect(struct usb_device *dev, void *ptr);
static int usb_dsbr100_ioctl(struct video_device *dev, unsigned int cmd, 
	void *arg);
static int usb_dsbr100_open(struct video_device *dev, int flags);
static void usb_dsbr100_close(struct video_device *dev);


typedef struct
{	struct urb readurb,writeurb;
	struct usb_device *dev;
	char transfer_buffer[TB_LEN];
	int curfreq;
	int stereo;
	int ifnum;
} usb_dsbr100;


static struct video_device usb_dsbr100_radio=
{
	"D-Link DSB R-100 USB radio",
	VID_TYPE_TUNER,
	VID_HARDWARE_AZTECH,
	usb_dsbr100_open,
	usb_dsbr100_close,
	NULL,	/* Can't read  (no capture ability) */
	NULL,	/* Can't write */
	NULL,	/* No poll */
	usb_dsbr100_ioctl,
	NULL,
	NULL
};

static int users = 0;

static struct usb_driver usb_dsbr100_driver = {
	name:		"dsbr100",
	probe:		usb_dsbr100_probe,
	disconnect:	usb_dsbr100_disconnect,
	driver_list: {NULL,NULL},
	fops: NULL,
	minor: 0
};


static int dsbr100_start(usb_dsbr100 *radio)
{
	if (usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x00, 0xC0, 0x00, 0xC7, radio->transfer_buffer, 8, 300)<0 ||
	    usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x02, 0xC0, 0x01, 0x00, radio->transfer_buffer, 8, 300)<0)
		return -1;
	return (radio->transfer_buffer)[0];
}


static int dsbr100_stop(usb_dsbr100 *radio)
{
	if (usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x00, 0xC0, 0x16, 0x1C, radio->transfer_buffer, 8, 300)<0 ||
	    usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x02, 0xC0, 0x00, 0x00, radio->transfer_buffer, 8, 300)<0)
		return -1;
	return (radio->transfer_buffer)[0];
}


static int dsbr100_setfreq(usb_dsbr100 *radio, int freq)
{
	freq = (freq*80)/16+856;
	if (usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x01, 0xC0, (freq&0xff00)>>8, freq&0xff, 
		radio->transfer_buffer, 8, 300)<0
	 || usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x00, 0xC0, 0x96, 0xB7, radio->transfer_buffer, 8, 300)<0 ||
	    usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x00, 0xC0, 0x00, 0x24, radio->transfer_buffer, 8, 300)<0) {
		radio->stereo = -1;
		return -1;
	}
	radio->stereo = ! ((radio->transfer_buffer)[0]&0x01);
	return (radio->transfer_buffer)[0];
}

static void dsbr100_getstat(usb_dsbr100 *radio)
{
	if (usb_control_msg(radio->dev, usb_rcvctrlpipe(radio->dev, 0),
		0x00, 0xC0, 0x00 , 0x24, radio->transfer_buffer, 8, 300)<0)
		radio->stereo = -1;
	else
		radio->stereo = ! (radio->transfer_buffer[0]&0x01);
}


static void *usb_dsbr100_probe(struct usb_device *dev, unsigned int ifnum)
{
	usb_dsbr100 *radio;

	if (dev->descriptor.idVendor!=DSB100_VENDOR ||
			dev->descriptor.idProduct!=DSB100_PRODUCT)
		return NULL;
	if (!(radio = kmalloc(sizeof(usb_dsbr100),GFP_KERNEL)))
		return NULL;
	usb_dsbr100_radio.priv = radio;
	radio->dev = dev;
	radio->ifnum = ifnum;
	radio->curfreq = 1454;
	return (void*)radio;
}

static void usb_dsbr100_disconnect(struct usb_device *dev, void *ptr)
{
	usb_dsbr100 *radio=ptr;

	if (users)
		return;
	kfree(radio);
	usb_dsbr100_radio.priv = NULL;
}

static int usb_dsbr100_ioctl(struct video_device *dev, unsigned int cmd, 
	void *arg)
{
	usb_dsbr100 *radio=dev->priv;

	if (!radio)
		return -EINVAL;

	switch(cmd)
	{
		case VIDIOCGCAP: {
			struct video_capability v;
			v.type=VID_TYPE_TUNER;
			v.channels=1;
			v.audios=1;
			/* No we don't do pictures */
			v.maxwidth=0;
			v.maxheight=0;
			v.minwidth=0;
			v.minheight=0;
			strcpy(v.name, "D-Link R-100 USB Radio");
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGTUNER: {
			struct video_tuner v;
			dsbr100_getstat(radio);
			if(copy_from_user(&v, arg,sizeof(v))!=0) 
				return -EFAULT;
			if(v.tuner)	/* Only 1 tuner */ 
				return -EINVAL;
			v.rangelow=(87*16000);
			v.rangehigh=(108*16000);
			/*v.flags=VIDEO_TUNER_LOW;*/
			v.mode=VIDEO_MODE_AUTO;
			v.signal=radio->stereo;
			v.flags|=VIDEO_TUNER_STEREO_ON;
			strcpy(v.name, "FM");
			if(copy_to_user(arg,&v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSTUNER: {
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if(v.tuner!=0)
				return -EINVAL;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
		case VIDIOCGFREQ: 
			if (radio->curfreq==-1)
				return -EINVAL;
			if(copy_to_user(arg, &(radio->curfreq), 
				sizeof(radio->curfreq)))
				return -EFAULT;
			return 0;

		case VIDIOCSFREQ:
			if(copy_from_user(&(radio->curfreq), arg,
				sizeof(radio->curfreq)))
				return -EFAULT;
			if (dsbr100_setfreq(radio, radio->curfreq)==-1)
				warn("set frequency failed");
			return 0;

		case VIDIOCGAUDIO: {
			struct video_audio v;
			memset(&v,0, sizeof(v));
			v.flags|=VIDEO_AUDIO_MUTABLE;
			v.mode=VIDEO_SOUND_STEREO;
			v.volume=1;
			v.step=1;
			strcpy(v.name, "Radio");
			if(copy_to_user(arg,&v, sizeof(v)))
				return -EFAULT;
			return 0;			
		}
		case VIDIOCSAUDIO: {
			struct video_audio v;
			if(copy_from_user(&v, arg, sizeof(v))) 
				return -EFAULT;	
			if(v.audio) 
				return -EINVAL;

			if(v.flags&VIDEO_AUDIO_MUTE) {
				if (dsbr100_stop(radio)==-1)
					warn("radio did not respond properly");
			}
			else
				if (dsbr100_start(radio)==-1)
					warn("radio did not respond properly");
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}


static int usb_dsbr100_open(struct video_device *dev, int flags)
{
	usb_dsbr100 *radio=dev->priv;

	if (! radio) {
		warn("radio not initialised");
		return -EAGAIN;
	}
	if(users)
	{
		warn("radio in use");
		return -EBUSY;
	}
	users++;
	MOD_INC_USE_COUNT;
	if (dsbr100_start(radio)<0)
		warn("radio did not start up properly");
	dsbr100_setfreq(radio,radio->curfreq);
	return 0;
}

static void usb_dsbr100_close(struct video_device *dev)
{
	usb_dsbr100 *radio=dev->priv;

	if (!radio)
		return;
	users--;
	dsbr100_stop(radio);
	MOD_DEC_USE_COUNT;
}

int __init dsbr100_init(void)
{
	usb_dsbr100_radio.priv = NULL;
	usb_register(&usb_dsbr100_driver);
	if (video_register_device(&usb_dsbr100_radio,VFL_TYPE_RADIO)==-1) {	
		warn("couldn't register video device");
		return -EINVAL;
	}
	return 0;
}

int __init init_module(void)
{
	return dsbr100_init();
}

void cleanup_module(void)
{
	usb_dsbr100 *radio=usb_dsbr100_radio.priv;

	if (radio)
		dsbr100_stop(radio);
	video_unregister_device(&usb_dsbr100_radio);
	usb_deregister(&usb_dsbr100_driver);
}

/*
vi: ts=8
Sigh.  Of course, I am one of the ts=2 heretics, but Linus' wish is
my command.
*/
