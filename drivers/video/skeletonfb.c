/*
 * linux/drivers/video/skeletonfb.c -- Skeleton for a frame buffer device
 *
 *  Created 28 Dec 1997 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
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

#include "fbcon.h"


    /*
     *  This is just simple sample code.
     *
     *  No warranty that it actually compiles.
     *  Even less warranty that it actually works :-)
     */


struct xxxfb_info {
    /*
     *  Choose _one_ of the two alternatives:
     *
     *    1. Use the generic frame buffer operations (fbgen_*).
     */
    struct fb_info_gen gen;
    /*
     *    2. Provide your own frame buffer operations.
     */
    struct fb_info info;

    /* Here starts the frame buffer device dependent part */
    /* You can use this to store e.g. the board number if you support */
    /* multiple boards */
};


struct xxxfb_par {
    /*
     *  The hardware specific data in this structure uniquely defines a video
     *  mode.
     *
     *  If your hardware supports only one video mode, you can leave it empty.
     */
};


    /*
     *  If your driver supports multiple boards, you should make these arrays,
     *  or allocate them dynamically (using mem_start for builtin drivers, and
     *  kmalloc() for loaded modules).
     */

static struct xxxfb_info fb_info;
static struct xxxfb_par current_par;
static int current_par_valid = 0;
static struct display disp;

static struct fb_var_screeninfo default_var;

static int currcon = 0;
static int inverse = 0;


/* ------------------- chipset specific functions -------------------------- */


static void xxx_detect(void)
{
    /*
     *  This function should detect the current video mode settings and store
     *  it as the default video mode
     */

    /* ... */
    xxx_get_par(&par);
    xxx_encode_var(&default_var, &par);
}

static int xxx_encode_fix(struct fb_fix_screeninfo *fix, struct xxxfb_par *par,
			  const struct fb_info *fb_info)
{
    /*
     *  This function should fill in the 'fix' structure based on the values
     *  in the `par' structure.
     */

    /* ... */
    return 0;
}

static int xxx_decode_var(struct fb_var_screeninfo *var, struct xxxfb_par *par,
			  const struct fb_info *fb_info)
{
    /*
     *  Get the video params out of 'var'. If a value doesn't fit, round it up,
     *  if it's too big, return -EINVAL.
     *
     *  Suggestion: Round up in the following order: bits_per_pixel, xres,
     *  yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
     *  bitfields, horizontal timing, vertical timing.
     */

    /* ... */

    /* pixclock in picos, htotal in pixels, vtotal in scanlines */
    if (!fbmon_valid_timings(pixclock, htotal, vtotal, info))
	    return -EINVAL;

    return 0;
}

static int xxx_encode_var(struct fb_var_screeninfo *var, struct xxxfb_par *par,
			  const struct fb_info *fb_info)
{
    /*
     *  Fill the 'var' structure based on the values in 'par' and maybe other
     *  values read out of the hardware.
     */

    /* ... */
    return 0;
}

static void xxx_get_par(struct xxxfb_par *par, const struct fb_info *fb_info)
{
    /*
     *  Fill the hardware's 'par' structure.
     */

    if (current_par_valid)
	*par = current_par;
    else {
	/* ... */
    }
}

static void xxx_set_par(struct xxxfb_par *par, const struct fb_info *fb_info)
{
    /*
     *  Set the hardware according to 'par'.
     */

    current_par = *par;
    current_par_valid = 1;
    /* ... */
}

static int xxx_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			 unsigned *blue, unsigned *transp,
			 const struct fb_info *fb_info)
{
    /*
     *  Read a single color register and split it into colors/transparent.
     *  Return != 0 for invalid regno.
     */

    /* ... */
    return 0;
}

static int xxx_setcolreg(unsigned regno, unsigned red, unsigned green,
			 unsigned blue, unsigned transp,
			 const struct fb_info *fb_info)
{
    /*
     *  Set a single color register. The values supplied are already rounded
     *  down to the hardware's capabilities (according to the entries in the
     *  `var' structure). Return != 0 for invalid regno.
     */

    if (regno < 16) {
	/*
	 *  Make the first 16 colors of the palette available to fbcon
	 */
	if (is_cfb15)		/* RGB 555 */
	    fbcon_cfb15_cmap[regno] = be16_to_cpu((red << 10) | (green << 5) |
						  blue);
	if (is_cfb16)		/* RGB 565 */
	    fbcon_cfb16_cmap[regno] = be16_to_cpu((red << 11) | (green << 5) |
						  blue);
	if (is_cfb24)		/* RGB 888 */
	    fbcon_cfb24_cmap[regno] = be32_to_cpu((red << 16) | (green << 8) |
						  blue);
	if (is_cfb32)		/* RGBA 8888 */
	    fbcon_cfb32_cmap[regno] = be32_to_cpu((red << 24) | (green << 16) |
						  (blue << 8) | transp);
    }
    /* ... */
    return 0;
}

static int xxx_pan_display(struct fb_var_screeninfo *var,
			   struct xxxfb_par *par,
			   const struct fb_info *fb_info)
{
    /*
     *  Pan (or wrap, depending on the `vmode' field) the display using the
     *  `xoffset' and `yoffset' fields of the `var' structure.
     *  If the values don't fit, return -EINVAL.
     */

    /* ... */
    return 0;
}

static int xxx_blank(int blank_mode, const struct fb_info *fb_info)
{
    /*
     *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
     *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
     *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
     *  to e.g. a video mode which doesn't support it. Implements VESA suspend
     *  and powerdown modes on hardware that supports disabling hsync/vsync:
     *    blank_mode == 2: suspend vsync
     *    blank_mode == 3: suspend hsync
     *    blank_mode == 4: powerdown
     */

    /* ... */
    return 0;
}

static struct display_switch *xxx_get_dispsw(const void *par,
					     struct fb_info_gen *info)
{
    /*
     *  Return a pointer to appropriate low level text console operations for
     *  the video mode `par' of your video hardware. These can be generic
     *  software routines, or hardware accelerated routines specifically
     *  tailored for your hardware.
     *  If you don't have any appropriate operations, simple fill in the NULL
     *  pointer, and there will be no text output.
     */
#ifdef CONFIG_FBCON_CFB8
    if (is_cfb8)
	return &fbcon_cfb8;
#endif
#ifdef CONFIG_FBCON_CFB16
    if (is_cfb16)
	return &fbcon_cfb16;
#endif
#ifdef CONFIG_FBCON_CFB32
    if (is_cfb32)
	return &fbcon_cfb32;
#endif
    return NULL;
}


/* ------------ Interfaces to hardware functions ------------ */


struct fbgen_hwswitch xxx_switch = {
    xxx_detect, xxx_encode_fix, xxx_decode_var, xxx_encode_var, xxx_get_par,
    xxx_set_par, xxx_getcolreg, xxx_setcolreg, xxx_blank, xxx_dispsw
};



/* ------------ Hardware Independant Functions ------------ */


    /*
     *  Initialization
     */

__initfunc(unsigned long xxxfb_init(unsigned long mem_start))
{
    int err;
    struct fb_var_screeninfo var;

    fb_info.fbhw = &xxx_switch;
    fbhw->detect();
    strcpy(fb_info.modename, "XXX");
    fb_info.changevar = NULL;
    fb_info.node = -1;
    fb_info.fbops = &xxxfb_ops;
    fb_info.disp = disp;
    fb_info.switch_con = &xxxfb_switch;
    fb_info.updatevar = &xxxfb_update_var;
    fb_info.blank = &xxxfb_blank;
    /* This should give a reasonable default video mode */
    fbgen_get_var(&disp.var, -1, &fb_info.gen);
    fbgen_do_set_var(var, 1, &fbinfo.gen);
    err = register_framebuffer(&fb_info.gen.info);
    if (err < 0)
	return err;
    fbgen_set_disp(-1, &fb_info.gen.info);
    fbgen_install_cmap(0, &fb_info.gen);
    printk("fb%d: %s frame buffer device\n", GET_FB_IDX(fb_info.node),
	   fb_info.modename);

    /* uncomment this if your driver cannot be unloaded */
    /* MOD_INC_USE_COUNT; */

    return mem_start;
}


    /*
     *  Cleanup
     */

void xxxfb_cleanup(struct fb_info *info)
{
    /*
     *  If your driver supports multiple boards, you should unregister and
     *  clean up all instances.
     */

    unregister_framebuffer(&fb_info);
    /* ... */
}


    /*
     *  Setup
     */

__initfunc(void xxxfb_setup(char *options, int *ints))
{
    /* Parse user speficied options (`video=xxxfb:') */
}


/* ------------------------------------------------------------------------- */


    /*
     *  Frame buffer operations
     */

static int xxxfb_open(const struct fb_info *info)
{
    /* Nothing, only a usage count for the moment */
    MOD_INC_USE_COUNT;
    return 0;
}

static int xxxfb_release(const struct fb_info *info)
{
    MOD_DEC_USE_COUNT;
    return 0;
}


    /*
     *  In most cases the `generic' routines (fbgen_*) should be satisfactory.
     *  However, you're free to fill in your own replacements.
     */

static struct fb_ops xxxfb_ops = {
    xxxfb_open, xxxfb_release, fbgen_get_fix, fbgen_get_var, fbgen_set_var,
    fbgen_get_cmap, fbgen_set_cmap, fbgen_pan_display, NULL, fbgen_ioctl
};


/* ------------------------------------------------------------------------- */


    /*
     *  Modularization
     */

#ifdef MODULE
int init_module(void)
{
    return xxxfb_init(NULL);
}

void cleanup_module(void)
{
    xxxfb_cleanup(void);
}
#endif /* MODULE */
