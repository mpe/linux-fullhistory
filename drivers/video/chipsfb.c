/*
 *  drivers/video/chipsfb.c -- frame buffer device for
 *  Chips & Technologies 65550 chip.
 *
 *  Copyright (C) 1998 Paul Mackerras
 *
 *  This file is derived from the Powermac "chips" driver:
 *  Copyright (C) 1997 Fabio Riccardi.
 *  And from the frame buffer device for Open Firmware-initialized devices:
 *  Copyright (C) 1997 Geert Uytterhoeven.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
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
#include <linux/selection.h>
#include <linux/init.h>
#include <linux/pci.h>
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/adb.h>
#include <asm/pmu.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/macmodes.h>

static int currcon = 0;

struct fb_info_chips {
	struct fb_info info;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct display disp;
	struct {
		__u8 red, green, blue;
	} palette[256];
	__u8 *frame_buffer;
	__u8 *blitter_regs;
	__u8 *io_base;
	unsigned long chips_base_phys;
	unsigned long chips_io_phys;
	struct fb_info_chips *next;
#ifdef CONFIG_PMAC_PBOOK
	unsigned char *save_framebuffer;
#endif
#ifdef FBCON_HAS_CFB16
	u16 fbcon_cfb16_cmap[16];
#endif
};

#define write_ind(num, val, ap, dp)	do { \
	out_8(p->io_base + (ap), (num)); out_8(p->io_base + (dp), (val)); \
} while (0)
#define read_ind(num, var, ap, dp)	do { \
	out_8(p->io_base + (ap), (num)); var = in_8(p->io_base + (dp)); \
} while (0);

/* extension registers */
#define write_xr(num, val)	write_ind(num, val, 0x3d6, 0x3d7)
#define read_xr(num, var)	read_ind(num, var, 0x3d6, 0x3d7)
/* flat panel registers */
#define write_fr(num, val)	write_ind(num, val, 0x3d0, 0x3d1)
#define read_fr(num, var)	read_ind(num, var, 0x3d0, 0x3d1)
/* CRTC registers */
#define write_cr(num, val)	write_ind(num, val, 0x3d4, 0x3d5)
#define read_cr(num, var)	read_ind(num, var, 0x3d4, 0x3d5)
/* graphics registers */
#define write_gr(num, val)	write_ind(num, val, 0x3ce, 0x3cf)
#define read_gr(num, var)	read_ind(num, var, 0x3ce, 0x3cf)
/* sequencer registers */
#define write_sr(num, val)	write_ind(num, val, 0x3c4, 0x3c5)
#define read_sr(num, var)	read_ind(num, var, 0x3c4, 0x3c5)
/* attribute registers - slightly strange */
#define write_ar(num, val)	do { \
	in_8(p->io_base + 0x3da); write_ind(num, val, 0x3c0, 0x3c0); \
} while (0)
#define read_ar(num, var)	do { \
	in_8(p->io_base + 0x3da); read_ind(num, var, 0x3c0, 0x3c1); \
} while (0)

static struct fb_info_chips *all_chips;

#ifdef CONFIG_PMAC_PBOOK
int chips_sleep_notify(struct notifier_block *, unsigned long, void *);
static struct notifier_block chips_sleep_notifier = {
	chips_sleep_notify, NULL, 0
};
#endif

/*
 * Exported functions
 */
void chips_init(void);
void chips_of_init(struct device_node *dp);

static int chips_open(struct fb_info *info, int user);
static int chips_release(struct fb_info *info, int user);
static int chips_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int chips_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int chips_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int chips_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info);
static int chips_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int chips_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int chips_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info);

static struct fb_ops chipsfb_ops = {
	chips_open,
	chips_release,
	chips_get_fix,
	chips_get_var,
	chips_set_var,
	chips_get_cmap,
	chips_set_cmap,
	chips_pan_display,
	chips_ioctl
};

static int chipsfb_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info);
static int chipsfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);
static void chips_set_bitdepth(struct fb_info_chips *p, struct display* disp, int con, int bpp);


static int chips_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int chips_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static int chips_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	struct fb_info_chips *cp = (struct fb_info_chips *) info;

	*fix = cp->fix;
	return 0;
}

static int chips_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_chips *cp = (struct fb_info_chips *) info;

	*var = cp->var;
	return 0;
}

static int chips_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_chips *cp = (struct fb_info_chips *) info;
	struct display *disp = (con >= 0)? &fb_display[con]: &cp->disp;

	if (var->xres > 800 || var->yres > 600
	    || var->xres_virtual > 800 || var->yres_virtual > 600
	    || (var->bits_per_pixel != 8 && var->bits_per_pixel != 16)
	    || var->nonstd
	    || (var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		return -EINVAL;

	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW &&
		var->bits_per_pixel != disp->var.bits_per_pixel) {
		chips_set_bitdepth(cp, disp, con, var->bits_per_pixel);
	}

	return 0;
}

static int chips_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
	if (var->xoffset != 0 || var->yoffset != 0)
		return -EINVAL;
	return 0;
}

static int chips_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	if (con == currcon)		/* current console? */
		return fb_get_cmap(cmap, kspc, chipsfb_getcolreg, info);
	if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc? 0: 2);
	else
		fb_copy_cmap(fb_default_cmap(256), cmap, kspc? 0: 2);
	return 0;
}

static int chips_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	struct display *disp = &fb_display[con];
	int err;

	if (disp->cmap.len == 0) {
		err = fb_alloc_cmap(&disp->cmap, 256, 0);
		if (err)
			return err;
	}

	if (con == currcon)
		return fb_set_cmap(cmap, kspc, chipsfb_setcolreg, info);
	fb_copy_cmap(cmap, &disp->cmap, kspc==0);
	return 0;
}

static int chips_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
	return -EINVAL;
}

static int chipsfb_switch(int con, struct fb_info *info)
{
	struct fb_info_chips *p = (struct fb_info_chips *) info;
	struct display* old_disp = &fb_display[currcon];
	struct display* new_disp = &fb_display[con];
	int	bit_depth;

	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&old_disp->cmap, 1, chipsfb_getcolreg, info);

	bit_depth = new_disp->var.bits_per_pixel;
	if (old_disp->var.bits_per_pixel != bit_depth)
	{
	  currcon = con;
	  chips_set_bitdepth(p, new_disp, con, bit_depth);
	}
	else
	  currcon = con;
	
	do_install_cmap(con, info);
	return 0;
}

static int chipsfb_updatevar(int con, struct fb_info *info)
{
	return 0;
}

static void chipsfb_blank(int blank, struct fb_info *info)
{
	struct fb_info_chips *p = (struct fb_info_chips *) info;
	int i;

	if (blank > 1) {
		pmu_enable_backlight(0);
	} else if (blank) {
		for (i = 0; i < 256; ++i) {
			out_8(p->io_base + 0x3c8, i);
			udelay(1);
			out_8(p->io_base + 0x3c9, 0);
			out_8(p->io_base + 0x3c9, 0);
			out_8(p->io_base + 0x3c9, 0);
		}
	} else {
		pmu_enable_backlight(1);
		do_install_cmap(currcon, info);
	}
}

static int chipsfb_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info)
{
	struct fb_info_chips *p = (struct fb_info_chips *) info;

	if (regno > 255)
		return 1;
	*red = (p->palette[regno].red<<8) | p->palette[regno].red;
	*green = (p->palette[regno].green<<8) | p->palette[regno].green;
	*blue = (p->palette[regno].blue<<8) | p->palette[regno].blue;
	*transp = 0;
	return 0;
}

static int chipsfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info)
{
	struct fb_info_chips *p = (struct fb_info_chips *) info;
	int hr;

	hr = (p->fix.visual != FB_VISUAL_PSEUDOCOLOR)? (regno << 3): regno;
	if (hr > 255)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;
	p->palette[regno].red = red;
	p->palette[regno].green = green;
	p->palette[regno].blue = blue;
	out_8(p->io_base + 0x3c8, hr);
	udelay(1);
	out_8(p->io_base + 0x3c9, red);
	out_8(p->io_base + 0x3c9, green);
	out_8(p->io_base + 0x3c9, blue);

#ifdef FBCON_HAS_CFB16
	if (regno < 16)
		p->fbcon_cfb16_cmap[regno] = (red << 10) | (green << 5) | blue;
#endif

    return 0;
}

static void do_install_cmap(int con, struct fb_info *info)
{
	if (con != currcon)
		return;
	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, 1, chipsfb_setcolreg, info);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_set_cmap(fb_default_cmap(size), 1, chipsfb_setcolreg, info);
	}
}

static void chips_set_bitdepth(struct fb_info_chips *p, struct display* disp, int con, int bpp)
{
	int err;
	struct fb_fix_screeninfo* fix = &p->fix;
	struct fb_var_screeninfo* var = &p->var;
	
	if (bpp == 16) {
		if (con == currcon) {
			write_cr(0x13, 200);		// 16 bit display width (decimal)
			write_xr(0x81, 0x14);		// 15 bit (TrueColor) color mode
			write_xr(0x20, 0x10);		// 16 bit blitter mode
		}

		fix->line_length = 800*2;
		fix->visual = FB_VISUAL_DIRECTCOLOR;

		var->red.offset = 10;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 5;
		
#ifdef FBCON_HAS_CFB16
		disp->dispsw = &fbcon_cfb16;
		disp->dispsw_data = p->fbcon_cfb16_cmap;
#else
		disp->dispsw = &fbcon_dummy;
#endif
	} else if (bpp == 8) {
		if (con == currcon) {
			write_cr(0x13, 100);		// 8 bit display width (decimal)
			write_xr(0x81, 0x12);		// 8 bit color mode
			write_xr(0x20, 0x00);		// 8 bit blitter mode
		}

		fix->line_length = 800;
		fix->visual = FB_VISUAL_PSEUDOCOLOR;		

 		var->red.offset = var->green.offset = var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 8;
		
#ifdef FBCON_HAS_CFB8
		disp->dispsw = &fbcon_cfb8;
#else
		disp->dispsw = &fbcon_dummy;
#endif
	}

	var->bits_per_pixel = bpp;
	disp->line_length = p->fix.line_length;
	disp->visual = fix->visual;
	disp->var = *var;

#ifdef CONFIG_PMAC_PBOOK
	display_info.depth = bpp;
	display_info.pitch = fix->line_length;
#endif
	
	if (p->info.changevar)
		(*p->info.changevar)(con);

	if ((err = fb_alloc_cmap(&disp->cmap, 0, 0)))
		return;
	do_install_cmap(con, (struct fb_info *)p);
}

struct chips_init_reg {
	unsigned char addr;
	unsigned char data;
};

#define N_ELTS(x)	(sizeof(x) / sizeof(x[0]))

static struct chips_init_reg chips_init_sr[] = {
	{ 0x00, 0x03 },
	{ 0x01, 0x01 },
	{ 0x02, 0x0f },
	{ 0x04, 0x0e }
};

static struct chips_init_reg chips_init_gr[] = {
	{ 0x05, 0x00 },
	{ 0x06, 0x0d },
	{ 0x08, 0xff }
};

static struct chips_init_reg chips_init_ar[] = {
	{ 0x10, 0x01 },
	{ 0x12, 0x0f },
	{ 0x13, 0x00 }
};

static struct chips_init_reg chips_init_cr[] = {
	{ 0x00, 0x7f },
	{ 0x01, 0x63 },
	{ 0x02, 0x63 },
	{ 0x03, 0x83 },
	{ 0x04, 0x66 },
	{ 0x05, 0x10 },
	{ 0x06, 0x72 },
	{ 0x07, 0x3e },
	{ 0x08, 0x00 },
	{ 0x09, 0x40 },
	{ 0x0c, 0x00 },
	{ 0x0d, 0x00 },
	{ 0x10, 0x59 },
	{ 0x11, 0x0d },
	{ 0x12, 0x57 },
	{ 0x13, 0x64 },
	{ 0x14, 0x00 },
	{ 0x15, 0x57 },
	{ 0x16, 0x73 },
	{ 0x17, 0xe3 },
	{ 0x18, 0xff },
	{ 0x30, 0x02 },
	{ 0x31, 0x02 },
	{ 0x32, 0x02 },
	{ 0x33, 0x02 },
	{ 0x40, 0x00 },
	{ 0x41, 0x00 },
	{ 0x40, 0x80 }
};

static struct chips_init_reg chips_init_fr[] = {
	{ 0x01, 0x02 },
	{ 0x03, 0x08 },
	{ 0x04, 0x81 },
	{ 0x05, 0x21 },
	{ 0x08, 0x0c },
	{ 0x0a, 0x74 },
	{ 0x0b, 0x11 },
	{ 0x10, 0x0c },
	{ 0x11, 0xe0 },
	/* { 0x12, 0x40 }, -- 3400 needs 40, 2400 needs 48, no way to tell */
	{ 0x20, 0x63 },
	{ 0x21, 0x68 },
	{ 0x22, 0x19 },
	{ 0x23, 0x7f },
	{ 0x24, 0x68 },
	{ 0x26, 0x00 },
	{ 0x27, 0x0f },
	{ 0x30, 0x57 },
	{ 0x31, 0x58 },
	{ 0x32, 0x0d },
	{ 0x33, 0x72 },
	{ 0x34, 0x02 },
	{ 0x35, 0x22 },
	{ 0x36, 0x02 },
	{ 0x37, 0x00 }
};

static struct chips_init_reg chips_init_xr[] = {
	{ 0xce, 0x00 },		/* set default memory clock */
	{ 0xcc, 0x43 },		/* memory clock ratio */
	{ 0xcd, 0x18 },
	{ 0xce, 0xa1 },
	{ 0xc8, 0x84 },
	{ 0xc9, 0x0a },
	{ 0xca, 0x00 },
	{ 0xcb, 0x20 },
	{ 0xcf, 0x06 },
	{ 0xd0, 0x0e },
	{ 0x09, 0x01 },
	{ 0x0a, 0x02 },
	{ 0x0b, 0x01 },
	{ 0x20, 0x00 },
	{ 0x40, 0x03 },
	{ 0x41, 0x01 },
	{ 0x42, 0x00 },
	{ 0x80, 0x82 },
	{ 0x81, 0x12 },
	{ 0x82, 0x08 },
	{ 0xa0, 0x00 },
	{ 0xa8, 0x00 }
};

__initfunc(static void chips_hw_init(struct fb_info_chips *p))
{
	int i;

	for (i = 0; i < N_ELTS(chips_init_xr); ++i)
		write_xr(chips_init_xr[i].addr, chips_init_xr[i].data);
	out_8(p->io_base + 0x3c2, 0x29); /* set misc output reg */
	for (i = 0; i < N_ELTS(chips_init_sr); ++i)
		write_sr(chips_init_sr[i].addr, chips_init_sr[i].data);
	for (i = 0; i < N_ELTS(chips_init_gr); ++i)
		write_gr(chips_init_gr[i].addr, chips_init_gr[i].data);
	for (i = 0; i < N_ELTS(chips_init_ar); ++i)
		write_ar(chips_init_ar[i].addr, chips_init_ar[i].data);
	for (i = 0; i < N_ELTS(chips_init_cr); ++i)
		write_cr(chips_init_cr[i].addr, chips_init_cr[i].data);
	for (i = 0; i < N_ELTS(chips_init_fr); ++i)
		write_fr(chips_init_fr[i].addr, chips_init_fr[i].data);
}

__initfunc(static void init_chips(struct fb_info_chips *p))
{
	int i;

	strcpy(p->fix.id, "C&T 65550");
	p->fix.smem_start = (char *) p->chips_base_phys;
	p->fix.smem_len = 800 * 600;
	p->fix.mmio_start = (char *) p->chips_io_phys;
	p->fix.type = FB_TYPE_PACKED_PIXELS;
	p->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	p->fix.line_length = 800;

	p->var.xres = 800;
	p->var.yres = 600;
	p->var.xres_virtual = 800;
	p->var.yres_virtual = 600;
	p->var.bits_per_pixel = 8;
	p->var.red.length = p->var.green.length = p->var.blue.length = 8;
	p->var.height = p->var.width = -1;
	p->var.vmode = FB_VMODE_NONINTERLACED;
	p->var.pixclock = 10000;
	p->var.left_margin = p->var.right_margin = 16;
	p->var.upper_margin = p->var.lower_margin = 16;
	p->var.hsync_len = p->var.vsync_len = 8;

	p->disp.var = p->var;
	p->disp.cmap.red = NULL;
	p->disp.cmap.green = NULL;
	p->disp.cmap.blue = NULL;
	p->disp.cmap.transp = NULL;
	p->disp.screen_base = (char *) p->frame_buffer;
	p->disp.visual = p->fix.visual;
	p->disp.type = p->fix.type;
	p->disp.type_aux = p->fix.type_aux;
	p->disp.line_length = p->fix.line_length;
	p->disp.can_soft_blank = 1;
	p->disp.dispsw = &fbcon_cfb8;
	p->disp.scrollmode = SCROLL_YREDRAW;

	strcpy(p->info.modename, p->fix.id);
	p->info.node = -1;
	p->info.fbops = &chipsfb_ops;
	p->info.disp = &p->disp;
	p->info.fontname[0] = 0;
	p->info.changevar = NULL;
	p->info.switch_con = &chipsfb_switch;
	p->info.updatevar = &chipsfb_updatevar;
	p->info.blank = &chipsfb_blank;
	p->info.flags = FBINFO_FLAG_DEFAULT;

	for (i = 0; i < 16; ++i) {
		int j = color_table[i];
		p->palette[i].red = default_red[j];
		p->palette[i].green = default_grn[j];
		p->palette[i].blue = default_blu[j];
	}

	if (register_framebuffer(&p->info) < 0) {
		kfree(p);
		return;
	}

	printk("fb%d: Chips 65550 frame buffer\n", GET_FB_IDX(p->info.node));

	chips_hw_init(p);

#ifdef CONFIG_FB_COMPAT_XPMAC
	if (!console_fb_info) {
		display_info.height = p->var.yres;
		display_info.width = p->var.xres;
		display_info.depth = 8;
		display_info.pitch = p->fix.line_length;
		display_info.mode = VMODE_800_600_60;
		strncpy(display_info.name, "chips65550",
			sizeof(display_info.name));
		display_info.fb_address = p->chips_base_phys + 0x800000;
		display_info.cmap_adr_address = p->chips_io_phys + 0x3c8;
		display_info.cmap_data_address = p->chips_io_phys + 0x3c9;
		display_info.disp_reg_address = p->chips_base_phys + 0xc00000;
		console_fb_info = &p->info;
	}
#endif /* CONFIG_FB_COMPAT_XPMAC */

#ifdef CONFIG_PMAC_PBOOK
	if (all_chips == NULL)
		notifier_chain_register(&sleep_notifier_list,
					&chips_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */
	p->next = all_chips;
	all_chips = p;
}

__initfunc(void chips_init(void))
{
#ifndef CONFIG_FB_OF
	struct device_node *dp;

	dp = find_devices("chips65550");
	if (dp != 0)
		chips_of_init(dp);
#endif /* CONFIG_FB_OF */
}

__initfunc(void chips_of_init(struct device_node *dp))
{
	struct fb_info_chips *p;
	unsigned long addr;
	unsigned char bus, devfn;
	unsigned short cmd;

	if (dp->n_addrs == 0)
		return;
	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == 0)
		return;
	memset(p, 0, sizeof(*p));
	addr = dp->addrs[0].address;
	p->chips_base_phys = addr;
	p->frame_buffer = __ioremap(addr+0x800000, 0x100000, _PAGE_NO_CACHE);
	p->blitter_regs = ioremap(addr + 0xC00000, 0x1000);

	if (pci_device_loc(dp, &bus, &devfn) == 0) {
		pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
		cmd |= 3;	/* enable memory and IO space */
		pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
		p->io_base = (unsigned char *) pci_io_base(bus);
		/* XXX really want the physical address here */
		p->chips_io_phys = (unsigned long) pci_io_base(bus);
	}

	/* Clear the entire framebuffer */
	memset(p->frame_buffer, 0, 0x100000);

	/* turn on the backlight */
	pmu_enable_backlight(1);

	init_chips(p);
}

#ifdef CONFIG_PMAC_PBOOK
/*
 * Save the contents of the frame buffer when we go to sleep,
 * and restore it when we wake up again.
 */
int
chips_sleep_notify(struct notifier_block *this, unsigned long code, void *x)
{
	struct fb_info_chips *p;

	for (p = all_chips; p != NULL; p = p->next) {
		int nb = p->var.yres * p->fix.line_length;

		switch (code) {
		case PBOOK_SLEEP:
			p->save_framebuffer = vmalloc(nb);
			if (p->save_framebuffer)
				memcpy(p->save_framebuffer,
				       p->frame_buffer, nb);
			break;
		case PBOOK_WAKE:
			if (p->save_framebuffer) {
				memcpy(p->frame_buffer,
				       p->save_framebuffer, nb);
				vfree(p->save_framebuffer);
				p->save_framebuffer = 0;
			}
			break;
		}
	}
	return NOTIFY_DONE;
}
#endif /* CONFIG_PMAC_PBOOK */
