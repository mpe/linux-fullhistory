/*
 * linux/drivers/video/sa1100fb.c -- StrongARM 1100 LCD Controller Frame Buffer Device
 *
 *  Copyright (C) 1999 Eric A. Thomas
 *  
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


/*
 *   Code Status:
 *	4/1/99 - Driver appears to be working for Brutus 320x200x8bpp mode.  Other
 *               resolutions are working, but only the 8bpp mode is supported.
 *               Changes need to be made to the palette encode and decode routines
 *               to support 4 and 16 bpp modes.  
 *               Driver is not designed to be a module.  The FrameBuffer is statically
 *               allocated since dynamic allocation of a 300k buffer cannot be guaranteed. 
 *
 *     6/17/99 - FrameBuffer memory is now allocated at run-time when the
 *               driver is initialized.    
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/proc/pgtable.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>


/*
 *  Debug macros 
 */
// #define DEBUG 
#ifdef DEBUG
#  define DPRINTK(fmt, args...)	printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif


/* 
 *  The MAX_x defines are used to specify the maximum ranges for 
 *  parameters that affect the size of the frame buffer memory
 *  region.  Since the frame buffer memory is not dynamically 
 *  alocated, the maximum size that will be used is allocated.
 */
#if defined (CONFIG_SA1100_PENNY)

#define MAX_BITS_PER_PIXEL   		8
#define MAX_SCREEN_SIZE_H   		640
#define MAX_SCREEN_SIZE_V    		480

#elif defined(CONFIG_SA1100_BRUTUS)

#define MAX_BITS_PER_PIXEL   		8
#define MAX_SCREEN_SIZE_H   		320
#define MAX_SCREEN_SIZE_V    		240

#elif defined (CONFIG_SA1100_THINCLIENT)

#define MAX_BITS_PER_PIXEL   		8 
/*#define MAX_BITS_PER_PIXEL   		16*/

#define MAX_SCREEN_SIZE_H   		640
#define MAX_SCREEN_SIZE_V    		480

#elif defined(CONFIG_SA1100_TIFON)

#define MAX_BITS_PER_PIXEL   		4
#define MAX_SCREEN_SIZE_H   		640
#define MAX_SCREEN_SIZE_V    		200

#define REVERSE_VIDEO_4BIT

#elif defined(CONFIG_SA1100_LART)

#define MAX_BITS_PER_PIXEL     		4
#define MAX_SCREEN_SIZE_H      		320
#define MAX_SCREEN_SIZE_V      		240

#endif

/* Default resolutions */
#if defined(CONFIG_SA1100_PENNY) 
#define DEFAULT_XRES	640
#define DEFAULT_YRES	480
#define DEFAULT_BPP	8
#else
#define DEFAULT_XRES	MAX_SCREEN_SIZE_H
#define DEFAULT_YRES	MAX_SCREEN_SIZE_V
#define DEFAULT_BPP	MAX_BITS_PER_PIXEL
#endif

/* Memory size macros for determining required FrameBuffer size */
#define MAX_PALETTE_NUM_ENTRIES         256
#define ADJUSTED_MAX_BITS_PER_PIXEL     (MAX_BITS_PER_PIXEL > 8 ? 16 : MAX_BITS_PER_PIXEL)
#define MAX_PALETTE_MEM_SIZE            (MAX_PALETTE_NUM_ENTRIES * 2)
#define MAX_PIXEL_MEM_SIZE              ((MAX_SCREEN_SIZE_H * MAX_SCREEN_SIZE_V * ADJUSTED_MAX_BITS_PER_PIXEL ) / 8)
#define MAX_FRAMEBUFFER_MEM_SIZE   	(MAX_PIXEL_MEM_SIZE + MAX_PALETTE_MEM_SIZE + 32)
#define ALLOCATED_FB_MEM_SIZE           (PAGE_ALIGN (MAX_FRAMEBUFFER_MEM_SIZE + PAGE_SIZE * 2))

#define SA1100_PALETTE_MEM_SIZE(bpp)	(((bpp)==8?256:16)*2)
#define SA1100_PALETTE_MODE_VAL(bpp)    (((bpp) & 0x018) << 9)

/* Minimum X and Y resolutions */
#define MIN_XRES	64
#define MIN_YRES	64

/* Possible controller_state modes */
#define LCD_MODE_DISABLED		0    // Controller is disabled and Disable Done received
#define LCD_MODE_DISABLE_BEFORE_ENABLE	1    // Re-enable after Disable Done IRQ is received
#define LCD_MODE_ENABLED		2    // Controller is enabled

#define SA1100_NAME	"SA1100"
#define NR_MONTYPES	1

static u_char *VideoMemRegion = NULL;
static u_char *VideoMemRegion_phys = NULL;

/* Local LCD controller parameters */
/* These can be reduced by making better use of fb_var_screeninfo parameters.  */
/* Several duplicates exist in the two structures. */
struct sa1100fb_par {
	u_char         	*p_screen_base;
	u_char         	*v_screen_base;
	u_short        	*p_palette_base;
	u_short        	*v_palette_base;
	unsigned long	 screen_size;
	unsigned int	 palette_size;
        unsigned int   	 xres;
        unsigned int   	 yres;
        unsigned int   	 xres_virtual;
        unsigned int   	 yres_virtual;
        unsigned int   	 bits_per_pixel;
	  signed int	 montype;
	unsigned int	 currcon;
        unsigned int   	 visual;
	unsigned int	 allow_modeset	: 1;
	unsigned int   	 active_lcd     : 1;
	volatile u_char	 controller_state;
};

/* Shadows for LCD controller registers */
struct sa1100fb_lcd_reg {
	Address	dbar1;
	Word	lccr0;
	Word	lccr1;
	Word	lccr2;
	Word	lccr3;
};

/* Fake monspecs to fill in fbinfo structure */
static struct fb_monspecs monspecs __initdata = {
	 30000, 70000, 50, 65, 0 	/* Generic */
};

static struct display global_disp;            /* Initial (default) Display Settings */
static struct fb_info fb_info;
static struct sa1100fb_par current_par;
static struct fb_var_screeninfo __initdata init_var = {};
static struct sa1100fb_lcd_reg lcd_shadow;


static int  sa1100fb_open(struct fb_info *info, int user);
static int  sa1100fb_release(struct fb_info *info, int user);
static int  sa1100fb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info);
static int  sa1100fb_ioctl(struct inode *ino, struct file *file, unsigned int cmd,
	       		  unsigned long arg, int con, struct fb_info *info);
static int  sa1100fb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info);
static int  sa1100fb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info);
static int  sa1100fb_get_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info);
static int  sa1100fb_set_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info);
static int  sa1100fb_pan_display(struct fb_var_screeninfo *var, int con, struct fb_info *info);
 
static int  sa1100fb_switch(int con, struct fb_info *info);
static void sa1100fb_blank(int blank, struct fb_info *info);
static int  sa1100fb_map_video_memory(void);
static int  sa1100fb_activate_var(struct fb_var_screeninfo *var);
static void sa1100fb_enable_lcd_controller(void);
static void sa1100fb_disable_lcd_controller(void);

static struct fb_ops sa1100fb_ops = {
	sa1100fb_open,
	sa1100fb_release,
	sa1100fb_get_fix,		      
	sa1100fb_get_var,		      
	sa1100fb_set_var,		      
	sa1100fb_get_cmap,		      
	sa1100fb_set_cmap,		      
	sa1100fb_pan_display,		      
	sa1100fb_ioctl			      
};



/*
 * sa1100fb_palette_write:
 *    Write palette data to the LCD frame buffer's palette area
 */
static inline void
sa1100fb_palette_write(u_int regno, u_short pal)
{
	current_par.v_palette_base[regno] = (regno ? pal : pal | 
	                                     SA1100_PALETTE_MODE_VAL(current_par.bits_per_pixel));
}


static inline u_short
sa1100fb_palette_encode(u_int regno, u_int red, u_int green, u_int blue, u_int trans)
{
	u_short pal;


	if(current_par.bits_per_pixel == 4){
		/*
		 * RGB -> luminance is defined to be
		 * Y =  0.299 * R + 0.587 * G + 0.114 * B
		 */
		pal = ((19595 * red + 38470 * green + 7471 * blue) >> 28) & 0x00f;
#ifdef REVERSE_VIDEO_4BIT
		pal = 15 - pal;
#endif
	}
	else{
		pal   = ((red   >>  4) & 0xf00);
		pal  |= ((green >>  8) & 0x0f0);
		pal  |= ((blue  >> 12) & 0x00f);
	}

	return pal;
}
	    
static inline u_short
sa1100fb_palette_read(u_int regno)
{
	return (current_par.v_palette_base[regno] & 0x0FFF);
}


static void
sa1100fb_palette_decode(u_int regno, u_int *red, u_int *green, u_int *blue, u_int *trans)
{
	u_short pal;

	pal = sa1100fb_palette_read(regno);

	if( current_par.bits_per_pixel == 4){
#ifdef REVERSE_VIDEO_4BIT
		pal = 15 - pal;
#endif
		pal &= 0x000f;
		pal |= pal << 4;
		pal |= pal << 8;
		*blue = *green = *red = pal;
	}
	else{
		*blue   = (pal & 0x000f) << 12;
		*green  = (pal & 0x00f0) << 8;
		*red    = (pal & 0x0f00) << 4;
	}
        *trans  = 0;
}

static int
sa1100fb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue, u_int *trans, struct fb_info *info)
{
	if (regno >= current_par.palette_size)
		return 1;
	
	sa1100fb_palette_decode(regno, red, green, blue, trans);

	return 0;
}


static int
sa1100fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue, u_int trans, struct fb_info *info)
{
	u_short pal;

	if (regno >= current_par.palette_size)
		return 1;

	pal = sa1100fb_palette_encode(regno, red, green, blue, trans);

	sa1100fb_palette_write(regno, pal);

	return 0;
}

static int
sa1100fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
		 struct fb_info *info)
{
	int err = 0;

	if (con == current_par.currcon)
		err = fb_get_cmap(cmap, kspc, sa1100fb_getcolreg, info);
	else if (fb_display[con].cmap.len)
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(current_par.palette_size),
			     cmap, kspc ? 0 : 2);
	return err;
}

static int
sa1100fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		 struct fb_info *info)
{
	int err = 0;

	if (!fb_display[con].cmap.len)
		err = fb_alloc_cmap(&fb_display[con].cmap,

				    current_par.palette_size, 0);
	if (!err) {
		if (con == current_par.currcon)
			err = fb_set_cmap(cmap, kspc, sa1100fb_setcolreg,
					  info);
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	}
	return err;
}

static void inline
sa1100fb_get_par(struct sa1100fb_par *par)
{
	*par = current_par;
}


/*
 * sa1100fb_encode_var():
 * 	Modify var structure using values in par
 */
static int 
sa1100fb_encode_var(struct fb_var_screeninfo *var,
                    struct sa1100fb_par *par)
{
        // Don't know if really want to var on entry.
        // Look at set_var to see.  If so, may need to add extra params to par     
//	memset(var, 0, sizeof(struct fb_var_screeninfo));
 
	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->xres_virtual;
	var->yres_virtual = par->yres_virtual;

	var->bits_per_pixel = par->bits_per_pixel;

	switch(var->bits_per_pixel) {
	case 2:
	case 4:
	case 8:
		var->red.length	   = 4;
		var->green         = var->red;
		var->blue          = var->red;
		var->transp.length = 0;
		break;
	case 12:          // This case should differ for Active/Passive mode  
	case 16:
        	var->red.length    = 
        	var->blue.length   =
        	var->green.length  = 5;
        	var->transp.length = 0;
        	var->red.offset    = 10;
        	var->green.offset  = 5;
        	var->blue.offset   = 0;
        	var->transp.offset = 0;
		break;
	}
	return 0;
}
 
/*
 *  sa1100fb_decode_var():
 *    Get the video params out of 'var'. If a value doesn't fit, round it up,
 *    if it's too big, return -EINVAL.
 *
 *    Suggestion: Round up in the following order: bits_per_pixel, xres,
 *    yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
 *    bitfields, horizontal timing, vertical timing.
 */
static int
sa1100fb_decode_var(struct fb_var_screeninfo *var, 
                    struct sa1100fb_par *par)
{
	u_long palette_mem_phys;
	u_long palette_mem_size;

	*par = current_par;

	if ((par->xres = var->xres) < MIN_XRES)
		par->xres = MIN_XRES; 
	if ((par->yres = var->yres) < MIN_YRES)
		par->yres = MIN_YRES;
        if (par->xres > MAX_SCREEN_SIZE_H)
        	par->xres = MAX_SCREEN_SIZE_H;
        if (par->yres > MAX_SCREEN_SIZE_V)
                par->yres = MAX_SCREEN_SIZE_V; 
        par->xres_virtual = 
		var->xres_virtual < par->xres ? par->xres : var->xres_virtual;
        par->yres_virtual = 
		var->yres_virtual < par->yres ? par->yres : var->yres_virtual;
        par->bits_per_pixel = var->bits_per_pixel;

	switch (par->bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
        case 4:
		par->visual = FB_VISUAL_PSEUDOCOLOR;
		par->palette_size = 16; 
                break;
#endif
#ifdef FBCON_HAS_CFB8
	case 8:
		par->visual = FB_VISUAL_PSEUDOCOLOR;
		par->palette_size = 256; 
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:  /* RGB 555 */
		par->visual = FB_VISUAL_TRUECOLOR;
		par->palette_size = -1; 
		break;
#endif
	default:
		return -EINVAL;
	}

	palette_mem_size  = SA1100_PALETTE_MEM_SIZE(par->bits_per_pixel);
	palette_mem_phys  = (u_long)VideoMemRegion_phys + PAGE_SIZE - 
	                    palette_mem_size;
	par->p_palette_base  = (u_short *)palette_mem_phys;
        par->v_palette_base  = (u_short *)((u_long)VideoMemRegion + PAGE_SIZE - palette_mem_size);
	par->p_screen_base   = (u_char *)((u_long)VideoMemRegion_phys + PAGE_SIZE); 
	par->v_screen_base   = (u_char *)((u_long)VideoMemRegion      + PAGE_SIZE); 
 
        DPRINTK("p_palette_base = 0x%08lx\n",(u_long)par->p_palette_base);
        DPRINTK("v_palette_base = 0x%08lx\n",(u_long)par->v_palette_base);
        DPRINTK("p_screen_base  = 0x%08lx\n",(u_long)par->p_screen_base);
        DPRINTK("v_screen_base  = 0x%08lx\n",(u_long)par->v_screen_base);
        DPRINTK("VideoMemRegion = 0x%08lx\n",(u_long)VideoMemRegion);
        DPRINTK("VideoMemRegion_phys = 0x%08lx\n",(u_long)VideoMemRegion_phys);
	return 0;
}

static int
sa1100fb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
 	struct sa1100fb_par par;

	if (con == -1) {
                sa1100fb_get_par(&par);
                sa1100fb_encode_var(var, &par);
	} else
		*var = fb_display[con].var;

	return 0;
}

/*
 * sa1100fb_set_var():
 *	Set the user defined part of the display for the specified console
 */
static int
sa1100fb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct display *display;
	int err, chgvar = 0;
        struct sa1100fb_par par;

	if (con >= 0)
		display = &fb_display[con]; /* Display settings for console */
	else
		display = &global_disp;     /* Default display settings */


	DPRINTK("xres = %d, yres = %d\n",var->xres, var->yres);
        // Decode var contents into a par structure, adjusting any
        // out of range values.  
	if ((err = sa1100fb_decode_var(var, &par)))
		return err;
        // Store adjusted par values into var structure
        sa1100fb_encode_var(var, &par);
       
        if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_TEST)
		return 0;
        else if (((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW) && 
                 ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NXTOPEN))
             	return -EINVAL;

	if (con >= 0) {
		if ((display->var.xres != var->xres) ||
		    (display->var.yres != var->yres) ||
		    (display->var.xres_virtual != var->xres_virtual) ||
		    (display->var.yres_virtual != var->yres_virtual) ||
		    (display->var.sync != var->sync)                 ||
                    (display->var.bits_per_pixel != var->bits_per_pixel) ||
		    (memcmp(&display->var.red, &var->red, sizeof(var->red))) ||
		    (memcmp(&display->var.green, &var->green, sizeof(var->green))) ||
		    (memcmp(&display->var.blue, &var->blue, sizeof(var->blue)))) 
			chgvar = 1;
	}

	display->var = *var;
	display->screen_base	= par.v_screen_base;
	display->visual		= par.visual;
	display->type		= FB_TYPE_PACKED_PIXELS;
	display->type_aux	= 0;
	display->ypanstep	= 0;
	display->ywrapstep	= 0;
	display->line_length	= 
	display->next_line      = (var->xres * var->bits_per_pixel) / 8;

	display->can_soft_blank	= 1;
	display->inverse	= 0;

	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
        case 4:
		display->dispsw = &fbcon_cfb4;
		break;
#endif
#ifdef FBCON_HAS_CFB8
	case 8: 
		display->dispsw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		display->dispsw = &fbcon_cfb16;
		break;
#endif
	default:
		display->dispsw = &fbcon_dummy;
		break;
	}

        // If the console has changed and the console has defined
        // a changevar function, call that function.
	if (chgvar && info && info->changevar)
		info->changevar(con);

        // If the current console is selected, update the palette 
	if (con == current_par.currcon) 
        {
		struct fb_cmap *cmap;

		current_par = par;
		if (display->cmap.len)
			cmap = &display->cmap;
		else
			cmap = fb_default_cmap(current_par.palette_size);

		fb_set_cmap(cmap, 1, sa1100fb_setcolreg, info);

               	sa1100fb_activate_var(var);
	}
	return 0;
}

static int
sa1100fb_updatevar(int con, struct fb_info *info)
{
	DPRINTK("entered\n");
	return 0;
}

static int
sa1100fb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	struct display *display;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, SA1100_NAME);

	if (con >= 0)
	{
		DPRINTK("Using console specific display for con=%d\n",con);
		display = &fb_display[con];  /* Display settings for console */
	}
	else
		display = &global_disp;      /* Default display settings */

	fix->smem_start	 = current_par.p_screen_base;
	fix->smem_len	 = current_par.screen_size;
	fix->type	 = display->type;
	fix->type_aux	 = display->type_aux;
	fix->xpanstep	 = 0;
	fix->ypanstep	 = display->ypanstep;
	fix->ywrapstep	 = display->ywrapstep;
	fix->visual	 = display->visual;
	fix->line_length = display->line_length;
	fix->accel	 = FB_ACCEL_NONE;

	return 0;
}


static void
__init sa1100fb_init_fbinfo(void)
{
	strcpy(fb_info.modename, SA1100_NAME);
	strcpy(fb_info.fontname, "Acorn8x8");

	fb_info.node		   = -1;
	fb_info.flags		   = FBINFO_FLAG_DEFAULT;
	fb_info.fbops		   = &sa1100fb_ops;
        fb_info.monspecs           = monspecs;
	fb_info.disp		   = &global_disp;
	fb_info.changevar	   = NULL;
	fb_info.switch_con	   = sa1100fb_switch;
	fb_info.updatevar	   = sa1100fb_updatevar;
	fb_info.blank		   = sa1100fb_blank;

	/*
	 * setup initial parameters
	 */
	memset(&init_var, 0, sizeof(init_var));

	init_var.xres		   = DEFAULT_XRES;		
	init_var.yres		   = DEFAULT_YRES;              
	init_var.xres_virtual      = init_var.xres;
	init_var.yres_virtual      = init_var.yres;
	init_var.xoffset           = 0;
	init_var.yoffset           = 0;
	init_var.bits_per_pixel	   = DEFAULT_BPP;
			
	init_var.transp.length	   = 0;
	init_var.nonstd		   = 0;
	init_var.activate	   = FB_ACTIVATE_NOW;
	init_var.height		   = -1;
	init_var.width		   = -1;
	init_var.vmode		   = FB_VMODE_NONINTERLACED;

#if defined(CONFIG_SA1100_PENNY)
	init_var.red.length	   = 4;				
	init_var.green		   = init_var.red;
	init_var.blue		   = init_var.red;
	init_var.sync              = 0;
#elif defined(CONFIG_SA1100_BRUTUS)
	init_var.red.length	   = 4;				
	init_var.green		   = init_var.red;
	init_var.blue		   = init_var.red;
	init_var.sync              = 0;
#elif defined(CONFIG_SA1100_THINCLIENT)
	init_var.red.length	   = 4;				
	init_var.green		   = init_var.red;
	init_var.blue		   = init_var.red;
	init_var.sync              = 0;
#elif defined(CONFIG_SA1100_TIFON)
	init_var.red.length	   = 4;				
	init_var.green		   = init_var.red;
	init_var.blue		   = init_var.red;
	init_var.grayscale         = 1;
        init_var.pixclock	   = 150000;	
        init_var.left_margin	   = 20;
        init_var.right_margin	   = 255;
        init_var.upper_margin	   = 20;
        init_var.lower_margin	   = 0;
        init_var.hsync_len	   = 2;
        init_var.vsync_len	   = 1;
	init_var.sync              = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT;
        init_var.vmode		   = 0;
#elif defined(CONFIG_SA1100_LART)
	init_var.red.length	   = 4;				
	init_var.green		   = init_var.red;
	init_var.blue		   = init_var.red;
	init_var.grayscale	   = 1;
	init_var.pixclock	   = 150000;	
	init_var.sync		   = 0;
#endif

	current_par.montype	   = -1;
	current_par.currcon	  = -1;
	current_par.allow_modeset =  1;
	current_par.controller_state = LCD_MODE_DISABLED;
}



/*
 * sa1100fb_map_video_memory():
 *      Allocates the DRAM memory for the frame buffer.  This buffer is  
 *	remapped into a non-cached, non-buffered, memory region to  
 *      allow palette and pixel writes to occur without flushing the 
 *      cache.  Once this area is remapped, all virtual memory
 *      access to the video memory should occur at the new region.
 */
static int
__init sa1100fb_map_video_memory(void)
{
	u_int  required_pages;
	u_int  extra_pages;
	u_int  order;
        u_int  i;
        char   *allocated_region;

	if (VideoMemRegion != NULL)
		return -EINVAL;

	/* Find order required to allocate enough memory for framebuffer */
	required_pages = ALLOCATED_FB_MEM_SIZE >> PAGE_SHIFT;
        for (order = 0 ; required_pages >> order ; order++) {;}
        extra_pages = (1 << order) - required_pages;

        if ((allocated_region = 
             (char *)__get_free_pages(GFP_KERNEL | GFP_DMA, order)) == NULL)
           return -ENOMEM;

        VideoMemRegion = (u_char *)allocated_region + (extra_pages << PAGE_SHIFT); 
        VideoMemRegion_phys = (u_char *)__virt_to_phys((u_long)VideoMemRegion);

	/* Free all pages that we don't need but were given to us because */
	/* __get_free_pages() works on powers of 2. */
	for (;extra_pages;extra_pages--)
          free_page((u_int)allocated_region + ((extra_pages-1) << PAGE_SHIFT));

        /* Set reserved flag for fb memory to allow it to be remapped into */
        /* user space by the common fbmem driver using remap_page_range(). */                             
        for(i = MAP_NR(VideoMemRegion);                                  
            i < MAP_NR(VideoMemRegion + ALLOCATED_FB_MEM_SIZE); i++) 
          set_bit(PG_reserved, &mem_map[i].flags);

	/* Remap the fb memory to a non-buffered, non-cached region */
	VideoMemRegion = (u_char *)__ioremap((u_long)VideoMemRegion_phys,
					     ALLOCATED_FB_MEM_SIZE,
					     L_PTE_PRESENT  |
					     L_PTE_YOUNG    |
					     L_PTE_DIRTY    |
					     L_PTE_WRITE);
	return (VideoMemRegion == NULL ? -EINVAL : 0);
}

static const int frequency[16] = {
	59000000,
        73700000,
        88500000,
        103200000,
        118000000,
        132700000,
        147500000,
        162200000,
        176900000,
        191700000,
        206400000, 
	230000000,
	245000000,
	260000000,
	275000000,
	290000000
};


static int get_pcd(unsigned int pixclock)
{
	unsigned int pcd;
	pcd = frequency[PPCR &0xf] / 1000;
	pcd *= pixclock/1000;
	return pcd / 10000000 * 12;
	/* the last multiplication by 1.2 is to handle */
	/* sync problems */
}

/*
 * sa1100fb_activate_var():
 *	Configures LCD Controller based on entries in var parameter.  Settings are      
 *      only written to the controller if changes were made.  
 */
static int
sa1100fb_activate_var(struct fb_var_screeninfo *var)
{						       
	u_long	flags;
	int pcd = get_pcd(var->pixclock);

	if (current_par.p_palette_base == NULL)
		return -EINVAL;

	/* Disable interrupts and save status */
	save_flags_cli(flags);		// disable the interrupts and save flags

	/* Reset the LCD Controller's DMA address if it has changed */
  	lcd_shadow.dbar1 = (Address)current_par.p_palette_base;

#if defined(CONFIG_SA1100_PENNY) 
	DPRINTK("Configuring  SA1100 LCD\n");

	DPRINTK("Configuring xres = %d, yres = %d\n",var->xres, var->yres);

  	lcd_shadow.lccr0 = LCCR0_LEN + LCCR0_Color + LCCR0_Sngl + LCCR0_Act + 
  	                   LCCR0_LtlEnd + LCCR0_LDM + LCCR0_BAM + LCCR0_ERM + 
  	                   LCCR0_DMADel(0); 
  	lcd_shadow.lccr1 = LCCR1_DisWdth(var->xres) + LCCR1_HorSnchWdth (65) +
  	                   LCCR1_EndLnDel (43) + LCCR1_BegLnDel(43) ;
  	lcd_shadow.lccr2 = LCCR2_DisHght (var->yres) + LCCR2_VrtSnchWdth (35) + 
  	                   LCCR2_EndFrmDel (0) + LCCR2_BegFrmDel (0) ;
  	lcd_shadow.lccr3 = LCCR3_PixClkDiv(16) +
  	                   LCCR3_ACBsDiv (2) + LCCR3_ACBsCntOff +
		           ((var->sync & FB_SYNC_HOR_HIGH_ACT) ?LCCR3_HorSnchH:LCCR3_HorSnchL) +
		           ((var->sync & FB_SYNC_VERT_HIGH_ACT)?LCCR3_VrtSnchH:LCCR3_VrtSnchL);
       
#elif defined(CONFIG_SA1100_BRUTUS) 
  	DPRINTK("Configuring  BRUTUS LCD\n");
	lcd_shadow.lccr0 = LCCR0_LEN + LCCR0_Color + LCCR0_Sngl + LCCR0_Pas + 
	                   LCCR0_LtlEnd + LCCR0_LDM + LCCR0_BAM + LCCR0_ERM + 
	                   LCCR0_DMADel(0);
  	lcd_shadow.lccr1 = LCCR1_DisWdth(var->xres) + LCCR1_HorSnchWdth(4) + 
  	                   LCCR1_BegLnDel(41) + LCCR1_EndLnDel(101);
  	lcd_shadow.lccr2 = LCCR2_DisHght(var->yres) + LCCR2_VrtSnchWdth(1) + 
  	                   LCCR2_BegFrmDel(0) + LCCR2_EndFrmDel(0);
  	lcd_shadow.lccr3 = LCCR3_OutEnH + LCCR3_PixFlEdg + LCCR3_VrtSnchH + LCCR3_HorSnchH +
  	                    LCCR3_ACBsCntOff + LCCR3_ACBsDiv(2) + LCCR3_PixClkDiv(44);
#elif defined(CONFIG_SA1100_THINCLIENT) 
	DPRINTK("Configuring ThinClient LCD\n");
	DPRINTK("Configuring xres = %d, yres = %d\n",var->xres, var->yres);

        lcd_shadow.lccr0 = LCCR0_LEN + LCCR0_Color + LCCR0_Sngl + 
                           LCCR0_Act;
  	lcd_shadow.lccr1 = LCCR1_DisWdth(var->xres) +LCCR1_HorSnchWdth(10)+
  	                   LCCR1_EndLnDel (81) + LCCR1_BegLnDel(81) ;
  	lcd_shadow.lccr2 = LCCR2_DisHght (var->yres) +LCCR2_VrtSnchWdth(9) + 
  	                   LCCR2_EndFrmDel (20) + LCCR2_BegFrmDel (20) ;
  	lcd_shadow.lccr3 = LCCR3_PixClkDiv(6) +
  	                   LCCR3_ACBsDiv (2) + LCCR3_ACBsCntOff +
                           LCCR3_HorSnchL + LCCR3_VrtSnchL;
              
#elif defined(CONFIG_SA1100_TIFON) 
	DPRINTK("Configuring TIFON LCD\n");

	lcd_shadow.lccr0 = LCCR0_LEN + LCCR0_Mono + LCCR0_Sngl + LCCR0_Pas +
		           LCCR0_BigEnd + LCCR0_LDM + LCCR0_BAM + LCCR0_ERM + 
		           LCCR0_8PixMono + LCCR0_DMADel(0);
	lcd_shadow.lccr1 = LCCR1_DisWdth( var->xres ) +
		           LCCR1_HorSnchWdth( var->hsync_len) +
		           LCCR1_BegLnDel( var->left_margin) +
		           LCCR1_EndLnDel( var->right_margin);
	lcd_shadow.lccr2 = LCCR2_DisHght( var->yres ) +
		           LCCR2_VrtSnchWdth( var->vsync_len )+
		           LCCR2_BegFrmDel( var->upper_margin ) +
		           LCCR2_EndFrmDel( var->lower_margin );
	lcd_shadow.lccr3 = LCCR3_PixClkDiv( pcd ) +
		           LCCR3_ACBsDiv( 512 ) +
		           LCCR3_ACBsCnt(0) +
			   LCCR3_HorSnchH + LCCR3_VrtSnchH;
	/*
		           ((current_var.sync & FB_SYNC_HOR_HIGH_ACT) ?
		           LCCR3_HorSnchH : LCCR3_HorSnchL) +
		           ((current_var.sync & FB_SYNC_VERT_HIGH_ACT) ?
		           LCCR3_VrtSnchH : LCCR3_VrtSnchL);
	*/
#elif defined(CONFIG_SA1100_LART) 
  	DPRINTK("Configuring LART LCD\n");

	lcd_shadow.lccr0 = LCCR0_LEN + LCCR0_Mono + LCCR0_Sngl + LCCR0_Pas +
		           LCCR0_LtlEnd + LCCR0_LDM + LCCR0_BAM + LCCR0_ERM + 
		           LCCR0_DMADel(0);
	lcd_shadow.lccr1 = LCCR1_DisWdth( var->xres ) +
		           LCCR1_HorSnchWdth( 2 ) +
		           LCCR1_BegLnDel( 4 ) +
		           LCCR1_EndLnDel( 2 );
	lcd_shadow.lccr2 = LCCR2_DisHght( var->yres ) +
		           LCCR2_VrtSnchWdth( 1 )+
		           LCCR2_BegFrmDel( 0 ) +
		           LCCR2_EndFrmDel( 0 );
	lcd_shadow.lccr3 = LCCR3_PixClkDiv( 34 ) +
		           LCCR3_ACBsDiv( 512 ) +
		           LCCR3_ACBsCntOff +
		           LCCR3_HorSnchH +
		           LCCR3_VrtSnchH;
#endif

       /* Restore status of interrupts */
       restore_flags(flags);


	if (( LCCR0 != lcd_shadow.lccr0 ) ||
	    ( LCCR1 != lcd_shadow.lccr1 ) ||
	    ( LCCR2 != lcd_shadow.lccr2 ) ||
            ( LCCR3 != lcd_shadow.lccr3 ) ||
	    ( DBAR1 != lcd_shadow.dbar1 ))
	{
		sa1100fb_enable_lcd_controller();
	}

  	return 0;
}


/*
 *  sa1100fb_inter_handler():
 *	Interrupt handler for LCD controller.  Processes disable done interrupt (LDD)
 *      to reenable controller if controller was disabled to change register values.
 */
static void sa1100fb_inter_handler(int irq, void *dev_id, struct pt_regs *regs)
{
        if (LCSR & LCSR_LDD) {
	        /* Disable Done Flag is set */
		LCCR0 |= LCCR0_LDM;	      /* Mask LCD Disable Done Interrupt */
		current_par.controller_state = LCD_MODE_DISABLED;
		if (current_par.controller_state == LCD_MODE_DISABLE_BEFORE_ENABLE) {
			sa1100fb_enable_lcd_controller();
		}
	}
	LCSR = 0;		      /* Clear LCD Status Register */
}


/*
 *  sa1100fb_disable_lcd_controller():
 *    	Disables LCD controller by and enables LDD interrupt.  The controller_state
 *      is not changed until the LDD interrupt is received to indicate the current
 *      frame has completed.  Platform specific hardware disabling is also included.
 */
static void sa1100fb_disable_lcd_controller(void)
{
	DPRINTK("sa1100fb:  Disabling LCD controller\n");

	/* Exit if already LCD disabled, or LDD IRQ unmasked */
	if ((current_par.controller_state == LCD_MODE_DISABLED) ||
	    (!(LCCR0 & LCCR0_LDM))) {
		DPRINTK("sa1100fb: LCD already disabled\n");
		return;
	}

#if defined(CONFIG_SA1100_PENNY)
  	FpgaLcdCS1 = 0x000;	      /* LCD Backlight to 0%    */
  	FpgaPortI  &= ~LCD_ON;        /* Turn off LCD Backlight */
#elif defined(CONFIG_SA1100_TIFON)
	GPCR = GPIO_GPIO(24);         /* turn off display */
#endif
	
	LCSR = 0;		      /* Clear LCD Status Register */
	LCCR0 &= ~(LCCR0_LDM);	      /* Enable LCD Disable Done Interrupt */
	enable_irq(IRQ_LCD);	      /* Enable LCD IRQ */
	LCCR0 &= ~(LCCR0_LEN);	      /* Disable LCD Controller */

}

/*
 *  sa1100fb_enable_lcd_controller():
 *    	Enables LCD controller.  If the controller is already enabled, it is first disabled.
 *      This forces all changes to the LCD controller registers to be done when the 
 *      controller is disabled.  Platform specific hardware enabling is also included.
 */
static void sa1100fb_enable_lcd_controller(void)
{
	u_long	flags;

	save_flags_cli(flags);		

        /* Disable controller before changing parameters */
	if (current_par.controller_state == LCD_MODE_ENABLED) 	{
		current_par.controller_state = LCD_MODE_DISABLE_BEFORE_ENABLE;
		sa1100fb_disable_lcd_controller();
	} else {
		DPRINTK("sa1100fb: Enabling LCD controller\n");

		/* Make sure the mode bits are present in the first palette entry */
		current_par.v_palette_base[0] &= 0x0FFF; 	           
		current_par.v_palette_base[0] |= SA1100_PALETTE_MODE_VAL(current_par.bits_per_pixel); 	 

		/* disable the interrupts and save flags */
		save_flags_cli(flags);		

		DBAR1 = lcd_shadow.dbar1;
		LCCR3 = lcd_shadow.lccr3;
		LCCR2 = lcd_shadow.lccr2;
		LCCR1 = lcd_shadow.lccr1;
		LCCR0 = lcd_shadow.lccr0;

#if defined(CONFIG_SA1100_PENNY)
		FpgaLcdCS1 = 0x0FF;	       /* LCD Backlight to 100% */
		FpgaPortI  |= LCD_ON;          /* Turn on LCD Backlight */
#elif defined(CONFIG_SA1100_TIFON)
		GPCR = GPIO_GPIO(24);          /* cycle on/off-switch */
		udelay(150);
		GPSR = GPIO_GPIO(24);          /* turn on display */
		udelay(150);
		GPSR = GPIO_GPIO(24);          /* turn on display */
#endif
		current_par.controller_state = LCD_MODE_ENABLED;

       		/* Restore status of interrupts */
       }
       restore_flags(flags);
}



static int
sa1100fb_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
sa1100fb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}


static int
sa1100fb_pan_display(struct fb_var_screeninfo *var, int con,
		    struct fb_info *info)
{
	DPRINTK("entered\n");
	return -EINVAL;
}


/*
 * sa1100fb_blank():
 *	Blank the display by setting all palette values to zero.  Note, the 
 * 	12 and 16 bpp modes don't really use the palette, so this will not
 *      blank the display in all modes.  
 */
static void
sa1100fb_blank(int blank, struct fb_info *info)
{
	int i;

	if (blank) {
		for (i = 0; i < current_par.palette_size; i++)
			sa1100fb_palette_write(i, sa1100fb_palette_encode(i, 0, 0, 0, 0));
		sa1100fb_disable_lcd_controller();
	}
	else {
		sa1100fb_set_cmap(&fb_display[current_par.currcon].cmap, 1, 
		                  current_par.currcon, info); 
		sa1100fb_enable_lcd_controller();
	}
}


/*
 *  sa1100fb_switch():       
 *	Change to the specified console.  Palette and video mode
 *      are changed to the console's stored parameters. 
 */
static int
sa1100fb_switch(int con, struct fb_info *info)
{
	struct fb_cmap *cmap;

	if (current_par.currcon >= 0) {
                // Get the colormap for the selected console 
		cmap = &fb_display[current_par.currcon].cmap;

		if (cmap->len)
			fb_get_cmap(cmap, 1, sa1100fb_getcolreg, info);
	}

	current_par.currcon = con;
	fb_display[con].var.activate = FB_ACTIVATE_NOW;
	sa1100fb_set_var(&fb_display[con].var, con, info);
	return 0;
}


void __init sa1100fb_init(void)
{
        current_par.p_palette_base  = NULL;
        current_par.v_palette_base  = NULL;
        current_par.p_screen_base   = NULL;
        current_par.v_screen_base   = NULL;
        current_par.palette_size    = MAX_PALETTE_NUM_ENTRIES;
        current_par.screen_size     = MAX_PIXEL_MEM_SIZE;

#if defined(CONFIG_SA1100_PENNY)
  	GPDR |= GPIO_GPDR_GFX;     /* GPIO Data Direction register for LCD data bits 8-11     */
  	GAFR |= GPIO_GAFR_GFX;     /* GPIO Alternate Function register for LCD data bits 8-11 */
#elif defined(CONFIG_SA1100_TIFON) 

	GPDR = GPDR | GPIO_GPIO(24);  /* set GPIO24 to output */
#endif

	/* Initialize video memory */
	if (sa1100fb_map_video_memory())
	    return;

	sa1100fb_init_fbinfo();
	if (current_par.montype < 0 || current_par.montype > NR_MONTYPES)
		current_par.montype = 1;

	/* Request the interrupt in the init routine only because */ 
	/* this driver will not be used as a module.              */ 
	if (request_irq(IRQ_LCD, sa1100fb_inter_handler, SA_INTERRUPT, "SA1100 LCD", NULL)!= 0) {
		DPRINTK("sa1100fb: failed in request_irq\n");
	}
	DPRINTK("sa1100fb: request_irq succeeded\n");
	disable_irq(IRQ_LCD);

	if (sa1100fb_set_var(&init_var, -1, &fb_info))
		current_par.allow_modeset = 0;
	sa1100fb_decode_var(&init_var, &current_par);

	register_framebuffer(&fb_info);

	/* This driver cannot be unloaded */
	MOD_INC_USE_COUNT;
}

void __init sa1100fb_setup(char *options)
{
	if (!options || !*options)
		return;

	return; 
}



static int
sa1100fb_ioctl(struct inode *ino, struct file *file, unsigned int cmd,
	      unsigned long arg, int con, struct fb_info *info)
{
	return -ENOIOCTLCMD;
}


