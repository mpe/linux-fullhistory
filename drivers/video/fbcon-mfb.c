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
#include <linux/fb.h>

#include "fbcon.h"
#include "fbcon-mfb.h"


    /*
     *  Monochrome
     */

void fbcon_mfb_setup(struct display *p)
{
    if (p->line_length)
	p->next_line = p->line_length;
    else
	p->next_line = p->var.xres_virtual>>3;
    p->next_plane = 0;
}

void fbcon_mfb_bmove(struct display *p, int sy, int sx, int dy, int dx,
		     int height, int width)
{
    u8 *src, *dest;
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

void fbcon_mfb_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		     int height, int width)
{
    u8 *dest;
    u_int rows;

    dest = p->screen_base+sy*p->fontheight*p->next_line+sx;

    if (sx == 0 && width == p->next_line) {
	if (attr_reverse(p,conp->vc_attr))
	    mymemset(dest, height*p->fontheight*width);
	else
	    mymemclear(dest, height*p->fontheight*width);
    } else
	for (rows = height*p->fontheight; rows--; dest += p->next_line)
	    if (attr_reverse(p,conp->vc_attr))
		mymemset(dest, width);
	    else
		mymemclear_small(dest, width);
}

void fbcon_mfb_putc(struct vc_data *conp, struct display *p, int c, int yy,
		    int xx)
{
    u8 *dest, *cdat;
    u_int rows, bold, revs, underl;
    u8 d;

    dest = p->screen_base+yy*p->fontheight*p->next_line+xx;
    cdat = p->fontdata+(c&0xff)*p->fontheight;
    bold = attr_bold(p,c);
    revs = attr_reverse(p,c);
    underl = attr_underline(p,c);

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

void fbcon_mfb_putcs(struct vc_data *conp, struct display *p, 
		     const unsigned short *s, int count, int yy, int xx)
{
    u8 *dest, *dest0, *cdat;
    u_int rows, bold, revs, underl;
    u8 c, d;

    dest0 = p->screen_base+yy*p->fontheight*p->next_line+xx;
    bold = attr_bold(p,*s);
    revs = attr_reverse(p,*s);
    underl = attr_underline(p,*s);

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

void fbcon_mfb_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    u_int rows;

    dest = p->screen_base+yy*p->fontheight*p->next_line+xx;
    for (rows = p->fontheight; rows--; dest += p->next_line)
	*dest = ~*dest;
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_mfb = {
    fbcon_mfb_setup, fbcon_mfb_bmove, fbcon_mfb_clear, fbcon_mfb_putc,
    fbcon_mfb_putcs, fbcon_mfb_revc, NULL
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

EXPORT_SYMBOL(fbcon_mfb);
EXPORT_SYMBOL(fbcon_mfb_setup);
EXPORT_SYMBOL(fbcon_mfb_bmove);
EXPORT_SYMBOL(fbcon_mfb_clear);
EXPORT_SYMBOL(fbcon_mfb_putc);
EXPORT_SYMBOL(fbcon_mfb_putcs);
EXPORT_SYMBOL(fbcon_mfb_revc);
