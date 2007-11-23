/*
 *
 * Hardware accelerated Matrox Millenium I, II, Mystique and G200
 *
 * (c) 1998 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Version: 1.3 1998/10/28 
 *
 * MTRR stuff: 1998 Tom Rini <tmrini@ntplx.net>
 *
 * Contributors: "menion?" <menion@mindless.com>
 *                     Betatesting, fixes, ideas
 *
 *               "Kurt Garloff" <garloff@kg1.ping.de>
 *                     Betatesting, fixes, ideas, videomodes, videomodes timmings
 *
 *               "Tom Rini" <tmrini@ntplx.net>
 *                     MTRR stuff, betatesting, fixes, ideas
 *
 *               "Gerd Knorr" <kraxel@goldbach.isdn.cs.tu-berlin.de>
 *                     Betatesting
 *
 *               "Kelly French" <targon@hazmat.com>
 *                     Betatesting, bug reporting
 *
 *               "Pablo Bianucci" <pbian@pccp.com.ar>
 *                     Fixes, ideas, betatesting
 *
 *               "Inaky Perez Gonzalez" <inaky@peloncho.fis.ucm.es>
 *                     Fixes, enhandcements, ideas, betatesting
 *
 *               "Paul Womar" <Paul@pwomar.demon.co.uk>
 *               "Ryuichi Oikawa" <roikawa@rr.iiij4u.or.jp>
 *               "Owen Waller" <O.Waller@ee.qub.ac.uk>
 *                     PPC betatesting 
 *                     
 * (following author is not in any relation with this code, but his code
 *  is included in this driver)
 *
 * Based on framebuffer driver for VBE 2.0 compliant graphic boards
 *     (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 * (following author is not in any relation with this code, but his ideas
 *  were used when writting this driver)
 *
 *		 FreeVBE/AF (Matrox), "Shawn Hargreaves" <shawn@talula.demon.co.uk>
 *
 */ 
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/io.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include <video/fbcon.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#ifndef __i386__
#ifndef ioremap_nocache
#define ioremap_nocache(X,Y) ioremap(X,Y)
#endif 
#endif

#ifdef DEBUG
#define dprintk(X...)	printk(X)
#else
#define dprintk(X...)
#endif

#ifndef PCI_SS_VENDOR_ID_SIEMENS_NIXDORF
#define PCI_SS_VENDOR_ID_SIEMENS_NIXDORF	0x110A
#endif
#ifndef PCI_SS_VENDOR_ID_MATROX
#define PCI_SS_VENDOR_ID_MATROX		PCI_VENDOR_ID_MATROX
#endif
#ifndef PCI_DEVICE_ID_MATROX_G200_AGP
#define PCI_DEVICE_ID_MATROX_G200_AGP	0x0521
#endif
#ifndef PCI_DEVICE_ID_MATROX_G100
#define PCI_DEVICE_ID_MATROX_G100	0x1000
#endif
#ifndef PCI_DEVICE_ID_MATROX_G100_AGP
#define PCI_DEVICE_ID_MATROX_G100_AGP	0x1001
#endif

#ifndef PCI_SS_ID_MATROX_PRODUCTIVA_G100_AGP
#define PCI_SS_ID_MATROX_UNSPECIFIED		0xFF00
#define PCI_SS_ID_MATROX_PRODUCTIVA_G100_AGP	0xFF01
#define PCI_SS_ID_MATROX_MYSTIQUE_G200_AGP	0xFF02
#define PCI_SS_ID_MATROX_MILLENIUM_G200_AGP	0xFF03
#define PCI_SS_ID_MATROX_MARVEL_G200_AGP	0xFF04
#define PCI_SS_ID_MATROX_MGA_G100_PCI		0xFF05
#define PCI_SS_ID_MATROX_MGA_G100_AGP		0x1001
#define PCI_SS_ID_SIEMENS_MGA_G100_AGP		0x001E /* 30 */
#define PCI_SS_ID_SIEMENS_MGA_G200_AGP		0x0032 /* 50 */
#endif

#define MX_VISUAL_TRUECOLOR	FB_VISUAL_DIRECTCOLOR
#define MX_VISUAL_DIRECTCOLOR	FB_VISUAL_TRUECOLOR
#define MX_VISUAL_PSEUDOCOLOR	FB_VISUAL_PSEUDOCOLOR

#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)

/* G100, G200 and Mystique have (almost) same DAC */
#undef NEED_DAC1064
#if defined(CONFIG_FB_MATROX_MYSTIQUE) || defined(CONFIG_FB_MATROX_G100)
#define NEED_DAC1064 1
#endif

struct matroxfb_par
{
	struct fb_var_screeninfo var;

	unsigned int	final_bppShift;
	int		visual;		/* unfortunately, fix */
	int		video_type;
	int		cmap_len;
};

struct my_timming {
	unsigned int pixclock;
	unsigned int HDisplay;
	unsigned int HSyncStart;
	unsigned int HSyncEnd;
	unsigned int HTotal;
	unsigned int VDisplay;
	unsigned int VSyncStart;
	unsigned int VSyncEnd;
	unsigned int VTotal;
	unsigned int sync;
	int	     dblscan;
	int	     interlaced;
};

struct matrox_fb_info;

#define MATROX_2MB_WITH_4MB_ADDON

struct matrox_pll_features {
	unsigned int	vco_freq_min;	
	unsigned int	ref_freq;
	unsigned int	feed_div_min;
	unsigned int	feed_div_max;
	unsigned int	in_div_min;
	unsigned int	in_div_max;
	unsigned int	post_shift_max;
};

struct matrox_DAC1064_features {
	u_int8_t	xvrefctrl;
};

struct matrox_accel_features {
	int		has_cacheflush;
};

/* current hardware status */
struct matrox_hw_state {
	u_int32_t	MXoptionReg;
	unsigned char	DACclk[6];
	unsigned char	DACreg[64];
	unsigned char	MiscOutReg;
	unsigned char	DACpal[768];
	unsigned char	CRTC[25];
	unsigned char	CRTCEXT[6];
	unsigned char	SEQ[5];
	/* unused for MGA mode, but who knows... */
	unsigned char	GCTL[9];
	/* unused for MGA mode, but who knows... */
	unsigned char	ATTR[21];
};

struct matrox_accel_data {
#ifdef CONFIG_FB_MATROX_MILLENIUM
	unsigned char	ramdac_rev;
#endif
	u_int32_t	m_dwg_rect;
};

#ifdef CONFIG_FB_MATROX_MULTIHEAD
#define ACCESS_FBINFO(x) (minfo->x)
#define ACCESS_FBINFO2(info, x) (((struct matrox_fb_info*)info)->x)

#define MINFO minfo

#define WPMINFO struct matrox_fb_info* minfo,
#define CPMINFO	const struct matrox_fb_info* minfo,
#define PMINFO  minfo,

static struct matrox_fb_info* mxinfo(struct display* p) {
	return (struct matrox_fb_info*)p->fb_info;
}

#define PMXINFO(p) mxinfo(p),
#define MINFO_FROM_DISP(x) struct matrox_fb_info* minfo = mxinfo(x)

#else

struct matrox_fb_info global_mxinfo;
struct display global_disp;

#define ACCESS_FBINFO(x) (global_mxinfo.x)
#define ACCESS_FBINFO2(info, x) (global_mxinfo.x)

#define MINFO (&global_mxinfo)

#define WPMINFO
#define CPMINFO
#define PMINFO

#if 0
static struct matrox_fb_info* mxinfo(struct display* p) {
	return &global_mxinfo;
}
#endif

#define PMXINFO(p)
#define MINFO_FROM_DISP(x)

#endif

struct matrox_switch {
	int	(*preinit)(WPMINFO struct matrox_hw_state*);
	void	(*reset)(WPMINFO struct matrox_hw_state*);
	int	(*init)(CPMINFO struct matrox_hw_state*, struct my_timming*);
	void	(*restore)(WPMINFO struct matrox_hw_state*, struct matrox_hw_state*, struct display*);
};

struct matrox_fb_info {
	/* fb_info must be first */
	struct fb_info		fbcon;

	struct matrox_fb_info*	next_fb;

	struct matroxfb_par	curr;
	struct matrox_hw_state	hw1;
	struct matrox_hw_state	hw2;
	struct matrox_hw_state*	newhw;
	struct matrox_hw_state*	currenthw;

	struct matrox_accel_data accel;

#ifdef MATROX_2MB_WITH_4MB_ADDON
	unsigned int	_mga_ydstorg;
	unsigned int	_curr_ydstorg;
#endif

	struct pci_dev*		pcidev;

	unsigned long	video_base;	/* physical */
	char*		video_vbase;	/* CPU view */
	unsigned int	video_len;

	unsigned long	mmio_base;	/* physical */
	unsigned char*	mmio;		/* CPU view */
	unsigned int	mmio_len;

	unsigned int	max_pixel_clock;

	struct matrox_switch*	hw_switch;
	int		currcon;

	struct {
		struct matrox_pll_features pll;
		struct matrox_DAC1064_features DAC1064;
		struct matrox_accel_features accel;
		} features;

	int			interleave;
	int			millenium;
	int			milleniumII;
	struct {
		int		cfb4;
		const int*	vxres;
			      } capable;
#ifdef CONFIG_MTRR
	struct {
		int		vram;
		int		vram_valid;
			      } mtrr;
#endif
	struct {
		int		precise_width;
		int		mga_24bpp_fix;
		int		novga;
		int		nobios;
		int		nopciretry;
		int		inverse;
		int		hwcursor;

		int		accelerator;
		int		video64bits;
			      } devflags;
	struct display_switch	dispsw;
#ifdef CONFIG_FB_MATROX_MILLENIUM
	struct {
		unsigned int x;
		unsigned int y;
		unsigned int w;
		unsigned int h;
		volatile unsigned int mode;
			      } cursor;
#endif
#if defined(FBCON_HAS_CFB16) || defined(FBCON_HAS_CFB24) || defined(FBCON_HAS_CFB32)
	union {
#ifdef FBCON_HAS_CFB16
		u_int16_t	cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
		u_int32_t	cfb24[16];
#endif
#ifdef FBCON_HAS_CFB32
		u_int32_t	cfb32[16];
#endif
	} cmap;
#endif
	struct { unsigned red, green, blue, transp; } palette[256];
};

#ifdef MATROX_2MB_WITH_4MB_ADDON
#define mga_ydstorg(x)	ACCESS_FBINFO2(x, _mga_ydstorg)
#define curr_ydstorg(x)	ACCESS_FBINFO2(x, _curr_ydstorg)
#else
#define mga_ydstorg(x) (0)
#define curr_ydstorg(x) (0)
#endif

#define PCI_OPTION_REG	0x40
#define PCI_MGA_INDEX	0x44
#define PCI_MGA_DATA	0x48

#define M_DWGCTL	0x1C00
#define M_MACCESS	0x1C04
#define M_CTLWTST	0x1C08

#define M_PLNWT		0x1C1C

#define M_BCOL		0x1C20
#define M_FCOL		0x1C24

#define M_SGN		0x1C58
#define M_LEN		0x1C5C
#define M_AR0		0x1C60
#define M_AR1		0x1C64
#define M_AR2		0x1C68
#define M_AR3		0x1C6C
#define M_AR4		0x1C70
#define M_AR5		0x1C74
#define M_AR6		0x1C78

#define M_CXBNDRY	0x1C80
#define M_FXBNDRY	0x1C84
#define M_YDSTLEN	0x1C88
#define M_PITCH		0x1C8C
#define M_YDST		0x1C90
#define M_YDSTORG	0x1C94
#define M_YTOP		0x1C98
#define M_YBOT		0x1C9C

/* mystique only */
#define M_CACHEFLUSH	0x1FFF

#define M_EXEC		0x0100

#define M_DWG_TRAP	0x04
#define M_DWG_BITBLT	0x08
#define M_DWG_ILOAD	0x09

#define M_DWG_LINEAR	0x0080
#define M_DWG_SOLID	0x0800
#define M_DWG_ARZERO	0x1000
#define M_DWG_SGNZERO	0x2000
#define M_DWG_SHIFTZERO	0x4000

#define M_DWG_REPLACE	0x000C0000
#define M_DWG_REPLACE2	(M_DWG_REPLACE | 0x40)
#define M_DWG_XOR	0x00060010

#define M_DWG_BFCOL	0x04000000
#define M_DWG_BMONOWF	0x08000000

#define M_DWG_TRANSC	0x40000000

#define M_FIFOSTATUS	0x1E10
#define M_STATUS	0x1E14

#define M_IEN		0x1E1C

#define M_VCOUNT	0x1E20

#define M_RESET		0x1E40

#define M_OPMODE	0x1E54
#define     M_OPMODE_DMA_GEN_WRITE	0x00
#define     M_OPMODE_DMA_BLIT		0x04
#define     M_OPMODE_DMA_VECTOR_WRITE	0x08
#define     M_OPMODE_DMA_LE		0x0000		/* little endian - no transformation */
#define     M_OPMODE_DMA_BE_8BPP	0x0000
#define     M_OPMODE_DMA_BE_16BPP	0x0100
#define     M_OPMODE_DMA_BE_32BPP	0x0200
#define     M_OPMODE_DIR_LE		0x000000	/* little endian - no transformation */
#define     M_OPMODE_DIR_BE_8BPP	0x000000
#define     M_OPMODE_DIR_BE_16BPP	0x010000
#define     M_OPMODE_DIR_BE_32BPP	0x020000

#define M_ATTR_INDEX	0x1FC0
#define M_ATTR_DATA	0x1FC1

#define M_MISC_REG	0x1FC2
#define M_3C2_RD	0x1FC2

#define M_SEQ_INDEX	0x1FC4
#define M_SEQ_DATA	0x1FC5

#define M_MISC_REG_READ	0x1FCC

#define M_GRAPHICS_INDEX 0x1FCE
#define M_GRAPHICS_DATA	0x1FCF

#define M_CRTC_INDEX	0x1FD4

#define M_ATTR_RESET	0x1FDA
#define M_3DA_WR	0x1FDA

#define M_EXTVGA_INDEX	0x1FDE
#define M_EXTVGA_DATA	0x1FDF

#define M_RAMDAC_BASE	0x3C00

/* fortunately, same on TVP3026 and MGA1064 */
#define M_DAC_REG	(M_RAMDAC_BASE+0)
#define M_DAC_VAL	(M_RAMDAC_BASE+1)
#define M_PALETTE_MASK	(M_RAMDAC_BASE+2)

#ifdef CONFIG_FB_MATROX_MILLENIUM
#define TVP3026_INDEX		0x00
#define TVP3026_PALWRADD	0x00
#define TVP3026_PALDATA		0x01
#define TVP3026_PIXRDMSK	0x02
#define TVP3026_PALRDADD	0x03
#define TVP3026_CURCOLWRADD	0x04
#define     TVP3026_CLOVERSCAN		0x00
#define     TVP3026_CLCOLOR0		0x01
#define     TVP3026_CLCOLOR1		0x02
#define     TVP3026_CLCOLOR2		0x03
#define TVP3026_CURCOLDATA	0x05
#define TVP3026_CURCOLRDADD	0x07
#define TVP3026_CURCTRL		0x09
#define TVP3026_X_DATAREG	0x0A
#define TVP3026_CURRAMDATA	0x0B
#define TVP3026_CURPOSXL	0x0C
#define TVP3026_CURPOSXH	0x0D
#define TVP3026_CURPOSYL	0x0E
#define TVP3026_CURPOSYH	0x0F

#define TVP3026_XSILICONREV	0x01
#define TVP3026_XCURCTRL	0x06
#define     TVP3026_XCURCTRL_DIS	0x00	/* transparent, transparent, transparent, transparent */
#define     TVP3026_XCURCTRL_3COLOR	0x01	/* transparent, 0, 1, 2 */
#define     TVP3026_XCURCTRL_XGA	0x02	/* 0, 1, transparent, complement */
#define     TVP3026_XCURCTRL_XWIN	0x03	/* transparent, transparent, 0, 1 */
#define     TVP3026_XCURCTRL_BLANK2048	0x00
#define     TVP3026_XCURCTRL_BLANK4096	0x10
#define     TVP3026_XCURCTRL_INTERLACED	0x20
#define     TVP3026_XCURCTRL_ODD	0x00 /* ext.signal ODD/\EVEN */
#define     TVP3026_XCURCTRL_EVEN	0x40 /* ext.signal EVEN/\ODD */
#define     TVP3026_XCURCTRL_INDIRECT	0x00
#define     TVP3026_XCURCTRL_DIRECT	0x80
#define TVP3026_XLATCHCTRL	0x0F
#define     TVP3026_XLATCHCTRL_1_1	0x06
#define     TVP3026_XLATCHCTRL_2_1	0x07
#define     TVP3026_XLATCHCTRL_4_1	0x06
#define     TVP3026_XLATCHCTRL_8_1	0x06
#define     TVP3026_XLATCHCTRL_16_1	0x06
#define     TVP3026A_XLATCHCTRL_4_3	0x07
#define     TVP3026B_XLATCHCTRL_4_3	0x08
#define     TVP3026B_XLATCHCTRL_8_3	0x06	/* ??? do not understand... but it works... ??? */
#define TVP3026_XTRUECOLORCTRL	0x18
#define     TVP3026_XTRUECOLORCTRL_VRAM_SHIFT_ACCEL	0x00
#define     TVP3026_XTRUECOLORCTRL_VRAM_SHIFT_TVP	0x20
#define     TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR		0x80
#define     TVP3026_XTRUECOLORCTRL_TRUECOLOR		0x40 /* paletized */
#define     TVP3026_XTRUECOLORCTRL_DIRECTCOLOR		0x00
#define     TVP3026_XTRUECOLORCTRL_24_ALTERNATE		0x08 /* 5:4/5:2 instead of 4:3/8:3 */
#define     TVP3026_XTRUECOLORCTRL_RGB_888		0x16 /* 4:3/8:3 (or 5:4/5:2) */
#define	    TVP3026_XTRUECOLORCTRL_BGR_888		0x17
#define     TVP3026_XTRUECOLORCTRL_ORGB_8888		0x06
#define     TVP3026_XTRUECOLORCTRL_BGRO_8888		0x07
#define     TVP3026_XTRUECOLORCTRL_RGB_565		0x05
#define     TVP3026_XTRUECOLORCTRL_ORGB_1555		0x04
#define     TVP3026_XTRUECOLORCTRL_RGB_664		0x03
#define     TVP3026_XTRUECOLORCTRL_RGBO_4444		0x01
#define TVP3026_XMUXCTRL	0x19
#define     TVP3026_XMUXCTRL_MEMORY_8BIT			0x01 /* - */
#define     TVP3026_XMUXCTRL_MEMORY_16BIT			0x02 /* - */
#define     TVP3026_XMUXCTRL_MEMORY_32BIT			0x03 /* 2MB RAM, 512K * 4 */
#define     TVP3026_XMUXCTRL_MEMORY_64BIT			0x04 /* >2MB RAM, 512K * 8 & more */
#define     TVP3026_XMUXCTRL_PIXEL_4BIT				0x40 /* L0,H0,L1,H1... */
#define     TVP3026_XMUXCTRL_PIXEL_4BIT_SWAPPED			0x60 /* H0,L0,H1,L1... */
#define     TVP3026_XMUXCTRL_PIXEL_8BIT				0x48
#define     TVP3026_XMUXCTRL_PIXEL_16BIT			0x50
#define     TVP3026_XMUXCTRL_PIXEL_32BIT			0x58
#define     TVP3026_XMUXCTRL_VGA				0x98 /* VGA MEMORY, 8BIT PIXEL */
#define TVP3026_XCLKCTRL	0x1A
#define     TVP3026_XCLKCTRL_DIV1	0x00
#define     TVP3026_XCLKCTRL_DIV2	0x10
#define     TVP3026_XCLKCTRL_DIV4	0x20
#define     TVP3026_XCLKCTRL_DIV8	0x30
#define     TVP3026_XCLKCTRL_DIV16	0x40
#define     TVP3026_XCLKCTRL_DIV32	0x50
#define     TVP3026_XCLKCTRL_DIV64	0x60
#define     TVP3026_XCLKCTRL_CLKSTOPPED	0x70
#define     TVP3026_XCLKCTRL_SRC_CLK0	0x00
#define     TVP3026_XCLKCTRL_SRC_CLK1   0x01
#define     TVP3026_XCLKCTRL_SRC_CLK2	0x02	/* CLK2 is TTL source*/
#define     TVP3026_XCLKCTRL_SRC_NCLK2	0x03	/* not CLK2 is TTL source */
#define     TVP3026_XCLKCTRL_SRC_ECLK2	0x04	/* CLK2 and not CLK2 is ECL source */
#define     TVP3026_XCLKCTRL_SRC_PLL	0x05
#define     TVP3026_XCLKCTRL_SRC_DIS	0x06	/* disable & poweroff internal clock */
#define     TVP3026_XCLKCTRL_SRC_CLK0VGA 0x07
#define TVP3026_XPALETTEPAGE	0x1C
#define TVP3026_XGENCTRL	0x1D
#define     TVP3026_XGENCTRL_HSYNC_POS	0x00
#define     TVP3026_XGENCTRL_HSYNC_NEG	0x01
#define     TVP3026_XGENCTRL_VSYNC_POS	0x00
#define     TVP3026_XGENCTRL_VSYNC_NEG	0x02
#define     TVP3026_XGENCTRL_LITTLE_ENDIAN 0x00
#define     TVP3026_XGENCTRL_BIG_ENDIAN    0x08
#define     TVP3026_XGENCTRL_BLACK_0IRE		0x00
#define     TVP3026_XGENCTRL_BLACK_75IRE	0x10
#define     TVP3026_XGENCTRL_NO_SYNC_ON_GREEN	0x00
#define     TVP3026_XGENCTRL_SYNC_ON_GREEN	0x20
#define     TVP3026_XGENCTRL_OVERSCAN_DIS	0x00
#define     TVP3026_XGENCTRL_OVERSCAN_EN	0x40
#define TVP3026_XMISCCTRL	0x1E
#define     TVP3026_XMISCCTRL_DAC_PUP	0x00
#define     TVP3026_XMISCCTRL_DAC_PDOWN	0x01
#define     TVP3026_XMISCCTRL_DAC_EXT	0x00 /* or 8, bit 3 is ignored */
#define     TVP3026_XMISCCTRL_DAC_6BIT	0x04
#define     TVP3026_XMISCCTRL_DAC_8BIT	0x0C
#define     TVP3026_XMISCCTRL_PSEL_DIS	0x00
#define     TVP3026_XMISCCTRL_PSEL_EN	0x10
#define     TVP3026_XMISCCTRL_PSEL_LOW	0x00 /* PSEL high selects directcolor */
#define     TVP3026_XMISCCTRL_PSEL_HIGH 0x20 /* PSEL high selects truecolor or pseudocolor */
#define TVP3026_XGENIOCTRL	0x2A
#define TVP3026_XGENIODATA	0x2B
#define TVP3026_XPLLADDR	0x2C
#define     TVP3026_XPLLADDR_X(LOOP,MCLK,PIX) (((LOOP)<<4) | ((MCLK)<<2) | (PIX))
#define     TVP3026_XPLLDATA_N		0x00
#define     TVP3026_XPLLDATA_M		0x01
#define     TVP3026_XPLLDATA_P		0x02
#define     TVP3026_XPLLDATA_STAT	0x03
#define TVP3026_XPIXPLLDATA	0x2D
#define TVP3026_XMEMPLLDATA	0x2E
#define TVP3026_XLOOPPLLDATA	0x2F
#define TVP3026_XCOLKEYOVRMIN	0x30
#define TVP3026_XCOLKEYOVRMAX	0x31
#define TVP3026_XCOLKEYREDMIN	0x32
#define TVP3026_XCOLKEYREDMAX	0x33
#define TVP3026_XCOLKEYGREENMIN	0x34
#define TVP3026_XCOLKEYGREENMAX	0x35
#define TVP3026_XCOLKEYBLUEMIN	0x36
#define TVP3026_XCOLKEYBLUEMAX	0x37
#define TVP3026_XCOLKEYCTRL	0x38
#define     TVP3026_XCOLKEYCTRL_OVR_EN	0x01
#define     TVP3026_XCOLKEYCTRL_RED_EN	0x02
#define     TVP3026_XCOLKEYCTRL_GREEN_EN 0x04
#define     TVP3026_XCOLKEYCTRL_BLUE_EN	0x08
#define     TVP3026_XCOLKEYCTRL_NEGATE	0x10
#define     TVP3026_XCOLKEYCTRL_ZOOM1	0x00
#define     TVP3026_XCOLKEYCTRL_ZOOM2	0x20
#define     TVP3026_XCOLKEYCTRL_ZOOM4	0x40
#define     TVP3026_XCOLKEYCTRL_ZOOM8	0x60
#define     TVP3026_XCOLKEYCTRL_ZOOM16	0x80
#define     TVP3026_XCOLKEYCTRL_ZOOM32	0xA0
#define TVP3026_XMEMPLLCTRL	0x39
#define     TVP3026_XMEMPLLCTRL_DIV(X)	(((X)-1)>>1)	/* 2,4,6,8,10,12,14,16, division applied to LOOP PLL after divide by 2^P */
#define     TVP3026_XMEMPLLCTRL_STROBEMKC4	0x08
#define     TVP3026_XMEMPLLCTRL_MCLK_DOTCLOCK	0x00	/* MKC4 */
#define     TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL	0x10	/* MKC4 */
#define     TVP3026_XMEMPLLCTRL_RCLK_PIXPLL	0x00
#define     TVP3026_XMEMPLLCTRL_RCLK_LOOPPLL	0x20
#define     TVP3026_XMEMPLLCTRL_RCLK_DOTDIVN	0x40	/* dot clock divided by loop pclk N prescaler */
#define TVP3026_XSENSETEST	0x3A
#define TVP3026_XTESTMODEDATA	0x3B
#define TVP3026_XCRCREML	0x3C
#define TVP3026_XCRCREMH	0x3D
#define TVP3026_XCRCBITSEL	0x3E
#define TVP3026_XID		0x3F

#endif

#ifdef NEED_DAC1064

#define DAC1064_OPT_SCLK_PCI	0x00
#define DAC1064_OPT_SCLK_PLL	0x01
#define DAC1064_OPT_SCLK_EXT	0x02
#define DAC1064_OPT_SCLK_MASK	0x03
#define DAC1064_OPT_GDIV1	0x04	/* maybe it is GDIV2 on G100 ?! */
#define DAC1064_OPT_GDIV3	0x00
#define DAC1064_OPT_MDIV1	0x08
#define DAC1064_OPT_MDIV2	0x00
#define DAC1064_OPT_RESERVED	0x10

#define M1064_INDEX	0x00
#define M1064_PALWRADD	0x00
#define M1064_PALDATA	0x01
#define M1064_PIXRDMSK	0x02
#define M1064_PALRDADD	0x03
#define M1064_X_DATAREG	0x0A
#define M1064_CURPOSXL	0x0C	/* can be accessed as DWORD */
#define M1064_CURPOSXH	0x0D
#define M1064_CURPOSYL	0x0E
#define M1064_CURPOSYH	0x0F

#define M1064_XCURADDL		0x04
#define M1064_XCURADDH		0x05
#define M1064_XCURCTRL		0x06
#define     M1064_XCURCTRL_DIS		0x00	/* transparent, transparent, transparent, transparent */
#define     M1064_XCURCTRL_3COLOR	0x01	/* transparent, 0, 1, 2 */
#define     M1064_XCURCTRL_XGA		0x02	/* 0, 1, transparent, complement */
#define     M1064_XCURCTRL_XWIN		0x03	/* transparent, transparent, 0, 1 */
#define M1064_XCURCOL0RED	0x08
#define M1064_XCURCOL0GREEN	0x09
#define M1064_XCURCOL0BLUE	0x0A
#define M1064_XCURCOL1RED	0x0C
#define M1064_XCURCOL1GREEN	0x0D
#define M1064_XCURCOL1BLUE	0x0E
#define M1064_XCURCOL2RED	0x10
#define M1064_XCURCOL2GREEN	0x11
#define M1064_XCURCOL2BLUE	0x12
#define DAC1064_XVREFCTRL	0x18
#define      DAC1064_XVREFCTRL_INTERNAL		0x3F
#define      DAC1064_XVREFCTRL_EXTERNAL		0x00
#define      DAC1064_XVREFCTRL_G100_DEFAULT	0x03
#define M1064_XMULCTRL		0x19
#define      M1064_XMULCTRL_DEPTH_8BPP		0x00	/* 8 bpp paletized */
#define      M1064_XMULCTRL_DEPTH_15BPP_1BPP	0x01	/* 15 bpp paletized + 1 bpp overlay */
#define      M1064_XMULCTRL_DEPTH_16BPP		0x02	/* 16 bpp paletized */
#define      M1064_XMULCTRL_DEPTH_24BPP		0x03	/* 24 bpp paletized */
#define      M1064_XMULCTRL_DEPTH_24BPP_8BPP	0x04	/* 24 bpp direct + 8 bpp overlay paletized */
#define      M1064_XMULCTRL_2G8V16		0x05	/* 15 bpp video direct, half xres, 8bpp paletized */
#define      M1064_XMULCTRL_G16V16		0x06	/* 15 bpp video, 15bpp graphics, one of them paletized */
#define      M1064_XMULCTRL_DEPTH_32BPP		0x07	/* 24 bpp paletized + 8 bpp unused */
#define      M1064_XMULCTRL_GRAPHICS_PALETIZED	0x00
#define      M1064_XMULCTRL_VIDEO_PALETIZED	0x08
#define M1064_XPIXCLKCTRL	0x1A
#define      M1064_XPIXCLKCTRL_SRC_PCI		0x00
#define      M1064_XPIXCLKCTRL_SRC_PLL		0x01
#define      M1064_XPIXCLKCTRL_SRC_EXT		0x02
#define      M1064_XPIXCLKCTRL_SRC_MASK		0x03
#define      M1064_XPIXCLKCTRL_EN		0x00
#define      M1064_XPIXCLKCTRL_DIS		0x04
#define      M1064_XPIXCLKCTRL_PLL_DOWN		0x00
#define      M1064_XPIXCLKCTRL_PLL_UP		0x08
#define M1064_XGENCTRL		0x1D
#define      M1064_XGENCTRL_VS_0		0x00
#define      M1064_XGENCTRL_VS_1		0x01
#define      M1064_XGENCTRL_ALPHA_DIS		0x00
#define      M1064_XGENCTRL_ALPHA_EN		0x02
#define      M1064_XGENCTRL_BLACK_0IRE		0x00
#define      M1064_XGENCTRL_BLACK_75IRE		0x10
#define      M1064_XGENCTRL_SYNC_ON_GREEN	0x00
#define      M1064_XGENCTRL_NO_SYNC_ON_GREEN	0x20
#define      M1064_XGENCTRL_SYNC_ON_GREEN_MASK	0x20
#define M1064_XMISCCTRL		0x1E
#define      M1064_XMISCCTRL_DAC_DIS		0x00
#define      M1064_XMISCCTRL_DAC_EN		0x01
#define      M1064_XMISCCTRL_MFC_VGA		0x00
#define      M1064_XMISCCTRL_MFC_MAFC		0x02
#define      M1064_XMISCCTRL_MFC_DIS		0x06
#define      M1064_XMISCCTRL_DAC_6BIT		0x00
#define      M1064_XMISCCTRL_DAC_8BIT		0x08
#define      M1064_XMISCCTRL_LUT_DIS		0x00
#define      M1064_XMISCCTRL_LUT_EN		0x10
#define M1064_XGENIOCTRL	0x2A
#define M1064_XGENIODATA	0x2B
#define DAC1064_XSYSPLLM	0x2C
#define DAC1064_XSYSPLLN	0x2D
#define DAC1064_XSYSPLLP	0x2E
#define DAC1064_XSYSPLLSTAT	0x2F
#define M1064_XZOOMCTRL		0x38
#define      M1064_XZOOMCTRL_1			0x00
#define      M1064_XZOOMCTRL_2			0x01
#define      M1064_XZOOMCTRL_4			0x03
#define M1064_XSENSETEST	0x3A
#define      M1064_XSENSETEST_BCOMP		0x01
#define      M1064_XSENSETEST_GCOMP		0x02
#define      M1064_XSENSETEST_RCOMP		0x04
#define      M1064_XSENSETEST_PDOWN		0x00
#define      M1064_XSENSETEST_PUP		0x80
#define M1064_XCRCREML		0x3C
#define M1064_XCRCREMH		0x3D
#define M1064_XCRCBITSEL	0x3E
#define M1064_XCOLKEYMASKL	0x40
#define M1064_XCOLKEYMASKH	0x41
#define M1064_XCOLKEYL		0x42
#define M1064_XCOLKEYH		0x43
#define M1064_XPIXPLLAM		0x44
#define M1064_XPIXPLLAN		0x45
#define M1064_XPIXPLLAP		0x46
#define M1064_XPIXPLLBM		0x48
#define M1064_XPIXPLLBN		0x49
#define M1064_XPIXPLLBP		0x4A
#define M1064_XPIXPLLCM		0x4C
#define M1064_XPIXPLLCN		0x4D
#define M1064_XPIXPLLCP		0x4E
#define M1064_XPIXPLLSTAT	0x4F

#endif

#ifdef __LITTLE_ENDIAN
#define MX_OPTION_BSWAP		0x00000000

#define M_OPMODE_4BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_8BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_16BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_24BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_32BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#else
#ifdef __BIG_ENDIAN
#define MX_OPTION_BSWAP		0x80000000

#define M_OPMODE_4BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)	/* TODO */
#define M_OPMODE_8BPP	(M_OPMODE_DMA_BE_8BPP  | M_OPMODE_DIR_BE_8BPP  | M_OPMODE_DMA_BLIT)
#define M_OPMODE_16BPP	(M_OPMODE_DMA_BE_16BPP | M_OPMODE_DIR_BE_16BPP | M_OPMODE_DMA_BLIT)
#define M_OPMODE_24BPP	(M_OPMODE_DMA_BE_8BPP | M_OPMODE_DIR_BE_8BPP | M_OPMODE_DMA_BLIT)	/* TODO, ?32 */
#define M_OPMODE_32BPP	(M_OPMODE_DMA_BE_32BPP | M_OPMODE_DIR_BE_32BPP | M_OPMODE_DMA_BLIT)
#else
#error "Byte ordering have to be defined. Cannot continue."
#endif
#endif

#define mga_inb(addr)	(*(volatile u_int8_t*)(ACCESS_FBINFO(mmio)+(addr)))
/* #define mga_inw(addr)	*(volatile u_int16_t*)(ACCESS_FBINFO(mmio)+(addr)) */
#define mga_inl(addr)	(*(volatile u_int32_t*)(ACCESS_FBINFO(mmio)+(addr)))
#define mga_outb(addr,val) *(volatile u_int8_t*)(ACCESS_FBINFO(mmio)+(addr))=(val)
/* #define mga_outw(addr,val) *(volatile u_int16_t*)(ACCESS_FBINFO(mmio)+(addr))=(val) */
#define mga_outl(addr,val) *(volatile u_int32_t*)(ACCESS_FBINFO(mmio)+(addr))=(val)
#define mga_readr(port,idx) (mga_outb((port),(idx)), mga_inb((port)+1))
#ifdef __LITTLE_ENDIAN
#define __mga_outw(addr,val) *(volatile u_int16_t*)(ACCESS_FBINFO(mmio)+(addr))=(val)
#define mga_setr(addr,port,val) __mga_outw(addr, ((val)<<8) | (port))
#else
#define mga_setr(addr,port,val) do { mga_outb(addr, port); mga_outb((addr)+1, val); } while (0)
#endif

#ifdef __LITTLE_ENDIAN
#define mga_fifo(n)	do {} while (mga_inb(M_FIFOSTATUS) < (n))
#else
#define mga_fifo(n)	do {} while ((mga_inl(M_FIFOSTATUS) & 0xFF) < (n))
#endif

static void WaitTillIdle(const struct matrox_fb_info* minfo) {
	do {} while (mga_inl(M_STATUS) & 0x10000);
}

/* code speedup */
#ifdef CONFIG_FB_MATROX_MILLENIUM
#define isInterleave(x)	 (x->interleave)
#define isMillenium(x)	 (x->millenium)
#define isMilleniumII(x) (x->milleniumII)
#else
#define isInterleave(x)  (0)
#define isMillenium(x)	 (0)
#define isMilleniumII(x) (0)
#endif

static void matrox_cfbX_init(struct matrox_fb_info* minfo) {
	u_int32_t maccess;
	u_int32_t mpitch;
	u_int32_t mopmode;

#ifdef MATROX_2MB_WITH_4MB_ADDON
	curr_ydstorg(MINFO) = mga_ydstorg(MINFO);
#endif
	mpitch = ACCESS_FBINFO(curr.var.xres_virtual);
	switch (ACCESS_FBINFO(curr.var.bits_per_pixel)) {
		case 4:		maccess = 0x00000000;	/* accelerate as 8bpp video */
				mpitch = (mpitch >> 1) | 0x8000; /* disable linearization */
				mopmode = M_OPMODE_4BPP;
				break;
    		case 8:		maccess = 0x00000000; 
				mopmode = M_OPMODE_8BPP;
				break;
    		case 16:	if (ACCESS_FBINFO(curr.var.green.length) == 5)
					maccess = 0xC0000001; 
				else
    				    	maccess = 0x40000001; 
#ifdef MATROX_2MB_WITH_4MB_ADDON
				curr_ydstorg(MINFO) >>= 1;
#endif
				mopmode = M_OPMODE_16BPP;
				break;
		case 24:	maccess = 0x00000003;
#ifdef MATROX_2MB_WITH_4MB_ADDON
				curr_ydstorg(MINFO) /= 3;
#endif
				mopmode = M_OPMODE_24BPP;
				break;
		case 32:	maccess = 0x00000002; 
#ifdef MATROX_2MB_WITH_4MB_ADDON
				curr_ydstorg(MINFO) >>= 2;
#endif
				mopmode = M_OPMODE_32BPP;
				break;
		default:	maccess = 0x00000000; 
				mopmode = 0x00000000;
				break;	/* turn off acceleration!!! */
	}
	mga_fifo(8);
	mga_outl(M_PITCH, mpitch);
	mga_outl(M_YDSTORG, curr_ydstorg(MINFO));
	mga_outl(M_PLNWT, -1);
	mga_outl(M_OPMODE, mopmode);
	mga_outl(M_CXBNDRY, 0xFFFF0000);
	mga_outl(M_YTOP, 0);
	mga_outl(M_YBOT, 0x007FFFFF);
	mga_outl(M_MACCESS, maccess);
	ACCESS_FBINFO(accel.m_dwg_rect) = M_DWG_TRAP | M_DWG_SOLID | M_DWG_ARZERO | M_DWG_SGNZERO | M_DWG_SHIFTZERO;
	if (isMilleniumII(MINFO)) ACCESS_FBINFO(accel.m_dwg_rect) |= M_DWG_TRANSC;
}

static void matrox_cfbX_bmove(struct display* p, int sy, int sx, int dy, int dx, int height, int width) {
	int pixx = p->var.xres_virtual, start, end;
	MINFO_FROM_DISP(p);

	sx *= fontwidth(p);
	dx *= fontwidth(p);
	width *= fontwidth(p);
	height *= fontheight(p);
	sy *= fontheight(p);
	dy *= fontheight(p);
	if ((dy < sy) || ((dy == sy) && (dx <= sx))) {
		mga_fifo(2);
		mga_outl(M_AR5, pixx);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_SGNZERO |
			       M_DWG_BFCOL | M_DWG_REPLACE);
		width--;
		start = sy*pixx+sx+curr_ydstorg(MINFO);
		end = start+width;
	} else {
		mga_fifo(3);
		mga_outl(M_SGN, 5);
		mga_outl(M_AR5, -pixx);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_BFCOL | M_DWG_REPLACE);
		width--;
		end = (sy+height-1)*pixx+sx+curr_ydstorg(MINFO);
		start = end+width;
		dy += height-1;
	}
	mga_fifo(4);
	mga_outl(M_AR0, end);
	mga_outl(M_AR3, start);
	mga_outl(M_FXBNDRY, ((dx+width)<<16) | dx);
	mga_outl(M_YDSTLEN | M_EXEC, ((dy)<<16) | height);
	WaitTillIdle(MINFO);
}

#ifdef FBCON_HAS_CFB4
static void matrox_cfb4_bmove(struct display* p, int sy, int sx, int dy, int dx, int height, int width) {
	int pixx, start, end;
	MINFO_FROM_DISP(p);
	/* both (sx or dx or width) and fontwidth() are odd, so their multiply is
	   also odd, that means that we cannot use acceleration */
	if ((sx | dx | width) & fontwidth(p) & 1) {
		fbcon_cfb4_bmove(p, sy, sx, dy, dx, height, width);
		return;
	}
	sx *= fontwidth(p);
	dx *= fontwidth(p);
	width *= fontwidth(p);
	height *= fontheight(p);
	sy *= fontheight(p);
	dy *= fontheight(p);
	pixx = p->var.xres_virtual >> 1;
	sx >>= 1;
	dx >>= 1;
	width >>= 1;
	if ((dy < sy) || ((dy == sy) && (dx <= sx))) {
		mga_fifo(2);
		mga_outl(M_AR5, pixx);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_SGNZERO |
			M_DWG_BFCOL | M_DWG_REPLACE);
		width--;
		start = sy*pixx+sx+curr_ydstorg(MINFO);
		end = start+width;
	} else {
		mga_fifo(3);
		mga_outl(M_SGN, 5);
		mga_outl(M_AR5, -pixx);
		mga_outl(M_DWGCTL, M_DWG_BITBLT | M_DWG_SHIFTZERO | M_DWG_BFCOL | M_DWG_REPLACE);
		width--;
		end = (sy+height-1)*pixx+sx+curr_ydstorg(MINFO);
		start = end+width;
		dy += height-1;
	}
	mga_fifo(5);
	mga_outl(M_AR0, end);
	mga_outl(M_AR3, start);
	mga_outl(M_FXBNDRY, ((dx+width)<<16) | dx);
	mga_outl(M_YDST, dy*pixx >> 5);
	mga_outl(M_LEN | M_EXEC, height);
	WaitTillIdle(MINFO);
}
#endif

static void matroxfb_accel_clear(CPMINFO u_int32_t color, int sy, int sx, int height, 
		int width) {
	mga_fifo(4);
	mga_outl(M_DWGCTL, ACCESS_FBINFO(accel.m_dwg_rect) | M_DWG_REPLACE);
	mga_outl(M_FCOL, color);
	mga_outl(M_FXBNDRY, ((sx + width) << 16) | sx);
	mga_outl(M_YDSTLEN | M_EXEC, (sy << 16) | height);
	WaitTillIdle(MINFO);
}

static void matrox_cfbX_clear(u_int32_t color, struct display* p, int sy, int sx, int height, int width) {
	matroxfb_accel_clear(PMXINFO(p) color, sy * fontheight(p), sx * fontwidth(p),
			     height * fontheight(p), width * fontwidth(p));
}

#ifdef FBCON_HAS_CFB4
static void matrox_cfb4_clear(struct vc_data* conp, struct display* p, int sy, int sx, int height, int width) {
	u_int32_t bgx;
	int whattodo;
	MINFO_FROM_DISP(p);

	whattodo = 0; 
	bgx = attr_bgcol_ec(p, conp);
	bgx |= bgx << 4;
	bgx |= bgx << 8;
	bgx |= bgx << 16;
	sy *= fontheight(p);
	sx *= fontwidth(p);
	height *= fontheight(p);
	width *= fontwidth(p);
	if (sx & 1) {
		sx ++;
		if (!width) return;
		width --;
		whattodo = 1;
	}
	if (width & 1) {
		whattodo |= 2;
	}
	width >>= 1;
	sx >>= 1;
	if (width) {
		mga_fifo(5);
		mga_outl(M_DWGCTL, ACCESS_FBINFO(accel.m_dwg_rect) | M_DWG_REPLACE2);
		mga_outl(M_FCOL, bgx);
		mga_outl(M_FXBNDRY, ((sx + width) << 16) | sx);
		mga_outl(M_YDST, sy * p->var.xres_virtual >> 6);
		mga_outl(M_LEN | M_EXEC, height);
		WaitTillIdle(MINFO);
	}
	if (whattodo) {
		u_int32_t step = p->var.xres_virtual >> 1;
		if (whattodo & 1) {
			u_int8_t* uaddr = p->screen_base + sy * step + sx - 1;
			u_int32_t loop;
			u_int8_t bgx2 = bgx & 0xF0;
			for (loop = height; loop > 0; loop --) {
				*uaddr = (*uaddr & 0x0F) | bgx2;
				uaddr += step;
			}
		}
		if (whattodo & 2) {
			u_int8_t* uaddr = p->screen_base + sy * step + sx + width;
			u_int32_t loop;
			u_int8_t bgx2 = bgx & 0x0F;
			for (loop = height; loop > 0; loop --) {
				*uaddr = (*uaddr & 0xF0) | bgx2;
				uaddr += step;
			}
		}
	}
}
#endif

#ifdef FBCON_HAS_CFB8
static void matrox_cfb8_clear(struct vc_data* conp, struct display* p, int sy, int sx, int height, int width) {
	u_int32_t bgx;

	bgx = attr_bgcol_ec(p, conp);
	bgx |= bgx << 8;
	bgx |= bgx << 16;
	matrox_cfbX_clear(bgx, p, sy, sx, height, width);
}
#endif

#ifdef FBCON_HAS_CFB16
static void matrox_cfb16_clear(struct vc_data* conp, struct display* p, int sy, int sx, int height, int width) {
	u_int32_t bgx;

	bgx = ((u_int16_t*)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	matrox_cfbX_clear((bgx << 16) | bgx, p, sy, sx, height, width);
}
#endif

#if defined(FBCON_HAS_CFB32) || defined(FBCON_HAS_CFB24)
static void matrox_cfb32_clear(struct vc_data* conp, struct display* p, int sy, int sx, int height, int width) {
	u_int32_t bgx;

	bgx = ((u_int32_t*)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	matrox_cfbX_clear(bgx, p, sy, sx, height, width);
}
#endif

static void matrox_cfbX_putc(u_int32_t fgx, u_int32_t bgx, struct display* p, int c, int yy, int xx) {
	u_int32_t ar0;
	u_int32_t step;
	MINFO_FROM_DISP(p);

	yy *= fontheight(p);
	xx *= fontwidth(p);
	mga_fifo(7);
	mga_outl(M_DWGCTL, M_DWG_ILOAD | M_DWG_SGNZERO | M_DWG_SHIFTZERO | M_DWG_BMONOWF | M_DWG_LINEAR | M_DWG_REPLACE);

	mga_outl(M_FCOL, fgx);
	mga_outl(M_BCOL, bgx);
	ar0 = fontwidth(p) - 1;
	mga_outl(M_FXBNDRY, ((xx+ar0)<<16) | xx);
	if (fontwidth(p) <= 8)
		step = 1;
	else if (fontwidth(p) <= 16)
		step = 2;
	else 
		step = 4;
	/* TODO: set OPMODE_DMA_LE for BE */
	if (fontwidth(p) == step << 3) {
		size_t charcell = fontheight(p)*step;
		/* TODO: Align charcell to 4B for BE */
		mga_outl(M_AR3, 0);
		mga_outl(M_AR0, fontheight(p)*fontwidth(p)-1);
		mga_outl(M_YDSTLEN | M_EXEC, (yy<<16) | fontheight(p));
		memcpy(ACCESS_FBINFO(mmio), p->fontdata+(c&p->charmask)*charcell, charcell);
	} else {
		u8* chardata = p->fontdata+(c&p->charmask)*fontheight(p)*step;
		u_int32_t ydstlen_tmp = (yy << 16) | 1;
		int i;
		switch (step) {
		case 1:	
			for (i = fontheight(p); i > 0; i--) {
				mga_fifo(3);
				mga_outl(M_AR0, ar0);
				mga_outl(M_AR3, 0);
				mga_outl(M_YDSTLEN | M_EXEC, ydstlen_tmp);
#ifdef __LITTLE_ENDIAN
				*(u_int32_t*)ACCESS_FBINFO(mmio) = *chardata++;
#else
				*(u_int32_t*)ACCESS_FBINFO(mmio) = (*chardata++) << 24;
#endif
				ydstlen_tmp += 0x10000;
			}
			break;
		case 2:
			for (i = fontheight(p); i > 0; i--) {
				mga_fifo(3);
				mga_outl(M_AR0, ar0);
				mga_outl(M_AR3, 0);
				mga_outl(M_YDSTLEN | M_EXEC, ydstlen_tmp);
#ifdef __LITTLE_ENDIAN
				*(u_int32_t*)ACCESS_FBINFO(mmio) = *(u_int16_t*)chardata;
#else
				*(u_int32_t*)ACCESS_FBINFO(mmio) = (*(u_int16_t*)chardata) << 16;
#endif
				ydstlen_tmp += 0x10000;
				chardata += 2;
			}
			break;
		case 4:
			for (i = fontheight(p); i > 0; i--) {
				mga_fifo(3);
				mga_outl(M_AR0, ar0);
				mga_outl(M_AR3, 0);
				mga_outl(M_YDSTLEN | M_EXEC, ydstlen_tmp);
				*(u_int32_t*)ACCESS_FBINFO(mmio) = *(u_int32_t*)chardata;
				ydstlen_tmp += 0x10000;
				chardata += 4;
			}
			break;
		}
	}
	/* TODO: restore OPMODE for BE */
	WaitTillIdle(MINFO);
}

#ifdef FBCON_HAS_CFB8
static void matrox_cfb8_putc(struct vc_data* conp, struct display* p, int c, int yy, int xx) {
	u_int32_t fgx, bgx;

	fgx = attr_fgcol(p, c);
	bgx = attr_bgcol(p, c);
	fgx |= (fgx << 8);
	fgx |= (fgx << 16);
	bgx |= (bgx << 8);
	bgx |= (bgx << 16);
	matrox_cfbX_putc(fgx, bgx, p, c, yy, xx);
}
#endif

#ifdef FBCON_HAS_CFB16
static void matrox_cfb16_putc(struct vc_data* conp, struct display* p, int c, int yy, int xx) {
	u_int32_t fgx, bgx;

	fgx = ((u_int16_t*)p->dispsw_data)[attr_fgcol(p, c)];
	bgx = ((u_int16_t*)p->dispsw_data)[attr_bgcol(p, c)];
	fgx |= (fgx << 16);
	bgx |= (bgx << 16);
	matrox_cfbX_putc(fgx, bgx, p, c, yy, xx);
}
#endif

#if defined(FBCON_HAS_CFB32) || defined(FBCON_HAS_CFB24)
static void matrox_cfb32_putc(struct vc_data* conp, struct display* p, int c, int yy, int xx) {
	u_int32_t fgx, bgx;

	fgx = ((u_int32_t*)p->dispsw_data)[attr_fgcol(p, c)];
	bgx = ((u_int32_t*)p->dispsw_data)[attr_bgcol(p, c)];
	matrox_cfbX_putc(fgx, bgx, p, c, yy, xx);
}
#endif

static void matrox_cfbX_putcs(u_int32_t fgx, u_int32_t bgx, struct display* p, const unsigned short* s, int count, int yy, int xx) {
	u_int32_t step;
	u_int32_t ydstlen;
	u_int32_t xlen;
	u_int32_t ar0;
	u_int32_t charcell;
	u_int32_t fxbndry;
	int easy;
	MINFO_FROM_DISP(p);

	yy *= fontheight(p);
	xx *= fontwidth(p);
	if (fontwidth(p) <= 8)
		step = 1;
	else if (fontwidth(p) <= 16)
		step = 2;
	else
		step = 4;
	charcell = fontheight(p)*step;
	xlen = (charcell + 3) & ~3;
	if (fontwidth(p) == step << 3) {
		ydstlen = (yy<<16) | fontheight(p);
		ar0 = fontheight(p)*fontwidth(p) - 1;
		easy = 1;
	} else {
		ydstlen = (yy<<16) | 1;
		ar0 = fontwidth(p) - 1;
		easy = 0;
	}
	mga_fifo(3);
	mga_outl(M_DWGCTL, M_DWG_ILOAD | M_DWG_SGNZERO | M_DWG_SHIFTZERO | M_DWG_BMONOWF | M_DWG_LINEAR | M_DWG_REPLACE);
	mga_outl(M_FCOL, fgx);
	mga_outl(M_BCOL, bgx);
	/* TODO: set OPMODE_DMA_LE for BE */
	fxbndry = ((xx + fontwidth(p) - 1) << 16) | xx;
	while (count--) {
		u_int8_t* chardata = p->fontdata + (*s++ & p->charmask)*charcell;

		mga_fifo(4);
		mga_outl(M_FXBNDRY, fxbndry);
		if (easy) {
			mga_outl(M_AR0, ar0);
			mga_outl(M_AR3, 0);
			mga_outl(M_YDSTLEN | M_EXEC, ydstlen);
			memcpy(ACCESS_FBINFO(mmio), chardata, xlen);
		} else {
			u_int32_t ydstlen_tmp = ydstlen;
			int i;
			switch (step) {
				case 1:	
					for (i = fontheight(p); i > 0; i--) {
						mga_fifo(3);
						mga_outl(M_AR0, ar0);
						mga_outl(M_AR3, 0);
						mga_outl(M_YDSTLEN | M_EXEC, ydstlen_tmp);
#ifdef __LITTLE_ENDIAN
						*(u_int32_t*)ACCESS_FBINFO(mmio) = *chardata++;
#else
						*(u_int32_t*)ACCESS_FBINFO(mmio) = (*chardata++) << 24;
#endif
						ydstlen_tmp += 0x10000;
					}
					break;
				case 2:
					for (i = fontheight(p); i > 0; i--) {
						mga_fifo(3);
						mga_outl(M_AR0, ar0);
						mga_outl(M_AR3, 0);
						mga_outl(M_YDSTLEN | M_EXEC, ydstlen_tmp);
#ifdef __LITTLE_ENDIAN
						*(u_int32_t*)ACCESS_FBINFO(mmio) = *(u_int16_t*)chardata;
#else
						*(u_int32_t*)ACCESS_FBINFO(mmio) = (*(u_int16_t*)chardata) << 32;
#endif
						ydstlen_tmp += 0x10000;
						chardata += 2;
					}
					break;
				case 4:
					for (i = fontheight(p); i > 0; i--) {
						mga_fifo(3);
						mga_outl(M_AR0, ar0);
						mga_outl(M_AR3, 0);
						mga_outl(M_YDSTLEN | M_EXEC, ydstlen_tmp);
						*(u_int32_t*)ACCESS_FBINFO(mmio) = *(u_int32_t*)chardata;
						ydstlen_tmp += 0x10000;
						chardata += 4;
					}
					break;
			}
		}
		fxbndry += fontwidth(p) + (fontwidth(p) << 16);
	}
	/* TODO: restore OPMODE for BE */
	WaitTillIdle(MINFO);
}

#ifdef FBCON_HAS_CFB8
static void matrox_cfb8_putcs(struct vc_data* conp, struct display* p, const unsigned short* s, int count, int yy, int xx) {
	u_int32_t fgx, bgx;

	fgx = attr_fgcol(p, *s);
	bgx = attr_bgcol(p, *s);
	fgx |= (fgx << 8);
	fgx |= (fgx << 16);
	bgx |= (bgx << 8);
	bgx |= (bgx << 16);
	matrox_cfbX_putcs(fgx, bgx, p, s, count, yy, xx);
}
#endif

#ifdef FBCON_HAS_CFB16
static void matrox_cfb16_putcs(struct vc_data* conp, struct display* p, const unsigned short* s, int count, int yy, int xx) {
	u_int32_t fgx, bgx;

	fgx = ((u_int16_t*)p->dispsw_data)[attr_fgcol(p, *s)];
	bgx = ((u_int16_t*)p->dispsw_data)[attr_bgcol(p, *s)];
	fgx |= (fgx << 16);
	bgx |= (bgx << 16);
	matrox_cfbX_putcs(fgx, bgx, p, s, count, yy, xx);
}
#endif

#if defined(FBCON_HAS_CFB32) || defined(FBCON_HAS_CFB24)
static void matrox_cfb32_putcs(struct vc_data* conp, struct display* p, const unsigned short* s, int count, int yy, int xx) {
	u_int32_t fgx, bgx;

	fgx = ((u_int32_t*)p->dispsw_data)[attr_fgcol(p, *s)];
	bgx = ((u_int32_t*)p->dispsw_data)[attr_bgcol(p, *s)];
	matrox_cfbX_putcs(fgx, bgx, p, s, count, yy, xx);
}
#endif

#ifdef FBCON_HAS_CFB4
static void matrox_cfb4_revc(struct display* p, int xx, int yy) {
	MINFO_FROM_DISP(p);

	if (fontwidth(p) & 1) {
		fbcon_cfb4_revc(p, xx, yy);
		return;
	}
	yy *= fontheight(p);
	xx *= fontwidth(p);
	xx |= (xx + fontwidth(p)) << 16;
	xx >>= 1;

	mga_fifo(5);
	mga_outl(M_DWGCTL, ACCESS_FBINFO(accel.m_dwg_rect) | M_DWG_XOR);
	mga_outl(M_FCOL, 0xFFFFFFFF);
	mga_outl(M_FXBNDRY, xx);
	mga_outl(M_YDST, yy * p->var.xres_virtual >> 6);
	mga_outl(M_LEN | M_EXEC, fontheight(p));
	WaitTillIdle(MINFO);
} 
#endif

#ifdef FBCON_HAS_CFB8
static void matrox_cfb8_revc(struct display* p, int xx, int yy) {
	MINFO_FROM_DISP(p);

	yy *= fontheight(p);
	xx *= fontwidth(p);

	mga_fifo(4);
	mga_outl(M_DWGCTL, ACCESS_FBINFO(accel.m_dwg_rect) | M_DWG_XOR);
	mga_outl(M_FCOL, 0x0F0F0F0F);
	mga_outl(M_FXBNDRY, ((xx + fontwidth(p)) << 16) | xx);
	mga_outl(M_YDSTLEN | M_EXEC, (yy << 16) | fontheight(p));
	WaitTillIdle(MINFO);
}
#endif

static void matrox_cfbX_revc(struct display* p, int xx, int yy) {
	MINFO_FROM_DISP(p);

	yy *= fontheight(p);
	xx *= fontwidth(p);

	mga_fifo(4);
	mga_outl(M_DWGCTL, ACCESS_FBINFO(accel.m_dwg_rect) | M_DWG_XOR);
	mga_outl(M_FCOL, 0xFFFFFFFF);
	mga_outl(M_FXBNDRY, ((xx + fontwidth(p)) << 16) | xx);
	mga_outl(M_YDSTLEN | M_EXEC, (yy << 16) | fontheight(p));
	WaitTillIdle(MINFO);
}

static void matrox_cfbX_clear_margins(struct vc_data* conp, struct display* p, int bottom_only) {
	unsigned int bottom_height, right_width;
	unsigned int bottom_start, right_start;
	unsigned int cell_h, cell_w;

	cell_w = fontwidth(p);
	if (!cell_w) return;	/* PARANOID */
	right_width = p->var.xres % cell_w;
	right_start = p->var.xres - right_width;
	if (!bottom_only && right_width) {
		matroxfb_accel_clear(     PMXINFO(p)
			     /* color */  0x00000000,
			     /* y */      p->var.yoffset,
			     /* x */      p->var.xoffset + right_start,
			     /* height */ p->var.yres,
			     /* width */  right_width);
	}
	cell_h = fontheight(p);
	if (!cell_h) return;	/* PARANOID */
	bottom_height = p->var.yres % cell_h;
	if (bottom_height) {
		bottom_start = p->var.yres - bottom_height;
		matroxfb_accel_clear(		  PMXINFO(p)
				     /* color */  0x00000000,
				     /* y */	  p->var.yoffset + bottom_start,
				     /* x */	  p->var.xoffset,
				     /* height */ bottom_height,
				     /* width */  right_start);
	}
}

#ifdef CONFIG_FB_MATROX_MILLENIUM
static void outTi3026(CPMINFO int reg, int val) {
	mga_outb(M_RAMDAC_BASE+TVP3026_INDEX, reg);
	mga_outb(M_RAMDAC_BASE+TVP3026_X_DATAREG, val);
}

static int inTi3026(CPMINFO int reg) {
	mga_outb(M_RAMDAC_BASE+TVP3026_INDEX, reg);
	return mga_inb(M_RAMDAC_BASE+TVP3026_X_DATAREG);
}

#define POS3026_XCURCTRL	20

static void matroxfb_ti3026_cursor(struct display* p, int mode, int x, int y) {
	MINFO_FROM_DISP(p);

	if (mode == CM_ERASE) {
		outTi3026(PMINFO TVP3026_XCURCTRL, ACCESS_FBINFO(currenthw->DACreg[POS3026_XCURCTRL]) & ~3);
		return;
	}
	x *= fontwidth(p);
	y *= fontheight(p);
	y -= p->var.yoffset;
	if ((x != ACCESS_FBINFO(cursor.x)) || (y != ACCESS_FBINFO(cursor.y))) {
		ACCESS_FBINFO(cursor.x) = x;
		ACCESS_FBINFO(cursor.y) = y;
		x += 64;
		y += 64;
		outTi3026(PMINFO TVP3026_XCURCTRL, ACCESS_FBINFO(currenthw->DACreg[POS3026_XCURCTRL]) & ~3);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURPOSXL, x);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURPOSXH, x >> 8);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURPOSYL, y);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURPOSYH, y >> 8);
	}
	outTi3026(PMINFO TVP3026_XCURCTRL, ACCESS_FBINFO(currenthw->DACreg[POS3026_XCURCTRL]) | 2);
}
#undef POS3026_XCURCTRL

static void matroxfb_ti3026_createcursor(WPMINFO int w, int h) {
	u_int32_t xline;
	int i;

	ACCESS_FBINFO(cursor.w) = w;
	ACCESS_FBINFO(cursor.h) = h;
	xline = (~0) << (32 - ACCESS_FBINFO(cursor.w));
	mga_outb(M_RAMDAC_BASE+TVP3026_INDEX, 0);
	for (i = 0; i < ACCESS_FBINFO(cursor.h); i++) {
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, xline >> 24);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, xline >> 16);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, xline >> 8);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, xline);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, 0);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, 0);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, 0);
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, 0);
	}
	i *= 8;
	for (; i < 512; i++)
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, 0);
	for (i = 0; i < 512; i++) 
		mga_outb(M_RAMDAC_BASE+TVP3026_CURRAMDATA, 0xFF);
}
	
static int matroxfb_ti3026_setfont(struct display* p, int width, int height) {
	matroxfb_ti3026_createcursor(PMXINFO(p) width, height);
	return 0;
}
#endif

#define matrox_cfb16_revc matrox_cfbX_revc
#define matrox_cfb24_revc matrox_cfbX_revc
#define matrox_cfb32_revc matrox_cfbX_revc

#define matrox_cfb24_clear matrox_cfb32_clear
#define matrox_cfb24_putc matrox_cfb32_putc
#define matrox_cfb24_putcs matrox_cfb32_putcs

#ifdef FBCON_HAS_CFB4
static struct display_switch matroxfb_cfb4 = {
	fbcon_cfb4_setup,     matrox_cfb4_bmove,  matrox_cfb4_clear,
	fbcon_cfb4_putc,      fbcon_cfb4_putcs,	  matrox_cfb4_revc,
	NULL,		      NULL,		  NULL,
	/* cursor... */       /* set_font... */
	FONTWIDTH(8) /* fix, fix, fix it */
};
#endif

#ifdef FBCON_HAS_CFB8
static struct display_switch matroxfb_cfb8 = {
	fbcon_cfb8_setup,     matrox_cfbX_bmove,  matrox_cfb8_clear,
	matrox_cfb8_putc,     matrox_cfb8_putcs,  matrox_cfb8_revc,
	NULL,		      NULL,		  matrox_cfbX_clear_margins,
	~1 /* FONTWIDTHS */
};
#endif

#ifdef FBCON_HAS_CFB16
static struct display_switch matroxfb_cfb16 = {
	fbcon_cfb16_setup,    matrox_cfbX_bmove,  matrox_cfb16_clear,
	matrox_cfb16_putc,    matrox_cfb16_putcs, matrox_cfb16_revc,
	NULL,		      NULL,		  matrox_cfbX_clear_margins,
	~1 /* FONTWIDTHS */
};
#endif

#ifdef FBCON_HAS_CFB24
static struct display_switch matroxfb_cfb24 = {
	fbcon_cfb24_setup,    matrox_cfbX_bmove,  matrox_cfb24_clear,
	matrox_cfb24_putc,    matrox_cfb24_putcs, matrox_cfb24_revc,
	NULL,		      NULL,		  matrox_cfbX_clear_margins,
	~1 /* FONTWIDTHS */ /* TODO: and what about non-aligned access on BE? I think that there are no in my code */
};
#endif

#ifdef FBCON_HAS_CFB32
static struct display_switch matroxfb_cfb32 = {
	fbcon_cfb32_setup,    matrox_cfbX_bmove,  matrox_cfb32_clear,
	matrox_cfb32_putc,    matrox_cfb32_putcs, matrox_cfb32_revc,
	NULL,		      NULL,		  matrox_cfbX_clear_margins,
	~1 /* FONTWIDTHS */
};
#endif

static struct pci_dev* pci_find(struct pci_dev* p) {
	if (p) return p->next;
	return pci_devices;
}

static void initMatrox(WPMINFO int con, struct display* p) {
	struct display_switch *swtmp;

	p->dispsw_data = NULL;
	if ((p->var.accel_flags & FB_ACCELF_TEXT) != FB_ACCELF_TEXT) {
		switch (p->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
			case 4:
				swtmp = &fbcon_cfb4;
				break;
#endif
#ifdef FBCON_HAS_CFB8
			case 8:
				swtmp = &fbcon_cfb8;
				break;
#endif
#ifdef FBCON_HAS_CFB16
			case 16:
				p->dispsw_data = &ACCESS_FBINFO(cmap.cfb16);
				swtmp = &fbcon_cfb16;
				break;
#endif
#ifdef FBCON_HAS_CFB24
			case 24:
				p->dispsw_data = &ACCESS_FBINFO(cmap.cfb24);
				swtmp = &fbcon_cfb24;
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case 32:
				p->dispsw_data = &ACCESS_FBINFO(cmap.cfb32);
				swtmp = &fbcon_cfb32;
				break;
#endif
			default:
				p->dispsw = &fbcon_dummy;
				return;
		}
		dprintk(KERN_INFO "matroxfb: acceleration disabled\n");
	} else {
		switch (p->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
			case 4:
				swtmp = &matroxfb_cfb4;
				break;
#endif
#ifdef FBCON_HAS_CFB8
			case 8:
				swtmp = &matroxfb_cfb8;
				break;
#endif
#ifdef FBCON_HAS_CFB16
			case 16:
				p->dispsw_data = &ACCESS_FBINFO(cmap.cfb16);
				swtmp = &matroxfb_cfb16;
				break;
#endif
#ifdef FBCON_HAS_CFB24
			case 24:
				p->dispsw_data = &ACCESS_FBINFO(cmap.cfb24);
				swtmp = &matroxfb_cfb24;
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case 32:
				p->dispsw_data = &ACCESS_FBINFO(cmap.cfb32);
				swtmp = &matroxfb_cfb32;
				break;
#endif
			default:
				p->dispsw = &fbcon_dummy;
				return;
		}
		dprintk(KERN_INFO "matroxfb: now accelerated\n");
	}
	memcpy(&ACCESS_FBINFO(dispsw), swtmp, sizeof(ACCESS_FBINFO(dispsw)));
	p->dispsw = &ACCESS_FBINFO(dispsw);
#ifdef CONFIG_FB_MATROX_MILLENIUM
	if (isMillenium(MINFO) && ACCESS_FBINFO(devflags.hwcursor)) {
		ACCESS_FBINFO(dispsw.cursor) = matroxfb_ti3026_cursor;
		ACCESS_FBINFO(dispsw.set_font) = matroxfb_ti3026_setfont;
	}
#endif
}

/* --------------------------------------------------------------------- */

/*
 * card parameters
 */

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo vesafb_defined __initdata = {
	0,0,0,0,	/* W,H, W, H (virtual) load xres,xres_virtual*/
	0,0,		/* virtual -> visible no offset */
	8,		/* depth -> load bits_per_pixel */
	0,		/* greyscale ? */
	{0,0,0},	/* R */
	{0,0,0},	/* G */
	{0,0,0},	/* B */
	{0,0,0},	/* transparency */
	0,		/* standard pixel format */
	FB_ACTIVATE_NOW,
	-1,-1,
	FB_ACCELF_TEXT,	/* accel flags */
	0L,0L,0L,0L,0L,
	0L,0L,0,	/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0,0,0,0,0,0}
};



/* --------------------------------------------------------------------- */

static void matrox_pan_var(struct fb_var_screeninfo *var, struct matrox_fb_info* minfo) {
	struct matroxfb_par* p = &ACCESS_FBINFO(curr);
	unsigned int pos;
	unsigned short p0, p1, p2;

	p->var.xoffset = var->xoffset;
	p->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		p->var.vmode |= FB_VMODE_YWRAP;
	else
		p->var.vmode &= FB_VMODE_YWRAP;
	
	pos = (p->var.yoffset * p->var.xres_virtual + p->var.xoffset) * p->final_bppShift / 32;
	if (mga_ydstorg(MINFO)) {
		if (isInterleave(MINFO))
			pos += mga_ydstorg(MINFO) >> 3;
		else
			pos += mga_ydstorg(MINFO) >> 2;
	}

	p0 = ACCESS_FBINFO(currenthw)->CRTC[0x0D] = pos & 0xFF;
	p1 = ACCESS_FBINFO(currenthw)->CRTC[0x0C] = (pos & 0xFF00) >> 8;
	p2 = ACCESS_FBINFO(currenthw)->CRTCEXT[0] = (ACCESS_FBINFO(currenthw)->CRTCEXT[0] & 0xF0) | ((pos >> 16) & 0x0F);
	mga_setr(M_CRTC_INDEX, 0x0D, p0);
	mga_setr(M_CRTC_INDEX, 0x0C, p1);
	mga_setr(M_EXTVGA_INDEX, 0x00, p2);
}

	/*
	 * Open/Release the frame buffer device
	 */

static int matroxfb_open(struct fb_info *info, int user)
{
	/*
	 * Nothing, only a usage count for the moment
	 */
	MOD_INC_USE_COUNT;
	return(0);
}

static int matroxfb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return(0);
}

static int matroxfb_pan_display(struct fb_var_screeninfo *var, int con,
		struct fb_info* info) {
#define minfo ((struct matrox_fb_info*)info)
	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0 || var->yoffset >= fb_display[con].var.yres_virtual || var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset+fb_display[con].var.xres > fb_display[con].var.xres_virtual ||
		    var->yoffset+fb_display[con].var.yres > fb_display[con].var.yres_virtual)
			return -EINVAL;
	}
	if (con == ACCESS_FBINFO(currcon))
		matrox_pan_var(var, MINFO);
	fb_display[con].var.xoffset = var->xoffset;
	fb_display[con].var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		fb_display[con].var.vmode |= FB_VMODE_YWRAP;
	else
		fb_display[con].var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
#undef minfo
}
	
static int matroxfb_updatevar(int con, struct fb_info *info)
{
	matrox_pan_var(&fb_display[con].var, (struct matrox_fb_info*)info);
	return 0;
}

static int matroxfb_get_final_bppShift(CPMINFO int bpp) {
	int bppshft2;

	bppshft2 = bpp;
	if (isInterleave(MINFO))
		bppshft2 >>= 1;
	if (ACCESS_FBINFO(devflags.video64bits))
		bppshft2 >>= 1;
	return bppshft2;
}
				
static int matroxfb_test_and_set_rounding(CPMINFO int xres, int bpp) {
	int over;
	int rounding;

	switch (bpp) {
		case 4:		rounding = 128;
				break;
		case 8:		rounding = 64;
				break;
		case 16:	rounding = 32;
				break;
		case 24:	rounding = 64;
				break;
		default:	rounding = 16;
				break;
	}
	if (isInterleave(MINFO)) {
		rounding *= 2;
	}
	over = xres % rounding;
	if (over) 
		xres += rounding-over;
	return xres;
}

static int matroxfb_pitch_adjust(CPMINFO int xres, int bpp) {
	const int* width;
	int xres_new;

	width = ACCESS_FBINFO(capable.vxres);

	if (ACCESS_FBINFO(devflags.precise_width)) {
		while (*width) {
			if ((*width >= xres) && (matroxfb_test_and_set_rounding(PMINFO *width, bpp) == *width)) {
				break;
			}
			width++;
		}
		xres_new = *width;
	} else {
		xres_new = matroxfb_test_and_set_rounding(PMINFO xres, bpp);
	}
	if (!xres_new) return 0;
	if (xres != xres_new) {
		printk(KERN_INFO "matroxfb: cannot set xres to %d, rounded up to %d\n", xres, xres_new);
	}
	return xres_new;
}

#ifdef NEED_DAC1064
static void outDAC1064(CPMINFO int reg, int val) {
	mga_outb(M_RAMDAC_BASE+M1064_INDEX, reg);
	mga_outb(M_RAMDAC_BASE+M1064_X_DATAREG, val);
}

static int inDAC1064(CPMINFO int reg) {
	mga_outb(M_RAMDAC_BASE+M1064_INDEX, reg);
	return mga_inb(M_RAMDAC_BASE+M1064_X_DATAREG);
}
#endif

#ifdef CONFIG_FB_MATROX_MILLENIUM
static const unsigned char DACseq[] =
{ TVP3026_XLATCHCTRL, TVP3026_XTRUECOLORCTRL, 
  TVP3026_XMUXCTRL, TVP3026_XCLKCTRL, 
  TVP3026_XPALETTEPAGE, 
  TVP3026_XGENCTRL, 
  TVP3026_XMISCCTRL, 
  TVP3026_XGENIOCTRL,
  TVP3026_XGENIODATA, 
  TVP3026_XCOLKEYOVRMIN, TVP3026_XCOLKEYOVRMAX, TVP3026_XCOLKEYREDMIN, TVP3026_XCOLKEYREDMAX,
  TVP3026_XCOLKEYGREENMIN, TVP3026_XCOLKEYGREENMAX, TVP3026_XCOLKEYBLUEMIN, TVP3026_XCOLKEYBLUEMAX,
  TVP3026_XCOLKEYCTRL,
  TVP3026_XMEMPLLCTRL, TVP3026_XSENSETEST, TVP3026_XCURCTRL };

#define POS3026_XLATCHCTRL	0
#define POS3026_XTRUECOLORCTRL	1
#define POS3026_XMUXCTRL	2
#define POS3026_XCLKCTRL	3
#define POS3026_XGENCTRL	5
#define POS3026_XMISCCTRL	6
#define POS3026_XMEMPLLCTRL	18
#define POS3026_XCURCTRL	20

static const unsigned char MGADACbpp32[] =
{ TVP3026_XLATCHCTRL_2_1, TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_ORGB_8888,
  0x00, TVP3026_XCLKCTRL_DIV1 | TVP3026_XCLKCTRL_SRC_PLL, 
  0x00, 
  TVP3026_XGENCTRL_HSYNC_POS | TVP3026_XGENCTRL_VSYNC_POS | TVP3026_XGENCTRL_LITTLE_ENDIAN | TVP3026_XGENCTRL_BLACK_0IRE | TVP3026_XGENCTRL_NO_SYNC_ON_GREEN | TVP3026_XGENCTRL_OVERSCAN_DIS, 
  TVP3026_XMISCCTRL_DAC_PUP | TVP3026_XMISCCTRL_DAC_8BIT | TVP3026_XMISCCTRL_PSEL_DIS | TVP3026_XMISCCTRL_PSEL_HIGH, 
  0x00, 
  0x1E, 
  0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF,
  TVP3026_XCOLKEYCTRL_ZOOM1, 
  0x00, 0x00, TVP3026_XCURCTRL_DIS };
#endif	/* CONFIG_FB_MATROX_MILLENIUM */

static int PLL_calcclock(CPMINFO unsigned int freq, unsigned int fmax, unsigned int* in, unsigned int* feed, unsigned int* post) {
	unsigned int bestdiff = ~0;
	unsigned int bestvco = 0;
	unsigned int fxtal = ACCESS_FBINFO(features.pll.ref_freq);
	unsigned int fwant;
	int p;

	fwant = freq;

#ifdef DEBUG
	printk(KERN_ERR "post_shift_max: %d\n", ACCESS_FBINFO(features.pll.post_shift_max));
	printk(KERN_ERR "ref_freq: %d\n", ACCESS_FBINFO(features.pll.ref_freq));
	printk(KERN_ERR "freq: %d\n", freq);
	printk(KERN_ERR "vco_freq_min: %d\n", ACCESS_FBINFO(features.pll.vco_freq_min));
	printk(KERN_ERR "in_div_min: %d\n", ACCESS_FBINFO(features.pll.in_div_min));
	printk(KERN_ERR "in_div_max: %d\n", ACCESS_FBINFO(features.pll.in_div_max));
	printk(KERN_ERR "feed_div_min: %d\n", ACCESS_FBINFO(features.pll.feed_div_min));
	printk(KERN_ERR "feed_div_max: %d\n", ACCESS_FBINFO(features.pll.feed_div_max));
	printk(KERN_ERR "fmax: %d\n", fmax);
#endif
	for (p = 0; p < ACCESS_FBINFO(features.pll.post_shift_max); p++) {
		if (fwant * 2 > fmax)
			break;
		fwant *= 2;
	}
	if (fwant < ACCESS_FBINFO(features.pll.vco_freq_min)) fwant = ACCESS_FBINFO(features.pll.vco_freq_min);
	if (fwant > fmax) fwant = fmax;
	for (; p >= 0; fwant >>= 1, bestdiff >>= 1, --p) {
		int m;

		if (fwant < ACCESS_FBINFO(features.pll.vco_freq_min)) break;
		for (m = ACCESS_FBINFO(features.pll.in_div_min); m <= ACCESS_FBINFO(features.pll.in_div_max); m++) {
			unsigned int diff, fvco;
			int n;

			n = (fwant * (m + 1) + (fxtal >> 1)) / fxtal - 1;
			if (n > ACCESS_FBINFO(features.pll.feed_div_max)) 
				break;
			if (n < ACCESS_FBINFO(features.pll.feed_div_min))
				n = ACCESS_FBINFO(features.pll.feed_div_min);
			fvco = (fxtal * (n + 1)) / (m + 1);
			if (fvco < fwant)
				diff = fwant - fvco;
			else
				diff = fvco - fwant;
			if (diff < bestdiff) {
				bestdiff = diff;
				*post = p;
				*in = m;
				*feed = n;
				bestvco = fvco;
			}
		}
	}
	dprintk(KERN_ERR "clk: %02X %02X %02X %d %d %d\n", *in, *feed, *post, fxtal, bestvco, fwant);
	return bestvco;
}

#ifdef NEED_DAC1064
static void DAC1064_calcclock(CPMINFO unsigned int freq, unsigned int fmax, unsigned int* in, unsigned int* feed, unsigned int* post) {
	unsigned int fvco;
	unsigned int p;

	fvco = PLL_calcclock(PMINFO freq, fmax, in, feed, &p);
	p = (1 << p) - 1;
	if (fvco <= 100000)
		;
	else if (fvco <= 140000)
		p |= 0x08;
	else if (fvco <= 180000)
		p |= 0x10;
	else
		p |= 0x18;
	*post = p;
}
#endif

#ifdef CONFIG_FB_MATROX_MILLENIUM
static int Ti3026_calcclock(CPMINFO unsigned int freq, unsigned int fmax, int* in, int* feed, int* post) {
	unsigned int fvco;
	unsigned int lin, lfeed, lpost;

	fvco = PLL_calcclock(PMINFO freq, fmax, &lin, &lfeed, &lpost);
	fvco >>= (*post = lpost);
	*in = 64 - lin;
	*feed = 64 - lfeed;
	return fvco;
}

static int Ti3026_setpclk(CPMINFO struct matrox_hw_state* hw, int clk, int Bpp) {
	unsigned int f_pll;
	unsigned int pixfeed, pixin, pixpost;
	unsigned int loopfeed, loopin, looppost, loopdiv, z;
	
	f_pll = Ti3026_calcclock(PMINFO clk, ACCESS_FBINFO(max_pixel_clock), &pixin, &pixfeed, &pixpost);
	
	hw->DACclk[0] = pixin | 0xC0;
	hw->DACclk[1] = pixfeed;
	hw->DACclk[2] = pixpost | 0xB0;
	if (ACCESS_FBINFO(curr.var.bits_per_pixel) == 24) {
		loopfeed = 3;		/* set lm to any possible value */
		loopin = 3 * 32 / Bpp;
	} else {
		loopfeed = 4;
		loopin = 4 * 32 / Bpp;
	}
	z = (110000 * loopin) / (f_pll * loopfeed);
	loopdiv = 0; /* div 2 */
	if (z < 2)
		looppost = 0;
	else if (z < 4)
		looppost = 1;
	else if (z < 8)
		looppost = 2;
	else {
		looppost = 3;
		loopdiv = z/16; 
	}
	if (ACCESS_FBINFO(curr.var.bits_per_pixel) == 24) {
		hw->DACclk[3] = ((65 - loopin) & 0x3F) | 0xC0;
		hw->DACclk[4] = (65 - loopfeed) | 0x80;
		if (ACCESS_FBINFO(accel.ramdac_rev) > 0x20) {
			if (isInterleave(MINFO))
				hw->DACreg[POS3026_XLATCHCTRL] = TVP3026B_XLATCHCTRL_8_3;
			else {
				hw->DACclk[4] &= ~0xC0;
				hw->DACreg[POS3026_XLATCHCTRL] = TVP3026B_XLATCHCTRL_4_3;
			}
		}
		hw->DACclk[5] = looppost | 0xF8;
		if (ACCESS_FBINFO(devflags.mga_24bpp_fix))
			hw->DACclk[5] ^= 0x40;
	} else {
		hw->DACclk[3] = ((65 - loopin) & 0x3F) | 0xC0;
		hw->DACclk[4] = 65 - loopfeed;
		hw->DACclk[5] = looppost | 0xF0;
	}
	hw->DACreg[POS3026_XMEMPLLCTRL] = loopdiv | TVP3026_XMEMPLLCTRL_STROBEMKC4 | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL | TVP3026_XMEMPLLCTRL_RCLK_LOOPPLL;
	return 0;
}
#endif

static void var2my(struct fb_var_screeninfo* var, struct my_timming* mt) {
	unsigned int pixclock = var->pixclock;
	if (!pixclock) pixclock = 10000;	/* 10ns = 100MHz */
	mt->pixclock = 1000000000 / pixclock;
	if (mt->pixclock < 1) mt->pixclock = 1;
	mt->dblscan = var->vmode & FB_VMODE_DOUBLE;
	mt->interlaced = var->vmode & FB_VMODE_INTERLACED;
	mt->HDisplay = var->xres;
	mt->HSyncStart = mt->HDisplay + var->right_margin;
	mt->HSyncEnd = mt->HSyncStart + var->hsync_len;
	mt->HTotal = mt->HSyncEnd + var->left_margin;
	mt->VDisplay = var->yres;
	mt->VSyncStart = mt->VDisplay + var->lower_margin;
	mt->VSyncEnd = mt->VSyncStart + var->vsync_len;
	mt->VTotal = mt->VSyncEnd + var->upper_margin;
	mt->sync = var->sync;
}

static int vgaHWinit(CPMINFO struct matrox_hw_state* hw, struct my_timming* m) {
	unsigned int hd, hs, he, ht;
	unsigned int vd, vs, ve, vt;
	unsigned int wd;
	unsigned int divider;
	int i;

	hw->SEQ[0] = 0x00;
	hw->SEQ[1] = 0x01;	/* or 0x09 */
	hw->SEQ[2] = 0x0F;	/* bitplanes */
	hw->SEQ[3] = 0x00;
	hw->SEQ[4] = 0x0E;
	/* CRTC 0..7, 9, 16..19, 21, 22 are reprogrammed by Matrox Millenium code... Hope that by MGA1064 too */
	if (m->dblscan) {
		m->VTotal <<= 1;
		m->VDisplay <<= 1;
		m->VSyncStart <<= 1;
		m->VSyncEnd <<= 1;
	}
	if (m->interlaced) {
		m->VTotal >>= 1;
		m->VDisplay >>= 1;
		m->VSyncStart >>= 1;
		m->VSyncEnd >>= 1;
	}

	/* GCTL is ignored when not using 0xA0000 aperture */
	hw->GCTL[0] = 0x00;
	hw->GCTL[1] = 0x00;
	hw->GCTL[2] = 0x00;
	hw->GCTL[3] = 0x00;
	hw->GCTL[4] = 0x00;
	hw->GCTL[5] = 0x40;
	hw->GCTL[6] = 0x05;
	hw->GCTL[7] = 0x0F;
	hw->GCTL[8] = 0xFF;

	/* Whole ATTR is ignored in PowerGraphics mode */
	for (i = 0; i < 16; i++)
		hw->ATTR[i] = i;
	hw->ATTR[16] = 0x41;
	hw->ATTR[17] = 0xFF;
	hw->ATTR[18] = 0x0F;
	hw->ATTR[19] = 0x00;
	hw->ATTR[20] = 0x00;

	hd = m->HDisplay >> 3;
	hs = m->HSyncStart >> 3;
	he = m->HSyncEnd >> 3;
	ht = m->HTotal >> 3;
	/* standard timmings are in 8pixels, but for interleaved we cannot */
	/* do it for 4bpp (because of (4bpp >> 1(interleaved))/4 == 0) */
	/* using 16 or more pixels per unit can save us */
	divider = ACCESS_FBINFO(curr.final_bppShift);
	while (divider & 3) {
		hd >>= 1;
		hs >>= 1;
		he >>= 1;
		ht >>= 1;
		divider <<= 1;
	}
	divider = divider / 4;
	/* divider can be from 1 to 8 */
	while (divider > 8) {
		hd <<= 1;
		hs <<= 1;
		he <<= 1;
		ht <<= 1;
		divider >>= 1;
	}
	hd = hd - 1;
	hs = hs - 1;
	he = he - 1;
	ht = ht - 1;
	vd = m->VDisplay - 1;
	vs = m->VSyncStart - 1;
	ve = m->VSyncEnd - 1;
	vt = m->VTotal - 2;
	if (((ht & 0x0F) == 0x0E) || ((ht & 0x0F) == 0x04))
		ht++;
	wd = ACCESS_FBINFO(curr.var.xres_virtual) * ACCESS_FBINFO(curr.final_bppShift) / 64;

	hw->CRTCEXT[0] = 0;
	hw->CRTCEXT[5] = 0;
	if (m->interlaced) {
		hw->CRTCEXT[0] = 0x80;
		hw->CRTCEXT[5] = (hs + he - ht) >> 1;
		if (!m->dblscan)
			wd <<= 1;
		vt &= ~1;
	}
	hw->CRTCEXT[0] |=  (wd & 0x300) >> 4;
	hw->CRTCEXT[1] = (((ht - 4) & 0x100) >> 8) |
			  ((hd      & 0x100) >> 7) | /* blanking */
			  ((hs      & 0x100) >> 6) | /* sync start */
			   (ht      & 0x040);	 /* end hor. blanking */
	hw->CRTCEXT[2] =  ((vt & 0xC00) >> 10) |
			  ((vd & 0x400) >>  8) |	/* disp end */
			  ((vd & 0xC00) >>  7) |	/* vblanking start */
			  ((vs & 0xC00) >>  5);
	hw->CRTCEXT[3] = (divider - 1) | 0x80;
	hw->CRTCEXT[4] = 0;

	hw->CRTC[0] = ht-4;
	hw->CRTC[1] = hd;
	hw->CRTC[2] = hd;
	hw->CRTC[3] = (ht & 0x1F) | 0x80;
	hw->CRTC[4] = hs;
	hw->CRTC[5] = ((ht & 0x20) << 2) | (he & 0x1F);
	hw->CRTC[6] = vt & 0xFF;
	hw->CRTC[7] = ((vt & 0x100) >> 8) |
		      ((vd & 0x100) >> 7) |
		      ((vs & 0x100) >> 6) |
		      ((vd & 0x100) >> 5) |
		      0x10                |
		      ((vt & 0x200) >> 4) |
		      ((vd & 0x200) >> 3) |
		      ((vs & 0x200) >> 2);
	hw->CRTC[8] = 0x00;
	hw->CRTC[9] = ((vd & 0x200) >> 4) | 0x40;
	if (m->dblscan && !m->interlaced)
		hw->CRTC[9] |= 0x80;
	for (i = 10; i < 16; i++)
		hw->CRTC[i] = 0x00;
	hw->CRTC[16] = vs /* & 0xFF */;
	hw->CRTC[17] = (ve & 0x0F) | 0x20;
	hw->CRTC[18] = vd /* & 0xFF */;
	hw->CRTC[19] = wd /* & 0xFF */;
	hw->CRTC[20] = 0x00;
	hw->CRTC[21] = vd /* & 0xFF */;
	hw->CRTC[22] = (vt + 1) /* & 0xFF */;
	hw->CRTC[23] = 0xC3;
	hw->CRTC[24] = 0xFF;
	return 0;
};

#ifdef NEED_DAC1064

static const unsigned char MGA1064_DAC_regs[] = {
		M1064_XCURADDL, M1064_XCURADDH, M1064_XCURCTRL,
		M1064_XCURCOL0RED, M1064_XCURCOL0GREEN, M1064_XCURCOL0BLUE,
		M1064_XCURCOL1RED, M1064_XCURCOL1GREEN, M1064_XCURCOL1BLUE,
		M1064_XCURCOL2RED, M1064_XCURCOL2GREEN, M1064_XCURCOL2BLUE, 
		DAC1064_XVREFCTRL, M1064_XMULCTRL, M1064_XPIXCLKCTRL, M1064_XGENCTRL, 
		M1064_XMISCCTRL,
		M1064_XGENIOCTRL, M1064_XGENIODATA, M1064_XZOOMCTRL, M1064_XSENSETEST, 
		M1064_XCRCBITSEL,
		M1064_XCOLKEYMASKL, M1064_XCOLKEYMASKH, M1064_XCOLKEYL, M1064_XCOLKEYH };

#define POS1064_XVREFCTRL		12
#define POS1064_XMULCTRL		13
#define POS1064_XGENCTRL		15

static const unsigned char MGA1064_DAC[] = {
		0xFF, 0x1F/*Memsizedependent*/, M1064_XCURCTRL_DIS,
		0x00, 0x00, 0x00, 	/* black */
		0xFF, 0xFF, 0xFF,	/* white */
		0xFF, 0x00, 0x00,	/* red */ 
		0x00, 0,  
		M1064_XPIXCLKCTRL_PLL_UP | M1064_XPIXCLKCTRL_EN | M1064_XPIXCLKCTRL_SRC_PLL, 
		M1064_XGENCTRL_VS_0 | M1064_XGENCTRL_ALPHA_DIS | M1064_XGENCTRL_BLACK_0IRE | M1064_XGENCTRL_NO_SYNC_ON_GREEN, 
		M1064_XMISCCTRL_DAC_EN | M1064_XMISCCTRL_MFC_VGA | M1064_XMISCCTRL_DAC_8BIT | M1064_XMISCCTRL_LUT_EN,
		0x10, 0x3F, M1064_XZOOMCTRL_1, M1064_XSENSETEST_BCOMP | M1064_XSENSETEST_GCOMP | M1064_XSENSETEST_RCOMP | M1064_XSENSETEST_PDOWN, 
		0x00,
		0x00, 0x00, 0xFF, 0xFF};

static void DAC1064_setpclk(CPMINFO struct matrox_hw_state* hw, unsigned long fout) {
	unsigned int m, n, p;

	DAC1064_calcclock(PMINFO fout, ACCESS_FBINFO(max_pixel_clock), &m, &n, &p);
	hw->DACclk[0] = m;
	hw->DACclk[1] = n;
	hw->DACclk[2] = p;
}

static void DAC1064_setmclk(CPMINFO struct matrox_hw_state* hw, int oscinfo, unsigned long fmem) {
	u_int32_t mx;

	mx = hw->MXoptionReg | 0x00000004;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
	mx &= ~0x000000BB;
	if (oscinfo & DAC1064_OPT_GDIV1)
		mx |= 0x00000008;
	if (oscinfo & DAC1064_OPT_MDIV1)
		mx |= 0x00000010;
	if (oscinfo & DAC1064_OPT_RESERVED)
		mx |= 0x00000080;
	if ((oscinfo & DAC1064_OPT_SCLK_MASK) == DAC1064_OPT_SCLK_PLL) {
		/* select PCI clock until we have setup oscilator... */
		int clk;
		unsigned int m, n, p;

		/* powerup system PLL, select PCI clock */
		mx |= 0x00000020;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
		mx &= ~0x00000004;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);

		/* !!! you must not access device if MCLK is not running !!!
		   Doing so cause immediate PCI lockup :-( Maybe they should
		   generate ABORT or I/O (parity...) error and Linux should 
		   recover from this... (kill driver/process). But world is not
		   perfect... */
		/* (bit 2 of PCI_OPTION_REG must be 0... and bits 0,1 must not
		   select PLL... because of PLL can be stopped at this time) */
		DAC1064_calcclock(PMINFO fmem, ACCESS_FBINFO(max_pixel_clock), &m, &n, &p);
		outDAC1064(PMINFO DAC1064_XSYSPLLM, hw->DACclk[3] = m);
		outDAC1064(PMINFO DAC1064_XSYSPLLN, hw->DACclk[4] = n);
		outDAC1064(PMINFO DAC1064_XSYSPLLP, hw->DACclk[5] = p);
		for (clk = 65536; clk; --clk) {
			if (inDAC1064(PMINFO DAC1064_XSYSPLLSTAT) & 0x40)
				break;
		}
		if (!clk)
			printk(KERN_ERR "matroxfb: aiee, SYSPLL not locked\n");
		/* select PLL */
		mx |= 0x00000005;
	} else {
		/* select specified system clock source */
		mx |= oscinfo & DAC1064_OPT_SCLK_MASK;
	}
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
	mx &= ~0x00000004;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
	hw->MXoptionReg = mx;
}

static int DAC1064_init_1(CPMINFO struct matrox_hw_state* hw, struct my_timming* m) {
	memcpy(hw->DACreg, MGA1064_DAC, sizeof(MGA1064_DAC_regs));
	switch (ACCESS_FBINFO(curr.var.bits_per_pixel)) {
		/* case 4: not supported by MGA1064 DAC */
		case 8:	
			hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_8BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		case 16:
			if (ACCESS_FBINFO(curr.var.green.length) == 5)
				hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_15BPP_1BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			else
				hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_16BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		case 24: 
			hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_24BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		case 32:
			hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_32BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		default:
			return 1;	/* unsupported depth */
	}
	hw->DACreg[POS1064_XVREFCTRL] = ACCESS_FBINFO(features.DAC1064.xvrefctrl);
	hw->DACreg[POS1064_XGENCTRL] &= ~M1064_XGENCTRL_SYNC_ON_GREEN_MASK;
	hw->DACreg[POS1064_XGENCTRL] |= (m->sync & FB_SYNC_ON_GREEN)?M1064_XGENCTRL_SYNC_ON_GREEN:M1064_XGENCTRL_NO_SYNC_ON_GREEN;
	return 0;
}

static int DAC1064_init_2(CPMINFO struct matrox_hw_state* hw, struct my_timming* m) {
	DAC1064_setpclk(PMINFO hw, m->pixclock);
	if (ACCESS_FBINFO(curr.var.bits_per_pixel) > 16) {	/* 256 entries */
		int i;

		for (i = 0; i < 256; i++) {
			hw->DACpal[i * 3 + 0] = i;
			hw->DACpal[i * 3 + 1] = i;
			hw->DACpal[i * 3 + 2] = i;
		}
	} else if (ACCESS_FBINFO(curr.var.bits_per_pixel) > 8) {
		if (ACCESS_FBINFO(curr.var.green.length) == 5) {	/* 0..31, 128..159 */
			int i;

			for (i = 0; i < 32; i++) {
				/* with p15 == 0 */
				hw->DACpal[i * 3 + 0] = i << 3;
				hw->DACpal[i * 3 + 1] = i << 3;
				hw->DACpal[i * 3 + 2] = i << 3;
				/* with p15 == 1 */
				hw->DACpal[(i + 128) * 3 + 0] = i << 3;
				hw->DACpal[(i + 128) * 3 + 1] = i << 3;
				hw->DACpal[(i + 128) * 3 + 2] = i << 3;
			}
		} else {
			int i;

			for (i = 0; i < 64; i++) {		/* 0..63 */
				hw->DACpal[i * 3 + 0] = i << 3;
				hw->DACpal[i * 3 + 1] = i << 2;
				hw->DACpal[i * 3 + 2] = i << 3;
			}
		}
	} else {
		memset(hw->DACpal, 0, 768);
	}
	return 0;
}

static void DAC1064_restore_1(CPMINFO const struct matrox_hw_state* hw, const struct matrox_hw_state* oldhw) {
	outDAC1064(PMINFO DAC1064_XSYSPLLM, hw->DACclk[3]);
	outDAC1064(PMINFO DAC1064_XSYSPLLN, hw->DACclk[4]);
	outDAC1064(PMINFO DAC1064_XSYSPLLP, hw->DACclk[5]);
	if (!oldhw || memcmp(hw->DACreg, oldhw->DACreg, sizeof(MGA1064_DAC_regs))) {
		int i;

		for (i = 0; i < sizeof(MGA1064_DAC_regs); i++)
			outDAC1064(PMINFO MGA1064_DAC_regs[i], hw->DACreg[i]);
	}
}

static void DAC1064_restore_2(CPMINFO const struct matrox_hw_state* hw, const struct matrox_hw_state* oldhw) {
	int i;
	for (i = 0; i < 3; i++)
		outDAC1064(PMINFO M1064_XPIXPLLCM + i, hw->DACclk[i]);
	while (!(inDAC1064(PMINFO M1064_XPIXPLLSTAT) & 0x40));
#ifdef DEBUG
	dprintk(KERN_DEBUG "DAC1064regs ");
	for (i = 0; i < sizeof(MGA1064_DAC_regs); i++) {
		dprintk("R%02X=%02X ", MGA1064_DAC_regs[i], hw->DACreg[i]);
		if ((i & 0x7) == 0x7) dprintk("\n" KERN_DEBUG "continuing... ");
	}
	dprintk("\n" KERN_DEBUG "DAC1064clk ");
	for (i = 0; i < 6; i++)
		dprintk("C%02X=%02X ", i, hw->DACclk[i]);
	dprintk("\n");
#endif
}
#endif /* NEED_DAC1064 */

#ifdef CONFIG_FB_MATROX_MYSTIQUE
static int MGA1064_init(CPMINFO struct matrox_hw_state* hw, struct my_timming* m) {
	if (DAC1064_init_1(PMINFO hw, m)) return 1;
	if (vgaHWinit(PMINFO hw, m)) return 1;

	hw->MiscOutReg = 0xCB;
	if (m->sync & FB_SYNC_HOR_HIGH_ACT)
		hw->MiscOutReg &= ~0x40;
	if (m->sync & FB_SYNC_VERT_HIGH_ACT)
		hw->MiscOutReg &= ~0x80;
	if (m->sync & FB_SYNC_COMP_HIGH_ACT) /* should be only FB_SYNC_COMP */
		hw->CRTCEXT[3] |= 0x40;

	if (DAC1064_init_2(PMINFO hw, m)) return 1;
	return 0;		
}
#endif

#ifdef CONFIG_FB_MATROX_G100
static int MGAG100_init(CPMINFO struct matrox_hw_state* hw, struct my_timming* m) {
	if (DAC1064_init_1(PMINFO hw, m)) return 1;
	hw->MXoptionReg &= ~0x2000;
	if (vgaHWinit(PMINFO hw, m)) return 1;

	hw->MiscOutReg = 0xEF;
	if (m->sync & FB_SYNC_HOR_HIGH_ACT)
		hw->MiscOutReg &= ~0x40;
	if (m->sync & FB_SYNC_VERT_HIGH_ACT)
		hw->MiscOutReg &= ~0x80;
	if (m->sync & FB_SYNC_COMP_HIGH_ACT) /* should be only FB_SYNC_COMP */
		hw->CRTCEXT[3] |= 0x40;

	if (DAC1064_init_2(PMINFO hw, m)) return 1;
	return 0;		
}
#endif	/* G100 */

#ifdef CONFIG_FB_MATROX_MILLENIUM
static int Ti3026_init(CPMINFO struct matrox_hw_state* hw, struct my_timming* m) {
	u_int8_t muxctrl = isInterleave(MINFO) ? TVP3026_XMUXCTRL_MEMORY_64BIT : TVP3026_XMUXCTRL_MEMORY_32BIT;

	memcpy(hw->DACreg, MGADACbpp32, sizeof(hw->DACreg));
	switch (ACCESS_FBINFO(curr.var.bits_per_pixel)) {
		case 4:	hw->DACreg[POS3026_XLATCHCTRL] = TVP3026_XLATCHCTRL_16_1;	/* or _8_1, they are same */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR;
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_4BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV8;
			hw->DACreg[POS3026_XMISCCTRL] = TVP3026_XMISCCTRL_DAC_PUP | TVP3026_XMISCCTRL_DAC_8BIT | TVP3026_XMISCCTRL_PSEL_DIS | TVP3026_XMISCCTRL_PSEL_LOW;
			break;
		case 8: hw->DACreg[POS3026_XLATCHCTRL] = TVP3026_XLATCHCTRL_8_1;	/* or _4_1, they are same */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR;
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_8BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV4;
			hw->DACreg[POS3026_XMISCCTRL] = TVP3026_XMISCCTRL_DAC_PUP | TVP3026_XMISCCTRL_DAC_8BIT | TVP3026_XMISCCTRL_PSEL_DIS | TVP3026_XMISCCTRL_PSEL_LOW;
			break;
		case 16:
			/* XLATCHCTRL should be _4_1 / _2_1... Why is not? (_2_1 is used everytime) */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = (ACCESS_FBINFO(curr.var.green.length) == 5)? (TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_ORGB_1555 ) : (TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_RGB_565);
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_16BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV2;
			break;
		case 24:
			/* XLATCHCTRL is: for (A) use _4_3 (?_8_3 is same? TBD), for (B) it is set in setpclk */
			hw->DACreg[POS3026_XTRUECOLORCTRL] = TVP3026_XTRUECOLORCTRL_DIRECTCOLOR | TVP3026_XTRUECOLORCTRL_RGB_888;
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_32BIT;
			hw->DACreg[POS3026_XCLKCTRL] = TVP3026_XCLKCTRL_SRC_PLL | TVP3026_XCLKCTRL_DIV4;
			break;
		case 32:
			/* XLATCHCTRL should be _2_1 / _1_1... Why is not? (_2_1 is used everytime) */
			hw->DACreg[POS3026_XMUXCTRL] = muxctrl | TVP3026_XMUXCTRL_PIXEL_32BIT;
			break;
		default:
			return 1;	/* TODO: failed */
	}
	if (vgaHWinit(PMINFO hw, m)) return 1;

	/* set SYNC */
	hw->MiscOutReg = 0xCB;
	if (m->sync & FB_SYNC_HOR_HIGH_ACT)
		hw->DACreg[POS3026_XGENCTRL] |= TVP3026_XGENCTRL_HSYNC_NEG;
	if (m->sync & FB_SYNC_VERT_HIGH_ACT)
		hw->DACreg[POS3026_XGENCTRL] |= TVP3026_XGENCTRL_VSYNC_NEG;
	if (m->sync & FB_SYNC_ON_GREEN)
		hw->DACreg[POS3026_XGENCTRL] |= TVP3026_XGENCTRL_SYNC_ON_GREEN;

	/* set DELAY */
	if (ACCESS_FBINFO(video_len) < 0x400000)
		hw->CRTCEXT[3] |= 0x08;
	else if (ACCESS_FBINFO(video_len) > 0x400000)
		hw->CRTCEXT[3] |= 0x10;

	/* set HWCURSOR */
	if (m->interlaced) {
		hw->DACreg[POS3026_XCURCTRL] |= TVP3026_XCURCTRL_INTERLACED;
	}

	/* set interleaving */
	hw->MXoptionReg &= ~0x00001000;
	if (isInterleave(MINFO)) hw->MXoptionReg |= 0x00001000;

	/* set DAC */
	Ti3026_setpclk(PMINFO hw, m->pixclock, ACCESS_FBINFO(curr.final_bppShift));
	return 0;
}
#endif	/* CONFIG_FB_MATROX_MILLENIUM */

static int matroxfb_get_cmap_len(struct fb_var_screeninfo *var) {
	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
		case 4:
			return 16;	/* pseudocolor... 16 entries HW palette */
#endif
#ifdef FBCON_HAS_CFB8
		case 8:
			return 256;	/* pseudocolor... 256 entries HW palette */
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			return 16;	/* directcolor... 16 entries SW palette */
					/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
#endif
#ifdef FBCON_HAS_CFB24
		case 24:
			return 16;	/* directcolor... 16 entries SW palette */
					/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			return 16;	/* directcolor... 16 entries SW palette */
					/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
#endif
	}
	return 16;	/* return something reasonable... or panic()? */
}

static int matroxfb_decode_var(CPMINFO struct fb_var_screeninfo *var, int *visual, int *video_cmap_len) {
	unsigned int vramlen;
	unsigned int memlen;

	if ((var->xres > var->xres_virtual) || (var->yres > var->yres_virtual) || 
	    (var->xoffset > var->xres_virtual) || (var->yoffset > var->yres_virtual))
		return -EINVAL;
	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
		case 4:	 if (!ACCESS_FBINFO(capable.cfb4)) return -EINVAL;
			 break;
#endif
#ifdef FBCON_HAS_CFB8
		case 8:	 break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16: break;
#endif
#ifdef FBCON_HAS_CFB24
		case 24: break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32: break;
#endif
		default: return -EINVAL;
	}
	vramlen = ACCESS_FBINFO(video_len);
	if (vramlen > 0x800000) vramlen = 0x800000;
	var->xres_virtual = matroxfb_pitch_adjust(PMINFO var->xres_virtual, var->bits_per_pixel);
	memlen = var->xres_virtual * var->bits_per_pixel * var->yres_virtual / 8;
	if (memlen > vramlen) 
		return -EINVAL;	/* out of memory */
	/* There is hardware bug that no line can cross 4MB boundary */
	/* give up for CFB24, it is impossible to easy workaround it */
	/* for other try to do something */
	if ((memlen > 0x400000) && (var->bits_per_pixel != 24)) {
		int new_xres = var->xres_virtual;

		if (new_xres <= 512)
			new_xres = 512;
		else if (new_xres <= 1024)
			new_xres = 1024;
		else if (new_xres <= 2048)
			new_xres = 2048;
		else
			new_xres = var->xres_virtual;
		memlen = new_xres * var->bits_per_pixel / 8;
		/* if now out of memory, try shrink virtual height */
		/* but if new virtual height is smaller than visible height, return -EINVAL */
		if (var->yres_virtual * memlen > vramlen) {
			int new_yres;

			new_yres = vramlen / memlen;
			if (new_yres < var->yres)
				return -EINVAL;
			var->yres_virtual = new_yres;
		}
		var->xres_virtual = new_xres;
	}

	if (var->bits_per_pixel == 4) {
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		*visual = MX_VISUAL_PSEUDOCOLOR;
	} else if (var->bits_per_pixel <= 8) {
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		*visual = MX_VISUAL_PSEUDOCOLOR;
	} else {
		if (var->bits_per_pixel <= 16) {
			if (var->green.length == 5) {
				var->red.offset    = 10;
				var->red.length    = 5;
				var->green.offset  = 5;
				var->green.length  = 5;
				var->blue.offset   = 0;
				var->blue.length   = 5;
				var->transp.offset = 15;
				var->transp.length = 1;
			} else {
				var->red.offset    = 11;
				var->red.length    = 5;
				var->green.offset  = 5;
				var->green.length  = 6;
				var->blue.offset   = 0;
				var->blue.length   = 5;
				var->transp.offset = 0;
				var->transp.length = 0;
			}
		} else if (var->bits_per_pixel <= 24) {
			var->red.offset    = 16;
			var->red.length    = 8;
			var->green.offset  = 8;
			var->green.length  = 8;
			var->blue.offset   = 0;
			var->blue.length   = 8;
			var->transp.offset = 0;
			var->transp.length = 0;
		} else {
			var->red.offset    = 16;
			var->red.length    = 8;
			var->green.offset  = 8;
			var->green.length  = 8;
			var->blue.offset   = 0;
			var->blue.length   = 8;
			var->transp.offset = 24;
			var->transp.length = 8;
		}
		dprintk("matroxfb: truecolor: "
		       "size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
		       var->transp.length,
		       var->red.length,
		       var->green.length,
		       var->blue.length,
		       var->transp.offset,
		       var->red.offset,
		       var->green.offset,
		       var->blue.offset);
		*visual = MX_VISUAL_DIRECTCOLOR;
	}
	*video_cmap_len = matroxfb_get_cmap_len(var);
	dprintk(KERN_INFO "requested %d*%d/%dbpp (%d*%d)\n", var->xres, var->yres, var->bits_per_pixel,
				var->xres_virtual, var->yres_virtual);
	return 0;
}

#ifdef CONFIG_FB_MATROX_MILLENIUM
__initfunc(static void ti3026_setMCLK(CPMINFO struct matrox_hw_state* hw, int fout)) {
	unsigned int f_pll;
	unsigned int pclk_m, pclk_n, pclk_p;
	unsigned int mclk_m, mclk_n, mclk_p;
	unsigned int rfhcnt, mclk_ctl;
	int tmout;

	f_pll = Ti3026_calcclock(PMINFO fout, ACCESS_FBINFO(max_pixel_clock), &mclk_n, &mclk_m, &mclk_p);

	/* save pclk */
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFC);
	pclk_n = inTi3026(PMINFO TVP3026_XPIXPLLDATA);
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFD);
	pclk_m = inTi3026(PMINFO TVP3026_XPIXPLLDATA);
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFE);
	pclk_p = inTi3026(PMINFO TVP3026_XPIXPLLDATA);
	
	/* stop pclk */
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFE);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, 0x00);

	/* set pclk to new mclk */
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFC);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, mclk_n | 0xC0);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, mclk_m);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, mclk_p | 0xB0);
	
	/* wait for PLL to lock */
	for (tmout = 500000; tmout; tmout--) {
		if (inTi3026(PMINFO TVP3026_XPIXPLLDATA) & 0x40)
			break;
		udelay(10);
	};
	if (!tmout)
		printk(KERN_ERR "matroxfb: Temporary pixel PLL not locked after 5 secs\n");

	/* output pclk on mclk pin */
	mclk_ctl = inTi3026(PMINFO TVP3026_XMEMPLLCTRL);
	outTi3026(PMINFO TVP3026_XMEMPLLCTRL, mclk_ctl & 0xE7);
	outTi3026(PMINFO TVP3026_XMEMPLLCTRL, (mclk_ctl & 0xE7) | TVP3026_XMEMPLLCTRL_STROBEMKC4);

	/* stop MCLK */
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFB);
	outTi3026(PMINFO TVP3026_XMEMPLLDATA, 0x00);

	/* set mclk to new freq */
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xF3);
	outTi3026(PMINFO TVP3026_XMEMPLLDATA, mclk_n | 0xC0);
	outTi3026(PMINFO TVP3026_XMEMPLLDATA, mclk_m);
	outTi3026(PMINFO TVP3026_XMEMPLLDATA, mclk_p | 0xB0);

	/* wait for PLL to lock */
	for (tmout = 500000; tmout; tmout--) {
		if (inTi3026(PMINFO TVP3026_XMEMPLLDATA) & 0x40) 
			break;
		udelay(10);
	}
	if (!tmout)
		printk(KERN_ERR "matroxfb: Memory PLL not locked after 5 secs\n");

	f_pll = f_pll * 333 / (10000 << mclk_p);	
	if (isMilleniumII(MINFO)) {
		rfhcnt = (f_pll - 128) / 256;
		if (rfhcnt > 15)
			rfhcnt = 15;
	} else {
		rfhcnt = (f_pll - 64) / 128;
		if (rfhcnt > 15)
			rfhcnt = 0;
	}
	hw->MXoptionReg = (hw->MXoptionReg & ~0x000F0000) | (rfhcnt << 16);
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg); 

	/* output MCLK to MCLK pin */	
	outTi3026(PMINFO TVP3026_XMEMPLLCTRL, (mclk_ctl & 0xE7) | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL);
	outTi3026(PMINFO TVP3026_XMEMPLLCTRL, (mclk_ctl       ) | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL | TVP3026_XMEMPLLCTRL_STROBEMKC4);

	/* stop PCLK */
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFE);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, 0x00);

	/* restore pclk */
	outTi3026(PMINFO TVP3026_XPLLADDR, 0xFC);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, pclk_n);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, pclk_m);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, pclk_p);

	/* wait for PLL to lock */
	for (tmout = 500000; tmout; tmout--) {
		if (inTi3026(PMINFO TVP3026_XPIXPLLDATA) & 0x40)
			break;
		udelay(10);
	}
	if (!tmout)
		printk(KERN_ERR "matroxfb: Pixel PLL not locked after 5 secs\n");
}

__initfunc(static void ti3026_ramdac_init(WPMINFO struct matrox_hw_state* hw)) {
	ACCESS_FBINFO(features.pll.vco_freq_min) = 110000; 
	ACCESS_FBINFO(features.pll.ref_freq)     = 114545;
	ACCESS_FBINFO(features.pll.feed_div_min) = 2;
	ACCESS_FBINFO(features.pll.feed_div_max) = 24;
	ACCESS_FBINFO(features.pll.in_div_min)   = 2;
	ACCESS_FBINFO(features.pll.in_div_max)   = 63;
	ACCESS_FBINFO(features.pll.post_shift_max) = 3;
	ACCESS_FBINFO(max_pixel_clock) = 220000;
	ti3026_setMCLK(PMINFO hw, 60000);
}
#endif

#ifdef CONFIG_FB_MATROX_MYSTIQUE
__initfunc(static void MGA1064_ramdac_init(WPMINFO struct matrox_hw_state* hw)) {
	unsigned char rev;

	/* ACCESS_FBINFO(features.DAC1064.vco_freq_min) = 120000; */
	ACCESS_FBINFO(features.pll.vco_freq_min) = 50000; 
	ACCESS_FBINFO(features.pll.ref_freq)     = 14318;
	ACCESS_FBINFO(features.pll.feed_div_min) = 100;
	ACCESS_FBINFO(features.pll.feed_div_max) = 127;
	ACCESS_FBINFO(features.pll.in_div_min)   = 1;
	ACCESS_FBINFO(features.pll.in_div_max)   = 31;
	ACCESS_FBINFO(features.pll.post_shift_max) = 3;
	ACCESS_FBINFO(features.DAC1064.xvrefctrl) = DAC1064_XVREFCTRL_EXTERNAL;
	/* I hope */
	pci_read_config_byte(ACCESS_FBINFO(pcidev), PCI_REVISION_ID, &rev);
	if (rev < 3)
		ACCESS_FBINFO(max_pixel_clock) = 170000;
	else
		ACCESS_FBINFO(max_pixel_clock) = 220000;
	/* maybe cmdline MCLK= ?, doc says gclk=44MHz, mclk=66MHz... it was 55/83 with old values */
	DAC1064_setmclk(PMINFO hw, DAC1064_OPT_MDIV2 | DAC1064_OPT_GDIV3 | DAC1064_OPT_SCLK_PLL, 133333);
}

__initfunc(static int MGA1064_preinit(WPMINFO struct matrox_hw_state* hw)) {
	static const int vxres_mystique[] = { 512,        640, 768,  800,  832,  960, 
					     1024, 1152, 1280,      1600, 1664, 1920, 
					     2048,    0};
	/* ACCESS_FBINFO(capable.cfb4) = 0; ... preinitialized by 0 */
	ACCESS_FBINFO(capable.vxres) = vxres_mystique;
	ACCESS_FBINFO(features.accel.has_cacheflush) = 1;

	hw->MXoptionReg &= 0xC0000100;
	hw->MXoptionReg |= 0x00094E20;
	if (ACCESS_FBINFO(devflags.novga))
		hw->MXoptionReg &= ~0x00000100;
	if (ACCESS_FBINFO(devflags.nobios))
		hw->MXoptionReg &= ~0x40000000;
	if (ACCESS_FBINFO(devflags.nopciretry))
		hw->MXoptionReg |=  0x20000000;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	mga_setr(M_SEQ_INDEX, 0x01, 0x20);
	mga_outl(M_CTLWTST, 0x00000000);
	udelay(200);
	mga_outl(M_MACCESS, 0x00008000);
	udelay(100);
	mga_outl(M_MACCESS, 0x0000C000);
	return 0;
}

__initfunc(static void MGA1064_reset(WPMINFO struct matrox_hw_state* hw)) {
	MGA1064_ramdac_init(PMINFO hw);
}
#endif

#ifdef CONFIG_FB_MATROX_G100
/* BIOS environ */
static int x7AF4 = 0;    /* flags */
#if 0
static int def50 = 0;	/* reg50, & 0x0F, & 0x3000 (only 0x0000, 0x1000, 0x2000 (0x3000 disallowed and treated as 0) */
#endif

#if 0
__initfunc(static int G100_ReadFeatureConn(const struct matrox_fb_info* minfo)) {
	int ret;

	mga_outb(M_3DA_WR, 0x01);
	ret =        (mga_inb(M_3C2_RD) >> 5) & 0x03;
	mga_outb(M_3DA_WR, 0x02);
	return ret | ((mga_inb(M_3C2_RD) >> 3) & 0x0C);
}
#endif

__initfunc(static void MGAG100_progPixClock(CPMINFO int flags, int m, int n, int p)) {
	int reg;
	int selClk;
	int clk;

	outDAC1064(PMINFO M1064_XPIXCLKCTRL, inDAC1064(PMINFO M1064_XPIXCLKCTRL) | M1064_XPIXCLKCTRL_DIS);
	switch (flags & 3) {
		case 0:		reg = M1064_XPIXPLLAM; break;
		case 1:		reg = M1064_XPIXPLLBM; break;
		default:	reg = M1064_XPIXPLLCM; break;
	}
	outDAC1064(PMINFO reg++, m);
	outDAC1064(PMINFO reg++, n);
	outDAC1064(PMINFO reg, p);
	selClk = mga_inb(M_MISC_REG_READ) & ~0xC;
	/* there should be flags & 0x03 & case 0/1/else */
	/* and we should first select source and after that we should wait for PLL */
	/* and we are waiting for PLL with oscilator disabled... Is it right? */
	switch (flags & 0x03) {
		case 0x00:	break;
		case 0x01:	selClk |= 4; break;
		default:	selClk |= 0x0C; break;
	}
	mga_outb(M_MISC_REG, selClk);
	for (clk = 65536; clk; clk--)
		if (inDAC1064(PMINFO M1064_XPIXPLLSTAT) & 0x40)
			break;
	if (!clk)
		printk(KERN_ERR "matroxfb: Pixel PLL%c not locked after usual time\n", (reg-M1064_XPIXPLLAM-2)/4 + 'A');
	selClk = inDAC1064(PMINFO M1064_XPIXCLKCTRL) & ~M1064_XPIXCLKCTRL_SRC_MASK;
	switch (flags & 0x0C) {
		case 0x00:	selClk |= M1064_XPIXCLKCTRL_SRC_PCI; break;
		case 0x04:	selClk |= M1064_XPIXCLKCTRL_SRC_PLL; break;
		default:	selClk |= M1064_XPIXCLKCTRL_SRC_EXT; break;
	}
	outDAC1064(PMINFO M1064_XPIXCLKCTRL, selClk);
	outDAC1064(PMINFO M1064_XPIXCLKCTRL, inDAC1064(PMINFO M1064_XPIXCLKCTRL) & ~M1064_XPIXCLKCTRL_DIS);
}

__initfunc(static void MGAG100_setPixClock(CPMINFO int flags, int freq)) {
	unsigned int m, n, p;

	DAC1064_calcclock(PMINFO freq, ACCESS_FBINFO(max_pixel_clock), &m, &n, &p);
	MGAG100_progPixClock(PMINFO flags, m, n, p);
}

__initfunc(static int MGAG100_preinit(WPMINFO struct matrox_hw_state* hw)) {
	static const int vxres_g100[] = {  512,        640, 768,  800,  832,  960, 
                                          1024, 1152, 1280,      1600, 1664, 1920, 
                                          2048, 0};
#if 0
	u_int32_t reg50;
	u_int32_t q;
#endif

	/* ACCESS_FBINFO(capable.cfb4) = 0; ... preinitialized by 0 */
	ACCESS_FBINFO(capable.vxres) = vxres_g100;
	ACCESS_FBINFO(features.accel.has_cacheflush) = 1;
	
	hw->MXoptionReg &= 0xC0000100;
	hw->MXoptionReg |= 0x00078C20;
	if (ACCESS_FBINFO(devflags.novga))
		hw->MXoptionReg &= ~0x00000100;
	if (ACCESS_FBINFO(devflags.nobios))
		hw->MXoptionReg &= ~0x40000000;
	if (ACCESS_FBINFO(devflags.nopciretry))
		hw->MXoptionReg |=  0x20000000;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);

	mga_outl(M_CTLWTST, 0x03258A31);	/* 03258A31 is x7AF0, default is 0x02032521 */
#if 0
	hw->MXoptionReg |= 0x1000;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	pci_read_config_dword(ACCESS_FBINFO(pcidev), 0x50, &reg50);
	q = def50 & 0x3000;
	reg50 = (reg50 & ~0x3000) | q;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), 0x50, reg50);
	mga_outb(0x1C05, 0x00);
	mga_outb(0x1C05, 0x80);
	udelay(100);
	mga_outb(0x1C05, 0x40);
	mga_outb(0x1C05, 0xC0);
	pci_write_config_byte(ACCESS_FBINFO(pcidev), 0x50, def50 & 0x0F);
	{
		volatile u_int8_t* ptr;
		mga_outb(M_GRAPHICS_INDEX, 6);
		mga_outb(M_GRAPHICS_DATA, (mga_inb(M_GRAPHICS_DATA) & 3) | 4);
		mga_setr(M_EXTVGA_INDEX, 0x03, 0x81);
		mga_setr(M_EXTVGA_INDEX, 0x04, 0x00);
		ptr = ACCESS_FBINFO(video_vbase);
		ptr[0x000] = 0xAA;
		ptr[0x800] = 0x55;
		if (ptr[0x000] != 0xAA) {
			hw->MXoptionReg &= ~0x1000;
		}
	}
#endif
	hw->MXoptionReg = (hw->MXoptionReg & ~0x1F8000) | 0x78000;
#if 0
	if (!(x7AF4 & 0x10))
		hw->MXoptionReg |= 0x4000;
#endif
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	return 0;
}
	
__initfunc(static void MGAG100_reset(WPMINFO struct matrox_hw_state* hw)) {
	u_int8_t b;

	ACCESS_FBINFO(features.pll.vco_freq_min) = 50000;
	ACCESS_FBINFO(features.pll.ref_freq)     = 27000;
	ACCESS_FBINFO(features.pll.feed_div_min) = 7;
	ACCESS_FBINFO(features.pll.feed_div_max) = 127;
	ACCESS_FBINFO(features.pll.in_div_min)   = 1;
	ACCESS_FBINFO(features.pll.in_div_max)   = 31;
	ACCESS_FBINFO(features.pll.post_shift_max) = 3;
	ACCESS_FBINFO(features.DAC1064.xvrefctrl) = DAC1064_XVREFCTRL_G100_DEFAULT;
	ACCESS_FBINFO(max_pixel_clock) = 220000;

	{
#ifdef G100_BROKEN_IBM_82351
		u_int32_t d;

		find 1014/22 (IBM/82351); /* if found and bridging Matrox, do some strange stuff */
		pci_read_config_byte(ibm, PCI_SECONDARY_BUS, &b);
		if (b == ACCESS_FBINFO(pcidev)->bus->number) {
			pci_write_config_byte(ibm, PCI_COMMAND+1, 0);	/* disable back-to-back & SERR */
			pci_write_config_byte(ibm, 0x41, 0xF4);		/* ??? */
			pci_write_config_byte(ibm, PCI_IO_BASE, 0xF0);	/* ??? */
			pci_write_config_byte(ibm, PCI_IO_LIMIT, 0x00);	/* ??? */
		}
#endif
		if (x7AF4 & 8) {
			hw->MXoptionReg |= 0x40;
			pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
		}
		mga_setr(M_EXTVGA_INDEX, 0x06, 0x50);
	}
	DAC1064_setmclk(PMINFO hw, DAC1064_OPT_SCLK_PLL, 120000);
	MGAG100_setPixClock(PMINFO 4, 25175);
	MGAG100_setPixClock(PMINFO 5, 28322);
	if (x7AF4 & 0x10) {
		b = inDAC1064(PMINFO M1064_XGENIODATA) & ~1;
		outDAC1064(PMINFO M1064_XGENIODATA, b);
		b = inDAC1064(PMINFO M1064_XGENIOCTRL) | 1;
		outDAC1064(PMINFO M1064_XGENIOCTRL, b);
	}
}
#endif

static void vgaHWrestore(CPMINFO struct matrox_hw_state* hw, struct matrox_hw_state* oldhw) {
	int i;

	dprintk(KERN_INFO "MiscOutReg: %02X\n", hw->MiscOutReg);
	dprintk(KERN_INFO "SEQ regs:   ");
	for (i = 0; i < 5; i++)
		dprintk("%02X:", hw->SEQ[i]);
	dprintk("\n");
	dprintk(KERN_INFO "GDC regs:   ");
	for (i = 0; i < 9; i++)
		dprintk("%02X:", hw->GCTL[i]);
	dprintk("\n");
	dprintk(KERN_INFO "CRTC regs: ");
	for (i = 0; i < 25; i++)
		dprintk("%02X:", hw->CRTC[i]);
	dprintk("\n");
	dprintk(KERN_INFO "ATTR regs: ");
	for (i = 0; i < 21; i++)
		dprintk("%02X:", hw->ATTR[i]);
	dprintk("\n");

	mga_inb(M_ATTR_RESET);
	mga_outb(M_ATTR_INDEX, 0);
	mga_outb(M_MISC_REG, hw->MiscOutReg);
	for (i = 1; i < 5; i++)
		mga_setr(M_SEQ_INDEX, i, hw->SEQ[i]);
	mga_setr(M_CRTC_INDEX, 17, hw->CRTC[17] & 0x7F);
	for (i = 0; i < 25; i++)
		mga_setr(M_CRTC_INDEX, i, hw->CRTC[i]);
	for (i = 0; i < 9; i++)
		mga_setr(M_GRAPHICS_INDEX, i, hw->GCTL[i]);
	for (i = 0; i < 21; i++) {
		mga_inb(M_ATTR_RESET);
		mga_outb(M_ATTR_INDEX, i);
		mga_outb(M_ATTR_INDEX, hw->ATTR[i]);
	}
	mga_outb(M_PALETTE_MASK, 0xFF);
	mga_outb(M_DAC_REG, 0x00);
	for (i = 0; i < 768; i++)
		mga_outb(M_DAC_VAL, hw->DACpal[i]);
	mga_inb(M_ATTR_RESET);
	mga_outb(M_ATTR_INDEX, 0x20);
}

static int matrox_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp,
			    struct fb_info *fb_info)
{
#ifdef CONFIG_FB_MATROX_MULTIHEAD
	struct matrox_fb_info* minfo = (struct matrox_fb_info*)fb_info;
#endif
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	
	if (regno >= ACCESS_FBINFO(curr.cmap_len))
		return 1;
	
	ACCESS_FBINFO(palette[regno].red)   = red;
	ACCESS_FBINFO(palette[regno].green) = green;
	ACCESS_FBINFO(palette[regno].blue)  = blue;
	ACCESS_FBINFO(palette[regno].transp) = transp;

	red = CNVT_TOHW(red, ACCESS_FBINFO(curr.var.red.length));
	green = CNVT_TOHW(green, ACCESS_FBINFO(curr.var.green.length));
	blue = CNVT_TOHW(blue, ACCESS_FBINFO(curr.var.blue.length));
	transp = CNVT_TOHW(transp, ACCESS_FBINFO(curr.var.transp.length));

	switch (ACCESS_FBINFO(curr.var.bits_per_pixel)) {
#if defined(FBCON_HAS_CFB8) || defined(FBCON_HAS_CFB4)
#ifdef FBCON_HAS_CFB4
	case 4:
#endif
#ifdef FBCON_HAS_CFB8
	case 8:
#endif
		mga_outb(M_DAC_REG, regno);
		mga_outb(M_DAC_VAL, red);
		mga_outb(M_DAC_VAL, green);
		mga_outb(M_DAC_VAL, blue);
		break;
#endif
#ifdef FBCON_HAS_CFB16	
	case 16:
		ACCESS_FBINFO(cmap.cfb16[regno]) =
			(red << ACCESS_FBINFO(curr.var.red.offset))     | 
			(green << ACCESS_FBINFO(curr.var.green.offset)) | 
			(blue << ACCESS_FBINFO(curr.var.blue.offset))   |
			(transp << ACCESS_FBINFO(curr.var.transp.offset)); /* for 1:5:5:5 */
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		ACCESS_FBINFO(cmap.cfb24[regno]) =
			(red   << ACCESS_FBINFO(curr.var.red.offset))   |
			(green << ACCESS_FBINFO(curr.var.green.offset)) |
			(blue  << ACCESS_FBINFO(curr.var.blue.offset));
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		ACCESS_FBINFO(cmap.cfb32[regno]) =
			(red   << ACCESS_FBINFO(curr.var.red.offset))   |
			(green << ACCESS_FBINFO(curr.var.green.offset)) |
			(blue  << ACCESS_FBINFO(curr.var.blue.offset))  |
			(transp << ACCESS_FBINFO(curr.var.transp.offset));	/* 8:8:8:8 */
		break;
#endif
	}
	return 0;
}

static void do_install_cmap(WPMINFO int con)
{
	struct display* dsp;

	if (con != ACCESS_FBINFO(currcon))
		return;
	if (con >= 0)
		dsp = fb_display+con;
	else
		dsp = ACCESS_FBINFO(fbcon.disp);
	if (dsp->cmap.len)
		fb_set_cmap(&dsp->cmap, 1, matrox_setcolreg, &ACCESS_FBINFO(fbcon));
	else
		fb_set_cmap(fb_default_cmap(ACCESS_FBINFO(curr.cmap_len)),
			    1, matrox_setcolreg, &ACCESS_FBINFO(fbcon));
}

#ifdef CONFIG_FB_MATROX_MYSTIQUE
static void MGA1064_restore(WPMINFO struct matrox_hw_state* hw, struct matrox_hw_state* oldhw, struct display* p) {
	int i;

	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	mga_outb(M_IEN, 0x00);
	mga_outb(M_CACHEFLUSH, 0x00);
	DAC1064_restore_1(PMINFO hw, oldhw);
	for (i = 0; i < 6; i++)
		mga_setr(M_EXTVGA_INDEX, i, hw->CRTCEXT[i]);
	vgaHWrestore(PMINFO hw, oldhw);
	DAC1064_restore_2(PMINFO hw, oldhw);
}
#endif

#ifdef CONFIG_FB_MATROX_G100
static void MGAG100_restore(WPMINFO struct matrox_hw_state* hw, struct matrox_hw_state* oldhw, struct display* p) {
	int i;

	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	DAC1064_restore_1(PMINFO hw, oldhw);
	for (i = 0; i < 6; i++)
		mga_setr(M_EXTVGA_INDEX, i, hw->CRTCEXT[i]);
	vgaHWrestore(PMINFO hw, oldhw);
	DAC1064_restore_2(PMINFO hw, oldhw);
}
#endif

#ifdef CONFIG_FB_MATROX_MILLENIUM
static void Ti3026_restore(WPMINFO struct matrox_hw_state* hw, struct matrox_hw_state* oldhw, struct display* p) {
	int i;

	dprintk(KERN_INFO "EXTVGA regs: ");
	for (i = 0; i < 6; i++)
		dprintk("%02X:", hw->CRTCEXT[i]);
	dprintk("\n");

	for (i = 0; i < 6; i++)
		mga_setr(M_EXTVGA_INDEX, i, hw->CRTCEXT[i]);

	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);

	vgaHWrestore(PMINFO hw, oldhw);

	for (i = 0; i < 21; i++) {
		outTi3026(PMINFO DACseq[i], hw->DACreg[i]);
	}
	if (!oldhw || memcmp(hw->DACclk, oldhw->DACclk, 6)) {
		/* agrhh... setting up PLL is very slow on Millenium... */
		/* Mystique PLL is locked in few ms, but Millenium PLL lock takes about 0.15 s... */
		/* Maybe even we should call schedule() ? */

		outTi3026(PMINFO TVP3026_XCLKCTRL, hw->DACreg[POS3026_XCLKCTRL]);
		outTi3026(PMINFO TVP3026_XPLLADDR, 0x2A);
		outTi3026(PMINFO TVP3026_XLOOPPLLDATA, 0);
		outTi3026(PMINFO TVP3026_XPIXPLLDATA, 0);

		outTi3026(PMINFO TVP3026_XPLLADDR, 0x00);
		for (i = 0; i < 3; i++)
			outTi3026(PMINFO TVP3026_XPIXPLLDATA, hw->DACclk[i]);
		/* wait for PLL only if PLL clock requested (always for PowerMode) */
		if (hw->MiscOutReg & 0x08) {
			int tmout;
			outTi3026(PMINFO TVP3026_XPLLADDR, 0x3F);
			for (tmout = 500000; tmout; --tmout) {
				if (inTi3026(PMINFO TVP3026_XPIXPLLDATA) & 0x40) 
					break;
				udelay(10);
			}
			if (!tmout)
				printk(KERN_ERR "matroxfb: Pixel PLL not locked after 5 secs\n");
			else
				dprintk(KERN_INFO "PixelPLL: %d\n", 500000-tmout);
		}
		outTi3026(PMINFO TVP3026_XMEMPLLCTRL, hw->DACreg[POS3026_XMEMPLLCTRL]);
		outTi3026(PMINFO TVP3026_XPLLADDR, 0x00);
		for (i = 3; i < 6; i++)
			outTi3026(PMINFO TVP3026_XLOOPPLLDATA, hw->DACclk[i]);
		if ((hw->MiscOutReg & 0x08) && ((hw->DACclk[3] & 0xC0) == 0xC0)) {
			int tmout;

			outTi3026(PMINFO TVP3026_XPLLADDR, 0x3F);
			for (tmout = 500000; tmout; --tmout) {
				if (inTi3026(PMINFO TVP3026_XLOOPPLLDATA) & 0x40) 
					break;
				udelay(10);
			}
			if (!tmout)
				printk(KERN_ERR "matroxfb: Loop PLL not locked after 5 secs\n");
			else
				dprintk(KERN_INFO "LoopPLL: %d\n", 500000-tmout);
		}
	}
	matroxfb_ti3026_createcursor(PMINFO fontwidth(p), fontheight(p));

	dprintk(KERN_DEBUG "3026DACregs ");
	for (i = 0; i < 21; i++) {
		dprintk("R%02X=%02X ", DACseq[i], hw->DACreg[i]);
		if ((i & 0x7) == 0x7) dprintk("\n" KERN_DEBUG "continuing... ");
	}
	dprintk("\n" KERN_DEBUG "DACclk ");
	for (i = 0; i < 6; i++)
		dprintk("C%02X=%02X ", i, hw->DACclk[i]);
	dprintk("\n");
}
#endif

static int matroxfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
#define minfo ((struct matrox_fb_info*)info)
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id,"MATROX");

	fix->smem_start = (void*)ACCESS_FBINFO(video_base);
	fix->smem_len = ACCESS_FBINFO(video_len);
	fix->type = ACCESS_FBINFO(curr.video_type);
	fix->visual = ACCESS_FBINFO(curr.visual);
	fix->xpanstep = 8; /* 8 for 8bpp, 4 for 16bpp, 2 for 32bpp */
	fix->ypanstep = 1;
	fix->ywrapstep = 0;
	fix->line_length = (ACCESS_FBINFO(curr.var.xres_virtual) * ACCESS_FBINFO(curr.var.bits_per_pixel)) >> 3;
	fix->mmio_start = (void*)ACCESS_FBINFO(mmio_base);
	fix->mmio_len = ACCESS_FBINFO(mmio_len);
	fix->accel = ACCESS_FBINFO(devflags.accelerator);
	return 0;
#undef minfo
}

static int matroxfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	if(con < 0)
		*var=ACCESS_FBINFO2(info, curr.var);
	else
		*var=fb_display[con].var;
	return 0;
}

static int matroxfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
#define minfo ((struct matrox_fb_info*)info)
	int err;
	int visual;
	int cmap_len;
	struct display* display;
	int chgvar;

	if ((err = matroxfb_decode_var(PMINFO var, &visual, &cmap_len)) != 0) 
		return err;
	switch (var->activate & FB_ACTIVATE_MASK) {
		case FB_ACTIVATE_TEST:	return 0;
		case FB_ACTIVATE_NXTOPEN:	/* ?? */
		case FB_ACTIVATE_NOW:   break;	/* continue */
		default:		return -EINVAL; /* unknown */
	}
	if (con >= 0) {
		display = fb_display+con;
		chgvar = ((display->var.xres != var->xres) || 
		    (display->var.yres != var->yres) || 
                    (display->var.xres_virtual != var->xres_virtual) ||
		    (display->var.yres_virtual != var->yres_virtual) || 
		    (display->var.bits_per_pixel != var->bits_per_pixel) ||
		    memcmp(&display->var.red, &var->red, sizeof(var->red)) || 
		    memcmp(&display->var.green, &var->green, sizeof(var->green)) ||
		    memcmp(&display->var.blue, &var->blue, sizeof(var->blue)));
	} else {
		display = ACCESS_FBINFO(fbcon.disp);
		chgvar = 0;
	}
	display->var = *var;
	/* cmap */
	display->screen_base = ACCESS_FBINFO(video_vbase);
	display->visual = visual;
	display->type = FB_TYPE_PACKED_PIXELS;
	display->type_aux = 0;
	display->ypanstep = 1;
	display->ywrapstep = 0;
	display->next_line = display->line_length = (var->xres_virtual * var->bits_per_pixel) >> 3;
	display->can_soft_blank = 1;
	display->inverse = ACCESS_FBINFO(devflags.inverse);
	/* conp, fb_info, vrows, cursor_x, cursor_y, fgcol, bgcol */
	/* next_plane, fontdata, _font*, userfont */
	initMatrox(PMINFO con, display);	/* dispsw */
	/* dispsw, scrollmode, yscroll */
	/* fgshift, bgshift, charmask */
	if (chgvar && info && info->changevar)
		info->changevar(con);
	if (con == ACCESS_FBINFO(currcon)) {
		unsigned int pos;

		ACCESS_FBINFO(curr.var) = *var;
		ACCESS_FBINFO(curr.visual) = visual;
		ACCESS_FBINFO(curr.cmap_len) = cmap_len;
		ACCESS_FBINFO(curr.final_bppShift) = matroxfb_get_final_bppShift(PMINFO var->bits_per_pixel);
		if (visual == MX_VISUAL_PSEUDOCOLOR) {
			int i;

			for (i = 0; i < 16; i++) {
				int j;

				j = color_table[i];
				ACCESS_FBINFO(palette[i].red)   = default_red[j];
				ACCESS_FBINFO(palette[i].green) = default_grn[j];
				ACCESS_FBINFO(palette[i].blue)  = default_blu[j];
			}
		}

		{	struct my_timming mt;
			struct matrox_hw_state* hw;
			struct matrox_hw_state* ohw;

			var2my(var, &mt);
			hw = ACCESS_FBINFO(newhw);
			ohw = ACCESS_FBINFO(currenthw);

			/* MXoptionReg is not set from scratch */
			hw->MXoptionReg = ohw->MXoptionReg;
			/* DACclk[3]..[5] are not initialized with DAC1064 */
			memcpy(hw->DACclk, ohw->DACclk, sizeof(hw->DACclk));
			/* others are initialized by init() */

			ACCESS_FBINFO(hw_switch->init(PMINFO hw, &mt));
			pos = (var->yoffset * var->xres_virtual + var->xoffset) * ACCESS_FBINFO(curr.final_bppShift) / 32;
			if (mga_ydstorg(MINFO)) {
				if (isInterleave(MINFO))
					pos += mga_ydstorg(MINFO) >> 3;
				else
					pos += mga_ydstorg(MINFO) >> 2;
			}

			hw->CRTC[0x0D] = pos & 0xFF;
			hw->CRTC[0x0C] = (pos & 0xFF00) >> 8;
			hw->CRTCEXT[0] = (hw->CRTCEXT[0] & 0xF0) | ((pos >> 16) & 0x0F);
			ACCESS_FBINFO(hw_switch->restore(PMINFO hw, ohw, display));
			ACCESS_FBINFO(currenthw) = hw;
			ACCESS_FBINFO(newhw) = ohw;
			matrox_cfbX_init(MINFO);
			do_install_cmap(PMINFO con);
		}
	}
	return 0;
#undef minfo
}

static int matrox_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			    unsigned *blue, unsigned *transp,
			    struct fb_info *info)
{
#define minfo ((struct matrox_fb_info*)info)
	/*
	 *  Read a single color register and split it into colors/transparent.
	 *  Return != 0 for invalid regno.
	 */

	if (regno >= ACCESS_FBINFO(curr.cmap_len))
		return 1;

	*red   = ACCESS_FBINFO(palette[regno].red);
	*green = ACCESS_FBINFO(palette[regno].green);
	*blue  = ACCESS_FBINFO(palette[regno].blue);
	*transp = ACCESS_FBINFO(palette[regno].transp);
	return 0;
#undef minfo
}

static int matroxfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			     struct fb_info *info)
{
	struct display* dsp = (con < 0) ? info->disp : fb_display+con;

	if (con == ACCESS_FBINFO2(info, currcon)) /* current console? */
		return fb_get_cmap(cmap, kspc, matrox_getcolreg, info);
	else if (dsp->cmap.len) /* non default colormap? */
		fb_copy_cmap(&dsp->cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(matroxfb_get_cmap_len(&dsp->var)),
			     cmap, kspc ? 0 : 2);
	return 0;
}

static int matroxfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			     struct fb_info *info)
{
	int cmap_len;
	struct display* dsp = (con < 0) ? info->disp : (fb_display + con);

	cmap_len = matroxfb_get_cmap_len(&dsp->var);
	if (dsp->cmap.len != cmap_len) {
		int err;

		err = fb_alloc_cmap(&dsp->cmap, cmap_len, 0);
		if (err)
			return err;
	}
	if (con == ACCESS_FBINFO2(info, currcon))			/* current console? */
		return fb_set_cmap(cmap, kspc, matrox_setcolreg, info);
	else
		fb_copy_cmap(cmap, &dsp->cmap, kspc ? 0 : 1);
	return 0;
}

static int matroxfb_ioctl(struct inode *inode, struct file *file, 
			  unsigned int cmd, unsigned long arg, int con,
			  struct fb_info *info)
{
	return -EINVAL;
}

static struct fb_ops matroxfb_ops = {
	matroxfb_open,
	matroxfb_release,
	matroxfb_get_fix,
	matroxfb_get_var,
	matroxfb_set_var,
	matroxfb_get_cmap,
	matroxfb_set_cmap,
	matroxfb_pan_display,
	matroxfb_ioctl,
	NULL			/* mmap */
};

static int matroxfb_switch(int con, struct fb_info *info)
{
	struct fb_cmap* cmap;
	if (ACCESS_FBINFO2(info, currcon) >= 0) {
		/* Do we have to save the colormap? */
		cmap = &fb_display[ACCESS_FBINFO2(info, currcon)].cmap;
		dprintk(KERN_DEBUG "switch1: con = %d, cmap.len = %d\n", ACCESS_FBINFO2(info, currcon), cmap->len);

		if (cmap->len) {
			dprintk(KERN_DEBUG "switch1a: %p %p %p %p\n", cmap->red, cmap->green, cmap->blue, cmap->transp);
			fb_get_cmap(cmap, 1, matrox_getcolreg, info);
#ifdef DEBUG
			if (cmap->red) {
				dprintk(KERN_DEBUG "switch1r: %X\n", cmap->red[0]);
			}
#endif
		}
	}
	ACCESS_FBINFO2(info, currcon) = con;
	fb_display[con].var.activate = FB_ACTIVATE_NOW;
#ifdef DEBUG
	cmap = &fb_display[con].cmap;
	dprintk(KERN_DEBUG "switch2: con = %d, cmap.len = %d\n", con, cmap->len);
	dprintk(KERN_DEBUG "switch2a: %p %p %p %p\n", cmap->red, cmap->green, cmap->blue, cmap->transp);
	if (fb_display[con].cmap.red) {
		dprintk(KERN_DEBUG "switch2r: %X\n", cmap->red[0]);
	}
#endif
	matroxfb_set_var(&fb_display[con].var, con, info);
#ifdef DEBUG
	dprintk(KERN_DEBUG "switch3: con = %d, cmap.len = %d\n", con, cmap->len);
	dprintk(KERN_DEBUG "switch3a: %p %p %p %p\n", cmap->red, cmap->green, cmap->blue, cmap->transp);
	if (fb_display[con].cmap.red) {
		dprintk(KERN_DEBUG "switch3r: %X\n", cmap->red[0]);
	}
#endif
	return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */

static void matroxfb_blank(int blank, struct fb_info *info)
{
#define minfo ((struct matrox_fb_info*)info)
	int seq;
	int crtc;

	switch (blank) {
		case 1:  seq = 0x20; crtc = 0x00; break; /* works ??? */
		case 2:  seq = 0x20; crtc = 0x10; break;
		case 3:  seq = 0x20; crtc = 0x20; break;
		case 4:  seq = 0x20; crtc = 0x30; break;
		default: seq = 0x00; crtc = 0x00; break;
	}
	mga_outb(M_SEQ_INDEX, 1);
	mga_outb(M_SEQ_DATA, (mga_inb(M_SEQ_DATA) & ~0x20) | seq);
	mga_outb(M_EXTVGA_INDEX, 1);
	mga_outb(M_EXTVGA_DATA, (mga_inb(M_EXTVGA_DATA) & ~0x30) | crtc);
#undef minfo
}

#define RSResolution(X)	((X) & 0x0F)
#define RS640x400	1
#define RS640x480	2
#define RS800x600	3
#define RS1024x768	4
#define RS1280x1024	5
#define RS1600x1200	6
#define RS768x576	7
#define RS960x720	8
#define RS1152x864	9
#define RS1408x1056	10
/* B-F */
static struct { int xres, yres, left, right, upper, lower, hslen, vslen, vfreq; } timmings[] __initdata = {
	{  640,  400,  48, 16, 39,  8,  96, 2, 70 },
	{  640,  480,  48, 16, 33, 10,  96, 2, 60 },
	{  800,  600, 144, 24, 28,  8, 112, 6, 60 },
	{ 1024,  768, 160, 32, 30,  4, 128, 4, 60 },
	{ 1280, 1024, 224, 32, 32,  4, 136, 4, 60 },
	{ 1600, 1200, 272, 48, 32,  5, 152, 5, 60 },
	{  768,  576, 144, 16, 28,  6, 112, 4, 60 },
	{  960,  720, 144, 24, 28,  8, 112, 4, 60 },
	{ 1152,  864, 192, 32, 30,  4, 128, 4, 60 },
	{ 1408, 1056, 256, 40, 32,  5, 144, 5, 60 }
};

#define RSDepth(X)	(((X) >> 4) & 0x0F)
#define RS8bpp		0x1
#define RS15bpp		0x2
#define RS16bpp		0x3
#define RS32bpp		0x4
#define RS4bpp		0x5
#define RS24bpp		0x6
/* 7-F */
static struct { struct fb_bitfield red, green, blue, transp; int bits_per_pixel; } colors[] __initdata = {
	{ {  0, 8, 0}, { 0, 8, 0}, { 0, 8, 0}, {  0, 0, 0},  8 },
	{ { 10, 5, 0}, { 5, 5, 0}, { 0, 5, 0}, { 15, 1, 0}, 16 },
	{ { 11, 5, 0}, { 6, 5, 0}, { 0, 5, 0}, {  0, 0, 0}, 16 },
	{ { 16, 8, 0}, { 8, 8, 0}, { 0, 8, 0}, { 24, 8, 0}, 32 },
	{ {  0, 8, 0}, { 0, 8, 0}, { 0, 8, 0}, {  0, 0, 0},  4 },
	{ { 16, 8, 0}, { 8, 8, 0}, { 0, 8, 0}, {  0, 0, 0}, 24 }
};

#define RSCreate(X,Y)	((X) | ((Y) << 4))
static struct { int vesa; int info; } *RSptr, vesamap[] __initdata = {
/* default must be first */
#ifdef FBCON_HAS_CFB8
	{ 0x101, RSCreate(RS640x480,   RS8bpp ) },
	{ 0x100, RSCreate(RS640x400,   RS8bpp ) },
	{ 0x180, RSCreate(RS768x576,   RS8bpp ) },
	{ 0x103, RSCreate(RS800x600,   RS8bpp ) },
	{ 0x188, RSCreate(RS960x720,   RS8bpp ) },
	{ 0x105, RSCreate(RS1024x768,  RS8bpp ) },
	{ 0x190, RSCreate(RS1152x864,  RS8bpp ) },
	{ 0x107, RSCreate(RS1280x1024, RS8bpp ) },
	{ 0x198, RSCreate(RS1408x1056, RS8bpp ) },
	{ 0x11C, RSCreate(RS1600x1200, RS8bpp ) },
#endif
#ifdef FBCON_HAS_CFB4
	{ 0x102, RSCreate(RS800x600,   RS4bpp ) },
	{ 0x104, RSCreate(RS1024x768,  RS4bpp ) },
#endif
#ifdef FBCON_HAS_CFB16
	{ 0x110, RSCreate(RS640x480,   RS15bpp) },
	{ 0x181, RSCreate(RS768x576,   RS15bpp) },
	{ 0x113, RSCreate(RS800x600,   RS15bpp) },
	{ 0x189, RSCreate(RS960x720,   RS15bpp) },
	{ 0x116, RSCreate(RS1024x768,  RS15bpp) },
	{ 0x191, RSCreate(RS1152x864,  RS15bpp) },
	{ 0x119, RSCreate(RS1280x1024, RS15bpp) },
	{ 0x199, RSCreate(RS1408x1056, RS15bpp) },
	{ 0x11D, RSCreate(RS1600x1200, RS15bpp) },
	{ 0x111, RSCreate(RS640x480,   RS16bpp) },
	{ 0x182, RSCreate(RS768x576,   RS16bpp) },
	{ 0x114, RSCreate(RS800x600,   RS16bpp) },
	{ 0x18A, RSCreate(RS960x720,   RS16bpp) },
	{ 0x117, RSCreate(RS1024x768,  RS16bpp) },
	{ 0x192, RSCreate(RS1152x864,  RS16bpp) },
	{ 0x11A, RSCreate(RS1280x1024, RS16bpp) },
	{ 0x19A, RSCreate(RS1408x1056, RS16bpp) },
	{ 0x11E, RSCreate(RS1600x1200, RS16bpp) },
#endif
#ifdef FBCON_HAS_CFB24
	{ 0x1B2, RSCreate(RS640x480,   RS24bpp) },
	{ 0x184, RSCreate(RS768x576,   RS24bpp) },
	{ 0x1B5, RSCreate(RS800x600,   RS24bpp) },
	{ 0x18C, RSCreate(RS960x720,   RS24bpp) },
	{ 0x1B8, RSCreate(RS1024x768,  RS24bpp) },
	{ 0x194, RSCreate(RS1152x864,  RS24bpp) },
	{ 0x1BB, RSCreate(RS1280x1024, RS24bpp) },
	{ 0x19C, RSCreate(RS1408x1056, RS24bpp) },
	{ 0x1BF, RSCreate(RS1600x1200, RS24bpp) },
#endif
#ifdef FBCON_HAS_CFB32
	{ 0x112, RSCreate(RS640x480,   RS32bpp) },
	{ 0x183, RSCreate(RS768x576,   RS32bpp) },
	{ 0x115, RSCreate(RS800x600,   RS32bpp) },
	{ 0x18B, RSCreate(RS960x720,   RS32bpp) },
	{ 0x118, RSCreate(RS1024x768,  RS32bpp) },
	{ 0x193, RSCreate(RS1152x864,  RS32bpp) },
	{ 0x11B, RSCreate(RS1280x1024, RS32bpp) },
	{ 0x19B, RSCreate(RS1408x1056, RS32bpp) },
#endif
	{     0, 0                                 }};

/* initialized by setup, see explanation at end of file (search for MODULE_PARM_DESC) */
static unsigned int mem = 0;		/* "matrox:mem:xxxxxM" */
static int option_precise_width = 1;	/* cannot be changed, option_precise_width==0 must imply noaccel */
static int inv24 = 0;			/* "matrox:inv24" */
static int disabled = 0;		/* "matrox:disabled" */
static int noaccel = 0;			/* "matrox:noaccel" */
static int nopan = 0;			/* "matrox:nopan" */
static int no_pci_retry = 0;		/* "matrox:nopciretry" */
static int novga = 0;			/* "matrox:novga" */
static int nobios = 0;			/* "matrox:nobios" */
static int inverse = 0;			/* "matrox:inverse" */
static int hwcursor = 0;		/* "matrox:hwcursor" */
static int dev = -1;			/* "matrox:dev:xxxxx" */
static unsigned int vesa = 0x101;	/* "matrox:vesa:xxxxx" */
static unsigned int depth = 0;		/* "matrox:depth:xxxxx" */
static unsigned int xres = 0;		/* "matrox:xres:xxxxx" */
static unsigned int yres = 0;		/* "matrox:yres:xxxxx" */
static unsigned int upper = 0;		/* "matrox:upper:xxxxx" */
static unsigned int lower = 0;		/* "matrox:lower:xxxxx" */
static unsigned int vslen = 0;		/* "matrox:vslen:xxxxx" */
static unsigned int left = 0;		/* "matrox:left:xxxxx" */
static unsigned int right = 0;		/* "matrox:right:xxxxx" */
static unsigned int hslen = 0;		/* "matrox:hslen:xxxxx" */
static unsigned int pixclock = 0;	/* "matrox:pixclock:xxxxx" */
static int sync = -1;			/* "matrox:sync:xxxxx" */
static int fv = 0;			/* "matrox:fv:xxxxx" */
static int fh = 0;			/* "matrox:fh:xxxxxk" */
static int maxclk = 0;			/* "matrox:maxclk:xxxxM" */
static char fontname[64];		/* "matrox:font:xxxxx" */

#ifndef MODULE
__initfunc(void matroxfb_setup(char *options, int *ints)) {
	char *this_opt;
	
	fontname[0] = '\0';
	
	if (!options || !*options)
		return;
	
	for(this_opt=strtok(options,","); this_opt; this_opt=strtok(NULL,",")) {
		if (!*this_opt) continue;
		
		dprintk("matroxfb_setup: option %s\n", this_opt);
		
		if (! strcmp(this_opt, "inverse"))
			inverse = 1;
		else if (!strcmp(this_opt, "disabled"))
			disabled = 1;
		else if (!strcmp(this_opt, "noaccel"))
			noaccel = 1;
		else if (!strcmp(this_opt, "nopan"))
			nopan = 1;
		else if (!strcmp(this_opt, "nopciretry"))
			no_pci_retry = 1;
		else if (!strcmp(this_opt, "novga"))
			novga = 1;
		else if (!strcmp(this_opt, "nobios"))
			nobios = 1;
		else if (!strcmp(this_opt, "inv24"))
			inv24 = 1;
		else if (!strcmp(this_opt, "hwcursor"))
			hwcursor = 1;
		else if (!strncmp(this_opt, "dev:", 4))
			dev = simple_strtoul(this_opt+4, NULL, 0);
		else if (!strncmp(this_opt, "depth:", 6)) {
			switch (simple_strtoul(this_opt+6, NULL, 0)) {
				case 4: depth = RS4bpp; break;
				case 8: depth = RS8bpp; break;
				case 15:depth = RS15bpp; break;
				case 16:depth = RS16bpp; break;
				case 24:depth = RS24bpp; break;
				case 32:depth = RS32bpp; break;
				default:
					printk(KERN_ERR "matroxfb: unsupported color depth\n");
			}
		} else if (!strncmp(this_opt, "xres:", 5))
			xres = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "yres:", 5))
			yres = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "vslen:", 6))
			vslen = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "hslen:", 6))
			hslen = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "left:", 5))
			left = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "right:", 6))
			right = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "upper:", 6))
			upper = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "lower:", 6))
			lower = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "pixclock:", 9))
			pixclock = simple_strtoul(this_opt+9, NULL, 0);
		else if (!strncmp(this_opt, "sync:", 5))
			sync = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "vesa:", 5)) 
			vesa = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "font:", 5))
			strcpy(fontname, this_opt+5);
		else if (!strncmp(this_opt, "maxclk:", 7))
			maxclk = simple_strtoul(this_opt+7, NULL, 0);
		else if (!strncmp(this_opt, "fh:", 3))
			fh = simple_strtoul(this_opt+3, NULL, 0);
		else if (!strncmp(this_opt, "fv:", 3)) 
			fv = simple_strtoul(this_opt+3, NULL, 0);
		else if (!strncmp(this_opt, "mem:", 4)) 
			mem = simple_strtoul(this_opt+4, NULL, 0);
		else {
			printk(KERN_ERR "matroxfb: unknown parameter %s\n", this_opt);
		}
	}
}
#endif

__initfunc(static int matroxfb_getmemory(WPMINFO int maxSize, unsigned int* realOffset, int *realSize)) {
	volatile unsigned char* vm;
	int offs;
	int offs2;
	unsigned char store;
	unsigned char bytes[16];
	unsigned char* tmp;
	unsigned long cbase;
	unsigned long mbase;
	unsigned int clen;
	unsigned int mlen;

	vm = ACCESS_FBINFO(video_vbase);
	maxSize &= ~0x1FFFFF;	/* must be X*2MB (really it must be 2 or X*4MB) */
	/* at least 2MB */
	if (maxSize < 0x0200000) return 0;
	if (maxSize > 0x1000000) maxSize = 0x1000000;
	
	mga_outb(M_EXTVGA_INDEX, 0x03);
	mga_outb(M_EXTVGA_DATA, mga_inb(M_EXTVGA_DATA) | 0x80);

	store = vm[0x1234];
	tmp = bytes;
	for (offs = 0x100000; offs < maxSize; offs += 0x200000)
		*tmp++ = vm[offs];			
	for (offs = 0x100000; offs < maxSize; offs += 0x200000)
		vm[offs] = 0x02;
	if (ACCESS_FBINFO(features.accel.has_cacheflush))
		mga_outb(M_CACHEFLUSH, 0x00);
	else
		vm[0x1234] = 0x99;
	cbase = mbase = 0;
	clen = mlen = 0;
	for (offs = 0x100000; offs < maxSize; offs += 0x200000) {
		if (vm[offs] != 0x02) 
			continue;
		vm[offs] -= 0x02;
		if (vm[offs]) 
			continue;
		if (offs - 0x100000 == cbase + clen) {
			clen += 0x200000;
		} else {
			cbase = offs - 0x100000;
			clen = 0x200000;
		}
		if ((clen > mlen)
#ifndef MATROX_2MB_WITH_4MB_ADDON
		    && (cbase == 0)
#endif
		   ) {
			mbase = cbase;
			mlen = clen;
		}
	}
	tmp = bytes;
	for (offs2 = 0x100000; offs2 < maxSize; offs2 += 0x200000)
		vm[offs2] = *tmp++;
	vm[0x1234] = store;

	mga_outb(M_EXTVGA_INDEX, 0x03);
	mga_outb(M_EXTVGA_DATA, mga_inb(M_EXTVGA_DATA) & ~0x80);

	*realOffset = mbase;
	*realSize = mlen;
#ifdef CONFIG_FB_MATROX_MILLENIUM
	ACCESS_FBINFO(interleave) = !(!isMillenium(MINFO) || (mbase & 0x3FFFFF) || (mlen & 0x3FFFFF));
#endif
	return 1;
}

#ifdef CONFIG_FB_MATROX_MILLENIUM
__initfunc(static int Ti3026_preinit(WPMINFO struct matrox_hw_state* hw)) {
	static const int vxres_mill2[] = { 512,        640, 768,  800,  832,  960, 
					  1024, 1152, 1280,      1600, 1664, 1920, 
					  2048, 0};
	static const int vxres_mill1[] = {             640, 768,  800,        960, 
					  1024, 1152, 1280,      1600,       1920, 
					  2048, 0};
	ACCESS_FBINFO(millenium) = 1;
	ACCESS_FBINFO(milleniumII) = (ACCESS_FBINFO(pcidev)->device != PCI_DEVICE_ID_MATROX_MIL);
	ACCESS_FBINFO(capable.cfb4) = 1;
	ACCESS_FBINFO(capable.vxres) = isMilleniumII(MINFO)?vxres_mill2:vxres_mill1;
	/* preserve VGA I/O, BIOS and PPC */
	hw->MXoptionReg &= 0xC0000100;
	hw->MXoptionReg |= 0x002C0000;
	if (ACCESS_FBINFO(devflags.novga))
		hw->MXoptionReg &= ~0x00000100;
	if (ACCESS_FBINFO(devflags.nobios))
		hw->MXoptionReg &= ~0x40000000;
	if (ACCESS_FBINFO(devflags.nopciretry))
		hw->MXoptionReg |=  0x20000000;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	return 0;
}

__initfunc(static void Ti3026_reset(WPMINFO struct matrox_hw_state* hw)) {
	ACCESS_FBINFO(accel.ramdac_rev) = inTi3026(PMINFO TVP3026_XSILICONREV);

	outTi3026(PMINFO TVP3026_XCLKCTRL, TVP3026_XCLKCTRL_SRC_CLK0VGA | TVP3026_XCLKCTRL_CLKSTOPPED);
	outTi3026(PMINFO TVP3026_XTRUECOLORCTRL, TVP3026_XTRUECOLORCTRL_PSEUDOCOLOR);
	outTi3026(PMINFO TVP3026_XMUXCTRL, TVP3026_XMUXCTRL_VGA);
	
	outTi3026(PMINFO TVP3026_XPLLADDR, 0x2A);
	outTi3026(PMINFO TVP3026_XLOOPPLLDATA, 0x00);
	outTi3026(PMINFO TVP3026_XPIXPLLDATA, 0x00);

	mga_outb(M_MISC_REG, 0x67);

	outTi3026(PMINFO TVP3026_XMEMPLLCTRL, TVP3026_XMEMPLLCTRL_STROBEMKC4 | TVP3026_XMEMPLLCTRL_MCLK_MCLKPLL);

	ti3026_ramdac_init(PMINFO hw);

	dprintk(KERN_INFO "matroxfb: PCI OPTION = %08X\n", hw->MXoptionReg);

	mga_outl(M_RESET, 1);
	udelay(250);
	mga_outl(M_RESET, 0);
	udelay(250);
	/* wait for retrace, draw black square and so on... */
}
#endif

#ifdef CONFIG_FB_MATROX_MILLENIUM
static struct matrox_switch matrox_millenium = { 
	Ti3026_preinit, Ti3026_reset, Ti3026_init, Ti3026_restore 
};
#endif

#ifdef CONFIG_FB_MATROX_MYSTIQUE
static struct matrox_switch matrox_mystique = {
	MGA1064_preinit, MGA1064_reset, MGA1064_init, MGA1064_restore
};
#endif

#ifdef CONFIG_FB_MATROX_G100
static struct matrox_switch matrox_G100 = {
	MGAG100_preinit, MGAG100_reset, MGAG100_init, MGAG100_restore
};
#endif

struct video_board {
	int maxvram;
	int accelID;
	struct matrox_switch* lowlevel;
		 };
#ifdef CONFIG_FB_MATROX_MILLENIUM
static struct video_board vbMillenium __initdata	= {0x0800000,	FB_ACCEL_MATROX_MGA2064W,	&matrox_millenium};
static struct video_board vbMillenium2 __initdata	= {0x1000000,	FB_ACCEL_MATROX_MGA2164W,	&matrox_millenium};
static struct video_board vbMillenium2A __initdata	= {0x1000000,	FB_ACCEL_MATROX_MGA2164W_AGP,	&matrox_millenium};
#endif	/* CONFIG_FB_MATROX_MILLENIUM */
#ifdef CONFIG_FB_MATROX_MYSTIQUE
static struct video_board vbMystique __initdata		= {0x0800000,	FB_ACCEL_MATROX_MGA1064SG,	&matrox_mystique};
#endif	/* CONFIG_FB_MATROX_MYSTIQUE */
#ifdef CONFIG_FB_MATROX_G100
static struct video_board vbG100 __initdata		= {0x0800000,	FB_ACCEL_MATROX_MGAG100,	&matrox_G100};
static struct video_board vbG200 __initdata		= {0x1000000,	FB_ACCEL_MATROX_MGAG200,	&matrox_G100};
#endif

#define DEVF_VIDEO64BIT	0x01
#define	DEVF_SWAPS	0x02
#define DEVF_MILLENIUM	0x04
#define	DEVF_MILLENIUM2	0x08
static struct board {
	unsigned short vendor, device, rev, svid, sid;
	unsigned flags;
	struct video_board* base;
	const char* name;
		} dev_list[] __initdata = {
#ifdef CONFIG_FB_MATROX_MILLENIUM
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL,	0xFF,
		0,			0,
		DEVF_MILLENIUM,
		&vbMillenium,
		"Millenium (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL_2,	0xFF,
		0,			0,
		DEVF_MILLENIUM | DEVF_MILLENIUM2 | DEVF_SWAPS,
		&vbMillenium2,
		"Millenium II (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MIL_2_AGP,	0xFF,
		0,			0,
		DEVF_MILLENIUM | DEVF_MILLENIUM2 | DEVF_SWAPS,
		&vbMillenium2A,
		"Millenium II (AGP)"},
#endif
#ifdef CONFIG_FB_MATROX_MYSTIQUE
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MYS,	0x02,
		0,			0,
		DEVF_VIDEO64BIT,
		&vbMystique,
		"Mystique (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_MYS,	0xFF,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbMystique,
		"Mystique 220 (PCI)"},
#endif
#ifdef CONFIG_FB_MATROX_G100
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MGA_G100_PCI,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG100,
		"MGA-G100 (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100,	0xFF,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG100,
		"unknown G100 (PCI)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_UNSPECIFIED,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG100,
		"MGA-G100 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MGA_G100_AGP,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG100,
		"MGA-G100 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_AGP,	0xFF,
		PCI_SS_VENDOR_ID_SIEMENS_NIXDORF,	PCI_SS_ID_SIEMENS_MGA_G100_AGP,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG100,
		"MGA-G100 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_PRODUCTIVA_G100_AGP,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG100,
		"Productiva G100 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G100_AGP,	0xFF,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG100,
		"unknown G100 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_UNSPECIFIED,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG200,
		"MGA-G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MYSTIQUE_G200_AGP,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG200,
		"Mystique G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MILLENIUM_G200_AGP,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG200,
		"Millenium G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_MATROX,	PCI_SS_ID_MATROX_MARVEL_G200_AGP,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG200,
		"Marvel G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		PCI_SS_VENDOR_ID_SIEMENS_NIXDORF,	PCI_SS_ID_SIEMENS_MGA_G200_AGP,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG200,
		"MGA-G200 (AGP)"},
	{PCI_VENDOR_ID_MATROX,	PCI_DEVICE_ID_MATROX_G200_AGP,	0xFF,
		0,			0,
		DEVF_VIDEO64BIT | DEVF_SWAPS,
		&vbG200,
		"unknown G200 (AGP)"},
#endif
	{0,			0,				0xFF,
		0,			0,
		0,
		NULL,
		NULL}};
	
__initfunc(static int initMatrox2(WPMINFO struct display* d, struct board* b)) {
	unsigned long ctrlptr_phys = 0;
	unsigned long video_base_phys = 0;
	int memsize;
	struct matrox_hw_state* hw = ACCESS_FBINFO(currenthw);

	/* set default values... */
	vesafb_defined.accel_flags = FB_ACCELF_TEXT;

	ACCESS_FBINFO(hw_switch) = b->base->lowlevel;
	ACCESS_FBINFO(devflags.accelerator) = b->base->accelID;

	printk(KERN_INFO "matroxfb: Matrox %s detected\n", b->name);
	ACCESS_FBINFO(devflags.video64bits) = b->flags & DEVF_VIDEO64BIT;
	if (b->flags & DEVF_SWAPS) {
		ctrlptr_phys = ACCESS_FBINFO(pcidev)->base_address[1] & ~0x3FFF;
		video_base_phys = ACCESS_FBINFO(pcidev)->base_address[0] & ~0x7FFFFF;	/* aligned at 8MB (or 16 for Mill 2) */
	} else {
		ctrlptr_phys = ACCESS_FBINFO(pcidev)->base_address[0] & ~0x3FFF;
		video_base_phys = ACCESS_FBINFO(pcidev)->base_address[1] & ~0x7FFFFF;	/* aligned at 8MB */
	}
	if (!ctrlptr_phys) {
		printk(KERN_ERR "matroxfb: control registers are not available, matroxfb disabled\n");
		return -EINVAL;
	}
	if (!video_base_phys) {
		printk(KERN_ERR "matroxfb: video RAM is not available in PCI address space, matroxfb disabled\n");
		return -EINVAL;
	}
	ACCESS_FBINFO(mmio) = ioremap_nocache(ctrlptr_phys, 16384); /* do we really need ioremap_nocache? */
	if (!ACCESS_FBINFO(mmio)) {
		printk(KERN_ERR "matroxfb: cannot ioremap(%lX, 16384), matroxfb disabled\n", ctrlptr_phys);
		return -ENOMEM;
	}
	ACCESS_FBINFO(mmio_base) = ctrlptr_phys;
	ACCESS_FBINFO(mmio_len) = 16384;
	memsize = b->base->maxvram;
/* convert mem (autodetect k, M) */
	if (mem < 1024) mem *= 1024;
	if (mem < 0x00100000) mem *= 1024;

	if (mem && (mem < memsize))
		memsize = mem;
	ACCESS_FBINFO(video_vbase) = ioremap(video_base_phys, memsize);
	ACCESS_FBINFO(video_base) = video_base_phys;
	if (!ACCESS_FBINFO(video_vbase)) {
		printk(KERN_ERR "matroxfb: cannot ioremap(%lX, %d), matroxfb disabled\n", 
			video_base_phys, memsize);
		iounmap(ACCESS_FBINFO(mmio));
		return -ENOMEM;
	}
	{
		u_int32_t cmd;
		u_int32_t mga_option;

		/* Matrox MilleniumII is deactivated on bootup, but address
		   regions are assigned to board. So we have to enable it */
		pci_read_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, &mga_option);
		pci_read_config_dword(ACCESS_FBINFO(pcidev), PCI_COMMAND, &cmd);
		mga_option &= 0x7FFFFFFF; /* clear BIG_ENDIAN */
		if ((cmd & PCI_COMMAND_MEMORY) !=
			   PCI_COMMAND_MEMORY) {
			/* But if we have to enable it, we have probably to
                           disable VGA I/O and BIOS... Sure? */
			dprintk(KERN_WARNING "matroxfb: PCI BIOS did not enable device!\n");
			cmd = (cmd | PCI_COMMAND_MEMORY) & ~PCI_COMMAND_VGA_PALETTE;
			mga_option &=  0xBFFFFEFF; 
			ACCESS_FBINFO(devflags.novga) = 1;
			ACCESS_FBINFO(devflags.nobios) = 1;
		}
		mga_option |= MX_OPTION_BSWAP;
		if (pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82437, NULL)) {
			if (!(mga_option & 0x20000000) && !ACCESS_FBINFO(devflags.nopciretry)) {
				printk(KERN_WARNING "matroxfb: Disabling PCI retries due to i82437 present\n");
			}
			mga_option |= 0x20000000;
			ACCESS_FBINFO(devflags.nopciretry) = 1;
		}
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_COMMAND, cmd);
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mga_option);
		hw->MXoptionReg = mga_option;

		/* select non-DMA memory for PCI_MGA_DATA, otherwise dump of PCI cfg space can lock PCI bus */
		/* maybe preinit() candidate, but it is same... for all devices... at this time... */
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_MGA_INDEX, 0x00003C00);
	}

	if (ACCESS_FBINFO(hw_switch)->preinit(PMINFO hw)) {
		iounmap(ACCESS_FBINFO(video_vbase));
		iounmap(ACCESS_FBINFO(mmio));
		return -ENXIO;
	}

	{
		unsigned int offs;

		if (!matroxfb_getmemory(PMINFO memsize, &offs, &ACCESS_FBINFO(video_len)) || !ACCESS_FBINFO(video_len)) {
			printk(KERN_ERR "matroxfb: cannot determine memory size\n");
			iounmap(ACCESS_FBINFO(video_vbase));
			iounmap(ACCESS_FBINFO(mmio));
			return -ENOMEM;
		}
#ifdef MATROX_2MB_WITH_4MB_ADDON
#ifdef FBCON_HAS_CFB24
		{
			unsigned int end = offs + ACCESS_FBINFO(video_len);

			if (offs)
				offs = ((offs - 1) / (4096 * 3) + 1) * 4096 * 3;
			ACCESS_FBINFO(video_len) = end - offs;
		}
#endif
		mga_ydstorg(MINFO) = offs;
		video_base_phys += offs;
#endif
	}
	ACCESS_FBINFO(curr.video_type) = FB_TYPE_PACKED_PIXELS;
	ACCESS_FBINFO(currcon) = -1;
	iounmap(ACCESS_FBINFO(video_vbase));
	ACCESS_FBINFO(video_vbase) = ioremap(video_base_phys, ACCESS_FBINFO(video_len));
	ACCESS_FBINFO(video_base) = video_base_phys;
	if (!ACCESS_FBINFO(video_vbase)) {
		printk(KERN_ERR "matroxfb: cannot ioremap(%lX, %d), matroxfb disabled\n", 
			video_base_phys, ACCESS_FBINFO(video_len));
		iounmap(ACCESS_FBINFO(mmio));
		return -ENOMEM;
	}
#ifdef CONFIG_MTRR
	ACCESS_FBINFO(mtrr.vram) = mtrr_add(video_base_phys, ACCESS_FBINFO(video_len), MTRR_TYPE_WRCOMB, 1);
	ACCESS_FBINFO(mtrr.vram_valid) = 1;
	printk(KERN_INFO "matroxfb: MTRR's turned on\n");
#endif	/* CONFIG_MTRR */

/* validate params, autodetect k, M */
	if (fh < 1000) fh *= 1000;	/* 1kHz minimum */
	if (maxclk < 1000) maxclk *= 1000;	/* kHz -> Hz, MHz -> kHz */
	if (maxclk < 1000000) maxclk *= 1000;	/* kHz -> Hz, 1MHz minimum */
	vesa &= 0x1DFF;		/* mask out clearscreen, acceleration and so on */

	ACCESS_FBINFO(fbcon.monspecs.hfmin) = 0;
	ACCESS_FBINFO(fbcon.monspecs.hfmax) = fh;
	ACCESS_FBINFO(fbcon.monspecs.vfmin) = 0;
	ACCESS_FBINFO(fbcon.monspecs.vfmax) = fv;
	ACCESS_FBINFO(fbcon.monspecs.dpms) = 0;	/* TBD */

/* static settings */
	for (RSptr = vesamap; RSptr->vesa; RSptr++) {
		if (RSptr->vesa == vesa) break;
	}
	if (!RSptr->vesa) {
		printk(KERN_ERR "Invalid vesa mode 0x%04X\n", vesa);
		RSptr = vesamap;
	}
	{
		int res = RSResolution(RSptr->info)-1;
		if (!left)
			left = timmings[res].left;
		if (!xres)
			xres = timmings[res].xres;
		if (!right)
			right = timmings[res].right;
		if (!hslen)
			hslen = timmings[res].hslen;
		if (!upper)
			upper = timmings[res].upper;
		if (!yres)
			yres = timmings[res].yres;
		if (!lower)
			lower = timmings[res].lower;
		if (!vslen)
			vslen = timmings[res].vslen;
		if (!(fv||fh||maxclk||pixclock))
			fv = timmings[res].vfreq;
		if (!depth)
			depth = RSDepth(RSptr->info);
	}
	if (sync == -1) {
		sync = 0;
		if (yres < 480)
			sync |= FB_SYNC_VERT_HIGH_ACT;
	}
	if (xres < 320)
		xres = 320;
	if (xres > 2048)
		xres = 2048;
	if (yres < 200)
		yres = 200;
	if (yres > 2048)
		yres = 2048;
	{
		int tmp;

		if (fv) {
			tmp = fv * (upper + yres + lower + vslen);
			if ((tmp < fh) || (fh == 0)) fh = tmp;
		}
		if (fh) {
			tmp = fh * (left + xres + right + hslen);
			if ((tmp < maxclk) || (maxclk == 0)) maxclk = tmp;
		}
		maxclk = (maxclk + 499) / 500;
		if (maxclk) {
			tmp = (2000000000 + maxclk) / maxclk;
			if (tmp > pixclock) pixclock = tmp;
		}
	}
	vesafb_defined.red = colors[depth-1].red;
	vesafb_defined.green = colors[depth-1].green;
	vesafb_defined.blue = colors[depth-1].blue;
	vesafb_defined.bits_per_pixel = colors[depth-1].bits_per_pixel;
	if (pixclock < 2000)		/* > 500MHz */
		pixclock = 4000;	/* 250MHz */
	if (pixclock > 1000000)
		pixclock = 1000000;	/* 1MHz */
	vesafb_defined.xres = xres;
	vesafb_defined.yres = yres;
	{
		int pixel_size = vesafb_defined.bits_per_pixel;
		int avamem = ACCESS_FBINFO(video_len);

		/* displaying is possible only in first 8MB */
		if (avamem > 0x8000000)
			avamem = 0x8000000;

		vesafb_defined.xres_virtual = matroxfb_pitch_adjust(PMINFO xres, pixel_size);
		if (nopan) {
			vesafb_defined.yres_virtual = yres;
		} else {
			vesafb_defined.yres_virtual = (avamem * 8 / (pixel_size * vesafb_defined.xres_virtual)) & ~0x1F;
			if (vesafb_defined.yres_virtual < vesafb_defined.yres)
				vesafb_defined.yres_virtual = yres;
		}
	}
	vesafb_defined.xoffset = 0;
	vesafb_defined.yoffset = 0;
	vesafb_defined.grayscale = 0;
	vesafb_defined.pixclock = pixclock;
	vesafb_defined.left_margin = left;
	vesafb_defined.right_margin = right;
	vesafb_defined.hsync_len = hslen;
	vesafb_defined.upper_margin = upper;
	vesafb_defined.lower_margin = lower;
	vesafb_defined.vsync_len = vslen;
	vesafb_defined.sync = sync;
	vesafb_defined.vmode = 0;

	if (!ACCESS_FBINFO(devflags.novga))
		request_region(0x3C0, 32, "matrox");
	if (noaccel)
		vesafb_defined.accel_flags &= ~FB_ACCELF_TEXT;

	strcpy(ACCESS_FBINFO(fbcon.modename), "MATROX VGA");
	ACCESS_FBINFO(fbcon.changevar) = NULL;
	ACCESS_FBINFO(fbcon.node) = -1;
	ACCESS_FBINFO(fbcon.fbops) = &matroxfb_ops;
	ACCESS_FBINFO(fbcon.disp) = d;
	ACCESS_FBINFO(fbcon.switch_con) = &matroxfb_switch;
	ACCESS_FBINFO(fbcon.updatevar) = &matroxfb_updatevar;
	ACCESS_FBINFO(fbcon.blank) = &matroxfb_blank;
	ACCESS_FBINFO(fbcon.flags) = FBINFO_FLAG_DEFAULT;
	ACCESS_FBINFO(hw_switch->reset(PMINFO hw));

	if (matroxfb_set_var(&vesafb_defined, -2, &ACCESS_FBINFO(fbcon))) {
		printk(KERN_ERR "matroxfb: cannot set required parameters\n");
		return -EINVAL;
	}

	printk(KERN_INFO "matroxfb: %dx%dx%dbpp (virtual: %dx%d)\n",
		vesafb_defined.xres, vesafb_defined.yres, vesafb_defined.bits_per_pixel,
		vesafb_defined.xres_virtual, vesafb_defined.yres_virtual);
	printk(KERN_INFO "matroxfb: framebuffer at 0x%lX, mapped to 0x%p, size %d\n",
		ACCESS_FBINFO(video_base), ACCESS_FBINFO(video_vbase), ACCESS_FBINFO(video_len));
	
/* We do not have to set currcon to 0... register_framebuffer do it for us on first console
 * and we do not want currcon == 0 for subsequent framebuffers */

	if (register_framebuffer(&ACCESS_FBINFO(fbcon)) < 0)
		return -EINVAL;
	printk("fb%d: %s frame buffer device\n",
	       GET_FB_IDX(ACCESS_FBINFO(fbcon.node)), ACCESS_FBINFO(fbcon.modename));
	if (ACCESS_FBINFO(currcon) < 0) {
		/* there is no console on this fb... but we have to initialize hardware 
		 * until someone tells me what is proper thing to do */
		printk(KERN_INFO "fb%d: initializing hardware\n",
			GET_FB_IDX(ACCESS_FBINFO(fbcon.node)));
		matroxfb_set_var(&vesafb_defined, -1, &ACCESS_FBINFO(fbcon));
	}
	return 0;
}

static struct matrox_fb_info* fb_list = NULL;

__initfunc(static int matrox_init(void)) {
	struct pci_dev* pdev = NULL;

	if (disabled)
		return -ENXIO; 
	while ((pdev = pci_find(pdev)) != NULL) {
		struct board* b;
		u_int8_t rev;
		u_int16_t svid;
		u_int16_t sid;

		pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);
		pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &svid);
		pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &sid);
		for (b = dev_list; b->vendor; b++) {
			if ((b->vendor != pdev->vendor) || (b->device != pdev->device) || (b->rev < rev)) continue;
			if (b->svid)
				if ((b->svid != svid) || (b->sid != sid)) continue;
			if (dev <= 0) {
				struct matrox_fb_info* minfo;
				struct display* d;
				int err;

#ifdef CONFIG_FB_MATROX_MULTIHEAD
				minfo = (struct matrox_fb_info*)kmalloc(sizeof(*minfo), GFP_KERNEL);
				if (minfo) {
					d = (struct display*)kmalloc(sizeof(*d), GFP_KERNEL);
					if (d) {
#else
				minfo = &global_mxinfo;
				d = &global_disp;
#endif
				memset(MINFO, 0, sizeof(*MINFO));
				memset(d, 0, sizeof(*d));

				ACCESS_FBINFO(currenthw) = &ACCESS_FBINFO(hw1);
				ACCESS_FBINFO(newhw) = &ACCESS_FBINFO(hw2);
				ACCESS_FBINFO(pcidev) = pdev;
				/* CMDLINE */
				memcpy(ACCESS_FBINFO(fbcon.fontname), fontname, sizeof(ACCESS_FBINFO(fbcon.fontname)));
				/* DEVFLAGS */
				ACCESS_FBINFO(devflags.inverse) = inverse;
				ACCESS_FBINFO(devflags.novga) = novga;
				ACCESS_FBINFO(devflags.nobios) = nobios;
				ACCESS_FBINFO(devflags.nopciretry) = no_pci_retry;
				ACCESS_FBINFO(devflags.mga_24bpp_fix) = inv24;
				ACCESS_FBINFO(devflags.precise_width) = option_precise_width;
				ACCESS_FBINFO(devflags.hwcursor) = hwcursor;

        			err = initMatrox2(PMINFO d, b);
				if (!err) {
					ACCESS_FBINFO(next_fb) = fb_list;
					fb_list = MINFO;
#ifdef CONFIG_FB_MATROX_MULTIHEAD
					goto leave;
#else
					return 0;
#endif
				}
#ifdef CONFIG_FB_MATROX_MULTIHEAD
						kfree(d);
					}
					kfree(minfo);
				}
#endif
			}
#ifdef CONFIG_FB_MATROX_MULTIHEAD
leave:;
#endif
			if (dev == 0) return 0;
			if (dev > 0) dev--;
			break;
		}
	}
	return 0;
}

#ifndef MODULE
__initfunc(void matroxfb_init(void))
{
	matrox_init();
}

#else

MODULE_AUTHOR("(c) 1998 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Accelerated FBDev driver for Matrox Millenium/Mystique");
MODULE_PARM(mem, "i");
MODULE_PARM_DESC(mem, "Size of available memory in MB, KB or B (2,4,8,12,16MB, default=autodetect)");
MODULE_PARM(disabled, "i");
MODULE_PARM_DESC(disabled, "Disabled (0 or 1=disabled), meaningless for module (default=0)");
MODULE_PARM(noaccel, "i");
MODULE_PARM_DESC(noaccel, "Do not use accelerating engine (0 or 1=disabled) (default=0)");
MODULE_PARM(nopan, "i");
MODULE_PARM_DESC(nopan, "Disable pan on startup (0 or 1=disabled) (default=0)");
MODULE_PARM(no_pci_retry, "i");
MODULE_PARM_DESC(no_pci_retry, "PCI retries enabled (0 or 1=disabled) (default=0)");
MODULE_PARM(novga, "i");
MODULE_PARM_DESC(novga, "VGA I/O (0x3C0-0x3DF) disabled (0 or 1=disabled) (default=0)");
MODULE_PARM(nobios, "i");
MODULE_PARM_DESC(nobios, "Disables ROM BIOS (0 or 1=disabled) (default=do not change BIOS state)"); 
MODULE_PARM(inv24, "i");
MODULE_PARM_DESC(inv24, "Inverts clock polarity for 24bpp and loop frequency > 100MHz (default=do not invert polarity)");
MODULE_PARM(inverse, "i");
MODULE_PARM_DESC(inverse, "Inverse (0 or 1) (default=0)");
#ifdef CONFIG_FB_MATROX_MULTIHEAD
MODULE_PARM(dev, "i");
MODULE_PARM_DESC(dev, "Multihead support, attach to device ID (0..N) (default=all working)");
#else
MODULE_PARM(dev, "i");
MODULE_PARM_DESC(dev, "Multihead support, attach to device ID (0..N) (default=first working)");
#endif
MODULE_PARM(vesa, "i");
MODULE_PARM_DESC(vesa, "Startup videomode (0x100-0x1FF) (default=0x101)");
MODULE_PARM(xres, "i");
MODULE_PARM_DESC(xres, "Horizontal resolutioni (px), overrides xres from vesa (default=vesa)");
MODULE_PARM(yres, "i");
MODULE_PARM_DESC(yres, "Vertical resolution (scans), overrides yres from vesa (default=vesa)");
MODULE_PARM(upper, "i");
MODULE_PARM_DESC(upper, "Upper blank space (scans), overrides upper from vesa (default=vesa)");
MODULE_PARM(lower, "i");
MODULE_PARM_DESC(lower, "Lower blank space (scans), overrides lower from vesa (default=vesa)");
MODULE_PARM(vslen, "i");
MODULE_PARM_DESC(vslen, "Vertical sync length (scans), overrides lower from vesa (default=vesa)");
MODULE_PARM(left, "i");
MODULE_PARM_DESC(left, "Left blank space (px), overrides left from vesa (default=vesa)");
MODULE_PARM(right, "i");
MODULE_PARM_DESC(right, "Right blank space (px), overrides right from vesa (default=vesa)");
MODULE_PARM(hslen, "i");
MODULE_PARM_DESC(hslen, "Horizontal sync length (px), overrides hslen from vesa (default=vesa)");
MODULE_PARM(pixclock, "i");
MODULE_PARM_DESC(pixclock, "Pixelclock (ns), overrides pixclock from vesa (default=vesa)");
MODULE_PARM(sync, "i");
MODULE_PARM_DESC(sync, "Sync polarity, overrides sync from vesa (default=vesa)");
MODULE_PARM(depth, "i");
MODULE_PARM_DESC(depth, "Color depth (8,15,16,24,32) (default=vesa)");
MODULE_PARM(maxclk, "i");
MODULE_PARM_DESC(maxclk, "Startup maximal clock, 0-999MHz, 1000-999999kHz, 1000000-INF Hz");
MODULE_PARM(fh, "i");
MODULE_PARM_DESC(fh, "Startup horizontal frequency, 0-999kHz, 1000-INF Hz");
MODULE_PARM(fv, "i");
MODULE_PARM_DESC(fv, "Startup vertical frequency, 0-INF Hz\n"
"You should specify \"fv:max_monitor_vsync,fh:max_monitor_hsync,maxclk:max_monitor_dotclock\"\n");


__initfunc(int init_module(void)) {
	if (depth == 0)
		depth = 0;	/* default */
	else if (depth == 4)
		depth = RS4bpp;
	else if (depth == 8)
		depth = RS8bpp;
	else if (depth == 15)
		depth = RS15bpp;
	else if (depth == 16)
		depth = RS16bpp;
	else if (depth == 24)
		depth = RS24bpp;
	else if (depth == 32)
		depth = RS32bpp;
	else {
		printk(KERN_ERR "matroxfb: depth %d is not supported, using default\n", depth);
		depth = 0;
	}
	matrox_init();
	if (!fb_list) return -ENXIO;
	return 0;
}

void cleanup_module(void) {
	while (fb_list) {
		struct matrox_fb_info* minfo;

		minfo = fb_list;
		fb_list = fb_list->next_fb;
		unregister_framebuffer(&ACCESS_FBINFO(fbcon));
#ifdef CONFIG_MTRR
		if (ACCESS_FBINFO(mtrr.vram_valid))
			mtrr_del(ACCESS_FBINFO(mtrr.vram), ACCESS_FBINFO(video_base), ACCESS_FBINFO(video_len));
#endif
		iounmap(ACCESS_FBINFO(mmio));
		iounmap(ACCESS_FBINFO(video_vbase));
#ifdef CONFIG_FB_MATROX_MULTIHEAD
		kfree_s(ACCESS_FBINFO(fbcon.disp), sizeof(struct display));
		kfree_s(minfo, sizeof(struct matrox_fb_info));
#endif
	}
}
#endif	/* MODULE */
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
