/*
 * valkyrie.c: Console support for PowerMac "valkyrie" display adaptor.
 *
 * Copyright (C) 1997 Paul Mackerras.
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
#include <linux/nvram.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <linux/selection.h>
#include "pmac-cons.h"
#include "valkyrie.h"

/*
 * Structure of the registers for the Valkyrie colormap registers.
 */
struct cmap_regs {
	unsigned char addr;
	char pad1[7];
	unsigned char lut;
};

/*
 * Structure of the registers for the "valkyrie" display adaptor.
 */
#define PAD(x)	char x[7]

struct valkyrie_regs {
	unsigned char mode;
	PAD(pad0);
	unsigned char depth;
	PAD(pad1);
	unsigned char status;
	PAD(pad2);
	unsigned char reg3;
	PAD(pad3);
	unsigned char intr;
	PAD(pad4);
	unsigned char reg5;
	PAD(pad5);
	unsigned char intr_enb;
	PAD(pad6);
	unsigned char msense;
	PAD(pad7);
};

static void set_valkyrie_clock(unsigned char *params);
static int read_valkyrie_sense(void);

static unsigned char *frame_buffer;
static struct cmap_regs *cmap_regs;
static struct valkyrie_regs *disp_regs;

/*
 * Register initialization tables for the valkyrie display.
 *
 * Dot clock rate is
 * 3.9064MHz * 2**clock_params[2] * clock_params[1] / clock_params[0].
 */
struct valkyrie_regvals {
	unsigned char mode;
	unsigned char clock_params[3];
	int	pitch[2];		/* bytes/line, indexed by color_mode */
};

/* Register values for 1024x768, 72Hz mode (15) */
static struct valkyrie_regvals valkyrie_reg_init_15 = {
	15,
	{ 12, 30, 3 },	/* pixel clock = 78.12MHz for V=72.12Hz */
	{ 1024, 0 }
};

/* Register values for 1024x768, 60Hz mode (14) */
static struct valkyrie_regvals valkyrie_reg_init_14 = {
	14,
	{ 15, 31, 3 },	/* pixel clock = 64.58MHz for V=59.62Hz */
	{ 1024, 0 }
};

/* Register values for 832x624, 75Hz mode (13) */
static struct valkyrie_regvals valkyrie_reg_init_13 = {
	9,
	{ 23, 42, 3 },	/* pixel clock = 57.07MHz for V=74.27Hz */
	{ 832, 0 }
};

/* Register values for 800x600, 72Hz mode (11) */
static struct valkyrie_regvals valkyrie_reg_init_11 = {
	13,
	{ 17, 27, 3 },	/* pixel clock = 49.63MHz for V=71.66Hz */
	{ 800, 0 }
};

/* Register values for 800x600, 60Hz mode (10) */
static struct valkyrie_regvals valkyrie_reg_init_10 = {
	12,
	{ 20, 53, 2 },	/* pixel clock = 41.41MHz for V=59.78Hz */
	{ 800, 0 }
};

/* Register values for 640x480, 67Hz mode (6) */
static struct valkyrie_regvals valkyrie_reg_init_6 = {
	6,
	{ 14, 27, 2 },	/* pixel clock = 30.13MHz for V=66.43Hz */
	{ 640, 1280 }
};

/* Register values for 640x480, 60Hz mode (5) */
static struct valkyrie_regvals valkyrie_reg_init_5 = {
	11,
	{ 23, 37, 2 },	/* pixel clock = 25.14MHz for V=59.85Hz */
	{ 640, 1280 }
};

static struct valkyrie_regvals *valkyrie_reg_init[20] = {
	NULL, NULL, NULL, NULL,
	&valkyrie_reg_init_5,
	&valkyrie_reg_init_6,
	NULL, NULL, NULL,
	&valkyrie_reg_init_10,
	&valkyrie_reg_init_11,
	NULL,
	&valkyrie_reg_init_13,
	&valkyrie_reg_init_14,
	&valkyrie_reg_init_15,
	NULL, NULL, NULL, NULL, NULL
};

/*
 * Get the monitor sense value.
 */
static int
read_valkyrie_sense()
{
	int sense;

	out_8(&disp_regs->msense, 0);	/* release all lines */
	__delay(20000);
	sense = (in_8(&disp_regs->msense) & 0x70) << 4;

	/* drive each sense line low in turn and collect the other 2 */
	out_8(&disp_regs->msense, 4);	/* drive A low */
	__delay(20000);
	sense |= in_8(&disp_regs->msense) & 0x30;
	out_8(&disp_regs->msense, 2);	/* drive B low */
	__delay(20000);
	sense |= ((in_8(&disp_regs->msense) & 0x40) >> 3)
		| ((in_8(&disp_regs->msense) & 0x10) >> 2);
	out_8(&disp_regs->msense, 1);	/* drive C low */
	__delay(20000);
	sense |= (in_8(&disp_regs->msense) & 0x60) >> 5;

	out_8(&disp_regs->msense, 7);
	return sense;
}

void
map_valkyrie_display(struct device_node *dp)
{
	int sense;
	unsigned long addr;

	if (dp->next != 0)
		printk("Warning: only using first valkyrie display device\n");
	if (dp->n_addrs != 1)
		panic("expecting 1 address for valkyrie (got %d)", dp->n_addrs);

	/* Map in frame buffer and registers */
	addr = dp->addrs[0].address;
	frame_buffer = ioremap(addr, 0x100000);
	disp_regs = ioremap(addr + 0x30a000, 4096);
	cmap_regs = ioremap(addr + 0x304000, 4096);

	/* Read the monitor sense value and choose the video mode */
	sense = read_valkyrie_sense();
	if (video_mode == VMODE_NVRAM) {
		video_mode = nvram_read_byte(NV_VMODE);
		if (video_mode <= 0 || video_mode > VMODE_MAX
		    || valkyrie_reg_init[video_mode-1] == 0)
			video_mode = VMODE_CHOOSE;
	}
	if (video_mode == VMODE_CHOOSE)
		video_mode = map_monitor_sense(sense);
	if (valkyrie_reg_init[video_mode-1] == 0)
		video_mode = VMODE_640_480_60;

	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	if (color_mode == CMODE_NVRAM)
		color_mode = nvram_read_byte(NV_CMODE);
	if (color_mode < CMODE_8 || color_mode > CMODE_16
	    || valkyrie_reg_init[video_mode-1]->pitch[color_mode] == 0)
		color_mode = CMODE_8;

	printk("Monitor sense value = 0x%x, ", sense);
}

static void
set_valkyrie_clock(unsigned char *params)
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

void
valkyrie_init()
{
	int i, yoff, hres;
	unsigned *p;
	struct valkyrie_regvals *init;

	if (video_mode <= 0 || video_mode > VMODE_MAX
	    || (init = valkyrie_reg_init[video_mode-1]) == 0)
		panic("valkyrie: display mode %d not supported", video_mode);
	n_scanlines = vmode_attrs[video_mode-1].vres;
	hres = vmode_attrs[video_mode-1].hres;
	pixel_size = 1 << color_mode;
	line_pitch = init->pitch[color_mode];
	row_pitch = line_pitch * 16;

	/* Reset the valkyrie */
	out_8(&disp_regs->status, 0);
	udelay(100);

	/* Initialize display timing registers */
	out_8(&disp_regs->mode, init->mode | 0x80);
	out_8(&disp_regs->depth, color_mode + 3);
	set_valkyrie_clock(init->clock_params);
	udelay(100);

	pmac_init_palette();	/* Initialize colormap */

	/* Turn on display */
	out_8(&disp_regs->mode, init->mode);

	yoff = (n_scanlines % 16) / 2;
	fb_start = frame_buffer + yoff * line_pitch + 0x1000;

	/* Clear screen */
	p = (unsigned *) (frame_buffer + 0x1000);
	for (i = n_scanlines * line_pitch / sizeof(unsigned); i != 0; --i)
		*p++ = 0;

	display_info.height = n_scanlines;
	display_info.width = hres;
	display_info.depth = pixel_size * 8;
	display_info.pitch = line_pitch;
	display_info.mode = video_mode;
	strncpy(display_info.name, "valkyrie", sizeof(display_info.name));
	display_info.fb_address = (unsigned long) frame_buffer + 0x1000;
	display_info.cmap_adr_address = (unsigned long) &cmap_regs->addr;
	display_info.cmap_data_address = (unsigned long) &cmap_regs->lut;
	display_info.disp_reg_address = (unsigned long) &disp_regs;
}

int
valkyrie_setmode(struct vc_mode *mode, int doit)
{
	int cmode;

	switch (mode->depth) {
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
	if (mode->mode <= 0 || mode->mode > VMODE_MAX
	    || valkyrie_reg_init[mode->mode-1] == 0
	    || valkyrie_reg_init[mode->mode-1]->pitch[cmode] == 0)
		return -EINVAL;
	if (doit) {
		video_mode = mode->mode;
		color_mode = cmode;
		valkyrie_init();
	}
	return 0;
}

void
valkyrie_set_palette(unsigned char red[], unsigned char green[],
		    unsigned char blue[], int index, int ncolors)
{
	int i;

	for (i = 0; i < ncolors; ++i) {
		out_8(&cmap_regs->addr, index + i);
		udelay(1);
		out_8(&cmap_regs->lut, red[i]);
		out_8(&cmap_regs->lut, green[i]);
		out_8(&cmap_regs->lut, blue[i]);
	}
}

void
valkyrie_set_blanking(int blank_mode)
{
	/* don't know how to do this yet */
}
