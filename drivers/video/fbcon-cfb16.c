/*
 *  linux/drivers/video/cfb16.c -- Low level frame buffer operations for 16 bpp
 *				   packed pixels
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


    /*
     *  Prototypes
     */

static int open_cfb16(struct display *p);
static void release_cfb16(void);
static void bmove_cfb16(struct display *p, int sy, int sx, int dy, int dx,
			int height, int width);
static void clear_cfb16(struct vc_data *conp, struct display *p, int sy,
			int sx, int height, int width);
static void putc_cfb16(struct vc_data *conp, struct display *p, int c,
		       int yy, int xx);
static void putcs_cfb16(struct vc_data *conp, struct display *p,
			const char *s, int count, int yy, int xx);
static void rev_char_cfb16(struct display *p, int xx, int yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_cfb16 = {
    open_cfb16, release_cfb16, bmove_cfb16, clear_cfb16, putc_cfb16,
    putcs_cfb16, rev_char_cfb16
};


    /*
     *  16 bpp packed pixels
     */

u_short packed16_cmap[16];

static u_long tab_cfb16[] = {
    0x00000000,0x0000ffff,0xffff0000,0xffffffff
};

static int open_cfb16(struct display *p)
{
    if (p->type != FB_TYPE_PACKED_PIXELS || p->var.bits_per_pixel != 16)
	return -EINVAL;

    p->next_line = p->var.xres_virtual<<1;
    p->next_plane = 0;
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_cfb16(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_cfb16(struct display *p, int sy, int sx, int dy, int dx,
			int height, int width)
{
    int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
    u_char *src,*dst;

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

static void clear_cfb16(struct vc_data *conp, struct display *p, int sy,
			int sx, int height, int width)
{
    u_char *dest0,*dest;
    int bytes=p->next_line,lines=height * p->fontheight, rows, i;
    u_long bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * 16;

    bgx = attr_bgcol_ec(p,conp);
    bgx = packed16_cmap[bgx];
    bgx |= (bgx << 16);

    if (sx == 0 && width * 16 == bytes)
	for (i = 0 ; i < lines * width ; i++) {
	    ((u_long *)dest)[0]=bgx;
	    ((u_long *)dest)[1]=bgx;
	    ((u_long *)dest)[2]=bgx;
	    ((u_long *)dest)[3]=bgx;
	    dest+=16;
	}
    else {
	dest0=dest;
	for (rows = lines; rows-- ; dest0 += bytes) {
	    dest=dest0;
	    for (i = 0 ; i < width ; i++) {
		((u_long *)dest)[0]=bgx;
		((u_long *)dest)[1]=bgx;
		((u_long *)dest)[2]=bgx;
		((u_long *)dest)[3]=bgx;
		dest+=16;
	    }
	}
    }
}

static void putc_cfb16(struct vc_data *conp, struct display *p, int c, int yy,
		       int xx)
{
    u_char *dest,*cdat;
    int bytes=p->next_line,rows;
    ulong eorx,fgx,bgx;

    c &= 0xff;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 16;
    cdat = p->fontdata + c * p->fontheight;

    fgx = attr_fgcol(p,conp);
    fgx = packed16_cmap[fgx];
    bgx = attr_bgcol(p,conp);
    bgx = packed16_cmap[bgx];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;

    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u_long *)dest)[0]= (tab_cfb16[*cdat >> 6] & eorx) ^ bgx;
	((u_long *)dest)[1]= (tab_cfb16[*cdat >> 4 & 0x3] & eorx) ^ bgx;
	((u_long *)dest)[2]= (tab_cfb16[*cdat >> 2 & 0x3] & eorx) ^ bgx;
	((u_long *)dest)[3]= (tab_cfb16[*cdat++ & 0x3] & eorx) ^ bgx;
    }
}

static void putcs_cfb16(struct vc_data *conp, struct display *p, const char *s,
			int count, int yy, int xx)
{
    u_char *cdat, c, *dest, *dest0;
    int rows,bytes=p->next_line;
    u_long eorx, fgx, bgx;

    dest0 = p->screen_base + yy * p->fontheight * bytes + xx * 16;
    fgx = attr_fgcol(p,conp);
    fgx = packed16_cmap[fgx];
    bgx = attr_bgcol(p,conp);
    bgx = packed16_cmap[bgx];
    fgx |= (fgx << 16);
    bgx |= (bgx << 16);
    eorx = fgx ^ bgx;
    while (count--) {
	c = *s++;
	cdat = p->fontdata + c * p->fontheight;

	for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
	    ((u_long *)dest)[0]= (tab_cfb16[*cdat >> 6] & eorx) ^ bgx;
	    ((u_long *)dest)[1]= (tab_cfb16[*cdat >> 4 & 0x3] & eorx) ^ bgx;
	    ((u_long *)dest)[2]= (tab_cfb16[*cdat >> 2 & 0x3] & eorx) ^ bgx;
	    ((u_long *)dest)[3]= (tab_cfb16[*cdat++ & 0x3] & eorx) ^ bgx;
	}
	dest0+=16;
    }
}

static void rev_char_cfb16(struct display *p, int xx, int yy)
{
    u_char *dest;
    int bytes=p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 16;
    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u_long *)dest)[0] ^= 0xffffffff;
	((u_long *)dest)[1] ^= 0xffffffff;
	((u_long *)dest)[2] ^= 0xffffffff;
	((u_long *)dest)[3] ^= 0xffffffff;
    }
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_cfb16(void)
#endif
{
    return(fbcon_register_driver(&dispsw_cfb16, 0));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_cfb16);
}
#endif /* MODULE */


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(packed16_cmap);
