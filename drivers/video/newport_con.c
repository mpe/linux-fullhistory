/* $Id: newport_con.c,v 1.3 1998/09/01 21:43:18 tsbogend Exp $
 *
 * newport_con.c: Abscon for newport hardware
 * 
 * (C) 1998 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 * 
 * This driver is based on sgicons.c and cons_newport.
 * 
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/kd.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/vt_kern.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/newport.h>

struct newport_regs *npregs;
int newport_num_lines;
int newport_num_columns;
int topscan;

extern unsigned char vga_font[];

#define BMASK(c) (c << 24)

#define RENDER(regs, cp) do { \
(regs)->go.zpattern = BMASK((cp)[0x0]); (regs)->go.zpattern = BMASK((cp)[0x1]); \
(regs)->go.zpattern = BMASK((cp)[0x2]); (regs)->go.zpattern = BMASK((cp)[0x3]); \
(regs)->go.zpattern = BMASK((cp)[0x4]); (regs)->go.zpattern = BMASK((cp)[0x5]); \
(regs)->go.zpattern = BMASK((cp)[0x6]); (regs)->go.zpattern = BMASK((cp)[0x7]); \
(regs)->go.zpattern = BMASK((cp)[0x8]); (regs)->go.zpattern = BMASK((cp)[0x9]); \
(regs)->go.zpattern = BMASK((cp)[0xa]); (regs)->go.zpattern = BMASK((cp)[0xb]); \
(regs)->go.zpattern = BMASK((cp)[0xc]); (regs)->go.zpattern = BMASK((cp)[0xd]); \
(regs)->go.zpattern = BMASK((cp)[0xe]); (regs)->go.zpattern = BMASK((cp)[0xf]); \
} while(0)

#define TESTVAL 0xdeadbeef
#define XSTI_TO_FXSTART(val) (((val) & 0xffff) << 11)

static inline void newport_render_background(int xpos, int ypos, int ci)
{
    newport_wait();
    npregs->set.wrmask = 0xffffffff;
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			     NPORT_DMODE0_STOPY);
    npregs->set.colori = ci;
    npregs->set.xystarti = (xpos << 16) | ((ypos + topscan) & 0x3ff);
    npregs->go.xyendi = ((xpos + 7) << 16) | ((ypos + topscan + 15) & 0x3ff);
}

static inline void newport_init_cmap(void)
{
    unsigned short i;
    
    for(i = 0; i < 16; i++) {
	newport_bfwait();
	newport_cmap_setaddr(npregs, color_table[i]);
	newport_cmap_setrgb(npregs,
			    default_red[i],
			    default_grn[i],
			    default_blu[i]);
    }
}

static inline void newport_clear_screen(int xstart, int ystart, int xend, int yend)
{
    newport_wait();
    npregs->set.wrmask = 0xffffffff;
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			     NPORT_DMODE0_STOPY);
    npregs->set.colori = 0;
    npregs->set.xystarti = (xstart << 16) | ystart;
    npregs->go.xyendi = (xend << 16) | yend;
}

static inline void newport_clear_lines(int ystart, int yend)
{
    ystart = ((ystart << 4) + topscan) & 0x3ff;
    yend = ((yend << 4) + topscan + 15) & 0x3ff;    
    newport_clear_screen (0, ystart, 1279, yend);
}

void newport_reset (void)
{
    unsigned short treg;    
    int i;
    
    newport_wait();
    treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
    newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_EVIDEO));

    treg = newport_vc2_get(npregs, VC2_IREG_CENTRY);
    newport_vc2_set(npregs, VC2_IREG_RADDR, treg);
    npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
			   NPORT_DMODE_W2 | VC2_PROTOCOL);
    for(i = 0; i < 128; i++) {
	newport_bfwait();
	if (i == 92 || i == 94)
	    npregs->set.dcbdata0.hwords.s1 = 0xff00;
	else
	    npregs->set.dcbdata0.hwords.s1 = 0x0000;
    }

    newport_init_cmap();
    npregs->cset.topscan = topscan = 0;
    npregs->cset.xywin = (4096 << 16) | 4096;
    /* Clear the screen. */
    newport_clear_screen(0,0,1280+63,1024);
}

__initfunc(static const char *newport_startup(void))
{
    struct newport_regs *p;

    npregs = (struct newport_regs *) (KSEG1 + 0x1f0f0000);
	
    p = npregs;
    p->cset.config = NPORT_CFG_GD0;

    if(newport_wait()) {
	return NULL;
    }

    p->set.xstarti = TESTVAL; if(p->set._xstart.i != XSTI_TO_FXSTART(TESTVAL)) {
	return NULL;
    }

    newport_reset ();

    // gfx_init (display_desc);
    newport_num_lines = ORIG_VIDEO_LINES;
    newport_num_columns = ORIG_VIDEO_COLS;
    
    return "SGI Newport";
}

static void newport_init(struct vc_data *vc, int init)
{
    vc->vc_cols = newport_num_columns;
    vc->vc_rows = newport_num_lines;
    vc->vc_can_do_color = 1;
}

static void newport_clear(struct vc_data *vc, int sy, int sx, int height, int width)
{
    int xend = ((sx + width) << 3) - 1;
    int ystart = ((sy << 4) + topscan) & 0x3ff;
    int yend = (((sy + height) << 4) + topscan - 1) & 0x3ff;
    
    if (ystart < yend) {
	newport_clear_screen(sx << 3, ystart, xend, yend);
    } else {
	newport_clear_screen(sx << 3, ystart, xend, 1023);
	newport_clear_screen(sx << 3, 0, xend, yend);
    }
}

static void newport_putc(struct vc_data *vc, int charattr, int ypos, int xpos)
{
    unsigned char *p;
    
    p = &vga_font[(charattr & 0xff) << 4];
    charattr = (charattr >> 8) & 0xff;
    xpos <<= 3;
    ypos <<= 4;

    newport_render_background(xpos, ypos, (charattr & 0xf0) >> 4);
    
    /* Set the color and drawing mode. */
    newport_wait();
    npregs->set.colori = charattr & 0xf;
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
			     NPORT_DMODE0_L32);
    
    /* Set coordinates for bitmap operation. */
    npregs->set.xystarti = (xpos << 16) | ((ypos + topscan) & 0x3ff);
    npregs->set.xyendi = ((xpos + 7) << 16);
    newport_wait();
    
    /* Go, baby, go... */
    RENDER(npregs, p);
}

static void newport_putcs(struct vc_data *vc, const unsigned short *s, int count,
			  int ypos, int xpos)
{
    while (count--)
	newport_putc (vc, *s++, ypos, xpos++);
}

static void newport_cursor(struct vc_data *vc, int mode)
{
    unsigned short treg;
    int xcurs, ycurs;
    
    switch (mode) {
     case CM_ERASE:
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg & ~(VC2_CTRL_ECDISP)));
	break;

     case CM_MOVE:
     case CM_DRAW:
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_ECDISP));
	xcurs = (vc->vc_pos - vc->vc_visible_origin) / 2;
	ycurs = ((xcurs / vc->vc_cols) << 4) + 31;
	xcurs = ((xcurs % vc->vc_cols) << 3) + 21;
	newport_vc2_set(npregs, VC2_IREG_CURSX, xcurs);
	newport_vc2_set(npregs, VC2_IREG_CURSY, ycurs);
    }
}

static int newport_switch(struct vc_data *vc)
{
    npregs->cset.topscan = topscan = 0;
    return 1;
}

static int newport_blank(struct vc_data *c, int blank)
{
    unsigned short treg;
    
    if (blank == 0) {
	/* unblank console */
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_EDISP));
    } else {
	/* blank console */
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg & ~(VC2_CTRL_EDISP)));
    }
    return 1;
}

static int newport_font_op(struct vc_data *vc, struct console_font_op *f)
{
    return -ENOSYS;
}

static int newport_set_palette(struct vc_data *vc, unsigned char *table)
{
    return -EINVAL;
}

static int newport_scrolldelta(struct vc_data *vc, int lines)
{
    /* there is (nearly) no off-screen memory, so we can't scroll back */
    return 0;
}

static int newport_scroll(struct vc_data *vc, int t, int b, int dir, int lines)
{
    int count,x,y;
    unsigned short *s, *d;
    unsigned short chattr;

    if (t == 0 && b == vc->vc_rows) {
	if (dir == SM_UP) {
	    npregs->cset.topscan = topscan = (topscan + (lines << 4)) & 0x3ff;
	    newport_clear_lines (vc->vc_rows-lines,vc->vc_rows-1);		
	} else {
	    npregs->cset.topscan = topscan = (topscan + (-lines << 4)) & 0x3ff;
	    newport_clear_lines (0,lines-1);
	}
	return 0;
    }
    
    count = (b-t-lines) * vc->vc_cols;
    if (dir == SM_UP) {
	x = 0; y = t;
	s = (unsigned short *)(vc->vc_origin + vc->vc_size_row*(t+lines));
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*t);
	while (count--) {
	    chattr = scr_readw (s++);
	    if (chattr != scr_readw(d)) {
		newport_putc (vc, chattr, y, x);
		scr_writew (chattr, d);
	    }
	    d++;
	    if (++x == vc->vc_cols) {
		x = 0; y++;
	    }
	}
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*(b-lines));
	x = 0; y = b-lines;
	for (count = 0; count < (lines * vc->vc_cols); count++) {
	    if (scr_readw(d) != vc->vc_video_erase_char) {
		newport_putc (vc, chattr, y, x);
		scr_writew (vc->vc_video_erase_char, d);
	    }
	    d++;
	    if (++x == vc->vc_cols) {
		x = 0; y++;
	    }
	}
    } else {
	x = vc->vc_cols-1; y = b-1;
	s = (unsigned short *)(vc->vc_origin + vc->vc_size_row*(b-lines)-2);
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*b-2);
	while (count--) {
	    chattr = scr_readw (s--);
	    if (chattr != scr_readw(d)) {
		newport_putc (vc, chattr, y, x);
		scr_writew (chattr, d);
	    }
	    d--;
	    if (x-- == 0) {
		x = vc->vc_cols-1; y--;
	    }
	}
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*t);
	x = 0; y = t;
	for (count = 0; count < (lines * vc->vc_cols); count++) {
	    if (scr_readw(d) != vc->vc_video_erase_char) {
		newport_putc (vc, vc->vc_video_erase_char, y, x);
		scr_writew (vc->vc_video_erase_char, d);
	    }
	    d++;
	    if (++x == vc->vc_cols) {
		x = 0; y++;
	    }
	}
    }
    return 1;
}

static void newport_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx, int h, int w)
{
    short xs, ys, xe, ye, xoffs, yoffs, tmp;

    xs = sx << 3; xe = ((sx+w) << 3)-1;
    /*
     * as bmove is only used to move stuff around in the same line
     * (h == 1), we don't care about wrap arounds caused by topscan != 0
     */
    ys = ((sy << 4) + topscan) & 0x3ff; ye = (((sy+h) << 4)-1+topscan) & 0x3ff;
    xoffs = (dx - sx) << 3;
    yoffs = (dy - sy) << 4;
    if (xoffs > 0) {
	/* move to the right, exchange starting points */
	tmp = xe; xe = xs; xs = tmp;
    }
    newport_wait();
    npregs->set.drawmode0 = (NPORT_DMODE0_S2S | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			     NPORT_DMODE0_STOPY);
    npregs->set.xystarti = (xs << 16) | ys;
    npregs->set.xyendi = (xe << 16) | ye;
    npregs->go.xymove = (xoffs << 16) | yoffs;
}

static int newport_dummy(struct vc_data *c)
{
    return 0;
}

#define DUMMY (void *) newport_dummy

struct consw newport_con = {
    newport_startup,
    newport_init,
    DUMMY,                          /* con_deinit */
    newport_clear,
    newport_putc,
    newport_putcs,
    newport_cursor,
    newport_scroll,
    newport_bmove,
    newport_switch,
    newport_blank,
    newport_font_op,
    newport_set_palette,
    newport_scrolldelta,
    DUMMY, /* newport_set_origin, */
    DUMMY, /* newport_save_screen */
    NULL, /* newport_build_attr */
    NULL  /* newport_invert_region */
};
