/*
 * OmniVision OV511 Camera-to-USB Bridge Driver
 * Copyright 1999 Mark W. McClelland
 *
 * Based on the Linux CPiA driver.
 * 
 * Released under GPL v.2 license.
 *
 * Important keywords in comments:
 *    CAMERA SPECIFIC - Camera specific code; may not work with other cameras.
 *    DEBUG - Debugging code.
 *    FIXME - Something that is broken or needs improvement.
 *
 * Version History:
 *    Version 1.00 - Initial version
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define __NO_VERSION__

/* Handle mangled (versioned) external symbols */

#include <linux/autoconf.h> /* retrieve the CONFIG_* macros */
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#	define MODVERSIONS  /* force it on */
#endif

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

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
#include "ov511.h"

#define OV511_I2C_RETRIES 3

/* Video Size 384 x 288 x 3 bytes for RGB */
#define MAX_FRAME_SIZE (320 * 240 * 3)

// FIXME - Force CIF to make some apps happy for the moment. Should find a 
//         better way to do this.
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240

char kernel_version[] = UTS_RELEASE;

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

static struct usb_driver ov511_driver;

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

int ov511_reg_write(struct usb_device *dev, unsigned char reg, unsigned char value)
{
	int rc;

	rc = usb_control_msg(dev,
		usb_sndctrlpipe(dev, 0),
		2 /* REG_IO */,
		USB_TYPE_CLASS | USB_RECIP_DEVICE,
		0, (__u16)reg, &value, 1, HZ);	
			
	PDEBUG("reg write: 0x%02X:0x%02X\n", reg, value);
			
	return rc;
}

/* returns: negative is error, pos or zero is data */
int ov511_reg_read(struct usb_device *dev, unsigned char reg)
{
	int rc;
	unsigned char buffer[1];

	rc = usb_control_msg(dev,
		usb_rcvctrlpipe(dev, 0),
		2 /* REG_IO */,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE,
		0, (__u16)reg, buffer, 1, HZ);
                               
	PDEBUG("reg read: 0x%02X:0x%02X\n", reg, buffer[0]);
	
	if(rc < 0)
		return rc;
	else
		return buffer[0];	
}

int ov511_i2c_write(struct usb_device *dev, unsigned char reg, unsigned char value)
{
	int rc, retries;

	PDEBUG("i2c write: 0x%02X:0x%02X\n", reg, value);
	/* Three byte write cycle */
	for(retries = OV511_I2C_RETRIES;;) {
		/* Select camera register */
		rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_3_BYTE, reg);
		if (rc < 0) return rc;

		/* Write "value" to I2C data port of OV511 */
		rc = ov511_reg_write(dev, OV511_REG_I2C_DATA_PORT, value);	
		if (rc < 0) return rc;

		/* Initiate 3-byte write cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x01);
		if (rc < 0) return rc;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while(rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) return rc;

		if((rc&2) == 0) /* Ack? */
			break;

		/* I2C abort */	
		ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);

		if (--retries < 0) return -1;
	}

	return 0;
}

/* returns: negative is error, pos or zero is data */
int ov511_i2c_read(struct usb_device *dev, unsigned char reg)
{
	int rc, value, retries;

	/* Two byte write cycle */
	for(retries = OV511_I2C_RETRIES;;) {
		/* Select camera register */
		rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_2_BYTE, reg);
		if (rc < 0) return rc;

		/* Initiate 2-byte write cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x03);
		if (rc < 0) return rc;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while(rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) return rc;

		if((rc&2) == 0) /* Ack? */
			break;

		/* I2C abort */	
		ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);

		if (--retries < 0) return -1;
	}

	/* Two byte read cycle */
	for(retries = OV511_I2C_RETRIES;;) {
		/* Initiate 2-byte read cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x05);
		if (rc < 0) return rc;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while(rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) return rc;

		if((rc&2) == 0) /* Ack? */
			break;

		/* I2C abort */	
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);
		if (rc < 0) return rc;

		if (--retries < 0) return -1;
	}

	value = ov511_reg_read(dev, OV511_REG_I2C_DATA_PORT);
	PDEBUG("i2c read: 0x%02X:0x%02X\n", reg, value);
		
	/* This is needed to make ov511_i2c_write() work */
	rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x05);
	if (rc < 0) return rc;
	
	return (value);
}

int ov511_reset(struct usb_device *dev, unsigned char reset_type)
{
	int rc;
	
	PDEBUG("Reset: type=0x%X\n", reset_type);
	rc = ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, reset_type);
	if (rc < 0)
		printk(KERN_ERR "ov511: reset: command failed\n");

	rc = ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, 0);
	if (rc < 0)
		printk(KERN_ERR "ov511: reset: command failed\n");

	return rc;
}

int ov511_set_packet_size(struct usb_ov511 *ov511, int size)
{
	int alt, multiplier, err;
		
	PDEBUG("set packet size: %d\n", size);
	
	switch (size) {
		case 992:
			alt = 0;
			multiplier = 31;
			break;
		case 993:
			alt = 1;
			multiplier = 31;
			break;
		case 768:
			alt = 2;
			multiplier = 24;
			break;
		case 769:
			alt = 3;
			multiplier = 24;
			break;
		case 512:
			alt = 4;
			multiplier = 16;
			break;
		case 513:
			alt = 5;
			multiplier = 16;
			break;
		case 257:
			alt = 6;
			multiplier = 8;
			break;
		case 0:
			alt = 7;
			multiplier = 1; // FIXME - is this correct?
			break;
		default:
			printk(KERN_ERR "ov511_set_packet_size: invalid size (%d)\n",
			       size);
			return -EINVAL;
	}

	err = ov511_reg_write(ov511->dev, OV511_REG_FIFO_PACKET_SIZE,
	                          multiplier);
	if (err < 0) {
		printk(KERN_ERR "ov511: Set packet size: Set FIFO size ret %d\n",
		       err);
		return -ENOMEM;
	}
	
	if (usb_set_interface(ov511->dev, ov511->iface, alt) < 0) {
		printk(KERN_ERR "ov511: Set packet size: set interface error\n");
		return -EBUSY;
	}

	// FIXME - Should we only reset the FIFO?
	if (ov511_reset(ov511->dev, OV511_RESET_NOREGS) < 0)
		return -ENOMEM;

	return 0;
}

/* How much data is left in the scratch buf? */
#define scratch_left(x)	(ov511->scratchlen - (int)((char *)x - (char *)ov511->scratch))

static int ov511_move_data(struct usb_ov511 *ov511, urb_t *urb)
{
	unsigned char *cdata;
	int i, totlen = 0;
	int aPackNum[10];
	struct ov511_frame *frame;

	if (ov511->curframe == -1) {
	  return 0;
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;
		
		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		if (!n) continue;

		aPackNum[i] = n ? cdata[512] : -1;

		if (st){ 
			// Macro - must be in braces!
			PDEBUG("data error: [%d] len=%d, status=%d\n",
				i, n, st);
		}

		frame = &ov511->frame[ov511->curframe];
		
		/* Can we find a frame end */
		if ((cdata[0] | cdata[1] | cdata[2] | cdata[3] | 
		     cdata[4] | cdata[5] | cdata[6] | cdata[7]) == 0 &&
		    (cdata[8] & 8) && (cdata[8] & 0x80)) {

		    PDEBUG("Found Frame End!, packnum = %d\n", (int)(cdata[512]));
		    PDEBUG("Current frame = %d\n", ov511->curframe);

		    if (frame->scanstate == STATE_LINES) {
		        if (waitqueue_active(&frame->wq)) {
			  PDEBUG("About to wake up waiting processes\n");
			  frame->grabstate = FRAME_DONE;
			  wake_up_interruptible(&frame->wq);
			}
		    }
		}

		/* Can we find a frame start */
		else if ((cdata[0] | cdata[1] | cdata[2] | cdata[3] | 
			  cdata[4] | cdata[5] | cdata[6] | cdata[7]) == 0 &&
			 (cdata[8] & 8)) {
		    PDEBUG("ov511: Found Frame Start!, packnum = %d\n", (int)(cdata[512]));
		    frame->scanstate = STATE_LINES;
		    frame->curpix = 0;
		}

		/* Are we in a frame? */
		else if (frame->scanstate == STATE_LINES) {
		  unsigned char *f = frame->data + 3 * frame->curpix;
		  int i;
		  if (frame->curpix <= 320 * 240 - 256) {
		    for (i=0; i<256; i++) {
		      *f++ = *cdata;
		      *f++ = *cdata;
		      *f++ = *cdata++;
		      *f++ = *cdata;
		      *f++ = *cdata;
		      *f++ = *cdata++;
		    }
		    frame->curpix += 512;
		  } else {
		    PDEBUG("Too many pixels!\n");
		  }
		}

	}

	PDEBUG("pn: %d %d %d %d %d %d %d %d %d %d\n",
	       aPackNum[0], aPackNum[1], aPackNum[2], aPackNum[3], aPackNum[4],
	       aPackNum[5],aPackNum[6], aPackNum[7], aPackNum[8], aPackNum[9]);
	return totlen;
}

static void ov511_isoc_irq(struct urb *urb)
{
	int len;
	struct usb_ov511 *ov511 = urb->context;
	struct ov511_sbuf *sbuf;
	int i;

#if 0
	static int last_status, last_error_count, last_actual_length;
	if (last_status != urb->status ||
	    last_error_count != urb->error_count ||
	    last_actual_length != urb->actual_length) {
	  PDEBUG("ov511_isoc_irq: %p status %d, errcount = %d, length = %d\n", urb, urb->status, urb->error_count, urb->actual_length);
	  last_status = urb->status;
	  last_error_count = urb->error_count;
	  last_actual_length = urb->actual_length;
	}
#endif

	if (!ov511->streaming) {
		PDEBUG("hmmm... not streaming, but got interrupt\n");
		return;
	}
	
	sbuf = &ov511->sbuf[ov511->cursbuf];

	/* Copy the data received into our scratch buffer */
	len = ov511_move_data(ov511, urb);
#if 0
	/* If we don't have a frame we're current working on, complain */
	if (ov511->scratchlen) {
		if (ov511->curframe < 0) {
			// Macro - must be in braces!!
			PDEBUG("received data, but no frame available\n");
		} else
			ov511_parse_data(ov511);
	}
#endif
	for (i = 0; i < FRAMES_PER_DESC; i++) {
		sbuf->urb->iso_frame_desc[i].status = 0;
		sbuf->urb->iso_frame_desc[i].actual_length = 0;
	}
	
	/* Move to the next sbuf */
	ov511->cursbuf = (ov511->cursbuf + 1) % OV511_NUMSBUF;

	return;
}

static int ov511_init_isoc(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	urb_t *urb;
	int fx, err;
	
	ov511->compress = 0;
	ov511->curframe = -1;
	ov511->cursbuf = 0;
	ov511->scratchlen = 0;

	ov511_set_packet_size(ov511, 512);
	
#define OV511_COLOR_BAR_TEST
#ifdef OV511_COLOR_BAR_TEST
	{
	  int rc;
	  rc = ov511_i2c_read(ov511->dev, 0x12);
	  rc = ov511_i2c_write(ov511->dev, 0x12, 0x3f);
	  rc = ov511_i2c_read(ov511->dev, 0x12);
	  rc = ov511_i2c_read(ov511->dev, 0x13);
	  rc = ov511_i2c_write(ov511->dev, 0x14, 0x4);
	  rc = ov511_i2c_read(ov511->dev, 0x14);
	  rc = ov511_i2c_write(ov511->dev, 0x28, 0x60);
	  rc = ov511_i2c_read(ov511->dev, 0x28);
	  ov511_reg_write(ov511->dev, OV511_REG_CAMERA_DATA_INPUT_SELECT,
			      0);
	}
#endif

	/* We double buffer the Iso lists */
	urb = usb_alloc_urb(FRAMES_PER_DESC);
	
	if (!urb) {
		printk(KERN_ERR "ov511_init_isoc: usb_alloc_urb ret. NULL\n");
		return -ENOMEM;
	}
	ov511->sbuf[0].urb = urb;
	urb->dev = dev;
	urb->context = ov511;
	urb->pipe = usb_rcvisocpipe(dev, OV511_ENDPOINT_ADDRESS);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = ov511->sbuf[0].data;
 	urb->complete = ov511_isoc_irq;
 	urb->number_of_packets = FRAMES_PER_DESC;
 	urb->transfer_buffer_length = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
 	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
 		urb->iso_frame_desc[fx].offset = FRAME_SIZE_PER_DESC * fx;
		urb->iso_frame_desc[fx].length = FRAME_SIZE_PER_DESC;
	}

	urb = usb_alloc_urb(FRAMES_PER_DESC);
	if (!urb) {
		printk(KERN_ERR "ov511_init_isoc: usb_alloc_urb ret. NULL\n");
		return -ENOMEM;
	}
	ov511->sbuf[1].urb = urb;
	urb->dev = dev;
	urb->context = ov511;
	urb->pipe = usb_rcvisocpipe(dev, OV511_ENDPOINT_ADDRESS);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = ov511->sbuf[1].data;
 	urb->complete = ov511_isoc_irq;
 	urb->number_of_packets = FRAMES_PER_DESC;
 	urb->transfer_buffer_length = FRAME_SIZE_PER_DESC * FRAMES_PER_DESC;
 	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
 		urb->iso_frame_desc[fx].offset = FRAME_SIZE_PER_DESC * fx;
		urb->iso_frame_desc[fx].length = FRAME_SIZE_PER_DESC;
	}

	ov511->sbuf[1].urb->next = ov511->sbuf[0].urb;
	ov511->sbuf[0].urb->next = ov511->sbuf[1].urb;

	err = usb_submit_urb(ov511->sbuf[0].urb);
	if (err)
		printk(KERN_ERR "ov511_init_isoc: usb_run_isoc(0) ret %d\n",
			err);
	err = usb_submit_urb(ov511->sbuf[1].urb);
	if (err)
		printk(KERN_ERR "ov511_init_isoc: usb_run_isoc(1) ret %d\n",
			err);

	ov511->streaming = 1;

	return 0;
}


static void ov511_stop_isoc(struct usb_ov511 *ov511)
{
	if (!ov511->streaming)
		return;

	ov511_set_packet_size(ov511, 0);
	
	/* Unschedule all of the iso td's */
	usb_unlink_urb(ov511->sbuf[1].urb);
	usb_unlink_urb(ov511->sbuf[0].urb);

	ov511->streaming = 0;

	/* Delete them all */
	usb_free_urb(ov511->sbuf[1].urb);
	usb_free_urb(ov511->sbuf[0].urb);
}

static int ov511_new_frame(struct usb_ov511 *ov511, int framenum)
{
	struct ov511_frame *frame;
	int width, height;

	/* If we're not grabbing a frame right now and the other frame is */
	/*  ready to be grabbed into, then use it instead */
	if (ov511->curframe == -1) {
		if (ov511->frame[(framenum - 1 + OV511_NUMFRAMES) % OV511_NUMFRAMES].grabstate == FRAME_READY)
			framenum = (framenum - 1 + OV511_NUMFRAMES) % OV511_NUMFRAMES;
	} else
		return 0;

	frame = &ov511->frame[framenum];
	width = frame->width;
	height = frame->height;

	frame->grabstate = FRAME_GRABBING;
	frame->scanstate = STATE_SCANNING;
	frame->scanlength = 0;		/* accumulated in ov511_parse_data() */

	ov511->curframe = framenum;

	/* Make sure it's not too big */
	if (width > DEFAULT_WIDTH)
		width = DEFAULT_WIDTH;
	width = (width / 8) * 8;	/* Multiple of 8 */

	if (height > DEFAULT_HEIGHT)
		height = DEFAULT_HEIGHT;
	height = (height / 4) * 4;	/* Multiple of 4 */
	
//	/* We want a fresh frame every 30 we get */
//	ov511->compress = (ov511->compress + 1) % 30;

	return 0;
}



/* Video 4 Linux API */
static int ov511_open(struct video_device *dev, int flags)
{
	int err = -EBUSY;
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;

	PDEBUG("ov511_open\n");
	
	down(&ov511->lock);
	if (ov511->user)
		goto out_unlock;
		
	ov511->frame[0].grabstate = FRAME_UNUSED;
	ov511->frame[1].grabstate = FRAME_UNUSED;

	err = -ENOMEM;
	
	/* Allocate memory for the frame buffers */				
	ov511->fbuf = rvmalloc(2 * MAX_FRAME_SIZE);
	if (!ov511->fbuf)
		goto open_err_ret;

	ov511->frame[0].data = ov511->fbuf;
	ov511->frame[1].data = ov511->fbuf + MAX_FRAME_SIZE;

	PDEBUG("frame [0] @ %p\n", ov511->frame[0].data);
	PDEBUG("frame [1] @ %p\n", ov511->frame[1].data);

	ov511->sbuf[0].data = kmalloc(FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!ov511->sbuf[0].data)
		goto open_err_on0;
	ov511->sbuf[1].data = kmalloc(FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!ov511->sbuf[1].data)
		goto open_err_on1;
		
	PDEBUG("sbuf[0] @ %p\n", ov511->sbuf[0].data);
	PDEBUG("sbuf[1] @ %p\n", ov511->sbuf[1].data);

	/* Set default sizes in case IOCTL (VIDIOCMCAPTURE) is not used
	 * (using read() instead). */
	ov511->frame[0].width = DEFAULT_WIDTH;
	ov511->frame[0].height = DEFAULT_HEIGHT;
	ov511->frame[0].bytes_read = 0;
	ov511->frame[1].width = DEFAULT_WIDTH;
	ov511->frame[1].height = DEFAULT_HEIGHT;
	ov511->frame[1].bytes_read = 0;

	err = ov511_init_isoc(ov511);
	if (err)
		goto open_err_on2;

	ov511->user++;
	up(&ov511->lock);

	MOD_INC_USE_COUNT;

	return 0;

open_err_on2:
	kfree (ov511->sbuf[1].data);
open_err_on1:
	kfree (ov511->sbuf[0].data);
open_err_on0:
	rvfree(ov511->fbuf, 2 * MAX_FRAME_SIZE);
open_err_ret:
	return err;
out_unlock:
	up(&ov511->lock);
	return err;

}

static void ov511_close(struct video_device *dev)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;

	PDEBUG("ov511_close\n");
	
	down(&ov511->lock);	
	ov511->user--;

	MOD_DEC_USE_COUNT;

	ov511_stop_isoc(ov511);

	rvfree(ov511->fbuf, 2 * MAX_FRAME_SIZE);

	kfree(ov511->sbuf[1].data);
	kfree(ov511->sbuf[0].data);

	up(&ov511->lock);
}

static int ov511_init_done(struct video_device *dev)
{
	return 0;
}

static long ov511_write(struct video_device *dev, const char *buf, unsigned long count, int noblock)
{
	return -EINVAL;
}

// FIXME - Needs much work!!!
static int ov511_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	
	PDEBUG("IOCtl: 0x%X\n", cmd);
	
	switch (cmd) {
		case VIDIOCGCAP:
		{
			struct video_capability b;

			strcpy(b.name, "OV511 USB Camera");
			b.type = VID_TYPE_CAPTURE | VID_TYPE_SUBCAPTURE;
			b.channels = 1;
			b.audios = 0;
			b.maxwidth = DEFAULT_WIDTH;
			b.maxheight = DEFAULT_HEIGHT;
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
			if (vw.height != DEFAULT_HEIGHT)
				return -EINVAL;
			if (vw.width != DEFAULT_WIDTH)
				return -EINVAL;

			ov511->compress = 0;

			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;

			vw.x = 0;
			vw.y = 0;
			vw.width = DEFAULT_WIDTH;
			vw.height = DEFAULT_HEIGHT;
			vw.chromakey = 0;
			vw.flags = 30;

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

			PDEBUG("MCAPTURE\n");
			PDEBUG("frame: %d, size: %dx%d, format: %d\n",
				vm.frame, vm.width, vm.height, vm.format);

			if (vm.format != VIDEO_PALETTE_RGB24)
				return -EINVAL;

			if ((vm.frame != 0) && (vm.frame != 1))
				return -EINVAL;
				
			if (ov511->frame[vm.frame].grabstate == FRAME_GRABBING)
				return -EBUSY;

			/* Don't compress if the size changed */
			if ((ov511->frame[vm.frame].width != vm.width) ||
			    (ov511->frame[vm.frame].height != vm.height))
				ov511->compress = 0;

			ov511->frame[vm.frame].width = vm.width;
			ov511->frame[vm.frame].height = vm.height;

			/* Mark it as ready */
			ov511->frame[vm.frame].grabstate = FRAME_READY;

			return ov511_new_frame(ov511, vm.frame);
		}
		case VIDIOCSYNC:
		{
			int frame;

			if (copy_from_user((void *)&frame, arg, sizeof(int)))
				return -EFAULT;

			PDEBUG("syncing to frame %d\n", frame);
			
			switch (ov511->frame[frame].grabstate) {
				case FRAME_UNUSED:
					return -EINVAL;
				case FRAME_READY:
				case FRAME_GRABBING:
				case FRAME_ERROR:
redo:
				do {
#if 0
					init_waitqueue_head(&ov511->frame[frame].wq);
#endif
					interruptible_sleep_on(&ov511->frame[frame].wq);
					if (signal_pending(current))
						return -EINTR;
				} while (ov511->frame[frame].grabstate == FRAME_GRABBING);

				if (ov511->frame[frame].grabstate == FRAME_ERROR) {
					int ret;

					if ((ret = ov511_new_frame(ov511, frame)) < 0)
						return ret;
					goto redo;
				}				
				case FRAME_DONE:
					ov511->frame[frame].grabstate = FRAME_UNUSED;
					break;
			}

			ov511->frame[frame].grabstate = FRAME_UNUSED;
			
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

static long ov511_read(struct video_device *dev, char *buf, unsigned long count, int noblock)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	int frmx = -1;
	volatile struct ov511_frame *frame;

	PDEBUG("ov511_read: %ld bytes, noblock=%d\n", count, noblock);

	if (!dev || !buf)
		return -EFAULT;

	/* See if a frame is completed, then use it. */
	if (ov511->frame[0].grabstate >= FRAME_DONE)	/* _DONE or _ERROR */
		frmx = 0;
	else if (ov511->frame[1].grabstate >= FRAME_DONE)/* _DONE or _ERROR */
		frmx = 1;

	if (noblock && (frmx == -1))
		return -EAGAIN;

	/* If no FRAME_DONE, look for a FRAME_GRABBING state. */
	/* See if a frame is in process (grabbing), then use it. */
	if (frmx == -1) {
		if (ov511->frame[0].grabstate == FRAME_GRABBING)
			frmx = 0;
		else if (ov511->frame[1].grabstate == FRAME_GRABBING)
			frmx = 1;
	}

	/* If no frame is active, start one. */
	if (frmx == -1)
		ov511_new_frame(ov511, frmx = 0);

	frame = &ov511->frame[frmx];

restart:
	while (frame->grabstate == FRAME_GRABBING) {
		interruptible_sleep_on(&frame->wq);
		if (signal_pending(current))
			return -EINTR;
	}

	if (frame->grabstate == FRAME_ERROR) {
		frame->bytes_read = 0;
		printk(KERN_ERR "ov511_read: errored frame %d\n", ov511->curframe);
		if (ov511_new_frame(ov511, frmx))
			printk(KERN_ERR "ov511_read: ov511_new_frame error\n");
		goto restart;
	}

	PDEBUG("ov511_read: frmx=%d, bytes_read=%ld, scanlength=%ld\n", frmx,
		frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count))
		return -EFAULT;

	frame->bytes_read += count;
	PDEBUG("ov511_read: {copy} count used=%ld, new bytes_read=%ld\n",
		count, frame->bytes_read);

	if (frame->bytes_read >= frame->scanlength) { /* All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		ov511->frame[frmx].grabstate = FRAME_UNUSED;
		if (ov511_new_frame(ov511, frmx ? 0 : 1))
			printk(KERN_ERR "ov511_read: ov511_new_frame returned error\n");
	}

	return count;
}

static int ov511_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

	PDEBUG("mmap: %ld (%lX) bytes\n", size, size);

	if (size > (((2 * MAX_FRAME_SIZE) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)))
		return -EINVAL;

	pos = (unsigned long)ov511->fbuf;
	while (size > 0)
	{
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

// FIXME - needs V4L ID to be assigned
static struct video_device ov511_template = {
	"OV511 USB Camera",
	VID_TYPE_CAPTURE,
	VID_HARDWARE_CPIA,  /* FIXME */
	ov511_open,
	ov511_close,
	ov511_read,
	ov511_write,
	NULL,
	ov511_ioctl,
	ov511_mmap,
	ov511_init_done,
	NULL,
	0,
	0
};

static int ov511_configure(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	int temprc;   // DEBUG CODE

	/* Set altsetting 0 */
	if (usb_set_interface(dev, ov511->iface, 0) < 0) {
		printk(KERN_ERR "ov511: usb_set_interface error\n");
		return -EBUSY;
	}

	memcpy(&ov511->vdev, &ov511_template, sizeof(ov511_template));

	init_waitqueue_head(&ov511->frame[0].wq);
	init_waitqueue_head(&ov511->frame[1].wq);

	if (video_register_device(&ov511->vdev, VFL_TYPE_GRABBER) == -1) {
		printk(KERN_ERR "ov511: video_register_device failed\n");
		return -EBUSY;
	}

	/* Reset in case driver was unloaded and reloaded without unplug */
	if (ov511_reset(dev, OV511_RESET_ALL) < 0)
		goto error;

	/* Initialize system */
	if (ov511_reg_write(dev, OV511_REG_SYSTEM_INIT, 0x01) < 0) {
		printk(KERN_ERR "ov511: enable system: command failed\n");
		goto error;
	}

	/* This seems to be necessary */
	if (ov511_reset(dev, OV511_RESET_ALL) < 0)
		goto error;

	/* Disable compression */
	if (ov511_reg_write(dev, OV511_OMNICE_ENABLE, 0x00) < 0) {
		printk(KERN_ERR "ov511: disable compression: command failed\n");
		goto error;
	}

// FIXME - error checking needed
	ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE,
	                     OV7610_I2C_WRITE_ID);
	ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ,
	                     OV7610_I2C_READ_ID);

// DEBUG CODE
//	usb_ov511_reg_write(dev, OV511_REG_I2C_CLOCK_PRESCALER,
//						 OV511_I2C_CLOCK_PRESCALER);
	
	if (ov511_reset(dev, OV511_RESET_NOREGS) < 0)
		goto error;
	
	/* Dummy read to sync I2C */
	ov511_i2c_read(dev, 0x1C);
	
// DEBUG - TEST CODE FOR CAMERA REG READ
	temprc = ov511_i2c_read(dev, 0x1C);

     	temprc = ov511_i2c_read(dev, 0x1D);
// END DEBUG CODE
     	
	ov511->compress = 0;
	
	return 0;
	
error:
	video_unregister_device(&ov511->vdev);
	usb_driver_release_interface(&ov511_driver,
		&dev->actconfig->interface[ov511->iface]);

	kfree(ov511);

	return -EBUSY;	
}

static void* ov511_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct usb_ov511 *ov511;
	int rc;

	PDEBUG("probing for device...\n");

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return NULL;

	interface = &dev->actconfig->interface[ifnum].altsetting[0];

	/* Is it an OV511? */
	if (dev->descriptor.idVendor != 0x05a9)
		return NULL;
	if (dev->descriptor.idProduct != 0x0511)
		return NULL;

	/* Checking vendor/product should be enough, but what the hell */
	if (interface->bInterfaceClass != 0xFF) 
		return NULL;
	if (interface->bInterfaceSubClass != 0x00)
		return NULL;

	/* We found one */
	printk(KERN_INFO "ov511: USB OV511-based camera found\n");

	if ((ov511 = kmalloc(sizeof(*ov511), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "ov511: couldn't kmalloc ov511 struct\n");
		return NULL;
	}

	memset(ov511, 0, sizeof(*ov511));
	
	ov511->dev = dev;
	ov511->iface = interface->bInterfaceNumber;

	rc = ov511_reg_read(dev, OV511_REG_SYSTEM_CUSTOM_ID);
	if (rc < 0) {
		printk("ov511: Unable to read camera bridge registers\n");
		return NULL;
	} else if (rc == 3) {  /* D-Link DSB-C300 */
		printk("ov511: Camera is a D-Link DSB-C300\n");
		ov511->customid = 3;
	} else if (rc == 21) { /* Creative Labs WebCam 3 */
		printk("ov511: Camera is a Creative Labs WebCam 3\n");
		ov511->customid = 21;		
	} else {
		printk("ov511: Specific camera type (%d) not recognized\n", rc);
		printk("ov511: Please contact mmcclelland@delphi.com to request\n");
		printk("ov511: support for your camera.\n");
		return NULL;
	}

	if (!ov511_configure(ov511)) {
		ov511->user=0;
		init_MUTEX(&ov511->lock);	/* to 1 == available */
		return ov511;
	}
	else {
		printk(KERN_ERR "ov511: Failed to configure camera\n");
		return NULL;
	}
    	
     	return ov511;
}

static void ov511_disconnect(struct usb_device *dev, void *ptr)
{

	struct usb_ov511 *ov511 = (struct usb_ov511 *) ptr;

	video_unregister_device(&ov511->vdev);

	usb_driver_release_interface(&ov511_driver,
		&ov511->dev->actconfig->interface[ov511->iface]);

	/* Free the memory */
	kfree(ov511); ov511 = NULL;
}

static struct usb_driver ov511_driver = {
	"ov511",
	ov511_probe,
	ov511_disconnect,
	{ NULL, NULL }
};

int usb_ov511_init(void)
{
	PDEBUG("usb_ov511_init()\n");
	
	EXPORT_NO_SYMBOLS;
	
	return usb_register(&ov511_driver);
}

void usb_ov511_cleanup(void)
{
	usb_deregister(&ov511_driver);
}

#ifdef MODULE
int init_module(void)
{
	return usb_ov511_init();
}

void cleanup_module(void)
{
	usb_ov511_cleanup();
	
	PDEBUG("Module unloaded\n");
}
#endif

