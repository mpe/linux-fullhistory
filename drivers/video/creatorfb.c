/* $Id: creatorfb.c,v 1.17 1998/12/28 11:23:37 jj Exp $
 * creatorfb.c: Creator/Creator3D frame buffer driver
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@ultra.linux.cz)
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

#define	FFB_SFB8R_VOFF		0x00000000
#define	FFB_SFB8G_VOFF		0x00400000
#define	FFB_SFB8B_VOFF		0x00800000
#define	FFB_SFB8X_VOFF		0x00c00000
#define	FFB_SFB32_VOFF		0x01000000
#define	FFB_SFB64_VOFF		0x02000000
#define	FFB_FBC_REGS_VOFF	0x04000000
#define	FFB_BM_FBC_REGS_VOFF	0x04002000
#define	FFB_DFB8R_VOFF		0x04004000
#define	FFB_DFB8G_VOFF		0x04404000
#define	FFB_DFB8B_VOFF		0x04804000
#define	FFB_DFB8X_VOFF		0x04c04000
#define	FFB_DFB24_VOFF		0x05004000
#define	FFB_DFB32_VOFF		0x06004000
#define	FFB_DFB422A_VOFF	0x07004000	/* DFB 422 mode write to A */
#define	FFB_DFB422AD_VOFF	0x07804000	/* DFB 422 mode with line doubling */
#define	FFB_DFB24B_VOFF		0x08004000	/* DFB 24bit mode write to B */
#define	FFB_DFB422B_VOFF	0x09004000	/* DFB 422 mode write to B */
#define	FFB_DFB422BD_VOFF	0x09804000	/* DFB 422 mode with line doubling */
#define	FFB_SFB16Z_VOFF		0x0a004000	/* 16bit mode Z planes */
#define	FFB_SFB8Z_VOFF		0x0a404000	/* 8bit mode Z planes */
#define	FFB_SFB422_VOFF		0x0ac04000	/* SFB 422 mode write to A/B */
#define	FFB_SFB422D_VOFF	0x0b404000	/* SFB 422 mode with line doubling */
#define	FFB_FBC_KREGS_VOFF	0x0bc04000
#define	FFB_DAC_VOFF		0x0bc06000
#define	FFB_PROM_VOFF		0x0bc08000
#define	FFB_EXP_VOFF		0x0bc18000

#define	FFB_SFB8R_POFF		0x04000000
#define	FFB_SFB8G_POFF		0x04400000
#define	FFB_SFB8B_POFF		0x04800000
#define	FFB_SFB8X_POFF		0x04c00000
#define	FFB_SFB32_POFF		0x05000000
#define	FFB_SFB64_POFF		0x06000000
#define	FFB_FBC_REGS_POFF	0x00600000
#define	FFB_BM_FBC_REGS_POFF	0x00600000
#define	FFB_DFB8R_POFF		0x01000000
#define	FFB_DFB8G_POFF		0x01400000
#define	FFB_DFB8B_POFF		0x01800000
#define	FFB_DFB8X_POFF		0x01c00000
#define	FFB_DFB24_POFF		0x02000000
#define	FFB_DFB32_POFF		0x03000000
#define	FFB_FBC_KREGS_POFF	0x00610000
#define	FFB_DAC_POFF		0x00400000
#define	FFB_PROM_POFF		0x00000000
#define	FFB_EXP_POFF		0x00200000

/* Draw operations */
#define FFB_DRAWOP_DOT		0x00
#define FFB_DRAWOP_AADOT	0x01
#define FFB_DRAWOP_BRLINECAP	0x02
#define FFB_DRAWOP_BRLINEOPEN	0x03
#define FFB_DRAWOP_DDLINE	0x04
#define FFB_DRAWOP_AALINE	0x05
#define FFB_DRAWOP_TRIANGLE	0x06
#define FFB_DRAWOP_POLYGON	0x07
#define FFB_DRAWOP_RECTANGLE	0x08
#define FFB_DRAWOP_FASTFILL	0x09
#define FFB_DRAWOP_BCOPY	0x0a
#define FFB_DRAWOP_VSCROLL	0x0b

/* Pixel processor control */
/* Force WID */
#define FFB_PPC_FW_DISABLE	0x800000
#define FFB_PPC_FW_ENABLE	0xc00000
/* Auxiliary clip */
#define FFB_PPC_ACE_DISABLE	0x040000
#define FFB_PPC_ACE_AUX_SUB	0x080000
#define FFB_PPC_ACE_AUX_ADD	0x0c0000
/* Depth cue */
#define FFB_PPC_DCE_DISABLE	0x020000
#define FFB_PPC_DCE_ENABLE	0x030000
/* Alpha blend */
#define FFB_PPC_ABE_DISABLE	0x008000
#define FFB_PPC_ABE_ENABLE	0x00c000
/* View clip */
#define FFB_PPC_VCE_DISABLE	0x001000
#define FFB_PPC_VCE_2D		0x002000
#define FFB_PPC_VCE_3D		0x003000
/* Area pattern */
#define FFB_PPC_APE_DISABLE	0x000800
#define FFB_PPC_APE_ENABLE	0x000c00
/* Transparent background */
#define FFB_PPC_TBE_OPAQUE	0x000200
#define FFB_PPC_TBE_TRANSPARENT	0x000300
/* Z source */
#define FFB_PPC_ZS_VAR		0x000080
#define FFB_PPC_ZS_CONST	0x0000c0
/* Y source */
#define FFB_PPC_YS_VAR		0x000020
#define FFB_PPC_YS_CONST	0x000030
/* X source */
#define FFB_PPC_XS_WID		0x000004
#define FFB_PPC_XS_VAR		0x000008
#define FFB_PPC_XS_CONST	0x00000c
/* Color (BGR) source */
#define FFB_PPC_CS_VAR		0x000002
#define FFB_PPC_CS_CONST	0x000003

#define FFB_ROP_NEW                  0x83

#define FFB_UCSR_FIFO_MASK     0x00000fff
#define FFB_UCSR_FB_BUSY       0x01000000
#define FFB_UCSR_RP_BUSY       0x02000000
#define FFB_UCSR_ALL_BUSY      (FFB_UCSR_RP_BUSY|FFB_UCSR_FB_BUSY)
#define FFB_UCSR_READ_ERR      0x40000000
#define FFB_UCSR_FIFO_OVFL     0x80000000
#define FFB_UCSR_ALL_ERRORS    (FFB_UCSR_READ_ERR|FFB_UCSR_FIFO_OVFL)

struct ffb_fbc {
	/* Next vertex registers */
	u32		xxx1[3];
	volatile u32	alpha;
	volatile u32	red;
	volatile u32	green;
	volatile u32	blue;
	volatile u32	depth;
	volatile u32	y;
	volatile u32	x;
	u32		xxx2[2];
	volatile u32	ryf;
	volatile u32	rxf;
	u32		xxx3[2];
	
	volatile u32	dmyf;
	volatile u32	dmxf;
	u32		xxx4[2];
	volatile u32	ebyi;
	volatile u32	ebxi;
	u32		xxx5[2];
	volatile u32	by;
	volatile u32	bx;
	u32		dy;
	u32		dx;
	volatile u32	bh;
	volatile u32	bw;
	u32		xxx6[2];
	
	u32		xxx7[32];
	
	/* Setup unit vertex state register */
	volatile u32	suvtx;
	u32		xxx8[63];
	
	/* Control registers */
	volatile u32	ppc;
	volatile u32	wid;
	volatile u32	fg;
	volatile u32	bg;
	volatile u32	consty;
	volatile u32	constz;
	volatile u32	xclip;
	volatile u32	dcss;
	volatile u32	vclipmin;
	volatile u32	vclipmax;
	volatile u32	vclipzmin;
	volatile u32	vclipzmax;
	volatile u32	dcsf;
	volatile u32	dcsb;
	volatile u32	dczf;
	volatile u32	dczb;
	
	u32		xxx9;
	volatile u32	blendc;
	volatile u32	blendc1;
	volatile u32	blendc2;
	volatile u32	fbramitc;
	volatile u32	fbc;
	volatile u32	rop;
	volatile u32	cmp;
	volatile u32	matchab;
	volatile u32	matchc;
	volatile u32	magnab;
	volatile u32	magnc;
	volatile u32	fbcfg0;
	volatile u32	fbcfg1;
	volatile u32	fbcfg2;
	volatile u32	fbcfg3;
	
	u32		ppcfg;
	volatile u32	pick;
	volatile u32	fillmode;
	volatile u32	fbramwac;
	volatile u32	pmask;
	volatile u32	xpmask;
	volatile u32	ypmask;
	volatile u32	zpmask;
	volatile u32	clip0min;
	volatile u32	clip0max;
	volatile u32	clip1min;
	volatile u32	clip1max;
	volatile u32	clip2min;
	volatile u32	clip2max;
	volatile u32	clip3min;
	volatile u32	clip3max;
	
	/* New 3dRAM III support regs */
	volatile u32	rawblend2;
	volatile u32	rawpreblend;
	volatile u32	rawstencil;
	volatile u32	rawstencilctl;
	volatile u32	threedram1;
	volatile u32	threedram2;
	volatile u32	passin;
	volatile u32	rawclrdepth;
	volatile u32	rawpmask;
	volatile u32	rawcsrc;
	volatile u32	rawmatch;
	volatile u32	rawmagn;
	volatile u32	rawropblend;
	volatile u32	rawcmp;
	volatile u32	rawwac;
	volatile u32	fbramid;
	
	volatile u32	drawop;
	u32		xxx10[2];
	volatile u32	fontlpat;
	u32		xxx11;
	volatile u32	fontxy;
	volatile u32	fontw;
	volatile u32	fontinc;
	volatile u32	font;
	u32		xxx12[3];
	volatile u32	blend2;
	volatile u32	preblend;
	volatile u32	stencil;
	volatile u32	stencilctl;

	u32		xxx13[4];	
	volatile u32	dcss1;
	volatile u32	dcss2;
	volatile u32	dcss3;
	volatile u32	widpmask;
	volatile u32	dcs2;
	volatile u32	dcs3;
	volatile u32	dcs4;
	u32		xxx14;
	volatile u32	dcd2;
	volatile u32	dcd3;
	volatile u32	dcd4;
	u32		xxx15;
	
	volatile u32	pattern[32];
	
	u32		xxx16[256];
	
	volatile u32	devid;
	u32		xxx17[63];
	
	volatile u32	ucsr;
	u32		xxx18[31];
	
	volatile u32	mer;
};

static __inline__ void FFBFifo(struct ffb_fbc *ffb, int n)
{
	int limit = 10000;

	do {
		if((ffb->ucsr & FFB_UCSR_FIFO_MASK) >= (n + 4))
			break;
		if((ffb->ucsr & FFB_UCSR_ALL_ERRORS) != 0)
			ffb->ucsr = FFB_UCSR_ALL_ERRORS;
	} while(--limit > 0);
}

static __inline__ void FFBWait(struct ffb_fbc *ffb)
{
	int limit = 10000;

	do {
		if((ffb->ucsr & FFB_UCSR_ALL_BUSY) == 0)
			break;
		if((ffb->ucsr & FFB_UCSR_ALL_ERRORS) != 0)
			ffb->ucsr = FFB_UCSR_ALL_ERRORS;
	} while(--limit > 0);
}

struct ffb_dac {
	volatile u32	type;
	volatile u32	value;
	volatile u32	type2;
	volatile u32	value2;
};

static struct sbus_mmap_map ffb_mmap_map[] = {
	{ FFB_SFB8R_VOFF,	FFB_SFB8R_POFF,		0x0400000 },
	{ FFB_SFB8G_VOFF,	FFB_SFB8G_POFF,		0x0400000 },
	{ FFB_SFB8B_VOFF,	FFB_SFB8B_POFF,		0x0400000 },
	{ FFB_SFB8X_VOFF,	FFB_SFB8X_POFF,		0x0400000 },
	{ FFB_SFB32_VOFF,	FFB_SFB32_POFF,		0x1000000 },
	{ FFB_SFB64_VOFF,	FFB_SFB64_POFF,		0x2000000 },
	{ FFB_FBC_REGS_VOFF,	FFB_FBC_REGS_POFF,	0x0002000 },
	{ FFB_BM_FBC_REGS_VOFF,	FFB_BM_FBC_REGS_POFF,	0x0002000 },
	{ FFB_DFB8R_VOFF,	FFB_DFB8R_POFF,		0x0400000 },
	{ FFB_DFB8G_VOFF,	FFB_DFB8G_POFF,		0x0400000 },
	{ FFB_DFB8B_VOFF,	FFB_DFB8B_POFF,		0x0400000 },
	{ FFB_DFB8X_VOFF,	FFB_DFB8X_POFF,		0x0400000 },
	{ FFB_DFB24_VOFF,	FFB_DFB24_POFF,		0x1000000 },
	{ FFB_DFB32_VOFF,	FFB_DFB32_POFF,		0x1000000 },
	{ FFB_FBC_KREGS_VOFF,	FFB_FBC_KREGS_POFF,	0x0002000 },
	{ FFB_DAC_VOFF,		FFB_DAC_POFF,		0x0002000 },
	{ FFB_PROM_VOFF,	FFB_PROM_POFF,		0x0010000 },
	{ FFB_EXP_VOFF,		FFB_EXP_POFF,		0x0002000 },
	{ 0,			0,			0	  }
};

static void ffb_setup(struct display *p)
{
	p->next_line = 8192;
	p->next_plane = 0;
}

static void ffb_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct ffb_fbc *fbc = fb->s.ffb.fbc;
	int x, y, w, h;
	
	FFBWait(fbc);
	FFBFifo(fbc, 6);
	fbc->fg = ((u32 *)p->dispsw_data)[attr_bgcol_ec(p,conp)];
	fbc->drawop = FFB_DRAWOP_RECTANGLE;

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
	fbc->by = y + fb->y_margin;
	fbc->bx = x + fb->x_margin;
	fbc->bh = h;
	fbc->bw = w;
}

static void ffb_fill(struct fb_info_sbusfb *fb, struct display *p, int s,
		     int count, unsigned short *boxes)
{
	register struct ffb_fbc *fbc = fb->s.ffb.fbc;

	FFBWait(fbc);
	FFBFifo(fbc, 2);
	fbc->fg = ((u32 *)p->dispsw_data)[attr_bgcol(p,s)];
	fbc->drawop = FFB_DRAWOP_RECTANGLE;
	while (count-- > 0) {
		FFBFifo(fbc, 4);
		fbc->by = boxes[1];
		fbc->bx = boxes[0];
		fbc->bh = boxes[3] - boxes[1];
		fbc->bw = boxes[2] - boxes[0];
		boxes += 4;
	}
}

static void ffb_putc(struct vc_data *conp, struct display *p, int c, int yy, int xx)
{
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct ffb_fbc *fbc = fb->s.ffb.fbc;
	int i, xy;
	u8 *fd;

	if (fontheightlog(p)) {
		xy = (yy << (16 + fontheightlog(p)));
		i = ((c & p->charmask) << fontheightlog(p));
	} else {
		xy = ((yy * fontheight(p)) << 16);
		i = (c & p->charmask) * fontheight(p);
	}
	if (fontwidth(p) <= 8)
		fd = p->fontdata + i;
	else
		fd = p->fontdata + (i << 1);
	if (fontwidthlog(p))
		xy += (xx << fontwidthlog(p)) + fb->s.ffb.xy_margin;
	else
		xy += (xx * fontwidth(p)) + fb->s.ffb.xy_margin;
	FFBWait(fbc);
	FFBFifo(fbc, 5);
	fbc->fg = ((u32 *)p->dispsw_data)[attr_fgcol(p,c)];
	fbc->bg = ((u32 *)p->dispsw_data)[attr_bgcol(p,c)];
	fbc->fontw = fontwidth(p);
	fbc->fontinc = 0x10000;
	fbc->fontxy = xy;
	FFBFifo(fbc, fontheight(p));
	if (fontwidth(p) <= 8) {
		for (i = 0; i < fontheight(p); i++)
			fbc->font = *fd++ << 24;
	} else {
		for (i = 0; i < fontheight(p); i++) {
			fbc->font = *(u16 *)fd << 16;
			fd += 2;
		}
	}
}

static void ffb_putcs(struct vc_data *conp, struct display *p, const unsigned short *s,
		      int count, int yy, int xx)
{
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct ffb_fbc *fbc = fb->s.ffb.fbc;
	int i, xy;
	u8 *fd1, *fd2, *fd3, *fd4;

	FFBWait(fbc);
	FFBFifo(fbc, 2);
	fbc->fg = ((u32 *)p->dispsw_data)[attr_fgcol(p,*s)];
	fbc->bg = ((u32 *)p->dispsw_data)[attr_bgcol(p,*s)];
	xy = fb->s.ffb.xy_margin;
	if (fontwidthlog(p))
		xy += (xx << fontwidthlog(p));
	else
		xy += xx * fontwidth(p);
	if (fontheightlog(p))
		xy += (yy << (16 + fontheightlog(p)));
	else
		xy += ((yy * fontheight(p)) << 16);
	if (fontwidth(p) <= 8) {
		while (count >= 4) {
			count -= 4;
			FFBFifo(fbc, 3);
			fbc->fontw = 4 * fontwidth(p);
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
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
			FFBFifo(fbc, fontheight(p));
			if (fontwidth(p) == 8) {
				for (i = 0; i < fontheight(p); i++)
					fbc->font = ((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) 
						<< 8)) << 8)) << 8);
				xy += 32;
			} else {
				for (i = 0; i < fontheight(p); i++)
					fbc->font = (((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) 
						<< fontwidth(p))) << fontwidth(p))) << fontwidth(p))) << (24 - 3 * fontwidth(p));
				xy += 4 * fontwidth(p);
			}
		}
	} else {
		while (count >= 2) {
			count -= 2;
			FFBFifo(fbc, 3);
			fbc->fontw = 2 * fontwidth(p);
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
			if (fontheightlog(p)) {
				fd1 = p->fontdata + ((*s++ & p->charmask) << (fontheightlog(p) + 1));
				fd2 = p->fontdata + ((*s++ & p->charmask) << (fontheightlog(p) + 1));
			} else {
				fd1 = p->fontdata + (((*s++ & p->charmask) * fontheight(p)) << 1);
				fd2 = p->fontdata + (((*s++ & p->charmask) * fontheight(p)) << 1);
			}
			FFBFifo(fbc, fontheight(p));
			for (i = 0; i < fontheight(p); i++) {
				fbc->font = ((((u32)*(u16 *)fd1) << fontwidth(p)) | ((u32)*(u16 *)fd2)) << (16 - fontwidth(p));
				fd1 += 2; fd2 += 2;
			}
			xy += 2 * fontwidth(p);
		}
	}
	while (count) {
		count--;
		FFBFifo(fbc, 3);
		fbc->fontw = fontwidth(p);
		fbc->fontinc = 0x10000;
		fbc->fontxy = xy;
		if (fontheightlog(p))
			i = ((*s++ & p->charmask) << fontheightlog(p));
		else
			i = ((*s++ & p->charmask) * fontheight(p));
		FFBFifo(fbc, fontheight(p));
		if (fontwidth(p) <= 8) {
			fd1 = p->fontdata + i;
			for (i = 0; i < fontheight(p); i++)
				fbc->font = *fd1++ << 24;
		} else {
			fd1 = p->fontdata + (i << 1);
			for (i = 0; i < fontheight(p); i++) {
				fbc->font = *(u16 *)fd1 << 16;
				fd1 += 2;
			}
		}
		xy += fontwidth(p);
	}
}

static void ffb_revc(struct display *p, int xx, int yy)
{
	/* Not used if hw cursor */
}

static void ffb_loadcmap (struct fb_info_sbusfb *fb, struct display *p, int index, int count)
{
	struct ffb_dac *dac = fb->s.ffb.dac;
	int i, j = count;
	
	dac->type = 0x2000 | index;
	for (i = index; j--; i++)
		/* Feed the colors in :)) */
		dac->value = ((fb->color_map CM(i,0))) |
			     ((fb->color_map CM(i,1)) << 8) |
			     ((fb->color_map CM(i,2)) << 16);
	if (!p)
		return;
	for (i = index, j = count; i < 16 && j--; i++)
		((u32 *)p->dispsw_data)[i] = ((fb->color_map CM(i,0))) |
			      		     ((fb->color_map CM(i,1)) << 8) |
					     ((fb->color_map CM(i,2)) << 16);
}

static struct display_switch ffb_dispsw __initdata = {
	ffb_setup, fbcon_redraw_bmove, ffb_clear, ffb_putc, ffb_putcs, ffb_revc, 
	NULL, NULL, NULL, FONTWIDTHRANGE(1,16) /* Allow fontwidths up to 16 */
};

static void ffb_margins (struct fb_info_sbusfb *fb, struct display *p, int x_margin, int y_margin)
{
	fb->s.ffb.xy_margin = (y_margin << 16) + x_margin;
	p->screen_base += 8192 * (y_margin - fb->y_margin) + 4 * (x_margin - fb->x_margin);
}

static inline void ffb_curs_enable (struct fb_info_sbusfb *fb, int enable)
{
	struct ffb_dac *dac = fb->s.ffb.dac;
	
	dac->type2 = 0x100;
	if (fb->s.ffb.dac_rev <= 2)
		dac->value2 = enable ? 3 : 0;
	else
		dac->value2 = enable ? 0 : 3;
}

static void ffb_setcursormap (struct fb_info_sbusfb *fb, u8 *red, u8 *green, u8 *blue)
{
	struct ffb_dac *dac = fb->s.ffb.dac;
	
	ffb_curs_enable (fb, 0);
	dac->type2 = 0x102;
	dac->value2 = (red[0] | (green[0]<<8) | (blue[0]<<16));
	dac->value2 = (red[1] | (green[1]<<8) | (blue[1]<<16));
}

/* Set cursor shape */
static void ffb_setcurshape (struct fb_info_sbusfb *fb)
{
	struct ffb_dac *dac = fb->s.ffb.dac;
	int i, j;

	ffb_curs_enable (fb, 0);
	for (j = 0; j < 2; j++) {
		dac->type2 = j ? 0 : 0x80;
		for (i = 0; i < 0x40; i++) {
			if (fb->cursor.size.fbx <= 32) {
				dac->value2 = fb->cursor.bits [j][i];
				dac->value2 = 0;
			} else {
				dac->value2 = fb->cursor.bits [j][2*i];
				dac->value2 = fb->cursor.bits [j][2*i+1];
			}
		}
	}	
}

/* Load cursor information */
static void ffb_setcursor (struct fb_info_sbusfb *fb)
{
	struct ffb_dac *dac = fb->s.ffb.dac;
	struct cg_cursor *c = &fb->cursor;

	dac->type2 = 0x104;
	/* Should this be just 0x7ff?? 
	   Should I do some margin handling and setcurshape in that case? */
	dac->value2 = (((c->cpos.fby - c->chot.fby) & 0xffff) << 16)
	              |((c->cpos.fbx - c->chot.fbx) & 0xffff);
	ffb_curs_enable (fb, fb->cursor.enable);
}

static void ffb_switch_from_graph (struct fb_info_sbusfb *fb)
{
	register struct ffb_fbc *fbc = fb->s.ffb.fbc;

	FFBWait(fbc);
	FFBFifo(fbc, 4);
	fbc->ppc = FFB_PPC_VCE_DISABLE|FFB_PPC_TBE_OPAQUE|FFB_PPC_APE_DISABLE|FFB_PPC_CS_CONST;
	fbc->fbc = 0x2000707f;
	fbc->rop = FFB_ROP_NEW;
	fbc->pmask = 0xffffffff;
	FFBWait(fbc);
}                                

static char idstring[60] __initdata = { 0 };

__initfunc(char *creatorfb_init(struct fb_info_sbusfb *fb))
{
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct fb_var_screeninfo *var = &fb->var;
	struct display *disp = &fb->disp;
	struct fbtype *type = &fb->type;
	struct linux_prom64_registers regs[2*PROMREG_MAX];
	int i, afb = 0;
	unsigned int btype;
	char name[64];

	if (prom_getproperty(fb->prom_node, "reg", (void *) regs, sizeof(regs)) <= 0)
		return NULL;
		
	disp->dispsw_data = (void *)kmalloc(16 * sizeof(u32), GFP_KERNEL);
	if (!disp->dispsw_data)
		return NULL;
	memset(disp->dispsw_data, 0, 16 * sizeof(u32));

	prom_getstring(fb->prom_node, "name", name, sizeof(name));
	if (!strcmp(name, "SUNW,afb"))
		afb = 1;
		
	btype = prom_getintdefault(fb->prom_node, "board_type", 0);
		
	strcpy(fb->info.modename, "Creator");
	if (!afb) {
		if ((btype & 7) == 3)
		    strcpy(fix->id, "Creator 3D");
		else
		    strcpy(fix->id, "Creator");
	} else
		strcpy(fix->id, "Elite 3D");
	
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->line_length = 8192;
	fix->accel = FB_ACCEL_SUN_CREATOR;
	
	var->bits_per_pixel = 32;
	var->green.offset = 8;
	var->blue.offset = 16;
	var->accel_flags = FB_ACCELF_TEXT;
	
	disp->scrollmode = SCROLL_YREDRAW;
	disp->screen_base = (char *)__va(regs[0].phys_addr) + FFB_DFB24_POFF + 8192 * fb->y_margin + 4 * fb->x_margin;
	fb->s.ffb.xy_margin = (fb->y_margin << 16) + fb->x_margin;
	fb->s.ffb.fbc = (struct ffb_fbc *)((char *)__va(regs[0].phys_addr) + FFB_FBC_REGS_POFF);
	fb->s.ffb.dac = (struct ffb_dac *)((char *)__va(regs[0].phys_addr) + FFB_DAC_POFF);
	fb->dispsw = ffb_dispsw;

	fb->margins = ffb_margins;
	fb->loadcmap = ffb_loadcmap;
	fb->setcursor = ffb_setcursor;
	fb->setcursormap = ffb_setcursormap;
	fb->setcurshape = ffb_setcurshape;
	fb->switch_from_graph = ffb_switch_from_graph;
	fb->fill = ffb_fill;
	
	/* If there are any read errors or fifo overflow conditions,
	 * clear them now.
	 */
	if((fb->s.ffb.fbc->ucsr & FFB_UCSR_ALL_ERRORS) != 0)
		fb->s.ffb.fbc->ucsr = FFB_UCSR_ALL_ERRORS;

	ffb_switch_from_graph(fb);
	
	fb->physbase = regs[0].phys_addr;
	fb->mmap_map = ffb_mmap_map;
	
	fb->cursor.hwsize.fbx = 64;
	fb->cursor.hwsize.fby = 64;
	
	type->fb_depth = 24;
	
	fb->s.ffb.dac->type = 0x8000;
	fb->s.ffb.dac_rev = (fb->s.ffb.dac->value >> 0x1c);
	                
	i = prom_getintdefault (fb->prom_node, "board_type", 8);
	                                                        
	sprintf(idstring, "%s at %016lx type %d DAC %d", fix->id, regs[0].phys_addr, i, fb->s.ffb.dac_rev);
	
	return idstring;
}
