/*
 * imstt.c: Console support for PowerMac "imstt" display adaptor.
 *
 * Copyright (C) 1997 Sigurdur Asgeirsson
 *	
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/vc_ioctl.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/nvram.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <linux/selection.h>
#include <linux/vt_kern.h>
#include "pmac-cons.h"
#include "imstt.h"

enum
{
  S1SA		=  0, /* 0x00 */
  S2SA		=  1, /* 0x04 */
  SP		=  2, /* 0x08 */
  DSA		=  3, /* 0x0C */
  CNT		=  4, /* 0x10 */
  DP_OCTRL	=  5, /* 0x14 */
  BLTCTL	= 10, /* 0x28 */
		
  //	Scan Timing Generator Registers
  HES		= 12, /* 0x30 */
  HEB		= 13, /* 0x34 */
  HSB		= 14, /* 0x38 */
  HT		= 15, /* 0x3C */
  VES		= 16, /* 0x40 */
  VEB		= 17, /* 0x44 */
  VSB		= 18, /* 0x48 */
  VT		= 19, /* 0x4C */
  HCIV		= 20, /* 0x50 */
  VCIV		= 21, /* 0x54 */
  TCDR		= 22, /* 0x58 */
  VIL		= 23, /* 0x5C */
  STGCTL	= 24, /* 0x60 */
	
  //	Screen Refresh Generator Registers
  SSR		= 25, /* 0x64 */
  HRIR		= 26, /* 0x68 */
  SPR		= 27, /* 0x6C */
  CMR		= 28, /* 0x70 */
  SRGCTL	= 29, /* 0x74 */
		
  //	RAM Refresh Generator Registers
  RRCIV		= 30, /* 0x78 */
  RRSC		= 31, /* 0x7C */
  RRCR		= 34, /* 0x88 */
		
  //	System Registers
  GIOE		= 32, /* 0x80 */
  GIO		= 33, /* 0x84 */
  SCR		= 35, /* 0x8C */
  SSTATUS	= 36, /* 0x90 */
  PRC		= 37, /* 0x94 */

#if 0	
  //	PCI Registers
  DVID		= 0x00000000L,
  SC		= 0x00000004L,
  CCR		= 0x00000008L,
  OG		= 0x0000000CL,
  BARM		= 0x00000010L,
  BARER		= 0x00000030L,
#endif
};

enum
{
 PADDRW		= 0x00,
 PDATA		= 0x04,
 PPMASK		= 0x08,
 PADDRR		= 0x0C,
 PIDXLO		= 0x10,	
 PIDXHI		= 0x14,	
 PIDXDATA	= 0x18,
 PIDXCTL	= 0x1C,

 PPIXREP	= 0x0A,
 PM0		= 0x20,
 PN0		= 0x21,
 PP0		= 0x22,
 PC0		= 0x23
};

struct initvalues
{
  unsigned char	addr, value;
};

static struct initvalues initregs[] = 
{
 { 0x02, 0x21 },	/* (0x01) Miscellaneous Clock Control */
 { 0x03, 0x00 },	/* (0x00) Sync Control */
 { 0x04, 0x00 },	/* (0x00) Horizontal Sync Position */
 { 0x05, 0x00 },	/* (0x00) Power Management */
 { 0x06, 0x0B },	/* (0x02) DAC Operation */
 { 0x07, 0x00 },	/* (0x00) Palette Control */
 { 0x08, 0x01 },	/* (0x01) System Clock Control */
 { 0x0B, 0x00 },	/* (U) 8 BPP Control */
 { 0x0C, 0xC4 },	/* (U) 16 BPP Control */
 { 0x0D, 0x00 },	/* (U) 24 BPP Packed Control */
 { 0x0E, 0x03 },	/* (U) 32 BPP Control */
 { 0x10, 0x05 },	/* (0x00) Pixel PLL Control 1 */
 { 0x11, 0x00 },	/* (0x00) Pixel PLL Control 2 */
 { 0x15, 0x08 },	/* (0x08) SYSCLK N (System PLL Reference Divider) */
 { 0x16, 0x57 },	/* (0x41) SYSCLK M (System PLL VCO Divider) */
 { 0x17, 0x00 },	/* (U) SYSCLK P */
 { 0x18, 0x00 },	/* (U) SYSCLK C */
 { 0x30, 0x00 },	/* (0x00) Cursor Control */
 { 0x60, 0xFF },	/* (U) Border Color Red */
 { 0x61, 0xFF },	/* (U) Border Color Green */
 { 0x62, 0xFF },	/* (U) Border Color Blue */
 { 0x70, 0x01 },	/* (0x00) Miscellaneous Control 1 */
 { 0x71, 0x45 },	/* (0x00) Miscellaneous Control 2 */
 { 0x72, 0x00 },	/* (0x00) Miscellaneous Control 3 */
 { 0x78, 0x00 },	/* (0x00) Key Control/DB Operation */
};

static void set_imstt_clock(unsigned char *params);
static int read_imstt_sense(void);
static int imstt_vram_reqd(int vmode, int cmode);

static int total_vram = 2 * 1024 * 1024;		/* total amount of video memory, bytes */
static unsigned char *frame_buffer;
static unsigned char *cmap_regs;
static unsigned *dc_regs;


/*
 * Register initialization tables for the imstt display.
 *
 * Dot clock rate is 20MHz * (m + 1) / ((n + 1) * (p ? 2 * p : 1)
 * where m = clk[0], n = clk[1], p = clk[2]
 * clk[3] is c, charge pump bias which depends on the VCO frequency  
 */
struct imstt_regvals {
	unsigned short		cfg[8];
	unsigned char		clk[4];
	unsigned long		pitch[3];
} imsttmode;

/* Register values for 1024x768, 75Hz mode (17) */
static struct imstt_regvals imstt_reg_init_17 = {
  { 0x0A, 0x1C, 0x9C, 0xA6, 0x0003, 0x0020, 0x0320, 0x0323 }, 
  { 0x07, 0x00, 0x01, 0x02 },
  { 0x0400, 0x0800, 0x1000 }
};

/* Register values for 832x624, 75Hz mode (13) */
static struct imstt_regvals imstt_reg_init_13 = {
  { 0x05, 0x20, 0x88, 0x90, 0x0003, 0x0028, 0x0298, 0x029B },
  { 0x3E, 0x0A, 0x01, 0x02 },
  { 832, 832 * 2, 832 * 4 }
};

/* Register values for 640x480, 67Hz mode (6) */
static struct imstt_regvals imstt_reg_init_6 = {
  { 0x08, 0x12, 0x62, 0x6C, 0x0003, 0x002A, 0x020A, 0x020C },
  { 0x78, 0x13, 0x02, 0x02 },
  { 640, 640 * 2, 640 * 4 }
};

static struct imstt_regvals *imstt_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&imstt_reg_init_6, // fake'm out
	&imstt_reg_init_6,
	NULL, NULL, NULL,
	NULL, NULL, NULL,
	&imstt_reg_init_13,
	NULL, NULL, NULL,
	&imstt_reg_init_17,
	NULL, NULL, NULL
};

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int
read_imstt_sense()
{
#if 0
	int sense;
	unsigned gio, gioe;

	gio = ld_le32(dc_regs + GIO) & ~0x0038;
	gioe = ld_le32(dc_
	
	out_le32(dc_regs + GIOE, reg);	/* drive all lines high */
	__delay(200);
	out_le32(dc_regs + GIOE, 077);	/* turn off drivers */
	__delay(2000);
	sense = (in_le32(dc_regs + GIOE) & 0x1c0) << 2;

	/* drive each sense line low in turn and collect the other 2 */
	out_le32(dc_regs + GIOE, 033);	/* drive A low */
	__delay(2000);
	sense |= (in_le32(dc_regs + GIOE) & 0xc0) >> 2;
	out_le32(dc_regs + GIOE, 055);	/* drive B low */
	__delay(2000);
	sense |= ((in_le32(dc_regs + GIOE) & 0x100) >> 5)
		| ((in_le32(dc_regs + GIOE) & 0x40) >> 4);
	out_le32(dc_regs + GIOE, 066);	/* drive C low */
	__delay(2000);
	sense |= (in_le32(dc_regs + GIOE) & 0x180) >> 7;

	out_le32(dc_regs + GIOE, 077);	/* turn off drivers */
	return sense;
#else
	return 0;
#endif
}

static inline int imstt_vram_reqd(int vmode, int cmode)
{
	return vmode_attrs[vmode-1].vres
		* imstt_reg_init[vmode-1]->pitch[cmode];
}

void
map_imstt_display(struct device_node *dp)
{
	int i, sense;
	unsigned long addr, size, tmp;
	unsigned char bus, devfn;
	unsigned short cmd;

	if (dp->next != 0)
		printk("Warning: only using first imstt display device\n");

#if 1
	printk("pmac_display_init: node = %p, addrs =", dp->node);
	for (i = 0; i < dp->n_addrs; ++i)
		printk(" %x(%x)", dp->addrs[i].address, dp->addrs[i].size);
	printk(", intrs =");
	for (i = 0; i < dp->n_intrs; ++i)
		printk(" %x", dp->intrs[i]);
	printk("\n");
#endif

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		if (size >= 0x02000000) {
			frame_buffer = ioremap(addr, size);
			dc_regs = (unsigned*)(frame_buffer + 0x00800000);
			cmap_regs = (unsigned char*)(frame_buffer + 0x00840000);

			printk("mapped frame_buffer=%x(%x)", (unsigned)frame_buffer, (unsigned)size);
			printk(" dc_regs=%x, cmap_regs=%x\n", (unsigned)dc_regs, (unsigned)cmap_regs);
		}
	}

	/* enable memory-space accesses using config-space command register */
	if (pci_device_loc(dp, &bus, &devfn) == 0) {
	  pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	  
	  printk("command word 0x%04X\n", cmd);
	  
	  if (cmd != 0xffff) {
	    cmd |= PCI_COMMAND_MEMORY;
	    pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
	  }
	}
	else
	  printk("unable to find pci device\n");

	tmp = in_le32(dc_regs + SSTATUS);
	printk("chip version %ld, ", (tmp & 0x0F00) >> 8);

	tmp = in_le32(dc_regs + PRC);
	total_vram = (tmp & 0x0004) ? 0x000400000L : 0x000200000L;
	printk("VRAM size %ldM\n", total_vram / 0x000100000L);

	sense = read_imstt_sense();
	printk("Monitor sense value = 0x%x, ", sense);
#if 0
	if (video_mode == VMODE_NVRAM) {
		video_mode = nvram_read_byte(NV_VMODE);
		if (video_mode <= 0 || video_mode > VMODE_MAX
		    || imstt_reg_init[video_mode-1] == 0)
			video_mode = VMODE_CHOOSE;
	}
	if (video_mode == VMODE_CHOOSE)
		video_mode = map_monitor_sense(sense);
	if (imstt_reg_init[video_mode-1] == 0)
		video_mode = VMODE_640_480_67;

	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	if (color_mode == CMODE_NVRAM)
		color_mode = nvram_read_byte(NV_CMODE);
	if (color_mode < CMODE_8 || color_mode > CMODE_32)
		color_mode = CMODE_8;
	while (color_mode > CMODE_8
	       && imstt_vram_reqd(video_mode, color_mode) > total_vram)
		--color_mode;

#endif

	video_mode = VMODE_640_480_67;
	color_mode = CMODE_8;
}

static void
set_imstt_clock(unsigned char *params)
{
  cmap_regs[PIDXHI] = 0; eieio();
  cmap_regs[PIDXLO] = PM0; eieio();
  cmap_regs[PIDXDATA] = params[0]; eieio();

  cmap_regs[PIDXLO] = PN0; eieio();
  cmap_regs[PIDXDATA] = params[1]; eieio();

  cmap_regs[PIDXLO] = PP0; eieio();
  cmap_regs[PIDXDATA] = params[2]; eieio();
	
  cmap_regs[PIDXLO] = PC0; eieio();
  cmap_regs[PIDXDATA] = params[3]; eieio();
}

void
imstt_init()
{
  int	                 i, yoff, hres;
  unsigned long	         ctl, pitch, tmp;
  unsigned char	         pformat;
  unsigned               *p;
  struct imstt_regvals   *init;

  if (video_mode <= 0 || video_mode > VMODE_MAX
      || (init = imstt_reg_init[video_mode-1]) == 0)
    panic("imstt: display mode %d not supported", video_mode);

  n_scanlines = vmode_attrs[video_mode-1].vres;
  hres = vmode_attrs[video_mode-1].hres;
  pixel_size = 1 << color_mode;
  line_pitch = init->pitch[color_mode];
  row_pitch = line_pitch * 16;

  /* initialize the card */
  tmp = in_le32(dc_regs + STGCTL);
  out_le32(dc_regs + STGCTL, tmp & ~0x1);
#if 0
  out_le32(dc_regs + SCR, 0);
#endif

  cmap_regs[PPMASK] = 0xFF;
  /* set default values for DAC registers */ 
  cmap_regs[PIDXHI] = 0; eieio();
  for(i = 0; i < sizeof(initregs) / sizeof(*initregs); i++) {
    cmap_regs[PIDXLO] = initregs[i].addr; eieio();
    cmap_regs[PIDXDATA] = initregs[i].value; eieio();
  }
  set_imstt_clock(init->clk);

  switch(color_mode) {
  case CMODE_32:
    ctl = 0x17b5;
    pitch = init->pitch[2] / 4;
    pformat = 0x06;
    break;
  case CMODE_16:
    ctl = 0x17b3;
    pitch = init->pitch[1] / 4;
    pformat = 0x04;
    break;
  case CMODE_8:
  default:
    ctl = 0x17b1;
    pitch = init->pitch[0] / 4;
    pformat = 0x03;
    break;
  }

  out_le32(&dc_regs[HES], init->cfg[0]);
  out_le32(&dc_regs[HEB], init->cfg[1]);
  out_le32(&dc_regs[HSB], init->cfg[2]);
  out_le32(&dc_regs[HT], init->cfg[3]);
  out_le32(&dc_regs[VES], init->cfg[4]);
  out_le32(&dc_regs[VEB], init->cfg[5]);
  out_le32(&dc_regs[VSB], init->cfg[6]);
  out_le32(&dc_regs[VT], init->cfg[7]);
  out_le32(&dc_regs[HCIV], 1);
  out_le32(&dc_regs[VCIV], 1);
  out_le32(&dc_regs[TCDR], 4);
  out_le32(&dc_regs[VIL], 0);
	
  out_le32(&dc_regs[SSR], 0);
  out_le32(&dc_regs[HRIR], 0x0200);
  out_le32(&dc_regs[CMR], 0x01FF);
  out_le32(&dc_regs[SRGCTL], 0x0003);
  if(total_vram == 0x000200000)
     out_le32(&dc_regs[SCR], 0x0059D);
  else {
    pitch /= 2;
    out_le32(&dc_regs[SCR], 0x00D0DC);
  }

  out_le32(&dc_regs[SPR], pitch);

  cmap_regs[PIDXLO] = PPIXREP; eieio();
  cmap_regs[PIDXDATA] = pformat; eieio();


  pmac_init_palette();	/* Initialize colormap */

  out_le32(&dc_regs[STGCTL], ctl);

  yoff = (n_scanlines % 16) / 2;
  fb_start = frame_buffer + yoff * line_pitch;

  /* Clear screen */
  p = (unsigned *)frame_buffer;
  for (i = n_scanlines * line_pitch / sizeof(unsigned); i != 0; --i)
    *p++ = 0;

  display_info.height = n_scanlines;
  display_info.width = hres;
  display_info.depth = pixel_size * 8;
  display_info.pitch = line_pitch;
  display_info.mode = video_mode;
  strncpy(display_info.name, "IMS,tt128mb", sizeof(display_info.name));
  display_info.fb_address = (unsigned long) frame_buffer;
  display_info.cmap_adr_address = (unsigned long) &cmap_regs[PADDRW];
  display_info.cmap_data_address = (unsigned long) &cmap_regs[PDATA];
  display_info.disp_reg_address = (unsigned long) NULL;
}

int
imstt_setmode(struct vc_mode *mode, int doit)
{
	int cmode;

	if (mode->mode <= 0 || mode->mode > VMODE_MAX
	    || imstt_reg_init[mode->mode-1] == 0)
		return -EINVAL;
	switch (mode->depth) {
	case 24:
	case 32:
		cmode = CMODE_32;
		break;
	case 16:
		cmode = CMODE_16;
		break;
	case 8:
	case 0:		/* (default) */
		cmode = CMODE_8;
		break;
	default:
		return -EINVAL;
	}
	if (imstt_vram_reqd(mode->mode, cmode) > total_vram)
		return -EINVAL;
	if (doit) {
		video_mode = mode->mode;
		color_mode = cmode;
		imstt_init();
	}
	return 0;
}

void
imstt_set_palette(unsigned char red[], unsigned char green[],
		    unsigned char blue[], int index, int ncolors)
{
	int i;

	for (i = 0; i < ncolors; ++i) {
		cmap_regs[PADDRW] = index + i;	eieio();
		cmap_regs[PDATA] = red[i];	eieio();
		cmap_regs[PDATA] = green[i];	eieio();
		cmap_regs[PDATA] = blue[i];	eieio();
	}
}

void
imstt_set_blanking(int blank_mode)
{
	long ctrl;

	ctrl = ld_le32(dc_regs + STGCTL) | 0x0030;
	if (blank_mode & VESA_VSYNC_SUSPEND)
		ctrl &= ~0x0020;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x0010;
	out_le32(dc_regs + STGCTL, ctrl);
}
