/*
 *  platinumfb.c -- frame buffer device for the PowerMac 'platinum' display
 *
 *  Created 12 July 1998 by Dan Jacobowitz <dan@debian.org>
 *  Copyright (C) 1998 Dan Jacobowitz
 *
 *  Frame buffer structure from:
 *    drivers/video/chipsfb.c -- frame buffer device for
 *    Chips & Technologies 65550 chip.
 *
 *    Copyright (C) 1998 Paul Mackerras
 *
 *    This file is derived from the Powermac "chips" driver:
 *    Copyright (C) 1997 Fabio Riccardi.
 *    And from the frame buffer device for Open Firmware-initialized devices:
 *    Copyright (C) 1997 Geert Uytterhoeven.
 *
 *  Hardware information from:
 *    platinum.c: Console support for PowerMac "platinum" display adaptor.
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
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
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pgtable.h>
#include <asm/adb.h>
#include <asm/cuda.h>

#include "fbcon.h"
#include "fbcon-cfb8.h"
#include "fbcon-cfb16.h"
#include "fbcon-cfb32.h"

#include "macmodes.h"
#include "platinumfb.h"

static int currcon = 0;
static int switching = 0;

struct fb_par_platinum {
	int	vmode, cmode;
	int	xres, yres;
	int	vxres, vyres;
	int	xoffset, yoffset;
};

struct fb_info_platinum {
	struct fb_info			info;
	struct fb_fix_screeninfo	fix;
	struct fb_var_screeninfo	var;
	struct display			disp;
	struct fb_par_platinum		par;
	struct {
		__u8 red, green, blue;
	}			palette[256];
	
	volatile struct cmap_regs	*cmap_regs;
	unsigned long		cmap_regs_phys;
	
	volatile struct platinum_regs	*platinum_regs;
	unsigned long		platinum_regs_phys;
	
	__u8			*frame_buffer;
	__u8			*base_frame_buffer;
	unsigned long		frame_buffer_phys;
	
	int			sense;
	unsigned long		total_vram;
};

/*
 * Exported functions
 */
void platinum_init(void);
void platinum_of_init(struct device_node *dp);

static int platinum_open(struct fb_info *info, int user);
static int platinum_release(struct fb_info *info, int user);
static int platinum_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int platinum_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int platinum_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int platinum_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info);
static int platinum_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int platinum_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int platinum_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info);

static int read_platinum_sense(struct fb_info_platinum *p);
static inline int platinum_vram_reqd(int video_mode, int color_mode);
static void set_platinum_clock(struct fb_info_platinum *p, unsigned char *params);
static void platinum_set_hardware(struct fb_info_platinum *p);
static void platinum_par_to_all(struct fb_info_platinum *p, int init);
static inline void platinum_par_to_var(struct fb_par_platinum *par, struct fb_var_screeninfo *var);
static int platinum_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_platinum *par, const struct fb_info *fb_info);

static void platinum_init_info(struct fb_info *info, struct fb_info_platinum *p);
static void platinum_par_to_display(struct fb_par_platinum *par,
  struct display *disp, struct fb_fix_screeninfo *fix, struct fb_info_platinum *p);
static void platinum_init_display(struct display *disp);
static void platinum_par_to_fix(struct fb_par_platinum *par, struct fb_fix_screeninfo *fix,
	struct fb_info_platinum *p);
static void platinum_init_fix(struct fb_fix_screeninfo *fix, struct fb_info_platinum *p);

static struct fb_ops platinumfb_ops = {
	platinum_open,
	platinum_release,
	platinum_get_fix,
	platinum_get_var,
	platinum_set_var,
	platinum_get_cmap,
	platinum_set_cmap,
	platinum_pan_display,
	platinum_ioctl
};

static int platinum_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info);
static int platinum_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);

#define FUNCID { printk(KERN_INFO "entering %s\n", __FUNCTION__); }

__openfirmware


static int platinum_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int platinum_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static int platinum_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	struct fb_info_platinum *cp = (struct fb_info_platinum *) info;

	*fix = cp->fix;
	return 0;
}

static int platinum_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_platinum *cp = (struct fb_info_platinum *) info;

	*var = cp->var;
	return 0;
}

/* Sets everything according to var */
static int platinum_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_platinum *p = (struct fb_info_platinum *) info;
	struct display *disp;
	struct fb_par_platinum par;
	int depthchange, err;

//    FUNCID;
	disp = (con >= 0) ? &fb_display[con] : &p->disp;
	if((err = platinum_var_to_par(var, &par, info))) {
		printk (KERN_ERR "Error in platinum_set_var, calling platinum_var_to_par: %d.\n", err);
		return err;
	}
	
	if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW) {
		printk("Not activating, in platinum_set_var.\n");
		platinum_par_to_var(&par, var);
		return 0;
	}
/* I know, we want to use fb_display[con], but grab certain info from p->var instead. */
#define DIRTY(x) (p->var.x != var->x)
	depthchange = DIRTY(bits_per_pixel);
	if(!DIRTY(xres) && !DIRTY(yres) && !DIRTY(xres_virtual) &&
	   !DIRTY(yres_virtual) && !DIRTY(bits_per_pixel)) {
	   	platinum_par_to_var(&par, var);
		p->var = disp->var = *var;
		return 0;
	}
	printk("Original bpp is %d, new bpp %d.\n", p->var.bits_per_pixel, var->bits_per_pixel);
	/* OK, we're getting here at the right times... */
	p->par = par;
	platinum_par_to_var(&par, var);
	p->var = *var;
	platinum_par_to_fix(&par, &p->fix, p);
	platinum_par_to_display(&par, disp, &p->fix, p);
	p->disp = *disp;
	
	if(info->changevar && !switching)	/* Don't want to do this if just switching consoles. */
		(*info->changevar)(con);
	if(con == currcon)
		platinum_set_hardware(p);
	if(depthchange)
		if((err = fb_alloc_cmap(&disp->cmap, 0, 0)))
			return err;
	if(depthchange || switching)
		do_install_cmap(con, info);
	return 0;
}

static int platinum_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
    /*
     *  Pan (or wrap, depending on the `vmode' field) the display using the
     *  `xoffset' and `yoffset' fields of the `var' structure.
     *  If the values don't fit, return -EINVAL.
     */

//	FUNCID;
	if (var->xoffset != 0 || var->yoffset != 0)
		return -EINVAL;
	return 0;
}

static int platinum_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
//    FUNCID;
	if (con == currcon)		/* current console? */
		return fb_get_cmap(cmap, &fb_display[con].var, kspc,
				   platinum_getcolreg, info);
	if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc? 0: 2);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
	}
	return 0;
}

static int platinum_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	struct display *disp = &fb_display[con];
	int err;

//    FUNCID;
	if (disp->cmap.len == 0) {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		err = fb_alloc_cmap(&disp->cmap, size, 0);
		if (err)
			return err;
	}

	if (con == currcon)
		return fb_set_cmap(cmap, &disp->var, kspc, platinum_setcolreg,
				   info);
	fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
	return 0;
}

static int platinum_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
//	FUNCID;
	return -EINVAL;
}

static int platinum_switch(int con, struct fb_info *info)
{
//    FUNCID;
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap,
			    &fb_display[currcon].var, 1, platinum_getcolreg,
			    info);
	currcon = con;
#if 0
	platinum_var_to_par(&fb_display[currcon].var, &par, info);
	platinum_set_par(&par, info); /*STOPPEDHERE - did i define that? */
	do_install_cmap(con, info);
#else
	/* I see no reason not to do this.  Minus info->changevar(). */
	/* DOH.  This makes platinum_set_var compare, you guessed it, */
	/* fb_display[con].var (first param), and fb_display[con].var! */
	/* Perhaps I just fixed that... */
	switching = 1;
	platinum_set_var(&fb_display[con].var, con, info);
	switching = 0;
#endif
	return 0;
}

static int platinum_updatevar(int con, struct fb_info *info)
{
	return 0;
}

static void platinum_blank(int blank_mode, struct fb_info *info)
{
/*
 *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
 *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
 *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
 *  to e.g. a video mode which doesn't support it. Implements VESA suspend
 *  and powerdown modes on hardware that supports disabling hsync/vsync:
 *    blank_mode == 2: suspend vsync
 *    blank_mode == 3: suspend hsync
 *    blank_mode == 4: powerdown
 */
/* [danj] I think there's something fishy about those constants... */
/*
	struct fb_info_platinum *p = (struct fb_info_platinum *) info;
	int	ctrl;

	ctrl = ld_le32(&p->platinum_regs->ctrl.r) | 0x33;
	if (blank_mode)
		--blank_mode;
	if (blank_mode & VESA_VSYNC_SUSPEND)
		ctrl &= ~3;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x30;
	out_le32(&p->platinum_regs->ctrl.r, ctrl);
*/
/* TODO: Figure out how the heck to powerdown this thing! */
//FUNCID;
    return;
}

static int platinum_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info)
{
	struct fb_info_platinum *p = (struct fb_info_platinum *) info;

//    FUNCID;
	if (regno > 255 || regno < 0)
		return 1;
	*red = p->palette[regno].red;
	*green = p->palette[regno].green;
	*blue = p->palette[regno].blue;
	return 0;
}

static int platinum_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info)
{
	struct fb_info_platinum *p = (struct fb_info_platinum *) info;

//    FUNCID;
	if (regno > 255 || regno < 0)
		return 1;
	p->palette[regno].red = red;
	p->palette[regno].green = green;
	p->palette[regno].blue = blue;

	out_8(&p->cmap_regs->addr, regno);	/* tell clut what addr to fill	*/
	out_8(&p->cmap_regs->lut, red);		/* send one color channel at	*/
	out_8(&p->cmap_regs->lut, green);	/* a time...			*/
	out_8(&p->cmap_regs->lut, blue);

	if(regno < 16) {
#if 0
#ifdef FBCON_HAS_CFB16
		fbcon_cfb16_cmap[regno] = (red << 10) | (green << 5) | blue;
#endif
#ifdef FBCON_HAS_CFB32
		fbcon_cfb32_cmap[regno] = (red << 16) | (green << 8) | blue;
		/* I think. */
#endif
#else
#ifdef FBCON_HAS_CFB16
		fbcon_cfb16_cmap[regno] = (regno << 10) | (regno << 5) | regno;
#endif
#ifdef FBCON_HAS_CFB32
		fbcon_cfb32_cmap[regno] = (regno << 24) | (regno << 16) | (regno << 8) | regno;
		/* I think. */
#endif
#endif
	}
	return 0;
}

static void do_install_cmap(int con, struct fb_info *info)
{
//    FUNCID;
	if (con != currcon)
		return;
	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
			    platinum_setcolreg, info);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_set_cmap(fb_default_cmap(size), &fb_display[con].var, 1,
			    platinum_setcolreg, info);
	}
}

#ifdef CONFIG_FB_COMPAT_XPMAC
extern struct vc_mode display_info;
extern struct fb_info *console_fb_info;
#if 0
extern int (*console_setmode_ptr)(struct vc_mode *, int);
extern int (*console_set_cmap_ptr)(struct fb_cmap *, int, int,
				   struct fb_info *);
int console_setmode(struct vc_mode *, int);
#endif
#endif /* CONFIG_FB_COMPAT_XPMAC */

static inline int platinum_vram_reqd(int video_mode, int color_mode)
{
	return vmode_attrs[video_mode - 1].vres
		* platinum_reg_init[video_mode-1]->pitch[color_mode];
}

#define STORE_D2(a, d) { \
	out_8(&p->cmap_regs->addr, (a+32)); \
	out_8(&p->cmap_regs->d2, (d)); \
}

static void set_platinum_clock(struct fb_info_platinum *p, unsigned char *clock_params)
{
//    FUNCID;
	STORE_D2(6, 0xc6);
	out_8(&p->cmap_regs->addr,3+32);
	if (in_8(&p->cmap_regs->d2) == 2) {
		STORE_D2(7, clock_params[0]);
		STORE_D2(8, clock_params[1]);
		STORE_D2(3, 3);
	} else {
		STORE_D2(4, clock_params[0]);
		STORE_D2(5, clock_params[1]);
		STORE_D2(3, 2);
	}

	__delay(5000);
	STORE_D2(9, 0xa6);
}


__initfunc(static void init_platinum(struct fb_info_platinum *p))
{
	struct fb_par_platinum *par = &p->par;

//    FUNCID;
	p->sense = read_platinum_sense(p);
	printk("Monitor sense value = 0x%x, ", p->sense);
	/* Try to pick a video mode out of NVRAM if we have one. */
	par->vmode = nvram_read_byte(NV_VMODE);
	if(par->vmode <= 0 || par->vmode > VMODE_MAX || !platinum_reg_init[par->vmode - 1])
		par->vmode = VMODE_CHOOSE;
	if(par->vmode == VMODE_CHOOSE)
		par->vmode = mac_map_monitor_sense(p->sense);
	if(!platinum_reg_init[par->vmode - 1])
		par->vmode = VMODE_640_480_67;

	par->cmode = nvram_read_byte(NV_CMODE);
	if(par->cmode < CMODE_8 || par->cmode > CMODE_32)
		par->cmode = CMODE_8;
	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	while(par->cmode > CMODE_8 && platinum_vram_reqd(par->vmode, par->cmode) > p->total_vram)
		par->cmode--;
	
	printk("using video mode %d and color mode %d.\n", par->vmode, par->cmode);
	
	par->vxres = par->xres = vmode_attrs[par->vmode - 1].hres;
	par->vyres = par->yres = vmode_attrs[par->vmode - 1].vres;
	par->xoffset = par->yoffset = 0;
	
	platinum_par_to_all(p, 1);
	
	if (register_framebuffer(&p->info) < 0) {
		kfree(p);
		return;
	}
	platinum_set_hardware(p);
	
	printk("fb%d: platinum display adapter\n", GET_FB_IDX(p->info.node));	
}

/* Now how about actually saying, Make it so! */
/* Some things in here probably don't need to be done each time. */
static void platinum_set_hardware(struct fb_info_platinum *p)
{
	struct platinum_regvals	*init;
	int			i, dtype, clkmode;
	int			vmode, cmode;
	
//    FUNCID;
	vmode = p->par.vmode;
	cmode = p->par.cmode;

	init = platinum_reg_init[vmode - 1];

	/* Initialize display timing registers */
	out_be32(&p->platinum_regs->reg[24].r, 7);	/* turn display off */

	for (i = 0; i < 26; ++i)
		out_be32(&p->platinum_regs->reg[i+32].r, init->regs[i]);
	out_be32(&p->platinum_regs->reg[26+32].r, (p->total_vram == 0x100000 ?
						   init->offset[cmode] + 4 - cmode :
						   init->offset[cmode]));
	out_be32(&p->platinum_regs->reg[16].r, (unsigned) p->frame_buffer_phys + init->fb_offset);
	out_be32(&p->platinum_regs->reg[18].r, init->pitch[cmode]);
	out_be32(&p->platinum_regs->reg[19].r, (p->total_vram == 0x100000 ?
						init->mode[cmode+1] :
						init->mode[cmode]));
	out_be32(&p->platinum_regs->reg[20].r, (p->total_vram == 0x100000 ? 0x11 : 0x1011));
	out_be32(&p->platinum_regs->reg[21].r, 0x100);
	out_be32(&p->platinum_regs->reg[22].r, 1);
	out_be32(&p->platinum_regs->reg[23].r, 1);
	out_be32(&p->platinum_regs->reg[26].r, 0xc00);
	out_be32(&p->platinum_regs->reg[27].r, 0x235);
	/* out_be32(&p->platinum_regs->reg[27].r, 0x2aa); */

	STORE_D2(0, (p->total_vram == 0x100000 ?
		     init->dacula_ctrl[cmode] & 0xf :
		     init->dacula_ctrl[cmode]));
	STORE_D2(1, 4);
	STORE_D2(2, 0);
	/*
	 * Try to determine whether we have an old or a new DACula.
	 */
	out_8(&p->cmap_regs->addr, 0x40);
	dtype = in_8(&p->cmap_regs->d2);
	switch (dtype) {
	case 0x3c:
		clkmode = 1;
		break;
	case 0x84:
		clkmode = 0;
		break;
	default:
		clkmode = 0;
		printk("Unknown DACula type: %x\n", dtype);
	}

	set_platinum_clock(p, init->clock_params[clkmode]);

	out_be32(&p->platinum_regs->reg[24].r, 0);	/* turn display on */

#ifdef CONFIG_FB_COMPAT_XPMAC
	/* And let the world know the truth. */
	if (!console_fb_info || console_fb_info == &p->info) {
		display_info.height = p->var.yres;
		display_info.width = p->var.xres;
		display_info.depth = ( (cmode == CMODE_32) ? 32 :
				      ((cmode == CMODE_16) ? 16 : 8));
		display_info.pitch = p->fix.line_length;
		display_info.mode = vmode;
		strncpy(display_info.name, "platinum",
			sizeof(display_info.name));
		display_info.fb_address = p->frame_buffer_phys
					+ init->fb_offset
					+ 0x10;
		display_info.cmap_adr_address = p->cmap_regs_phys;
		display_info.cmap_data_address = p->cmap_regs_phys + 0x30;
		display_info.disp_reg_address = p->platinum_regs_phys;
		console_fb_info = &p->info;
	}
#endif /* CONFIG_FB_COMPAT_XPMAC */
}

__initfunc(void platinum_init(void))
{
#ifndef CONFIG_FB_OF
	struct device_node *dp;

	dp = find_devices("platinum");
	if (dp != 0)
		platinum_of_init(dp);
#endif /* CONFIG_FB_OF */
}

__initfunc(void platinum_of_init(struct device_node *dp))
{
	struct fb_info_platinum	*p;
	unsigned long		addr, size;
	int			i, bank0, bank1, bank2, bank3;
//FUNCID;	
	if(dp->n_addrs != 2)
		panic("expecting 2 address for platinum (got %d)", dp->n_addrs);
	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == 0)
		return;

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		if (size >= 0x400000) {
			/* frame buffer - map only 4MB */
			p->frame_buffer_phys = addr;
			p->frame_buffer = __ioremap(addr, 0x400000, _PAGE_WRITETHRU);
			p->base_frame_buffer = p->frame_buffer;
		} else {
			/* registers */
			p->platinum_regs_phys = addr;
			p->platinum_regs = ioremap(addr, size);
		}
	}
	p->cmap_regs_phys = 0xf301b000;	/* XXX not in prom? */
	p->cmap_regs = ioremap(p->cmap_regs_phys, 0x1000);

	/* Grok total video ram */
	out_be32(&p->platinum_regs->reg[16].r, (unsigned)p->frame_buffer_phys);
	out_be32(&p->platinum_regs->reg[20].r, 0x1011);	/* select max vram */
	out_be32(&p->platinum_regs->reg[24].r, 0);	/* switch in vram */
	eieio();
	p->frame_buffer[0x100000] = 0x34;
	asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0x100000]) : "memory");
	p->frame_buffer[0x200000] = 0x56;
	asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0x200000]) : "memory");
	p->frame_buffer[0x300000] = 0x78;
	asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0x300000]) : "memory");
	bank0 = 1; /* builtin 1MB vram, always there */
	bank1 = p->frame_buffer[0x100000] == 0x34;
	bank2 = p->frame_buffer[0x200000] == 0x56;
	bank3 = p->frame_buffer[0x300000] == 0x78;
	p->total_vram = (bank0 + bank1 + bank2 + bank3) * 0x100000;
	printk("Total VRAM = %dMB\n", p->total_vram / 1024 / 1024);

//	p->frame_buffer = p->base_frame_buffer
//			+ platinum_reg_init[p->par.vmode-1]->fb_offset;

#ifdef CONFIG_FB_COMPAT_XPMAC
#if 0
	console_set_cmap_ptr = platinum_set_cmap;
	console_setmode_ptr = platinum_console_setmode;
#endif
#endif /* CONFIG_FB_COMPAT_XPMAC */

	init_platinum(p);
}

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int read_platinum_sense(struct fb_info_platinum *p)
{
	int sense;

	out_be32(&p->platinum_regs->reg[23].r, 7);	/* turn off drivers */
	__delay(2000);
	sense = (~in_be32(&p->platinum_regs->reg[23].r) & 7) << 8;

	/* drive each sense line low in turn and collect the other 2 */
	out_be32(&p->platinum_regs->reg[23].r, 3);	/* drive A low */
	__delay(2000);
	sense |= (~in_be32(&p->platinum_regs->reg[23].r) & 3) << 4;
	out_be32(&p->platinum_regs->reg[23].r, 5);	/* drive B low */
	__delay(2000);
	sense |= (~in_be32(&p->platinum_regs->reg[23].r) & 4) << 1;
	sense |= (~in_be32(&p->platinum_regs->reg[23].r) & 1) << 2;
	out_be32(&p->platinum_regs->reg[23].r, 6);	/* drive C low */
	__delay(2000);
	sense |= (~in_be32(&p->platinum_regs->reg[23].r) & 6) >> 1;

	out_be32(&p->platinum_regs->reg[23].r, 7);	/* turn off drivers */

	return sense;
}

#if 0
/* This routine takes a user-supplied var, and picks the best vmode/cmode from it. */
static int platinum_var_to_par(struct fb_var_screeninfo *var, 
	struct fb_par_platinum *par, const struct fb_info *fb_info)
{
	int xres = var->xres;
	int yres = var->yres;
	int bpp = var->bits_per_pixel;
	
	struct platinum_regvals *init;
	struct fb_info_platinum *p = (struct fb_info_platinum *) fb_info;

//    FUNCID;
    /*
     *  Get the video params out of 'var'. If a value doesn't fit, round it up,
     *  if it's too big, return -EINVAL.
     *
     *  Suggestion: Round up in the following order: bits_per_pixel, xres,
     *  yres, xres_virtual, yres_virtual, xoffset, yoffset, grayscale,
     *  bitfields, horizontal timing, vertical timing.
     */
	/* swiped by jonh from atyfb.c */
	if (xres <= 512 && yres <= 384)
		par->vmode = VMODE_512_384_60;		 /* 512x384, 60Hz */
	else if (xres <= 640 && yres <= 480)
		par->vmode = VMODE_640_480_67;		/* 640x480, 67Hz */
	else if (xres <= 640 && yres <= 870)
		par->vmode = VMODE_640_870_75P;		/* 640x870, 75Hz (portrait) */
	else if (xres <= 768 && yres <= 576)
		par->vmode = VMODE_768_576_50I;		/* 768x576, 50Hz (PAL full frame) */
	else if (xres <= 800 && yres <= 600)
		par->vmode = VMODE_800_600_75;		/* 800x600, 75Hz */
	else if (xres <= 832 && yres <= 624)
		par->vmode = VMODE_832_624_75;		/* 832x624, 75Hz */
	else if (xres <= 1024 && yres <= 768)
		par->vmode = VMODE_1024_768_75;		/* 1024x768, 75Hz */
	else if (xres <= 1152 && yres <= 870)
		par->vmode = VMODE_1152_870_75;		/* 1152x870, 75Hz */
	else if (xres <= 1280 && yres <= 960)
		par->vmode = VMODE_1280_960_75;		/* 1280x960, 75Hz */
	else if (xres <= 1280 && yres <= 1024)
		par->vmode = VMODE_1280_1024_75;	/* 1280x1024, 75Hz */
	else {
		printk(KERN_ERR "Bad  resolution in platinum_var_to_par()!\n");
		return -EINVAL;
	}
	xres = vmode_attrs[par->vmode - 1].hres;
	yres = vmode_attrs[par->vmode - 1].vres;

/*
	if (var->xres_virtual <= xres)
		par->vxres = xres;
	else
		par->vxres = (var->xres_virtual+7) & ~7;
	if (var->yres_virtual <= yres)
		par->vyres = yres;
	else
		par->vyres = var->yres_virtual;

	par->xoffset = (var->xoffset+7) & ~7;
	par->yoffset = var->yoffset;
	if (par->xoffset+xres > par->vxres || par->yoffset+yres > par->vyres)
		return -EINVAL;
*/

	/* I'm too chicken to think about virtual	*/
	/* resolutions just yet. Bok bok.			*/
	
	/* And I'm too chicken to even figure out what they are.  Awk awk. [danj] */
	if (var->xres_virtual > xres || var->yres_virtual > yres
		|| var->xoffset != 0 || var->yoffset != 0) {
		printk(KERN_ERR "Bad  virtual resolution in platinum_var_to_par()!\n");
		return -EINVAL;
	}

	par->xres = xres;
	par->yres = yres;
	par->vxres = xres;
	par->vyres = yres;
	par->xoffset = 0;
	par->yoffset = 0;

	if (bpp <= 8)
		par->cmode = CMODE_8;
	else if (bpp <= 16)
		par->cmode = CMODE_16;
	else if (bpp <= 32)
		par->cmode = CMODE_32;
	else {
		printk(KERN_ERR "Bad color mode in platinum_var_to_par()!\n");
		return -EINVAL;
	}

	if (platinum_vram_reqd(par->vmode, par->cmode) > p->total_vram) {
		printk(KERN_ERR "Bad vram size requested in platinum_var_to_par()!\n");
		return -EINVAL;
	}
	/* Check if we know about the wanted video mode */
	init = platinum_reg_init[par->vmode-1];
	if (init == NULL) {
		/* I'm not sure if platinum has any specific requirements --	*/
		/* if we have a regvals struct, we're good to go?		*/
		printk(KERN_ERR "platinum_reg_init failed platinum_var_to_par()!\n");
		return -EINVAL;
	}

	return 0;
}
#else
/* This routine takes a user-supplied var, and picks the best vmode/cmode from it. */
static int platinum_var_to_par(struct fb_var_screeninfo *var, 
	struct fb_par_platinum *par, const struct fb_info *fb_info)
{
	struct fb_info_platinum *p = (struct fb_info_platinum *) fb_info;
	
//    FUNCID;
	if(mac_var_to_vmode(var, &par->vmode, &par->cmode) != 0)
		return -EINVAL;
	par->xres = par->vxres = vmode_attrs[par->vmode - 1].hres;
	par->yres = par->vyres = vmode_attrs[par->vmode - 1].vres;
	par->xoffset = par->yoffset = 0;
	
	if (platinum_vram_reqd(par->vmode, par->cmode) > p->total_vram)
		return -EINVAL;

	/* Check if we know about the wanted video mode */
	if(!platinum_reg_init[par->vmode-1]) {
		/* I'm not sure if platinum has any specific requirements --	*/
		/* if we have a regvals struct, we're good to go?		*/
		return -EINVAL;
	}
	return 0;
}
#endif

#if 0
static void platinum_par_to_var(struct fb_par_platinum *par, struct fb_var_screeninfo *var)
{
	memset(var, 0, sizeof(*var));
	var->xres = vmode_attrs[par->vmode - 1].hres;
	var->yres = vmode_attrs[par->vmode - 1].vres;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;	/* For now. */
	var->xoffset = par->xoffset;
	var->yoffset = par->yoffset;
	var->grayscale = 0;
	
	if(par->cmode != CMODE_8 && par->cmode != CMODE_16 && par->cmode != CMODE_32) {
		printk(KERN_ERR "Bad color mode in platinum_par_to_var()!\n");
		par->cmode = CMODE_8;
	}
	switch(par->cmode) {
	case CMODE_8:
		var->bits_per_pixel = 8;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CMODE_16:	/* RGB 555 */
		var->bits_per_pixel = 16;
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case CMODE_32:	/* RGB 888 */
		var->bits_per_pixel = 32;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	var->nonstd = 0;
	var->activate = 0;
	var->height = -1;
	var->width = -1;
	var->vmode = FB_VMODE_NONINTERLACED;

	/* these are total guesses, copied right out of atyfb.c */
	var->left_margin = var->right_margin = 64;
	var->upper_margin = var->lower_margin = 32;
	var->hsync_len = 8;
	var->vsync_len = 8;
	var->sync = 0;

#if 1
/* jonh's pixclocks...*/
	/* no long long support in the kernel :-( */
	/* this splittig trick will work if xres > 232 */
	var->pixclock = 1000000000/
	(var->left_margin+var->xres+var->right_margin+var->hsync_len);
	var->pixclock *= 1000;
	var->pixclock /= vmode_attrs[par->vmode-1].vfreq*
	 (var->upper_margin+var->yres+var->lower_margin+var->vsync_len);
#else
/* danj's */
	/* 10^12 * clock_params[0] / (3906400 * clock_params[1] * 2^clock_params[2]) */
	/* (10^12 * clock_params[0] / (3906400 * clock_params[1])) >> clock_params[2] */
	/* (255990.17 * clock_params[0] / clock_params[1]) >> clock_params[2] */
	var->pixclock = 255990 * platinum_reg_init[par->vmode-1]->clock_params[0];
	var->pixclock /= platinum_reg_init[par->vmode-1]->clock_params[1];
	var->pixclock >>= platinum_reg_init[par->vmode-1]->clock_params[2];
#endif
}
#else
static inline void platinum_par_to_var(struct fb_par_platinum *par, struct fb_var_screeninfo *var)
{
//    FUNCID;
	mac_vmode_to_var(par->vmode, par->cmode, var);
}
#endif

static void platinum_init_fix(struct fb_fix_screeninfo *fix, struct fb_info_platinum *p)
{
//    FUNCID;
	memset(fix, 0, sizeof(*fix));
	strcpy(fix->id, "platinum");
	fix->mmio_start = (char *)p->platinum_regs_phys;
	fix->mmio_len = 0x1000;
	fix->type = FB_TYPE_PACKED_PIXELS;
	
	fix->type_aux = 0;
	fix->ywrapstep = 0;
	fix->xpanstep = 0;
	fix->ypanstep = 0;
}

/* Fix must already be inited ^^^^^^^ */
static void platinum_par_to_fix(struct fb_par_platinum *par, 
				struct fb_fix_screeninfo *fix,
				struct fb_info_platinum *p)
{
//    FUNCID;
	fix->smem_start = (void *)(p->frame_buffer_phys);
	fix->smem_len = platinum_vram_reqd(par->vmode, par->cmode);
		/* Hmm, jonh used total_vram here. */
	fix->visual = (par->cmode == CMODE_8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
//	fix->line_length = par->vxres << par->cmode;
	fix->line_length = platinum_reg_init[par->vmode-1]->pitch[par->cmode];

}

static void platinum_init_display(struct display *disp)
{
	memset(disp, 0, sizeof(*disp));
	disp->type = /* fix->type */ FB_TYPE_PACKED_PIXELS;
	disp->can_soft_blank = 1;
	disp->scrollmode = SCROLL_YREDRAW;
#if 0
		disp->type_aux = fix->type_aux;
		disp->cmap.red = NULL;	/* ??? danj */
		disp->cmap.green = NULL;
		disp->cmap.blue = NULL;
		disp->cmap.transp = NULL;
			/* Yeah, I realize I just set 0 = 0. */
#endif
}

static void platinum_par_to_display(struct fb_par_platinum *par,
  struct display *disp, struct fb_fix_screeninfo *fix, struct fb_info_platinum *p)
{
//    FUNCID;
	disp->var = p->var;
	disp->screen_base = (char *) p->frame_buffer
			  + platinum_reg_init[par->vmode-1]->fb_offset
			  + ((par->yres % 16) / 2) * fix->line_length + 0x10;
	disp->visual = fix->visual;
	disp->line_length = fix->line_length;

	if(disp->scrollmode != SCROLL_YREDRAW) {
		printk(KERN_ERR "Scroll mode not YREDRAW in platinum_par_to_display!!\n");
		disp->scrollmode = SCROLL_YREDRAW;
	}

	switch(par->cmode) {
#ifdef FBCON_HAS_CFB8
	 case CMODE_8:
	    disp->dispsw = &fbcon_cfb8;
	    break;
#endif
#ifdef FBCON_HAS_CFB16
	 case CMODE_16:
	    disp->dispsw = &fbcon_cfb16;
	    break;
#endif
#ifdef FBCON_HAS_CFB32
	 case CMODE_32:
	    disp->dispsw = &fbcon_cfb32;
	    break;
#endif
	 default:
	    disp->dispsw = NULL;
	    break;
	}
}

static void platinum_init_info(struct fb_info *info, struct fb_info_platinum *p)
{
//    FUNCID;
	strcpy(info->modename, p->fix.id);
	info->node = -1;	/* ??? danj */
	info->fbops = &platinumfb_ops;
	info->disp = &p->disp;
	info->fontname[0] = 0;
	info->changevar = NULL;
	info->switch_con = &platinum_switch;
	info->updatevar = &platinum_updatevar;
	info->blank = &platinum_blank;
}

/* danj: Oh, I HOPE I didn't miss anything major in here... */
static void platinum_par_to_all(struct fb_info_platinum *p, int init)
{
//    FUNCID;
	if(init) {
		platinum_init_fix(&p->fix, p);
	}
	platinum_par_to_fix(&p->par, &p->fix, p);

	platinum_par_to_var(&p->par, &p->var);

	if(init) {
		platinum_init_display(&p->disp);
	}
	platinum_par_to_display(&p->par, &p->disp, &p->fix, p);
	
	if(init) {
		platinum_init_info(&p->info, p);
	}
}

#if 0
__initfunc(void platinum_setup(char *options, int *ints))
{
    /* Parse user speficied options (`video=platinumfb:') */
	FUNCID;
}

#endif

