#ifndef __LINUX_CPIA_H
#define __LINUX_CPIA_H

#include <linux/list.h>

#define USB_REQ_CPIA_GET_VERSION		0x01
#define USB_REQ_CPIA_GET_PNP_ID			0x02
#define USB_REQ_CPIA_GET_CAMERA_STATUS		0x03
#define USB_REQ_CPIA_GOTO_HI_POWER		0x04
#define USB_REQ_CPIA_GOTO_LO_POWER		0x05
/* No 0x06 */
#define USB_REQ_CPIA_GOTO_SUSPEND		0x07
#define USB_REQ_CPIA_GOTO_PASS_THROUGH		0x08
/* No 0x09 */
#define USB_REQ_CPIA_MODIFY_CAMERA_STATUS	0x0A

#define USB_REQ_CPIA_READ_VC_REGS		0x21
#define USB_REQ_CPIA_WRITE_BC_REG		0x22
#define USB_REQ_CPIA_READ_MC_PORTS		0x23
#define USB_REQ_CPIA_WRITE_MC_PORT		0x24
#define USB_REQ_CPIA_SET_BAUD_RATE		0x25
#define USB_REQ_CPIA_SET_ECP_TIMING		0x26
#define USB_REQ_CPIA_READ_IDATA			0x27
#define USB_REQ_CPIA_WRITE_IDATA		0x28
#define USB_REQ_CPIA_GENERIC_CALL		0x29
#define USB_REQ_CPIA_I2CSTART			0x2A
#define USB_REQ_CPIA_I2CSTOP			0x2B
#define USB_REQ_CPIA_I2CWRITE			0x2C
#define USB_REQ_CPIA_I2CREAD			0x2D

#define USB_REQ_CPIA_GET_VP_VERSION		0xA1
#define USB_REQ_CPIA_SET_COLOUR_PARAMS		0xA3
#define USB_REQ_CPIA_SET_EXPOSURE		0xA4
/* No 0xA5 */
#define USB_REQ_CPIA_SET_COLOUR_BALANCE		0xA6
#define USB_REQ_CPIA_SET_SENSOR_FPS		0xA7
#define USB_REQ_CPIA_SET_VP_DEFAULTS		0xA8
#define USB_REQ_CPIA_SET_APCOR			0xA9
#define USB_REQ_CPIA_SET_FLICKER_CTRL		0xAA
#define USB_REQ_CPIA_SET_VL_OFFSET		0xAB

#define USB_REQ_CPIA_GET_COLOUR_PARAMETERS	0xB0
#define USB_REQ_CPIA_GET_COLOUR_BALANCE		0xB1
#define USB_REQ_CPIA_GET_EXPOSURE		0xB2
#define USB_REQ_CPIA_SET_SENSOR_MATRIX		0xB3

#define USB_REQ_CPIA_COLOUR_BARS		0xBD
#define USB_REQ_CPIA_READ_VP_REGS		0xBE
#define USB_REQ_CPIA_WRITE_VP_REGS		0xBF

#define USB_REQ_CPIA_GRAB_FRAME			0xC1
#define USB_REQ_CPIA_UPLOAD_FRAME		0xC2
#define  WAIT_FOR_NEXT_FRAME			0
#define  FORCE_FRAME_UPLOAD			1
#define USB_REQ_CPIA_SET_GRAB_MODE		0xC3
#define USB_REQ_CPIA_INIT_STREAM_CAP		0xC4
#define USB_REQ_CPIA_FINI_STREAM_CAP		0xC5
#define USB_REQ_CPIA_START_STREAM_CAP		0xC6
#define USB_REQ_CPIA_END_STREAM_CAP		0xC7
#define USB_REQ_CPIA_SET_FORMAT			0xC8
#define  FORMAT_QCIF	0
#define  FORMAT_CIF	1
#define  FORMAT_YUYV	0
#define  FORMAT_UYVY	1
#define  FORMAT_420	0
#define  FORMAT_422	1
#define USB_REQ_CPIA_SET_ROI			0xC9
#define USB_REQ_CPIA_SET_COMPRESSION		0xCA
#define  COMP_DISABLED	0
#define  COMP_AUTO	1
#define  COMP_MANUAL	2
#define  DONT_DECIMATE	0
#define  DECIMATE	1
#define USB_REQ_CPIA_SET_COMPRESSION_TARGET	0xCB
#define  TARGET_QUALITY		0
#define  TARGET_FRAMERATE	1
#define USB_REQ_CPIA_SET_YUV_THRESH		0xCC
#define USB_REQ_CPIA_SET_COMPRESSION_PARAMS	0xCD
#define USB_REQ_CPIA_DISCARD_FRAME		0xCE

#define USB_REQ_CPIA_OUTPUT_RS232		0xE1
#define USB_REQ_CPIA_ABORT_PROCESS		0xE4
#define USB_REQ_CPIA_SET_DRAM_PAGE		0xE5
#define USB_REQ_CPIA_START_DRAM_UPLOAD		0xE6
#define USB_REQ_CPIA_START_DUMMY_STREAM		0xE8
#define USB_REQ_CPIA_ABORT_STREAM		0xE9
#define USB_REQ_CPIA_DOWNLOAD_DRAM		0xEA
/* #define USB_REQ_CPIA_NULL_CMD		0x?? */

#define STREAM_BUF_SIZE		(PAGE_SIZE * 4)
/* #define STREAM_BUF_SIZE	(FRAMES_PER_DESC * FRAME_SIZE_PER_DESC) */

#define SCRATCH_BUF_SIZE	(STREAM_BUF_SIZE * 2)

#define FRAMES_PER_DESC		10
#define FRAME_SIZE_PER_DESC	960	/* Shouldn't be hardcoded */

enum {
	STATE_SCANNING,		/* Scanning for start */
	STATE_HEADER,		/* Parsing header */
	STATE_LINES,		/* Parsing lines */
};

#define CPIA_MAGIC	0x1968
struct cpia_frame_header {
	__u16 magic;		/* 0 - 1 */
	__u16 timestamp;	/* 2 - 3 */
	__u16 unused;		/* 4 - 5 */
	__u16 timestamp1;	/* 6 - 7 */
	__u8  unused1[8];	/* 8 - 15 */
	__u8  video_size;	/* 16 0 = QCIF, 1 = CIF */
	__u8  sub_sample;	/* 17 0 = 4:2:0, 1 = 4:2:2 */
	__u8  yuv_order;	/* 18 0 = YUYV, 1 = UYVY */
	__u8  unused2[5];	/* 19 - 23 */
	__u8  col_start;	/* 24 */
	__u8  col_end;		/* 25 */
	__u8  row_start;	/* 26 */
	__u8  row_end;		/* 27 */
	__u8  comp_enable;	/* 28 0 = non compressed, 1 = compressed */
	__u8  decimation;	/* 29 0 = no decimation, 1 = decimation */
	__u8  y_thresh;		/* 30 */
	__u8  uv_thresh;	/* 31 */
	__u8  system_state;	/* 32 */
	__u8  grab_state;	/* 33 */
	__u8  stream_state;	/* 34 */
	__u8  fatal_error;	/* 35 */
	__u8  cmd_error;	/* 36 */
	__u8  debug_flags;	/* 37 */
	__u8  camera_state_7;	/* 38 */
	__u8  camera_state_8;	/* 39 */
	__u8  cr_achieved;	/* 40 */
	__u8  fr_achieved;	/* 41 */
	__u8  unused3[22];	/* 42 - 63 */
};

struct usb_device;

struct cpia_sbuf {
	char *data;
	urb_t *urb;
};

enum {
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
	FRAME_READY,		/* Ready to start grabbing */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,		/* Finished grabbing, but not been synced yet */
	FRAME_ERROR,		/* Something bad happened while processing */
};

struct cpia_frame {
	char *data;		/* Frame buffer */

	struct cpia_frame_header header;	/* Header from stream */

	int width;		/* Width application is expecting */
	int height;		/* Height */

	int hdrwidth;		/* Width the frame actually is */
	int hdrheight;		/* Height */

	volatile int grabstate;	/* State of grabbing */
	int scanstate;		/* State of scanning */

	int curline;		/* Line of frame we're working on */

	long scanlength;	/* uncompressed, raw data length of frame */
	long bytes_read;	/* amount of scanlength that has been read from *data */

	wait_queue_head_t wq;	/* Processes waiting */
};

#define CPIA_NUMFRAMES	2
#define CPIA_NUMSBUF	2

struct usb_cpia {
	struct video_device vdev;

	/* Device structure */
	struct usb_device *dev;

	unsigned char iface;

	struct semaphore lock;
	int user;		/* user count for exclusive use */

	int streaming;		/* Are we streaming Isochronous? */
	int grabbing;		/* Are we grabbing? */

	int compress;		/* Should the next frame be compressed? */

	char *fbuf;		/* Videodev buffer area */

	int curframe;
	struct cpia_frame frame[CPIA_NUMFRAMES];	/* Double buffering */

	int cursbuf;		/* Current receiving sbuf */
	struct cpia_sbuf sbuf[CPIA_NUMSBUF];		/* Double buffering */

	/* Scratch space from the Isochronous pipe */
	unsigned char scratch[SCRATCH_BUF_SIZE];
	int scratchlen;
};

#endif
