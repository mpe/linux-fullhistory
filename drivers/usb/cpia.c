/*
 * USB CPiA Video Camera driver
 *
 * Supports CPiA based Video Cameras. Many manufacturers use this chipset.
 *
 * (C) Copyright 1999-2000 Johannes Erdfelt, jerdfelt@valinux.com
 * (C) Copyright 1999 Randy Dunlap
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/videodev.h>
#include <linux/vmalloc.h>
#include <linux/wrapper.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

#include <asm/io.h>

#include "cpia.h"

static int debug = 0;
MODULE_PARM(debug, "i");

/* Video Size 384 x 288 x 3 bytes for RGB */
/* 384 because xawtv tries to grab 384 even though we tell it 352 is our max */
#define MAX_FRAME_SIZE (384 * 288 * 3)

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

static struct usb_driver cpia_driver;

/* Given PGD from the address space's page table, return the kernel
 * virtual mapping of the physical memory mapped at ADR.
 */
static inline unsigned long uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
	unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;

	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, adr);
		if (!pmd_none(*pmd)) {
			ptep = pte_offset(pmd, adr);
			pte = *ptep;
			if (pte_present(pte))
				ret = page_address(pte_page(pte)) | (adr & (PAGE_SIZE-1));
		}
	}
	MDEBUG(printk("uv2kva(%lx-->%lx)", adr, ret));
	return ret;
}

static inline unsigned long uvirt_to_bus(unsigned long adr)
{
	unsigned long kva, ret;

	kva = uvirt_to_kva(pgd_offset(current->mm, adr), adr);
	ret = virt_to_bus((void *)kva);
	MDEBUG(printk("uv2b(%lx-->%lx)", adr, ret));
	return ret;
}

static inline unsigned long kvirt_to_bus(unsigned long adr)
{
	unsigned long va, kva, ret;

	va = VMALLOC_VMADDR(adr);
	kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = virt_to_bus((void *)kva);
	MDEBUG(printk("kv2b(%lx-->%lx)", adr, ret));
	return ret;
}

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long kvirt_to_pa(unsigned long adr)
{
	unsigned long va, kva, ret;

	va = VMALLOC_VMADDR(adr);
	kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = __pa(kva);
	MDEBUG(printk("kv2pa(%lx-->%lx)", adr, ret));
	return ret;
}

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr, page;

	/* Round it off to PAGE_SIZE */
	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	mem = vmalloc(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		page = kvirt_to_pa(adr);
		mem_map_reserve(MAP_NR(__va(page)));
		adr += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr, page;

	if (!mem)
		return;

	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	adr=(unsigned long) mem;
	while (size > 0) {
		page = kvirt_to_pa(adr);
		mem_map_unreserve(MAP_NR(__va(page)));
		adr += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
	vfree(mem);
}

static int usb_cpia_get_version(struct usb_device *dev, void *buf)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_CPIA_GET_VERSION,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, 0, buf, 4, HZ);
}

#ifdef NOTUSED
static int usb_cpia_get_pnp_id(struct usb_device *dev, void *buf)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_CPIA_GET_PNP_ID,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, 0, buf, 6, HZ);
}
#endif

#ifdef NOTUSED
static int usb_cpia_get_camera_status(struct usb_device *dev, void *buf)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_CPIA_GET_CAMERA_STATUS,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, 0, buf, 8, HZ);
}
#endif

static int usb_cpia_goto_hi_power(struct usb_device *dev)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_GOTO_HI_POWER, USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, 0, NULL, 0, HZ);
}

static int usb_cpia_get_vp_version(struct usb_device *dev, void *buf)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_CPIA_GET_VP_VERSION,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0, 0, buf, 4, HZ);
}

static int usb_cpia_set_sensor_fps(struct usb_device *dev, int sensorbaserate, int sensorclkdivisor)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_SET_SENSOR_FPS,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		(sensorbaserate << 8) + sensorclkdivisor, 0, NULL, 0, HZ);
}

#ifdef NOTUSED
static int usb_cpia_grab_frame(struct usb_device *dev, int streamstartline)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_GRAB_FRAME, USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		streamstartline << 8, 0, NULL, 0, HZ);
}
#endif

static int usb_cpia_upload_frame(struct usb_device *dev, int forceupload)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_UPLOAD_FRAME, USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		forceupload, 0, NULL, 0, HZ);
}

static int usb_cpia_set_grab_mode(struct usb_device *dev, int continuousgrab)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_SET_GRAB_MODE,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE, continuousgrab,
		0, NULL, 0, HZ);
}

static int usb_cpia_set_format(struct usb_device *dev, int size, int subsample, int order)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_SET_FORMAT,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		(subsample << 8) + size, order, NULL, 0, HZ);
}

static int usb_cpia_set_roi(struct usb_device *dev, int colstart, int colend, int rowstart, int rowend)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_SET_ROI,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		(colend << 8) + colstart, (rowend << 8) + rowstart,
		NULL, 0, HZ);
}

static int usb_cpia_set_compression(struct usb_device *dev, int compmode, int decimation)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_SET_COMPRESSION,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		(decimation << 8) + compmode, 0, NULL, 0, HZ);
}

#ifdef NOTUSED
static int usb_cpia_set_compression_target(struct usb_device *dev, int target, int targetfr, int targetq)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_SET_COMPRESSION_TARGET,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		(targetfr << 8) + target, targetq, NULL, 0, HZ);
}
#endif

#ifdef NOTUSED
static int usb_cpia_initstreamcap(struct usb_device *dev, int skipframes, int streamstartline)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_INIT_STREAM_CAP,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		(streamstartline << 8) + skipframes, 0, NULL, 0, HZ);
}

static int usb_cpia_finistreamcap(struct usb_device *dev)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_FINI_STREAM_CAP,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE, 0, 0, NULL, 0, HZ);
}

static int usb_cpia_startstreamcap(struct usb_device *dev)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_START_STREAM_CAP,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE, 0, 0, NULL, 0, HZ);
}

static int usb_cpia_endstreamcap(struct usb_device *dev)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_END_STREAM_CAP,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE, 0, 0, NULL, 0, HZ);
}
#endif

/* How much data is left in the scratch buf? */
#define scratch_left(x)	(cpia->scratchlen - (int)((char *)x - (char *)cpia->scratch))

static void cpia_parse_data(struct usb_cpia *cpia)
{
	struct cpia_frame *frame, *pframe;
	unsigned char *data = cpia->scratch;
	unsigned long left;
	long copylen = 0;

	/* Grab the current frame and the previous frame */
	frame = &cpia->frame[cpia->curframe];
	pframe = &cpia->frame[(cpia->curframe - 1 + CPIA_NUMFRAMES) % CPIA_NUMFRAMES];

	while (1) {
		if (!scratch_left(data))
			goto out;

		switch (frame->scanstate) {
		case STATE_SCANNING:
		{
			struct cpia_frame_header *header;

			/* We need at least 2 bytes for the magic value */
			if (scratch_left(data) < 2)
				goto out;

			header = (struct cpia_frame_header *)data;

			if (be16_to_cpup(&header->magic) == CPIA_MAGIC) {
				frame->scanstate = STATE_HEADER;
				break;
			}

			/* Woops, lost the header, find the end of the frame */
			if (scratch_left(data) < 4)
				goto out;

			/* See if we found the end of the frame */
			while (scratch_left(data) >= 4) {
				if (*((__u32 *)data) == 0xFFFFFFFF) {
					data += 4;
					if (debug >= 1)
						printk(KERN_INFO "cpia: EOF while scanning for magic\n");
					goto error;
				}
				data++;
			}
			break;
		}
		case STATE_HEADER:
			/* We need at least 64 bytes for the header */
			if (scratch_left(data) <
			    sizeof(struct cpia_frame_header))
				goto out;

			memcpy(&frame->header, data,
				sizeof(struct cpia_frame_header));

			/* Skip over the header */
			data += sizeof(struct cpia_frame_header);

			frame->hdrwidth = (frame->header.col_end -
				frame->header.col_start) * 8;
			frame->hdrheight = (frame->header.row_end -
				frame->header.row_start) * 4;
			if (debug >= 2) {
				printk(KERN_DEBUG "cpia: frame size %dx%d\n",
					frame->hdrwidth, frame->hdrheight);
				printk(KERN_DEBUG "cpia: frame %scompressed\n",
					frame->header.comp_enable ? "" : "not ");
			}

			frame->scanstate = STATE_LINES;
			frame->curline = 0;
			break;

		case STATE_LINES:
		{
			unsigned char *f, *end;
			unsigned int len;
			int i;
			int y, u, y1, v, r, g, b;

			/* We want at least 2 bytes for the length */
			if (scratch_left(data) < 2)
				goto out;

			/* Grab the length */
			len = data[0] + (data[1] << 8);

			/* Check to make sure it's nothing outrageous */
			if (len > (frame->hdrwidth * 2) + 1) {
				if (debug >= 1)
					printk(KERN_DEBUG "cpia: bad length, resynching (expected %d, got %d)\n", (frame->hdrwidth * 2) + 1, len);
				goto error;
			}

			/* Make sure there's enough data for the entire line */
			if (scratch_left(data + 2) < len)
				goto out;

			/* Skip over the length */
			data += 2;

			/* Is the end of the line there */
			if (data[len - 1] != 0xFD) {
				if (debug >= 1)
					printk(KERN_DEBUG "cpia: lost synch\n");
				goto error;
			}

			/* Start at the beginning */
			end = data + len - 1;

			f = frame->data + (frame->width * 3 * frame->curline);

			if (frame->header.comp_enable) {
				unsigned char *fp;

				/* We use the previous frame as a reference */
				fp = pframe->data +
					(frame->width * 3 * frame->curline);

				while (data < end) {
					if (*data & 1) {
						/* Compress RLE data */
						i = *data >> 1;
						memcpy(f, fp, i * 3);
						copylen += (i * 3);
						f += (i * 3);
						fp += (i * 3);
						data++;
					} else {
						/* Raw data */

#define LIMIT(x) ((((x)>0xffffff)?0xff0000:(((x)<=0xffff)?0:(x)&0xff0000))>>16)

y =  *data++ - 16;
u =  *data++ - 128;
y1 = *data++ - 16;
v =  *data++ - 128;
r = 104635 * v;
g = -25690 * u + -53294 * v;
b = 132278 * u;
y  *= 76310;
y1 *= 76310;
*f++ = LIMIT(b + y); *f++ = LIMIT(g + y); *f++ = LIMIT(r + y);
*f++ = LIMIT(b + y1); *f++ = LIMIT(g + y1); *f++ = LIMIT(r + y1);
						fp += 6;
						copylen += 6;
					}
				}
			} else {
				/* Raw data */
				while (data < end) {
y =  *data++ - 16;
u =  *data++ - 128;
y1 = *data++ - 16;
v =  *data++ - 128;
r = 104635 * v;
g = -25690 * u + -53294 * v;
b = 132278 * u;
y  *= 76310;
y1 *= 76310;
*f++ = LIMIT(b + y); *f++ = LIMIT(g + y); *f++ = LIMIT(r + y);
*f++ = LIMIT(b + y1); *f++ = LIMIT(g + y1); *f++ = LIMIT(r + y1);
copylen += 6;
				}
			}

			/* Skip the last byte */
			data++;

			if (++frame->curline >= frame->hdrheight)
				goto nextframe;

			break;
		} /* end case STATE_LINES */
		} /* end switch (scanstate) */
	} /* end while (1) */

nextframe:
	if (debug >= 1)
		printk(KERN_DEBUG "cpia: marking as success\n");

	if (scratch_left(data) >= 4 && *((__u32 *)data) == 0xFFFFFFFF)
		data += 4;

	frame->grabstate = FRAME_DONE;

	goto wakeup;

error:
	if (debug >= 1)
		printk(KERN_DEBUG "cpia: marking as error\n");

	frame->grabstate = FRAME_ERROR;

	/* Get a fresh frame since this frame may have been important */
	cpia->compress = 0;

	copylen = 0;

wakeup:
	cpia->curframe = -1;

	/* This will cause the process to request another frame. */
	if (waitqueue_active(&frame->wq))
		wake_up_interruptible(&frame->wq);

out:
	/* Grab the remaining */
	left = scratch_left(data);
	memmove(cpia->scratch, data, left);
	cpia->scratchlen = left;

	/* Update the frame's uncompressed length. */
	frame->scanlength += copylen;
}

/*
 * Make all of the blocks of data contiguous
 */
static int cpia_compress_isochronous(struct usb_cpia *cpia, urb_t *urb)
{
	unsigned char *cdata, *data;
	int i, totlen = 0;

	data = cpia->scratch + cpia->scratchlen;
	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;
		
		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		if (st && debug >= 1)
			printk(KERN_DEBUG "cpia data error: [%d] len=%d, status=%X\n",
				i, n, st);

		if ((cpia->scratchlen + n) > SCRATCH_BUF_SIZE) {
			printk(KERN_DEBUG "cpia: scratch buf overflow!scr_len: %d, n: %d\n",cpia->scratchlen, n );
			return totlen;
		}

		if (n) {
			memmove(data, cdata, n);
			data += n;
			totlen += n;
			cpia->scratchlen += n;
		}
	}

	return totlen;
}

static void cpia_isoc_irq(struct urb *urb)
{
	int len;
	struct usb_cpia *cpia = urb->context;
	struct cpia_sbuf *sbuf;
	int i;

	if (!cpia->dev)
		return;

	if (!cpia->streaming) {
		if (debug >= 1)
			printk(KERN_DEBUG "cpia: oops, not streaming, but interrupt\n");
		return;
	}
	
	sbuf = &cpia->sbuf[cpia->cursbuf];

	/* Copy the data received into our scratch buffer */
	len = cpia_compress_isochronous(cpia, urb);

	/* If we don't have a frame we're current working on, complain */
	if (cpia->scratchlen) {
		if (cpia->curframe < 0) {
			if (debug >= 1)
				printk(KERN_DEBUG "cpia: received data, but no frame available\n");
		} else
			cpia_parse_data(cpia);
	}

	for (i = 0; i < FRAMES_PER_DESC; i++) {
		sbuf->urb->iso_frame_desc[i].status = 0;
		sbuf->urb->iso_frame_desc[i].actual_length = 0;
	}

	/* Move to the next sbuf */
	cpia->cursbuf = (cpia->cursbuf + 1) % CPIA_NUMSBUF;

	return;
}

static int cpia_init_isoc(struct usb_cpia *cpia)
{
	urb_t *urb;
	int fx, err;

	cpia->compress = 0;
	cpia->curframe = -1;
	cpia->cursbuf = 0;
	cpia->scratchlen = 0;

	/* Alternate interface 3 is is the biggest frame size */
	if (usb_set_interface(cpia->dev, cpia->iface, 3) < 0) {
		printk(KERN_ERR "usb_set_interface error\n");
		return -EBUSY;
	}

	/* We double buffer the Iso lists */
	urb = usb_alloc_urb(FRAMES_PER_DESC);
	
	if (!urb) {
		printk(KERN_ERR "cpia_init_isoc: usb_init_isoc ret %d\n",
			0);
		return -ENOMEM;
	}
	cpia->sbuf[0].urb = urb;
	urb->dev = cpia->dev;
	urb->context = cpia;
	urb->pipe = usb_rcvisocpipe(cpia->dev, 1);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = cpia->sbuf[0].data;
 	urb->complete = cpia_isoc_irq;
 	urb->number_of_packets = FRAMES_PER_DESC;
 	urb->transfer_buffer_length = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
 	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
 		urb->iso_frame_desc[fx].offset = FRAME_SIZE_PER_DESC * fx;
		urb->iso_frame_desc[fx].length = FRAME_SIZE_PER_DESC;
	}
	urb = usb_alloc_urb(FRAMES_PER_DESC);
	if (!urb) {
		printk(KERN_ERR "cpia_init_isoc: usb_init_isoc ret %d\n",
			0);
		return -ENOMEM;
	}
	cpia->sbuf[1].urb = urb;
	urb->dev = cpia->dev;
	urb->context = cpia;
	urb->pipe = usb_rcvisocpipe(cpia->dev, 1);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = cpia->sbuf[1].data;
 	urb->complete = cpia_isoc_irq;
 	urb->number_of_packets = FRAMES_PER_DESC;
 	urb->transfer_buffer_length = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
 	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
 		urb->iso_frame_desc[fx].offset = FRAME_SIZE_PER_DESC * fx;
		urb->iso_frame_desc[fx].length = FRAME_SIZE_PER_DESC;
	}

	cpia->sbuf[1].urb->next = cpia->sbuf[0].urb;
	cpia->sbuf[0].urb->next = cpia->sbuf[1].urb;
	
	err = usb_submit_urb(cpia->sbuf[0].urb);
	if (err)
		printk(KERN_ERR "cpia_init_isoc: usb_submit_urb(0) ret %d\n",
			err);
	err = usb_submit_urb(cpia->sbuf[1].urb);
	if (err)
		printk(KERN_ERR "cpia_init_isoc: usb_submit_urb(1) ret %d\n",
			err);

	cpia->streaming = 1;

	return 0;
}

static void cpia_stop_isoc(struct usb_cpia *cpia)
{
	if (!cpia->streaming || !cpia->dev)
		return;

	/* Turn off continuous grab */
	if (usb_cpia_set_grab_mode(cpia->dev, 0) < 0) {
		printk(KERN_ERR "cpia_set_grab_mode error\n");
		return /* -EBUSY */;
	}

	/* Set packet size to 0 */
	if (usb_set_interface(cpia->dev, cpia->iface, 0) < 0) {
		printk(KERN_ERR "usb_set_interface error\n");
		return /* -EINVAL */;
	}

	cpia->streaming = 0;

	/* Unschedule all of the iso td's */
	if (cpia->sbuf[1].urb) {
		cpia->sbuf[1].urb->next = NULL;
		usb_unlink_urb(cpia->sbuf[1].urb);
		usb_free_urb(cpia->sbuf[1].urb);
		cpia->sbuf[1].urb = NULL;
	}
	if (cpia->sbuf[0].urb) {
		cpia->sbuf[0].urb->next = NULL;
		usb_unlink_urb(cpia->sbuf[0].urb);
		usb_free_urb(cpia->sbuf[0].urb);
		cpia->sbuf[0].urb = NULL;
	}
}

static int cpia_new_frame(struct usb_cpia *cpia, int framenum)
{
	struct cpia_frame *frame;
	int width, height;

	if (!cpia->dev)
		return -1;

	/* If we're not grabbing a frame right now and the other frame is */
	/*  ready to be grabbed into, then use it instead */
	if (cpia->curframe == -1) {
		if (cpia->frame[(framenum - 1 + CPIA_NUMFRAMES) % CPIA_NUMFRAMES].grabstate == FRAME_READY)
			framenum = (framenum - 1 + CPIA_NUMFRAMES) % CPIA_NUMFRAMES;
	} else
		return 0;

	frame = &cpia->frame[framenum];
	width = frame->width;
	height = frame->height;

	frame->grabstate = FRAME_GRABBING;
	frame->scanstate = STATE_SCANNING;
	frame->scanlength = 0;		/* accumulated in cpia_parse_data() */

	cpia->curframe = framenum;

	/* Make sure it's not too big */
	if (width > 352)
		width = 352;
	width = (width / 8) * 8;	/* Multiple of 8 */

	if (height > 288)
		height = 288;
	height = (height / 4) * 4;	/* Multiple of 4 */

	/* Set the ROI they want */
	if (usb_cpia_set_roi(cpia->dev, 0, width / 8, 0, height / 4) < 0)
		return -EBUSY;

	if (usb_cpia_set_compression(cpia->dev, cpia->compress ?
			COMP_AUTO : COMP_DISABLED, DONT_DECIMATE) < 0) {
		printk(KERN_ERR "cpia_set_compression error\n");
		return -EBUSY;
	}

	/* We want a fresh frame every 30 we get */
	cpia->compress = (cpia->compress + 1) % 30;

	/* Grab the frame */
	if (usb_cpia_upload_frame(cpia->dev, WAIT_FOR_NEXT_FRAME) < 0) {
		printk(KERN_ERR "cpia_upload_frame error\n");
		return -EBUSY;
	}

	return 0;
}

/* Video 4 Linux API */
static int cpia_open(struct video_device *dev, int flags)
{
	int err = -EBUSY;
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

	down(&cpia->lock);
	if (cpia->user)
		goto out_unlock;

	cpia->frame[0].grabstate = FRAME_UNUSED;
	cpia->frame[1].grabstate = FRAME_UNUSED;

	err = -ENOMEM;

	/* Allocate memory for the frame buffers */
	cpia->fbuf = rvmalloc(2 * MAX_FRAME_SIZE);
	if (!cpia->fbuf)
		goto open_err_ret;

	cpia->frame[0].data = cpia->fbuf;
	cpia->frame[1].data = cpia->fbuf + MAX_FRAME_SIZE;

	cpia->sbuf[0].data = kmalloc (FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!cpia->sbuf[0].data)
		goto open_err_on0;

	cpia->sbuf[1].data = kmalloc (FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!cpia->sbuf[1].data)
		goto open_err_on1;

	/* Set default sizes in case IOCTL (VIDIOCMCAPTURE) is not used
	 * (using read() instead). */
	cpia->frame[0].width = 352;
	cpia->frame[0].height = 288;
	cpia->frame[0].bytes_read = 0;
	cpia->frame[1].width = 352;
	cpia->frame[1].height = 288;
	cpia->frame[1].bytes_read = 0;

	err = cpia_init_isoc(cpia);
	if (err)
		goto open_err_on2;

	cpia->user++;
	up(&cpia->lock);

	MOD_INC_USE_COUNT;

	return 0;

open_err_on2:
	kfree (cpia->sbuf[1].data);
open_err_on1:
	kfree (cpia->sbuf[0].data);
open_err_on0:
	rvfree(cpia->fbuf, 2 * MAX_FRAME_SIZE);
open_err_ret:
	return err;

out_unlock:
	up(&cpia->lock);
	return err;
}

static void cpia_close(struct video_device *dev)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

	down(&cpia->lock);	
	cpia->user--;

	MOD_DEC_USE_COUNT;

	cpia_stop_isoc(cpia);

	rvfree(cpia->fbuf, 2 * MAX_FRAME_SIZE);

	kfree(cpia->sbuf[1].data);
	kfree(cpia->sbuf[0].data);

	up(&cpia->lock);

	if (!cpia->dev) {
		video_unregister_device(&cpia->vdev);
		kfree(cpia);
	}
}

static int cpia_init_done(struct video_device *dev)
{
	return 0;
}

static long cpia_write(struct video_device *dev, const char *buf, unsigned long count, int noblock)
{
	return -EINVAL;
}

static int cpia_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

	if (!cpia->dev)
		return -EIO;

	switch (cmd) {
		case VIDIOCGCAP:
		{
			struct video_capability b;

			strcpy(b.name, "CPiA USB Camera");
			b.type = VID_TYPE_CAPTURE | VID_TYPE_SUBCAPTURE;
			b.channels = 1;
			b.audios = 0;
			b.maxwidth = 352;	/* CIF */
			b.maxheight = 288;	/*  "  */
			b.minwidth = 8;
			b.minheight = 4;

			if (copy_to_user(arg, &b, sizeof(b)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if (v.channel != 0)
				return -EINVAL;

			v.flags = 0;
			v.tuners = 0;
			v.type = VIDEO_TYPE_CAMERA;
			strcpy(v.name, "Camera");

			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCSCHAN:
		{
			int v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			if (v != 0)
				return -EINVAL;

			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture p;

			p.colour = 0x8000;	/* Damn British people :) */
			p.hue = 0x8000;
			p.brightness = 180 << 8;	/* XXX */
			p.contrast = 192 << 8;		/* XXX */
			p.whiteness = 105 << 8;		/* XXX */
			p.depth = 24;
			p.palette = VIDEO_PALETTE_RGB24;

			if (copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture p;

			if (copy_from_user(&p, arg, sizeof(p)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window vw;

			if (copy_from_user(&vw, arg, sizeof(vw)))
				return -EFAULT;
			if (vw.flags)
				return -EINVAL;
			if (vw.clipcount)
				return -EINVAL;
			if (vw.height != 288)
				return -EINVAL;
			if (vw.width != 352)
				return -EINVAL;

			cpia->compress = 0;

			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;

			vw.x = 0;
			vw.y = 0;
			vw.width = 352;
			vw.height = 288;
			vw.chromakey = 0;
			vw.flags = 30;		/* 30 fps */

			if (copy_to_user(arg, &vw, sizeof(vw)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCGMBUF:
		{
			struct video_mbuf vm;

			memset(&vm, 0, sizeof(vm));
			vm.size = MAX_FRAME_SIZE * 2;
			vm.frames = 2;
			vm.offsets[0] = 0;
			vm.offsets[1] = MAX_FRAME_SIZE;

			if (copy_to_user((void *)arg, (void *)&vm, sizeof(vm)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCMCAPTURE:
		{
			struct video_mmap vm;

			if (copy_from_user((void *)&vm, (void *)arg, sizeof(vm)))
				return -EFAULT;

			if (debug >= 1)
				printk(KERN_DEBUG "frame: %d, size: %dx%d, format: %d\n",
					vm.frame, vm.width, vm.height, vm.format);

			if (vm.format != VIDEO_PALETTE_RGB24)
				return -EINVAL;

			if ((vm.frame != 0) && (vm.frame != 1))
				return -EINVAL;

			if (cpia->frame[vm.frame].grabstate == FRAME_GRABBING)
				return -EBUSY;

			/* Don't compress if the size changed */
			if ((cpia->frame[vm.frame].width != vm.width) ||
			    (cpia->frame[vm.frame].height != vm.height))
				cpia->compress = 0;

			cpia->frame[vm.frame].width = vm.width;
			cpia->frame[vm.frame].height = vm.height;

			/* Mark it as ready */
			cpia->frame[vm.frame].grabstate = FRAME_READY;

			return cpia_new_frame(cpia, vm.frame);
		}
		case VIDIOCSYNC:
		{
			int frame;

			if (copy_from_user((void *)&frame, arg, sizeof(int)))
				return -EFAULT;

			if (debug >= 1)
				printk(KERN_DEBUG "cpia: syncing to frame %d\n", frame);

			switch (cpia->frame[frame].grabstate) {
			case FRAME_UNUSED:
				return -EINVAL;
			case FRAME_READY:
			case FRAME_GRABBING:
			case FRAME_ERROR:
redo:
				if (!cpia->dev)
					return -EIO;

				do {
					interruptible_sleep_on(&cpia->frame[frame].wq);
					if (signal_pending(current))
						return -EINTR;
				} while (cpia->frame[frame].grabstate == FRAME_GRABBING);

				if (cpia->frame[frame].grabstate == FRAME_ERROR) {
					int ret;

					if ((ret = cpia_new_frame(cpia, frame)) < 0)
						return ret;
					goto redo;
				}
			case FRAME_DONE:
				cpia->frame[frame].grabstate = FRAME_UNUSED;
				break;
			}

			cpia->frame[frame].grabstate = FRAME_UNUSED;

			return 0;
		}
		case VIDIOCGFBUF:
		{
			struct video_buffer vb;

			memset(&vb, 0, sizeof(vb));
			vb.base = NULL;	/* frame buffer not supported, not used */

			if (copy_to_user((void *)arg, (void *)&vb, sizeof(vb)))
				return -EFAULT;

 			return 0;
 		}
		case VIDIOCKEY:
			return 0;
		case VIDIOCCAPTURE:
			return -EINVAL;
		case VIDIOCSFBUF:
			return -EINVAL;
		case VIDIOCGTUNER:
		case VIDIOCSTUNER:
			return -EINVAL;
		case VIDIOCGFREQ:
		case VIDIOCSFREQ:
			return -EINVAL;
		case VIDIOCGAUDIO:
		case VIDIOCSAUDIO:
			return -EINVAL;
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static long cpia_read(struct video_device *dev, char *buf, unsigned long count, int noblock)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;
	int frmx = -1;
	volatile struct cpia_frame *frame;

	if (debug >= 1)
		printk(KERN_DEBUG "cpia_read: %ld bytes, noblock=%d\n", count, noblock);

	if (!dev || !buf)
		return -EFAULT;

	if (!cpia->dev)
		return -EIO;

	/* See if a frame is completed, then use it. */
	if (cpia->frame[0].grabstate >= FRAME_DONE)	/* _DONE or _ERROR */
		frmx = 0;
	else if (cpia->frame[1].grabstate >= FRAME_DONE)/* _DONE or _ERROR */
		frmx = 1;

	if (noblock && (frmx == -1))
		return -EAGAIN;

	/* If no FRAME_DONE, look for a FRAME_GRABBING state. */
	/* See if a frame is in process (grabbing), then use it. */
	if (frmx == -1) {
		if (cpia->frame[0].grabstate == FRAME_GRABBING)
			frmx = 0;
		else if (cpia->frame[1].grabstate == FRAME_GRABBING)
			frmx = 1;
	}

	/* If no frame is active, start one. */
	if (frmx == -1)
		cpia_new_frame(cpia, frmx = 0);

	frame = &cpia->frame[frmx];

restart:
	if (!cpia->dev)
		return -EIO;

	while (frame->grabstate == FRAME_GRABBING) {
		interruptible_sleep_on(&frame->wq);
		if (signal_pending(current))
			return -EINTR;
	}

	if (frame->grabstate == FRAME_ERROR) {
		frame->bytes_read = 0;
printk("cpia_read: errored frame %d\n", cpia->curframe);
		if (cpia_new_frame(cpia, frmx))
			printk(KERN_ERR "cpia_read: cpia_new_frame error\n");
		goto restart;
	}

	if (debug >= 1)
		printk(KERN_DEBUG "cpia_read: frmx=%d, bytes_read=%ld, scanlength=%ld\n",
			frmx, frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count))
		return -EFAULT;

	frame->bytes_read += count;
	if (debug >= 1)
		printk(KERN_DEBUG "cpia_read: {copy} count used=%ld, new bytes_read=%ld\n",
			count, frame->bytes_read);

	if (frame->bytes_read >= frame->scanlength) { /* All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		cpia->frame[frmx].grabstate = FRAME_UNUSED;
		if (cpia_new_frame(cpia, frmx ? 0 : 1))
			printk(KERN_ERR "cpia_read: cpia_new_frame returned error\n");
	}

	return count;
}

static int cpia_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

	if (!cpia->dev)
		return -EIO;

	if (size > (((2 * MAX_FRAME_SIZE) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)))
		return -EINVAL;

	pos = (unsigned long)cpia->fbuf;
	while (size > 0) {
		page = kvirt_to_pa(pos);
		if (remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	return 0;
}

static struct video_device cpia_template = {
	"CPiA USB Camera",
	VID_TYPE_CAPTURE,
	VID_HARDWARE_CPIA,
	cpia_open,
	cpia_close,
	cpia_read,
	cpia_write,
	NULL,
	cpia_ioctl,
	cpia_mmap,
	cpia_init_done,
	NULL,
	0,
	0
};

static int usb_cpia_configure(struct usb_cpia *cpia)
{
	struct usb_device *dev = cpia->dev;
	unsigned char version[4];

	/* Set altsetting 0 */
	if (usb_set_interface(dev, cpia->iface, 0) < 0) {
		printk(KERN_ERR "usb_set_interface error\n");
		return -EBUSY;
	}

	if (usb_cpia_get_version(dev, version) < 0) {
		printk(KERN_ERR "cpia_get_version error\n");
		return -EBUSY;
	}

	if (debug >= 1)
		printk(KERN_DEBUG "cpia: Firmware v%d.%d, VC Hardware v%d.%d\n",
			version[0], version[1], version[2], version[3]);

	memcpy(&cpia->vdev, &cpia_template, sizeof(cpia_template));

	init_waitqueue_head(&cpia->frame[0].wq);
	init_waitqueue_head(&cpia->frame[1].wq);

	if (video_register_device(&cpia->vdev, VFL_TYPE_GRABBER) == -1) {
		printk(KERN_ERR "video_register_device failed\n");
		return -EBUSY;
	}

	if (usb_cpia_goto_hi_power(dev) < 0) {
		printk(KERN_ERR "cpia_goto_hi_power error\n");
		goto error;
	}

	if (usb_cpia_get_vp_version(dev, version) < 0) {
		printk(KERN_ERR "cpia_get_vp_version error\n");
		goto error;
	}

	if (debug >= 1) {
		printk(KERN_DEBUG "cpia: VP v%d rev %d\n", version[0], version[1]);
		printk(KERN_DEBUG "cpia: Camera Head ID %04X\n", (version[3] << 8) + version[2]);
	}

	/* Turn on continuous grab */
	if (usb_cpia_set_grab_mode(dev, 1) < 0) {
		printk(KERN_ERR "cpia_set_grab_mode error\n");
		goto error;
	}

	/* Set up the sensor to be 30fps */
	if (usb_cpia_set_sensor_fps(dev, 1, 0) < 0) {
		printk(KERN_ERR "cpia_set_sensor_fps error\n");
		goto error;
	}

	/* Set video into CIF mode, and order into YUYV mode */
	if (usb_cpia_set_format(dev, FORMAT_CIF, FORMAT_422,
			FORMAT_YUYV) < 0) {
		printk(KERN_ERR "cpia_set_format error\n");
		goto error;
	}

	/* Turn off compression */
	if (usb_cpia_set_compression(dev, COMP_DISABLED, DONT_DECIMATE) < 0) {
		printk(KERN_ERR "cpia_set_compression error\n");
		goto error;
	}

	cpia->compress = 0;

	return 0;

error:
	video_unregister_device(&cpia->vdev);
	usb_driver_release_interface(&cpia_driver,
		&dev->actconfig->interface[0]);

	kfree(cpia);

	return -EBUSY;
}

static void * cpia_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct usb_cpia *cpia;

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return NULL;

	interface = &dev->actconfig->interface[ifnum].altsetting[0];

	/* Is it a CPiA? */
	if (dev->descriptor.idVendor != 0x0553)
		return NULL;
	if (dev->descriptor.idProduct != 0x0002)
		return NULL;

	/* Checking vendor/product should be enough, but what the hell */
	if (interface->bInterfaceClass != 0xFF)
		return NULL;
	if (interface->bInterfaceSubClass != 0x00)
		return NULL;

	/* We found a CPiA */
	printk(KERN_INFO "USB CPiA camera found\n");

	if ((cpia = kmalloc(sizeof(*cpia), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "couldn't kmalloc cpia struct\n");
		return NULL;
	}

	memset(cpia, 0, sizeof(*cpia));

	cpia->dev = dev;
	cpia->iface = interface->bInterfaceNumber;

	if (!usb_cpia_configure(cpia)) {
		cpia->user=0; 
		init_MUTEX(&cpia->lock);	/* to 1 == available */

		return cpia;
	} else
		return NULL;
}

static void cpia_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_cpia *cpia = (struct usb_cpia *) ptr;

	/* We don't want people trying to open up the device */
	if (!cpia->user)
		video_unregister_device(&cpia->vdev);

	usb_driver_release_interface(&cpia_driver,
		&cpia->dev->actconfig->interface[0]);

	cpia->dev = NULL;
	cpia->frame[0].grabstate = FRAME_ERROR;
	cpia->frame[1].grabstate = FRAME_ERROR;
	cpia->curframe = -1;

	/* This will cause the process to request another frame. */
	if (waitqueue_active(&cpia->frame[0].wq))
		wake_up_interruptible(&cpia->frame[0].wq);

	if (waitqueue_active(&cpia->frame[1].wq))
		wake_up_interruptible(&cpia->frame[1].wq);

	cpia->streaming = 0;

	/* Unschedule all of the iso td's */
	if (cpia->sbuf[1].urb) {
		cpia->sbuf[1].urb->next = NULL;
		usb_unlink_urb(cpia->sbuf[1].urb);
		usb_free_urb(cpia->sbuf[1].urb);
		cpia->sbuf[1].urb = NULL;
	}
	if (cpia->sbuf[0].urb) {
		cpia->sbuf[0].urb->next = NULL;
		usb_unlink_urb(cpia->sbuf[0].urb);
		usb_free_urb(cpia->sbuf[0].urb);
		cpia->sbuf[0].urb = NULL;
	}

	/* Free the memory */
	if (!cpia->user)
		kfree(cpia);
}

static struct usb_driver cpia_driver = {
	"cpia",
	cpia_probe,
	cpia_disconnect,
	{ NULL, NULL }
};

int usb_cpia_init(void)
{
	return usb_register(&cpia_driver);
}

void usb_cpia_cleanup(void)
{
	usb_deregister(&cpia_driver);
}

#ifdef MODULE
int init_module(void)
{
	return usb_cpia_init();
}

void cleanup_module(void)
{
	usb_cpia_cleanup();
}
#endif
