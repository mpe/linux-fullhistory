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
 *  Hardware cursor support added by Emmanuel Marty (core@ggi-project.org)
 *  Smart redraw scrolling added by Jakub Jelinek (jj@ultra.linux.cz)
 *
 *
 *  The low level operations for the various display memory organizations are
 *  now in separate source files.
 *
 *  Currently the following organizations are supported:
 *
 *    o afb			Amiga bitplanes
 *    o cfb{2,4,8,16,24,32}	Packed pixels
 *    o ilbm			Amiga interleaved bitplanes
 *    o iplan2p[248]		Atari interleaved bitplanes
 *    o mfb			Monochrome
 *    o vga			VGA characters/attributes
 *
 *  To do:
 *
 *    - Implement 16 plane mode (iplan2p16)
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#undef FBCONDEBUG

#define FLASHING_CURSOR		1

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/delay.h>	/* MSch: for IRQ probe */
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/smp.h>
#include <linux/init.h>

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
#ifdef CONFIG_MAC
#include <asm/macints.h>
#endif
#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/machdep.h>
#include <asm/setup.h>
#endif
#define INCLUDE_LINUX_LOGO_DATA
#include <asm/linux_logo.h>

#include "fbcon.h"
#include "fbcon-mac.h"	/* for 6x11 font on mac */
#include "font.h"

#ifdef FBCONDEBUG
#  define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

#define LOGO_H			80
#define LOGO_W			80
#define LOGO_LINE	(LOGO_W/8)

struct display fb_display[MAX_NR_CONSOLES];


/*
 * Emmanuel: fbcon will now use a hardware cursor if the
 * low-level driver provides a non-NULL dispsw->cursor pointer,
 * in which case the hardware should do blinking, etc.
 *
 * if dispsw->cursor is NULL, use Atari alike software cursor
 */

#if FLASHING_CURSOR
static int cursor_drawn = 0;

#define CURSOR_DRAW_DELAY		(2)

/* # VBL ints between cursor state changes */
#define AMIGA_CURSOR_BLINK_RATE		(20)
#define ATARI_CURSOR_BLINK_RATE		(42)
#define MAC_CURSOR_BLINK_RATE		(32)
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
    return cursor_was_drawn;
}
#endif

/*
 *  Scroll Method
 */

#define divides(a, b)	((!(a) || (b)%(a)) ? 0 : 1)


/*
 *  Interface used by the world
 */

static const char *fbcon_startup(void);
static void fbcon_init(struct vc_data *conp, int init);
static void fbcon_deinit(struct vc_data *conp);
static int fbcon_changevar(int con);
static void fbcon_clear(struct vc_data *conp, int sy, int sx, int height,
		       int width);
static void fbcon_putc(struct vc_data *conp, int c, int ypos, int xpos);
static void fbcon_putcs(struct vc_data *conp, const unsigned short *s, int count,
			int ypos, int xpos);
static void fbcon_cursor(struct vc_data *conp, int mode);
static int fbcon_scroll(struct vc_data *conp, int t, int b, int dir,
			 int count);
static void fbcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
			int height, int width);
static int fbcon_switch(struct vc_data *conp);
static int fbcon_blank(int blank);
static int fbcon_get_font(struct vc_data *conp, int *w, int *h, char *data);
static int fbcon_set_font(struct vc_data *conp, int w, int h, char *data);
static int fbcon_set_palette(struct vc_data *conp, unsigned char *table);
static int fbcon_scrolldelta(struct vc_data *conp, int lines);


/*
 *  Internal routines
 */

static void fbcon_setup(int con, int init, int logo);
static __inline__ int real_y(struct display *p, int ypos);
#if FLASHING_CURSOR
static void fbcon_vbl_handler(int irq, void *dummy, struct pt_regs *fp);
#endif
static __inline__ void updatescrollmode(struct display *p);
static __inline__ void ywrap_up(int unit, struct vc_data *conp,
				struct display *p, int count);
static __inline__ void ywrap_down(int unit, struct vc_data *conp,
				  struct display *p, int count);
static __inline__ void ypan_up(int unit, struct vc_data *conp,
			       struct display *p, int count);
static __inline__ void ypan_down(int unit, struct vc_data *conp,
				 struct display *p, int count);
static void fbcon_bmove_rec(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width, u_int y_break);

static int fbcon_show_logo(void);

#if FLASHING_CURSOR

#ifdef CONFIG_MAC
/*
 * On the Macintoy, there may or may not be a working VBL int. We need to prob
 */
static int vbl_detected = 0;

static void fbcon_vbl_detect(int irq, void *dummy, struct pt_regs *fp)
{
      vbl_detected++;
}
#endif

static void cursor_timer_handler(unsigned long dev_addr);

static struct timer_list cursor_timer = {
    NULL, NULL, 0, 0L, cursor_timer_handler
};

static void cursor_timer_handler(unsigned long dev_addr)
{
      fbcon_vbl_handler(0, NULL, NULL);
      cursor_timer.expires = jiffies+HZ/50;
      cursor_timer.data = 0;
      cursor_timer.next = cursor_timer.next = NULL;
      add_timer(&cursor_timer);
}
#endif

/*
 *  Low Level Operations
 */

static struct display_switch fbcon_dummy;

/* NOTE: fbcon cannot be __initfunc: it may be called from take_over_console later */

static const char *fbcon_startup(void)
{
    const char *display_desc = "frame buffer device";
    int irqres = 1;
    static int done = 0;

    /*
     *  If num_registered_fb is zero, this is a call for the dummy part.
     *  The frame buffer devices weren't initialized yet.
     */
    if (!num_registered_fb || done)
	return display_desc;
    done = 1;

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

#ifdef CONFIG_MAC
    /*
     * On a Macintoy, the VBL interrupt may or may not be active. 
     * As interrupt based cursor is more reliable and race free, we 
     * probe for VBL interrupts.
     */
    if (MACH_IS_MAC) {
       int ct = 0;
       /*
        * Probe for VBL: set temp. handler ...
        */
       irqres = request_irq(IRQ_MAC_VBL, fbcon_vbl_detect, 0,
                            "console/cursor", fbcon_vbl_detect);
       /*
        * ... and spin for 20 ms ...
        */
       while (!vbl_detected && ++ct<1000)
          udelay(20);
 
       if(ct==1000)
          printk("fbcon_startup: No VBL detected, using timer based cursor.\n");
 
       if (vbl_detected) {
         /*
          * interrupt based cursor ok
          */
          cursor_blink_rate = MAC_CURSOR_BLINK_RATE;
          irqres = request_irq(IRQ_MAC_VBL, fbcon_vbl_handler, 0,
                               "console/cursor", fbcon_vbl_handler);
       } else {
          /*
           * VBL not detected: fall through, use timer based cursor
           */
           irqres = 1;
	   /* free interrupt here ?? */
       }
    }
#endif /* CONFIG_MAC */

#if defined(__arm__) && defined(IRQ_VSYNCPULSE)
    irqres = request_irq(IRQ_VSYNCPULSE, fbcon_vbl_handler, 0,
			 "console/cursor", fbcon_vbl_handler);
#endif

    if (irqres) {
	cursor_blink_rate = DEFAULT_CURSOR_BLINK_RATE;
	cursor_timer.expires = jiffies+HZ/50;
	cursor_timer.data = 0;
	cursor_timer.next = cursor_timer.prev = NULL;
	add_timer(&cursor_timer);
    }

    return display_desc;
}


static void fbcon_init(struct vc_data *conp, int init)
{
    int unit = conp->vc_num;
    struct fb_info *info;

    /* on which frame buffer will we open this console? */
    info = registered_fb[(int)con2fb_map[unit]];

    info->changevar = &fbcon_changevar;
    fb_display[unit] = *(info->disp);	/* copy from default */
    DPRINTK("mode:   %s\n",info->modename);
    DPRINTK("visual: %d\n",fb_display[unit].visual);
    DPRINTK("res:    %dx%d-%d\n",fb_display[unit].var.xres,
	                     fb_display[unit].var.yres,
	                     fb_display[unit].var.bits_per_pixel);
    fb_display[unit].conp = conp;
    fb_display[unit].fb_info = info;
    fbcon_setup(unit, init, !init);
}


static void fbcon_deinit(struct vc_data *conp)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    p->dispsw = NULL;
    p->conp = 0;
}


static int fbcon_changevar(int con)
{
    if (fb_display[con].conp)
	    fbcon_setup(con, 0, 0);
    return 0;
}


static __inline__ void updatescrollmode(struct display *p)
{
    if (p->scrollmode == SCROLL_YREDRAW)
    	return;
    if (divides(p->ywrapstep, p->fontheight) &&
	divides(p->fontheight, p->var.yres_virtual))
	p->scrollmode = SCROLL_YWRAP;
    else if (divides(p->ypanstep, p->fontheight) &&
	     p->var.yres_virtual >= p->var.yres+p->fontheight)
	p->scrollmode = SCROLL_YPAN;
    else
	p->scrollmode = SCROLL_YMOVE;
}


static void fbcon_setup(int con, int init, int logo)
{
    struct display *p = &fb_display[con];
    struct vc_data *conp = p->conp;
    int nr_rows, nr_cols;
    int old_rows, old_cols;
    /* Only if not module */
    extern int initmem_freed;
    
    if (con != fg_console || initmem_freed || p->type == FB_TYPE_TEXT)
    	logo = 0;

    p->var.xoffset = p->var.yoffset = p->yscroll = 0;  /* reset wrap/pan */

    if (!p->fb_info->fontname[0] ||
	!findsoftfont(p->fb_info->fontname, &p->fontwidth, &p->fontheight,
		      &p->fontdata) || p->fontwidth != 8)
	getdefaultfont(p->var.xres, p->var.yres, NULL, &p->fontwidth,
		       &p->fontheight, &p->fontdata);
    if (p->fontwidth != 8) {
#ifdef CONFIG_MAC
	if (MACH_IS_MAC)
	    /* ++Geert: hack to make 6x11 fonts work on mac */
	    p->dispsw = &fbcon_mac;
	else
#endif
	{
	    /* ++Geert: changed from panic() to `correct and continue' */
	    printk(KERN_ERR "fbcon_setup: No support for fontwidth != 8");
	    p->dispsw = &fbcon_dummy;
	}
    }
    updatescrollmode(p);
    
    if (logo) {
    	/* Need to make room for the logo */
    	int logo_lines = (LOGO_H + p->fontheight - 1) / p->fontheight;
    	unsigned short *q = (unsigned short *)(conp->vc_origin + 
    				conp->vc_size_row * conp->vc_rows);
	unsigned short *r;
    
    	for (r = q - logo_lines * conp->vc_cols; r < q; r++)
    	    if (*r != conp->vc_video_erase_char)
    	    	break;
    	if (r == q) {
    	    /* We can scroll screen down */
    	    int cnt;
    	    int step = logo_lines * conp->vc_cols;

	    r = q - step - conp->vc_cols;
    	    for (cnt = conp->vc_rows - logo_lines; cnt > 0; cnt--) {
    	    	scr_memcpyw(r + step, r, conp->vc_size_row);
    	    	r -= conp->vc_cols;
    	    }
    	    conp->vc_y += logo_lines;
    	    conp->vc_pos += logo_lines * conp->vc_size_row;
    	}
    	scr_memsetw((unsigned short *)conp->vc_origin, conp->vc_video_erase_char, 
    		conp->vc_size_row * logo_lines);
    }
    
    old_cols = conp->vc_cols;
    old_rows = conp->vc_rows;
    
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

    if (!p->dispsw) {
	printk(KERN_WARNING "fbcon_setup: type %d (aux %d, depth %d) not "
	       "supported\n", p->type, p->type_aux, p->var.bits_per_pixel);
	p->dispsw = &fbcon_dummy;
    }
    p->dispsw->setup(p);

    p->fgcol = p->var.bits_per_pixel > 2 ? 7 : (1<<p->var.bits_per_pixel)-1;
    p->bgcol = 0;

    if (!init) {
	vc_resize_con(nr_rows, nr_cols, con);
	if (con == fg_console &&
	    old_rows == nr_rows && old_cols == nr_cols)
	    update_screen(con); /* So that we set origin correctly */
    }
	
    if (logo)
    	fbcon_show_logo(); /* This is protected above by initmem_freed */
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
    return ypos < rows ? ypos : ypos-rows;
}


static void fbcon_clear(struct vc_data *conp, int sy, int sx, int height,
			int width)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];
    u_int y_break;

    if (!p->can_soft_blank && console_blanked)
	return;

    if (!height || !width)
	return;

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
}


static void fbcon_putc(struct vc_data *conp, int c, int ypos, int xpos)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    if (!p->can_soft_blank && console_blanked)
	    return;

    if ((p->cursor_x == xpos) && (p->cursor_y == ypos))
	    CURSOR_UNDRAWN();

    p->dispsw->putc(conp, p, c, real_y(p, ypos), xpos);
}


static void fbcon_putcs(struct vc_data *conp, const unsigned short *s, int count,
		       int ypos, int xpos)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    if (!p->can_soft_blank && console_blanked)
	    return;

    if ((p->cursor_y == ypos) && (xpos <= p->cursor_x) &&
	(p->cursor_x < (xpos + count)))
	    CURSOR_UNDRAWN();
    p->dispsw->putcs(conp, p, s, count, real_y(p, ypos), xpos);
}


static void fbcon_cursor(struct vc_data *conp, int mode)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    /* do we have a hardware cursor ? */
    if (p->dispsw->cursor) {
	p->cursor_x = conp->vc_x;
	p->cursor_y = conp->vc_y;
	p->dispsw->cursor(p, mode, p->cursor_x, real_y(p, p->cursor_y));
	return;
    }

    /* Avoid flickering if there's no real change. */
    if (p->cursor_x == conp->vc_x && p->cursor_y == conp->vc_y &&
	(mode == CM_ERASE) == !cursor_on)
	return;

	cursor_on = 0;
	if (cursor_drawn)
	    p->dispsw->revc(p, p->cursor_x, real_y(p, p->cursor_y));

	p->cursor_x = conp->vc_x;
	p->cursor_y = conp->vc_y;

	switch (mode) {
	    case CM_ERASE:
	        cursor_drawn = 0;
	        break;
	    case CM_MOVE:
	    case CM_DRAW:
		if (cursor_drawn)
		    p->dispsw->revc(p, p->cursor_x, real_y(p, p->cursor_y));
		vbl_cursor_cnt = CURSOR_DRAW_DELAY;
		cursor_on = 1;
		break;
	}
}


#if FLASHING_CURSOR
static void fbcon_vbl_handler(int irq, void *dummy, struct pt_regs *fp)
{
    struct display *p;
    static int _vbl_cursor_cnt = 1, _vbl_cursor_drawn;

    if (!--_vbl_cursor_cnt) {
	_vbl_cursor_cnt = cursor_blink_rate;
	_vbl_cursor_drawn = !_vbl_cursor_drawn;
    }

    if (cursor_on && vbl_cursor_cnt) {
	if (cursor_drawn != _vbl_cursor_drawn) {
	    p = &fb_display[fg_console];
	    p->dispsw->revc(p, p->cursor_x, real_y(p, p->cursor_y));
	}
	cursor_drawn = _vbl_cursor_drawn;
    }
}
#endif

static int scrollback_phys_max = 0;
static int scrollback_max = 0;
static int scrollback_current = 0;

static __inline__ void ywrap_up(int unit, struct vc_data *conp,
				struct display *p, int count)
{
    p->yscroll += count;
    if (p->yscroll >= p->vrows)	/* Deal with wrap */
	p->yscroll -= p->vrows;
    p->var.xoffset = 0;
    p->var.yoffset = p->yscroll*p->fontheight;
    p->var.vmode |= FB_VMODE_YWRAP;
    p->fb_info->updatevar(unit, p->fb_info);
    scrollback_max += count;
    if (scrollback_max > scrollback_phys_max)
	scrollback_max = scrollback_phys_max;
    scrollback_current = 0;
}


static __inline__ void ywrap_down(int unit, struct vc_data *conp,
				  struct display *p, int count)
{
    p->yscroll -= count;
    if (p->yscroll < 0)		/* Deal with wrap */
	p->yscroll += p->vrows;
    p->var.xoffset = 0;
    p->var.yoffset = p->yscroll*p->fontheight;
    p->var.vmode |= FB_VMODE_YWRAP;
    p->fb_info->updatevar(unit, p->fb_info);
    scrollback_max -= count;
    if (scrollback_max < 0)
	scrollback_max = 0;
    scrollback_current = 0;
}


static __inline__ void ypan_up(int unit, struct vc_data *conp,
			       struct display *p, int count)
{
    p->yscroll += count;
    if (p->yscroll > p->vrows-conp->vc_rows) {
	p->dispsw->bmove(p, p->vrows-conp->vc_rows, 0, 0, 0,
			 conp->vc_rows, conp->vc_cols);
	p->yscroll -= p->vrows-conp->vc_rows;
    }
    p->var.xoffset = 0;
    p->var.yoffset = p->yscroll*p->fontheight;
    p->var.vmode &= ~FB_VMODE_YWRAP;
    p->fb_info->updatevar(unit, p->fb_info);
    scrollback_max += count;
    if (scrollback_max > scrollback_phys_max)
	scrollback_max = scrollback_phys_max;
    scrollback_current = 0;
}


static __inline__ void ypan_down(int unit, struct vc_data *conp,
				 struct display *p, int count)
{
    p->yscroll -= count;
    if (p->yscroll < 0) {
	p->dispsw->bmove(p, 0, 0, p->vrows-conp->vc_rows, 0,
			 conp->vc_rows, conp->vc_cols);
	p->yscroll += p->vrows-conp->vc_rows;
    }
    p->var.xoffset = 0;
    p->var.yoffset = p->yscroll*p->fontheight;
    p->var.vmode &= ~FB_VMODE_YWRAP;
    p->fb_info->updatevar(unit, p->fb_info);
    scrollback_max -= count;
    if (scrollback_max < 0)
	scrollback_max = 0;
    scrollback_current = 0;
}


static void fbcon_redraw(struct vc_data *conp, struct display *p, 
			 int line, int count, int offset)
{
    unsigned short *d = (unsigned short *)
	(conp->vc_origin + conp->vc_size_row * line);
    unsigned short *s = d + offset;
    while (count--) {
	unsigned short *start = s;
	unsigned short *le = (unsigned short *)
	    ((unsigned long)s + conp->vc_size_row);
	unsigned short c;
	int x = 0;
	unsigned short attr = 1;

	do {
	    c = scr_readw(s);
	    if (attr != (c & 0xff00)) {
		attr = c & 0xff00;
		if (s > start) {
		    p->dispsw->putcs(conp, p, start, s - start, line, x);
		    x += s - start;
		    start = s;
		}
	    }
	    if (c == scr_readw(d)) {
	    	if (s > start) {
	    	    p->dispsw->putcs(conp, p, start, s - start, line, x);
		    x += s - start + 1;
		    start = s + 1;
	    	} else {
	    	    x++;
	    	    start++;
	    	}
	    }
	    scr_writew(c, d);
	    s++;
	    d++;
	} while (s < le);
	if (s > start)
	    p->dispsw->putcs(conp, p, start, s - start, line, x);
	if (offset > 0)
		line++;
	else {
		line--;
		/* NOTE: We subtract two lines from these pointers */
		s -= conp->vc_size_row;
		d -= conp->vc_size_row;
	}
    }
}


static int fbcon_scroll(struct vc_data *conp, int t, int b, int dir,
			int count)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];
    int is_txt = (p->type == FB_TYPE_TEXT);

    if (!p->can_soft_blank && console_blanked)
	return 0;

    if (!count)
	return 0;

    fbcon_cursor(conp, CM_ERASE);

    /*
     * ++Geert: Only use ywrap/ypan if the console is in text mode
     * ++Andrew: Only use ypan on hardware text mode when scrolling the
     *           whole screen (prevents flicker).
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
			    ywrap_up(unit, conp, p, count);
			    if (conp->vc_rows-b > 0)
				fbcon_bmove(conp, b-count, 0, b, 0,
					    conp->vc_rows-b, conp->vc_cols);
			} else
			    fbcon_bmove(conp, t+count, 0, t, 0, b-t-count,
					conp->vc_cols);
			fbcon_clear(conp, b-count, 0, count, conp->vc_cols);
			break;

		    case SCROLL_YPAN:
			if (( is_txt && (b-t == conp->vc_rows)) ||
			    (!is_txt && (b-t-count > 3*conp->vc_rows>>2))) {
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
		    case SCROLL_YREDRAW:
		    	fbcon_redraw(conp, p, t, b-t-count, count*conp->vc_cols);
		    	p->dispsw->clear(conp, p, b-count, 0, count,
		    			 conp->vc_cols);
		    	scr_memsetw((unsigned short *)(conp->vc_origin + 
		    		    conp->vc_size_row * (b-count)), 
		    		    conp->vc_video_erase_char,
		    		    conp->vc_size_row * count);
		    	return 1;
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
			    ywrap_down(unit, conp, p, count);
			    if (t > 0)
				fbcon_bmove(conp, count, 0, 0, 0, t,
					    conp->vc_cols);
			} else
			    fbcon_bmove(conp, t, 0, t+count, 0, b-t-count,
					conp->vc_cols);
			fbcon_clear(conp, t, 0, count, conp->vc_cols);
			break;

		    case SCROLL_YPAN:
			if (( is_txt && (b-t == conp->vc_rows)) ||
			    (!is_txt && (b-t-count > 3*conp->vc_rows>>2))) {
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
			
		    case SCROLL_YREDRAW:
			fbcon_redraw(conp, p, b - 1, b-t-count, -count*conp->vc_cols);
			p->dispsw->clear(conp, p, t, 0, count, conp->vc_cols);
		    	scr_memsetw((unsigned short *)(conp->vc_origin + 
		    		    conp->vc_size_row * t), 
		    		    conp->vc_video_erase_char,
		    		    conp->vc_size_row * count);
		    	return 1;
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
    return 0;
}


static void fbcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
			int height, int width)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];

    if (!p->can_soft_blank && console_blanked)
	return;

    if (!width || !height)
	return;

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

    p->var.yoffset = p->yscroll*p->fontheight;
    switch (p->scrollmode) {
	case SCROLL_YWRAP:
	    scrollback_phys_max = p->vrows-conp->vc_rows;
	    break;
	case SCROLL_YPAN:
	    scrollback_phys_max = p->vrows-2*conp->vc_rows;
	    if (scrollback_phys_max < 0)
		scrollback_phys_max = 0;
	    break;
	default:
	    scrollback_phys_max = 0;
	    break;
    }
    scrollback_max = 0;
    scrollback_current = 0;

    if (info && info->switch_con)
	(*info->switch_con)(conp->vc_num, info);
    return 1;
}


static int fbcon_blank(int blank)
{
    struct display *p = &fb_display[fg_console];
    struct fb_info *info = p->fb_info;

    fbcon_cursor(p->conp, blank ? CM_ERASE : CM_DRAW);

    if (!p->can_soft_blank) {
	if (blank) {
#ifdef CONFIG_MAC
	    if (MACH_IS_MAC) {
		if (p->screen_base)
		    mymemset(p->screen_base,
			     p->var.xres_virtual*p->var.yres_virtual*
			     p->var.bits_per_pixel>>3);
	    } else
#endif
	    if (p->visual == FB_VISUAL_MONO01) {
		if (p->screen_base)
		    mymemset(p->screen_base,
			     p->var.xres_virtual*p->var.yres_virtual*
			     p->var.bits_per_pixel>>3);
	     } else
		 p->dispsw->clear(p->conp, p, 0, 0, p->conp->vc_rows, p->conp->vc_cols);
	    return 0;
	} else {
	    /* Tell console.c that it has to restore the screen itself */
	    return 1;
	}
    }
    (*info->blank)(blank, info);
    return 0;
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
	return -ENAMETOOLONG;

    for (i = 0; i < 256; i++)
	for (j = 0; j < p->fontheight; j++)
	    data[i*32+j] = p->fontdata[i*p->fontheight+j];
    return 0;
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

	if (!findsoftfont( name, &w, &h, (u8 **)&data ))
	    return -ENOENT;
	userspace = 0;
    } else if (w == 1) {
	/* copy font from some other console in 'h'*/
	struct display *op;

	if (h < 0 || !vc_cons_allocated( h ))
	    return -ENOTTY;
	if (h == unit)
	    return 0; /* nothing to do */
	op = &fb_display[h];
	if (op->fontdata == p->fontdata)
	    return 0; /* already the same font... */

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
	return -ENXIO;

    resize = (w != p->fontwidth) || (h != p->fontheight);
    size = (w+7)/8 * h * 256;

    if (p->userfont)
	old_data = p->fontdata;

    if (userspace) {
	if (!(new_data = kmalloc( sizeof(int)+size, GFP_USER )))
	    return -ENOMEM;
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

    return 0;
}

static u16 palette_red[16];
static u16 palette_green[16];
static u16 palette_blue[16];

static struct fb_cmap palette_cmap  = {
    0, 16, palette_red, palette_green, palette_blue, NULL
};

static int fbcon_set_palette(struct vc_data *conp, unsigned char *table)
{
    int unit = conp->vc_num;
    struct display *p = &fb_display[unit];
    int i, j, k;
    u8 val;

    if (!conp->vc_can_do_color || (!p->can_soft_blank && console_blanked))
	return -EINVAL;
    for (i = j = 0; i < 16; i++) {
	k = table[i];
	val = conp->vc_palette[j++];
	palette_red[k] = (val<<8)|val;
	val = conp->vc_palette[j++];
	palette_green[k] = (val<<8)|val;
	val = conp->vc_palette[j++];
	palette_blue[k] = (val<<8)|val;
    }
    if (p->var.bits_per_pixel <= 4)
	palette_cmap.len = 1<<p->var.bits_per_pixel;
    else
	palette_cmap.len = 16;
    return p->fb_info->fbops->fb_set_cmap(&palette_cmap, 1, unit, p->fb_info);
}

static int fbcon_scrolldelta(struct vc_data *conp, int lines)
{
    int unit, offset, limit, scrollback_old;
    struct display *p;

    /* FIXME: Sync to new code, remember to set visible_origin */

    if (!scrollback_phys_max)
	return -ENOSYS;

    scrollback_old = scrollback_current;
    scrollback_current -= lines;
    if (scrollback_current < 0)
	scrollback_current = 0;
    else if (scrollback_current > scrollback_max)
	scrollback_current = scrollback_max;
    if (scrollback_current == scrollback_old)
	return 0;

    unit = fg_console;
    p = &fb_display[unit];
    if (!p->can_soft_blank &&
	(console_blanked || vt_cons[unit]->vc_mode != KD_TEXT || !lines))
	return 0;
    fbcon_cursor(conp, CM_ERASE);

    offset = p->yscroll-scrollback_current;
    limit = p->vrows;
    switch (p->scrollmode) {
	case SCROLL_YWRAP:
	    p->var.vmode |= FB_VMODE_YWRAP;
	    break;
	case SCROLL_YPAN:
	    limit -= conp->vc_rows;
	    p->var.vmode &= ~FB_VMODE_YWRAP;
	    break;
    }
    if (offset < 0)
	offset += limit;
    else if (offset >= limit)
	offset -= limit;
    p->var.xoffset = 0;
    p->var.yoffset = offset*p->fontheight;
    p->fb_info->updatevar(unit, p->fb_info);
    return 0;
}


__initfunc(static int fbcon_show_logo( void ))
{
    struct display *p = &fb_display[fg_console]; /* draw to vt in foreground */
    int depth = p->var.bits_per_pixel;
    int line = p->next_line;
    unsigned char *fb = p->screen_base;
    unsigned char *logo;
    unsigned char *dst, *src;
    int i, j, n, x1, y1, x;
    int logo_depth, done = 0;

    /* Return if the frame buffer is not mapped */
    if (!fb)
	return 0;

    /* Set colors if visual is PSEUDOCOLOR and we have enough colors, or for
     * TRUECOLOR */
    if ((p->visual == FB_VISUAL_PSEUDOCOLOR && depth >= 4) ||
	p->visual == FB_VISUAL_TRUECOLOR) {
	int is_truecolor = (p->visual == FB_VISUAL_TRUECOLOR);
	int use_256 = (!is_truecolor && depth >= 8) ||
		      (is_truecolor && depth >= 24);
	int first_col = use_256 ? 32 : depth > 4 ? 16 : 0;
	int num_cols = use_256 ? LINUX_LOGO_COLORS : 16;
	unsigned char *red, *green, *blue;
	int old_cmap_len;
	
	if (use_256) {
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
	fb_display[fg_console].cmap.len = 1 << (depth/(is_truecolor ? 3 : 1));
	
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
	    p->fb_info->fbops->fb_set_cmap(&palette_cmap, 1, fg_console,
					   p->fb_info);
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

    for (x = 0; x < smp_num_cpus * (LOGO_W + 8) &&
    	 x < p->var.xres - (LOGO_W + 8); x += (LOGO_W + 8)) {
    	 
#if defined(CONFIG_FBCON_CFB16) || defined(CONFIG_FBCON_CFB24) || \
    defined(CONFIG_FBCON_CFB32) || defined(CONFIG_FB_SBUS)
        if (p->visual == FB_VISUAL_TRUECOLOR) {
	    unsigned int val;		/* max. depth 32! */
	    int bdepth;
	    int redshift, greenshift, blueshift;
		
	    /* Bug: Doesn't obey msb_right ... (who needs that?) */
	    redshift   = p->var.red.offset;
	    greenshift = p->var.green.offset;
	    blueshift  = p->var.blue.offset;

	    if (depth >= 24 && (depth % 8) == 0) {
		/* have at least 8 bits per color */
		src = logo;
		bdepth = depth/8;
		for( y1 = 0; y1 < LOGO_H; y1++ ) {
		    dst = fb + y1*line + x*bdepth;
		    for( x1 = 0; x1 < LOGO_W; x1++, src++ ) {
			val = (*src << redshift) |
			      (*src << greenshift) |
			      (*src << blueshift);
#ifdef __LITTLE_ENDIAN
			for( i = 0; i < bdepth; ++i )
#else
			for( i = bdepth-1; i >= 0; --i )
#endif
			    *dst++ = val >> (i*8);
		    }
		}
	    }
	    else if (depth >= 15 && depth <= 23) {
	        /* have 5..7 bits per color, using 16 color image */
		unsigned int pix;
		src = linux_logo16;
		bdepth = (depth+7)/8;
		for( y1 = 0; y1 < LOGO_H; y1++ ) {
		    dst = fb + y1*line + x*bdepth;
		    for( x1 = 0; x1 < LOGO_W/2; x1++, src++ ) {
			pix = (*src >> 4) | 0x10; /* upper nibble */
			val = (pix << redshift) |
			      (pix << greenshift) |
			      (pix << blueshift);
			for( i = 0; i < bdepth; ++i )
			    *dst++ = val >> (i*8);
			pix = (*src & 0x0f) | 0x10; /* lower nibble */
			val = (pix << redshift) |
			      (pix << greenshift) |
			      (pix << blueshift);
			for( i = bdepth-1; i >= 0; --i )
			    *dst++ = val >> (i*8);
		    }
		}
	    }
	    done = 1;
        }
#endif
#if defined(CONFIG_FBCON_CFB16) || defined(CONFIG_FBCON_CFB24) || \
    defined(CONFIG_FBCON_CFB32) || defined(CONFIG_FB_SBUS)
	if ((depth % 8 == 0) && (p->visual == FB_VISUAL_DIRECTCOLOR)) {
	    /* Modes without color mapping, needs special data transformation... */
	    unsigned int val;		/* max. depth 32! */
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
		dst = fb + y1*line + x*bdepth;
		for( x1 = 0; x1 < LOGO_W; x1++, src++ ) {
		    val = ((linux_logo_red[*src-32]   & redmask)   << redshift) |
		          ((linux_logo_green[*src-32] & greenmask) << greenshift) |
		          ((linux_logo_blue[*src-32]  & bluemask)  << blueshift);
#ifdef __LITTLE_ENDIAN
		    for( i = 0; i < bdepth; ++i )
#else
		    for( i = bdepth-1; i >= 0; --i )
#endif
			*dst++ = val >> (i*8);
		}
	    }
	    done = 1;
	}
#endif
#if defined(CONFIG_FBCON_CFB8) || defined(CONFIG_FB_SBUS)
	if (depth == 8 && p->type == FB_TYPE_PACKED_PIXELS) {
	    /* depth 8 or more, packed, with color registers */
		
	    src = logo;
	    for( y1 = 0; y1 < LOGO_H; y1++ ) {
		dst = fb + y1*line + x;
		for( x1 = 0; x1 < LOGO_W; x1++ )
		    *dst++ = *src++;
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
	    break;
	}
#endif
#if defined(CONFIG_FBCON_MFB) || defined(CONFIG_FBCON_AFB) || \
    defined(CONFIG_FBCON_ILBM)

	if (depth == 1 && (p->type == FB_TYPE_PACKED_PIXELS ||
			   p->type == FB_TYPE_PLANES ||
			   p->type == FB_TYPE_INTERLEAVED_PLANES)) {

	    /* monochrome */
	    unsigned char inverse = p->inverse ? 0x00 : 0xff;

	    /* can't use simply memcpy because need to apply inverse */
	    for( y1 = 0; y1 < LOGO_H; y1++ ) {
		src = logo + y1*LOGO_LINE + x/8;
		dst = fb + y1*line;
		for( x1 = 0; x1 < LOGO_LINE; ++x1 )
		    *dst++ = *src++ ^ inverse;
	    }
	    done = 1;
	}
#endif
    }
    
    /* Modes not yet supported: packed pixels with depth != 8 (does such a
     * thing exist in reality?) */

    return done ? (LOGO_H + p->fontheight - 1) / p->fontheight : 0 ;
}

/*
 *  The console `switch' structure for the frame buffer based console
 */
 
struct consw fb_con = {
    con_startup: 	fbcon_startup, 
    con_init: 		fbcon_init,
    con_deinit: 	fbcon_deinit,
    con_clear: 		fbcon_clear,
    con_putc: 		fbcon_putc,
    con_putcs: 		fbcon_putcs,
    con_cursor: 	fbcon_cursor,
    con_scroll: 	fbcon_scroll,
    con_bmove: 		fbcon_bmove,
    con_switch: 	fbcon_switch,
    con_blank: 		fbcon_blank,
    con_get_font: 	fbcon_get_font,
    con_set_font: 	fbcon_set_font,
    con_set_palette: 	fbcon_set_palette,
    con_scrolldelta: 	fbcon_scrolldelta,
    con_set_origin: 	NULL,
    con_save_screen: 	NULL,
};


/*
 *  Dummy Low Level Operations
 */

static void fbcon_dummy_op(void) {}

static struct display_switch fbcon_dummy = {
    (void *)fbcon_dummy_op,	/* fbcon_dummy_setup */
    (void *)fbcon_dummy_op,	/* fbcon_dummy_bmove */
    (void *)fbcon_dummy_op,	/* fbcon_dummy_clear */
    (void *)fbcon_dummy_op,	/* fbcon_dummy_putc */
    (void *)fbcon_dummy_op,	/* fbcon_dummy_putcs */
    (void *)fbcon_dummy_op,	/* fbcon_dummy_revc */
    NULL,			/* fbcon_dummy_cursor */
};


/*
 *  Visible symbols for modules
 */

EXPORT_SYMBOL(fb_display);
