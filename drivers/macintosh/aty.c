/*
 * aty.c: Console support for ATI/mach64 display adaptor cards.
 *
 * Copyright (C) 1997 Michael AK Tesch
 *  written with much help from Jon Howell
 *  changes to support the vt chip set by harry ac eaton
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
#include "aty.h"

struct aty_cmap_regs {
	unsigned char windex;
	unsigned char lut;
	unsigned char mask;
	unsigned char rindex;
	unsigned char cntl;
};

struct aty_regvals {
     int	offset[3];		 /* first pixel address */

     int	crtc_h_sync_strt_wid[3]; /* depth dependant */
     int	crtc_gen_cntl[3];
     int	mem_cntl[3];

     int	crtc_h_tot_disp;         /* mode dependant */
     int	crtc_v_tot_disp;
     int	crtc_v_sync_strt_wid;
     int	crtc_off_pitch;

     unsigned char clock_val[2];        /* vals for 20 and 21 */
};

/*static void set_aty_clock(unsigned char *params);*/
static int aty_vram_reqd(int vmode, int cmode);

static unsigned char *frame_buffer;
static int total_vram;		/* total amount of video memory, bytes */
static int is_vt_chip;		/* whether a vt chip type was detected */

static unsigned long aty_regbase;
static struct aty_cmap_regs *aty_cmap_regs;

/* this array contains the number of bytes/line for each mode and color depth */
static int pitch[20][3] = {
	{0,0,0},  /* mode 1?? */
	{0,0,0},  /* mode 2?? */
	{0,0,0},  /* mode 3??*/
	{0,0,0},  /* mode 4?? */
	{640, 1280, 2560},  /* mode 5 */
	{640, 1280, 2560},  /* mode 6 */
	{640, 1280, 2560},  /* mode 7 */
	{800, 1600, 3200},  /* mode 8 */
	{0,0,0},  /* mode 9 ??*/
	{800, 1600, 3200},  /* mode 10 */
	{800, 1600, 3200},  /* mode 11 */
	{800, 1600, 3200},  /* mode 12 */
	{832, 1664, 3328},  /* mode 13 */
	{1024, 2048, 4096},  /* mode 14 */
	{1024, 2048, 4096},  /* mode 15 */
	{1024, 2048, 4096},  /* mode 16 */
	{1024, 2048, 4096},  /* mode 17 */
	{1152, 2304, 4608},  /* mode 18 */
	{1280, 2560, 5120},  /* mode 19 */
	{1280, 2560, 5120}   /* mode 20 */
};

#include "ati-gt.h"
#include "ati-vt.h"

static struct aty_regvals *aty_gt_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&aty_gt_reg_init_6,
	&aty_gt_reg_init_6,
	NULL,
	NULL, NULL,
	NULL,
	NULL,NULL,
	&aty_gt_reg_init_13,
	&aty_gt_reg_init_14,
	&aty_gt_reg_init_15,
	NULL,
	&aty_gt_reg_init_17,
	NULL,
	NULL,
	&aty_gt_reg_init_20
};

static struct aty_regvals *aty_vt_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&aty_vt_reg_init_5,
	&aty_vt_reg_init_6,
	NULL,
	NULL, NULL,
	&aty_vt_reg_init_10,
	&aty_vt_reg_init_11,
	&aty_vt_reg_init_12,
	&aty_vt_reg_init_13,
	&aty_vt_reg_init_14,
	&aty_vt_reg_init_15,
	NULL,
	&aty_vt_reg_init_17,
	&aty_vt_reg_init_18,
	NULL,
	&aty_vt_reg_init_20
};

static inline int aty_vram_reqd(int vmode, int cmode)
{
	return vmode_attrs[vmode-1].vres
		* pitch[vmode-1][cmode];
}

extern inline unsigned aty_ld_rev(volatile unsigned long addr)
{
     unsigned val;

     (long)addr += (long)aty_regbase;
     asm volatile("lwbrx %0,0,%1" : "=r" (val) : "r" (addr));
     return val;
}

extern inline void aty_st_rev(volatile unsigned long addr, unsigned val)
{
     (long)addr += (long)aty_regbase;
     asm volatile("stwbrx %0,0,%1" : : "r" (val), "r" (addr) : "memory");
}

extern inline unsigned char aty_ld_byte(volatile unsigned long addr)
{
     unsigned char val;

     val = *(char*)((long)addr+(long)aty_regbase);
     return val;
}

extern inline void aty_st_byte(volatile unsigned long addr, unsigned char val)
{
     *(unsigned char*)(addr+(unsigned long)aty_regbase) = val;
}

void
aty_st_514( int offset, char val )
{
     aty_WaitQueue(5);
     aty_st_byte( DAC_CNTL, 1);
     aty_st_byte( DAC_W_INDEX, offset & 0xff );   /* right addr byte */
     aty_st_byte( DAC_DATA, (offset>>8) & 0xff ); /* left addr byte */
     aty_st_byte( DAC_MASK, val );
     aty_st_byte( DAC_CNTL, 0 );
}

void
aty_st_pll( int offset, char val )
{
     aty_WaitQueue(3);
     aty_st_byte( CLOCK_CNTL + 1, (offset << 2) | PLL_WR_EN );   /* write addr byte */
     aty_st_byte( CLOCK_CNTL + 2, val); /* write the register value */
     aty_st_byte( CLOCK_CNTL + 1, (offset << 2) & ~PLL_WR_EN);
}

#if 0
unsigned char
aty_ld_514( int offset )
{
/* do the same thing as aty_st_514, just read the DAC_MASK instead of writing*/
}
#endif

void
aty_set_palette(unsigned char red[], unsigned char green[],
		unsigned char blue[], int index, int ncolors)
{
     int i,scale;

     aty_WaitQueue(2);
     aty_st_byte(DAC_CNTL, aty_ld_byte(DAC_CNTL) & 0xfc);
     aty_st_byte(DAC_REGS + DAC_MASK, 0xff);
     eieio();
     scale = (is_vt_chip) ? ((color_mode == CMODE_16) ? 3 : 0) : 0;
     for (i = 0; i < ncolors; ++i) {
	  aty_WaitQueue(4);
          aty_cmap_regs->windex = (index + i) << scale;	eieio();
          aty_cmap_regs->lut = red[i];		eieio();
          aty_cmap_regs->lut = green[i];	eieio();
          aty_cmap_regs->lut = blue[i];		eieio();
     }
   
}

void
map_aty_display(struct device_node *dp)
{
     int i, sense;
     unsigned long addr;
     unsigned char bus, devfn;
     unsigned short cmd;

     if (dp->next != 0)
	  printk("Warning: only using first ATI card detected\n");
     if (dp->n_addrs != 1)
	  panic("expecting 1 addresses for ATY (got %d)", dp->n_addrs);

     aty_regbase = (int)ioremap((0x7ffc00 + dp->addrs[0].address), 0x1000);
     aty_cmap_regs = (struct aty_cmap_regs *)(aty_regbase+0xC0);

     /* enable memory-space accesses using config-space command register */
     if (pci_device_loc(dp, &bus, &devfn) == 0) {
	  pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	  if (cmd != 0xffff) {
	       cmd |= PCI_COMMAND_MEMORY;
	       pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
	  }
     }

     switch( aty_ld_rev(MEM_CNTL)&MEM_SIZE_ALIAS ) {
     case MEM_SIZE_512K:
	  total_vram = 0x80000;
	  break;
     case MEM_SIZE_1M:
	  total_vram = 0x100000;
	  break;
     case MEM_SIZE_2M:
	  total_vram = 0x200000;
	  break;
     case MEM_SIZE_4M:
	  total_vram = 0x400000;
	  break;
     case MEM_SIZE_6M:
	  total_vram = 0x600000;
	  break;
     case MEM_SIZE_8M:
	  total_vram = 0x800000;
	  break;
     default:
	  total_vram = 0x80000;
     }
#if 1
     printk("aty_display_init: node = %p, addrs = ", dp->node);
     printk(" %x(%x)", dp->addrs[0].address, dp->addrs[0].size);
     printk(", intrs =");
     for (i = 0; i < dp->n_intrs; ++i)
	  printk(" %x", dp->intrs[i]);
     printk( "\nregbase: %x pci loc: %x:%x total_vram: %x cregs: %x\n", (int)aty_regbase, 
	     bus, devfn, total_vram, (int)aty_cmap_regs );
#endif
     is_vt_chip = ((aty_ld_rev(CONFIG_CHIP_ID) & CFG_CHIP_TYPE) == MACH64_VT_ID);
     /* Map in frame buffer */
     addr = dp->addrs[0].address;

     /* use the big-endian aperture (??) */
     addr += 0x800000;
     frame_buffer = ioremap(addr, 0x800000);
     

     /*	sense = read_aty_sense(); XXX not yet, just give it mine */
     sense = 0x62b;
     if (video_mode == VMODE_NVRAM) {
	  video_mode = nvram_read_byte(NV_VMODE);
	  if (video_mode <= 0 || video_mode > VMODE_MAX
	      || ((is_vt_chip) ? aty_vt_reg_init[video_mode-1] : aty_gt_reg_init[video_mode-1]) == 0)
	       video_mode = VMODE_CHOOSE;
     }
     if (video_mode == VMODE_CHOOSE)
	  video_mode = map_monitor_sense(sense);
     if (((is_vt_chip) ? aty_vt_reg_init[video_mode-1] : aty_gt_reg_init[video_mode-1]) == 0)
	  video_mode = VMODE_640_480_60;

     /*
      * Reduce the pixel size if we don't have enough VRAM.
      */

     if (color_mode == CMODE_NVRAM)
        color_mode = nvram_read_byte(NV_CMODE);
     if (color_mode < CMODE_8 || color_mode > CMODE_32)
	color_mode = CMODE_8;
     while (aty_vram_reqd(video_mode, color_mode) > total_vram) {
     	while (color_mode > CMODE_8
	    && aty_vram_reqd(video_mode, color_mode) > total_vram)
	  --color_mode;
        /*
	 * adjust the video mode smaller if there still is not enough VRAM
	 */
	if (aty_vram_reqd(video_mode, color_mode) > total_vram)
	  while ((((is_vt_chip) ? aty_vt_reg_init[--video_mode - 1]
	        : aty_gt_reg_init[--video_mode -1 ]) == 0) && (video_mode > VMODE_640_480_60)) ;
    }
}

#if 0
static void
set_aty_clock(unsigned char *params)
{
     /* done in aty_init...probably need to change for different modes */
     printk("tried to set ATY clock\n");
}
#endif

void
RGB514_Program(int cmode)
{
     typedef struct {
          char    pixel_dly;
          char    misc2_cntl;
          char    pixel_rep;
          char    pixel_cntl_index;
          char    pixel_cntl_v1;
     } RGB514_DAC_Table;

     static  RGB514_DAC_Table RGB514DAC_Tab[8] = {
          { 0, 0x41, 0x03, 0x71, 0x45 },          // 8bpp
          { 0, 0x45, 0x04, 0x0c, 0x01 },          // 555
          { 0, 0x45, 0x06, 0x0e, 0x00 },          // XRGB
     };
     RGB514_DAC_Table *pDacProgTab;

     pDacProgTab = &RGB514DAC_Tab[cmode];

     aty_st_514(0x90, 0x00);
     aty_st_514(0x04, pDacProgTab->pixel_dly);
     aty_st_514(0x05, 0x00);

     aty_st_514(0x2, 0x1);
     aty_st_514(0x71, pDacProgTab->misc2_cntl);
     aty_st_514(0x0a, pDacProgTab->pixel_rep);

     aty_st_514(pDacProgTab->pixel_cntl_index, pDacProgTab->pixel_cntl_v1);
}

/* The vt chipset seems to need a specialized color table for 15 bit mode */
void
VT_Program(int cmode)
{
#if 0
	int i;
	if (cmode != CMODE_8) {
		aty_WaitQueue(2);
		aty_cmap_regs->mask = 0xff; eieio();
		aty_cmap_regs->windex = 0; eieio();
		for (i = 0; i < 0x100; i++) {
			aty_WaitQueue(3);
			aty_cmap_regs->lut = i;
			aty_cmap_regs->lut = i;
			aty_cmap_regs->lut = i; eieio();
		}
	}
#endif
}

void
aty_init()
{
     int i, yoff, hres;
     unsigned *p;
     struct aty_regvals *init;

     if (video_mode <= 0 || video_mode > VMODE_MAX
	 || (init = ((is_vt_chip) ? aty_vt_reg_init[video_mode-1] : aty_gt_reg_init[video_mode-1])) == 0)
	  panic("aty: display mode %d not supported", video_mode);
     n_scanlines = vmode_attrs[video_mode-1].vres;
     hres = vmode_attrs[video_mode-1].hres;
     pixel_size = 1 << color_mode;
     line_pitch = pitch[video_mode-1][color_mode];
     row_pitch = line_pitch * 16;

     aty_st_rev(BUS_CNTL, aty_ld_rev(BUS_CNTL) | BUS_HOST_ERR_ACK | BUS_FIFO_ERR_ACK);
		
     /* Reset engine */
     i = aty_ld_rev(GEN_TEST_CNTL);
     aty_st_rev(GEN_TEST_CNTL, i & ~GUI_ENGINE_ENABLE);
     eieio();
     aty_WaitIdleEmpty();
     aty_st_rev(GEN_TEST_CNTL, i | GUI_ENGINE_ENABLE);
     aty_WaitIdleEmpty();
			
     i = aty_ld_byte(CRTC_GEN_CNTL+3);
     aty_st_byte(CRTC_GEN_CNTL+3, i | (CRTC_EXT_DISP_EN >> 24));

     i = aty_ld_byte(GEN_TEST_CNTL);
     aty_st_byte(GEN_TEST_CNTL, i | GEN_OVR_OUTPUT_EN );

     if (!is_vt_chip) {		
     	RGB514_Program(color_mode);

     	aty_WaitIdleEmpty();

     	aty_st_514(0x06, 0x02);
     	aty_st_514(0x10, 0x01);
     	aty_st_514(0x70, 0x01);
     	aty_st_514(0x8f, 0x1f);
     	aty_st_514(0x03, 0x00);
     	aty_st_514(0x05, 0x00);
     
     	aty_st_514(0x20, init->clock_val[0]);
    	aty_st_514(0x21, init->clock_val[1]);
     } else {
	VT_Program(color_mode);
    	aty_st_pll(PLL_MACRO_CNTL, 0xb5);
    	aty_st_pll(PLL_REF_DIV, 0x2d);
    	aty_st_pll(PLL_GEN_CNTL, 0x14);
    	aty_st_pll(MCLK_FB_DIV, 0xbd);
    	aty_st_pll(PLL_VCLK_CNTL, 0x0b);
    	aty_st_pll(VCLK_POST_DIV, init->clock_val[0]);
    	aty_st_pll(VCLK0_FB_DIV, init->clock_val[1]);
    	aty_st_pll(VCLK1_FB_DIV, 0xd6);
    	aty_st_pll(VCLK2_FB_DIV, 0xee);
    	aty_st_pll(VCLK3_FB_DIV, 0xf8);
    	aty_st_pll(PLL_XCLK_CNTL, 0x0);
    	aty_st_pll(PLL_TEST_CTRL, 0x0);
     	aty_st_pll(PLL_TEST_COUNT, 0x0);
     }
 	  	
     aty_ld_byte( DAC_REGS ); /* clear counter */
			
     aty_WaitIdleEmpty();

     aty_st_rev(CRTC_H_TOTAL_DISP, init->crtc_h_tot_disp);
     aty_st_rev(CRTC_H_SYNC_STRT_WID, init->crtc_h_sync_strt_wid[color_mode]);
     aty_st_rev(CRTC_V_TOTAL_DISP, init->crtc_v_tot_disp);
     aty_st_rev(CRTC_V_SYNC_STRT_WID, init->crtc_v_sync_strt_wid);

     aty_st_byte(CLOCK_CNTL, 0);
     aty_st_byte(CLOCK_CNTL, CLOCK_STROBE);

     aty_st_rev(CRTC_OFF_PITCH, init->crtc_off_pitch);
     
     aty_st_rev(CRTC_VLINE_CRNT_VLINE, 0x14e01d0);
/* The magic constant below translates into: 
 * 5   = No RDY delay, 1 wait st for mem write, increment during burst transfer
 * 9   = DAC access delayed, 1 wait state for DAC
 * 0   = Disables interupts for FIFO errors
 * e   = Allows FIFO to generate 14 wait states before generating error
 * 1   = DAC snooping disabled, ROM disabled
 * 0   = ROM page at 0 (disabled so doesn't matter)
 * f   = 15 ROM wait states (disabled so doesn't matter) 
 * f   = 15 BUS wait states (I'm not sure this applies to PCI bus types)
 * at some point it would be good to experiment with bench marks to see if
 * we can gain some speed by fooling with the wait states etc.
 */
     aty_st_rev(BUS_CNTL, 0x590e10ff);
     i = aty_ld_rev(MEM_CNTL) & MEM_SIZE_ALIAS;
     if (total_vram >= 0x400000)
        aty_st_rev(MEM_CNTL, (init->mem_cntl[color_mode] &0xffff0000) | 0x0538 | i);
     else
        aty_st_rev(MEM_CNTL, (init->mem_cntl[color_mode] & ~MEM_SIZE_ALIAS) | i ); 
     aty_st_rev(CRTC_INT_CNTL, 0x2);
     aty_WaitIdleEmpty();
/* These magic constants are harder to figure out
 * on the vt chipset bit 3 set makes the screen brighter
 * and bit 15 makes the screen black! But nothing else
 * seems to matter for the vt DAC_CNTL
 */
     if (is_vt_chip)
	aty_st_rev(DAC_CNTL, 0x47012104);
     else
        aty_st_rev(DAC_CNTL, 0x47012100);

     aty_st_byte(DAC_MASK, 0xff);

     aty_st_rev(CRTC_INT_CNTL, 0x00000002);
     
     aty_st_byte(CRTC_FIFO, ((char*)&init->crtc_gen_cntl[color_mode])[1] );
     aty_st_byte(CRTC_PIX_WIDTH, ((char*)&init->crtc_gen_cntl[color_mode])[2] );
     aty_st_byte(CRTC_EXT_DISP, ((char*)&init->crtc_gen_cntl[color_mode])[0] );
     
     aty_st_rev(GEN_TEST_CNTL, 0x300); /* gui_en block_en*/

     pmac_init_palette();		/* Initialize colormap */
     yoff = (n_scanlines % 16) / 2;
     fb_start = frame_buffer + yoff * line_pitch + init->offset[color_mode];

     /* Clear screen */
     p = (unsigned *) fb_start;

     for (i = n_scanlines * line_pitch * pixel_size / sizeof(unsigned); i != 0; --i)
	  *p++ = 0;
     display_info.height = n_scanlines;
     display_info.width = hres;
     display_info.depth = pixel_size * 8;
     display_info.pitch = line_pitch;
     display_info.mode = video_mode;
     strncpy(display_info.name, "ATY Mach64", sizeof(display_info.name));
     display_info.fb_address = (unsigned long) frame_buffer + init->offset[color_mode];
     display_info.cmap_adr_address = (unsigned long) &aty_cmap_regs->windex;
     display_info.cmap_data_address = (unsigned long) &aty_cmap_regs->lut;
     display_info.disp_reg_address = aty_regbase;
}

int
aty_setmode(struct vc_mode *mode, int doit)
{
	int cmode;
#if 0
	if (mode->mode == 21) {
		printk("hace: about to set 0x%x to 0x%x\n",mode->depth, mode->pitch & 0xff);
		aty_st_byte(mode->depth, mode->pitch & 0xff);
		return 0;
	}

	if (mode->mode == 0) {
		printk("hace: 0x%x contains 0x%x\n",mode->depth, aty_ld_byte(mode->depth));
		return 0;
	}
#endif		
	
	if (mode->mode <= 0 || mode->mode > VMODE_MAX
	    || ((is_vt_chip) ? aty_vt_reg_init[mode->mode-1] : aty_gt_reg_init[mode->mode-1]) == NULL)
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
	if (aty_vram_reqd(mode->mode, cmode) > total_vram) {
		return -EINVAL;
	}
	if (doit) {
		video_mode = mode->mode;
		color_mode = cmode;
		aty_init();
	}
	return 0;
}

void
aty_set_blanking(int blank_mode)
{
	char gen_cntl;

	gen_cntl = aty_ld_byte(CRTC_GEN_CNTL);
	if (blank_mode & VESA_VSYNC_SUSPEND)
		gen_cntl |= 0x8;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		gen_cntl |= 0x4;
	if ((blank_mode & VESA_POWERDOWN) == VESA_POWERDOWN)
		gen_cntl |= 0x40;
	if (blank_mode == VESA_NO_BLANKING)
		gen_cntl &= ~(0x4c);
	aty_st_byte(CRTC_GEN_CNTL, gen_cntl);
}
