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
#include <linux/config.h>
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

    if (sx == 0 && dx == 0 && width * 32 == bytes)
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
    else if (dy < sy || (dy == sy && dx < sx)) {
	src = p->screen_base + sy * linesize + sx * 32;
	dst = p->screen_base + dy * linesize + dx * 32;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 32);
	    src += bytes;
	    dst += bytes;
	}
    } else {
	src = p->screen_base + (sy+height) * linesize + sx * 32 - bytes;
	dst = p->screen_base + (dy+height) * linesize + dx * 32 - bytes;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 32);
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

    dest = p->screen_base + sy * p->fontheight * bytes + sx * 32;

    bgx = fbcon_cfb32_cmap[attr_bgcol_ec(p, conp)];

    if (sx == 0 && width * 32 == bytes)
	for (i = 0 ; i < lines * width ; i++) {
	    ((u32 *)dest)[0] = bgx;
	    ((u32 *)dest)[1] = bgx;
	    ((u32 *)dest)[2] = bgx;
	    ((u32 *)dest)[3] = bgx;
	    ((u32 *)dest)[4] = bgx;
	    ((u32 *)dest)[5] = bgx;
	    ((u32 *)dest)[6] = bgx;
	    ((u32 *)dest)[7] = bgx;
	    dest += 32;
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
		((u32 *)dest)[6] = bgx;
		((u32 *)dest)[7] = bgx;
		dest += 32;
	    }
	}
    }
}

void fbcon_cfb32_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx;

    c &= 0xff;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 32;
    cdat = p->fontdata + c * p->fontheight;

    fgx = fbcon_cfb32_cmap[attr_fgcol(p, conp)];
    bgx = fbcon_cfb32_cmap[attr_bgcol(p, conp)];
    eorx = fgx ^ bgx;

    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	u8 bits = *cdat++;
	((u32 *)dest)[0] = (-(bits >> 7) & eorx) ^ bgx;
	((u32 *)dest)[1] = (-(bits >> 6 & 1) & eorx) ^ bgx;
	((u32 *)dest)[2] = (-(bits >> 5 & 1) & eorx) ^ bgx;
	((u32 *)dest)[3] = (-(bits >> 4 & 1) & eorx) ^ bgx;
	((u32 *)dest)[4] = (-(bits >> 3 & 1) & eorx) ^ bgx;
	((u32 *)dest)[5] = (-(bits >> 2 & 1) & eorx) ^ bgx;
	((u32 *)dest)[6] = (-(bits >> 1 & 1) & eorx) ^ bgx;
	((u32 *)dest)[7] = (-(bits & 1) & eorx) ^ bgx;
    }
}

void fbcon_cfb32_putcs(struct vc_data *conp, struct display *p, const char *s,
		       int count, int yy, int xx)
{
    u8 *cdat, c, *dest, *dest0;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * 32;
    fgx = fbcon_cfb32_cmap[attr_fgcol(p, conp)];
    bgx = fbcon_cfb32_cmap[attr_bgcol(p, conp)];
    eorx = fgx ^ bgx;
    while (count--) {
	c = *s++;
	cdat = p->fontdata + c * p->fontheight;

	for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
	    u8 bits = *cdat++;
	    ((u32 *)dest)[0] = (-(bits >> 7) & eorx) ^ bgx;
	    ((u32 *)dest)[1] = (-(bits >> 6 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[2] = (-(bits >> 5 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[3] = (-(bits >> 4 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[4] = (-(bits >> 3 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[5] = (-(bits >> 2 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[6] = (-(bits >> 1 & 1) & eorx) ^ bgx;
	    ((u32 *)dest)[7] = (-(bits & 1) & eorx) ^ bgx;
	}
	dest0 += 32;
    }
}

void fbcon_cfb32_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 32;
    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u32 *)dest)[0] ^= 0xffffffff;
	((u32 *)dest)[1] ^= 0xffffffff;
	((u32 *)dest)[2] ^= 0xffffffff;
	((u32 *)dest)[3] ^= 0xffffffff;
	((u32 *)dest)[4] ^= 0xffffffff;
	((u32 *)dest)[5] ^= 0xffffffff;
	((u32 *)dest)[6] ^= 0xffffffff;
	((u32 *)dest)[7] ^= 0xffffffff;
    }
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb32 = {
    fbcon_cfb32_setup, fbcon_cfb32_bmove, fbcon_cfb32_clear, fbcon_cfb32_putc,
    fbcon_cfb32_putcs, fbcon_cfb32_revc
};


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
