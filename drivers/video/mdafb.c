/*
 *  linux/drivers/video/mdafb.c -- MDA frame buffer device
 *
 *	Adapted from vgafb, May 1998 by Andrew Apted
 *
 *  This file is based on vgacon.c and vgafb.c.  Read about their
 *  contributors there.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */


/* KNOWN PROBLEMS/TO DO ==================================================== *
 *
 *	-  detecting amount of memory is not yet implemented
 *
 * ========================================================================= */


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
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/selection.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "fbcon.h"
#include "fbcon-vga.h"


#ifdef __powerpc__
#define VGA_OFFSET _ISA_MEM_BASE;
#else
#define VGA_OFFSET 0x0
#endif


static int mda_font_height = 16;   /* !!! */


static int currcon = 0;
static struct display disp;
static struct fb_info fb_info;

static struct fb_fix_screeninfo fb_fix = { { 0, } };
static struct fb_var_screeninfo fb_var = { 0, };


/* Description of the hardware situation */
static unsigned char mda_video_type;
static unsigned long mda_video_mem_base;	/* Base of video memory */
static unsigned long mda_video_mem_len;		/* End of video memory */
static u16 mda_video_port_reg;			/* Video register select port */
static u16 mda_video_port_val;			/* Video register value port */
static unsigned long mda_video_num_columns;	/* Number of text columns */
static unsigned long mda_video_num_lines;	/* Number of text lines */


    /*
     *  MDA screen access
     */ 

static inline void mda_writew(u16 val, u16 *addr)
{
#ifdef __powerpc__
	st_le16(addr, val);
#else
	writew(val, (unsigned long)addr);
#endif /* !__powerpc__ */
}

static inline u16 mda_readw(u16 *addr)
{
#ifdef __powerpc__
	return ld_le16(addr);
#else
	return readw((unsigned long)addr);
#endif /* !__powerpc__ */	
}


static inline void write_mda(unsigned char reg, unsigned int val)
{
	unsigned long flags;

	save_flags(flags); cli();

	outb(reg, mda_video_port_reg);
	outb(val >> 8, mda_video_port_val);
	outb(reg+1, mda_video_port_reg);
	outb(val & 0xff, mda_video_port_val);

	restore_flags(flags);
}

static inline void mda_set_origin(unsigned short location)
{
	write_mda(12, location >> 1);
}

static inline void mda_set_cursor(int location) 
{
	write_mda(14, location >> 1);
}


    /*
     *  Move hardware mda cursor
     */
    
void fbcon_mdafb_cursor(struct display *p, int mode, int x, int y)
{
	switch (mode) {
		case CM_ERASE:
			mda_set_cursor(mda_video_mem_len - 1);
			break;

		case CM_MOVE:
		case CM_DRAW:
			mda_set_cursor(y*p->next_line + (x << 1));
			break;
	}
}


    /*
     *  Interface to the low level console driver
     */

void mdafb_setup(char *options, int *ints);
static int mdafbcon_switch(int con, struct fb_info *info);
static int mdafbcon_updatevar(int con, struct fb_info *info);
static void mdafbcon_blank(int blank, struct fb_info *info);


    /*
     *  Open/Release the frame buffer device
     */

static int mdafb_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int mdafb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}


    /*
     *  Get the Fixed Part of the Display
     */

static int mdafb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	memcpy(fix, &fb_fix, sizeof(fb_fix));
	return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int mdafb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	memcpy(var, &fb_var, sizeof(fb_var));
	return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int mdafb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    struct display *display;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = &disp;	/* used during initialization */

    if (var->xres > fb_var.xres || var->yres > fb_var.yres ||
	var->xres_virtual > fb_var.xres_virtual ||
	var->yres_virtual > fb_var.yres_virtual ||
	var->bits_per_pixel > fb_var.bits_per_pixel ||
	var->nonstd || !(var->accel_flags & FB_ACCELF_TEXT) ||
	(var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
	return -EINVAL;

    memcpy(var, &fb_var, sizeof(fb_var));

    if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	display->var = *var;
	mda_set_origin(var->yoffset/mda_font_height*fb_fix.line_length);
    }

    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int mdafb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
	if (var->xoffset || var->yoffset+var->yres > var->yres_virtual)
		return -EINVAL;

	mda_set_origin(var->yoffset/mda_font_height*fb_fix.line_length);
	return 0;
}


    /*
     *  Get the Colormap
     */

static int mdafb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	/* MDA is simply black and white */

	return 0;
}


    /*
     *  Set the Colormap
     */

static int mdafb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	/* MDA is simply black and white */

	return 0;
}


static int mdafb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
	return -EINVAL;
}

    /*
     *  Interface used by the world
     */

static struct fb_ops mdafb_ops = {
	mdafb_open, mdafb_release, mdafb_get_fix, mdafb_get_var,
	mdafb_set_var, mdafb_get_cmap, mdafb_set_cmap, mdafb_pan_display, 
	mdafb_ioctl
};

    /*
     *  MDA text console with hardware cursor
     */

static struct display_switch fbcon_mdafb = {
    fbcon_vga_setup, fbcon_vga_bmove, fbcon_vga_clear, fbcon_vga_putc,
    fbcon_vga_putcs, fbcon_vga_revc, fbcon_mdafb_cursor
};


    /*
     *  Initialisation
     */

__initfunc(void mdafb_init(void))
{
    u16 saved;
    u16 *p;

    mda_video_num_lines = 25;
    mda_video_num_columns = 80;
    mda_video_type = VIDEO_TYPE_MDA;
    mda_video_mem_base = 0xb0000 + VGA_OFFSET;
    mda_video_mem_len  = 0x01000;
    mda_video_port_reg = 0x3b4;
    mda_video_port_val = 0x3b5;

    strcpy(fb_fix.id, "MDA-Dual-Head");
    request_region(0x3b0, 12, "mda-2");
    request_region(0x3bf, 1, "mda-2");

    /*
     *	Find out if there is a graphics card present.
     *	Are there smarter methods around?
     */
    p = (u16 *)mda_video_mem_base;
    saved = mda_readw(p);
    mda_writew(0xAA55, p);
    if (mda_readw(p) != 0xAA55) {
	mda_writew(saved, p);
	return;
    }
    mda_writew(0x55AA, p);
    if (mda_readw(p) != 0x55AA) {
	mda_writew(saved, p);
	return;
    }
    mda_writew(saved, p);

    fb_fix.smem_start = (char *) mda_video_mem_base;
    fb_fix.smem_len = mda_video_mem_len;
    fb_fix.type = FB_TYPE_TEXT;
    fb_fix.type_aux = FB_AUX_TEXT_MDA;
    fb_fix.visual = FB_VISUAL_PSEUDOCOLOR;
    fb_fix.ypanstep = mda_font_height;
    fb_fix.xpanstep = 0;
    fb_fix.ywrapstep = 0;
    fb_fix.line_length = 2 * mda_video_num_columns;
    fb_fix.mmio_start = NULL;
    fb_fix.mmio_len = 0;
    fb_fix.accel = FB_ACCEL_NONE;

    fb_var.xres = mda_video_num_columns*8;
    fb_var.yres = mda_video_num_lines * mda_font_height;
    fb_var.xres_virtual = fb_var.xres;
    /* the cursor is put at the end of the video memory, hence the -2 */
    fb_var.yres_virtual = ((fb_fix.smem_len-2)/fb_fix.line_length)*
			  mda_font_height;

    fb_var.xoffset = fb_var.yoffset = 0;
    fb_var.bits_per_pixel = 1;
    fb_var.grayscale = 1;
    fb_var.red.offset = 0;
    fb_var.red.length = 0;
    fb_var.red.msb_right = 0;
    fb_var.green.offset = 0;
    fb_var.green.length = 0;
    fb_var.green.msb_right = 0;
    fb_var.blue.offset = 0;
    fb_var.blue.length = 0;
    fb_var.blue.msb_right = 0;
    fb_var.transp.offset = 0;
    fb_var.transp.length = 0;
    fb_var.transp.msb_right = 0;
    fb_var.nonstd = 0;
    fb_var.activate = 0;
    fb_var.height = fb_var.width = -1;
    fb_var.accel_flags = FB_ACCELF_TEXT;
    fb_var.pixclock = 39722;		/* 25.175 MHz */
    fb_var.left_margin = 40;
    fb_var.right_margin = 24;
    fb_var.upper_margin = 39;
    fb_var.lower_margin = 9;
    fb_var.hsync_len = 96;
    fb_var.vsync_len = 2;
    fb_var.sync = 0;
    fb_var.vmode = FB_VMODE_NONINTERLACED;

    disp.var = fb_var;
    disp.cmap.start = 0;
    disp.cmap.len = 0;
    disp.cmap.red = NULL;
    disp.cmap.green = NULL;
    disp.cmap.blue = NULL;
    disp.cmap.transp = NULL;

#ifdef __i386__
    disp.screen_base = ioremap((unsigned long) fb_fix.smem_start, 
    				fb_fix.smem_len);
#else
    disp.screen_base = bus_to_virt((unsigned long) fb_fix.smem_start);
#endif
    disp.visual = fb_fix.visual;
    disp.type = fb_fix.type;
    disp.type_aux = fb_fix.type_aux;
    disp.ypanstep = fb_fix.ypanstep;
    disp.ywrapstep = fb_fix.ywrapstep;
    disp.line_length = fb_fix.line_length;
    disp.can_soft_blank = 1;
    disp.inverse = 0;
    disp.dispsw = &fbcon_mdafb;

    strcpy(fb_info.modename, fb_fix.id);
    fb_info.node = -1;
    fb_info.fbops = &mdafb_ops;
    fb_info.disp = &disp;
    fb_info.fontname[0] = '\0';
    fb_info.changevar = NULL;
    fb_info.switch_con = &mdafbcon_switch;
    fb_info.updatevar = &mdafbcon_updatevar;
    fb_info.blank = &mdafbcon_blank;

    mdafb_set_var(&fb_var, -1, &fb_info);

    if (register_framebuffer(&fb_info) < 0)
	return;

    printk("fb%d: MDA frame buffer device, using %dK of video memory\n",
	   GET_FB_IDX(fb_info.node), fb_fix.smem_len>>10);
}

__initfunc(void mdafb_setup(char *options, int *ints))
{
	/* nothing yet */
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int mdafbcon_updatevar(int con, struct fb_info *info)
{
	if (con == currcon) {
		struct fb_var_screeninfo *var = &fb_display[currcon].var;

		/* hardware scrolling */

		mda_set_origin(var->yoffset / mda_font_height *
			fb_fix.line_length);
	}

	return 0;
}

static int mdafbcon_switch(int con, struct fb_info *info)
{
	currcon = con;
	mdafbcon_updatevar(con, info);
	return 0;
}

    /*
     *  Blank the display.
     */

static void mdafbcon_blank(int blank, struct fb_info *info)
{
	if (blank) {
		outb_p(0x00, 0x3b8);  /* disable video */
	} else {
		outb_p(0x08, 0x3b8);  /* enable video */
	}
}


#ifdef MODULE
int init_module(void)
{
	mdafb_init();
	return 0;
}

void cleanup_module(void)
{
	unregister_framebuffer(&fb_info);
}
#endif /* MODULE */
