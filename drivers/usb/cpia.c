/*
 * USB CPiA Video Camera driver
 *
 * Supports CPiA based Video Camera's. Many manufacturers use this chipset.
 * There's a good chance, if you have a USB video camera, it's a CPiA based
 * one
 *
 * (C) Copyright 1999 Johannes Erdfelt
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

#include <asm/spinlock.h>
#include <asm/io.h>

#include "usb.h"
#include "cpia.h"

#define CPIA_DEBUG	/* Gobs of debugging info */

#define MAX_FRAME_SIZE (384 * 288 * 3)

/*******************************/
/* Memory management functions */
/*******************************/

/* convert virtual user memory address to physical address */
/* (virt_to_phys only works for kmalloced kernel memory) */

static inline unsigned long uvirt_to_phys(unsigned long adr)
{
        pgd_t *pgd;
        pmd_t *pmd;
        pte_t *ptep, pte;
  
        pgd = pgd_offset(current->mm, adr);
        if (pgd_none(*pgd))
                return 0;
        pmd = pmd_offset(pgd, adr);
        if (pmd_none(*pmd))
                return 0;
        ptep = pte_offset(pmd, adr/*&(~PGDIR_MASK)*/);
        pte = *ptep;
        if(pte_present(pte))
                return 
                  virt_to_phys((void *)(pte_page(pte)|(adr&(PAGE_SIZE-1))));
        return 0;
}

static inline unsigned long uvirt_to_bus(unsigned long adr) 
{
        return virt_to_bus(phys_to_virt(uvirt_to_phys(adr)));
}

/* convert virtual kernel memory address to physical address */
/* (virt_to_phys only works for kmalloced kernel memory) */

static inline unsigned long kvirt_to_phys(unsigned long adr) 
{
        return uvirt_to_phys(VMALLOC_VMADDR(adr));
}

static inline unsigned long kvirt_to_bus(unsigned long adr) 
{
        return uvirt_to_bus(VMALLOC_VMADDR(adr));
}


static void * rvmalloc(unsigned long size)
{
	void * mem;
	unsigned long adr, page;
        
	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	mem=vmalloc(size);
	if (mem) 
	{
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
		adr=(unsigned long) mem;
		while (size > 0) 
		{
			page = kvirt_to_phys(adr);
			mem_map_reserve(MAP_NR(phys_to_virt(page)));
			adr+=PAGE_SIZE;
			if (size > PAGE_SIZE)
				size-=PAGE_SIZE;
			else
				size=0;
		}
	}
	return mem;
}

static void rvfree(void * mem, unsigned long size)
{
	unsigned long adr, page;
        
	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	if (mem) 
	{
		adr=(unsigned long) mem;
		while (size > 0) 
		{
 			page = kvirt_to_phys(adr);
			mem_map_unreserve(MAP_NR(phys_to_virt(page)));
			adr+=PAGE_SIZE;
			if (size > PAGE_SIZE)
				size-=PAGE_SIZE;
			else
				size=0;
		}
		vfree(mem);
	}
}

int usb_cpia_get_version(struct usb_device *dev, void *buf)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE) | 0x80;
	dr.request = USB_REQ_CPIA_GET_VERSION;
	dr.value = 0;
	dr.index = 0;
	dr.length = 4;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, 4);
}

int usb_cpia_get_pnp_id(struct usb_device *dev, void *buf)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE) | 0x80;
	dr.request = USB_REQ_CPIA_GET_PNP_ID;
	dr.value = 0;
	dr.index = 0;
 	dr.length = 6;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, 6);
}

int usb_cpia_get_camera_status(struct usb_device *dev, void *buf)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE) | 0x80;
	dr.request = USB_REQ_CPIA_GET_CAMERA_STATUS;
	dr.value = 0;
	dr.index = 0;
	dr.length = 8;

	return dev->bus->op->control_msg(dev, usb_rcvctrlpipe(dev,0), &dr, buf, 8);
}

int usb_cpia_goto_hi_power(struct usb_device *dev)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_GOTO_HI_POWER;
	dr.value = 0;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_get_vp_version(struct usb_device *dev, void *buf)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_GET_VP_VERSION;
	dr.value = 0;
	dr.index = 0;
	dr.length = 4;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, buf, 4);
}

int usb_cpia_set_sensor_fps(struct usb_device *dev, int sensorbaserate, int sensorclkdivisor)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_SET_SENSOR_FPS;
	dr.value = (sensorclkdivisor << 8) + sensorbaserate;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_grab_frame(struct usb_device *dev, int streamstartline)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_GRAB_FRAME;
	dr.value = streamstartline << 8;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_upload_frame(struct usb_device *dev, int forceupload)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_UPLOAD_FRAME;
	dr.value = forceupload;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_set_grab_mode(struct usb_device *dev, int continuousgrab)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_SET_GRAB_MODE;
	dr.value = continuousgrab;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_set_format(struct usb_device *dev, int size, int subsample, int order)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_SET_FORMAT;
	dr.value = (subsample << 8) + size;
	dr.index = order;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_set_compression(struct usb_device *dev, int compmode, int decimation)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_SET_COMPRESSION;
	dr.value = (decimation << 8) + compmode;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_initstreamcap(struct usb_device *dev, int skipframes, int streamstartline)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_INIT_STREAM_CAP;
	dr.value = (streamstartline << 8) + skipframes;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_finistreamcap(struct usb_device *dev)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_FINI_STREAM_CAP;
	dr.value = 0;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_startstreamcap(struct usb_device *dev)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_START_STREAM_CAP;
	dr.value = 0;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

int usb_cpia_endstreamcap(struct usb_device *dev)
{
	devrequest dr;

	dr.requesttype = (USB_TYPE_VENDOR | USB_RECIP_DEVICE);
	dr.request = USB_REQ_CPIA_END_STREAM_CAP;
	dr.value = 0;
	dr.index = 0;
	dr.length = 0;

	return dev->bus->op->control_msg(dev, usb_sndctrlpipe(dev,0), &dr, NULL, 0);
}

/* How much data is left in the scratch buf? */
#define scratch_left(x)	(cpia->scratchlen - (int)((char *)x - (char *)cpia->scratch))

static void cpia_parse_data(struct usb_cpia *cpia)
{
	unsigned char *data = cpia->scratch;
	unsigned long l;
	int done;

	done = 0;
	while (!done && scratch_left(data)) {
		switch (cpia->state) {
		case STATE_SCANNING:
		{
			unsigned char *begin = data;

			/* We need atleast 2 bytes for the magic value */
			if (scratch_left(data) < 2) {
				done = 1;
				break;
			}

			/* 0x1968 is magic */
			printk("header: %X\n", (*data << 8) + *(data + 1));
			if ((*data == 0x19) && (*(data + 1) == 0x68)) {
				cpia->state = STATE_HEADER;
				printk("moving to header\n");
				break;
			}

			/* Woops, lost the header, find the end of the frame */
			if (scratch_left(data) < 4) {
				done = 1;
				break;
			}

			printk("Scanning for end of frame\n");
			while (scratch_left(data) >= 4) {
				if ((*data == 0xFF) &&
					(*(data + 1) == 0xFF) &&
					(*(data + 2) == 0xFF) &&
					(*(data + 3) == 0xFF)) {
					data += 4;
					break;
				}
				data++;
			}
#ifdef CPIA_DEBUG
			printk("scan: scanned %d bytes\n", data-begin);
#endif
			break;
		}
		case STATE_HEADER:
			/* We need atleast 64 bytes for the header */
			if (scratch_left(data) < 64) {
				done = 1;
				break;
			}

#ifdef CPIA_DEBUG
			printk("header: framerate %d\n", data[41]);
#endif

			data += 64;

			cpia->state = STATE_LINES;
				
			break;
		case STATE_LINES:
		{
			unsigned char *begin = data;
			int found = 0;

			while (scratch_left(data)) {
				if (*data == 0xFD) {
					data++;
					found = 1;
					break;
				} else if ((*data == 0xFF) &&
						(scratch_left(data) >= 3) &&
						(*(data + 1) == 0xFF) &&
						(*(data + 2) == 0xFF) &&
						(*(data + 3) == 0xFF)) {
					data += 4;
					cpia->curline = 144;
					found = 1;
					break;
				}

				data++;
			}

			if (data-begin == 355 && cpia->frame[cpia->curframe].width != 64) {
				int i;
				char *f = cpia->frame[cpia->curframe].data, *b = begin;

				b += 2;
				f += (cpia->frame[cpia->curframe].width * 3) * cpia->curline;

				for (i = 0; i < 176; i++)
					f[(i * 3) + 0] =
					f[(i * 3) + 1] =
					f[(i * 3) + 2] =
						b[(i * 2)];
			}

			if (found) {
				cpia->curline++;
				if (cpia->curline >= 144) {
					wake_up(&cpia->wq);
					cpia->state = STATE_SCANNING;
					cpia->curline = 0;
					cpia->curframe = -1;
					done = 1;
				}
			} else {
				data = begin;
				done = 1;
			}
			
			break;
		}
		}
	}

	/* Grab the remaining */
	l = scratch_left(data);
	memmove(cpia->scratch, data, l);

	cpia->scratchlen = l;
}

static int cpia_isoc_irq(int status, void *__buffer, int len, void *dev_id)
{
	struct usb_cpia *cpia = dev_id;
	struct usb_device *dev = cpia->dev;
	struct cpia_sbuf *sbuf;
	int i;
	char *p;

	if (!cpia->streaming) {
		printk("oops, not streaming, but interrupt\n");
		return 0;
	}
	
	if (cpia->curframe < 0) {
		if (cpia->frame[0].state == FRAME_READY) {
			cpia->curframe = 0;
			cpia->frame[0].state = FRAME_GRABBING;
#ifdef CPIA_DEBUG
			printk("capturing to frame 0\n");
#endif
		} else if (cpia->frame[1].state == FRAME_READY) {
			cpia->curframe = 1;
			cpia->frame[1].state = FRAME_GRABBING;
#ifdef CPIA_DEBUG
			printk("capturing to frame 1\n");
#endif
#ifdef CPIA_DEBUG
		} else
			printk("no frame available\n");
#else
		}
#endif
	}

	sbuf = &cpia->sbuf[cpia->receivesbuf];

	usb_unschedule_isochronous(dev, sbuf->isodesc);

	/* Do something to it now */
	sbuf->len = usb_compress_isochronous(dev, sbuf->isodesc);

#ifdef CPIA_DEBUG
	if (sbuf->len)
		printk("%d bytes received\n", sbuf->len);
#endif

	if (sbuf->len && cpia->curframe >= 0) {
		if (sbuf->len > (SCRATCH_BUF_SIZE - cpia->scratchlen)) {
			printk("overflow!\n");
			return 0;
		}
		memcpy(cpia->scratch + cpia->scratchlen, sbuf->data, sbuf->len);
		cpia->scratchlen += sbuf->len;

		cpia_parse_data(cpia);
	}

	/* Reschedule this block of Isochronous desc */
	usb_schedule_isochronous(dev, sbuf->isodesc, cpia->sbuf[(cpia->receivesbuf + 2) % 3].isodesc);

	/* Move to the next one */
	cpia->receivesbuf = (cpia->receivesbuf + 1) % 3;

	return 1;
}

int cpia_init_isoc(struct usb_cpia *cpia)
{
	struct usb_device *dev = cpia->dev;

	cpia->receivesbuf = 0;

	cpia->scratchlen = 0;
	cpia->curline = 0;
	cpia->state = STATE_SCANNING;

	/* Allocate all of the memory necessary */
	cpia->sbuf[0].isodesc = usb_allocate_isochronous(dev, usb_rcvisocpipe(dev,1), cpia->sbuf[0].data, STREAM_BUF_SIZE, 960, cpia_isoc_irq, cpia);
	cpia->sbuf[1].isodesc = usb_allocate_isochronous(dev, usb_rcvisocpipe(dev,1), cpia->sbuf[1].data, STREAM_BUF_SIZE, 960, cpia_isoc_irq, cpia);
	cpia->sbuf[2].isodesc = usb_allocate_isochronous(dev, usb_rcvisocpipe(dev,1), cpia->sbuf[2].data, STREAM_BUF_SIZE, 960, cpia_isoc_irq, cpia);

#ifdef CPIA_DEBUG
	printk("isodesc[0] @ %p\n", cpia->sbuf[0].isodesc);
	printk("isodesc[1] @ %p\n", cpia->sbuf[1].isodesc);
	printk("isodesc[2] @ %p\n", cpia->sbuf[2].isodesc);
#endif

	/* Schedule the queues */
	usb_schedule_isochronous(dev, cpia->sbuf[0].isodesc, NULL);
	usb_schedule_isochronous(dev, cpia->sbuf[1].isodesc, cpia->sbuf[0].isodesc);
	usb_schedule_isochronous(dev, cpia->sbuf[2].isodesc, cpia->sbuf[1].isodesc);

#ifdef CPIA_DEBUG
	printk("done scheduling\n");
#endif
	if (usb_set_interface(cpia->dev, 1, 3)) {
		printk("cpia_set_interface error\n");
		return -EINVAL;
	}

	usb_cpia_startstreamcap(cpia->dev);

	cpia->streaming = 1;
#ifdef CPIA_DEBUG
	printk("now streaming\n");
#endif

	return 0;
}

void cpia_stop_isoc(struct usb_cpia *cpia)
{
	struct usb_device *dev = cpia->dev;

	if (!cpia->streaming)
		return;

	cpia->streaming = 0;

	/* Stop the streaming */
	usb_cpia_endstreamcap(cpia->dev);

	/* Set packet size to 0 */
	if (usb_set_interface(cpia->dev, 1, 0)) {
		printk("cpia_set_interface error\n");
		return /* -EINVAL */;
	}

	/* Unschedule all of the iso td's */
	usb_unschedule_isochronous(dev, cpia->sbuf[2].isodesc);
	usb_unschedule_isochronous(dev, cpia->sbuf[1].isodesc);
	usb_unschedule_isochronous(dev, cpia->sbuf[0].isodesc);

	/* Delete them all */
	usb_delete_isochronous(dev, cpia->sbuf[2].isodesc);
	usb_delete_isochronous(dev, cpia->sbuf[1].isodesc);
	usb_delete_isochronous(dev, cpia->sbuf[0].isodesc);
}

/* Video 4 Linux API */
static int cpia_open(struct video_device *dev, int flags)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

#ifdef CPIA_DEBUG
	printk("cpia_open\n");
#endif

	cpia->fbuf = rvmalloc(2 * MAX_FRAME_SIZE);
	if (!cpia->fbuf)
		goto open_err_ret;

	cpia->frame[0].state = FRAME_DONE;
	cpia->frame[1].state = FRAME_DONE;

	cpia->frame[0].data = cpia->fbuf;
	cpia->frame[1].data = cpia->fbuf + MAX_FRAME_SIZE;
#ifdef CPIA_DEBUG
	printk("frame [0] @ %p\n", cpia->frame[0].data);
	printk("frame [1] @ %p\n", cpia->frame[1].data);
#endif

	cpia->sbuf[0].data = kmalloc(STREAM_BUF_SIZE, GFP_KERNEL);
	if (!cpia->sbuf[0].data)
		goto open_err_on0;

	cpia->sbuf[1].data = kmalloc(STREAM_BUF_SIZE, GFP_KERNEL);
	if (!cpia->sbuf[1].data)
		goto open_err_on1;

	cpia->sbuf[2].data = kmalloc(STREAM_BUF_SIZE, GFP_KERNEL);
	if (!cpia->sbuf[2].data)
		goto open_err_on2;

#ifdef CPIA_DEBUG
	printk("sbuf[0] @ %p\n", cpia->sbuf[0].data);
	printk("sbuf[1] @ %p\n", cpia->sbuf[1].data);
	printk("sbuf[2] @ %p\n", cpia->sbuf[2].data);
#endif

	cpia->curframe = -1;
	cpia->receivesbuf = 0;

	usb_cpia_initstreamcap(cpia->dev, 0, 60);

	cpia_init_isoc(cpia);

	return 0;

open_err_on2:
	kfree (cpia->sbuf[1].data);
open_err_on1:
	kfree (cpia->sbuf[0].data);
open_err_on0:
	rvfree(cpia->fbuf, 2 * MAX_FRAME_SIZE);
open_err_ret:
	return -ENOMEM;
}

static void cpia_close(struct video_device *dev)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

#ifdef CPIA_DEBUG
	printk("cpia_close\n");
#endif

	cpia_stop_isoc(cpia);

	usb_cpia_finistreamcap(cpia->dev);

	rvfree(cpia->fbuf, 2 * MAX_FRAME_SIZE);

	kfree(cpia->sbuf[2].data);
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

			strcpy(b.name, "CPiA USB Camera");
			b.type = VID_TYPE_CAPTURE /* | VID_TYPE_SUBCAPTURE */;
			b.channels = 1;
			b.audios = 0;
			b.maxwidth = 176	/* 352 */;
			b.maxheight = 144	/* 240 */;
			b.minwidth = 176	/* (Something small?) */;
			b.minheight = 144	/* "         " */;

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
		case VIDIOCGTUNER:
		{
			struct video_tuner v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			if (v.tuner)
				return -EINVAL;

			strcpy(v.name, "Format");

			v.rangelow = 0;
			v.rangehigh = 0;
			v.flags = 0;
			v.mode = VIDEO_MODE_AUTO;

			if (copy_to_user(arg, &v, sizeof(v)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			if (v.tuner)
				return -EINVAL;

			if (v.mode != VIDEO_MODE_AUTO)
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
#if 0
			p.depth = 24;
#endif
			p.depth = 16;
			p.palette = VIDEO_PALETTE_YUYV;

			if (copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture p;

			if (copy_from_user(&p, arg, sizeof(p)))
				return -EFAULT;

#ifdef CPIA_DEBUG
			printk("Attempting to set palette %d, depth %d\n",
				p.palette, p.depth);
#endif

			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window vw;

#ifdef CPIA_DEBUG
			printk("VIDIOCSWIN\n");
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

			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;

#ifdef CPIA_DEBUG
			printk("VIDIOCGWIN\n");
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

			cpia->frame[vm.frame].width = vm.width;
			cpia->frame[vm.frame].height = vm.height;

			/* Mark it as free */
			cpia->frame[vm.frame].state = FRAME_READY;

			return 0;
		}
		case VIDIOCSYNC:
		{
			int frame;

			if (copy_from_user((void *)&frame, arg, sizeof(int)))
				return -EFAULT;

#ifdef CPIA_DEBUG
			printk("syncing to frame %d\n", frame);
#endif
			switch (cpia->frame[frame].state) {
				case FRAME_UNUSED:
					return -EINVAL;
				case FRAME_READY:
				case FRAME_GRABBING:
					interruptible_sleep_on(&cpia->wq);
				case FRAME_DONE:
					cpia->frame[frame].state = FRAME_UNUSED;
					break;
			}
#ifdef CPIA_DEBUG
			printk("synced to frame %d\n", frame);
#endif
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

static long cpia_read(struct video_device *dev, char *buf, unsigned long count, int noblock)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;
	int len;

#ifdef CPIA_DEBUG
	printk("cpia_read: %ld bytes\n", count);
#endif
#if 0
	len = cpia_capture(cpia, buf, count);

	return len;
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
	while (size > 0)
	{
		page = kvirt_to_phys(pos);
		if (remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start+=PAGE_SIZE;
		pos+=PAGE_SIZE;
		if (size > PAGE_SIZE)
			size-=PAGE_SIZE;
		else
			size=0;
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

static void usb_cpia_configure(struct usb_cpia *cpia)
{
	struct usb_device *dev = cpia->dev;
	unsigned char version[4];
	unsigned char pnpid[6];
	unsigned char camerastat[8];
	unsigned char *buf;

	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		printk (KERN_INFO " Failed usb_set_configuration: CPIA\n");
		return;
	}

	if (usb_cpia_get_version(dev, version)) {
		printk("cpia_get_version error\n");
		return;
	}

	printk("cpia: Firmware v%d.%d, VC Hardware v%d.%d\n",
		version[0], version[1], version[2], version[3]);

	if (usb_cpia_get_pnp_id(dev, pnpid)) {
		printk("cpia_get_pnp_id error\n");
		return;
	}

	printk("cpia: PnP Id: Vendor: %X, Product: %X, Revision: %X\n",
		(pnpid[1] << 8) + pnpid[0], (pnpid[3] << 8) + pnpid[2],
		(pnpid[5] << 8) + pnpid[4]);

	memcpy(&cpia->vdev, &cpia_template, sizeof(cpia_template));

	init_waitqueue_head(&cpia->wq);

	if (video_register_device(&cpia->vdev, VFL_TYPE_GRABBER) == -1) {
		printk("video_register_device failed\n");
		return;
	}

	if (usb_cpia_goto_hi_power(dev)) {
		printk("cpia_goto_hi_power error\n");
		return;
	}

	if (usb_cpia_get_vp_version(dev, version)) {
		printk("cpia_get_vp_version error\n");
		return;
	}

	printk("cpia: VP v%d rev %d\n", version[0], version[1]);
	printk("cpia: Camera Head ID %04X\n", (version[3] << 8) + version[2]);

	/* Turn off continuous grab */
	if (usb_cpia_set_grab_mode(dev, 1)) {
		printk("cpia_set_grab_mode error\n");
		return;
	}

	/* Set up the sensor to be 30fps */
	if (usb_cpia_set_sensor_fps(dev, 1, 0)) {
		printk("cpia_set_sensor_fps error\n");
		return;
	}

	/* Set video into QCIF mode, and order into YUYV mode */
	if (usb_cpia_set_format(dev, CPIA_QCIF, 1, CPIA_YUYV)) {
		printk("cpia_set_format error\n");
		return;
	}

	/* Turn off compression */
	if (usb_cpia_set_compression(dev, 0, 0)) {
		printk("cpia_set_compression error\n");
		return;
	}
}

static int cpia_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_cpia *cpia;

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return -1;

	interface = &dev->config[0].altsetting[0].interface[0];

	/* Is it a CPiA? */
	if (dev->descriptor.idVendor != 0x0553)
		return -1;
	if (dev->descriptor.idProduct != 0x0002)
		return -1;
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

	usb_cpia_configure(cpia);

	return 0;
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

/*
 * This should be a separate module.
 */
int usb_cpia_init(void)
{
	usb_register(&cpia_driver);

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return usb_cpia_init();
}

void cleanup_module(void)
{
}
#endif

