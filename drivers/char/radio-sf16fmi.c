/* SF16FMI radio driver for Linux radio support
 * heavily based on rtrack driver...
 * (c) 1997 M. Kirkwood
 * (c) 1998 Petr Vandrovec, vandrove@vc.cvut.cz
 *
 * Fitted to new interface by Alan Cox <alan.cox@linux.org>
 *
 * Notes on the hardware
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *  No volume control - only mute/unmute - you have to use line volume
 *  control on SB-part of SF16FMI
 *  
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* check_region, request_region	*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev.h>	/* kernel radio structs		*/
#include <linux/config.h>	/* CONFIG_RADIO_SF16MI_PORT 	*/

#include "rsf16fmi.h"

struct fmi_device
{
	int port;
	int curvol;
	unsigned long curfreq;
	int flags;
};

#ifndef CONFIG_RADIO_SF16FMI_PORT
#define CONFIG_RADIO_SF16FMI_PORT -1
#endif

static int io = CONFIG_RADIO_SF16FMI_PORT; 
static int users = 0;

/* local things */
/* freq in 1/16kHz to internal number */
#define RSF16_ENCODE(x)	((x/16+10700)/50)

static void outbits(int bits, int data, int port)
{
	while(bits--) {
 		if(data & 1) {
			outb(5, port);
			udelay(6);
			outb(7, port);
			udelay(6);
		} else {
			outb(1, port);
			udelay(6);
			outb(3, port);
			udelay(6);
		}
		data>>=1;
	}
}

static void fmi_mute(int port)
{
	outb(0x00, port);
}

static void fmi_unmute(int port)
{
	outb(0x08, port);
}

static int fmi_setfreq(struct fmi_device *dev, unsigned long freq)
{
	int myport = dev->port;

	outbits(16, RSF16_ENCODE(freq), myport);
	outbits(8, 0xC0, myport);
	/* we should wait here... */
	return 0;
}

static int fmi_getsigstr(struct fmi_device *dev)
{
	int val;
	int res;
	int myport = dev->port;

	val = dev->curvol ? 0x08 : 0x00;	/* unmute/mute */
	outb(val, myport);
	outb(val | 0x10, myport);
	udelay(140000);
	res = (int)inb(myport+1);
	outb(val, myport);
	return (res & 2) ? 0 : 1;
}

static int fmi_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct fmi_device *fmi=dev->priv;
	
	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability v;
			v.type=VID_TYPE_TUNER;
			v.channels=1;
			v.audios=1;
			/* No we don't do pictures */
			v.maxwidth=0;
			v.maxheight=0;
			v.minwidth=0;
			v.minheight=0;
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg,sizeof(v))!=0)
				return -EFAULT;
			if(v.tuner)	/* Only 1 tuner */
				return -EINVAL;
			if (fmi->flags & VIDEO_TUNER_LOW) {
				v.rangelow = 87500 * 16;
				v.rangehigh = 108000 * 16;
			} else {
				v.rangelow=(int)(175*8 /* 87.5 *16 */);
				v.rangehigh=(int)(108*16);
			}
			v.flags=fmi->flags;
			v.mode=VIDEO_MODE_AUTO;
			v.signal=0xFFFF*fmi_getsigstr(fmi);
			if(copy_to_user(arg,&v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if(v.tuner!=0)
				return -EINVAL;
			fmi->flags = v.flags & VIDEO_TUNER_LOW;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
		case VIDIOCGFREQ:
		{
			unsigned long tmp = fmi->curfreq;
			if (!(fmi->flags & VIDEO_TUNER_LOW))
				tmp /= 1000;
			if(copy_to_user(arg, &tmp, sizeof(tmp)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long tmp;
			if(copy_from_user(&tmp, arg, sizeof(tmp)))
				return -EFAULT;
			if (!(fmi->flags & VIDEO_TUNER_LOW))
				tmp *= 1000;
			fmi->curfreq = tmp;
			fmi_setfreq(fmi, fmi->curfreq);
			return 0;
		}
		case VIDIOCGAUDIO:
		{	
			struct video_audio v;
			memset(&v,0, sizeof(v));
			v.flags|=VIDEO_AUDIO_MUTABLE;
			v.mode=VIDEO_SOUND_MONO;
			v.volume=fmi->curvol;
			strcpy(v.name, "Radio");
			if(copy_to_user(arg,&v, sizeof(v)))
				return -EFAULT;
			return 0;			
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if(v.audio)
				return -EINVAL;
			fmi->curvol=v.volume;
			if(v.flags&VIDEO_AUDIO_MUTE)
				fmi_mute(fmi->port);
			else if(fmi->curvol)
				fmi_unmute(fmi->port);
			else
				fmi_mute(fmi->port);
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int fmi_open(struct video_device *dev, int flags)
{
	if(users)
		return -EBUSY;
	users++;
	MOD_INC_USE_COUNT;
	return 0;
}

static void fmi_close(struct video_device *dev)
{
	users--;
	MOD_DEC_USE_COUNT;
}

static struct fmi_device fmi_unit;

static struct video_device fmi_radio=
{
	"SF16FMI radio",
	VID_TYPE_TUNER,
	VID_HARDWARE_SF16MI,
	fmi_open,
	fmi_close,
	NULL,	/* Can't read  (no capture ability) */
	NULL,	/* Can't write */
	NULL,	/* Can't poll */
	fmi_ioctl,
	NULL,
	NULL
};

__initfunc(int fmi_init(struct video_init *v))
{
	if (check_region(io, 2)) 
	{
		printk(KERN_ERR "fmi: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	fmi_unit.port=io;
	fmi_unit.flags = VIDEO_TUNER_LOW;
	fmi_radio.priv=&fmi_unit;
	
	if(video_register_device(&fmi_radio, VFL_TYPE_RADIO)==-1)
		return -EINVAL;
		
	request_region(io, 2, "fmi");
	printk(KERN_INFO "SF16FMI radio card driver.\n");
	printk(KERN_INFO "(c) 1998 Petr Vandrovec, vandrove@vc.cvut.cz.\n");
	/* mute card - prevents noisy bootups */
	fmi_mute(io);
	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("Petr Vandrovec, vandrove@vc.cvut.cz and M. Kirkwood");
MODULE_DESCRIPTION("A driver for the SF16MI radio.");
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "I/O address of the SF16MI card (0x284 or 0x384)");

EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if(io==-1)
	{
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	return fmi_init(NULL);
}

void cleanup_module(void)
{
	video_unregister_device(&fmi_radio);
	release_region(io,2);
}

#endif
