#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/apollohw.h>
#include <linux/fb.h>
#include <linux/module.h>

#include <video/fbcon.h>

/* apollo video HW definitions */

/*
 * Control Registers.   IOBASE + $x
 *
 * Note: these are the Memory/IO BASE definitions for a mono card set to the
 * alternate address
 *
 * Control 3A and 3B serve identical functions except that 3A
 * deals with control 1 and 3b deals with Color LUT reg.
 */

#define AP_IOBASE       0x3b0	/* Base address of 1 plane board. */
#define AP_STATUS       isaIO2mem(AP_IOBASE+0)	/* Status register.  Read */
#define AP_WRITE_ENABLE isaIO2mem(AP_IOBASE+0)	/* Write Enable Register Write */
#define AP_DEVICE_ID    isaIO2mem(AP_IOBASE+1)	/* Device ID Register. Read */
#define AP_ROP_1        isaIO2mem(AP_IOBASE+2)	/* Raster Operation reg. Write Word */
#define AP_DIAG_MEM_REQ isaIO2mem(AP_IOBASE+4)	/* Diagnostic Memory Request. Write Word */
#define AP_CONTROL_0    isaIO2mem(AP_IOBASE+8)	/* Control Register 0.  Read/Write */
#define AP_CONTROL_1    isaIO2mem(AP_IOBASE+0xa)	/* Control Register 1.  Read/Write */
#define AP_CONTROL_3A   isaIO2mem(AP_IOBASE+0xe)	/* Control Register 3a. Read/Write */
#define AP_CONTROL_2    isaIO2mem(AP_IOBASE+0xc)	/* Control Register 2. Read/Write */


#define FRAME_BUFFER_START 0x0FA0000
#define FRAME_BUFFER_LEN 0x40000

/* CREG 0 */
#define VECTOR_MODE 0x40	/* 010x.xxxx */
#define DBLT_MODE   0x80	/* 100x.xxxx */
#define NORMAL_MODE 0xE0	/* 111x.xxxx */
#define SHIFT_BITS  0x1F	/* xxx1.1111 */
	/* other bits are Shift value */

/* CREG 1 */
#define AD_BLT      0x80	/* 1xxx.xxxx */
#define NORMAL      0x80 /* 1xxx.xxxx */	/* What is happening here ?? */
#define INVERSE     0x00 /* 0xxx.xxxx */	/* Clearing this reverses the screen */
#define PIX_BLT     0x00	/* 0xxx.xxxx */

#define AD_HIBIT        0x40	/* xIxx.xxxx */

#define ROP_EN          0x10	/* xxx1.xxxx */
#define DST_EQ_SRC      0x00	/* xxx0.xxxx */
#define nRESET_SYNC     0x08	/* xxxx.1xxx */
#define SYNC_ENAB       0x02	/* xxxx.xx1x */

#define BLANK_DISP      0x00	/* xxxx.xxx0 */
#define ENAB_DISP       0x01	/* xxxx.xxx1 */

#define NORM_CREG1      (nRESET_SYNC | SYNC_ENAB | ENAB_DISP)	/* no reset sync */

/* CREG 2 */

/*
 * Following 3 defines are common to 1, 4 and 8 plane.
 */

#define S_DATA_1s   0x00 /* 00xx.xxxx */	/* set source to all 1's -- vector drawing */
#define S_DATA_PIX  0x40 /* 01xx.xxxx */	/* takes source from ls-bits and replicates over 16 bits */
#define S_DATA_PLN  0xC0 /* 11xx.xxxx */	/* normal, each data access =16-bits in
						   one plane of image mem */

/* CREG 3A/CREG 3B */
#       define RESET_CREG 0x80	/* 1000.0000 */

/* ROP REG  -  all one nibble */
/*      ********* NOTE : this is used r0,r1,r2,r3 *********** */
#define ROP(r2,r3,r0,r1) ( (U_SHORT)((r0)|((r1)<<4)|((r2)<<8)|((r3)<<12)) )
#define DEST_ZERO               0x0
#define SRC_AND_DEST    0x1
#define SRC_AND_nDEST   0x2
#define SRC                             0x3
#define nSRC_AND_DEST   0x4
#define DEST                    0x5
#define SRC_XOR_DEST    0x6
#define SRC_OR_DEST             0x7
#define SRC_NOR_DEST    0x8
#define SRC_XNOR_DEST   0x9
#define nDEST                   0xA
#define SRC_OR_nDEST    0xB
#define nSRC                    0xC
#define nSRC_OR_DEST    0xD
#define SRC_NAND_DEST   0xE
#define DEST_ONE                0xF

#define SWAP(A) ((A>>8) | ((A&0xff) <<8))

#if 0
#define outb(a,d) *(char *)(a)=(d)
#define outw(a,d) *(unsigned short *)a=d
#endif

static struct fb_info fb_info;
static struct display disp;

/* frame buffer operations */

static int dn_fb_blank(int blank, struct fb_info *info);
static void dnfb_copyarea(struct fb_info *info, struct fb_copyarea *area);

static struct fb_ops dn_fb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	gen_get_fix,
	fb_get_var:	gen_get_var,
	fb_set_var:	gen_set_var,
	fb_get_cmap:	gen_get_cmap,
	fb_set_cmap:	gen_set_cmap,
	fb_blank:	dnfb_blank,
	fb_fillrect:	cfb_fillrect,
	fb_copyarea:	dnfb_copyarea,
	fb_imageblit:	cfb_imageblit,
};

struct fb_var_screeninfo dnfb_var __initdata = {
	xres:		1280,
	yres:		1024,
	xres_virtual:	2048,
	yres_virtual:	1024,
	bits_per_pixel:	1,
	height:		-1,
	width:		-1,
	vmode:		FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo dnfb_fix __initdata = {
	id:		"Apollo Mono",
	smem_start:	(FRAME_BUFFER_START + IO_BASE),
	smem_len:	FRAME_BUFFER_LEN,
	type:		FB_TYPE_PACKED_PIXELS,
	visual:		FB_VISUAL_MONO10,
	line_length:	256,
};

static int dnfb_blank(int blank, struct fb_info *info)
{
	if (blank)
		outb(0x0, AP_CONTROL_3A);
	else
		outb(0x1, AP_CONTROL_3A);
	return 0;
}

static 
void dnfb_copyarea(struct fb_info *info, struct fb_copyarea *area)
{

	int incr, y_delta, pre_read = 0, x_end, x_word_count;
	int x_count, y_count;
	ushort *src, dummy;
	uint start_mask, end_mask, dest;
	short i, j;

	incr = (area->dy <= area->sy) ? 1 : -1;

	src =
	    (ushort *) (info->screen_base + area->sy * info->fix.next_line +
			(area->sx >> 4));
	dest = area->dy * (info->fix.next_line >> 1) + (area->dx >> 4);

	if (incr > 0) {
		y_delta = (info->fix.next_line * 8) - area->sx - x_count;
		x_end = area->dx + x_count - 1;
		x_word_count = (x_end >> 4) - (area->dx >> 4) + 1;
		start_mask = 0xffff0000 >> (area->dx & 0xf);
		end_mask = 0x7ffff >> (x_end & 0xf);
		outb((((area->dx & 0xf) - (area->sx & 0xf)) % 16) | (0x4 << 5),
		     AP_CONTROL_0);
		if ((area->dx & 0xf) < (area->sx & 0xf))
			pre_read = 1;
	} else {
		y_delta = -((info->fix.next_line * 8) - area->sx - x_count);
		x_end = area->dx - x_count + 1;
		x_word_count = (area->dx >> 4) - (x_end >> 4) + 1;
		start_mask = 0x7ffff >> (area->dx & 0xf);
		end_mask = 0xffff0000 >> (x_end & 0xf);
		outb(((-((area->sx & 0xf) - (area->dx & 0xf))) %
		      16) | (0x4 << 5), AP_CONTROL_0);
		if ((area->dx & 0xf) > (area->sx & 0xf))
			pre_read = 1;
	}

	for (i = 0; i < y_count; i++) {

		outb(0xc | (dest >> 16), AP_CONTROL_3A);

		if (pre_read) {
			dummy = *src;
			src += incr;
		}

		if (x_word_count) {
			outb(start_mask, AP_WRITE_ENABLE);
			*src = dest;
			src += incr;
			dest += incr;
			outb(0, AP_WRITE_ENABLE);

			for (j = 1; j < (x_word_count - 1); j++) {
				*src = dest;
				src += incr;
				dest += incr;
			}

			outb(start_mask, AP_WRITE_ENABLE);
			*src = dest;
			dest += incr;
			src += incr;
		} else {
			outb(start_mask | end_mask, AP_WRITE_ENABLE);
			*src = dest;
			dest += incr;
			src += incr;
		}
		src += (y_delta / 16);
		dest += (y_delta / 16);
	}
	outb(NORMAL_MODE, AP_CONTROL_0);
}


unsigned long __init dnfb_init(unsigned long mem_start)
{
	int err;

	strcpy(&fb_info.modename, dnfb_fix);
	fb_info.changevar = NULL;
	fb_info.fontname[0] = 0;
	fb_info.disp = &disp;
	fb_info.switch_con = gen_switch;
	fb_info.updatevar = gen_update_var;
	fb_info.node = NODEV;
	fb_info.fbops = &dn_fb_ops;
	fb_info.currcon = -1;
	fb_info.fix = dnfb_fix;
	fb_info.var = dnfb_var;

	fb_alloc_cmap(&fb_info.cmap, 2, 0);
	gen_set_disp(-1, &fb_info);

	fb_info.screen_base = (u_char *) fb_info.fix.smem_start;

	err = register_framebuffer(&fb_info);
	if (err < 0)
		panic("unable to register apollo frame buffer\n");

	/* now we have registered we can safely setup the hardware */
	outb(RESET_CREG, AP_CONTROL_3A);
	outw(0x0, AP_WRITE_ENABLE);
	outb(NORMAL_MODE, AP_CONTROL_0);
	outb((AD_BLT | DST_EQ_SRC | NORM_CREG1), AP_CONTROL_1);
	outb(S_DATA_PLN, AP_CONTROL_2);
	outw(SWAP(0x3), AP_ROP_1);

	printk("apollo frame buffer alive and kicking !\n");
	return mem_start;
}

MODULE_LICENSE("GPL");
