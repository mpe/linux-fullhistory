/*
 *  linux/drivers/video/vfb.c -- Virtual frame buffer device
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

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
#include <asm/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>


#define arraysize(x)	(sizeof(x)/sizeof(*(x)))


    /*
     *  RAM we reserve for the frame buffer. This defines the maximum screen
     *  size
     *
     *  The default can be overridden if the driver is compiled as a module
     */

#define VIDEOMEMSIZE	(1*1024*1024)	/* 1 MB */

static u_long videomemory, videomemorysize = VIDEOMEMSIZE;
MODULE_PARM(videomemorysize, "l");
static int currcon = 0;
static struct display disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[256];
static char virtual_fb_name[16] = "Virtual FB";

static struct fb_var_screeninfo virtual_fb_predefined[] = {

    /*
     *  Autodetect (Default) Video Mode
     */

    {
	/* 640x480, 8 bpp */
	640, 480, 640, 480, 0, 0, 8, 0,
	{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	0, 0, -1, -1, FB_ACCEL_NONE, 20000, 64, 64, 32, 32, 64, 2,
	0, FB_VMODE_NONINTERLACED
    },

    /*
     *  User Defined Video Modes (8)
     */

    { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }, { 0, }
};

#define NUM_USER_MODES		(8)
#define NUM_TOTAL_MODES		arraysize(virtual_fb_predefined)
#define NUM_PREDEF_MODES	(1)


    /*
     *  Interface used by the world
     */

void vfb_video_setup(char *options, int *ints);

static int virtual_fb_open(int fbidx);
static int virtual_fb_release(int fbidx);
static int virtual_fb_get_fix(struct fb_fix_screeninfo *fix, int con);
static int virtual_fb_get_var(struct fb_var_screeninfo *var, int con);
static int virtual_fb_set_var(struct fb_var_screeninfo *var, int con);
static int virtual_fb_pan_display(struct fb_var_screeninfo *var, int con);
static int virtual_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con);
static int virtual_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con);
static int virtual_fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			    u_long arg, int con);


    /*
     *  Interface to the low level console driver
     */

unsigned long virtual_fb_init(unsigned long mem_start);
static int vfbcon_switch(int con);
static int vfbcon_updatevar(int con);
static void vfbcon_blank(int blank);
static int vfbcon_setcmap(struct fb_cmap *cmap, int con);


    /*
     *  Internal routines
     */

static u_long get_line_length(int xres_virtual, int bpp);
static void vfb_encode_fix(struct fb_fix_screeninfo *fix,
			   struct fb_var_screeninfo *var);
static void set_color_bitfields(struct fb_var_screeninfo *var);
static int vfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp);
static int vfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp);
static void do_install_cmap(int con);


static struct fb_ops virtual_fb_ops = {
    virtual_fb_open, virtual_fb_release, virtual_fb_get_fix,
    virtual_fb_get_var, virtual_fb_set_var, virtual_fb_get_cmap,
    virtual_fb_set_cmap, virtual_fb_pan_display, virtual_fb_ioctl
};


    /*
     *  Open/Release the frame buffer device
     */

static int virtual_fb_open(int fbidx)                                       
{
    /*                                                                     
     *  Nothing, only a usage count for the moment                          
     */                                                                    

    MOD_INC_USE_COUNT;
    return(0);                              
}
        
static int virtual_fb_release(int fbidx)
{
    MOD_DEC_USE_COUNT;
    return(0);                                                    
}


    /*
     *  Get the Fixed Part of the Display
     */

static int virtual_fb_get_fix(struct fb_fix_screeninfo *fix, int con)
{
    struct fb_var_screeninfo *var;

    if (con == -1)
	var = &virtual_fb_predefined[0];
    else
	var = &fb_display[con].var;
    vfb_encode_fix(fix, var);
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int virtual_fb_get_var(struct fb_var_screeninfo *var, int con)
{
    if (con == -1)
	*var = virtual_fb_predefined[0];
    else
	*var = fb_display[con].var;
    set_color_bitfields(var);
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int virtual_fb_set_var(struct fb_var_screeninfo *var, int con)
{
    int err, activate = var->activate;
    int oldxres, oldyres, oldvxres, oldvyres, oldbpp;
    u_long line_length;

    struct display *display;
    if (con >= 0)
	display = &fb_display[con];
    else
	display = &disp;	/* used during initialization */

    /*
     *  FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
     *  as FB_VMODE_SMOOTH_XPAN is only used internally
     */

    if (var->vmode & FB_VMODE_CONUPDATE) {
	var->vmode |= FB_VMODE_YWRAP;
	var->xoffset = display->var.xoffset;
	var->yoffset = display->var.yoffset;
    }

    /*
     *  Some very basic checks
     */
    if (!var->xres)
	var->xres = 1;
    if (!var->yres)
	var->yres = 1;
    if (var->xres > var->xres_virtual)
	var->xres_virtual = var->xres;
    if (var->yres > var->yres_virtual)
	var->yres_virtual = var->yres;
    if (var->bits_per_pixel <= 1)
	var->bits_per_pixel = 1;
    else if (var->bits_per_pixel <= 8)
	var->bits_per_pixel = 8;
    else if (var->bits_per_pixel <= 16)
	var->bits_per_pixel = 16;
#if 0
    /* fbcon doesn't support this (yet) */
    else if (var->bits_per_pixel <= 24)
	var->bits_per_pixel = 24;
    else if (var->bits_per_pixel <= 32)
	var->bits_per_pixel = 32;
#endif
    else
	return -EINVAL;

    /*
     *  Memory limit
     */
    line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
    if (line_length*var->yres_virtual > videomemorysize)
	return -ENOMEM;

    set_color_bitfields(var);

    if ((activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	oldxres = display->var.xres;
	oldyres = display->var.yres;
	oldvxres = display->var.xres_virtual;
	oldvyres = display->var.yres_virtual;
	oldbpp = display->var.bits_per_pixel;
	display->var = *var;
	if (oldxres != var->xres || oldyres != var->yres ||
	    oldvxres != var->xres_virtual || oldvyres != var->yres_virtual ||
	    oldbpp != var->bits_per_pixel) {
	    struct fb_fix_screeninfo fix;

	    vfb_encode_fix(&fix, var);
	    display->screen_base = (u_char *)fix.smem_start;
	    display->visual = fix.visual;
	    display->type = fix.type;
	    display->type_aux = fix.type_aux;
	    display->ypanstep = fix.ypanstep;
	    display->ywrapstep = fix.ywrapstep;
	    display->line_length = fix.line_length;
	    display->can_soft_blank = 1;
	    display->inverse = 0;
	    if (fb_info.changevar)
		(*fb_info.changevar)(con);
	}
	if (oldbpp != var->bits_per_pixel) {
	    if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
		return err;
	    do_install_cmap(con);
	}
    }
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int virtual_fb_pan_display(struct fb_var_screeninfo *var, int con)
{
    if (var->vmode & FB_VMODE_YWRAP) {
	if (var->yoffset < 0 ||
	    var->yoffset >= fb_display[con].var.yres_virtual ||
	    var->xoffset)
	    return -EINVAL;
    } else {
	if (var->xoffset+fb_display[con].var.xres >
	    fb_display[con].var.xres_virtual ||
	    var->yoffset+fb_display[con].var.yres >
	    fb_display[con].var.yres_virtual)
	    return -EINVAL;
    }
    fb_display[con].var.xoffset = var->xoffset;
    fb_display[con].var.yoffset = var->yoffset;
    if (var->vmode & FB_VMODE_YWRAP)
	fb_display[con].var.vmode |= FB_VMODE_YWRAP;
    else
	fb_display[con].var.vmode &= ~FB_VMODE_YWRAP;
    return 0;
}

    /*
     *  Get the Colormap
     */

static int virtual_fb_get_cmap(struct fb_cmap *cmap, int kspc, int con)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, &fb_display[con].var, kspc, vfb_getcolreg);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else
	fb_copy_cmap(fb_default_cmap(fb_display[con].var.bits_per_pixel),
		     cmap, kspc ? 0 : 2);
    return 0;
}

    /*
     *  Set the Colormap
     */

static int virtual_fb_set_cmap(struct fb_cmap *cmap, int kspc, int con)
{
    int err;

    if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
	if ((err = fb_alloc_cmap(&fb_display[con].cmap,
			      1<<fb_display[con].var.bits_per_pixel, 0)))
	    return err;
    }
    if (con == currcon)			/* current console? */
	return fb_set_cmap(cmap, &fb_display[con].var, kspc, vfb_setcolreg);
    else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


    /*
     *  Virtual Frame Buffer Specific ioctls
     */

static int virtual_fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			    u_long arg, int con)
{
    return -EINVAL;
}


__initfunc(void vfb_video_setup(char *options, int *ints))
{
    char *this_opt;

    fb_info.fontname[0] = '\0';

    if (!options || !*options)
	return;

    for (this_opt = strtok(options, ","); this_opt;
	 this_opt = strtok(NULL, ",")) {
	if (!strncmp(this_opt, "font:", 5))
	    strcpy(fb_info.fontname, this_opt+5);
    }
}


    /*
     *  Initialisation
     */

__initfunc(unsigned long virtual_fb_init(unsigned long mem_start))
{
    int err;

    if (mem_start) {
	videomemory = mem_start;
	mem_start += videomemorysize;
    } else
	videomemory = (u_long)vmalloc(videomemorysize);

    if (!videomemory)
	return mem_start;

    strcpy(fb_info.modename, virtual_fb_name);
    fb_info.changevar = NULL;
    fb_info.node = -1;
    fb_info.fbops = &virtual_fb_ops;
    fb_info.fbvar_num = NUM_TOTAL_MODES;
    fb_info.fbvar = virtual_fb_predefined;
    fb_info.disp = &disp;
    fb_info.switch_con = &vfbcon_switch;
    fb_info.updatevar = &vfbcon_updatevar;
    fb_info.blank = &vfbcon_blank;
    fb_info.setcmap = &vfbcon_setcmap;

    err = register_framebuffer(&fb_info);
    if (err < 0)
	return mem_start;

    virtual_fb_set_var(&virtual_fb_predefined[0], -1);

    printk("Virtual frame buffer device, using %ldK of video memory\n",
	   videomemorysize>>10);
    return mem_start;
}


static int vfbcon_switch(int con)
{
    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
		    vfb_getcolreg);

    currcon = con;
    /* Install new colormap */
    do_install_cmap(con);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int vfbcon_updatevar(int con)
{
    /* Nothing */
    return 0;
}

    /*
     *  Blank the display.
     */

static void vfbcon_blank(int blank)
{
    /* Nothing */
}

    /*
     *  Set the colormap
     */

static int vfbcon_setcmap(struct fb_cmap *cmap, int con)
{
    return(virtual_fb_set_cmap(cmap, 1, con));
}


static u_long get_line_length(int xres_virtual, int bpp)
{
    u_long length;
    
    length = (xres_virtual+bpp-1)/bpp;
    length = (length+31)&-32;
    length >>= 3;
    return(length);
}

static void vfb_encode_fix(struct fb_fix_screeninfo *fix,
			   struct fb_var_screeninfo *var)
{
    memset(fix, 0, sizeof(struct fb_fix_screeninfo));
    strcpy(fix->id, virtual_fb_name);
    fix->smem_start = (caddr_t)videomemory;
    fix->smem_len = videomemorysize;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;
    switch (var->bits_per_pixel) {
	case 1:
	    fix->visual = FB_VISUAL_MONO01;
	    break;
	case 8:
	    fix->visual = FB_VISUAL_PSEUDOCOLOR;
	    break;
	case 16:
	case 24:
	case 32:
	    fix->visual = FB_VISUAL_TRUECOLOR;
	    break;
    }
    fix->ywrapstep = 1;
    fix->xpanstep = 1;
    fix->ypanstep = 1;
    fix->line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
}

static void set_color_bitfields(struct fb_var_screeninfo *var)
{
    switch (var->bits_per_pixel) {
	case 1:
	case 8:
	    var->red.offset = 0;
	    var->red.length = 8;
	    var->green.offset = 0;
	    var->green.length = 8;
	    var->blue.offset = 0;
	    var->blue.length = 8;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
	case 16:	/* RGB 565 */
	    var->red.offset = 0;
	    var->red.length = 5;
	    var->green.offset = 5;
	    var->green.length = 6;
	    var->blue.offset = 11;
	    var->blue.length = 5;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
	case 24:	/* RGB 888 */
	    var->red.offset = 0;
	    var->red.length = 8;
	    var->green.offset = 8;
	    var->green.length = 8;
	    var->blue.offset = 16;
	    var->blue.length = 8;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
	case 32:	/* RGBA 8888 */
	    var->red.offset = 0;
	    var->red.length = 8;
	    var->green.offset = 8;
	    var->green.length = 8;
	    var->blue.offset = 16;
	    var->blue.length = 8;
	    var->transp.offset = 24;
	    var->transp.length = 8;
	    break;
    }
    var->red.msb_right = 0;
    var->green.msb_right = 0;
    var->blue.msb_right = 0;
    var->transp.msb_right = 0;
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int vfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp)
{
    if (regno > 255)
	return 1;
    *red = palette[regno].red;
    *green = palette[regno].green;
    *blue = palette[regno].blue;
    return 0;
}


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int vfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp)
{
    if (regno > 255)
	return 1;
    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;
    return 0;
}


static void do_install_cmap(int con)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
		    vfb_setcolreg);
    else
	fb_set_cmap(fb_default_cmap(fb_display[con].var.bits_per_pixel),
				    &fb_display[con].var, 1, vfb_setcolreg);
}


#ifdef MODULE
int init_module(void)
{
    return(virtual_fb_init(NULL));
}

void cleanup_module(void)
{
    unregister_framebuffer(&fb_info);
    vfree(videomemory);
}

#endif /* MODULE */
