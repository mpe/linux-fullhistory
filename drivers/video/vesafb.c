/*
 * framebuffer driver for VBE 2.0 compliant graphic boards
 *
 * switching to graphics mode happens at boot time (while
 * running in real mode, see arch/i386/video.S).
 *
 * (c) 1998 Gerd Knorr <kraxel@cs.tu-berlin.de>
 *
 */ 

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

#include <asm/io.h>

#include "fbcon.h"
#include "fbcon-cfb8.h"
#include "fbcon-cfb16.h"
#include "fbcon-cfb32.h"

#define dac_reg	(0x3c8)
#define dac_val	(0x3c9)

/* --------------------------------------------------------------------- */

/*
 * card parameters
 */

/* card */
char *video_base;
int   video_size;
char *video_vbase;        /* mapped */

/* mode */
int  video_bpp;
int  video_width;
int  video_height;
int  video_type = FB_TYPE_PACKED_PIXELS;
int  video_visual;
int  video_linelength;
int  video_cmap_len;

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo vesafb_defined = {
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
	0,
	0L,0L,0L,0L,0L,
	0L,0L,0,	/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0,0,0,0,0,0}
};

static struct display disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[256];

static int inverse = 0;

static int currcon = 0;

/* --------------------------------------------------------------------- */
/* speed up scrolling                                                    */

#define USE_REDRAW   1
#define USE_MEMMOVE  2
  
static int vesafb_scroll = USE_REDRAW;
static struct display_switch vesafb_sw;

/* --------------------------------------------------------------------- */

	/*
	 * Open/Release the frame buffer device
	 */

static int vesafb_open(struct fb_info *info, int user)
{
	/*
	 * Nothing, only a usage count for the moment
	 */
	MOD_INC_USE_COUNT;
	return(0);
}

static int vesafb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return(0);
}

static int fb_update_var(int con, struct fb_info *info)
{
	return 0;
}

static int vesafb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id,"VESA VGA");

	fix->smem_start=(char *) video_base;
	fix->smem_len=video_size;
	fix->type = video_type;
	fix->visual = video_visual;
	fix->xpanstep=0;
	fix->ypanstep=0;
	fix->ywrapstep=0;
	fix->line_length=video_linelength;
	return 0;
}

static int vesafb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	if(con==-1)
		memcpy(var, &vesafb_defined, sizeof(struct fb_var_screeninfo));
	else
		*var=fb_display[con].var;
	return 0;
}

static void vesafb_set_disp(int con)
{
	struct fb_fix_screeninfo fix;
	struct display *display;
	struct display_switch *sw;
	
	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	vesafb_get_fix(&fix, con, 0);

	memset(display, 0, sizeof(struct display));
	display->screen_base = video_vbase;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->next_line = fix.line_length;
	display->can_soft_blank = 0;
	display->inverse = inverse;
	vesafb_get_var(&display->var, -1, &fb_info);

	switch (video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
		sw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 15:
	case 16:
		sw = &fbcon_cfb16;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		sw = &fbcon_cfb32;
		break;
#endif
	default:
		return;
	}
	memcpy(&vesafb_sw, sw, sizeof(*sw));
	display->dispsw = &vesafb_sw;
	if (vesafb_scroll == USE_REDRAW) {
		display->scrollmode = SCROLL_YREDRAW;
		vesafb_sw.bmove = fbcon_redraw_bmove;
	}
}

static int vesafb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	memcpy(var, &vesafb_defined, sizeof(struct fb_var_screeninfo));
	return 0;
}

static int vesa_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			  unsigned *blue, unsigned *transp,
			  struct fb_info *fb_info)
{
	/*
	 *  Read a single color register and split it into colors/transparent.
	 *  Return != 0 for invalid regno.
	 */

	if (regno >= video_cmap_len)
		return 1;

	*red   = palette[regno].red;
	*green = palette[regno].green;
	*blue  = palette[regno].blue;
	return 0;
}

static int vesa_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp,
			  struct fb_info *fb_info)
{
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	
	if (regno >= video_cmap_len)
		return 1;
	
	palette[regno].red   = red;
	palette[regno].green = green;
	palette[regno].blue  = blue;
	
	switch (video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
		/* Hmm, can we do it _always_ this way ??? */
		outb_p(regno, dac_reg);
		outb_p(red, dac_val);
		outb_p(green, dac_val);
		outb_p(blue, dac_val);
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 15:
	case 16:
		fbcon_cfb16_cmap[regno] =
			(red << vesafb_defined.red.offset) | (green << 5) | blue;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		/* FIXME: todo */
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		fbcon_cfb32_cmap[regno] =
			(red   << vesafb_defined.red.offset)   |
			(green << vesafb_defined.green.offset) |
			(blue  << vesafb_defined.blue.offset);
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
		fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
			    vesa_setcolreg, info);
	else
		fb_set_cmap(fb_default_cmap(video_cmap_len),
			    &fb_display[con].var, 1, vesa_setcolreg,
			    info);
}

static int vesafb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info)
{
	if (con == currcon) /* current console? */
		return fb_get_cmap(cmap, &fb_display[con].var, kspc, vesa_getcolreg, info);
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(video_cmap_len),
		     cmap, kspc ? 0 : 2);
	return 0;
}

static int vesafb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
		err = fb_alloc_cmap(&fb_display[con].cmap,video_cmap_len,0);
		if (err)
			return err;
	}
	if (con == currcon)			/* current console? */
		return fb_set_cmap(cmap, &fb_display[con].var, kspc, vesa_setcolreg, info);
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}

static int vesafb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
	/* no panning */
	return -EINVAL;
}

static int vesafb_ioctl(struct inode *inode, struct file *file, 
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info)
{
	return -EINVAL;
}

static struct fb_ops vesafb_ops = {
	vesafb_open,
	vesafb_release,
	vesafb_get_fix,
	vesafb_get_var,
	vesafb_set_var,
	vesafb_get_cmap,
	vesafb_set_cmap,
	vesafb_pan_display,
	vesafb_ioctl
};

void vesafb_setup(char *options, int *ints)
{
	char *this_opt;
	
	fb_info.fontname[0] = '\0';
	
	if (!options || !*options)
		return;
	
	for(this_opt=strtok(options,","); this_opt; this_opt=strtok(NULL,",")) {
		if (!*this_opt) continue;
		
		printk("vesafb_setup: option %s\n", this_opt);
		
		if (! strcmp(this_opt, "inverse"))
			inverse=1;
		else if (! strcmp(this_opt, "redraw"))
			vesafb_scroll = USE_REDRAW;
		else if (! strcmp(this_opt, "memmove"))
			vesafb_scroll = USE_MEMMOVE;
		else if (!strncmp(this_opt, "font:", 5))
			strcpy(fb_info.fontname, this_opt+5);
	}
}

static int vesafb_switch(int con, struct fb_info *info)
{
	/* Do we have to save the colormap? */
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1,
			    vesa_getcolreg, info);
	
	currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */

static void vesafb_blank(int blank, struct fb_info *info)
{
	/* Not supported */
}

__initfunc(void vesafb_init(void))
{
	int i,j;

	if (screen_info.orig_video_isVGA != VIDEO_TYPE_VLFB)
		return;

	video_base          = (char*)screen_info.lfb_base;
	video_bpp           = screen_info.lfb_depth;
	video_width         = screen_info.lfb_width;
	video_height        = screen_info.lfb_height;
	video_linelength    = screen_info.lfb_linelength;
	video_size          = video_linelength * video_height /* screen_info.lfb_size */;
	video_visual = (video_bpp == 8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
        video_vbase = ioremap((unsigned long)video_base, video_size);

	printk("vesafb: %dx%dx%d, linelength=%d\n",
	       video_width, video_height, video_bpp, video_linelength);
	printk("vesafb: framebuffer at 0x%p, mapped to 0x%p, size %d\n",
	       video_base, video_vbase, video_size);
	if (vesafb_scroll == USE_REDRAW)  printk("vesafb: scrolling=redraw\n");
	if (vesafb_scroll == USE_MEMMOVE) printk("vesafb: scrolling=memmove\n");
	 
	vesafb_defined.xres=video_width;
	vesafb_defined.yres=video_height;
	vesafb_defined.xres_virtual=video_width;
	vesafb_defined.yres_virtual=video_height;
	vesafb_defined.bits_per_pixel=video_bpp;

	if (video_bpp > 8) {
		vesafb_defined.red.offset    = screen_info.red_pos;
		vesafb_defined.red.length    = screen_info.red_size;
		vesafb_defined.green.offset  = screen_info.green_pos;
		vesafb_defined.green.length  = screen_info.green_size;
		vesafb_defined.blue.offset   = screen_info.blue_pos;
		vesafb_defined.blue.length   = screen_info.blue_size;
		vesafb_defined.transp.offset = screen_info.rsvd_pos;
		vesafb_defined.transp.length = screen_info.rsvd_size;
		printk("vesafb: directcolor: "
		       "size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
		       screen_info.rsvd_size,
		       screen_info.red_size,
		       screen_info.green_size,
		       screen_info.blue_size,
		       screen_info.rsvd_pos,
		       screen_info.red_pos,
		       screen_info.green_pos,
		       screen_info.blue_pos);
		video_cmap_len = 16;
	} else {
		vesafb_defined.red.length   = 6;
		vesafb_defined.green.length = 6;
		vesafb_defined.blue.length  = 6;
		for(i = 0; i < 16; i++) {
			j = color_table[i];
			palette[i].red   = default_red[j];
			palette[i].green = default_grn[j];
			palette[i].blue  = default_blu[j];
		}
		video_cmap_len = 256;
	}
	request_region(0x3c0, 32, "vga+");
	
	strcpy(fb_info.modename, "VESA VGA");
	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &vesafb_ops;
	fb_info.disp=&disp;
	fb_info.switch_con=&vesafb_switch;
	fb_info.updatevar=&fb_update_var;
	fb_info.blank=&vesafb_blank;
	vesafb_set_disp(-1);

	if (register_framebuffer(&fb_info)<0)
		return;

	printk("fb%d: %s frame buffer device\n",
	       GET_FB_IDX(fb_info.node), fb_info.modename);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
