/*
 *  linux/drivers/video/atyfb.c -- Frame buffer device for ATI/Open Firmware
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This driver is partly based on the PowerMac console driver:
 *
 *	Copyright (C) 1996 Paul Mackerras
 *
 *  and on the PowerMac ATI/mach64 display driver:
 *
 *	Copyright (C) 1997 Michael AK Tesch
 *
 *	      with work by Jon Howell
 *			   Harry AC Eaton
 *			   Anthony Tong <atong@uiuc.edu>
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
#include <linux/nvram.h>
#include <linux/vc_ioctl.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#include "aty.h"
#include "fbcon.h"
#include "fbcon-cfb8.h"
#include "fbcon-cfb16.h"
#include "fbcon-cfb32.h"


static int currcon = 0;
static struct display fb_disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[256];

static char atyfb_name[16] = "ATY Mach64";

struct atyfb_par {
    int vmode;
    int cmode;
    u_int vxres;	/* virtual screen size */
    u_int vyres;
    int xoffset;	/* virtual screen position */
    int yoffset;
};


/*
 * Video mode values.
 * These are supposed to be the same as the values that
 * Apple uses in MacOS.
 */
#define VMODE_NVRAM		0	/* use value stored in nvram */
#define VMODE_512_384_60I	1	/* 512x384, 60Hz interlaced (NTSC) */
#define VMODE_512_384_60	2	/* 512x384, 60Hz */
#define VMODE_640_480_50I	3	/* 640x480, 50Hz interlaced (PAL) */
#define VMODE_640_480_60I	4	/* 640x480, 60Hz interlaced (NTSC) */
#define VMODE_640_480_60	5	/* 640x480, 60Hz (VGA) */
#define VMODE_640_480_67	6	/* 640x480, 67Hz */
#define VMODE_640_870_75P	7	/* 640x870, 75Hz (portrait) */
#define VMODE_768_576_50I	8	/* 768x576, 50Hz (PAL full frame) */
#define VMODE_800_600_56	9	/* 800x600, 56Hz */
#define VMODE_800_600_60	10	/* 800x600, 60Hz */
#define VMODE_800_600_72	11	/* 800x600, 72Hz */
#define VMODE_800_600_75	12	/* 800x600, 75Hz */
#define VMODE_832_624_75	13	/* 832x624, 75Hz */
#define VMODE_1024_768_60	14	/* 1024x768, 60Hz */
#define VMODE_1024_768_70	15	/* 1024x768, 70Hz (or 72Hz?) */
#define VMODE_1024_768_75V	16	/* 1024x768, 75Hz (VESA) */
#define VMODE_1024_768_75	17	/* 1024x768, 75Hz */
#define VMODE_1152_870_75	18	/* 1152x870, 75Hz */
#define VMODE_1280_960_75	19	/* 1280x960, 75Hz */
#define VMODE_1280_1024_75	20	/* 1280x1024, 75Hz */
#define VMODE_MAX		20
#define VMODE_CHOOSE		99	/* choose based on monitor sense */

/*
 * Color mode values, used to select number of bits/pixel.
 */
#define CMODE_NVRAM		-1	/* use value stored in nvram */
#define CMODE_8			0	/* 8 bits/pixel */
#define CMODE_16		1	/* 16 (actually 15) bits/pixel */
#define CMODE_32		2	/* 32 (actually 24) bits/pixel */


static int default_video_mode = VMODE_NVRAM;
static int default_color_mode = CMODE_NVRAM;

static struct atyfb_par default_par;
static struct atyfb_par current_par;


/*
 * Addresses in NVRAM where video mode and pixel size are stored.
 */
#define NV_VMODE	0x140f
#define NV_CMODE	0x1410

/*
 * Horizontal and vertical resolution information.
 */
extern struct vmode_attr {
	int	hres;
	int	vres;
	int	vfreq;
	int	interlaced;
} vmode_attrs[VMODE_MAX];


/*
 * Horizontal and vertical resolution for each mode.
 */
static struct vmode_attr vmode_attrs[VMODE_MAX] = {
    {512, 384, 60, 1},
    {512, 384, 60},
    {640, 480, 50, 1},
    {640, 480, 60, 1},
    {640, 480, 60},
    {640, 480, 67},
    {640, 870, 75},
    {768, 576, 50, 1},
    {800, 600, 56},
    {800, 600, 60},
    {800, 600, 72},
    {800, 600, 75},
    {832, 624, 75},
    {1024, 768, 60},
    {1024, 768, 72},
    {1024, 768, 75},
    {1024, 768, 75},
    {1152, 870, 75},
    {1280, 960, 75},
    {1280, 1024, 75}
};


/*
 * We get a sense value from the monitor and use it to choose
 * what resolution to use.  This structure maps sense values
 * to display mode values (which determine the resolution and
 * frequencies).
 */
static struct mon_map {
	int	sense;
	int	vmode;
} monitor_map [] = {
	{0x000, VMODE_1280_1024_75},	/* 21" RGB */
	{0x114, VMODE_640_870_75P},	/* Portrait Monochrome */
	{0x221, VMODE_512_384_60},	/* 12" RGB*/
	{0x331, VMODE_1280_1024_75},	/* 21" RGB (Radius) */
	{0x334, VMODE_1280_1024_75},	/* 21" mono (Radius) */
	{0x335, VMODE_1280_1024_75},	/* 21" mono */
	{0x40A, VMODE_640_480_60I},	/* NTSC */
	{0x51E, VMODE_640_870_75P},	/* Portrait RGB */
	{0x603, VMODE_832_624_75},	/* 12"-16" multiscan */
	{0x60b, VMODE_1024_768_70},	/* 13"-19" multiscan */
	{0x623, VMODE_1152_870_75},	/* 13"-21" multiscan */
	{0x62b, VMODE_640_480_67},	/* 13"/14" RGB */
	{0x700, VMODE_640_480_50I},	/* PAL */
	{0x714, VMODE_640_480_60I},	/* NTSC */
	{0x717, VMODE_800_600_75},	/* VGA */
	{0x72d, VMODE_832_624_75},	/* 16" RGB (Goldfish) */
	{0x730, VMODE_768_576_50I},	/* PAL (Alternate) */
	{0x73a, VMODE_1152_870_75},	/* 3rd party 19" */
	{-1,	VMODE_640_480_60},	/* catch-all, must be last */
};

static int map_monitor_sense(int sense)
{
	struct mon_map *map;

	for (map = monitor_map; map->sense >= 0; ++map)
		if (map->sense == sense)
			break;
	return map->vmode;
}

struct aty_cmap_regs {
    unsigned char windex;
    unsigned char lut;
    unsigned char mask;
    unsigned char rindex;
    unsigned char cntl;
};

typedef struct aty_regvals {
    int offset[3];		/* first pixel address */

    int crtc_h_sync_strt_wid[3];	/* depth dependent */
    int crtc_gen_cntl[3];
    int mem_cntl[3];

    int crtc_h_tot_disp;	/* mode dependent */
    int crtc_v_tot_disp;
    int crtc_v_sync_strt_wid;
    int crtc_off_pitch;

    unsigned char clock_val[2];	/* vals for 20 and 21 */
} aty_regvals;

struct rage_regvals {
    int h_total, h_sync_start, h_sync_width;
    int v_total, v_sync_start, v_sync_width;
    int h_sync_neg, v_sync_neg;
};

static int aty_vram_reqd(const struct atyfb_par *par);
static struct aty_regvals *get_aty_struct(int vmode);

static unsigned long frame_buffer;

static int total_vram;		/* total amount of video memory, bytes */
static int chip_type;		/* what chip type was detected */

static unsigned long ati_regbase;
static struct aty_cmap_regs *aty_cmap_regs;

#include "ati-gx.h"
#include "ati-gt.h"
#include "ati-vt.h"

static struct aty_regvals *aty_gt_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&aty_gt_reg_init_5,
	&aty_gt_reg_init_6,
	NULL, NULL,
	&aty_gt_reg_init_9,
	&aty_gt_reg_init_10,
	&aty_gt_reg_init_11,
	&aty_gt_reg_init_12,
	&aty_gt_reg_init_13,
	&aty_gt_reg_init_14,
	&aty_gt_reg_init_15,
	NULL,
	&aty_gt_reg_init_17,
	&aty_gt_reg_init_18,
	NULL,
	&aty_gt_reg_init_20
};

static struct aty_regvals *aty_gx_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&aty_gx_reg_init_6,
	&aty_gx_reg_init_6,
	NULL, NULL, NULL, NULL, NULL, NULL,
	&aty_gx_reg_init_13,
	&aty_gx_reg_init_14,
	&aty_gx_reg_init_15,
	NULL,
	&aty_gx_reg_init_17,
	&aty_gx_reg_init_18,
	NULL,
	&aty_gx_reg_init_20
};

static struct aty_regvals *aty_vt_reg_init[21] = {
	NULL, NULL, NULL, NULL,
	&aty_vt_reg_init_5,
	&aty_vt_reg_init_6,
	NULL, NULL, NULL,
	&aty_vt_reg_init_10,
	&aty_vt_reg_init_11,
	&aty_vt_reg_init_12,
	&aty_vt_reg_init_13,
	&aty_vt_reg_init_14,
	&aty_vt_reg_init_15,
	NULL,
	&aty_vt_reg_init_17,
	&aty_vt_reg_init_18,
	&aty_vt_reg_init_19,
	&aty_vt_reg_init_20
};

    /*
     *  Interface used by the world
     */

unsigned long atyfb_init(unsigned long mem_start);
void atyfb_setup(char *options, int *ints);

static int atyfb_open(struct fb_info *info);
static int atyfb_release(struct fb_info *info);
static int atyfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int atyfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int atyfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int atyfb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info);
static int atyfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int atyfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int atyfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info);


    /*
     *  Interface to the low level console driver
     */

static int atyfbcon_switch(int con, struct fb_info *info);
static int atyfbcon_updatevar(int con, struct fb_info *info);
static void atyfbcon_blank(int blank, struct fb_info *info);


    /*
     *  Text console acceleration
     */

#ifdef CONFIG_FBCON_CFB8
static struct display_switch fbcon_aty8;
#endif


#ifdef CONFIG_FB_COMPAT_XPMAC
extern struct vc_mode display_info;
extern struct fb_info *console_fb_info;
extern int (*console_setmode_ptr)(struct vc_mode *, int);
extern int (*console_set_cmap_ptr)(struct fb_cmap *, int, int,
				   struct fb_info *);
static int atyfb_console_setmode(struct vc_mode *, int);
#endif


    /*
     *  Internal routines
     */

static int atyfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			 u_int *transp, struct fb_info *info);
static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);


static struct fb_ops atyfb_ops = {
    atyfb_open, atyfb_release, atyfb_get_fix, atyfb_get_var, atyfb_set_var,
    atyfb_get_cmap, atyfb_set_cmap, atyfb_pan_display, NULL, atyfb_ioctl
};


static inline int aty_vram_reqd(const struct atyfb_par *par)
{
    return (par->vxres*par->vyres) << par->cmode;
}

extern inline unsigned aty_ld_le32(volatile unsigned long addr)
{
    register unsigned long temp = ati_regbase,val;

    asm("lwbrx %0,%1,%2": "=r"(val):"r"(addr), "r"(temp));
    return val;
}

extern inline void aty_st_le32(volatile unsigned long addr, unsigned val)
{
    register unsigned long temp = ati_regbase;

    asm("stwbrx %0,%1,%2": : "r"(val), "r"(addr), "r"(temp):"memory");
}

extern inline unsigned char aty_ld_8(volatile unsigned long addr)
{
    return *(char *) ((long) addr + (long) ati_regbase);
}

extern inline void aty_st_8(volatile unsigned long addr, unsigned char val)
{
    *(unsigned char *) (addr + (unsigned long) ati_regbase) = val;
}

static void aty_st_514(int offset, char val)
{
    aty_WaitQueue(5);
    aty_st_8(DAC_CNTL, 1);
    aty_st_8(DAC_W_INDEX, offset & 0xff);	/* right addr byte */
    aty_st_8(DAC_DATA, (offset >> 8) & 0xff);	/* left addr byte */
    eieio();
    aty_st_8(DAC_MASK, val);
    eieio();
    aty_st_8(DAC_CNTL, 0);
}

static void aty_st_pll(int offset, char val)
{
    aty_WaitQueue(3);
    aty_st_8(CLOCK_CNTL + 1, (offset << 2) | PLL_WR_EN);	/* write addr byte */
    eieio();
    aty_st_8(CLOCK_CNTL + 2, val);	/* write the register value */
    eieio();
    aty_st_8(CLOCK_CNTL + 1, (offset << 2) & ~PLL_WR_EN);
}

static struct aty_regvals *get_aty_struct(int vmode)
{
    int v = vmode - 1;

    switch (chip_type) {
	case MACH64_GT_ID:
	    return aty_gt_reg_init[v];
	    break;
	case MACH64_VT_ID:
	    return aty_vt_reg_init[v];
	    break;
	default: /* default to MACH64_GX_ID */
	    return aty_gx_reg_init[v];
	    break;
    }
}

static int read_aty_sense(void)
{
    int sense, i;

    aty_st_le32(GP_IO, 0x31003100);	/* drive outputs high */
    __delay(200);
    aty_st_le32(GP_IO, 0);		/* turn off outputs */
    __delay(2000);
    i = aty_ld_le32(GP_IO);		/* get primary sense value */
    sense = ((i & 0x3000) >> 3) | (i & 0x100);

    /* drive each sense line low in turn and collect the other 2 */
    aty_st_le32(GP_IO, 0x20000000);	/* drive A low */
    __delay(2000);
    i = aty_ld_le32(GP_IO);
    sense |= ((i & 0x1000) >> 7) | ((i & 0x100) >> 4);
    aty_st_le32(GP_IO, 0x20002000);	/* drive A high again */
    __delay(200);

    aty_st_le32(GP_IO, 0x10000000);	/* drive B low */
    __delay(2000);
    i = aty_ld_le32(GP_IO);
    sense |= ((i & 0x2000) >> 10) | ((i & 0x100) >> 6);
    aty_st_le32(GP_IO, 0x10001000);	/* drive B high again */
    __delay(200);

    aty_st_le32(GP_IO, 0x01000000);	/* drive C low */
    __delay(2000);
    sense |= (aty_ld_le32(GP_IO) & 0x3000) >> 12;
    aty_st_le32(GP_IO, 0);		/* turn off outputs */

    return sense;
}

static void RGB514_Program(int cmode)
{
    typedef struct {
	char pixel_dly;
	char misc2_cntl;
	char pixel_rep;
	char pixel_cntl_index;
	char pixel_cntl_v1;
    } RGB514_DAC_Table;

    static RGB514_DAC_Table RGB514DAC_Tab[8] = {
	{0, 0x41, 0x03, 0x71, 0x45},	// 8bpp
	{0, 0x45, 0x04, 0x0c, 0x01},	// 555
	{0, 0x45, 0x06, 0x0e, 0x00},	// XRGB
    };
    RGB514_DAC_Table *pDacProgTab;

    pDacProgTab = &RGB514DAC_Tab[cmode];

    aty_st_514(0x90, 0x00);
    aty_st_514(0x04, pDacProgTab->pixel_dly);
    aty_st_514(0x05, 0x00);

    aty_st_514(0x2, 0x1);
    aty_st_514(0x71, pDacProgTab->misc2_cntl);
    aty_st_514(0x0a, pDacProgTab->pixel_rep);

    aty_st_514(pDacProgTab->pixel_cntl_index, pDacProgTab->pixel_cntl_v1);
}

static void set_off_pitch(const struct atyfb_par *par)
{
    u32 pitch, offset;

    pitch = par->vxres>>3;
    offset = ((par->yoffset*par->vxres+par->xoffset)>>3)<<par->cmode;
    aty_st_le32(CRTC_OFF_PITCH, pitch<<22 | offset);
    if (chip_type == MACH64_GT_ID) {
	/* Is this OK for other chips? */
	aty_st_le32(DST_OFF_PITCH, pitch<<22 | offset);
	aty_st_le32(SRC_OFF_PITCH, pitch<<22 | offset);
    }
}

static void atyfb_set_par(struct atyfb_par *par)
{
    int i, hres;
    struct aty_regvals *init = get_aty_struct(par->vmode);
    int vram_type = aty_ld_le32(CONFIG_STAT0) & 7;

    if (init == 0)	/* paranoia, shouldn't get here */
	panic("aty: display mode %d not supported", par->vmode);

    current_par = *par;
    hres = vmode_attrs[par->vmode-1].hres;

    /* clear FIFO errors */
    aty_st_le32(BUS_CNTL, aty_ld_le32(BUS_CNTL) | BUS_HOST_ERR_ACK
			  | BUS_FIFO_ERR_ACK);

    /* Reset engine */
    i = aty_ld_le32(GEN_TEST_CNTL);
    aty_st_le32(GEN_TEST_CNTL, i & ~GUI_ENGINE_ENABLE);
    eieio();
    aty_WaitIdleEmpty();
    aty_st_le32(GEN_TEST_CNTL, i | GUI_ENGINE_ENABLE);
    aty_WaitIdleEmpty();

    if ( chip_type != MACH64_GT_ID ) {
	i = aty_ld_le32(CRTC_GEN_CNTL);
	aty_st_le32(CRTC_GEN_CNTL, i | CRTC_EXT_DISP_EN);
    }

    if ( chip_type == MACH64_GX_ID ) {
	i = aty_ld_le32(GEN_TEST_CNTL);
	aty_st_le32(GEN_TEST_CNTL, i | GEN_OVR_OUTPUT_EN );
    }

    switch (chip_type) {
	case MACH64_VT_ID:
	    aty_st_pll(PLL_MACRO_CNTL, 0xb5);
	    aty_st_pll(PLL_REF_DIV, 0x2d);
	    aty_st_pll(PLL_GEN_CNTL, 0x14);
	    aty_st_pll(MCLK_FB_DIV, 0xbd);
	    aty_st_pll(PLL_VCLK_CNTL, 0x0b);
	    aty_st_pll(VCLK_POST_DIV, init->clock_val[0]);
	    aty_st_pll(VCLK0_FB_DIV, init->clock_val[1]);
	    aty_st_pll(VCLK1_FB_DIV, 0xd6);
	    aty_st_pll(VCLK2_FB_DIV, 0xee);
	    aty_st_pll(VCLK3_FB_DIV, 0xf8);
	    aty_st_pll(PLL_XCLK_CNTL, 0x0);
	    aty_st_pll(PLL_TEST_CTRL, 0x0);
	    aty_st_pll(PLL_TEST_COUNT, 0x0);
	    break;
	case MACH64_GT_ID:
	    if (vram_type == 5) {
		aty_st_pll(0, 0xcd);
		aty_st_pll(PLL_MACRO_CNTL,
			   par->vmode >= VMODE_1024_768_60 ? 0xd3: 0xd5);
		aty_st_pll(PLL_REF_DIV, 0x21);
		aty_st_pll(PLL_GEN_CNTL, 0x44);
		aty_st_pll(MCLK_FB_DIV, 0xe8);
		aty_st_pll(PLL_VCLK_CNTL, 0x03);
		aty_st_pll(VCLK_POST_DIV, init->offset[0]);
		aty_st_pll(VCLK0_FB_DIV, init->offset[1]);
		aty_st_pll(VCLK1_FB_DIV, 0x8e);
		aty_st_pll(VCLK2_FB_DIV, 0x9e);
		aty_st_pll(VCLK3_FB_DIV, 0xc6);
		aty_st_pll(PLL_XCLK_CNTL, init->offset[2]);
		aty_st_pll(12, 0xa6);
		aty_st_pll(13, 0x1b);
	    } else {
		aty_st_pll(PLL_MACRO_CNTL, 0xd5);
		aty_st_pll(PLL_REF_DIV, 0x21);
		aty_st_pll(PLL_GEN_CNTL, 0xc4);
		aty_st_pll(MCLK_FB_DIV, 0xda);
		aty_st_pll(PLL_VCLK_CNTL, 0x03);
		/* offset actually holds clock values */
		aty_st_pll(VCLK_POST_DIV, init->offset[0]);
		aty_st_pll(VCLK0_FB_DIV, init->offset[1]);
		aty_st_pll(VCLK1_FB_DIV, 0x8e);
		aty_st_pll(VCLK2_FB_DIV, 0x9e);
		aty_st_pll(VCLK3_FB_DIV, 0xc6);
		aty_st_pll(PLL_TEST_CTRL, 0x0);
		aty_st_pll(PLL_XCLK_CNTL, init->offset[2]);
		aty_st_pll(12, 0xa0);
		aty_st_pll(13, 0x1b);
	    }
	    break;
	default:
	    RGB514_Program(par->cmode);
	    aty_WaitIdleEmpty();
	    aty_st_514(0x06, 0x02);
	    aty_st_514(0x10, 0x01);
	    aty_st_514(0x70, 0x01);
	    aty_st_514(0x8f, 0x1f);
	    aty_st_514(0x03, 0x00);
	    aty_st_514(0x05, 0x00);
	    aty_st_514(0x20, init->clock_val[0]);
	    aty_st_514(0x21, init->clock_val[1]);
	    break;
    }

    aty_ld_8(DAC_REGS);	/* clear counter */
    aty_WaitIdleEmpty();

    aty_st_le32(CRTC_H_TOTAL_DISP, init->crtc_h_tot_disp);
    aty_st_le32(CRTC_H_SYNC_STRT_WID, init->crtc_h_sync_strt_wid[par->cmode]);
    aty_st_le32(CRTC_V_TOTAL_DISP, init->crtc_v_tot_disp);
    aty_st_le32(CRTC_V_SYNC_STRT_WID, init->crtc_v_sync_strt_wid);

    aty_st_8(CLOCK_CNTL, 0);
    aty_st_8(CLOCK_CNTL, CLOCK_STROBE);

    aty_st_le32(CRTC_VLINE_CRNT_VLINE, 0);

    set_off_pitch(par);

    if (chip_type == MACH64_GT_ID) {
	aty_st_le32(BUS_CNTL, 0x7b23a040);

	/* need to set DSP values !! assume sdram */
	i = init->crtc_gen_cntl[0] - (0x100000 * par->cmode);
	if ( vram_type == 5 )
	    i = init->crtc_gen_cntl[1] - (0x100000 * par->cmode);
	aty_st_le32(DSP_CONFIG, i);

	i = aty_ld_le32(MEM_CNTL) & MEM_SIZE_ALIAS;
	if ( vram_type == 5 ) {
	    i |= ((1 * par->cmode) << 26) | 0x4215b0;
	    aty_st_le32(DSP_ON_OFF,sgram_dsp[par->vmode-1][par->cmode]);

	//aty_st_le32(CLOCK_CNTL,8192);
	} else {
	    i |= ((1 * par->cmode) << 26) | 0x300090;
	    aty_st_le32(DSP_ON_OFF, init->mem_cntl[par->cmode]);
	}

	aty_st_le32(MEM_CNTL, i);
	aty_st_le32(EXT_MEM_CNTL, 0x5000001);

	/* if (total_vram > 0x400000)	
	    i |= 0x538; this not been verified on > 4Megs!! */
    } else {

/* The magic constant below translates into:
* 5   = No RDY delay, 1 wait st for mem write, increment during burst transfer
* 9   = DAC access delayed, 1 wait state for DAC
* 0   = Disables interupts for FIFO errors
* e   = Allows FIFO to generate 14 wait states before generating error
* 1   = DAC snooping disabled, ROM disabled
* 0   = ROM page at 0 (disabled so doesn't matter)
* f   = 15 ROM wait states (disabled so doesn't matter)
* f   = 15 BUS wait states (I'm not sure this applies to PCI bus types)
* at some point it would be good to experiment with bench marks to see if
* we can gain some speed by fooling with the wait states etc.
*/
	if (chip_type == MACH64_VT_ID)
	    aty_st_le32(BUS_CNTL, 0x680000f9);
	else
	    aty_st_le32(BUS_CNTL, 0x590e10ff);

	switch (total_vram) {
	    case 0x00100000:
		aty_st_le32(MEM_CNTL, vt_mem_cntl[0][par->cmode]);
		break;
	    case 0x00200000:
		aty_st_le32(MEM_CNTL, vt_mem_cntl[1][par->cmode]);
		break;
	    case 0x00400000:
		aty_st_le32(MEM_CNTL, vt_mem_cntl[2][par->cmode]);
		break;
	    default:
		i = aty_ld_le32(MEM_CNTL) & 0x000F;
		aty_st_le32(MEM_CNTL,
			    (init->mem_cntl[par->cmode] & 0xFFFFFFF0) | i);
	}
    }
/* These magic constants are harder to figure out
* on the vt chipset bit 2 set makes the screen brighter
* and bit 15 makes the screen black! But nothing else
* seems to matter for the vt DAC_CNTL
*/
    switch (chip_type) {
	case MACH64_GT_ID:
	    i = 0x86010102;
	    break;
	case MACH64_VT_ID:
	    i = 0x87010184;
	    break;
	default:
	    i = 0x47012100;
	    break;
    }

    aty_st_le32(DAC_CNTL, i);
    aty_st_8(DAC_MASK, 0xff);

    switch (par->cmode) {
	case CMODE_16:
	    i = CRTC_PIX_WIDTH_15BPP; break;
	/*case CMODE_24: */
	case CMODE_32:
	    i = CRTC_PIX_WIDTH_32BPP; break;
	case CMODE_8:
	default:
	    i = CRTC_PIX_WIDTH_8BPP; break;
    }

    if (chip_type != MACH64_GT_ID) {
	aty_st_le32(CRTC_INT_CNTL, 0x00000002);
	aty_st_le32(GEN_TEST_CNTL, GUI_ENGINE_ENABLE | BLOCK_WRITE_ENABLE);	/* gui_en block_en */
	i |= init->crtc_gen_cntl[par->cmode];
    }
    /* Gentlemen, start your crtc engine */
    aty_st_le32(CRTC_GEN_CNTL, CRTC_EXT_DISP_EN | CRTC_ENABLE | i);

#ifdef CONFIG_FB_COMPAT_XPMAC
    display_info.height = vmode_attrs[par->vmode-1].vres;
    display_info.width = vmode_attrs[par->vmode-1].hres;
    display_info.depth = 8<<par->cmode;
    display_info.pitch = par->vxres<<par->cmode;
    display_info.mode = par->vmode;
    strcpy(display_info.name, atyfb_name);
    display_info.fb_address =
	iopa(((chip_type != MACH64_GT_ID) ?
	     frame_buffer + init->offset[par->cmode] : frame_buffer));
    display_info.cmap_adr_address = iopa((unsigned long)&aty_cmap_regs->windex);
    display_info.cmap_data_address = iopa((unsigned long)&aty_cmap_regs->lut);
    display_info.disp_reg_address = iopa(ati_regbase);
#endif /* CONFIG_FB_COMPAT_XPMAC) */
}


    /*
     *  Open/Release the frame buffer device
     */

static int atyfb_open(struct fb_info *info)

{
    /*
     *  Nothing, only a usage count for the moment
     */

    MOD_INC_USE_COUNT;
    return(0);
}

static int atyfb_release(struct fb_info *info)
{
    MOD_DEC_USE_COUNT;
    return(0);
}


static int encode_fix(struct fb_fix_screeninfo *fix,
		      const struct atyfb_par *par)
{
    struct aty_regvals *init;

    memset(fix, 0, sizeof(struct fb_fix_screeninfo));

    strcpy(fix->id, atyfb_name);
    init = get_aty_struct(par->vmode);
    /*
     *  FIXME: This will cause problems on non-GT chips, because the frame
     *  buffer must be aligned to a page
     */
    fix->smem_start = (char *)((chip_type != MACH64_GT_ID)
	    ? frame_buffer + init->offset[par->cmode] : frame_buffer);
    fix->smem_len = (u32)total_vram;
    if (fix->smem_len > 0x7ff000)
	fix->smem_len = 0x7ff000;	/* last page is MMIO */
    fix->mmio_start = (char *)(ati_regbase & ~0xfff);
    fix->mmio_len = 4096;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;
    fix->line_length = par->vxres<<par->cmode;
    fix->visual = par->cmode == CMODE_8 ? FB_VISUAL_PSEUDOCOLOR
					: FB_VISUAL_TRUECOLOR;
    fix->ywrapstep = 0;
    fix->xpanstep = 8;
    fix->ypanstep = 1;

    return 0;
}


static int decode_var(struct fb_var_screeninfo *var,
		      struct atyfb_par *par)
{
    int xres = var->xres;
    int yres = var->yres;
    int bpp = var->bits_per_pixel;
    struct aty_regvals *init;

    /* This should support more video modes */

    if (xres <= 512 && yres <= 384)
	par->vmode = VMODE_512_384_60;		/* 512x384, 60Hz */
    else if (xres <= 640 && yres <= 480)
	par->vmode = VMODE_640_480_67;		/* 640x480, 67Hz */
    else if (xres <= 640 && yres <= 870)
	par->vmode = VMODE_640_870_75P;		/* 640x870, 75Hz (portrait) */
    else if (xres <= 768 && yres <= 576)
	par->vmode = VMODE_768_576_50I;		/* 768x576, 50Hz (PAL full frame) */
    else if (xres <= 800 && yres <= 600)
	par->vmode = VMODE_800_600_75;		/* 800x600, 75Hz */
    else if (xres <= 832 && yres <= 624)
	par->vmode = VMODE_832_624_75;		/* 832x624, 75Hz */
    else if (xres <= 1024 && yres <= 768)
	par->vmode = VMODE_1024_768_75;		/* 1024x768, 75Hz */
    else if (xres <= 1152 && yres <= 870)
	par->vmode = VMODE_1152_870_75;		/* 1152x870, 75Hz */
    else if (xres <= 1280 && yres <= 960)
	par->vmode = VMODE_1280_960_75;		/* 1280x960, 75Hz */
    else if (xres <= 1280 && yres <= 1024)
	par->vmode = VMODE_1280_1024_75;	/* 1280x1024, 75Hz */
    else
	return -EINVAL;

    xres = vmode_attrs[par->vmode-1].hres;
    yres = vmode_attrs[par->vmode-1].vres;

    if (var->xres_virtual <= xres)
	par->vxres = xres;
    else
	par->vxres = (var->xres_virtual+7) & ~7;
    if (var->yres_virtual <= yres)
	par->vyres = yres;
    else
	par->vyres = var->yres_virtual;

    par->xoffset = (var->xoffset+7) & ~7;
    par->yoffset = var->yoffset;
    if (par->xoffset+xres > par->vxres || par->yoffset+yres > par->vyres)
	return -EINVAL;

    if (bpp <= 8)
	par->cmode = CMODE_8;
    else if (bpp <= 16)
	par->cmode = CMODE_16;
    else if (bpp <= 32)
	par->cmode = CMODE_32;
    else
	return -EINVAL;

    if (aty_vram_reqd(par) > total_vram)
	return -EINVAL;

    /* Check if we know about the wanted video mode */
    init = get_aty_struct(par->vmode);
    if (init == NULL || init->crtc_h_sync_strt_wid[par->cmode] == 0 ||
	(chip_type != MACH64_GT_ID &&
	 init->crtc_gen_cntl[par->cmode] == 0) ||
	(chip_type == MACH64_GT_ID && (aty_ld_le32(CONFIG_STAT0) & 7) == 5 &&
	 init->crtc_gen_cntl[1] == 0))
	return -EINVAL;

#if 0
    if (!fbmon_valid_timings(pixclock, htotal, vtotal, info))
	return -EINVAL;
#endif

    return 0;
}

static int encode_var(struct fb_var_screeninfo *var,
		      const struct atyfb_par *par)
{
    memset(var, 0, sizeof(struct fb_var_screeninfo));

    var->xres = vmode_attrs[par->vmode-1].hres;
    var->yres = vmode_attrs[par->vmode-1].vres;
    var->xres_virtual = par->vxres;
    var->yres_virtual = par->vyres;
    var->xoffset = par->xoffset;
    var->yoffset = par->yoffset;
    var->grayscale = 0;
    switch (par->cmode) {
	case CMODE_8:
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
	case CMODE_16:	/* RGB 555 */
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
	case CMODE_32:	/* RGB 888 */
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
    var->red.msb_right = 0;
    var->green.msb_right = 0;
    var->blue.msb_right = 0;
    var->transp.msb_right = 0;
    var->nonstd = 0;
    var->activate = 0;
    var->height = -1;
    var->width = -1;
    var->accel = /* FB_ACCEL_ATY */ 0;
    var->vmode = FB_VMODE_NONINTERLACED;
    var->left_margin = var->right_margin = 64;	/* guesses */
    var->upper_margin = var->lower_margin = 32;
    var->hsync_len = 64;
    var->vsync_len = 2;

    /* no long long support in the kernel :-( */
    /* this splittig trick will work if xres > 232 */
    var->pixclock = 1000000000/
	(var->left_margin+var->xres+var->right_margin+var->hsync_len);
    var->pixclock *= 1000;
    var->pixclock /= vmode_attrs[par->vmode-1].vfreq*
	 (var->upper_margin+var->yres+var->lower_margin+var->vsync_len);
    var->sync = 0;

    return 0;
}


static void init_par(struct atyfb_par *par, int vmode, int cmode)
{
    par->vmode = vmode;
    par->cmode = cmode;
    par->vxres = vmode_attrs[vmode-1].hres;
    par->vyres = vmode_attrs[vmode-1].vres;
    par->xoffset = 0;
    par->yoffset = 0;
}


    /*
     *  Get the Fixed Part of the Display
     */

static int atyfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
    struct atyfb_par par;

    if (con == -1)
	par = default_par;
    else
	decode_var(&fb_display[con].var, &par);
    encode_fix(fix, &par);
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int atyfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    if (con == -1)
	encode_var(var, &default_par);
    else
	*var=fb_display[con].var;
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int atyfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    struct atyfb_par par;
    struct display *display;
    int oldxres, oldyres, oldvxres, oldvyres, oldbpp;
    int err;
    int activate = var->activate;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = &fb_disp;	/* used during initialization */

    if ((err = decode_var(var, &par)))
	return err;

    encode_var(var, &par);

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

	    encode_fix(&fix, &par);
	    display->screen_base = (u_char *)fix.smem_start;
	    display->visual = fix.visual;
	    display->type = fix.type;
	    display->type_aux = fix.type_aux;
	    display->ypanstep = fix.ypanstep;
	    display->ywrapstep = fix.ywrapstep;
	    display->line_length = fix.line_length;
	    display->can_soft_blank = 1;
	    display->inverse = 0;
	    switch (par.cmode) {
		case CMODE_8:
#if 1
		    display->dispsw = &fbcon_cfb8;
#else
		    display->dispsw = &fbcon_aty8;
#endif
		    break;
		case CMODE_16:
		    display->dispsw = &fbcon_cfb16;
		    break;
		case CMODE_32:
		    display->dispsw = &fbcon_cfb32;
		    break;
		default:
		    display->dispsw = NULL;
		    break;
	    }
	    if (fb_info.changevar)
		(*fb_info.changevar)(con);
	}
	if (con == currcon)
	    atyfb_set_par(&par);
	if (oldbpp != var->bits_per_pixel) {
	    if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
		return err;
	    do_install_cmap(con, info);
	}
    }

    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int atyfb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
    u32 xres, yres, xoffset, yoffset;
    struct atyfb_par *par = &current_par;

    xres = vmode_attrs[par->vmode-1].hres;
    yres = vmode_attrs[par->vmode-1].vres;
    xoffset = (var->xoffset+7) & ~7;
    yoffset = var->yoffset;
    if (xoffset+xres > par->vxres || yoffset+yres > par->vyres)
	return -EINVAL;
    par->xoffset = xoffset;
    par->yoffset = yoffset;
    set_off_pitch(par);
    return 0;
}

    /*
     *  Get the Colormap
     */

static int atyfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, &fb_display[con].var, kspc, atyfb_getcolreg,
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

static int atyfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
    int err;

    if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
	if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				 1<<fb_display[con].var.bits_per_pixel, 0)))
	    return err;
    }
    if (con == currcon)			/* current console? */
	return fb_set_cmap(cmap, &fb_display[con].var, kspc, atyfb_setcolreg,
			   info);
    else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


static int atyfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}


    /*
     *  Initialisation
     */

__initfunc(unsigned long atyfb_init(unsigned long mem_start))
{
#ifdef __powerpc__
    /* We don't want to be called like this. */
    /* We rely on Open Firmware (offb) instead. */
    return mem_start;
#else /* !__powerpc__ */
    /* To be merged with Bernd's mach64fb */
    return mem_start;
#endif /* !__powerpc__ */
}


unsigned long atyfb_of_init(unsigned long mem_start, struct device_node *dp)
{
    int i, err, sense;
    struct fb_var_screeninfo var;
    struct aty_regvals *init;
    unsigned long addr;
    unsigned char bus, devfn;
    unsigned short cmd;

    if (dp->next)
	printk("Warning: only using first ATI card detected\n");
    if (dp->n_addrs != 1 && dp->n_addrs != 3)
	printk("Warning: expecting 1 or 3 addresses for ATY (got %d)",
	       dp->n_addrs);

    ati_regbase = (int)ioremap((0x7ffc00 + dp->addrs[0].address), 0x1000);
    aty_cmap_regs = (struct aty_cmap_regs *)(ati_regbase + 0xC0);

    /* enable memory-space accesses using config-space command register */
    if (pci_device_loc(dp, &bus, &devfn) == 0) {
	pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	if (cmd != 0xffff) {
	    cmd |= PCI_COMMAND_MEMORY;
	    pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
	}
    }
    chip_type = (aty_ld_le32(CONFIG_CHIP_ID) & CFG_CHIP_TYPE);

    i = aty_ld_le32(MEM_CNTL);
    if (chip_type != MACH64_GT_ID)
	switch (i & MEM_SIZE_ALIAS) {
	    case MEM_SIZE_512K:
		total_vram = 0x80000;
		break;
	    case MEM_SIZE_1M:
		total_vram = 0x100000;
		break;
	    case MEM_SIZE_2M:
		total_vram = 0x200000;
		break;
	    case MEM_SIZE_4M:
		total_vram = 0x400000;
		break;
	    case MEM_SIZE_6M:
		total_vram = 0x600000;
		break;
	    case MEM_SIZE_8M:
		total_vram = 0x800000;
		break;
	    default:
		total_vram = 0x80000;
	}
    else
	switch (i & 0xF) {	/* 0xF used instead of MEM_SIZE_ALIAS */
	    case MEM_SIZE_512K:
		total_vram = 0x80000;
		break;
	    case MEM_SIZE_1M:
		total_vram = 0x100000;
		break;
	    case MEM_SIZE_2M_GTB:
		total_vram = 0x200000;
		break;
	    case MEM_SIZE_4M_GTB:
		total_vram = 0x400000;
		break;
	    case MEM_SIZE_6M_GTB:
		total_vram = 0x600000;
		break;
	    case MEM_SIZE_8M_GTB:
		total_vram = 0x800000;
		break;
	    default:
		total_vram = 0x80000;
	}

#if 1
    printk("aty_display_init: node = %p, addrs = ", dp->node);
    printk(" %x(%x)", dp->addrs[0].address, dp->addrs[0].size);
    printk(", intrs =");
    for (i = 0; i < dp->n_intrs; ++i)
    printk(" %x", dp->intrs[i].line);
    printk("\nregbase: %x pci loc: %x:%x total_vram: %x cregs: %p\n",
	   (int)ati_regbase, bus, devfn, total_vram, aty_cmap_regs);
#endif

    /* Map in frame buffer */
    addr = dp->addrs[0].address;

    /* use the big-endian aperture (??) */
    addr += 0x800000;
    frame_buffer = (unsigned long)__ioremap(addr, 0x800000, _PAGE_WRITETHRU);

    if (default_video_mode != -1) {
	sense = read_aty_sense();
	printk("monitor sense = %x\n", sense);
	if (default_video_mode == VMODE_NVRAM) {
	    default_video_mode = nvram_read_byte(NV_VMODE);
	    init = get_aty_struct(default_video_mode);
	    if (default_video_mode <= 0 ||
		default_video_mode > VMODE_MAX || init == 0)
		default_video_mode = VMODE_CHOOSE;
	}
	if (default_video_mode == VMODE_CHOOSE)
	    default_video_mode = map_monitor_sense(sense);

	init = get_aty_struct(default_video_mode);
	if (!init)
	    default_video_mode = VMODE_640_480_60;
    }

    /*
     * Reduce the pixel size if we don't have enough VRAM.
     */

    if (default_color_mode == CMODE_NVRAM)
	default_color_mode = nvram_read_byte(NV_CMODE);
    if (default_color_mode < CMODE_8 ||
	default_color_mode > CMODE_32)
	default_color_mode = CMODE_8;

    init_par(&default_par, default_video_mode, default_color_mode);
    while (aty_vram_reqd(&default_par) > total_vram) {
	while (default_color_mode > CMODE_8 &&
	       aty_vram_reqd(&default_par) > total_vram) {
	    --default_color_mode;
	    init_par(&default_par, default_video_mode, default_color_mode);
	}
	/*
	 * adjust the video mode smaller if there still is not enough VRAM
	 */
	if (aty_vram_reqd(&default_par) > total_vram)
	    do {
		default_video_mode--;
		init_par(&default_par, default_video_mode, default_color_mode);
		init = get_aty_struct(default_video_mode);
	    } while ((init == 0) &&
		     (default_video_mode > VMODE_640_480_60));
    }

    if (chip_type == MACH64_GT_ID && (aty_ld_le32(CONFIG_STAT0) & 7) == 5
	&& init->crtc_gen_cntl[1] == 0) {
	    default_video_mode = VMODE_640_480_67;
	    default_color_mode = CMODE_8;
	    init_par(&default_par, default_video_mode, default_color_mode);
    }

    switch (chip_type) {
	case MACH64_GX_ID:
	    strcat(atyfb_name, "GX");
	    break;
	case MACH64_VT_ID:
	    strcat(atyfb_name, "VT");
	    break;
	case MACH64_GT_ID:
	    strcat(atyfb_name, "GT");
	    break;
	default:
	    break;
    }
    strcpy(fb_info.modename, atyfb_name);
    fb_info.node = -1;
    fb_info.fbops = &atyfb_ops;
    fb_info.disp = &fb_disp;
    fb_info.fontname[0] = '\0';
    fb_info.changevar = NULL;
    fb_info.switch_con = &atyfbcon_switch;
    fb_info.updatevar = &atyfbcon_updatevar;
    fb_info.blank = &atyfbcon_blank;

    err = register_framebuffer(&fb_info);
    if (err < 0)
	return mem_start;

    for (i = 0; i < 16; i++) {
	int j = color_table[i];
	palette[i].red = default_red[j];
	palette[i].green = default_grn[j];
	palette[i].blue = default_blu[j];
    }
    atyfb_set_par(&default_par);
    encode_var(&var, &default_par);
    atyfb_set_var(&var, -1, &fb_info);

    printk("fb%d: %s frame buffer device on %s\n", GET_FB_IDX(fb_info.node),
	   atyfb_name, dp->full_name);

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (!console_fb_info) {
	console_fb_info = &fb_info;
	console_setmode_ptr = atyfb_console_setmode;
	console_set_cmap_ptr = atyfb_set_cmap;
    }
#endif /* CONFIG_FB_COMPAT_XPMAC) */

    return mem_start;
}


/* XXX: doesn't work yet */
void atyfb_setup(char *options, int *ints)
{
    char *this_opt;
    int vmode;
    int depth;

    if (!options || !*options)
	return;

    for (this_opt = strtok(options, ","); this_opt;
	 this_opt = strtok(NULL, ",")) {
	if (!strncmp(this_opt, "vmode:", 6)) {
	    vmode = simple_strtoul(this_opt+6, NULL, 0);
	    if (vmode > 0 && vmode <= VMODE_MAX)
		default_video_mode = vmode;
	} else if (!strncmp(this_opt, "cmode:", 6)) {
	    depth = simple_strtoul(this_opt+6, NULL, 0);
	    switch (depth) {
		case 8:
		    default_color_mode = CMODE_8;
		    break;
		case 15:
		case 16:
		    default_color_mode = CMODE_16;
		    break;
		case 24:
		case 32:
		    default_color_mode = CMODE_32;
		    break;
	    };
	}
    }
}


static int atyfbcon_switch(int con, struct fb_info *info)
{
    struct atyfb_par par;

    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
		    atyfb_getcolreg, info);
    currcon = con;
    decode_var(&fb_display[con].var, &par);
    atyfb_set_par(&par);
    /* Install new colormap */
    do_install_cmap(con, info);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int atyfbcon_updatevar(int con, struct fb_info *info)
{
    current_par.yoffset = fb_display[con].var.yoffset;
    set_off_pitch(&current_par);
    return 0;
}

    /*
     *  Blank the display.
     */

static void atyfbcon_blank(int blank, struct fb_info *info)
{
    char gen_cntl;

    gen_cntl = aty_ld_8(CRTC_GEN_CNTL);
    if (blank & VESA_VSYNC_SUSPEND)
	    gen_cntl |= 0x8;
    if (blank & VESA_HSYNC_SUSPEND)
	    gen_cntl |= 0x4;
    if ((blank & VESA_POWERDOWN) == VESA_POWERDOWN)
	    gen_cntl |= 0x40;
    if (blank == VESA_NO_BLANKING)
	    gen_cntl &= ~(0x4c);
    aty_st_8(CRTC_GEN_CNTL, gen_cntl);
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int atyfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
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

static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
    int i, scale;

    if (regno > 255)
	return 1;
    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;
    aty_WaitQueue(2);
    i = aty_ld_8(DAC_CNTL) & 0xfc;
    if (chip_type == MACH64_GT_ID)
	    i |= 0x2;	/*DAC_CNTL|0x2 turns off the extra brightness for gt*/
    aty_st_8(DAC_CNTL, i);
    aty_st_8(DAC_REGS + DAC_MASK, 0xff);
    eieio();
    scale = ((chip_type != MACH64_GX_ID) &&
	     (current_par.cmode == CMODE_16)) ? 3 : 0;
    aty_WaitQueue(4);
    aty_cmap_regs->windex = regno << scale;
    eieio();
    aty_cmap_regs->lut = red << scale;
    eieio();
    aty_cmap_regs->lut = green << scale;
    eieio();
    aty_cmap_regs->lut = blue << scale;
    eieio();
    if (regno < 16) {
#ifdef CONFIG_FBCON_CFB16
	fbcon_cfb16_cmap[regno] = (regno << 10) | (regno << 5) | regno;
#endif
#ifdef CONFIG_FBCON_CFB32
	fbcon_cfb32_cmap[regno] = (regno << 24) | (regno << 16) |
				  (regno << 8) | regno;
#endif
    }
    return 0;
}


static void do_install_cmap(int con, struct fb_info *info)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
		    atyfb_setcolreg, info);
    else
	fb_set_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
				    &fb_display[con].var, 1, atyfb_setcolreg,
		    info);
}


    /*
     *  Accelerated functions
     */

void aty_waitblit(void)
{
    aty_WaitIdleEmpty();	/* Make sure that all commands have finished */
}

void aty_rectcopy(int srcx, int srcy, int dstx, int dsty, u_int width,
		  u_int height)
{
    u_int direction = 0;

    if (srcy < dsty) {
	dsty += height - 1;
	srcy += height - 1;
    } else
	direction |= DST_Y_TOP_TO_BOTTOM;

    if (srcx < dstx) {
	dstx += width - 1;
	srcx += width - 1;
    } else
	direction |= DST_X_LEFT_TO_RIGHT;

    aty_WaitQueue(4);
    aty_st_le32(DP_WRITE_MSK, 0x000000FF /* pGC->planemask */ );
    aty_st_le32(DP_MIX, (MIX_SRC << 16) |  MIX_DST);
    aty_st_le32(DP_SRC, FRGD_SRC_BLIT);

    aty_WaitQueue(5);
    aty_st_le32(SRC_Y_X, (srcx << 16) | (srcy & 0x0000ffff));
    aty_st_le32(SRC_WIDTH1, width);
    aty_st_le32(DST_CNTL, direction);
    aty_st_le32(DST_Y_X, (dstx << 16) | (dsty & 0x0000ffff));
    aty_st_le32(DST_HEIGHT_WIDTH, (width << 16) | (height & 0x0000ffff));

    aty_WaitIdleEmpty();	/* Make sure that all commands have finished */

    /*
     * Make sure that the destination trajectory is correctly set
     * for subsequent calls.  MACH64_BIT_BLT is the only function that
     * currently changes the destination trajectory from L->R and T->B.
     */
    aty_st_le32(DST_CNTL, (DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM));
}

void aty_rectfill(int dstx, int dsty, u_int width, u_int height, u_int color)
{
    if (!width || !height)
	return;

    aty_WaitQueue(5);
    aty_st_le32(DP_FRGD_CLR, color /* pGC->fgPixel */ );
    aty_st_le32(DP_WRITE_MSK, 0x000000FF /* pGC->planemask */ );
    aty_st_le32(DP_MIX, (MIX_SRC << 16) | MIX_DST);
    aty_st_le32(DP_SRC, FRGD_SRC_FRGD_CLR);

    aty_st_le32(DST_CNTL, DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM);

    aty_WaitQueue(2);
    aty_st_le32(DST_Y_X, (((u_int)dstx << 16) | ((u_int)dsty & 0x0000ffff)));
    aty_st_le32(DST_HEIGHT_WIDTH, (((u_int)width << 16) | height));

    aty_WaitIdleEmpty();	/* Make sure that all commands have finished */
}


    /*
     *  Text console acceleration
     */

static void fbcon_aty8_bmove(struct display *p, int sy, int sx, int dy, int dx,
			     int height, int width)
{
    sx *= p->fontwidth;
    sy *= p->fontheight;
    dx *= p->fontwidth;
    dy *= p->fontheight;
    width *= p->fontwidth;
    height *= p->fontheight;

    aty_rectcopy(sx, sy, dx, dy, width, height);
}

static void fbcon_aty8_clear(struct vc_data *conp, struct display *p, int sy,
			     int sx, int height, int width)
{
    u32 bgx = attr_bgcol_ec(p, conp);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);

    sx *= p->fontwidth;
    sy *= p->fontheight;
    width *= p->fontwidth;
    height *= p->fontheight;

    aty_rectfill(sx, sy, width, height, bgx);
}

static void fbcon_aty8_putc(struct vc_data *conp, struct display *p, int c,
			    int yy, int xx)
{
    aty_waitblit();
    fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_aty8_putcs(struct vc_data *conp, struct display *p,
			     const char *s, int count, int yy, int xx)
{
    aty_waitblit();
    fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static struct display_switch fbcon_aty8 = {
    fbcon_cfb8_setup, fbcon_aty8_bmove, fbcon_aty8_clear, fbcon_aty8_putc,
    fbcon_aty8_putcs, fbcon_cfb8_revc
};


#ifdef CONFIG_FB_COMPAT_XPMAC

    /*
     *  Backward compatibility mode for Xpmac
     */

static int atyfb_console_setmode(struct vc_mode *mode, int doit)
{
    int err;
    struct fb_var_screeninfo var;
    struct atyfb_par par;
    int vmode, cmode;

    if (mode->mode <= 0 || mode->mode > VMODE_MAX )
	return -EINVAL;
    vmode = mode->mode;

    switch (mode->depth) {
	case 24:
	case 32:
	    cmode = CMODE_32;
	    break;
	case 16:
	    cmode = CMODE_16;
	    break;
	case 8:
	case 0:			/* (default) */
	    cmode = CMODE_8;
	    break;
	default:
	    return -EINVAL;
    }
    init_par(&par, vmode, cmode);
    encode_var(&var, &par);
    if ((err = decode_var(&var, &par)))
	return err;
    if (doit)
	atyfb_set_var(&var, currcon, 0);
    return 0;
}

#endif /* CONFIG_FB_COMPAT_XPMAC */
