/*
 * CybervisionPPC (TVP4020) low level driver for the frame buffer device
 *                          ^^^^^^^^^
 *                          literally ;)
 *
 * Copyright (c) 1998 Ilario Nardinocchi (nardinoc@CS.UniBO.IT) (v124)
 * --------------------------------------------------------------------------
 * based on linux/drivers/video/skeletonfb.c by Geert Uytterhoeven
 * --------------------------------------------------------------------------
 * TODO h/w parameters detect/modify, 8-bit CLUT, acceleration
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
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/amigahw.h>
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>
#include <asm/setup.h>
#include <asm/io.h>

#define ISDIGIT(a) ((a)>='0' && (a)<='9')

#undef CVPPCFB_MASTER_DEBUG
#ifdef CVPPCFB_MASTER_DEBUG
#define FBEGIN	if (usr_startup.debug>1)\
			printk(__FUNCTION__ " {\n")
#define FEND	if (usr_startup.debug>1)\
			printk("} /* " __FUNCTION__ " */\n")
#define DPRINTK(a,b...)	if (usr_startup.debug)\
			printk("%s: " a, __FUNCTION__ , ## b)
#else
#define FBEGIN
#define FEND
#define DPRINTK(a,b...)
#endif 

static const char cvppcfb_name[16]="CybervisionPPC";

struct cvppcfb_startup {		/* startup options */
	char font[40];
	u32 xres;
	u32 yres;
	u32 bpp;
	unsigned long debug;
	unsigned long YANW;			/* You Are Not Welcome */
	struct fb_monspecs monitor;
};
static struct cvppcfb_startup usr_startup = {
	"\0", 640, 480, 16, 0, 1, { 31, 32, 58, 62, 0 } };

#define CVPPC_BASE	0xe0000000
#define CVPPC_SIZE	0x00800000
static char* video_base;	/* virtual address of board video memory */
static unsigned long video_phys;/* physical address of board video memory */
static u32 video_size;		/* size of board video memory */

struct cvppcfb_par {		/* board parameters (sort of) */
	u32 xres;
	u32 yres;
	u32 vxres;
	u32 vyres;
	u32 vxoff;
	u32 vyoff;
	u32 bpp;
	u32 clock;
	u32 sflags;
	u32 left;
	u32 right;
	u32 top;
	u32 bottom;
	u32 hsynclen;
	u32 vsynclen;
};

struct cvppcfb_info {
	struct fb_info_gen gen;
	struct cvppcfb_par current_par;
	int current_par_valid;
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
#ifdef FBCON_HAS_CFB32
		u32 cmap32[16];
#endif
	} cmap;
};
static struct cvppcfb_info fb_info;

/*
 * declaration of hw switch functions
 */
static void cvppcfb_detect(void);
static int cvppcfb_encode_fix(struct fb_fix_screeninfo* fix,
				const void* par, struct fb_info_gen* info);
static int cvppcfb_decode_var(const struct fb_var_screeninfo* var,
					void* par, struct fb_info_gen* info);
static int cvppcfb_encode_var(struct fb_var_screeninfo* var,
				const void* par, struct fb_info_gen* info);
static void cvppcfb_get_par(void* par, struct fb_info_gen* info);
static void cvppcfb_set_par(const void* par, struct fb_info_gen* info);
static int cvppcfb_getcolreg(unsigned regno,
			unsigned* red, unsigned* green, unsigned* blue,
				unsigned* transp, struct fb_info* info);
static int cvppcfb_setcolreg(unsigned regno,
			unsigned red, unsigned green, unsigned blue,
				unsigned transp, struct fb_info* info);
static void cvppcfb_dispsw(const void* par, struct display* disp,
						struct fb_info_gen* info);

static struct fbgen_hwswitch cvppcfb_hwswitch={
	cvppcfb_detect, cvppcfb_encode_fix, cvppcfb_decode_var,
	cvppcfb_encode_var, cvppcfb_get_par, cvppcfb_set_par,
	cvppcfb_getcolreg, cvppcfb_setcolreg, NULL /* pan_display() */,
	NULL /* blank() */, cvppcfb_dispsw
};

/*
 * declaration of ops switch functions
 */
static int cvppcfb_open(struct fb_info* info, int user);
static int cvppcfb_release(struct fb_info* info, int user);

static struct fb_ops cvppcfb_ops={
	cvppcfb_open, cvppcfb_release, fbgen_get_fix, fbgen_get_var,
	fbgen_set_var, fbgen_get_cmap, fbgen_set_cmap, fbgen_pan_display,
	fbgen_ioctl, NULL /* fb_mmap() */
};

/*
 * the actual definition of the above mentioned functions follows
 */

/*
 * private functions
 */

static void cvppcfb_set_modename(struct cvppcfb_info* info,
						struct cvppcfb_startup* s) {

	strcpy(info->gen.info.modename, cvppcfb_name);
}

static void cvppcfb_decode_opt(struct cvppcfb_startup* s, void* par,
						struct cvppcfb_info* info) {
	struct cvppcfb_par* p=(struct cvppcfb_par* )par;

	memset(p, 0, sizeof(struct cvppcfb_par));
	p->xres=p->vxres=(s->xres+7)&~7;
	p->yres=p->vyres=s->yres;
	p->bpp=(s->bpp+7)&~7;
	if (p->bpp==24)
		p->bpp=32;
	if (p->bpp<32)
		p->clock=6666;
	else
		p->clock=10000;
}

static void cvppcfb_encode_mcap(char* options, struct fb_monspecs* mcap) {
	char* next;
	int i=0;

	while (i<4 && options) {
		if ((next=strchr(options, ';')))
			*(next++)='\0';
		switch (i++) {
			case 0:				/* vmin */
				mcap->vfmin=(__u16 )
					simple_strtoul(options, NULL, 0);
				break;
			case 1:				/* vmax */
				mcap->vfmax=(__u16 )
					simple_strtoul(options, NULL, 0);
				break;
			case 2:				/* hmin */
				mcap->hfmin=(__u32 )
					simple_strtoul(options, NULL, 0);
				break;
			case 3:				/* hmax */
				mcap->hfmax=(__u32 )
					simple_strtoul(options, NULL, 0);
				break;
		}
		options=next;
	}
}

static void cvppcfb_encode_mode(char* options, struct cvppcfb_startup* s) {
	char* next;
	int i=0;

	while (i<3 && options) {
		if ((next=strchr(options, ';')))
			*(next++)='\0';
		switch (i++) {
			case 0:
				s->xres=(u32 )
					simple_strtoul(options, NULL, 0);
				break;
			case 1:
				s->yres=(u32 )
					simple_strtoul(options, NULL, 0);
				break;
			case 2:
				s->bpp=(u32 )
					simple_strtoul(options, NULL, 0);
				break;
		}
		options=next;
	}
}

/*
 * protected functions
 */

static void cvppcfb_detect(void) {

	FBEGIN;
	FEND;
}

static int cvppcfb_encode_fix(struct fb_fix_screeninfo* fix,
			const void* par, struct fb_info_gen* info) {

	FBEGIN;
	strcpy(fix->id, cvppcfb_name);
	fix->smem_start=(char* )video_phys;
	fix->smem_len=(__u32 )video_size;
	fix->type=FB_TYPE_PACKED_PIXELS;
	if (((struct cvppcfb_par* )par)->bpp==8)
		fix->visual=FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual=FB_VISUAL_TRUECOLOR;
	fix->xpanstep=fix->ypanstep=fix->ywrapstep=0;
	fix->line_length=0;			/* computed by fbcon */
	fix->mmio_start=NULL;
	fix->mmio_len=0;
	fix->accel=FB_ACCEL_NONE;
	FEND;
	return 0;
}

static int cvppcfb_decode_var(const struct fb_var_screeninfo* var,
				void* par, struct fb_info_gen* info) {
	struct cvppcfb_par p;

	FBEGIN;
	memset(&p, 0, sizeof(struct cvppcfb_par));
	p.bpp=(var->bits_per_pixel+7)&~7;
	if (p.bpp==24)
		p.bpp=32;
	if (p.bpp>32) {
		DPRINTK("depth too big (%lu)\n", p.bpp);
		return -EINVAL;
	}
	p.xres=(var->xres+7)&~7;
	p.yres=var->yres;
	if (p.xres<320 || p.yres<200 || p.xres>2048 || p.yres>2048) {
		DPRINTK("bad resolution (%lux%lu)\n", p.xres, p.yres);
		return -EINVAL;
	}
	p.vxres=(var->xres_virtual+7)&~7;
	p.vxoff=(var->xoffset+7)&~7;
	p.vyres=var->yres_virtual;
	p.vyoff=var->yoffset;
	if (p.vxres<p.xres+p.vxoff)
		p.vxres=p.xres+p.vxoff;
	if (p.vyres<p.yres+p.vyoff)
		p.vyres=p.yres+p.vyoff;
	if (p.vxres*p.vyres*p.bpp/8>video_size) {
		DPRINTK("no memory for screen (%lux%lux%lu)\n",
						p.vxres, p.vyres, p.bpp);
		return -EINVAL;
	}
	p.sflags=var->sync&(FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT);
	p.clock=var->pixclock;
	if (p.clock<6666) {
		DPRINTK("pixclock too fast (%lu)\n", p.clock);
		return -EINVAL;
	}
	p.left=var->left_margin;
	p.top=var->upper_margin;
	p.right=var->right_margin;
	p.bottom=var->lower_margin;
	p.hsynclen=var->hsync_len;
	p.vsynclen=var->vsync_len;
	*((struct cvppcfb_par* )par)=p;
	FEND;
	return 0;
}

static int cvppcfb_encode_var(struct fb_var_screeninfo* var,
				const void* par, struct fb_info_gen* info) {
	struct cvppcfb_par* p=(struct cvppcfb_par* )par;
	struct fb_var_screeninfo v;

	FBEGIN;
	memset(&v, 0, sizeof(struct fb_var_screeninfo));
	v.xres=p->xres;
	v.yres=p->yres;
	v.xres_virtual=p->vxres;
	v.yres_virtual=p->vyres;
	v.xoffset=p->vxoff;
	v.yoffset=p->vyoff;
	v.bits_per_pixel=p->bpp;
	switch (p->bpp) {
		case 16:
			v.red.offset=11;
			v.red.length=5;
			v.green.offset=5;
			v.green.length=6;
			v.blue.length=5;
			break;
		case 32:
			v.transp.offset=24;
			v.red.offset=16;
			v.green.offset=8;
			v.transp.length=8;
			/* fallback */
		case 8:
			v.red.length=v.green.length=v.blue.length=8;
			break;
	}
	v.activate=FB_ACTIVATE_NOW;
	v.height=v.width=-1;
	v.pixclock=p->clock;
	v.left_margin=p->left;
	v.right_margin=p->right;
	v.upper_margin=p->top;
	v.lower_margin=p->bottom;
	v.hsync_len=p->hsynclen;
	v.vsync_len=p->vsynclen;
	v.sync=p->sflags;
	v.vmode=FB_VMODE_NONINTERLACED;
	*var=v;
	FEND;
	return 0;
}

static void cvppcfb_get_par(void* par, struct fb_info_gen* info) {
	struct cvppcfb_info* i=(struct cvppcfb_info* )info;
	
	FBEGIN;
	if (i->current_par_valid)
		*((struct cvppcfb_par* )par)=i->current_par;
	else
		cvppcfb_decode_opt(&usr_startup, par, i);
	FEND;
}

static void cvppcfb_set_par(const void* par, struct fb_info_gen* info) {
	struct cvppcfb_info* i=(struct cvppcfb_info* )info;

	FBEGIN;
	i->current_par=*((struct cvppcfb_par* )par);
	i->current_par_valid=1;
	FEND;
}

static int cvppcfb_getcolreg(unsigned regno,
			unsigned* red, unsigned* green, unsigned* blue,
				unsigned* transp, struct fb_info* info) {
	struct cvppcfb_info* i=(struct cvppcfb_info* )info;

	if (regno<256) {
		*red=i->palette[regno].red<<8|i->palette[regno].red;
		*green=i->palette[regno].green<<8|i->palette[regno].green;
		*blue=i->palette[regno].blue<<8|i->palette[regno].blue;
		*transp=i->palette[regno].transp<<8|i->palette[regno].transp;
	}
	return regno>255;
}

static int cvppcfb_setcolreg(unsigned regno,
			unsigned red, unsigned green, unsigned blue,
				unsigned transp, struct fb_info* info) {
	struct cvppcfb_info* i=(struct cvppcfb_info* )info;

	if (regno<16) {
		switch (i->current_par.bpp) {
#ifdef FBCON_HAS_CFB8
			case 8:
				DPRINTK("8 bit depth not supported yet.\n");
				return 1;
#endif
#ifdef FBCON_HAS_CFB16
			case 16:
				i->cmap.cmap16[regno]=
					((u32 )red & 0xf800) |
					(((u32 )green & 0xfc00)>>5) |
					(((u32 )blue & 0xf800)>>11);
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
		}
	}
	if (regno<256) {
		i->palette[regno].red=red >> 8;
		i->palette[regno].green=green >> 8;
		i->palette[regno].blue=blue >> 8;
		i->palette[regno].transp=transp >> 8;
	}
	return regno>255;
}

static void cvppcfb_dispsw(const void* par, struct display* disp,
						struct fb_info_gen* info) {
	struct cvppcfb_info* i=(struct cvppcfb_info* )info;
	unsigned long flags;

	FBEGIN;
	save_flags(flags);
	cli();
	switch (((struct cvppcfb_par* )par)->bpp) {
#ifdef FBCON_HAS_CFB8
		case 8:
			disp->dispsw=&fbcon_cfb8;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			disp->dispsw=&fbcon_cfb16;
			disp->dispsw_data=i->cmap.cmap16;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			disp->dispsw=&fbcon_cfb32;
			disp->dispsw_data=i->cmap.cmap32;
			break;
#endif
		default:
			disp->dispsw=&fbcon_dummy;
			break;
	}
	restore_flags(flags);
	FEND;
}

static int cvppcfb_open(struct fb_info* info, int user) {

	MOD_INC_USE_COUNT;
	return 0;
}

static int cvppcfb_release(struct fb_info* info, int user) {

	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * public functions
 */

void cvppcfb_cleanup(struct fb_info* info) {

	unregister_framebuffer(info);
}

__initfunc(void cvppcfb_init(void)) {

	FBEGIN;
#ifdef CVPPCFB_MASTER_DEBUG
	printk("cvppcfb_init():\n");
	printk("    resolution %ldx%ldx%ld\n", usr_startup.xres,
					usr_startup.yres, usr_startup.bpp);
	printk("    debug: %ld, YANW: %ld\n", usr_startup.debug,
						usr_startup.YANW);
	printk("    monitorcap: %ld,%ld,%ld,%ld\n",
			usr_startup.monitor.vfmin, usr_startup.monitor.vfmax,
			usr_startup.monitor.hfmin, usr_startup.monitor.hfmax);
#endif
	if (usr_startup.YANW)			/* cannot probe yet */
		return;
	memset(&fb_info, 0, sizeof(struct cvppcfb_info));
	video_size=CVPPC_SIZE;
	video_phys=CVPPC_BASE;
#ifdef CONFIG_APUS
	video_base=(char* )
		kernel_map(video_phys, video_size, KERNELMAP_NOCACHE_SER, NULL);
#else
	video_base=ioremap(video_phys, video_size);
#endif
	DPRINTK("video_phys=%08lx, video_base=%08lx\n", video_phys, video_base);
	DPRINTK("phys_to_virt(video_phys)=%08lx\n", phys_to_virt(video_phys));
	DPRINTK("virt_to_phys(video_base)=%08lx\n", virt_to_phys(video_base));
	fb_info.disp.scrollmode=SCROLL_YREDRAW;
	fb_info.gen.parsize=sizeof(struct cvppcfb_par);
	fb_info.gen.fbhw=&cvppcfb_hwswitch;
	cvppcfb_set_modename(&fb_info, &usr_startup);
	fb_info.gen.info.flags=FBINFO_FLAG_DEFAULT;
	fb_info.gen.info.fbops=&cvppcfb_ops;
	fb_info.gen.info.monspecs=usr_startup.monitor;
	fb_info.gen.info.disp=&fb_info.disp;
	strcpy(fb_info.gen.info.fontname, usr_startup.font);
	fb_info.gen.info.switch_con=&fbgen_switch;
	fb_info.gen.info.updatevar=&fbgen_update_var;
	fb_info.gen.info.blank=&fbgen_blank;
	fbgen_get_var(&fb_info.disp.var, -1, &fb_info.gen.info);
	if (fbgen_do_set_var(&fb_info.disp.var, 1, &fb_info.gen)<0) {
		printk(	"cvppcfb: bad startup configuration: "
			"unable to register.\n");
		return;
	}
	fbgen_set_disp(-1, &fb_info.gen);
	fbgen_install_cmap(0, &fb_info.gen);
	if (register_framebuffer(&fb_info.gen.info)<0) {
		printk("cvppcfb: unable to register.\n");
		return;
	}
	printk("fb%d: %s frame buffer device, using %ldK of video memory\n",
		GET_FB_IDX(fb_info.gen.info.node), fb_info.gen.info.modename,
					(unsigned long )(video_size>>10));
	MOD_INC_USE_COUNT;
	FEND;
}

__initfunc(void cvppcfb_setup(char* options, int* ints)) {
	char* next;

	usr_startup.YANW=0;
	DPRINTK("options: '%s'\n", options);
	while (options) {
		if ((next=strchr(options, ',')))
			*(next++)='\0';
		if (!strncmp(options, "monitorcap:", 11))
			cvppcfb_encode_mcap(options+11, &usr_startup.monitor);
		else if (!strncmp(options, "debug:", 6)) {
			if (ISDIGIT(options[6]))
				usr_startup.debug=options[6]-'0';
			else
				usr_startup.debug=1;
		}
		else if (!strncmp(options, "mode:", 5))
			cvppcfb_encode_mode(options+5, &usr_startup);
		else if (!strncmp(options, "font:", 5))
			strcpy(usr_startup.font, options+5);
		else
			DPRINTK("unrecognized option '%s'\n", options);
		options=next;
	}
#ifdef CVPPCFB_MASTER_DEBUG
	printk("cvppcfb_setup():\n");
	printk("    resolution %ldx%ldx%ld\n", usr_startup.xres,
					usr_startup.yres, usr_startup.bpp);
	printk("    debug: %ld, YANW: %ld\n", usr_startup.debug,
						usr_startup.YANW);
	printk("    monitorcap: %ld,%ld,%ld,%ld\n",
			usr_startup.monitor.vfmin, usr_startup.monitor.vfmax,
			usr_startup.monitor.hfmin, usr_startup.monitor.hfmax);
#endif
}

/*
 * modularization
 */

#ifdef MODULE
int init_module(void) {

	cvppcfb_init();
}

void cleanup_module(void) {

	cvppcfb_cleanup();
}
#endif /* MODULE */

