/*
 *
 * tdfxfb.c
 *
 * Author: Hannu Mallat <hmallat@cc.hut.fi>
 *
 * Copyright © 1999 Hannu Mallat
 * All rights reserved
 *
 * Created      : Thu Sep 23 18:17:43 1999, hmallat
 * Last modified: Thu Oct  7 18:39:04 1999, hmallat
 *
 * Lots of the information here comes from the Daryll Strauss' Banshee 
 * patches to the XF86 server, and the rest comes from the 3dfx
 * Banshee specification. I'm very much indebted to Daryll for his
 * work on the X server.
 *
 * Voodoo3 support was contributed Harold Oga. Thanks!
 *
 * While I _am_ grateful to 3Dfx for releasing the specs for Banshee,
 * I do wish the next version is a bit more complete. Without the XF86
 * patches I couldn't have gotten even this far... for instance, the
 * extensions to the VGA register set go completely unmentioned in the
 * spec! Also, lots of references are made to the 'SST core', but no
 * spec is publicly available, AFAIK.
 *
 * The structure of this driver comes pretty much from the Permedia
 * driver by Ilario Nardinocchi, which in turn is based on skeletonfb.
 * 
 * TODO:
 * - support for 16/32 bpp needs fixing (funky bootup penguin)
 * - multihead support (it's all hosed now with pokes to VGA standard
 *   register locations, but shouldn't be that hard to change, some
 *   other code needs to be changed too where the fb_info (which should
 *   be an array of head-specific information) is referred to directly.
 *   are referred to )
 * - hw cursor
 * - better acceleration support (e.g., font blitting from fb memory?)
 * - banshee and voodoo3 now supported -- any others? afaik, the original
 *   voodoo was a 3d-only card, so we won't consider that. what about
 *   voodoo2?
 * - 24bpp 
 * - panning (doesn't seem to work properly yet)
 *
 * Version history:
 *
 * 0.1.1 (released 1999-10-07) added Voodoo3 support by Harold Oga.
 * 0.1.0 (released 1999-10-06) initial version
 *
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
#include <linux/console.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#include <linux/kd.h>
#include <linux/vt_kern.h>
#include <asm/io.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
#define PCI_DEVICE_ID_3DFX_VOODOO3      0x0005
#endif

/* membase0 register offsets */
#define STATUS		0x00
#define PCIINIT0	0x04
#define SIPMONITOR	0x08
#define LFBMEMORYCONFIG	0x0c
#define MISCINIT0	0x10
#define MISCINIT1	0x14
#define DRAMINIT0	0x18
#define DRAMINIT1	0x1c
#define AGPINIT		0x20
#define TMUGBEINIT	0x24
#define VGAINIT0	0x28
#define VGAINIT1	0x2c
#define DRAMCOMMAND	0x30
#define DRAMDATA	0x34
/* reserved             0x38 */
/* reserved             0x3c */
#define PLLCTRL0	0x40
#define PLLCTRL1	0x44
#define PLLCTRL2	0x48
#define DACMODE		0x4c
#define DACADDR		0x50
#define DACDATA		0x54
#define RGBMAXDELTA	0x58
#define VIDPROCCFG	0x5c
#define HWCURPATADDR	0x60
#define HWCURLOC	0x64
#define HWCURC0		0x68
#define HWCURC1		0x6c
#define VIDINFORMAT	0x70
#define VIDINSTATUS	0x74
#define VIDSERPARPORT	0x78
#define VIDINXDELTA	0x7c
#define VIDININITERR	0x80
#define VIDINYDELTA	0x84
#define VIDPIXBUFTHOLD	0x88
#define VIDCHRMIN	0x8c
#define VIDCHRMAX	0x90
#define VIDCURLIN	0x94
#define VIDSCREENSIZE	0x98
#define VIDOVRSTARTCRD	0x9c
#define VIDOVRENDCRD	0xa0
#define VIDOVRDUDX	0xa4
#define VIDOVRDUDXOFF	0xa8
#define VIDOVRDVDY	0xac
/*  ... */
#define VIDOVRDVDYOFF	0xe0
#define VIDDESKSTART	0xe4
#define VIDDESKSTRIDE	0xe8
#define VIDINADDR0	0xec
#define VIDINADDR1	0xf0
#define VIDINADDR2	0xf4
#define VIDINSTRIDE	0xf8
#define VIDCUROVRSTART	0xfc

#define INTCTRL		(0x00100000 + 0x04)
#define CLIP0MIN	(0x00100000 + 0x08)
#define CLIP0MAX	(0x00100000 + 0x0c)
#define DSTBASE		(0x00100000 + 0x10)
#define DSTFORMAT	(0x00100000 + 0x14)
#define SRCBASE		(0x00100000 + 0x34)
#define COMMANDEXTRA_2D	(0x00100000 + 0x38)
#define CLIP1MIN	(0x00100000 + 0x4c)
#define CLIP1MAX	(0x00100000 + 0x50)
#define SRCFORMAT	(0x00100000 + 0x54)
#define SRCSIZE		(0x00100000 + 0x58)
#define SRCXY		(0x00100000 + 0x5c)
#define COLORBACK	(0x00100000 + 0x60)
#define COLORFORE	(0x00100000 + 0x64)
#define DSTSIZE		(0x00100000 + 0x68)
#define DSTXY		(0x00100000 + 0x6c)
#define COMMAND_2D	(0x00100000 + 0x70)
#define LAUNCH_2D	(0x00100000 + 0x80)

#define COMMAND_3D	(0x00200000 + 0x120)

/* register bitfields (not all, only as needed) */

#define BIT(x) (1UL << (x))

#define ROP_COPY	0xcc

#define COMMAND_2D_FILLRECT		0x05
#define COMMAND_2D_BITBLT		0x01

#define COMMAND_3D_NOP			0x00

#define STATUS_RETRACE			BIT(6)
#define STATUS_BUSY			BIT(9)

#define MISCINIT1_CLUT_INV		BIT(0)
#define MISCINIT1_2DBLOCK_DIS		BIT(15)

#define DRAMINIT0_SGRAM_NUM		BIT(26)
#define DRAMINIT0_SGRAM_TYPE		BIT(27)

#define DRAMINIT1_MEM_SDRAM		BIT(30)

#define VGAINIT0_VGA_DISABLE		BIT(0)
#define VGAINIT0_EXT_TIMING		BIT(1)
#define VGAINIT0_8BIT_DAC		BIT(2)
#define VGAINIT0_EXT_ENABLE		BIT(6)
#define VGAINIT0_WAKEUP_3C3		BIT(8)
#define VGAINIT0_LEGACY_DISABLE		BIT(9)
#define VGAINIT0_ALT_READBACK		BIT(10)
#define VGAINIT0_FAST_BLINK		BIT(11)
#define VGAINIT0_EXTSHIFTOUT		BIT(12)
#define VGAINIT0_DECODE_3C6		BIT(13)
#define VGAINIT0_SGRAM_HBLANK_DISABLE	BIT(22)

#define VGAINIT1_MASK			0x1fffff

#define VIDCFG_VIDPROC_ENABLE		BIT(0)
#define VIDCFG_CURS_X11			BIT(1)
#define VIDCFG_HALF_MODE		BIT(4)
#define VIDCFG_DESK_ENABLE		BIT(7)
#define VIDCFG_CLUT_BYPASS		BIT(10)
#define VIDCFG_2X			BIT(26)
#define VIDCFG_PIXFMT_SHIFT		18

#define DACMODE_2X			BIT(0)

/* VGA rubbish, need to change this for multihead support */
#define MISC_W 	0x3c2
#define MISC_R 	0x3cc
#define SEQ_I 	0x3c4
#define SEQ_D	0x3c5
#define CRT_I	0x3d4
#define CRT_D	0x3d5
#define ATT_IW	0x3c0
#define IS1_R	0x3da
#define GRA_I	0x3ce
#define GRA_D	0x3cf
#define DAC_IR	0x3c7
#define DAC_IW	0x3c8
#define DAC_D	0x3c9

#ifndef FB_ACCEL_3DFX_BANSHEE 
#define FB_ACCEL_3DFX_BANSHEE 31
#endif

#define TDFXF_HSYNC_ACT_HIGH	0x01
#define TDFXF_HSYNC_ACT_LOW	0x02
#define TDFXF_VSYNC_ACT_HIGH	0x04
#define TDFXF_VSYNC_ACT_LOW	0x08
#define TDFXF_LINE_DOUBLE	0x10
#define TDFXF_VIDEO_ENABLE	0x20

#define TDFXF_HSYNC_MASK	0x03
#define TDFXF_VSYNC_MASK	0x0c

/* #define TDFXFB_DEBUG */
#ifdef TDFXFB_DEBUG
#define DPRINTK(a,b...) printk("fb: %s: " a, __FUNCTION__ , ## b)
#else
#define DPRINTK(a,b...)
#endif 

#define PICOS2KHZ(a) (1000000000UL/(a))
#define KHZ2PICOS(a) (1000000000UL/(a))

#define BANSHEE_MAX_PIXCLOCK 270000.0
#define VOODOO3_MAX_PIXCLOCK 300000.0

struct banshee_reg {
  /* VGA rubbish */
  unsigned char att[21];
  unsigned char crt[25];
  unsigned char gra[ 9];
  unsigned char misc[1];
  unsigned char seq[ 5];

  /* Banshee extensions */
  unsigned char ext[2];
  unsigned long vidcfg;
  unsigned long vidpll;
  unsigned long mempll;
  unsigned long gfxpll;
  unsigned long dacmode;
  unsigned long vgainit0;
  unsigned long vgainit1;
  unsigned long screensize;
  unsigned long stride;
  unsigned long cursloc;
  unsigned long startaddr;
  unsigned long clip0min;
  unsigned long clip0max;
  unsigned long clip1min;
  unsigned long clip1max;
  unsigned long srcbase;
  unsigned long dstbase;
};

struct tdfxfb_par {
  u32 pixclock;

  u32 baseline;

  u32 width;
  u32 height;
  u32 width_virt;
  u32 height_virt;
  u32 lpitch; /* line pitch, in bytes */
  u32 ppitch; /* pixel pitch, in bits */
  u32 bpp;    

  u32 hdispend;
  u32 hsyncsta;
  u32 hsyncend;
  u32 htotal;

  u32 vdispend;
  u32 vsyncsta;
  u32 vsyncend;
  u32 vtotal;

  u32 video;
  u32 accel_flags;
};

struct fb_info_tdfx {
  struct fb_info fb_info;

  u16 dev;
  u32 max_pixclock;

  unsigned long regbase_phys;
  unsigned long regbase_virt;
  unsigned long regbase_size;
  unsigned long bufbase_phys;
  unsigned long bufbase_virt;
  unsigned long bufbase_size;

  struct { u8 red, green, blue, pad; } palette[256];
  struct tdfxfb_par default_par;
  struct tdfxfb_par current_par;
  struct display disp;
  struct display_switch dispsw;
  
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
 *  Frame buffer device API
 */
static int tdfxfb_open(struct fb_info* info, 
		       int user);
static int tdfxfb_release(struct fb_info* info, 
			  int user);
static int tdfxfb_get_fix(struct fb_fix_screeninfo* fix, 
			  int con,
			  struct fb_info* fb);
static int tdfxfb_get_var(struct fb_var_screeninfo* var, 
			  int con,
			  struct fb_info* fb);
static int tdfxfb_set_var(struct fb_var_screeninfo* var,
			  int con,
			  struct fb_info* fb);
static int tdfxfb_pan_display(struct fb_var_screeninfo* var, 
			      int con,
			      struct fb_info* fb);
static int tdfxfb_get_cmap(struct fb_cmap *cmap, 
			   int kspc, 
			   int con,
			   struct fb_info* info);
static int tdfxfb_set_cmap(struct fb_cmap* cmap, 
			   int kspc, 
			   int con,
			   struct fb_info* info);
static int tdfxfb_ioctl(struct inode* inode, 
			struct file* file, 
			u_int cmd,
			u_long arg, 
			int con, 
			struct fb_info* info);

/*
 *  Interface to the low level console driver
 */
static int  tdfxfb_switch_con(int con, 
			      struct fb_info* fb);
static int  tdfxfb_updatevar(int con, 
			     struct fb_info* fb);
static void tdfxfb_blank(int blank, 
			 struct fb_info* fb);

/*
 *  Internal routines
 */
static void tdfxfb_set_par(const struct tdfxfb_par* par,
			   struct fb_info_tdfx* 
			   info);
static int  tdfxfb_decode_var(const struct fb_var_screeninfo *var,
			      struct tdfxfb_par *par,
			      const struct fb_info_tdfx *info);
static int  tdfxfb_encode_var(struct fb_var_screeninfo* var,
			      const struct tdfxfb_par* par,
			      const struct fb_info_tdfx* info);
static int  tdfxfb_encode_fix(struct fb_fix_screeninfo* fix,
			      const struct tdfxfb_par* par,
			      const struct fb_info_tdfx* info);
static void tdfxfb_set_disp(struct display* disp, 
			    struct fb_info_tdfx* info,
			    int bpp, 
			    int accel);
static int  tdfxfb_getcolreg(u_int regno,
			     u_int* red, 
			     u_int* green, 
			     u_int* blue,
			     u_int* transp, 
			     struct fb_info* fb);
static int  tdfxfb_setcolreg(u_int regno, 
			     u_int red, 
			     u_int green, 
			     u_int blue,
			     u_int transp, 
			     struct fb_info* fb);
static void  tdfxfb_install_cmap(int con, 
				 struct fb_info *info);

/*
 *  Interface used by the world
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
void tdfxfb_init(void);
#else
int tdfxfb_init(void);
#endif
void tdfxfb_setup(char *options, 
		  int *ints);

static int currcon = 0;

static struct fb_ops tdfxfb_ops = {
  tdfxfb_open, 
  tdfxfb_release, 
  tdfxfb_get_fix, 
  tdfxfb_get_var, 
  tdfxfb_set_var,
  tdfxfb_get_cmap, 
  tdfxfb_set_cmap, 
  tdfxfb_pan_display, 
  tdfxfb_ioctl,
  NULL
};

struct mode {
  char* name;
  struct fb_var_screeninfo var;
} mode;

/* 2.3.x kernels have a fb mode database, so supply only one backup default */
struct mode default_mode[] = {
  { "640x480-8@60", /* @ 60 Hz */
    {
      640, 480, 640, 480, 0, 0, 8, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, FB_ACTIVATE_NOW, -1, -1, FB_ACCELF_TEXT, 
      39722, 40, 24, 32, 11, 96, 2,
      0, FB_VMODE_NONINTERLACED
    }
  }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
  ,
  { "800x600-8@56", /* @ 56 Hz */
    {
      800, 600, 800, 600, 0, 0, 8, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, FB_ACTIVATE_NOW, -1, -1, FB_ACCELF_TEXT, 
      27778, 128, 24, 22, 1, 72, 2,
      0, FB_VMODE_NONINTERLACED
    }
  },
  { "1024x768-8@60", /* @ 60 Hz */
    {
      1024, 768, 1024, 768, 0, 0, 8, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, FB_ACTIVATE_NOW, -1, -1, FB_ACCELF_TEXT, 
      15385, 168, 8, 29, 3, 144, 6,
      0, FB_VMODE_NONINTERLACED
    }
  },
  { "1280x1024-8@61", /* @ 61 Hz */
    {
      1280, 1024, 1280, 1024, 0, 0, 8, 0,
      {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
      0, FB_ACTIVATE_NOW, -1, -1, FB_ACCELF_TEXT, 
      9091, 200, 48, 26, 1, 184, 3,
      0, FB_VMODE_NONINTERLACED
    }
  },
  { "1024x768-16@60", /* @ 60 Hz */ /* basically for testing */
    {
      1024, 768, 1024, 768, 0, 0, 16, 0,
      {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
      0, FB_ACTIVATE_NOW, -1, -1, FB_ACCELF_TEXT, 
      15385, 168, 8, 29, 3, 144, 6,
      0, FB_VMODE_NONINTERLACED
    }
  }
#endif
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
static int modes = sizeof(default_mode)/sizeof(struct mode);
static int default_mode_index = 0;
#endif

static struct fb_info_tdfx fb_info;

static int  __initdata noaccel = 0;
static int  __initdata nopan   = 0;
static int  __initdata nowrap  = 0;
static int  __initdata inverse = 0;
static char __initdata fontname[40] = { 0 };
static const char *mode_option __initdata = NULL;

/* ------------------------------------------------------------------------- */

static inline  __u8 vga_inb(__u32 reg) { return inb(reg); }
static inline __u16 vga_inw(__u32 reg) { return inw(reg); }
static inline __u16 vga_inl(__u32 reg) { return inl(reg); }

static inline void vga_outb(__u32 reg,  __u8 val) { outb(val, reg); }
static inline void vga_outw(__u32 reg, __u16 val) { outw(val, reg); }
static inline void vga_outl(__u32 reg, __u32 val) { outl(val, reg); }

static inline void gra_outb(__u32 idx, __u8 val) {
  vga_outb(GRA_I, idx); vga_outb(GRA_D, val);
}

static inline __u8 gra_inb(__u32 idx) {
  vga_outb(GRA_I, idx); return vga_inb(GRA_D);
}

static inline void seq_outb(__u32 idx, __u8 val) {
  vga_outb(SEQ_I, idx); vga_outb(SEQ_D, val);
}

static inline __u8 seq_inb(__u32 idx) {
  vga_outb(SEQ_I, idx); return vga_inb(SEQ_D);
}

static inline void crt_outb(__u32 idx, __u8 val) {
  vga_outb(CRT_I, idx); vga_outb(CRT_D, val);
}

static inline __u8 crt_inb(__u32 idx) {
  vga_outb(CRT_I, idx); return vga_inb(CRT_D);
}

static inline void att_outb(__u32 idx, __u8 val) {
  unsigned char tmp;
  tmp = vga_inb(IS1_R);
  vga_outb(ATT_IW, idx);
  vga_outb(ATT_IW, val);
}

static inline __u8 att_inb(__u32 idx) {
  unsigned char tmp;
  tmp = vga_inb(IS1_R);
  vga_outb(ATT_IW, idx);
  return vga_inb(ATT_IW);
}

static inline void vga_disable_video(void) {
  unsigned char s;
  s = seq_inb(0x01) | 0x20;
  seq_outb(0x00, 0x01);
  seq_outb(0x01, s);
  seq_outb(0x00, 0x03);
}

static inline void vga_enable_video(void) {
  unsigned char s;
  s = seq_inb(0x01) & 0xdf;
  seq_outb(0x00, 0x01);
  seq_outb(0x01, s);
  seq_outb(0x00, 0x03);
}

static inline void vga_disable_palette(void) {
  vga_inb(IS1_R);
  vga_outb(ATT_IW, 0x00);
}

static inline void vga_enable_palette(void) {
  vga_inb(IS1_R);
  vga_outb(ATT_IW, 0x20);
}

static inline __u32 tdfx_inl(unsigned int reg) {
  return readl(fb_info.regbase_virt + reg);
}

static inline void tdfx_outl(unsigned int reg, __u32 val) {
  writel(val, fb_info.regbase_virt + reg);
}

static inline void banshee_make_room(int size) {
  while((tdfx_inl(STATUS) & 0x1f) < size);
}
 
static inline void banshee_wait_idle(void) {
  int i = 0;

  banshee_make_room(1);
  tdfx_outl(COMMAND_3D, COMMAND_3D_NOP);

  while(1) {
    i = (tdfx_inl(STATUS) & STATUS_BUSY) ? 0 : i + 1;
    if(i == 3) break;
  }
}

static void banshee_fillrect(__u32 x, 
			     __u32 y, 
			     __u32 w,
			     __u32 h, 
			     __u32 color,
			     __u32 stride,
			     __u32 bpp) {
  banshee_make_room(2);
  tdfx_outl(DSTFORMAT, 
	    (stride & 0x3fff) | 
	    (bpp ==  8 ? 0x10000 : 
	     bpp == 16 ? 0x30000 : 0x50000));
  tdfx_outl(COLORFORE, color);
  
  banshee_make_room(3);
  tdfx_outl(COMMAND_2D, COMMAND_2D_FILLRECT | (ROP_COPY << 24));
  tdfx_outl(DSTSIZE,    (w & 0x1fff) | ((h & 0x1fff) << 16));
  tdfx_outl(LAUNCH_2D,  (x & 0x1fff) | ((y & 0x1fff) << 16));
}

static void banshee_bitblt(__u32 curx, 
			   __u32 cury, 
			   __u32 dstx,
			   __u32 dsty, 
			   __u32 width, 
			   __u32 height,
			   __u32 stride,
			   __u32 bpp) {
  int xdir, ydir;

  xdir = dstx < curx ? 1 : -1;
  ydir = dsty < cury ? 1 : -1;

  banshee_make_room(4);
  tdfx_outl(SRCFORMAT, 
	    (stride & 0x3fff) | 
	    (bpp ==  8 ? 0x10000 : 
	     bpp == 16 ? 0x30000 : 0x50000));
  tdfx_outl(DSTFORMAT, 
	    (stride & 0x3fff) | 
	    (bpp ==  8 ? 0x10000 : 
	     bpp == 16 ? 0x30000 : 0x50000));
  tdfx_outl(COMMAND_2D, 
	    COMMAND_2D_BITBLT | 
	    (xdir == -1 ? BIT(14) : 0) |
	    (ydir == -1 ? BIT(15) : 0));
  tdfx_outl(COMMANDEXTRA_2D, 0); /* no color keying */

  if(xdir == -1) {
    curx += width - 1;
    dstx += width - 1;
  }
  if(ydir == -1) {
    cury += height - 1;
    dsty += height - 1;
  }
  
  /* Consecutive overlapping regions can hang the board -- 
     since we allow mmap'ing of control registers, we cannot
     __safely__ assume anything, like XF86 does... */
  banshee_make_room(1);
  tdfx_outl(COMMAND_3D, COMMAND_3D_NOP);

  banshee_make_room(3);
  tdfx_outl(DSTSIZE,   (width & 0x1fff) | ((height & 0x1fff) << 16));
  tdfx_outl(DSTXY,     (dstx  & 0x1fff) | ((dsty  & 0x1fff) << 16));
  tdfx_outl(LAUNCH_2D, (curx  & 0x1fff) | ((cury  & 0x1fff) << 16));
}

static __u32 banshee_calc_pll(int freq, int* freq_out) {
  int m, n, k, best_m, best_n, best_k, f_cur, best_error;
  int fref = 14318;
  
  /* this really could be done with more intelligence */
  best_error = freq;
  best_n = best_m = best_k = 0;
  for(n = 1; n < 256; n++) {
    for(m = 1; m < 64; m++) {
      for(k = 0; k < 4; k++) {
	f_cur = fref*(n + 2)/(m + 2)/(1 << k);
	if(abs(f_cur - freq) < best_error) {
	  best_error = abs(f_cur-freq);
	  best_n = n;
	  best_m = m;
	  best_k = k;
	}
      }
    }
  }
  n = best_n;
  m = best_m;
  k = best_k;
  *freq_out = fref*(n + 2)/(m + 2)/(1 << k);

  DPRINTK("freq = %d kHz, freq_out = %d kHz\n", freq, *freq_out);
  DPRINTK("N = %d, M = %d, K = %d\n", n, m, k);

  return (n << 8) | (m << 2) | k;
}

static void banshee_write_regs(struct banshee_reg* reg) {
  int i;

  banshee_wait_idle();

  tdfx_outl(MISCINIT1, tdfx_inl(MISCINIT1) | 0x01);

  crt_outb(0x11, crt_inb(0x11) & 0x7f); /* CRT unprotect */

  banshee_make_room(3);
  tdfx_outl(VGAINIT1,      reg->vgainit1 &  0x001FFFFF);
  tdfx_outl(VIDPROCCFG,    reg->vidcfg   & ~0x00000001);
#if 0
  tdfx_outl(PLLCTRL1,      reg->mempll);
  tdfx_outl(PLLCTRL2,      reg->gfxpll);
#endif
  tdfx_outl(PLLCTRL0,      reg->vidpll);

  vga_outb(MISC_W, reg->misc[0x00] | 0x01);

  for(i = 0; i < 5; i++)
    seq_outb(i, reg->seq[i]);

  for(i = 0; i < 25; i++)
    crt_outb(i, reg->crt[i]);

  for(i = 0; i < 9; i++)
    gra_outb(i, reg->gra[i]);

  for(i = 0; i < 21; i++)
    att_outb(i, reg->att[i]);

  crt_outb(0x1a, reg->ext[0]);
  crt_outb(0x1b, reg->ext[1]);

  vga_enable_palette();
  vga_enable_video();

  banshee_make_room(9);
  tdfx_outl(VGAINIT0,      reg->vgainit0);
  tdfx_outl(DACMODE,       reg->dacmode);
  tdfx_outl(VIDDESKSTRIDE, reg->stride);
  tdfx_outl(HWCURPATADDR,  reg->cursloc);
  tdfx_outl(VIDSCREENSIZE, reg->screensize);
  tdfx_outl(VIDDESKSTART,  reg->startaddr);
  tdfx_outl(VIDPROCCFG,    reg->vidcfg);
  tdfx_outl(VGAINIT1,      reg->vgainit1);  

  banshee_make_room(7);
  tdfx_outl(SRCBASE,         reg->srcbase);
  tdfx_outl(DSTBASE,         reg->dstbase);
  tdfx_outl(COMMANDEXTRA_2D, 0);
  tdfx_outl(CLIP0MIN,        0);
  tdfx_outl(CLIP0MAX,        0x0fff0fff);
  tdfx_outl(CLIP1MIN,        0);
  tdfx_outl(CLIP1MAX,        0x0fff0fff);

  banshee_wait_idle();
}

static unsigned long tdfx_lfb_size(void) {
  __u32 draminit0 = 0;
  __u32 draminit1 = 0;
  __u32 miscinit1 = 0;
  __u32 lfbsize   = 0;
  int sgram_p     = 0;

  if(!((fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE) ||
       (fb_info.dev == PCI_DEVICE_ID_3DFX_VOODOO3)))
    return 0;

  draminit0 = tdfx_inl(DRAMINIT0);  
  draminit1 = tdfx_inl(DRAMINIT1);

  sgram_p = (draminit1 & DRAMINIT1_MEM_SDRAM) ? 0 : 1;
  
  lfbsize = sgram_p ?
    (((draminit0 & DRAMINIT0_SGRAM_NUM)  ? 2 : 1) * 
     ((draminit0 & DRAMINIT0_SGRAM_TYPE) ? 8 : 4) * 1024 * 1024) :
    16 * 1024 * 1024;

  /* disable block writes for SDRAM (why?) */
  miscinit1 = tdfx_inl(MISCINIT1);
  miscinit1 |= sgram_p ? 0 : MISCINIT1_2DBLOCK_DIS;
  miscinit1 |= MISCINIT1_CLUT_INV;
  tdfx_outl(MISCINIT1, miscinit1);

  return lfbsize;
}

static void fbcon_banshee_bmove(struct display* p, 
				int sy, 
				int sx, 
				int dy,
				int dx, 
				int height, 
				int width) {
  banshee_bitblt(fontwidth(p)*sx,
		 fontheight(p)*sy, 
		 fontwidth(p)*dx,
		 fontheight(p)*dy, 
		 fontwidth(p)*width, 
		 fontheight(p)*height, 
		 fb_info.current_par.lpitch, 
		 fb_info.current_par.bpp);
}

static void fbcon_banshee_clear(struct vc_data* conp, 
				struct display* p, 
				int sy,
				int sx, 
				int height, 
				int width) {
  unsigned int bg;

  bg = attr_bgcol_ec(p,conp);
  banshee_fillrect(fontwidth(p)*sx,
		   fontheight(p)*sy,
		   fontwidth(p)*width, 
		   fontheight(p)*height,
		   bg, 
		   fb_info.current_par.lpitch, 
		   fb_info.current_par.bpp);
}

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_banshee8 = {
   fbcon_cfb8_setup, 
   fbcon_banshee_bmove, 
   fbcon_banshee_clear, 
   fbcon_cfb8_putc,
   fbcon_cfb8_putcs, 
   fbcon_cfb8_revc, 
   NULL, 
   NULL, 
   fbcon_cfb8_clear_margins,
   FONTWIDTH(8)
};
#endif
#ifdef FBCON_HAS_CFB16
static struct display_switch fbcon_banshee16 = {
   fbcon_cfb16_setup, 
   fbcon_banshee_bmove, 
   fbcon_banshee_clear, 
   fbcon_cfb16_putc,
   fbcon_cfb16_putcs, 
   fbcon_cfb16_revc, 
   NULL, 
   NULL, 
   fbcon_cfb16_clear_margins,
   FONTWIDTH(8)
};
#endif
#ifdef FBCON_HAS_CFB32
static struct display_switch fbcon_banshee32 = {
   fbcon_cfb32_setup, 
   fbcon_banshee_bmove, 
   fbcon_banshee_clear, 
   fbcon_cfb32_putc,
   fbcon_cfb32_putcs, 
   fbcon_cfb32_revc, 
   NULL, 
   NULL, 
   fbcon_cfb32_clear_margins,
   FONTWIDTH(8)
};
#endif

/* ------------------------------------------------------------------------- */

static void tdfxfb_set_par(const struct tdfxfb_par* par,
			   struct fb_info_tdfx*     info) {
  struct fb_info_tdfx* i = (struct fb_info_tdfx*)info;
  struct banshee_reg reg;
  __u32 cpp;
  __u32 hd, hs, he, ht, hbs, hbe;
  __u32 vd, vs, ve, vt, vbs, vbe;
  __u32 wd;
  int fout;
  int freq;

  memset(&reg, 0, sizeof(reg));

  cpp = (par->bpp + 7)/8;
  
  wd = (par->hdispend >> 3) - 1;

  hd  = (par->hdispend >> 3) - 1;
  hs  = (par->hsyncsta >> 3) - 1;
  he  = (par->hsyncend >> 3) - 1;
  ht  = (par->htotal   >> 3) - 1;
  hbs = hd;
  hbe = ht;

  vd  = par->vdispend - 1;
  vs  = par->vsyncsta - 1;
  ve  = par->vsyncend - 1;
  vt  = par->vtotal   - 2;
  vbs = vd;
  vbe = vt;
  
  /* this is all pretty standard VGA register stuffing */
  reg.misc[0x00] = 
    0x0f |
    (par->hdispend < 400 ? 0xa0 :
     par->hdispend < 480 ? 0x60 :
     par->hdispend < 768 ? 0xe0 : 0x20);
     
  reg.gra[0x00] = 0x00;
  reg.gra[0x01] = 0x00;
  reg.gra[0x02] = 0x00;
  reg.gra[0x03] = 0x00;
  reg.gra[0x04] = 0x00;
  reg.gra[0x05] = 0x40;
  reg.gra[0x06] = 0x05;
  reg.gra[0x07] = 0x0f;
  reg.gra[0x08] = 0xff;

  reg.att[0x00] = 0x00;
  reg.att[0x01] = 0x01;
  reg.att[0x02] = 0x02;
  reg.att[0x03] = 0x03;
  reg.att[0x04] = 0x04;
  reg.att[0x05] = 0x05;
  reg.att[0x06] = 0x06;
  reg.att[0x07] = 0x07;
  reg.att[0x08] = 0x08;
  reg.att[0x09] = 0x09;
  reg.att[0x0a] = 0x0a;
  reg.att[0x0b] = 0x0b;
  reg.att[0x0c] = 0x0c;
  reg.att[0x0d] = 0x0d;
  reg.att[0x0e] = 0x0e;
  reg.att[0x0f] = 0x0f;
  reg.att[0x10] = 0x41;
  reg.att[0x11] = 0x00;
  reg.att[0x12] = 0x0f;
  reg.att[0x13] = 0x00;
  reg.att[0x14] = 0x00;

  reg.seq[0x00] = 0x03;
  reg.seq[0x01] = 0x01; /* fixme: clkdiv2? */
  reg.seq[0x02] = 0x0f;
  reg.seq[0x03] = 0x00;
  reg.seq[0x04] = 0x0e;

  reg.crt[0x00] = ht - 4;
  reg.crt[0x01] = hd;
  reg.crt[0x02] = hbs;
  reg.crt[0x03] = 0x80 | (hbe & 0x1f);
  reg.crt[0x04] = hs;
  reg.crt[0x05] = 
    ((hbe & 0x20) << 2) | 
    (he & 0x1f);
  reg.crt[0x06] = vt;
  reg.crt[0x07] = 
    ((vs & 0x200) >> 2) |
    ((vd & 0x200) >> 3) |
    ((vt & 0x200) >> 4) |
    0x10 |
    ((vbs & 0x100) >> 5) |
    ((vs  & 0x100) >> 6) |
    ((vd  & 0x100) >> 7) |
    ((vt  & 0x100) >> 8);
  reg.crt[0x08] = 0x00;
  reg.crt[0x09] = 
    0x40 |
    ((vbs & 0x200) >> 4);
  reg.crt[0x0a] = 0x00;
  reg.crt[0x0b] = 0x00;
  reg.crt[0x0c] = 0x00;
  reg.crt[0x0d] = 0x00;
  reg.crt[0x0e] = 0x00;
  reg.crt[0x0f] = 0x00;
  reg.crt[0x10] = vs;
  reg.crt[0x11] = 
    (ve & 0x0f) |
    0x20;
  reg.crt[0x12] = vd;
  reg.crt[0x13] = wd;
  reg.crt[0x14] = 0x00;
  reg.crt[0x15] = vbs;
  reg.crt[0x16] = vbe + 1; 
  reg.crt[0x17] = 0xc3;
  reg.crt[0x18] = 0xff;
  
  /* Banshee's nonvga stuff */
  reg.ext[0x00] = (((ht  & 0x100) >> 8) | 
		   ((hd  & 0x100) >> 6) |
		   ((hbs & 0x100) >> 4) |
		   ((hbe &  0x40) >> 1) |
		   ((hs  & 0x100) >> 2) |
		   ((he  &  0x20) << 2)); 
  reg.ext[0x01] = (((vt  & 0x400) >> 10) |
		   ((vd  & 0x400) >>  8) | 
		   ((vbs & 0x400) >>  6) |
		   ((vbe & 0x400) >>  4));
  
  reg.vgainit0 = 
    VGAINIT0_8BIT_DAC     |
    VGAINIT0_EXT_ENABLE   |
    VGAINIT0_WAKEUP_3C3   |
    VGAINIT0_ALT_READBACK |
    VGAINIT0_EXTSHIFTOUT;
  reg.vgainit1 = tdfx_inl(VGAINIT1) & 0x1fffff;

  reg.vidcfg = 
    VIDCFG_VIDPROC_ENABLE |
    VIDCFG_DESK_ENABLE    |
    ((cpp - 1) << VIDCFG_PIXFMT_SHIFT) |
    (cpp != 1 ? VIDCFG_CLUT_BYPASS : 0);
  reg.stride    = par->width*cpp;
  reg.cursloc   = 0;

  reg.startaddr = par->baseline*reg.stride;
  reg.srcbase   = reg.startaddr;
  reg.dstbase   = reg.startaddr;

  /* PLL settings */
  freq = par->pixclock;

  reg.dacmode &= ~DACMODE_2X;
  reg.vidcfg  &= ~VIDCFG_2X;
  if(freq > i->max_pixclock/2) {
    freq = freq > i->max_pixclock ? i->max_pixclock : freq;
    reg.dacmode |= DACMODE_2X;
    reg.vidcfg  |= VIDCFG_2X;
  }
  reg.vidpll = banshee_calc_pll(freq, &fout);
#if 0
  reg.mempll = banshee_calc_pll(..., &fout);
  reg.gfxpll = banshee_calc_pll(..., &fout);
#endif

  reg.screensize = par->width | (par->height << 12);
  reg.vidcfg &= ~VIDCFG_HALF_MODE;

  banshee_write_regs(&reg);

  i->current_par = *par;
}

static int tdfxfb_decode_var(const struct fb_var_screeninfo* var,
			     struct tdfxfb_par*              par,
			     const struct fb_info_tdfx*      info) {
  struct fb_info_tdfx* i = (struct fb_info_tdfx*)info;

  if(var->bits_per_pixel != 8  &&
     var->bits_per_pixel != 16 &&
     var->bits_per_pixel != 32) {
    DPRINTK("depth not supported: %u\n", var->bits_per_pixel);
    return -EINVAL;
  }

  if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
    DPRINTK("interlace not supported\n");
    return -EINVAL;
  }

  if(var->xoffset) {
    DPRINTK("xoffset not supported\n");
    return -EINVAL;
  }

  if(var->xres != var->xres_virtual) {
    DPRINTK("virtual x resolution != physical x resolution not supported\n");
    return -EINVAL;
  }

  if(nopan && nowrap) {
    if(var->yres != var->yres_virtual) {
      DPRINTK("virtual y resolution != physical y resolution not supported\n");
      return -EINVAL;
    }
  } else {
    if(var->yres > var->yres_virtual) {
      DPRINTK("virtual y resolution < physical y resolution not possible\n");
      return -EINVAL;
    }
  }

  if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
    DPRINTK("interlace not supported\n");
    return -EINVAL;
  }

  memset(par, 0, sizeof(struct tdfxfb_par));

  switch(i->dev) {
  case PCI_DEVICE_ID_3DFX_BANSHEE:
  case PCI_DEVICE_ID_3DFX_VOODOO3:
    par->width       = (var->xres + 15) & ~15; /* could sometimes be 8 */
    par->width_virt  = par->width;
    par->height      = var->yres;
    par->height_virt = var->yres_virtual;
    par->bpp         = var->bits_per_pixel;
    par->ppitch      = var->bits_per_pixel;
    par->lpitch      = par->width*par->ppitch/8;

    par->baseline = 0;

    if(par->width < 320 || par->width > 2048) {
      DPRINTK("width not supported: %u\n", par->width);
      return -EINVAL;
    }
    if(par->height < 200 || par->height > 2048) {
      DPRINTK("height not supported: %u\n", par->height);
      return -EINVAL;
    }
    if(par->lpitch*par->height_virt > i->bufbase_size) {
      DPRINTK("no memory for screen (%ux%ux%u)\n",
	      par->width, par->height_virt, par->bpp);
      return -EINVAL;
    }
    par->pixclock = PICOS2KHZ(var->pixclock);
    if(par->pixclock > i->max_pixclock) {
      DPRINTK("pixclock too high (%uKHz)\n", par->pixclock);
      return -EINVAL;
    }

    par->hdispend = var->xres;
    par->hsyncsta = par->hdispend + var->right_margin;
    par->hsyncend = par->hsyncsta + var->hsync_len;
    par->htotal   = par->hsyncend + var->left_margin;

    par->vdispend = var->yres;
    par->vsyncsta = par->vdispend + var->lower_margin;
    par->vsyncend = par->vsyncsta + var->vsync_len;
    par->vtotal   = par->vsyncend + var->upper_margin;

    if(var->sync & FB_SYNC_HOR_HIGH_ACT)
      par->video |= TDFXF_HSYNC_ACT_HIGH;
    else
      par->video |= TDFXF_HSYNC_ACT_LOW;
    if(var->sync & FB_SYNC_VERT_HIGH_ACT)
      par->video |= TDFXF_VSYNC_ACT_HIGH;
    else
      par->video |= TDFXF_VSYNC_ACT_LOW;
    if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
      par->video |= TDFXF_LINE_DOUBLE;
    if(var->activate == FB_ACTIVATE_NOW)
      par->video |= TDFXF_VIDEO_ENABLE;
  }

  if(var->accel_flags & FB_ACCELF_TEXT)
    par->accel_flags = FB_ACCELF_TEXT;
  else
    par->accel_flags = 0;

  return 0;
}

static int tdfxfb_encode_var(struct fb_var_screeninfo* var,
			    const struct tdfxfb_par* par,
			    const struct fb_info_tdfx* info) {
  struct fb_var_screeninfo v;

  memset(&v, 0, sizeof(struct fb_var_screeninfo));
  v.xres_virtual   = par->width_virt;
  v.yres_virtual   = par->height_virt;
  v.xres           = par->width;
  v.yres           = par->height;
  v.right_margin   = par->hsyncsta - par->hdispend;
  v.hsync_len      = par->hsyncend - par->hsyncsta;
  v.left_margin    = par->htotal   - par->hsyncend;
  v.lower_margin   = par->vsyncsta - par->vdispend;
  v.vsync_len      = par->vsyncend - par->vsyncsta;
  v.upper_margin   = par->vtotal   - par->vsyncend;
  v.bits_per_pixel = par->bpp;
  switch(par->bpp) {
  case 8:
    v.red.length = v.green.length = v.blue.length = 8;
    break;
  case 16:
    v.red.offset   = 11;
    v.red.length   = 5;
    v.green.offset = 5;
    v.green.length = 6;
    v.blue.offset  = 0;
    v.blue.length  = 5;
    break;
  case 32:
    v.red.offset   = 16;
    v.green.offset = 8;
    v.blue.offset  = 0;
    v.red.length = v.green.length = v.blue.length = 8;
    break;
  }
  v.height = v.width = -1;
  v.pixclock = KHZ2PICOS(par->pixclock);
  if((par->video & TDFXF_HSYNC_MASK) == TDFXF_HSYNC_ACT_HIGH)
    v.sync |= FB_SYNC_HOR_HIGH_ACT;
  if((par->video & TDFXF_VSYNC_MASK) == TDFXF_VSYNC_ACT_HIGH)
    v.sync |= FB_SYNC_VERT_HIGH_ACT;
  if(par->video & TDFXF_LINE_DOUBLE)
    v.vmode = FB_VMODE_DOUBLE;
  *var = v;
  return 0;
}

static int tdfxfb_open(struct fb_info* info, 
		       int user) {
  MOD_INC_USE_COUNT;
  return(0);
}

static int tdfxfb_release(struct fb_info* info, 
			  int user) {
  MOD_DEC_USE_COUNT;
  return(0);
}


static int tdfxfb_encode_fix(struct fb_fix_screeninfo*  fix,
			     const struct tdfxfb_par*   par,
			     const struct fb_info_tdfx* info) {
  memset(fix, 0, sizeof(struct fb_fix_screeninfo));

  switch(info->dev) {
  case PCI_DEVICE_ID_3DFX_BANSHEE:
  case PCI_DEVICE_ID_3DFX_VOODOO3:
	if (info->dev == PCI_DEVICE_ID_3DFX_BANSHEE)
      strcpy(fix->id, "3Dfx Banshee");
	else
      strcpy(fix->id, "3Dfx Voodoo3");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
    fix->smem_start  = (char*)info->bufbase_phys;
    fix->smem_len    = info->bufbase_size;
    fix->mmio_start  = (char*)info->regbase_phys;
    fix->mmio_len    = info->regbase_size;
#else
    fix->smem_start  = info->bufbase_phys;
    fix->smem_len    = info->bufbase_size;
    fix->mmio_start  = info->regbase_phys;
    fix->mmio_len    = info->regbase_size;
#endif
    fix->accel       = FB_ACCEL_3DFX_BANSHEE;
    fix->type        = FB_TYPE_PACKED_PIXELS;
    fix->type_aux    = 0;
    fix->line_length = par->lpitch;
    fix->visual      = par->bpp == 8 
      ? FB_VISUAL_PSEUDOCOLOR
      : FB_VISUAL_DIRECTCOLOR;

    fix->xpanstep    = 0; 
    fix->ypanstep    = (nowrap && nopan) ? 0 : 1;
    fix->ywrapstep   = nowrap ? 0 : 1;

    break;
  default:
    return -EINVAL;
  }

  return 0;
}

static int tdfxfb_get_fix(struct fb_fix_screeninfo *fix, 
			  int con,
			  struct fb_info *fb) {
  const struct fb_info_tdfx *info = (struct fb_info_tdfx*)fb;
  struct tdfxfb_par par;

  if(con == -1)
    par = info->default_par;
  else
    tdfxfb_decode_var(&fb_display[con].var, &par, info);
  tdfxfb_encode_fix(fix, &par, info);
  return 0;
}

static int tdfxfb_get_var(struct fb_var_screeninfo *var, 
			  int con,
			  struct fb_info *fb) {
  const struct fb_info_tdfx *info = (struct fb_info_tdfx*)fb;

  if(con == -1)
    tdfxfb_encode_var(var, &info->default_par, info);
  else
    *var = fb_display[con].var;
  return 0;
}

static void tdfxfb_set_disp(struct display *disp, 
			    struct fb_info_tdfx *info,
			    int bpp, 
			    int accel) {
  DPRINTK("actually, %s using acceleration!\n", 
	  noaccel ? "NOT" : "");

  switch(bpp) {
#ifdef FBCON_HAS_CFB8
  case 8:
    info->dispsw = noaccel ? fbcon_cfb8 : fbcon_banshee8;
    disp->dispsw = &info->dispsw;
    break;
#endif
#ifdef FBCON_HAS_CFB16
  case 16:
    info->dispsw = noaccel ? fbcon_cfb16 : fbcon_banshee16;
    disp->dispsw = &info->dispsw;
    disp->dispsw_data = info->fbcon_cmap.cfb16;
    break;
#endif
#ifdef FBCON_HAS_CFB32
  case 32:
    info->dispsw = noaccel ? fbcon_cfb32 : fbcon_banshee32;
    disp->dispsw = &info->dispsw;
    disp->dispsw_data = info->fbcon_cmap.cfb32;
    break;
#endif
  default:
    info->dispsw = fbcon_dummy;
    disp->dispsw = &info->dispsw;
  }
}

static int tdfxfb_set_var(struct fb_var_screeninfo *var, 
			  int con,
			  struct fb_info *fb) {
  struct fb_info_tdfx *info = (struct fb_info_tdfx*)fb;
  struct tdfxfb_par par;
  struct display *display;
  int oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel, accel, err;
  int activate = var->activate;

  if(con >= 0)
    display = &fb_display[con];
  else
    display = fb->disp;	/* used during initialization */

  if((err = tdfxfb_decode_var(var, &par, info)))
    return err;

  tdfxfb_encode_var(var, &par, info);

  if((activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
    oldxres  = display->var.xres;
    oldyres  = display->var.yres;
    oldvxres = display->var.xres_virtual;
    oldvyres = display->var.yres_virtual;
    oldbpp   = display->var.bits_per_pixel;
    oldaccel = display->var.accel_flags;
    display->var = *var;
    if(con < 0                         ||
       oldxres  != var->xres           || 
       oldyres  != var->yres           ||
       oldvxres != var->xres_virtual   || 
       oldvyres != var->yres_virtual   ||
       oldbpp   != var->bits_per_pixel || 
       oldaccel != var->accel_flags) {
      struct fb_fix_screeninfo fix;

      tdfxfb_encode_fix(&fix, &par, info);
      display->screen_base    = (char *)info->bufbase_virt;
      display->visual         = fix.visual;
      display->type           = fix.type;
      display->type_aux       = fix.type_aux;
      display->ypanstep       = fix.ypanstep;
      display->ywrapstep      = fix.ywrapstep;
      display->line_length    = fix.line_length;
      display->next_line      = fix.line_length;
      display->can_soft_blank = 1;
      display->inverse        = inverse;
      accel = var->accel_flags & FB_ACCELF_TEXT;
      tdfxfb_set_disp(display, info, par.bpp, accel);

      if(nopan && nowrap) {
	display->scrollmode = SCROLL_YREDRAW;
#ifdef FBCON_HAS_CFB8
	fbcon_banshee8.bmove = fbcon_redraw_bmove;
#endif
#ifdef FBCON_HAS_CFB16
	fbcon_banshee16.bmove = fbcon_redraw_bmove;
#endif
#ifdef FBCON_HAS_CFB32
	fbcon_banshee32.bmove = fbcon_redraw_bmove;
#endif
      }
      if (info->fb_info.changevar)
	(*info->fb_info.changevar)(con);
    }
    if(!info->fb_info.display_fg ||
       info->fb_info.display_fg->vc_num == con ||
       con < 0)
      tdfxfb_set_par(&par, info);
    if(oldbpp != var->bits_per_pixel || con < 0) {
      if((err = fb_alloc_cmap(&display->cmap, 0, 0)))
	return err;
      tdfxfb_install_cmap(con, &info->fb_info);
    }
  }
  
  return 0;
}

static int tdfxfb_pan_display(struct fb_var_screeninfo* var, 
			      int con,
			      struct fb_info* fb) {
  struct fb_info_tdfx* i = (struct fb_info_tdfx*)fb;
  __u32 addr;

  if(nowrap && nopan) {
    return -EINVAL;
  } else {
    if(var->xoffset) 
      return -EINVAL;
    if(var->yoffset < 0) 
      return -EINVAL;
    if(nopan && var->yoffset > var->yres_virtual) 
      return -EINVAL;
    if(nowrap && var->yoffset + var->yres > var->yres_virtual) 
      return -EINVAL;
    
    i->current_par.baseline = var->yoffset;
    
    addr = var->yoffset*i->current_par.lpitch;
    tdfx_outl(VIDDESKSTART, addr);
    tdfx_outl(SRCBASE,      addr);
    tdfx_outl(DSTBASE,      addr);
    return 0;
  }
}

static int tdfxfb_get_cmap(struct fb_cmap *cmap, 
			   int kspc, 
			   int con,
			   struct fb_info *fb) {
  if(!fb->display_fg || con == fb->display_fg->vc_num) {
    /* current console? */
    return fb_get_cmap(cmap, kspc, tdfxfb_getcolreg, fb);
  } else if(fb_display[con].cmap.len) {
    /* non default colormap? */
    fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
  } else {
    int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
    fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
  }
  return 0;
}

static int tdfxfb_set_cmap(struct fb_cmap *cmap, 
			   int kspc, 
			   int con,
			   struct fb_info *fb) {
  int err;
  struct display *disp;

  if(con >= 0)
    disp = &fb_display[con];
  else
    disp = fb->disp;
  if(!disp->cmap.len) {	/* no colormap allocated? */
    int size = disp->var.bits_per_pixel == 16 ? 32 : 256;
    if((err = fb_alloc_cmap(&disp->cmap, size, 0)))
      return err;
  }
  if(!fb->display_fg || con == fb->display_fg->vc_num) {
    /* current console? */
    return fb_set_cmap(cmap, kspc, tdfxfb_setcolreg, fb);
  } else {
    fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
  }
  return 0;
}

static int tdfxfb_ioctl(struct inode *inode, 
			struct file *file, 
			u_int cmd,
			u_long arg, 
			int con, 
			struct fb_info *fb) {
  return -EINVAL;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
__initfunc(void tdfxfb_init(void)) {
#else
int __init tdfxfb_init(void) {
#endif
  struct pci_dev *pdev = NULL;
  struct fb_var_screeninfo var;
  int j, k;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
  if(!pcibios_present()) return;
#else
  if(!pcibios_present()) return -ENXIO;
#endif

  for(pdev = pci_devices; pdev; pdev = pdev->next) {
    if(((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) &&
       (pdev->vendor == PCI_VENDOR_ID_3DFX) &&
       ((pdev->device == PCI_DEVICE_ID_3DFX_BANSHEE) ||
	(pdev->device == PCI_DEVICE_ID_3DFX_VOODOO3))) {
      
      fb_info.dev   = pdev->device;
      fb_info.max_pixclock = 
	pdev->device == PCI_DEVICE_ID_3DFX_BANSHEE 
	? BANSHEE_MAX_PIXCLOCK
	: VOODOO3_MAX_PIXCLOCK;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
      fb_info.regbase_phys = pdev->base_address[0] & PCI_BASE_ADDRESS_MEM_MASK;
      fb_info.regbase_size = 1 << 25;
      fb_info.regbase_virt = 
	(__u32)ioremap_nocache(fb_info.regbase_phys, 1 << 25);
      if(!fb_info.regbase_virt) {
	if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
	  printk("fb: Can't remap Banshee register area.\n");
	else
	  printk("fb: Can't remap Voodoo3 register area.\n");
	return;
      }

      fb_info.bufbase_phys = pdev->base_address[1] & PCI_BASE_ADDRESS_MEM_MASK;
      if(!(fb_info.bufbase_size = tdfx_lfb_size())) {
	if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
	  printk("fb: Can't count Banshee memory.\n");
	else
	  printk("fb: Can't count Voodoo3 memory.\n");
	iounmap((void*)fb_info.regbase_virt);
	return;
      }
      fb_info.bufbase_virt    = 
	(__u32)ioremap_nocache(fb_info.bufbase_phys, 1 << 25);
      if(!fb_info.regbase_virt) {
	if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
      printk("fb: Can't remap Banshee framebuffer.\n");
	else
      printk("fb: Can't remap Voodoo3 framebuffer.\n");
	iounmap((void*)fb_info.regbase_virt);
	return;
      }
#else
      fb_info.regbase_phys = pdev->resource[0].start;
      fb_info.regbase_size = 1 << 25;
      fb_info.regbase_virt = 
	(__u32)ioremap_nocache(fb_info.regbase_phys, 1 << 25);
      if(!fb_info.regbase_virt) {
	if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
	  printk("fb: Can't remap Banshee register area.\n");
	else
	  printk("fb: Can't remap Voodoo3 register area.\n");
	return -ENXIO;
      }
      
      fb_info.bufbase_phys = pdev->resource[1].start;
      if(!(fb_info.bufbase_size = tdfx_lfb_size())) {
	iounmap((void*)fb_info.regbase_virt);
	if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
	  printk("fb: Can't count Banshee memory.\n");
	else
	  printk("fb: Can't count Voodoo3 memory.\n");
	return -ENXIO;
      }
      fb_info.bufbase_virt    = 
	(__u32)ioremap_nocache(fb_info.bufbase_phys, 1 << 25);
      if(!fb_info.regbase_virt) {
	if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
	  printk("fb: Can't remap Banshee framebuffer.\n");
	else
	  printk("fb: Can't remap Voodoo3 framebuffer.\n");
	iounmap((void*)fb_info.regbase_virt);
	return -ENXIO;
      }
#endif
      
	if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
      printk("fb: Banshee memory = %ldK\n", fb_info.bufbase_size >> 10);
	else
      printk("fb: Voodoo3 memory = %ldK\n", fb_info.bufbase_size >> 10);
      
      /* clear framebuffer memory */
      memset_io(fb_info.bufbase_virt, 0, fb_info.bufbase_size);
      
	  if (fb_info.dev == PCI_DEVICE_ID_3DFX_BANSHEE)
        strcpy(fb_info.fb_info.modename, "3Dfx Banshee");
	  else
        strcpy(fb_info.fb_info.modename, "3Dfx Voodoo3");
      fb_info.fb_info.changevar  = NULL;
      fb_info.fb_info.node       = -1;
      fb_info.fb_info.fbops      = &tdfxfb_ops;
      fb_info.fb_info.disp       = &fb_info.disp;
      strcpy(fb_info.fb_info.fontname, fontname);
      fb_info.fb_info.switch_con = &tdfxfb_switch_con;
      fb_info.fb_info.updatevar  = &tdfxfb_updatevar;
      fb_info.fb_info.blank      = &tdfxfb_blank;
      fb_info.fb_info.flags      = FBINFO_FLAG_DEFAULT;
      
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
      var = default_mode[default_mode_index < modes
			? default_mode_index
			: 0].var;
#else
      memset(&var, 0, sizeof(var));
      if(!mode_option || 
	 !fb_find_mode(&var, &fb_info.fb_info, mode_option, NULL, 0, NULL, 8))
	var = default_mode[0].var;
#endif
      
      if(noaccel) var.accel_flags &= ~FB_ACCELF_TEXT;
      else var.accel_flags |= FB_ACCELF_TEXT;
      
      if(tdfxfb_decode_var(&var, &fb_info.default_par, &fb_info)) {
	/* ugh -- can't use the mode from the mode db. (or command line),
	   so try the default */

	printk("tdfxfb: "
	       "can't decode the supplied video mode, using default\n");

	var = default_mode[0].var;
	if(noaccel) var.accel_flags &= ~FB_ACCELF_TEXT;
	else var.accel_flags |= FB_ACCELF_TEXT;
      
	if(tdfxfb_decode_var(&var, &fb_info.default_par, &fb_info)) {
	  printk("tdfxfb: can't decode default video mode\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
	  return;
#else
	  return -ENXIO;
#endif
	}
      }
      
      fb_info.disp.screen_base    = (void*)fb_info.bufbase_virt;
      fb_info.disp.visual         = 
	var.bits_per_pixel == 8 
	? FB_VISUAL_PSEUDOCOLOR
	: FB_VISUAL_DIRECTCOLOR;
      fb_info.disp.type           = FB_TYPE_PACKED_PIXELS;
      fb_info.disp.type_aux       = 0;
      
      fb_info.disp.ypanstep       = (nowrap && nopan) ? 0 : 1;
      fb_info.disp.ywrapstep      = nowrap ? 0 : 1;
      
      fb_info.disp.line_length    = 
	fb_info.disp.next_line    = 	  
	var.xres*(var.bits_per_pixel + 7)/8;
      fb_info.disp.can_soft_blank = 1;
      fb_info.disp.inverse        = inverse;
      fb_info.disp.scrollmode     = SCROLL_YREDRAW;
      fb_info.disp.var            = var;
      tdfxfb_set_disp(&fb_info.disp, &fb_info, 
		      var.bits_per_pixel, 
		      0);
      
      for(j = 0; j < 16; j++) {
	k = color_table[j];
	fb_info.palette[j].red   = default_red[k];
	fb_info.palette[j].green = default_grn[k];
	fb_info.palette[j].blue  = default_blu[k];
      }
      
      if(tdfxfb_set_var(&var, -1, &fb_info.fb_info)) {
	printk("tdfxfb: can't set default video mode\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
	return;
#else
	return -ENXIO;
#endif
      }
      
      if(register_framebuffer(&fb_info.fb_info) < 0) {
	printk("tdfxfb: can't register framebuffer\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
	return;
#else
	return -ENXIO;
#endif
      }

      printk("fb%d: %s frame buffer device\n", 
	     GET_FB_IDX(fb_info.fb_info.node),
	     fb_info.fb_info.modename);
      
      MOD_INC_USE_COUNT;
      
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
      return;
#else
      return 0;
#endif
    }
  }

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
  return;
#else
  return -ENXIO;
#endif
}

void tdfxfb_setup(char *options, 
		  int *ints) {
  char* this_opt;

  if(!options || !*options)
    return;

  for(this_opt = strtok(options, ","); 
      this_opt;
      this_opt = strtok(NULL, ",")) {
    if(!strcmp(this_opt, "inverse")) {
      inverse = 1;
      fb_invert_cmaps();
    } else if(!strcmp(this_opt, "noaccel")) {
      noaccel = 1;
    } else if(!strcmp(this_opt, "nopan")) {
      nopan = 1;
    } else if(!strcmp(this_opt, "nowrap")) {
      nowrap = 1;
    } else if (!strncmp(this_opt, "font:", 5)) {
      strncpy(fontname, this_opt + 5, 40);
    } else {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
      int i;
      for(i = 0; i < modes; i++) {
	if(!strcmp(this_opt, default_mode[i].name)) {
	  default_mode_index = i;
	}
      }
#else
      mode_option = this_opt;
#endif
    }
  } 
}

static int tdfxfb_switch_con(int con, 
			     struct fb_info *fb) {
  struct fb_info_tdfx *info = (struct fb_info_tdfx*)fb;
  struct tdfxfb_par par;

  /* Do we have to save the colormap? */
  if(fb_display[currcon].cmap.len)
    fb_get_cmap(&fb_display[currcon].cmap, 1, tdfxfb_getcolreg, fb);

  currcon = con;

  tdfxfb_decode_var(&fb_display[con].var, &par, info);
  tdfxfb_set_par(&par, info);
  tdfxfb_set_disp(&fb_display[con], 
		  info, 
		  par.bpp,
		  par.accel_flags & FB_ACCELF_TEXT);

  tdfxfb_install_cmap(con, fb);
  tdfxfb_updatevar(con, fb);

  return 1;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static void tdfxfb_blank(int blank, 
			 struct fb_info *fb) {
  u32 dacmode, state = 0, vgablank = 0;

  dacmode = tdfx_inl(DACMODE);

  switch(blank) {
  case 0: /* Screen: On; HSync: On, VSync: On */    
    state    = 0;
    vgablank = 0;
    break;
  case 1: /* Screen: Off; HSync: On, VSync: On */
    state    = 0;
    vgablank = 1;
    break;
  case 2: /* Screen: Off; HSync: On, VSync: Off */
    state    = BIT(3);
    vgablank = 1;
    break;
  case 3: /* Screen: Off; HSync: Off, VSync: On */
    state    = BIT(1);
    vgablank = 1;
    break;
  case 4: /* Screen: Off; HSync: Off, VSync: Off */
    state    = BIT(1) | BIT(3);
    vgablank = 1;
    break;
  }

  dacmode &= ~(BIT(1) | BIT(3));
  dacmode |= state;
  tdfx_outl(DACMODE, dacmode);
  if(vgablank) 
    vga_disable_video();
  else
    vga_enable_video();

  return;
}

static int  tdfxfb_updatevar(int con, 
			     struct fb_info* fb) {
  if(con != currcon || (nowrap && nopan)) { 
    return 0;
  } else {
    struct fb_var_screeninfo* var = &fb_display[currcon].var;
    return tdfxfb_pan_display(var, con, fb);
  }
}

static int tdfxfb_getcolreg(unsigned        regno, 
			    unsigned*       red, 
			    unsigned*       green,
			    unsigned*       blue, 
			    unsigned*       transp,
			    struct fb_info* fb) {
  struct fb_info_tdfx* i = (struct fb_info_tdfx*)fb;

  if(regno < 256) {
    *red    = i->palette[regno].red   << 8 | i->palette[regno].red;
    *green  = i->palette[regno].green << 8 | i->palette[regno].green;
    *blue   = i->palette[regno].blue  << 8 | i->palette[regno].blue;
    *transp = 0;
  }
  return regno > 255;
}

static int tdfxfb_setcolreg(unsigned        regno, 
			    unsigned        red, 
			    unsigned        green,
			    unsigned        blue, 
			    unsigned        transp,
			    struct fb_info* info) {
  struct fb_info_tdfx* i = (struct fb_info_tdfx*)info;

  if(regno < 16) {
    switch(i->current_par.bpp) {
#ifdef FBCON_HAS_CFB8
    case 8:
      break;
#endif
#ifdef FBCON_HAS_CFB16
    case 16:
      i->fbcon_cmap.cfb16[regno] =
	(((u32)red   & 0xf800) >> 0) |
	(((u32)green & 0xfc00) >> 5) |
	(((u32)blue  & 0xf800) >> 11);
      break;
#endif
#ifdef FBCON_HAS_CFB32
    case 32:
      i->fbcon_cmap.cfb32[regno] =
	(((u32)red   & 0xff00) << 8) |
	(((u32)green & 0xff00) << 0) |
	(((u32)blue  & 0xff00) >> 8);
      break;
#endif
    default:
      DPRINTK("bad depth %u\n", i->current_par.bpp);
      break;
    }
  }
  if(regno < 256) {
    i->palette[regno].red    = red   >> 8;
    i->palette[regno].green  = green >> 8;
    i->palette[regno].blue   = blue  >> 8;
    if(i->current_par.bpp == 8) {
      vga_outb(DAC_IW, (unsigned char)regno);
      vga_outb(DAC_D,  (unsigned char)(red   >> 8));
      vga_outb(DAC_D,  (unsigned char)(green >> 8));
      vga_outb(DAC_D,  (unsigned char)(blue  >> 8));
    }
  }
  return regno > 255;
}

static void tdfxfb_install_cmap(int             con, 
				struct fb_info* info) {
  if(con != currcon) return;
  if(fb_display[con].cmap.len) {
    fb_set_cmap(&fb_display[con].cmap, 1, tdfxfb_setcolreg, info);
  } else {
    int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
    fb_set_cmap(fb_default_cmap(size), 1, tdfxfb_setcolreg, info);
  }
}

