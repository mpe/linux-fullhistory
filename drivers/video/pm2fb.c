/*
 * Permedia2 framebuffer driver.
 * Copyright (c) 1998-1999 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 * Based on linux/drivers/video/skeletonfb.c by Geert Uytterhoeven.
 * --------------------------------------------------------------------------
 * $Id: pm2fb.c,v 1.1.2.1 1999/01/12 19:53:02 geert Exp $
 * --------------------------------------------------------------------------
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
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
#include <linux/console.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "pm2fb.h"
#ifdef CONFIG_FB_PM2_CVPPC
#include "cvisionppc.h"
#endif

#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
#error	"The endianness of the target host has not been defined."
#endif

#undef PM2FB_MASTER_DEBUG
#ifdef PM2FB_MASTER_DEBUG
#define DPRINTK(a,b...)	printk("pm2fb: %s: " a, __FUNCTION__ , ## b)
#else
#define DPRINTK(a,b...)
#endif 

#define PICOS2KHZ(a) (1000000000UL/(a))
#define KHZ2PICOS(a) (1000000000UL/(a))

#ifdef CONFIG_APUS
#define MMAP(a,b)	(unsigned char* )kernel_map((unsigned long )(a), \
					b, KERNELMAP_NOCACHE_SER, NULL)
#else
#define MMAP(a,b)	ioremap(a, b)
#endif

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef __powerpc__
#define eieio()
#endif

struct pm2fb_par {
	unsigned long pixclock;		/* pixclock in KHz */
	unsigned long width;		/* width of virtual screen */
	unsigned long height;		/* height of virtual screen */
	unsigned long hsstart;		/* horiz. sync start */
	unsigned long hsend;		/* horiz. sync end */
	unsigned long hbend;		/* horiz. blank end (also gate end) */
	unsigned long htotal;		/* total width (w/ sync & blank) */
	unsigned long vsstart;		/* vert. sync start */
	unsigned long vsend;		/* vert. sync end */
	unsigned long vbend;		/* vert. blank end */
	unsigned long vtotal;		/* total height (w/ sync & blank) */
	unsigned long stride;		/* screen stride */
	unsigned long base;		/* screen base (xoffset+yoffset) */
	unsigned long depth;		/* screen depth (8, 16, 24 or 32) */
	unsigned long video;		/* video control (hsync,vsync) */
};

#define OPTF_OLD_MEM		0x00000001
#define OPTF_YPAN		0x00000002
static struct {
	char font[40];
	unsigned long flags;
	struct pm2fb_par user_mode;
} pm2fb_options;

static const struct {
	char name[16];
	struct pm2fb_par par;
} user_mode[] __initdata = {
	{"640x480-60",
		{25174,640,480,4,28,40,199,9,11,45,524,80,0,8,121}},
	{"640x480-72",
		{31199,640,480,6,16,48,207,8,10,39,518,80,0,8,121}},
	{"640x480-75",
		{31499,640,480,4,20,50,209,0,3,20,499,80,0,8,121}},
	{"640x480-90",
		{39909,640,480,8,18,48,207,24,38,53,532,80,0,8,121}},
	{"640x480-100",
		{44899,640,480,8,40,52,211,21,33,51,530,80,0,8,121}},
	{"800x600-56",
		{35999,800,600,6,24,56,255,0,2,25,624,100,0,8,41}},
	{"800x600-60",
		{40000,800,600,10,42,64,263,0,4,28,627,100,0,8,41}},
	{"800x600-70",
		{44899,800,600,6,42,52,251,8,20,36,635,100,0,8,105}},
	{"800x600-72",
		{50000,800,600,14,44,60,259,36,42,66,665,100,0,8,41}},
	{"800x600-75",
		{49497,800,600,4,24,64,263,0,3,25,624,100,0,8,41}},
	{"800x600-90",
		{56637,800,600,2,18,48,247,7,18,35,634,100,0,8,41}},
	{"800x600-100",
		{67499,800,600,0,16,70,269,6,10,25,624,100,0,8,41}},
	{"1024x768-60",
		{64998,1024,768,6,40,80,335,2,8,38,805,128,0,8,121}},
	{"1024x768-70",
		{74996,1024,768,6,40,76,331,2,8,38,805,128,0,8,121}},
	{"1024x768-72",
		{74996,1024,768,6,40,66,321,2,8,38,805,128,0,8,121}},
	{"1024x768-75",
		{78932,1024,768,4,28,72,327,0,3,32,799,128,0,8,41}},
	{"1024x768-90",
		{100000,1024,768,0,24,72,327,20,35,77,844,128,0,8,121}},
	{"1024x768-100",
		{109998,1024,768,0,22,92,347,0,7,24,791,128,0,8,121}},
	{"1024x768-illo",
		{120322,1024,768,12,48,120,375,3,7,32,799,128,0,8,41}},
	{"1152x864-60",
		{80000,1152,864,16,44,76,363,5,10,52,915,144,0,8,41}},
	{"1152x864-70",
		{100000,1152,864,10,48,90,377,12,23,81,944,144,0,8,41}},
	{"1152x864-75",
		{109998,1152,864,6,42,78,365,44,52,138,1001,144,0,8,41}},
	{"1152x864-80",
		{109998,1152,864,4,32,72,359,29,36,94,957,144,0,8,41}},
	{"1280x1024-60",
		{107991,1280,1024,12,40,102,421,0,3,42,1065,160,0,8,41}},
	{"1280x1024-70",
		{125992,1280,1024,20,48,102,421,0,5,42,1065,160,0,8,41}},
	{"1280x1024-74",
		{134989,1280,1024,8,44,108,427,0,29,40,1063,160,0,8,41}},
	{"1280x1024-75",
		{134989,1280,1024,4,40,102,421,0,3,42,1065,160,0,8,41}},
	{"1600x1200-60",
		{155981,1600,1200,8,48,112,511,9,17,70,1269,200,0,8,121}},
	{"1600x1200-66",
		{171998,1600,1200,10,44,120,519,2,5,53,1252,200,0,8,121}},
	{"1600x1200-76",
		{197980,1600,1200,10,44,120,519,2,7,50,1249,200,0,8,121}},
	{"\0", },
};

static const char permedia2_name[16]="Permedia2";

static struct pm2fb_info {
	struct fb_info_gen gen;
	int board;			/* Permedia2 board index (see
					   board_table[] below) */
	struct {
		unsigned char* fb_base;	/* framebuffer memory base */
		unsigned long  fb_size;	/* framebuffer memory size */
		unsigned char* rg_base;	/* register memory base */
		unsigned char* p_fb;	/* physical address of frame buffer */
		unsigned char* v_fb;	/* virtual address of frame buffer */
		unsigned char* p_regs;	/* physical address of registers
					   region, must be rg_base or
					   rg_base+PM2_REGS_SIZE depending on
					   the host endianness */
		unsigned char* v_regs;	/* virtual address of p_regs */
	} regions;
	union {				/* here, the per-board par structs */
#ifdef CONFIG_FB_PM2_CVPPC
		struct cvppc_par cvppc;	/* CVisionPPC data */
#endif
	} board_par;
	struct pm2fb_par current_par;	/* displayed screen */
	int current_par_valid;
	unsigned long memclock;		/* memclock (set by the per-board
					   		init routine) */
	struct display disp;
	struct {
		u8 transp;
		u8 red;
		u8 green;
		u8 blue;
	} palette[256];
	union {
#ifdef FBCON_HAS_CFB16
		u16 cmap16[16];
#endif
#ifdef FBCON_HAS_CFB24
		u32 cmap24[16];
#endif
#ifdef FBCON_HAS_CFB32
		u32 cmap32[16];
#endif
	} cmap;
} fb_info;

#ifdef CONFIG_FB_PM2_CVPPC
static int cvppc_detect(struct pm2fb_info*);
static void cvppc_init(struct pm2fb_info*);
#endif

/*
 * Table of the supported Permedia2 based boards.
 * Three hooks are defined for each board:
 * detect(): should return 1 if the related board has been detected, 0
 *           otherwise. It should also fill the fields 'regions.fb_base',
 *           'regions.fb_size', 'regions.rg_base' and 'memclock' in the
 *           passed pm2fb_info structure.
 * init(): called immediately after the reset of the Permedia2 chip.
 *         It should reset the memory controller if needed (the MClk
 *         is set shortly afterwards by the caller).
 * cleanup(): called after the driver has been unregistered.
 *
 * the init and cleanup pointers can be NULL.
 */
static const struct {
	int (*detect)(struct pm2fb_info*);
	void (*init)(struct pm2fb_info*);
	void (*cleanup)(struct pm2fb_info*);
	char name[32];
} board_table[] = {
#ifdef CONFIG_FB_PM2_CVPPC
	{ cvppc_detect, cvppc_init, NULL, "CVisionPPC/BVisionPPC" },
#endif
	{ NULL, }
};

/*
 * partial products for the supported horizontal resolutions.
 */
#define PACKPP(p0,p1,p2)	(((p2)<<6)|((p1)<<3)|(p0))
static const struct {
	unsigned short width;
	unsigned short pp;
} pp_table[] = {
	{ 32,	PACKPP(1, 0, 0) }, { 64,	PACKPP(1, 1, 0) },
	{ 96,	PACKPP(1, 1, 1) }, { 128,	PACKPP(2, 1, 1) },
	{ 160,	PACKPP(2, 2, 1) }, { 192,	PACKPP(2, 2, 2) },
	{ 224,	PACKPP(3, 2, 1) }, { 256,	PACKPP(3, 2, 2) },
	{ 288,	PACKPP(3, 3, 1) }, { 320,	PACKPP(3, 3, 2) },
	{ 384,	PACKPP(3, 3, 3) }, { 416,	PACKPP(4, 3, 1) },
	{ 448,	PACKPP(4, 3, 2) }, { 512,	PACKPP(4, 3, 3) },
	{ 544,	PACKPP(4, 4, 1) }, { 576,	PACKPP(4, 4, 2) },
	{ 640,	PACKPP(4, 4, 3) }, { 768,	PACKPP(4, 4, 4) },
	{ 800,	PACKPP(5, 4, 1) }, { 832,	PACKPP(5, 4, 2) },
	{ 896,	PACKPP(5, 4, 3) }, { 1024,	PACKPP(5, 4, 4) },
	{ 1056,	PACKPP(5, 5, 1) }, { 1088,	PACKPP(5, 5, 2) },
	{ 1152,	PACKPP(5, 5, 3) }, { 1280,	PACKPP(5, 5, 4) },
	{ 1536,	PACKPP(5, 5, 5) }, { 1568,	PACKPP(6, 5, 1) },
	{ 1600,	PACKPP(6, 5, 2) }, { 1664,	PACKPP(6, 5, 3) },
	{ 1792,	PACKPP(6, 5, 4) }, { 2048,	PACKPP(6, 5, 5) },
	{ 0,	0 } };

static void pm2fb_detect(void);
static int pm2fb_encode_fix(struct fb_fix_screeninfo* fix,
				const void* par, struct fb_info_gen* info);
static int pm2fb_decode_var(const struct fb_var_screeninfo* var,
					void* par, struct fb_info_gen* info);
static int pm2fb_encode_var(struct fb_var_screeninfo* var,
				const void* par, struct fb_info_gen* info);
static void pm2fb_get_par(void* par, struct fb_info_gen* info);
static void pm2fb_set_par(const void* par, struct fb_info_gen* info);
static int pm2fb_getcolreg(unsigned regno,
			unsigned* red, unsigned* green, unsigned* blue,
				unsigned* transp, struct fb_info* info);
static int pm2fb_setcolreg(unsigned regno,
			unsigned red, unsigned green, unsigned blue,
				unsigned transp, struct fb_info* info);
static int pm2fb_blank(int blank_mode, struct fb_info_gen* info);
static int pm2fb_pan_display(const struct fb_var_screeninfo* var,
					struct fb_info_gen* info);
static void pm2fb_dispsw(const void* par, struct display* disp,
						struct fb_info_gen* info);

static struct fbgen_hwswitch pm2fb_hwswitch={
	pm2fb_detect, pm2fb_encode_fix, pm2fb_decode_var,
	pm2fb_encode_var, pm2fb_get_par, pm2fb_set_par,
	pm2fb_getcolreg, pm2fb_setcolreg, pm2fb_pan_display,
	pm2fb_blank, pm2fb_dispsw
};

static int pm2fb_open(struct fb_info* info, int user);
static int pm2fb_release(struct fb_info* info, int user);

static struct fb_ops pm2fb_ops={
	pm2fb_open, pm2fb_release, fbgen_get_fix, fbgen_get_var,
	fbgen_set_var, fbgen_get_cmap, fbgen_set_cmap, fbgen_pan_display,
	NULL /* fb_ioctl() */, NULL /* fb_mmap() */
};

/***************************************************************************
 * Begin of Permedia2 specific functions
 ***************************************************************************/

inline static unsigned long RD32(unsigned char* base, long off) {

	return *((volatile unsigned long* )(base+off));
}

inline static void WR32(unsigned char* base, long off, unsigned long v) {

	*((volatile unsigned long* )(base+off))=v;
}

inline static unsigned long pm2_RD(struct pm2fb_info* p, long off) {

	return RD32(p->regions.v_regs, off);
}

inline static void pm2_WR(struct pm2fb_info* p, long off, unsigned long v) {

	WR32(p->regions.v_regs, off, v);
}

inline static unsigned long pm2_RDAC_RD(struct pm2fb_info* p, long idx) {

	pm2_WR(p, PM2R_RD_PALETTE_WRITE_ADDRESS, idx);
	eieio();
	return pm2_RD(p, PM2R_RD_INDEXED_DATA);
}

inline static void pm2_RDAC_WR(struct pm2fb_info* p, long idx,
						unsigned long v) {

	pm2_WR(p, PM2R_RD_PALETTE_WRITE_ADDRESS, idx);
	eieio();
	pm2_WR(p, PM2R_RD_INDEXED_DATA, v);
}

#ifdef CONFIG_FB_PM2_FIFO_DISCONNECT
#define WAIT_FIFO(p,a)
#else
inline static void WAIT_FIFO(struct pm2fb_info* p, unsigned long a) {

	while(pm2_RD(p, PM2R_IN_FIFO_SPACE)<a);
	eieio();
}
#endif

static unsigned long partprod(unsigned long xres) {
	int i;

	for (i=0; pp_table[i].width && pp_table[i].width!=xres; i++);
	if (!pp_table[i].width)
		DPRINTK("invalid width %lu\n", xres);
	return pp_table[i].pp;
}

static unsigned long to3264(unsigned long timing, int bpp, int is64) {

	switch (bpp) {
		case 8:
			timing=timing>>(2+is64);
			break;
		case 16:
			timing=timing>>(1+is64);
			break;
		case 24:
			timing=(timing*3)>>(2+is64);
			break;
		case 32:
			if (is64)
				timing=timing>>1;
			break;
	}
	return timing;
}

static unsigned long from3264(unsigned long timing, int bpp, int is64) {

	switch (bpp) {
		case 8:
			timing=timing<<(2+is64);
			break;
		case 16:
			timing=timing<<(1+is64);
			break;
		case 24:
			timing=(timing<<(2+is64))/3;
			break;
		case 32:
			if (is64)
				timing=timing<<1;
			break;
	}
	return timing;
}

static void mnp(unsigned long clk, unsigned char* mm, unsigned char* nn,
							unsigned char* pp) {
	unsigned char m;
	unsigned char n;
	unsigned char p;
	unsigned long f;
	long current;
	long delta=100000;

	*mm=*nn=*pp=0;
	for (n=2; n<15; n++) {
		for (m=2; m; m++) {
			f=PM2_REFERENCE_CLOCK*m/n;
			if (f>=150000 && f<=300000) {
				for (p=0; p<5; p++, f>>=1) {
					current=clk>f?clk-f:f-clk;
					if (current<delta) {
						delta=current;
						*mm=m;
						*nn=n;
						*pp=p;
					}
				}
			}
		}
	}
}

static void wait_pm2(struct pm2fb_info* i) {

	WAIT_FIFO(i, 1);
	pm2_WR(i, PM2R_SYNC, 0);
	eieio();
	do {
		while (pm2_RD(i, PM2R_OUT_FIFO_WORDS)==0);
		eieio();
	} while (pm2_RD(i, PM2R_OUT_FIFO)!=PM2TAG(PM2R_SYNC));
}

static void set_memclock(struct pm2fb_info* info, unsigned long clk) {
	int i;
	unsigned char m, n, p;

	mnp(clk, &m, &n, &p);
	WAIT_FIFO(info, 5);
	pm2_RDAC_WR(info, PM2I_RD_MEMORY_CLOCK_3, 6);
	eieio();
	pm2_RDAC_WR(info, PM2I_RD_MEMORY_CLOCK_1, m);
	pm2_RDAC_WR(info, PM2I_RD_MEMORY_CLOCK_2, n);
	eieio();
	pm2_RDAC_WR(info, PM2I_RD_MEMORY_CLOCK_3, 8|p);
	eieio();
	pm2_RDAC_RD(info, PM2I_RD_MEMORY_CLOCK_STATUS);
	eieio();
	for (i=256; i &&
		!(pm2_RD(info, PM2R_RD_INDEXED_DATA)&PM2F_PLL_LOCKED); i--);
}

static void set_pixclock(struct pm2fb_info* info, unsigned long clk) {
	int i;
	unsigned char m, n, p;

	mnp(clk, &m, &n, &p);
	WAIT_FIFO(info, 5);
	pm2_RDAC_WR(info, PM2I_RD_PIXEL_CLOCK_A3, 0);
	eieio();
	pm2_RDAC_WR(info, PM2I_RD_PIXEL_CLOCK_A1, m);
	pm2_RDAC_WR(info, PM2I_RD_PIXEL_CLOCK_A2, n);
	eieio();
	pm2_RDAC_WR(info, PM2I_RD_PIXEL_CLOCK_A3, 8|p);
	eieio();
	pm2_RDAC_RD(info, PM2I_RD_PIXEL_CLOCK_STATUS);
	eieio();
	for (i=256; i &&
		!(pm2_RD(info, PM2R_RD_INDEXED_DATA)&PM2F_PLL_LOCKED); i--);
}

static void set_color(struct pm2fb_info* p, unsigned char regno,
			unsigned char r, unsigned char g, unsigned char b) {

	WAIT_FIFO(p, 4);
	eieio();
	pm2_WR(p, PM2R_RD_PALETTE_WRITE_ADDRESS, regno);
	eieio();
	pm2_WR(p, PM2R_RD_PALETTE_DATA, r);
	eieio();
	pm2_WR(p, PM2R_RD_PALETTE_DATA, g);
	eieio();
	pm2_WR(p, PM2R_RD_PALETTE_DATA, b);
}

static void set_aperture(struct pm2fb_info* i, struct pm2fb_par* p) {

	WAIT_FIFO(i, 2);
#ifdef __LITTLE_ENDIAN
	pm2_WR(i, PM2R_APERTURE_ONE, 0);	/* FIXME */
	pm2_WR(i, PM2R_APERTURE_TWO, 0);
#else
	switch (p->depth) {
		case 8:
		case 24:
			pm2_WR(i, PM2R_APERTURE_ONE, 0);
			pm2_WR(i, PM2R_APERTURE_TWO, 1);
			break;
		case 16:
			pm2_WR(i, PM2R_APERTURE_ONE, 2);
			pm2_WR(i, PM2R_APERTURE_TWO, 1);
			break;
		case 32:
			pm2_WR(i, PM2R_APERTURE_ONE, 1);
			pm2_WR(i, PM2R_APERTURE_TWO, 1);
			break;
	}
#endif
}

static void set_screen(struct pm2fb_info* i, struct pm2fb_par* p) {
	unsigned long clrmode=0;
	unsigned long txtmap=0;
	unsigned long xres;

	xres=(p->width+31)&~31;
	set_aperture(i, p);
	WAIT_FIFO(i, 22);
	pm2_RDAC_WR(i, PM2I_RD_COLOR_KEY_CONTROL, p->depth==8?0:
						PM2F_COLOR_KEY_TEST_OFF);
	switch (p->depth) {
		case 8:
			pm2_WR(i, PM2R_FB_READ_PIXEL, 0);
			break;
		case 16:
			pm2_WR(i, PM2R_FB_READ_PIXEL, 1);
			clrmode=PM2F_RD_TRUECOLOR|0x06;
			txtmap=PM2F_TEXTEL_SIZE_16;
			break;
		case 32:
			pm2_WR(i, PM2R_FB_READ_PIXEL, 2);
			clrmode=PM2F_RD_TRUECOLOR|0x08;
			txtmap=PM2F_TEXTEL_SIZE_32;
			break;
		case 24:
			pm2_WR(i, PM2R_FB_READ_PIXEL, 4);
			clrmode=PM2F_RD_TRUECOLOR|0x09;
			txtmap=PM2F_TEXTEL_SIZE_24;
			break;
	}
	pm2_WR(i, PM2R_SCREEN_SIZE, (p->height<<16)|p->width);
	pm2_WR(i, PM2R_SCISSOR_MODE, PM2F_SCREEN_SCISSOR_ENABLE);
	pm2_WR(i, PM2R_FB_WRITE_MODE, PM2F_FB_WRITE_ENABLE);
	pm2_WR(i, PM2R_FB_READ_MODE, partprod(xres));
	pm2_WR(i, PM2R_LB_READ_MODE, partprod(xres));
	pm2_WR(i, PM2R_TEXTURE_MAP_FORMAT, txtmap|partprod(xres));
	pm2_WR(i, PM2R_H_TOTAL, p->htotal);
	pm2_WR(i, PM2R_HS_START, p->hsstart);
	pm2_WR(i, PM2R_HS_END, p->hsend);
	pm2_WR(i, PM2R_HG_END, p->hbend);
	pm2_WR(i, PM2R_HB_END, p->hbend);
	pm2_WR(i, PM2R_V_TOTAL, p->vtotal);
	pm2_WR(i, PM2R_VS_START, p->vsstart);
	pm2_WR(i, PM2R_VS_END, p->vsend);
	pm2_WR(i, PM2R_VB_END, p->vbend);
	pm2_WR(i, PM2R_SCREEN_STRIDE, p->stride);
	eieio();
	pm2_WR(i, PM2R_SCREEN_BASE, p->base);
	pm2_RDAC_WR(i, PM2I_RD_COLOR_MODE, PM2F_RD_COLOR_MODE_RGB|
						PM2F_RD_GUI_ACTIVE|clrmode);
	pm2_WR(i, PM2R_VIDEO_CONTROL, p->video);
	set_pixclock(i, p->pixclock);
};

/***************************************************************************
 * Begin of generic initialization functions
 ***************************************************************************/

static void pm2fb_reset(struct pm2fb_info* p) {

	pm2_WR(p, PM2R_RESET_STATUS, 0);
	eieio();
	while (pm2_RD(p, PM2R_RESET_STATUS)&PM2F_BEING_RESET);
	eieio();
#ifdef CONFIG_FB_PM2_FIFO_DISCONNECT
	DPRINTK("FIFO disconnect enabled\n");
	pm2_WR(p, PM2R_FIFO_DISCON, 1);
#endif
	eieio();
	if (board_table[p->board].init)
		board_table[p->board].init(p);
	WAIT_FIFO(p, 48);
	pm2_WR(p, PM2R_CHIP_CONFIG, pm2_RD(p, PM2R_CHIP_CONFIG)&
					~(PM2F_VGA_ENABLE|PM2F_VGA_FIXED));
	pm2_WR(p, PM2R_BYPASS_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FRAMEBUFFER_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FIFO_CONTROL, 0);
	pm2_WR(p, PM2R_FILTER_MODE, PM2F_SYNCHRONIZATION);
	pm2_WR(p, PM2R_APERTURE_ONE, 0);
	pm2_WR(p, PM2R_APERTURE_TWO, 0);
	pm2_WR(p, PM2R_LB_READ_FORMAT, 0);
	pm2_WR(p, PM2R_LB_WRITE_FORMAT, 0); 
	pm2_WR(p, PM2R_LB_READ_MODE, 0);
	pm2_WR(p, PM2R_LB_SOURCE_OFFSET, 0);
	pm2_WR(p, PM2R_FB_SOURCE_OFFSET, 0);
	pm2_WR(p, PM2R_FB_PIXEL_OFFSET, 0);
	pm2_WR(p, PM2R_WINDOW_ORIGIN, 0);
	pm2_WR(p, PM2R_FB_WINDOW_BASE, 0);
	pm2_WR(p, PM2R_LB_WINDOW_BASE, 0);
	pm2_WR(p, PM2R_FB_SOFT_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FB_HARD_WRITE_MASK, ~(0L));
	pm2_WR(p, PM2R_FB_READ_PIXEL, 0);
	pm2_WR(p, PM2R_DITHER_MODE, 0);
	pm2_WR(p, PM2R_AREA_STIPPLE_MODE, 0);
	pm2_WR(p, PM2R_DEPTH_MODE, 0);
	pm2_WR(p, PM2R_STENCIL_MODE, 0);
	pm2_WR(p, PM2R_TEXTURE_ADDRESS_MODE, 0);
	pm2_WR(p, PM2R_TEXTURE_READ_MODE, 0);
	pm2_WR(p, PM2R_TEXEL_LUT_MODE, 0);
	pm2_WR(p, PM2R_YUV_MODE, 0);
	pm2_WR(p, PM2R_COLOR_DDA_MODE, 0);
	pm2_WR(p, PM2R_TEXTURE_COLOR_MODE, 0);
	pm2_WR(p, PM2R_FOG_MODE, 0);
	pm2_WR(p, PM2R_ALPHA_BLEND_MODE, 0);
	pm2_WR(p, PM2R_LOGICAL_OP_MODE, 0);
	pm2_WR(p, PM2R_STATISTICS_MODE, 0);
	pm2_WR(p, PM2R_SCISSOR_MODE, 0);
	pm2_RDAC_WR(p, PM2I_RD_CURSOR_CONTROL, 0);
	pm2_RDAC_WR(p, PM2I_RD_MISC_CONTROL, PM2F_RD_PALETTE_WIDTH_8);
	pm2_RDAC_WR(p, PM2I_RD_COLOR_KEY_CONTROL, 0);
	pm2_RDAC_WR(p, PM2I_RD_OVERLAY_KEY, 0);
	pm2_RDAC_WR(p, PM2I_RD_RED_KEY, 0);
	pm2_RDAC_WR(p, PM2I_RD_GREEN_KEY, 0);
	pm2_RDAC_WR(p, PM2I_RD_BLUE_KEY, 0);
	eieio();
	set_memclock(p, p->memclock);
}

__initfunc(static int pm2fb_conf(struct pm2fb_info* p)) {

	for (p->board=0; board_table[p->board].detect &&
			!(board_table[p->board].detect(p)); p->board++);
	if (!board_table[p->board].detect) {
		DPRINTK("no board found.\n");
		return 0;
	}
	DPRINTK("found board: %s\n", board_table[p->board].name);
	p->regions.p_fb=p->regions.fb_base;
	p->regions.v_fb=MMAP(p->regions.p_fb, p->regions.fb_size);
#ifdef __LITTLE_ENDIAN
	p->regions.p_regs=p->regions.rg_base;
#else
	p->regions.p_regs=p->regions.rg_base+PM2_REGS_SIZE;
#endif
	p->regions.v_regs=MMAP(p->regions.p_regs, PM2_REGS_SIZE);
	return 1;
}

/***************************************************************************
 * Begin of per-board initialization functions
 ***************************************************************************/

#ifdef CONFIG_FB_PM2_CVPPC
static int cvppc_PCI_init(struct cvppc_par* p) {
	extern unsigned long powerup_PCI_present;

	if (!powerup_PCI_present) {
		DPRINTK("no PCI bridge detected\n");
		return 0;
	}
	if (!(p->pci_config=MMAP(CVPPC_PCI_CONFIG, 256))) {
		DPRINTK("unable to map PCI config region\n");
		return 0;
	}
	if (RD32(p->pci_config, PCI_VENDOR_ID)!=
			((PCI_DEVICE_ID_TI_TVP4020<<16)|PCI_VENDOR_ID_TI)) {
		DPRINTK("bad vendorID/deviceID\n");
		return 0;
	}
	if (!(p->pci_bridge=MMAP(CSPPC_PCI_BRIDGE, 256))) {
		DPRINTK("unable to map PCI bridge\n");
		return 0;
	}
	WR32(p->pci_bridge, CSPPC_BRIDGE_ENDIAN, CSPPCF_BRIDGE_BIG_ENDIAN);
	eieio();
	if (pm2fb_options.flags & OPTF_OLD_MEM)
		WR32(p->pci_config, PCI_CACHE_LINE_SIZE, 0xff00);
	WR32(p->pci_config, PCI_BASE_ADDRESS_0, CVPPC_REGS_REGION);
	WR32(p->pci_config, PCI_BASE_ADDRESS_1, CVPPC_FB_APERTURE_ONE);
	WR32(p->pci_config, PCI_BASE_ADDRESS_2, CVPPC_FB_APERTURE_TWO);
	WR32(p->pci_config, PCI_ROM_ADDRESS, CVPPC_ROM_ADDRESS);
	eieio();
	WR32(p->pci_config, PCI_COMMAND, 0xef000000 |
						PCI_COMMAND_IO |
						PCI_COMMAND_MEMORY |
						PCI_COMMAND_MASTER);
	return 1;
}

static int cvppc_detect(struct pm2fb_info* p) {

	if (!cvppc_PCI_init(&p->board_par.cvppc))
		return 0;
	p->regions.fb_base=(unsigned char* )CVPPC_FB_APERTURE_ONE;
	p->regions.fb_size=CVPPC_FB_SIZE;
	p->regions.rg_base=(unsigned char* )CVPPC_REGS_REGION;
	p->memclock=CVPPC_MEMCLOCK;
	return 1;
}

static void cvppc_init(struct pm2fb_info* p) {

	WAIT_FIFO(p, 3);
	pm2_WR(p, PM2R_MEM_CONTROL, 0);
	pm2_WR(p, PM2R_BOOT_ADDRESS, 0x30);
	eieio();
	if (pm2fb_options.flags & OPTF_OLD_MEM)
		pm2_WR(p, PM2R_MEM_CONFIG, CVPPC_MEM_CONFIG_OLD);
	else
		pm2_WR(p, PM2R_MEM_CONFIG, CVPPC_MEM_CONFIG_NEW);
}
#endif /* CONFIG_FB_PM2_CVPPC */

/***************************************************************************
 * Console hw acceleration
 ***************************************************************************/

/*
 * copy with packed pixels (8/16bpp only).
 */
static void pm2fb_pp_copy(struct pm2fb_info* i, long xsrc, long ysrc,
					long x, long y, long w, long h) {
	long scale=i->current_par.depth==8?2:1;
	long offset;

	if (!w || !h)
		return;
	WAIT_FIFO(i, 7);
	pm2_WR(i, PM2R_CONFIG,	PM2F_CONFIG_FB_WRITE_ENABLE|
				PM2F_CONFIG_FB_PACKED_DATA|
				PM2F_CONFIG_FB_READ_SOURCE_ENABLE);
	pm2_WR(i, PM2R_FB_PIXEL_OFFSET, 0);
	pm2_WR(i, PM2R_FB_SOURCE_DELTA,	((ysrc-y)&0xfff)<<16|
						((xsrc-x)&0xfff));
	offset=(x&0x3)-(xsrc&0x3);
	pm2_WR(i, PM2R_RECTANGLE_ORIGIN, (y<<16)|(x>>scale));
	pm2_WR(i, PM2R_RECTANGLE_SIZE, (h<<16)|((w+7)>>scale));
	pm2_WR(i, PM2R_PACKED_DATA_LIMITS, (offset<<29)|(x<<16)|(x+w));
	eieio();
	pm2_WR(i, PM2R_RENDER,	PM2F_RENDER_RECTANGLE|
				(x<xsrc?PM2F_INCREASE_X:0)|
				(y<ysrc?PM2F_INCREASE_Y:0));
	wait_pm2(i);
}

/*
 * block operation. copy=0: rectangle fill, copy=1: rectangle copy.
 */
static void pm2fb_block_op(struct pm2fb_info* i, int copy,
					long xsrc, long ysrc,
					long x, long y, long w, long h,
					unsigned long color) {

	if (!w || !h)
		return;
	WAIT_FIFO(i, 6);
	pm2_WR(i, PM2R_CONFIG,	PM2F_CONFIG_FB_WRITE_ENABLE|
				PM2F_CONFIG_FB_READ_SOURCE_ENABLE);
	pm2_WR(i, PM2R_FB_PIXEL_OFFSET, 0);
	if (copy)
		pm2_WR(i, PM2R_FB_SOURCE_DELTA,	((ysrc-y)&0xfff)<<16|
							((xsrc-x)&0xfff));
	else
		pm2_WR(i, PM2R_FB_BLOCK_COLOR, color);
	pm2_WR(i, PM2R_RECTANGLE_ORIGIN, (y<<16)|x);
	pm2_WR(i, PM2R_RECTANGLE_SIZE, (h<<16)|w);
	eieio();
	pm2_WR(i, PM2R_RENDER,	PM2F_RENDER_RECTANGLE|
				(x<xsrc?PM2F_INCREASE_X:0)|
				(y<ysrc?PM2F_INCREASE_Y:0)|
				(copy?0:PM2F_RENDER_FASTFILL));
	wait_pm2(i);
}

static int pm2fb_blank(int blank_mode, struct fb_info_gen* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;
	unsigned long video;

	if (!i->current_par_valid)
		return 1;
	video=i->current_par.video;
	if (blank_mode>0) {
		switch (blank_mode-1) {
			case VESA_NO_BLANKING:		/* FIXME */
				video=video&~(PM2F_VIDEO_ENABLE);
				break;
			case VESA_HSYNC_SUSPEND:
				video=video&~(PM2F_HSYNC_MASK|
						PM2F_BLANK_LOW);
				break;
			case VESA_VSYNC_SUSPEND:
				video=video&~(PM2F_VSYNC_MASK|
						PM2F_BLANK_LOW);
				break;
			case VESA_POWERDOWN:
				video=video&~(PM2F_VSYNC_MASK|
						PM2F_HSYNC_MASK|
						PM2F_BLANK_LOW);
				break;
		}
	}
	WAIT_FIFO(i, 1);
	pm2_WR(i, PM2R_VIDEO_CONTROL, video);
	return 0;
}

static int pm2fb_pan_display(const struct fb_var_screeninfo* var,
					struct fb_info_gen* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;

	if (!i->current_par_valid)
		return -EINVAL;
	i->current_par.base=to3264(var->yoffset*i->current_par.width+
				var->xoffset, i->current_par.depth, 1);
	WAIT_FIFO(i, 1);
	pm2_WR(i, PM2R_SCREEN_BASE, i->current_par.base);
	return 0;
}

static void pm2fb_pp_bmove(struct display* p, int sy, int sx,
				int dy, int dx, int height, int width) {

	if (fontwidthlog(p)) {
		sx=sx<<fontwidthlog(p);
		dx=dx<<fontwidthlog(p);
		width=width<<fontwidthlog(p);
	}
	else {
		sx=sx*fontwidth(p);
		dx=dx*fontwidth(p);
		width=width*fontwidth(p);
	}
	sy=sy*fontheight(p);
	dy=dy*fontheight(p);
	height=height*fontheight(p);
	pm2fb_pp_copy((struct pm2fb_info* )p->fb_info, sx, sy, dx,
							dy, width, height);
}

static void pm2fb_bmove(struct display* p, int sy, int sx,
				int dy, int dx, int height, int width) {

	if (fontwidthlog(p)) {
		sx=sx<<fontwidthlog(p);
		dx=dx<<fontwidthlog(p);
		width=width<<fontwidthlog(p);
	}
	else {
		sx=sx*fontwidth(p);
		dx=dx*fontwidth(p);
		width=width*fontwidth(p);
	}
	sy=sy*fontheight(p);
	dy=dy*fontheight(p);
	height=height*fontheight(p);
	pm2fb_block_op((struct pm2fb_info* )p->fb_info, 1, sx, sy, dx, dy,
							width, height, 0);
}

#ifdef FBCON_HAS_CFB8
static void pm2fb_clear8(struct vc_data* conp, struct display* p,
				int sy, int sx, int height, int width) {
	unsigned long c;

	sx=sx*fontwidth(p);
	width=width*fontwidth(p);
	sy=sy*fontheight(p);
	height=height*fontheight(p);
	c=attr_bgcol_ec(p, conp);
	c|=c<<8;
	c|=c<<16;
	pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0, sx, sy,
							width, height, c);
}

static void pm2fb_clear_margins8(struct vc_data* conp, struct display* p,
							int bottom_only) {
	unsigned long c;
	unsigned long sx;
	unsigned long sy;

	c=attr_bgcol_ec(p, conp);
	c|=c<<8;
	c|=c<<16;
	sx=conp->vc_cols*fontwidth(p);
	sy=conp->vc_rows*fontheight(p);
	if (!bottom_only)
		pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0,
			sx, 0, (p->var.xres-sx), p->var.yres_virtual, c);
	pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0,
				0, p->var.yoffset+sy, sx, p->var.yres-sy, c);
}

static struct display_switch pm2_cfb8 = {
	fbcon_cfb8_setup, pm2fb_pp_bmove, pm2fb_clear8,
	fbcon_cfb8_putc, fbcon_cfb8_putcs, fbcon_cfb8_revc,
	NULL /* cursor() */, NULL /* set_font() */,
	pm2fb_clear_margins8,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16) };
#endif /* FBCON_HAS_CFB8 */

#ifdef FBCON_HAS_CFB16
static void pm2fb_clear16(struct vc_data* conp, struct display* p,
				int sy, int sx, int height, int width) {
	unsigned long c;

	sx=sx*fontwidth(p);
	width=width*fontwidth(p);
	sy=sy*fontheight(p);
	height=height*fontheight(p);
	c=((u16 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	c|=c<<16;
	pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0, sx, sy,
							width, height, c);
}

static void pm2fb_clear_margins16(struct vc_data* conp, struct display* p,
							int bottom_only) {
	unsigned long c;
	unsigned long sx;
	unsigned long sy;

	c = ((u16 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	c|=c<<16;
	sx=conp->vc_cols*fontwidth(p);
	sy=conp->vc_rows*fontheight(p);
	if (!bottom_only)
		pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0,
			sx, 0, (p->var.xres-sx), p->var.yres_virtual, c);
	pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0,
				0, p->var.yoffset+sy, sx, p->var.yres-sy, c);
}

static struct display_switch pm2_cfb16 = {
	fbcon_cfb16_setup, pm2fb_pp_bmove, pm2fb_clear16,
	fbcon_cfb16_putc, fbcon_cfb16_putcs, fbcon_cfb16_revc,
	NULL /* cursor() */, NULL /* set_font() */,
	pm2fb_clear_margins16,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16) };
#endif /* FBCON_HAS_CFB16 */

#ifdef FBCON_HAS_CFB24
/*
 * fast fill for 24bpp works only when red==green==blue
 */
static void pm2fb_clear24(struct vc_data* conp, struct display* p,
				int sy, int sx, int height, int width) {
	struct pm2fb_info* i=(struct pm2fb_info* )p->fb_info;
	unsigned long c;

	c=attr_bgcol_ec(p, conp);
	if (		i->palette[c].red==i->palette[c].green &&
			i->palette[c].green==i->palette[c].blue) {
		c=((u32 *)p->dispsw_data)[c];
		c|=(c&0xff0000)<<8;
		sx=sx*fontwidth(p);
		width=width*fontwidth(p);
		sy=sy*fontheight(p);
		height=height*fontheight(p);
		pm2fb_block_op(i, 0, 0, 0, sx, sy, width, height, c);
	}
	else
		fbcon_cfb24_clear(conp, p, sy, sx, height, width);

}

static void pm2fb_clear_margins24(struct vc_data* conp, struct display* p,
							int bottom_only) {
	struct pm2fb_info* i=(struct pm2fb_info* )p->fb_info;
	unsigned long c;
	unsigned long sx;
	unsigned long sy;

	c=attr_bgcol_ec(p, conp);
	if (		i->palette[c].red==i->palette[c].green &&
			i->palette[c].green==i->palette[c].blue) {
		c=((u32 *)p->dispsw_data)[c];
		c|=(c&0xff0000)<<8;
		sx=conp->vc_cols*fontwidth(p);
		sy=conp->vc_rows*fontheight(p);
		if (!bottom_only)
		pm2fb_block_op(i, 0, 0, 0, sx, 0, (p->var.xres-sx),
							p->var.yres_virtual, c);
		pm2fb_block_op(i, 0, 0, 0, 0, p->var.yoffset+sy,
						sx, p->var.yres-sy, c);
	}
	else
		fbcon_cfb24_clear_margins(conp, p, bottom_only);

}

static struct display_switch pm2_cfb24 = {
	fbcon_cfb24_setup, pm2fb_bmove, pm2fb_clear24,
	fbcon_cfb24_putc, fbcon_cfb24_putcs, fbcon_cfb24_revc,
	NULL /* cursor() */, NULL /* set_font() */,
	pm2fb_clear_margins24,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16) };
#endif /* FBCON_HAS_CFB24 */

#ifdef FBCON_HAS_CFB32
static void pm2fb_clear32(struct vc_data* conp, struct display* p,
				int sy, int sx, int height, int width) {
	unsigned long c;

	sx=sx*fontwidth(p);
	width=width*fontwidth(p);
	sy=sy*fontheight(p);
	height=height*fontheight(p);
	c=((u32 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0, sx, sy,
							width, height, c);
}

static void pm2fb_clear_margins32(struct vc_data* conp, struct display* p,
							int bottom_only) {
	unsigned long c;
	unsigned long sx;
	unsigned long sy;

	c = ((u32 *)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	sx=conp->vc_cols*fontwidth(p);
	sy=conp->vc_rows*fontheight(p);
	if (!bottom_only)
		pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0,
			sx, 0, (p->var.xres-sx), p->var.yres_virtual, c);
	pm2fb_block_op((struct pm2fb_info* )p->fb_info, 0, 0, 0,
				0, p->var.yoffset+sy, sx, p->var.yres-sy, c);
}

static struct display_switch pm2_cfb32 = {
	fbcon_cfb32_setup, pm2fb_bmove, pm2fb_clear32,
	fbcon_cfb32_putc, fbcon_cfb32_putcs, fbcon_cfb32_revc,
	NULL /* cursor() */, NULL /* set_font() */,
	pm2fb_clear_margins32,
	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16) };
#endif /* FBCON_HAS_CFB32 */

/***************************************************************************
 * Framebuffer functions
 ***************************************************************************/

static void pm2fb_detect(void) {}

static int pm2fb_encode_fix(struct fb_fix_screeninfo* fix,
			const void* par, struct fb_info_gen* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;
	struct pm2fb_par* p=(struct pm2fb_par* )par;

	strcpy(fix->id, permedia2_name);
	fix->smem_start=i->regions.p_fb;
	fix->smem_len=i->regions.fb_size;
	fix->mmio_start=i->regions.p_regs;
	fix->mmio_len=PM2_REGS_SIZE;
	fix->accel=FB_ACCEL_3DLABS_PERMEDIA2;
	fix->type=FB_TYPE_PACKED_PIXELS;
	fix->visual=p->depth==8?FB_VISUAL_PSEUDOCOLOR:FB_VISUAL_TRUECOLOR;
	fix->line_length=0;
	fix->xpanstep=p->depth==24?8:64/p->depth;
	fix->ypanstep=1;
	fix->ywrapstep=0;
	return 0;
}

static int pm2fb_decode_var(const struct fb_var_screeninfo* var,
				void* par, struct fb_info_gen* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;
	struct pm2fb_par p;
	unsigned long xres;
	int data64;

	memset(&p, 0, sizeof(struct pm2fb_par));
	p.width=(var->xres_virtual+7)&~7;
	p.height=var->yres_virtual;
	p.depth=(var->bits_per_pixel+7)&~7;
	p.depth=p.depth>32?32:p.depth;
	data64=p.depth>8;
	xres=(var->xres+31)&~31;
	if (p.width==~(0L))
		p.width=xres;
	if (p.height==~(0L))
		p.height=var->yres;
	if (p.width<xres+var->xoffset)
		p.width=xres+var->xoffset;
	if (p.height<var->yres+var->yoffset)
		p.height=var->yres+var->yoffset;
	if (!partprod(xres)) {
		DPRINTK("width not supported: %lu\n", xres);
		return -EINVAL;
	}
	if (p.width>2047) {
		DPRINTK("virtual width not supported: %lu\n", p.width);
		return -EINVAL;
	}
	if (var->yres<200) {
		DPRINTK("height not supported: %lu\n",
						(unsigned long )var->yres);
		return -EINVAL;
	}
	if (p.height<200 || p.height>2047) {
		DPRINTK("virtual height not supported: %lu\n", p.height);
		return -EINVAL;
	}
	if (p.width*p.height*p.depth/8>i->regions.fb_size) {
		DPRINTK("no memory for screen (%lux%lux%lu)\n",
						xres, p.height, p.depth);
		return -EINVAL;
	}
	p.pixclock=PICOS2KHZ(var->pixclock);
	if (p.pixclock>PM2_MAX_PIXCLOCK) {
		DPRINTK("pixclock too high (%luKHz)\n", p.pixclock);
		return -EINVAL;
	}
	p.hsstart=to3264(var->right_margin, p.depth, data64);
	p.hsend=p.hsstart+to3264(var->hsync_len, p.depth, data64);
	p.hbend=p.hsend+to3264(var->left_margin, p.depth, data64);
	p.htotal=to3264(xres, p.depth, data64)+p.hbend-1;
	p.vsstart=var->lower_margin?var->lower_margin-1:0;	/* FIXME! */
	p.vsend=var->lower_margin+var->vsync_len-1;
	p.vbend=var->lower_margin+var->vsync_len+var->upper_margin;
	p.vtotal=var->yres+p.vbend-1;
	p.stride=to3264(p.width, p.depth, 1);
	p.base=to3264(var->yoffset*xres+var->xoffset, p.depth, 1);
	if (data64)
		p.video|=PM2F_DATA_64_ENABLE;
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		p.video|=PM2F_HSYNC_ACT_HIGH;
	else
		p.video|=PM2F_HSYNC_ACT_LOW;
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		p.video|=PM2F_VSYNC_ACT_HIGH;
	else
		p.video|=PM2F_VSYNC_ACT_LOW;
	if ((var->vmode & FB_VMODE_MASK)==FB_VMODE_INTERLACED) {
		DPRINTK("interlaced not supported\n");
		return -EINVAL;
	}
	if ((var->vmode & FB_VMODE_MASK)==FB_VMODE_DOUBLE)
		p.video|=PM2F_LINE_DOUBLE;
	if (var->activate==FB_ACTIVATE_NOW)
		p.video|=PM2F_VIDEO_ENABLE;
	*((struct pm2fb_par* )par)=p;
	return 0;
}

static int pm2fb_encode_var(struct fb_var_screeninfo* var,
				const void* par, struct fb_info_gen* info) {
	struct pm2fb_par* p=(struct pm2fb_par* )par;
	struct fb_var_screeninfo v;
	unsigned long base;

	memset(&v, 0, sizeof(struct fb_var_screeninfo));
	v.xres_virtual=p->width;
	v.yres_virtual=p->height;
	v.xres=(p->htotal+1)-p->hbend;
	v.yres=(p->vtotal+1)-p->vbend;
	v.right_margin=p->hsstart;
	v.hsync_len=p->hsend-p->hsstart;
	v.left_margin=p->hbend-p->hsend;
	v.lower_margin=p->vsstart+1;
	v.vsync_len=p->vsend-v.lower_margin+1;
	v.upper_margin=p->vbend-v.lower_margin-v.vsync_len;
	v.bits_per_pixel=p->depth;
	if (p->video & PM2F_DATA_64_ENABLE) {
		v.xres=v.xres<<1;
		v.right_margin=v.right_margin<<1;
		v.hsync_len=v.hsync_len<<1;
		v.left_margin=v.left_margin<<1;
	}
	switch (p->depth) {
		case 8:
			v.red.length=v.green.length=v.blue.length=8;
			v.xres=v.xres<<2;
			v.right_margin=v.right_margin<<2;
			v.hsync_len=v.hsync_len<<2;
			v.left_margin=v.left_margin<<2;
			break;
		case 16:
			v.red.offset=11;
			v.red.length=5;
			v.green.offset=5;
			v.green.length=6;
			v.blue.length=5;
			v.xres=v.xres<<1;
			v.right_margin=v.right_margin<<1;
			v.hsync_len=v.hsync_len<<1;
			v.left_margin=v.left_margin<<1;
			break;
		case 32:
			v.transp.offset=24;
			v.red.offset=16;
			v.green.offset=8;
			v.red.length=v.green.length=v.blue.length=
							v.transp.length=8;
			break;
		case 24:
			v.blue.offset=16;
			v.green.offset=8;
			v.red.length=v.green.length=v.blue.length=8;
			v.xres=(v.xres<<2)/3;
			v.right_margin=(v.right_margin<<2)/3;
			v.hsync_len=(v.hsync_len<<2)/3;
			v.left_margin=(v.left_margin<<2)/3;
			break;
	}
	base=from3264(p->base, p->depth, 1);
	v.xoffset=base%v.xres;
	v.yoffset=base/v.xres;
	v.height=v.width=-1;
	v.pixclock=KHZ2PICOS(p->pixclock);
	if ((p->video & PM2F_HSYNC_MASK)==PM2F_HSYNC_ACT_HIGH)
		v.sync|=FB_SYNC_HOR_HIGH_ACT;
	if ((p->video & PM2F_VSYNC_MASK)==PM2F_VSYNC_ACT_HIGH)
		v.sync|=FB_SYNC_VERT_HIGH_ACT;
	if (p->video & PM2F_LINE_DOUBLE)
		v.vmode=FB_VMODE_DOUBLE;
	*var=v;
	return 0;
}

static void set_user_mode(struct pm2fb_info* i) {

	memcpy(&i->current_par, &pm2fb_options.user_mode,
						sizeof(i->current_par));
	if (pm2fb_options.flags & OPTF_YPAN) {
		i->current_par.height=i->regions.fb_size/
			(i->current_par.width*i->current_par.depth/8);
		i->current_par.height=MIN(i->current_par.height,2047);
		i->current_par.height=MAX(i->current_par.height,
						pm2fb_options.user_mode.height);
	}
}

static void pm2fb_get_par(void* par, struct fb_info_gen* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;
	
	if (!i->current_par_valid) {
		set_user_mode(i);
		pm2fb_reset(i);
		set_screen(i, &i->current_par);
		i->current_par_valid=1;
	}
	*((struct pm2fb_par* )par)=i->current_par;
}

static void pm2fb_set_par(const void* par, struct fb_info_gen* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;
	struct pm2fb_par* p;

	p=(struct pm2fb_par* )par;
	if (i->current_par_valid) {
		i->current_par.base=p->base;
		if (!memcmp(p, &i->current_par, sizeof(struct pm2fb_par))) {
			WAIT_FIFO(i, 1);
			pm2_WR(i, PM2R_SCREEN_BASE, p->base);
			return;
		}
	}
	i->current_par=*p;
	i->current_par_valid=1;
	set_screen(i, &i->current_par);
}

static int pm2fb_getcolreg(unsigned regno,
			unsigned* red, unsigned* green, unsigned* blue,
				unsigned* transp, struct fb_info* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;

	if (regno<256) {
		*red=i->palette[regno].red<<8|i->palette[regno].red;
		*green=i->palette[regno].green<<8|i->palette[regno].green;
		*blue=i->palette[regno].blue<<8|i->palette[regno].blue;
		*transp=i->palette[regno].transp<<8|i->palette[regno].transp;
	}
	return regno>255;
}

static int pm2fb_setcolreg(unsigned regno,
			unsigned red, unsigned green, unsigned blue,
				unsigned transp, struct fb_info* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;

	if (regno<16) {
		switch (i->current_par.depth) {
#ifdef FBCON_HAS_CFB8
			case 8:
				break;
#endif
#ifdef FBCON_HAS_CFB16
			case 16:
				i->cmap.cmap16[regno]=
					((u32 )red & 0xf800) |
					(((u32 )green & 0xfc00)>>5) |
					(((u32 )blue & 0xf800)>>11);
				break;
#endif
#ifdef FBCON_HAS_CFB24
			case 24:
				i->cmap.cmap24[regno]=
					(((u32 )blue & 0xff00) << 8) |
					((u32 )green & 0xff00) |
					(((u32 )red & 0xff00) >> 8);
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case 32:
	   			i->cmap.cmap32[regno]=
					(((u32 )transp & 0xff00) << 16) |
		    			(((u32 )red & 0xff00) << 8) |
					(((u32 )green & 0xff00)) |
			 		(((u32 )blue & 0xff00) >> 8);
				break;
#endif
			default:
				DPRINTK("bad depth %lu\n",
						i->current_par.depth);
				break;
		}
	}
	if (regno<256) {
		i->palette[regno].red=red >> 8;
		i->palette[regno].green=green >> 8;
		i->palette[regno].blue=blue >> 8;
		i->palette[regno].transp=transp >> 8;
		if (i->current_par.depth==8)
			set_color(i, regno, red>>8, green>>8, blue>>8);
	}
	return regno>255;
}

static void pm2fb_dispsw(const void* par, struct display* disp,
						struct fb_info_gen* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;
	unsigned long flags;
	unsigned long depth;

	save_flags(flags);
	cli();
	switch (depth=((struct pm2fb_par* )par)->depth) {
#ifdef FBCON_HAS_CFB8
		case 8:
			disp->dispsw=&pm2_cfb8;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			disp->dispsw=&pm2_cfb16;
			disp->dispsw_data=i->cmap.cmap16;
			break;
#endif
#ifdef FBCON_HAS_CFB24
		case 24:
			disp->dispsw=&pm2_cfb24;
			disp->dispsw_data=i->cmap.cmap24;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			disp->dispsw=&pm2_cfb32;
			disp->dispsw_data=i->cmap.cmap32;
			break;
#endif
		default:
			disp->dispsw=&fbcon_dummy;
			break;
	}
	restore_flags(flags);
}

static int pm2fb_open(struct fb_info* info, int user) {

	MOD_INC_USE_COUNT;
	return 0;
}

static int pm2fb_release(struct fb_info* info, int user) {

	MOD_DEC_USE_COUNT;
	return 0;
}

/***************************************************************************
 * Begin of public functions
 ***************************************************************************/

void pm2fb_cleanup(struct fb_info* info) {
	struct pm2fb_info* i=(struct pm2fb_info* )info;

	unregister_framebuffer(info);
	pm2fb_reset(i);
	/* FIXME UNMAP()??? */
	if (board_table[i->board].cleanup)
		board_table[i->board].cleanup(i);
}

__initfunc(void pm2fb_init(void)) {

	memset(&fb_info, 0, sizeof(fb_info));
	if (!pm2fb_conf(&fb_info))
		return;
	fb_info.disp.scrollmode=SCROLL_YNOMOVE;
	fb_info.gen.parsize=sizeof(struct pm2fb_par);
	fb_info.gen.fbhw=&pm2fb_hwswitch;
	strcpy(fb_info.gen.info.modename, permedia2_name);
	fb_info.gen.info.flags=FBINFO_FLAG_DEFAULT;
	fb_info.gen.info.fbops=&pm2fb_ops;
	fb_info.gen.info.disp=&fb_info.disp;
	strcpy(fb_info.gen.info.fontname, pm2fb_options.font);
	fb_info.gen.info.switch_con=&fbgen_switch;
	fb_info.gen.info.updatevar=&fbgen_update_var;
	fb_info.gen.info.blank=&fbgen_blank;
	fbgen_get_var(&fb_info.disp.var, -1, &fb_info.gen.info);
	if (fbgen_do_set_var(&fb_info.disp.var, 1, &fb_info.gen)<0)
		return;
	fbgen_set_disp(-1, &fb_info.gen);
	fbgen_install_cmap(0, &fb_info.gen);
	if (register_framebuffer(&fb_info.gen.info)<0) {
		printk("pm2fb: unable to register.\n");
		return;
	}
	printk("fb%d: %s (%s), using %ldK of video memory.\n",
				GET_FB_IDX(fb_info.gen.info.node),
				board_table[fb_info.board].name,
				permedia2_name,
				(unsigned long )(fb_info.regions.fb_size>>10));
	MOD_INC_USE_COUNT;
}

__initfunc(void pm2fb_mode_setup(char* options)) {
	int i;

	for (i=0; user_mode[i].name[0] &&
		strcmp(options, user_mode[i].name); i++);
	if (user_mode[i].name[0])
		memcpy(&pm2fb_options.user_mode, &user_mode[i].par,
					sizeof(pm2fb_options.user_mode));
}

__initfunc(void pm2fb_font_setup(char* options)) {

	strncpy(pm2fb_options.font, options, sizeof(pm2fb_options.font));
	pm2fb_options.font[sizeof(pm2fb_options.font)-1]='\0';
}

__initfunc(void pm2fb_setup(char* options, int* ints)) {
	char* next;

	memset(&pm2fb_options, 0, sizeof(pm2fb_options));
	memcpy(&pm2fb_options.user_mode, &user_mode[0].par,
				sizeof(pm2fb_options.user_mode));
	while (options) {
		if ((next=strchr(options, ',')))
			*(next++)='\0';
		if (!strncmp(options, "font:", 5))
			pm2fb_font_setup(options+5);
		else if (!strncmp(options, "mode:", 5))
			pm2fb_mode_setup(options+5);
		else if (!strcmp(options, "ypan"))
			pm2fb_options.flags |= OPTF_YPAN;
		else if (!strcmp(options, "oldmem"))
			pm2fb_options.flags |= OPTF_OLD_MEM;
		options=next;
	}
}

/***************************************************************************
 * Begin of module functions
 ***************************************************************************/

#ifdef MODULE
int init_module(void) {

	pm2fb_init();
}

void cleanup_module(void) {

	pm2fb_cleanup();
}
#endif /* MODULE */

/***************************************************************************
 * That's all folks!
 ***************************************************************************/
