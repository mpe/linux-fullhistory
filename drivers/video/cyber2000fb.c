/*
 * Linux/drivers/video/cyber2000fb.c
 *
 * Copyright (C) 1998-2000 Russell King
 *
 * Integraphics CyberPro 2000, 2010 and 5000 frame buffer device
 *
 * Based on cyberfb.c.
 *
 * Note that we now use the new fbcon fix, var and cmap scheme.  We do still
 * have to check which console is the currently displayed one however, since
 * especially for the colourmap stuff.  Once fbcon has been fully migrated,
 * we can kill the last 5 references to cfb->currcon.
 *
 * We also use the new hotplug PCI subsystem.  This doesn't work fully in
 * the case of multiple CyberPro cards yet however.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>

/*
 * Define this if you don't want RGB565, but RGB555 for 16bpp displays.
 */
/*#define CFB16_IS_CFB15*/

/*
 * This is the offset of the PCI space in physical memory
 */
#ifdef CONFIG_ARCH_FOOTBRIDGE
#define PCI_PHYS_OFFSET	0x80000000
#else
#define	PCI_PHYS_OFFSET	0x00000000
#endif

static char			*CyberRegs;

#include "cyber2000fb.h"

struct cfb_info {
	struct fb_info		fb;
	struct display_switch	*dispsw;
	struct pci_dev		*dev;
	signed int		currcon;

	/*
	 * Clock divisors
	 */
	u_int			divisors[4];

	struct {
		u8 red, green, blue;
	} palette[NR_PALETTE];

	u_char			mem_ctl2;
};

/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Cyber2000 Acceleration
 */
static void cyber2000_accel_wait(void)
{
	int count = 100000;

	while (cyber2000_inb(CO_REG_CONTROL) & 0x80) {
		if (!count--) {
			debug_printf("accel_wait timed out\n");
			cyber2000_outb(0, CO_REG_CONTROL);
			return;
		}
		udelay(1);
	}
}

static void cyber2000_accel_setup(struct display *p)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cfb->dispsw->setup(p);
}

static void
cyber2000_accel_bmove(struct display *p, int sy, int sx, int dy, int dx,
		      int height, int width)
{
	struct fb_var_screeninfo *var = &p->fb_info->var;
	u_long src, dst;
	u_int fh, fw;
	int cmd = CO_CMD_L_PATTERN_FGCOL;

	fw    = fontwidth(p);
	sx    *= fw;
	dx    *= fw;
	width *= fw;
	width -= 1;

	if (sx < dx) {
		sx += width;
		dx += width;
		cmd |= CO_CMD_L_INC_LEFT;
	}

	fh     = fontheight(p);
	sy     *= fh;
	dy     *= fh;
	height *= fh;
	height -= 1;

	if (sy < dy) {
		sy += height;
		dy += height;
		cmd |= CO_CMD_L_INC_UP;
	}

	src    = sx + sy * var->xres_virtual;
	dst    = dx + dy * var->xres_virtual;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,  CO_REG_CONTROL);
	cyber2000_outb(0x03,  CO_REG_FORE_MIX);
	cyber2000_outw(width, CO_REG_WIDTH);

	if (var->bits_per_pixel != 24) {
		cyber2000_outl(dst, CO_REG_DEST_PTR);
		cyber2000_outl(src, CO_REG_SRC_PTR);
	} else {
		cyber2000_outl(dst * 3, CO_REG_DEST_PTR);
		cyber2000_outb(dst,     CO_REG_X_PHASE);
		cyber2000_outl(src * 3, CO_REG_SRC_PTR);
	}

	cyber2000_outw(height, CO_REG_HEIGHT);
	cyber2000_outw(cmd,    CO_REG_CMD_L);
	cyber2000_outw(0x2800, CO_REG_CMD_H);
}

static void
cyber2000_accel_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
	struct fb_var_screeninfo *var = &p->fb_info->var;
	u_long dst;
	u_int fw, fh;
	u32 bgx = attr_bgcol_ec(p, conp);

	fw = fontwidth(p);
	fh = fontheight(p);

	dst    = sx * fw + sy * var->xres_virtual * fh;
	width  = width * fw - 1;
	height = height * fh - 1;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,   CO_REG_CONTROL);
	cyber2000_outb(0x03,   CO_REG_FORE_MIX);
	cyber2000_outw(width,  CO_REG_WIDTH);
	cyber2000_outw(height, CO_REG_HEIGHT);

	switch (var->bits_per_pixel) {
	case 15:
	case 16:
		bgx = ((u16 *)p->dispsw_data)[bgx];
	case 8:
		cyber2000_outl(dst, CO_REG_DEST_PTR);
		break;

	case 24:
		cyber2000_outl(dst * 3, CO_REG_DEST_PTR);
		cyber2000_outb(dst, CO_REG_X_PHASE);
		bgx = ((u32 *)p->dispsw_data)[bgx];
		break;
	}

	cyber2000_outl(bgx, CO_REG_FOREGROUND);
	cyber2000_outw(CO_CMD_L_PATTERN_FGCOL, CO_REG_CMD_L);
	cyber2000_outw(0x0800, CO_REG_CMD_H);
}

static void
cyber2000_accel_putc(struct vc_data *conp, struct display *p, int c,
		     int yy, int xx)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cyber2000_accel_wait();
	cfb->dispsw->putc(conp, p, c, yy, xx);
}

static void
cyber2000_accel_putcs(struct vc_data *conp, struct display *p,
		      const unsigned short *s, int count, int yy, int xx)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cyber2000_accel_wait();
	cfb->dispsw->putcs(conp, p, s, count, yy, xx);
}

static void cyber2000_accel_revc(struct display *p, int xx, int yy)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cyber2000_accel_wait();
	cfb->dispsw->revc(p, xx, yy);
}

static void
cyber2000_accel_clear_margins(struct vc_data *conp, struct display *p,
			      int bottom_only)
{
	struct cfb_info *cfb = (struct cfb_info *)p->fb_info;

	cfb->dispsw->clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_cyber_accel = {
	cyber2000_accel_setup,
	cyber2000_accel_bmove,
	cyber2000_accel_clear,
	cyber2000_accel_putc,
	cyber2000_accel_putcs,
	cyber2000_accel_revc,
	NULL,
	NULL,
	cyber2000_accel_clear_margins,
	FONTWIDTH(8)|FONTWIDTH(16)
};

/*
 *    Set a single color register. Return != 0 for invalid regno.
 */
static int
cyber2000_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		    u_int transp, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;

	if (regno >= NR_PALETTE)
		return 1;

	red   >>= 10;
	green >>= 10;
	blue  >>= 10;

	cfb->palette[regno].red   = red;
	cfb->palette[regno].green = green;
	cfb->palette[regno].blue  = blue;

	switch (cfb->fb.var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		cyber2000_outb(regno, 0x3c8);
		cyber2000_outb(red,   0x3c9);
		cyber2000_outb(green, 0x3c9);
		cyber2000_outb(blue,  0x3c9);
		break;
#endif

#ifdef FBCON_HAS_CFB16
	case 16:
#ifndef CFB16_IS_CFB15
		if (regno < 64) {
			/* write green */
			cyber2000_outb(regno << 2, 0x3c8);
			cyber2000_outb(cfb->palette[regno >> 1].red, 0x3c9);
			cyber2000_outb(green, 0x3c9);
			cyber2000_outb(cfb->palette[regno >> 1].blue, 0x3c9);
		}

		if (regno < 32) {
			/* write red,blue */
			cyber2000_outb(regno << 3, 0x3c8);
			cyber2000_outb(red, 0x3c9);
			cyber2000_outb(cfb->palette[regno << 1].green, 0x3c9);
			cyber2000_outb(blue, 0x3c9);
		}

		if (regno < 16)
			((u16 *)cfb->fb.pseudo_palette)[regno] =
				regno | regno << 5 | regno << 11;
		break;
#endif

	case 15:
		if (regno < 32) {
			cyber2000_outb(regno << 3, 0x3c8);
			cyber2000_outb(red, 0x3c9);
			cyber2000_outb(green, 0x3c9);
			cyber2000_outb(blue, 0x3c9);
		}
		if (regno < 16)
			((u16 *)cfb->fb.pseudo_palette)[regno] =
				regno | regno << 5 | regno << 10;
		break;

#endif

#ifdef FBCON_HAS_CFB24
	case 24:
		cyber2000_outb(regno, 0x3c8);
		cyber2000_outb(red,   0x3c9);
		cyber2000_outb(green, 0x3c9);
		cyber2000_outb(blue,  0x3c9);

		if (regno < 16)
			((u32 *)cfb->fb.pseudo_palette)[regno] =
				regno | regno << 8 | regno << 16;
		break;
#endif

	default:
		return 1;
	}

	return 0;
}

struct par_info {
	/*
	 * Hardware
	 */
	u_char	clock_mult;
	u_char	clock_div;
	u_char	visualid;
	u_char	pixformat;
	u_char	crtc_ofl;
	u_char	crtc[19];
	u_int	width;
	u_int	pitch;
	u_int	fetch;

	/*
	 * Other
	 */
	u_char	palette_ctrl;
};

static const u_char crtc_idx[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
};

static void cyber2000fb_set_timing(struct par_info *hw)
{
	u_int i;

	/*
	 * Blank palette
	 */
	for (i = 0; i < NR_PALETTE; i++) {
		cyber2000_outb(i, 0x3c8);
		cyber2000_outb(0, 0x3c9);
		cyber2000_outb(0, 0x3c9);
		cyber2000_outb(0, 0x3c9);
	}

	cyber2000_outb(0xef, 0x3c2);
	cyber2000_crtcw(0x11, 0x0b);
	cyber2000_attrw(0x11, 0x00);

	cyber2000_seqw(0x00, 0x01);
	cyber2000_seqw(0x01, 0x01);
	cyber2000_seqw(0x02, 0x0f);
	cyber2000_seqw(0x03, 0x00);
	cyber2000_seqw(0x04, 0x0e);
	cyber2000_seqw(0x00, 0x03);

	for (i = 0; i < sizeof(crtc_idx); i++)
		cyber2000_crtcw(crtc_idx[i], hw->crtc[i]);

	for (i = 0x0a; i < 0x10; i++)
		cyber2000_crtcw(i, 0);

	cyber2000_grphw(0x11, hw->crtc_ofl);
	cyber2000_grphw(0x00, 0x00);
	cyber2000_grphw(0x01, 0x00);
	cyber2000_grphw(0x02, 0x00);
	cyber2000_grphw(0x03, 0x00);
	cyber2000_grphw(0x04, 0x00);
	cyber2000_grphw(0x05, 0x60);
	cyber2000_grphw(0x06, 0x05);
	cyber2000_grphw(0x07, 0x0f);
	cyber2000_grphw(0x08, 0xff);

	/* Attribute controller registers */
	for (i = 0; i < 16; i++)
		cyber2000_attrw(i, i);

	cyber2000_attrw(0x10, 0x01);
	cyber2000_attrw(0x11, 0x00);
	cyber2000_attrw(0x12, 0x0f);
	cyber2000_attrw(0x13, 0x00);
	cyber2000_attrw(0x14, 0x00);

	/* PLL registers */
	cyber2000_grphw(0xb0, hw->clock_mult);
	cyber2000_grphw(0xb1, hw->clock_div);

	cyber2000_grphw(0xb2, 0xdb);
	cyber2000_grphw(0xb3, 0x54);		/* MCLK: 75MHz */
	cyber2000_grphw(0x90, 0x01);
	cyber2000_grphw(0xb9, 0x80);
	cyber2000_grphw(0xb9, 0x00);

	cyber2000_outb(0x56, 0x3ce);
	i = cyber2000_inb(0x3cf);
	cyber2000_outb(i | 4, 0x3cf);
	cyber2000_outb(hw->palette_ctrl, 0x3c6);
	cyber2000_outb(i,    0x3cf);

	cyber2000_outb(0x20, 0x3c0);
	cyber2000_outb(0xff, 0x3c6);

	cyber2000_grphw(0x14, hw->fetch);
	cyber2000_grphw(0x15, ((hw->fetch >> 8) & 0x03) |
			      ((hw->pitch >> 4) & 0x30));
	cyber2000_grphw(0x77, hw->visualid);
	cyber2000_grphw(0x33, 0x0c);

	/*
	 * Set up accelerator registers
	 */
	cyber2000_outw(hw->width, CO_REG_SRC_WIDTH);
	cyber2000_outw(hw->width, CO_REG_DEST_WIDTH);
	cyber2000_outb(hw->pixformat, CO_REG_PIX_FORMAT);
}

static inline int
cyber2000fb_update_start(struct cfb_info *cfb, struct fb_var_screeninfo *var)
{
	u_int base;

	base = var->yoffset * var->xres_virtual + var->xoffset;

	base >>= 2;

	if (base >= 1 << 20)
		return -EINVAL;

	cyber2000_grphw(0x10, base >> 16 | 0x10);
	cyber2000_crtcw(0x0c, base >> 8);
	cyber2000_crtcw(0x0d, base);

	return 0;
}

/*
 *  Open/Release the frame buffer device
 */
static int cyber2000fb_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int cyber2000fb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Set the Colormap
 */
static int
cyber2000fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		     struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	struct fb_cmap *dcmap = &fb_display[con].cmap;
	int err = 0;

	/* no colormap allocated? */
	if (!dcmap->len) {
		int size;

		if (cfb->fb.var.bits_per_pixel == 16)
			size = 32;
		else
			size = 256;

		err = fb_alloc_cmap(dcmap, size, 0);
	}

	/*
	 * we should be able to remove this test once fbcon has been
	 * "improved" --rmk
	 */
	if (!err && con == cfb->currcon) {
		err = fb_set_cmap(cmap, kspc, cyber2000_setcolreg, &cfb->fb);
		dcmap = &cfb->fb.cmap;
	}

	if (!err)
		fb_copy_cmap(cmap, dcmap, kspc ? 0 : 1);

	return err;
}

static int
cyber2000fb_decode_crtc(struct par_info *hw, struct cfb_info *cfb,
			struct fb_var_screeninfo *var)
{
	u_int Htotal, Hblankend, Hsyncend;
	u_int Vtotal, Vdispend, Vblankstart, Vblankend, Vsyncstart, Vsyncend;
#define BIT(v,b1,m,b2) (((v >> b1) & m) << b2)

	hw->crtc[13] = hw->pitch;
	hw->crtc[17] = 0xe3;
	hw->crtc[14] = 0;
	hw->crtc[8]  = 0;

	Htotal      = var->xres + var->right_margin +
		      var->hsync_len + var->left_margin;
	if (Htotal > 2080)
		return -EINVAL;

	hw->crtc[0] = (Htotal >> 3) - 5;
	hw->crtc[1] = (var->xres >> 3) - 1;
	hw->crtc[2] = var->xres >> 3;
	hw->crtc[4] = (var->xres + var->right_margin) >> 3;

	Hblankend   = (Htotal - 4*8) >> 3;

	hw->crtc[3] = BIT(Hblankend,  0, 0x1f,  0) |
		      BIT(1,          0, 0x01,  7);

	Hsyncend    = (var->xres + var->right_margin + var->hsync_len) >> 3;

	hw->crtc[5] = BIT(Hsyncend,   0, 0x1f,  0) |
		      BIT(Hblankend,  5, 0x01,  7);

	Vdispend    = var->yres - 1;
	Vsyncstart  = var->yres + var->lower_margin;
	Vsyncend    = var->yres + var->lower_margin + var->vsync_len;
	Vtotal      = var->yres + var->lower_margin + var->vsync_len +
		      var->upper_margin - 2;

	if (Vtotal > 2047)
		return -EINVAL;

	Vblankstart = var->yres + 6;
	Vblankend   = Vtotal - 10;

	hw->crtc[6]  = Vtotal;
	hw->crtc[7]  = BIT(Vtotal,     8, 0x01,  0) |
			BIT(Vdispend,   8, 0x01,  1) |
			BIT(Vsyncstart, 8, 0x01,  2) |
			BIT(Vblankstart,8, 0x01,  3) |
			BIT(1,          0, 0x01,  4) |
	        	BIT(Vtotal,     9, 0x01,  5) |
			BIT(Vdispend,   9, 0x01,  6) |
			BIT(Vsyncstart, 9, 0x01,  7);
	hw->crtc[9]  = BIT(0,          0, 0x1f,  0) |
		        BIT(Vblankstart,9, 0x01,  5) |
			BIT(1,          0, 0x01,  6);
	hw->crtc[10] = Vsyncstart;
	hw->crtc[11] = BIT(Vsyncend,   0, 0x0f,  0) |
		       BIT(1,          0, 0x01,  7);
	hw->crtc[12] = Vdispend;
	hw->crtc[15] = Vblankstart;
	hw->crtc[16] = Vblankend;
	hw->crtc[18] = 0xff;

	/* overflow - graphics reg 0x11 */
	/* 0=VTOTAL:10 1=VDEND:10 2=VRSTART:10 3=VBSTART:10
	 * 4=LINECOMP:10 5-IVIDEO 6=FIXCNT
	 */
	hw->crtc_ofl =
		BIT(Vtotal,     10, 0x01,  0) |
		BIT(Vdispend,   10, 0x01,  1) |
		BIT(Vsyncstart, 10, 0x01,  2) |
		BIT(Vblankstart,10, 0x01,  3) |
		1 << 4;

	return 0;
}

/*
 * The following was discovered by a good monitor, bit twiddling, theorising
 * and but mostly luck.  Strangely, it looks like everyone elses' PLL!
 *
 * Clock registers:
 *   fclock = fpll / div2
 *   fpll   = fref * mult / div1
 * where:
 *   fref = 14.318MHz (69842ps)
 *   mult = reg0xb0.7:0
 *   div1 = (reg0xb1.5:0 + 1)
 *   div2 =  2^(reg0xb1.7:6)
 *   fpll should be between 115 and 260 MHz
 *  (8696ps and 3846ps)
 */
static int
cyber2000fb_decode_clock(struct par_info *hw, struct cfb_info *cfb,
			 struct fb_var_screeninfo *var)
{
	u_long pll_ps = var->pixclock;
	const u_long ref_ps = 69842;
	u_int div2, t_div1, best_div1, best_mult;
	int best_diff;

	/*
	 * Step 1:
	 *   find div2 such that 115MHz < fpll < 260MHz
	 *   and 0 <= div2 < 4
	 */
	for (div2 = 0; div2 < 4; div2++) {
		u_long new_pll;

		new_pll = pll_ps / cfb->divisors[div2];
		if (8696 > new_pll && new_pll > 3846) {
			pll_ps = new_pll;
			break;
		}
	}

	if (div2 == 4)
		return -EINVAL;

#if 1
	/*
	 * Step 2:
	 *  Given pll_ps and ref_ps, find:
	 *    pll_ps * 0.995 < pll_ps_calc < pll_ps * 1.005
	 *  where { 1 < best_div1 < 32, 1 < best_mult < 256 }
	 *    pll_ps_calc = best_div1 / (ref_ps * best_mult)
	 */
	best_diff = 0x7fffffff;
	best_mult = 32;
	best_div1 = 255;
	for (t_div1 = 32; t_div1 > 1; t_div1 -= 1) {
		u_int rr, t_mult, t_pll_ps;
		int diff;

		/*
		 * Find the multiplier for this divisor
		 */
		rr = ref_ps * t_div1;
		t_mult = (rr + pll_ps / 2) / pll_ps;

		/*
		 * Is the multiplier within the correct range?
		 */
		if (t_mult > 256 || t_mult < 2)
			continue;

		/*
		 * Calculate the actual clock period from this multiplier
		 * and divisor, and estimate the error.
		 */
		t_pll_ps = (rr + t_mult / 2) / t_mult;
		diff = pll_ps - t_pll_ps;
		if (diff < 0)
			diff = -diff;

		if (diff < best_diff) {
			best_diff = diff;
			best_mult = t_mult;
			best_div1 = t_div1;
		}

		/*
		 * If we hit an exact value, there is no point in continuing.
		 */
		if (diff == 0)
			break;
	}
#else
	/* Note! This table will be killed shortly. --rmk */
	/*
	 *				1600x1200 1280x1024 1152x864 1024x768 800x600 640x480
	 * 5051		5051	yes	   76*
	 * 5814		5814	no	   66
	 * 6411		6411	no	   60
	 * 7408		7408	yes	             75*
	 *				             74*
	 * 7937		7937	yes	             70*
	 * 9091		4545	yes	                       80*
	 *				                       75*     100*
	 * 9260		4630	yes	             60*
	 * 10000	5000	no	                       70       90
	 * 12500	6250	yes	             47-lace*  60*
	 *				             43-lace*
	 * 12699	6349	yes	                                75*
	 * 13334	6667	no	                                72
	 *				                                70
	 * 14815	7407	yes	                                       100*
	 * 15385	7692	yes	                       47-lace* 60*
	 *				                       43-lace*
	 * 17656	4414	no	                                        90
	 * 20000	5000	no	                                        72
	 * 20203	5050	yes	                                        75*
	 * 22272	5568	yes	                               43-lace* 70*    100*
	 * 25000	6250	yes	                                        60*
	 * 25057	6264	no	                                                90
	 * 27778	6944	yes	                                        56*
	 *									48-lace*
	 * 31747	7936	yes	                                                75*
	 * 32052	8013	no	                                                72
	 * 39722 /6	6620	no
	 * 39722 /8	4965	yes	                                                60*
	 */
					/*  /1     /2     /4     /6     /8    */
					/*                      (2010) (2000) */
	if (pll_ps >= 4543 && pll_ps <= 4549) {
		best_mult = 169;	/*u220.0  110.0  54.99  36.663 27.497 */
		best_div1 = 11;		/* 4546    9092  18184  27276  36367  */
	} else if (pll_ps >= 4596 && pll_ps <= 4602) {
		best_mult = 243;	/* 217.5  108.7  54.36  36.243 27.181 */
		best_div1 = 16;		/* 4599    9197  18395  27592  36789  */
	} else if (pll_ps >= 4627 && pll_ps <= 4633) {
		best_mult = 181;	/*u216.0, 108.0, 54.00, 36.000 27.000 */
		best_div1 = 12;		/* 4630    9260  18520  27780  37040  */
	} else if (pll_ps >= 4962 && pll_ps <= 4968) {
		best_mult = 211;	/*u201.0, 100.5, 50.25, 33.500 25.125 */
		best_div1 = 15;		/* 4965    9930  19860  29790  39720  */
	} else if (pll_ps >= 5005 && pll_ps <= 5011) {
		best_mult = 251;	/* 200.0   99.8  49.92  33.280 24.960 */
		best_div1 = 18;		/* 5008   10016  20032  30048  40064  */
	} else if (pll_ps >= 5047 && pll_ps <= 5053) {
		best_mult = 83;		/*u198.0,  99.0, 49.50, 33.000 24.750 */
		best_div1 = 6;		/* 5050   10100  20200  30300  40400  */
	} else if (pll_ps >= 5490 && pll_ps <= 5496) {
		best_mult = 89;		/* 182.0   91.0  45.51  30.342 22.756 */
		best_div1 = 7;		/* 5493   10986  21972  32958  43944  */
	} else if (pll_ps >= 5567 && pll_ps <= 5573) {
		best_mult = 163;	/*u179.5   89.8  44.88  29.921 22.441 */
		best_div1 = 13;		/* 5570   11140  22281  33421  44562  */
	} else if (pll_ps >= 6246 && pll_ps <= 6252) {
		best_mult = 190;	/*u160.0,  80.0, 40.00, 26.671 20.003 */
		best_div1 = 17;		/* 6249   12498  24996  37494  49992  */
	} else if (pll_ps >= 6346 && pll_ps <= 6352) {
		best_mult = 209;	/*u158.0,  79.0, 39.50, 26.333 19.750 */
		best_div1 = 19;		/* 6349   12698  25396  38094  50792  */
	} else if (pll_ps >= 6648 && pll_ps <= 6655) {
		best_mult = 210;	/*u150.3   75.2  37.58  25.057 18.792 */
		best_div1 = 20;		/* 6652   13303  26606  39909  53213  */
	} else if (pll_ps >= 6943 && pll_ps <= 6949) {
		best_mult = 181;	/*u144.0   72.0  36.00  23.996 17.997 */
		best_div1 = 18;		/* 6946   13891  27782  41674  55565  */
	} else if (pll_ps >= 7404 && pll_ps <= 7410) {
		best_mult = 198;	/*u134.0   67.5  33.75  22.500 16.875 */
		best_div1 = 21;		/* 7407   14815  29630  44445  59260  */
	} else if (pll_ps >= 7689 && pll_ps <= 7695) {
		best_mult = 227;	/*u130.0   65.0  32.50  21.667 16.251 */
		best_div1 = 25;		/* 7692   15384  30768  46152  61536  */
	} else if (pll_ps >= 7808 && pll_ps <= 7814) {
		best_mult = 152;	/* 128.0   64.0  32.00  21.337 16.003 */
		best_div1 = 17;		/* 7811   15623  31245  46868  62490  */
	} else if (pll_ps >= 7934 && pll_ps <= 7940) {
		best_mult = 44;		/*u126.0   63.0  31.498 20.999 15.749 */
		best_div1 = 5;		/* 7937   15874  31748  47622  63494  */
	} else
		return -EINVAL;
#endif
	/*
	 * Step 3:
	 *  combine values
	 */
	hw->clock_mult = best_mult - 1;
	hw->clock_div  = div2 << 6 | (best_div1 - 1);

	return 0;
}

/*
 * Decode the info required for the hardware.
 * This involves the PLL parameters for the dot clock,
 * CRTC registers, and accelerator settings.
 */
static int
cyber2000fb_decode_var(struct fb_var_screeninfo *var, struct cfb_info *cfb,
		       struct par_info *hw)
{
	int err;

	hw->width = var->xres_virtual;

	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:	/* PSEUDOCOLOUR, 256 */
		hw->pixformat		= PIXFORMAT_8BPP;
		hw->visualid		= VISUALID_256;
		hw->pitch		= hw->width >> 3;
		hw->palette_ctrl	= 0x04;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:/* DIRECTCOLOUR, 64k */
#ifndef CFB16_IS_CFB15
		hw->pixformat		= PIXFORMAT_16BPP;
		hw->visualid		= VISUALID_64K;
		hw->pitch		= hw->width >> 2;
		hw->palette_ctrl	= 0x14;
		break;
#endif
	case 15:/* DIRECTCOLOUR, 32k */
		hw->pixformat		= PIXFORMAT_16BPP;
		hw->visualid		= VISUALID_32K;
		hw->pitch		= hw->width >> 2;
		hw->palette_ctrl	= 0x14;
		break;

#endif
#ifdef FBCON_HAS_CFB24
	case 24:/* TRUECOLOUR, 16m */
		hw->pixformat		= PIXFORMAT_24BPP;
		hw->visualid		= VISUALID_16M;
		hw->width		*= 3;
		hw->pitch		= hw->width >> 3;
		hw->palette_ctrl	= 0x14;
		break;
#endif
	default:
		return -EINVAL;
	}

	err = cyber2000fb_decode_clock(hw, cfb, var);
	if (err)
		return err;

	err = cyber2000fb_decode_crtc(hw, cfb, var);
	if (err)
		return err;

	hw->width -= 1;
	hw->fetch = hw->pitch;
	if (!(cfb->mem_ctl2 & MEM_CTL2_64BIT))
		hw->fetch <<= 1;
	hw->fetch += 1;

	return 0;
}

/*
 *    Set the User Defined Part of the Display
 */
static int
cyber2000fb_set_var(struct fb_var_screeninfo *var, int con,
		    struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	struct display *display;
	struct par_info hw;
	int err, chgvar = 0;

	/*
	 * CONUPDATE and SMOOTH_XPAN are equal.  However,
	 * SMOOTH_XPAN is only used internally by fbcon.
	 */
	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = cfb->fb.var.xoffset;
		var->yoffset = cfb->fb.var.yoffset;
	}

	err = cyber2000fb_decode_var(var, (struct cfb_info *)info, &hw);
	if (err)
		return err;

	if (var->activate & FB_ACTIVATE_TEST)
		return 0;

	if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW)
		return -EINVAL;

	if (cfb->fb.var.xres != var->xres)
		chgvar = 1;
	if (cfb->fb.var.yres != var->yres)
		chgvar = 1;
	if (cfb->fb.var.xres_virtual != var->xres_virtual)
		chgvar = 1;
	if (cfb->fb.var.yres_virtual != var->yres_virtual)
		chgvar = 1;
	if (cfb->fb.var.bits_per_pixel != var->bits_per_pixel)
		chgvar = 1;

	if (con < 0) {
		display = cfb->fb.disp;
		chgvar = 0;
	} else {
		display = fb_display + con;
	}

	var->red.msb_right	= 0;
	var->green.msb_right	= 0;
	var->blue.msb_right	= 0;

	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:	/* PSEUDOCOLOUR, 256 */
		var->red.offset		= 0;
		var->red.length		= 8;
		var->green.offset	= 0;
		var->green.length	= 8;
		var->blue.offset	= 0;
		var->blue.length	= 8;

		cfb->fb.fix.visual	= FB_VISUAL_PSEUDOCOLOR;
		cfb->dispsw		= &fbcon_cfb8;
		display->dispsw_data	= NULL;
		display->next_line	= var->xres_virtual;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:/* DIRECTCOLOUR, 64k */
#ifndef CFB16_IS_CFB15
		var->bits_per_pixel	= 15;
		var->red.offset		= 11;
		var->red.length		= 5;
		var->green.offset	= 5;
		var->green.length	= 6;
		var->blue.offset	= 0;
		var->blue.length	= 5;

		cfb->fb.fix.visual	= FB_VISUAL_DIRECTCOLOR;
		cfb->dispsw		= &fbcon_cfb16;
		display->dispsw_data	= cfb->fb.pseudo_palette;
		display->next_line	= var->xres_virtual * 2;
		break;
#endif
	case 15:/* DIRECTCOLOUR, 32k */
		var->bits_per_pixel	= 15;
		var->red.offset		= 10;
		var->red.length		= 5;
		var->green.offset	= 5;
		var->green.length	= 5;
		var->blue.offset	= 0;
		var->blue.length	= 5;

		cfb->fb.fix.visual	= FB_VISUAL_DIRECTCOLOR;
		cfb->dispsw		= &fbcon_cfb16;
		display->dispsw_data	= cfb->fb.pseudo_palette;
		display->next_line	= var->xres_virtual * 2;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:/* TRUECOLOUR, 16m */
		var->red.offset		= 16;
		var->red.length		= 8;
		var->green.offset	= 8;
		var->green.length	= 8;
		var->blue.offset	= 0;
		var->blue.length	= 8;

		cfb->fb.fix.visual	= FB_VISUAL_TRUECOLOR;
		cfb->dispsw		= &fbcon_cfb24;
		display->dispsw_data	= cfb->fb.pseudo_palette;
		display->next_line	= var->xres_virtual * 3;
		break;
#endif
	default:/* in theory this should never happen */
		printk(KERN_WARNING "%s: no support for %dbpp\n",
		       cfb->fb.fix.id, var->bits_per_pixel);
		cfb->dispsw = &fbcon_dummy;
		break;
	}

	if (var->accel_flags & FB_ACCELF_TEXT && cfb->dispsw != &fbcon_dummy)
		display->dispsw = &fbcon_cyber_accel;
	else
		display->dispsw = cfb->dispsw;

	cfb->fb.fix.line_length	= display->next_line;

	display->screen_base	= cfb->fb.screen_base;
	display->line_length	= cfb->fb.fix.line_length;
	display->visual		= cfb->fb.fix.visual;
	display->type		= cfb->fb.fix.type;
	display->type_aux	= cfb->fb.fix.type_aux;
	display->ypanstep	= cfb->fb.fix.ypanstep;
	display->ywrapstep	= cfb->fb.fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse	= 0;

	cfb->fb.var = *var;
	cfb->fb.var.activate &= ~FB_ACTIVATE_ALL;

	/*
	 * Update the old var.  The fbcon drivers still use this.
	 * Once they are using cfb->fb.var, this can be dropped.
	 *					--rmk
	 */
	display->var = cfb->fb.var;

	/*
	 * If we are setting all the virtual consoles, also set the
	 * defaults used to create new consoles.
	 */
	if (var->activate & FB_ACTIVATE_ALL)
		cfb->fb.disp->var = cfb->fb.var;

	if (chgvar && info && cfb->fb.changevar)
		cfb->fb.changevar(con);

	cyber2000fb_update_start(cfb, var);
	cyber2000fb_set_timing(&hw);
	fb_set_cmap(&cfb->fb.cmap, 1, cyber2000_setcolreg, &cfb->fb);

	return 0;
}


/*
 *    Pan or Wrap the Display
 */
static int
cyber2000fb_pan_display(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	u_int y_bottom;

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (y_bottom > cfb->fb.var.yres_virtual)
		return -EINVAL;

	if (cyber2000fb_update_start(cfb, var))
		return -EINVAL;

	cfb->fb.var.xoffset = var->xoffset;
	cfb->fb.var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP) {
		cfb->fb.var.vmode |= FB_VMODE_YWRAP;
	} else {
		cfb->fb.var.vmode &= ~FB_VMODE_YWRAP;
	}

	return 0;
}


static int
cyber2000fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		  u_long arg, int con, struct fb_info *info)
{
	return -EINVAL;
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */
static int cyber2000fb_updatevar(int con, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;

	return cyber2000fb_update_start(cfb, &fb_display[con].var);
}

static int cyber2000fb_switch(int con, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	struct display *disp;
	struct fb_cmap *cmap;

	if (cfb->currcon >= 0) {
		disp = fb_display + cfb->currcon;

		/*
		 * Save the old colormap and video mode.
		 */
		disp->var = cfb->fb.var;
		if (disp->cmap.len)
			fb_copy_cmap(&cfb->fb.cmap, &disp->cmap, 0);
	}

	cfb->currcon = con;
	disp = fb_display + con;

	/*
	 * Install the new colormap and change the video mode.  By default,
	 * fbcon sets all the colormaps and video modes to the default
	 * values at bootup.
	 *
	 * Really, we want to set the colourmap size depending on the
	 * depth of the new video mode.  For now, we leave it at its
	 * default 256 entry.
	 */
	if (disp->cmap.len)
		cmap = &disp->cmap;
	else
		cmap = fb_default_cmap(1 << disp->var.bits_per_pixel);

	fb_copy_cmap(cmap, &cfb->fb.cmap, 0);

	cfb->fb.var = disp->var;
	cfb->fb.var.activate = FB_ACTIVATE_NOW;

	cyber2000fb_set_var(&cfb->fb.var, con, &cfb->fb);

	return 0;
}

/*
 *    (Un)Blank the display.
 */
static void cyber2000fb_blank(int blank, struct fb_info *info)
{
	struct cfb_info *cfb = (struct cfb_info *)info;
	int i;

	if (blank) {
		for (i = 0; i < NR_PALETTE; i++) {
			cyber2000_outb(i, 0x3c8);
			cyber2000_outb(0, 0x3c9);
			cyber2000_outb(0, 0x3c9);
			cyber2000_outb(0, 0x3c9);
		}
	} else {
		for (i = 0; i < NR_PALETTE; i++) {
			cyber2000_outb(i, 0x3c8);
			cyber2000_outb(cfb->palette[i].red, 0x3c9);
			cyber2000_outb(cfb->palette[i].green, 0x3c9);
			cyber2000_outb(cfb->palette[i].blue, 0x3c9);
		}
	}
}

/*
 * Get the currently displayed virtual consoles colormap.
 */
static int
gen_get_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	fb_copy_cmap(&info->cmap, cmap, kspc ? 0 : 2);
	return 0;
}

/*
 * Get the currently displayed virtual consoles fixed part of the display.
 */
static int
gen_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	*fix = info->fix;
	return 0;
}

/*
 * Get the current user defined part of the display.
 */
static int
gen_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	*var = info->var;
	return 0;
}

static struct fb_ops cyber2000fb_ops =
{
	fb_open:	cyber2000fb_open,
	fb_release:	cyber2000fb_release,
	fb_set_var:	cyber2000fb_set_var,
	fb_set_cmap:	cyber2000fb_set_cmap,
	fb_pan_display:	cyber2000fb_pan_display,
	fb_ioctl:	cyber2000fb_ioctl,
	fb_get_fix:	gen_get_fix,
	fb_get_var:	gen_get_var,
	fb_get_cmap:	gen_get_cmap,
};

/*
 * Enable access to the extended registers
 *  Bug: this should track the usage of these registers
 */
static void cyber2000fb_enable_extregs(void)
{
	int old;

	old = cyber2000_grphr(FUNC_CTL);
	cyber2000_grphw(FUNC_CTL, old | FUNC_CTL_EXTREGENBL);
}

/*
 * Disable access to the extended registers
 *  Bug: this should track the usage of these registers
 */
static void cyber2000fb_disable_extregs(void)
{
	int old;

	old = cyber2000_grphr(FUNC_CTL);
	cyber2000_grphw(FUNC_CTL, old & ~FUNC_CTL_EXTREGENBL);
}

/*
 * This is the only "static" reference to the internal data structures
 * of this driver.  It is here solely at the moment to support the other
 * CyberPro modules external to this driver.
 */
static struct cfb_info		*int_cfb_info;

/*
 * Attach a capture/tv driver to the core CyberX0X0 driver.
 */
int cyber2000fb_attach(struct cyberpro_info *info)
{
	if (int_cfb_info != NULL) {
		info->dev	      = int_cfb_info->dev;
		info->regs	      = CyberRegs;
		info->fb	      = int_cfb_info->fb.screen_base;
		info->fb_size	      = int_cfb_info->fb.fix.smem_len;
		info->enable_extregs  = cyber2000fb_enable_extregs;
		info->disable_extregs = cyber2000fb_disable_extregs;

		strncpy(info->dev_name, int_cfb_info->fb.fix.id, sizeof(info->dev_name));

		MOD_INC_USE_COUNT;
	}

	return int_cfb_info != NULL;
}

/*
 * Detach a capture/tv driver from the core CyberX0X0 driver.
 */
void cyber2000fb_detach(void)
{
	MOD_DEC_USE_COUNT;
}

EXPORT_SYMBOL(cyber2000fb_attach);
EXPORT_SYMBOL(cyber2000fb_detach);

/*
 * These parameters give
 * 640x480, hsync 31.5kHz, vsync 60Hz
 */
static struct fb_videomode __devinitdata cyber2000fb_default_mode = {
	refresh:	60,
	xres:		640,
	yres:		480,
	pixclock:	39722,
	left_margin:	56,
	right_margin:	16,
	upper_margin:	34,
	lower_margin:	9,
	hsync_len:	88,
	vsync_len:	2,
	sync:		FB_SYNC_COMP_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	vmode:		FB_VMODE_NONINTERLACED
};

int __init cyber2000fb_setup(char *options)
{
	return 0;
}

static char igs_regs[] __devinitdata = {
	0x10, 0x10,			0x12, 0x00,	0x13, 0x00,
			0x31, 0x00,	0x32, 0x00,	0x33, 0x01,
	0x50, 0x00,	0x51, 0x00,	0x52, 0x00,	0x53, 0x00,
	0x54, 0x00,	0x55, 0x00,	0x56, 0x00,	0x57, 0x01,
	0x58, 0x00,	0x59, 0x00,	0x5a, 0x00,
	0x70, 0x0b,					0x73, 0x30,
	0x74, 0x0b,	0x75, 0x17,	0x76, 0x00,	0x7a, 0xc8
};

static inline void cyberpro_init_hw(struct cfb_info *cfb)
{
	int i;

	/*
	 * Wake up the CyberPro
	 */
	cyber2000_outb(0x18, 0x46e8);
	cyber2000_outb(0x01, 0x102);
	cyber2000_outb(0x08, 0x46e8);

	/*
	 * Initialise the CyberPro
	 */
	for (i = 0; i < sizeof(igs_regs); i += 2)
		cyber2000_grphw(igs_regs[i], igs_regs[i+1]);
}

static struct cfb_info * __devinit
cyberpro_alloc_fb_info(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cfb_info *cfb;

	cfb = kmalloc(sizeof(struct cfb_info) + sizeof(struct display) +
		       sizeof(u32) * 16, GFP_KERNEL);

	if (!cfb)
		return NULL;

	memset(cfb, 0, sizeof(struct cfb_info) + sizeof(struct display));

	cfb->currcon		= -1;
	cfb->dev		= dev;
	cfb->divisors[0]	= 1;
	cfb->divisors[1]	= 2;
	cfb->divisors[2]	= 4;

	if (id->driver_data == FB_ACCEL_IGS_CYBER2010)
		cfb->divisors[3] = 6;
	else
		cfb->divisors[3] = 8;

	sprintf(cfb->fb.fix.id, "CyberPro%4X", id->device);

	cfb->fb.fix.type	= FB_TYPE_PACKED_PIXELS;
	cfb->fb.fix.type_aux	= 0;
	cfb->fb.fix.xpanstep	= 0;
	cfb->fb.fix.ypanstep	= 1;
	cfb->fb.fix.ywrapstep	= 0;
	cfb->fb.fix.accel	= id->driver_data;

	cfb->fb.var.nonstd	= 0;
	cfb->fb.var.activate	= FB_ACTIVATE_NOW;
	cfb->fb.var.height	= -1;
	cfb->fb.var.width	= -1;
	cfb->fb.var.accel_flags	= FB_ACCELF_TEXT;

	strcpy(cfb->fb.modename, cfb->fb.fix.id);
	strcpy(cfb->fb.fontname, "Acorn8x8");

	cfb->fb.fbops		= &cyber2000fb_ops;
	cfb->fb.changevar	= NULL;
	cfb->fb.switch_con	= cyber2000fb_switch;
	cfb->fb.updatevar	= cyber2000fb_updatevar;
	cfb->fb.blank		= cyber2000fb_blank;
	cfb->fb.flags		= FBINFO_FLAG_DEFAULT;
	cfb->fb.disp		= (struct display *)(cfb + 1);
	cfb->fb.pseudo_palette	= (void *)(cfb->fb.disp + 1);

	fb_alloc_cmap(&cfb->fb.cmap, NR_PALETTE, 0);

	return cfb;
}

static void __devinit
cyberpro_free_fb_info(struct cfb_info *cfb)
{
	if (cfb) {
		/*
		 * Free the colourmap
		 */
		fb_alloc_cmap(&cfb->fb.cmap, 0, 0);

		kfree(cfb);
	}
}

/*
 * Map in the registers
 */
static int __devinit
cyberpro_map_mmio(struct cfb_info *cfb, struct pci_dev *dev)
{
	u_long mmio_base;

	mmio_base = pci_resource_start(dev, 0) + MMIO_OFFSET;

	cfb->fb.fix.mmio_start = mmio_base + PCI_PHYS_OFFSET;
	cfb->fb.fix.mmio_len   = MMIO_SIZE;

	if (!request_mem_region(mmio_base, MMIO_SIZE, "memory mapped I/O")) {
		printk("%s: memory mapped IO in use\n", cfb->fb.fix.id);
		return -EBUSY;
	}

	CyberRegs = ioremap(mmio_base, MMIO_SIZE);
	if (!CyberRegs) {
		printk("%s: unable to map memory mapped IO\n",
		       cfb->fb.fix.id);
		return -ENOMEM;
	}
	return 0;
}

/*
 * Unmap registers
 */
static void __devinit cyberpro_unmap_mmio(struct cfb_info *cfb)
{
	if (cfb && CyberRegs) {
		iounmap(CyberRegs);
		CyberRegs = NULL;

		release_mem_region(cfb->fb.fix.mmio_start - PCI_PHYS_OFFSET,
				   cfb->fb.fix.mmio_len);
	}
}

/*
 * Map in screen memory
 */
static int __devinit
cyberpro_map_smem(struct cfb_info *cfb, struct pci_dev *dev, u_long smem_len)
{
	u_long smem_base;

	smem_base = pci_resource_start(dev, 0);

	cfb->fb.fix.smem_start	= smem_base + PCI_PHYS_OFFSET;
	cfb->fb.fix.smem_len	= smem_len;

	if (!request_mem_region(smem_base, smem_len, "frame buffer")) {
		printk("%s: frame buffer in use\n",
		       cfb->fb.fix.id);
		return -EBUSY;
	}

	cfb->fb.screen_base = ioremap(smem_base, smem_len);
	if (!cfb->fb.screen_base) {
		printk("%s: unable to map screen memory\n",
		       cfb->fb.fix.id);
		return -ENOMEM;
	}

	return 0;
}

static void __devinit cyberpro_unmap_smem(struct cfb_info *cfb)
{
	if (cfb && cfb->fb.screen_base) {
		iounmap(cfb->fb.screen_base);
		cfb->fb.screen_base = NULL;

		release_mem_region(cfb->fb.fix.smem_start - PCI_PHYS_OFFSET,
				   cfb->fb.fix.smem_len);
	}
}

static int __devinit
cyberpro_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct cfb_info *cfb;
	u_int h_sync, v_sync;
	u_long smem_size;
	int err;

	/*
	 * We can only accept one CyberPro device at the moment.  We can
	 * kill this once int_cfb_info and CyberRegs have been killed.
	 */
	if (int_cfb_info)
		return -EBUSY;

	err = pci_enable_device(dev);
	if (err)
		return err;

	err = -ENOMEM;
	cfb = cyberpro_alloc_fb_info(dev, id);
	if (!cfb)
		goto failed;

	err = cyberpro_map_mmio(cfb, dev);
	if (err)
		goto failed;

	cyberpro_init_hw(cfb);

	/*
	 * get the video RAM size and width from the VGA register.
	 * This should have been already initialised by the BIOS,
	 * but if it's garbage, claim default 1MB VRAM (woody)
	 */
	cfb->mem_ctl2 = cyber2000_grphr(MEM_CTL2);

	switch (cfb->mem_ctl2 & MEM_CTL2_SIZE_MASK) {
	case MEM_CTL2_SIZE_4MB:	smem_size = 0x00400000; break;
	case MEM_CTL2_SIZE_2MB:	smem_size = 0x00200000; break;
	default:		smem_size = 0x00100000; break;
	}

	err = cyberpro_map_smem(cfb, dev, smem_size);
	if (err)
		goto failed;

	if (!fb_find_mode(&cfb->fb.var, &cfb->fb, NULL, NULL, 0,
	    		  &cyber2000fb_default_mode, 8)) {
		printk("%s: no valid mode found\n", cfb->fb.fix.id);
		goto failed;
	}

	cfb->fb.var.yres_virtual = cfb->fb.fix.smem_len * 8 /
			(cfb->fb.var.bits_per_pixel * cfb->fb.var.xres_virtual);

	if (cfb->fb.var.yres_virtual < cfb->fb.var.yres)
		cfb->fb.var.yres_virtual = cfb->fb.var.yres;

	cyber2000fb_set_var(&cfb->fb.var, -1, &cfb->fb);

	h_sync = 1953125000 / cfb->fb.var.pixclock;
	h_sync = h_sync * 512 / (cfb->fb.var.xres + cfb->fb.var.left_margin +
		 cfb->fb.var.right_margin + cfb->fb.var.hsync_len);
	v_sync = h_sync / (cfb->fb.var.yres + cfb->fb.var.upper_margin +
		 cfb->fb.var.lower_margin + cfb->fb.var.vsync_len);

	printk(KERN_INFO "%s: %dkB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
		cfb->fb.fix.id, cfb->fb.fix.smem_len >> 10,
		cfb->fb.var.xres, cfb->fb.var.yres,
		h_sync / 1000, h_sync % 1000, v_sync);

	err = register_framebuffer(&cfb->fb);
	if (err < 0)
		goto failed;

	/*
	 * Our driver data
	 */
	dev->driver_data = cfb;
	int_cfb_info = cfb;

	return 0;

failed:
	cyberpro_unmap_smem(cfb);
	cyberpro_unmap_mmio(cfb);
	cyberpro_free_fb_info(cfb);

	return err;
}

static void __devexit cyberpro_remove(struct pci_dev *dev)
{
	struct cfb_info *cfb = (struct cfb_info *)dev->driver_data;

	if (cfb) {
		unregister_framebuffer(&cfb->fb);
		cyberpro_unmap_smem(cfb);
		cyberpro_unmap_mmio(cfb);
		cyberpro_free_fb_info(cfb);

		/*
		 * Ensure that the driver data is no longer
		 * valid.
		 */
		dev->driver_data = NULL;
		int_cfb_info = NULL;
	}
}

static void cyberpro_suspend(struct pci_dev *dev)
{
}

/*
 * Re-initialise the CyberPro hardware
 */
static void cyberpro_resume(struct pci_dev *dev)
{
	struct cfb_info *cfb = (struct cfb_info *)dev->driver_data;

	if (cfb) {
		cyberpro_init_hw(cfb);

		/*
		 * Reprogram the MEM_CTL2 register
		 */
		cyber2000_grphw(MEM_CTL2, cfb->mem_ctl2);

		/*
		 * Restore the old video mode and the palette.
		 * We also need to tell fbcon to redraw the console.
		 */
		cfb->fb.var.activate = FB_ACTIVATE_NOW;
		cyber2000fb_set_var(&cfb->fb.var, -1, &cfb->fb);
	}
}

static struct pci_device_id cyberpro_pci_table[] __devinitdata = {
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2000,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_IGS_CYBER2000 },
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_2010,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_IGS_CYBER2010 },
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_5000,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_IGS_CYBER5000 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, cyberpro_pci_table);

static struct pci_driver cyberpro_driver = {
	name:		"CyberPro",
	probe:		cyberpro_probe,
	remove:		cyberpro_remove,
	suspend:	cyberpro_suspend,
	resume:		cyberpro_resume,
	id_table:	cyberpro_pci_table
};

/*
 * I don't think we can use the "module_init" stuff here because
 * the fbcon stuff may not be initialised yet.
 */
int __init cyber2000fb_init(void)
{
	return pci_module_init(&cyberpro_driver);
}

static void __exit cyberpro_exit(void)
{
	pci_unregister_driver(&cyberpro_driver);
}

#ifdef MODULE
module_init(cyber2000fb_init);
#endif
module_exit(cyberpro_exit);
