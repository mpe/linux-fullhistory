#ifndef __LINUX_VIDEODEV_H
#define __LINUX_VIDEODEV_H

#ifdef __KERNEL__

struct video_device
{
	char name[32];
	int type;
	int hardware;

	int (*open)(struct video_device *, int mode);
	void (*close)(struct video_device *);
	long (*read)(struct video_device *, char *, unsigned long, int noblock);
	/* Do we need a write method ? */
	long (*write)(struct video_device *, const char *, unsigned long, int noblock);
	int (*ioctl)(struct video_device *, unsigned int , void *);
	int (*mmap)(struct video_device *, const char *, unsigned long);
	int (*initialize)(struct video_device *);	
	void *private;
	int busy;
	int minor;
};

extern int videodev_init(void);
#define VIDEO_MAJOR	81
extern int video_register_device(struct video_device *);
extern void video_unregister_device(struct video_device *);
#endif


#define VID_TYPE_CAPTURE	1	/* Can capture */
#define VID_TYPE_TUNER		2	/* Can tune */
#define VID_TYPE_TELETEXT	4	/* Does teletext */
#define VID_TYPE_OVERLAY	8	/* Overlay onto frame buffer */
#define VID_TYPE_CHROMAKEY	16	/* Overlay by chromakey */
#define VID_TYPE_CLIPPING	32	/* Can clip */
#define VID_TYPE_FRAMERAM	64	/* Uses the frame buffer memory */
#define VID_TYPE_SCALES		128	/* Scalable */
#define VID_TYPE_MONOCHROME	256	/* Monochrome only */


struct video_capability
{
	char	name[32];
	int type;
	int channels;	/* Num channels */
	int audios;	/* Num audio devices */
	int maxwidth;	/* Supported width */
	int maxheight;	/* And height */
	int minwidth;	/* Supported width */
	int minheight;	/* And height */
};


struct video_channel
{
	int channel;
	char name[32];
	int tuners;
	__u32  flags;
#define VIDEO_VC_TUNER		1	/* Channel has a tuner */
#define VIDEO_VC_AUDIO		2	/* Channel has audio */
	__u16  type;
#define VIDEO_TYPE_TV		1
#define VIDEO_TYPE_CAMERA	2	
};

struct video_tuner
{
	int tuner;
	char name[32];
	ulong rangelow, rangehigh;	/* Tuner range */
	__u32 flags;
#define VIDEO_TUNER_PAL		1
#define VIDEO_TUNER_NTSC	2
#define VIDEO_TUNER_SECAM	4
	__u16 mode;			/* PAL/NTSC/SECAM/OTHER */
#define VIDEO_MODE_PAL		0
#define VIDEO_MODE_NTSC		1
#define VIDEO_MODE_SECAM	2
#define VIDEO_MODE_AUTO		3
};

struct video_picture
{
	__u16	brightness;
	__u16	hue;
	__u16	colour;
	__u16	contrast;
	__u16	whiteness;	/* Black and white only */
	__u16	depth;		/* Capture depth */
	__u16   palette;	/* Palette in use */
#define VIDEO_PALETTE_GREY	1	/* Linear greyscale */
#define VIDEO_PALETTE_HI240	2	/* High 240 cube (BT848) */
#define VIDEO_PALETTE_RGB565	3	/* 565 16 bit RGB */
#define VIDEO_PALETTE_RGB24	4	/* 24bit RGB */
#define VIDEO_PALETTE_RGB32	5	/* 32bit RGB */	
#define VIDEO_PALETTE_RGB555	6	/* 555 15bit RGB */
};

struct video_audio
{
	int	audio;		/* Audio channel */
	__u16	volume;		/* If settable */
	__u16	bass, treble;
	__u32	flags;
#define VIDEO_AUDIO_MUTE	1
#define VIDEO_AUDIO_MUTABLE	2
#define VIDEO_AUDIO_VOLUME	4
#define VIDEO_AUDIO_BASS	8
#define VIDEO_AUDIO_TREBLE	16	
	char    name[16];
};

struct video_clip
{
	__s32	x,y;
	__s32	width, height;
	struct	video_clip *next;	/* For user use/driver use only */
};

struct video_window
{
	__u32	x,y;
	__u32	width,height;
	__u32	chromakey;
	__u32	flags;
	struct	video_clip *clips;	/* Set only */
	int	clipcount;
#define VIDEO_WINDOW_INTERLACE	1
};

struct video_buffer
{
	void	*base;
	int	height,width;
	int	depth;
	int	bytesperline;
};


struct video_key
{
	__u8	key[8];
	__u32	flags;
};
	
#define VIDIOCGCAP		_IOR('v',1,struct video_capability)	/* Get capabilities */
#define VIDIOCGCHAN		_IOWR('v',2,struct video_channel)	/* Get channel info (sources) */
#define VIDIOCSCHAN		_IOW('v',3,int)				/* Set channel 	*/
#define VIDIOCGTUNER		_IOWR('v',4,struct video_tuner)		/* Get tuner abilities */
#define VIDIOCSTUNER		_IOW('v',5,struct video_tuner)		/* Tune the tuner for the current channel */
#define VIDIOCGPICT		_IOR('v',6,struct video_picture)	/* Get picture properties */
#define VIDIOCSPICT		_IOW('v',7,struct video_picture)	/* Set picture properties */
#define VIDIOCCAPTURE		_IOW('v',8,int)				/* Start, end capture */
#define VIDIOCGWIN		_IOR('v',9, struct video_window)	/* Set the video overlay window */
#define VIDIOCSWIN		_IOW('v',10, struct video_window)	/* Set the video overlay window - passes clip list for hardware smarts , chromakey etc */
#define VIDIOCGFBUF		_IOR('v',11, struct video_buffer)	/* Get frame buffer */
#define VIDIOCSFBUF		_IOW('v',12, struct video_buffer)	/* Set frame buffer - root only */
#define VIDIOCKEY		_IOR('v',13, struct video_key)		/* Video key event - to dev 255 is to all - cuts capture on all DMA windows with this key (0xFFFFFFFF == all) */
#define VIDIOCGFREQ		_IOR('v',15, unsigned long)		/* Set tuner */
#define VIDIOCSFREQ		_IOW('v',15, unsigned long)		/* Set tuner */
#define VIDIOCGAUDIO		_IOR('v',16, struct video_audio)	/* Get audio info */
#define VIDIOCSAUDIO		_IOW('v',17, struct video_audio)	/* Audio source, mute etc */


#define BASE_VIDIOCPRIVATE	192		/* 192-255 are private */


#define VID_HARDWARE_BT848	1
#define VID_HARDWARE_QCAM_BW	2
#define VID_HARDWARE_PMS	3

#endif
