/*
 *  linux/drivers/video/iplan2p8.c -- Low level frame buffer operations for
 *				      interleaved bitplanes à la Atari (8
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

static int open_iplan2p8(struct display *p);
static void release_iplan2p8(void);
static void bmove_iplan2p8(struct display *p, int sy, int sx, int dy, int dx,
			   int height, int width);
static void clear_iplan2p8(struct vc_data *conp, struct display *p, int sy,
			   int sx, int height, int width);
static void putc_iplan2p8(struct vc_data *conp, struct display *p, int c,
			  int yy, int xx);
static void putcs_iplan2p8(struct vc_data *conp, struct display *p,
			   const char *s, int count, int yy, int xx);
static void rev_char_iplan2p8(struct display *display, int xx, int yy);


    /*
     *  `switch' for the low level operations
     */

static struct display_switch dispsw_iplan2p8 = {
    open_iplan2p8, release_iplan2p8, bmove_iplan2p8, clear_iplan2p8,
    putc_iplan2p8, putcs_iplan2p8, rev_char_iplan2p8
};


    /*
     *  Interleaved bitplanes à la Atari (8 planes, 2 bytes interleave)
     *
     *  In 8 plane mode, 256 colors would be possible, but only the first
     *  16 are used by the console code (the upper 4 bits are
     *  background/unused). For that, the following functions mask off the
     *  higher 4 bits of each color.
     */

/* Increment/decrement 8 plane addresses */

#define	INC_8P(p)	do { if (!((long)(++(p)) & 1)) (p) += 14; } while(0)
#define	DEC_8P(p)	do { if ((long)(--(p)) & 1) (p) -= 14; } while(0)


/* Sets the bytes in the visible column at d, height h, to the value
 * val1,val2 for a 8 plane screen. The the bis of the color in 'color' are
 * moved (8 times) to the respective bytes. This means:
 *
 * for(h times; d += bpr)
 *   *d      = (color & 1) ? 0xff : 0;
 *   *(d+2)  = (color & 2) ? 0xff : 0;
 *   *(d+4)  = (color & 4) ? 0xff : 0;
 *   *(d+6)  = (color & 8) ? 0xff : 0;
 *   *(d+8)  = (color & 16) ? 0xff : 0;
 *   *(d+10) = (color & 32) ? 0xff : 0;
 *   *(d+12) = (color & 64) ? 0xff : 0;
 *   *(d+14) = (color & 128) ? 0xff : 0;
 */

static __inline__ void memclear_8p_col(void *d, size_t h, u_long val1,
                                       u_long val2, int bpr)
{
#ifdef __mc68000__
    __asm__ __volatile__ ("1: movepl %4,%0@(0)\n\t"
			  "movepl %5,%0@(8)\n\t"
			  "addal  %6,%0\n\t"
			  "dbra	  %1,1b"
			  : "=a" (d), "=d" (h)
			  : "0" (d), "1" (h - 1), "d" (val1), "d" (val2),
			    "r" (bpr));
#endif /* !m68k */
}

/* Sets a 8 plane region from 'd', length 'count' bytes, to the color
 * val1..val4. 'd' has to be an even address and count must be divisible
 * by 16, because only whole words and all planes are accessed. I.e.:
 *
 * for(count/16 times)
 *   *d      = *(d+1)  = (color & 1) ? 0xff : 0;
 *   *(d+2)  = *(d+3)  = (color & 2) ? 0xff : 0;
 *   *(d+4)  = *(d+5)  = (color & 4) ? 0xff : 0;
 *   *(d+6)  = *(d+7)  = (color & 8) ? 0xff : 0;
 *   *(d+8)  = *(d+9)  = (color & 16) ? 0xff : 0;
 *   *(d+10) = *(d+11) = (color & 32) ? 0xff : 0;
 *   *(d+12) = *(d+13) = (color & 64) ? 0xff : 0;
 *   *(d+14) = *(d+15) = (color & 128) ? 0xff : 0;
 */

static __inline__ void memset_even_8p(void *d, size_t count, u_long val1,
                                      u_long val2, u_long val3, u_long val4)
{
    u_long *dd = d;

    count /= 16;
    while (count--) {
	*dd++ = val1;
	*dd++ = val2;
	*dd++ = val3;
	*dd++ = val4;
    }
}

/* Copies a 8 plane column from 's', height 'h', to 'd'. */

static __inline__ void memmove_8p_col (void *d, void *s, int h, int bpr)
{
    u_char *dd = d, *ss = s;

    while (h--) {
	dd[0] = ss[0];
	dd[2] = ss[2];
	dd[4] = ss[4];
	dd[6] = ss[6];
	dd[8] = ss[8];
	dd[10] = ss[10];
	dd[12] = ss[12];
	dd[14] = ss[14];
	dd += bpr;
	ss += bpr;
    }
}


/* This expands a 8 bit color into two longs for two movepl (8 plane)
 * operations.
 */

static __inline__ void expand8dl(u_char c, u_long *ret1, u_long *ret2)
{
    u_long	rv1, rv2;

#ifdef __mc68000__
    __asm__ __volatile__ ("lsrb	 #1,%3\n\t"
			  "scs	 %0\n\t"
			  "lsll	 #8,%0\n\t"
			  "lsrb	 #1,%3\n\t"
			  "scs	 %0\n\t"
			  "lsll	 #8,%0\n\t"
			  "lsrb	 #1,%3\n\t"
			  "scs	 %0\n\t"
			  "lsll	 #8,%0\n\t"
			  "lsrb	 #1,%3\n\t"
			  "scs	 %0\n\t"
			  "lsrb	 #1,%3\n\t"
			  "scs	 %1\n\t"
			  "lsll	 #8,%1\n\t"
			  "lsrb	 #1,%3\n\t"
			  "scs	 %1\n\t"
			  "lsll	 #8,%1\n\t"
			  "lsrb	 #1,%3\n\t"
			  "scs	 %1\n\t"
			  "lsll	 #8,%1\n\t"
			  "lsrb	 #1,%3\n\t"
			  "scs	 %1"
			  : "=&d" (rv1), "=&d" (rv2),"=d" (c)
			  : "2" (c));
#endif /* !m68k */
    *ret1 = rv1;
    *ret2 = rv2;
}

/* This expands a 8 bit color into four longs for four movel operations
 * (8 planes).
 */

#ifdef __mc68000__
/* ++andreas: use macro to avoid taking address of return values */
#define expand8ql(c, rv1, rv2, rv3, rv4) \
    do {								\
	u_char tmp = c;							\
	__asm__ __volatile__ ("lsrb  #1,%5\n\t"				\
			      "scs   %0\n\t"				\
			      "extw  %0\n\t"				\
			      "swap  %0\n\t"				\
			      "lsrb  #1,%5\n\t"				\
			      "scs   %0\n\t"				\
			      "extw  %0\n\t"				\
			      "lsrb  #1,%5\n\t"				\
			      "scs   %1\n\t"				\
			      "extw  %1\n\t"				\
			      "swap  %1\n\t"				\
			      "lsrb  #1,%5\n\t"				\
			      "scs   %1\n\t"				\
			      "extw  %1\n\t"				\
			      "lsrb  #1,%5\n\t"				\
			      "scs   %2\n\t"				\
			      "extw  %2\n\t"				\
			      "swap  %2\n\t"				\
			      "lsrb  #1,%5\n\t"				\
			      "scs   %2\n\t"				\
			      "extw  %2\n\t"				\
			      "lsrb  #1,%5\n\t"				\
			      "scs   %3\n\t"				\
			      "extw  %3\n\t"				\
			      "swap  %3\n\t"				\
			      "lsrb  #1,%5\n\t"				\
			      "scs   %3\n\t"				\
			      "extw  %3"				\
			      : "=&d" (rv1), "=&d" (rv2), "=&d" (rv3),	\
				"=&d" (rv4), "=d" (tmp)			\
			      : "4" (tmp));				\
    } while (0)
#endif /* !m68k */


/* This duplicates a byte 4 times into a long. */

static __inline__ u_long dup4l(u_char c)
{
    ushort	tmp;
    ulong	rv;

#ifdef __mc68000__
    __asm__ __volatile__ ("moveb  %2,%0\n\t"
			  "lslw   #8,%0\n\t"
			  "moveb  %2,%0\n\t"
			  "movew  %0,%1\n\t"
			  "swap   %0\n\t"
			  "movew  %1,%0"
			  : "=&d" (rv), "=d" (tmp)
			  : "d" (c));
#endif /* !m68k */
    return(rv);
}


static int open_iplan2p8(struct display *p)
{
    if (p->type != FB_TYPE_INTERLEAVED_PLANES || p->type_aux != 2 ||
	p->var.bits_per_pixel != 8)
	return -EINVAL;

    p->next_line = p->var.xres_virtual;
    p->next_plane = 2;
    MOD_INC_USE_COUNT;
    return 0;
}

static void release_iplan2p8(void)
{
    MOD_DEC_USE_COUNT;
}

static void bmove_iplan2p8(struct display *p, int sy, int sx, int dy, int dx,
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

     if (sx == 0 && dx == 0 && width == p->next_line/8) {
	/*  Special (but often used) case: Moving whole lines can be
	 *  done with memmove()
	 */
	fast_memmove(p->screen_base + dy * p->next_line * p->fontheight,
		     p->screen_base + sy * p->next_line * p->fontheight,
		     p->next_line * height * p->fontheight);
     } else {
	int rows, cols;
	u_char *src;
	u_char *dst;
	int bytes = p->next_line;
	int linesize = bytes * p->fontheight;
	u_int colsize = height * p->fontheight;
	u_int upwards = (dy < sy) || (dy == sy && dx < sx);

	if ((sx & 1) == (dx & 1)) {
	    /* odd->odd or even->even */

	    if (upwards) {
		src = p->screen_base + sy * linesize + (sx>>1)*16 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*16 + (dx & 1);
		if (sx & 1) {
		    memmove_8p_col(dst, src, colsize, bytes);
		    src += 15;
		    dst += 15;
		    --width;
		}
		if (width > 1) {
		    for(rows = colsize; rows > 0; --rows) {
			fast_memmove (dst, src, (width >> 1) * 16);
			src += bytes;
			dst += bytes;
		    }
		}

		if (width & 1) {
		    src -= colsize * bytes;
		    dst -= colsize * bytes;
		    memmove_8p_col(dst + (width>>1)*16, src + (width>>1)*16,
		    colsize, bytes);
		}
	    } else {
		if (!((sx+width-1) & 1)) {
		    src = p->screen_base + sy * linesize + ((sx+width-1)>>1)*16;
		    dst = p->screen_base + dy * linesize + ((dx+width-1)>>1)*16;
		    memmove_8p_col(dst, src, colsize, bytes);
		    --width;
		}
		src = p->screen_base + sy * linesize + (sx>>1)*16 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*16 + (dx & 1);
		if (width > 1) {
		    src += colsize * bytes + (sx & 1)*15;
		    dst += colsize * bytes + (sx & 1)*15;
		    for(rows = colsize; rows > 0; --rows) {
			src -= bytes;
			dst -= bytes;
			fast_memmove (dst, src, (width>>1)*16);
		    }
		}
		if (width & 1)
		    memmove_8p_col(dst-15, src-15, colsize, bytes);
	    }
	} else {
	/* odd->even or even->odd */

	    if (upwards) {
		src = p->screen_base + sy * linesize + (sx>>1)*16 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*16 + (dx & 1);
		for(cols = width; cols > 0; --cols) {
		    memmove_8p_col(dst, src, colsize, bytes);
		    INC_8P(src);
		    INC_8P(dst);
		}
	    } else {
		sx += width-1;
		dx += width-1;
		src = p->screen_base + sy * linesize + (sx>>1)*16 + (sx & 1);
		dst = p->screen_base + dy * linesize + (dx>>1)*16 + (dx & 1);
		for(cols = width; cols > 0; --cols) {
		    memmove_8p_col(dst, src, colsize, bytes);
		    DEC_8P(src);
		    DEC_8P(dst);
		}
	    }
	}
    }
}

static void clear_iplan2p8(struct vc_data *conp, struct display *p, int sy,
			   int sx, int height, int width)
{
    ulong offset;
    u_char *start;
    int rows;
    int bytes = p->next_line;
    int lines = height * p->fontheight;
    ulong size;
    u_long cval1, cval2, cval3, cval4, pcval1, pcval2;

    expand8ql(attr_bgcol_ec(p,conp), cval1, cval2, cval3, cval4);

    if (sx == 0 && width == bytes/8) {
	offset = sy * bytes * p->fontheight;
	size    = lines * bytes;
	memset_even_8p(p->screen_base+offset, size, cval1, cval2, cval3, cval4);
    } else {
	offset = (sy * bytes * p->fontheight) + (sx>>1)*16 + (sx & 1);
	start = p->screen_base + offset;
	expand8dl(attr_bgcol_ec(p,conp), &pcval1, &pcval2);

	/* Clears are split if the region starts at an odd column or
	* end at an even column. These extra columns are spread
	* across the interleaved planes. All in between can be
	* cleared by normal mymemclear_small(), because both bytes of
	* the single plane words are affected.
	*/

	if (sx & 1) {
	    memclear_8p_col(start, lines, pcval1, pcval2, bytes);
	    start += 7;
	    width--;
	}
	if (width & 1) {
	    memclear_8p_col(start + (width>>1)*16, lines, pcval1,
	    pcval2, bytes);
	    width--;
	}
	if (width)
	    for(rows = lines; rows-- ; start += bytes)
		memset_even_8p(start, width*8, cval1, cval2, cval3, cval4);
	}
}

static void putc_iplan2p8(struct vc_data *conp, struct display *p, int c,
			  int yy, int xx)
{
    u_char *dest;
    u_char *cdat;
    int rows;
    int bytes = p->next_line;
    ulong eorx1, eorx2, fgx1, fgx2, bgx1, bgx2, fdx;

    c &= 0xff;

    dest = p->screen_base + yy * p->fontheight * bytes + (xx>>1)*16 + (xx & 1);
    cdat = p->fontdata + (c * p->fontheight);

    expand8dl(attr_fgcol(p,conp), &fgx1, &fgx2);
    expand8dl(attr_bgcol(p,conp), &bgx1, &bgx2);
    eorx1 = fgx1 ^ bgx1; eorx2  = fgx2 ^ bgx2;

    for(rows = p->fontheight ; rows-- ; dest += bytes) {
	fdx = dup4l(*cdat++);
#ifdef __mc68000__
	__asm__ __volatile__ ("movepl %1,%0@(0)\n\t"
			      "movepl %2,%0@(8)"
			      : /* no outputs */
			      : "a" (dest), "d" ((fdx & eorx1) ^ bgx1),
				"d" ((fdx & eorx2) ^ bgx2) );
#endif /* !m68k */
    }
}

static void putcs_iplan2p8(struct vc_data *conp, struct display *p,
			   const char *s, int count, int yy, int xx)
{
    u_char *dest, *dest0;
    u_char *cdat, c;
    int rows;
    int bytes;
    ulong eorx1, eorx2, fgx1, fgx2, bgx1, bgx2, fdx;

    bytes = p->next_line;
    dest0 = p->screen_base + yy * p->fontheight * bytes + (xx>>1)*16 +
	    (xx & 1);

    expand8dl(attr_fgcol(p,conp), &fgx1, &fgx2);
    expand8dl(attr_bgcol(p,conp), &bgx1, &bgx2);
    eorx1 = fgx1 ^ bgx1; eorx2  = fgx2 ^ bgx2;

    while (count--) {

	/* I think, unrolling the loops like in the 1 plane case isn't
	* practicable here, because the body is much longer for 4
	* planes (mostly the dup4l()). I guess, unrolling this would
	* need more than 256 bytes and so exceed the instruction
	* cache :-(
	*/

	c = *s++;
	cdat  = p->fontdata + (c * p->fontheight);

	for(rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
	    fdx = dup4l(*cdat++);
#ifdef __mc68000__
	    __asm__ __volatile__ ("movepl %1,%0@(0)\n\t"
				  "movepl %2,%0@(8)"
				  : /* no outputs */
				  : "a" (dest), "d" ((fdx & eorx1) ^ bgx1),
				    "d" ((fdx & eorx2) ^ bgx2));
#endif /* !m68k */
	}
	INC_8P(dest0);
    }
}

static void rev_char_iplan2p8(struct display *p, int xx, int yy)
{
    u_char *dest;
    int j;
    int bytes;

    dest = p->screen_base + yy * p->fontheight * p->next_line + (xx>>1)*16 +
	   (xx & 1);
    j = p->fontheight;
    bytes = p->next_line;

    while (j--) {
	/*  This should really obey the individual character's
	 *  background and foreground colors instead of simply
	 *  inverting. For 8 plane mode, only the lower 4 bits of the
	 *  color are inverted, because only that color registers have
	 *  been set up.
	 */
	dest[0] = ~dest[0];
	dest[2] = ~dest[2];
	dest[4] = ~dest[4];
	dest[6] = ~dest[6];
	dest += bytes;
    }
}


#ifdef MODULE
int init_module(void)
#else
int fbcon_init_iplan2p8(void)
#endif
{
    return(fbcon_register_driver(&dispsw_iplan2p8, 0));
}

#ifdef MODULE
void cleanup_module(void)
{
    fbcon_unregister_driver(&dispsw_iplan2p8);
}
#endif /* MODULE */
