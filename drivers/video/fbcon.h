/*
 *  linux/drivers/video/fbcon.h -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef __VIDEO_FBCON_H
#define __VIDEO_FBCON_H

#include <linux/console_struct.h>


    /*                                  
     *  `switch' for the Low Level Operations
     */
 
struct display_switch {                                                
    void (*setup)(struct display *p);
    void (*bmove)(struct display *p, int sy, int sx, int dy, int dx,
		  int height, int width);
    void (*clear)(struct vc_data *conp, struct display *p, int sy, int sx,
		  int height, int width);
    void (*putc)(struct vc_data *conp, struct display *p, int c, int yy,
    		 int xx);
    void (*putcs)(struct vc_data *conp, struct display *p, const char *s,
		  int count, int yy, int xx);     
    void (*revc)(struct display *p, int xx, int yy);
    void (*cursor)(struct display *p, int mode, int xx, int yy);
}; 


    /*
     *  Attribute Decoding
     */

/* Color */
#define attr_fgcol(p,conp)    \
	(((conp)->vc_attr >> ((p)->inverse ? 4 : 0)) & 0x0f)
#define attr_bgcol(p,conp)    \
	(((conp)->vc_attr >> ((p)->inverse ? 0 : 4)) & 0x0f)
#define	attr_bgcol_ec(p,conp) \
	(((conp)->vc_video_erase_char >> ((p)->inverse ? 8 : 12)) & 0x0f)

/* Monochrome */
#define attr_bold(p,conp) \
	((conp)->vc_attr & 2)
#define attr_reverse(p,conp) \
	(((conp)->vc_attr & 8) ^ ((p)->inverse ? 8 : 0))
#define attr_underline(p,conp) \
	((conp)->vc_attr & 4)
#define attr_blink(p,conp) \
	((conp)->vc_attr & 0x80)


/* ================================================================= */
/*                      Utility Assembler Functions                  */
/* ================================================================= */


#ifdef __mc68000__

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

#else /* !m68k */

    /*
     *  Anyone who'd like to write asm functions for other CPUs?
     */

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

#ifdef __i386__
static __inline__ void fast_memmove(void *d, const void *s, size_t count)
{
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
	: /* no output */
	:"c"(count),"D"((long)d),"S"((long)s)
	:"cx","di","si","memory");
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
	"movsl"
	: /* no output */
	:"c"(count),"D"(count-4+(long)d),"S"(count-4+(long)s)
	:"ax","cx","di","si","memory");
    }
}

static __inline__ void *mymemmove(char *dst, const char *src, size_t size)
{
    fast_memmove(dst, src, size);
    return dst;
}
#else
static __inline__ void *mymemmove(void *d, const void *s, size_t count)
{
    return(memmove(d, s, count));
}

static __inline__ void fast_memmove(char *dst, const char *src, size_t size)
{
    memmove(dst, src, size);
}
#endif	/* !i386 */

#endif /* !m68k */

#endif /* __VIDEO_FBCON_H */
