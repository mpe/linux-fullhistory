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


    /*
     *  Prototypes
     */

static int open_cfb8(struct display *p);
static void release_cfb8(void);
static void bmove_cfb8(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width);
static void clear_cfb8(struct vc_data *conp, struct display *p, int sy,
		       int sx, int height, int width);
static void putc_cfb8(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx);
static void putcs_cfb8(struct vc_data *conp, struct display *p,
		       const char *s, int count, int yy, int xx);
static void rev_char_cfb8(struct display *p, int xx, int yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_cfb8 = {
    open_cfb8, release_cfb8, bmove_cfb8, clear_cfb8, putc_cfb8, putcs_cfb8,
    rev_char_cfb8
};


    /*
     *  8 bpp packed pixels
     */

static u_long nibbletab_cfb8[] = {
    0x00000000,0x000000ff,0x0000ff00,0x0000ffff,
    0x00ff0000,0x00ff00ff,0x00ffff00,0x00ffffff,
    0xff000000,0xff0000ff,0xff00ff00,0xff00ffff,
    0xffff0000,0xffff00ff,0xffffff00,0xffffffff
};

static int open_cfb8(struct display *p)
{
    if (p->type != FB_TYPE_PACKED_PIXELS || p->var.bits_per_pixel != 8)
	return -EINVAL;

    p->next_line = p->var.xres_virtual;
    p->next_plane = 0;
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_cfb8(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_cfb8(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
    int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
    u_char *src,*dst;

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

static void clear_cfb8(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u_char *dest0,*dest;
    int bytes=p->next_line,lines=height * p->fontheight, rows, i;
    u_long bgx;

    dest = p->screen_base + sy * p->fontheight * bytes + sx * 8;

    bgx=attr_bgcol_ec(p,conp);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);

    if (sx == 0 && width * 8 == bytes)
	for (i = 0 ; i < lines * width ; i++) {
	    ((u_long *)dest)[0]=bgx;
	    ((u_long *)dest)[1]=bgx;
	    dest+=8;
	}
    else {
	dest0=dest;
	for (rows = lines; rows-- ; dest0 += bytes) {
	    dest=dest0;
	    for (i = 0 ; i < width ; i++) {
		((u_long *)dest)[0]=bgx;
		((u_long *)dest)[1]=bgx;
		dest+=8;
	    }
	}
    }
}

static void putc_cfb8(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx)
{
    u_char *dest,*cdat;
    int bytes=p->next_line,rows;
    ulong eorx,fgx,bgx;

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
	((u_long *)dest)[0]= (nibbletab_cfb8[*cdat >> 4] & eorx) ^ bgx;
	((u_long *)dest)[1]= (nibbletab_cfb8[*cdat++ & 0xf] & eorx) ^ bgx;
    }
}

static void putcs_cfb8(struct vc_data *conp, struct display *p, const char *s,
		       int count, int yy, int xx)
{
    u_char *cdat, c, *dest, *dest0;
    int rows,bytes=p->next_line;
    u_long eorx, fgx, bgx;

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
	    ((u_long *)dest)[0]= (nibbletab_cfb8[*cdat >> 4] & eorx) ^ bgx;
	    ((u_long *)dest)[1]= (nibbletab_cfb8[*cdat++ & 0xf] & eorx) ^
				 bgx;
	}
	dest0+=8;
    }
}

static void rev_char_cfb8(struct display *p, int xx, int yy)
{
    u_char *dest;
    int bytes=p->next_line, rows;

    dest = p->screen_base + yy * p->fontheight * bytes + xx * 8;
    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	((u_long *)dest)[0] ^= 0x0f0f0f0f;
	((u_long *)dest)[1] ^= 0x0f0f0f0f;
    }
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_cfb8(void)
#endif
{
    return(fbcon_register_driver(&dispsw_cfb8, 0));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_cfb8);
}
#endif /* MODULE */
