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

    if (sx == 0 && dx == 0 && width * p->fontwidth == bytes) {
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
	return;
    }
    if (p->fontwidthlog) {
    	sx <<= p->fontwidthlog; dx <<= p->fontwidthlog; width <<= p->fontwidthlog;
    } else {
    	sx *= p->fontwidth; dx *= p->fontwidth; width *= p->fontwidth;
    }
    if (dy < sy || (dy == sy && dx < sx)) {
	src = p->screen_base + sy * linesize + sx;
	dst = p->screen_base + dy * linesize + dx;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width);
	    src += bytes;
	    dst += bytes;
	}
    } else {
	src = p->screen_base + (sy+height) * linesize + sx - bytes;
	dst = p->screen_base + (dy+height) * linesize + dx - bytes;
	for (rows = height * p->fontheight ; rows-- ;) {
	    mymemmove(dst, src, width);
	    src -= bytes;
	    dst -= bytes;
	}
    }
}

void fbcon_cfb8_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
    u8 *dest;
    int bytes=p->next_line,lines=height * p->fontheight, rows;
    u8 bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * p->fontwidth;

    bgx=attr_bgcol_ec(p,conp);

    if (sx == 0 && p->fontwidth == 8 && width * 8 == bytes)
        memset(dest, bgx, lines * width * p->fontwidth);
    else {
    	width *= p->fontwidth;
	for (rows = lines; rows-- ; dest += bytes)
	    memset(dest, bgx, width);
    }
}

void fbcon_cfb8_putc(struct vc_data *conp, struct display *p, int c, int yy,
		     int xx)
{
    u8 *dest,*cdat;
    int bytes=p->next_line,rows;
    u32 eorx,fgx,bgx;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 8;
    if (p->fontwidth <= 8)
	cdat = p->fontdata + (c & 0xff) * p->fontheight;
    else
	cdat = p->fontdata + ((c & 0xff) * p->fontheight << 1);

    fgx=attr_fgcol(p,c);
    bgx=attr_bgcol(p,c);
    fgx |= (fgx << 8);
    fgx |= (fgx << 16);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

    switch (p->fontwidth) {
    case 4:
	for (rows = p->fontheight ; rows-- ; dest += bytes)
	    ((u32 *)dest)[0]= (nibbletab_cfb8[*cdat++ >> 4] & eorx) ^ bgx;
        break;
    case 8:
	for (rows = p->fontheight ; rows-- ; dest += bytes) {
	    ((u32 *)dest)[0]= (nibbletab_cfb8[*cdat >> 4] & eorx) ^ bgx;
	    ((u32 *)dest)[1]= (nibbletab_cfb8[*cdat++ & 0xf] & eorx) ^ bgx;
        }
        break;
    case 12:
    case 16:
	for (rows = p->fontheight ; rows-- ; dest += bytes) {
	    ((u32 *)dest)[0]= (nibbletab_cfb8[*(u16 *)cdat >> 12] & eorx) ^ bgx;
	    ((u32 *)dest)[1]= (nibbletab_cfb8[(*(u16 *)cdat >> 8) & 0xf] & eorx) ^ bgx;
	    ((u32 *)dest)[2]= (nibbletab_cfb8[(*(u16 *)cdat >> 4) & 0xf] & eorx) ^ bgx;
	    if (p->fontwidth == 16)
	        ((u32 *)dest)[3]= (nibbletab_cfb8[*cdat & 0xf] & eorx) ^ bgx;
	    cdat += 2;
        }
        break;
    }
}

void fbcon_cfb8_putcs(struct vc_data *conp, struct display *p, 
		      const unsigned short *s, int count, int yy, int xx)
{
    u8 *cdat, c, *dest, *dest0;
    int rows,bytes=p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * 8;
    fgx=attr_fgcol(p,*s);
    bgx=attr_bgcol(p,*s);
    fgx |= (fgx << 8);
    fgx |= (fgx << 16);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;
    switch (p->fontwidth) {
    case 4:
	while (count--) {
	    c = *s++;
	    cdat = p->fontdata + c * p->fontheight;

	    for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes)
		((u32 *)dest)[0]= (nibbletab_cfb8[*cdat++ >> 4] & eorx) ^ bgx;
	    dest0+=4;
        }
        break;
    case 8:
	while (count--) {
	    c = *s++;
	    cdat = p->fontdata + c * p->fontheight;

	    for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
		((u32 *)dest)[0]= (nibbletab_cfb8[*cdat >> 4] & eorx) ^ bgx;
		((u32 *)dest)[1]= (nibbletab_cfb8[*cdat++ & 0xf] & eorx) ^ bgx;
	    }
	    dest0+=8;
        }
        break;
    case 12:
    case 16:
	while (count--) {
	    c = *s++;
	    cdat = p->fontdata + (c * p->fontheight << 1);

	    for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
		((u32 *)dest)[0]= (nibbletab_cfb8[*(u16 *)cdat >> 12] & eorx) ^ bgx;
		((u32 *)dest)[1]= (nibbletab_cfb8[(*(u16 *)cdat >> 8) & 0xf] & eorx) ^ bgx;
		((u32 *)dest)[2]= (nibbletab_cfb8[(*(u16 *)cdat >> 4) & 0xf] & eorx) ^ bgx;
		if (p->fontwidth == 16)
		    ((u32 *)dest)[3]= (nibbletab_cfb8[*cdat & 0xf] & eorx) ^ bgx;
		cdat += 2;
	    }
	    dest0+=p->fontwidth;
        }
        break;
    }
}

void fbcon_cfb8_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes=p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth;
    for (rows = p->fontheight ; rows-- ; dest += bytes) {
    	switch (p->fontwidth) {
    	case 16: ((u32 *)dest)[3] ^= 0x0f0f0f0f; /* FALL THROUGH */
    	case 12: ((u32 *)dest)[2] ^= 0x0f0f0f0f; /* FALL THROUGH */
    	case 8: ((u32 *)dest)[1] ^= 0x0f0f0f0f;  /* FALL THROUGH */
    	case 4: ((u32 *)dest)[0] ^= 0x0f0f0f0f;  /* FALL THROUGH */
    	default: break;
    	}
    }
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb8 = {
    fbcon_cfb8_setup, fbcon_cfb8_bmove, fbcon_cfb8_clear, fbcon_cfb8_putc,
    fbcon_cfb8_putcs, fbcon_cfb8_revc, NULL, NULL, 
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

EXPORT_SYMBOL(fbcon_cfb8);
EXPORT_SYMBOL(fbcon_cfb8_setup);
EXPORT_SYMBOL(fbcon_cfb8_bmove);
EXPORT_SYMBOL(fbcon_cfb8_clear);
EXPORT_SYMBOL(fbcon_cfb8_putc);
EXPORT_SYMBOL(fbcon_cfb8_putcs);
EXPORT_SYMBOL(fbcon_cfb8_revc);
