/*
 * aty.c: Console support for ATI/mach64 display adaptor cards.
 *
 * Copyright (C) 1997 Michael AK Tesch
 *  written with much help from Jon Howell
 *  changes to support the vt chip set by harry ac eaton
 *  gt chipset support, scrollback console by anthony tong <atong@uiuc.edu>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h> /* for CONFIG_CHIP_ID and CONFIG_STAT0 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/vc_ioctl.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#include <linux/selection.h>
#include <linux/vt_kern.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/pci-bridge.h>
#include <asm/init.h>
#include "pmac-cons.h"
#include "aty.h"

struct aty_cmap_regs {
	unsigned char windex;
	unsigned char lut;
	unsigned char mask;
	unsigned char rindex;
	unsigned char cntl;
};

typedef struct aty_regvals {
	int offset[3];		/* first pixel address */

	int crtc_h_sync_strt_wid[3];	/* depth dependent */
	int crtc_gen_cntl[3];
	int mem_cntl[3];

	int crtc_h_tot_disp;	/* mode dependent */
	int crtc_v_tot_disp;
	int crtc_v_sync_strt_wid;
	int crtc_off_pitch;

	unsigned char clock_val[2];	/* vals for 20 and 21 */
} aty_regvals;

struct rage_regvals {
	int h_total, h_sync_start, h_sync_width;
	int v_total, v_sync_start, v_sync_width;
	int h_sync_neg, v_sync_neg;
};

static int aty_vram_reqd(int vmode, int cmode);
static aty_regvals *get_aty_struct(void);

static unsigned char *frame_buffer;
static unsigned long frame_buffer_phys;
static int total_vram;		/* total amount of video memory, bytes */
static int chip_type;		/* what chip type was detected */

static unsigned long ati_regbase;
static unsigned long ati_regbase_phys;
static struct aty_cmap_regs *aty_cmap_regs;

#if 0
/* this array contains the number of bytes/line for each mode and color depth */
static int pitch[20][3] = {
	{512, 1024, 2048},		/* mode 1 */
	{512, 1024, 2048},		/* mode 2 */
	{640, 1024, 2048},		/* mode 3 */
	{640, 1024, 2048},		/* mode 4 */
	{640, 1280, 2560},		/* mode 5 */
	{640, 1280, 2560},		/* mode 6 */
	{640, 1280, 2560},		/* mode 7 */
	{800, 1600, 3200},		/* mode 8 */
	{768, 1536, 2072},		/* mode 9 */
	{800, 1600, 3200},		/* mode 10 */
	{800, 1600, 3200},		/* mode 11 */
	{800, 1600, 3200},		/* mode 12 */
	{832, 1664, 3328},		/* mode 13 */
	{1024, 2048, 4096},		/* mode 14 */
	{1024, 2048, 4096},		/* mode 15 */
	{1024, 2048, 4096},		/* mode 16 */
	{1024, 2048, 4096},		/* mode 17 */
	{1152, 2304, 4608},		/* mode 18 */
	{1280, 2560, 5120},		/* mode 19 */
	{1280, 2560, 5120}		/* mode 20 */
};
#endif

#include "ati-gx.h"
#include "ati-gt.h"
#include "ati-vt.h"

static struct aty_regvals *aty_gt_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&aty_gt_reg_init_5,
	&aty_gt_reg_init_6,
	NULL, NULL,
	&aty_gt_reg_init_9,
	&aty_gt_reg_init_10,
	&aty_gt_reg_init_11,
	&aty_gt_reg_init_12,
	&aty_gt_reg_init_13,
	&aty_gt_reg_init_14,
	&aty_gt_reg_init_15,
	NULL,
	&aty_gt_reg_init_17,
	&aty_gt_reg_init_18,
	NULL,
	&aty_gt_reg_init_20
};

static struct aty_regvals *aty_gx_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&aty_gx_reg_init_6,
	&aty_gx_reg_init_6,
	NULL, NULL, NULL, NULL, NULL, NULL,
	&aty_gx_reg_init_13,
	&aty_gx_reg_init_14,
	&aty_gx_reg_init_15,
	NULL,
	&aty_gx_reg_init_17,
	&aty_gx_reg_init_18,
	NULL,
	&aty_gx_reg_init_20
};

static struct aty_regvals *aty_vt_reg_init[21] = {
	NULL, NULL, NULL, NULL,
	&aty_vt_reg_init_5,
	&aty_vt_reg_init_6,
	NULL, NULL, NULL,
	&aty_vt_reg_init_10,
	&aty_vt_reg_init_11,
	&aty_vt_reg_init_12,
	&aty_vt_reg_init_13,
	&aty_vt_reg_init_14,
	&aty_vt_reg_init_15,
	NULL,
	&aty_vt_reg_init_17,
	&aty_vt_reg_init_18,
	&aty_vt_reg_init_19,
	&aty_vt_reg_init_20
};

__openfirmware

static inline int
aty_vram_reqd(int vmode, int cmode)
{
	return vmode_attrs[vmode - 1].vres * 
		(vmode_attrs[vmode - 1].hres << cmode);
}

extern inline unsigned aty_ld_le32(volatile unsigned long addr)
{
	register unsigned long temp = ati_regbase,val;

	asm("lwbrx %0,%1,%2": "=r"(val):"r"(addr), "r"(temp));
	return val;
}

extern inline void aty_st_le32(volatile unsigned long addr, unsigned val)
{
	register unsigned long temp = ati_regbase;
	asm("stwbrx %0,%1,%2": : "r"(val), "r"(addr), "r"(temp):"memory");
}

extern inline unsigned char aty_ld_8(volatile unsigned long addr)
{
	return *(char *) ((long) addr + (long) ati_regbase);
}

extern inline void aty_st_8(volatile unsigned long addr, unsigned char val)
{
	*(unsigned char *) (addr + (unsigned long) ati_regbase) = val;
}

static void aty_st_514(int offset, char val)
{
	aty_WaitQueue(5);
	aty_st_8(DAC_CNTL, 1);
	aty_st_8(DAC_W_INDEX, offset & 0xff);	/* right addr byte */
	aty_st_8(DAC_DATA, (offset >> 8) & 0xff);	/* left addr byte */
	eieio();
	aty_st_8(DAC_MASK, val);
	eieio();
	aty_st_8(DAC_CNTL, 0);
}

static void
aty_st_pll(int offset, char val)
{
	aty_WaitQueue(3);
	aty_st_8(CLOCK_CNTL + 1, (offset << 2) | PLL_WR_EN);	/* write addr byte */
	eieio();
	aty_st_8(CLOCK_CNTL + 2, val);	/* write the register value */
	eieio();
	aty_st_8(CLOCK_CNTL + 1, (offset << 2) & ~PLL_WR_EN);
}

#if 0 // unused
static char
aty_ld_pll(int offset)
{
	aty_WaitQueue(2);
	aty_st_8(CLOCK_CNTL + 1, offset << 2);
	eieio();
	return aty_ld_8(CLOCK_CNTL + 2);
}
#endif

unsigned char
aty_ld_514(int offset)
{
/* do the same thing as aty_st_514, just read the DAC_MASK instead of writing */
	char val;

	aty_WaitQueue(5);
	aty_st_8(DAC_CNTL, 1);
	aty_st_8(DAC_W_INDEX, offset & 0xff);	/* right addr byte */
	aty_st_8(DAC_DATA, (offset >> 8) & 0xff);	/* left addr byte */
	val = aty_ld_8(DAC_MASK);
	eieio();
	aty_st_8(DAC_CNTL, 0);
	return val;
}

void
aty_set_palette(unsigned char red[], unsigned char green[],
		unsigned char blue[], int index, int ncolors)
{
	int i, scale;

	aty_WaitQueue(2);

	i = aty_ld_8(DAC_CNTL) & 0xfc;
	if (chip_type == MACH64_GT_ID)
		i |= 0x2;		/*DAC_CNTL|0x2 turns off the extra brightness for gt*/
	aty_st_8(DAC_CNTL, i);
	aty_st_8(DAC_REGS + DAC_MASK, 0xff);
	eieio();
	scale = (chip_type != MACH64_GX_ID)
		? ((color_mode == CMODE_16) ? 3 : 0) : 0;

	for (i = 0; i < ncolors; ++i) {
		aty_WaitQueue(4);
		aty_cmap_regs->windex = (index + i) << scale; eieio();
		aty_cmap_regs->lut = red[i];	eieio();
		aty_cmap_regs->lut = green[i];	eieio();
		aty_cmap_regs->lut = blue[i];	eieio();
	}
}

static aty_regvals
*get_aty_struct()
{
	int v = video_mode - 1;

	switch (chip_type) {
	case MACH64_GT_ID:
		return aty_gt_reg_init[v];
		break;
	case MACH64_VT_ID:
		return aty_vt_reg_init[v];
		break;
	default: /* default to MACH64_GX_ID */
		return aty_gx_reg_init[v];
		break;
	}
}

static int
read_aty_sense(void)
{
	int sense, i;

	aty_st_le32(MON_SENSE, 0x31003100);	/* drive outputs high */
	__delay(200);
	aty_st_le32(MON_SENSE, 0);		/* turn off outputs */
	__delay(2000);
	i = aty_ld_le32(MON_SENSE);		/* get primary sense value */
	sense = ((i & 0x3000) >> 3) | (i & 0x100);

	/* drive each sense line low in turn and collect the other 2 */
	aty_st_le32(MON_SENSE, 0x20000000);	/* drive A low */
	__delay(2000);
	i = aty_ld_le32(MON_SENSE);
	sense |= ((i & 0x1000) >> 7) | ((i & 0x100) >> 4);
	aty_st_le32(MON_SENSE, 0x20002000);	/* drive A high again */
	__delay(200);

	aty_st_le32(MON_SENSE, 0x10000000);	/* drive B low */
	__delay(2000);
	i = aty_ld_le32(MON_SENSE);
	sense |= ((i & 0x2000) >> 10) | ((i & 0x100) >> 6);
	aty_st_le32(MON_SENSE, 0x10001000);	/* drive B high again */
	__delay(200);

	aty_st_le32(MON_SENSE, 0x01000000);	/* drive C low */
	__delay(2000);
	sense |= (aty_ld_le32(MON_SENSE) & 0x3000) >> 12;
	aty_st_le32(MON_SENSE, 0);		/* turn off outputs */

	return sense;
}

void
map_aty_display(struct device_node *dp)
{
	struct aty_regvals *init;
	int i, sense;
	unsigned long addr;
	unsigned char bus, devfn;
	unsigned short cmd;

	if (dp->next != 0)
	printk("Warning: only using first ATI card detected\n");
	if (dp->n_addrs != 1 && dp->n_addrs != 3)
	printk("Warning: expecting 1 or 3 addresses for ATY (got %d)",
	       dp->n_addrs);

	ati_regbase_phys = 0x7ffc00 + dp->addrs[0].address;
	ati_regbase = (int) ioremap(ati_regbase_phys, 0x1000);
	aty_cmap_regs = (struct aty_cmap_regs *) (ati_regbase + 0xC0);

	/* enable memory-space accesses using config-space command register */
	if (pci_device_loc(dp, &bus, &devfn) == 0) {
		pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
		if (cmd != 0xffff) {
			cmd |= PCI_COMMAND_MEMORY;
			pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
		}
	}
	chip_type = (aty_ld_le32(CONFIG_CHIP_ID) & CFG_CHIP_TYPE);

	i = aty_ld_le32(MEM_CNTL);
	if (chip_type != MACH64_GT_ID)
		switch (i & MEM_SIZE_ALIAS) {
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
	else
		switch (i & 0xF) { /* 0xF used instead of MEM_SIZE_ALIAS */
		case MEM_SIZE_512K:
			total_vram = 0x80000;
			break;
		case MEM_SIZE_1M:
			total_vram = 0x100000;
			break;
		case MEM_SIZE_2M_GTB:
			total_vram = 0x200000;
			break;
		case MEM_SIZE_4M_GTB:
			total_vram = 0x400000;
			break;
		case MEM_SIZE_6M_GTB:
			total_vram = 0x600000;
			break;
		case MEM_SIZE_8M_GTB:
			total_vram = 0x800000;
			break;
		default:
			total_vram = 0x80000;
		}

#if 0
	printk("aty_display_init: node = %p, addrs = ", dp->node);
	printk(" %x(%x)", dp->addrs[0].address, dp->addrs[0].size);
	printk(", intrs =");
	for (i = 0; i < dp->n_intrs; ++i)
	printk(" %x", dp->intrs[i].line);
	printk("\nregbase: %x pci loc: %x:%x total_vram: %x cregs: %x\n", (int) ati_regbase,
		bus, devfn, total_vram, (int) aty_cmap_regs);
#endif
	/* Map in frame buffer */
	addr = dp->addrs[0].address;

	/* use the big-endian aperture (??) */
	addr += 0x800000;
	frame_buffer_phys = addr;
	frame_buffer = __ioremap(addr, 0x800000, _PAGE_WRITETHRU);

	sense = read_aty_sense();
	printk(KERN_INFO "monitor sense = %x\n", sense);
	if (video_mode == VMODE_NVRAM) {
		video_mode = nvram_read_byte(NV_VMODE);
		init = get_aty_struct();
		if (video_mode <= 0 || video_mode > VMODE_MAX || init == 0)
			video_mode = VMODE_CHOOSE;
	}
	if (video_mode == VMODE_CHOOSE)
		video_mode = map_monitor_sense(sense);

	init = get_aty_struct();
	if (!init)
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
		if (aty_vram_reqd(video_mode, color_mode) > total_vram) {
			do {
				video_mode--;
				init = get_aty_struct();
			} while ((init == 0) && (video_mode > VMODE_640_480_60));
		}
	}

	if (chip_type == MACH64_GT_ID && (aty_ld_le32(CONFIG_STAT0) & 7) == 5
			&& init->crtc_gen_cntl[1] == 0) {
		video_mode = 6; color_mode = 0;
	}
	return;
}

void
RGB514_Program(int cmode)
{
	typedef struct {
		char pixel_dly;
		char misc2_cntl;
		char pixel_rep;
		char pixel_cntl_index;
		char pixel_cntl_v1;
	} RGB514_DAC_Table;

	static RGB514_DAC_Table RGB514DAC_Tab[8] = {
		{0, 0x41, 0x03, 0x71, 0x45},	// 8bpp
		{0, 0x45, 0x04, 0x0c, 0x01},	// 555
		{0, 0x45, 0x06, 0x0e, 0x00},	// XRGB
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

void
aty_init()
{
	int i, hres;
	struct aty_regvals *init = get_aty_struct();
	int vram_type = aty_ld_le32(CONFIG_STAT0) & 7;

	if (init == 0) /* paranoia, shouldn't get here */
		panic("aty: display mode %d not supported", video_mode);

	n_scanlines = vmode_attrs[video_mode - 1].vres;
	hres = vmode_attrs[video_mode - 1].hres;
	pixel_size = 1 << color_mode;
	line_pitch = vmode_attrs[video_mode - 1].hres << color_mode;
	row_pitch = line_pitch * 16;

	/* clear FIFO errors */
	aty_st_le32(BUS_CNTL, aty_ld_le32(BUS_CNTL) | BUS_HOST_ERR_ACK
		| BUS_FIFO_ERR_ACK);

	/* Reset engine */
	i = aty_ld_le32(GEN_TEST_CNTL);
	aty_st_le32(GEN_TEST_CNTL, i & ~GUI_ENGINE_ENABLE);
	eieio();
	aty_WaitIdleEmpty();
	aty_st_le32(GEN_TEST_CNTL, i | GUI_ENGINE_ENABLE);
	aty_WaitIdleEmpty();

	if ( chip_type != MACH64_GT_ID ) {
		i = aty_ld_le32(CRTC_GEN_CNTL);
		aty_st_le32(CRTC_GEN_CNTL, i | CRTC_EXT_DISP_EN);
	}

	if ( chip_type == MACH64_GX_ID ) { 
		i = aty_ld_le32(GEN_TEST_CNTL);
		aty_st_le32(GEN_TEST_CNTL, i | GEN_OVR_OUTPUT_EN );
	}

	switch (chip_type) {
	case MACH64_VT_ID:
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
		break;
	case MACH64_GT_ID:
		if (vram_type == 5) {
			aty_st_pll(0, 0xcd);
			aty_st_pll(PLL_MACRO_CNTL, 
				  video_mode >= VMODE_1024_768_60? 0xd3: 0xd5);
			aty_st_pll(PLL_REF_DIV, 0x21);
			aty_st_pll(PLL_GEN_CNTL, 0x44);
			aty_st_pll(MCLK_FB_DIV, 0xe8);
			aty_st_pll(PLL_VCLK_CNTL, 0x03);
			aty_st_pll(VCLK_POST_DIV, init->offset[0]);
			aty_st_pll(VCLK0_FB_DIV, init->offset[1]);
			aty_st_pll(VCLK1_FB_DIV, 0x8e);
			aty_st_pll(VCLK2_FB_DIV, 0x9e);
			aty_st_pll(VCLK3_FB_DIV, 0xc6);
			aty_st_pll(PLL_XCLK_CNTL, init->offset[2]);
			aty_st_pll(12, 0xa6);
			aty_st_pll(13, 0x1b);
		} else {
			aty_st_pll(PLL_MACRO_CNTL, 0xd5);
			aty_st_pll(PLL_REF_DIV, 0x21);
			aty_st_pll(PLL_GEN_CNTL, 0xc4); 
			aty_st_pll(MCLK_FB_DIV, 0xda);
			aty_st_pll(PLL_VCLK_CNTL, 0x03);
			/* offset actually holds clock values */
			aty_st_pll(VCLK_POST_DIV, init->offset[0]);
			aty_st_pll(VCLK0_FB_DIV, init->offset[1]);
			aty_st_pll(VCLK1_FB_DIV, 0x8e);
			aty_st_pll(VCLK2_FB_DIV, 0x9e);
			aty_st_pll(VCLK3_FB_DIV, 0xc6);
			aty_st_pll(PLL_TEST_CTRL, 0x0);
			aty_st_pll(PLL_XCLK_CNTL, init->offset[2]);
			aty_st_pll(12, 0xa0);
			aty_st_pll(13, 0x1b);
		}
		break;
	default:
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
		break;
	}

	aty_ld_8(DAC_REGS);	/* clear counter */
	aty_WaitIdleEmpty();

	aty_st_le32(CRTC_H_TOTAL_DISP, init->crtc_h_tot_disp);
	aty_st_le32(CRTC_H_SYNC_STRT_WID, init->crtc_h_sync_strt_wid[color_mode]);
	aty_st_le32(CRTC_V_TOTAL_DISP, init->crtc_v_tot_disp);
	aty_st_le32(CRTC_V_SYNC_STRT_WID, init->crtc_v_sync_strt_wid);

	aty_st_8(CLOCK_CNTL, 0);
	aty_st_8(CLOCK_CNTL, CLOCK_STROBE);

	aty_st_le32(CRTC_VLINE_CRNT_VLINE, 0);

	if (chip_type == MACH64_GT_ID) {
		aty_st_le32(BUS_CNTL, 0x7b23a040);

		/* we calculate this so we can use a scrollback buffer.
		 * this should theoretically work with other ati's
		 * OFF_PITCH == (((hres + 7) & 0xfff8) >> 3) << 22 
		 */
		ati_set_origin(0);

		/* need to set DSP values !! assume sdram */
		i = init->crtc_gen_cntl[0] - (0x100000 * color_mode);
		if ( vram_type == 5 )
			i = init->crtc_gen_cntl[1] - (0x100000 * color_mode);
		aty_st_le32(DSP_CONFIG, i); 

		i = aty_ld_le32(MEM_CNTL) & MEM_SIZE_ALIAS;
		if ( vram_type == 5 ) {
			i |= ((1 * color_mode) << 26) | 0x4215b0;
			aty_st_le32(DSP_ON_OFF,sgram_dsp[video_mode-1][color_mode]);

		//aty_st_le32(CLOCK_CNTL,8192);
		} else {
			i |= ((1 * color_mode) << 26) | 0x300090;
			aty_st_le32(DSP_ON_OFF, init->mem_cntl[color_mode]);
		}

		aty_st_le32(MEM_CNTL, i);
		aty_st_le32(EXT_MEM_CNTL, 0x5000001);

		/* if (total_vram > 0x400000)	
			i |= 0x538; this not been verified on > 4Megs!! */
	} else {
		aty_st_le32(CRTC_OFF_PITCH, init->crtc_off_pitch);

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
		if (chip_type == MACH64_VT_ID)
			aty_st_le32(BUS_CNTL, 0x680000f9);
		else
			aty_st_le32(BUS_CNTL, 0x590e10ff);

		switch (total_vram) {
		case 0x00100000:
			aty_st_le32(MEM_CNTL, vt_mem_cntl[0][color_mode]);
			break;
		case 0x00200000:
			aty_st_le32(MEM_CNTL, vt_mem_cntl[1][color_mode]);
			break;
		case 0x00400000:
			aty_st_le32(MEM_CNTL, vt_mem_cntl[2][color_mode]);
			break;
		default:
			i = aty_ld_le32(MEM_CNTL) & 0x000F;
			aty_st_le32(MEM_CNTL, (init->mem_cntl[color_mode] & 0xFFFFFFF0) | i);
		}
	}
/* These magic constants are harder to figure out
 * on the vt chipset bit 2 set makes the screen brighter
 * and bit 15 makes the screen black! But nothing else
 * seems to matter for the vt DAC_CNTL
 */
	switch (chip_type) {
	case MACH64_GT_ID:
		i = 0x86010102;
		break;
	case MACH64_VT_ID:
		i = 0x87010184;
		break;
	default:
		i = 0x47012100;
		break;
	}

	aty_st_le32(DAC_CNTL, i);
	aty_st_8(DAC_MASK, 0xff);

	switch (color_mode) {
	case CMODE_16:
		i = CRTC_PIX_WIDTH_15BPP; break;
	/*case CMODE_24: */
	case CMODE_32:
		i = CRTC_PIX_WIDTH_32BPP; break;
	case CMODE_8:
	default:
		i = CRTC_PIX_WIDTH_8BPP; break;
	}

	if (chip_type != MACH64_GT_ID) {
		aty_st_le32(CRTC_INT_CNTL, 0x00000002);
		aty_st_le32(GEN_TEST_CNTL, GUI_ENGINE_ENABLE | BLOCK_WRITE_ENABLE);	/* gui_en block_en */
		i |= init->crtc_gen_cntl[color_mode];
	}
	/* Gentlemen, start your crtc engine */
	aty_st_le32(CRTC_GEN_CNTL, CRTC_EXT_DISP_EN | CRTC_EXT_EN | i);
	pmac_init_palette();	/* Initialize colormap */

	/* clear screen */
	fb_start = frame_buffer + (((n_scanlines % 16) * line_pitch) >> 1);
	memsetw((unsigned short *) fb_start, 0, total_vram);

	display_info.height = n_scanlines;
	display_info.width = hres;
	display_info.depth = pixel_size << 3;
	display_info.pitch = line_pitch;
	display_info.mode = video_mode;
	strncpy(display_info.name, "ATY Mach64", sizeof(display_info.name));
	switch ( chip_type ) {
	case MACH64_GX_ID: strcat(display_info.name,"GX");
		break;
	case MACH64_VT_ID: strcat(display_info.name,"VT");
		break;
	case MACH64_GT_ID: strcat(display_info.name,"GT");
		break;
	default: strcat(display_info.name,"unknown");
		break;
	}
	display_info.fb_address = (chip_type != MACH64_GT_ID) ?
		frame_buffer_phys + init->offset[color_mode] :
		frame_buffer_phys;
	display_info.cmap_adr_address = ati_regbase_phys + 0xc0;
	display_info.cmap_data_address = ati_regbase_phys + 0xc1;
	display_info.disp_reg_address = ati_regbase_phys;
}

int
aty_setmode(struct vc_mode *mode, int doit)
{
	int cmode,old_vmode=video_mode;
	struct aty_regvals *init;

#if 1
	if (mode->mode == 21) {
		printk("hace: about to set 0x%x to 0x%x\n", mode->depth, mode->pitch & 0xff);
		aty_st_8(mode->depth, mode->pitch & 0xff);
		return 0;
	}
	if (mode->mode == 0) {
		printk("hace: 0x%x contains 0x%x\n", mode->depth, aty_ld_8(mode->depth));
		return 0;
	}
#endif

	if (mode->mode <= 0 || mode->mode > VMODE_MAX )
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
	if (aty_vram_reqd(mode->mode, cmode) > total_vram)
		return -EINVAL;

	video_mode = mode->mode;
	init = get_aty_struct();

	/* Check if we know about the wanted video mode */
	if ( init == 0 || init->crtc_h_sync_strt_wid[cmode] == 0
	   || (chip_type != MACH64_GT_ID && init->crtc_gen_cntl[cmode] == 0)
	   || (chip_type == MACH64_GT_ID && (aty_ld_le32(CONFIG_STAT0) & 7) == 5
	   && init->crtc_gen_cntl[1] == 0)) {
			video_mode = old_vmode;
			return -EINVAL;
	}

	if (doit) {
		video_mode = mode->mode;
		color_mode = cmode;
		hide_cursor();
		aty_init();
	}
	return 0;
}

void
aty_set_blanking(int blank_mode)
{
	char gen_cntl;

	gen_cntl = aty_ld_8(CRTC_GEN_CNTL);
	if (blank_mode & VESA_VSYNC_SUSPEND)
		gen_cntl |= 0x8;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		gen_cntl |= 0x4;
	if ((blank_mode & VESA_POWERDOWN) == VESA_POWERDOWN)
		gen_cntl |= 0x40;
	if (blank_mode == VESA_NO_BLANKING)
		gen_cntl &= ~(0x4c);
	aty_st_8(CRTC_GEN_CNTL, gen_cntl);
}

/* handle video scrollback; offset is in # of characters */
void
ati_set_origin(unsigned short offset)
{
	register int x = (vmode_attrs[video_mode - 1].hres + 7) & 0xfff8,
		lines = offset / video_num_columns, reg;

	reg = ((x >> 3) << 22) |	/* calculate pitch */
	  ((lines * video_font_height * x * (1<<color_mode)) >> 3); /*offset*/

	aty_st_le32(CRTC_OFF_PITCH, reg);
	aty_st_le32(DST_OFF_PITCH, reg);
	aty_st_le32(SRC_OFF_PITCH, reg);

#if 0
	fb_start = display_info.fb_address = 
		(unsigned long) frame_buffer + ((lines-2) * 
		vmode_attrs[video_mode-1].hres) << (4 + color_mode);
#endif
}

int
ati_vram(void)
{
	return total_vram;
}

