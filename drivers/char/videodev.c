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
 * Author:	Alan Cox, <alan@redhat.com>
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
#include <linux/init.h>

#include <asm/uaccess.h>
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
#ifdef CONFIG_VIDEO_BWQCAM
extern int init_bw_qcams(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_CPIA
extern int cpia_init(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_PLANB
extern int init_planbs(struct video_init *);
#endif
#ifdef CONFIG_VIDEO_ZORAN
extern int init_zoran_cards(struct video_init *);
#endif

static struct video_init video_init_list[]={
#ifdef CONFIG_VIDEO_BT848
	{"i2c-tuner", i2c_tuner_init},
	{"bttv", init_bttv_cards},
#endif	
#ifdef CONFIG_VIDEO_BWQCAM
	{"bw-qcam", init_bw_qcams},
#endif	
#ifdef CONFIG_VIDEO_CPIA
	{"cpia", cpia_init},
#endif	
#ifdef CONFIG_VIDEO_PLANB
	{"planb", init_planbs},
#endif
#ifdef CONFIG_VIDEO_ZORAN
	{"zoran", init_zoran_cards},
#endif	
	{"end", NULL}
};

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

		MOD_INC_USE_COUNT;
		sprintf (modname, "char-major-%d-%d", VIDEO_MAJOR, minor);
		request_module(modname);
		vfl=video_device[minor];
		MOD_DEC_USE_COUNT;
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
 
static long long video_lseek(struct file * file,
			  long long offset, int origin)
{
	return -ESPIPE;
}

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
 
 
int video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	if(vfl->mmap)
		return vfl->mmap(vfl, (char *)vma->vm_start, 
				(unsigned long)(vma->vm_end-vma->vm_start));
	return -EINVAL;
}

extern struct file_operations video_fops;

/**
 *	video_register_device - register video4linux devices
 *	@vfd: video device structure we want to register
 *	@type: type of device to register
 *	FIXME: needs a semaphore on 2.3.x
 *	
 *	The registration code assigns minor numbers based on the type
 *	requested. -ENFILE is returned in all the device slots for this
 *	catetory are full. If not then the minor field is set and the
 *	driver initialize function is called (if non %NULL).
 *
 *	Zero is returned on success.
 *
 *	Valid types are
 *
 *	%VFL_TYPE_GRABBER - A frame grabber
 *
 *	%VFL_TYPE_VTX - A teletext device
 *
 *	%VFL_TYPE_VBI - Vertical blank data (undecoded)
 *
 *	%VFL_TYPE_RADIO - A radio card	
 */
 
int video_register_device(struct video_device *vfd, int type)
{
	int i=0;
	int base;
	int err;
	int end;
	char *name_base;
	
	switch(type)
	{
		case VFL_TYPE_GRABBER:
			base=0;
			end=64;
			name_base = "video";
			break;
		case VFL_TYPE_VTX:
			base=192;
			end=224;
			name_base = "vtx";
			break;
		case VFL_TYPE_VBI:
			base=224;
			end=240;
			name_base = "vbi";
			break;
		case VFL_TYPE_RADIO:
			base=64;
			end=128;
			name_base = "radio";
			break;
		default:
			return -1;
	}
	
	for(i=base;i<end;i++)
	{
		if(video_device[i]==NULL)
		{
			char name[16];

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
			sprintf (name, "v4l/%s%d", name_base, i - base);
			/*
			 *	Start the device root only. Anything else
			 *	has serious privacy issues.
			 */
			vfd->devfs_handle =
			    devfs_register (NULL, name, 0, DEVFS_FL_DEFAULT,
					    VIDEO_MAJOR, vfd->minor,
					    S_IFCHR | S_IRUSR | S_IWUSR, 0, 0,
					    &video_fops, NULL);
			return 0;
		}
	}
	return -ENFILE;
}

/**
 *	video_unregister_device - unregister a video4linux device
 *	@vfd: the device to unregister
 *
 *	This unregisters the passed device and deassigns the minor
 *	number. Future open calls will be met with errors.
 */
 
void video_unregister_device(struct video_device *vfd)
{
	if(video_device[vfd->minor]!=vfd)
		panic("vfd: bad unregister");
	devfs_unregister (vfd->devfs_handle);
	video_device[vfd->minor]=NULL;
	MOD_DEC_USE_COUNT;
}


static struct file_operations video_fops=
{
	llseek:		video_lseek,
	read:		video_read,
	write:		video_write,
	ioctl:		video_ioctl,
	mmap:		video_mmap,
	open:		video_open,
	release:	video_release,
	poll:		video_poll,
};

/*
 *	Initialise video for linux
 */
 
int __init videodev_init(void)
{
	struct video_init *vfli = video_init_list;
	
	printk(KERN_INFO "Linux video capture interface: v1.00\n");
	if(devfs_register_chrdev(VIDEO_MAJOR,"video_capture", &video_fops))
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
	devfs_unregister_chrdev(VIDEO_MAJOR, "video_capture");
}

#endif

EXPORT_SYMBOL(video_register_device);
EXPORT_SYMBOL(video_unregister_device);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("Device registrar for Video4Linux drivers");
