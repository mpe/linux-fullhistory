#ifndef __SAA7146_VV__
#define __SAA7146_VV__

#include <linux/videodev2.h>

#include <media/saa7146.h>
#include <media/video-buf.h>

#define MAX_SAA7146_CAPTURE_BUFFERS	32	/* arbitrary */
#define BUFFER_TIMEOUT     (HZ/2)  /* 0.5 seconds */

#define WRITE_RPS0(x) do { \
	static int count = 0;	\
	dev->d_rps0.cpu_addr[ count++ ] = cpu_to_le32(x); \
	} while (0);

#define WRITE_RPS1(x) do { \
	static int count = 0;	\
	dev->d_rps1.cpu_addr[ count++ ] = cpu_to_le32(x); \
	} while (0);

struct	saa7146_video_dma {
	u32 base_odd;
	u32 base_even;
	u32 prot_addr;
	u32 pitch;
	u32 base_page;
	u32 num_line_byte;
};

struct saa7146_format {
	char	*name;
	int   	pixelformat;
	u32	trans;
	u8	depth;
	int	swap;
};

struct saa7146_standard
{
	char          *name;
	v4l2_std_id   id;

	int v_offset;
	int v_field;
	int v_calc;
	
	int h_offset;
	int h_pixels;
	int h_calc;
	
	int v_max_out;
	int h_max_out;
};

/* buffer for one video/vbi frame */
struct saa7146_buf {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	/* saa7146 specific */
	struct v4l2_pix_format  *fmt;
	int (*activate)(struct saa7146_dev *dev,
			struct saa7146_buf *buf,
			struct saa7146_buf *next);

	/* page tables */
	struct saa7146_pgtable  pt[3];
};

struct saa7146_dmaqueue {
	struct saa7146_dev	*dev;
	struct saa7146_buf	*curr;
	struct list_head	queue;
	struct timer_list	timeout;
};

struct saa7146_overlay {
	struct saa7146_fh	*fh;
	struct v4l2_window	win;
	struct v4l2_clip	clips[16];
	int			nclips;
};

/* per open data */
struct saa7146_fh {
	struct saa7146_dev	*dev;
	/* if this is a vbi or capture open */
	enum v4l2_buf_type	type;

	/* video overlay */
	struct saa7146_overlay	ov;
	
	/* video capture */
	struct videobuf_queue	video_q;
	struct v4l2_pix_format	video_fmt;

	/* vbi capture */
	struct videobuf_queue	vbi_q;
	struct v4l2_vbi_format	vbi_fmt;
	struct timer_list	vbi_read_timeout;
};

struct saa7146_vv
{
	int vbi_minor;

	/* vbi capture */
	struct saa7146_dmaqueue		vbi_q;
	/* vbi workaround interrupt queue */
        wait_queue_head_t		vbi_wq;
	int				vbi_fieldcount;
	struct saa7146_fh		*vbi_streaming;

	int video_minor;

	/* video overlay */
	struct v4l2_framebuffer		ov_fb;
	struct saa7146_format		*ov_fmt;
	struct saa7146_overlay		*ov_data;

	/* video capture */
	struct saa7146_dmaqueue		video_q;
	struct saa7146_fh		*streaming;

	/* common: fixme? shouldn't this be in saa7146_fh?
	   (this leads to a more complicated question: shall the driver
	   store the different settings (for example S_INPUT) for every open
	   and restore it appropriately, or should all settings be common for
	   all opens? currently, we do the latter, like all other
	   drivers do... */
	struct saa7146_standard	*standard;
	
	int	vflip;
	int 	hflip;
	int 	current_hps_source;
	int 	current_hps_sync;

	struct saa7146_dma	d_clipping;	/* pointer to clipping memory */
};

#define SAA7146_EXCLUSIVE	0x1
#define SAA7146_BEFORE		0x2
#define SAA7146_AFTER		0x4

struct saa7146_extension_ioctls
{
	unsigned int	cmd;
	int		flags;	
};

/* flags */
#define SAA7146_EXT_SWAP_ODD_EVEN       0x1     /* needs odd/even fields swapped */

struct saa7146_ext_vv
{
	/* informations about the video capabilities of the device */
	int	inputs;			
	int	audios;			
	u32	capabilities;
	int 	flags;

	/* additionally supported transmission standards */
	struct saa7146_standard *stds;
	int num_stds;
	int (*std_callback)(struct saa7146_dev*, struct saa7146_standard *);
		
	struct saa7146_extension_ioctls *ioctls;
	int (*ioctl)(struct saa7146_dev*, unsigned int cmd, void *arg);
};

struct saa7146_use_ops  {
        void (*init)(struct saa7146_dev *, struct saa7146_vv *);
        void(*open)(struct saa7146_dev *, struct saa7146_fh *);
        void (*release)(struct saa7146_dev *, struct saa7146_fh *,struct file *);
        void (*irq_done)(struct saa7146_dev *, unsigned long status);
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);
        int (*capture_begin)(struct saa7146_fh *);
        int (*capture_end)(struct saa7146_fh *);
};

/* from saa7146_fops.c */
int saa7146_register_device(struct video_device *vid, struct saa7146_dev* dev, char *name, int type);
int saa7146_unregister_device(struct video_device *vid, struct saa7146_dev* dev);
void saa7146_buffer_finish(struct saa7146_dev *dev, struct saa7146_dmaqueue *q, int state);
void saa7146_buffer_next(struct saa7146_dev *dev, struct saa7146_dmaqueue *q,int vbi);
int saa7146_buffer_queue(struct saa7146_dev *dev, struct saa7146_dmaqueue *q, struct saa7146_buf *buf);
void saa7146_buffer_timeout(unsigned long data);
void saa7146_dma_free(struct saa7146_dev *dev,struct saa7146_buf *buf);

int saa7146_vv_init(struct saa7146_dev* dev);
int saa7146_vv_release(struct saa7146_dev* dev);


/* from saa7146_hlp.c */
void saa7146_set_overlay(struct saa7146_dev *dev, struct saa7146_fh *fh, int v);
void saa7146_set_capture(struct saa7146_dev *dev, struct saa7146_buf *buf, struct saa7146_buf *next);
void saa7146_write_out_dma(struct saa7146_dev* dev, int which, struct saa7146_video_dma* vdma) ;
void saa7146_set_hps_source_and_sync(struct saa7146_dev *saa, int source, int sync);
void saa7146_set_gpio(struct saa7146_dev *saa, u8 pin, u8 data);

/* from saa7146_video.c */
extern struct saa7146_use_ops saa7146_video_uops;

/* from saa7146_vbi.c */
extern struct saa7146_use_ops saa7146_vbi_uops;

/* saa7146 source inputs */
#define SAA7146_HPS_SOURCE_PORT_A	0x00
#define SAA7146_HPS_SOURCE_PORT_B	0x01
#define SAA7146_HPS_SOURCE_YPB_CPA	0x02
#define SAA7146_HPS_SOURCE_YPA_CPB	0x03

/* sync inputs */
#define SAA7146_HPS_SYNC_PORT_A		0x00
#define SAA7146_HPS_SYNC_PORT_B		0x01

/* number of vertical active lines */
#define V_ACTIVE_LINES_PAL	576
#define V_ACTIVE_LINES_NTSC	480
#define V_ACTIVE_LINES_SECAM	576

/* number of lines in a field for HPS to process */
#define V_FIELD_PAL	288
#define V_FIELD_NTSC	240
#define V_FIELD_SECAM	288

/* number of lines of vertical offset before processing */
#define V_OFFSET_PAL	0x17
#define V_OFFSET_NTSC	0x16
#define V_OFFSET_SECAM	0x14

/* number of horizontal pixels to process */
#define H_PIXELS_PAL	680
#define H_PIXELS_NTSC	708
#define H_PIXELS_SECAM	720

/* horizontal offset of processing window */
#define H_OFFSET_PAL	0x14
#define H_OFFSET_NTSC	0x06
#define H_OFFSET_SECAM	0x14

#define SAA7146_PAL_VALUES 	V_OFFSET_PAL, V_FIELD_PAL, V_ACTIVE_LINES_PAL, H_OFFSET_PAL, H_PIXELS_PAL, H_PIXELS_PAL+1, V_ACTIVE_LINES_PAL, 768
#define SAA7146_NTSC_VALUES	V_OFFSET_NTSC, V_FIELD_NTSC, V_ACTIVE_LINES_NTSC, H_OFFSET_NTSC, H_PIXELS_NTSC, H_PIXELS_NTSC+1, V_ACTIVE_LINES_NTSC, 640
#define SAA7146_SECAM_VALUES	V_OFFSET_SECAM, V_FIELD_SECAM, V_ACTIVE_LINES_SECAM, H_OFFSET_SECAM, H_PIXELS_SECAM, H_PIXELS_SECAM+1, V_ACTIVE_LINES_SECAM, 768

/* some memory sizes */
#define SAA7146_CLIPPING_MEM	(14*PAGE_SIZE)

/* some defines for the various clipping-modes */
#define SAA7146_CLIPPING_RECT		0x4
#define SAA7146_CLIPPING_RECT_INVERTED	0x5
#define SAA7146_CLIPPING_MASK		0x6
#define SAA7146_CLIPPING_MASK_INVERTED	0x7

/* output formats: each entry holds four informations */
#define RGB08_COMPOSED	0x0217 /* composed is used in the sense of "not-planar" */
/* this means: planar?=0, yuv2rgb-conversation-mode=2, dither=yes(=1), format-mode = 7 */
#define RGB15_COMPOSED	0x0213
#define RGB16_COMPOSED	0x0210
#define RGB24_COMPOSED	0x0201
#define RGB32_COMPOSED	0x0202

#define Y8			0x0006
#define YUV411_COMPOSED		0x0003
#define YUV422_COMPOSED		0x0000
/* this means: planar?=1, yuv2rgb-conversion-mode=0, dither=no(=0), format-mode = b */
#define YUV411_DECOMPOSED	0x100b
#define YUV422_DECOMPOSED	0x1009
#define YUV420_DECOMPOSED	0x100a

#define IS_PLANAR(x) (x & 0xf000)

/* misc defines */
#define SAA7146_NO_SWAP		(0x0)
#define SAA7146_TWO_BYTE_SWAP 	(0x1)
#define SAA7146_FOUR_BYTE_SWAP	(0x2)

#endif
