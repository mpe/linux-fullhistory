/* $Id: cgsixfb.c,v 1.1 1998/07/06 15:51:09 jj Exp $
 * cgsixfb.c: CGsix (GX,GXplus) frame buffer driver
 *
 * Copyright (C) 1996,1998 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
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
#include <asm/io.h>

/* Offset of interesting structures in the OBIO space */
/*
 * Brooktree is the video dac and is funny to program on the cg6.
 * (it's even funnier on the cg3)
 * The FBC could be the frame buffer control
 * The FHC could is the frame buffer hardware control.
 */
#define CG6_ROM_OFFSET       0x0
#define CG6_BROOKTREE_OFFSET 0x200000
#define CG6_DHC_OFFSET       0x240000
#define CG6_ALT_OFFSET       0x280000
#define CG6_FHC_OFFSET       0x300000
#define CG6_THC_OFFSET       0x301000
#define CG6_FBC_OFFSET       0x700000
#define CG6_TEC_OFFSET       0x701000
#define CG6_RAM_OFFSET       0x800000

/* FHC definitions */
#define CG6_FHC_FBID_SHIFT           24
#define CG6_FHC_FBID_MASK            255
#define CG6_FHC_REV_SHIFT            20
#define CG6_FHC_REV_MASK             15
#define CG6_FHC_FROP_DISABLE         (1 << 19)
#define CG6_FHC_ROW_DISABLE          (1 << 18)
#define CG6_FHC_SRC_DISABLE          (1 << 17)
#define CG6_FHC_DST_DISABLE          (1 << 16)
#define CG6_FHC_RESET                (1 << 15)
#define CG6_FHC_LITTLE_ENDIAN        (1 << 13)
#define CG6_FHC_RES_MASK             (3 << 11)
#define CG6_FHC_1024                 (0 << 11)
#define CG6_FHC_1152                 (1 << 11)
#define CG6_FHC_1280                 (2 << 11)
#define CG6_FHC_1600                 (3 << 11)
#define CG6_FHC_CPU_MASK             (3 << 9)
#define CG6_FHC_CPU_SPARC            (0 << 9)
#define CG6_FHC_CPU_68020            (1 << 9)
#define CG6_FHC_CPU_386              (2 << 9)
#define CG6_FHC_TEST		     (1 << 8)
#define CG6_FHC_TEST_X_SHIFT	     4
#define CG6_FHC_TEST_X_MASK	     15
#define CG6_FHC_TEST_Y_SHIFT	     0
#define CG6_FHC_TEST_Y_MASK	     15

/* FBC mode definitions */
#define CG6_FBC_BLIT_IGNORE		0x00000000
#define CG6_FBC_BLIT_NOSRC		0x00100000
#define CG6_FBC_BLIT_SRC		0x00200000
#define CG6_FBC_BLIT_ILLEGAL		0x00300000
#define CG6_FBC_BLIT_MASK		0x00300000

#define CG6_FBC_VBLANK			0x00080000

#define CG6_FBC_MODE_IGNORE		0x00000000
#define CG6_FBC_MODE_COLOR8		0x00020000
#define CG6_FBC_MODE_COLOR1		0x00040000
#define CG6_FBC_MODE_HRMONO		0x00060000
#define CG6_FBC_MODE_MASK		0x00060000

#define CG6_FBC_DRAW_IGNORE		0x00000000
#define CG6_FBC_DRAW_RENDER		0x00008000
#define CG6_FBC_DRAW_PICK		0x00010000
#define CG6_FBC_DRAW_ILLEGAL		0x00018000
#define CG6_FBC_DRAW_MASK		0x00018000

#define CG6_FBC_BWRITE0_IGNORE		0x00000000
#define CG6_FBC_BWRITE0_ENABLE		0x00002000
#define CG6_FBC_BWRITE0_DISABLE		0x00004000
#define CG6_FBC_BWRITE0_ILLEGAL		0x00006000
#define CG6_FBC_BWRITE0_MASK		0x00006000

#define CG6_FBC_BWRITE1_IGNORE		0x00000000
#define CG6_FBC_BWRITE1_ENABLE		0x00000800
#define CG6_FBC_BWRITE1_DISABLE		0x00001000
#define CG6_FBC_BWRITE1_ILLEGAL		0x00001800
#define CG6_FBC_BWRITE1_MASK		0x00001800

#define CG6_FBC_BREAD_IGNORE		0x00000000
#define CG6_FBC_BREAD_0			0x00000200
#define CG6_FBC_BREAD_1			0x00000400
#define CG6_FBC_BREAD_ILLEGAL		0x00000600
#define CG6_FBC_BREAD_MASK		0x00000600

#define CG6_FBC_BDISP_IGNORE		0x00000000
#define CG6_FBC_BDISP_0			0x00000080
#define CG6_FBC_BDISP_1			0x00000100
#define CG6_FBC_BDISP_ILLEGAL		0x00000180
#define CG6_FBC_BDISP_MASK		0x00000180

#define CG6_FBC_INDEX_MOD		0x00000040
#define CG6_FBC_INDEX_MASK		0x00000030

/* THC definitions */
#define CG6_THC_MISC_REV_SHIFT       16
#define CG6_THC_MISC_REV_MASK        15
#define CG6_THC_MISC_RESET           (1 << 12)
#define CG6_THC_MISC_VIDEO           (1 << 10)
#define CG6_THC_MISC_SYNC            (1 << 9)
#define CG6_THC_MISC_VSYNC           (1 << 8)
#define CG6_THC_MISC_SYNC_ENAB       (1 << 7)
#define CG6_THC_MISC_CURS_RES        (1 << 6)
#define CG6_THC_MISC_INT_ENAB        (1 << 5)
#define CG6_THC_MISC_INT             (1 << 4)
#define CG6_THC_MISC_INIT            0x9f

/* The contents are unknown */
struct cg6_tec {
	volatile int tec_matrix;
	volatile int tec_clip;
	volatile int tec_vdc;
};

struct cg6_thc {
        uint thc_pad0[512];
	volatile uint thc_hs;		/* hsync timing */
	volatile uint thc_hsdvs;
	volatile uint thc_hd;
	volatile uint thc_vs;		/* vsync timing */
	volatile uint thc_vd;
	volatile uint thc_refresh;
	volatile uint thc_misc;
	uint thc_pad1[56];
	volatile uint thc_cursxy;	/* cursor x,y position (16 bits each) */
	volatile uint thc_cursmask[32];	/* cursor mask bits */
	volatile uint thc_cursbits[32];	/* what to show where mask enabled */
};

struct cg6_fbc {
	u32		xxx0[1];
	volatile u32	mode;
	volatile u32	clip;
	u32		xxx1[1];	    
	volatile u32	s;
	volatile u32	draw;
	volatile u32	blit;
	volatile u32	font;
	u32		xxx2[24];
	volatile u32	x0, y0, z0, color0;
	volatile u32	x1, y1, z1, color1;
	volatile u32	x2, y2, z2, color2;
	volatile u32	x3, y3, z3, color3;
	volatile u32	offx, offy;
	u32		xxx3[2];
	volatile u32	incx, incy;
	u32		xxx4[2];
	volatile u32	clipminx, clipminy;
	u32		xxx5[2];
	volatile u32	clipmaxx, clipmaxy;
	u32		xxx6[2];
	volatile u32	fg;
	volatile u32	bg;
	volatile u32	alu;
	volatile u32	pm;
	volatile u32	pixelm;
	u32		xxx7[2];
	volatile u32	patalign;
	volatile u32	pattern[8];
	u32		xxx8[432];
	volatile u32	apointx, apointy, apointz;
	u32		xxx9[1];
	volatile u32	rpointx, rpointy, rpointz;
	u32		xxx10[5];
	volatile u32	pointr, pointg, pointb, pointa;
	volatile u32	alinex, aliney, alinez;
	u32		xxx11[1];
	volatile u32	rlinex, rliney, rlinez;
	u32		xxx12[5];
	volatile u32	liner, lineg, lineb, linea;
	volatile u32	atrix, atriy, atriz;
	u32		xxx13[1];
	volatile u32	rtrix, rtriy, rtriz;
	u32		xxx14[5];
	volatile u32	trir, trig, trib, tria;
	volatile u32	aquadx, aquady, aquadz;
	u32		xxx15[1];
	volatile u32	rquadx, rquady, rquadz;
	u32		xxx16[5];
	volatile u32	quadr, quadg, quadb, quada;
	volatile u32	arectx, arecty, arectz;
	u32		xxx17[1];
	volatile u32	rrectx, rrecty, rrectz;
	u32		xxx18[5];
	volatile u32	rectr, rectg, rectb, recta;
};

static struct sbus_mmap_map cg6_mmap_map[] = {
	{ CG6_FBC,		CG6_FBC_OFFSET,		PAGE_SIZE },
	{ CG6_TEC,		CG6_TEC_OFFSET,		PAGE_SIZE },
	{ CG6_BTREGS,		CG6_BROOKTREE_OFFSET,	PAGE_SIZE },
	{ CG6_FHC,		CG6_FHC_OFFSET,		PAGE_SIZE },
	{ CG6_THC,		CG6_THC_OFFSET,		PAGE_SIZE },
	{ CG6_ROM,		CG6_ROM_OFFSET,		0x10000   },
	{ CG6_RAM,		CG6_RAM_OFFSET,		0x100000  }, /* FIXME: This should really be fbsize */
	{ CG6_DHC,		CG6_DHC_OFFSET,		0x40000   },
	{ 0,			0,			0	  }
};

static void cg6_setup(struct display *p)
{
	p->next_line = sbusfbinfo(p->fb_info)->var.xres_virtual;
	p->next_plane = 0;
}

static void cg6_bmove(struct display *p, int sy, int sx, int dy, int dx,
			   int height, int width)
{
#if 0
	int bytes = p->next_line, linesize = bytes * p->fontheight, rows;
	u8 *src, *dst;

	if (sx == 0 && dx == 0 && width * 32 == bytes)
		mymemmove(p->screen_base + dy * linesize,
				  p->screen_base + sy * linesize,
				  height * linesize);
	else if (dy < sy || (dy == sy && dx < sx)) {
		src = p->screen_base + sy * linesize + sx * 32;
		dst = p->screen_base + dy * linesize + dx * 32;
		for (rows = height * p->fontheight ; rows-- ;) {
			mymemmove(dst, src, width * 32);
			src += bytes;
			dst += bytes;
		}
	} else {
		src = p->screen_base + (sy+height) * linesize + sx * 32 - bytes;
		dst = p->screen_base + (dy+height) * linesize + dx * 32 - bytes;
		for (rows = height * p->fontheight ; rows-- ;) {
			mymemmove(dst, src, width * 32);
			src -= bytes;
			dst -= bytes;
		}
	}
#endif	
}

static void cg6_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
#if 0
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct cg6_fbc *fbc = fb->s.cg6.fbc;
	int x, y, w, h;
	
	fbc->ppc = 0x1803;
	fbc->fg = cg6_cmap[attr_bg_col_ec(conp)];
	fbc->fbc = 0x2000707f;
	fbc->rop = 0x83;
	fbc->pmask = 0xffffffff;
	fbc->unk2 = 8;

	/* FIXME: Optimize this by allowing 8/16 fontheigh only and introduce p->fontheightlog */
	if (p->fontheight == 16) {
		y = sy << 4; h = height << 4;
	} else {
		y = sy * p->fontheight; h = height * p->fontheight;
	}
	x = sx << 3; w = width << 3;
	fbc->by = y + fb->y_margin;
	fbc->bx = x + fb->x_margin;
	fbc->bh = h;
	fbc->bw = w;
#endif
}

static void cg6_fill(struct fb_info_sbusfb *fb, int s,
		     int count, unsigned short *boxes)
{
#if 0
	register struct cg6_fbc *fbc = fb->s.cg6.fbc;

	fbc->ppc = 0x1803;
	fbc->fg = cg6_cmap[attr_bg_col(s)];
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
#endif
}

static void cg6_putc(struct vc_data *conp, struct display *p, int c, int yy, int xx)
{
#if 0
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct cg6_fbc *fbc = fb->s.cg6.fbc;
	int i, xy;
	u8 *fd;

	if (p->fontheight == 16) {
		xy = (yy << (16 + 4));
		fd = p->fontdata + ((c & 0xff) << 4);
	} else {
		xy = ((yy * p->fontheight) << 16);
		fd = p->fontdata + (c & 0xff) * p->fontheight;
	}
	xy += (xx << 3) + fb->s.cg6.xy_margin;
	fbc->ppc = 0x203;
	fbc->fg = cg6_cmap[attr_fg_col(c)];
	fbc->fbc = 0x2000707f;
	fbc->rop = 0x83;
	fbc->pmask = 0xffffffff;
	fbc->bg = cg6_cmap[attr_bg_col(c)];
	fbc->fontw = 8;
	fbc->fontinc = 0x10000;
	fbc->fontxy = xy;
	for (i = 0; i < p->fontheight; i++)
		fbc->font = *fd++ << 24;
#endif		
}

static void cg6_putcs(struct vc_data *conp, struct display *p, const unsigned short *s,
		      int count, int yy, int xx)
{
#if 0
	struct fb_info_sbusfb *fb = (struct fb_info_sbusfb *)p->fb_info;
	register struct cg6_fbc *fbc = fb->s.cg6.fbc;
	int i, xy;
	u8 *fd1, *fd2, *fd3, *fd4;

	fbc->ppc = 0x203;
	fbc->fg = cg6_cmap[attr_fg_col(*s)];
	fbc->fbc = 0x2000707f;
	fbc->rop = 0x83;
	fbc->pmask = 0xffffffff;
	fbc->bg = cg6_cmap[attr_bg_col(*s)];
	if (p->fontheight == 16) {
		xy = (yy << (16 + 4)) + (xx << 3) + fb->s.cg6.xy_margin;
		while (count >= 4) {
			count -= 4;
			fbc->fontw = 32;
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
			fd1 = p->fontdata + ((*s++ & 0xff) << 4);
			fd2 = p->fontdata + ((*s++ & 0xff) << 4);
			fd3 = p->fontdata + ((*s++ & 0xff) << 4);
			fd4 = p->fontdata + ((*s++ & 0xff) << 4);
			for (i = 0; i < 16; i++)
				fbc->font = ((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) << 8)) << 8)) << 8);
			xy += 32;
		}
		while (count) {
			count--;
			fbc->fontw = 8;
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
			fd1 = p->fontdata + ((*s++ & 0xff) << 4);
			for (i = 0; i < 16; i++)
				fbc->font = *fd1++ << 24;
			xy += 8;
		}
	} else {
		xy = ((yy * p->fontheight) << 16) + (xx << 3) + fb->s.cg6.xy_margin;
		while (count >= 4) {
			count -= 4;
			fbc->fontw = 32;
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
			fd1 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
			fd2 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
			fd3 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
			fd4 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
			for (i = 0; i < p->fontheight; i++)
				fbc->font = ((u32)*fd4++) | ((((u32)*fd3++) | ((((u32)*fd2++) | (((u32)*fd1++) << 8)) << 8)) << 8);
			xy += 32;
		}
		while (count) {
			count--;
			fbc->fontw = 8;
			fbc->fontinc = 0x10000;
			fbc->fontxy = xy;
			fd1 = p->fontdata + ((*s++ & 0xff) * p->fontheight);
			for (i = 0; i < 16; i++)
				fbc->font = *fd1++ << 24;
			xy += 8;
		}
	}
#endif
}

static void cg6_revc(struct display *p, int xx, int yy)
{
	u8 *dest;
	int bytes=p->next_line, rows;
        
	dest = p->screen_base + yy * p->fontheight * bytes + xx * 8;
	for (rows = p->fontheight ; rows-- ; dest += bytes) {
		((u32 *)dest)[0] ^= 0x0f0f0f0f;
		((u32 *)dest)[1] ^= 0x0f0f0f0f;
	}
}

static void cg6_loadcmap (struct fb_info_sbusfb *fb, int index, int count)
{
	struct bt_regs *bt = fb->s.cg6.bt;
	int i;
                
	bt->addr = index << 24;
	for (i = index; count--; i++){
		bt->color_map = fb->color_map CM(i,0) << 24;
		bt->color_map = fb->color_map CM(i,1) << 24;
		bt->color_map = fb->color_map CM(i,2) << 24;
	}
}

static struct display_switch cg6_dispsw __initdata = {
	cg6_setup, cg6_bmove, cg6_clear, cg6_putc, cg6_putcs, cg6_revc, NULL
};

static void cg6_setcursormap (struct fb_info_sbusfb *fb, u8 *red, u8 *green, u8 *blue)
{
        struct bt_regs *bt = fb->s.cg6.bt;
        
	bt->addr = 1 << 24;
	bt->cursor = red[0] << 24;
	bt->cursor = green[0] << 24;
	bt->cursor = blue[0] << 24;
	bt->addr = 3 << 24;
	bt->cursor = red[1] << 24;
	bt->cursor = green[1] << 24;
	bt->cursor = blue[1] << 24;
}

/* Set cursor shape */
static void cg6_setcurshape (struct fb_info_sbusfb *fb)
{
	struct cg6_thc *thc = fb->s.cg6.thc;
	int i;

	for (i = 0; i < 32; i++){
		thc->thc_cursmask [i] = fb->cursor.bits[0][i];
		thc->thc_cursbits [i] = fb->cursor.bits[1][i];
	}
}

/* Load cursor information */
static void cg6_setcursor (struct fb_info_sbusfb *fb)
{
	unsigned int v;
	struct cg_cursor *c = &fb->cursor;

	if (c->enable)
		v = ((c->cpos.fbx - c->chot.fbx) << 16)
		    |((c->cpos.fby - c->chot.fby) & 0xffff);
	else
		/* Magic constant to turn off the cursor */
		v = ((65536-32) << 16) | (65536-32);
	fb->s.cg6.thc->thc_cursxy = v;
}

static char idstring[60] __initdata = { 0 };

__initfunc(char *cgsixfb_init(struct fb_info_sbusfb *fb))
{
	struct fb_fix_screeninfo *fix = &fb->fix;
	struct fb_var_screeninfo *var = &fb->var;
	struct display *disp = &fb->disp;
	struct fbtype *type = &fb->type;
	unsigned long phys = fb->sbdp->reg_addrs[0].phys_addr;
	u32 conf;
	char *p;

	strcpy(fb->info.modename, "CGsix");
		
	strcpy(fix->id, "CGsix");
	fix->smem_start = (char *)phys + CG6_RAM_OFFSET;
	fix->line_length = fb->var.xres_virtual;
	fix->mmio_start = (char *)phys + CG6_FBC_OFFSET;
	fix->mmio_len = PAGE_SIZE;
	
	var->accel_flags = FB_ACCELF_TEXT;
	
	disp->scrollmode = SCROLL_YREDRAW;
	if (!disp->screen_base)
		disp->screen_base = (char *)sparc_alloc_io(phys + CG6_RAM_OFFSET, 0, 
			type->fb_size, "cgsix_ram", fb->iospace, 0);
	disp->screen_base += fix->line_length * fb->y_margin + fb->x_margin;
	fb->s.cg6.fbc = (struct cg6_fbc *)(char *)sparc_alloc_io(phys + CG6_FBC_OFFSET, 0, 
			4096, "cgsix_fbc", fb->iospace, 0);
	fb->s.cg6.thc = (struct cg6_thc *)sparc_alloc_io(phys + CG6_THC_OFFSET, 0, 
				sizeof(struct cg6_thc), "cgsix_thc", fb->iospace, 0);
	fb->s.cg6.bt = (struct bt_regs *)sparc_alloc_io(phys + CG6_BROOKTREE_OFFSET, 0, 
				sizeof(struct bt_regs), "cgsix_dac", fb->iospace, 0);
	fb->s.cg6.fhc = (u32 *)sparc_alloc_io(phys + CG6_FHC_OFFSET, 0, 
				sizeof(u32), "cgsix_fhc", fb->iospace, 0);
	fb->dispsw = cg6_dispsw;

	fb->loadcmap = cg6_loadcmap;
	fb->setcursor = cg6_setcursor;
	fb->setcursormap = cg6_setcursormap;
	fb->setcurshape = cg6_setcurshape;
	fb->fill = cg6_fill;
	
	fb->physbase = phys;
	fb->mmap_map = cg6_mmap_map;
	
	/* Initialize Brooktree DAC */
	fb->s.cg6.bt->addr = 0x04 << 24;         /* color planes */
	fb->s.cg6.bt->control = 0xff << 24;
	fb->s.cg6.bt->addr = 0x05 << 24;
	fb->s.cg6.bt->control = 0x00 << 24;
	fb->s.cg6.bt->addr = 0x06 << 24;         /* overlay plane */
	fb->s.cg6.bt->control = 0x73 << 24;
	fb->s.cg6.bt->addr = 0x07 << 24;
	fb->s.cg6.bt->control = 0x00 << 24;
	
	conf = *fb->s.cg6.fhc;
	switch(conf & CG6_FHC_CPU_MASK) {
	case CG6_FHC_CPU_SPARC: p = "sparc"; break;
	case CG6_FHC_CPU_68020: p = "68020"; break;
	default: p = "i386"; break;
	}
	                                                                        
	sprintf(idstring, "cgsix at %02x.%08lx TEC Rev %x CPU %s Rev %x", fb->iospace, phys, 
		    (fb->s.cg6.thc->thc_misc >> CG6_THC_MISC_REV_SHIFT) & CG6_THC_MISC_REV_MASK,
		    p, conf >> CG6_FHC_REV_SHIFT & CG6_FHC_REV_MASK);
		    
	return idstring;
}
