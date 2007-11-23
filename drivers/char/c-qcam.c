/*
 *	Video4Linux: Colour QuickCam driver
 *
 *	Philip Blundell <philb@gnu.org>, December 30 1997
 *
 *	Largely untested (seems to work at 24bpp with a bidirectional port,
 *	though). 
 */


#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/parport.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/videodev.h>
#include <asm/uaccess.h>

#include "c-qcam.h"

static __inline__ void qcam_set_ack(struct qcam_device *qcam, unsigned int i)
{
	/* note: the QC specs refer to the PCAck pin by voltage, not
	   software level.  PC ports have builtin inverters. */
	parport_frob_control(qcam->pport, 8, i?8:0);
}

static __inline__ unsigned int qcam_ready1(struct qcam_device *qcam)
{
	return (parport_read_status(qcam->pport) & 0x8)?1:0;

}

static __inline__ unsigned int qcam_ready2(struct qcam_device *qcam)
{
	return (parport_read_data(qcam->pport) & 0x1)?1:0;
}

static inline unsigned int qcam_await_ready1(struct qcam_device *qcam, int value)
{
	unsigned long oldjiffies = jiffies;
	unsigned int i;

	for (oldjiffies = jiffies; (jiffies - oldjiffies) < (HZ/25); )
		if (qcam_ready1(qcam) == value)
			return 0;

	/* If the camera didn't respond within 1/25 second, poll slowly 
	   for a while. */
	for (i = 0; i < 50; i++)
	{
		if (qcam_ready1(qcam) == value)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/10);
	}

	/* Probably somebody pulled the plug out.  Not much we can do. */
	printk(KERN_ERR "c-qcam: ready1 timeout (%d) %x %x\n", value,
	       parport_read_status(qcam->pport),
	       parport_read_control(qcam->pport));
	return 1;
}

static inline unsigned int qcam_await_ready2(struct qcam_device *qcam, int value)
{
	unsigned long oldjiffies = jiffies;
	unsigned int i;

	for (oldjiffies = jiffies; (jiffies - oldjiffies) < (HZ/25); )
		if (qcam_ready2(qcam) == value)
			return 0;

	/* If the camera didn't respond within 1/25 second, poll slowly 
	   for a while. */
	for (i = 0; i < 50; i++)
	{
		if (qcam_ready2(qcam) == value)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/10);
	}

	/* Probably somebody pulled the plug out.  Not much we can do. */
	printk(KERN_ERR "c-qcam: ready2 timeout (%d) %x %x %x\n", value,
	       parport_read_status(qcam->pport),
	       parport_read_control(qcam->pport),
	       parport_read_data(qcam->pport));
	return 1;
}

static inline int qcam_read_data(struct qcam_device *qcam)
{
	unsigned int idata;
	qcam_set_ack(qcam, 0);
	if (qcam_await_ready1(qcam, 1)) return -1;
	idata = parport_read_status(qcam->pport) & 0xf0;
	qcam_set_ack(qcam, 1);
	if (qcam_await_ready1(qcam, 0)) return -1;
	idata |= (parport_read_status(qcam->pport) >> 4);
	return idata;
}

static int qcam_write_data(struct qcam_device *qcam, unsigned int data)
{
	unsigned int idata;
	parport_write_data(qcam->pport, data);
	idata = qcam_read_data(qcam);
	if (data != idata) 
	{
		printk(KERN_WARNING "cqcam: sent %x but received %x\n", data, 
		       idata);
		return 1;
	} 
	return 0;
}

static inline int qcam_set(struct qcam_device *qcam, unsigned int cmd, unsigned int data)
{
	if (qcam_write_data(qcam, cmd))
		return -1;
	if (qcam_write_data(qcam, data))
		return -1;
	return 0;
}

static inline int qcam_get(struct qcam_device *qcam, unsigned int cmd)
{
	if (qcam_write_data(qcam, cmd))
		return -1;
	return qcam_read_data(qcam);
}

static int qc_detect(struct qcam_device *qcam)
{
	unsigned int stat, ostat, i, count = 0;

	parport_write_control(qcam->pport, 0xc);

	/* look for a heartbeat */
	ostat = stat = parport_read_status(qcam->pport);
	for (i=0; i<250; i++) 
	{
		mdelay(1);
		stat = parport_read_status(qcam->pport);
		if (ostat != stat) 
		{
			if (++count >= 3) return 1;
			ostat = stat;
		}
	}

	/* no (or flatline) camera, give up */
	return 0;
}

static void qc_reset(struct qcam_device *qcam)
{
	parport_write_control(qcam->pport, 0xc);
	parport_write_control(qcam->pport, 0x8);
	mdelay(1);
	parport_write_control(qcam->pport, 0xc);
	mdelay(1);          
}

/* Reset the QuickCam and program for brightness, contrast,
 * white-balance, and resolution. */

static void qc_setup(struct qcam_device *q)
{
	qc_reset(q);

	/* Set the brightness.  */
       	qcam_set(q, 11, q->brightness);

	/* Set the height.  */
	qcam_set(q, 17, q->height);

	/* Set the width.  */
	qcam_set(q, 19, q->width/2);

	/* Set top and left.  */
	qcam_set(q, 0xd, q->top);
	qcam_set(q, 0xf, q->left);

	/* Set contrast and white balance.  */
	qcam_set(q, 0x19, q->contrast);
	qcam_set(q, 0x1f, q->whitebal);
	
	/* Set the speed.  */
	qcam_set(q, 45, 2);
}

/* Read some bytes from the camera and put them in the buffer. 
   nbytes should be a multiple of 3, because bidirectional mode gives
   us three bytes at a time.  */

static unsigned int qcam_read_bytes(struct qcam_device *q, unsigned char *buf, unsigned int nbytes)
{
	unsigned int bytes = 0;
	qcam_set_ack(q, 0);
	if (q->bidirectional)
	{
		/* It's a bidirectional port */
		while (bytes < nbytes)
		{
			unsigned int lo1, hi1, lo2, hi2;
			if (qcam_await_ready2(q, 1)) return bytes;
			lo1 = parport_read_data(q->pport) >> 1;
			hi1 = ((parport_read_status(q->pport) >> 3) & 0x1f) ^ 0x10;
			qcam_set_ack(q, 1);
			if (qcam_await_ready2(q, 0)) return bytes;
			lo2 = parport_read_data(q->pport) >> 1;
			hi2 = ((parport_read_status(q->pport) >> 3) & 0x1f) ^ 0x10;
			qcam_set_ack(q, 0);
			buf[bytes++] = (lo1 | ((hi1 & 1)<<7));
			buf[bytes++] = ((hi1 & 0x1e)<<3) | ((hi2 & 0x1e)>>1);
			buf[bytes++] = (lo2 | ((hi2 & 1)<<7));
		}
	}
	else
	{
		/* It's a unidirectional port */
		while (bytes < nbytes)
		{
			unsigned int hi, lo;
			if (qcam_await_ready1(q, 1)) return bytes;
			hi = (parport_read_status(q->pport) & 0xf0);
			qcam_set_ack(q, 1);
			if (qcam_await_ready1(q, 0)) return bytes;
			lo = (parport_read_status(q->pport) & 0xf0);
			qcam_set_ack(q, 0);
			/* flip some bits; cqcam gets this wrong */
			buf[bytes++] = (hi | lo) ^ 0x88;
		}
	}
	return bytes;
}

/* Convert the data the camera gives us into the desired output format. 
   At the moment this is a no-op because read_bytes() does all the 
   required stuff, for 24bpp at least.  */
static size_t qcam_munge_buffer(struct qcam_device *q, char *inbuf, size_t inlen, char *outbuf, size_t outlen)
{
	size_t outptr = 0;
	switch (q->bpp)
	{
	case 24:
		while (inlen && (outptr <= (outlen-3)))
		{
			unsigned char r, g, b;
			r = inbuf[0];
			g = inbuf[1];
			b = inbuf[2];
			put_user(r, outbuf+(outptr++));
			put_user(g, outbuf+(outptr++));
			put_user(b, outbuf+(outptr++));
			inlen -= 3;
			inbuf += 3;
		}
		break;
	default:
		printk("c-qcam: can't convert this format (%d).\n", q->bpp);
		return 0;
	}
	return outptr;
}

static long qc_capture(struct qcam_device *q, char *buf, unsigned long len)
{
	unsigned int tbpp = 0, tdecimation = 0, lines, pixelsperline, bitsperxfer;
	unsigned int is_bi_dir = q->bidirectional;
	size_t wantlen, outptr = 0;
	char *tmpbuf = kmalloc(768, GFP_KERNEL);
	if (tmpbuf == NULL)
	{
		printk(KERN_ERR "cqcam: couldn't allocate a buffer.\n");
		return -ENOMEM;
	}

	/* Wait for camera to become ready */
	for (;;)
	{
		int i = qcam_get(q, 41);
		if (i == -1) {
			qc_setup(q);
			kfree(tmpbuf);
			return -EIO;
		}
		if (i & 0x80)
			schedule();
		else
			break;
	}

	switch (q->bpp) 
	{
	case 24: tbpp = QC_24BPP; break;
	case 32: tbpp = QC_32BPP; break;
	case 16: tbpp = QC_16BPP; break;
	default: printk("qcam: Bad bpp.\n");
	}
	switch (q->transfer_scale) {
	case 1: tdecimation = QC_1_1; break;
	case 2: tdecimation = QC_2_1; break;
	case 4: tdecimation = QC_4_1; break;
	default: printk("qcam: Bad decimation.\n");
	}
	
	qcam_set(q, 7, (tbpp | tdecimation) + ((is_bi_dir)?1:0) + 1);
	
	lines = q->height / q->transfer_scale;
	pixelsperline = q->width / q->transfer_scale;
	bitsperxfer = (is_bi_dir) ? 24 : 8;

	if (is_bi_dir)
	{
		/* Turn the port around */
		parport_frob_control(q->pport, 0x20, 0x20);
		mdelay(3);
		qcam_set_ack(q, 0);
		if (qcam_await_ready1(q, 1)) {
			kfree(tmpbuf);
			qc_setup(q);
			return -EIO;
		}
		qcam_set_ack(q, 1);
		if (qcam_await_ready1(q, 0)) {
			kfree(tmpbuf);
			qc_setup(q);
			return -EIO;
		}
	}

	wantlen = lines * pixelsperline * q->bpp / 8;

	while (wantlen)
	{
		size_t t, s, o;
		s = (wantlen > 768)?768:wantlen;
		t = qcam_read_bytes(q, tmpbuf, s);
		if (outptr < len)
		{
			o = qcam_munge_buffer(q, tmpbuf, t, buf + outptr, 
					      len - outptr);
			outptr += o;
		}
		wantlen -= t;
		if (t < s) 
			break;
		if (current->need_resched)
			schedule();
	}

	len = outptr;

	if (wantlen)
	{
		printk("qcam: short read.\n");
		if (is_bi_dir)
			parport_frob_control(q->pport, 0x20, 0);
		qc_setup(q);
		kfree(tmpbuf);
		return len;
	}

	if (is_bi_dir)
	{
		int l;
		do {
			l = qcam_read_bytes(q, tmpbuf, 3);
			if (current->need_resched)
				schedule();
		} while (l && (tmpbuf[0] == 0x7e || tmpbuf[1] == 0x7e || tmpbuf[2] == 0x7e));
		if (tmpbuf[0] != 0xe || tmpbuf[1] != 0x0 || tmpbuf[2] != 0xf)
			printk("qcam: bad EOF\n");
		qcam_set_ack(q, 0);
		if (qcam_await_ready1(q, 1))
		{
			printk("qcam: no ack after EOF\n");
			parport_frob_control(q->pport, 0x20, 0);
			qc_setup(q);
			kfree(tmpbuf);
			return len;
		}
		parport_frob_control(q->pport, 0x20, 0);
		mdelay(3);
		qcam_set_ack(q, 1);
		if (qcam_await_ready1(q, 0))
		{
			printk("qcam: no ack to port turnaround\n");
			qc_setup(q);
			kfree(tmpbuf);
			return len;
		}
	}
	else
	{
		int l;
		do {
			l = qcam_read_bytes(q, tmpbuf, 1);
			if (current->need_resched)
				schedule();
		} while (l && tmpbuf[0] == 0x7e);
		l = qcam_read_bytes(q, tmpbuf+1, 2);
		if (tmpbuf[0] != 0xe || tmpbuf[1] != 0x0 || tmpbuf[2] != 0xf)
			printk("qcam: bad EOF\n");
	}

	kfree(tmpbuf);

	qcam_write_data(q, 0);

	return len;
}

/*
 *	Video4linux interfacing
 */

static int qcam_open(struct video_device *dev, int flags)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static void qcam_close(struct video_device *dev)
{
	MOD_DEC_USE_COUNT;
}

static int qcam_init_done(struct video_device *dev)
{
	return 0;
}

static long qcam_write(struct video_device *v, const char *buf, unsigned long count, int noblock)
{
	return -EINVAL;
}

static int qcam_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct qcam_device *qcam=(struct qcam_device *)dev;
	
	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability b;
			strcpy(b.name, "Quickcam");
			b.type = VID_TYPE_CAPTURE|VID_TYPE_SCALES;
			b.channels = 1;
			b.audios = 0;
			b.maxwidth = 320;
			b.maxheight = 240;
			b.minwidth = 80;
			b.minheight = 60;
			if(copy_to_user(arg, &b,sizeof(b)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if(v.channel!=0)
				return -EINVAL;
			v.flags=0;
			v.tuners=0;
			/* Good question.. its composite or SVHS so.. */
			v.type = VIDEO_TYPE_CAMERA;
			strcpy(v.name, "Camera");
			if(copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSCHAN:
		{
			int v;
			if(copy_from_user(&v, arg,sizeof(v)))
				return -EFAULT;
			if(v!=0)
				return -EINVAL;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v))!=0)
				return -EFAULT;
			if(v.tuner)
				return -EINVAL;
			strcpy(v.name, "Format");
			v.rangelow=0;
			v.rangehigh=0;
			v.flags= 0;
			v.mode = VIDEO_MODE_AUTO;
			if(copy_to_user(arg,&v,sizeof(v))!=0)
				return -EFAULT;
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v))!=0)
				return -EFAULT;
			if(v.tuner)
				return -EINVAL;
			if(v.mode!=VIDEO_MODE_AUTO)
				return -EINVAL;
			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture p;
			p.colour=0x8000;
			p.hue=0x8000;
			p.brightness=qcam->brightness<<8;
			p.contrast=qcam->contrast<<8;
			p.whiteness=qcam->whitebal<<8;
			p.depth=qcam->bpp;
			p.palette=VIDEO_PALETTE_RGB24;
			if(copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture p;
			if(copy_from_user(&p, arg, sizeof(p)))
				return -EFAULT;

			if (p.palette != VIDEO_PALETTE_RGB24)
				return -EINVAL;
			
			/*
			 *	Now load the camera.
			 */
			qcam->brightness = p.brightness>>8;
			qcam->contrast = p.contrast>>8;
			qcam->whitebal = p.whiteness>>8;
			qcam->bpp = p.depth;
			
			parport_claim_or_block(qcam->pdev);
			qc_setup(qcam); 
			parport_release(qcam->pdev);
			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window vw;
			if(copy_from_user(&vw, arg,sizeof(vw)))
				return -EFAULT;
			if(vw.flags)
				return -EINVAL;
			if(vw.clipcount)
				return -EINVAL;
			if(vw.height<60||vw.height>240)
				return -EINVAL;
			if(vw.width<80||vw.width>320)
				return -EINVAL;
				
			qcam->width = 320;
			qcam->height = 240;
			qcam->transfer_scale = 4;
			
			if(vw.width>=160 && vw.height>=120)
			{
				qcam->transfer_scale = 2;
			}
			if(vw.width>=320 && vw.height>=240)
			{
				qcam->width = 320;
				qcam->height = 240;
				qcam->transfer_scale = 1;
			}
			/* Ok we figured out what to use from our wide choice */
			parport_claim_or_block(qcam->pdev);
			qc_setup(qcam);
			parport_release(qcam->pdev);
			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;
			vw.x=0;
			vw.y=0;
			vw.width=qcam->width/qcam->transfer_scale;
			vw.height=qcam->height/qcam->transfer_scale;
			vw.chromakey=0;
			vw.flags=0;
			if(copy_to_user(arg, &vw, sizeof(vw)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCCAPTURE:
			return -EINVAL;
		case VIDIOCGFBUF:
			return -EINVAL;
		case VIDIOCSFBUF:
			return -EINVAL;
		case VIDIOCKEY:
			return 0;
		case VIDIOCGFREQ:
			return -EINVAL;
		case VIDIOCSFREQ:
			return -EINVAL;
		case VIDIOCGAUDIO:
			return -EINVAL;
		case VIDIOCSAUDIO:
			return -EINVAL;
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static long qcam_read(struct video_device *v, char *buf, unsigned long count,  int noblock)
{
	struct qcam_device *qcam=(struct qcam_device *)v;
	int len;
	parport_claim_or_block(qcam->pdev);
	/* Probably should have a semaphore against multiple users */
	len = qc_capture(qcam, buf,count); 
	parport_release(qcam->pdev);
	return len;
}

/* video device template */
static struct video_device qcam_template=
{
	"Colour Quickcam",
	VID_TYPE_CAPTURE,
	VID_HARDWARE_QCAM_C,
	qcam_open,
	qcam_close,
	qcam_read,
	qcam_write,
	NULL,
	qcam_ioctl,
	NULL,
	qcam_init_done,
	NULL,
	0,
	0
};

/* Initialize the QuickCam driver control structure. */

static struct qcam_device *qcam_init(struct parport *port)
{
	struct qcam_device *q;
	
	q = kmalloc(sizeof(struct qcam_device), GFP_KERNEL);

	q->pport = port;
	q->pdev = parport_register_device(port, "c-qcam", NULL, NULL,
					  NULL, 0, NULL);

	q->bidirectional = (q->pport->modes & PARPORT_MODE_PCPS2)?1:0;

	if (q->pdev == NULL) 
	{
		printk(KERN_ERR "c-qcam: couldn't register for %s.\n",
		       port->name);
		kfree(q);
		return NULL;
	}
	
	memcpy(&q->vdev, &qcam_template, sizeof(qcam_template));

	q->width = 320;
	q->height = 240;
	q->bpp = 32;
	q->transfer_scale = 1;
	q->contrast = 192;
	q->brightness = 240;
	q->whitebal = 128;
	q->top = 1;
	q->left = 14;
	return q;
}

#define MAX_CAMS 4
static struct qcam_device *qcams[MAX_CAMS];
static unsigned int num_cams = 0;

int init_cqcam(struct parport *port)
{
	struct qcam_device *qcam;

	if (num_cams == MAX_CAMS)
	{
		printk(KERN_ERR "Too many Quickcams (max %d)\n", MAX_CAMS);
		return -ENOSPC;
	}

	qcam = qcam_init(port);
	if (qcam==NULL)
		return -ENODEV;
		
	parport_claim_or_block(qcam->pdev);

	qc_reset(qcam);
	
	if (qc_detect(qcam)==0)
	{
		parport_release(qcam->pdev);
		parport_unregister_device(qcam->pdev);
		kfree(qcam);
		return -ENODEV;
	}

	qc_setup(qcam);

	parport_release(qcam->pdev);
	
	printk(KERN_INFO "Connectix Colour Quickcam on %s\n", 
	       qcam->pport->name);
	
	if (video_register_device(&qcam->vdev, VFL_TYPE_GRABBER)==-1)
	{
		parport_unregister_device(qcam->pdev);
		kfree(qcam);
		return -ENODEV;
	}

	qcams[num_cams++] = qcam;

	return 0;
}

void close_cqcam(struct qcam_device *qcam)
{
	video_unregister_device(&qcam->vdev);
	parport_unregister_device(qcam->pdev);
	kfree(qcam);
}

#ifdef MODULE
int init_module(void)
{
	struct parport *port;

	for (port = parport_enumerate(); port; port=port->next)
		init_cqcam(port);

	return (num_cams)?0:-ENODEV;
}

void cleanup_module(void)
{
	unsigned int i;
	for (i = 0; i < num_cams; i++)
		close_cqcam(qcams[i]);
}
#else
__initfunc(int init_colour_qcams(struct video_init *unused))
{
	struct parport *port;

	for (port = parport_enumerate(); port; port=port->next)
		init_cqcam(port);
	return 0;
}
#endif
