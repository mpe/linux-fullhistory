/*
 *  linux/drivers/video/cfb32.c -- Low level frame buffer operations for 32 bpp
 *				   truecolor packed pixels
 *
 *	Created 28 Dec 1997 by Geert Uytterhoeven
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
#include "fbcon-cfb32.h"


    /*
     *  32 bpp packed pixels
     */

u32 fbcon_cfb32_cmap[16];

void fbcon_cfb32_setup(struct display *p)
{
    p->next_line = p->line_length ? p->line_length : p->var.xres_virtual<<2;
    p->next_plane = 0;
}

void fbcon_cfb32_bmove(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
    int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
    u8 *src, *dst;

    if (sx == 0 && dx == 0 && width * p->fontwidth * 4 == bytes) {
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
	return;
    }
    if (p->fontwidthlog) {
	sx <<= p->fontwidthlog+2;
	dx <<= p->fontwidthlog+2;
	width <<= p->fontwidthlog+2;
    } else {
	sx *= p->fontwidth*4;
	dx *= p->fontwidth*4;
	width *= p->fontwidth*4;
    }
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

void fbcon_cfb32_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u8 *dest0, *dest;
    int bytes = p->next_line, lines = height * p->fontheight, rows, i;
    u32 bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * p->fontwidth * 4;

    bgx = fbcon_cfb32_cmap[attr_bgcol_ec(p, conp)];

    width *= p->fontwidth/4;
    if (sx == 0 && width * 16 == bytes)
	for (i = 0; i < lines * width; i++) {
	    ((u32 *)dest)[0] = bgx;
	    ((u32 *)dest)[1] = bgx;
	    ((u32 *)dest)[2] = bgx;
	    ((u32 *)dest)[3] = bgx;
	    dest += 16;
	}
    else {
	dest0 = dest;
	for (rows = lines; rows--; dest0 += bytes) {
	    dest = dest0;
	    for (i = 0; i < width; i++) {
		((u32 *)dest)[0] = bgx;
		((u32 *)dest)[1] = bgx;
		((u32 *)dest)[2] = bgx;
		((u32 *)dest)[3] = bgx;
		dest += 16;
	    }
	}
    }
}

void fbcon_cfb32_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat, bits;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 4;
#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
    cdat = p->fontdata + (c & p->charmask) * p->fontheight;
#else
    if (p->fontwidth <= 8)
	cdat = p->fontdata + (c & p->charmask) * p->fontheight;
    else
	cdat = p->fontdata + ((c & p->charmask) * p->fontheight << 1);
#endif
    fgx = fbcon_cfb32_cmap[attr_fgcol(p, c)];
    bgx = fbcon_cfb32_cmap[attr_bgcol(p, c)];
    eorx = fgx ^ bgx;

    for (rows = p->fontheight; rows--; dest += bytes) {
	bits = *cdat++;
	((u32 *)dest)[0] = (-(bits >> 7) & eorx) ^ bgx;
	((u32 *)dest)[1] = (-(bits >> 6 & 1) & eorx) ^ bgx;
	((u32 *)dest)[2] = (-(bits >> 5 & 1) & eorx) ^ bgx;
	((u32 *)dest)[3] = (-(bits >> 4 & 1) & eorx) ^ bgx;
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	if (p->fontwidth < 8)
	    continue;
#endif
	((u32 *)dest)[4] = (-(bits >> 3 & 1) & eorx) ^ bgx;
	((u32 *)dest)[5] = (-(bits >> 2 & 1) & eorx) ^ bgx;
	((u32 *)dest)[6] = (-(bits >> 1 & 1) & eorx) ^ bgx;
	((u32 *)dest)[7] = (-(bits & 1) & eorx) ^ bgx;
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	if (p->fontwidth < 12)
	    continue;
	bits = *cdat++;
	((u32 *)dest)[8] = (-(bits >> 7) & eorx) ^ bgx;
	((u32 *)dest)[9] = (-(bits >> 6 & 1) & eorx) ^ bgx;
	((u32 *)dest)[10] = (-(bits >> 5 & 1) & eorx) ^ bgx;
	((u32 *)dest)[11] = (-(bits >> 4 & 1) & eorx) ^ bgx;
	if (p->fontwidth < 16)
	    continue;
	((u32 *)dest)[12] = (-(bits >> 3 & 1) & eorx) ^ bgx;
	((u32 *)dest)[13] = (-(bits >> 2 & 1) & eorx) ^ bgx;
	((u32 *)dest)[14] = (-(bits >> 1 & 1) & eorx) ^ bgx;
	((u32 *)dest)[15] = (-(bits & 1) & eorx) ^ bgx;
#endif
    }
}

void fbcon_cfb32_putcs(struct vc_data *conp, struct display *p,
		       const unsigned short *s, int count, int yy, int xx)
{
    u8 *cdat, *dest, *dest0, bits;
    u16 c;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 4;
    fgx = fbcon_cfb32_cmap[attr_fgcol(p, *s)];
    bgx = fbcon_cfb32_cmap[attr_bgcol(p, *s)];
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
	    ((u32 *)dest)[0] = (-(bits >> 7) & eorx) ^ bgx;
	    ((u32 *)dest)[1] = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[2] = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[3] = (-(bits >> 4 & 1) & eorx) ^ bgx;
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	    if (p->fontwidth < 8)
		continue;
#endif
	    ((u32 *)dest)[4] = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[5] = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[6] = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[7] = (-(bits & 1) & eorx) ^ bgx;
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	    if (p->fontwidth < 12)
		continue;
	    bits = *cdat++;
	    ((u32 *)dest)[8] = (-(bits >> 7) & eorx) ^ bgx;
	    ((u32 *)dest)[9] = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[10] = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[11] = (-(bits >> 4 & 1) & eorx) ^ bgx;
	    if (p->fontwidth < 16)
		continue;
	    ((u32 *)dest)[12] = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[13] = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[14] = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[15] = (-(bits & 1) & eorx) ^ bgx;
#endif
	}
	dest0 += p->fontwidth*4;
    }
}

void fbcon_cfb32_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 4;
    for (rows = p->fontheight; rows--; dest += bytes) {
#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
	((u32 *)dest)[4] ^= 0xffffffff; ((u32 *)dest)[5] ^= 0xffffffff;
	((u32 *)dest)[6] ^= 0xffffffff; ((u32 *)dest)[7] ^= 0xffffffff;
	((u32 *)dest)[0] ^= 0xffffffff; ((u32 *)dest)[1] ^= 0xffffffff;
	((u32 *)dest)[2] ^= 0xffffffff; ((u32 *)dest)[3] ^= 0xffffffff;
#else
	switch (p->fontwidth) {
	case 16:
	    ((u32 *)dest)[12] ^= 0xffffffff; ((u32 *)dest)[13] ^= 0xffffffff;
	    ((u32 *)dest)[14] ^= 0xffffffff; ((u32 *)dest)[15] ^= 0xffffffff;
	    /* FALL THROUGH */
	case 12:
	    ((u32 *)dest)[8] ^= 0xffffffff; ((u32 *)dest)[9] ^= 0xffffffff;
	    ((u32 *)dest)[10] ^= 0xffffffff; ((u32 *)dest)[11] ^= 0xffffffff;
	    /* FALL THROUGH */
	case 8:
	    ((u32 *)dest)[4] ^= 0xffffffff; ((u32 *)dest)[5] ^= 0xffffffff;
	    ((u32 *)dest)[6] ^= 0xffffffff; ((u32 *)dest)[7] ^= 0xffffffff;
	    /* FALL THROUGH */
	case 4:
	    ((u32 *)dest)[0] ^= 0xffffffff; ((u32 *)dest)[1] ^= 0xffffffff;
	    ((u32 *)dest)[2] ^= 0xffffffff; ((u32 *)dest)[3] ^= 0xffffffff;
	    /* FALL THROUGH */
	}
#endif
    }
}

void fbcon_cfb32_clear_margins(struct vc_data *conp, struct display *p)
{
    u8 *dest0;
    u32 *dest;
    int bytes = p->next_line;
    u32 bgx;
    int i, j;

    unsigned int right_start = conp->vc_cols*p->fontwidth;
    unsigned int right_width = p->var.xres_virtual-right_start;
    unsigned int bottom_start = conp->vc_rows*p->fontheight;
    unsigned int bottom_width = p->var.yres_virtual-bottom_start;

    bgx = fbcon_cfb32_cmap[attr_bgcol_ec(p, conp)];

    if (right_width) {
	dest0 = p->screen_base+right_start*4;
	for (i = 0; i < bottom_start; i++, dest0 += bytes)
	    for (j = 0, dest = (u32 *)dest0; j < right_width; j++)
		*dest++ = bgx;
    }
    if (bottom_width) {
	dest = (u32 *)(p->screen_base+bottom_start*bytes);
	for (i = 0; i < bytes*bottom_width/4; i++)
	    *dest++ = bgx;
    }
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb32 = {
    fbcon_cfb32_setup, fbcon_cfb32_bmove, fbcon_cfb32_clear, fbcon_cfb32_putc,
    fbcon_cfb32_putcs, fbcon_cfb32_revc, NULL, NULL, fbcon_cfb32_clear_margins,
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

EXPORT_SYMBOL(fbcon_cfb32);
EXPORT_SYMBOL(fbcon_cfb32_setup);
EXPORT_SYMBOL(fbcon_cfb32_bmove);
EXPORT_SYMBOL(fbcon_cfb32_clear);
EXPORT_SYMBOL(fbcon_cfb32_putc);
EXPORT_SYMBOL(fbcon_cfb32_putcs);
EXPORT_SYMBOL(fbcon_cfb32_revc);
EXPORT_SYMBOL(fbcon_cfb32_cmap);
