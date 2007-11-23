/* $Id: aty128fb.c,v 1.1.1.1.36.1 1999/12/11 09:03:05 Exp $
 *  linux/drivers/video/aty128fb.c -- Frame buffer device for ATI Rage128
 *
 *  Copyright (C) 1999-2000, Brad Douglas <brad@neruo.com>
 *  Copyright (C) 1999, Anthony Tong <atong@uiuc.edu>
 *
 *                Ani Joshi / Jeff Garzik
 *                      - Code cleanup
 *
 *  Based off of Geert's atyfb.c and vfb.c.
 *
 *  TODO:
 *		- panning
 *		- monitor sensing (DDC)
 *              - virtual display
 *		- other platform support (only ppc/x86 supported)
 *		- PPLL_REF_DIV & XTALIN calculation    -done for x86
 *		- determine MCLK from previous setting -done for x86            
 *              - calculate XCLK, rather than probe BIOS
 *		- hardware cursor support
 *              - acceleration (do not use with Rage128 Pro!)
 *		- ioctl()'s
 */

/*
 * A special note of gratitude to ATI's devrel for providing documentation,
 * example code and hardware. Thanks Nitya.	-atong and brad
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
#include <asm/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <asm/io.h>

#if defined(CONFIG_PPC)
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#ifdef CONFIG_NVRAM
#include <linux/nvram.h>
#endif
#include <video/macmodes.h>
#endif

#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif /* CONFIG_MTRR */

#include "aty128.h"

/* Debug flag */
#undef DEBUG

#ifdef DEBUG
#define DBG(x)		printk(KERN_DEBUG "aty128fb: %s\n",(x));
#else
#define DBG(x)
#endif

/* default mode */
static struct fb_var_screeninfo default_var = {
    /* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
    640, 480, 640, 480, 0, 0, 8, 0,
    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
    0, 0, -1, -1, 0, 39722, 48, 16, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};

#ifndef MODULE
/* default modedb mode */
static struct fb_videomode defaultmode __initdata = {
    /* 640x480, 60 Hz, Non-Interlaced (25.172 MHz dotclock) */
    NULL, 60, 640, 480, 39722, 48, 16, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};
#endif

/* chip description information */
struct aty128_chip_info {
    const char *name;
    unsigned short device;
};

/* supported Rage128 chipsets */
static const struct aty128_chip_info aty128_pci_probe_list[] __initdata =
{
    {"Rage128 RE (PCI)", PCI_DEVICE_ID_ATI_RAGE128_RE},
    {"Rage128 RF (AGP)", PCI_DEVICE_ID_ATI_RAGE128_RF},
    {"Rage128 RK (PCI)", PCI_DEVICE_ID_ATI_RAGE128_RK},
    {"Rage128 RL (AGP)", PCI_DEVICE_ID_ATI_RAGE128_RL},
    {"Rage128 Pro PF (AGP)", PCI_DEVICE_ID_ATI_RAGE128_PF},
    {"Rage128 Pro PR (PCI)", PCI_DEVICE_ID_ATI_RAGE128_PR},
    {NULL, 0}
};

/* packed BIOS settings */
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

/* onboard memory information */
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

/* various memory configurations */
const struct aty128_meminfo sdr_128   = { 4, 4, 3, 3, 1, 3, 1, 16, 30, 16 };
const struct aty128_meminfo sdr_64    = { 4, 8, 3, 3, 1, 3, 1, 17, 46, 17 };
const struct aty128_meminfo sdr_sgram = { 4, 4, 1, 2, 1, 2, 1, 16, 24, 16 };
const struct aty128_meminfo ddr_sgram = { 4, 4, 3, 3, 2, 3, 1, 16, 31, 16 };

static int currcon = 0;

static char *aty128fb_name = "ATY Rage128";
static char fontname[40] __initdata = { 0 };
static char noaccel __initdata = 1;
static unsigned int initdepth __initdata = 8;

#ifndef MODULE
static const char *mode_option __initdata = NULL;
#endif

#ifdef CONFIG_PPC
#ifdef CONFIG_NVRAM_NOT_DEFINED
static int default_vmode __initdata = VMODE_NVRAM;
static int default_cmode __initdata = CMODE_NVRAM;
#else
static int default_vmode __initdata = VMODE_CHOOSE;
static int default_cmode __initdata = CMODE_8;
#endif
#endif

#ifdef CONFIG_MTRR
static int mtrr = 1;
#endif /* CONFIG_MTRR */

/* PLL constants */
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
    u32 xoffset, yoffset;
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
    struct fb_info_aty128 *next;
    struct aty128_constants constants;
    unsigned long regbase_phys;         /* mmio                */
    unsigned long frame_buffer_phys;    /* framebuffer memory  */
    unsigned long frame_buffer;         /* remaped framebuffer */
    void *regbase;
    const struct aty128_meminfo *mem;   /* onboard mem info    */
    u32 vram_size;                      /* onboard video ram   */
#ifndef CONFIG_PPC
    void *bios_seg;                     /* video BIOS segment  */
#endif
    unsigned short card_revision;	/* video card revision */
    struct aty128fb_par default_par, current_par;
    struct display disp;
    struct display_switch dispsw;       /* for cursor and font */
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
#ifdef CONFIG_PCI
    struct pci_dev *pdev;
#endif
#ifdef CONFIG_MTRR
    struct { int vram; int vram_valid; } mtrr;
#endif /* CONFIG_MTRR */
};

static struct fb_info_aty128 *board_list = NULL;

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
			   struct fb_info *fb);
static int aty128fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		     u_long arg, int con, struct fb_info *info);


    /*
     *  Interface to the low level console driver
     */

int aty128fb_init(void);
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
static int aty128_pci_register(struct pci_dev *pdev,
                               const struct aty128_chip_info *aci);
static struct fb_info_aty128 *aty128_board_list_add(struct fb_info_aty128
				*board_list, struct fb_info_aty128 *new_node);
#ifndef CONFIG_PPC
static int aty128find_ROM(struct fb_info_aty128 *info);
static void aty128_get_pllinfo(struct fb_info_aty128 *info);
#endif
static void aty128_timings(struct fb_info_aty128 *info);
static void aty128_init_engine(const struct aty128fb_par *par, 
				struct fb_info_aty128 *info);
static void aty128_reset_engine(const struct fb_info_aty128 *info);
static void aty128_flush_pixel_cache(const struct fb_info_aty128 *info);
static void wait_for_fifo(u16 entries, const struct fb_info_aty128 *info);
static void wait_for_idle(struct fb_info_aty128 *info);
static u32 bpp_to_depth(u32 bpp);

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_aty128_8;
static void fbcon_aty8_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx);
static void fbcon_aty8_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx);
#endif
#ifdef FBCON_HAS_CFB16
static struct display_switch fbcon_aty128_16;
static void fbcon_aty16_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx);
static void fbcon_aty16_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx);
#endif
#ifdef FBCON_HAS_CFB24
static struct display_switch fbcon_aty128_24;
static void fbcon_aty24_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx);
static void fbcon_aty24_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx);
#endif
#ifdef FBCON_HAS_CFB32
static struct display_switch fbcon_aty128_32;
static void fbcon_aty32_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx);
static void fbcon_aty32_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx);
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
    unsigned long *temp;
    u32 val;

#if defined(__powerpc__)
    eieio();
    temp = info->regbase;
    asm("lwbrx %0,%1,%2" : "=b"(val) : "b" (regindex), "b" (temp));
#else
    temp = info->regbase+regindex;
    val = readl (temp);
#endif

    return val;
}

static inline void
_aty_st_le32(volatile unsigned int regindex, u32 val, 
                               const struct fb_info_aty128 *info)
{
    unsigned long *temp;

#if defined(__powerpc__)
    temp = info->regbase;
    asm("stwbrx %0,%1,%2" : : "r" (val), "b" (regindex), "r" (temp) :
        "memory");
#elif defined(__mc68000__)
    *((volatile u32 *)(info->regbase+regindex)) = cpu_to_le32(val);
#else
    temp = info->regbase+regindex;
    writel (val, temp);
#endif
}

static inline u8
_aty_ld_8(unsigned int regindex, const struct fb_info_aty128 *info)
{
#if defined(__powerpc__)
    eieio();
#endif
    return readb (info->regbase + regindex);
}

static inline void
_aty_st_8(unsigned int regindex, u8 val, const struct fb_info_aty128 *info)
{
#if defined(__powerpc__)
    eieio();
#endif
    writeb (val, info->regbase + regindex);
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
#if defined(__powerpc__)
    eieio();
#endif
    aty_st_8(CLOCK_CNTL_INDEX, pll_index & 0x1F);
    return aty_ld_le32(CLOCK_CNTL_DATA);
}

    
static void
_aty_st_pll(unsigned int pll_index, u32 val,
			const struct fb_info_aty128 *info)
{
#if defined(__powerpc__)
    eieio();
#endif
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
	DBG("PLL write timeout!");
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
     * Accelerator engine functions
     */
static void
wait_for_idle(struct fb_info_aty128 *info)
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
	
    info->blitter_may_be_busy = 0;
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

    while (i && ((aty_ld_le32(PC_NGUI_CTLSTAT) & PC_BUSY) == PC_BUSY))
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
    DBG("engine reset");
#endif
}


static void
aty128_init_engine(const struct aty128fb_par *par,
		struct fb_info_aty128 *info)
{
    u32 pitch_value;

    /* 3D scaler not spoken here */
    aty_st_le32(SCALE_3D_CNTL, 0x00000000);

    aty128_reset_engine(info);

    pitch_value = par->crtc.pitch;	/* fix this up */
    if (par->crtc.bpp == 24) {
        pitch_value = pitch_value * 3;
    }

    /* setup engine offset registers */
    wait_for_fifo(1, info);
    aty_st_le32(DEFAULT_OFFSET, 0x00000000);

    /* setup engine pitch registers */
    aty_st_le32(DEFAULT_PITCH, pitch_value);

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
		(bpp_to_depth(par->crtc.bpp << 8))	|
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
    aty_st_le32(DP_BRUSH_FRGD_CLR, 0xFFFFFFFF); /* white */
    aty_st_le32(DP_BRUSH_BKGD_CLR, 0x00000000); /* black */

    /* set source color registers */
    aty_st_le32(DP_SRC_FRGD_CLR, 0xFFFFFFFF);   /* white */
    aty_st_le32(DP_SRC_BKGD_CLR, 0x00000000);   /* black */

    /* default write mask */
    aty_st_le32(DP_WRITE_MASK, 0xFFFFFFFF);

    /* Wait for all the writes to be completed before returning */
    wait_for_idle(info);
}


/* convert bpp values to their register representation */
static u32
bpp_to_depth(u32 bpp)
{
    if (bpp <= 8)
	return DST_8BPP;
    else if (bpp <= 16)
        return DST_15BPP;
    else if (bpp <= 24)
	return DST_24BPP;
    else if (bpp <= 32)
	return DST_32BPP;

    return -EINVAL;
}


    /*
     * CRTC programming
     */

/* Program the CRTC registers */
static void
aty128_set_crtc(const struct aty128_crtc *crtc,
		const struct fb_info_aty128 *info)
{
    aty_st_le32(CRTC_GEN_CNTL, crtc->gen_cntl);
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
    u32 depth, bytpp;
    u8 hsync_strt_pix[5] = { 0, 0x12, 9, 6, 5 };
    u8 mode_bytpp[7] = { 0, 0, 1, 2, 2, 3, 4 };

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

    depth = bpp_to_depth(bpp);

    /* make sure we didn't get an invalid depth */
    if (depth == -EINVAL) {
        printk(KERN_ERR "aty128fb: Invalid depth\n");
        return -EINVAL;
    }

   bytpp = mode_bytpp[depth];

    /* make sure there is enough video ram for the mode */
    if ((u32)(vxres * vyres * bytpp) > info->vram_size) {
        printk(KERN_ERR "aty128fb: Not enough memory for mode\n");
        return -EINVAL;
    }

    h_disp = (xres/8) - 1;
    h_total = (((xres + right + hslen + left) / 8) - 1) & 0xFFFFL;

    v_disp = yres - 1;
    v_total = (yres + upper + vslen + lower - 1) & 0xFFFFL;

    /* check to make sure h_total and v_total are in range */
    if ((h_total/8 - 1) > 0x1ff || (v_total - 1) > 0x7FF) {
        printk(KERN_ERR "aty128fb: invalid width ranges\n");
        return -EINVAL;
    }

    h_sync_wid = (hslen+7)/8;
    if (h_sync_wid == 0)
	h_sync_wid = 1;
    else if (h_sync_wid > 0x3f)        /* 0x3f = max hwidth */
	h_sync_wid = 0x3f;

    h_sync_strt = h_disp + (right/8);

    v_sync_wid = vslen;
    if (v_sync_wid == 0)
	v_sync_wid = 1;
    else if (v_sync_wid > 0x1f)        /* 0x1f = max vwidth */
	v_sync_wid = 0x1f;
    
    v_sync_strt = v_disp + lower;

    h_sync_pol = sync & FB_SYNC_HOR_HIGH_ACT ? 0 : 1;
    v_sync_pol = sync & FB_SYNC_VERT_HIGH_ACT ? 0 : 1;
    
    c_sync = sync & FB_SYNC_COMP_HIGH_ACT ? (1 << 4) : 0;

    crtc->gen_cntl = 0x03000000L | c_sync | (depth << 8);

    crtc->h_total = h_total | (h_disp << 16);
    crtc->v_total = v_total | (v_disp << 16);

    crtc->h_sync_strt_wid = hsync_strt_pix[bytpp] | (h_sync_strt << 3) |
                (h_sync_wid << 16) | (h_sync_pol << 23);
    crtc->v_sync_strt_wid = v_sync_strt | (v_sync_wid << 16) |
                (v_sync_pol << 23);

    crtc->pitch = xres >> 3;

    crtc->offset = 0;
    crtc->offset_cntl = 0;

    crtc->vxres = vxres;
    crtc->vyres = vyres;
    crtc->xoffset = xoffset;
    crtc->yoffset = yoffset;
    crtc->bpp = bpp;

    return 0;
}


static int
aty128_bpp_to_var(int pix_width, struct fb_var_screeninfo *var)
{

    /* fill in pixel info */
    switch (pix_width) {
    case CRTC_PIX_WIDTH_8BPP:
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
    case CRTC_PIX_WIDTH_15BPP:
    case CRTC_PIX_WIDTH_16BPP:
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
    case CRTC_PIX_WIDTH_24BPP:
        var->bits_per_pixel = 24;
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
    default:
        printk(KERN_ERR "Invalid pixel width\n");
        return -1;
    }

    return 0;
}


static int
aty128_crtc_to_var(const struct aty128_crtc *crtc,
			struct fb_var_screeninfo *var)
{
    u32 xres, yres, left, right, upper, lower, hslen, vslen, sync;
    u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid, h_sync_pol;
    u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
    u32 pix_width;

    /* fun with masking */
    h_total = crtc->h_total & 0x1ff;
    h_disp = (crtc->h_total>>16) & 0xff;
    h_sync_strt = (crtc->h_sync_strt_wid>>3) & 0x1ff;
    h_sync_dly = crtc->h_sync_strt_wid & 0x7;
    h_sync_wid = (crtc->h_sync_strt_wid>>16) & 0x3f;
    h_sync_pol = (crtc->h_sync_strt_wid>>23) & 0x1;
    v_total = crtc->v_total & 0x7ff;
    v_disp = (crtc->v_total>>16) & 0x7ff;
    v_sync_strt = crtc->v_sync_strt_wid & 0x7ff;
    v_sync_wid = (crtc->v_sync_strt_wid>>16) & 0x1f;
    v_sync_pol = (crtc->v_sync_strt_wid>>23) & 0x1;
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

    aty128_bpp_to_var(pix_width, var);

    var->xres = xres;
    var->yres = yres;
    var->xres_virtual = crtc->vxres;
    var->yres_virtual = crtc->vyres;
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

    /* reset PLL */
    aty_st_pll(PPLL_CNTL,
		aty_ld_pll(PPLL_CNTL) | PPLL_RESET | PPLL_ATOMIC_UPDATE_EN);

    /* write the reference divider */
    aty_st_pll(PPLL_REF_DIV, info->constants.ref_divider & 0x3ff);
    aty_pll_writeupdate(info);
    aty_pll_wait_readupdate(info);

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
aty128_var_to_pll(u32 period_in_ps, struct aty128_pll *pll,
			const struct fb_info_aty128 *info)
{
    const struct aty128_constants c = info->constants;
    unsigned char post_dividers [] = {1,2,4,8,3,6,12};
    u32 output_freq;
    u32 vclk;        /* in .01 MHz */
    int i;
    u32 n, d;

    vclk = 100000000 / period_in_ps;	/* convert units to 10 kHz */

    /* adjust pixel clock if necessary */
    if (vclk > c.ppll_max)
	vclk = c.ppll_max;
    if (vclk * 12 < c.ppll_min)
	vclk = c.ppll_min/12;

    /* now, find an acceptable divider */
    for (i = 0; i < sizeof(post_dividers); i++) {
	output_freq = post_dividers[i] * vclk;
	if (output_freq >= c.ppll_min && output_freq <= c.ppll_max)
	    break;
    }

    /* calculate feedback divider */
    n = c.ref_divider * output_freq;
    d = c.dotclock;

    pll->post_divider = post_dividers[i];
    pll->feedback_divider = round_div(n, d);
    pll->vclk = vclk;

#ifdef DEBUG
    printk(KERN_DEBUG "var_to_pll: post %d feedback %d vlck %d output %d ref_divider %d\n", 
           pll->post_divider, pll->feedback_divider, vclk, output_freq,
           c.ref_divider);
    printk(KERN_DEBUG "var_to_pll: vclk_per: %d\n", period_in_ps);
#endif

    return 0;
}


static int
aty128_pll_to_var(const struct aty128_pll *pll, struct fb_var_screeninfo *var,
		const struct fb_info_aty128 *info)
{
    var->pixclock = 100000000 / pll->vclk;

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

    /* 15bpp is really 16bpp */
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
    printk(KERN_DEBUG "aty128fb: x %x\n", x);
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
	printk(KERN_ERR "aty128fb: Mode out of range!\n");
	return -EINVAL;
    }

#ifdef DEBUG
    printk(KERN_DEBUG "aty128fb: p: %x rloop: %x x: %x ron: %x roff: %x\n",
                       p, m->Rloop, x, ron, roff);
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
    
    if (info->blitter_may_be_busy)
        wait_for_idle(info);

    /* clear all registers that may interfere with mode setting */
    aty_st_le32(OVR_CLR, 0);
    aty_st_le32(OVR_WID_LEFT_RIGHT, 0);
    aty_st_le32(OVR_WID_TOP_BOTTOM, 0);
    aty_st_le32(OV0_SCALE_CNTL, 0);
    aty_st_le32(MPP_TB_CONFIG, 0);
    aty_st_le32(MPP_GP_CONFIG, 0);
    aty_st_le32(SUBPIC_CNTL, 0);
    aty_st_le32(VIPH_CONTROL, 0);
    aty_st_le32(I2C_CNTL_1, 0);         /* turn off i2c */
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

    if (par->accel_flags & FB_ACCELF_TEXT)
        aty128_init_engine(par, info);
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


    /*
     *  encode/decode the User Defined Part of the Display
     */

static int
aty128_decode_var(struct fb_var_screeninfo *var, struct aty128fb_par *par,
			const struct fb_info_aty128 *info)
{
    int err;

    if ((err = aty128_var_to_crtc(var, &par->crtc, info)))
	return err;

    if ((err = aty128_var_to_pll(var->pixclock, &par->pll, info)))
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

    if ((err = aty128_crtc_to_var(&par->crtc, var)))
	return err;

    if ((err = aty128_pll_to_var(&par->pll, var, info)))
	return err;

    var->red.msb_right = 0;
    var->green.msb_right = 0;
    var->blue.msb_right = 0;
    var->transp.msb_right = 0;

    var->nonstd = 0;
    var->activate = 0;

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

    /* basic (in)sanity checks */
    if (!var->xres)
        var->xres = 1;
    if (!var->yres)
        var->yres = 1;
    if (var->xres > var->xres_virtual)
        var->xres_virtual = var->xres;
    if (var->yres > var->yres_virtual)
        var->yres_virtual = var->yres;
    if (var->bits_per_pixel <= 8)
        var->bits_per_pixel = 8;
    else if (var->bits_per_pixel <= 16)
        var->bits_per_pixel = 16;
    else if (var->bits_per_pixel <= 24)
        var->bits_per_pixel = 24;
    else if (var->bits_per_pixel <= 32)
        var->bits_per_pixel = 32;
    else
        return -EINVAL;

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
        display->screen_base = (char *)info->frame_buffer;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->can_soft_blank = 1;
	display->inverse = 0;

	accel = var->accel_flags & FB_ACCELF_TEXT;
        aty128_set_disp(display, info, par.crtc.bpp, accel);

	if (accel)
	    display->scrollmode = SCROLL_YNOMOVE;
	else
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

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (!console_fb_info || console_fb_info == &info->fb_info) {
	int vmode, cmode;

	display_info.width = var->xres;
	display_info.height = var->yres;
	display_info.depth = var->bits_per_pixel;
	display_info.pitch = (var->xres_virtual)*(var->bits_per_pixel)/8;
	if (mac_var_to_vmode(var, &vmode, &cmode))
	    display_info.mode = 0;
	else
	    display_info.mode = vmode;
	strcpy(info->fb_info.modename, aty128fb_name);
	display_info.fb_address = info->frame_buffer_phys;
	display_info.cmap_adr_address = 0;
	display_info.cmap_data_address = 0;
	display_info.disp_reg_address = info->regbase_phys;
    }
#endif

    return 0;
}


static void
aty128_set_disp(struct display *disp,
			struct fb_info_aty128 *info, int bpp, int accel)
{
    switch (bpp) {
#ifdef FBCON_HAS_CFB8
    case 8:
	info->dispsw = accel ? fbcon_aty128_8 : fbcon_cfb8;
        disp->dispsw = &info->dispsw;
	break;
#endif
#ifdef FBCON_HAS_CFB16
    case 15:
    case 16:
	info->dispsw = accel ? fbcon_aty128_16 : fbcon_cfb16;
        disp->dispsw = &info->dispsw;
	disp->dispsw_data = info->fbcon_cmap.cfb16;
	break;
#endif
#ifdef FBCON_HAS_CFB24
    case 24:
	info->dispsw = accel ? fbcon_aty128_24 : fbcon_cfb24;
        disp->dispsw = &info->dispsw;
	disp->dispsw_data = info->fbcon_cmap.cfb24;
	break;
#endif
#ifdef FBCON_HAS_CFB32
    case 32:
	info->dispsw = accel ? fbcon_aty128_32 : fbcon_cfb32;
        disp->dispsw = &info->dispsw;
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

    fix->smem_start = (long)info->frame_buffer_phys;
    fix->mmio_start = (long)info->regbase_phys;

    fix->smem_len = (u32)info->vram_size;
    fix->mmio_len = 0x1fff;

    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;
    fix->line_length = par->crtc.vxres*par->crtc.bpp/8;
    fix->visual = par->crtc.bpp <= 8 ? FB_VISUAL_PSEUDOCOLOR
                                     : FB_VISUAL_DIRECTCOLOR;
    fix->ywrapstep = 0;
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
			   struct fb_info *fb)
{
    struct fb_info_aty128 *info = (struct fb_info_aty128 *)fb;
    struct aty128fb_par *par = &info->current_par;
    u32 xoffset, yoffset;
    u32 offset;
    u32 xres, yres;

    xres = (((par->crtc.h_total >> 16) & 0xff) + 1) * 8;
    yres = ((par->crtc.v_total >> 16) & 0x7ff) + 1;

    xoffset = (var->xoffset +7) & ~7;
    yoffset = var->yoffset;

    if (xoffset+xres > par->crtc.vxres || yoffset+yres > par->crtc.vyres)
        return -EINVAL;

    par->crtc.xoffset = xoffset;
    par->crtc.yoffset = yoffset;

    offset = ((yoffset * par->crtc.vxres + xoffset) * par->crtc.bpp) >> 6;

    aty_st_le32(CRTC_OFFSET, offset);

    return 0;
}


    /*
     *  Get the Colormap
     */

static int
aty128fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info)
{
    if (con == currcon) /* current console? */
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
        int size = (disp->var.bits_per_pixel <= 8) ? 256 : 32;
	if ((err = fb_alloc_cmap(&disp->cmap, size, 0)))
	    return err;
    }

    if (con == currcon) /* current console? */
	return fb_set_cmap(cmap, kspc, aty128_setcolreg, info);
    else
	fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);

    return 0;                
}


    /*
     *  Frame Buffer Specific ioctls
     */

static int
aty128fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		     u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}


#ifndef MODULE
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
	} else if (!strncmp(this_opt, "noaccel", 7)) {
	    noaccel = 1;
	} else if (!strncmp(this_opt, "depth:", 6)) {
            unsigned int depth = simple_strtoul(this_opt+6, NULL, 0);
            switch (depth) {
            case 0 ... 8:
                initdepth = 8;
                break;
            case 9 ... 16:
                initdepth = 16;
                break;
            case 17 ... 24:
                initdepth = 24;
                break;
            case 25 ... 32:
                initdepth = 32;
                break;
            default:
                initdepth = 8;
            }
        }
#ifdef CONFIG_MTRR
        else if(!strncmp(this_opt, "nomtrr", 6)) {
            mtrr = 0;
        }
#endif /* CONFIG_MTRR */
#if defined(CONFIG_PPC)
        /* vmode and cmode depreciated */
	else if (!strncmp(this_opt, "vmode:", 6)) {
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
#endif /* CONFIG_PPC */
        else
            mode_option = this_opt;
    }
    return 0;
}
#endif /* !MODULE */


    /*
     *  Initialisation
     */

static int __init
aty128_init(struct fb_info_aty128 *info, const char *name)
{
    struct fb_var_screeninfo var;
    u32 dac;
    int j, k;
    u8 chip_rev;
    const struct aty128_chip_info *aci = &aty128_pci_probe_list[0];
    char *video_card = "Rage128";

    if (!register_test(info)) {
	printk(KERN_ERR "aty128fb: Can't write to video registers\n");
	return 0;
    }

    if (!info->vram_size)	/* may have already been probed */
	info->vram_size = aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

    chip_rev = (aty_ld_le32(CONFIG_CNTL) >> 16) & 0x1F;

    /* put a name with the face */
    while (aci->name && info->pdev->device != aci->device) { aci++; }
    video_card = (char *)aci->name;

    printk(KERN_INFO "aty128fb: %s [chip rev 0x%x] [card rev %x] ",
                      video_card, chip_rev, info->card_revision);

    if (info->vram_size % (1024 * 1024) == 0)
	printk("%dM\n", info->vram_size / (1024*1024));
    else
	printk("%dk\n", info->vram_size / 1024);

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

#ifdef MODULE
    var = default_var;
#else
    memset(&var, 0, sizeof(var));
#ifdef CONFIG_NVRAM
    if (default_vmode == VMODE_NVRAM) {
        default_vmode = nvram_read_byte(NV_VMODE);
        if (default_vmode <= 0 || default_vmode > VMODE_MAX)
            default_vmode = VMODE_CHOOSE;
    }
#endif
#ifdef CONFIG_PPC
    if (default_vmode == VMODE_CHOOSE) {
        var = default_var;
#endif /* CONFIG_PPC */

	if (!fb_find_mode(&var, &info->fb_info, mode_option, NULL, 0,
			  &defaultmode, initdepth))
	    var = default_var;

#ifdef CONFIG_PPC
#ifdef CONFIG_NVRAM
    if (default_cmode == CMODE_NVRAM)
        default_cmode = nvram_read_byte(NV_CMODE);
#endif
    } else if (_machine == _MACH_Pmac)
 	if (mac_vmode_to_var(default_vmode, default_cmode, &var))
	    var = default_var;

#endif
#endif /* MODULE */

    if (noaccel)
        var.accel_flags &= ~FB_ACCELF_TEXT;
    else
        var.accel_flags |= FB_ACCELF_TEXT;

    if (aty128_decode_var(&var, &info->default_par, info)) {
	printk(KERN_ERR "aty128fb: Cannot set default mode.\n");
	return 0;
    }

    /* load up the palette with default colors */
    for (j = 0; j < 16; j++) {
        k = color_table[j];
        info->palette[j].red = default_red[k];
        info->palette[j].green = default_grn[k];
        info->palette[j].blue = default_blu[k];
    }

    dac = aty_ld_le32(DAC_CNTL) & 15;	/* preserve lower three bits */
    dac |= DAC_8BIT_EN;			/* set 8 bit dac             */
    dac |= DAC_MASK;			/* set DAC mask              */
    aty_st_le32(DAC_CNTL, dac);

    /* turn off bus mastering, just in case */
    aty_st_le32(BUS_CNTL, aty_ld_le32(BUS_CNTL) | BUS_MASTER_DIS);

    aty128fb_set_var(&var, -1, &info->fb_info);
    aty128_init_engine(&info->default_par, info);

    board_list = aty128_board_list_add(board_list, info);

    if (register_framebuffer(&info->fb_info) < 0)
	return 0;

    printk(KERN_INFO "fb%d: %s frame buffer device on %s\n",
	   GET_FB_IDX(info->fb_info.node), aty128fb_name, name);

    return 1;	/* success! */
}


/* add a new card to the list  ++ajoshi */
static struct
fb_info_aty128 *aty128_board_list_add(struct fb_info_aty128 *board_list,
                                       struct fb_info_aty128 *new_node)
{
    struct fb_info_aty128 *i_p = board_list;

    new_node->next = NULL;
    if(board_list == NULL)
	return new_node;
    while(i_p->next != NULL)
	i_p = i_p->next;
    i_p->next = new_node;

    return board_list;
}


int __init
aty128fb_init(void)
{
#ifdef CONFIG_PCI
    struct pci_dev *pdev = NULL;
    const struct aty128_chip_info *aci = &aty128_pci_probe_list[0];

    while (aci->name != NULL) {
        pdev = pci_find_device(PCI_VENDOR_ID_ATI, aci->device, pdev);
        while (pdev != NULL) {
            if (aty128_pci_register(pdev, aci) == 0)
                return 0;
            pdev = pci_find_device(PCI_VENDOR_ID_ATI, aci->device, pdev);
        }
	aci++;
    }
#endif

    return 0;
}


#ifdef CONFIG_PCI
/* register a card    ++ajoshi */
static int __init
aty128_pci_register(struct pci_dev *pdev,
                               const struct aty128_chip_info *aci)
{
    struct fb_info_aty128 *info = NULL;
    unsigned long fb_addr, reg_addr = 0;
    u16 tmp;

    struct resource *rp;

    rp = &pdev->resource[0];
    fb_addr = rp->start;
    fb_addr &= PCI_BASE_ADDRESS_MEM_MASK;

    if (!request_mem_region(rp->start, rp->end - rp->start + 1,
                            "aty128fb FB")) {
        release_mem_region(pdev->resource[0].start,
                           pdev->resource[0].end -
                           pdev->resource[0].start + 1);
        return -1;
    }

    rp = &pdev->resource[2];
    reg_addr = rp->start;
    reg_addr &= PCI_BASE_ADDRESS_MEM_MASK;

    if (!request_mem_region(rp->start, rp->end - rp->start + 1,
                            "aty128fb MMIO"))
        goto unmap_out;

    info = kmalloc(sizeof(struct fb_info_aty128), GFP_ATOMIC);
    if(!info) {
        printk(KERN_ERR "aty128fb: can't alloc fb_info_aty128\n");
	goto unmap_out;
    }
    memset(info, 0, sizeof(struct fb_info_aty128));

    info->pdev = pdev;

    info->regbase_phys = reg_addr;
    info->regbase = ioremap(reg_addr, 0x1FFF);

    if (!info->regbase)
        goto err_out;

    pci_read_config_word(pdev, 0x08, &tmp);
    info->card_revision = tmp;

    info->vram_size = aty_ld_le32(CONFIG_MEMSIZE) & 0x03FFFFFF;

    info->frame_buffer_phys = fb_addr;
    info->frame_buffer = (unsigned long)ioremap(fb_addr, info->vram_size);

    if (!info->frame_buffer)
        goto err_out;

    pci_read_config_word(pdev, PCI_COMMAND, &tmp);
    if (!(tmp & PCI_COMMAND_MEMORY)) {
        tmp |= PCI_COMMAND_MEMORY;
        pci_write_config_word(pdev, PCI_COMMAND, tmp);
    }

#if defined(CONFIG_PPC)
    aty128_timings(info);
#else
    if (!aty128find_ROM(info)) {
        printk(KERN_INFO "Rage128 BIOS not located.  Guessing...\n");
        aty128_timings(info);
    }
    else
        aty128_get_pllinfo(info);
#endif
#ifdef CONFIG_MTRR
    if (mtrr) {
        info->mtrr.vram = mtrr_add(info->frame_buffer_phys, info->vram_size,
                                   MTRR_TYPE_WRCOMB, 1);
        info->mtrr.vram_valid = 1;
        /* let there be speed */
        printk(KERN_INFO "aty128fb: Rage128 MTRR set to ON\n");
    }
#endif /* CONFIG_MTRR */

    if (!aty128_init(info, "PCI"))
        goto err_out;

    return 0;

err_out:
    kfree(info);
unmap_out:
    release_mem_region(pdev->resource[0].start,
			pdev->resource[0].end -
			pdev->resource[0].start + 1);
    release_mem_region(pdev->resource[2].start,
			pdev->resource[2].end -
			pdev->resource[2].start + 1);

    return -1;
}
#endif /* CONFIG_PCI */


#ifndef CONFIG_PPC
static int __init
aty128find_ROM(struct fb_info_aty128 *info)
{
    u32 segstart;
    char *rom_base;
    char *rom_base1;
    char *rom;
    int stage;
    int i;
    char aty_rom_sig[] = "761295520";   /* ATI ROM Signature      */
    char R128_sig[] = "R128";           /* Rage128 ROM identifier */
    int flag = 0;

    for (segstart=0x000c0000; segstart<0x000f0000; segstart+=0x00001000) {
        stage = 1;

        rom_base = (char *) ioremap(segstart, 0x1000);
        rom_base1 = (char *) (rom_base+1);

        if ((*rom_base == 0x55) && (((*rom_base1) & 0xff) == 0xaa))
            stage = 2;

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

        /* ATI signature found.  Let's see if it's a Rage128 */
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

        printk(KERN_INFO "aty128fb: Rage128 BIOS located at segment %4.4X\n",
                         (u32)rom_base);
        info->bios_seg = rom_base;
        flag = 1;
        break;
    }
    return (flag);
}


static void __init
aty128_get_pllinfo(struct fb_info_aty128 *info)
{   
    void *bios_header;
    void *header_ptr;
    u16 bios_header_offset, pll_info_offset;
    PLL_BLOCK pll;

    bios_header = info->bios_seg + 0x48L;
    header_ptr = bios_header;

    bios_header_offset = readw(header_ptr);
    bios_header = info->bios_seg + bios_header_offset;
    bios_header += 0x30;

    header_ptr = bios_header;
    pll_info_offset = readw(header_ptr);
    header_ptr = info->bios_seg + pll_info_offset;

    memcpy_fromio(&pll, header_ptr, 50);

    info->constants.ppll_max = pll.PCLK_max_freq;
    info->constants.ppll_min = pll.PCLK_min_freq;
    info->constants.xclk = (u32)pll.XCLK;
    info->constants.ref_divider = (u32)pll.PCLK_ref_divider;
    info->constants.dotclock = (u32)pll.PCLK_ref_freq;

    aty_st_pll(PPLL_REF_DIV, info->constants.ref_divider);
    aty_pll_writeupdate(info);

    info->constants.fifo_width = 128;
    info->constants.fifo_depth = 32;

#ifdef DEBUG
    printk(KERN_DEBUG "get_pllinfo: ppll_max %d ppll_min %d xclk %d "
                      "ref_divider %d dotclock %d\n", 
                   info->constants.ppll_max, info->constants.ppll_min,
                   info->constants.xclk, info->constants.ref_divider,
                   info->constants.dotclock);
#endif

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

    /* free up to-be unused resources */
    if (info->bios_seg)
        iounmap(info->bios_seg);

    return;
}           
#endif /* ! CONFIG_PPC */


/* fill in known card constants if pll_block is not available */
static void __init
aty128_timings(struct fb_info_aty128 *info)
{
    /* TODO make an attempt at probing */
    if (!info->constants.dotclock)
        info->constants.dotclock = 2950;

    /* from documentation */
    if (!info->constants.ppll_min)
        info->constants.ppll_min = 12500;
    if (!info->constants.ppll_max)
        info->constants.ppll_max = 25000;    /* 23000 on some cards? */

#if 1
    /* XXX TODO. Calculuate properly. Fix OF's pll ideas. */
    if (!info->constants.ref_divider)
        info->constants.ref_divider = 0x3b;
    aty_st_pll(PPLL_REF_DIV, info->constants.ref_divider);
    aty_pll_writeupdate(info);

    aty_st_pll(X_MPLL_REF_FB_DIV, 0x004c4c1e);
    aty_pll_writeupdate(info);
#else
    info->constants.ref_divider = aty_ld_pll(PPLL_REF_DIV) & PPLL_REF_DIV_MASK;
#endif

    /* TODO. Calculate */
    if (!info->constants.xclk)
        info->constants.xclk = 0x1d4d;	  /* same as mclk */

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
    struct fb_info_aty128 *info = (struct fb_info_aty128 *)fb;
    struct aty128fb_par par;

    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
    	fb_get_cmap(&fb_display[currcon].cmap, 1, aty128_getcolreg, fb);

    /* set the current console */
    currcon = con;

    aty128_decode_var(&fb_display[con].var, &par, info);
    aty128_set_par(&par, info);

    aty128_set_disp(&fb_display[con], info, par.crtc.bpp,
        par.accel_flags & FB_ACCELF_TEXT);

    do_install_cmap(con, fb);

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
    struct fb_info_aty128 *info = (struct fb_info_aty128 *)fb;
    u32 col;

    if (regno > 255)
	return 1;

    red >>= 8;
    green >>= 8;
    blue >>= 8;
    info->palette[regno].red = red;
    info->palette[regno].green = green;
    info->palette[regno].blue = blue;

    /* initialize gamma ramp for hi-color+ */
    if ((info->current_par.crtc.bpp > 8) && (regno == 0)) {
        int i;

        for (i=16; i<256; i++) {
            aty_st_8(PALETTE_INDEX, i);
            col = (i << 16) | (i << 8) | i;
            aty_st_le32(PALETTE_DATA, col);
        }
    }

    /* initialize palette */
    if (info->current_par.crtc.bpp == 16)
        aty_st_8(PALETTE_INDEX, (regno << 3));
    else
        aty_st_8(PALETTE_INDEX, regno);
    col = (red << 16) | (green << 8) | blue;
    aty_st_le32(PALETTE_DATA, col);

    if (regno < 16)
	switch (info->current_par.crtc.bpp) {
#ifdef FBCON_HAS_CFB16
	case 9 ... 16:
	    info->fbcon_cmap.cfb16[regno] = (regno << 10) | (regno << 5) |
                regno;
	    break;
#endif
#ifdef FBCON_HAS_CFB24
	case 17 ... 24:
	    info->fbcon_cmap.cfb24[regno] = (regno << 16) | (regno << 8) |
		regno;
	    break;
#endif
#ifdef FBCON_HAS_CFB32
	case 25 ... 32: {
            u32 i;

            i = (regno << 8) | regno;
            info->fbcon_cmap.cfb32[regno] = (i << 16) | i;
	    break;
        }
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
aty128_rectcopy(int srcx, int srcy, int dstx, int dsty,
		u_int width, u_int height,
		struct fb_info_aty128 *info)
{
    u32 save_dp_datatype, save_dp_cntl;

    wait_for_fifo(2, info);
    save_dp_datatype = aty_ld_le32(DP_DATATYPE);
    save_dp_cntl = aty_ld_le32(DP_CNTL);

    wait_for_fifo(6, info);
    aty_st_le32(DP_DATATYPE, (0 | BRUSH_SOLIDCOLOR << 16) | SRC_DSTCOLOR);
    aty_st_le32(DP_MIX, ROP3_SRCCOPY | DP_SRC_RECT);
    aty_st_le32(DP_CNTL, DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM);
    aty_st_le32(SRC_Y_X, (srcy << 16) | srcx);
    aty_st_le32(DST_Y_X, (dsty << 16) | dstx);
    aty_st_le32(DST_HEIGHT_WIDTH, (height << 16) | width);

    wait_for_fifo(2, info);
    aty_st_le32(DP_DATATYPE, save_dp_datatype);
    aty_st_le32(DP_CNTL, save_dp_cntl); 

    info->blitter_may_be_busy = 1;

    wait_for_idle(info);
}


    /*
     * Text mode accelerated functions
     */


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
static void fbcon_aty8_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb8_putc(conp, p, c, yy, xx);
}


static void fbcon_aty8_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}


static void fbcon_aty8_clear_margins(struct vc_data *conp,
                                     struct display *p, int bottom_only)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb8_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty128_8 = {
    fbcon_cfb8_setup, fbcon_aty128_bmove, fbcon_cfb8_clear,
    fbcon_aty8_putc, fbcon_aty8_putcs, fbcon_cfb8_revc, NULL, NULL,
    fbcon_aty8_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB16
static void fbcon_aty16_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb16_putc(conp, p, c, yy, xx);
}


static void fbcon_aty16_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb16_putcs(conp, p, s, count, yy, xx);
}


static void fbcon_aty16_clear_margins(struct vc_data *conp,
                                     struct display *p, int bottom_only)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb16_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty128_16 = {
    fbcon_cfb16_setup, fbcon_aty128_bmove, fbcon_cfb16_clear,
    fbcon_aty16_putc, fbcon_aty16_putcs, fbcon_cfb16_revc, NULL, NULL,
    fbcon_aty16_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB24
static void fbcon_aty24_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb24_putc(conp, p, c, yy, xx);
}


static void fbcon_aty24_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb24_putcs(conp, p, s, count, yy, xx);
}


static void fbcon_aty24_clear_margins(struct vc_data *conp,
                                     struct display *p, int bottom_only)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb24_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty128_24 = {
    fbcon_cfb24_setup, fbcon_aty128_bmove, fbcon_cfb24_clear,
    fbcon_aty24_putc, fbcon_aty24_putcs, fbcon_cfb24_revc, NULL, NULL,
    fbcon_aty24_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB32
static void fbcon_aty32_putc(struct vc_data *conp, struct display *p,
                            int c, int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb32_putc(conp, p, c, yy, xx);
}


static void fbcon_aty32_putcs(struct vc_data *conp, struct display *p,
                             const unsigned short *s, int count,
                             int yy, int xx)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb32_putcs(conp, p, s, count, yy, xx);
}


static void fbcon_aty32_clear_margins(struct vc_data *conp,
                                     struct display *p, int bottom_only)
{
    struct fb_info_aty128 *fb = (struct fb_info_aty128 *)(p->fb_info);

    if (fb->blitter_may_be_busy)
        wait_for_idle(fb);

    fbcon_cfb32_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty128_32 = {
    fbcon_cfb32_setup, fbcon_aty128_bmove, fbcon_cfb32_clear,
    fbcon_aty32_putc, fbcon_aty32_putcs, fbcon_cfb32_revc, NULL, NULL,
    fbcon_aty32_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#if defined(MODULE)
MODULE_AUTHOR("(c)1999-2000 Brad Douglas <brad@neruo.com>, Anthony Tong "
              "<atong@uiuc.edu>");
MODULE_DESCRIPTION("FBDev driver for ATI Rage128 cards");

int __init
init_module(void)
{
    aty128fb_init();
    return 0;
}

void __exit
cleanup_module(void)
{
    struct fb_info_aty128 *info = board_list;

    while (board_list) {
        info = board_list;
        board_list = board_list->next;

        unregister_framebuffer(&info->fb_info);
#ifdef CONFIG_MTRR
        if (info->mtrr.vram_valid)
            mtrr_del(info->mtrr.vram, info->frame_buffer_phys,
                     info->vram_size);
#endif /* CONFIG_MTRR */
        iounmap(info->regbase);
        iounmap(&info->frame_buffer);

        release_mem_region(info->pdev->resource[0].start,
                           info->pdev->resource[0].end -
                           info->pdev->resource[0].start + 1);
        release_mem_region(info->pdev->resource[2].start,
                           info->pdev->resource[2].end -
                           info->pdev->resource[2].start + 1);

        kfree(info);
    }
}
#endif /* MODULE */
