/*
 *  linux/drivers/video/offb.c -- Open Firmware based frame buffer device
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This driver is partly based on the PowerMac console driver:
 *
 *	Copyright (C) 1996 Paul Mackerras
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
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/prom.h>


#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

static int currcon = 0;
static struct display disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[256];
static char offb_name[16] = "OFfb ";

static volatile unsigned char *unknown_cmap_adr = NULL;
static volatile unsigned char *unknown_cmap_data = NULL;

static struct fb_fix_screeninfo fb_fix = { 0, };
static struct fb_var_screeninfo fb_var = { 0, };


    /*
     *  Interface used by the world
     */

void offb_video_setup(char *options, int *ints);

static int offb_open(int fbidx);
static int offb_release(int fbidx);
static int offb_get_fix(struct fb_fix_screeninfo *fix, int con);
static int offb_get_var(struct fb_var_screeninfo *var, int con);
static int offb_set_var(struct fb_var_screeninfo *var, int con);
static int offb_pan_display(struct fb_var_screeninfo *var, int con);
static int offb_get_cmap(struct fb_cmap *cmap, int kspc, int con);
static int offb_set_cmap(struct fb_cmap *cmap, int kspc, int con);
static int offb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			    u_long arg, int con);


    /*
     *  Interface to the low level console driver
     */

unsigned long offb_init(unsigned long mem_start);
static int offbcon_switch(int con);
static int offbcon_updatevar(int con);
static void offbcon_blank(int blank);
static int offbcon_setcmap(struct fb_cmap *cmap, int con);


    /*
     *  Internal routines
     */

static int offb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp);
static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp);
static void do_install_cmap(int con);


static struct fb_ops offb_ops = {
    offb_open, offb_release, offb_get_fix, offb_get_var, offb_set_var,
    offb_get_cmap, offb_set_cmap, offb_pan_display, offb_ioctl
};


    /*
     *  Open/Release the frame buffer device
     */

static int offb_open(int fbidx)                                       
{
    /*                                                                     
     *  Nothing, only a usage count for the moment                          
     */                                                                    

    MOD_INC_USE_COUNT;
    return(0);                              
}
        
static int offb_release(int fbidx)
{
    MOD_DEC_USE_COUNT;
    return(0);                                                    
}


    /*
     *  Get the Fixed Part of the Display
     */

static int offb_get_fix(struct fb_fix_screeninfo *fix, int con)
{
    memcpy(fix, &fb_fix, sizeof(fb_fix));
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int offb_get_var(struct fb_var_screeninfo *var, int con)
{
    memcpy(var, &fb_var, sizeof(fb_var));
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int offb_set_var(struct fb_var_screeninfo *var, int con)
{
    struct display *display;
    int oldbpp = -1, err;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = &disp;	/* used during initialization */

    if (var->xres > fb_var.xres || var->yres > fb_var.yres ||
	var->xres_virtual > fb_var.xres_virtual ||
	var->yres_virtual > fb_var.yres_virtual ||
	var->bits_per_pixel > fb_var.bits_per_pixel ||
	var->nonstd ||
	(var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
	return -EINVAL;
    memcpy(var, &fb_var, sizeof(fb_var));

    if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	oldbpp = display->var.bits_per_pixel;
	display->var = *var;
    }
    if (oldbpp != var->bits_per_pixel) {
	if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
	    return err;
	do_install_cmap(con);
    }
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int offb_pan_display(struct fb_var_screeninfo *var, int con)
{
    if (var->xoffset || var->yoffset)
	return -EINVAL;
    else
	return 0;
}

    /*
     *  Get the Colormap
     */

static int offb_get_cmap(struct fb_cmap *cmap, int kspc, int con)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, &fb_display[con].var, kspc, offb_getcolreg);
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

static int offb_set_cmap(struct fb_cmap *cmap, int kspc, int con)
{
    int err;

    if (!unknown_cmap_adr)
	return -ENOSYS;

    if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
	if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				 1<<fb_display[con].var.bits_per_pixel, 0)))
	    return err;
    }
    if (con == currcon)			/* current console? */
	return fb_set_cmap(cmap, &fb_display[con].var, kspc, offb_setcolreg);
    else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


static int offb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		      u_long arg, int con)
{
    return -EINVAL;
}


    /*
     *  Initialisation
     */

__initfunc(unsigned long offb_init(unsigned long mem_start))
{
    struct device_node *dp;
    int i, err, *pp, len;
    unsigned *up, address;

    if (!prom_display_path[0])
	return mem_start;
    if (!(dp = find_path_device(prom_display_path)))
	return mem_start;

    strncat(offb_name, dp->name, sizeof(offb_name));
    offb_name[sizeof(offb_name)-1] = '\0';
    strcpy(fb_fix.id, offb_name);

    if ((pp = (int *)get_property(dp, "depth", &len)) != NULL
	&& len == sizeof(int) && *pp != 8) {
	printk("%s: can't use depth = %d\n", dp->full_name, *pp);
	return mem_start;
    }
    if ((pp = (int *)get_property(dp, "width", &len)) != NULL
	&& len == sizeof(int))
	fb_var.xres = fb_var.xres_virtual = *pp;
    if ((pp = (int *)get_property(dp, "height", &len)) != NULL
	&& len == sizeof(int))
	fb_var.yres = fb_var.yres_virtual = *pp;
    if ((pp = (int *)get_property(dp, "linebytes", &len)) != NULL
	&& len == sizeof(int))
	fb_fix.line_length = *pp;
    else
	fb_fix.line_length = fb_var.xres_virtual;
    fb_fix.smem_len = fb_fix.line_length*fb_var.yres;
    if ((up = (unsigned *)get_property(dp, "address", &len)) != NULL
	&& len == sizeof(unsigned))
	address = (u_long)*up;
    else {
	for (i = 0; i < dp->n_addrs; ++i)
	    if (dp->addrs[i].size >= len)
		break;
	if (i >= dp->n_addrs) {
	    printk("no framebuffer address found for %s\n", dp->full_name);
	    return mem_start;
	}
	address = (u_long)dp->addrs[i].address;
    }
    fb_fix.smem_start = ioremap(address, fb_fix.smem_len);
    fb_fix.type = FB_TYPE_PACKED_PIXELS;
    fb_fix.type_aux = 0;

    /* XXX kludge for ati */
    if (strncmp(dp->name, "ATY,", 4) == 0) {
	unknown_cmap_adr = ioremap(address + 0x7ff000, 0x1000) + 0xcc0;
	unknown_cmap_data = unknown_cmap_adr + 1;
    }

    fb_fix.visual = unknown_cmap_adr ? FB_VISUAL_PSEUDOCOLOR :
				       FB_VISUAL_STATIC_PSEUDOCOLOR;

    fb_var.xoffset = fb_var.yoffset = 0;
    fb_var.bits_per_pixel = 8;
    fb_var.grayscale = 0;
    fb_var.red.offset = fb_var.green.offset = fb_var.blue.offset = 0;
    fb_var.red.length = fb_var.green.length = fb_var.blue.length = 8;
    fb_var.red.msb_right = fb_var.green.msb_right = fb_var.blue.msb_right = 0;
    fb_var.transp.offset = fb_var.transp.length = fb_var.transp.msb_right = 0;
    fb_var.nonstd = 0;
    fb_var.activate = 0;
    fb_var.height = fb_var.width = -1;
    fb_var.accel = FB_ACCEL_NONE;
    fb_var.pixclock = 10000;
    fb_var.left_margin = fb_var.right_margin = 16;
    fb_var.upper_margin = fb_var.lower_margin = 16;
    fb_var.hsync_len = fb_var.vsync_len = 8;
    fb_var.sync = 0;
    fb_var.vmode = FB_VMODE_NONINTERLACED;

    disp.var = fb_var;
    disp.cmap.start = 0;
    disp.cmap.len = 0;
    disp.cmap.red = disp.cmap.green = disp.cmap.blue = disp.cmap.transp = NULL;
    disp.screen_base = fb_fix.smem_start;
    disp.visual = fb_fix.visual;
    disp.type = fb_fix.type;
    disp.type_aux = fb_fix.type_aux;
    disp.ypanstep = 0;
    disp.ywrapstep = 0;
    disp.line_length = fb_fix.line_length;
    disp.can_soft_blank = 1;
    disp.inverse = 0;

    strcpy(fb_info.modename, "OFfb ");
    strncat(fb_info.modename, dp->full_name, sizeof(fb_info.modename));
    fb_info.node = -1;
    fb_info.fbops = &offb_ops;
    fb_info.fbvar_num = 1;
    fb_info.fbvar = &fb_var;
    fb_info.disp = &disp;
    fb_info.fontname[0] = '\0';
    fb_info.changevar = NULL;
    fb_info.switch_con = &offbcon_switch;
    fb_info.updatevar = &offbcon_updatevar;
    fb_info.blank = &offbcon_blank;
    fb_info.setcmap = &offbcon_setcmap;

    err = register_framebuffer(&fb_info);
    if (err < 0)
	return mem_start;

    offb_set_var(&fb_var, -1);

    printk("Open Firmware frame buffer device on %s\n", dp->full_name);
    return mem_start;
}


static int offbcon_switch(int con)
{
    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
		    offb_getcolreg);

    currcon = con;
    /* Install new colormap */
    do_install_cmap(con);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int offbcon_updatevar(int con)
{
    /* Nothing */
    return 0;
}

    /*
     *  Blank the display.
     */

static void offbcon_blank(int blank)
{
    /* Nothing */
}

    /*
     *  Set the colormap
     */

static int offbcon_setcmap(struct fb_cmap *cmap, int con)
{
    return(offb_set_cmap(cmap, 1, con));
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int offb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp)
{
    if (!unknown_cmap_adr || regno > 255)
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

static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp)
{
    if (!unknown_cmap_adr || regno > 255)
	return 1;
    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;
    *unknown_cmap_adr = regno;
#ifdef __powerpc__
    eieio();
#endif
    *unknown_cmap_data = red;
#ifdef __powerpc__
    eieio();
#endif
    *unknown_cmap_data = green;
#ifdef __powerpc__
    eieio();
#endif
    *unknown_cmap_data = blue;
#ifdef __powerpc__
    eieio();
#endif
    return 0;
}


static void do_install_cmap(int con)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
		    offb_setcolreg);
    else
	fb_set_cmap(fb_default_cmap(fb_display[con].var.bits_per_pixel),
				    &fb_display[con].var, 1, offb_setcolreg);
}
