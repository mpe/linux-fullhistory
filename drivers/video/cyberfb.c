/*
* linux/drivers/video/cyberfb.c -- CyberVision64 frame buffer device
* $Id: cyberfb.c,v 1.6 1998/09/11 04:54:58 abair Exp $
*
*    Copyright (C) 1998 Alan Bair
*
* This file is based on two CyberVision64 frame buffer device drivers
*
* The second CyberVision64 frame buffer device (cvision.c cvision_core.c):
*
*   Copyright (c) 1997 Antonio Santos
*
* Released as a patch to 2.1.35, but never included in the source tree.
* This is based on work from the NetBSD CyberVision64 frame buffer driver 
* and support files (grf_cv.c, grf_cvreg.h, ite_cv.c):
* Permission to use the source of this driver was obtained from the
* author Michael Teske by Alan Bair.
*
*   Copyright (c) 1995 Michael Teske
*
* The first CyberVision64 frame buffer device (cyberfb.c):
*
*    Copyright (C) 1996 Martin Apel
*                       Geert Uytterhoeven
*
* Which is based on the Amiga frame buffer device (amifb.c):
*
*    Copyright (C) 1995 Geert Uytterhoeven
*
*
* History:
*   - 22 Dec 95: Original version by Martin Apel
*   - 05 Jan 96: Geert: integration into the current source tree
*   - 01 Aug 98: Alan: Merge in code from cvision.c and cvision_core.c
* $Log: cyberfb.c,v $
* Revision 1.6  1998/09/11 04:54:58  abair
* Update for 2.1.120 change in include file location.
* Clean up for public release.
*
* Revision 1.5  1998/09/03 04:27:13  abair
* Move cv64_load_video_mode to cyber_set_video so a new video mode is install
* with each change of the 'var' data.
*
* Revision 1.4  1998/09/01 00:31:17  abair
* Put in a set of default 8,16,24 bpp modes and map cyber8,16 to them.
* Update operations with 'par' to handle a more complete set of parameter
* values for encode/decode process.
*
* Revision 1.3  1998/08/31 21:31:33  abair
* Swap 800x490 for 640x480 video mode and more cleanup.
* Abandon idea to resurrect "custom" mode setting via kernel opts,
* instead work on making use of fbset program to do this.
*
* Revision 1.2  1998/08/31 06:17:08  abair
* Make updates for changes in cyberfb.c released in 2.1.119
* and do some cleanup of the code.
*
* Revision 1.1  1998/08/29 18:38:31  abair
* Initial revision
*
* Revision 1.3  1998/08/17 06:21:53  abair
* Remove more redundant code after merging in cvision_core.c
* Set blanking by colormap to pale red to detect this vs trying to
* use video blanking. More formating to Linux code style.
*
* Revision 1.2  1998/08/15 17:51:37  abair
* Added cvision_core.c code from 2.1.35 patches.
* Changed to compile correctly and switch to using initialization
* code. Added debugging and dropping of duplicate code.
*
*
*
* This file is subject to the terms and conditions of the GNU General Public
* License.  See the file COPYING in the main directory of this archive
* for more details.
*/


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/zorro.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/amigahw.h>
#include <asm/io.h>

#include "cyberfb.h"
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>

/*#define CYBERFBDEBUG*/
#ifdef CYBERFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
static void cv64_dump(void);
#else
#define DPRINTK(fmt, args...)
#endif

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

#define wb_64(reg,dat) (*((unsigned char volatile *)CyberRegs + reg) = dat)

struct cyberfb_par {
	struct fb_var_screeninfo var;
	__u32 type;
	__u32 type_aux;
	__u32 visual;
	__u32 line_length;
};

static struct cyberfb_par current_par;

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

int (*encode_fix)(struct fb_fix_screeninfo *fix, struct cyberfb_par *par);
int (*decode_var)(struct fb_var_screeninfo *var, struct cyberfb_par *par);
int (*encode_var)(struct fb_var_screeninfo *var, struct cyberfb_par *par);
int (*getcolreg)(u_int regno, u_int *red, u_int *green, u_int *blue,
		 u_int *transp, struct fb_info *info);
int (*setcolreg)(u_int regno, u_int red, u_int green, u_int blue,
		 u_int transp, struct fb_info *info);
void (*blank)(int blank);
} *fbhw;


/*
*    Frame Buffer Name
*/

static char cyberfb_name[16] = "Cybervision";


/*
*    Cybervision Graphics Board
*/

static unsigned char Cyber_colour_table [256][3];
static unsigned long CyberMem;
static unsigned long CyberSize;
static volatile char *CyberRegs;
static unsigned long CyberMem_phys;
static unsigned long CyberRegs_phys;
/* From cvision.c  for cvision_core.c */
static unsigned long cv64_mem;
static unsigned long cv64_fbmem;
static volatile char *cv64_regs;
static unsigned long cv64_size;
#if 0
static int cvision_custom_mode = 0;
static int hbs, hbe, hss, hse, ht, vbs, vbe, vss, vse, vt;
#endif

/*
*    Predefined Video Modes
*/

static struct fb_videomode cyberfb_predefined[] __initdata = {
	{ "640x480-8", {		/* Default 8 BPP mode (cyber8) */
		640, 480, 640, 480, 0, 0, 8, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCELF_TEXT, 39722, 40, 24, 32, 11, 96, 2,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, 
		FB_VMODE_NONINTERLACED
	}}, 
	{ "640x480-16", {		/* Default 16 BPP mode (cyber16) */
		640, 480, 640, 480, 0, 0, 16, 0,
		{11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCELF_TEXT, 39722, 40, 24, 32, 11, 96, 2,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, 
		FB_VMODE_NONINTERLACED
	}}, 
	{ "640x480-24", {		/* Default 24 BPP mode */
		640, 480, 640, 480, 0, 0, 24, 0,
		{16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCELF_TEXT, 39722, 40, 24, 32, 11, 96, 2,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, 
		FB_VMODE_NONINTERLACED
	}}, 
	{ "800x490-8", {		/* Cybervision 8 bpp */
		/* NO Acceleration */
		800, 490, 800, 490, 0, 0, 8, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCEL_NONE, 33333, 80, 24, 23, 1, 56, 8,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED
	}},
/* I can't test these with my monitor, but I suspect they will
 * be OK, since Antonio Santos indicated he had tested them in
 * his system.
 */
	{ "800x600-8", {		/* Cybervision 8 bpp */
		800, 600, 800, 600, 0, 0, 8, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCELF_TEXT, 27778, 64, 24, 22, 1, 72, 2,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED
	}},
	{ "1024x768-8", {		/* Cybervision 8 bpp */
		1024, 768, 1024, 768, 0, 0, 8, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCELF_TEXT, 16667, 224, 72, 60, 12, 168, 4,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED
	}},
	{ "1152x886-8", {		/* Cybervision 8 bpp */
		1152, 886, 1152, 886, 0, 0, 8, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCELF_TEXT, 15873, 184, 40, 24, 1, 56, 16,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED
	}},
	{ "1280x1024-8", {	/* Cybervision 8 bpp */
		1280, 1024, 1280, 1024, 0, 0, 8, 0,
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
		0, 0, -1, -1, FB_ACCELF_TEXT, 16667, 256, 48, 50, 12, 72, 4,
		FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_INTERLACED
	}}
};

#define NUM_TOTAL_MODES    arraysize(cyberfb_predefined)

static int Cyberfb_inverse = 0;

/*
*    Some default modes
*/

#define CYBER8_DEFMODE     (0)
#define CYBER16_DEFMODE    (1)

static struct fb_var_screeninfo cyberfb_default;

/*
*    Interface used by the world
*/

void cyberfb_setup(char *options, int *ints);

static int cyberfb_open(struct fb_info *info, int user);
static int cyberfb_release(struct fb_info *info, int user);
static int cyberfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info);
static int cyberfb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int cyberfb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int cyberfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info);
static int cyberfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info);
static int cyberfb_pan_display(struct fb_var_screeninfo *var, int con,
			       struct fb_info *info);
static int cyberfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			 u_long arg, int con, struct fb_info *info);

/*
*    Interface to the low level console driver
*/

void cyberfb_init(void);
static int Cyberfb_switch(int con, struct fb_info *info);
static int Cyberfb_updatevar(int con, struct fb_info *info);
static void Cyberfb_blank(int blank, struct fb_info *info);

/*
*    Text console acceleration
*/

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_cyber8;
#endif

/*
*    Accelerated Functions used by the low level console driver
*/

static void Cyber_WaitQueue(u_short fifo);
static void Cyber_WaitBlit(void);
static void Cyber_BitBLT(u_short curx, u_short cury, u_short destx,
			 u_short desty, u_short width, u_short height,
			 u_short mode);
static void Cyber_RectFill(u_short x, u_short y, u_short width, u_short height,
			   u_short mode, u_short color);
static void Cyber_MoveCursor(u_short x, u_short y);

/*
*   Hardware Specific Routines
*/

static int Cyber_init(void);
static int Cyber_encode_fix(struct fb_fix_screeninfo *fix,
			    struct cyberfb_par *par);
static int Cyber_decode_var(struct fb_var_screeninfo *var,
			    struct cyberfb_par *par);
static int Cyber_encode_var(struct fb_var_screeninfo *var,
			    struct cyberfb_par *par);
static int Cyber_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info);
static int Cyber_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info);
static void Cyber_blank(int blank);

/*
*    Internal routines
*/

static void cyberfb_get_par(struct cyberfb_par *par);
static void cyberfb_set_par(struct cyberfb_par *par);
static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive);
static void do_install_cmap(int con, struct fb_info *info);
static void cyberfb_set_disp(int con, struct fb_info *info);
static int get_video_mode(const char *name);

/* For cvision_core.c */
static unsigned short cv64_compute_clock(unsigned long);
static int cv_has_4mb (volatile caddr_t);
static void cv64_board_init (void);
static void cv64_load_video_mode (struct fb_var_screeninfo *);


/* -------------------- Hardware specific routines ------------------------- */


/*
*    Initialization
*
*    Set the default video mode for this chipset. If a video mode was
*    specified on the command line, it will override the default mode.
*/

static int Cyber_init(void)
{
	int i;
	volatile u_long *CursorBase;
	DPRINTK("ENTER\n");

/* Init local cmap as greyscale levels */
	for (i = 0; i < 256; i++) {
		Cyber_colour_table [i][0] = i;
		Cyber_colour_table [i][1] = i;
		Cyber_colour_table [i][2] = i;
	}

/* Initialize the board and determine fbmem size */
	cv64_board_init (); 
#ifdef CYBERFBDEBUG
	DPRINTK("Register state after initing board\n");
	cv64_dump();
#endif
/* Clear framebuffer memory */
	DPRINTK("Clear framebuffer memory\n");
	memset ((char *) cv64_fbmem, 0, cv64_size);

/* Disable hardware cursor */
	DPRINTK("Disable HW cursor\n");
	wb_64(S3_CRTC_ADR, S3_REG_LOCK2);
	wb_64(S3_CRTC_DATA, 0xa0);
	wb_64(S3_CRTC_ADR, S3_HGC_MODE);
	wb_64(S3_CRTC_DATA, 0x00);
	wb_64(S3_CRTC_ADR, S3_HWGC_DX);
	wb_64(S3_CRTC_DATA, 0x00);
	wb_64(S3_CRTC_ADR, S3_HWGC_DY);
	wb_64(S3_CRTC_DATA, 0x00);

	CyberSize = cv64_size;

/* Initialize hardware cursor */
	DPRINTK("Init HW cursor\n");
	CursorBase = (u_long *)((char *)(CyberMem) + CyberSize - 0x400);
	for (i=0; i < 8; i++)
	{
		*(CursorBase  +(i*4)) = 0xffffff00;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}
	for (i=8; i < 64; i++)
	{
		*(CursorBase  +(i*4)) = 0xffff0000;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}

	Cyber_setcolreg (255, 56<<8, 100<<8, 160<<8, 0, NULL /* unused */);
	Cyber_setcolreg (254, 0, 0, 0, 0, NULL /* unused */);

	DPRINTK("EXIT\n");
	return 0;
}


/*
*    This function should fill in the `fix' structure based on the
*    values in the `par' structure.
*/

static int Cyber_encode_fix(struct fb_fix_screeninfo *fix,
			    struct cyberfb_par *par)
{
	DPRINTK("ENTER\n");
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, cyberfb_name);
	fix->smem_start = (char*) CyberMem_phys;
	fix->smem_len = CyberSize;
	fix->mmio_start = (char*) CyberRegs_phys;
	fix->mmio_len = 0x10000;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	if (par->var.bits_per_pixel == 15 || par->var.bits_per_pixel == 16 ||
	    par->var.bits_per_pixel == 24 || par->var.bits_per_pixel == 32) {
		fix->visual = FB_VISUAL_DIRECTCOLOR;
	} else {
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	}

	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = 0;
	fix->accel = FB_ACCEL_S3_TRIO64;

	DPRINTK("EXIT\n");
	return(0);
}


/*
*    Fill the `par' structure based on the values in `var'.
*    TODO: Verify and adjust values, return -EINVAL if bad.
*/

static int Cyber_decode_var(struct fb_var_screeninfo *var,
			    struct cyberfb_par *par)
{
	DPRINTK("ENTER\n");
	par->var.xres = var->xres;
	par->var.yres = var->yres;
	par->var.xres_virtual = var->xres_virtual;
	par->var.yres_virtual = var->yres_virtual;
	par->var.xoffset = var->xoffset;
	par->var.yoffset = var->yoffset;
	par->var.bits_per_pixel = var->bits_per_pixel;
	par->var.grayscale = var->grayscale;
	par->var.red = var->red;
	par->var.green = var->green;
	par->var.blue = var->blue;
	par->var.transp = var->transp;
	par->var.nonstd = var->nonstd;
	par->var.activate = var->activate;
	par->var.height = var->height;
	par->var.width = var->width;
	if (var->accel_flags & FB_ACCELF_TEXT) {
		par->var.accel_flags = FB_ACCELF_TEXT;
	} else {
		par->var.accel_flags = 0;
	}
	par->var.pixclock = var->pixclock;
	par->var.left_margin = var->left_margin;
	par->var.right_margin = var->right_margin;
	par->var.upper_margin = var->upper_margin;
	par->var.lower_margin = var->lower_margin;
	par->var.hsync_len = var->hsync_len;
	par->var.vsync_len = var->vsync_len;
	par->var.sync = var->sync;
	par->var.vmode = var->vmode;
	DPRINTK("EXIT\n");
	return(0);
}

/*
*    Fill the `var' structure based on the values in `par' and maybe
*    other values read out of the hardware.
*/

static int Cyber_encode_var(struct fb_var_screeninfo *var,
			    struct cyberfb_par *par)
{
	DPRINTK("ENTER\n");
	var->xres = par->var.xres;
	var->yres = par->var.yres;
	var->xres_virtual = par->var.xres_virtual;
	var->yres_virtual = par->var.yres_virtual;
	var->xoffset = par->var.xoffset;
	var->yoffset = par->var.yoffset;

	var->bits_per_pixel = par->var.bits_per_pixel;
	var->grayscale = par->var.grayscale;

	var->red = par->var.red;
	var->green = par->var.green;
	var->blue = par->var.blue;
	var->transp = par->var.transp;

	var->nonstd = par->var.nonstd;
	var->activate = par->var.activate;

	var->height = par->var.height;
	var->width = par->var.width;

	var->accel_flags = par->var.accel_flags;

	var->pixclock = par->var.pixclock;
	var->left_margin = par->var.left_margin;
	var->right_margin = par->var.right_margin;
	var->upper_margin = par->var.upper_margin;
	var->lower_margin = par->var.lower_margin;
	var->hsync_len = par->var.hsync_len;
	var->vsync_len = par->var.vsync_len;
	var->sync = par->var.sync;
	var->vmode = par->var.vmode;
	
	DPRINTK("EXIT\n");
	return(0);
}


/*
*    Set a single color register. Return != 0 for invalid regno.
*/

static int Cyber_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	/*DPRINTK("ENTER\n");*/
	if (regno > 255) {
		DPRINTK("EXIT - Register # > 255\n");
		return (1);
	}

	wb_64(0x3c8, (unsigned char) regno);

 	red >>= 10;
 	green >>= 10;
 	blue >>= 10;

	Cyber_colour_table [regno][0] = red;
	Cyber_colour_table [regno][1] = green;
	Cyber_colour_table [regno][2] = blue;

	wb_64(0x3c9, red);
	wb_64(0x3c9, green);
	wb_64(0x3c9, blue);

	/*DPRINTK("EXIT\n");*/
	return (0);
}


/*
*    Read a single color register and split it into
*    colors/transparent. Return != 0 for invalid regno.
*/

static int Cyber_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info)
{
	int t;

	/*DPRINTK("ENTER\n");*/
	if (regno > 255) {
		DPRINTK("EXIT - Register # > 255\n");
		return (1);
	}
	/* ARB This shifting & oring seems VERY strange */
 	t	= Cyber_colour_table [regno][0];
 	*red	= (t<<10) | (t<<4) | (t>>2);
 	t	= Cyber_colour_table [regno][1];
 	*green	= (t<<10) | (t<<4) | (t>>2);
 	t	= Cyber_colour_table [regno][2];
 	*blue	= (t<<10) | (t<<4) | (t>>2);
 	*transp = 0;
	/*DPRINTK("EXIT\n");*/
	return (0);
}


/*
*    (Un)Blank the screen
*    blank: 1 = zero fb cmap
*           0 = restore fb cmap from local cmap
*/

void Cyber_blank(int blank)
{
	int i;

	DPRINTK("ENTER\n");
#if 0
/* Blank by turning gfx off */
	gfx_on_off (1, cv64_regs);
#else
	if (blank) {
		for (i = 0; i < 256; i++) {
			wb_64(0x3c8, (unsigned char) i);
			/* ARB Pale red to detect this blanking method */
			wb_64(0x3c9, 48); 
			wb_64(0x3c9, 0);
			wb_64(0x3c9, 0);
		}
	} else {
		for (i = 0; i < 256; i++) {
			wb_64(0x3c8, (unsigned char) i);
			wb_64(0x3c9, Cyber_colour_table[i][0]);
			wb_64(0x3c9, Cyber_colour_table[i][1]);
			wb_64(0x3c9, Cyber_colour_table[i][2]);
		}
	}
#endif
	DPRINTK("EXIT\n");
}


/**************************************************************
 * We are waiting for "fifo" FIFO-slots empty
 */
static void Cyber_WaitQueue (u_short fifo)
{
	u_short status;

	DPRINTK("ENTER\n");
	do {
		status = *((u_short volatile *)(CyberRegs + S3_GP_STAT));
	}
	while (status & fifo);
	DPRINTK("EXIT\n");
}

/**************************************************************
 * We are waiting for Hardware (Graphics Engine) not busy
 */
static void Cyber_WaitBlit (void)
{
	u_short status;

	DPRINTK("ENTER\n");
	do {
		status = *((u_short volatile *)(CyberRegs + S3_GP_STAT));
	}
	while (status & S3_HDW_BUSY);
	DPRINTK("EXIT\n");
}

/**************************************************************
 * BitBLT - Through the Plane
 */
static void Cyber_BitBLT (u_short curx, u_short cury, u_short destx,
			  u_short desty, u_short width, u_short height,
			  u_short mode)
{
	u_short blitcmd = S3_BITBLT;

	DPRINTK("ENTER\n");
	/* Set drawing direction */
	/* -Y, X maj, -X (default) */
	if (curx > destx) {
		blitcmd |= 0x0020;  /* Drawing direction +X */
	} else {
		curx  += (width - 1);
		destx += (width - 1);
	}

	if (cury > desty) {
		blitcmd |= 0x0080;  /* Drawing direction +Y */
	} else {
		cury  += (height - 1);
		desty += (height - 1);
	}

	Cyber_WaitQueue (0x8000);

	*((u_short volatile *)(CyberRegs + S3_PIXEL_CNTL)) = 0xa000;
	*((u_short volatile *)(CyberRegs + S3_FRGD_MIX)) = (0x0060 | mode);

	*((u_short volatile *)(CyberRegs + S3_CUR_X)) = curx;
	*((u_short volatile *)(CyberRegs + S3_CUR_Y)) = cury;

	*((u_short volatile *)(CyberRegs + S3_DESTX_DIASTP)) = destx;
	*((u_short volatile *)(CyberRegs + S3_DESTY_AXSTP)) = desty;

	*((u_short volatile *)(CyberRegs + S3_MIN_AXIS_PCNT)) = height - 1;
	*((u_short volatile *)(CyberRegs + S3_MAJ_AXIS_PCNT)) = width  - 1;

	*((u_short volatile *)(CyberRegs + S3_CMD)) = blitcmd;
	DPRINTK("EXIT\n");
}

/**************************************************************
 * Rectangle Fill Solid
 */
static void Cyber_RectFill (u_short x, u_short y, u_short width,
			    u_short height, u_short mode, u_short color)
{
	u_short blitcmd = S3_FILLEDRECT;

	DPRINTK("ENTER\n");
	Cyber_WaitQueue (0x8000);

	*((u_short volatile *)(CyberRegs + S3_PIXEL_CNTL)) = 0xa000;
	*((u_short volatile *)(CyberRegs + S3_FRGD_MIX)) = (0x0020 | mode);

	*((u_short volatile *)(CyberRegs + S3_MULT_MISC)) = 0xe000;
	*((u_short volatile *)(CyberRegs + S3_FRGD_COLOR)) = color;

	*((u_short volatile *)(CyberRegs + S3_CUR_X)) = x;
	*((u_short volatile *)(CyberRegs + S3_CUR_Y)) = y;

	*((u_short volatile *)(CyberRegs + S3_MIN_AXIS_PCNT)) = height - 1;
	*((u_short volatile *)(CyberRegs + S3_MAJ_AXIS_PCNT)) = width  - 1;

	*((u_short volatile *)(CyberRegs + S3_CMD)) = blitcmd;
	DPRINTK("EXIT\n");
}

/**************************************************************
 * Move cursor to x, y
 */
static void Cyber_MoveCursor (u_short x, u_short y)
{
	DPRINTK("ENTER\n");
	*(CyberRegs + S3_CRTC_ADR)  = 0x39;
	*(CyberRegs + S3_CRTC_DATA) = 0xa0;

	*(CyberRegs + S3_CRTC_ADR)  = S3_HWGC_ORGX_H;
	*(CyberRegs + S3_CRTC_DATA) = (char)((x & 0x0700) >> 8);
	*(CyberRegs + S3_CRTC_ADR)  = S3_HWGC_ORGX_L;
	*(CyberRegs + S3_CRTC_DATA) = (char)(x & 0x00ff);

	*(CyberRegs + S3_CRTC_ADR)  = S3_HWGC_ORGY_H;
	*(CyberRegs + S3_CRTC_DATA) = (char)((y & 0x0700) >> 8);
	*(CyberRegs + S3_CRTC_ADR)  = S3_HWGC_ORGY_L;
	*(CyberRegs + S3_CRTC_DATA) = (char)(y & 0x00ff);
	DPRINTK("EXIT\n");
}


/* -------------------- Interfaces to hardware functions -------------------- */


static struct fb_hwswitch Cyber_switch = {
	Cyber_init, Cyber_encode_fix, Cyber_decode_var, Cyber_encode_var,
	Cyber_getcolreg, Cyber_setcolreg, Cyber_blank
};


/* -------------------- Generic routines ---------------------------------- */


/*
 *    Fill the hardware's `par' structure.
 */

static void cyberfb_get_par(struct cyberfb_par *par)
{
	DPRINTK("ENTER\n");
	if (current_par_valid) {
		*par = current_par;
	} else {
		fbhw->decode_var(&cyberfb_default, par);
	}
	DPRINTK("EXIT\n");
}


static void cyberfb_set_par(struct cyberfb_par *par)
{
	DPRINTK("ENTER\n");
	current_par = *par;
	current_par_valid = 1;
	DPRINTK("EXIT\n");
}


static void cyber_set_video(struct fb_var_screeninfo *var)
{

	/* Load the video mode defined by the 'var' data */
	cv64_load_video_mode (var);
#ifdef CYBERFBDEBUG
	DPRINTK("Register state after loading video mode\n");
	cv64_dump();
#endif
}


static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
	int err, activate;
	struct cyberfb_par par;

	DPRINTK("ENTER\n");
	if ((err = fbhw->decode_var(var, &par))) {
		DPRINTK("EXIT - decode_var failed\n");
		return(err);
	}
	activate = var->activate;
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
		cyberfb_set_par(&par);
	fbhw->encode_var(var, &par);
	var->activate = activate;

	cyber_set_video(var);
	DPRINTK("EXIT\n");
	return 0;
}


static void do_install_cmap(int con, struct fb_info *info)
{
	DPRINTK("ENTER\n");
	if (con != currcon) {
		DPRINTK("EXIT - Not current console\n");
		return;
	}
	if (fb_display[con].cmap.len) {
		DPRINTK("Use console cmap\n");
		fb_set_cmap(&fb_display[con].cmap, 1, fbhw->setcolreg, info);
	} else {
		DPRINTK("Use default cmap\n");
		fb_set_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			    1, fbhw->setcolreg, info);
	}
	DPRINTK("EXIT\n");
}


/*
 *  Open/Release the frame buffer device
 */

static int cyberfb_open(struct fb_info *info, int user)
{
	/*
	 * Nothing, only a usage count for the moment
	 */

	MOD_INC_USE_COUNT;
	return(0);
}

static int cyberfb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return(0);
}


/*
 *    Get the Fixed Part of the Display
 */

static int cyberfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info)
{
	struct cyberfb_par par;
	int error = 0;

	DPRINTK("ENTER\n");
	if (con == -1) {
		cyberfb_get_par(&par);
	} else {
		error = fbhw->decode_var(&fb_display[con].var, &par);
	}
	DPRINTK("EXIT\n");
	return(error ? error : fbhw->encode_fix(fix, &par));
}


/*
 *    Get the User Defined Part of the Display
 */

static int cyberfb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	struct cyberfb_par par;
	int error = 0;

	DPRINTK("ENTER\n");
	if (con == -1) {
		cyberfb_get_par(&par);
		error = fbhw->encode_var(var, &par);
		disp.var = *var;   /* ++Andre: don't know if this is the right place */
	} else {
		*var = fb_display[con].var;
	}

	DPRINTK("EXIT\n");
	return(error);
}


static void cyberfb_set_disp(int con, struct fb_info *info)
{
	struct fb_fix_screeninfo fix;
	struct display *display;

	DPRINTK("ENTER\n");
	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	cyberfb_get_fix(&fix, con, info);
	if (con == -1)
		con = 0;
	display->screen_base = phys_to_virt ((unsigned long) fix.smem_start);
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse = Cyberfb_inverse;
	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	    case 8:
		if (display->var.accel_flags & FB_ACCELF_TEXT) {
		    display->dispsw = &fbcon_cyber8;
#warning FIXME: We should reinit the graphics engine here
		} else
		    display->dispsw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	    case 16:
		display->dispsw = &fbcon_cfb16;
		break;
#endif
	    default:
		display->dispsw = NULL;
		break;
	}
	DPRINTK("EXIT\n");
}


/*
 *    Set the User Defined Part of the Display
 */

static int cyberfb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	int err, oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel;

	DPRINTK("ENTER\n");
	if ((err = do_fb_set_var(var, con == currcon))) {
		DPRINTK("EXIT - do_fb_set_var failed\n");
		return(err);
	}
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldxres = fb_display[con].var.xres;
		oldyres = fb_display[con].var.yres;
		oldvxres = fb_display[con].var.xres_virtual;
		oldvyres = fb_display[con].var.yres_virtual;
		oldbpp = fb_display[con].var.bits_per_pixel;
		oldaccel = fb_display[con].var.accel_flags;
		fb_display[con].var = *var;
		if (oldxres != var->xres || oldyres != var->yres ||
		    oldvxres != var->xres_virtual ||
		    oldvyres != var->yres_virtual ||
		    oldbpp != var->bits_per_pixel ||
		    oldaccel != var->accel_flags) {
			cyberfb_set_disp(con, info);
			(*fb_info.changevar)(con);
			fb_alloc_cmap(&fb_display[con].cmap, 0, 0);
			do_install_cmap(con, info);
		}
	}
	var->activate = 0;
	DPRINTK("EXIT\n");
	return(0);
}


/*
 *    Get the Colormap
 */

static int cyberfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	DPRINTK("ENTER\n");
	if (con == currcon) { /* current console? */
		DPRINTK("EXIT - console is current console\n");
		return(fb_get_cmap(cmap, kspc, fbhw->getcolreg, info));
	} else if (fb_display[con].cmap.len) { /* non default colormap? */
		DPRINTK("Use console cmap\n");
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	} else {
		DPRINTK("Use default cmap\n");
		fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			     cmap, kspc ? 0 : 2);
	}
	DPRINTK("EXIT\n");
	return(0);
}


/*
 *    Set the Colormap
 */

static int cyberfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	int err;

	DPRINTK("ENTER\n");
	if (!fb_display[con].cmap.len) {       /* no colormap allocated? */
		if ((err = fb_alloc_cmap(&fb_display[con].cmap,
					 1<<fb_display[con].var.bits_per_pixel,
					 0))) {
			DPRINTK("EXIT - fb_alloc_cmap failed\n");
			return(err);
		}
	}
	if (con == currcon) {		 /* current console? */
		DPRINTK("EXIT - Current console\n");
		return(fb_set_cmap(cmap, kspc, fbhw->setcolreg, info));
	} else {
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	}
	DPRINTK("EXIT\n");
	return(0);
}


/*
 *    Pan or Wrap the Display
 *
 *    This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */

static int cyberfb_pan_display(struct fb_var_screeninfo *var, int con,
			       struct fb_info *info)
{
	return(-EINVAL);
}


/*
    *	 Cybervision Frame Buffer Specific ioctls
    */

static int cyberfb_ioctl(struct inode *inode, struct file *file,
			 u_int cmd, u_long arg, int con, struct fb_info *info)
{
	return(-EINVAL);
}


static struct fb_ops cyberfb_ops = {
	cyberfb_open, cyberfb_release, cyberfb_get_fix, cyberfb_get_var,
	cyberfb_set_var, cyberfb_get_cmap, cyberfb_set_cmap,
	cyberfb_pan_display, cyberfb_ioctl
};


__initfunc(void cyberfb_setup(char *options, int *ints))
{
	char *this_opt;
	DPRINTK("ENTER\n");

	fb_info.fontname[0] = '\0';

	if (!options || !*options) {
		DPRINTK("EXIT - no options\n");
		return;
	}

	for (this_opt = strtok(options, ","); this_opt;
	     this_opt = strtok(NULL, ",")) {
		if (!strcmp(this_opt, "inverse")) {
			Cyberfb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5)) {
			strcpy(fb_info.fontname, this_opt+5);
		} else if (!strcmp (this_opt, "cyber8")) {
			cyberfb_default = cyberfb_predefined[CYBER8_DEFMODE].var;
		} else if (!strcmp (this_opt, "cyber16")) {
			cyberfb_default = cyberfb_predefined[CYBER16_DEFMODE].var;
		} else get_video_mode(this_opt);
	}

	DPRINTK("default mode: xres=%d, yres=%d, bpp=%d\n",
		cyberfb_default.xres,
		cyberfb_default.yres,
		cyberfb_default.bits_per_pixel);
	DPRINTK("EXIT\n");
}

/*
 *    Initialization
 */

__initfunc(void cyberfb_init(void))
{
	struct cyberfb_par par;
	unsigned long board_addr;
	unsigned long board_size;
	const struct ConfigDev *cd;
	unsigned int CyberKey = 0;
	DPRINTK("ENTER\n");

	if (!(CyberKey = zorro_find(ZORRO_PROD_PHASE5_CYBERVISION64, 0, 0))) {
		DPRINTK("EXIT - zorro_find failed\n");
		return;
	}

	cd = zorro_get_board (CyberKey);
	zorro_config_board (CyberKey, 0);
	board_addr = (unsigned long)cd->cd_BoardAddr;
	board_size = (unsigned long)cd->cd_BoardSize;
	DPRINTK("board_addr=%08lx\n", board_addr);
	DPRINTK("board_size=%08lx\n", board_size);

	cv64_mem = ioremap(board_addr, board_size);
	cv64_regs = (volatile char *)(cv64_mem + 0x02000000);
	cv64_fbmem = cv64_mem + 0x01400000;
	DPRINTK("cv64_mem=%08lx cv64_regs=%08lx cv64_fbmem=%08lx\n",
		cv64_mem, (long unsigned int)cv64_regs, cv64_fbmem);

	CyberMem = cv64_fbmem;
	CyberRegs = cv64_regs;
	CyberMem_phys = board_addr + 0x01400000;
	CyberRegs_phys = CyberMem_phys + 0x00c00000;
	DPRINTK("CyberMem=%08lx CyberRegs=%08lx\n", CyberMem,
		(long unsigned int)CyberRegs);

#ifdef CYBERFBDEBUG
	DPRINTK("Register state just after mapping memory\n");
	cv64_dump();
#endif

	fbhw = &Cyber_switch;

	strcpy(fb_info.modename, cyberfb_name);
	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &cyberfb_ops;
	fb_info.disp = &disp;
	fb_info.switch_con = &Cyberfb_switch;
	fb_info.updatevar = &Cyberfb_updatevar;
	fb_info.blank = &Cyberfb_blank;

	fbhw->init();
	fbhw->decode_var(&cyberfb_default, &par);
	fbhw->encode_var(&cyberfb_default, &par);

	do_fb_set_var(&cyberfb_default, 1);
	cyberfb_get_var(&fb_display[0].var, -1, &fb_info);
	cyberfb_set_disp(-1, &fb_info);
	do_install_cmap(0, &fb_info);

	if (register_framebuffer(&fb_info) < 0) {
		DPRINTK("EXIT - register_framebuffer failed\n");
		return;
	}

	printk("fb%d: %s frame buffer device, using %ldK of video memory\n",
	       GET_FB_IDX(fb_info.node), fb_info.modename, CyberSize>>10);

	/* TODO: This driver cannot be unloaded yet */
	MOD_INC_USE_COUNT;
	DPRINTK("EXIT\n");
}


static int Cyberfb_switch(int con, struct fb_info *info)
{
        DPRINTK("ENTER\n");
	/* Do we have to save the colormap? */
	if (fb_display[currcon].cmap.len) {
		fb_get_cmap(&fb_display[currcon].cmap, 1, fbhw->getcolreg,
			    info);
	}

	do_fb_set_var(&fb_display[con].var, 1);
	currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	DPRINTK("EXIT\n");
	return(0);
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */

static int Cyberfb_updatevar(int con, struct fb_info *info)
{
	DPRINTK("Enter - Exit\n");
	return(0);
}


/*
 *    Blank the display.
 */

static void Cyberfb_blank(int blank, struct fb_info *info)
{
	DPRINTK("Enter\n");
	fbhw->blank(blank);
	DPRINTK("Exit\n");
}


/*
 *    Get a Video Mode
 */

__initfunc(static int get_video_mode(const char *name))
{
	int i;

	DPRINTK("ENTER\n");
	for (i = 0; i < NUM_TOTAL_MODES; i++) {
		if (!strcmp(name, cyberfb_predefined[i].name)) {
			cyberfb_default = cyberfb_predefined[i].var;
			DPRINTK("EXIT - Matched predefined mode\n");
			return(i);
		}
	}
	/* ++Andre: set cyberfb default mode */
	cyberfb_default = cyberfb_predefined[CYBER8_DEFMODE].var;
	DPRINTK("EXIT - Use default cyber8 mode\n");
	return(0);
}


/*
 *    Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static void fbcon_cyber8_bmove(struct display *p, int sy, int sx, int dy,
			       int dx, int height, int width)
{
	DPRINTK("ENTER\n");
	sx *= 8; dx *= 8; width *= 8;
	Cyber_BitBLT((u_short)sx, (u_short)(sy*fontheight(p)), (u_short)dx,
		     (u_short)(dy*fontheight(p)), (u_short)width,
		     (u_short)(height*fontheight(p)), (u_short)S3_NEW);
	DPRINTK("EXIT\n");
}

static void fbcon_cyber8_clear(struct vc_data *conp, struct display *p, int sy,
			       int sx, int height, int width)
{
	unsigned char bg;

	DPRINTK("ENTER\n");
	sx *= 8; width *= 8;
	bg = attr_bgcol_ec(p,conp);
	Cyber_RectFill((u_short)sx,
		       (u_short)(sy*fontheight(p)),
		       (u_short)width,
		       (u_short)(height*fontheight(p)),
		       (u_short)S3_NEW,
		       (u_short)bg);
	DPRINTK("EXIT\n");
}

static void fbcon_cyber8_putc(struct vc_data *conp, struct display *p, int c,
			      int yy, int xx)
{
	DPRINTK("ENTER\n");
	Cyber_WaitBlit();
	fbcon_cfb8_putc(conp, p, c, yy, xx);
	DPRINTK("EXIT\n");
}

static void fbcon_cyber8_putcs(struct vc_data *conp, struct display *p,
			       const unsigned short *s, int count,
			       int yy, int xx)
{
	DPRINTK("ENTER\n");
	Cyber_WaitBlit();
	fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
	DPRINTK("EXIT\n");
}

static void fbcon_cyber8_revc(struct display *p, int xx, int yy)
{
	DPRINTK("ENTER\n");
	Cyber_WaitBlit();
	fbcon_cfb8_revc(p, xx, yy);
	DPRINTK("EXIT\n");
}

static struct display_switch fbcon_cyber8 = {
   fbcon_cfb8_setup, fbcon_cyber8_bmove, fbcon_cyber8_clear, fbcon_cyber8_putc,
   fbcon_cyber8_putcs, fbcon_cyber8_revc, NULL, NULL, fbcon_cfb8_clear_margins,
   FONTWIDTH(8)
};
#endif


#ifdef MODULE
int init_module(void)
{
	cyberfb_init();
	return 0;
}

void cleanup_module(void)
{
	/* Not reached because the usecount will never be
	   decremented to zero */
	unregister_framebuffer(&fb_info);
	/* TODO: clean up ... */
}
#endif /* MODULE */

/*
 *
 * Low level initialization routines for the CyberVision64 graphics card
 *
 * Most of the following code is from cvision_core.c
 *
 */

#define MAXPIXELCLOCK 135000000 /* safety */

#ifdef CV_AGGRESSIVE_TIMING
long cv64_memclk = 55000000;
#else
long cv64_memclk = 50000000;
#endif

/*********************/

static unsigned char clocks[]={
  0x13, 0x61, 0x6b, 0x6d, 0x51, 0x69, 0x54, 0x69,
  0x4f, 0x68, 0x6b, 0x6b, 0x18, 0x61, 0x7b, 0x6c,
  0x51, 0x67, 0x24, 0x62, 0x56, 0x67, 0x77, 0x6a,
  0x1d, 0x61, 0x53, 0x66, 0x6b, 0x68, 0x79, 0x69,
  0x7c, 0x69, 0x7f, 0x69, 0x22, 0x61, 0x54, 0x65,
  0x56, 0x65, 0x58, 0x65, 0x67, 0x66, 0x41, 0x63,
  0x27, 0x61, 0x13, 0x41, 0x37, 0x62, 0x6b, 0x4d,
  0x23, 0x43, 0x51, 0x49, 0x79, 0x66, 0x54, 0x49,
  0x7d, 0x66, 0x34, 0x56, 0x4f, 0x63, 0x1f, 0x42,
  0x6b, 0x4b, 0x7e, 0x4d, 0x18, 0x41, 0x2a, 0x43,
  0x7b, 0x4c, 0x74, 0x4b, 0x51, 0x47, 0x65, 0x49,
  0x24, 0x42, 0x68, 0x49, 0x56, 0x47, 0x75, 0x4a,
  0x77, 0x4a, 0x31, 0x43, 0x1d, 0x41, 0x71, 0x49,
  0x53, 0x46, 0x29, 0x42, 0x6b, 0x48, 0x1f, 0x41,
  0x79, 0x49, 0x6f, 0x48, 0x7c, 0x49, 0x38, 0x43,
  0x7f, 0x49, 0x5d, 0x46, 0x22, 0x41, 0x53, 0x45,
  0x54, 0x45, 0x55, 0x45, 0x56, 0x45, 0x57, 0x45,
  0x58, 0x45, 0x25, 0x41, 0x67, 0x46, 0x5b, 0x45,
  0x41, 0x43, 0x78, 0x47, 0x27, 0x41, 0x51, 0x44,
  0x13, 0x21, 0x7d, 0x47, 0x37, 0x42, 0x71, 0x46,
  0x6b, 0x2d, 0x14, 0x21, 0x23, 0x23, 0x7d, 0x2f,
  0x51, 0x29, 0x61, 0x2b, 0x79, 0x46, 0x1d, 0x22,
  0x54, 0x29, 0x45, 0x27, 0x7d, 0x46, 0x7f, 0x46,
  0x4f, 0x43, 0x2f, 0x41, 0x1f, 0x22, 0x6a, 0x2b,
  0x6b, 0x2b, 0x5b, 0x29, 0x7e, 0x2d, 0x65, 0x44,
  0x18, 0x21, 0x5e, 0x29, 0x2a, 0x23, 0x45, 0x26,
  0x7b, 0x2c, 0x19, 0x21, 0x74, 0x2b, 0x75, 0x2b,
  0x51, 0x27, 0x3f, 0x25, 0x65, 0x29, 0x40, 0x25,
  0x24, 0x22, 0x41, 0x25, 0x68, 0x29, 0x42, 0x25,
  0x56, 0x27, 0x7e, 0x2b, 0x75, 0x2a, 0x1c, 0x21,
  0x77, 0x2a, 0x4f, 0x26, 0x31, 0x23, 0x6f, 0x29,
  0x1d, 0x21, 0x32, 0x23, 0x71, 0x29, 0x72, 0x29,
  0x53, 0x26, 0x69, 0x28, 0x29, 0x22, 0x75, 0x29,
  0x6b, 0x28, 0x1f, 0x21, 0x1f, 0x21, 0x6d, 0x28,
  0x79, 0x29, 0x2b, 0x22, 0x6f, 0x28, 0x59, 0x26,
  0x7c, 0x29, 0x7d, 0x29, 0x38, 0x23, 0x21, 0x21,
  0x7f, 0x29, 0x39, 0x23, 0x5d, 0x26, 0x75, 0x28,
  0x22, 0x21, 0x77, 0x28, 0x53, 0x25, 0x6c, 0x27,
  0x54, 0x25, 0x61, 0x26, 0x55, 0x25, 0x30, 0x22,
  0x56, 0x25, 0x63, 0x26, 0x57, 0x25, 0x71, 0x27,
  0x58, 0x25, 0x7f, 0x28, 0x25, 0x21, 0x74, 0x27,
  0x67, 0x26, 0x40, 0x23, 0x5b, 0x25, 0x26, 0x21,
  0x41, 0x23, 0x34, 0x22, 0x78, 0x27, 0x6b, 0x26,
  0x27, 0x21, 0x35, 0x22, 0x51, 0x24, 0x7b, 0x27,
  0x13, 0x1,  0x13, 0x1,  0x7d, 0x27, 0x4c, 0x9,
  0x37, 0x22, 0x5b, 0xb,  0x71, 0x26, 0x5c, 0xb,
  0x6b, 0xd,  0x47, 0x23, 0x14, 0x1,  0x4f, 0x9,
  0x23, 0x3,  0x75, 0x26, 0x7d, 0xf,  0x1c, 0x2,
  0x51, 0x9,  0x59, 0x24, 0x61, 0xb,  0x69, 0x25,
  0x79, 0x26, 0x34, 0x5,  0x1d, 0x2,  0x6b, 0x25,
  0x54, 0x9,  0x35, 0x5,  0x45, 0x7,  0x6d, 0x25,
  0x7d, 0x26, 0x16, 0x1,  0x7f, 0x26, 0x77, 0xd,
  0x4f, 0x23, 0x78, 0xd,  0x2f, 0x21, 0x27, 0x3,
  0x1f, 0x2,  0x59, 0x9,  0x6a, 0xb,  0x73, 0x25,
  0x6b, 0xb,  0x63, 0x24, 0x5b, 0x9,  0x20, 0x2,
  0x7e, 0xd,  0x4b, 0x7,  0x65, 0x24, 0x43, 0x22,
  0x18, 0x1,  0x6f, 0xb,  0x5e, 0x9,  0x70, 0xb,
  0x2a, 0x3,  0x33, 0x4,  0x45, 0x6,  0x60, 0x9,
  0x7b, 0xc,  0x19, 0x1,  0x19, 0x1,  0x7d, 0xc,
  0x74, 0xb,  0x50, 0x7,  0x75, 0xb,  0x63, 0x9,
  0x51, 0x7,  0x23, 0x2,  0x3f, 0x5,  0x1a, 0x1,
  0x65, 0x9,  0x2d, 0x3,  0x40, 0x5,  0x0,  0x0,
};

/* Console colors */
unsigned char cvconscolors[16][3] = {	/* background, foreground, hilite */
  /*  R     G     B  */
  {0x30, 0x30, 0x30},
  {0x00, 0x00, 0x00},
  {0x80, 0x00, 0x00},
  {0x00, 0x80, 0x00},
  {0x00, 0x00, 0x80},
  {0x80, 0x80, 0x00},
  {0x00, 0x80, 0x80},
  {0x80, 0x00, 0x80},
  {0xff, 0xff, 0xff},
  {0x40, 0x40, 0x40},
  {0xff, 0x00, 0x00},
  {0x00, 0xff, 0x00},
  {0x00, 0x00, 0xff},
  {0xff, 0xff, 0x00},
  {0x00, 0xff, 0xff},
  {0x00, 0x00, 0xff}
};

/* -------------------- Hardware specific routines ------------------------- */
#if 0
/* ARB Generates 100 usec delay */
inline void __cv_delay (unsigned long usecs)
{
	int k;
	
	for (k = 0; k < 1000; k++) {
		asm volatile ("nop");
	}
}
#endif

/* Wait while Graphics Engine is busy */
inline void GfxBusyWait (volatile caddr_t board)
{
	int test;
	DPRINTK("ENTER\n");
	
	do {
		test = vgar16 (board, ECR_GP_STAT);
		asm volatile ("nop");
	} while (test & (1 << 9));
	DPRINTK("EXIT\n");
}

/* Wait for any of the 8 Trio32 FIFOs to be free */ 
inline void GfxFifoWait (volatile caddr_t board)
{
	int test;
	DPRINTK("ENTER\n");
	
	do {
		test = vgar16 (board, ECR_GP_STAT);
	} while (test & 0x0f);
	DPRINTK("EXIT\n");
}

/* Read Attribute Controller Register=idx */
inline unsigned char RAttr (volatile caddr_t board, short idx)
{
	vgaw (board, ACT_ADDRESS_W, idx);
	udelay(100);
	/* __cv_delay (0); */
	return (vgar (board, ACT_ADDRESS_R));
}

/* Read Sequencer Register=idx */
inline unsigned char RSeq (volatile caddr_t board, short idx)
{
	vgaw (board, SEQ_ADDRESS, idx);
	return (vgar (board, SEQ_ADDRESS_R));
}

/* Read CRT Controller Register=idx */
inline unsigned char RCrt (volatile caddr_t board, short idx)
{
	vgaw (board, CRT_ADDRESS, idx);
	return (vgar (board, CRT_ADDRESS_R));
}

/* Read Graphics Controller Register=idx */
inline unsigned char RGfx (volatile caddr_t board, short idx)
{
	vgaw (board, GCT_ADDRESS, idx);
	return (vgar (board, GCT_ADDRESS_R));
}

/*
 * Special wakeup/passthrough registers on graphics boards
 */

inline void cv64_write_port (unsigned short bits,
			     volatile unsigned char *board)
{
	volatile unsigned char *addr;
	static unsigned char cvportbits = 0; /* Mirror port bits here */
	DPRINTK("ENTER\n");

	addr = board + 0x40001;
	if (bits & 0x8000) {
		cvportbits |= bits & 0xff; /* Set bits */
		DPRINTK("Set bits: %04x\n", bits);
	} else {
		bits = bits & 0xff;
		bits = (~bits) & 0xff;
		cvportbits &= bits; /* Clear bits */
		DPRINTK("Clear bits: %04x\n", bits);
	}
	
	*addr = cvportbits;
	DPRINTK("EXIT\n");
}

/*
 * Monitor switch on CyberVision board
 *
 *  toggle:
 *    0 = CyberVision Signal
 *    1 = Amiga Signal
 *  board = board addr
 *
 */
inline void cvscreen (int toggle, volatile unsigned char *board)
{
	DPRINTK("ENTER\n");
	if (toggle == 1) {
		DPRINTK("Show Amiga video\n");
		cv64_write_port (0x10, board);
	} else {
		DPRINTK("Show CyberVision video\n");
		cv64_write_port (0x8010, board);
	}
	DPRINTK("EXIT\n");
}

/* Control screen display */
/* toggle: 0 = on, 1 = off */
/* board = registerbase */
inline void gfx_on_off (int toggle, volatile unsigned char *board)
{
	int r;
	DPRINTK("ENTER\n");
	
	toggle &= 0x1;
	toggle = toggle << 5;
	DPRINTK("Turn display %s\n", (toggle ? "off" : "on"));
	
	r = (int) RSeq ((volatile caddr_t) board, SEQ_ID_CLOCKING_MODE);
	r &= 0xdf;	/* Set bit 5 to 0 */
	
	WSeq (board, SEQ_ID_CLOCKING_MODE, r | toggle);
	DPRINTK("EXIT\n");
}

/*
 * Computes M, N, and R values from
 * given input frequency. It uses a table of
 * precomputed values, to keep CPU time low.
 *
 * The return value consist of:
 * lower byte:  Bits 4-0: N Divider Value
 *	        Bits 5-6: R Value          for e.g. SR10 or SR12
 * higher byte: Bits 0-6: M divider value  for e.g. SR11 or SR13
 */
static unsigned short cv64_compute_clock(unsigned long freq)
{
	static unsigned char *mnr, *save;	/* M, N + R vals */
	unsigned long work_freq, r;
	unsigned short erg;
	long diff, d2;

	DPRINTK("ENTER\n");
	if (freq < 12500000 || freq > MAXPIXELCLOCK) {
		printk("CV64 driver: Illegal clock frequency %ld, using 25MHz\n",
		       freq);
		freq = 25000000;
	}
	DPRINTK("Freq = %ld\n", freq);
	mnr = clocks;	/* there the vals are stored */
	d2 = 0x7fffffff;

	while (*mnr) {	/* mnr vals are 0-terminated */
		work_freq = (0x37EE * (mnr[0] + 2)) / ((mnr[1] & 0x1F) + 2);

		r = (mnr[1] >> 5) & 0x03;
		if (r != 0) {
			work_freq = work_freq >> r;	/* r is the freq divider */
		}

		work_freq *= 0x3E8;	/* 2nd part of OSC */

		diff = abs(freq - work_freq);

		if (d2 >= diff) {
			d2 = diff;
			/* In save are the vals for minimal diff */
			save = mnr;
		}
		mnr += 2;
	}
	erg = *((unsigned short *)save);

	DPRINTK("EXIT\n");
	return (erg);
}

static int cv_has_4mb (volatile caddr_t fb)
{
	volatile unsigned long *tr, *tw;
	DPRINTK("ENTER\n");
	
	/* write patterns in memory and test if they can be read */
	tw = (volatile unsigned long *) fb;
	tr = (volatile unsigned long *) (fb + 0x02000000);

	*tw = 0x87654321;
	
	if (*tr != 0x87654321) {
		DPRINTK("EXIT - <4MB\n");
		return (0);
	}
	
	/* upper memory region */
	tw = (volatile unsigned long *) (fb + 0x00200000);
	tr = (volatile unsigned long *) (fb + 0x02200000);
	
	*tw = 0x87654321;
	
	if (*tr != 0x87654321) {
		DPRINTK("EXIT - <4MB\n");
		return (0);
	}
	
	*tw = 0xAAAAAAAA;
	
	if (*tr != 0xAAAAAAAA) {
		DPRINTK("EXIT - <4MB\n");
		return (0);
	}
	
	*tw = 0x55555555;
	
	if (*tr != 0x55555555) {
		DPRINTK("EXIT - <4MB\n");
		return (0);
	}
	
	DPRINTK("EXIT\n");
	return (1);
}

static void cv64_board_init (void)
{
	int i;
	unsigned char test;
	unsigned int clockpar;
	
	DPRINTK("ENTER\n");

	/*
	 * Special CyberVision 64 board operations
	 */
	/* Reset board */
	for (i = 0; i < 6; i++) {
		cv64_write_port (0xff, (volatile unsigned char *) cv64_mem);
	}
	/* Return to operational mode */
	cv64_write_port (0x8004, (volatile unsigned char *) cv64_mem);
	
  /*
   * Generic (?) S3 chip wakeup
   */
  /* Disable I/O & memory decoders, video in setup mode */
  vgaw (cv64_regs, SREG_VIDEO_SUBS_ENABLE, 0x10);
  /* Video responds to cmds, addrs & data */
  vgaw (cv64_regs, SREG_OPTION_SELECT, 0x1);
  /* Enable I/O & memory decoders, video in operational mode */
  vgaw (cv64_regs, SREG_VIDEO_SUBS_ENABLE, 0x8);
  /* VGA color emulation, enable cpu access to display mem */ 
  vgaw (cv64_regs, GREG_MISC_OUTPUT_W, 0x03);
  /* Unlock S3 VGA regs */
  WCrt (cv64_regs, CRT_ID_REGISTER_LOCK_1, 0x48); 
  /* Unlock system control & extension registers */
  WCrt (cv64_regs, CRT_ID_REGISTER_LOCK_2, 0xA5);
/* GRF - Enable interrupts */
  /* Enable enhanced regs access, Ready cntl 0 wait states */
  test = RCrt (cv64_regs, CRT_ID_SYSTEM_CONFIG);
  test = test | 0x01;		/* enable enhaced register access */
  test = test & 0xEF;		/* clear bit 4, 0 wait state */
  WCrt (cv64_regs, CRT_ID_SYSTEM_CONFIG, test);
  /*
   * bit 0=1: Enable enhaced mode functions
   * bit 2=0: Enhanced mode 8+ bits/pixel
   * bit 4=1: Enable linear addressing
   * bit 5=1: Enable MMIO
   */
  vgaw (cv64_regs, ECR_ADV_FUNC_CNTL, 0x31);
  /*
   * bit 0=1: Color emulation
   * bit 1=1: Enable CPU access to display memory
   * bit 5=1: Select high 64K memory page
   */
/* GRF - 0xE3 */
  vgaw (cv64_regs, GREG_MISC_OUTPUT_W, 0x23);
	
  /* Cpu base addr */
  WCrt (cv64_regs, CRT_ID_EXT_SYS_CNTL_4, 0x0);
	
  /* Reset. This does nothing on Trio, but standard VGA practice */
  /* WSeq (cv64_regs, SEQ_ID_RESET, 0x03); */
  /* Character clocks 8 dots wide */
  WSeq (cv64_regs, SEQ_ID_CLOCKING_MODE, 0x01);
  /* Enable cpu write to all color planes */
  WSeq (cv64_regs, SEQ_ID_MAP_MASK, 0x0F);
  /* Font table in 1st 8k of plane 2, font A=B disables swtich */
  WSeq (cv64_regs, SEQ_ID_CHAR_MAP_SELECT, 0x0);
  /* Allow mem access to 256kb */
  WSeq (cv64_regs, SEQ_ID_MEMORY_MODE, 0x2);
  /* Unlock S3 extensions to VGA Sequencer regs */
  WSeq (cv64_regs, SEQ_ID_UNLOCK_EXT, 0x6);
	
  /* Enable 4MB fast page mode */
  test = RSeq (cv64_regs, SEQ_ID_BUS_REQ_CNTL);
  test = test | 1 << 6;
  WSeq (cv64_regs, SEQ_ID_BUS_REQ_CNTL, test);
	
  /* Faster LUT write: 1 DCLK LUT write cycle, RAMDAC clk doubled */
  WSeq (cv64_regs, SEQ_ID_RAMDAC_CNTL, 0xC0);
	
  /* Clear immediate clock load bit */
  test = RSeq (cv64_regs, SEQ_ID_CLKSYN_CNTL_2);
  test = test & 0xDF;
  /* If > 55MHz, enable 2 cycle memory write */
  if (cv64_memclk >= 55000000) {
    test |= 0x80;
  }
  WSeq (cv64_regs, SEQ_ID_CLKSYN_CNTL_2, test);

  /* Set MCLK value */
  clockpar = cv64_compute_clock (cv64_memclk);
  test = (clockpar & 0xFF00) >> 8;
  WSeq (cv64_regs, SEQ_ID_MCLK_HI, test);
  test = clockpar & 0xFF;
  WSeq (cv64_regs, SEQ_ID_MCLK_LO, test);

  /* Chip rev specific: Not in my Trio manual!!! */
  if (RCrt (cv64_regs, CRT_ID_REVISION) == 0x10)
    WSeq (cv64_regs, SEQ_ID_MORE_MAGIC, test);
	
  /* We now load an 25 MHz, 31kHz, 640x480 standard VGA Mode. */

  /* Set DCLK value */
  WSeq (cv64_regs, SEQ_ID_DCLK_HI, 0x13);
  WSeq (cv64_regs, SEQ_ID_DCLK_LO, 0x41);

  /* Load DCLK (and MCLK?) immediately */
  test = RSeq (cv64_regs, SEQ_ID_CLKSYN_CNTL_2);
  test = test | 0x22;
  WSeq (cv64_regs, SEQ_ID_CLKSYN_CNTL_2, test);

  /* Enable loading of DCLK */
  test = vgar (cv64_regs, GREG_MISC_OUTPUT_R);
  test = test | 0x0C;
  vgaw (cv64_regs, GREG_MISC_OUTPUT_W, test);

  /* Turn off immediate xCLK load */
  WSeq (cv64_regs, SEQ_ID_CLKSYN_CNTL_2, 0x2);

  /* Horizontal character clock counts */
  /* 8 LSB of 9 bits = total line - 5 */
  WCrt (cv64_regs, CRT_ID_HOR_TOTAL, 0x5F);
  /* Active display line */
  WCrt (cv64_regs, CRT_ID_HOR_DISP_ENA_END, 0x4F);
  /* Blank assertion start */
  WCrt (cv64_regs, CRT_ID_START_HOR_BLANK, 0x50);
  /* Blank assertion end */
  WCrt (cv64_regs, CRT_ID_END_HOR_BLANK, 0x82);
  /* HSYNC assertion start */
  WCrt (cv64_regs, CRT_ID_START_HOR_RETR, 0x54);
  /* HSYNC assertion end */
  WCrt (cv64_regs, CRT_ID_END_HOR_RETR, 0x80);
  WCrt (cv64_regs, CRT_ID_VER_TOTAL, 0xBF);
  WCrt (cv64_regs, CRT_ID_OVERFLOW, 0x1F);
  WCrt (cv64_regs, CRT_ID_PRESET_ROW_SCAN, 0x0);
  WCrt (cv64_regs, CRT_ID_MAX_SCAN_LINE, 0x40);
  WCrt (cv64_regs, CRT_ID_CURSOR_START, 0x00);
  WCrt (cv64_regs, CRT_ID_CURSOR_END, 0x00);
  WCrt (cv64_regs, CRT_ID_START_ADDR_HIGH, 0x00);
  WCrt (cv64_regs, CRT_ID_START_ADDR_LOW, 0x00);
  WCrt (cv64_regs, CRT_ID_CURSOR_LOC_HIGH, 0x00);
  WCrt (cv64_regs, CRT_ID_CURSOR_LOC_LOW, 0x00);
  WCrt (cv64_regs, CRT_ID_START_VER_RETR, 0x9C);
  WCrt (cv64_regs, CRT_ID_END_VER_RETR, 0x0E);
  WCrt (cv64_regs, CRT_ID_VER_DISP_ENA_END, 0x8F);
  WCrt (cv64_regs, CRT_ID_SCREEN_OFFSET, 0x50);
  WCrt (cv64_regs, CRT_ID_UNDERLINE_LOC, 0x00);
  WCrt (cv64_regs, CRT_ID_START_VER_BLANK, 0x96);
  WCrt (cv64_regs, CRT_ID_END_VER_BLANK, 0xB9);
  WCrt (cv64_regs, CRT_ID_MODE_CONTROL, 0xE3);
  WCrt (cv64_regs, CRT_ID_LINE_COMPARE, 0xFF);
  WCrt (cv64_regs, CRT_ID_BACKWAD_COMP_3, 0x10);	/* FIFO enabled */
  WCrt (cv64_regs, CRT_ID_MISC_1, 0x35);
  WCrt (cv64_regs, CRT_ID_DISPLAY_FIFO, 0x5A);
  WCrt (cv64_regs, CRT_ID_EXT_MEM_CNTL_2, 0x70);
  WCrt (cv64_regs, CRT_ID_LAW_POS_LO, 0x40);
  WCrt (cv64_regs, CRT_ID_EXT_MEM_CNTL_3, 0xFF);
  
  WGfx (cv64_regs, GCT_ID_SET_RESET, 0x0);
  WGfx (cv64_regs, GCT_ID_ENABLE_SET_RESET, 0x0);
  WGfx (cv64_regs, GCT_ID_COLOR_COMPARE, 0x0);
  WGfx (cv64_regs, GCT_ID_DATA_ROTATE, 0x0);
  WGfx (cv64_regs, GCT_ID_READ_MAP_SELECT, 0x0);
  WGfx (cv64_regs, GCT_ID_GRAPHICS_MODE, 0x40);
  WGfx (cv64_regs, GCT_ID_MISC, 0x01);
  WGfx (cv64_regs, GCT_ID_COLOR_XCARE, 0x0F);
  WGfx (cv64_regs, GCT_ID_BITMASK, 0xFF);
	
  /* Colors for text mode */
  for (i = 0; i < 0xf; i++)
    WAttr (cv64_regs, i, i);
	
  WAttr (cv64_regs, ACT_ID_ATTR_MODE_CNTL, 0x41);
  WAttr (cv64_regs, ACT_ID_OVERSCAN_COLOR, 0x01);
  WAttr (cv64_regs, ACT_ID_COLOR_PLANE_ENA, 0x0F);
  WAttr (cv64_regs, ACT_ID_HOR_PEL_PANNING, 0x0);
  WAttr (cv64_regs, ACT_ID_COLOR_SELECT, 0x0);
	
  vgaw (cv64_regs, VDAC_MASK, 0xFF);
	
  *((unsigned long *) (cv64_regs + ECR_FRGD_COLOR)) = 0xFF;
  *((unsigned long *) (cv64_regs + ECR_BKGD_COLOR)) = 0;
	
  /* Colors initially set to grayscale */
	
  vgaw (cv64_regs, VDAC_ADDRESS_W, 0);
  for (i = 255; i >= 0; i--) {
    vgaw (cv64_regs, VDAC_DATA, i);
    vgaw (cv64_regs, VDAC_DATA, i);
    vgaw (cv64_regs, VDAC_DATA, i);
  }

  /* GFx hardware cursor off */
  WCrt (cv64_regs, CRT_ID_HWGC_MODE, 0x00);

  /* Set first to 4MB, so test will work */
  WCrt (cv64_regs, CRT_ID_LAW_CNTL, 0x13);
  /* Find "correct" size of fbmem of Z3 board */
  if (cv_has_4mb ((volatile caddr_t) cv64_fbmem)) {
    cv64_size = 1024 * 1024 * 4;
    WCrt (cv64_regs, CRT_ID_LAW_CNTL, 0x13);
    DPRINTK("4MB board\n");
  } else {
    cv64_size = 1024 * 1024 * 2;
    WCrt (cv64_regs, CRT_ID_LAW_CNTL, 0x12);
    DPRINTK("2MB board\n");
  }
	
  /* Initialize graphics engine */
  Cyber_WaitBlit();
  /* GfxBusyWait (cv64_regs); */
  vgaw16 (cv64_regs, ECR_FRGD_MIX, 0x27);
  vgaw16 (cv64_regs, ECR_BKGD_MIX, 0x07);
  vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0x1000);
  udelay(200);
  /* __cv_delay (200000); */
  vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0x2000);
  Cyber_WaitBlit();
  /* GfxBusyWait (cv64_regs); */
  vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0x3FFF);
  Cyber_WaitBlit();
  /* GfxBusyWait (cv64_regs); */
  udelay(200);
  /* __cv_delay (200000); */
  vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0x4FFF);
  Cyber_WaitBlit();
  /* GfxBusyWait (cv64_regs); */
  vgaw16 (cv64_regs, ECR_BITPLANE_WRITE_MASK, ~0);
  Cyber_WaitBlit();
  /* GfxBusyWait (cv64_regs); */
  vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0xE000);
  vgaw16 (cv64_regs, ECR_CURRENT_Y_POS2, 0x00);
  vgaw16 (cv64_regs, ECR_CURRENT_X_POS2, 0x00);
  vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0xA000);
  vgaw16 (cv64_regs, ECR_DEST_Y__AX_STEP, 0x00);
  vgaw16 (cv64_regs, ECR_DEST_Y2__AX_STEP2, 0x00);
  vgaw16 (cv64_regs, ECR_DEST_X__DIA_STEP, 0x00);
  vgaw16 (cv64_regs, ECR_DEST_X2__DIA_STEP2, 0x00);
  vgaw16 (cv64_regs, ECR_SHORT_STROKE, 0x00);
  vgaw16 (cv64_regs, ECR_DRAW_CMD, 0x01);
	
  Cyber_WaitBlit();
  /* GfxBusyWait (cv64_regs); */
	
  vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0x4FFF);
  vgaw16 (cv64_regs, ECR_BKGD_COLOR, 0x01);
  vgaw16 (cv64_regs, ECR_FRGD_COLOR, 0x00);
	
	
  /* Enable video display (set bit 5) */
/* ARB - Would also seem to write to AR13.
 *       May want to use parts of WAttr to set JUST bit 5
 */
  WAttr (cv64_regs, 0x33, 0);
	
/* GRF - function code ended here */

  /* Turn gfx on again */
  gfx_on_off (0, cv64_regs);
	
  /* Pass-through */
  cvscreen (0, (volatile unsigned char *) cv64_mem);

  DPRINTK("EXIT\n");
}

static void cv64_load_video_mode (struct fb_var_screeninfo *video_mode)
{
  int fx, fy;
  unsigned short mnr;
  unsigned short HT, HDE, HBS, HBE, HSS, HSE, VDE, VBS, VBE, VSS, VSE, VT;
  char LACE, DBLSCAN, TEXT, CONSOLE;
  int cr50, sr15, sr18, clock_mode, test;
  int m, n;
  int tfillm, temptym;
  int hmul;
	
  /* ---------------- */
  int xres, hfront, hsync, hback;
  int yres, vfront, vsync, vback;
  int bpp;
#if 0
  float freq_f;
#endif
  long freq;
  /* ---------------- */
	
  DPRINTK("ENTER\n");
  TEXT = 0;	/* if depth == 4 */
  CONSOLE = 0;	/* mode num == 255 (console) */
  fx = fy = 8;	/* force 8x8 font */

/* GRF - Disable interrupts */	
	
  gfx_on_off (1, cv64_regs);
	
  switch (video_mode->bits_per_pixel) {
  case 15:
  case 16:
    hmul = 2;
    break;
		
  default:
    hmul = 1;
    break;
  }
	
  bpp = video_mode->bits_per_pixel;
  xres = video_mode->xres;
  hfront = video_mode->right_margin;
  hsync = video_mode->hsync_len;
  hback = video_mode->left_margin;

  LACE = 0;
  DBLSCAN = 0;

  if (video_mode->vmode & FB_VMODE_DOUBLE) {
    yres = video_mode->yres * 2;
    vfront = video_mode->lower_margin * 2;
    vsync = video_mode->vsync_len * 2;
    vback = video_mode->upper_margin * 2;
    DBLSCAN = 1;
  } else if (video_mode->vmode & FB_VMODE_INTERLACED) {
    yres = (video_mode->yres + 1) / 2;
    vfront = (video_mode->lower_margin + 1) / 2;
    vsync = (video_mode->vsync_len + 1) / 2;
    vback = (video_mode->upper_margin + 1) / 2;
    LACE = 1;
  } else {
    yres = video_mode->yres;
    vfront = video_mode->lower_margin;
    vsync = video_mode->vsync_len;
    vback = video_mode->upper_margin;
  }

  /* ARB Dropping custom setup method from cvision.c */
#if 0
  if (cvision_custom_mode) {
    HBS = hbs / 8 * hmul;
    HBE = hbe / 8 * hmul;
    HSS = hss / 8 * hmul;
    HSE = hse / 8 * hmul;
    HT  = ht / 8 * hmul - 5;
		
    VBS = vbs - 1;
    VSS = vss;
    VSE = vse;
    VBE = vbe;
    VT  = vt - 2;
  } else {
#else
    {
#endif
    HBS = hmul * (xres / 8);
    HBE = hmul * ((xres/8) + (hfront/8) + (hsync/8) + (hback/8) - 2);
    HSS = hmul * ((xres/8) + (hfront/8) + 2);
    HSE = hmul * ((xres/8) + (hfront/8) + (hsync/8) + 1);
    HT  = hmul * ((xres/8) + (hfront/8) + (hsync/8) + (hback/8));
	
    VBS = yres;
    VBE = yres + vfront + vsync + vback - 2;
    VSS = yres + vfront - 1;
    VSE = yres + vfront + vsync - 1;
    VT  = yres + vfront + vsync + vback - 2;
  }

  vgaw (cv64_regs, ECR_ADV_FUNC_CNTL, (TEXT ? 0x00 : 0x31));
	
  if (TEXT)
    HDE = ((video_mode->xres + fx - 1) / fx) - 1;
  else
    HDE = (video_mode->xres + 3) * hmul / 8 - 1;
	
  VDE = video_mode->yres - 1;

  WCrt (cv64_regs, CRT_ID_HWGC_MODE, 0x00);
  WCrt (cv64_regs, CRT_ID_EXT_DAC_CNTL, 0x00);
	
  WSeq (cv64_regs, SEQ_ID_MEMORY_MODE,
	(TEXT || (video_mode->bits_per_pixel == 1)) ? 0x06 : 0x0e);
  WGfx (cv64_regs, GCT_ID_READ_MAP_SELECT, 0x00);
  WSeq (cv64_regs, SEQ_ID_MAP_MASK,
	(video_mode->bits_per_pixel == 1) ? 0x01 : 0xFF);
  WSeq (cv64_regs, SEQ_ID_CHAR_MAP_SELECT, 0x00);
	
  /* cv64_compute_clock accepts arguments in Hz */
  /* pixclock is in ps ... convert to Hz */
	
#if 0
  freq_f = (1.0 / (float) video_mode->pixclock) * 1000000000;
  freq = ((long) freq_f) * 1000;
#else
/* freq = (long) ((long long)1000000000000 / (long long) video_mode->pixclock);
 */
  freq = (1000000000 / video_mode->pixclock) * 1000;
#endif

  mnr = cv64_compute_clock (freq);
  WSeq (cv64_regs, SEQ_ID_DCLK_HI, ((mnr & 0xFF00) >> 8));
  WSeq (cv64_regs, SEQ_ID_DCLK_LO, (mnr & 0xFF));
	
  /* Load display parameters into board */
  WCrt (cv64_regs, CRT_ID_EXT_HOR_OVF,
	((HT & 0x100) ? 0x01 : 0x00) |
	((HDE & 0x100) ? 0x02 : 0x00) |
	((HBS & 0x100) ? 0x04 : 0x00) |
	/* ((HBE & 0x40) ? 0x08 : 0x00) | */
	((HSS & 0x100) ? 0x10 : 0x00) |
	/* ((HSE & 0x20) ? 0x20 : 0x00) | */
	(((HT-5) & 0x100) ? 0x40 : 0x00)
	);
	
  WCrt (cv64_regs, CRT_ID_EXT_VER_OVF,
	0x40 |
	((VT & 0x400) ? 0x01 : 0x00) |
	((VDE & 0x400) ? 0x02 : 0x00) |
	((VBS & 0x400) ? 0x04 : 0x00) |
	((VSS & 0x400) ? 0x10 : 0x00)
	);
	
  WCrt (cv64_regs, CRT_ID_HOR_TOTAL, HT);
  WCrt (cv64_regs, CRT_ID_DISPLAY_FIFO, HT - 5);
  WCrt (cv64_regs, CRT_ID_HOR_DISP_ENA_END, ((HDE >= HBS) ? (HBS - 1) : HDE));
  WCrt (cv64_regs, CRT_ID_START_HOR_BLANK, HBS);
  WCrt (cv64_regs, CRT_ID_END_HOR_BLANK, ((HBE & 0x1F) | 0x80));
  WCrt (cv64_regs, CRT_ID_START_HOR_RETR, HSS);
  WCrt (cv64_regs, CRT_ID_END_HOR_RETR,
	(HSE & 0x1F) |
	((HBE & 0x20) ? 0x80 : 0x00)
	);
  WCrt (cv64_regs, CRT_ID_VER_TOTAL, VT);
  WCrt (cv64_regs, CRT_ID_OVERFLOW,
	0x10 |
	((VT & 0x100) ? 0x01 : 0x00) |
	((VDE & 0x100) ? 0x02 : 0x00) |
	((VSS & 0x100) ? 0x04 : 0x00) |
	((VBS & 0x100) ? 0x08 : 0x00) |
	((VT & 0x200) ? 0x20 : 0x00) |
	((VDE & 0x200) ? 0x40 : 0x00) |
	((VSS & 0x200) ? 0x80 : 0x00)
	);
  WCrt (cv64_regs, CRT_ID_MAX_SCAN_LINE,
	0x40 |
	(DBLSCAN ? 0x80 : 0x00) |
	((VBS & 0x200) ? 0x20 : 0x00) |
	(TEXT ? ((fy - 1) & 0x1F) : 0x00)
	);
	
  WCrt (cv64_regs, CRT_ID_MODE_CONTROL, 0xE3);

  /* Text cursor */
	
  if (TEXT) {
#if 1
    WCrt (cv64_regs, CRT_ID_CURSOR_START, (fy & 0x1f) - 2);
    WCrt (cv64_regs, CRT_ID_CURSOR_END, (fy & 0x1F) - 1);
#else
    WCrt (cv64_regs, CRT_ID_CURSOR_START, 0x00);
    WCrt (cv64_regs, CRT_ID_CURSOR_END, fy & 0x1F);
#endif
    WCrt (cv64_regs, CRT_ID_UNDERLINE_LOC, (fy - 1) & 0x1F);
    WCrt (cv64_regs, CRT_ID_CURSOR_LOC_HIGH, 0x00);
    WCrt (cv64_regs, CRT_ID_CURSOR_LOC_LOW, 0x00);
  }
	
  WCrt (cv64_regs, CRT_ID_START_ADDR_HIGH, 0x00);
  WCrt (cv64_regs, CRT_ID_START_ADDR_LOW, 0x00);
  WCrt (cv64_regs, CRT_ID_START_VER_RETR, VSS);
  WCrt (cv64_regs, CRT_ID_END_VER_RETR, (VSE & 0x0F));
  WCrt (cv64_regs, CRT_ID_VER_DISP_ENA_END, VDE);
  WCrt (cv64_regs, CRT_ID_START_VER_BLANK, VBS);
  WCrt (cv64_regs, CRT_ID_END_VER_BLANK, VBE);
  WCrt (cv64_regs, CRT_ID_LINE_COMPARE, 0xFF);
  WCrt (cv64_regs, CRT_ID_LACE_RETR_START, HT / 2);
  WCrt (cv64_regs, CRT_ID_LACE_CONTROL, (LACE ? 0x20 : 0x00));
  WGfx (cv64_regs, GCT_ID_GRAPHICS_MODE,
	((TEXT || (video_mode->bits_per_pixel == 1)) ? 0x00 : 0x40));
  WGfx (cv64_regs, GCT_ID_MISC, (TEXT ? 0x04 : 0x01));
  WSeq (cv64_regs, SEQ_ID_MEMORY_MODE,
	((TEXT || (video_mode->bits_per_pixel == 1)) ? 0x06 : 0x02));
	
  vgaw (cv64_regs, VDAC_MASK, 0xFF);
	
  /* Blank border */
  test = RCrt (cv64_regs, CRT_ID_BACKWAD_COMP_2);
  WCrt (cv64_regs, CRT_ID_BACKWAD_COMP_2, (test | 0x20));
	
  sr15 = RSeq (cv64_regs, SEQ_ID_CLKSYN_CNTL_2);
  sr15 &= 0xEF;
  sr18 = RSeq (cv64_regs, SEQ_ID_RAMDAC_CNTL);
  sr18 &= 0x7F;
  clock_mode = 0x00;
  cr50 = 0x00;
	
  test = RCrt (cv64_regs, CRT_ID_EXT_MISC_CNTL_2);
  test &= 0xD;
	
  /* Clear roxxler byte-swapping... */
  cv64_write_port (0x0040, (volatile unsigned char *) cv64_mem);
  cv64_write_port (0x0020, (volatile unsigned char *) cv64_mem);
	
  switch (video_mode->bits_per_pixel) {
  case 1:
  case 4:	/* text */
    HDE = video_mode->xres / 16;
    break;
		
  case 8:
    if (freq > 80000000) {
      clock_mode = 0x10 | 0x02;
      sr15 |= 0x10;
      sr18 |= 0x80;
    }
    HDE = video_mode->xres / 8;
    cr50 |= 0x00;
    break;
		
  case 15:
    cv64_write_port (0x8020, (volatile unsigned char *) cv64_mem);
    clock_mode = 0x30;
    HDE = video_mode->xres / 4;
    cr50 |= 0x10;
    break;
		
  case 16:
    cv64_write_port (0x8020, (volatile unsigned char *) cv64_mem);
    clock_mode = 0x50;
    HDE = video_mode->xres / 4;
    cr50 |= 0x10;
    break;
		
  case 24:
  case 32:
    cv64_write_port (0x8040, (volatile unsigned char *) cv64_mem);
    clock_mode = 0xD0;
    HDE = video_mode->xres / 2;
    cr50 |= 0x30;
    break;
  }
	
  WCrt (cv64_regs, CRT_ID_EXT_MISC_CNTL_2, clock_mode | test);
  WSeq (cv64_regs, SEQ_ID_CLKSYN_CNTL_2, sr15);
  WSeq (cv64_regs, SEQ_ID_RAMDAC_CNTL, sr18);
  WCrt (cv64_regs, CRT_ID_SCREEN_OFFSET, HDE);

  WCrt (cv64_regs, CRT_ID_MISC_1, (TEXT ? 0x05 : 0x35));
	
  test = RCrt (cv64_regs, CRT_ID_EXT_SYS_CNTL_2);
  test &= ~0x30;
  test |= (HDE >> 4) & 0x30;
  WCrt (cv64_regs, CRT_ID_EXT_SYS_CNTL_2, test);
	
  /* Set up graphics engine */
  switch (video_mode->xres) {
  case 1024:
    cr50 |= 0x00;
    break;
		
  case 640:
    cr50 |= 0x40;
    break;
		
  case 800:
    cr50 |= 0x80;
    break;
		
  case 1280:
    cr50 |= 0xC0;
    break;
		
  case 1152:
    cr50 |= 0x01;
    break;
		
  case 1600:
    cr50 |= 0x81;
    break;
		
  default:	/* XXX */
    break;
  }
	
  WCrt (cv64_regs, CRT_ID_EXT_SYS_CNTL_1, cr50);
	
  udelay(100);
  /* __cv_delay (100000); */
  WAttr (cv64_regs, ACT_ID_ATTR_MODE_CNTL, (TEXT ? 0x08 : 0x41));
  udelay(100);
  /* __cv_delay (100000); */
  WAttr (cv64_regs, ACT_ID_COLOR_PLANE_ENA,
	 (video_mode->bits_per_pixel == 1) ? 0x01 : 0x0F);
  udelay(100);
  /* __cv_delay (100000); */
	
  tfillm = (96 * (cv64_memclk / 1000)) / 240000;
	
  switch (video_mode->bits_per_pixel) {
  case 32:
  case 24:
    temptym = (24 * (cv64_memclk / 1000)) / (freq / 1000);
    break;
  case 15:
  case 16:
    temptym = (48 * (cv64_memclk / 1000)) / (freq / 1000);
    break;
  case 4:
    temptym = (192 * (cv64_memclk / 1000)) / (freq / 1000);
    break;
  default:
    temptym = (96 * (cv64_memclk / 1000)) / (freq / 1000);
    break;
  }
	
  m = (temptym - tfillm - 9) / 2;
  if (m < 0)
    m = 0;
  m = (m & 0x1F) << 3;
  if (m < 0x18)
    m = 0x18;
  n = 0xFF;
	
  WCrt (cv64_regs, CRT_ID_EXT_MEM_CNTL_2, m);
  WCrt (cv64_regs, CRT_ID_EXT_MEM_CNTL_3, n);
  udelay(10);
  /* __cv_delay (10000); */
	
  /* Text initialization */
	
  if (TEXT) {
    /* Do text initialization here ! */
  }
	
  if (CONSOLE) {
    int i;
    vgaw (cv64_regs, VDAC_ADDRESS_W, 0);
    for (i = 0; i < 4; i++) {
      vgaw (cv64_regs, VDAC_DATA, cvconscolors [i][0]);
      vgaw (cv64_regs, VDAC_DATA, cvconscolors [i][1]);
      vgaw (cv64_regs, VDAC_DATA, cvconscolors [i][2]);
    }
  }
	
  WAttr (cv64_regs, 0x33, 0);
	
  /* Turn gfx on again */
  gfx_on_off (0, (volatile unsigned char *) cv64_regs);
	
  /* Pass-through */
  cvscreen (0, (volatile unsigned char *) cv64_mem);

DPRINTK("EXIT\n");
}

void cvision_bitblt (u_short sx, u_short sy, u_short dx, u_short dy,
		     u_short w, u_short h)
{
	unsigned short drawdir = 0;
	
	DPRINTK("ENTER\n");
	if (sx > dx) {
		drawdir |= 1 << 5;
	} else {
		sx += w - 1;
		dx += w - 1;
	}
	
	if (sy > dy) {
		drawdir |= 1 << 7;
	} else {
		sy += h - 1;
		dy += h - 1;
	}
	
	Cyber_WaitBlit();
	/* GfxBusyWait (cv64_regs); */
	vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0xA000);
	vgaw16 (cv64_regs, ECR_BKGD_MIX, 0x7);
	vgaw16 (cv64_regs, ECR_FRGD_MIX, 0x67);
	vgaw16 (cv64_regs, ECR_BKGD_COLOR, 0x0);
	vgaw16 (cv64_regs, ECR_FRGD_COLOR, 0x1);
	vgaw16 (cv64_regs, ECR_BITPLANE_READ_MASK, 0x1);
	vgaw16 (cv64_regs, ECR_BITPLANE_WRITE_MASK, 0xFFF);
	vgaw16 (cv64_regs, ECR_CURRENT_Y_POS, sy);
	vgaw16 (cv64_regs, ECR_CURRENT_X_POS, sx);
	vgaw16 (cv64_regs, ECR_DEST_Y__AX_STEP, dy);
	vgaw16 (cv64_regs, ECR_DEST_X__DIA_STEP, dx);
	vgaw16 (cv64_regs, ECR_READ_REG_DATA, h - 1);
	vgaw16 (cv64_regs, ECR_MAJ_AXIS_PIX_CNT, w - 1);
	vgaw16 (cv64_regs, ECR_DRAW_CMD, 0xC051 | drawdir);
	DPRINTK("EXIT\n");
}

void cvision_clear (u_short dx, u_short dy, u_short w, u_short h, u_short bg)
{
	DPRINTK("ENTER\n");
	Cyber_WaitBlit();
	/* GfxBusyWait (cv64_regs); */
	vgaw16 (cv64_regs, ECR_FRGD_MIX, 0x0027);
	vgaw16 (cv64_regs, ECR_FRGD_COLOR, bg);
	vgaw16 (cv64_regs, ECR_READ_REG_DATA, 0xA000);
	vgaw16 (cv64_regs, ECR_CURRENT_Y_POS, dy);
	vgaw16 (cv64_regs, ECR_CURRENT_X_POS, dx);
	vgaw16 (cv64_regs, ECR_READ_REG_DATA, h - 1);
	vgaw16 (cv64_regs, ECR_MAJ_AXIS_PIX_CNT, w - 1);
	vgaw16 (cv64_regs, ECR_DRAW_CMD, 0x40B1);	
	DPRINTK("EXIT\n");
}

#ifdef CYBERFBDEBUG
/*
 * Dump internal settings of CyberVision board
 */
static void cv64_dump (void)
{
	DPRINTK("ENTER\n");
        /* Dump the VGA setup values */
	*(CyberRegs + S3_CRTC_ADR) = 0x00;
	DPRINTK("CR00 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x01;
	DPRINTK("CR01 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x02;
	DPRINTK("CR02 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x03;
	DPRINTK("CR03 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x04;
	DPRINTK("CR04 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x05;
	DPRINTK("CR05 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x06;
	DPRINTK("CR06 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x07;
	DPRINTK("CR07 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x08;
	DPRINTK("CR08 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x09;
	DPRINTK("CR09 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x10;
	DPRINTK("CR10 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x11;
	DPRINTK("CR11 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x12;
	DPRINTK("CR12 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x13;
	DPRINTK("CR13 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x15;
	DPRINTK("CR15 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x16;
	DPRINTK("CR16 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x36;
	DPRINTK("CR36 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x37;
	DPRINTK("CR37 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x42;
	DPRINTK("CR42 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x43;
	DPRINTK("CR43 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x50;
	DPRINTK("CR50 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x51;
	DPRINTK("CR51 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x53;
	DPRINTK("CR53 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x58;
	DPRINTK("CR58 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x59;
	DPRINTK("CR59 = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x5A;
	DPRINTK("CR5A = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x5D;
	DPRINTK("CR5D = %x\n", *(CyberRegs + S3_CRTC_DATA));
	*(CyberRegs + S3_CRTC_ADR) = 0x5E;
	DPRINTK("CR5E = %x\n", *(CyberRegs + S3_CRTC_DATA));
	DPRINTK("MISC = %x\n", *(CyberRegs + GREG_MISC_OUTPUT_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x01;
	DPRINTK("SR01 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x02;
	DPRINTK("SR02 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x03;
	DPRINTK("SR03 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x09;
	DPRINTK("SR09 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x10;
	DPRINTK("SR10 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x11;
	DPRINTK("SR11 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x12;
	DPRINTK("SR12 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x13;
	DPRINTK("SR13 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	*(CyberRegs + SEQ_ADDRESS) = 0x15;
	DPRINTK("SR15 = %x\n", *(CyberRegs + SEQ_ADDRESS_R));
	
	return;
}
#endif
