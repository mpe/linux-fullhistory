/*
 * USB IBM C-It Video Camera driver
 *
 * Supports IBM C-It Video Camera.
 *
 * This driver is based on earlier work of:
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
#include "ibmcam.h"

#define	ENABLE_HEXDUMP	0	/* Enable if you need it */
static int debug = 0;

/* Completion states of the data parser */
typedef enum {
	scan_Continue,		/* Just parse next item */
	scan_NextFrame,		/* Frame done, send it to V4L */
	scan_Out,		/* Not enough data for frame */
	scan_EndParse		/* End parsing */
} scan_state_t;

/* Bit flags (options) */
#define FLAGS_RETRY_VIDIOCSYNC		(1 << 0)
#define	FLAGS_MONOCHROME		(1 << 1)
#define FLAGS_DISPLAY_HINTS		(1 << 2)
#define FLAGS_OVERLAY_STATS		(1 << 3)
#define FLAGS_FORCE_TESTPATTERN		(1 << 4)

static int flags = 0; /* FLAGS_DISPLAY_HINTS | FLAGS_OVERLAY_STATS; */

/* This is the size of V4L frame that we provide */
static const int imgwidth = V4L_FRAME_WIDTH_USED;
static const int imgheight = V4L_FRAME_HEIGHT;
static const int min_imgwidth  = 8;
static const int min_imgheight = 4;

#define	LIGHTING_MIN	0 /* 0=Bright 1=Med 2=Low */
#define	LIGHTING_MAX	2
static int lighting = (LIGHTING_MIN + LIGHTING_MAX) / 2; /* Medium */

#define SHARPNESS_MIN	0
#define SHARPNESS_MAX	6
static int sharpness = 4; /* Low noise, good details */

#define FRAMERATE_MIN	0
#define FRAMERATE_MAX	6
static int framerate = 2; /* Lower, reliable frame rate (8-12 fps) */

enum {
	VIDEOSIZE_128x96 = 0,
	VIDEOSIZE_176x144,
	VIDEOSIZE_352x288
};

static int videosize = VIDEOSIZE_352x288;

/*
 * The value of 'scratchbufsize' affects quality of the picture
 * in many ways. Shorter buffers may cause loss of data when client
 * is too slow. Larger buffers are memory-consuming and take longer
 * to work with. This setting can be adjusted, but the default value
 * should be OK for most desktop users.
 */
#define DEFAULT_SCRATCH_BUF_SIZE	(0x10000)	/* 64 KB */
static const int scratchbufsize = DEFAULT_SCRATCH_BUF_SIZE;

/*
 * Here we define several initialization variables. They may
 * be used to automatically set color, hue, brightness and
 * contrast to desired values. This is particularly useful in
 * case of webcams (which have no controls and no on-screen
 * output) and also when a client V4L software is used that
 * does not have some of those controls. In any case it's
 * good to have startup values as options.
 *
 * These values are all in [0..255] range. This simplifies
 * operation. Note that actual values of V4L variables may
 * be scaled up (as much as << 8). User can see that only
 * on overlay output, however, or through a V4L client.
 */
static int init_brightness = 128;
static int init_contrast = 192;
static int init_color = 128;
static int init_hue = 128;

MODULE_PARM(debug, "i");
MODULE_PARM(flags, "i");
MODULE_PARM(framerate, "i");
MODULE_PARM(lighting, "i");
MODULE_PARM(sharpness, "i");
MODULE_PARM(videosize, "i");
MODULE_PARM(init_brightness, "i");
MODULE_PARM(init_contrast, "i");
MODULE_PARM(init_color, "i");
MODULE_PARM(init_hue, "i");

/* Still mysterious i2o commands */
static const unsigned short unknown_88 = 0x0088;
static const unsigned short unknown_89 = 0x0089;
static const unsigned short bright_3x[3] = { 0x0031, 0x0032, 0x0033 };
static const unsigned short contrast_14 = 0x0014;
static const unsigned short light_27 = 0x0027;
static const unsigned short sharp_13 = 0x0013;

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

static struct usb_driver ibmcam_driver;

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

/*
 * usb_ibmcam_overlaychar()
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_overlaychar(
	struct usb_ibmcam *ibmcam,
	struct ibmcam_frame *frame,
	int x, int y, int ch)
{
	static const unsigned short digits[16] = {
		0xF6DE, /* 0 */
		0x2492, /* 1 */
		0xE7CE, /* 2 */
		0xE79E, /* 3 */
		0xB792, /* 4 */
		0xF39E, /* 5 */
		0xF3DE, /* 6 */
		0xF492, /* 7 */
		0xF7DE, /* 8 */
		0xF79E, /* 9 */
		0x77DA, /* a */
		0xD75C, /* b */
		0xF24E, /* c */
		0xD6DC, /* d */
		0xF34E, /* e */
		0xF348  /* f */
	};
	unsigned short digit;
	int ix, iy;

	if ((ibmcam == NULL) || (frame == NULL))
		return;

	if (ch >= '0' && ch <= '9')
		ch -= '0';
	else if (ch >= 'A' && ch <= 'F')
		ch = 10 + (ch - 'A');
	else if (ch >= 'a' && ch <= 'f')
		ch = 10 + (ch - 'a');
	else
		return;
	digit = digits[ch];

	for (iy=0; iy < 5; iy++) {
		for (ix=0; ix < 3; ix++) {
			if (digit & 0x8000) {
				IBMCAM_PUTPIXEL(frame, x+ix, y+iy, 0xFF, 0xFF, 0xFF);
			}
			digit = digit << 1;
		}
	}
}

/*
 * usb_ibmcam_overlaystring()
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_overlaystring(
	struct usb_ibmcam *ibmcam,
	struct ibmcam_frame *frame,
	int x, int y, const char *str)
{
	while (*str) {
		usb_ibmcam_overlaychar(ibmcam, frame, x, y, *str);
		str++;
		x += 4; /* 3 pixels character + 1 space */
	}
}

/*
 * usb_ibmcam_overlaystats()
 *
 * Overlays important debugging information.
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_overlaystats(struct usb_ibmcam *ibmcam, struct ibmcam_frame *frame)
{
	const int y_diff = 8;
	char tmp[16];
	int x = 10;
	int y = 10;

	sprintf(tmp, "%8x", ibmcam->frame_num);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->urb_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->urb_length);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->data_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->header_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->scratch_ovf_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->iso_skip_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", ibmcam->iso_err_count);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.colour);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.hue);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.brightness >> 8);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", ibmcam->vpic.contrast >> 12);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8d", ibmcam->vpic.whiteness >> 8);
	usb_ibmcam_overlaystring(ibmcam, frame, x, y, tmp);
	y += y_diff;
}

/*
 * usb_ibmcam_testpattern()
 *
 * Procedure forms a test pattern (yellow grid on blue background).
 *
 * Parameters:
 * fullframe:   if TRUE then entire frame is filled, otherwise the procedure
 *	      continues from the current scanline.
 * pmode	0: fill the frame with solid blue color (like on VCR or TV)
 *	      1: Draw a colored grid
 *
 * History:
 * 1/2/00   Created.
 */
void usb_ibmcam_testpattern(struct usb_ibmcam *ibmcam, int fullframe, int pmode)
{
	static const char proc[] = "usb_ibmcam_testpattern";
	struct ibmcam_frame *frame;
	unsigned char *f;
	int num_cell = 0;
	int scan_length = 0;
	static int num_pass = 0;

	if (ibmcam == NULL) {
		printk(KERN_ERR "%s: ibmcam == NULL\n", proc);
		return;
	}
	if ((ibmcam->curframe < 0) || (ibmcam->curframe >= IBMCAM_NUMFRAMES)) {
		printk(KERN_ERR "%s: ibmcam->curframe=%d.\n", proc, ibmcam->curframe);
		return;
	}

	/* Grab the current frame */
	frame = &ibmcam->frame[ibmcam->curframe];

	/* Optionally start at the beginning */
	if (fullframe) {
		frame->curline = 0;
		frame->scanlength = 0;
	}

	/* Form every scan line */
	for (; frame->curline < imgheight; frame->curline++) {
		int i;

		f = frame->data + (imgwidth * 3 * frame->curline);
		for (i=0; i < imgwidth; i++) {
			unsigned char cb=0x80;
			unsigned char cg = 0;
			unsigned char cr = 0;

			if (pmode == 1) {
				if (frame->curline % 32 == 0)
					cb = 0, cg = cr = 0xFF;
				else if (i % 32 == 0) {
					if (frame->curline % 32 == 1)
						num_cell++;
					cb = 0, cg = cr = 0xFF;
				} else {
					cb = ((num_cell*7) + num_pass) & 0xFF;
					cg = ((num_cell*5) + num_pass*2) & 0xFF;
					cr = ((num_cell*3) + num_pass*3) & 0xFF;
				}
			} else {
				/* Just the blue screen */
			}
				
			*f++ = cb;
			*f++ = cg;
			*f++ = cr;
			scan_length += 3;
		}
	}

	frame->grabstate = FRAME_DONE;
	frame->scanlength += scan_length;
	++num_pass;

	/* We do this unconditionally, regardless of FLAGS_OVERLAY_STATS */
	usb_ibmcam_overlaystats(ibmcam, frame);
}

static unsigned char *ibmcam_find_header(const unsigned char hdr_sig, unsigned char *data, int len)
{
	while (len >= 4)
	{
		if ((data[0] == 0x00) && (data[1] == 0xFF) && (data[2] == 0x00))
		{
#if 0
			/* This code helps to detect new frame markers */
			printk(KERN_DEBUG "Header sig: 00 FF 00 %02X\n", data[3]);
#endif
			if (data[3] == hdr_sig) {
				if (debug > 2)
					printk(KERN_DEBUG "Header found.\n");
				return data;
			}
		}
		++data;
		--len;
	}
	return NULL;
}

/* How much data is left in the scratch buf? */
#define scratch_left(x)	(ibmcam->scratchlen - (int)((char *)x - (char *)ibmcam->scratch))

/* Grab the remaining */
static void usb_ibmcam_align_scratch(struct usb_ibmcam *ibmcam, unsigned char *data)
{
	unsigned long left;

	left = scratch_left(data);
	memmove(ibmcam->scratch, data, left);
	ibmcam->scratchlen = left;
}

/*
 * usb_ibmcam_find_header()
 *
 * Locate one of supported header markers in the scratch buffer.
 * Once found, remove all preceding bytes AND the marker (4 bytes)
 * from the scratch buffer. Whatever follows must be video lines.
 *
 * History:
 * 1/21/00  Created.
 */
static scan_state_t usb_ibmcam_find_header(struct usb_ibmcam *ibmcam)
{
	struct ibmcam_frame *frame;
	unsigned char *data, *tmp;

	data = ibmcam->scratch;
	frame = &ibmcam->frame[ibmcam->curframe];
	tmp = ibmcam_find_header(frame->hdr_sig, data, scratch_left(data));

	if (tmp == NULL) {
		/* No header - entire scratch buffer is useless! */
		if (debug > 2)
			printk(KERN_DEBUG "Skipping frame, no header\n");
		ibmcam->scratchlen = 0;
		return scan_EndParse;
	}
	/* Header found */
	data = tmp+4;

	ibmcam->has_hdr = 1;
	ibmcam->header_count++;
	frame->scanstate = STATE_LINES;
	frame->curline = 0;

	if (flags & FLAGS_FORCE_TESTPATTERN) {
		usb_ibmcam_testpattern(ibmcam, 1, 1);
		return scan_NextFrame;
	}
	usb_ibmcam_align_scratch(ibmcam, data);
	return scan_Continue;
}

/*
 * usb_ibmcam_parse_lines()
 *
 * Parse one line (TODO: more than one!) from the scratch buffer, put
 * decoded RGB value into the current frame buffer and add the written
 * number of bytes (RGB) to the *pcopylen.
 *
 * History:
 * 1/21/00  Created.
 */
static scan_state_t usb_ibmcam_parse_lines(struct usb_ibmcam *ibmcam, long *pcopylen)
{
	struct ibmcam_frame *frame;
	unsigned char *data, *f, *chromaLine;
	unsigned int len;
	const int v4l_linesize = imgwidth * V4L_BYTES_PER_PIXEL; /* V4L line offset */
	int y, u, v, i, frame_done=0, mono_plane, hue_corr, color_corr;

	hue_corr = (ibmcam->vpic.hue - 0x8000) >> 10;      /* -32..+31 */
	color_corr = (ibmcam->vpic.colour - 0x8000) >> 10; /* -32..+31 */

	data = ibmcam->scratch;
	frame = &ibmcam->frame[ibmcam->curframe];

	len = frame->frmwidth * 3; /* 1 line of mono + 1 line of color */
	/*printk(KERN_DEBUG "len=%d. left=%d.\n",len,scratch_left(data));*/

	mono_plane = ((frame->curline & 1) == 0);

	/*
	 * Lines are organized this way (or are they?)
	 *
	 * I420:
	 * ~~~~
	 * ___________________________________
	 * |-----Y-----|---UVUVUV...UVUV-----| \
	 * |-----------+---------------------|  \
	 * |<-- 176 -->|<------ 176*2 ------>|  Total 72. pairs of lines
	 * |...	   ...	     ...|  /
	 * |___________|_____________________| /
	 *  - odd line- ------- even line ---
	 *
	 * another format:
	 * ~~~~~~~~~~~~~~
	 * ___________________________________
	 * |-----Y-----|---UVUVUV...UVUV-----| \
	 * |-----------+---------------------|  \
	 * |<-- 352 -->|<------ 352*2 ------>|  Total 144. pairs of lines
	 * |...	   ...	     ...|  /
	 * |___________|_____________________| /
	 *  - odd line- ------- even line ---
	 */

	/* Make sure there's enough data for the entire line */
	if (scratch_left(data) < (len+1024)) {
		/*printk(KERN_DEBUG "out of data, need %u.\n", len);*/
		return scan_Out;
	}

	/*
	 * Make sure that our writing into output buffer
	 * will not exceed the buffer. Mind that we may write
	 * not into current output scanline but in several after
	 * it as well (if we enlarge image vertically.)
	 */
	if ((frame->curline + 1) >= V4L_FRAME_HEIGHT)
		return scan_NextFrame;

	/*
	 * Now we are sure that entire line (representing all 'frame->frmwidth'
	 * pixels from the camera) is available in the scratch buffer. We
	 * start copying the line left-aligned to the V4L buffer (which
	 * might be larger - not smaller, hopefully). If the camera
	 * line is shorter then we should pad the V4L buffer with something
	 * (black in this case) to complete the line.
	 */
	f = frame->data + (v4l_linesize * frame->curline);

	/*
	 * chromaLine points to 1st pixel of the line with chrominance.
	 * If current line is monochrome then chromaLine points to next
	 * line after monochrome one. If current line has chrominance
	 * then chromaLine points to this very line. Such method allows
	 * to access chrominance data uniformly.
	 *
	 * To obtain chrominance data from the 'chromaLine' use this:
	 *   v = chromaLine[0]; // 0-1:[0], 2-3:[4], 4-5:[8]...
	 *   u = chromaLine[2]; // 0-1:[2], 2-3:[6], 4-5:[10]...
	 *
	 * Indices must be calculated this way:
	 * v_index = (i >> 1) << 2;
	 * u_index = (i >> 1) << 2 + 2;
	 *
	 * where 'i' is the column number [0..frame->frmwidth-1]
	 */
	chromaLine = data;
	if (mono_plane)
		chromaLine += frame->frmwidth;

	for (i = 0; i < frame->frmwidth; i++, data += (mono_plane ? 1 : 2))
	{
		unsigned char rv, gv, bv;	/* RGB components */

		/*
		 * Search for potential Start-Of-Frame marker. It should
		 * not be here, of course, but if your formats don't match
		 * you might exceed the frame. We must match the marker to
		 * each byte of multi-byte data element if it is multi-byte.
		 */
#if 1
		if (scratch_left(data) >= (4+2)) {
			unsigned char *dp;
			int j;

			for (j=0, dp=data; j < 2; j++, dp++) {
				if ((dp[0] == 0x00) && (dp[1] == 0xFF) &&
				    (dp[2] == 0x00) && (dp[3] == frame->hdr_sig)) {
					ibmcam->has_hdr = 2;
					frame_done++;
					break;
				}
			}
		}
#endif

		/* Check for various visual debugging hints (colorized pixels) */
		if ((flags & FLAGS_DISPLAY_HINTS) && (ibmcam->has_hdr)) {
			/*
			 * This is bad and should not happen. This means that
			 * we somehow overshoot the line and encountered new
			 * frame! Obviously our camera/V4L frame size is out
			 * of whack. This cyan dot will help you to figure
			 * out where exactly the new frame arrived.
			 */
			if (ibmcam->has_hdr == 1) {
				bv = 0; /* Yellow marker */
				gv = 0xFF;
				rv = 0xFF;
			} else {
				bv = 0xFF; /* Cyan marker */
				gv = 0xFF;
				rv = 0;
			}
			ibmcam->has_hdr = 0;
			goto make_pixel;
		}

		y = mono_plane ? data[0] : data[1];

		if (flags & FLAGS_MONOCHROME) /* Use monochrome for debugging */
			rv = gv = bv = y;
		else {
			if (frame->order_uv) {
				u = chromaLine[(i >> 1) << 2] + hue_corr;
				v = chromaLine[((i >> 1) << 2) + 2] + color_corr;
			} else {
				v = chromaLine[(i >> 1) << 2] + color_corr;
				u = chromaLine[((i >> 1) << 2) + 2] + hue_corr;
			}
			YUV_TO_RGB_BY_THE_BOOK(y, u, v, rv, gv, bv);
		}

	make_pixel:
		/*
		 * The purpose of creating the pixel here, in one,
		 * dedicated place is that we may need to make the
		 * pixel wider and taller than it actually is. This
		 * may be used if camera generates small frames for
		 * sake of frame rate (or any other reason.)
		 *
		 * The output data consists of B, G, R bytes
		 * (in this order).
		 */
#if USES_IBMCAM_PUTPIXEL
		IBMCAM_PUTPIXEL(frame, i, frame->curline, rv, gv, bv);
#else
		*f++ = bv;
		*f++ = gv;
		*f++ = rv;
#endif
		/*
		 * Typically we do not decide within a legitimate frame
		 * that we want to end the frame. However debugging code
		 * may detect marker of new frame within the data. Then
		 * this condition activates. The 'data' pointer is already
		 * pointing at the new marker, so we'd better leave it as is.
		 */
		if (frame_done)
			break;	/* End scanning of lines */
	}
	/*
	 * Account for number of bytes that we wrote into output V4L frame.
	 * We do it here, after we are done with the scanline, because we
	 * may fill more than one output scanline if we do vertical
	 * enlargement.
	 */
	frame->curline++;
	*pcopylen += v4l_linesize;
	usb_ibmcam_align_scratch(ibmcam, data);

	if (frame_done || (frame->curline >= frame->frmheight))
		return scan_NextFrame;
	else
		return scan_Continue;
}

/*
 * ibmcam_parse_data()
 *
 * Generic routine to parse the scratch buffer. It employs either
 * usb_ibmcam_find_header() or usb_ibmcam_parse_lines() to do most
 * of work.
 *
 * History:
 * 1/21/00  Created.
 */
static void ibmcam_parse_data(struct usb_ibmcam *ibmcam)
{
	struct ibmcam_frame *frame;
	unsigned char *data = ibmcam->scratch;
	scan_state_t newstate;
	long copylen = 0;

	/* Grab the current frame and the previous frame */
	frame = &ibmcam->frame[ibmcam->curframe];

	/* printk(KERN_DEBUG "parsing %u.\n", ibmcam->scratchlen); */

	while (1) {

		newstate = scan_Out;
		if (scratch_left(data)) {
			if (frame->scanstate == STATE_SCANNING)
				newstate = usb_ibmcam_find_header(ibmcam);
			else if (frame->scanstate == STATE_LINES)
				newstate = usb_ibmcam_parse_lines(ibmcam, &copylen);
		}
		if (newstate == scan_Continue)
			continue;
		else if ((newstate == scan_NextFrame) || (newstate == scan_Out))
			break;
		else
			return; /* scan_EndParse */
	}

	if (newstate == scan_NextFrame) {
		frame->grabstate = FRAME_DONE;
		ibmcam->curframe = -1;
		ibmcam->frame_num++;

		/* Optionally display statistics on the screen */
		if (flags & FLAGS_OVERLAY_STATS)
			usb_ibmcam_overlaystats(ibmcam, frame);

		/* This will cause the process to request another frame. */
		if (waitqueue_active(&frame->wq))
			wake_up_interruptible(&frame->wq);
	}

	/* Update the frame's uncompressed length. */
	frame->scanlength += copylen;
}

#if ENABLE_HEXDUMP
static void ibmcam_hexdump(const unsigned char *data, int len)
{
	char tmp[80];
	int i, k;

	for (i=k=0; len > 0; i++, len--) {
		if (i > 0 && (i%16 == 0)) {
			printk("%s\n", tmp);
			k=0;
		}
		k += sprintf(&tmp[k], "%02x ", data[i]);
	}
	if (k > 0)
		printk("%s\n", tmp);
}
#endif

/*
 * Make all of the blocks of data contiguous
 */
static int ibmcam_compress_isochronous(struct usb_ibmcam *ibmcam, urb_t *urb)
{
	unsigned char *cdata, *data, *data0;
	int i, totlen = 0;

	data = data0 = ibmcam->scratch + ibmcam->scratchlen;
	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;
		
		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		/* Detect and ignore errored packets */
		if (st < 0) {
			if (debug >= 1) {
				printk(KERN_ERR "ibmcam data error: [%d] len=%d, status=%X\n",
				       i, n, st);
			}
			ibmcam->iso_err_count++;
			continue;
		}

		/* Detect and ignore empty packets */
		if (n <= 0) {
			ibmcam->iso_skip_count++;
			continue;
		}

		/*
		 * If camera continues to feed us with data but there is no
		 * consumption (if, for example, V4L client fell asleep) we
		 * may overflow the buffer. We have to move old data over to
		 * free room for new data. This is bad for old data. If we
		 * just drop new data then it's bad for new data... choose
		 * your favorite evil here.
		 */
		if ((ibmcam->scratchlen + n) > scratchbufsize) {
#if 0
			ibmcam->scratch_ovf_count++;
			if (debug >= 3)
				printk(KERN_ERR "ibmcam: scratch buf overflow! "
				       "scr_len: %d, n: %d\n", ibmcam->scratchlen, n );
			return totlen;
#else
			int mv;

			ibmcam->scratch_ovf_count++;
			if (debug >= 3) {
				printk(KERN_ERR "ibmcam: scratch buf overflow! "
				       "scr_len: %d, n: %d\n", ibmcam->scratchlen, n );
			}
			mv  = (ibmcam->scratchlen + n) - scratchbufsize;
			if (ibmcam->scratchlen >= mv) {
				int newslen = ibmcam->scratchlen - mv;
				memmove(ibmcam->scratch, ibmcam->scratch + mv, newslen);
				ibmcam->scratchlen = newslen;
				data = data0 = ibmcam->scratch + ibmcam->scratchlen;
			} else {
				printk(KERN_ERR "ibmcam: scratch buf too small\n");
				return totlen;
			}
#endif
		}

		/* Now we know that there is enough room in scratch buffer */
		memmove(data, cdata, n);
		data += n;
		totlen += n;
		ibmcam->scratchlen += n;
	}
#if 0
	if (totlen > 0) {
		static int foo=0;
		if (foo < 1) {
			printk(KERN_DEBUG "+%d.\n", totlen);
			ibmcam_hexdump(data0, (totlen > 64) ? 64:totlen);
			++foo;
		}
	}
#endif
	return totlen;
}

static void ibmcam_isoc_irq(struct urb *urb)
{
	int len;
	struct usb_ibmcam *ibmcam = urb->context;
	struct ibmcam_sbuf *sbuf;
	int i;

	/* We don't want to do anything if we are about to be removed! */
	if (ibmcam->remove_pending)
		return;

#if 0
	if (urb->actual_length > 0) {
		printk(KERN_DEBUG "ibmcam_isoc_irq: %p status %d, "
		       " errcount = %d, length = %d\n", urb, urb->status,
		       urb->error_count, urb->actual_length);
	} else {
		static int c = 0;
		if (c++ % 100 == 0)
			printk(KERN_DEBUG "ibmcam_isoc_irq: no data\n");
	}
#endif

	if (!ibmcam->streaming) {
		if (debug >= 1)
			printk(KERN_DEBUG "ibmcam: oops, not streaming, but interrupt\n");
		return;
	}
	
	sbuf = &ibmcam->sbuf[ibmcam->cursbuf];

	/* Copy the data received into our scratch buffer */
	len = ibmcam_compress_isochronous(ibmcam, urb);

	ibmcam->urb_count++;
	ibmcam->urb_length = len;
	ibmcam->data_count += len;

#if 0   /* This code prints few initial bytes of ISO data: used to decode markers */
	if (ibmcam->urb_count % 64 == 1) {
		if (ibmcam->urb_count == 1) {
			ibmcam_hexdump(ibmcam->scratch,
				       (ibmcam->scratchlen > 32) ? 32 : ibmcam->scratchlen);
		}
	}
#endif

	/* If we collected enough data let's parse! */
	if (ibmcam->scratchlen) {
		/* If we don't have a frame we're current working on, complain */
		if (ibmcam->curframe >= 0)
			ibmcam_parse_data(ibmcam);
		else {
			if (debug >= 1)
				printk(KERN_DEBUG "ibmcam: received data, but no frame available\n");
		}
	}

	for (i = 0; i < FRAMES_PER_DESC; i++) {
		sbuf->urb->iso_frame_desc[i].status = 0;
		sbuf->urb->iso_frame_desc[i].actual_length = 0;
	}

	/* Move to the next sbuf */
	ibmcam->cursbuf = (ibmcam->cursbuf + 1) % IBMCAM_NUMSBUF;

	return;
}

static int usb_ibmcam_veio(
	struct usb_device *dev,
	unsigned char req,
	unsigned short value,
	unsigned short index)
{
	static const char proc[] = "usb_ibmcam_veio";
	unsigned char cp[8] = { 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef };
	const unsigned short len = sizeof(cp);
	int i;

	if (req == 1) {
		i = usb_control_msg(
			dev,
			usb_rcvctrlpipe(dev, 0),
			req,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			value,
			index,
			cp,
			len,
			HZ);
#if 0
		printk(KERN_DEBUG "USB => %02x%02x%02x%02x%02x%02x%02x%02x "
		       "(req=$%02x val=$%04x ind=$%04x len=%d.)\n",
		       cp[0],cp[1],cp[2],cp[3],cp[4],cp[5],cp[6],cp[7],
		       req, value, index, len);
#endif
	} else {
		i = usb_control_msg(
			dev,
			usb_sndctrlpipe(dev, 0),
			req,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
			value,
			index,
			NULL,
			0,
			HZ);
	}
	if (i < 0)
		printk(KERN_ERR "%s: ERROR=%d.\n", proc, i);
	return i;
}

/*
 * usb_ibmcam_calculate_fps()
 *
 * This procedure roughly calculates the real frame rate based
 * on FPS code (framerate=NNN option). Actual FPS differs
 * slightly depending on lighting conditions, so that actual frame
 * rate is determined by the camera. Since I don't know how to ask
 * the camera what FPS is now I have to use the FPS code instead.
 *
 * The FPS code is in range [0..6], 0 is slowest, 6 is fastest.
 * Corresponding real FPS should be in range [3..30] frames per second.
 * The conversion formula is obvious:
 *
 * real_fps = 3 + (fps_code * 4.5)
 *
 * History:
 * 1/18/99  Created.
 */
static int usb_ibmcam_calculate_fps(void)
{
	return 3 + framerate*4 + framerate/2;
}

/*
 * usb_ibmcam_send_FF_04_02()
 *
 * This procedure sends magic 3-command prefix to the camera.
 * The purpose of this prefix is not known.
 *
 * History:
 * 1/2/00   Created.
 */
static void usb_ibmcam_send_FF_04_02(struct usb_device *dev)
{
	usb_ibmcam_veio(dev, 0, 0x00FF, 0x0127);
	usb_ibmcam_veio(dev, 0, 0x0004, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0002, 0x0124);
}

static void usb_ibmcam_send_00_04_06(struct usb_device *dev)
{
	usb_ibmcam_veio(dev, 0, 0x0000, 0x0127);
	usb_ibmcam_veio(dev, 0, 0x0004, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0006, 0x0124);
}

static void usb_ibmcam_send_x_00(struct usb_device *dev, unsigned short x)
{
	usb_ibmcam_veio(dev, 0, x,      0x0127);
	usb_ibmcam_veio(dev, 0, 0x0000, 0x0124);
}

static void usb_ibmcam_send_x_00_05(struct usb_device *dev, unsigned short x)
{
	usb_ibmcam_send_x_00(dev, x);
	usb_ibmcam_veio(dev, 0, 0x0005, 0x0124);
}

static void usb_ibmcam_send_x_00_05_02(struct usb_device *dev, unsigned short x)
{
	usb_ibmcam_veio(dev, 0, x,      0x0127);
	usb_ibmcam_veio(dev, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0005, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0002, 0x0124);
}

static void usb_ibmcam_send_x_01_00_05(struct usb_device *dev, unsigned short x)
{
	usb_ibmcam_veio(dev, 0, x,      0x0127);
	usb_ibmcam_veio(dev, 0, 0x0001, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0005, 0x0124);
}

static void usb_ibmcam_send_x_00_05_02_01(struct usb_device *dev, unsigned short x)
{
	usb_ibmcam_veio(dev, 0, x,      0x0127);
	usb_ibmcam_veio(dev, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0005, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0002, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0001, 0x0124);
}

static void usb_ibmcam_send_x_00_05_02_08_01(struct usb_device *dev, unsigned short x)
{
	usb_ibmcam_veio(dev, 0, x,      0x0127);
	usb_ibmcam_veio(dev, 0, 0x0000, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0005, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0002, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0008, 0x0124);
	usb_ibmcam_veio(dev, 0, 0x0001, 0x0124);
}

static void usb_ibmcam_Packet_Format1(struct usb_device *dev, unsigned char fkey, unsigned char val)
{
	usb_ibmcam_send_x_01_00_05	(dev, unknown_88);
	usb_ibmcam_send_x_00_05		(dev, fkey);
	usb_ibmcam_send_x_00_05_02_08_01(dev, val);
	usb_ibmcam_send_x_00_05		(dev, unknown_88);
	usb_ibmcam_send_x_00_05_02_01	(dev, fkey);
	usb_ibmcam_send_x_00_05		(dev, unknown_89);
	usb_ibmcam_send_x_00		(dev, fkey);
	usb_ibmcam_send_00_04_06	(dev);
	usb_ibmcam_veio			(dev, 1, 0x0000, 0x0126);
	usb_ibmcam_send_FF_04_02	(dev);
}

static void usb_ibmcam_PacketFormat2(struct usb_device *dev, unsigned char fkey, unsigned char val)
{
	usb_ibmcam_send_x_01_00_05	(dev, unknown_88);
	usb_ibmcam_send_x_00_05		(dev, fkey);
	usb_ibmcam_send_x_00_05_02	(dev, val);
}

/*
 * usb_ibmcam_adjust_contrast()
 *
 * The contrast value changes from 0 (high contrast) to 15 (low contrast).
 * This is in reverse to usual order of things (such as TV controls), so
 * we reverse it again here.
 *
 * TODO: we probably don't need to send the setup 5 times...
 *
 * History:
 * 1/2/99   Created.
 */
static void usb_ibmcam_adjust_contrast(struct usb_ibmcam *ibmcam)
{
	struct usb_device *dev = ibmcam->dev;
	unsigned char new_contrast = ibmcam->vpic.contrast >> 12;
	const int ntries = 5;

	if (new_contrast >= 16)
		new_contrast = 15;
	new_contrast = 15 - new_contrast;
	if (new_contrast != ibmcam->vpic_old.contrast) {
		int i;
		ibmcam->vpic_old.contrast = new_contrast;
		for (i=0; i < ntries; i++) {
			usb_ibmcam_Packet_Format1(dev, contrast_14, new_contrast);
			usb_ibmcam_send_FF_04_02(dev);
		}
		/*usb_ibmcam_veio(dev, 0, 0x00FF, 0x0127);*/
	}
}

/*
 * usb_ibmcam_change_lighting_conditions()
 *
 * We have 3 levels of lighting conditions: 0=Bright, 1=Medium, 2=Low.
 * Low lighting forces slower FPS. Lighting is set as a module parameter.
 *
 * History:
 * 1/5/99   Created.
 */
static void usb_ibmcam_change_lighting_conditions(struct usb_ibmcam *ibmcam)
{
	static const char proc[] = "usb_ibmcam_change_lighting_conditions";
	struct usb_device *dev = ibmcam->dev;
	const int ntries = 5;
	int i;

	RESTRICT_TO_RANGE(lighting, LIGHTING_MIN, LIGHTING_MAX);
	if (debug > 0)
		printk(KERN_INFO "%s: Set lighting to %hu.\n", proc, lighting);

	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, light_27, (unsigned short) lighting);
}

static void usb_ibmcam_set_sharpness(struct usb_ibmcam *ibmcam)
{
	static const char proc[] = "usb_ibmcam_set_sharpness";
	struct usb_device *dev = ibmcam->dev;
	static const unsigned short sa[] = { 0x11, 0x13, 0x16, 0x18, 0x1a, 0x8, 0x0a };
	unsigned short i, sv;

	RESTRICT_TO_RANGE(sharpness, SHARPNESS_MIN, SHARPNESS_MAX);
	if (debug > 0)
		printk(KERN_INFO "%s: Set sharpness to %hu.\n", proc, sharpness);

	sv = sa[sharpness - SHARPNESS_MIN];
	for (i=0; i < 2; i++) {
		usb_ibmcam_send_x_01_00_05	(dev, unknown_88);
		usb_ibmcam_send_x_00_05		(dev, sharp_13);
		usb_ibmcam_send_x_00_05_02	(dev, sv);
	}
}

static void usb_ibmcam_set_brightness(struct usb_ibmcam *ibmcam)
{
	static const char proc[] = "usb_ibmcam_set_brightness";
	struct usb_device *dev = ibmcam->dev;
	static const unsigned short n = 1;
	unsigned short i, j, bv[3];

	bv[0] = bv[1] = bv[2] = ibmcam->vpic.brightness >> 10;
	if (bv[0] == (ibmcam->vpic_old.brightness >> 10))
		return;
	ibmcam->vpic_old.brightness = ibmcam->vpic.brightness;

	if (debug > 0)
		printk(KERN_INFO "%s: Set brightness to (%hu,%hu,%hu)\n",
		       proc, bv[0], bv[1], bv[2]);

	for (j=0; j < 3; j++)
		for (i=0; i < n; i++)
			usb_ibmcam_Packet_Format1(dev, bright_3x[j], bv[j]);
}

static void usb_ibmcam_adjust_picture(struct usb_ibmcam *ibmcam)
{
	usb_ibmcam_adjust_contrast(ibmcam);
	usb_ibmcam_set_brightness(ibmcam);
}

static void usb_ibmcam_setup_before_if1(struct usb_device *dev)
{
	switch (videosize) {
	case VIDEOSIZE_128x96:
	case VIDEOSIZE_176x144:
		usb_ibmcam_veio(dev, 1, 0x00, 0x0100);
		usb_ibmcam_veio(dev, 0, 0x01, 0x0100);
		usb_ibmcam_veio(dev, 0, 0x01, 0x0108);
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_veio(dev, 1, 0x00, 0x0100);
		usb_ibmcam_veio(dev, 0, 0x01, 0x0100);
		usb_ibmcam_veio(dev, 1, 0x00, 0x0100);
		usb_ibmcam_veio(dev, 0, 0x81, 0x0100);
		usb_ibmcam_veio(dev, 1, 0x00, 0x0100);
		usb_ibmcam_veio(dev, 0, 0x01, 0x0100);
		usb_ibmcam_veio(dev, 0, 0x01, 0x0108);
		break;
	}
}

static void usb_ibmcam_setup_before_if2(struct usb_device *dev)
{
	const int ntries = 5;
	int i;

	usb_ibmcam_veio(dev, 0, 0x03, 0x0112);
	usb_ibmcam_veio(dev, 1, 0x00, 0x0115);
	usb_ibmcam_veio(dev, 0, 0x06, 0x0115);
	usb_ibmcam_veio(dev, 1, 0x00, 0x0116);
	usb_ibmcam_veio(dev, 0, 0x44, 0x0116);
	usb_ibmcam_veio(dev, 1, 0x00, 0x0116);
	usb_ibmcam_veio(dev, 0, 0x40, 0x0116);
	usb_ibmcam_veio(dev, 1, 0x00, 0x0115);
	usb_ibmcam_veio(dev, 0, 0x0e, 0x0115);
	usb_ibmcam_veio(dev, 0, 0x19, 0x012c);

	usb_ibmcam_Packet_Format1(dev, 0x00, 0x1e);
	usb_ibmcam_Packet_Format1(dev, 0x39, 0x0d);
	usb_ibmcam_Packet_Format1(dev, 0x39, 0x09);
	usb_ibmcam_Packet_Format1(dev, 0x3b, 0x00);
	usb_ibmcam_Packet_Format1(dev, 0x28, 0x22);
	usb_ibmcam_Packet_Format1(dev, light_27, 0);
	usb_ibmcam_Packet_Format1(dev, 0x2b, 0x1f);
	usb_ibmcam_Packet_Format1(dev, 0x39, 0x08);

	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x2c, 0x00);

	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x30, 0x14);

	usb_ibmcam_PacketFormat2(dev, 0x39, 0x02);
	usb_ibmcam_PacketFormat2(dev, 0x01, 0xe1);
	usb_ibmcam_PacketFormat2(dev, 0x02, 0xcd);
	usb_ibmcam_PacketFormat2(dev, 0x03, 0xcd);
	usb_ibmcam_PacketFormat2(dev, 0x04, 0xfa);
	usb_ibmcam_PacketFormat2(dev, 0x3f, 0xff);
	usb_ibmcam_PacketFormat2(dev, 0x39, 0x00);

	usb_ibmcam_PacketFormat2(dev, 0x39, 0x02);
	usb_ibmcam_PacketFormat2(dev, 0x0a, 0x37);
	usb_ibmcam_PacketFormat2(dev, 0x0b, 0xb8);
	usb_ibmcam_PacketFormat2(dev, 0x0c, 0xf3);
	usb_ibmcam_PacketFormat2(dev, 0x0d, 0xe3);
	usb_ibmcam_PacketFormat2(dev, 0x0e, 0x0d);
	usb_ibmcam_PacketFormat2(dev, 0x0f, 0xf2);
	usb_ibmcam_PacketFormat2(dev, 0x10, 0xd5);
	usb_ibmcam_PacketFormat2(dev, 0x11, 0xba);
	usb_ibmcam_PacketFormat2(dev, 0x12, 0x53);
	usb_ibmcam_PacketFormat2(dev, 0x3f, 0xff);
	usb_ibmcam_PacketFormat2(dev, 0x39, 0x00);

	usb_ibmcam_PacketFormat2(dev, 0x39, 0x02);
	usb_ibmcam_PacketFormat2(dev, 0x16, 0x00);
	usb_ibmcam_PacketFormat2(dev, 0x17, 0x28);
	usb_ibmcam_PacketFormat2(dev, 0x18, 0x7d);
	usb_ibmcam_PacketFormat2(dev, 0x19, 0xbe);
	usb_ibmcam_PacketFormat2(dev, 0x3f, 0xff);
	usb_ibmcam_PacketFormat2(dev, 0x39, 0x00);

	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x00, 0x18);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x13, 0x18);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x14, 0x06);

	/* This is default brightness */
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x31, 0x37);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x32, 0x46);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x33, 0x55);

	usb_ibmcam_Packet_Format1(dev, 0x2e, 0x04);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x2d, 0x04);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x29, 0x80);
	usb_ibmcam_Packet_Format1(dev, 0x2c, 0x01);
	usb_ibmcam_Packet_Format1(dev, 0x30, 0x17);
	usb_ibmcam_Packet_Format1(dev, 0x39, 0x08);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x34, 0x00);

	usb_ibmcam_veio(dev, 0, 0x00, 0x0101);
	usb_ibmcam_veio(dev, 0, 0x00, 0x010a);

	switch (videosize) {
	case VIDEOSIZE_128x96:
		usb_ibmcam_veio(dev, 0, 0x80, 0x0103);
		usb_ibmcam_veio(dev, 0, 0x60, 0x0105);
		usb_ibmcam_veio(dev, 0, 0x0c, 0x010b);
		usb_ibmcam_veio(dev, 0, 0x04, 0x011b);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x0b, 0x011d);
		usb_ibmcam_veio(dev, 0, 0x00, 0x011e);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x00, 0x0129);
		break;
	case VIDEOSIZE_176x144:
		usb_ibmcam_veio(dev, 0, 0xb0, 0x0103);
		usb_ibmcam_veio(dev, 0, 0x8f, 0x0105);
		usb_ibmcam_veio(dev, 0, 0x06, 0x010b);
		usb_ibmcam_veio(dev, 0, 0x04, 0x011b);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x0d, 0x011d);
		usb_ibmcam_veio(dev, 0, 0x00, 0x011e);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x03, 0x0129);
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_veio(dev, 0, 0xb0, 0x0103);
		usb_ibmcam_veio(dev, 0, 0x90, 0x0105);
		usb_ibmcam_veio(dev, 0, 0x02, 0x010b);
		usb_ibmcam_veio(dev, 0, 0x04, 0x011b);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x05, 0x011d);
		usb_ibmcam_veio(dev, 0, 0x00, 0x011e);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x00, 0x0129);
		break;
	}

	usb_ibmcam_veio(dev, 0, 0xff, 0x012b);

	/* This is another brightness - don't know why */
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x31, 0xc3);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x32, 0xd2);
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, 0x33, 0xe1);

	/* Default contrast */
	for (i=0; i < ntries; i++)
		usb_ibmcam_Packet_Format1(dev, contrast_14, 0x0a);

	/* Default sharpness */
	for (i=0; i < 2; i++)
		usb_ibmcam_PacketFormat2(dev, sharp_13, 0x1a);	/* Level 4 FIXME */

	/* Default lighting conditions */
	usb_ibmcam_Packet_Format1(dev, light_27, lighting); /* 0=Bright 2=Low */

	/* Assorted init */

	switch (videosize) {
	case VIDEOSIZE_128x96:
		usb_ibmcam_Packet_Format1(dev, 0x2b, 0x1e);
		usb_ibmcam_veio(dev, 0, 0xc9, 0x0119);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x80, 0x0109);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x36, 0x0102);
		usb_ibmcam_veio(dev, 0, 0x1a, 0x0104);
		usb_ibmcam_veio(dev, 0, 0x04, 0x011a);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x2b, 0x011c);
		usb_ibmcam_veio(dev, 0, 0x23, 0x012a);	/* Same everywhere */
#if 0
		usb_ibmcam_veio(dev, 0, 0x00, 0x0106);
		usb_ibmcam_veio(dev, 0, 0x38, 0x0107);
#else
		usb_ibmcam_veio(dev, 0, 0x02, 0x0106);
		usb_ibmcam_veio(dev, 0, 0x2a, 0x0107);
#endif
		break;
	case VIDEOSIZE_176x144:
		usb_ibmcam_Packet_Format1(dev, 0x2b, 0x1e);
		usb_ibmcam_veio(dev, 0, 0xc9, 0x0119);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x80, 0x0109);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x04, 0x0102);
		usb_ibmcam_veio(dev, 0, 0x02, 0x0104);
		usb_ibmcam_veio(dev, 0, 0x04, 0x011a);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x2b, 0x011c);
		usb_ibmcam_veio(dev, 0, 0x23, 0x012a);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x01, 0x0106);
		usb_ibmcam_veio(dev, 0, 0xca, 0x0107);
		break;
	case VIDEOSIZE_352x288:
		usb_ibmcam_Packet_Format1(dev, 0x2b, 0x1f);
		usb_ibmcam_veio(dev, 0, 0xc9, 0x0119);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x80, 0x0109);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x08, 0x0102);
		usb_ibmcam_veio(dev, 0, 0x01, 0x0104);
		usb_ibmcam_veio(dev, 0, 0x04, 0x011a);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x2f, 0x011c);
		usb_ibmcam_veio(dev, 0, 0x23, 0x012a);	/* Same everywhere */
		usb_ibmcam_veio(dev, 0, 0x03, 0x0106);
		usb_ibmcam_veio(dev, 0, 0xf6, 0x0107);
		break;
	}
}

static void usb_ibmcam_setup_after_video_if(struct usb_device *dev)
{
	unsigned short internal_frame_rate;

	RESTRICT_TO_RANGE(framerate, FRAMERATE_MIN, FRAMERATE_MAX);
	internal_frame_rate = FRAMERATE_MAX - framerate; /* 0=Fast 6=Slow */
	usb_ibmcam_veio(dev, 0, internal_frame_rate, 0x0111);
	usb_ibmcam_veio(dev, 0, 0x01, 0x0114);
	usb_ibmcam_veio(dev, 0, 0xc0, 0x010c);
}

static void usb_ibmcam_setup_video_stop(struct usb_device *dev)
{
	usb_ibmcam_veio(dev, 0, 0x00, 0x010c);
	usb_ibmcam_veio(dev, 0, 0x00, 0x010c);
	usb_ibmcam_veio(dev, 0, 0x01, 0x0114);
	usb_ibmcam_veio(dev, 0, 0xc0, 0x010c);
	usb_ibmcam_veio(dev, 0, 0x00, 0x010c);
	usb_ibmcam_send_FF_04_02(dev);
	usb_ibmcam_veio(dev, 1, 0x00, 0x0100);
	usb_ibmcam_veio(dev, 0, 0x81, 0x0100);
}

/*
 * usb_ibmcam_reinit_iso()
 *
 * This procedure sends couple of commands to the camera and then
 * resets the video pipe. This sequence was observed to reinit the
 * camera or, at least, to initiate ISO data stream.
 *
 * History:
 * 1/2/00   Created.
 */
static void usb_ibmcam_reinit_iso(struct usb_ibmcam *ibmcam, int do_stop)
{
	if (do_stop)
		usb_ibmcam_setup_video_stop(ibmcam->dev);

	usb_ibmcam_veio(ibmcam->dev, 0, 0x0001, 0x0114);
	usb_ibmcam_veio(ibmcam->dev, 0, 0x00c0, 0x010c);
	usb_clear_halt(ibmcam->dev, ibmcam->video_endp);
	usb_ibmcam_setup_after_video_if(ibmcam->dev);
}

static int ibmcam_init_isoc(struct usb_ibmcam *ibmcam)
{
	struct usb_device *dev = ibmcam->dev;
	urb_t *urb;
	int fx, err;

	ibmcam->compress = 0;
	ibmcam->curframe = -1;
	ibmcam->cursbuf = 0;
	ibmcam->scratchlen = 0;

	/* Alternate interface 1 is is the biggest frame size */
	if (usb_set_interface(ibmcam->dev, 2, 1) < 0) {
		printk(KERN_ERR "usb_set_interface error\n");
		return -EBUSY;
	}
	usb_ibmcam_change_lighting_conditions(ibmcam);
	usb_ibmcam_set_sharpness(ibmcam);
	usb_ibmcam_reinit_iso(ibmcam, 0);

	/* We double buffer the Iso lists */
	urb = usb_alloc_urb(FRAMES_PER_DESC);
	
	if (!urb) {
		printk(KERN_ERR "ibmcam_init_isoc: usb_init_isoc ret %d\n",
			0);
		return -ENOMEM;
	}
	ibmcam->sbuf[0].urb = urb;
	urb->dev = dev;
	urb->context = ibmcam;
	urb->pipe = usb_rcvisocpipe(dev, ibmcam->video_endp);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = ibmcam->sbuf[0].data;
 	urb->complete = ibmcam_isoc_irq;
 	urb->number_of_packets = FRAMES_PER_DESC;
 	urb->transfer_buffer_length = ibmcam->iso_packet_len * FRAMES_PER_DESC;
 	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
 		urb->iso_frame_desc[fx].offset = ibmcam->iso_packet_len * fx;
		urb->iso_frame_desc[fx].length = ibmcam->iso_packet_len;
	}
	urb = usb_alloc_urb(FRAMES_PER_DESC);
	if (!urb) {
		printk(KERN_ERR "ibmcam_init_isoc: usb_init_isoc ret %d\n",
			0);
		return -ENOMEM;
	}
	ibmcam->sbuf[1].urb = urb;
	urb->dev = dev;
	urb->context = ibmcam;
	urb->pipe = usb_rcvisocpipe(dev, ibmcam->video_endp);
	urb->transfer_flags = USB_ISO_ASAP;
	urb->transfer_buffer = ibmcam->sbuf[1].data;
 	urb->complete = ibmcam_isoc_irq;
 	urb->number_of_packets = FRAMES_PER_DESC;
 	urb->transfer_buffer_length = ibmcam->iso_packet_len * FRAMES_PER_DESC;
 	for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
 		urb->iso_frame_desc[fx].offset = ibmcam->iso_packet_len * fx;
		urb->iso_frame_desc[fx].length = ibmcam->iso_packet_len;
	}

	ibmcam->sbuf[1].urb->next = ibmcam->sbuf[0].urb;
	ibmcam->sbuf[0].urb->next = ibmcam->sbuf[1].urb;
	
	err = usb_submit_urb(ibmcam->sbuf[0].urb);
	if (err)
		printk(KERN_ERR "ibmcam_init_isoc: usb_run_isoc(0) ret %d\n",
			err);
	err = usb_submit_urb(ibmcam->sbuf[1].urb);
	if (err)
		printk(KERN_ERR "ibmcam_init_isoc: usb_run_isoc(1) ret %d\n",
			err);

	ibmcam->streaming = 1;
	// printk(KERN_DEBUG "streaming=1 ibmcam->video_endp=$%02x\n", ibmcam->video_endp);
	return 0;
}

static void ibmcam_stop_isoc(struct usb_ibmcam *ibmcam)
{
	if (!ibmcam->streaming)
		return;

	usb_ibmcam_setup_video_stop(ibmcam->dev);

	/* Set packet size to 0 */
	if (usb_set_interface(ibmcam->dev, 2, 0) < 0) {
		printk(KERN_ERR "usb_set_interface error\n");
		return /* -EINVAL */;
	}

	/* Unschedule all of the iso td's */
	usb_unlink_urb(ibmcam->sbuf[1].urb);
	usb_unlink_urb(ibmcam->sbuf[0].urb);

	// printk(KERN_DEBUG "streaming=0\n");
	ibmcam->streaming = 0;

	/* Delete them all */
	usb_free_urb(ibmcam->sbuf[1].urb);
	usb_free_urb(ibmcam->sbuf[0].urb);
}

static int ibmcam_new_frame(struct usb_ibmcam *ibmcam, int framenum)
{
	struct ibmcam_frame *frame;
	int n, width, height;

	/* If we're not grabbing a frame right now and the other frame is */
	/*  ready to be grabbed into, then use it instead */
	if (ibmcam->curframe != -1)
		return 0;

	n = (framenum - 1 + IBMCAM_NUMFRAMES) % IBMCAM_NUMFRAMES;
	if (ibmcam->frame[n].grabstate == FRAME_READY)
		framenum = n;

	frame = &ibmcam->frame[framenum];

	frame->grabstate = FRAME_GRABBING;
	frame->scanstate = STATE_SCANNING;
	frame->scanlength = 0;		/* Accumulated in ibmcam_parse_data() */
	ibmcam->curframe = framenum;
#if 0
	/* This provides a "clean" frame but slows things down */
	memset(frame->data, 0, MAX_FRAME_SIZE);
#endif
	switch (videosize) {
	case VIDEOSIZE_128x96:
		frame->frmwidth = 128;
		frame->frmheight = 96;
		frame->order_uv = 1;	/* U Y V Y ... */
		frame->hdr_sig = 0x06;	/* 00 FF 00 06 */
		break;
	case VIDEOSIZE_176x144:
		frame->frmwidth = 176;
		frame->frmheight = 144;
		frame->order_uv = 1;	/* U Y V Y ... */
		frame->hdr_sig = 0x0E;	/* 00 FF 00 0E */
		break;
	case VIDEOSIZE_352x288:
		frame->frmwidth = 352;
		frame->frmheight = 288;
		frame->order_uv = 0;	/* V Y U Y ... */
		frame->hdr_sig = 0x00;	/* 00 FF 00 00 */
		break;
	}

	width = frame->width;
	RESTRICT_TO_RANGE(width, min_imgwidth, imgwidth);
	width &= ~7;		/* Multiple of 8 */

	height = frame->height;
	RESTRICT_TO_RANGE(height, min_imgheight, imgheight);
	height &= ~3;		/* Multiple of 4 */

	return 0;
}

/* Video 4 Linux API */
static int ibmcam_open(struct video_device *dev, int flags)
{
	int err = -EBUSY;
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;

	if (ibmcam->remove_pending)
		return -EFAULT;

	down(&ibmcam->lock);
	if (ibmcam->user)
		goto out_unlock;

	ibmcam->frame[0].grabstate = FRAME_UNUSED;
	ibmcam->frame[1].grabstate = FRAME_UNUSED;

	err = -ENOMEM;

	/* Allocate memory for the frame buffers */
	ibmcam->fbuf = rvmalloc(2 * MAX_FRAME_SIZE);
	if (!ibmcam->fbuf)
		goto open_err_ret;

	ibmcam->frame[0].data = ibmcam->fbuf;
	ibmcam->frame[1].data = ibmcam->fbuf + MAX_FRAME_SIZE;

	ibmcam->sbuf[0].data = kmalloc (FRAMES_PER_DESC * ibmcam->iso_packet_len, GFP_KERNEL);
	if (!ibmcam->sbuf[0].data)
		goto open_err_on0;

	ibmcam->sbuf[1].data = kmalloc (FRAMES_PER_DESC * ibmcam->iso_packet_len, GFP_KERNEL);
	if (!ibmcam->sbuf[1].data)
		goto open_err_on1;

	/* Set default sizes in case IOCTL (VIDIOCMCAPTURE) is not used
	 * (using read() instead). */
	ibmcam->frame[0].width = imgwidth;
	ibmcam->frame[0].height = imgheight;
	ibmcam->frame[0].bytes_read = 0;
	ibmcam->frame[1].width = imgwidth;
	ibmcam->frame[1].height = imgheight;
	ibmcam->frame[1].bytes_read = 0;

	err = ibmcam_init_isoc(ibmcam);
	if (err)
		goto open_err_on2;

	ibmcam->user++;
	up(&ibmcam->lock);

	MOD_INC_USE_COUNT;

	return 0;

open_err_on2:
	kfree (ibmcam->sbuf[1].data);
open_err_on1:
	kfree (ibmcam->sbuf[0].data);
open_err_on0:
	rvfree(ibmcam->fbuf, 2 * MAX_FRAME_SIZE);
open_err_ret:
	return err;

out_unlock:
	up(&ibmcam->lock);
	return err;
}

static void ibmcam_close(struct video_device *dev)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;

	down(&ibmcam->lock);	
	ibmcam->user--;

	MOD_DEC_USE_COUNT;

	ibmcam_stop_isoc(ibmcam);

	rvfree(ibmcam->fbuf, 2 * MAX_FRAME_SIZE);

	kfree(ibmcam->sbuf[1].data);
	kfree(ibmcam->sbuf[0].data);

	up(&ibmcam->lock);
}

static int ibmcam_init_done(struct video_device *dev)
{
	return 0;
}

static long ibmcam_write(struct video_device *dev, const char *buf, unsigned long count, int noblock)
{
	return -EINVAL;
}

static int ibmcam_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;

	if (ibmcam->remove_pending)
		return -EFAULT;

	switch (cmd) {
		case VIDIOCGCAP:
		{
			if (copy_to_user(arg, &ibmcam->vcap, sizeof(ibmcam->vcap)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGCHAN:
		{
			if (copy_to_user(arg, &ibmcam->vchan, sizeof(ibmcam->vchan)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSCHAN:
		{
			int v;

			if (copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			if ((v < 0) || (v >= 3)) /* 3 grades of lighting conditions */
				return -EINVAL;
			if (v != ibmcam->vchan.channel) {
				ibmcam->vchan.channel = v;
				usb_ibmcam_change_lighting_conditions(ibmcam);
			}
			return 0;
		}
		case VIDIOCGPICT:
		{
			if (copy_to_user(arg, &ibmcam->vpic, sizeof(ibmcam->vpic)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSPICT:
		{
			if (copy_from_user(&ibmcam->vpic, arg, sizeof(ibmcam->vpic)))
				return -EFAULT;
			usb_ibmcam_adjust_picture(ibmcam);
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
			if (vw.height != imgheight)
				return -EINVAL;
			if (vw.width != imgwidth)
				return -EINVAL;

			ibmcam->compress = 0;

			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;

			vw.x = 0;
			vw.y = 0;
			vw.width = imgwidth;
			vw.height = imgheight;
			vw.chromakey = 0;
			vw.flags = usb_ibmcam_calculate_fps();

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

			if (ibmcam->frame[vm.frame].grabstate == FRAME_GRABBING)
				return -EBUSY;

			/* Don't compress if the size changed */
			if ((ibmcam->frame[vm.frame].width != vm.width) ||
			    (ibmcam->frame[vm.frame].height != vm.height))
				ibmcam->compress = 0;

			ibmcam->frame[vm.frame].width = vm.width;
			ibmcam->frame[vm.frame].height = vm.height;

			/* Mark it as ready */
			ibmcam->frame[vm.frame].grabstate = FRAME_READY;

			return ibmcam_new_frame(ibmcam, vm.frame);
		}
		case VIDIOCSYNC:
		{
			int frame;

			if (copy_from_user((void *)&frame, arg, sizeof(int)))
				return -EFAULT;

			if (debug >= 1)
				printk(KERN_DEBUG "ibmcam: syncing to frame %d\n", frame);

			switch (ibmcam->frame[frame].grabstate) {
			case FRAME_UNUSED:
				return -EINVAL;
			case FRAME_READY:
			case FRAME_GRABBING:
			case FRAME_ERROR:
			{
				int ntries;
		redo:
				ntries = 0; 
				do {
					interruptible_sleep_on(&ibmcam->frame[frame].wq);
					if (signal_pending(current)) {
						if (flags & FLAGS_RETRY_VIDIOCSYNC) {
							/* Polling apps will destroy frames with that! */
							ibmcam_new_frame(ibmcam, frame);
							usb_ibmcam_testpattern(ibmcam, 1, 0);
							ibmcam->curframe = -1;
							ibmcam->frame_num++;

							/* This will request another frame. */
							if (waitqueue_active(&ibmcam->frame[frame].wq))
								wake_up_interruptible(&ibmcam->frame[frame].wq);
							return 0;
						} else {
							/* Standard answer: not ready yet! */
							return -EINTR;
						}
					}
				} while (ibmcam->frame[frame].grabstate == FRAME_GRABBING);

				if (ibmcam->frame[frame].grabstate == FRAME_ERROR) {
					int ret = ibmcam_new_frame(ibmcam, frame);
					if (ret < 0)
						return ret;
					goto redo;
				}
			}
			case FRAME_DONE:
				ibmcam->frame[frame].grabstate = FRAME_UNUSED;
				break;
			}

			ibmcam->frame[frame].grabstate = FRAME_UNUSED;

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

		case VIDIOCGTUNER:
		case VIDIOCSTUNER:

		case VIDIOCGFREQ:
		case VIDIOCSFREQ:

		case VIDIOCGAUDIO:
		case VIDIOCSAUDIO:
			return -EINVAL;

		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static long ibmcam_read(struct video_device *dev, char *buf, unsigned long count, int noblock)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;
	int frmx = -1;
	volatile struct ibmcam_frame *frame;

	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_read: %ld bytes, noblock=%d\n", count, noblock);

	if (ibmcam->remove_pending)
		return -EFAULT;

	if (!dev || !buf)
		return -EFAULT;

	/* See if a frame is completed, then use it. */
	if (ibmcam->frame[0].grabstate >= FRAME_DONE)	/* _DONE or _ERROR */
		frmx = 0;
	else if (ibmcam->frame[1].grabstate >= FRAME_DONE)/* _DONE or _ERROR */
		frmx = 1;

	if (noblock && (frmx == -1))
		return -EAGAIN;

	/* If no FRAME_DONE, look for a FRAME_GRABBING state. */
	/* See if a frame is in process (grabbing), then use it. */
	if (frmx == -1) {
		if (ibmcam->frame[0].grabstate == FRAME_GRABBING)
			frmx = 0;
		else if (ibmcam->frame[1].grabstate == FRAME_GRABBING)
			frmx = 1;
	}

	/* If no frame is active, start one. */
	if (frmx == -1)
		ibmcam_new_frame(ibmcam, frmx = 0);

	frame = &ibmcam->frame[frmx];

restart:
	while (frame->grabstate == FRAME_GRABBING) {
		interruptible_sleep_on((void *)&frame->wq);
		if (signal_pending(current))
			return -EINTR;
	}

	if (frame->grabstate == FRAME_ERROR) {
		frame->bytes_read = 0;
		if (ibmcam_new_frame(ibmcam, frmx))
			printk(KERN_ERR "ibmcam_read: ibmcam_new_frame error\n");
		goto restart;
	}

	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_read: frmx=%d, bytes_read=%ld, scanlength=%ld\n",
			frmx, frame->bytes_read, frame->scanlength);

	/* copy bytes to user space; we allow for partials reads */
	if ((count + frame->bytes_read) > frame->scanlength)
		count = frame->scanlength - frame->bytes_read;

	if (copy_to_user(buf, frame->data + frame->bytes_read, count))
		return -EFAULT;

	frame->bytes_read += count;
	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_read: {copy} count used=%ld, new bytes_read=%ld\n",
			count, frame->bytes_read);

	if (frame->bytes_read >= frame->scanlength) { /* All data has been read */
		frame->bytes_read = 0;

		/* Mark it as available to be used again. */
		ibmcam->frame[frmx].grabstate = FRAME_UNUSED;
		if (ibmcam_new_frame(ibmcam, frmx ? 0 : 1))
			printk(KERN_ERR "ibmcam_read: ibmcam_new_frame returned error\n");
	}

	return count;
}

static int ibmcam_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

	if (ibmcam->remove_pending)
		return -EFAULT;

	if (size > (((2 * MAX_FRAME_SIZE) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)))
		return -EINVAL;

	pos = (unsigned long)ibmcam->fbuf;
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

static struct video_device ibmcam_template = {
	"CPiA USB Camera",
	VID_TYPE_CAPTURE,
	VID_HARDWARE_CPIA,
	ibmcam_open,
	ibmcam_close,
	ibmcam_read,
	ibmcam_write,
	NULL,
	ibmcam_ioctl,
	ibmcam_mmap,
	ibmcam_init_done,
	NULL,
	0,
	0
};

static void usb_ibmcam_configure_video(struct usb_ibmcam *ibmcam)
{
	if (ibmcam == NULL)
		return;

	RESTRICT_TO_RANGE(init_brightness, 0, 255);
	RESTRICT_TO_RANGE(init_contrast, 0, 255);
	RESTRICT_TO_RANGE(init_color, 0, 255);
	RESTRICT_TO_RANGE(init_hue, 0, 255);

	memset(&ibmcam->vpic, 0, sizeof(ibmcam->vpic));
	memset(&ibmcam->vpic_old, 0x55, sizeof(ibmcam->vpic_old));

	ibmcam->vpic.colour = init_color << 8;
	ibmcam->vpic.hue = init_hue << 8;
	ibmcam->vpic.brightness = init_brightness << 8;
	ibmcam->vpic.contrast = init_contrast << 8;
	ibmcam->vpic.whiteness = 105 << 8; /* This one isn't used */
	ibmcam->vpic.depth = 24;
	ibmcam->vpic.palette = VIDEO_PALETTE_RGB24;

	memset(&ibmcam->vcap, 0, sizeof(ibmcam->vcap));
	strcpy(ibmcam->vcap.name, "IBM USB Camera");
	ibmcam->vcap.type = VID_TYPE_CAPTURE /*| VID_TYPE_SUBCAPTURE*/;
	ibmcam->vcap.channels = 1;
	ibmcam->vcap.audios = 0;
	ibmcam->vcap.maxwidth = imgwidth;
	ibmcam->vcap.maxheight = imgheight;
	ibmcam->vcap.minwidth = min_imgwidth;
	ibmcam->vcap.minheight = min_imgheight;

	memset(&ibmcam->vchan, 0, sizeof(ibmcam->vchan));
	ibmcam->vchan.flags = 0;
	ibmcam->vchan.tuners = 0;
	ibmcam->vchan.channel = 0;
	ibmcam->vchan.type = VIDEO_TYPE_CAMERA;
	strcpy(ibmcam->vchan.name, "Camera");
}

static void *usb_ibmcam_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_ibmcam *ibmcam = NULL;
	struct usb_interface_descriptor *interface;

	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_probe(%p,%u.)\n", dev, ifnum);

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return NULL;

	/* Is it an IBM camera? */
	if ((dev->descriptor.idVendor != 0x0545) ||
	    (dev->descriptor.idProduct != 0x8080))
		return NULL;

	/* Camera confirmed. We claim only interface 2 (video data) */
	if (ifnum != 2)
		return NULL;

	/* We found an IBM camera */
	printk(KERN_INFO "IBM USB camera found (interface %u.)\n", ifnum);

	if (debug >= 1)
		printk(KERN_DEBUG "ibmcam_probe: new ibmcam alloc\n");
	ibmcam = kmalloc(sizeof(*ibmcam), GFP_KERNEL);
	if (ibmcam == NULL) {
		printk(KERN_ERR "couldn't kmalloc ibmcam struct\n");
		return NULL;
	}
	memset(ibmcam, 0, sizeof(struct usb_ibmcam));
	ibmcam->dev = dev;
	interface = &dev->actconfig->interface[ifnum].altsetting[0];
	ibmcam->iface = interface->bInterfaceNumber;
	ibmcam->video_endp = 0x82;
	ibmcam->scratch = kmalloc(scratchbufsize, GFP_KERNEL);
	if (ibmcam->scratch == NULL) {
		printk(KERN_ERR "couldn't kmalloc ibmcam->scratch\n");
		kfree(ibmcam);
		return NULL;
	}
	init_waitqueue_head (&ibmcam->remove_ok);
	ibmcam->iso_packet_len = 1014;

	/* Camera setup */
	usb_ibmcam_veio(dev, 1, 0, 0x128);
	usb_ibmcam_setup_before_if1(dev);
	usb_ibmcam_setup_before_if2(dev);

	memcpy(&ibmcam->vdev, &ibmcam_template, sizeof(ibmcam_template));
	usb_ibmcam_configure_video(ibmcam);

	init_waitqueue_head(&ibmcam->frame[0].wq);
	init_waitqueue_head(&ibmcam->frame[1].wq);

	if (video_register_device(&ibmcam->vdev, VFL_TYPE_GRABBER) == -1) {
		printk(KERN_ERR "video_register_device failed\n");
		return NULL;
	}
	if (debug > 1)
		printk(KERN_DEBUG "video_register_device() successful\n");

	ibmcam->compress = 0;
	ibmcam->user=0; 
	init_MUTEX(&ibmcam->lock);	/* to 1 == available */

	return ibmcam;
}

static void usb_ibmcam_disconnect(struct usb_device *dev, void *ptr)
{
	static const char proc[] = "usb_ibmcam_disconnect";
	struct usb_ibmcam *ibmcam = (struct usb_ibmcam *) ptr;

	if (debug > 0)
		printk(KERN_DEBUG "%s(%p,%p.)\n", proc, dev, ptr);

	ibmcam->remove_pending = 1;
	/* Now all ISO data will be ignored */

	/* At this time we ask to cancel outstanding URBs */
	ibmcam_stop_isoc(ibmcam);

	/* sleep_on(&s->remove_ok); */
	/* Now it should be safe to remove */

	video_unregister_device(&ibmcam->vdev);

	/* Free the memory */
	if (debug > 0)
		printk(KERN_DEBUG "%s: freeing ibmcam=%p\n", proc, ibmcam);
	if (ibmcam->scratch != NULL)
		kfree(ibmcam->scratch);
	kfree(ibmcam);
}

static struct usb_driver ibmcam_driver = {
	"ibmcam",
	usb_ibmcam_probe,
	usb_ibmcam_disconnect,
	{ NULL, NULL }
};

int usb_ibmcam_init(void)
{
	return usb_register(&ibmcam_driver);
}

void usb_ibmcam_cleanup(void)
{
	usb_deregister(&ibmcam_driver);
}

#ifdef MODULE
int init_module(void)
{
	return usb_ibmcam_init();
}

void cleanup_module(void)
{
	usb_ibmcam_cleanup();
}
#endif
