/*
 *  linux/drivers/video/afb.c -- Low level frame buffer operations for
 *				 bitplanes à la Amiga
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
#include <linux/config.h>
#include <linux/fb.h>

#include "fbcon.h"


    /*
     *  Prototypes
     */

static int open_afb(struct display *p);
static void release_afb(void);
static void bmove_afb(struct display *p, int sy, int sx, int dy, int dx,
	              int height, int width);
static void clear_afb(struct vc_data *conp, struct display *p, int sy, int sx,
	              int height, int width);
static void putc_afb(struct vc_data *conp, struct display *p, int c, int yy,
	             int xx);
static void putcs_afb(struct vc_data *conp, struct display *p, const char *s,
	              int count, int yy, int xx);
static void rev_char_afb(struct display *p, int xx, int yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_afb = {
    open_afb, release_afb, bmove_afb, clear_afb, putc_afb, putcs_afb,
    rev_char_afb
};

    /*
     *  Bitplanes à la Amiga
     */

static int open_afb(struct display *p)
{
    if (p->type != FB_TYPE_PLANES)
	return -EINVAL;

    if (p->line_length)
	p->next_line = p->line_length;
    else
	p->next_line = p->var.xres_virtual>>3;
    p->next_plane = p->var.yres_virtual*p->next_line;
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_afb(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_afb(struct display *p, int sy, int sx, int dy, int dx,
	              int height, int width)
{
    u_char *src, *dest, *src0, *dest0;
    u_int i, rows;

    if (sx == 0 && dx == 0 && width == p->next_line) {
	src = p->screen_base+sy*p->fontheight*width;
	dest = p->screen_base+dy*p->fontheight*width;
	for (i = p->var.bits_per_pixel; i--;) {
	    mymemmove(dest, src, height*p->fontheight*width);
	    src += p->next_plane;
	    dest += p->next_plane;
	}
    } else if (dy <= sy) {
	src0 = p->screen_base+sy*p->fontheight*p->next_line+sx;
	dest0 = p->screen_base+dy*p->fontheight*p->next_line+dx;
	for (i = p->var.bits_per_pixel; i--;) {
	    src = src0;
	    dest = dest0;
	    for (rows = height*p->fontheight; rows--;) {
	        mymemmove(dest, src, width);
	        src += p->next_line;
	        dest += p->next_line;
	    }
	    src0 += p->next_plane;
	    dest0 += p->next_plane;
	}
    } else {
	src0 = p->screen_base+(sy+height)*p->fontheight*p->next_line+sx;
	dest0 = p->screen_base+(dy+height)*p->fontheight*p->next_line+dx;
	for (i = p->var.bits_per_pixel; i--;) {
	    src = src0;
	    dest = dest0;
	    for (rows = height*p->fontheight; rows--;) {
	        src -= p->next_line;
	        dest -= p->next_line;
	        mymemmove(dest, src, width);
	    }
	    src0 += p->next_plane;
	    dest0 += p->next_plane;
	}
    }
}

static void clear_afb(struct vc_data *conp, struct display *p, int sy, int sx,
	              int height, int width)
{
    u_char *dest, *dest0;
    u_int i, rows;
    int bg;

    dest0 = p->screen_base+sy*p->fontheight*p->next_line+sx;

    bg = attr_bgcol_ec(p,conp);
    for (i = p->var.bits_per_pixel; i--; dest0 += p->next_plane) {
	dest = dest0;
	for (rows = height*p->fontheight; rows--; dest += p->next_line)
	    if (bg & 1)
	        mymemset(dest, width);
	    else
	        mymemclear(dest, width);
	bg >>= 1;
    }
}

static void putc_afb(struct vc_data *conp, struct display *p, int c, int yy,
	             int xx)
{
    u_char *dest, *dest0, *cdat, *cdat0;
    u_int rows, i;
    u_char d;
    int fg, bg;

    c &= 0xff;

    dest0 = p->screen_base+yy*p->fontheight*p->next_line+xx;
    cdat0 = p->fontdata+c*p->fontheight;
    fg = attr_fgcol(p,conp);
    bg = attr_bgcol(p,conp);

    for (i = p->var.bits_per_pixel; i--; dest0 += p->next_plane) {
	dest = dest0;
	cdat = cdat0;
	for (rows = p->fontheight; rows--; dest += p->next_line) {
	    d = *cdat++;
	    if (bg & 1)
	        if (fg & 1)
	            *dest = 0xff;
	        else
	            *dest = ~d;
	    else
	        if (fg & 1)
	            *dest = d;
	        else
	            *dest = 0x00;
	}
	bg >>= 1;
	fg >>= 1;
    }
}

    /*
     *  I've split the console character loop in two parts
     *  (cfr. fbcon_putcs_ilbm())
     */

static void putcs_afb(struct vc_data *conp, struct display *p, const char *s,
	              int count, int yy, int xx)
{
    u_char *dest, *dest0, *dest1;
    u_char *cdat1, *cdat2, *cdat3, *cdat4, *cdat10, *cdat20, *cdat30, *cdat40;
    u_int rows, i;
    u_char c1, c2, c3, c4;
    u_long d;
    int fg0, bg0, fg, bg;

    dest0 = p->screen_base+yy*p->fontheight*p->next_line+xx;
    fg0 = attr_fgcol(p,conp);
    bg0 = attr_bgcol(p,conp);

    while (count--)
	if (xx&3 || count < 3) {	/* Slow version */
	    c1 = *s++;
	    dest1 = dest0++;
	    xx++;

	    cdat10 = p->fontdata+c1*p->fontheight;
	    fg = fg0;
	    bg = bg0;

	    for (i = p->var.bits_per_pixel; i--; dest1 += p->next_plane) {
	        dest = dest1;
	        cdat1 = cdat10;
	        for (rows = p->fontheight; rows--; dest += p->next_line) {
	            d = *cdat1++;
	            if (bg & 1)
	                if (fg & 1)
	                    *dest = 0xff;
	                else
	                    *dest = ~d;
	            else
	                if (fg & 1)
	                    *dest = d;
	                else
	                    *dest = 0x00;
	        }
	        bg >>= 1;
	        fg >>= 1;
	    }
	} else {	/* Fast version */
	    c1 = s[0];
	    c2 = s[1];
	    c3 = s[2];
	    c4 = s[3];

	    dest1 = dest0;
	    cdat10 = p->fontdata+c1*p->fontheight;
	    cdat20 = p->fontdata+c2*p->fontheight;
	    cdat30 = p->fontdata+c3*p->fontheight;
	    cdat40 = p->fontdata+c4*p->fontheight;
	    fg = fg0;
	    bg = bg0;

	    for (i = p->var.bits_per_pixel; i--; dest1 += p->next_plane) {
	        dest = dest1;
	        cdat1 = cdat10;
	        cdat2 = cdat20;
	        cdat3 = cdat30;
	        cdat4 = cdat40;
	        for (rows = p->fontheight; rows--; dest += p->next_line) {
	            d = *cdat1++<<24 | *cdat2++<<16 | *cdat3++<<8 | *cdat4++;
	            if (bg & 1)
	                if (fg & 1)
	                    *(u_long *)dest = 0xffffffff;
	                else
	                    *(u_long *)dest = ~d;
	            else
	                if (fg & 1)
	                    *(u_long *)dest = d;
	                else
	                    *(u_long *)dest = 0x00000000;
	        }
	        bg >>= 1;
	        fg >>= 1;
	    }
	    s += 4;
	    dest0 += 4;
	    xx += 4;
	    count -= 3;
	}
}

static void rev_char_afb(struct display *p, int xx, int yy)
{
    u_char *dest, *dest0;
    u_int rows, i;
    int mask;

    dest0 = p->screen_base+yy*p->fontheight*p->next_line+xx;
    mask = p->fgcol ^ p->bgcol;

    /*
     *  This should really obey the individual character's
     *  background and foreground colors instead of simply
     *  inverting.
     */

    for (i = p->var.bits_per_pixel; i--; dest0 += p->next_plane) {
	if (mask & 1) {
	    dest = dest0;
	    for (rows = p->fontheight; rows--; dest += p->next_line)
	        *dest = ~*dest;
	}
	mask >>= 1;
    }
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_afb(void)
#endif
{
    return(fbcon_register_driver(&dispsw_afb, 0));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_afb);
}
#endif /* MODULE */
