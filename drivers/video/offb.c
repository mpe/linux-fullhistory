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
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#include <asm/io.h>
#include <asm/prom.h>

#include "fbcon-cfb8.h"


static int currcon = 0;

struct fb_info_offb {
    struct fb_info info;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    struct display disp;
    struct { u_char red, green, blue, pad; } palette[256];
    volatile unsigned char *cmap_adr;
    volatile unsigned char *cmap_data;
};

#ifdef __powerpc__
#define mach_eieio()	eieio()
#else
#define mach_eieio()	do {} while (0)
#endif

static int ofonly = 0;


    /*
     *  Interface used by the world
     */

unsigned long offb_init(unsigned long mem_start);
void offb_setup(char *options, int *ints);

static int offb_open(struct fb_info *info);
static int offb_release(struct fb_info *info);
static int offb_get_fix(struct fb_fix_screeninfo *fix, int con,
			struct fb_info *info);
static int offb_get_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int offb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int offb_pan_display(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int offb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int offb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int offb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			    u_long arg, int con, struct fb_info *info);

#ifdef CONFIG_FB_COMPAT_XPMAC
int console_getmode(struct vc_mode *);
int console_setmode(struct vc_mode *, int);
int console_setcmap(int, unsigned char *, unsigned char *, unsigned char *);
int console_powermode(int);
struct fb_info *console_fb_info = NULL;
int (*console_setmode_ptr)(struct vc_mode *, int) = NULL;
int (*console_set_cmap_ptr)(struct fb_cmap *, int, int, struct fb_info *)
    = NULL;
struct vc_mode display_info;
#endif /* CONFIG_FB_COMPAT_XPMAC */


    /*
     *  Interface to the low level console driver
     */

static int offbcon_switch(int con, struct fb_info *info);
static int offbcon_updatevar(int con, struct fb_info *info);
static void offbcon_blank(int blank, struct fb_info *info);


    /*
     *  Internal routines
     */

static int offb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			 u_int *transp, struct fb_info *info);
static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);


static struct fb_ops offb_ops = {
    offb_open, offb_release, offb_get_fix, offb_get_var, offb_set_var,
    offb_get_cmap, offb_set_cmap, offb_pan_display, offb_ioctl
};


    /*
     *  Open/Release the frame buffer device
     */

static int offb_open(struct fb_info *info)
{
    /*
     *  Nothing, only a usage count for the moment
     */

    MOD_INC_USE_COUNT;
    return(0);
}

static int offb_release(struct fb_info *info)
{
    MOD_DEC_USE_COUNT;
    return(0);
}


    /*
     *  Get the Fixed Part of the Display
     */

static int offb_get_fix(struct fb_fix_screeninfo *fix, int con,
			struct fb_info *info)
{
    struct fb_info_offb *info2 = (struct fb_info_offb *)info;

    memcpy(fix, &info2->fix, sizeof(struct fb_fix_screeninfo));
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int offb_get_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
    struct fb_info_offb *info2 = (struct fb_info_offb *)info;

    memcpy(var, &info2->var, sizeof(struct fb_var_screeninfo));
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int offb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
    struct display *display;
    int oldbpp = -1, err;
    int activate = var->activate;
    struct fb_info_offb *info2 = (struct fb_info_offb *)info;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = &info2->disp;	/* used during initialization */

    if (var->xres > info2->var.xres || var->yres > info2->var.yres ||
	var->xres_virtual > info2->var.xres_virtual ||
	var->yres_virtual > info2->var.yres_virtual ||
	var->bits_per_pixel > info2->var.bits_per_pixel ||
	var->nonstd ||
	(var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
	return -EINVAL;
    memcpy(var, &info2->var, sizeof(struct fb_var_screeninfo));

    if ((activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	oldbpp = display->var.bits_per_pixel;
	display->var = *var;
    }
    if (oldbpp != var->bits_per_pixel) {
	if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
	    return err;
	do_install_cmap(con, info);
    }
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int offb_pan_display(struct fb_var_screeninfo *var, int con,
			    struct fb_info *info)
{
    if (var->xoffset || var->yoffset)
	return -EINVAL;
    else
	return 0;
}

    /*
     *  Get the Colormap
     */

static int offb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, &fb_display[con].var, kspc, offb_getcolreg,
			   info);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else
	fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
		     cmap, kspc ? 0 : 2);
    return 0;
}

    /*
     *  Set the Colormap
     */

static int offb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
    struct fb_info_offb *info2 = (struct fb_info_offb *)info;
    int err;

    if (!info2->cmap_adr)
	return -ENOSYS;

    if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
	if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				 1<<fb_display[con].var.bits_per_pixel, 0)))
	    return err;
    }
    if (con == currcon)			/* current console? */
	return fb_set_cmap(cmap, &fb_display[con].var, kspc, offb_setcolreg,
			   info);
    else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


static int offb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		      u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}


#ifdef CONFIG_FB_ATY
extern unsigned long atyfb_of_init(unsigned long mem_start,
				   struct device_node *dp);

static const char *aty_names[] = {
    "ATY,mach64", "ATY,XCLAIM", "ATY,264VT", "ATY,mach64ii", "ATY,264GT-B", 
    "ATY,mach64_3D_pcc", "ATY,XCLAIM3D", "ATY,XCLAIMVR", "ATY,RAGEII_M",
    "ATY,XCLAIMVRPro", "ATY,mach64_3DU", "ATY,XCLAIM3DPro"
};
#endif /* CONFIG_FB_ATY */
#ifdef CONFIG_FB_S3TRIO
extern void s3triofb_init_of(unsigned long mem_start, struct device_node *dp);
#endif /* CONFIG_FB_S3TRIO */


    /*
     *  Initialisation
     */

__initfunc(unsigned long offb_init(unsigned long mem_start))
{
    struct device_node *dp;
    int dpy, i, err, *pp, len;
    unsigned *up, address;
    struct fb_fix_screeninfo *fix;
    struct fb_var_screeninfo *var;
    struct display *disp;
    struct fb_info_offb *info;

    for (dpy = 0; dpy < prom_num_displays; dpy++) {
	if (!(dp = find_path_device(prom_display_paths[dpy])))
	    continue;

	if (!ofonly) {
#ifdef CONFIG_FB_ATY
	    for (i = 0; i < sizeof(aty_names)/sizeof(*aty_names); i++)
		if (!strcmp(dp->name, aty_names[i]))
		    break;
	    if (i < sizeof(aty_names)/sizeof(*aty_names)) {
		mem_start = atyfb_of_init(mem_start, dp);
		continue;
	    }
#endif /* CONFIG_FB_ATY */
#ifdef CONFIG_FB_S3TRIO
            if (s3triofb_init_of(dp))
                continue;
#endif /* CONFIG_FB_S3TRIO */
	}

	info = kmalloc(sizeof(struct fb_info_offb), GFP_ATOMIC);
	fix = &info->fix;
	var = &info->var;
	disp = &info->disp;

	strcpy(fix->id, "OFfb ");
	strncat(fix->id, dp->name, sizeof(fix->id));
	fix->id[sizeof(fix->id)-1] = '\0';

	if ((pp = (int *)get_property(dp, "depth", &len)) != NULL
	    && len == sizeof(int) && *pp != 8) {
	    printk("%s: can't use depth = %d\n", dp->full_name, *pp);
	    kfree(info);
	    continue;
	}
	if ((pp = (int *)get_property(dp, "width", &len)) != NULL
	    && len == sizeof(int))
	    var->xres = var->xres_virtual = *pp;
	if ((pp = (int *)get_property(dp, "height", &len)) != NULL
	    && len == sizeof(int))
	    var->yres = var->yres_virtual = *pp;
	if ((pp = (int *)get_property(dp, "linebytes", &len)) != NULL
	    && len == sizeof(int))
	    fix->line_length = *pp;
	else
	    fix->line_length = var->xres_virtual;
	fix->smem_len = fix->line_length*var->yres;
	if ((up = (unsigned *)get_property(dp, "address", &len)) != NULL
	    && len == sizeof(unsigned))
	    address = (u_long)*up;
	else {
	    for (i = 0; i < dp->n_addrs; ++i)
		if (dp->addrs[i].size >= len)
		    break;
	    if (i >= dp->n_addrs) {
		printk("no framebuffer address found for %s\n", dp->full_name);
		kfree(info);
		continue;
	    }
	    address = (u_long)dp->addrs[i].address;
	}
	fix->smem_start = (char *)address;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;

	/* XXX kludge for ati */
	if (strncmp(dp->name, "ATY,", 4) == 0) {
	    info->cmap_adr = ioremap(address + 0x7ff000, 0x1000) + 0xcc0;
	    info->cmap_data = info->cmap_adr + 1;
	}

	fix->visual = info->cmap_adr ? FB_VISUAL_PSEUDOCOLOR :
				       FB_VISUAL_STATIC_PSEUDOCOLOR;

	var->xoffset = var->yoffset = 0;
	var->bits_per_pixel = 8;
	var->grayscale = 0;
	var->red.offset = var->green.offset = var->blue.offset = 0;
	var->red.length = var->green.length = var->blue.length = 8;
	var->red.msb_right = var->green.msb_right = var->blue.msb_right = 0;
	var->transp.offset = var->transp.length = var->transp.msb_right = 0;
	var->nonstd = 0;
	var->activate = 0;
	var->height = var->width = -1;
	var->pixclock = 10000;
	var->left_margin = var->right_margin = 16;
	var->upper_margin = var->lower_margin = 16;
	var->hsync_len = var->vsync_len = 8;
	var->sync = 0;
	var->vmode = FB_VMODE_NONINTERLACED;

	disp->var = *var;
	disp->cmap.start = 0;
	disp->cmap.len = 0;
	disp->cmap.red = NULL;
	disp->cmap.green = NULL;
	disp->cmap.blue = NULL;
	disp->cmap.transp = NULL;
	disp->screen_base = ioremap(address, fix->smem_len);
	disp->visual = fix->visual;
	disp->type = fix->type;
	disp->type_aux = fix->type_aux;
	disp->ypanstep = 0;
	disp->ywrapstep = 0;
	disp->line_length = fix->line_length;
	disp->can_soft_blank = info->cmap_adr ? 1 : 0;
	disp->inverse = 0;
#ifdef CONFIG_FBCON_CFB8
	disp->dispsw = &fbcon_cfb8;
#else
	disp->dispsw = NULL;
#endif

	strcpy(info->info.modename, "OFfb ");
	strncat(info->info.modename, dp->full_name,
		sizeof(info->info.modename));
	info->info.node = -1;
	info->info.fbops = &offb_ops;
	info->info.disp = disp;
	info->info.fontname[0] = '\0';
	info->info.changevar = NULL;
	info->info.switch_con = &offbcon_switch;
	info->info.updatevar = &offbcon_updatevar;
	info->info.blank = &offbcon_blank;

	err = register_framebuffer(&info->info);
	if (err < 0) {
	    kfree(info);
	    return mem_start;
	}

	for (i = 0; i < 16; i++) {
	    int j = color_table[i];
	    info->palette[i].red = default_red[j];
	    info->palette[i].green = default_grn[j];
	    info->palette[i].blue = default_blu[j];
	}
	offb_set_var(var, -1, &info->info);

	printk("fb%d: Open Firmware frame buffer device on %s\n",
	       GET_FB_IDX(info->info.node), dp->full_name);

#ifdef CONFIG_FB_COMPAT_XPMAC
	if (!console_fb_info) {
	    display_info.height = var->yres;
	    display_info.width = var->xres;
	    display_info.depth = 8;
	    display_info.pitch = fix->line_length;
	    display_info.mode = 0;
	    strncpy(display_info.name, dp->name, sizeof(display_info.name));
	    display_info.fb_address = address;
	    display_info.cmap_adr_address = 0;
	    display_info.cmap_data_address = 0;
	    display_info.disp_reg_address = 0;
	    /* XXX kludge for ati */
	    if (strncmp(dp->name, "ATY,", 4) == 0) {
		    display_info.disp_reg_address = address + 0x7ffc00;
		    display_info.cmap_adr_address = address + 0x7ffcc0;
		    display_info.cmap_data_address = address + 0x7ffcc1;
	    }
	    console_fb_info = &info->info;
	    console_set_cmap_ptr = offb_set_cmap;
	}
#endif /* CONFIG_FB_COMPAT_XPMAC) */
    }
    return mem_start;
}


    /*
     *  Setup: parse used options
     */

void offb_setup(char *options, int *ints)
{
    if (!options || !*options)
	return;

    if (!strcmp(options, "ofonly"))
	ofonly = 1;
}


static int offbcon_switch(int con, struct fb_info *info)
{
    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
		    offb_getcolreg, info);

    currcon = con;
    /* Install new colormap */
    do_install_cmap(con, info);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int offbcon_updatevar(int con, struct fb_info *info)
{
    /* Nothing */
    return 0;
}

    /*
     *  Blank the display.
     */

static void offbcon_blank(int blank, struct fb_info *info)
{
    struct fb_info_offb *info2 = (struct fb_info_offb *)info;
    int i, j;

    if (!info2->cmap_adr)
	return;

    if (blank)
	for (i = 0; i < 256; i++) {
	    *info2->cmap_adr = i;
	    mach_eieio();
	    for (j = 0; j < 3; j++) {
		*info2->cmap_data = 0;
		mach_eieio();
	    }
	}
    else
	do_install_cmap(currcon, info);
}

    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int offb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			  u_int *transp, struct fb_info *info)
{
    struct fb_info_offb *info2 = (struct fb_info_offb *)info;

    if (!info2->cmap_adr || regno > 255)
	return 1;
    *red = info2->palette[regno].red;
    *green = info2->palette[regno].green;
    *blue = info2->palette[regno].blue;
    return 0;
}


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
    struct fb_info_offb *info2 = (struct fb_info_offb *)info;

    if (!info2->cmap_adr || regno > 255)
	return 1;
    info2->palette[regno].red = red;
    info2->palette[regno].green = green;
    info2->palette[regno].blue = blue;
    *info2->cmap_adr = regno;
    mach_eieio();
    *info2->cmap_data = red;
    mach_eieio();
    *info2->cmap_data = green;
    mach_eieio();
    *info2->cmap_data = blue;
    mach_eieio();
    return 0;
}


static void do_install_cmap(int con, struct fb_info *info)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
		    offb_setcolreg, info);
    else
	fb_set_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
				    &fb_display[con].var, 1, offb_setcolreg,
				    info);
}


#ifdef CONFIG_FB_COMPAT_XPMAC

    /*
     *  Backward compatibility mode for Xpmac
     *
     *  To do:
     *
     *    - console_setmode() should fill in a struct fb_var_screeninfo (using
     *	    the MacOS video mode database) and simply call a decode_var()
     *	    function, so console_setmode_ptr is no longer needed.
     *	    console_getmode() should convert in the other direction.
     *
     *    - instead of using the console_* stuff (filled in by the frame
     *      buffer), we should use the correct struct fb_info for the
     *	    foreground virtual console.
     */

int console_getmode(struct vc_mode *mode)
{
    *mode = display_info;
    return 0;
}

int console_setmode(struct vc_mode *mode, int doit)
{
    int err;

    if (console_setmode_ptr == NULL)
	return -EINVAL;

    err = (*console_setmode_ptr)(mode, doit);
    return err;
}

static u16 palette_red[16];
static u16 palette_green[16];                                                 
static u16 palette_blue[16];

static struct fb_cmap palette_cmap = {
    0, 16, palette_red, palette_green, palette_blue, NULL
};

int console_setcmap(int n_entries, unsigned char *red, unsigned char *green,
		    unsigned char *blue)
{
    int i, j, n;

    if (console_set_cmap_ptr == NULL)
	return -EOPNOTSUPP;
    for (i = 0; i < n_entries; i += n) {
	n = n_entries-i;
	if (n > 16)
	    n = 16;
	palette_cmap.start = i;
	palette_cmap.len = n;
	for (j = 0; j < n; j++) {
	    palette_cmap.red[j]   = (red[i+j] << 8) | red[i+j];
	    palette_cmap.green[j] = (green[i+j] << 8) | green[i+j];
	    palette_cmap.blue[j]  = (blue[i+j] << 8) | blue[i+j];
	}
	(*console_set_cmap_ptr)(&palette_cmap, 1, fg_console, console_fb_info);
    }
    return 0;
}

int console_powermode(int mode)
{
    if (mode == VC_POWERMODE_INQUIRY)
	return 0;
    if (mode < VESA_NO_BLANKING || mode > VESA_POWERDOWN)
	return -EINVAL;
    /* Not supported */
    return -ENXIO;
}

#endif /* CONFIG_FB_COMPAT_XPMAC */
