/* radiotrack (radioreveal) driver for Linux radio support
 * (c) 1997 M. Kirkwood
 * Coverted to new API by Alan Cox <Alan.Cox@linux.org>
 * Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
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
	int muted;
};


/* local things */

static void sleep_delay(long n)
{
	/* Sleep nicely for 'n' uS */
	int d=n/(1000000/HZ);
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

static void rt_decvol(void)
{
	outb(0x58, io);		/* volume down + sigstr + on	*/
	sleep_delay(100000);
	outb(0xd8, io);		/* volume steady + sigstr + on	*/
}

static void rt_incvol(void)
{
	outb(0x98, io);		/* volume up + sigstr + on	*/
	sleep_delay(100000);
	outb(0xd8, io);		/* volume steady + sigstr + on	*/
}

static void rt_mute(struct rt_device *dev)
{
	dev->muted = 1;
	outb(0xd0, io);			/* volume steady, off		*/
}

static int rt_setvol(struct rt_device *dev, int vol)
{
	int i;

	if(vol == dev->curvol) {	/* requested volume = current */
		if (dev->muted) {	/* user is unmuting the card  */
			dev->muted = 0;
			outb (0xd8, io);	/* enable card */
		}	
	
		return 0;
	}

	if(vol == 0) {			/* volume = 0 means mute the card */
		outb(0x48, io);		/* volume down but still "on"	*/
		sleep_delay(2000000);	/* make sure it's totally down	*/
		outb(0xd0, io);		/* volume steady, off		*/
		dev->curvol = 0;	/* track the volume state!	*/
		return 0;
	}

	dev->muted = 0;
	if(vol > dev->curvol)
		for(i = dev->curvol; i < vol; i++) 
			rt_incvol();
	else
		for(i = dev->curvol; i > vol; i--) 
			rt_decvol();

	dev->curvol = vol;

	return 0;
}

/* the 128+64 on these outb's is to keep the volume stable while tuning 
 * without them, the volume _will_ creep up with each frequency change
 * and bit 4 (+16) is to keep the signal strength meter enabled
 */

void send_0_byte(int port, struct rt_device *dev)
{
	if ((dev->curvol == 0) || (dev->muted)) {
		outb_p(128+64+16+  1, port);   /* wr-enable + data low */
		outb_p(128+64+16+2+1, port);   /* clock */
	}
	else {
		outb_p(128+64+16+8+  1, port);  /* on + wr-enable + data low */
		outb_p(128+64+16+8+2+1, port);  /* clock */
	}
	sleep_delay(1000); 
}

void send_1_byte(int port, struct rt_device *dev)
{
	if ((dev->curvol == 0) || (dev->muted)) {
		outb_p(128+64+16+4  +1, port);   /* wr-enable+data high */
		outb_p(128+64+16+4+2+1, port);   /* clock */
	} 
	else {
		outb_p(128+64+16+8+4  +1, port); /* on+wr-enable+data high */
		outb_p(128+64+16+8+4+2+1, port); /* clock */
	}

	sleep_delay(1000); 
}

static int rt_setfreq(struct rt_device *dev, unsigned long freq)
{
	int i;

	/* adapted from radio-aztech.c */

	/* We want to compute x * 100 / 16 without overflow 
	 * So we compute x*6 + (x/100)*25 to give x*6.25
	 */
	 
	freq = freq * 6 + freq/4;	/* massage the data a little	*/
	freq += 1070;			/* IF = 10.7 MHz 		*/
	freq /= 5;			/* ref = 25 kHz			*/

	send_0_byte (io, dev);		/*  0: LSB of frequency		*/

	for (i = 0; i < 13; i++)	/*   : frequency bits (1-13)	*/
		if (freq & (1 << i))
			send_1_byte (io, dev);
		else
			send_0_byte (io, dev);

	send_0_byte (io, dev);		/* 14: test bit - always 0    */
	send_0_byte (io, dev);		/* 15: test bit - always 0    */

	send_0_byte (io, dev);		/* 16: band data 0 - always 0 */
	send_0_byte (io, dev);		/* 17: band data 1 - always 0 */
	send_0_byte (io, dev);		/* 18: band data 2 - always 0 */
	send_0_byte (io, dev);		/* 19: time base - always 0   */

	send_0_byte (io, dev);		/* 20: spacing (0 = 25 kHz)   */
	send_1_byte (io, dev);		/* 21: spacing (1 = 25 kHz)   */
	send_0_byte (io, dev);		/* 22: spacing (0 = 25 kHz)   */
	send_1_byte (io, dev);		/* 23: AM/FM (FM = 1, always) */

	if ((dev->curvol == 0) || (dev->muted))
		outb (0xd0, io);	/* volume steady + sigstr */
	else
		outb (0xd8, io);	/* volume steady + sigstr + on */

	return 0;
}

int rt_getsigstr(struct rt_device *dev)
{
	if (inb(io) & 2)	/* bit set = no signal present	*/
		return 0;
	return 1;		/* signal present		*/
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
			strcpy(v.name, "RadioTrack");
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
			v.rangelow=(88*16);
			v.rangehigh=(108*16);
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
			v.volume=rt->curvol * 6554;
			v.step=6554;
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

			if(v.flags&VIDEO_AUDIO_MUTE) 
				rt_mute(rt);
			else
				rt_setvol(rt,v.volume/6554);	

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
	NULL,	/* No poll */
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

	/* this ensures that the volume is all the way down  */
	outb(0x48, io);		/* volume down but still "on"	*/
	sleep_delay(2000000);	/* make sure it's totally down	*/
	outb(0xc0, io);		/* steady volume, mute card	*/
	rtrack_unit.curvol = 0;

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
