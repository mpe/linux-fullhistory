/*
 *  linux/drivers/video/mfb.c -- Low level frame buffer operations for
 *				 monochrome
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

static int open_mfb(struct display *p);
static void release_mfb(void);
static void bmove_mfb(struct display *p, int sy, int sx, int dy, int dx,
		      int height, int width);
static void clear_mfb(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width);
static void putc_mfb(struct vc_data *conp, struct display *p, int c, int yy,
		      int xx);
static void putcs_mfb(struct vc_data *conp, struct display *p, const char *s,
		      int count, int yy, int xx);
static void rev_char_mfb(struct display *p, int xx, int yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_mfb = {
    open_mfb, release_mfb, bmove_mfb, clear_mfb, putc_mfb, putcs_mfb,
    rev_char_mfb
};


    /*
     *  Monochrome
     */

static int open_mfb(struct display *p)
{
    if (p->var.bits_per_pixel != 1)
	return -EINVAL;

    if (p->line_length)
	p->next_line = p->line_length;
    else
	p->next_line = p->var.xres_virtual>>3;
    p->next_plane = 0;
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_mfb(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_mfb(struct display *p, int sy, int sx, int dy, int dx,
		      int height, int width)
{
    u_char *src, *dest;
    u_int rows;

    if (sx == 0 && dx == 0 && width == p->next_line) {
	src = p->screen_base+sy*p->fontheight*width;
	dest = p->screen_base+dy*p->fontheight*width;
	mymemmove(dest, src, height*p->fontheight*width);
    } else if (dy <= sy) {
	src = p->screen_base+sy*p->fontheight*p->next_line+sx;
	dest = p->screen_base+dy*p->fontheight*p->next_line+dx;
	for (rows = height*p->fontheight; rows--;) {
	    mymemmove(dest, src, width);
	    src += p->next_line;
	    dest += p->next_line;
	}
    } else {
	src = p->screen_base+((sy+height)*p->fontheight-1)*p->next_line+sx;
	dest = p->screen_base+((dy+height)*p->fontheight-1)*p->next_line+dx;
	for (rows = height*p->fontheight; rows--;) {
	    mymemmove(dest, src, width);
	    src -= p->next_line;
	    dest -= p->next_line;
	}
    }
}

static void clear_mfb(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
    u_char *dest;
    u_int rows;

    dest = p->screen_base+sy*p->fontheight*p->next_line+sx;

    if (sx == 0 && width == p->next_line) {
	if (attr_reverse(p,conp))
	    mymemset(dest, height*p->fontheight*width);
	else
	    mymemclear(dest, height*p->fontheight*width);
    } else
	for (rows = height*p->fontheight; rows--; dest += p->next_line)
	    if (attr_reverse(p,conp))
		mymemset(dest, width);
	    else
		mymemclear_small(dest, width);
}

static void putc_mfb(struct vc_data *conp, struct display *p, int c, int yy,
		     int xx)
{
    u_char *dest, *cdat;
    u_int rows, bold, revs, underl;
    u_char d;

    c &= 0xff;

    dest = p->screen_base+yy*p->fontheight*p->next_line+xx;
    cdat = p->fontdata+c*p->fontheight;
    bold = attr_bold(p,conp);
    revs = attr_reverse(p,conp);
    underl = attr_underline(p,conp);

    for (rows = p->fontheight; rows--; dest += p->next_line) {
	d = *cdat++;
	if (underl && !rows)
	    d = 0xff;
	else if (bold)
	    d |= d>>1;
	if (revs)
	    d = ~d;
	*dest = d;
    }
}

static void putcs_mfb(struct vc_data *conp, struct display *p, const char *s,
		      int count, int yy, int xx)
{
    u_char *dest, *dest0, *cdat;
    u_int rows, bold, revs, underl;
    u_char c, d;

    dest0 = p->screen_base+yy*p->fontheight*p->next_line+xx;
    bold = attr_bold(p,conp);
    revs = attr_reverse(p,conp);
    underl = attr_underline(p,conp);

    while (count--) {
	c = *s++;
	dest = dest0++;
	cdat = p->fontdata+c*p->fontheight;
	for (rows = p->fontheight; rows--; dest += p->next_line) {
	    d = *cdat++;
	    if (underl && !rows)
		d = 0xff;
	    else if (bold)
		d |= d>>1;
	    if (revs)
		d = ~d;
	    *dest = d;
	}
    }
}

static void rev_char_mfb(struct display *p, int xx, int yy)
{
    u_char *dest;
    u_int rows;

    dest = p->screen_base+yy*p->fontheight*p->next_line+xx;
    for (rows = p->fontheight; rows--; dest += p->next_line)
	*dest = ~*dest;
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_mfb(void)
#endif
{
    return(fbcon_register_driver(&dispsw_mfb, 0));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_mfb);
}
#endif /* MODULE */
