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
#include <linux/config.h>
#include <linux/module.h>

#include <asm/spinlock.h>
#include <asm/io.h>

#include "usb.h"
#include "uhci.h"
#include "cpia.h"

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

#define scratch_left(x)	(cpia->scratchlen - (int)((char *)x - (char *)cpia->scratch))

static void cpia_parse_data(struct usb_cpia *cpia)
{
	unsigned char *data = cpia->scratch;
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

			printk("header: %X\n", (*data << 8) + *(data + 1));
			if ((*data == 0x19) && (*(data + 1) == 0x68)) {
				cpia->state = STATE_HEADER;
				printk("moving to header\n");
				break;
			}

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
printk("scan: scanned %d bytes\n", data-begin);
			break;
		}
		case STATE_HEADER:
			/* We need atleast 64 bytes for the header */
			if (scratch_left(data) < 64) {
				done = 1;
				break;
			}

printk("header: framerate %d\n", data[41]);

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
					data+=4;
					cpia->curline = 144;
					found = 1;
					break;
				}

				data++;
			}
#if 0
printk("line %d: scanned %d bytes\n", cpia->curline, data-begin);
#endif
if (data-begin == 355 && cpia->frame[cpia->curframe].width != 64) {
	int i;
	char *f = cpia->frame[cpia->curframe].data, *b = begin;

#if 0
printk("copying\n");
#endif

	b+=2;
	f+=(cpia->frame[cpia->curframe].width*3)*cpia->curline;
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

	{
	int l;

	l = scratch_left(data);
	memmove(cpia->scratch, data, l);
	cpia->scratchlen = l;
	}
}

static int cpia_isoc_irq(int status, void *__buffer, void *dev_id)
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
printk("capturing to frame 0\n");
		} else if (cpia->frame[1].state == FRAME_READY) {
			cpia->curframe = 1;
			cpia->frame[1].state = FRAME_GRABBING;
printk("capturing to frame 1\n");
		} else
printk("no frame available\n");
	}

	sbuf = &cpia->sbuf[cpia->receivesbuf];

	uhci_unsched_isochronous(dev, sbuf->isodesc);

	/* Do something to it now */
	sbuf->len = uhci_compress_isochronous(dev, sbuf->isodesc);

	if (sbuf->len)
	printk("%d bytes received\n", sbuf->len);

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
	uhci_sched_isochronous(dev, sbuf->isodesc, cpia->sbuf[(cpia->receivesbuf + 2) % 3].isodesc);

	/* Move to the next one */
	cpia->receivesbuf = (cpia->receivesbuf + 1) % 3;

	return 1;
}

int cpia_init_isoc(struct usb_cpia *cpia)
{
	struct usb_device *dev = cpia->dev;

	cpia->receivesbuf = 0;

#if 0
	cpia->parsesbuf = 0;
	cpia->parsepos = 0;
#endif
	cpia->scratchlen = 0;
	cpia->curline = 0;
	cpia->state = STATE_SCANNING;

	/* Allocate all of the memory necessary */
	cpia->sbuf[0].isodesc = uhci_alloc_isochronous(dev, usb_rcvisocpipe(dev,1), cpia->sbuf[0].data, STREAM_BUF_SIZE, 960, cpia_isoc_irq, cpia);
	cpia->sbuf[1].isodesc = uhci_alloc_isochronous(dev, usb_rcvisocpipe(dev,1), cpia->sbuf[1].data, STREAM_BUF_SIZE, 960, cpia_isoc_irq, cpia);
	cpia->sbuf[2].isodesc = uhci_alloc_isochronous(dev, usb_rcvisocpipe(dev,1), cpia->sbuf[2].data, STREAM_BUF_SIZE, 960, cpia_isoc_irq, cpia);

	printk("isodesc[0] @ %p\n", cpia->sbuf[0].isodesc);
	printk("isodesc[1] @ %p\n", cpia->sbuf[1].isodesc);
	printk("isodesc[2] @ %p\n", cpia->sbuf[2].isodesc);

	/* Schedule the queues */
	uhci_sched_isochronous(dev, cpia->sbuf[0].isodesc, NULL);
	uhci_sched_isochronous(dev, cpia->sbuf[1].isodesc, cpia->sbuf[0].isodesc);
	uhci_sched_isochronous(dev, cpia->sbuf[2].isodesc, cpia->sbuf[1].isodesc);

	if (usb_set_interface(cpia->dev, 1, 3)) {
		printk("cpia_set_interface error\n");
		return -EINVAL;
	}

	usb_cpia_startstreamcap(cpia->dev);

	cpia->streaming = 1;

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
		return -EINVAL;
	}

	/* Unschedule all of the iso td's */
	uhci_unsched_isochronous(dev, cpia->sbuf[2].isodesc);
	uhci_unsched_isochronous(dev, cpia->sbuf[1].isodesc);
	uhci_unsched_isochronous(dev, cpia->sbuf[0].isodesc);

	/* Delete them all */
	uhci_delete_isochronous(dev, cpia->sbuf[2].isodesc);
	uhci_delete_isochronous(dev, cpia->sbuf[1].isodesc);
	uhci_delete_isochronous(dev, cpia->sbuf[0].isodesc);
}

/* Video 4 Linux API */
static int cpia_open(struct video_device *dev, int flags)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

printk("cpia_open\n");

	cpia->fbuf = rvmalloc(2 * MAX_FRAME_SIZE);
	if (!cpia->fbuf)
		return -ENOMEM;

	cpia->frame[0].state = FRAME_DONE;
	cpia->frame[1].state = FRAME_DONE;

	cpia->frame[0].data = cpia->fbuf;
	cpia->frame[1].data = cpia->fbuf + MAX_FRAME_SIZE;
	printk("frame [0] @ %p\n", cpia->frame[0].data);
	printk("frame [1] @ %p\n", cpia->frame[1].data);

	cpia->sbuf[0].data = kmalloc(STREAM_BUF_SIZE, GFP_KERNEL);
	if (!cpia->sbuf[0].data)
		return -ENOMEM;

	cpia->sbuf[1].data = kmalloc(STREAM_BUF_SIZE, GFP_KERNEL);
	if (!cpia->sbuf[1].data)
		return -ENOMEM;

	cpia->sbuf[2].data = kmalloc(STREAM_BUF_SIZE, GFP_KERNEL);
	if (!cpia->sbuf[2].data)
		return -ENOMEM;

	printk("sbuf[0] @ %p\n", cpia->sbuf[0].data);
	printk("sbuf[1] @ %p\n", cpia->sbuf[1].data);
	printk("sbuf[2] @ %p\n", cpia->sbuf[2].data);

	cpia->curframe = -1;
	cpia->receivesbuf = 0;

	usb_cpia_initstreamcap(cpia->dev, 0, 60);

	cpia_init_isoc(cpia);

	return 0;
}

static void cpia_close(struct video_device *dev)
{
	struct usb_cpia *cpia = (struct usb_cpia *)dev;

printk("cpia_close\n");

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

#if 0
	if (usb_set_interface(dev, 1, 3)) {
		printk("cpia_set_interface error\n");
		return -EINVAL;
	}

	if (usb_cpia_grab_frame(dev, 0)) {
		printk("cpia_grab_frame error\n");
		return -EINVAL;
	}

	if (usb_cpia_upload_frame(dev, 0)) {
		printk("cpia_upload_frame error\n");
		return -EINVAL;
	}

	buf = cpia->ibuf;
	uhci_receive_isochronous(dev, usb_rcvisocpipe(dev,1), buf, 176*144*4);

	{
	printk("header magic: %X\n", (buf[0] << 8) + buf[1]);

	while ((buf[0] != 0x19) || (buf[1] != 0x68)) {
		int i;

		printk("resync'ing\n");
		for (i=0;i<(176*144*4)-4;i++, buf++)
			if (
				(buf[0] == 0xFF) &&
				(buf[1] == 0xFF) &&
				(buf[2] == 0xFF) &&
				(buf[3] == 0xFF)) {
				buf+=4;
				i+=4;
				break;
			}

		memmove(cpia->ibuf, buf, (176*144*4) - i);
		uhci_receive_isochronous(dev, usb_rcvisocpipe(dev,1), cpia->ibuf + (176*144*4) - i, i);
		buf = cpia->ibuf;

#if 0
		printk("header magic: %X\n", (buf[0] << 8) + buf[1]);
#endif
	}

	printk("size: %d, sample: %d, order: %d\n", buf[16], buf[17], buf[18]);
	printk("comp: %d, decimation: %d\n", buf[28], buf[29]);
	printk("roi: top left: %d, %d bottom right: %d, %d\n",
		buf[26] * 4, buf[24] * 8,
		buf[27] * 4, buf[25] * 8);

	printk("vm->frame: %d\n", vm->frame);

	{
	int i, i1;
	char *b = buf + 64, *fbuf = &cpia->fbuffer[MAX_FRAME_SIZE * (vm->frame & 1)];
	for (i=0;i<144;i++) {
#if 0
		printk("line len: %d\n", (b[1] << 8) + b[0]);
#endif
		b += 2;
		for (i1=0;i1<176;i1++) {
			fbuf[(i * vm->width * 3) + (i1 * 3)] = 
			fbuf[(i * vm->width * 3) + (i1 * 3) + 1] = 
			fbuf[(i * vm->width * 3) + (i1 * 3) + 2] = 
				b[i1 * 2];
#if 0
			*((short *)&fbuf[(i * vm->width * 2) + (i1 * 2)]) =
				((b[i1 * 2] >> 3) << 11) + ((b[i1 * 2] >> 2) << 6) + (b[i1 * 2] >> 3);
#endif
		}
		b += (176 * 2) + 1;
	}
	}

	}

	if (usb_set_interface(dev, 1, 0)) {
		printk("cpia_set_interface error\n");
		return -EINVAL;
	}
#endif

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
			b.maxwidth = 176 /* 352 */;
			b.maxheight = 144 /* 240 */;
			b.minwidth = 176 /* (Something small?) */;
			b.minheight = 144 /* "         " */;

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

printk("Attempting to set palette %d, depth %d\n", p.palette, p.depth);

#if 0
			if (p.palette != VIDEO_PALETTE_YUYV)
				return -EINVAL;
			if (p.depth != 16)
				return -EINVAL;
#endif

			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window vw;

printk("VIDIOCSWIN\n");
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

printk("VIDIOCGWIN\n");
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

printk("MCAPTURE\n");
printk("frame: %d, size: %dx%d, format: %d\n", vm.frame, vm.width, vm.height, vm.format);

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

			printk("syncing to frame %d\n", frame);
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
			printk("synced to frame %d\n", frame);
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

	printk("cpia_read: %d bytes\n", count);
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

	printk("mmap: %d (%X) bytes\n", size, size);
	if (size > (((2 * MAX_FRAME_SIZE) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)))
		return -EINVAL;

#if 0
	if (!cpia->fbuffer) {
		if ((cpia->fbuffer = rvmalloc(2 * MAX_FRAME_SIZE)) == NULL)
			return -EINVAL;
	}
#endif

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

	usb_set_configuration(dev, dev->config[0].bConfigurationValue);

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

#if 0
	if (usb_cpia_grab_frame(dev, 0)) {
		printk("cpia_grab_frame error\n");
		return;
	}

	if (usb_cpia_upload_frame(dev, 1)) {
		printk("cpia_upload_frame error\n");
		return;
	}

	buf = (void *)__get_free_page(GFP_KERNEL);

	{
	int i;
	for (i=0;i<448;i++)
		buf[i]=0;
	}
	uhci_receive_isochronous(dev, usb_rcvisocpipe(dev,1), buf, 448);

	{
	int i;
	for (i=0;i<448;i++) {
		printk("%02X ", buf[i]);
		if ((i % 16) == 15)
			printk("\n");
	}
	printk("\n");
	}

	free_page((unsigned long)buf);
#endif
}

static int cpia_probe(struct usb_device *dev)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_cpia *cpia;

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return -1;

#if 0
	/* We don't handle multi-interface hubs */
	if (dev->config[0].bNumInterfaces != 1)
		return -1;
#endif

	interface = &dev->config[0].interface[0];

	/* Is it a CPiA? */
/*
Apr 24 17:49:04 bjorn kernel:   Vendor:  0545 
Apr 24 17:49:04 bjorn kernel:   Product: 8080 
*/
/*
	if (dev->descriptor.idVendor != 0x0545)
		return -1;
	if (dev->descriptor.idProduct != 0x8080)
		return -1;
	if (interface->bInterfaceClass != 0xFF)
		return -1;
	if (interface->bInterfaceSubClass != 0xFF)
		return -1;
*/
	if (dev->descriptor.idVendor != 0x0553)
		return -1;
	if (dev->descriptor.idProduct != 0x0002)
		return -1;
	if (interface->bInterfaceClass != 0xFF)
		return -1;
	if (interface->bInterfaceSubClass != 0x00)
		return -1;

#if 0
	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (interface->bNumEndpoints != 1)
		return -1;

	endpoint = &interface->endpoint[0];

	/* Output endpoint? Curiousier and curiousier.. */
	if (!(endpoint->bEndpointAddress & 0x80))
		return -1;

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & 3) != 3)
		return -1;
#endif

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

#if 0
	usb_request_irq(dev, usb_rcvctrlpipe(dev, endpoint->bEndpointAddress), pport_irq, endpoint->bInterval, pport);
#endif

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
void module_cleanup(void)
{
}
#endif

