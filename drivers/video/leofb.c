/* $Id: leofb.c,v 1.4 1998/09/04 15:43:45 jj Exp $
 * leofb.c: Leo (ZX) 24/8bit frame buffer driver
 *
 * Copyright (C) 1996,1997,1998 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1997 Michal Rehacek (Michal.Rehacek@st.mff.cuni.cz)
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/selection.h>

#include <video/sbusfb.h>
#include <asm/io.h>

#define LEO_OFF_LC_SS0_KRN	0x00200000
#define LEO_OFF_LC_SS0_USR	0x00201000
#define LEO_OFF_LC_SS1_KRN	0x01200000
#define LEO_OFF_LC_SS1_USR	0x01201000
#define LEO_OFF_LD_SS0		0x00400000
#define LEO_OFF_LD_SS1		0x01400000
#define LEO_OFF_LD_GBL		0x00401000
#define LEO_OFF_LX_KRN		0x00600000
#define LEO_OFF_LX_CURSOR	0x00601000
#define LEO_OFF_SS0		0x00800000
#define LEO_OFF_SS1		0x01800000
#define LEO_OFF_UNK		0x00602000
#define LEO_OFF_UNK2		0x00000000

#define LEO_CUR_ENABLE		0x00000080
#define LEO_CUR_UPDATE		0x00000030
#define LEO_CUR_PROGRESS	0x00000006
#define LEO_CUR_UPDATECMAP	0x00000003

#define LEO_CUR_TYPE_MASK	0x00000000
#define LEO_CUR_TYPE_IMAGE	0x00000020
#define LEO_CUR_TYPE_CMAP	0x00000050

struct leo_cursor {
	u8		xxx0[16];
	volatile u32	cur_type;
	volatile u32	cur_misc;
	volatile u32	cur_cursxy;
	volatile u32	cur_data;
};

#define LEO_KRN_TYPE_CLUT0	0x00001000
#define LEO_KRN_TYPE_CLUT1	0x00001001
#define LEO_KRN_TYPE_CLUT2	0x00001002
#define LEO_KRN_TYPE_WID	0x00001003
#define LEO_KRN_TYPE_UNK	0x00001006
#define LEO_KRN_TYPE_VIDEO	0x00002003
#define LEO_KRN_TYPE_CLUTDATA	0x00004000
#define LEO_KRN_CSR_ENABLE	0x00000008
#define LEO_KRN_CSR_PROGRESS	0x00000004
#define LEO_KRN_CSR_UNK		0x00000002
#define LEO_KRN_CSR_UNK2	0x00000001

struct leo_lx_krn {
	volatile u32	krn_type;
	volatile u32	krn_csr;
	volatile u32	krn_value;
};

struct leo_lc_ss0_krn {
	volatile u32 	misc;
	u8		xxx0[0x800-4];
	volatile u32	rev;
};

struct leo_lc_ss0_usr {
	volatile u32	csr;
	volatile u32	attrs;
	volatile u32 	fontc;
	volatile u32	fontc2;
	volatile u32	extent;
	volatile u32	src;
	u32		xxx1[1];
	volatile u32	copy;
	volatile u32	fill;
};

struct leo_lc_ss1_krn {
	u8	unknown;
};

struct leo_lc_ss1_usr {
	u8	unknown;
};

struct leo_ld_ss0 {
	u8		xxx0[0xe00];
	u32		xxx1[2];
	volatile u32	unk;
	u32		xxx2[1];
	volatile u32	unk2;
	volatile u32	unk3;
	u32		xxx3[2];
	volatile u32	fg;
	volatile u32	bg;
	u8		xxx4[0x05c];
	volatile u32	planemask;
	volatile u32	rop;
};

#define LEO_SS1_MISC_ENABLE	0x00000001
#define LEO_SS1_MISC_STEREO	0x00000002
struct leo_ld_ss1 {
	u8		xxx0[0xef4];
	volatile u32	ss1_misc;
};

struct leo_ld_gbl {
	u8	unknown;
};

static struct sbus_mmap_map leo_mmap_map[] = {
	{ LEO_SS0_MAP,		LEO_OFF_SS0,		0x800000	},
	{ LEO_LC_SS0_USR_MAP,	LEO_OFF_LC_SS0_USR,	PAGE_SIZE	},
	{ LEO_LD_SS0_MAP,	LEO_OFF_LD_SS0,		PAGE_SIZE	},
	{ LEO_LX_CURSOR_MAP,	LEO_OFF_LX_CURSOR,	PAGE_SIZE	},
	{ LEO_SS1_MAP,		LEO_OFF_SS1,		0x800000	},
	{ LEO_LC_SS1_USR_MAP,	LEO_OFF_LC_SS1_USR,	PAGE_SIZE	},
	{ LEO_LD_SS1_MAP,	LEO_OFF_LD_SS1,		PAGE_SIZE	},
	{ LEO_UNK_MAP,		LEO_OFF_UNK,		PAGE_SIZE	},
	{ LEO_LX_KRN_MAP,	LEO_OFF_LX_KRN,		PAGE_SIZE	},
	{ LEO_LC_SS0_KRN_MAP,	LEO_OFF_LC_SS0_KRN,	PAGE_SIZE	},
	{ LEO_LC_SS1_KRN_MAP,	LEO_OFF_LC_SS1_KRN,	PAGE_SIZE	},
	{ LEO_LD_GBL_MAP,	LEO_OFF_LD_GBL,		PAGE_SIZE	},
	{ LEO_UNK2_MAP,		LEO_OFF_UNK2,		0x100000	},
	{ 0,			0,			0	  	}
};

static void leo_setup(struct display *p)
{
	p->next_line = 8192;
	p->next_plane = 0;
}

static void leo_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct leo_lc_ss0_usr *us = fb->s.leo.lc_ss0_usr;
	register struct leo_ld_ss0 *ss = fb->s.leo.ld_ss0;
	int x, y, w, h;
	int i;

	do {
		i = us->csr;
	} while (i & 0x20000000);
	ss->unk = 0xffff;
	ss->unk2 = 0;
	ss->unk3 = fb->s.leo.extent;
	ss->fg = (attr_bgcol_ec(p,conp)<<24) | 0x030703;
	ss->planemask = 0xff000000;
	ss->rop = 0xd0840;
	if (fontheightlog(p)) {
		y = sy << fontheightlog(p); h = height << fontheightlog(p);
	} else {
		y = sy * fontheight(p); h = height * fontheight(p);
	}
	if (fontwidthlog(p)) {
		x = sx << fontwidthlog(p); w = width << fontwidthlog(p);
	} else {
		x = sx * fontwidth(p); w = width * fontwidth(p);
	}
	us->extent = (w - 1) | ((h - 1) << 11);
	i = us->attrs;
	us->fill = (x + fb->x_margin) | ((y + fb->y_margin) << 11) |
		   ((i & 3) << 29) | ((i & 8) ? 0x80000000 : 0);
}

static void leo_fill(struct fb_info_sbusfb *fb, struct display *p, int s,
		     int count, unsigned short *boxes)
{
	int i;
	register struct leo_lc_ss0_usr *us = fb->s.leo.lc_ss0_usr;
	register struct leo_ld_ss0 *ss = fb->s.leo.ld_ss0;
	
	do {
		i = us->csr;
	} while (i & 0x20000000);
	ss->unk = 0xffff;
	ss->unk2 = 0;
	ss->unk3 = fb->s.leo.extent;
	ss->fg = (attr_bgcol(p,s)<<24) | 0x030703;
	ss->planemask = 0xff000000;
	ss->rop = 0xd0840;
	while (count-- > 0) {
		us->extent = (boxes[2] - boxes[0] - 1) | 
			     ((boxes[3] - boxes[1] - 1) << 11);
		i = us->attrs;
		us->fill = boxes[0] | (boxes[1] << 11) |
			   ((i & 3) << 29) | ((i & 8) ? 0x80000000 : 0);
	}
}

static void leo_putc(struct vc_data *conp, struct display *p, int c, int yy, int xx)
{
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct leo_lc_ss0_usr *us = fb->s.leo.lc_ss0_usr;
	register struct leo_ld_ss0 *ss = fb->s.leo.ld_ss0;
	int i, x, y;
	u8 *fd;
	u32 *u;

	if (fontheightlog(p)) {
		y = yy << (fontheightlog(p) + 11);
		i = (c & p->charmask) << fontheightlog(p);
	} else {
		y = (yy * fontheight(p)) << 11;
		i = (c & p->charmask) * fontheight(p);
	}
	if (fontwidth(p) <= 8)
		fd = p->fontdata + i;
	else
		fd = p->fontdata + (i << 1);
	if (fontwidthlog(p))
		x = xx << fontwidthlog(p);
	else
		x = xx * fontwidth(p);
	do {
		i = us->csr;
	} while (i & 0x20000000);
	ss->fg = attr_fgcol(p,c) << 24;
	ss->bg = attr_bgcol(p,c) << 24;
	ss->rop = 0x310040;
	ss->planemask = 0xff000000;
	us->fontc2 = 0xFFFFFFFE;
	us->attrs = 4;
	us->fontc = 0xFFFFFFFF<<(32-fontwidth(p));
	u = ((u32 *)p->screen_base) + y + x;
	if (fontwidth(p) <= 8) {
		for (i = 0; i < fontheight(p); i++, u += 2048)
			*u = *fd++ << 24;
	} else {
		for (i = 0; i < fontheight(p); i++, u += 2048) {
			*u = *(u16 *)fd << 16;
			fd += 2;
		}
	}
}

static void leo_putcs(struct vc_data *conp, struct display *p, const unsigned short *s,
		      int count, int yy, int xx)
{
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct leo_lc_ss0_usr *us = fb->s.leo.lc_ss0_usr;
	register struct leo_ld_ss0 *ss = fb->s.leo.ld_ss0;
	int i, x, y;
	u8 *fd1, *fd2, *fd3, *fd4;
	u32 *u;

	do {
		i = us->csr;
	} while (i & 0x20000000);
	ss->fg = attr_fgcol(p,*s) << 24;
	ss->bg = attr_bgcol(p,*s) << 24;
	ss->rop = 0x310040;
	ss->planemask = 0xff000000;
	us->fontc2 = 0xFFFFFFFE;
	us->attrs = 4;
	us->fontc = 0xFFFFFFFF<<(32-fontwidth(p));
	if (fontwidthlog(p))
		x = (xx << fontwidthlog(p));
	else
		x = xx * fontwidth(p);
	if (fontheightlog(p))
		y = yy << (fontheightlog(p) + 11);
	else
		y = (yy * fontheight(p)) << 11;
	u = ((u32 *)p->screen_base) + y + x;
	if (fontwidth(p) <= 8) {
		us->fontc = 0xFFFFFFFF<<(32-4*fontwidth(p));
		x = 4*fontwidth(p) - fontheight(p)*2048;
		while (count >= 4) {
			count -= 4;
			if (fontheightlog(p)) {
				fd1 = p->fontdata + ((*s++ & p->charmask) << fontheightlog(p));
				fd2 = p->fontdata + ((*s++ & p->charmask) << fontheightlog(p));
				fd3 = p->fontdata + ((*s++ & p->charmask) << fontheightlog(p));
				fd4 = p->fontdata + ((*s++ & p->charmask) << fontheightlog(p));
			} else {
				fd1 = p->fontdata + ((*s++ & p->charmask) * fontheight(p));
				fd2 = p->fontdata + ((*s++ & p->charmask) * fontheight(p));
				fd3 = p->fontdata + ((*s++ & p->charmask) * fontheight(p));
				fd4 = p->fontdata + ((*s++ & p->charmask) * fontheight(p));
			}
			if (fontwidth(p) == 8) {
				for (i = 0; i < fontheight(p); i++, u += 2048)
					*u = ((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) 
						<< 8)) << 8)) << 8);
				u += x;
			} else {
				for (i = 0; i < fontheight(p); i++, u += 2048)
					*u = (((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) 
						<< fontwidth(p))) << fontwidth(p))) << fontwidth(p))) << (24 - 3 * fontwidth(p));
				u += x;
			}
		}
	} else {
		us->fontc = 0xFFFFFFFF<<(32-2*fontwidth(p));
		x = 2*fontwidth(p) - fontheight(p)*2048;
		while (count >= 2) {
			count -= 2;
			if (fontheightlog(p)) {
				fd1 = p->fontdata + ((*s++ & p->charmask) << (fontheightlog(p) + 1));
				fd2 = p->fontdata + ((*s++ & p->charmask) << (fontheightlog(p) + 1));
			} else {
				fd1 = p->fontdata + (((*s++ & p->charmask) * fontheight(p)) << 1);
				fd2 = p->fontdata + (((*s++ & p->charmask) * fontheight(p)) << 1);
			}
			for (i = 0; i < fontheight(p); i++, u += 2048) {
				*u = ((((u32)*(u16 *)fd1) << fontwidth(p)) | ((u32)*(u16 *)fd2)) << (16 - fontwidth(p));
				fd1 += 2; fd2 += 2;
			}
			u += x;
		}
	}
	us->fontc = 0xFFFFFFFF<<(32-fontwidth(p));
	x = fontwidth(p) - fontheight(p)*2048;
	while (count) {
		count--;
		if (fontheightlog(p))
			i = ((*s++ & p->charmask) << fontheightlog(p));
		else
			i = ((*s++ & p->charmask) * fontheight(p));
		if (fontwidth(p) <= 8) {
			fd1 = p->fontdata + i;
			for (i = 0; i < fontheight(p); i++, u += 2048)
				*u = *fd1++ << 24;
		} else {
			fd1 = p->fontdata + (i << 1);
			for (i = 0; i < fontheight(p); i++, u += 2048) {
				*u = *(u16 *)fd1 << 16;
				fd1 += 2;
			}
		}
		u += x;
	}
}

static void leo_revc(struct display *p, int xx, int yy)
{
	/* Not used if hw cursor */
}

static int leo_wait (struct leo_lx_krn *lx_krn)
{
	int i;
	
	for (i = 0; (lx_krn->krn_csr & LEO_KRN_CSR_PROGRESS) && i < 300000; i++)
		udelay (1); /* Busy wait at most 0.3 sec */
	if (i == 300000) return -EFAULT; /* Timed out - should we print some message? */
	return 0;
}

static void leo_loadcmap (struct fb_info_sbusfb *fb, struct display *p, int index, int count)
{
        struct leo_lx_krn *lx_krn = fb->s.leo.lx_krn;
	int i;
	
	lx_krn->krn_type = LEO_KRN_TYPE_CLUT0;
	i = leo_wait (lx_krn);
	if (i) return;
	lx_krn->krn_type = LEO_KRN_TYPE_CLUTDATA;
	for (i = 0; i < 256; i++)
		lx_krn->krn_value = fb->color_map CM(i,0) |
				    (fb->color_map CM(i,1) << 8) |
				    (fb->color_map CM(i,2) << 16); /* Throw colors there :)) */
	lx_krn->krn_type = LEO_KRN_TYPE_CLUT0;
	lx_krn->krn_csr |= (LEO_KRN_CSR_UNK|LEO_KRN_CSR_UNK2);
}

static void leo_restore_palette (struct fb_info_sbusfb *fb)
{
	fb->s.leo.ld_ss1->ss1_misc &= ~(LEO_SS1_MISC_ENABLE);
}

static struct display_switch leo_dispsw __initdata = {
	leo_setup, fbcon_redraw_bmove, leo_clear, leo_putc, leo_putcs, leo_revc, 
	NULL, NULL, NULL, FONTWIDTHRANGE(1,16) /* Allow fontwidths up to 16 */
};

static void leo_setcursormap (struct fb_info_sbusfb *fb, u8 *red, u8 *green, u8 *blue)
{
        struct leo_cursor *l = fb->s.leo.cursor;
	int i;
                
	for (i = 0; (l->cur_misc & LEO_CUR_PROGRESS) && i < 300000; i++)
		udelay (1); /* Busy wait at most 0.3 sec */
	if (i == 300000) return; /* Timed out - should we print some message? */
	l->cur_type = LEO_CUR_TYPE_CMAP;
	l->cur_data = (red[0] | (green[0]<<8) | (blue[0]<<16));
	l->cur_data = (red[1] | (green[1]<<8) | (blue[1]<<16));
	l->cur_misc = LEO_CUR_UPDATECMAP;
}

/* Set cursor shape */
static void leo_setcurshape (struct fb_info_sbusfb *fb)
{
	int i, j, k;
	u32 m, n, mask;
	struct leo_cursor *l = fb->s.leo.cursor;
                        
	l->cur_misc &= ~LEO_CUR_ENABLE;
	for (k = 0; k < 2; k ++) {
		l->cur_type = (k * LEO_CUR_TYPE_IMAGE); /* LEO_CUR_TYPE_MASK is 0 */
		for (i = 0; i < 32; i++) {
			mask = 0;
			m = fb->cursor.bits[k][i];
			/* mask = m with reversed bit order */
			for (j = 0, n = 1; j < 32; j++, n <<= 1)
				if (m & n)
					mask |= (0x80000000 >> j);
			l->cur_data = mask;
		}
	}
	l->cur_misc |= LEO_CUR_ENABLE;
}

/* Load cursor information */
static void leo_setcursor (struct fb_info_sbusfb *fb)
{
	struct cg_cursor *c = &fb->cursor;
	struct leo_cursor *l = fb->s.leo.cursor;

	l->cur_misc &= ~LEO_CUR_ENABLE;
	l->cur_cursxy = ((c->cpos.fbx - c->chot.fbx) & 0x7ff)
	|(((c->cpos.fby - c->chot.fby) & 0x7ff) << 11);
	l->cur_misc |= LEO_CUR_UPDATE;
	if (c->enable)
		l->cur_misc |= LEO_CUR_ENABLE;
}

static void leo_blank (struct fb_info_sbusfb *fb)
{
	fb->s.leo.lx_krn->krn_type = LEO_KRN_TYPE_VIDEO;
	fb->s.leo.lx_krn->krn_csr &= ~LEO_KRN_CSR_ENABLE;
}

static void leo_unblank (struct fb_info_sbusfb *fb)
{
	fb->s.leo.lx_krn->krn_type = LEO_KRN_TYPE_VIDEO;
	if (!(fb->s.leo.lx_krn->krn_csr & LEO_KRN_CSR_ENABLE))
		fb->s.leo.lx_krn->krn_csr |= LEO_KRN_CSR_ENABLE;
}

__initfunc(static int
leo_wid_put (struct fb_info_sbusfb *fb, struct fb_wid_list *wl))
{
	struct leo_lx_krn *lx_krn = fb->s.leo.lx_krn;
	struct fb_wid_item *wi;
	int i, j;

	lx_krn->krn_type = LEO_KRN_TYPE_WID;
	i = leo_wait (lx_krn);
	if (i) return i;
	for (i = 0, wi = wl->wl_list; i < wl->wl_count; i++, wi++) {
		switch (wi->wi_type) {
		case FB_WID_DBL_8: j = (wi->wi_index & 0xf) + 0x40; break;
		case FB_WID_DBL_24: j = wi->wi_index & 0x3f; break;
		default: return -EINVAL;
		}
		lx_krn->krn_type = 0x5800 + j;
		lx_krn->krn_value = wi->wi_values[0];
	}
	return 0;
}

static void leo_margins (struct fb_info_sbusfb *fb, struct display *p, int x_margin, int y_margin)
{
	p->screen_base += 8192 * (y_margin - fb->y_margin) + 4 * (x_margin - fb->x_margin);
}

static char idstring[40] __initdata = { 0 };

__initfunc(char *leofb_init(struct fb_info_sbusfb *fb))
{
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct fb_var_screeninfo *var = &fb->var;
	struct display *disp = &fb->disp;
	struct fbtype *type = &fb->type;
	unsigned long phys = fb->sbdp->reg_addrs[0].phys_addr;
	struct fb_wid_item wi;
	struct fb_wid_list wl;
	int i;

	strcpy(fb->info.modename, "Leo");
		
	strcpy(fix->id, "Leo");
	fix->visual = 0xff; /* We only know how to do acceleration and know nothing
	                       about the actual memory layout */
	fix->line_length = 8192;
	fix->accel = FB_ACCEL_SUN_LEO;
	
	var->accel_flags = FB_ACCELF_TEXT;
	
	disp->scrollmode = SCROLL_YREDRAW;
	if (!disp->screen_base)
		disp->screen_base = (char *)sparc_alloc_io(phys + LEO_OFF_SS0, 0, 
			0x800000, "leo_ram", fb->iospace, 0);
	disp->screen_base += 8192 * fb->y_margin + 4 * fb->x_margin;
	fb->s.leo.lc_ss0_usr = (struct leo_lc_ss0_usr *)
			sparc_alloc_io(phys + LEO_OFF_LC_SS0_USR, 0, 
			PAGE_SIZE, "leo_lc_ss0_usr", fb->iospace, 0);
	fb->s.leo.ld_ss0 = (struct leo_ld_ss0 *)
			sparc_alloc_io(phys + LEO_OFF_LD_SS0, 0, 
			PAGE_SIZE, "leo_ld_ss0", fb->iospace, 0);
	fb->s.leo.ld_ss1 = (struct leo_ld_ss1 *)
			sparc_alloc_io(phys + LEO_OFF_LD_SS1, 0, 
			PAGE_SIZE, "leo_ld_ss1", fb->iospace, 0);
	fb->s.leo.lx_krn = (struct leo_lx_krn *)
			sparc_alloc_io(phys + LEO_OFF_LX_KRN, 0, 
			PAGE_SIZE, "leo_lx_krn", fb->iospace, 0);
	fb->s.leo.cursor = (struct leo_cursor *)
			sparc_alloc_io(phys + LEO_OFF_LX_CURSOR, 0, 
			sizeof(struct leo_cursor), "leo_lx_cursor", fb->iospace, 0);
	fb->dispsw = leo_dispsw;

	fb->s.leo.extent = (type->fb_width-1) | ((type->fb_height-1) << 16);

	fb->s.leo.ld_ss0->unk = 0xffff;
	fb->s.leo.ld_ss0->unk2 = 0;
	fb->s.leo.ld_ss0->unk3 = fb->s.leo.extent;
	wl.wl_count = 1;
	wl.wl_list = &wi;
	wi.wi_type = FB_WID_DBL_8;
	wi.wi_index = 0;
	wi.wi_values [0] = 0x2c0;
	leo_wid_put (fb, &wl);
	wi.wi_index = 1;
	wi.wi_values [0] = 0x30;
	leo_wid_put (fb, &wl);
	wi.wi_index = 2;
	wi.wi_values [0] = 0x20;
	leo_wid_put (fb, &wl);

	fb->s.leo.ld_ss1->ss1_misc |= LEO_SS1_MISC_ENABLE;

	fb->s.leo.ld_ss0->fg = 0x30703;
	fb->s.leo.ld_ss0->planemask = 0xff000000;
	fb->s.leo.ld_ss0->rop = 0xd0840;
	fb->s.leo.lc_ss0_usr->extent = (type->fb_width-1) | ((type->fb_height-1) << 11);
	i = fb->s.leo.lc_ss0_usr->attrs;
	fb->s.leo.lc_ss0_usr->fill = (0) | ((0) << 11) | ((i & 3) << 29) | ((i & 8) ? 0x80000000 : 0);
	do {
		i = fb->s.leo.lc_ss0_usr->csr;
	} while (i & 0x20000000);

	fb->margins = leo_margins;
	fb->loadcmap = leo_loadcmap;
	fb->setcursor = leo_setcursor;
	fb->setcursormap = leo_setcursormap;
	fb->setcurshape = leo_setcurshape;
	fb->restore_palette = leo_restore_palette;
	fb->fill = leo_fill;
	fb->blank = leo_blank;
	fb->unblank = leo_unblank;
	
	fb->physbase = phys;
	fb->mmap_map = leo_mmap_map;
	
#ifdef __sparc_v9__
	sprintf(idstring, "leo at %016lx", phys);
#else	
	sprintf(idstring, "leo at %x.%08lx", fb->iospace, phys);
#endif
		    
	return idstring;
}
