/*
 *  linux/drivers/video/cfb16.c -- Low level frame buffer operations for 16 bpp
 *				   truecolor packed pixels
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
#include "fbcon-cfb16.h"


    /*
     *  16 bpp packed pixels
     */

u16 fbcon_cfb16_cmap[16];

static u32 tab_cfb16[] = {
#if defined(__BIG_ENDIAN)
    0x00000000, 0x0000ffff, 0xffff0000, 0xffffffff
#elif defined(__LITTLE_ENDIAN)
    0x00000000, 0xffff0000, 0x0000ffff, 0xffffffff
#else
#error FIXME: No endianness??
#endif
};

void fbcon_cfb16_setup(struct display *p)
{
    p->next_line = p->line_length ? p->line_length : p->var.xres_virtual<<1;
    p->next_plane = 0;
}

void fbcon_cfb16_bmove(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
    int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
    u8 *src, *dst;

    if (sx == 0 && dx == 0 && width * 16 == bytes)
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
    else if (dy < sy || (dy == sy && dx < sx)) {
	src = p->screen_base + sy * linesize + sx * 16;
	dst = p->screen_base + dy * linesize + dx * 16;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 16);
	    src += bytes;
	    dst += bytes;
	}
    } else {
	src = p->screen_base + (sy+height) * linesize + sx * 16 - bytes;
	dst = p->screen_base + (dy+height) * linesize + dx * 16 - bytes;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 16);
	    src -= bytes;
	    dst -= bytes;
	}
    }
}

void fbcon_cfb16_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u8 *dest0, *dest;
    int bytes = p->next_line, lines = height * p->fontheight, rows, i;
    u32 bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * 16;

    bgx = fbcon_cfb16_cmap[attr_bgcol_ec(p, conp)];
    bgx |= (bgx << 16);

    if (sx == 0 && width * 16 == bytes)
	for (i = 0 ; i < lines * width ; i++) {
	    ((u32 *)dest)[0] = bgx;
	    ((u32 *)dest)[1] = bgx;
	    ((u32 *)dest)[2] = bgx;
	    ((u32 *)dest)[3] = bgx;
	    dest += 16;
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
		dest += 16;
	    }
	}
    }
}

void fbcon_cfb16_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx;

    c &= 0xff;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 16;
    cdat = p->fontdata + c * p->fontheight;

    fgx = fbcon_cfb16_cmap[attr_fgcol(p, conp)];
    bgx = fbcon_cfb16_cmap[attr_bgcol(p, conp)];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	u8 bits = *cdat++;
	((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
	((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
	((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
    }
}

void fbcon_cfb16_putcs(struct vc_data *conp, struct display *p, const char *s,
		       int count, int yy, int xx)
{
    u8 *cdat, c, *dest, *dest0;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * 16;
    fgx = fbcon_cfb16_cmap[attr_fgcol(p, conp)];
    bgx = fbcon_cfb16_cmap[attr_bgcol(p, conp)];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;
    while (count--) {
	c = *s++;
	cdat = p->fontdata + c * p->fontheight;

	for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
	    u8 bits = *cdat++;
	    ((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	    ((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
	    ((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
	    ((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
	}
	dest0 += 16;
    }
}

void fbcon_cfb16_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 16;
    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u32 *)dest)[0] ^= 0xffffffff;
	((u32 *)dest)[1] ^= 0xffffffff;
	((u32 *)dest)[2] ^= 0xffffffff;
	((u32 *)dest)[3] ^= 0xffffffff;
    }
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb16 = {
    fbcon_cfb16_setup, fbcon_cfb16_bmove, fbcon_cfb16_clear, fbcon_cfb16_putc,
    fbcon_cfb16_putcs, fbcon_cfb16_revc, NULL
};


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(fbcon_cfb16);
EXPORT_SYMBOL(fbcon_cfb16_setup);
EXPORT_SYMBOL(fbcon_cfb16_bmove);
EXPORT_SYMBOL(fbcon_cfb16_clear);
EXPORT_SYMBOL(fbcon_cfb16_putc);
EXPORT_SYMBOL(fbcon_cfb16_putcs);
EXPORT_SYMBOL(fbcon_cfb16_revc);
EXPORT_SYMBOL(fbcon_cfb16_cmap);
