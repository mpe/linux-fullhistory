/*
 *  linux/drivers/video/cfb8.c -- Low level frame buffer operations for 8 bpp
 *				  packed pixels
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
#include "fbcon-cfb8.h"


    /*
     *  8 bpp packed pixels
     */

static u32 nibbletab_cfb8[] = {
#if defined(__BIG_ENDIAN)
    0x00000000,0x000000ff,0x0000ff00,0x0000ffff,
    0x00ff0000,0x00ff00ff,0x00ffff00,0x00ffffff,
    0xff000000,0xff0000ff,0xff00ff00,0xff00ffff,
    0xffff0000,0xffff00ff,0xffffff00,0xffffffff
#elif defined(__LITTLE_ENDIAN)
    0x00000000,0xff000000,0x00ff0000,0xffff0000,
    0x0000ff00,0xff00ff00,0x00ffff00,0xffffff00,
    0x000000ff,0xff0000ff,0x00ff00ff,0xffff00ff,
    0x0000ffff,0xff00ffff,0x00ffffff,0xffffffff
#else
#error FIXME: No endianness??
#endif
};

void fbcon_cfb8_setup(struct display *p)
{
    p->next_line = p->line_length ? p->line_length : p->var.xres_virtual;
    p->next_plane = 0;
}

void fbcon_cfb8_bmove(struct display *p, int sy, int sx, int dy, int dx,
		      int height, int width)
{
    int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
    u8 *src,*dst;

    if (sx == 0 && dx == 0 && width * 8 == bytes)
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
    else if (dy < sy || (dy == sy && dx < sx)) {
	src = p->screen_base + sy * linesize + sx * 8;
	dst = p->screen_base + dy * linesize + dx * 8;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 8);
	    src += bytes;
	    dst += bytes;
	}
    } else {
	src = p->screen_base + (sy+height) * linesize + sx * 8 - bytes;
	dst = p->screen_base + (dy+height) * linesize + dx * 8 - bytes;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width * 8);
	    src -= bytes;
	    dst -= bytes;
	}
    }
}

void fbcon_cfb8_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
    u8 *dest0,*dest;
    int bytes=p->next_line,lines=height * p->fontheight, rows, i;
    u32 bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * 8;

    bgx=attr_bgcol_ec(p,conp);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);

    if (sx == 0 && width * 8 == bytes)
	for (i = 0 ; i < lines * width ; i++) {
	    ((u32 *)dest)[0]=bgx;
	    ((u32 *)dest)[1]=bgx;
	    dest+=8;
	}
    else {
	dest0=dest;
	for (rows = lines; rows-- ; dest0 += bytes) {
	    dest=dest0;
	    for (i = 0 ; i < width ; i++) {
		((u32 *)dest)[0]=bgx;
		((u32 *)dest)[1]=bgx;
		dest+=8;
	    }
	}
    }
}

void fbcon_cfb8_putc(struct vc_data *conp, struct display *p, int c, int yy,
		     int xx)
{
    u8 *dest,*cdat;
    int bytes=p->next_line,rows;
    u32 eorx,fgx,bgx;

    c &= 0xff;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 8;
    cdat = p->fontdata + c * p->fontheight;

    fgx=attr_fgcol(p,conp);
    bgx=attr_bgcol(p,conp);
    fgx |= (fgx << 8);
    fgx |= (fgx << 16);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u32 *)dest)[0]= (nibbletab_cfb8[*cdat >> 4] & eorx) ^ bgx;
	((u32 *)dest)[1]= (nibbletab_cfb8[*cdat++ & 0xf] & eorx) ^ bgx;
    }
}

void fbcon_cfb8_putcs(struct vc_data *conp, struct display *p, const char *s,
		      int count, int yy, int xx)
{
    u8 *cdat, c, *dest, *dest0;
    int rows,bytes=p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * 8;
    fgx=attr_fgcol(p,conp);
    bgx=attr_bgcol(p,conp);
    fgx |= (fgx << 8);
    fgx |= (fgx << 16);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;
    while (count--) {
	c = *s++;
	cdat = p->fontdata + c * p->fontheight;

	for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
	    ((u32 *)dest)[0]= (nibbletab_cfb8[*cdat >> 4] & eorx) ^ bgx;
	    ((u32 *)dest)[1]= (nibbletab_cfb8[*cdat++ & 0xf] & eorx) ^ bgx;
	}
	dest0+=8;
    }
}

void fbcon_cfb8_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes=p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 8;
    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u32 *)dest)[0] ^= 0x0f0f0f0f;
	((u32 *)dest)[1] ^= 0x0f0f0f0f;
    }
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb8 = {
    fbcon_cfb8_setup, fbcon_cfb8_bmove, fbcon_cfb8_clear, fbcon_cfb8_putc,
    fbcon_cfb8_putcs, fbcon_cfb8_revc, NULL
};


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(fbcon_cfb8);
EXPORT_SYMBOL(fbcon_cfb8_setup);
EXPORT_SYMBOL(fbcon_cfb8_bmove);
EXPORT_SYMBOL(fbcon_cfb8_clear);
EXPORT_SYMBOL(fbcon_cfb8_putc);
EXPORT_SYMBOL(fbcon_cfb8_putcs);
EXPORT_SYMBOL(fbcon_cfb8_revc);
