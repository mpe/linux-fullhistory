/*
 * linux/arch/m68k/console/fbcon.c -- Low level frame buffer based console
 *                                    driver
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 * This file is based on the original Amiga console driver (amicon.c):
 *
 *    Copyright (C) 1993 Hamish Macdonald
 *                       Greg Harp
 *    Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *          with work by William Rucklidge (wjr@cs.cornell.edu)
 *                       Geert Uytterhoeven
 *                       Jes Sorensen (jds@kom.auc.dk)
 *                       Martin Apel
 *
 * and on the original Atari console driver (atacon.c):
 *
 *    Copyright (C) 1993 Bjoern Brauel
 *                       Roman Hodek
 *
 *          with work by Guenther Kelleter
 *                       Martin Schaller
 *                       Andreas Schwab
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * To do:
 *  - Implement 16 plane mode.
 *  - Add support for 16/24/32 bit packed pixels
 *  - Hardware cursor
 */


#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/kd.h>
#include <linux/malloc.h>

#include <asm/setup.h>
#include <asm/irq.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_ATARI
#include <asm/atariints.h>
#endif
#ifdef CONFIG_FB_CYBER
#include "../amiga/s3blit.h"
#endif
#include <linux/fb.h>
#include <asm/font.h>
#include <asm/machdep.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/vt_kern.h>
#include <linux/console_struct.h>

/* Import console_blanked from console.c */

extern int console_blanked;


   /*
    *    The following symbols select what modes are supported. They should
    *    be settable by the user ("make config") later.
    */

/* Clear all definitions */

#undef CONFIG_FBCON_MONO
#undef CONFIG_FBCON_ILBM
#undef CONFIG_FBCON_PLANES
#undef CONFIG_FBCON_2PLANE
#undef CONFIG_FBCON_4PLANE
#undef CONFIG_FBCON_8PLANE
#undef CONFIG_FBCON_8PACKED
#undef CONFIG_FBCON_16PACKED
#undef CONFIG_FBCON_24PACKED
#undef CONFIG_FBCON_32PACKED
#undef CONFIG_FBCON_CYBER
#undef CONFIG_FBCON_RETINAZ3

/* Monochrome is default */

#define CONFIG_FBCON_MONO

/* Amiga support */

#ifdef CONFIG_AMIGA
#ifndef CONFIG_FBCON_ILBM
#define CONFIG_FBCON_ILBM
#endif
#ifndef CONFIG_FBCON_PLANES
#define CONFIG_FBCON_PLANES
#endif

/* Cybervision Graphics Board */

#ifdef CONFIG_FB_CYBER
#ifndef CONFIG_FBCON_CYBER
#define CONFIG_FBCON_CYBER
#endif
#endif

/* RetinaZ3 Graphics Board */

#ifdef CONFIG_FB_RETINAZ3
#ifndef CONFIG_FBCON_RETINAZ3
#define CONFIG_FBCON_RETINAZ3
#endif
#ifndef CONFIG_FBCON_8PACKED
#define CONFIG_FBCON_8PACKED
#endif
#endif

#endif /* CONFIG_AMIGA */

/* Atari support */

#ifdef CONFIG_ATARI
#ifndef CONFIG_FBCON_2PLANE
#define CONFIG_FBCON_2PLANE
#endif
#ifndef CONFIG_FBCON_4PLANE
#define CONFIG_FBCON_4PLANE
#endif
#ifndef CONFIG_FBCON_8PLANE
#define CONFIG_FBCON_8PLANE
#endif
#ifndef CONFIG_FBCON_8PACKED
#define CONFIG_FBCON_8PACKED
#endif
#ifndef CONFIG_FBCON_16PACKED
#define CONFIG_FBCON_16PACKED
#endif
#endif /* CONFIG_ATARI */


/* Extra definitions to make the code more readable */

#if defined(CONFIG_FBCON_2PLANE) || defined(CONFIG_FBCON_4PLANE) || \
    defined(CONFIG_FBCON_8PLANE)
#define CONFIG_FBCON_IPLAN2
#else
#undef CONFIG_FBCON_IPLAN2
#endif

#if defined(CONFIG_FBCON_CYBER) || defined(CONFIG_FBCON_RETINAZ3) || \
    defined(CONFIG_FBCON_8PACKED) || defined(CONFIG_FBCON_16PACKED) || \
    defined(CONFIG_FBCON_24PACKED) || defined(CONFIG_FBCON_32PACKED)
#define CONFIG_FBCON_PACKED
#else
#undef CONFIG_FBCON_PACKED
#endif


static struct fb_info *fb_info;
static struct display *disp;


/* ++Geert: Sorry, no hardware cursor support at the moment;
   use Atari alike software cursor */

static int cursor_drawn = 0;

#define CURSOR_DRAW_DELAY           (2)

/* # VBL ints between cursor state changes */
#define AMIGA_CURSOR_BLINK_RATE   (20)
#define ATARI_CURSOR_BLINK_RATE   (42)

static int vbl_cursor_cnt = 0;
static int cursor_on = 0;
static int cursor_blink_rate;

static __inline__ int CURSOR_UNDRAWN(void)
{
   int cursor_was_drawn;
   vbl_cursor_cnt = 0;
   cursor_was_drawn = cursor_drawn;
   cursor_drawn = 0;
   return(cursor_was_drawn);
}

   /*
    *    Attribute Decoding
    */

/* Color */
#define attr_fgcol(p,conp)    \
	(((conp)->vc_attr >> ((p)->inverse ? 4 : 0)) & 0x0f)
#define attr_bgcol(p,conp)    \
	(((conp)->vc_attr >> ((p)->inverse ? 0 : 4)) & 0x0f)
#define	attr_bgcol_ec(p,conp) \
	(((conp)->vc_video_erase_char >> ((p)->inverse ? 8 : 12)) & 0x0f)

/* Monochrome */
#define attr_bold(p,conp)     \
	(((conp)->vc_attr & 3) == 2)
#define attr_reverse(p,conp)  \
	(((conp)->vc_attr & 8) ^ ((p)->inverse ? 8 : 0))
#define attr_underline(p,conp) \
	(((conp)->vc_attr) & 4)


   /*
    *    Scroll Method
    */

#define SCROLL_YWRAP          (0)
#define SCROLL_YPAN           (1)
#define SCROLL_YMOVE          (2)

#define divides(a, b)         ((!(a) || (b)%(a)) ? 0 : 1)


   /*
    *    Interface used by the world
    */

static u_long fbcon_startup(u_long kmem_start, const char **display_desc);
static void fbcon_init(struct vc_data *conp);
static int fbcon_deinit(struct vc_data *conp);
static int fbcon_changevar(int con);
static int fbcon_clear(struct vc_data *conp, int sy, int sx, int height,
                       int width);
static int fbcon_putc(struct vc_data *conp, int c, int yy, int xx);
static int fbcon_putcs(struct vc_data *conp, const char *s, int count, int yy,
                       int xx);
static int fbcon_cursor(struct vc_data *conp, int mode);
static int fbcon_scroll(struct vc_data *conp, int t, int b, int dir, int count);
static int fbcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
                       int height, int width);
static int fbcon_switch(struct vc_data *conp);
static int fbcon_blank(int blank);
static int fbcon_get_font(struct vc_data *conp, int *w, int *h, char *data);
static int fbcon_set_font(struct vc_data *conp, int w, int h, char *data);
static int fbcon_set_palette(struct vc_data *conp, unsigned char *table);


   /*
    *    Internal routines
    */

static void fbcon_setup(int con, int setcol, int init);
static __inline__ void *mymemclear_small(void *s, size_t count);
static __inline__ void *mymemclear(void *s, size_t count);
static __inline__ void *mymemset(void *s, size_t count);
static __inline__ void *mymemmove(void *d, void *s, size_t count);
static __inline__ void fast_memmove(char *dst, char *src, size_t size);
static __inline__ void memclear_4p_col(void *d, size_t h, u_long val, int bpr);
static __inline__ void memset_even_4p(void *d, size_t count, u_long val1,
                                      u_long val2);
static __inline__ void memmove_4p_col(void *d, void *s, int h, int bpr);
static __inline__ u_long expand4l(u_char c);
static __inline__ void expand4dl(u_char c, u_long *ret1, u_long *ret2);
static __inline__ u_long dup4l(u_char c);
static __inline__ void memclear_8p_col(void *d, size_t h, u_long val1,
                                       u_long val2, int bpr);
static __inline__ void memset_even_8p(void *d, size_t count, u_long val1,
                                      u_long val2, u_long val3, u_long val4);
static __inline__ void memmove_8p_col(void *d, void *s, int h, int bpr);
static __inline__ void expand8dl(u_char c, u_long *ret1, u_long *ret2);
static __inline__ void memclear_2p_col(void *d, size_t h, u_short val,
				       int bpr);
static __inline__ void memset_even_2p(void *d, size_t count, u_long val);
static __inline__ void memmove_2p_col(void *d, void *s, int h, int bpr);
static __inline__ u_short expand2w(u_char c);
static __inline__ u_long expand2l(u_char c);
static __inline__ u_short dup2w(u_char c);
static __inline__ int real_y(struct display *p, int yy);
static void fbcon_vbl_handler(int irq, void *dummy, struct pt_regs *fp);
static __inline__ void updatescrollmode(struct display *p);
static __inline__ void ywrap_up(int unit, struct display *p, int count);
static __inline__ void ywrap_down(int unit, struct display *p, int count);
static __inline__ void ypan_up(int unit, struct vc_data *conp,
			       struct display *p, int count);
static __inline__ void ypan_down(int unit, struct vc_data *conp,
				 struct display *p, int count);
static void fbcon_bmove_rec(struct display *p, int sy, int sx, int dy, int dx,
                            int height, int width, u_int y_break);


   /*
    *    Monochrome
    */

#ifdef CONFIG_FBCON_MONO
static void bmove_mono(struct display *p, int sy, int sx, int dy, int dx,
                       int height, int width);
static void clear_mono(struct vc_data *conp, struct display *p, int sy, int sx,
                       int height, int width);
static void putc_mono(struct vc_data *conp, struct display *p, int c, int yy,
                      int xx);
static void putcs_mono(struct vc_data *conp, struct display *p, const char *s,
                       int count, int yy, int xx);
static void rev_char_mono(struct display *p, int xx, int yy);
#endif /* CONFIG_FBCON_MONO */


   /*
    *    Color Interleaved Planes
    */

#ifdef CONFIG_FBCON_ILBM
static void bmove_ilbm(struct display *p, int sy, int sx, int dy, int dx,
                       int height, int width);
static void clear_ilbm(struct vc_data *conp, struct display *p, int sy, int sx,
                       int height, int width);
static void putc_ilbm(struct vc_data *conp, struct display *p, int c, int yy,
                      int xx);
static void putcs_ilbm(struct vc_data *conp, struct display *p, const char *s,
                       int count, int yy, int xx);
static void rev_char_ilbm(struct display *p, int xx, int yy);
#endif /* CONFIG_FBCON_ILBM */


   /*
    *    Color Planes
    */

#ifdef CONFIG_FBCON_PLANES
static void bmove_plan(struct display *p, int sy, int sx, int dy, int dx,
                       int height, int width);
static void clear_plan(struct vc_data *conp, struct display *p, int sy, int sx,
                       int height, int width);
static void putc_plan(struct vc_data *conp, struct display *p, int c, int yy,
                      int xx);
static void putcs_plan(struct vc_data *conp, struct display *p, const char *s,
                       int count, int yy, int xx);
static void rev_char_plan(struct display *p, int xx, int yy);
#endif /* CONFIG_FBCON_PLANES */


   /*
    *    2 Planes (2-bytes interleave)
    */

#ifdef CONFIG_FBCON_2PLANE
static void bmove_2_plane(struct display *p, int sy, int sx, int dy, int dx,
                          int height, int width);
static void clear_2_plane(struct vc_data *conp, struct display *p, int sy,
                          int sx, int height, int width);
static void putc_2_plane(struct vc_data *conp, struct display *p, int c, int yy,
                         int xx);
static void putcs_2_plane(struct vc_data *conp, struct display *p,
                          const char *s, int count, int yy, int xx);
static void rev_char_2_plane(struct display *display, int xx, int yy);
#endif /* CONFIG_FBCON_2PLANE */


   /*
    *    4 Planes (2-bytes interleave)
    */

#ifdef CONFIG_FBCON_4PLANE
static void bmove_4_plane(struct display *p, int sy, int sx, int dy, int dx,
                          int height, int width);
static void clear_4_plane(struct vc_data *conp, struct display *p, int sy,
                          int sx, int height, int width);
static void putc_4_plane(struct vc_data *conp, struct display *p, int c, int yy,
                         int xx);
static void putcs_4_plane(struct vc_data *conp, struct display *p,
                          const char *s, int count, int yy, int xx);
static void rev_char_4_plane(struct display *p, int xx, int yy);
#endif /* CONFIG_FBCON_4PLANE */


   /*
    *    8 Planes (2-bytes interleave)
    */

#ifdef CONFIG_FBCON_8PLANE
static void bmove_8_plane(struct display *p, int sy, int sx, int dy, int dx,
                          int height, int width);
static void clear_8_plane(struct vc_data *conp, struct display *p, int sy,
                          int sx, int height, int width);
static void putc_8_plane(struct vc_data *conp, struct display *p, int c, int yy,
                         int xx);
static void putcs_8_plane(struct vc_data *conp, struct display *p,
                          const char *s, int count, int yy, int xx);
static void rev_char_8_plane(struct display *display, int xx, int yy);
#endif /* CONFIG_FBCON_8PLANE */


   /*
    *    8 bpp Packed Pixels
    */

#ifdef CONFIG_FBCON_8PACKED
static void bmove_8_packed(struct display *p, int sy, int sx, int dy, int dx,
                           int height, int width);
static void clear_8_packed(struct vc_data *conp, struct display *p, int sy,
                           int sx, int height, int width);
static void putc_8_packed(struct vc_data *conp, struct display *p, int c, int yy,
                          int xx);
static void putcs_8_packed(struct vc_data *conp, struct display *p,
                           const char *s, int count, int yy, int xx);
static void rev_char_8_packed(struct display *p, int xx, int yy);
#endif /* CONFIG_FBCON_8PACKED */


   /*
    *    16 bpp Packed Pixels
    */

#ifdef CONFIG_FBCON_16PACKED
static void bmove_16_packed(struct display *p, int sy, int sx, int dy, int dx,
                            int height, int width);
static void clear_16_packed(struct vc_data *conp, struct display *p, int sy,
                            int sx, int height, int width);
static void putc_16_packed(struct vc_data *conp, struct display *p, int c,
                           int yy, int xx);
static void putcs_16_packed(struct vc_data *conp, struct display *p,
                            const char *s, int count, int yy, int xx);
static void rev_char_16_packed(struct display *p, int xx, int yy);
#endif */ CONFIG_FBCON_8PACKED */


   /*
    *    Cybervision (accelerated)
    */

#ifdef CONFIG_FBCON_CYBER
static void bmove_cyber(struct display *p, int sy, int sx, int dy, int dx,
                        int height, int width);
static void clear_cyber(struct vc_data *conp, struct display *p, int sy, int sx,
                        int height, int width);
static void putc_cyber(struct vc_data *conp, struct display *p, int c, int yy,
                       int xx);
static void putcs_cyber(struct vc_data *conp, struct display *p, const char *s,
                        int count, int yy, int xx);
static void rev_char_cyber(struct display *p, int xx, int yy);

extern void Cyber_WaitQueue(unsigned short fifo);
extern void Cyber_WaitBlit(void);
extern void Cyber_BitBLT(unsigned short curx, unsigned short cury,
			 unsigned short destx, unsigned short desty,
			 unsigned short width, unsigned short height,
                         unsigned short mode);
extern void Cyber_RectFill(unsigned short xx, unsigned short yy,
			   unsigned short width, unsigned short
			   height, unsigned short mode,
			   unsigned short fcolor); 
extern void Cyber_MoveCursor(unsigned short xx, unsigned short yy);
#endif /* CONFIG_FBCON_CYBER */

#ifdef CONFIG_FBCON_RETINAZ3
static void clear_retz3(struct vc_data *conp, struct display *p, int
			sy, int sx, int height, int width);
static void bmove_retz3(struct display *p, int sy, int sx, int dy, int dx,
                        int height, int width);
extern void retz3_bitblt(struct fb_var_screeninfo *scr,
			 unsigned short srcx, unsigned short srcy, unsigned
			 short destx, unsigned short desty, unsigned short
			 width, unsigned short height, unsigned short cmd,
			 unsigned short mask);
static void putc_retz3(struct vc_data *conp, struct display *p, int c,
		       int yy, int xx);
static void putcs_retz3(struct vc_data *conp, struct display *p, const
			char *s, int count, int yy, int xx);
static void rev_char_retz3(struct display *p, int xx, int yy);
#endif

   /*
    *    `switch' for the Low Level Operations
    */

struct display_switch {
    void (*bmove)(struct display *p, int sy, int sx, int dy, int dx, int height,
                  int width);
    void (*clear)(struct vc_data *conp, struct display *p, int sy, int sx,
                  int height, int width);
    void (*putc)(struct vc_data *conp, struct display *p, int c, int yy, int xx);
    void (*putcs)(struct vc_data *conp, struct display *p, const char *s,
                  int count, int yy, int xx);
    void (*rev_char)(struct display *p, int xx, int yy);
};


#ifdef CONFIG_FBCON_MONO
static struct display_switch dispsw_mono = {
   bmove_mono, clear_mono, putc_mono, putcs_mono, rev_char_mono
};
#endif /* CONFIG_FBCON_MONO */

#ifdef CONFIG_FBCON_ILBM
static struct display_switch dispsw_ilbm = {
   bmove_ilbm, clear_ilbm, putc_ilbm, putcs_ilbm, rev_char_ilbm
};
#endif /* CONFIG_FBCON_ILBM */

#ifdef CONFIG_FBCON_PLANES
static struct display_switch dispsw_plan = {
   bmove_plan, clear_plan, putc_plan, putcs_plan, rev_char_plan
};
#endif /* CONFIG_FBCON_PLANES */

#ifdef CONFIG_FBCON_2PLANE
static struct display_switch dispsw_2_plane = {
   bmove_2_plane, clear_2_plane, putc_2_plane, putcs_2_plane, rev_char_2_plane
};
#endif /* CONFIG_FBCON_2PLANE */

#ifdef CONFIG_FBCON_4PLANE
static struct display_switch dispsw_4_plane = {
   bmove_4_plane, clear_4_plane, putc_4_plane, putcs_4_plane, rev_char_4_plane
};
#endif /* CONFIG_FBCON_4PLANE */

#ifdef CONFIG_FBCON_8PLANE
static struct display_switch dispsw_8_plane = {
   bmove_8_plane, clear_8_plane, putc_8_plane, putcs_8_plane, rev_char_8_plane
};
#endif /* CONFIG_FBCON_8PLANE */

#ifdef CONFIG_FBCON_8PACKED
static struct display_switch dispsw_8_packed = {
   bmove_8_packed, clear_8_packed, putc_8_packed, putcs_8_packed, rev_char_8_packed
};
#endif /* CONFIG_FBCON_8PACKED */

#ifdef CONFIG_FBCON_16PACKED
static struct display_switch dispsw_16_packed = {
   bmove_16_packed, clear_16_packed, putc_16_packed, putcs_16_packed,
   rev_char_16_packed
};
#endif /* CONFIG_FBCON_16PACKED */

#ifdef CONFIG_FBCON_CYBER
static struct display_switch dispsw_cyber = {
   bmove_cyber, clear_cyber, putc_cyber, putcs_cyber, rev_char_cyber
};
#endif /* CONFIG_FBCON_CYBER */

#ifdef CONFIG_FBCON_RETINAZ3
static struct display_switch dispsw_retz3 = {
   bmove_retz3, clear_retz3, putc_retz3,
   putcs_retz3, rev_char_retz3
};
#endif


static u_long fbcon_startup(u_long kmem_start, const char **display_desc)
{
   int irqres = 0;

   fb_info = mach_fb_init(&kmem_start);
   disp = fb_info->disp;
   *display_desc = fb_info->modename;
   fb_info->changevar = &fbcon_changevar;

#ifdef CONFIG_AMIGA
   if (MACH_IS_AMIGA) {
      cursor_blink_rate = AMIGA_CURSOR_BLINK_RATE;
      irqres = request_irq(IRQ_AMIGA_VERTB, fbcon_vbl_handler, 0,
                           "console/cursor", fbcon_vbl_handler);
   }
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_ATARI
   if (MACH_IS_ATARI) {
      cursor_blink_rate = ATARI_CURSOR_BLINK_RATE;
      irqres = request_irq(IRQ_AUTO_4, fbcon_vbl_handler, IRQ_TYPE_PRIO,
                           "console/cursor", fbcon_vbl_handler);
   }
#endif /* CONFIG_ATARI */

   if (irqres)
      panic("fbcon_startup: Couldn't add vblank interrupt");

   return(kmem_start);
}


static void fbcon_init(struct vc_data *conp)
{
   int unit = conp->vc_num;

   if (unit)
      disp[unit] = disp[0];
   disp[unit].conp = conp;
   fbcon_setup(unit, 1, 1);
}


static int fbcon_deinit(struct vc_data *conp)
{
   disp[conp->vc_num].conp = 0;
   return(0);
}


static int fbcon_changevar(int con)
{
   fbcon_setup(con, 1, 0);
   return(0);
}


static __inline__ void updatescrollmode(struct display *p)
{
   if (divides(p->ywrapstep, p->fontheight) &&
       divides(p->fontheight, p->var.yres_virtual))
      p->scrollmode = SCROLL_YWRAP;
   else if (divides(p->ypanstep, p->fontheight) &&
            p->var.yres_virtual >= p->var.yres+p->fontheight)
      p->scrollmode = SCROLL_YPAN;
   else
      p->scrollmode = SCROLL_YMOVE;
}


static void fbcon_setup(int con, int setcol, int init)
{
   struct display *p = &disp[con];
   struct vc_data *conp = p->conp;
   int nr_rows, nr_cols;

   p->var.xoffset = p->var.yoffset = p->yscroll = 0;  /* reset wrap/pan */

   if (!fb_info->fontname[0] ||
       !findsoftfont(fb_info->fontname, &p->fontwidth, &p->fontheight,
                     &p->fontdata) || p->fontwidth != 8)
	   getdefaultfont(p->var.xres, p->var.yres, NULL, &p->fontwidth,
	                  &p->fontheight, &p->fontdata);
   if (p->fontwidth != 8) {
      /* ++Geert: changed from panic() to `correct and continue' */
      printk("fbcon_setup: No support for fontwidth != 8");
      p->fontwidth = 8;
   }
   updatescrollmode(p);

   nr_cols = p->var.xres/p->fontwidth;
   nr_rows = p->var.yres/p->fontheight;
   /* ++guenther: console.c:vc_allocate() relies on initializing vc_{cols,rows},
    * but we must not set those if we are only resizing the console.
    */
   if (init) {
      conp->vc_cols = nr_cols;
      conp->vc_rows = nr_rows;
   }
   p->vrows = p->var.yres_virtual/p->fontheight;
   conp->vc_can_do_color = p->var.bits_per_pixel != 1;

#ifdef CONFIG_FBCON_MONO
   if (p->var.bits_per_pixel == 1) {
      if (p->line_length)
         p->next_line = p->line_length;
      else
         p->next_line = p->var.xres_virtual>>3;
      p->next_plane = 0;
      p->dispsw = &dispsw_mono;
   } else
#endif /* CONFIG_FBCON_MONO */
#ifdef CONFIG_FBCON_IPLAN2
   if (p->type == FB_TYPE_INTERLEAVED_PLANES && p->type_aux == 2) {
      p->next_line = p->var.xres_virtual*p->var.bits_per_pixel>>3;
      p->next_plane = 0;
#ifdef CONFIG_FBCON_2PLANE
      if (p->var.bits_per_pixel == 2)
         p->dispsw = &dispsw_2_plane;
      else
#endif /* CONFIG_FBCON_2PLANE */
#ifdef CONFIG_FBCON_4PLANE
      if (p->var.bits_per_pixel == 4)
         p->dispsw = &dispsw_4_plane;
      else
#endif /* CONFIG_FBCON_4PLANE */
#ifdef CONFIG_FBCON_8PLANE
      if (p->var.bits_per_pixel == 8)
         p->dispsw = &dispsw_8_plane;
      else
#endif /* CONFIG_FBCON_8PLANE */
      goto fail;
   } else
#endif /* CONFIG_FBCON_IPLAN2 */
#ifdef CONFIG_FBCON_ILBM
   if (p->type == FB_TYPE_INTERLEAVED_PLANES && p->type_aux != 2) {
      if (p->line_length) {
         p->next_line = p->line_length*p->var.bits_per_pixel;
         p->next_plane = p->line_length
;
      } else {
         p->next_line = p->type_aux;
         p->next_plane = p->type_aux/p->var.bits_per_pixel;
      }
      p->dispsw = &dispsw_ilbm;
   } else
#endif /* CONFIG_FBCON_ILBM */
#ifdef CONFIG_FBCON_PLANES
   if (p->type == FB_TYPE_PLANES) {
      if (p->line_length)
         p->next_line = p->line_length;
      else
         p->next_line = p->var.xres_virtual>>3;
      p->next_plane = p->var.yres_virtual*p->next_line;
      p->dispsw = &dispsw_plan;
   } else
#endif /* CONFIG_FBCON_PLANES */
#ifdef CONFIG_FBCON_PACKED
   if (p->type == FB_TYPE_PACKED_PIXELS) {
      p->next_line = (p->var.xres_virtual*p->var.bits_per_pixel)>>3;
      p->next_plane = 0;
#ifdef CONFIG_FBCON_CYBER
      if (p->var.accel == FB_ACCEL_CYBERVISION)
         p->dispsw = &dispsw_cyber;
      else
#endif
#ifdef CONFIG_FBCON_RETINAZ3
      if (p->var.accel == FB_ACCEL_RETINAZ3)
         p->dispsw = &dispsw_retz3;
      else
#endif
#ifdef CONFIG_FBCON_8PACKED
      if (p->var.bits_per_pixel == 8)
         p->dispsw = &dispsw_8_packed;
      else
#endif /* CONFIG_FBCON_8PACKED */
#ifdef CONFIG_FBCON_16PACKED
      if (p->var.bits_per_pixel == 16)
         p->dispsw = &dispsw_16_packed;
      else
#endif /* CONFIG_FBCON_16PACKED */
#ifdef CONFIG_FBCON_24PACKED
      if (p->var.bits_per_pixel == 24)
         p->dispsw = &dispsw_24_packed;
      else
#endif /* CONFIG_FBCON_24PACKED */
#ifdef CONFIG_FBCON_32PACKED
      if (p->var.bits_per_pixel == 32)
         p->dispsw = &dispsw_32_packed;
      else
#endif /* CONFIG_FBCON_32PACKED */
      goto fail;
   } else
#endif /* CONFIG_FBCON_PACKED */
   {
fail:
#ifdef CONFIG_FBCON_MONO
      printk("fbcon_setup: type %d (aux %d) not supported, trying mono\n",
             p->type, p->type_aux);
      if (p->line_length)
         p->next_line = p->line_length;
      else
         p->next_line = p->var.xres_virtual>>3;
      p->next_plane = 0;
      p->var.bits_per_pixel = 1;
      p->dispsw = &dispsw_mono;
#else /* CONFIG_FBCON_MONO */
      panic("fbcon_setup: no default driver");
#endif /* CONFIG_FBCON_MONO */
   }

   if (setcol) {
      p->fgcol = p->var.bits_per_pixel > 2 ? 7 : (1<<p->var.bits_per_pixel)-1;
      p->bgcol = 0;
   }

   if (!init)
      vc_resize_con(nr_rows, nr_cols, con);
}


/* ================================================================= */
/*                      Utility Assembler Functions                  */
/* ================================================================= */


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


static __inline__ void *mymemmove(void *d, void *s, size_t count)
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
static __inline__ void fast_memmove(char *dst, char *src, size_t size)
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


/* Sets the bytes in the visible column at d, height h, to the value
 * val for a 4 plane screen. The the bis of the color in 'color' are
 * moved (8 times) to the respective bytes. This means:
 *
 * for(h times; d += bpr)
 *   *d     = (color & 1) ? 0xff : 0;
 *   *(d+2) = (color & 2) ? 0xff : 0;
 *   *(d+4) = (color & 4) ? 0xff : 0;
 *   *(d+6) = (color & 8) ? 0xff : 0;
 */

static __inline__ void memclear_4p_col(void *d, size_t h, u_long val, int bpr)
{
	__asm__ __volatile__
		("1: movepl %4,%0@(0)\n\t"
		  "addal  %5,%0\n\t"
		  "dbra	  %1,1b"
		  : "=a" (d), "=d" (h)
		  : "0" (d), "1" (h - 1), "d" (val), "r" (bpr)
		);
}

/* Sets a 4 plane region from 'd', length 'count' bytes, to the color
 * in val1/val2. 'd' has to be an even address and count must be divisible
 * by 8, because only whole words and all planes are accessed. I.e.:
 *
 * for(count/8 times)
 *   *d     = *(d+1) = (color & 1) ? 0xff : 0;
 *   *(d+2) = *(d+3) = (color & 2) ? 0xff : 0;
 *   *(d+4) = *(d+5) = (color & 4) ? 0xff : 0;
 *   *(d+6) = *(d+7) = (color & 8) ? 0xff : 0;
 */

static __inline__ void memset_even_4p(void *d, size_t count, u_long val1,
                                      u_long val2)
{
  u_long *dd = d;

  count /= 8;
  while (count--)
    {
      *dd++ = val1;
      *dd++ = val2;
    }
}

/* Copies a 4 plane column from 's', height 'h', to 'd'. */

static __inline__ void memmove_4p_col (void *d, void *s, int h, int bpr)
{
  u_char *dd = d, *ss = s;

  while (h--)
    {
      dd[0] = ss[0];
      dd[2] = ss[2];
      dd[4] = ss[4];
      dd[6] = ss[6];
      dd += bpr;
      ss += bpr;
    }
}


/* This expands a 4 bit color into a long for movepl (4 plane) operations. */

static __inline__ u_long expand4l(u_char c)
{
	u_long	rv;

	__asm__ __volatile__
		("lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  "lsll	 #8,%0\n\t"
		  "lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  "lsll	 #8,%0\n\t"
		  "lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  "lsll	 #8,%0\n\t"
		  "lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  : "=&d" (rv), "=d" (c)
		  : "1" (c)
		);
	return(rv);
}

/* This expands a 4 bit color into two longs for two movel operations
 * (4 planes).
 */

static __inline__ void expand4dl(u_char c, u_long *ret1, u_long *ret2)
{
	u_long	rv1, rv2;

	__asm__ __volatile__
		("lsrb	 #1,%3\n\t"
		  "scs	 %0\n\t"
		  "extw	 %0\n\t"
		  "swap	 %0\n\t"
		  "lsrb	 #1,%3\n\t"
		  "scs	 %0\n\t"
		  "extw	 %0\n\t"
		  "lsrb	 #1,%3\n\t"
		  "scs	 %1\n\t"
		  "extw	 %1\n\t"
		  "swap	 %1\n\t"
		  "lsrb	 #1,%3\n\t"
		  "scs   %1\n\t"
		  "extw	 %1"
		  : "=&d" (rv1), "=&d" (rv2), "=d" (c)
		  : "2" (c)
		);
	*ret1 = rv1;
	*ret2 = rv2;
}


/* This duplicates a byte 4 times into a long. */

static __inline__ u_long dup4l(u_char c)
{
	ushort	tmp;
	ulong	rv;

	__asm__ __volatile__
		("moveb  %2,%0\n\t"
		  "lslw   #8,%0\n\t"
		  "moveb  %2,%0\n\t"
		  "movew  %0,%1\n\t"
		  "swap   %0\n\t"
		  "movew  %1,%0"
		  : "=&d" (rv), "=d" (tmp)
		  : "d" (c)
		);

	return(rv);
}


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
	__asm__ __volatile__
		("1: movepl %4,%0@(0)\n\t"
	      "movepl %5,%0@(8)\n\t"
		  "addal  %6,%0\n\t"
		  "dbra	  %1,1b"
		  : "=a" (d), "=d" (h)
		  : "0" (d), "1" (h - 1), "d" (val1), "d" (val2), "r" (bpr)
		);
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
  while (count--)
    {
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

  while (h--)
    {
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

	__asm__ __volatile__
		("lsrb	 #1,%3\n\t"
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
		  : "2" (c)
		);

	*ret1 = rv1;
	*ret2 = rv2;
}

/* This expands a 8 bit color into four longs for four movel operations
 * (8 planes).
 */

/* ++andreas: use macro to avoid taking address of return values */
#define expand8ql(c, rv1, rv2, rv3, rv4) \
do {	u_char tmp = c;						\
	__asm__ __volatile__					\
		("lsrb	 #1,%5\n\t"				\
		  "scs	 %0\n\t"				\
		  "extw	 %0\n\t"				\
		  "swap	 %0\n\t"				\
		  "lsrb	 #1,%5\n\t"				\
		  "scs	 %0\n\t"				\
		  "extw	 %0\n\t"				\
		  "lsrb	 #1,%5\n\t"				\
		  "scs	 %1\n\t"				\
		  "extw	 %1\n\t"				\
		  "swap	 %1\n\t"				\
		  "lsrb	 #1,%5\n\t"				\
		  "scs   %1\n\t"				\
		  "extw	 %1\n\t"				\
		  "lsrb	 #1,%5\n\t"				\
		  "scs	 %2\n\t"				\
		  "extw	 %2\n\t"				\
		  "swap	 %2\n\t"				\
		  "lsrb	 #1,%5\n\t"				\
		  "scs	 %2\n\t"				\
		  "extw	 %2\n\t"				\
		  "lsrb	 #1,%5\n\t"				\
		  "scs	 %3\n\t"				\
		  "extw	 %3\n\t"				\
		  "swap	 %3\n\t"				\
		  "lsrb	 #1,%5\n\t"				\
		  "scs   %3\n\t"				\
		  "extw	 %3"					\
		  : "=&d" (rv1), "=&d" (rv2), "=&d" (rv3),	\
		    "=&d" (rv4), "=d" (tmp)			\
		  : "4" (tmp)					\
		);						\
} while (0)


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
	__asm__ __volatile__
		("1: movepw %4,%0@(0)\n\t"
		  "addal  %5,%0\n\t"
		  "dbra	  %1,1b"
		  : "=a" (d), "=d" (h)
		  : "0" (d), "1" (h - 1), "d" (val), "r" (bpr)
		);
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

  while (h--)
    {
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

	__asm__ __volatile__
		("lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  "lsll	 #8,%0\n\t"
		  "lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  : "=&d" (rv), "=d" (c)
		  : "1" (c)
		);
	return(rv);
}

/* This expands a 2 bit color into one long for a movel operation
 * (2 planes).
 */

static __inline__ u_long expand2l(u_char c)
{
	u_long	rv;

	__asm__ __volatile__
		("lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  "extw	 %0\n\t"
		  "swap	 %0\n\t"
		  "lsrb	 #1,%2\n\t"
		  "scs	 %0\n\t"
		  "extw	 %0\n\t"
		  : "=&d" (rv), "=d" (c)
		  : "1" (c)
		);

	return rv;
}


/* This duplicates a byte 2 times into a short. */

static __inline__ u_short dup2w(u_char c)
{
    ushort  rv;

    __asm__ __volatile__
        ( "moveb  %1,%0\n\t"
          "lslw   #8,%0\n\t"
          "moveb  %1,%0\n\t"
          : "=&d" (rv)
          : "d" (c)
        );

    return( rv );
}


/* ====================================================================== */

/* fbcon_XXX routines - interface used by the world
 *
 * This system is now divided into two levels because of complications
 * caused by hardware scrolling. Top level functions:
 *
 *    fbcon_bmove(), fbcon_clear(), fbcon_putc()
 *
 * handles y values in range [0, scr_height-1] that correspond to real
 * screen positions. y_wrap shift means that first line of bitmap may be
 * anywhere on this display. These functions convert lineoffsets to
 * bitmap offsets and deal with the wrap-around case by splitting blits.
 *
 *    fbcon_bmove_physical_8()   -- These functions fast implementations
 *    fbcon_clear_physical_8()   -- of original fbcon_XXX fns.
 *    fbcon_putc_physical_8()    -- (fontwidth != 8) may be added later
 *
 * WARNING:
 *
 * At the moment fbcon_putc() cannot blit across vertical wrap boundary
 * Implies should only really hardware scroll in rows. Only reason for
 * restriction is simplicity & efficiency at the moment.
 */

static __inline__ int real_y(struct display *p, int yy)
{
   int rows = p->vrows;

   yy += p->yscroll;
   return(yy < rows ? yy : yy-rows);
}


static int fbcon_clear(struct vc_data *conp, int sy, int sx, int height,
                       int width)
{
   int unit = conp->vc_num;
   struct display *p = &disp[unit];
   u_int y_break;

   if (!p->can_soft_blank && console_blanked)
      return(0);

   if ((sy <= p->cursor_y) && (p->cursor_y < sy+height) &&
       (sx <= p->cursor_x) && (p->cursor_x < sx+width))
      CURSOR_UNDRAWN();

   /* Split blits that cross physical y_wrap boundary */

   y_break = p->vrows-p->yscroll;
   if (sy < y_break && sy+height-1 >= y_break) {
      u_int b = y_break-sy;
      p->dispsw->clear(conp, p, real_y(p, sy), sx, b, width);
      p->dispsw->clear(conp, p, real_y(p, sy+b), sx, height-b, width);
   } else
      p->dispsw->clear(conp, p, real_y(p, sy), sx, height, width);

   return(0);
}


static int fbcon_putc(struct vc_data *conp, int c, int yy, int xx)
{
   int unit = conp->vc_num;
   struct display *p = &disp[unit];

   if (!p->can_soft_blank && console_blanked)
      return(0);

   if ((p->cursor_x == xx) && (p->cursor_y == yy))
       CURSOR_UNDRAWN();

   p->dispsw->putc(conp, p, c, real_y(p, yy), xx);

   return(0);
}


static int fbcon_putcs(struct vc_data *conp, const char *s, int count, int yy,
                       int xx)
{
   int unit = conp->vc_num;
   struct display *p = &disp[unit];

   if (!p->can_soft_blank && console_blanked)
      return(0);

   if ((p->cursor_y == yy) && (xx <= p->cursor_x) && (p->cursor_x < xx+count))
      CURSOR_UNDRAWN();

   p->dispsw->putcs(conp, p, s, count, real_y(p, yy), xx);

   return(0);
}


static int fbcon_cursor(struct vc_data *conp, int mode)
{
   int unit = conp->vc_num;
   struct display *p = &disp[unit];

   /* Avoid flickering if there's no real change. */
   if (p->cursor_x == conp->vc_x && p->cursor_y == conp->vc_y &&
       (mode == CM_ERASE) == !cursor_on)
      return 0;
   if (CURSOR_UNDRAWN ())
      p->dispsw->rev_char(p, p->cursor_x, real_y(p, p->cursor_y));
   p->cursor_x = conp->vc_x;
   p->cursor_y = conp->vc_y;

   switch (mode) {
      case CM_ERASE:
         cursor_on = 0;
         break;

      case CM_MOVE:
      case CM_DRAW:
         vbl_cursor_cnt = CURSOR_DRAW_DELAY;
         cursor_on = 1;
         break;
   }

   return(0);
}


static void fbcon_vbl_handler(int irq, void *dummy, struct pt_regs *fp)
{
   struct display *p;

   if (!cursor_on)
      return;

   if (vbl_cursor_cnt && --vbl_cursor_cnt == 0) {
      /* Here no check is possible for console changing. The console
       * switching code should set vbl_cursor_cnt to an appropriate value.
       */
      p = &disp[fg_console];
      p->dispsw->rev_char(p, p->cursor_x, real_y(p, p->cursor_y));
      cursor_drawn ^= 1;
      vbl_cursor_cnt = cursor_blink_rate;
   }
}


static __inline__ void ywrap_up(int unit, struct display *p, int count)
{
   p->yscroll += count;
   if (p->yscroll >= p->vrows)	/* Deal with wrap */
      p->yscroll -= p->vrows;
   p->var.xoffset = 0;
   p->var.yoffset = p->yscroll*p->fontheight;
   p->var.vmode |= FB_VMODE_YWRAP;
   fb_info->updatevar(unit);
}


static __inline__ void ywrap_down(int unit, struct display *p, int count)
{
   p->yscroll -= count;
   if (p->yscroll < 0)		/* Deal with wrap */
      p->yscroll += p->vrows;
   p->var.xoffset = 0;
   p->var.yoffset = p->yscroll*p->fontheight;
   p->var.vmode |= FB_VMODE_YWRAP;
   fb_info->updatevar(unit);
}


static __inline__ void ypan_up(int unit, struct vc_data *conp,
			       struct display *p, int count)
{
   p->yscroll += count;
   if (p->yscroll+conp->vc_rows > p->vrows) {
      p->dispsw->bmove(p, p->yscroll, 0, 0, 0, conp->vc_rows-count,
		       conp->vc_cols);
      p->yscroll = 0;
   }
   p->var.xoffset = 0;
   p->var.yoffset = p->yscroll*p->fontheight;
   p->var.vmode &= ~FB_VMODE_YWRAP;
   fb_info->updatevar(unit);
}


static __inline__ void ypan_down(int unit, struct vc_data *conp,
				 struct display *p, int count)
{
   p->yscroll -= count;
   if (p->yscroll < 0) {
      p->yscroll = p->vrows-conp->vc_rows;
      p->dispsw->bmove(p, 0, 0, p->yscroll+count, 0, conp->vc_rows-count,
		       conp->vc_cols);
   }
   p->var.xoffset = 0;
   p->var.yoffset = p->yscroll*p->fontheight;
   p->var.vmode &= ~FB_VMODE_YWRAP;
   fb_info->updatevar(unit);
}


static int fbcon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
   int unit = conp->vc_num;
   struct display *p = &disp[unit];

   if (!p->can_soft_blank && console_blanked)
      return(0);

   fbcon_cursor(conp, CM_ERASE);

   /*
    * ++Geert: Only use ywrap/ypan if the console is in text mode
    */

   switch (dir) {
      case SM_UP:
	 if (count > conp->vc_rows)	/* Maximum realistic size */
	    count = conp->vc_rows;
	 if (vt_cons[unit]->vc_mode == KD_TEXT)
	    switch (p->scrollmode) {
	       case SCROLL_YWRAP:
		  if (b-t-count > 3*conp->vc_rows>>2) {
		     if (t > 0)
			fbcon_bmove(conp, 0, 0, count, 0, t, conp->vc_cols);
		     ywrap_up(unit, p, count);
		     if (conp->vc_rows-b > 0)
			fbcon_bmove(conp, b-count, 0, b, 0, conp->vc_rows-b,
				    conp->vc_cols);
		  } else
		     fbcon_bmove(conp, t+count, 0, t, 0, b-t-count,
				 conp->vc_cols);
		  fbcon_clear(conp, b-count, 0, count, conp->vc_cols);
		  break;

	       case SCROLL_YPAN:
		  if (b-t-count > 3*conp->vc_rows>>2) {
		     if (t > 0)
			fbcon_bmove(conp, 0, 0, count, 0, t, conp->vc_cols);
		     ypan_up(unit, conp, p, count);
		     if (conp->vc_rows-b > 0)
			fbcon_bmove(conp, b-count, 0, b, 0, conp->vc_rows-b,
				    conp->vc_cols);
		  } else
		     fbcon_bmove(conp, t+count, 0, t, 0, b-t-count,
				 conp->vc_cols);
		  fbcon_clear(conp, b-count, 0, count, conp->vc_cols);
		  break;

	       case SCROLL_YMOVE:
		  p->dispsw->bmove(p, t+count, 0, t, 0, b-t-count,
				   conp->vc_cols);
		  p->dispsw->clear(conp, p, b-count, 0, count, conp->vc_cols);
		  break;
	    }
	 else {
	    fbcon_bmove(conp, t+count, 0, t, 0, b-t-count, conp->vc_cols);
	    fbcon_clear(conp, b-count, 0, count, conp->vc_cols);
	 }
         break;

      case SM_DOWN:
	 if (count > conp->vc_rows)	/* Maximum realistic size */
	    count = conp->vc_rows;
	 if (vt_cons[unit]->vc_mode == KD_TEXT)
	    switch (p->scrollmode) {
	       case SCROLL_YWRAP:
		  if (b-t-count > 3*conp->vc_rows>>2) {
		     if (conp->vc_rows-b > 0)
			fbcon_bmove(conp, b, 0, b-count, 0, conp->vc_rows-b,
				    conp->vc_cols);
		     ywrap_down(unit, p, count);
		     if (t > 0)
			fbcon_bmove(conp, count, 0, 0, 0, t, conp->vc_cols);
		  } else
		     fbcon_bmove(conp, t, 0, t+count, 0, b-t-count,
				 conp->vc_cols);
		  fbcon_clear(conp, t, 0, count, conp->vc_cols);
		  break;

	       case SCROLL_YPAN:
		  if (b-t-count > 3*conp->vc_rows>>2) {
		     if (conp->vc_rows-b > 0)
			fbcon_bmove(conp, b, 0, b-count, 0, conp->vc_rows-b,
				    conp->vc_cols);
		     ypan_down(unit, conp, p, count);
		     if (t > 0)
			fbcon_bmove(conp, count, 0, 0, 0, t, conp->vc_cols);
		  } else
		     fbcon_bmove(conp, t, 0, t+count, 0, b-t-count,
				 conp->vc_cols);
		  fbcon_clear(conp, t, 0, count, conp->vc_cols);
		  break;

	       case SCROLL_YMOVE:
		  p->dispsw->bmove(p, t, 0, t+count, 0, b-t-count,
				   conp->vc_cols);
		  p->dispsw->clear(conp, p, t, 0, count, conp->vc_cols);
		  break;
	    }
	 else {
	    /*
	     * Fixed bmove() should end Arno's frustration with copying?
	     * Confucius says:
	     *    Man who copies in wrong direction, end up with trashed data
	     */
	    fbcon_bmove(conp, t, 0, t+count, 0, b-t-count, conp->vc_cols);
	    fbcon_clear(conp, t, 0, count, conp->vc_cols);
	 }
         break;

      case SM_LEFT:
         fbcon_bmove(conp, 0, t+count, 0, t, conp->vc_rows, b-t-count);
         fbcon_clear(conp, 0, b-count, conp->vc_rows, count);
         break;

      case SM_RIGHT:
         fbcon_bmove(conp, 0, t, 0, t+count, conp->vc_rows, b-t-count);
         fbcon_clear(conp, 0, t, conp->vc_rows, count);
         break;
   }

   return(0);
}


static int fbcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
                       int height, int width)
{
   int unit = conp->vc_num;
   struct display *p = &disp[unit];

   if (!p->can_soft_blank && console_blanked)
      return(0);

   if (((sy <= p->cursor_y) && (p->cursor_y < sy+height) &&
        (sx <= p->cursor_x) && (p->cursor_x < sx+width)) ||
       ((dy <= p->cursor_y) && (p->cursor_y < dy+height) &&
        (dx <= p->cursor_x) && (p->cursor_x < dx+width)))
      fbcon_cursor(conp, CM_ERASE);

   /* Split blits that cross physical y_wrap case.
    * Pathological case involves 4 blits, better to use recursive
    * code rather than unrolled case
    *
    * Recursive invocations don't need to erase the cursor over and
    * over again, so we use fbcon_bmove_rec()
    */
   fbcon_bmove_rec(p, sy, sx, dy, dx, height, width, p->vrows-p->yscroll);

   return(0);
}


static void fbcon_bmove_rec(struct display *p, int sy, int sx, int dy, int dx,
                            int height, int width, u_int y_break)
{
   u_int b;

   if (sy < y_break && sy+height > y_break) {
      b = y_break-sy;
      if (dy < sy) {       /* Avoid trashing self */
         fbcon_bmove_rec(p, sy, sx, dy, dx, b, width, y_break);
         fbcon_bmove_rec(p, sy+b, sx, dy+b, dx, height-b, width, y_break);
      } else {
         fbcon_bmove_rec(p, sy+b, sx, dy+b, dx, height-b, width, y_break);
         fbcon_bmove_rec(p, sy, sx, dy, dx, b, width, y_break);
      }
      return;
   }

   if (dy < y_break && dy+height > y_break) {
      b = y_break-dy;
      if (dy < sy) {       /* Avoid trashing self */
         fbcon_bmove_rec(p, sy, sx, dy, dx, b, width, y_break);
         fbcon_bmove_rec(p, sy+b, sx, dy+b, dx, height-b, width, y_break);
      } else {
         fbcon_bmove_rec(p, sy+b, sx, dy+b, dx, height-b, width, y_break);
         fbcon_bmove_rec(p, sy, sx, dy, dx, b, width, y_break);
      }
      return;
   }
   p->dispsw->bmove(p, real_y(p, sy), sx, real_y(p, dy), dx, height, width);
}


static int fbcon_switch(struct vc_data *conp)
{
   if (fb_info && fb_info->switch_con)
      (*fb_info->switch_con)(conp->vc_num);
   return(0);
}


static int fbcon_blank(int blank)
{
   struct display *p = &disp[fg_console];

   fbcon_cursor(p->conp, blank ? CM_ERASE : CM_DRAW);

   if (!p->can_soft_blank)
      if (blank) {
         if (p->visual == FB_VISUAL_MONO01)
             mymemset(p->screen_base, p->var.xres_virtual*p->var.yres_virtual*
                                      p->var.bits_per_pixel>>3);
          else
             mymemclear(p->screen_base, p->var.xres_virtual*p->var.yres_virtual*
                                        p->var.bits_per_pixel>>3);
         return(0);
      } else {
         /* Tell console.c that it has to restore the screen itself */
         return(1);
      }
   (*fb_info->blank)(blank);
   return(0);
}


static int fbcon_get_font(struct vc_data *conp, int *w, int *h, char *data)
{
	int unit = conp->vc_num;
	struct display *p = &disp[unit];
	int i, j, size, alloc;

	size = (p->fontwidth+7)/8 * p->fontheight * 256;
	alloc = (*w+7)/8 * *h * 256;
	*w = p->fontwidth;
	*h = p->fontheight;
   
	if (alloc < size)
		/* allocation length not sufficient */
		return( -ENAMETOOLONG );

	for (i = 0; i < 256; i++)
		for (j = 0; j < p->fontheight; j++)
			data[i*32+j] = p->fontdata[i*p->fontheight+j];
	return( 0 );
}


#define REFCOUNT(fd)	(((int *)(fd))[-1])

static int fbcon_set_font(struct vc_data *conp, int w, int h, char *data)
{
	int unit = conp->vc_num;
	struct display *p = &disp[unit];
	int i, j, size, userspace = 1, resize;
	char *old_data = NULL, *new_data;

	if (w < 0)
		w = p->fontwidth;
	if (h < 0)
		h = p->fontheight;
	
	if (w == 0) {
		/* engage predefined font, name in 'data' */
		char name[MAX_FONT_NAME+1];
	   
		if ((i = verify_area( VERIFY_READ, (void *)data, MAX_FONT_NAME )))
			return i;
		copy_from_user( name, data, MAX_FONT_NAME );
		name[sizeof(name)-1] = 0;
		
		if (!findsoftfont( name, &w, &h, (u_char **)&data ))
			return( -ENOENT );
		userspace = 0;
	}
	else if (w == 1) {
		/* copy font from some other console in 'h'*/
		struct display *op;

		if (h < 0 || !vc_cons_allocated( h ))
			return( -ENOTTY );
		if (h == unit)
			return( 0 ); /* nothing to do */
		op = &disp[h];
		if (op->fontdata == p->fontdata)
			return( 0 ); /* already the same font... */

		resize = (op->fontwidth != p->fontwidth) ||
			     (op->fontheight != p->fontheight);
		if (p->userfont)
			old_data = p->fontdata;
		p->fontdata = op->fontdata;
		w = p->fontwidth = op->fontwidth;
		h = p->fontheight = op->fontheight;
		if ((p->userfont = op->userfont))
			REFCOUNT(p->fontdata)++; /* increment usage counter */
		goto activate;
	}

	if (w != 8)
		/* Currently only fontwidth == 8 supported */
		return( -ENXIO );
	
	resize = (w != p->fontwidth) || (h != p->fontheight);
	size = (w+7)/8 * h * 256;
	
	if (p->userfont)
		old_data = p->fontdata;
	
	if (userspace) {
		if (!(new_data = kmalloc( sizeof(int)+size, GFP_USER )))
			return( -ENOMEM );
		new_data += sizeof(int);
		REFCOUNT(new_data) = 1; /* usage counter */

		for (i = 0; i < 256; i++)
			for (j = 0; j < h; j++)
				new_data[i*h+j] = data[i*32+j];

		p->fontdata = new_data;
		p->userfont = 1;
	}
	else {
		p->fontdata = data;
		p->userfont = 0;
	}
	p->fontwidth = w;
	p->fontheight = h;

  activate:
	if (resize) {
		p->var.xoffset = p->var.yoffset = p->yscroll = 0;  /* reset wrap/pan */
		/* Adjust the virtual screen-size to fontheight*rows */
		p->var.yres_virtual = (p->var.yres/h)*h;
		p->vrows = p->var.yres_virtual/h;
		updatescrollmode(p);
		vc_resize_con( p->var.yres/h, p->var.xres/w, unit );
	}
	else if (unit == fg_console)
		update_screen( unit );
	
	if (old_data) {
		if (--REFCOUNT(old_data) == 0) {
			kfree( old_data - sizeof(int) );
		}
	}
	
	return( 0 );
}

static unsigned short palette_red[16];
static unsigned short palette_green[16];
static unsigned short palette_blue[16];

static struct fb_cmap palette_cmap  = {
    0, 16, palette_red, palette_green, palette_blue, NULL
};

static int fbcon_set_palette(struct vc_data *conp, unsigned char *table)
{
    int unit = conp->vc_num;
    struct display *p = &disp[unit];
    int i, j, k;
    u_char val;

    if (!conp->vc_can_do_color || (!p->can_soft_blank && console_blanked))
	return(-EINVAL);
    for (i = j = 0; i < 16; i++) {
	k = table[i];
	val = conp->vc_palette[j++];
	palette_red[k] = (val<<8)|val;
	val = conp->vc_palette[j++];
	palette_green[k] = (val<<8)|val;
	val = conp->vc_palette[j++];
	palette_blue[k] = (val<<8)|val;
    }
    palette_cmap.len = 1<<p->var.bits_per_pixel;
    if (palette_cmap.len > 16)
	palette_cmap.len = 16;
    return(fb_info->setcmap(&palette_cmap, unit));
}


/* ====================================================================== */

/*
 *    Low Level Operations for the various display memory organizations.
 *
 *    Currently only the following organizations are supported here:
 *
 *      - Monochrome
 *      - Color Interleaved Planes à la Amiga
 *      - Color Normal Planes
 *      - Color Interleaved Planes à la Atari (2, 4 and 8 planes)
 *      - Color Packed Pixels (8 and 16 bpp)
 *      - Cybervision Color Packed Pixels (accelerated)
 */

#ifdef CONFIG_FBCON_MONO

   /*
    *    Monochrome
    */

static void bmove_mono(struct display *p, int sy, int sx, int dy, int dx,
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


static void clear_mono(struct vc_data *conp, struct display *p, int sy, int sx,
                       int height, int width)
{
   u_char *dest;
   u_int rows;

   dest = p->screen_base+sy*p->fontheight*p->next_line+sx;

   if (sx == 0 && width == p->next_line)
      if (attr_reverse(p,conp))
         mymemset(dest, height*p->fontheight*width);
      else
         mymemclear(dest, height*p->fontheight*width);
   else
      for (rows = height*p->fontheight; rows--; dest += p->next_line)
         if (attr_reverse(p,conp))
            mymemset(dest, width);
         else
            mymemclear_small(dest, width);
}


static void putc_mono(struct vc_data *conp, struct display *p, int c, int yy,
                      int xx)
{
   u_char *dest, *cdat;
   u_int rows, bold, revs, underl;
   u_char d;

   c &= 0xff;

   dest = p->screen_base + yy*p->fontheight*p->next_line + xx;
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


static void putcs_mono(struct vc_data *conp, struct display *p, const char *s,
                       int count, int yy, int xx)
{
   u_char *dest, *dest0, *cdat;
   u_int rows, bold, revs, underl;
   u_char c, d;

   dest0 = p->screen_base + yy*p->fontheight*p->next_line + xx;
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


static void rev_char_mono(struct display *p, int xx, int yy)
{
   u_char *dest;
   u_int rows;

   dest = p->screen_base + yy*p->fontheight*p->next_line + xx;
   for (rows = p->fontheight; rows--; dest += p->next_line)
      *dest = ~*dest;
}

#endif /* CONFIG_FBCON_MONO */


/* ====================================================================== */

#ifdef CONFIG_FBCON_ILBM

   /*
    *    Color Interleaved Planes
    *
    *    This code heavily relies on the fact that
    *
    *       next_line == interleave == next_plane*bits_per_pixel
    *
    *    But maybe it can be merged with the code for normal bitplanes without
    *    much performance loss?
    */

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

   dest = p->screen_base + yy*p->fontheight*p->next_line + xx;
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
 *    I split the console character loop in two parts:
 *
 *      - slow version: this blits one character at a time
 *
 *      - fast version: this blits 4 characters at a time at a longword aligned
 *                      address, to reduce the number of expensive Chip RAM
 *                      accesses.
 *
 *    Experiments on my A4000/040 revealed that this makes a console
 *    switch on a 640x400 screen with 256 colors about 3 times faster.
 *
 *                                                                Geert
 */

static void putcs_ilbm(struct vc_data *conp, struct display *p, const char *s,
                       int count, int yy, int xx)
{
   u_char *dest0, *dest, *cdat1, *cdat2, *cdat3, *cdat4;
   u_int rows, i;
   u_char c1, c2, c3, c4;
   u_long d;
   int fg0, bg0, fg, bg;

   dest0 = p->screen_base + yy*p->fontheight*p->next_line + xx;
   fg0 = attr_fgcol(p,conp);
   bg0 = attr_bgcol(p,conp);

   while (count--)
      if (xx & 3 || count < 3) {   /* Slow version */
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
      } else {                      /* Fast version */
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

   dest0 = p->screen_base + yy*p->fontheight*p->next_line + xx;
   mask = p->fgcol ^ p->bgcol;

   /*
    *    This should really obey the individual character's
    *    background and foreground colors instead of simply
    *    inverting.
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

#endif /* CONFIG_FBCON_ILBM */


/* ====================================================================== */

#ifdef CONFIG_FBCON_PLANES

   /*
    *    Color Planes
    */

static void bmove_plan(struct display *p, int sy, int sx, int dy, int dx,
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


static void clear_plan(struct vc_data *conp, struct display *p, int sy, int sx,
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


static void putc_plan(struct vc_data *conp, struct display *p, int c, int yy,
                      int xx)
{
   u_char *dest, *dest0, *cdat, *cdat0;
   u_int rows, i;
   u_char d;
   int fg, bg;

   c &= 0xff;

   dest0 = p->screen_base + yy*p->fontheight*p->next_line + xx;
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
 *    I split the console character loop in two parts
 *    (cfr. fbcon_putcs_ilbm())
 */

static void putcs_plan(struct vc_data *conp, struct display *p, const char *s,
                       int count, int yy, int xx)
{
   u_char *dest, *dest0, *dest1;
   u_char *cdat1, *cdat2, *cdat3, *cdat4, *cdat10, *cdat20, *cdat30, *cdat40;
   u_int rows, i;
   u_char c1, c2, c3, c4;
   u_long d;
   int fg0, bg0, fg, bg;

   dest0 = p->screen_base + yy*p->fontheight*p->next_line + xx;
   fg0 = attr_fgcol(p,conp);
   bg0 = attr_bgcol(p,conp);

   while (count--)
      if (xx & 3 || count < 3) {   /* Slow version */
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
      } else {                      /* Fast version */
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


static void rev_char_plan(struct display *p, int xx, int yy)
{
   u_char *dest, *dest0;
   u_int rows, i;
   int mask;

   dest0 = p->screen_base + yy*p->fontheight*p->next_line + xx;
   mask = p->fgcol ^ p->bgcol;

   /*
    *    This should really obey the individual character's
    *    background and foreground colors instead of simply
    *    inverting.
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

#endif /* CONFIG_FBCON_PLANES */


/* ====================================================================== */

#ifdef CONFIG_FBCON_2PLANE

   /*
    *    2 Planes (2-bytes interleave)
    */

/* Increment/decrement 2 plane addresses */

#define	INC_2P(p)	do { if (!((long)(++(p)) & 1)) (p) += 2; } while(0)
#define	DEC_2P(p)	do { if ((long)(--(p)) & 1) (p) -= 2; } while(0)

/* Convert a standard 4 bit color to our 2 bit color assignment:
 * If at least two RGB channels are active, the low bit is turned on;
 * The intensity bit (b3) is shifted into b1.
 */

#define	COLOR_2P(c)	(((c & 7) >= 3 && (c & 7) != 4) | (c & 8) >> 2)


static void bmove_2_plane(struct display *p, int sy, int sx, int dy, int dx,
                          int height, int width)
{
	/* bmove() has to distinguish two major cases: If both, source and
	 * destination, start at even addresses or both are at odd
	 * addresses, just the first odd and last even column (if present)
	 * require special treatment (memmove_col()). The rest between
	 * then can be copied by normal operations, because all adjacent
	 * bytes are affected and are to be stored in the same order.
	 *   The pathological case is when the move should go from an odd
	 * address to an even or vice versa. Since the bytes in the plane
	 * words must be assembled in new order, it seems wisest to make
	 * all movements by memmove_col().
	 */

    if (sx == 0 && dx == 0 && width == p->next_line/2) {
		/* Special (but often used) case: Moving whole lines can be
		 * done with memmove()
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
					for(rows = colsize; rows > 0; --rows) {
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
			}
			else {

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

				if (width & 1) {
					memmove_2p_col(dst-3, src-3, colsize, bytes);
				}

			}
		}
		else {
			/* odd->even or even->odd */

			if (upwards) {
				src = p->screen_base + sy * linesize + (sx>>1)*4 + (sx & 1);
				dst = p->screen_base + dy * linesize + (dx>>1)*4 + (dx & 1);
				for(cols = width; cols > 0; --cols) {
					memmove_2p_col(dst, src, colsize, bytes);
					INC_2P(src);
					INC_2P(dst);
				}
			}
			else {
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


static void clear_2_plane(struct vc_data *conp, struct display *p, int sy,
                          int sx, int height, int width)
{
    ulong offset;
    u_char *start;
    int rows;
    int bytes = p->next_line;
    int lines = height * p->fontheight;
    ulong  size;
    u_long          cval;
    u_short			pcval;

    cval = expand2l (COLOR_2P (attr_bgcol_ec(p,conp)));

    if (sx == 0 && width == bytes/2) {

        offset = sy * bytes * p->fontheight;
        size   = lines * bytes;
		memset_even_2p(p->screen_base+offset, size, cval);

    } else {

        offset = (sy * bytes * p->fontheight) + (sx>>1)*4 + (sx & 1);
		start = p->screen_base + offset;
		pcval = expand2w(COLOR_2P(attr_bgcol_ec(p,conp)));

		/* Clears are split if the region starts at an odd column or
		 * end at an even column. These extra columns are spread
		 * across the interleaved planes. All in between can be
		 * cleared by normal mymemclear_small(), because both bytes of
		 * the single plane words are affected.
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
			for(rows = lines; rows-- ; start += bytes)
				memset_even_2p(start, width*2, cval);
		}
    }
}


static void putc_2_plane(struct vc_data *conp, struct display *p, int
			 c, int yy, int xx)
{
	u_char   *dest;
	u_char   *cdat;
	int rows;
	int bytes = p->next_line;
	ulong    eorx, fgx, bgx, fdx;

	c &= 0xff;

	dest  = p->screen_base + yy * p->fontheight * bytes +
		(xx >> 1)*4 + (xx & 1);
	cdat  = p->fontdata + (c * p->fontheight);

	fgx   = expand2w(COLOR_2P(attr_fgcol(p,conp)));
	bgx   = expand2w(COLOR_2P(attr_bgcol(p,conp)));
	eorx  = fgx ^ bgx;

	for(rows = p->fontheight ; rows-- ; dest += bytes) {
		fdx = dup2w(*cdat++);
		__asm__ __volatile__ ("movepw %1,%0@(0)" : /* no outputs */
				      : "a" (dest), "d" ((fdx & eorx) ^ bgx));
	}
}


static void putcs_2_plane(struct vc_data *conp, struct display *p,
                          const char *s, int count, int yy, int xx)
{
	u_char   *dest, *dest0;
	u_char   *cdat, c;
	int rows;
	int bytes;
	ulong    eorx, fgx, bgx, fdx;

	bytes = p->next_line;
	dest0 = p->screen_base + yy * p->fontheight * bytes
		+ (xx >> 1)*4 + (xx & 1);
	fgx   = expand2w(COLOR_2P(attr_fgcol(p,conp)));
	bgx   = expand2w(COLOR_2P(attr_bgcol(p,conp)));
	eorx  = fgx ^ bgx;

	while (count--) {

		c = *s++;
		cdat  = p->fontdata + (c * p->fontheight);

		for(rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
			fdx = dup2w(*cdat++);
			__asm__ __volatile__ ("movepw %1,%0@(0)" : /* no outputs */
					      : "a" (dest), "d" ((fdx & eorx) ^ bgx));
		}
		INC_2P(dest0);
	}
}


static void rev_char_2_plane(struct display *p, int xx, int yy)
{
	u_char *dest;
	int j;
	int bytes;

	dest = p->screen_base + yy * p->fontheight * p->next_line +
		(xx >> 1)*4 + (xx & 1);
	j = p->fontheight;
	bytes = p->next_line;
	while (j--)
	{
		/* This should really obey the individual character's
		 * background and foreground colors instead of simply
		 * inverting.
		 */
		dest[0] = ~dest[0];
		dest[2] = ~dest[2];
		dest += bytes;
	}
}
#endif /* CONFIG_FBCON_2PLANE */


/* ====================================================================== */

#ifdef CONFIG_FBCON_4PLANE

   /*
    *    4 Planes (2-bytes interleave)
    */

/* Increment/decrement 4 plane addresses */

#define	INC_4P(p)	do { if (!((long)(++(p)) & 1)) (p) += 6; } while(0)
#define	DEC_4P(p)	do { if ((long)(--(p)) & 1) (p) -= 6; } while(0)


static void bmove_4_plane(struct display *p, int sy, int sx, int dy, int dx,
                          int height, int width)
{
	/* bmove() has to distinguish two major cases: If both, source and
	 * destination, start at even addresses or both are at odd
	 * addresses, just the first odd and last even column (if present)
	 * require special treatment (memmove_col()). The rest between
	 * then can be copied by normal operations, because all adjacent
	 * bytes are affected and are to be stored in the same order.
	 *   The pathological case is when the move should go from an odd
	 * address to an even or vice versa. Since the bytes in the plane
	 * words must be assembled in new order, it seems wisest to make
	 * all movements by memmove_col().
	 */

    if (sx == 0 && dx == 0 && width == p->next_line/4) {
		/* Special (but often used) case: Moving whole lines can be
		 * done with memmove()
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

				src = p->screen_base + sy * linesize + (sx>>1)*8 + (sx & 1);
				dst = p->screen_base + dy * linesize + (dx>>1)*8 + (dx & 1);

				if (sx & 1) {
					memmove_4p_col(dst, src, colsize, bytes);
					src += 7;
					dst += 7;
					--width;
				}

				if (width > 1) {
					for(rows = colsize; rows > 0; --rows) {
						mymemmove(dst, src, (width>>1)*8);
						src += bytes;
						dst += bytes;
					}
				}

				if (width & 1) {
					src -= colsize * bytes;
					dst -= colsize * bytes;
					memmove_4p_col(dst + (width>>1)*8, src + (width>>1)*8,
									colsize, bytes);
				}
			}
			else {

				if (!((sx+width-1) & 1)) {
					src = p->screen_base + sy * linesize + ((sx+width-1)>>1)*8;
					dst = p->screen_base + dy * linesize + ((dx+width-1)>>1)*8;
					memmove_4p_col(dst, src, colsize, bytes);
					--width;
				}

				src = p->screen_base + sy * linesize + (sx>>1)*8 + (sx & 1);
				dst = p->screen_base + dy * linesize + (dx>>1)*8 + (dx & 1);

				if (width > 1) {
					src += colsize * bytes + (sx & 1)*7;
					dst += colsize * bytes + (sx & 1)*7;
					for(rows = colsize; rows > 0; --rows) {
						src -= bytes;
						dst -= bytes;
						mymemmove(dst, src, (width>>1)*8);
					}
				}

				if (width & 1) {
					memmove_4p_col(dst-7, src-7, colsize, bytes);
				}

			}
		}
		else {
			/* odd->even or even->odd */

			if (upwards) {
				src = p->screen_base + sy * linesize + (sx>>1)*8 + (sx & 1);
				dst = p->screen_base + dy * linesize + (dx>>1)*8 + (dx & 1);
				for(cols = width; cols > 0; --cols) {
					memmove_4p_col(dst, src, colsize, bytes);
					INC_4P(src);
					INC_4P(dst);
				}
			}
			else {
				sx += width-1;
				dx += width-1;
				src = p->screen_base + sy * linesize + (sx>>1)*8 + (sx & 1);
				dst = p->screen_base + dy * linesize + (dx>>1)*8 + (dx & 1);
				for(cols = width; cols > 0; --cols) {
					memmove_4p_col(dst, src, colsize, bytes);
					DEC_4P(src);
					DEC_4P(dst);
				}
			}
		}


    }
}


static void clear_4_plane(struct vc_data *conp, struct display *p, int sy,
                          int sx, int height, int width)
{
    ulong offset;
    u_char *start;
    int rows;
    int bytes = p->next_line;
    int lines = height * p->fontheight;
    ulong  size;
    u_long          cval1, cval2, pcval;

    expand4dl(attr_bgcol_ec(p,conp), &cval1, &cval2);

    if (sx == 0 && width == bytes/4) {

	offset = sy * bytes * p->fontheight;
        size   = lines * bytes;
		memset_even_4p(p->screen_base+offset, size, cval1, cval2);

    } else {

        offset = (sy * bytes * p->fontheight) + (sx>>1)*8 + (sx & 1);
		start = p->screen_base + offset;
		pcval = expand4l(attr_bgcol_ec(p,conp));

		/* Clears are split if the region starts at an odd column or
		 * end at an even column. These extra columns are spread
		 * across the interleaved planes. All in between can be
		 * cleared by normal mymemclear_small(), because both bytes of
		 * the single plane words are affected.
		 */

		if (sx & 1) {
			memclear_4p_col(start, lines, pcval, bytes);
			start += 7;
			width--;
		}

		if (width & 1) {
			memclear_4p_col(start + (width>>1)*8, lines, pcval, bytes);
			width--;
		}

		if (width) {
			for(rows = lines; rows-- ; start += bytes)
				memset_even_4p(start, width*4, cval1, cval2);
		}
    }
}


static void putc_4_plane(struct vc_data *conp, struct display *p, int
			 c, int yy, int xx)
{
	u_char   *dest;
	u_char   *cdat;
	int rows;
	int bytes = p->next_line;
	ulong	  eorx, fgx, bgx, fdx;

	c &= 0xff;

	dest  = p->screen_base + yy * p->fontheight * bytes +
		(xx >> 1)*8 + (xx & 1);
	cdat  = p->fontdata + (c * p->fontheight);

	fgx   = expand4l(attr_fgcol(p,conp));
	bgx   = expand4l(attr_bgcol(p,conp));
	eorx  = fgx ^ bgx;

	for(rows = p->fontheight ; rows-- ; dest += bytes) {
		fdx = dup4l(*cdat++);
		__asm__ __volatile__ ("movepl %1,%0@(0)" : /* no outputs */
				      : "a" (dest), "d" ((fdx & eorx) ^ bgx));
	}
}


static void putcs_4_plane(struct vc_data *conp, struct display *p,
                          const char *s, int count, int yy, int xx)
{
	u_char   *dest, *dest0;
	u_char   *cdat, c;
	int rows;
	int bytes;
	ulong	  eorx, fgx, bgx, fdx;

	bytes = p->next_line;
	dest0 = p->screen_base + yy * p->fontheight * bytes
		+ (xx >> 1)*8 + (xx & 1);
	fgx   = expand4l(attr_fgcol(p,conp));
	bgx   = expand4l(attr_bgcol(p,conp));
	eorx  = fgx ^ bgx;

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
			__asm__ __volatile__ ("movepl %1,%0@(0)" : /* no outputs */
					      : "a" (dest), "d" ((fdx & eorx) ^ bgx));
		}
		INC_4P(dest0);
	}
}


static void rev_char_4_plane(struct display *p, int xx, int yy)
{
   u_char *dest;
   int j;
   int bytes;

   dest = p->screen_base + yy * p->fontheight * p->next_line +
	   (xx >> 1)*8 + (xx & 1);
   j = p->fontheight;
   bytes = p->next_line;

   while (j--)
     {
      /* This should really obey the individual character's
       * background and foreground colors instead of simply
       * inverting.
       */
       dest[0] = ~dest[0];
       dest[2] = ~dest[2];
       dest[4] = ~dest[4];
       dest[6] = ~dest[6];
       dest += bytes;
     }
}
#endif /* CONFIG_FBCON_4PLANE */


/* ====================================================================== */

#ifdef CONFIG_FBCON_8PLANE

   /*
    *    8 Planes (2-bytes interleave)
    */

/* In 8 plane mode, 256 colors would be possible, but only the first
 * 16 are used by the console code (the upper 4 bits are
 * background/unused). For that, the following functions mask off the
 * higher 4 bits of each color.
 */

/* Increment/decrement 8 plane addresses */

#define	INC_8P(p)	do { if (!((long)(++(p)) & 1)) (p) += 14; } while(0)
#define	DEC_8P(p)	do { if ((long)(--(p)) & 1) (p) -= 14; } while(0)


static void bmove_8_plane(struct display *p, int sy, int sx, int dy, int dx,
                          int height, int width)
{
	/* bmove() has to distinguish two major cases: If both, source and
	 * destination, start at even addresses or both are at odd
	 * addresses, just the first odd and last even column (if present)
	 * require special treatment (memmove_col()). The rest between
	 * then can be copied by normal operations, because all adjacent
	 * bytes are affected and are to be stored in the same order.
	 *   The pathological case is when the move should go from an odd
	 * address to an even or vice versa. Since the bytes in the plane
	 * words must be assembled in new order, it seems wisest to make
	 * all movements by memmove_col().
	 */

    if (sx == 0 && dx == 0 && width == p->next_line/8) {
		/* Special (but often used) case: Moving whole lines can be
		 * done with memmove()
		 */
      fast_memmove (p->screen_base + dy * p->next_line * p->fontheight,
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
			}
			else {

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

				if (width & 1) {
					memmove_8p_col(dst-15, src-15, colsize, bytes);
				}

			}
		}
		else {
			/* odd->even or even->odd */

			if (upwards) {
				src = p->screen_base + sy * linesize + (sx>>1)*16 + (sx & 1);
				dst = p->screen_base + dy * linesize + (dx>>1)*16 + (dx & 1);
				for(cols = width; cols > 0; --cols) {
					memmove_8p_col(dst, src, colsize, bytes);
					INC_8P(src);
					INC_8P(dst);
				}
			}
			else {
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


static void clear_8_plane(struct vc_data *conp, struct display *p, int sy,
                          int sx, int height, int width)
{
    ulong offset;
    u_char *start;
    int rows;
    int bytes = p->next_line;
    int lines = height * p->fontheight;
    ulong  size;
    u_long          cval1, cval2, cval3, cval4, pcval1, pcval2;

    expand8ql(attr_bgcol_ec(p,conp), cval1, cval2, cval3, cval4);

    if (sx == 0 && width == bytes/8) {

        offset = sy * bytes * p->fontheight;
        size   = lines * bytes;
		memset_even_8p(p->screen_base+offset, size, cval1,
			       cval2, cval3, cval4);

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

		if (width) {
			for(rows = lines; rows-- ; start += bytes)
				memset_even_8p(start, width*8, cval1, cval2, cval3, cval4);
		}
    }
}


static void putc_8_plane(struct vc_data *conp, struct display *p, int c, int yy,
                         int xx)
{
	u_char   *dest;
	u_char   *cdat;
	int rows;
	int bytes = p->next_line;
	ulong	  eorx1, eorx2, fgx1, fgx2, bgx1, bgx2, fdx;

	c &= 0xff;

	dest  = p->screen_base + yy * p->fontheight * bytes +
		(xx >> 1)*16 + (xx & 1);
	cdat  = p->fontdata + (c * p->fontheight);

	expand8dl(attr_fgcol(p,conp), &fgx1, &fgx2);
	expand8dl(attr_bgcol(p,conp), &bgx1, &bgx2);
	eorx1  = fgx1 ^ bgx1; eorx2  = fgx2 ^ bgx2;

	for(rows = p->fontheight ; rows-- ; dest += bytes) {
		fdx = dup4l(*cdat++);
		__asm__ __volatile__
			("movepl %1,%0@(0)\n\t"
			 "movepl %2,%0@(8)"
			 : /* no outputs */
			 : "a" (dest), "d" ((fdx & eorx1) ^ bgx1),
			 "d" ((fdx & eorx2) ^ bgx2)
			);
	}
}


static void putcs_8_plane(struct vc_data *conp, struct display *p,
                          const char *s, int count, int yy, int xx)
{
	u_char   *dest, *dest0;
	u_char   *cdat, c;
	int rows;
	int bytes;
	ulong	  eorx1, eorx2, fgx1, fgx2, bgx1, bgx2, fdx;

	bytes = p->next_line;
	dest0 = p->screen_base + yy * p->fontheight * bytes
		+ (xx >> 1)*16 + (xx & 1);

	expand8dl(attr_fgcol(p,conp), &fgx1, &fgx2);
	expand8dl(attr_bgcol(p,conp), &bgx1, &bgx2);
	eorx1  = fgx1 ^ bgx1; eorx2  = fgx2 ^ bgx2;

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
			__asm__ __volatile__
				("movepl %1,%0@(0)\n\t"
				  "movepl %2,%0@(8)"
				  : /* no outputs */
				  : "a" (dest), "d" ((fdx & eorx1) ^ bgx1),
				  "d" ((fdx & eorx2) ^ bgx2)
				);
		}
		INC_8P(dest0);
	}
}


static void rev_char_8_plane(struct display *p, int xx, int yy)
{
	u_char *dest;
	int j;
	int bytes;

	dest = p->screen_base + yy * p->fontheight * p->next_line
		+ (xx >> 1)*16 + (xx & 1);
	j = p->fontheight;
	bytes = p->next_line;

	while (j--)
	{
		/* This should really obey the individual character's
		 * background and foreground colors instead of simply
		 * inverting. For 8 plane mode, only the lower 4 bits of the
		 * color are inverted, because only that color registers have
		 * been set up.
		 */
		dest[0] = ~dest[0];
		dest[2] = ~dest[2];
		dest[4] = ~dest[4];
		dest[6] = ~dest[6];
		dest += bytes;
	}
}
#endif /* CONFIG_FBCON_8PLANE */


/* ====================================================================== */

#ifdef CONFIG_FBCON_8PACKED

   /*
    *    8 bpp Packed Pixels
    */

static u_long nibbletab_8_packed[]={
0x00000000,0x000000ff,0x0000ff00,0x0000ffff,
0x00ff0000,0x00ff00ff,0x00ffff00,0x00ffffff,
0xff000000,0xff0000ff,0xff00ff00,0xff00ffff,
0xffff0000,0xffff00ff,0xffffff00,0xffffffff};

static void bmove_8_packed(struct display *p, int sy, int sx, int dy, int dx,
                           int height, int width)
{
	int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
	u_char *src,*dst;

	if (sx == 0 && dx == 0 && width * 8 == bytes) {
		mymemmove(p->screen_base + dy * linesize,
			  p->screen_base + sy * linesize,
			  height * linesize);
	}
	else {
		if (dy < sy || (dy == sy && dx < sx)) {
			src = p->screen_base + sy * linesize + sx * 8;
			dst = p->screen_base + dy * linesize + dx * 8;
			for (rows = height * p->fontheight ; rows-- ;) {
				mymemmove(dst, src, width * 8);
				src += bytes;
				dst += bytes;
			}
		}
		else {
			src = p->screen_base + (sy+height) * linesize + sx * 8
				- bytes;
			dst = p->screen_base + (dy+height) * linesize + dx * 8
				- bytes;
			for (rows = height * p->fontheight ; rows-- ;) {
				mymemmove(dst, src, width * 8);
				src -= bytes;
				dst -= bytes;
			}
		}
	}
}


static void clear_8_packed(struct vc_data *conp, struct display *p, int sy,
                           int sx, int height, int width)
{
	u_char *dest0,*dest;
	int bytes=p->next_line,lines=height * p->fontheight, rows, i;
	u_long bgx;

	dest = p->screen_base + sy * p->fontheight * bytes + sx * 8;

	bgx=attr_bgcol_ec(p,conp);
	bgx |= (bgx << 8);
	bgx |= (bgx << 16);

	if (sx == 0 && width * 8 == bytes) {
		for (i = 0 ; i < lines * width ; i++) {
			((u_long *)dest)[0]=bgx;
			((u_long *)dest)[1]=bgx;
			dest+=8;
		}
	} else {
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


static void putc_8_packed(struct vc_data *conp, struct display *p, int c,
			  int yy, int xx)
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
		((u_long *)dest)[0]=
			(nibbletab_8_packed[*cdat >> 4] & eorx) ^ bgx;
		((u_long *)dest)[1]=
			(nibbletab_8_packed[*cdat++ & 0xf] & eorx) ^ bgx;
	}
}


static void putcs_8_packed(struct vc_data *conp, struct display *p,
                           const char *s, int count, int yy, int xx)
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
			((u_long *)dest)[0]=
			(nibbletab_8_packed[*cdat >> 4] & eorx) ^ bgx;
			((u_long *)dest)[1]=
			(nibbletab_8_packed[*cdat++ & 0xf] & eorx) ^ bgx;
		}
		dest0+=8;
	}
}


static void rev_char_8_packed(struct display *p, int xx, int yy)
{
	u_char *dest;
	int bytes=p->next_line, rows;

	dest = p->screen_base + yy * p->fontheight * bytes + xx * 8;
	for (rows = p->fontheight ; rows-- ; dest += bytes) {
		((u_long *)dest)[0] ^= 0x0f0f0f0f;
		((u_long *)dest)[1] ^= 0x0f0f0f0f;
	}
}

#endif /* CONFIG_FBCON_8PACKED */


/* ====================================================================== */

#ifdef CONFIG_FBCON_16PACKED

   /*
    *    16 bpp Packed Pixels
    */

u_short packed16_cmap[16];

static u_long tab_16_packed[]={
0x00000000,0x0000ffff,0xffff0000,0xffffffff};

static void bmove_16_packed(struct display *p, int sy, int sx, int dy, int dx,
                            int height, int width)
{
	int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
	u_char *src,*dst;

	if (sx == 0 && dx == 0 && width * 16 == bytes) {
		mymemmove(p->screen_base + dy * linesize,
			  p->screen_base + sy * linesize,
			  height * linesize);
	}
	else {
		if (dy < sy || (dy == sy && dx < sx)) {
			src = p->screen_base + sy * linesize + sx * 16;
			dst = p->screen_base + dy * linesize + dx * 16;
			for (rows = height * p->fontheight ; rows-- ;) {
				mymemmove(dst, src, width * 16);
				src += bytes;
				dst += bytes;
			}
		}
		else {
			src = p->screen_base + (sy+height) * linesize + sx * 16
				- bytes;
			dst = p->screen_base + (dy+height) * linesize + dx * 16
				- bytes;
			for (rows = height * p->fontheight ; rows-- ;) {
				mymemmove(dst, src, width * 16);
				src -= bytes;
				dst -= bytes;
			}
		}
	}
}


static void clear_16_packed(struct vc_data *conp, struct display *p, int sy,
                            int sx, int height, int width)
{
	u_char *dest0,*dest;
	int bytes=p->next_line,lines=height * p->fontheight, rows, i;
	u_long bgx;

	dest = p->screen_base + sy * p->fontheight * bytes + sx * 16;

	bgx = attr_bgcol_ec(p,conp);
	bgx = packed16_cmap[bgx];
	bgx |= (bgx << 16);

	if (sx == 0 && width * 16 == bytes) {
		for (i = 0 ; i < lines * width ; i++) {
			((u_long *)dest)[0]=bgx;
			((u_long *)dest)[1]=bgx;
			((u_long *)dest)[2]=bgx;
			((u_long *)dest)[3]=bgx;
			dest+=16;
		}
	} else {
		dest0=dest;
		for (rows = lines; rows-- ; dest0 += bytes) {
			dest=dest0;
			for (i = 0 ; i < width ; i++) {
				((u_long *)dest)[0]=bgx;
				((u_long *)dest)[1]=bgx;
				((u_long *)dest)[2]=bgx;
				((u_long *)dest)[3]=bgx;
				dest+=16;
			}
		}
	}
}


static void putc_16_packed(struct vc_data *conp, struct display *p, int c,
                           int yy, int xx)
{
	u_char *dest,*cdat;
	int bytes=p->next_line,rows;
	ulong eorx,fgx,bgx;

	c &= 0xff;

	dest = p->screen_base + yy * p->fontheight * bytes + xx * 16;
	cdat = p->fontdata + c * p->fontheight;

	fgx = attr_fgcol(p,conp);
	fgx = packed16_cmap[fgx];
	bgx = attr_bgcol(p,conp);
	bgx = packed16_cmap[bgx];
	fgx |= (fgx << 16);
	bgx |= (bgx << 16);
	eorx = fgx ^ bgx;

	for (rows = p->fontheight ; rows-- ; dest += bytes) {
		((u_long *)dest)[0]=
			(tab_16_packed[*cdat >> 6] & eorx) ^ bgx;
		((u_long *)dest)[1]=
			(tab_16_packed[*cdat >> 4 & 0x3] & eorx) ^ bgx;
		((u_long *)dest)[2]=
			(tab_16_packed[*cdat >> 2 & 0x3] & eorx) ^ bgx;
		((u_long *)dest)[3]=
			(tab_16_packed[*cdat++ & 0x3] & eorx) ^ bgx;
	}
}


/* TODO */
static void putcs_16_packed(struct vc_data *conp, struct display *p,
                            const char *s, int count, int yy, int xx)
{
	u_char *cdat, c, *dest, *dest0;
	int rows,bytes=p->next_line;
	u_long eorx, fgx, bgx;

	dest0 = p->screen_base + yy * p->fontheight * bytes + xx * 16;
	fgx = attr_fgcol(p,conp);
	fgx = packed16_cmap[fgx];
	bgx = attr_bgcol(p,conp);
	bgx = packed16_cmap[bgx];
	fgx |= (fgx << 16);
	bgx |= (bgx << 16);
	eorx = fgx ^ bgx;
	while (count--) {
		c = *s++;
		cdat = p->fontdata + c * p->fontheight;

		for (rows = p->fontheight, dest = dest0; rows-- ; dest += bytes) {
			((u_long *)dest)[0]=
				(tab_16_packed[*cdat >> 6] & eorx) ^ bgx;
			((u_long *)dest)[1]=
				(tab_16_packed[*cdat >> 4 & 0x3] & eorx) ^ bgx;
			((u_long *)dest)[2]=
				(tab_16_packed[*cdat >> 2 & 0x3] & eorx) ^ bgx;
			((u_long *)dest)[3]=
				(tab_16_packed[*cdat++ & 0x3] & eorx) ^ bgx;
		}
		dest0+=16;
	}
}


static void rev_char_16_packed(struct display *p, int xx, int yy)
{
	u_char *dest;
	int bytes=p->next_line, rows;

	dest = p->screen_base + yy * p->fontheight * bytes + xx * 16;
	for (rows = p->fontheight ; rows-- ; dest += bytes) {
		((u_long *)dest)[0] ^= 0xffffffff;
		((u_long *)dest)[1] ^= 0xffffffff;
		((u_long *)dest)[2] ^= 0xffffffff;
		((u_long *)dest)[3] ^= 0xffffffff;
	}
}

#endif /* CONFIG_FBCON_16PACKED */


/* ====================================================================== */

#ifdef CONFIG_FBCON_CYBER

   /*
    *    Cybervision (accelerated)
    */

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

	dest = p->screen_base + yy * p->fontheight * p->next_line + 8 * xx;
	cdat = p->fontdata+(c*p->fontheight);
	fg = disp->fgcol;
	bg = disp->bgcol;
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
	u_int rows, revs, underl;
	u_char c, d;
	u_char fg, bg;

	dest0 = p->screen_base + yy * p->fontheight * p->next_line + 8 * xx;
	fg = disp->fgcol;
	bg = disp->bgcol;
	revs = conp->vc_reverse;
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
}


static void rev_char_cyber(struct display *p, int xx, int yy)
{
	unsigned char *dest;
	unsigned int rows;
	unsigned char fg, bg;

	fg = disp->fgcol;
	bg = disp->bgcol;

	dest = p->screen_base + yy * p->fontheight * p->next_line + 8 * xx;
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

#endif /* CONFIG_FBCON_CYBER */


#ifdef CONFIG_FBCON_RETINAZ3

/*
 *    RetinaZ3 (accelerated)
 */

#define Z3BLTcopy		0xc0
#define Z3BLTset		0xf0

static void clear_retz3(struct vc_data *conp, struct display *p, int
			sy, int sx, int height, int width)
{
	unsigned short col;
	int fontwidth = p->fontwidth;

	sx *= fontwidth;
	width *= fontwidth;

	col = attr_bgcol_ec(p, conp);
	col &= 0xff;
	col |= (col << 8);

	retz3_bitblt(&p->var,
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)width,
		     (unsigned short)(height*p->fontheight),
		     Z3BLTset,
		     col); 
}

static void bmove_retz3(struct display *p, int sy, int sx, int dy, int dx,
                        int height, int width)
{
	int fontwidth = p->fontwidth;

	sx *= fontwidth;
	dx *= fontwidth;
	width *= fontwidth;

	retz3_bitblt(&p->var,
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)dx,
		     (unsigned short)(dy*p->fontheight),
		     (unsigned short)width,
		     (unsigned short)(height*p->fontheight),
		     Z3BLTcopy,
		     0xffff);
}

static void putc_retz3(struct vc_data *conp, struct display *p,
		       int c, int yy, int xx)
{
	unsigned char *dest, *cdat;
	unsigned long tmp;
	unsigned int rows, revs, underl, bytes;
	unsigned char d;
	unsigned char fg, bg;
   
	c &= 0xff;

	bytes = p->next_line;

	dest = p->screen_base + yy*p->fontheight*bytes
		+ xx*p->var.bits_per_pixel;
	cdat = p->fontdata + c * p->fontheight;

	fg = disp->fgcol;
	bg = disp->bgcol;
	revs = conp->vc_reverse;
	underl = conp->vc_underline;

	for (rows = p->fontheight; rows--; dest += bytes) {
		d = *cdat++;

		if (underl && !rows)
			d = 0xff;
		if (revs)
			d = ~d;

		tmp =  ((d & 0x80) ? fg : bg) << 24;
		tmp |= ((d & 0x40) ? fg : bg) << 16;
		tmp |= ((d & 0x20) ? fg : bg) << 8;
		tmp |= ((d & 0x10) ? fg : bg);
		*((unsigned long*) dest) = tmp;
		tmp =  ((d & 0x8) ? fg : bg) << 24;
		tmp |= ((d & 0x4) ? fg : bg) << 16;
		tmp |= ((d & 0x2) ? fg : bg) << 8;
		tmp |= ((d & 0x1) ? fg : bg);
		*((unsigned long*) dest + 1) = tmp;
	}
}


static void putcs_retz3(struct vc_data *conp, struct display *p,
			const char *s, int count, int yy, int xx)
{
	unsigned char *dest, *dest0, *cdat;
	unsigned long tmp;
	unsigned int rows, revs, underl, bytes;
	unsigned char c, d;
	unsigned char fg, bg;

	bytes = p->next_line;

	dest0 = p->screen_base + yy*p->fontheight*bytes
		+ xx * p->var.bits_per_pixel;
	fg = disp->fgcol;
	bg = disp->bgcol;
	revs = conp->vc_reverse;
	underl = conp->vc_underline;

	while (count--) {
		c = *s++;
		dest = dest0;
		dest0 += 8;

		cdat = p->fontdata + c * p->fontheight;
		for (rows = p->fontheight; rows--; dest += bytes) {
 			d = *cdat++;

			if (underl && !rows)
				d = 0xff;
			if (revs)
				d = ~d;

			tmp =  ((d & 0x80) ? fg : bg) << 24;
			tmp |= ((d & 0x40) ? fg : bg) << 16;
			tmp |= ((d & 0x20) ? fg : bg) << 8;
			tmp |= ((d & 0x10) ? fg : bg);
			*((unsigned long*) dest) = tmp;
			tmp =  ((d & 0x8) ? fg : bg) << 24;
			tmp |= ((d & 0x4) ? fg : bg) << 16;
			tmp |= ((d & 0x2) ? fg : bg) << 8;
			tmp |= ((d & 0x1) ? fg : bg);
			*((unsigned long*) dest + 1) = tmp;
		}
	}
}

static void rev_char_retz3(struct display *p, int xx, int yy)
{
	unsigned char *dest;
	int bytes=p->next_line, rows;
	unsigned int bpp, mask;

	bpp = p->var.bits_per_pixel;

	switch (bpp){
	case 8:
		mask = 0x0f0f0f0f;
		break;
	case 16:
		mask = 0xffffffff;
		break;
	case 24:
		mask = 0xffffffff; /* ??? */
		break;
	default:
		printk("illegal depth for rev_char_retz3(), bpp = %i\n", bpp);
		return;
	}

	dest = p->screen_base + yy * p->fontheight * bytes + xx * bpp;

	for (rows = p->fontheight ; rows-- ; dest += bytes) {
		((unsigned long *)dest)[0] ^= mask;
		((unsigned long *)dest)[1] ^= mask;
	}
}

#endif

/* ====================================================================== */

   /*
    *    The console `switch' structure for the frame buffer based console
    */

struct consw fb_con = {
   fbcon_startup, fbcon_init, fbcon_deinit, fbcon_clear, fbcon_putc,
   fbcon_putcs, fbcon_cursor, fbcon_scroll, fbcon_bmove, fbcon_switch,
   fbcon_blank, fbcon_get_font, fbcon_set_font, fbcon_set_palette
};
