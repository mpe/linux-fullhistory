/*
 *  linux/drivers/video/iplan2p2.c -- Low level frame buffer operations for
 *				      interleaved bitplanes à la Atari (2
 *				      planes, 2 bytes interleave)
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


#ifndef __mc68000__
#error No support for non-m68k yet
#endif


    /*
     *  Prototypes
     */

static int open_iplan2p2(struct display *p);
static void release_iplan2p2(void);
static void bmove_iplan2p2(struct display *p, int sy, int sx, int dy, int dx,
			   int height, int width);
static void clear_iplan2p2(struct vc_data *conp, struct display *p, int sy,
			   int sx, int height, int width);
static void putc_iplan2p2(struct vc_data *conp, struct display *p, int c,
			  int yy, int xx);
static void putcs_iplan2p2(struct vc_data *conp, struct display *p,
			   const char *s, int count, int yy, int xx);
static void rev_char_iplan2p2(struct display *display, int xx, int yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_iplan2p2 = {
    open_iplan2p2, release_iplan2p2, bmove_iplan2p2, clear_iplan2p2,
    putc_iplan2p2, putcs_iplan2p2, rev_char_iplan2p2
};


    /*
     *  Interleaved bitplanes à la Atari (2 planes, 2 bytes interleave)
     */

/* Increment/decrement 2 plane addresses */

#define	INC_2P(p)	do { if (!((long)(++(p)) & 1)) (p) += 2; } while(0)
#define	DEC_2P(p)	do { if ((long)(--(p)) & 1) (p) -= 2; } while(0)

    /*  Convert a standard 4 bit color to our 2 bit color assignment:
     *  If at least two RGB channels are active, the low bit is turned on;
     *  The intensity bit (b3) is shifted into b1.
     */

#define	COLOR_2P(c)	(((c & 7) >= 3 && (c & 7) != 4) | (c & 8) >> 2)


/* Sets the bytes in the visible column at d, height h, to the value
 * val for a 2 plane screen. The the bis of the color in 'color' are
 * moved (8 times) to the respective bytes. This means:
 *
 * for(h times; d += bpr)
 *   *d     = (color & 1) ? 0xff : 0;
 *   *(d+2) = (color & 2) ? 0xff : 0;
 */

static __inline__ void memclear_2p_col(void *d, size_t h, u_short val, int bpr)
{
#ifdef __mc68000__
    __asm__ __volatile__ ("1: movepw %4,%0@(0)\n\t"
			  "addal  %5,%0\n\t"
			  "dbra	  %1,1b"
			  : "=a" (d), "=d" (h)
			  : "0" (d), "1" (h - 1), "d" (val), "r" (bpr));
#else /* !m68k */
#endif /* !m68k */
}

/* Sets a 2 plane region from 'd', length 'count' bytes, to the color
 * in val1. 'd' has to be an even address and count must be divisible
 * by 8, because only whole words and all planes are accessed. I.e.:
 *
 * for(count/4 times)
 *   *d     = *(d+1) = (color & 1) ? 0xff : 0;
 *   *(d+2) = *(d+3) = (color & 2) ? 0xff : 0;
 */

static __inline__ void memset_even_2p(void *d, size_t count, u_long val)
{
    u_long *dd = d;

    count /= 4;
    while (count--)
	*dd++ = val;
}

/* Copies a 2 plane column from 's', height 'h', to 'd'. */

static __inline__ void memmove_2p_col (void *d, void *s, int h, int bpr)
{
    u_char *dd = d, *ss = s;

    while (h--) {
	dd[0] = ss[0];
	dd[2] = ss[2];
	dd += bpr;
	ss += bpr;
    }
}


/* This expands a 2 bit color into a short for movepw (2 plane) operations. */

static __inline__ u_short expand2w(u_char c)
{
    u_short	rv;

#ifdef __mc68000__
    __asm__ __volatile__ ("lsrb	 #1,%2\n\t"
			  "scs	 %0\n\t"
			  "lsll	 #8,%0\n\t"
			  "lsrb	 #1,%2\n\t"
			  "scs	 %0\n\t"
			  : "=&d" (rv), "=d" (c)
			  : "1" (c));
#endif /* !m68k */
    return(rv);
}

/* This expands a 2 bit color into one long for a movel operation
 * (2 planes).
 */

static __inline__ u_long expand2l(u_char c)
{
    u_long	rv;

#ifdef __mc68000__
    __asm__ __volatile__ ("lsrb	 #1,%2\n\t"
			  "scs	 %0\n\t"
			  "extw	 %0\n\t"
			  "swap	 %0\n\t"
			  "lsrb	 #1,%2\n\t"
			  "scs	 %0\n\t"
			  "extw	 %0\n\t"
			  : "=&d" (rv), "=d" (c)
			  : "1" (c));
#endif /* !m68k */
    return rv;
}


/* This duplicates a byte 2 times into a short. */

static __inline__ u_short dup2w(u_char c)
{
    ushort  rv;

#ifdef __mc68000__
    __asm__ __volatile__ ("moveb  %1,%0\n\t"
			  "lslw   #8,%0\n\t"
			  "moveb  %1,%0\n\t"
			  : "=&d" (rv)
			  : "d" (c));
#endif /* !m68k */
    return( rv );
}


static int open_iplan2p2(struct display *p)
{
    if (p->type != FB_TYPE_INTERLEAVED_PLANES || p->type_aux != 2 ||
	p->var.bits_per_pixel != 2)
	return -EINVAL;

    p->next_line = p->var.xres_virtual>>2;
    p->next_plane = 2;
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_iplan2p2(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_iplan2p2(struct display *p, int sy, int sx, int dy, int dx,
			   int height, int width)
{
    /*  bmove() has to distinguish two major cases: If both, source and
     *  destination, start at even addresses or both are at odd
     *  addresses, just the first odd and last even column (if present)
     *  require special treatment (memmove_col()). The rest between
     *  then can be copied by normal operations, because all adjacent
     *  bytes are affected and are to be stored in the same order.
     *    The pathological case is when the move should go from an odd
     *  address to an even or vice versa. Since the bytes in the plane
     *  words must be assembled in new order, it seems wisest to make
     *  all movements by memmove_col().
     */

    if (sx == 0 && dx == 0 && width == p->next_line/2) {
	/*  Special (but often used) case: Moving whole lines can be
	 *  done with memmove()
	 */
	mymemmove(p->screen_base + dy * p->next_line * p->fontheight,
		  p->screen_base + sy * p->next_line * p->fontheight,
		  p->next_line * height * p->fontheight);
    } else {
	int rows, cols;
	u_char *src;
	u_char *dst;
	int bytes = p->next_line;
	int linesize = bytes * p->fontheight;
	u_int colsize  = height * p->fontheight;
	u_int upwards  = (dy < sy) || (dy == sy && dx < sx);

	if ((sx & 1) == (dx & 1)) {
	    /* odd->odd or even->even */
	    if (upwards) {
		src = p->screen_base + sy * linesize + (sx>>1)*4 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*4 + (dx & 1);
		if (sx & 1) {
		    memmove_2p_col(dst, src, colsize, bytes);
		    src += 3;
		    dst += 3;
		    --width;
		}
		if (width > 1) {
		    for (rows = colsize; rows > 0; --rows) {
			mymemmove(dst, src, (width>>1)*4);
			src += bytes;
			dst += bytes;
		    }
		}
		if (width & 1) {
		    src -= colsize * bytes;
		    dst -= colsize * bytes;
		    memmove_2p_col(dst + (width>>1)*4, src + (width>>1)*4,
				   colsize, bytes);
		}
	    } else {
		if (!((sx+width-1) & 1)) {
		    src = p->screen_base + sy * linesize + ((sx+width-1)>>1)*4;
		    dst = p->screen_base + dy * linesize + ((dx+width-1)>>1)*4;
		    memmove_2p_col(dst, src, colsize, bytes);
		    --width;
		}
		src = p->screen_base + sy * linesize + (sx>>1)*4 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*4 + (dx & 1);
		if (width > 1) {
		    src += colsize * bytes + (sx & 1)*3;
		    dst += colsize * bytes + (sx & 1)*3;
		    for(rows = colsize; rows > 0; --rows) {
			src -= bytes;
			dst -= bytes;
			mymemmove(dst, src, (width>>1)*4);
		    }
		}
		if (width & 1)
		    memmove_2p_col(dst-3, src-3, colsize, bytes);
	    }
	} else {
	    /* odd->even or even->odd */
	    if (upwards) {
		src = p->screen_base + sy * linesize + (sx>>1)*4 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*4 + (dx & 1);
		for (cols = width; cols > 0; --cols) {
		    memmove_2p_col(dst, src, colsize, bytes);
		    INC_2P(src);
		    INC_2P(dst);
		}
	    } else {
		sx += width-1;
		dx += width-1;
		src = p->screen_base + sy * linesize + (sx>>1)*4 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*4 + (dx & 1);
		for(cols = width; cols > 0; --cols) {
		    memmove_2p_col(dst, src, colsize, bytes);
		    DEC_2P(src);
		    DEC_2P(dst);
		}
	    }
	}
    }
}

static void clear_iplan2p2(struct vc_data *conp, struct display *p, int sy,
			   int sx, int height, int width)
{
    ulong offset;
    u_char *start;
    int rows;
    int bytes = p->next_line;
    int lines = height * p->fontheight;
    ulong size;
    u_long cval;
    u_short pcval;

    cval = expand2l (COLOR_2P (attr_bgcol_ec(p,conp)));

    if (sx == 0 && width == bytes/2) {
	offset = sy * bytes * p->fontheight;
	size = lines * bytes;
	memset_even_2p(p->screen_base+offset, size, cval);
    } else {
	offset = (sy * bytes * p->fontheight) + (sx>>1)*4 + (sx & 1);
	start = p->screen_base + offset;
	pcval = expand2w(COLOR_2P(attr_bgcol_ec(p,conp)));

	/*  Clears are split if the region starts at an odd column or
	 *  end at an even column. These extra columns are spread
	 *  across the interleaved planes. All in between can be
	 *  cleared by normal mymemclear_small(), because both bytes of
	 *  the single plane words are affected.
	 */

	if (sx & 1) {
	    memclear_2p_col(start, lines, pcval, bytes);
	    start += 3;
	    width--;
	}
	if (width & 1) {
	    memclear_2p_col(start + (width>>1)*4, lines, pcval, bytes);
	    width--;
	}
	if (width) {
	    for (rows = lines; rows-- ; start += bytes)
	    memset_even_2p(start, width*2, cval);
	}
    }
}

static void putc_iplan2p2(struct vc_data *conp, struct display *p, int c,
			  int yy, int xx)
{
    u_char *dest;
    u_char *cdat;
    int rows;
    int bytes = p->next_line;
    ulong eorx, fgx, bgx, fdx;

    c &= 0xff;

    dest = p->screen_base + yy * p->fontheight * bytes + (xx>>1)*4 + (xx & 1);
    cdat = p->fontdata + (c * p->fontheight);

    fgx = expand2w(COLOR_2P(attr_fgcol(p,conp)));
    bgx = expand2w(COLOR_2P(attr_bgcol(p,conp)));
    eorx = fgx ^ bgx;

    for (rows = p->fontheight ; rows-- ; dest += bytes) {
	fdx = dup2w(*cdat++);
#ifdef __mc68000__
	__asm__ __volatile__ ("movepw %1,%0@(0)"
			      : /* no outputs */
			      : "a" (dest), "d" ((fdx & eorx) ^ bgx));
#endif /* !m68k */
    }
}

static void putcs_iplan2p2(struct vc_data *conp, struct display *p,
			   const char *s, int count, int yy, int xx)
{
    u_char *dest, *dest0;
    u_char *cdat, c;
    int rows;
    int bytes;
    ulong eorx, fgx, bgx, fdx;

    bytes = p->next_line;
    dest0 = p->screen_base + yy * p->fontheight * bytes + (xx>>1)*4 + (xx & 1);
    fgx = expand2w(COLOR_2P(attr_fgcol(p,conp)));
    bgx = expand2w(COLOR_2P(attr_bgcol(p,conp)));
    eorx = fgx ^ bgx;

    while (count--) {
	c = *s++;
	cdat  = p->fontdata + (c * p->fontheight);

	for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
	    fdx = dup2w(*cdat++);
#ifdef __mc68000__
	    __asm__ __volatile__ ("movepw %1,%0@(0)"
				  : /* no outputs */
				  : "a" (dest), "d" ((fdx & eorx) ^ bgx));
#endif /* !m68k */
	}
	INC_2P(dest0);
    }
}

static void rev_char_iplan2p2(struct display *p, int xx, int yy)
{
    u_char *dest;
    int j;
    int bytes;

    dest = p->screen_base + yy * p->fontheight * p->next_line + (xx>>1)*4 +
	   (xx & 1);
    j = p->fontheight;
    bytes = p->next_line;
    while (j--) {
	/*  This should really obey the individual character's
	 *  background and foreground colors instead of simply
	 *  inverting.
	 */
	dest[0] = ~dest[0];
	dest[2] = ~dest[2];
	dest += bytes;
    }
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_iplan2p2(void)
#endif
{
    return(fbcon_register_driver(&dispsw_iplan2p2, 0));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_iplan2p2);
}
#endif /* MODULE */
