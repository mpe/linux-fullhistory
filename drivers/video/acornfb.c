/*
 * linux/drivers/video/acorn.c
 *
 * Copyright (C) 1998 Russell King
 *
 * Frame buffer code for Acorn platforms
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/fb.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>

#define MAX_VIDC20_PALETTE	256
#define MAX_VIDC_PALETTE	16

struct acornfb_par {
	unsigned long screen_base;
	unsigned int xres;
	unsigned int yres;
	unsigned char bits_per_pixel;
	unsigned int palette_size;

	union {
		union {
			struct {
				unsigned long red:8;
				unsigned long green:8;
				unsigned long blue:8;
				unsigned long ext:4;
				unsigned long unused:4;
			} d;
			unsigned long p;
		} vidc20[MAX_VIDC20_PALETTE];
		union {
			struct {
				unsigned long red:4;
				unsigned long green:4;
				unsigned long blue:4;
				unsigned long trans:1;
				unsigned long unused:19;
			} d;
			unsigned long p;
		} vidc[MAX_VIDC_PALETTE];
	} palette;
};

static int currcon = 0;
static struct display disp;
static struct fb_info fb_info;
static struct acornfb_par current_par;

static int
acornfb_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
acornfb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static void
acornfb_encode_var(struct fb_var_screeninfo *var, struct acornfb_par *par)
{
	var->xres		= par->xres;
	var->yres		= par->yres;
	var->xres_virtual	= par->xres;
	var->yres_virtual	= par->yres;
	var->xoffset		= 0;
	var->yoffset		= 0;
	var->bits_per_pixel	= par->bits_per_pixel;
	var->grayscale		= 0;
	var->red.offset		= 0;
	var->red.length		= 8;
	var->red.msb_right	= 0;
	var->green.offset	= 0;
	var->green.length	= 8;
	var->green.msb_right	= 0;
	var->blue.offset	= 0;
	var->blue.length	= 8;
	var->blue.msb_right	= 0;
	var->transp.offset	= 0;
	var->transp.length	= 4;
	var->transp.msb_right	= 0;
	var->nonstd		= 0;
	var->activate		= FB_ACTIVATE_NOW;
	var->height		= -1;
	var->width		= -1;
	var->vmode		= FB_VMODE_NONINTERLACED;
	var->pixclock		= 1;
	var->sync		= 0;
	var->left_margin	= 0;
	var->right_margin	= 0;
	var->upper_margin	= 0;
	var->lower_margin	= 0;
	var->hsync_len		= 0;
	var->vsync_len		= 0;
}

static int
acornfb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	struct acornfb_par *par = &current_par;
	unsigned int line_length;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, "Acorn");

	line_length = par->xres * par->bits_per_pixel / 8;

	fix->smem_start	 = (char *)SCREEN2_BASE;
	fix->smem_len	 = (((line_length * par->yres) - 1) | (PAGE_SIZE - 1)) + 1;
	fix->type	 = FB_TYPE_PACKED_PIXELS;
	fix->type_aux	 = 0;
	fix->visual	 = FB_VISUAL_PSEUDOCOLOR;
	fix->xpanstep	 = 0;
	fix->ypanstep	 = 0;
	fix->ywrapstep	 = 1;
	fix->line_length = line_length;
	fix->accel	 = FB_ACCEL_NONE;

	return 0;
}

static int
acornfb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	if (con == -1) {
		acornfb_encode_var(var, &current_par);
	} else
		*var = fb_display[con].var;
	return 0;
}

static int
acornfb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	return 0;
}

static void
acornfb_set_disp(int con)
{
	struct fb_fix_screeninfo fix;
	struct display *display;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;

	current_par.xres = 8 * ORIG_VIDEO_COLS;
	current_par.yres = 8 * ORIG_VIDEO_LINES;
	current_par.bits_per_pixel = 8;
	current_par.palette_size = MAX_VIDC20_PALETTE;

	acornfb_get_fix(&fix, con, 0);

	acornfb_get_var(&display->var, con, 0);

	display->cmap.start	= 0;
	display->cmap.len	= 0;
	display->cmap.red	= NULL;
	display->cmap.green	= NULL;
	display->cmap.blue	= NULL;
	display->cmap.transp	= NULL;
	display->screen_base	= fix.smem_start;
	display->visual		= fix.visual;
	display->type		= fix.type;
	display->type_aux	= fix.type_aux;
	display->ypanstep	= fix.ypanstep;
	display->ywrapstep	= fix.ywrapstep;
	display->line_length	= fix.line_length;
	display->can_soft_blank	= 0;
	display->inverse	= 0;

	outl(SCREEN_START, VDMA_START);
	outl(SCREEN_START + fix.smem_len - VDMA_XFERSIZE, VDMA_END);
	outl(SCREEN_START, VDMA_INIT);

	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_MFB
	case 1:
		display->dispsw = &fbcon_mfb;
		break;
#endif
#ifdef FBCON_HAS_CFB2
	case 2:
		display->dispsw = &fbcon_cfb2;
		break;
#endif
#ifdef FBCON_HAS_CFB4
	case 4:
		display->dispsw = &fbcon_cfb4;
		break;
#endif
#ifdef FBCON_HAS_CFB8
	case 8:
		display->dispsw = &fbcon_cfb8;
		break;
#endif
	default:
		display->dispsw = &fbcon_dummy;
		break;
	}
}

static int
acornfb_vidc20_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue, u_int *trans, struct fb_info *info)
{
	int t;

	if (regno >= current_par.palette_size)
		return 1;
	t = current_par.palette.vidc20[regno].d.red;
	*red = (t << 8) | t;
	t = current_par.palette.vidc20[regno].d.green;
	*green = (t << 8) | t;
	t = current_par.palette.vidc20[regno].d.blue;
	*blue = (t << 8) | t;
	t = current_par.palette.vidc20[regno].d.ext;
	t |= t << 4;
	*transp = (t << 8) | t;
	return 0;
}

static int
acornfb_vidc20_setcolreg(u_int regno, u_int red, u_int green, u_int blue, u_int trans, struct fb_info *info)
{
	if (regno >= current_par.palette_size)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;
	current_par.palette.vidc20[regno].p = 0;
	current_par.palette.vidc20[regno].d.red   = red;
	current_par.palette.vidc20[regno].d.green = green;
	current_par.palette.vidc20[regno].d.blue  = blue;

	outl(0x10000000 | regno, VIDC_BASE);
	outl(current_par.palette.vidc20[regno].p, VIDC_BASE);

	return 0;
}

static int
acornfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
		 struct fb_info *info)
{
	int err = 0;

	if (con == currcon)
		err = fb_get_cmap(cmap, kspc, acornfb_vidc20_getcolreg, info);
	else if (fb_display[con].cmap.len)
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(current_par.palette_size),
			     cmap, kspc ? 0 : 2);
	return err;
}

static int
acornfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		 struct fb_info *info)
{
	int err = 0;

	if (!fb_display[con].cmap.len)
		err = fb_alloc_cmap(&fb_display[con].cmap,
				    current_par.palette_size, 0);
	if (!err) {
		if (con == currcon)
			err = fb_set_cmap(cmap, kspc, acornfb_vidc20_setcolreg,
					  info);
		else
			fb_copy_cmap(cmap, &fb_display[con].cmap,
				     kspc ? 0 : 1);
	}
	return err;
}

static int
acornfb_pan_display(struct fb_var_screeninfo *var, int con,
		    struct fb_info *info)
{
	if (var->xoffset || var->yoffset)
		return -EINVAL;
	else
		return 0;
}

static int
acornfb_ioctl(struct inode *ino, struct file *file, unsigned int cmd,
	      unsigned long arg, int con, struct fb_info *info)
{
	return -ENOIOCTLCMD;
}

static struct fb_ops acornfb_ops = {
	acornfb_open,
	acornfb_release,
	acornfb_get_fix,
	acornfb_get_var,
	acornfb_set_var,
	acornfb_get_cmap,
	acornfb_set_cmap,
	acornfb_pan_display,
	acornfb_ioctl
};

void
acornfb_setup(char *options, int *ints)
{
}

static int
acornfb_update_var(int con, struct fb_info *info)
{
	if (con == currcon) {
		int off = fb_display[con].var.yoffset *
			  fb_display[con].var.xres_virtual *
			  fb_display[con].var.bits_per_pixel >> 3;
		unsigned long base;

		base = current_par.screen_base = SCREEN_START + off;

		outl (SCREEN_START + base, VDMA_INIT);
	}

	return 0;
}

static int
acornfb_switch(int con, struct fb_info *info)
{
	currcon = con;
	acornfb_update_var(con, info);
	return 0;
}

static void
acornfb_blank(int blank, struct fb_info *info)
{
}

__initfunc(unsigned long
acornfb_init(unsigned long mem_start))
{
	strcpy(fb_info.modename, "Acorn");
	fb_info.node		= -1;
	fb_info.fbops		= &acornfb_ops;
	fb_info.disp		= &disp;
	fb_info.monspecs.hfmin	= 0;
	fb_info.monspecs.hfmax	= 0;
	fb_info.monspecs.vfmin	= 0;
	fb_info.monspecs.vfmax	= 0;
	fb_info.monspecs.dpms	= 0;
	strcpy(fb_info.fontname, "Acorn8x8");
	fb_info.changevar	= NULL;
	fb_info.switch_con	= acornfb_switch;
	fb_info.updatevar	= acornfb_update_var;
	fb_info.blank		= acornfb_blank;
	fb_info.flags		= FBINFO_FLAG_DEFAULT;

	acornfb_set_disp(-1);
	fb_set_cmap(fb_default_cmap(current_par.palette_size),
		    1, acornfb_vidc20_setcolreg, &fb_info);
	register_framebuffer(&fb_info);

	return mem_start;
}
