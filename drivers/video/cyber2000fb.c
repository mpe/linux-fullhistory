/*
 * linux/drivers/video/cyber2000fb.c
 *
 * Integraphics Cyber2000 frame buffer device
 *
 * Based on cyberfb.c
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
 *    Some defaults
 */
#define DEFAULT_XRES	640
#define DEFAULT_YRES	480
#define DEFAULT_BPP	8

#define MMIO_SIZE	0x000c0000

static char			*CyberRegs;

#include "cyber2000fb.h"

static struct display		global_disp;
static struct fb_info		fb_info;
static struct cyber2000fb_par	current_par;
static struct display_switch	*dispsw;
static struct fb_var_screeninfo __initdata init_var = {};

#if defined(DEBUG) && defined(CONFIG_DEBUG_LL)
static void debug_printf(char *fmt, ...)
{
	char buffer[128];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buffer, fmt, ap);
	va_end(ap);

	printascii(buffer);
}
#else
#define debug_printf(x...) do { } while (0)
#endif

/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Cyber2000 Acceleration
 */
static void cyber2000_accel_wait(void)
{
	int count = 10000;

	while (cyber2000_inb(CO_REG_CONTROL) & 0x80) {
		if (!count--) {
			debug_printf("accel_wait timed out\n");
			cyber2000_outb(0, CO_REG_CONTROL);
			return;
		}
		udelay(10);
	}
}

static void
cyber2000_accel_setup(struct display *p)
{
	dispsw->setup(p);
}

static void
cyber2000_accel_bmove(struct display *p, int sy, int sx, int dy, int dx,
		int height, int width)
{
	unsigned long src, dst;
	unsigned int fh, fw;
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

	src    = sx + sy * p->var.xres_virtual;
	dst    = dx + dy * p->var.xres_virtual;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,  CO_REG_CONTROL);
	cyber2000_outb(0x03,  CO_REG_FORE_MIX);
	cyber2000_outw(width, CO_REG_WIDTH);

	if (p->var.bits_per_pixel != 24) {
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
	unsigned long dst;
	unsigned int fw, fh;
	u32 bgx = attr_bgcol_ec(p, conp);

	fw = fontwidth(p);
	fh = fontheight(p);

	dst    = sx * fw + sy * p->var.xres_virtual * fh;
	width  = width * fw - 1;
	height = height * fh - 1;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,   CO_REG_CONTROL);
	cyber2000_outb(0x03,   CO_REG_FORE_MIX);
	cyber2000_outw(width,  CO_REG_WIDTH);
	cyber2000_outw(height, CO_REG_HEIGHT);

	switch (p->var.bits_per_pixel) {
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
cyber2000_accel_putc(struct vc_data *conp, struct display *p, int c, int yy, int xx)
{
	cyber2000_accel_wait();
	dispsw->putc(conp, p, c, yy, xx);
}

static void
cyber2000_accel_putcs(struct vc_data *conp, struct display *p,
		const unsigned short *s, int count, int yy, int xx)
{
	cyber2000_accel_wait();
	dispsw->putcs(conp, p, s, count, yy, xx);
}

static void
cyber2000_accel_revc(struct display *p, int xx, int yy)
{
	cyber2000_accel_wait();
	dispsw->revc(p, xx, yy);
}

static void
cyber2000_accel_clear_margins(struct vc_data *conp, struct display *p, int bottom_only)
{
	dispsw->clear_margins(conp, p, bottom_only);
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
 * Palette
 */
static int
cyber2000_getcolreg(u_int regno, u_int * red, u_int * green, u_int * blue,
		    u_int * transp, struct fb_info *fb_info)
{
	int t;

	if (regno >= 256)
		return 1;

	t = current_par.palette[regno].red;
	*red = t << 10 | t << 4 | t >> 2;

	t = current_par.palette[regno].green;
	*green = t << 10 | t << 4 | t >> 2;

	t = current_par.palette[regno].blue;
	*blue = t << 10 | t << 4 | t >> 2;

	*transp = 0;

	return 0;
}

/*
 *    Set a single color register. Return != 0 for invalid regno.
 */
static int
cyber2000_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		    u_int transp, struct fb_info *fb_info)
{
	if (regno > 255)
		return 1;

	red   >>= 10;
	green >>= 10;
	blue  >>= 10;

	current_par.palette[regno].red   = red;
	current_par.palette[regno].green = green;
	current_par.palette[regno].blue  = blue;

	switch (fb_display[current_par.currcon].var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		cyber2000_outb(regno, 0x3c8);
		cyber2000_outb(red,   0x3c9);
		cyber2000_outb(green, 0x3c9);
		cyber2000_outb(blue,  0x3c9);
		break;
#endif

#ifdef FBCON_HAS_CFB16
	case 15:
		if (regno < 32) {
			cyber2000_outb(regno << 3, 0x3c8);
			cyber2000_outb(red, 0x3c9);
			cyber2000_outb(green, 0x3c9);
			cyber2000_outb(blue, 0x3c9);
		}
		if (regno < 16)
			current_par.c_table.cfb16[regno] = regno | regno << 5 | regno << 10;
		break;

	case 16:
		if (regno < 64) {
			/* write green */
			cyber2000_outb(regno << 2, 0x3c8);
			cyber2000_outb(current_par.palette[regno >> 1].red, 0x3c9);
			cyber2000_outb(green, 0x3c9);
			cyber2000_outb(current_par.palette[regno >> 1].blue, 0x3c9);
		}

		if (regno < 32) {
			/* write red,blue */
			cyber2000_outb(regno << 3, 0x3c8);
			cyber2000_outb(red, 0x3c9);
			cyber2000_outb(current_par.palette[regno << 1].green, 0x3c9);
			cyber2000_outb(blue, 0x3c9);
		}

		if (regno < 16)
			current_par.c_table.cfb16[regno] = regno | regno << 5 | regno << 11;
		break;
#endif

#ifdef FBCON_HAS_CFB24
	case 24:
		cyber2000_outb(regno, 0x3c8);
		cyber2000_outb(red,   0x3c9);
		cyber2000_outb(green, 0x3c9);
		cyber2000_outb(blue,  0x3c9);

		if (regno < 16)
			current_par.c_table.cfb24[regno] = regno | regno << 8 | regno << 16;
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
	unsigned char	clock_mult;
	unsigned char	clock_div;
	unsigned char	visualid;
	unsigned char	pixformat;
	unsigned char	crtc_ofl;
	unsigned char	crtc[19];
	unsigned int	width;
	unsigned int	pitch;

	/*
	 * Other
	 */
	unsigned int	visual;
};

static const char crtc_idx[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
};

static void cyber2000fb_set_timing(struct par_info *hw)
{
	unsigned int fetchrow, i;

	/*
	 * Blank palette
	 */
	for (i = 0; i < 256; i++) {
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
	cyber2000_outb(0x04, 0x3c6);
	cyber2000_outb(i,    0x3cf);

	cyber2000_outb(0x20, 0x3c0);
	cyber2000_outb(0xff, 0x3c6);

	fetchrow = hw->pitch + 1;
	cyber2000_grphw(0x14, fetchrow);
	/* FIXME: is this the right way round? */
	cyber2000_grphw(0x15, ((fetchrow >> 4) & 0xf0) | ((hw->pitch >> 8) & 0x0f));
	cyber2000_grphw(0x77, hw->visualid);
	cyber2000_grphw(0x33, 0x1c);

	/*
	 * Set up accelerator registers
	 */
	cyber2000_outw(hw->width, CO_REG_SRC_WIDTH);
	cyber2000_outw(hw->width, CO_REG_DEST_WIDTH);
	cyber2000_outb(hw->pixformat, CO_REG_PIX_FORMAT);
}

static inline int
cyber2000fb_update_start(struct fb_var_screeninfo *var)
{
	unsigned int base;

	base = var->yoffset * var->xres_virtual + var->xoffset;

	base >>= 2;

	if (base >= 1 << 20)
		return -EINVAL;

	/*
	 * FIXME: need the upper bits of the start offset
	 */
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
 *    Get the Colormap
 */
static int
cyber2000fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
		     struct fb_info *info)
{
	int err = 0;

	if (con == current_par.currcon)	/* current console? */
		err = fb_get_cmap(cmap, kspc, cyber2000_getcolreg, info);
	else if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(1 << fb_display[con].var.bits_per_pixel),
			     cmap, kspc ? 0 : 2);
	return err;
}


/*
 *    Set the Colormap
 */
static int
cyber2000fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		     struct fb_info *info)
{
	struct display *disp = &fb_display[con];
	int err = 0;

	if (!disp->cmap.len) {	/* no colormap allocated? */
		int size;

		if (disp->var.bits_per_pixel == 16)
			size = 32;
		else
			size = 256;

		err = fb_alloc_cmap(&disp->cmap, size, 0);
	}
	if (!err) {
		if (con == current_par.currcon)	/* current console? */
			err = fb_set_cmap(cmap, kspc, cyber2000_setcolreg,
					  info);
		else
			fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
	}

	return err;
}

static int cyber2000fb_decode_crtc(struct par_info *hw, struct fb_var_screeninfo *var)
{
	unsigned int Htotal, Hblankend, Hsyncend;
	unsigned int Vtotal, Vdispend, Vblankstart, Vblankend, Vsyncstart, Vsyncend;
#define BIT(v,b1,m,b2) (((v >> b1) & m) << b2)

	hw->crtc[13] = hw->pitch;
	hw->crtc[17] = 0xe3;
	hw->crtc[14] = 0;
	hw->crtc[8]  = 0;

	Htotal      = var->xres + var->right_margin + var->hsync_len + var->left_margin;
	if (Htotal > 2080)
		return -EINVAL;

	hw->crtc[0] = (Htotal >> 3) - 5;			/* Htotal	*/
	hw->crtc[1] = (var->xres >> 3) - 1;			/* Hdispend	*/
	hw->crtc[2] = var->xres >> 3;				/* Hblankstart	*/
	hw->crtc[4] = (var->xres + var->right_margin) >> 3;	/* Hsyncstart	*/

	Hblankend   = (Htotal - 4*8) >> 3;

	hw->crtc[3] = BIT(Hblankend,  0, 0x1f,  0) |		/* Hblankend	*/
		      BIT(1,          0, 0x01,  7);

	Hsyncend    = (var->xres + var->right_margin + var->hsync_len) >> 3;

	hw->crtc[5] = BIT(Hsyncend,   0, 0x1f,  0) |		/* Hsyncend	*/
		      BIT(Hblankend,  5, 0x01,  7);

	Vdispend    = var->yres - 1;
	Vsyncstart  = var->yres + var->lower_margin;
	Vsyncend    = var->yres + var->lower_margin + var->vsync_len;
	Vtotal      = var->yres + var->lower_margin + var->vsync_len + var->upper_margin - 2;

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
/* 0=VTOTAL:10 1=VDEND:10 2=VRSTART:10 3=VBSTART:10 4=LINECOMP:10 5-IVIDEO 6=FIXCNT */
	hw->crtc_ofl =
		BIT(Vtotal,     10, 0x01,  0) |
		BIT(Vdispend,   10, 0x01,  1) |
		BIT(Vsyncstart, 10, 0x01,  2) |
		BIT(Vblankstart,10, 0x01,  3) |
		1 << 4;

	return 0;
}

/*
 * The following was discovered by a good monitor,
 * bit twiddling, theorising and but mostly luck.
 * Strangely, it looks like everyone elses' PLL!
 *
 * Clock registers:
 *   fclock = fpll / div2
 *   fpll   = fref * mult / div1
 * where:
 *   fref = 14.318MHz (69842ps)
 *   mult = reg0xb0.7:0
 *   div1 = (reg0xb1.5:0 + 1)
 *   div2 =  2^(reg0xb1.7:6)
 *   fpll should be between 150 and 220 MHz
 *  (6667ps and 4545ps)
 */
static int
cyber2000fb_decode_clock(struct par_info *hw, struct fb_var_screeninfo *var)
{
	unsigned long pll_ps = var->pixclock;
	unsigned long ref_ps = 69842;
	int div2, div1, mult;

	/*
	 * Step 1:
	 *   find div2 such that 150MHz < fpll < 220MHz
	 *   and 0 <= div2 < 4
	 */
	for (div2 = 0; div2 < 4; div2++, pll_ps >>= 1)
		if (6667 > pll_ps && pll_ps > 4545)
			break;

	if (div2 == 4)
		return -EINVAL;

#if 0
	/*
	 * Step 2:
	 *  Find fpll
	 *    fpll = fref * mult / div1
	 *
	 * Note!  This just picks any old values at the moment,
	 * and as such I don't trust it.  It certainly doesn't
	 * come out with the values below, so the PLL may become
	 * unstable under some circumstances (you don't want an
	 * FM dot clock)
	 */
	for (div1 = 32; div1 > 1; div1 -= 1) {
		mult = (ref_ps * div1 + pll_ps / 2) / pll_ps;
		if (mult < 256)
			break;
	}
#else
	if (pll_ps == 4630) {		/* 216.0, 108.0, 54.00, 27.000 */
		mult = 181;		/* 4630   9260   18520  37040  */
		div1 = 12;
	} else if (pll_ps == 4965) {	/* 201.0, 100.5, 50.25, 25.125 */
		mult = 211;		/* 4965   9930   19860  39720  */
		div1 = 15;
	} else if (pll_ps == 5050) {	/* 198.0,  99.0, 49.50, 24.750 */
		mult = 83;		/* 5050   10100  20200  40400  */
		div1 = 6;
	} else if (pll_ps == 6349) {	/* 158.0,  79.0, 39.50, 19.750 */
		mult = 209;		/* 6349   12698  25396  50792  */
		div1 = 19;
	} else if (pll_ps == 6422) {	/* 156.0,  78.0, 39.00, 19.500 */
		mult = 190;		/* 6422   12844  25688  51376  */
		div1 = 17;
	} else
		return -EINVAL;
#endif
	/*
	 * Step 3:
	 *  combine values
	 */
	hw->clock_mult = mult - 1;
	hw->clock_div  = div2 << 6 | (div1 - 1);

	return 0;
}

/*
 * Decode the info required for the hardware.
 * This involves the PLL parameters for the dot clock,
 * CRTC registers, and accelerator settings.
 */
static int
cyber2000fb_decode_var(struct fb_var_screeninfo *var, int con, struct par_info *hw)
{
	int err;

	hw->width = var->xres_virtual;
	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:	/* PSEUDOCOLOUR, 256 */
		hw->visual    = FB_VISUAL_PSEUDOCOLOR;
		hw->pixformat = PIXFORMAT_8BPP;
		hw->visualid  = VISUALID_256;
		hw->pitch     = hw->width >> 3;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 15:/* DIRECTCOLOUR, 32k */
		hw->visual    = FB_VISUAL_DIRECTCOLOR;
		hw->pixformat = PIXFORMAT_16BPP;
		hw->visualid  = VISUALID_32K;
		hw->pitch     = hw->width >> 2;
		break;

	case 16:/* DIRECTCOLOUR, 64k */
		hw->visual    = FB_VISUAL_DIRECTCOLOR;
		hw->pixformat = PIXFORMAT_16BPP;
		hw->visualid  = VISUALID_64K;
		hw->pitch     = hw->width >> 2;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:/* TRUECOLOUR, 16m */
		hw->visual    = FB_VISUAL_TRUECOLOR;
		hw->pixformat = PIXFORMAT_24BPP;
		hw->visualid  = VISUALID_16M;
		hw->width    *= 3;
		hw->pitch     = hw->width >> 3;
		break;
#endif
	default:
		return -EINVAL;
	}

	err = cyber2000fb_decode_clock(hw, var);
	if (err)
		return err;

	err = cyber2000fb_decode_crtc(hw, var);
	if (err)
		return err;

	debug_printf("Clock: %02X %02X\n",
		hw->clock_mult, hw->clock_div);
	{
		int i;

		for (i = 0; i < 19; i++)
			debug_printf("%2d ", i);
		debug_printf("\n");
		for (i = 0; i < 18; i++)
			debug_printf("%02X ", hw->crtc[i]);
		debug_printf("%02X\n", hw->crtc_ofl);
	}
	hw->width     -= 1;

	return 0;
}

/*
 *    Get the Fixed Part of the Display
 */
static int
cyber2000fb_get_fix(struct fb_fix_screeninfo *fix, int con,
		    struct fb_info *fb_info)
{
	struct display *display;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, current_par.dev_name);

	if (con >= 0)
		display = fb_display + con;
	else
		display = &global_disp;

	fix->smem_start	 = current_par.screen_base_p;
	fix->smem_len	 = current_par.screen_size;
	fix->mmio_start	 = current_par.regs_base_p;
	fix->mmio_len	 = MMIO_SIZE;
	fix->type	 = display->type;
	fix->type_aux	 = display->type_aux;
	fix->xpanstep	 = 0;
	fix->ypanstep	 = display->ypanstep;
	fix->ywrapstep	 = display->ywrapstep;
	fix->visual	 = display->visual;
	fix->line_length = display->line_length;
	fix->accel	 = 22; /*FB_ACCEL_IGS_CYBER2000*/

	return 0;
}


/*
 *    Get the User Defined Part of the Display
 */
static int
cyber2000fb_get_var(struct fb_var_screeninfo *var, int con,
		    struct fb_info *fb_info)
{
	if (con == -1)
		*var = global_disp.var;
	else
		*var = fb_display[con].var;

	return 0;
}

/*
 *    Set the User Defined Part of the Display
 */
static int
cyber2000fb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct display *display;
	struct par_info hw;
	int err, chgvar = 0;

	if (con >= 0)
		display = fb_display + con;
	else
		display = &global_disp;

	err = cyber2000fb_decode_var(var, con, &hw);
	if (err)
		return err;

	switch (var->activate & FB_ACTIVATE_MASK) {
	case FB_ACTIVATE_TEST:
		return 0;

	case FB_ACTIVATE_NXTOPEN:
	case FB_ACTIVATE_NOW:
		break;

	default:
		return -EINVAL;
	}

	if (con >= 0) {
		if (display->var.xres != var->xres)
			chgvar = 1;
		if (display->var.yres != var->yres)
			chgvar = 1;
		if (display->var.xres_virtual != var->xres_virtual)
			chgvar = 1;
		if (display->var.yres_virtual != var->yres_virtual)
			chgvar = 1;
		if (display->var.accel_flags != var->accel_flags)
			chgvar = 1;
		if (memcmp(&display->var.red, &var->red, sizeof(var->red)))
			chgvar = 1;
		if (memcmp(&display->var.green, &var->green, sizeof(var->green)))
			chgvar = 1;
		if (memcmp(&display->var.blue, &var->blue, sizeof(var->green)))
			chgvar = 1;
	}

	display->var = *var;
	display->var.activate &= ~FB_ACTIVATE_ALL;

	if (var->activate & FB_ACTIVATE_ALL)
		global_disp.var = display->var;

	display->screen_base	= current_par.screen_base;
	display->visual		= hw.visual;
	display->type		= FB_TYPE_PACKED_PIXELS;
	display->type_aux	= 0;
	display->ypanstep	= 1;
	display->ywrapstep	= 0;
	display->can_soft_blank = 1;
	display->inverse	= 0;

	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		dispsw = &fbcon_cfb8;
		display->dispsw_data = NULL;
		display->next_line = var->xres_virtual;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 15:
	case 16:
		dispsw = &fbcon_cfb16;
		display->dispsw_data = current_par.c_table.cfb16;
		display->next_line = var->xres_virtual * 2;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		dispsw = &fbcon_cfb24;
		display->dispsw_data = current_par.c_table.cfb24;
		display->next_line = var->xres_virtual * 3;
		break;
#endif
	default:
		printk(KERN_WARNING "%s: no support for %dbpp\n",
		       current_par.dev_name, display->var.bits_per_pixel);
		dispsw = &fbcon_dummy;
		break;
	}

	display->line_length = display->next_line;

	if (display->var.accel_flags & FB_ACCELF_TEXT &&
	    dispsw != &fbcon_dummy)
		display->dispsw = &fbcon_cyber_accel;
	else
		display->dispsw = dispsw;

	if (chgvar && info && info->changevar)
		info->changevar(con);

	if (con == current_par.currcon) {
		struct fb_cmap *cmap;

		cyber2000fb_update_start(var);
		cyber2000fb_set_timing(&hw);

		if (display->cmap.len)
			cmap = &display->cmap;
		else
			cmap = fb_default_cmap(current_par.palette_size);

		fb_set_cmap(cmap, 1, cyber2000_setcolreg, info);
	}
	return 0;
}


/*
 *    Pan or Wrap the Display
 */
static int cyber2000fb_pan_display(struct fb_var_screeninfo *var, int con,
				   struct fb_info *info)
{
	u_int y_bottom;

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (y_bottom > fb_display[con].var.yres_virtual)
		return -EINVAL;

	if (cyber2000fb_update_start(var))
		return -EINVAL;

	fb_display[con].var.xoffset = var->xoffset;
	fb_display[con].var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		fb_display[con].var.vmode |= FB_VMODE_YWRAP;
	else
		fb_display[con].var.vmode &= ~FB_VMODE_YWRAP;

	return 0;
}


static int cyber2000fb_ioctl(struct inode *inode, struct file *file,
		    u_int cmd, u_long arg, int con, struct fb_info *info)
{
	return -EINVAL;
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */
static int
cyber2000fb_updatevar(int con, struct fb_info *info)
{
	int ret = 0;

	if (con == current_par.currcon)
		ret = cyber2000fb_update_start(&fb_display[con].var);

	return ret;
}

static int
cyber2000fb_switch(int con, struct fb_info *info)
{
	struct fb_cmap *cmap;

	if (current_par.currcon >= 0) {
		cmap = &fb_display[current_par.currcon].cmap;

		if (cmap->len)
			fb_get_cmap(cmap, 1, cyber2000_getcolreg, info);
	}

	current_par.currcon = con;

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	cyber2000fb_set_var(&fb_display[con].var, con, info);

	return 0;
}

/*
 *    (Un)Blank the display.
 */
static void cyber2000fb_blank(int blank, struct fb_info *fb_info)
{
	int i;

	if (blank) {
		for (i = 0; i < 256; i++) {
			cyber2000_outb(i, 0x3c8);
			cyber2000_outb(0, 0x3c9);
			cyber2000_outb(0, 0x3c9);
			cyber2000_outb(0, 0x3c9);
		}
	} else {
		for (i = 0; i < 256; i++) {
			cyber2000_outb(i, 0x3c8);
			cyber2000_outb(current_par.palette[i].red, 0x3c9);
			cyber2000_outb(current_par.palette[i].green, 0x3c9);
			cyber2000_outb(current_par.palette[i].blue, 0x3c9);
		}
	}
}

static struct fb_ops cyber2000fb_ops =
{
	cyber2000fb_open,
	cyber2000fb_release,
	cyber2000fb_get_fix,
	cyber2000fb_get_var,
	cyber2000fb_set_var,
	cyber2000fb_get_cmap,
	cyber2000fb_set_cmap,
	cyber2000fb_pan_display,
	cyber2000fb_ioctl
};

/*
 * These parameters give
 * 640x480, hsync 31.5kHz, vsync 60Hz
 */
static struct fb_videomode __initdata
cyber2000fb_default_mode = {
	name:		NULL,
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

static void __init 
cyber2000fb_init_fbinfo(void)
{
	static int first = 1;

	if (!first)
		return;
	first = 0;

	strcpy(fb_info.modename, "Cyber2000");
	strcpy(fb_info.fontname, "Acorn8x8");

	fb_info.node			= -1;
	fb_info.fbops			= &cyber2000fb_ops;
	fb_info.disp			= &global_disp;
	fb_info.changevar		= NULL;
	fb_info.switch_con		= cyber2000fb_switch;
	fb_info.updatevar		= cyber2000fb_updatevar;
	fb_info.blank			= cyber2000fb_blank;
	fb_info.flags			= FBINFO_FLAG_DEFAULT;

	/*
	 * setup initial parameters
	 */
	memset(&init_var, 0, sizeof(init_var));

	init_var.red.msb_right		= 0;
	init_var.green.msb_right	= 0;
	init_var.blue.msb_right		= 0;

	switch(init_var.bits_per_pixel) {
	default:
		init_var.bits_per_pixel = 8;
	case 8: /* PSEUDOCOLOUR */
		init_var.bits_per_pixel	= 8;
		init_var.red.offset	= 0;
		init_var.red.length	= 8;
		init_var.green.offset	= 0;
		init_var.green.length	= 8;
		init_var.blue.offset	= 0;
		init_var.blue.length	= 8;
		break;

	case 15: /* RGB555 */
		init_var.bits_per_pixel = 15;
		init_var.red.offset	= 10;
		init_var.red.length	= 5;
		init_var.green.offset	= 5;
		init_var.green.length	= 5;
		init_var.blue.offset	= 0;
		init_var.blue.length	= 5;
		break;

	case 16: /* RGB565 */
		init_var.bits_per_pixel = 16;
		init_var.red.offset	= 11;
		init_var.red.length	= 5;
		init_var.green.offset	= 5;
		init_var.green.length	= 6;
		init_var.blue.offset	= 0;
		init_var.blue.length	= 5;
		break;

	case 24: /* RGB888 */
		init_var.bits_per_pixel = 24;
		init_var.red.offset	= 16;
		init_var.red.length	= 8;
		init_var.green.offset	= 8;
		init_var.green.length	= 8;
		init_var.blue.offset	= 0;
		init_var.blue.length	= 8;
		break;
	}

	init_var.nonstd			= 0;
	init_var.activate		= FB_ACTIVATE_NOW;
	init_var.height			= -1;
	init_var.width			= -1;
	init_var.accel_flags		= FB_ACCELF_TEXT;
}

/*
 * Cyber2000 options:
 *
 *  font:fontname
 *	Set the fontname
 *
 *  res:XxY
 *	Set the default display resolution
 */
static void __init
cyber2000fb_parse_font(char *opt)
{
	strcpy(fb_info.fontname, opt);
}

static struct options {
	char *name;
	void (*parse)(char *opt);
} opt_table[] __initdata = {
	{ "font",	cyber2000fb_parse_font		},
	{ NULL,		NULL				}
};

int __init
cyber2000fb_setup(char *options)
{
	struct options *optp;
	char *opt;

	if (!options || !*options)
		return 0;

	cyber2000fb_init_fbinfo();

	for (opt = strtok(options, ","); opt; opt = strtok(NULL, ",")) {
		if (!*opt)
			continue;

		for (optp = opt_table; optp->name; optp++) {
			int optlen;

			optlen = strlen(optp->name);

			if (strncmp(opt, optp->name, optlen) == 0 &&
			    opt[optlen] == ':') {
				optp->parse(opt + optlen + 1);
				break;
			}
		}

		if (!optp->name)
			printk(KERN_ERR "CyberPro20x0: unknown parameter: %s\n",
				opt);
	}
	return 0;
}

static char igs_regs[] __initdata = {
	0x10, 0x10,			0x12, 0x00,	0x13, 0x00,
/*	0x30, 0x21,*/	0x31, 0x00,	0x32, 0x00,	0x33, 0x01,
	0x50, 0x00,	0x51, 0x00,	0x52, 0x00,	0x53, 0x00,
	0x54, 0x00,	0x55, 0x00,	0x56, 0x00,	0x57, 0x01,
	0x58, 0x00,	0x59, 0x00,	0x5a, 0x00,
	0x70, 0x0b,/*	0x71, 0x10,	0x72, 0x45,*/	0x73, 0x30,
	0x74, 0x1b,	0x75, 0x1e,	0x76, 0x00,	0x7a, 0xc8
};

static void __init cyber2000fb_hw_init(void)
{
	int i;

	for (i = 0; i < sizeof(igs_regs); i += 2)
		cyber2000_grphw(igs_regs[i], igs_regs[i+1]);
}

/*
 *    Initialization
 */
int __init cyber2000fb_init(void)
{
	struct pci_dev *dev;
	u_int h_sync, v_sync;
	u_long mmio_base, smem_base, smem_size;
	int err = 0;

	dev = pci_find_device(PCI_VENDOR_ID_INTERG,
			      PCI_DEVICE_ID_INTERG_2000, NULL);

	if (!dev)
		dev = pci_find_device(PCI_VENDOR_ID_INTERG,
				      PCI_DEVICE_ID_INTERG_2010, NULL);

	if (!dev)
		return -ENXIO;

	sprintf(current_par.dev_name, "CyberPro%4X", dev->device);

	smem_base = dev->resource[0].start;
	mmio_base = dev->resource[0].start + 0x00800000;

	/*
	 * Map in the registers
	 */
	if (!request_mem_region(mmio_base, MMIO_SIZE, "memory mapped I/O")) {
		printk("%s: memory mapped IO in use\n",
		       current_par.dev_name);
		return -EBUSY;
	}

	CyberRegs = ioremap(mmio_base, MMIO_SIZE);
	if (!CyberRegs) {
		printk("%s: unable to map memory mapped IO\n",
		       current_par.dev_name);
		err = -ENOMEM;
		goto release_mmio_resource;
	}

	cyber2000_outb(0x18, 0x46e8);
	cyber2000_outb(0x01, 0x102);
	cyber2000_outb(0x08, 0x46e8);

	/*
	 * get the video RAM size from the VGA register.
	 * This should have been already initialised by the BIOS,
	 * but if it's garbage, claim default 1MB VRAM (woody)
	 */
	cyber2000_outb(0x72, 0x3ce);
	switch (cyber2000_inb(0x3cf) & 3) {
	case 2:	 smem_size = 0x00400000; break;
	case 1:	 smem_size = 0x00200000; break;
	default: smem_size = 0x00100000; break;
	}

	/*
	 * Map in screen memory
	 */
	if (!request_mem_region(smem_base, smem_size, "frame buffer")) {
		printk("%s: frame buffer in use\n",
		       current_par.dev_name);
		err = -EBUSY;
		goto release_mmio;
	}

	current_par.screen_base = ioremap(smem_base, smem_size);
	if (!current_par.screen_base) {
		printk("%s: unable to map screen memory\n",
		       current_par.dev_name);
		err = -ENOMEM;
		goto release_smem_resource;
	}
current_par.screen_base += IO_FUDGE_FACTOR;
	current_par.screen_size   = smem_size;
	current_par.screen_base_p = smem_base + 0x80000000;
	current_par.regs_base_p   = mmio_base + 0x80000000;
	current_par.currcon	  = -1;

	cyber2000fb_init_fbinfo();

	if (!fb_find_mode(&init_var, &fb_info, NULL,
	    NULL, 0, &cyber2000fb_default_mode, 8)) {
		printk("%s: no valid mode found\n",
			current_par.dev_name);
		goto release_smem_resource;
	}

	init_var.yres_virtual = smem_size * 8 /
			(init_var.bits_per_pixel * init_var.xres_virtual);

	if (init_var.yres_virtual < init_var.yres)
		init_var.yres_virtual = init_var.yres;
    
	cyber2000fb_hw_init();
	cyber2000fb_set_var(&init_var, -1, &fb_info);

	h_sync = 1953125000 / init_var.pixclock;
	h_sync = h_sync * 512 / (init_var.xres + init_var.left_margin +
		 init_var.right_margin + init_var.hsync_len);
	v_sync = h_sync / (init_var.yres + init_var.upper_margin +
		 init_var.lower_margin + init_var.vsync_len);

	printk("%s: %ldkB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
		current_par.dev_name,
		current_par.screen_size >> 10,
		init_var.xres, init_var.yres,
		h_sync / 1000, h_sync % 1000, v_sync);

	if (register_framebuffer(&fb_info) < 0) {
		err = -EINVAL;
		goto release_smem;
	}

	current_par.initialised = 1;

	MOD_INC_USE_COUNT;	/* TODO: This driver cannot be unloaded yet */
	return 0;

release_smem:
	iounmap(current_par.screen_base);
release_smem_resource:
	release_mem_region(smem_base, smem_size);
release_mmio:
	iounmap(CyberRegs);
release_mmio_resource:
	release_mem_region(mmio_base, MMIO_SIZE);

	return err;
}

#ifdef MODULE
int __init init_module(void)
{
	int ret;

	ret = cyber2000fb_init();
	if (ret)
		return ret;

	return 0;
}

void cleanup_module(void)
{
	/* Not reached because the usecount will never be
	   decremented to zero */
	unregister_framebuffer(&fb_info);
	/* TODO: clean up ... */

	iounmap(current_par.screen_base);
	iounmap(CyberRegs);

	release_mem_region(smem_base, current_par.screen_size);
	release_mem_region(mmio_base, MMIO_SIZE);
}

#endif				/* MODULE */
