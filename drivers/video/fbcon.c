/*
 *  linux/drivers/video/fbcon.c -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 *  This file is based on the original Amiga console driver (amicon.c):
 *
 *	Copyright (C) 1993 Hamish Macdonald
 *			   Greg Harp
 *	Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *	      with work by William Rucklidge (wjr@cs.cornell.edu)
 *			   Geert Uytterhoeven
 *			   Jes Sorensen (jds@kom.auc.dk)
 *			   Martin Apel
 *
 *  and on the original Atari console driver (atacon.c):
 *
 *	Copyright (C) 1993 Bjoern Brauel
 *			   Roman Hodek
 *
 *	      with work by Guenther Kelleter
 *			   Martin Schaller
 *			   Andreas Schwab
 *
 *
 *  The low level operations for the various display memory organizations are
 *  now in separate source files.
 *
 *  Currently only the following organizations are supported:
 *
 *    - non-accelerated:
 *
 *	  o mfb		 Monochrome
 *	  o ilbm	 Interleaved bitplanes à la Amiga
 *	  o afb		 Bitplanes à la Amiga
 *	  o iplan2p[248] Interleaved bitplanes à la Atari (2, 4 and 8 planes)
 *	  o cfb{8,16}    Packed pixels (8 and 16 bpp)
 *
 *    - accelerated:
 *
 *	  o cyber	 CyberVision64 packed pixels (accelerated)
 *	  o retz3	 Retina Z3 packed pixels (accelerated)
 *	  o mach64	 ATI Mach 64 packed pixels (accelerated)
 *
 *  To do:
 *
 *    - Implement 16 plane mode (iplan2p16)
 *    - Add support for 24/32 bit packed pixels (cfb{24,32})
 *    - Hardware cursor
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#define SUPPORT_SCROLLBACK	0
#define FLASHING_CURSOR		1

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/init.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_ATARI
#include <asm/atariints.h>
#endif
#ifdef __mc68000__
#include <asm/machdep.h>
#include <asm/setup.h>
#endif
#include <asm/linux_logo.h>

#include "fbcon.h"
#include "font.h"


struct display fb_display[MAX_NR_CONSOLES];


/* ++Geert: Sorry, no hardware cursor support at the moment;
   use Atari alike software cursor */

#if FLASHING_CURSOR
static int cursor_drawn = 0;

#define CURSOR_DRAW_DELAY		(2)

/* # VBL ints between cursor state changes */
#define AMIGA_CURSOR_BLINK_RATE		(20)
#define ATARI_CURSOR_BLINK_RATE		(42)
#define DEFAULT_CURSOR_BLINK_RATE	(20)

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
#endif

/*
 *  Scroll Method
 */

#define SCROLL_YWRAP	(0)
#define SCROLL_YPAN	(1)
#define SCROLL_YMOVE	(2)

#define divides(a, b)	((!(a) || (b)%(a)) ? 0 : 1)


/*
 *  Interface used by the world
 */

static unsigned long fbcon_startup(unsigned long kmem_start,
				   const char **display_desc);
static void fbcon_init(struct vc_data *conp);
static int fbcon_deinit(struct vc_data *conp);
static int fbcon_changevar(int con);
static int fbcon_clear(struct vc_data *conp, int sy, int sx, int height,
		       int width);
static int fbcon_putc(struct vc_data *conp, int c, int ypos, int xpos);
static int fbcon_putcs(struct vc_data *conp, const char *s, int count,
		       int ypos, int xpos);
static int fbcon_cursor(struct vc_data *conp, int mode);
static int fbcon_scroll(struct vc_data *conp, int t, int b,
			int dir, int count);
static int fbcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
		       int height, int width);
static int fbcon_switch(struct vc_data *conp);
static int fbcon_blank(int blank);
static int fbcon_get_font(struct vc_data *conp, int *w, int *h, char *data);
static int fbcon_set_font(struct vc_data *conp, int w, int h, char *data);
static int fbcon_set_palette(struct vc_data *conp, unsigned char *table);
static int fbcon_scrolldelta(int lines);
int fbcon_register_driver(struct display_switch *dispsw, int is_accel);
int fbcon_unregister_driver(struct display_switch *dispsw);


/*
 *  Internal routines
 */

static void fbcon_setup(int con, int setcol, int init);
static __inline__ int real_y(struct display *p, int ypos);
#if FLASHING_CURSOR
static void fbcon_vbl_handler(int irq, void *dummy, struct pt_regs *fp);
#endif
static __inline__ void updatescrollmode(struct display *p);
#if SUPPORT_SCROLLBACK
static __inline__ void ywrap_up(int unit, struct vc_data *conp,
				struct display *p, int count);
static __inline__ void ywrap_down(int unit, struct vc_data *conp,
				  struct display *p, int count);
#else
static __inline__ void ywrap_up(int unit, struct display *p, int count);
static __inline__ void ywrap_down(int unit, struct display *p, int count);
#endif
static __inline__ void ypan_up(int unit, struct vc_data *conp,
			       struct display *p, int count);
static __inline__ void ypan_down(int unit, struct vc_data *conp,
				 struct display *p, int count);
static void fbcon_bmove_rec(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width, u_int y_break);
static struct display_switch *probe_list(struct display_switch *dispsw,
					 struct display *disp);

#ifdef CONFIG_KMOD
static void request_driver(struct display *disp, int is_accel);
#endif
static struct display_switch *fbcon_get_driver(struct display *disp);
static int fbcon_show_logo(void);

#if FLASHING_CURSOR
static void cursor_timer_handler(unsigned long dev_addr);

static struct timer_list cursor_timer = {
    NULL, NULL, 0, 0L, cursor_timer_handler
};

static void cursor_timer_handler(unsigned long dev_addr)
{
      fbcon_vbl_handler(0, NULL, NULL);
      cursor_timer.expires = jiffies+2;
      cursor_timer.data = 0;
      cursor_timer.next = cursor_timer.next = NULL;
      add_timer(&cursor_timer);
}
#endif

/*
 *  Low Level Operations
 */

static struct display_switch dispsw_dummy;

#ifdef CONFIG_FBCON_MFB
extern int fbcon_init_mfb(void);
#endif
#ifdef CONFIG_FBCON_ILBM
extern int fbcon_init_ilbm(void);
#endif
#ifdef CONFIG_FBCON_AFB
extern int fbcon_init_afb(void);
#endif
#ifdef CONFIG_FBCON_IPLAN2P2
extern int fbcon_init_iplan2p2(void);
#endif
#ifdef CONFIG_FBCON_IPLAN2P4
extern int fbcon_init_iplan2p4(void);
#endif
#ifdef CONFIG_FBCON_IPLAN2P8
extern int fbcon_init_iplan2p8(void);
#endif
#ifdef CONFIG_FBCON_CFB8
extern int fbcon_init_cfb8(void);
#endif
#ifdef CONFIG_FBCON_CFB16
extern int fbcon_init_cfb16(void);
#endif
#ifdef CONFIG_FBCON_CFB24
extern int fbcon_init_cfb24(void);
#endif
#ifdef CONFIG_FBCON_CFB32
extern int fbcon_init_cfb32(void);
#endif
#ifdef CONFIG_FBCON_CYBER
extern int fbcon_init_cyber(void);
#endif
#ifdef CONFIG_FBCON_RETINAZ3
extern int fbcon_init_retz3(void);
#endif
#ifdef CONFIG_FBCON_MACH64
extern int fbcon_init_mach64(void);
#endif

extern int num_registered_fb;

__initfunc(static unsigned long fbcon_startup(unsigned long kmem_start,
					      const char **display_desc))
{
    int irqres = 1;

    /* Probe all frame buffer devices */
    kmem_start = probe_framebuffers(kmem_start);

    if (!num_registered_fb)
	    return kmem_start;

    /* Initialize all built-in low level drivers */
#ifdef CONFIG_FBCON_RETINAZ3
    fbcon_init_retz3();
#endif
#ifdef CONFIG_FBCON_MFB
    fbcon_init_mfb();
#endif
#ifdef CONFIG_FBCON_IPLAN2P2
    fbcon_init_iplan2p2();
#endif
#ifdef CONFIG_FBCON_IPLAN2P4
    fbcon_init_iplan2p4();
#endif
#ifdef CONFIG_FBCON_IPLAN2P8
    fbcon_init_iplan2p8();
#endif
#ifdef CONFIG_FBCON_ILBM
    fbcon_init_ilbm();
#endif
#ifdef CONFIG_FBCON_AFB
    fbcon_init_afb();
#endif
#ifdef CONFIG_FBCON_CFB8
    fbcon_init_cfb8();
#endif
#ifdef CONFIG_FBCON_CFB16
    fbcon_init_cfb16();
#endif
#ifdef CONFIG_FBCON_CFB24
    fbcon_init_cfb24();
#endif
#ifdef CONFIG_FBCON_CFB32
    fbcon_init_cfb32();
#endif
#ifdef CONFIG_FBCON_CYBER
    fbcon_init_cyber();
#endif
#ifdef CONFIG_FBCON_MACH64
    fbcon_init_mach64();
#endif

    *display_desc = "frame buffer device";

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
    if (irqres) {
	cursor_blink_rate = DEFAULT_CURSOR_BLINK_RATE;
	cursor_timer.expires = jiffies+2;
	cursor_timer.data = 0;
	cursor_timer.next = cursor_timer.prev = NULL;
	add_timer(&cursor_timer);
    }

    if (!console_show_logo)
	console_show_logo = fbcon_show_logo;

    return kmem_start;
}


static void fbcon_init(struct vc_data *conp)
{
    int unit = conp->vc_num;
    struct fb_info *info;

    /* on which frame buffer will we open this console? */
    info = registered_fb[(int)con2fb_map[unit]];

    info->changevar = &fbcon_changevar;
    fb_display[unit] = *(info->disp);	/* copy from default */
    fb_display[unit].conp = conp;
    fb_display[unit].fb_info = info;
    fbcon_setup(unit, 1, 1);
}


static int fbcon_deinit(struct vc_data *conp)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    if (p->dispsw)
	    p->dispsw->release();
    p->dispsw = 0;
    p->conp = 0;
    return(0);
}


static int fbcon_changevar(int con)
{
    if (fb_display[con].conp)
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
    struct display *p = &fb_display[con];
    struct vc_data *conp = p->conp;
    int nr_rows, nr_cols;
    struct display_switch *old_dispsw, *new_dispsw;

    p->var.xoffset = p->var.yoffset = p->yscroll = 0;  /* reset wrap/pan */

    if (!p->fb_info->fontname[0] ||
	!findsoftfont(p->fb_info->fontname, &p->fontwidth, &p->fontheight,
		      &p->fontdata) || p->fontwidth != 8)
	getdefaultfont(p->var.xres, p->var.yres, NULL, &p->fontwidth,
		       &p->fontheight, &p->fontdata);
    if (p->fontwidth != 8) {
	/* ++Geert: changed from panic() to `correct and continue' */
	printk(KERN_ERR "fbcon_setup: No support for fontwidth != 8");
	p->fontwidth = 8;
    }
    updatescrollmode(p);

    nr_cols = p->var.xres/p->fontwidth;
    nr_rows = p->var.yres/p->fontheight;
    /*
     *  ++guenther: console.c:vc_allocate() relies on initializing
     *  vc_{cols,rows}, but we must not set those if we are only
     *  resizing the console.
     */
    if (init) {
	conp->vc_cols = nr_cols;
	conp->vc_rows = nr_rows;
    }
    p->vrows = p->var.yres_virtual/p->fontheight;
    conp->vc_can_do_color = p->var.bits_per_pixel != 1;

    new_dispsw = fbcon_get_driver(p);
    if (!new_dispsw) {
	printk(KERN_WARNING "fbcon_setup: type %d (aux %d, depth %d) not "
	       "supported\n", p->type, p->type_aux, p->var.bits_per_pixel);
	dispsw_dummy.open(p);
	new_dispsw = &dispsw_dummy;
    }
    /* Be careful when changing dispsw, it might be the current console.  */
    old_dispsw = p->dispsw;
    p->dispsw = new_dispsw;
    if (old_dispsw)
	old_dispsw->release();

    if (setcol) {
	p->fgcol = p->var.bits_per_pixel > 2 ? 7 : (1<<p->var.bits_per_pixel)-1;
	p->bgcol = 0;
    }

    if (!init)
	vc_resize_con(nr_rows, nr_cols, con);
}


/* ====================================================================== */

/*  fbcon_XXX routines - interface used by the world
 *
 *  This system is now divided into two levels because of complications
 *  caused by hardware scrolling. Top level functions:
 *
 *	fbcon_bmove(), fbcon_clear(), fbcon_putc()
 *
 *  handles y values in range [0, scr_height-1] that correspond to real
 *  screen positions. y_wrap shift means that first line of bitmap may be
 *  anywhere on this display. These functions convert lineoffsets to
 *  bitmap offsets and deal with the wrap-around case by splitting blits.
 *
 *	fbcon_bmove_physical_8()    -- These functions fast implementations
 *	fbcon_clear_physical_8()    -- of original fbcon_XXX fns.
 *	fbcon_putc_physical_8()	    -- (fontwidth != 8) may be added later
 *
 *  WARNING:
 *
 *  At the moment fbcon_putc() cannot blit across vertical wrap boundary
 *  Implies should only really hardware scroll in rows. Only reason for
 *  restriction is simplicity & efficiency at the moment.
 */

static __inline__ int real_y(struct display *p, int ypos)
{
    int rows = p->vrows;

    ypos += p->yscroll;
    return(ypos < rows ? ypos : ypos-rows);
}


static int fbcon_clear(struct vc_data *conp, int sy, int sx, int height,
			      int width)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];
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


static int fbcon_putc(struct vc_data *conp, int c, int ypos, int xpos)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    if (!p->can_soft_blank && console_blanked)
	    return 0;

    if ((p->cursor_x == xpos) && (p->cursor_y == ypos))
	    CURSOR_UNDRAWN();

    p->dispsw->putc(conp, p, c, real_y(p, ypos), xpos);

    return 0;
}


static int fbcon_putcs(struct vc_data *conp, const char *s, int count,
		       int ypos, int xpos)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    if (!p->can_soft_blank && console_blanked)
	    return 0;

    if ((p->cursor_y == ypos) && (xpos <= p->cursor_x) &&
	(p->cursor_x < (xpos + count)))
	    CURSOR_UNDRAWN();
    p->dispsw->putcs(conp, p, s, count, real_y(p, ypos), xpos);

    return(0);
}


static int fbcon_cursor(struct vc_data *conp, int mode)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

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


#if FLASHING_CURSOR
static void fbcon_vbl_handler(int irq, void *dummy, struct pt_regs *fp)
{
    struct display *p;

    if (!cursor_on)
	return;

    if (vbl_cursor_cnt && --vbl_cursor_cnt == 0) {
	/* Here no check is possible for console changing. The console
	 * switching code should set vbl_cursor_cnt to an appropriate value.
	 */
	p = &fb_display[fg_console];
	p->dispsw->rev_char(p, p->cursor_x, real_y(p, p->cursor_y));
	cursor_drawn ^= 1;
	vbl_cursor_cnt = cursor_blink_rate;
    }
}
#endif

#if SUPPORT_SCROLLBACK
static int scrollback_max = 0;
static int scrollback_current = 0;
#endif

#if SUPPORT_SCROLLBACK
static __inline__ void ywrap_up(int unit, struct vc_data *conp,
				struct display *p, int count)
#else
static __inline__ void ywrap_up(int unit, struct display *p, int count)
#endif
{
    p->yscroll += count;
    if (p->yscroll >= p->vrows)	/* Deal with wrap */
	p->yscroll -= p->vrows;
    p->var.xoffset = 0;
    p->var.yoffset = p->yscroll*p->fontheight;
    p->var.vmode |= FB_VMODE_YWRAP;
    p->fb_info->updatevar(unit);
#if SUPPORT_SCROLLBACK
    scrollback_max += count;
    if (scrollback_max > p->vrows-conp->vc_rows)
	scrollback_max = p->vrows-conp->vc_rows;
    scrollback_current = 0;
#endif
}


#if SUPPORT_SCROLLBACK
static __inline__ void ywrap_down(int unit, struct vc_data *conp,
				  struct display *p, int count)
#else
static __inline__ void ywrap_down(int unit, struct display *p, int count)
#endif
{
    p->yscroll -= count;
    if (p->yscroll < 0)		/* Deal with wrap */
	p->yscroll += p->vrows;
    p->var.xoffset = 0;
    p->var.yoffset = p->yscroll*p->fontheight;
    p->var.vmode |= FB_VMODE_YWRAP;
    p->fb_info->updatevar(unit);
#if SUPPORT_SCROLLBACK
    scrollback_max -= count;
    if (scrollback_max < 0)
	scrollback_max = 0;
    scrollback_current = 0;
#endif
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
    p->fb_info->updatevar(unit);
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
    p->fb_info->updatevar(unit);
}


static int fbcon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

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
				fbcon_bmove(conp, 0, 0, count, 0, t,
					    conp->vc_cols);
#if SUPPORT_SCROLLBACK
			    ywrap_up(unit, conp, p, count);
#else
			    ywrap_up(unit, p, count);
#endif
			    if (conp->vc_rows-b > 0)
				fbcon_bmove(conp, b-count, 0, b, 0,
					    conp->vc_rows-b, conp->vc_cols);
			} else
			    fbcon_bmove(conp, t+count, 0, t, 0, b-t-count,
					conp->vc_cols);
			fbcon_clear(conp, b-count, 0, count, conp->vc_cols);
			break;

		    case SCROLL_YPAN:
			if (b-t-count > 3*conp->vc_rows>>2) {
			    if (t > 0)
				fbcon_bmove(conp, 0, 0, count, 0, t,
					    conp->vc_cols);
			    ypan_up(unit, conp, p, count);
			    if (conp->vc_rows-b > 0)
				fbcon_bmove(conp, b-count, 0, b, 0,
					    conp->vc_rows-b, conp->vc_cols);
			} else
			    fbcon_bmove(conp, t+count, 0, t, 0, b-t-count,
					conp->vc_cols);
			fbcon_clear(conp, b-count, 0, count, conp->vc_cols);
			break;

		    case SCROLL_YMOVE:
			p->dispsw->bmove(p, t+count, 0, t, 0, b-t-count,
					 conp->vc_cols);
			p->dispsw->clear(conp, p, b-count, 0, count,
					 conp->vc_cols);
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
				fbcon_bmove(conp, b, 0, b-count, 0,
					    conp->vc_rows-b, conp->vc_cols);
#if SUPPORT_SCROLLBACK
			    ywrap_down(unit, conp, p, count);
#else
			    ywrap_down(unit, p, count);
#endif
			    if (t > 0)
				fbcon_bmove(conp, count, 0, 0, 0, t,
					    conp->vc_cols);
			} else
			    fbcon_bmove(conp, t, 0, t+count, 0, b-t-count,
					conp->vc_cols);
			fbcon_clear(conp, t, 0, count, conp->vc_cols);
			break;

		    case SCROLL_YPAN:
			if (b-t-count > 3*conp->vc_rows>>2) {
			    if (conp->vc_rows-b > 0)
				fbcon_bmove(conp, b, 0, b-count, 0,
					    conp->vc_rows-b, conp->vc_cols);
			    ypan_down(unit, conp, p, count);
			    if (t > 0)
				fbcon_bmove(conp, count, 0, 0, 0, t,
					    conp->vc_cols);
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
		 *  Fixed bmove() should end Arno's frustration with copying?
		 *  Confucius says:
		 *	Man who copies in wrong direction, end up with trashed
		 *	data
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
    struct display *p = &fb_display[unit];

    if (!p->can_soft_blank && console_blanked)
	return(0);

    if (((sy <= p->cursor_y) && (p->cursor_y < sy+height) &&
	 (sx <= p->cursor_x) && (p->cursor_x < sx+width)) ||
	((dy <= p->cursor_y) && (p->cursor_y < dy+height) &&
	 (dx <= p->cursor_x) && (p->cursor_x < dx+width)))
	fbcon_cursor(conp, CM_ERASE);

    /*  Split blits that cross physical y_wrap case.
     *  Pathological case involves 4 blits, better to use recursive
     *  code rather than unrolled case
     *
     *  Recursive invocations don't need to erase the cursor over and
     *  over again, so we use fbcon_bmove_rec()
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
	if (dy < sy) {	/* Avoid trashing self */
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
	if (dy < sy) {	/* Avoid trashing self */
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
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];
    struct fb_info *info = p->fb_info;

    if (info && info->switch_con)
	(*info->switch_con)(conp->vc_num);
#if SUPPORT_SCROLLBACK
    scrollback_max = 0;
    scrollback_current = 0;
#endif
    return(0);
}


static int fbcon_blank(int blank)
{
    struct display *p = &fb_display[fg_console];

    fbcon_cursor(p->conp, blank ? CM_ERASE : CM_DRAW);

    if (!p->can_soft_blank) {
	if (blank) {
	    if (p->visual == FB_VISUAL_MONO01)
		mymemset(p->screen_base,
			 p->var.xres_virtual*p->var.yres_virtual*
			 p->var.bits_per_pixel>>3);
	     else
		 mymemclear(p->screen_base,
			    p->var.xres_virtual*p->var.yres_virtual*
			    p->var.bits_per_pixel>>3);
	    return(0);
	} else {
	    /* Tell console.c that it has to restore the screen itself */
	    return(1);
	}
    }
    (*p->fb_info->blank)(blank);
    return(0);
}


static int fbcon_get_font(struct vc_data *conp, int *w, int *h, char *data)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];
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
    struct display *p = &fb_display[unit];
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
    } else if (w == 1) {
	/* copy font from some other console in 'h'*/
	struct display *op;

	if (h < 0 || !vc_cons_allocated( h ))
	    return( -ENOTTY );
	if (h == unit)
	    return( 0 ); /* nothing to do */
	op = &fb_display[h];
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
	    REFCOUNT(p->fontdata)++;	/* increment usage counter */
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
    } else {
	p->fontdata = data;
	p->userfont = 0;
    }
    p->fontwidth = w;
    p->fontheight = h;

activate:
    if (resize) {
	/* reset wrap/pan */
	p->var.xoffset = p->var.yoffset = p->yscroll = 0;
	/* Adjust the virtual screen-size to fontheight*rows */
	p->var.yres_virtual = (p->var.yres/h)*h;
	p->vrows = p->var.yres_virtual/h;
	updatescrollmode(p);
	vc_resize_con( p->var.yres/h, p->var.xres/w, unit );
    } else if (unit == fg_console)
	update_screen( unit );

    if (old_data && (--REFCOUNT(old_data) == 0))
	kfree( old_data - sizeof(int) );

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
    struct display *p = &fb_display[unit];
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
    return(p->fb_info->setcmap(&palette_cmap, unit));
}

static int fbcon_scrolldelta(int lines)
{
#if SUPPORT_SCROLLBACK
    int unit = fg_console; /* xxx */
    struct display *p = &fb_display[unit];
    int offset;

    if (!p->can_soft_blank && console_blanked ||
	vt_cons[unit]->vc_mode != KD_TEXT || !lines ||
	p->scrollmode != SCROLL_YWRAP)
	return 0;

    fbcon_cursor(conp, CM_ERASE);

    scrollback_current -= lines;
    if (scrollback_current < 0)
	scrollback_current = 0;
    else if (scrollback_current > scrollback_max)
	scrollback_current = scrollback_max;

    offset = p->yscroll-scrollback_current;
    if (offset < 0)
	offset += p->vrows;
    else if (offset > p->vrows)
	offset -= p->vrows;
    p->var.vmode |= FB_VMODE_YWRAP;
    p->var.xoffset = 0;
    p->var.yoffset = offset*p->fontheight;
    p->fb_info->updatevar(unit);
#else
    return -ENOSYS;
#endif
}


#define LOGO_H			80
#define LOGO_W			80
#define LOGO_LINE	(LOGO_W/8)

__initfunc(static int fbcon_show_logo( void ))
{
    struct display *p = &fb_display[fg_console]; /* draw to vt in foreground */
    int depth = p->var.bits_per_pixel;
    int line = p->next_line;
    unsigned char *fb = p->screen_base;
    unsigned char *logo;
    unsigned char *dst, *src;
    int i, j, n, x1, y1;
    int logo_depth, done = 0;
	
    /* Set colors if visual is PSEUDOCOLOR and we have enough colors */
    if (p->visual == FB_VISUAL_PSEUDOCOLOR && depth >= 4) {
	int first_col = depth >= 8 ? 32 : depth > 4 ? 16 : 0;
	int num_cols = depth >= 8 ? LINUX_LOGO_COLORS : 16;
	unsigned char *red, *green, *blue;
	int old_cmap_len;
	
	if (depth >= 8) {
	    red   = linux_logo_red;
	    green = linux_logo_green;
	    blue  = linux_logo_blue;
	}
	else {
	    red   = linux_logo16_red;
	    green = linux_logo16_green;
	    blue  = linux_logo16_blue;
	}

	/* dirty trick to avoid setcmap calling kmalloc which isn't
	 * initialized yet... */
	old_cmap_len = fb_display[fg_console].cmap.len;
	fb_display[fg_console].cmap.len = 1 << depth;
	
	for( i = 0; i < num_cols; i += n ) {
	    n = num_cols - i;
	    if (n > 16)
		/* palette_cmap provides space for only 16 colors at once */
		n = 16;
	    palette_cmap.start = first_col + i;
	    palette_cmap.len   = n;
	    for( j = 0; j < n; ++j ) {
		palette_cmap.red[j]   = (red[i+j] << 8) | red[i+j];
		palette_cmap.green[j] = (green[i+j] << 8) | green[i+j];
		palette_cmap.blue[j]  = (blue[i+j] << 8) | blue[i+j];
	    }
	    p->fb_info->setcmap( &palette_cmap, fg_console );
	}
	fb_display[fg_console].cmap.len = old_cmap_len;
    }

    if (depth >= 8) {
	logo = linux_logo;
	logo_depth = 8;
    }
    else if (depth >= 4) {
	logo = linux_logo16;
	logo_depth = 4;
    }
    else {
	logo = linux_logo_bw;
	logo_depth = 1;
    }

#if defined(CONFIG_FBCON_CFB16) || defined(CONFIG_FBCON_CYBER) || \
    defined(CONFIG_FBCON_RETINAZ3)
    if ((depth % 8 == 0) && (p->visual == FB_VISUAL_TRUECOLOR ||
			     p->visual == FB_VISUAL_DIRECTCOLOR)) {
	/* Modes without color mapping, needs special data transformation... */
	unsigned long val;		/* max. depth 32! */
	int bdepth = depth/8;
	unsigned char mask[9] = { 0,0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff };
	unsigned char redmask, greenmask, bluemask;
	int redshift, greenshift, blueshift;
		
	/* Bug: Doesn't obey msb_right ... (who needs that?) */
	redmask   = mask[p->var.red.length   < 8 ? p->var.red.length   : 8];
	greenmask = mask[p->var.green.length < 8 ? p->var.green.length : 8];
	bluemask  = mask[p->var.blue.length  < 8 ? p->var.blue.length  : 8];
	redshift   = p->var.red.offset   - (8-p->var.red.length);
	greenshift = p->var.green.offset - (8-p->var.green.length);
	blueshift  = p->var.blue.offset  - (8-p->var.blue.length);

	src = logo;
	for( y1 = 0; y1 < LOGO_H; y1++ ) {
	    dst = fb + y1*line;
	    for( x1 = 0; x1 < LOGO_W; x1++, src++ ) {
		val = ((linux_logo_red[*src]   & redmask)   << redshift) |
		      ((linux_logo_green[*src] & greenmask) << greenshift) |
		      ((linux_logo_blue[*src]  & bluemask)  << blueshift);
		for( i = 0; i < bdepth; ++i )
		    *dst++ = val >> (i*8);
	    }
	}
		
	done = 1;
    }
#endif
#if defined(CONFIG_FBCON_CFB8) || defined(CONFIG_FBCON_CYBER) || \
    defined(CONFIG_FBCON_RETINAZ3)
    if (depth == 8 && p->type == FB_TYPE_PACKED_PIXELS) {
	/* depth 8 or more, packed, with color registers */
		
	src = logo;
	for( y1 = 0; y1 < LOGO_H; y1++ ) {
	    dst = fb + y1*line;
	    for( x1 = 0; x1 < LOGO_W; x1++ ) {
		*dst++ = *src++;
	    }
	}

	done = 1;
    }
#endif
#if defined(CONFIG_FBCON_AFB) || defined(CONFIG_FBCON_ILBM) || \
    defined(CONFIG_FBCON_IPLAN2P2) || defined(CONFIG_FBCON_IPLAN2P4) || \
    defined(CONFIG_FBCON_IPLAN2P8)
    if (depth >= 2 && (p->type == FB_TYPE_PLANES ||
		       p->type == FB_TYPE_INTERLEAVED_PLANES)) {
	/* planes (normal or interleaved), with color registers */
	int bit;
	unsigned char val, mask;
	int plane = p->next_plane;

	/* for support of Atari interleaved planes */
#define MAP_X(x)	(plane > line ? x : (x & ~1)*depth + (x & 1))
	/* extract a bit from the source image */
#define	BIT(p,pix,bit)	(p[pix*logo_depth/8] & \
			 (1 << ((8-((pix*logo_depth)&7)-logo_depth) + bit)))
		
	src = logo;
	for( y1 = 0; y1 < LOGO_H; y1++ ) {
	    for( x1 = 0; x1 < LOGO_LINE; x1++, src += logo_depth ) {
		dst = fb + y1*line + MAP_X(x1);
		for( bit = 0; bit < logo_depth; bit++ ) {
		    val = 0;
		    for( mask = 0x80, i = 0; i < 8; mask >>= 1, i++ ) {
			if (BIT( src, i, bit ))
			    val |= mask;
		    }
		    *dst = val;
		    dst += plane;
		}
	    }
	}
	
	/* fill remaining planes
	 * special case for logo_depth == 4: we used color registers 16..31,
	 * so fill plane 4 with 1 bits instead of 0 */
	if (depth > logo_depth) {
	    for( y1 = 0; y1 < LOGO_H; y1++ ) {
		for( x1 = 0; x1 < LOGO_LINE; x1++ ) {
		    dst = fb + y1*line + MAP_X(x1) + logo_depth*plane;
		    for( i = logo_depth; i < depth; i++, dst += plane )
			*dst = (i == logo_depth && logo_depth == 4)
			       ? 0xff : 0x00;
		}
	    }
	}
	
	done = 1;
    }
#endif
#if defined(CONFIG_FBCON_MFB) || defined(CONFIG_FBCON_AFB) || \
    defined(CONFIG_FBCON_ILBM)
    if (depth == 1) {
	/* monochrome */
	unsigned char inverse = p->inverse ? 0x00 : 0xff;

	/* can't use simply memcpy because need to apply inverse */
	for( y1 = 0; y1 < LOGO_H; y1++ ) {
	    src = logo + y1*LOGO_LINE;
	    dst = fb + y1*line;
	    for( x1 = 0; x1 < LOGO_LINE; ++x1 )
		*dst++ = *src++ ^ inverse;
	}

	done = 1;
    }
#endif
    /* Modes not yet supported: packed pixels with depth != 8 (does such a
     * thing exist in reality?) */

    return( done ? LOGO_H/p->fontheight + 1 : 0 );
}



/*
 *  The console `switch' structure for the frame buffer based console
 */

struct consw fb_con = {
    fbcon_startup, fbcon_init, fbcon_deinit, fbcon_clear, fbcon_putc,
    fbcon_putcs, fbcon_cursor, fbcon_scroll, fbcon_bmove, fbcon_switch,
    fbcon_blank, fbcon_get_font, fbcon_set_font, fbcon_set_palette,
    fbcon_scrolldelta
};


/*
 *  Driver registration
 */

static struct display_switch *drivers = NULL, *accel_drivers = NULL;

int fbcon_register_driver(struct display_switch *dispsw, int is_accel)
{
    struct display_switch **list;

    list = is_accel ? &accel_drivers : &drivers;
    dispsw->next = *list;
    *list = dispsw;
    return 0;
}

int fbcon_unregister_driver(struct display_switch *dispsw)
{
    struct display_switch **list;

    for (list = &accel_drivers; *list; list = &(*list)->next)
	if (*list == dispsw) {
	    *list = dispsw->next;
	    dispsw->next = NULL;
	    return 0;
	}
    for (list = &drivers; *list; list = &(*list)->next)
	if (*list == dispsw) {
	    *list = dispsw->next;
	    dispsw->next = NULL;
	    return 0;
	}
    return -EINVAL;
}


static struct display_switch *probe_list(struct display_switch *dispsw,
					 struct display *disp)
{
    while (dispsw) {
	if (!dispsw->open(disp))
	    return(dispsw);
	dispsw = dispsw->next;
    }
    return(NULL);
}


#ifdef CONFIG_KMOD
static void request_driver(struct display *disp, int is_accel)
{
    char modname[30];
    int len;
    const char *type;

    if (disp->var.bits_per_pixel == 1)
	type = "mfb";
    else
	switch (disp->type) {
	    case FB_TYPE_INTERLEAVED_PLANES:
		if (disp->type_aux == 2)
		    type = "iplan2p%d";
		else
		    type = "ilbm";
		break;
	    case FB_TYPE_PLANES:
		type = "afb";
		break;
	    case FB_TYPE_PACKED_PIXELS:
		type = "cfb%d";
		break;
	    default:
		return;
	}
    len = sprintf(modname, "fbcon-");
    len += sprintf(modname+len, type, disp->var.bits_per_pixel);
    if (is_accel)
	len += sprintf(modname+len, "-%d", disp->var.accel);
    request_module(modname);
}
#endif /* CONFIG_KMOD */


static struct display_switch *fbcon_get_driver(struct display *disp)
{
    struct display_switch *dispsw;

    if (disp->var.accel != FB_ACCEL_NONE) {
	/* First try an accelerated driver */
	dispsw = probe_list(accel_drivers, disp);
#ifdef CONFIG_KMOD
	if (!dispsw) {
	    request_driver(disp, 1);
	    dispsw = probe_list(accel_drivers, disp);
	}
#endif
	if (dispsw)
	    return(dispsw);
    }

    /* Then try an unaccelerated driver */
    dispsw = probe_list(drivers, disp);
#ifdef CONFIG_KMOD
    if (!dispsw) {
	request_driver(disp, 0);
	dispsw = probe_list(drivers, disp);
    }
#endif
    return(dispsw);
}


/*
 *  Dummy Low Level Operations
 */

static int open_dummy(struct display *p)
{
    if (p->line_length)
	p->next_line = p->line_length;
    else
	p->next_line = p->var.xres_virtual>>3;
    p->next_plane = 0;
    p->var.bits_per_pixel = 1;
    return 0;
}

static void misc_dummy(void) {}

static struct display_switch dispsw_dummy = {
    open_dummy,
    /* release_dummy */
    misc_dummy,
    /* bmove_dummy */
    (void (*)(struct display *, int, int, int, int, int, int))misc_dummy,
    /* clear_dummy */
    (void (*)(struct vc_data *, struct display *, int, int, int, int))misc_dummy,
    /* putc_dummy */
    (void (*)(struct vc_data *, struct display *, int, int, int))misc_dummy,
    /* putcs_dummy */
    (void (*)(struct vc_data *, struct display *, const char *, int, int, int))misc_dummy,
    /* rev_char_dummy */
    (void (*)(struct display *, int, int))misc_dummy,
};


/*
 *  Visible symbols for modules
 */

EXPORT_SYMBOL(fb_display);
EXPORT_SYMBOL(fbcon_register_driver);
EXPORT_SYMBOL(fbcon_unregister_driver);
