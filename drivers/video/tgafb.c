/*
 *  linux/drivers/video/tgafb.c -- DEC 21030 TGA frame buffer device
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This driver is partly based on the original TGA console driver
 *
 *	Copyright (C) 1995  Jay Estabrook
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */


/* KNOWN PROBLEMS/TO DO ===================================================== *
 *
 *	- How to set a single color register on 24-plane cards?
 *
 *	- Hardware cursor (useful for other graphics boards too)
 *
 *	- Support for more resolutions
 *
 *	- Some redraws can stall kernel for several seconds
 *
 * KNOWN PROBLEMS/TO DO ==================================================== */

#include <linux/module.h>
#include <linux/sched.h>
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
#include <linux/pci.h>
#include <linux/selection.h>
#include <asm/io.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb32.h>


/* TGA hardware description (minimal) */
/*
 * Offsets within Memory Space
 */
#define	TGA_ROM_OFFSET			0x0000000
#define	TGA_REGS_OFFSET			0x0100000
#define	TGA_8PLANE_FB_OFFSET		0x0200000
#define	TGA_24PLANE_FB_OFFSET		0x0804000
#define	TGA_24PLUSZ_FB_OFFSET		0x1004000

#define	TGA_PLANEMASK_REG		0x0028
#define	TGA_MODE_REG			0x0030
#define	TGA_RASTEROP_REG		0x0034
#define	TGA_DEEP_REG			0x0050
#define	TGA_PIXELMASK_REG		0x005c
#define	TGA_CURSOR_BASE_REG		0x0060
#define	TGA_HORIZ_REG			0x0064
#define	TGA_VERT_REG			0x0068
#define	TGA_BASE_ADDR_REG		0x006c
#define	TGA_VALID_REG			0x0070
#define	TGA_CURSOR_XY_REG		0x0074
#define	TGA_INTR_STAT_REG		0x007c
#define	TGA_RAMDAC_SETUP_REG		0x00c0
#define	TGA_BLOCK_COLOR0_REG		0x0140
#define	TGA_BLOCK_COLOR1_REG		0x0144
#define	TGA_CLOCK_REG			0x01e8
#define	TGA_RAMDAC_REG			0x01f0
#define	TGA_CMD_STAT_REG		0x01f8

/*
 * useful defines for managing the BT485 on the 8-plane TGA
 */
#define	BT485_READ_BIT			0x01
#define	BT485_WRITE_BIT			0x00

#define	BT485_ADDR_PAL_WRITE		0x00
#define	BT485_DATA_PAL			0x02
#define	BT485_PIXEL_MASK		0x04
#define	BT485_ADDR_PAL_READ		0x06
#define	BT485_ADDR_CUR_WRITE		0x08
#define	BT485_DATA_CUR			0x0a
#define	BT485_CMD_0			0x0c
#define	BT485_ADDR_CUR_READ		0x0e
#define	BT485_CMD_1			0x10
#define	BT485_CMD_2			0x12
#define	BT485_STATUS			0x14
#define	BT485_CMD_3			0x14
#define	BT485_CUR_RAM			0x16
#define	BT485_CUR_LOW_X			0x18
#define	BT485_CUR_HIGH_X		0x1a
#define	BT485_CUR_LOW_Y			0x1c
#define	BT485_CUR_HIGH_Y		0x1e

/*
 * useful defines for managing the BT463 on the 24-plane TGAs
 */
#define	BT463_ADDR_LO		0x0
#define	BT463_ADDR_HI		0x1
#define	BT463_REG_ACC		0x2
#define	BT463_PALETTE		0x3

#define	BT463_CUR_CLR_0		0x0100
#define	BT463_CUR_CLR_1		0x0101

#define	BT463_CMD_REG_0		0x0201
#define	BT463_CMD_REG_1		0x0202
#define	BT463_CMD_REG_2		0x0203

#define	BT463_READ_MASK_0	0x0205
#define	BT463_READ_MASK_1	0x0206
#define	BT463_READ_MASK_2	0x0207
#define	BT463_READ_MASK_3	0x0208

#define	BT463_BLINK_MASK_0	0x0209
#define	BT463_BLINK_MASK_1	0x020a
#define	BT463_BLINK_MASK_2	0x020b
#define	BT463_BLINK_MASK_3	0x020c

#define	BT463_WINDOW_TYPE_BASE	0x0300


int tga_type;
unsigned int tga_mem_base;
unsigned long tga_fb_base;
unsigned long tga_regs_base;

static unsigned int fb_offset_presets[4] __initdata = {
	TGA_8PLANE_FB_OFFSET,
	TGA_24PLANE_FB_OFFSET,
	0xffffffff,
	TGA_24PLUSZ_FB_OFFSET
};

static unsigned int deep_presets[4] __initdata = {
  0x00014000,
  0x0001440d,
  0xffffffff,
  0x0001441d
};

static unsigned int rasterop_presets[4] __initdata = {
  0x00000003,
  0x00000303,
  0xffffffff,
  0x00000303
};

static unsigned int mode_presets[4] __initdata = {
  0x00002000,
  0x00002300,
  0xffffffff,
  0x00002300
};

static unsigned int base_addr_presets[4] __initdata = {
  0x00000000,
  0x00000001,
  0xffffffff,
  0x00000001
};

#define TGA_WRITE_REG(v,r) \
	{ writel((v), tga_regs_base+(r)); mb(); }

#define TGA_READ_REG(r) readl(tga_regs_base+(r))

#define BT485_WRITE(v,r) \
	  TGA_WRITE_REG((r),TGA_RAMDAC_SETUP_REG);		\
	  TGA_WRITE_REG(((v)&0xff)|((r)<<8),TGA_RAMDAC_REG);

#define BT463_LOAD_ADDR(a) \
	TGA_WRITE_REG(BT463_ADDR_LO<<2, TGA_RAMDAC_SETUP_REG); \
	TGA_WRITE_REG((BT463_ADDR_LO<<10)|((a)&0xff), TGA_RAMDAC_REG); \
	TGA_WRITE_REG(BT463_ADDR_HI<<2, TGA_RAMDAC_SETUP_REG); \
	TGA_WRITE_REG((BT463_ADDR_HI<<10)|(((a)>>8)&0xff), TGA_RAMDAC_REG);

#define BT463_WRITE(m,a,v) \
	BT463_LOAD_ADDR((a)); \
	TGA_WRITE_REG(((m)<<2),TGA_RAMDAC_SETUP_REG); \
	TGA_WRITE_REG(((m)<<10)|((v)&0xff),TGA_RAMDAC_REG);


unsigned char PLLbits[7] __initdata = { 0x80, 0x04, 0x00, 0x24, 0x44, 0x80, 0xb8 };

const unsigned long bt485_cursor_source[64] __initdata = {
#if 1
  0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000,
  0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000,
  0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000,
  0x0000000000000000,0x0000000000000000,0x0000000000000000,0x0000000000000000,
#else
  0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,
  0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,
  0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,
  0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,0x00000000000000ff,
#endif
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

const unsigned int bt463_cursor_source[256] __initdata = {
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0xffff0000, 0x00000000, 0x00000000, 0x00000000,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

static int currcon = 0;
static struct display disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[256];
#ifdef FBCON_HAS_CFB32
static u32 fbcon_cfb32_cmap[16];
#endif

static struct fb_fix_screeninfo fb_fix = { { "DEC TGA ", } };
static struct fb_var_screeninfo fb_var = { 0, };


    /*
     *  Interface used by the world
     */

static int tgafb_open(struct fb_info *info, int user);
static int tgafb_release(struct fb_info *info, int user);
static int tgafb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int tgafb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int tgafb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int tgafb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info);
static int tgafb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int tgafb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int tgafb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info);


    /*
     *  Interface to the low level console driver
     */

void tgafb_init(void);
static int tgafbcon_switch(int con, struct fb_info *info);
static int tgafbcon_updatevar(int con, struct fb_info *info);
static void tgafbcon_blank(int blank, struct fb_info *info);


    /*
     *  Internal routines
     */

static int tgafb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info);
static int tgafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info);
#if 1
static void tga_update_palette(void);
#endif
static void do_install_cmap(int con, struct fb_info *info);


static struct fb_ops tgafb_ops = {
    tgafb_open, tgafb_release, tgafb_get_fix, tgafb_get_var, tgafb_set_var,
    tgafb_get_cmap, tgafb_set_cmap, tgafb_pan_display, tgafb_ioctl
};


    /*
     *  Open/Release the frame buffer device
     */

static int tgafb_open(struct fb_info *info, int user)
{
    /*                                                                     
     *  Nothing, only a usage count for the moment                          
     */                                                                    

    MOD_INC_USE_COUNT;
    return(0);                              
}
        
static int tgafb_release(struct fb_info *info, int user)
{
    MOD_DEC_USE_COUNT;
    return(0);                                                    
}


    /*
     *  Get the Fixed Part of the Display
     */

static int tgafb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
    memcpy(fix, &fb_fix, sizeof(fb_fix));
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int tgafb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    memcpy(var, &fb_var, sizeof(fb_var));
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int tgafb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
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
	do_install_cmap(con, info);
    }
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int tgafb_pan_display(struct fb_var_screeninfo *var, int con,
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

static int tgafb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, kspc, tgafb_getcolreg, info);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else
	fb_copy_cmap(fb_default_cmap(256), cmap, kspc ? 0 : 2);
    return 0;
}

    /*
     *  Set the Colormap
     */

static int tgafb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
    int err;

    if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
	if ((err = fb_alloc_cmap(&fb_display[con].cmap, 256, 0)))
	    return err;
    }
    if (con == currcon) {		/* current console? */
	err = fb_set_cmap(cmap, kspc, tgafb_setcolreg, info);
#if 1
	if (tga_type != 0)
		tga_update_palette();
#endif
	return err;
    } else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


static int tgafb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}


    /*
     *  Initialisation
     */

__initfunc(void tgafb_init(void))
{
    int i, j, temp;
    unsigned char *cbp;
    struct pci_dev *pdev;

    pdev = pci_find_device(PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TGA, NULL);
    if (!pdev)
	return;
    tga_mem_base = pdev->base_address[0] & PCI_BASE_ADDRESS_MEM_MASK;
#ifdef DEBUG
    printk("tgafb_init: mem_base 0x%x\n", tga_mem_base);
#endif /* DEBUG */

    tga_type = (readl((unsigned long)tga_mem_base) >> 12) & 0x0f;
    switch (tga_type) {
	case 0:
	    strcat(fb_fix.id, "8plane");
	    break;
	case 1:
	    strcat(fb_fix.id, "24plane");
	    break;
	case 3:
	    strcat(fb_fix.id, "24plusZ");
	    break;
	default:
	    printk("TGA type (0x%x) unrecognized!\n", tga_type);
	    return;
    }

    tga_regs_base = ((unsigned long)tga_mem_base + TGA_REGS_OFFSET);
    tga_fb_base = ((unsigned long)tga_mem_base + fb_offset_presets[tga_type]);

    /* first, disable video timing */
    TGA_WRITE_REG(0x03, TGA_VALID_REG); /* SCANNING and BLANK */

    /* write the DEEP register */
    while (TGA_READ_REG(TGA_CMD_STAT_REG) & 1) /* wait for not busy */
      continue;

    mb();
    TGA_WRITE_REG(deep_presets[tga_type], TGA_DEEP_REG);
    while (TGA_READ_REG(TGA_CMD_STAT_REG) & 1) /* wait for not busy */
	continue;
    mb();

    /* write some more registers */
    TGA_WRITE_REG(rasterop_presets[tga_type], TGA_RASTEROP_REG);
    TGA_WRITE_REG(mode_presets[tga_type], TGA_MODE_REG);
    TGA_WRITE_REG(base_addr_presets[tga_type], TGA_BASE_ADDR_REG);

    /* write the PLL for 640x480 @ 60Hz */
    for (i = 0; i <= 6; i++) {
	for (j = 0; j <= 7; j++) {
	    temp = (PLLbits[i] >> (7-j)) & 1;
	    if (i == 6 && j == 7)
		temp |= 2;
	    TGA_WRITE_REG(temp, TGA_CLOCK_REG);
	}
    }

    /* write some more registers */
    TGA_WRITE_REG(0xffffffff, TGA_PLANEMASK_REG);
    TGA_WRITE_REG(0xffffffff, TGA_PIXELMASK_REG);
    TGA_WRITE_REG(0x12345678, TGA_BLOCK_COLOR0_REG);
    TGA_WRITE_REG(0x12345678, TGA_BLOCK_COLOR1_REG);

    /* init video timing regs for 640x480 @ 60 Hz */
    TGA_WRITE_REG(0x018608a0, TGA_HORIZ_REG);
    TGA_WRITE_REG(0x084251e0, TGA_VERT_REG);

    if (tga_type == 0) { /* 8-plane */

	fb_var.bits_per_pixel = 8;
	fb_fix.visual = FB_VISUAL_PSEUDOCOLOR;

	/* init BT485 RAMDAC registers */
	BT485_WRITE(0xa2, BT485_CMD_0);
	BT485_WRITE(0x01, BT485_ADDR_PAL_WRITE);
	BT485_WRITE(0x14, BT485_CMD_3); /* cursor 64x64 */
	BT485_WRITE(0x40, BT485_CMD_1);
	BT485_WRITE(0x22, BT485_CMD_2); /* WIN cursor type */
	BT485_WRITE(0xff, BT485_PIXEL_MASK);

	/* fill palette registers */
	BT485_WRITE(0x00, BT485_ADDR_PAL_WRITE);
	TGA_WRITE_REG(BT485_DATA_PAL, TGA_RAMDAC_SETUP_REG);

	for (i = 0; i < 16; i++) {
	    j = color_table[i];
	    TGA_WRITE_REG(default_red[j]|(BT485_DATA_PAL<<8), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(default_grn[j]|(BT485_DATA_PAL<<8), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(default_blu[j]|(BT485_DATA_PAL<<8), TGA_RAMDAC_REG);
	    palette[i].red=default_red[j];
	    palette[i].green=default_grn[j];
	    palette[i].blue=default_blu[j];
	}
	for (i = 0; i < 240*3; i += 4) {
	    TGA_WRITE_REG(0x55|(BT485_DATA_PAL<<8), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x00|(BT485_DATA_PAL<<8), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x00|(BT485_DATA_PAL<<8), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x00|(BT485_DATA_PAL<<8), TGA_RAMDAC_REG);
	}	  

	/* initialize RAMDAC cursor colors */
	BT485_WRITE(0, BT485_ADDR_CUR_WRITE);

	BT485_WRITE(0x00, BT485_DATA_CUR); /* overscan WHITE */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* overscan WHITE */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* overscan WHITE */

	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 1 BLACK */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 1 BLACK */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 1 BLACK */

	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 2 BLACK */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 2 BLACK */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 2 BLACK */

	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 3 BLACK */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 3 BLACK */
	BT485_WRITE(0x00, BT485_DATA_CUR); /* color 3 BLACK */

	/* initialize RAMDAC cursor RAM */
	BT485_WRITE(0x00, BT485_ADDR_PAL_WRITE);
	cbp = (unsigned char *)bt485_cursor_source;
	for (i = 0; i < 512; i++) {
	    BT485_WRITE(*cbp++, BT485_CUR_RAM);
	}
	for (i = 0; i < 512; i++) {
	    BT485_WRITE(0xff, BT485_CUR_RAM);
	}

    } else { /* 24-plane or 24plusZ */

	fb_var.bits_per_pixel = 32;
	fb_fix.visual = FB_VISUAL_TRUECOLOR;

	TGA_WRITE_REG(0x01, TGA_VALID_REG); /* SCANNING */

	/*
	 * init some registers
	 */
	BT463_WRITE(BT463_REG_ACC, BT463_CMD_REG_0, 0x40);
	BT463_WRITE(BT463_REG_ACC, BT463_CMD_REG_1, 0x08);
	BT463_WRITE(BT463_REG_ACC, BT463_CMD_REG_2, 0x40);

	BT463_WRITE(BT463_REG_ACC, BT463_READ_MASK_0, 0xff);
	BT463_WRITE(BT463_REG_ACC, BT463_READ_MASK_1, 0xff);
	BT463_WRITE(BT463_REG_ACC, BT463_READ_MASK_2, 0xff);
	BT463_WRITE(BT463_REG_ACC, BT463_READ_MASK_3, 0x0f);

	BT463_WRITE(BT463_REG_ACC, BT463_BLINK_MASK_0, 0x00);
	BT463_WRITE(BT463_REG_ACC, BT463_BLINK_MASK_1, 0x00);
	BT463_WRITE(BT463_REG_ACC, BT463_BLINK_MASK_2, 0x00);
	BT463_WRITE(BT463_REG_ACC, BT463_BLINK_MASK_3, 0x00);

	/*
	 * fill the palette
	 */
	BT463_LOAD_ADDR(0x0000);
	TGA_WRITE_REG((BT463_PALETTE<<2), TGA_RAMDAC_REG);

	for (i = 0; i < 16; i++) {
	    j = color_table[i];
	    TGA_WRITE_REG(default_red[j]|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(default_grn[j]|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(default_blu[j]|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	}
	for (i = 0; i < 512*3; i += 4) {
	    TGA_WRITE_REG(0x55|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x00|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x00|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x00|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	}	  

	/*
	 * fill window type table after start of vertical retrace
	 */
	while (!(TGA_READ_REG(TGA_INTR_STAT_REG) & 0x01))
	    continue;
	TGA_WRITE_REG(0x01, TGA_INTR_STAT_REG);
	mb();
	while (!(TGA_READ_REG(TGA_INTR_STAT_REG) & 0x01))
	    continue;
	TGA_WRITE_REG(0x01, TGA_INTR_STAT_REG);

	BT463_LOAD_ADDR(BT463_WINDOW_TYPE_BASE);
	TGA_WRITE_REG((BT463_REG_ACC<<2), TGA_RAMDAC_SETUP_REG);
	
	for (i = 0; i < 16; i++) {
	    TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x01|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);
	    TGA_WRITE_REG(0x80|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);
	}

	/*
	 * init cursor colors
	 */
	BT463_LOAD_ADDR(BT463_CUR_CLR_0);

	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG); /* background */
	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG); /* background */
	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG); /* background */

	TGA_WRITE_REG(0xff|(BT463_REG_ACC<<10), TGA_RAMDAC_REG); /* foreground */
	TGA_WRITE_REG(0xff|(BT463_REG_ACC<<10), TGA_RAMDAC_REG); /* foreground */
	TGA_WRITE_REG(0xff|(BT463_REG_ACC<<10), TGA_RAMDAC_REG); /* foreground */

	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);
	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);
	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);

	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);
	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);
	TGA_WRITE_REG(0x00|(BT463_REG_ACC<<10), TGA_RAMDAC_REG);

	/*
	 * finally, init the cursor shape
	 */
	temp = tga_fb_base - 1024; /* this assumes video starts at base
				     and base is beyond memory start*/

	for (i = 0; i < 256; i++) {
	    writel(bt463_cursor_source[i], temp + i*4);
	}
	TGA_WRITE_REG(temp & 0x000fffff, TGA_CURSOR_BASE_REG);
    }

    /* finally, enable video scan
       (and pray for the monitor... :-) */
    TGA_WRITE_REG(0x01, TGA_VALID_REG); /* SCANNING */

    fb_var.xres = fb_var.xres_virtual = 640;
    fb_var.yres = fb_var.yres_virtual = 480;
    fb_fix.line_length = 80*fb_var.bits_per_pixel;
    fb_fix.smem_start = (char *)__pa(tga_fb_base + dense_mem(tga_fb_base));
    fb_fix.smem_len = fb_fix.line_length*fb_var.yres;
    fb_fix.type = FB_TYPE_PACKED_PIXELS;
    fb_fix.type_aux = 0;
    fb_fix.mmio_start = (char *)__pa(tga_regs_base);
    fb_fix.mmio_len = 0x1000;		/* Is this sufficient? */
    fb_fix.accel = FB_ACCEL_DEC_TGA;

    fb_var.xoffset = fb_var.yoffset = 0;
    fb_var.grayscale = 0;
    if (tga_type == 0) { /* 8-plane */
	fb_var.red.offset = 0;
	fb_var.green.offset = 0;
	fb_var.blue.offset = 0;
    } else { /* 24-plane or 24plusZ */
	/* XXX: is this correct?? */
	fb_var.red.offset = 16;
	fb_var.green.offset = 8;
	fb_var.blue.offset = 0;
    }
    fb_var.red.length = fb_var.green.length = fb_var.blue.length = 8;
    fb_var.red.msb_right = fb_var.green.msb_right = fb_var.blue.msb_right = 0;
    fb_var.transp.offset = fb_var.transp.length = fb_var.transp.msb_right = 0;
    fb_var.nonstd = 0;
    fb_var.activate = 0;
    fb_var.height = fb_var.width = -1;
    fb_var.accel_flags = 0;
    fb_var.pixclock = 39722;
    fb_var.left_margin = 40;
    fb_var.right_margin = 24;
    fb_var.upper_margin = 32;
    fb_var.lower_margin = 11;
    fb_var.hsync_len = 96;
    fb_var.vsync_len = 2;
    fb_var.sync = 0;
    fb_var.vmode = FB_VMODE_NONINTERLACED;

    disp.var = fb_var;
    disp.cmap.start = 0;
    disp.cmap.len = 0;
    disp.cmap.red = disp.cmap.green = disp.cmap.blue = disp.cmap.transp = NULL;
    disp.screen_base = (char *)tga_fb_base + dense_mem(tga_fb_base);
    disp.visual = fb_fix.visual;
    disp.type = fb_fix.type;
    disp.type_aux = fb_fix.type_aux;
    disp.ypanstep = 0;
    disp.ywrapstep = 0;
    disp.line_length = fb_fix.line_length;
    disp.can_soft_blank = 1;
    disp.inverse = 0;
    switch (tga_type) {
#ifdef FBCON_HAS_CFB8
	case 0: /* 8-plane */
	    disp.dispsw = &fbcon_cfb8;
	    break;
#endif
#ifdef FBCON_HAS_CFB32
	case 1: /* 24-plane */
	case 3: /* 24plusZ */
	    disp.dispsw = &fbcon_cfb32;
	    disp.dispsw_data = &fbcon_cfb32_cmap;
	    break;
#endif
	default:
	    disp.dispsw = &fbcon_dummy;
    }
    disp.scrollmode = SCROLL_YREDRAW;

    strcpy(fb_info.modename, fb_fix.id);
    fb_info.node = -1;
    fb_info.fbops = &tgafb_ops;
    fb_info.disp = &disp;
    fb_info.fontname[0] = '\0';
    fb_info.changevar = NULL;
    fb_info.switch_con = &tgafbcon_switch;
    fb_info.updatevar = &tgafbcon_updatevar;
    fb_info.blank = &tgafbcon_blank;
    fb_info.flags = FBINFO_FLAG_DEFAULT;

    tgafb_set_var(&fb_var, -1, &fb_info);

    if (register_framebuffer(&fb_info) < 0)
	return;

    printk("fb%d: %s frame buffer device\n", GET_FB_IDX(fb_info.node),
	   fb_fix.id);
}


static int tgafbcon_switch(int con, struct fb_info *info)
{
    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, 1, tgafb_getcolreg, info);

    currcon = con;
    /* Install new colormap */
    do_install_cmap(con, info);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int tgafbcon_updatevar(int con, struct fb_info *info)
{
    /* Nothing */
    return 0;
}

    /*
     *  Blank and unblank the display.
     */

static void tgafbcon_blank(int blank, struct fb_info *info)
{
    static int tga_vesa_blanked = 0;
    u32 vhcr, vvcr;
    unsigned long flags;
    
    save_flags(flags);
    cli();

    vhcr = TGA_READ_REG(TGA_HORIZ_REG);
    vvcr = TGA_READ_REG(TGA_VERT_REG);

    switch (blank) {
    case 0: /* Unblanking */
        if (tga_vesa_blanked) {
	   TGA_WRITE_REG(vhcr & 0xbfffffff, TGA_HORIZ_REG);
	   TGA_WRITE_REG(vvcr & 0xbfffffff, TGA_VERT_REG);
	   tga_vesa_blanked = 0;
	}
 	TGA_WRITE_REG(0x01, TGA_VALID_REG); /* SCANNING */
	break;

    case 1: /* Normal blanking */
	TGA_WRITE_REG(0x03, TGA_VALID_REG); /* SCANNING and BLANK */
	break;

    case 2: /* VESA blank (vsync off) */
	TGA_WRITE_REG(vvcr | 0x40000000, TGA_VERT_REG);
	TGA_WRITE_REG(0x02, TGA_VALID_REG); /* BLANK */
	tga_vesa_blanked = 1;
	break;

    case 3: /* VESA blank (hsync off) */
	TGA_WRITE_REG(vhcr | 0x40000000, TGA_HORIZ_REG);
	TGA_WRITE_REG(0x02, TGA_VALID_REG); /* BLANK */
	tga_vesa_blanked = 1;
	break;

    case 4: /* Poweroff */
	TGA_WRITE_REG(vhcr | 0x40000000, TGA_HORIZ_REG);
	TGA_WRITE_REG(vvcr | 0x40000000, TGA_VERT_REG);
	TGA_WRITE_REG(0x02, TGA_VALID_REG); /* BLANK */
	tga_vesa_blanked = 1;
	break;
    }

    restore_flags(flags);
}

    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int tgafb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info)
{
    if (regno > 255)
	return 1;
    *red = (palette[regno].red<<8) | palette[regno].red;
    *green = (palette[regno].green<<8) | palette[regno].green;
    *blue = (palette[regno].blue<<8) | palette[regno].blue;
    *transp = 0;
    return 0;
}


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int tgafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info)
{
    if (regno > 255)
	return 1;
    red >>= 8;
    green >>= 8;
    blue >>= 8;
    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;

#ifdef FBCON_HAS_CFB32
    if (regno < 16 && tga_type != 0)
	fbcon_cfb32_cmap[regno] = (red << 16) | (green << 8) | blue;
#endif

    if (tga_type == 0) { /* 8-plane */
        BT485_WRITE(regno, BT485_ADDR_PAL_WRITE);
        TGA_WRITE_REG(BT485_DATA_PAL, TGA_RAMDAC_SETUP_REG);
        TGA_WRITE_REG(red|(BT485_DATA_PAL<<8),TGA_RAMDAC_REG);
        TGA_WRITE_REG(green|(BT485_DATA_PAL<<8),TGA_RAMDAC_REG);
        TGA_WRITE_REG(blue|(BT485_DATA_PAL<<8),TGA_RAMDAC_REG);
    }                                                    
    /* How to set a single color register on 24-plane cards?? */

    return 0;
}


#if 1
    /*
     *	FIXME: since I don't know how to set a single arbitrary color register
     *  on 24-plane cards, all color palette registers have to be updated
     */

static void tga_update_palette(void)
{
    int i;

    BT463_LOAD_ADDR(0x0000);
    TGA_WRITE_REG((BT463_PALETTE<<2), TGA_RAMDAC_REG);

    for (i = 0; i < 256; i++) {
	 TGA_WRITE_REG(palette[i].red|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	 TGA_WRITE_REG(palette[i].green|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
	 TGA_WRITE_REG(palette[i].blue|(BT463_PALETTE<<10), TGA_RAMDAC_REG);
    }
}
#endif

static void do_install_cmap(int con, struct fb_info *info)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, 1, tgafb_setcolreg, info);
    else
	fb_set_cmap(fb_default_cmap(256), 1, tgafb_setcolreg, info);
#if 1
    if (tga_type != 0)
        tga_update_palette();
#endif
}

#if 0	/* No cursor stuff yet */

/*
 * Hide the cursor from view, during blanking, usually...
 */
void
hide_cursor(void)
{
	unsigned long flags;
	save_flags(flags); cli();

	if (tga_type == 0) {
	  BT485_WRITE(0x20, BT485_CMD_2);
	} else {
	  TGA_WRITE_REG(0x03, TGA_VALID_REG); /* SCANNING and BLANK */
	}

	restore_flags(flags);
}

void
set_cursor(int currcons)
{
  unsigned int idx, xt, yt, row, col;
  unsigned long flags;

  if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
    return;

  save_flags(flags); cli();

  if (deccm) {
    idx = (pos - video_mem_base) >> 1;
    col = idx % 80;
    row = (idx - col) / 80;

    if (tga_type == 0) { /* 8-plane */

      xt = col * TGA_F_WIDTH + 64;
      yt = row * TGA_F_HEIGHT_PADDED + 64;

      /* make sure it's enabled */
      BT485_WRITE(0x22, BT485_CMD_2); /* WIN cursor type */

      BT485_WRITE(xt, BT485_CUR_LOW_X);
      BT485_WRITE((xt >> 8), BT485_CUR_HIGH_X);
      BT485_WRITE(yt, BT485_CUR_LOW_Y);
      BT485_WRITE((yt >> 8), BT485_CUR_HIGH_Y);

    } else {

      xt = col * TGA_F_WIDTH + 144;
      yt = row * TGA_F_HEIGHT_PADDED + 35;

      TGA_WRITE_REG(0x05, TGA_VALID_REG); /* SCANNING and CURSOR */
      TGA_WRITE_REG(xt | (yt << 12), TGA_CURSOR_XY_REG);
    }

  } else
    hide_cursor();
  restore_flags(flags);
}

#endif
