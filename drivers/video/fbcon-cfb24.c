/*
 *  linux/drivers/video/cfb24.c -- Low level frame buffer operations for 24 bpp
 *				   truecolor packed pixels
 *
 *	Created 7 Mar 1998 by Geert Uytterhoeven
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

#include <video/fbcon.h>
#include <video/fbcon-cfb24.h>


    /*
     *  24 bpp packed pixels
     */

void fbcon_cfb24_setup(struct display *p)
{
    p->next_line = p->line_length ? p->line_length : p->var.xres_virtual*3;
    p->next_plane = 0;
}

void fbcon_cfb24_bmove(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
    int bytes = p->next_line, linesize = bytes * fontheight(p), rows;
    u8 *src, *dst;

    if (sx == 0 && dx == 0 && width * fontwidth(p) * 3 == bytes) {
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
	return;
    }
    if (fontwidthlog(p)) {
	sx <<= fontwidthlog(p);
	dx <<= fontwidthlog(p);
	width <<= fontwidthlog(p);
    } else {
	sx *= fontwidth(p);
	dx *= fontwidth(p);
	width *= fontwidth(p);
    }
    sx *= 3; dx *= 3; width *= 3;
    if (dy < sy || (dy == sy && dx < sx)) {
	src = p->screen_base + sy * linesize + sx;
	dst = p->screen_base + dy * linesize + dx;
	for (rows = height * fontheight(p); rows--;) {
	    mymemmove(dst, src, width);
	    src += bytes;
	    dst += bytes;
	}
    } else {
	src = p->screen_base + (sy+height) * linesize + sx - bytes;
	dst = p->screen_base + (dy+height) * linesize + dx - bytes;
	for (rows = height * fontheight(p); rows--;) {
	    mymemmove(dst, src, width);
	    src -= bytes;
	    dst -= bytes;
	}
    }
}

#if defined(__BIG_ENDIAN)
#define convert4to3(in1, in2, in3, in4, out1, out2, out3) \
    do { \
	out1 = (in1<<8)  | (in2>>16); \
	out2 = (in2<<16) | (in3>>8); \
	out3 = (in3<<24) | in4; \
    } while (0);
#elif defined(__LITTLE_ENDIAN)
#define convert4to3(in1, in2, in3, in4, out1, out2, out3) \
    do { \
	out1 = in1       | (in2<<24); \
	out2 = (in2>> 8) | (in3<<16); \
	out3 = (in3>>16) | (in4<< 8); \
    } while (0);
#else
#error FIXME: No endianness??
#endif

static inline void store4pixels(u32 d1, u32 d2, u32 d3, u32 d4, u32 *dest)
{
    convert4to3(d1, d2, d3, d4, *dest++, *dest++, *dest++);
}

static inline void rectfill(u8 *dest, int width, int height, u32 data,
			    int linesize)
{
    u32 d1, d2, d3;
    int i;

    convert4to3(data, data, data, data, d1, d2, d3);
    while (height-- > 0) {
	u32 *p = (u32 *)dest;
	for (i = 0; i < width/4; i++) {
	    *p++ = d1;
	    *p++ = d2;
	    *p++ = d3;
	}
	dest += linesize;
    }
}

void fbcon_cfb24_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u8 *dest;
    int bytes = p->next_line, lines = height * fontheight(p);
    u32 bgx;

    dest = p->screen_base + sy * fontheight(p) * bytes + sx * fontwidth(p) * 3;

    bgx = ((u32 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];

    width *= fontwidth(p)/4;
    if (width * 12 == bytes)
	rectfill(dest, lines * width * 4, 1, bgx, bytes);
    else
	rectfill(dest, width * 4, lines, bgx, bytes);
}

void fbcon_cfb24_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat, bits;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx, d1, d2, d3, d4;

    dest = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p) * 3;
    if (fontwidth(p) <= 8)
	cdat = p->fontdata + (c & p->charmask) * fontheight(p);
    else
	cdat = p->fontdata + ((c & p->charmask) * fontheight(p) << 1);

    fgx = ((u32 *)p->dispsw_data)[attr_fgcol(p, c)];
    bgx = ((u32 *)p->dispsw_data)[attr_bgcol(p, c)];
    eorx = fgx ^ bgx;

    for (rows = fontheight(p); rows--; dest += bytes) {
	bits = *cdat++;
	d1 = (-(bits >> 7) & eorx) ^ bgx;
	d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)dest);
	if (fontwidth(p) < 8)
	    continue;
	d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	d4 = (-(bits & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)(dest+12));
	if (fontwidth(p) < 12)
	    continue;
	bits = *cdat++;
	d1 = (-(bits >> 7) & eorx) ^ bgx;
	d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)(dest+24));
	if (fontwidth(p) < 16)
	    continue;
	d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	d4 = (-(bits & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)(dest+32));
    }
}

void fbcon_cfb24_putcs(struct vc_data *conp, struct display *p,
		       const unsigned short *s, int count, int yy, int xx)
{
    u8 *cdat, *dest, *dest0, bits;
    u16 c;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx, d1, d2, d3, d4;

    dest0 = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p) * 3;
    fgx = ((u32 *)p->dispsw_data)[attr_fgcol(p, *s)];
    bgx = ((u32 *)p->dispsw_data)[attr_bgcol(p, *s)];
    eorx = fgx ^ bgx;
    while (count--) {
	c = *s++ & p->charmask;
	if (fontwidth(p) <= 8)
	    cdat = p->fontdata + c * fontheight(p);
	  
	else
	    cdat = p->fontdata + (c * fontheight(p) << 1);
	for (rows = fontheight(p), dest = dest0; rows--; dest += bytes) {
	    bits = *cdat++;
	    d1 = (-(bits >> 7) & eorx) ^ bgx;
	    d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)dest);
	    if (fontwidth(p) < 8)
		continue;
	    d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    d4 = (-(bits & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)(dest+12));
	    if (fontwidth(p) < 12)
		continue;
	    bits = *cdat++;
	    d1 = (-(bits >> 7) & eorx) ^ bgx;
	    d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)(dest+24));
	    if (fontwidth(p) < 16)
		continue;
	    d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    d4 = (-(bits & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)(dest+32));
	}
	dest0 += fontwidth(p)*3;
    }
}

void fbcon_cfb24_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p) * 3;
    for (rows = fontheight(p); rows--; dest += bytes) {
	switch (fontwidth(p)) {
	case 16:
	    ((u32 *)dest)[9] ^= 0xffffffff; ((u32 *)dest)[10] ^= 0xffffffff;
	    ((u32 *)dest)[11] ^= 0xffffffff;	/* FALL THROUGH */
	case 12:
	    ((u32 *)dest)[6] ^= 0xffffffff; ((u32 *)dest)[7] ^= 0xffffffff;
	    ((u32 *)dest)[8] ^= 0xffffffff;	/* FALL THROUGH */
	case 8:
	    ((u32 *)dest)[3] ^= 0xffffffff; ((u32 *)dest)[4] ^= 0xffffffff;
	    ((u32 *)dest)[5] ^= 0xffffffff;	/* FALL THROUGH */
	case 4:
	    ((u32 *)dest)[0] ^= 0xffffffff; ((u32 *)dest)[1] ^= 0xffffffff;
	    ((u32 *)dest)[2] ^= 0xffffffff;
	}
    }
}

void fbcon_cfb24_clear_margins(struct vc_data *conp, struct display *p,
			       int bottom_only)
{
    int bytes = p->next_line;
    u32 bgx;

    unsigned int right_start = conp->vc_cols*fontwidth(p);
    unsigned int bottom_start = conp->vc_rows*fontheight(p);
    unsigned int right_width, bottom_width;

    bgx = ((u32 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];

    if (!bottom_only && (right_width = p->var.xres-right_start))
	rectfill(p->screen_base+right_start*3, right_width,
		 p->var.yres_virtual, bgx, bytes);
    if ((bottom_width = p->var.yres-bottom_start))
	rectfill(p->screen_base+(p->var.yoffset+bottom_start)*bytes,
		 right_start, bottom_width, bgx, bytes);
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb24 = {
    fbcon_cfb24_setup, fbcon_cfb24_bmove, fbcon_cfb24_clear, fbcon_cfb24_putc,
    fbcon_cfb24_putcs, fbcon_cfb24_revc, NULL, NULL, fbcon_cfb24_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};


#ifdef MODULE
int init_module(void)
{
    return 0;
}

void cleanup_module(void)
{}
#endif /* MODULE */


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(fbcon_cfb24);
EXPORT_SYMBOL(fbcon_cfb24_setup);
EXPORT_SYMBOL(fbcon_cfb24_bmove);
EXPORT_SYMBOL(fbcon_cfb24_clear);
EXPORT_SYMBOL(fbcon_cfb24_putc);
EXPORT_SYMBOL(fbcon_cfb24_putcs);
EXPORT_SYMBOL(fbcon_cfb24_revc);
EXPORT_SYMBOL(fbcon_cfb24_clear_margins);
