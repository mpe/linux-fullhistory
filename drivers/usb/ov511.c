/*
 * OmniVision OV511 Camera-to-USB Bridge Driver
 *
 * Copyright (c) 1999-2000 Mark W. McClelland
 * Many improvements by Bret Wallach
 * Color fixes by by Orion Sky Lawlor, olawlor@acm.org, 2/26/2000
 * Snapshot code by Kevin Moore
 * OV7620 fixes by Charl P. Botha <cpbotha@ieee.org>
 * Changes by Claudio Matsuoka <claudio@conectiva.com>
 * 
 * Based on the Linux CPiA driver written by Peter Pregler,
 * Scott J. Bertin and Johannes Erdfelt.
 * 
 * Please see the file: linux/Documentation/usb/ov511.txt 
 * and the website at:  http://alpha.dyndns.org/ov511
 * for more info.
 *
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

static const char version[] = "1.14";

#define __NO_VERSION__

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/pagemap.h>
#include <linux/usb.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <linux/wrapper.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include "ov511.h"

#define OV511_I2C_RETRIES 3

/* Video Size 640 x 480 x 3 bytes for RGB */
#define MAX_FRAME_SIZE (640 * 480 * 3)
#define MAX_DATA_SIZE (MAX_FRAME_SIZE + sizeof(struct timeval))

#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480

#define GET_SEGSIZE(p) ((p) == VIDEO_PALETTE_RGB24 ? 384 : 256)
#define GET_DEPTH(p) ((p) == VIDEO_PALETTE_RGB24 ? 24 : 8)

/* PARAMETER VARIABLES: */
static int autoadjust = 1;    /* CCD dynamically changes exposure, etc... */

/* 0=no debug messages
 * 1=init/detection/unload and other significant messages,
 * 2=some warning messages
 * 3=config/control function calls
 * 4=most function calls and data parsing messages
 * 5=highly repetitive mesgs
 * NOTE: This should be changed to 0, 1, or 2 for production kernels
 */
static int debug = 3;

/* Fix vertical misalignment of red and blue at 640x480 */
static int fix_rgb_offset = 0;

/* Snapshot mode enabled flag */
static int snapshot = 0;

/* Sensor detection override (global for all attached cameras) */
static int sensor = 0;

/* Increase this if you are getting "Failed to read sensor ID..." */
static int i2c_detect_tries = 5;

/* For legal values, see the OV7610/7620 specs under register Common F,
 * upper nybble  (set to 0-F) */
static int aperture = -1;

/* Force image to be read in RGB instead of BGR. This option allow
 * programs that expect RGB data (e.g. gqcam) to work with this driver. */
static int force_rgb = 0;

MODULE_PARM(autoadjust, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(fix_rgb_offset, "i");
MODULE_PARM(snapshot, "i");
MODULE_PARM(sensor, "i");
MODULE_PARM(i2c_detect_tries, "i");
MODULE_PARM(aperture, "i");
MODULE_PARM(force_rgb, "i");

MODULE_AUTHOR("Mark McClelland <mmcclelland@delphi.com> & Bret Wallach & Orion Sky Lawlor <olawlor@acm.org> & Kevin Moore & Charl P. Botha <cpbotha@ieee.org> & Claudio Matsuoka <claudio@conectiva.com>");
MODULE_DESCRIPTION("OV511 USB Camera Driver");

char kernel_version[] = UTS_RELEASE;

static struct usb_driver ov511_driver;

/**********************************************************************
 * List of known OV511-based cameras
 **********************************************************************/

static struct cam_list clist[] = {
	{   0, "generic model (no ID)" },
	{   3, "D-Link DSB-C300" },
	{   4, "generic OV511/OV7610" },
	{   5, "Puretek PT-6007" },
	{  21, "Creative Labs WebCam 3" },
	{  36, "Koala-Cam" },
	{  38, "Lifeview USB Life TV" },	/* No support yet! */
	{ 100, "Lifeview RoboCam" },
	{ 102, "AverMedia InterCam Elite" },
	{ 112, "MediaForte MV300" },	/* or OV7110 evaluation kit */
	{  -1, NULL }
};

static struct palette_list plist[] = {
	{ VIDEO_PALETTE_GREY,	"GREY" },
	{ VIDEO_PALETTE_HI240,  "HI240" },
	{ VIDEO_PALETTE_RGB565, "RGB565" },
	{ VIDEO_PALETTE_RGB24,	"RGB24" },
	{ VIDEO_PALETTE_RGB32,	"RGB32" },
	{ VIDEO_PALETTE_RGB555, "RGB555" },
	{ VIDEO_PALETTE_YUV422, "YUV422" },
	{ VIDEO_PALETTE_YUYV,   "YUYV" },
	{ VIDEO_PALETTE_UYVY,   "UYVY" },
	{ VIDEO_PALETTE_YUV420, "YUV420" },
	{ VIDEO_PALETTE_YUV411, "YUV411" },
	{ VIDEO_PALETTE_RAW,    "RAW" },
	{ VIDEO_PALETTE_YUV422P,"YUV422P" },
	{ VIDEO_PALETTE_YUV411P,"YUV411P" },
	{ VIDEO_PALETTE_YUV420P,"YUV420P" },
	{ VIDEO_PALETTE_YUV410P,"YUV410P" },
	{ -1, NULL }
};

/**********************************************************************
 *
 * Memory management
 *
 * This is a shameless copy from the USB-cpia driver (linux kernel
 * version 2.3.29 or so, I have no idea what this code actually does ;).
 * Actually it seems to be a copy of a shameless copy of the bttv-driver.
 * Or that is a copy of a shameless copy of ... (To the powers: is there
 * no generic kernel-function to do this sort of stuff?)
 *
 * Yes, it was a shameless copy from the bttv-driver. IIRC, Alan says
 * there will be one, but apparentely not yet -jerdfelt
 *
 * So I copied it again for the OV511 driver -claudio
 **********************************************************************/

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
				ret = page_address(pte_page(pte)) |
				      (adr & (PAGE_SIZE-1));
		}
	}

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

/**********************************************************************
 * /proc interface
 * Based on the CPiA driver version 0.7.4 -claudio
 **********************************************************************/

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *ov511_proc_entry = NULL;
static struct proc_dir_entry *video_proc_entry = NULL;

#define YES_NO(x) ((x) ? "yes" : "no")

static int ov511_read_proc(char *page, char **start, off_t off,
                          int count, int *eof, void *data)
{
	char *out = page;
	int i, j, len;
	struct usb_ov511 *ov511 = data;

	/* IMPORTANT: This output MUST be kept under PAGE_SIZE
	 *            or we need to get more sophisticated. */

	out += sprintf (out, "custom_id       : %d\n", ov511->customid);
	out += sprintf (out, "model           : %s\n", ov511->desc ?
		clist[ov511->desc].description : "unknown");
	out += sprintf (out, "streaming       : %s\n", YES_NO (ov511->streaming));
	out += sprintf (out, "grabbing        : %s\n", YES_NO (ov511->grabbing));
	out += sprintf (out, "compress        : %s\n", YES_NO (ov511->compress));
	out += sprintf (out, "subcapture      : %s\n", YES_NO (ov511->sub_flag));
	out += sprintf (out, "sub_size        : %d %d %d %d\n",
		ov511->subx, ov511->suby, ov511->subw, ov511->subh);
	out += sprintf (out, "data_format     : %s\n", force_rgb ? "RGB" : "BGR");
	out += sprintf (out, "brightness      : %d\n", ov511->brightness >> 8);
	out += sprintf (out, "colour          : %d\n", ov511->colour >> 8);
	out += sprintf (out, "contrast        : %d\n", ov511->contrast >> 8);
	out += sprintf (out, "num_frames      : %d\n", OV511_NUMFRAMES);
	for (i = 0; i < OV511_NUMFRAMES; i++) {
		out += sprintf (out, "frame           : %d\n", i);
		out += sprintf (out, "  depth         : %d\n",
			ov511->frame[i].depth);
		out += sprintf (out, "  size          : %d %d\n",
			ov511->frame[i].width, ov511->frame[i].height);
#if 0
		out += sprintf (out, "  hdr_size      : %d %d\n",
			ov511->frame[i].hdrwidth, ov511->frame[i].hdrheight);
#endif
		out += sprintf (out, "  format        : ");
		for (j = 0; plist[j].num >= 0; j++) {
			if (plist[j].num == ov511->frame[i].format) {
				out += sprintf (out, "%s\n", plist[j].name);
				break;
			}
		}
		if (plist[j].num < 0)
			out += sprintf (out, "unknown\n");
		out += sprintf (out, "  segsize       : %d\n",
			ov511->frame[i].segsize);
		out += sprintf (out, "  data_buffer   : 0x%p\n",
			ov511->frame[i].data);
#if 0
		out += sprintf (out, "  bytesread     : %ld\n",
			ov511->frame[i].bytes_read);
#endif
	}
	out += sprintf (out, "snap_enabled    : %s\n", YES_NO (ov511->snap_enabled));
	out += sprintf (out, "bridge          : %s\n",
		ov511->bridge == BRG_OV511 ? "OV511" :
		ov511->bridge == BRG_OV511PLUS ? "OV511+" :
		"unknown");
	out += sprintf (out, "sensor          : %s\n",
		ov511->sensor == SEN_OV7610 ? "OV7610" :
		ov511->sensor == SEN_OV7620 ? "OV7620" :
		ov511->sensor == SEN_OV7620AE ? "OV7620AE" :
		"unknown");
	out += sprintf (out, "packet_size     : %d\n", ov511->packet_size);
	out += sprintf (out, "framebuffer     : 0x%p\n", ov511->fbuf);
	
	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0) return 0;
	} else
		len = count;

	*start = page + off;
	return len;
}

static int ov511_write_proc(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
	return -EINVAL;
}

static void create_proc_ov511_cam (struct usb_ov511 *ov511)
{
	char name[7];
	struct proc_dir_entry *ent;
	
	PDEBUG (4, "creating /proc/video/ov511/videoX entry");
	if (!ov511_proc_entry || !ov511)
		return;

	sprintf(name, "video%d", ov511->vdev.minor);
	PDEBUG (4, "creating %s", name);
	
	ent = create_proc_entry(name, S_IFREG|S_IRUGO|S_IWUSR, ov511_proc_entry);

	if (!ent)
		return;

	ent->data = ov511;
	ent->read_proc = ov511_read_proc;
	ent->write_proc = ov511_write_proc;
	ent->size = 3626;	/* FIXME */
	ov511->proc_entry = ent;
}

static void destroy_proc_ov511_cam (struct usb_ov511 *ov511)
{
	char name[7];
	
	if (!ov511 || !ov511->proc_entry)
		return;
	
	sprintf(name, "video%d", ov511->vdev.minor);
	PDEBUG (4, "destroying %s", name);
	remove_proc_entry(name, ov511_proc_entry);
	ov511->proc_entry = NULL;
}

static void proc_ov511_create(void)
{
	struct proc_dir_entry *p = NULL;

	/* No current standard here. Alan prefers /proc/video/ as it keeps
	 * /proc "less cluttered than /proc/randomcardifoundintheshed/"
	 * -claudio
	 */
	PDEBUG (3, "creating /proc/video");
	video_proc_entry = proc_mkdir("video", p);
	if (!video_proc_entry) {
		if (!p) {
			err("Unable to initialise /proc/video\n");
			return;
		} else {	/* FIXME - this doesn't work */
			PDEBUG (3, "/proc/video already exists");
			video_proc_entry = p;
		}
	}

	ov511_proc_entry = create_proc_entry("ov511", S_IFDIR, video_proc_entry);

	if (ov511_proc_entry)
		ov511_proc_entry->owner = THIS_MODULE;
	else
		err("Unable to initialise /proc/video/ov511\n");
}

static void proc_ov511_destroy(void)
{
	PDEBUG (3, "removing /proc/video/ov511");
	remove_proc_entry("ov511", video_proc_entry);
	remove_proc_entry("video", NULL);
}
#endif /* CONFIG_PROC_FS */


/**********************************************************************
 *
 * Camera interface
 *
 **********************************************************************/

static int ov511_reg_write(struct usb_device *dev,
			   unsigned char reg,
			   unsigned char value)
{
	int rc;

	rc = usb_control_msg(dev,
		usb_sndctrlpipe(dev, 0),
		2 /* REG_IO */,
		USB_TYPE_CLASS | USB_RECIP_DEVICE,
		0, (__u16)reg, &value, 1, HZ);	

	PDEBUG(5, "reg write: 0x%02X:0x%02X, 0x%x", reg, value, rc);

	if (rc < 0)
		err("ov511_reg_write: error %d", rc);

	return rc;
}

/* returns: negative is error, pos or zero is data */
static int ov511_reg_read(struct usb_device *dev, unsigned char reg)
{
	int rc;
	unsigned char buffer[1];

	rc = usb_control_msg(dev,
		usb_rcvctrlpipe(dev, 0),
		2 /* REG_IO */,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE,
		0, (__u16)reg, buffer, 1, HZ);
                               
	PDEBUG(5, "reg read: 0x%02X:0x%02X", reg, buffer[0]);
	
	if (rc < 0) {
		err("ov511_reg_read: error %d", rc);
		return rc;
	} else {
		return buffer[0];	
	}
}

static int ov511_i2c_write(struct usb_device *dev,
			   unsigned char reg,
			   unsigned char value)
{
	int rc, retries;

	PDEBUG(5, "i2c write: 0x%02X:0x%02X", reg, value);

	/* Three byte write cycle */
	for (retries = OV511_I2C_RETRIES; ; ) {
		/* Select camera register */
		rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_3_BYTE, reg);
		if (rc < 0) goto error;

		/* Write "value" to I2C data port of OV511 */
		rc = ov511_reg_write(dev, OV511_REG_I2C_DATA_PORT, value);	
		if (rc < 0) goto error;

		/* Initiate 3-byte write cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x01);
		if (rc < 0) goto error;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while (rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) goto error;

		if ((rc&2) == 0) /* Ack? */
			break;
#if 0
		/* I2C abort */	
		ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);
#endif
		if (--retries < 0) {
			err("i2c write retries exhausted");
			rc = -1;
			goto error;
		}
	}

	return 0;

error:
	err("ov511_i2c_write: error %d", rc);
	return rc;
}

/* returns: negative is error, pos or zero is data */
static int ov511_i2c_read(struct usb_device *dev, unsigned char reg)
{
	int rc, value, retries;

	/* Two byte write cycle */
	for (retries = OV511_I2C_RETRIES; ; ) {
		/* Select camera register */
		rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_2_BYTE, reg);
		if (rc < 0) goto error;

		/* Initiate 2-byte write cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x03);
		if (rc < 0) goto error;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while (rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) goto error;

		if ((rc&2) == 0) /* Ack? */
			break;

		/* I2C abort */	
		ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);

		if (--retries < 0) {
			err("i2c write retries exhausted");
			rc = -1;
			goto error;
		}
	}

	/* Two byte read cycle */
	for (retries = OV511_I2C_RETRIES; ; ) {
		/* Initiate 2-byte read cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x05);
		if (rc < 0) goto error;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while (rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) goto error;

		if ((rc&2) == 0) /* Ack? */
			break;

		/* I2C abort */	
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);
		if (rc < 0) goto error;

		if (--retries < 0) {
			err("i2c read retries exhausted");
			rc = -1;
			goto error;
		}
	}

	value = ov511_reg_read(dev, OV511_REG_I2C_DATA_PORT);

	PDEBUG(5, "i2c read: 0x%02X:0x%02X", reg, value);
		
	/* This is needed to make ov511_i2c_write() work */
	rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x05);
	if (rc < 0)
		goto error;
	
	return value;

error:
	err("ov511_i2c_read: error %d", rc);
	return rc;
}

static int ov511_write_regvals(struct usb_device *dev,
			       struct ov511_regvals * pRegvals)
{
	int rc;

	while (pRegvals->bus != OV511_DONE_BUS) {
		if (pRegvals->bus == OV511_REG_BUS) {
			if ((rc = ov511_reg_write(dev, pRegvals->reg,
			                           pRegvals->val)) < 0)
				goto error;
		} else if (pRegvals->bus == OV511_I2C_BUS) {
			if ((rc = ov511_i2c_write(dev, pRegvals->reg, 
			                           pRegvals->val)) < 0)
				goto error;
		} else {
			err("Bad regval array");
			rc = -1;
			goto error;
		}
		pRegvals++;
	}
	return 0;

error:
	err("ov511_write_regvals: error %d", rc);
	return rc;
}

#ifdef OV511_DEBUG 
static void ov511_dump_i2c_range(struct usb_device *dev, int reg1, int regn)
{
	int i;
	int rc;
	for(i = reg1; i <= regn; i++) {
		rc = ov511_i2c_read(dev, i);
		PDEBUG(1, "OV7610[0x%X] = 0x%X", i, rc);
	}
}

static void ov511_dump_i2c_regs(struct usb_device *dev)
{
	PDEBUG(3, "I2C REGS");
	ov511_dump_i2c_range(dev, 0x00, 0x38);
}

static void ov511_dump_reg_range(struct usb_device *dev, int reg1, int regn)
{
	int i;
	int rc;
	for(i = reg1; i <= regn; i++) {
	  rc = ov511_reg_read(dev, i);
	  PDEBUG(1, "OV511[0x%X] = 0x%X", i, rc);
	}
}

#if 0
static void ov511_dump_regs(struct usb_device *dev)
{
	PDEBUG(1, "CAMERA INTERFACE REGS");
	ov511_dump_reg_range(dev, 0x10, 0x1f);
	PDEBUG(1, "DRAM INTERFACE REGS");
	ov511_dump_reg_range(dev, 0x20, 0x23);
	PDEBUG(1, "ISO FIFO REGS");
	ov511_dump_reg_range(dev, 0x30, 0x31);
	PDEBUG(1, "PIO REGS");
	ov511_dump_reg_range(dev, 0x38, 0x39);
	ov511_dump_reg_range(dev, 0x3e, 0x3e);
	PDEBUG(1, "I2C REGS");
	ov511_dump_reg_range(dev, 0x40, 0x49);
	PDEBUG(1, "SYSTEM CONTROL REGS");
	ov511_dump_reg_range(dev, 0x50, 0x53);
	ov511_dump_reg_range(dev, 0x5e, 0x5f);
	PDEBUG(1, "OmniCE REGS");
	ov511_dump_reg_range(dev, 0x70, 0x79);
	ov511_dump_reg_range(dev, 0x80, 0x9f);
	ov511_dump_reg_range(dev, 0xa0, 0xbf);

}
#endif
#endif

static int ov511_reset(struct usb_device *dev, unsigned char reset_type)
{
	int rc;
	
	PDEBUG(3, "Reset: type=0x%X", reset_type);
	rc = ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, reset_type);
	rc = ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, 0);

	if (rc < 0)
		err("reset: command failed");

	return rc;
}

/* Temporarily stops OV511 from functioning. Must do this before changing
 * registers while the camera is streaming */
static inline int ov511_stop(struct usb_device *dev)
{
	PDEBUG(4, "stopping");
	return (ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, 0x3d));
}

/* Restarts OV511 after ov511_stop() is called */
static inline int ov511_restart(struct usb_device *dev)
{
	PDEBUG(4, "restarting");
	return (ov511_reg_write(dev, OV511_REG_SYSTEM_RESET, 0x00));
}

static int ov511_set_packet_size(struct usb_ov511 *ov511, int size)
{
	int alt, mult;

	if (ov511_stop(ov511->dev) < 0)
		return -EIO;

	mult = size >> 5;

	if (ov511->bridge == BRG_OV511) {
		if (size == 0) alt = OV511_ALT_SIZE_0;
		else if (size == 257) alt = OV511_ALT_SIZE_257;
//		else if (size == 512) alt = OV511_ALT_SIZE_512;
		else if (size == 513) alt = OV511_ALT_SIZE_513;
//		else if (size == 768) alt = OV511_ALT_SIZE_768;
		else if (size == 769) alt = OV511_ALT_SIZE_769;
//		else if (size == 992) alt = OV511_ALT_SIZE_992;
		else if (size == 993) alt = OV511_ALT_SIZE_993;
		else {
			err("Set packet size: invalid size (%d)", size);
			return -EINVAL;
		}
	} else if (ov511->bridge == BRG_OV511PLUS) {
		if (size == 0) alt = OV511PLUS_ALT_SIZE_0;
		else if (size == 33) alt = OV511PLUS_ALT_SIZE_33;
		else if (size == 129) alt = OV511PLUS_ALT_SIZE_129;
		else if (size == 257) alt = OV511PLUS_ALT_SIZE_257;
		else if (size == 385) alt = OV511PLUS_ALT_SIZE_385;
		else if (size == 513) alt = OV511PLUS_ALT_SIZE_513;
		else if (size == 769) alt = OV511PLUS_ALT_SIZE_769;
		else if (size == 961) alt = OV511PLUS_ALT_SIZE_961;
		else {
			err("Set packet size: invalid size (%d)", size);
			return -EINVAL;
		}
	} else {
		err("Set packet size: Invalid bridge type");
		return -EINVAL;
	}

	PDEBUG(3, "set packet size: %d, mult=%d, alt=%d", size, mult, alt);

	if (ov511_reg_write(ov511->dev, OV511_REG_FIFO_PACKET_SIZE, mult) < 0)
		return -ENOMEM;
	
	if (usb_set_interface(ov511->dev, ov511->iface, alt) < 0) {
		err("Set packet size: set interface error");
		return -EBUSY;
	}

	// FIXME - Should we only reset the FIFO?
	if (ov511_reset(ov511->dev, OV511_RESET_NOREGS) < 0)
		return -ENOMEM;

	ov511->packet_size = size;

	if (ov511_restart(ov511->dev) < 0)
		return -EIO;

	return 0;
}


static inline int
ov7610_set_picture(struct usb_ov511 *ov511, struct video_picture *p)
{
	int ret;
	struct usb_device *dev = ov511->dev;

	PDEBUG(4, "ov511_set_picture");

	if (ov511_stop(dev) < 0)
		return -EIO;

	ov511->contrast = p->contrast;
	ov511->brightness = p->brightness;
	ov511->colour = p->colour;
	ov511->hue = p->hue;
	ov511->whiteness = p->whiteness;

	if ((ret = ov511_i2c_read(dev, OV7610_REG_COM_B)) < 0)
		return -EIO;
#if 0
	/* disable auto adjust mode */
	if (ov511_i2c_write(dev, OV7610_REG_COM_B, ret & 0xfe) < 0)
		return -EIO;
#endif
	if (ov511->sensor == SEN_OV7610 || ov511->sensor == SEN_OV7620AE)
		if(ov511_i2c_write(dev, OV7610_REG_SAT, p->colour >> 8) < 0)
			return -EIO;

	if (ov511->sensor == SEN_OV7610) {
		if(ov511_i2c_write(dev, OV7610_REG_CNT, p->contrast >> 8) < 0)
			return -EIO;

		if(ov511_i2c_write(dev, OV7610_REG_BRT, p->brightness >> 8) < 0)
			return -EIO;
	} else if ((ov511->sensor == SEN_OV7620) 
	         || (ov511->sensor == SEN_OV7620AE)) {
#if 0
		cur_con = ov511_i2c_read(dev, OV7610_REG_CNT);
		cur_brt = ov511_i2c_read(dev, OV7610_REG_BRT);
	        // DEBUG_CODE
	        PDEBUG(1, "con=%d brt=%d", ov511_i2c_read(dev, OV7610_REG_CNT),
	         ov511_i2c_read(dev, OV7610_REG_BRT)); 

		if(ov511_i2c_write(dev, OV7610_REG_CNT, p->contrast >> 8) < 0)
			return -EIO;
#endif
	}

	if (ov511_restart(dev) < 0)
		return -EIO;

	return 0;
}

static inline int
ov7610_get_picture(struct usb_ov511 *ov511, struct video_picture *p)
{
	int ret;
	struct usb_device *dev = ov511->dev;

	PDEBUG(4, "ov511_get_picture");

	if (ov511_stop(dev) < 0)
		return -EIO;

	if((ret = ov511_i2c_read(dev, OV7610_REG_SAT)) < 0) return -EIO;
	p->colour = ret << 8;

	if((ret = ov511_i2c_read(dev, OV7610_REG_CNT)) < 0) return -EIO;
	p->contrast = ret << 8;

	if((ret = ov511_i2c_read(dev, OV7610_REG_BRT)) < 0) return -EIO;
	p->brightness = ret << 8;

	p->hue = 0x8000;
	p->whiteness = 105 << 8;

	/* Can we get these from frame[0]? -claudio? */
	p->depth = ov511->frame[0].depth;
	p->palette = ov511->frame[0].format;

	if (ov511_restart(dev) < 0)
		return -EIO;

	return 0;
}

/* FIXME: add 176x144, 160x140 */
static struct mode_list mlist[] = {
	{ 640, 480, VIDEO_PALETTE_GREY, 0x4f, 0x3d, 0x00, 0x00,
	  0x4f, 0x3d, 0x00, 0x00, 0x04, 0x03, 0x24, 0x04, 0x9e },
	{ 640, 480, VIDEO_PALETTE_RGB24,0x4f, 0x3d, 0x00, 0x00,
	  0x4f, 0x3d, 0x00, 0x00, 0x06, 0x03, 0x24, 0x04, 0x9e },
	{ 320, 240, VIDEO_PALETTE_GREY, 0x27, 0x1f, 0x00, 0x00,
	  0x27, 0x1f, 0x00, 0x00, 0x01, 0x03, 0x04, 0x24, 0x1e },
	{ 320, 240, VIDEO_PALETTE_RGB24,0x27, 0x1f, 0x00, 0x00,
	  0x27, 0x1f, 0x00, 0x00, 0x01, 0x03, 0x04, 0x24, 0x1e },
	{ 352, 288, VIDEO_PALETTE_GREY, 0x2b, 0x25, 0x00, 0x00,
	  0x2b, 0x25, 0x00, 0x00, 0x01, 0x03, 0x04, 0x04, 0x1e },
	{ 352, 288, VIDEO_PALETTE_RGB24,0x2b, 0x25, 0x00, 0x00,
	  0x2b, 0x25, 0x00, 0x00, 0x01, 0x03, 0x04, 0x04, 0x1e },
	{ 0, 0 }
};

static int
ov511_mode_init_regs(struct usb_ov511 *ov511,
		     int width, int height, int mode, int sub_flag)
{
	int rc = 0;
	struct usb_device *dev = ov511->dev;
	int hwsbase = 0;
	int hwebase = 0;
	int i;

	PDEBUG(3, "width:%d, height:%d, mode:%d, sub:%d",
	       width, height, mode, sub_flag);

	if (ov511_stop(ov511->dev) < 0)
		return -EIO;

	if (mode == VIDEO_PALETTE_GREY) {
		ov511_reg_write(dev, 0x16, 0x00);
		if (ov511->sensor == SEN_OV7610
		    || ov511->sensor == SEN_OV7620AE) {
			/* these aren't valid on the OV7620 */
			ov511_i2c_write(dev, 0x0e, 0x44);
		}
		ov511_i2c_write(dev, 0x13, autoadjust ? 0x21 : 0x20);

		/* For snapshot */
		ov511_reg_write(dev, 0x1e, 0x00);
		ov511_reg_write(dev, 0x1f, 0x01);
	} else {
		ov511_reg_write(dev, 0x16, 0x01);
		if (ov511->sensor == SEN_OV7610
		    || ov511->sensor == SEN_OV7620AE) {
			/* not valid on the OV7620 */
			ov511_i2c_write(dev, 0x0e, 0x04);
		}
		ov511_i2c_write(dev, 0x13, autoadjust ? 0x01 : 0x00);

		/* For snapshot */
		ov511_reg_write(dev, 0x1e, 0x01);
		ov511_reg_write(dev, 0x1f, 0x03);
	}

	/* The different sensor ICs handle setting up of window differently */
	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV7620AE:
		hwsbase = 0x38;
		hwebase = 0x3a;
		break;
	case SEN_OV7620:
		hwsbase = 0x2c;
		hwebase = 0x2d;
		break;
	default:
		hwsbase = 0;
		hwebase = 0;
		break;
	}

#if 0
	/* FIXME: subwindow support is currently broken! 
	 */
	if (width == 640 && height == 480) {
		if (sub_flag) {
			/* horizontal window start */
			ov511_i2c_write(dev, 0x17, hwsbase+(ov511->subx>>2));
			/* horizontal window end */
			ov511_i2c_write(dev, 0x18,
				hwebase+((ov511->subx+ov511->subw)>>2));
			/* vertical window start */
			ov511_i2c_write(dev, 0x19, 0x5+(ov511->suby>>1));
			/* vertical window end */
			ov511_i2c_write(dev, 0x1a,
				0x5+((ov511->suby+ov511->subh)>>1));
			ov511_reg_write(dev, 0x12, (ov511->subw>>3)-1);
			ov511_reg_write(dev, 0x13, (ov511->subh>>3)-1);
			/* clock rate control */
			ov511_i2c_write(dev, 0x11, 0x01);

			/* Snapshot additions */
			ov511_reg_write(dev, 0x1a, (ov511->subw>>3)-1);
			ov511_reg_write(dev, 0x1b, (ov511->subh>>3)-1);
			ov511_reg_write(dev, 0x1c, 0x00);
			ov511_reg_write(dev, 0x1d, 0x00);
		} else {
			ov511_i2c_write(dev, 0x17, hwsbase);
			ov511_i2c_write(dev, 0x18, hwebase + (640>>2));
			ov511_i2c_write(dev, 0x19, 0x5);
			ov511_i2c_write(dev, 0x1a, 5 + (480>>1));
			ov511_reg_write(dev, 0x12, 0x4f);
			ov511_reg_write(dev, 0x13, 0x3d);

			/* Snapshot additions */
			ov511_reg_write(dev, 0x1a, 0x4f);
			ov511_reg_write(dev, 0x1b, 0x3d);
			ov511_reg_write(dev, 0x1c, 0x00);
			ov511_reg_write(dev, 0x1d, 0x00);

			if (mode == VIDEO_PALETTE_GREY) {
				ov511_i2c_write(dev, 0x11, 4); /* check */
			} else {
				ov511_i2c_write(dev, 0x11, 6); /* check */
			}
		}

		ov511_reg_write(dev, 0x14, 0x00);	/* Pixel divisor */
		ov511_reg_write(dev, 0x15, 0x00);	/* Line divisor */

		/* FIXME?? Shouldn't below be true only for YUV420? */
		ov511_reg_write(dev, 0x18, 0x03);	/* YUV420/422, YFIR */

		ov511_i2c_write(dev, 0x12, 0x24);	/* Common A */
		ov511_i2c_write(dev, 0x14, 0x04);	/* Common C */

		/* 7620 doesn't have register 0x35, so play it safe */
		if (ov511->sensor != SEN_OV7620)
			ov511_i2c_write(dev, 0x35, 0x9e);
#endif

	for (i = 0; mlist[i].width; i++) {
		if (width != mlist[i].width ||
		    height != mlist[i].height ||
		    mode != mlist[i].mode)
			continue;

		ov511_reg_write(dev, 0x12, mlist[i].pxcnt);
		ov511_reg_write(dev, 0x13, mlist[i].lncnt);
		ov511_reg_write(dev, 0x14, mlist[i].pxdv);
		ov511_reg_write(dev, 0x15, mlist[i].lndv);
		ov511_reg_write(dev, 0x18, mlist[i].m420);

		/* Snapshot additions */
		ov511_reg_write(dev, 0x1a, mlist[i].s_pxcnt);
		ov511_reg_write(dev, 0x1b, mlist[i].s_lncnt);
                ov511_reg_write(dev, 0x1c, mlist[i].s_pxdv);
                ov511_reg_write(dev, 0x1d, mlist[i].s_lndv);

		ov511_i2c_write(dev, 0x11, mlist[i].clock); /* check */

		ov511_i2c_write(dev, 0x12, mlist[i].common_A);
		ov511_i2c_write(dev, 0x14, mlist[i].common_C);

		/* 7620 doesn't have register 0x35, so play it safe */
		if (ov511->sensor != SEN_OV7620)
			ov511_i2c_write(dev, 0x35, mlist[i].common_L);

		break;
	}

	if (mlist[i].width == 0) {
		err("Unknown mode (%d, %d): %d", width, height, mode);
		rc = -EINVAL;
	}

	if (ov511_restart(ov511->dev) < 0)
		return -EIO;

#ifdef OV511_DEBUG
	if (debug >= 5)
		ov511_dump_i2c_regs(dev);
#endif

	return rc;
}

/**********************************************************************
 *
 * Color correction functions
 *
 **********************************************************************/

/*
 * Turn a YUV4:2:0 block into an RGB block
 *
 * Video4Linux seems to use the blue, green, red channel
 * order convention-- rgb[0] is blue, rgb[1] is green, rgb[2] is red.
 *
 * Color space conversion coefficients taken from the excellent
 * http://www.inforamp.net/~poynton/ColorFAQ.html
 * In his terminology, this is a CCIR 601.1 YCbCr -> RGB.
 * Y values are given for all 4 pixels, but the U (Pb)
 * and V (Pr) are assumed constant over the 2x2 block.
 *
 * To avoid floating point arithmetic, the color conversion
 * coefficients are scaled into 16.16 fixed-point integers.
 */

/* LIMIT: convert a 16.16 fixed-point value to a byte, with clipping. */
#define LIMIT(x) ((x)>0xffffff?0xff: ((x)<=0xffff?0:((x)>>16)))

static inline void
ov511_move_420_block(int yTL, int yTR, int yBL, int yBR, int u, int v, 
	int rowPixels, unsigned char * rgb)
{
	const double brightness = 1.0; /* 0->black; 1->full scale */
	const double saturation = 1.0; /* 0->greyscale; 1->full color */
	const double fixScale = brightness * 256 * 256;
	const int rvScale = (int)(1.402 * saturation * fixScale);
	const int guScale = (int)(-0.344136 * saturation * fixScale);
	const int gvScale = (int)(-0.714136 * saturation * fixScale);
	const int buScale = (int)(1.772 * saturation * fixScale);
	const int yScale = (int)(fixScale);	
	int r, g, b;

	g = guScale * u + gvScale * v;
	if (force_rgb) {
		r = buScale * u;
		b = rvScale * v;
	} else {
		r = rvScale * v;
		b = buScale * u;
	}

	yTL *= yScale; yTR *= yScale;
	yBL *= yScale; yBR *= yScale;

	/* Write out top two pixels */
	rgb[0] = LIMIT(b+yTL); rgb[1] = LIMIT(g+yTL); rgb[2] = LIMIT(r+yTL);
	rgb[3] = LIMIT(b+yTR); rgb[4] = LIMIT(g+yTR); rgb[5] = LIMIT(r+yTR);

	/* Skip down to next line to write out bottom two pixels */
	rgb += 3 * rowPixels;
	rgb[0] = LIMIT(b+yBL); rgb[1] = LIMIT(g+yBL); rgb[2] = LIMIT(r+yBL);
	rgb[3] = LIMIT(b+yBR); rgb[4] = LIMIT(g+yBR); rgb[5] = LIMIT(r+yBR);
}

/*
 * For a 640x480 YUV4:2:0 images, data shows up in 1200 384 byte segments.
 * The first 64 bytes of each segment are U, the next 64 are V.  The U and
 * V are arranged as follows:
 *
 *      0  1 ...  7
 *      8  9 ... 15
 *           ...   
 *     56 57 ... 63
 *
 * U and V are shipped at half resolution (1 U,V sample -> one 2x2 block).
 *
 * The next 256 bytes are full resolution Y data and represent 4 squares
 * of 8x8 pixels as follows:
 *
 *      0  1 ...  7    64  65 ...  71   ...  192 193 ... 199
 *      8  9 ... 15    72  73 ...  79        200 201 ... 207
 *           ...              ...                    ...
 *     56 57 ... 63   120 121     127        248 249 ... 255
 *
 * Note that the U and V data in one segment represents a 16 x 16 pixel
 * area, but the Y data represents a 32 x 8 pixel area.
 *
 * If OV511_DUMPPIX is defined, _parse_data just dumps the incoming segments,
 * verbatim, in order, into the frame. When used with vidcat -f ppm -s 640x480
 * this puts the data on the standard output and can be analyzed with the
 * parseppm.c utility I wrote.  That's a much faster way for figuring out how
 * this data is scrambled.
 */

#define HDIV 8
#define WDIV (256/HDIV)

#undef OV511_DUMPPIX

static void
ov511_parse_data_rgb24(unsigned char *pIn0, unsigned char *pOut0,
		       int iOutY, int iOutUV, int iHalf, int iWidth)    			    
{
#ifndef OV511_DUMPPIX
	int k, l, m;
	unsigned char *pIn;
	unsigned char *pOut, *pOut1;

	/* Just copy the Y's if in the first stripe */
	if (!iHalf) {
		pIn = pIn0 + 128;
		pOut = pOut0 + iOutY;
		for (k = 0; k < 4; k++) {
			pOut1 = pOut;
			for (l = 0; l < 8; l++) {
				for (m = 0; m < 8; m++) {
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
	for (l = 0; l < 4; l++) {
		for (m=0; m<8; m++) {
			int y00 = *(pOut);
			int y01 = *(pOut+3);
			int y10 = *(pOut+iWidth*3);
			int y11 = *(pOut+iWidth*3+3);
			int v   = *(pIn+64) - 128;
			int u   = *pIn++ - 128;
			ov511_move_420_block(y00, y01, y10, y11, u, v, iWidth,
				pOut);
			pOut += 6;
		}
		pOut += (iWidth*2 - 16) * 3;
	}

	/* Just copy the other UV rows */
	for (l = 0; l < 4; l++) {
		for (m = 0; m < 8; m++) {
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
		for (k = 0; k < 4; k++) {
			pOut1 = pOut;
			for (l=0; l<4; l++) {
				for (m=0; m<4; m++) {
					int y10 = *(pIn+8);
					int y00 = *pIn++;
					int y11 = *(pIn+8);
					int y01 = *pIn++;
					int v   = *pOut1 - 128;
					int u   = *(pOut1+1) - 128;
					ov511_move_420_block(y00, y01, y10,
						y11, u, v, iWidth, pOut1);
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
	int i, j;

	pOut0 += iOutY;
	for (i = 0; i < HDIV; i++) {
		for (j = 0; j < WDIV; j++) {
			*pOut0++ = *pIn0++;
			*pOut0++ = *pIn0++;
			*pOut0++ = *pIn0++;
		}
		pOut0 += (iWidth - WDIV) * 3;
	}
#endif
}

/*
 * For 640x480 RAW BW images, data shows up in 1200 256 byte segments.
 * The segments represent 4 squares of 8x8 pixels as follows:
 *
 *      0  1 ...  7    64  65 ...  71   ...  192 193 ... 199
 *      8  9 ... 15    72  73 ...  79        200 201 ... 207
 *           ...              ...                    ...
 *     56 57 ... 63   120 121 ... 127        248 249 ... 255
 *
 */ 
static void
ov511_parse_data_grey(unsigned char *pIn0, unsigned char *pOut0,
		      int iOutY, int iWidth)		    
{
	int k, l, m;
	unsigned char *pIn;
	unsigned char *pOut, *pOut1;

	pIn = pIn0;
	pOut = pOut0 + iOutY;
	for (k = 0; k < 4; k++) {
		pOut1 = pOut;
		for (l = 0; l < 8; l++) {
			for (m = 0; m < 8; m++) {
				*pOut1++ = *pIn++;
			}
			pOut1 += iWidth - WDIV;
		}
		pOut += 8;
	}
}

/*
 * fixFrameRGBoffset--
 * My camera seems to return the red channel about 1 pixel
 * low, and the blue channel about 1 pixel high. After YUV->RGB
 * conversion, we can correct this easily. OSL 2/24/2000.
 */
static void fixFrameRGBoffset(struct ov511_frame *frame)
{
	int x, y;
	int rowBytes = frame->width*3, w = frame->width;
	unsigned char *rgb = frame->data;
	const int shift = 1;  /* Distance to shift pixels by, vertically */

	/* Don't bother with little images */
	if (frame->width < 400) 
		return;

	/* Shift red channel up */
	for (y = shift; y < frame->height; y++)	{
		int lp = (y-shift)*rowBytes;     /* Previous line offset */
		int lc = y*rowBytes;             /* Current line offset */
		for (x = 0; x < w; x++)
			rgb[lp+x*3+2] = rgb[lc+x*3+2]; /* Shift red up */
	}

	/* Shift blue channel down */
	for (y = frame->height-shift-1; y >= 0; y--) {
		int ln = (y + shift) * rowBytes;  /* Next line offset */
		int lc = y * rowBytes;            /* Current line offset */
		for (x = 0; x < w; x++)
			rgb[ln+x*3+0] = rgb[lc+x*3+0]; /* Shift blue down */
	}
}

/**********************************************************************
 *
 * OV511 data transfer, IRQ handler
 *
 **********************************************************************/

static int ov511_move_data(struct usb_ov511 *ov511, urb_t *urb)
{
	unsigned char *cdata;
	int i, totlen = 0;
	int aPackNum[10];
	struct ov511_frame *frame;

	PDEBUG(4, "ov511_move_data");

	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;
		urb->iso_frame_desc[i].actual_length = 0;
		urb->iso_frame_desc[i].status = 0;
		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		aPackNum[i] = n ? cdata[ov511->packet_size - 1] : -1;

		if (!n || ov511->curframe == -1) continue;

		if (st)
			PDEBUG(2, "data error: [%d] len=%d, status=%d", i, n, st);

		frame = &ov511->frame[ov511->curframe];
		
		/* Can we find a frame end */
		if ((cdata[0] | cdata[1] | cdata[2] | cdata[3] | 
		     cdata[4] | cdata[5] | cdata[6] | cdata[7]) == 0 &&
		    (cdata[8] & 8) && (cdata[8] & 0x80)) {

		    struct timeval *ts;
		    ts = (struct timeval *)(frame->data + MAX_FRAME_SIZE);
		    do_gettimeofday(ts);

		    PDEBUG(4, "Frame End, curframe = %d, packnum=%d, hw=%d, vw=%d",
			   ov511->curframe, (int)(cdata[ov511->packet_size - 1]),
			   (int)(cdata[9]), (int)(cdata[10]));

		    if (frame->scanstate == STATE_LINES) {
		        int iFrameNext;
				if (fix_rgb_offset)
					fixFrameRGBoffset(frame);
				frame->grabstate = FRAME_DONE;
		        if (waitqueue_active(&frame->wq)) {
			  		frame->grabstate = FRAME_DONE;
			 	 	wake_up_interruptible(&frame->wq);
				}
				/* If next frame is ready or grabbing, point to it */
				iFrameNext = (ov511->curframe + 1) % OV511_NUMFRAMES;
				if (ov511->frame[iFrameNext].grabstate== FRAME_READY ||
				    ov511->frame[iFrameNext].grabstate== FRAME_GRABBING) {
				  ov511->curframe = iFrameNext;
				  ov511->frame[iFrameNext].scanstate = STATE_SCANNING;
				} else {

				  PDEBUG(4, "Frame not ready? state = %d",
					 ov511->frame[iFrameNext].grabstate);

				  ov511->curframe = -1;
				}
		    }
		}

		/* Can we find a frame start */
		else if ((cdata[0] | cdata[1] | cdata[2] | cdata[3] | 
			  cdata[4] | cdata[5] | cdata[6] | cdata[7]) == 0 &&
			 (cdata[8] & 8)) {

			PDEBUG(4, "ov511: Found Frame Start!, framenum = %d",
			       ov511->curframe);

			/* Check to see if it's a snapshot frame */
			/* FIXME?? Should the snapshot reset go here? Performance? */
			if (cdata[8] & 0x02) {
				frame->snapshot = 1;
				PDEBUG(3, "ov511_move_data: snapshot detected");
			}

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
			  memmove(pData + ov511->scratchlen, cdata,
				  iPix+frame->segsize);
			} else {
			  pData = &cdata[iPix = 9];
		 	}

			/* Parse the segments */
			while(iPix <= (ov511->packet_size - 1) - frame->segsize &&
			      frame->segment < frame->width * frame->height / 256) {
			  int iSegY;
			  int iSegUV;
			  int iY, jY, iUV, jUV;
			  int iOutY, iOutUV;
			  unsigned char * pOut;

			  iSegY = iSegUV = frame->segment;
			  pOut = frame->data;
			  
			  frame->segment++;
			  iPix += frame->segsize;

			  if (frame->sub_flag) {
			    int iSeg1;
			    iSeg1 = iSegY / (ov511->subw / 32);
			    iSeg1 *= frame->width / 32;
			    iSegY = iSeg1 + (iSegY % (ov511->subw / 32));
			    if (iSegY >= frame->width * ov511->subh / 256)
			      break;

			    iSeg1 = iSegUV / (ov511->subw / 16);
			    iSeg1 *= frame->width / 16;
			    iSegUV = iSeg1 + (iSegUV % (ov511->subw / 16));

			    pOut += (ov511->subx +
				     ov511->suby * frame->width) * frame->depth;
			  }

			  iY     = iSegY / (frame->width / WDIV);
			  jY     = iSegY - iY * (frame->width / WDIV);
			  iOutY  = (iY*HDIV*frame->width + jY*WDIV) * frame->depth;
			  iUV    = iSegUV / (frame->width / WDIV * 2);
			  jUV    = iSegUV - iUV * (frame->width / WDIV * 2);
			  iOutUV = (iUV*HDIV*2*frame->width + jUV*WDIV/2) * frame->depth;

			  if (frame->format == VIDEO_PALETTE_GREY) {
			    ov511_parse_data_grey(pData, pOut, iOutY, frame->width);
			  } else if (frame->format == VIDEO_PALETTE_RGB24) {
			    ov511_parse_data_rgb24(pData, pOut, iOutY, iOutUV, iY & 1,
						   frame->width);
			  }
			  pData = &cdata[iPix];
			}

			/* Save extra data for next time */
			if (frame->segment < frame->width * frame->height / 256) {
			  ov511->scratchlen = (ov511->packet_size - 1) - iPix;
			  if (ov511->scratchlen < frame->segsize) {
			    memmove(ov511->scratch, pData, ov511->scratchlen);
			  } else {
			    ov511->scratchlen = 0;
			  }
			}
		}
	}


	PDEBUG(5, "pn: %d %d %d %d %d %d %d %d %d %d\n",
	       aPackNum[0], aPackNum[1], aPackNum[2], aPackNum[3], aPackNum[4],
	       aPackNum[5],aPackNum[6], aPackNum[7], aPackNum[8], aPackNum[9]);

	return totlen;
}

static void ov511_isoc_irq(struct urb *urb)
{
	int len;
	struct usb_ov511 *ov511 = urb->context;
	struct ov511_sbuf *sbuf;

	if (!ov511->dev)
		return;

	if (!ov511->streaming) {
		PDEBUG(2, "hmmm... not streaming, but got interrupt");
		return;
	} else {
		PDEBUG(5, "streaming. got interrupt");
	}
	
	sbuf = &ov511->sbuf[ov511->cursbuf];

	/* Copy the data received into our scratch buffer */
	if (ov511->curframe >= 0) {
		len = ov511_move_data(ov511, urb);
	} else if (waitqueue_active(&ov511->wq)) {
		wake_up_interruptible(&ov511->wq);
	}

	/* Move to the next sbuf */
	ov511->cursbuf = (ov511->cursbuf + 1) % OV511_NUMSBUF;

	return;
}

static int ov511_init_isoc(struct usb_ov511 *ov511)
{
	urb_t *urb;
	int fx, err;

	PDEBUG(3, "*** Initializing capture ***");

	ov511->compress = 0;
	ov511->curframe = -1;
	ov511->cursbuf = 0;
	ov511->scratchlen = 0;

	if (ov511->bridge == BRG_OV511)
		ov511_set_packet_size(ov511, 993);
	else if (ov511->bridge == BRG_OV511PLUS)
		ov511_set_packet_size(ov511, 961);
	else
		err("invalid bridge type");

	/* We double buffer the Iso lists */
	urb = usb_alloc_urb(FRAMES_PER_DESC);
	
	if (!urb) {
		err("ov511_init_isoc: usb_alloc_urb ret. NULL");
		return -ENOMEM;
	}
	ov511->sbuf[0].urb = urb;
	urb->dev = ov511->dev;
	urb->context = ov511;
	urb->pipe = usb_rcvisocpipe(ov511->dev, OV511_ENDPOINT_ADDRESS);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = ov511->sbuf[0].data;
	urb->complete = ov511_isoc_irq;
	urb->number_of_packets = FRAMES_PER_DESC;
	urb->transfer_buffer_length = ov511->packet_size * FRAMES_PER_DESC;
	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
		urb->iso_frame_desc[fx].offset = ov511->packet_size * fx;
		urb->iso_frame_desc[fx].length = ov511->packet_size;
	}

	urb = usb_alloc_urb(FRAMES_PER_DESC);
	if (!urb) {
		err("ov511_init_isoc: usb_alloc_urb ret. NULL");
		return -ENOMEM;
	}
	ov511->sbuf[1].urb = urb;
	urb->dev = ov511->dev;
	urb->context = ov511;
	urb->pipe = usb_rcvisocpipe(ov511->dev, OV511_ENDPOINT_ADDRESS);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = ov511->sbuf[1].data;
	urb->complete = ov511_isoc_irq;
	urb->number_of_packets = FRAMES_PER_DESC;
	urb->transfer_buffer_length = ov511->packet_size * FRAMES_PER_DESC;
	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
		urb->iso_frame_desc[fx].offset = ov511->packet_size * fx;
		urb->iso_frame_desc[fx].length = ov511->packet_size;
	}

	ov511->sbuf[1].urb->next = ov511->sbuf[0].urb;
	ov511->sbuf[0].urb->next = ov511->sbuf[1].urb;

	err = usb_submit_urb(ov511->sbuf[0].urb);
	if (err)
		err("ov511_init_isoc: usb_submit_urb(0) ret %d", err);
	err = usb_submit_urb(ov511->sbuf[1].urb);
	if (err)
		err("ov511_init_isoc: usb_submit_urb(1) ret %d", err);

	ov511->streaming = 1;

	return 0;
}

static void ov511_stop_isoc(struct usb_ov511 *ov511)
{
	if (!ov511->streaming || !ov511->dev)
		return;

	PDEBUG (3, "*** Stopping capture ***");

	ov511_set_packet_size(ov511, 0);

	ov511->streaming = 0;

	/* Unschedule all of the iso td's */
	if (ov511->sbuf[1].urb) {
		ov511->sbuf[1].urb->next = NULL;
		usb_unlink_urb(ov511->sbuf[1].urb);
		usb_free_urb(ov511->sbuf[1].urb);
		ov511->sbuf[1].urb = NULL;
	}
	if (ov511->sbuf[0].urb) {
		ov511->sbuf[0].urb->next = NULL;
		usb_unlink_urb(ov511->sbuf[0].urb);
		usb_free_urb(ov511->sbuf[0].urb);
		ov511->sbuf[0].urb = NULL;
	}
}

static int ov511_new_frame(struct usb_ov511 *ov511, int framenum)
{
	struct ov511_frame *frame;
	int width, height;

	PDEBUG(4, "ov511->curframe = %d, framenum = %d", ov511->curframe,
		framenum);
	if (!ov511->dev)
		return -1;

	/* If we're not grabbing a frame right now and the other frame is */
	/* ready to be grabbed into, then use it instead */
	if (ov511->curframe == -1) {
		if (ov511->frame[(framenum - 1 + OV511_NUMFRAMES) % OV511_NUMFRAMES].grabstate == FRAME_READY)
			framenum = (framenum - 1 + OV511_NUMFRAMES) % OV511_NUMFRAMES;
	} else
		return 0;

	frame = &ov511->frame[framenum];
	width = frame->width;
	height = frame->height;

	PDEBUG (4, "framenum = %d, width = %d, height = %d", framenum, width,
		height);

	frame->grabstate = FRAME_GRABBING;
	frame->scanstate = STATE_SCANNING;
	frame->scanlength = 0;		/* accumulated in ov511_parse_data() */
	frame->snapshot = 0;

	ov511->curframe = framenum;

	/* Make sure it's not too big */
	if (width > DEFAULT_WIDTH)
		width = DEFAULT_WIDTH;

	width &= ~7L;			/* Multiple of 8 */

	if (height > DEFAULT_HEIGHT)
		height = DEFAULT_HEIGHT;

	width &= ~3L;			/* Multiple of 4 */

	return 0;
}

/****************************************************************************
 *
 * V4L API
 *
 ***************************************************************************/

static int ov511_open(struct video_device *dev, int flags)
{
	int err = -EBUSY;
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	int i;

	PDEBUG(4, "opening");

	down(&ov511->lock);

	if (ov511->user) {
		up(&ov511->lock);
		return -EBUSY;
	}

	err = -ENOMEM;

	/* Allocate memory for the frame buffers */
	ov511->fbuf = rvmalloc(OV511_NUMFRAMES * MAX_DATA_SIZE);
	if (!ov511->fbuf)
		return err;

	ov511->sub_flag = 0;

	for (i = 0; i < OV511_NUMFRAMES; i++) {
		ov511->frame[i].grabstate = FRAME_UNUSED;
		ov511->frame[i].data = ov511->fbuf + i * MAX_DATA_SIZE;
		PDEBUG(4, "frame [%d] @ %p", i, ov511->frame[0].data);

		ov511->sbuf[i].data = kmalloc(FRAMES_PER_DESC *
			MAX_FRAME_SIZE_PER_DESC, GFP_KERNEL);
		if (!ov511->sbuf[i].data) {
open_free_ret:
			while (--i) kfree(ov511->sbuf[i].data);
			rvfree(ov511->fbuf, 2 * MAX_DATA_SIZE);
			return err;
		}	
		PDEBUG(4, "sbuf[%d] @ %p", i, ov511->sbuf[i].data);
	}

	err = ov511_init_isoc(ov511);
	if (err)
		goto open_free_ret;

	ov511->user++;
	up(&ov511->lock);

	MOD_INC_USE_COUNT;

	return 0;
}

static void ov511_close(struct video_device *dev)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	int i;

	PDEBUG(4, "ov511_close");
	
	down(&ov511->lock);	
	ov511->user--;

	MOD_DEC_USE_COUNT;

	ov511_stop_isoc(ov511);

	rvfree(ov511->fbuf, OV511_NUMFRAMES * MAX_DATA_SIZE);
	for (i = 0; i < OV511_NUMFRAMES; i++)
		kfree(ov511->sbuf[i].data);

	up(&ov511->lock);

	if (!ov511->dev) {
		video_unregister_device(&ov511->vdev);
		kfree(ov511);
	}
}

static int ov511_init_done(struct video_device *dev)
{
#ifdef CONFIG_PROC_FS
	create_proc_ov511_cam((struct usb_ov511 *)dev);
#endif

	return 0;
}

static long ov511_write(struct video_device *dev, const char *buf, unsigned long count, int noblock)
{
	return -EINVAL;
}

static int ov511_ioctl(struct video_device *vdev, unsigned int cmd, void *arg)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)vdev;

	PDEBUG(4, "IOCtl: 0x%X", cmd);

	if (!ov511->dev)
		return -EIO;	

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
		b.minwidth = 32;
		b.minheight = 16;

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
	case VIDIOCGCAPTURE:
	{
		int vf;
		if (copy_from_user(&vf, arg, sizeof(vf)))
			return -EFAULT;
		ov511->sub_flag = vf;
		return 0;
	}
	case VIDIOCSCAPTURE:
	{
		struct video_capture vc;

		if (copy_from_user(&vc, arg, sizeof(vc)))
			return -EFAULT;
		if (vc.flags)
			return -EINVAL;
		if (vc.decimation)
			return -EINVAL;
		vc.x /= 4;
		vc.x *= 4;
		vc.y /= 2;
		vc.y *= 2;
		vc.width /= 32;
		vc.width *= 32;
		if (vc.width == 0) vc.width = 32;
		vc.height /= 16;
		vc.height *= 16;
		if (vc.height == 0) vc.height = 16;

		ov511->subx = vc.x;
		ov511->suby = vc.y;
		ov511->subw = vc.width;
		ov511->subh = vc.height;

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
		vm.size = 2 * MAX_DATA_SIZE;
		vm.frames = 2;
		vm.offsets[0] = 0;
		vm.offsets[1] = MAX_FRAME_SIZE + sizeof (struct timeval);

		if (copy_to_user((void *)arg, (void *)&vm, sizeof(vm)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCMCAPTURE:
	{
		struct video_mmap vm;

		if (copy_from_user((void *)&vm, (void *)arg, sizeof(vm)))
			return -EFAULT;

		PDEBUG(4, "MCAPTURE");
		PDEBUG(4, "frame: %d, size: %dx%d, format: %d",
			vm.frame, vm.width, vm.height, vm.format);

		if (vm.format != VIDEO_PALETTE_RGB24 &&
		    vm.format != VIDEO_PALETTE_GREY)
			return -EINVAL;

		if ((vm.frame != 0) && (vm.frame != 1))
			return -EINVAL;
				
		if (ov511->frame[vm.frame].grabstate == FRAME_GRABBING)
			return -EBUSY;

		/* Don't compress if the size changed */
		if ((ov511->frame[vm.frame].width != vm.width) ||
		    (ov511->frame[vm.frame].height != vm.height) ||
		    (ov511->frame[vm.frame].format != vm.format) ||
		    (ov511->frame[vm.frame].sub_flag !=
		     ov511->sub_flag)) {
			/* If we're collecting previous frame wait
			   before changing modes */
			interruptible_sleep_on(&ov511->wq);
			if (signal_pending(current)) return -EINTR;
			ov511_mode_init_regs(ov511,
					     vm.width, vm.height,
					     vm.format, ov511->sub_flag);
		}

		ov511->frame[vm.frame].width = vm.width;
		ov511->frame[vm.frame].height = vm.height;
		ov511->frame[vm.frame].format = vm.format;
		ov511->frame[vm.frame].sub_flag = ov511->sub_flag;
		ov511->frame[vm.frame].segsize =
		  vm.format == VIDEO_PALETTE_RGB24 ? 384 : 256;
		ov511->frame[vm.frame].depth =
		  vm.format == VIDEO_PALETTE_RGB24 ? 3 : 1;

		/* Mark it as ready */
		ov511->frame[vm.frame].grabstate = FRAME_READY;

		return ov511_new_frame(ov511, vm.frame);
	}
	case VIDIOCSYNC:
	{
		int frame;

		if (copy_from_user((void *)&frame, arg, sizeof(int)))
			return -EFAULT;

		PDEBUG(4, "syncing to frame %d, grabstate = %d", frame,
		       ov511->frame[frame].grabstate);

		switch (ov511->frame[frame].grabstate) {
			case FRAME_UNUSED:
				return -EINVAL;
			case FRAME_READY:
			case FRAME_GRABBING:
			case FRAME_ERROR:
redo:
			if (!ov511->dev)
				return -EIO;

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

		/* Reset the hardware snapshot button */
		/* FIXME - Is this the best place for this? */
		if ((ov511->snap_enabled) &&
		    (ov511->frame[frame].snapshot)) {
			ov511->frame[frame].snapshot = 0;
			ov511_reg_write(ov511->dev, 0x52, 0x01);
			ov511_reg_write(ov511->dev, 0x52, 0x03);
			ov511_reg_write(ov511->dev, 0x52, 0x01);
		}

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
	}	/* End switch(cmd) */

	return 0;
}

static long ov511_read(struct video_device *dev, char *buf, unsigned long count, int noblock)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	int i;
	int frmx = -1;
	volatile struct ov511_frame *frame;

	PDEBUG(4, "%ld bytes, noblock=%d", count, noblock);

	if (!dev || !buf)
		return -EFAULT;

	if (!ov511->dev)
		return -EIO;

	/* See if a frame is completed, then use it. */
	if (ov511->frame[0].grabstate >= FRAME_DONE)	/* _DONE or _ERROR */
		frmx = 0;
	else if (ov511->frame[1].grabstate >= FRAME_DONE)/* _DONE or _ERROR */
		frmx = 1;

	/* If nonblocking we return immediately */
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
	if (!ov511->dev)
		return -EIO;

	/* Wait while we're grabbing the image */
	PDEBUG(4, "Waiting image grabbing");
	while (frame->grabstate == FRAME_GRABBING) {
		interruptible_sleep_on(&ov511->frame[frmx].wq);
		if (signal_pending(current))
			return -EINTR;
	}
	PDEBUG(4, "Got image, frame->grabstate = %d", frame->grabstate);

	if (frame->grabstate == FRAME_ERROR) {
		frame->bytes_read = 0;
		err("** ick! ** Errored frame %d", ov511->curframe);
		if (ov511_new_frame(ov511, frmx))
			err("ov511_read: ov511_new_frame error");
		goto restart;
	}


	/* Repeat until we get a snapshot frame */
	if (ov511->snap_enabled)
		PDEBUG (4, "Waiting snapshot frame");
	if (ov511->snap_enabled && !frame->snapshot) {
		frame->bytes_read = 0;
		if (ov511_new_frame(ov511, frmx))
			err("ov511_new_frame error");
		goto restart;
	}

	/* Clear the snapshot */
	if (ov511->snap_enabled && frame->snapshot) {
		frame->snapshot = 0;
		ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_SNAPSHOT, 0x01);
		ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_SNAPSHOT, 0x03);
		ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_SNAPSHOT, 0x01);
	}

	PDEBUG(4, "frmx=%d, bytes_read=%ld, scanlength=%ld", frmx,
		frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
//	if ((count + frame->bytes_read) > frame->scanlength)
//		count = frame->scanlength - frame->bytes_read;

	/* FIXME - count hardwired to be one frame... */
	count = frame->width * frame->height * (frame->depth >> 3);

	PDEBUG(4, "Copy to user space: %ld bytes", count);
	if ((i = copy_to_user(buf, frame->data + frame->bytes_read, count))) {
		PDEBUG(4, "Copy failed! %d bytes not copied", i);
		return -EFAULT;
	}

	frame->bytes_read += count;
	PDEBUG(4, "{copy} count used=%ld, new bytes_read=%ld",
		count, frame->bytes_read);

	if (frame->bytes_read >= frame->scanlength) { /* All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		ov511->frame[frmx].grabstate = FRAME_UNUSED;
		if (ov511_new_frame(ov511, !frmx))
			err("ov511_new_frame returned error");
	}

	PDEBUG(4, "read finished, returning %ld (sweet)", count);

	return count;
}

static int ov511_mmap(struct video_device *dev, const char *adr,
	unsigned long size)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

	if (ov511->dev == NULL)
		return -EIO;

	PDEBUG(4, "mmap: %ld (%lX) bytes", size, size);

	if (size > (((2 * MAX_DATA_SIZE) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)))
		return -EINVAL;

	pos = (unsigned long)ov511->fbuf;
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

static struct video_device ov511_template = {
	name:		"OV511 USB Camera",
	type:		VID_TYPE_CAPTURE,
	hardware:	VID_HARDWARE_OV511,
	open:		ov511_open,
	close:		ov511_close,
	read:		ov511_read,
	write:		ov511_write,
	ioctl:		ov511_ioctl,
	mmap:		ov511_mmap,
	initialize:	ov511_init_done,
};

/****************************************************************************
 *
 * OV511/OV7610 configuration
 *
 ***************************************************************************/

static int ov76xx_configure(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	int i, success;
	int rc;

	static struct ov511_regvals aRegvalsNorm7610[] = {
		{ OV511_I2C_BUS, 0x10, 0xff },
		{ OV511_I2C_BUS, 0x16, 0x06 },
		{ OV511_I2C_BUS, 0x28, 0x24 },
		{ OV511_I2C_BUS, 0x2b, 0xac },
		{ OV511_I2C_BUS, 0x05, 0x00 },
		{ OV511_I2C_BUS, 0x06, 0x00 },
		{ OV511_I2C_BUS, 0x12, 0x00 },
		{ OV511_I2C_BUS, 0x38, 0x81 },
		{ OV511_I2C_BUS, 0x28, 0x24 }, /* 0c */
		{ OV511_I2C_BUS, 0x05, 0x00 },
		{ OV511_I2C_BUS, 0x0f, 0x05 },
		{ OV511_I2C_BUS, 0x15, 0x01 },
		{ OV511_I2C_BUS, 0x20, 0x1c },
		{ OV511_I2C_BUS, 0x23, 0x2a },
		{ OV511_I2C_BUS, 0x24, 0x10 },
		{ OV511_I2C_BUS, 0x25, 0x8a },
		{ OV511_I2C_BUS, 0x27, 0xc2 },
		{ OV511_I2C_BUS, 0x29, 0x03 }, /* 91 */
		{ OV511_I2C_BUS, 0x2a, 0x04 },
		{ OV511_I2C_BUS, 0x2c, 0xfe },
		{ OV511_I2C_BUS, 0x30, 0x71 },
		{ OV511_I2C_BUS, 0x31, 0x60 },
		{ OV511_I2C_BUS, 0x32, 0x26 },
		{ OV511_I2C_BUS, 0x33, 0x20 },
		{ OV511_I2C_BUS, 0x34, 0x48 },
		{ OV511_I2C_BUS, 0x12, 0x24 },
		{ OV511_I2C_BUS, 0x11, 0x01 },
		{ OV511_I2C_BUS, 0x0c, 0x24 },
		{ OV511_I2C_BUS, 0x0d, 0x24 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	static struct ov511_regvals aRegvalsNorm7620[] = {
		{ OV511_I2C_BUS, 0x10, 0xff },
		{ OV511_I2C_BUS, 0x16, 0x06 },
		{ OV511_I2C_BUS, 0x28, 0x24 },
		{ OV511_I2C_BUS, 0x2b, 0xac },
		{ OV511_I2C_BUS, 0x12, 0x00 },
		{ OV511_I2C_BUS, 0x28, 0x24 },
		{ OV511_I2C_BUS, 0x05, 0x00 },
		{ OV511_I2C_BUS, 0x0f, 0x05 },
		{ OV511_I2C_BUS, 0x15, 0x01 },
		{ OV511_I2C_BUS, 0x23, 0x00 },
		{ OV511_I2C_BUS, 0x24, 0x10 },
		{ OV511_I2C_BUS, 0x25, 0x8a },
		{ OV511_I2C_BUS, 0x27, 0xe2 },
		{ OV511_I2C_BUS, 0x29, 0x03 },
		{ OV511_I2C_BUS, 0x2a, 0x00 },
		{ OV511_I2C_BUS, 0x2c, 0xfe },
		{ OV511_I2C_BUS, 0x30, 0x71 },
		{ OV511_I2C_BUS, 0x31, 0x60 },
		{ OV511_I2C_BUS, 0x32, 0x26 },
		{ OV511_I2C_BUS, 0x33, 0x20 },
		{ OV511_I2C_BUS, 0x34, 0x48 },
		{ OV511_I2C_BUS, 0x12, 0x24 },
		{ OV511_I2C_BUS, 0x11, 0x01 },
		{ OV511_I2C_BUS, 0x0c, 0x24 },
		{ OV511_I2C_BUS, 0x0d, 0x24 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	PDEBUG (4, "starting configuration");

	if(ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE,
	                        OV7610_I2C_WRITE_ID) < 0)
		return -1;

	if(ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ,
	                        OV7610_I2C_READ_ID) < 0)
		return -1;

	if (ov511_reset(dev, OV511_RESET_NOREGS) < 0)
		return -1;
	
	/* Reset the 76xx */ 
	if (ov511_i2c_write(dev, 0x12, 0x80) < 0) return -1;

	for (i = 0, success = 0; i < i2c_detect_tries && !success; i++) {
		if ((ov511_i2c_read(dev, OV7610_REG_ID_HIGH) == 0x7F) &&
		    (ov511_i2c_read(dev, OV7610_REG_ID_LOW) == 0xA2))
			success = 1;

		/* Dummy read to sync I2C */
		if (ov511_i2c_read(dev, 0x00) < 0) return -1;
		/* Wait for it to initialize */ 
		schedule_timeout (1 + 150 * HZ / 1000);
	}

	if (success) {
		PDEBUG(1, "I2C synced in %d attempt(s)", i);
	} else {
		err("Failed to read sensor ID. You might not have an OV76xx,");
		err("or it may be not responding. Report this to");
		err("mmcclelland@delphi.com");
		return -1;
	}

	/* Detect sensor if user didn't use override param */
	if (sensor == 0) {
		rc = ov511_i2c_read(dev, OV7610_REG_COM_I);

		if (rc < 0) {
			err("Error detecting sensor type");
			return -1;
		} else if((rc & 3) == 3) {
			info("Sensor is an OV7610");
			ov511->sensor = SEN_OV7610;
		} else if((rc & 3) == 1) {
			info("Sensor is an OV7620AE");
			ov511->sensor = SEN_OV7620AE;
		} else if((rc & 3) == 0) {
			info("Sensor is an OV7620");
			ov511->sensor = SEN_OV7620;
		} else {
			err("Unknown image sensor version: %d", rc & 3);
			return -1;
		}
	} else {	/* sensor != 0; user overrode detection */
		ov511->sensor = sensor;
		info("Sensor set to type %d", ov511->sensor);
	}

	if (ov511->sensor == SEN_OV7620) {
		PDEBUG(4, "Writing 7620 registers");
		if (ov511_write_regvals(dev, aRegvalsNorm7620))
			return -1;
	} else {
		PDEBUG(4, "Writing 7610 registers");
		if (ov511_write_regvals(dev, aRegvalsNorm7610))
			return -1;
	}

	if (aperture < 0) {          /* go with the default */
		if (ov511_i2c_write(dev, 0x26, 0xa2) < 0) return -1;
	} else if (aperture <= 0xf) {  /* user overrode default */
		if (ov511_i2c_write(dev, 0x26, (aperture << 4) + 2) < 0)
			return -1;
	} else {
		err("Invalid setting for aperture; legal value: 0 - 15");
		return -1;
	}

	if (autoadjust) {
		if (ov511_i2c_write(dev, 0x13, 0x01) < 0) return -1;
		if (ov511_i2c_write(dev, 0x2d, 
		     ov511->sensor==SEN_OV7620?0x91:0x93) < 0) return -1;
	} else {
		if (ov511_i2c_write(dev, 0x13, 0x00) < 0) return -1;
		if (ov511_i2c_write(dev, 0x2d, 
		     ov511->sensor==SEN_OV7620?0x81:0x83) < 0) return -1;
		ov511_i2c_write(dev, 0x28, ov511_i2c_read(dev, 0x28) | 8);
	}

	return 0;
}


static int ov511_configure(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	int i;

	static struct ov511_regvals aRegvalsInit[] = {
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x7f },
	 	{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0x01 },
	 	{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x7f },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x3f },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x3d },
		{ OV511_DONE_BUS, 0x0, 0x00},
	};

	static struct ov511_regvals aRegvalsNorm511[] = {
		{ OV511_REG_BUS, OV511_REG_DRAM_ENABLE_FLOW_CONTROL, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x02 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x00 },
		{ OV511_REG_BUS, OV511_REG_FIFO_BITMASK, 0x1f }, /* 0f */
		{ OV511_REG_BUS, OV511_OMNICE_PREDICTION_HORIZ_Y, 0x3f },
		{ OV511_REG_BUS, OV511_OMNICE_PREDICTION_HORIZ_UV, 0x3f },
		{ OV511_REG_BUS, OV511_OMNICE_PREDICTION_VERT_Y, 0x01 },
		{ OV511_REG_BUS, OV511_OMNICE_PREDICTION_VERT_UV, 0x01 },
		{ OV511_REG_BUS, OV511_OMNICE_QUANTIZATION_HORIZ_Y, 0x01 },
		{ OV511_REG_BUS, OV511_OMNICE_QUANTIZATION_HORIZ_UV, 0x01 },
		{ OV511_REG_BUS, OV511_OMNICE_QUANTIZATION_VERT_Y, 0x01 },
		{ OV511_REG_BUS, OV511_OMNICE_QUANTIZATION_VERT_UV, 0x01 },
		{ OV511_REG_BUS, OV511_OMNICE_ENABLE, 0x06 },
		{ OV511_REG_BUS, OV511_OMNICE_LUT_ENABLE, 0x03 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	memcpy(&ov511->vdev, &ov511_template, sizeof(ov511_template));

	init_waitqueue_head(&ov511->frame[0].wq);
	init_waitqueue_head(&ov511->frame[1].wq);
	init_waitqueue_head(&ov511->wq);

	if (video_register_device(&ov511->vdev, VFL_TYPE_GRABBER) == -1) {
		err("video_register_device failed");
		return -EBUSY;
	}

	if (ov511_write_regvals(dev, aRegvalsInit)) goto error;
	if (ov511_write_regvals(dev, aRegvalsNorm511)) goto error;

	ov511_set_packet_size(ov511, 0);

	ov511->snap_enabled = snapshot;	

	/* Set default sizes in case IOCTL (VIDIOCMCAPTURE) is not used
	 * (using read() instead). */
	for (i = 0; i < OV511_NUMFRAMES; i++) {
		ov511->frame[i].width = DEFAULT_WIDTH;
		ov511->frame[i].height = DEFAULT_HEIGHT;
		ov511->frame[i].depth = 24;
		ov511->frame[i].bytes_read = 0;
		ov511->frame[i].segment = 0;
		ov511->frame[i].format = VIDEO_PALETTE_RGB24;
		ov511->frame[i].segsize = GET_SEGSIZE(ov511->frame[i].format);
	}

	/* Initialize to DEFAULT_WIDTH, DEFAULT_HEIGHT, YUV4:2:0 */

	if(ov76xx_configure(ov511) < 0) {
		err("failed to configure OV76xx");
 		goto error;	
	}

	if (ov511_mode_init_regs(ov511, DEFAULT_WIDTH, DEFAULT_HEIGHT,
				 VIDEO_PALETTE_RGB24, 0) < 0)
		goto error;

	return 0;
	
error:
	video_unregister_device(&ov511->vdev);
	usb_driver_release_interface(&ov511_driver,
		&dev->actconfig->interface[ov511->iface]);

	kfree(ov511);
	ov511 = NULL;

	return -EBUSY;	
}


/****************************************************************************
 *
 *  USB routines
 *
 ***************************************************************************/

static void* ov511_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct usb_ov511 *ov511;
	int i;

	PDEBUG(1, "probing for device...");

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return NULL;

	interface = &dev->actconfig->interface[ifnum].altsetting[0];

	/* Is it an OV511/OV511+? */
	if (dev->descriptor.idVendor != 0x05a9)
		return NULL;
	if (dev->descriptor.idProduct != 0x0511
	 && dev->descriptor.idProduct != 0xA511)
		return NULL;

	/* Checking vendor/product should be enough, but what the hell */
	if (interface->bInterfaceClass != 0xFF) 
		return NULL;
	if (interface->bInterfaceSubClass != 0x00)
		return NULL;

	if ((ov511 = kmalloc(sizeof(*ov511), GFP_KERNEL)) == NULL) {
		err("couldn't kmalloc ov511 struct");
		return NULL;
	}

	memset(ov511, 0, sizeof(*ov511));

	ov511->dev = dev;
	ov511->iface = interface->bInterfaceNumber;

	switch (dev->descriptor.idProduct) {
	case 0x0511:
		info("USB OV511 camera found");
		ov511->bridge = BRG_OV511;
		break;
	case 0xA511:
		info("USB OV511+ camera found");
		ov511->bridge = BRG_OV511PLUS;
		break;
	}

	ov511->customid = ov511_reg_read(dev, OV511_REG_SYSTEM_CUSTOM_ID);
	if (ov511->customid < 0) {
		err("Unable to read camera bridge registers");
		goto error;
	}

	ov511->desc = -1;
	PDEBUG (4, "CustomID = %d", ov511->customid);
	for (i = 0; clist[i].id >= 0; i++) {
		if (ov511->customid == clist[i].id) {
			info("camera: %s", clist[i].description);
			ov511->desc = i;
			break;
		}
	}

	/* Lifeview USB Life TV not supported */
	if (clist[i].id == 38) {
		err("This device is not supported yet.");
		return NULL;
	}

	if (clist[i].id == -1) {
		err("Camera type (%d) not recognized", ov511->customid);
		err("Please contact mmcclelland@delphi.com to request");
		err("support for your camera.");
	}

	/* Workaround for some applications that want data in RGB
	 * instead of BGR */
	if (force_rgb)
		info("data format set to RGB");

	if (!ov511_configure(ov511)) {
		ov511->user = 0;
		init_MUTEX(&ov511->lock);	/* to 1 == available */
		return ov511;
	} else {
		err("Failed to configure camera");
		goto error;
	}

     	return ov511;

error:
	if (ov511) {
		kfree(ov511);
		ov511 = NULL;
	}

	return NULL;
}


static void ov511_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *) ptr;

//	video_unregister_device(&ov511->vdev);

	/* We don't want people trying to open up the device */
	if (!ov511->user)
		video_unregister_device(&ov511->vdev);

	usb_driver_release_interface(&ov511_driver,
		&ov511->dev->actconfig->interface[ov511->iface]);

	ov511->dev = NULL;
	ov511->frame[0].grabstate = FRAME_ERROR;
	ov511->frame[1].grabstate = FRAME_ERROR;
	ov511->curframe = -1;

	/* This will cause the process to request another frame */
	if (waitqueue_active(&ov511->frame[0].wq))
		wake_up_interruptible(&ov511->frame[0].wq);
	if (waitqueue_active(&ov511->frame[1].wq))
		wake_up_interruptible(&ov511->frame[1].wq);
	if (waitqueue_active(&ov511->wq))
		wake_up_interruptible(&ov511->wq);

	ov511->streaming = 0;

	/* Unschedule all of the iso td's */
	if (ov511->sbuf[1].urb) {
		ov511->sbuf[1].urb->next = NULL;
		usb_unlink_urb(ov511->sbuf[1].urb);
		usb_free_urb(ov511->sbuf[1].urb);
		ov511->sbuf[1].urb = NULL;
	}
	if (ov511->sbuf[0].urb) {
		ov511->sbuf[0].urb->next = NULL;
		usb_unlink_urb(ov511->sbuf[0].urb);
		usb_free_urb(ov511->sbuf[0].urb);
		ov511->sbuf[0].urb = NULL;
	}

#ifdef CONFIG_PROC_FS
        destroy_proc_ov511_cam(ov511);
#endif

	/* Free the memory */
	if (!ov511->user) {
		kfree(ov511);
		ov511 = NULL;
	}
}

static struct usb_driver ov511_driver = {
	"ov511",
	ov511_probe,
	ov511_disconnect,
	{ NULL, NULL }
};


/****************************************************************************
 *
 *  Module routines
 *
 ***************************************************************************/

static int __init usb_ov511_init(void)
{
#ifdef CONFIG_PROC_FS
	PDEBUG(3, "creating /proc/ov511");
        proc_ov511_create();
#endif

	if (usb_register(&ov511_driver) < 0)
		return -1;

	info("ov511 driver version %s registered", version);

	return 0;
}

static void __exit usb_ov511_exit(void)
{
	usb_deregister(&ov511_driver);
	info("ov511 driver deregistered");

#ifdef CONFIG_PROC_FS
        proc_ov511_destroy();
#endif 
}

module_init(usb_ov511_init);
module_exit(usb_ov511_exit);
