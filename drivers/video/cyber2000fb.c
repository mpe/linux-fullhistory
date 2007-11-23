/*
 * linux/drivers/video/cyber2000fb.c
 *
 * Integraphics Cyber2000 frame buffer device
 *
 * Based on cyberfb.c
 */
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

static volatile unsigned char	*CyberRegs;

#include "cyber2000fb.h"

static struct display		global_disp;
static struct fb_info		fb_info;
static struct cyber2000fb_par	current_par;
static struct display_switch	*dispsw;
static struct fb_var_screeninfo __initdata init_var = {};

#ifdef DEBUG
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

/*
 *    Predefined Video Modes
 */
static const struct res cyber2000_res[] = {
	{
		640, 480,
		{
			0x5f, 0x4f, 0x50, 0x80, 0x52, 0x9d, 0x0b, 0x3e,
			0x00, 0x40,
			0xe9, 0x8b, 0xdf, 0x50, 0x00, 0xe6, 0x04, 0xc3
		},
		0x00,
		{ 0xd2, 0xce, 0xdb, 0x54 }
	},

	{
		800, 600,
		{
			0x7f, 0x63, 0x64, 0x00, 0x66, 0x10, 0x6f, 0xf0,
			0x00, 0x60,
			0x5b, 0x8f, 0x57, 0x64, 0x00, 0x59, 0x6e, 0xe3
		},
		0x00,
		{ 0x52, 0x85, 0xdb, 0x54 }
	},

	{
		1024, 768,
		{
			0x9f, 0x7f, 0x80, 0x80, 0x8b, 0x94, 0x1e, 0xfd,
			0x00, 0x60,
			0x03, 0x86, 0xff, 0x80, 0x0f, 0x00, 0x1e, 0xe3
		},
		0x00,
		{ 0xd0, 0x52, 0xdb, 0x54 }
	},
#if 0
	{
		1152, 886,
		{
		},
		{
		}
	},
#endif
	{
		1280, 1024,
		{
			0xce, 0x9f, 0xa0, 0x8f, 0xa2, 0x1f, 0x28, 0x52,
			0x00, 0x40,
			0x08, 0x8f, 0xff, 0xa0, 0x00, 0x03, 0x27, 0xe3
		},
		0x1d,
		{ 0xb4, 0x4b, 0xdb, 0x54 }
	},

	{
		1600, 1200,
		{
			0xff, 0xc7, 0xc9, 0x9f, 0xcf, 0xa0, 0xfe, 0x10,
			0x00, 0x40,
			0xcf, 0x89, 0xaf, 0xc8, 0x00, 0xbc, 0xf1, 0xe3
		},
		0x1f,
		{ 0xbd, 0x10, 0xdb, 0x54 }
	}
};

#define NUM_TOTAL_MODES    arraysize(cyber2000_res)

static const char igs_regs[] = {
	0x10, 0x10,			0x12, 0x00,	0x13, 0x00,
	0x30, 0x21,	0x31, 0x00,	0x32, 0x00,	0x33, 0x01,
	0x50, 0x00,	0x51, 0x00,	0x52, 0x00,	0x53, 0x00,
	0x54, 0x00,	0x55, 0x00,	0x56, 0x00,	0x57, 0x01,
	0x58, 0x00,	0x59, 0x00,	0x5a, 0x00,
	0x70, 0x0b,	0x71, 0x10,	0x72, 0x45,	0x73, 0x30,
	0x74, 0x1b,	0x75, 0x1e,	0x76, 0x00,	0x7a, 0xc8
};

static const char crtc_idx[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

static void cyber2000_init_hw(const struct res *res)
{
	int i;

	debug_printf("init vga hw for %dx%d\n", res->xres, res->yres);

	cyber2000_outb(0xef, 0x3c2);
	cyber2000_crtcw(0x0b, 0x11);
	cyber2000_attrw(0x00, 0x11);

	cyber2000_seqw(0x01, 0x00);
	cyber2000_seqw(0x01, 0x01);
	cyber2000_seqw(0x0f, 0x02);
	cyber2000_seqw(0x00, 0x03);
	cyber2000_seqw(0x0e, 0x04);
	cyber2000_seqw(0x03, 0x00);

	for (i = 0; i < sizeof(crtc_idx); i++)
		cyber2000_crtcw(res->crtc_regs[i], crtc_idx[i]);

	for (i = 0x0a; i < 0x10; i++)
		cyber2000_crtcw(0, i);

	cyber2000_crtcw(0xff, 0x18);

	cyber2000_grphw(0x00, 0x00);
	cyber2000_grphw(0x00, 0x01);
	cyber2000_grphw(0x00, 0x02);
	cyber2000_grphw(0x00, 0x03);
	cyber2000_grphw(0x00, 0x04);
	cyber2000_grphw(0x60, 0x05);
	cyber2000_grphw(0x05, 0x06);
	cyber2000_grphw(0x0f, 0x07);
	cyber2000_grphw(0xff, 0x08);

	for (i = 0; i < 16; i++)
		cyber2000_attrw(i, i);

	cyber2000_attrw(0x01, 0x10);
	cyber2000_attrw(0x00, 0x11);
	cyber2000_attrw(0x0f, 0x12);
	cyber2000_attrw(0x00, 0x13);
	cyber2000_attrw(0x00, 0x14);

	for (i = 0; i < sizeof(igs_regs); i += 2)
		cyber2000_grphw(igs_regs[i+1], igs_regs[i]);

	cyber2000_grphw(res->crtc_ofl, 0x11);

	for (i = 0; i < 4; i += 1)
		cyber2000_grphw(res->clk_regs[i], 0xb0 + i);

	cyber2000_grphw(0x01, 0x90);
	cyber2000_grphw(0x80, 0xb9);
	cyber2000_grphw(0x00, 0xb9);

	cyber2000_outb(0x56, 0x3ce);
	i = cyber2000_inb(0x3cf);
	cyber2000_outb(i | 4, 0x3cf);
	cyber2000_outb(0x04, 0x3c6);
	cyber2000_outb(i,    0x3cf);

	cyber2000_outb(0x20, 0x3c0);
	cyber2000_outb(0xff, 0x3c6);

	for (i = 0; i < 256; i++) {
		cyber2000_outb(i, 0x3c8);
		cyber2000_outb(0, 0x3c9);
		cyber2000_outb(0, 0x3c9);
		cyber2000_outb(0, 0x3c9);
	}
}


static struct fb_ops cyber2000fb_ops;

/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Cyber2000 Acceleration
 */
static void cyber2000_accel_wait(void)
{
	int count = 10000;

	while (cyber2000_inb(0xbf011) & 0x80) {
		if (!count--) {
			debug_printf("accel_wait timed out\n");
			cyber2000_outb(0, 0xbf011);
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
	unsigned long src, dst, chwidth = p->var.xres_virtual * fontheight(p);
	int v = 0x8000;

	if (sx < dx) {
		sx += width - 1;
		dx += width - 1;
		v |= 4;
	}

	if (sy < dy) {
		sy += height - 1;
		dy += height - 1;
		v |= 2;
	}

	sx *= fontwidth(p);
	dx *= fontwidth(p);
	src = sx + sy * chwidth;
	dst = dx + dy * chwidth;
	width = width * fontwidth(p) - 1;
	height = height * fontheight(p) - 1;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,   0xbf011);
	cyber2000_outb(0x03,   0xbf048);
	cyber2000_outw(width,  0xbf060);

	if (p->var.bits_per_pixel != 24) {
		cyber2000_outl(dst, 0xbf178);
		cyber2000_outl(src, 0xbf170);
	} else {
		cyber2000_outl(dst * 3, 0xbf178);
		cyber2000_outb(dst, 0xbf078);
		cyber2000_outl(src * 3, 0xbf170);
	}

	cyber2000_outw(height, 0xbf062);
	cyber2000_outw(v,      0xbf07c);
	cyber2000_outw(0x2800, 0xbf07e);
}

static void
cyber2000_accel_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		int height, int width)
{
	unsigned long dst;
	u32 bgx = attr_bgcol_ec(p, conp);

	dst = sx * fontwidth(p) + sy * p->var.xres_virtual * fontheight(p);
	width = width * fontwidth(p) - 1;
	height = height * fontheight(p) - 1;

	cyber2000_accel_wait();
	cyber2000_outb(0x00,   0xbf011);
	cyber2000_outb(0x03,   0xbf048);
	cyber2000_outw(width,  0xbf060);
	cyber2000_outw(height, 0xbf062);

	switch (p->var.bits_per_pixel) {
	case 16:
		bgx = ((u16 *)p->dispsw_data)[bgx];
	case 8:
		cyber2000_outl(dst, 0xbf178);
		break;

	case 24:
		cyber2000_outl(dst * 3, 0xbf178);
		cyber2000_outb(dst, 0xbf078);
		bgx = ((u32 *)p->dispsw_data)[bgx];
		break;
	}

	cyber2000_outl(bgx,    0xbf058);
	cyber2000_outw(0x8000, 0xbf07c);
	cyber2000_outw(0x0800, 0xbf07e);
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
	case 8:
		cyber2000_outb(regno, 0x3c8);
		cyber2000_outb(red,   0x3c9);
		cyber2000_outb(green, 0x3c9);
		cyber2000_outb(blue,  0x3c9);
		break;

#ifdef FBCON_HAS_CFB16
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

static int cyber2000fb_set_timing(struct fb_var_screeninfo *var)
{
	int width = var->xres_virtual;
	int scr_pitch, fetchrow;
	int i;
	char b, col;

	switch (var->bits_per_pixel) {
	case 8:	/* PSEUDOCOLOUR, 256 */
		b = 0;
		col = 1;
		scr_pitch = var->xres_virtual / 8;
		break;

	case 16:/* DIRECTCOLOUR, 64k */
		b = 1;
		col = 2;
		scr_pitch = var->xres_virtual / 8 * 2;
		break;
	case 24:/* TRUECOLOUR, 16m */
		b = 2;
		col = 4;
		scr_pitch = var->xres_virtual / 8 * 3;
		width *= 3;
		break;

	default:
		return 1;
	}

	for (i = 0; i < NUM_TOTAL_MODES; i++)
		if (var->xres == cyber2000_res[i].xres &&
		    var->yres == cyber2000_res[i].yres)
			break;

	if (i < NUM_TOTAL_MODES)
		cyber2000_init_hw(cyber2000_res + i);

	fetchrow = scr_pitch + 1;

	debug_printf("Setting regs: pitch=%X, fetchrow=%X, col=%X, b=%X\n",
		     scr_pitch, fetchrow, col, b);

	cyber2000_outb(0x13, 0x3d4);
	cyber2000_outb(scr_pitch, 0x3d5);
	cyber2000_outb(0x14, 0x3ce);
	cyber2000_outb(fetchrow, 0x3cf);
	cyber2000_outb(0x15, 0x3ce);
					/* FIXME: is this the right way round? */
	cyber2000_outb(((fetchrow >> 4) & 0xf0) | ((scr_pitch >> 8) & 0x0f), 0x3cf);
	cyber2000_outb(0x77, 0x3ce);
	cyber2000_outb(col, 0x3cf);


	cyber2000_outb(0x33, 0x3ce);
	cyber2000_outb(0x1c, 0x3cf);

	cyber2000_outw(width - 1, 0xbf018);
	cyber2000_outw(width - 1, 0xbf218);
	cyber2000_outb(b, 0xbf01c);

	return 0;
}

static inline void
cyber2000fb_update_start(struct fb_var_screeninfo *var)
{
#if 0
	unsigned int base;

	base = var->yoffset * var->xres_virtual + var->xoffset;

	cyber2000_outb(0x0c, 0x3d4);
	cyber2000_outb(base, 0x3d5);
	cyber2000_outb(0x0d, 0x3d4);
	cyber2000_outb(base >> 8, 0x3d5);
	/* FIXME: need the upper bits of the start offset */
/*	cyber2000_outb(0x??, 0x3d4);
	cyber2000_outb(base >> 16, 0x3d5);*/
#endif
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

static int
cyber2000fb_decode_var(struct fb_var_screeninfo *var, int con, int *visual)
{
	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		*visual = FB_VISUAL_PSEUDOCOLOR;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		*visual = FB_VISUAL_DIRECTCOLOR;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		*visual = FB_VISUAL_TRUECOLOR;
		break;
#endif
	default:
		return -EINVAL;
	}

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
	strcpy(fix->id, "Cyber2000");

	if (con >= 0)
		display = fb_display + con;
	else
		display = &global_disp;

	fix->smem_start	 = (char *)current_par.screen_base_p;
	fix->smem_len	 = current_par.screen_size;
	fix->mmio_start	 = (char *)current_par.regs_base_p;
	fix->mmio_len	 = 0x000c0000;
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
	int err, chgvar = 0, visual;

	if (con >= 0)
		display = fb_display + con;
	else
		display = &global_disp;

	err = cyber2000fb_decode_var(var, con, &visual);
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

	display->screen_base	= (char *)current_par.screen_base;
	display->visual		= visual;
	display->type		= FB_TYPE_PACKED_PIXELS;
	display->type_aux	= 0;
	display->ypanstep	= 0;
	display->ywrapstep	= 0;
	display->line_length	=
	display->next_line	= (var->xres_virtual * var->bits_per_pixel) / 8;
	display->can_soft_blank = 1;
	display->inverse	= 0;

	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		dispsw = &fbcon_cfb8;
		display->dispsw_data = NULL;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		dispsw = &fbcon_cfb16;
		display->dispsw_data = current_par.c_table.cfb16;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		dispsw = &fbcon_cfb24;
		display->dispsw_data = current_par.c_table.cfb24;
		break;
#endif
	default:
		printk(KERN_WARNING "cyber2000: no support for %dbpp\n",
		       display->var.bits_per_pixel);
		dispsw = &fbcon_dummy;
		break;
	}

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
		cyber2000fb_set_timing(var);

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
return -EINVAL;

	cyber2000fb_update_start(var);

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
	if (con == current_par.currcon)
		cyber2000fb_update_start(&fb_display[con].var);
	return 0;
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

__initfunc(void cyber2000fb_setup(char *options, int *ints))
{
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

__initfunc(static void
cyber2000fb_init_fbinfo(void))
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
	init_var.xres_virtual		=
	init_var.xres			= DEFAULT_XRES;
	init_var.yres_virtual		=
	init_var.yres			= DEFAULT_YRES;
	init_var.bits_per_pixel		= DEFAULT_BPP;

	init_var.red.msb_right		= 0;
	init_var.green.msb_right	= 0;
	init_var.blue.msb_right		= 0;

	switch(init_var.bits_per_pixel) {
	case 8:
		init_var.bits_per_pixel	= 8;
		init_var.red.offset	= 0;
		init_var.red.length	= 8;
		init_var.green.offset	= 0;
		init_var.green.length	= 8;
		init_var.blue.offset	= 0;
		init_var.blue.length	= 8;
		break;

	case 16:
		init_var.bits_per_pixel = 16;
		init_var.red.offset	= 11;
		init_var.red.length	= 5;
		init_var.green.offset	= 5;
		init_var.green.length	= 6;
		init_var.blue.offset	= 0;
		init_var.blue.length	= 5;
		break;

	case 24:
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
	init_var.sync			= FB_SYNC_COMP_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT;
	init_var.vmode			= FB_VMODE_NONINTERLACED;
}

/*
 *    Initialization
 */
__initfunc(void cyber2000fb_init(void))
{
	struct pci_dev *dev;
	u_int h_sync, v_sync;

	dev = pci_find_device(PCI_VENDOR_ID_INTERG, 0x2000, NULL);
	if (!dev)
		return;

	CyberRegs = bus_to_virt(dev->base_address[0]) + 0x00800000;/*FIXME*/

	cyber2000_outb(0x18, 0x46e8);
	cyber2000_outb(0x01, 0x102);
	cyber2000_outb(0x08, 0x46e8);

	cyber2000fb_init_fbinfo();

	current_par.currcon		= -1;
	current_par.screen_base_p	= 0x80000000 + dev->base_address[0];
	current_par.screen_base		= (u_int)bus_to_virt(dev->base_address[0]);
	current_par.screen_size		= 0x00200000;
	current_par.regs_base_p		= 0x80800000 + dev->base_address[0];

	cyber2000fb_set_var(&init_var, -1, &fb_info);

	h_sync = 1953125000 / init_var.pixclock;
	h_sync = h_sync * 512 / (init_var.xres + init_var.left_margin +
		 init_var.right_margin + init_var.hsync_len);
	v_sync = h_sync / (init_var.yres + init_var.upper_margin +
		 init_var.lower_margin + init_var.vsync_len);

	printk("Cyber2000: %ldkB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
		current_par.screen_size >> 10,
		init_var.xres, init_var.yres,
		h_sync / 1000, h_sync % 1000, v_sync);

	if (register_framebuffer(&fb_info) < 0)
		return;

	MOD_INC_USE_COUNT;	/* TODO: This driver cannot be unloaded yet */
}



#ifdef MODULE
int init_module(void)
{
	cyber2000fb_init();
	return 0;
}

void cleanup_module(void)
{
	/* Not reached because the usecount will never be
	   decremented to zero */
	unregister_framebuffer(&fb_info);
	/* TODO: clean up ... */
}

#endif				/* MODULE */
