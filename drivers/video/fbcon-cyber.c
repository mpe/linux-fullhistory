/*
 *  linux/drivers/video/cyber.c -- Low level frame buffer operations for the
 *				   CyberVision64 (accelerated)
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
#include "s3blit.h"


    /*
     *  Prototypes
     */

static int open_cyber(struct display *p);
static void release_cyber(void);
static void bmove_cyber(struct display *p, int sy, int sx, int dy, int dx,
	                int height, int width);
static void clear_cyber(struct vc_data *conp, struct display *p, int sy, int sx,
	                int height, int width);
static void putc_cyber(struct vc_data *conp, struct display *p, int c, int yy,
	               int xx);
static void putcs_cyber(struct vc_data *conp, struct display *p, const char *s,
	                int count, int yy, int xx);
static void rev_char_cyber(struct display *p, int xx, int yy);


    /*
     *  Acceleration functions in cyberfb.c
     */

extern void Cyber_WaitQueue(unsigned short fifo);
extern void Cyber_WaitBlit(void);
extern void Cyber_BitBLT(unsigned short curx, unsigned short cury,
			 unsigned short destx, unsigned short desty,
			 unsigned short width, unsigned short height,
			 unsigned short mode);
extern void Cyber_RectFill(unsigned short xx, unsigned short yy,
			   unsigned short width, unsigned short height,
			   unsigned short mode, unsigned short fillcolor);
extern void Cyber_MoveCursor(unsigned short xx, unsigned short yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_cyber = {
   open_cyber, release_cyber, bmove_cyber, clear_cyber, putc_cyber,
   putcs_cyber, rev_char_cyber
};


    /*
     *  CyberVision64 (accelerated)
     */

static int open_cyber(struct display *p)
{
    if (p->type != FB_TYPE_PACKED_PIXELS ||
	p->var.accel != FB_ACCEL_CYBERVISION)
	return -EINVAL;

    p->next_line = p->var.xres_virtual*p->var.bits_per_pixel>>3;
    p->next_plane = 0;
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_cyber(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_cyber(struct display *p, int sy, int sx, int dy, int dx,
	                int height, int width)
{
    sx *= 8; dx *= 8; width *= 8;
    Cyber_BitBLT((u_short)sx, (u_short)(sy*p->fontheight), (u_short)dx,
		 (u_short)(dy*p->fontheight), (u_short)width,
		 (u_short)(height*p->fontheight), (u_short)S3_NEW);
}

static void clear_cyber(struct vc_data *conp, struct display *p, int
			sy, int sx, int height, int width)
{
    unsigned char bg;

    sx *= 8; width *= 8;
    bg = attr_bgcol_ec(p,conp);
    Cyber_RectFill((u_short)sx,
		   (u_short)(sy*p->fontheight),
		   (u_short)width,
		   (u_short)(height*p->fontheight),
		   (u_short)S3_NEW,
		   (u_short)bg);
}

static void putc_cyber(struct vc_data *conp, struct display *p, int c, int yy,
	               int xx)
{
    u_char *dest, *cdat;
    u_long tmp;
    u_int rows, revs, underl;
    u_char d;
    u_char fg, bg;

    c &= 0xff;

    dest = p->screen_base+yy*p->fontheight*p->next_line+8*xx;
    cdat = p->fontdata+(c*p->fontheight);
    fg = p->fgcol;
    bg = p->bgcol;
    revs = conp->vc_reverse;
    underl = conp->vc_underline;

    Cyber_WaitBlit();
    for (rows = p->fontheight; rows--; dest += p->next_line) {
	d = *cdat++;

	if (underl && !rows)
	    d = 0xff;
	if (revs)
	    d = ~d;

	tmp =  ((d & 0x80) ? fg : bg) << 24;
	tmp |= ((d & 0x40) ? fg : bg) << 16;
	tmp |= ((d & 0x20) ? fg : bg) << 8;
	tmp |= ((d & 0x10) ? fg : bg);
	*((u_long*) dest) = tmp;
	tmp =  ((d & 0x8) ? fg : bg) << 24;
	tmp |= ((d & 0x4) ? fg : bg) << 16;
	tmp |= ((d & 0x2) ? fg : bg) << 8;
	tmp |= ((d & 0x1) ? fg : bg);
	*((u_long*) dest + 1) = tmp;
    }
}

static void putcs_cyber(struct vc_data *conp, struct display *p, const char *s,
	                int count, int yy, int xx)
{
    u_char *dest, *dest0, *cdat;
    u_long tmp;
    u_int rows, underl;
    u_char c, d;
    u_char fg, bg;

    dest0 = p->screen_base+yy*p->fontheight*p->next_line+8*xx;
    fg = p->fgcol;
    bg = p->bgcol;
    underl = conp->vc_underline;

    Cyber_WaitBlit();
    while (count--) {
	c = *s++;
	dest = dest0;
	dest0 += 8;
	cdat = p->fontdata+(c*p->fontheight);
	for (rows = p->fontheight; rows--; dest += p->next_line) {
	    d = *cdat++;

	    if (underl && !rows)
		d = 0xff;

	    tmp =  ((d & 0x80) ? fg : bg) << 24;
	    tmp |= ((d & 0x40) ? fg : bg) << 16;
	    tmp |= ((d & 0x20) ? fg : bg) << 8;
	    tmp |= ((d & 0x10) ? fg : bg);
	    *((u_long*) dest) = tmp;
	    tmp =  ((d & 0x8) ? fg : bg) << 24;
	    tmp |= ((d & 0x4) ? fg : bg) << 16;
	    tmp |= ((d & 0x2) ? fg : bg) << 8;
	    tmp |= ((d & 0x1) ? fg : bg);
	    *((u_long*) dest + 1) = tmp;
	}
    }
}


static void rev_char_cyber(struct display *p, int xx, int yy)
{
    unsigned char *dest;
    unsigned int rows;
    unsigned char fg, bg;

    fg = p->fgcol;
    bg = p->bgcol;

    dest = p->screen_base+yy*p->fontheight*p->next_line+8*xx;
    Cyber_WaitBlit();
    for (rows = p->fontheight; rows--; dest += p->next_line) {
	*dest = (*dest == fg) ? bg : fg;
	*(dest+1) = (*(dest + 1) == fg) ? bg : fg;
	*(dest+2) = (*(dest + 2) == fg) ? bg : fg;
	*(dest+3) = (*(dest + 3) == fg) ? bg : fg;
	*(dest+4) = (*(dest + 4) == fg) ? bg : fg;
	*(dest+5) = (*(dest + 5) == fg) ? bg : fg;
	*(dest+6) = (*(dest + 6) == fg) ? bg : fg;
	*(dest+7) = (*(dest + 7) == fg) ? bg : fg;
    }
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_cyber(void)
#endif
{
    return(fbcon_register_driver(&dispsw_cyber, 1));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_cyber);
}
#endif /* MODULE */
