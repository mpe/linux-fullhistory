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

    if (sx == 0 && dx == 0 && width * p->fontwidth * 2 == bytes) {
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
	return;
    }
    if (p->fontwidthlog) {
	sx <<= p->fontwidthlog+1;
	dx <<= p->fontwidthlog+1;
	width <<= p->fontwidthlog+1;
    } else {
	sx *= p->fontwidth*2;
	dx *= p->fontwidth*2;
	width *= p->fontwidth*2;
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

void fbcon_cfb16_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u8 *dest0, *dest;
    int bytes = p->next_line, lines = height * p->fontheight, rows, i;
    u32 bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * p->fontwidth * 2;

    bgx = fbcon_cfb16_cmap[attr_bgcol_ec(p, conp)];
    bgx |= (bgx << 16);

    width *= p->fontwidth/4;
    if (sx == 0 && width * 8 == bytes)
	for (i = 0; i < lines * width; i++) {
	    ((u32 *)dest)[0] = bgx;
	    ((u32 *)dest)[1] = bgx;
	    dest += 8;
	}
    else {
	dest0 = dest;
	for (rows = lines; rows--; dest0 += bytes) {
	    dest = dest0;
	    for (i = 0; i < width; i++) {
		((u32 *)dest)[0] = bgx;
		((u32 *)dest)[1] = bgx;
		dest += 8;
	    }
	}
    }
}

void fbcon_cfb16_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat, bits;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 2;

    fgx = fbcon_cfb16_cmap[attr_fgcol(p, c)];
    bgx = fbcon_cfb16_cmap[attr_bgcol(p, c)];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
    switch (p->fontwidth) {
    case 4:
    case 8:
#endif
	cdat = p->fontdata + (c & p->charmask) * p->fontheight;
	for (rows = p->fontheight; rows--; dest += bytes) {
	    bits = *cdat++;
	    ((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	    ((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	    if (p->fontwidth == 8)
#endif
	    {
		((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
	    }
	}
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	break;
    case 12:
    case 16:
	cdat = p->fontdata + ((c & p->charmask) * p->fontheight << 1);
	for (rows = p->fontheight; rows--; dest += bytes) {
	    bits = *cdat++;
	    ((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	    ((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
	    ((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
	    ((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
	    bits = *cdat++;
	    ((u32 *)dest)[4] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	    ((u32 *)dest)[5] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
	    if (p->fontwidth == 16) {
		((u32 *)dest)[6] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		((u32 *)dest)[7] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
	    }
	}
	break;
    }
#endif
}

void fbcon_cfb16_putcs(struct vc_data *conp, struct display *p,
		       const unsigned short *s, int count, int yy, int xx)
{
    u8 *cdat, *dest, *dest0;
    u16 c;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth * 2;
    fgx = fbcon_cfb16_cmap[attr_fgcol(p, *s)];
    bgx = fbcon_cfb16_cmap[attr_bgcol(p, *s)];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
    switch (p->fontwidth) {
    case 4:
    case 8:
#endif
	while (count--) {
	    c = *s++ & p->charmask;
	    cdat = p->fontdata + c * p->fontheight;
	    for (rows = p->fontheight, dest = dest0; rows--; dest += bytes) {
		u8 bits = *cdat++;
		((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
		((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
		if (p->fontwidth == 8)
#endif
		{
		
		    ((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		    ((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
		}
	    }
	    dest0 += p->fontwidth*2;;
	}
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
	break;
    case 12:
    case 16:
	while (count--) {
	    c = *s++ & p->charmask;
	    cdat = p->fontdata + (c * p->fontheight << 1);
	    for (rows = p->fontheight, dest = dest0; rows--; dest += bytes) {
		u8 bits = *cdat++;
		((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
		((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
		((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
		bits = *cdat++;
		((u32 *)dest)[4] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
		((u32 *)dest)[5] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
		if (p->fontwidth == 16) {
		    ((u32 *)dest)[6] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		    ((u32 *)dest)[7] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
		}
	    }
	    dest0 += p->fontwidth*2;
	}
	break;
    }
#endif
}

void fbcon_cfb16_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * p->fontwidth*2;
    for (rows = p->fontheight; rows--; dest += bytes) {
#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
	((u32 *)dest)[2] ^= 0xffffffff; ((u32 *)dest)[3] ^= 0xffffffff;
	((u32 *)dest)[0] ^= 0xffffffff; ((u32 *)dest)[1] ^= 0xffffffff;
#else
	switch (p->fontwidth) {
	case 16:
	    ((u32 *)dest)[6] ^= 0xffffffff; ((u32 *)dest)[7] ^= 0xffffffff;
	    /* FALL THROUGH */
	case 12:
	    ((u32 *)dest)[4] ^= 0xffffffff; ((u32 *)dest)[5] ^= 0xffffffff;
	    /* FALL THROUGH */
	case 8:
	    ((u32 *)dest)[2] ^= 0xffffffff; ((u32 *)dest)[3] ^= 0xffffffff;
	    /* FALL THROUGH */
	case 4:
	    ((u32 *)dest)[0] ^= 0xffffffff; ((u32 *)dest)[1] ^= 0xffffffff;
	}
#endif
    }
}

void fbcon_cfb16_clear_margins(struct vc_data *conp, struct display *p)
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

    bgx = fbcon_cfb16_cmap[attr_bgcol_ec(p, conp)];
    bgx |= (bgx << 16);

    if (right_width) {
	dest0 = p->screen_base+right_start*2;
	for (i = 0; i < bottom_start; i++, dest0 += bytes) {
	    for (j = 0, dest = (u32 *)dest0; j < right_width/2; j++)
		*dest++ = bgx;
	    if (right_width & 1)
		*(u16 *)dest = bgx;
	}
    }
    if (bottom_width) {
	dest = (u32 *)(p->screen_base+bottom_start*bytes);
	for (i = 0; i < bytes*bottom_width/4; i++)
	    *dest++ = bgx;
	if ((bytes*bottom_width) & 2)
	    *(u16 *)dest = bgx;
    }
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_cfb16 = {
    fbcon_cfb16_setup, fbcon_cfb16_bmove, fbcon_cfb16_clear, fbcon_cfb16_putc,
    fbcon_cfb16_putcs, fbcon_cfb16_revc, NULL, NULL, fbcon_cfb16_clear_margins,
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

EXPORT_SYMBOL(fbcon_cfb16);
EXPORT_SYMBOL(fbcon_cfb16_setup);
EXPORT_SYMBOL(fbcon_cfb16_bmove);
EXPORT_SYMBOL(fbcon_cfb16_clear);
EXPORT_SYMBOL(fbcon_cfb16_putc);
EXPORT_SYMBOL(fbcon_cfb16_putcs);
EXPORT_SYMBOL(fbcon_cfb16_revc);
EXPORT_SYMBOL(fbcon_cfb16_cmap);
