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

#include <linux/config.h>
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

    if (sx == 0 && dx == 0 && width * p->fontwidth * 3 == bytes) {
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
	return;
    }
    if (p->fontwidthlog) {
	sx <<= p->fontwidthlog;
	dx <<= p->fontwidthlog;
	width <<= p->fontwidthlog;
    } else {
	sx *= p->fontwidth;
	dx *= p->fontwidth;
	width *= p->fontwidth;
    }
    sx *= 3; dx *= 3; width *= 3;
    if (dy < sy || (dy == sy && dx < sx)) {
	src = p->screen_base + sy * linesize + sx;
	dst = p->screen_base + dy * linesize + dx;
	for (rows = height * p->fontheight; rows--;) {
	    mymemmove(dst, src, width);
	    src += bytes;
	    dst += bytes;
	}
    } else {
	src = p->screen_base + (sy+height) * linesize + sx - bytes;
	dst = p->screen_base + (dy+height) * linesize + dx - bytes;
	for (rows = height * p->fontheight; rows--;) {
	    mymemmove(dst, src, width);
	    src -= bytes;
	    dst -= bytes;
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

void fbcon_cfb24_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u8 *dest0, *dest;
    int bytes = p->next_line, lines = height * p->fontheight, rows, i;
    u32 bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * p->fontwidth * 3;

    bgx = fbcon_cfb24_cmap[attr_bgcol_ec(p, conp)];

    width *= p->fontwidth/4;
    if (sx == 0 && width * 12 == bytes)
	for (i = 0; i < lines * width; i++) {
	    store4pixels(bgx, bgx, bgx, bgx, (u32 *)dest);
	    dest += 12;
	}
    else {
	dest0 = dest;
	for (rows = lines; rows--; dest0 += bytes) {
	    dest = dest0;
	    for (i = 0; i < width; i++) {
		store4pixels(bgx, bgx, bgx, bgx, (u32 *)dest);
		dest += 12;
	    }
	}
    }
}

void fbcon_cfb24_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat, bits;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx, d1, d2, d3, d4;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 3;
    if (p->fontwidth <= 8)
	cdat = p->fontdata + (c & p->charmask) * p->fontheight;
    else
	cdat = p->fontdata + ((c & p->charmask) * p->fontheight << 1);

    fgx = fbcon_cfb24_cmap[attr_fgcol(p, c)];
    bgx = fbcon_cfb24_cmap[attr_bgcol(p, c)];
    eorx = fgx ^ bgx;

    for (rows = p->fontheight; rows--; dest += bytes) {
	bits = *cdat++;
	d1 = (-(bits >> 7) & eorx) ^ bgx;
	d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)dest);
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	if (p->fontwidth < 8)
	    continue;
#endif
	d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	d4 = (-(bits & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)(dest+12));
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	if (p->fontwidth < 12)
	    continue;
	bits = *cdat++;
	d1 = (-(bits >> 7) & eorx) ^ bgx;
	d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)(dest+24));
	if (p->fontwidth < 16)
	    continue;
	d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	d4 = (-(bits & 1) & eorx) ^ bgx;
	store4pixels(d1, d2, d3, d4, (u32 *)(dest+32));
#endif
    }
}

void fbcon_cfb24_putcs(struct vc_data *conp, struct display *p,
		       const unsigned short *s, int count, int yy, int xx)
{
    u8 *cdat, *dest, *dest0, bits;
    u16 c;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx, d1, d2, d3, d4;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 3;
    fgx = fbcon_cfb24_cmap[attr_fgcol(p, *s)];
    bgx = fbcon_cfb24_cmap[attr_bgcol(p, *s)];
    eorx = fgx ^ bgx;
    while (count--) {
	c = *s++ & p->charmask;
#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
	cdat = p->fontdata + c * p->fontheight;
#else
	if (p->fontwidth <= 8)
	    cdat = p->fontdata + c * p->fontheight;
	  
	else
	    cdat = p->fontdata + (c * p->fontheight << 1);
#endif
	for (rows = p->fontheight, dest = dest0; rows--; dest += bytes) {
	    bits = *cdat++;
	    d1 = (-(bits >> 7) & eorx) ^ bgx;
	    d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)dest);
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	    if (p->fontwidth < 8)
		continue;
#endif
	    d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    d4 = (-(bits & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)(dest+12));
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	    if (p->fontwidth < 12)
		continue;
	    bits = *cdat++;
	    d1 = (-(bits >> 7) & eorx) ^ bgx;
	    d2 = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    d4 = (-(bits >> 4 & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)(dest+24));
	    if (p->fontwidth < 16)
		continue;
	    d1 = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    d2 = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    d3 = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    d4 = (-(bits & 1) & eorx) ^ bgx;
	    store4pixels(d1, d2, d3, d4, (u32 *)(dest+32));
#endif
	}
	dest0 += p->fontwidth*3;
    }
}

void fbcon_cfb24_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 3;
    for (rows = p->fontheight; rows--; dest += bytes) {
#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
	((u32 *)dest)[3] ^= 0xffffffff; ((u32 *)dest)[4] ^= 0xffffffff;
	((u32 *)dest)[5] ^= 0xffffffff;
	((u32 *)dest)[0] ^= 0xffffffff; ((u32 *)dest)[1] ^= 0xffffffff;
	((u32 *)dest)[2] ^= 0xffffffff;
#else
	switch (p->fontwidth) {
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
#endif
    }
}

void fbcon_cfb24_clear_margins(struct vc_data *conp, struct display *p)
{
    u8 *dest0, *dest;
    int bytes = p->next_line;
    u32 bgx;
    int i, j;

    unsigned int right_start = conp->vc_cols*p->fontwidth;
    unsigned int right_width = p->var.xres_virtual-right_start;
    unsigned int bottom_start = conp->vc_rows*p->fontheight;
    unsigned int bottom_width = p->var.yres_virtual-bottom_start;

    bgx = fbcon_cfb24_cmap[attr_bgcol_ec(p, conp)];

    if (right_width) {
	dest0 = p->screen_base+right_start*3;
	for (i = 0; i < bottom_start; i++, dest0 += bytes)
	    for (j = 0, dest = dest0; j < right_width/4; j++) {
		store4pixels(bgx, bgx, bgx, bgx, (u32 *)dest);
		dest += 12;
	    }
    }
    if (bottom_width) {
	dest = p->screen_base+bottom_start*bytes;
	for (i = 0; i < bytes*bottom_width/12; i++) {
	    store4pixels(bgx, bgx, bgx, bgx, (u32 *)dest);
	    dest += 12;
	}
    }
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
EXPORT_SYMBOL(fbcon_cfb24_cmap);
