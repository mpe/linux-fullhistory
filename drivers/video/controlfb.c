/*
 *  controlfb.c -- frame buffer device for the PowerMac 'control' display
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
 *    control.c: Console support for PowerMac "control" display adaptor.
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

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>
#include <video/macmodes.h>

#include "controlfb.h"

static int currcon = 0;
static int switching = 0;

struct fb_par_control {
	int	vmode, cmode;
	int	xres, yres;
	int	vxres, vyres;
	int	xoffset, yoffset;
};

struct fb_info_control {
	struct fb_info			info;
	struct fb_fix_screeninfo	fix;
	struct fb_var_screeninfo	var;
	struct display			disp;
	struct fb_par_control		par;
	struct {
		__u8 red, green, blue;
	}			palette[256];
	
	struct cmap_regs	*cmap_regs;
	unsigned long		cmap_regs_phys;
	
	struct control_regs	*control_regs;
	unsigned long		control_regs_phys;
	
	__u8			*frame_buffer;
	unsigned long		frame_buffer_phys;
	
	int			sense, control_use_bank2;
	unsigned long		total_vram;
	union {
#ifdef FBCON_HAS_CFB16
		u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
		u32 cfb32[16];
#endif
	} fbcon_cmap;
};

/*
 * Exported functions
 */
void control_init(void);
void control_of_init(struct device_node *dp);

static int control_open(struct fb_info *info, int user);
static int control_release(struct fb_info *info, int user);
static int control_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int control_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int control_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int control_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info);
static int control_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int control_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int control_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info);

static int read_control_sense(struct fb_info_control *p);
static inline int control_vram_reqd(int video_mode, int color_mode);
static void set_control_clock(unsigned char *params);
static void control_set_hardware(struct fb_info_control *p);
static void control_par_to_all(struct fb_info_control *p, int init);
static inline void control_par_to_var(struct fb_par_control *par, struct fb_var_screeninfo *var);
static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info);

static void control_init_info(struct fb_info *info, struct fb_info_control *p);
static void control_par_to_display(struct fb_par_control *par,
  struct display *disp, struct fb_fix_screeninfo *fix, struct fb_info_control *p);
static void control_init_display(struct display *disp);
static void control_par_to_fix(struct fb_par_control *par, struct fb_fix_screeninfo *fix,
	struct fb_info_control *p);
static void control_init_fix(struct fb_fix_screeninfo *fix, struct fb_info_control *p);

static struct fb_ops controlfb_ops = {
	control_open,
	control_release,
	control_get_fix,
	control_get_var,
	control_set_var,
	control_get_cmap,
	control_set_cmap,
	control_pan_display,
	control_ioctl
};

static int controlfb_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info);
static int controlfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);


__openfirmware


static int control_open(struct fb_info *info, int user)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int control_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static int control_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	struct fb_info_control *cp = (struct fb_info_control *) info;

	*fix = cp->fix;
	return 0;
}

static int control_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_control *cp = (struct fb_info_control *) info;

	*var = cp->var;
	return 0;
}

/* Sets everything according to var */
static int control_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;
	struct display *disp;
	struct fb_par_control par;
	int depthchange, err;

	disp = (con >= 0) ? &fb_display[con] : &p->disp;
	if((err = control_var_to_par(var, &par, info))) {
		printk (KERN_ERR "Error in control_set_var, calling control_var_to_par: %d.\n", err);
		return err;
	}
	
	if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW) {
		/* printk("Not activating, in control_set_var.\n"); */
		control_par_to_var(&par, var);
		return 0;
	}
/* I know, we want to use fb_display[con], but grab certain info from p->var instead. */
#define DIRTY(x) (p->var.x != var->x)
	depthchange = DIRTY(bits_per_pixel);
	if(!DIRTY(xres) && !DIRTY(yres) && !DIRTY(xres_virtual) &&
	   !DIRTY(yres_virtual) && !DIRTY(bits_per_pixel)) {
	   	control_par_to_var(&par, var);
		p->var = disp->var = *var;
		return 0;
	}
	/* printk("Original bpp is %d, new bpp %d.\n", p->var.bits_per_pixel, var->bits_per_pixel); */
	/* OK, we're getting here at the right times... */
	p->par = par;
	control_par_to_var(&par, var);
	p->var = *var;
	control_par_to_fix(&par, &p->fix, p);
	control_par_to_display(&par, disp, &p->fix, p);
	p->disp = *disp;
	
	if(info->changevar && !switching)	/* Don't want to do this if just switching consoles. */
		(*info->changevar)(con);
	if(con == currcon)
		control_set_hardware(p);
	if(depthchange)
		if((err = fb_alloc_cmap(&disp->cmap, 0, 0)))
			return err;
	if(depthchange || switching)
		do_install_cmap(con, info);
	return 0;
}

static int control_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
	if (var->xoffset != 0 || var->yoffset != 0)
		return -EINVAL;
	return 0;
}

static int control_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	if (con == currcon)		/* current console? */
		return fb_get_cmap(cmap, kspc, controlfb_getcolreg, info);
	if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc? 0: 2);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
	}
	return 0;
}

static int control_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	struct display *disp = &fb_display[con];
	int err;

	if (disp->cmap.len == 0) {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		err = fb_alloc_cmap(&disp->cmap, size, 0);
		if (err)
			return err;
	}

	if (con == currcon)
		return fb_set_cmap(cmap, kspc, controlfb_setcolreg, info);
	fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
	return 0;
}

static int control_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg, int con, struct fb_info *info)
{
	return -EINVAL;
}

static int controlfb_switch(int con, struct fb_info *info)
{
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, 1, controlfb_getcolreg,
			    info);
	currcon = con;
#if 0
	control_var_to_par(&fb_display[currcon].var, &par, info);
	control_set_par(&par, info); /*STOPPEDHERE - did i define that? */
	do_install_cmap(con, info);
#else
	/* I see no reason not to do this.  Minus info->changevar(). */
	/* DOH.  This makes control_set_var compare, you guessed it, */
	/* fb_display[con].var (first param), and fb_display[con].var! */
	/* Perhaps I just fixed that... */
	switching = 1;
	control_set_var(&fb_display[con].var, con, info);
	switching = 0;
#endif
	return 0;
}

static int controlfb_updatevar(int con, struct fb_info *info)
{
	return 0;
}

static void controlfb_blank(int blank_mode, struct fb_info *info)
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
	struct fb_info_control *p = (struct fb_info_control *) info;
	int	ctrl;

	ctrl = ld_le32(&p->control_regs->ctrl.r) | 0x33;
	if (blank_mode)
		--blank_mode;
	if (blank_mode & VESA_VSYNC_SUSPEND)
		ctrl &= ~3;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x30;
	out_le32(&p->control_regs->ctrl.r, ctrl);

/* TODO: Figure out how the heck to powerdown this thing! */

    return;
}

static int controlfb_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;

	if (regno > 255)
		return 1;
	*red = (p->palette[regno].red<<8) | p->palette[regno].red;
	*green = (p->palette[regno].green<<8) | p->palette[regno].green;
	*blue = (p->palette[regno].blue<<8) | p->palette[regno].blue;
	*transp = 0;
	return 0;
}

static int controlfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;
	int i;

	if (regno > 255)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;
	p->palette[regno].red = red;
	p->palette[regno].green = green;
	p->palette[regno].blue = blue;

	out_8(&p->cmap_regs->addr, regno);	/* tell clut what addr to fill	*/
	out_8(&p->cmap_regs->lut, red);		/* send one color channel at	*/
	out_8(&p->cmap_regs->lut, green);	/* a time...			*/
	out_8(&p->cmap_regs->lut, blue);

	if (regno < 16)
		switch (p->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB16
			case 16:
#if 0
				p->fbcon_cmap.cfb16[regno] = (red << 10) | (green << 5) | blue;
#else
				p->fbcon_cmap.cfb16[regno] = (regno << 10) | (regno << 5) | regno;
#endif
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case 32:
#if 0
				p->fbcon_cmap.cfb32[regno] = (red << 16) | (green << 8) | blue;
#else
				i = (regno << 8) | regno;
				p->fbcon_cmap.cfb32[regno] = (i << 16) | i;
				/* I think */
#endif
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
		fb_set_cmap(&fb_display[con].cmap, 1, controlfb_setcolreg,
			    info);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_set_cmap(fb_default_cmap(size), 1, controlfb_setcolreg,
			    info);
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

static inline int control_vram_reqd(int video_mode, int color_mode)
{
	return control_reg_init[video_mode-1]->vres
		* control_reg_init[video_mode-1]->pitch[color_mode];
}

static void set_control_clock(unsigned char *params)
{
	struct adb_request req;
	int i;

	for (i = 0; i < 3; ++i) {
		cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
			     0x50, i + 1, params[i]);
		while (!req.complete)
			cuda_poll();
	}
}


__initfunc(static void init_control(struct fb_info_control *p))
{
	struct fb_par_control *par = &p->par;

	p->sense = read_control_sense(p);
	printk(KERN_INFO "Monitor sense value = 0x%x, ", p->sense);
	/* Try to pick a video mode out of NVRAM if we have one. */
	par->vmode = nvram_read_byte(NV_VMODE);
	if(par->vmode <= 0 || par->vmode > VMODE_MAX || !control_reg_init[par->vmode - 1])
		par->vmode = VMODE_CHOOSE;
	if(par->vmode == VMODE_CHOOSE)
		par->vmode = mac_map_monitor_sense(p->sense);
	if(!control_reg_init[par->vmode - 1])
		par->vmode = VMODE_640_480_60;

	par->cmode = nvram_read_byte(NV_CMODE);
	if(par->cmode < CMODE_8 || par->cmode > CMODE_32)
		par->cmode = CMODE_8;
	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	while(par->cmode > CMODE_8 && control_vram_reqd(par->vmode, par->cmode) > p->total_vram)
		par->cmode--;
	
	printk("using video mode %d and color mode %d.\n", par->vmode, par->cmode);
	
	par->vxres = par->xres = control_reg_init[par->vmode - 1]->hres;
	par->vyres = par->yres = control_reg_init[par->vmode - 1]->vres;
	par->xoffset = par->yoffset = 0;
	
	control_par_to_all(p, 1);
	
	if (register_framebuffer(&p->info) < 0) {
		kfree(p);
		return;
	}
	control_set_hardware(p);
	
	printk(KERN_INFO "fb%d: control display adapter\n", GET_FB_IDX(p->info.node));	
}

/* Now how about actually saying, Make it so! */
/* Some things in here probably don't need to be done each time. */
static void control_set_hardware(struct fb_info_control *p)
{
	struct control_regvals	*init;
	struct preg		*rp;
	int			flags, ctrl, i;
	int			vmode, cmode;
	
	vmode = p->par.vmode;
	cmode = p->par.cmode;
	
	init = control_reg_init[vmode - 1];
	
	if (control_vram_reqd(vmode, cmode) > 0x200000)
		flags = 0x51;
	else if (p->control_use_bank2)
		flags = 0x39;
	else
		flags = 0x31;
	if (vmode >= VMODE_1280_960_75 && cmode >= CMODE_16)
		ctrl = 0x7f;
	else
		ctrl = 0x3b;

	/* Initialize display timing registers */
	out_le32(&p->control_regs->ctrl.r, 0x43b);
	
	set_control_clock(init->clock_params);
	
	p->cmap_regs->addr = 0x20; p->cmap_regs->d2 = init->radacal_ctrl[cmode];
	p->cmap_regs->addr = 0x21; p->cmap_regs->d2 = p->control_use_bank2 ? 0: 1;
	p->cmap_regs->addr = 0x10; p->cmap_regs->d2 = 0;
	p->cmap_regs->addr = 0x11; p->cmap_regs->d2 = 0;

	rp = &p->control_regs->vswin;
	for (i = 0; i < 16; ++i, ++rp)
		out_le32(&rp->r, init->regs[i]);
	
	out_le32(&p->control_regs->pitch.r, init->pitch[cmode]);
	out_le32(&p->control_regs->mode.r, init->mode[cmode]);
	out_le32(&p->control_regs->flags.r, flags);
	out_le32(&p->control_regs->start_addr.r, 0);
	out_le32(&p->control_regs->reg18.r, 0x1e5);
	out_le32(&p->control_regs->reg19.r, 0);

	for (i = 0; i < 16; ++i) {
		controlfb_setcolreg(color_table[i], default_red[i]<<8,
				    default_grn[i]<<8, default_blu[i]<<8,
				    0, (struct fb_info *)p);
	}
/* Does the above need to be here each time? -- danj */

	/* Turn on display */
	out_le32(&p->control_regs->ctrl.r, ctrl);
	
#ifdef CONFIG_FB_COMPAT_XPMAC
	/* And let the world know the truth. */
	if (!console_fb_info || console_fb_info == &p->info) {
		display_info.height = p->var.yres;
		display_info.width = p->var.xres;
		display_info.depth = (cmode == CMODE_32) ? 32 :
			((cmode == CMODE_16) ? 16 : 8);
		display_info.pitch = p->fix.line_length;
		display_info.mode = vmode;
		strncpy(display_info.name, "control",
			sizeof(display_info.name));
		display_info.fb_address = p->frame_buffer_phys
			 + control_reg_init[vmode-1]->offset[cmode];
		display_info.cmap_adr_address = p->cmap_regs_phys;
		display_info.cmap_data_address = p->cmap_regs_phys + 0x30;
		display_info.disp_reg_address = p->control_regs_phys;
		console_fb_info = &p->info;
	}
#endif /* CONFIG_FB_COMPAT_XPMAC */
}

__initfunc(void control_init(void))
{
#ifndef CONFIG_FB_OF
	struct device_node *dp;

	dp = find_devices("control");
	if (dp != 0)
		control_of_init(dp);
#endif /* CONFIG_FB_OF */
}

__initfunc(void control_of_init(struct device_node *dp))
{
	struct fb_info_control	*p;
	unsigned long		addr, size;
	int			i, bank1, bank2;

#if 0
	if(dp->next != 0)
		printk("Warning: only using first control display device.\n");
		/* danj: I have a feeling this no longer applies - if we somehow *
		 * had two of them, they'd be two framebuffers, right?
		 * Yep. - paulus
		 */
#endif

	if(dp->n_addrs != 2) {
		printk(KERN_ERR "expecting 2 address for control (got %d)", dp->n_addrs);
		return;
	}
	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == 0)
		return;
	memset(p, 0, sizeof(*p));

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		if (size >= 0x800000) {
			/* use the big-endian aperture (??) */
			addr += 0x800000;
			/* map at most 8MB for the frame buffer */
			p->frame_buffer_phys = addr;
			p->frame_buffer = __ioremap(addr, 0x800000, _PAGE_WRITETHRU);
		} else {
			p->control_regs_phys = addr;
			p->control_regs = ioremap(addr, size);
		}
	}
	p->cmap_regs_phys = 0xf301b000;	 /* XXX not in prom? */
	p->cmap_regs = ioremap(p->cmap_regs_phys, 0x1000);

	/* Work out which banks of VRAM we have installed. */
	/* danj: I guess the card just ignores writes to nonexistant VRAM... */
	p->frame_buffer[0] = 0x5a;
	p->frame_buffer[1] = 0xc7;
	bank1 = p->frame_buffer[0] == 0x5a && p->frame_buffer[1] == 0xc7;
	p->frame_buffer[0x600000] = 0xa5;
	p->frame_buffer[0x600001] = 0x38;
	bank2 = p->frame_buffer[0x600000] == 0xa5 && p->frame_buffer[0x600001] == 0x38;
	p->total_vram = (bank1 + bank2) * 0x200000;
	/* If we don't have bank 1 installed, we hope we have bank 2 :-) */
	p->control_use_bank2 = !bank1;
	if (p->control_use_bank2)
		p->frame_buffer += 0x600000;

#ifdef CONFIG_FB_COMPAT_XPMAC
#if 0
	console_set_cmap_ptr = control_set_cmap;
	console_setmode_ptr = control_console_setmode;
#endif
#endif /* CONFIG_FB_COMPAT_XPMAC */

	init_control(p);
}

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 *
 * Hmm - looking at platinum, should we be calling eieio() here?
 */
static int read_control_sense(struct fb_info_control *p)
{
	int sense;

	out_le32(&p->control_regs->mon_sense.r, 7);	/* drive all lines high */
	__delay(200);
	out_le32(&p->control_regs->mon_sense.r, 077);	/* turn off drivers */
	__delay(2000);
	sense = (in_le32(&p->control_regs->mon_sense.r) & 0x1c0) << 2;

	/* drive each sense line low in turn and collect the other 2 */
	out_le32(&p->control_regs->mon_sense.r, 033);	/* drive A low */
	__delay(2000);
	sense |= (in_le32(&p->control_regs->mon_sense.r) & 0xc0) >> 2;
	out_le32(&p->control_regs->mon_sense.r, 055);	/* drive B low */
	__delay(2000);
	sense |= ((in_le32(&p->control_regs->mon_sense.r) & 0x100) >> 5)
		| ((in_le32(&p->control_regs->mon_sense.r) & 0x40) >> 4);
	out_le32(&p->control_regs->mon_sense.r, 066);	/* drive C low */
	__delay(2000);
	sense |= (in_le32(&p->control_regs->mon_sense.r) & 0x180) >> 7;

	out_le32(&p->control_regs->mon_sense.r, 077);	/* turn off drivers */
	
	return sense;
}

#if 1
/* This routine takes a user-supplied var, and picks the best vmode/cmode from it. */
static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info)
{
	int xres = var->xres;
	int yres = var->yres;
	int bpp = var->bits_per_pixel;
	
	struct control_regvals *init;
	struct fb_info_control *p = (struct fb_info_control *) fb_info;

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
	else
		return -EINVAL;

	xres = control_reg_init[par->vmode-1]->hres;
	yres = control_reg_init[par->vmode-1]->vres;

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
	else
		return -EINVAL;

	if (control_vram_reqd(par->vmode, par->cmode) > p->total_vram)
		return -EINVAL;

	/* Check if we know about the wanted video mode */
	init = control_reg_init[par->vmode-1];
	if (init == NULL) {
		/* I'm not sure if control has any specific requirements --	*/
		/* if we have a regvals struct, we're good to go?		*/
		return -EINVAL;
	}

	return 0;
}
#else
/* This routine takes a user-supplied var, and picks the best vmode/cmode from it. */
static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info)
{
	struct fb_info_control *p = (struct fb_info_control *) fb_info;
	
	if(mac_var_to_vmode(var, &par->vmode, &par->cmode) != 0)
		return -EINVAL;
	par->xres = par->vxres = vmode_attrs[par->vmode - 1].hres;
	par->yres = par->vyres = vmode_attrs[par->vmode - 1].vres;
	par->xoffset = par->yoffset = 0;
	
	if (control_vram_reqd(par->vmode, par->cmode) > p->total_vram)
		return -EINVAL;

	/* Check if we know about the wanted video mode */
	if(!control_reg_init[par->vmode-1]) {
		/* I'm not sure if control has any specific requirements --	*/
		/* if we have a regvals struct, we're good to go?		*/
		return -EINVAL;
	}
	return 0;
}
#endif

#if 1
static void control_par_to_var(struct fb_par_control *par, struct fb_var_screeninfo *var)
{
	memset(var, 0, sizeof(*var));
	var->xres = control_reg_init[par->vmode - 1]->hres;
	var->yres = control_reg_init[par->vmode - 1]->vres;
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;	/* For now. */
	var->xoffset = par->xoffset;
	var->yoffset = par->yoffset;
	var->grayscale = 0;
	
	if(par->cmode != CMODE_8 && par->cmode != CMODE_16 && par->cmode != CMODE_32) {
		printk(KERN_ERR "Bad color mode in control_par_to_var()!\n");
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
	var->hsync_len = /*64*/8;
	var->vsync_len = /*2*/8;
	var->sync = 0;

#if 0
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
	var->pixclock = 255990 * control_reg_init[par->vmode-1]->clock_params[0];
	var->pixclock /= control_reg_init[par->vmode-1]->clock_params[1];
	var->pixclock >>= control_reg_init[par->vmode-1]->clock_params[2];
#endif
}
#else
static inline void control_par_to_var(struct fb_par_control *par, struct fb_var_screeninfo *var)
{
	mac_vmode_to_var(par->vmode, par->cmode, var);
}
#endif

static void control_init_fix(struct fb_fix_screeninfo *fix, struct fb_info_control *p)
{
	memset(fix, 0, sizeof(*fix));
	strcpy(fix->id, "control");
	fix->mmio_start = (char *)p->control_regs_phys;
	fix->mmio_len = sizeof(struct control_regs);
	fix->type = FB_TYPE_PACKED_PIXELS;
	
	/*
		fix->type_aux = 0;
		fix->ywrapstep = 0;
		fix->ypanstep = 0;
		fix->xpanstep = 0;
	*/
}

/* Fix must already be inited ^^^^^^^ */
static void control_par_to_fix(struct fb_par_control *par, struct fb_fix_screeninfo *fix,
	struct fb_info_control *p)
{
	fix->smem_start = (void *)(p->frame_buffer_phys
		+ control_reg_init[par->vmode-1]->offset[par->cmode]);
	p->fix.smem_len = control_vram_reqd(par->vmode, par->cmode);
		/* Hmm, jonh used total_vram here. */
	p->fix.visual = (par->cmode == CMODE_8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
	p->fix.line_length = par->vxres << par->cmode;
		/* ywrapstep, xpanstep, ypanstep */
}

static void control_init_display(struct display *disp)
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

static void control_par_to_display(struct fb_par_control *par,
  struct display *disp, struct fb_fix_screeninfo *fix, struct fb_info_control *p)
{
	disp->var = p->var;
	disp->screen_base = (char *) p->frame_buffer
		 + control_reg_init[par->vmode-1]->offset[par->cmode];
	disp->visual = fix->visual;
	disp->line_length = fix->line_length;

if(disp->scrollmode != SCROLL_YREDRAW) {
	printk(KERN_ERR "Scroll mode not YREDRAW in control_par_to_display!!\n");
	disp->scrollmode = SCROLL_YREDRAW;
}
	switch (par->cmode) {
#ifdef FBCON_HAS_CFB8
		case CMODE_8:
			disp->dispsw = &fbcon_cfb8;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case CMODE_16:
			disp->dispsw = &fbcon_cfb16;
			disp->dispsw_data = p->fbcon_cmap.cfb16;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case CMODE_32:
			disp->dispsw = &fbcon_cfb32;
			disp->dispsw_data = p->fbcon_cmap.cfb32;
			break;
#endif
		default:
			disp->dispsw = &fbcon_dummy;
			break;
	}
}

static void control_init_info(struct fb_info *info, struct fb_info_control *p)
{
	strcpy(info->modename, p->fix.id);
	info->node = -1;	/* ??? danj */
	info->fbops = &controlfb_ops;
	info->disp = &p->disp;
	info->fontname[0] = 0;
	info->changevar = NULL;
	info->switch_con = &controlfb_switch;
	info->updatevar = &controlfb_updatevar;
	info->blank = &controlfb_blank;
}

/* danj: Oh, I HOPE I didn't miss anything major in here... */
static void control_par_to_all(struct fb_info_control *p, int init)
{
	if(init) {
		control_init_fix(&p->fix, p);
	}
	control_par_to_fix(&p->par, &p->fix, p);

	control_par_to_var(&p->par, &p->var);

	if(init) {
		control_init_display(&p->disp);
	}
	control_par_to_display(&p->par, &p->disp, &p->fix, p);
	
	if(init) {
		control_init_info(&p->info, p);
	}
}

#if 0
__initfunc(void controlfb_setup(char *options, int *ints))
{
    /* Parse user speficied options (`video=controlfb:') */
	FUNCID;
}

static int controlfb_pan_display(struct fb_var_screeninfo *var,
			   struct controlfb_par *par,
			   const struct fb_info *fb_info)
{
    /*
     *  Pan (or wrap, depending on the `vmode' field) the display using the
     *  `xoffset' and `yoffset' fields of the `var' structure.
     *  If the values don't fit, return -EINVAL.
     */

	FUNCID;

    return 0;
}

#endif
