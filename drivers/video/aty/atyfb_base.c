
/*
 *  ATI Frame Buffer Device Driver Core
 *
 *	Copyright (C) 1997-2001  Geert Uytterhoeven
 *	Copyright (C) 1998  Bernd Harries
 *	Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 *  This driver supports the following ATI graphics chips:
 *    - ATI Mach64
 *
 *  To do: add support for
 *    - ATI Rage128 (from aty128fb.c)
 *    - ATI Radeon (from radeonfb.c)
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
 *  
 *  Many thanks to Nitya from ATI devrel for support and patience !
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
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>
#include <linux/kd.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#include "mach64.h"
#include "atyfb.h"

#ifdef __powerpc__
#include <asm/prom.h>
#include <video/macmodes.h>
#endif
#ifdef __sparc__
#include <asm/pbm.h>
#include <asm/fbio.h>
#endif

#ifdef CONFIG_ADB_PMU
#include <linux/adb.h>
#include <linux/pmu.h>
#endif
#ifdef CONFIG_NVRAM
#include <linux/nvram.h>
#endif
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif


/*
 * Debug flags.
 */
#undef DEBUG

/* Make sure n * PAGE_SIZE is protected at end of Aperture for GUI-regs */
/*  - must be large enough to catch all GUI-Regs   */
/*  - must be aligned to a PAGE boundary           */
#define GUI_RESERVE	(1 * PAGE_SIZE)


/* FIXME: remove the FAIL definition */
#define FAIL(x) do { printk(x "\n"); return -EINVAL; } while (0)


    /*
     *  The Hardware parameters for each card
     */

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
static int atyfb_rasterimg(struct fb_info *info, int start);


    /*
     *  Interface to the low level console driver
     */

static int atyfbcon_switch(int con, struct fb_info *fb);
static int atyfbcon_updatevar(int con, struct fb_info *fb);
static void atyfbcon_blank(int blank, struct fb_info *fb);


    /*
     *  Internal routines
     */

static int aty_init(struct fb_info_aty *info, const char *name);
#ifdef CONFIG_ATARI
static int store_video_par(char *videopar, unsigned char m64_num);
static char *strtoke(char *s, const char *ct);
#endif

static void aty_set_crtc(const struct fb_info_aty *info,
			 const struct crtc *crtc);
static int aty_var_to_crtc(const struct fb_info_aty *info,
			   const struct fb_var_screeninfo *var,
			   struct crtc *crtc);
static int aty_crtc_to_var(const struct crtc *crtc,
			   struct fb_var_screeninfo *var);

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
static void atyfb_set_dispsw(struct display *disp, struct fb_info_aty *info,
			     int bpp, int accel);
static int atyfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			 u_int *transp, struct fb_info *fb);
static int atyfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *fb);
static void do_install_cmap(int con, struct fb_info *info);
#ifdef CONFIG_PPC
static int read_aty_sense(const struct fb_info_aty *info);
#endif


    /*
     *  Interface used by the world
     */

int atyfb_init(void);
#ifndef MODULE
int atyfb_setup(char*);
#endif

static int currcon = 0;

static struct fb_ops atyfb_ops = {
	owner:		THIS_MODULE,
	fb_open:	atyfb_open,
	fb_release:	atyfb_release,
	fb_get_fix:	atyfb_get_fix,
	fb_get_var:	atyfb_get_var,
	fb_set_var:	atyfb_set_var,
	fb_get_cmap:	atyfb_get_cmap,
	fb_set_cmap:	atyfb_set_cmap,
	fb_pan_display:	atyfb_pan_display,
	fb_ioctl:	atyfb_ioctl,
#ifdef __sparc__
	fb_mmap:	atyfb_mmap,
#endif
	fb_rasterimg:	atyfb_rasterimg,
};

static char atyfb_name[16] = "ATY Mach64";
static char fontname[40] __initdata = { 0 };
static char curblink __initdata = 1;
static char noaccel __initdata = 0;
static u32 default_vram __initdata = 0;
static int default_pll __initdata = 0;
static int default_mclk __initdata = 0;

#ifndef MODULE
static const char *mode_option __initdata = NULL;
#endif

#ifdef CONFIG_PPC
#ifdef CONFIG_NVRAM_NOT_DEFINED
static int default_vmode __initdata = VMODE_NVRAM;
static int default_cmode __initdata = CMODE_NVRAM;
#else
static int default_vmode __initdata = VMODE_CHOOSE;
static int default_cmode __initdata = CMODE_CHOOSE;
#endif
#endif

#ifdef CONFIG_ATARI
static unsigned int mach64_count __initdata = 0;
static unsigned long phys_vmembase[FB_MAX] __initdata = { 0, };
static unsigned long phys_size[FB_MAX] __initdata = { 0, };
static unsigned long phys_guiregbase[FB_MAX] __initdata = { 0, };
#endif

static const char m64n_gx[] __initdata = "mach64GX (ATI888GX00)";
static const char m64n_cx[] __initdata = "mach64CX (ATI888CX00)";
static const char m64n_ct[] __initdata = "mach64CT (ATI264CT)";
static const char m64n_et[] __initdata = "mach64ET (ATI264ET)";
static const char m64n_vta3[] __initdata = "mach64VTA3 (ATI264VT)";
static const char m64n_vta4[] __initdata = "mach64VTA4 (ATI264VT)";
static const char m64n_vtb[] __initdata = "mach64VTB (ATI264VTB)";
static const char m64n_vt4[] __initdata = "mach64VT4 (ATI264VT4)";
static const char m64n_gt[] __initdata = "3D RAGE (GT)";
static const char m64n_gtb[] __initdata = "3D RAGE II+ (GTB)";
static const char m64n_iic_p[] __initdata = "3D RAGE IIC (PCI)";
static const char m64n_iic_a[] __initdata = "3D RAGE IIC (AGP)";
static const char m64n_lt[] __initdata = "3D RAGE LT";
static const char m64n_ltg[] __initdata = "3D RAGE LT-G";
static const char m64n_gtc_ba[] __initdata = "3D RAGE PRO (BGA, AGP)";
static const char m64n_gtc_ba1[] __initdata = "3D RAGE PRO (BGA, AGP, 1x only)";
static const char m64n_gtc_bp[] __initdata = "3D RAGE PRO (BGA, PCI)";
static const char m64n_gtc_pp[] __initdata = "3D RAGE PRO (PQFP, PCI)";
static const char m64n_gtc_ppl[] __initdata = "3D RAGE PRO (PQFP, PCI, limited 3D)";
static const char m64n_xl[] __initdata = "3D RAGE (XL)";
static const char m64n_ltp_a[] __initdata = "3D RAGE LT PRO (AGP)";
static const char m64n_ltp_p[] __initdata = "3D RAGE LT PRO (PCI)";
static const char m64n_mob_p[] __initdata = "3D RAGE Mobility (PCI)";
static const char m64n_mob_a[] __initdata = "3D RAGE Mobility (AGP)";


static const struct {
    u16 pci_id, chip_type;
    u8 rev_mask, rev_val;
    const char *name;
    int pll, mclk;
    u32 features;
} aty_chips[] __initdata = {
#ifdef CONFIG_FB_ATY_GX
    /* Mach64 GX */
    { 0x4758, 0x00d7, 0x00, 0x00, m64n_gx,      135,  50, M64F_GX },
    { 0x4358, 0x0057, 0x00, 0x00, m64n_cx,      135,  50, M64F_GX },
#endif /* CONFIG_FB_ATY_GX */

#ifdef CONFIG_FB_ATY_CT
    /* Mach64 CT */
    { 0x4354, 0x4354, 0x00, 0x00, m64n_ct,      135,  60, M64F_CT | M64F_INTEGRATED | M64F_CT_BUS | M64F_MAGIC_FIFO },
    { 0x4554, 0x4554, 0x00, 0x00, m64n_et,      135,  60, M64F_CT | M64F_INTEGRATED | M64F_CT_BUS | M64F_MAGIC_FIFO },

    /* Mach64 VT */
    { 0x5654, 0x5654, 0xc7, 0x00, m64n_vta3,    170,  67, M64F_VT | M64F_INTEGRATED | M64F_VT_BUS | M64F_MAGIC_FIFO | M64F_FIFO_24 },
    { 0x5654, 0x5654, 0xc7, 0x40, m64n_vta4,    200,  67, M64F_VT | M64F_INTEGRATED | M64F_VT_BUS | M64F_MAGIC_FIFO | M64F_FIFO_24 | M64F_MAGIC_POSTDIV },
    { 0x5654, 0x5654, 0x00, 0x00, m64n_vtb,     200,  67, M64F_VT | M64F_INTEGRATED | M64F_VT_BUS | M64F_GTB_DSP | M64F_FIFO_24 },
    { 0x5655, 0x5655, 0x00, 0x00, m64n_vtb,     200,  67, M64F_VT | M64F_INTEGRATED | M64F_VT_BUS | M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL },
    { 0x5656, 0x5656, 0x00, 0x00, m64n_vt4,     230,  83, M64F_VT | M64F_INTEGRATED | M64F_GTB_DSP },

    /* Mach64 GT (3D RAGE) */
    { 0x4754, 0x4754, 0x07, 0x00, m64n_gt,      135,  63, M64F_GT | M64F_INTEGRATED | M64F_MAGIC_FIFO | M64F_FIFO_24 | M64F_EXTRA_BRIGHT },
    { 0x4754, 0x4754, 0x07, 0x01, m64n_gt,      170,  67, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x4754, 0x4754, 0x07, 0x02, m64n_gt,      200,  67, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x4755, 0x4755, 0x00, 0x00, m64n_gtb,     200,  67, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x4756, 0x4756, 0x00, 0x00, m64n_iic_p,   230,  83, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x4757, 0x4757, 0x00, 0x00, m64n_iic_a,   230,  83, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x475a, 0x475a, 0x00, 0x00, m64n_iic_a,   230,  83, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP | M64F_FIFO_24 | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },

    /* Mach64 LT */
    { 0x4c54, 0x4c54, 0x00, 0x00, m64n_lt,      135,  63, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP },
    { 0x4c47, 0x4c47, 0x00, 0x00, m64n_ltg,     230,  63, M64F_GT | M64F_INTEGRATED | M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT | M64F_LT_SLEEP | M64F_G3_PB_1024x768 },

    /* Mach64 GTC (3D RAGE PRO) */
    { 0x4742, 0x4742, 0x00, 0x00, m64n_gtc_ba,  230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x4744, 0x4744, 0x00, 0x00, m64n_gtc_ba1, 230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x4749, 0x4749, 0x00, 0x00, m64n_gtc_bp,  230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT | M64F_MAGIC_VRAM_SIZE },
    { 0x4750, 0x4750, 0x00, 0x00, m64n_gtc_pp,  230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },
    { 0x4751, 0x4751, 0x00, 0x00, m64n_gtc_ppl, 230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT },

    /* 3D RAGE XL */
    { 0x4752, 0x4752, 0x00, 0x00, m64n_xl, 230, 120, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_SDRAM_MAGIC_PLL | M64F_EXTRA_BRIGHT | M64F_XL_DLL },

    /* Mach64 LT PRO */
    { 0x4c42, 0x4c42, 0x00, 0x00, m64n_ltp_a,   230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP },
    { 0x4c44, 0x4c44, 0x00, 0x00, m64n_ltp_p,   230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP },
    { 0x4c49, 0x4c49, 0x00, 0x00, m64n_ltp_p,   230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_EXTRA_BRIGHT | M64F_G3_PB_1_1 | M64F_G3_PB_1024x768 },
    { 0x4c50, 0x4c50, 0x00, 0x00, m64n_ltp_p,   230, 100, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP },

    /* 3D RAGE Mobility */
    { 0x4c4d, 0x4c4d, 0x00, 0x00, m64n_mob_p,   230,  50, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_MOBIL_BUS },
    { 0x4c4e, 0x4c4e, 0x00, 0x00, m64n_mob_a,   230,  50, M64F_GT | M64F_INTEGRATED | M64F_RESET_3D | M64F_GTB_DSP | M64F_MOBIL_BUS },
#endif /* CONFIG_FB_ATY_CT */
};

static const char ram_dram[] __initdata = "DRAM";
static const char ram_vram[] __initdata = "VRAM";
static const char ram_edo[] __initdata = "EDO";
static const char ram_sdram[] __initdata = "SDRAM";
static const char ram_sgram[] __initdata = "SGRAM";
static const char ram_wram[] __initdata = "WRAM";
static const char ram_off[] __initdata = "OFF";
static const char ram_resv[] __initdata = "RESV";

#ifdef CONFIG_FB_ATY_GX
static const char *aty_gx_ram[8] __initdata = {
    ram_dram, ram_vram, ram_vram, ram_dram,
    ram_dram, ram_vram, ram_vram, ram_resv
};
#endif /* CONFIG_FB_ATY_GX */

#ifdef CONFIG_FB_ATY_CT
static const char *aty_ct_ram[8] __initdata = {
    ram_off, ram_dram, ram_edo, ram_edo,
    ram_sdram, ram_sgram, ram_wram, ram_resv
};
#endif /* CONFIG_FB_ATY_CT */


#if defined(CONFIG_PPC)

    /*
     *  Apple monitor sense
     */

static int __init read_aty_sense(const struct fb_info_aty *info)
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

#if defined(CONFIG_PMAC_PBOOK) || defined(CONFIG_PMAC_BACKLIGHT)
static void aty_st_lcd(int index, u32 val, const struct fb_info_aty *info)
{
    unsigned long temp;

    /* write addr byte */
    temp = aty_ld_le32(LCD_INDEX, info);
    aty_st_le32(LCD_INDEX, (temp & ~LCD_INDEX_MASK) | index, info);
    /* write the register value */
    aty_st_le32(LCD_DATA, val, info);
}

static u32 aty_ld_lcd(int index, const struct fb_info_aty *info)
{
    unsigned long temp;

    /* write addr byte */
    temp = aty_ld_le32(LCD_INDEX, info);
    aty_st_le32(LCD_INDEX, (temp & ~LCD_INDEX_MASK) | index, info);
    /* read the register value */
    return aty_ld_le32(LCD_DATA, info);
}
#endif /* CONFIG_PMAC_PBOOK || CONFIG_PMAC_BACKLIGHT */

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
    } else if (bpp <= 24 && M64_HAS(INTEGRATED)) {
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
    if (M64_HAS(MAGIC_FIFO)) {
	/* Not VTB/GTB */
	/* FIXME: magic FIFO values */
	crtc->gen_cntl |= aty_ld_le32(CRTC_GEN_CNTL, info) & 0x000e0000;
    }
    crtc->dp_pix_width = dp_pix_width;
    crtc->dp_chain_mask = dp_chain_mask;

    return 0;
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
	    var->green.offset = 5;
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

static void atyfb_set_par(const struct atyfb_par *par,
			  struct fb_info_aty *info)
{
    u32 i;
    int accelmode;
    u8 tmp;

    accelmode = par->accel_flags;  /* hack */

    info->current_par = *par;

    if (info->blitter_may_be_busy)
	wait_for_idle(info);
    tmp = aty_ld_8(CRTC_GEN_CNTL + 3, info);
    aty_set_crtc(info, &par->crtc);
    aty_st_8(CLOCK_CNTL + info->clk_wr_offset, 0, info);
					/* better call aty_StrobeClock ?? */
    aty_st_8(CLOCK_CNTL + info->clk_wr_offset, CLOCK_STROBE, info);

    info->dac_ops->set_dac(info, &par->pll, par->crtc.bpp, accelmode);
    info->pll_ops->set_pll(info, &par->pll);

    if (!M64_HAS(INTEGRATED)) {
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
	i = aty_ld_le32(MEM_CNTL, info) & 0xf00fffff;
	if (!M64_HAS(MAGIC_POSTDIV))
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
	if (M64_HAS(CT_BUS)) {
	    aty_st_le32(DAC_CNTL, 0x87010184, info);
	    aty_st_le32(BUS_CNTL, 0x680000f9, info);
	} else if (M64_HAS(VT_BUS)) {
	    aty_st_le32(DAC_CNTL, 0x87010184, info);
	    aty_st_le32(BUS_CNTL, 0x680000f9, info);
	}  else if (M64_HAS(MOBIL_BUS)) {
	    aty_st_le32(DAC_CNTL, 0x80010102, info);
	    aty_st_le32(BUS_CNTL, 0x7b33a040, info);
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
	aty_init_engine(par, info);

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (!console_fb_info || console_fb_info == &info->fb_info) {
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

    if ((err = aty_var_to_crtc(info, var, &par->crtc)) ||
	(err = info->pll_ops->var_to_pll(info, var->pixclock, par->crtc.bpp,
		    			 &par->pll)))
	return err;

    if (var->accel_flags & FB_ACCELF_TEXT)
	par->accel_flags = FB_ACCELF_TEXT;
    else
	par->accel_flags = 0;

#if 0 /* fbmon is not done. uncomment for 2.5.x -brad */
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
    var->pixclock = info->pll_ops->pll_to_var(info, &par->pll);

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
	fb->open++;
	fb->mmaped = 0;
	fb->vtconsole = -1;
    } else {
	fb->consolecnt++;
    }
#endif
    return(0);
}

struct fb_var_screeninfo default_var = {
    /* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
    640, 480, 640, 480, 0, 0, 8, 0,
    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
    0, 0, -1, -1, 0, 39722, 48, 16, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};

static int atyfb_release(struct fb_info *info, int user)
{
#ifdef __sparc__
    struct fb_info_aty *fb = (struct fb_info_aty *)info;

    if (user) {
	fb->open--;
	mdelay(1);
	wait_for_idle(fb);
	if (!fb->open) {
		int was_mmaped = fb->mmaped;

		fb->mmaped = 0;
		if (fb->vtconsole != -1)
			vt_cons[fb->vtconsole]->vc_mode = KD_TEXT;
		fb->vtconsole = -1;

		if (was_mmaped) {
			struct fb_var_screeninfo var;

			/* Now reset the default display config, we have no
			 * idea what the program(s) which mmap'd the chip did
			 * to the configuration, nor whether it restored it
			 * correctly.
			 */
			var = default_var;
			if (noaccel)
				var.accel_flags &= ~FB_ACCELF_TEXT;
			else
				var.accel_flags |= FB_ACCELF_TEXT;
			if (var.yres == var.yres_virtual) {
				u32 vram = (fb->total_vram - (PAGE_SIZE << 2));
				var.yres_virtual = ((vram * 8) / var.bits_per_pixel) /
					var.xres_virtual;
				if (var.yres_virtual < var.yres)
					var.yres_virtual = var.yres;
			}
			atyfb_set_var(&var, -1, &fb->fb_info);
		}
	}
    } else {
	fb->consolecnt--;
    }
#endif
    return(0);
}


static int encode_fix(struct fb_fix_screeninfo *fix,
		      const struct atyfb_par *par,
		      const struct fb_info_aty *info)
{
    memset(fix, 0, sizeof(struct fb_fix_screeninfo));

    strcpy(fix->id, atyfb_name);
    fix->smem_start = info->frame_buffer_phys;
    fix->smem_len = (u32)info->total_vram;

    /*
     *  Reg Block 0 (CT-compatible block) is at ati_regbase_phys
     *  Reg Block 1 (multimedia extensions) is at ati_regbase_phys-0x400
     */
    if (M64_HAS(GX)) {
	fix->mmio_start = info->ati_regbase_phys;
	fix->mmio_len = 0x400;
	fix->accel = FB_ACCEL_ATI_MACH64GX;
    } else if (M64_HAS(CT)) {
	fix->mmio_start = info->ati_regbase_phys;
	fix->mmio_len = 0x400;
	fix->accel = FB_ACCEL_ATI_MACH64CT;
    } else if (M64_HAS(VT)) {
	fix->mmio_start = info->ati_regbase_phys-0x400;
	fix->mmio_len = 0x800;
	fix->accel = FB_ACCEL_ATI_MACH64VT;
    } else /* if (M64_HAS(GT)) */ {
	fix->mmio_start = info->ati_regbase_phys-0x400;
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


static void atyfb_set_dispsw(struct display *disp, struct fb_info_aty *info,
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
#ifdef CONFIG_FB_ATY_CT
	    if (info->cursor) {
		info->dispsw.cursor = atyfb_cursor;
		info->dispsw.set_font = atyfb_set_font;
	    }
#endif /* CONFIG_FB_ATY_CT */
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
	accel = var->accel_flags & FB_ACCELF_TEXT;
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
	    if (accel)
	    	display->scrollmode = (info->bus_type == PCI) ? SCROLL_YNOMOVE : 0;
	    else
	    	display->scrollmode = SCROLL_YREDRAW;
	    if (info->fb_info.changevar)
		(*info->fb_info.changevar)(con);
	}
	if (!info->fb_info.display_fg ||
	    info->fb_info.display_fg->vc_num == con) {
	    atyfb_set_par(&par, info);
	    atyfb_set_dispsw(display, info, par.crtc.bpp, accel);
	}
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
    if (!info->display_fg || con == info->display_fg->vc_num) /* current console? */
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
    struct display *disp;

    if (con >= 0)
    	disp = &fb_display[con];
    else
        disp = info->disp;
    if (!disp->cmap.len) {	/* no colormap allocated? */
	int size = disp->var.bits_per_pixel == 16 ? 32 : 256;
	if ((err = fb_alloc_cmap(&disp->cmap, size, 0)))
	    return err;
    }
    if (!info->display_fg || con == info->display_fg->vc_num)			/* current console? */
	return fb_set_cmap(cmap, kspc, atyfb_setcolreg, info);
    else
	fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
    return 0;
}


#ifdef DEBUG
#define ATYIO_CLKR		0x41545900	/* ATY\00 */
#define ATYIO_CLKW		0x41545901	/* ATY\01 */

struct atyclk {
    u32 ref_clk_per;
    u8 pll_ref_div;
    u8 mclk_fb_div;
    u8 mclk_post_div;		/* 1,2,3,4,8 */
    u8 vclk_fb_div;
    u8 vclk_post_div;		/* 1,2,3,4,6,8,12 */
    u32 dsp_xclks_per_row;	/* 0-16383 */
    u32 dsp_loop_latency;	/* 0-15 */
    u32 dsp_precision;		/* 0-7 */
    u32 dsp_on;			/* 0-2047 */
    u32 dsp_off;		/* 0-2047 */
};

#define ATYIO_FEATR		0x41545902	/* ATY\02 */
#define ATYIO_FEATW		0x41545903	/* ATY\03 */
#endif

static int atyfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info2)
{
#if defined(__sparc__) || (defined(DEBUG) && defined(CONFIG_FB_ATY_CT))
    struct fb_info_aty *info = (struct fb_info_aty *)info2;
#endif /* __sparc__ || DEBUG */
#ifdef __sparc__
    struct fbtype fbtyp;
    struct display *disp;

    if (con >= 0)
    	disp = &fb_display[con];
    else
        disp = info2->disp;
#endif

    switch (cmd) {
#ifdef __sparc__
    case FBIOGTYPE:
	fbtyp.fb_type = FBTYPE_PCI_GENERIC;
	fbtyp.fb_width = info->current_par.crtc.vxres;
	fbtyp.fb_height = info->current_par.crtc.vyres;
	fbtyp.fb_depth = info->current_par.crtc.bpp;
	fbtyp.fb_cmsize = disp->cmap.len;
	fbtyp.fb_size = info->total_vram;
	if (copy_to_user((struct fbtype *)arg, &fbtyp, sizeof(fbtyp)))
		return -EFAULT;
	break;
#endif /* __sparc__ */
#if defined(DEBUG) && defined(CONFIG_FB_ATY_CT)
    case ATYIO_CLKR:
	if (M64_HAS(INTEGRATED)) {
	    struct atyclk clk;
	    union aty_pll *pll = &info->current_par.pll;
	    u32 dsp_config = pll->ct.dsp_config;
	    u32 dsp_on_off = pll->ct.dsp_on_off;
	    clk.ref_clk_per = info->ref_clk_per;
	    clk.pll_ref_div = pll->ct.pll_ref_div;
	    clk.mclk_fb_div = pll->ct.mclk_fb_div;
	    clk.mclk_post_div = pll->ct.mclk_post_div_real;
	    clk.vclk_fb_div = pll->ct.vclk_fb_div;
	    clk.vclk_post_div = pll->ct.vclk_post_div_real;
	    clk.dsp_xclks_per_row = dsp_config & 0x3fff;
	    clk.dsp_loop_latency = (dsp_config>>16) & 0xf;
	    clk.dsp_precision = (dsp_config>>20) & 7;
	    clk.dsp_on = dsp_on_off & 0x7ff;
	    clk.dsp_off = (dsp_on_off>>16) & 0x7ff;
	    if (copy_to_user((struct atyclk *)arg, &clk, sizeof(clk)))
		    return -EFAULT;
	} else
	    return -EINVAL;
	break;
    case ATYIO_CLKW:
	if (M64_HAS(INTEGRATED)) {
	    struct atyclk clk;
	    union aty_pll *pll = &info->current_par.pll;
	    if (copy_from_user(&clk, (struct atyclk *)arg, sizeof(clk)))
		    return -EFAULT;
	    info->ref_clk_per = clk.ref_clk_per;
	    pll->ct.pll_ref_div = clk.pll_ref_div;
	    pll->ct.mclk_fb_div = clk.mclk_fb_div;
	    pll->ct.mclk_post_div_real = clk.mclk_post_div;
	    pll->ct.vclk_fb_div = clk.vclk_fb_div;
	    pll->ct.vclk_post_div_real = clk.vclk_post_div;
	    pll->ct.dsp_config = (clk.dsp_xclks_per_row & 0x3fff) |
				 ((clk.dsp_loop_latency & 0xf)<<16) |
				 ((clk.dsp_precision & 7)<<20);
	    pll->ct.dsp_on_off = (clk.dsp_on & 0x7ff) |
				 ((clk.dsp_off & 0x7ff)<<16);
	    aty_calc_pll_ct(info, &pll->ct);
	    aty_set_pll_ct(info, pll);
	} else
	    return -EINVAL;
	break;
    case ATYIO_FEATR:
	if (get_user(info->features, (u32 *)arg))
	    return -EFAULT;
	break;
    case ATYIO_FEATW:
	if (put_user(info->features, (u32 *)arg))
	    return -EFAULT;
	break;
#endif /* DEBUG && CONFIG_FB_ATY_CT */
    default:
	return -EINVAL;
    }
    return 0;
}

static int atyfb_rasterimg(struct fb_info *info, int start)
{
    struct fb_info_aty *fb = (struct fb_info_aty *)info;

    if (fb->blitter_may_be_busy)
	wait_for_idle(fb);
    return 0;
}

#ifdef __sparc__
static int atyfb_mmap(struct fb_info *info, struct file *file,
		      struct vm_area_struct *vma)
{
	struct fb_info_aty *fb = (struct fb_info_aty *)info;
	unsigned int size, page, map_size = 0;
	unsigned long map_offset = 0;
	unsigned long off;
	int i;

	if (!fb->mmap_map)
		return -ENXIO;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;
	size = vma->vm_end - vma->vm_start;

	/* To stop the swapper from even considering these pages. */
	vma->vm_flags |= (VM_SHM | VM_LOCKED);

	if (((vma->vm_pgoff == 0) && (size == fb->total_vram)) ||
	    ((off == fb->total_vram) && (size == PAGE_SIZE)))
		off += 0x8000000000000000UL;

	vma->vm_pgoff = off >> PAGE_SHIFT;	/* propagate off changes */

	/* Each page, see which map applies */
	for (page = 0; page < size; ) {
		map_size = 0;
		for (i = 0; fb->mmap_map[i].size; i++) {
			unsigned long start = fb->mmap_map[i].voff;
			unsigned long end = start + fb->mmap_map[i].size;
			unsigned long offset = off + page;

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
	int i, tmp;

	for (i = 0; i < 256; i++) {
		tmp = aty_ld_8(DAC_CNTL, info) & 0xfc;
		if (M64_HAS(EXTRA_BRIGHT))
			tmp |= 0x2;
		aty_st_8(DAC_CNTL, tmp, info);
		aty_st_8(DAC_MASK, 0xff, info);

		writeb(i, &info->aty_cmap_regs->rindex);
		atyfb_save.r[enter][i] = readb(&info->aty_cmap_regs->lut);
		atyfb_save.g[enter][i] = readb(&info->aty_cmap_regs->lut);
		atyfb_save.b[enter][i] = readb(&info->aty_cmap_regs->lut);
		writeb(i, &info->aty_cmap_regs->windex);
		writeb(atyfb_save.r[1-enter][i], &info->aty_cmap_regs->lut);
		writeb(atyfb_save.g[1-enter][i], &info->aty_cmap_regs->lut);
		writeb(atyfb_save.b[1-enter][i], &info->aty_cmap_regs->lut);
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



#ifdef CONFIG_PMAC_PBOOK

static struct fb_info_aty* first_display = NULL;

/* Power management routines. Those are used for PowerBook sleep.
 *
 * It appears that Rage LT and Rage LT Pro have different power
 * management registers. There's is some confusion about which
 * chipID is a Rage LT or LT pro :(
 */
static int aty_power_mgmt_LT(int sleep, struct fb_info_aty *info)
{
 	unsigned int pm;
	int timeout;
	
	pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
	pm = (pm & ~PWR_MGT_MODE_MASK) | PWR_MGT_MODE_REG;
	aty_st_le32(POWER_MANAGEMENT_LG, pm, info);
	pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
	
	timeout = 200000;
	if (sleep) {
		/* Sleep */
		pm &= ~PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, info);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
		udelay(10);
		pm &= ~(PWR_BLON | AUTO_PWR_UP);
		pm |= SUSPEND_NOW;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, info);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, info);
		do {
			pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
			udelay(10);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) != PWR_MGT_STATUS_SUSPEND);
	} else {
		/* Wakeup */
		pm &= ~PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, info);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
		udelay(10);
		pm |=  (PWR_BLON | AUTO_PWR_UP);
		pm &= ~SUSPEND_NOW;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, info);
		pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_le32(POWER_MANAGEMENT_LG, pm, info);
		do {
			pm = aty_ld_le32(POWER_MANAGEMENT_LG, info);
			udelay(10);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) != 0);
	}
	mdelay(500);

	return timeout ? PBOOK_SLEEP_OK : PBOOK_SLEEP_REFUSE;
}

static int aty_power_mgmt_LTPro(int sleep, struct fb_info_aty *info)
{
 	unsigned int pm;
	int timeout;
	
	pm = aty_ld_lcd(POWER_MANAGEMENT, info);
	pm = (pm & ~PWR_MGT_MODE_MASK) | PWR_MGT_MODE_REG;
	aty_st_lcd(POWER_MANAGEMENT, pm, info);
	pm = aty_ld_lcd(POWER_MANAGEMENT, info);

	timeout = 200;
	if (sleep) {
		/* Sleep */
		pm &= ~PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, info);
		pm = aty_ld_lcd(POWER_MANAGEMENT, info);
		udelay(10);
		pm &= ~(PWR_BLON | AUTO_PWR_UP);
		pm |= SUSPEND_NOW;
		aty_st_lcd(POWER_MANAGEMENT, pm, info);
		pm = aty_ld_lcd(POWER_MANAGEMENT, info);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, info);
		do {
			pm = aty_ld_lcd(POWER_MANAGEMENT, info);
			mdelay(1);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) != PWR_MGT_STATUS_SUSPEND);
	} else {
		/* Wakeup */
		pm &= ~PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, info);
		pm = aty_ld_lcd(POWER_MANAGEMENT, info);
		udelay(10);
		pm &= ~SUSPEND_NOW;
		pm |= (PWR_BLON | AUTO_PWR_UP);
		aty_st_lcd(POWER_MANAGEMENT, pm, info);
		pm = aty_ld_lcd(POWER_MANAGEMENT, info);
		udelay(10);
		pm |= PWR_MGT_ON;
		aty_st_lcd(POWER_MANAGEMENT, pm, info);
		do {
			pm = aty_ld_lcd(POWER_MANAGEMENT, info);			
			mdelay(1);
			if ((--timeout) == 0)
				break;
		} while ((pm & PWR_MGT_STATUS_MASK) != 0);
	}

	return timeout ? PBOOK_SLEEP_OK : PBOOK_SLEEP_REFUSE;
}

static int aty_power_mgmt(int sleep, struct fb_info_aty *info)
{
    return M64_HAS(LT_SLEEP) ? aty_power_mgmt_LT(sleep, info)
			     : aty_power_mgmt_LTPro(sleep, info);
}

/*
 * Save the contents of the frame buffer when we go to sleep,
 * and restore it when we wake up again.
 */
static int aty_sleep_notify(struct pmu_sleep_notifier *self, int when)
{
	struct fb_info_aty *info;
 	int result;

	result = PBOOK_SLEEP_OK;

	for (info = first_display; info != NULL; info = info->next) {
		struct fb_fix_screeninfo fix;
		int nb;

		atyfb_get_fix(&fix, fg_console, (struct fb_info *)info);
		nb = fb_display[fg_console].var.yres * fix.line_length;

		switch (when) {
		case PBOOK_SLEEP_REQUEST:
			info->save_framebuffer = vmalloc(nb);
			if (info->save_framebuffer == NULL)
				return PBOOK_SLEEP_REFUSE;
			break;
		case PBOOK_SLEEP_REJECT:
			if (info->save_framebuffer) {
				vfree(info->save_framebuffer);
				info->save_framebuffer = 0;
			}
			break;
		case PBOOK_SLEEP_NOW:
			if (info->blitter_may_be_busy)
				wait_for_idle(info);
			/* Stop accel engine (stop bus mastering) */
			if (info->current_par.accel_flags & FB_ACCELF_TEXT)
				aty_reset_engine(info);

			/* Backup fb content */	
			if (info->save_framebuffer)
				memcpy_fromio(info->save_framebuffer,
				       (void *)info->frame_buffer, nb);

			/* Blank display and LCD */
			atyfbcon_blank(VESA_POWERDOWN+1, (struct fb_info *)info);

			/* Set chip to "suspend" mode */
			result = aty_power_mgmt(1, info);
			break;
		case PBOOK_WAKE:
			/* Wakeup chip */
			result = aty_power_mgmt(0, info);

			/* Restore fb content */			
			if (info->save_framebuffer) {
				memcpy_toio((void *)info->frame_buffer,
				       info->save_framebuffer, nb);
				vfree(info->save_framebuffer);
				info->save_framebuffer = 0;
			}
			/* Restore display */
			atyfb_set_par(&info->current_par, info);
			atyfbcon_blank(0, (struct fb_info *)info);
			break;
		}
	}
	return result;
}

static struct pmu_sleep_notifier aty_sleep_notifier = {
        aty_sleep_notify, SLEEP_LEVEL_VIDEO,
};
#endif /* CONFIG_PMAC_PBOOK */

#ifdef CONFIG_PMAC_BACKLIGHT

    /*
     *   LCD backlight control
     */

static int backlight_conv[] = {
	0x00, 0x3f, 0x4c, 0x59, 0x66, 0x73, 0x80, 0x8d,
	0x9a, 0xa7, 0xb4, 0xc1, 0xcf, 0xdc, 0xe9, 0xff
};

static int
aty_set_backlight_enable(int on, int level, void* data)
{
	struct fb_info_aty *info = (struct fb_info_aty *)data;
	unsigned int reg = aty_ld_lcd(LCD_MISC_CNTL, info);
	
	reg |= (BLMOD_EN | BIASMOD_EN);
	if (on && level > BACKLIGHT_OFF) {
		reg &= ~BIAS_MOD_LEVEL_MASK;
		reg |= (backlight_conv[level] << BIAS_MOD_LEVEL_SHIFT);
	} else {
		reg &= ~BIAS_MOD_LEVEL_MASK;
		reg |= (backlight_conv[0] << BIAS_MOD_LEVEL_SHIFT);
	}
	aty_st_lcd(LCD_MISC_CNTL, reg, info);

	return 0;
}

static int
aty_set_backlight_level(int level, void* data)
{
	return aty_set_backlight_enable(1, level, data);
}

static struct backlight_controller aty_backlight_controller = {
	aty_set_backlight_enable,
	aty_set_backlight_level
};
#endif /* CONFIG_PMAC_BACKLIGHT */



    /*
     *  Initialisation
     */

static struct fb_info_aty *fb_list = NULL;

static int __init aty_init(struct fb_info_aty *info, const char *name)
{
    u32 chip_id;
    u32 i;
    int j, k;
    struct fb_var_screeninfo var;
    struct display *disp;
    u16 type;
    u8 rev;
    const char *chipname = NULL, *ramname = NULL, *xtal;
    int pll, mclk, gtb_memsize;
#if defined(CONFIG_PPC)
    int sense;
#endif
    u8 pll_ref_div;

    info->aty_cmap_regs = (struct aty_cmap_regs *)(info->ati_regbase+0xc0);
    chip_id = aty_ld_le32(CONFIG_CHIP_ID, info);
    type = chip_id & CFG_CHIP_TYPE;
    rev = (chip_id & CFG_CHIP_REV)>>24;
    for (j = 0; j < (sizeof(aty_chips)/sizeof(*aty_chips)); j++)
	if (type == aty_chips[j].chip_type &&
	    (rev & aty_chips[j].rev_mask) == aty_chips[j].rev_val) {
	    chipname = aty_chips[j].name;
	    pll = aty_chips[j].pll;
	    mclk = aty_chips[j].mclk;
	    info->features = aty_chips[j].features;
	    goto found;
	}
    printk("atyfb: Unknown mach64 0x%04x rev 0x%04x\n", type, rev);
    return 0;

found:
    printk("atyfb: %s [0x%04x rev 0x%02x] ", chipname, type, rev);
#ifdef CONFIG_FB_ATY_GX
    if (!M64_HAS(INTEGRATED)) {
	u32 stat0;
	u8 dac_type, dac_subtype, clk_type;
	stat0 = aty_ld_le32(CONFIG_STAT0, info);
	info->bus_type = (stat0 >> 0) & 0x07;
	info->ram_type = (stat0 >> 3) & 0x07;
	ramname = aty_gx_ram[info->ram_type];
	/* FIXME: clockchip/RAMDAC probing? */
	dac_type = (aty_ld_le32(DAC_CNTL, info) >> 16) & 0x07;
#ifdef CONFIG_ATARI
	clk_type = CLK_ATI18818_1;
	dac_type = (stat0 >> 9) & 0x07;
	if (dac_type == 0x07)
	    dac_subtype = DAC_ATT20C408;
	else
	    dac_subtype = (aty_ld_8(SCRATCH_REG1 + 1, info) & 0xF0) | dac_type;
#else
	dac_type = DAC_IBMRGB514;
	dac_subtype = DAC_IBMRGB514;
	clk_type = CLK_IBMRGB514;
#endif
	switch (dac_subtype) {
	    case DAC_IBMRGB514:
		info->dac_ops = &aty_dac_ibm514;
		break;
	    case DAC_ATI68860_B:
	    case DAC_ATI68860_C:
		info->dac_ops = &aty_dac_ati68860b;
		break;
	    case DAC_ATT20C408:
	    case DAC_ATT21C498:
		info->dac_ops = &aty_dac_att21c498;
		break;
	    default:
		printk(" atyfb_set_par: DAC type not implemented yet!\n");
		info->dac_ops = &aty_dac_unsupported;
		break;
	}
	switch (clk_type) {
	    case CLK_ATI18818_1:
		info->pll_ops = &aty_pll_ati18818_1;
		break;
	    case CLK_STG1703:
		info->pll_ops = &aty_pll_stg1703;
		break;
	    case CLK_CH8398:
		info->pll_ops = &aty_pll_ch8398;
		break;
	    case CLK_ATT20C408:
		info->pll_ops = &aty_pll_att20c408;
		break;
	    case CLK_IBMRGB514:
		info->pll_ops = &aty_pll_ibm514;
		break;
	    default:
		printk(" atyfb_set_par: CLK type not implemented yet!");
		info->pll_ops = &aty_pll_unsupported;
		break;
	}
    }
#endif /* CONFIG_FB_ATY_GX */
#ifdef CONFIG_FB_ATY_CT
    if (M64_HAS(INTEGRATED)) {
	info->bus_type = PCI;
	info->ram_type = (aty_ld_le32(CONFIG_STAT0, info) & 0x07);
	ramname = aty_ct_ram[info->ram_type];
	info->dac_ops = &aty_dac_ct;
	info->pll_ops = &aty_pll_ct;
	/* for many chips, the mclk is 67 MHz for SDRAM, 63 MHz otherwise */
	if (mclk == 67 && info->ram_type < SDRAM)
	    mclk = 63;
    }
#endif /* CONFIG_FB_ATY_CT */

    info->ref_clk_per = 1000000000000ULL/14318180;
    xtal = "14.31818";
    if (M64_HAS(GTB_DSP) && (pll_ref_div = aty_ld_pll(PLL_REF_DIV, info))) {
	int diff1, diff2;
	diff1 = 510*14/pll_ref_div-pll;
	diff2 = 510*29/pll_ref_div-pll;
	if (diff1 < 0)
	    diff1 = -diff1;
	if (diff2 < 0)
	    diff2 = -diff2;
	if (diff2 < diff1) {
	    info->ref_clk_per = 1000000000000ULL/29498928;
	    xtal = "29.498928";
	}
    }

    i = aty_ld_le32(MEM_CNTL, info);
    gtb_memsize = M64_HAS(GTB_DSP);
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

    if (M64_HAS(MAGIC_VRAM_SIZE)) {
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

    printk("%d%c %s, %s MHz XTAL, %d MHz PLL, %d Mhz MCLK\n",
    	   info->total_vram == 0x80000 ? 512 : (info->total_vram >> 20),
    	   info->total_vram == 0x80000 ? 'K' : 'M', ramname, xtal, pll, mclk);

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
    if (M64_HAS(INTEGRATED)) {
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

    /*
     *  Last page of 8 MB (4 MB on ISA) aperture is MMIO
     *  FIXME: we should use the auxiliary aperture instead so we can access
     *  the full 8 MB of video RAM on 8 MB boards
     */
    if (info->total_vram == 0x800000 ||
	(info->bus_type == ISA && info->total_vram == 0x400000))
	    info->total_vram -= GUI_RESERVE;

    /* Clear the video memory */
    fb_memset((void *)info->frame_buffer, 0, info->total_vram);

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

#ifdef CONFIG_PMAC_BACKLIGHT
    if (M64_HAS(G3_PB_1_1) && machine_is_compatible("PowerBook1,1")) {
	/* these bits let the 101 powerbook wake up from sleep -- paulus */
	aty_st_lcd(POWER_MANAGEMENT, aty_ld_lcd(POWER_MANAGEMENT, info)
		| (USE_F32KHZ | TRISTATE_MEM_EN), info);
    }
    if (M64_HAS(MOBIL_BUS))
	register_backlight_controller(&aty_backlight_controller, info, "ati");
#endif /* CONFIG_PMAC_BACKLIGHT */

#ifdef MODULE
    var = default_var;
#else /* !MODULE */
    memset(&var, 0, sizeof(var));
#ifdef CONFIG_PPC
    if (_machine == _MACH_Pmac) {
	    /*
	     *  FIXME: The NVRAM stuff should be put in a Mac-specific file, as it
	     *         applies to all Mac video cards
	     */
	    if (mode_option) {
		if (!mac_find_mode(&var, &info->fb_info, mode_option, 8))
		    var = default_var;
	    } else {
#ifdef CONFIG_NVRAM
		if (default_vmode == VMODE_NVRAM) {
		    default_vmode = nvram_read_byte(NV_VMODE);
		    if (default_vmode <= 0 || default_vmode > VMODE_MAX)
			default_vmode = VMODE_CHOOSE;
		}
#endif
		if (default_vmode == VMODE_CHOOSE) {
		    if (M64_HAS(G3_PB_1024x768))
			/* G3 PowerBook with 1024x768 LCD */
			default_vmode = VMODE_1024_768_60;
		    else if (machine_is_compatible("iMac"))
			default_vmode = VMODE_1024_768_75;
		    else if (machine_is_compatible("PowerBook2,1"))
			/* iBook with 800x600 LCD */
			default_vmode = VMODE_800_600_60;
		    else
			default_vmode = VMODE_640_480_67;
		    sense = read_aty_sense(info);
		    printk(KERN_INFO "atyfb: monitor sense=%x, mode %d\n",
			   sense, mac_map_monitor_sense(sense));
		}
		if (default_vmode <= 0 || default_vmode > VMODE_MAX)
		    default_vmode = VMODE_640_480_60;
#ifdef CONFIG_NVRAM
		if (default_cmode == CMODE_NVRAM)
		    default_cmode = nvram_read_byte(NV_CMODE);
#endif
		if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
		    default_cmode = CMODE_8;
		if (mac_vmode_to_var(default_vmode, default_cmode, &var))
		    var = default_var;
	    }
    }
    else if (!fb_find_mode(&var, &info->fb_info, mode_option, NULL, 0, NULL, 8))
	var = default_var;
#else /* !CONFIG_PPC */
#ifdef __sparc__
    if (mode_option) {
    	if (!fb_find_mode(&var, &info->fb_info, mode_option, NULL, 0, NULL, 8))
	    var = default_var;
    } else
	var = default_var;
#else
    if (!fb_find_mode(&var, &info->fb_info, mode_option, NULL, 0, NULL, 8))
	var = default_var;
#endif /* !__sparc__ */
#endif /* !CONFIG_PPC */
#endif /* !MODULE */
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

#ifdef __sparc__
    atyfb_save_palette(&info->fb_info, 0);
#endif
    for (j = 0; j < 16; j++) {
	k = color_table[j];
	info->palette[j].red = default_red[k];
	info->palette[j].green = default_grn[k];
	info->palette[j].blue = default_blu[k];
    }

#ifdef CONFIG_FB_ATY_CT
    if (curblink && M64_HAS(INTEGRATED)) {
	info->cursor = aty_init_cursor(info);
	if (info->cursor) {
	    info->dispsw.cursor = atyfb_cursor;
	    info->dispsw.set_font = atyfb_set_font;
	}
    }
#endif /* CONFIG_FB_ATY_CT */

    atyfb_set_var(&var, -1, &info->fb_info);

    if (register_framebuffer(&info->fb_info) < 0)
	return 0;

    info->next = fb_list;
    fb_list = info;

    printk("fb%d: %s frame buffer device on %s\n",
	   GET_FB_IDX(info->fb_info.node), atyfb_name, name);
    return 1;
}

int __init atyfb_init(void)
{
#if defined(CONFIG_PCI)
    struct pci_dev *pdev = NULL;
    struct fb_info_aty *info;
    unsigned long addr, res_start, res_size;
    int i;
#ifdef __sparc__
    extern void (*prom_palette) (int);
    extern int con_is_present(void);
    struct pcidev_cookie *pcp;
    char prop[128];
    int node, len, j;
    u32 mem, chip_id;

    /* Do not attach when we have a serial console. */
    if (!con_is_present())
	return -ENXIO;
#else
    u16 tmp;
#endif

    while ((pdev = pci_find_device(PCI_VENDOR_ID_ATI, PCI_ANY_ID, pdev))) {
	if ((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
	    struct resource *rp;

	    for (i = sizeof(aty_chips)/sizeof(*aty_chips)-1; i >= 0; i--)
		if (pdev->device == aty_chips[i].pci_id)
		    break;
	    if (i < 0)
		continue;

	    info = kmalloc(sizeof(struct fb_info_aty), GFP_ATOMIC);
	    if (!info) {
		printk("atyfb_init: can't alloc fb_info_aty\n");
		return -ENXIO;
	    }
	    memset(info, 0, sizeof(struct fb_info_aty));

	    rp = &pdev->resource[0];
	    if (rp->flags & IORESOURCE_IO)
		    rp = &pdev->resource[1];
	    addr = rp->start;
	    if (!addr)
		continue;

	    res_start = rp->start;
	    res_size = rp->end-rp->start+1;
	    if (!request_mem_region(res_start, res_size, "atyfb"))
		continue;

#ifdef __sparc__
	    /*
	     * Map memory-mapped registers.
	     */
	    info->ati_regbase = addr + 0x7ffc00UL;
	    info->ati_regbase_phys = addr + 0x7ffc00UL;

	    /*
	     * Map in big-endian aperture.
	     */
	    info->frame_buffer = (unsigned long) addr + 0x800000UL;
	    info->frame_buffer_phys = addr + 0x800000UL;

	    /*
	     * Figure mmap addresses from PCI config space.
	     * Split Framebuffer in big- and little-endian halfs.
	     */
	    for (i = 0; i < 6 && pdev->resource[i].start; i++)
		/* nothing */;
	    j = i + 4;

	    info->mmap_map = kmalloc(j * sizeof(*info->mmap_map), GFP_ATOMIC);
	    if (!info->mmap_map) {
		printk("atyfb_init: can't alloc mmap_map\n");
		kfree(info);
		release_mem_region(res_start, res_size);
		return -ENXIO;
	    }
	    memset(info->mmap_map, 0, j * sizeof(*info->mmap_map));

	    for (i = 0, j = 2; i < 6 && pdev->resource[i].start; i++) {
		struct resource *rp = &pdev->resource[i];
		int io, breg = PCI_BASE_ADDRESS_0 + (i << 2);
		unsigned long base;
		u32 size, pbase;

		base = rp->start;

		io = (rp->flags & IORESOURCE_IO);

		size = rp->end - base + 1;

		pci_read_config_dword(pdev, breg, &pbase);

		if (io)
			size &= ~1;

		/*
		 * Map the framebuffer a second time, this time without
		 * the braindead _PAGE_IE setting. This is used by the
		 * fixed Xserver, but we need to maintain the old mapping
		 * to stay compatible with older ones...
		 */
		if (base == addr) {
		    info->mmap_map[j].voff = (pbase + 0x10000000) & PAGE_MASK;
		    info->mmap_map[j].poff = base & PAGE_MASK;
		    info->mmap_map[j].size = (size + ~PAGE_MASK) & PAGE_MASK;
		    info->mmap_map[j].prot_mask = _PAGE_CACHE;
		    info->mmap_map[j].prot_flag = _PAGE_E;
		    j++;
		}

		/*
		 * Here comes the old framebuffer mapping with _PAGE_IE
		 * set for the big endian half of the framebuffer...
		 */
		if (base == addr) {
		    info->mmap_map[j].voff = (pbase + 0x800000) & PAGE_MASK;
		    info->mmap_map[j].poff = (base+0x800000) & PAGE_MASK;
		    info->mmap_map[j].size = 0x800000;
		    info->mmap_map[j].prot_mask = _PAGE_CACHE;
		    info->mmap_map[j].prot_flag = _PAGE_E|_PAGE_IE;
		    size -= 0x800000;
		    j++;
		}

		info->mmap_map[j].voff = pbase & PAGE_MASK;
		info->mmap_map[j].poff = base & PAGE_MASK;
		info->mmap_map[j].size = (size + ~PAGE_MASK) & PAGE_MASK;
		info->mmap_map[j].prot_mask = _PAGE_CACHE;
		info->mmap_map[j].prot_flag = _PAGE_E;
		j++;
	    }

	    if (pdev->device != XL_CHIP_ID) {
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
	    }

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
		unsigned int N, P, Q, M, T, R;
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
		 * PLL Reference Divider M:
		 */
		M = pll_regs[2];

		/*
		 * PLL Feedback Divider N (Dependant on CLOCK_CNTL):
		 */
		N = pll_regs[7 + (clock_cntl & 3)];

		/*
		 * PLL Post Divider P (Dependant on CLOCK_CNTL):
		 */
		P = 1 << (pll_regs[6] >> ((clock_cntl & 3) << 1));

		/*
		 * PLL Divider Q:
		 */
		Q = N / P;

		/*
		 * Target Frequency:
		 *
		 *      T * M
		 * Q = -------
		 *      2 * R
		 *
		 * where R is XTALIN (= 14318 or 29498 kHz).
		 */
		if (pdev->device == XL_CHIP_ID)
			R = 29498;
		else
			R = 14318;

		T = 2 * Q * R / M;

		default_var.pixclock = 1000000000 / T;
	    }
#else /* __sparc__ */

	    info->ati_regbase_phys = 0x7ff000 + addr;
	    info->ati_regbase = (unsigned long)
				ioremap(info->ati_regbase_phys, 0x1000);

	    if(!info->ati_regbase) {
		    kfree(info);
		    release_mem_region(res_start, res_size);
		    return -ENOMEM;
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
		    release_mem_region(res_start, res_size);
		    return -ENXIO;
	    }

#endif /* __sparc__ */

	    if (!aty_init(info, "PCI")) {
		if (info->mmap_map)
		    kfree(info->mmap_map);
		kfree(info);
		release_mem_region(res_start, res_size);
		return -ENXIO;
	    }

#ifdef __sparc__
	    if (!prom_palette)
		prom_palette = atyfb_palette;

	    /*
	     * Add /dev/fb mmap values.
	     */
	    info->mmap_map[0].voff = 0x8000000000000000UL;
	    info->mmap_map[0].poff = info->frame_buffer & PAGE_MASK;
	    info->mmap_map[0].size = info->total_vram;
	    info->mmap_map[0].prot_mask = _PAGE_CACHE;
	    info->mmap_map[0].prot_flag = _PAGE_E;
	    info->mmap_map[1].voff = info->mmap_map[0].voff + info->total_vram;
	    info->mmap_map[1].poff = info->ati_regbase & PAGE_MASK;
	    info->mmap_map[1].size = PAGE_SIZE;
	    info->mmap_map[1].prot_mask = _PAGE_CACHE;
	    info->mmap_map[1].prot_flag = _PAGE_E;
#endif /* __sparc__ */

#ifdef CONFIG_PMAC_PBOOK
	    if (first_display == NULL)
		    pmu_register_sleep_notifier(&aty_sleep_notifier);
	    info->next = first_display;
	    first_display = info;
#endif

#ifdef CONFIG_FB_COMPAT_XPMAC
	    if (!console_fb_info)
		console_fb_info = &info->fb_info;
#endif /* CONFIG_FB_COMPAT_XPMAC */
	}
    }

#elif defined(CONFIG_ATARI)
    u32 clock_r;
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
	    return -ENOMEM;
	}
	memset(info, 0, sizeof(struct fb_info_aty));

	/*
	 *  Map the video memory (physical address given) to somewhere in the
	 *  kernel address space.
	 */
	info->frame_buffer = ioremap(phys_vmembase[m64_num], phys_size[m64_num]);
	info->frame_buffer_phys = info->frame_buffer;  /* Fake! */
	info->ati_regbase = ioremap(phys_guiregbase[m64_num], 0x10000)+0xFC00ul;
	info->ati_regbase_phys = info->ati_regbase;  /* Fake! */

	aty_st_le32(CLOCK_CNTL, 0x12345678, info);
	clock_r = aty_ld_le32(CLOCK_CNTL, info);

	switch (clock_r & 0x003F) {
	    case 0x12:
		info->clk_wr_offset = 3;  /*  */
		break;
	    case 0x34:
		info->clk_wr_offset = 2;  /* Medusa ST-IO ISA Adapter etc. */
		break;
	    case 0x16:
		info->clk_wr_offset = 1;  /*  */
		break;
	    case 0x38:
		info->clk_wr_offset = 0;  /* Panther 1 ISA Adapter (Gerald) */
		break;
	}

	if (!aty_init(info, "ISA bus")) {
	    kfree(info);
	    /* This is insufficient! kernel_map has added two large chunks!! */
	    return -ENXIO;
	}
    }
#endif /* CONFIG_ATARI */
    return 0;
}

#ifndef MODULE
int __init atyfb_setup(char *options)
{
    char *this_opt;

    if (!options || !*options)
	return 0;

    while ((this_opt = strsep(&options, ",")) != NULL) {
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
#ifdef CONFIG_PPC
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
	else
	    mode_option = this_opt;
    }
    return 0;
}
#endif /* !MODULE */

#ifdef CONFIG_ATARI
static int __init store_video_par(char *video_str, unsigned char m64_num)
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

static char __init *strtoke(char *s, const char *ct)
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

#ifdef CONFIG_FB_ATY_CT
    /* Erase HW Cursor */
    if (info->cursor)
	atyfb_cursor(&fb_display[currcon], CM_ERASE,
		     info->cursor->pos.x, info->cursor->pos.y);
#endif /* CONFIG_FB_ATY_CT */

    currcon = con;

    atyfb_decode_var(&fb_display[con].var, &par, info);
    atyfb_set_par(&par, info);
    atyfb_set_dispsw(&fb_display[con], info, par.crtc.bpp,
		     par.accel_flags & FB_ACCELF_TEXT);

    /* Install new colormap */
    do_install_cmap(con, fb);

#ifdef CONFIG_FB_ATY_CT
    /* Install hw cursor */
    if (info->cursor) {
	aty_set_cursor_color(info);
	aty_set_cursor_shape(info);
    }
#endif /* CONFIG_FB_ATY_CT */
    return 1;
}

    /*
     *  Blank the display.
     */

static void atyfbcon_blank(int blank, struct fb_info *fb)
{
    struct fb_info_aty *info = (struct fb_info_aty *)fb;
    u8 gen_cntl;

#ifdef CONFIG_PMAC_BACKLIGHT
    if ((_machine == _MACH_Pmac) && blank)
    	set_backlight_enable(0);
#endif /* CONFIG_PMAC_BACKLIGHT */

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

#ifdef CONFIG_PMAC_BACKLIGHT
    if ((_machine == _MACH_Pmac) && !blank)
    	set_backlight_enable(1);
#endif /* CONFIG_PMAC_BACKLIGHT */
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
    if (M64_HAS(EXTRA_BRIGHT))
	i |= 0x2;	/*DAC_CNTL|0x2 turns off the extra brightness for gt*/
    aty_st_8(DAC_CNTL, i, info);
    aty_st_8(DAC_MASK, 0xff, info);
    scale = (M64_HAS(INTEGRATED) && info->current_par.crtc.bpp == 16) ? 3 : 0;
    writeb(regno << scale, &info->aty_cmap_regs->windex);
    writeb(red, &info->aty_cmap_regs->lut);
    writeb(green, &info->aty_cmap_regs->lut);
    writeb(blue, &info->aty_cmap_regs->lut);
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

#ifdef CONFIG_FB_ATY_CT
    if (info->cursor && (yoffset + yres <= sy))
	atyfb_cursor(p, CM_ERASE, info->cursor->pos.x, info->cursor->pos.y);
#endif /* CONFIG_FB_ATY_CT */

    info->current_par.crtc.yoffset = yoffset;
    set_off_pitch(&info->current_par, info);
    return 0;
}



#ifdef MODULE
int __init init_module(void)
{
    atyfb_init();
    return fb_list ? 0 : -ENXIO;
}

void cleanup_module(void)
{
    while (fb_list) {
	struct fb_info_aty *info = fb_list;
	fb_list = info->next;

	unregister_framebuffer(&info->fb_info);

#ifndef __sparc__
	if (info->ati_regbase)
	    iounmap((void *)info->ati_regbase);
	if (info->frame_buffer)
	    iounmap((void *)info->frame_buffer);
#ifdef __BIG_ENDIAN
	if (info->cursor && info->cursor->ram)
	    iounmap(info->cursor->ram);
#endif
#endif

	if (info->cursor) {
	    if (info->cursor->timer)
		kfree(info->cursor->timer);
	    kfree(info->cursor);
	}
#ifdef __sparc__
	if (info->mmap_map)
	    kfree(info->mmap_map);
#endif
	kfree(info);
    }
}

#endif
