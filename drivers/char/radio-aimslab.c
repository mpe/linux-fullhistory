/* radiotrack (radioreveal) driver for Linux radio support
 * (c) 1997 M. Kirkwood
 * Coverted to new API by Alan Cox <Alan.Cox@linux.org>
 *
 * TODO: Allow for more than one of these foolish entities :-)
 *
 * Notes on the hardware (reverse engineered from other peoples'
 * reverse engineering of AIMS' code :-)
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *
 *  The signal strength query is unsurprisingly inaccurate.  And it seems
 *  to indicate that (on my card, at least) the frequency setting isn't
 *  too great.  (I have to tune up .025MHz from what the freq should be
 *  to get a report that the thing is tuned.)
 *
 *  Volume control is (ugh) analogue:
 *   out(port, start_increasing_volume);
 *   wait(a_wee_while);
 *   out(port, stop_changing_the_volume);
 *  
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* check_region, request_region	*/
#include <linux/delay.h>	/* udelay			*/
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/videodev.h>	/* kernel radio structs		*/
#include <linux/config.h>	/* CONFIG_RADIO_RTRACK_PORT 	*/

#ifndef CONFIG_RADIO_RTRACK_PORT
#define CONFIG_RADIO_RTRACK_PORT -1
#endif

static int io = CONFIG_RADIO_RTRACK_PORT; 
static int users = 0;

struct rt_device
{
	int port;
	int curvol;
	unsigned long curfreq;
};


/* local things */

static void sleep_delay(int n)
{
	/* Sleep nicely for 'n' uS */
	int d=n/1000000/HZ;
	if(!d)
		udelay(n);
	else
	{
		/* Yield CPU time */
		unsigned long x=jiffies;
		while((jiffies-x)<=d)
			schedule();
	}
}
	
/* Clock out data to the chip. This looks suspiciously like i2c as usual */

static void outbits(int bits, int data, int port)
{
	while(bits--) 
	{
		if(data & 1) 
		{
			outw(5, port);
			outw(5, port);
			outw(7, port);
			outw(7, port);
		}
		else 
		{
			outw(1, port);
			outw(1, port);
			outw(3, port);
			outw(3, port);
		}
		data>>=1;
	}
}

static void rt_decvol(int port)
{
	outb(0x48, port);
	sleep_delay(100000);
	outb(0xc8, port);
}

static void rt_incvol(int port)
{
	outb(0x88, port);
	sleep_delay(100000);
	outb(0xc8, port);
}

static void rt_mute(int port)
{
	outb(0, port);
	outb(0xc0, port);
}

static void rt_unmute(int port)
{
	outb(0, port);
	outb(0xc8, port);
}

static int rt_setvol(struct rt_device *dev, int vol)
{
	int i;
	if(vol == dev->curvol)
		return 0;

	if(vol == 0)
		rt_mute(dev->port);

	if(vol > dev->curvol)
		for(i = dev->curvol; i < vol; i++)
			rt_incvol(dev->port);
	else
		for(i = dev->curvol; i > vol; i--)
			rt_decvol(dev->port);

	if(dev->curvol == 0)
		rt_unmute(dev->port);

	return 0;
}

static int rt_setfreq(struct rt_device *dev, unsigned long frequency)
{
	int myport = dev->port;
#define	RTRACK_ENCODE(x)	(((((x)*2)/5)-(40*88))+0xf6c)
	outbits(16, RTRACK_ENCODE(frequency), myport);
	outbits(8, 0xa0, myport);
/* XXX - get rid of this once setvol is implemented properly - XXX */
/* these insist on turning the thing on.  not sure I approve... */
	udelay(1000);
	outb(0, myport);
	outb(0xc8, myport);

	return 0;
}

int rt_getsigstr(struct rt_device *dev)
{
	int res;
	int myport = dev->port;

	outb(0xf8, myport);
	sleep_delay(200000);
	res = (int)inb(myport);
	sleep_delay(10000);
	outb(0xe8, myport);
	if(res == 0xfd)
		return 1;
	else
		return 0;
}

static int rt_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct rt_device *rt=dev->priv;
	
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
			v.rangelow=(int)(88.0*16);
			v.rangehigh=(int)(108.0*16);
			v.flags=0;
			v.mode=VIDEO_MODE_AUTO;
			v.signal=0xFFFF*rt_getsigstr(rt);
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
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
		case VIDIOCGFREQ:
			if(copy_to_user(arg, &rt->curfreq, sizeof(rt->curfreq)))
				return -EFAULT;
			return 0;
		case VIDIOCSFREQ:
			if(copy_from_user(&rt->curfreq, arg,sizeof(rt->curfreq)))
				return -EFAULT;
			rt_setfreq(rt, rt->curfreq);
			return 0;
		case VIDIOCGAUDIO:
		{	
			struct video_audio v;
			memset(&v,0, sizeof(v));
			v.flags|=VIDEO_AUDIO_MUTABLE|VIDEO_AUDIO_VOLUME;
			v.volume=rt->curvol;
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
			rt->curvol=v.volume;

			if(v.flags&VIDEO_AUDIO_MUTE) 
				rt_mute(rt->port);
			else
				rt_setvol(rt,rt->curvol/6554);	
			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int rt_open(struct video_device *dev, int flags)
{
	if(users)
		return -EBUSY;
	users++;
	MOD_INC_USE_COUNT;
	return 0;
}

static void rt_close(struct video_device *dev)
{
	users--;
	MOD_DEC_USE_COUNT;
}

static struct rt_device rtrack_unit;

static struct video_device rtrack_radio=
{
	"RadioTrack radio",
	VID_TYPE_TUNER,
	VID_HARDWARE_RTRACK,
	rt_open,
	rt_close,
	NULL,	/* Can't read  (no capture ability) */
	NULL,	/* Can't write */
	rt_ioctl,
	NULL,
	NULL
};

__initfunc(int rtrack_init(struct video_init *v))
{
	if (check_region(io, 2)) 
	{
		printk(KERN_ERR "rtrack: port 0x%x already in use\n", io);
		return -EBUSY;
	}

	rtrack_radio.priv=&rtrack_unit;
	
	if(video_register_device(&rtrack_radio, VFL_TYPE_RADIO)==-1)
		return -EINVAL;
		
	request_region(io, 2, "rtrack");
	printk(KERN_INFO "AIMSlab Radiotrack/radioreveal card driver.\n");
	/* mute card - prevents noisy bootups */
	rt_mute(io);
	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("M.Kirkwood");
MODULE_DESCRIPTION("A driver for the RadioTrack/RadioReveal radio card.");
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "I/O address of the RadioTrack card (0x20f or 0x30f)");

EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if(io==-1)
	{
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	return rtrack_init(NULL);
}

void cleanup_module(void)
{
	video_unregister_device(&rtrack_radio);
	release_region(io,2);
}

#endif
