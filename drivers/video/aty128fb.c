/* $Id: aty128fb.c,v 1.1 1999/10/12 11:00:43 geert Exp $
 *  linux/drivers/video/aty128fb.c -- Frame buffer device for ATI Rage128
 *
 *  Copyright (C) Summer 1999, Anthony Tong <atong@uiuc.edu>
 *
 * 				Brad Douglas <brad@neruo.com>
 *				- x86 support
 *				- MTRR
 *				- Probe ROM for PLL
 *
 *  Based off of Geert's atyfb.c and vfb.c.
 *
 *  TODO:
 *		- panning
 *		- fix 15/16 bpp on big endian arch's
 *		- monitor sensing (DDC)
 *		- other platform support (only ppc/x86 supported)
 *		- PPLL_REF_DIV & XTALIN calculation
 *		- determine MCLK from previous hardware setting
 */

/*
 * A special note of gratitude to ATI's devrel for providing documentation,
 * example code and hardware. Thanks Nitya.	-atong
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
#include <linux/selection.h>
#include <linux/pci.h>
#include <asm/io.h>

#if defined(CONFIG_PPC)
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <linux/nvram.h>
#include <video/macmodes.h>
#endif

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "aty128.h"

#undef DEBUG
#undef CONFIG_MTRR	/* not ready? */

#ifdef DEBUG
#define DBG(x)		printk(KERN_INFO "aty128fb: %s\n",(x));
#else
#define DBG(x)
#endif

static char *aty128fb_name = "ATY Rage128";

static struct fb_var_screeninfo default_var = {
    /* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
    640, 480, 640, 480, 0, 0, 8, 0,
    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
    0, 0, -1, -1, 0, 39722, 48, 16, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};

#pragma pack(1)
typedef struct {
    u8 clock_chip_type;
    u8 struct_size;
    u8 accelerator_entry;
    u8 VGA_entry;
    u16 VGA_table_offset;
    u16 POST_table_offset;
    u16 XCLK;
    u16 MCLK;
    u8 num_PLL_blocks;
    u8 size_PLL_blocks;
    u16 PCLK_ref_freq;
    u16 PCLK_ref_divider;
    u32 PCLK_min_freq;
    u32 PCLK_max_freq;
    u16 MCLK_ref_freq;
    u16 MCLK_ref_divider;
    u32 MCLK_min_freq;
    u32 MCLK_max_freq;
    u16 XCLK_ref_freq;
    u16 XCLK_ref_divider;
    u32 XCLK_min_freq;
    u32 XCLK_max_freq;
} PLL_BLOCK;
#pragma pack()

struct aty128_meminfo {
    u8 ML;
    u8 MB;
    u8 Trcd;
    u8 Trp;
    u8 Twr;
    u8 CL;
    u8 Tr2w;
    u8 LoopLatency;
    u8 DspOn;
    u8 Rloop;
};

const struct aty128_meminfo sdr_128   = { 4, 4, 3, 3, 1, 3, 1, 16, 30, 16 };
const struct aty128_meminfo sdr_64    = { 4, 8, 3, 3, 1, 3, 1, 17, 46, 17 };
const struct aty128_meminfo sdr_sgram = { 4, 4, 1, 2, 1, 2, 1, 16, 24, 16 };
const struct aty128_meminfo ddr_sgram = { 4, 4, 3, 3, 2, 3, 1, 16, 31, 16 };

static int currcon = 0;
static char fontname[40] __initdata = { 0 };

#if defined(CONFIG_PPC)
static int default_vmode __initdata = VMODE_NVRAM;
static int default_cmode __initdata = CMODE_NVRAM;
#endif

#if defined(CONFIG_MTRR)
static int mtrr = 1;
#endif

struct aty128_constants {
    u32 dotclock;
    u32 ppll_min;
    u32 ppll_max;
    u32 ref_divider;
    u32 xclk;
    u32 fifo_width;
    u32 fifo_depth;
};

struct aty128_crtc {
    u32 gen_cntl;
    u32 ext_cntl;
    u32 h_total, h_sync_strt_wid;
    u32 v_total, v_sync_strt_wid;
    u32 pitch;
    u32 offset, offset_cntl;
    u32 vxres, vyres;
    u32 bpp;
};

struct aty128_pll {
    u32 post_divider;
    u32 feedback_divider;
    u32 vclk;
};

struct aty128_ddafifo {
    u32 dda_config;
    u32 dda_on_off;
};

/* register values for a specific mode */
struct aty128fb_par {
    struct aty128_crtc crtc;
    struct aty128_pll pll;
    struct aty128_ddafifo fifo_reg;
    u32 accel_flags;
};

struct fb_info_aty128 {
    struct fb_info fb_info;
    struct aty128_constants constants;
    unsigned long regbase_phys, regbase;
    unsigned long frame_buffer_phys, frame_buffer;
    const struct aty128_meminfo *mem;
    u32 vram_size;
    u32 BIOS_SEG;
#ifdef CONFIG_MTRR
    struct { int vram; int vram_valid; } mtrr;
#endif
    struct aty128fb_par default_par, current_par;
    struct display disp;
    struct { u8 red, green, blue, pad; } palette[256];
    union {
#ifdef FBCON_HAS_CFB16
    u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
    u32 cfb24[16];
#endif
#ifdef FBCON_HAS_CFB32
    u32 cfb32[16];
#endif
    } fbcon_cmap;
    int blitter_may_be_busy;
};

#define round_div(n, d) ((n+(d/2))/d)

    /*
     *  Interface used by the world
     */

int aty128fb_setup(char *options);

static int aty128fb_open(struct fb_info *info, int user);
static int aty128fb_release(struct fb_info *info, int user);
static int aty128fb_get_fix(struct fb_fix_screeninfo *fix, int con,
		       struct fb_info *info);
static int aty128fb_get_var(struct fb_var_screeninfo *var, int con,
		       struct fb_info *info);
static int aty128fb_set_var(struct fb_var_screeninfo *var, int con,
		       struct fb_info *info);
static int aty128fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int aty128fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int aty128fb_pan_display(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int aty128fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		     u_long arg, int con, struct fb_info *info);


    /*
     *  Interface to the low level console driver
     */

void aty128fb_init(void);
#ifdef CONFIG_FB_OF
void aty128fb_of_init(struct device_node *dp);
#endif
static int aty128fbcon_switch(int con, struct fb_info *info);
static void aty128fbcon_blank(int blank, struct fb_info *info);


    /*
     *  Internal routines
     */

static void aty128_encode_fix(struct fb_fix_screeninfo *fix,
				struct aty128fb_par *par,
				const struct fb_info_aty128 *info);
static void aty128_set_disp(struct display *disp,
			struct fb_info_aty128 *info, int bpp, int accel);
static int aty128_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
				u_int *transp, struct fb_info *info);
static int aty128_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
				u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);
static void aty128pci_probe(void);
static int aty128find_ROM(struct fb_info_aty128 *info);
static void aty128_timings(struct fb_info_aty128 *info);
static void aty128_get_pllinfo(struct fb_info_aty128 *info);
static void aty128_reset_engine(const struct fb_info_aty128 *info);
static void aty128_flush_pixel_cache(const struct fb_info_aty128 *info);
static void wait_for_fifo(u16 entries, const struct fb_info_aty128 *info);
static void wait_for_idle(const struct fb_info_aty128 *info);
static u32 bpp_to_depth(u32 bpp);

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_aty128_8;
#endif


static struct fb_ops aty128fb_ops = {
    aty128fb_open, aty128fb_release, aty128fb_get_fix,
    aty128fb_get_var, aty128fb_set_var, aty128fb_get_cmap,
    aty128fb_set_cmap, aty128fb_pan_display, aty128fb_ioctl
};


    /*
     * Functions to read from/write to the mmio registers
     *	- endian conversions may possibly be avoided by flipping CONFIG_CNTL
     *  or using the other register aperture? TODO.
     */
static inline u32
_aty_ld_le32(volatile unsigned int regindex,
                              const struct fb_info_aty128 *info)
{
    unsigned long temp;
    u32 val;

#if defined(__powerpc__)
    eieio();
    temp = info->regbase;
    asm("lwbrx %0,%1,%2" : "=b"(val) : "b" (regindex), "b" (temp));
#elif defined(__sparc_v9__)
    temp = info->regbase + regindex;
    asm("lduwa [%1] %2, %0" : "=r" (val) : "r" (temp), "i" (ASI_PL));
#else
    temp = info->regbase+regindex;
    val = le32_to_cpu(*((volatile u32 *)(temp)));
#endif
    return val;
}

static inline void
_aty_st_le32(volatile unsigned int regindex, u32 val,
                               const struct fb_info_aty128 *info)
{
    unsigned long temp;

#if defined(__powerpc__)
    eieio();
    temp = info->regbase;
    asm("stwbrx %0,%1,%2" : : "b" (val), "b" (regindex), "b" (temp) :
        "memory");
#elif defined(__sparc_v9__)
    temp = info->regbase + regindex;
    asm("stwa %0, [%1] %2" : : "r" (val), "r" (temp), "i" (ASI_PL) : "memory");
#else
    temp = info->regbase+regindex;
    *((volatile u32 *)(temp)) = cpu_to_le32(val);
#endif
}

static inline u8
_aty_ld_8(volatile unsigned int regindex,
                          const struct fb_info_aty128 *info)
{
#if defined(__powerpc__)
    eieio();
#endif
    return *(volatile u8 *)(info->regbase+regindex);
}

static inline void
_aty_st_8(volatile unsigned int regindex, u8 val,
                            const struct fb_info_aty128 *info)
{
#if defined(__powerpc__)
    eieio();
#endif
    *(volatile u8 *)(info->regbase+regindex) = val;
}

#define aty_ld_le32(regindex)		_aty_ld_le32(regindex, info)
#define aty_st_le32(regindex, val)	_aty_st_le32(regindex, val, info)
#define aty_ld_8(regindex)		_aty_ld_8(regindex, info)
#define aty_st_8(regindex, val)		_aty_st_8(regindex, val, info)

    /*
     * Functions to read from/write to the pll registers
     */

#define aty_ld_pll(pll_index)		_aty_ld_pll(pll_index, info)
#define aty_st_pll(pll_index, val)	_aty_st_pll(pll_index, val, info)

static u32
_aty_ld_pll(unsigned int pll_index,
			const struct fb_info_aty128 *info)
{       
    aty_st_8(CLOCK_CNTL_INDEX, pll_index & 0x1F);
    return aty_ld_le32(CLOCK_CNTL_DATA);
}
    
static void
_aty_st_pll(unsigned int pll_index, u32 val,
			const struct fb_info_aty128 *info)
{   
    aty_st_8(CLOCK_CNTL_INDEX, (pll_index & 0x1F) | PLL_WR_EN);
    aty_st_le32(CLOCK_CNTL_DATA, val);
}
 
/* return true when the PLL has completed an atomic update */
static int
aty_pll_readupdate(const struct fb_info_aty128 *info)
{
    return !(aty_ld_pll(PPLL_REF_DIV) & PPLL_ATOMIC_UPDATE_R);
}

static void
aty_pll_wait_readupdate(const struct fb_info_aty128 *info)
{
    unsigned long timeout = jiffies + HZ/100;	// should be more than enough
    int reset = 1;

    while (time_before(jiffies, timeout))
	if (aty_pll_readupdate(info)) {
	    reset = 0;
	    break;
	}

#ifdef DEBUG
    if (reset)	/* reset engine?? */
	printk(KERN_ERR "aty128fb: PLL write timeout!");
#endif
}

/* tell PLL to update */
static void
aty_pll_writeupdate(const struct fb_info_aty128 *info)
{
    aty_pll_wait_readupdate(info);

    aty_st_pll(PPLL_REF_DIV,
	aty_ld_pll(PPLL_REF_DIV) | PPLL_ATOMIC_UPDATE_W);
}


/* write to the scratch register to test r/w functionality */
static u32
register_test(const struct fb_info_aty128 *info)
{
    u32 val, flag = 0;

    val = aty_ld_le32(BIOS_0_SCRATCH);

    aty_st_le32(BIOS_0_SCRATCH, 0x55555555);
    if (aty_ld_le32(BIOS_0_SCRATCH) == 0x55555555) {
	aty_st_le32(BIOS_0_SCRATCH, 0xAAAAAAAA);

	if (aty_ld_le32(BIOS_0_SCRATCH) == 0xAAAAAAAA)
	    flag = 1; 
    }

    aty_st_le32(BIOS_0_SCRATCH, val);	// restore value
    return flag;
}


    /*
     * Accelerator functions
     */
static void
wait_for_idle(const struct fb_info_aty128 *info)
{
    unsigned long timeout = jiffies + HZ/20;
    int reset = 1;

    wait_for_fifo(64, info);

    while (time_before(jiffies, timeout))
	if ((aty_ld_le32(GUI_STAT) & GUI_ACTIVE) != ENGINE_IDLE) {
	    reset = 0;
	    break;
	}

    if (reset)
	aty128_reset_engine(info);
}


static void
wait_for_fifo(u16 entries, const struct fb_info_aty128 *info)
{
    unsigned long timeout = jiffies + HZ/20;
    int reset = 1;

    while (time_before(jiffies, timeout))
	if ((aty_ld_le32(GUI_STAT) & 0x00000FFF) < entries) {
	    reset = 0;
	    break;
	}

    if (reset)
	aty128_reset_engine(info);
}


static void
aty128_flush_pixel_cache(const struct fb_info_aty128 *info)
{
    int i = 16384;

    aty_st_le32(PC_NGUI_CTLSTAT, aty_ld_le32(PC_NGUI_CTLSTAT) | 0x000000ff);

    while (i && (aty_ld_le32(PC_NGUI_CTLSTAT) & PC_BUSY))
	i--;
}


static void
aty128_reset_engine(const struct fb_info_aty128 *info)
{
    u32 gen_reset_cntl, clock_cntl_index, mclk_cntl;

    aty128_flush_pixel_cache(info);

    clock_cntl_index = aty_ld_le32(CLOCK_CNTL_INDEX);
    mclk_cntl = aty_ld_pll(MCLK_CNTL);

    aty_st_pll(MCLK_CNTL, mclk_cntl | 0x00030000);

    gen_reset_cntl = aty_ld_le32(GEN_RESET_CNTL);
    aty_st_le32(GEN_RESET_CNTL, gen_reset_cntl | SOFT_RESET_GUI);
    aty_ld_le32(GEN_RESET_CNTL);
    aty_st_le32(GEN_RESET_CNTL, gen_reset_cntl & ~(SOFT_RESET_GUI));
    aty_ld_le32(GEN_RESET_CNTL);

    aty_st_pll(MCLK_CNTL, mclk_cntl);
    aty_st_le32(CLOCK_CNTL_INDEX, clock_cntl_index);
    aty_st_le32(GEN_RESET_CNTL, gen_reset_cntl);

    /* use old pio mode */
    aty_st_le32(PM4_BUFFER_CNTL, PM4_BUFFER_CNTL_NONPM4);

#ifdef DEBUG
    printk("aty128fb: engine reset\n");
#endif
}


static void
aty128_init_engine(const struct aty128fb_par *par,
		const struct fb_info_aty128 *info)
{
    u32 temp;
    aty_st_le32(SCALE_3D_CNTL, 0x00000000);

    aty128_reset_engine(info);

    temp = par->crtc.pitch;	/* fix this up */
    if (par->crtc.bpp == 24) {
        temp = temp * 3;
    }

    /* setup engine offset registers */
    wait_for_fifo(4, info);
    aty_st_le32(DEFAULT_OFFSET, 0x00000000);

    /* setup engine pitch registers */
    aty_st_le32(DEFAULT_PITCH, temp);

    /* set the default scissor register to max dimensions */
    wait_for_fifo(1, info);
    aty_st_le32(DEFAULT_SC_BOTTOM_RIGHT, (0x1FFF << 16) | 0x1FFF);

    /* set the drawing controls registers */
    wait_for_fifo(1, info);
    aty_st_le32(DP_GUI_MASTER_CNTL,
			GMC_SRC_PITCH_OFFSET_DEFAULT		|
			GMC_DST_PITCH_OFFSET_DEFAULT		|
			GMC_SRC_CLIP_DEFAULT			|
			GMC_DST_CLIP_DEFAULT			|
			GMC_BRUSH_SOLIDCOLOR			|
			(bpp_to_depth(par->crtc.bpp) << 8)	|
			GMC_SRC_DSTCOLOR			|
			GMC_BYTE_ORDER_MSB_TO_LSB		|
			GMC_DP_CONVERSION_TEMP_6500		|
			ROP3_PATCOPY				|
			GMC_DP_SRC_RECT				|
			GMC_3D_FCN_EN_CLR			|
			GMC_DST_CLR_CMP_FCN_CLEAR		|
			GMC_AUX_CLIP_CLEAR			|
			GMC_WRITE_MASK_SET);
    wait_for_fifo(8, info);

    /* clear the line drawing registers */
    aty_st_le32(DST_BRES_ERR, 0);
    aty_st_le32(DST_BRES_INC, 0);
    aty_st_le32(DST_BRES_DEC, 0);

    /* set brush color registers */
    aty_st_le32(DP_BRUSH_FRGD_CLR, 0xFFFFFFFF);
    aty_st_le32(DP_BRUSH_BKGD_CLR, 0x00000000);

    /* set source color registers */
    aty_st_le32(DP_SRC_FRGD_CLR, 0xFFFFFFFF);
    aty_st_le32(DP_SRC_BKGD_CLR, 0x00000000);

    /* default write mask */
    aty_st_le32(DP_WRITE_MASK, 0xFFFFFFFF);

    /* Wait for all the writes to be completed before returning */
    wait_for_idle(info);
}


    /*
     * CRTC programming
     */

/* convert bpp values to their register representation */
static u32
bpp_to_depth(u32 bpp)
{
    if (bpp <= 8)
	return 2;
    else if (bpp <= 15)
	return 3;
    else if (bpp <= 16)
#if 0	/* force 15bpp */
	return 4;
#else
	return 3;
#endif
    else if (bpp <= 24)
	return 5;
    else if (bpp <= 32)
	return 6;

    return -EINVAL;
}


static void
aty128_set_crtc(const struct aty128_crtc *crtc,
		const struct fb_info_aty128 *info)
{
    aty_st_le32(CRTC_GEN_CNTL, crtc->gen_cntl);
    // aty_st_le32(CRTC_EXT_CNTL, crtc->ext_cntl);
    aty_st_le32(CRTC_H_TOTAL_DISP, crtc->h_total);
    aty_st_le32(CRTC_H_SYNC_STRT_WID, crtc->h_sync_strt_wid);
    aty_st_le32(CRTC_V_TOTAL_DISP, crtc->v_total);
    aty_st_le32(CRTC_V_SYNC_STRT_WID, crtc->v_sync_strt_wid);
    aty_st_le32(CRTC_PITCH, crtc->pitch);
    aty_st_le32(CRTC_OFFSET, crtc->offset);
    aty_st_le32(CRTC_OFFSET_CNTL, crtc->offset_cntl);
}


static int
aty128_var_to_crtc(const struct fb_var_screeninfo *var,
			struct aty128_crtc *crtc,
			const struct fb_info_aty128 *info)
{
    u32 xres, yres, vxres, vyres, xoffset, yoffset, bpp;
    u32 left, right, upper, lower, hslen, vslen, sync, vmode;
    u32 h_total, h_disp, h_sync_strt, h_sync_wid, h_sync_pol;
    u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
    u32 depth;
    u8 hsync_strt_pix[5] = { 0, 0x12, 9, 6, 5 };

    /* input */
    xres = var->xres;
    yres = var->yres;
    vxres = var->xres_virtual;
    vyres = var->yres_virtual;
    xoffset = var->xoffset;
    yoffset = var->yoffset;
    bpp = var->bits_per_pixel;
    left = var->left_margin;
    right = var->right_margin;
    upper = var->upper_margin;
    lower = var->lower_margin;
    hslen = var->hsync_len;
    vslen = var->vsync_len;
    sync = var->sync;
    vmode = var->vmode;

    /* check for mode eligibility */

    /* accept only non interlaced modes */
    if ((vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
	return -EINVAL;

    /* convert (and round up) and validate */
    xres = (xres + 7) & ~7;
    xoffset = (xoffset + 7) & ~7;

    if (vxres < xres + xoffset)
	vxres = xres + xoffset;

    if (vyres < yres + yoffset)
	vyres = yres + yoffset;

    if (bpp <= 8)
	bpp = 8;
    else if (bpp <= 16)
	bpp = 16;
    else if (bpp <= 32)
	bpp = 32;

    if (vxres * vyres * (bpp/8) > info->vram_size)
	return -EINVAL;

    h_disp = xres / 8 - 1;
    h_total = (xres + right + hslen + left) / 8 - 1;

    v_disp = yres - 1;
    v_total = yres + upper + vslen + lower - 1;

    h_sync_wid = hslen / 8;
    if (h_sync_wid == 0)
	h_sync_wid = 1;
    else if (h_sync_wid > 0x3f)
	h_sync_wid = 0x3f;

    h_sync_strt = (xres + right - 8) + hsync_strt_pix[bpp/8];

    v_disp = yres - 1;
    v_sync_wid = vslen;
    if (v_sync_wid == 0)
	v_sync_wid = 1;
    else if (v_sync_wid > 0x1f)
	v_sync_wid = 0x1f;
    
    v_sync_strt = yres + lower - 1;

    h_sync_pol = sync & FB_SYNC_HOR_HIGH_ACT ? 0 : (1 << 23);
    v_sync_pol = sync & FB_SYNC_VERT_HIGH_ACT ? 0 : (1 << 23);

    depth = bpp_to_depth(bpp);
    c_sync = sync & FB_SYNC_COMP_HIGH_ACT ? (1 << 4) : 0;

    crtc->gen_cntl = 0x03000000 | c_sync | depth << 8;

    crtc->h_total = (h_disp << 16) | (h_total & 0x0000FFFF);
    crtc->v_total = (v_disp << 16) | (v_total & 0x0000FFFF);

    crtc->h_sync_strt_wid = (h_sync_wid << 16) | (h_sync_strt) | h_sync_pol;
    crtc->v_sync_strt_wid = (v_sync_wid << 16) | (v_sync_strt) | v_sync_pol;

    crtc->pitch = xres / 8;

    crtc->offset = 0;
    crtc->offset_cntl = 0;

    crtc->vxres = vxres;
    crtc->vyres = vyres;
    crtc->bpp = bpp;

    return 0;
}


static int
aty128_crtc_to_var(const struct aty128_crtc *crtc,
			struct fb_var_screeninfo *var)
{
#ifdef notyet	/* xoffset and yoffset are not correctly calculated */
    u32 xres, yres, bpp, left, right, upper, lower, hslen, vslen, sync;
    u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid, h_sync_pol;
    u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
    u32 pix_width;

    h_total = crtc->h_total & 0x1ff;
    h_disp = (crtc->h_total>>16) & 0xff;
    h_sync_strt = (crtc->h_sync_strt_wid & 0xff) |
        ((crtc->h_sync_strt_wid>>4) & 0x100);
    h_sync_dly = (crtc->h_sync_strt_wid>>8) & 0x7;
    h_sync_wid = (crtc->h_sync_strt_wid>>16) & 0x1f;
    h_sync_pol = (crtc->h_sync_strt_wid>>21) & 0x1;
    v_total = crtc->v_total & 0x7ff;
    v_disp = (crtc->v_total>>16) & 0x7ff;
    v_sync_strt = crtc->v_sync_strt_wid & 0x7ff;
    v_sync_wid = (crtc->v_sync_strt_wid>>16) & 0x1f;
    v_sync_pol = (crtc->v_sync_strt_wid>>21) & 0x1;
    c_sync = crtc->gen_cntl & CRTC_CSYNC_EN ? 1 : 0;
    pix_width = crtc->gen_cntl & CRTC_PIX_WIDTH_MASK;

    xres = (h_disp+1)*8;
    yres = v_disp+1;
    left = (h_total-h_sync_strt-h_sync_wid)*8-h_sync_dly;
    right = (h_sync_strt-h_disp)*8+h_sync_dly;
    hslen = h_sync_wid*8;
    upper = v_total-v_sync_strt-v_sync_wid;
    lower = v_sync_strt-v_disp;
    vslen = v_sync_wid;
    sync = (h_sync_pol ? 0 : FB_SYNC_HOR_HIGH_ACT) |
        (v_sync_pol ? 0 : FB_SYNC_VERT_HIGH_ACT) |
        (c_sync ? FB_SYNC_COMP_HIGH_ACT : 0);

    switch (pix_width) {
#if 0
    case CRTC_PIX_WIDTH_4BPP:
        bpp = 4;
        var->red.offset = 0;
        var->red.length = 8;
        var->green.offset = 0;
        var->green.length = 8;
        var->blue.offset = 0;
        var->blue.length = 8;
        var->transp.offset = 0;
        var->transp.length = 0;
        break;
#endif
    case CRTC_PIX_WIDTH_8BPP:
        bpp = 8;
        var->red.offset = 0;
        var->red.length = 8;
        var->green.offset = 0;
        var->green.length = 8;
        var->blue.offset = 0;
        var->blue.length = 8;
        var->transp.offset = 0;
        var->transp.length = 0;
        break;
    case CRTC_PIX_WIDTH_15BPP:
        bpp = 16;
        var->red.offset = 10;
        var->red.length = 5;
        var->green.offset = 5;
        var->green.length = 5;
        var->blue.offset = 0;
        var->blue.length = 5;
        var->transp.offset = 0;
        var->transp.length = 0;
        break;
    case CRTC_PIX_WIDTH_16BPP:
        bpp = 16;
        var->red.offset = 11;
        var->red.length = 5;
        var->green.offset = 5;
        var->green.length = 6;
        var->blue.offset = 0;
        var->blue.length = 5;
        var->transp.offset = 0;
        var->transp.length = 0;
        break;
    case CRTC_PIX_WIDTH_24BPP:
        bpp = 24;
        var->red.offset = 16;
        var->red.length = 8;
        var->green.offset = 8;
        var->green.length = 8;
        var->blue.offset = 0;
        var->blue.length = 8;
        var->transp.offset = 0;
        var->transp.length = 0;
        break;
    case CRTC_PIX_WIDTH_32BPP:
        bpp = 32;
        var->red.offset = 16;
        var->red.length = 8;
        var->green.offset = 8;
        var->green.length = 8;
        var->blue.offset = 0;
        var->blue.length = 8;
        var->transp.offset = 24;
        var->transp.length = 8;
        break;
    default:
        printk(KERN_ERR "Invalid pixel width\n");
    }

//Godda do math for xoffset and yoffset: does not exist in crtc
    var->xres = xres;
    var->yres = yres;
    var->xres_virtual = crtc->vxres;
    var->yres_virtual = crtc->vyres;
    var->bits_per_pixel = bpp;
    var->xoffset = crtc->xoffset;
    var->yoffset = crtc->yoffset;
    var->left_margin = left;
    var->right_margin = right;
    var->upper_margin = upper;
    var->lower_margin = lower;
    var->hsync_len = hslen;
    var->vsync_len = vslen;
    var->sync = sync;
    var->vmode = FB_VMODE_NONINTERLACED;

#endif /* notyet */
    return 0;
}

static int
aty128_bpp_to_var(int bpp, struct fb_var_screeninfo *var)
{
    /* fill in pixel info */
    switch (bpp) {
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
    case 15:
	var->bits_per_pixel = 16;
	var->red.offset = 10;
	var->red.length = 5;
	var->green.offset = 5;
	var->green.length = 5;
	var->blue.offset = 0;
	var->blue.length = 5;
	var->transp.offset = 15;
	var->transp.length = 1;
	break;
    case 16:
	var->bits_per_pixel = 16;
	var->red.offset = 11;
	var->red.length = 5;
	var->green.offset = 5;
	var->green.length = 6;
	var->blue.offset = 0;
	var->blue.length = 5;
	var->transp.offset = 0;
	var->transp.length = 0;
	break;
    case 32:
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

    return 0;
}


static void
aty128_set_pll(struct aty128_pll *pll, const struct fb_info_aty128 *info)
{
    int div3;
    unsigned char post_conv[] =	/* register values for post dividers */
	{ 2, 0, 1, 4, 2, 2, 6, 2, 3, 2, 2, 2, 7 };

    /* select PPLL_DIV_3 */
    aty_st_le32(CLOCK_CNTL_INDEX, aty_ld_le32(CLOCK_CNTL_INDEX) | (3 << 8));

    /* reset ppll */
    aty_st_pll(PPLL_CNTL,
		aty_ld_pll(PPLL_CNTL) | PPLL_RESET | PPLL_ATOMIC_UPDATE_EN);

    div3 = aty_ld_pll(PPLL_DIV_3);

    div3 &= ~PPLL_FB3_DIV_MASK;
    div3 |= pll->feedback_divider;

    div3 &= ~PPLL_POST3_DIV_MASK;
    div3 |= post_conv[pll->post_divider] << 16;

    /* write feedback and post dividers */
    aty_st_pll(PPLL_DIV_3, div3);
    aty_pll_writeupdate(info);
    aty_pll_wait_readupdate(info);

    aty_st_pll(HTOTAL_CNTL, 0);	/* no horiz crtc adjustment */

    aty_pll_writeupdate(info);

    /* clear the reset, just in case */
    aty_st_pll(PPLL_CNTL, aty_ld_pll(PPLL_CNTL) & ~PPLL_RESET);
}


static int
aty128_var_to_pll(u32 vclk_per, struct aty128_pll *pll,
			const struct fb_info_aty128 *info)
{
    const struct aty128_constants c = info->constants;
    unsigned char post_dividers [] = {1,2,4,8,3,6,12};
    u32 output_freq, vclk;
    int i;
    u32 n, d;

    vclk = 100000000 / vclk_per;	/* convert units to 10 kHz */

    /* adjust pixel clock if necessary */
    if (vclk > c.ppll_max)
	vclk = c.ppll_max;
    if (vclk * 12 < c.ppll_min)
	vclk = c.ppll_min;

    /* now, find an acceptable divider */
    for (i = 0; i < sizeof(post_dividers); i++) {
	output_freq = post_dividers[i] * vclk;
	if (output_freq >= c.ppll_min && output_freq <= c.ppll_max)
	    break;
    }
    pll->post_divider = post_dividers[i];

    /* calculate feedback divider */
    n = c.ref_divider * output_freq;
    d = c.dotclock;
    pll->feedback_divider = round_div(n, d);

    pll->vclk = vclk;
#ifdef DEBUG
    printk("post %x  feedback %x vlck %x output %x\n",
	   pll->post_divider, pll->feedback_divider, vclk, output_freq);
#endif

    return 0;
}


static int
aty128_pll_to_var(const struct aty128_pll *pll, struct fb_var_screeninfo *var)
{
    /* TODO */
    return 0;
}


static void
aty128_set_fifo(const struct aty128_ddafifo *dsp,
			const struct fb_info_aty128 *info)
{
    aty_st_le32(DDA_CONFIG, dsp->dda_config);
    aty_st_le32(DDA_ON_OFF, dsp->dda_on_off);
}


static int
aty128_ddafifo(struct aty128_ddafifo *dsp,
		const struct aty128_pll *pll,
		u32 bpp,
		const struct fb_info_aty128 *info)
{
    const struct aty128_meminfo *m = info->mem;
    u32 xclk = info->constants.xclk;
    u32 fifo_width = info->constants.fifo_width;
    u32 fifo_depth = info->constants.fifo_depth;
    s32 x, b, p, ron, roff;
    u32 n, d;

    if (bpp == 15)
	bpp = 16;

    n = xclk * fifo_width;
    d = pll->vclk*bpp;
    x = round_div(n, d);

    ron = 4 * m->MB +
	3 * ((m->Trcd - 2 > 0) ? m->Trcd - 2 : 0) +
	2 * m->Trp +
	m->Twr +
	m->CL +
	m->Tr2w +
	x;

#ifdef DEBUG
    printk("x %x\n", x);
#endif
    b = 0;
    while (x) {
	x >>= 1;
	b++;
    }
    p = b + 1;

    ron <<= (11 - p);

    n <<= (11 - p);
    x = round_div(n, d);
    roff = x * (fifo_depth - 4);
    if ((ron + m->Rloop) >= roff) {
	printk("Mode out of range\n");
	return -EINVAL;
    }

#ifdef DEBUG
    printk("p: %x rloop: %x x: %x ron: %x roff: %x\n", p, m->Rloop, x,
	ron, roff);
#endif
    dsp->dda_config = p << 16 | m->Rloop << 20 | x;
    dsp->dda_on_off = ron << 16 | roff;

    return 0;
}


/*
 * This actually sets the video mode.
 */
static void
aty128_set_par(struct aty128fb_par *par,
			struct fb_info_aty128 *info)
{ 
    u32 config;
    
    info->current_par = *par;

    /* clear all registers that may interfere with mode setting */
    aty_st_le32(OVR_CLR, 0);
    aty_st_le32(OVR_WID_LEFT_RIGHT, 0);
    aty_st_le32(OVR_WID_TOP_BOTTOM, 0);
    aty_st_le32(OV0_SCALE_CNTL, 0);
    aty_st_le32(MPP_TB_CONFIG, 0);
    aty_st_le32(MPP_GP_CONFIG, 0);
    aty_st_le32(SUBPIC_CNTL, 0);
    aty_st_le32(VIPH_CONTROL, 0);
    aty_st_le32(I2C_CNTL_1, 0);
    aty_st_le32(GEN_INT_CNTL, 0);	/* turn off interrupts */
    aty_st_le32(CAP0_TRIG_CNTL, 0);
    aty_st_le32(CAP1_TRIG_CNTL, 0);

    aty_st_8(CRTC_EXT_CNTL + 1, 4);	/* turn video off */

    aty128_set_crtc(&par->crtc, info);
    aty128_set_pll(&par->pll, info);
    aty128_set_fifo(&par->fifo_reg, info);

    config = aty_ld_le32(CONFIG_CNTL) & ~3;

#if defined(__BIG_ENDIAN)
    if (par->crtc.bpp >= 24)
	config |= 2;	/* make aperture do 32 byte swapping */
    else if (par->crtc.bpp > 8)
	config |= 1;	/* make aperture do 16 byte swapping */
#endif

    aty_st_le32(CONFIG_CNTL, config);

    aty_st_8(CRTC_EXT_CNTL + 1, 0);	/* turn the video back on */
}


    /*
     *  Open/Release the frame buffer device
     */

static int aty128fb_open(struct fb_info *info, int user)
{
    MOD_INC_USE_COUNT;
    return(0);                              
}
        

static int aty128fb_release(struct fb_info *info, int user)
{
    MOD_DEC_USE_COUNT;
    return(0);                                                    
}


static int
aty128_decode_var(struct fb_var_screeninfo *var, struct aty128fb_par *par,
			const struct fb_info_aty128 *info)
{
    int err;

    if ((err = aty128_var_to_crtc(var, &(par->crtc), info)))
	return err;

    if ((err = aty128_var_to_pll(var->pixclock, &(par->pll), info)))
	return err;

    if ((err = aty128_ddafifo(&par->fifo_reg, &par->pll, par->crtc.bpp, info)))
	return err;

    if (var->accel_flags & FB_ACCELF_TEXT)
	par->accel_flags = FB_ACCELF_TEXT;
    else
	par->accel_flags = 0;

    return 0;
}


static int
aty128_encode_var(struct fb_var_screeninfo *var,
			const struct aty128fb_par *par,
			const struct fb_info_aty128 *info)
{
    int err;

    //memset(var, 0, sizeof(struct fb_var_screeninfo));

    /* XXX aty128_*_to_var() aren't fully implemented! */
    if ((err = aty128_crtc_to_var(&par->crtc, var)))
	return err;

    if ((err = aty128_pll_to_var(&par->pll, var)))
	return err;

    if ((err = aty128_bpp_to_var(var->bits_per_pixel, var)))
	return err;

    var->height = -1;
    var->width = -1;
    var->accel_flags = par->accel_flags;

    return 0;
}           


    /*
     *  Get the User Defined Part of the Display
     */

static int
aty128fb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *fb)
{
    const struct fb_info_aty128 *info = (struct fb_info_aty128 *)fb;

    if (con == -1)
	aty128_encode_var(var, &info->default_par, info); 
    else
	*var = fb_display[con].var;
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int
aty128fb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *fb)
{
    struct fb_info_aty128 *info = (struct fb_info_aty128 *)fb;
    struct aty128fb_par par;
    struct display *display;
    int oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel;
    int accel, err;

    display = (con >= 0) ? &fb_display[con] : fb->disp;

    if ((err = aty128_decode_var(var, &par, info)))
	return err;

    aty128_encode_var(var, &par, info);

    if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW)
	return 0;

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
	aty128_encode_fix(&fix, &par, info);
	display->screen_base = (char *) info->frame_buffer;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->can_soft_blank = 1;
	display->inverse = 0;

	accel = var->accel_flags & FB_ACCELF_TEXT;
	aty128_set_disp(display, info, var->bits_per_pixel, accel);

#if 0	/* acceleration is not ready */
	if (accel)
	    display->scrollmode = 0;
	else
#endif
	    display->scrollmode = SCROLL_YREDRAW;

	if (info->fb_info.changevar)
	    (*info->fb_info.changevar)(con);
    }

    if (!info->fb_info.display_fg || info->fb_info.display_fg->vc_num == con)
	aty128_set_par(&par, info);

    if (oldbpp != var->bits_per_pixel) {
	if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
	    return err;
	do_install_cmap(con, &info->fb_info);
    } 

    return 0;
}


static void
aty128_set_disp(struct display *disp,
			struct fb_info_aty128 *info, int bpp, int accel)
{
    switch (bpp) {
#ifdef FBCON_HAS_CFB8
    case 8:
	disp->dispsw = accel ? &fbcon_aty128_8 : &fbcon_cfb8;
	break;
#endif
#ifdef FBCON_HAS_CFB16
    case 16:
	disp->dispsw = &fbcon_cfb16;
	disp->dispsw_data = info->fbcon_cmap.cfb16;
	break;
#endif
#ifdef FBCON_HAS_CFB24
    case 24:
	disp->dispsw = &fbcon_cfb24;
	disp->dispsw_data = info->fbcon_cmap.cfb24;
	break;
#endif
#ifdef FBCON_HAS_CFB32
    case 32:
	disp->dispsw = &fbcon_cfb32;
	disp->dispsw_data = info->fbcon_cmap.cfb32;
	break;
#endif
    default:
	disp->dispsw = &fbcon_dummy;
    }
}


static void
aty128_encode_fix(struct fb_fix_screeninfo *fix,
			struct aty128fb_par *par,
			const struct fb_info_aty128 *info)
{
    memset(fix, 0, sizeof(struct fb_fix_screeninfo));
    
    strcpy(fix->id, aty128fb_name);
    fix->smem_start = (long) info->frame_buffer_phys;
    fix->smem_len = info->vram_size;

    fix->mmio_start = (long) info->regbase_phys;
    fix->mmio_len = 0x1fff;

    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->line_length = par->crtc.vxres*par->crtc.bpp/8;
    fix->visual = par->crtc.bpp <= 8 ? FB_VISUAL_PSEUDOCOLOR
					: FB_VISUAL_DIRECTCOLOR;

    fix->xpanstep = 8;
    fix->ypanstep = 1;

    fix->accel = FB_ACCEL_ATI_RAGE128;
    return;
}


    /*
     *  Get the Fixed Part of the Display
     */
static int
aty128fb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *fb)
{
    const struct fb_info_aty128 *info = (struct fb_info_aty128 *)fb;
    struct aty128fb_par par;

    if (con == -1)
	par = info->default_par;
    else
	aty128_decode_var(&fb_display[con].var, &par, info); 

    aty128_encode_fix(fix, &par, info);
    return 0;            
}


    /*
     *  Pan or Wrap the Display
     *
     *  Not supported (yet!)
     */
static int
aty128fb_pan_display(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
    if (var->xoffset != 0 || var->yoffset != 0)
	return -EINVAL;

    return 0;
}


    /*
     *  Get the Colormap
     */

static int
aty128fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info)
{
    if (!info->display_fg ||
	con == info->display_fg->vc_num) /* current console ? */        
	return fb_get_cmap(cmap, kspc, aty128_getcolreg, info);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else {  
	int size = (fb_display[con].var.bits_per_pixel <= 8) ? 256 : 32;
	fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
    }
    return 0;
}

    /*
     *  Set the Colormap
     */

static int
aty128fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info)
{
    int err;
    struct display *disp;  

    if (con >= 0)
	disp = &fb_display[con];
    else
	disp = info->disp;
    if (!disp->cmap.len) {      /* no colormap allocated? */
	int size = (disp->var.bits_per_pixel <= 16) ? 256 : 32;
	if ((err = fb_alloc_cmap(&disp->cmap, size, 0)))
	    return err;
    }
    if (!info->display_fg || con == info->display_fg->vc_num)
/* current console? */
	return fb_set_cmap(cmap, kspc, aty128_setcolreg, info);
    else
	fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
    return 0;                
}


    /*
     *  Virtual Frame Buffer Specific ioctls
     */

static int
aty128fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		     u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}


int __init
aty128fb_setup(char *options)
{
    char *this_opt;

    if (!options || !*options)
	return 0;

    for (this_opt = strtok(options, ","); this_opt;
	 this_opt = strtok(NULL, ",")) {
	if (!strncmp(this_opt, "font:", 5)) {
	    char *p;
	    int i;
	    
	    p = this_opt +5;
	    for (i = 0; i < sizeof(fontname) - 1; i++)
		if (!*p || *p == ' ' || *p == ',')
		    break;
	    memcpy(fontname, this_opt + 5, i);
	    fontname[i] = 0;
	}
#if defined(CONFIG_PPC)
	if (!strncmp(this_opt, "vmode:", 6)) {
            unsigned int vmode = simple_strtoul(this_opt+6, NULL, 0);
            if (vmode > 0 && vmode <= VMODE_MAX)
                default_vmode = vmode;
        } else if (!strncmp(this_opt, "cmode:", 6)) {
            unsigned int cmode = simple_strtoul(this_opt+6, NULL, 0);
            switch (cmode) {
	    case 0:
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
#endif
#ifdef CONFIG_MTRR
	if(mtrr) {
		ACCESS_FBINFO(mtrr.vram) =
			mtrr_add(video_base_phys, ACCESS_FBINFO(video.len),
				MTRR_TYPE_WRCOMB, 1);
		ACCESS_FBINFO(mtrr.valid_vram) = 1;
		printk(KERN_INFO "aty128fb: MTRR set to ON\n");
	}
#endif
    }
    return 0;
}


    /*
     *  Initialisation
     */

static int
aty128_init(struct fb_info_aty128 *info, const char *name)
{
    struct fb_var_screeninfo var;
    u32 dac;
    int j, k;
    u8 chip_rev;

    if (!register_test(info)) {
	printk("Can't write to video registers\n");
	return 0;
    }

    if (!info->vram_size)	/* may have already been probed */
	info->vram_size = aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

    chip_rev = (aty_ld_le32(CONFIG_CNTL) >> 16) & 0x1F;

    /* TODO be more verbose */
    printk("aty128fb: Rage128 [rev 0x%x] ", chip_rev);

    if (info->vram_size % (1024 * 1024) == 0)
	printk("%dM ", info->vram_size / (1024*1024));
    else
	printk("%dk ", info->vram_size / 1024);

    var = default_var;

#ifdef CONFIG_PMAC

    if (default_vmode == VMODE_NVRAM) {
#ifdef CONFIG_NVRAM
	default_vmode = nvram_read_byte(NV_VMODE);
	if (default_vmode <= 0 || default_vmode > VMODE_MAX)
#endif /* CONFIG_NVRAM */
	    default_vmode = VMODE_CHOOSE;
    }

    if (default_cmode == CMODE_NVRAM) {
#ifdef CONFIG_NVRAM
	default_cmode = nvram_read_byte(NV_CMODE);
	if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
#endif /* CONFIG_NVRAM */
	    default_vmode = VMODE_CHOOSE;
    }

    if (default_vmode != VMODE_CHOOSE &&
	mac_vmode_to_var(default_vmode, default_cmode, &var))
	var = default_var;

#endif /* CONFIG_PMAC */

    if (aty128_decode_var(&var, &info->default_par, info)) {
	printk("Cannot set default mode.\n");
	return 0;
    }

    /* fill in info */
    strcpy(info->fb_info.modename, aty128fb_name);
    info->fb_info.node = -1;
    info->fb_info.fbops = &aty128fb_ops;
    info->fb_info.disp = &info->disp;
    strcpy(info->fb_info.fontname, fontname);
    info->fb_info.changevar = NULL;
    info->fb_info.switch_con = &aty128fbcon_switch;
    info->fb_info.blank = &aty128fbcon_blank;
    info->fb_info.flags = FBINFO_FLAG_DEFAULT;

    for (j = 0; j < 16; j++) {
        k = color_table[j];
        info->palette[j].red = default_red[k];
        info->palette[j].green = default_grn[k];
        info->palette[j].blue = default_blu[k];
    }

    dac = aty_ld_le32(DAC_CNTL) & 15;	/* preserve lower three bits */
    dac |= DAC_8BIT_EN;			/* set 8 bit dac */
    dac |= (0xFF << 24);		/* set DAC mask */
    aty_st_le32(DAC_CNTL, dac);

    /* turn off bus mastering, just in case */
    aty_st_le32(BUS_CNTL, aty_ld_le32(BUS_CNTL) | BUS_MASTER_DIS);

    aty128fb_set_var(&var, -1, &info->fb_info);
    aty128_init_engine(&info->default_par, info);

    printk("\n");
    if (register_framebuffer(&info->fb_info) < 0)
	return 0;

    printk("fb%d: %s frame buffer device on %s\n",
	   GET_FB_IDX(info->fb_info.node), aty128fb_name, name);

    return 1;	/* success! */
}


void __init
aty128fb_init(void)
{
#if defined(CONFIG_FB_OF)
/* let offb handle init */
#elif defined (CONFIG_PCI)
    aty128pci_probe();
#endif
}


void
aty128pci_probe(void)
{
    struct pci_dev *pdev;
    struct fb_info_aty128 *info;
    unsigned long fb_addr, reg_addr;
    u16 tmp;

    for (pdev = pci_devices; pdev; pdev = pdev->next) {
	if (((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) &&
	    (pdev->vendor == PCI_VENDOR_ID_ATI)) {
	    struct resource *rp;

	    /* FIXME add other known R128 device ID's */
	    switch (pdev->device) {
	    case 0x5245:
	    case 0x5246:
	    case 0x524B:
	    case 0x524C:
		break;
	    default:
		continue;
	    }

	    rp = &pdev->resource[0];
	    fb_addr = rp->start;
	    if (!fb_addr)
		continue;
	    fb_addr &= PCI_BASE_ADDRESS_MEM_MASK;

	    rp = &pdev->resource[2];
	    reg_addr = rp->start;
	    if (!reg_addr)
		continue;
	    reg_addr &= PCI_BASE_ADDRESS_MEM_MASK;

	    info = kmalloc(sizeof(struct fb_info_aty128), GFP_ATOMIC);
	    if (!info) {
		printk("aty128fb: can't alloc fb_info_aty128\n");
		return;
	    }
	    memset(info, 0, sizeof(struct fb_info_aty128));

	    info->regbase_phys = reg_addr;
	    info->regbase = (unsigned long) ioremap(reg_addr, 0x1FFF);

	    if (!info->regbase) {
		kfree(info);
		return;
	    }

	    info->vram_size = aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

	    info->frame_buffer = fb_addr;
	    info->frame_buffer = (unsigned long)
				    ioremap(fb_addr, info->vram_size);

	    if (!info->frame_buffer) {
		kfree(info);
		return;
	    }

	    pci_read_config_word(pdev, PCI_COMMAND, &tmp);
	    if (!(tmp & PCI_COMMAND_MEMORY)) {
		tmp |= PCI_COMMAND_MEMORY;
		pci_write_config_word(pdev, PCI_COMMAND, tmp);
	    }

#if defined(CONFIG_PPC)
	    aty128_timings(info);
#else
            if (!aty128find_ROM(info)) {
                printk("Rage128 BIOS not located.  Guessing...\n");
                aty128_timings(info);
            }
            else
                aty128_get_pllinfo(info);
#endif

	    if (!aty128_init(info, "PCI")) {
		kfree(info);
		return;
	    }
	}
    }
}


static int
aty128find_ROM(struct fb_info_aty128 *info)
{
    u32 segstart;
    char *rom_base;
    char *rom_base1;
    char *rom;
    int stage;
    int i;
    char aty_rom_sig[] = "761295520";
    char R128_sig[] = "R128";
    int flag = 0;
DBG("E  aty128find_ROM");

    for (segstart = 0x000c0000; segstart < 0x000f0000; segstart += 0x00001000) {
        stage = 1;

        rom_base = (char *) ioremap(segstart, 0x1000);
        rom_base1 = (char *) (rom_base+1);

        if ((*rom_base == 0x55) && (((*rom_base1) & 0xff) == 0xaa)) {
            stage = 2;
        }

        if (stage != 2) {
            iounmap(rom_base);
            continue;
        }
        rom = rom_base;

        for (i = 0; (i < 128 - strlen(aty_rom_sig)) && (stage != 3); i++) {
            if (aty_rom_sig[0] == *rom) {
                if (strncmp(aty_rom_sig, rom, strlen(aty_rom_sig)) == 0) {
                    stage = 3;
                }
            }
            rom++;
        }
        if (stage != 3) {
            iounmap(rom_base);
            continue;
        }
        rom = rom_base;

        for (i = 0; (i < 512) && (stage != 4); i++) {
            if (R128_sig[0] == *rom) {
                if (strncmp(R128_sig, rom, strlen(R128_sig)) == 0) {
                    stage = 4;
                }
            }
            rom++;
        }
        if (stage != 4) {
            iounmap(rom_base);
            continue;
        }

        printk("Rage128 BIOS located at segment %4.4X\n", (u32)rom_base);
        info->BIOS_SEG = (u32)rom_base;
        flag = 1;

        break;
    }
DBG("L  aty128find_ROM");
    return (flag);
}


static void
aty128_get_pllinfo(struct fb_info_aty128 *info)
{   
    u32 bios_header;
    u32 *header_ptr;
    u16 bios_header_offset, pll_info_offset;
    PLL_BLOCK pll;
DBG("E  aty128_get_pllinfo");

    bios_header = info->BIOS_SEG + 0x48L;
    header_ptr = (u32 *)bios_header;

    bios_header_offset = (u16)*header_ptr;
    bios_header = info->BIOS_SEG + (u32)bios_header_offset;
    bios_header += 0x30;

    header_ptr = (u32 *)bios_header;
    pll_info_offset = (u16)*header_ptr;
    header_ptr = (u32 *)(info->BIOS_SEG + (u32)pll_info_offset);

    memcpy(&pll, header_ptr, 50);

    info->constants.ppll_max = pll.PCLK_max_freq;
    info->constants.ppll_min = pll.PCLK_min_freq;
    info->constants.xclk = (u32)pll.XCLK;
    info->constants.ref_divider = (u32)pll.PCLK_ref_divider;
    info->constants.dotclock = (u32)pll.PCLK_ref_freq;

    info->constants.fifo_width = 128;
    info->constants.fifo_depth = 32;

    switch(aty_ld_le32(MEM_CNTL) & 0x03) {
    case 0:
        info->mem = &sdr_128; 
        break;
    case 1:
        info->mem = &sdr_sgram;
        break;  
    case 2: 
        info->mem = &ddr_sgram;
        break;  
    default:        
        info->mem = &sdr_sgram;
    }       

DBG("L  aty128get_pllinfo");
    return;
}           


#ifdef CONFIG_FB_OF
void
aty128fb_of_init(struct device_node *dp)
{
    unsigned long addr, reg_addr, fb_addr;
    struct fb_info_aty128 *info;
    u8 bus, devfn;
    u16 cmd;

    switch (dp->n_addrs) {
    case 3:
	fb_addr = dp->addrs[0].address;
	reg_addr = dp->addrs[2].address;
	break;
    default:
	printk("aty128fb: TODO unexpected addresses\n");
	return;
    }

    addr = (unsigned long) ioremap(reg_addr, 0x1FFF);
    if (!addr) {
	printk("aty128fb: can't map memory registers\n");
	return;
    }

    info = kmalloc(sizeof(struct fb_info_aty128), GFP_ATOMIC);
    if (!info) {
	printk("aty128fb: can't alloc fb_info_aty128\n");
	return;
    }
    memset(info, 0, sizeof(struct fb_info_aty128));

    info->regbase_phys = reg_addr;
    info->regbase = addr;

    /* enabled memory-space accesses using config-space command register */
    if (pci_device_loc(dp, &bus, &devfn) == 0) {
	pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_MEMORY)) {
	    cmd |= PCI_COMMAND_MEMORY;
	    pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
	}
    }

    info->vram_size = aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;
    info->frame_buffer_phys = fb_addr;
    info->frame_buffer = (unsigned long) ioremap(fb_addr, info->vram_size);

    /*
     * TODO find OF values/hints.
     *
     * If we are booted from BootX, the MacOS ATI driver will likely have
     * left useful tidbits in the DeviceRegistry.
     */

    if (!info->frame_buffer) {
	printk("aty128fb: can't map frame buffer\n");
	return;
    }

    aty128_timings(info);

    if (!aty128_init(info, dp->full_name)) {
	kfree(info);
	return;
    }
}
#endif


/* fill in known card constants if pll_block is not available */
static void
aty128_timings(struct fb_info_aty128 *info)
{
    /* TODO make an attempt at probing */

    info->constants.dotclock = 2950;

    /* from documentation */
    info->constants.ppll_min = 12500;
    info->constants.ppll_max = 25000;	/* 23000 on some cards? */

#if 1
    /* XXX TODO. Calculate properly. Fix OF's pll ideas. */
    info->constants.ref_divider = 0x3b;
    aty_st_pll(PPLL_REF_DIV, info->constants.ref_divider);
    aty_pll_writeupdate(info);

    aty_st_pll(X_MPLL_REF_FB_DIV, 0x004c4c1e);
    aty_pll_writeupdate(info);
#else
    info->constants.ref_divider = aty_ld_pll(PPLL_REF_DIV) & PPLL_REF_DIV_MASK;
#endif

    /* TODO. Calculate */
    info->constants.xclk = 0x1d4d;	/* same as mclk */

    info->constants.fifo_width = 128;
    info->constants.fifo_depth = 32;

    switch (aty_ld_le32(MEM_CNTL) & 0x3) {
    case 0:
	info->mem = &sdr_128;
	break;
    case 1:
	info->mem = &sdr_sgram;
	break;
    case 2:
	info->mem = &ddr_sgram;
	break;
    default:
	info->mem = &sdr_sgram;
    }
}


static int
aty128fbcon_switch(int con, struct fb_info *fb)
{
    currcon = con;

    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, 1, aty128_getcolreg, fb);

#if 1
    aty128fb_set_var(&fb_display[con].var, con, fb);
#else
{
    struct fb_info_aty128 *info = (struct fb_info_aty128 *) fb;
    struct aty128fb_par par;

    aty128_decode_var(&fb_display[con].var, &par, info);
    aty128_set_par(&par, info);
    aty128_set_disp(&fb_display[con], info,
	fb_display[con].var.bits_per_pixel);

    do_install_cmap(con, fb);
}
#endif

    return 1;
}


    /*
     *  Blank the display.
     */

static void
aty128fbcon_blank(int blank, struct fb_info *fb)
{
    struct fb_info_aty128 *info = (struct fb_info_aty128 *)fb;
    u8 state = 0;

    if (blank & VESA_VSYNC_SUSPEND)
	state |= 2;
    if (blank & VESA_HSYNC_SUSPEND)
	state |= 1;
    if (blank & VESA_POWERDOWN)
	state |= 4;

    aty_st_8(CRTC_EXT_CNTL+1, state);
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */
static int
aty128_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *fb)
{
    struct fb_info_aty128 *info = (struct fb_info_aty128 *) fb;

    if (regno > 255)
	return 1;

    *red = (info->palette[regno].red<<8) | info->palette[regno].red;
    *green = (info->palette[regno].green<<8) | info->palette[regno].green;
    *blue = (info->palette[regno].blue<<8) | info->palette[regno].blue;
    *transp = 0;
    return 0;
}


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */
static int
aty128_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *fb)
{
    struct fb_info_aty128 *info = (struct fb_info_aty128 *) fb;
    u32 col;

    if (regno > 255)
	return 1;

    red >>= 8;
    green >>= 8;
    blue >>= 8;
    info->palette[regno].red = red;
    info->palette[regno].green = green;
    info->palette[regno].blue = blue;

    aty_st_8(PALETTE_INDEX, regno);
    col = red << 16 | green << 8 | blue;
    aty_st_le32(PALETTE_DATA, col);

    if (regno < 16)
	switch (info->current_par.crtc.bpp) {
#ifdef FBCON_HAS_CFB16
	case 16:
	    info->fbcon_cmap.cfb16[regno] = (regno << 10) | (regno << 5) |
		regno;
	    break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
	    info->fbcon_cmap.cfb24[regno] = (regno << 16) | (regno << 8) |
		regno;
	    break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
	    {
		u32 i;
		i = (regno << 8) | regno;
		info->fbcon_cmap.cfb32[regno] = (i << 16) | i;
	    }
	    break;
#endif
	}
    return 0;
}


static void
do_install_cmap(int con, struct fb_info *info)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, 1, aty128_setcolreg, info);
    else {
	int size = (fb_display[con].var.bits_per_pixel <= 8) ? 256 : 16;
	fb_set_cmap(fb_default_cmap(size), 1, aty128_setcolreg, info);
    }
}


    /*
     *  Accelerated functions
     */

static void
aty128_rectdraw(s16 x, s16 y, u16 width, u16 height,
		struct fb_info_aty128 *info)
{
    /* perform rectangle fill */
    wait_for_fifo(2, info);
    aty_st_le32(DST_Y_X, (y << 16) | x);
    aty_st_le32(DST_HEIGHT_WIDTH, (height << 16) | width);
}


static void
aty128_rectcopy(int srcx, int srcy, int dstx, int dsty,
		u_int width, u_int height,
		struct fb_info_aty128 *info)
{
    u32 direction = DST_LAST_PEL;
    u32 pitch_value;

    if (!width || !height)
        return;

    pitch_value = info->current_par.crtc.vxres;
    if (info->current_par.crtc.bpp == 24) {
        /* In 24 bpp, the engine is in 8 bpp - this requires that all */
        /* horizontal coordinates and widths must be adjusted */
        pitch_value *= 3;
        srcx *= 3;
        dstx *= 3;
        width *= 3;
    }
    
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

    wait_for_fifo(4, info);
    aty_st_le32(SRC_Y_X, (srcy << 16) | srcx);
    aty_st_le32(DP_MIX, ROP3_SRCCOPY | DP_SRC_RECT);
    aty_st_le32(DP_CNTL, direction);
    aty_st_le32(DP_DATATYPE, aty_ld_le32(DP_DATATYPE) | SRC_DSTCOLOR);
    aty128_rectdraw(dstx, dsty, width, height, info);
}

static void
fbcon_aty128_bmove(struct display *p, int sy, int sx, int dy, int dx,
			int height, int width)
{
    sx *= fontwidth(p);
    sy *= fontheight(p);
    dx *= fontwidth(p);
    dy *= fontheight(p);
    width *= fontwidth(p);
    height *= fontheight(p);

    aty128_rectcopy(sx, sy, dx, dy, width, height,
			(struct fb_info_aty128 *)p->fb_info);
}

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_aty128_8 = {
    fbcon_cfb8_setup, fbcon_aty128_bmove, fbcon_cfb8_clear, fbcon_cfb8_putc,
    fbcon_cfb8_putcs, fbcon_cfb8_revc, NULL, NULL, fbcon_cfb8_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#if defined(MODULE) && defined(DEBUG)
int
init_module(void)
{
    aty128pci_probe();
    return 0;
}

void
cleanup_module(void)
{
/* XXX unregister! */
}
#endif /* MODULE */
