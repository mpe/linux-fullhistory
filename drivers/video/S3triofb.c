/*
 *  linux/drivers/video/S3Triofb.c -- Open Firmware based frame buffer device
 *
 *	Copyright (C) 1997 Peter De Schrijver
 *
 *  This driver is partly based on the PowerMac console driver:
 *
 *	Copyright (C) 1996 Paul Mackerras
 *
 *  and on the Open Firmware based frame buffer device:
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

/*
	Bugs : + OF dependencies should be removed.
               + This driver should be merged with the CyberVision driver. The
                 CyberVision is a Zorro III implementation of the S3Trio64 chip.

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
#include <linux/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <linux/pci.h>

#include "fbcon-cfb8.h"


#define mem_in8(addr)           in_8((void *)(addr))
#define mem_in16(addr)          in_le16((void *)(addr))
#define mem_in32(addr)          in_le32((void *)(addr))

#define mem_out8(val, addr)     out_8((void *)(addr), val)
#define mem_out16(val, addr)    out_le16((void *)(addr), val)
#define mem_out32(val, addr)    out_le32((void *)(addr), val)

#define IO_OUT16VAL(v, r)       (((v) << 8) | (r))

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

static int currcon = 0;
static struct display disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[256];
static char s3trio_name[16] = "S3Trio ";


static struct fb_fix_screeninfo fb_fix;
static struct fb_var_screeninfo fb_var = { 0, };


    /*
     *  Interface used by the world
     */

void of_video_setup(char *options, int *ints);

static int s3trio_open(struct fb_info *info);
static int s3trio_release(struct fb_info *info);
static int s3trio_get_fix(struct fb_fix_screeninfo *fix, int con,
			  struct fb_info *info);
static int s3trio_get_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info);
static int s3trio_set_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info);
static int s3trio_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info);
static int s3trio_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info);
static int s3trio_pan_display(struct fb_var_screeninfo *var, int con,
			      struct fb_info *info);
static int s3trio_ioctl(struct inode *inode, struct file *file, u_int cmd,
			u_long arg, int con, struct fb_info *info);

#ifdef CONFIG_FB_COMPAT_XPMAC
extern struct vc_mode display_info;
extern struct fb_info *console_fb_info;
extern int (*console_setmode_ptr)(struct vc_mode *, int);
extern int (*console_set_cmap_ptr)(struct fb_cmap *, int, int,
				   struct fb_info *);
static int s3trio_console_setmode(struct vc_mode *mode, int doit);
#endif /* CONFIG_FB_COMPAT_XPMAC */

    /*
     *  Interface to the low level console driver
     */

unsigned long s3trio_fb_init(unsigned long mem_start);
static int s3triofbcon_switch(int con, struct fb_info *info);
static int s3triofbcon_updatevar(int con, struct fb_info *info);
static void s3triofbcon_blank(int blank, struct fb_info *info);
static int s3triofbcon_setcmap(struct fb_cmap *cmap, int con);

    /*
     *  Text console acceleration
     */

#ifdef CONFIG_FBCON_CFB8
static struct display_switch fbcon_trio8;
#endif

    /*
     *    Accelerated Functions used by the low level console driver
     */

static void Trio_WaitQueue(u_short fifo);
static void Trio_WaitBlit(void);
static void Trio_BitBLT(u_short curx, u_short cury, u_short destx,
			u_short desty, u_short width, u_short height,
			u_short mode);
static void Trio_RectFill(u_short x, u_short y, u_short width, u_short height,
			  u_short mode, u_short color);
static void Trio_MoveCursor(u_short x, u_short y);


    /*
     *  Internal routines
     */

static int s3trio_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info);
static int s3trio_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info);
static void do_install_cmap(int con);


static struct fb_ops s3trio_ops = {
    s3trio_open, s3trio_release, s3trio_get_fix, s3trio_get_var, s3trio_set_var,
    s3trio_get_cmap, s3trio_set_cmap, s3trio_pan_display, NULL, s3trio_ioctl
};


    /*
     *  Open/Release the frame buffer device
     */

static int s3trio_open(struct fb_info *info)
{
    /*
     *  Nothing, only a usage count for the moment
     */

    MOD_INC_USE_COUNT;
    return(0);
}

static int s3trio_release(struct fb_info *info)
{
    MOD_DEC_USE_COUNT;
    return(0);
}


    /*
     *  Get the Fixed Part of the Display
     */

static int s3trio_get_fix(struct fb_fix_screeninfo *fix, int con,
			  struct fb_info *info)
{
    memcpy(fix, &fb_fix, sizeof(fb_fix));
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int s3trio_get_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
    memcpy(var, &fb_var, sizeof(fb_var));
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int s3trio_set_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
    if (var->xres > fb_var.xres || var->yres > fb_var.yres ||
	var->xres_virtual > fb_var.xres_virtual ||
	var->yres_virtual > fb_var.yres_virtual ||
	var->bits_per_pixel > fb_var.bits_per_pixel ||
	var->nonstd || var->vmode != FB_VMODE_NONINTERLACED)
	return -EINVAL;
    memcpy(var, &fb_var, sizeof(fb_var));
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int s3trio_pan_display(struct fb_var_screeninfo *var, int con,
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

static int s3trio_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, &fb_display[con].var, kspc, s3trio_getcolreg,
			   info);
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

static int s3trio_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info)
{
    int err;


    if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
	if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				 1<<fb_display[con].var.bits_per_pixel, 0)))
	    return err;
    }
    if (con == currcon)			/* current console? */
	return fb_set_cmap(cmap, &fb_display[con].var, kspc, s3trio_setcolreg,
			   info);
    else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


static int s3trio_ioctl(struct inode *inode, struct file *file, u_int cmd,
			u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}

__initfunc(int s3trio_resetaccel(void)) {


#define EC01_ENH_ENB    0x0005
#define EC01_LAW_ENB    0x0010
#define EC01_MMIO_ENB   0x0020

#define EC00_RESET      0x8000
#define EC00_ENABLE     0x4000
#define MF_MULT_MISC    0xE000
#define SRC_FOREGROUND  0x0020
#define SRC_BACKGROUND  0x0000
#define MIX_SRC                 0x0007
#define MF_T_CLIP       0x1000
#define MF_L_CLIP       0x2000
#define MF_B_CLIP       0x3000
#define MF_R_CLIP       0x4000
#define MF_PIX_CONTROL  0xA000
#define MFA_SRC_FOREGR_MIX      0x0000
#define MF_PIX_CONTROL  0xA000

	outw(EC00_RESET,  0x42e8);
	inw(  0x42e8);
	outw(EC00_ENABLE,  0x42e8);
	inw(  0x42e8);
	outw(EC01_ENH_ENB | EC01_LAW_ENB,
		   0x4ae8);
	outw(MF_MULT_MISC,  0xbee8); /* 16 bit I/O registers */

	/* Now set some basic accelerator registers */
	Trio_WaitQueue(0x0400);
	outw(SRC_FOREGROUND | MIX_SRC, 0xbae8);
	outw(SRC_BACKGROUND | MIX_SRC,  0xb6e8);/* direct color*/
	outw(MF_T_CLIP | 0, 0xbee8 );     /* clip virtual area  */
	outw(MF_L_CLIP | 0, 0xbee8 );
	outw(MF_R_CLIP | (640 - 1), 0xbee8);
	outw(MF_B_CLIP | (480 - 1),  0xbee8);
	Trio_WaitQueue(0x0400);
	outw(0xffff,  0xaae8);       /* Enable all planes */
	outw(0xffff, 0xaae8);       /* Enable all planes */
	outw( MF_PIX_CONTROL | MFA_SRC_FOREGR_MIX,  0xbee8);

}

__initfunc(int s3trio_init(void)) {

    u_char bus, dev;
    unsigned int t32;
    unsigned short cmd;
    int i;

	bus=0;
	dev=(3<<3);
                pcibios_read_config_dword(bus, dev, PCI_VENDOR_ID, &t32);
                if(t32 == (PCI_DEVICE_ID_S3_TRIO << 16) + PCI_VENDOR_ID_S3) {
                        pcibios_read_config_dword(bus, dev, PCI_BASE_ADDRESS_0, &t32);
                        pcibios_read_config_dword(bus, dev, PCI_BASE_ADDRESS_1, &t32);
			pcibios_read_config_word(bus, dev, PCI_COMMAND,&cmd);

			pcibios_write_config_word(bus, dev, PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

			pcibios_write_config_dword(bus, dev, PCI_BASE_ADDRESS_0,0xffffffff);
                        pcibios_read_config_dword(bus, dev, PCI_BASE_ADDRESS_0, &t32);

/* This is a gross hack as OF only maps enough memory for the framebuffer and
   we want to use MMIO too. We should find out which chunk of address space
   we can use here */
			pcibios_write_config_dword(bus,dev,PCI_BASE_ADDRESS_0,0xc6000000);

			/* unlock s3 */

			outb(0x01, 0x3C3);

			outb(inb(0x03CC) | 1, 0x3c2);

			outw(IO_OUT16VAL(0x48, 0x38),0x03D4);
			outw(IO_OUT16VAL(0xA0, 0x39),0x03D4);
			outb(0x33,0x3d4);
			outw(IO_OUT16VAL( inb(0x3d5) & ~(0x2 |
			 0x10 | 0x40) , 0x33),0x3d4);

			outw(IO_OUT16VAL(0x6,0x8), 0x3c4);

			/* switch to MMIO only mode */

			outb(0x58,0x3d4);
			outw(IO_OUT16VAL(inb(0x3d5) | 3 | 0x10,0x58),0x3d4);
			outw(IO_OUT16VAL(8,0x53),0x3d4);

			/* switch off I/O accesses */

#if 0
			pcibios_write_config_word(bus, dev, PCI_COMMAND,
				        PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
#endif
                }

	return 0;
}


    /*
     *  Initialisation
     *  We heavily rely on OF for the moment. This needs fixing.
     */

__initfunc(unsigned long s3trio_fb_init(unsigned long mem_start))
{
    struct device_node *dp;
    int i, err, *pp, len;
    unsigned *up, address;
    u_long *CursorBase;

    if (!prom_display_paths[0])
	return mem_start;
    if (!(dp = find_path_device(prom_display_paths[0])))
	return mem_start;

    strncat(s3trio_name, dp->name, sizeof(s3trio_name));
    s3trio_name[sizeof(s3trio_name)-1] = '\0';
    strcpy(fb_fix.id, s3trio_name);

    if((pp = (int *)get_property(dp, "vendor-id", &len)) != NULL
	&& *pp!=PCI_VENDOR_ID_S3) {
	printk("%s: can't find S3 Trio board\n", dp->full_name);
	return mem_start;
    }

    if((pp = (int *)get_property(dp, "device-id", &len)) != NULL
	&& *pp!=PCI_DEVICE_ID_S3_TRIO) {
	printk("%s: can't find S3 Trio board\n", dp->full_name);
	return mem_start;
    }

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

    s3trio_init();
    address=0xc6000000;
    fb_fix.smem_start = ioremap(address,64*1024*1024);
    fb_fix.type = FB_TYPE_PACKED_PIXELS;
    fb_fix.type_aux = 0;


    s3trio_resetaccel();

	mem_out8(0x30,fb_fix.smem_start+0x1008000 + 0x03D4);
	mem_out8(0x2d,fb_fix.smem_start+0x1008000 + 0x03D4);
	mem_out8(0x2e,fb_fix.smem_start+0x1008000 + 0x03D4);

	mem_out8(0x50,fb_fix.smem_start+0x1008000 + 0x03D4);

    /* disable HW cursor */

    mem_out8(0x39,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(0xa0,fb_fix.smem_start+0x1008000 + 0x03D5);

    mem_out8(0x45,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(0,fb_fix.smem_start+0x1008000 + 0x03D5);

    mem_out8(0x4e,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(0,fb_fix.smem_start+0x1008000 + 0x03D5);

    mem_out8(0x4f,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(0,fb_fix.smem_start+0x1008000 + 0x03D5);

    /* init HW cursor */

    CursorBase=(u_long *)(fb_fix.smem_start + 2*1024*1024 - 0x400);
	for (i=0; i < 8; i++) {
		*(CursorBase  +(i*4)) = 0xffffff00;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}
	for (i=8; i < 64; i++) {
		*(CursorBase  +(i*4)) = 0xffff0000;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}


    mem_out8(0x4c,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(((2*1024 - 1)&0xf00)>>8,fb_fix.smem_start+0x1008000 + 0x03D5);

    mem_out8(0x4d,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8((2*1024 - 1) & 0xff,fb_fix.smem_start+0x1008000 + 0x03D5);

    mem_out8(0x45,fb_fix.smem_start+0x1008000 + 0x03D4);
	mem_in8(fb_fix.smem_start+0x1008000 + 0x03D4);

    mem_out8(0x4a,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(0x80,fb_fix.smem_start+0x1008000 + 0x03D5);
    mem_out8(0x80,fb_fix.smem_start+0x1008000 + 0x03D5);
    mem_out8(0x80,fb_fix.smem_start+0x1008000 + 0x03D5);

    mem_out8(0x4b,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(0x00,fb_fix.smem_start+0x1008000 + 0x03D5);
    mem_out8(0x00,fb_fix.smem_start+0x1008000 + 0x03D5);
    mem_out8(0x00,fb_fix.smem_start+0x1008000 + 0x03D5);

    mem_out8(0x45,fb_fix.smem_start+0x1008000 + 0x03D4);
    mem_out8(0,fb_fix.smem_start+0x1008000 + 0x03D5);

    s3trio_setcolreg(255, 56, 100, 160, 0, NULL /* not used */);
    s3trio_setcolreg(254, 0, 0, 0, 0, NULL /* not used */);
    memset((char *)fb_fix.smem_start,0,640*480);

#if 0
    Trio_RectFill(0,0,90,90,7,1);
#endif

    fb_fix.visual = FB_VISUAL_PSEUDOCOLOR ;
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
    fb_var.accel = 5;
    fb_var.pixclock = 1;
    fb_var.left_margin = fb_var.right_margin = 0;
    fb_var.upper_margin = fb_var.lower_margin = 0;
    fb_var.hsync_len = fb_var.vsync_len = 0;
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
#ifdef CONFIG_FBCON_CFB8
    disp.dispsw = &fbcon_trio8;
#else
    disp.dispsw = NULL;
#endif

    strcpy(fb_info.modename, "Trio64 ");
    strncat(fb_info.modename, dp->full_name, sizeof(fb_info.modename));
    fb_info.node = -1;
    fb_info.fbops = &s3trio_ops;
#if 0
    fb_info.fbvar_num = 1;
    fb_info.fbvar = &fb_var;
#endif
    fb_info.disp = &disp;
    fb_info.fontname[0] = '\0';
    fb_info.changevar = NULL;
    fb_info.switch_con = &s3triofbcon_switch;
    fb_info.updatevar = &s3triofbcon_updatevar;
    fb_info.blank = &s3triofbcon_blank;
#if 0
    fb_info.setcmap = &s3triofbcon_setcmap;
#endif

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (!console_fb_info) {
	display_info.height = fb_var.yres;
	display_info.width = fb_var.xres;
	display_info.depth = 8;
	display_info.pitch = fb_fix.line_length;
	display_info.mode = 0;
	strncpy(display_info.name, dp->name, sizeof(display_info.name));
	display_info.fb_address = (unsigned long)fb_fix.smem_start;
	display_info.disp_reg_address = address + 0x1008000;
	display_info.cmap_adr_address = address + 0x1008000 + 0x3c8;
	display_info.cmap_data_address = address + 0x1008000 + 0x3c9;
	console_fb_info = &fb_info;
	console_setmode_ptr = s3trio_console_setmode;
	console_set_cmap_ptr = s3trio_set_cmap;
    }
#endif /* CONFIG_FB_COMPAT_XPMAC) */

    err = register_framebuffer(&fb_info);
    if (err < 0)
	return mem_start;

    printk("fb%d: S3 Trio frame buffer device on %s\n",
	   GET_FB_IDX(fb_info.node), dp->full_name);

    return mem_start;
}


static int s3triofbcon_switch(int con, struct fb_info *info)
{
    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
		    s3trio_getcolreg, info);

    currcon = con;
    /* Install new colormap */
    do_install_cmap(con);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int s3triofbcon_updatevar(int con, struct fb_info *info)
{
    /* Nothing */
    return 0;
}

    /*
     *  Blank the display.
     */

static void s3triofbcon_blank(int blank, struct fb_info *info)
{
    /* Nothing */
}

    /*
     *  Set the colormap
     */

static int s3triofbcon_setcmap(struct fb_cmap *cmap, int con)
{
    return(s3trio_set_cmap(cmap, 1, con, &fb_info));
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int s3trio_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info)
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

static int s3trio_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                            u_int transp, struct fb_info *info)
{
    if (regno > 255)
	return 1;
    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;

    mem_out8(regno,fb_fix.smem_start+0x1008000 + 0x3c8);
    mem_out8((red & 0xff) >> 2,fb_fix.smem_start+0x1008000 + 0x3c9);
    mem_out8((green & 0xff) >> 2,fb_fix.smem_start+0x1008000 + 0x3c9);
    mem_out8((blue & 0xff) >> 2,fb_fix.smem_start+0x1008000 + 0x3c9);

    return 0;
}


static void do_install_cmap(int con)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
		    s3trio_setcolreg, &fb_info);
    else
	fb_set_cmap(fb_default_cmap(fb_display[con].var.bits_per_pixel),
		    &fb_display[con].var, 1, s3trio_setcolreg, &fb_info);
}

#ifdef CONFIG_FB_COMPAT_XPMAC

    /*
     *  Backward compatibility mode for Xpmac
     */

static int s3trio_console_setmode(struct vc_mode *mode, int doit)
{
    int err;
    struct fb_var_screeninfo var;
    struct s3trio_par par;

    if (mode->mode <= 0 || mode->mode > VMODE_MAX )
        return -EINVAL;
    par.video_mode = mode->mode;

    switch (mode->depth) {
        case 24:
        case 32:
            par.color_mode = CMODE_32;
            break;
        case 16:
            par.color_mode = CMODE_16;
            break;
        case 8:
        case 0:			/* (default) */
            par.color_mode = CMODE_8;
            break;
        default:
            return -EINVAL;
    }
    encode_var(&var, &par);
    if ((err = decode_var(&var, &par)))
        return err;
    if (doit)
        s3trio_set_var(&var, currcon, 0);
    return 0;
}

#endif /* CONFIG_FB_COMPAT_XPMAC */

void s3trio_video_setup(char *options, int *ints) {

        return;

}

static void Trio_WaitQueue(u_short fifo) {

	u_short status;

        do
        {
		status = mem_in16(fb_fix.smem_start + 0x1000000 + 0x9AE8);
	}  while (!(status & fifo));

}

static void Trio_WaitBlit(void) {

	u_short status;

        do
        {
		status = mem_in16(fb_fix.smem_start + 0x1000000 + 0x9AE8);
	}  while (status & 0x200);

}

static void Trio_BitBLT(u_short curx, u_short cury, u_short destx,
			u_short desty, u_short width, u_short height,
			u_short mode) {

	u_short blitcmd = 0xc011;

	/* Set drawing direction */
        /* -Y, X maj, -X (default) */

	if (curx > destx)
		blitcmd |= 0x0020;  /* Drawing direction +X */
	else {
		curx  += (width - 1);
		destx += (width - 1);
	}

	if (cury > desty)
		blitcmd |= 0x0080;  /* Drawing direction +Y */
	else {
		cury  += (height - 1);
		desty += (height - 1);
	}

	Trio_WaitQueue(0x0400);

	outw(0xa000,  0xBEE8);
	outw(0x60 | mode,  0xBAE8);

	outw(curx,  0x86E8);
	outw(cury,  0x82E8);

	outw(destx,  0x8EE8);
	outw(desty,  0x8AE8);

	outw(height - 1,  0xBEE8);
	outw(width - 1,  0x96E8);

	outw(blitcmd,  0x9AE8);

}

static void Trio_RectFill(u_short x, u_short y, u_short width, u_short height,
			  u_short mode, u_short color) {

	u_short blitcmd = 0x40b1;

	Trio_WaitQueue(0x0400);

	outw(0xa000,  0xBEE8);
	outw((0x20 | mode),  0xBAE8);
	outw(0xe000,  0xBEE8);
	outw(color,  0xA6E8);
	outw(x,  0x86E8);
	outw(y,  0x82E8);
	outw((height - 1), 0xBEE8);
	outw((width - 1), 0x96E8);
	outw(blitcmd,  0x9AE8);

}


static void Trio_MoveCursor(u_short x, u_short y) {

	mem_out8(0x39, fb_fix.smem_start + 0x1008000 + 0x3d4);
	mem_out8(0xa0, fb_fix.smem_start + 0x1008000 + 0x3d5);

	mem_out8(0x46, fb_fix.smem_start + 0x1008000 + 0x3d4);
	mem_out8((x & 0x0700) >> 8, fb_fix.smem_start + 0x1008000 + 0x3d5);
	mem_out8(0x47, fb_fix.smem_start + 0x1008000 + 0x3d4);
	mem_out8(x & 0x00ff, fb_fix.smem_start + 0x1008000 + 0x3d5);

	mem_out8(0x48, fb_fix.smem_start + 0x1008000 + 0x3d4);
	mem_out8((y & 0x0700) >> 8, fb_fix.smem_start + 0x1008000 + 0x3d5);
	mem_out8(0x49, fb_fix.smem_start + 0x1008000 + 0x3d4);
	mem_out8(y & 0x00ff, fb_fix.smem_start + 0x1008000 + 0x3d5);

}


    /*
     *  Text console acceleration
     */

#ifdef CONFIG_FBCON_CFB8
static void fbcon_trio8_bmove(struct display *p, int sy, int sx, int dy,
			      int dx, int height, int width)
{
    sx *= 8; dx *= 8; width *= 8;
    Trio_BitBLT((u_short)sx, (u_short)(sy*p->fontheight), (u_short)dx,
		 (u_short)(dy*p->fontheight), (u_short)width,
		 (u_short)(height*p->fontheight), (u_short)S3_NEW);
}

static void fbcon_trio8_clear(struct vc_data *conp, struct display *p, int sy,
			      int sx, int height, int width)
{
    unsigned char bg;

    sx *= 8; width *= 8;
    bg = attr_bgcol_ec(p,conp);
    Trio_RectFill((u_short)sx,
		   (u_short)(sy*p->fontheight),
		   (u_short)width,
		   (u_short)(height*p->fontheight),
		   (u_short)S3_NEW,
		   (u_short)bg);
}

static void fbcon_trio8_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx)
{
    Trio_WaitBlit();
    fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_trio8_putcs(struct vc_data *conp, struct display *p,
			      const char *s, int count, int yy, int xx)
{
    Trio_WaitBlit();
    fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_trio8_revc(struct display *p, int xx, int yy)
{
    Trio_WaitBlit();
    fbcon_cfb8_revc(p, xx, yy);
}

static struct display_switch fbcon_trio8 = {
   fbcon_cfb8_setup, fbcon_trio8_bmove, fbcon_trio8_clear, fbcon_trio8_putc,
   fbcon_trio8_putcs, fbcon_trio8_revc
};
#endif
