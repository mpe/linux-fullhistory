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
 * Version: 1.05
 *
 * Please see the file: linux/Documentation/usb/ov511.txt 
 * and the website at:  http://people.delphi.com/mmcclelland/linux/ 
 * for more info.
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

#include <linux/config.h>   /* retrieve the CONFIG_* macros */
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

#define OV7610_AUTO_ADJUST 1

/* Video Size 640 x 480 x 3 bytes for RGB */
#define MAX_FRAME_SIZE (640 * 480 * 3)

// FIXME - Force CIF to make some apps happy for the moment. Should find a 
//         better way to do this.
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480

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
			
#if 0
	PDEBUG("reg write: 0x%02X:0x%02X", reg, value);
#endif
			
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
                               
#if 0
	PDEBUG("reg read: 0x%02X:0x%02X", reg, buffer[0]);
#endif
	
	if(rc < 0)
		return rc;
	else
		return buffer[0];	
}

int ov511_i2c_write(struct usb_device *dev, unsigned char reg, unsigned char value)
{
	int rc, retries;

#if 0
	PDEBUG("i2c write: 0x%02X:0x%02X", reg, value);
#endif
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
#if 0
	PDEBUG("i2c read: 0x%02X:0x%02X", reg, value);
#endif
		
	/* This is needed to make ov511_i2c_write() work */
	rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x05);
	if (rc < 0) return rc;
	
	return (value);
}

#if 0
static void ov511_dump_i2c_range( struct usb_device *dev, int reg1, int regn)
{
	int i;
	int rc;
	for(i=reg1; i<=regn; i++) {
	  rc = ov511_i2c_read(dev, i);
#if 0
	  PDEBUG("OV7610[0x%X] = 0x%X", i, rc);
#endif
	}
}

static void ov511_dump_i2c_regs( struct usb_device *dev)
{
	PDEBUG("I2C REGS");
	ov511_dump_i2c_range(dev, 0x00, 0x38);
}

static void ov511_dump_reg_range( struct usb_device *dev, int reg1, int regn)
{
	int i;
	int rc;
	for(i=reg1; i<=regn; i++) {
	  rc = ov511_reg_read(dev, i);
	  PDEBUG("OV511[0x%X] = 0x%X", i, rc);
	}
}

static void ov511_dump_regs( struct usb_device *dev)
{
	PDEBUG("CAMERA INTERFACE REGS");
	ov511_dump_reg_range(dev, 0x10, 0x1f);
	PDEBUG("DRAM INTERFACE REGS");
	ov511_dump_reg_range(dev, 0x20, 0x23);
	PDEBUG("ISO FIFO REGS");
	ov511_dump_reg_range(dev, 0x30, 0x31);
	PDEBUG("PIO REGS");
	ov511_dump_reg_range(dev, 0x38, 0x39);
	ov511_dump_reg_range(dev, 0x3e, 0x3e);
	PDEBUG("I2C REGS");
	ov511_dump_reg_range(dev, 0x40, 0x49);
	PDEBUG("SYSTEM CONTROL REGS");
	ov511_dump_reg_range(dev, 0x50, 0x53);
	ov511_dump_reg_range(dev, 0x5e, 0x5f);
	PDEBUG("OmniCE REGS");
	ov511_dump_reg_range(dev, 0x70, 0x79);
	ov511_dump_reg_range(dev, 0x80, 0x9f);
	ov511_dump_reg_range(dev, 0xa0, 0xbf);

}
#endif

int ov511_i2c_reset(struct usb_device *dev)
{
	int rc;

	PDEBUG("Reset 7610");
	rc = ov511_i2c_write(dev, 0x12, 0x80);
	if (rc < 0)
		err("i2c reset: command failed");

	return rc;
}

int ov511_reset(struct usb_device *dev, unsigned char reset_type)
{
	int rc;
	
	PDEBUG("Reset: type=0x%X", reset_type);
	rc = ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, reset_type);
	if (rc < 0)
		err("reset: command failed");

	rc = ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, 0);
	if (rc < 0)
		err("reset: command failed");

	return rc;
}

int ov511_set_packet_size(struct usb_ov511 *ov511, int size)
{
	int alt, multiplier, rc;
		
#if 0
	PDEBUG("set packet size: %d", size);
#endif
	
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
			err("Set packet size: invalid size (%d)", size);
			return -EINVAL;
	}

	rc = ov511_reg_write(ov511->dev, OV511_REG_FIFO_PACKET_SIZE,
	                     multiplier);
	if (rc < 0) {
		err("Set packet size: Set FIFO size ret %d", rc);
		return -ENOMEM;
	}
	
	if (usb_set_interface(ov511->dev, ov511->iface, alt) < 0) {
		err("Set packet size: set interface error");
		return -EBUSY;
	}

	// FIXME - Should we only reset the FIFO?
	if (ov511_reset(ov511->dev, OV511_RESET_NOREGS) < 0)
		return -ENOMEM;

	return 0;
}

static inline int ov7610_set_picture(struct usb_ov511 *ov511,
                                     struct video_picture *p)
{
	if(ov511_i2c_write(ov511->dev, OV7610_REG_SAT, p->colour >> 8) < 0)
		return -EIO;

	if(ov511_i2c_write(ov511->dev, OV7610_REG_CNT, p->contrast >> 8) < 0)
		return -EIO;

	if(ov511_i2c_write(ov511->dev, OV7610_REG_BRT, p->brightness >> 8) < 0)
		return -EIO;

	return 0;
}

static inline int ov7610_get_picture(struct usb_ov511 *ov511,
                                     struct video_picture *p)
{
	int ret;

	if((ret = ov511_i2c_read(ov511->dev, OV7610_REG_SAT)) < 0) return -EIO;
	p->colour = ret << 8;

	if((ret = ov511_i2c_read(ov511->dev, OV7610_REG_CNT)) < 0) return -EIO;
	p->contrast = ret << 8;

	if((ret = ov511_i2c_read(ov511->dev, OV7610_REG_BRT)) < 0) return -EIO;
	p->brightness = ret << 8;

	p->hue = 0x8000;
	p->whiteness = 105 << 8;
	p->depth = 24;
	p->palette = VIDEO_PALETTE_RGB24;

	return 0;
}

static int ov511_mode_init_regs(struct usb_ov511 *ov511,
				int width, int height, int mode)
{
	int rc = 0;
	struct usb_device *dev = ov511->dev;

#if 0
	PDEBUG("ov511_mode_init_regs(ov511, %d, %d, %d)", width, height, mode);
#endif
	ov511_set_packet_size(ov511, 0);

	/* Set mode consistent registers */
	ov511_i2c_write(dev, 0x0f, 0x03);
	ov511_i2c_write(dev, 0x10, 0xff);
	ov511_i2c_write(dev, 0x13, 0x01);
	ov511_i2c_write(dev, 0x16, 0x06);
	ov511_i2c_write(dev, 0x20, 0x1c);
	ov511_i2c_write(dev, 0x24, 0x2e); /* 10 */
	ov511_i2c_write(dev, 0x25, 0x7c); /* 8a */
	ov511_i2c_write(dev, 0x26, 0x70);
	ov511_i2c_write(dev, 0x28, 0x24); /* 24 */
	ov511_i2c_write(dev, 0x2b, 0xac);
	ov511_i2c_write(dev, 0x2c, 0xfe);
	ov511_i2c_write(dev, 0x2d, 0x93);
	ov511_i2c_write(dev, 0x34, 0x8b);

	if (width == 640 && height == 480) {
		ov511_reg_write(dev, 0x12, 0x4f);
		ov511_reg_write(dev, 0x13, 0x3d);
		ov511_reg_write(dev, 0x14, 0x00);
		ov511_reg_write(dev, 0x15, 0x00);
		ov511_reg_write(dev, 0x18, 0x03);

		ov511_i2c_write(dev, 0x11, 0x01);
		ov511_i2c_write(dev, 0x12, 0x24);
		ov511_i2c_write(dev, 0x14, 0x04);
		ov511_i2c_write(dev, 0x35, 0x9e);
	} else if (width == 320 && height == 240) {
		ov511_reg_write(dev, 0x12, 0x27);
		ov511_reg_write(dev, 0x13, 0x1f);
		ov511_reg_write(dev, 0x14, 0x00);
		ov511_reg_write(dev, 0x15, 0x00);
		ov511_reg_write(dev, 0x18, 0x03);

		ov511_i2c_write(dev, 0x11, 0x00);
		ov511_i2c_write(dev, 0x12, 0x04);
		ov511_i2c_write(dev, 0x14, 0x24);
		ov511_i2c_write(dev, 0x35, 0x1e);
	} else {
		err("Unknown mode (%d, %d): %d", width, height, mode);
		rc = -EINVAL;
	}
	ov511_set_packet_size(ov511, 993);

	return rc;
}

	
/*************************************************************

Turn a YUV4:2:0 block into an RGB block

*************************************************************/
#define LIMIT(x) ((((x)>0xffffff)?0xff0000:(((x)<=0xffff)?0:(x)&0xff0000))>>16)
static inline void ov511_move_420_block(int y00, int y01, int y10, int y11,
					int u, int v, int w,
					unsigned char * pOut)
{
	int r    = 68911 * v;
	int g    = -16915 * u + -35101 * v;
	int b    = 87097 * u;
	y00 *= 49152;
	y01 *= 49152;
	y10 *= 49152;
	y11 *= 49152;
	*(pOut+w*3) = LIMIT(r + y10);
	*pOut++     = LIMIT(r + y00);
	*(pOut+w*3) = LIMIT(g + y10);
	*pOut++     = LIMIT(g + y00);
	*(pOut+w*3) = LIMIT(b + y10);
	*pOut++     = LIMIT(b + y00);
	*(pOut+w*3) = LIMIT(r + y11);
	*pOut++     = LIMIT(r + y01);
	*(pOut+w*3) = LIMIT(g + y11);
	*pOut++     = LIMIT(g + y01);
	*(pOut+w*3) = LIMIT(b + y11);
	*pOut++     = LIMIT(b + y01);
}

/***************************************************************

For a 640x480 YUV4:2:0 images, data shows up in 1200 384 byte segments.  The
first 64 bytes of each segment are V, the next 64 are U.  The V and
U are arranged as follows:

  0  1 ...  7
  8  9 ... 15
       ...   
 56 57 ... 63

The next 256 bytes are Y data and represent 4 squares of 8x8 pixels as
follows:

  0  1 ...  7    64  65 ...  71   ...  192 193 ... 199
  8  9 ... 15    72  73 ...  79        200 201 ... 207
       ...              ...                    ...
 56 57 ... 63   120 121     127        248 249 ... 255

If OV511_DUMPPIX is defined, _parse_data just dumps the
incoming segments, verbatim, in order, into the frame.
When used with vidcat -f ppm -s 640x480 this puts the data
on the standard output and can be analyzed with the parseppm.c
utility I wrote.  That's a much faster way for figuring out how
this data is scrambled.

****************************************************************/ 
#define HDIV 8
#define WDIV (256/HDIV)

static void ov511_parse_data(unsigned char * pIn0,
			     unsigned char * pOut0,
			     int iWidth,
			     int iSegment)
			    
{
#ifndef OV511_DUMPPIX
    int k, l, m;
    unsigned char * pIn;
    unsigned char * pOut, * pOut1;

    int iHalf = (iSegment / (iWidth / 32)) & 1;
    int iY     = iSegment / (iWidth / WDIV);
    int jY     = iSegment - iY * (iWidth / WDIV);
    int iOutY  = (iY*HDIV*iWidth + jY*WDIV) * 3;
    int iUV    = iSegment / (iWidth / WDIV * 2);
    int jUV    = iSegment - iUV * (iWidth / WDIV * 2);
    int iOutUV = (iUV*HDIV*2*iWidth + jUV*WDIV/2) * 3;

    /* Just copy the Y's if in the first stripe */
    if (!iHalf) {
	pIn = pIn0 + 128;
	pOut = pOut0 + iOutY;
	for(k=0; k<4; k++) {
	    pOut1 = pOut;
	    for(l=0; l<8; l++) {
	      for(m=0; m<8; m++) {
		*pOut1 = *pIn++;
		pOut1 += 3;
	      }
	      pOut1 += (iWidth - 8) * 3;
	    }
	    pOut += 8 * 3;
	}
    }

    /* Use the first half of VUs to calculate value */
    pIn = pIn0;
    pOut = pOut0 + iOutUV;
    for(l=0; l<4; l++) {
	for(m=0; m<8; m++) {
	    int y00 = *(pOut);
	    int y01 = *(pOut+3);
	    int y10 = *(pOut+iWidth*3);
	    int y11 = *(pOut+iWidth*3+3);
	    int u   = *(pIn+64) - 128;
	    int v   = *pIn++ - 128;
	    ov511_move_420_block(y00, y01, y10, y11, u, v, iWidth, pOut);
	    pOut += 6;
	}
	pOut += (iWidth*2 - 16) * 3;
    }

    /* Just copy the other UV rows */
    for(l=0; l<4; l++) {
	for(m=0; m<8; m++) {
	  *pOut++ = *(pIn + 64);
	  *pOut = *pIn++;
	  pOut += 5;
	}
	pOut += (iWidth*2 - 16) * 3;
    }

    /* Calculate values if it's the second half */
    if (iHalf) {
	pIn = pIn0 + 128;
	pOut = pOut0 + iOutY;
	for(k=0; k<4; k++) {
	    pOut1 = pOut;
	    for(l=0; l<4; l++) {
	      for(m=0; m<4; m++) {
		int y10 = *(pIn+8);
		int y00 = *pIn++;
		int y11 = *(pIn+8);
		int y01 = *pIn++;
		int u   = *pOut1 - 128;
		int v   = *(pOut1+1) - 128;
		ov511_move_420_block(y00, y01, y10, y11, u, v, iWidth, pOut1);
		pOut1 += 6;
	      }
	      pOut1 += (iWidth*2 - 8) * 3;
	      pIn += 8;
	    }
	    pOut += 8 * 3;
	}
    }

#else
	/* Just dump pix data straight out for debug */
	int i;
	pOut0 += iSegment * 384;
	for(i=0; i<384; i++) {
	  *pOut0++ = *pIn0++;
	}
#endif
}

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

		aPackNum[i] = n ? cdata[992] : -1;

		if (st)
			PDEBUG("data error: [%d] len=%d, status=%d", i, n, st);

		frame = &ov511->frame[ov511->curframe];
		
		/* Can we find a frame end */
		if ((cdata[0] | cdata[1] | cdata[2] | cdata[3] | 
		     cdata[4] | cdata[5] | cdata[6] | cdata[7]) == 0 &&
		    (cdata[8] & 8) && (cdata[8] & 0x80)) {

#if 0
		    PDEBUG("Found Frame End!, packnum = %d", (int)(cdata[992]));
		    PDEBUG("Current frame = %d", ov511->curframe);
#endif

		    if (frame->scanstate == STATE_LINES) {
		        if (waitqueue_active(&frame->wq)) {
#if 0
			  PDEBUG("About to wake up waiting processes");
#endif
			  frame->grabstate = FRAME_DONE;
			  ov511->curframe = -1;
			  wake_up_interruptible(&frame->wq);
			}
		    }
		}

		/* Can we find a frame start */
		else if ((cdata[0] | cdata[1] | cdata[2] | cdata[3] | 
			  cdata[4] | cdata[5] | cdata[6] | cdata[7]) == 0 &&
			 (cdata[8] & 8)) {
#if 0
			PDEBUG("ov511: Found Frame Start!, packnum = %d", (int)(cdata[992]));
			PDEBUG("ov511: Frame Header Byte = 0x%x", (int)(cdata[8]));
#endif
		    frame->scanstate = STATE_LINES;
			frame->segment = 0;
		}

		/* Are we in a frame? */
		if (frame->scanstate == STATE_LINES) {
			unsigned char * pData;
			int iPix;

			/* Deal with leftover from last segment, if any */
			if (frame->segment) {
			  pData = ov511->scratch;
			  iPix = - ov511->scratchlen;
			  memmove(pData + ov511->scratchlen, cdata, iPix+384);
		  } else {
			  pData = &cdata[iPix = 9];
		  }

			/* Parse the segments */
			while(iPix <= 992 - 384 &&
			      frame->segment < frame->width * frame->height / 256) {
			  ov511_parse_data(pData, frame->data,
					   frame->width,
					   frame->segment);
			  frame->segment++;
			  iPix += 384;
			  pData = &cdata[iPix];
		}

			/* Save extra data for next time */
			if (frame->segment < frame->width * frame->height / 256) {
			  memmove(ov511->scratch, pData, 992 - iPix);
			  ov511->scratchlen = 992 - iPix;
			}
		}
	}

#if 0
	PDEBUG("pn: %d %d %d %d %d %d %d %d %d %d\n",
	       aPackNum[0], aPackNum[1], aPackNum[2], aPackNum[3], aPackNum[4],
	       aPackNum[5],aPackNum[6], aPackNum[7], aPackNum[8], aPackNum[9]);
#endif
	return totlen;
}

static void ov511_isoc_irq(struct urb *urb)
{
	int len;
	struct usb_ov511 *ov511 = urb->context;
	struct ov511_sbuf *sbuf;
	int i;

	if (!ov511->streaming) {
		PDEBUG("hmmm... not streaming, but got interrupt\n");
		return;
	}
	
	sbuf = &ov511->sbuf[ov511->cursbuf];

	/* Copy the data received into our scratch buffer */
	if (ov511->curframe >= 0) {
	  len = ov511_move_data(ov511, urb);
	}

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

	ov511_set_packet_size(ov511, 993);

	/* We double buffer the Iso lists */
	urb = usb_alloc_urb(FRAMES_PER_DESC);
	
	if (!urb) {
		err("ov511_init_isoc: usb_alloc_urb ret. NULL");
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
		err("ov511_init_isoc: usb_alloc_urb ret. NULL");
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
		err("ov511_init_isoc: usb_run_isoc(0) ret %d", err);
	err = usb_submit_urb(ov511->sbuf[1].urb);
	if (err)
		err("ov511_init_isoc: usb_run_isoc(1) ret %d\n", err);

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

	PDEBUG("ov511_open");
	
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

	PDEBUG("frame [0] @ %p", ov511->frame[0].data);
	PDEBUG("frame [1] @ %p", ov511->frame[1].data);

	ov511->sbuf[0].data = kmalloc(FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!ov511->sbuf[0].data)
		goto open_err_on0;
	ov511->sbuf[1].data = kmalloc(FRAMES_PER_DESC * FRAME_SIZE_PER_DESC, GFP_KERNEL);
	if (!ov511->sbuf[1].data)
		goto open_err_on1;
		
	PDEBUG("sbuf[0] @ %p", ov511->sbuf[0].data);
	PDEBUG("sbuf[1] @ %p", ov511->sbuf[1].data);

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

	PDEBUG("ov511_close");
	
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
static int ov511_ioctl(struct video_device *vdev, unsigned int cmd, void *arg)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)vdev;
#if 0	
	PDEBUG("IOCtl: 0x%X", cmd);
#endif	
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

			if (ov7610_get_picture(ov511, &p))
				return -EIO;
							
			if (copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;

			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture p;

			if (copy_from_user(&p, arg, sizeof(p)))
				return -EFAULT;
			
			if (ov7610_set_picture(ov511, &p))
				return -EIO;

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

#if 0
			PDEBUG("MCAPTURE");
			PDEBUG("frame: %d, size: %dx%d, format: %d",
				vm.frame, vm.width, vm.height, vm.format);
#endif

			if (vm.format != VIDEO_PALETTE_RGB24)
				return -EINVAL;

			if ((vm.frame != 0) && (vm.frame != 1))
				return -EINVAL;
				
			if (ov511->frame[vm.frame].grabstate == FRAME_GRABBING)
				return -EBUSY;

			/* Don't compress if the size changed */
			if ((ov511->frame[vm.frame].width != vm.width) ||
			    (ov511->frame[vm.frame].height != vm.height)) {
				ov511->compress = 0;
				ov511_mode_init_regs(ov511,
						     vm.width, vm.height, 0);
#if 0
				PDEBUG("ov511: Setting frame %d to (%d, %d) : %d",
				       vm.frame, vm.width, vm.height, 0);
#endif
			}

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

#if 0
			PDEBUG("syncing to frame %d", frame);
#endif
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

	PDEBUG("ov511_read: %ld bytes, noblock=%d", count, noblock);

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
		interruptible_sleep_on(&ov511->frame[frmx].wq);
		if (signal_pending(current))
			return -EINTR;
	}

	if (frame->grabstate == FRAME_ERROR) {
		frame->bytes_read = 0;
		err("ov511_read: errored frame %d", ov511->curframe);
		if (ov511_new_frame(ov511, frmx))
			err("ov511_read: ov511_new_frame error");
		goto restart;
	}

	PDEBUG("ov511_read: frmx=%d, bytes_read=%ld, scanlength=%ld", frmx,
		frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count))
		return -EFAULT;

	frame->bytes_read += count;
	PDEBUG("ov511_read: {copy} count used=%ld, new bytes_read=%ld",
		count, frame->bytes_read);

	if (frame->bytes_read >= frame->scanlength) { /* All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		ov511->frame[frmx].grabstate = FRAME_UNUSED;
		if (ov511_new_frame(ov511, frmx ? 0 : 1))
			err("ov511_read: ov511_new_frame returned error");
	}

	return count;
}

static int ov511_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

	PDEBUG("mmap: %ld (%lX) bytes", size, size);

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

static int ov7610_configure(struct usb_device *dev)
{
	if(ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE,
	                        OV7610_I2C_WRITE_ID) < 0)
		return -1;

	if(ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ,
	                        OV7610_I2C_READ_ID) < 0)
		return -1;
	
	/* Reset the camera chip */
	if (ov511_i2c_reset(dev) < 0)
		return -1;

#if 0
	if(usb_ov511_reg_write(dev, OV511_REG_I2C_CLOCK_PRESCALER,
	                            OV511_I2C_CLOCK_PRESCALER))
		return -1;
#endif
	
	if (ov511_reset(dev, OV511_RESET_NOREGS) < 0)
		return -1;
	
	/* Dummy read to sync I2C */
	if(ov511_i2c_read(dev, 0x00) < 0)
		return -1;
	

	if((ov511_i2c_read(dev, OV7610_REG_ID_HIGH) != 0x7F) ||
	   (ov511_i2c_read(dev, OV7610_REG_ID_LOW) != 0xA2)) {
		err("Failed to read OV7610 ID. You might not have an OV7610,");
		err("or it may be not responding. Report this to");
		err("mmcclelland@delphi.com");
		return -1;
	}

	return 0;
}

static int ov511_configure(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;

	/* Set altsetting 0 */
	if (usb_set_interface(dev, ov511->iface, 0) < 0) {
		err("usb_set_interface error");
		return -EBUSY;
	}

	memcpy(&ov511->vdev, &ov511_template, sizeof(ov511_template));

	init_waitqueue_head(&ov511->frame[0].wq);
	init_waitqueue_head(&ov511->frame[1].wq);

	if (video_register_device(&ov511->vdev, VFL_TYPE_GRABBER) == -1) {
		err("video_register_device failed");
		return -EBUSY;
	}

	/* Reset in case driver was unloaded and reloaded without unplug */
	if (ov511_reset(dev, OV511_RESET_ALL) < 0)
		goto error;

	/* Initialize system */
	if (ov511_reg_write(dev, OV511_REG_SYSTEM_INIT, 0x01) < 0) {
		err("enable system: command failed");
		goto error;
	}

	/* This seems to be necessary */
	if (ov511_reset(dev, OV511_RESET_ALL) < 0)
		goto error;

	if(ov7610_configure(dev) < 0) {
		err("failed to configure OV7610");
 		goto error;	
	}

	/* Disable compression */
	if (ov511_reg_write(dev, OV511_OMNICE_ENABLE, 0x00) < 0) {
		err("disable compression: command failed");
		goto error;
	}

	ov511->compress = 0;
	
	/* Set default sizes in case IOCTL (VIDIOCMCAPTURE) is not used
	 * (using read() instead). */
	ov511->frame[0].width = DEFAULT_WIDTH;
	ov511->frame[0].height = DEFAULT_HEIGHT;
	ov511->frame[0].bytes_read = 0;
	ov511->frame[1].width = DEFAULT_WIDTH;
	ov511->frame[1].height = DEFAULT_HEIGHT;
	ov511->frame[1].bytes_read = 0;

	/* Initialize to DEFAULT_WIDTH, DEFAULT_HEIGHT, YUV4:2:0 */
	ov511_mode_init_regs(ov511, DEFAULT_WIDTH, DEFAULT_HEIGHT, 0);

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

	PDEBUG("probing for device...");

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
		err("couldn't kmalloc ov511 struct");
		return NULL;
	}

	memset(ov511, 0, sizeof(*ov511));
	
	ov511->dev = dev;
	ov511->iface = interface->bInterfaceNumber;

	rc = ov511_reg_read(dev, OV511_REG_SYSTEM_CUSTOM_ID);
	if (rc < 0) {
		err("Unable to read camera bridge registers");
		return NULL;
	}
	
	switch(ov511->customid = rc) {
	case 0: /* This also means that no custom ID was set */
		printk("ov511: Camera is probably a MediaForte MV300\n");
		break;
	case 3:
		printk("ov511: Camera is a D-Link DSB-C300\n");
		break;
	case 21:
		printk("ov511: Camera is a Creative Labs WebCam 3\n");
		break;
	case 100:
		printk("ov511: Camera is a Lifeview RoboCam\n");
		break;
	case 102:
		printk("ov511: Camera is a AverMedia InterCam Elite\n");
		break;
	default:
		err("Specific camera type (%d) not recognized", rc);
		err("Please contact mmcclelland@delphi.com to request");
		err("support for your camera.");
		return NULL;
	}

	if (!ov511_configure(ov511)) {
		ov511->user=0;
		init_MUTEX(&ov511->lock);	/* to 1 == available */
		return ov511;
	}
	else {
		err("Failed to configure camera");
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
	PDEBUG("usb_ov511_init()");
	
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
	
	PDEBUG("Module unloaded");
}
#endif

