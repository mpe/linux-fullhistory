/*
 * Linux/drivers/video/retz3fb.c -- RetinaZ3 frame buffer device
 *
 *    Copyright (C) 1997 Jes Sorensen
 *
 * This file is based on the CyberVision64 frame buffer device and
 * the generic Cirrus Logic driver.
 *
 * cyberfb.c: Copyright (C) 1996 Martin Apel,
 *                               Geert Uytterhoeven
 * clgen.c:   Copyright (C) 1996 Frank Neumann
 *
 * History:
 *   - 22 Jan 97: Initial work
 *   - 14 Feb 97: Screen initialization works somewhat, still only
 *                8-bit packed pixel is supported.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
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
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/zorro.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

#include "retz3fb.h"
#include "fbcon.h"
#include "fbcon-cfb8.h"
#include "fbcon-cfb16.h"


/* #define DEBUG if(1) */
#define DEBUG if(0)

/*
 * Reserve space for one pattern line.
 *
 * For the time being we only support 4MB boards!
 */

#define PAT_MEM_SIZE 16*3
#define PAT_MEM_OFF  (4*1024*1024 - PAT_MEM_SIZE)

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

struct retz3fb_par {
	int xres;
	int yres;
	int xres_vir;
	int yres_vir;
	int xoffset;
	int yoffset;
	int bpp;

	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;

	int pixclock;
	int left_margin;	/* time from sync to picture	*/
	int right_margin;	/* time from picture to sync	*/
	int upper_margin;	/* time from sync to picture	*/
	int lower_margin;
	int hsync_len;	/* length of horizontal sync	*/
	int vsync_len;	/* length of vertical sync	*/
	int vmode;

	int accel;
};

struct display_data {
	long h_total;		/* Horizontal Total */
	long h_sstart;		/* Horizontal Sync Start */
	long h_sstop;		/* Horizontal Sync Stop */
	long h_bstart;		/* Horizontal Blank Start */
	long h_bstop;		/* Horizontal Blank Stop */
	long h_dispend;		/* Horizontal Display End */
	long v_total;		/* Vertical Total */
	long v_sstart;		/* Vertical Sync Start */
	long v_sstop;		/* Vertical Sync Stop */
	long v_bstart;		/* Vertical Blank Start */
	long v_bstop;		/* Vertical Blank Stop */
	long v_dispend;		/* Horizontal Display End */
};

static struct retz3fb_par current_par;

static int current_par_valid = 0;
static int currcon = 0;

static struct display disp;
static struct fb_info fb_info;


/*
 *    Switch for Chipset Independency
 */

static struct fb_hwswitch {

   /* Initialisation */

   int (*init)(void);

   /* Display Control */

   int (*encode_fix)(struct fb_fix_screeninfo *fix, struct retz3fb_par *par);
   int (*decode_var)(struct fb_var_screeninfo *var, struct retz3fb_par *par);
   int (*encode_var)(struct fb_var_screeninfo *var, struct retz3fb_par *par);
   int (*getcolreg)(unsigned int regno, unsigned int *red, unsigned
		    int *green, unsigned int *blue, unsigned int *transp,
			struct fb_info *info);
   int (*setcolreg)(unsigned int regno, unsigned int red, unsigned int
		    green, unsigned int blue, unsigned int transp,
			struct fb_info *info);
   void (*blank)(int blank);
} *fbhw;


/*
 *    Frame Buffer Name
 */

static char retz3fb_name[16] = "RetinaZ3";


static unsigned char retz3_color_table [256][4];
static unsigned long z3_mem;
static unsigned long z3_fbmem;
static unsigned long z3_size;
static volatile unsigned char *z3_regs;


/*
 * A small info on how to convert XFree86 timing values into fb
 * timings - by Frank Neumann:
 *
An XFree86 mode line consists of the following fields:
 "800x600"     50      800  856  976 1040    600  637  643  666
 < name >     DCF       HR  SH1  SH2  HFL     VR  SV1  SV2  VFL

The fields in the fb_var_screeninfo structure are:
        unsigned long pixclock;         * pixel clock in ps (pico seconds) *
        unsigned long left_margin;      * time from sync to picture    *
        unsigned long right_margin;     * time from picture to sync    *
        unsigned long upper_margin;     * time from sync to picture    *
        unsigned long lower_margin;
        unsigned long hsync_len;        * length of horizontal sync    *
        unsigned long vsync_len;        * length of vertical sync      *

1) Pixelclock:
   xfree: in MHz
   fb: In Picoseconds (ps)

   pixclock = 1000000 / DCF

2) horizontal timings:
   left_margin = HFL - SH2
   right_margin = SH1 - HR
   hsync_len = SH2 - SH1

3) vertical timings:
   upper_margin = VFL - SV2
   lower_margin = SV1 - VR
   vsync_len = SV2 - SV1

Good examples for VESA timings can be found in the XFree86 source tree,
under "programs/Xserver/hw/xfree86/doc/modeDB.txt".
*/

/*
 *    Predefined Video Modes
 */

static struct fb_videomode retz3fb_predefined[] __initdata = {
    /*
     * NB: it is very important to adjust the pixel-clock to the color-depth.
     */

    {
	"640x480", {		/* 640x480, 8 bpp */
	    640, 480, 640, 480, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 38461, 28, 32, 12, 10, 96, 2,
	    FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    },
    /*
     ModeLine "800x600" 36 800 824 896 1024 600 601 603 625
	      < name > DCF HR  SH1 SH2  HFL VR  SV1 SV2 VFL
     */
    {
	"800x600", {		/* 800x600, 8 bpp */
	    800, 600, 800, 600, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 27778, 64, 24, 22, 1, 120, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    },
    /*
     ModeLine "1024x768i" 45 1024 1064 1224 1264 768 777 785 817 interlace
	       < name >   DCF HR  SH1  SH2  HFL  VR  SV1 SV2 VFL
     */
    {
	"1024x768i", {		/* 1024x768, 8 bpp, interlaced */
	    1024, 768, 1024, 768, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 22222, 40, 40, 32, 9, 160, 8,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_INTERLACED
	}
    }, {
	"640x480-16", {		/* 640x480, 16 bpp */
	    640, 480, 640, 480, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, 0, 38461/2, 28, 32, 12, 10, 96, 2,
	    FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"640x480-24", {		/* 640x480, 24 bpp */
	    640, 480, 640, 480, 0, 0, 24, 0,
	    {8, 8, 8}, {8, 8, 8}, {8, 8, 8}, {0, 0, 0},
	    0, 0, -1, -1, 0, 38461/3, 28, 32, 12, 10, 96, 2,
	    FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    },
};


#define NUM_TOTAL_MODES    arraysize(retz3fb_predefined)

static struct fb_var_screeninfo retz3fb_default;

static int z3fb_inverse = 0;
static int z3fb_mode __initdata = 0;


/*
 *    Interface used by the world
 */

void retz3fb_setup(char *options, int *ints);

static int retz3fb_open(struct fb_info *info);
static int retz3fb_release(struct fb_info *info);
static int retz3fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info);
static int retz3fb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int retz3fb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int retz3fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info);
static int retz3fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info);
static int retz3fb_pan_display(struct fb_var_screeninfo *var, int con,
			       struct fb_info *info);
static int retz3fb_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg, int con,
			 struct fb_info *info);


/*
 *    Interface to the low level console driver
 */

unsigned long retz3fb_init(unsigned long mem_start);
static int z3fb_switch(int con, struct fb_info *info);
static int z3fb_updatevar(int con, struct fb_info *info);
static void z3fb_blank(int blank, struct fb_info *info);


/*
 *    Text console acceleration
 */

#ifdef CONFIG_FBCON_CFB8
static struct display_switch fbcon_retz3_8;
#endif


/*
 *    Accelerated Functions used by the low level console driver
 */

static void retz3_bitblt(struct fb_var_screeninfo *scr,
			 unsigned short curx, unsigned short cury, unsigned
			 short destx, unsigned short desty, unsigned short
			 width, unsigned short height, unsigned short cmd,
			 unsigned short mask);

/*
 *   Hardware Specific Routines
 */

static int retz3_init(void);
static int retz3_encode_fix(struct fb_fix_screeninfo *fix,
                          struct retz3fb_par *par);
static int retz3_decode_var(struct fb_var_screeninfo *var,
                          struct retz3fb_par *par);
static int retz3_encode_var(struct fb_var_screeninfo *var,
                          struct retz3fb_par *par);
static int retz3_getcolreg(unsigned int regno, unsigned int *red,
			   unsigned int *green, unsigned int *blue,
			   unsigned int *transp, struct fb_info *info);
static int retz3_setcolreg(unsigned int regno, unsigned int red,
			   unsigned int green, unsigned int blue,
			   unsigned int transp, struct fb_info *info);
static void retz3_blank(int blank);


/*
 *    Internal routines
 */

static void retz3fb_get_par(struct retz3fb_par *par);
static void retz3fb_set_par(struct retz3fb_par *par);
static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive);
static void do_install_cmap(int con, struct fb_info *info);
static void retz3fb_set_disp(int con, struct fb_info *info);
static int get_video_mode(const char *name);


/* -------------------- Hardware specific routines -------------------------- */

static unsigned short find_fq(unsigned int freq)
{
	unsigned long f;
	long tmp;
	long prev = 0x7fffffff;
	long n2, n1 = 3;
	unsigned long m;
	unsigned short res = 0;

	if (freq <= 31250000)
		n2 = 3;
	else if (freq <= 62500000)
		n2 = 2;
	else if (freq <= 125000000)
		n2 = 1;
	else if (freq <= 250000000)
		n2 = 0;
	else
		return 0;


	do {
		f = freq >> (10 - n2);

		m = (f * n1) / (14318180/1024);

		if (m > 129)
			break;

		tmp =  (((m * 14318180) >> n2) / n1) - freq;
		if (tmp < 0)
			tmp = -tmp;

		if (tmp < prev) {
			prev = tmp;
			res = (((n2 << 5) | (n1-2)) << 8) | (m-2);
		}

	} while ( (++n1) <= 21);

	return res;
}


static int retz3_set_video(struct fb_var_screeninfo *var,
			   struct retz3fb_par *par)
{
#if 0
	float freq_f;
#endif
	unsigned int freq;

	int xres, hfront, hsync, hback;
	int yres, vfront, vsync, vback;
	unsigned char tmp;
	unsigned short best_freq;
	struct display_data data;

	short clocksel = 0; /* Apparantly this is always zero */

	int bpp = var->bits_per_pixel;

	/*
	 * XXX
	 */
	if (bpp == 24)
		return 0;

	if ((bpp != 8) && (bpp != 16) && (bpp != 24))
		return -EFAULT;

	par->xoffset = 0;
	par->yoffset = 0;

	xres   = var->xres * bpp / 4;
	hfront = var->right_margin * bpp / 4;
	hsync  = var->hsync_len * bpp / 4;
	hback  = var->left_margin * bpp / 4;

	if (var->vmode & FB_VMODE_DOUBLE)
	{
		yres = var->yres * 2;
		vfront = var->lower_margin * 2;
		vsync  = var->vsync_len * 2;
		vback  = var->upper_margin * 2;
	}
	else if (var->vmode & FB_VMODE_INTERLACED)
	{
		yres   = (var->yres + 1) / 2;
		vfront = (var->lower_margin + 1) / 2;
		vsync  = (var->vsync_len + 1) / 2;
		vback  = (var->upper_margin + 1) / 2;
	}
	else
	{
		yres   = var->yres; /* -1 ? */
		vfront = var->lower_margin;
		vsync  = var->vsync_len;
		vback  = var->upper_margin;
	}

	data.h_total	= (hback / 8) + (xres / 8)
			+ (hfront / 8) + (hsync / 8) - 1 /* + 1 */;
	data.h_dispend	= ((xres + bpp - 1)/ 8) - 1;
	data.h_bstart	= xres / 8 /* + 1 */;

	data.h_bstop	= data.h_total+1 + 2 + 1;
	data.h_sstart	= (xres / 8) + (hfront / 8) + 1;
	data.h_sstop	= (xres / 8) + (hfront / 8) + (hsync / 8) + 1;

	data.v_total	= yres + vfront + vsync + vback - 1;

	data.v_dispend	= yres - 1;
	data.v_bstart	= yres;

	data.v_bstop	= data.v_total;
	data.v_sstart	= yres + vfront - 1 - 2;
	data.v_sstop	= yres + vfront + vsync - 1;

#if 0 /* testing */

	printk("HBS: %i\n", data.h_bstart);
	printk("HSS: %i\n", data.h_sstart);
	printk("HSE: %i\n", data.h_sstop);
	printk("HBE: %i\n", data.h_bstop);
	printk("HT: %i\n", data.h_total);

	printk("hsync: %i\n", hsync);
	printk("hfront: %i\n", hfront);
	printk("hback: %i\n", hback);

	printk("VBS: %i\n", data.v_bstart);
	printk("VSS: %i\n", data.v_sstart);
	printk("VSE: %i\n", data.v_sstop);
	printk("VBE: %i\n", data.v_bstop);
	printk("VT: %i\n", data.v_total);

	printk("vsync: %i\n", vsync);
	printk("vfront: %i\n", vfront);
	printk("vback: %i\n", vback);
#endif

	if (data.v_total >= 1024)
		printk("MAYDAY: v_total >= 1024; bailing out!\n");

	reg_w(GREG_MISC_OUTPUT_W, 0xe3 | ((clocksel & 3) * 0x04));
	reg_w(GREG_FEATURE_CONTROL_W, 0x00);

	seq_w(SEQ_RESET, 0x00);
	seq_w(SEQ_RESET, 0x03);	/* reset sequencer logic */

	/*
	 * CLOCKING_MODE bits:
	 * 2: This one is only set for certain text-modes, wonder if
	 *    it may be for EGA-lines? (it was referred to as CLKDIV2)
	 * (The CL drivers sets it to 0x21 with the comment:
	 *  FullBandwidth (video off) and 8/9 dot clock)
	 */
	seq_w(SEQ_CLOCKING_MODE, 0x01 | 0x00 /* 0x08 */);

	seq_w(SEQ_MAP_MASK, 0x0f);        /* enable writing to plane 0-3 */
	seq_w(SEQ_CHAR_MAP_SELECT, 0x00); /* doesn't matter in gfx-mode */
	seq_w(SEQ_MEMORY_MODE, 0x06); /* CL driver says 0x0e for 256 col mode*/
	seq_w(SEQ_RESET, 0x01);
	seq_w(SEQ_RESET, 0x03);

	seq_w(SEQ_EXTENDED_ENABLE, 0x05);

	seq_w(SEQ_CURSOR_CONTROL, 0x00);	/* disable cursor */
	seq_w(SEQ_PRIM_HOST_OFF_HI, 0x00);
	seq_w(SEQ_PRIM_HOST_OFF_HI, 0x00);
	seq_w(SEQ_LINEAR_0, 0x4a);
	seq_w(SEQ_LINEAR_1, 0x00);

	seq_w(SEQ_SEC_HOST_OFF_HI, 0x00);
	seq_w(SEQ_SEC_HOST_OFF_LO, 0x00);
	seq_w(SEQ_EXTENDED_MEM_ENA, 0x3 | 0x4 | 0x10 | 0x40);

	/*
	 * The lower 4 bits (0-3) are used to set the font-width for
	 * text-mode - DON'T try to set this for gfx-mode.
	 */
	seq_w(SEQ_EXT_CLOCK_MODE, 0x10);
	seq_w(SEQ_EXT_VIDEO_ADDR, 0x03);

	/*
	 * Extended Pixel Control:
	 * bit 0:   text-mode=0, gfx-mode=1 (Graphics Byte ?)
	 * bit 1: (Packed/Nibble Pixel Format ?)
	 * bit 4-5: depth, 0=1-8bpp, 1=9-16bpp, 2=17-24bpp
	 */
	seq_w(SEQ_EXT_PIXEL_CNTL, 0x01 | (((bpp / 8) - 1) << 4));

	seq_w(SEQ_BUS_WIDTH_FEEDB, 0x04);
	seq_w(SEQ_COLOR_EXP_WFG, 0x01);
	seq_w(SEQ_COLOR_EXP_WBG, 0x00);
	seq_w(SEQ_EXT_RW_CONTROL, 0x00);
	seq_w(SEQ_MISC_FEATURE_SEL, (0x51 | (clocksel & 8)));
	seq_w(SEQ_COLOR_KEY_CNTL, 0x40);
	seq_w(SEQ_COLOR_KEY_MATCH0, 0x00);
	seq_w(SEQ_COLOR_KEY_MATCH1, 0x00);
	seq_w(SEQ_COLOR_KEY_MATCH2, 0x00);
	seq_w(SEQ_CRC_CONTROL, 0x00);
	seq_w(SEQ_PERF_SELECT, 0x10);
	seq_w(SEQ_ACM_APERTURE_1, 0x00);
	seq_w(SEQ_ACM_APERTURE_2, 0x30);
	seq_w(SEQ_ACM_APERTURE_3, 0x00);
	seq_w(SEQ_MEMORY_MAP_CNTL, 0x03);


	/* unlock register CRT0..CRT7 */
	crt_w(CRT_END_VER_RETR, (data.v_sstop & 0x0f) | 0x20);

	/* Zuerst zu schreibende Werte nur per printk ausgeben */
	DEBUG printk("CRT_HOR_TOTAL: %ld\n", data.h_total);
	crt_w(CRT_HOR_TOTAL, data.h_total & 0xff);

	DEBUG printk("CRT_HOR_DISP_ENA_END: %ld\n", data.h_dispend);
	crt_w(CRT_HOR_DISP_ENA_END, (data.h_dispend) & 0xff);

	DEBUG printk("CRT_START_HOR_BLANK: %ld\n", data.h_bstart);
	crt_w(CRT_START_HOR_BLANK, data.h_bstart & 0xff);

	DEBUG printk("CRT_END_HOR_BLANK: 128+%ld\n", data.h_bstop % 32);
	crt_w(CRT_END_HOR_BLANK,  0x80 | (data.h_bstop & 0x1f));

	DEBUG printk("CRT_START_HOR_RETR: %ld\n", data.h_sstart);
	crt_w(CRT_START_HOR_RETR, data.h_sstart & 0xff);

	tmp = (data.h_sstop & 0x1f);
	if (data.h_bstop & 0x20)
		tmp |= 0x80;
	DEBUG printk("CRT_END_HOR_RETR: %d\n", tmp);
	crt_w(CRT_END_HOR_RETR, tmp);

	DEBUG printk("CRT_VER_TOTAL: %ld\n", data.v_total & 0xff);
	crt_w(CRT_VER_TOTAL, (data.v_total & 0xff));

	tmp = 0x10;  /* LineCompare bit #9 */
	if (data.v_total & 256)
		tmp |= 0x01;
	if (data.v_dispend & 256)
		tmp |= 0x02;
	if (data.v_sstart & 256)
		tmp |= 0x04;
	if (data.v_bstart & 256)
		tmp |= 0x08;
	if (data.v_total & 512)
		tmp |= 0x20;
	if (data.v_dispend & 512)
		tmp |= 0x40;
	if (data.v_sstart & 512)
		tmp |= 0x80;
	DEBUG printk("CRT_OVERFLOW: %d\n", tmp);
	crt_w(CRT_OVERFLOW, tmp);

	crt_w(CRT_PRESET_ROW_SCAN, 0x00); /* not CL !!! */

	tmp = 0x40; /* LineCompare bit #8 */
	if (data.v_bstart & 512)
		tmp |= 0x20;
	if (var->vmode & FB_VMODE_DOUBLE)
		tmp |= 0x80;
 	DEBUG printk("CRT_MAX_SCAN_LINE: %d\n", tmp);
	crt_w(CRT_MAX_SCAN_LINE, tmp);

	crt_w(CRT_CURSOR_START, 0x00);
	crt_w(CRT_CURSOR_END, 8 & 0x1f); /* font height */

	crt_w(CRT_START_ADDR_HIGH, 0x00);
	crt_w(CRT_START_ADDR_LOW, 0x00);

	crt_w(CRT_CURSOR_LOC_HIGH, 0x00);
	crt_w(CRT_CURSOR_LOC_LOW, 0x00);

 	DEBUG printk("CRT_START_VER_RETR: %ld\n", data.v_sstart & 0xff);
	crt_w(CRT_START_VER_RETR, (data.v_sstart & 0xff));

#if 1
	/* 5 refresh cycles per scanline */
	DEBUG printk("CRT_END_VER_RETR: 64+32+%ld\n", data.v_sstop % 16);
	crt_w(CRT_END_VER_RETR, ((data.v_sstop & 0x0f) | 0x40 | 0x20));
#else
	DEBUG printk("CRT_END_VER_RETR: 128+32+%ld\n", data.v_sstop % 16);
	crt_w(CRT_END_VER_RETR, ((data.v_sstop & 0x0f) | 128 | 32));
#endif
	DEBUG printk("CRT_VER_DISP_ENA_END: %ld\n", data.v_dispend & 0xff);
	crt_w(CRT_VER_DISP_ENA_END, (data.v_dispend & 0xff));

	DEBUG printk("CRT_START_VER_BLANK: %ld\n", data.v_bstart & 0xff);
	crt_w(CRT_START_VER_BLANK, (data.v_bstart & 0xff));

	DEBUG printk("CRT_END_VER_BLANK: %ld\n", data.v_bstop & 0xff);
	crt_w(CRT_END_VER_BLANK, (data.v_bstop & 0xff));

	DEBUG printk("CRT_MODE_CONTROL: 0xe3\n");
	crt_w(CRT_MODE_CONTROL, 0xe3);

	DEBUG printk("CRT_LINE_COMPARE: 0xff\n");
	crt_w(CRT_LINE_COMPARE, 0xff);

	tmp = (var->xres_virtual / 8) * (bpp / 8);
	crt_w(CRT_OFFSET, tmp);

	crt_w(CRT_UNDERLINE_LOC, 0x07); /* probably font-height - 1 */

	tmp = 0x20;			/* Enable extended end bits */
	if (data.h_total & 0x100)
		tmp |= 0x01;
	if ((data.h_dispend) & 0x100)
		tmp |= 0x02;
	if (data.h_bstart & 0x100)
		tmp |= 0x04;
	if (data.h_sstart & 0x100)
		tmp |= 0x08;
	if (var->vmode & FB_VMODE_INTERLACED)
		tmp |= 0x10;
 	DEBUG printk("CRT_EXT_HOR_TIMING1: %d\n", tmp);
	crt_w(CRT_EXT_HOR_TIMING1, tmp);

	tmp = 0x00;
	if (((var->xres_virtual / 8) * (bpp / 8)) & 0x100)
		tmp |= 0x10;
	crt_w(CRT_EXT_START_ADDR, tmp);

	tmp = 0x00;
	if (data.h_total & 0x200)
		tmp |= 0x01;
	if ((data.h_dispend) & 0x200)
		tmp |= 0x02;
	if (data.h_bstart & 0x200)
		tmp |= 0x04;
	if (data.h_sstart & 0x200)
		tmp |= 0x08;
	tmp |= ((data.h_bstop & 0xc0) >> 2);
	tmp |= ((data.h_sstop & 0x60) << 1);
	crt_w(CRT_EXT_HOR_TIMING2, tmp);
 	DEBUG printk("CRT_EXT_HOR_TIMING2: %d\n", tmp);

	tmp = 0x10;			/* Line compare bit 10 */
	if (data.v_total & 0x400)
		tmp |= 0x01;
	if ((data.v_dispend) & 0x400)
		tmp |= 0x02;
	if (data.v_bstart & 0x400)
		tmp |= 0x04;
	if (data.v_sstart & 0x400)
		tmp |= 0x08;
	tmp |= ((data.v_bstop & 0x300) >> 3);
	if (data.v_sstop & 0x10)
		tmp |= 0x80;
	crt_w(CRT_EXT_VER_TIMING, tmp);
 	DEBUG printk("CRT_EXT_VER_TIMING: %d\n", tmp);

	crt_w(CRT_MONITOR_POWER, 0x00);

	/*
	 * Convert from ps to Hz.
	 */
#if 0
	freq_f = (1.0/(float)var->pixclock) * 1000000000;
	freq = ((unsigned int)freq_f) * 1000;
#else
	freq = 2000000000 / var->pixclock;
	freq = freq * 500;
#endif

	best_freq = find_fq(freq);
	pll_w(0x02, best_freq);
	best_freq = find_fq(61000000);
	pll_w(0x0a, best_freq);
	pll_w(0x0e, 0x22);

	gfx_w(GFX_SET_RESET, 0x00);
	gfx_w(GFX_ENABLE_SET_RESET, 0x00);
	gfx_w(GFX_COLOR_COMPARE, 0x00);
	gfx_w(GFX_DATA_ROTATE, 0x00);
	gfx_w(GFX_READ_MAP_SELECT, 0x00);
	gfx_w(GFX_GRAPHICS_MODE, 0x00);
	gfx_w(GFX_MISC, 0x05);
	gfx_w(GFX_COLOR_XCARE, 0x0f);
	gfx_w(GFX_BITMASK, 0xff);

	reg_r(ACT_ADDRESS_RESET);
	attr_w(ACT_PALETTE0 , 0x00);
	attr_w(ACT_PALETTE1 , 0x01);
	attr_w(ACT_PALETTE2 , 0x02);
	attr_w(ACT_PALETTE3 , 0x03);
	attr_w(ACT_PALETTE4 , 0x04);
	attr_w(ACT_PALETTE5 , 0x05);
	attr_w(ACT_PALETTE6 , 0x06);
	attr_w(ACT_PALETTE7 , 0x07);
	attr_w(ACT_PALETTE8 , 0x08);
	attr_w(ACT_PALETTE9 , 0x09);
	attr_w(ACT_PALETTE10, 0x0a);
	attr_w(ACT_PALETTE11, 0x0b);
	attr_w(ACT_PALETTE12, 0x0c);
	attr_w(ACT_PALETTE13, 0x0d);
	attr_w(ACT_PALETTE14, 0x0e);
	attr_w(ACT_PALETTE15, 0x0f);
	reg_r(ACT_ADDRESS_RESET);

	attr_w(ACT_ATTR_MODE_CNTL, 0x09); /* 0x01 for CL */

	attr_w(ACT_OVERSCAN_COLOR, 0x00);
	attr_w(ACT_COLOR_PLANE_ENA, 0x0f);
	attr_w(ACT_HOR_PEL_PANNING, 0x00);
	attr_w(ACT_COLOR_SELECT, 0x00);

	reg_r(ACT_ADDRESS_RESET);
	reg_w(ACT_DATA, 0x20);

	reg_w(VDAC_MASK, 0xff);

	/*
	 * Extended palette adressing ???
	 */
	switch (bpp){
	case 8:
		reg_w(0x83c6, 0x00);
		break;
	case 16:
		reg_w(0x83c6, 0x60);
		break;
	case 24:
		reg_w(0x83c6, 0xe0);
		break;
	default:
		printk("Illegal color-depth: %i\n", bpp);
	}

	reg_w(VDAC_ADDRESS, 0x00);

	seq_w(SEQ_MAP_MASK, 0x0f );

	return 0;
}

/*
 *    Initialization
 *
 *    Set the default video mode for this chipset. If a video mode was
 *    specified on the command line, it will override the default mode.
 */

static int retz3_init(void)
{
	short i;
#if 0
	volatile unsigned long *CursorBase;
#endif

	for (i = 0; i < 256; i++){
		for (i = 0; i < 256; i++){
			retz3_color_table [i][0] = i;
			retz3_color_table [i][1] = i;
			retz3_color_table [i][2] = i;
			retz3_color_table [i][3] = 0;
		}
	}

	/* Disable hardware cursor */

	seq_w(SEQ_CURSOR_Y_INDEX, 0x00);

#if 0
	/* Initialize hardware cursor */
	CursorBase = (unsigned long *)((char *)(z3_mem) + z3_size - 0x400);
	for (i=0; i < 8; i++){
		*(CursorBase  +(i*4)) = 0xffffff00;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}
	for (i=8; i < 64; i++){
		*(CursorBase  +(i*4)) = 0xffff0000;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}
#endif

	retz3_setcolreg (255, 56, 100, 160, 0, NULL /* unused */);
	retz3_setcolreg (254, 0, 0, 0, 0, NULL /* unused */);

	return 0;
}


/*
 *    This function should fill in the `fix' structure based on the
 *    values in the `par' structure.
 */

static int retz3_encode_fix(struct fb_fix_screeninfo *fix,
			    struct retz3fb_par *par)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, retz3fb_name);
	fix->smem_start = (char *)z3_fbmem;
	fix->smem_len = z3_size;
	fix->mmio_start = (char *)z3_regs;
	fix->mmio_len = 0x00c00000;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	if (par->bpp == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_DIRECTCOLOR;

	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = 0;

	fix->accel = FB_ACCEL_NCR_77C32BLT;

	return 0;
}


/*
 *    Get the video params out of `var'. If a value doesn't fit, round
 *    it up, if it's too big, return -EINVAL.
 */

static int retz3_decode_var(struct fb_var_screeninfo *var,
			    struct retz3fb_par *par)
{
	par->xres = var->xres;
	par->yres = var->yres;
	par->xres_vir = var->xres_virtual;
	par->yres_vir = var->yres_virtual;
	par->bpp = var->bits_per_pixel;
	par->pixclock = var->pixclock;
	par->vmode = var->vmode;

	par->red = var->red;
	par->green = var->green;
	par->blue = var->blue;
	par->transp = var->transp;

	par->left_margin = var->left_margin;
	par->right_margin = var->right_margin;
	par->upper_margin = var->upper_margin;
	par->lower_margin = var->lower_margin;
	par->hsync_len = var->hsync_len;
	par->vsync_len = var->vsync_len;

	if (var->accel_flags & FB_ACCELF_TEXT)
	    par->accel = FB_ACCELF_TEXT;
	else
	    par->accel = 0;

	return 0;
}


/*
 *    Fill the `var' structure based on the values in `par' and maybe
 *    other values read out of the hardware.
 */

static int retz3_encode_var(struct fb_var_screeninfo *var,
			    struct retz3fb_par *par)
{
	memset(var, 0, sizeof(struct fb_var_screeninfo));
	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->xres_vir;
	var->yres_virtual = par->yres_vir;
	var->xoffset = 0;
	var->yoffset = 0;

	var->bits_per_pixel = par->bpp;
	var->grayscale = 0;

	var->red = par->red;
	var->green = par->green;
	var->blue = par->blue;
	var->transp = par->transp;

	var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;

	var->accel_flags = (par->accel && par->bpp == 8) ? FB_ACCELF_TEXT : 0;

	var->pixclock = par->pixclock;

	var->sync = 0;				/* ??? */
	var->left_margin = par->left_margin;
	var->right_margin = par->right_margin;
	var->upper_margin = par->upper_margin;
	var->lower_margin = par->lower_margin;
	var->hsync_len = par->hsync_len;
	var->vsync_len = par->vsync_len;

	var->vmode = par->vmode;
	return 0;
}


/*
 *    Set a single color register. The values supplied are already
 *    rounded down to the hardware's capabilities (according to the
 *    entries in the var structure). Return != 0 for invalid regno.
 */

static int retz3_setcolreg(unsigned int regno, unsigned int red,
			   unsigned int green, unsigned int blue,
			   unsigned int transp, struct fb_info *info)
{
	/* We'll get to this */

	if (regno > 255)
		return 1;

	retz3_color_table [regno][0] = red & 0xff;
	retz3_color_table [regno][1] = green & 0xff;
	retz3_color_table [regno][2] = blue & 0xff;
	retz3_color_table [regno][3] = transp;

	reg_w(VDAC_ADDRESS_W, regno);
	reg_w(VDAC_DATA, (red & 0xff) >> 2);
	reg_w(VDAC_DATA, (green & 0xff) >> 2);
	reg_w(VDAC_DATA, (blue & 0xff) >> 2);

	return 0;
}


/*
 *    Read a single color register and split it into
 *    colors/transparent. Return != 0 for invalid regno.
 */

static int retz3_getcolreg(unsigned int regno, unsigned int *red,
			   unsigned int *green, unsigned int *blue,
			   unsigned int *transp, struct fb_info *info)
{
	if (regno > 255)
		return 1;
	*red    = retz3_color_table [regno][0];
	*green  = retz3_color_table [regno][1];
	*blue   = retz3_color_table [regno][2];
	*transp = retz3_color_table [regno][3];
	return 0;
}


/*
 *    (Un)Blank the screen
 */

void retz3_blank(int blank)
{
	short i;

	if (blank)
		for (i = 0; i < 256; i++){
			reg_w(VDAC_ADDRESS_W, i);
			reg_w(VDAC_DATA, 0);
			reg_w(VDAC_DATA, 0);
			reg_w(VDAC_DATA, 0);
		}
	else
		for (i = 0; i < 256; i++){
			reg_w(VDAC_ADDRESS_W, i);
			reg_w(VDAC_DATA, retz3_color_table [i][0] >> 2);
			reg_w(VDAC_DATA, retz3_color_table [i][1] >> 2);
			reg_w(VDAC_DATA, retz3_color_table [i][2] >> 2);
		}
}


static void retz3_bitblt (struct fb_var_screeninfo *var,
			  unsigned short srcx, unsigned short srcy,
			  unsigned short destx, unsigned short desty,
			  unsigned short width, unsigned short height,
			  unsigned short cmd, unsigned short mask)
{

	volatile unsigned long *acm = (unsigned long *) (z3_mem + ACM_OFFSET);
	unsigned long *pattern = (unsigned long *)(z3_fbmem + PAT_MEM_OFF);

	unsigned short mod;
	unsigned long tmp;
	unsigned long pat, src, dst;
	unsigned char blt_status;

	int i, xres_virtual = var->xres_virtual;
	short bpp = (var->bits_per_pixel & 0xff);

	if (bpp < 8)
		bpp = 8;

	tmp = mask | (mask << 16);

#if 0
	/*
	 * Check for blitter finished before we start messing with the
	 * pattern.
	 */
	do{
		blt_status = *(((volatile unsigned char *)acm) +
			       (ACM_START_STATUS + 2));
	}while ((blt_status & 1) == 0);
#endif

	i = 0;
	do{
		*pattern++ = tmp;
	}while(i++ < bpp/4);

	tmp = cmd << 8;
	*(acm + ACM_RASTEROP_ROTATION/4) = tmp;

	mod = 0xc0c2;

	pat = 8 * PAT_MEM_OFF;
	dst = bpp * (destx + desty * xres_virtual);

	/*
	 * Source is not set for clear.
	 */
	if ((cmd != Z3BLTclear) && (cmd != Z3BLTset)) {
		src = bpp * (srcx + srcy * xres_virtual);

		if (destx > srcx) {
			mod &= ~0x8000;
			src += bpp * (width - 1);
			dst += bpp * (width - 1);
			pat += bpp * 2;
		}
		if (desty > srcy) {
			mod &= ~0x4000;
			src += bpp * (height - 1) * xres_virtual;
			dst += bpp * (height - 1) * xres_virtual;
			pat += bpp * 4;
		}

		*(acm + ACM_SOURCE/4) = cpu_to_le32(src);
	}

	*(acm + ACM_PATTERN/4) = cpu_to_le32(pat);

	*(acm + ACM_DESTINATION/4) = cpu_to_le32(dst);

	tmp = mod << 16;
	*(acm + ACM_CONTROL/4) = tmp;

	tmp  = width | (height << 16);

	*(acm + ACM_BITMAP_DIMENSION/4) = cpu_to_le32(tmp);

	*(((volatile unsigned char *)acm) + ACM_START_STATUS) = 0x00;
	*(((volatile unsigned char *)acm) + ACM_START_STATUS) = 0x01;

	/*
	 * No reason to wait for the blitter to finish, it is better
	 * just to check if it has finished before we use it again.
	 */
#if 1
#if 0
	while ((*(((volatile unsigned char *)acm) +
		  (ACM_START_STATUS + 2)) & 1) == 0);
#else
	do{
		blt_status = *(((volatile unsigned char *)acm) +
			       (ACM_START_STATUS + 2));
	}
	while ((blt_status & 1) == 0);
#endif
#endif
}

#if 0
/*
 * Move cursor to x, y
 */
static void retz3_MoveCursor (unsigned short x, unsigned short y)
{
	/* Guess we gotta deal with the cursor at some point */
}
#endif

/* -------------------- Interfaces to hardware functions -------------------- */


static struct fb_hwswitch retz3_switch = {
	retz3_init, retz3_encode_fix, retz3_decode_var, retz3_encode_var,
	retz3_getcolreg, retz3_setcolreg, retz3_blank
};


/* -------------------- Generic routines ------------------------------------ */


/*
 *    Fill the hardware's `par' structure.
 */

static void retz3fb_get_par(struct retz3fb_par *par)
{
	if (current_par_valid)
		*par = current_par;
	else
		fbhw->decode_var(&retz3fb_default, par);
}


static void retz3fb_set_par(struct retz3fb_par *par)
{
	current_par = *par;
	current_par_valid = 1;
}


static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
	int err, activate;
	struct retz3fb_par par;

	if ((err = fbhw->decode_var(var, &par)))
		return err;
	activate = var->activate;

	/* XXX ... what to do about isactive ? */

	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
		retz3fb_set_par(&par);
	fbhw->encode_var(var, &par);
	var->activate = activate;

	retz3_set_video(var, &current_par);

	return 0;
}


static void do_install_cmap(int con, struct fb_info *info)
{
	if (con != currcon)
		return;
	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
			    fbhw->setcolreg, info);
	else
		fb_set_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
					    &fb_display[con].var, 1,
					    fbhw->setcolreg, info);
}


/*
 *    Open/Release the frame buffer device
 */

static int retz3fb_open(struct fb_info *info)
{
	/*
	 * Nothing, only a usage count for the moment
	 */

	MOD_INC_USE_COUNT;
	return 0;
}

static int retz3fb_release(struct fb_info *info)
{
	MOD_DEC_USE_COUNT;
	return 0;
}


/*
 *    Get the Fixed Part of the Display
 */

static int retz3fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info)
{
	struct retz3fb_par par;
	int error = 0;

	if (con == -1)
		retz3fb_get_par(&par);
	else
		error = fbhw->decode_var(&fb_display[con].var, &par);
	return(error ? error : fbhw->encode_fix(fix, &par));
}


/*
 *    Get the User Defined Part of the Display
 */

static int retz3fb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	struct retz3fb_par par;
	int error = 0;

	if (con == -1) {
		retz3fb_get_par(&par);
		error = fbhw->encode_var(var, &par);
	} else
		*var = fb_display[con].var;
	return error;
}


#if 1
static void retz3fb_set_disp(int con, struct fb_info *info)
{
	struct fb_fix_screeninfo fix;
	struct display *display;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	retz3fb_get_fix(&fix, con, info);

	if (con == -1)
		con = 0;

	display->screen_base = fix.smem_start;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse = z3fb_inverse;
	switch (display->var.bits_per_pixel) {
#ifdef CONFIG_FBCON_CFB8
	case 8:
		if (display->var.accel_flags & FB_ACCELF_TEXT) {
		    display->dispsw = &fbcon_retz3_8;
#warning FIXME: We should reinit the graphics engine here
		} else
		    display->dispsw = &fbcon_cfb8;
		break;
#endif
#ifdef CONFIG_FBCON_CFB16
	case 16:
		display->dispsw = &fbcon_cfb16;
		break;
#endif
	default:
		display->dispsw = NULL;
		break;
	}
}
#endif

/*
 *    Set the User Defined Part of the Display
 */

static int retz3fb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	int err, oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel;
	struct display *display;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

#if 0
	if (con == -1)
		con = 0;
#endif

	if ((err = do_fb_set_var(var, con == currcon)))
		return err;
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldxres = display->var.xres;
		oldyres = display->var.yres;
		oldvxres = display->var.xres_virtual;
		oldvyres = display->var.yres_virtual;
		oldbpp = display->var.bits_per_pixel;
		oldaccel = display->var.accel_flags;
		display->var = *var;

		if (oldxres != var->xres || oldyres != var->yres ||
		    oldvxres != var->xres_virtual ||
		    oldvyres != var->yres_virtual ||
		    oldbpp != var->bits_per_pixel ||
		    oldaccel != var->accel_flags) {

			struct fb_fix_screeninfo fix;
			retz3fb_get_fix(&fix, con, info);

			display->screen_base = fix.smem_start;
			display->visual = fix.visual;
			display->type = fix.type;
			display->type_aux = fix.type_aux;
			display->ypanstep = fix.ypanstep;
			display->ywrapstep = fix.ywrapstep;
			display->line_length = fix.line_length;
			display->can_soft_blank = 1;
			display->inverse = z3fb_inverse;
			switch (display->var.bits_per_pixel) {
#ifdef CONFIG_FBCON_CFB8
			case 8:
				if (var->accel_flags & FB_ACCELF_TEXT) {
					display->dispsw = &fbcon_retz3_8;
#warning FIXME: We should reinit the graphics engine here
				} else
					display->dispsw = &fbcon_cfb8;
				break;
#endif
#ifdef CONFIG_FBCON_CFB16
			case 16:
				display->dispsw = &fbcon_cfb16;
				break;
#endif
			default:
				display->dispsw = NULL;
				break;
			}
/*
	retz3fb_set_disp(con, info);
*/
			if (fb_info.changevar)
				(*fb_info.changevar)(con);
		}

		if (oldbpp != var->bits_per_pixel) {
			if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
				return err;
			do_install_cmap(con, info);
		}
	}
	return 0;
}


/*
 *    Get the Colormap
 */

static int retz3fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	if (con == currcon) /* current console? */
		return(fb_get_cmap(cmap, &fb_display[con].var, kspc,
				   fbhw->getcolreg, info));
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			     cmap, kspc ? 0 : 2);
	return 0;
}


/*
 *    Set the Colormap
 */

static int retz3fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {       /* no colormap allocated? */
		if ((err = fb_alloc_cmap(&fb_display[con].cmap,
					 1<<fb_display[con].var.bits_per_pixel,
					 0)))
			return err;
	}
	if (con == currcon)              /* current console? */
		return(fb_set_cmap(cmap, &fb_display[con].var, kspc,
				   fbhw->setcolreg, info));
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}


/*
 *    Pan or Wrap the Display
 *
 *    This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */

static int retz3fb_pan_display(struct fb_var_screeninfo *var, int con,
				struct fb_info *info)
{
	return -EINVAL;
}


/*
 *    RetinaZ3 Frame Buffer Specific ioctls
 */

static int retz3fb_ioctl(struct inode *inode, struct file *file,
                         unsigned int cmd, unsigned long arg, int con,
                         struct fb_info *info)
{
	return -EINVAL;
}


static struct fb_ops retz3fb_ops = {
	retz3fb_open, retz3fb_release, retz3fb_get_fix, retz3fb_get_var,
	retz3fb_set_var, retz3fb_get_cmap, retz3fb_set_cmap,
	retz3fb_pan_display, retz3fb_ioctl
};


__initfunc(void retz3fb_setup(char *options, int *ints))
{
	char *this_opt;

	fb_info.fontname[0] = '\0';

	if (!options || !*options)
		return;

	for (this_opt = strtok(options, ","); this_opt;
	     this_opt = strtok(NULL, ",")){
		if (!strcmp(this_opt, "inverse")) {
			z3fb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5))
			strcpy(fb_info.fontname, this_opt+5);
		else
			z3fb_mode = get_video_mode(this_opt);
	}
}


/*
 *    Initialization
 */

__initfunc(unsigned long retz3fb_init(unsigned long mem_start))
{
	int err;
	unsigned long board_addr, board_size;
	unsigned int key;
	const struct ConfigDev *cd;

	struct retz3fb_par par;

	if (!(key = zorro_find(ZORRO_PROD_MACROSYSTEMS_RETINA_Z3, 0, 0)))
		return mem_start;

	cd = zorro_get_board (key);
	zorro_config_board (key, 0);
	board_addr = (unsigned long)cd->cd_BoardAddr;
	board_size = (unsigned long)cd->cd_BoardSize;

	z3_mem = kernel_map (board_addr, board_size,
			     KERNELMAP_NOCACHE_SER, &mem_start);

	z3_regs = (char*) z3_mem;
	z3_fbmem = z3_mem + VIDEO_MEM_OFFSET;

	/* Get memory size - for now we asume its a 4MB board */

	z3_size = 0x00400000; /* 4 MB */

	fbhw = &retz3_switch;

	fbhw->init();

	strcpy(fb_info.modename, retz3fb_name);
	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &retz3fb_ops;
	fb_info.disp = &disp;
	fb_info.switch_con = &z3fb_switch;
	fb_info.updatevar = &z3fb_updatevar;
	fb_info.blank = &z3fb_blank;

	err = register_framebuffer(&fb_info);
	if (err < 0)
		return mem_start;

	if (z3fb_mode == -1)
		retz3fb_default = retz3fb_predefined[0].var;

	fbhw->decode_var(&retz3fb_default, &par);
	fbhw->encode_var(&retz3fb_default, &par);

	do_fb_set_var(&retz3fb_default, 0);
	retz3fb_get_var(&disp.var, -1, &fb_info);

	retz3fb_set_disp(-1, &fb_info);

	do_install_cmap(0, &fb_info);

	printk("fb%d: %s frame buffer device, using %ldK of video memory\n",
	       GET_FB_IDX(fb_info.node), fb_info.modename, z3_size>>10);

	/* TODO: This driver cannot be unloaded yet */
	MOD_INC_USE_COUNT;

	return mem_start;
}


static int z3fb_switch(int con, struct fb_info *info)
{
	/* Do we have to save the colormap? */
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap,
			    &fb_display[currcon].var, 1, fbhw->getcolreg,
			    info);

	do_fb_set_var(&fb_display[con].var, 1);
	currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	return 0;
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */

static int z3fb_updatevar(int con, struct fb_info *info)
{
	return 0;
}


/*
 *    Blank the display.
 */

static void z3fb_blank(int blank, struct fb_info *info)
{
	fbhw->blank(blank);
}


/*
 *    Get a Video Mode
 */

__initfunc(static int get_video_mode(const char *name))
{
	short i;

	for (i = 0; i <= NUM_TOTAL_MODES; i++)
		if (!strcmp(name, retz3fb_predefined[i].name)){
			retz3fb_default = retz3fb_predefined[i].var;
			return i;
		}
	return -1;
}


#ifdef MODULE
int init_module(void)
{
	return(retz3fb_init(NULL));
}

void cleanup_module(void)
{
	/*
	 * Not reached because the usecount will never
	 * be decremented to zero
	 */
	unregister_framebuffer(&fb_info);
	/* TODO: clean up ... */
}
#endif


/*
 *  Text console acceleration
 */

#ifdef CONFIG_FBCON_CFB8
static void fbcon_retz3_8_bmove(struct display *p, int sy, int sx, int dy, int dx,
	                int height, int width)
{
	int fontwidth = p->fontwidth;

	sx *= fontwidth;
	dx *= fontwidth;
	width *= fontwidth;

	retz3_bitblt(&p->var,
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)dx,
		     (unsigned short)(dy*p->fontheight),
		     (unsigned short)width,
		     (unsigned short)(height*p->fontheight),
		     Z3BLTcopy,
		     0xffff);
}

static void fbcon_retz3_8_clear(struct vc_data *conp, struct display *p, int
			sy, int sx, int height, int width)
{
	unsigned short col;
	int fontwidth = p->fontwidth;

	sx *= fontwidth;
	width *= fontwidth;

	col = attr_bgcol_ec(p, conp);
	col &= 0xff;
	col |= (col << 8);

	retz3_bitblt(&p->var,
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)sx,
		     (unsigned short)(sy*p->fontheight),
		     (unsigned short)width,
		     (unsigned short)(height*p->fontheight),
		     Z3BLTset,
		     col);
}

static struct display_switch fbcon_retz3_8 = {
    fbcon_cfb8_setup, fbcon_retz3_8_bmove, fbcon_retz3_8_clear,
    fbcon_cfb8_putc, fbcon_cfb8_putcs, fbcon_cfb8_revc
};
#endif
