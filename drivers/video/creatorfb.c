/* $Id: creatorfb.c,v 1.7 1998/07/21 10:36:48 jj Exp $
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

#include "sbusfb.h"

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

#define FFB_Y_BYTE_ADDR_SHIFT          11
#define FFB_Y_ADDR_SHIFT               13

#define FFB_PPC_ACE_DISABLE             1
#define FFB_PPC_ACE_AUX_ADD             3
#define FFB_PPC_ACE_SHIFT              18
#define FFB_PPC_DCE_DISABLE             2
#define FFB_PPC_DCE_SHIFT              16
#define FFB_PPC_ABE_DISABLE             2
#define FFB_PPC_ABE_SHIFT              14
#define FFB_PPC_VCE_DISABLE             1
#define FFB_PPC_VCE_2D                  2
#define FFB_PPC_VCE_SHIFT              12
#define FFB_PPC_APE_DISABLE             2
#define FFB_PPC_APE_SHIFT              10
#define FFB_PPC_CS_VARIABLE             2
#define FFB_PPC_CS_SHIFT                0

#define FFB_FBC_WB_A                    1
#define FFB_FBC_WB_SHIFT               29
#define FFB_FBC_PGE_MASK                3
#define FFB_FBC_BE_SHIFT                4
#define FFB_FBC_GE_SHIFT                2
#define FFB_FBC_RE_SHIFT                0

#define FFB_ROP_NEW                  0x83
#define FFB_ROP_RGB_SHIFT               0

#define FFB_UCSR_FIFO_MASK     0x00000fff
#define FFB_UCSR_RP_BUSY       0x02000000

struct ffb_fbc {
	u8		xxx1[0x60];
	volatile u32	by;
	volatile u32	bx;
	u32		xxx2;
	u32		xxx3;
	volatile u32	bh;
	volatile u32	bw;
	u8		xxx4[0x188];
	volatile u32	ppc;
	u32		xxx5;
	volatile u32	fg;
	volatile u32	bg;
	u8		xxx6[0x44];
	volatile u32	fbc;
	volatile u32	rop;
	u8		xxx7[0x34];
	volatile u32	pmask;
	u8		xxx8[12];
	volatile u32	clip0min;
	volatile u32	clip0max;
	volatile u32	clip1min;
	volatile u32	clip1max;
	volatile u32	clip2min;
	volatile u32	clip2max;
	volatile u32	clip3min;
	volatile u32	clip3max;
	u8		xxx9[0x3c];
	volatile u32	unk1;
	volatile u32	unk2;
	u8		xxx10[0x10];
	volatile u32	fontxy;
	volatile u32	fontw;
	volatile u32	fontinc;
	volatile u32	font;
	u8		xxx11[0x4dc];
	volatile u32	unk3;
	u8		xxx12[0xfc];
	volatile u32	ucsr;
};

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

static u32 ffb_cmap[16];

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
	
	fbc->ppc = 0x1803;
	fbc->fg = ffb_cmap[attr_bg_col_ec(conp)];
	fbc->fbc = 0x2000707f;
	fbc->rop = 0x83;
	fbc->pmask = 0xffffffff;
	fbc->unk2 = 8;

	if (p->fontheightlog) {
		y = sy << p->fontheightlog; h = height << p->fontheightlog;
	} else {
		y = sy * p->fontheight; h = height * p->fontheight;
	}
	if (p->fontwidthlog) {
		x = sx << p->fontwidthlog; w = width << p->fontwidthlog;
	} else {
		x = sx * p->fontwidth; w = width * p->fontwidth;
	}
	fbc->by = y + fb->y_margin;
	fbc->bx = x + fb->x_margin;
	fbc->bh = h;
	fbc->bw = w;
}

static void ffb_fill(struct fb_info_sbusfb *fb, int s,
		     int count, unsigned short *boxes)
{
	register struct ffb_fbc *fbc = fb->s.ffb.fbc;

	fbc->ppc = 0x1803;
	fbc->fg = ffb_cmap[attr_bg_col(s)];
	fbc->fbc = 0x2000707f;
	fbc->rop = 0x83;
	fbc->pmask = 0xffffffff;
	fbc->unk2 = 8;
	while (count-- > 0) {
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

	if (p->fontheightlog) {
		xy = (yy << (16 + p->fontheightlog));
		i = ((c & 0xff) << p->fontheightlog);
	} else {
		xy = ((yy * p->fontheight) << 16);
		i = (c & 0xff) * p->fontheight;
	}
	if (p->fontwidth <= 8)
		fd = p->fontdata + i;
	else
		fd = p->fontdata + (i << 1);
	if (p->fontwidthlog)
		xy += (xx << p->fontwidthlog) + fb->s.ffb.xy_margin;
	else
		xy += (xx * p->fontwidth) + fb->s.ffb.xy_margin;
	fbc->ppc = 0x203;
	fbc->fg = ffb_cmap[attr_fg_col(c)];
	fbc->fbc = 0x2000707f;
	fbc->rop = 0x83;
	fbc->pmask = 0xffffffff;
	fbc->bg = ffb_cmap[attr_bg_col(c)];
	fbc->fontw = p->fontwidth;
	fbc->fontinc = 0x10000;
	fbc->fontxy = xy;
	if (p->fontwidth <= 8) {
		for (i = 0; i < p->fontheight; i++)
			fbc->font = *fd++ << 24;
	} else {
		for (i = 0; i < p->fontheight; i++) {
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

	fbc->ppc = 0x203;
	fbc->fg = ffb_cmap[attr_fg_col(*s)];
	fbc->fbc = 0x2000707f;
	fbc->rop = 0x83;
	fbc->pmask = 0xffffffff;
	fbc->bg = ffb_cmap[attr_bg_col(*s)];
	xy = fb->s.ffb.xy_margin;
	if (p->fontwidthlog)
		xy += (xx << p->fontwidthlog);
	else
		xy += xx * p->fontwidth;
	if (p->fontheightlog)
		xy += (yy << (16 + p->fontheightlog));
	else
		xy += ((yy * p->fontheight) << 16);
	if (p->fontwidth <= 8) {
		while (count >= 4) {
			count -= 4;
			fbc->fontw = 4 * p->fontwidth;
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
			if (p->fontheightlog) {
				fd1 = p->fontdata + ((*s++ & 0xff) << p->fontheightlog);
				fd2 = p->fontdata + ((*s++ & 0xff) << p->fontheightlog);
				fd3 = p->fontdata + ((*s++ & 0xff) << p->fontheightlog);
				fd4 = p->fontdata + ((*s++ & 0xff) << p->fontheightlog);
			} else {
				fd1 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
				fd2 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
				fd3 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
				fd4 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
			}
			if (p->fontwidth == 8) {
				for (i = 0; i < p->fontheight; i++)
					fbc->font = ((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) 
						<< 8)) << 8)) << 8);
				xy += 32;
			} else {
				for (i = 0; i < p->fontheight; i++)
					fbc->font = (((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) 
						<< p->fontwidth)) << p->fontwidth)) << p->fontwidth)) << (24 - 3 * p->fontwidth);
				xy += 4 * p->fontwidth;
			}
		}
	} else {
		while (count >= 2) {
			count -= 2;
			fbc->fontw = 2 * p->fontwidth;
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
			if (p->fontheightlog) {
				fd1 = p->fontdata + ((*s++ & 0xff) << (p->fontheightlog + 1));
				fd2 = p->fontdata + ((*s++ & 0xff) << (p->fontheightlog + 1));
			} else {
				fd1 = p->fontdata + (((*s++ & 0xff) * p->fontheight) << 1);
				fd2 = p->fontdata + (((*s++ & 0xff) * p->fontheight) << 1);
			}
			for (i = 0; i < p->fontheight; i++) {
				fbc->font = ((((u32)*(u16 *)fd1) << p->fontwidth) | ((u32)*(u16 *)fd2)) << (16 - p->fontwidth);
				fd1 += 2; fd2 += 2;
			}
			xy += 2 * p->fontwidth;
		}
	}
	while (count) {
		count--;
		fbc->fontw = p->fontwidth;
		fbc->fontinc = 0x10000;
		fbc->fontxy = xy;
		if (p->fontheightlog)
			i = ((*s++ & 0xff) << p->fontheightlog);
		else
			i = ((*s++ & 0xff) * p->fontheight);
		if (p->fontwidth <= 8) {
			fd1 = p->fontdata + i;
			for (i = 0; i < p->fontheight; i++)
				fbc->font = *fd1++ << 24;
		} else {
			fd1 = p->fontdata + (i << 1);
			for (i = 0; i < p->fontheight; i++) {
				fbc->font = *(u16 *)fd1 << 16;
				fd1 += 2;
			}
		}
		xy += p->fontwidth;
	}
}

static void ffb_revc(struct display *p, int xx, int yy)
{
	/* Not used if hw cursor */
}

static void ffb_loadcmap (struct fb_info_sbusfb *fb, int index, int count)
{
	struct ffb_dac *dac = fb->s.ffb.dac;
	int i, j = count;
	
	dac->type = 0x2000 | index;
	for (i = index; j--; i++)
		/* Feed the colors in :)) */
		dac->value = ((fb->color_map CM(i,0))) |
			     ((fb->color_map CM(i,1)) << 8) |
			     ((fb->color_map CM(i,2)) << 16);
	for (i = index, j = count; i < 16 && j--; i++)
		ffb_cmap[i] = ((fb->color_map CM(i,0))) |
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

static char idstring[60] __initdata = { 0 };

__initfunc(char *creatorfb_init(struct fb_info_sbusfb *fb))
{
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct fb_var_screeninfo *var = &fb->var;
	struct display *disp = &fb->disp;
	struct fbtype *type = &fb->type;
	struct linux_prom64_registers regs[2*PROMREG_MAX];
	int i;

	if (prom_getproperty(fb->prom_node, "reg", (void *) regs, sizeof(regs)) <= 0)
		return NULL;
		
	strcpy(fb->info.modename, "Creator");
		
	strcpy(fix->id, "Creator");
	fix->smem_start = (char *)(regs[0].phys_addr) + FFB_DFB24_POFF;
	fix->visual = FB_VISUAL_DIRECTCOLOR;
	fix->line_length = 8192;
	fix->mmio_start = (char *)(regs[0].phys_addr) + FFB_FBC_REGS_POFF;
	fix->mmio_len = PAGE_SIZE;
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
	fb->fill = ffb_fill;
	
	fb->physbase = regs[0].phys_addr;
	fb->mmap_map = ffb_mmap_map;
	
	fb->cursor.hwsize.fbx = 64;
	fb->cursor.hwsize.fby = 64;
	
	type->fb_depth = 24;
	
	fb->s.ffb.dac->type = 0x8000;
	fb->s.ffb.dac_rev = (fb->s.ffb.dac->value >> 0x1c);
	                
	i = prom_getintdefault (fb->prom_node, "board_type", 8);
	                                                        
	sprintf(idstring, "Creator at %016lx type %d DAC %d", regs[0].phys_addr, i, fb->s.ffb.dac_rev);
	
	return idstring;
}
