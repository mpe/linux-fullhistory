/*
 *  linux/drivers/video/vgafb.c -- VGA frame buffer device
 *
 *	Created 28 Mar 1998 by Geert Uytterhoeven
 *	Hardware cursor support added on 14 Apr 1998 by Emmanuel Marty
 *
 *  This file is heavily based on vgacon.c. Read about its contributors there.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */




/* KNOWN PROBLEMS/TO DO ===================================================== *
 *
 *	- add support for loadable fonts and VESA blanking
 *
 *	- for now only VGA _text_ mode is supported
 *
 * KNOWN PROBLEMS/TO DO ==================================================== */


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
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/vt_kern.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "fbcon.h"
#include "fbcon-vga.h"


#define BLANK 0x0020

#define CAN_LOAD_EGA_FONTS	/* undefine if the user must not do this */
#define CAN_LOAD_PALETTE	/* undefine if the user must not do this */

#define dac_reg	(0x3c8)
#define dac_val	(0x3c9)

#ifdef __powerpc__
#define VGA_OFFSET _ISA_MEM_BASE;
#else
#define VGA_OFFSET 0x0
#endif


static int currcon = 0;
static struct display disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[16];

static struct fb_fix_screeninfo fb_fix = { { 0, } };
static struct fb_var_screeninfo fb_var = { 0, };


/* Description of the hardware situation */
static unsigned char vga_video_type;
static unsigned long vga_video_mem_base;	/* Base of video memory */
static unsigned long vga_video_mem_term;	/* End of video memory */
static u16 vga_video_port_reg;			/* Video register select port */
static u16 vga_video_port_val;			/* Video register value port */
static unsigned long vga_video_num_columns;	/* Number of text columns */
static unsigned long vga_video_num_lines;	/* Number of text lines */
static int vga_can_do_color = 0;


    /*
     *  VGA screen access
     */ 

static inline void vga_writew(u16 val, u16 *addr)
{
#ifdef __powerpc__
    st_le16(addr, val);
#else
    writew(val, (unsigned long)addr);
#endif /* !__powerpc__ */
}

static inline u16 vga_readw(u16 *addr)
{
#ifdef __powerpc__
    return ld_le16(addr);
#else
    return readw((unsigned long)addr);
#endif /* !__powerpc__ */	
}

/*
 * By replacing the four outb_p with two back to back outw, we can reduce
 * the window of opportunity to see text mislocated to the RHS of the
 * console during heavy scrolling activity. However there is the remote
 * possibility that some pre-dinosaur hardware won't like the back to back
 * I/O. Since the Xservers get away with it, we should be able to as well.
 */
static inline void write_vga(unsigned char reg, unsigned int val)
{
#ifndef SLOW_VGA
    unsigned int v1, v2;

    v1 = reg + (val & 0xff00);
    v2 = reg + 1 + ((val << 8) & 0xff00);
    outw(v1, vga_video_port_reg);
    outw(v2, vga_video_port_reg);
#else
    outb_p(reg, vga_video_port_reg);
    outb_p(val >> 8, vga_video_port_val);
    outb_p(reg+1, vga_video_port_reg);
    outb_p(val & 0xff, vga_video_port_val);
#endif
}

static inline void vga_set_origin(unsigned short location)
{
	write_vga(12, location >> 1);
}

static inline void vga_set_cursor(int location)
{
    write_vga(14, location >> 1);
}

static void vga_set_split(unsigned short linenum)
{
	unsigned long flags;
	unsigned char overflow, fontsize;
	
	if (vga_video_type != VIDEO_TYPE_VGAC) {
		return;
	}

	save_flags(flags); cli();

	outb_p(0x07, vga_video_port_reg);
	overflow = inb_p(vga_video_port_val);

	outb_p(0x09, vga_video_port_reg);
	fontsize = inb_p(vga_video_port_val);
	
	overflow &= ~0x10; overflow |= (linenum & 0x100) ? 0x10 : 0;
	fontsize &= ~0x40; fontsize |= (linenum & 0x200) ? 0x40 : 0;
	linenum  &=  0xff;

	outb_p(0x18, vga_video_port_reg);
	outb_p(linenum, vga_video_port_val);

	outb_p(0x07, vga_video_port_reg);
	outb_p(overflow, vga_video_port_val);

	outb_p(0x09, vga_video_port_reg);
	outb_p(fontsize, vga_video_port_val);

	restore_flags(flags);
}

static inline void vga_set_palreg(u_int regno, u_int red, 
				  u_int green, u_int blue)
{
	unsigned long flags;

	save_flags(flags); cli();
	
	outb_p(regno, dac_reg);
	outb_p(red,   dac_val);
	outb_p(green, dac_val);
	outb_p(blue,  dac_val);
	
	restore_flags(flags);
}


    /*
     *  Interface used by the world
     */

static int vgafb_open(struct fb_info *info, int user);
static int vgafb_release(struct fb_info *info, int user);
static int vgafb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int vgafb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int vgafb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int vgafb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info);
static int vgafb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int vgafb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int vgafb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info);


    /*
     *  Interface to the low level console driver
     */

void vgafb_init(void);
void vgafb_setup(char *options, int *ints);
static int vgafbcon_switch(int con, struct fb_info *info);
static int vgafbcon_updatevar(int con, struct fb_info *info);
static void vgafbcon_blank(int blank, struct fb_info *info);


    /*
     *  VGA text console with hardware cursor
     */

static struct display_switch fbcon_vgafb;


    /*
     *  Internal routines
     */

static int vgafb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info);
static int vgafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);


static struct fb_ops vgafb_ops = {
    vgafb_open, vgafb_release, vgafb_get_fix, vgafb_get_var, vgafb_set_var,
    vgafb_get_cmap, vgafb_set_cmap, vgafb_pan_display, vgafb_ioctl
};


    /*
     *  Open/Release the frame buffer device
     */

static int vgafb_open(struct fb_info *info, int user)

{
    /*
     *  Nothing, only a usage count for the moment
     */

    MOD_INC_USE_COUNT;
    return(0);
}

static int vgafb_release(struct fb_info *info, int user)
{
    MOD_DEC_USE_COUNT;
    return(0);
}


    /*
     *  Get the Fixed Part of the Display
     */

static int vgafb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
    memcpy(fix, &fb_fix, sizeof(fb_fix));
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int vgafb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    memcpy(var, &fb_var, sizeof(fb_var));
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int vgafb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
    struct display *display;
    int oldbpp = -1, err;

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
	oldbpp = display->var.bits_per_pixel;
	display->var = *var;
	vga_set_origin(var->yoffset/video_font_height*fb_fix.line_length);
    }

    if (oldbpp != var->bits_per_pixel) {
	if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
	    return err;
	do_install_cmap(con, info);
    }
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int vgafb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
    if (var->xoffset || var->yoffset+var->yres > var->yres_virtual)
	return -EINVAL;

    vga_set_origin(var->yoffset/video_font_height*fb_fix.line_length);
    return 0;
}


    /*
     *  Get the Colormap
     */

static int vgafb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
    if (con == currcon) /* current console? */
	return fb_get_cmap(cmap, &fb_display[con].var, kspc, vgafb_getcolreg,
			   info);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else
	fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
		     cmap, kspc ? 0 : 2);
    return 0;
}


    /*
     *  Set the Colormap
     */

static int vgafb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
    int err;

    if (!fb_display[con].cmap.len) {    /* no colormap allocated? */
	if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				 1<<fb_display[con].var.bits_per_pixel, 0)))
	    return err;
    }
    if (con == currcon) {		/* current console? */
	err = fb_set_cmap(cmap, &fb_display[con].var, kspc, vgafb_setcolreg,
			  info);
	return err;
    } else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


static int vgafb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}


    /*
     *  Move hardware vga cursor
     */

static void fbcon_vgafb_cursor(struct display *p, int mode, int x, int y)
{
    switch (mode) {
	case CM_ERASE:
	   vga_set_cursor(vga_video_mem_term - vga_video_mem_base - 1);
	   break;

	case CM_MOVE:
	case CM_DRAW:
	   vga_set_cursor(y*p->next_line + (x << 1));
	   break;
    }
}


    /*
     *  Initialisation
     */

__initfunc(void vgafb_init(void))
{
    u16 saved;
    u16 *p;

    if (screen_info.orig_video_isVGA == VIDEO_TYPE_VLFB)
	return;

    vga_video_num_lines = ORIG_VIDEO_LINES;
    vga_video_num_columns = ORIG_VIDEO_COLS;

    if (ORIG_VIDEO_MODE == 7) {		/* Is this a monochrome display? */
	vga_video_mem_base = 0xb0000 + VGA_OFFSET;
	vga_video_port_reg = 0x3b4;
	vga_video_port_val = 0x3b5;
	if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
	    vga_video_type = VIDEO_TYPE_EGAM;
	    vga_video_mem_term = 0xb8000 + VGA_OFFSET;
	    strcpy(fb_fix.id, "EGA+");
	    request_region(0x3b0, 16, "ega");
	} else {
	    vga_video_type = VIDEO_TYPE_MDA;
	    vga_video_mem_term = 0xb1000 + VGA_OFFSET;
	    strcpy(fb_fix.id, "*MDA");
	    request_region(0x3b0, 12, "mda");
	    request_region(0x3bf, 1, "mda");
	}
    } else {				/* If not, it is color. */
	vga_can_do_color = 1;
	vga_video_mem_base = 0xb8000  + VGA_OFFSET;
	vga_video_port_reg = 0x3d4;
	vga_video_port_val = 0x3d5;
	if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
	    int i;

	    vga_video_mem_term = 0xc0000 + VGA_OFFSET;

	    if (!ORIG_VIDEO_ISVGA) {
		vga_video_type = VIDEO_TYPE_EGAC;
		strcpy(fb_fix.id, "EGA");
		request_region(0x3c0, 32, "ega");
	    } else {
		vga_video_type = VIDEO_TYPE_VGAC;
		strcpy(fb_fix.id, "VGA+");
		request_region(0x3c0, 32, "vga+");

#ifdef VGA_CAN_DO_64KB
		/*
		 * get 64K rather than 32K of video RAM.
		 * This doesn't actually work on all "VGA"
		 * controllers (it seems like setting MM=01
		 * and COE=1 isn't necessarily a good idea)
		 */
		vga_video_mem_base = 0xa0000  + VGA_OFFSET;
		vga_video_mem_term = 0xb0000  + VGA_OFFSET;
		outb_p(6, 0x3ce);
		outb_p(6, 0x3cf);
#endif

		/*
		 * Normalise the palette registers, to point
		 * the 16 screen colours to the first 16
		 * DAC entries.
		 */

		for (i = 0; i < 16; i++) {
		    inb_p(0x3da);
		    outb_p(i, 0x3c0);
		    outb_p(i, 0x3c0);
		}
		outb_p(0x20, 0x3c0);

		/* now set the DAC registers back to their
		 * default values */

		for (i = 0; i < 16; i++) {
		    vga_set_palreg(color_table[i], default_red[i],
		    		   default_grn[i], default_blu[i]);
		}
	    }
	} else {
		vga_video_type = VIDEO_TYPE_CGA;
		vga_video_mem_term = 0xba000 + VGA_OFFSET;
		strcpy(fb_fix.id, "*CGA");
		request_region(0x3d4, 2, "cga");
	}
    }

    /*
     *	Find out if there is a graphics card present.
     *	Are there smarter methods around?
     */
    p = (u16 *)vga_video_mem_base;
    saved = vga_readw(p);
    vga_writew(0xAA55, p);
    if (vga_readw(p) != 0xAA55) {
	vga_writew(saved, p);
	return;
    }
    vga_writew(0x55AA, p);
    if (vga_readw(p) != 0x55AA) {
	vga_writew(saved, p);
	return;
    }
    vga_writew(saved, p);

    if (vga_video_type == VIDEO_TYPE_VGAC
	|| vga_video_type == VIDEO_TYPE_EGAC
	|| vga_video_type == VIDEO_TYPE_EGAM) {
	    video_font_height = ORIG_VIDEO_POINTS;
    } else {
            video_font_height = 16;
    }

    /* This may be suboptimal but is a safe bet - go with it */
    video_scan_lines = video_font_height * vga_video_num_lines;

    fb_fix.smem_start = (char *) vga_video_mem_base;
    fb_fix.smem_len = vga_video_mem_term - vga_video_mem_base;
    fb_fix.type = FB_TYPE_TEXT;
    fb_fix.type_aux = vga_can_do_color ? FB_AUX_TEXT_CGA : FB_AUX_TEXT_MDA;
    fb_fix.visual = FB_VISUAL_PSEUDOCOLOR;
    fb_fix.xpanstep = 0;
    fb_fix.ypanstep = video_font_height;
    fb_fix.ywrapstep = 0;
    fb_fix.line_length = 2*vga_video_num_columns;
    fb_fix.mmio_start = NULL;
    fb_fix.mmio_len = 0;
    fb_fix.accel = FB_ACCEL_NONE;

    fb_var.xres = vga_video_num_columns*8;
    fb_var.yres = vga_video_num_lines*video_font_height;
    fb_var.xres_virtual = fb_var.xres;
    /* the cursor is put at the end of the video memory, hence the -2 */
    fb_var.yres_virtual = ((fb_fix.smem_len-2)/fb_fix.line_length)*
			  video_font_height;

    fb_var.xoffset = fb_var.yoffset = 0;
    fb_var.bits_per_pixel = vga_can_do_color ? 4 : 1;
    fb_var.grayscale = !vga_can_do_color;
    fb_var.red.offset = 0;
    fb_var.red.length = 6;
    fb_var.red.msb_right = 0;
    fb_var.green.offset = 0;
    fb_var.green.length = 6;
    fb_var.green.msb_right = 0;
    fb_var.blue.offset = 0;
    fb_var.blue.length = 6;
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
    disp.can_soft_blank = vga_can_do_color;
    disp.inverse = 0;
    disp.dispsw = &fbcon_vgafb;

    strcpy(fb_info.modename, fb_fix.id);
    fb_info.node = -1;
    fb_info.fbops = &vgafb_ops;
    fb_info.disp = &disp;
    fb_info.fontname[0] = '\0';
    fb_info.changevar = NULL;
    fb_info.switch_con = &vgafbcon_switch;
    fb_info.updatevar = &vgafbcon_updatevar;
    fb_info.blank = &vgafbcon_blank;

    vgafb_set_var(&fb_var, -1, &fb_info);

    if (register_framebuffer(&fb_info) < 0)
	return;

    printk("fb%d: VGA frame buffer device, using %dK of video memory\n",
	   GET_FB_IDX(fb_info.node), fb_fix.smem_len>>10);
}

__initfunc(void vgafb_setup(char *options, int *ints))
{
    /* nothing yet */
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int vgafbcon_updatevar(int con, struct fb_info *info)
{
	if (con == currcon) {
		struct fb_var_screeninfo *var = &fb_display[currcon].var;

		/* hardware scrolling */

		vga_set_origin(var->yoffset / video_font_height *
			fb_fix.line_length);

		vga_set_split(var->yres - ((var->vmode & FB_VMODE_YWRAP) ?
			var->yoffset+1 : 0));
	}

	return 0;
}

static int vgafbcon_switch(int con, struct fb_info *info)
{
    /* Do we have to save the colormap? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
		    vgafb_getcolreg, info);

    currcon = con;
    /* Install new colormap */
    do_install_cmap(con, info);
    vgafbcon_updatevar(con, info);
    return 0;
}

    /*
     *  Blank the display.
     */

static void vgafbcon_blank(int blank, struct fb_info *info)
{
    int i;

    if (blank)
	for (i = 0; i < 16; i++) {
	    vga_set_palreg(i, 0, 0, 0);
	}
    else
	for (i = 0; i < 16; i++) {
	    vga_set_palreg(i, palette[i].red, palette[i].green,
			      palette[i].blue);
	}
}


    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int vgafb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			 u_int *transp, struct fb_info *info)
{
    if (regno > 15)
	return 1;
    *red = palette[regno].red;
    *green = palette[regno].green;
    *blue = palette[regno].blue;
    return 0;
}


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int vgafb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
    if (regno > 15)
	return 1;
    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;

    vga_set_palreg(regno, red, green, blue);

    return 0;
}

static void do_install_cmap(int con, struct fb_info *info)
{
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
		    vgafb_setcolreg, info);
    else
	fb_set_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
				    &fb_display[con].var, 1, vgafb_setcolreg,
				    info);
}


    /*
     *  VGA text console with hardware cursor
     */

static struct display_switch fbcon_vgafb = {
    fbcon_vga_setup, fbcon_vga_bmove, fbcon_vga_clear, fbcon_vga_putc,
    fbcon_vga_putcs, fbcon_vga_revc, fbcon_vgafb_cursor
};


#ifdef MODULE
int init_module(void)
{
    vgafb_init();
    return 0;
}

void cleanup_module(void)
{
    unregister_framebuffer(&fb_info);
}
#endif /* MODULE */
