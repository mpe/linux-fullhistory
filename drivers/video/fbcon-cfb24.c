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

#include "fbcon.h"
#include "fbcon-cfb24.h"


    /*
     *  24 bpp packed pixels
     */

u32 fbcon_cfb24_cmap[16];

void fbcon_cfb24_setup(struct display *p)
{
    p->next_line = p->line_length ? p->line_length : p->var.xres_virtual*3;
    p->next_plane = 0;
}

void fbcon_cfb24_bmove(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
    int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
    u8 *src, *dst;

    if (sx == 0 && dx == 0 && width * 24 == bytes)
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
    else if (dy < sy || (dy == sy && dx < sx)) {
	src = p->screen_base + sy * linesize + sx * 24;
	dst = p->screen_base + dy * linesize + dx * 24;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 24);
	    src += bytes;
	    dst += bytes;
	}
    } else {
	src = p->screen_base + (sy+height) * linesize + sx * 24 - bytes;
	dst = p->screen_base + (dy+height) * linesize + dx * 24 - bytes;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 24);
	    src -= bytes;
	    dst -= bytes;
	}
    }
}

void fbcon_cfb24_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u8 *dest0, *dest;
    int bytes = p->next_line, lines = height * p->fontheight, rows, i;
    u32 bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * 24;

    bgx = fbcon_cfb24_cmap[attr_bgcol_ec(p, conp)];

    if (sx == 0 && width * 24 == bytes)
	for (i = 0 ; i < lines * width ; i++) {
	    ((u32 *)dest)[0] = bgx;
	    ((u32 *)dest)[1] = bgx;
	    ((u32 *)dest)[2] = bgx;
	    ((u32 *)dest)[3] = bgx;
	    ((u32 *)dest)[4] = bgx;
	    ((u32 *)dest)[5] = bgx;
	    dest += 24;
	}
    else {
	dest0 = dest;
	for (rows = lines; rows-- ; dest0 += bytes) {
	    dest = dest0;
	    for (i = 0 ; i < width ; i++) {
		((u32 *)dest)[0] = bgx;
		((u32 *)dest)[1] = bgx;
		((u32 *)dest)[2] = bgx;
		((u32 *)dest)[3] = bgx;
		((u32 *)dest)[4] = bgx;
		((u32 *)dest)[5] = bgx;
		dest += 24;
	    }
	}
    }
}

static inline void store4pixels(u32 d1, u32 d2, u32 d3, u32 d4, u32 *dest)
{
#if defined(__BIG_ENDIAN)
    *dest++ = (d1<<8) | (d2>>16);
    *dest++ = (d2<<16) | (d3>>8);
    *dest++ = (d3<<24) | d4;
#elif defined(__LITTLE_ENDIAN)
    *dest++ = (d1<<8) | (d2>>16);
    *dest++ = (d2<<16) | (d3>>8);
    *dest++ = (d3<<24) | d4;
#else
#error FIXME: No endianness??
#endif
}

void fbcon_cfb24_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 24;
    cdat = p->fontdata + (c & 0xff) * p->fontheight;

    fgx = fbcon_cfb24_cmap[attr_fgcol(p, c)];
    bgx = fbcon_cfb24_cmap[attr_bgcol(p, c)];
    eorx = fgx ^ bgx;

    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	u8 bits = *cdat++;
	u32 d1, d2, d3, d4;
	d1 = (-(bits >> 7) & eorx) ^ bgx;
	d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)dest);
	d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	d4 = (-(bits & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)(dest+12));
    }
}

void fbcon_cfb24_putcs(struct vc_data *conp, struct display *p, 
		       const unsigned short *s, int count, int yy, int xx)
{
    u8 *cdat, c, *dest, *dest0;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * 24;
    fgx = fbcon_cfb24_cmap[attr_fgcol(p, *s)];
    bgx = fbcon_cfb24_cmap[attr_bgcol(p, *s)];
    eorx = fgx ^ bgx;
    while (count--) {
	c = *s++;
	cdat = p->fontdata + c * p->fontheight;

	for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
	    u8 bits = *cdat++;
	    u32 d1, d2, d3, d4;
	    d1 = (-(bits >> 7) & eorx) ^ bgx;
	    d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)dest);
	    d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    d4 = (-(bits & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)(dest+12));
	}
	dest0 += 24;
    }
}

void fbcon_cfb24_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 24;
    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u32 *)dest)[0] ^= 0xffffffff;
	((u32 *)dest)[1] ^= 0xffffffff;
	((u32 *)dest)[2] ^= 0xffffffff;
	((u32 *)dest)[3] ^= 0xffffffff;
	((u32 *)dest)[4] ^= 0xffffffff;
	((u32 *)dest)[5] ^= 0xffffffff;
    }
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb24 = {
    fbcon_cfb24_setup, fbcon_cfb24_bmove, fbcon_cfb24_clear, fbcon_cfb24_putc,
    fbcon_cfb24_putcs, fbcon_cfb24_revc, NULL, NULL, NULL, FONTWIDTH(8)
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
EXPORT_SYMBOL(fbcon_cfb24_cmap);
