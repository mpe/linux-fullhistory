/*
 * Based on retz3fb.c and clgen.c
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
#include <linux/zorro.h>
#include <linux/init.h>
#include <asm/amigahw.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/io.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#include "clgenfb.h"

#define CLGEN_VERSION "1.4 ?"
/* #define DEBUG if(1) */
#define DEBUG if(0)

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

/* board types */
#define BT_NONE     0
#define BT_SD64     1
#define BT_PICCOLO  2
#define BT_PICASSO  3
#define BT_SPECTRUM 4
#define BT_PICASSO4 5

#define MAX_NUM_BOARDS 7

#define TRUE  1
#define FALSE 0 

struct clgenfb_par
{
    struct fb_var_screeninfo var;

    __u32 line_length;  /* in BYTES! */
    __u32 visual;
    __u32 type;

    long freq;
    long nom;
    long den;
    long div;

    long HorizRes;   /* The x resolution in pixel */
    long HorizTotal;
    long HorizDispEnd;
    long HorizBlankStart;
    long HorizBlankEnd;
    long HorizSyncStart;
    long HorizSyncEnd;

    long VertRes;   /* the physical y resolution in scanlines */
    long VertTotal;
    long VertDispEnd;
    long VertSyncStart;
    long VertSyncEnd;
    long VertBlankStart;
    long VertBlankEnd;
};

/* info about board */
struct clgenfb_info
{
    struct fb_info_gen gen;

    int keyRAM;  /* RAM, REG zorro board keys */
    int keyREG;
    unsigned long fbmem;
    volatile unsigned char *regs;
    unsigned long mem;
    unsigned long size;
    int btype;
    int smallboard;
    unsigned char SFR; /* Shadow of special function register */

    unsigned long fbmem_phys;
    unsigned long fbregs_phys;
    
    struct clgenfb_par currentmode;
    union {
#ifdef FBCON_HAS_CFB16
	u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
	u32 cfb24[16];
#endif
#ifdef FBCON_HAS_CFB32
	u32 cfb32[16];
#endif
    } fbcon_cmap;
};

static struct display disp;

static struct clgenfb_info boards[MAX_NUM_BOARDS];  /* the boards */
static struct clgenfb_info *fb_info=NULL;  /* pointer to current board */

/*
 *    Predefined Video Modes
 */

static struct fb_videomode clgenfb_predefined[] __initdata =
{
	{   "Autodetect", /* autodetect mode */
	    { 0 }
	},

	{   "640x480", /* 640x480, 31.25 kHz, 60 Hz, 25 MHz PixClock */
	    { 
		640, 480, 640, 480, 0, 0, 8, 0, 
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0}, 
		0, 0, -1, -1, FB_ACCEL_NONE, 40000, 32, 32, 33, 10, 96, 2,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	    }
	},

	/* 1024x768, 55.8 kHz, 70 Hz, 80 MHz PixClock */
	/* 
	    Modeline from XF86Config:
	    Mode "1024x768" 80  1024 1136 1340 1432  768 770 774 805
	*/
	{
	    "1024x768",
	    { 
		1024, 768, 1024, 768, 0, 0, 8, 0, 
		{0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0}, 
		0, 0, -1, -1, FB_ACCEL_NONE, 12500, 92, 112, 31, 2, 204, 4,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	    }
	}
};

#define NUM_TOTAL_MODES    arraysize(clgenfb_predefined)
static struct fb_var_screeninfo clgenfb_default;

/*
 *    Frame Buffer Name
 */

static char clgenfb_name[16] = "CLgen";

/****************************************************************************/
/**** BEGIN PROTOTYPES ******************************************************/

/*--- Interface used by the world ------------------------------------------*/
void clgenfb_init(void);
void clgenfb_setup(char *options, int *ints);
int clgenfb_open(struct fb_info *info, int user);
int clgenfb_release(struct fb_info *info, int user);
int clgenfb_ioctl(struct inode *inode, struct file *file, 
		  unsigned int cmd, unsigned long arg, int con, 
		  struct fb_info *info);

/* function table of the above functions */
static struct fb_ops clgenfb_ops =
{
    clgenfb_open,
    clgenfb_release,
    fbgen_get_fix,   /* using the generic functions */
    fbgen_get_var,   /* makes things much easier... */
    fbgen_set_var,
    fbgen_get_cmap,
    fbgen_set_cmap,
    fbgen_pan_display,
    clgenfb_ioctl,
    NULL
};

/*--- Hardware Specific Routines -------------------------------------------*/
static void clgen_detect(void);
static int  clgen_encode_fix(struct fb_fix_screeninfo *fix, const void *par,
			     struct fb_info_gen *info);
static int  clgen_decode_var(const struct fb_var_screeninfo *var, void *par,
			     struct fb_info_gen *info);
static int  clgen_encode_var(struct fb_var_screeninfo *var, const void *par,
			     struct fb_info_gen *info);
static void clgen_get_par(void *par, struct fb_info_gen *info);
static void clgen_set_par(const void *par, struct fb_info_gen *info);
static int  clgen_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			   unsigned *blue, unsigned *transp,
			   struct fb_info *info);
static int  clgen_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp,
			    struct fb_info *info);
static int  clgen_pan_display(const struct fb_var_screeninfo *var,
			      struct fb_info_gen *info);
static int  clgen_blank(int blank_mode, struct fb_info_gen *info);

static void clgen_set_dispsw(const void *par, struct display *disp,
			     struct fb_info_gen *info);

/* function table of the above functions */
static struct fbgen_hwswitch clgen_hwswitch = 
{
    clgen_detect,
    clgen_encode_fix,
    clgen_decode_var,
    clgen_encode_var,
    clgen_get_par,
    clgen_set_par,
    clgen_getcolreg, 
    clgen_setcolreg,
    clgen_pan_display,
    clgen_blank,
    clgen_set_dispsw
};

/* Text console acceleration */

#ifdef FBCON_HAS_CFB8
static void fbcon_clgen8_bmove(struct display *p, int sy, int sx, 
				int dy, int dx, int height, int width);
static void fbcon_clgen8_clear(struct vc_data *conp, struct display *p, 
				int sy, int sx, int height, int width);

static struct display_switch fbcon_clgen_8 = {
    fbcon_cfb8_setup,
    fbcon_clgen8_bmove,
    fbcon_clgen8_clear,
    fbcon_cfb8_putc,
    fbcon_cfb8_putcs,
    fbcon_cfb8_revc,
    NULL,
    NULL,
    fbcon_cfb8_clear_margins,
    FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif



/*--- Internal routines ----------------------------------------------------*/
static void init_vgachip(void);
static void switch_monitor(int on);

static void		WGen( int regnum, unsigned char val );
static unsigned char	RGen( int regnum );
static void		WSeq( unsigned char regnum, unsigned char val );
static unsigned char	RSeq( unsigned char regnum );
static void		WCrt( unsigned char regnum, unsigned char val );
static unsigned char	RCrt( unsigned char regnum );
static void		WGfx( unsigned char regnum, unsigned char val );
static unsigned char	RGfx( unsigned char regnum );
static void		WAttr( unsigned char regnum, unsigned char val );
static void		AttrOn( void );
static unsigned char	RAttr( unsigned char regnum );
static void		WHDR( unsigned char val );
static unsigned char	RHDR( void );
static void		WSFR( unsigned char val );
static void		WSFR2( unsigned char val );
static void		WClut( unsigned char regnum, unsigned char red, 
						     unsigned char green, 
						     unsigned char blue );
static void		RClut( unsigned char regnum, unsigned char *red, 
						     unsigned char *green, 
						     unsigned char *blue );
static void		clgen_WaitBLT( void );
static void		clgen_BitBLT (u_short curx, u_short cury,
				      u_short destx, u_short desty,
				      u_short width, u_short height, 
				      u_short line_length);
static void		clgen_RectFill (u_short x, u_short y,
					u_short width, u_short height,
					u_char color, u_short line_length);

static void		bestclock(long freq, long *best, 
				  long *nom, long *den, 
				  long *div, long maxfreq);

/*** END   PROTOTYPES ********************************************************/
/*****************************************************************************/
/*** BEGIN Interface Used by the World ***************************************/

static int opencount = 0;

/*--- Open /dev/fbx ---------------------------------------------------------*/
int clgenfb_open(struct fb_info *info, int user)
{
    MOD_INC_USE_COUNT;
    if (opencount++ == 0) switch_monitor(1);
    return 0;
}

/*--- Close /dev/fbx --------------------------------------------------------*/
int clgenfb_release(struct fb_info *info, int user)
{
    if (--opencount == 0) switch_monitor(0);
    MOD_DEC_USE_COUNT;
    return 0;
}

/*--- handle /dev/fbx ioctl calls ------------------------------------------*/
int clgenfb_ioctl(struct inode *inode, struct file *file,
                  unsigned int cmd, unsigned long arg, int con,
                  struct fb_info *info)
{
    printk(">clgenfb_ioctl()\n");
    /* Nothing exciting here... */
    printk("<clgenfb_ioctl()\n");
    return -EINVAL;
}

/**** END   Interface used by the World *************************************/
/****************************************************************************/
/**** BEGIN Hardware specific Routines **************************************/

static void clgen_detect(void)
{
    printk(">clgen_detect()\n");
    printk("<clgen_detect()\n");
}

static int clgen_encode_fix(struct fb_fix_screeninfo *fix, const void *par,
			    struct fb_info_gen *info)
{
    struct clgenfb_par  *_par  = (struct clgenfb_par*) par;
    struct clgenfb_info *_info = (struct clgenfb_info*)info;
    
    memset(fix, 0, sizeof(struct fb_fix_screeninfo));
    strcpy(fix->id, clgenfb_name);
	
    fix->smem_start	= (char*)_info->fbmem_phys;
    
    /* monochrome: only 1 memory plane */
    /* 8 bit and above: Use whole memory area */
    fix->smem_len       = _par->var.bits_per_pixel == 1 ? _info->size / 4 
							: _info->size;
    fix->type		= _par->type;
    fix->type_aux	= 0;
    fix->visual		= _par->visual;
    fix->xpanstep	= 1;
    fix->ypanstep	= 1;
    fix->ywrapstep	= 0;
    fix->line_length	= _par->line_length;
    fix->mmio_start	= (char *)_info->fbregs_phys;
    fix->mmio_len	= 0x10000;
    fix->accel		= FB_ACCEL_NONE;

    return 0;
}

static int clgen_decode_var(const struct fb_var_screeninfo *var, void *par,
			    struct fb_info_gen *info)
{
    long freq;
    int  xres, hfront, hsync, hback;
    int  yres, vfront, vsync, vback;
    int  nom,den; /* translyting from pixels->bytes */
    int  i;
    static struct
    {
	int xres,yres;
    }
    modes[] = { {1600,1280}, {1280,1024}, {1024,768}, 
		{800,600}, {640,480}, {-1,-1} };

    struct clgenfb_par *_par = (struct clgenfb_par *)par;

    fb_info = (struct clgenfb_info*)info;
    printk("clgen_decode_var()\n");

    printk("Requested: %dx%dx%d\n", var->xres,var->yres,var->bits_per_pixel);
    printk("  virtual: %dx%d\n", var->xres_virtual,var->yres_virtual);
    printk("   offset: (%d,%d)\n", var->xoffset, var->yoffset);
    printk("grayscale: %d\n", var->grayscale);
#if 0
    printk(" activate: 0x%x\n", var->activate);
    printk(" pixclock: %d\n", var->pixclock);
    printk("  htiming: %d;%d  %d\n", var->left_margin,var->right_margin,var->hsync_len);
    printk("  vtiming: %d;%d  %d\n", var->upper_margin,var->lower_margin,var->vsync_len);
    printk("     sync: 0x%x\n", var->sync);
    printk("    vmode: 0x%x\n", var->vmode);
#endif

    _par->var = *var;

    switch (var->bits_per_pixel)
    {
    case 1:  nom = 4; den = 8; break;   /* 8 pixel per byte, only 1/4th of mem usable */
    case 8:  nom = 1; den = 1; break;   /* 1 pixel == 1 byte */
    case 16: nom = 2; den = 1; break;   /* 2 bytes per pixel */
    case 24: nom = 3; den = 1; break;   /* 3 bytes per pixel */
    case 32: nom = 4; den = 1; break;	/* 4 bytes per pixel */
    default:
	printk("clgen: mode %dx%dx%d rejected...color depth not supported.\n",
		var->xres, var->yres, var->bits_per_pixel);
	return -EINVAL;
    }

    if (_par->var.xres*nom/den * _par->var.yres > fb_info->size)
    {
	printk("clgen: mode %dx%dx%d rejected...resolution too high to fit into video memory!\n",
		var->xres, var->yres, var->bits_per_pixel);
	return -EINVAL;
    }
    
    /* use highest possible virtual resolution */
    if (_par->var.xres_virtual == -1 &&
	_par->var.yres_virtual == -1)
    {
	printk("clgen: using maximum available virtual resolution\n");
	for (i=0; modes[i].xres != -1; i++)
	{
	    if (modes[i].xres*nom/den * modes[i].yres < fb_info->size/2)
		break;
	}
	if (modes[i].xres == -1)
	{
	    printk("clgen: could not find a virtual resolution that fits into video memory!!\n");
	    return -EINVAL;
	}
	_par->var.xres_virtual = modes[i].xres;
	_par->var.yres_virtual = modes[i].yres;

	printk("clgen: virtual resolution set to maximum of %dx%d\n",
		_par->var.xres_virtual, _par->var.yres_virtual);
    }
    else if (_par->var.xres_virtual == -1)
    {
    }
    else if (_par->var.yres_virtual == -1)
    {
    }

    if (_par->var.xoffset < 0) _par->var.xoffset = 0;
    if (_par->var.yoffset < 0) _par->var.yoffset = 0;
	
    /* truncate xoffset and yoffset to maximum if too high */
    if (_par->var.xoffset > _par->var.xres_virtual-_par->var.xres)
	_par->var.xoffset = _par->var.xres_virtual-_par->var.xres -1;

    if (_par->var.yoffset > _par->var.yres_virtual-_par->var.yres)
	_par->var.yoffset = _par->var.yres_virtual-_par->var.yres -1;

    switch (var->bits_per_pixel)
    {
    case 1:
	_par->line_length	= _par->var.xres_virtual / 8;
	_par->visual		= FB_VISUAL_MONO10;
	break;

    case 8:
	_par->line_length 	= _par->var.xres_virtual;
	_par->visual		= FB_VISUAL_PSEUDOCOLOR;
	_par->var.red.offset    = 0;
	_par->var.red.length    = 6;
	_par->var.green.offset  = 0;
	_par->var.green.length  = 6;
	_par->var.blue.offset   = 0;
	_par->var.blue.length   = 6;
	break;

    case 16:
	_par->line_length	= _par->var.xres_virtual * 2;
	_par->visual		= FB_VISUAL_DIRECTCOLOR;
	_par->var.red.offset    = 10;
	_par->var.red.length    = 5;
	_par->var.green.offset  = 5;
	_par->var.green.length  = 5;
	_par->var.blue.offset   = 0;
	_par->var.blue.length   = 5;
	break;

    case 24:
	_par->line_length	= _par->var.xres_virtual * 3;
	_par->visual		= FB_VISUAL_DIRECTCOLOR;
	_par->var.red.offset    = 16;
	_par->var.red.length    = 8;
	_par->var.green.offset  = 8;
	_par->var.green.length  = 8;
	_par->var.blue.offset   = 0;
	_par->var.blue.length   = 8;
	break;

    case 32:
	_par->line_length	= _par->var.xres_virtual * 4;
	_par->visual		= FB_VISUAL_DIRECTCOLOR;
	_par->var.red.offset    = 16;
	_par->var.red.length    = 8;
	_par->var.green.offset  = 8;
	_par->var.green.length  = 8;
	_par->var.blue.offset   = 0;
	_par->var.blue.length   = 8;
	break;
    }
    _par->var.red.msb_right = 0;
    _par->var.green.msb_right = 0;
    _par->var.blue.msb_right = 0;
    _par->var.transp.offset = 0;
    _par->var.transp.length = 0;
    _par->var.transp.msb_right = 0;

    _par->type		= FB_TYPE_PACKED_PIXELS;

    /* convert from ps to kHz */
    freq = 1000000000 / var->pixclock;

    DEBUG printk("desired pixclock: %ld kHz\n", freq);	

    /* the SD64/P4 have a higher max. videoclock */
    bestclock(freq, &_par->freq, &_par->nom, &_par->den, &_par->div, 
		fb_info->btype == BT_SD64 || fb_info->btype == BT_PICASSO4
		? 140000 : 90000);

    DEBUG printk("Best possible values for given frequency: best: %ld kHz  nom: %ld  den: %ld  div: %ld\n", 
		 _par->freq, _par->nom, _par->den, _par->div);

    xres   = _par->var.xres;
    hfront = _par->var.right_margin;
    hsync  = _par->var.hsync_len;
    hback  = _par->var.left_margin;

    yres   = _par->var.yres;
    vfront = _par->var.lower_margin;
    vsync  = _par->var.vsync_len;
    vback  = _par->var.upper_margin;
    
    if (_par->var.vmode & FB_VMODE_DOUBLE)
    {
    	yres   *= 2;
    	vfront *= 2;
    	vsync  *= 2;
    	vback  *= 2;
    }
    else if (_par->var.vmode & FB_VMODE_INTERLACED)
    {
    	yres   = ++yres   / 2;
    	vfront = ++vfront / 2;
    	vsync  = ++vsync  / 2;
    	vback  = ++vback  / 2;
    }

    _par->HorizRes        = xres;
    _par->HorizTotal      = (xres + hfront + hsync + hback)/8 - 5;
    _par->HorizDispEnd    = xres/8 - 1;
    _par->HorizBlankStart = xres/8;
    _par->HorizBlankEnd   = _par->HorizTotal+5; /* does not count with "-5" */
    _par->HorizSyncStart  = (xres + hfront)/8 + 1;
    _par->HorizSyncEnd    = (xres + hfront + hsync)/8 + 1;

    _par->VertRes         = yres;
    _par->VertTotal       = yres + vfront + vsync + vback -2;
    _par->VertDispEnd     = yres - 1;
    _par->VertBlankStart  = yres;
    _par->VertBlankEnd    = _par->VertTotal;
    _par->VertSyncStart   = yres + vfront - 1;
    _par->VertSyncEnd     = yres + vfront + vsync - 1;

    if (_par->VertTotal >= 1024)
    {
	printk(KERN_WARNING "clgen: ERROR: VerticalTotal >= 1024; special treatment required! (TODO)\n");
	return -EINVAL;
    }

    return 0;
}


static int clgen_encode_var(struct fb_var_screeninfo *var, const void *par,
			    struct fb_info_gen *info)
{
    *var = ((struct clgenfb_par*)par)->var;
    return 0;
}

/* get current video mode */
static void clgen_get_par(void *par, struct fb_info_gen *info)
{
    struct clgenfb_par *_par  = (struct clgenfb_par*)par;
    struct clgenfb_info*_info = (struct clgenfb_info*)fb_info;

    *_par = _info->currentmode;   
}

/*************************************************************************
	clgen_set_par()

	actually writes the values for a new video mode into the hardware,
**************************************************************************/
static void clgen_set_par(const void *par, struct fb_info_gen *info)
{
    unsigned char tmp;
    int offset = 0;
    struct clgenfb_par *_par = (struct clgenfb_par*)par;

    printk(KERN_INFO">clgen_set_par()\n");
    printk(KERN_INFO"Requested mode: %dx%dx%d\n",
		    _par->var.xres, _par->var.yres, _par->var.bits_per_pixel);
    printk(KERN_INFO"pixclock: %d\n", _par->var.pixclock);
    
    fb_info = (struct clgenfb_info *)info;

    /* unlock register CRT0..CRT7 */
    WCrt(CRT11, 0x20); /* previously: 0x00) */

    /* if DEBUG is set, all parameters get output before writing */
    DEBUG printk("CRT0: %ld\n", _par->HorizTotal); 
    WCrt(CRT0, _par->HorizTotal);

    DEBUG printk("CRT1: %ld\n", _par->HorizDispEnd); 
    WCrt(CRT1, _par->HorizDispEnd);

    DEBUG printk("CRT2: %ld\n", _par->HorizBlankStart); 
    WCrt(CRT2, _par->HorizBlankStart);

    DEBUG printk("CRT3: 128+%ld\n", _par->HorizBlankEnd % 32); /*  + 128: Compatible read */
    WCrt(CRT3, 128 + (_par->HorizBlankEnd % 32));

    DEBUG printk("CRT4: %ld\n", _par->HorizSyncStart);
    WCrt(CRT4, _par->HorizSyncStart);

    tmp = _par->HorizSyncEnd % 32;
    if (_par->HorizBlankEnd & 32)	
    	tmp += 128;
    DEBUG printk("CRT5: %d\n", tmp); 
    WCrt(CRT5, tmp);

    DEBUG printk("CRT6: %ld\n", _par->VertTotal & 0xff); 
    WCrt(CRT6, (_par->VertTotal & 0xff));

    tmp = 16;  /* LineCompare bit #9 */
    if (_par->VertTotal      & 256) tmp |= 1;
    if (_par->VertDispEnd    & 256) tmp |= 2;
    if (_par->VertSyncStart  & 256) tmp |= 4;
    if (_par->VertBlankStart & 256) tmp |= 8;
    if (_par->VertTotal      & 512) tmp |= 32;
    if (_par->VertDispEnd    & 512) tmp |= 64;
    if (_par->VertSyncStart  & 512) tmp |= 128;
    DEBUG printk("CRT7: %d\n", tmp);
    WCrt(CRT7, tmp);

    tmp = 0x40; /* LineCompare bit #8 */
    if (_par->VertBlankStart & 512)		tmp |= 0x20;
    if (_par->var.vmode & FB_VMODE_DOUBLE)	tmp |= 0x80;
    DEBUG printk("CRT9: %d\n", tmp);
    WCrt(CRT9, tmp);

    DEBUG printk("CRT10: %ld\n", _par->VertSyncStart & 0xff);
    WCrt(CRT10, (_par->VertSyncStart & 0xff));

    DEBUG printk("CRT11: 64+32+%ld\n", _par->VertSyncEnd % 16);
    WCrt(CRT11, (_par->VertSyncEnd % 16 + 64 + 32));

    DEBUG printk("CRT12: %ld\n", _par->VertDispEnd & 0xff);
    WCrt(CRT12, (_par->VertDispEnd & 0xff));

    DEBUG printk("CRT15: %ld\n", _par->VertBlankStart & 0xff);
    WCrt(CRT15, (_par->VertBlankStart & 0xff));

    DEBUG printk("CRT16: %ld\n", _par->VertBlankEnd & 0xff);
    WCrt(CRT16, (_par->VertBlankEnd & 0xff));

    DEBUG printk("CRT18: 0xff\n");
    WCrt(CRT18, 0xff);

    tmp = 0;
    if (_par->var.vmode & FB_VMODE_INTERLACED)	tmp |= 1;
    if (_par->HorizBlankEnd & 64)		tmp |= 16;
    if (_par->HorizBlankEnd & 128)		tmp |= 32;
    if (_par->VertBlankEnd  & 256)		tmp |= 64;
    if (_par->VertBlankEnd  & 512)		tmp |= 128;
    
    DEBUG printk("CRT1a: %d\n", tmp);
    WCrt(CRT1A, tmp);

    /* set VCLK0 */
    /* hardware RefClock: 14.31818 MHz */
    /* formula: VClk = (OSC * N) / (D * (1+P)) */
    /* Example: VClk = (14.31818 * 91) / (23 * (1+1)) = 28.325 MHz */

    WSeq(SEQRB, _par->nom);
    tmp = _par->den<<1; 
    if (_par->div != 0) tmp |= 1;

    if (fb_info->btype == BT_SD64)
    	tmp |= 0x80; /* 6 bit denom; ONLY 5434!!! (bugged me 10 days) */
    
    WSeq(SEQR1B, tmp);

    WCrt(CRT17, 0xc3); /* mode control: CRTC enable, ROTATE(?), 16bit address wrap, no compat. */

/* HAEH?	WCrt(CRT11, 0x20);  * previously: 0x00  unlock CRT0..CRT7 */

    /* don't know if it would hurt to also program this if no interlaced */
    /* mode is used, but I feel better this way.. :-) */
    if (_par->var.vmode & FB_VMODE_INTERLACED)
    	WCrt(CRT19, _par->HorizTotal / 2);
    else
    	WCrt(CRT19, 0x00);	/* interlace control */

    WSeq(SEQR3, 0);

    /* adjust horizontal/vertical sync type (low/high) */
    tmp = 0x03; /* enable display memory & CRTC I/O address for color mode */
    if (_par->var.sync & FB_SYNC_HOR_HIGH_ACT)  tmp |= 0x40;
    if (_par->var.sync & FB_SYNC_VERT_HIGH_ACT) tmp |= 0x80;
    WGen(MISC_W, tmp);

    WCrt(CRT8,    0);      /* Screen A Preset Row-Scan register */
    WCrt(CRTA,    0);      /* text cursor on and start line */
    WCrt(CRTB,   31);      /* text cursor end line */

    /* programming for different color depths */
    if (_par->var.bits_per_pixel == 1)
    {
	DEBUG printk(KERN_INFO "clgen: preparing for 1 bit deep display\n");
#if 0
    	/* restore first 2 color registers for mono mode */
    	WClut( 0, 0x00, 0x00, 0x00);  /* background: black */
	WClut( 1, 0x3f, 0x3f, 0x3f);  /* foreground: white */
#endif
	WGfx(GR5,     0);      /* mode register */
	
	/* Extended Sequencer Mode */
	switch(fb_info->btype)
	{
	case BT_SD64:
	    /* setting the SEQRF on SD64 is not necessary (only during init) */
	    DEBUG printk(KERN_INFO "(for SD64)\n");
	    WSeq(SEQR7,  0xf0);
	    WSeq(SEQR1F, 0x1a);     /*  MCLK select */
	    break;

	case BT_PICCOLO:
	    DEBUG printk(KERN_INFO "(for Piccolo)\n");
	    WSeq(SEQR7, 0x80);
/* ### ueberall 0x22? */
	    WSeq(SEQR1F, 0x22);     /* ##vorher 1c MCLK select */
	    WSeq(SEQRF, 0xb0);    /* evtl d0 bei 1 bit? avoid FIFO underruns..? */
	    break;

	case BT_PICASSO:
	    DEBUG printk(KERN_INFO "(for Picasso)\n");
	    WSeq(SEQR7, 0x20);
	    WSeq(SEQR1F, 0x22);     /* ##vorher 22 MCLK select */
	    WSeq(SEQRF, 0xd0);    /* ## vorher d0 avoid FIFO underruns..? */
	    break;

	case BT_SPECTRUM:
	    DEBUG printk(KERN_INFO "(for Spectrum)\n");
	    WSeq(SEQR7, 0x80);
/* ### ueberall 0x22? */
	    WSeq(SEQR1F, 0x22);     /* ##vorher 1c MCLK select */
	    WSeq(SEQRF, 0xb0);    /* evtl d0? avoid FIFO underruns..? */
	    break;

	case BT_PICASSO4:
	    DEBUG printk(KERN_INFO "(for Picasso 4)\n");
	    WSeq(SEQR7, 0x20);
/*	    WSeq(SEQR1F, 0x1c); */
/* SEQRF not being set here...	WSeq(SEQRF, 0xd0); */
	    break;

	default:
	    printk(KERN_WARNING "clgen: unknown Board\n");
	    break;
	}

	WGen(M_3C6,0x01);     /* pixel mask: pass-through for first plane */
	WHDR(0);              /* hidden dac reg: nothing special */
	WSeq(SEQR4,  0x06);     /* memory mode: odd/even, ext. memory */
	WSeq(SEQR2, 0x01);      /* plane mask: only write to first plane */
	offset 			= _par->var.xres_virtual / 16;
    }
    else if (_par->var.bits_per_pixel == 8)
    {
	DEBUG printk(KERN_INFO "clgen: preparing for 8 bit deep display\n");
	switch(fb_info->btype)
	{
	case BT_SD64:
	    WSeq(SEQR7,  0xf1); /* Extended Sequencer Mode: 256c col. mode */
	    WSeq(SEQR1F, 0x1d);     /* MCLK select */
	    break;

	case BT_PICCOLO:
	    WSeq(SEQR7, 0x81);
	    WSeq(SEQR1F, 0x22);     /* ### vorher 1c MCLK select */
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    break;

	case BT_PICASSO:
	    WSeq(SEQR7, 0x21);
	    WSeq(SEQR1F, 0x22);     /* ### vorher 1c MCLK select */
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    break;

	case BT_SPECTRUM:
	    WSeq(SEQR7, 0x81);
	    WSeq(SEQR1F, 0x22);     /* ### vorher 1c MCLK select */
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    break;

	case BT_PICASSO4:
	    WSeq(SEQR7, 0x21);
	    WSeq(SEQRF, 0xb8); /* ### INCOMPLETE!! */
/*	    WSeq(SEQR1F, 0x1c); */
	    break;

	default:
	    printk(KERN_WARNING "clgen: unknown Board\n");
	    break;
	}

	WGfx(GR5,    64);     /* mode register: 256 color mode */
	WGen(M_3C6,0xff);     /* pixel mask: pass-through all planes */
	WHDR(0);              /* hidden dac reg: nothing special */
	WSeq(SEQR4,  0x0a);     /* memory mode: chain4, ext. memory */
	WSeq(SEQR2,  0xff);     /* plane mask: enable writing to all 4 planes */
	offset 			= _par->var.xres_virtual / 8;
    }
    else if (_par->var.bits_per_pixel == 16)
    {
	DEBUG printk(KERN_INFO "clgen: preparing for 16 bit deep display\n");
	switch(fb_info->btype)
	{
	case BT_SD64:
	    WSeq(SEQR7,  0xf7); /* Extended Sequencer Mode: 256c col. mode */
	    WSeq(SEQR1F, 0x1e);     /* MCLK select */
	    break;

	case BT_PICCOLO:
	    WSeq(SEQR7, 0x87);
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    WSeq(SEQR1F, 0x22);     /* MCLK select */
	    break;

	case BT_PICASSO:
	    WSeq(SEQR7, 0x27);
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    WSeq(SEQR1F, 0x22);     /* MCLK select */
	    break;

	case BT_SPECTRUM:
	    WSeq(SEQR7, 0x87);
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    WSeq(SEQR1F, 0x22);     /* MCLK select */
	    break;

	case BT_PICASSO4:
	    WSeq(SEQR7, 0x27);
/*	    WSeq(SEQR1F, 0x1c);  */
	    break;

	default:
	    printk(KERN_WARNING "CLGEN: unknown Board\n");
	    break;
	}

	WGfx(GR5,    64);     /* mode register: 256 color mode */
	WGen(M_3C6,0xff);     /* pixel mask: pass-through all planes */
	WHDR(0xa0);           /* hidden dac reg: nothing special */
	WSeq(SEQR4,  0x0a);     /* memory mode: chain4, ext. memory */
	WSeq(SEQR2,  0xff);     /* plane mask: enable writing to all 4 planes */
	offset 			= _par->var.xres_virtual / 4;
    }
    else if (_par->var.bits_per_pixel == 32)
    {
	DEBUG printk(KERN_INFO "clgen: preparing for 24/32 bit deep display\n");
	switch(fb_info->btype)
	{
	case BT_SD64:
	    WSeq(SEQR7,  0xf9); /* Extended Sequencer Mode: 256c col. mode */
	    WSeq(SEQR1F, 0x1e);     /* MCLK select */
	    break;

	case BT_PICCOLO:
	    WSeq(SEQR7, 0x85);
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    WSeq(SEQR1F, 0x22);     /* MCLK select */
	    break;

	case BT_PICASSO:
	    WSeq(SEQR7, 0x25);
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    WSeq(SEQR1F, 0x22);     /* MCLK select */
	    break;

	case BT_SPECTRUM:
	    WSeq(SEQR7, 0x85);
	    WSeq(SEQRF, 0xb0);    /* Fast Page-Mode writes */
	    WSeq(SEQR1F, 0x22);     /* MCLK select */
	    break;

	case BT_PICASSO4:
	    WSeq(SEQR7, 0x25);
/*	    WSeq(SEQR1F, 0x1c);  */
	    break;

	default:
	    printk(KERN_WARNING "clgen: unknown Board\n");
	    break;
	}

	WGfx(GR5,    64);     /* mode register: 256 color mode */
	WGen(M_3C6,0xff);     /* pixel mask: pass-through all planes */
	WHDR(0xc5);           /* hidden dac reg: 8-8-8 mode (24 or 32) */
	WSeq(SEQR4,  0x0a);     /* memory mode: chain4, ext. memory */
	WSeq(SEQR2,  0xff);     /* plane mask: enable writing to all 4 planes */
	offset			= _par->var.xres_virtual / 4;
    }
    else
	printk(KERN_ERR "clgen: What's this?? requested color depth == %d.\n",
		_par->var.bits_per_pixel);

    WCrt(CRT13, offset & 0xff);
    tmp = 0x22;
    if (offset & 0x100) tmp |= 0x10; /* offset overflow bit */
    	
    WCrt(CRT1B,tmp);    /* screen start addr #16-18, fastpagemode cycles */

    if (fb_info->btype == BT_SD64 || fb_info->btype == BT_PICASSO4)
    	WCrt(CRT1D, 0x00);   /* screen start address bit 19 */

    WCrt(CRTE,    0);      /* text cursor location high */
    WCrt(CRTF,    0);      /* text cursor location low */
    WCrt(CRT14,   0);      /* underline row scanline = at very bottom */

    WAttr(AR10,  1);      /* controller mode */
    WAttr(AR11,  0);      /* overscan (border) color */
    WAttr(AR12, 15);      /* color plane enable */
    WAttr(AR33,  0);      /* pixel panning */
    WAttr(AR14,  0);      /* color select */

    /* [ EGS: SetOffset(); ] */
    /* From SetOffset(): Turn on VideoEnable bit in Attribute controller */
    AttrOn();

    WGfx(GR0,    0);      /* set/reset register */
    WGfx(GR1,    0);      /* set/reset enable */
    WGfx(GR2,    0);      /* color compare */
    WGfx(GR3,    0);      /* data rotate */
    WGfx(GR4,    0);      /* read map select */
    WGfx(GR6,    1);      /* miscellaneous register */
    WGfx(GR7,   15);      /* color don't care */
    WGfx(GR8,  255);      /* bit mask */

    WSeq(SEQR12,  0x0);     /* graphics cursor attributes: nothing special */

    /* finally, turn on everything - turn off "FullBandwidth" bit */
    /* also, set "DotClock%2" bit where requested */
    tmp = 0x01;

/*** FB_VMODE_CLOCK_HALVE in linux/fb.h not defined anymore ?
    if (var->vmode & FB_VMODE_CLOCK_HALVE)
	tmp |= 0x08;
*/

    WSeq(SEQR1, tmp);
    DEBUG printk("SEQR1: %d\n", tmp);

#if 0
    DEBUG printk(KERN_INFO "clgen: clearing display...");
    clgen_RectFill(0, 0, _par->HorizRes, _par->VertRes, 0, _par->line_length);
    clgen_WaitBLT();
    DEBUG printk("done.\n");
#endif

    fb_info->currentmode = *_par;

    printk("virtual offset: (%d,%d)\n", _par->var.xoffset,_par->var.yoffset);
    /* pan to requested offset */
    clgen_pan_display (&fb_info->currentmode.var, (struct fb_info_gen*)fb_info);

    DEBUG printk("<clgen_set_par()\n");
    return;
}

static int clgen_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			   unsigned *blue, unsigned *transp,
			   struct fb_info *info)
{
    unsigned char bred, bgreen, bblue;

    if (regno > 255)
	return (1);

    fb_info = (struct clgenfb_info *)info;

    RClut(regno, &bred, &bgreen, &bblue);

    *red = (bred<<10) | (bred<<4) | (bred>>2);
    *green = (bgreen<<10) | (bgreen<<4) | (bgreen>>2);
    *blue = (bblue<<10) | (bblue<<4) | (bblue>>2);
    *transp = 0;
    return (0);
}

static int clgen_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp, 
			   struct fb_info *info)
{
    if (regno > 255)
	return (1);

    fb_info = (struct clgenfb_info *)info;
    
    /* "transparent" stuff is completely ignored. */
    WClut(regno, red>>10, green>>10, blue>>10);

    return (0);
}

/*************************************************************************
	clgen_pan_display()

	performs display panning - provided hardware permits this
**************************************************************************/
static int clgen_pan_display(const struct fb_var_screeninfo *var,
			     struct fb_info_gen *info)
{
    int xoffset = 0;
    int yoffset = 0;
    unsigned long base;
    unsigned char tmp = 0, tmp2 = 0, xpix;

    fb_info = (struct clgenfb_info*)fb_info;

    /* no range checks for xoffset and yoffset,   */
    /* as fbgen_pan_display has already done this */

    fb_info->currentmode.var.xoffset = var->xoffset;
    fb_info->currentmode.var.yoffset = var->yoffset;

    xoffset = var->xoffset * fb_info->currentmode.var.bits_per_pixel / 8;
    yoffset = var->yoffset;

    base = yoffset * fb_info->currentmode.line_length + xoffset;

    if (fb_info->currentmode.var.bits_per_pixel == 1)
    {
	/* base is already correct */
	xpix = (unsigned char)(var->xoffset % 8);
    }
    else
    {
	base /= 4;
	xpix = (unsigned char)((xoffset % 4) * 2);
    }

    /* lower 8 + 8 bits of screen start address */
    WCrt(CRTD, (unsigned char)(base & 0xff));
    WCrt(CRTC, (unsigned char)(base >> 8));

    /* construct bits 16, 17 and 18 of screen start address */
    if (base & 0x10000) tmp |= 0x01;
    if (base & 0x20000) tmp |= 0x04;
    if (base & 0x40000) tmp |= 0x08;

    tmp2 = (RCrt(CRT1B) & 0xf2) | tmp; /* 0xf2 is %11110010, exclude tmp bits */
    WCrt(CRT1B, tmp2);
    /* construct bit 19 of screen start address (only on SD64) */
    if (fb_info->btype == BT_SD64 ||
	fb_info->btype == BT_PICASSO4)
    {
	tmp2 = 0;
	if (base & 0x80000) tmp2 = 0x80;
	WCrt(CRT1D, tmp2);
    }

    /* write pixel panning value to AR33; this does not quite work in 8bpp */
    /* ### Piccolo..? Will this work? */
    if (fb_info->currentmode.var.bits_per_pixel == 1)
	WAttr(AR33, xpix);

    return(0);
}


static int clgen_blank(int blank_mode, struct fb_info_gen *info)
{
    unsigned char val;
    printk(">clgen_blank(%d)\n",blank_mode);

    fb_info = (struct clgenfb_info *)info;

    val = RSeq(SEQR1);
    if (blank_mode)
	WSeq(SEQR1, val | 0x20); /* set "FullBandwidth" bit */
    else
	WSeq(SEQR1, val & 0xdf); /* clear "FullBandwidth" bit */

    printk("<clgen_blank()\n");
    return 0;
}

/**** END   Hardware specific Routines **************************************/
/****************************************************************************/
/**** BEGIN Internal Routines ***********************************************/

static void init_vgachip(void)
{
    printk(">init_vgachip()\n");

    /* reset board globally */
    switch(fb_info->btype)
    {
    case BT_SD64:     WSFR(0x1f);  udelay(500); WSFR(0x4f); udelay(500); break;
    case BT_PICCOLO:  WSFR(0x01);  udelay(500); WSFR(0x51); udelay(500); break;
    case BT_PICASSO:  WSFR2(0xff); udelay(500);				 break;
    case BT_SPECTRUM: WSFR(0x1f);  udelay(500); WSFR(0x4f); udelay(500); break;
    case BT_PICASSO4: 
	WCrt(CRT51, 0x00); /* disable flickerfixer */
	udelay(100000);
	WGfx(GR2F, 0x00); /* from Klaus' NetBSD driver: */
	WGfx(GR33, 0x00); /* put blitter into 542x compat */
	WGfx(GR31, 0x00); /* mode */
	break;

    default:
	printk(KERN_ERR "clgen: Warning: Unknown board type\n");
	break;
    }

    /* "pre-set" a RAMsize; if the test succeeds, double it */
    if (fb_info->btype == BT_SD64 ||
        fb_info->btype == BT_PICASSO4)
        fb_info->size = 0x400000;
    else
        fb_info->size = 0x200000;

    /* assume it's a "large memory" board (2/4 MB) */
    fb_info->smallboard = FALSE;

    /* the P4 is not fully initialized here; I rely on it having been */
    /* inited under AmigaOS already, which seems to work just fine    */
    /* (Klaus advised to do it this way)                              */

    if (fb_info->btype != BT_PICASSO4)
    {
	WGen(VSSM, 0x10);  /* EGS: 0x16 */
	WGen(POS102, 0x01);
	WGen(VSSM, 0x08);  /* EGS: 0x0e */

	if(fb_info->btype != BT_SD64)
		WGen(VSSM2, 0x01);

	WSeq(SEQR0, 0x03);  /* reset sequencer logic */

	WSeq(SEQR1, 0x21);  /* FullBandwidth (video off) and 8/9 dot clock */
	WGen(MISC_W, 0xc1);  /* polarity (-/-), disable access to display memory, CRTC base address: color */

/*	WGfx(GRA, 0xce);    "magic cookie" - doesn't make any sense to me.. */
	WSeq(SEQR6, 0x12);   /* unlock all extension registers */

	WGfx(GR31, 0x04);  /* reset blitter */

	if (fb_info->btype == BT_SD64)
	{
	    WSeq(SEQRF, 0xb8);  /* 4 MB Ram SD64, disable CRT fifo(!), 64 bit bus */
	}
	else
	{
	    WSeq(SEQR16, 0x0f); /* Perf. Tuning: Fix value..(?) */
	    WSeq(SEQRF, 0xb0); /* 2 MB DRAM, 8level write buffer, 32bit bus */
	}
    }

    WSeq(SEQR2, 0xff);  /* plane mask: nothing */
    WSeq(SEQR3, 0x00);  /* character map select: doesn't even matter in gx mode */
    WSeq(SEQR4, 0x0e);  /* memory mode: chain-4, no odd/even, ext. memory */

    /* controller-internal base address of video memory */
    switch(fb_info->btype)
    {
    case BT_SD64:     WSeq(SEQR7, 0xf0); break;
    case BT_PICCOLO:  WSeq(SEQR7, 0x80); break;
    case BT_SPECTRUM: WSeq(SEQR7, 0x80); break;
    case BT_PICASSO:  WSeq(SEQR7, 0x20); break;
    case BT_PICASSO4: WSeq(SEQR7, 0x20); break;
    }

/*  WSeq(SEQR8, 0x00);*/  /* EEPROM control: shouldn't be necessary to write to this at all.. */

    WSeq(SEQR10, 0x00); /* graphics cursor X position (incomplete; position gives rem. 3 bits */
    WSeq(SEQR11, 0x00); /* graphics cursor Y position (..."... ) */
    WSeq(SEQR12, 0x00); /* graphics cursor attributes */
    WSeq(SEQR13, 0x00); /* graphics cursor pattern address */

    /* writing these on a P4 might give problems..  */
    if (fb_info->btype != BT_PICASSO4)
    {
	WSeq(SEQR17, 0x00); /* configuration readback and ext. color */
	WSeq(SEQR18, 0x02); /* signature generator */
    }

    /* MCLK select etc. */
    switch(fb_info->btype)
    {
    case BT_PICCOLO:
    case BT_PICASSO:
    case BT_SPECTRUM:  WSeq(SEQR1F, 0x22); break;
    case BT_SD64:      WSeq(SEQR1F, 0x20); break;
    case BT_PICASSO4:/*WSeq(SEQR1F, 0x1c); */ break;
    }

    WCrt(CRT8, 0x00);  /* Screen A preset row scan: none */
    WCrt(CRTA, 0x20);  /* Text cursor start: disable text cursor */
    WCrt(CRTB, 0x00);  /* Text cursor end: - */
    WCrt(CRTC, 0x00);  /* Screen start address high: 0 */
    WCrt(CRTD, 0x00);  /* Screen start address low: 0 */
    WCrt(CRTE, 0x00);  /* text cursor location high: 0 */
    WCrt(CRTF, 0x00);  /* text cursor location low: 0 */

    WCrt(CRT14, 0x00); /* Underline Row scanline: - */
    WCrt(CRT17, 0xc3); /* mode control: timing enable, byte mode, no compat modes */
    WCrt(CRT18, 0x00); /* Line Compare: not needed */
    /* ### add 0x40 for text modes with > 30 MHz pixclock */
    WCrt(CRT1B, 0x02); /* ext. display controls: ext.adr. wrap */

    WGfx(GR0, 0x00); /* Set/Reset registes: - */
    WGfx(GR1, 0x00); /* Set/Reset enable: - */
    WGfx(GR2, 0x00); /* Color Compare: - */
    WGfx(GR3, 0x00); /* Data Rotate: - */
    WGfx(GR4, 0x00); /* Read Map Select: - */
    WGfx(GR5, 0x00); /* Mode: conf. for 16/4/2 color mode, no odd/even, read/write mode 0 */
    WGfx(GR6, 0x01); /* Miscellaneous: memory map base address, graphics mode */
    WGfx(GR7, 0x0f); /* Color Don't care: involve all planes */
    WGfx(GR8, 0xff); /* Bit Mask: no mask at all */
    WGfx(GRB, 0x28); /* Graphics controller mode extensions: finer granularity, 8byte data latches */

    WGfx(GRC, 0xff); /* Color Key compare: - */
    WGfx(GRD, 0x00); /* Color Key compare mask: - */
    WGfx(GRE, 0x00); /* Miscellaneous control: - */
/*  WGfx(GR10, 0x00);*/ /* Background color byte 1: - */
/*  WGfx(GR11, 0x00); */

    WAttr(AR0, 0x00); /* Attribute Controller palette registers: "identity mapping" */
    WAttr(AR1, 0x01);
    WAttr(AR2, 0x02);
    WAttr(AR3, 0x03);
    WAttr(AR4, 0x04);
    WAttr(AR5, 0x05);
    WAttr(AR6, 0x06);
    WAttr(AR7, 0x07);
    WAttr(AR8, 0x08);
    WAttr(AR9, 0x09);
    WAttr(ARA, 0x0a);
    WAttr(ARB, 0x0b);
    WAttr(ARC, 0x0c);
    WAttr(ARD, 0x0d);
    WAttr(ARE, 0x0e);
    WAttr(ARF, 0x0f);

    WAttr(AR10, 0x01); /* Attribute Controller mode: graphics mode */
    WAttr(AR11, 0x00); /* Overscan color reg.: reg. 0 */
    WAttr(AR12, 0x0f); /* Color Plane enable: Enable all 4 planes */
/* ### 	WAttr(AR33, 0x00); * Pixel Panning: - */
    WAttr(AR14, 0x00); /* Color Select: - */

    WGen(M_3C6, 0xff); /* Pixel mask: no mask */

    WGen(MISC_W, 0xc3); /* polarity (-/-), enable display mem, CRTC i/o base = color */

    WGfx(GR31, 0x04); /* BLT Start/status: Blitter reset */
    WGfx(GR31, 0x00); /* - " -           : "end-of-reset" */

    /* CLUT setup */
    WClut( 0, 0x00, 0x00, 0x00);  /* background: black */
    WClut( 1, 0x3f, 0x3f, 0x3f);  /* foreground: white */
    WClut( 2, 0x00, 0x20, 0x00);
    WClut( 3, 0x00, 0x20, 0x20);
    WClut( 4, 0x20, 0x00, 0x00);
    WClut( 5, 0x20, 0x00, 0x20);
    WClut( 6, 0x20, 0x10, 0x00);
    WClut( 7, 0x20, 0x20, 0x20);
    WClut( 8, 0x10, 0x10, 0x10);
    WClut( 9, 0x10, 0x10, 0x30);
    WClut(10, 0x10, 0x30, 0x10);
    WClut(11, 0x10, 0x30, 0x30);
    WClut(12, 0x30, 0x10, 0x10);
    WClut(13, 0x30, 0x10, 0x30);
    WClut(14, 0x30, 0x30, 0x10);
    WClut(15, 0x30, 0x30, 0x30);

    /* the rest a grey ramp */
    {
	int i;

	for (i = 16; i < 256; i++)
	    WClut(i, i>>2, i>>2, i>>2);
    }


    /* misc... */
    WHDR(0); /* Hidden DAC register: - */

#if 0
    /* check for 1/2 MB Piccolo/Picasso/Spectrum resp. 2/4 MB SD64 */
    /* DRAM register has already been pre-set for "large", so it is*/
    /* only modified if we find that this is a "small" version */
    {
	unsigned volatile char *ram = fb_info->fbmem;
	int i, flag = 0;

	ram += (fb_info->size >> 1);

	for (i = 0; i < 256; i++)
	    ram[i] = (unsigned char)i;

	for (i = 0; i < 256; i++)
	{
	    if (ram[i] != i)
		flag = 1;
	}

	/* if the DRAM test failed, halve RAM value */
	if (flag)
	{
	    fb_info->size /= 2;
	    fb_info->smallboard = TRUE;
	    switch(fb_info->btype)
	    {
	    case BT_SD64:     WSeq(SEQRF, 0x38); break; /* 2 MB Ram SD64 */
	    case BT_PICASSO4: WSeq(SEQRF, 0x38); break; /* ### like SD64? */
	    case BT_PICCOLO:
	    case BT_PICASSO:
	    case BT_SPECTRUM: WSeq(SEQRF, 0x30); break; /* 1 MB DRAM */
	    default:
		printk(KERN_WARNING "clgen: Uuhh..could not determine RAM size!\n");
	    }
	}

    }
#endif
    printk(KERN_INFO "clgen: This board has %ld bytes of DRAM memory\n", fb_info->size);
    printk("<init_vgachip()\n");
    return;
}

static void switch_monitor(int on)
{
    static int IsOn = 0;    /* XXX not ok for multiple boards */

    if (fb_info->btype == BT_PICASSO4) return; /* nothing to switch */
    if (fb_info->btype == BT_PICASSO)
    {
	if ((on && !IsOn) || (!on && IsOn))
	    WSFR(0xff);
	return;
    }    
    if (on)
        switch(fb_info->btype)
	{
        case BT_SD64:     WSFR(fb_info->SFR | 0x21);	break;
        case BT_PICCOLO:  WSFR(fb_info->SFR | 0x28);	break;
        case BT_SPECTRUM: WSFR(0x6f);			break;
	}
    else
        switch(fb_info->btype)
	{
        case BT_SD64:     WSFR(fb_info->SFR & 0xde);	break;
        case BT_PICCOLO:  WSFR(fb_info->SFR & 0xd7);	break;
        case BT_SPECTRUM: WSFR(0x4f);			break;
	}
}

static void clgen_set_dispsw(const void *par, struct display *disp,
			     struct fb_info_gen *info)
{
    struct clgenfb_par *_par = (struct clgenfb_par*) par;
    struct clgenfb_info *info2 = (struct clgenfb_info *)info;

    printk("clgen_get_dispsw(): ");
    switch (_par->var.bits_per_pixel)
    {
#ifdef FBCON_HAS_MFB
    case 1:
	printk("monochrome\n");
	disp->dispsw = &fbcon_mfb;
	break;
#endif
#ifdef FBCON_HAS_CFB8
    case 8:
	printk("8 bit color depth\n");
	disp->dispsw = &fbcon_clgen_8;
	break;
#endif
#ifdef FBCON_HAS_CFB16
    case 16:
	printk("16 bit color depth\n");
	disp->dispsw = &fbcon_cfb16;
	disp->dispsw_data = info2->fbcon_cmap.cfb16;
	break;
#endif
#ifdef FBCON_HAS_CFB24
    case 24:
	printk("24 bit color depth\n");
	disp->dispsw = &fbcon_cfb24;
	disp->dispsw_data = info2->fbcon_cmap.cfb24;
	break;
#endif
#ifdef FBCON_HAS_CFB32
    case 32:
	printk("32 bit color depth\n");
	disp->dispsw = &fbcon_cfb32;
	disp->dispsw_data = info2->fbcon_cmap.cfb32;
	break;
#endif

    default:
	printk("unsupported color depth\n");
	disp->dispsw = &fbcon_dummy;
	break;
    }
}

static void fbcon_clgen8_bmove(struct display *p, int sy, int sx, 
				int dy, int dx, int height, int width)
{
    sx     *= fontwidth(p);
    sy     *= fontheight(p);
    dx     *= fontwidth(p);
    dy     *= fontheight(p);
    width  *= fontwidth(p);
    height *= fontheight(p);

    fb_info = (struct clgenfb_info*)p->fb_info;

    clgen_BitBLT((unsigned short)sx, (unsigned short)sy,
		 (unsigned short)dx, (unsigned short)dy,
		 (unsigned short)width, (unsigned short)height,
		 fb_info->currentmode.line_length);
    clgen_WaitBLT();
}

static void fbcon_clgen8_clear(struct vc_data *conp, struct display *p, 
				int sy, int sx, int height, int width)
{
    unsigned short col;
    
    fb_info = (struct clgenfb_info*)p->fb_info;

    sx     *= fontwidth(p);
    sy     *= fontheight(p);
    width  *= fontwidth(p);
    height *= fontheight(p);

    col = attr_bgcol_ec(p, conp);
    col &= 0xff;

    clgen_RectFill((unsigned short)sx, (unsigned short)sy,
		    (unsigned short)width,(unsigned short)height,
		     col, fb_info->currentmode.line_length);
    clgen_WaitBLT();
}


/********************************************************************/
/* clgenfb_init() - master initialization function                  */
/********************************************************************/
__initfunc(void clgenfb_init(void))
{
    const struct ConfigDev *cd  = NULL;
    const struct ConfigDev *cd2 = NULL;
    int err;
    int btype;
    int key,key2;
    unsigned long board_addr,board_size;

    printk(">clgenfb_init()\n");
    printk(KERN_INFO "clgen: Driver for Cirrus Logic based graphic boards, v" CLGEN_VERSION "\n");

    btype = -1;

    if ((key = zorro_find(ZORRO_PROD_HELFRICH_SD64_RAM, 0, 0)))
    {
	key2 = zorro_find(ZORRO_PROD_HELFRICH_SD64_REG, 0, 0);
        btype = BT_SD64;
        printk(KERN_INFO "clgen: SD64 board detected; ");
    }
    else if ((key = zorro_find(ZORRO_PROD_HELFRICH_PICCOLO_RAM, 0, 0)))
    {
        key2      = zorro_find(ZORRO_PROD_HELFRICH_PICCOLO_REG, 0, 0);
    	btype = BT_PICCOLO;
    	printk(KERN_INFO "clgen: Piccolo board detected; ");
    }
    else if ((key = zorro_find(ZORRO_PROD_VILLAGE_TRONIC_PICASSO_II_II_PLUS_RAM, 0, 0)))
    {
        key2      = zorro_find(ZORRO_PROD_VILLAGE_TRONIC_PICASSO_II_II_PLUS_REG, 0, 0);
	btype = BT_PICASSO;
    	printk(KERN_INFO "clgen: Picasso II board detected; ");
    }
    else if ((key = zorro_find(ZORRO_PROD_GVP_EGS_28_24_SPECTRUM_RAM, 0, 0)))
    {
        key2      = zorro_find(ZORRO_PROD_GVP_EGS_28_24_SPECTRUM_REG, 0, 0);
        btype = BT_SPECTRUM;
        printk(KERN_INFO "clgen: Spectrum board detected; ");
    }
    else if ((key = zorro_find(ZORRO_PROD_VILLAGE_TRONIC_PICASSO_IV_Z3, 0, 0)))
    {
        btype = BT_PICASSO4;
        printk(KERN_INFO "clgen: Picasso 4 board detected; ");
    }
    else
    {
	printk(KERN_NOTICE "clgen: no supported board found.\n");
	return;
    }
    
    fb_info = &boards[0]; /* FIXME support multiple boards ...*/
    
    fb_info->keyRAM = key;
    fb_info->keyREG = key2;
    fb_info->btype  = btype;
    
    cd = zorro_get_board(key);
    board_addr = (unsigned long)cd->cd_BoardAddr;
    board_size = (unsigned long)cd->cd_BoardSize;
    printk(" RAM (%lu MB) at $%lx, ", board_size/0x100000, board_addr);

    if (btype == BT_PICASSO4)
    {
	printk(" REG at $%lx\n", board_addr + 0x600000);

        /* To be precise, for the P4 this is not the */
        /* begin of the board, but the begin of RAM. */
	/* for P4, map in its address space in 2 chunks (### TEST! ) */
	/* (note the ugly hardcoded 16M number) */
	fb_info->regs = ioremap(board_addr, 16777216);
        DEBUG printk(KERN_INFO "clgen: Virtual address for board set to: $%p\n", fb_info->regs);
	fb_info->regs += 0x600000;
	fb_info->fbregs_phys = board_addr + 0x600000;

	fb_info->fbmem_phys = board_addr + 16777216;
	fb_info->fbmem = ioremap(fb_info->fbmem_phys, 16777216);
	DEBUG printk(KERN_INFO "clgen: (RAM start set to: $%lx)\n", fb_info->fbmem);
    }
    else
    {
        cd2 = zorro_get_board(key2);
        printk(" REG at $%lx\n", (unsigned long)cd2->cd_BoardAddr);

	fb_info->fbmem_phys = board_addr;
        if (board_addr > 0x01000000)
	    fb_info->fbmem = ioremap(board_addr, board_size);
	else
	    fb_info->fbmem = ZTWO_VADDR(board_addr);

        /* set address for REG area of board */
	fb_info->regs = (unsigned char *)ZTWO_VADDR(cd2->cd_BoardAddr);
	fb_info->fbregs_phys = (unsigned long) cd2->cd_BoardAddr;

        DEBUG printk(KERN_INFO "clgen: Virtual address for board set to: $%p\n", fb_info->regs);
	DEBUG printk(KERN_INFO "clgen: (RAM start set to: $%lx)\n", fb_info->fbmem);
    }

    init_vgachip();

    /* set up a few more things, register framebuffer driver etc */
    fb_info->gen.parsize         = sizeof(struct clgenfb_par);
    fb_info->gen.fbhw            = &clgen_hwswitch;
    strcpy (fb_info->gen.info.modename, clgenfb_name);
    fb_info->gen.info.node       = -1;
    fb_info->gen.info.fbops      = &clgenfb_ops;
    fb_info->gen.info.disp       = &disp;
    fb_info->gen.info.changevar  = NULL;
    fb_info->gen.info.switch_con = &fbgen_switch;
    fb_info->gen.info.updatevar  = &fbgen_update_var;
    fb_info->gen.info.blank      = &fbgen_blank;
    fb_info->gen.info.flags	 = FBINFO_FLAG_DEFAULT;
    
    /* mark this board as "autoconfigured" */
    zorro_config_board(key, 0);
    if (btype != BT_PICASSO4)
	zorro_config_board(key2, 0);

    /* now that we know the board has been registered n' stuff, we */
    /* can finally initialize it to a default mode (640x480) */
    clgenfb_default = clgenfb_predefined[1].var;
    clgenfb_default.activate = FB_ACTIVATE_NOW;
    clgenfb_default.yres_virtual = 480*3; /* for fast scrolling (YPAN-Mode) */
    err = fbgen_do_set_var(&clgenfb_default, 1, &fb_info->gen);

    if (err)
	return;

    disp.var = clgenfb_default;
    fbgen_set_disp(-1, &fb_info->gen);
    fbgen_install_cmap(0, &fb_info->gen);

    err = register_framebuffer(&fb_info->gen.info);
    if (err)
    {
	printk(KERN_ERR "clgen: ERROR - could not register fb device; err = %d!\n", err);
	return;
    }

    printk("<clgenfb_init()\n");
    return;
}

    /*
     *  Cleanup
     */

void clgenfb_cleanup(struct clgenfb_info *info)
{
    printk(">clgenfb_cleanup()\n");

    fb_info = info;

    switch_monitor(0);

    zorro_unconfig_board(info->keyRAM, 0);
    if (fb_info->btype != BT_PICASSO4)
	zorro_unconfig_board(info->keyREG, 0);

    unregister_framebuffer(&info->gen.info);
    printk("Framebuffer unregistered\n");
    printk("<clgenfb_cleanup()\n");
}


/* A strtok which returns empty strings, too */
static char *strtoke(char *s,const char *ct)
{
	char *sbegin, *send;
	static char *ssave = NULL;

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

/*****************************************************************/
/* clgenfb_setup() might be used later for parsing possible      */
/* arguments to the video= bootstrap parameter. Right now, there */
/* is nothing I do here.                                         */
/*****************************************************************/
__initfunc(void clgenfb_setup(char *options, int *ints))
{
//    char *this_opt;

//    printk("clgenfb_setup(): options: %s\n", options);	
}


    /*
     *  Modularization
     */

#ifdef MODULE
int init_module(void)
{
    printk("init_module()\n");
    clgenfb_init(0);
    return 0;
}

void cleanup_module(void)
{
    printk("module_cleanup()\n");
    clgenfb_cleanup(fb_info);
}
#endif /* MODULE */



/**********************************************************************/
/* about the following functions - I have used the same names for the */
/* functions as Markus Wild did in his Retina driver for NetBSD as    */
/* they just made sense for this purpose. Apart from that, I wrote    */
/* these functions myself.                                            */
/**********************************************************************/

/*** WGen() - write into one of the external/general registers ***/
void WGen(int regnum, unsigned char val)
{
	unsigned volatile char *reg = fb_info->regs + regnum;

	if(fb_info->btype == BT_PICASSO)
	{
		/* Picasso II specific hack */
/*		if (regnum == M_3C7_W || regnum == M_3C9 || regnum == VSSM2) */
		if (regnum == M_3C7_W || regnum == M_3C9)
			reg += 0xfff;
	}

	*reg = val;
}

/*** RGen() - read out one of the external/general registers ***/
unsigned char RGen(int regnum)
{
	unsigned volatile char *reg = fb_info->regs + regnum;

	if(fb_info->btype == BT_PICASSO)
	{
		/* Picasso II specific hack */
/*		if (regnum == M_3C7_W || regnum == M_3C9 || regnum == VSSM2) */
		if (regnum == M_3C7_W || regnum == M_3C9)
			reg += 0xfff;
	}

	return *reg;
}

/*** WSeq() - write into a register of the sequencer ***/
void WSeq(unsigned char regnum, unsigned char val)
{
	fb_info->regs[SEQRX]   = regnum;
	fb_info->regs[SEQRX+1] = val;
}

/*** RSeq() - read out one of the Sequencer registers ***/
unsigned char RSeq(unsigned char regnum)
{
	fb_info->regs[SEQRX] = regnum;
	return fb_info->regs[SEQRX+1];
}

/*** WCrt() - write into a register of the CRT controller ***/
void WCrt(unsigned char regnum, unsigned char val)
{
	fb_info->regs[CRTX]   = regnum;
	fb_info->regs[CRTX+1] = val;
}

/*** RCrt() - read out one of the CRT controller registers ***/
unsigned char RCrt(unsigned char regnum)
{
	fb_info->regs[CRTX] = regnum;
	return fb_info->regs[CRTX+1];
}

/*** WGfx() - write into a register of the Gfx controller ***/
void WGfx(unsigned char regnum, unsigned char val)
{
	fb_info->regs[GRX]   = regnum;
	fb_info->regs[GRX+1] = val;
}

/*** RGfx() - read out one of the Gfx controller registers ***/
unsigned char RGfx(unsigned char regnum)
{
	fb_info->regs[GRX] = regnum;
	return fb_info->regs[GRX+1];
}

/*** WAttr() - write into a register of the Attribute controller ***/
void WAttr(unsigned char regnum, unsigned char val)
{
	/* if the next access to the attribute controller is a data write access, */
	/* simply write back the information that was already there before, so that */
	/* the next write access after that will be an index write. */
	if (RCrt(CRT24) & 0x80)
		/* can't use WAttr() here - we would go into a recursive loop otherwise */
		fb_info->regs[ARX] = fb_info->regs[ARX+1];

	if (RCrt(CRT24) & 0x80)
		printk(KERN_WARNING "clgen: *** AttrIdx BAD!***\n");

	/* now, first set index and after that the value - both to the same address (!) */
	fb_info->regs[ARX] = regnum;
	fb_info->regs[ARX] = val;
}

/*** AttrOn() - turn on VideoEnable for Attribute controller ***/
void AttrOn()
{
	if (RCrt(CRT24) & 0x80)
		/* if we're just in "write value" mode, write back the */
		/* same value as before to not modify anything */
		fb_info->regs[ARX] = fb_info->regs[ARX+1];

	/* turn on video bit */
/*	fb_info->regs[ARX] = 0x20; */
	fb_info->regs[ARX] = 0x33;

	/* dummy write on Reg0 to be on "write index" mode next time */
	fb_info->regs[ARX] = 0x00;
}

/*** RAttr() - read out a register of the Attribute controller ***/
unsigned char RAttr(unsigned char regnum)
{
	/* (explanation see above in WAttr() ) */
	if (RCrt(CRT24) & 0x80)
		fb_info->regs[ARX] = fb_info->regs[ARX+1];

	fb_info->regs[ARX] = regnum;
	return fb_info->regs[ARX+1];
}


/*** WHDR() - write into the Hidden DAC register ***/
/* as the HDR is the only extension register that requires special treatment 
 * (the other extension registers are accessible just like the "ordinary"
 * registers of their functional group) here is a specialized routine for 
 * accessing the HDR
 */
void WHDR(unsigned char val)
{
	unsigned char dummy;

	if(fb_info->btype == BT_PICASSO)
	{
		/* Klaus' hint for correct access to HDR on some boards */
		/* first write 0 to pixel mask (3c6) */
		WGen(M_3C6, 0x00); udelay(200);
		/* next read dummy from pixel address (3c8) */
		dummy = RGen(M_3C8); udelay(200);
	}

	/* now do the usual stuff to access the HDR */

	dummy = RGen(M_3C6); udelay(200);
	dummy = RGen(M_3C6); udelay(200);
	dummy = RGen(M_3C6); udelay(200);
	dummy = RGen(M_3C6); udelay(200);

	WGen(M_3C6, val); udelay(200);

	if(fb_info->btype == BT_PICASSO)
	{
		/* now first reset HDR access counter */
		dummy = RGen(M_3C8); udelay(200);

		/* and at the end, restore the mask value */
		/* ## is this mask always 0xff? */
		WGen(M_3C6, 0xff); udelay(200);
	}
}

/*** RHDR() - read out the Hidden DAC register ***/
/* I hope this does not break on the GD5428 - cannot test it. */
/* (Is there any board for the Amiga that uses the 5428 ?) */
unsigned char RHDR()
{
	unsigned char dummy;

	dummy = RGen(M_3C6);
	dummy = RGen(M_3C6);
	dummy = RGen(M_3C6);
	dummy = RGen(M_3C6);

	return RGen(M_3C6);
}


/*** WSFR() - write to the "special function register" (SFR) ***/
void WSFR(unsigned char val)
{
	fb_info->SFR          = val;
	fb_info->regs[0x8000] = val;
}

/* The Picasso has a second register for switching the monitor bit */
void WSFR2(unsigned char val)
{
	/* writing an arbitrary value to this one causes the monitor switcher */
	/* to flip to Amiga display */
	fb_info->SFR          = val;
	fb_info->regs[0x9000] = val;
}

/*** WClut - set CLUT entry (range: 0..63) ***/
void WClut(unsigned char regnum, unsigned char red, unsigned char green, unsigned char blue)
{
	unsigned int data = 0x3c9;

	/* address write mode register is not translated.. */
	fb_info->regs[0x3c8] = regnum;

	if(fb_info->btype == BT_PICASSO || fb_info->btype == BT_PICASSO4)
	{
		/* but DAC data register IS, at least for Picasso II */
		if(fb_info->btype == BT_PICASSO)
			data += 0xfff;
		fb_info->regs[data] = red;
		fb_info->regs[data] = green;
		fb_info->regs[data] = blue;
	}
	else
	{
		fb_info->regs[data] = blue;
		fb_info->regs[data] = green;
		fb_info->regs[data] = red;
	}
}

/*** RClut - read CLUT entry (range 0..63) ***/
void RClut(unsigned char regnum, unsigned char *red, unsigned char *green, unsigned char *blue)
{
	unsigned int data = 0x3c9;

	fb_info->regs[0x3c7] = regnum;

	if(fb_info->btype == BT_PICASSO || fb_info->btype == BT_PICASSO4)
	{
		if(fb_info->btype == BT_PICASSO)
			data += 0xfff;
		*red   = fb_info->regs[data];
		*green = fb_info->regs[data];
		*blue  = fb_info->regs[data];
	}
	else
	{
		*blue  = fb_info->regs[data];
		*green = fb_info->regs[data];
		*red   = fb_info->regs[data];
	}
}


/*******************************************************************
	clgen_WaitBLT()

	Wait for the BitBLT engine to complete a possible earlier job
*********************************************************************/

void clgen_WaitBLT()
{
	/* now busy-wait until we're done */
	while (RGfx(GR31) & 0x08)
		;
}

/*******************************************************************
	clgen_BitBLT()

	perform accelerated "scrolling"
********************************************************************/

void clgen_BitBLT (u_short curx, u_short cury, u_short destx, u_short desty,
		u_short width, u_short height, u_short line_length)
{
	u_short nwidth, nheight;
	u_long nsrc, ndest;
	u_char bltmode;

	nwidth = width - 1;
	nheight = height - 1;

	bltmode = 0x00;
	/* if source adr < dest addr, do the Blt backwards */
	if (cury <= desty)
	{
		if (cury == desty)
		{
			/* if src and dest are on the same line, check x */
			if (curx < destx)
				bltmode |= 0x01;
		}
		else
			bltmode |= 0x01;
	}

	if (!bltmode)
	{
		/* standard case: forward blitting */
		nsrc = (cury * line_length) + curx;
		ndest = (desty * line_length) + destx;
	}
	else
	{
		/* this means start addresses are at the end, counting backwards */
		nsrc = cury * line_length + curx + nheight * line_length + nwidth;
		ndest = desty * line_length + destx + nheight * line_length + nwidth;
	}

//	clgen_WaitBLT(); /* ### NOT OK for multiple boards! */

	/*
		run-down of registers to be programmed:
		destination pitch
		source pitch
		BLT width/height
		source start
		destination start
		BLT mode
		BLT ROP
		GR0 / GR1: "fill color"
		start/stop
	*/

	/* pitch: set to line_length */
	WGfx(GR24, line_length & 0xff);	/* dest pitch low */
	WGfx(GR25, (line_length >> 8));	/* dest pitch hi */
	WGfx(GR26, line_length & 0xff);	/* source pitch low */
	WGfx(GR27, (line_length >> 8));	/* source pitch hi */

	/* BLT width: actual number of pixels - 1 */
	WGfx(GR20, nwidth & 0xff);	/* BLT width low */
	WGfx(GR21, (nwidth >> 8));	/* BLT width hi */

	/* BLT height: actual number of lines -1 */
	WGfx(GR22, nheight & 0xff);	/* BLT height low */
	WGfx(GR23, (nheight >> 8));	/* BLT width hi */

	/* BLT destination */
	WGfx(GR28, (u_char)(ndest & 0xff));	/* BLT dest low */
	WGfx(GR29, (u_char)(ndest >> 8));	/* BLT dest mid */
	WGfx(GR2A, (u_char)(ndest >> 16));	/* BLT dest hi */

	/* BLT source */
	WGfx(GR2C, (u_char)(nsrc & 0xff));	/* BLT src low */
	WGfx(GR2D, (u_char)(nsrc >> 8));	/* BLT src mid */
	WGfx(GR2E, (u_char)(nsrc >> 16));	/* BLT src hi */

	/* BLT mode */
	WGfx(GR30, bltmode);	/* BLT mode */

	/* BLT ROP: SrcCopy */
	WGfx(GR32, 0x0d);	/* BLT ROP */

	/* and finally: GO! */
	WGfx(GR31, 0x02);	/* BLT Start/status */
}

/*******************************************************************
	clgen_RectFill()

	perform accelerated rectangle fill
********************************************************************/

void clgen_RectFill (u_short x, u_short y, u_short width, u_short height,
                     u_char color, u_short line_length)
{
	u_short nwidth, nheight;
	u_long ndest;

	nwidth = width - 1;
	nheight = height - 1;

	ndest = (y * line_length) + x;

//	clgen_WaitBLT(); /* ### NOT OK for multiple boards! */

	/* pitch: set to line_length */
	WGfx(GR24, line_length & 0xff);	/* dest pitch low */
	WGfx(GR25, (line_length >> 8));	/* dest pitch hi */
	WGfx(GR26, line_length & 0xff);	/* source pitch low */
	WGfx(GR27, (line_length >> 8));	/* source pitch hi */

	/* BLT width: actual number of pixels - 1 */
	WGfx(GR20, nwidth & 0xff);	/* BLT width low */
	WGfx(GR21, (nwidth >> 8));	/* BLT width hi */

	/* BLT height: actual number of lines -1 */
	WGfx(GR22, nheight & 0xff);	/* BLT height low */
	WGfx(GR23, (nheight >> 8));	/* BLT width hi */

	/* BLT destination */
	WGfx(GR28, (u_char)(ndest & 0xff));	/* BLT dest low */
	WGfx(GR29, (u_char)(ndest >> 8));	/* BLT dest mid */
	WGfx(GR2A, (u_char)(ndest >> 16));	/* BLT dest hi */

	/* BLT source: set to 0 (is a dummy here anyway) */
	WGfx(GR2C, 0x00);	/* BLT src low */
	WGfx(GR2D, 0x00);	/* BLT src mid */
	WGfx(GR2E, 0x00);	/* BLT src hi */

	/* This is a ColorExpand Blt, using the */
	/* same color for foreground and background */
	WGfx(GR0, color);	/* foreground color */
	WGfx(GR1, color);	/* background color */

	/* BLT mode: color expand, Enable 8x8 copy (faster?) */
	WGfx(GR30, 0xc0);	/* BLT mode */

	/* BLT ROP: SrcCopy */
	WGfx(GR32, 0x0d);	/* BLT ROP */

	/* and finally: GO! */
	WGfx(GR31, 0x02);	/* BLT Start/status */
}

/**************************************************************************
 * bestclock() - determine closest possible clock lower(?) than the
 * desired pixel clock
 **************************************************************************/
#define abs(x) ((x)<0 ? -(x) : (x))
static void bestclock(long freq, long *best, long *nom,
		      long *den, long *div,  long maxfreq)
{
    long n, h, d, f;

    *nom = 0;
    *den = 0;
    *div = 0;

    if (freq < 8000)
	freq = 8000;

    if (freq > maxfreq)
	freq = maxfreq;

    *best = 0;
    f = freq * 10;

    for(n = 32; n < 128; n++)
    {
        d = (143181 * n) / f;
	if ( (d >= 7) && (d <= 63) )
        {
            if (d > 31)
            	d = (d / 2) * 2;
            h = (14318 * n) / d;
            if ( abs(h - freq) < abs(*best - freq) )
            {
                *best = h;
		*nom = n;
		if (d < 32)
		{
                    *den = d;
                    *div = 0;
		}
		else
		{
                    *den = d / 2;
		    *div = 1;
		}
            }
        }
        d = ( (143181 * n)+f-1) / f;
        if ( (d >= 7) && (d <= 63) )
	{
	    if (d > 31)
    		d = (d / 2) * 2;
	    h = (14318 * n) / d;
	    if ( abs(h - freq) < abs(*best - freq) )
	    {
    	        *best = h;
    	        *nom = n;
            	if (d < 32)
		{
    	            *den = d;
        	    *div = 0;
	        }
    	        else
    	        {
    	            *den = d / 2;
	            *div = 1;
	        }
	    }
	}
    }
}

