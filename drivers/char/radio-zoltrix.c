/* zoltrix radio plus driver for Linux radio support
 * (c) 1998 C. van Schaik <carl@leg.uct.ac.za>
 *
 * BUGS  
 *  Due to the inconsistancy in reading from the signal flags
 *  it is difficult to get an accurate tuned signal.
 *
 *  There seems to be a problem with the volume setting that I must still
 *  figure out. 
 *  It seems that the card has is not linear to 0 volume. It cuts off
 *  at a low frequency, and it is not possible (at least I have not found)
 *  to get fine volume control over the low volume range.
 *
 *  Some code derived from code by Frans Brinkman
 */

#include <linux/module.h>	/* Modules                        */
#include <linux/init.h>		/* Initdata                       */
#include <linux/ioport.h>	/* check_region, request_region   */
#include <linux/delay.h>	/* udelay                 */
#include <asm/io.h>		/* outb, outb_p                   */
#include <asm/uaccess.h>	/* copy to/from user              */
#include <linux/videodev.h>	/* kernel radio structs           */
#include <linux/config.h>	/* CONFIG_RADIO_ZOLTRIX_PORT      */

#ifndef CONFIG_RADIO_ZOLTRIX_PORT
#define CONFIG_RADIO_ZOLTRIX_PORT -1
#endif

static int io = CONFIG_RADIO_ZOLTRIX_PORT;
static int users = 0;

struct zol_device {
	int port;
	int curvol;
	unsigned long curfreq;
	int muted;
};


/* local things */

static void sleep_delay(long n)
{
	/* Sleep nicely for 'n' uS */
	int d = n / (1000000 / HZ);
	if (!d)
		udelay(n);
	else {
		/* Yield CPU time */
		unsigned long x = jiffies;
		while ((jiffies - x) <= d)
			schedule();
	}
}

static void zol_mute(struct zol_device *dev)
{
	dev->muted = 1;
	outb(0, io);
	outb(0, io);
	inb(io + 3);		/* Zoltrix needs to be read to confirm */
}

static void zol_on(int vol)
{
        int l;
        outb(vol, io);
        sleep_delay(10000);
        l = inb(io + 2);
}

static int zol_setvol(struct zol_device *dev, int vol)
{
	if (vol == dev->curvol) {	/* requested volume = current */
		if (dev->muted) {	/* user is unmuting the card  */
			dev->muted = 0;
                        zol_on(vol);
		}
		return 0;
	}
	if (vol == 0) {		/* volume = 0 means mute the card */
                zol_mute(dev);
		return 0;
	}
	dev->muted = 0;
	dev->curvol = vol;

	zol_on(vol);

	return 0;
}

static int zol_setfreq(struct zol_device *dev, unsigned long freq)
{
	/* tunes the radio to the desired frequency */
	unsigned long long bitmask, f, m;
	int i;

	m = (freq * 25 / 4 - 8800) * 2;
	f = (unsigned long long) m + 0x4d1c;

	bitmask = 0xc480402c10080000ull;
	i = 45;

	zol_mute(dev);

	outb(0x40, io);
	outb(0xc0, io);

	bitmask = (bitmask ^ ((f & 0xff) << 47) ^ ((f & 0xff00) << 30) ^ ( /*stereo */ 0 << 31));
	while (i--) {
		if ((bitmask & 0x8000000000000000ull) != 0) {
			outb(0x80, io);
			sleep_delay(50);
			outb(0x00, io);
			sleep_delay(50);
			outb(0x80, io);
			sleep_delay(50);
		} else {
			outb(0xc0, io);
			sleep_delay(50);
			outb(0x40, io);
			sleep_delay(50);
			outb(0xc0, io);
			sleep_delay(50);
		}
		bitmask *= 2;
	}
	/* termination sequence */
	outb(0x80, io);
	outb(0xc0, io);
	outb(0x40, io);
        zol_on(dev->curvol);
	return 0;
}

/* Get signal strength */

int zol_getsigstr(struct zol_device *dev)
{
	int a, b;

	outb(0x00, io);         /* This stuff I found to do nothing */
	outb(dev->curvol, io);
	sleep_delay(20000);

	a = inb(io);
	sleep_delay(1000);
	b = inb(io);

        if ((a == b) && (a == 0xdf))  /* I found this out by playing */
                                      /* with a binary scanner on the card io */
		return (1);
	else
		return (0);

	if (inb(io) & 2)	/* bit set = no signal present    */
		return 0;
	return 1;		/* signal present               */
}

static int zol_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct zol_device *rt = dev->priv;

	switch (cmd) {
	case VIDIOCGCAP:
		{
			struct video_capability v;
			v.type = VID_TYPE_TUNER;
			v.channels = 1;
			v.audios = 1;
			/* No we don't do pictures */
			v.maxwidth = 0;
			v.maxheight = 0;
			v.minwidth = 0;
			v.minheight = 0;
			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCGTUNER:
		{
			struct video_tuner v;
			if (copy_from_user(&v, arg, sizeof(v)) != 0)
				return -EFAULT;
			if (v.tuner)	/* Only 1 tuner */
				return -EINVAL;
			v.rangelow = (int) (88.0 * 16);
			v.rangehigh = (int) (108.0 * 16);
			v.flags = 0;
			v.mode = VIDEO_MODE_AUTO;
			v.signal = 0xFFFF * zol_getsigstr(rt);
			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCSTUNER:
		{
			struct video_tuner v;
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if (v.tuner != 0)
				return -EINVAL;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
	case VIDIOCGFREQ:
		if (copy_to_user(arg, &rt->curfreq, sizeof(rt->curfreq)))
			return -EFAULT;
		return 0;
	case VIDIOCSFREQ:
		if (copy_from_user(&rt->curfreq, arg, sizeof(rt->curfreq)))
			return -EFAULT;
		zol_setfreq(rt, rt->curfreq);
		return 0;
	case VIDIOCGAUDIO:
		{
			struct video_audio v;
			memset(&v, 0, sizeof(v));
			v.flags |= VIDEO_AUDIO_MUTABLE | VIDEO_AUDIO_VOLUME;
			v.volume = rt->curvol * 4095;
			strcpy(v.name, "Radio");
			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
	case VIDIOCSAUDIO:
		{
			struct video_audio v;
			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if (v.audio)
				return -EINVAL;

			if (v.flags & VIDEO_AUDIO_MUTE)
				zol_mute(rt);
			else
				zol_setvol(rt, v.volume / 4096);

			return 0;
		}
	default:
		return -ENOIOCTLCMD;
	}
}

static int zol_open(struct video_device *dev, int flags)
{
	if (users)
		return -EBUSY;
	users++;
	MOD_INC_USE_COUNT;
	return 0;
}

static void zol_close(struct video_device *dev)
{
	users--;
	MOD_DEC_USE_COUNT;
}

static struct zol_device zoltrix_unit;

static struct video_device zoltrix_radio =
{
	"Zoltrix Radio Plus",
	VID_TYPE_TUNER,
	VID_HARDWARE_ZOLTRIX,
	zol_open,
	zol_close,
	NULL,			/* Can't read  (no capture ability) */
	NULL,			/* Can't write */
	NULL,
	zol_ioctl,
	NULL,
	NULL
};

__initfunc(int zoltrix_init(struct video_init *v))
{
	if (check_region(io, 2)) {
		printk(KERN_ERR "zoltrix: port 0x%x already in use\n", io);
		return -EBUSY;
	}
	zoltrix_radio.priv = &zoltrix_unit;

	if (video_register_device(&zoltrix_radio, VFL_TYPE_RADIO) == -1)
		return -EINVAL;

	request_region(io, 2, "zoltrix");
	printk(KERN_INFO "Zoltrix Radio Plus card driver.\n");

	/* mute card - prevents noisy bootups */

	/* this ensures that the volume is all the way down  */

	outb(0, io);
	outb(0, io);
	sleep_delay(20000);
	inb(io + 3);

	zoltrix_unit.curvol = 0;

	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("C.van Schaik");
MODULE_DESCRIPTION("A driver for the Zoltrix Radio Plus.");
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "I/O address of the Zoltrix Radio Plus (0x20c or 0x30c)");

EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if (io == -1) {
		printk(KERN_ERR "You must set an I/O address with io=0x???\n");
		return -EINVAL;
	}
	return zoltrix_init(NULL);
}

void cleanup_module(void)
{
	video_unregister_device(&zoltrix_radio);
	release_region(io, 2);
}

#endif
