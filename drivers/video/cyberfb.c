/*
 * linux/drivers/video/cyberfb.c -- CyberVision64 frame buffer device
 *
 *    Copyright (C) 1996 Martin Apel
 *                       Geert Uytterhoeven
 *
 *
 * This file is based on the Amiga frame buffer device (amifb.c):
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 * History:
 *   - 22 Dec 95: Original version by Martin Apel
 *   - 05 Jan 96: Geert: integration into the current source tree
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

#include "s3blit.h"
#include "fbcon.h"
#include "fbcon-cfb8.h"
#include "fbcon-cfb16.h"


#ifdef CYBERFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))


#define wb_64(reg,dat) (*((unsigned char volatile *)CyberRegs + reg) = dat)



struct cyberfb_par {
   int xres;
   int yres;
   int bpp;
   int accel;
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

#define CYBER8_WIDTH 1152
#define CYBER8_HEIGHT 886
#define CYBER8_PIXCLOCK 12500    /* ++Geert: Just a guess */

#if 0
#define CYBER16_WIDTH 800
#define CYBER16_HEIGHT 600
#endif
#define CYBER16_PIXCLOCK 25000   /* ++Geert: Just a guess */


static unsigned int CyberKey = 0;
static unsigned char Cyber_colour_table [256][4];
static unsigned long CyberMem;
static unsigned long CyberSize;
static volatile char *CyberRegs;
 

/*
 *    Predefined Video Modes
 */

static struct fb_videomode cyberfb_predefined[] __initdata = {
    {
	"640x480-8", {		/* Cybervision 8 bpp */
	    640, 480, 640, 480, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, CYBER8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"800x600-8", {		/* Cybervision 8 bpp */
	    800, 600, 800, 600, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, CYBER8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1024x768-8", {		/* Cybervision 8 bpp */
	    1024, 768, 1024, 768, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, CYBER8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1152x886-8", {		/* Cybervision 8 bpp */
	    1152, 886, 1152, 886, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, CYBER8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1280x1024-8", {	/* Cybervision 8 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, CYBER8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"1600x1200-8", {	/* Cybervision 8 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, CYBER8_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }, {
	"800x600-16", {		/* Cybervision 16 bpp */
	    800, 600, 800, 600, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, CYBER16_PIXCLOCK, 64, 96, 35, 12, 112, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    }
};


#define NUM_TOTAL_MODES    arraysize(cyberfb_predefined)


static int Cyberfb_inverse = 0;
#if 0
static int Cyberfb_Cyber8 = 0;        /* Use Cybervision board */
static int Cyberfb_Cyber16 = 0;       /* Use Cybervision board */
#endif

/*
 *    Some default modes
 */

#define CYBER8_DEFMODE     (0)
#define CYBER16_DEFMODE    (6)

static struct fb_var_screeninfo cyberfb_default;


/*
 *    Interface used by the world
 */

void cyberfb_setup(char *options, int *ints);

static int cyberfb_open(struct fb_info *info, int user);
static int cyberfb_release(struct fb_info *info, int user);
static int cyberfb_get_fix(struct fb_fix_screeninfo *fix, int con, struct
fb_info *info);
static int cyberfb_get_var(struct fb_var_screeninfo *var, int con, struct
fb_info *info);
static int cyberfb_set_var(struct fb_var_screeninfo *var, int con, struct
fb_info *info);
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
	char size;
	volatile u_long *CursorBase;

	for (i = 0; i < 256; i++)
	{
		Cyber_colour_table [i][0] = i;
		Cyber_colour_table [i][1] = i;
		Cyber_colour_table [i][2] = i;
		Cyber_colour_table [i][3] = 0;
	}

	/*
	 * Just clear the thing for the biggest mode.
	 *
	 * ++Andre, TODO: determine size first, then clear all memory
	 *                (the 3D penguin might need texture memory :-) )
	 */

	memset ((char*)CyberMem, 0, 1600 * 1200);

	/* Disable hardware cursor */
	wb_64(S3_CRTC_ADR, S3_REG_LOCK2);
	wb_64(S3_CRTC_DATA, 0xa0);
	wb_64(S3_CRTC_ADR, S3_HGC_MODE);
	wb_64(S3_CRTC_DATA, 0x00);
	wb_64(S3_CRTC_ADR, S3_HWGC_DX);
	wb_64(S3_CRTC_DATA, 0x00);
	wb_64(S3_CRTC_ADR, S3_HWGC_DY);
	wb_64(S3_CRTC_DATA, 0x00);

	/* Get memory size (if not 2MB it is 4MB) */
	*(CyberRegs + S3_CRTC_ADR) = S3_LAW_CTL;
	size = *(CyberRegs + S3_CRTC_DATA);
	if ((size & 0x03) == 0x02)
		CyberSize = 0x00200000; /* 2 MB */
	else
		CyberSize = 0x00400000; /* 4 MB */

	/* Initialize hardware cursor */
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

	Cyber_setcolreg (255, 56, 100, 160, 0, NULL /* unused */);
	Cyber_setcolreg (254, 0, 0, 0, 0, NULL /* unused */);

	return 0;
}


/*
 *    This function should fill in the `fix' structure based on the
 *    values in the `par' structure.
 */

static int Cyber_encode_fix(struct fb_fix_screeninfo *fix,
			    struct cyberfb_par *par)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, cyberfb_name);
	fix->smem_start = (char *)CyberMem;
	fix->smem_len = CyberSize;
	fix->mmio_start = (char *)CyberRegs;
	fix->mmio_len = 0x10000;

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
	fix->accel = FB_ACCEL_S3_TRIO64;

	return(0);
}


/*
 *    Get the video params out of `var'. If a value doesn't fit, round
 *    it up, if it's too big, return -EINVAL.
 */

static int Cyber_decode_var(struct fb_var_screeninfo *var,
			    struct cyberfb_par *par)
{
#if 1
	par->xres = var->xres;
	par->yres = var->yres;
	par->bpp = var->bits_per_pixel;
	if (var->accel_flags & FB_ACCELF_TEXT)
	    par->accel = FB_ACCELF_TEXT;
	else
	    par->accel = 0;
#else
	if (Cyberfb_Cyber8) {
		par->xres = CYBER8_WIDTH;
		par->yres = CYBER8_HEIGHT;
		par->bpp = 8;
	} else {
		par->xres = CYBER16_WIDTH;
		par->yres = CYBER16_HEIGHT;
		par->bpp = 16;
	}
#endif
	return(0);
}


/*
 *    Fill the `var' structure based on the values in `par' and maybe
 *    other values read out of the hardware.
 */

static int Cyber_encode_var(struct fb_var_screeninfo *var,
			    struct cyberfb_par *par)
{
	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->xres;
	var->yres_virtual = par->yres;
	var->xoffset = 0;
	var->yoffset = 0;

	var->bits_per_pixel = par->bpp;
	var->grayscale = 0;

	if (par->bpp == 8) {
		var->red.offset = 0;
		var->red.length = 8;
		var->red.msb_right = 0;
		var->blue = var->green = var->red;
	} else {
		var->red.offset = 11;
		var->red.length = 5;
		var->red.msb_right = 0;
		var->green.offset = 5;
		var->green.length = 6;
		var->green.msb_right = 0;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->blue.msb_right = 0;
	}
	var->transp.offset = 0;
	var->transp.length = 0;
	var->transp.msb_right = 0;

	var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;

	var->accel_flags = (par->accel && par->bpp == 8) ? FB_ACCELF_TEXT : 0;

	var->vmode = FB_VMODE_NONINTERLACED;

	/* Dummy values */

	if (par->bpp == 8)
		var->pixclock = CYBER8_PIXCLOCK;
	else
		var->pixclock = CYBER16_PIXCLOCK;
	var->sync = 0;
	var->left_margin = 64;
	var->right_margin = 96;
	var->upper_margin = 35;
	var->lower_margin = 12;
	var->hsync_len = 112;
	var->vsync_len = 2;

	return(0);
}


/*
 *    Set a single color register. The values supplied are already
 *    rounded down to the hardware's capabilities (according to the
 *    entries in the var structure). Return != 0 for invalid regno.
 */

static int Cyber_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	if (regno > 255)
	{
		return (1);
	}

	wb_64(0x3c8, (unsigned char) regno);
	Cyber_colour_table [regno][0] = red & 0xff;
	Cyber_colour_table [regno][1] = green & 0xff;
	Cyber_colour_table [regno][2] = blue & 0xff;
	Cyber_colour_table [regno][3] = transp;

	wb_64(0x3c9, (red & 0xff) >> 2);
	wb_64(0x3c9, (green & 0xff) >> 2);
	wb_64(0x3c9, (blue & 0xff) >> 2);

	return (0);
}


/*
 *    Read a single color register and split it into
 *    colors/transparent. Return != 0 for invalid regno.
 */

static int Cyber_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info)
{
	if (regno >= 256)
		return (1);
	*red	= Cyber_colour_table [regno][0];
	*green	= Cyber_colour_table [regno][1];
	*blue	= Cyber_colour_table [regno][2];
	*transp = Cyber_colour_table [regno][3];
	return (0);
}


/*
 *    (Un)Blank the screen
 */

void Cyber_blank(int blank)
{
	int i;

	if (blank)
	{
		for (i = 0; i < 256; i++)
		{
			wb_64(0x3c8, (unsigned char) i);
			wb_64(0x3c9, 0);
			wb_64(0x3c9, 0);
			wb_64(0x3c9, 0);
		}
	}
	else
	{
		for (i = 0; i < 256; i++)
		{
			wb_64(0x3c8, (unsigned char) i);
			wb_64(0x3c9, Cyber_colour_table[i][0] >> 2);
			wb_64(0x3c9, Cyber_colour_table[i][1] >> 2);
			wb_64(0x3c9, Cyber_colour_table[i][2] >> 2);
		}
	}
}


/**************************************************************
 * We are waiting for "fifo" FIFO-slots empty
 */
static void Cyber_WaitQueue (u_short fifo)
{
	u_short status;

	do
	{
		status = *((u_short volatile *)(CyberRegs + S3_GP_STAT));
	}
	while (status & fifo);
}

/**************************************************************
 * We are waiting for Hardware (Graphics Engine) not busy
 */
static void Cyber_WaitBlit (void)
{
	u_short status;

	do
	{
		status = *((u_short volatile *)(CyberRegs + S3_GP_STAT));
	}
	while (status & S3_HDW_BUSY);
}

/**************************************************************
 * BitBLT - Through the Plane
 */
static void Cyber_BitBLT (u_short curx, u_short cury, u_short destx,
			  u_short desty, u_short width, u_short height,
			  u_short mode)
{
	u_short blitcmd = S3_BITBLT;

	/* Set drawing direction */
	/* -Y, X maj, -X (default) */
	if (curx > destx)
		blitcmd |= 0x0020;  /* Drawing direction +X */
	else
	{
		curx  += (width - 1);
		destx += (width - 1);
	}

	if (cury > desty)
		blitcmd |= 0x0080;  /* Drawing direction +Y */
	else
	{
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
}

/**************************************************************
 * Rectangle Fill Solid
 */
static void Cyber_RectFill (u_short x, u_short y, u_short width,
			    u_short height, u_short mode, u_short color)
{
	u_short blitcmd = S3_FILLEDRECT;

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
}

/**************************************************************
 * Move cursor to x, y
 */
static void Cyber_MoveCursor (u_short x, u_short y)
{
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
}


/* -------------------- Interfaces to hardware functions -------------------- */


static struct fb_hwswitch Cyber_switch = {
	Cyber_init, Cyber_encode_fix, Cyber_decode_var, Cyber_encode_var,
	Cyber_getcolreg, Cyber_setcolreg, Cyber_blank
};


/* -------------------- Generic routines ------------------------------------ */


/*
 *    Fill the hardware's `par' structure.
 */

static void cyberfb_get_par(struct cyberfb_par *par)
{
	if (current_par_valid)
	{
		*par = current_par;
	}
	else
	{
		fbhw->decode_var(&cyberfb_default, par);
	}
}


static void cyberfb_set_par(struct cyberfb_par *par)
{
	current_par = *par;
	current_par_valid = 1;
}


static void cyber_set_video(struct fb_var_screeninfo *var)
{
	/* Set clipping rectangle to current screen size */
 
	*((u_short volatile *)(CyberRegs + 0xbee8)) = 0x1000;
	*((u_short volatile *)(CyberRegs + 0xbee8)) = 0x2000;

	*((u_short volatile *)(CyberRegs + 0xbee8)) = 0x3000 | (var->yres - 1);
	*((u_short volatile *)(CyberRegs + 0xbee8)) = 0x4000 | (var->xres - 1);
}


static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
	int err, activate;
	struct cyberfb_par par;

	if ((err = fbhw->decode_var(var, &par)))
		return(err);
	activate = var->activate;
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
		cyberfb_set_par(&par);
	fbhw->encode_var(var, &par);
	var->activate = activate;

	cyber_set_video(var);
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
			    &fb_display[con].var, 1, fbhw->setcolreg, info);
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

	if (con == -1)
		cyberfb_get_par(&par);
	else
		error = fbhw->decode_var(&fb_display[con].var, &par);
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

	if (con == -1)
	{
		cyberfb_get_par(&par);
		error = fbhw->encode_var(var, &par);
		disp.var = *var;   /* ++Andre: don't know if this is the right place */
	}
	else
	{
		*var = fb_display[con].var;
	}

	return(error);
}


static void cyberfb_set_disp(int con, struct fb_info *info)
{
	struct fb_fix_screeninfo fix;
	struct display *display;

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	cyberfb_get_fix(&fix, con, info);
	if (con == -1)
		con = 0;
	display->screen_base = fix.smem_start;
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
}


/*
 *    Set the User Defined Part of the Display
 */

static int cyberfb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	int err, oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel;

	if ((err = do_fb_set_var(var, con == currcon)))
		return(err);
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
	return(0);
}


/*
 *    Get the Colormap
 */

static int cyberfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	if (con == currcon) /* current console? */
		return(fb_get_cmap(cmap, &fb_display[con].var,
				   kspc, fbhw->getcolreg, info));
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			     cmap, kspc ? 0 : 2);
	return(0);
}


/*
 *    Set the Colormap
 */

static int cyberfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {       /* no colormap allocated? */
		if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				 1<<fb_display[con].var.bits_per_pixel, 0)))
			return(err);
	}
	if (con == currcon)		 /* current console? */
		return(fb_set_cmap(cmap, &fb_display[con].var,
				   kspc, fbhw->setcolreg, info));
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
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

	fb_info.fontname[0] = '\0';

	if (!options || !*options)
		return;

	for (this_opt = strtok(options, ","); this_opt; this_opt = strtok(NULL, ","))
		if (!strcmp(this_opt, "inverse")) {
			Cyberfb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5))
			strcpy(fb_info.fontname, this_opt+5);
		else if (!strcmp (this_opt, "cyber8")){
			cyberfb_default = cyberfb_predefined[CYBER8_DEFMODE].var;
		}
		else if (!strcmp (this_opt, "cyber16")){
			cyberfb_default = cyberfb_predefined[CYBER16_DEFMODE].var;
		}
		else
			get_video_mode(this_opt);

	DPRINTK("default mode: xres=%d, yres=%d, bpp=%d\n",cyberfb_default.xres,
                                                           cyberfb_default.yres,
		                                           cyberfb_default.bits_per_pixel);
}


/*
 *    Initialization
 */

__initfunc(void cyberfb_init(void))
{
	struct cyberfb_par par;
	unsigned long board_addr;
	const struct ConfigDev *cd;

	if (!(CyberKey = zorro_find(ZORRO_PROD_PHASE5_CYBERVISION64, 0, 0)))
		return;

	cd = zorro_get_board (CyberKey);
	zorro_config_board (CyberKey, 0);
	board_addr = (unsigned long)cd->cd_BoardAddr;

	/* This includes the video memory as well as the S3 register set */
	CyberMem = kernel_map (board_addr + 0x01400000, 0x01000000,
			       KERNELMAP_NOCACHE_SER, NULL);
	CyberRegs = (char*) (CyberMem + 0x00c00000);

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

	if (register_framebuffer(&fb_info) < 0)
		return;

	printk("fb%d: %s frame buffer device, using %ldK of video memory\n",
	       GET_FB_IDX(fb_info.node), fb_info.modename, CyberSize>>10);

	/* TODO: This driver cannot be unloaded yet */
	MOD_INC_USE_COUNT;
}


static int Cyberfb_switch(int con, struct fb_info *info)
{
	/* Do we have to save the colormap? */
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
			    fbhw->getcolreg, info);

	do_fb_set_var(&fb_display[con].var, 1);
	currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
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
	return(0);
}


/*
    *    Blank the display.
    */

static void Cyberfb_blank(int blank, struct fb_info *info)
{
	fbhw->blank(blank);
}


/*
 *    Get a Video Mode
 */

__initfunc(static int get_video_mode(const char *name))
{
	int i;

	for (i = 0; i < NUM_TOTAL_MODES; i++) {
		if (!strcmp(name, cyberfb_predefined[i].name)) {
			cyberfb_default = cyberfb_predefined[i].var;
			return(i);
		}
	}
	/* ++Andre: set cyberfb default mode */
	cyberfb_default = cyberfb_predefined[CYBER8_DEFMODE].var;
	return(0);
}


/*
 *    Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static void fbcon_cyber8_bmove(struct display *p, int sy, int sx, int dy,
			       int dx, int height, int width)
{
    sx *= 8; dx *= 8; width *= 8;
    Cyber_BitBLT((u_short)sx, (u_short)(sy*p->fontheight), (u_short)dx,
		 (u_short)(dy*p->fontheight), (u_short)width,
		 (u_short)(height*p->fontheight), (u_short)S3_NEW);
}

static void fbcon_cyber8_clear(struct vc_data *conp, struct display *p, int sy,
			       int sx, int height, int width)
{
    unsigned char bg;

    sx *= 8; width *= 8;
    bg = attr_bgcol_ec(p,conp);
    Cyber_RectFill((u_short)sx,
		   (u_short)(sy*p->fontheight),
		   (u_short)width,
		   (u_short)(height*p->fontheight),
		   (u_short)S3_NEW,
		   (u_short)bg);
}

static void fbcon_cyber8_putc(struct vc_data *conp, struct display *p, int c,
			      int yy, int xx)
{
    Cyber_WaitBlit();
    fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_cyber8_putcs(struct vc_data *conp, struct display *p,
			       const unsigned short *s, int count, int yy, int xx)
{
    Cyber_WaitBlit();
    fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_cyber8_revc(struct display *p, int xx, int yy)
{
    Cyber_WaitBlit();
    fbcon_cfb8_revc(p, xx, yy);
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
