/*
 * Video capture interface for Linux
 *
 *		A generic video device interface for the LINUX operating system
 *		using a set of device structures/vectors for low level operations.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Alan Cox, <alan@cymru.net>
 *
 * Fixes:
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/videodev.h>

#if LINUX_VERSION_CODE >= 0x020100
#include <asm/uaccess.h>
#endif
#include <asm/system.h>

#include <linux/kmod.h>


#define VIDEO_NUM_DEVICES	256 

/*
 *	Active devices 
 */
 
static struct video_device *video_device[VIDEO_NUM_DEVICES];

#ifdef CONFIG_VIDEO_BT848
extern int init_bttv_cards(struct video_init *);
extern int i2c_tuner_init(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_SAA5249
extern int init_saa_5249(struct video_init *);
#endif	
#ifdef CONFIG_VIDEO_CQCAM
extern int init_colour_qcams(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_BWQCAM
extern int init_bw_qcams(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_PLANB
extern int init_planbs(struct video_init *);
#endif
#ifdef CONFIG_RADIO_AZTECH
extern int aztech_init(struct video_init *);
#endif
#ifdef CONFIG_RADIO_RTRACK
extern int rtrack_init(struct video_init *);
#endif
#ifdef CONFIG_RADIO_SF16FMI
extern int fmi_init(struct video_init *);
#endif
#ifdef CONFIG_RADIO_MIROPCM20
extern int pcm20_init(struct video_init *);
#endif
#ifdef CONFIG_RADIO_GEMTEK
extern int gemtek_init(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_PMS
extern int init_pms_cards(struct video_init *);
#endif

static struct video_init video_init_list[]={
#ifdef CONFIG_VIDEO_BT848
	{"i2c-tuner", i2c_tuner_init},
	{"bttv", init_bttv_cards},
#endif	
#ifdef CONFIG_VIDEO_SAA5249
	{"saa5249", init_saa_5249},
#endif	
#ifdef CONFIG_VIDEO_CQCAM
	{"c-qcam", init_colour_qcams},
#endif	
#ifdef CONFIG_VIDEO_BWQCAM
	{"bw-qcam", init_bw_qcams},
#endif	
#ifdef CONFIG_VIDEO_PMS
	{"PMS", init_pms_cards}, 
#endif	
#ifdef CONFIG_VIDEO_PLANB
	{"planb", init_planbs},
#endif
#ifdef CONFIG_RADIO_AZTECH
	{"Aztech", aztech_init}, 
#endif	
#ifdef CONFIG_RADIO_RTRACK
	{"RTrack", rtrack_init}, 
#endif	
#ifdef CONFIG_RADIO_SF16FMI
	{"SF16FMI", fmi_init}, 
#endif	
#ifdef CONFIG_RADIO_MIROPCM20
	{"PCM20", pcm20_init}, 
#endif
#ifdef CONFIG_RADIO_GEMTEK
	{"GemTek", gemtek_init},
#endif
	{"end", NULL}
};

#if LINUX_VERSION_CODE >= 0x020100
/*
 *	Read will do some smarts later on. Buffer pin etc.
 */
 
static ssize_t video_read(struct file *file,
	char *buf, size_t count, loff_t *ppos)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->read)
		return vfl->read(vfl, buf, count, file->f_flags&O_NONBLOCK);
	else
		return -EINVAL;
}



/*
 *	Write for now does nothing. No reason it shouldnt do overlay setting
 *	for some boards I guess..
 */

static ssize_t video_write(struct file *file, const char *buf, 
	size_t count, loff_t *ppos)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->write)
		return vfl->write(vfl, buf, count, file->f_flags&O_NONBLOCK);
	else
		return 0;
}

/*
 *	Poll to see if we're readable, can probably be used for timing on incoming
 *  frames, etc..
 */

static unsigned int video_poll(struct file *file, poll_table * wait)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->poll)
		return vfl->poll(vfl, file, wait);
	else
		return 0;
}


#else
static int video_read(struct inode *ino,struct file *file,
			  char *buf, int count)
{
         int err;
	 struct video_device *vfl=video_device[MINOR(ino->i_rdev)];
	 if (vfl->read)
	   return vfl->read(vfl, buf, count, file->f_flags&O_NONBLOCK);
	 else
	   return -EINVAL;
}

static int video_write(struct inode *ino,struct file *file, const char *buf, 
			int count)
{
	int err;
	struct video_device *vfl=video_device[MINOR(ino->i_rdev)];
	if (vfl->write)
	  return vfl->write(vfl, buf, count, file->f_flags&O_NONBLOCK);
	else
	  return 0;
}

#endif

/*
 *	Open a video device.
 */

static int video_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int err;
	struct video_device *vfl;
	
	if(minor>=VIDEO_NUM_DEVICES)
		return -ENODEV;
		
	vfl=video_device[minor];
	if(vfl==NULL) {
		char modname[20];

		sprintf (modname, "char-major-%d-%d", VIDEO_MAJOR, minor);
		request_module(modname);
		vfl=video_device[minor];
		if (vfl==NULL)
			return -ENODEV;
	}
	if(vfl->busy)
		return -EBUSY;
	vfl->busy=1;		/* In case vfl->open sleeps */
	
	if(vfl->open)
	{
		err=vfl->open(vfl,0);	/* Tell the device it is open */
		if(err)
		{
			vfl->busy=0;
			return err;
		}
	}
	return 0;
}

/*
 *	Last close of a video for Linux device
 */
	
static int video_release(struct inode *inode, struct file *file)
{
	struct video_device *vfl=video_device[MINOR(inode->i_rdev)];
	if(vfl->close)
		vfl->close(vfl);
	vfl->busy=0;
	return 0;
}

/*
 *	Question: Should we be able to capture and then seek around the
 *	image ?
 */
 
#if LINUX_VERSION_CODE >= 0x020100
static long long video_lseek(struct file * file,
			  long long offset, int origin)
{
	return -ESPIPE;
}
#else
static long long video_lseek(struct inode *inode, struct file * file,
			     long long offset, int origin)
{
	return -ESPIPE;
}
#endif


static int video_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct video_device *vfl=video_device[MINOR(inode->i_rdev)];
	int err=vfl->ioctl(vfl, cmd, (void *)arg);

	if(err!=-ENOIOCTLCMD)
		return err;
	
	switch(cmd)
	{
		default:
			return -EINVAL;
	}
}

/*
 *	We need to do MMAP support
 */
 
 
#if LINUX_VERSION_CODE >= 0x020100
int video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
#else
static int video_mmap(struct inode * ino, struct file * file,
		      struct vm_area_struct * vma)
{
	struct video_device *vfl=video_device[MINOR(ino->i_rdev)];
#endif
	if(vfl->mmap)
		return vfl->mmap(vfl, (char *)vma->vm_start, 
				(unsigned long)(vma->vm_end-vma->vm_start));
	return -EINVAL;
}

/*
 *	Video For Linux device drivers request registration here.
 */
 
int video_register_device(struct video_device *vfd, int type)
{
	int i=0;
	int base;
	int err;
	int end;
	
	switch(type)
	{
		case VFL_TYPE_GRABBER:
			base=0;
			end=64;
			break;
		case VFL_TYPE_VTX:
			base=192;
			end=224;
			break;
		case VFL_TYPE_VBI:
			base=224;
			end=240;
			break;
		case VFL_TYPE_RADIO:
			base=64;
			end=128;
			break;
		default:
			return -1;
	}
	
	for(i=base;i<end;i++)
	{
		if(video_device[i]==NULL)
		{
			video_device[i]=vfd;
			vfd->minor=i;
			/* The init call may sleep so we book the slot out
			   then call */
			MOD_INC_USE_COUNT;
			if(vfd->initialize)
			{
				err=vfd->initialize(vfd);
				if(err<0)
				{
					video_device[i]=NULL;
					MOD_DEC_USE_COUNT;
					return err;
				}
			}
			return 0;
		}
	}
	return -ENFILE;
}

/*
 *	Unregister an unused video for linux device
 */
 
void video_unregister_device(struct video_device *vfd)
{
	if(video_device[vfd->minor]!=vfd)
		panic("vfd: bad unregister");
	video_device[vfd->minor]=NULL;
	MOD_DEC_USE_COUNT;
}


static struct file_operations video_fops=
{
	video_lseek,
	video_read,
	video_write,
	NULL,	/* readdir */
#if LINUX_VERSION_CODE >= 0x020100
	video_poll,	/* poll */
#else
	NULL,
#endif
	video_ioctl,
	video_mmap,
	video_open,
#if LINUX_VERSION_CODE >= 0x020100
	NULL,		/* flush */
#endif
	video_release
};

/*
 *	Initialise video for linux
 */
 
int videodev_init(void)
{
	struct video_init *vfli = video_init_list;
	
	printk(KERN_INFO "Linux video capture interface: v1.00\n");
	if(register_chrdev(VIDEO_MAJOR,"video_capture", &video_fops))
	{
		printk("video_dev: unable to get major %d\n", VIDEO_MAJOR);
		return -EIO;
	}

	/*
	 *	Init kernel installed video drivers
	 */
	 	
	while(vfli->init!=NULL)
	{
		vfli->init(vfli);
		vfli++;
	}
	return 0;
}

#ifdef MODULE		
int init_module(void)
{
	return videodev_init();
}

void cleanup_module(void)
{
	unregister_chrdev(VIDEO_MAJOR, "video_capture");
}

#endif

#if LINUX_VERSION_CODE >= 0x020100
EXPORT_SYMBOL(video_register_device);
EXPORT_SYMBOL(video_unregister_device);
#endif
