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
#include <asm/bootx.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>
#include <video/macmodes.h>


static int currcon = 0;

struct fb_info_offb {
    struct fb_info info;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    struct display disp;
    struct { u_char red, green, blue, pad; } palette[256];
    volatile unsigned char *cmap_adr;
    volatile unsigned char *cmap_data;
    union {
#ifdef FBCON_HAS_CFB16
	u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
	u32 cfb32[16];
#endif
    } fbcon_cmap;
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

void offb_init(void);
void offb_setup(char *options, int *ints);

static int offb_open(struct fb_info *info, int user);
static int offb_release(struct fb_info *info, int user);
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
struct vc_mode display_info;
#endif /* CONFIG_FB_COMPAT_XPMAC */

extern boot_infos_t *boot_infos;

static int offb_init_driver(struct device_node *);
static void offb_init_nodriver(struct device_node *);
static void offb_init_fb(const char *name, const char *full_name, int width,
		      int height, int depth, int pitch, unsigned long address);

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

static int offb_open(struct fb_info *info, int user)
{
    /*
     *  Nothing, only a usage count for the moment
     */

    MOD_INC_USE_COUNT;
    return(0);
}

static int offb_release(struct fb_info *info, int user)
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
    unsigned int oldbpp = 0;
    int err;
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
    if ((oldbpp != var->bits_per_pixel) || (display->cmap.len == 0)) {
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
	return fb_get_cmap(cmap, kspc, offb_getcolreg, info);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else
    {
	int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
	fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
    }
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
	int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
	if ((err = fb_alloc_cmap(&fb_display[con].cmap, size, 0)))
	    return err;
    }
    if (con == currcon)			/* current console? */
	return fb_set_cmap(cmap, kspc, offb_setcolreg, info);
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
extern void atyfb_of_init(struct device_node *dp);
#endif /* CONFIG_FB_ATY */
#ifdef CONFIG_FB_S3TRIO
extern void s3triofb_init_of(struct device_node *dp);
#endif /* CONFIG_FB_S3TRIO */
#ifdef CONFIG_FB_IMSTT
extern void imsttfb_of_init(struct device_node *dp);
#endif
#ifdef CONFIG_FB_CT65550
extern void chips_of_init(struct device_node *dp);
#endif /* CONFIG_FB_CT65550 */
#ifdef CONFIG_FB_MATROX
extern int matrox_of_init(struct device_node *dp);
#endif /* CONFIG_FB_MATROX */
#ifdef CONFIG_FB_CONTROL
extern void control_of_init(struct device_node *dp);
#endif /* CONFIG_FB_CONTROL */
#ifdef CONFIG_FB_VALKYRIE
extern void valkyrie_of_init(struct device_node *dp);
#endif /* CONFIG_FB_VALKYRIE */
#ifdef CONFIG_FB_PLATINUM
extern void platinum_of_init(struct device_node *dp);
#endif /* CONFIG_FB_PLATINUM */


    /*
     *  Initialisation
     */

__initfunc(void offb_init(void))
{
    struct device_node *dp;
    unsigned int dpy;
    struct device_node *displays = find_type_devices("display");
    struct device_node *macos_display = NULL;

    /* If we're booted from BootX... */
    if (prom_num_displays == 0 && boot_infos != 0) {
	unsigned long addr = (unsigned long) boot_infos->dispDeviceBase;
	/* find the device node corresponding to the macos display */
	for (dp = displays; dp != NULL; dp = dp->next) {
	    int i;
	    /*
	     * Grrr...  It looks like the MacOS ATI driver
	     * munges the assigned-addresses property (but
	     * the AAPL,address value is OK).
	     */
	    if (strncmp(dp->name, "ATY,", 4) == 0 && dp->n_addrs == 1) {
		unsigned int *ap = (unsigned int *)
		    get_property(dp, "AAPL,address", NULL);
		if (ap != NULL) {
		    dp->addrs[0].address = *ap;
		    dp->addrs[0].size = 0x01000000;
		}
	    }
	    /*
	     * See if the display address is in one of the address
	     * ranges for this display.
	     */
	    for (i = 0; i < dp->n_addrs; ++i) {
		if (dp->addrs[i].address <= addr
		    && addr < dp->addrs[i].address + dp->addrs[i].size)
		    break;
	    }
	    if (i < dp->n_addrs) {
		printk(KERN_INFO "MacOS display is %s\n", dp->full_name);
		macos_display = dp;
		break;
	    }
	}

	/* initialize it */
	if (ofonly || macos_display == NULL 
	    || !offb_init_driver(macos_display)) {
	    offb_init_fb(macos_display? macos_display->name: "MacOS display",
			 macos_display? macos_display->full_name: "MacOS display",
			 boot_infos->dispDeviceRect[2],
			 boot_infos->dispDeviceRect[3],
			 boot_infos->dispDeviceDepth,
			 boot_infos->dispDeviceRowBytes, addr);
	}
    }

    for (dpy = 0; dpy < prom_num_displays; dpy++) {
	if ((dp = find_path_device(prom_display_paths[dpy])))
	    if (ofonly || !offb_init_driver(dp))
		offb_init_nodriver(dp);
    }

    if (!ofonly) {
	for (dp = find_type_devices("display"); dp != NULL; dp = dp->next) {
	    for (dpy = 0; dpy < prom_num_displays; dpy++)
		if (strcmp(dp->full_name, prom_display_paths[dpy]) == 0)
		    break;
	    if (dpy >= prom_num_displays && dp != macos_display)
		offb_init_driver(dp);
	}
    }
}

__initfunc(static int offb_init_driver(struct device_node *dp))
{
#ifdef CONFIG_FB_ATY
    if (!strncmp(dp->name, "ATY", 3)) {
	atyfb_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_ATY */
#ifdef CONFIG_FB_S3TRIO
    if (!strncmp(dp->name, "S3Trio", 6)) {
    	s3triofb_init_of(dp);
	return 1;
    }
#endif /* CONFIG_FB_S3TRIO */
#ifdef CONFIG_FB_IMSTT
    if (!strncmp(dp->name, "IMS,tt", 6)) {
	imsttfb_of_init(dp);
	return 1;
    }
#endif
#ifdef CONFIG_FB_CT65550
    if (!strcmp(dp->name, "chips65550")) {
	chips_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_CT65550 */
#ifdef CONFIG_FB_MATROX
    if (!strncmp(dp->name, "MTRX", 4)) {
	matrox_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_MATROX */
#ifdef CONFIG_FB_CONTROL
    if(!strcmp(dp->name, "control")) {
	control_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_CONTROL */
#ifdef CONFIG_FB_VALKYRIE
    if(!strcmp(dp->name, "valkyrie")) {
	valkyrie_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_VALKYRIE */
#ifdef CONFIG_FB_PLATINUM
    if (!strncmp(dp->name, "platinum",8)) {
	platinum_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_PLATINUM */
    return 0;
}

__initfunc(static void offb_init_nodriver(struct device_node *dp))
{
    int *pp, i;
    unsigned int len;
    int width = 640, height = 480, depth = 8, pitch;
    unsigned *up, address;

    if ((pp = (int *)get_property(dp, "depth", &len)) != NULL
	&& len == sizeof(int))
	depth = *pp;
    if ((pp = (int *)get_property(dp, "width", &len)) != NULL
	&& len == sizeof(int))
	width = *pp;
    if ((pp = (int *)get_property(dp, "height", &len)) != NULL
	&& len == sizeof(int))
	height = *pp;
    if ((pp = (int *)get_property(dp, "linebytes", &len)) != NULL
	&& len == sizeof(int))
	pitch = *pp;
    else
	pitch = width;
    if ((up = (unsigned *)get_property(dp, "address", &len)) != NULL
	&& len == sizeof(unsigned))
	address = (u_long)*up;
    else {
	for (i = 0; i < dp->n_addrs; ++i)
	    if (dp->addrs[i].size >= len)
		break;
	if (i >= dp->n_addrs) {
	    printk("no framebuffer address found for %s\n", dp->full_name);
	    return;
	}
	address = (u_long)dp->addrs[i].address;

	/* kludge for valkyrie */
	if (strcmp(dp->name, "valkyrie") == 0) 
	    address += 0x1000;
    }
    offb_init_fb(dp->name, dp->full_name, width, height, depth,
		 pitch, address);
}

__initfunc(static void offb_init_fb(const char *name, const char *full_name,
				    int width, int height, int depth,
				    int pitch, unsigned long address))
{
    int i;
    struct fb_fix_screeninfo *fix;
    struct fb_var_screeninfo *var;
    struct display *disp;
    struct fb_info_offb *info;

    printk(KERN_INFO "Using unsupported %dx%d %s at %lx, depth=%d, pitch=%d\n",
	   width, height, name, address, depth, pitch);
    if (depth != 8 && depth != 16 && depth != 32) {
	printk("%s: can't use depth = %d\n", full_name, depth);
	return;
    }

    info = kmalloc(sizeof(struct fb_info_offb), GFP_ATOMIC);
    if (info == 0)
	return;
    memset(info, 0, sizeof(*info));

    fix = &info->fix;
    var = &info->var;
    disp = &info->disp;

    strcpy(fix->id, "OFfb ");
    strncat(fix->id, name, sizeof(fix->id));
    fix->id[sizeof(fix->id)-1] = '\0';

    var->xres = var->xres_virtual = width;
    var->yres = var->yres_virtual = height;
    fix->line_length = pitch;

    fix->smem_start = (char *)address;
    fix->smem_len = pitch * height;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;

    if (depth == 8)
    {
    	/* XXX kludge for ati */
    	if (strncmp(name, "ATY,", 4) == 0) {
		unsigned long base = address & 0xff000000UL;
		info->cmap_adr = ioremap(base + 0x7ff000, 0x1000) + 0xcc0;
		info->cmap_data = info->cmap_adr + 1;
	}
        fix->visual = info->cmap_adr ? FB_VISUAL_PSEUDOCOLOR
				     : FB_VISUAL_STATIC_PSEUDOCOLOR;
    }
    else
	fix->visual = /*info->cmap_adr ? FB_VISUAL_DIRECTCOLOR
				     : */FB_VISUAL_TRUECOLOR;

    var->xoffset = var->yoffset = 0;
    var->bits_per_pixel = depth;
    switch (depth) {
	case 8:
	    var->bits_per_pixel = 8;
	    var->red.offset = 0;
	    var->red.length = 8;
	    var->green.offset = 0;
	    var->green.length = 8;
	    var->blue.offset = 0;
	    var->blue.length = 8;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
	case 16:	/* RGB 555 */
	    var->bits_per_pixel = 16;
	    var->red.offset = 10;
	    var->red.length = 5;
	    var->green.offset = 5;
	    var->green.length = 5;
	    var->blue.offset = 0;
	    var->blue.length = 5;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
	case 32:	/* RGB 888 */
	    var->bits_per_pixel = 32;
	    var->red.offset = 16;
	    var->red.length = 8;
	    var->green.offset = 8;
	    var->green.length = 8;
	    var->blue.offset = 0;
	    var->blue.length = 8;
	    var->transp.offset = 24;
	    var->transp.length = 8;
	    break;
    }
    var->red.msb_right = var->green.msb_right = var->blue.msb_right = var->transp.msb_right = 0;
    var->grayscale = 0;
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
    switch (depth) {
#ifdef FBCON_HAS_CFB8
        case 8:
            disp->dispsw = &fbcon_cfb8;
            break;
#endif
#ifdef FBCON_HAS_CFB16
        case 16:
            disp->dispsw = &fbcon_cfb16;
            disp->dispsw_data = info->fbcon_cmap.cfb16;
            for (i = 0; i < 16; i++)
            	if (fix->visual == FB_VISUAL_TRUECOLOR)
		    info->fbcon_cmap.cfb16[i] =
			    (((default_blu[i] >> 3) & 0x1f) << 10) |
			    (((default_grn[i] >> 3) & 0x1f) << 5) |
			    ((default_red[i] >> 3) & 0x1f);
		else
		    info->fbcon_cmap.cfb16[i] =
			    (i << 10) | (i << 5) | i;
            break;
#endif
#ifdef FBCON_HAS_CFB32
        case 32:
            disp->dispsw = &fbcon_cfb32;
            disp->dispsw_data = info->fbcon_cmap.cfb32;
            for (i = 0; i < 16; i++)
            	if (fix->visual == FB_VISUAL_TRUECOLOR)
		    info->fbcon_cmap.cfb32[i] =
			(default_blu[i] << 16) |
			(default_grn[i] << 8) |
			default_red[i];
		else
		    info->fbcon_cmap.cfb32[i] =
			    (i << 16) | (i << 8) | i;
            break;
#endif
        default:
            disp->dispsw = &fbcon_dummy;
    }

    disp->scrollmode = SCROLL_YREDRAW;

    strcpy(info->info.modename, "OFfb ");
    strncat(info->info.modename, full_name, sizeof(info->info.modename));
    info->info.node = -1;
    info->info.fbops = &offb_ops;
    info->info.disp = disp;
    info->info.fontname[0] = '\0';
    info->info.changevar = NULL;
    info->info.switch_con = &offbcon_switch;
    info->info.updatevar = &offbcon_updatevar;
    info->info.blank = &offbcon_blank;
    info->info.flags = FBINFO_FLAG_DEFAULT;

    for (i = 0; i < 16; i++) {
	int j = color_table[i];
	info->palette[i].red = default_red[j];
	info->palette[i].green = default_grn[j];
	info->palette[i].blue = default_blu[j];
    }
    offb_set_var(var, -1, &info->info);

    if (register_framebuffer(&info->info) < 0) {
	kfree(info);
	return;
    }

    printk("fb%d: Open Firmware frame buffer device on %s\n",
	   GET_FB_IDX(info->info.node), full_name);

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (!console_fb_info) {
	display_info.height = var->yres;
	display_info.width = var->xres;
	display_info.depth = depth;
	display_info.pitch = fix->line_length;
	display_info.mode = 0;
	strncpy(display_info.name, name, sizeof(display_info.name));
	display_info.fb_address = address;
	display_info.cmap_adr_address = 0;
	display_info.cmap_data_address = 0;
	display_info.disp_reg_address = 0;
	/* XXX kludge for ati */
	if (strncmp(name, "ATY,", 4) == 0) {
	    unsigned long base = address & 0xff000000UL;
	    display_info.disp_reg_address = base + 0x7ffc00;
	    display_info.cmap_adr_address = base + 0x7ffcc0;
	    display_info.cmap_data_address = base + 0x7ffcc1;
	}
	console_fb_info = &info->info;
    }
#endif /* CONFIG_FB_COMPAT_XPMAC) */
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
	fb_get_cmap(&fb_display[currcon].cmap, 1, offb_getcolreg, info);

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
    
    *red = (info2->palette[regno].red<<8) | info2->palette[regno].red;
    *green = (info2->palette[regno].green<<8) | info2->palette[regno].green;
    *blue = (info2->palette[regno].blue<<8) | info2->palette[regno].blue;
    *transp = 0;
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
    int i;
	
    if (!info2->cmap_adr || regno > 255)
	return 1;

    red >>= 8;
    green >>= 8;
    blue >>= 8;

    info2->palette[regno].red = red;
    info2->palette[regno].green = green;
    info2->palette[regno].blue = blue;

    *info2->cmap_adr = regno;/* On some chipsets, add << 3 in 15 bits */
    mach_eieio();
    *info2->cmap_data = red;
    mach_eieio();
    *info2->cmap_data = green;
    mach_eieio();
    *info2->cmap_data = blue;
    mach_eieio();

    if (regno < 16)
	switch (info2->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB16
	    case 16:
		info2->fbcon_cmap.cfb16[regno] = (regno << 10) | (regno << 5) | regno;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	    case 32:
		i = (regno << 8) | regno;
		info2->fbcon_cmap.cfb32[regno] = (i << 16) | i;
		break;
#endif
       }

    return 0;
}


static void do_install_cmap(int con, struct fb_info *info)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, 1, offb_setcolreg, info);
    else
    {
	int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
	fb_set_cmap(fb_default_cmap(size), 1, offb_setcolreg, info);
    }
}


#ifdef CONFIG_FB_COMPAT_XPMAC

    /*
     *  Backward compatibility mode for Xpmac
     */

int console_getmode(struct vc_mode *mode)
{
    *mode = display_info;
    return 0;
}

int console_setmode(struct vc_mode *mode, int doit)
{
    struct fb_var_screeninfo var;
    int cmode, err;

    if (!console_fb_info)
	return -EOPNOTSUPP;
    switch (mode->depth) {
	case 8:
	case 0:		/* default */
	    cmode = CMODE_8;
	    break;
	case 16:
	    cmode = CMODE_16;
	    break;
	case 24:
	case 32:
	    cmode = CMODE_32;
	    break;
	default:
	    return -EINVAL;
    }
    if ((err = mac_vmode_to_var(mode->mode, cmode, &var)))
	return err;
    var.activate = FB_ACTIVATE_TEST;
    err = console_fb_info->fbops->fb_set_var(&var, fg_console,
					     console_fb_info);
    if (err || !doit)
	return err;
    else {
	int unit;
	var.activate = FB_ACTIVATE_NOW;
	for (unit = 0; unit < MAX_NR_CONSOLES; unit++)
	    if (fb_display[unit].conp &&
		(GET_FB_IDX(console_fb_info->node) == con2fb_map[unit]))
		    console_fb_info->fbops->fb_set_var(&var, unit,
						       console_fb_info);
    }
    return 0;
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
    int i, j, n, err;

    if (!console_fb_info)
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
	err = console_fb_info->fbops->fb_set_cmap(&palette_cmap, 1, fg_console,
						  console_fb_info);
	if (err)
	    return err;
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
