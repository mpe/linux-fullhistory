/*
 *  linux/drivers/video/retz3.c -- Low level frame buffer operations for the
 *				   RetinaZ3 (accelerated)
 *
 *	Created 5 Apr 1997 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/fb.h>

#include "fbcon.h"


/*
 *  Prototypes
 */

static int open_retz3(struct display *p);
static void release_retz3(void);
static void bmove_retz3(struct display *p, int sy, int sx, int dy, int dx,
	                int height, int width);
static void clear_retz3(struct vc_data *conp, struct display *p, int
			sy, int sx, int height, int width);
static void putc_retz3(struct vc_data *conp, struct display *p, int c,
		       int ypos, int xpos);
static void putcs_retz3(struct vc_data *conp, struct display *p, const
			char *s, int count, int ypos, int xpos);
static void rev_char_retz3(struct display *p, int xpos, int ypos);


    /*
     *  Acceleration functions in retz3fb.c
     */

extern void retz3_bitblt(struct fb_var_screeninfo *scr,
			 unsigned short srcx, unsigned short srcy, unsigned
			 short destx, unsigned short desty, unsigned short
			 width, unsigned short height, unsigned short cmd,
			 unsigned short mask);

#define Z3BLTcopy		0xc0
#define Z3BLTset		0xf0


/*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_retz3 = {
	open_retz3, release_retz3, bmove_retz3, clear_retz3, putc_retz3,
	putcs_retz3, rev_char_retz3
};


/*
     *  RetinaZ3 (accelerated)
     */

static int open_retz3(struct display *p)
{
	if (p->type != FB_TYPE_PACKED_PIXELS ||
	    p->var.accel != FB_ACCEL_RETINAZ3)
		return -EINVAL;

	p->next_line = p->var.xres_virtual*p->var.bits_per_pixel>>3;
	p->next_plane = 0;
	MOD_INC_USE_COUNT;
	return 0;
}

static void release_retz3(void)
{
	MOD_DEC_USE_COUNT;
}

static void bmove_retz3(struct display *p, int sy, int sx, int dy, int dx,
	                int height, int width)
{
	int fontwidth = p->fontwidth;

	sx *= fontwidth;
	dx *= fontwidth;
	width *= fontwidth;

	retz3_bitblt(&p->var,
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)dx,
		     (unsigned short)(dy*p->fontheight),
		     (unsigned short)width,
		     (unsigned short)(height*p->fontheight),
		     Z3BLTcopy,
		     0xffff);
}

static void clear_retz3(struct vc_data *conp, struct display *p, int
			sy, int sx, int height, int width)
{
	unsigned short col;
	int fontwidth = p->fontwidth;

	sx *= fontwidth;
	width *= fontwidth;

	col = attr_bgcol_ec(p, conp);
	col &= 0xff;
	col |= (col << 8);

	retz3_bitblt(&p->var,
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)width,
		     (unsigned short)(height*p->fontheight),
		     Z3BLTset,
		     col);
}

static void putc_retz3(struct vc_data *conp, struct display *p,
		       int c, int ypos, int xpos)
{
	unsigned char *dest, *cdat;
	unsigned long tmp;
	unsigned int rows, revs, underl;
	unsigned char d;
	unsigned char fg, bg;

	c &= 0xff;

	dest = p->screen_base + ypos * p->fontheight * p->next_line +
	        xpos*p->fontwidth;
	cdat = p->fontdata + c * p->fontheight;

	fg = p->fgcol;
	bg = p->bgcol;
	revs = conp->vc_reverse;
	underl = conp->vc_underline;

	for (rows = p->fontheight; rows--; dest += p->next_line) {
		d = *cdat++;

		if (underl && !rows)
			d = 0xff;
		if (revs)
			d = ~d;

		tmp =  ((d & 0x80) ? fg : bg) << 24;
		tmp |= ((d & 0x40) ? fg : bg) << 16;
		tmp |= ((d & 0x20) ? fg : bg) << 8;
		tmp |= ((d & 0x10) ? fg : bg);
		*((unsigned long*) dest) = tmp;
		tmp =  ((d & 0x8) ? fg : bg) << 24;
		tmp |= ((d & 0x4) ? fg : bg) << 16;
		tmp |= ((d & 0x2) ? fg : bg) << 8;
		tmp |= ((d & 0x1) ? fg : bg);
		*((unsigned long*) dest + 1) = tmp;
	}
}

static void putcs_retz3(struct vc_data *conp, struct display *p,
			const char *s, int count, int ypos, int xpos)
{
	unsigned char *dest, *dest0, *cdat;
	unsigned long tmp;
	unsigned int rows, revs, underl;
	unsigned char c, d;
	unsigned char fg, bg;

	dest0 = p->screen_base + ypos * p->fontheight * p->next_line
		+ xpos * p->fontwidth;

	fg = p->fgcol;
	bg = p->bgcol;
	revs = conp->vc_reverse;
	underl = conp->vc_underline;

	while (count--) {
		c = *s++;
		dest = dest0;
		dest0 += 8;

		cdat = p->fontdata + c * p->fontheight;
		for (rows = p->fontheight; rows--; dest += p->next_line) {
			d = *cdat++;

			if (underl && !rows)
				d = 0xff;
			if (revs)
				d = ~d;

			tmp =  ((d & 0x80) ? fg : bg) << 24;
			tmp |= ((d & 0x40) ? fg : bg) << 16;
			tmp |= ((d & 0x20) ? fg : bg) << 8;
			tmp |= ((d & 0x10) ? fg : bg);
			*((unsigned long*) dest) = tmp;
			tmp =  ((d & 0x8) ? fg : bg) << 24;
			tmp |= ((d & 0x4) ? fg : bg) << 16;
			tmp |= ((d & 0x2) ? fg : bg) << 8;
			tmp |= ((d & 0x1) ? fg : bg);
			*((unsigned long*) dest + 1) = tmp;
		}
	}
}

static void rev_char_retz3(struct display *p, int xpos, int ypos)
{
	unsigned char *dest;
	int bytes=p->next_line, rows;
	unsigned int bpp, mask;

	bpp = p->var.bits_per_pixel;

	switch (bpp){
	case 8:
		mask = 0x0f0f0f0f;
		break;
	case 16:
		mask = 0xffffffff;
		break;
	case 24:
		mask = 0xffffffff; /* ??? */
		break;
	default:
		printk("illegal depth for rev_char_retz3(), bpp = %i\n", bpp);
		return;
	}

	dest = p->screen_base + ypos * p->fontheight * bytes +
		xpos * p->fontwidth;

	for (rows = p->fontheight ; rows-- ; dest += bytes) {
		((unsigned long *)dest)[0] ^= mask;
		((unsigned long *)dest)[1] ^= mask;
	}
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_retz3(void)
#endif
{
	return(fbcon_register_driver(&dispsw_retz3, 1));
}

#ifdef MODULE
void cleanup_module(void)
{
	fbcon_unregister_driver(&dispsw_retz3);
}
#endif /* MODULE */
