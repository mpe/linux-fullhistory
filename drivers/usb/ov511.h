#ifndef __LINUX_OV511_H
#define __LINUX_OV511_H

//#include <linux/list.h>

#define OV511_DEBUG	/* Turn on debug messages */

#ifdef OV511_DEBUG
#  define PDEBUG(level, fmt, args...) \
if (debug >= level) printk("ov511: " fmt "\n" , ## args)
#else
#  define PDEBUG(level, fmt, args...) do {} while(0)
#endif

/* Camera interface register numbers */
#define OV511_REG_CAMERA_DELAY_MODE			0x10
#define OV511_REG_CAMERA_EDGE_MODE			0x11
#define OV511_REG_CAMERA_CLAMPED_PIXEL_NUM	0x12
#define OV511_REG_CAMERA_CLAMPED_LINE_NUM	0x13
#define OV511_REG_CAMERA_PIXEL_DIVISOR		0x14
#define OV511_REG_CAMERA_LINE_DIVISOR		0x15
#define OV511_REG_CAMERA_DATA_INPUT_SELECT	0x16
#define OV511_REG_CAMERA_RESERVED_LINE_MODE	0x17
#define OV511_REG_CAMERA_BITMASK			0x18

/* Snapshot mode camera interface register numbers */
#define OV511_REG_SNAP_CAPTURED_FRAME		0x19
#define OV511_REG_SNAP_CLAMPED_PIXEL_NUM	0x1A
#define OV511_REG_SNAP_CLAMPED_LINE_NUM		0x1B
#define OV511_REG_SNAP_PIXEL_DIVISOR		0x1C
#define OV511_REG_SNAP_LINE_DIVISOR			0x1D
#define OV511_REG_SNAP_DATA_INPUT_SELECT	0x1E
#define OV511_REG_SNAP_BITMASK				0x1F

/* DRAM register numbers */
#define OV511_REG_DRAM_ENABLE_FLOW_CONTROL	0x20
#define OV511_REG_DRAM_READ_CYCLE_PREDICT	0x21
#define OV511_REG_DRAM_MANUAL_READ_CYCLE	0x22
#define OV511_REG_DRAM_REFRESH_COUNTER		0x23

/* ISO FIFO register numbers */
#define OV511_REG_FIFO_PACKET_SIZE			0x30
#define OV511_REG_FIFO_BITMASK				0x31

/* PIO register numbers */
#define OV511_REG_PIO_BITMASK				0x38
#define OV511_REG_PIO_DATA_PORT				0x39
#define OV511_REG_PIO_BIST					0x3E

/* I2C register numbers */
#define OV511_REG_I2C_CONTROL				0x40
#define OV511_REG_I2C_SLAVE_ID_WRITE		0x41
#define OV511_REG_I2C_SUB_ADDRESS_3_BYTE	0x42
#define OV511_REG_I2C_SUB_ADDRESS_2_BYTE	0x43
#define OV511_REG_I2C_SLAVE_ID_READ			0x44
#define OV511_REG_I2C_DATA_PORT				0x45
#define OV511_REG_I2C_CLOCK_PRESCALER		0x46
#define OV511_REG_I2C_TIME_OUT_COUNTER		0x47

/* I2C snapshot register numbers */
#define OV511_REG_I2C_SNAP_SUB_ADDRESS		0x48
#define OV511_REG_I2C_SNAP_DATA_PORT		0x49

/* System control register numbers */
#define OV511_REG_SYSTEM_RESET				0x50
#define 	OV511_RESET_UDC				0x01
#define 	OV511_RESET_I2C				0x02
#define 	OV511_RESET_FIFO			0x04
#define 	OV511_RESET_OMNICE			0x08
#define 	OV511_RESET_DRAM_INTF		0x10
#define 	OV511_RESET_CAMERA_INTF		0x20
#define		OV511_RESET_OV511			0x40
#define		OV511_RESET_NOREGS			0x3F	/* All but OV511 & regs */
#define 	OV511_RESET_ALL				0x7F
#define OV511_REG_SYSTEM_CLOCK_DIVISOR		0x51
#define OV511_REG_SYSTEM_SNAPSHOT			0x52
#define OV511_REG_SYSTEM_INIT         		0x53
#define OV511_REG_SYSTEM_USER_DEFINED		0x5E
#define OV511_REG_SYSTEM_CUSTOM_ID			0x5F

/* OmniCE register numbers */
#define OV511_OMNICE_PREDICATION_HORIZ_Y	0x70
#define OV511_OMNICE_PREDICATION_HORIZ_UV	0x71
#define OV511_OMNICE_PREDICATION_VERT_Y		0x72
#define OV511_OMNICE_PREDICATION_VERT_UV	0x73
#define OV511_OMNICE_QUANTIZATION_HORIZ_Y	0x74
#define OV511_OMNICE_QUANTIZATION_HORIZ_UV	0x75
#define OV511_OMNICE_QUANTIZATION_VERT_Y	0x76
#define OV511_OMNICE_QUANTIZATION_VERT_UV	0x77
#define OV511_OMNICE_ENABLE					0x78
#define OV511_OMNICE_LUT_ENABLE				0x79		
#define OV511_OMNICE_Y_LUT_BEGIN			0x80
#define OV511_OMNICE_Y_LUT_END				0x9F
#define OV511_OMNICE_UV_LUT_BEGIN			0xA0
#define OV511_OMNICE_UV_LUT_END				0xBF

/* Alternate numbers for various max packet sizes */
#define OV511_ALTERNATE_SIZE_992	0
#define OV511_ALTERNATE_SIZE_993	1
#define OV511_ALTERNATE_SIZE_768	2
#define OV511_ALTERNATE_SIZE_769	3
#define OV511_ALTERNATE_SIZE_512	4
#define OV511_ALTERNATE_SIZE_513	5
#define OV511_ALTERNATE_SIZE_257	6
#define OV511_ALTERNATE_SIZE_0		7

/* ov7610 registers */
#define OV7610_REG_GAIN          0x00
#define OV7610_REG_BLUE          0x01
#define OV7610_REG_RED           0x02
#define OV7610_REG_SAT           0x03
#define OV7610_REG_CNT           0x05
#define OV7610_REG_BRT           0x06
#define OV7610_REG_BLUE_BIAS     0x0C
#define OV7610_REG_RED_BIAS      0x0D
#define OV7610_REG_GAMMA_COEFF   0x0E
#define OV7610_REG_WB_RANGE      0x0F
#define OV7610_REG_EXP           0x10
#define OV7610_REG_CLOCK         0x11
#define OV7610_REG_COM_A         0x12
#define OV7610_REG_COM_B         0x13
#define OV7610_REG_COM_C         0x14
#define OV7610_REG_COM_D         0x15
#define OV7610_REG_FIELD_DIVIDE  0x16
#define OV7610_REG_HWIN_START    0x17
#define OV7610_REG_HWIN_END      0x18
#define OV7610_REG_VWIN_START    0x19
#define OV7610_REG_VWIN_END      0x1A
#define OV7610_REG_PIXEL_SHIFT   0x1B
#define OV7610_REG_ID_HIGH       0x1C
#define OV7610_REG_ID_LOW        0x1D
#define OV7610_REG_COM_E         0x20
#define OV7610_REG_YOFFSET       0x21
#define OV7610_REG_UOFFSET       0x22
#define OV7610_REG_ECW           0x24
#define OV7610_REG_ECB           0x25
#define OV7610_REG_COM_F         0x26
#define OV7610_REG_COM_G         0x27
#define OV7610_REG_COM_H         0x28
#define OV7610_REG_COM_I         0x29
#define OV7610_REG_FRAMERATE_H   0x2A
#define OV7610_REG_FRAMERATE_L   0x2B
#define OV7610_REG_ALC           0x2C
#define OV7610_REG_COM_J         0x2D
#define OV7610_REG_VOFFSET       0x2E
#define OV7610_REG_YGAMMA        0x33
#define OV7610_REG_BIAS_ADJUST   0x34
#define OV7610_REG_COM_L         0x35
#define OV7610_REG_COM_K         0x38


#define STREAM_BUF_SIZE	(PAGE_SIZE * 4)

#define SCRATCH_BUF_SIZE 384

#define FRAMES_PER_DESC		10  /* FIXME - What should this be? */
#define FRAME_SIZE_PER_DESC	993	/* FIXME - Shouldn't be hardcoded */

// FIXME - should this be 0x81 (endpoint address) or 0x01 (endpoint number)?
#define OV511_ENDPOINT_ADDRESS 0x81 /* Address of isoc endpoint */

// CAMERA SPECIFIC
// FIXME - these can vary between specific models
#define OV7610_I2C_WRITE_ID 0x42
#define OV7610_I2C_READ_ID  0x43
#define OV511_I2C_CLOCK_PRESCALER 0x03

/* Prototypes */
int usb_ov511_reg_read(struct usb_device *dev, unsigned char reg);
int usb_ov511_reg_write(struct usb_device *dev,
                        unsigned char reg,
                        unsigned char value);


enum {
	STATE_SCANNING,		/* Scanning for start */
	STATE_HEADER,		/* Parsing header */
	STATE_LINES,		/* Parsing lines */
};

struct usb_device;

struct ov511_sbuf {
	char *data;
	urb_t *urb;
};

enum {
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
	FRAME_READY,		/* Ready to start grabbing */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,			/* Finished grabbing, but not been synced yet */
	FRAME_ERROR,		/* Something bad happened while processing */
};

struct ov511_regvals {
	enum {
	  OV511_DONE_BUS,
	  OV511_REG_BUS,
	  OV511_I2C_BUS,
	} bus;
	unsigned char reg;
	unsigned char val;
};

struct ov511_frame {
	char *data;		/* Frame buffer */

	int depth;		/* Bytes per pixel */
	int width;		/* Width application is expecting */
	int height;		/* Height */

	int hdrwidth;		/* Width the frame actually is */
	int hdrheight;		/* Height */

	int sub_flag;		/* Sub-capture mode for this frame? */
	int format;		/* Format for this frame */
	int segsize;		/* How big is each segment from the camera? */

	volatile int grabstate;	/* State of grabbing */
	int scanstate;		/* State of scanning */

	int curline;		/* Line of frame we're working on */
	int curpix;
	int segment;		/* Segment from the incoming data */

	long scanlength;	/* uncompressed, raw data length of frame */
	long bytes_read;	/* amount of scanlength that has been read from *data */

	wait_queue_head_t wq;	/* Processes waiting */

	int snapshot;			/* True if frame was a snapshot */
};

#define OV511_NUMFRAMES	2
#define OV511_NUMSBUF	2

struct usb_ov511 {
	struct video_device vdev;
	
	/* Device structure */
	struct usb_device *dev;

	unsigned char customid; /* Type of camera */

	unsigned char iface;
	
	struct semaphore lock;
	int user;			/* user count for exclusive use */

	int streaming;		/* Are we streaming Isochronous? */
	int grabbing;		/* Are we grabbing? */

	int compress;		/* Should the next frame be compressed? */

	char *fbuf;			/* Videodev buffer area */

	int sub_flag;		/* Pix Array subcapture on flag */
	int subx;		/* Pix Array subcapture x offset */
	int suby;		/* Pix Array subcapture y offset */
	int subw;		/* Pix Array subcapture width */
	int subh;		/* Pix Array subcapture height */

	int curframe;		/* Current receiving sbuf */
	struct ov511_frame frame[OV511_NUMFRAMES];	

	int cursbuf;		/* Current receiving sbuf */
	struct ov511_sbuf sbuf[OV511_NUMSBUF];

	/* Scratch space from the Isochronous pipe */
	unsigned char scratch[SCRATCH_BUF_SIZE];
	int scratchlen;

	wait_queue_head_t wq;	/* Processes waiting */

	int snap_enabled;   /* Snapshot mode enabled */
};

#endif

