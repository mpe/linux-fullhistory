/*
 *  linux/drivers/video/atyfb.c -- Frame buffer device for ATI Mach64
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *	Copyright (C) 1998 Bernd Harries
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

/******************************************************************************

  TODO:

    - support arbitrary video modes

******************************************************************************/

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
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#include <asm/io.h>
#if defined(CONFIG_PMAC) || defined(CONFIG_CHRP)
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif

#include "aty.h"
#include "fbcon.h"
#include "fbcon-cfb8.h"
#include "fbcon-cfb16.h"
#include "fbcon-cfb32.h"


#ifndef __powerpc__
#define eieio()		/* Enforce In-order Execution of I/O */
#endif

static int currcon = 0;
static struct display fb_disp;

static char atyfb_name[16] = "ATY Mach64";

struct atyfb_par {
    union {
	/* this should contain chipset specific mode information */
	struct {
	    int vmode;
	    int cmode;
	} gx, gt, vt;
    } hw;
    u_int vxres;	/* virtual screen size */
    u_int vyres;
    int xoffset;	/* virtual screen position */
    int yoffset;
    int accel;
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


static int default_vmode = VMODE_NVRAM;
static int default_cmode = CMODE_NVRAM;

#if defined(CONFIG_PMAC) || defined(CONFIG_CHRP)
/*
 * Addresses in NVRAM where video mode and pixel size are stored.
 */
#define NV_VMODE	0x140f
#define NV_CMODE	0x1410
#endif /* CONFIG_PMAC || CONFIG_CHRP */


/*
 * Horizontal and vertical resolution for each mode.
 */
static struct vmode_attr {
	int	hres;
	int	vres;
	int	vfreq;
	int	interlaced;
} vmode_attrs[VMODE_MAX] = {
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
	{0x73f, VMODE_640_480_67},	/* no sense lines connected at all */
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
    u8 windex;
    u8 lut;
    u8 mask;
    u8 rindex;
    u8 cntl;
};

typedef struct aty_regvals {
    u32 offset[3];		/* first pixel address */

    u32 crtc_h_sync_strt_wid[3];	/* depth dependent */
    u32 crtc_gen_cntl[3];
    u32 mem_cntl[3];

    u32 crtc_h_tot_disp;	/* mode dependent */
    u32 crtc_v_tot_disp;
    u32 crtc_v_sync_strt_wid;
    u32 crtc_off_pitch;

    u8 clock_val[2];	/* vals for 20 and 21 */
} aty_regvals;

struct rage_regvals {
    u32 h_total, h_sync_start, h_sync_width;
    u32 v_total, v_sync_start, v_sync_width;
    u32 h_sync_neg, v_sync_neg;
};

struct fb_info_aty {
    struct fb_info fb_info;
    unsigned long ati_regbase_phys;
    unsigned long ati_regbase;
    unsigned long frame_buffer_phys;
    unsigned long frame_buffer;
    u8 chip_class;
    u8 pixclock_lim_8;	/* ps, <= 8 bpp */
    u8 pixclock_lim_hi;	/* ps, > 8 bpp */
    u32 total_vram;
    struct aty_cmap_regs *aty_cmap_regs;
    struct { u8 red, green, blue, pad; } palette[256];
    struct atyfb_par default_par;
    struct atyfb_par current_par;
};

#ifdef CONFIG_ATARI
static unsigned int mach64_count __initdata = 0;
static unsigned long phys_vmembase[FB_MAX] __initdata = { 0, };
static unsigned long phys_size[FB_MAX] __initdata = { 0, };
static unsigned long phys_guiregbase[FB_MAX] __initdata = { 0, };
#endif

static int aty_vram_reqd(const struct atyfb_par *par);
static struct aty_regvals *get_aty_struct(int vmode, struct fb_info_aty *info);

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


#define CLASS_GX	1
#define CLASS_CT	2
#define CLASS_VT	3
#define CLASS_GT	4

struct aty_features {
    u16 pci_id;
    u16 chip_type;
    const char *name;
    u8 chip_class;
    u8 pixclock_lim_8;	/* MHz, <= 8 bpp (not sure about these limits!) */
    u8 pixclock_lim_hi;	/* MHz, > 8 bpp (not sure about these limits!) */
} aty_features[] __initdata = {
    /* mach64GX family */
    { 0x4758, 0x00d7, "mach64GX (ATI888GX00)", CLASS_GX, 135, 80 },
    { 0x4358, 0x0057, "mach64CX (ATI888CX00)", CLASS_GX, 135, 80 },

    /* mach64CT family */
    { 0x4354, 0x4354, "mach64CT (ATI264CT)", CLASS_CT, 135, 80 },
    { 0x4554, 0x4554, "mach64ET (ATI264ET)", CLASS_CT, 135, 80 },

    /* mach64CT family / mach64VT class */
    { 0x5654, 0x5654, "mach64VT (ATI264VT)", CLASS_VT, 160, 135 },
    { 0x5655, 0x5655, "mach64VTB (ATI264VTB)", CLASS_VT, 160, 135 },
    { 0x5656, 0x5656, "mach64VT4 (ATI264VT4)", CLASS_VT, 160, 135 },

    /* mach64CT family / mach64GT (3D RAGE) class */
    { 0x4742, 0x4742, "3D RAGE PRO (BGA, AGP)", CLASS_GT, 240, 240 },
    { 0x4744, 0x4744, "3D RAGE PRO (BGA, AGP, 1x only)", CLASS_GT, 240, 240 },
    { 0x4749, 0x4749, "3D RAGE PRO (BGA, PCI)", CLASS_GT, 240, 240 },
    { 0x4750, 0x4750, "3D RAGE PRO (PQFP, PCI)", CLASS_GT, 240, 240 },
    { 0x4751, 0x4751, "3D RAGE PRO (PQFP, PCI, limited 3D)", CLASS_GT, 240, 240 },
    { 0x4754, 0x4754, "3D RAGE (GT)", CLASS_GT, 200, 200 },
    { 0x4755, 0x4755, "3D RAGE II+ (GTB)", CLASS_GT, 200, 200 },
    { 0x4756, 0x4756, "3D RAGE IIC", CLASS_GT, 200, 200 },
    { 0x4c47, 0x4c47, "3D RAGE LT", CLASS_GT, 200, 200 },
};


    /*
     *  Interface used by the world
     */

void atyfb_init(void);
#ifdef CONFIG_FB_OF
void atyfb_of_init(struct device_node *dp);
#endif
void atyfb_setup(char *options, int *ints);

static int atyfb_open(struct fb_info *info, int user);
static int atyfb_release(struct fb_info *info, int user);
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
#ifdef CONFIG_FBCON_CFB16
static struct display_switch fbcon_aty16;
#endif
#ifdef CONFIG_FBCON_CFB32
static struct display_switch fbcon_aty32;
#endif


#ifdef CONFIG_FB_COMPAT_XPMAC
extern struct vc_mode display_info;
extern struct fb_info *console_fb_info;
extern int (*console_setmode_ptr)(struct vc_mode *, int);
extern int (*console_set_cmap_ptr)(struct fb_cmap *, int, int,
				   struct fb_info *);
static int atyfb_console_setmode(struct vc_mode *, int);
#endif /* CONFIG_FB_COMPAT_XPMAC */


    /*
     *  Internal routines
     */

static int aty_init(struct fb_info_aty *info, const char *name);
#ifndef CONFIG_FB_OF
static int store_video_par(char *videopar, unsigned char m64_num);
static char *strtoke(char *s, const char *ct);
#endif

static int atyfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			 u_int *transp, struct fb_info *info);
static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);


static struct fb_ops atyfb_ops = {
    atyfb_open, atyfb_release, atyfb_get_fix, atyfb_get_var, atyfb_set_var,
    atyfb_get_cmap, atyfb_set_cmap, atyfb_pan_display, atyfb_ioctl
};


static inline int aty_vram_reqd(const struct atyfb_par *par)
{
    return (par->vxres*par->vyres) << par->hw.gx.cmode;
}

static inline u32 aty_ld_le32(volatile unsigned int regindex,
			      struct fb_info_aty *info)
{
    unsigned long temp;
    u32 val;

#ifdef __powerpc__
    temp = info->ati_regbase;
    asm("lwbrx %0,%1,%2": "=r"(val):"r"(regindex), "r"(temp));
#else
    temp = info->ati_regbase+regindex;
    val = le32_to_cpu(*((volatile u32 *)(temp)));
#endif
    return val;
}

static inline void aty_st_le32(volatile unsigned int regindex, u32 val,
			       struct fb_info_aty *info)
{
    unsigned long temp;

#ifdef __powerpc__
    temp = info->ati_regbase;
    asm("stwbrx %0,%1,%2": : "r"(val), "r"(regindex), "r"(temp):"memory");
#else
    temp = info->ati_regbase+regindex;
    *((volatile u32 *)(temp)) = cpu_to_le32(val);
#endif
}

static inline u8 aty_ld_8(volatile unsigned int regindex,
			  struct fb_info_aty *info)
{
    return *(volatile u8 *)(info->ati_regbase+regindex);
}

static inline void aty_st_8(volatile unsigned int regindex, u8 val,
			    struct fb_info_aty *info)
{
    *(volatile u8 *)(info->ati_regbase+regindex) = val;
}

    /*
     *  All writes to draw engine registers are automatically routed through a
     *  32-bit-wide, 16-entry-deep command FIFO ...
     *  Register writes to registers with DWORD offsets less than 40h are not
     *  FIFOed.
     *  (from Chapter 5 of the Mach64 Programmer's Guide)
     */

static inline void wait_for_fifo(u16 entries, struct fb_info_aty *info)
{
    while ((aty_ld_le32(FIFO_STAT, info) & 0xffff) >
	   ((u32)(0x8000 >> entries)));
}

static inline void wait_for_idle(struct fb_info_aty *info)
{
    wait_for_fifo(16, info);
    while ((aty_ld_le32(GUI_STAT, info) & 1)!= 0);
}

static void reset_engine(struct fb_info_aty *info)
{
    /* reset engine */
    aty_st_le32(GEN_TEST_CNTL,
		aty_ld_le32(GEN_TEST_CNTL, info) & ~GUI_ENGINE_ENABLE, info);
    /* enable engine */
    aty_st_le32(GEN_TEST_CNTL,
		aty_ld_le32(GEN_TEST_CNTL, info) | GUI_ENGINE_ENABLE, info);
    /* ensure engine is not locked up by clearing any FIFO or */
    /* HOST errors */
    aty_st_le32(BUS_CNTL, aty_ld_le32(BUS_CNTL, info) | BUS_HOST_ERR_ACK |
			  BUS_FIFO_ERR_ACK, info);
}

static void init_engine(const struct atyfb_par *par, struct fb_info_aty *info)
{
    u32 pitch_value;

    /* determine modal information from global mode structure */
    pitch_value = par->vxres;

#if 0
    if (par->hw.gx.cmode == CMODE_24) {
	/* In 24 bpp, the engine is in 8 bpp - this requires that all */
	/* horizontal coordinates and widths must be adjusted */
	pitch_value = pitch_value * 3;
    }
#endif

    /* Reset engine, enable, and clear any engine errors */
    reset_engine(info);
    /* Ensure that vga page pointers are set to zero - the upper */
    /* page pointers are set to 1 to handle overflows in the */
    /* lower page */
    aty_st_le32(MEM_VGA_WP_SEL, 0x00010000, info);
    aty_st_le32(MEM_VGA_RP_SEL, 0x00010000, info);

    /* ---- Setup standard engine context ---- */

    /* All GUI registers here are FIFOed - therefore, wait for */
    /* the appropriate number of empty FIFO entries */
    wait_for_fifo(14, info);

    /* enable all registers to be loaded for context loads */
    aty_st_le32(CONTEXT_MASK, 0xFFFFFFFF, info);

    /* set destination pitch to modal pitch, set offset to zero */
    aty_st_le32(DST_OFF_PITCH, (pitch_value / 8) << 22, info);

    /* zero these registers (set them to a known state) */
    aty_st_le32(DST_Y_X, 0, info);
    aty_st_le32(DST_HEIGHT, 0, info);
    aty_st_le32(DST_BRES_ERR, 0, info);
    aty_st_le32(DST_BRES_INC, 0, info);
    aty_st_le32(DST_BRES_DEC, 0, info);

    /* set destination drawing attributes */
    aty_st_le32(DST_CNTL, DST_LAST_PEL | DST_Y_TOP_TO_BOTTOM |
			  DST_X_LEFT_TO_RIGHT, info);

    /* set source pitch to modal pitch, set offset to zero */
    aty_st_le32(SRC_OFF_PITCH, (pitch_value / 8) << 22, info);

    /* set these registers to a known state */
    aty_st_le32(SRC_Y_X, 0, info);
    aty_st_le32(SRC_HEIGHT1_WIDTH1, 1, info);
    aty_st_le32(SRC_Y_X_START, 0, info);
    aty_st_le32(SRC_HEIGHT2_WIDTH2, 1, info);

    /* set source pixel retrieving attributes */
    aty_st_le32(SRC_CNTL, SRC_LINE_X_LEFT_TO_RIGHT, info);

    /* set host attributes */
    wait_for_fifo(13, info);
    aty_st_le32(HOST_CNTL, 0, info);

    /* set pattern attributes */
    aty_st_le32(PAT_REG0, 0, info);
    aty_st_le32(PAT_REG1, 0, info);
    aty_st_le32(PAT_CNTL, 0, info);

    /* set scissors to modal size */
    aty_st_le32(SC_LEFT, 0, info);
    aty_st_le32(SC_TOP, 0, info);
    aty_st_le32(SC_BOTTOM, par->vyres-1, info);
    aty_st_le32(SC_RIGHT, pitch_value-1, info);

    /* set background color to minimum value (usually BLACK) */
    aty_st_le32(DP_BKGD_CLR, 0, info);

    /* set foreground color to maximum value (usually WHITE) */
    aty_st_le32(DP_FRGD_CLR, 0xFFFFFFFF, info);

    /* set write mask to effect all pixel bits */
    aty_st_le32(DP_WRITE_MASK, 0xFFFFFFFF, info);

    /* set foreground mix to overpaint and background mix to */
    /* no-effect */
    aty_st_le32(DP_MIX, FRGD_MIX_S | BKGD_MIX_D, info);

    /* set primary source pixel channel to foreground color */
    /* register */
    aty_st_le32(DP_SRC, FRGD_SRC_FRGD_CLR, info);

    /* set compare functionality to false (no-effect on */
    /* destination) */
    wait_for_fifo(3, info);
    aty_st_le32(CLR_CMP_CLR, 0, info);
    aty_st_le32(CLR_CMP_MASK, 0xFFFFFFFF, info);
    aty_st_le32(CLR_CMP_CNTL, 0, info);

    /* set pixel depth */
    wait_for_fifo(2, info);
    switch(par->hw.gx.cmode) {
#ifdef CONFIG_FBCON_CFB8
	case CMODE_8:
	    aty_st_le32(DP_PIX_WIDTH, HOST_8BPP | SRC_8BPP | DST_8BPP |
				      BYTE_ORDER_LSB_TO_MSB,
	    info);
	    aty_st_le32(DP_CHAIN_MASK, 0x8080, info);
	    break;
#endif
#ifdef CONFIG_FBCON_CFB16
	case CMODE_16:
	    aty_st_le32(DP_PIX_WIDTH, HOST_15BPP | SRC_15BPP | DST_15BPP |
				      BYTE_ORDER_LSB_TO_MSB,
	    info);
	    aty_st_le32(DP_CHAIN_MASK, 0x4210, info);
	    break;
#endif
#if 0
	case CMODE_24:
	    aty_st_le32(DP_PIX_WIDTH, HOST_8BPP | SRC_8BPP | DST_8BPP |
				      BYTE_ORDER_LSB_TO_MSB,
	    info);
	    aty_st_le32(DP_CHAIN_MASK, 0x8080, info);
	    break;
#endif
#ifdef CONFIG_FBCON_CFB32
	case CMODE_32:
	    aty_st_le32(DP_PIX_WIDTH, HOST_32BPP | SRC_32BPP | DST_32BPP |
				      BYTE_ORDER_LSB_TO_MSB, info);
	    aty_st_le32(DP_CHAIN_MASK, 0x8080, info);
	    break;
#endif
    }
    /* insure engine is idle before leaving */
    wait_for_idle(info);
}

static void aty_st_514(int offset, u8 val, struct fb_info_aty *info)
{
    aty_st_8(DAC_CNTL, 1, info);
    /* right addr byte */
    aty_st_8(DAC_W_INDEX, offset & 0xff, info);	
    /* left addr byte */
    aty_st_8(DAC_DATA, (offset >> 8) & 0xff, info);
    eieio();
    aty_st_8(DAC_MASK, val, info);
    eieio();
    aty_st_8(DAC_CNTL, 0, info);
}

static void aty_st_pll(int offset, u8 val, struct fb_info_aty *info)
{
    /* write addr byte */
    aty_st_8(CLOCK_CNTL + 1, (offset << 2) | PLL_WR_EN, info);
    eieio();
    /* write the register value */
    aty_st_8(CLOCK_CNTL + 2, val, info);
    eieio();
    aty_st_8(CLOCK_CNTL + 1, (offset << 2) & ~PLL_WR_EN, info);
}

static struct aty_regvals *get_aty_struct(int vmode, struct fb_info_aty *info)
{
    int v = vmode - 1;

    switch (info->chip_class) {
	case CLASS_GX:
	    return aty_gx_reg_init[v];
	    break;
	case CLASS_CT:
	case CLASS_VT:
	    return aty_vt_reg_init[v];
	    break;
	case CLASS_GT:
	    return aty_gt_reg_init[v];
	    break;
	default:
	    /* should NOT happen */
	    return NULL;
    }
}

static int read_aty_sense(struct fb_info_aty *info)
{
    int sense, i;

    aty_st_le32(GP_IO, 0x31003100, info);	/* drive outputs high */
    __delay(200);
    aty_st_le32(GP_IO, 0, info);		/* turn off outputs */
    __delay(2000);
    i = aty_ld_le32(GP_IO, info);		/* get primary sense value */
    sense = ((i & 0x3000) >> 3) | (i & 0x100);

    /* drive each sense line low in turn and collect the other 2 */
    aty_st_le32(GP_IO, 0x20000000, info);	/* drive A low */
    __delay(2000);
    i = aty_ld_le32(GP_IO, info);
    sense |= ((i & 0x1000) >> 7) | ((i & 0x100) >> 4);
    aty_st_le32(GP_IO, 0x20002000, info);	/* drive A high again */
    __delay(200);

    aty_st_le32(GP_IO, 0x10000000, info);	/* drive B low */
    __delay(2000);
    i = aty_ld_le32(GP_IO, info);
    sense |= ((i & 0x2000) >> 10) | ((i & 0x100) >> 6);
    aty_st_le32(GP_IO, 0x10001000, info);	/* drive B high again */
    __delay(200);

    aty_st_le32(GP_IO, 0x01000000, info);	/* drive C low */
    __delay(2000);
    sense |= (aty_ld_le32(GP_IO, info) & 0x3000) >> 12;
    aty_st_le32(GP_IO, 0, info);		/* turn off outputs */

    return sense;
}

static void RGB514_Program(int cmode, struct fb_info_aty *info)
{
    typedef struct {
	u8 pixel_dly;
	u8 misc2_cntl;
	u8 pixel_rep;
	u8 pixel_cntl_index;
	u8 pixel_cntl_v1;
    } RGB514_DAC_Table;

    static RGB514_DAC_Table RGB514DAC_Tab[8] = {
	{0, 0x41, 0x03, 0x71, 0x45},	/* 8bpp */
	{0, 0x45, 0x04, 0x0c, 0x01},	/* 555 */
	{0, 0x45, 0x06, 0x0e, 0x00},	/* XRGB */
    };
    RGB514_DAC_Table *pDacProgTab;

    pDacProgTab = &RGB514DAC_Tab[cmode];

    aty_st_514(0x90, 0x00, info);
    aty_st_514(0x04, pDacProgTab->pixel_dly, info);
    aty_st_514(0x05, 0x00, info);

    aty_st_514(0x2, 0x1, info);
    aty_st_514(0x71, pDacProgTab->misc2_cntl, info);
    aty_st_514(0x0a, pDacProgTab->pixel_rep, info);

    aty_st_514(pDacProgTab->pixel_cntl_index, pDacProgTab->pixel_cntl_v1,
	       info);
}

static void set_off_pitch(const struct atyfb_par *par,
			  struct fb_info_aty *info)
{
    u32 pitch, offset;

    pitch = par->vxres>>3;
    offset = ((par->yoffset*par->vxres+par->xoffset)>>3)<<par->hw.gx.cmode;
    aty_st_le32(CRTC_OFF_PITCH, pitch<<22 | offset, info);
}

static void atyfb_set_par(struct atyfb_par *par, struct fb_info_aty *info)
{
    int i, j, hres;
    struct aty_regvals *init = get_aty_struct(par->hw.gx.vmode, info);
    int vram_type = aty_ld_le32(CONFIG_STAT0, info) & 7;

    if (init == 0)	/* paranoia, shouldn't get here */
	panic("aty: display mode %d not supported", par->hw.gx.vmode);

    info->current_par = *par;
    hres = vmode_attrs[par->hw.gx.vmode-1].hres;

    if (info->chip_class != CLASS_GT) {
	i = aty_ld_le32(CRTC_GEN_CNTL, info);
	aty_st_le32(CRTC_GEN_CNTL, i | CRTC_EXT_DISP_EN, info);
    }

    if (info->chip_class == CLASS_GX) {
	i = aty_ld_le32(GEN_TEST_CNTL, info);
	aty_st_le32(GEN_TEST_CNTL, i | GEN_OVR_OUTPUT_EN, info);
    }

    switch (info->chip_class) {
	case CLASS_GX:
	    RGB514_Program(par->hw.gx.cmode, info);
	    wait_for_idle(info);
	    aty_st_514(0x06, 0x02, info);
	    aty_st_514(0x10, 0x01, info);
	    aty_st_514(0x70, 0x01, info);
	    aty_st_514(0x8f, 0x1f, info);
	    aty_st_514(0x03, 0x00, info);
	    aty_st_514(0x05, 0x00, info);
	    aty_st_514(0x20, init->clock_val[0], info);
	    aty_st_514(0x21, init->clock_val[1], info);
	    break;
	case CLASS_CT:
	case CLASS_VT:
	    aty_st_pll(VPLL_CNTL, 0xb5, info);
	    aty_st_pll(PLL_REF_DIV, 0x2d, info);
	    aty_st_pll(PLL_GEN_CNTL, 0x14, info);
	    aty_st_pll(MCLK_FB_DIV, 0xbd, info);
	    aty_st_pll(PLL_VCLK_CNTL, 0x0b, info);
	    aty_st_pll(VCLK_POST_DIV, init->clock_val[0], info);
	    aty_st_pll(VCLK0_FB_DIV, init->clock_val[1], info);
	    aty_st_pll(VCLK1_FB_DIV, 0xd6, info);
	    aty_st_pll(VCLK2_FB_DIV, 0xee, info);
	    aty_st_pll(VCLK3_FB_DIV, 0xf8, info);
	    aty_st_pll(PLL_EXT_CNTL, 0x0, info);
	    aty_st_pll(PLL_TEST_CTRL, 0x0, info);
	    aty_st_pll(PLL_TEST_COUNT, 0x0, info);
	    break;
	case CLASS_GT:
	    if (vram_type == 5) {
		aty_st_pll(MPLL_CNTL, 0xcd, info);
		aty_st_pll(VPLL_CNTL,
			   par->hw.gx.vmode >= VMODE_1024_768_60 ? 0xd3
								 : 0xd5,
			   info);
		aty_st_pll(PLL_REF_DIV, 0x21, info);
		aty_st_pll(PLL_GEN_CNTL, 0x44, info);
		aty_st_pll(MCLK_FB_DIV, 0xe8, info);
		aty_st_pll(PLL_VCLK_CNTL, 0x03, info);
		aty_st_pll(VCLK_POST_DIV, init->offset[0], info);
		aty_st_pll(VCLK0_FB_DIV, init->offset[1], info);
		aty_st_pll(VCLK1_FB_DIV, 0x8e, info);
		aty_st_pll(VCLK2_FB_DIV, 0x9e, info);
		aty_st_pll(VCLK3_FB_DIV, 0xc6, info);
		aty_st_pll(PLL_EXT_CNTL, init->offset[2], info);
		aty_st_pll(DLL_CNTL, 0xa6, info);
		aty_st_pll(VFC_CNTL, 0x1b, info);
	    } else {
		aty_st_pll(VPLL_CNTL, 0xd5, info);
		aty_st_pll(PLL_REF_DIV, 0x21, info);
		aty_st_pll(PLL_GEN_CNTL, 0xc4, info);
		aty_st_pll(MCLK_FB_DIV, 0xda, info);
		aty_st_pll(PLL_VCLK_CNTL, 0x03, info);
		/* offset actually holds clock values */
		aty_st_pll(VCLK_POST_DIV, init->offset[0], info);
		aty_st_pll(VCLK0_FB_DIV, init->offset[1], info);
		aty_st_pll(VCLK1_FB_DIV, 0x8e, info);
		aty_st_pll(VCLK2_FB_DIV, 0x9e, info);
		aty_st_pll(VCLK3_FB_DIV, 0xc6, info);
		aty_st_pll(PLL_TEST_CTRL, 0x0, info);
		aty_st_pll(PLL_EXT_CNTL, init->offset[2], info);
		aty_st_pll(DLL_CNTL, 0xa0, info);
		aty_st_pll(VFC_CNTL, 0x1b, info);
	    }
	    break;
    }

    aty_ld_8(DAC_REGS, info);	/* clear counter */
    wait_for_idle(info);

    aty_st_le32(CRTC_H_TOTAL_DISP, init->crtc_h_tot_disp, info);
    aty_st_le32(CRTC_H_SYNC_STRT_WID,
		init->crtc_h_sync_strt_wid[par->hw.gx.cmode], info);
    aty_st_le32(CRTC_V_TOTAL_DISP, init->crtc_v_tot_disp, info);
    aty_st_le32(CRTC_V_SYNC_STRT_WID, init->crtc_v_sync_strt_wid, info);

    aty_st_8(CLOCK_CNTL, 0, info);
    aty_st_8(CLOCK_CNTL, CLOCK_STROBE, info);

    aty_st_le32(CRTC_VLINE_CRNT_VLINE, 0, info);

    set_off_pitch(par, info);

    switch (info->chip_class) {
	case CLASS_GX:
	    /* The magic constant below translates into:
	     * 5  = No RDY delay, 1 wait st for mem write, increment during
	     *      burst transfer
	     * 9  = DAC access delayed, 1 wait state for DAC
	     * 0  = Disables interupts for FIFO errors
	     * e  = Allows FIFO to generate 14 wait states before generating
	     *      error
	     * 1  = DAC snooping disabled, ROM disabled
	     * 0  = ROM page at 0 (disabled so doesn't matter)
	     * f  = 15 ROM wait states (disabled so doesn't matter)
	     * f  = 15 BUS wait states (I'm not sure this applies to PCI bus
	     *      types)
	     * at some point it would be good to experiment with bench marks to
	     * if we can gain some speed by fooling with the wait states etc.
	     */
	    aty_st_le32(BUS_CNTL, 0x890e20f1 /* 0x590e10ff */, info);
	    j = 0x47012100;
	    j = 0x47052100;
	    break;

	case CLASS_CT:
	case CLASS_VT:
	    aty_st_le32(BUS_CNTL, 0x680000f9, info);
	    switch (info->total_vram) {
		case 0x00100000:
		    aty_st_le32(MEM_CNTL, vt_mem_cntl[0][par->hw.gx.cmode],
				info);
		    break;
		case 0x00200000:
		    aty_st_le32(MEM_CNTL, vt_mem_cntl[1][par->hw.gx.cmode],
				info);
		    break;
		case 0x00400000:
		    aty_st_le32(MEM_CNTL, vt_mem_cntl[2][par->hw.gx.cmode],
				info);
		    break;
		default:
		    i = aty_ld_le32(MEM_CNTL, info) & 0x000F;
		    aty_st_le32(MEM_CNTL,
				(init->mem_cntl[par->hw.gx.cmode] &
				 0xFFFFFFF0) | i,
				info);
	    }
	    j = 0x87010184;
	    break;

	case CLASS_GT:
	    aty_st_le32(BUS_CNTL, 0x7b23a040, info);

	    /* need to set DSP values !! assume sdram */
	    i = init->crtc_gen_cntl[0] - (0x100000 * par->hw.gx.cmode);
	    if ( vram_type == 5 )
		i = init->crtc_gen_cntl[1] - (0x100000 * par->hw.gx.cmode);
	    aty_st_le32(DSP_CONFIG, i, info);

	    i = aty_ld_le32(MEM_CNTL, info) & MEM_SIZE_ALIAS;
	    if ( vram_type == 5 ) {
		i |= ((1 * par->hw.gx.cmode) << 26) | 0x4215b0;
		aty_st_le32(DSP_ON_OFF,
			    sgram_dsp[par->hw.gx.vmode-1][par->hw.gx.cmode],
			    info);

		/* aty_st_le32(CLOCK_CNTL, 8192, info); */
	    } else {
		i |= ((1 * par->hw.gx.cmode) << 26) | 0x300090;
		aty_st_le32(DSP_ON_OFF, init->mem_cntl[par->hw.gx.cmode],
			    info);
	    }
	    aty_st_le32(MEM_CNTL, i, info);
	    aty_st_le32(EXT_MEM_CNTL, 0x5000001, info);

	    /* if (info->total_vram > 0x400000)	
		i |= 0x538; this not been verified on > 4Megs!! */

	    j = 0x86010102;
	    break;
    }

    /* These magic constants (variable j) are harder to figure out
    * on the vt chipset bit 2 set makes the screen brighter
    * and bit 15 makes the screen black! But nothing else
    * seems to matter for the vt DAC_CNTL
    */
    aty_st_le32(DAC_CNTL, j, info);
    aty_st_8(DAC_MASK, 0xff, info);

    switch (par->hw.gx.cmode) {
	case CMODE_16:
	    i = CRTC_PIX_WIDTH_15BPP; break;
	/*case CMODE_24: */
	case CMODE_32:
	    i = CRTC_PIX_WIDTH_32BPP; break;
	case CMODE_8:
	default:
	    i = CRTC_PIX_WIDTH_8BPP; break;
    }

    if (info->chip_class != CLASS_GT) {
	aty_st_le32(CRTC_INT_CNTL, 0x00000002, info);
	aty_st_le32(GEN_TEST_CNTL, GUI_ENGINE_ENABLE | BLOCK_WRITE_ENABLE,
		    info);	/* gui_en block_en */
	i |= init->crtc_gen_cntl[par->hw.gx.cmode];
    }
    /* Gentlemen, start your crtc engine */
    aty_st_le32(CRTC_GEN_CNTL, CRTC_EXT_DISP_EN | CRTC_ENABLE | i, info);

    /* Initialize the graphics engine */
    if (par->accel & FB_ACCELF_TEXT)
	init_engine(par, info);

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (console_fb_info == &info->fb_info) {
	display_info.height = vmode_attrs[par->hw.gx.vmode-1].vres;
	display_info.width = vmode_attrs[par->hw.gx.vmode-1].hres;
	display_info.depth = 8<<par->hw.gx.cmode;
	display_info.pitch = par->vxres<<par->hw.gx.cmode;
	display_info.mode = par->hw.gx.vmode;
	strcpy(display_info.name, atyfb_name);
	display_info.fb_address = info->frame_buffer_phys;
	if (info->chip_class == CLASS_VT)
	    display_info.fb_address += init->offset[par->hw.gx.cmode];
	display_info.cmap_adr_address = info->ati_regbase_phys+0xc0;
	display_info.cmap_data_address = info->ati_regbase_phys+0xc1;
	display_info.disp_reg_address = info->ati_regbase_phys;
    }
#endif /* CONFIG_FB_COMPAT_XPMAC */
}


    /*
     *  Open/Release the frame buffer device
     */

static int atyfb_open(struct fb_info *info, int user)

{
    /*
     *  Nothing, only a usage count for the moment
     */

    MOD_INC_USE_COUNT;
    return(0);
}

static int atyfb_release(struct fb_info *info, int user)
{
    MOD_DEC_USE_COUNT;
    return(0);
}


static int encode_fix(struct fb_fix_screeninfo *fix,
		      const struct atyfb_par *par, struct fb_info_aty *info)
{
    struct aty_regvals *init;

    memset(fix, 0, sizeof(struct fb_fix_screeninfo));

    strcpy(fix->id, atyfb_name);
    init = get_aty_struct(par->hw.gx.vmode, info);
    /*
     *  FIXME: This will cause problems on non-GT chips, because the frame
     *  buffer must be aligned to a page
     */
    fix->smem_start = (char *)info->frame_buffer_phys;
    if (info->chip_class == CLASS_VT)
	fix->smem_start += init->offset[par->hw.gx.cmode];
    fix->smem_len = (u32)info->total_vram;

#ifdef __LITTLE_ENDIAN
    /*
     *  Last page of 8 MB little-endian aperture is MMIO
     *  FIXME: we should use the auxillary aperture instead so we can acces the
     *  full 8 MB of video RAM on 8 MB boards
     */
    if (fix->smem_len > 0x800000-PAGE_SIZE)
	fix->smem_len = 0x800000-PAGE_SIZE;
#endif
    /*
     *  Reg Block 0 (CT-compatible block) is at ati_regbase_phys
     *  Reg Block 1 (multimedia extensions) is at ati_regbase_phys-0x400
     */
    switch (info->chip_class) {
	case CLASS_GX:
	    fix->mmio_start = (char *)info->ati_regbase_phys;
	    fix->mmio_len = 0x400;
	    fix->accel = FB_ACCEL_ATI_MACH64GX;
	    break;
	case CLASS_CT:
	    fix->mmio_start = (char *)info->ati_regbase_phys;
	    fix->mmio_len = 0x400;
	    fix->accel = FB_ACCEL_ATI_MACH64CT;
	    break;
	case CLASS_VT:
	    fix->mmio_start = (char *)(info->ati_regbase_phys-0x400);
	    fix->mmio_len = 0x800;
	    fix->accel = FB_ACCEL_ATI_MACH64VT;
	    break;
	case CLASS_GT:
	    fix->mmio_start = (char *)(info->ati_regbase_phys-0x400);
	    fix->mmio_len = 0x800;
	    fix->accel = FB_ACCEL_ATI_MACH64GT;
	    break;
	default:
	    fix->mmio_start = NULL;
	    fix->mmio_len = 0;
	    fix->accel = 0;
    }
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;
    fix->line_length = par->vxres<<par->hw.gx.cmode;
    fix->visual = par->hw.gx.cmode == CMODE_8 ? FB_VISUAL_PSEUDOCOLOR
					      : FB_VISUAL_TRUECOLOR;
    fix->ywrapstep = 0;
    fix->xpanstep = 8;
    fix->ypanstep = 1;

    return 0;
}


static int decode_var(struct fb_var_screeninfo *var,
		      struct atyfb_par *par, struct fb_info_aty *info)
{
    int xres = var->xres;
    int yres = var->yres;
    int bpp = var->bits_per_pixel;
    struct aty_regvals *init;

    /* This should support more video modes */

    if (xres <= 512 && yres <= 384)
	par->hw.gx.vmode = VMODE_512_384_60;	/* 512x384, 60Hz */
    else if (xres <= 640 && yres <= 480)
	par->hw.gx.vmode = VMODE_640_480_67;	/* 640x480, 67Hz */
    else if (xres <= 640 && yres <= 870)
	par->hw.gx.vmode = VMODE_640_870_75P;	/* 640x870, 75Hz (portrait) */
    else if (xres <= 768 && yres <= 576)
	par->hw.gx.vmode = VMODE_768_576_50I;	/* 768x576, 50Hz (PAL full frame) */
    else if (xres <= 800 && yres <= 600)
	par->hw.gx.vmode = VMODE_800_600_75;	/* 800x600, 75Hz */
    else if (xres <= 832 && yres <= 624)
	par->hw.gx.vmode = VMODE_832_624_75;	/* 832x624, 75Hz */
    else if (xres <= 1024 && yres <= 768)
	par->hw.gx.vmode = VMODE_1024_768_75;	/* 1024x768, 75Hz */
    else if (xres <= 1152 && yres <= 870)
	par->hw.gx.vmode = VMODE_1152_870_75;	/* 1152x870, 75Hz */
    else if (xres <= 1280 && yres <= 960)
	par->hw.gx.vmode = VMODE_1280_960_75;	/* 1280x960, 75Hz */
    else if (xres <= 1280 && yres <= 1024)
	par->hw.gx.vmode = VMODE_1280_1024_75;	/* 1280x1024, 75Hz */
    else
	return -EINVAL;

    xres = vmode_attrs[par->hw.gx.vmode-1].hres;
    yres = vmode_attrs[par->hw.gx.vmode-1].vres;

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
	par->hw.gx.cmode = CMODE_8;
    else if (bpp <= 16)
	par->hw.gx.cmode = CMODE_16;
    else if (bpp <= 32)
	par->hw.gx.cmode = CMODE_32;
    else
	return -EINVAL;

    if (var->accel_flags & FB_ACCELF_TEXT)
	par->accel = FB_ACCELF_TEXT;
    else
	par->accel = 0;

    if (aty_vram_reqd(par) > info->total_vram)
	return -EINVAL;

    /* Check if we know about the wanted video mode */
    init = get_aty_struct(par->hw.gx.vmode, info);
    if (init == NULL || init->crtc_h_sync_strt_wid[par->hw.gx.cmode] == 0 ||
	(info->chip_class != CLASS_GT &&
	 init->crtc_gen_cntl[par->hw.gx.cmode] == 0) ||
	(info->chip_class == CLASS_GT &&
	 (aty_ld_le32(CONFIG_STAT0, info) & 7) == 5 &&
	 init->crtc_gen_cntl[1] == 0))
	return -EINVAL;

#if 0
    if (!fbmon_valid_timings(pixclock, htotal, vtotal, info))
	return -EINVAL;
#endif

    return 0;
}

static int encode_var(struct fb_var_screeninfo *var,
		      const struct atyfb_par *par,
		      struct fb_info_aty *info)
{
    int vmode = par->hw.gx.vmode;
    int cmode = par->hw.gx.cmode;
    struct aty_regvals *init = get_aty_struct(vmode, info);
    u_int h_total, h_disp;
    u_int h_sync_strt, h_sync_dly, h_sync_wid, h_sync_pol;
    u_int v_total, v_disp;
    u_int v_sync_strt, v_sync_wid, v_sync_pol;
    u_int xtalin, vclk;
    u8 pll_ref_div, vclk_fb_div, vclk_post_div, pll_ext_cntl;

    memset(var, 0, sizeof(struct fb_var_screeninfo));
    if (!init)
	return -EINVAL;

    var->xres = vmode_attrs[vmode-1].hres;
    var->yres = vmode_attrs[vmode-1].vres;
    var->xres_virtual = par->vxres;
    var->yres_virtual = par->vyres;
    var->xoffset = par->xoffset;
    var->yoffset = par->yoffset;
    var->grayscale = 0;
    switch (cmode) {
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
    var->vmode = FB_VMODE_NONINTERLACED;
    var->accel_flags = par->accel;

    h_total = (init->crtc_h_tot_disp<<3) & 0xff8;
    h_disp = (init->crtc_h_tot_disp>>13) & 0x7f8;
    h_sync_strt = ((init->crtc_h_sync_strt_wid[cmode]<<3) & 0x7f8) |
		  ((init->crtc_h_sync_strt_wid[cmode]>>1) & 0x800);
    h_sync_dly = (init->crtc_h_sync_strt_wid[cmode]>>8) & 0x7;
    h_sync_wid = (init->crtc_h_sync_strt_wid[cmode]>>13) & 0xf8;
    h_sync_pol = (init->crtc_h_sync_strt_wid[cmode]>>21) & 0x1;

    v_total = init->crtc_v_tot_disp & 0x7ff;
    v_disp = (init->crtc_v_tot_disp>>16) & 0x7ff;
    v_sync_strt = init->crtc_v_sync_strt_wid & 0x7ff;
    v_sync_wid = (init->crtc_v_sync_strt_wid>>16) & 0x1f;
    v_sync_pol = (init->crtc_v_sync_strt_wid>>21) & 0x1;

    var->left_margin = (h_total+8)-h_sync_strt-h_sync_wid;
    var->right_margin = h_sync_strt-(h_disp+8);
    var->upper_margin = (v_total+1)-v_sync_strt-v_sync_wid;
    var->lower_margin = v_sync_strt-(v_disp+1);
    var->hsync_len = h_sync_wid;
    var->vsync_len = v_sync_wid;
    var->sync = (h_sync_pol ? 0 : FB_SYNC_HOR_HIGH_ACT) |
		(v_sync_pol ? 0 : FB_SYNC_VERT_HIGH_ACT);

    xtalin = 69841;	/* 14.31818 MHz */
    switch (info->chip_class) {
	case CLASS_GX:
	    {
		/* haven't read the IBM RGB514 PDF yet, so just guesses */
		static u32 gx_vclk[VMODE_MAX] = {
		        0,	/* vmode  1 */	
		        0,	/* vmode  2 */	
		        0,	/* vmode  3 */	
		        0,	/* vmode  4 */	
		    39722,	/* vmode  5 (25.175 MHz) */
		    33333,	/* vmode  6 (30 MHz) */
		        0,	/* vmode  7 */	
		        0,	/* vmode  8 */	
		    27778,	/* vmode  9 (36 MHz) */
		    25000,	/* vmode 10 (40 MHz) */
		    20000,	/* vmode 11 (50 MHz) */
		    20000,	/* vmode 12 (50 MHz) */
		    17544,	/* vmode 13 (57 MHz) */
		    15385,	/* vmode 14 (65 MHz) */
		    13333,	/* vmode 15 (75 MHz) */
		        0,	/* vmode 16 */	
		    12821,	/* vmode 17 (78 MHz) */
		    10000,	/* vmode 18 (100 MHz) */
		     7937,	/* vmode 19 (126 MHz) */
		     7407	/* vmode 20 (135 MHz) */
		};
		vclk = gx_vclk[vmode-1];
	    }
	    break;
	case CLASS_CT:
	case CLASS_GT:
	case CLASS_VT:
	    if (info->chip_class == CLASS_GT) {
		pll_ref_div = 0x21;
		vclk_post_div = init->offset[0];
		vclk_fb_div = init->offset[1];
		pll_ext_cntl = init->offset[2];
	    } else {
		pll_ref_div = 0x2d;
		vclk_post_div = init->clock_val[0];
		vclk_fb_div = init->clock_val[1];
		pll_ext_cntl = 0x0;
	    }
	    vclk = xtalin*pll_ref_div;
	    switch (vclk_post_div & 3) {
		case 0:
		    vclk *= (pll_ext_cntl & 0x10) ? 3 : 1;
		    break;
		case 1:
		    if (pll_ext_cntl & 0x10)
			return -EINVAL;
		    vclk *= 2;
		    break;
		case 2:
		    vclk *= (pll_ext_cntl & 0x10) ? 6 : 4;
		    break;
		case 3:
		    vclk *= (pll_ext_cntl & 0x10) ? 12 : 8;
		    break;
	    }
	    vclk /= 2*vclk_fb_div;
	    break;
    }
    var->pixclock = vclk;

    return 0;
}


static void init_par(struct atyfb_par *par, int vmode, int cmode)
{
    par->hw.gx.vmode = vmode;
    par->hw.gx.cmode = cmode;
    par->vxres = vmode_attrs[vmode-1].hres;
    par->vyres = vmode_attrs[vmode-1].vres;
    par->xoffset = 0;
    par->yoffset = 0;
    par->accel = FB_ACCELF_TEXT;
}


    /*
     *  Get the Fixed Part of the Display
     */

static int atyfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;
    struct atyfb_par par;

    if (con == -1)
	par = info2->default_par;
    else
	decode_var(&fb_display[con].var, &par, info2);
    encode_fix(fix, &par, info2);
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int atyfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;

    if (con == -1)
	encode_var(var, &info2->default_par, (struct fb_info_aty *)info);
    else
	*var = fb_display[con].var;
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int atyfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;
    struct atyfb_par par;
    struct display *display;
    int oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel, accel, err;
    int activate = var->activate;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = &fb_disp;	/* used during initialization */

    if ((err = decode_var(var, &par, info2)))
	return err;

    encode_var(var, &par, (struct fb_info_aty *)info);

    if ((activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	oldxres = display->var.xres;
	oldyres = display->var.yres;
	oldvxres = display->var.xres_virtual;
	oldvyres = display->var.yres_virtual;
	oldbpp = display->var.bits_per_pixel;
	oldaccel = display->var.accel_flags;
	display->var = *var;
	if (oldxres != var->xres || oldyres != var->yres ||
	    oldvxres != var->xres_virtual || oldvyres != var->yres_virtual ||
	    oldbpp != var->bits_per_pixel || oldaccel != var->accel_flags) {
	    struct fb_fix_screeninfo fix;

	    encode_fix(&fix, &par, info2);
	    display->screen_base = (char *)info2->frame_buffer;
	    display->visual = fix.visual;
	    display->type = fix.type;
	    display->type_aux = fix.type_aux;
	    display->ypanstep = fix.ypanstep;
	    display->ywrapstep = fix.ywrapstep;
	    display->line_length = fix.line_length;
	    display->can_soft_blank = 1;
	    display->inverse = 0;
	    accel = var->accel_flags & FB_ACCELF_TEXT;
	    switch (par.hw.gx.cmode) {
#ifdef CONFIG_FBCON_CFB8
		case CMODE_8:
		    display->dispsw = accel ? &fbcon_aty8 : &fbcon_cfb8;
		    break;
#endif
#ifdef CONFIG_FBCON_CFB16
		case CMODE_16:
		    display->dispsw = accel ? &fbcon_aty16 : &fbcon_cfb16;
		    break;
#endif
#ifdef CONFIG_FBCON_CFB32
		case CMODE_32:
		    display->dispsw = accel ? &fbcon_aty32 : &fbcon_cfb32;
		    break;
#endif
		default:
		    display->dispsw = NULL;
		    break;
	    }
	    if (info->changevar)
		(*info->changevar)(con);
	}
	if (con == currcon)
	    atyfb_set_par(&par, info2);
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
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;
    u32 xres, yres, xoffset, yoffset;
    struct atyfb_par *par = &info2->current_par;

    xres = vmode_attrs[par->hw.gx.vmode-1].hres;
    yres = vmode_attrs[par->hw.gx.vmode-1].vres;
    xoffset = (var->xoffset+7) & ~7;
    yoffset = var->yoffset;
    if (xoffset+xres > par->vxres || yoffset+yres > par->vyres)
	return -EINVAL;
    par->xoffset = xoffset;
    par->yoffset = yoffset;
    set_off_pitch(par, info2);
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

__initfunc(static int aty_init(struct fb_info_aty *info, const char *name))
{
    u32 chip_id;
    u32 i;
    int j, k, sense;
    struct fb_var_screeninfo var;
    struct aty_regvals *init;
    const char *chipname = NULL;
    u8 rev;

    info->aty_cmap_regs = (struct aty_cmap_regs *)(info->ati_regbase+0xc0);
    chip_id = aty_ld_le32(CONFIG_CHIP_ID, info);
    for (j = 0; j < (sizeof(aty_features)/sizeof(*aty_features)); j++)
	if (aty_features[j].chip_type == (chip_id & CFG_CHIP_TYPE)) {
	    chipname = aty_features[j].name;
	    info->chip_class = aty_features[j].chip_class;
	    info->pixclock_lim_8 = 1000000/aty_features[j].pixclock_lim_8;
	    info->pixclock_lim_hi = 1000000/aty_features[j].pixclock_lim_hi;
	}
    if (!chipname) {
	printk("atyfb: Unknown Mach64 0x%04x\n", chip_id & CFG_CHIP_TYPE);
	return 0;
    } else
	printk("atyfb: %s [", chipname);
    rev = (chip_id & CFG_CHIP_REV)>>24;
    switch ((rev>>3) & 7) {
	case MACH64_FND_SGS:
	    printk("SGS");
	    break;
	case MACH64_FND_NEC:
	    printk("NEC");
	    break;
	case MACH64_FND_UMC:
	    printk("UMC");
	    break;
    }
    printk(" %c%d]\n", 'A'+(rev & 7), rev>>6);

    i = aty_ld_le32(MEM_CNTL, info);
    if (info->chip_class != CLASS_GT)
	switch (i & MEM_SIZE_ALIAS) {
	    case MEM_SIZE_512K:
		info->total_vram = 0x80000;
		break;
	    case MEM_SIZE_1M:
		info->total_vram = 0x100000;
		break;
	    case MEM_SIZE_2M:
		info->total_vram = 0x200000;
		break;
	    case MEM_SIZE_4M:
		info->total_vram = 0x400000;
		break;
	    case MEM_SIZE_6M:
		info->total_vram = 0x600000;
		break;
	    case MEM_SIZE_8M:
		info->total_vram = 0x800000;
		break;
	    default:
		info->total_vram = 0x80000;
	}
    else
	switch (i & 0xF) {	/* 0xF used instead of MEM_SIZE_ALIAS */
	    case MEM_SIZE_512K:
		info->total_vram = 0x80000;
		break;
	    case MEM_SIZE_1M:
		info->total_vram = 0x100000;
		break;
	    case MEM_SIZE_2M_GTB:
		info->total_vram = 0x200000;
		break;
	    case MEM_SIZE_4M_GTB:
		info->total_vram = 0x400000;
		break;
	    case MEM_SIZE_6M_GTB:
		info->total_vram = 0x600000;
		break;
	    case MEM_SIZE_8M_GTB:
		info->total_vram = 0x800000;
		break;
	    default:
		info->total_vram = 0x80000;
	}
#ifdef CONFIG_ATARI	/* this is definately not the wrong way to set this */
    if ((info->total_vram == 0x400000) || (info->total_vram == 0x800000)) {
	/* protect GUI-regs if complete Aperture is VRAM */
	info->total_vram -= 0x00001000;
    }
#endif

#if 0
    printk("aty_init: regbase = %lx, frame_buffer = %lx, total_vram = %x\n",
	   info->ati_regbase, info->frame_buffer, info->total_vram);
#endif

    sense = read_aty_sense(info);
    printk("monitor sense = %x\n", sense);
#if defined(CONFIG_PMAC) || defined(CONFIG_CHRP)
    if (default_vmode == VMODE_NVRAM) {
	default_vmode = nvram_read_byte(NV_VMODE);
	init = get_aty_struct(default_vmode, info);
	if (default_vmode <= 0 || default_vmode > VMODE_MAX || init == 0)
	    default_vmode = VMODE_CHOOSE;
    }
    if (default_vmode == VMODE_CHOOSE)
	default_vmode = map_monitor_sense(sense);
#else /* !CONFIG_PMAC && !CONFIG_CHRP */
    if (default_vmode == VMODE_NVRAM)
	default_vmode = map_monitor_sense(sense);
#endif /* !CONFIG_PMAC && !CONFIG_CHRP */

    if (!(init = get_aty_struct(default_vmode, info)))
	default_vmode = VMODE_640_480_60;

    /*
     * Reduce the pixel size if we don't have enough VRAM.
     */

    if (default_cmode == CMODE_NVRAM)
#if defined(CONFIG_PMAC) || defined(CONFIG_CHRP)
	default_cmode = nvram_read_byte(NV_CMODE);
#else /* !CONFIG_PMAC && !CONFIG_CHRP */
	default_cmode = CMODE_8;
#endif /* !CONFIG_PMAC && !CONFIG_CHRP */
    if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
	default_cmode = CMODE_8;

    init_par(&info->default_par, default_vmode, default_cmode);
    while (aty_vram_reqd(&info->default_par) > info->total_vram) {
	while (default_cmode > CMODE_8 &&
	       aty_vram_reqd(&info->default_par) > info->total_vram) {
	    --default_cmode;
	    init_par(&info->default_par, default_vmode, default_cmode);
	}
	/*
	 * Adjust the video mode smaller if there still is not enough VRAM
	 */
	if (aty_vram_reqd(&info->default_par) > info->total_vram)
	    do {
		default_vmode--;
		init_par(&info->default_par, default_vmode, default_cmode);
		init = get_aty_struct(default_vmode, info);
	    } while ((init == 0) &&
		     (default_vmode > VMODE_640_480_60));
    }

    if (info->chip_class == CLASS_GT &&
	(aty_ld_le32(CONFIG_STAT0, info) & 7) == 5
	&& init->crtc_gen_cntl[1] == 0) {
	    default_vmode = VMODE_640_480_67;
	    default_cmode = CMODE_8;
	    init_par(&info->default_par, default_vmode, default_cmode);
    }

    switch (info->chip_class) {
	case CLASS_GX:
	    strcat(atyfb_name, "GX");
	    break;
	case CLASS_CT:
	    strcat(atyfb_name, "CT");
	    break;
	case CLASS_VT:
	    strcat(atyfb_name, "VT");
	    break;
	case CLASS_GT:
	    strcat(atyfb_name, "GT");
	    break;
    }
    strcpy(info->fb_info.modename, atyfb_name);
    info->fb_info.node = -1;
    info->fb_info.fbops = &atyfb_ops;
    info->fb_info.disp = &fb_disp;
    info->fb_info.fontname[0] = '\0';
    info->fb_info.changevar = NULL;
    info->fb_info.switch_con = &atyfbcon_switch;
    info->fb_info.updatevar = &atyfbcon_updatevar;
    info->fb_info.blank = &atyfbcon_blank;

    for (j = 0; j < 16; j++) {
	k = color_table[j];
	info->palette[j].red = default_red[k];
	info->palette[j].green = default_grn[k];
	info->palette[j].blue = default_blu[k];
    }

    atyfb_set_par(&info->default_par, info);
    encode_var(&var, &info->default_par, info);
    atyfb_set_var(&var, -1, &info->fb_info);

    if (register_framebuffer(&info->fb_info) < 0)
	return 0;

    printk("fb%d: %s frame buffer device on %s\n",
	   GET_FB_IDX(info->fb_info.node), atyfb_name, name);
    return 1;
}

__initfunc(void atyfb_init(void))
{
#if defined(CONFIG_FB_OF)
    /* We don't want to be called like this. */
    /* We rely on Open Firmware (offb) instead. */
#elif defined(CONFIG_PCI)
    /* Anyone who wants to do a PCI probe for an ATI chip? */
#elif defined(CONFIG_ATARI)
    int m64_num;
    struct fb_info_aty *info;

    for (m64_num = 0; m64_num < mach64_count; m64_num++) {
	if (!phys_vmembase[m64_num] || !phys_size[m64_num] ||
	    !phys_guiregbase[m64_num]) {
	    printk(" phys_*[%d] parameters not set => returning early. \n",
		   m64_num);
	    continue;
	}

	info = kmalloc(sizeof(struct fb_info_aty), GFP_ATOMIC);

	/*
	 *  Map the video memory (physical address given) to somewhere in the
	 *  kernel address space.
	 */
	info->frame_buffer = kernel_map(phys_vmembase[m64_num],
					phys_size[m64_num],
					KERNELMAP_NOCACHE_SER, NULL);
	info->frame_buffer_phys = info->frame_buffer;
	info->ati_regbase = kernel_map(phys_guiregbase[m64_num], 0x10000,
				       KERNELMAP_NOCACHE_SER, NULL)+0xFC00ul;
	info->ati_regbase_phys = info->ati_regbase;

	if (!aty_init(info, "ISA bus")) {
	    kfree(info);
	    /* This is insufficient! kernel_map has added two large chunks!! */
	    return;
	}
    }
#endif
}

#ifdef CONFIG_FB_OF
__initfunc(void atyfb_of_init(struct device_node *dp))
{
    unsigned long addr;
    u8 bus, devfn;
    u16 cmd;
    struct fb_info_aty *info;
    int i;

    for (; dp; dp = dp->next) {
	switch (dp->n_addrs) {
	    case 1:
	    case 3:
		addr = dp->addrs[0].address;
		break;
	    case 4:
		addr = dp->addrs[1].address;
		break;
	    default:
		printk("Warning: got %d adresses for ATY:\n", dp->n_addrs);
		for (i = 0; i < dp->n_addrs; i++)
		    printk(" %08x-%08x", dp->addrs[i].address,
			   dp->addrs[i].address+dp->addrs[i].size-1);
		if (dp->n_addrs)
		    printk("\n");
		continue;
	}

	info = kmalloc(sizeof(struct fb_info_aty), GFP_ATOMIC);

	info->ati_regbase = (unsigned long)ioremap(0x7ff000+addr,
						   0x1000)+0xc00;
	info->ati_regbase_phys = 0x7ff000+addr;
	info->ati_regbase = (unsigned long)ioremap(info->ati_regbase_phys,
						   0x1000);
	info->ati_regbase_phys += 0xc00;
	info->ati_regbase += 0xc00;

	/* enable memory-space accesses using config-space command register */
	if (pci_device_loc(dp, &bus, &devfn) == 0) {
	    pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	    if (cmd != 0xffff) {
		cmd |= PCI_COMMAND_MEMORY;
		pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
	    }
	}

#ifdef __BIG_ENDIAN
	/* Use the big-endian aperture */
	addr += 0x800000;
#endif

	/* Map in frame buffer */
	info->frame_buffer_phys = addr;
	info->frame_buffer = (unsigned long)ioremap(addr, 0x800000);

	if (!aty_init(info, dp->full_name)) {
	    kfree(info);
	    return;
	}

#ifdef CONFIG_FB_COMPAT_XPMAC
	if (!console_fb_info) {
	    console_fb_info = &info->fb_info;
	    console_setmode_ptr = atyfb_console_setmode;
	    console_set_cmap_ptr = atyfb_set_cmap;
	}
#endif /* CONFIG_FB_COMPAT_XPMAC */
    }
}
#endif /* CONFIG_FB_OF */


__initfunc(void atyfb_setup(char *options, int *ints))
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
		default_vmode = vmode;
	} else if (!strncmp(this_opt, "cmode:", 6)) {
	    depth = simple_strtoul(this_opt+6, NULL, 0);
	    switch (depth) {
		case 8:
		    default_cmode = CMODE_8;
		    break;
		case 15:
		case 16:
		    default_cmode = CMODE_16;
		    break;
		case 24:
		case 32:
		    default_cmode = CMODE_32;
		    break;
	    }
	}
#ifdef CONFIG_ATARI
	/*
	 * Why do we need this silly Mach64 argument?
	 * We are already here because of mach64= so its redundant.
	 */
	else if (MACH_IS_ATARI && (!strncmp(this_opt, "Mach64:", 7))) {
	    static unsigned char m64_num;
	    static char mach64_str[80];
	    strncpy(mach64_str, this_opt+7, 80);
	    if (!store_video_par(mach64_str, m64_num)) {
		m64_num++;
		mach64_count = m64_num;
	    }
	}
#endif
    }
}

#ifdef CONFIG_ATARI
__initfunc(static int store_video_par(char *video_str, unsigned char m64_num))
{
    char *p;
    unsigned long vmembase, size, guiregbase;

    printk("store_video_par() '%s' \n", video_str);

    if (!(p = strtoke(video_str, ";")) || !*p)
	goto mach64_invalid;
    vmembase = simple_strtoul(p, NULL, 0);
    if (!(p = strtoke(NULL, ";")) || !*p)
	goto mach64_invalid;
    size = simple_strtoul(p, NULL, 0);
    if (!(p = strtoke(NULL, ";")) || !*p)
	goto mach64_invalid;
    guiregbase = simple_strtoul(p, NULL, 0);

    phys_vmembase[m64_num] = vmembase;
    phys_size[m64_num] = size;
    phys_guiregbase[m64_num] = guiregbase;
    printk(" stored them all: $%08lX $%08lX $%08lX \n", vmembase, size,
	   guiregbase);
    return 0;

mach64_invalid:
    phys_vmembase[m64_num]   = 0;
    return -1;
}

__initfunc(static char *strtoke(char *s, const char *ct))
{
    static char *ssave = NULL;
    char *sbegin, *send;

    sbegin  = s ? s : ssave;
    if (!sbegin)
	return NULL;
    if (*sbegin == '\0') {
	ssave = NULL;
	return NULL;
    }
    send = strpbrk(sbegin, ct);
    if (send && *send != '\0')
	*send++ = '\0';
    ssave = send;
    return sbegin;
}
#endif /* !CONFIG_FB_OF */

static int atyfbcon_switch(int con, struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;
    struct atyfb_par par;

    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
		    atyfb_getcolreg, info);
    currcon = con;
    decode_var(&fb_display[con].var, &par, info2);
    atyfb_set_par(&par, info2);
    /* Install new colormap */
    do_install_cmap(con, info);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int atyfbcon_updatevar(int con, struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;

    info2->current_par.yoffset = fb_display[con].var.yoffset;
    set_off_pitch(&info2->current_par, info2);
    return 0;
}

    /*
     *  Blank the display.
     */

static void atyfbcon_blank(int blank, struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;
    u8 gen_cntl;

    gen_cntl = aty_ld_8(CRTC_GEN_CNTL, info2);
    if (blank & VESA_VSYNC_SUSPEND)
	    gen_cntl |= 0x8;
    if (blank & VESA_HSYNC_SUSPEND)
	    gen_cntl |= 0x4;
    if ((blank & VESA_POWERDOWN) == VESA_POWERDOWN)
	    gen_cntl |= 0x40;
    if (blank == VESA_NO_BLANKING)
	    gen_cntl &= ~(0x4c);
    aty_st_8(CRTC_GEN_CNTL, gen_cntl, info2);
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int atyfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;

    if (regno > 255)
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

static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
    struct fb_info_aty *info2 = (struct fb_info_aty *)info;
    int i, scale;

    if (regno > 255)
	return 1;
    info2->palette[regno].red = red;
    info2->palette[regno].green = green;
    info2->palette[regno].blue = blue;
    i = aty_ld_8(DAC_CNTL, info2) & 0xfc;
    if (info2->chip_class == CLASS_GT)
	i |= 0x2;	/*DAC_CNTL|0x2 turns off the extra brightness for gt*/
    aty_st_8(DAC_CNTL, i, info2);
    aty_st_8(DAC_REGS + DAC_MASK, 0xff, info2);
    eieio();
    scale = ((info2->chip_class != CLASS_GX) &&
	     (info2->current_par.hw.gx.cmode == CMODE_16)) ? 3 : 0;
    info2->aty_cmap_regs->windex = regno << scale;
    eieio();
    info2->aty_cmap_regs->lut = red << scale;
    eieio();
    info2->aty_cmap_regs->lut = green << scale;
    eieio();
    info2->aty_cmap_regs->lut = blue << scale;
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

static inline void draw_rect(s16 x, s16 y, u16 width, u16 height,
			     struct fb_info_aty *info)
{
    /* perform rectangle fill */
    wait_for_fifo(2, info);
    aty_st_le32(DST_Y_X, (x << 16) | y, info);
    aty_st_le32(DST_HEIGHT_WIDTH, (width << 16) | height, info);
}

static inline void aty_rectcopy(int srcx, int srcy, int dstx, int dsty,
				u_int width, u_int height,
				struct fb_info_aty *info)
{
    u32 direction = DST_LAST_PEL;
    u32 pitch_value;

    if (!width || !height)
	return;

    pitch_value = info->current_par.vxres;
#if 0
    if (par->hw.gx.cmode == CMODE_24) {
	/* In 24 bpp, the engine is in 8 bpp - this requires that all */
	/* horizontal coordinates and widths must be adjusted */
	pitch_value = pitch_value * 3;
    }
#endif

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

    wait_for_fifo(5, info);
    aty_st_le32(DP_SRC, FRGD_SRC_BLIT, info);
    /*
     * ++Geert:
     * Warning: SRC_OFF_PITCH may be thrashed by writing to other registers
     * (e.g. CRTC_H_TOTAL_DISP, DP_SRC, DP_FRGD_CLR)
     */
    aty_st_le32(SRC_OFF_PITCH, (pitch_value / 8) << 22, info);
    aty_st_le32(SRC_Y_X, (srcx << 16) | srcy, info);
    aty_st_le32(SRC_HEIGHT1_WIDTH1, (width << 16) | height, info);
    aty_st_le32(DST_CNTL, direction, info);
    draw_rect(dstx, dsty, width, height, info);
}

static inline void aty_rectfill(int dstx, int dsty, u_int width, u_int height,
				u_int color, struct fb_info_aty *info)
{
    if (!width || !height)
	return;

    wait_for_fifo(3, info);
    aty_st_le32(DP_FRGD_CLR, color, info);
    aty_st_le32(DP_SRC, BKGD_SRC_BKGD_CLR | FRGD_SRC_FRGD_CLR | MONO_SRC_ONE,
		info);
    aty_st_le32(DST_CNTL, DST_LAST_PEL | DST_Y_TOP_TO_BOTTOM |
			  DST_X_LEFT_TO_RIGHT, info);
    draw_rect(dstx, dsty, width, height, info);
}


    /*
     *  Text console acceleration
     */

static void fbcon_aty_bmove(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width)
{
    sx *= p->fontwidth;
    sy *= p->fontheight;
    dx *= p->fontwidth;
    dy *= p->fontheight;
    width *= p->fontwidth;
    height *= p->fontheight;

    aty_rectcopy(sx, sy, dx, dy, width, height,
		 (struct fb_info_aty *)p->fb_info);
}

static void fbcon_aty_clear(struct vc_data *conp, struct display *p, int sy,
			    int sx, int height, int width)
{
    u32 bgx = attr_bgcol_ec(p, conp);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);

    sx *= p->fontwidth;
    sy *= p->fontheight;
    width *= p->fontwidth;
    height *= p->fontheight;

    aty_rectfill(sx, sy, width, height, bgx,
		 (struct fb_info_aty *)p->fb_info);
}

#ifdef CONFIG_FBCON_CFB8
static void fbcon_aty8_putc(struct vc_data *conp, struct display *p, int c,
			    int yy, int xx)
{
    wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_aty8_putcs(struct vc_data *conp, struct display *p,
			     const unsigned short *s, int count, int yy, int xx)
{
    wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static struct display_switch fbcon_aty8 = {
    fbcon_cfb8_setup, fbcon_aty_bmove, fbcon_aty_clear, fbcon_aty8_putc,
    fbcon_aty8_putcs, fbcon_cfb8_revc, NULL
};
#endif

#ifdef CONFIG_FBCON_CFB16
static void fbcon_aty16_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx)
{
    wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb16_putc(conp, p, c, yy, xx);
}

static void fbcon_aty16_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy, int xx)
{
    wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb16_putcs(conp, p, s, count, yy, xx);
}

static struct display_switch fbcon_aty16 = {
    fbcon_cfb16_setup, fbcon_aty_bmove, fbcon_aty_clear, fbcon_aty16_putc,
    fbcon_aty16_putcs, fbcon_cfb16_revc, NULL
};
#endif

#ifdef CONFIG_FBCON_CFB32
static void fbcon_aty32_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx)
{
    wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb32_putc(conp, p, c, yy, xx);
}

static void fbcon_aty32_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy, int xx)
{
    wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb32_putcs(conp, p, s, count, yy, xx);
}

static struct display_switch fbcon_aty32 = {
    fbcon_cfb32_setup, fbcon_aty_bmove, fbcon_aty_clear, fbcon_aty32_putc,
    fbcon_aty32_putcs, fbcon_cfb32_revc, NULL
};
#endif


#ifdef CONFIG_FB_COMPAT_XPMAC

    /*
     *  Backward compatibility mode for Xpmac
     *
     *  This should move to offb.c once this driver supports arbitrary video
     *  modes
     */

static int atyfb_console_setmode(struct vc_mode *mode, int doit)
{
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
    encode_var(&var, &par, (struct fb_info_aty *)console_fb_info);
    var.activate = doit ? FB_ACTIVATE_NOW : FB_ACTIVATE_TEST;
    return atyfb_set_var(&var, currcon, console_fb_info);
}

#endif /* CONFIG_FB_COMPAT_XPMAC */
