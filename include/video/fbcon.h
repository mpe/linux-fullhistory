/*
 *  linux/drivers/video/fbcon.h -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _VIDEO_FBCON_H
#define _VIDEO_FBCON_H

#include <linux/config.h>
#include <linux/console_struct.h>


    /*                                  
     *  `switch' for the Low Level Operations
     */
 
struct display_switch {                                                
    void (*setup)(struct display *p);
    void (*bmove)(struct display *p, int sy, int sx, int dy, int dx,
		  int height, int width);
    /* for clear, conp may be NULL, which means use a blanking (black) color */
    void (*clear)(struct vc_data *conp, struct display *p, int sy, int sx,
		  int height, int width);
    void (*putc)(struct vc_data *conp, struct display *p, int c, int yy,
    		 int xx);
    void (*putcs)(struct vc_data *conp, struct display *p, const unsigned short *s,
		  int count, int yy, int xx);     
    void (*revc)(struct display *p, int xx, int yy);
    void (*cursor)(struct display *p, int mode, int xx, int yy);
    int  (*set_font)(struct display *p, int width, int height);
    void (*clear_margins)(struct vc_data *conp, struct display *p,
			  int bottom_only);
    unsigned int fontwidthmask;      /* 1 at (1 << (width - 1)) if width is supported */
}; 

extern struct display_switch fbcon_dummy;

#define fontheight(p) ((p)->_fontheight)
#define fontheightlog(p) ((p)->_fontheightlog)

#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY

/* fontwidth w is supported by dispsw */
#define FONTWIDTH(w)	(1 << ((8) - 1))
/* fontwidths w1-w2 inclusive are supported by dispsw */
#define FONTWIDTHRANGE(w1,w2)	FONTWIDTH(8)

#define fontwidth(p) (8)
#define fontwidthlog(p) (0)

#else

/* fontwidth w is supported by dispsw */
#define FONTWIDTH(w)	(1 << ((w) - 1))
/* fontwidths w1-w2 inclusive are supported by dispsw */
#define FONTWIDTHRANGE(w1,w2)	(FONTWIDTH(w2+1) - FONTWIDTH(w1))

#define fontwidth(p) ((p)->_fontwidth)
#define fontwidthlog(p) ((p)->_fontwidthlog)

#endif

    /*
     *  Attribute Decoding
     */

/* Color */
#define attr_fgcol(p,s)    \
	(((s) >> ((p)->fgshift)) & 0x0f)
#define attr_bgcol(p,s)    \
	(((s) >> ((p)->bgshift)) & 0x0f)
#define	attr_bgcol_ec(p,conp) \
	((conp) ? (((conp)->vc_video_erase_char >> ((p)->bgshift)) & 0x0f) : 0)

/* Monochrome */
#define attr_bold(p,s) \
	((s) & 0x200)
#define attr_reverse(p,s) \
	(((s) & 0x800) ^ ((p)->inverse ? 0x800 : 0))
#define attr_underline(p,s) \
	((s) & 0x400)
#define attr_blink(p,s) \
	((s) & 0x8000)
	
    /*
     *  Scroll Method
     */
     
/* Internal flags */
#define __SCROLL_YPAN		0x001
#define __SCROLL_YWRAP		0x002
#define __SCROLL_YMOVE		0x003
#define __SCROLL_YREDRAW	0x004
#define __SCROLL_YMASK		0x00f
#define __SCROLL_YFIXED		0x010
#define __SCROLL_YNOMOVE	0x020
#define __SCROLL_YPANREDRAW	0x040
#define __SCROLL_YNOPARTIAL	0x080

/* Only these should be used by the drivers */
/* Which one should you use? If you have a fast card and slow bus,
   then probably just 0 to indicate fbcon should choose between
   YWRAP/YPAN+MOVE/YMOVE. On the other side, if you have a fast bus
   and even better if your card can do fonting (1->8/32bit painting),
   you should consider either SCROLL_YREDRAW (if your card is
   able to do neither YPAN/YWRAP), or SCROLL_YNOMOVE.
   The best is to test it with some real life scrolling (usually, not
   all lines on the screen are filled completely with non-space characters,
   and REDRAW performs much better on such lines, so don't cat a file
   with every line covering all screen columns, it would not be the right
   benchmark).
 */
#define SCROLL_YREDRAW		(__SCROLL_YFIXED|__SCROLL_YREDRAW)
#define SCROLL_YNOMOVE		(__SCROLL_YNOMOVE|__SCROLL_YPANREDRAW)

/* SCROLL_YNOPARTIAL, used in combination with the above, is for video
   cards which can not handle using panning to scroll a portion of the
   screen without excessive flicker.  Panning will only be used for
   whole screens.
 */
/* Namespace consistency */
#define SCROLL_YNOPARTIAL	__SCROLL_YNOPARTIAL


extern void fbcon_redraw_bmove(struct display *, int, int, int, int, int, int);


/* ================================================================= */
/*                      Utility Assembler Functions                  */
/* ================================================================= */


#if defined(__mc68000__)

/* ====================================================================== */

/* Those of a delicate disposition might like to skip the next couple of
 * pages.
 *
 * These functions are drop in replacements for memmove and
 * memset(_, 0, _). However their five instances add at least a kilobyte
 * to the object file. You have been warned.
 *
 * Not a great fan of assembler for the sake of it, but I think
 * that these routines are at least 10 times faster than their C
 * equivalents for large blits, and that's important to the lowest level of
 * a graphics driver. Question is whether some scheme with the blitter
 * would be faster. I suspect not for simple text system - not much
 * asynchrony.
 *
 * Code is very simple, just gruesome expansion. Basic strategy is to
 * increase data moved/cleared at each step to 16 bytes to reduce
 * instruction per data move overhead. movem might be faster still
 * For more than 15 bytes, we try to align the write direction on a
 * longword boundary to get maximum speed. This is even more gruesome.
 * Unaligned read/write used requires 68020+ - think this is a problem?
 *
 * Sorry!
 */


/* ++roman: I've optimized Robert's original versions in some minor
 * aspects, e.g. moveq instead of movel, let gcc choose the registers,
 * use movem in some places...
 * For other modes than 1 plane, lots of more such assembler functions
 * were needed (e.g. the ones using movep or expanding color values).
 */

/* ++andreas: more optimizations:
   subl #65536,d0 replaced by clrw d0; subql #1,d0 for dbcc
   addal is faster than addaw
   movep is rather expensive compared to ordinary move's
   some functions rewritten in C for clarity, no speed loss */

static __inline__ void *mymemclear_small(void *s, size_t count)
{
   if (!count)
      return(0);

   __asm__ __volatile__(
         "lsrl   #1,%1 ; jcc 1f ; moveb %2,%0@-\n\t"
      "1: lsrl   #1,%1 ; jcc 1f ; movew %2,%0@-\n\t"
      "1: lsrl   #1,%1 ; jcc 1f ; movel %2,%0@-\n\t"
      "1: lsrl   #1,%1 ; jcc 1f ; movel %2,%0@- ; movel %2,%0@-\n\t"
      "1: subql  #1,%1 ; jcs 3f\n\t"
      "2: moveml %2/%3/%4/%5,%0@-\n\t"
         "dbra %1,2b\n\t"
      "3:"
         : "=a" (s), "=d" (count)
         :  "d" (0), "d" (0), "d" (0), "d" (0),
            "0" ((char *)s+count), "1" (count)
  );

   return(0);
}


static __inline__ void *mymemclear(void *s, size_t count)
{
   if (!count)
      return(0);

   if (count < 16) {
      __asm__ __volatile__(
            "lsrl   #1,%1 ; jcc 1f ; clrb %0@+\n\t"
         "1: lsrl   #1,%1 ; jcc 1f ; clrw %0@+\n\t"
         "1: lsrl   #1,%1 ; jcc 1f ; clrl %0@+\n\t"
         "1: lsrl   #1,%1 ; jcc 1f ; clrl %0@+ ; clrl %0@+\n\t"
         "1:"
            : "=a" (s), "=d" (count)
            : "0" (s), "1" (count)
     );
   } else {
      long tmp;
      __asm__ __volatile__(
            "movel %1,%2\n\t"
            "lsrl   #1,%2 ; jcc 1f ; clrb %0@+ ; subqw #1,%1\n\t"
            "lsrl   #1,%2 ; jcs 2f\n\t"  /* %0 increased=>bit 2 switched*/
            "clrw   %0@+  ; subqw  #2,%1 ; jra 2f\n\t"
         "1: lsrl   #1,%2 ; jcc 2f\n\t"
            "clrw   %0@+  ; subqw  #2,%1\n\t"
         "2: movew %1,%2; lsrl #2,%1 ; jeq 6f\n\t"
            "lsrl   #1,%1 ; jcc 3f ; clrl %0@+\n\t"
         "3: lsrl   #1,%1 ; jcc 4f ; clrl %0@+ ; clrl %0@+\n\t"
         "4: subql  #1,%1 ; jcs 6f\n\t"
         "5: clrl %0@+; clrl %0@+ ; clrl %0@+ ; clrl %0@+\n\t"
            "dbra %1,5b   ; clrw %1; subql #1,%1; jcc 5b\n\t"
         "6: movew %2,%1; btst #1,%1 ; jeq 7f ; clrw %0@+\n\t"
         "7:            ; btst #0,%1 ; jeq 8f ; clrb %0@+\n\t"
         "8:"
            : "=a" (s), "=d" (count), "=d" (tmp)
            : "0" (s), "1" (count)
     );
   }

   return(0);
}


static __inline__ void *mymemset(void *s, size_t count)
{
   if (!count)
      return(0);

   __asm__ __volatile__(
         "lsrl   #1,%1 ; jcc 1f ; moveb %2,%0@-\n\t"
      "1: lsrl   #1,%1 ; jcc 1f ; movew %2,%0@-\n\t"
      "1: lsrl   #1,%1 ; jcc 1f ; movel %2,%0@-\n\t"
      "1: lsrl   #1,%1 ; jcc 1f ; movel %2,%0@- ; movel %2,%0@-\n\t"
      "1: subql  #1,%1 ; jcs 3f\n\t"
      "2: moveml %2/%3/%4/%5,%0@-\n\t"
         "dbra %1,2b\n\t"
      "3:"
         : "=a" (s), "=d" (count)
         :  "d" (-1), "d" (-1), "d" (-1), "d" (-1),
            "0" ((char *) s + count), "1" (count)
  );

   return(0);
}


static __inline__ void *mymemmove(void *d, const void *s, size_t count)
{
   if (d < s) {
      if (count < 16) {
         __asm__ __volatile__(
               "lsrl   #1,%2 ; jcc 1f ; moveb %1@+,%0@+\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movew %1@+,%0@+\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@+,%0@+\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@+,%0@+ ; movel %1@+,%0@+\n\t"
            "1:"
               : "=a" (d), "=a" (s), "=d" (count)
               : "0" (d), "1" (s), "2" (count)
        );
      } else {
         long tmp;
         __asm__ __volatile__(
               "movel  %0,%3\n\t"
               "lsrl   #1,%3 ; jcc 1f ; moveb %1@+,%0@+ ; subqw #1,%2\n\t"
               "lsrl   #1,%3 ; jcs 2f\n\t"  /* %0 increased=>bit 2 switched*/
               "movew  %1@+,%0@+  ; subqw  #2,%2 ; jra 2f\n\t"
            "1: lsrl   #1,%3 ; jcc 2f\n\t"
               "movew  %1@+,%0@+  ; subqw  #2,%2\n\t"
            "2: movew  %2,%-; lsrl #2,%2 ; jeq 6f\n\t"
               "lsrl   #1,%2 ; jcc 3f ; movel %1@+,%0@+\n\t"
            "3: lsrl   #1,%2 ; jcc 4f ; movel %1@+,%0@+ ; movel %1@+,%0@+\n\t"
            "4: subql  #1,%2 ; jcs 6f\n\t"
            "5: movel  %1@+,%0@+;movel %1@+,%0@+\n\t"
               "movel  %1@+,%0@+;movel %1@+,%0@+\n\t"
               "dbra   %2,5b ; clrw %2; subql #1,%2; jcc 5b\n\t"
            "6: movew  %+,%2; btst #1,%2 ; jeq 7f ; movew %1@+,%0@+\n\t"
            "7:              ; btst #0,%2 ; jeq 8f ; moveb %1@+,%0@+\n\t"
            "8:"
               : "=a" (d), "=a" (s), "=d" (count), "=d" (tmp)
               : "0" (d), "1" (s), "2" (count)
        );
      }
   } else {
      if (count < 16) {
         __asm__ __volatile__(
               "lsrl   #1,%2 ; jcc 1f ; moveb %1@-,%0@-\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movew %1@-,%0@-\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@-,%0@-\n\t"
            "1: lsrl   #1,%2 ; jcc 1f ; movel %1@-,%0@- ; movel %1@-,%0@-\n\t"
            "1:"
               : "=a" (d), "=a" (s), "=d" (count)
               : "0" ((char *) d + count), "1" ((char *) s + count), "2" (count)
        );
      } else {
         long tmp;
         __asm__ __volatile__(
               "movel %0,%3\n\t"
               "lsrl   #1,%3 ; jcc 1f ; moveb %1@-,%0@- ; subqw #1,%2\n\t"
               "lsrl   #1,%3 ; jcs 2f\n\t"  /* %0 increased=>bit 2 switched*/
               "movew  %1@-,%0@-  ; subqw  #2,%2 ; jra 2f\n\t"
            "1: lsrl   #1,%3 ; jcc 2f\n\t"
               "movew  %1@-,%0@-  ; subqw  #2,%2\n\t"
            "2: movew %2,%-; lsrl #2,%2 ; jeq 6f\n\t"
               "lsrl   #1,%2 ; jcc 3f ; movel %1@-,%0@-\n\t"
            "3: lsrl   #1,%2 ; jcc 4f ; movel %1@-,%0@- ; movel %1@-,%0@-\n\t"
            "4: subql  #1,%2 ; jcs 6f\n\t"
            "5: movel %1@-,%0@-;movel %1@-,%0@-\n\t"
               "movel %1@-,%0@-;movel %1@-,%0@-\n\t"
               "dbra %2,5b ; clrw %2; subql #1,%2; jcc 5b\n\t"
            "6: movew %+,%2; btst #1,%2 ; jeq 7f ; movew %1@-,%0@-\n\t"
            "7:              ; btst #0,%2 ; jeq 8f ; moveb %1@-,%0@-\n\t"
            "8:"
               : "=a" (d), "=a" (s), "=d" (count), "=d" (tmp)
               : "0" ((char *) d + count), "1" ((char *) s + count), "2" (count)
        );
      }
   }

   return(0);
}


/* ++andreas: Simple and fast version of memmove, assumes size is
   divisible by 16, suitable for moving the whole screen bitplane */
static __inline__ void fast_memmove(char *dst, const char *src, size_t size)
{
  if (!size)
    return;
  if (dst < src)
    __asm__ __volatile__
      ("1:"
       "  moveml %0@+,%/d0/%/d1/%/a0/%/a1\n"
       "  moveml %/d0/%/d1/%/a0/%/a1,%1@\n"
       "  addql #8,%1; addql #8,%1\n"
       "  dbra %2,1b\n"
       "  clrw %2; subql #1,%2\n"
       "  jcc 1b"
       : "=a" (src), "=a" (dst), "=d" (size)
       : "0" (src), "1" (dst), "2" (size / 16 - 1)
       : "d0", "d1", "a0", "a1", "memory");
  else
    __asm__ __volatile__
      ("1:"
       "  subql #8,%0; subql #8,%0\n"
       "  moveml %0@,%/d0/%/d1/%/a0/%/a1\n"
       "  moveml %/d0/%/d1/%/a0/%/a1,%1@-\n"
       "  dbra %2,1b\n"
       "  clrw %2; subql #1,%2\n"
       "  jcc 1b"
       : "=a" (src), "=a" (dst), "=d" (size)
       : "0" (src + size), "1" (dst + size), "2" (size / 16 - 1)
       : "d0", "d1", "a0", "a1", "memory");
}

#elif defined(CONFIG_SUN4)

/* You may think that I'm crazy and that I should use generic
   routines.  No, I'm not: sun4's framebuffer crashes if we std
   into it, so we cannot use memset.  */

static __inline__ void *sun4_memset(void *s, char val, size_t count)
{
    int i;
    for(i=0; i<count;i++)
        ((char *) s) [i] = val;
    return s;
}

static __inline__ void *mymemset(void *s, size_t count)
{
    return sun4_memset(s, 255, count);
}

static __inline__ void *mymemclear(void *s, size_t count)
{
    return sun4_memset(s, 0, count);
}

static __inline__ void *mymemclear_small(void *s, size_t count)
{
    return sun4_memset(s, 0, count);
}

/* To be honest, this is slow_memmove :). But sun4 is crappy, so what we can do. */
static __inline__ void fast_memmove(void *d, const void *s, size_t count)
{
    int i;
    if (d<s) {
	for (i=0; i<count; i++)
	    ((char *) d)[i] = ((char *) s)[i];
    } else
	for (i=0; i<count; i++)
	    ((char *) d)[count-i-1] = ((char *) s)[count-i-1];
}

static __inline__ void *mymemmove(char *dst, const char *src, size_t size)
{
    fast_memmove(dst, src, size);
    return dst;
}

#else

static __inline__ void *mymemclear_small(void *s, size_t count)
{
    return(memset(s, 0, count));
}

static __inline__ void *mymemclear(void *s, size_t count)
{
    return(memset(s, 0, count));
}

static __inline__ void *mymemset(void *s, size_t count)
{
    return(memset(s, 255, count));
}

#if defined(__i386__)

static __inline__ void fast_memmove(void *d, const void *s, size_t count)
{
  int d0, d1, d2, d3;
    if (d < s) {
__asm__ __volatile__ (
	"cld\n\t"
	"shrl $1,%%ecx\n\t"
	"jnc 1f\n\t"
	"movsb\n"
	"1:\tshrl $1,%%ecx\n\t"
	"jnc 2f\n\t"
	"movsw\n"
	"2:\trep\n\t"
	"movsl"
	: "=&c" (d0), "=&D" (d1), "=&S" (d2)
	:"0"(count),"1"((long)d),"2"((long)s)
	:"memory");
    } else {
__asm__ __volatile__ (
	"std\n\t"
	"shrl $1,%%ecx\n\t"
	"jnc 1f\n\t"
	"movb 3(%%esi),%%al\n\t"
	"movb %%al,3(%%edi)\n\t"
	"decl %%esi\n\t"
	"decl %%edi\n"
	"1:\tshrl $1,%%ecx\n\t"
	"jnc 2f\n\t"
	"movw 2(%%esi),%%ax\n\t"
	"movw %%ax,2(%%edi)\n\t"
	"decl %%esi\n\t"
	"decl %%edi\n\t"
	"decl %%esi\n\t"
	"decl %%edi\n"
	"2:\trep\n\t"
	"movsl\n\t"
	"cld"
	: "=&c" (d0), "=&D" (d1), "=&S" (d2), "=&a" (d3)
	:"0"(count),"1"(count-4+(long)d),"2"(count-4+(long)s)
	:"memory");
    }
}

static __inline__ void *mymemmove(char *dst, const char *src, size_t size)
{
    fast_memmove(dst, src, size);
    return dst;
}

#else /* !i386 */

    /*
     *  Anyone who'd like to write asm functions for other CPUs?
     *   (Why are these functions better than those from include/asm/string.h?)
     */

static __inline__ void *mymemmove(void *d, const void *s, size_t count)
{
    return(memmove(d, s, count));
}

static __inline__ void fast_memmove(char *dst, const char *src, size_t size)
{
    memmove(dst, src, size);
}

#endif /* !i386 */

#endif

#endif /* _VIDEO_FBCON_H */
