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
#define USB_REQ_CPIA_SET_GRAB_MODE		0xC3
#define USB_REQ_CPIA_INIT_STREAM_CAP		0xC4
#define USB_REQ_CPIA_FINI_STREAM_CAP		0xC5
#define USB_REQ_CPIA_START_STREAM_CAP		0xC6
#define USB_REQ_CPIA_END_STREAM_CAP		0xC7
#define USB_REQ_CPIA_SET_FORMAT			0xC8
#define USB_REQ_CPIA_SET_ROI			0xC9
#define USB_REQ_CPIA_SET_COMPRESSION		0xCA
#define USB_REQ_CPIA_SET_COMPRESSION_TARGET	0xCB
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

#define CPIA_QCIF	0
#define CPIA_CIF	1

#define CPIA_YUYV	0
#define CPIA_UYVY	1

#define STREAM_BUF_SIZE	(PAGE_SIZE * 4)

#define SCRATCH_BUF_SIZE (STREAM_BUF_SIZE * 2)

enum {
	STATE_SCANNING,		/* Scanning for start */
	STATE_HEADER,		/* Parsing header */
	STATE_LINES,		/* Parsing lines */
};

struct usb_device;

struct cpia_sbuf {
	char *data;
	int len;
	void *isodesc;
};

enum {
	FRAME_READY,		/* Ready to grab into */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,		/* Finished grabbing, but not been synced yet */
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
};

struct cpia_frame {
	char *data;
	int width;
	int height;
	int state;
};

struct usb_cpia {
	struct video_device vdev;

	/* Device structure */
	struct usb_device *dev;

	int streaming;

	char *fbuf;			/* Videodev buffer area */

	int curframe;
	struct cpia_frame frame[2];	/* Double buffering */

	int receivesbuf;		/* Current receiving sbuf */
	struct cpia_sbuf sbuf[3];	/* Triple buffering */

	int state;			/* Current scanning state */
	int curline;

	char scratch[SCRATCH_BUF_SIZE];
	int scratchlen;

	wait_queue_head_t wq;
};

#endif

