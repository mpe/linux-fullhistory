/*  $Id: atyfb.c,v 1.98 1999/01/14 08:50:53 geert Exp $
 *  linux/drivers/video/atyfb.c -- Frame buffer device for ATI Mach64
 *
 *	Copyright (C) 1997-1998  Geert Uytterhoeven
 *	Copyright (C) 1998  Bernd Harries
 *	Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
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

    - cursor support on all cards and all ramdacs.
    - cursor parameters controlable via ioctl()s.
    - guess PLL and MCLK based on the original PLL register values initialized
      by the BIOS or Open Firmware (if they are initialized).

						(Anyone to help with this?)

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
#include <linux/console.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#include <linux/kd.h>
#include <linux/vt_kern.h>

#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif

#include <asm/io.h>

#if defined(CONFIG_PPC)
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <video/macmodes.h>
#endif
#ifdef __sparc__
#include <asm/pbm.h>
#include <asm/fbio.h>
#include <asm/uaccess.h>
#endif

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#include "aty.h"


/*
 * Debug flags.
 */
#undef DEBUG


#define GUI_RESERVE	0x00001000


#ifndef __powerpc__
#define eieio()		/* Enforce In-order Execution of I/O */
#endif

/* FIXME: remove the FAIL definition */
#define FAIL(x) do { printk(x "\n"); return -EINVAL; } while (0)


    /*
     *  Elements of the Hardware specific atyfb_par structure
     */

struct crtc {
    u32 vxres;
    u32 vyres;
    u32 xoffset;
    u32 yoffset;
    u32 bpp;
    u32 h_tot_disp;
    u32 h_sync_strt_wid;
    u32 v_tot_disp;
    u32 v_sync_strt_wid;
    u32 off_pitch;
    u32 gen_cntl;
    u32 dp_pix_width;	/* acceleration */
    u32 dp_chain_mask;	/* acceleration */
};

struct pll_gx {
    u8 m;
    u8 n;
};

struct pll_ct {
    u8 pll_ref_div;
    u8 pll_gen_cntl;
    u8 mclk_fb_div;
    u8 pll_vclk_cntl;
    u8 vclk_post_div;
    u8 vclk_fb_div;
    u8 pll_ext_cntl;
    u32 dsp_config;	/* Mach64 GTB DSP */
    u32 dsp_on_off;	/* Mach64 GTB DSP */
};


    /*
     *  The Hardware parameters for each card
     */

struct atyfb_par {
    struct crtc crtc;
    union {
	struct pll_gx gx;
	struct pll_ct ct;
    } pll;
    u32 accel_flags;
};

struct aty_cmap_regs {
    u8 windex;
    u8 lut;
    u8 mask;
    u8 rindex;
    u8 cntl;
};

struct pci_mmap_map {
    unsigned long voff;
    unsigned long poff;
    unsigned long size;
    unsigned long prot_flag;
    unsigned long prot_mask;
};

#define DEFAULT_CURSOR_BLINK_RATE	(20)
#define CURSOR_DRAW_DELAY		(2)

struct aty_cursor {
    int	enable;
    int on;
    int vbl_cnt;
    int blink_rate;
    u32 offset;
    struct {
        u16 x, y;
    } pos, hot, size;
    u32 color[2];
    u8 bits[8][64];
    u8 mask[8][64];
    u8 *ram;
    struct timer_list *timer;
};

struct fb_info_aty {
    struct fb_info fb_info;
    unsigned long ati_regbase_phys;
    unsigned long ati_regbase;
    unsigned long frame_buffer_phys;
    unsigned long frame_buffer;
    struct pci_mmap_map *mmap_map;
    struct aty_cursor *cursor;
    struct aty_cmap_regs *aty_cmap_regs;
    struct { u8 red, green, blue, pad; } palette[256];
    struct atyfb_par default_par;
    struct atyfb_par current_par;
    u32 total_vram;
    u32 pll_per;
    u32 mclk_per;
    u16 chip_type;
#define Gx info->chip_type
    u8 chip_rev;
#define Rev info->chip_rev
    u8 bus_type;
    u8 ram_type;
    u8 dac_type;
    u8 clk_type;
    u8 mem_refresh_rate;
    struct display disp;
    struct display_switch dispsw;
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
    u8 blitter_may_be_busy;
#ifdef __sparc__
    u8 open;
    u8 mmaped;
    int vtconsole;
    int consolecnt;
#endif
};


    /*
     *  Frame buffer device API
     */

static int atyfb_open(struct fb_info *info, int user);
static int atyfb_release(struct fb_info *info, int user);
static int atyfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *fb);
static int atyfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *fb);
static int atyfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *fb);
static int atyfb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *fb);
static int atyfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int atyfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int atyfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info);
#ifdef __sparc__
static int atyfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma);
#endif


    /*
     *  Interface to the low level console driver
     */

static int atyfbcon_switch(int con, struct fb_info *fb);
static int atyfbcon_updatevar(int con, struct fb_info *fb);
static void atyfbcon_blank(int blank, struct fb_info *fb);


    /*
     *  Text console acceleration
     */

static void fbcon_aty_bmove(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width);
static void fbcon_aty_clear(struct vc_data *conp, struct display *p, int sy,
			    int sx, int height, int width);
#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_aty8;
static void fbcon_aty8_putc(struct vc_data *conp, struct display *p, int c,
			    int yy, int xx);
static void fbcon_aty8_putcs(struct vc_data *conp, struct display *p,
			     const unsigned short *s, int count, int yy,
			     int xx);
#endif
#ifdef FBCON_HAS_CFB16
static struct display_switch fbcon_aty16;
static void fbcon_aty16_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx);
static void fbcon_aty16_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy,
			      int xx);
#endif
#ifdef FBCON_HAS_CFB24
static struct display_switch fbcon_aty24;
static void fbcon_aty24_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx);
static void fbcon_aty24_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy,
			      int xx);
#endif
#ifdef FBCON_HAS_CFB32
static struct display_switch fbcon_aty32;
static void fbcon_aty32_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx);
static void fbcon_aty32_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy,
			      int xx);
#endif


    /*
     *  Internal routines
     */

static int aty_init(struct fb_info_aty *info, const char *name);
static struct aty_cursor *aty_init_cursor(struct fb_info_aty *fb);
#ifdef CONFIG_ATARI
static int store_video_par(char *videopar, unsigned char m64_num);
static char *strtoke(char *s, const char *ct);
#endif

static void reset_engine(const struct fb_info_aty *info);
static void init_engine(const struct atyfb_par *par, struct fb_info_aty *info);
static void aty_st_514(int offset, u8 val, const struct fb_info_aty *info);
static void aty_st_pll(int offset, u8 val, const struct fb_info_aty *info);
#if defined(__sparc__) || defined(DEBUG)
static u8 aty_ld_pll(int offset, const struct fb_info_aty *info);
#endif
static void aty_set_crtc(const struct fb_info_aty *info,
			 const struct crtc *crtc);
static int aty_var_to_crtc(const struct fb_info_aty *info,
			   const struct fb_var_screeninfo *var,
			   struct crtc *crtc);
static void aty_set_dac_514(const struct fb_info_aty *info, u32 bpp);
static int aty_crtc_to_var(const struct crtc *crtc,
			   struct fb_var_screeninfo *var);
static void aty_set_pll_gx(const struct fb_info_aty *info,
			   const struct pll_gx *pll);
static int aty_var_to_pll_18818(u32 vclk_per, struct pll_gx *pll);
static int aty_var_to_pll_514(u32 vclk_per, struct pll_gx *pll);
static int aty_pll_gx_to_var(const struct pll_gx *pll, u32 *vclk_per);
static void aty_set_pll_ct(const struct fb_info_aty *info,
			   const struct pll_ct *pll);
static int aty_dsp_gt(const struct fb_info_aty *info, u8 mclk_fb_div,
		      u8 mclk_post_div, u8 vclk_fb_div, u8 vclk_post_div,
		      u8 bpp, struct pll_ct *pll);
static int aty_var_to_pll_ct(const struct fb_info_aty *info, u32 vclk_per,
			     u8 bpp, struct pll_ct *pll);
static int aty_pll_ct_to_var(const struct pll_ct *pll, u32 *vclk_per);
static void atyfb_set_par(const struct atyfb_par *par,
			  struct fb_info_aty *info);
static int atyfb_decode_var(const struct fb_var_screeninfo *var,
			    struct atyfb_par *par,
			    const struct fb_info_aty *info);
static int atyfb_encode_var(struct fb_var_screeninfo *var,
			    const struct atyfb_par *par,
			    const struct fb_info_aty *info);
static void set_off_pitch(struct atyfb_par *par,
			  const struct fb_info_aty *info);
static int encode_fix(struct fb_fix_screeninfo *fix,
		      const struct atyfb_par *par,
		      const struct fb_info_aty *info);
static void atyfb_set_disp(struct display *disp, struct fb_info_aty *info,
			   int bpp, int accel);
static int atyfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			 u_int *transp, struct fb_info *fb);
static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *fb);
static void do_install_cmap(int con, struct fb_info *info);
#if defined(CONFIG_PPC)
static int read_aty_sense(const struct fb_info_aty *info);
#endif


    /*
     *  Interface used by the world
     */

void atyfb_init(void);
#ifdef CONFIG_FB_OF
void atyfb_of_init(struct device_node *dp);
#endif
void atyfb_setup(char *options, int *ints);


static int currcon = 0;

static struct fb_ops atyfb_ops = {
    atyfb_open, atyfb_release, atyfb_get_fix, atyfb_get_var, atyfb_set_var,
    atyfb_get_cmap, atyfb_set_cmap, atyfb_pan_display, atyfb_ioctl,
#ifdef __sparc__
    atyfb_mmap
#else
    NULL
#endif
};

static char atyfb_name[16] = "ATY Mach64";
static char fontname[40] __initdata = { 0 };
static char curblink __initdata = 1;
static char noaccel __initdata = 0;
static u32 default_vram __initdata = 0;
static int default_pll __initdata = 0;
static int default_mclk __initdata = 0;

static const u32 ref_clk_per = 1000000000000ULL/14318180;

#if defined(CONFIG_PPC)
static int default_vmode __initdata = VMODE_NVRAM;
static int default_cmode __initdata = CMODE_NVRAM;
#endif

#ifdef CONFIG_ATARI
static unsigned int mach64_count __initdata = 0;
static unsigned long phys_vmembase[FB_MAX] __initdata = { 0, };
static unsigned long phys_size[FB_MAX] __initdata = { 0, };
static unsigned long phys_guiregbase[FB_MAX] __initdata = { 0, };
#endif


static struct aty_features {
    u16 pci_id;
    u16 chip_type;
    const char *name;
} aty_features[] __initdata = {
    /* mach64GX family */
    { 0x4758, 0x00d7, "mach64GX (ATI888GX00)" },
    { 0x4358, 0x0057, "mach64CX (ATI888CX00)" },

    /* mach64CT family */
    { 0x4354, 0x4354, "mach64CT (ATI264CT)" },
    { 0x4554, 0x4554, "mach64ET (ATI264ET)" },

    /* mach64CT family / mach64VT class */
    { 0x5654, 0x5654, "mach64VT (ATI264VT)" },
    { 0x5655, 0x5655, "mach64VTB (ATI264VTB)" },
    { 0x5656, 0x5656, "mach64VT4 (ATI264VT4)" },

    /* mach64CT family / mach64GT (3D RAGE) class */
    { 0x4c42, 0x4c42, "3D RAGE LT PRO (AGP)" },
    { 0x4c42, 0x4c44, "3D RAGE LT PRO" },
    { 0x4c42, 0x4c47, "3D RAGE LT PRO" },
    { 0x4c42, 0x4c49, "3D RAGE LT PRO" },
    { 0x4c42, 0x4c50, "3D RAGE LT PRO" },
    { 0x4c54, 0x4c54, "3D RAGE LT" },
    { 0x4754, 0x4754, "3D RAGE (GT)" },
    { 0x4755, 0x4755, "3D RAGE II+ (GTB)" },
    { 0x4756, 0x4756, "3D RAGE IIC (PCI)" },
    { 0x4757, 0x4757, "3D RAGE IIC (AGP)" },
    { 0x475a, 0x475a, "3D RAGE IIC (AGP)" },
    { 0x4742, 0x4742, "3D RAGE PRO (BGA, AGP)" },
    { 0x4744, 0x4744, "3D RAGE PRO (BGA, AGP, 1x only)" },
    { 0x4749, 0x4749, "3D RAGE PRO (BGA, PCI)" },
    { 0x4750, 0x4750, "3D RAGE PRO (PQFP, PCI)" },
    { 0x4751, 0x4751, "3D RAGE PRO (PQFP, PCI, limited 3D)" },
};

static const char *aty_gx_ram[8] __initdata = {
    "DRAM", "VRAM", "VRAM", "DRAM", "DRAM", "VRAM", "VRAM", "RESV"
};

static const char *aty_ct_ram[8] __initdata = {
    "OFF", "DRAM", "EDO", "EDO", "SDRAM", "SGRAM", "WRAM", "RESV"
};


static inline u32 aty_ld_le32(volatile unsigned int regindex,
			      const struct fb_info_aty *info)
{
    unsigned long temp;
    u32 val;

#if defined(__powerpc__)
    temp = info->ati_regbase;
    asm("lwbrx %0,%1,%2" : "=r"(val) : "r" (regindex), "r" (temp));
#elif defined(__sparc_v9__)
    temp = info->ati_regbase + regindex;
    asm("lduwa [%1] %2, %0" : "=r" (val) : "r" (temp), "i" (ASI_PL));
#else
    temp = info->ati_regbase+regindex;
    val = le32_to_cpu(*((volatile u32 *)(temp)));
#endif
    return val;
}

static inline void aty_st_le32(volatile unsigned int regindex, u32 val,
			       const struct fb_info_aty *info)
{
    unsigned long temp;

#if defined(__powerpc__)
    temp = info->ati_regbase;
    asm("stwbrx %0,%1,%2" : : "r" (val), "r" (regindex), "r" (temp) :
	"memory");
#elif defined(__sparc_v9__)
    temp = info->ati_regbase + regindex;
    asm("stwa %0, [%1] %2" : : "r" (val), "r" (temp), "i" (ASI_PL) : "memory");
#else
    temp = info->ati_regbase+regindex;
    *((volatile u32 *)(temp)) = cpu_to_le32(val);
#endif
}

static inline u8 aty_ld_8(volatile unsigned int regindex,
			  const struct fb_info_aty *info)
{
    return *(volatile u8 *)(info->ati_regbase+regindex);
}

static inline void aty_st_8(volatile unsigned int regindex, u8 val,
			    const struct fb_info_aty *info)
{
    *(volatile u8 *)(info->ati_regbase+regindex) = val;
}


    /*
     *  Generic Mach64 routines
     */

    /*
     *  All writes to draw engine registers are automatically routed through a
     *  32-bit-wide, 16-entry-deep command FIFO ...
     *  Register writes to registers with DWORD offsets less than 40h are not
     *  FIFOed.
     *  (from Chapter 5 of the Mach64 Programmer's Guide)
     */

static inline void wait_for_fifo(u16 entries, const struct fb_info_aty *info)
{
    while ((aty_ld_le32(FIFO_STAT, info) & 0xffff) >
	   ((u32)(0x8000 >> entries)));
}

static inline void wait_for_idle(struct fb_info_aty *info)
{
    wait_for_fifo(16, info);
    while ((aty_ld_le32(GUI_STAT, info) & 1)!= 0);
    info->blitter_may_be_busy = 0;
}

static void reset_engine(const struct fb_info_aty *info)
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
    pitch_value = par->crtc.vxres;

    if (par->crtc.bpp == 24) {
	/* In 24 bpp, the engine is in 8 bpp - this requires that all */
	/* horizontal coordinates and widths must be adjusted */
	pitch_value = pitch_value * 3;
    }

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
    aty_st_le32(SC_BOTTOM, par->crtc.vyres-1, info);
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
    aty_st_le32(DP_PIX_WIDTH, par->crtc.dp_pix_width, info);
    aty_st_le32(DP_CHAIN_MASK, par->crtc.dp_chain_mask, info);

    wait_for_fifo(5, info);
    aty_st_le32(SCALE_3D_CNTL, 0, info);
    aty_st_le32(Z_CNTL, 0, info);
    aty_st_le32(CRTC_INT_CNTL, aty_ld_le32(CRTC_INT_CNTL, info) & ~0x20, info);
    aty_st_le32(GUI_TRAJ_CNTL, 0x100023, info);

    /* insure engine is idle before leaving */
    wait_for_idle(info);
}

static void aty_st_514(int offset, u8 val, const struct fb_info_aty *info)
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

static void aty_st_pll(int offset, u8 val, const struct fb_info_aty *info)
{
    /* write addr byte */
    aty_st_8(CLOCK_CNTL + 1, (offset << 2) | PLL_WR_EN, info);
    eieio();
    /* write the register value */
    aty_st_8(CLOCK_CNTL + 2, val, info);
    eieio();
    aty_st_8(CLOCK_CNTL + 1, (offset << 2) & ~PLL_WR_EN, info);
}

#if defined(__sparc__) || defined(DEBUG)
static u8 aty_ld_pll(int offset, const struct fb_info_aty *info)
{
    u8 res;

    /* write addr byte */
    aty_st_8(CLOCK_CNTL + 1, (offset << 2), info);
    eieio();
    /* read the register value */
    res = aty_ld_8(CLOCK_CNTL + 2, info);
    eieio();
    return res;
}
#endif

#if defined(CONFIG_PPC)

    /*
     *  Apple monitor sense
     */

static int read_aty_sense(const struct fb_info_aty *info)
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

#endif /* defined(CONFIG_PPC) */

/* ------------------------------------------------------------------------- */

    /*
     *  Hardware Cursor support.
     */

static u8 cursor_pixel_map[2] = { 0, 15 };
static u8 cursor_color_map[2] = { 0, 0xff };

static u8 cursor_bits_lookup[16] =
{
	0x00, 0x40, 0x10, 0x50, 0x04, 0x44, 0x14, 0x54,
	0x01, 0x41, 0x11, 0x51, 0x05, 0x45, 0x15, 0x55
};

static u8 cursor_mask_lookup[16] =
{
	0xaa, 0x2a, 0x8a, 0x0a, 0xa2, 0x22, 0x82, 0x02,
	0xa8, 0x28, 0x88, 0x08, 0xa0, 0x20, 0x80, 0x00
};

static void
aty_set_cursor_color(struct fb_info_aty *fb, u8 *pixel,
		     u8 *red, u8 *green, u8 *blue)
{
	struct aty_cursor *c = fb->cursor;
	int i;

	if (!c)
		return;

#ifdef __sparc__
	if (fb->mmaped && currcon == fb->vtconsole)
		return;
#endif

	for (i = 0; i < 2; i++) {
		c->color[i] =  (u32)red[i] << 24;
		c->color[i] |= (u32)green[i] << 16;
 		c->color[i] |= (u32)blue[i] <<  8;
 		c->color[i] |= (u32)pixel[i];
	}

	wait_for_fifo(2, fb);
	aty_st_le32(CUR_CLR0, c->color[0], fb);
	aty_st_le32(CUR_CLR1, c->color[1], fb);
}

static void
aty_set_cursor_shape(struct fb_info_aty *fb)
{
	struct aty_cursor *c = fb->cursor;
	u8 *ram, m, b;
	int x, y;

	if (!c)
		return;

#ifdef __sparc__
	if (fb->mmaped && currcon == fb->vtconsole)
		return;
#endif

	ram = c->ram;
	for (y = 0; y < c->size.y; y++) {
		for (x = 0; x < c->size.x >> 2; x++) {
			m = c->mask[x][y];
			b = c->bits[x][y];
			*ram++ = cursor_mask_lookup[m >> 4] |
				 cursor_bits_lookup[(b & m) >> 4];
			*ram++ = cursor_mask_lookup[m & 0x0f] |
				 cursor_bits_lookup[(b & m) & 0x0f];
		}
		for ( ; x < 8; x++) {
			*ram++ = 0xaa;
			*ram++ = 0xaa;
		}
	}
	memset(ram, 0xaa, (64 - c->size.y) * 16);
}

static void
aty_set_cursor(struct fb_info_aty *fb, int on)
{
	struct atyfb_par *par = &fb->current_par;
	struct aty_cursor *c = fb->cursor;
	u16 xoff, yoff;
	int x, y;

	if (!c)
		return;

#ifdef __sparc__
	if (fb->mmaped && currcon == fb->vtconsole)
		return;
#endif

	if (on) {
		x = c->pos.x - c->hot.x - par->crtc.xoffset;
		if (x < 0) {
			xoff = -x;
			x = 0;
		} else {
			xoff = 0;
		}

		y = c->pos.y - c->hot.y - par->crtc.yoffset;
		if (y < 0) {
			yoff = -y;
			y = 0;
		} else {
			yoff = 0;
		}

		wait_for_fifo(4, fb);
		aty_st_le32(CUR_OFFSET, (c->offset >> 3) + (yoff << 1), fb);
		aty_st_le32(CUR_HORZ_VERT_OFF,
			    ((u32)(64 - c->size.y + yoff) << 16) | xoff, fb);
		aty_st_le32(CUR_HORZ_VERT_POSN, ((u32)y << 16) | x, fb);
		aty_st_le32(GEN_TEST_CNTL, aty_ld_le32(GEN_TEST_CNTL, fb)
						       | HWCURSOR_ENABLE, fb);
	} else {
		wait_for_fifo(1, fb);
		aty_st_le32(GEN_TEST_CNTL,
			    aty_ld_le32(GEN_TEST_CNTL, fb) & ~HWCURSOR_ENABLE,
			    fb);
	}
	if (fb->blitter_may_be_busy)
		wait_for_idle(fb);
}

static void
aty_cursor_timer_handler(unsigned long dev_addr)
{
	struct fb_info_aty *fb = (struct fb_info_aty *)dev_addr;

	if (!fb->cursor)
		return;

	if (!fb->cursor->enable)
		goto out;

	if (fb->cursor->vbl_cnt && --fb->cursor->vbl_cnt == 0) {
		fb->cursor->on ^= 1;
		aty_set_cursor(fb, fb->cursor->on);
		fb->cursor->vbl_cnt = fb->cursor->blink_rate;
	}

out:
	fb->cursor->timer->expires = jiffies + (HZ / 50);
	add_timer(fb->cursor->timer);
}

static void
atyfb_cursor(struct display *p, int mode, int x, int y)
{
	struct fb_info_aty *fb = (struct fb_info_aty *)p->fb_info;
	struct aty_cursor *c = fb->cursor;

	if (!c)
		return;

#ifdef __sparc__
	if (fb->mmaped && currcon == fb->vtconsole)
		return;
#endif

	x *= fontwidth(p);
	y *= fontheight(p);
	if (c->pos.x == x && c->pos.y == y && (mode == CM_ERASE) == !c->enable)
		return;

	c->enable = 0;
	if (c->on)
		aty_set_cursor(fb, 0);
	c->pos.x = x;
	c->pos.y = y;

	switch (mode) {
	case CM_ERASE:
		c->on = 0;
		break;

	case CM_DRAW:
	case CM_MOVE:
		if (c->on)
			aty_set_cursor(fb, 1);
		else
			c->vbl_cnt = CURSOR_DRAW_DELAY;
		c->enable = 1;
		break;
	}
}

__initfunc(static struct aty_cursor *
aty_init_cursor(struct fb_info_aty *fb))
{
	struct aty_cursor *cursor;
	unsigned long addr;

	cursor = kmalloc(sizeof(struct aty_cursor), GFP_ATOMIC);
	if (!cursor)
		return 0;
	memset(cursor, 0, sizeof(*cursor));

	cursor->timer = kmalloc(sizeof(*cursor->timer), GFP_KERNEL);
	if (!cursor->timer) {
		kfree(cursor);
		return 0;
	}
	memset(cursor->timer, 0, sizeof(*cursor->timer));

	cursor->blink_rate = DEFAULT_CURSOR_BLINK_RATE;
	fb->total_vram -= PAGE_SIZE;
	cursor->offset = fb->total_vram;

#ifdef __sparc__
	addr = fb->frame_buffer - 0x800000 + cursor->offset;
	cursor->ram = (u8 *)addr;
#else
#ifdef __BIG_ENDIAN
	addr = fb->frame_buffer_phys - 0x800000 + cursor->offset;
	cursor->ram = (u8 *)ioremap(addr, 1024);
#else
	addr = fb->frame_buffer + cursor->offset;
	cursor->ram = (u8 *)addr;
#endif
#endif

	if (! cursor->ram) {
		kfree(cursor);
		return NULL;
	}

	if (curblink) {
		init_timer(cursor->timer);
		cursor->timer->expires = jiffies + (HZ / 50);
		cursor->timer->data = (unsigned long)fb;
		cursor->timer->function = aty_cursor_timer_handler;
		add_timer(cursor->timer);
	}

	return cursor;
}

static int
atyfb_set_font(struct display *d, int width, int height)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)d->fb_info;
    struct aty_cursor *c = fb->cursor;
    int i, j;

    if (c) {
	if (!width || !height) {
	    width = 8;
	    height = 16;
	}

	c->hot.x = 0;
	c->hot.y = 0;
	c->size.x = width;
	c->size.y = height;

	memset(c->bits, 0xff, sizeof(c->bits));
	memset(c->mask, 0, sizeof(c->mask));

	for (i = 0, j = width; j >= 0; j -= 8, i++) {
	    c->mask[i][height-2] = (j >= 8) ? 0xff : (0xff << (8 - j));
	    c->mask[i][height-1] = (j >= 8) ? 0xff : (0xff << (8 - j));
	}

	aty_set_cursor_color(fb, cursor_pixel_map, cursor_color_map,
			     cursor_color_map, cursor_color_map);
	aty_set_cursor_shape(fb);
    }
    return 1;
}




/* ------------------------------------------------------------------------- */

    /*
     *  CRTC programming
     */

static void aty_set_crtc(const struct fb_info_aty *info,
			 const struct crtc *crtc)
{
    aty_st_le32(CRTC_H_TOTAL_DISP, crtc->h_tot_disp, info);
    aty_st_le32(CRTC_H_SYNC_STRT_WID, crtc->h_sync_strt_wid, info);
    aty_st_le32(CRTC_V_TOTAL_DISP, crtc->v_tot_disp, info);
    aty_st_le32(CRTC_V_SYNC_STRT_WID, crtc->v_sync_strt_wid, info);
    aty_st_le32(CRTC_VLINE_CRNT_VLINE, 0, info);
    aty_st_le32(CRTC_OFF_PITCH, crtc->off_pitch, info);
    aty_st_le32(CRTC_GEN_CNTL, crtc->gen_cntl, info);
}

static int aty_var_to_crtc(const struct fb_info_aty *info,
			   const struct fb_var_screeninfo *var,
			   struct crtc *crtc)
{
    u32 xres, yres, vxres, vyres, xoffset, yoffset, bpp;
    u32 left, right, upper, lower, hslen, vslen, sync, vmode;
    u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid, h_sync_pol;
    u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
    u32 pix_width, dp_pix_width, dp_chain_mask;

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

    /* convert (and round up) and validate */
    xres = (xres+7) & ~7;
    xoffset = (xoffset+7) & ~7;
    vxres = (vxres+7) & ~7;
    if (vxres < xres+xoffset)
	vxres = xres+xoffset;
    h_disp = xres/8-1;
    if (h_disp > 0xff)
	FAIL("h_disp too large");
    h_sync_strt = h_disp+(right/8);
    if (h_sync_strt > 0x1ff)
	FAIL("h_sync_start too large");
    h_sync_dly = right & 7;
    h_sync_wid = (hslen+7)/8;
    if (h_sync_wid > 0x1f)
	FAIL("h_sync_wid too large");
    h_total = h_sync_strt+h_sync_wid+(h_sync_dly+left+7)/8;
    if (h_total > 0x1ff)
	FAIL("h_total too large");
    h_sync_pol = sync & FB_SYNC_HOR_HIGH_ACT ? 0 : 1;

    if (vyres < yres+yoffset)
	vyres = yres+yoffset;
    v_disp = yres-1;
    if (v_disp > 0x7ff)
	FAIL("v_disp too large");
    v_sync_strt = v_disp+lower;
    if (v_sync_strt > 0x7ff)
	FAIL("v_sync_strt too large");
    v_sync_wid = vslen;
    if (v_sync_wid > 0x1f)
	FAIL("v_sync_wid too large");
    v_total = v_sync_strt+v_sync_wid+upper;
    if (v_total > 0x7ff)
	FAIL("v_total too large");
    v_sync_pol = sync & FB_SYNC_VERT_HIGH_ACT ? 0 : 1;

    c_sync = sync & FB_SYNC_COMP_HIGH_ACT ? CRTC_CSYNC_EN : 0;

    if (bpp <= 8) {
	bpp = 8;
	pix_width = CRTC_PIX_WIDTH_8BPP;
	dp_pix_width = HOST_8BPP | SRC_8BPP | DST_8BPP | BYTE_ORDER_LSB_TO_MSB;
	dp_chain_mask = 0x8080;
    } else if (bpp <= 16) {
	bpp = 16;
	pix_width = CRTC_PIX_WIDTH_15BPP;
	dp_pix_width = HOST_15BPP | SRC_15BPP | DST_15BPP |
		       BYTE_ORDER_LSB_TO_MSB;
	dp_chain_mask = 0x4210;
    } else if ((bpp <= 24) && (Gx != GX_CHIP_ID) && (Gx != CX_CHIP_ID)) {
	bpp = 24;
	pix_width = CRTC_PIX_WIDTH_24BPP;
	dp_pix_width = HOST_8BPP | SRC_8BPP | DST_8BPP | BYTE_ORDER_LSB_TO_MSB;
	dp_chain_mask = 0x8080;
    } else if (bpp <= 32) {
	bpp = 32;
	pix_width = CRTC_PIX_WIDTH_32BPP;
	dp_pix_width = HOST_32BPP | SRC_32BPP | DST_32BPP |
		       BYTE_ORDER_LSB_TO_MSB;
	dp_chain_mask = 0x8080;
    } else
	FAIL("invalid bpp");

    if (vxres*vyres*bpp/8 > info->total_vram)
	FAIL("not enough video RAM");

    if ((vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
	FAIL("invalid vmode");

    /* output */
    crtc->vxres = vxres;
    crtc->vyres = vyres;
    crtc->xoffset = xoffset;
    crtc->yoffset = yoffset;
    crtc->bpp = bpp;
    crtc->h_tot_disp = h_total | (h_disp<<16);
    crtc->h_sync_strt_wid = (h_sync_strt & 0xff) | (h_sync_dly<<8) |
			    ((h_sync_strt & 0x100)<<4) | (h_sync_wid<<16) |
			    (h_sync_pol<<21);
    crtc->v_tot_disp = v_total | (v_disp<<16);
    crtc->v_sync_strt_wid = v_sync_strt | (v_sync_wid<<16) | (v_sync_pol<<21);
    crtc->off_pitch = ((yoffset*vxres+xoffset)*bpp/64) | (vxres<<19);
    crtc->gen_cntl = pix_width | c_sync | CRTC_EXT_DISP_EN | CRTC_ENABLE;
    if ((Gx == CT_CHIP_ID) || (Gx == ET_CHIP_ID) ||
	((Gx == VT_CHIP_ID || Gx == GT_CHIP_ID) && !(Rev & 0x07))) {
	/* Not VTB/GTB */
	/* FIXME: magic FIFO values */
	crtc->gen_cntl |= aty_ld_le32(CRTC_GEN_CNTL, info) & 0x000e0000;
    }
    crtc->dp_pix_width = dp_pix_width;
    crtc->dp_chain_mask = dp_chain_mask;

    return 0;
}

static void aty_set_dac_514(const struct fb_info_aty *info, u32 bpp)
{
    static struct {
	u8 pixel_dly;
	u8 misc2_cntl;
	u8 pixel_rep;
	u8 pixel_cntl_index;
	u8 pixel_cntl_v1;
    } tab[3] = {
	{ 0, 0x41, 0x03, 0x71, 0x45 },	/* 8 bpp */
	{ 0, 0x45, 0x04, 0x0c, 0x01 },	/* 555 */
	{ 0, 0x45, 0x06, 0x0e, 0x00 },	/* XRGB */
    };
    int i;

    switch (bpp) {
	case 8:
	default:
	    i = 0;
	    break;
	case 16:
	    i = 1;
	    break;
	case 32:
	    i = 2;
	    break;
    }
    aty_st_514(0x90, 0x00, info);		/* VRAM Mask Low */
    aty_st_514(0x04, tab[i].pixel_dly, info);	/* Horizontal Sync Control */
    aty_st_514(0x05, 0x00, info);		/* Power Management */
    aty_st_514(0x02, 0x01, info);		/* Misc Clock Control */
    aty_st_514(0x71, tab[i].misc2_cntl, info);	/* Misc Control 2 */
    aty_st_514(0x0a, tab[i].pixel_rep, info);	/* Pixel Format */
    aty_st_514(tab[i].pixel_cntl_index, tab[i].pixel_cntl_v1, info);
			/* Misc Control 2 / 16 BPP Control / 32 BPP Control */
}

static int aty_crtc_to_var(const struct crtc *crtc,
			   struct fb_var_screeninfo *var)
{
    u32 xres, yres, bpp, left, right, upper, lower, hslen, vslen, sync;
    u32 h_total, h_disp, h_sync_strt, h_sync_dly, h_sync_wid, h_sync_pol;
    u32 v_total, v_disp, v_sync_strt, v_sync_wid, v_sync_pol, c_sync;
    u32 pix_width;

    /* input */
    h_total = crtc->h_tot_disp & 0x1ff;
    h_disp = (crtc->h_tot_disp>>16) & 0xff;
    h_sync_strt = (crtc->h_sync_strt_wid & 0xff) |
		  ((crtc->h_sync_strt_wid>>4) & 0x100);
    h_sync_dly = (crtc->h_sync_strt_wid>>8) & 0x7;
    h_sync_wid = (crtc->h_sync_strt_wid>>16) & 0x1f;
    h_sync_pol = (crtc->h_sync_strt_wid>>21) & 0x1;
    v_total = crtc->v_tot_disp & 0x7ff;
    v_disp = (crtc->v_tot_disp>>16) & 0x7ff;
    v_sync_strt = crtc->v_sync_strt_wid & 0x7ff;
    v_sync_wid = (crtc->v_sync_strt_wid>>16) & 0x1f;
    v_sync_pol = (crtc->v_sync_strt_wid>>21) & 0x1;
    c_sync = crtc->gen_cntl & CRTC_CSYNC_EN ? 1 : 0;
    pix_width = crtc->gen_cntl & CRTC_PIX_WIDTH_MASK;

    /* convert */
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
	case CRTC_PIX_WIDTH_15BPP:	/* RGB 555 */
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
#if 0
	case CRTC_PIX_WIDTH_16BPP:	/* RGB 565 */
	    bpp = 16;
	    var->red.offset = 11;
	    var->red.length = 5;
	    var->green.offset = 6;
	    var->green.length = 6;
	    var->blue.offset = 0;
	    var->blue.length = 5;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
#endif
	case CRTC_PIX_WIDTH_24BPP:	/* RGB 888 */
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
	case CRTC_PIX_WIDTH_32BPP:	/* ARGB 8888 */
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
	    FAIL("Invalid pixel width");
    }

    /* output */
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

    return 0;
}

/* ------------------------------------------------------------------------- */

    /*
     *  PLL programming (Mach64 GX family)
     *
     *  FIXME: use function pointer tables instead of switch statements
     */

static void aty_set_pll_gx(const struct fb_info_aty *info,
			   const struct pll_gx *pll)
{
    switch (info->clk_type) {
	case CLK_ATI18818_1:
	    aty_st_8(CLOCK_CNTL, pll->m, info);
	    break;
	case CLK_IBMRGB514:
	    aty_st_514(0x06, 0x02, info);	/* DAC Operation */
	    aty_st_514(0x10, 0x01, info);	/* PLL Control 1 */
	    aty_st_514(0x70, 0x01, info);	/* Misc Control 1 */
	    aty_st_514(0x8f, 0x1f, info);	/* PLL Ref. Divider Input */
	    aty_st_514(0x03, 0x00, info);	/* Sync Control */
	    aty_st_514(0x05, 0x00, info);	/* Power Management */
	    aty_st_514(0x20, pll->m, info);	/* F0 / M0 */
	    aty_st_514(0x21, pll->n, info);	/* F1 / N0 */
	    break;
    }
}

static int aty_var_to_pll_18818(u32 vclk_per, struct pll_gx *pll)
{
    /*
     *  FIXME: use real calculations instead of using fixed values from the old
     *	       driver
     */
    static struct {
	u32 ps_lim;	/* pixclock period rounding limit (arbitrary) */
	u8 mode;	/* (prescsaler << 4) | Select */
	u8 prog;	/* ref_div_count */
    } ATI18818_clocks[] = {
	{  7500, 0x0B, 1 },	/*  7407.4 ps = 135.00 MHz */
	{  9000, 0x0A, 1 },	/*  7936.5 ps = 126.00 MHz */
	{ 11000, 0x09, 1 },	/* 10000.0 ps = 100.00 MHz */
	{ 12800, 0x0D, 1 },	/* 12500.0 ps =  80.00 MHz */
	{ 13500, 0x0E, 1 },	/* 13333.3 ps =  75.00 MHz */
/*	{ 14000, 0x03, 2 },*/	/* 13888.8 ps =  72.00 MHz */
	{ 15000, 0x1B, 1 },	/* 14814.8 ps =  67.50 MHz */
	{ 15500, 0x0F, 1 },	/* 15384.6 ps =  65.00 MHz */
	{ 16000, 0x1A, 1 },	/* 15873.0 ps =  63.00 MHz */
/*	{ 16000, 0x02, 2 },*/	/* 15873.0 ps =  63.00 MHz */
/*	{ 18000, 0x01, 2 },*/	/* 17655.4 ps =  56.64 MHz */
/*	{ 19900, 0x00, 2 },*/	/* 19860.9 ps =  50.35 MHz */
	{ 20000, 0x07, 1 },	/* 20000.0 ps =  50.00 MHz */
	{ 20300, 0x06, 1 },	/* 20202.0 ps =  49.50 MHz */
	{ 22500, 0x05, 1 },	/* 22271.2 ps =  44.90 MHz */
	{ 25000, 0x04, 1 },	/* 25000.0 ps =  40.00 MHz */
/*	{ 28000, 0x03, 1 },*/	/* 27777.8 ps =  36.00 MHz */
	{ 30000, 0x2B, 1 },	/* 29629,6 ps =  33.75 MHz */
	{ 31000, 0x1F, 1 },	/* 30769.2 ps =  32.50 MHz */
	{ 32000, 0x2A, 1 },	/* 31746.0 ps =  31.50 MHz */
/*	{ 32000, 0x02, 1 },*/	/* 31746.0 ps =  31.50 MHz */
/*	{ 36000, 0x01, 1 },*/	/* 35310.7 ps =  28.32 MHz */
/*	{ 39900, 0x00, 1 },*/	/* 39714.1 ps =  25.18 MHz */
	{ 40000, 0x17, 1 },	/* 40000.0 ps =  25.00 MHz */
	{ 40600, 0x16, 1 },	/* 40404.0 ps =  24.75 MHz */
	{ 45000, 0x15, 1 },	/* 44543.4 ps =  22.45 MHz */
	{ 50000, 0x14, 1 },	/* 50000.0 ps =  20.00 MHz */
/*	{ 56000, 0x13, 1 },*/	/* 55555.5 ps =  18.00 MHz */
	{ 62000, 0x2F, 1 },	/* 61538.8 ps =  16.25 MHz */
/*	{ 64000, 0x12, 1 },*/	/* 63492.0 ps =  15.75 MHz */
    };
    int set;

    for (set = 0; set < sizeof(ATI18818_clocks)/sizeof(*ATI18818_clocks);
	 set++)
	if (vclk_per <= ATI18818_clocks[set].ps_lim) {
	    pll->m = ATI18818_clocks[set].mode;
	    pll->n = ATI18818_clocks[set].prog;
	    return 0;
	}
    return -EINVAL;
}

static int aty_var_to_pll_514(u32 vclk_per, struct pll_gx *pll)
{
    /*
     *  FIXME: use real calculations instead of using fixed values from the old
     *	       driver
     */
    static struct {
	u32 limit;	/* pixlock rounding limit (arbitrary) */
	u8 m;		/* (df<<6) | vco_div_count */
	u8 n;		/* ref_div_count */
    } RGB514_clocks[7] = {
	{  8000, (3<<6) | 20, 9 },	/*  7395 ps / 135.2273 MHz */
	{ 10000, (1<<6) | 19, 3 },	/*  9977 ps / 100.2273 MHz */
	{ 13000, (1<<6) |  2, 3 },	/* 12509 ps /  79.9432 MHz */
	{ 14000, (2<<6) |  8, 7 },	/* 13394 ps /  74.6591 MHz */
	{ 16000, (1<<6) | 44, 6 },	/* 15378 ps /  65.0284 MHz */
	{ 25000, (1<<6) | 15, 5 },	/* 17460 ps /  57.2727 MHz */
	{ 50000, (0<<6) | 53, 7 },	/* 33145 ps /  30.1705 MHz */
    };
    int i;

    for (i = 0; i < sizeof(RGB514_clocks)/sizeof(*RGB514_clocks); i++)
	if (vclk_per <= RGB514_clocks[i].limit) {
	    pll->m = RGB514_clocks[i].m;
	    pll->n = RGB514_clocks[i].n;
	    return 0;
	}
    return -EINVAL;
}

    /* FIXME: ATI18818?? */

static int aty_pll_gx_to_var(const struct pll_gx *pll, u32 *vclk_per)
{
    u8 df, vco_div_count, ref_div_count;

    df = pll->m >> 6;
    vco_div_count = pll->m & 0x3f;
    ref_div_count = pll->n;

    *vclk_per = ((ref_clk_per*ref_div_count)<<(3-df))/(vco_div_count+65);

    return 0;
}


    /*
     *  PLL programming (Mach64 CT family)
     */

static void aty_set_pll_ct(const struct fb_info_aty *info,
			   const struct pll_ct *pll)
{
    aty_st_pll(PLL_REF_DIV, pll->pll_ref_div, info);
    aty_st_pll(PLL_GEN_CNTL, pll->pll_gen_cntl, info);
    aty_st_pll(MCLK_FB_DIV, pll->mclk_fb_div, info);
    aty_st_pll(PLL_VCLK_CNTL, pll->pll_vclk_cntl, info);
    aty_st_pll(VCLK_POST_DIV, pll->vclk_post_div, info);
    aty_st_pll(VCLK0_FB_DIV, pll->vclk_fb_div, info);
    aty_st_pll(PLL_EXT_CNTL, pll->pll_ext_cntl, info);

    if (!(Gx == GX_CHIP_ID || Gx == CX_CHIP_ID || Gx == CT_CHIP_ID ||
	  Gx == ET_CHIP_ID ||
	  ((Gx == VT_CHIP_ID || Gx == GT_CHIP_ID) && !(Rev & 0x07)))) {
	if (info->ram_type >= SDRAM)
	    aty_st_pll(DLL_CNTL, 0xa6, info);
	else
	    aty_st_pll(DLL_CNTL, 0xa0, info);
	aty_st_pll(VFC_CNTL, 0x1b, info);
	aty_st_le32(DSP_CONFIG, pll->dsp_config, info);
	aty_st_le32(DSP_ON_OFF, pll->dsp_on_off, info);
    }
}

static int aty_dsp_gt(const struct fb_info_aty *info, u8 mclk_fb_div,
		      u8 mclk_post_div, u8 vclk_fb_div, u8 vclk_post_div,
		      u8 bpp, struct pll_ct *pll)
{
    u32 dsp_xclks_per_row, dsp_loop_latency, dsp_precision, dsp_off, dsp_on;
    u32 xclks_per_row, fifo_off, fifo_on, y, fifo_size, page_size;

    /* xclocks_per_row<<11 */
    xclks_per_row = (mclk_fb_div*vclk_post_div*64<<11)/
		    (vclk_fb_div*mclk_post_div*bpp);
    if (xclks_per_row < (1<<11))
	FAIL("Dotclock to high");
    if (Gx == GT_CHIP_ID || Gx == GU_CHIP_ID || Gx == VT_CHIP_ID ||
	Gx == VU_CHIP_ID || Gx == GV_CHIP_ID || Gx == GW_CHIP_ID ||
	Gx == GZ_CHIP_ID) {
	fifo_size = 24;
	dsp_loop_latency = 0;
    } else {
	fifo_size = 32;
	dsp_loop_latency = 2;
    }
    dsp_precision = 0;
    y = (xclks_per_row*fifo_size)>>11;
    while (y) {
	y >>= 1;
	dsp_precision++;
    }
    dsp_precision -= 5;
    /* fifo_off<<6 */
    fifo_off = ((xclks_per_row*(fifo_size-1))>>5)+(1<<6);

    if (info->total_vram > 1*1024*1024) {
	if (info->ram_type >= SDRAM) {
	    /* >1 MB SDRAM */
	    dsp_loop_latency += 8;
	    page_size = 8;
	} else {
	    /* >1 MB DRAM */
	    dsp_loop_latency += 6;
	    page_size = 9;
	}
    } else {
	if (info->ram_type >= SDRAM) {
	    /* <2 MB SDRAM */
	    dsp_loop_latency += 9;
	    page_size = 10;
	} else {
	    /* <2 MB DRAM */
	    dsp_loop_latency += 8;
	    page_size = 10;
	}
    }
    /* fifo_on<<6 */
    if (xclks_per_row >= (page_size<<11))
	fifo_on = ((2*page_size+1)<<6)+(xclks_per_row>>5);
    else
	fifo_on = (3*page_size)<<6;

    dsp_xclks_per_row = xclks_per_row>>dsp_precision;
    dsp_on = fifo_on>>dsp_precision;
    dsp_off = fifo_off>>dsp_precision;

    pll->dsp_config = (dsp_xclks_per_row & 0x3fff) |
		      ((dsp_loop_latency & 0xf)<<16) |
		      ((dsp_precision & 7)<<20);
    pll->dsp_on_off = (dsp_on & 0x7ff) | ((dsp_off & 0x7ff)<<16);
    return 0;
}

static int aty_var_to_pll_ct(const struct fb_info_aty *info, u32 vclk_per,
			     u8 bpp, struct pll_ct *pll)
{
    u32 q, x;			/* x is a workaround for sparc64-linux-gcc */
    u8 pll_ref_div, pll_gen_cntl, pll_ext_cntl;
    u8 mclk_fb_div, mclk_post_div, mpostdiv = 0;
    u8 vclk_fb_div, vclk_post_div, vpostdiv = 0;
    int err;

    x = x;			/* x is a workaround for sparc64-linux-gcc */

    pll->pll_vclk_cntl = 0x03;	/* VCLK = PLL_VCLK/VCLKx_POST */

    pll_ref_div = info->pll_per*2*255/ref_clk_per;

    /* FIXME: use the VTB/GTB /3 post divider if it's better suited */
    q = ref_clk_per*pll_ref_div*4/info->mclk_per;	/* actually 8*q */
    if (q < 16*8 || q > 255*8)
	FAIL("mclk out of range");
    else if (q < 32*8)
	mclk_post_div = 8;
    else if (q < 64*8)
	mclk_post_div = 4;
    else if (q < 128*8)
	mclk_post_div = 2;
    else
	mclk_post_div = 1;
    mclk_fb_div = q*mclk_post_div/8;

    /* FIXME: use the VTB/GTB /{3,6,12} post dividers if they're better suited */
    q = ref_clk_per*pll_ref_div*4/vclk_per;	/* actually 8*q */
    if (q < 16*8 || q > 255*8)
	FAIL("vclk out of range");
    else if (q < 32*8)
	vclk_post_div = 8;
    else if (q < 64*8)
	vclk_post_div = 4;
    else if (q < 128*8)
	vclk_post_div = 2;
    else
	vclk_post_div = 1;
    vclk_fb_div = q*vclk_post_div/8;

    if ((err = aty_dsp_gt(info, mclk_fb_div, mclk_post_div, vclk_fb_div,
			  vclk_post_div, bpp, pll)))
	return err;

    if ((((Gx == GT_CHIP_ID) && (Rev & 0x03)) || (Gx == GU_CHIP_ID) ||
	 (Gx == GV_CHIP_ID) || (Gx == GW_CHIP_ID) || (Gx == GZ_CHIP_ID) ||
	 (Gx == LG_CHIP_ID) || (Gx == GB_CHIP_ID) || (Gx == GD_CHIP_ID) ||
	 (Gx == GI_CHIP_ID) || (Gx == GP_CHIP_ID) || (Gx == GQ_CHIP_ID) ||
	 (Gx == VU_CHIP_ID)) && (info->ram_type >= SDRAM))
	pll_gen_cntl = 0x04;
    else
	pll_gen_cntl = 0x84;

    switch (mclk_post_div) {
	case 1:
	    mpostdiv = 0;
	    break;
	case 2:
	    mpostdiv = 1;
	    break;
	case 3:
	    mpostdiv = 4;
	    break;
	case 4:
	    mpostdiv = 2;
	    break;
	case 8:
	    mpostdiv = 3;
	    break;
    }
    pll_gen_cntl |= mpostdiv<<4;	/* mclk */

    if (Gx == VT_CHIP_ID && (Rev == 0x40 || Rev == 0x48))
	pll_ext_cntl = 0;
    else
    	pll_ext_cntl = mpostdiv;	/* xclk == mclk */

    switch (vclk_post_div) {
	case 1:
	    vpostdiv = 0;
	    break;
	case 2:
	    vpostdiv = 1;
	    break;
	case 3:
	    vpostdiv = 0;
	    pll_ext_cntl |= 0x10;
	    break;
	case 4:
	    vpostdiv = 2;
	    break;
	case 6:
	    vpostdiv = 2;
	    pll_ext_cntl |= 0x10;
	    break;
	case 8:
	    vpostdiv = 3;
	    break;
	case 12:
	    vpostdiv = 3;
	    pll_ext_cntl |= 0x10;
	    break;
    }
    vclk_post_div = vpostdiv;

    pll->pll_ref_div = pll_ref_div;
    pll->pll_gen_cntl = pll_gen_cntl;
    pll->mclk_fb_div = mclk_fb_div;
    pll->vclk_post_div = vclk_post_div;
    pll->vclk_fb_div = vclk_fb_div;
    pll->pll_ext_cntl = pll_ext_cntl;
    return 0;
}

static int aty_pll_ct_to_var(const struct pll_ct *pll, u32 *vclk_per)
{
    u8 pll_ref_div = pll->pll_ref_div;
    u8 vclk_fb_div = pll->vclk_fb_div;
    u8 vclk_post_div = pll->vclk_post_div;
    u8 pll_ext_cntl = pll->pll_ext_cntl;
    static u8 vclk_post_div_tab[] = {
	1, 2, 4, 8,
	3, 0, 6, 12
    };
    u8 vpostdiv = vclk_post_div_tab[((pll_ext_cntl & 0x10) >> 1) |
				    (vclk_post_div & 3)];
    if (vpostdiv == 0)
	return -EINVAL;
    *vclk_per = pll_ref_div*vpostdiv*ref_clk_per/vclk_fb_div/2;
    return 0;
}

/* ------------------------------------------------------------------------- */

static void atyfb_set_par(const struct atyfb_par *par,
			  struct fb_info_aty *info)
{
    u32 i;

    info->current_par = *par;

    if (info->blitter_may_be_busy)
	wait_for_idle(info);
    aty_set_crtc(info, &par->crtc);
    aty_st_8(CLOCK_CNTL, 0, info);
    aty_st_8(CLOCK_CNTL, CLOCK_STROBE, info);

    if ((Gx == GX_CHIP_ID) || (Gx == CX_CHIP_ID)) {
	switch (info->dac_type) {
	    case DAC_IBMRGB514:
		aty_set_dac_514(info, par->crtc.bpp);
		break;
	    case DAC_ATI68860_B:
		/* FIXME */
		break;
	}
	aty_set_pll_gx(info, &par->pll.gx);
	aty_st_le32(BUS_CNTL, 0x590e10ff, info);
	aty_st_le32(DAC_CNTL, 0x47012100, info);

	/* Don't forget MEM_CNTL */
	i = aty_ld_le32(MEM_CNTL, info) & 0xf0ffffff;
	switch (par->crtc.bpp) {
	    case 8:
		i |= 0x02000000;
		break;
	    case 16:
		i |= 0x03000000;
		break;
	    case 32:
		i |= 0x06000000;
		break;
	}
	aty_st_le32(MEM_CNTL, i, info);
					
    } else {
	aty_set_pll_ct(info, &par->pll.ct);
	i = aty_ld_le32(MEM_CNTL, info) & 0xf30fffff;
	if (!(Gx == VT_CHIP_ID && (Rev == 0x40 || Rev == 0x48)))
	    i |= info->mem_refresh_rate << 20;
	switch (par->crtc.bpp) {
	    case 8:
	    case 24:
		i |= 0x00000000;
		break;
	    case 16:
		i |= 0x04000000;
		break;
	    case 32:
		i |= 0x08000000;
		break;
	}
	if ((Gx == CT_CHIP_ID) || (Gx == ET_CHIP_ID)) {
	    aty_st_le32(DAC_CNTL, 0x87010184, info);
	    aty_st_le32(BUS_CNTL, 0x680000f9, info);
	} else if ((Gx == VT_CHIP_ID) || (Gx == VU_CHIP_ID)) {
	    aty_st_le32(DAC_CNTL, 0x87010184, info);
	    aty_st_le32(BUS_CNTL, 0x680000f9, info);
	} else {
	    /* GT */
	    aty_st_le32(DAC_CNTL, 0x86010102, info);
	    aty_st_le32(BUS_CNTL, 0x7b23a040, info);
	    aty_st_le32(EXT_MEM_CNTL,
			aty_ld_le32(EXT_MEM_CNTL, info) | 0x5000001, info);
	}
	aty_st_le32(MEM_CNTL, i, info);
    }
    aty_st_8(DAC_MASK, 0xff, info);

    /* Initialize the graphics engine */
    if (par->accel_flags & FB_ACCELF_TEXT)
	init_engine(par, info);

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (console_fb_info == &info->fb_info) {
	struct fb_var_screeninfo var;
	int vmode, cmode;
	display_info.height = ((par->crtc.v_tot_disp>>16) & 0x7ff)+1;
	display_info.width = (((par->crtc.h_tot_disp>>16) & 0xff)+1)*8;
	display_info.depth = par->crtc.bpp;
	display_info.pitch = par->crtc.vxres*par->crtc.bpp/8;
	atyfb_encode_var(&var, par, info);
	if (mac_var_to_vmode(&var, &vmode, &cmode))
	    display_info.mode = 0;
	else
	    display_info.mode = vmode;
	strcpy(display_info.name, atyfb_name);
	display_info.fb_address = info->frame_buffer_phys;
	display_info.cmap_adr_address = info->ati_regbase_phys+0xc0;
	display_info.cmap_data_address = info->ati_regbase_phys+0xc1;
	display_info.disp_reg_address = info->ati_regbase_phys;
    }
#endif /* CONFIG_FB_COMPAT_XPMAC */
}

static int atyfb_decode_var(const struct fb_var_screeninfo *var,
			    struct atyfb_par *par,
			    const struct fb_info_aty *info)
{
    int err;

    if ((err = aty_var_to_crtc(info, var, &par->crtc)))
	return err;
    if ((Gx == GX_CHIP_ID) || (Gx == CX_CHIP_ID))
	switch (info->clk_type) {
	    case CLK_ATI18818_1:
		err = aty_var_to_pll_18818(var->pixclock, &par->pll.gx);
		break;
	    case CLK_IBMRGB514:
		err = aty_var_to_pll_514(var->pixclock, &par->pll.gx);
		break;
	}
    else
	err = aty_var_to_pll_ct(info, var->pixclock, par->crtc.bpp,
				&par->pll.ct);
    if (err)
	return err;

    if (var->accel_flags & FB_ACCELF_TEXT)
	par->accel_flags = FB_ACCELF_TEXT;
    else
	par->accel_flags = 0;

#if 0
    if (!fbmon_valid_timings(var->pixclock, htotal, vtotal, info))
	return -EINVAL;
#endif

    return 0;
}

static int atyfb_encode_var(struct fb_var_screeninfo *var,
			    const struct atyfb_par *par,
			    const struct fb_info_aty *info)
{
    int err;

    memset(var, 0, sizeof(struct fb_var_screeninfo));

    if ((err = aty_crtc_to_var(&par->crtc, var)))
	return err;
    if ((Gx == GX_CHIP_ID) || (Gx == CX_CHIP_ID))
	err = aty_pll_gx_to_var(&par->pll.gx, &var->pixclock);
    else
	err = aty_pll_ct_to_var(&par->pll.ct, &var->pixclock);
    if (err)
	return err;

    var->height = -1;
    var->width = -1;
    var->accel_flags = par->accel_flags;

    return 0;
}



static void set_off_pitch(struct atyfb_par *par,
			  const struct fb_info_aty *info)
{
    u32 xoffset = par->crtc.xoffset;
    u32 yoffset = par->crtc.yoffset;
    u32 vxres = par->crtc.vxres;
    u32 bpp = par->crtc.bpp;

    par->crtc.off_pitch = ((yoffset*vxres+xoffset)*bpp/64) | (vxres<<19);
    aty_st_le32(CRTC_OFF_PITCH, par->crtc.off_pitch, info);
}


    /*
     *  Open/Release the frame buffer device
     */

static int atyfb_open(struct fb_info *info, int user)

{
#ifdef __sparc__
    struct fb_info_aty *fb = (struct fb_info_aty *)info;

    if (user) {
	if (fb->open)
	    return -EBUSY;
	fb->mmaped = 0;
	fb->open = 1;
	fb->vtconsole = -1;
    } else {
	fb->consolecnt++;
    }
#endif
    MOD_INC_USE_COUNT;
    return(0);
}

static int atyfb_release(struct fb_info *info, int user)
{
#ifdef __sparc__
    struct fb_info_aty *fb = (struct fb_info_aty *)info;

    if (user) {
	if (fb->vtconsole != -1)
	    vt_cons[fb->vtconsole]->vc_mode = KD_TEXT;
	fb->open = 0;
	fb->mmaped = 0;
	fb->vtconsole = -1;
    } else {
	fb->consolecnt--;
    }
#endif
    MOD_DEC_USE_COUNT;
    return(0);
}


static int encode_fix(struct fb_fix_screeninfo *fix,
		      const struct atyfb_par *par,
		      const struct fb_info_aty *info)
{
    memset(fix, 0, sizeof(struct fb_fix_screeninfo));

    strcpy(fix->id, atyfb_name);
    fix->smem_start = (char *)info->frame_buffer_phys;
    fix->smem_len = (u32)info->total_vram;

#ifdef __LITTLE_ENDIAN
    /*
     *  Last page of 8 MB little-endian aperture is MMIO
     *  FIXME: we should use the auxiliary aperture instead so we can acces the
     *  full 8 MB of video RAM on 8 MB boards
     */
    if (fix->smem_len > 0x800000-GUI_RESERVE)
	fix->smem_len = 0x800000-GUI_RESERVE;
#endif
    /*
     *  Reg Block 0 (CT-compatible block) is at ati_regbase_phys
     *  Reg Block 1 (multimedia extensions) is at ati_regbase_phys-0x400
     */
    if (Gx == GX_CHIP_ID || Gx == CX_CHIP_ID) {
	fix->mmio_start = (char *)info->ati_regbase_phys;
	fix->mmio_len = 0x400;
	fix->accel = FB_ACCEL_ATI_MACH64GX;
    } else if (Gx == CT_CHIP_ID || Gx == ET_CHIP_ID) {
	fix->mmio_start = (char *)info->ati_regbase_phys;
	fix->mmio_len = 0x400;
	fix->accel = FB_ACCEL_ATI_MACH64CT;
    } else if (Gx == VT_CHIP_ID || Gx == VU_CHIP_ID || Gx == VV_CHIP_ID) {
	fix->mmio_start = (char *)(info->ati_regbase_phys-0x400);
	fix->mmio_len = 0x800;
	fix->accel = FB_ACCEL_ATI_MACH64VT;
    } else {
	fix->mmio_start = (char *)(info->ati_regbase_phys-0x400);
	fix->mmio_len = 0x800;
	fix->accel = FB_ACCEL_ATI_MACH64GT;
    }
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;
    fix->line_length = par->crtc.vxres*par->crtc.bpp/8;
    fix->visual = par->crtc.bpp <= 8 ? FB_VISUAL_PSEUDOCOLOR
				     : FB_VISUAL_DIRECTCOLOR;
    fix->ywrapstep = 0;
    fix->xpanstep = 8;
    fix->ypanstep = 1;

    return 0;
}


struct fb_var_screeninfo default_var = {
    /* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
    640, 480, 640, 480, 0, 0, 8, 0,
    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
    0, 0, -1, -1, 0, 39722, 48, 16, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};


    /*
     *  Get the Fixed Part of the Display
     */

static int atyfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *fb)
{
    const struct fb_info_aty *info = (struct fb_info_aty *)fb;
    struct atyfb_par par;

    if (con == -1)
	par = info->default_par;
    else
	atyfb_decode_var(&fb_display[con].var, &par, info);
    encode_fix(fix, &par, info);
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int atyfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *fb)
{
    const struct fb_info_aty *info = (struct fb_info_aty *)fb;

    if (con == -1)
	atyfb_encode_var(var, &info->default_par, info);
    else
	*var = fb_display[con].var;
    return 0;
}


static void atyfb_set_disp(struct display *disp, struct fb_info_aty *info,
			   int bpp, int accel)
{
	    switch (bpp) {
#ifdef FBCON_HAS_CFB8
		case 8:
		    info->dispsw = accel ? fbcon_aty8 : fbcon_cfb8;
		    disp->dispsw = &info->dispsw;
		    break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
		    info->dispsw = accel ? fbcon_aty16 : fbcon_cfb16;
		    disp->dispsw = &info->dispsw;
		    disp->dispsw_data = info->fbcon_cmap.cfb16;
		    break;
#endif
#ifdef FBCON_HAS_CFB24
		case 24:
		    info->dispsw = accel ? fbcon_aty24 : fbcon_cfb24;
		    disp->dispsw = &info->dispsw;
		    disp->dispsw_data = info->fbcon_cmap.cfb24;
		    break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
		    info->dispsw = accel ? fbcon_aty32 : fbcon_cfb32;
		    disp->dispsw = &info->dispsw;
		    disp->dispsw_data = info->fbcon_cmap.cfb32;
		    break;
#endif
		default:
		    disp->dispsw = &fbcon_dummy;
	    }
	    if (info->cursor) {
		info->dispsw.cursor = atyfb_cursor;
		info->dispsw.set_font = atyfb_set_font;
	    }
}


    /*
     *  Set the User Defined Part of the Display
     */

static int atyfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;
    struct atyfb_par par;
    struct display *display;
    int oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel, accel, err;
    int activate = var->activate;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = fb->disp;	/* used during initialization */

    if ((err = atyfb_decode_var(var, &par, info)))
	return err;

    atyfb_encode_var(var, &par, (struct fb_info_aty *)info);

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

	    encode_fix(&fix, &par, info);
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
	    atyfb_set_disp(display, info, par.crtc.bpp, accel);
	    if (accel)
	    	display->scrollmode = (info->bus_type == PCI) ? SCROLL_YNOMOVE : 0;
	    else
	    	display->scrollmode = SCROLL_YREDRAW;
	    if (info->fb_info.changevar)
		(*info->fb_info.changevar)(con);
	}
	if (con == currcon)
	    atyfb_set_par(&par, info);
	if (oldbpp != var->bits_per_pixel) {
	    if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
		return err;
	    do_install_cmap(con, &info->fb_info);
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
			     struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;
    u32 xres, yres, xoffset, yoffset;
    struct atyfb_par *par = &info->current_par;

    xres = (((par->crtc.h_tot_disp>>16) & 0xff)+1)*8;
    yres = ((par->crtc.v_tot_disp>>16) & 0x7ff)+1;
    xoffset = (var->xoffset+7) & ~7;
    yoffset = var->yoffset;
    if (xoffset+xres > par->crtc.vxres || yoffset+yres > par->crtc.vyres)
	return -EINVAL;
    par->crtc.xoffset = xoffset;
    par->crtc.yoffset = yoffset;
    set_off_pitch(par, info);
    return 0;
}

    /*
     *  Get the Colormap
     */

static int atyfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, kspc, atyfb_getcolreg, info);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else {
	int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
	fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
    }
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
	int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
	if ((err = fb_alloc_cmap(&fb_display[con].cmap, size, 0)))
	    return err;
    }
    if (con == currcon)			/* current console? */
	return fb_set_cmap(cmap, kspc, atyfb_setcolreg, info);
    else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


static int atyfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
#ifdef __sparc__
    struct fb_info_aty *fb = (struct fb_info_aty *)info;
    struct fbtype fbtyp;

    switch (cmd) {
    case FBIOGTYPE:
	fbtyp.fb_type = FBTYPE_PCI_GENERIC;
	fbtyp.fb_width = fb->current_par.crtc.vxres;
	fbtyp.fb_height = fb->current_par.crtc.vyres;
	fbtyp.fb_depth = fb->current_par.crtc.bpp;
	fbtyp.fb_cmsize = fb_display[con].cmap.len;
	fbtyp.fb_size = fb->total_vram;
	copy_to_user_ret((struct fbtype *)arg, &fbtyp, sizeof(fbtyp), -EFAULT);
	break;
    default:
	return -EINVAL;
    }
    return 0;
#else
    return -EINVAL;
#endif
}

#ifdef __sparc__
static int atyfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	struct fb_info_aty *fb = (struct fb_info_aty *)info;
	unsigned int size, page, map_size = 0;
	unsigned long map_offset = 0;
	int i;

	if (!fb->mmap_map)
		return -ENXIO;

	size = vma->vm_end - vma->vm_start;
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;

	/* To stop the swapper from even considering these pages. */
	vma->vm_flags |= (VM_SHM | VM_LOCKED);

	if (((vma->vm_offset == 0) && (size == fb->total_vram)) ||
	    ((vma->vm_offset == fb->total_vram) && (size == PAGE_SIZE)))
		vma->vm_offset += 0x8000000000000000UL;

#ifdef __sparc_v9__
	/* Align it as much as desirable */
	{
		unsigned long j, align;
		int max = -1;
		
		map_offset = vma->vm_offset+size;
		for (i = 0; fb->mmap_map[i].size; i++) {
			if (fb->mmap_map[i].voff < vma->vm_offset)
				continue;
			if (fb->mmap_map[i].voff >= map_offset)
				break;
			if (max < 0 ||
			    fb->mmap_map[i].size > fb->mmap_map[max].size)
				max = i;
		}
		if (max >= 0) {
			j = fb->mmap_map[max].size;
			if (fb->mmap_map[max].voff + j > map_offset)
				j = map_offset - fb->mmap_map[max].voff;
			for (align = 0x400000; align > PAGE_SIZE; align >>= 3)
				if (j >= align &&
				    !(fb->mmap_map[max].poff & (align - 1)))
					break;
			if (align > PAGE_SIZE) {
				j = align;
				align = j - ((vma->vm_start
					      + fb->mmap_map[max].voff
					      - vma->vm_offset) & (j - 1));
				if (align != j) {
					struct vm_area_struct *vmm;

					vmm = find_vma(current->mm,
						       vma->vm_start);
					if (!vmm || vmm->vm_start
						    >= vma->vm_end + align) {
						vma->vm_start += align;
						vma->vm_end += align;
					}
				}
			}
		}
	}
#endif	

	/* Each page, see which map applies */
	for (page = 0; page < size; ) {
		map_size = 0;
		for (i = 0; fb->mmap_map[i].size; i++) {
			unsigned long start = fb->mmap_map[i].voff;
			unsigned long end = start + fb->mmap_map[i].size;
			unsigned long offset = vma->vm_offset + page;

			if (start > offset)
				continue;
			if (offset >= end)
				continue;

			map_size = fb->mmap_map[i].size - (offset - start);
			map_offset = fb->mmap_map[i].poff + (offset - start);
			break;
		}
		if (!map_size) {
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;

		pgprot_val(vma->vm_page_prot) &= ~(fb->mmap_map[i].prot_mask);
		pgprot_val(vma->vm_page_prot) |= fb->mmap_map[i].prot_flag;

		if (remap_page_range(vma->vm_start + page, map_offset,
				     map_size, vma->vm_page_prot))
			return -EAGAIN;

		page += map_size;
	}

	if (!map_size)
		return -EINVAL;

	vma->vm_file = file;
	file->f_count++;
	vma->vm_flags |= VM_IO;

	if (!fb->mmaped) {
		int lastconsole = 0;

		if (info->display_fg)
			lastconsole = info->display_fg->vc_num;
		fb->mmaped = 1;
		if (fb->consolecnt && fb_display[lastconsole].fb_info == info) {
			fb->vtconsole = lastconsole;
			vt_cons[lastconsole]->vc_mode = KD_GRAPHICS;
		}
	}
	return 0;
}

static struct {
	u32	yoffset;
	u8	r[2][256];
	u8	g[2][256];
	u8	b[2][256];
} atyfb_save;

static void atyfb_save_palette(struct fb_info *fb, int enter)
{
	struct fb_info_aty *info = (struct fb_info_aty *)fb;
	int i, tmp, scale;

	for (i = 0; i < 256; i++) {
		tmp = aty_ld_8(DAC_CNTL, info) & 0xfc;
		if ((Gx == GT_CHIP_ID) || (Gx == GU_CHIP_ID) ||
		    (Gx == LG_CHIP_ID) || (Gx == GB_CHIP_ID) ||
		    (Gx == GD_CHIP_ID) || (Gx == GI_CHIP_ID) ||
		    (Gx == GP_CHIP_ID) || (Gx == GQ_CHIP_ID))
			tmp |= 0x2;
		aty_st_8(DAC_CNTL, tmp, info);
		aty_st_8(DAC_MASK, 0xff, info);
		eieio();
		scale = ((Gx != GX_CHIP_ID) && (Gx != CX_CHIP_ID) &&
			(info->current_par.crtc.bpp == 16)) ? 3 : 0;
		info->aty_cmap_regs->rindex = i << scale;
		eieio();
		atyfb_save.r[enter][i] = info->aty_cmap_regs->lut;
		eieio();
		atyfb_save.g[enter][i] = info->aty_cmap_regs->lut;
		eieio();
		atyfb_save.b[enter][i] = info->aty_cmap_regs->lut;
		eieio();
		info->aty_cmap_regs->windex = i << scale;
		eieio();
		info->aty_cmap_regs->lut = atyfb_save.r[1-enter][i];
		eieio();
		info->aty_cmap_regs->lut = atyfb_save.g[1-enter][i];
		eieio();
		info->aty_cmap_regs->lut = atyfb_save.b[1-enter][i];
		eieio();
	}
}

static void atyfb_palette(int enter)
{
	struct fb_info_aty *info;
	struct atyfb_par *par;
	struct display *d;
	int i;

	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		d = &fb_display[i];
		if (d->fb_info &&
		    d->fb_info->fbops == &atyfb_ops &&
		    d->fb_info->display_fg &&
		    d->fb_info->display_fg->vc_num == i) {
			atyfb_save_palette(d->fb_info, enter);
			info = (struct fb_info_aty *)d->fb_info;
			par = &info->current_par;
			if (enter) {
				atyfb_save.yoffset = par->crtc.yoffset;
				par->crtc.yoffset = 0;
				set_off_pitch(par, info);
			} else {
				par->crtc.yoffset = atyfb_save.yoffset;
				set_off_pitch(par, info);
			}
			break;
		}
	}
}
#endif /* __sparc__ */

    /*
     *  Initialisation
     */

__initfunc(static int aty_init(struct fb_info_aty *info, const char *name))
{
    u32 chip_id;
    u32 i;
    int j, k;
    struct fb_var_screeninfo var;
    struct display *disp;
    const char *chipname = NULL, *ramname = NULL;
    int pll, mclk, gtb_memsize;
#if defined(CONFIG_PPC)
    int sense;
#endif

    info->aty_cmap_regs = (struct aty_cmap_regs *)(info->ati_regbase+0xc0);
    chip_id = aty_ld_le32(CONFIG_CHIP_ID, info);
    Gx = chip_id & CFG_CHIP_TYPE;
    Rev = (chip_id & CFG_CHIP_REV)>>24;
    for (j = 0; j < (sizeof(aty_features)/sizeof(*aty_features)); j++)
	if (aty_features[j].chip_type == Gx) {
	    chipname = aty_features[j].name;
	    break;
	}
    if (!chipname) {
	printk("atyfb: Unknown mach64 0x%04x\n", Gx);
	return 0;
    } else
	printk("atyfb: %s [0x%04x rev 0x%02x] ", chipname, Gx, Rev);
    if ((Gx == GX_CHIP_ID) || (Gx == CX_CHIP_ID)) {
	info->bus_type = (aty_ld_le32(CONFIG_STAT0, info) >> 0) & 0x07;
	info->ram_type = (aty_ld_le32(CONFIG_STAT0, info) >> 3) & 0x07;
	ramname = aty_gx_ram[info->ram_type];
	/* FIXME: clockchip/RAMDAC probing? */
#ifdef CONFIG_ATARI
	info->dac_type = DAC_ATI68860_B;
	info->clk_type = CLK_ATI18818_1;
#else
	info->dac_type = DAC_IBMRGB514;
	info->clk_type = CLK_IBMRGB514;
#endif
	/* FIXME */
	pll = 135;
	mclk = 50;
    } else {
	info->bus_type = PCI;
	info->ram_type = (aty_ld_le32(CONFIG_STAT0, info) & 0x07);
	ramname = aty_ct_ram[info->ram_type];
	info->dac_type = DAC_INTERNAL;
	info->clk_type = CLK_INTERNAL;
	if ((Gx == CT_CHIP_ID) || (Gx == ET_CHIP_ID)) {
	    pll = 135;
	    mclk = 60;
	} else {
	    mclk = info->ram_type >= SDRAM ? 67 : 63;
	    if ((Gx == VT_CHIP_ID) && (Rev == 0x08)) {
		/* VTA3 */
		pll = 170;
	    } else if (((Gx == VT_CHIP_ID) && ((Rev == 0x40) ||
					       (Rev == 0x48))) ||
		       ((Gx == VT_CHIP_ID) && ((Rev == 0x01) ||
					       (Rev == 0x9a))) ||
		       Gx == VU_CHIP_ID) {
		/* VTA4 or VTB */
		pll = 200;
	    } else if (Gx == VV_CHIP_ID) {
		/* VT4 */
		pll = 230;
		mclk = 83;
	    } else if (Gx == VT_CHIP_ID) {
		/* other VT */
		pll = 135;
		mclk = 63;
	    } else if ((Gx == GT_CHIP_ID) && (Rev & 0x01)) {
		/* RAGE II */
		pll = 170;
	    } else if (((Gx == GT_CHIP_ID) && (Rev & 0x02)) ||
		       (Gx == GU_CHIP_ID)) {
		/* RAGE II+ */
		pll = 200;
	    } else if (Gx == GV_CHIP_ID || Gx == GW_CHIP_ID ||
		       Gx == GZ_CHIP_ID) {
		/* RAGE IIC */
		pll = 230;
		mclk = 83;
	    } else if (Gx == GB_CHIP_ID || Gx == GD_CHIP_ID ||
		       Gx == GI_CHIP_ID || Gx == GP_CHIP_ID ||
		       Gx == GQ_CHIP_ID || Gx == LB_CHIP_ID ||
		       Gx == LD_CHIP_ID || Gx == LG_CHIP_ID ||
		       Gx == LI_CHIP_ID || Gx == LP_CHIP_ID) {
		/* RAGE PRO or LT PRO */
		pll = 230;
		mclk = 100;
	    } else {
		/* other RAGE */
		pll = 135;
		mclk = 63;
	    }
	}
    }

    i = aty_ld_le32(MEM_CNTL, info);
    gtb_memsize = !(Gx == GX_CHIP_ID || Gx == CX_CHIP_ID || Gx == CT_CHIP_ID ||
		    Gx == ET_CHIP_ID ||
		    ((Gx == VT_CHIP_ID || Gx == GT_CHIP_ID) && !(Rev & 0x07)));
    if (gtb_memsize)
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
    else
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

    if (Gx == GI_CHIP_ID) {
	if (aty_ld_le32(CONFIG_STAT1, info) & 0x40000000)
	  info->total_vram += 0x400000;
    }

    if (default_vram) {
	info->total_vram = default_vram*1024;
	i = i & ~(gtb_memsize ? 0xF : MEM_SIZE_ALIAS);
	if (info->total_vram <= 0x80000)
	    i |= MEM_SIZE_512K;
	else if (info->total_vram <= 0x100000)
	    i |= MEM_SIZE_1M;
	else if (info->total_vram <= 0x200000)
	    i |= gtb_memsize ? MEM_SIZE_2M_GTB : MEM_SIZE_2M;
	else if (info->total_vram <= 0x400000)
	    i |= gtb_memsize ? MEM_SIZE_4M_GTB : MEM_SIZE_4M;
	else if (info->total_vram <= 0x600000)
	    i |= gtb_memsize ? MEM_SIZE_6M_GTB : MEM_SIZE_6M;
	else
	    i |= gtb_memsize ? MEM_SIZE_8M_GTB : MEM_SIZE_8M;
	aty_st_le32(MEM_CNTL, i, info);
    }
    if (default_pll)
	pll = default_pll;
    if (default_mclk)
	mclk = default_mclk;

    printk("%d%c %s, %d MHz PLL, %d Mhz MCLK\n", 
    	   info->total_vram == 0x80000 ? 512 : (info->total_vram >> 20), 
    	   info->total_vram == 0x80000 ? 'K' : 'M', ramname, pll, mclk);

    if (mclk < 44)
	info->mem_refresh_rate = 0;	/* 000 = 10 Mhz - 43 Mhz */
    else if (mclk < 50)
	info->mem_refresh_rate = 1;	/* 001 = 44 Mhz - 49 Mhz */
    else if (mclk < 55)
	info->mem_refresh_rate = 2;	/* 010 = 50 Mhz - 54 Mhz */
    else if (mclk < 66)
	info->mem_refresh_rate = 3;	/* 011 = 55 Mhz - 65 Mhz */
    else if (mclk < 75)
	info->mem_refresh_rate = 4;	/* 100 = 66 Mhz - 74 Mhz */
    else if (mclk < 80)
	info->mem_refresh_rate = 5;	/* 101 = 75 Mhz - 79 Mhz */
    else if (mclk < 100)
	info->mem_refresh_rate = 6;	/* 110 = 80 Mhz - 100 Mhz */
    else
	info->mem_refresh_rate = 7;	/* 111 = 100 Mhz and above */
    info->pll_per = 1000000/pll;
    info->mclk_per = 1000000/mclk;

#ifdef DEBUG
    if ((Gx != GX_CHIP_ID) && (Gx != CX_CHIP_ID)) {
	int i;
	printk("BUS_CNTL DAC_CNTL MEM_CNTL EXT_MEM_CNTL CRTC_GEN_CNTL "
	       "DSP_CONFIG DSP_ON_OFF\n"
	       "%08x %08x %08x %08x     %08x      %08x   %08x\n"
	       "PLL",
	       aty_ld_le32(BUS_CNTL, info), aty_ld_le32(DAC_CNTL, info),
	       aty_ld_le32(MEM_CNTL, info), aty_ld_le32(EXT_MEM_CNTL, info),
	       aty_ld_le32(CRTC_GEN_CNTL, info), aty_ld_le32(DSP_CONFIG, info),
	       aty_ld_le32(DSP_ON_OFF, info));
	for (i = 0; i < 16; i++)
	    printk(" %02x", aty_ld_pll(i, info));
	printk("\n");
    }
#endif

    if (info->bus_type == ISA)
	if ((info->total_vram == 0x400000) || (info->total_vram == 0x800000)) {
	    /* protect GUI-regs if complete Aperture is VRAM */
	    info->total_vram -= GUI_RESERVE;
	}

#if defined(CONFIG_PPC)
    if (default_vmode == VMODE_NVRAM) {
	default_vmode = nvram_read_byte(NV_VMODE);
	if (default_vmode <= 0 || default_vmode > VMODE_MAX)
	    default_vmode = VMODE_CHOOSE;
    }
    if (default_vmode == VMODE_CHOOSE) {
	if (Gx == LG_CHIP_ID)
	    /* G3 PowerBook with 1024x768 LCD */
	    default_vmode = VMODE_1024_768_60;
	else {
	    sense = read_aty_sense(info);
	    default_vmode = mac_map_monitor_sense(sense);
	}
    }
    if (default_vmode <= 0 || default_vmode > VMODE_MAX)
	default_vmode = VMODE_640_480_60;
    if (default_cmode == CMODE_NVRAM)
	default_cmode = nvram_read_byte(NV_CMODE);
    if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
	default_cmode = CMODE_8;
    if (mac_vmode_to_var(default_vmode, default_cmode, &var))
	var = default_var;
#else /* !CONFIG_PPC */
    var = default_var;
#endif /* !CONFIG_PPC */
    if (noaccel)
        var.accel_flags &= ~FB_ACCELF_TEXT;
    else
        var.accel_flags |= FB_ACCELF_TEXT;

    if (var.yres == var.yres_virtual) {
	u32 vram = (info->total_vram - (PAGE_SIZE << 2));
	var.yres_virtual = ((vram * 8) / var.bits_per_pixel) / var.xres_virtual;
	if (var.yres_virtual < var.yres)
		var.yres_virtual = var.yres;
    }

    if (atyfb_decode_var(&var, &info->default_par, info)) {
	printk("atyfb: can't set default video mode\n");
	return 0;
    }

    disp = &info->disp;

    strcpy(info->fb_info.modename, atyfb_name);
    info->fb_info.node = -1;
    info->fb_info.fbops = &atyfb_ops;
    info->fb_info.disp = disp;
    strcpy(info->fb_info.fontname, fontname);
    info->fb_info.changevar = NULL;
    info->fb_info.switch_con = &atyfbcon_switch;
    info->fb_info.updatevar = &atyfbcon_updatevar;
    info->fb_info.blank = &atyfbcon_blank;
    info->fb_info.flags = FBINFO_FLAG_DEFAULT;

#ifdef __sparc__
    atyfb_save_palette(&info->fb_info, 0);
#endif
    for (j = 0; j < 16; j++) {
	k = color_table[j];
	info->palette[j].red = default_red[k];
	info->palette[j].green = default_grn[k];
	info->palette[j].blue = default_blu[k];
    }

    if (Gx != GX_CHIP_ID && Gx != CX_CHIP_ID) {
	info->cursor = aty_init_cursor(info);
	if (info->cursor) {
	    info->dispsw.cursor = atyfb_cursor;
	    info->dispsw.set_font = atyfb_set_font;
	}
    }
    
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
    struct pci_dev *pdev;
    struct fb_info_aty *info;
    unsigned long addr;
#ifdef __sparc__
    extern void (*prom_palette) (int);
    extern int con_is_present(void);
    struct pcidev_cookie *pcp;
    char prop[128];
    int node, len;
    u32 mem, chip_id;
    int i, j;

    /* Do not attach when we have a serial console. */
    if (!con_is_present())
	return;
#else
    u16 tmp;
#endif

    for (pdev = pci_devices; pdev; pdev = pdev->next) {
	if (((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) &&
	    (pdev->vendor == PCI_VENDOR_ID_ATI)) {

	    info = kmalloc(sizeof(struct fb_info_aty), GFP_ATOMIC);
	    if (!info) {
		printk("atyfb_init: can't alloc fb_info_aty\n");
		return;
	    }
	    memset(info, 0, sizeof(struct fb_info_aty));

	    addr = pdev->base_address[0];
	    if ((addr & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO)
		addr = pdev->base_address[1];
	    if (!addr)
		continue;
	    addr &= PCI_BASE_ADDRESS_MEM_MASK;

#ifdef __sparc__
	    /*
	     * Map memory-mapped registers.
	     */
	    info->ati_regbase = addr + 0x7ffc00;
	    info->ati_regbase_phys = __pa(addr + 0x7ffc00);

	    /*
	     * Map in big-endian aperture.
	     */
	    info->frame_buffer = (unsigned long)(addr + 0x800000);
	    info->frame_buffer_phys = __pa(addr + 0x800000);

	    /*
	     * Figure mmap addresses from PCI config space.
	     * Split Framebuffer in big- and little-endian halfs.
	     */
	    for (i = 0; i < 6 && pdev->base_address[i]; i++)
		/* nothing */;
	    j = i + 3;

	    info->mmap_map = kmalloc(j * sizeof(*info->mmap_map), GFP_ATOMIC);
	    if (!info->mmap_map) {
		printk("atyfb_init: can't alloc mmap_map\n");
		kfree(info);
		return;
	    }
	    memset(info->mmap_map, 0, j * sizeof(*info->mmap_map));

	    for (i = 0, j = 2; i < 6 && pdev->base_address[i]; i++) {
		int io, breg = PCI_BASE_ADDRESS_0 + (i << 2);
		unsigned long base;
		u32 size, pbase;

		base = pdev->base_address[i];

		io = (base & PCI_BASE_ADDRESS_SPACE)==PCI_BASE_ADDRESS_SPACE_IO;

		pci_read_config_dword(pdev, breg, &pbase);
		pci_write_config_dword(pdev, breg, 0xffffffff);
		pci_read_config_dword(pdev, breg, &size);
		pci_write_config_dword(pdev, breg, pbase);

		if (io)
			size &= ~1;
		size = ~(size) + 1;

		if (base == addr) {
			info->mmap_map[j].voff = (pbase + 0x800000) & PAGE_MASK;
			info->mmap_map[j].poff = __pa((base + 0x800000)
								& PAGE_MASK);
			info->mmap_map[j].size = 0x800000;
			info->mmap_map[j].prot_mask = _PAGE_CACHE;
			info->mmap_map[j].prot_flag = _PAGE_E|_PAGE_IE;
			size -= 0x800000;
			j++;
		}

		info->mmap_map[j].voff = pbase & PAGE_MASK;
		info->mmap_map[j].poff = __pa(base & PAGE_MASK);
		info->mmap_map[j].size = (size + ~PAGE_MASK) & PAGE_MASK;
		info->mmap_map[j].prot_mask = _PAGE_CACHE;
		info->mmap_map[j].prot_flag = _PAGE_E;
		j++;
	    }

	    /*
	     * Fix PROMs idea of MEM_CNTL settings...
	     */
	    mem = aty_ld_le32(MEM_CNTL, info);
	    chip_id = aty_ld_le32(CONFIG_CHIP_ID, info);
	    if (((chip_id & CFG_CHIP_TYPE) == VT_CHIP_ID) &&
		!((chip_id >> 24) & 1)) {
		switch (mem & 0x0f) {
		    case 3:
			mem = (mem & ~(0x0f)) | 2;
			break;
		    case 7:
			mem = (mem & ~(0x0f)) | 3;
			break;
		    case 9:
			mem = (mem & ~(0x0f)) | 4;
			break;
		    case 11:
			mem = (mem & ~(0x0f)) | 5;
			break;
		    default:
			break;
		}
		if ((aty_ld_le32(CONFIG_STAT0, info) & 7) >= SDRAM)
			mem &= ~(0x00700000);
	    }
	    mem &= ~(0xcf80e000);	/* Turn off all undocumented bits. */
	    aty_st_le32(MEM_CNTL, mem, info);

	    /*
	     * If this is the console device, we will set default video
	     * settings to what the PROM left us with.
	     */
	    node = prom_getchild(prom_root_node);
	    node = prom_searchsiblings(node, "aliases");
	    if (node) {
		len = prom_getproperty(node, "screen", prop, sizeof(prop));
		if (len > 0) {
		    prop[len] = '\0';
		    node = prom_finddevice(prop);
		} else {
		    node = 0;
		}
	    }

	    pcp = pdev->sysdata;
	    if (node == pcp->prom_node) {

		struct fb_var_screeninfo *var = &default_var;
		unsigned int N, P, Q, M, T;
		u32 v_total, h_total;
		struct crtc crtc;
		u8 pll_regs[16];
		u8 clock_cntl;

		crtc.vxres = prom_getintdefault(node, "width", 1024);
		crtc.vyres = prom_getintdefault(node, "height", 768);
		crtc.bpp = prom_getintdefault(node, "depth", 8);
		crtc.xoffset = crtc.yoffset = 0;
		crtc.h_tot_disp = aty_ld_le32(CRTC_H_TOTAL_DISP, info);
		crtc.h_sync_strt_wid = aty_ld_le32(CRTC_H_SYNC_STRT_WID, info);
		crtc.v_tot_disp = aty_ld_le32(CRTC_V_TOTAL_DISP, info);
		crtc.v_sync_strt_wid = aty_ld_le32(CRTC_V_SYNC_STRT_WID, info);
		crtc.gen_cntl = aty_ld_le32(CRTC_GEN_CNTL, info);
		aty_crtc_to_var(&crtc, var);

		h_total = var->xres + var->right_margin +
			  var->hsync_len + var->left_margin;
		v_total = var->yres + var->lower_margin +
			  var->vsync_len + var->upper_margin;

		/*
		 * Read the PLL to figure actual Refresh Rate.
		 */
    		clock_cntl = aty_ld_8(CLOCK_CNTL, info);
		/* printk("atyfb: CLOCK_CNTL: %02x\n", clock_cntl); */
		for (i = 0; i < 16; i++)
			pll_regs[i] = aty_ld_pll(i, info);

		/*
		 * PLL Reference Devider M:
		 */
		M = pll_regs[2];

		/*
		 * PLL Feedback Devider N (Dependant on CLOCK_CNTL):
		 */
		N = pll_regs[7 + (clock_cntl & 3)];

		/*
		 * PLL Post Devider P (Dependant on CLOCK_CNTL):
		 */
		P = 1 << (pll_regs[6] >> ((clock_cntl & 3) << 1));

		/*
		 * PLL Devider Q:
		 */
		Q = N / P;

		/*
		 * Target Frequency:
		 *
		 *      T * M
		 * Q = -------
		 *      2 * R
		 *
		 * where R is XTALIN (= 14318 kHz).
		 */
		T = 2 * Q * 14318 / M;

		default_var.pixclock = 1000000000 / T;
	    }

#else /* __sparc__ */

	    info->ati_regbase_phys = 0x7ff000 + addr;
	    info->ati_regbase = (unsigned long)
				ioremap(info->ati_regbase_phys, 0x1000);

	    if(!info->ati_regbase) {
		    kfree(info);
		    return;
	    }

	    info->ati_regbase_phys += 0xc00;
	    info->ati_regbase += 0xc00;

	    /*
	     * Enable memory-space accesses using config-space
	     * command register.
	     */
	    pci_read_config_word(pdev, PCI_COMMAND, &tmp);
	    if (!(tmp & PCI_COMMAND_MEMORY)) {
		tmp |= PCI_COMMAND_MEMORY;
		pci_write_config_word(pdev, PCI_COMMAND, tmp);
	    }

#ifdef __BIG_ENDIAN
	    /* Use the big-endian aperture */
	    addr += 0x800000;
#endif

	    /* Map in frame buffer */
	    info->frame_buffer_phys = addr;
	    info->frame_buffer = (unsigned long)ioremap(addr, 0x800000);

	    if(!info->frame_buffer) {
		    kfree(info);
		    return;
	    }

#endif /* __sparc__ */

	    if (!aty_init(info, "PCI")) {
		if (info->mmap_map)
		    kfree(info->mmap_map);
		kfree(info);
		return;
	    }

#ifdef __sparc__
	    if (!prom_palette)
		prom_palette = atyfb_palette;

	    /*
	     * Add /dev/fb mmap values.
	     */
	    info->mmap_map[0].voff = 0x8000000000000000UL;
	    info->mmap_map[0].poff = __pa(info->frame_buffer & PAGE_MASK);
	    info->mmap_map[0].size = info->total_vram;
	    info->mmap_map[0].prot_mask = _PAGE_CACHE;
	    info->mmap_map[0].prot_flag = _PAGE_E;
	    info->mmap_map[1].voff = info->mmap_map[0].voff + info->total_vram;
	    info->mmap_map[1].poff = __pa(info->ati_regbase & PAGE_MASK);
	    info->mmap_map[1].size = PAGE_SIZE;
	    info->mmap_map[1].prot_mask = _PAGE_CACHE;
	    info->mmap_map[1].prot_flag = _PAGE_E;
#endif /* __sparc__ */
	}
    }
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
	if (!info) {
	    printk("atyfb_init: can't alloc fb_info_aty\n");
	    return;
	}
	memset(info, 0, sizeof(struct fb_info_aty));

	/*
	 *  Map the video memory (physical address given) to somewhere in the
	 *  kernel address space.
	 */
	info->frame_buffer = ioremap(phys_vmembase[m64_num], phys_size[m64_num]);
	info->frame_buffer_phys = info->frame_buffer;
	info->ati_regbase = ioremap(phys_guiregbase[m64_num], 0x10000)+0xFC00ul;
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

    switch (dp->n_addrs) {
	case 1:
	case 2:
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
	    return;
    }

    info = kmalloc(sizeof(struct fb_info_aty), GFP_ATOMIC);
    if (!info) {
	printk("atyfb_of_init: can't alloc fb_info_aty\n");
	return;
    }
    memset(info, 0, sizeof(struct fb_info_aty));

    info->ati_regbase_phys = 0x7ff000+addr;
    info->ati_regbase = (unsigned long)ioremap(info->ati_regbase_phys,
						   0x1000);

    if(! info->ati_regbase) {
	    printk("atyfb_init: ioremap() returned NULL\n");
	    kfree(info);
	    return;
    }

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

    if(! info->frame_buffer) {
	    printk("atyfb_init: ioremap() returned NULL\n");
	    kfree(info);
	    return;
    }

    if (!aty_init(info, dp->full_name)) {
	kfree(info);
	return;
    }

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (!console_fb_info)
	console_fb_info = &info->fb_info;
#endif /* CONFIG_FB_COMPAT_XPMAC */
}
#endif /* CONFIG_FB_OF */


__initfunc(void atyfb_setup(char *options, int *ints))
{
    char *this_opt;

    if (!options || !*options)
	return;

    for (this_opt = strtok(options, ","); this_opt;
	 this_opt = strtok(NULL, ",")) {
	if (!strncmp(this_opt, "font:", 5)) {
		char *p;
		int i;

		p = this_opt + 5;
		for (i = 0; i < sizeof(fontname) - 1; i++)
			if (!*p || *p == ' ' || *p == ',')
				break;
		memcpy(fontname, this_opt + 5, i);
		fontname[i] = 0;
	} else if (!strncmp(this_opt, "noblink", 7)) {
		curblink = 0;
	} else if (!strncmp(this_opt, "noaccel", 7)) {
		noaccel = 1;
	} else if (!strncmp(this_opt, "vram:", 5))
		default_vram = simple_strtoul(this_opt+5, NULL, 0);
	else if (!strncmp(this_opt, "pll:", 4))
		default_pll = simple_strtoul(this_opt+4, NULL, 0);
	else if (!strncmp(this_opt, "mclk:", 5))
		default_mclk = simple_strtoul(this_opt+5, NULL, 0);
#if defined(CONFIG_PPC)
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
#endif
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
#endif /* CONFIG_ATARI */

static int atyfbcon_switch(int con, struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;
    struct atyfb_par par;

    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, 1, atyfb_getcolreg, fb);

    /* Erase HW Cursor */
    if (info->cursor)
	atyfb_cursor(&fb_display[currcon], CM_ERASE,
		     info->cursor->pos.x, info->cursor->pos.y);

    currcon = con;

    atyfb_decode_var(&fb_display[con].var, &par, info);
    atyfb_set_par(&par, info);
    atyfb_set_disp(&fb_display[con], info, par.crtc.bpp,
		   par.accel_flags & FB_ACCELF_TEXT);

    /* Install new colormap */
    do_install_cmap(con, fb);

    /* Install hw cursor */
    if (info->cursor) {
	aty_set_cursor_color(info, cursor_pixel_map, cursor_color_map,
			     cursor_color_map, cursor_color_map);
	aty_set_cursor_shape(info);
    }
    return 1;
}

    /*
     *  Blank the display.
     */

static void atyfbcon_blank(int blank, struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;
    u8 gen_cntl;

    gen_cntl = aty_ld_8(CRTC_GEN_CNTL, info);
    if (blank > 0)
	switch (blank-1) {
	    case VESA_NO_BLANKING:
		gen_cntl |= 0x40;
		break;
	    case VESA_VSYNC_SUSPEND:
		gen_cntl |= 0x8;
		break;
	    case VESA_HSYNC_SUSPEND:
		gen_cntl |= 0x4;
		break;
	    case VESA_POWERDOWN:
		gen_cntl |= 0x4c;
		break;
	}
    else
	gen_cntl &= ~(0x4c);
    aty_st_8(CRTC_GEN_CNTL, gen_cntl, info);
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int atyfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;

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

static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;
    int i, scale;

    if (regno > 255)
	return 1;
    red >>= 8;
    green >>= 8;
    blue >>= 8;
    info->palette[regno].red = red;
    info->palette[regno].green = green;
    info->palette[regno].blue = blue;
    i = aty_ld_8(DAC_CNTL, info) & 0xfc;
    if ((Gx == GT_CHIP_ID) || (Gx == GU_CHIP_ID) || (Gx == LG_CHIP_ID) ||
	(Gx == GB_CHIP_ID) || (Gx == GD_CHIP_ID) || (Gx == GI_CHIP_ID) ||
	(Gx == GP_CHIP_ID) || (Gx == GQ_CHIP_ID))
	i |= 0x2;	/*DAC_CNTL|0x2 turns off the extra brightness for gt*/
    aty_st_8(DAC_CNTL, i, info);
    aty_st_8(DAC_MASK, 0xff, info);
    eieio();
    scale = ((Gx != GX_CHIP_ID) && (Gx != CX_CHIP_ID) &&
	     (info->current_par.crtc.bpp == 16)) ? 3 : 0;
    info->aty_cmap_regs->windex = regno << scale;
    eieio();
    info->aty_cmap_regs->lut = red;
    eieio();
    info->aty_cmap_regs->lut = green;
    eieio();
    info->aty_cmap_regs->lut = blue;
    eieio();
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
		i = (regno << 8) | regno;
		info->fbcon_cmap.cfb32[regno] = (i << 16) | i;
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
	fb_set_cmap(&fb_display[con].cmap, 1, atyfb_setcolreg, info);
    else {
	int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
	fb_set_cmap(fb_default_cmap(size), 1, atyfb_setcolreg, info);
    }
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
    info->blitter_may_be_busy = 1;
}

static inline void aty_rectcopy(int srcx, int srcy, int dstx, int dsty,
				u_int width, u_int height,
				struct fb_info_aty *info)
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
    aty_st_le32(DP_SRC, FRGD_SRC_BLIT, info);
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

    if (info->current_par.crtc.bpp == 24) {
	/* In 24 bpp, the engine is in 8 bpp - this requires that all */
	/* horizontal coordinates and widths must be adjusted */
	dstx *= 3;
	width *= 3;
    }

    wait_for_fifo(3, info);
    aty_st_le32(DP_FRGD_CLR, color, info);
    aty_st_le32(DP_SRC, BKGD_SRC_BKGD_CLR | FRGD_SRC_FRGD_CLR | MONO_SRC_ONE,
		info);
    aty_st_le32(DST_CNTL, DST_LAST_PEL | DST_Y_TOP_TO_BOTTOM |
			  DST_X_LEFT_TO_RIGHT, info);
    draw_rect(dstx, dsty, width, height, info);
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int atyfbcon_updatevar(int con, struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;
    struct atyfb_par *par = &info->current_par;
    struct display *p = &fb_display[con];
    struct vc_data *conp = p->conp;
    u32 yres, yoffset, sy, height;

    yres = ((par->crtc.v_tot_disp >> 16) & 0x7ff) + 1;
    yoffset = fb_display[con].var.yoffset;

    sy = (conp->vc_rows + p->yscroll) * fontheight(p);
    height = yres - conp->vc_rows * fontheight(p);

    if (height && (yoffset + yres > sy)) {
	u32 xres, xoffset;
	u32 bgx;

	xres = (((par->crtc.h_tot_disp >> 16) & 0xff) + 1) * 8;
	xoffset = fb_display[con].var.xoffset;


	bgx = attr_bgcol_ec(p, conp);
	bgx |= (bgx << 8);
	bgx |= (bgx << 16);

	if (sy + height > par->crtc.vyres) {
	    wait_for_fifo(1, info);
	    aty_st_le32(SC_BOTTOM, sy + height - 1, info);
	}
	aty_rectfill(xoffset, sy, xres, height, bgx, info);
    }

    if (info->cursor && (yoffset + yres <= sy))
	atyfb_cursor(p, CM_ERASE, info->cursor->pos.x, info->cursor->pos.y);

    info->current_par.crtc.yoffset = yoffset;
    set_off_pitch(&info->current_par, info);
    return 0;
}

    /*
     *  Text console acceleration
     */

static void fbcon_aty_bmove(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width)
{
#ifdef __sparc__
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    sx *= fontwidth(p);
    sy *= fontheight(p);
    dx *= fontwidth(p);
    dy *= fontheight(p);
    width *= fontwidth(p);
    height *= fontheight(p);

    aty_rectcopy(sx, sy, dx, dy, width, height,
		 (struct fb_info_aty *)p->fb_info);
}

static void fbcon_aty_clear(struct vc_data *conp, struct display *p, int sy,
			    int sx, int height, int width)
{
    u32 bgx;
#ifdef __sparc__
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    bgx = attr_bgcol_ec(p, conp);
    bgx |= (bgx << 8);
    bgx |= (bgx << 16);

    sx *= fontwidth(p);
    sy *= fontheight(p);
    width *= fontwidth(p);
    height *= fontheight(p);

    aty_rectfill(sx, sy, width, height, bgx,
		 (struct fb_info_aty *)p->fb_info);
}

#ifdef FBCON_HAS_CFB8
static void fbcon_aty8_putc(struct vc_data *conp, struct display *p, int c,
			    int yy, int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_aty8_putcs(struct vc_data *conp, struct display *p,
			     const unsigned short *s, int count, int yy,
			     int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_aty8_clear_margins(struct vc_data *conp, struct display *p,
				     int bottom_only)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb8_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty8 = {
    fbcon_cfb8_setup, fbcon_aty_bmove, fbcon_aty_clear, fbcon_aty8_putc,
    fbcon_aty8_putcs, fbcon_cfb8_revc, NULL, NULL, fbcon_aty8_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef FBCON_HAS_CFB16
static void fbcon_aty16_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb16_putc(conp, p, c, yy, xx);
}

static void fbcon_aty16_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy,
			      int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb16_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_aty16_clear_margins(struct vc_data *conp, struct display *p,
				      int bottom_only)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb16_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty16 = {
    fbcon_cfb16_setup, fbcon_aty_bmove, fbcon_aty_clear, fbcon_aty16_putc,
    fbcon_aty16_putcs, fbcon_cfb16_revc, NULL, NULL, fbcon_aty16_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef FBCON_HAS_CFB24
static void fbcon_aty24_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb24_putc(conp, p, c, yy, xx);
}

static void fbcon_aty24_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy,
			      int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb24_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_aty24_clear_margins(struct vc_data *conp, struct display *p,
				      int bottom_only)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb24_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty24 = {
    fbcon_cfb24_setup, fbcon_aty_bmove, fbcon_aty_clear, fbcon_aty24_putc,
    fbcon_aty24_putcs, fbcon_cfb24_revc, NULL, NULL, fbcon_aty24_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef FBCON_HAS_CFB32
static void fbcon_aty32_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb32_putc(conp, p, c, yy, xx);
}

static void fbcon_aty32_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy,
			      int xx)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb32_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_aty32_clear_margins(struct vc_data *conp, struct display *p,
				      int bottom_only)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)(p->fb_info);

#ifdef __sparc__
    if (fb->mmaped && currcon == fb->vtconsole)
	return;
#endif

    if (fb->blitter_may_be_busy)
	wait_for_idle((struct fb_info_aty *)p->fb_info);
    fbcon_cfb32_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_aty32 = {
    fbcon_cfb32_setup, fbcon_aty_bmove, fbcon_aty_clear, fbcon_aty32_putc,
    fbcon_aty32_putcs, fbcon_cfb32_revc, NULL, NULL, fbcon_aty32_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
