/*
 * USB CPiA Video Camera driver
 *
 * Supports CPiA based Video Cameras. Many manufacturers use this chipset.
 * There's a good chance, if you have a USB video camera, it's a CPiA based
 * one.
 *
 * (C) Copyright 1999 Johannes Erdfelt
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

#include <asm/io.h>

#include "usb.h"
#include "cpia.h"

#define CPIA_DEBUG	/* Gobs of debugging info */

/* Video Size 384 x 288 x 3 bytes for RGB */
#define MAX_FRAME_SIZE (384 * 288 * 3)

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

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

static int usb_cpia_grab_frame(struct usb_device *dev, int streamstartline)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_GRAB_FRAME, USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		streamstartline << 8, 0, NULL, 0, HZ);
}

static int usb_cpia_upload_frame(struct usb_device *dev, int forceupload)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CPIA_UPLOAD_FRAME,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE, forceupload, 0, NULL, 0, HZ);
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
	unsigned long l;

	frame = &cpia->frame[cpia->curframe];
	pframe = &cpia->frame[(cpia->curframe - 1 + CPIA_NUMFRAMES) % CPIA_NUMFRAMES];

	while (1) {
		if (!scratch_left(data))
			goto out;

		switch (frame->scanstate) {
		case STATE_SCANNING:
		{
			struct cpia_frame_header *header;

			/* We need atleast 2 bytes for the magic value */
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
printk("found end of frame\n");
					data += 4;
goto error;
				}
				data++;
			}
			break;
		}
		case STATE_HEADER:
			/* We need atleast 64 bytes for the header */
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
#ifdef CPIA_DEBUG
			printk("cpia: frame size %dx%d\n",
				frame->hdrwidth, frame->hdrheight);
			printk("cpia: frame %scompressed\n",
				frame->header.comp_enable ? "" : "not ");
#endif

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

printk("line %d, %d bytes long\n", frame->curline, len);
			/* Check to make sure it's nothing outrageous */
			if (len > (frame->hdrwidth * 2) + 1) {
				printk(KERN_INFO "cpia: bad length, resynching\n");
				goto error;
			}

			/* Make sure there's enough data for the entire line */
			if (scratch_left(data + 2) < len)
				goto out;

			/* Skip over the length */
			data += 2;

			/* Is the end of the line there */
			if (data[len - 1] != 0xFD) {
				printk(KERN_INFO "cpia: lost synch\n");
end = data + len - 1 - 4;
printk("%02X %02X %02X %02X %02X %02X %02X %02X\n",
end[0], end[1],
end[2], end[3],
end[4], end[5],
end[6], end[7]);
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
/*
						f[0] = f[1] = f[2] = *data;
						f += 3;
						data += 2;
						fp += 3;
*/
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
				}
			}

#ifdef CPIA_DEBUG
			/* Make sure we found the end correctly */
			if (*data != 0xFD)
				printk("cpia: missed end!\n");
#endif

			/* Skip the last byte */
			data++;

			if (++frame->curline >= frame->hdrheight)
				goto nextframe;

			break;
		}
		}
	}

nextframe:
	if (scratch_left(data) >= 4 && *((__u32 *)data) == 0xFFFFFFFF) {
		data += 4;
printk("end of frame found normally\n");
}

	frame->grabstate = FRAME_DONE;
	cpia->curframe = -1;

	/* This will cause the process to request another frame */
	if (waitqueue_active(&frame->wq))
		wake_up_interruptible(&frame->wq);

	goto out;

error:
	frame->grabstate = FRAME_ERROR;
	cpia->curframe = -1;
	cpia->compress = 0;

	/* This will cause the process to request another frame */
	if (waitqueue_active(&frame->wq))
		wake_up_interruptible(&frame->wq);

out:
printk("scanned %d bytes, %d left\n", data - cpia->scratch, scratch_left(data));
	/* Grab the remaining */
	l = scratch_left(data);
	memmove(cpia->scratch, data, l);
	cpia->scratchlen = l;
}

/*
 * Make all of the blocks of data contiguous
 */
static int cpia_compress_isochronous(struct usb_cpia *cpia, struct usb_isoc_desc *isodesc)
{
	unsigned char *cdata, *data;
	int i, totlen = 0;

	cdata = isodesc->data;
	data = cpia->scratch + cpia->scratchlen;
	for (i = 0; i < isodesc->frame_count; i++) {
		int n = isodesc->frames[i].frame_length;
#ifdef CPIA_DEBUG
		int st = isodesc->frames[i].frame_status;

		if (st)
			printk(KERN_DEBUG "cpia data error: [%d] len=%d, status=%X\n",
				i, n, st);
#endif
		if ((cpia->scratchlen + n) > SCRATCH_BUF_SIZE) {
			printk(KERN_ERR "cpia: scratch buf overflow!\n");
			return 0;
		}

		if (n)
			memmove(data, cdata, n);

		data += n;
		totlen += n;
		cpia->scratchlen += n;
		cdata += isodesc->frame_size;
	}

	return totlen;
}

static int cpia_isoc_irq(int status, void *__buffer, int len, void *isocdesc)
{
	void *dev_id = ((struct usb_isoc_desc *)isocdesc)->context;
	struct usb_cpia *cpia = (struct usb_cpia *)dev_id;
	struct cpia_sbuf *sbuf;
	int i;

	if (!cpia->streaming) {
		printk("oops, not streaming, but interrupt\n");
		return 0;
	}
	
	sbuf = &cpia->sbuf[cpia->cursbuf];
	usb_kill_isoc(sbuf->isodesc);

	/* Copy the data received into our scratch buffer */
	len = cpia_compress_isochronous(cpia, sbuf->isodesc);

printk("%d bytes received\n", len);
	/* If we don't have a frame we're current working on, complain */
	if (len) {
		if (cpia->curframe < 0)
			printk("cpia: received data, but no frame available\n");
		else
			cpia_parse_data(cpia);
	}

	for (i = 0; i < FRAMES_PER_DESC; i++)
		sbuf->isodesc->frames[i].frame_length = FRAME_SIZE_PER_DESC;

	/* Move to the next sbuf */
	cpia->cursbuf = (cpia->cursbuf + 1) % CPIA_NUMSBUF;

	/* Reschedule this block of Isochronous desc */
	usb_run_isoc(sbuf->isodesc, cpia->sbuf[cpia->cursbuf].isodesc);

	return -1;
}

static int cpia_init_isoc(struct usb_cpia *cpia)
{
	struct usb_device *dev = cpia->dev;
	struct usb_isoc_desc *id;
	int fx, err;

	cpia->curframe = -1;
	cpia->cursbuf = 0;
	cpia->scratchlen = 0;

	/* Alternate interface 3 is is the biggest frame size */
	if (usb_set_interface(cpia->dev, 1, 3) < 0) {
		printk("usb_set_interface error\n");
		return -EBUSY;
	}

	/* We double buffer the Iso lists */
	err = usb_init_isoc(dev, usb_rcvisocpipe(dev, 1), FRAMES_PER_DESC,
		cpia, &cpia->sbuf[0].isodesc);
	if (err) {
		printk(KERN_ERR "cpia_init_isoc: usb_init_isoc() ret %d\n",
			err);
		return -ENOMEM;
	}

	err = usb_init_isoc(dev, usb_rcvisocpipe(dev, 1), FRAMES_PER_DESC,
		cpia, &cpia->sbuf[1].isodesc);
	if (err) {
		printk(KERN_ERR "cpia_init_isoc: usb_init_isoc() ret %d\n",
			err);
		usb_free_isoc (cpia->sbuf[0].isodesc);
		return -ENOMEM;
	}

#ifdef CPIA_DEBUG
	printk("isodesc[0] @ %p\n", cpia->sbuf[0].isodesc);
	printk("isodesc[1] @ %p\n", cpia->sbuf[1].isodesc);
#endif

	/* Set the Isoc. desc. parameters. */
	/* First for desc. [0] */
	id = cpia->sbuf[0].isodesc;
	id->start_type = START_ASAP;
	id->callback_frames = 10;	/* on every 10th frame */
	id->callback_fn = cpia_isoc_irq;
	id->data = cpia->sbuf[0].data;
	id->buf_size = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
	for (fx = 0; fx < FRAMES_PER_DESC; fx++)
		id->frames[fx].frame_length = FRAME_SIZE_PER_DESC;

	/* and for desc. [1] */
	id = cpia->sbuf[1].isodesc;
	id->start_type = 0;             /* will follow the first desc. */
	id->callback_frames = 10;	/* on every 10th frame */
	id->callback_fn = cpia_isoc_irq;
	id->data = cpia->sbuf[1].data;
	id->buf_size = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
	for (fx = 0; fx < FRAMES_PER_DESC; fx++)
		id->frames[fx].frame_length = FRAME_SIZE_PER_DESC;

	err = usb_run_isoc(cpia->sbuf[0].isodesc, NULL);
	if (err)
		printk(KERN_ERR "CPiA USB driver error (%d) on usb_run_isoc\n", err);
	err = usb_run_isoc(cpia->sbuf[1].isodesc, cpia->sbuf[0].isodesc);
	if (err)
		printk(KERN_ERR "CPiA USB driver error (%d) on usb_run_isoc\n", err);

#ifdef CPIA_DEBUG
	printk("done scheduling\n");
#endif

#if 0
	if (usb_cpia_grab_frame(dev, 120) < 0) {
		printk(KERN_INFO "cpia_grab_frame error\n");
		return -EBUSY;
	}
#endif

	cpia->streaming = 1;
#ifdef CPIA_DEBUG
	printk("now streaming\n");
#endif

	return 0;
}

static void cpia_stop_isoc(struct usb_cpia *cpia)
{
	if (!cpia->streaming)
		return;

	cpia->streaming = 0;

	/* Turn off continuous grab */
	if (usb_cpia_set_grab_mode(cpia->dev, 0) < 0) {
		printk(KERN_INFO "cpia_set_grab_mode error\n");
		return /* -EBUSY */;
	}

#if 0
	if (usb_cpia_grab_frame(cpia->dev, 0) < 0) {
		printk(KERN_INFO "cpia_grab_frame error\n");
		return /* -EBUSY */;
	}
#endif

	/* Set packet size to 0 */
	if (usb_set_interface(cpia->dev, 1, 0) < 0) {
		printk(KERN_INFO "usb_set_interface error\n");
		return /* -EINVAL */;
	}

	/* Unschedule all of the iso td's */
	usb_kill_isoc(cpia->sbuf[1].isodesc);
	usb_kill_isoc(cpia->sbuf[0].isodesc);

	/* Delete them all */
	usb_free_isoc(cpia->sbuf[1].isodesc);
	usb_free_isoc(cpia->sbuf[0].isodesc);
}

static int cpia_new_frame(struct usb_cpia *cpia, int framenum)
{
	struct cpia_frame *frame;
	int width, height;

printk("new frame %d\n", framenum);
	if (framenum == -1) {
		int i;
		for (i = 0; i < CPIA_NUMFRAMES; i++)
			if (cpia->frame[i].grabstate == FRAME_READY)
				break;

		if (i >= CPIA_NUMFRAMES) {
			printk("no frame ready\n");
			return 0;
		}

		framenum = i;
printk("using frame %d\n", framenum);
	}

	if (cpia->curframe != -1 && cpia->curframe != framenum)
		return 0;

	frame = &cpia->frame[framenum];
	width = frame->width;
	height = frame->height;

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

	if (usb_cpia_set_compression(cpia->dev, cpia->compress ? 1 : 0, 0) < 0) {
		printk(KERN_INFO "cpia_set_compression error\n");
		return -EBUSY;
	}

	/* We want a fresh frame every 30 we get */
	cpia->compress = (cpia->compress + 1) % 30;

	/* Grab the frame */
	if (usb_cpia_upload_frame(cpia->dev, 1) < 0) {
		printk(KERN_INFO "cpia_upload_frame error\n");
		return -EBUSY;
	}

	frame->grabstate = FRAME_GRABBING;
	frame->scanstate = STATE_SCANNING;

	cpia->curframe = framenum;

	return 0;
}

/* Video 4 Linux API */
static int cpia_open(struct video_device *dev, int flags)
{
	int err = -ENOMEM;
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

#ifdef CPIA_DEBUG
	printk("cpia_open\n");
#endif

	cpia->frame[0].grabstate = FRAME_UNUSED;
	cpia->frame[1].grabstate = FRAME_UNUSED;

	/* Allocate memory for the frame buffers */
	cpia->fbuf = rvmalloc(2 * MAX_FRAME_SIZE);
	if (!cpia->fbuf)
		goto open_err_ret;

	cpia->frame[0].data = cpia->fbuf;
	cpia->frame[1].data = cpia->fbuf + MAX_FRAME_SIZE;
#ifdef CPIA_DEBUG
	printk("frame [0] @ %p\n", cpia->frame[0].data);
	printk("frame [1] @ %p\n", cpia->frame[1].data);
#endif

	cpia->sbuf[0].data = kmalloc (FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!cpia->sbuf[0].data)
		goto open_err_on0;

	cpia->sbuf[1].data = kmalloc (FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!cpia->sbuf[1].data)
		goto open_err_on1;

#ifdef CPIA_DEBUG
	printk("sbuf[0] @ %p\n", cpia->sbuf[0].data);
	printk("sbuf[1] @ %p\n", cpia->sbuf[1].data);
#endif

	err = cpia_init_isoc(cpia);
	if (err)
		goto open_err_on2;

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
}

static void cpia_close(struct video_device *dev)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

#ifdef CPIA_DEBUG
	printk("cpia_close\n");
#endif

	MOD_DEC_USE_COUNT;

	cpia_stop_isoc(cpia);

	rvfree(cpia->fbuf, 2 * MAX_FRAME_SIZE);

	kfree(cpia->sbuf[1].data);
	kfree(cpia->sbuf[0].data);
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

	switch (cmd) {
		case VIDIOCGCAP:
		{
			struct video_capability b;

#ifdef CPIA_DEBUG
			printk("GCAP\n");
#endif

			strcpy(b.name, "CPiA USB Camera");
			b.type = VID_TYPE_CAPTURE | VID_TYPE_SUBCAPTURE;
			b.channels = 1;
			b.audios = 0;
			b.maxwidth = 352;	/* CIF */
			b.maxheight = 288;	/*  "  */
			b.minwidth = 176;	/* QCIF */
			b.minheight = 144;	/*  "   */

			if (copy_to_user(arg, &b, sizeof(b)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel v;

#ifdef CPIA_DEBUG
			printk("GCHAN\n");
#endif

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

#ifdef CPIA_DEBUG
			printk("SCHAN\n");
#endif

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			if (v != 0)
				return -EINVAL;

			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture p;

#ifdef CPIA_DEBUG
			printk("GPICT\n");
#endif

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

#ifdef CPIA_DEBUG
			printk("SPICT\n");
#endif

			if (copy_from_user(&p, arg, sizeof(p)))
				return -EFAULT;

#ifdef CPIA_DEBUG
			printk("Attempting to set palette %d, depth %d\n",
				p.palette, p.depth);
			printk("SPICT: brightness=%d, hue=%d, colour=%d, contrast=%d, whiteness=%d\n",
				p.brightness, p.hue, p.colour, p.contrast, p.whiteness);
#endif

			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window vw;

#ifdef CPIA_DEBUG
			printk("SWIN\n");
#endif

			if (copy_from_user(&vw, arg, sizeof(vw)))
				return -EFAULT;
			if (vw.flags)
				return -EINVAL;
			if (vw.clipcount)
				return -EINVAL;
			if (vw.height != 176)
				return -EINVAL;
			if (vw.width != 144)
				return -EINVAL;

			cpia->compress = 0;

			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;

#ifdef CPIA_DEBUG
			printk("GWIN\n");
#endif

			vw.x = 0;
			vw.y = 0;
			vw.width = 176;
			vw.height = 144;
			vw.chromakey = 0;
			vw.flags = 0;

			if (copy_to_user(arg, &vw, sizeof(vw)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCGMBUF:
		{
			struct video_mbuf vm;
#ifdef CPIA_DEBUG
			printk("MBUF\n");
#endif

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

#ifdef CPIA_DEBUG
			printk("MCAPTURE\n");
			printk("frame: %d, size: %dx%d, format: %d\n",
				vm.frame, vm.width, vm.height, vm.format);
#endif

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

#ifdef CPIA_DEBUG
			printk("SYNC\n");
#endif

			if (copy_from_user((void *)&frame, arg, sizeof(int)))
				return -EFAULT;

#ifdef CPIA_DEBUG
			printk("cpia: syncing to frame %d\n", frame);
#endif

			switch (cpia->frame[frame].grabstate) {
			case FRAME_UNUSED:
				return -EINVAL;
			case FRAME_READY:
			case FRAME_GRABBING:
redo:
				do {
printk("enter sleeping\n");
					interruptible_sleep_on(&cpia->frame[frame].wq);
printk("back from sleeping\n");
					if (signal_pending(current))
						return -EINTR;
				} while (cpia->frame[frame].grabstate ==
				       FRAME_GRABBING);

				if (cpia->frame[frame].grabstate ==
				    FRAME_ERROR) {
					int ret;

					if ((ret = cpia_new_frame(cpia, frame)) < 0)
						return ret;
					goto redo;
				}
			case FRAME_DONE:
				cpia->frame[frame].grabstate = FRAME_UNUSED;
				break;
			}

#ifdef CPIA_DEBUG
			printk("cpia: finished, synced to frame %d\n", frame);
#endif

			return cpia_new_frame(cpia, -1);
		}
		case VIDIOCGFBUF:
		{
			struct video_buffer vb;

#ifdef CPIA_DEBUG
			printk("GFBUF\n");
#endif

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
#if 0
	struct usb_cpia *cpia = (struct usb_cpia *)dev;
	int len;
#endif

#ifdef CPIA_DEBUG
	printk("cpia_read: %ld bytes\n", count);
#endif
	return 0;
}

static int cpia_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

#ifdef CPIA_DEBUG
	printk("mmap: %ld (%lX) bytes\n", size, size);
#endif
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

	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue) < 0) {
		printk(KERN_INFO "cpia: usb_set_configuration failed\n");
		return -EBUSY;
	}

	/* Set packet size to 0 */
	if (usb_set_interface(dev, 1, 0) < 0) {
		printk(KERN_INFO "usb_set_interface error\n");
		return -EBUSY;
	}

	if (usb_cpia_get_version(dev, version) < 0) {
		printk(KERN_INFO "cpia_get_version error\n");
		return -EBUSY;
	}

	printk("cpia: Firmware v%d.%d, VC Hardware v%d.%d\n",
		version[0], version[1], version[2], version[3]);

	memcpy(&cpia->vdev, &cpia_template, sizeof(cpia_template));

	init_waitqueue_head(&cpia->frame[0].wq);
	init_waitqueue_head(&cpia->frame[1].wq);

	if (video_register_device(&cpia->vdev, VFL_TYPE_GRABBER) == -1) {
		printk(KERN_INFO "video_register_device failed\n");
		return -EBUSY;
	}

	if (usb_cpia_goto_hi_power(dev) < 0) {
		printk(KERN_INFO "cpia_goto_hi_power error\n");
		goto error;
	}

	if (usb_cpia_get_vp_version(dev, version) < 0) {
		printk(KERN_INFO "cpia_get_vp_version error\n");
		goto error;
	}

	printk("cpia: VP v%d rev %d\n", version[0], version[1]);
	printk("cpia: Camera Head ID %04X\n", (version[3] << 8) + version[2]);

	/* Turn on continuous grab */
	if (usb_cpia_set_grab_mode(dev, 1) < 0) {
		printk(KERN_INFO "cpia_set_grab_mode error\n");
		goto error;
	}

	/* Set up the sensor to be 30fps */
	if (usb_cpia_set_sensor_fps(dev, 1, 0) < 0) {
		printk(KERN_INFO "cpia_set_sensor_fps error\n");
		goto error;
	}

	/* Set video into CIF mode, and order into YUYV mode */
	if (usb_cpia_set_format(dev, CPIA_CIF, 1, CPIA_YUYV) < 0) {
		printk(KERN_INFO "cpia_set_format error\n");
		goto error;
	}

	/* Turn off compression */
	if (usb_cpia_set_compression(dev, 0, 0) < 0) {
		printk(KERN_INFO "cpia_set_compression error\n");
		goto error;
	}

	cpia->compress = 0;

	return 0;

error:
	video_unregister_device(&cpia->vdev);

	kfree(cpia);

	return -EBUSY;
}

static int cpia_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_cpia *cpia;

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return -1;

	interface = &dev->config[0].interface[0].altsetting[0];

	/* Is it a CPiA? */
	if (dev->descriptor.idVendor != 0x0553)
		return -1;
	if (dev->descriptor.idProduct != 0x0002)
		return -1;

	/* Checking vendor/product should be enough, but what the hell */
	if (interface->bInterfaceClass != 0xFF)
		return -1;
	if (interface->bInterfaceSubClass != 0x00)
		return -1;

	/* We found a CPiA */
	printk("USB CPiA camera found\n");

	if ((cpia = kmalloc(sizeof(*cpia), GFP_KERNEL)) == NULL) {
		printk("couldn't kmalloc cpia struct\n");
		return -1;
	}

	memset(cpia, 0, sizeof(*cpia));

	dev->private = cpia;
	cpia->dev = dev;

	return usb_cpia_configure(cpia);
}

static void cpia_disconnect(struct usb_device *dev)
{
	struct usb_cpia *cpia = dev->private;

	video_unregister_device(&cpia->vdev);

	/* Free the memory */
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
	usb_register(&cpia_driver);

	return 0;
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
