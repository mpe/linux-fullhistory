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

#include <video/fbcon.h>
#include <video/fbcon-cfb16.h>


    /*
     *  16 bpp packed pixels
     */

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
    int bytes = p->next_line, linesize = bytes * fontheight(p), rows;
    u8 *src, *dst;

    if (sx == 0 && dx == 0 && width * fontwidth(p) * 2 == bytes) {
	mymemmove(p->screen_base + dy * linesize,
		  p->screen_base + sy * linesize,
		  height * linesize);
	return;
    }
    if (fontwidthlog(p)) {
	sx <<= fontwidthlog(p)+1;
	dx <<= fontwidthlog(p)+1;
	width <<= fontwidthlog(p)+1;
    } else {
	sx *= fontwidth(p)*2;
	dx *= fontwidth(p)*2;
	width *= fontwidth(p)*2;
    }
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

static inline void rectfill(u8 *dest, int width, int height, u32 data,
			    int linesize)
{
    int i;

    data |= data<<16;

    while (height-- > 0) {
	u32 *p = (u32 *)dest;
	for (i = 0; i < width/4; i++) {
	    *p++ = data;
	    *p++ = data;
	}
	if (width & 2)
	    *p++ = data;
	if (width & 1)
	    *(u16 *)p = data;
	dest += linesize;
    }
}

void fbcon_cfb16_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u8 *dest;
    int bytes = p->next_line, lines = height * fontheight(p);
    u32 bgx;

    dest = p->screen_base + sy * fontheight(p) * bytes + sx * fontwidth(p) * 2;

    bgx = ((u16 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];

    width *= fontwidth(p)/4;
    if (width * 8 == bytes)
	rectfill(dest, lines * width * 4, 1, bgx, bytes);
    else
	rectfill(dest, width * 4, lines, bgx, bytes);
}

void fbcon_cfb16_putc(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u8 *dest, *cdat, bits;
    int bytes = p->next_line, rows;
    u32 eorx, fgx, bgx;

    dest = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p) * 2;

    fgx = ((u16 *)p->dispsw_data)[attr_fgcol(p, c)];
    bgx = ((u16 *)p->dispsw_data)[attr_bgcol(p, c)];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

    switch (fontwidth(p)) {
    case 4:
    case 8:
	cdat = p->fontdata + (c & p->charmask) * fontheight(p);
	for (rows = fontheight(p); rows--; dest += bytes) {
	    bits = *cdat++;
	    ((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	    ((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
	    if (fontwidth(p) == 8) {
		((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
	    }
	}
	break;
    case 12:
    case 16:
	cdat = p->fontdata + ((c & p->charmask) * fontheight(p) << 1);
	for (rows = fontheight(p); rows--; dest += bytes) {
	    bits = *cdat++;
	    ((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	    ((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
	    ((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
	    ((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
	    bits = *cdat++;
	    ((u32 *)dest)[4] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
	    ((u32 *)dest)[5] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
	    if (fontwidth(p) == 16) {
		((u32 *)dest)[6] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		((u32 *)dest)[7] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
	    }
	}
	break;
    }
}

void fbcon_cfb16_putcs(struct vc_data *conp, struct display *p,
		       const unsigned short *s, int count, int yy, int xx)
{
    u8 *cdat, *dest, *dest0;
    u16 c;
    int rows, bytes = p->next_line;
    u32 eorx, fgx, bgx;

    dest0 = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p) * 2;
    fgx = ((u16 *)p->dispsw_data)[attr_fgcol(p, *s)];
    bgx = ((u16 *)p->dispsw_data)[attr_bgcol(p, *s)];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

    switch (fontwidth(p)) {
    case 4:
    case 8:
	while (count--) {
	    c = *s++ & p->charmask;
	    cdat = p->fontdata + c * fontheight(p);
	    for (rows = fontheight(p), dest = dest0; rows--; dest += bytes) {
		u8 bits = *cdat++;
		((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
		((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
		if (fontwidth(p) == 8) {
		    ((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		    ((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
		}
	    }
	    dest0 += fontwidth(p)*2;;
	}
	break;
    case 12:
    case 16:
	while (count--) {
	    c = *s++ & p->charmask;
	    cdat = p->fontdata + (c * fontheight(p) << 1);
	    for (rows = fontheight(p), dest = dest0; rows--; dest += bytes) {
		u8 bits = *cdat++;
		((u32 *)dest)[0] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
		((u32 *)dest)[1] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
		((u32 *)dest)[2] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		((u32 *)dest)[3] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
		bits = *cdat++;
		((u32 *)dest)[4] = (tab_cfb16[bits >> 6] & eorx) ^ bgx;
		((u32 *)dest)[5] = (tab_cfb16[bits >> 4 & 3] & eorx) ^ bgx;
		if (fontwidth(p) == 16) {
		    ((u32 *)dest)[6] = (tab_cfb16[bits >> 2 & 3] & eorx) ^ bgx;
		    ((u32 *)dest)[7] = (tab_cfb16[bits & 3] & eorx) ^ bgx;
		}
	    }
	    dest0 += fontwidth(p)*2;
	}
	break;
    }
}

void fbcon_cfb16_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p)*2;
    for (rows = fontheight(p); rows--; dest += bytes) {
	switch (fontwidth(p)) {
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
    }
}

void fbcon_cfb16_clear_margins(struct vc_data *conp, struct display *p,
			       int bottom_only)
{
    int bytes = p->next_line;
    u32 bgx;

    unsigned int right_start = conp->vc_cols*fontwidth(p);
    unsigned int bottom_start = conp->vc_rows*fontheight(p);
    unsigned int right_width, bottom_width;

    bgx = ((u16 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];

    if (!bottom_only && (right_width = p->var.xres-right_start))
	rectfill(p->screen_base+right_start*2, right_width,
		 p->var.yres_virtual, bgx, bytes);
    if ((bottom_width = p->var.yres-bottom_start))
	rectfill(p->screen_base+(p->var.yoffset+bottom_start)*bytes,
		 right_start, bottom_width, bgx, bytes);
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
EXPORT_SYMBOL(fbcon_cfb16_clear_margins);
