/*
 * linux/drivers/video/acorn.c
 *
 * Copyright (C) 1998,1999 Russell King
 *
 * Frame buffer code for Acorn platforms
 *
 * NOTE: Most of the modes with X!=640 will disappear shortly.
 * NOTE: Startup setting of HS & VS polarity not supported.
 *       (do we need to support it if we're coming up in 640x480?)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/fb.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>

/*
 * Default resolution.
 * NOTE that it has to be supported in the table towards
 * the end of this file.
 */
#define DEFAULT_XRES	640
#define DEFAULT_YRES	480

/*
 * define this to debug the video mode selection
 */
#undef DEBUG_MODE_SELECTION

#if defined(HAS_VIDC20)
#define VIDC_PALETTE_SIZE	256
#define VIDC_NAME		"VIDC20"
#elif defined(HAS_VIDC)
#include <asm/memc.h>
#define VIDC_PALETTE_SIZE	16
#define VIDC_NAME		"VIDC"
#endif

#define EXTEND8(x) ((x)|(x)<<8)
#define EXTEND4(x) ((x)|(x)<<4|(x)<<8|(x)<<16)

struct vidc20_palette {
	u_int red:8;
	u_int green:8;
	u_int blue:8;
	u_int ext:4;
	u_int unused:4;
};

struct vidc_palette {
	u_int red:4;
	u_int green:4;
	u_int blue:4;
	u_int trans:1;
	u_int sbz1:13;
	u_int reg:4;
	u_int sbz2:2;
};

union palette {
	struct vidc20_palette	vidc20;
	struct vidc_palette	vidc;
	u_int	p;
};

struct acornfb_par {
	unsigned long	screen_base;
	unsigned long	screen_base_p;
	unsigned long	screen_end;
	unsigned long	screen_size;
	unsigned int	dram_size;
	unsigned int	vram_half_sam;
	unsigned int	palette_size;
	  signed int	montype;
	  signed int	currcon;
	unsigned int	allow_modeset	: 1;
	unsigned int	using_vram	: 1;
	unsigned int	dpms		: 1;

	union palette palette[VIDC_PALETTE_SIZE];

	union {
		unsigned short cfb16[16];
		unsigned long  cfb32[16];
	} cmap;
};

/*
 * Translation from RISC OS monitor types to actual
 * HSYNC and VSYNC frequency ranges.  These are
 * probably not right...
 */
#define NR_MONTYPES	6
static struct fb_monspecs monspecs[NR_MONTYPES] __initdata = {
	{ 15625, 15625, 50, 50, 0 },	/* TV		*/
	{     0, 99999,  0, 99, 0 },	/* Multi Freq	*/
	{ 58608, 58608, 64, 64, 0 },	/* Hi-res mono	*/
	{ 30000, 70000, 60, 60, 0 },	/* VGA		*/
	{ 30000, 70000, 56, 75, 0 },	/* SVGA		*/
	{ 30000, 70000, 60, 60, 0 }
};

static struct display global_disp;
static struct fb_info fb_info;
static struct acornfb_par current_par;
static struct fb_var_screeninfo __initdata init_var = {};

extern int acornfb_depth;	/* set by setup.c */
extern unsigned int vram_size;	/* set by setup.c */


static struct vidc_timing {
	u_int	h_cycle;
	u_int	h_sync_width;
	u_int	h_border_start;
	u_int	h_display_start;
	u_int	h_display_end;
	u_int	h_border_end;
	u_int	h_interlace;

	u_int	v_cycle;
	u_int	v_sync_width;
	u_int	v_border_start;
	u_int	v_display_start;
	u_int	v_display_end;
	u_int	v_border_end;

	u_int	control;

	/* VIDC20 only */
	u_int	pll_ctl;
} current_vidc;

#ifdef HAS_VIDC

#define VID_CTL_VS_NVSYNC	(1 << 3)
#define VID_CTL_HS_NHSYNC	(1 << 2)
#define VID_CTL_24MHz		(0)
#define VID_CTL_25MHz		(1)
#define VID_CTL_36MHz		(2)

#define VIDC_CTRL_INTERLACE	(1 << 6)
#define VIDC_CTRL_FIFO_0_4	(0 << 4)
#define VIDC_CTRL_FIFO_1_5	(1 << 4)
#define VIDC_CTRL_FIFO_2_6	(2 << 4)
#define VIDC_CTRL_FIFO_3_7	(3 << 4)
#define VIDC_CTRL_1BPP		(0 << 2)
#define VIDC_CTRL_2BPP		(1 << 2)
#define VIDC_CTRL_4BPP		(2 << 2)
#define VIDC_CTRL_8BPP		(3 << 2)
#define VIDC_CTRL_DIV3		(0 << 0)
#define VIDC_CTRL_DIV2		(1 << 0)
#define VIDC_CTRL_DIV1_5	(2 << 0)
#define VIDC_CTRL_DIV1		(3 << 0)

/* CTL     VIDC	Actual
 * 24.000  0	 8.000
 * 25.175  0	 8.392
 * 36.000  0	12.000
 * 24.000  1	12.000
 * 25.175  1	12.588
 * 24.000  2	16.000
 * 25.175  2	16.783
 * 36.000  1	18.000
 * 24.000  3	24.000
 * 36.000  2	24.000
 * 25.175  3	25.175
 * 36.000  3	36.000
 */
static struct pixclock {
	u_long	min_clock;
	u_long	max_clock;
	u_int	vidc_ctl;
	u_int	vid_ctl;
} pixclocks[] = {
	/* we allow +/-1% on these */
	{ 123750, 126250, VIDC_CTRL_DIV3,   VID_CTL_24MHz },	/*  8.000MHz */
	{  82500,  84167, VIDC_CTRL_DIV2,   VID_CTL_24MHz },	/* 12.000MHz */
	{  61875,  63125, VIDC_CTRL_DIV1_5, VID_CTL_24MHz },	/* 16.000MHz */
	{  41250,  42083, VIDC_CTRL_DIV1,   VID_CTL_24MHz },	/* 24.000MHz */
#ifdef CONFIG_ARCH_A5K
	{ 117974, 120357, VIDC_CTRL_DIV3,   VID_CTL_25MHz },	/*  8.392MHz */
	{  78649,  80238, VIDC_CTRL_DIV2,   VID_CTL_25MHz },	/* 12.588MHz */
	{  58987,  60178, VIDC_CTRL_DIV1_5, VID_CTL_25MHz },	/* 16.588MHz */
	{  55000,  56111, VIDC_CTRL_DIV2,   VID_CTL_36MHz },	/* 18.000MHz */
	{  39325,  40119, VIDC_CTRL_DIV1,   VID_CTL_25MHz },	/* 25.175MHz */
	{  27500,  28055, VIDC_CTRL_DIV1,   VID_CTL_36MHz },	/* 36.000MHz */
#endif
	{ 0, }
};

static struct pixclock *
acornfb_valid_pixrate(u_long pixclock)
{
	u_int i;

	for (i = 0; pixclocks[i].min_clock; i++)
		if (pixclock > pixclocks[i].min_clock &&
		    pixclock < pixclocks[i].max_clock)
			return pixclocks + i;

	return NULL;
}

/* VIDC Rules:
 * hcr  : must be even (interlace, hcr/2 must be even)
 * hswr : must be even
 * hdsr : must be odd
 * hder : must be odd
 *
 * vcr  : must be odd
 * vswr : >= 1
 * vdsr : >= 1
 * vder : >= vdsr
 * if interlaced, then hcr/2 must be even
 */
static void
acornfb_set_timing(struct fb_var_screeninfo *var)
{
	struct pixclock *pclk;
	struct vidc_timing vidc;
	u_int horiz_correction;
	u_int sync_len, display_start, display_end, cycle;
	u_int is_interlaced;
	u_int vid_ctl, vidc_ctl;
	u_int bandwidth;

	memset(&vidc, 0, sizeof(vidc));

	pclk = acornfb_valid_pixrate(var->pixclock);
	vidc_ctl = pclk->vidc_ctl;
	vid_ctl  = pclk->vid_ctl;

	bandwidth = var->pixclock * 8 / var->bits_per_pixel;
	/* 25.175, 4bpp = 79.444ns per byte, 317.776ns per word: fifo = 2,6 */
	if (bandwidth > 143500)
		vidc_ctl |= VIDC_CTRL_FIFO_3_7;
	else if (bandwidth > 71750)
		vidc_ctl |= VIDC_CTRL_FIFO_2_6;
	else if (bandwidth > 35875)
		vidc_ctl |= VIDC_CTRL_FIFO_1_5;
	else
		vidc_ctl |= VIDC_CTRL_FIFO_0_4;

	switch (var->bits_per_pixel) {
	case 1:
		horiz_correction = 19;
		vidc_ctl |= VIDC_CTRL_1BPP;
		break;

	case 2:
		horiz_correction = 11;
		vidc_ctl |= VIDC_CTRL_2BPP;
		break;

	case 4:
		horiz_correction = 7;
		vidc_ctl |= VIDC_CTRL_4BPP;
		break;

	default:
	case 8:
		horiz_correction = 5;
		vidc_ctl |= VIDC_CTRL_8BPP;
		break;
	}

	if (!(var->sync & FB_SYNC_HOR_HIGH_ACT))
		vid_ctl |= VID_CTL_HS_NHSYNC;

	if (!(var->sync & FB_SYNC_VERT_HIGH_ACT))
		vid_ctl |= VID_CTL_VS_NVSYNC;

	sync_len	= var->hsync_len;
	display_start	= sync_len + var->left_margin;
	display_end	= display_start + var->xres;
	cycle		= display_end + var->right_margin;

	/* if interlaced, then hcr/2 must be even */
	is_interlaced = (var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED;

	if (is_interlaced) {
		vidc_ctl |= VIDC_CTRL_INTERLACE;
		if (cycle & 2) {
			cycle += 2;
			var->right_margin += 2;
		}
	}

	vidc.h_cycle		= (cycle - 2) / 2;
	vidc.h_sync_width	= (sync_len - 2) / 2;
	vidc.h_border_start	= (display_start - 1) / 2;
	vidc.h_display_start	= (display_start - horiz_correction) / 2;
	vidc.h_display_end	= (display_end - horiz_correction) / 2;
	vidc.h_border_end	= (display_end - 1) / 2;
	vidc.h_interlace	= (vidc.h_cycle + 1) / 2;

	sync_len	= var->vsync_len;
	display_start	= sync_len + var->upper_margin;
	display_end	= display_start + var->yres;
	cycle		= display_end + var->lower_margin;

	if (is_interlaced)
		cycle = (cycle - 3) / 2;
	else
		cycle = cycle - 1;

	vidc.v_cycle		= cycle;
	vidc.v_sync_width	= sync_len - 1;
	vidc.v_border_start	= display_start - 1;
	vidc.v_display_start	= vidc.v_border_start;
	vidc.v_display_end	= display_end - 1;
	vidc.v_border_end	= vidc.v_display_end;

#ifdef CONFIG_ARCH_A5K
	outb(vid_ctl, IOEB_VID_CTL);
#endif
	if (memcmp(&current_vidc, &vidc, sizeof(vidc))) {
		current_vidc = vidc;

		outl(0xe0000000 | vidc_ctl,			IO_VIDC_BASE);
		outl(0x80000000 | (vidc.h_cycle << 14),		IO_VIDC_BASE);
		outl(0x84000000 | (vidc.h_sync_width << 14),	IO_VIDC_BASE);
		outl(0x88000000 | (vidc.h_border_start << 14),	IO_VIDC_BASE);
		outl(0x8c000000 | (vidc.h_display_start << 14),	IO_VIDC_BASE);
		outl(0x90000000 | (vidc.h_display_end << 14),	IO_VIDC_BASE);
		outl(0x94000000 | (vidc.h_border_end << 14),	IO_VIDC_BASE);
		outl(0x98000000,				IO_VIDC_BASE);
		outl(0x9c000000 | (vidc.h_interlace << 14),	IO_VIDC_BASE);
		outl(0xa0000000 | (vidc.v_cycle << 14),		IO_VIDC_BASE);
		outl(0xa4000000 | (vidc.v_sync_width << 14),	IO_VIDC_BASE);
		outl(0xa8000000 | (vidc.v_border_start << 14),	IO_VIDC_BASE);
		outl(0xac000000 | (vidc.v_display_start << 14),	IO_VIDC_BASE);
		outl(0xb0000000 | (vidc.v_display_end << 14),	IO_VIDC_BASE);
		outl(0xb4000000 | (vidc.v_border_end << 14),	IO_VIDC_BASE);
		outl(0xb8000000,				IO_VIDC_BASE);
		outl(0xbc000000,				IO_VIDC_BASE);
	}
#ifdef DEBUG_MODE_SELECTION
	printk(KERN_DEBUG "VIDC registers for %dx%dx%d:\n", var->xres,
	       var->yres, var->bits_per_pixel);
	printk(KERN_DEBUG " H-cycle          : %d\n", vidc.h_cycle);
	printk(KERN_DEBUG " H-sync-width     : %d\n", vidc.h_sync_width);
	printk(KERN_DEBUG " H-border-start   : %d\n", vidc.h_border_start);
	printk(KERN_DEBUG " H-display-start  : %d\n", vidc.h_display_start);
	printk(KERN_DEBUG " H-display-end    : %d\n", vidc.h_display_end);
	printk(KERN_DEBUG " H-border-end     : %d\n", vidc.h_border_end);
	printk(KERN_DEBUG " H-interlace      : %d\n", vidc.h_interlace);
	printk(KERN_DEBUG " V-cycle          : %d\n", vidc.v_cycle);
	printk(KERN_DEBUG " V-sync-width     : %d\n", vidc.v_sync_width);
	printk(KERN_DEBUG " V-border-start   : %d\n", vidc.v_border_start);
	printk(KERN_DEBUG " V-display-start  : %d\n", vidc.v_display_start);
	printk(KERN_DEBUG " V-display-end    : %d\n", vidc.v_display_end);
	printk(KERN_DEBUG " V-border-end     : %d\n", vidc.v_border_end);
	printk(KERN_DEBUG " VIDC Ctrl (E)    : 0x%08X\n", vidc_ctl);
	printk(KERN_DEBUG " IOEB Ctrl        : 0x%08X\n", vid_ctl);
#endif
}

static inline void
acornfb_palette_write(u_int regno, union palette pal)
{
	outl(pal.p, IO_VIDC_BASE);
}

static inline union palette
acornfb_palette_encode(u_int regno, u_int red, u_int green, u_int blue,
		       u_int trans)
{
	union palette pal;

	pal.p = 0;
	pal.vidc.reg   = regno;
	pal.vidc.red   = red >> 12;
	pal.vidc.green = green >> 12;
	pal.vidc.blue  = blue >> 12;
	return pal;
}

static void
acornfb_palette_decode(u_int regno, u_int *red, u_int *green, u_int *blue,
		       u_int *trans)
{
	*red   = EXTEND4(current_par.palette[regno].vidc.red);
	*green = EXTEND4(current_par.palette[regno].vidc.green);
	*blue  = EXTEND4(current_par.palette[regno].vidc.blue);
	*trans = current_par.palette[regno].vidc.trans ? -1 : 0;
}
#endif

#ifdef HAS_VIDC20
/*
 * VIDC20 registers
 */
#define VIDC20_CTRL		0xe0000000
#define VIDC20_CTRL_PIX_VCLK	(0 << 0)
#define VIDC20_CTRL_PIX_HCLK	(1 << 0)
#define VIDC20_CTRL_PIX_RCLK	(2 << 0)
#define VIDC20_CTRL_PIX_CK	(0 << 2)
#define VIDC20_CTRL_PIX_CK2	(1 << 2)
#define VIDC20_CTRL_PIX_CK3	(2 << 2)
#define VIDC20_CTRL_PIX_CK4	(3 << 2)
#define VIDC20_CTRL_PIX_CK5	(4 << 2)
#define VIDC20_CTRL_PIX_CK6	(5 << 2)
#define VIDC20_CTRL_PIX_CK7	(6 << 2)
#define VIDC20_CTRL_PIX_CK8	(7 << 2)
#define VIDC20_CTRL_1BPP	(0 << 5)
#define VIDC20_CTRL_2BPP	(1 << 5)
#define VIDC20_CTRL_4BPP	(2 << 5)
#define VIDC20_CTRL_8BPP	(3 << 5)
#define VIDC20_CTRL_16BPP	(4 << 5)
#define VIDC20_CTRL_32BPP	(6 << 5)
#define VIDC20_CTRL_FIFO_NS	(0 << 8)
#define VIDC20_CTRL_FIFO_4	(1 << 8)
#define VIDC20_CTRL_FIFO_8	(2 << 8)
#define VIDC20_CTRL_FIFO_12	(3 << 8)
#define VIDC20_CTRL_FIFO_16	(4 << 8)
#define VIDC20_CTRL_FIFO_20	(5 << 8)
#define VIDC20_CTRL_FIFO_24	(6 << 8)
#define VIDC20_CTRL_FIFO_28	(7 << 8)
#define VIDC20_CTRL_INT		(1 << 12)
#define VIDC20_CTRL_DUP		(1 << 13)
#define VIDC20_CTRL_PDOWN	(1 << 14)

#define VIDC20_ECTL		0xc0000000
#define VIDC20_ECTL_REG(x)	((x) & 0xf3)
#define VIDC20_ECTL_ECK		(1 << 2)
#define VIDC20_ECTL_REDPED	(1 << 8)
#define VIDC20_ECTL_GREENPED	(1 << 9)
#define VIDC20_ECTL_BLUEPED	(1 << 10)
#define VIDC20_ECTL_DAC		(1 << 12)
#define VIDC20_ECTL_LCDGS	(1 << 13)
#define VIDC20_ECTL_HRM		(1 << 14)

#define VIDC20_ECTL_HS_MASK	(3 << 16)
#define VIDC20_ECTL_HS_HSYNC	(0 << 16)
#define VIDC20_ECTL_HS_NHSYNC	(1 << 16)
#define VIDC20_ECTL_HS_CSYNC	(2 << 16)
#define VIDC20_ECTL_HS_NCSYNC	(3 << 16)

#define VIDC20_ECTL_VS_MASK	(3 << 18)
#define VIDC20_ECTL_VS_VSYNC	(0 << 18)
#define VIDC20_ECTL_VS_NVSYNC	(1 << 18)
#define VIDC20_ECTL_VS_CSYNC	(2 << 18)
#define VIDC20_ECTL_VS_NCSYNC	(3 << 18)

#define VIDC20_DCTL		0xf0000000
/* 0-9 = number of words in scanline */
#define VIDC20_DCTL_SNA		(1 << 12)
#define VIDC20_DCTL_HDIS	(1 << 13)
#define VIDC20_DCTL_BUS_NS	(0 << 16)
#define VIDC20_DCTL_BUS_D31_0	(1 << 16)
#define VIDC20_DCTL_BUS_D63_32	(2 << 16)
#define VIDC20_DCTL_BUS_D63_0	(3 << 16)
#define VIDC20_DCTL_VRAM_DIS	(0 << 18)
#define VIDC20_DCTL_VRAM_PXCLK	(1 << 18)
#define VIDC20_DCTL_VRAM_PXCLK2	(2 << 18)
#define VIDC20_DCTL_VRAM_PXCLK4	(3 << 18)

#define acornfb_valid_pixrate(rate) (1)

/*
 * Try to find the best PLL parameters for the pixel clock.
 * This algorithm seems to give best predictable results,
 * and produces the same values as detailed in the VIDC20
 * data sheet.
 */
static inline u_int
acornfb_vidc20_find_pll(u_int pixclk)
{
	u_int r, best_r = 2, best_v = 2;
	int best_d = 0x7fffffff;

	for (r = 2; r <= 32; r++) {
		u_int rr, v, p;
		int d;

		rr = 41667 * r;

		v = (rr + pixclk / 2) / pixclk;

		if (v > 32 || v < 2)
			continue;

		p = (rr + v / 2) / v;

		d = pixclk - p;

		if (d < 0)
			d = -d;

		if (d < best_d) {
			best_d = d;
			best_v = v - 1;
			best_r = r - 1;
		}

		if (d == 0)
			break;
	}

	return best_v << 8 | best_r;
}

static inline void
acornfb_vidc20_find_rates(struct vidc_timing *vidc,
			  struct fb_var_screeninfo *var)
{
	u_int div, bandwidth;

	/* Select pixel-clock divisor to keep PLL in range */
	div = var->pixclock / 9090; /*9921*/

	/* Limit divisor */
	if (div == 0)
		div = 1;
	if (div > 8)
		div = 8;

	/* Encode divisor to VIDC20 setting */
	switch (div) {
	case 1:	vidc->control |= VIDC20_CTRL_PIX_CK;  break;
	case 2:	vidc->control |= VIDC20_CTRL_PIX_CK2; break;
	case 3:	vidc->control |= VIDC20_CTRL_PIX_CK3; break;
	case 4:	vidc->control |= VIDC20_CTRL_PIX_CK4; break;
	case 5:	vidc->control |= VIDC20_CTRL_PIX_CK5; break;
	case 6:	vidc->control |= VIDC20_CTRL_PIX_CK6; break;
	case 7:	vidc->control |= VIDC20_CTRL_PIX_CK7; break;
	case 8: vidc->control |= VIDC20_CTRL_PIX_CK8; break;
	}

	/* Calculate bandwidth */
	bandwidth = var->pixclock * 8 / var->bits_per_pixel;

	/* Encode bandwidth as VIDC20 setting */
	if (bandwidth > 16667*2)
		vidc->control |= VIDC20_CTRL_FIFO_16;
	else if (bandwidth > 13333*2)
		vidc->control |= VIDC20_CTRL_FIFO_20;
	else if (bandwidth > 11111*2)
		vidc->control |= VIDC20_CTRL_FIFO_24;
	else
		vidc->control |= VIDC20_CTRL_FIFO_28;

	/* Find the PLL values */
	vidc->pll_ctl  = acornfb_vidc20_find_pll(var->pixclock / div);
}

/* VIDC20 has a different set of rules from the VIDC:
 *  hcr  : must be multiple of 4
 *  hswr : must be even
 *  hdsr : must be even
 *  hder : must be even
 *  vcr  : >= 2, (interlace, must be odd)
 *  vswr : >= 1
 *  vdsr : >= 1
 *  vder : >= vdsr
 */
static void
acornfb_set_timing(struct fb_var_screeninfo *var)
{
	struct vidc_timing vidc;
	u_int vcr, fsize;
	u_int ext_ctl, dat_ctl;
	u_int words_per_line;

	memset(&vidc, 0, sizeof(vidc));

	vidc.h_sync_width	= var->hsync_len - 8;
	vidc.h_border_start	= vidc.h_sync_width + var->left_margin + 8 - 12;
	vidc.h_display_start	= vidc.h_border_start + 12 - 18;
	vidc.h_display_end	= vidc.h_display_start + var->xres;
	vidc.h_border_end	= vidc.h_display_end + 18 - 12;
	vidc.h_cycle		= vidc.h_border_end + var->right_margin + 12 - 8;
	vidc.h_interlace	= vidc.h_cycle / 2;
	vidc.v_sync_width	= var->vsync_len - 1;
	vidc.v_border_start	= vidc.v_sync_width + var->upper_margin;
	vidc.v_display_start	= vidc.v_border_start;
	vidc.v_display_end	= vidc.v_display_start + var->yres;
	vidc.v_border_end	= vidc.v_display_end;
	vidc.control		= VIDC20_CTRL_PIX_VCLK;

	vcr = var->vsync_len + var->upper_margin + var->yres +
	      var->lower_margin;

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vidc.v_cycle = (vcr - 3) / 2;
		vidc.control |= VIDC20_CTRL_INT;
	} else
		vidc.v_cycle = vcr - 2;

	switch (var->bits_per_pixel) {
	case  1: vidc.control |= VIDC20_CTRL_1BPP;	break;
	case  2: vidc.control |= VIDC20_CTRL_2BPP;	break;
	case  4: vidc.control |= VIDC20_CTRL_4BPP;	break;
	default:
	case  8: vidc.control |= VIDC20_CTRL_8BPP;	break;
	case 16: vidc.control |= VIDC20_CTRL_16BPP;	break;
	case 32: vidc.control |= VIDC20_CTRL_32BPP;	break;
	}

	acornfb_vidc20_find_rates(&vidc, var);
	fsize = var->vsync_len + var->upper_margin + var->lower_margin - 1;

	if (memcmp(&current_vidc, &vidc, sizeof(vidc))) {
		current_vidc = vidc;

		outl(VIDC20_CTRL| vidc.control,		IO_VIDC_BASE);
		outl(0xd0000000 | vidc.pll_ctl,		IO_VIDC_BASE);
		outl(0x80000000 | vidc.h_cycle,		IO_VIDC_BASE);
		outl(0x81000000 | vidc.h_sync_width,	IO_VIDC_BASE);
		outl(0x82000000 | vidc.h_border_start,	IO_VIDC_BASE);
		outl(0x83000000 | vidc.h_display_start,	IO_VIDC_BASE);
		outl(0x84000000 | vidc.h_display_end,	IO_VIDC_BASE);
		outl(0x85000000 | vidc.h_border_end,	IO_VIDC_BASE);
		outl(0x86000000,			IO_VIDC_BASE);
		outl(0x87000000 | vidc.h_interlace,	IO_VIDC_BASE);
		outl(0x90000000 | vidc.v_cycle,		IO_VIDC_BASE);
		outl(0x91000000 | vidc.v_sync_width,	IO_VIDC_BASE);
		outl(0x92000000 | vidc.v_border_start,	IO_VIDC_BASE);
		outl(0x93000000 | vidc.v_display_start,	IO_VIDC_BASE);
		outl(0x94000000 | vidc.v_display_end,	IO_VIDC_BASE);
		outl(0x95000000 | vidc.v_border_end,	IO_VIDC_BASE);
		outl(0x96000000,			IO_VIDC_BASE);
		outl(0x97000000,			IO_VIDC_BASE);
	}

	outl(fsize, IOMD_FSIZE);

	ext_ctl = VIDC20_ECTL_DAC | VIDC20_ECTL_REG(3);

	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		ext_ctl |= VIDC20_ECTL_HS_HSYNC;
	else
		ext_ctl |= VIDC20_ECTL_HS_NHSYNC;

	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		ext_ctl |= VIDC20_ECTL_VS_VSYNC;
	else
		ext_ctl |= VIDC20_ECTL_VS_NVSYNC;

	outl(VIDC20_ECTL | ext_ctl, IO_VIDC_BASE);

	words_per_line = var->xres * var->bits_per_pixel / 32;

	if (current_par.using_vram && current_par.screen_size == 2048*1024)
		words_per_line /= 2;

	/* RiscPC doesn't use the VIDC's VRAM control. */
	dat_ctl = VIDC20_DCTL_VRAM_DIS | VIDC20_DCTL_SNA | words_per_line;

	/* The data bus width is dependent on both the type
	 * and amount of video memory.
	 *     DRAM	32bit low
	 * 1MB VRAM	32bit
	 * 2MB VRAM	64bit
	 */
	if (current_par.using_vram && current_par.vram_half_sam == 2048) {
		dat_ctl |= VIDC20_DCTL_BUS_D63_0;
	} else 
		dat_ctl |= VIDC20_DCTL_BUS_D31_0;

	outl(VIDC20_DCTL | dat_ctl, IO_VIDC_BASE);

#ifdef DEBUG_MODE_SELECTION
	printk(KERN_DEBUG "VIDC registers for %dx%dx%d:\n", var->xres,
	       var->yres, var->bits_per_pixel);
	printk(KERN_DEBUG " H-cycle          : %d\n", vidc.h_cycle);
	printk(KERN_DEBUG " H-sync-width     : %d\n", vidc.h_sync_width);
	printk(KERN_DEBUG " H-border-start   : %d\n", vidc.h_border_start);
	printk(KERN_DEBUG " H-display-start  : %d\n", vidc.h_display_start);
	printk(KERN_DEBUG " H-display-end    : %d\n", vidc.h_display_end);
	printk(KERN_DEBUG " H-border-end     : %d\n", vidc.h_border_end);
	printk(KERN_DEBUG " H-interlace      : %d\n", vidc.h_interlace);
	printk(KERN_DEBUG " V-cycle          : %d\n", vidc.v_cycle);
	printk(KERN_DEBUG " V-sync-width     : %d\n", vidc.v_sync_width);
	printk(KERN_DEBUG " V-border-start   : %d\n", vidc.v_border_start);
	printk(KERN_DEBUG " V-display-start  : %d\n", vidc.v_display_start);
	printk(KERN_DEBUG " V-display-end    : %d\n", vidc.v_display_end);
	printk(KERN_DEBUG " V-border-end     : %d\n", vidc.v_border_end);
	printk(KERN_DEBUG " Ext Ctrl  (C)    : 0x%08X\n", ext_ctl);
	printk(KERN_DEBUG " PLL Ctrl  (D)    : 0x%08X\n", vidc.pll_ctl);
	printk(KERN_DEBUG " Ctrl      (E)    : 0x%08X\n", vidc.control);
	printk(KERN_DEBUG " Data Ctrl (F)    : 0x%08X\n", dat_ctl);
	printk(KERN_DEBUG " Fsize            : 0x%08X\n", fsize);
#endif
}

static inline void
acornfb_palette_write(u_int regno, union palette pal)
{
	outl(0x10000000 | regno, IO_VIDC_BASE);
	outl(pal.p, IO_VIDC_BASE);
}

static inline union palette
acornfb_palette_encode(u_int regno, u_int red, u_int green, u_int blue,
		       u_int trans)
{
	union palette pal;

	pal.p = 0;
	pal.vidc20.red   = red >> 8;
	pal.vidc20.green = green >> 8;
	pal.vidc20.blue  = blue >> 8;
	return pal;
}

static void
acornfb_palette_decode(u_int regno, u_int *red, u_int *green, u_int *blue,
		       u_int *trans)
{
	*red   = EXTEND8(current_par.palette[regno].vidc20.red);
	*green = EXTEND8(current_par.palette[regno].vidc20.green);
	*blue  = EXTEND8(current_par.palette[regno].vidc20.blue);
	*trans = EXTEND4(current_par.palette[regno].vidc20.ext);
}
#endif

/*
 * Before selecting the timing parameters, adjust
 * the resolution to fit the rules.
 */
static void
acornfb_pre_adjust_timing(struct fb_var_screeninfo *var, int con)
{
	u_int font_line_len;
	u_int fontht;
	u_int sam_size, min_size, size;
	u_int nr_y;

	/* xres must be even */
	var->xres = (var->xres + 1) & ~1;

	/*
	 * We don't allow xres_virtual to differ from xres
	 */
	var->xres_virtual = var->xres;
	var->xoffset = 0;

	/*
	 * Find the font height
	 */
	if (con == -1)
		fontht = fontheight(&global_disp);
	else
		fontht = fontheight(fb_display + con);

	if (fontht == 0)
		fontht = 8;

	if (current_par.using_vram)
		sam_size = current_par.vram_half_sam * 2;
	else
		sam_size = 16;

	/*
	 * Now, find a value for yres_virtual which allows
	 * us to do ywrap scrolling.  The value of
	 * yres_virtual must be such that the end of the
	 * displayable frame buffer must be aligned with
	 * the start of a font line.
	 */
	font_line_len = var->xres * var->bits_per_pixel * fontht / 8;
	min_size = var->xres * var->yres * var->bits_per_pixel / 8;

	/* Find int 'y', such that y * fll == s * sam < maxsize
	 * y = s * sam / fll; s = maxsize / sam
	 */
	for (size = current_par.screen_size; min_size <= size;
	     size -= sam_size) {
		nr_y = size / font_line_len;

		if (nr_y * font_line_len == size)
			break;
	}

	if (min_size > size) {
		/*
		 * failed, use ypan
		 */
		size = current_par.screen_size;
		var->yres_virtual = size / (font_line_len / fontht);
	} else
		var->yres_virtual = nr_y * fontht;

	current_par.screen_end = current_par.screen_base_p + size;

	/*
	 * Fix yres & yoffset if needed.
	 */
	if (var->yres > var->yres_virtual)
		var->yres = var->yres_virtual;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset > var->yres_virtual)
			var->yoffset = var->yres_virtual;
	} else {
		if (var->yoffset + var->yres > var->yres_virtual)
			var->yoffset = var->yres_virtual - var->yres;
	}
}

/*
 * After selecting the timing parameters, adjust
 * the timing to suit the chip.
 * NOTE! Only minor adjustments should be made here.
 */
static void
acornfb_post_adjust_timing(struct fb_var_screeninfo *var)
{
	/* hsync_len must be even */
	var->hsync_len = (var->hsync_len + 1) & ~1;

#ifdef HAS_VIDC
	/* left_margin must be odd */
	if ((var->left_margin & 1) == 0) {
		var->left_margin -= 1;
		var->right_margin += 1;
	}

	/* right_margin must be odd */
	var->right_margin |= 1;
#elif defined(HAS_VIDC20)
	/* left_margin must be even */
	if (var->left_margin & 1) {
		var->left_margin += 1;
		var->right_margin -= 1;
	}

	/* right_margin must be even */
	if (var->right_margin & 1)
		var->right_margin += 1;
#endif

	if (var->vsync_len < 1)
		var->vsync_len = 1;
}

static inline void
acornfb_update_dma(struct fb_var_screeninfo *var)
{
	int off = (var->yoffset * var->xres_virtual *
		   var->bits_per_pixel) >> 3;

#if defined(HAS_MEMC)
	memc_write(VDMA_INIT, off >> 2);
#elif defined(HAS_IOMD)
	outl(current_par.screen_base_p + off, IOMD_VIDINIT);
#endif
}

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

static int
acornfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
		  u_int *trans, struct fb_info *info)
{
	if (regno >= current_par.palette_size)
		return 1;

	acornfb_palette_decode(regno, red, green, blue, trans);

	return 0;
}

static int
acornfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
		  u_int trans, struct fb_info *info)
{
	union palette pal;

	if (regno >= current_par.palette_size)
		return 1;

	pal = acornfb_palette_encode(regno, red, green, blue, trans);
	acornfb_palette_write(regno, pal);
	current_par.palette[regno] = pal;

	if (regno < 16) {
		switch (info->disp->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB16
		case 16:	/* RGB555 */
			current_par.cmap.cfb16[regno] = (regno << 10) | (regno << 5) | regno;
			break;
#endif

		default:
			break;
		}
	}

	return 0;
}

static int
acornfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
		 struct fb_info *info)
{
	int err = 0;

	if (con == current_par.currcon)
		err = fb_get_cmap(cmap, kspc, acornfb_getcolreg, info);
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
		if (con == current_par.currcon)
			err = fb_set_cmap(cmap, kspc, acornfb_setcolreg,
					  info);
		else
			fb_copy_cmap(cmap, &fb_display[con].cmap,
				     kspc ? 0 : 1);
	}
	return err;
}

static int
acornfb_decode_var(struct fb_var_screeninfo *var, int con, int *visual)
{
	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_MFB
	case 1:
		*visual = FB_VISUAL_MONO10;
		break;
#endif
#ifdef FBCON_HAS_CFB8
	case 8:
#ifdef HAS_VIDC
		*visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
#else
		*visual = FB_VISUAL_PSEUDOCOLOR;
#endif
		break;
#endif
#ifdef FBCON_HAS_CFB4
	case 4:
		*visual = FB_VISUAL_PSEUDOCOLOR;
		break;
#endif
#ifdef FBCON_HAS_CFB2
	case 2:
		*visual = FB_VISUAL_PSEUDOCOLOR;
		break;
#endif
	case 16:
	case 24:
	case 32:
		*visual = FB_VISUAL_TRUECOLOR;
	default:
		return -EINVAL;
	}

	if (!acornfb_valid_pixrate(var->pixclock))
		return -EINVAL;

	/*
	 * Adjust the resolution before using it.
	 */
	acornfb_pre_adjust_timing(var, con);

#if defined(HAS_VIDC20)
	var->red.length	   = 8;
	var->transp.length = 4;
#elif defined(HAS_VIDC)
	var->red.length	   = 4;
	var->transp.length = 1;
#endif
	var->green = var->red;
	var->blue  = var->red;

	/*
	 * Now adjust the timing parameters
	 */
	acornfb_post_adjust_timing(var);

	return 0;
}

static int
acornfb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	struct display *display;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, "Acorn");

	if (con >= 0)
		display = fb_display + con;
	else
		display = &global_disp;

	fix->smem_start	 = (char *)current_par.screen_base_p;
	fix->smem_len	 = current_par.screen_size;
	fix->type	 = display->type;
	fix->type_aux	 = display->type_aux;
	fix->xpanstep	 = 0;
	fix->ypanstep	 = display->ypanstep;
	fix->ywrapstep	 = display->ywrapstep;
	fix->visual	 = display->visual;
	fix->line_length = display->line_length;
	fix->accel	 = FB_ACCEL_NONE;

	return 0;
}

static int
acornfb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	if (con == -1) {
		*var = global_disp.var;
	} else
		*var = fb_display[con].var;

	return 0;
}

static int
acornfb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct display *display;
	int err, chgvar = 0, visual;

	if (con >= 0)
		display = fb_display + con;
	else
		display = &global_disp;

	if (!current_par.allow_modeset && con != -1)
		return -EINVAL;

	err = acornfb_decode_var(var, con, &visual);
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
		if (memcmp(&display->var.red, &var->red, sizeof(var->red)))
			chgvar = 1;
		if (memcmp(&display->var.green, &var->green, sizeof(var->green)))
			chgvar = 1;
		if (memcmp(&display->var.blue, &var->blue, sizeof(var->blue)))
			chgvar = 1;
	}

	display->var = *var;
	display->var.activate &= ~FB_ACTIVATE_ALL;

	if (var->activate & FB_ACTIVATE_ALL)
		global_disp.var = display->var;

	display->screen_base	= (char *)current_par.screen_base;
	display->visual		= visual;
	display->type		= FB_TYPE_PACKED_PIXELS;
	display->type_aux	= 0;
	display->ypanstep	= 1;
	display->ywrapstep	= 1;
	display->line_length	=
	display->next_line      = (var->xres * var->bits_per_pixel) / 8;
	display->can_soft_blank	= visual == FB_VISUAL_PSEUDOCOLOR ? 1 : 0;
	display->inverse	= 0;

	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_MFB
	case 1:
		current_par.palette_size = 2;
		display->dispsw = &fbcon_mfb;
		break;
#endif
#ifdef FBCON_HAS_CFB2
	case 2:
		current_par.palette_size = 4;
		display->dispsw = &fbcon_cfb2;
		break;
#endif
#ifdef FBCON_HAS_CFB4
	case 4:
		current_par.palette_size = 16;
		display->dispsw = &fbcon_cfb4;
		break;
#endif
#ifdef FBCON_HAS_CFB8
	case 8:
		current_par.palette_size = VIDC_PALETTE_SIZE;
		display->dispsw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		current_par.palette_size = VIDC_PALETTE_SIZE;
		display->dispsw = &fbcon_cfb16;
		display->dispsw_data = current_par.cmap.cfb16;
		break;
#endif
	default:
		display->dispsw = &fbcon_dummy;
		break;
	}

	if (chgvar && info && info->changevar)
		info->changevar(con);

	if (con == current_par.currcon) {
		struct fb_cmap *cmap;
		unsigned long start, size;
		int control;

#if defined(HAS_MEMC)
		start   = 0;
		size    = current_par.screen_size - VDMA_XFERSIZE;
		control = 0;

		memc_write(VDMA_START, start);
		memc_write(VDMA_END, size >> 2);
#elif defined(HAS_IOMD)

		start = current_par.screen_base_p;
		size  = current_par.screen_end;

		if (current_par.using_vram) {
			size -= current_par.vram_half_sam;
			control = DMA_CR_E | (current_par.vram_half_sam / 256);
		} else {
			size -= 16;
			control = DMA_CR_E | DMA_CR_D | 16;
		}

		outl(start,   IOMD_VIDSTART);
		outl(size,    IOMD_VIDEND);
		outl(control, IOMD_VIDCR);
#endif
		acornfb_update_dma(var);

		if (current_par.allow_modeset)
			acornfb_set_timing(var);

		if (display->cmap.len)
			cmap = &display->cmap;
		else
			cmap = fb_default_cmap(current_par.palette_size);

		fb_set_cmap(cmap, 1, acornfb_setcolreg, info);
	}
	return 0;
}

static int
acornfb_pan_display(struct fb_var_screeninfo *var, int con,
		    struct fb_info *info)
{
	u_int y_bottom;

	if (var->xoffset)
		return -EINVAL;

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (y_bottom > fb_display[con].var.yres_virtual)
		return -EINVAL;

	acornfb_update_dma(var);

	fb_display[con].var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		fb_display[con].var.vmode |= FB_VMODE_YWRAP;
	else
		fb_display[con].var.vmode &= ~FB_VMODE_YWRAP;

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

static int
acornfb_updatevar(int con, struct fb_info *info)
{
	if (con == current_par.currcon)
		acornfb_update_dma(&fb_display[con].var);

	return 0;
}

static int
acornfb_switch(int con, struct fb_info *info)
{
	struct fb_cmap *cmap;

	if (current_par.currcon >= 0) {
		cmap = &fb_display[current_par.currcon].cmap;

		if (cmap->len)
			fb_get_cmap(cmap, 1, acornfb_getcolreg, info);
	}

	current_par.currcon = con;

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	acornfb_set_var(&fb_display[con].var, con, info);

	return 0;
}

static void
acornfb_blank(int blank, struct fb_info *info)
{
	int i;

	if (blank)
		for (i = 0; i < current_par.palette_size; i++) {
			union palette p;

			p = acornfb_palette_encode(i, 0, 0, 0, 0);

			acornfb_palette_write(i, p);
		}
	else
		for (i = 0; i < current_par.palette_size; i++)
			acornfb_palette_write(i, current_par.palette[i]);
}

/*
 * Everything after here is initialisation!!!
 */
struct modey_params {
	u_int	y_res;
	u_int	u_margin;
	u_int	b_margin;
	u_int	vsync_len;
	u_int	vf;
};

struct modex_params {
	u_int	x_res;
	u_int	l_margin;
	u_int	r_margin;
	u_int	hsync_len;
	u_int	clock;
	u_int	hf;
	const struct modey_params *modey;
};

static const struct modey_params modey_640_15600[] __initdata = {
	{  250,  38,  21,  3,  50 },	/*  640x 250, 50Hz */
	{  256,  35,  18,  3,  50 },	/*  640x 256, 50Hz */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_640_26800[] __initdata = {
	{  512,  18,   1,  3,  50 },	/*  640x 512, 50Hz */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_640_31500[] __initdata = {
	{  250, 109,  88,  2,  70 },	/*  640x 250, 70Hz */
	{  256, 106,  85,  2,  70 },	/*  640x 256, 70Hz */
	{  352,  58,  37,  2,  70 },	/*  640x 352, 70Hz */
	{  480,  32,  11,  2,  60 },	/*  640x 480, 60Hz */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_800_35200[] __initdata = {
	{  600,  22,   1,  2,  56 },	/*  800x 600, 56Hz */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_896_21800[] __initdata = {
	{  352,   9,   0,  3,  60 },	/*  896x 352, 60Hz */
	{    0,   0,   0,  0,   0 }
};

/* everything after here is not supported */
static const struct modey_params modey_1024_uk[] __initdata = {
	{  768,   0,   0,  0,   0 },	/* 1024x 768 */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_1056_uk[] __initdata = {
	{  250,   0,   0,  0,   0 },	/* 1056x 250 */
	{  256,   0,   0,  0,   0 },	/* 1056x 256 */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_1152_uk[] __initdata = {
	{  896,   0,   0,  0,   0 },	/* 1152x 896 */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_1280_63600[] __initdata = {
	{ 1024,   0,   0,  0,  60 },	/* 1280x1024, 60Hz */
	{    0,   0,   0,  0,   0 }
};

static const struct modey_params modey_1600_uk[] __initdata = {
	{ 1280,   0,   0,  0,   0 },	/* 1600x1280 */
	{    0,   0,   0,  0,   0 }
};

/*
 * Horizontal video programming requirements.
 * This table is searched for the required horizontal
 * and required frequency, and then the tables above
 * are then searched for the required vertical
 * resolution.
 *
 * NOTE! we can match multiple entries, so we search
 * all horizontal entries for which the hfreq is within
 * the monitor's range.
 */
static const struct modex_params modex_params[] __initdata = {
	{						/* X:  640, 15.6kHz */
		 640,  185, 123,  76, 16000, 15625, modey_640_15600
	},
	{						/* X:  640, 26.8kHz */
		 640,  113,  87,  56, 24000, 26800, modey_640_26800
	},
	{						/* X:  640, 31.5kHz */
		 640,   48,  16,  96, 25175, 31500, modey_640_31500
	},
	{						/* X:  800, 35.2kHz */
		 800,  101,  23, 100, 36000, 35200, modey_800_35200
	},
	{						/* X:  896, 21.8kHz */
		 896,   59,  27, 118, 24000, 21800, modey_896_21800
	},
	{						/* X: 1024 */
		1024,    0,   0,   0,     0,     0, modey_1024_uk
	},
	{						/* X: 1056 */
		1056,    0,   0,   0,     0,     0, modey_1056_uk
	},
	{						/* X: 1152 */
		1152,    0,   0,   0,     0,     0, modey_1152_uk
	},
	{						/* X: 1280, 63.6kHz */
		1280,    0,   0,   0,     0, 63600, modey_1280_63600
	},
	{						/* X: 1600 */
		1600,    0,   0,   0,     0,     0, modey_1600_uk
	},
	{
		0,
	}
};

__initfunc(static int
acornfb_lookup_timing(struct fb_var_screeninfo *var))
{
	const struct modex_params *x;
	const struct modey_params *y;

	/*
	 * We must adjust the resolution parameters
	 * before selecting the timing parameters.
	 */
	acornfb_pre_adjust_timing(var, -1);

	for (x = modex_params; x->x_res; x++) {

		/*
		 * Is this resolution one we're looking for?
		 */
		if (x->x_res != var->xres)
			continue;

		/*
		 * Is the hsync frequency ok for our monitor?
		 */
		if (x->hf > fb_info.monspecs.hfmax ||
		    x->hf < fb_info.monspecs.hfmin)
			continue;

		/*
		 * Try to find a vertical resolution
		 */
		for (y = x->modey; y->y_res; y++) {
			/*
			 * Is this resolution one we're looking for?
			 */
			if (y->y_res != var->yres)
				continue;

			/*
			 * Is the vsync frequency ok for our monitor?
			 */
			if (y->vf > fb_info.monspecs.vfmax ||
			    y->vf < fb_info.monspecs.vfmin)
				continue;

			goto found;
		}
	}

	var->pixclock = 0;

	return -EINVAL;

found:
	/*
	 * Why is pixclock in picoseconds?
	 */
	switch (x->clock) {
	case 36000: var->pixclock =  27778;	break;
	case 25175: var->pixclock =  39722;	break;
	case 24000: var->pixclock =  41667;	break;
	case 16000: var->pixclock =  62500;	break;
	case 12000: var->pixclock =  83333;	break;
	case  8000: var->pixclock = 125000;	break;
	default:    var->pixclock =      0;     break;
	}

#ifdef DEBUG_MODE_SELECTION
	printk(KERN_DEBUG "Found %dx%d at %d.%3dkHz, %dHz, pix %d\n",
		x->x_res, y->y_res,
		x->hf / 1000, x->hf % 1000,
		y->vf, var->pixclock);
#endif

	var->left_margin	= x->l_margin;
	var->right_margin	= x->r_margin;
	var->upper_margin	= y->u_margin;
	var->lower_margin	= y->b_margin;
	var->hsync_len		= x->hsync_len;
	var->vsync_len		= y->vsync_len;
	var->sync		= 0;

	/*
	 * Now adjust the parameters we found
	 */
	acornfb_post_adjust_timing(var);

	return 0;
}

__initfunc(static void
acornfb_init_fbinfo(void))
{
	static int first = 1;

	if (!first)
		return;
	first = 0;

	strcpy(fb_info.modename, "Acorn");
	strcpy(fb_info.fontname, "Acorn8x8");

	fb_info.node		   = -1;
	fb_info.fbops		   = &acornfb_ops;
	fb_info.disp		   = &global_disp;
	fb_info.changevar	   = NULL;
	fb_info.switch_con	   = acornfb_switch;
	fb_info.updatevar	   = acornfb_updatevar;
	fb_info.blank		   = acornfb_blank;
	fb_info.flags		   = FBINFO_FLAG_DEFAULT;

	global_disp.dispsw	   = &fbcon_dummy;

	/*
	 * setup initial parameters
	 */
	memset(&init_var, 0, sizeof(init_var));
	init_var.xres		   = DEFAULT_XRES;
	init_var.yres		   = DEFAULT_YRES;

#if   defined(FBCON_HAS_CFB4)
	init_var.bits_per_pixel	   = 4;
#elif defined(FBCON_HAS_CFB8)
	init_var.bits_per_pixel    = 8;
#elif defined(FBCON_HAS_CFB2)
	init_var.bits_per_pixel    = 2;
#elif defined(FBCON_HAS_MFB)
	init_var.bits_per_pixel    = 1;
#else
#error No suitable framebuffers configured
#endif

#if defined(HAS_VIDC20)
	init_var.red.length	   = 8;
	init_var.transp.length	   = 4;
#elif defined(HAS_VIDC)
	init_var.red.length	   = 4;
	init_var.transp.length	   = 1;
#endif
	init_var.green		   = init_var.red;
	init_var.blue		   = init_var.red;
	init_var.nonstd		   = 0;
	init_var.activate	   = FB_ACTIVATE_NOW;
	init_var.height		   = -1;
	init_var.width		   = -1;
	init_var.vmode		   = FB_VMODE_NONINTERLACED;

	current_par.dram_size	   = 0;
	current_par.montype	   = -1;
	current_par.dpms	   = 0;
}

/*
 * setup acornfb options:
 *
 *  font:fontname
 *	Set fontname
 *
 *  mon:hmin-hmax:vmin-vmax:dpms:width:height
 *	Set monitor parameters:
 *		hmin   = horizontal minimum frequency (Hz)
 *		hmax   = horizontal maximum frequency (Hz)	(optional)
 *		vmin   = vertical minimum frequency (Hz)
 *		vmax   = vertical maximum frequency (Hz)	(optional)
 *		dpms   = DPMS supported?			(optional)
 *		width  = width of picture in mm.		(optional)
 *		height = height of picture in mm.		(optional)
 *
 * montype:type
 *	Set RISC-OS style monitor type:
 *		0 (or tv)	- TV frequency
 *		1 (or multi)	- Multi frequency
 *		2 (or hires)	- Hi-res monochrome
 *		3 (or vga)	- VGA
 *		4 (or svga)	- SVGA
 *		auto, or option missing
 *				- try hardware detect
 *
 * dram:size
 *	Set the amount of DRAM to use for the frame buffer
 *	(even if you have VRAM).
 *	size can optionally be followed by 'M' or 'K' for
 *	MB or KB respectively.
 */
__initfunc(static void
acornfb_parse_font(char *opt))
{
	strcpy(fb_info.fontname, opt);
}

__initfunc(static void
acornfb_parse_mon(char *opt))
{
	fb_info.monspecs.hfmin = simple_strtoul(opt, &opt, 0);
	if (*opt == '-')
		fb_info.monspecs.hfmax = simple_strtoul(opt + 1, &opt, 0);
	else
		fb_info.monspecs.hfmax = fb_info.monspecs.hfmin;

	if (*opt != ':')
		return;

	fb_info.monspecs.vfmin = simple_strtoul(opt + 1, &opt, 0);
	if (*opt == '-')
		fb_info.monspecs.vfmax = simple_strtoul(opt + 1, &opt, 0);
	else
		fb_info.monspecs.vfmax = fb_info.monspecs.vfmin;

	if (*opt != ':')
		return;

	fb_info.monspecs.dpms = simple_strtoul(opt + 1, &opt, 0);

	if (*opt != ':')
		return;

	init_var.width = simple_strtoul(opt + 1, &opt, 0);

	if (*opt != ':')
		return;

	init_var.height = simple_strtoul(opt + 1, NULL, 0);
}

__initfunc(static void
acornfb_parse_montype(char *opt))
{
	current_par.montype = -2;

	if (strncmp(opt, "tv", 2) == 0) {
		opt += 2;
		current_par.montype = 0;
	} else if (strncmp(opt, "multi", 5) == 0) {
		opt += 5;
		current_par.montype = 1;
	} else if (strncmp(opt, "hires", 5) == 0) {
		opt += 5;
		current_par.montype = 2;
	} else if (strncmp(opt, "vga", 3) == 0) {
		opt += 3;
		current_par.montype = 3;
	} else if (strncmp(opt, "svga", 4) == 0) {
		opt += 4;
		current_par.montype = 4;
	} else if (strncmp(opt, "auto", 4) == 0) {
		opt += 4;
		current_par.montype = -1;
	} else if (isdigit(*opt))
		current_par.montype = simple_strtoul(opt, &opt, 0);

	if (current_par.montype == -2 ||
	    current_par.montype > NR_MONTYPES) {
		printk(KERN_ERR "acornfb: unknown monitor type: %s\n",
			opt);
		current_par.montype = -1;
	} else
	if (opt && *opt) {
		if (strcmp(opt, ",dpms") == 0)
			current_par.dpms = 1;
		else
			printk(KERN_ERR
			       "acornfb: unknown monitor option: %s\n",
			       opt);
	}
}

__initfunc(static void
acornfb_parse_dram(char *opt))
{
	unsigned int size;

	size = simple_strtoul(opt, &opt, 0);

	if (opt) {
		switch (*opt) {
		case 'M':
		case 'm':
			size *= 1024;
		case 'K':
		case 'k':
			size *= 1024;
		default:
			break;
		}
	}

	current_par.dram_size = size;
}

static struct options {
	char *name;
	void (*parse)(char *opt);
} opt_table[] __initdata = {
	{ "font",    acornfb_parse_font    },
	{ "mon",     acornfb_parse_mon     },
	{ "montype", acornfb_parse_montype },
	{ "dram",    acornfb_parse_dram    },
	{ NULL, NULL }
};

__initfunc(void
acornfb_setup(char *options, int *ints))
{
	struct options *optp;
	char *opt;

	if (!options || !*options)
		return;

	acornfb_init_fbinfo();

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
			printk(KERN_ERR "acornfb: unknown parameter: %s\n",
			       opt);
	}
}

/*
 * Detect type of monitor connected
 *  For now, we just assume SVGA
 */
__initfunc(static int
acornfb_detect_monitortype(void))
{
	return 4;
}

/*
 * This enables the unused memory to be freed on older Acorn machines.
 */
static inline void
free_unused_pages(unsigned int virtual_start, unsigned int virtual_end)
{
	int mb_freed = 0;

	/*
	 * Align addresses
	 */
	virtual_start = PAGE_ALIGN(virtual_start);
	virtual_end = PAGE_ALIGN(virtual_end);

	while (virtual_start < virtual_end) {
		/*
		 * Clear page reserved bit,
		 * set count to 1, and free
		 * the page.
		 */
		clear_bit(PG_reserved, &mem_map[MAP_NR(virtual_start)].flags);
		atomic_set(&mem_map[MAP_NR(virtual_start)].count, 1);
		free_page(virtual_start);

		virtual_start += PAGE_SIZE;
		mb_freed += PAGE_SIZE / 1024;
	}

	printk("acornfb: freed %dK memory\n", mb_freed);
}

__initfunc(void
acornfb_init(void))
{
	unsigned long size;
	u_int h_sync, v_sync;

	acornfb_init_fbinfo();

	if (current_par.montype == -1)
		current_par.montype = acornfb_detect_monitortype();

	if (current_par.montype < 0 || current_par.montype > NR_MONTYPES)
		current_par.montype = 4;

	fb_info.monspecs = monspecs[current_par.montype];
	fb_info.monspecs.dpms = current_par.dpms;

	current_par.currcon	   = -1;
	current_par.screen_base	   = SCREEN2_BASE;
	current_par.screen_base_p  = SCREEN_START;
	current_par.using_vram     = 0;

	/*
	 * If vram_size is set, we are using VRAM in
	 * a Risc PC.  However, if the user has specified
	 * an amount of DRAM then use that instead.
	 */
	if (vram_size && !current_par.dram_size) {
		size = vram_size;
		current_par.vram_half_sam = vram_size / 1024;
		current_par.using_vram = 1;
	} else if (current_par.dram_size)
		size = current_par.dram_size;
	else
		size = (init_var.xres * init_var.yres *
			init_var.bits_per_pixel) / 8;

	size = PAGE_ALIGN(size);

#ifdef CONFIG_ARCH_RPC
	if (!current_par.using_vram) {
		/*
		 * RiscPC needs to allocate the DRAM memory
		 * for the framebuffer if we are not using
		 * VRAM.  Archimedes/A5000 machines use a
		 * fixed address for their framebuffers.
		 */
		current_par.screen_base = (unsigned long)kmalloc(size, GFP_KERNEL);
		if (current_par.screen_base == 0) {
			printk(KERN_ERR "acornfb: unable to allocate screen "
			       "memory\n");
			return;
		}
		current_par.screen_base_p =
			virt_to_phys(current_par.screen_base);
	}
#endif
#if defined(CONFIG_ARCH_A5K) || defined(CONFIG_ARCH_ARC)
#define MAX_SIZE	480*1024
	/*
	 * Limit maximum screen size.
	 */
	if (size > MAX_SIZE)
		size = MAX_SIZE;

	/*
	 * Free unused pages
	 */
	free_unused_pages(PAGE_OFFSET + size, PAGE_OFFSET + MAX_SIZE);
#endif
	
	current_par.screen_size	   = size;
	current_par.palette_size   = VIDC_PALETTE_SIZE;
	current_par.allow_modeset  = 1;

	/*
	 * Lookup the timing for this resolution.  If we can't
	 * find it, then we can't restore it if we change
	 * the resolution, so we disable this feature.
	 */
	if (acornfb_lookup_timing(&init_var))
		current_par.allow_modeset = 0;

	/*
	 * Again, if this does not succeed, then we disallow
	 * changes to the resolution parameters.
	 */
	if (acornfb_set_var(&init_var, -1, &fb_info))
		current_par.allow_modeset = 0;

	h_sync = 1953125000 / init_var.pixclock;
	h_sync = h_sync * 512 / (init_var.xres + init_var.left_margin +
		 init_var.right_margin + init_var.hsync_len);
	v_sync = h_sync / (init_var.yres + init_var.upper_margin +
		 init_var.lower_margin + init_var.vsync_len);

	printk("Acornfb: %ldkB %cRAM, %s, using %dx%d, %d.%03dkHz, %dHz\n",
		current_par.screen_size / 1024,
		current_par.using_vram ? 'V' : 'D',
		VIDC_NAME, init_var.xres, init_var.yres,
		h_sync / 1000, h_sync % 1000, v_sync);

	register_framebuffer(&fb_info);
}
