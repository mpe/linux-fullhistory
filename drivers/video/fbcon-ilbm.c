/*
 *  linux/drivers/video/ilbm.c -- Low level frame buffer operations for
 *				  interleaved bitplanes à la Amiga
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

static int open_ilbm(struct display *p);
static void release_ilbm(void);
static void bmove_ilbm(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width);
static void clear_ilbm(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width);
static void putc_ilbm(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx);
static void putcs_ilbm(struct vc_data *conp, struct display *p, const char *s,
		       int count, int yy, int xx);
static void rev_char_ilbm(struct display *p, int xx, int yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_ilbm = {
    open_ilbm, release_ilbm, bmove_ilbm, clear_ilbm, putc_ilbm, putcs_ilbm,
    rev_char_ilbm
};


    /*
     *  Interleaved bitplanes à la Amiga
     *
     *  This code heavily relies on the fact that
     *
     *      next_line == interleave == next_plane*bits_per_pixel
     *
     *  But maybe it can be merged with the code for normal bitplanes without
     *  much performance loss?
     */

static int open_ilbm(struct display *p)
{
    if (p->type != FB_TYPE_INTERLEAVED_PLANES || p->type_aux == 2)
	return -EINVAL;

    if (p->line_length) {
	p->next_line = p->line_length*p->var.bits_per_pixel;
	p->next_plane = p->line_length;
    } else {
	p->next_line = p->type_aux;
	p->next_plane = p->type_aux/p->var.bits_per_pixel;
    }
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_ilbm(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_ilbm(struct display *p, int sy, int sx, int dy, int dx,
		       int height, int width)
{
    if (sx == 0 && dx == 0 && width == p->next_plane)
	mymemmove(p->screen_base+dy*p->fontheight*p->next_line,
		  p->screen_base+sy*p->fontheight*p->next_line,
		  height*p->fontheight*p->next_line);
    else {
	u_char *src, *dest;
	u_int i;

	if (dy <= sy) {
	    src = p->screen_base+sy*p->fontheight*p->next_line+sx;
	    dest = p->screen_base+dy*p->fontheight*p->next_line+dx;
	    for (i = p->var.bits_per_pixel*height*p->fontheight; i--;) {
		mymemmove(dest, src, width);
		src += p->next_plane;
		dest += p->next_plane;
	    }
	} else {
	    src = p->screen_base+(sy+height)*p->fontheight*p->next_line+sx;
	    dest = p->screen_base+(dy+height)*p->fontheight*p->next_line+dx;
	    for (i = p->var.bits_per_pixel*height*p->fontheight; i--;) {
		src -= p->next_plane;
		dest -= p->next_plane;
		mymemmove(dest, src, width);
	    }
	}
    }
}

static void clear_ilbm(struct vc_data *conp, struct display *p, int sy, int sx,
		       int height, int width)
{
    u_char *dest;
    u_int i, rows;
    int bg, bg0;

    dest = p->screen_base+sy*p->fontheight*p->next_line+sx;

    bg0 = attr_bgcol_ec(p,conp);
    for (rows = height*p->fontheight; rows--;) {
	bg = bg0;
	for (i = p->var.bits_per_pixel; i--; dest += p->next_plane) {
	    if (bg & 1)
		mymemset(dest, width);
	    else
		mymemclear(dest, width);
	    bg >>= 1;
	}
    }
}

static void putc_ilbm(struct vc_data *conp, struct display *p, int c, int yy,
			     int xx)
{
    u_char *dest, *cdat;
    u_int rows, i;
    u_char d;
    int fg0, bg0, fg, bg;

    c &= 0xff;

    dest = p->screen_base+yy*p->fontheight*p->next_line+xx;
    cdat = p->fontdata+c*p->fontheight;
    fg0 = attr_fgcol(p,conp);
    bg0 = attr_bgcol(p,conp);

    for (rows = p->fontheight; rows--;) {
	d = *cdat++;
	fg = fg0;
	bg = bg0;
	for (i = p->var.bits_per_pixel; i--; dest += p->next_plane) {
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
	    bg >>= 1;
	    fg >>= 1;
	}
    }
}

    /*
     *  I've split the console character loop in two parts:
     *
     *      - slow version: this blits one character at a time
     *
     *      - fast version: this blits 4 characters at a time at a longword
     *			    aligned address, to reduce the number of expensive
     *			    Chip RAM accesses.
     *
     *  Experiments on my A4000/040 revealed that this makes a console switch
     *  on a 640x400 screen with 256 colors about 3 times faster.
     *
     *  -- Geert
     */

static void putcs_ilbm(struct vc_data *conp, struct display *p, const char *s,
		       int count, int yy, int xx)
{
    u_char *dest0, *dest, *cdat1, *cdat2, *cdat3, *cdat4;
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
	    dest = dest0++;
	    xx++;

	    cdat1 = p->fontdata+c1*p->fontheight;
	    for (rows = p->fontheight; rows--;) {
		d = *cdat1++;
		fg = fg0;
		bg = bg0;
		for (i = p->var.bits_per_pixel; i--; dest += p->next_plane) {
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
		    bg >>= 1;
		    fg >>= 1;
		}
	    }
	} else {		/* Fast version */
	    c1 = s[0];
	    c2 = s[1];
	    c3 = s[2];
	    c4 = s[3];

	    dest = dest0;
	    cdat1 = p->fontdata+c1*p->fontheight;
	    cdat2 = p->fontdata+c2*p->fontheight;
	    cdat3 = p->fontdata+c3*p->fontheight;
	    cdat4 = p->fontdata+c4*p->fontheight;
	    for (rows = p->fontheight; rows--;) {
		d = *cdat1++<<24 | *cdat2++<<16 | *cdat3++<<8 | *cdat4++;
		fg = fg0;
		bg = bg0;
		for (i = p->var.bits_per_pixel; i--; dest += p->next_plane) {
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
		    bg >>= 1;
		    fg >>= 1;
		}
	    }
	    s += 4;
	    dest0 += 4;
	    xx += 4;
	    count -= 3;
	}
}

static void rev_char_ilbm(struct display *p, int xx, int yy)
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
int fbcon_init_ilbm(void)
#endif
{
    return(fbcon_register_driver(&dispsw_ilbm, 0));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_ilbm);
}
#endif /* MODULE */
